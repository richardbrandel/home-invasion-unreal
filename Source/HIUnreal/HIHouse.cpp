#include "HIHouse.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "HIMaterials.h"
#include "HIUnits.h"
#include "UObject/ConstructorHelpers.h"

// (A file-local `AddInstance` helper lived here until 2026-10-23. It took metres and built
// the transform with them directly, which Unreal reads as centimetres, and it also turned
// collision on for the whole component every call. Its last two callers were the broken
// hedge blocks, now rewritten against `AddBox`, so it is gone rather than left as a trap:
// with -Werror an unused static is a build break, and a second way to add a box is how this
// project got a hedge at the wrong scale in the first place.)

namespace
{
	/** The four ways a decoration is always the same, in one place. */
	struct FDecorDefaults
	{
		static void Apply(UInstancedStaticMeshComponent* Comp, UStaticMesh* Cube)
		{
			if (Cube != nullptr)
			{
				Comp->SetStaticMesh(Cube);
			}
			// NO COLLISION. This is the file's governing invariant -- see the header. It is
			// applied here once rather than at eight call sites, because a ninth component
			// added later by someone who has not read the header would otherwise be the one
			// that breaks the rule and lets a picture frame stop a bullet.
			Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Comp->SetGenerateOverlapEvents(false);
			Comp->SetMobility(EComponentMobility::Static);
		}
	};
}

const TArray<AHIHouse::FFitting>& AHIHouse::WallFittings()
{
	// Sockets, switches and a thermostat, in metres: x, y, z, sizeX, sizeY, sizeZ.
	// PHASE 2: rewritten for the three-floor house; the slice-era coordinates pointed at
	// walls that no longer exist (the old house was 16 m wide, this one is 12.7).
	//
	// EVERY POSITION IS FLUSH AGAINST A WALL FACE, and the arithmetic is written out so it
	// can be checked rather than trusted. The faces this table uses, from `WallRows()`:
	//
	//   north exterior  band y -17.39..-17.14   interior face y = -17.14 (room side: y >)
	//   south exterior  band y  -8.00.. -7.75   interior face y =  -8.00 (room side: y <)
	//   west exterior   band x  -6.35.. -6.10   interior face x =  -6.10 (room side: x >)
	//   east exterior   band x   6.10..  6.35   interior face x =   6.10 (room side: x <)
	//   x -1.2 partition  band -1.26..-1.14     faces x = -1.26 (west) / -1.14 (east)
	//   x  1.83 partition band  1.77.. 1.89     faces x =  1.77 (west) /  1.89 (east)
	//   x  2.00 partition (floor 1) 1.94..2.06  faces x =  1.94 (west) /  2.06 (east)
	//   x  3.00 partition (floor -1) 2.94..3.06 face x =   2.94 (west)
	//
	// A fitting's centre is the face plus half its own depth toward the room, so its back
	// lands ON the wall. Sizes X and Y swap on the side walls, because the wall is thin in
	// the other axis there. Heights are floor-relative: ground sockets at 0.30, switches at
	// 1.25 (second floor: +2.74; basement: -2.44). `HI.Unreal.WallFittings` keeps them
	// honest -- the slice learned the hard way that a fitting "by the front door" sitting
	// INSIDE the door gap hangs in mid-air, visible from the spawn as a floating pale box.
	static const TArray<FFitting> F = {
		// ---- ground floor: sockets, just above the floor (or above a counter)
		{ -6.070f, -10.40f, 0.30f, 0.06f, 0.09f, 0.13f },   // living room, west wall, behind the sofa
		{ -5.600f, -17.11f, 0.30f, 0.09f, 0.06f, 0.13f },   // master, north wall, behind the bed head
		{  2.900f, -17.11f, 1.15f, 0.09f, 0.06f, 0.13f },   // kitchen, north wall above the counter
		{  6.070f, -10.50f, 0.30f, 0.06f, 0.09f, 0.13f },   // dining, east wall, north of the sideboard
		// ---- ground floor: switches, at hand height, each beside the door it serves and
		// each VERIFIED ON A WALL SEGMENT (the y -9.70 switch is on segment y -8..-10 of
		// the x -1.2 partition; -15.10 is on segment y -14.8..-17.14; -12.10 is on segment
		// y -10.4..-12.2 of the x 1.83 partition -- the segment edge moved 2026-10-18 when
		// the hall<->kitchen opening was widened, and -12.10 stayed on the wall).
		{ -1.850f,  -8.03f, 1.25f, 0.09f, 0.06f, 0.09f },   // by the front door, west of the gap and of the coat
		{ -1.110f,  -9.70f, 1.25f, 0.06f, 0.09f, 0.09f },   // living room, hall side of the opening
		{ -1.290f, -15.10f, 1.25f, 0.06f, 0.09f, 0.09f },   // master, beside its door
		{  1.740f, -12.10f, 1.25f, 0.06f, 0.09f, 0.09f },   // hall, beside the kitchen opening
		// ---- ground floor: thermostat, hall side of the x 1.83 partition, segment b
		{  1.745f, -10.80f, 1.50f, 0.05f, 0.14f, 0.14f },
		// ---- second floor (faces are the same rows; heights are +2.74)
		{ -5.500f,  -8.03f, 3.04f, 0.09f, 0.06f, 0.13f },   // bedroom 2, south wall, west of its window
		{ -5.900f, -17.11f, 3.04f, 0.09f, 0.06f, 0.13f },   // bedroom 3, north wall, west of its window frame
		{ -1.290f,  -9.70f, 3.99f, 0.06f, 0.09f, 0.09f },   // bedroom 2, beside its door
		{  2.090f, -12.40f, 3.99f, 0.06f, 0.09f, 0.09f },   // office, beside its opening
		// ---- basement (floor -2.44)
		{  2.910f, -15.00f, -2.14f, 0.06f, 0.09f, 0.13f },  // mechanical-room wall, open-area side
		{ -6.070f, -16.80f, -1.74f, 0.06f, 0.09f, 0.13f },  // over the workbench, west wall
	};
	return F;
}

AHIHouse::AHIHouse()
{
	PrimaryActorTick.bCanEverTick = false;

	// The engine's own one-metre cube, so the project needs no assets and the scale maths
	// stays honest: at unit scale a cube is 1 m, so a measurement in metres IS the scale.
	UStaticMesh* Cube = nullptr;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		Cube = CubeFinder.Object;
	}

	// ---- the two components that COLLIDE, and nothing else in this file does
	WallInstances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Walls"));
	RootComponent = WallInstances;
	if (Cube != nullptr)
	{
		WallInstances->SetStaticMesh(Cube);
	}
	// Walls block sight and pellets -- the trace is cast on Visibility, so a wall has to be
	// able to stop it. They never need to move, so they are static.
	WallInstances->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	WallInstances->SetCollisionResponseToAllChannels(ECR_Ignore);
	WallInstances->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	// A CORPSE IS A PHYSICS BODY, and nothing else in this game is. Walls have to stop one for
	// the same reason they have to stop a bullet -- otherwise a man shot next to a wall falls
	// through it and lands outside the house.
	WallInstances->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
	WallInstances->SetMobility(EComponentMobility::Static);

	FurnitureInstances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Furniture"));
	FurnitureInstances->SetupAttachment(WallInstances);
	if (Cube != nullptr)
	{
		FurnitureInstances->SetStaticMesh(Cube);
	}
	// Furniture blocks sight too, and that is not decoration: FHISim::Solids() includes
	// non-flat furniture precisely because the AI cannot see through it. If the geometry did
	// not match, the player could shoot through a sofa the intruder cannot see past.
	FurnitureInstances->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	FurnitureInstances->SetCollisionResponseToAllChannels(ECR_Ignore);
	FurnitureInstances->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	FurnitureInstances->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
	FurnitureInstances->SetMobility(EComponentMobility::Static);

	// ---- the fork's slice: three more COLLIDING components, set up exactly like the
	// walls and furniture above. The slab separates the storeys for bullets, bodies and
	// sightlines; the treads are the visible staircase; the railings guard the hole's
	// edge. One component per material, as everywhere else in this class.
	auto MakeSolid = [this, Cube](const TCHAR* Name)
	{
		UInstancedStaticMeshComponent* Comp = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		Comp->SetupAttachment(WallInstances);
		if (Cube != nullptr)
		{
			Comp->SetStaticMesh(Cube);
		}
		Comp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Comp->SetCollisionResponseToAllChannels(ECR_Ignore);
		Comp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Comp->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
		Comp->SetMobility(EComponentMobility::Static);
		return Comp;
	};
	SlabInstances = MakeSolid(TEXT("Slabs"));
	StairInstances = MakeSolid(TEXT("Stairs"));
	RailingInstances = MakeSolid(TEXT("Railings"));
	// Collision-only boxes for the MODELED furniture: colliding but never drawn, so the mesh
	// is the only thing the player sees.
	FurnitureCollisionInstances = MakeSolid(TEXT("FurnitureCollision"));
	FurnitureCollisionInstances->SetHiddenInGame(true);
	// Phase 2: the below-grade walls collide exactly like the walls above them; only the
	// material differs (cinderblock, per Richard's brief), and a material is a
	// component-level property -- hence a second component, not a second opinion.
	WallBasementInstances = MakeSolid(TEXT("BasementWalls"));

	// Phase-2 realism: the composed furniture's colliding bodies, one component per material.
	FurnitureSoftInstances = MakeSolid(TEXT("FurnitureSoft"));
	FurnitureApplianceInstances = MakeSolid(TEXT("FurnitureAppliance"));
	FurnitureDarkInstances = MakeSolid(TEXT("FurnitureDark"));
	// ...and their non-colliding detail parts. Decor, so a cushion never stops a bullet.
	FurnitureSoftDecorInstances = MakeDecor(TEXT("FurnitureSoftDecor"), Cube, WallInstances);
	FurnitureApplianceDecorInstances = MakeDecor(TEXT("FurnitureApplianceDecor"), Cube, WallInstances);
	FurnitureDarkDecorInstances = MakeDecor(TEXT("FurnitureDarkDecor"), Cube, WallInstances);
	FurnitureWoodDecorInstances = MakeDecor(TEXT("FurnitureWoodDecor"), Cube, WallInstances);
	CountertopInstances = MakeDecor(TEXT("Countertop"), Cube, WallInstances);
	StreetInstances = MakeDecor(TEXT("Street"), Cube, WallInstances);
	NeighborInstances = MakeDecor(TEXT("Neighbors"), Cube, WallInstances);
	TreeInstances = MakeDecor(TEXT("Trees"), Cube, WallInstances);
	BushInstances = MakeDecor(TEXT("Bushes"), Cube, WallInstances);
	// The leaf card is the engine PLANE: 1 m square, normal +Z, so a card is one transform.
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	LeafInstances = MakeDecor(TEXT("Leaves"), Plane != nullptr ? Plane : Cube, WallInstances);
	if (LeafInstances != nullptr)
	{
		// Foliage cards do not cast. A masked card a few centimetres from its own opaque core
		// throws a hard leaf-shaped shadow onto it, which in a frame reads as a black blotch on
		// the hedge rather than as depth -- measured, 2026-10-29.
		LeafInstances->SetCastShadow(false);
	}
	// Round wheels: the engine Cylinder (not the cube), so a wheel is a disc, never a block.
	{
		UStaticMesh* Cylinder = nullptr;
		static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(
			TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		if (CylinderFinder.Succeeded())
		{
			Cylinder = CylinderFinder.Object;
		}
		WheelInstances = MakeDecor(TEXT("Wheels"), Cylinder, WallInstances);
		WheelRimInstances = MakeDecor(TEXT("WheelRims"), Cylinder, WallInstances);
	}

	// ---- everything below is decoration, and none of it collides. See the header.
	GroundInstance = MakeDecor(TEXT("Ground"), Cube, WallInstances);
	FloorInstance = MakeDecor(TEXT("InteriorFloor"), Cube, WallInstances);

	// ---- THE FLOOR A CORPSE LANDS ON, and this is NOT an exception to the invariant above.
	//
	// The invariant is that decoration must never stop a BULLET. A ragdoll is not a bullet: it
	// is a physics body, and `ECC_PhysicsBody` is a channel nothing else in this project uses.
	// So the response is set for that channel ONLY -- every other channel, Visibility included,
	// still passes straight through, and a shot at the floor behaves exactly as it did.
	//
	// Without this the corpse falls through the lawn for ever, and that is measured rather than
	// feared: component Z went 0 -> -1330 cm in 1.5 s and was still accelerating, because a
	// component with collision disabled does not exist as far as the physics scene is
	// concerned. `AHIIntruder::GoLimp` is the other half of this.
	auto BlockCorpses = [](UInstancedStaticMeshComponent* Comp)
	{
		Comp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Comp->SetCollisionResponseToAllChannels(ECR_Ignore);
		Comp->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
	};
	BlockCorpses(GroundInstance);
	BlockCorpses(FloorInstance);
	// Phase 2: the basement slab, corpse-catcher for the storey below grade -- the lawn
	// that catches everything else is 2.44 m above a body that falls down there.
	BasementFloorInstances = MakeDecor(TEXT("BasementFloor"), Cube, WallInstances);
	BlockCorpses(BasementFloorInstances);
	RoofInstance = MakeDecor(TEXT("Roof"), Cube, WallInstances);
	// The roof is the one decorative component that must still cast a shadow: it exists to
	// keep the sun out of the rooms, and a roof that did not shadow would be a roof that did
	// nothing at all.
	RoofInstance->SetCastShadow(true);
	CeilingInstance = MakeDecor(TEXT("Ceiling"), Cube, WallInstances);
	SidingInstances = MakeDecor(TEXT("Siding"), Cube, WallInstances);
	TrimInstances = MakeDecor(TEXT("Trim"), Cube, WallInstances);
	WindowInstances = MakeDecor(TEXT("Windows"), Cube, WallInstances);
	GlassInstances = MakeDecor(TEXT("Glass"), Cube, WallInstances);
	CoatInstances = MakeDecor(TEXT("Coat"), Cube, WallInstances);
	FittingInstances = MakeDecor(TEXT("Fittings"), Cube, WallInstances);
	PropInstances = MakeDecor(TEXT("Props"), Cube, WallInstances);
	DoorHeadInstances = MakeDecor(TEXT("DoorHead"), Cube, WallInstances);
	VanInstances = MakeDecor(TEXT("Van"), Cube, WallInstances);
	VanDetailInstances = MakeDecor(TEXT("VanDetail"), Cube, WallInstances);
	GTOInstances = MakeDecor(TEXT("GTO"), Cube, WallInstances);
}

