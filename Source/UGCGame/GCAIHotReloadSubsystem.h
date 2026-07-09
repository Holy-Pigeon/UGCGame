#pragma once

#include "CoreMinimal.h"
#include "JsEnv.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GCAIHotReloadTypes.h"
#include "GCAIHotReloadSubsystem.generated.h"

class UGCAgentKernel;
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
	bool IsAgentTurnRunning() const;

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
	void SendAgentPrompt(const FString& Prompt, const FString& ModuleName);

	UFUNCTION(BlueprintCallable, Category = "UGC|AI Hotfix")
	void ResetChatSession();

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

	// Hotfix module the resident AgentRuntime.js should require() on boot.
	FString GetBootHotfixModuleName() const;

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

private:
	void ShutdownJsEnv();
	bool StartAgentJsRuntime(FString& OutError);
	bool WriteHotfixFiles(const FString& ModuleName, const FString& SourceCode, FString& OutModuleName, FString& OutError) const;
	FString NormalizeModuleName(const FString& ProposedName) const;
	FString GetAbsoluteScriptPathForModule(const FString& ModuleName, const FString& Extension) const;
	FString BuildAgentSystemPrompt() const;

	void ConfigureKernel();
	void RegisterKernelTools();
	void StartKernelTask(const FString& Prompt);

	void HandleKernelAssistantDelta(const FString& Text);
	void HandleKernelAssistantMessage(const FString& Text);
	void HandleKernelToolEvent(const FString& ToolName, const FString& Payload, bool bIsResult);
	void HandleKernelRunFinished(bool bSuccess, const FString& FinalTextOrError);

	void AppendChatMessage(const FString& Role, const FString& Content, const FString& Kind = TEXT("message"), const FString& Title = FString());
	void UpdateStreamingAssistantMessage(const FString& Content);
	void BroadcastChatSessionChanged();
	void LoadProviderConfigCache();
	void SaveProviderConfigCache() const;
	FString GetProviderConfigCachePath() const;
	FString GetAgentSessionFilePath() const;

	FGCAIProviderConfig ProviderConfig;
	TUniquePtr<PUERTS_NAMESPACE::FJsEnv> JsEnv;

	UPROPERTY(Transient)
	TObjectPtr<UGCAIHotfixBridge> Bridge;

	UPROPERTY(Transient)
	TObjectPtr<UGCAgentKernel> Kernel;

	FString ActiveModuleName;
	FString BootHotfixModuleName;
	FString CurrentTargetModuleName;
	FString PendingGeneratedModuleName;
	FString PendingGeneratedSource;
	FGCAIHotfixGenerationResult LastGeneratedResult;
	TArray<FGCAIChatMessage> ChatMessages;
};
