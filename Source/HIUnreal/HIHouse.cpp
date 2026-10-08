#include "HIHouse.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "HIUnits.h"
#include "UObject/ConstructorHelpers.h"

AHIHouse::AHIHouse()
{
	PrimaryActorTick.bCanEverTick = false;

	WallInstances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Walls"));
	RootComponent = WallInstances;
	FurnitureInstances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Furniture"));
	FurnitureInstances->SetupAttachment(WallInstances);

	GroundInstance = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Ground"));
	GroundInstance->SetupAttachment(WallInstances);
	// No collision at all. See the header: the sim places every body itself, and a solid the
	// sim does not know about is exactly the drift this actor exists to prevent.
	GroundInstance->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GroundInstance->SetMobility(EComponentMobility::Static);

	// The engine's own one-metre cube, so the project needs no assets and the scale maths
	// stays honest: at unit scale a cube is 1 m, so a measurement in metres IS the scale.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		WallInstances->SetStaticMesh(CubeFinder.Object);
		FurnitureInstances->SetStaticMesh(CubeFinder.Object);
		GroundInstance->SetStaticMesh(CubeFinder.Object);
	}

	// Walls block sight and pellets -- the trace is cast on Visibility, so a wall has to be
	// able to stop it. They never need to move, so they are static.
	WallInstances->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	WallInstances->SetCollisionResponseToAllChannels(ECR_Ignore);
	WallInstances->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	WallInstances->SetMobility(EComponentMobility::Static);

	// Furniture blocks sight too, and that is not decoration: FHISim::Solids() includes
	// non-flat furniture precisely because the AI cannot see through it. If the geometry did
	// not match, the player could shoot through a sofa the intruder cannot see past.
	FurnitureInstances->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	FurnitureInstances->SetCollisionResponseToAllChannels(ECR_Ignore);
	FurnitureInstances->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	FurnitureInstances->SetMobility(EComponentMobility::Static);
}

FTransform AHIHouse::BoxTransform(const FBox2D& Rect, float MinZ, float MaxZ)
{
	// FBox2D::GetSize() is an FVector2D, because a sim rectangle lives in the plane. The
	// third component is the span, built here rather than converted.
	const FVector2D Size = Rect.GetSize();
	const float Height = FMath::Max(MaxZ - MinZ, 0.01f);
	const FVector Centre(
		(Rect.Min.X + Rect.Max.X) * 0.5f,
		(Rect.Min.Y + Rect.Max.Y) * 0.5f,
		(MinZ + MaxZ) * 0.5f);

	return FTransform(
		FRotator::ZeroRotator,
		Centre * HI_METERS_TO_UU,
		FVector(Size.X, Size.Y, Height));
}

void AHIHouse::Build()
{
	if (WallInstances == nullptr || FurnitureInstances == nullptr)
	{
		return;
	}
	WallInstances->ClearInstances();
	FurnitureInstances->ClearInstances();

	// Solids() is walls followed by non-flat furniture, and SolidSpans() is index-aligned
	// with it -- so the split is by count rather than by inspecting each rectangle, which
	// is exactly why no rectangle can end up in the wrong bucket.
	const TArray<FBox2D>& Solids = FHISim::Solids();
	const TArray<FHISim::FHISolidSpan>& Spans = FHISim::SolidSpans();
	const int32 WallCount = FHISim::Walls().Num();

	// If the two ever fall out of step, index-aligned access is out of bounds. Better to
	// build nothing than to build walls at the wrong heights.
	if (Spans.Num() != Solids.Num())
	{
		return;
	}

	for (int32 Idx = 0; Idx < Solids.Num(); ++Idx)
	{
		const FTransform Box = BoxTransform(Solids[Idx], Spans[Idx].MinZ, Spans[Idx].MaxZ);
		if (Idx < WallCount)
		{
			WallInstances->AddInstance(Box, /*bWorldSpace=*/false);
		}
		else
		{
			FurnitureInstances->AddInstance(Box, /*bWorldSpace=*/false);
		}
	}

	// A slab just under everything, sized to the sim's own world bounds so it reaches exactly
	// as far as the player is allowed to walk and no further.
	if (GroundInstance != nullptr)
	{
		GroundInstance->ClearInstances();
		const FBox2D& Bounds = FHISim::Bounds;
		const FVector2D Size = Bounds.GetSize();
		const FVector Centre(
			(Bounds.Min.X + Bounds.Max.X) * 0.5f,
			(Bounds.Min.Y + Bounds.Max.Y) * 0.5f,
			-0.25f);
		GroundInstance->AddInstance(FTransform(
			FRotator::ZeroRotator,
			Centre * HI_METERS_TO_UU,
			FVector(Size.X, Size.Y, 0.5f)), /*bWorldSpace=*/false);
	}
}
