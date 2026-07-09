#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Interfaces/IHttpRequest.h"
#include "GCAgentTypes.h"
#include "GCAIHotReloadTypes.h"
#include "GCAgentKernel.generated.h"

// Per-request streaming accumulator. Written by the HTTP worker thread under
// Lock; the kernel only reads it back on the game thread once the request
// completes, so it lives outside the UObject.
struct FGCAgentStreamState
{
	struct FToolCall
	{
		FString Id;
		FString Name;
		FString ArgumentsJson;
	};

	FCriticalSection Lock;
	TArray<uint8> ByteBuffer;
	FString AssistantText;
	TArray<FToolCall> ToolCalls;
	FString FinishReason;
	FString RawBody;
	bool bSawSseData = false;
	double LastFlushSeconds = 0.0;

	// Anthropic streams index content blocks globally; map a block index to the
	// ToolCalls slot it feeds so input_json_delta chunks land in the right call.
	TMap<int32, int32> AnthropicBlockToTool;
};

/**
 * Minimal ReAct agent kernel, structured after smolagents:
 * memory steps -> provider messages -> model turn (native function calling,
 * SSE streaming) -> execute tools -> observations back into memory -> loop
 * until the model answers without tool calls or the step budget runs out.
 *
 * Also handles token-budget context compaction (LLM summary of the oldest
 * steps, hard truncation as fallback) and JSON session persistence.
 */
UCLASS()
class UGCAgentKernel : public UObject
{
	GENERATED_BODY()

public:
	void Configure(const FGCAIProviderConfig& InConfig, const FString& InSessionFilePath, int32 InContextTokenBudget, int32 InMaxSteps, int32 InKeepRecentSteps);
	// Endpoint actually used for requests; lets the owner inject short-lived
	// tokens without touching the persisted provider config.
	void SetEndpointOverride(const FString& BaseUrl, const FString& ApiKey, const TMap<FString, FString>& ExtraHeaders);
	void ClearEndpointOverride();
	void SetSystemPrompt(const FString& InSystemPrompt);

	void RegisterTool(FGCAgentToolDefinition&& Definition);
	void ClearTools();

	bool IsRunning() const { return bRunning; }
	void RunTask(const FString& UserText);
	void Cancel();
	void ResetSession();

	const TArray<FGCAgentMemoryStep>& GetMemory() const { return Memory; }

	FGCAgentTextDelegate OnAssistantDelta;
	FGCAgentTextDelegate OnAssistantMessage;
	FGCAgentToolEventDelegate OnToolEvent;
	FGCAgentRunFinishedDelegate OnRunFinished;
	FSimpleMulticastDelegate OnMemoryChanged;

private:
	void BeginNextTurn();
	void BeginModelTurn();
	void BeginCompaction(int32 StepsToCompact);
	void HandleCompactionResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, int32 StepsToCompact);
	void ApplyCompactionSummary(int32 StepsToCompact, const FString& SummaryText);
	void HandleTurnCompleted(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
	void FinishRun(bool bSuccess, const FString& FinalTextOrError);
	void AppendStep(FGCAgentMemoryStep&& Step);
	void ExecuteToolCallsAndContinue(const TArray<FGCAgentStreamState::FToolCall>& ToolCalls);

	bool IsAnthropic() const;
	TSharedRef<FJsonObject> BuildChatRequestBody(bool bStreaming, bool bIncludeTools) const;
	TArray<TSharedPtr<FJsonValue>> BuildProviderMessages() const;
	// Anthropic Messages API shaping (Claude / cc-switch relays).
	TSharedRef<FJsonObject> BuildAnthropicRequestBody(bool bStreaming, bool bIncludeTools) const;
	TArray<TSharedPtr<FJsonValue>> BuildAnthropicMessages() const;
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> CreateProviderRequest(const FString& BodyJson, bool bStreaming) const;
	static void HandleAnthropicSseData(const FString& JsonPayload, FGCAgentStreamState& State);
	static bool ParseAnthropicMessage(const FString& Body, FString& OutText, TArray<FGCAgentStreamState::FToolCall>& OutToolCalls);

	int32 EstimateMemoryTokens() const;
	// Returns the number of leading steps to fold into a summary, or 0.
	int32 ComputeStepsToCompact() const;
	FString BuildCompactionTranscript(int32 StepsToCompact) const;

	void SaveSession() const;
	void LoadSession();

	static bool ParseCompleteChatCompletion(const FString& Body, FString& OutText, TArray<FGCAgentStreamState::FToolCall>& OutToolCalls);
	static FString TruncateObservation(const FString& Text);

	FGCAIProviderConfig Config;
	FString SystemPrompt;
	FString SessionFilePath;
	int32 ContextTokenBudget = 24000;
	int32 MaxSteps = 24;
	int32 KeepRecentSteps = 12;
	bool bSessionLoaded = false;

	bool bHasEndpointOverride = false;
	FString OverrideBaseUrl;
	FString OverrideApiKey;
	TMap<FString, FString> OverrideExtraHeaders;

	TArray<FGCAgentMemoryStep> Memory;
	TArray<FGCAgentToolDefinition> Tools;

	bool bRunning = false;
	int32 StepsRemaining = 0;
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> ActiveRequest;
	TSharedPtr<FGCAgentStreamState, ESPMode::ThreadSafe> StreamState;
};
