#include "GCAIHotReloadChatWidget.h"

#include "Dom/JsonObject.h"
#include "Framework/Application/SlateApplication.h"
#include "GCAIHotReloadSubsystem.h"
#include "GCAIHotReloadTypes.h"
#include "IWebBrowserWindow.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "SWebBrowser.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
const FString DefaultCopilotModel = TEXT("gpt-5.2");
const FString DefaultModuleName = TEXT("AIHotfix/Generated/Current");
const TCHAR* BrowserCommandPrefix = TEXT("__UGC_HOTFIX__");
const TCHAR* HtmlCssPlaceholder = TEXT("__AI_HOTFIX_CSS__");
const TCHAR* HtmlJsPlaceholder = TEXT("__AI_HOTFIX_JS__");

FString MakeAuthSummary(const FGCAICopilotDeviceAuthState& State)
{
	if (State.bIsAuthenticated)
	{
		return TEXT("GitHub connected");
	}

	if (State.bIsPending)
	{
		return TEXT("Waiting for GitHub approval");
	}

	return TEXT("GitHub not connected");
}

void AddStringArray(TSharedRef<FJsonObject> RootObject, const TCHAR* FieldName, const TArray<FString>& Values)
{
	TArray<TSharedPtr<FJsonValue>> JsonValues;
	JsonValues.Reserve(Values.Num());
	for (const FString& Value : Values)
	{
		JsonValues.Add(MakeShared<FJsonValueString>(Value));
	}

	RootObject->SetArrayField(FieldName, JsonValues);
}

FString GetWebUiAssetPath(const TCHAR* FileName)
{
	return FPaths::Combine(FPaths::ProjectContentDir(), TEXT("UI/AIHotfixWeb"), FileName);
}

bool LoadWebUiAsset(const TCHAR* FileName, FString& OutContents)
{
	return FFileHelper::LoadFileToString(OutContents, *GetWebUiAssetPath(FileName));
}

FString BuildMissingAssetDocument()
{
	return TEXT(
		"<html><body style=\"margin:0;padding:24px;font-family:Segoe UI,sans-serif;background:#0b1320;color:#eef4ff;\">"
		"<h2 style=\"margin:0 0 12px;\">AI Hotfix UI assets are missing</h2>"
		"<p style=\"margin:0;line-height:1.6;\">Expected files under Content/UI/AIHotfixWeb: index.html, styles.css, app.js.</p>"
		"</body></html>");
}
}

void UGCAIHotReloadChatWidget::NativeConstruct()
{
	Super::NativeConstruct();

	LoadBrowserDocument();

	if (UGCAIHotReloadSubsystem* Subsystem = GetHotReloadSubsystem())
	{
		Subsystem->OnChatSessionChanged.AddDynamic(this, &UGCAIHotReloadChatWidget::HandleChatSessionChanged);
		Subsystem->OnRuntimeLog.AddDynamic(this, &UGCAIHotReloadChatWidget::HandleRuntimeLog);
		Subsystem->OnHotfixApplied.AddDynamic(this, &UGCAIHotReloadChatWidget::HandleHotfixApplied);
		Subsystem->OnHotfixFailed.AddDynamic(this, &UGCAIHotReloadChatWidget::HandleHotfixFailed);
		Subsystem->OnHotfixGenerated.AddDynamic(this, &UGCAIHotReloadChatWidget::HandleHotfixGenerated);
		Subsystem->OnCopilotDeviceAuthUpdated.AddDynamic(this, &UGCAIHotReloadChatWidget::HandleCopilotDeviceAuthUpdated);
	}

	RefreshFromSubsystem();
}

void UGCAIHotReloadChatWidget::NativeDestruct()
{
	if (UGCAIHotReloadSubsystem* Subsystem = GetHotReloadSubsystem())
	{
		Subsystem->OnChatSessionChanged.RemoveAll(this);
		Subsystem->OnRuntimeLog.RemoveAll(this);
		Subsystem->OnHotfixApplied.RemoveAll(this);
		Subsystem->OnHotfixFailed.RemoveAll(this);
		Subsystem->OnHotfixGenerated.RemoveAll(this);
		Subsystem->OnCopilotDeviceAuthUpdated.RemoveAll(this);
	}

	Super::NativeDestruct();
}

void UGCAIHotReloadChatWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	BrowserWidget.Reset();
	bBrowserReady = false;
}

TSharedRef<SWidget> UGCAIHotReloadChatWidget::RebuildWidget()
{
	SAssignNew(BrowserWidget, SWebBrowser)
		.InitialURL(TEXT("about:blank"))
		.ShowControls(false)
		.ShowAddressBar(false)
		.ShowErrorMessage(true)
		.SupportsTransparency(true)
		.BackgroundColor(FColor(9, 12, 18, 0))
		.OnLoadCompleted(FSimpleDelegate::CreateUObject(this, &UGCAIHotReloadChatWidget::HandleBrowserLoadCompleted))
		.OnConsoleMessage(FOnConsoleMessageDelegate::CreateUObject(this, &UGCAIHotReloadChatWidget::HandleBrowserConsoleMessage));

	BrowserWidget->BindUObject(TEXT("bridge"), this, true);
	LoadBrowserDocument();

	return BrowserWidget.ToSharedRef();
}

void UGCAIHotReloadChatWidget::RefreshFromSubsystem()
{
	if (UGCAIHotReloadSubsystem* Subsystem = GetHotReloadSubsystem())
	{
		const FGCAIProviderConfig ProviderConfig = Subsystem->GetProviderConfig();
		if (DraftModel.IsEmpty())
		{
			DraftModel = ProviderConfig.Model.IsEmpty() ? DefaultCopilotModel : ProviderConfig.Model;
		}

		if (DraftModuleName.IsEmpty())
		{
			DraftModuleName = Subsystem->GetGeneratedModuleName();
		}

		CopilotDeviceAuthState = Subsystem->GetCopilotDeviceAuthState();
		bIsGenerating = Subsystem->IsAgentTurnRunning();
	}

	SyncBrowserState(true);
}

void UGCAIHotReloadChatWidget::SyncBrowserState(bool bHydrateInputs)
{
	if (!bBrowserReady || !BrowserWidget.IsValid())
	{
		return;
	}

	PushBrowserState(bHydrateInputs);
}

void UGCAIHotReloadChatWidget::PushBrowserState(bool bHydrateInputs)
{
	if (!BrowserWidget.IsValid())
	{
		return;
	}

	const FString PageStateJson = BuildPageStateJson(bHydrateInputs);
	BrowserWidget->ExecuteJavascript(
		FString::Printf(TEXT("window.ugcHotfix && window.ugcHotfix.applyState(%s);"), *PageStateJson));
}

UGCAIHotReloadSubsystem* UGCAIHotReloadChatWidget::GetHotReloadSubsystem() const
{
	if (!GetGameInstance())
	{
		return nullptr;
	}

	return GetGameInstance()->GetSubsystem<UGCAIHotReloadSubsystem>();
}

