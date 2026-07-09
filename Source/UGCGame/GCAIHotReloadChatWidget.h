#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "GCAIHotReloadTypes.h"
#include "GCAIHotReloadChatWidget.generated.h"

class SButton;
class SEditableTextBox;
class SMultiLineEditableTextBox;
class SScrollBox;
class STextBlock;
class UGCAIHotReloadSubsystem;

// Native Slate chat panel for the in-game AI agent. Replaces the previous CEF
// WebBrowser UI, which rendered unreliably in PIE.
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
	UGCAIHotReloadSubsystem* GetHotReloadSubsystem() const;

	void RefreshMessages();
	void RefreshStatus();
	void RefreshRuntimeLog();

	void SubmitPrompt();
	FReply OnSendClicked();
	FReply OnReloadClicked();
	FReply OnResetClicked();
	void OnPromptCommitted(const FText& Text, ETextCommit::Type CommitType);

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

	TSharedRef<SWidget> BuildMessageEntry(const FGCAIChatMessage& Message) const;

	TSharedPtr<SScrollBox> MessageScrollBox;
	TSharedPtr<SEditableTextBox> BaseUrlInput;
	TSharedPtr<SEditableTextBox> TokenInput;
	TSharedPtr<SEditableTextBox> ModelInput;
	TSharedPtr<SMultiLineEditableTextBox> PromptInput;
	TSharedPtr<STextBlock> StatusText;
	TSharedPtr<STextBlock> AuthText;
	TSharedPtr<STextBlock> RuntimeLogText;
	TSharedPtr<SButton> SendButton;

	FSlateBrush PanelBrush;
	FSlateBrush HeaderBrush;
	FSlateBrush UserBubbleBrush;
	FSlateBrush AssistantBubbleBrush;
	FSlateBrush ToolBubbleBrush;

	TArray<FString> RuntimeLines;
	FString StatusMessage = TEXT("Ready");
	FString DraftModuleName = TEXT("AIHotfix/Generated/Current");
	bool bIsGenerating = false;
};
