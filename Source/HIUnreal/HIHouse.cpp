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
	FurnitureInstances->SetMobility(EComponentMobility::Static);

	// ---- everything below is decoration, and none of it collides. See the header.
	GroundInstance = MakeDecor(TEXT("Ground"), Cube, WallInstances);
	FloorInstance = MakeDecor(TEXT("InteriorFloor"), Cube, WallInstances);
	RoofInstance = MakeDecor(TEXT("Roof"), Cube, WallInstances);
	// The roof is the one decorative component that must still cast a shadow: it exists to
	// keep the sun out of the rooms, and a roof that did not shadow would be a roof that did
	// nothing at all.
	RoofInstance->SetCastShadow(true);
	CeilingInstance = MakeDecor(TEXT("Ceiling"), Cube, WallInstances);
	TrimInstances = MakeDecor(TEXT("Trim"), Cube, WallInstances);
	WindowInstances = MakeDecor(TEXT("Windows"), Cube, WallInstances);
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

void AHIHouse::AddWindow(bool bAlongX, float Along, float WallCoord, float Facing, float Width){
	// A window is a hole with a frame round it and something in the hole. The glazing is a
	// PALE BLUE PANEL rather than real glass, because a translucent material is a .uasset
	// and this project has none -- which is exactly the compromise the Godot original is
	// still carrying ("the glazing is still a pale opaque panel from outside"). What is
	// fixed here is the part that was actually missing: there were no windows at all.
	const float Sill = 0.95f;
	const float Head = 2.15f;
	const float MidZ = (Sill + Head) * 0.5f;
	const float Height = Head - Sill;
	// Proud of the wall face by 15 cm, on the outward side, so the frame reads as a frame
	// rather than being buried inside the wall it is cut into.
	const float Out = WallCoord + Facing * 0.15f;
	const float FrameT = 0.06f;

	auto Place = [&](float AlongOffset, float HeightOffset, float SizeAlong, float SizeZ)
	{
		const FVector Centre = bAlongX
			? FVector(Along + AlongOffset, Out, MidZ + HeightOffset)
			: FVector(Out, Along + AlongOffset, MidZ + HeightOffset);
		const FVector Size = bAlongX
			? FVector(SizeAlong, 0.08f, SizeZ)
			: FVector(0.08f, SizeAlong, SizeZ);
		AddBox(WindowInstances, Centre, Size);
	};

	// The glazing, then the four sides of the frame around it.
	Place(0.0f, 0.0f, Width, Height);
	Place(0.0f, (Height * 0.5f) + (FrameT * 0.5f), Width + FrameT * 2.0f, FrameT);
	Place(0.0f, -(Height * 0.5f) - (FrameT * 0.5f), Width + FrameT * 2.0f, FrameT);
	Place(-(Width * 0.5f) - (FrameT * 0.5f), 0.0f, FrameT, Height);
	Place((Width * 0.5f) + (FrameT * 0.5f), 0.0f, FrameT, Height);
	// A central mullion, because one big pane reads as a hole and two reads as a window.
	Place(0.0f, 0.0f, FrameT * 0.8f, Height);
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

	// ---- the roof and its ceiling. The ceiling underside sits just under the wall top so
	// the rooms are closed; the roof slab above it gives the outside a pitched-looking mass.
	{
		const FVector2D HSize = H.GetSize();
		const float Wall = FHISim::WallHeight;
		const float CXm = (H.Min.X + H.Max.X) * 0.5f;
		const float CYm = (H.Min.Y + H.Max.Y) * 0.5f;
		// The ceiling: its underside at the wall top, so the rooms are closed and lit from
		// the lamps below rather than from the sun above.
		AddBox(CeilingInstance, FVector(CXm, CYm, Wall + 0.03f), FVector(HSize.X, HSize.Y, 0.06f));
		// The roof above it, and an eave overhanging the walls so the roof does not read as
		// a lid dropped on a box.
		AddBox(RoofInstance, FVector(CXm, CYm, Wall + 0.26f), FVector(HSize.X, HSize.Y, 0.32f));
		AddBox(RoofInstance, FVector(CXm, CYm, Wall + 0.50f), FVector(HSize.X + 0.9f, HSize.Y + 0.9f, 0.14f));
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
	static const FVector Roses[] =
	{
		FVector(-5.00f, -11.00f, FHISim::WallHeight - 0.07f),
		FVector( 4.00f, -11.00f, FHISim::WallHeight - 0.07f),
		FVector(-4.00f, -16.50f, FHISim::WallHeight - 0.07f),
		FVector( 4.00f, -16.50f, FHISim::WallHeight - 0.07f),
		FVector( 0.00f, -14.00f, FHISim::WallHeight - 0.07f),
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
	AddBox(PropInstances, FVector(-1.80f, -8.35f, 1.75f), FVector(0.10f, 0.10f, 0.10f));  // coat hook
	AddBox(PropInstances, FVector(-1.80f, -8.45f, 1.28f), FVector(0.44f, 0.22f, 0.86f));  // coat
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
		AddBox(WindowInstances, FVector(CX, CY - 2.16f, 1.30f), FVector(Size.X * 0.78f, 0.06f, 0.60f));
		AddBox(WindowInstances, FVector(CX - Size.X * 0.5f - 0.02f, CY - 1.55f, 1.30f), FVector(0.06f, 1.00f, 0.50f));
		AddBox(WindowInstances, FVector(CX + Size.X * 0.5f + 0.02f, CY - 1.55f, 1.30f), FVector(0.06f, 1.00f, 0.50f));
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
		TrimInstances, WindowInstances, FittingInstances, PropInstances, DoorHeadInstances,
		VanInstances, VanDetailInstances })
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

	for (int32 Idx = 0; Idx < Solids.Num(); ++Idx)
	{
		if (Idx < WallCount)
		{
			WallInstances->AddInstance(
				BoxTransform(Solids[Idx], Spans[Idx].MinZ, Spans[Idx].MaxZ), /*bWorldSpace=*/false);
		}
		else
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
	FHIMaterials::Tint(WallInstances, FHIMaterials::Wall);
	FHIMaterials::Tint(FurnitureInstances, FHIMaterials::Wood);
	FHIMaterials::Tint(GroundInstance, FHIMaterials::Grass);
	FHIMaterials::Tint(FloorInstance, FHIMaterials::Floor);
	FHIMaterials::Tint(RoofInstance, FHIMaterials::WallExterior);
	FHIMaterials::Tint(CeilingInstance, FHIMaterials::Ceiling);
	FHIMaterials::Tint(TrimInstances, FHIMaterials::Trim);
	FHIMaterials::Tint(WindowInstances, FHIMaterials::Glass);
	FHIMaterials::Tint(FittingInstances, FHIMaterials::Trim);
	FHIMaterials::Tint(PropInstances, FHIMaterials::Counter);
	// The door head is WALL, not trim: it is a piece of wall that was missing, and a white
	// lintel against a cream wall would read as a mistake rather than as masonry.
	FHIMaterials::Tint(DoorHeadInstances, FHIMaterials::Wall);
	FHIMaterials::Tint(VanInstances, FHIMaterials::Van);
	FHIMaterials::Tint(VanDetailInstances, FHIMaterials::Tyre);

	BuildDressing();

	// WHAT WAS ACTUALLY BUILT, counted. The first render of this port showed what looked like
	// a house but could equally have been one wall seen edge-on, and a screenshot cannot tell
	// those apart. These counts can: 7 walls, 19 furniture solids, and every decorative
	// component non-zero, or something in this function silently did not run.
	UE_LOG(LogTemp, Display,
		TEXT("HI: house built -- walls=%d furniture=%d ground=%d floor=%d roof=%d ceiling=%d trim=%d glass=%d fittings=%d props=%d door_head=%d van=%d van_detail=%d"),
		WallInstances->GetInstanceCount(), FurnitureInstances->GetInstanceCount(),
		GroundInstance->GetInstanceCount(), FloorInstance->GetInstanceCount(),
		RoofInstance->GetInstanceCount(), CeilingInstance->GetInstanceCount(),
		TrimInstances->GetInstanceCount(),
		WindowInstances->GetInstanceCount(), FittingInstances->GetInstanceCount(),
		PropInstances->GetInstanceCount(), DoorHeadInstances->GetInstanceCount(),
		VanInstances->GetInstanceCount(),
		VanDetailInstances->GetInstanceCount());
	UE_LOG(LogTemp, Display, TEXT("HI: shape material %s"),
		FHIMaterials::ShapeMaterial() != nullptr ? TEXT("loaded") : TEXT("MISSING -- everything will be default grey"));
}
