#include "HIMarks.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "HIMaterials.h"
#include "HIUnits.h"

AHIMark::AHIMark()
{
	// Nothing ticks here. A mark is placed once and never moves.
	PrimaryActorTick.bCanEverTick = false;

	Quad = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Quad"));
	RootComponent = Quad;
	Quad->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Quad->SetGenerateOverlapEvents(false);
	Quad->SetMobility(EComponentMobility::Movable);
	// Marks are small, numerous and never cast anything a player would notice -- but a
	// hundred of them casting shadows across a room is a very visible cost for nothing.
	Quad->SetCastShadow(false);
	Quad->SetVisibility(false);

	if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		Quad->SetStaticMesh(Mesh);
	}
}

void AHIMark::Stamp(const FVector& Where, const FVector& Normal, float SizeMeters,
	const FLinearColor& Colour)
{
	if (Quad->GetStaticMesh() == nullptr)
	{
		return;
	}

	// The cube is one metre at unit scale, so a millimetre-thick slab is 0.001 across its
	// depth and SizeMeters across the face. Lying flat in the plane the normal implies means
	// turning the slab's own Z onto the normal.
	const float Thickness = 0.004f;
	SetActorScale3D(FVector(SizeMeters, SizeMeters, Thickness));

	const FVector Facing = Normal.GetSafeNormal();
	SetActorRotation(Facing.IsNearlyZero() ? FRotator::ZeroRotator : Facing.Rotation());
	// Sunk slightly INTO the surface along the normal rather than floating above it, so the
	// slab's own thickness cannot poke through the wall it is supposed to be lying on.
	SetActorLocation(Where - Facing * Thickness * 0.5f);

	if (!Tinted.Equals(Colour, 0.001f))
	{
		FHIMaterials::Tint(Quad, Colour);
		Tinted = Colour;
	}
	SetActorHiddenInGame(false);
}

void AHIMark::StampLine(const FVector& From, const FVector& To, float ThicknessMeters,
	const FLinearColor& Colour)
{
	if (Quad->GetStaticMesh() == nullptr)
	{
		return;
	}
	const FVector Delta = To - From;
	const float Length = Delta.Size();
	if (Length < 1.0f)
	{
		// Under a centimetre: there is nothing to draw a line along, and normalising this
		// would produce a NaN rotation that Unreal propagates silently into the transform.
		return;
	}

	// The cube's own X is its length, so the scale carries the span and the rotation aims
	// it. Scaled in METRES because the engine's cube is one metre at unit scale.
	SetActorScale3D(FVector(Length / HI_METERS_TO_UU, ThicknessMeters, ThicknessMeters));
	SetActorRotation(Delta.Rotation());
	SetActorLocation(From + Delta * 0.5f);

	if (!Tinted.Equals(Colour, 0.001f))
	{
		FHIMaterials::Tint(Quad, Colour);
		Tinted = Colour;
	}
	SetActorHiddenInGame(false);
}
