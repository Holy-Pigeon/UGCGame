#include "GCGameplayCommandSubsystem.h"

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GCAIHotReloadSubsystem.h"
#include "GCAITeammate.h"
#include "GCWorldActors.h"
#include "Kismet/GameplayStatics.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
struct FGCBuildingSpec
{
	const TCHAR* MeshPath;
	FVector Scale;
	FLinearColor Color;
};

const TMap<FString, FGCBuildingSpec>& GetBuildingCatalog()
{
	static const TMap<FString, FGCBuildingSpec> Catalog = {
		{TEXT("house"), {TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(6.0f, 6.0f, 4.0f), FLinearColor(0.65f, 0.45f, 0.25f)}},
		{TEXT("tower"), {TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FVector(3.0f, 3.0f, 12.0f), FLinearColor(0.5f, 0.5f, 0.6f)}},
		{TEXT("wall"), {TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(10.0f, 0.8f, 3.0f), FLinearColor(0.55f, 0.55f, 0.5f)}},
		{TEXT("shelter"), {TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(4.0f, 4.0f, 2.5f), FLinearColor(0.3f, 0.5f, 0.3f)}},
		{TEXT("platform"), {TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(8.0f, 8.0f, 0.5f), FLinearColor(0.4f, 0.4f, 0.45f)}},
	};
	return Catalog;
}

FString VectorToText(const FVector& Location)
{
	return FString::Printf(TEXT("(%.0f, %.0f, %.0f)"), Location.X, Location.Y, Location.Z);
}

TSharedPtr<FJsonObject> ParsePayload(const FString& PayloadJson)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PayloadJson);
	FJsonSerializer::Deserialize(Reader, Root);
	return Root;
}

FVector ReadVectorField(const TSharedPtr<FJsonObject>& Object, const FVector& Fallback)
{
	if (!Object.IsValid())
	{
		return Fallback;
	}

	FVector Result = Fallback;
	Object->TryGetNumberField(TEXT("x"), Result.X);
	Object->TryGetNumberField(TEXT("y"), Result.Y);
	Object->TryGetNumberField(TEXT("z"), Result.Z);
	return Result;
}
}

void UGCGameplayCommandSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (UGameInstance* GameInstance = InWorld.GetGameInstance())
	{
		if (UGCAIHotReloadSubsystem* HotReload = GameInstance->GetSubsystem<UGCAIHotReloadSubsystem>())
		{
			HotReload->OnGameplayCommand.AddDynamic(this, &UGCGameplayCommandSubsystem::HandleGameplayCommand);
			bBoundToHotReload = true;
		}
	}
}

void UGCGameplayCommandSubsystem::Deinitialize()
{
	if (bBoundToHotReload)
	{
		if (UWorld* World = GetWorld())
		{
			if (UGameInstance* GameInstance = World->GetGameInstance())
			{
				if (UGCAIHotReloadSubsystem* HotReload = GameInstance->GetSubsystem<UGCAIHotReloadSubsystem>())
				{
					HotReload->OnGameplayCommand.RemoveDynamic(this, &UGCGameplayCommandSubsystem::HandleGameplayCommand);
				}
			}
		}
		bBoundToHotReload = false;
	}

	Super::Deinitialize();
}

