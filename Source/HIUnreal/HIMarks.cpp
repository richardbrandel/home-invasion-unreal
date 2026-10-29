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
	// NOT `SetVisibility(false)` here (2026-10-19): that flips the component's bVisible
	// flag, which Stamp/StampLine never re-set -- they show with SetActorHiddenInGame(false),
	// a DIFFERENT flag (the actor's bHiddenInGame). UE's IsVisible() is
	// `!bHiddenInGame && bVisible`, so a constructor-time SetVisibility(false) left every
	// bullet hole, blood slab, tracer and muzzle flash invisible for the life of the port.
	// The mark pool owns hiding (SetActorHiddenInGame(true) at spawn); the component keeps
	// its default bVisible=true, and the first Stamp shows it.

	// A cylinder, not a cube (2026-10-19): a bullet hole is ROUND. The cube produced a
	// square slab; the engine's cylinder is 1 m across, so the same Stamp scale yields a
	// disc of that diameter. A tracer becomes a round rod instead of a square one, and a
	// blood pool reads as a pool instead of a tile.
	CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	// A PLANE, for the textured hole. The disc was right for a tracer rod and wrong for a bullet
	// hole: a solid disc of one colour is a perfect circle (Richard, 2026-10-28), while a real
	// hole is a ragged dark patch with a rim. The Godot project stamped
	// /Game/HIImported/textures/bullet_hole on a 0.16 m quad -- that texture has been imported
	// here since the asset pass and nothing ever sampled it, because no material existed to
	// sample it with. M_HIMark is that material (tools/make-assets.py, make_mark).
	PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (CylinderMesh != nullptr)
	{
		Quad->SetStaticMesh(CylinderMesh);
	}
}

FRotator AHIMark::SlabRotation(const FVector& Normal)
{
	if (Normal.IsNearlyZero())
	{
		return FRotator::ZeroRotator;
	}
	// MakeFromZ, NOT `Normal.Rotation()`.
	//
	// The slab is THIN IN Z and SizeMeters across X and Y. `FVector::Rotation()` builds a
	// rotation whose local **X** points along the vector, so using it here turned the 10 cm
	// face onto the wall normal and left the slab standing out of the wall as a blade -- and
	// turned the 0.9 m corpse mark into a 0.9 m red sheet standing vertically on the floor,
	// which is the "unidentified two-tone object" this project had recorded and could not
	// name. `MakeFromZ` is the rotation whose Z is the normal, which is what the geometry and
	// the comment both always wanted.
	//
	// Pure and static so the orientation is asserted rather than looked at: a still frame of a
	// wall with holes in it did not reveal this for two sessions.
	return FRotationMatrix::MakeFromZ(Normal.GetSafeNormal()).Rotator();
}

void AHIMark::Stamp(const FVector& Where, const FVector& Normal, float SizeMeters,
	const FLinearColor& Colour)
{
	if (Quad->GetStaticMesh() == nullptr)
	{
		return;
	}

	// THE HOLE IS A TEXTURED QUAD NOW. Swap the mesh to a plane and the material to M_HIMark if
	// this slab was last used as a tracer rod; a mark pool reuses actors for both.
	const float Thickness = 0.004f;
	if (!bUsingDecal && PlaneMesh != nullptr)
	{
		Quad->SetStaticMesh(PlaneMesh);
		bUsingDecal = true;
		Tinted = FLinearColor(-1.0f, -1.0f, -1.0f, -1.0f);   // force the tint to re-apply
	}
	if (DecalMat == nullptr && PlaneMesh != nullptr)
	{
		if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(
				nullptr, TEXT("/Game/HIGenerated/M_HIMark.M_HIMark")))
		{
			DecalMat = UMaterialInstanceDynamic::Create(Base, this);
			Quad->SetMaterial(0, DecalMat);
		}
	}
	// A plane has no thickness to sink, so it sits a centimetre PROUD of the surface: far
	// enough out to beat the depth buffer, close enough not to read as a floating card.
	SetActorScale3D(FVector(SizeMeters, SizeMeters, 1.0f));

	const FVector Facing = Normal.GetSafeNormal();
	SetActorRotation(SlabRotation(Facing));
	SetActorLocation(Where + Facing * 0.01f);

	if (!Tinted.Equals(Colour, 0.001f))
	{
		if (DecalMat != nullptr)
		{
			DecalMat->SetVectorParameterValue(TEXT("Tint"), Colour);
		}
		else
		{
			FHIMaterials::Tint(Quad, Colour);
		}
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

	// A TRACER IS A ROD, NOT A CARD: switch back from the decal plane if the pool left this
	// slab as a bullet hole. Without this a tracer would be a one-metre flat sheet standing
	// along the shot line.
	if (bUsingDecal && CylinderMesh != nullptr)
	{
		Quad->SetStaticMesh(CylinderMesh);
		bUsingDecal = false;
		Tinted = FLinearColor(-1.0f, -1.0f, -1.0f, -1.0f);
	}
	// The cylinder's own Z is its length, so the span goes on Z and the rotation points that
	// Z along the segment; X/Y carry the (now round) cross-section. Scaled in METRES because
	// the engine's cylinder is one metre at unit scale.
	SetActorScale3D(FVector(ThicknessMeters, ThicknessMeters, Length / HI_METERS_TO_UU));
	SetActorRotation(FRotationMatrix::MakeFromZ(Delta.GetSafeNormal()).Rotator());
	SetActorLocation(From + Delta * 0.5f);

	if (!Tinted.Equals(Colour, 0.001f))
	{
		FHIMaterials::Tint(Quad, Colour);
		Tinted = Colour;
	}
	SetActorHiddenInGame(false);
}
