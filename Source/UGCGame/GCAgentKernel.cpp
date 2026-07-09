#include "GCAgentKernel.h"

#include "Async/Async.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
constexpr int32 MaxObservationChars = 12000;
constexpr double DeltaFlushIntervalSeconds = 0.15;
const FString AnthropicVersion = TEXT("2023-06-01");
constexpr int32 AnthropicMaxTokens = 4096;

FString CondenseJson(const TSharedRef<FJsonObject>& Object)
{
	FString Output;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Object, Writer);
	return Output;
}

TSharedPtr<FJsonObject> ParseJsonObject(const FString& Text)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Root))
	{
		return nullptr;
	}
	return Root;
}

FString JoinUrl(const FString& BaseUrl, const FString& Path)
{
	FString Base = BaseUrl.TrimStartAndEnd();
	while (Base.EndsWith(TEXT("/")))
	{
		Base.LeftChopInline(1, EAllowShrinking::No);
	}

	FString NormalizedPath = Path.TrimStartAndEnd();
	if (NormalizedPath.IsEmpty())
	{
		NormalizedPath = TEXT("/chat/completions");
	}
	if (!NormalizedPath.StartsWith(TEXT("/")))
	{
		NormalizedPath = TEXT("/") + NormalizedPath;
	}

	return Base + NormalizedPath;
}

const FString CompactionSystemPrompt = TEXT(
	"You compress an in-game AI agent's conversation memory. Summarize the transcript below into a compact brief that "
	"preserves: the player's standing goals and instructions, world state changes already made (spawned actors, "
	"teammate orders, applied hotfix modules), important tool results, and any unfinished work. Write plain text, "
	"no code fences, at most 400 words. Use the same language the player used.");
}

void UGCAgentKernel::Configure(const FGCAIProviderConfig& InConfig, const FString& InSessionFilePath, int32 InContextTokenBudget, int32 InMaxSteps, int32 InKeepRecentSteps)
{
	Config = InConfig;
	SessionFilePath = InSessionFilePath;
	ContextTokenBudget = FMath::Max(4000, InContextTokenBudget);
	MaxSteps = FMath::Clamp(InMaxSteps, 1, 128);
	KeepRecentSteps = FMath::Clamp(InKeepRecentSteps, 4, 64);

	if (!bSessionLoaded)
	{
		LoadSession();
		bSessionLoaded = true;
	}
}

void UGCAgentKernel::SetEndpointOverride(const FString& BaseUrl, const FString& ApiKey, const TMap<FString, FString>& ExtraHeaders)
{
	bHasEndpointOverride = true;
	OverrideBaseUrl = BaseUrl;
	OverrideApiKey = ApiKey;
	OverrideExtraHeaders = ExtraHeaders;
}

void UGCAgentKernel::ClearEndpointOverride()
{
	bHasEndpointOverride = false;
	OverrideBaseUrl.Reset();
	OverrideApiKey.Reset();
	OverrideExtraHeaders.Reset();
}

void UGCAgentKernel::SetSystemPrompt(const FString& InSystemPrompt)
{
	SystemPrompt = InSystemPrompt;
}

void UGCAgentKernel::RegisterTool(FGCAgentToolDefinition&& Definition)
{
	Tools.RemoveAll([&Definition](const FGCAgentToolDefinition& Existing) { return Existing.Name == Definition.Name; });
	Tools.Add(MoveTemp(Definition));
}

void UGCAgentKernel::ClearTools()
{
	Tools.Reset();
}

void UGCAgentKernel::RunTask(const FString& UserText)
{
	if (bRunning)
	{
		OnRunFinished.Broadcast(false, TEXT("An agent run is already in progress."));
		return;
	}

	FGCAgentMemoryStep Step;
	Step.Role = TEXT("user");
	Step.Kind = TEXT("task");
	Step.Content = UserText;
	AppendStep(MoveTemp(Step));

	bRunning = true;
	StepsRemaining = MaxSteps;
	BeginNextTurn();
}

void UGCAgentKernel::Cancel()
{
	if (ActiveRequest.IsValid())
	{
		ActiveRequest->OnProcessRequestComplete().Unbind();
		ActiveRequest->CancelRequest();
		ActiveRequest.Reset();
	}
	StreamState.Reset();
	if (bRunning)
	{
		bRunning = false;
		OnRunFinished.Broadcast(false, TEXT("Cancelled."));
	}
}

void UGCAgentKernel::ResetSession()
{
	Cancel();
	Memory.Reset();
	SaveSession();
	OnMemoryChanged.Broadcast();
}

void UGCAgentKernel::BeginNextTurn()
{
	const int32 StepsToCompact = ComputeStepsToCompact();
	if (StepsToCompact > 0)
	{
		BeginCompaction(StepsToCompact);
		return;
	}

	BeginModelTurn();
}

void UGCAgentKernel::BeginModelTurn()
{
	const TSharedRef<FJsonObject> Body = BuildChatRequestBody(/*bStreaming*/ true, /*bIncludeTools*/ true);
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateProviderRequest(CondenseJson(Body), /*bStreaming*/ true);
	Request->OnProcessRequestComplete().BindUObject(this, &UGCAgentKernel::HandleTurnCompleted);
	ActiveRequest = Request;
	Request->ProcessRequest();
}

