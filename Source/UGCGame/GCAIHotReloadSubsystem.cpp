#include "GCAIHotReloadSubsystem.h"

#include "GCAIHotReloadSettings.h"
#include "GCAIHotfixBridge.h"
#include "HttpModule.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IHttpResponse.h"
#include "JsEnv.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TimerManager.h"

namespace
{
const FString DefaultModulePrefix = TEXT("AIHotfix/Generated");
const FString DefaultCopilotApiBaseUrl = TEXT("https://api.individual.githubcopilot.com");
const FString CopilotTokenUrl = TEXT("https://api.github.com/copilot_internal/v2/token");
const FString GitHubDeviceCodeUrl = TEXT("https://github.com/login/device/code");
const FString GitHubDeviceAccessTokenUrl = TEXT("https://github.com/login/oauth/access_token");
const FString GitHubOAuthClientId = TEXT("01ab8ac9400c4e429b23");
const FString GitHubDeviceCodeScope = TEXT("read:user");
constexpr int32 MaxChatMessagesToSend = 12;
}

void UGCAIHotReloadSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UGCAIHotReloadSettings* Settings = GetDefault<UGCAIHotReloadSettings>();
	ProviderConfig = Settings->DefaultProvider;

	Bridge = NewObject<UGCAIHotfixBridge>(this);
	Bridge->Initialize(this);
}

void UGCAIHotReloadSubsystem::Deinitialize()
{
	if (PendingGenerationRequest.IsValid())
	{
		PendingGenerationRequest->OnProcessRequestComplete().Unbind();
		PendingGenerationRequest->CancelRequest();
		PendingGenerationRequest.Reset();
	}

	if (PendingCopilotAuthRequest.IsValid())
	{
		PendingCopilotAuthRequest->OnProcessRequestComplete().Unbind();
		PendingCopilotAuthRequest->CancelRequest();
		PendingCopilotAuthRequest.Reset();
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CopilotDevicePollTimer);
	}

	ShutdownJsEnv();
	Super::Deinitialize();
}

void UGCAIHotReloadSubsystem::ConfigureProvider(const FGCAIProviderConfig& InProviderConfig)
{
	ProviderConfig = InProviderConfig;
}

FGCAIProviderConfig UGCAIHotReloadSubsystem::GetProviderConfig() const
{
	return ProviderConfig;
}

bool UGCAIHotReloadSubsystem::IsRuntimeReady() const
{
	return JsEnv != nullptr;
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

bool UGCAIHotReloadSubsystem::ApplyHotfixJavaScript(const FString& ModuleName, const FString& SourceCode)
{
	FString SavedModuleName;
	FString Error;

	if (!WriteHotfixFiles(ModuleName, SourceCode, SavedModuleName, Error))
	{
		OnHotfixFailed.Broadcast(Error);
		return false;
	}

	if (!StartJsEnvForModule(SavedModuleName, Error))
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
		const FString ModuleName = ActiveModuleName.IsEmpty() ? GetGeneratedModuleName() : ActiveModuleName;
		FString Error;
		if (!StartJsEnvForModule(ModuleName, Error))
		{
			OnHotfixFailed.Broadcast(Error);
		}
	}
}

