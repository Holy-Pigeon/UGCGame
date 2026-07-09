#include "GCAIHotfixBridge.h"

#include "Engine/World.h"
#include "GCAIHotReloadSubsystem.h"

void UGCAIHotfixBridge::Initialize(UGCAIHotReloadSubsystem* InOwner)
{
	Owner = InOwner;
}

void UGCAIHotfixBridge::EmitGameplayCommand(const FString& CommandName, const FString& PayloadJson)
{
	if (UGCAIHotReloadSubsystem* OwnerSubsystem = Owner.Get())
	{
		OwnerSubsystem->EmitGameplayCommand(CommandName, PayloadJson);
	}
}

void UGCAIHotfixBridge::LogMessage(const FString& Message)
{
	if (UGCAIHotReloadSubsystem* OwnerSubsystem = Owner.Get())
	{
		OwnerSubsystem->EmitRuntimeLog(Message);
	}
}

float UGCAIHotfixBridge::GetWorldSeconds() const
{
	if (const UGCAIHotReloadSubsystem* OwnerSubsystem = Owner.Get())
	{
		if (const UWorld* World = OwnerSubsystem->GetWorld())
		{
			return World->GetTimeSeconds();
		}
	}

	return 0.0f;
}

FString UGCAIHotfixBridge::GetActiveModuleName() const
{
	if (const UGCAIHotReloadSubsystem* OwnerSubsystem = Owner.Get())
	{
		return OwnerSubsystem->GetActiveModuleName();
	}

	return FString();
}

FString UGCAIHotfixBridge::GetHotfixModuleName() const
{
	if (const UGCAIHotReloadSubsystem* OwnerSubsystem = Owner.Get())
	{
		return OwnerSubsystem->GetBootHotfixModuleName();
	}

	return FString();
}

bool UGCAIHotfixBridge::ExecuteScript(const FString& Code, FString& OutResult) const
{
	if (!ScriptEvalHandler.IsBound())
	{
		OutResult = TEXT("JavaScript runtime is not ready (eval handler not bound).");
		return false;
	}

	OutResult = ScriptEvalHandler.Execute(Code);
	return true;
}
