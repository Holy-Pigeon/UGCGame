#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GCWorldActors.generated.h"

class UStaticMeshComponent;

// A building placed by the agent. Built from engine basic shapes so no game
// content is required; buildings double as cover for the teammate's hide task.
UCLASS(BlueprintType)
class AGCBuildingActor : public AActor
{
	GENERATED_BODY()

public:
	AGCBuildingActor();

	void InitBuilding(const FString& InBuildingType, const FString& InLabel, UStaticMesh* Mesh, const FVector& Scale, const FLinearColor& Color);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UGC|World")
	FString BuildingType;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UGC|World")
	FString Label;

	// Approximate world-space bounds radius, used to pick hide positions behind the building.
	float GetFootprintRadius() const;

private:
	UPROPERTY(VisibleAnywhere, Category = "UGC|World")
	TObjectPtr<UStaticMeshComponent> MeshComponent;
};

// A pickup the teammate can be sent to fetch.
UCLASS(BlueprintType)
class AGCPickupItem : public AActor
{
	GENERATED_BODY()

public:
	AGCPickupItem();

	void InitPickup(const FString& InItemName, const FLinearColor& Color);
	bool PickUpBy(AActor* Collector);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UGC|World")
	FString ItemName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UGC|World")
	bool bTaken = false;

private:
	UPROPERTY(VisibleAnywhere, Category = "UGC|World")
	TObjectPtr<UStaticMeshComponent> MeshComponent;
};

// An explicit hide location. Buildings also work as implicit hide spots.
UCLASS(BlueprintType)
class AGCHideSpot : public AActor
{
	GENERATED_BODY()

public:
	AGCHideSpot();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UGC|World")
	FString SpotName;

private:
	UPROPERTY(VisibleAnywhere, Category = "UGC|World")
	TObjectPtr<UStaticMeshComponent> MeshComponent;
};
