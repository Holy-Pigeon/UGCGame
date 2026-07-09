#include "GCAIHotReloadSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GCAgentKernel.h"
#include "GCAIHotReloadSettings.h"
#include "GCAIHotfixBridge.h"
#include "GCGameplayCommandSubsystem.h"
#include "HAL/FileManager.h"
#include "JsEnv.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
const FString DefaultModulePrefix = TEXT("AIHotfix/Generated");
const FString AgentRuntimeModuleName = TEXT("AIHotfix/AgentRuntime");
constexpr int64 MaxReadableFileBytes = 64 * 1024;

FString SerializeJsonObject(const TSharedPtr<FJsonObject>& Object)
{
	if (!Object.IsValid())
	{
		return TEXT("{}");
	}

	FString Output;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
	return Output;
}

bool ResolveReadableProjectPath(const FString& ProjectDir, const FString& RequestedPath, FString& OutAbsolutePath)
{
	FString Normalized = RequestedPath.TrimStartAndEnd();
	if (Normalized.IsEmpty())
	{
		return false;
	}

	Normalized.ReplaceInline(TEXT("\\"), TEXT("/"));
	while (Normalized.StartsWith(TEXT("./")))
	{
		Normalized.RightChopInline(2, EAllowShrinking::No);
	}

	if (Normalized.Contains(TEXT("..")))
	{
		return false;
	}

	if (!(Normalized.StartsWith(TEXT("Source/")) ||
		Normalized.StartsWith(TEXT("Config/")) ||
		Normalized.StartsWith(TEXT("Content/")) ||
		Normalized.EndsWith(TEXT(".uproject"))))
	{
		return false;
	}

	const FString AbsolutePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(ProjectDir, Normalized));
	if (!AbsolutePath.StartsWith(ProjectDir))
	{
		return false;
	}

	if (!FPaths::FileExists(AbsolutePath))
	{
		return false;
	}

	const int64 FileSize = IFileManager::Get().FileSize(*AbsolutePath);
	if (FileSize < 0 || FileSize > MaxReadableFileBytes)
	{
		return false;
	}

	OutAbsolutePath = AbsolutePath;
	return true;
}

FVector ReadLocationArgs(const TSharedRef<FJsonObject>& Args, const FVector& Fallback)
{
	FVector Result = Fallback;
	Args->TryGetNumberField(TEXT("x"), Result.X);
	Args->TryGetNumberField(TEXT("y"), Result.Y);
	Args->TryGetNumberField(TEXT("z"), Result.Z);
	return Result;
}
}

void UGCAIHotReloadSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UGCAIHotReloadSettings* Settings = GetDefault<UGCAIHotReloadSettings>();
	ProviderConfig = Settings->DefaultProvider;
	LoadProviderConfigCache();

	Bridge = NewObject<UGCAIHotfixBridge>(this);
	Bridge->Initialize(this);

	Kernel = NewObject<UGCAgentKernel>(this);
	Kernel->OnAssistantDelta.AddUObject(this, &UGCAIHotReloadSubsystem::HandleKernelAssistantDelta);
	Kernel->OnAssistantMessage.AddUObject(this, &UGCAIHotReloadSubsystem::HandleKernelAssistantMessage);
	Kernel->OnToolEvent.AddUObject(this, &UGCAIHotReloadSubsystem::HandleKernelToolEvent);
	Kernel->OnRunFinished.AddUObject(this, &UGCAIHotReloadSubsystem::HandleKernelRunFinished);
	ConfigureKernel();
	RegisterKernelTools();

	FString RuntimeError;
	if (!StartAgentJsRuntime(RuntimeError))
	{
		UE_LOG(LogTemp, Warning, TEXT("[AIHotfix] Agent JS runtime failed to start: %s"), *RuntimeError);
	}
}

void UGCAIHotReloadSubsystem::Deinitialize()
{
	if (Kernel)
	{
		Kernel->Cancel();
	}

	ShutdownJsEnv();
	Super::Deinitialize();
}

void UGCAIHotReloadSubsystem::ConfigureProvider(const FGCAIProviderConfig& InProviderConfig)
{
	ProviderConfig = InProviderConfig;
	SaveProviderConfigCache();
	ConfigureKernel();
}

FGCAIProviderConfig UGCAIHotReloadSubsystem::GetProviderConfig() const
{
	return ProviderConfig;
}

bool UGCAIHotReloadSubsystem::IsRuntimeReady() const
{
	return JsEnv != nullptr;
}

bool UGCAIHotReloadSubsystem::IsAgentTurnRunning() const
{
	return Kernel && Kernel->IsRunning();
}