void UGCAIHotReloadSubsystem::GenerateHotfixFromPrompt(const FString& Prompt, const FString& ModuleName)
{
	const FString TrimmedPrompt = Prompt.TrimStartAndEnd();
	if (TrimmedPrompt.IsEmpty())
	{
		OnHotfixFailed.Broadcast(TEXT("Prompt is empty."));
		return;
	}

	if (!ProviderConfig.bEnabled)
	{
		OnHotfixFailed.Broadcast(TEXT("AI provider is disabled. Configure a provider before generating hotfix code."));
		return;
	}

	if (ProviderConfig.BaseUrl.IsEmpty() || ProviderConfig.Model.IsEmpty() || ProviderConfig.ApiKey.IsEmpty())
	{
		OnHotfixFailed.Broadcast(TEXT("AI provider is missing BaseUrl, Model, or ApiKey."));
		return;
	}

	if (PendingGenerationRequest.IsValid())
	{
		OnHotfixFailed.Broadcast(TEXT("A hotfix generation request is already running."));
		return;
	}

	const FString NormalizedModuleName = NormalizeModuleName(ModuleName.IsEmpty() ? GetGeneratedModuleName() : ModuleName);
	AppendChatMessage(TEXT("user"), TrimmedPrompt);

	if (ProviderConfig.Transport == EGCAIProviderTransport::GitHubCopilot)
	{
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
		Request->SetURL(CopilotTokenUrl);
		Request->SetVerb(TEXT("GET"));
		Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
		Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ProviderConfig.ApiKey));
		Request->SetHeader(TEXT("User-Agent"), TEXT("UGCGame/1.0"));
		Request->OnProcessRequestComplete().BindUObject(this, &UGCAIHotReloadSubsystem::HandleCopilotTokenResponse, TrimmedPrompt, NormalizedModuleName);
		PendingGenerationRequest = Request;
		Request->ProcessRequest();
		OnRuntimeLog.Broadcast(TEXT("Exchanging GitHub token for Copilot API token."));
		return;
	}

	BeginGenerateRequest(TrimmedPrompt, NormalizedModuleName, ProviderConfig.BaseUrl, ProviderConfig.ApiKey);
}

void UGCAIHotReloadSubsystem::BeginCopilotDeviceLogin()
{
	if (PendingCopilotAuthRequest.IsValid() || CopilotDeviceAuthState.bIsPending)
	{
		OnHotfixFailed.Broadcast(TEXT("GitHub device login is already running."));
		return;
	}

	CopilotDeviceAuthState = FGCAICopilotDeviceAuthState();
	CopilotDeviceAuthState.bIsPending = true;
	CopilotDeviceAuthState.StatusMessage = TEXT("Requesting GitHub device code...");
	BroadcastCopilotDeviceAuthUpdated();

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(FString::Printf(TEXT("%s?client_id=%s&scope=%s"), *GitHubDeviceCodeUrl, *GitHubOAuthClientId, *GitHubDeviceCodeScope));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetHeader(TEXT("User-Agent"), TEXT("UGCGame/1.0"));
	Request->OnProcessRequestComplete().BindUObject(this, &UGCAIHotReloadSubsystem::HandleCopilotDeviceCodeResponse);
	PendingCopilotAuthRequest = Request;
	Request->ProcessRequest();
}

void UGCAIHotReloadSubsystem::CancelCopilotDeviceLogin()
{
	ResetCopilotDeviceLogin(false);
	CopilotDeviceAuthState.StatusMessage = TEXT("GitHub device login cancelled.");
	BroadcastCopilotDeviceAuthUpdated();
}

FString UGCAIHotReloadSubsystem::GetHotfixDirectoryOnDisk() const
{
	return FPaths::GetPath(GetAbsoluteScriptPathForModule(GetGeneratedModuleName(), TEXT(".js")));
}

bool UGCAIHotReloadSubsystem::ApplyPendingHotfix()
{
	if (PendingGeneratedSource.IsEmpty())
	{
		return false;
	}

	const bool bApplied = ApplyHotfixJavaScript(PendingGeneratedModuleName, PendingGeneratedSource);
	if (bApplied)
	{
		PendingGeneratedSource.Reset();
	}
	return bApplied;
}

bool UGCAIHotReloadSubsystem::HasPendingHotfix() const
{
	return !PendingGeneratedSource.IsEmpty();
}

FGCAIHotfixGenerationResult UGCAIHotReloadSubsystem::GetLastGeneratedResult() const
{
	return LastGeneratedResult;
}

TArray<FGCAIChatMessage> UGCAIHotReloadSubsystem::GetChatMessages() const
{
	return ChatMessages;
}

