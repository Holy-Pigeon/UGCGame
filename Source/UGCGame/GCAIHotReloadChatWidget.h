#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GCAIHotReloadTypes.h"
#include "GCAIHotReloadChatWidget.generated.h"

enum class EWebBrowserConsoleLogSeverity;
class SWebBrowser;

UCLASS()
class UGCAIHotReloadChatWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	void RefreshFromSubsystem();
	void SyncBrowserState(bool bHydrateInputs);
	void PushBrowserState(bool bHydrateInputs);
	class UGCAIHotReloadSubsystem* GetHotReloadSubsystem() const;
	FString BuildPageStateJson(bool bHydrateInputs) const;
	static FString BuildHtmlDocument();
	void HandleBrowserLoadCompleted();
	void HandleBrowserConsoleMessage(const FString& Message, const FString& Source, int32 Line, EWebBrowserConsoleLogSeverity Severity);
	void LoadBrowserDocument();
	void UpdateDraftFields(const FString& Token, const FString& Model, const FString& ModuleName, const FString& Prompt);

	UFUNCTION()
	void UpdateDraft(const FString& Token, const FString& Model, const FString& ModuleName, const FString& Prompt);

	UFUNCTION()
	void Send(const FString& Token, const FString& Model, const FString& ModuleName, const FString& Prompt);

	UFUNCTION()
	void Login(const FString& Token, const FString& Model, const FString& ModuleName, const FString& Prompt);

	UFUNCTION()
	void Reload();

	UFUNCTION()
	void HandleChatSessionChanged();

	UFUNCTION()
	void HandleRuntimeLog(const FString& Message);

	UFUNCTION()
	void HandleHotfixApplied(const FString& Message);

	UFUNCTION()
	void HandleHotfixFailed(const FString& Message);

	UFUNCTION()
	void HandleHotfixGenerated(const FGCAIHotfixGenerationResult& Result);

	UFUNCTION()
	void HandleCopilotDeviceAuthUpdated(const FGCAICopilotDeviceAuthState& State);

	TSharedPtr<SWebBrowser> BrowserWidget;
	TArray<FString> RuntimeLines;
	FGCAICopilotDeviceAuthState CopilotDeviceAuthState;
	FString StatusMessage = TEXT("Ready");
	FString DraftGitHubToken;
	FString DraftPrompt;
	FString DraftModuleName = TEXT("AIHotfix/Generated/Current");
	FString DraftModel = TEXT("gpt-5.2");
	bool bBrowserReady = false;
	bool bIsGenerating = false;
};
