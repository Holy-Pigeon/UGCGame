#include "GCAIHotReloadChatWidget.h"

#include "Engine/GameInstance.h"
#include "GCAIHotReloadSubsystem.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "GCAIChat"

namespace
{
const FString DefaultModelName = TEXT("claude-opus-4-8");

FSlateFontInfo BodyFont(int32 Size)
{
	return FCoreStyle::GetDefaultFontStyle("Regular", Size);
}

FSlateFontInfo BoldFont(int32 Size)
{
	return FCoreStyle::GetDefaultFontStyle("Bold", Size);
}

FText RoleLabelFor(const FGCAIChatMessage& Message)
{
	if (Message.Kind == TEXT("tool_call"))
	{
		return FText::FromString(FString::Printf(TEXT("工具调用 · %s"), *Message.Title));
	}
	if (Message.Kind == TEXT("tool_result"))
	{
		return FText::FromString(FString::Printf(TEXT("工具结果 · %s"), *Message.Title));
	}
	if (Message.Role == TEXT("user"))
	{
		return LOCTEXT("RoleYou", "你");
	}
	if (Message.Role == TEXT("assistant"))
	{
		return LOCTEXT("RoleAI", "AI");
	}
	return LOCTEXT("RoleSystem", "系统");
}

FString ClampForDisplay(const FString& Text, int32 MaxChars)
{
	if (Text.Len() <= MaxChars)
	{
		return Text;
	}
	return Text.Left(MaxChars) + FString::Printf(TEXT("\n…（省略 %d 字）"), Text.Len() - MaxChars);
}
}

TSharedRef<SWidget> UGCAIHotReloadChatWidget::RebuildWidget()
{
	PanelBrush = FSlateColorBrush(FLinearColor(0.035f, 0.05f, 0.085f, 0.97f));
	HeaderBrush = FSlateColorBrush(FLinearColor(0.09f, 0.12f, 0.19f, 1.0f));
	UserBubbleBrush = FSlateColorBrush(FLinearColor(0.14f, 0.26f, 0.46f, 1.0f));
	AssistantBubbleBrush = FSlateColorBrush(FLinearColor(0.12f, 0.14f, 0.2f, 1.0f));
	ToolBubbleBrush = FSlateColorBrush(FLinearColor(0.13f, 0.17f, 0.12f, 1.0f));

	const FLinearColor SubtleText(0.62f, 0.68f, 0.78f, 1.0f);

	return SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Fill)
		[
			SNew(SBox)
			.WidthOverride(500.0f)
			[
				SNew(SBorder)
				.BorderImage(&PanelBrush)
				.Padding(FMargin(14.0f))
				[
					SNew(SVerticalBox)

					// Header
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(SBorder)
						.BorderImage(&HeaderBrush)
						.Padding(FMargin(12.0f, 8.0f))
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock)
								.Text(LOCTEXT("Title", "AI 助手 · 实时指挥"))
								.Font(BoldFont(15))
								.ColorAndOpacity(FSlateColor(FLinearColor::White))
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 0.0f)
							[
								SAssignNew(StatusText, STextBlock)
								.Text(LOCTEXT("StatusReady", "Ready"))
								.Font(BodyFont(9))
								.ColorAndOpacity(FSlateColor(SubtleText))
							]
						]
					]

					// Base URL row (Anthropic / cc-switch relay)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 5.0f)
					[
						SAssignNew(BaseUrlInput, SEditableTextBox)
						.HintText(LOCTEXT("BaseUrlHint", "Base URL（cc-switch / Anthropic 地址，如 https://...，必填）"))
						.Font(BodyFont(9))
					]

					// Auth / config row
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 6.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 6.0f, 0.0f)
						[
							SAssignNew(TokenInput, SEditableTextBox)
							.IsPassword(true)
							.HintText(LOCTEXT("TokenHint", "API Token（cc-switch / Anthropic 的 auth token）"))
							.Font(BodyFont(9))
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
						[
							SNew(SBox).WidthOverride(140.0f)
							[
								SAssignNew(ModelInput, SEditableTextBox)
								.Text(FText::FromString(DefaultModelName))
								.HintText(LOCTEXT("ModelHint", "model"))
								.Font(BodyFont(9))
							]
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SButton)
							.Text(LOCTEXT("Reset", "清空"))
							.OnClicked(FOnClicked::CreateUObject(this, &UGCAIHotReloadChatWidget::OnResetClicked))
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SAssignNew(AuthText, STextBlock)
						.Text(LOCTEXT("AuthHint", "填 Base URL + Token + model，例如 cc-switch 的地址与 auth token。"))
						.Font(BodyFont(9))
						.ColorAndOpacity(FSlateColor(SubtleText))
						.AutoWrapText(true)
					]

					// Messages
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(SBorder)
						.BorderImage(&HeaderBrush)
						.Padding(FMargin(6.0f))
						[
							SAssignNew(MessageScrollBox, SScrollBox)
							.Orientation(Orient_Vertical)
						]
					]

					// Runtime log
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(SBox).HeightOverride(52.0f)
						[
							SNew(SScrollBox)
							.Orientation(Orient_Vertical)
							+ SScrollBox::Slot()
							[
								SAssignNew(RuntimeLogText, STextBlock)
								.Text(LOCTEXT("NoRuntime", "No runtime events yet."))
								.Font(BodyFont(8))
								.ColorAndOpacity(FSlateColor(SubtleText))
								.AutoWrapText(true)
							]
						]
					]

					// Composer
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 6.0f, 0.0f)
						[
							SNew(SBox).MinDesiredHeight(48.0f)
							[
								SAssignNew(PromptInput, SMultiLineEditableTextBox)
								.HintText(LOCTEXT("PromptHint", "描述你想让 AI 做的事，例如：造座房子、放个药包、让队友去捡了再躲起来。"))
								.Font(BodyFont(10))
								.OnTextCommitted(FOnTextCommitted::CreateUObject(this, &UGCAIHotReloadChatWidget::OnPromptCommitted))
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Fill)
						[
							SNew(SBox).WidthOverride(72.0f)
							[
								SAssignNew(SendButton, SButton)
								.HAlign(HAlign_Center)
								.VAlign(VAlign_Center)
								.Text(LOCTEXT("Send", "发送"))
								.OnClicked(FOnClicked::CreateUObject(this, &UGCAIHotReloadChatWidget::OnSendClicked))
							]
						]
					]
				]
			]
		];
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

		bIsGenerating = Subsystem->IsAgentTurnRunning();

		const FGCAIProviderConfig ProviderConfig = Subsystem->GetProviderConfig();
		if (ModelInput.IsValid() && !ProviderConfig.Model.IsEmpty())
		{
			ModelInput->SetText(FText::FromString(ProviderConfig.Model));
		}
		// Prefill the saved base URL so the connection is remembered across runs.
		if (BaseUrlInput.IsValid() && !ProviderConfig.BaseUrl.IsEmpty())
		{
			BaseUrlInput->SetText(FText::FromString(ProviderConfig.BaseUrl));
		}
	}

	RefreshMessages();
	RefreshStatus();
	RefreshRuntimeLog();
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
	}

	Super::NativeDestruct();
}

void UGCAIHotReloadChatWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	MessageScrollBox.Reset();
	BaseUrlInput.Reset();
	TokenInput.Reset();
	ModelInput.Reset();
	PromptInput.Reset();
	StatusText.Reset();
	AuthText.Reset();
	RuntimeLogText.Reset();
	SendButton.Reset();

	Super::ReleaseSlateResources(bReleaseChildren);
}

UGCAIHotReloadSubsystem* UGCAIHotReloadChatWidget::GetHotReloadSubsystem() const
{
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		return GameInstance->GetSubsystem<UGCAIHotReloadSubsystem>();
	}
	return nullptr;
}