FGCAICopilotDeviceAuthState UGCAIHotReloadSubsystem::GetCopilotDeviceAuthState() const
{
	return CopilotDeviceAuthState;
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

void UGCAIHotReloadSubsystem::AppendChatMessage(const FString& Role, const FString& Content)
{
	const FString TrimmedContent = Content.TrimStartAndEnd();
	if (TrimmedContent.IsEmpty())
	{
		return;
	}

	FGCAIChatMessage Message;
	Message.Role = Role;
	Message.Content = TrimmedContent;
	ChatMessages.Add(MoveTemp(Message));
	BroadcastChatSessionChanged();
}

void UGCAIHotReloadSubsystem::BroadcastChatSessionChanged()
{
	OnChatSessionChanged.Broadcast();
}

void UGCAIHotReloadSubsystem::BroadcastCopilotDeviceAuthUpdated()
{
	OnCopilotDeviceAuthUpdated.Broadcast(CopilotDeviceAuthState);
}

void UGCAIHotReloadSubsystem::ScheduleCopilotDeviceTokenPoll(float DelaySeconds)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CopilotDevicePollTimer);
		World->GetTimerManager().SetTimer(
			CopilotDevicePollTimer,
			FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (PendingCopilotDeviceCode.IsEmpty() || !CopilotDeviceAuthState.bIsPending)
				{
					return;
				}

				TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
				Request->SetURL(
					FString::Printf(
						TEXT("%s?client_id=%s&device_code=%s&grant_type=urn:ietf:params:oauth:grant-type:device_code"),
						*GitHubDeviceAccessTokenUrl,
						*GitHubOAuthClientId,
						*PendingCopilotDeviceCode));
				Request->SetVerb(TEXT("POST"));
				Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
				Request->SetHeader(TEXT("User-Agent"), TEXT("UGCGame/1.0"));
				Request->OnProcessRequestComplete().BindUObject(this, &UGCAIHotReloadSubsystem::HandleCopilotDeviceAccessTokenResponse);
				PendingCopilotAuthRequest = Request;
				Request->ProcessRequest();
			}),
			DelaySeconds,
			false);
	}
}

void UGCAIHotReloadSubsystem::ResetCopilotDeviceLogin(bool bKeepAuthenticatedState)
{
	if (PendingCopilotAuthRequest.IsValid())
	{
		PendingCopilotAuthRequest->OnProcessRequestComplete().Unbind();
		PendingCopilotAuthRequest->CancelRequest();
		PendingCopilotAuthRequest.Reset();
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CopilotDevicePollTimer);
	}

	PendingCopilotDeviceCode.Reset();

	const bool bWasAuthenticated = CopilotDeviceAuthState.bIsAuthenticated;
	const FString ExistingStatus = CopilotDeviceAuthState.StatusMessage;
	CopilotDeviceAuthState = FGCAICopilotDeviceAuthState();
	if (bKeepAuthenticatedState)
	{
		CopilotDeviceAuthState.bIsAuthenticated = bWasAuthenticated;
		CopilotDeviceAuthState.StatusMessage = ExistingStatus;
	}
}

void UGCAIHotReloadSubsystem::ShutdownJsEnv()
{
	JsEnv.Reset();
}

bool UGCAIHotReloadSubsystem::StartJsEnvForModule(const FString& ModuleName, FString& OutError)
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

	JsEnv->Start(ModuleName, Arguments);
	ActiveModuleName = ModuleName;

	EmitRuntimeLog(FString::Printf(TEXT("Started puerts runtime with module %s"), *ModuleName));
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
	Trimmed.ReplaceInline(TEXT(".."), TEXT("_"));

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