FString UGCAIHotReloadSubsystem::GetActiveModuleName() const
{
	return ActiveModuleName;
}

FString UGCAIHotReloadSubsystem::GetGeneratedModuleName() const
{
	const UGCAIHotReloadSettings* Settings = GetDefault<UGCAIHotReloadSettings>();
	return NormalizeModuleName(Settings->GeneratedModuleName);
}

FString UGCAIHotReloadSubsystem::GetBootHotfixModuleName() const
{
	if (!BootHotfixModuleName.IsEmpty())
	{
		return BootHotfixModuleName;
	}

	const FString GeneratedModule = GetGeneratedModuleName();
	if (FPaths::FileExists(GetAbsoluteScriptPathForModule(GeneratedModule, TEXT(".js"))))
	{
		return GeneratedModule;
	}

	return FString();
}

bool UGCAIHotReloadSubsystem::ApplyHotfixJavaScript(const FString& ModuleName, const FString& SourceCode)
{
	FString SavedModuleName;
	FString Error;

	if (!WriteHotfixFiles(ModuleName, SourceCode, SavedModuleName, Error))
	{
		OnHotfixFailed.Broadcast(Error);
		return false;
	}

	BootHotfixModuleName = SavedModuleName;
	if (!StartAgentJsRuntime(Error))
	{
		OnHotfixFailed.Broadcast(Error);
		return false;
	}

	OnHotfixApplied.Broadcast(FString::Printf(TEXT("Applied hotfix module %s"), *SavedModuleName));
	return true;
}

void UGCAIHotReloadSubsystem::RestartHotfixRuntime()
{
	if (!ApplyPendingHotfix())
	{
		FString Error;
		if (!StartAgentJsRuntime(Error))
		{
			OnHotfixFailed.Broadcast(Error);
		}
	}
}

void UGCAIHotReloadSubsystem::GenerateHotfixFromPrompt(const FString& Prompt, const FString& ModuleName)
{
	SendAgentPrompt(Prompt, ModuleName);
}

void UGCAIHotReloadSubsystem::SendAgentPrompt(const FString& Prompt, const FString& ModuleName)
{
	const FString TrimmedPrompt = Prompt.TrimStartAndEnd();
	if (TrimmedPrompt.IsEmpty())
	{
		OnHotfixFailed.Broadcast(TEXT("Message is empty."));
		return;
	}

	if (!ProviderConfig.bEnabled)
	{
		OnHotfixFailed.Broadcast(TEXT("AI provider is disabled."));
		return;
	}

	if (ProviderConfig.BaseUrl.IsEmpty() || ProviderConfig.Model.IsEmpty() || ProviderConfig.ApiKey.IsEmpty())
	{
		OnHotfixFailed.Broadcast(TEXT("AI provider is missing Base URL, Model, or Token."));
		return;
	}

	if (IsAgentTurnRunning())
	{
		OnHotfixFailed.Broadcast(TEXT("An AI turn is already running."));
		return;
	}

	CurrentTargetModuleName = NormalizeModuleName(ModuleName.IsEmpty() ? GetGeneratedModuleName() : ModuleName);
	AppendChatMessage(TEXT("user"), TrimmedPrompt);
	ConfigureKernel();

	Kernel->ClearEndpointOverride();
	StartKernelTask(TrimmedPrompt);
}

void UGCAIHotReloadSubsystem::ResetChatSession()
{
	if (Kernel)
	{
		Kernel->ResetSession();
	}
	ChatMessages.Reset();
	BroadcastChatSessionChanged();
}

void UGCAIHotReloadSubsystem::StartKernelTask(const FString& Prompt)
{
	EmitRuntimeLog(FString::Printf(TEXT("Submitting agent task to %s (model %s)."), *ProviderConfig.ProviderId, *ProviderConfig.Model));
	Kernel->RunTask(Prompt);
}

void UGCAIHotReloadSubsystem::ConfigureKernel()
{
	if (!Kernel)
	{
		return;
	}

	const UGCAIHotReloadSettings* Settings = GetDefault<UGCAIHotReloadSettings>();
	const FString SessionPath = Settings->bPersistAgentSession ? GetAgentSessionFilePath() : FString();
	Kernel->Configure(ProviderConfig, SessionPath, Settings->ContextTokenBudget, Settings->MaxAgentSteps, Settings->CompactionKeepRecentSteps);
	Kernel->SetSystemPrompt(BuildAgentSystemPrompt());
}

void UGCAIHotReloadSubsystem::HandleKernelAssistantDelta(const FString& Text)
{
	UpdateStreamingAssistantMessage(Text);
}