void UGCAgentKernel::BeginCompaction(int32 StepsToCompact)
{
	const FString Transcript = BuildCompactionTranscript(StepsToCompact);

	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("model"), Config.Model);

	if (IsAnthropic())
	{
		Body->SetNumberField(TEXT("max_tokens"), AnthropicMaxTokens);
		Body->SetStringField(TEXT("system"), CompactionSystemPrompt);

		const TSharedRef<FJsonObject> TextBlock = MakeShared<FJsonObject>();
		TextBlock->SetStringField(TEXT("type"), TEXT("text"));
		TextBlock->SetStringField(TEXT("text"), Transcript);
		TArray<TSharedPtr<FJsonValue>> Blocks;
		Blocks.Add(MakeShared<FJsonValueObject>(TextBlock));

		const TSharedRef<FJsonObject> UserMessage = MakeShared<FJsonObject>();
		UserMessage->SetStringField(TEXT("role"), TEXT("user"));
		UserMessage->SetArrayField(TEXT("content"), Blocks);
		TArray<TSharedPtr<FJsonValue>> Messages;
		Messages.Add(MakeShared<FJsonValueObject>(UserMessage));
		Body->SetArrayField(TEXT("messages"), Messages);
	}
	else
	{
		TArray<TSharedPtr<FJsonValue>> Messages;
		const TSharedRef<FJsonObject> SystemMessage = MakeShared<FJsonObject>();
		SystemMessage->SetStringField(TEXT("role"), TEXT("system"));
		SystemMessage->SetStringField(TEXT("content"), CompactionSystemPrompt);
		Messages.Add(MakeShared<FJsonValueObject>(SystemMessage));

		const TSharedRef<FJsonObject> UserMessage = MakeShared<FJsonObject>();
		UserMessage->SetStringField(TEXT("role"), TEXT("user"));
		UserMessage->SetStringField(TEXT("content"), Transcript);
		Messages.Add(MakeShared<FJsonValueObject>(UserMessage));

		Body->SetArrayField(TEXT("messages"), Messages);
	}

	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = CreateProviderRequest(CondenseJson(Body), /*bStreaming*/ false);
	Request->OnProcessRequestComplete().BindUObject(this, &UGCAgentKernel::HandleCompactionResponse, StepsToCompact);
	ActiveRequest = Request;
	Request->ProcessRequest();
}

void UGCAgentKernel::HandleCompactionResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, int32 StepsToCompact)
{
	ActiveRequest.Reset();

	FString Summary;
	if (bWasSuccessful && Response.IsValid() && Response->GetResponseCode() >= 200 && Response->GetResponseCode() < 300)
	{
		TArray<FGCAgentStreamState::FToolCall> IgnoredToolCalls;
		if (IsAnthropic())
		{
			ParseAnthropicMessage(Response->GetContentAsString(), Summary, IgnoredToolCalls);
		}
		else
		{
			ParseCompleteChatCompletion(Response->GetContentAsString(), Summary, IgnoredToolCalls);
		}
	}

	if (Summary.IsEmpty())
	{
		// Fall back to a lossy note so a summarizer outage never wedges the run.
		Summary = FString::Printf(TEXT("(%d earlier steps were dropped to fit the context budget; their summary is unavailable.)"), StepsToCompact);
	}

	ApplyCompactionSummary(StepsToCompact, Summary);
	BeginModelTurn();
}

void UGCAgentKernel::ApplyCompactionSummary(int32 StepsToCompact, const FString& SummaryText)
{
	if (StepsToCompact <= 0 || StepsToCompact > Memory.Num())
	{
		return;
	}

	Memory.RemoveAt(0, StepsToCompact, EAllowShrinking::No);

	FGCAgentMemoryStep SummaryStep;
	SummaryStep.Role = TEXT("system");
	SummaryStep.Kind = TEXT("summary");
	SummaryStep.Content = SummaryText;
	Memory.Insert(MoveTemp(SummaryStep), 0);

	SaveSession();
	OnMemoryChanged.Broadcast();
}

void UGCAgentKernel::HandleTurnCompleted(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	ActiveRequest.Reset();

	const TSharedPtr<FGCAgentStreamState, ESPMode::ThreadSafe> State = StreamState;
	StreamState.Reset();

	if (!bWasSuccessful || !Response.IsValid())
	{
		FinishRun(false, TEXT("Model request failed before a response was received."));
		return;
	}

	FString AssistantText;
	TArray<FGCAgentStreamState::FToolCall> ToolCalls;
	FString RawBody;
	bool bSawSseData = false;

	if (State.IsValid())
	{
		FScopeLock ScopedLock(&State->Lock);
		AssistantText = State->AssistantText;
		ToolCalls = State->ToolCalls;
		RawBody = State->RawBody;
		bSawSseData = State->bSawSseData;
	}

	const int32 ResponseCode = Response->GetResponseCode();
	if (ResponseCode < 200 || ResponseCode >= 300)
	{
		const FString ErrorBody = RawBody.IsEmpty() ? Response->GetContentAsString() : RawBody;
		FinishRun(false, FString::Printf(TEXT("Model request failed with HTTP %d: %s"), ResponseCode, *ErrorBody.Left(2000)));
		return;
	}

	// Provider may have ignored stream=true and returned a single JSON body.
	if (!bSawSseData && AssistantText.IsEmpty() && ToolCalls.Num() == 0)
	{
		const FString Body = RawBody.IsEmpty() ? Response->GetContentAsString() : RawBody;
		const bool bParsed = IsAnthropic()
			? ParseAnthropicMessage(Body, AssistantText, ToolCalls)
			: ParseCompleteChatCompletion(Body, AssistantText, ToolCalls);
		if (!bParsed)
		{
			FinishRun(false, TEXT("Could not parse the model response."));
			return;
		}
	}

	if (!AssistantText.IsEmpty() || ToolCalls.Num() > 0)
	{
		FGCAgentMemoryStep AssistantStep;
		AssistantStep.Role = TEXT("assistant");
		AssistantStep.Kind = ToolCalls.Num() > 0 ? TEXT("tool_call") : TEXT("message");
		AssistantStep.Content = AssistantText;

		if (ToolCalls.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> ToolCallValues;
			for (const FGCAgentStreamState::FToolCall& ToolCall : ToolCalls)
			{
				const TSharedRef<FJsonObject> ToolCallObject = MakeShared<FJsonObject>();
				ToolCallObject->SetStringField(TEXT("id"), ToolCall.Id);
				ToolCallObject->SetStringField(TEXT("type"), TEXT("function"));
				const TSharedRef<FJsonObject> FunctionObject = MakeShared<FJsonObject>();
				FunctionObject->SetStringField(TEXT("name"), ToolCall.Name);
				FunctionObject->SetStringField(TEXT("arguments"), ToolCall.ArgumentsJson.IsEmpty() ? TEXT("{}") : ToolCall.ArgumentsJson);
				ToolCallObject->SetObjectField(TEXT("function"), FunctionObject);
				ToolCallValues.Add(MakeShared<FJsonValueObject>(ToolCallObject));
			}

			const TSharedRef<FJsonObject> Wrapper = MakeShared<FJsonObject>();
			Wrapper->SetArrayField(TEXT("tool_calls"), ToolCallValues);
			AssistantStep.ToolCallsJson = CondenseJson(Wrapper);
		}

		AppendStep(MoveTemp(AssistantStep));

		if (!AssistantText.IsEmpty())
		{
			OnAssistantMessage.Broadcast(AssistantText);
		}
	}

	if (ToolCalls.Num() == 0)
	{
		FinishRun(true, AssistantText);
		return;
	}

	ExecuteToolCallsAndContinue(ToolCalls);
}