FString UGCAIHotReloadSubsystem::BuildSystemPrompt() const
{
	const UGCAIHotReloadSettings* Settings = GetDefault<UGCAIHotReloadSettings>();

	return Settings->SystemPrompt +
		TEXT("\nUse only these safe bridge calls: bridge.LogMessage(text), bridge.EmitGameplayCommand(name, payloadJson), bridge.GetWorldSeconds(), bridge.GetActiveModuleName().") +
			TEXT("\nAvoid direct engine globals unless required through puerts modules.") +
			TEXT("\nThe module should run from top-level when loaded and may export helper functions if useful.") +
			TEXT("\nDo not assume hotfix code is live until the user presses Reload in the game UI.");
}

void UGCAIHotReloadSubsystem::BeginGenerateRequest(
	const FString& Prompt,
	const FString& NormalizedModuleName,
	const FString& BaseUrl,
	const FString& AuthToken)
{
	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("model"), ProviderConfig.Model);
	RootObject->SetNumberField(TEXT("temperature"), ProviderConfig.Temperature);

	TArray<TSharedPtr<FJsonValue>> Messages;

	TSharedRef<FJsonObject> SystemMessage = MakeShared<FJsonObject>();
	SystemMessage->SetStringField(TEXT("role"), TEXT("system"));
	SystemMessage->SetStringField(TEXT("content"), BuildSystemPrompt());
	Messages.Add(MakeShared<FJsonValueObject>(SystemMessage));

	TSharedRef<FJsonObject> ModuleContextMessage = MakeShared<FJsonObject>();
	ModuleContextMessage->SetStringField(TEXT("role"), TEXT("system"));
	ModuleContextMessage->SetStringField(
		TEXT("content"),
		FString::Printf(
			TEXT("Target module for this conversation: %s.\nLatest user request: %s\nReturn JSON only. Use bridge.EmitGameplayCommand(name, payloadJson) for gameplay-facing actions."),
			*NormalizedModuleName,
			*Prompt));
	Messages.Add(MakeShared<FJsonValueObject>(ModuleContextMessage));

	if (!LastGeneratedResult.JavaScript.IsEmpty())
	{
		TSharedRef<FJsonObject> CurrentCodeMessage = MakeShared<FJsonObject>();
		CurrentCodeMessage->SetStringField(TEXT("role"), TEXT("system"));
		CurrentCodeMessage->SetStringField(
			TEXT("content"),
			FString::Printf(
				TEXT("Current generated JavaScript snapshot for %s:\n```javascript\n%s\n```"),
				*NormalizedModuleName,
				*LastGeneratedResult.JavaScript));
		Messages.Add(MakeShared<FJsonValueObject>(CurrentCodeMessage));
	}

	const int32 StartIndex = FMath::Max(0, ChatMessages.Num() - MaxChatMessagesToSend);
	for (int32 Index = StartIndex; Index < ChatMessages.Num(); ++Index)
	{
		const FGCAIChatMessage& ChatMessage = ChatMessages[Index];
		if (ChatMessage.Role != TEXT("user") && ChatMessage.Role != TEXT("assistant"))
		{
			continue;
		}

		TSharedRef<FJsonObject> HistoryMessage = MakeShared<FJsonObject>();
		HistoryMessage->SetStringField(TEXT("role"), ChatMessage.Role);
		HistoryMessage->SetStringField(TEXT("content"), ChatMessage.Content);
		Messages.Add(MakeShared<FJsonValueObject>(HistoryMessage));
	}

	RootObject->SetArrayField(TEXT("messages"), Messages);

	FString RequestBody;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&RequestBody);
	FJsonSerializer::Serialize(RootObject, Writer);

	const FString Url = BaseUrl / ProviderConfig.ChatCompletionsPath;
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *AuthToken));

	for (const TPair<FString, FString>& HeaderPair : ProviderConfig.ExtraHeaders)
	{
		Request->SetHeader(HeaderPair.Key, HeaderPair.Value);
	}

	Request->SetContentAsString(RequestBody);
	Request->OnProcessRequestComplete().BindUObject(
		this,
		&UGCAIHotReloadSubsystem::HandleGenerateHotfixResponse,
		NormalizedModuleName);
	PendingGenerationRequest = Request;
	Request->ProcessRequest();
	OnRuntimeLog.Broadcast(FString::Printf(TEXT("Submitting AI hotfix generation request to %s"), *ProviderConfig.ProviderId));
}