UInstancedStaticMeshComponent* AHIHouse::MakeDecor(const TCHAR* Name, UStaticMesh* Cube,
	USceneComponent* Parent)
{
	UInstancedStaticMeshComponent* Comp = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
	FDecorDefaults::Apply(Comp, Cube);
	Comp->SetupAttachment(Parent);
	return Comp;
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

void AHIHouse::AddBox(UInstancedStaticMeshComponent* Target, const FVector& Centre,
	const FVector& SizeMeters, float YawDegrees)
{
	if (Target == nullptr)
	{
		return;
	}
	Target->AddInstance(FTransform(
		FRotator(0.0f, YawDegrees, 0.0f),
		Centre * HI_METERS_TO_UU,
		SizeMeters), /*bWorldSpace=*/false);
}

void AHIHouse::AddBoxR(UInstancedStaticMeshComponent* Target, const FVector& Centre,
	const FVector& SizeMeters, const FRotator& Rot)
{
	// The pitched roof needs a rotation AddBox cannot express: yaw spins a box in the
	// ground plane, and a gable plane is pitched out of it. Roll about X tilts in the YZ
	// plane, which is exactly the plane a north-south roof slope lives in.
	if (Target == nullptr)
	{
		return;
	}
	Target->AddInstance(FTransform(Rot, Centre * HI_METERS_TO_UU, SizeMeters),
		/*bWorldSpace=*/false);
}

void AHIHouse::AddWindow(bool bAlongX, float Along, float WallCoord, float Facing, float Width,
	float FloorZ, float SillUp, float HeadUp)
{
	// A window is a hole with a frame round it and something in the hole.
	//
	// The glazing WAS a pale opaque panel, because the port's notes said a translucent
	// material is a .uasset and a .uasset needs an editor session. That is not true any more
	// -- see `tools/make-assets.py` -- so the pane is now real glass from
	// `/Game/HIGenerated/M_HIGlass` in its own component, and the frame, mullion and sill
	// stay opaque trim. The original's own note ("the glazing is still a pale opaque panel
	// from outside") applied to Godot; this closes it here.
	//
	// Phase 2: `FloorZ` lifts the whole assembly onto an upper storey (the exterior walls
	// are single rows spanning 0..RoofTop, so a second-floor window is the same call with
	// FloorZ = SecondZ), and SillUp/HeadUp are heights above that floor -- the small high
	// bathroom windows of HOUSE-DESIGN.md §4.6 pass 1.40/2.00.
	const float Sill = FloorZ + SillUp;
	const float Head = FloorZ + HeadUp;
	const float MidZ = (Sill + Head) * 0.5f;
	const float Height = Head - Sill;
	// Proud of the wall face by 15 cm, on the outward side, so the frame reads as a frame
	// rather than being buried inside the wall it is cut into.
	const float Out = WallCoord + Facing * 0.15f;
	const float FrameT = 0.06f;

	auto Place = [&](UInstancedStaticMeshComponent* Target, float AlongOffset, float HeightOffset,
		float SizeAlong, float SizeZ)
	{
		const FVector Centre = bAlongX
			? FVector(Along + AlongOffset, Out, MidZ + HeightOffset)
			: FVector(Out, Along + AlongOffset, MidZ + HeightOffset);
		const FVector Size = bAlongX
			? FVector(SizeAlong, 0.08f, SizeZ)
			: FVector(0.08f, SizeAlong, SizeZ);
		AddBox(Target, Centre, Size);
	};

	// The glazing goes to its OWN component, so that making it translucent cannot drag the
	// frame, the mullion and the sill along with it -- they share nothing but a position.
	Place(GlassInstances, 0.0f, 0.0f, Width, Height);
	// Then the four sides of the frame around it.
	Place(WindowInstances, 0.0f, (Height * 0.5f) + (FrameT * 0.5f), Width + FrameT * 2.0f, FrameT);
	Place(WindowInstances, 0.0f, -(Height * 0.5f) - (FrameT * 0.5f), Width + FrameT * 2.0f, FrameT);
	Place(WindowInstances, -(Width * 0.5f) - (FrameT * 0.5f), 0.0f, FrameT, Height);
	Place(WindowInstances, (Width * 0.5f) + (FrameT * 0.5f), 0.0f, FrameT, Height);
	// A central mullion, because one big pane reads as a hole and two reads as a window.
	Place(WindowInstances, 0.0f, 0.0f, FrameT * 0.8f, Height);
	// No sill: the white ledge at counter height read as a white box on the counter
	// (2026-10-21). The frame + frosted glass read as a window on their own.
}

void AHIHouse::BuildDressing()
{
	const FBox2D H = FHISim::House;

	// ---- the lawn, the basement slab, and the ground floor's wood.
	{
		// The lawn is DELIBERATELY FAR LARGER than the sim's play area. A 60 x 52 m plane
		// ends in mid-air 30 m from the house, and the first render of this port showed a
		// black band along the horizon where the ground stopped and the sky atmosphere had
		// nothing to draw below itself. Size here is pure scenery: the sim still clamps every
		// body to `FHISim::Bounds`, and this component blocks corpses only, so nothing about
		// where a player may walk changed.
		//
		// PHASE 2: the lawn can no longer be ONE box. The house has a basement now, and a
		// solid 5 km slab from -0.5 to 0 would fill the top of it -- so the lawn is four
		// strips around the footprint, and the footprint's own ground level is the deck the
		// sim already carries ([BasementCeiling..0] in WorldSolids, drawn as structure in
		// Build()). The strips share the top face at 0.0 exactly.
		const float LawnHalf = 2600.0f;
		const float LZ = -0.27f;   // top face at -0.02, clear of the street's +0.04 (see BuildNeighborhood)
		const float LT = 0.5f;
		const float HCY = (H.Min.Y + H.Max.Y) * 0.5f;
		const float HDY = H.Max.Y - H.Min.Y;
		AddBox(GroundInstance, FVector(0.0f, (-LawnHalf + H.Min.Y) * 0.5f, LZ),
			FVector(LawnHalf * 2.0f, H.Min.Y + LawnHalf, LT));                  // north strip
		AddBox(GroundInstance, FVector(0.0f, (LawnHalf + H.Max.Y) * 0.5f, LZ),
			FVector(LawnHalf * 2.0f, LawnHalf - H.Max.Y, LT));                  // south strip
		AddBox(GroundInstance, FVector((-LawnHalf + H.Min.X) * 0.5f, HCY, LZ),
			FVector(H.Min.X + LawnHalf, HDY, LT));                              // west strip
		AddBox(GroundInstance, FVector((LawnHalf + H.Max.X) * 0.5f, HCY, LZ),
			FVector(LawnHalf - H.Max.X, HDY, LT));                              // east strip

		const FVector2D HSize = H.GetSize();
		const float HCX = (H.Min.X + H.Max.X) * 0.5f;

		// The basement slab, top face 2 cm proud of BasementZ -- the same proud convention
		// as the floors above, for the same z-fighting reason. Corpses land on it; bullets
		// pass it, exactly like the lawn.
		AddBox(BasementFloorInstances,
			FVector(HCX, HCY, FHISim::BasementZ + 0.02f - 0.20f),
			FVector(HSize.X, HSize.Y, 0.4f));

		// The ground floor's wood: thin panels laid over the sim's ground deck rectangles,
		// so the stairwell hole exists by construction and the top face sits 2 cm proud of
		// both the deck and the lawn -- under a shoe, over a depth epsilon.
		for (const FHISim::FHISolid& S : FHISim::WorldSolids())
		{
			if (S.bDeck && FMath::IsNearlyEqual(S.Span.MaxZ, FHISim::FloorHeight(0), 0.01f))
			{
				AddBox(FloorInstance,
					FVector((S.Rect.Min.X + S.Rect.Max.X) * 0.5f,
						(S.Rect.Min.Y + S.Rect.Max.Y) * 0.5f, 0.01f),
					FVector(S.Rect.GetSize().X, S.Rect.GetSize().Y, 0.02f));
			}
		}
	}

	// ---- THE GABLE ROOF (§4.6) and the three ceilings under it.
	//
	// The slice's flat lids retired with the slice: phase 2's house is a realistic
	// two-storey, and §4.6 pins the roof -- ridge east-west at y = -12.57, ~1.9 m rise.
	// -12.57 is exactly the footprint's y-centre ((-17.39 + -7.75)/2), so the two slopes
	// are symmetric and their run is 4.82 each; the numbers below are computed from the
	// shell rect, not typed in twice.
	//
	// The pitched planes are DECOR, and that is deliberate: the sim's roof deck
	// [RoofBottom, RoofTop] is drawn as structure in Build() and is what stops a bullet.
	// A pellet at a steep angle can still pass the shingles above 5.33 -- the sim has no
	// roof PLANE, and inventing colliding geometry the sim does not know about is exactly
	// the invariant violation this file refuses to commit (see the header).
	{
		const float CXm = (H.Min.X + H.Max.X) * 0.5f;
		const float RidgeY = (H.Min.Y + H.Max.Y) * 0.5f;   // -12.57, the doc's number
		const float Rise = 1.9f;
		const float Run = H.Max.Y - RidgeY;                 // 4.82
		const float Slope = FMath::Sqrt(Run * Run + Rise * Rise);
		const float Over = 0.45f;                           // eave overhang, ALONG the slope
		const float PitchDeg = FMath::RadiansToDegrees(FMath::Atan2(Rise, Run));   // 21.5

		// Each slope runs from the ridge down past its eave: the overhang extends the
		// plane 0.45 along the slope, which is 0.419 out and 0.165 down at this pitch.
		const float RidgeZ = FHISim::RoofTop + Rise;        // 7.23
		const float EaveZ = FHISim::RoofTop - Over * Rise / Slope;
		const float SouthEaveY = H.Max.Y + Over * Run / Slope;
		const float NorthEaveY = H.Min.Y - Over * Run / Slope;
		const float SlabLen = FMath::Sqrt(
			FMath::Square(RidgeY - SouthEaveY) + FMath::Square(RidgeZ - EaveZ));   // 5.63
		const float SlabZ = (RidgeZ + EaveZ) * 0.5f;
		const FVector SlabSize(H.GetSize().X + 2.0f * Over, SlabLen, 0.12f);
		// Roll about X tilts in the YZ plane -- the plane a north-south roof slope lives
		// in. The south slab's +Y end is its low eave (roll POSITIVE -- the 2026-10-19
		// neighbour roofs came out as a V, which proved the sign was inverted).
		AddBoxR(RoofInstance, FVector(0.0f, (SouthEaveY + RidgeY) * 0.5f, SlabZ),
			SlabSize, FRotator(0.0f, 0.0f, PitchDeg));
		AddBoxR(RoofInstance, FVector(0.0f, (NorthEaveY + RidgeY) * 0.5f, SlabZ),
			SlabSize, FRotator(0.0f, 0.0f, -PitchDeg));
		// A ridge cap over the joint, so the two planes meet in a spine, not a seam.
		AddBox(RoofInstance, FVector(0.0f, RidgeY, RidgeZ + 0.07f),
			FVector(H.GetSize().X + 2.0f * Over + 0.10f, 0.35f, 0.14f));
		// The gable ends: the wall rows stop at RoofTop, and the triangle between that
		// and the ridge is closed with eight stepped boxes per end (a real gable is a
		// triangle; eight steps read as one from the lawn, which is where you see it).
		for (float SX : { -1.0f, 1.0f })
		{
			for (int32 K = 0; K < 8; ++K)
			{
				const float ZC = FHISim::RoofTop + (K + 0.5f) * Rise / 8.0f;
				const float HalfDepth = Run * (1.0f - (K + 0.5f) / 8.0f);
				AddBox(RoofInstance,
					FVector(SX * (H.Max.X - FHISim::WallThickness * 0.5f), RidgeY, ZC),
					FVector(FHISim::WallThickness, HalfDepth * 2.0f, Rise / 8.0f + 0.02f));
			}
		}

		// The ceilings: one panel under EVERY deck the sim carries, taken from the sim's
		// own list so the stairwell holes exist by construction. The ground deck's
		// underside -0.15 is the basement's ceiling; the slab's 2.59 is the ground
		// floor's; the roof deck's 5.18 is the second floor's. The slice drew these for
		// the slab only, because the slice had one ceiling.
		for (const FHISim::FHISolid& S : FHISim::WorldSolids())
		{
			if (!S.bDeck)
			{
				continue;
			}
			AddBox(CeilingInstance,
				FVector((S.Rect.Min.X + S.Rect.Max.X) * 0.5f,
					(S.Rect.Min.Y + S.Rect.Max.Y) * 0.5f,
					S.Span.MinZ - 0.03f),
				FVector(S.Rect.GetSize().X, S.Rect.GetSize().Y, 0.06f));
		}
	}

	// ---- the front door SURROUND.
	//
	// History: the sim is a 2-D model, so `DoorGap` splits the south wall horizontally and
	// the opening ran from the floor to the CEILING -- a letterbox in the front of the
	// house, which Godot filled with a decorative panel and the slice followed. Phase 2
	// made the fill REAL instead (an ADDITION, veto-able): the facade now runs to 5.33, so
	// the letterbox became a 3.2 m invisible slot, and this file's own invariant forbids
	// decoration that lies about geometry -- a drawn wall a pellet flies through. The wall
	// above the door head is now a sim row with MinZ 2.10 (see WallRows); what is left
	// here is what a real door has anyway: jambs and a head trim, proud of the faces.
	{
		const float HeadBottom = 2.10f;                       // the original's DOOR_TOP
		const float WallCY = H.Max.Y - FHISim::WallThickness * 0.5f;
		const float JamDepth = FHISim::WallThickness + 0.06f; // proud 3 cm each face
		for (float Edge : { FHISim::DoorGap.X, FHISim::DoorGap.Y })
		{
			const float Outward = Edge < 0.0f ? -1.0f : 1.0f;
			AddBox(DoorHeadInstances,
				FVector(Edge + Outward * 0.05f, WallCY, HeadBottom * 0.5f),
				FVector(0.10f, JamDepth, HeadBottom));
		}
		AddBox(DoorHeadInstances,
			FVector((FHISim::DoorGap.X + FHISim::DoorGap.Y) * 0.5f, WallCY, HeadBottom + 0.05f),
			FVector(FHISim::DoorGap.Y - FHISim::DoorGap.X + 0.20f, JamDepth, 0.10f));
	}

	// ---- the porch light's FIXTURE (2026-10-23): the lamp itself is spawned by
	// `AHIGameMode::SetupLighting` at (1.45, -7.62, 2.10), just outside the front door, and a
	// lamp with nothing on the wall reads as a bulb hanging in mid-air. Dark metal body with a
	// pale diffuser under it, so the glow has a source.
	AddBox(VanDetailInstances,
		FVector(1.45f, H.Max.Y + 0.10f, 2.10f), FVector(0.16f, 0.20f, 0.26f));
	AddBox(TrimInstances,
		FVector(1.45f, H.Max.Y + 0.21f, 2.02f), FVector(0.12f, 0.04f, 0.16f));

	// ---- the house's OUTSIDE SKIN (2026-10-23): board-and-batten panels on the four
	// facades, in their own component so the interior keeps its painted plaster. This is
	// what turns the front view from a mottled concrete shed into a house -- measured in a
	// front frame, the exterior walls wore `ceiling_plaster` at 40 tiling and read as render.
	//
	// Equal-width panels, because every cube face maps 0..1 and `M_HITextured` has one
	// tiling scalar: a 12.7 m facade and a 2 m one would otherwise draw different board
	// widths side by side. The front ground floor is split around the door gap, which is a
	// real hole in the wall; the windows are panels ON the wall, so they need no split and
	// their frames stand proud of this skin.
	{
		const float PanelWidth = 1.9f;
		const float Thick = 0.04f;
		const float Out = Thick * 0.5f + 0.005f;   // 5 mm clear of the wall face: no z-fight
		const float Z0 = FHISim::FloorHeight(0);
		const float Z1 = FHISim::FloorHeight(1);
		const float Z2 = FHISim::RoofTop;

		auto PanelRun = [&](bool bAlongX, float Face, float From, float To, float MinZ, float MaxZ)
		{
			const float Len = To - From;
			if (Len <= 0.05f || MaxZ - MinZ <= 0.05f)
			{
				return;
			}
			const int32 Panels = FMath::Max(1, FMath::RoundToInt(Len / PanelWidth));
			const float Each = Len / Panels;
			for (int32 i = 0; i < Panels; ++i)
			{
				const float C = From + (i + 0.5f) * Each;
				const float Zc = (MinZ + MaxZ) * 0.5f;
				const FVector Centre = bAlongX ? FVector(C, Face, Zc) : FVector(Face, C, Zc);
				const FVector Size = bAlongX
					? FVector(Each - 0.02f, Thick, MaxZ - MinZ)
					: FVector(Thick, Each - 0.02f, MaxZ - MinZ);
				AddBox(SidingInstances, Centre, Size);
			}
		};

		// south (front): ground floor split around the front door, then the storey above
		PanelRun(true, H.Max.Y + Out, H.Min.X, FHISim::DoorGap.X, Z0, Z1);
		PanelRun(true, H.Max.Y + Out, FHISim::DoorGap.Y, H.Max.X, Z0, Z1);
		PanelRun(true, H.Max.Y + Out, H.Min.X, H.Max.X, Z1, Z2);
		// north (back)
		PanelRun(true, H.Min.Y - Out, H.Min.X, H.Max.X, Z0, Z1);
		PanelRun(true, H.Min.Y - Out, H.Min.X, H.Max.X, Z1, Z2);
		// west and east
		PanelRun(false, H.Min.X - Out, H.Min.Y, H.Max.Y, Z0, Z1);
		PanelRun(false, H.Min.X - Out, H.Min.Y, H.Max.Y, Z1, Z2);
		PanelRun(false, H.Max.X + Out, H.Min.Y, H.Max.Y, Z0, Z1);
		PanelRun(false, H.Max.X + Out, H.Min.Y, H.Max.Y, Z1, Z2);
	}

	// ---- skirting, DERIVED from the wall rows rather than hand-placed, per storey: a band
	// at each row's floor. Interior partitions only -- the exterior rows (TopZ = RoofTop, and
	// the door transom) get no trim, because skirting on the outside of a house is a plinth
	// and the transom floats 2.1 m up in the air.
	//
	// THE PICTURE RAIL AT 2.06 m IS GONE (2026-10-23). It was a second box over the row's
	// whole plan footprint at head height, and in a first-person frame it read as a bright
	// white STRIPE PAINTED ACROSS the wall -- in the living room it ran straight across the
	// doorway opening it was supposed to terminate at, because a wall ROW spans the row's
	// rect and the opening is not a row. Picture rail is a Victorian detail on a modern
	// house, and the skirting is the piece a real room actually has; removing the rail is
	// cheaper and more honest than giving it mitred ends at every opening.
	for (const FHISim::FHIWallRow& Row : FHISim::WallRows())
	{
		if (Row.TopZ > 0.0f)
		{
			continue;
		}
		// NO SKIRTING BELOW GRADE (2026-10-28). The loop applied interior trim to every storey,
		// so the basement's cinderblock runs each carried a bright white 12 cm board at floor
		// level -- the brightest thing in a room meant to be bare block. Painted skirting is a
		// finished-room detail; exposed block gets a concrete plinth or nothing.
		if (Row.Floor < 0)
		{
			continue;
		}
		const FVector2D Size = Row.Rect.GetSize();
		const float Base = Row.MinZ > 0.0f ? Row.MinZ : FHISim::FloorHeight(Row.Floor);
		const float Girth = 0.035f;
		AddBox(TrimInstances,
			FVector((Row.Rect.Min.X + Row.Rect.Max.X) * 0.5f,
				(Row.Rect.Min.Y + Row.Rect.Max.Y) * 0.5f, Base + 0.06f),
			FVector(Size.X + Girth * 2.0f, Size.Y + Girth * 2.0f, 0.12f));
	}

	// ---- windows, the HOUSE-DESIGN.md §4.6 schedule. Ground floor first, then the
	// second at FloorZ = SecondZ, then the two small high bathroom windows (0.6 m wide,
	// sill at 1.40). The basement gets none: it is below grade. Every `along` coordinate
	// was checked at authoring time against the furniture under it -- a window with a
	// wardrobe in front of it is a wall with a picture of a window on it.
	{
		const float Z2 = FHISim::SecondZ;
		// ground
		AddWindow(true, -3.60f, H.Max.Y, 1.0f, 2.20f);                       // living-room picture, south
		AddWindow(false, -15.40f, H.Min.X, -1.0f, 1.60f);                    // master, west (over the bed head)
		AddWindow(true, -5.10f, H.Min.Y, -1.0f, 1.40f);                      // master, north
		AddWindow(true, -2.60f, H.Min.Y, -1.0f, 0.60f, 0.0f, 1.40f, 2.00f);  // master bath, high
		AddWindow(true, 3.45f, H.Min.Y, -1.0f, 1.60f);                       // kitchen, north (over the counter)
		AddWindow(false, -15.20f, H.Max.X, 1.0f, 1.40f);                     // kitchen, east
		AddWindow(true, 3.80f, H.Max.Y, 1.0f, 1.50f);                        // dining, south
		AddWindow(false, -10.60f, H.Max.X, 1.0f, 1.40f);                     // dining, east
		// second
		AddWindow(true, -2.60f, H.Max.Y, 1.0f, 1.60f, Z2);                   // bedroom 2, south
		AddWindow(true, -4.90f, H.Min.Y, -1.0f, 1.40f, Z2);                  // bedroom 3, north
		AddWindow(true, 4.30f, H.Max.Y, 1.0f, 1.50f, Z2);                    // office, south (over the desk)
		AddWindow(true, 4.60f, H.Min.Y, -1.0f, 0.60f, Z2, 1.40f, 2.00f);     // bath 2, high
	}

	// ---- fix-list item 8: the things that are on a wall in a real house.
	//
	// The positions live in `WallFittings()` rather than here, because they were WRONG and
	// only a test can keep them right: measured on a capture, the switch "by the front door"
	// sat at x = -0.60, which is inside `DoorGap` (x -1..1), so it hung in the open doorway
	// with no wall behind it -- visible from the spawn as a small pale box floating over the
	// lawn. Two sockets and four switches were likewise up to 1.4 m off any wall face.
	// `HI.Unreal.WallFittings` now asserts every one of them is within a centimetre of a wall.
	for (const FFitting& D : WallFittings())
	{
		AddBox(FittingInstances, FVector(D.X, D.Y, D.Z), FVector(D.SX, D.SY, D.SZ));
	}
	// A smoke alarm on the hall ceiling -- in the hall, which is where a smoke alarm goes.
	// Phase 2: the ground floor's ceiling is the slab's underside; the panel hangs 6 cm
	// under it (top at 2.59, underside at 2.53), so the alarm's centre is 2.50 and its top
	// bites 5 mm into the panel instead of floating under it.
	AddBox(FittingInstances, FVector(0.40f, -14.50f, 2.50f), FVector(0.16f, 0.16f, 0.07f));

	// Light fittings: a shallow rose under the ceiling at each lamp, so the lamp in the game
	// mode is a light that is visibly COMING FROM something. THESE COORDINATES ARE THE LAMP
	// COORDINATES: `LampSpecs` in HIGameMode.cpp is the other half of this table, and a rose
	// with no lamp over it is a light fitting that does not light, which reads as broken.
	// Phase 2 has three ceiling heights, one per storey: the basement's at -0.21 (under the
	// ground deck), the ground floor's at 2.53 (under the slab), the second floor's at 5.12
	// (under the roof deck). A rose centre sits 2 cm under its ceiling so the disc reads.
	static const FVector Roses[] =
	{
		// basement (ceiling underside -0.21)
		FVector(-3.50f, -12.50f, -0.23f),   // open area
		FVector( 4.60f, -15.20f, -0.23f),   // mechanical room
		// ground (underside 2.53)
		FVector(-3.60f, -10.00f, 2.51f),    // living room
		FVector(-4.40f, -14.20f, 2.51f),    // master bedroom
		FVector(-2.60f, -16.00f, 2.51f),    // master bath
		FVector( 3.60f, -16.00f, 2.51f),    // kitchen
		FVector( 3.90f, -10.90f, 2.51f),    // dining
		FVector( 0.30f,  -9.30f, 2.51f),    // entry hall
		FVector( 0.65f, -14.25f, 2.51f),    // hall, between the two stairwell walls
		// second (underside 5.12)
		FVector(-4.20f, -10.40f, 5.10f),    // bedroom 2
		FVector(-4.20f, -15.40f, 5.10f),    // bedroom 3
		FVector( 4.80f, -16.00f, 5.10f),    // bath 2
		FVector( 4.00f, -10.70f, 5.10f),    // office
		FVector( 0.45f, -13.50f, 5.10f),    // upstairs hall
	};
	for (const FVector& R : Roses)
	{
		// A FIXTURE, NOT A PANEL (2026-10-27). This was one 0.30 m white slab, and in every
		// interior frame it read as a bright rectangle stuck to the ceiling -- measured in the
		// kitchen: (241,238,232) with a hard edge and nothing around it. A flush ceiling light
		// is a housing with a diffuser inset in it, so: a dark plate against the ceiling, and a
		// slightly smaller white diffuser hanging 3.5 cm below it. From underneath that shows a
		// white panel with a dark rim, which is what the eye reads as a light fitting.
		AddBox(VanDetailInstances, R, FVector(0.34f, 0.34f, 0.03f));      // housing, dark metal
		AddBox(FittingInstances, FVector(R.X, R.Y, R.Z - 0.035f),
			FVector(0.26f, 0.26f, 0.05f));                                 // diffuser, inset
	}

	// ---- fix-list items 10 and 11: the small things a house actually contains, on phase-2
	// furniture. Every z is the counter's own row height plus half the prop, so the kettle's
	// bottom lands ON the 0.92 counter top -- not inside it, not above it.
	// SMALL APPLIANCES WITH A SILHOUETTE, NOT WHITE CUBES (2026-10-28). These were four plain
	// boxes, and in the kitchen frame the kettle and toaster read as two featureless pale cubes on
	// the counter -- the "white boxes" that were reported more than once. Each is now its body plus
	// the one or two pieces that say what it is, all still sitting ON the 0.92 counter top.
	{
		// kettle: body, domed lid, spout toward -Y, and a handle on +Y
		AddBox(PropInstances, FVector(2.90f, -16.82f, 1.04f), FVector(0.20f, 0.20f, 0.24f));
		AddBox(PropInstances, FVector(2.90f, -16.82f, 1.17f), FVector(0.14f, 0.14f, 0.03f));
		AddBox(PropInstances, FVector(2.90f, -16.94f, 1.10f), FVector(0.05f, 0.06f, 0.06f));
		AddBox(VanDetailInstances, FVector(2.90f, -16.70f, 1.10f), FVector(0.03f, 0.03f, 0.13f));
		// toaster: body with two dark slots in the top face
		AddBox(PropInstances, FVector(3.45f, -16.82f, 1.00f), FVector(0.26f, 0.16f, 0.15f));
		for (float DX : { -0.06f, 0.06f })
		{
			AddBox(VanDetailInstances, FVector(3.45f + DX, -16.82f, 1.075f),
				FVector(0.07f, 0.10f, 0.01f));
		}
		// bread bin: body and a lid that overhangs
		AddBox(PropInstances, FVector(4.05f, -16.82f, 0.97f), FVector(0.28f, 0.22f, 0.10f));
		AddBox(PropInstances, FVector(4.05f, -16.82f, 1.025f), FVector(0.30f, 0.24f, 0.02f));
		// fruit bowl: a shallow open dish, so a thin rim rather than a solid block
		AddBox(PropInstances, FVector(3.70f, -14.95f, 0.955f), FVector(0.28f, 0.28f, 0.05f));
		AddBox(VanDetailInstances, FVector(3.70f, -14.95f, 0.975f), FVector(0.20f, 0.20f, 0.02f));
	}
	// THE COAT IS ON THE WALL BESIDE THE DOOR, and it used to be IN the doorway -- `DoorGap`
	// is x -1..1 and the slice's coat at -0.80 spanned the whole opening, a pale slab hanging
	// in the one hole in the house. Measured on a capture. Phase 2 keeps the lesson and the
	// wall moves: the south face is now y -8.00, so the coat hangs at x -1.5, clear of the
	// gap AND of the door switch at -1.85.
	AddBox(CoatInstances, FVector(-1.50f, -8.05f, 1.75f), FVector(0.10f, 0.10f, 0.10f));  // coat hook
	AddBox(CoatInstances, FVector(-1.50f, -8.11f, 1.28f), FVector(0.44f, 0.22f, 0.86f));  // coat
	AddBox(PropInstances, FVector(-4.70f, -10.70f, 0.44f), FVector(0.26f, 0.20f, 0.04f)); // keys on the coffee table
	AddBox(PropInstances, FVector(-4.45f, -10.85f, 0.43f), FVector(0.09f, 0.16f, 0.02f)); // phone
	// A FRAMED PICTURE, NOT A SLAB (2026-10-27). This was one 0.05 m dark box on the wall and in
	// the living-room frame it read as a grey panel hovering against the wall -- nothing about it
	// said "picture". A white frame plate against the wall with the canvas standing a little
	// proud of it gives the border the eye needs, for the cost of one instance.
	AddBox(TrimInstances, FVector(-6.085f, -10.40f, 1.60f), FVector(0.04f, 1.00f, 0.78f)); // frame
	AddBox(PropInstances, FVector(-6.055f, -10.40f, 1.60f), FVector(0.03f, 0.86f, 0.64f)); // canvas
	AddBox(PropInstances, FVector(3.75f, -8.40f, 3.52f), FVector(0.50f, 0.36f, 0.06f));   // desk mat, office
	AddBox(PropInstances, FVector(4.90f, -8.35f, 3.54f), FVector(0.09f, 0.09f, 0.10f));   // a mug-ish shape

	// ---- the van: a real Sketchfab model (CC BY, tools/fetch-sketchfab.py), scaled to the
	// sim's van rect so the thing the intruders deliver to is still the thing the player
	// sees. The model is Y-up with its length on Z; FRotator(0,90,90) puts length on X,
	// width on Y and up on Z. It carries its own body, cab, glass and wheels.
	{
		const FBox2D& V = FHISim::Van;
		PlaceSketchfabModel(TEXT("van"), V.GetCenter(), V.GetSize().X, V.GetSize().Y,
			0.0f, 2.5f, FRotator::ZeroRotator);
	}
}

void AHIHouse::PlaceSketchfabModel(const FString& Name, const FVector2D& Center,
	float WidthM, float DepthM, float Base, float Height, const FRotator& Rot, bool bCollide,
	bool bPerAxis, const TCHAR* OpaqueTexture, const FLinearColor& TintColour)
{
	// Every StaticMesh under /Game/HIImported/models/<Name>, placed at one shared transform
	// (2026-10-19). The glTF importer bakes node transforms, so the sub-meshes -- a van's
	// body, doors, wheels and glass, or a tree's hundreds of leaf planes -- are already in
	// the model's own space and one transform assembles the whole thing.
	FAssetRegistryModule& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& Reg = AR.Get();
	// In -game the registry starts empty; force a synchronous scan so the imported meshes
	// are indexed before we ask for them. In-editor this is a cheap no-op.
	Reg.SearchAllAssets(true);
	TArray<FAssetData> Assets;
	Reg.GetAssetsByPath(
		FName(*FString::Printf(TEXT("/Game/HIImported/models/%s"), *Name)),
		Assets, /*bRecursive=*/true);

	TArray<UStaticMesh*> Meshes;
	FBox Combined(ForceInit);
	for (const FAssetData& A : Assets)
	{
		if (A.GetClass() != UStaticMesh::StaticClass())
		{
			continue;
		}
		if (UStaticMesh* M = Cast<UStaticMesh>(A.GetAsset()))
		{
			Meshes.Add(M);
			Combined += M->GetBounds().GetBox();
		}
	}
	if (Meshes.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("HI: no meshes for Sketchfab model %s -- run tools/fetch-sketchfab.py"), *Name);
		return;
	}

	const FVector Full = Combined.GetExtent() * 2.0f;   // full model size, in UU
	// Two scales: uniform fits the longest axis to WidthM (vehicles/trees, whose proportions
	// are right); per-axis forces X/Y/Z onto WidthM/DepthM/Height (furniture, where the
	// imports arrived with wildly non-uniform unit scales and need normalising to the sim's
	// collision rect).
	const FVector Scale = bPerAxis
		? FVector(WidthM * HI_METERS_TO_UU / Full.X,
			DepthM * HI_METERS_TO_UU / Full.Y,
			Height * HI_METERS_TO_UU / Full.Z)
		: [&]()
		{
			const float Longest = FMath::Max3(Full.X, Full.Y, Full.Z);
			const float S = Longest > 1.0f ? WidthM * HI_METERS_TO_UU / Longest : 0.0f;
			return FVector(S, S, S);
		}();
	const FVector C = Combined.GetCenter();
	const FVector Target(Center.X, Center.Y, Base + Height * 0.5f);
	const FVector Loc = Target * HI_METERS_TO_UU - Rot.RotateVector(C * Scale);
	UE_LOG(LogTemp, Log, TEXT("HI: SketchfabModel %s full=(%.1f,%.1f,%.1f)UU center=(%.1f,%.1f,%.1f) scale=(%.3f,%.3f,%.3f) Loc=(%.1f,%.1f,%.1f) meshes=%d"),
		*Name, Full.X, Full.Y, Full.Z, C.X, C.Y, C.Z, Scale.X, Scale.Y, Scale.Z, Loc.X, Loc.Y, Loc.Z, Meshes.Num());
	for (UStaticMesh* M : Meshes)
	{
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
		Comp->SetStaticMesh(M);
		// Some glb materials import with a translucent blend (the fridge read as
		// semi-transparent); override every slot with the model's own base-colour texture,
		// sampled opaquely, when asked.
		if (OpaqueTexture != nullptr)
		{
			// A MODEL'S TEXTURE IS NOT ALWAYS INSIDE ITS OWN FOLDER. Poly Haven's gltf references
			// sibling jpgs, which the importer lands at /Game/HIImported/models/<image>, while the
			// Sketchfab rips carry theirs under <model>/Textures. Try the model folder first, then
			// the sibling path -- the difference is otherwise a silently untextured model.
			const FString TexPath = FString::Printf(TEXT("/Game/HIImported/models/%s/Textures/%s.%s"),
				*Name, OpaqueTexture, OpaqueTexture);
			UTexture* Tex = LoadObject<UTexture>(nullptr, *TexPath);
			if (Tex == nullptr)
			{
				const FString Sibling = FString::Printf(TEXT("/Game/HIImported/models/%s.%s"),
					OpaqueTexture, OpaqueTexture);
				Tex = LoadObject<UTexture>(nullptr, *Sibling);
			}
			if (Tex != nullptr)
			{
				if (UMaterialInterface* BaseMat = FHIMaterials::Generated(TEXT("M_HITextured")))
				{
					for (int32 SM = 0; SM < Comp->GetNumMaterials(); ++SM)
					{
						UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BaseMat, Comp);
						Mid->SetTextureParameterValue(TEXT("BaseTexture"), Tex);
						Mid->SetScalarParameterValue(TEXT("UVTiling"), 1.0f);
						// A DYNAMIC INSTANCE INHERITS EVERY PARAMETER IT DOES NOT SET (2026-10-28).
						// This override used to set the texture and nothing else, so the model took
						// M_HITextured's DEFAULT Tint and TextureStrength -- and the trees rendered
						// flat crimson while their own atlas measures olive (extracted from
						// SourceAssets/models/tree1.glb: avg (91,84,59)). The fridge's red frame was
						// the same defect. If a material input matters, the instance must state it.
						Mid->SetVectorParameterValue(TEXT("Tint"), TintColour);
						Mid->SetScalarParameterValue(TEXT("TextureStrength"), 1.0f);
						Comp->SetMaterial(SM, Mid);
					}
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("HI: no opaque texture at %s"), *TexPath);
			}
		}
		if (bCollide)
		{
			// Furniture: the mesh's own triangle collision blocks the bullet where the mesh
			// is, replacing the sim's colliding box. The sim's sight is unchanged.
			Comp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Comp->SetCollisionResponseToAllChannels(ECR_Ignore);
			Comp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
			Comp->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
		}
		else
		{
			Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->RegisterComponent();
		Comp->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepWorldTransform);
		// One atomic transform: separate SetWorldRotation/Scale/Location calls can mis-compose
		// when a scale and a rotation are both present (the cars parked sideways, 2026-10-20).
		Comp->SetRelativeTransform(FTransform(Rot, Loc, Scale));
		UE_LOG(LogTemp, Log, TEXT("HI: %s actualWorld=(%.1f,%.1f,%.1f) rel=(%.1f,%.1f,%.1f) parent=%s"),
			*Name,
			Comp->GetComponentLocation().X, Comp->GetComponentLocation().Y, Comp->GetComponentLocation().Z,
			Comp->GetRelativeLocation().X, Comp->GetRelativeLocation().Y, Comp->GetRelativeLocation().Z,
			Comp->GetAttachParent() ? *Comp->GetAttachParent()->GetName() : TEXT("none"));
	}
}

