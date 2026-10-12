#include "HILootProp.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "HIMaterials.h"
#include "HIUnits.h"

namespace
{
	const TCHAR* CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* CylinderPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");

	// The three valuables' own colours. Bright on purpose: a safe is a dark object in a dim
	// room, and the Godot original had to lift its colours after the first pass was too dark
	// to find. Legibility beats realism when the player has to spot it across a room.
	const FLinearColor TVPanel = FLinearColor(0.05f, 0.05f, 0.06f);
	const FLinearColor TVScreen = FLinearColor(0.24f, 0.32f, 0.40f);
	const FLinearColor LaptopShell = FLinearColor(0.72f, 0.73f, 0.76f);
	const FLinearColor LaptopScreen = FLinearColor(0.10f, 0.12f, 0.16f);
	const FLinearColor SafeBody = FLinearColor(0.30f, 0.31f, 0.33f);
	const FLinearColor SafeDoor = FLinearColor(0.44f, 0.45f, 0.48f);
	const FLinearColor SafeDial = FLinearColor(0.78f, 0.66f, 0.30f);
}

AHILootProp::AHILootProp()
{
	// The game mode owns the tick and calls SyncFromSim. See HIGameMode.h.
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

UStaticMeshComponent* AHILootProp::AddPart(const TCHAR* Name, const TCHAR* MeshPath,
	const FVector& Centre, const FVector& SizeMeters, const FLinearColor& Colour,
	const FRotator& Rotation)
{
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, MeshPath);
	if (Mesh == nullptr)
	{
		return nullptr;
	}

	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this, Name);
	Part->SetupAttachment(Root);
	Part->SetStaticMesh(Mesh);
	Part->RegisterComponent();
	// No collision, at all, on every part. See the class comment: the sim's rectangles are
	// the only solids in this port, and a prop that blocked a pellet would be a wall the AI
	// does not know about.
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetMobility(EComponentMobility::Movable);

	// The engine's cube is one metre across at unit scale, so a size in metres IS the scale.
	//
	// EVERY SIZE BELOW IS THE ORIGINAL'S. `game.gd:673-708` builds the same three props from
	// the same primitives, and its numbers are the design document -- the port's were 10-15%
	// larger all round (a 1.10 m television against Godot's 0.98 m) for no recorded reason.
	// Rotating first and then offsetting keeps a tipped part on its hinge instead of
	// swinging its centre around.
	const FVector Offset = Rotation.RotateVector(Centre * HI_METERS_TO_UU);
	Part->SetRelativeLocation(Offset);
	Part->SetRelativeRotation(Rotation);
	Part->SetRelativeScale3D(SizeMeters);

	FHIMaterials::Tint(Part, Colour);
	Parts.Add(Part);
	return Part;
}

void AHILootProp::Init(int32 InIndex, const FString& InLabel)
{
	LootIndex = InIndex;
	Label = InLabel;

	if (bBuilt)
	{
		return;
	}
	bBuilt = true;

	// Every shape is modelled with its BASE at z = 0, exactly as the Godot original is, so
	// the same transform drops it onto the floor whether it is being stolen, carried or
	// delivered. That is what makes one placement rule serve all three states.

	if (InLabel == TEXT("TV"))
	{
		AddPart(TEXT("Stand"), CubePath, FVector(0.0f, 0.0f, 0.015f), FVector(0.48f, 0.22f, 0.03f),
			TVPanel);
		AddPart(TEXT("Neck"), CubePath, FVector(0.0f, 0.0f, 0.12f), FVector(0.10f, 0.06f, 0.18f),
			TVPanel);
		AddPart(TEXT("Panel"), CubePath, FVector(0.0f, 0.0f, 0.50f), FVector(0.98f, 0.05f, 0.58f),
			TVPanel);
		// The screen sits a few millimetres proud of the panel's front face, on -Y, which is
		// the side the camera sees when the set is facing the room.
		AddPart(TEXT("Screen"), CubePath, FVector(0.0f, -0.036f, 0.50f), FVector(0.90f, 0.02f, 0.50f),
			TVScreen);
	}
	else if (InLabel == TEXT("Laptop"))
	{
		AddPart(TEXT("Base"), CubePath, FVector(0.0f, 0.0f, 0.012f), FVector(0.34f, 0.25f, 0.024f),
			LaptopShell);
		AddPart(TEXT("Keys"), CubePath, FVector(0.0f, -0.02f, 0.026f), FVector(0.30f, 0.18f, 0.004f),
			LaptopScreen);
		// Hinged at the back edge and tipped back, which is what makes it read as a laptop
		// rather than as two grey slabs. The offset is applied AFTER the rotation, so the
		// lid rotates about the hinge instead of about its own middle.
		const FRotator LidTilt(-104.0f, 0.0f, 0.0f);
		AddPart(TEXT("Lid"), CubePath, FVector(0.0f, 0.125f, -0.11f), FVector(0.34f, 0.24f, 0.014f),
			LaptopShell, LidTilt);
		AddPart(TEXT("LidScreen"), CubePath, FVector(0.0f, 0.125f, -0.104f), FVector(0.30f, 0.20f, 0.006f),
			LaptopScreen, LidTilt);
	}
	else if (InLabel == TEXT("Safe"))
	{
		AddPart(TEXT("Body"), CubePath, FVector(0.0f, 0.0f, 0.23f), FVector(0.46f, 0.40f, 0.46f),
			SafeBody);
		AddPart(TEXT("Door"), CubePath, FVector(0.0f, -0.205f, 0.23f), FVector(0.38f, 0.03f, 0.38f),
			SafeDoor);
		// The cylinder is 1 m tall along its own Z, so tipping it 90 degrees about X stands
		// its flat face toward -Y where the dial belongs.
		AddPart(TEXT("Dial"), CylinderPath, FVector(0.0f, -0.275f, 0.36f), FVector(0.12f, 0.12f, 0.03f),
			SafeDial, FRotator(90.0f, 0.0f, 0.0f));
		AddPart(TEXT("Handle"), CubePath, FVector(0.10f, -0.272f, 0.22f), FVector(0.03f, 0.03f, 0.16f),
			SafeDial);
		// Feet, so a safe does not look like a box resting flush on the carpet.
		AddPart(TEXT("FootL"), CubePath, FVector(-0.20f, 0.0f, 0.01f), FVector(0.06f, 0.40f, 0.02f),
			SafeBody);
		AddPart(TEXT("FootR"), CubePath, FVector(0.20f, 0.0f, 0.01f), FVector(0.06f, 0.40f, 0.02f),
			SafeBody);
	}
}

