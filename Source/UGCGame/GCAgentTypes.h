#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "GCAgentTypes.generated.h"

// One entry in the agent's memory. Steps are replayed into provider messages on
// every model turn (smolagents-style "memory as chat messages").
USTRUCT(BlueprintType)
struct FGCAgentMemoryStep
{
	GENERATED_BODY()

	// system | user | assistant | tool
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Agent")
	FString Role;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Agent")
	FString Content;

	// task | message | tool_call | tool_result | summary | error
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Agent")
	FString Kind = TEXT("message");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Agent")
	FString ToolName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Agent")
	FString ToolCallId;

	// Raw provider tool_calls array json, kept verbatim on assistant steps so the
	// exchange can be replayed exactly on later turns.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Agent")
	FString ToolCallsJson;
};

// Executes a tool. Args is the parsed function-call arguments object; the result
// text becomes the tool observation fed back to the model. Return false to mark
// the observation as an error (it is still fed back).
DECLARE_DELEGATE_RetVal_TwoParams(bool, FGCAgentToolExecutor, const TSharedRef<FJsonObject>& /*Args*/, FString& /*OutResult*/);

struct FGCAgentToolDefinition
{
	FString Name;
	FString Description;
	// JSON schema for the function parameters ("{\"type\":\"object\",...}").
	FString ParametersSchemaJson = TEXT("{\"type\":\"object\",\"properties\":{}}");
	FGCAgentToolExecutor Executor;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FGCAgentTextDelegate, const FString&);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FGCAgentToolEventDelegate, const FString& /*ToolName*/, const FString& /*Payload*/, bool /*bIsResult*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FGCAgentRunFinishedDelegate, bool /*bSuccess*/, const FString& /*FinalTextOrError*/);