void UGCAIHotReloadSubsystem::HandleCopilotTokenResponse(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bWasSuccessful,
	FString Prompt,
	FString TargetModuleName)
{
	PendingGenerationRequest.Reset();

	if (!bWasSuccessful || !Response.IsValid())
	{
		OnHotfixFailed.Broadcast(TEXT("GitHub Copilot token exchange failed before a response was received."));
		return;
	}

	if (Response->GetResponseCode() < 200 || Response->GetResponseCode() >= 300)
	{
		OnHotfixFailed.Broadcast(
			FString::Printf(TEXT("GitHub Copilot token exchange failed with HTTP %d: %s"), Response->GetResponseCode(), *Response->GetContentAsString()));
		return;
	}

	FString CopilotToken;
	FString CopilotBaseUrl;
	int64 ExpiresAtUnixMs = 0;
	if (!TryParseCopilotTokenResponse(Response->GetContentAsString(), CopilotToken, CopilotBaseUrl, ExpiresAtUnixMs))
	{
		OnHotfixFailed.Broadcast(TEXT("GitHub Copilot token exchange returned an unreadable payload."));
		return;
	}

	BeginGenerateRequest(Prompt, TargetModuleName, CopilotBaseUrl, CopilotToken);
}

void UGCAIHotReloadSubsystem::HandleCopilotDeviceCodeResponse(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bWasSuccessful)
{
	PendingCopilotAuthRequest.Reset();

	if (!bWasSuccessful || !Response.IsValid())
	{
		ResetCopilotDeviceLogin(false);
		OnHotfixFailed.Broadcast(TEXT("GitHub device login failed before a response was received."));
		return;
	}

	if (Response->GetResponseCode() < 200 || Response->GetResponseCode() >= 300)
	{
		const FString ErrorMessage = FString::Printf(TEXT("GitHub device login failed with HTTP %d: %s"), Response->GetResponseCode(), *Response->GetContentAsString());
		ResetCopilotDeviceLogin(false);
		OnHotfixFailed.Broadcast(ErrorMessage);
		return;
	}

	FString DeviceCode;
	if (!TryParseCopilotDeviceCodeResponse(Response->GetContentAsString(), CopilotDeviceAuthState, DeviceCode))
	{
		ResetCopilotDeviceLogin(false);
		OnHotfixFailed.Broadcast(TEXT("GitHub device login returned an unreadable payload."));
		return;
	}

	PendingCopilotDeviceCode = DeviceCode;
	CopilotDeviceAuthState.bIsPending = true;
	CopilotDeviceAuthState.StatusMessage = TEXT("Open GitHub and approve this device.");
	BroadcastCopilotDeviceAuthUpdated();

	const FString LaunchUrl = CopilotDeviceAuthState.VerificationUriComplete.IsEmpty()
		? CopilotDeviceAuthState.VerificationUri
		: CopilotDeviceAuthState.VerificationUriComplete;
	if (!LaunchUrl.IsEmpty())
	{
		FPlatformProcess::LaunchURL(*LaunchUrl, nullptr, nullptr);
	}

	ScheduleCopilotDeviceTokenPoll(FMath::Max(1, CopilotDeviceAuthState.PollIntervalSeconds));
}