void UGCAgentKernel::ExecuteToolCallsAndContinue(const TArray<FGCAgentStreamState::FToolCall>& ToolCalls)
{
	for (const FGCAgentStreamState::FToolCall& ToolCall : ToolCalls)
	{
		OnToolEvent.Broadcast(ToolCall.Name, ToolCall.ArgumentsJson, /*bIsResult*/ false);

		FString ResultText;
		const FGCAgentToolDefinition* Definition = Tools.FindByPredicate(
			[&ToolCall](const FGCAgentToolDefinition& Candidate) { return Candidate.Name == ToolCall.Name; });

		if (!Definition || !Definition->Executor.IsBound())
		{
			ResultText = FString::Printf(TEXT("Unknown tool: %s"), *ToolCall.Name);
		}
		else
		{
			TSharedPtr<FJsonObject> Args = ParseJsonObject(ToolCall.ArgumentsJson);
			if (!Args.IsValid())
			{
				Args = MakeShared<FJsonObject>();
			}

			const bool bSucceeded = Definition->Executor.Execute(Args.ToSharedRef(), ResultText);
			if (!bSucceeded && !ResultText.StartsWith(TEXT("Error")))
			{
				ResultText = TEXT("Error: ") + ResultText;
			}
		}

		ResultText = TruncateObservation(ResultText);

		FGCAgentMemoryStep ToolStep;
		ToolStep.Role = TEXT("tool");
		ToolStep.Kind = TEXT("tool_result");
		ToolStep.ToolName = ToolCall.Name;
		ToolStep.ToolCallId = ToolCall.Id;
		ToolStep.Content = ResultText;
		AppendStep(MoveTemp(ToolStep));

		OnToolEvent.Broadcast(ToolCall.Name, ResultText, /*bIsResult*/ true);
	}

	--StepsRemaining;
	if (StepsRemaining <= 0)
	{
		FGCAgentMemoryStep LimitStep;
		LimitStep.Role = TEXT("system");
		LimitStep.Kind = TEXT("error");
		LimitStep.Content = TEXT("Step budget for this run was exhausted before the task finished.");
		AppendStep(MoveTemp(LimitStep));
		FinishRun(false, TEXT("已达到本次任务的步数上限，继续发消息可以接着做。"));
		return;
	}

	BeginNextTurn();
}

void UGCAgentKernel::FinishRun(bool bSuccess, const FString& FinalTextOrError)
{
	bRunning = false;
	StepsRemaining = 0;
	OnRunFinished.Broadcast(bSuccess, FinalTextOrError);
}

void UGCAgentKernel::AppendStep(FGCAgentMemoryStep&& Step)
{
	Memory.Add(MoveTemp(Step));
	SaveSession();
	OnMemoryChanged.Broadcast();
}

bool UGCAgentKernel::IsAnthropic() const
{
	return Config.Transport == EGCAIProviderTransport::Anthropic;
}

TSharedRef<FJsonObject> UGCAgentKernel::BuildChatRequestBody(bool bStreaming, bool bIncludeTools) const
{
	if (IsAnthropic())
	{
		return BuildAnthropicRequestBody(bStreaming, bIncludeTools);
	}

	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("model"), Config.Model);
	if (bStreaming)
	{
		Body->SetBoolField(TEXT("stream"), true);
	}
	Body->SetNumberField(TEXT("temperature"), Config.Temperature);

	Body->SetArrayField(TEXT("messages"), BuildProviderMessages());

	if (bIncludeTools && Tools.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ToolValues;
		for (const FGCAgentToolDefinition& Tool : Tools)
		{
			const TSharedRef<FJsonObject> FunctionObject = MakeShared<FJsonObject>();
			FunctionObject->SetStringField(TEXT("name"), Tool.Name);
			FunctionObject->SetStringField(TEXT("description"), Tool.Description);

			TSharedPtr<FJsonObject> SchemaObject = ParseJsonObject(Tool.ParametersSchemaJson);
			if (!SchemaObject.IsValid())
			{
				SchemaObject = MakeShared<FJsonObject>();
				SchemaObject->SetStringField(TEXT("type"), TEXT("object"));
				SchemaObject->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
			}
			FunctionObject->SetObjectField(TEXT("parameters"), SchemaObject);

			const TSharedRef<FJsonObject> ToolObject = MakeShared<FJsonObject>();
			ToolObject->SetStringField(TEXT("type"), TEXT("function"));
			ToolObject->SetObjectField(TEXT("function"), FunctionObject);
			ToolValues.Add(MakeShared<FJsonValueObject>(ToolObject));
		}
		Body->SetArrayField(TEXT("tools"), ToolValues);
		Body->SetStringField(TEXT("tool_choice"), TEXT("auto"));
	}

	return Body;
}