void UGCAIHotReloadSubsystem::HandleKernelAssistantMessage(const FString& Text)
{
	if (ChatMessages.Num() > 0 && ChatMessages.Last().Kind == TEXT("streaming"))
	{
		ChatMessages.Last().Content = Text;
		ChatMessages.Last().Kind = TEXT("message");
		BroadcastChatSessionChanged();
		return;
	}

	AppendChatMessage(TEXT("assistant"), Text);
}

void UGCAIHotReloadSubsystem::HandleKernelToolEvent(const FString& ToolName, const FString& Payload, bool bIsResult)
{
	if (ChatMessages.Num() > 0 && ChatMessages.Last().Kind == TEXT("streaming"))
	{
		ChatMessages.RemoveAt(ChatMessages.Num() - 1);
	}

	AppendChatMessage(
		bIsResult ? TEXT("tool") : TEXT("assistant"),
		Payload.IsEmpty() ? TEXT("{}") : Payload,
		bIsResult ? TEXT("tool_result") : TEXT("tool_call"),
		ToolName);
}

void UGCAIHotReloadSubsystem::HandleKernelRunFinished(bool bSuccess, const FString& FinalTextOrError)
{
	if (ChatMessages.Num() > 0 && ChatMessages.Last().Kind == TEXT("streaming"))
	{
		ChatMessages.Last().Kind = TEXT("message");
	}

	if (!bSuccess)
	{
		OnHotfixFailed.Broadcast(FinalTextOrError);
	}

	BroadcastChatSessionChanged();
}

FString UGCAIHotReloadSubsystem::GetHotfixDirectoryOnDisk() const
{
	const UGCAIHotReloadSettings* Settings = GetDefault<UGCAIHotReloadSettings>();
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), Settings->ScriptRoot, TEXT("AIHotfix")));
}

bool UGCAIHotReloadSubsystem::ApplyPendingHotfix()
{
	if (PendingGeneratedModuleName.IsEmpty())
	{
		return false;
	}

	BootHotfixModuleName = PendingGeneratedModuleName;

	FString Error;
	if (!StartAgentJsRuntime(Error))
	{
		OnHotfixFailed.Broadcast(Error);
		return false;
	}

	const FString AppliedModule = PendingGeneratedModuleName;
	PendingGeneratedModuleName.Reset();
	PendingGeneratedSource.Reset();
	OnHotfixApplied.Broadcast(FString::Printf(TEXT("Applied hotfix module %s"), *AppliedModule));
	return true;
}

bool UGCAIHotReloadSubsystem::HasPendingHotfix() const
{
	return !PendingGeneratedModuleName.IsEmpty();
}

FGCAIHotfixGenerationResult UGCAIHotReloadSubsystem::GetLastGeneratedResult() const
{
	return LastGeneratedResult;
}

TArray<FGCAIChatMessage> UGCAIHotReloadSubsystem::GetChatMessages() const
{
	return ChatMessages;
}

void UGCAIHotReloadSubsystem::EmitGameplayCommand(const FString& CommandName, const FString& PayloadJson)
{
	OnGameplayCommand.Broadcast(CommandName, PayloadJson);
}

void UGCAIHotReloadSubsystem::EmitRuntimeLog(const FString& Message)
{
	UE_LOG(LogTemp, Log, TEXT("[AIHotfix] %s"), *Message);
	OnRuntimeLog.Broadcast(Message);
}

void UGCAIHotReloadSubsystem::AppendChatMessage(
	const FString& Role,
	const FString& Content,
	const FString& Kind,
	const FString& Title)
{
	const FString TrimmedContent = Content.TrimStartAndEnd();
	if (TrimmedContent.IsEmpty())
	{
		return;
	}

	FGCAIChatMessage Message;
	Message.Role = Role;
	Message.Content = TrimmedContent;
	Message.Kind = Kind.IsEmpty() ? TEXT("message") : Kind;
	Message.Title = Title;
	ChatMessages.Add(MoveTemp(Message));
	BroadcastChatSessionChanged();
}

void UGCAIHotReloadSubsystem::UpdateStreamingAssistantMessage(const FString& Content)
{
	if (Content.TrimStartAndEnd().IsEmpty())
	{
		return;
	}

	if (ChatMessages.Num() > 0 && ChatMessages.Last().Kind == TEXT("streaming"))
	{
		ChatMessages.Last().Content = Content;
	}
	else
	{
		FGCAIChatMessage Message;
		Message.Role = TEXT("assistant");
		Message.Content = Content;
		Message.Kind = TEXT("streaming");
		ChatMessages.Add(MoveTemp(Message));
	}

	BroadcastChatSessionChanged();
}

