#include "GCAITeammate.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GCWorldActors.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Navigation/PathFollowingComponent.h"
#include "UObject/ConstructorHelpers.h"

AGCAITeammateCharacter::AGCAITeammateCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	AIControllerClass = AGCAITeammateController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	GetCapsuleComponent()->InitCapsuleSize(42.0f, 96.0f);
	GetCharacterMovement()->MaxWalkSpeed = 450.0f;
	GetCharacterMovement()->bUseRVOAvoidance = true;

	// Visible placeholder body from engine content so the teammate shows up
	// without any game assets.
	UStaticMeshComponent* Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(GetCapsuleComponent());
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetRelativeLocation(FVector(0.0f, 0.0f, -50.0f));
	Body->SetRelativeScale3D(FVector(0.8f, 0.5f, 1.7f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Body->SetStaticMesh(CubeMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BaseMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BaseMaterial.Succeeded())
	{
		Body->SetMaterial(0, BaseMaterial.Object);
	}

	Tags.Add(TEXT("GCTeammate"));
}

AGCAITeammateController::AGCAITeammateController()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AGCAITeammateController::EnqueueTask(const FGCTeammateTask& Task)
{
	TaskQueue.Add(Task);
	bIsHiding = false;

	if (!bTaskActive)
	{
		StartNextTask();
	}
}

void AGCAITeammateController::ClearTasks()
{
	TaskQueue.Reset();
	bTaskActive = false;
	bIsHiding = false;
	StopMovement();
	CurrentTask = FGCTeammateTask();
}

FString AGCAITeammateController::DescribeStatus() const
{
	if (bIsHiding)
	{
		return TEXT("hiding");
	}

	if (!bTaskActive)
	{
		return TaskQueue.Num() > 0 ? TEXT("starting next task") : TEXT("idle");
	}

	FString Status = CurrentTask.Description.IsEmpty() ? TEXT("working") : CurrentTask.Description;
	if (TaskQueue.Num() > 0)
	{
		Status += FString::Printf(TEXT(" (+%d queued)"), TaskQueue.Num());
	}

	return Status;
}

void AGCAITeammateController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Keep follow targets fresh: re-issue the move when the target strays.
	if (bTaskActive && CurrentTask.Type == EGCTeammateTaskType::FollowActor)
	{
		FollowRefreshCooldown -= DeltaSeconds;
		AActor* Target = CurrentTask.TargetActor.Get();
		if (Target && FollowRefreshCooldown <= 0.0f)
		{
			FollowRefreshCooldown = 0.5f;
			const float Distance = GetPawn() ? FVector::Dist(GetPawn()->GetActorLocation(), Target->GetActorLocation()) : 0.0f;
			if (Distance > 350.0f)
			{
				IssueMoveTo(FVector::ZeroVector, Target, 250.0f);
			}
		}
	}
}

void AGCAITeammateController::StartNextTask()
{
	if (TaskQueue.Num() == 0)
	{
		bTaskActive = false;
		CurrentTask = FGCTeammateTask();
		return;
	}

	CurrentTask = TaskQueue[0];
	TaskQueue.RemoveAt(0);
	bTaskActive = true;

	switch (CurrentTask.Type)
	{
	case EGCTeammateTaskType::MoveToLocation:
	case EGCTeammateTaskType::HideAt:
		IssueMoveTo(CurrentTask.Location, nullptr, 60.0f);
		break;

	case EGCTeammateTaskType::MoveToActor:
	case EGCTeammateTaskType::PickUpItem:
		if (AActor* Target = CurrentTask.TargetActor.Get())
		{
			IssueMoveTo(FVector::ZeroVector, Target, 120.0f);
		}
		else
		{
			CompleteCurrentTask();
		}
		break;

	case EGCTeammateTaskType::FollowActor:
		if (AActor* Target = CurrentTask.TargetActor.Get())
		{
			FollowRefreshCooldown = 0.0f;
			IssueMoveTo(FVector::ZeroVector, Target, 250.0f);
		}
		else
		{
			CompleteCurrentTask();
		}
		break;

	case EGCTeammateTaskType::Wait:
	default:
		CompleteCurrentTask();
		break;
	}
}

void AGCAITeammateController::IssueMoveTo(const FVector& Location, AActor* TargetActor, float AcceptanceRadius)
{
	FAIMoveRequest MoveRequest;
	if (TargetActor)
	{
		MoveRequest.SetGoalActor(TargetActor);
	}
	else
	{
		MoveRequest.SetGoalLocation(Location);
	}
	MoveRequest.SetAcceptanceRadius(AcceptanceRadius);
	MoveRequest.SetUsePathfinding(true);
	MoveRequest.SetAllowPartialPath(true);
	MoveRequest.SetProjectGoalLocation(true);

	FPathFollowingRequestResult Result = MoveTo(MoveRequest);
	if (Result.Code == EPathFollowingRequestResult::Failed)
	{
		// No navmesh in the map yet: fall back to straight-line movement so the
		// demo still works on a flat test level.
		MoveRequest.SetUsePathfinding(false);
		Result = MoveTo(MoveRequest);
	}

	if (Result.Code == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		CompleteCurrentTask();
	}
	else if (Result.Code == EPathFollowingRequestResult::Failed)
	{
		CompleteCurrentTask();
	}
}

void AGCAITeammateController::OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	Super::OnMoveCompleted(RequestID, Result);

	if (!bTaskActive)
	{
		return;
	}

	if (CurrentTask.Type == EGCTeammateTaskType::FollowActor)
	{
		// Following never self-completes; a new order clears it.
		return;
	}

	CompleteCurrentTask();
}

void AGCAITeammateController::CompleteCurrentTask()
{
	const FGCTeammateTask FinishedTask = CurrentTask;

	if (FinishedTask.Type == EGCTeammateTaskType::PickUpItem)
	{
		if (AGCPickupItem* Item = Cast<AGCPickupItem>(FinishedTask.TargetActor.Get()))
		{
			APawn* ControlledPawn = GetPawn();
			if (ControlledPawn && !Item->bTaken &&
				FVector::Dist2D(ControlledPawn->GetActorLocation(), Item->GetActorLocation()) < 250.0f)
			{
				if (Item->PickUpBy(ControlledPawn))
				{
					if (AGCAITeammateCharacter* Teammate = Cast<AGCAITeammateCharacter>(ControlledPawn))
					{
						Teammate->CarriedItems.Add(Item);
					}
				}
			}
		}
	}

	if (FinishedTask.Type == EGCTeammateTaskType::HideAt && TaskQueue.Num() == 0)
	{
		bIsHiding = true;
		if (ACharacter* ControlledCharacter = Cast<ACharacter>(GetPawn()))
		{
			ControlledCharacter->Crouch();
		}
	}
	else if (ACharacter* ControlledCharacter = Cast<ACharacter>(GetPawn()))
	{
		ControlledCharacter->UnCrouch();
	}

	StartNextTask();
}