void UGCAIHotReloadSubsystem::HandleCopilotDeviceAccessTokenResponse(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bWasSuccessful)
{
	PendingCopilotAuthRequest.Reset();

	if (!bWasSuccessful || !Response.IsValid())
	{
		ScheduleCopilotDeviceTokenPoll(FMath::Max(1, CopilotDeviceAuthState.PollIntervalSeconds));
		return;
	}

	if (Response->GetResponseCode() < 200 || Response->GetResponseCode() >= 300)
	{
		ScheduleCopilotDeviceTokenPoll(FMath::Max(1, CopilotDeviceAuthState.PollIntervalSeconds));
		return;
	}

	FString AccessToken;
	FString ErrorCode;
	FString ErrorDescription;
	int32 IntervalSeconds = 0;
	if (!TryParseCopilotDeviceAccessTokenResponse(Response->GetContentAsString(), AccessToken, ErrorCode, ErrorDescription, IntervalSeconds))
	{
		ResetCopilotDeviceLogin(false);
		OnHotfixFailed.Broadcast(TEXT("GitHub device login polling returned an unreadable payload."));
		return;
	}

	if (!AccessToken.IsEmpty())
	{
		ProviderConfig.ApiKey = AccessToken;
		CopilotDeviceAuthState.bIsPending = false;
		CopilotDeviceAuthState.bIsAuthenticated = true;
		CopilotDeviceAuthState.StatusMessage = TEXT("GitHub device login complete.");
		PendingCopilotDeviceCode.Reset();
		BroadcastCopilotDeviceAuthUpdated();
		return;
	}

	if (IntervalSeconds > 0)
	{
		CopilotDeviceAuthState.PollIntervalSeconds = IntervalSeconds;
	}

	if (ErrorCode == TEXT("authorization_pending"))
	{
		CopilotDeviceAuthState.StatusMessage = TEXT("Waiting for GitHub authorization...");
		BroadcastCopilotDeviceAuthUpdated();
		ScheduleCopilotDeviceTokenPoll(FMath::Max(1, CopilotDeviceAuthState.PollIntervalSeconds));
		return;
	}

	if (ErrorCode == TEXT("slow_down"))
	{
		CopilotDeviceAuthState.PollIntervalSeconds = FMath::Max(5, CopilotDeviceAuthState.PollIntervalSeconds + 5);
		CopilotDeviceAuthState.StatusMessage = TEXT("GitHub asked to slow down polling.");
		BroadcastCopilotDeviceAuthUpdated();
		ScheduleCopilotDeviceTokenPoll(CopilotDeviceAuthState.PollIntervalSeconds);
		return;
	}

	ResetCopilotDeviceLogin(false);
	OnHotfixFailed.Broadcast(
		ErrorDescription.IsEmpty()
			? FString::Printf(TEXT("GitHub device login failed: %s"), *ErrorCode)
			: FString::Printf(TEXT("GitHub device login failed: %s"), *ErrorDescription));
}

void UGCAIHotReloadSubsystem::HandleGenerateHotfixResponse(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bWasSuccessful,
	FString TargetModuleName)
{
	PendingGenerationRequest.Reset();

	if (!bWasSuccessful || !Response.IsValid())
	{
		OnHotfixFailed.Broadcast(TEXT("Hotfix generation request failed before a response was received."));
		return;
	}

	if (Response->GetResponseCode() < 200 || Response->GetResponseCode() >= 300)
	{
		OnHotfixFailed.Broadcast(FString::Printf(TEXT("Hotfix generation failed with HTTP %d: %s"), Response->GetResponseCode(), *Response->GetContentAsString()));
		return;
	}

	FGCAIHotfixGenerationResult Result;
	Result.ModuleName = TargetModuleName;

	if (!TryParseGenerationResponse(Response->GetContentAsString(), Result))
	{
		Result.Error = TEXT("Could not parse model response into a hotfix module.");
		OnHotfixGenerated.Broadcast(Result);
		OnHotfixFailed.Broadcast(Result.Error);
		return;
	}

	Result.ModuleName = TargetModuleName;
	PendingGeneratedModuleName = TargetModuleName;
	PendingGeneratedSource = Result.JavaScript;
	LastGeneratedResult = Result;
	FString SavedModuleName;
	FString Error;
	if (!WriteHotfixFiles(TargetModuleName, Result.JavaScript, SavedModuleName, Error))
	{
		Result.Error = Error;
		OnHotfixGenerated.Broadcast(Result);
		OnHotfixFailed.Broadcast(Result.Error);
		return;
	}

	Result.Summary = Result.Summary.IsEmpty()
		? TEXT("Generated hotfix code is ready. Press Reload to make it live.")
		: Result.Summary + TEXT(" Press Reload to make it live.");
	LastGeneratedResult = Result;
	AppendChatMessage(TEXT("assistant"), Result.Summary);
	OnHotfixGenerated.Broadcast(Result);
}

