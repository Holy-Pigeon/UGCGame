#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GCAIHotReloadTypes.h"
#include "GCAIHotReloadChatWidget.generated.h"

class UButton;
class UBorder;
class UComboBoxString;
class UEditableTextBox;
class UMultiLineEditableTextBox;
class UScrollBox;
class UTextBlock;
class UVerticalBox;

UCLASS()
class UGCAIHotReloadChatWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;

private:
	void BuildUi();
	void RefreshFromSubsystem();
	void RebuildTranscript();
	void RebuildRuntimeLog();
	class UGCAIHotReloadSubsystem* GetHotReloadSubsystem() const;

	UFUNCTION()
	void HandleGenerateClicked();

	UFUNCTION()
	void HandleDeviceLoginClicked();

	UFUNCTION()
	void HandleReloadClicked();

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

	UPROPERTY()
	TObjectPtr<UScrollBox> ChatScrollBox;

	UPROPERTY()
	TObjectPtr<UVerticalBox> ChatMessagesBox;

	UPROPERTY()
	TObjectPtr<UMultiLineEditableTextBox> RuntimeLogTextBox;

	UPROPERTY()
	TObjectPtr<UMultiLineEditableTextBox> PromptInputTextBox;

	UPROPERTY()
	TObjectPtr<UEditableTextBox> GitHubTokenTextBox;

	UPROPERTY()
	TObjectPtr<UComboBoxString> ModelComboBox;

	UPROPERTY()
	TObjectPtr<UTextBlock> CopilotAuthText;

	UPROPERTY()
	TObjectPtr<UEditableTextBox> ModuleNameTextBox;

	UPROPERTY()
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY()
	TObjectPtr<UBorder> AuthStatusBorder;

	UPROPERTY()
	TObjectPtr<UTextBlock> AuthStatusText;

	UPROPERTY()
	TObjectPtr<UButton> DeviceLoginButton;

	UPROPERTY()
	TObjectPtr<UButton> GenerateButton;

	UPROPERTY()
	TObjectPtr<UButton> ReloadButton;

	TArray<FString> RuntimeLines;
};
