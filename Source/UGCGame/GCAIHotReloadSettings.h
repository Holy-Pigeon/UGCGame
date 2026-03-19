#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GCAIHotReloadTypes.h"
#include "GCAIHotReloadSettings.generated.h"

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="UGC AI Hot Reload"))
class UGCAIHotReloadSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UGCAIHotReloadSettings();

	UPROPERTY(EditAnywhere, Config, Category = "Runtime")
	FString ScriptRoot = TEXT("JavaScript");

	UPROPERTY(EditAnywhere, Config, Category = "Runtime")
	FString GeneratedModuleName = TEXT("AIHotfix/Generated/Current");

	UPROPERTY(EditAnywhere, Config, Category = "Runtime")
	FString TypeScriptMirrorExtension = TEXT(".ts");

	UPROPERTY(EditAnywhere, Config, Category = "AI")
	FGCAIProviderConfig DefaultProvider;

	UPROPERTY(EditAnywhere, Config, Category = "AI", meta=(MultiLine=true))
	FString SystemPrompt = TEXT(
		"You generate Unreal puerts hotfix modules. "
		"Return JSON only with keys summary, javascript, and optional typescript. "
		"The javascript must be CommonJS, must stay inside the provided Bridge API, "
		"and should start with const { argv } = require('puerts'); const bridge = argv.getByName('Bridge');");

	virtual FName GetCategoryName() const override
	{
		return TEXT("Game");
	}
};
