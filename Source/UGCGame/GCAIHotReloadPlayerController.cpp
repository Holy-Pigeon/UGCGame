#include "GCAIHotReloadPlayerController.h"

#include "Components/InputComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GCAIHotReloadChatWidget.h"
#include "GameFramework/PlayerController.h"

void AGCAIHotReloadPlayerController::BeginPlay()
{
	Super::BeginPlay();

	const UWorld* World = GetWorld();
	const FString MapName = World ? World->GetMapName() : FString();
	const FString CleanMapName = UWorld::RemovePIEPrefix(MapName);
	if (!CleanMapName.EndsWith(TEXT("MainScene")))
	{
		return;
	}

	if (!ChatWidget)
	{
		ChatWidget = CreateWidget<UGCAIHotReloadChatWidget>(this, UGCAIHotReloadChatWidget::StaticClass());
		if (ChatWidget)
		{
			ChatWidget->AddToViewport(1000);
			// Keep the root painted but click-through in the empty area; the browser
			// child stays interactive. We never Collapse it, so CEF keeps its render
			// surface warm and toggling can't leave the panel blank.
			ChatWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
	}

	// Start shown so CEF initializes and paints while it has real geometry; Tab
	// then slides it off-screen instead of collapsing it.
	SetChatVisible(true);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			-1, 12.0f, FColor::Cyan,
			TEXT("按 Tab 打开/关闭 AI 助手聊天窗"));
	}
}

void AGCAIHotReloadPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (InputComponent)
	{
		// Low-level key binding so the toggle works without any input asset setup.
		InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &AGCAIHotReloadPlayerController::ToggleChat);
	}
}

void AGCAIHotReloadPlayerController::ToggleChat()
{
	SetChatVisible(!bChatVisible);
}

void AGCAIHotReloadPlayerController::SetChatVisible(bool bVisible)
{
	bChatVisible = bVisible;

	if (ChatWidget)
	{
		// Hide by sliding fully off the right edge rather than collapsing, so the
		// CEF surface keeps rendering and never comes back blank. 2000px clears the
		// 860px panel past the right edge at any common resolution.
		ChatWidget->SetRenderTranslation(bVisible ? FVector2D::ZeroVector : FVector2D(2000.0f, 0.0f));
	}

	bShowMouseCursor = bVisible;

	if (bVisible)
	{
		FInputModeGameAndUI InputMode;
		if (ChatWidget)
		{
			InputMode.SetWidgetToFocus(ChatWidget->TakeWidget());
		}
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		SetInputMode(InputMode);
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
	}
}