bool UGCAIHotReloadSubsystem::TryParseGenerationResponse(const FString& ResponseText, FGCAIHotfixGenerationResult& OutResult) const
{
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
	if (!Root->TryGetArrayField(TEXT("choices"), Choices) || !Choices || Choices->Num() == 0)
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* ChoiceObject = nullptr;
	if (!(*Choices)[0]->TryGetObject(ChoiceObject) || !ChoiceObject || !ChoiceObject->IsValid())
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* MessageObject = nullptr;
	if (!(*ChoiceObject)->TryGetObjectField(TEXT("message"), MessageObject) || !MessageObject || !MessageObject->IsValid())
	{
		return false;
	}

	FString Content;
	if (!(*MessageObject)->TryGetStringField(TEXT("content"), Content))
	{
		return false;
	}

	TSharedPtr<FJsonObject> Payload;
	TSharedRef<TJsonReader<>> PayloadReader = TJsonReaderFactory<>::Create(Content);
	if (FJsonSerializer::Deserialize(PayloadReader, Payload) && Payload.IsValid())
	{
		Payload->TryGetStringField(TEXT("summary"), OutResult.Summary);
		Payload->TryGetStringField(TEXT("javascript"), OutResult.JavaScript);
		Payload->TryGetStringField(TEXT("typescript"), OutResult.TypeScript);
	}
	else
	{
		OutResult.JavaScript = ExtractFirstCodeBlock(Content);
		OutResult.Summary = TEXT("Parsed code block from non-JSON response.");
	}

	if (OutResult.JavaScript.IsEmpty())
	{
		return false;
	}

	if (OutResult.TypeScript.IsEmpty())
	{
		OutResult.TypeScript = OutResult.JavaScript;
	}

	return true;
}

bool UGCAIHotReloadSubsystem::TryParseCopilotDeviceCodeResponse(
	const FString& ResponseText,
	FGCAICopilotDeviceAuthState& OutState,
	FString& OutDeviceCode) const
{
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}

	if (!Root->TryGetStringField(TEXT("device_code"), OutDeviceCode) || OutDeviceCode.IsEmpty())
	{
		return false;
	}

	if (!Root->TryGetStringField(TEXT("user_code"), OutState.UserCode) || OutState.UserCode.IsEmpty())
	{
		return false;
	}

	if (!Root->TryGetStringField(TEXT("verification_uri"), OutState.VerificationUri) || OutState.VerificationUri.IsEmpty())
	{
		return false;
	}

	Root->TryGetStringField(TEXT("verification_uri_complete"), OutState.VerificationUriComplete);

	double Interval = 5.0;
	Root->TryGetNumberField(TEXT("interval"), Interval);
	OutState.PollIntervalSeconds = FMath::Max(1, static_cast<int32>(Interval));

	double ExpiresIn = 0.0;
	if (Root->TryGetNumberField(TEXT("expires_in"), ExpiresIn) && ExpiresIn > 0.0)
	{
		const FDateTime ExpirationTime = FDateTime::UtcNow() + FTimespan::FromSeconds(ExpiresIn);
		OutState.ExpiresAtUnixMs = ExpirationTime.ToUnixTimestamp() * 1000;
	}

	return true;
}

