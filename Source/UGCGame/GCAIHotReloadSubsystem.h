#pragma once

#include "CoreMinimal.h"
#include "JsEnv.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/IHttpRequest.h"
#include "GCAIHotReloadTypes.h"
#include "GCAIHotReloadSubsystem.generated.h"

class UGCAIHotfixBridge;

UCLASS(BlueprintType)
class UGCAIHotReloadSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "UGC|AI Hotfix")
	void ConfigureProvider(const FGCAIProviderConfig& InProviderConfig);

	UFUNCTION(BlueprintPure, Category = "UGC|AI Hotfix")
	FGCAIProviderConfig GetProviderConfig() const;

	UFUNCTION(BlueprintPure, Category = "UGC|AI Hotfix")
	bool IsRuntimeReady() const;

	UFUNCTION(BlueprintPure, Category = "UGC|AI Hotfix")
	FString GetActiveModuleName() const;

	UFUNCTION(BlueprintPure, Category = "UGC|AI Hotfix")
	FString GetGeneratedModuleName() const;

	UFUNCTION(BlueprintCallable, Category = "UGC|AI Hotfix")
	bool ApplyHotfixJavaScript(const FString& ModuleName, const FString& SourceCode);

	UFUNCTION(BlueprintCallable, Category = "UGC|AI Hotfix")
	void RestartHotfixRuntime();

	UFUNCTION(BlueprintCallable, Category = "UGC|AI Hotfix")
	void GenerateHotfixFromPrompt(const FString& Prompt, const FString& ModuleName);

	UFUNCTION(BlueprintCallable, Category = "UGC|AI Hotfix")
	void BeginCopilotDeviceLogin();

	UFUNCTION(BlueprintCallable, Category = "UGC|AI Hotfix")
	void CancelCopilotDeviceLogin();

	UFUNCTION(BlueprintPure, Category = "UGC|AI Hotfix")
	FString GetHotfixDirectoryOnDisk() const;

	UFUNCTION(BlueprintCallable, Category = "UGC|AI Hotfix")
	bool ApplyPendingHotfix();

	UFUNCTION(BlueprintPure, Category = "UGC|AI Hotfix")
	bool HasPendingHotfix() const;

	UFUNCTION(BlueprintPure, Category = "UGC|AI Hotfix")
	FGCAIHotfixGenerationResult GetLastGeneratedResult() const;

	UFUNCTION(BlueprintPure, Category = "UGC|AI Hotfix")
	TArray<FGCAIChatMessage> GetChatMessages() const;

	UFUNCTION(BlueprintPure, Category = "UGC|AI Hotfix")
	FGCAICopilotDeviceAuthState GetCopilotDeviceAuthState() const;

	void EmitGameplayCommand(const FString& CommandName, const FString& PayloadJson);
	void EmitRuntimeLog(const FString& Message);

	UPROPERTY(BlueprintAssignable, Category = "UGC|AI Hotfix")
	FGCAIHotfixMessageDelegate OnRuntimeLog;

	UPROPERTY(BlueprintAssignable, Category = "UGC|AI Hotfix")
	FGCAIHotfixMessageDelegate OnHotfixApplied;

	UPROPERTY(BlueprintAssignable, Category = "UGC|AI Hotfix")
	FGCAIHotfixMessageDelegate OnHotfixFailed;

	UPROPERTY(BlueprintAssignable, Category = "UGC|AI Hotfix")
	FGCAIHotfixCommandDelegate OnGameplayCommand;

	UPROPERTY(BlueprintAssignable, Category = "UGC|AI Hotfix")
	FGCAIHotfixGeneratedDelegate OnHotfixGenerated;

	UPROPERTY(BlueprintAssignable, Category = "UGC|AI Hotfix")
	FGCAIHotfixSimpleDelegate OnChatSessionChanged;

	UPROPERTY(BlueprintAssignable, Category = "UGC|AI Hotfix")
	FGCAICopilotDeviceAuthUpdatedDelegate OnCopilotDeviceAuthUpdated;

private:
	void ShutdownJsEnv();
	bool StartJsEnvForModule(const FString& ModuleName, FString& OutError);
	bool WriteHotfixFiles(const FString& ModuleName, const FString& SourceCode, FString& OutModuleName, FString& OutError) const;
	FString NormalizeModuleName(const FString& ProposedName) const;
	FString GetAbsoluteScriptPathForModule(const FString& ModuleName, const FString& Extension) const;
	FString BuildSystemPrompt() const;
	void BeginGenerateRequest(const FString& Prompt, const FString& NormalizedModuleName, const FString& BaseUrl, const FString& AuthToken);
	void AppendChatMessage(const FString& Role, const FString& Content);
	void BroadcastChatSessionChanged();
	void BroadcastCopilotDeviceAuthUpdated();
	void ScheduleCopilotDeviceTokenPoll(float DelaySeconds);
	void ResetCopilotDeviceLogin(bool bKeepAuthenticatedState);
	void HandleCopilotDeviceCodeResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
	void HandleCopilotDeviceAccessTokenResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
	void HandleCopilotTokenResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, FString Prompt, FString TargetModuleName);
	void HandleGenerateHotfixResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, FString TargetModuleName);
	bool TryParseGenerationResponse(const FString& ResponseText, FGCAIHotfixGenerationResult& OutResult) const;
	bool TryParseCopilotDeviceCodeResponse(const FString& ResponseText, FGCAICopilotDeviceAuthState& OutState, FString& OutDeviceCode) const;
	bool TryParseCopilotDeviceAccessTokenResponse(const FString& ResponseText, FString& OutAccessToken, FString& OutErrorCode, FString& OutErrorDescription, int32& OutIntervalSeconds) const;
	bool TryParseCopilotTokenResponse(const FString& ResponseText, FString& OutToken, FString& OutBaseUrl, int64& OutExpiresAtUnixMs) const;
	static FString ExtractFirstCodeBlock(const FString& Text);
	static FString DeriveCopilotBaseUrlFromToken(const FString& Token);

	FGCAIProviderConfig ProviderConfig;
	TUniquePtr<PUERTS_NAMESPACE::FJsEnv> JsEnv;

	UPROPERTY(Transient)
	TObjectPtr<UGCAIHotfixBridge> Bridge;

	FString ActiveModuleName;
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> PendingGenerationRequest;
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> PendingCopilotAuthRequest;
	FString PendingGeneratedModuleName;
	FString PendingGeneratedSource;
	FGCAIHotfixGenerationResult LastGeneratedResult;
	TArray<FGCAIChatMessage> ChatMessages;
	FGCAICopilotDeviceAuthState CopilotDeviceAuthState;
	FString PendingCopilotDeviceCode;
	FTimerHandle CopilotDevicePollTimer;
};
