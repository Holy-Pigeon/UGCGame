#include "GCAIHotReloadGameMode.h"

#include "GCAIHotReloadPlayerController.h"

AGCAIHotReloadGameMode::AGCAIHotReloadGameMode()
{
	PlayerControllerClass = AGCAIHotReloadPlayerController::StaticClass();
}
