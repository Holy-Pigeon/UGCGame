#include "GCWorldActors.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
const FName GCBuildingTag(TEXT("GCBuilding"));
const FName GCPickupTag(TEXT("GCPickup"));
const FName GCHideSpotTag(TEXT("GCHideSpot"));

UMaterialInstanceDynamic* ApplyBasicShapeColor(UStaticMeshComponent* MeshComponent, const FLinearColor& Color)
{
	UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (!BaseMaterial || !MeshComponent)
	{
		return nullptr;
	}

	UMaterialInstanceDynamic* Dynamic = UMaterialInstanceDynamic::Create(BaseMaterial, MeshComponent);
	Dynamic->SetVectorParameterValue(TEXT("Color"), Color);
	MeshComponent->SetMaterial(0, Dynamic);
	return Dynamic;
}
}

AGCBuildingActor::AGCBuildingActor()
{
	PrimaryActorTick.bCanEverTick = false;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(MeshComponent);
	MeshComponent->SetMobility(EComponentMobility::Movable);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComponent->SetCollisionProfileName(TEXT("BlockAll"));

	Tags.Add(GCBuildingTag);
}

void AGCBuildingActor::InitBuilding(const FString& InBuildingType, const FString& InLabel, UStaticMesh* Mesh, const FVector& Scale, const FLinearColor& Color)
{
	BuildingType = InBuildingType;
	Label = InLabel;

	if (Mesh)
	{
		MeshComponent->SetStaticMesh(Mesh);
	}
	MeshComponent->SetWorldScale3D(Scale);
	ApplyBasicShapeColor(MeshComponent, Color);

#if WITH_EDITOR
	SetActorLabel(Label);
#endif
}

float AGCBuildingActor::GetFootprintRadius() const
{
	FVector Origin;
	FVector Extent;
	GetActorBounds(/*bOnlyCollidingComponents*/ false, Origin, Extent);
	return FMath::Max(Extent.X, Extent.Y);
}

AGCPickupItem::AGCPickupItem()
{
	PrimaryActorTick.bCanEverTick = false;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(MeshComponent);
	MeshComponent->SetMobility(EComponentMobility::Movable);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	MeshComponent->SetCollisionResponseToAllChannels(ECR_Overlap);
	MeshComponent->SetWorldScale3D(FVector(0.35f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		MeshComponent->SetStaticMesh(SphereMesh.Object);
	}

	Tags.Add(GCPickupTag);
}

void AGCPickupItem::InitPickup(const FString& InItemName, const FLinearColor& Color)
{
	ItemName = InItemName;
	ApplyBasicShapeColor(MeshComponent, Color);

#if WITH_EDITOR
	SetActorLabel(FString::Printf(TEXT("Pickup_%s"), *ItemName));
#endif
}

bool AGCPickupItem::PickUpBy(AActor* Collector)
{
	if (bTaken || !Collector)
	{
		return false;
	}

	bTaken = true;
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AttachToActor(Collector, FAttachmentTransformRules::KeepWorldTransform);
	SetActorRelativeLocation(FVector(0.0f, 0.0f, 120.0f));
	SetActorRelativeRotation(FRotator::ZeroRotator);
	return true;
}

AGCHideSpot::AGCHideSpot()
{
	PrimaryActorTick.bCanEverTick = false;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(MeshComponent);
	MeshComponent->SetMobility(EComponentMobility::Movable);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetWorldScale3D(FVector(0.4f, 0.4f, 0.8f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));
	if (ConeMesh.Succeeded())
	{
		MeshComponent->SetStaticMesh(ConeMesh.Object);
	}

	Tags.Add(GCHideSpotTag);
}
