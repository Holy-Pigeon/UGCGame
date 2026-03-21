#include "GCAIHotReloadPlayerController.h"

#include "GCAIHotReloadChatWidget.h"
#include "Engine/World.h"
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

	bShowMouseCursor = true;
	SetInputMode(FInputModeGameAndUI());

	if (!ChatWidget)
	{
		ChatWidget = CreateWidget<UGCAIHotReloadChatWidget>(this, UGCAIHotReloadChatWidget::StaticClass());
		if (ChatWidget)
		{
			ChatWidget->AddToViewport(1000);
		}
	}
}
