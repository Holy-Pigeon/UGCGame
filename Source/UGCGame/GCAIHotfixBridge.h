#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GCAIHotfixBridge.generated.h"

class UGCAIHotReloadSubsystem;

// Bound by the resident AgentRuntime.js module; lets C++ evaluate arbitrary
// JavaScript inside the puerts runtime and get the result back as a string.
DECLARE_DYNAMIC_DELEGATE_RetVal_OneParam(FString, FGCAIScriptEvalDelegate, const FString&, Code);

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

	// Hotfix module the resident runtime should require() on boot; empty when
	// no hotfix has been generated yet.
	UFUNCTION(BlueprintPure, Category = "UGC|AI Hotfix")
	FString GetHotfixModuleName() const;

	// AgentRuntime.js binds this so the run_js agent tool can execute code.
	UPROPERTY(BlueprintReadWrite, Category = "UGC|AI Hotfix")
	FGCAIScriptEvalDelegate ScriptEvalHandler;

	bool ExecuteScript(const FString& Code, FString& OutResult) const;

private:
	TWeakObjectPtr<UGCAIHotReloadSubsystem> Owner;
};