void AHIHouse::BuildFurniture(const FString& Label, const FBox2D& Rect, float Base, float Height)
{
	// Compose one furniture row into a recognisable silhouette (2026-10-19). The BODY is the
	// sim's exact footprint -- one colliding box, so a bullet still cannot pass where the AI
	// cannot see. Everything else is non-colliding decor that reads against the body. All
	// dimensions are metres, derived from the row's own rect, so one rule draws a 2.0 m bed
	// and a 0.45 m chair alike. Proportions are a first pass: they read as the type they are
	// meant to, and Richard's eye is the arbiter of whether they read WELL.
	const FVector2D C = Rect.GetCenter();
	const FVector2D S = Rect.GetSize();

	auto Part = [&](UInstancedStaticMeshComponent* Comp, float Cx, float Cy,
		float Z1, float Z2, float Sx, float Sy)
	{
		if (Comp != nullptr)
		{
			AddBox(Comp, FVector(Cx, Cy, (Z1 + Z2) * 0.5f), FVector(Sx, Sy, Z2 - Z1));
		}
	};
	// The rotated sibling of Part, for the tilted backs and raked faces that stop a box
	// reading as a box (2026-10-19).
	auto PartR = [&](UInstancedStaticMeshComponent* Comp, float Cx, float Cy,
		float Z1, float Z2, float Sx, float Sy, const FRotator& Rot)
	{
		if (Comp != nullptr)
		{
			AddBoxR(Comp, FVector(Cx, Cy, (Z1 + Z2) * 0.5f), FVector(Sx, Sy, Z2 - Z1), Rot);
		}
	};

	const float Top = Base + Height;

	// Real scanned models (Poly Haven CC0, no account, 2026-10-19) for the pieces a box
	// cannot read as. The model REPLACES the body: its own triangle collision blocks a
	// bullet exactly where the drawn mesh is, which is more honest than the box it replaces,
	// and the sim's sight (full-storey span over the row's rect) is unchanged. If the model
	// is missing the row falls through to the procedural composition below.
	auto ModelFor = [](const FString& L) -> const TCHAR*
	{
		if (L == TEXT("sofa"))         return TEXT("Sofa_01");
		if (L == TEXT("armchair"))     return TEXT("ArmChair_01");
		if (L == TEXT("dining_table")) return TEXT("dining_table");
		if (L == TEXT("chair"))        return TEXT("dining_chair_02");
		if (L == TEXT("coffee_table")) return TEXT("modern_coffee_table_01");
		if (L == TEXT("nightstand"))   return TEXT("ClassicNightstand_01");
		if (L == TEXT("bookshelf"))    return TEXT("wooden_bookshelf_worn");
		return nullptr;
	};
	// The sofa and bookshelf models import with their length on X, but the sim's rect has
	// length on Y; without a 90-degree yaw the per-axis scale stretches their depth 3.6x into
	// a boxy blob (2026-10-19, "furniture still boxy").
	auto ModelYaw = [](const FString& L) -> float
	{
		if (L == TEXT("sofa") || L == TEXT("bookshelf")) return 90.0f;
		return 0.0f;
	};
	// The gltf import did NOT link each model's material to its diff texture (the material
	// shipped with no texture reference), so the meshes rendered black. Apply the diff texture
	// by name instead (2026-10-20).
	auto DiffTex = [](const FString& L) -> const TCHAR*
	{
		if (L == TEXT("sofa"))         return TEXT("Sofa_01_diff_1k");
		if (L == TEXT("armchair"))     return TEXT("Armchair_01_diff_1k");
		if (L == TEXT("dining_table")) return TEXT("dining_table_diff_1k");
		if (L == TEXT("chair"))        return TEXT("dining_chair_02_diff_1k");
		if (L == TEXT("coffee_table")) return TEXT("modern_coffee_table_01_diff_1k");
		if (L == TEXT("nightstand"))   return TEXT("ClassicNightstand_01_diff_1k");
		if (L == TEXT("bookshelf"))    return TEXT("wooden_bookshelf_worn_diff_1k");
		return nullptr;
	};
	if (const TCHAR* Model = ModelFor(Label))
	{
		const FString Path = FString::Printf(
			TEXT("/Game/HIImported/models/%s/StaticMeshes/%s.%s"), Model, Model, Model);
		if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path))
		{
			const FBoxSphereBounds B = Mesh->GetBounds();
			const FVector MS = B.BoxExtent * 2.0f;   // full mesh size, in UU
			if (MS.X > 1.0f && MS.Y > 1.0f && MS.Z > 1.0f)
			{
				const float Yaw = ModelYaw(Label);
				const FRotator Rot(0.0f, Yaw, 0.0f);
				// Uniform scale: fit the model's longest axis to the row's largest dimension,
				// preserving its proportions (per-axis stretching was what made a scanned sofa
				// read as a box, 2026-10-20).
				const float Longest = FMath::Max3(MS.X, MS.Y, MS.Z);
				const float Key = FMath::Max3((float)S.X, (float)S.Y, Height);
				const float US = Key * HI_METERS_TO_UU / Longest;
				const FVector Scale(US, US, US);
				const FVector Target(C.X, C.Y, Base + Height * 0.5f);
				// The colliding body stays a plain box (the sim's exact rect) so a bullet still
				// cannot pass where the AI cannot see -- but on a HIDDEN component so the box
				// never pokes out around the mesh.
				Part(FurnitureCollisionInstances, C.X, C.Y, Base, Top, S.X, S.Y);
				UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
				Comp->SetStaticMesh(Mesh);
				Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Comp->SetMobility(EComponentMobility::Movable);
				Comp->RegisterComponent();
				Comp->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepWorldTransform);
				const FVector Loc = Target * HI_METERS_TO_UU - Rot.RotateVector(B.Origin * Scale);
				Comp->SetRelativeTransform(FTransform(Rot, Loc, Scale));
				// Apply the texture AFTER register+transform so it is not reset by registration.
				if (const TCHAR* DTex = DiffTex(Label))
				{
					FHIMaterials::TexturedModels(Comp, DTex, 1.0f, 0);
				}
				return;
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("HI: no model at %s -- run tools/fetch-models.py"), *Path);
		}
	}

	// Sketchfab furniture (CC BY, tools/fetch-sketchfab.py): the pieces a box cannot read
	// as, that Poly Haven had no no-account model for. The model REPLACES the body and its
	// own triangle collision blocks the bullet; the sim's sight is unchanged. Uniform scale
	// keys off the row's largest dimension so a tall fridge or a long bed both come out right.
	auto SketchfabFor = [](const FString& L) -> const TCHAR*
	{
		if (L == TEXT("bed"))        return TEXT("bed");
		if (L == TEXT("fridge"))     return TEXT("fridge");
		if (L == TEXT("toilet"))     return TEXT("toilet");
		if (L == TEXT("tub"))        return TEXT("tub");
		if (L == TEXT("washer"))     return TEXT("washer");
		if (L == TEXT("dryer"))      return TEXT("dryer");
		if (L == TEXT("furnace"))    return TEXT("furnace");
		if (L == TEXT("water_heater")) return TEXT("water_heater");
		if (L == TEXT("wardrobe"))   return TEXT("wardrobe");
		if (L == TEXT("desk"))       return TEXT("desk");
		if (L == TEXT("desk_chair")) return TEXT("deskchair");
		if (L == TEXT("sideboard"))  return TEXT("sideboard");
		if (L == TEXT("vanity"))     return TEXT("vanity");
		if (L == TEXT("lamp"))       return TEXT("floorlamp");
		if (L == TEXT("workbench"))  return TEXT("workbench");
		if (L == TEXT("shelving"))   return TEXT("shelving");
		// Reuse: the linen closet is shelving, and the coat closet is a tall cabinet (wardrobe).
		if (L == TEXT("linen_shelving")) return TEXT("shelving");
		if (L == TEXT("coat_closet"))    return TEXT("wardrobe");
		return nullptr;
	};
	if (const TCHAR* SModel = SketchfabFor(Label))
	{
		// Colliding body = the sim's box on the HIDDEN component, so the box never pokes out.
		Part(FurnitureCollisionInstances, C.X, C.Y, Base, Top, S.X, S.Y);
		// The fridge's glb material imported translucent; force it opaque.
		// The fridge's glb material imported translucent; sample its own base-colour texture
		// opaquely so it reads as a cream fridge, not a white box.
		const TCHAR* Opaque = (Label == TEXT("fridge")) ? TEXT("fridge_texture_0") : nullptr;
		PlaceSketchfabModel(SModel, C, (float)S.X, (float)S.Y, Base, Height,
			FRotator::ZeroRotator, /*bCollide=*/false, /*bPerAxis=*/true, Opaque);
		return;
	}

	// --- soft seating: fabric body, taller back on the -X edge, arms, two cushions
	if (Label == TEXT("sofa") || Label == TEXT("armchair"))
	{
		Part(FurnitureSoftInstances, C.X, C.Y, Base, Top, S.X, S.Y);
		// De-boxing (2026-10-19): a tilted back, three cushions with gaps, and short legs.
		PartR(FurnitureSoftDecorInstances, C.X - S.X * 0.5f + 0.10f, C.Y,
			Base + 0.10f, Top + 0.20f, 0.20f, S.Y, FRotator(-12.0f, 0.0f, 0.0f));  // back
		Part(FurnitureSoftDecorInstances, C.X + S.X * 0.15f, C.Y - S.Y * 0.5f + 0.08f,
			Base, Top, S.X, 0.16f);                                            // arm -Y
		Part(FurnitureSoftDecorInstances, C.X + S.X * 0.15f, C.Y + S.Y * 0.5f - 0.08f,
			Base, Top, S.X, 0.16f);                                            // arm +Y
		Part(FurnitureSoftDecorInstances, C.X + S.X * 0.16f, C.Y,
			Top - 0.12f, Top + 0.06f, S.X * 0.52f, S.Y * 0.24f);               // cushion mid
		Part(FurnitureSoftDecorInstances, C.X + S.X * 0.16f, C.Y - S.Y * 0.36f,
			Top - 0.12f, Top + 0.06f, S.X * 0.52f, S.Y * 0.24f);               // cushion -Y
		Part(FurnitureSoftDecorInstances, C.X + S.X * 0.16f, C.Y + S.Y * 0.36f,
			Top - 0.12f, Top + 0.06f, S.X * 0.52f, S.Y * 0.24f);               // cushion +Y
		for (float FX : { -0.425f, 0.425f })
		{
			for (float FY : { -0.425f, 0.425f })
			{
				Part(FurnitureDarkDecorInstances, C.X + FX * S.X, C.Y + FY * S.Y,
					Base - 0.04f, Base + 0.08f, 0.06f, 0.06f);
			}
		}
	}
	// --- tables: wood mass, a slightly-overhanging top, four dark legs
	else if (Label == TEXT("dining_table") || Label == TEXT("coffee_table")
		|| Label == TEXT("tv_stand") || Label == TEXT("sideboard") || Label == TEXT("workbench"))
	{
		Part(FurnitureInstances, C.X, C.Y, Base, Top, S.X, S.Y);
		Part(FurnitureWoodDecorInstances, C.X, C.Y, Top - 0.05f, Top + 0.03f, S.X + 0.10f, S.Y + 0.10f); // top
		Part(FurnitureDarkDecorInstances, C.X - S.X * 0.5f + 0.05f, C.Y - S.Y * 0.5f + 0.05f,
			Base - 0.04f, Top - 0.05f, 0.08f, 0.08f);
		Part(FurnitureDarkDecorInstances, C.X + S.X * 0.5f - 0.05f, C.Y - S.Y * 0.5f + 0.05f,
			Base - 0.04f, Top - 0.05f, 0.08f, 0.08f);
		Part(FurnitureDarkDecorInstances, C.X - S.X * 0.5f + 0.05f, C.Y + S.Y * 0.5f - 0.05f,
			Base - 0.04f, Top - 0.05f, 0.08f, 0.08f);
		Part(FurnitureDarkDecorInstances, C.X + S.X * 0.5f - 0.05f, C.Y + S.Y * 0.5f - 0.05f,
			Base - 0.04f, Top - 0.05f, 0.08f, 0.08f);
	}
	// --- chairs: a seat and a back over four legs
	else if (Label == TEXT("chair") || Label == TEXT("desk_chair"))
	{
		Part(FurnitureInstances, C.X, C.Y, Base, Top, S.X, S.Y);
		Part(FurnitureWoodDecorInstances, C.X, C.Y, Top - 0.10f, Top, S.X + 0.06f, S.Y + 0.06f);   // seat
		// De-boxing (2026-10-19): the back leans back instead of standing straight.
		PartR(FurnitureWoodDecorInstances, C.X - S.X * 0.5f + 0.05f, C.Y, Top - 0.10f, Top + 0.34f,
			0.10f, S.Y * 0.7f, FRotator(-14.0f, 0.0f, 0.0f));                            // back
		Part(FurnitureDarkDecorInstances, C.X - S.X * 0.5f + 0.04f, C.Y - S.Y * 0.5f + 0.04f,
			Base - 0.04f, Top - 0.12f, 0.06f, 0.06f);
		Part(FurnitureDarkDecorInstances, C.X + S.X * 0.5f - 0.04f, C.Y - S.Y * 0.5f + 0.04f,
			Base - 0.04f, Top - 0.12f, 0.06f, 0.06f);
		Part(FurnitureDarkDecorInstances, C.X - S.X * 0.5f + 0.04f, C.Y + S.Y * 0.5f - 0.04f,
			Base - 0.04f, Top - 0.12f, 0.06f, 0.06f);
		Part(FurnitureDarkDecorInstances, C.X + S.X * 0.5f - 0.04f, C.Y + S.Y * 0.5f - 0.04f,
			Base - 0.04f, Top - 0.12f, 0.06f, 0.06f);
	}
	// --- beds: wood frame, fabric mattress + duvet + two pillows
	else if (Label == TEXT("bed"))
	{
		Part(FurnitureInstances, C.X, C.Y, Base, Top, S.X, S.Y);
		// De-boxing (2026-10-19): a headboard and footboard bookend the mattress, four legs
		// lift the frame, and the duvet folds back over itself.
		Part(FurnitureWoodDecorInstances, C.X - S.X * 0.5f + 0.03f, C.Y,
			Base, Top + 0.55f, 0.06f, S.Y);                                    // headboard
		Part(FurnitureWoodDecorInstances, C.X + S.X * 0.5f - 0.03f, C.Y,
			Base, Top + 0.30f, 0.06f, S.Y);                                    // footboard
		Part(FurnitureSoftDecorInstances, C.X, C.Y, Top, Top + 0.16f, S.X - 0.10f, S.Y - 0.10f); // mattress
		Part(FurnitureSoftDecorInstances, C.X, C.Y, Top + 0.16f, Top + 0.24f, S.X - 0.20f, S.Y - 0.34f); // duvet
		Part(FurnitureSoftDecorInstances, C.X + S.X * 0.16f, C.Y, Top + 0.16f, Top + 0.22f,
			S.X * 0.55f, S.Y - 0.40f);                                         // folded fold
		Part(FurnitureSoftDecorInstances, C.X - S.X * 0.5f + 0.16f, C.Y,
			Top + 0.16f, Top + 0.30f, 0.30f, 0.60f);                           // pillow 1
		Part(FurnitureSoftDecorInstances, C.X - S.X * 0.5f + 0.16f, C.Y + S.Y * 0.24f,
			Top + 0.16f, Top + 0.30f, 0.30f, 0.60f);                           // pillow 2
		for (float FX : { -0.40f, 0.40f })
		{
			for (float FY : { -0.42f, 0.42f })
			{
				Part(FurnitureDarkDecorInstances, C.X + FX * S.X, C.Y + FY * S.Y,
					Base - 0.04f, Base + 0.08f, 0.07f, 0.07f);
			}
		}
	}
	// --- storage with doors: wood body, two door panels and a handle seam
	// (the dresser was removed from this group -- it has its own chest-of-drawers block
	// below, and being caught here first meant the drawer fronts never ran, 2026-10-21)
	else if (Label == TEXT("wardrobe") || Label == TEXT("coat_closet")
		|| Label == TEXT("bookshelf") || Label == TEXT("shelving") || Label == TEXT("linen_shelving")
		|| Label == TEXT("nightstand"))
	{
		Part(FurnitureInstances, C.X, C.Y, Base, Top, S.X, S.Y);
		Part(FurnitureDarkDecorInstances, C.X, C.Y, Base + 0.03f, Top - 0.03f, 0.02f, S.Y - 0.10f); // seam
		if (Label == TEXT("wardrobe") || Label == TEXT("nightstand"))
		{
			Part(FurnitureDarkDecorInstances, C.X - S.X * 0.5f + 0.03f, C.Y - S.Y * 0.32f,
				Top * 0.5f, Top * 0.5f + 0.05f, 0.05f, 0.05f);                        // handle
			Part(FurnitureDarkDecorInstances, C.X - S.X * 0.5f + 0.03f, C.Y + S.Y * 0.32f,
				Top * 0.5f, Top * 0.5f + 0.05f, 0.05f, 0.05f);                        // handle
		}
	}
	// --- kitchen + bathroom appliances: near-white body, dark worktop or details
	else if (Label == TEXT("counter") || Label == TEXT("island") || Label == TEXT("vanity"))
	{
		// Wood base + a wood-textured worktop (the flat colour's top face read white).
		Part(FurnitureInstances, C.X, C.Y, Base, Top, S.X, S.Y);           // wood base
		Part(CountertopInstances, C.X, C.Y, Top - 0.05f, Top + 0.02f,
			S.X + 0.10f, S.Y + 0.10f);
		// CABINET DOORS AND A TOE KICK (2026-10-28). The base was one flat slab, and in the
		// dining frame the island read as a beige box with a dark lid. Real kitchen cabinetry is
		// a recessed plinth with door faces above it, so: a dark toe kick inset 4 cm off every
		// side, then door panels standing 1 cm proud, split every ~60 cm along the LONG axis and
		// drawn on BOTH long faces -- an island is seen from both sides, and a run against a wall
		// hides its back one inside the wall anyway.
		Part(FurnitureDarkDecorInstances, C.X, C.Y, Base, Base + 0.10f,
			S.X - 0.08f, S.Y - 0.08f);                                     // toe kick
		{
			const bool bLongX = S.X >= S.Y;
			const float LongLen = bLongX ? S.X : S.Y;
			const int32 Panels = FMath::Max(1, FMath::RoundToInt(LongLen / 0.60f));
			const float DoorZ0 = Base + 0.12f;
			const float DoorZ1 = Top - 0.06f;
			for (int32 i = 0; i < Panels; ++i)
			{
				const float T = (i + 0.5f) / static_cast<float>(Panels);
				const float AlongLen = LongLen / Panels - 0.03f;
				if (bLongX)
				{
					const float X = C.X - S.X * 0.5f + S.X * T;
					for (float Side : { -1.0f, 1.0f })
					{
						Part(FurnitureWoodDecorInstances, X,
							C.Y + Side * (S.Y * 0.5f + 0.012f), DoorZ0, DoorZ1,
							AlongLen, 0.024f);
					}
				}
				else
				{
					const float Y = C.Y - S.Y * 0.5f + S.Y * T;
					for (float Side : { -1.0f, 1.0f })
					{
						Part(FurnitureWoodDecorInstances,
							C.X + Side * (S.X * 0.5f + 0.012f), Y, DoorZ0, DoorZ1,
							0.024f, AlongLen);
					}
				}
			}
		}
	}
	else if (Label == TEXT("fridge") || Label == TEXT("washer") || Label == TEXT("dryer"))
	{
		Part(FurnitureApplianceInstances, C.X, C.Y, Base, Top, S.X, S.Y);
		Part(FurnitureDarkDecorInstances, C.X - S.X * 0.5f - 0.02f, C.Y,
			Base + Height * 0.55f, Base + Height * 0.62f, 0.05f, 0.05f);           // handle
		if (Label == TEXT("washer") || Label == TEXT("dryer"))
		{
			Part(FurnitureDarkDecorInstances, C.X - S.X * 0.5f - 0.02f, C.Y,
				Base + Height * 0.40f, Base + Height * 0.55f, 0.05f, S.Y * 0.45f); // round door front
		}
	}
	else if (Label == TEXT("tub"))
	{
		Part(FurnitureApplianceInstances, C.X, C.Y, Base, Top, S.X, S.Y);
		Part(FurnitureApplianceDecorInstances, C.X, C.Y, Top, Top + 0.04f, S.X - 0.14f, S.Y - 0.14f); // rim
	}
	else if (Label == TEXT("toilet"))
	{
		Part(FurnitureApplianceInstances, C.X, C.Y, Base, Top, S.X, S.Y);
		Part(FurnitureApplianceDecorInstances, C.X - S.X * 0.5f + 0.08f, C.Y,
			Top, Top + 0.38f, 0.18f, 0.30f);                                          // tank
		Part(FurnitureDarkDecorInstances, C.X + S.X * 0.20f, C.Y, Top - 0.04f, Top, 0.26f, 0.30f); // seat
	}
	// --- lamp: dark pole, pale shade
	else if (Label == TEXT("lamp"))
	{
		Part(FurnitureDarkInstances, C.X, C.Y, Base, Top, S.X, S.Y);
		Part(FurnitureApplianceDecorInstances, C.X, C.Y, Top - 0.30f, Top + 0.05f,
			S.X * 0.8f, S.Y * 0.8f);                                                   // shade
	}
	// --- lally column: a round steel post (the engine Cylinder, not a box)
	else if (Label == TEXT("lally_column"))
	{
		WheelInstances->AddInstance(FTransform(
			FRotator::ZeroRotator,
			FVector(C.X, C.Y, (Base + Top) * 0.5f) * HI_METERS_TO_UU,
			FVector(S.X, S.Y, Top - Base)), /*bWorldSpace=*/false);
	}
	// --- dresser: a chest of drawers (wood body + pale drawer fronts + top)
	else if (Label == TEXT("dresser"))
	{
		Part(FurnitureInstances, C.X, C.Y, Base, Top, S.X, S.Y);
		Part(FurnitureInstances, C.X, C.Y, Top - 0.04f, Top + 0.02f,
			S.X + 0.06f, S.Y + 0.06f); // top
		// 3 pale drawer fronts, on BOTH faces so the wall side cannot hide them (the dresser
		// reads as a chest of drawers instead of a plain box). Proud of the body by 2 cm so
		// they don't z-fight with the brown face behind them.
		for (int32 D = 0; D < 3; ++D)
		{
			const float Z1 = Base + Height * (0.08f + 0.28f * (float)D);
			const float Z2 = Z1 + 0.22f;
			for (float Sgn : { -1.0f, 1.0f })
			{
				Part(FurnitureApplianceDecorInstances, C.X, C.Y + Sgn * (S.Y * 0.5f + 0.02f), Z1, Z2,
					S.X * 0.90f, 0.03f); // pale drawer front
				Part(FurnitureDarkDecorInstances, C.X, C.Y + Sgn * (S.Y * 0.5f + 0.05f),
					(Z1 + Z2) * 0.5f, (Z1 + Z2) * 0.5f + 0.03f, 0.12f, 0.02f); // handle
			}
		}
	}
	// --- everything else keeps its single wooden box (desk, workbench already covered above)
	else
	{
		Part(FurnitureInstances, C.X, C.Y, Base, Top, S.X, S.Y);
	}
}