void AHILootProp::SyncFromSim(const FHISim::FHILootItem& Item, bool bCarried,
	const FVector2D& CarrierPos, float CarrierYaw, const FVector& HandCm, bool bHaveHand)
{
	if (!bBuilt)
	{
		return;
	}

	FVector2D Where = Item.Pos;
	float Height = 0.0f;
	FRotator Facing = FRotator::ZeroRotator;
	// Set when a world-space position has been computed directly (the carried case, which
	// follows a bone) rather than from the sim's ground-plane position plus a height.
	FVector WorldCm = FVector::ZeroVector;
	bool bWorldSet = false;

	if (Item.bDelivered)
	{
		// Piled at the drop point rather than stacked on one coordinate: three props at the
		// same place z-fight and read as one object, which hides that anything was taken.
		Where = FHISim::Drop + FVector2D(((LootIndex % 3) - 1) * 0.75f, (LootIndex / 3) * 0.8f);
		Facing = FRotator(0.0f, 180.0f, 0.0f);
	}
	else if (bCarried)
	{
		Facing = FRotator(0.0f, 90.0f - FMath::RadiansToDegrees(CarrierYaw), 0.0f);
		if (bHaveHand)
		{
			// THE HAND BONE, as `game.gd:730-734` does it, and this is the fix for "whatever
			// the intruder carried out was so big it took up most of the screen".
			//
			// The port used to hold the load at a FIXED 1.05 m in front of the carrier. A
			// television is 0.98 m wide and 0.58 m tall, so at eye level and half a metre from
			// his chest it covers most of a defender's view as he walks past -- and it reads as
			// held out in front of his face rather than carried.
			//
			// The original's comment is the specification: "the hand bone moves with the
			// animation, so taking the position from it is what makes the object look carried
			// rather than glued to his middle". A hand rides at HIP height while walking, so
			// the load drops below the sight line.
			//
			// The offsets are the original's ARRANGEMENT, not its numbers: Godot's own comment
			// says its values were tuned to Mixamo bone positions ("his right hand is at
			// x -0.22"), and this model is the UE mannequin, so its literals would be wrong
			// here. What transfers is: from the right hand, toward the middle of the body, a
			// little forward. THESE THREE NUMBERS WANT ONE VISUAL CHECK.
			const FVector Right(FMath::Cos(CarrierYaw), -FMath::Sin(CarrierYaw), 0.0f);
			const FVector Forward(FMath::Sin(CarrierYaw), FMath::Cos(CarrierYaw), 0.0f);
			WorldCm = HandCm
				- Right * (0.18f * HI_METERS_TO_UU)
				+ Forward * (0.10f * HI_METERS_TO_UU)
				+ FVector::UpVector * (-0.02f * HI_METERS_TO_UU);
			bWorldSet = true;
		}
		else
		{
			// No body to hang it from (a headless run, or a pawn that has not spawned): fall
			// back to the chest offset, pulled in from the 0.42 m and lowered from 1.05 m so
			// the fallback is not the thing that blocks the view either.
			const FVector2D Forward(FMath::Sin(CarrierYaw), FMath::Cos(CarrierYaw));
			Where = CarrierPos + Forward * 0.26f;
			Height = 0.88f;
		}
	}

	SetActorLocation(bWorldSet ? WorldCm : FVector(Where.X, Where.Y, Height) * HI_METERS_TO_UU);
	SetActorRotation(Facing);

	// A taken item that nobody is carrying is a state the sim reaches for exactly one frame
	// while ownership moves between bodies. Hiding it is better than drawing it on the floor
	// where it is not, and it cannot flicker for longer than that.
	const bool bVisible = !Item.bTaken || bCarried || Item.bDelivered;
	SetActorHiddenInGame(!bVisible);
}