TArray<TSharedPtr<FJsonValue>> UGCAgentKernel::BuildProviderMessages() const
{
	TArray<TSharedPtr<FJsonValue>> Messages;

	auto AddSimpleMessage = [&Messages](const FString& Role, const FString& Content)
	{
		const TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
		Message->SetStringField(TEXT("role"), Role);
		Message->SetStringField(TEXT("content"), Content);
		Messages.Add(MakeShared<FJsonValueObject>(Message));
	};

	if (!SystemPrompt.IsEmpty())
	{
		AddSimpleMessage(TEXT("system"), SystemPrompt);
	}

	for (const FGCAgentMemoryStep& Step : Memory)
	{
		if (Step.Kind == TEXT("summary"))
		{
			AddSimpleMessage(TEXT("system"), TEXT("[Earlier conversation summary]\n") + Step.Content);
			continue;
		}

		if (Step.Role == TEXT("user"))
		{
			AddSimpleMessage(TEXT("user"), Step.Content);
			continue;
		}

		if (Step.Role == TEXT("assistant"))
		{
			const TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
			Message->SetStringField(TEXT("role"), TEXT("assistant"));
			if (!Step.Content.IsEmpty())
			{
				Message->SetStringField(TEXT("content"), Step.Content);
			}

			if (!Step.ToolCallsJson.IsEmpty())
			{
				const TSharedPtr<FJsonObject> Wrapper = ParseJsonObject(Step.ToolCallsJson);
				const TArray<TSharedPtr<FJsonValue>>* ToolCallValues = nullptr;
				if (Wrapper.IsValid() && Wrapper->TryGetArrayField(TEXT("tool_calls"), ToolCallValues) && ToolCallValues)
				{
					Message->SetArrayField(TEXT("tool_calls"), *ToolCallValues);
				}
			}

			Messages.Add(MakeShared<FJsonValueObject>(Message));
			continue;
		}

		if (Step.Role == TEXT("tool"))
		{
			const TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
			Message->SetStringField(TEXT("role"), TEXT("tool"));
			Message->SetStringField(TEXT("tool_call_id"), Step.ToolCallId);
			Message->SetStringField(TEXT("content"), Step.Content);
			Messages.Add(MakeShared<FJsonValueObject>(Message));
			continue;
		}

		if (Step.Role == TEXT("system"))
		{
			AddSimpleMessage(TEXT("system"), Step.Content);
		}
	}

	return Messages;
}

TSharedRef<FJsonObject> UGCAgentKernel::BuildAnthropicRequestBody(bool bStreaming, bool bIncludeTools) const
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("model"), Config.Model);
	Body->SetNumberField(TEXT("max_tokens"), AnthropicMaxTokens);
	if (bStreaming)
	{
		Body->SetBoolField(TEXT("stream"), true);
	}
	if (!SystemPrompt.IsEmpty())
	{
		Body->SetStringField(TEXT("system"), SystemPrompt);
	}

	Body->SetArrayField(TEXT("messages"), BuildAnthropicMessages());

	if (bIncludeTools && Tools.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ToolValues;
		for (const FGCAgentToolDefinition& Tool : Tools)
		{
			const TSharedRef<FJsonObject> ToolObject = MakeShared<FJsonObject>();
			ToolObject->SetStringField(TEXT("name"), Tool.Name);
			ToolObject->SetStringField(TEXT("description"), Tool.Description);

			TSharedPtr<FJsonObject> SchemaObject = ParseJsonObject(Tool.ParametersSchemaJson);
			if (!SchemaObject.IsValid())
			{
				SchemaObject = MakeShared<FJsonObject>();
				SchemaObject->SetStringField(TEXT("type"), TEXT("object"));
				SchemaObject->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
			}
			ToolObject->SetObjectField(TEXT("input_schema"), SchemaObject);
			ToolValues.Add(MakeShared<FJsonValueObject>(ToolObject));
		}
		Body->SetArrayField(TEXT("tools"), ToolValues);
	}

	return Body;
}

