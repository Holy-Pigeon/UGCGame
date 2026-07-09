#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "AITypes.h"
#include "GameFramework/Character.h"
#include "Navigation/PathFollowingComponent.h"
#include "GCAITeammate.generated.h"

class AGCPickupItem;

UENUM(BlueprintType)
enum class EGCTeammateTaskType : uint8
{
	MoveToLocation,
	MoveToActor,
	PickUpItem,
	HideAt,
	FollowActor,
	Wait
};

USTRUCT()
struct FGCTeammateTask
{
	GENERATED_BODY()

	UPROPERTY()
	EGCTeammateTaskType Type = EGCTeammateTaskType::Wait;

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	TWeakObjectPtr<AActor> TargetActor;

	UPROPERTY()
	FString Description;
};

// AI-controlled teammate the agent can order around: move, fetch pickups,
// hide behind cover, follow the player.
UCLASS(BlueprintType)
class AGCAITeammateCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AGCAITeammateCharacter();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UGC|Teammate")
	FString TeammateName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UGC|Teammate")
	TArray<TObjectPtr<AGCPickupItem>> CarriedItems;
};

UCLASS()
class AGCAITeammateController : public AAIController
{
	GENERATED_BODY()

public:
	AGCAITeammateController();

	void EnqueueTask(const FGCTeammateTask& Task);
	void ClearTasks();
	// One-line status for query_world observations.
	FString DescribeStatus() const;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result) override;

private:
	void StartNextTask();
	void CompleteCurrentTask();
	void IssueMoveTo(const FVector& Location, AActor* TargetActor, float AcceptanceRadius);

	TArray<FGCTeammateTask> TaskQueue;

	UPROPERTY()
	FGCTeammateTask CurrentTask;

	bool bTaskActive = false;
	bool bIsHiding = false;
	float FollowRefreshCooldown = 0.0f;
};