TSharedRef<SWidget> UGCAIHotReloadChatWidget::BuildMessageEntry(const FGCAIChatMessage& Message) const
{
	const bool bIsUser = Message.Role == TEXT("user");
	const bool bIsTool = Message.Kind == TEXT("tool_call") || Message.Kind == TEXT("tool_result");

	const FSlateBrush* BubbleBrush = bIsUser ? &UserBubbleBrush : (bIsTool ? &ToolBubbleBrush : &AssistantBubbleBrush);
	const int32 DisplayLimit = bIsTool ? 700 : 4000;

	return SNew(SBorder)
		.BorderImage(BubbleBrush)
		.Padding(FMargin(10.0f, 7.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
			[
				SNew(STextBlock)
				.Text(RoleLabelFor(Message))
				.Font(BoldFont(8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.78f, 0.88f, 1.0f)))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(ClampForDisplay(Message.Content, DisplayLimit)))
				.Font(BodyFont(bIsTool ? 9 : 10))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.94f, 0.96f, 1.0f, 1.0f)))
				.AutoWrapText(true)
			]
		];
}

void UGCAIHotReloadChatWidget::RefreshMessages()
{
	if (!MessageScrollBox.IsValid())
	{
		return;
	}

	MessageScrollBox->ClearChildren();

	const UGCAIHotReloadSubsystem* Subsystem = GetHotReloadSubsystem();
	const TArray<FGCAIChatMessage> Messages = Subsystem ? Subsystem->GetChatMessages() : TArray<FGCAIChatMessage>();

	if (Messages.Num() == 0)
	{
		MessageScrollBox->AddSlot().Padding(4.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Welcome", "先说出你想做的事。例如：\n· 在我面前造一座塔\n· 放一个药包，让队友去捡\n· 让队友躲到房子后面"))
			.Font(BodyFont(10))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.66f, 0.76f, 1.0f)))
			.AutoWrapText(true)
		];
		return;
	}

	for (const FGCAIChatMessage& Message : Messages)
	{
		MessageScrollBox->AddSlot().Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			BuildMessageEntry(Message)
		];
	}

	MessageScrollBox->ScrollToEnd();
}

void UGCAIHotReloadChatWidget::RefreshStatus()
{
	if (StatusText.IsValid())
	{
		const FString Suffix = bIsGenerating ? TEXT(" · 运行中…") : FString();
		StatusText->SetText(FText::FromString(StatusMessage + Suffix));
	}

	if (SendButton.IsValid())
	{
		SendButton->SetEnabled(!bIsGenerating);
	}

	if (AuthText.IsValid())
	{
		FString AuthLine = TEXT("填 Base URL + Token + model，例如 cc-switch 的地址与 auth token。");
		if (const UGCAIHotReloadSubsystem* Subsystem = GetHotReloadSubsystem())
		{
			const FGCAIProviderConfig Config = Subsystem->GetProviderConfig();
			if (!Config.BaseUrl.IsEmpty() && !Config.ApiKey.IsEmpty())
			{
				AuthLine = FString::Printf(TEXT("已配置：%s · %s"), *Config.BaseUrl, *Config.Model);
			}
		}
		AuthText->SetText(FText::FromString(AuthLine));
	}
}

void UGCAIHotReloadChatWidget::RefreshRuntimeLog()
{
	if (!RuntimeLogText.IsValid())
	{
		return;
	}

	if (RuntimeLines.Num() == 0)
	{
		RuntimeLogText->SetText(LOCTEXT("NoRuntime", "No runtime events yet."));
		return;
	}

	const int32 Start = FMath::Max(0, RuntimeLines.Num() - 12);
	TArray<FString> Recent;
	for (int32 Index = Start; Index < RuntimeLines.Num(); ++Index)
	{
		Recent.Add(RuntimeLines[Index]);
	}
	RuntimeLogText->SetText(FText::FromString(FString::Join(Recent, TEXT("\n"))));
}