void UGCAIHotReloadSubsystem::BroadcastChatSessionChanged()
{
	OnChatSessionChanged.Broadcast();
}

void UGCAIHotReloadSubsystem::LoadProviderConfigCache()
{
	const FString CachePath = GetProviderConfigCachePath();
	FString CacheJson;
	if (!FFileHelper::LoadFileToString(CacheJson, *CachePath))
	{
		return;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CacheJson);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return;
	}

	double CachedTransport = 0.0;
	if (Root->TryGetNumberField(TEXT("transport"), CachedTransport))
	{
		ProviderConfig.Transport = static_cast<EGCAIProviderTransport>(static_cast<int32>(CachedTransport));
	}

	FString CachedBaseUrl;
	if (Root->TryGetStringField(TEXT("base_url"), CachedBaseUrl) && !CachedBaseUrl.IsEmpty())
	{
		ProviderConfig.BaseUrl = CachedBaseUrl;
	}

	FString CachedApiKey;
	if (Root->TryGetStringField(TEXT("api_key"), CachedApiKey) && !CachedApiKey.IsEmpty())
	{
		ProviderConfig.ApiKey = CachedApiKey;
	}

	FString CachedModel;
	if (Root->TryGetStringField(TEXT("model"), CachedModel) && !CachedModel.IsEmpty())
	{
		ProviderConfig.Model = CachedModel;
	}
}

void UGCAIHotReloadSubsystem::SaveProviderConfigCache() const
{
	const FString CachePath = GetProviderConfigCachePath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(CachePath), true);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("provider_id"), ProviderConfig.ProviderId);
	Root->SetStringField(TEXT("api_key"), ProviderConfig.ApiKey);
	Root->SetStringField(TEXT("model"), ProviderConfig.Model);
	Root->SetStringField(TEXT("base_url"), ProviderConfig.BaseUrl);
	Root->SetNumberField(TEXT("transport"), static_cast<int32>(ProviderConfig.Transport));

	FString CacheJson;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&CacheJson);
	FJsonSerializer::Serialize(Root, Writer);
	FFileHelper::SaveStringToFile(CacheJson, *CachePath);
}

FString UGCAIHotReloadSubsystem::GetProviderConfigCachePath() const
{
	const FString RelativePath = ProviderConfig.ApiTokenCachePath.IsEmpty()
		? TEXT("Saved/AI/provider.json")
		: ProviderConfig.ApiTokenCachePath;
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), RelativePath);
}

FString UGCAIHotReloadSubsystem::GetAgentSessionFilePath() const
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), TEXT("Saved/AI/Sessions/default.json"));
}

void UGCAIHotReloadSubsystem::ShutdownJsEnv()
{
	if (Bridge)
	{
		Bridge->ScriptEvalHandler.Unbind();
	}
	JsEnv.Reset();
}

bool UGCAIHotReloadSubsystem::StartAgentJsRuntime(FString& OutError)
{
	ShutdownJsEnv();

	if (!Bridge)
	{
		OutError = TEXT("Hotfix bridge is not available.");
		return false;
	}

	JsEnv = MakeUnique<PUERTS_NAMESPACE::FJsEnv>(TEXT("JavaScript"));

	TArray<TPair<FString, UObject*>> Arguments;
	Arguments.Emplace(TEXT("Bridge"), Bridge);
	Arguments.Emplace(TEXT("GameInstance"), GetGameInstance());

	JsEnv->Start(AgentRuntimeModuleName, Arguments);
	ActiveModuleName = AgentRuntimeModuleName;

	EmitRuntimeLog(FString::Printf(TEXT("Started puerts agent runtime (hotfix module: %s)."),
		GetBootHotfixModuleName().IsEmpty() ? TEXT("none") : *GetBootHotfixModuleName()));
	return true;
}