bool UGCAIHotReloadSubsystem::TryParseCopilotDeviceAccessTokenResponse(
	const FString& ResponseText,
	FString& OutAccessToken,
	FString& OutErrorCode,
	FString& OutErrorDescription,
	int32& OutIntervalSeconds) const
{
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}

	Root->TryGetStringField(TEXT("access_token"), OutAccessToken);
	Root->TryGetStringField(TEXT("error"), OutErrorCode);
	Root->TryGetStringField(TEXT("error_description"), OutErrorDescription);

	double Interval = 0.0;
	if (Root->TryGetNumberField(TEXT("interval"), Interval))
	{
		OutIntervalSeconds = FMath::Max(1, static_cast<int32>(Interval));
	}

	return !OutAccessToken.IsEmpty() || !OutErrorCode.IsEmpty();
}

bool UGCAIHotReloadSubsystem::TryParseCopilotTokenResponse(
	const FString& ResponseText,
	FString& OutToken,
	FString& OutBaseUrl,
	int64& OutExpiresAtUnixMs) const
{
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}

	if (!Root->TryGetStringField(TEXT("token"), OutToken) || OutToken.IsEmpty())
	{
		return false;
	}

	FString ExpiresAtString;
	if (Root->TryGetStringField(TEXT("expires_at"), ExpiresAtString))
	{
		OutExpiresAtUnixMs = FCString::Atoi64(*ExpiresAtString);
	}
	else
	{
		double ExpiresAtNumber = 0.0;
		if (Root->TryGetNumberField(TEXT("expires_at"), ExpiresAtNumber))
		{
			OutExpiresAtUnixMs = static_cast<int64>(ExpiresAtNumber);
		}
	}

	if (OutExpiresAtUnixMs > 0 && OutExpiresAtUnixMs < 100000000000LL)
	{
		OutExpiresAtUnixMs *= 1000;
	}

	OutBaseUrl = DeriveCopilotBaseUrlFromToken(OutToken);
	if (OutBaseUrl.IsEmpty())
	{
		OutBaseUrl = DefaultCopilotApiBaseUrl;
	}

	return true;
}

FString UGCAIHotReloadSubsystem::ExtractFirstCodeBlock(const FString& Text)
{
	const FString Fence = TEXT("```");
	const int32 FenceStart = Text.Find(Fence);
	if (FenceStart == INDEX_NONE)
	{
		return FString();
	}

	const int32 NewLineAfterFence = Text.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, FenceStart);
	if (NewLineAfterFence == INDEX_NONE)
	{
		return FString();
	}

	const int32 CodeStart = NewLineAfterFence + 1;
	const int32 FenceEnd = Text.Find(Fence, ESearchCase::CaseSensitive, ESearchDir::FromStart, CodeStart);
	if (FenceEnd == INDEX_NONE || FenceEnd <= CodeStart)
	{
		return FString();
	}

	return Text.Mid(CodeStart, FenceEnd - CodeStart).TrimStartAndEnd();
}

FString UGCAIHotReloadSubsystem::DeriveCopilotBaseUrlFromToken(const FString& Token)
{
	FRegexPattern Pattern(TEXT("(?:^|;)\\s*proxy-ep=([^;\\s]+)"));
	FRegexMatcher Matcher(Pattern, Token);
	if (!Matcher.FindNext())
	{
		return FString();
	}

	FString Host = Matcher.GetCaptureGroup(1);
	Host.RemoveFromStart(TEXT("https://"));
	Host.RemoveFromStart(TEXT("http://"));
	if (Host.StartsWith(TEXT("proxy.")))
	{
		Host.RightChopInline(6, EAllowShrinking::No);
		Host = TEXT("api.") + Host;
	}

	return Host.IsEmpty() ? FString() : FString::Printf(TEXT("https://%s"), *Host);
}