TArray<TSharedPtr<FJsonValue>> UGCAgentKernel::BuildAnthropicMessages() const
{
	// Anthropic requires content blocks and alternating user/assistant roles, with
	// every tool_result grouped into the single user turn after the assistant's
	// tool_use. Build (role, blocks) entries, then merge adjacent same-role ones.
	struct FEntry
	{
		FString Role;
		TArray<TSharedPtr<FJsonValue>> Blocks;
	};
	TArray<FEntry> Entries;

	auto TextBlock = [](const FString& Text)
	{
		const TSharedRef<FJsonObject> Block = MakeShared<FJsonObject>();
		Block->SetStringField(TEXT("type"), TEXT("text"));
		Block->SetStringField(TEXT("text"), Text);
		return TSharedPtr<FJsonValue>(MakeShared<FJsonValueObject>(Block));
	};

	auto PushEntry = [&Entries](const FString& Role, TSharedPtr<FJsonValue> Block)
	{
		if (Entries.Num() > 0 && Entries.Last().Role == Role)
		{
			Entries.Last().Blocks.Add(Block);
		}
		else
		{
			FEntry Entry;
			Entry.Role = Role;
			Entry.Blocks.Add(Block);
			Entries.Add(MoveTemp(Entry));
		}
	};

	for (const FGCAgentMemoryStep& Step : Memory)
	{
		if (Step.Kind == TEXT("summary"))
		{
			PushEntry(TEXT("user"), TextBlock(TEXT("[Earlier conversation summary]\n") + Step.Content));
			continue;
		}

		if (Step.Role == TEXT("user"))
		{
			PushEntry(TEXT("user"), TextBlock(Step.Content));
			continue;
		}

		if (Step.Role == TEXT("system"))
		{
			PushEntry(TEXT("user"), TextBlock(Step.Content));
			continue;
		}

		if (Step.Role == TEXT("assistant"))
		{
			if (!Step.Content.IsEmpty())
			{
				PushEntry(TEXT("assistant"), TextBlock(Step.Content));
			}

			if (!Step.ToolCallsJson.IsEmpty())
			{
				const TSharedPtr<FJsonObject> Wrapper = ParseJsonObject(Step.ToolCallsJson);
				const TArray<TSharedPtr<FJsonValue>>* ToolCallValues = nullptr;
				if (Wrapper.IsValid() && Wrapper->TryGetArrayField(TEXT("tool_calls"), ToolCallValues) && ToolCallValues)
				{
					for (const TSharedPtr<FJsonValue>& ToolCallValue : *ToolCallValues)
					{
						const TSharedPtr<FJsonObject>* ToolCallObject = nullptr;
						if (!ToolCallValue.IsValid() || !ToolCallValue->TryGetObject(ToolCallObject) || !ToolCallObject || !ToolCallObject->IsValid())
						{
							continue;
						}

						FString Id;
						(*ToolCallObject)->TryGetStringField(TEXT("id"), Id);
						FString Name;
						FString ArgumentsJson;
						const TSharedPtr<FJsonObject>* FunctionObject = nullptr;
						if ((*ToolCallObject)->TryGetObjectField(TEXT("function"), FunctionObject) && FunctionObject && FunctionObject->IsValid())
						{
							(*FunctionObject)->TryGetStringField(TEXT("name"), Name);
							(*FunctionObject)->TryGetStringField(TEXT("arguments"), ArgumentsJson);
						}

						TSharedPtr<FJsonObject> Input = ParseJsonObject(ArgumentsJson);
						if (!Input.IsValid())
						{
							Input = MakeShared<FJsonObject>();
						}

						const TSharedRef<FJsonObject> ToolUse = MakeShared<FJsonObject>();
						ToolUse->SetStringField(TEXT("type"), TEXT("tool_use"));
						ToolUse->SetStringField(TEXT("id"), Id);
						ToolUse->SetStringField(TEXT("name"), Name);
						ToolUse->SetObjectField(TEXT("input"), Input);
						PushEntry(TEXT("assistant"), MakeShared<FJsonValueObject>(ToolUse));
					}
				}
			}
			continue;
		}

		if (Step.Role == TEXT("tool"))
		{
			const TSharedRef<FJsonObject> ToolResult = MakeShared<FJsonObject>();
			ToolResult->SetStringField(TEXT("type"), TEXT("tool_result"));
			ToolResult->SetStringField(TEXT("tool_use_id"), Step.ToolCallId);
			ToolResult->SetStringField(TEXT("content"), Step.Content);
			PushEntry(TEXT("user"), MakeShared<FJsonValueObject>(ToolResult));
			continue;
		}
	}

	TArray<TSharedPtr<FJsonValue>> Messages;
	for (const FEntry& Entry : Entries)
	{
		const TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
		Message->SetStringField(TEXT("role"), Entry.Role);
		Message->SetArrayField(TEXT("content"), Entry.Blocks);
		Messages.Add(MakeShared<FJsonValueObject>(Message));
	}

	// Anthropic requires the first message to be from the user.
	if (Messages.Num() == 0)
	{
		const TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
		Message->SetStringField(TEXT("role"), TEXT("user"));
		TArray<TSharedPtr<FJsonValue>> Blocks;
		Blocks.Add(TextBlock(TEXT("(no input)")));
		Message->SetArrayField(TEXT("content"), Blocks);
		Messages.Add(MakeShared<FJsonValueObject>(Message));
	}

	return Messages;
}