FString UGCAIHotReloadChatWidget::BuildPageStateJson(bool bHydrateInputs) const
{
	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetBoolField(TEXT("hydrateInputs"), bHydrateInputs);
	RootObject->SetStringField(TEXT("statusText"), StatusMessage);
	RootObject->SetBoolField(TEXT("isGenerating"), bIsGenerating);

	if (const UGCAIHotReloadSubsystem* Subsystem = GetHotReloadSubsystem())
	{
		RootObject->SetBoolField(TEXT("hasPendingHotfix"), Subsystem->HasPendingHotfix());

		TArray<TSharedPtr<FJsonValue>> ChatMessageValues;
		const TArray<FGCAIChatMessage> ChatMessages = Subsystem->GetChatMessages();
		ChatMessageValues.Reserve(ChatMessages.Num());
		for (const FGCAIChatMessage& ChatMessage : ChatMessages)
		{
			TSharedRef<FJsonObject> ChatMessageObject = MakeShared<FJsonObject>();
			ChatMessageObject->SetStringField(TEXT("role"), ChatMessage.Role);
			ChatMessageObject->SetStringField(TEXT("content"), ChatMessage.Content);
			ChatMessageObject->SetStringField(TEXT("kind"), ChatMessage.Kind);
			ChatMessageObject->SetStringField(TEXT("title"), ChatMessage.Title);
			ChatMessageValues.Add(MakeShared<FJsonValueObject>(ChatMessageObject));
		}
		RootObject->SetArrayField(TEXT("messages"), ChatMessageValues);
	}
	else
	{
		RootObject->SetBoolField(TEXT("hasPendingHotfix"), false);
		RootObject->SetArrayField(TEXT("messages"), TArray<TSharedPtr<FJsonValue>>());
	}

	AddStringArray(RootObject, TEXT("runtimeLines"), RuntimeLines);

	TSharedRef<FJsonObject> AuthObject = MakeShared<FJsonObject>();
	AuthObject->SetBoolField(TEXT("isAuthenticated"), CopilotDeviceAuthState.bIsAuthenticated);
	AuthObject->SetBoolField(TEXT("isPending"), CopilotDeviceAuthState.bIsPending);
	AuthObject->SetStringField(TEXT("summary"), MakeAuthSummary(CopilotDeviceAuthState));
	AuthObject->SetStringField(TEXT("statusMessage"), CopilotDeviceAuthState.StatusMessage);
	AuthObject->SetStringField(TEXT("userCode"), CopilotDeviceAuthState.UserCode);
	AuthObject->SetStringField(TEXT("verificationUri"), CopilotDeviceAuthState.VerificationUri);
	AuthObject->SetStringField(TEXT("verificationUriComplete"), CopilotDeviceAuthState.VerificationUriComplete);
	RootObject->SetObjectField(TEXT("auth"), AuthObject);

	TSharedRef<FJsonObject> DraftObject = MakeShared<FJsonObject>();
	DraftObject->SetStringField(TEXT("token"), DraftGitHubToken);
	DraftObject->SetStringField(TEXT("model"), DraftModel.IsEmpty() ? DefaultCopilotModel : DraftModel);
	DraftObject->SetStringField(TEXT("moduleName"), DraftModuleName.IsEmpty() ? DefaultModuleName : DraftModuleName);
	DraftObject->SetStringField(TEXT("prompt"), DraftPrompt);
	RootObject->SetObjectField(TEXT("draft"), DraftObject);

	FString Output;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(RootObject, Writer);
	return Output;
}

FString UGCAIHotReloadChatWidget::BuildHtmlDocument()
{
	FString HtmlTemplate;
	FString Stylesheet;
	FString Script;
	if (!LoadWebUiAsset(TEXT("index.html"), HtmlTemplate) ||
		!LoadWebUiAsset(TEXT("styles.css"), Stylesheet) ||
		!LoadWebUiAsset(TEXT("app.js"), Script))
	{
		return BuildMissingAssetDocument();
	}

	HtmlTemplate.ReplaceInline(HtmlCssPlaceholder, *Stylesheet, ESearchCase::CaseSensitive);
	HtmlTemplate.ReplaceInline(HtmlJsPlaceholder, *Script, ESearchCase::CaseSensitive);
	return HtmlTemplate;
}

void UGCAIHotReloadChatWidget::LoadBrowserDocument()
{
	if (!BrowserWidget.IsValid())
	{
		return;
	}

	const FString Document = BuildHtmlDocument();
	BrowserWidget->LoadString(Document, TEXT("http://ugc.local/ai-hotfix/"));
}

void UGCAIHotReloadChatWidget::HandleBrowserLoadCompleted()
{
	bBrowserReady = true;
	PushBrowserState(true);
}

void UGCAIHotReloadChatWidget::UpdateDraftFields(
	const FString& Token,
	const FString& Model,
	const FString& ModuleName,
	const FString& Prompt)
{
	DraftGitHubToken = Token;
	DraftModel = Model.IsEmpty() ? DefaultCopilotModel : Model;
	DraftModuleName = ModuleName.IsEmpty() ? DefaultModuleName : ModuleName;
	DraftPrompt = Prompt;
}

void UGCAIHotReloadChatWidget::HandleBrowserConsoleMessage(
	const FString& Message,
	const FString& Source,
	int32 Line,
	EWebBrowserConsoleLogSeverity Severity)
{
	(void)Source;
	(void)Line;
	(void)Severity;

	if (!Message.StartsWith(BrowserCommandPrefix))
	{
		return;
	}
}