FString UGCGameplayCommandSubsystem::SpawnBuilding(const FString& BuildingType, FVector Location, const FString& Label, bool bSnapToGround)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return TEXT("Error: no world available.");
	}

	const FString NormalizedType = BuildingType.TrimStartAndEnd().ToLower();
	const FGCBuildingSpec* Spec = GetBuildingCatalog().Find(NormalizedType);
	if (!Spec)
	{
		TArray<FString> Types;
		GetBuildingCatalog().GetKeys(Types);
		return FString::Printf(TEXT("Error: unknown building type '%s'. Available types: %s"), *BuildingType, *FString::Join(Types, TEXT(", ")));
	}

	if (bSnapToGround)
	{
		Location = SnapToGround(Location);
	}

	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Spec->MeshPath);
	if (!Mesh)
	{
		return TEXT("Error: failed to load basic shape mesh.");
	}

	// Basic shapes are 100uu tall and centered; lift so the base sits on the ground.
	const float HalfHeight = 50.0f * Spec->Scale.Z;
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	AGCBuildingActor* Building = World->SpawnActor<AGCBuildingActor>(Location + FVector(0.0f, 0.0f, HalfHeight), FRotator::ZeroRotator, SpawnParameters);
	if (!Building)
	{
		return TEXT("Error: failed to spawn building actor.");
	}

	const FString EffectiveLabel = Label.TrimStartAndEnd().IsEmpty()
		? FString::Printf(TEXT("%s_%d"), *NormalizedType, FMath::RandRange(100, 999))
		: Label.TrimStartAndEnd();
	Building->InitBuilding(NormalizedType, EffectiveLabel, Mesh, Spec->Scale, Spec->Color);

	return FString::Printf(TEXT("Spawned building '%s' (type=%s) at %s."), *EffectiveLabel, *NormalizedType, *VectorToText(Building->GetActorLocation()));
}

FString UGCGameplayCommandSubsystem::SpawnPickup(const FString& ItemName, FVector Location, bool bSnapToGround)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return TEXT("Error: no world available.");
	}

	if (bSnapToGround)
	{
		Location = SnapToGround(Location) + FVector(0.0f, 0.0f, 30.0f);
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	AGCPickupItem* Pickup = World->SpawnActor<AGCPickupItem>(Location, FRotator::ZeroRotator, SpawnParameters);
	if (!Pickup)
	{
		return TEXT("Error: failed to spawn pickup actor.");
	}

	const FString EffectiveName = ItemName.TrimStartAndEnd().IsEmpty() ? TEXT("item") : ItemName.TrimStartAndEnd();
	Pickup->InitPickup(EffectiveName, FLinearColor(0.9f, 0.75f, 0.1f));

	return FString::Printf(TEXT("Spawned pickup '%s' at %s."), *EffectiveName, *VectorToText(Pickup->GetActorLocation()));
}

FString UGCGameplayCommandSubsystem::QueryWorld()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return TEXT("Error: no world available.");
	}

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();

	if (APawn* PlayerPawn = GetPlayerPawn())
	{
		const TSharedRef<FJsonObject> PlayerObject = MakeShared<FJsonObject>();
		const FVector PlayerLocation = PlayerPawn->GetActorLocation();
		PlayerObject->SetNumberField(TEXT("x"), PlayerLocation.X);
		PlayerObject->SetNumberField(TEXT("y"), PlayerLocation.Y);
		PlayerObject->SetNumberField(TEXT("z"), PlayerLocation.Z);
		Root->SetObjectField(TEXT("player"), PlayerObject);
	}

	auto LocationJson = [](const FVector& Location)
	{
		const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("x"), FMath::RoundToDouble(Location.X));
		Object->SetNumberField(TEXT("y"), FMath::RoundToDouble(Location.Y));
		Object->SetNumberField(TEXT("z"), FMath::RoundToDouble(Location.Z));
		return Object;
	};

	TArray<TSharedPtr<FJsonValue>> TeammateValues;
	for (TActorIterator<AGCAITeammateCharacter> It(World); It; ++It)
	{
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), It->TeammateName);
		Entry->SetObjectField(TEXT("location"), LocationJson(It->GetActorLocation()));
		if (const AGCAITeammateController* Controller = Cast<AGCAITeammateController>(It->GetController()))
		{
			Entry->SetStringField(TEXT("status"), Controller->DescribeStatus());
		}
		TArray<TSharedPtr<FJsonValue>> Carried;
		for (const AGCPickupItem* Item : It->CarriedItems)
		{
			if (Item)
			{
				Carried.Add(MakeShared<FJsonValueString>(Item->ItemName));
			}
		}
		Entry->SetArrayField(TEXT("carrying"), Carried);
		TeammateValues.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Root->SetArrayField(TEXT("teammates"), TeammateValues);

	TArray<TSharedPtr<FJsonValue>> PickupValues;
	for (TActorIterator<AGCPickupItem> It(World); It; ++It)
	{
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), It->ItemName);
		Entry->SetBoolField(TEXT("taken"), It->bTaken);
		Entry->SetObjectField(TEXT("location"), LocationJson(It->GetActorLocation()));
		PickupValues.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Root->SetArrayField(TEXT("pickups"), PickupValues);

	TArray<TSharedPtr<FJsonValue>> BuildingValues;
	for (TActorIterator<AGCBuildingActor> It(World); It; ++It)
	{
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("label"), It->Label);
		Entry->SetStringField(TEXT("type"), It->BuildingType);
		Entry->SetObjectField(TEXT("location"), LocationJson(It->GetActorLocation()));
		BuildingValues.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Root->SetArrayField(TEXT("buildings"), BuildingValues);

	TArray<TSharedPtr<FJsonValue>> HideSpotValues;
	for (TActorIterator<AGCHideSpot> It(World); It; ++It)
	{
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), It->SpotName);
		Entry->SetObjectField(TEXT("location"), LocationJson(It->GetActorLocation()));
		HideSpotValues.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Root->SetArrayField(TEXT("hide_spots"), HideSpotValues);

	FString Output;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}

