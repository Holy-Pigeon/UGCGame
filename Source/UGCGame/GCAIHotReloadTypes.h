#pragma once

#include "CoreMinimal.h"
#include "GCAIHotReloadTypes.generated.h"

UENUM(BlueprintType)
enum class EGCAIProviderTransport : uint8
{
	Anthropic UMETA(DisplayName = "Anthropic Messages (Claude / cc-switch)"),
	OpenAICompatible UMETA(DisplayName = "OpenAI Compatible"),
	OpenClawGateway UMETA(DisplayName = "OpenClaw Gateway"),
	CustomHttp UMETA(DisplayName = "Custom HTTP")
};

USTRUCT(BlueprintType)
struct FGCAIProviderConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Provider")
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Provider")
	FString ProviderId = TEXT("anthropic");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Provider")
	EGCAIProviderTransport Transport = EGCAIProviderTransport::Anthropic;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Provider")
	FString BaseUrl;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Provider")
	FString ChatCompletionsPath = TEXT("/v1/messages");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Provider")
	FString ApiKey;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Provider")
	FString ApiTokenCachePath = TEXT("Saved/AI/provider.json");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Provider")
	FString Model = TEXT("claude-opus-4-8");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Provider")
	float Temperature = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Provider")
	TMap<FString, FString> ExtraHeaders;
};

USTRUCT(BlueprintType)
struct FGCAIHotfixGenerationResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hotfix")
	bool bSuccess = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hotfix")
	FString Summary;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hotfix")
	FString ModuleName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hotfix")
	FString JavaScript;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hotfix")
	FString TypeScript;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hotfix")
	FString Error;
};

USTRUCT(BlueprintType)
struct FGCAIChatMessage
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chat")
	FString Role;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chat")
	FString Content;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chat")
	FString Kind = TEXT("message");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chat")
	FString Title;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGCAIHotfixMessageDelegate, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGCAIHotfixCommandDelegate, const FString&, CommandName, const FString&, PayloadJson);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGCAIHotfixGeneratedDelegate, const FGCAIHotfixGenerationResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGCAIHotfixSimpleDelegate);
