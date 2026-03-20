#include "GCAIHotReloadChatWidget.h"

#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Blueprint/WidgetTree.h"
#include "GCAIHotReloadSubsystem.h"
#include "GCAIHotReloadTypes.h"
#include "Styling/SlateTypes.h"

namespace
{
const FString DefaultCopilotModel = TEXT("gpt-5.2");

FButtonStyle MakePrimaryButtonStyle(const FLinearColor& BaseColor)
{
	FButtonStyle Style = FButtonStyle();
	Style.Normal.TintColor = FSlateColor(BaseColor);
	Style.Hovered.TintColor = FSlateColor(BaseColor * 1.08f);
	Style.Pressed.TintColor = FSlateColor(BaseColor * 0.92f);
	return Style;
}

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

UTextBlock* AddSectionLabel(UVerticalBox* Parent, UWidgetTree* Tree, const TCHAR* Label)
{
	UTextBlock* Text = AddChild(Parent, Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass()));
	Text->SetText(FText::FromString(Label));
	Text->SetColorAndOpacity(FSlateColor(FLinearColor(0.34f, 0.37f, 0.42f, 1.0f)));
	return Text;
}

UTextBlock* MakeMessageText(UWidgetTree* Tree, const FString& Content)
{
	UTextBlock* Text = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Text->SetText(FText::FromString(Content));
	Text->SetAutoWrapText(true);
	return Text;
}
}