FString UGCGameplayCommandSubsystem::CommandTeammate(const FString& TeammateName, const FString& Action, const FString& TargetName, FVector Location)
{
	FString Error;
	AGCAITeammateCharacter* Teammate = FindOrSpawnTeammate(TeammateName, Error);
	if (!Teammate)
	{
		return Error;
	}

	AGCAITeammateController* Controller = GetTeammateController(Teammate);
	if (!Controller)
	{
		return TEXT("Error: teammate has no AI controller.");
	}

	const FString NormalizedAction = Action.TrimStartAndEnd().ToLower();
	AActor* Target = TargetName.TrimStartAndEnd().IsEmpty() ? nullptr : FindNamedTarget(TargetName);

	if (NormalizedAction == TEXT("stop"))
	{
		Controller->ClearTasks();
		return FString::Printf(TEXT("Teammate '%s' stopped and cleared all tasks."), *Teammate->TeammateName);
	}

	if (NormalizedAction == TEXT("move_to"))
	{
		FGCTeammateTask Task;
		if (Target)
		{
			Task.Type = EGCTeammateTaskType::MoveToActor;
			Task.TargetActor = Target;
			Task.Description = FString::Printf(TEXT("moving to %s"), *TargetName);
		}
		else
		{
			Task.Type = EGCTeammateTaskType::MoveToLocation;
			Task.Location = SnapToGround(Location) + FVector(0.0f, 0.0f, 90.0f);
			Task.Description = FString::Printf(TEXT("moving to %s"), *VectorToText(Location));
		}
		Controller->EnqueueTask(Task);
		return FString::Printf(TEXT("Teammate '%s': %s."), *Teammate->TeammateName, *Task.Description);
	}

	if (NormalizedAction == TEXT("pick_up"))
	{
		AGCPickupItem* Pickup = Cast<AGCPickupItem>(Target);
		if (!Pickup)
		{
			// No explicit target: fetch the nearest untaken pickup.
			float BestDistance = TNumericLimits<float>::Max();
			for (TActorIterator<AGCPickupItem> It(GetWorld()); It; ++It)
			{
				if (It->bTaken)
				{
					continue;
				}
				const float Distance = FVector::Dist(Teammate->GetActorLocation(), It->GetActorLocation());
				if (Distance < BestDistance)
				{
					BestDistance = Distance;
					Pickup = *It;
				}
			}
		}

		if (!Pickup)
		{
			return TEXT("Error: no pickup found to fetch. Spawn one first or check query_world.");
		}
		if (Pickup->bTaken)
		{
			return FString::Printf(TEXT("Error: pickup '%s' was already taken."), *Pickup->ItemName);
		}

		FGCTeammateTask Task;
		Task.Type = EGCTeammateTaskType::PickUpItem;
		Task.TargetActor = Pickup;
		Task.Description = FString::Printf(TEXT("fetching %s"), *Pickup->ItemName);
		Controller->EnqueueTask(Task);
		return FString::Printf(TEXT("Teammate '%s' is going to fetch '%s' at %s."), *Teammate->TeammateName, *Pickup->ItemName, *VectorToText(Pickup->GetActorLocation()));
	}

	if (NormalizedAction == TEXT("hide"))
	{
		const FVector HideLocation = ComputeHideLocation(Target, Location);

		FGCTeammateTask Task;
		Task.Type = EGCTeammateTaskType::HideAt;
		Task.Location = HideLocation;
		Task.Description = Target
			? FString::Printf(TEXT("hiding behind %s"), *TargetName)
			: TEXT("finding a hiding place");
		Controller->EnqueueTask(Task);
		return FString::Printf(TEXT("Teammate '%s' is going to hide at %s."), *Teammate->TeammateName, *VectorToText(HideLocation));
	}

	if (NormalizedAction == TEXT("follow"))
	{
		AActor* FollowTarget = Target ? Target : Cast<AActor>(GetPlayerPawn());
		if (!FollowTarget)
		{
			return TEXT("Error: no follow target available.");
		}

		Controller->ClearTasks();
		FGCTeammateTask Task;
		Task.Type = EGCTeammateTaskType::FollowActor;
		Task.TargetActor = FollowTarget;
		Task.Description = FString::Printf(TEXT("following %s"), Target ? *TargetName : TEXT("player"));
		Controller->EnqueueTask(Task);
		return FString::Printf(TEXT("Teammate '%s' is now %s."), *Teammate->TeammateName, *Task.Description);
	}

	return FString::Printf(TEXT("Error: unknown action '%s'. Use move_to | pick_up | hide | follow | stop."), *Action);
}

