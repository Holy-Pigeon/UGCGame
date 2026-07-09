#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GCAIHotReloadPlayerController.generated.h"

class UGCAIHotReloadChatWidget;

UCLASS()
class AGCAIHotReloadPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

private:
	void ToggleChat();
	void SetChatVisible(bool bVisible);

	UPROPERTY(Transient)
	TObjectPtr<UGCAIHotReloadChatWidget> ChatWidget;

	bool bChatVisible = false;
};