TSharedRef<IHttpRequest, ESPMode::ThreadSafe> UGCAgentKernel::CreateProviderRequest(const FString& BodyJson, bool bStreaming) const
{
	const FString BaseUrl = bHasEndpointOverride ? OverrideBaseUrl : Config.BaseUrl;
	const FString ApiKey = bHasEndpointOverride ? OverrideApiKey : Config.ApiKey;
	const bool bAnthropic = IsAnthropic();
	const FString Path = bAnthropic ? TEXT("/v1/messages") : Config.ChatCompletionsPath;

	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(JoinUrl(BaseUrl, Path));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	// cc-switch / Claude Code relays authenticate the ANTHROPIC_AUTH_TOKEN as a
	// bearer token; native api.anthropic.com also accepts x-api-key, so send both.
	Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
	if (bAnthropic)
	{
		Request->SetHeader(TEXT("x-api-key"), ApiKey);
		Request->SetHeader(TEXT("anthropic-version"), AnthropicVersion);
	}
	Request->SetHeader(TEXT("Accept"), bStreaming ? TEXT("text/event-stream") : TEXT("application/json"));

	for (const TPair<FString, FString>& HeaderPair : Config.ExtraHeaders)
	{
		Request->SetHeader(HeaderPair.Key, HeaderPair.Value);
	}
	for (const TPair<FString, FString>& HeaderPair : OverrideExtraHeaders)
	{
		Request->SetHeader(HeaderPair.Key, HeaderPair.Value);
	}

	Request->SetContentAsString(BodyJson);

	if (bStreaming)
	{
		const TSharedPtr<FGCAgentStreamState, ESPMode::ThreadSafe> State = MakeShared<FGCAgentStreamState, ESPMode::ThreadSafe>();
		const_cast<UGCAgentKernel*>(this)->StreamState = State;

		TWeakObjectPtr<UGCAgentKernel> WeakThis = const_cast<UGCAgentKernel*>(this);
		Request->SetResponseBodyReceiveStreamDelegateV2(FHttpRequestStreamDelegateV2::CreateLambda(
			[State, WeakThis, bAnthropic](void* Ptr, int64& InOutLength)
			{
				if (!Ptr || InOutLength <= 0)
				{
					return;
				}

				bool bShouldFlushDelta = false;
				FString TextSnapshot;

				{
					FScopeLock ScopedLock(&State->Lock);
					State->ByteBuffer.Append(static_cast<const uint8*>(Ptr), InOutLength);

					// Cut complete lines on '\n' at the byte level so multi-byte
					// UTF-8 sequences split across chunks stay intact.
					int32 LineStart = 0;
					for (int32 Index = 0; Index < State->ByteBuffer.Num(); ++Index)
					{
						if (State->ByteBuffer[Index] != '\n')
						{
							continue;
						}

						const int32 LineLength = Index - LineStart;
						FString Line;
						if (LineLength > 0)
						{
							TArray<uint8> LineBytes;
							LineBytes.Append(State->ByteBuffer.GetData() + LineStart, LineLength);
							LineBytes.Add(0);
							Line = UTF8_TO_TCHAR(reinterpret_cast<const char*>(LineBytes.GetData()));
							Line.TrimEndInline();
						}
						LineStart = Index + 1;

						State->RawBody += Line + TEXT("\n");

						if (!Line.StartsWith(TEXT("data:")))
						{
							continue;
						}

						State->bSawSseData = true;
						const FString Payload = Line.Mid(5).TrimStartAndEnd();
						if (Payload == TEXT("[DONE]") || Payload.IsEmpty())
						{
							continue;
						}

						if (bAnthropic)
						{
							HandleAnthropicSseData(Payload, *State);
							continue;
						}

						const TSharedPtr<FJsonObject> Chunk = ParseJsonObject(Payload);
						if (!Chunk.IsValid())
						{
							continue;
						}

						const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
						if (!Chunk->TryGetArrayField(TEXT("choices"), Choices) || !Choices || Choices->Num() == 0)
						{
							continue;
						}

						const TSharedPtr<FJsonObject>* Choice = nullptr;
						if (!(*Choices)[0]->TryGetObject(Choice) || !Choice || !Choice->IsValid())
						{
							continue;
						}

						FString FinishReason;
						if ((*Choice)->TryGetStringField(TEXT("finish_reason"), FinishReason) && !FinishReason.IsEmpty())
						{
							State->FinishReason = FinishReason;
						}

						const TSharedPtr<FJsonObject>* Delta = nullptr;
						if (!(*Choice)->TryGetObjectField(TEXT("delta"), Delta) || !Delta || !Delta->IsValid())
						{
							continue;
						}

						FString ContentDelta;
						if ((*Delta)->TryGetStringField(TEXT("content"), ContentDelta) && !ContentDelta.IsEmpty())
						{
							State->AssistantText += ContentDelta;
						}

						const TArray<TSharedPtr<FJsonValue>>* ToolCallDeltas = nullptr;
						if ((*Delta)->TryGetArrayField(TEXT("tool_calls"), ToolCallDeltas) && ToolCallDeltas)
						{
							for (const TSharedPtr<FJsonValue>& ToolCallValue : *ToolCallDeltas)
							{
								const TSharedPtr<FJsonObject>* ToolCallDelta = nullptr;
								if (!ToolCallValue.IsValid() || !ToolCallValue->TryGetObject(ToolCallDelta) || !ToolCallDelta || !ToolCallDelta->IsValid())
								{
									continue;
								}

								int32 ToolIndex = 0;
								double IndexNumber = 0.0;
								if ((*ToolCallDelta)->TryGetNumberField(TEXT("index"), IndexNumber))
								{
									ToolIndex = static_cast<int32>(IndexNumber);
								}

								while (State->ToolCalls.Num() <= ToolIndex)
								{
									State->ToolCalls.AddDefaulted();
								}

								FGCAgentStreamState::FToolCall& Accumulated = State->ToolCalls[ToolIndex];

								FString Id;
								if ((*ToolCallDelta)->TryGetStringField(TEXT("id"), Id) && !Id.IsEmpty())
								{
									Accumulated.Id = Id;
								}

								const TSharedPtr<FJsonObject>* FunctionDelta = nullptr;
								if ((*ToolCallDelta)->TryGetObjectField(TEXT("function"), FunctionDelta) && FunctionDelta && FunctionDelta->IsValid())
								{
									FString Name;
									if ((*FunctionDelta)->TryGetStringField(TEXT("name"), Name) && !Name.IsEmpty())
									{
										Accumulated.Name = Name;
									}

									FString ArgumentsDelta;
									if ((*FunctionDelta)->TryGetStringField(TEXT("arguments"), ArgumentsDelta))
									{
										Accumulated.ArgumentsJson += ArgumentsDelta;
									}
								}
							}
						}
					}

					if (LineStart > 0)
					{
						State->ByteBuffer.RemoveAt(0, LineStart, EAllowShrinking::No);
					}

					const double Now = FPlatformTime::Seconds();
					if (!State->AssistantText.IsEmpty() && Now - State->LastFlushSeconds > DeltaFlushIntervalSeconds)
					{
						State->LastFlushSeconds = Now;
						bShouldFlushDelta = true;
						TextSnapshot = State->AssistantText;
					}
				}

				if (bShouldFlushDelta)
				{
					AsyncTask(ENamedThreads::GameThread, [WeakThis, TextSnapshot]()
					{
						if (UGCAgentKernel* Kernel = WeakThis.Get())
						{
							Kernel->OnAssistantDelta.Broadcast(TextSnapshot);
						}
					});
				}
			}));
	}

	return Request;
}

