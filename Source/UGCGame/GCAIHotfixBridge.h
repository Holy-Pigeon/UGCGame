#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GCAIHotfixBridge.generated.h"

class UGCAIHotReloadSubsystem;

UCLASS(BlueprintType)
class UGCAIHotfixBridge : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UGCAIHotReloadSubsystem* InOwner);

	UFUNCTION(BlueprintCallable, Category = "UGC|AI Hotfix")
	void EmitGameplayCommand(const FString& CommandName, const FString& PayloadJson);

	UFUNCTION(BlueprintCallable, Category = "UGC|AI Hotfix")
	void LogMessage(const FString& Message);

	UFUNCTION(BlueprintPure, Category = "UGC|AI Hotfix")
	float GetWorldSeconds() const;

	UFUNCTION(BlueprintPure, Category = "UGC|AI Hotfix")
	FString GetActiveModuleName() const;

private:
	TWeakObjectPtr<UGCAIHotReloadSubsystem> Owner;
};