void UGCAIHotReloadChatWidget::NativeConstruct()
{
	Super::NativeConstruct();

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

TSharedRef<SWidget> UGCAIHotReloadChatWidget::RebuildWidget()
{
	BuildUi();
	return Super::RebuildWidget();
}

void UGCAIHotReloadChatWidget::BuildUi()
{
	if (WidgetTree->RootWidget)
	{
		return;
	}

	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
	WidgetTree->RootWidget = RootCanvas;

	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Backdrop->SetBrushColor(FLinearColor(0.95f, 0.92f, 0.86f, 0.76f));
	if (UCanvasPanelSlot* BackdropSlot = RootCanvas->AddChildToCanvas(Backdrop))
	{
		BackdropSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		BackdropSlot->SetOffsets(FMargin(0.0f));
	}

	UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Root->SetPadding(FMargin(28.0f));
	Root->SetBrushColor(FLinearColor(0.95f, 0.93f, 0.89f, 0.98f));
	if (UCanvasPanelSlot* RootSlot = RootCanvas->AddChildToCanvas(Root))
	{
		RootSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		RootSlot->SetOffsets(FMargin(28.0f, 28.0f, 28.0f, 28.0f));
	}

	UHorizontalBox* Layout = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Root->SetContent(Layout);

	USizeBox* SidebarBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	SidebarBox->SetWidthOverride(540.0f);
	if (UHorizontalBoxSlot* SidebarSlot = Layout->AddChildToHorizontalBox(SidebarBox))
	{
		SidebarSlot->SetPadding(FMargin(0.0f, 0.0f, 20.0f, 0.0f));
		SidebarSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	}

	UBorder* Sidebar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Sidebar->SetPadding(FMargin(24.0f));
	Sidebar->SetBrushColor(FLinearColor(0.99f, 0.98f, 0.96f, 1.0f));
	SidebarBox->SetContent(Sidebar);

	UVerticalBox* SidebarLayout = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Sidebar->SetContent(SidebarLayout);

	UTextBlock* Title = AddChild(SidebarLayout, WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass()));
	Title->SetText(FText::FromString(TEXT("AI Hotfix Console")));
	Title->SetColorAndOpacity(FSlateColor(FLinearColor(0.12f, 0.15f, 0.18f, 1.0f)));

	UTextBlock* Subtitle = AddChild(SidebarLayout, WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass()));
	Subtitle->SetText(FText::FromString(TEXT("Use GitHub Copilot to generate and reload gameplay hotfix scripts.")));
	Subtitle->SetColorAndOpacity(FSlateColor(FLinearColor(0.40f, 0.43f, 0.48f, 1.0f)));

	AuthStatusBorder = AddChild(SidebarLayout, WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass()));
	AuthStatusBorder->SetPadding(FMargin(14.0f, 12.0f));
	AuthStatusBorder->SetBrushColor(FLinearColor(0.87f, 0.50f, 0.41f, 1.0f));
	AuthStatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	AuthStatusText->SetText(FText::FromString(TEXT("GitHub not connected")));
	AuthStatusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.12f, 0.15f, 0.18f, 1.0f)));
	AuthStatusBorder->SetContent(AuthStatusText);

	StatusText = AddChild(SidebarLayout, WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass()));
	StatusText->SetText(FText::FromString(TEXT("Ready")));
	StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.24f, 0.28f, 0.32f, 1.0f)));

	AddSectionLabel(SidebarLayout, WidgetTree, TEXT("GitHub Token"));
	GitHubTokenTextBox = AddChild(SidebarLayout, WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass()));
	GitHubTokenTextBox->SetHintText(FText::FromString(TEXT("GitHub token / Copilot token")));
	GitHubTokenTextBox->SetIsPassword(true);
	GitHubTokenTextBox->SetForegroundColor(FLinearColor::Black);
	{
		FEditableTextBoxStyle Style = GitHubTokenTextBox->WidgetStyle;
		Style.BackgroundImageNormal.TintColor = FSlateColor(FLinearColor(0.98f, 0.98f, 0.98f, 1.0f));
		Style.BackgroundImageHovered.TintColor = FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
		Style.BackgroundImageFocused.TintColor = FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
		GitHubTokenTextBox->SetWidgetStyle(Style);
	}

	CopilotAuthText = AddChild(SidebarLayout, WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass()));
	CopilotAuthText->SetText(FText::FromString(TEXT("Copilot auth: token paste or device login")));
	CopilotAuthText->SetColorAndOpacity(FSlateColor(FLinearColor(0.32f, 0.36f, 0.40f, 1.0f)));

	AddSectionLabel(SidebarLayout, WidgetTree, TEXT("Model"));
	ModelComboBox = AddChild(SidebarLayout, WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass()));
	ModelComboBox->AddOption(TEXT("gpt-5.2"));
	ModelComboBox->SetSelectedOption(DefaultCopilotModel);

	AddSectionLabel(SidebarLayout, WidgetTree, TEXT("Module"));
	ModuleNameTextBox = AddChild(SidebarLayout, WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass()));
	ModuleNameTextBox->SetHintText(FText::FromString(TEXT("Module name")));
	ModuleNameTextBox->SetText(FText::FromString(TEXT("AIHotfix/Generated/Current")));
	ModuleNameTextBox->SetForegroundColor(FLinearColor::Black);
	{
		FEditableTextBoxStyle Style = ModuleNameTextBox->WidgetStyle;
		Style.BackgroundImageNormal.TintColor = FSlateColor(FLinearColor(0.98f, 0.98f, 0.98f, 1.0f));
		Style.BackgroundImageHovered.TintColor = FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
		Style.BackgroundImageFocused.TintColor = FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
		ModuleNameTextBox->SetWidgetStyle(Style);
	}

	AddSectionLabel(SidebarLayout, WidgetTree, TEXT("Prompt"));
	PromptInputTextBox = AddChild(SidebarLayout, WidgetTree->ConstructWidget<UMultiLineEditableTextBox>(UMultiLineEditableTextBox::StaticClass()), false, 1.0f);
	PromptInputTextBox->SetHintText(FText::FromString(TEXT("Describe the hotfix you want to generate.")));
	PromptInputTextBox->SetForegroundColor(FLinearColor::Black);
	{
		FEditableTextBoxStyle BoxStyle = PromptInputTextBox->WidgetStyle;
		BoxStyle.BackgroundImageNormal.TintColor = FSlateColor(FLinearColor(0.98f, 0.98f, 0.98f, 1.0f));
		BoxStyle.BackgroundImageHovered.TintColor = FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
		BoxStyle.BackgroundImageFocused.TintColor = FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
		PromptInputTextBox->WidgetStyle = BoxStyle;

		FTextBlockStyle PromptStyle = PromptInputTextBox->WidgetStyle.TextStyle;
		PromptStyle.SetColorAndOpacity(FSlateColor(FLinearColor::Black));
		PromptStyle.Font.Size = 18;
		PromptInputTextBox->SetTextStyle(PromptStyle);
	}

	UHorizontalBox* Buttons = AddChild(SidebarLayout, WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass()));
	DeviceLoginButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	if (UHorizontalBoxSlot* Slot = Buttons->AddChildToHorizontalBox(DeviceLoginButton))
	{
		Slot->SetPadding(FMargin(0, 0, 12, 0));
		Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	UTextBlock* DeviceLoginLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	DeviceLoginLabel->SetText(FText::FromString(TEXT("Login")));
	DeviceLoginLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.08f, 0.10f, 0.15f, 1.0f)));
	DeviceLoginButton->AddChild(DeviceLoginLabel);

	GenerateButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	if (UHorizontalBoxSlot* Slot = Buttons->AddChildToHorizontalBox(GenerateButton))
	{
		Slot->SetPadding(FMargin(0, 0, 12, 0));
		Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	UTextBlock* GenerateLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	GenerateLabel->SetText(FText::FromString(TEXT("Generate")));
	GenerateLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	GenerateButton->AddChild(GenerateLabel);
	GenerateButton->SetStyle(MakePrimaryButtonStyle(FLinearColor(0.16f, 0.46f, 0.35f, 1.0f)));

	ReloadButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	if (UHorizontalBoxSlot* Slot = Buttons->AddChildToHorizontalBox(ReloadButton))
	{
		Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	UTextBlock* ReloadLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	ReloadLabel->SetText(FText::FromString(TEXT("Reload")));
	ReloadLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	ReloadButton->AddChild(ReloadLabel);
	ReloadButton->SetStyle(MakePrimaryButtonStyle(FLinearColor(0.21f, 0.36f, 0.58f, 1.0f)));

	DeviceLoginButton->SetStyle(MakePrimaryButtonStyle(FLinearColor(0.88f, 0.84f, 0.76f, 1.0f)));
	DeviceLoginLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.16f, 0.19f, 0.22f, 1.0f)));

	UBorder* ContentBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	ContentBorder->SetPadding(FMargin(24.0f));
	ContentBorder->SetBrushColor(FLinearColor(0.99f, 0.98f, 0.96f, 1.0f));
	if (UHorizontalBoxSlot* ContentSlot = Layout->AddChildToHorizontalBox(ContentBorder))
	{
		ContentSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	UVerticalBox* ContentLayout = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	ContentBorder->SetContent(ContentLayout);

	AddSectionLabel(ContentLayout, WidgetTree, TEXT("Conversation"));
	ChatScrollBox = AddChild(ContentLayout, WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass()), false, 0.78f);
	ChatMessagesBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	ChatScrollBox->AddChild(ChatMessagesBox);

	AddSectionLabel(ContentLayout, WidgetTree, TEXT("Runtime"));
	RuntimeLogTextBox = AddChild(ContentLayout, WidgetTree->ConstructWidget<UMultiLineEditableTextBox>(UMultiLineEditableTextBox::StaticClass()), false, 0.22f);
	RuntimeLogTextBox->SetIsReadOnly(true);
	RuntimeLogTextBox->SetForegroundColor(FLinearColor(0.25f, 0.28f, 0.32f, 1.0f));
	{
		FEditableTextBoxStyle LogStyle = RuntimeLogTextBox->WidgetStyle;
		LogStyle.BackgroundImageNormal.TintColor = FSlateColor(FLinearColor(0.96f, 0.94f, 0.90f, 1.0f));
		LogStyle.BackgroundImageHovered.TintColor = FSlateColor(FLinearColor(0.96f, 0.94f, 0.90f, 1.0f));
		LogStyle.BackgroundImageFocused.TintColor = FSlateColor(FLinearColor(0.96f, 0.94f, 0.90f, 1.0f));
		RuntimeLogTextBox->WidgetStyle = LogStyle;
	}

	DeviceLoginButton->OnClicked.AddDynamic(this, &UGCAIHotReloadChatWidget::HandleDeviceLoginClicked);
	GenerateButton->OnClicked.AddDynamic(this, &UGCAIHotReloadChatWidget::HandleGenerateClicked);
	ReloadButton->OnClicked.AddDynamic(this, &UGCAIHotReloadChatWidget::HandleReloadClicked);
}