void UGCAgentKernel::HandleAnthropicSseData(const FString& JsonPayload, FGCAgentStreamState& State)
{
	const TSharedPtr<FJsonObject> Data = ParseJsonObject(JsonPayload);
	if (!Data.IsValid())
	{
		return;
	}

	FString Type;
	if (!Data->TryGetStringField(TEXT("type"), Type))
	{
		return;
	}

	auto ReadIndex = [&Data]() -> int32
	{
		double IndexNumber = 0.0;
		Data->TryGetNumberField(TEXT("index"), IndexNumber);
		return static_cast<int32>(IndexNumber);
	};

	if (Type == TEXT("content_block_start"))
	{
		const TSharedPtr<FJsonObject>* ContentBlock = nullptr;
		if (Data->TryGetObjectField(TEXT("content_block"), ContentBlock) && ContentBlock && ContentBlock->IsValid())
		{
			FString BlockType;
			(*ContentBlock)->TryGetStringField(TEXT("type"), BlockType);
			if (BlockType == TEXT("tool_use"))
			{
				FGCAgentStreamState::FToolCall Call;
				(*ContentBlock)->TryGetStringField(TEXT("id"), Call.Id);
				(*ContentBlock)->TryGetStringField(TEXT("name"), Call.Name);
				const int32 ToolIndex = State.ToolCalls.Add(Call);
				State.AnthropicBlockToTool.Add(ReadIndex(), ToolIndex);
			}
		}
		return;
	}

	if (Type == TEXT("content_block_delta"))
	{
		const TSharedPtr<FJsonObject>* Delta = nullptr;
		if (!Data->TryGetObjectField(TEXT("delta"), Delta) || !Delta || !Delta->IsValid())
		{
			return;
		}

		FString DeltaType;
		(*Delta)->TryGetStringField(TEXT("type"), DeltaType);
		if (DeltaType == TEXT("text_delta"))
		{
			FString Text;
			if ((*Delta)->TryGetStringField(TEXT("text"), Text))
			{
				State.AssistantText += Text;
			}
		}
		else if (DeltaType == TEXT("input_json_delta"))
		{
			FString Partial;
			if ((*Delta)->TryGetStringField(TEXT("partial_json"), Partial))
			{
				if (const int32* ToolIndex = State.AnthropicBlockToTool.Find(ReadIndex()))
				{
					if (State.ToolCalls.IsValidIndex(*ToolIndex))
					{
						State.ToolCalls[*ToolIndex].ArgumentsJson += Partial;
					}
				}
			}
		}
		return;
	}

	if (Type == TEXT("message_delta"))
	{
		const TSharedPtr<FJsonObject>* Delta = nullptr;
		if (Data->TryGetObjectField(TEXT("delta"), Delta) && Delta && Delta->IsValid())
		{
			FString StopReason;
			if ((*Delta)->TryGetStringField(TEXT("stop_reason"), StopReason) && !StopReason.IsEmpty())
			{
				State.FinishReason = StopReason;
			}
		}
		return;
	}

	if (Type == TEXT("error"))
	{
		const TSharedPtr<FJsonObject>* Error = nullptr;
		FString Message;
		if (Data->TryGetObjectField(TEXT("error"), Error) && Error && Error->IsValid())
		{
			(*Error)->TryGetStringField(TEXT("message"), Message);
		}
		State.RawBody += TEXT("\n[anthropic error] ") + Message;
	}
}

bool UGCAgentKernel::ParseAnthropicMessage(const FString& Body, FString& OutText, TArray<FGCAgentStreamState::FToolCall>& OutToolCalls)
{
	const TSharedPtr<FJsonObject> Root = ParseJsonObject(Body);
	if (!Root.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Content = nullptr;
	if (!Root->TryGetArrayField(TEXT("content"), Content) || !Content)
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& BlockValue : *Content)
	{
		const TSharedPtr<FJsonObject>* Block = nullptr;
		if (!BlockValue.IsValid() || !BlockValue->TryGetObject(Block) || !Block || !Block->IsValid())
		{
			continue;
		}

		FString BlockType;
		(*Block)->TryGetStringField(TEXT("type"), BlockType);
		if (BlockType == TEXT("text"))
		{
			FString Text;
			if ((*Block)->TryGetStringField(TEXT("text"), Text))
			{
				OutText += Text;
			}
		}
		else if (BlockType == TEXT("tool_use"))
		{
			FGCAgentStreamState::FToolCall Call;
			(*Block)->TryGetStringField(TEXT("id"), Call.Id);
			(*Block)->TryGetStringField(TEXT("name"), Call.Name);

			const TSharedPtr<FJsonObject>* Input = nullptr;
			if ((*Block)->TryGetObjectField(TEXT("input"), Input) && Input && Input->IsValid())
			{
				Call.ArgumentsJson = CondenseJson(Input->ToSharedRef());
			}

			if (!Call.Name.IsEmpty())
			{
				OutToolCalls.Add(MoveTemp(Call));
			}
		}
	}

	return !OutText.IsEmpty() || OutToolCalls.Num() > 0;
}

int32 UGCAgentKernel::EstimateMemoryTokens() const
{
	int32 TotalChars = SystemPrompt.Len();
	for (const FGCAgentMemoryStep& Step : Memory)
	{
		TotalChars += Step.Content.Len() + Step.ToolCallsJson.Len() + 40;
	}

	// Rough mixed CJK/ASCII estimate; good enough for a budget gate.
	return TotalChars / 3;
}

int32 UGCAgentKernel::ComputeStepsToCompact() const
{
	if (EstimateMemoryTokens() <= ContextTokenBudget || Memory.Num() <= KeepRecentSteps + 4)
	{
		return 0;
	}

	int32 StepsToCompact = Memory.Num() - KeepRecentSteps;

	// Never split an assistant tool_call step from its tool results: the OpenAI
	// protocol rejects a tool message whose call is missing from the history.
	while (StepsToCompact < Memory.Num() && Memory[StepsToCompact].Role == TEXT("tool"))
	{
		++StepsToCompact;
	}

	return StepsToCompact >= Memory.Num() ? 0 : StepsToCompact;
}