FString UGCGameplayCommandSubsystem::EnsureTeammate(const FString& TeammateName)
{
	FString Error;
	AGCAITeammateCharacter* Teammate = FindOrSpawnTeammate(TeammateName, Error);
	if (!Teammate)
	{
		return Error;
	}

	return FString::Printf(TEXT("Teammate '%s' is ready at %s."), *Teammate->TeammateName, *VectorToText(Teammate->GetActorLocation()));
}

void UGCGameplayCommandSubsystem::HandleGameplayCommand(const FString& CommandName, const FString& PayloadJson)
{
	const TSharedPtr<FJsonObject> Payload = ParsePayload(PayloadJson);
	FString Result;

	if (CommandName == TEXT("ShowToast"))
	{
		FString Message = PayloadJson;
		if (Payload.IsValid())
		{
			Payload->TryGetStringField(TEXT("message"), Message);
		}
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Cyan, Message);
		}
		return;
	}

	if (CommandName == TEXT("spawn_building"))
	{
		FString BuildingType;
		FString Label;
		if (Payload.IsValid())
		{
			Payload->TryGetStringField(TEXT("type"), BuildingType);
			Payload->TryGetStringField(TEXT("label"), Label);
		}
		const FVector PlayerLocation = GetPlayerPawn() ? GetPlayerPawn()->GetActorLocation() : FVector::ZeroVector;
		const FVector Location = ReadVectorField(Payload, PlayerLocation + FVector(500.0f, 0.0f, 0.0f));
		Result = SpawnBuilding(BuildingType, Location, Label);
	}
	else if (CommandName == TEXT("spawn_pickup"))
	{
		FString ItemName;
		if (Payload.IsValid())
		{
			Payload->TryGetStringField(TEXT("name"), ItemName);
		}
		const FVector PlayerLocation = GetPlayerPawn() ? GetPlayerPawn()->GetActorLocation() : FVector::ZeroVector;
		const FVector Location = ReadVectorField(Payload, PlayerLocation + FVector(300.0f, 300.0f, 0.0f));
		Result = SpawnPickup(ItemName, Location);
	}
	else if (CommandName == TEXT("command_teammate"))
	{
		FString TeammateName;
		FString Action;
		FString TargetName;
		if (Payload.IsValid())
		{
			Payload->TryGetStringField(TEXT("teammate"), TeammateName);
			Payload->TryGetStringField(TEXT("action"), Action);
			Payload->TryGetStringField(TEXT("target"), TargetName);
		}
		const FVector Location = ReadVectorField(Payload, FVector::ZeroVector);
		Result = CommandTeammate(TeammateName, Action, TargetName, Location);
	}
	else
	{
		Result = FString::Printf(TEXT("Unhandled gameplay command '%s'"), *CommandName);
	}

	if (!Result.IsEmpty())
	{
		UE_LOG(LogTemp, Log, TEXT("[GameplayCommand] %s -> %s"), *CommandName, *Result);
		if (UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
		{
			if (UGCAIHotReloadSubsystem* HotReload = GameInstance->GetSubsystem<UGCAIHotReloadSubsystem>())
			{
				HotReload->EmitRuntimeLog(FString::Printf(TEXT("[%s] %s"), *CommandName, *Result));
			}
		}
	}
}