void UGCAIHotReloadChatWidget::RefreshFromSubsystem()
{
	if (UGCAIHotReloadSubsystem* Subsystem = GetHotReloadSubsystem())
	{
		const FString SelectedModel = Subsystem->GetProviderConfig().Model;
		if (ModelComboBox)
		{
			if (!SelectedModel.IsEmpty() && ModelComboBox->FindOptionIndex(SelectedModel) != INDEX_NONE)
			{
				ModelComboBox->SetSelectedOption(SelectedModel);
			}
			else
			{
				ModelComboBox->SetSelectedOption(DefaultCopilotModel);
			}
		}

		HandleCopilotDeviceAuthUpdated(Subsystem->GetCopilotDeviceAuthState());
	}

	RebuildTranscript();
}

void UGCAIHotReloadChatWidget::RebuildTranscript()
{
	if (ChatMessagesBox)
	{
		ChatMessagesBox->ClearChildren();

		if (UGCAIHotReloadSubsystem* Subsystem = GetHotReloadSubsystem())
		{
			const TArray<FGCAIChatMessage> Messages = Subsystem->GetChatMessages();
			for (const FGCAIChatMessage& Message : Messages)
			{
				const bool bIsAssistant = Message.Role.Equals(TEXT("assistant"), ESearchCase::IgnoreCase);
				const bool bIsUser = Message.Role.Equals(TEXT("user"), ESearchCase::IgnoreCase);

				UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
				if (UVerticalBoxSlot* RowSlot = ChatMessagesBox->AddChildToVerticalBox(Row))
				{
					RowSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
					RowSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
				}

				if (bIsAssistant)
				{
					USizeBox* Spacer = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
					Spacer->SetWidthOverride(220.0f);
					if (UHorizontalBoxSlot* SpacerSlot = Row->AddChildToHorizontalBox(Spacer))
					{
						SpacerSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
					}
				}

				USizeBox* BubbleSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
				BubbleSizeBox->SetMaxDesiredWidth(860.0f);

				UBorder* Bubble = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
				Bubble->SetPadding(FMargin(18.0f, 14.0f));
				Bubble->SetBrushColor(
					bIsAssistant
						? FLinearColor(0.97f, 0.95f, 0.91f, 1.0f)
						: (bIsUser ? FLinearColor(0.24f, 0.48f, 0.38f, 1.0f) : FLinearColor(0.90f, 0.90f, 0.90f, 1.0f)));
				BubbleSizeBox->SetContent(Bubble);

				UVerticalBox* BubbleLayout = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
				Bubble->SetContent(BubbleLayout);

				UTextBlock* RoleLabel = AddChild(BubbleLayout, WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass()));
				RoleLabel->SetText(FText::FromString(bIsAssistant ? TEXT("AI") : (bIsUser ? TEXT("You") : Message.Role)));
				RoleLabel->SetColorAndOpacity(FSlateColor(
					bIsAssistant
						? FLinearColor(0.52f, 0.46f, 0.32f, 1.0f)
						: FLinearColor(0.90f, 0.96f, 0.93f, 1.0f)));

				UTextBlock* BodyText = AddChild(BubbleLayout, MakeMessageText(WidgetTree, Message.Content));
				BodyText->SetColorAndOpacity(FSlateColor(
					bIsAssistant
						? FLinearColor(0.14f, 0.17f, 0.20f, 1.0f)
						: FLinearColor::White));

				if (UHorizontalBoxSlot* BubbleSlot = Row->AddChildToHorizontalBox(BubbleSizeBox))
				{
					BubbleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
					BubbleSlot->SetHorizontalAlignment(bIsAssistant ? HAlign_Left : HAlign_Right);
				}

				if (!bIsAssistant)
				{
					USizeBox* Spacer = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
					Spacer->SetWidthOverride(220.0f);
					if (UHorizontalBoxSlot* SpacerSlot = Row->AddChildToHorizontalBox(Spacer))
					{
						SpacerSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
					}
				}
			}
		}
	}

	if (ChatScrollBox)
	{
		ChatScrollBox->ScrollToEnd();
	}

	RebuildRuntimeLog();
}

