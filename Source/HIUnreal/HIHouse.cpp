#include "HIHouse.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "HIMaterials.h"
#include "HIUnits.h"
#include "UObject/ConstructorHelpers.h"

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
	//
	// EVERY POSITION IS FLUSH AGAINST A WALL FACE, and the arithmetic is written out so it
	// can be checked rather than trusted. The wall list is `FHISim::Walls()`:
	//
	//   north      y -19.000 .. -18.750, x -8..8      inner face y = -18.750
	//   south-west y  -8.250 ..  -8.000, x -8..-1     inner face y =  -8.250
	//   south-east y  -8.250 ..  -8.000, x  1..8      inner face y =  -8.250
	//   west       x  -8.000 ..  -7.750, y -19..-8    inner face x =  -7.750
	//   east       x   7.750 ..   8.000, y -19..-8    inner face x =   7.750
	//   divider W  y -14.125 .. -13.875, x -8..-2     faces y = -14.125 and y = -13.875
	//   divider E  y -14.125 .. -13.875, x  2..8      faces y = -14.125 and y = -13.875
	//
	// A fitting's centre is the face plus half its own depth, so its back lands ON the wall.
	// Two of these used to sit in the hall opening (x -2..2) and one inside `DoorGap`
	// (x -1..1); those are the three that hung in mid-air. Sizes X and Y swap on the side
	// walls, because the wall is thin in the other axis there.
	static const TArray<FFitting> F = {
		// ---- sockets, just above the floor
		{ -1.20f, -18.720f, 0.30f, 0.09f, 0.06f, 0.13f },   // bedroom, north wall
		{  1.20f, -18.720f, 0.30f, 0.09f, 0.06f, 0.13f },   // study, north wall
		{  7.720f, -15.62f, 0.30f, 0.06f, 0.09f, 0.13f },   // kitchen, east wall
		{ -7.720f, -11.00f, 0.30f, 0.06f, 0.09f, 0.13f },   // living room, west wall
		// ---- switches, at hand height
		{ -2.35f, -14.155f, 1.25f, 0.09f, 0.06f, 0.09f },   // bedroom, beside the hall doorway
		{ -2.35f, -13.845f, 1.25f, 0.09f, 0.06f, 0.09f },   // living room, same stub, other side
		{  3.00f, -13.845f, 1.25f, 0.09f, 0.06f, 0.09f },   // kitchen, divider
		{  6.35f, -13.845f, 1.25f, 0.09f, 0.06f, 0.09f },   // kitchen, by the counter
		{ -1.45f,  -8.280f, 1.25f, 0.09f, 0.06f, 0.09f },   // by the front door, west of the gap
		// ---- thermostat
		{ -2.60f, -13.850f, 1.50f, 0.14f, 0.05f, 0.14f },   // living room, divider
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
	RoofInstance = MakeDecor(TEXT("Roof"), Cube, WallInstances);
	// The roof is the one decorative component that must still cast a shadow: it exists to
	// keep the sun out of the rooms, and a roof that did not shadow would be a roof that did
	// nothing at all.
	RoofInstance->SetCastShadow(true);
	CeilingInstance = MakeDecor(TEXT("Ceiling"), Cube, WallInstances);
	TrimInstances = MakeDecor(TEXT("Trim"), Cube, WallInstances);
	WindowInstances = MakeDecor(TEXT("Windows"), Cube, WallInstances);
	GlassInstances = MakeDecor(TEXT("Glass"), Cube, WallInstances);
	CoatInstances = MakeDecor(TEXT("Coat"), Cube, WallInstances);
	FittingInstances = MakeDecor(TEXT("Fittings"), Cube, WallInstances);
	PropInstances = MakeDecor(TEXT("Props"), Cube, WallInstances);
	DoorHeadInstances = MakeDecor(TEXT("DoorHead"), Cube, WallInstances);
	VanInstances = MakeDecor(TEXT("Van"), Cube, WallInstances);
	VanDetailInstances = MakeDecor(TEXT("VanDetail"), Cube, WallInstances);
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

void AHIHouse::AddWindow(bool bAlongX, float Along, float WallCoord, float Facing, float Width)
{
	// A window is a hole with a frame round it and something in the hole.
	//
	// The glazing WAS a pale opaque panel, because the port's notes said a translucent
	// material is a .uasset and a .uasset needs an editor session. That is not true any more
	// -- see `tools/make-assets.py` -- so the pane is now real glass from
	// `/Game/HIGenerated/M_HIGlass` in its own component, and the frame, mullion and sill
	// stay opaque trim. The original's own note ("the glazing is still a pale opaque panel
	// from outside") applied to Godot; this closes it here.
	const float Sill = 0.95f;
	const float Head = 2.15f;
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
	// The sill, standing proud of the wall below the opening.
	const FVector SillCentre = bAlongX
		? FVector(Along, Out + Facing * 0.06f, Sill - 0.03f)
		: FVector(Out + Facing * 0.06f, Along, Sill - 0.03f);
	const FVector SillSize = bAlongX
		? FVector(Width + 0.30f, 0.22f, 0.06f)
		: FVector(0.22f, Width + 0.30f, 0.06f);
	AddBox(WindowInstances, SillCentre, SillSize);
}

void AHIHouse::BuildDressing()
{
	const FBox2D H = FHISim::House;

	// ---- the lawn, and the interior floor 2 cm proud of it. See the header for why.
	{
		// The lawn is DELIBERATELY FAR LARGER than the sim's play area. A 60 x 52 m plane
		// ends in mid-air 30 m from the house, and the first render of this port showed a
		// black band along the horizon where the ground stopped and the sky atmosphere had
		// nothing to draw below itself. Size here is pure scenery: the sim still clamps every
		// body to `FHISim::Bounds`, and this component has no collision, so nothing about
		// where a player may walk changed.
		const float LawnHalf = 2600.0f;
		AddBox(GroundInstance, FVector(0.0f, 0.0f, -0.25f), FVector(LawnHalf * 2.0f, LawnHalf * 2.0f, 0.5f));

		const FVector2D HSize = H.GetSize();
		// Top face at +0.02 m: the lawn's top is at 0.0, so a 0.4 m slab centred at -0.18
		// lands its top at +0.02. Deliberately under a shoe and far over a depth epsilon.
		AddBox(FloorInstance,
			FVector((H.Min.X + H.Max.X) * 0.5f, (H.Min.Y + H.Max.Y) * 0.5f, -0.18f),
			FVector(HSize.X, HSize.Y, 0.4f));
	}

	// ---- THE ROOF AND ITS CEILINGS, SPLIT AT THE STOREY DIVIDER (the fork's slice).
	// The rear wing (y -19..-14) is single-storey: its lid stays at 3.0 exactly as it was.
	// The front half (y -14..-8) is two storeys: its lid sits on the second-floor wall
	// tops at 5.75, matching the sim's own roof decks in WorldSolids. One flat roof over
	// both halves was the single-storey house; a drawn house that disagrees with the
	// house that stops bullets is a bug, not a style.
	{
		const FVector2D HSize = H.GetSize();
		const float Wall = FHISim::WallHeight;
		const float IZ = FHISim::InteriorZ;
		const float CXm = (H.Min.X + H.Max.X) * 0.5f;

		// Rear wing: ceiling under the wall top, roof slab, overhang eave.
		const float RearCY = (H.Min.Y + IZ) * 0.5f;
		const float RearD = IZ - H.Min.Y;
		AddBox(CeilingInstance, FVector(CXm, RearCY, Wall + 0.03f), FVector(HSize.X, RearD, 0.06f));
		AddBox(RoofInstance, FVector(CXm, RearCY, Wall + 0.26f), FVector(HSize.X, RearD, 0.32f));
		AddBox(RoofInstance, FVector(CXm, RearCY, Wall + 0.50f), FVector(HSize.X + 0.9f, RearD + 0.9f, 0.14f));

		// Front, two storeys: roof slab on the second-floor wall tops, same eaves, and a
		// ceiling panel under it at 5.75.
		const float Top = FHISim::FloorHeight(1) + 2.75f;
		const float FrontCY = (IZ + H.Max.Y) * 0.5f;
		const float FrontD = H.Max.Y - IZ;
		AddBox(RoofInstance, FVector(CXm, FrontCY, Top + 0.26f), FVector(HSize.X, FrontD, 0.32f));
		AddBox(RoofInstance, FVector(CXm, FrontCY, Top + 0.50f), FVector(HSize.X + 0.9f, FrontD + 0.9f, 0.14f));
		AddBox(CeilingInstance, FVector(CXm, FrontCY, Top - 0.03f), FVector(HSize.X, FrontD, 0.06f));

		// The ground floor's front ceiling IS the slab's underside: a 6 cm panel under
		// each deck rectangle, taken from the sim's own list so the stairwell hole exists
		// by construction -- from the ground floor you can look up through the hole and
		// see the second floor, which is the whole point of the slice.
		for (const FHISim::FHISolid& S : FHISim::WorldSolids())
		{
			if (S.bDeck && S.Span.MinZ < FHISim::FloorHeight(1)
				&& FMath::IsNearlyEqual(S.Span.MaxZ, FHISim::FloorHeight(1), 0.01f))
			{
				AddBox(CeilingInstance,
					FVector((S.Rect.Min.X + S.Rect.Max.X) * 0.5f,
						(S.Rect.Min.Y + S.Rect.Max.Y) * 0.5f,
						S.Span.MinZ - 0.03f),
					FVector(S.Rect.GetSize().X, S.Rect.GetSize().Y, 0.06f));
			}
		}
	}

	// ---- the HEAD of the front doorway, and the round starts facing it.
	//
	// `Walls()` splits the south wall either side of `DoorGap`, which is a horizontal split
	// because the sim is a 2-D model with a single vertical span -- so the opening ran from
	// the floor to the CEILING and the front of the house had a 3 m letterbox in it. Godot
	// fills exactly this in `build_exterior` ("the doorway is a hole all the way to the
	// ceiling, which a 3 m wall makes glaring"); the port never did. Measured on a capture.
	//
	// The two numbers are the original's: `DOOR_TOP` is 2.10 and the head runs to the wall
	// top. It is a DECORATION, so it is drawn from `DoorHeadInstances` -- no collision, and
	// no entry in `Walls()` -- which is what the original's plain MeshInstance3D amounts to.
	{
		const float HeadBottom = 2.10f;
		AddBox(DoorHeadInstances,
			FVector((FHISim::DoorGap.X + FHISim::DoorGap.Y) * 0.5f,
				H.Max.Y - FHISim::WallThickness * 0.5f,
				HeadBottom + (FHISim::WallHeight - HeadBottom) * 0.5f),
			FVector(FHISim::DoorGap.Y - FHISim::DoorGap.X,
				FHISim::WallThickness,
				FHISim::WallHeight - HeadBottom));
	}

	// ---- skirting and picture rail, DERIVED from the wall list rather than hand-placed.
	// Every wall in the sim is an axis-aligned box, so a band at its base and another near
	// its top can be generated exactly, for all seven, with no positions to get wrong.
	for (const FBox2D& W : FHISim::Walls())
	{
		const FVector2D Size = W.GetSize();
		// Only the two long exterior faces get trim; a 0.25 m wall stub with skirting on all
		// four sides is a plinth, not a skirting board.
		const float Girth = 0.035f;
		AddBox(TrimInstances,
			FVector((W.Min.X + W.Max.X) * 0.5f, (W.Min.Y + W.Max.Y) * 0.5f, 0.06f),
			FVector(Size.X + Girth * 2.0f, Size.Y + Girth * 2.0f, 0.12f));
		AddBox(TrimInstances,
			FVector((W.Min.X + W.Max.X) * 0.5f, (W.Min.Y + W.Max.Y) * 0.5f, 2.06f),
			FVector(Size.X + Girth * 2.0f, Size.Y + Girth * 2.0f, 0.07f));
	}

	// ---- windows. Placed on the four exterior walls, clear of the door gap at x -1..1.
	// `AddWindow(bAlongX, along, wallCoord, facing, width)`.
	for (float X : { -4.60f, 4.60f })
	{
		AddWindow(true, X, H.Min.Y, -1.0f, 1.70f);   // north wall, facing the back garden
	}
	for (float X : { -5.00f, 5.00f })
	{
		AddWindow(true, X, H.Max.Y, 1.0f, 1.50f);    // south wall, either side of the door
	}
	for (float Y : { -11.00f, -16.60f })
	{
		AddWindow(false, Y, H.Min.X, -1.0f, 1.60f);  // west wall
		AddWindow(false, Y, H.Max.X, 1.0f, 1.60f);   // east wall
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
	// A smoke alarm on the hall ceiling. Not a wall fitting -- it is on the ceiling, and it
	// belongs in the hall opening, which is where a smoke alarm goes.
	AddBox(FittingInstances, FVector(0.00f, -14.00f, FHISim::WallHeight - 0.06f), FVector(0.16f, 0.16f, 0.07f));

	// Light fittings: a shallow rose under the ceiling at each lamp, so the lamp in the game
	// mode is a light that is visibly COMING FROM something.
	// The fork's slice moved some of the ceilings: the front half's is now the slab's
	// underside at 2.75, so a rose at 2.93 would be buried in the deck. The rear keeps
	// its 3.0 ceiling, the hall rose steps just south of the slab's edge at -14, and the
	// second floor gets a rose of its own under the 5.75 lid.
	static const FVector Roses[] =
	{
		FVector(-4.00f, -16.50f, FHISim::WallHeight - 0.07f),   // bedroom (rear wing)
		FVector( 4.00f, -16.50f, FHISim::WallHeight - 0.07f),   // study (rear wing)
		FVector(-5.00f, -11.00f, 2.72f),                        // living room, under the slab
		FVector( 4.00f, -11.00f, 2.72f),                        // kitchen, under the slab
		FVector( 0.00f, -13.90f, 2.72f),                        // hall, south of the slab edge
		FVector( 0.00f, -12.20f, 5.66f),                        // upstairs
	};
	for (const FVector& R : Roses)
	{
		AddBox(FittingInstances, R, FVector(0.30f, 0.30f, 0.06f));
	}

	// ---- fix-list items 10 and 11: the small things a house actually contains.
	// A kettle and a toaster on the kitchen counter, a coat on a hook by the door, keys and
	// a phone on the side table, and a picture on the living-room wall.
	AddBox(PropInstances, FVector(7.20f, -15.60f, 1.05f), FVector(0.22f, 0.22f, 0.26f));  // kettle
	AddBox(PropInstances, FVector(7.20f, -14.90f, 0.98f), FVector(0.28f, 0.18f, 0.16f));  // toaster
	AddBox(PropInstances, FVector(7.20f, -16.30f, 0.95f), FVector(0.30f, 0.24f, 0.12f));  // bread bin
	// THE COAT IS ON THE WALL BESIDE THE DOOR, and it used to be IN the doorway. `DoorGap` is
	// x -1..1, so the coat at x -0.80 with a width of 0.44 spanned -1.02..-0.58 -- the whole
	// of it inside the opening -- and a pale slab hanging in the one hole in the house read
	// as a bug rather than as a coat. Measured on a capture; the wall starts at x = -1, so it
	// needs to be clear of that edge. See fix-list items 10 and 11.
	AddBox(CoatInstances, FVector(-1.80f, -8.35f, 1.75f), FVector(0.10f, 0.10f, 0.10f));  // coat hook
	AddBox(CoatInstances, FVector(-1.80f, -8.45f, 1.28f), FVector(0.44f, 0.22f, 0.86f));  // coat
	AddBox(PropInstances, FVector(-6.86f, -13.95f, 0.62f), FVector(0.26f, 0.20f, 0.04f)); // keys on the table
	AddBox(PropInstances, FVector(-6.62f, -14.10f, 0.62f), FVector(0.09f, 0.16f, 0.02f)); // phone
	AddBox(PropInstances, FVector(-7.85f, -11.00f, 1.60f), FVector(0.05f, 0.90f, 0.68f)); // a picture
	AddBox(PropInstances, FVector(5.60f, -16.30f, 0.78f), FVector(0.50f, 0.36f, 0.06f));  // desk mat
	AddBox(PropInstances, FVector(6.20f, -18.20f, 0.78f), FVector(0.22f, 0.30f, 0.30f));  // a mug-ish shape

	// ---- the van. Its rectangle is the sim's, so the thing the intruders deliver to and
	// the thing the player can see are the same object.
	{
		const FBox2D& V = FHISim::Van;
		const FVector2D Size = V.GetSize();
		const float CX = (V.Min.X + V.Max.X) * 0.5f;
		const float CY = (V.Min.Y + V.Max.Y) * 0.5f;
		// Body, then a raised cab, so it reads as a van and not a shipping container. The
		// sim's van rectangle is 5.5 x 2.4 m, which is a real van's footprint.
		// Seated LOW, on its wheels, rather than floating above them: the first version had
		// a 0.4 m gap under the body, which is what made the whole thing read as a table.
		AddBox(VanInstances, FVector(CX, CY, 1.05f), FVector(Size.X, Size.Y, 1.45f));
		AddBox(VanInstances, FVector(CX, CY - 1.55f, 0.92f), FVector(Size.X * 0.92f, 1.30f, 1.05f));
		// A slightly proud roof line, so the body has a top edge rather than ending flat.
		AddBox(VanInstances, FVector(CX, CY, 1.82f), FVector(Size.X * 0.98f, Size.Y * 0.96f, 0.12f));
		// The windscreen and the two flank glazing strips, as slightly proud slabs. Routed
		// into the WINDOW component rather than the body's: glass is glass, and giving it its
		// own colour is the entire difference between a van and a green box.
		AddBox(GlassInstances, FVector(CX, CY - 2.16f, 1.30f), FVector(Size.X * 0.78f, 0.06f, 0.60f));
		AddBox(GlassInstances, FVector(CX - Size.X * 0.5f - 0.02f, CY - 1.55f, 1.30f), FVector(0.06f, 1.00f, 0.50f));
		AddBox(GlassInstances, FVector(CX + Size.X * 0.5f + 0.02f, CY - 1.55f, 1.30f), FVector(0.06f, 1.00f, 0.50f));
		// Four wheels, standing proud of the body on both sides, and DARK. See the header:
		// green wheels on a green body is a table.
		for (float SX : { -1.0f, 1.0f })
		{
			for (float SY : { -1.55f, 1.55f })
			{
				AddBox(VanDetailInstances,
					FVector(CX + SX * (Size.X * 0.5f + 0.08f), CY + SY, 0.34f),
					FVector(0.22f, 0.68f, 0.68f));
			}
		}
	}
}

void AHIHouse::Build()
{
	if (WallInstances == nullptr || FurnitureInstances == nullptr)
	{
		return;
	}
	WallInstances->ClearInstances();
	FurnitureInstances->ClearInstances();
	for (UInstancedStaticMeshComponent* Comp : { GroundInstance, FloorInstance, RoofInstance,
		TrimInstances, WindowInstances, GlassInstances, FittingInstances, PropInstances,
		CoatInstances, DoorHeadInstances, VanInstances, VanDetailInstances })
	{
		if (Comp != nullptr)
		{
			Comp->ClearInstances();
		}
	}

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

	// The sim's own furniture labels and HEIGHTS, both index-aligned with `FurnitureRects`.
	// The labels are carried only so a problem can name a row; the heights are what the
	// drawing needs, and they come straight out of `sim.gd` rather than from anything
	// invented here. `game.gd` draws with the same number: `_add_box(body, f["rect"], f["h"])`.
	const TArray<FString>& AllLabels = FHISim::FurnitureLabels();
	const TArray<float>& AllHeights = FHISim::FurnitureHeights();
	const TArray<bool>& Flat = FHISim::FurnitureFlat();
	if (AllHeights.Num() != AllLabels.Num() || Flat.Num() != AllLabels.Num())
	{
		return;
	}

	// The non-flat rows are the ones that appear in `Solids` after the walls, in the same
	// order, so they can be indexed directly. Checked rather than assumed: a list out of step
	// would give the sofa the fridge's height, which would look merely odd rather than wrong.
	TArray<float> SolidHeights;
	SolidHeights.Reserve(AllHeights.Num());
	for (int32 Idx = 0; Idx < AllHeights.Num(); ++Idx)
	{
		if (!Flat[Idx])
		{
			SolidHeights.Add(AllHeights[Idx]);
		}
	}
	if (SolidHeights.Num() != Solids.Num() - WallCount)
	{
		return;
	}

	// THE FORK'S SLICE: walls are drawn from WallRows(), not from the ground-floor
	// projection. Each row carries its own Floor and optional TopZ, so the first-floor
	// walls stand on FloorHeight(1) and the stringer board stops at its 2.70 header --
	// the drawn house and the house that stops a bullet are the same house. The legacy
	// Solids()/SolidSpans() pair stays the furniture's index alignment.
	for (const FHISim::FHIWallRow& Row : FHISim::WallRows())
	{
		const float MinZ = FHISim::FloorHeight(Row.Floor);
		const float MaxZ = Row.TopZ > 0.0f ? Row.TopZ : MinZ + FHISim::WallHeight;
		WallInstances->AddInstance(BoxTransform(Row.Rect, MinZ, MaxZ), /*bWorldSpace=*/false);
	}
	for (int32 Idx = WallCount; Idx < Solids.Num(); ++Idx)
	{
		// THE HEIGHT IS THE ITEM'S OWN, not the sight model's. `SolidSpans` spans a full
		// storey on purpose -- it answers "can he see past it", not "how tall is it" --
		// and using it as geometry is what drew every couch, table and counter as a
		// column from floor to ceiling. The mismatch that remains -- you cannot see over
		// a sofa you can see the top of -- is Godot's too, because its `los_blocked` is
		// 2-D and ignores `h`. That is fix-list item 18.
		FurnitureInstances->AddInstance(
			BoxTransform(Solids[Idx], Spans[Idx].MinZ, SolidHeights[Idx - WallCount]),
			/*bWorldSpace=*/false);
	}

	// ---- the slab, the railings and the treads, each from the sim's own lists.
	for (const FHISim::FHISolid& S : FHISim::WorldSolids())
	{
		if (S.bDeck)
		{
			// The storey slab is the deck whose top IS the first floor. Both roof decks
			// are decks too, and this span test skips them: they sit on wall tops and the
			// roof component draws them.
			if (S.Span.MinZ < FHISim::FloorHeight(1)
				&& FMath::IsNearlyEqual(S.Span.MaxZ, FHISim::FloorHeight(1), 0.01f))
			{
				SlabInstances->AddInstance(BoxTransform(S.Rect, S.Span.MinZ, S.Span.MaxZ), /*bWorldSpace=*/false);
			}
			continue;
		}
		// Railings: the floor-1, metre-tall class standing on the deck's top face -- the
		// same test ResolveCircleZ's stair skip uses, so what a climber may pass and what
		// is drawn cannot disagree.
		if (S.Floor == 1 && FMath::IsNearlyEqual(S.Span.MinZ, FHISim::FloorHeight(1), 0.01f)
			&& S.Span.MaxZ <= FHISim::FloorHeight(1) + 1.05f)
		{
			RailingInstances->AddInstance(BoxTransform(S.Rect, S.Span.MinZ, S.Span.MaxZ), /*bWorldSpace=*/false);
		}
	}
	// Treads: the sim walks the band's interpolated Z; the treads are the visible steps
	// under it -- sixteen per flight (HOUSE-DESIGN.md section 3), each a 5 cm slab with
	// OPEN risers, so the stairwell's sight tube stays honest: what the sim's exact span
	// clipping says is visible through the hole is also visible past the drawn stair.
	for (const FHISim::FHIStairBand& B : FHISim::Stairs())
	{
		const int32 Risers = 16;
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
			StairInstances->AddInstance(BoxTransform(Plan, TopZ - 0.05f, TopZ), /*bWorldSpace=*/false);
		}
	}

	// ---- the RUGS, which had never been drawn at all.
	//
	// `Solids()` is walls plus NON-FLAT furniture on purpose -- a rug that blocks a man is
	// absurd -- so a rug is not in the list above and was invisible. It is drawn here from
	// `FurnitureRects` and its own `h` of 2 cm, so it comes from the sim's numbers rather
	// than from a second hand-written copy.
	for (int32 Idx = 0; Idx < AllLabels.Num(); ++Idx)
	{
		if (!Flat[Idx])
		{
			continue;
		}
		FurnitureInstances->AddInstance(
			BoxTransform(FHISim::FurnitureRects()[Idx], 0.0f, AllHeights[Idx]),
			/*bWorldSpace=*/false);
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
	FHIMaterials::Tint(WallInstances, FHIMaterials::Wall);
	if (FHIMaterials::Textured(WallInstances, TEXT("ceiling_plaster"), 40.0f) == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("HI: walls stay flat -- no M_HITextured or texture"));
	}
	FHIMaterials::Tint(FloorInstance, FHIMaterials::Floor);
	FHIMaterials::Textured(FloorInstance, TEXT("floor_wood"), 6.0f);
	FHIMaterials::Tint(GroundInstance, FHIMaterials::Grass);
	// The lawn is a 5 km plane, so its tiling is large: one repeat every ~13 m reads as grass
	// from the doorstep without turning to noise at the horizon.
	FHIMaterials::Textured(GroundInstance, TEXT("lawn"), 400.0f);
	FHIMaterials::Tint(RoofInstance, FHIMaterials::WallExterior);
	FHIMaterials::Textured(RoofInstance, TEXT("roof_shingle"), 8.0f);
	FHIMaterials::Tint(FurnitureInstances, FHIMaterials::Wood);
	// The slice's three: the slab is walked on upstairs, so it wears the floor's wood;
	// treads are bare wood; railings match the trim.
	FHIMaterials::Tint(SlabInstances, FHIMaterials::Floor);
	FHIMaterials::Textured(SlabInstances, TEXT("floor_wood"), 6.0f);
	FHIMaterials::Tint(StairInstances, FHIMaterials::Wood);
	FHIMaterials::Tint(RailingInstances, FHIMaterials::Trim);
	FHIMaterials::Tint(GroundInstance, FHIMaterials::Grass);
	FHIMaterials::Tint(FloorInstance, FHIMaterials::Floor);
	FHIMaterials::Tint(RoofInstance, FHIMaterials::WallExterior);
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
			Mid->SetScalarParameterValue(TEXT("Opacity"), 0.22f);
			Mid->SetVectorParameterValue(TEXT("GlassColor"), FLinearColor(0.42f, 0.52f, 0.58f, 1.0f));
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
	FHIMaterials::Tint(PropInstances, FHIMaterials::Counter);
	// The door head is WALL, not trim: it is a piece of wall that was missing, and a white
	// lintel against a cream wall would read as a mistake rather than as masonry.
	//
	// AND IT NEEDS THE WALL'S TEXTURE, which the first textured capture made obvious: a flat
	// 2 m panel of untextured masonry above a textured wall reads as a patch of missing paint
	// from the front lawn. Its tiling is the wall's scaled by the ratio of their widths
	// (2 m against the 16 m wall run), so the grain matches instead of merely approximating.
	FHIMaterials::Tint(DoorHeadInstances, FHIMaterials::Wall);
	FHIMaterials::Textured(DoorHeadInstances, TEXT("ceiling_plaster"), 40.0f * (2.0f / 16.0f));
	FHIMaterials::Tint(VanInstances, FHIMaterials::Van);
	FHIMaterials::Tint(VanDetailInstances, FHIMaterials::Tyre);

	BuildDressing();

	// WHAT WAS ACTUALLY BUILT, counted. The first render of this port showed what looked like
	// a house but could equally have been one wall seen edge-on, and a screenshot cannot tell
	// those apart. These counts can: 7 walls, 19 furniture solids, and every decorative
	// component non-zero, or something in this function silently did not run.
	UE_LOG(LogTemp, Display,
		TEXT("HI: house built -- walls=%d furniture=%d ground=%d floor=%d roof=%d ceiling=%d trim=%d frames=%d glass=%d fittings=%d props=%d coat=%d door_head=%d van=%d van_detail=%d slabs=%d stairs=%d railings=%d"),
		WallInstances->GetInstanceCount(), FurnitureInstances->GetInstanceCount(),
		GroundInstance->GetInstanceCount(), FloorInstance->GetInstanceCount(),
		RoofInstance->GetInstanceCount(), CeilingInstance->GetInstanceCount(),
		TrimInstances->GetInstanceCount(),
		WindowInstances->GetInstanceCount(), GlassInstances->GetInstanceCount(),
		FittingInstances->GetInstanceCount(),
		PropInstances->GetInstanceCount(), CoatInstances->GetInstanceCount(),
		DoorHeadInstances->GetInstanceCount(),
		VanInstances->GetInstanceCount(),
		VanDetailInstances->GetInstanceCount(),
		SlabInstances->GetInstanceCount(),
		StairInstances->GetInstanceCount(),
		RailingInstances->GetInstanceCount());
	UE_LOG(LogTemp, Display, TEXT("HI: shape material %s"),
		FHIMaterials::ShapeMaterial() != nullptr ? TEXT("loaded") : TEXT("MISSING -- everything will be default grey"));
}
