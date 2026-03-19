#pragma once

#include "CoreMinimal.h"
#include "GCAIHotReloadTypes.generated.h"

UENUM(BlueprintType)
enum class EGCAIProviderTransport : uint8
{
	GitHubCopilot UMETA(DisplayName = "GitHub Copilot"),
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
	FString ProviderId = TEXT("openai-compatible");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Provider")
	EGCAIProviderTransport Transport = EGCAIProviderTransport::OpenAICompatible;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Provider")
	FString BaseUrl = TEXT("https://api.openai.com/v1");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Provider")
	FString ChatCompletionsPath = TEXT("/chat/completions");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Provider")
	FString ApiKey;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Provider")
	FString ApiTokenCachePath = TEXT("Saved/AI/github-copilot.token.json");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Provider")
	FString Model = TEXT("gpt-4.1-mini");

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
};

USTRUCT(BlueprintType)
struct FGCAICopilotDeviceAuthState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Auth")
	bool bIsPending = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Auth")
	bool bIsAuthenticated = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Auth")
	FString StatusMessage;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Auth")
	FString UserCode;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Auth")
	FString VerificationUri;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Auth")
	FString VerificationUriComplete;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Auth")
	int32 PollIntervalSeconds = 5;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Auth")
	int64 ExpiresAtUnixMs = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGCAIHotfixMessageDelegate, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGCAIHotfixCommandDelegate, const FString&, CommandName, const FString&, PayloadJson);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGCAIHotfixGeneratedDelegate, const FGCAIHotfixGenerationResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGCAIHotfixSimpleDelegate);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGCAICopilotDeviceAuthUpdatedDelegate, const FGCAICopilotDeviceAuthState&, State);