void UGCAIHotReloadChatWidget::RebuildRuntimeLog()
{
	FString Transcript;
	for (const FString& RuntimeLine : RuntimeLines)
	{
		Transcript = Transcript.IsEmpty() ? RuntimeLine : Transcript + TEXT("\n") + RuntimeLine;
	}

	if (RuntimeLogTextBox)
	{
		RuntimeLogTextBox->SetText(FText::FromString(Transcript));
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
	Config.Model = ModelComboBox ? ModelComboBox->GetSelectedOption() : DefaultCopilotModel;
	if (GitHubTokenTextBox && !GitHubTokenTextBox->GetText().ToString().TrimStartAndEnd().IsEmpty())
	{
		Config.ApiKey = GitHubTokenTextBox->GetText().ToString();
	}
	Config.BaseUrl = TEXT("https://api.individual.githubcopilot.com");
	Config.ChatCompletionsPath = TEXT("/v1/responses");
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
		const FString StatusMessage = Result.Summary.IsEmpty()
			? TEXT("Code ready, press Reload")
			: Result.Summary;
		StatusText->SetText(FText::FromString(StatusMessage));
	}
	RebuildTranscript();
}

void UGCAIHotReloadChatWidget::HandleCopilotDeviceAuthUpdated(const FGCAICopilotDeviceAuthState& State)
{
	if (AuthStatusBorder)
	{
		const FLinearColor AuthColor = State.bIsAuthenticated
			? FLinearColor(0.12f, 0.36f, 0.22f, 1.0f)
			: (State.bIsPending ? FLinearColor(0.46f, 0.32f, 0.08f, 1.0f) : FLinearColor(0.35f, 0.17f, 0.12f, 1.0f));
		AuthStatusBorder->SetBrushColor(AuthColor);
	}

	if (AuthStatusText)
	{
		const FString AuthSummary = State.bIsAuthenticated
			? TEXT("GitHub connected")
			: (State.bIsPending ? TEXT("Waiting for GitHub approval") : TEXT("GitHub not connected"));
		AuthStatusText->SetText(FText::FromString(AuthSummary));
	}

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

	if (StatusText)
	{
		if (State.bIsAuthenticated)
		{
			StatusText->SetText(FText::FromString(TEXT("Copilot ready")));
		}
		else if (State.bIsPending)
		{
			StatusText->SetText(FText::FromString(TEXT("Waiting for GitHub device approval")));
		}
	}

	if (GitHubTokenTextBox && State.bIsAuthenticated && GitHubTokenTextBox->GetText().ToString().TrimStartAndEnd().IsEmpty())
	{
		GitHubTokenTextBox->SetHintText(FText::FromString(TEXT("GitHub device login active")));
	}
}
