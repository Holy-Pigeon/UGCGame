#include "GCAIHotReloadSettings.h"

UGCAIHotReloadSettings::UGCAIHotReloadSettings()
{
	DefaultProvider.bEnabled = true;
	DefaultProvider.ProviderId = TEXT("github-copilot");
	DefaultProvider.Transport = EGCAIProviderTransport::GitHubCopilot;
	DefaultProvider.BaseUrl = TEXT("https://api.individual.githubcopilot.com");
	DefaultProvider.ChatCompletionsPath = TEXT("/chat/completions");
	DefaultProvider.Model = TEXT("gpt-4o");
}