AGCAITeammateCharacter* UGCGameplayCommandSubsystem::FindTeammate(const FString& TeammateName) const
{
	const FString Trimmed = TeammateName.TrimStartAndEnd();
	AGCAITeammateCharacter* FirstTeammate = nullptr;
	for (TActorIterator<AGCAITeammateCharacter> It(GetWorld()); It; ++It)
	{
		if (!FirstTeammate)
		{
			FirstTeammate = *It;
		}
		if (!Trimmed.IsEmpty() && It->TeammateName.Equals(Trimmed, ESearchCase::IgnoreCase))
		{
			return *It;
		}
	}

	// No name given: any existing teammate will do.
	return Trimmed.IsEmpty() ? FirstTeammate : nullptr;
}

AGCAITeammateCharacter* UGCGameplayCommandSubsystem::FindOrSpawnTeammate(const FString& TeammateName, FString& OutError)
{
	if (AGCAITeammateCharacter* Existing = FindTeammate(TeammateName))
	{
		return Existing;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		OutError = TEXT("Error: no world available.");
		return nullptr;
	}

	APawn* PlayerPawn = GetPlayerPawn();
	const FVector BaseLocation = PlayerPawn ? PlayerPawn->GetActorLocation() : FVector(0.0f, 0.0f, 200.0f);
	const FVector SpawnLocation = SnapToGround(BaseLocation + FVector(200.0f, 200.0f, 0.0f)) + FVector(0.0f, 0.0f, 100.0f);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	AGCAITeammateCharacter* Teammate = World->SpawnActor<AGCAITeammateCharacter>(SpawnLocation, FRotator::ZeroRotator, SpawnParameters);
	if (!Teammate)
	{
		OutError = TEXT("Error: failed to spawn teammate.");
		return nullptr;
	}

	Teammate->TeammateName = TeammateName.TrimStartAndEnd().IsEmpty() ? TEXT("Buddy") : TeammateName.TrimStartAndEnd();
#if WITH_EDITOR
	Teammate->SetActorLabel(FString::Printf(TEXT("Teammate_%s"), *Teammate->TeammateName));
#endif
	return Teammate;
}

AGCAITeammateController* UGCGameplayCommandSubsystem::GetTeammateController(AGCAITeammateCharacter* Teammate) const
{
	return Teammate ? Cast<AGCAITeammateController>(Teammate->GetController()) : nullptr;
}

APawn* UGCGameplayCommandSubsystem::GetPlayerPawn() const
{
	return UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
}