FString UGCAgentKernel::BuildCompactionTranscript(int32 StepsToCompact) const
{
	FString Transcript;
	for (int32 Index = 0; Index < StepsToCompact && Index < Memory.Num(); ++Index)
	{
		const FGCAgentMemoryStep& Step = Memory[Index];
		FString Label = Step.Role;
		if (!Step.ToolName.IsEmpty())
		{
			Label += TEXT("(") + Step.ToolName + TEXT(")");
		}

		FString Content = Step.Content;
		if (!Step.ToolCallsJson.IsEmpty())
		{
			Content += TEXT("\n") + Step.ToolCallsJson;
		}

		Transcript += FString::Printf(TEXT("[%s] %s\n"), *Label, *Content.Left(2000));
	}

	return Transcript;
}

void UGCAgentKernel::SaveSession() const
{
	if (SessionFilePath.IsEmpty())
	{
		return;
	}

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> StepValues;
	for (const FGCAgentMemoryStep& Step : Memory)
	{
		const TSharedRef<FJsonObject> StepObject = MakeShared<FJsonObject>();
		StepObject->SetStringField(TEXT("role"), Step.Role);
		StepObject->SetStringField(TEXT("content"), Step.Content);
		StepObject->SetStringField(TEXT("kind"), Step.Kind);
		StepObject->SetStringField(TEXT("tool_name"), Step.ToolName);
		StepObject->SetStringField(TEXT("tool_call_id"), Step.ToolCallId);
		StepObject->SetStringField(TEXT("tool_calls_json"), Step.ToolCallsJson);
		StepValues.Add(MakeShared<FJsonValueObject>(StepObject));
	}
	Root->SetArrayField(TEXT("steps"), StepValues);

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(SessionFilePath), true);
	FFileHelper::SaveStringToFile(CondenseJson(Root), *SessionFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void UGCAgentKernel::LoadSession()
{
	if (SessionFilePath.IsEmpty())
	{
		return;
	}

	FString SessionJson;
	if (!FFileHelper::LoadFileToString(SessionJson, *SessionFilePath))
	{
		return;
	}

	const TSharedPtr<FJsonObject> Root = ParseJsonObject(SessionJson);
	if (!Root.IsValid())
	{
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* StepValues = nullptr;
	if (!Root->TryGetArrayField(TEXT("steps"), StepValues) || !StepValues)
	{
		return;
	}

	Memory.Reset();
	for (const TSharedPtr<FJsonValue>& StepValue : *StepValues)
	{
		const TSharedPtr<FJsonObject>* StepObject = nullptr;
		if (!StepValue.IsValid() || !StepValue->TryGetObject(StepObject) || !StepObject || !StepObject->IsValid())
		{
			continue;
		}

		FGCAgentMemoryStep Step;
		(*StepObject)->TryGetStringField(TEXT("role"), Step.Role);
		(*StepObject)->TryGetStringField(TEXT("content"), Step.Content);
		(*StepObject)->TryGetStringField(TEXT("kind"), Step.Kind);
		(*StepObject)->TryGetStringField(TEXT("tool_name"), Step.ToolName);
		(*StepObject)->TryGetStringField(TEXT("tool_call_id"), Step.ToolCallId);
		(*StepObject)->TryGetStringField(TEXT("tool_calls_json"), Step.ToolCallsJson);
		Memory.Add(MoveTemp(Step));
	}

	OnMemoryChanged.Broadcast();
}

bool UGCAgentKernel::ParseCompleteChatCompletion(const FString& Body, FString& OutText, TArray<FGCAgentStreamState::FToolCall>& OutToolCalls)
{
	const TSharedPtr<FJsonObject> Root = ParseJsonObject(Body);
	if (!Root.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
	if (!Root->TryGetArrayField(TEXT("choices"), Choices) || !Choices || Choices->Num() == 0)
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* Choice = nullptr;
	if (!(*Choices)[0]->TryGetObject(Choice) || !Choice || !Choice->IsValid())
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* Message = nullptr;
	if (!(*Choice)->TryGetObjectField(TEXT("message"), Message) || !Message || !Message->IsValid())
	{
		return false;
	}

	(*Message)->TryGetStringField(TEXT("content"), OutText);

	const TArray<TSharedPtr<FJsonValue>>* ToolCallValues = nullptr;
	if ((*Message)->TryGetArrayField(TEXT("tool_calls"), ToolCallValues) && ToolCallValues)
	{
		for (const TSharedPtr<FJsonValue>& ToolCallValue : *ToolCallValues)
		{
			const TSharedPtr<FJsonObject>* ToolCallObject = nullptr;
			if (!ToolCallValue.IsValid() || !ToolCallValue->TryGetObject(ToolCallObject) || !ToolCallObject || !ToolCallObject->IsValid())
			{
				continue;
			}

			FGCAgentStreamState::FToolCall ToolCall;
			(*ToolCallObject)->TryGetStringField(TEXT("id"), ToolCall.Id);

			const TSharedPtr<FJsonObject>* FunctionObject = nullptr;
			if ((*ToolCallObject)->TryGetObjectField(TEXT("function"), FunctionObject) && FunctionObject && FunctionObject->IsValid())
			{
				(*FunctionObject)->TryGetStringField(TEXT("name"), ToolCall.Name);
				(*FunctionObject)->TryGetStringField(TEXT("arguments"), ToolCall.ArgumentsJson);
			}

			if (!ToolCall.Name.IsEmpty())
			{
				OutToolCalls.Add(MoveTemp(ToolCall));
			}
		}
	}

	return !OutText.IsEmpty() || OutToolCalls.Num() > 0;
}

FString UGCAgentKernel::TruncateObservation(const FString& Text)
{
	if (Text.Len() <= MaxObservationChars)
	{
		return Text;
	}

	return Text.Left(MaxObservationChars) + FString::Printf(TEXT("\n\n... truncated (%d chars total)."), Text.Len());
}
