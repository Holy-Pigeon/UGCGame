#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GCGameplayCommandSubsystem.generated.h"

class AGCAITeammateCharacter;
class AGCAITeammateController;
class AGCBuildingActor;
class AGCPickupItem;

/**
 * World-side executor for agent gameplay commands. Backs the kernel's
 * spawn_building / spawn_pickup / query_world / command_teammate tools and is
 * the real consumer of UGCAIHotReloadSubsystem::OnGameplayCommand, so hotfix
 * JavaScript can drive the same actions via bridge.EmitGameplayCommand.
 *
 * Every entry point returns a human/model-readable result string that becomes
 * the tool observation.
 */
UCLASS()
class UGCGameplayCommandSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "UGC|Gameplay Commands")
	FString SpawnBuilding(const FString& BuildingType, FVector Location, const FString& Label, bool bSnapToGround = true);

	UFUNCTION(BlueprintCallable, Category = "UGC|Gameplay Commands")
	FString SpawnPickup(const FString& ItemName, FVector Location, bool bSnapToGround = true);

	UFUNCTION(BlueprintCallable, Category = "UGC|Gameplay Commands")
	FString QueryWorld();

	// Action: move_to | pick_up | hide | follow | stop. TargetName matches a
	// pickup/building/hide-spot/teammate label; Location is used when no target
	// is given. Spawns the teammate on first use.
	UFUNCTION(BlueprintCallable, Category = "UGC|Gameplay Commands")
	FString CommandTeammate(const FString& TeammateName, const FString& Action, const FString& TargetName, FVector Location);

	UFUNCTION(BlueprintCallable, Category = "UGC|Gameplay Commands")
	FString EnsureTeammate(const FString& TeammateName);

private:
	UFUNCTION()
	void HandleGameplayCommand(const FString& CommandName, const FString& PayloadJson);

	AGCAITeammateCharacter* FindTeammate(const FString& TeammateName) const;
	AGCAITeammateCharacter* FindOrSpawnTeammate(const FString& TeammateName, FString& OutError);
	AGCAITeammateController* GetTeammateController(AGCAITeammateCharacter* Teammate) const;
	APawn* GetPlayerPawn() const;
	AActor* FindNamedTarget(const FString& TargetName) const;
	FVector SnapToGround(const FVector& Location, float ProbeHalfHeight = 5000.0f) const;
	FVector ComputeHideLocation(const AActor* HideTarget, const FVector& FallbackLocation) const;

	bool bBoundToHotReload = false;
};