AActor* UGCGameplayCommandSubsystem::FindNamedTarget(const FString& TargetName) const
{
	const FString Trimmed = TargetName.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		return nullptr;
	}

	if (Trimmed.Equals(TEXT("player"), ESearchCase::IgnoreCase))
	{
		return GetPlayerPawn();
	}

	for (TActorIterator<AGCPickupItem> It(GetWorld()); It; ++It)
	{
		if (It->ItemName.Equals(Trimmed, ESearchCase::IgnoreCase))
		{
			return *It;
		}
	}

	for (TActorIterator<AGCBuildingActor> It(GetWorld()); It; ++It)
	{
		if (It->Label.Equals(Trimmed, ESearchCase::IgnoreCase) || It->BuildingType.Equals(Trimmed, ESearchCase::IgnoreCase))
		{
			return *It;
		}
	}

	for (TActorIterator<AGCHideSpot> It(GetWorld()); It; ++It)
	{
		if (It->SpotName.Equals(Trimmed, ESearchCase::IgnoreCase))
		{
			return *It;
		}
	}

	for (TActorIterator<AGCAITeammateCharacter> It(GetWorld()); It; ++It)
	{
		if (It->TeammateName.Equals(Trimmed, ESearchCase::IgnoreCase))
		{
			return *It;
		}
	}

	return nullptr;
}

FVector UGCGameplayCommandSubsystem::SnapToGround(const FVector& Location, float ProbeHalfHeight) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return Location;
	}

	FHitResult Hit;
	const FVector Start = Location + FVector(0.0f, 0.0f, ProbeHalfHeight);
	const FVector End = Location - FVector(0.0f, 0.0f, ProbeHalfHeight);
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic))
	{
		return Hit.Location;
	}

	return Location;
}

FVector UGCGameplayCommandSubsystem::ComputeHideLocation(const AActor* HideTarget, const FVector& FallbackLocation) const
{
	const APawn* PlayerPawn = GetPlayerPawn();
	const FVector PlayerLocation = PlayerPawn ? PlayerPawn->GetActorLocation() : FVector::ZeroVector;

	// Explicit target: tuck in behind it, on the far side from the player.
	if (HideTarget)
	{
		const FVector TargetLocation = HideTarget->GetActorLocation();
		FVector AwayFromPlayer = TargetLocation - PlayerLocation;
		AwayFromPlayer.Z = 0.0f;
		AwayFromPlayer = AwayFromPlayer.IsNearlyZero() ? FVector(1.0f, 0.0f, 0.0f) : AwayFromPlayer.GetSafeNormal();

		float Clearance = 150.0f;
		if (const AGCBuildingActor* Building = Cast<AGCBuildingActor>(HideTarget))
		{
			Clearance += Building->GetFootprintRadius();
		}

		return SnapToGround(TargetLocation + AwayFromPlayer * Clearance) + FVector(0.0f, 0.0f, 90.0f);
	}

	// No target: nearest explicit hide spot, then any building, then just away from the player.
	if (!FallbackLocation.IsNearlyZero())
	{
		return SnapToGround(FallbackLocation) + FVector(0.0f, 0.0f, 90.0f);
	}

	const AActor* BestSpot = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<AGCHideSpot> It(GetWorld()); It; ++It)
	{
		const float Distance = FVector::Dist(PlayerLocation, It->GetActorLocation());
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			BestSpot = *It;
		}
	}

	if (!BestSpot)
	{
		for (TActorIterator<AGCBuildingActor> It(GetWorld()); It; ++It)
		{
			const float Distance = FVector::Dist(PlayerLocation, It->GetActorLocation());
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				BestSpot = *It;
			}
		}
	}

	if (BestSpot)
	{
		return ComputeHideLocation(BestSpot, FVector::ZeroVector);
	}

	const FVector RandomDirection = FVector(FMath::FRandRange(-1.0f, 1.0f), FMath::FRandRange(-1.0f, 1.0f), 0.0f).GetSafeNormal();
	return SnapToGround(PlayerLocation + RandomDirection * 1500.0f) + FVector(0.0f, 0.0f, 90.0f);
}
