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

private:
	UPROPERTY(Transient)
	TObjectPtr<UGCAIHotReloadChatWidget> ChatWidget;
};