void AHIHouse::Build()
{
	if (WallInstances == nullptr || FurnitureInstances == nullptr)
	{
		return;
	}
	// EVERY component is cleared, including the slice's three structural ones -- the header
	// promises Build() is idempotent, and a clear list that forgot Slabs/Stairs/Railings
	// would double every slab on a second call. It is called once per game today; the
	// promise is what keeps a second call site from being a bug.
	for (UInstancedStaticMeshComponent* Comp : { WallInstances, FurnitureInstances,
		FurnitureCollisionInstances, CountertopInstances,
		SlabInstances, StairInstances, RailingInstances, WallBasementInstances,
		FurnitureSoftInstances, FurnitureApplianceInstances, FurnitureDarkInstances,
		FurnitureSoftDecorInstances, FurnitureApplianceDecorInstances, FurnitureDarkDecorInstances,
		FurnitureWoodDecorInstances,
		StreetInstances, NeighborInstances, TreeInstances, WheelInstances, WheelRimInstances,
		GroundInstance, FloorInstance, BasementFloorInstances, RoofInstance, CeilingInstance,
		SidingInstances,
		TrimInstances, WindowInstances, GlassInstances, FittingInstances, PropInstances,
		CoatInstances, DoorHeadInstances, VanInstances, VanDetailInstances, GTOInstances })
	{
		if (Comp != nullptr)
		{
			Comp->ClearInstances();
		}
	}

	// ---- the walls, from WallRows(), with EXACTLY WorldSolids' span rule: a row starts
	// at MinZ if it overrides (the door transom, sill 2.10) and at its floor otherwise,
	// and it runs to TopZ if it overrides (the exterior, one span to the eaves) and to its
	// storey top otherwise. The drawn house and the house that stops a bullet are the same
	// house -- if this rule and the sim's ever diverge, that invariant is the first thing
	// that breaks. Below-grade rows go to their own component: same collision, cinderblock
	// paint (Richard's brief for the basement).
	for (const FHISim::FHIWallRow& Row : FHISim::WallRows())
	{
		const float MinZ = Row.MinZ > 0.0f ? Row.MinZ : FHISim::FloorHeight(Row.Floor);
		const float MaxZ = Row.TopZ > 0.0f ? Row.TopZ
			: (Row.Floor < 1 ? FHISim::FloorHeight(Row.Floor + 1) : FHISim::RoofTop);
		UInstancedStaticMeshComponent* Target =
			Row.Floor == -1 ? WallBasementInstances : WallInstances;
		if (Target != nullptr)
		{
			Target->AddInstance(BoxTransform(Row.Rect, MinZ, MaxZ), /*bWorldSpace=*/false);
		}
	}

	// ---- the furniture, ALL of it, each row on its own floor at its own height. The sim
	// carries four index-aligned arrays (rects, heights, flat flags, floors) and the
	// drawing reads them directly -- phase 2 replaced the slice's walk through Solids(),
	// which was ground-floor-only by definition. The mismatch between a drawn sofa (its own
	// 0.8 m) and the sight model's sofa (a full storey of span) stays exactly as inherited:
	// you cannot see over a sofa you can see the top of. Godot's `los_blocked` is 2-D and
	// ignores `h` too. That is fix-list item 18, deliberately not fixed here.
	{
		const TArray<FBox2D>& Rects = FHISim::FurnitureRects();
		const TArray<float>& Heights = FHISim::FurnitureHeights();
		const TArray<bool>& Flat = FHISim::FurnitureFlat();
		const TArray<int32>& Floors = FHISim::FurnitureFloors();
		const TArray<FString>& Labels = FHISim::FurnitureLabels();
		if (Rects.Num() != Heights.Num() || Rects.Num() != Flat.Num()
			|| Rects.Num() != Floors.Num() || Rects.Num() != Labels.Num()
			|| FurnitureInstances == nullptr)
		{
			return;
		}
		for (int32 Idx = 0; Idx < Rects.Num(); ++Idx)
		{
			// Flat rows (rugs) stay a single thin box; the rest are composed per label.
			if (Flat[Idx])
			{
				const float Base = FHISim::FloorHeight(Floors[Idx]);
				AddBox(FurnitureInstances,
					FVector(Rects[Idx].GetCenter().X, Rects[Idx].GetCenter().Y, Base + Heights[Idx] * 0.5f),
					FVector(Rects[Idx].GetSize().X, Rects[Idx].GetSize().Y, Heights[Idx]));
			}
			else
			{
				BuildFurniture(Labels[Idx], Rects[Idx],
					FHISim::FloorHeight(Floors[Idx]), Heights[Idx]);
			}
		}
	}

	// ---- the structural tail of WorldSolids: the decks, the stairwell screen and the
	// railings. WorldSolids is walls, then NON-FLAT furniture (all floors), then decks,
	// then the screen and railings -- so the tail starts at a computed index and the
	// split inside it is by the sim's own markers (bDeck, span height). Counted rather
	// than trusted: phase 2's house is 32 wall rows, 43 non-flat furniture rows, 9 decks
	// (4 ground + 4 slab + 1 roof) and 3 rail/screen solids, and a table that stops
	// matching these numbers is an editing accident, not a redesign.
	{
		const TArray<FHISim::FHISolid>& WS = FHISim::WorldSolids();
		int32 NonFlat = 0;
		for (bool b : FHISim::FurnitureFlat())
		{
			NonFlat += b ? 0 : 1;
		}
		const int32 Tail = WS.Num() - (FHISim::WallRows().Num() + NonFlat);
		if (Tail < 0)
		{
			return;
		}
		int32 Decks = 0;
		for (int32 Idx = WS.Num() - Tail; Idx < WS.Num(); ++Idx)
		{
			const FHISim::FHISolid& S = WS[Idx];
			if (S.bDeck)
			{
				// EVERY deck, the roof deck included: the sim says it stops a sightline,
				// so the drawn box stops a pellet. The gable planes above it are decor.
				++Decks;
				if (SlabInstances != nullptr)
				{
					SlabInstances->AddInstance(
						BoxTransform(S.Rect, S.Span.MinZ, S.Span.MaxZ), /*bWorldSpace=*/false);
				}
			}
			else if (S.Span.MaxZ - S.Span.MinZ <= 1.05f + KINDA_SMALL_NUMBER)
			{
				// Railing class: the same metre-tall test ResolveCircleZ's stair skip and
				// WalkBlocked's span overlap use, so what a climber may pass and what is
				// drawn cannot disagree.
				if (RailingInstances != nullptr)
				{
					// A RAILING, NOT A PARAPET SLAB (2026-10-28). This was one solid box in Trim
					// white, and in the office frame the stair screen's top metre read as a blank
					// white wall -- the largest object in the picture and the least legible. The
					// SIM still carries the box (the span rules are unchanged); only the drawing
					// changes to what a stair railing is: a wood cap, a bottom rail, and balusters
					// at 15 cm centres.
					const FBox2D& R = S.Rect;
					const FVector2D Size = R.GetSize();
					const bool bAlongX = Size.X >= Size.Y;
					const float Len = bAlongX ? Size.X : Size.Y;
					const float H = S.Span.MaxZ - S.Span.MinZ;
					const float CapT = 0.05f;
					const float BotT = 0.04f;
					const float CX = (R.Min.X + R.Max.X) * 0.5f;
					const float CY = (R.Min.Y + R.Max.Y) * 0.5f;
					AddBox(StairInstances, FVector(CX, CY, S.Span.MaxZ - CapT * 0.5f),
						FVector(Size.X + 0.05f, Size.Y + 0.05f, CapT));
					AddBox(RailingInstances, FVector(CX, CY, S.Span.MinZ + BotT * 0.5f),
						FVector(Size.X + 0.02f, Size.Y + 0.02f, BotT));
					const int32 N = FMath::Max(2, FMath::RoundToInt(Len / 0.15f));
					for (int32 i = 0; i <= N; ++i)
					{
						const float T = static_cast<float>(i) / static_cast<float>(N);
						const float X = bAlongX ? FMath::Lerp(R.Min.X, R.Max.X, T) : CX;
						const float Y = bAlongX ? CY : FMath::Lerp(R.Min.Y, R.Max.Y, T);
						AddBox(RailingInstances, FVector(X, Y, (S.Span.MinZ + S.Span.MaxZ) * 0.5f),
							FVector(0.03f, 0.03f, H - CapT - BotT));
					}
				}
			}
			else if (WallInstances != nullptr)
			{
				// The stairwell screen: a full-height wall the sim's WorldSolids carries
				// directly (it belongs to no storey's WallRows because it climbs 0..3.74
				// across two), drawn with the walls it behaves like.
				WallInstances->AddInstance(
					BoxTransform(S.Rect, S.Span.MinZ, S.Span.MaxZ), /*bWorldSpace=*/false);
			}
		}
		if (Decks != 9 || Tail != 12)
		{
			UE_LOG(LogTemp, Error,
				TEXT("HI: WorldSolids tail is %d entries with %d decks, expected 12 and 9 -- structural tables changed without the builder"),
				Tail, Decks);
		}
	}

	// ---- the treads. The sim walks the band's interpolated Z; the treads are the visible
	// steps under it, with OPEN risers, so the stairwell's sight tube stays honest: what
	// the sim's exact span clipping says is visible through the hole is also visible past
	// the drawn stair. Phase 2 has two flights with DIFFERENT riser counts -- the doc's
	// own numbers: 15 risers of 0.183 on the ground run, 14 of 0.174 below grade -- and a
	// hardcoded 16 (the slice's single flight) would have stretched both.
	for (const FHISim::FHIStairBand& B : FHISim::Stairs())
	{
		const int32 Risers = B.FloorLow == -1 ? 14 : 15;
		const bool bRunsY = FMath::Abs(B.Top.Y - B.Bottom.Y) >= FMath::Abs(B.Top.X - B.Bottom.X);
		for (int32 I = 1; I <= Risers; ++I)
		{
			const float T0 = static_cast<float>(I - 1) / static_cast<float>(Risers);
			const float T1 = static_cast<float>(I) / static_cast<float>(Risers);
			const float TopZ = B.BottomH + (B.TopH - B.BottomH) * T1;
			FBox2D Plan = B.Rect;
			if (bRunsY)
			{
				const float Y0 = B.Bottom.Y + (B.Top.Y - B.Bottom.Y) * T0;
				const float Y1 = B.Bottom.Y + (B.Top.Y - B.Bottom.Y) * T1;
				Plan = FBox2D(FVector2D(B.Rect.Min.X, FMath::Min(Y0, Y1)),
					FVector2D(B.Rect.Max.X, FMath::Max(Y0, Y1)));
			}
			else
			{
				const float X0 = B.Bottom.X + (B.Top.X - B.Bottom.X) * T0;
				const float X1 = B.Bottom.X + (B.Top.X - B.Bottom.X) * T1;
				Plan = FBox2D(FVector2D(FMath::Min(X0, X1), B.Rect.Min.Y),
					FVector2D(FMath::Max(X0, X1), B.Rect.Max.Y));
			}
			if (StairInstances != nullptr)
			{
				StairInstances->AddInstance(BoxTransform(Plan, TopZ - 0.05f, TopZ), /*bWorldSpace=*/false);
			}
		}
	}

	// Colours, applied once to each component's single material slot, so every instance of
	// that component shares it. Without these the whole world is the BasicShapeMaterial's
	// default grey and a wall, a floor, a van and a man are indistinguishable.
	// TEXTURES, where there is one, and the flat colour where there is not. The Godot
	// repository's own `assets/textures/` are imported to /Game/HIImported/textures; until
	// the generated M_HITextured existed there was nothing in this project that could sample
	// one, and every surface was a flat colour because a .uasset could not be made.
	//
	// Each of these keeps its `Tint` fallback underneath, so a missing texture or a missing
	// generator run leaves the previous look rather than a black house.

	// Build dressing + neighbourhood BEFORE the material pass: SetMaterial on an instanced
	// component does not reach instances added afterwards, which left the props, windows and
	// street as default grey instead of their tints.
	// THE BUSH MESH (2026-10-28). The hedge runs were boxes with a leaf-scale texture, which was
	// the honest stopgap when no hedge art existed. The Godot project's own CC0 asset pack has
	// one, so the runs are instanced bushes now: one component, one draw call per run.
	{
		FAssetRegistryModule& AR2 = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> BushAssets;
		AR2.Get().GetAssetsByPath(FName(TEXT("/Game/HIImported/models/bush")), BushAssets, true);
		for (const FAssetData& A : BushAssets)
		{
			if (A.GetClass() != UStaticMesh::StaticClass())
			{
				continue;
			}
			if (UStaticMesh* M = Cast<UStaticMesh>(A.GetAsset()))
			{
				BushInstances->SetStaticMesh(M);
				BushHeightUU = M->GetBounds().GetBox().GetSize().Z;
				break;
			}
		}
		if (BushHeightUU <= 0.0f)
		{
			UE_LOG(LogTemp, Warning, TEXT("HI: no bush mesh -- hedges fall back to boxes"));
		}
		// THE LEAF CARDS. M_HILeaf is generated by tools/make-assets.py from leaf_cluster.png --
		// a masked, two-sided, LIT foliage material. Applied before BuildNeighborhood so every
		// instance added below inherits it; an InstancedStaticMesh keeps the material it had when
		// its instances were added.
		if (LeafInstances != nullptr)
		{
			if (UMaterialInterface* LeafBase = LoadObject<UMaterialInterface>(
					nullptr, TEXT("/Game/HIGenerated/M_HILeaf.M_HILeaf")))
			{
				UMaterialInstanceDynamic* LeafMid =
					UMaterialInstanceDynamic::Create(LeafBase, this);
				LeafMid->SetVectorParameterValue(TEXT("Tint"),
					FLinearColor(0.26f, 0.40f, 0.17f));
				LeafInstances->SetMaterial(0, LeafMid);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("HI: no M_HILeaf -- hedge cards will not draw"));
			}
		}
	}

	BuildDressing();
	BuildNeighborhood();

	// 2026-10-23: the wall texture now takes the tint it was always meant to. `WallPaint`
	// at strength 0.30 is painted drywall with the plaster scan's grain through it, instead
	// of the scan's own olive blotches at full strength (which read as sandstone).
	FHIMaterials::Tint(WallInstances, FHIMaterials::WallPaint);
	if (FHIMaterials::Textured(WallInstances, TEXT("ceiling_plaster"), 40.0f,
			0, FHIMaterials::WallPaint, 0.30f) == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("HI: walls stay flat -- no M_HITextured or texture"));
	}
	// The house's outside skin (2026-10-23). Tiling 1.0 puts the scan's full run of boards
	// across one ~1.9 m panel, which lands near 12 cm a board on every facade -- the whole
	// reason the panels are equal-width. A missing `siding` texture degrades to the tint.
	FHIMaterials::Tint(SidingInstances, FHIMaterials::WallExterior);
	FHIMaterials::Textured(SidingInstances, TEXT("siding"), 1.0f,
		0, FLinearColor(0.97f, 0.95f, 0.90f), 1.0f);
	// The basement: Richard's brief is CINDERBLOCK. The asset set once had no cinderblock
	// texture, so it wore a coarse plaster grain as a stand-in. A real concrete-block
	// texture is now imported (Poly Haven `concrete_block_wall`, CC0, 2026-10-19), so the
	// brief is met as written: block, not the cream plaster of the rooms above.
	// Tiling 14 -> 26 (2026-10-23): at 14 the courses rendered ~30 cm high on a 10 m wall,
	// which reads as a retaining wall, not a basement.
	FHIMaterials::Tint(WallBasementInstances, FHIMaterials::WallExterior);
	FHIMaterials::Textured(WallBasementInstances, TEXT("cinderblock"), 26.0f,
		0, FHIMaterials::WallExterior, 1.0f);
	// The basement slab wears the asset set's one tile floor -- a utility floor may be
	// tiled where the storeys above are wood.
	FHIMaterials::Tint(BasementFloorInstances, FHIMaterials::Floor);
	FHIMaterials::Textured(BasementFloorInstances, TEXT("floor_tile"), 8.0f,
		0, FLinearColor(0.86f, 0.86f, 0.83f), 1.0f);
	FHIMaterials::Tint(FloorInstance, FHIMaterials::Floor);
	FHIMaterials::Textured(FloorInstance, TEXT("floor_wood"), 6.0f,
		0, FHIMaterials::Floor, 1.0f);
	FHIMaterials::Tint(GroundInstance, FHIMaterials::Grass);
	// The lawn is a 5 km plane, so its tiling is large: one repeat every ~13 m reads as grass
	// from the doorstep without turning to noise at the horizon.
	// 2026-10-23: this is THE fix for the yard. The scan is a dry, tan-dominated lawn, and
	// at full strength with no tint the front yard rendered as desert -- the single largest
	// "this is not a suburb" tell in the frame. Tinted green and slightly darker.
	FHIMaterials::Textured(GroundInstance, TEXT("lawn"), 400.0f,
		0, FLinearColor(0.55f, 0.78f, 0.34f), 1.0f);
	FHIMaterials::Tint(RoofInstance, FHIMaterials::WallExterior);
	FHIMaterials::Textured(RoofInstance, TEXT("roof_shingle"), 8.0f,
		0, FLinearColor(0.86f, 0.84f, 0.80f), 1.0f);
	FHIMaterials::Tint(FurnitureInstances, FHIMaterials::Wood);
	// The composed furniture's materials (2026-10-19): fabric in the sofa blue, appliances
	// in near-white, dark metal/screens in near-black -- body and detail share a tint so a
	// sofa's cushions read as one piece with its base.
	FHIMaterials::Tint(FurnitureSoftInstances, FHIMaterials::Sofa);
	FHIMaterials::Tint(FurnitureSoftDecorInstances, FHIMaterials::Sofa);
	FHIMaterials::Tint(FurnitureApplianceInstances, FHIMaterials::Trim);
	FHIMaterials::Tint(FurnitureApplianceDecorInstances, FHIMaterials::Trim);
	FHIMaterials::Tint(FurnitureDarkInstances, FHIMaterials::Tyre);
	FHIMaterials::Tint(FurnitureDarkDecorInstances, FHIMaterials::Tyre);
	FHIMaterials::Tint(FurnitureWoodDecorInstances, FHIMaterials::Wood);
	FHIMaterials::Tint(CountertopInstances, FHIMaterials::DarkSteel);
	// The neighborhood scenery (2026-10-19): grey street/sidewalk/driveway, plaster neighbor
	// shells, green tree canopies.
	// ASPHALT, NOT WHITE (2026-10-28). Street, kerbs, sidewalks and the driveway all share this
	// one component, so the whole ground under the front camera came out the colour of paper --
	// the driveway in particular read as a runway. Path (0.34) under this sun clips. 0.17 keeps
	// it a road surface and takes the glare off the frame.
	FHIMaterials::Tint(StreetInstances, FLinearColor(0.16f, 0.16f, 0.17f));
	FHIMaterials::Tint(NeighborInstances, FHIMaterials::WallExterior);
	FHIMaterials::Textured(NeighborInstances, TEXT("ceiling_plaster"), 20.0f,
		0, FHIMaterials::WallExterior, 0.45f);
	// (A second copy of the neighbour houses and their hedges lived here, in the MATERIAL
	// pass, from the 2026-10-23 patch. It was added while `BuildNeighborhood` itself was
	// emptied, so the same five houses were built twice from two lambdas -- once here and
	// once in `BuildNeighborhood` -- and the copy here carried the metre/centimetre hedge bug
	// and the grey-plaster hedges. `BuildNeighborhood` has the one remaining definition. The
	// block also would not compile once its `AddInstance` helper was removed, which is how
	// this was found: the patch had never been built.)
	FHIMaterials::Tint(TreeInstances, FHIMaterials::Grass);
	// HEDGES READ AS BOXES BECAUSE THEY WERE FLAT GREEN (2026-10-28, from play: "the hedges are
	// boxes, not hedges"). There is no hedge asset in the project and the models fetched so far
	// are trees, so the cheap honest step is a sampled surface: the lawn scan at a fine tiling
	// breaks the flat fill into leaf-scale noise, and the grass tint keeps it the right green.
	// TreeInstances holds ONLY the hedges -- the trees are imported models -- so this cannot
	// reach anything it should not.
	// 2026-10-28: the hedges read almost BLACK at Grass (0.16,0.30,0.12) with the scan at 0.85
	// strength -- a clipped hedge in daylight is a mid green, lighter than the lawn it stands on.
	// Finer tiling: at 6 m the scan read as mown grass up close; 9 m is leaf-scale.
	if (FHIMaterials::Textured(TreeInstances, TEXT("lawn"), 13.0f,
			0, FLinearColor(0.56f, 0.82f, 0.42f), 1.0f) == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("HI: hedges stay flat -- no lawn texture"));
	}
	FHIMaterials::Tint(WheelInstances, FHIMaterials::Tyre);
	FHIMaterials::Tint(WheelRimInstances, FHIMaterials::Trim);
	// The decks: every deck in WorldSolids is drawn by SlabInstances now -- the ground
	// deck, the storey slab AND the roof deck -- because the sim says each of them stops a
	// bullet, and the invariant at the top of this header forbids a solid the sim knows
	// about that the engine does not. The slab's walked face wears the floor's wood; its
	// undersides are hidden by the ceiling panels; the roof deck hides under the gable.
	FHIMaterials::Tint(SlabInstances, FHIMaterials::Floor);
	FHIMaterials::Textured(SlabInstances, TEXT("floor_wood"), 6.0f,
		0, FHIMaterials::Floor, 1.0f);
	FHIMaterials::Tint(StairInstances, FHIMaterials::Wood);
	FHIMaterials::Tint(RailingInstances, FHIMaterials::Trim);
	FHIMaterials::Tint(CeilingInstance, FHIMaterials::Ceiling);
	FHIMaterials::Tint(TrimInstances, FHIMaterials::Trim);
	FHIMaterials::Tint(WindowInstances, FHIMaterials::Trim);
	// The glazing: a GENERATED material if the generator has been run, and the old opaque
	// pane if it has not. A missing asset must degrade to the previous look rather than to
	// a white void -- that is the whole reason `Generated` returns nullptr instead of a
	// default. Run tools/make-assets.sh to get the real thing.
	if (UMaterialInterface* GlassMat = FHIMaterials::Generated(TEXT("M_HIGlass")))
	{
		if (UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(GlassMat, GlassInstances))
		{
			// DARK NIGHT GLASS, and the reason is geometric rather than stylistic: `AddWindow`
			// puts the pane 15 cm PROUD of a SOLID wall (nothing in `WallRows` is cut for a
			// window -- the sim's sight lines run straight through that wall). So there is no
			// room behind the glass to see: a bright pane was showing the plaster wall it
			// stands in front of, and every window in an exterior frame read as a white
			// sticker on the siding instead of as a window. A dark pane over a lit facade is
			// what glass actually looks like from outside at night, and it is the only honest
			// answer available while the opening is a panel.
			//
			// OBSERVED: 0.30/0.40/0.52 at opacity 0.72 put the pane BRIGHTER than the wall in
			// every front view (the 10-cell grid sheet, 2026-10-19, cells 1 and 3, and again in
			// the packaged build's front frame on 2026-10-23). Lower opacity matters as much as
			// a darker colour -- the pane blends toward whatever is behind it.
			Mid->SetScalarParameterValue(TEXT("Opacity"), 0.55f);
			Mid->SetVectorParameterValue(TEXT("GlassColor"), FLinearColor(0.045f, 0.060f, 0.085f, 1.0f));
			GlassInstances->SetMaterial(0, Mid);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("HI: M_HIGlass missing -- windows fall back to opaque. Run tools/make-assets.sh"));
		FHIMaterials::Tint(GlassInstances, FHIMaterials::Glass);
	}
	FHIMaterials::Tint(CoatInstances, FHIMaterials::Coat);
	FHIMaterials::Tint(FittingInstances, FHIMaterials::Trim);
	FHIMaterials::Tint(PropInstances, FLinearColor(0.30f, 0.32f, 0.36f));  // dark steel (0.48 still read white under the light)
	// The door surround is WALL, not trim: jambs and a head against a cream facade should
	// still read as masonry-adjacent paint, not as a white outline where a door was cut.
	FHIMaterials::Tint(DoorHeadInstances, FHIMaterials::WallPaint);
	FHIMaterials::Textured(DoorHeadInstances, TEXT("ceiling_plaster"), 40.0f * (2.0f / 16.0f),
		0, FHIMaterials::WallPaint, 0.30f);
	FHIMaterials::Tint(VanInstances, FHIMaterials::Van);
	FHIMaterials::Tint(VanDetailInstances, FHIMaterials::Tyre);
	FHIMaterials::Tint(GTOInstances, FHIMaterials::GTOGreen);

	// WHAT WAS ACTUALLY BUILT, counted. The first render of this port showed what looked like
	// a house but could equally have been one wall seen edge-on, and a screenshot cannot tell
	// those apart. These counts can. Phase 2's numbers, all derivable from the sim's tables:
	// walls=27 (26 above-grade rows + the stairwell screen), basement_walls=6 (4 exterior
	// runs + 2 partitions), slabs=9 decks, stairs=29 treads (15 + 14), railings=2, ceiling=9
	// panels, roof=19 (2 slopes + ridge + 16 gable steps), trim=44 (22 interior rows x
	// skirting + rail), ground=4 lawn strips, floor=4 wood panels, basement_floor=1,
	// door_head=3, coat=2. Furniture bodies are split across wood/soft/appliance/dark since
	// the 2026-10-19 composition, and its decor is counted separately; the four body counts
	// together cover the 48 non-flat rows. Every other component must simply be non-zero, or
	// something in this function silently did not run.
	UE_LOG(LogTemp, Display,
		TEXT("HI: house built -- walls=%d basement_walls=%d furniture=%d furniture_soft=%d furniture_appliance=%d furniture_dark=%d furn_woodd=%d furn_softd=%d furn_appd=%d furn_darkd=%d ground=%d floor=%d basement_floor=%d roof=%d ceiling=%d trim=%d frames=%d glass=%d fittings=%d props=%d coat=%d door_head=%d van=%d van_detail=%d gto=%d slabs=%d stairs=%d railings=%d street=%d neighbor=%d trees=%d bushes=%d bushH=%.1f wheels=%d rims=%d"),
		WallInstances->GetInstanceCount(),
		WallBasementInstances->GetInstanceCount(),
		FurnitureInstances->GetInstanceCount(),
		FurnitureSoftInstances->GetInstanceCount(),
		FurnitureApplianceInstances->GetInstanceCount(),
		FurnitureDarkInstances->GetInstanceCount(),
		FurnitureWoodDecorInstances->GetInstanceCount(),
		FurnitureSoftDecorInstances->GetInstanceCount(),
		FurnitureApplianceDecorInstances->GetInstanceCount(),
		FurnitureDarkDecorInstances->GetInstanceCount(),
		GroundInstance->GetInstanceCount(), FloorInstance->GetInstanceCount(),
		BasementFloorInstances->GetInstanceCount(),
		RoofInstance->GetInstanceCount(), CeilingInstance->GetInstanceCount(),
		TrimInstances->GetInstanceCount(),
		WindowInstances->GetInstanceCount(), GlassInstances->GetInstanceCount(),
		FittingInstances->GetInstanceCount(),
		PropInstances->GetInstanceCount(), CoatInstances->GetInstanceCount(),
		DoorHeadInstances->GetInstanceCount(),
		VanInstances->GetInstanceCount(),
		VanDetailInstances->GetInstanceCount(),
		GTOInstances->GetInstanceCount(),
		SlabInstances->GetInstanceCount(),
		StairInstances->GetInstanceCount(),
		RailingInstances->GetInstanceCount(),
		StreetInstances->GetInstanceCount(),
		NeighborInstances->GetInstanceCount(),
		TreeInstances->GetInstanceCount(),
		BushInstances != nullptr ? BushInstances->GetInstanceCount() : -1,
		BushHeightUU,
		WheelInstances->GetInstanceCount(),
		WheelRimInstances->GetInstanceCount());
	UE_LOG(LogTemp, Display, TEXT("HI: shape material %s"),
		FHIMaterials::ShapeMaterial() != nullptr ? TEXT("loaded") : TEXT("MISSING -- everything will be default grey"));
}

