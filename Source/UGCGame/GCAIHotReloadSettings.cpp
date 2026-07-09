#include "GCAIHotReloadSettings.h"

UGCAIHotReloadSettings::UGCAIHotReloadSettings()
{
	DefaultProvider.bEnabled = true;
	DefaultProvider.ProviderId = TEXT("anthropic");
	DefaultProvider.Transport = EGCAIProviderTransport::Anthropic;
	DefaultProvider.BaseUrl = FString();
	DefaultProvider.ChatCompletionsPath = TEXT("/v1/messages");
	DefaultProvider.Model = TEXT("claude-opus-4-8");
}
