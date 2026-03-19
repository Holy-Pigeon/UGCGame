#include "GCAIHotReloadChatWidget.h"

#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Blueprint/WidgetTree.h"
#include "GCAIHotReloadSubsystem.h"
#include "GCAIHotReloadTypes.h"

namespace
{
template <typename TWidget>
TWidget* AddChild(UVerticalBox* Parent, TWidget* Child, bool bAutoHeight = true, float Fill = 0.0f)
{
	if (UVerticalBoxSlot* Slot = Parent->AddChildToVerticalBox(Child))
	{
		Slot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
		if (bAutoHeight)
		{
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		}
		else
		{
			FSlateChildSize Size(ESlateSizeRule::Fill);
			Size.Value = Fill;
			Slot->SetSize(Size);
		}
	}
	return Child;
}
}

void UGCAIHotReloadChatWidget::NativeConstruct()
{
	Super::NativeConstruct();

	BuildUi();

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

void UGCAIHotReloadChatWidget::BuildUi()
{
	if (WidgetTree->RootWidget)
	{
		return;
	}

	UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Root->SetPadding(FMargin(16.0f));
	Root->SetBrushColor(FLinearColor(0.04f, 0.04f, 0.05f, 0.92f));
	WidgetTree->RootWidget = Root;

	UVerticalBox* Layout = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Root->SetContent(Layout);

	UTextBlock* Title = AddChild(Layout, WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass()));
	Title->SetText(FText::FromString(TEXT("AI Hotfix Chat")));
	Title->SetColorAndOpacity(FSlateColor(FLinearColor::White));

	StatusText = AddChild(Layout, WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass()));
	StatusText->SetText(FText::FromString(TEXT("Ready")));

	GitHubTokenTextBox = AddChild(Layout, WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass()));
	GitHubTokenTextBox->SetHintText(FText::FromString(TEXT("GitHub token / Copilot token")));
	GitHubTokenTextBox->SetIsPassword(true);

	CopilotAuthText = AddChild(Layout, WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass()));
	CopilotAuthText->SetText(FText::FromString(TEXT("Copilot auth: token paste or device login")));

	ModelTextBox = AddChild(Layout, WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass()));
	ModelTextBox->SetHintText(FText::FromString(TEXT("Model")));
	ModelTextBox->SetText(FText::FromString(TEXT("gpt-4o")));

	ModuleNameTextBox = AddChild(Layout, WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass()));
	ModuleNameTextBox->SetHintText(FText::FromString(TEXT("Module name")));
	ModuleNameTextBox->SetText(FText::FromString(TEXT("AIHotfix/Generated/Current")));

	PromptInputTextBox = AddChild(Layout, WidgetTree->ConstructWidget<UMultiLineEditableTextBox>(UMultiLineEditableTextBox::StaticClass()));
	PromptInputTextBox->SetHintText(FText::FromString(TEXT("Describe the hotfix you want to generate.")));

	UHorizontalBox* Buttons = AddChild(Layout, WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass()));
	DeviceLoginButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	if (UHorizontalBoxSlot* Slot = Buttons->AddChildToHorizontalBox(DeviceLoginButton))
	{
		Slot->SetPadding(FMargin(0, 0, 8, 0));
	}
	UTextBlock* DeviceLoginLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	DeviceLoginLabel->SetText(FText::FromString(TEXT("Device Login")));
	DeviceLoginButton->AddChild(DeviceLoginLabel);

	GenerateButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	if (UHorizontalBoxSlot* Slot = Buttons->AddChildToHorizontalBox(GenerateButton))
	{
		Slot->SetPadding(FMargin(0, 0, 8, 0));
	}
	UTextBlock* GenerateLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	GenerateLabel->SetText(FText::FromString(TEXT("Generate")));
	GenerateButton->AddChild(GenerateLabel);

	ReloadButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	Buttons->AddChildToHorizontalBox(ReloadButton);
	UTextBlock* ReloadLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	ReloadLabel->SetText(FText::FromString(TEXT("Reload")));
	ReloadButton->AddChild(ReloadLabel);

	TranscriptTextBox = AddChild(Layout, WidgetTree->ConstructWidget<UMultiLineEditableTextBox>(UMultiLineEditableTextBox::StaticClass()), false, 0.35f);
	TranscriptTextBox->SetIsReadOnly(true);

	CodePreviewTextBox = AddChild(Layout, WidgetTree->ConstructWidget<UMultiLineEditableTextBox>(UMultiLineEditableTextBox::StaticClass()), false, 0.45f);
	CodePreviewTextBox->SetIsReadOnly(true);
	CodePreviewTextBox->SetHintText(FText::FromString(TEXT("Generated hotfix code preview")));

	DeviceLoginButton->OnClicked.AddDynamic(this, &UGCAIHotReloadChatWidget::HandleDeviceLoginClicked);
	GenerateButton->OnClicked.AddDynamic(this, &UGCAIHotReloadChatWidget::HandleGenerateClicked);
	ReloadButton->OnClicked.AddDynamic(this, &UGCAIHotReloadChatWidget::HandleReloadClicked);
}

void UGCAIHotReloadChatWidget::RefreshFromSubsystem()
{
	if (UGCAIHotReloadSubsystem* Subsystem = GetHotReloadSubsystem())
	{
		const FGCAIHotfixGenerationResult LastResult = Subsystem->GetLastGeneratedResult();
		if (CodePreviewTextBox)
		{
			CodePreviewTextBox->SetText(FText::FromString(LastResult.JavaScript));
		}

		HandleCopilotDeviceAuthUpdated(Subsystem->GetCopilotDeviceAuthState());
	}

	RebuildTranscript();
}