void UGCAIHotReloadChatWidget::SubmitPrompt()
{
	if (!PromptInput.IsValid())
	{
		return;
	}

	const FString Prompt = PromptInput->GetText().ToString().TrimStartAndEnd();
	if (Prompt.IsEmpty())
	{
		return;
	}

	UGCAIHotReloadSubsystem* Subsystem = GetHotReloadSubsystem();
	if (!Subsystem)
	{
		return;
	}

	FGCAIProviderConfig Config = Subsystem->GetProviderConfig();
	Config.bEnabled = true;

	// The panel targets an Anthropic Messages endpoint (cc-switch relay / Claude).
	const FString BaseUrl = BaseUrlInput.IsValid() ? BaseUrlInput->GetText().ToString().TrimStartAndEnd() : FString();
	if (!BaseUrl.IsEmpty())
	{
		Config.Transport = EGCAIProviderTransport::Anthropic;
		Config.ProviderId = TEXT("anthropic");
		Config.BaseUrl = BaseUrl;
	}

	if (TokenInput.IsValid())
	{
		const FString Token = TokenInput->GetText().ToString().TrimStartAndEnd();
		if (!Token.IsEmpty())
		{
			Config.ApiKey = Token;
		}
	}
	if (ModelInput.IsValid())
	{
		const FString Model = ModelInput->GetText().ToString().TrimStartAndEnd();
		if (!Model.IsEmpty())
		{
			Config.Model = Model;
		}
	}
	Subsystem->ConfigureProvider(Config);

	bIsGenerating = true;
	StatusMessage = TEXT("AI thinking...");
	RefreshStatus();

	Subsystem->SendAgentPrompt(Prompt, DraftModuleName);
	PromptInput->SetText(FText::GetEmpty());
}

FReply UGCAIHotReloadChatWidget::OnSendClicked()
{
	SubmitPrompt();
	return FReply::Handled();
}

FReply UGCAIHotReloadChatWidget::OnReloadClicked()
{
	if (UGCAIHotReloadSubsystem* Subsystem = GetHotReloadSubsystem())
	{
		if (!Subsystem->ApplyPendingHotfix())
		{
			Subsystem->RestartHotfixRuntime();
		}
	}
	return FReply::Handled();
}

FReply UGCAIHotReloadChatWidget::OnResetClicked()
{
	if (UGCAIHotReloadSubsystem* Subsystem = GetHotReloadSubsystem())
	{
		Subsystem->ResetChatSession();
	}
	RuntimeLines.Reset();
	RefreshMessages();
	RefreshRuntimeLog();
	return FReply::Handled();
}

void UGCAIHotReloadChatWidget::OnPromptCommitted(const FText& Text, ETextCommit::Type CommitType)
{
	// Multiline: Shift+Enter inserts a newline; a plain Enter commits as OnEnter.
	if (CommitType == ETextCommit::OnEnter)
	{
		SubmitPrompt();
	}
}

void UGCAIHotReloadChatWidget::HandleChatSessionChanged()
{
	if (const UGCAIHotReloadSubsystem* Subsystem = GetHotReloadSubsystem())
	{
		bIsGenerating = Subsystem->IsAgentTurnRunning();
		if (!bIsGenerating && StatusMessage == TEXT("AI thinking..."))
		{
			StatusMessage = TEXT("Ready");
		}
	}

	RefreshMessages();
	RefreshStatus();
}

void UGCAIHotReloadChatWidget::HandleRuntimeLog(const FString& Message)
{
	RuntimeLines.Add(TEXT("[runtime] ") + Message);
	RefreshRuntimeLog();
}

void UGCAIHotReloadChatWidget::HandleHotfixApplied(const FString& Message)
{
	bIsGenerating = false;
	StatusMessage = TEXT("Hotfix live");
	RuntimeLines.Add(TEXT("[apply] ") + Message);
	RefreshStatus();
	RefreshRuntimeLog();
}

void UGCAIHotReloadChatWidget::HandleHotfixFailed(const FString& Message)
{
	bIsGenerating = false;
	StatusMessage = TEXT("Error");
	RuntimeLines.Add(TEXT("[error] ") + Message);
	RefreshStatus();
	RefreshRuntimeLog();
}

void UGCAIHotReloadChatWidget::HandleHotfixGenerated(const FGCAIHotfixGenerationResult& Result)
{
	bIsGenerating = false;
	StatusMessage = Result.Summary.IsEmpty() ? TEXT("Code ready") : Result.Summary;
	RefreshStatus();
}

#undef LOCTEXT_NAMESPACE