// Patch: Fix hedges + nice neighbor houses (applied 2024-10-23)
// Changes:
// - Main-house hedges moved to y = -12.5, height = 1.83f, collision enabled.
// - Neighbor houses replaced with NiceHouse() (porch, shingle roof, dark door, windows).
// - Each neighbor gets 2.0f-meter hedges around its property.

void AHIHouse::BuildNeighborhood()
{
	// Realism item 2: the outside doesn't look like a neighborhood
	{
		// +0.03 CENTRE, NOT +0.01 (2026-10-23). The lawn's top face sat at 0.00 and these
		// strips' top at 0.02 -- 2 cm apart across a 5 km plane, which at a grazing angle is
		// inside the depth buffer's precision and FIGHTS. Measured rather than argued: two
		// captures of the SAME front camera disagreed about the ground, one showing grey
		// driveway and the next showing lawn, with nothing else changed. The lawn is now
		// -0.02 (top face) and the street +0.04, which is 6 cm of separation.
		const float SZ = 0.02f;
		auto Strip = [&](float X1, float X2, float Y1, float Y2)
		{
			AddBox(StreetInstances, FVector((X1 + X2) * 0.5f, (Y1 + Y2) * 0.5f, 0.03f),
				FVector(X2 - X1, Y2 - Y1, SZ));
		};
		// FOUR CAR WIDTHS (2026-10-28, from play: "the road should be wider, it should be 4 car
		// width, enough room for traffic and parking at the same time"). It was 4.6 m -- one
		// lane, with the parked cars sitting IN it. 11.4 m is two 3.3 m traffic lanes plus a
		// 2.4 m parking lane at each kerb. The neighbours had to move back with it (below),
		// because their front doors were 0.8 m from the old far kerb.
		const float StreetNear = 6.6f;
		const float StreetFar = 18.0f;
		Strip(-260.0f, 260.0f, StreetNear, StreetFar);  // street, to the horizon
		Strip(-260.0f, 260.0f, 5.4f, 6.6f);             // our sidewalk
		Strip(-260.0f, 260.0f, 18.0f, 19.2f);           // far sidewalk
		Strip(-1.6f, 1.6f, -7.75f, 6.6f);             // driveway
		Strip(-260.0f, 260.0f, 6.6f, 6.74f);            // our kerb
		Strip(-260.0f, 260.0f, 17.86f, 18.0f);          // far kerb
		Strip(-260.0f, 260.0f, 5.26f, 5.4f);
		for (float X = -258.0f; X <= 258.0f; X += 4.0f)
		{
			AddBox(TrimInstances, FVector(X, (StreetNear + StreetFar) * 0.5f, 0.075f),
				FVector(1.5f, 0.10f, 0.02f));           // centre line
		}
	}

	// Main-house hedges: 6 ft (1.83 m) at the property line.
	//
	// 2026-10-23 FIX -- the version this replaces was wrong twice over and had never been
	// built, which is why neither fault was visible:
	//   (a) its "front hedge" sat at y = -12.5, which is INSIDE the house (the house spans
	//       y -17.14 .. -8.00), so a row of green boxes would have stood through the living
	//       room and the master bedroom;
	//   (b) it went through the file-local `AddInstance`, which passes metres straight into
	//       an Unreal transform -- CENTIMETRES -- so even correct coordinates would have
	//       collapsed every hedge into a blob at the world origin. `AddBox` converts; it is
	//       the only box helper here that does.
	//
	// Geometry is SECTIONS ALONG THE RUN (2 m long, 0.4 m through, 1.7 m tall), not 4 m deep
	// pillars stepping 2 m sideways, which is what the old `HedgeScale` described.
	auto HedgeRun = [&](float X1, float Y1, float X2, float Y2, float Height, bool bOrganic = false)
	{
		const float DX = X2 - X1;
		const float DY = Y2 - Y1;
		const float Along = FMath::Sqrt(DX * DX + DY * DY);
		const float Spacing = bOrganic ? 2.15f : (BushHeightUU > 0.0f ? 1.20f : 2.0f);
		const int32 Sections = FMath::Max(1, FMath::RoundToInt(Along / Spacing));
		const float YawDeg = FMath::RadiansToDegrees(FMath::Atan2(DY, DX));
		for (int32 i = 0; i < Sections; ++i)
		{
			const float T = (i + 0.5f) / Sections;
			const float X = X1 + DX * T;
			const float Y = Y1 + DY * T;
			// A cosine jitter in height and yaw: without it a hedgerow reads as one stamp
			// repeated, which is the same complaint as "the hedges are boxes".
			// A CLIPPED HEDGE IS A BOX, AND THAT IS THE POINT (2026-10-28, second look). The
			// KayKit "bush" is a POTTED PLANT -- green foliage in a terracotta pot -- so a run of
			// them was a row of planters pretending to be a hedge. A clipped hedge really is a
			// continuous rectangular mass with a flat-ish top, so it is drawn as one: overlapping
			// boxes at 1.2 m centres, 0.55 m through, carrying the lawn scan at a fine tiling so
			// the surface reads as leaves, with a 3 cm height wobble so the top line looks
			// clipped rather than machined. TreeInstances holds only hedging and carries both the
			// grass tint and that texture.
			// Two frequencies of wobble: a slow clump and a per-section step, so the top line
			// reads as clipped foliage instead of a machined edge.
			const float H = Height * (0.97f + 0.055f * FMath::Sin((float)i * 2.1f)
				+ 0.020f * FMath::Sin((float)i * 7.3f));
			// REAL FOLIAGE WHERE THE CAMERA ACTUALLY GOES (2026-10-29, "the hedges still look a
			// little blocky"). A textured box has a box silhouette no matter what is mapped onto
			// it, so the run around OUR OWN plot -- the one the player walks beside -- is built
			// from a Poly Haven photogrammetry shrub (CC0, shrub_01, 156k tris) placed as
			// overlapping clumps. The distant neighbour runs stay boxes: they are across a 11 m
			// street and 156k triangles each is not worth spending there.
			if (bOrganic)
			{
				// PER-AXIS, because this scan is 6.05 m WIDE and 1.70 m tall -- a spreading
				// ground shrub. Uniform scaling keeps its proportions and gives a knee-high
				// strip, so the footprint is forced to a hedge: 2.6 m along the run, 1.2 m
				// through, 1.7 m tall. The stretch is invisible at hedge scale because the
				// silhouette is what matters, not the individual leaves.
				PlaceSketchfabModel(TEXT("shrub_02"), FVector2D(X, Y),
					2.60f * (1.0f + 0.10f * FMath::Sin((float)i * 1.9f)), 1.25f,
					-0.30f, Height + 0.30f, FRotator(0.0f, 41.0f * (float)i, 0.0f),
					/*bCollide=*/false, /*bPerAxis=*/true, TEXT("shrub_02_diff_1k"));
				continue;
			}
			// THE BLOCKINESS IS THE SILHOUETTE, NOT THE TEXTURE (2026-10-29). A hedge made of
			// equal boxes has one flat face and one perfectly straight top; real clipped hedging
			// wanders in and out by a few centimetres along its length. So each section gets its
			// own through-depth (0.50..0.64 m), a small sideways nudge along the run's normal, and
			// a height wobble -- three cheap variations that break the plane without adding
			// geometry. (A photogrammetry shrub was tried first: Poly Haven's shrub_01/02 are
			// prostrate and BARE -- shrub_02 rendered as dry twigs across the lawn -- so the run
			// stays boxed and this is as far as the assets here can take it.)
			// Overlap, yaw and depth all vary per section. Equal boxes butted end to end give a
			// straight top, one flat face and a vertical seam every 1.2 m -- which is exactly what
			// "blocky" means. Overlapping them by 0.35 m interleaves the faces, a 2.5 degree yaw
			// means no two are coplanar, and the depth/nudge variation keeps the run wandering.
			const float Depth = 0.57f + 0.07f * FMath::Sin((float)i * 3.7f);
			const float Nudge = 0.05f * FMath::Sin((float)i * 5.1f);
			const float YawJitter = 2.5f * FMath::Sin((float)i * 9.7f);
			const float NX = -DY / FMath::Max(Along, KINDA_SMALL_NUMBER);
			const float NY = DX / FMath::Max(Along, KINDA_SMALL_NUMBER);
			AddBox(TreeInstances,
				FVector(X + NX * Nudge, Y + NY * Nudge, H * 0.5f),
				FVector(Along / Sections + 0.35f, Depth * 0.80f, H * 0.97f),
				YawDeg + YawJitter);

			// LEAF CARDS OVER THE BODY (2026-10-29). The box stays as the opaque core -- a hedge
			// you can see through is worse than a hedge that looks like a box -- and cards are
			// scattered through its upper two thirds so the silhouette and the surface are
			// foliage rather than paint. Deterministic hashes, not rand(): the same build must
			// produce the same hedge, or a capture diff would move every time.
			if (LeafInstances != nullptr)
			{
				const float LX = DX / FMath::Max(Along, KINDA_SMALL_NUMBER);
				const float LY = DY / FMath::Max(Along, KINDA_SMALL_NUMBER);
				const float SectionLen = Along / Sections;
				for (int32 c = 0; c < 7; ++c)
				{
					const float Seed = static_cast<float>(i * 7 + c * 31 + 1);
					const float U1 = 0.5f + 0.5f * FMath::Sin(Seed * 12.9898f);
					const float U2 = 0.5f + 0.5f * FMath::Sin(Seed * 78.233f);
					const float U3 = 0.5f + 0.5f * FMath::Sin(Seed * 37.719f);
					const float U4 = 0.5f + 0.5f * FMath::Sin(Seed * 4.581f);
					const float AlongOff = (U1 - 0.5f) * SectionLen;
					const float NormOff = (U2 - 0.5f) * Depth * 1.35f;
					const float CardX = X + LX * AlongOff + NX * NormOff;
					const float CardY = Y + LY * AlongOff + NY * NormOff;
					// Biased UP and OUTWARD: the top line and the two faces are what a hedge is
					// judged by, and a card buried in the middle of the core is never seen.
					const float CardZ = H * (0.45f + 0.62f * U3);
					const float CardSize = 0.70f + 0.45f * U4;
					LeafInstances->AddInstance(
						FTransform(
							FRotator((U2 - 0.5f) * 70.0f,
								YawDeg + (U1 - 0.5f) * 180.0f,
								(U3 - 0.5f) * 90.0f),
							FVector(CardX, CardY, CardZ) * HI_METERS_TO_UU,
							FVector(CardSize, CardSize, CardSize)),
						/*bWorldSpace=*/false);
				}
			}
		}
	};
	// A U, OPEN TO THE STREET (2026-10-28, from play: "I want the hedges to form a U around the
	// sides and back of the house, nothing in front"). The two low front runs are GONE. The
	// sides now run the full depth of the plot to the back line and a back run closes the U, so
	// the front elevation reads clear from the street -- which is also what lets the front
	// camera finally see the house it is supposed to be photographing.
	// FIVE METRES CLEAR OF THE HOUSE (Richard, 2026-10-28). The house spans x -6.35..6.35 and
	// y -17.39..-7.75, so the U is x +-11.35, back y -22.39. These are EXACTLY
	// FHISim::PlayerWestLimit/PlayerEastLimit/PlayerSouthLimit -- the boundary the player is
	// clamped to, so she stops at the hedge instead of walking through it.
	// THE LIMITS COME FROM THE SIM, NOT FROM LITERALS HERE, so the boundary the player feels and
	// the hedge they see cannot drift apart. A duplicated number is a boundary you can see and
	// walk through.
	HedgeRun(FHISim::PlayerWestLimit, 4.6f, FHISim::PlayerWestLimit, FHISim::PlayerSouthLimit, 1.70f);
	HedgeRun(FHISim::PlayerEastLimit, 4.6f, FHISim::PlayerEastLimit, FHISim::PlayerSouthLimit, 1.70f);
	HedgeRun(FHISim::PlayerWestLimit, FHISim::PlayerSouthLimit,
		FHISim::PlayerEastLimit, FHISim::PlayerSouthLimit, 1.70f);

	// Nice neighbor houses with 2m hedges
	// KAYKIT BUILDINGS, NOT BOX SHELLS (2026-10-28). Imported CC0 from the Godot project's own
	// asset pack (assets/kaykit/city in home-invasion-godot): building_A..D, one atlas,
	// commercial use allowed. This is the "nice neighbor houses" the Godot version had -- the
	// pack is the same one, so the neighbourhood finally matches the original instead of being
	// grey boxes with a siding tint.
	auto NiceHouse = [&](float CX, float CY, float W, float D, const TCHAR* Model)
	{
		const float HalfW = W * 0.5f;
		// bPerAxis fits the model to the row's footprint and a two-storey height.
		PlaceSketchfabModel(Model, FVector2D(CX, CY), W, D, 0.0f, 7.2f,
			FRotator(0.0f, 180.0f, 0.0f), /*bCollide=*/false, /*bPerAxis=*/true);
		// SAME U AS OUR OWN HOUSE: nothing across the front, sides down the plot and a back run.
		// The neighbour's front faces -Y (the street), so its U opens that way.
		{
			// 9 m clear of the building, BUT NEVER INTO THE ROADWAY (2026-10-28). With the 9 m
			// offset a plot at CY 26.5 put its front run at y 14 -- inside the 6.6..18 street --
			// and the side runs crossed the road as a hedge wall. The clamp pins the run to the
			// plot's own side of the kerb: far-side plots start behind the far sidewalk, our-side
			// plots stop behind our own back hedge.
			float Front = CY - D * 0.5f - 9.0f;
			float Back = CY + D * 0.5f + 9.0f;
			if (CY > 0.0f)
			{
				Front = FMath::Max(Front, 19.6f);      // behind the far sidewalk
			}
			else
			{
				Back = FMath::Min(Back, -28.5f);       // behind our back hedge
			}
			const float SideX = HalfW + 9.0f;
			HedgeRun(CX - SideX, Front, CX - SideX, Back, 1.70f);
			HedgeRun(CX + SideX, Front, CX + SideX, Back, 1.70f);
			HedgeRun(CX - SideX, Back, CX + SideX, Back, 1.70f);
		}
	};
	NiceHouse(-13.0f, 26.5f, 9.0f, 7.0f, TEXT("building_A"));
	NiceHouse(13.0f, 26.5f, 9.0f, 7.0f, TEXT("building_B"));
	NiceHouse(0.0f, 32.0f, 10.0f, 7.0f, TEXT("building_C"));
	NiceHouse(-26.0f, 27.0f, 8.0f, 6.0f, TEXT("building_D"));
	// THE STREET KEEPS GOING (2026-10-28, "scatter more KayKit buildings down the street"). The
	// road runs 260 m each way and had nothing along it past the first five plots. These are the
	// pack's other four shells, alternating between the far side and our own, 16 m apart.
	{
		static const TCHAR* const Extras[] = { TEXT("building_E"), TEXT("building_F"),
			TEXT("building_G"), TEXT("building_H") };
		for (int32 i = 0; i < 4; ++i)
		{
			NiceHouse(-42.0f - 16.0f * i, 26.5f, 10.0f, 8.0f, Extras[i]);
			NiceHouse( 42.0f + 16.0f * i, 26.5f, 10.0f, 8.0f, Extras[(i + 2) % 4]);
			NiceHouse(-42.0f - 16.0f * i, -30.0f, 10.0f, 8.0f, Extras[(i + 1) % 4]);
			NiceHouse( 42.0f + 16.0f * i, -30.0f, 10.0f, 8.0f, Extras[(i + 3) % 4]);
		}
	}
	NiceHouse(26.0f, 27.0f, 8.0f, 6.0f, TEXT("building_A"));

	// Cars and trees. The TREES pass an OpaqueTexture, and that is a fix rather than a choice:
	// without it a tree wore its glb's own material, which has no base-colour texture bound in
	// this build, and rendered CRIMSON -- measured across three separate captures of the same
	// front view (2026-10-23), where the conifer in the front yard is bright red in two of
	// them. It is the same failure the fridge had, fixed the same way: override every slot
	// with the model's own atlas through M_HITextured, which is opaque.
	PlaceSketchfabModel(TEXT("car"), FVector2D(-12.0f, 7.8f), 4.9f, 1.8f, 0.0f, 1.5f, FRotator(0.0f, 90.0f, 0.0f));
	PlaceSketchfabModel(TEXT("pickup"), FVector2D(12.0f, 7.8f), 4.9f, 1.8f, 0.0f, 1.5f, FRotator(0.0f, 90.0f, 0.0f));
	PlaceSketchfabModel(TEXT("car"), FVector2D(24.0f, 7.8f), 4.9f, 1.8f, 0.0f, 1.5f, FRotator(0.0f, 90.0f, 0.0f));
	// TINTED, because the atlas alone is olive (91,84,59) and a conifer in a lawn should read
	// green. The tint multiplies the texture, so it darkens toward evergreen rather than
	// painting over the needle detail.
	const FLinearColor Evergreen(0.42f, 0.72f, 0.34f);
	PlaceSketchfabModel(TEXT("tree2"), FVector2D(-4.5f, 3.5f), 5.0f, 5.0f, 0.0f, 5.0f, FRotator::ZeroRotator,
		false, false, TEXT("tree2_texture_0"), Evergreen);
	PlaceSketchfabModel(TEXT("tree2"), FVector2D(4.5f, 3.5f), 4.5f, 4.5f, 0.0f, 4.5f, FRotator::ZeroRotator,
		false, false, TEXT("tree2_texture_0"), Evergreen);
	// tree1 NEEDED THE SAME OPAQUE OVERRIDE AS tree2 AND THE FRIDGE (2026-10-28). Only the two
	// tree2 instances passed a texture, so both tree1s fell through to the glb's own material,
	// which has no base-colour bound in this build and renders CRIMSON ("the red trees don't
	// look right, they should be green" -- reported from play, and visible in every front
	// frame since 2026-10-23).
	PlaceSketchfabModel(TEXT("tree1"), FVector2D(-15.0f, 22.0f), 6.0f, 6.0f, 0.0f, 6.0f, FRotator::ZeroRotator,
		false, false, TEXT("tree1_texture_0"), Evergreen);
	PlaceSketchfabModel(TEXT("tree1"), FVector2D(15.0f, 22.0f), 6.0f, 6.0f, 0.0f, 6.0f, FRotator::ZeroRotator,
		false, false, TEXT("tree1_texture_0"), Evergreen);

	// Street light and mailbox
	AddBox(VanDetailInstances, FVector(2.6f, 5.9f, 2.6f), FVector(0.14f, 0.14f, 5.2f));
	AddBox(GlassInstances, FVector(2.6f, 5.9f, 5.2f), FVector(0.5f, 0.5f, 0.2f));
	// THE GREEN BOX AT THE DOOR WAS THE MAILBOX (Richard, 2026-10-28: "there is a green
	// rectangular box outside at the entrance... I suspect it's something else not rendered
	// properly"). It was exactly that: one 0.3 x 0.5 x 1.0 m block in the van's green, standing
	// on the driveway like a pillar. A mailbox is a post, a box and a flag.
	AddBox(VanDetailInstances, FVector(1.70f, -6.90f, 0.55f), FVector(0.07f, 0.07f, 1.10f)); // post
	AddBox(VanInstances, FVector(1.70f, -6.82f, 1.18f), FVector(0.26f, 0.46f, 0.24f));       // box
	AddBox(TrimInstances, FVector(1.84f, -6.86f, 1.30f), FVector(0.02f, 0.10f, 0.16f));      // flag
}