void UGCAIHotReloadChatWidget::RebuildTranscript()
{
	FString Transcript;

	if (UGCAIHotReloadSubsystem* Subsystem = GetHotReloadSubsystem())
	{
		const TArray<FGCAIChatMessage> Messages = Subsystem->GetChatMessages();
		for (const FGCAIChatMessage& Message : Messages)
		{
			const FString Prefix = FString::Printf(TEXT("[%s] "), *Message.Role);
			Transcript = Transcript.IsEmpty() ? Prefix + Message.Content : Transcript + TEXT("\n") + Prefix + Message.Content;
		}
	}

	for (const FString& RuntimeLine : RuntimeLines)
	{
		Transcript = Transcript.IsEmpty() ? RuntimeLine : Transcript + TEXT("\n") + RuntimeLine;
	}

	if (TranscriptTextBox)
	{
		TranscriptTextBox->SetText(FText::FromString(Transcript));
	}
}

UGCAIHotReloadSubsystem* UGCAIHotReloadChatWidget::GetHotReloadSubsystem() const
{
	if (!GetGameInstance())
	{
		return nullptr;
	}
	return GetGameInstance()->GetSubsystem<UGCAIHotReloadSubsystem>();
}

void UGCAIHotReloadChatWidget::HandleGenerateClicked()
{
	UGCAIHotReloadSubsystem* Subsystem = GetHotReloadSubsystem();
	if (!Subsystem)
	{
		return;
	}

	FGCAIProviderConfig Config = Subsystem->GetProviderConfig();
	Config.bEnabled = true;
	Config.ProviderId = TEXT("github-copilot");
	Config.Transport = EGCAIProviderTransport::GitHubCopilot;
	Config.Model = ModelTextBox ? ModelTextBox->GetText().ToString() : TEXT("gpt-4o");
	if (GitHubTokenTextBox && !GitHubTokenTextBox->GetText().ToString().TrimStartAndEnd().IsEmpty())
	{
		Config.ApiKey = GitHubTokenTextBox->GetText().ToString();
	}
	Config.BaseUrl = TEXT("https://api.individual.githubcopilot.com");
	Config.ChatCompletionsPath = TEXT("/chat/completions");
	Subsystem->ConfigureProvider(Config);

	if (StatusText)
	{
		StatusText->SetText(FText::FromString(TEXT("Generating hotfix code...")));
	}
	Subsystem->GenerateHotfixFromPrompt(
		PromptInputTextBox ? PromptInputTextBox->GetText().ToString() : FString(),
		ModuleNameTextBox ? ModuleNameTextBox->GetText().ToString() : FString());
}

void UGCAIHotReloadChatWidget::HandleDeviceLoginClicked()
{
	if (UGCAIHotReloadSubsystem* Subsystem = GetHotReloadSubsystem())
	{
		Subsystem->BeginCopilotDeviceLogin();
	}
}

void UGCAIHotReloadChatWidget::HandleReloadClicked()
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
	RefreshFromSubsystem();
}

void UGCAIHotReloadChatWidget::HandleRuntimeLog(const FString& Message)
{
	RuntimeLines.Add(TEXT("[runtime] ") + Message);
	RebuildTranscript();
}

void UGCAIHotReloadChatWidget::HandleHotfixApplied(const FString& Message)
{
	if (StatusText)
	{
		StatusText->SetText(FText::FromString(TEXT("Hotfix live")));
	}
	RuntimeLines.Add(TEXT("[apply] ") + Message);
	RebuildTranscript();
}

void UGCAIHotReloadChatWidget::HandleHotfixFailed(const FString& Message)
{
	if (StatusText)
	{
		StatusText->SetText(FText::FromString(TEXT("Error")));
	}
	RuntimeLines.Add(TEXT("[error] ") + Message);
	RebuildTranscript();
}

void UGCAIHotReloadChatWidget::HandleHotfixGenerated(const FGCAIHotfixGenerationResult& Result)
{
	if (StatusText)
	{
		StatusText->SetText(FText::FromString(TEXT("Code ready, press Reload")));
	}
	if (CodePreviewTextBox)
	{
		CodePreviewTextBox->SetText(FText::FromString(Result.JavaScript));
	}
	RebuildTranscript();
}

void UGCAIHotReloadChatWidget::HandleCopilotDeviceAuthUpdated(const FGCAICopilotDeviceAuthState& State)
{
	if (CopilotAuthText)
	{
		FString AuthText = State.StatusMessage;
		if (!State.UserCode.IsEmpty())
		{
			AuthText += FString::Printf(TEXT(" Code: %s"), *State.UserCode);
		}
		if (!State.VerificationUri.IsEmpty())
		{
			AuthText += FString::Printf(TEXT(" URL: %s"), *State.VerificationUri);
		}
		if (AuthText.IsEmpty())
		{
			AuthText = TEXT("Copilot auth: token paste or device login");
		}
		CopilotAuthText->SetText(FText::FromString(AuthText));
	}

	if (GitHubTokenTextBox && State.bIsAuthenticated && GitHubTokenTextBox->GetText().ToString().TrimStartAndEnd().IsEmpty())
	{
		GitHubTokenTextBox->SetHintText(FText::FromString(TEXT("GitHub device login active")));
	}
}