bool UGCAIHotReloadSubsystem::WriteHotfixFiles(const FString& ModuleName, const FString& SourceCode, FString& OutModuleName, FString& OutError) const
{
	OutModuleName = NormalizeModuleName(ModuleName.IsEmpty() ? GetGeneratedModuleName() : ModuleName);

	const FString JsPath = GetAbsoluteScriptPathForModule(OutModuleName, TEXT(".js"));
	const FString TsPath = GetAbsoluteScriptPathForModule(OutModuleName, TEXT(".ts"));
	const FString Directory = FPaths::GetPath(JsPath);

	IFileManager::Get().MakeDirectory(*Directory, true);

	if (!FFileHelper::SaveStringToFile(SourceCode, *JsPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("Failed to write generated JavaScript to %s"), *JsPath);
		return false;
	}

	FFileHelper::SaveStringToFile(SourceCode, *TsPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	return true;
}

FString UGCAIHotReloadSubsystem::NormalizeModuleName(const FString& ProposedName) const
{
	FString Trimmed = ProposedName.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		Trimmed = GetDefault<UGCAIHotReloadSettings>()->GeneratedModuleName;
	}

	Trimmed.ReplaceInline(TEXT("\\"), TEXT("/"));
	Trimmed.RemoveFromEnd(TEXT(".js"));
	Trimmed.RemoveFromEnd(TEXT(".ts"));

	TArray<FString> Parts;
	Trimmed.ParseIntoArray(Parts, TEXT("/"), true);

	TArray<FString> SanitizedParts;
	for (const FString& Part : Parts)
	{
		SanitizedParts.Add(FPaths::MakeValidFileName(Part, '_'));
	}

	FString Sanitized = FString::Join(SanitizedParts, TEXT("/"));
	if (!Sanitized.StartsWith(TEXT("AIHotfix/")))
	{
		Sanitized = DefaultModulePrefix / Sanitized;
	}

	return Sanitized;
}

FString UGCAIHotReloadSubsystem::GetAbsoluteScriptPathForModule(const FString& ModuleName, const FString& Extension) const
{
	const UGCAIHotReloadSettings* Settings = GetDefault<UGCAIHotReloadSettings>();
	const FString RelativeModulePath = ModuleName + Extension;
	return FPaths::Combine(FPaths::ProjectContentDir(), Settings->ScriptRoot, RelativeModulePath);
}

FString UGCAIHotReloadSubsystem::BuildAgentSystemPrompt() const
{
	return TEXT(
		"You are an in-game AI director and squad commander running inside an Unreal Engine session. "
		"The player talks to you through an in-game chat window; you change the live game world through tools. "
		"Always answer in the language the player used.\n\n"
		"How to work:\n"
		"- Call query_world before acting on the world so your decisions use real state, never guesses.\n"
		"- Prefer the dedicated gameplay tools (spawn_building, spawn_pickup, command_teammate) for their jobs.\n"
		"- run_js executes JavaScript inside the game's puerts runtime for anything the dedicated tools cannot do. "
		"The script runs as CommonJS: const {argv} = require('puerts') gives argv.getByName('Bridge') and "
		"argv.getByName('GameInstance'); require('ue') exposes engine reflection. The last expression's value is "
		"returned to you. Keep scripts short and side-effect focused.\n"
		"- For behaviors that must persist or run every frame, write a hotfix module with write_hotfix_file and "
		"activate it with apply_hotfix. Hotfix modules are CommonJS and should start with "
		"const { argv } = require('puerts'); const bridge = argv.getByName('Bridge');\n"
		"- Tool results are real observations. Never claim an action happened unless the tool result confirms it.\n"
		"- When a request is ambiguous, make a sensible choice and state the assumption instead of stalling.\n"
		"- Keep chat replies short and conversational; do not dump code or raw JSON at the player unless asked.");
}