void UGCAIHotReloadChatWidget::UpdateDraft(
	const FString& Token,
	const FString& Model,
	const FString& ModuleName,
	const FString& Prompt)
{
	UpdateDraftFields(Token, Model, ModuleName, Prompt);
}

void UGCAIHotReloadChatWidget::Send(
	const FString& Token,
	const FString& Model,
	const FString& ModuleName,
	const FString& Prompt)
{
	UpdateDraftFields(Token, Model, ModuleName, Prompt);

	UGCAIHotReloadSubsystem* Subsystem = GetHotReloadSubsystem();
	if (!Subsystem)
	{
		return;
	}

	bIsGenerating = true;
	StatusMessage = TEXT("AI thinking...");

	FGCAIProviderConfig Config = Subsystem->GetProviderConfig();
	Config.bEnabled = true;
	Config.ProviderId = TEXT("github-copilot");
	Config.Transport = EGCAIProviderTransport::GitHubCopilot;
	Config.Model = DraftModel.IsEmpty() ? DefaultCopilotModel : DraftModel;
	if (!DraftGitHubToken.TrimStartAndEnd().IsEmpty())
	{
		Config.ApiKey = DraftGitHubToken;
	}
	Config.BaseUrl = TEXT("https://api.individual.githubcopilot.com");
	Config.ChatCompletionsPath = TEXT("/v1/responses");
	Subsystem->ConfigureProvider(Config);
	Subsystem->SendAgentPrompt(DraftPrompt, DraftModuleName);
	SyncBrowserState(false);
}

void UGCAIHotReloadChatWidget::Login(
	const FString& Token,
	const FString& Model,
	const FString& ModuleName,
	const FString& Prompt)
{
	UpdateDraftFields(Token, Model, ModuleName, Prompt);

	if (UGCAIHotReloadSubsystem* Subsystem = GetHotReloadSubsystem())
	{
		Subsystem->BeginCopilotDeviceLogin();
	}
}

void UGCAIHotReloadChatWidget::Reload()
{
	if (UGCAIHotReloadSubsystem* Subsystem = GetHotReloadSubsystem())
	{
		const bool bAppliedPending = Subsystem->ApplyPendingHotfix();
		if (!bAppliedPending)
		{
			Subsystem->RestartHotfixRuntime();
		}
	}
}

void UGCAIHotReloadChatWidget::HandleChatSessionChanged()
{
	if (UGCAIHotReloadSubsystem* Subsystem = GetHotReloadSubsystem())
	{
		bIsGenerating = Subsystem->IsAgentTurnRunning();
		if (!bIsGenerating && StatusMessage == TEXT("AI thinking..."))
		{
			StatusMessage = TEXT("Ready");
		}
	}

	SyncBrowserState(false);
}

void UGCAIHotReloadChatWidget::HandleRuntimeLog(const FString& Message)
{
	RuntimeLines.Add(TEXT("[runtime] ") + Message);
	SyncBrowserState(false);
}

void UGCAIHotReloadChatWidget::HandleHotfixApplied(const FString& Message)
{
	bIsGenerating = false;
	StatusMessage = TEXT("Hotfix live");
	RuntimeLines.Add(TEXT("[apply] ") + Message);
	SyncBrowserState(false);
}

void UGCAIHotReloadChatWidget::HandleHotfixFailed(const FString& Message)
{
	bIsGenerating = false;
	StatusMessage = TEXT("Error");
	RuntimeLines.Add(TEXT("[error] ") + Message);
	SyncBrowserState(false);
}

void UGCAIHotReloadChatWidget::HandleHotfixGenerated(const FGCAIHotfixGenerationResult& Result)
{
	bIsGenerating = false;
	StatusMessage = Result.Summary.IsEmpty() ? TEXT("Code ready, press Reload") : Result.Summary;
	SyncBrowserState(false);
}

void UGCAIHotReloadChatWidget::HandleCopilotDeviceAuthUpdated(const FGCAICopilotDeviceAuthState& State)
{
	CopilotDeviceAuthState = State;

	if (State.bIsAuthenticated)
	{
		StatusMessage = TEXT("Copilot ready");
	}
	else if (State.bIsPending)
	{
		StatusMessage = TEXT("Waiting for GitHub device approval");
	}

	SyncBrowserState(false);
}