void UGCAIHotReloadSubsystem::RegisterKernelTools()
{
	if (!Kernel)
	{
		return;
	}

	Kernel->ClearTools();

	auto GetGameplaySubsystem = [this]() -> UGCGameplayCommandSubsystem*
	{
		const UGameInstance* GameInstance = GetGameInstance();
		UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
		return World ? World->GetSubsystem<UGCGameplayCommandSubsystem>() : nullptr;
	};

	auto GetDefaultSpawnLocation = [this]() -> FVector
	{
		const UGameInstance* GameInstance = GetGameInstance();
		UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
		if (APawn* PlayerPawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr)
		{
			return PlayerPawn->GetActorLocation() + PlayerPawn->GetActorForwardVector() * 600.0f;
		}
		return FVector::ZeroVector;
	};

	// --- world-driving tools ---

	{
		FGCAgentToolDefinition Tool;
		Tool.Name = TEXT("query_world");
		Tool.Description = TEXT("Snapshot of the live game world: player position, teammates (with status and carried items), pickups, buildings, hide spots. Call this before acting on the world.");
		Tool.Executor = FGCAgentToolExecutor::CreateWeakLambda(this,
			[GetGameplaySubsystem](const TSharedRef<FJsonObject>& Args, FString& OutResult)
			{
				UGCGameplayCommandSubsystem* Gameplay = GetGameplaySubsystem();
				if (!Gameplay)
				{
					OutResult = TEXT("No game world is active right now.");
					return false;
				}
				OutResult = Gameplay->QueryWorld();
				return true;
			});
		Kernel->RegisterTool(MoveTemp(Tool));
	}

	{
		FGCAgentToolDefinition Tool;
		Tool.Name = TEXT("spawn_building");
		Tool.Description = TEXT("Place a building in the world. Types: house, tower, wall, shelter, platform. Omit x/y/z to build in front of the player. Buildings also serve as hiding cover.");
		Tool.ParametersSchemaJson = TEXT(
			"{\"type\":\"object\",\"properties\":{"
			"\"type\":{\"type\":\"string\",\"description\":\"house|tower|wall|shelter|platform\"},"
			"\"label\":{\"type\":\"string\",\"description\":\"Optional friendly name\"},"
			"\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"},\"z\":{\"type\":\"number\"}"
			"},\"required\":[\"type\"]}");
		Tool.Executor = FGCAgentToolExecutor::CreateWeakLambda(this,
			[GetGameplaySubsystem, GetDefaultSpawnLocation](const TSharedRef<FJsonObject>& Args, FString& OutResult)
			{
				UGCGameplayCommandSubsystem* Gameplay = GetGameplaySubsystem();
				if (!Gameplay)
				{
					OutResult = TEXT("No game world is active right now.");
					return false;
				}

				FString BuildingType;
				Args->TryGetStringField(TEXT("type"), BuildingType);
				FString Label;
				Args->TryGetStringField(TEXT("label"), Label);
				const FVector Location = ReadLocationArgs(Args, GetDefaultSpawnLocation());
				OutResult = Gameplay->SpawnBuilding(BuildingType, Location, Label);
				return !OutResult.StartsWith(TEXT("Error"));
			});
		Kernel->RegisterTool(MoveTemp(Tool));
	}

	{
		FGCAgentToolDefinition Tool;
		Tool.Name = TEXT("spawn_pickup");
		Tool.Description = TEXT("Spawn a pickup item the teammate can fetch. Omit x/y/z to drop it near the player.");
		Tool.ParametersSchemaJson = TEXT(
			"{\"type\":\"object\",\"properties\":{"
			"\"name\":{\"type\":\"string\",\"description\":\"Item name, e.g. medkit\"},"
			"\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"},\"z\":{\"type\":\"number\"}"
			"},\"required\":[\"name\"]}");
		Tool.Executor = FGCAgentToolExecutor::CreateWeakLambda(this,
			[GetGameplaySubsystem, GetDefaultSpawnLocation](const TSharedRef<FJsonObject>& Args, FString& OutResult)
			{
				UGCGameplayCommandSubsystem* Gameplay = GetGameplaySubsystem();
				if (!Gameplay)
				{
					OutResult = TEXT("No game world is active right now.");
					return false;
				}

				FString ItemName;
				Args->TryGetStringField(TEXT("name"), ItemName);
				const FVector Location = ReadLocationArgs(Args, GetDefaultSpawnLocation());
				OutResult = Gameplay->SpawnPickup(ItemName, Location);
				return !OutResult.StartsWith(TEXT("Error"));
			});
		Kernel->RegisterTool(MoveTemp(Tool));
	}

	{
		FGCAgentToolDefinition Tool;
		Tool.Name = TEXT("command_teammate");
		Tool.Description = TEXT("Order an AI teammate (spawned automatically on first use). Actions: move_to (target name or x/y/z), pick_up (target pickup name, or nearest), hide (behind target building, or best cover), follow (player or target), stop. Orders queue up, so you can chain fetch-then-hide.");
		Tool.ParametersSchemaJson = TEXT(
			"{\"type\":\"object\",\"properties\":{"
			"\"teammate\":{\"type\":\"string\",\"description\":\"Teammate name; empty means the default teammate\"},"
			"\"action\":{\"type\":\"string\",\"description\":\"move_to|pick_up|hide|follow|stop\"},"
			"\"target\":{\"type\":\"string\",\"description\":\"Name of a pickup/building/hide spot/player\"},"
			"\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"},\"z\":{\"type\":\"number\"}"
			"},\"required\":[\"action\"]}");
		Tool.Executor = FGCAgentToolExecutor::CreateWeakLambda(this,
			[GetGameplaySubsystem](const TSharedRef<FJsonObject>& Args, FString& OutResult)
			{
				UGCGameplayCommandSubsystem* Gameplay = GetGameplaySubsystem();
				if (!Gameplay)
				{
					OutResult = TEXT("No game world is active right now.");
					return false;
				}

				FString Teammate;
				Args->TryGetStringField(TEXT("teammate"), Teammate);
				FString Action;
				Args->TryGetStringField(TEXT("action"), Action);
				FString Target;
				Args->TryGetStringField(TEXT("target"), Target);
				const FVector Location = ReadLocationArgs(Args, FVector::ZeroVector);
				OutResult = Gameplay->CommandTeammate(Teammate, Action, Target, Location);
				return !OutResult.StartsWith(TEXT("Error"));
			});
		Kernel->RegisterTool(MoveTemp(Tool));
	}

	{
		FGCAgentToolDefinition Tool;
		Tool.Name = TEXT("run_js");
		Tool.Description = TEXT("Execute JavaScript inside the game's puerts runtime and return the result. Full engine access via require('ue') plus argv Bridge/GameInstance. Use for anything the dedicated tools cannot do.");
		Tool.ParametersSchemaJson = TEXT(
			"{\"type\":\"object\",\"properties\":{"
			"\"code\":{\"type\":\"string\",\"description\":\"CommonJS JavaScript source to evaluate\"}"
			"},\"required\":[\"code\"]}");
		Tool.Executor = FGCAgentToolExecutor::CreateWeakLambda(this,
			[this](const TSharedRef<FJsonObject>& Args, FString& OutResult)
			{
				FString Code;
				if (!Args->TryGetStringField(TEXT("code"), Code) || Code.TrimStartAndEnd().IsEmpty())
				{
					OutResult = TEXT("run_js requires non-empty code.");
					return false;
				}

				if (!JsEnv.IsValid())
				{
					FString Error;
					if (!StartAgentJsRuntime(Error))
					{
						OutResult = Error;
						return false;
					}
				}

				return Bridge->ExecuteScript(Code, OutResult);
			});
		Kernel->RegisterTool(MoveTemp(Tool));
	}

	// --- hotfix / project tools ---

	{
		FGCAgentToolDefinition Tool;
		Tool.Name = TEXT("get_runtime_state");
		Tool.Description = TEXT("Hotfix runtime state: active module, pending hotfix, readiness.");
		Tool.Executor = FGCAgentToolExecutor::CreateWeakLambda(this,
			[this](const TSharedRef<FJsonObject>& Args, FString& OutResult)
			{
				const TSharedRef<FJsonObject> State = MakeShared<FJsonObject>();
				State->SetBoolField(TEXT("runtime_ready"), IsRuntimeReady());
				State->SetStringField(TEXT("active_module_name"), GetActiveModuleName());
				State->SetStringField(TEXT("boot_hotfix_module"), GetBootHotfixModuleName());
				State->SetStringField(TEXT("generated_module_name"), GetGeneratedModuleName());
				State->SetBoolField(TEXT("has_pending_hotfix"), HasPendingHotfix());
				OutResult = SerializeJsonObject(State);
				return true;
			});
		Kernel->RegisterTool(MoveTemp(Tool));
	}

	{
		FGCAgentToolDefinition Tool;
		Tool.Name = TEXT("read_project_file");
		Tool.Description = TEXT("Read a project file under Source/, Config/ or Content/ (max 64KB).");
		Tool.ParametersSchemaJson = TEXT(
			"{\"type\":\"object\",\"properties\":{"
			"\"path\":{\"type\":\"string\",\"description\":\"Project-relative path, e.g. Content/JavaScript/AIHotfix/SampleHotfix.js\"}"
			"},\"required\":[\"path\"]}");
		Tool.Executor = FGCAgentToolExecutor::CreateWeakLambda(this,
			[this](const TSharedRef<FJsonObject>& Args, FString& OutResult)
			{
				FString RequestedPath;
				if (!Args->TryGetStringField(TEXT("path"), RequestedPath) || RequestedPath.TrimStartAndEnd().IsEmpty())
				{
					OutResult = TEXT("read_project_file requires a non-empty path.");
					return false;
				}

				FString AbsolutePath;
				if (!ResolveReadableProjectPath(FPaths::ProjectDir(), RequestedPath, AbsolutePath))
				{
					OutResult = FString::Printf(TEXT("Path is not readable within project bounds: %s"), *RequestedPath);
					return false;
				}

				FString FileContents;
				if (!FFileHelper::LoadFileToString(FileContents, *AbsolutePath))
				{
					OutResult = FString::Printf(TEXT("Failed to read file: %s"), *RequestedPath);
					return false;
				}

				OutResult = FString::Printf(TEXT("File: %s\n\n%s"), *RequestedPath, *FileContents);
				return true;
			});
		Kernel->RegisterTool(MoveTemp(Tool));
	}

	{
		FGCAgentToolDefinition Tool;
		Tool.Name = TEXT("read_generated_hotfix");
		Tool.Description = TEXT("Read the JavaScript source of a generated hotfix module.");
		Tool.ParametersSchemaJson = TEXT(
			"{\"type\":\"object\",\"properties\":{"
			"\"module_name\":{\"type\":\"string\",\"description\":\"Defaults to the current target module\"}"
			"}}");
		Tool.Executor = FGCAgentToolExecutor::CreateWeakLambda(this,
			[this](const TSharedRef<FJsonObject>& Args, FString& OutResult)
			{
				FString ModuleName;
				Args->TryGetStringField(TEXT("module_name"), ModuleName);
				ModuleName = NormalizeModuleName(ModuleName.IsEmpty() ? CurrentTargetModuleName : ModuleName);

				const FString JsPath = GetAbsoluteScriptPathForModule(ModuleName, TEXT(".js"));
				FString FileContents;
				if (!FFileHelper::LoadFileToString(FileContents, *JsPath))
				{
					OutResult = FString::Printf(TEXT("No generated hotfix source found at %s"), *JsPath);
					return false;
				}

				OutResult = FString::Printf(TEXT("Module: %s\nPath: %s\n\n%s"), *ModuleName, *JsPath, *FileContents);
				return true;
			});
		Kernel->RegisterTool(MoveTemp(Tool));
	}

	{
		FGCAgentToolDefinition Tool;
		Tool.Name = TEXT("write_hotfix_file");
		Tool.Description = TEXT("Write a hotfix module (CommonJS JavaScript) to disk as the pending hotfix. Activate it with apply_hotfix.");
		Tool.ParametersSchemaJson = TEXT(
			"{\"type\":\"object\",\"properties\":{"
			"\"module_name\":{\"type\":\"string\",\"description\":\"Defaults to the current target module\"},"
			"\"javascript\":{\"type\":\"string\"},"
			"\"typescript\":{\"type\":\"string\",\"description\":\"Optional TS mirror\"}"
			"},\"required\":[\"javascript\"]}");
		Tool.Executor = FGCAgentToolExecutor::CreateWeakLambda(this,
			[this](const TSharedRef<FJsonObject>& Args, FString& OutResult)
			{
				FString ModuleName;
				Args->TryGetStringField(TEXT("module_name"), ModuleName);
				ModuleName = NormalizeModuleName(ModuleName.IsEmpty() ? CurrentTargetModuleName : ModuleName);

				FString JavaScript;
				if (!Args->TryGetStringField(TEXT("javascript"), JavaScript) || JavaScript.TrimStartAndEnd().IsEmpty())
				{
					OutResult = TEXT("write_hotfix_file requires javascript.");
					return false;
				}

				FString SavedModuleName;
				FString Error;
				if (!WriteHotfixFiles(ModuleName, JavaScript, SavedModuleName, Error))
				{
					OutResult = Error;
					return false;
				}

				FString TypeScript;
				if (Args->TryGetStringField(TEXT("typescript"), TypeScript) && !TypeScript.TrimStartAndEnd().IsEmpty())
				{
					const FString TsPath = GetAbsoluteScriptPathForModule(SavedModuleName, TEXT(".ts"));
					FFileHelper::SaveStringToFile(TypeScript, *TsPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
				}

				PendingGeneratedModuleName = SavedModuleName;
				PendingGeneratedSource = JavaScript;

				FGCAIHotfixGenerationResult Result;
				Result.bSuccess = true;
				Result.ModuleName = SavedModuleName;
				Result.JavaScript = JavaScript;
				Result.TypeScript = TypeScript.IsEmpty() ? JavaScript : TypeScript;
				Result.Summary = TEXT("Generated hotfix code is ready.");
				LastGeneratedResult = Result;
				OnHotfixGenerated.Broadcast(Result);

				OutResult = FString::Printf(TEXT("Wrote pending hotfix module %s"), *SavedModuleName);
				return true;
			});
		Kernel->RegisterTool(MoveTemp(Tool));
	}

	{
		FGCAgentToolDefinition Tool;
		Tool.Name = TEXT("apply_hotfix");
		Tool.Description = TEXT("Activate the pending hotfix module by restarting the puerts runtime with it loaded.");
		Tool.Executor = FGCAgentToolExecutor::CreateWeakLambda(this,
			[this](const TSharedRef<FJsonObject>& Args, FString& OutResult)
			{
				if (!HasPendingHotfix())
				{
					OutResult = TEXT("No pending hotfix is available to apply.");
					return false;
				}

				if (!ApplyPendingHotfix())
				{
					OutResult = TEXT("ApplyPendingHotfix failed; see runtime log.");
					return false;
				}

				OutResult = TEXT("Pending hotfix applied successfully.");
				return true;
			});
		Kernel->RegisterTool(MoveTemp(Tool));
	}
}
