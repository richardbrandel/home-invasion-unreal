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
		AddPart(TEXT("Stand"), CubePath, FVector(0.0f, 0.0f, 0.02f), FVector(0.42f, 0.28f, 0.04f),
			TVPanel);
		AddPart(TEXT("Neck"), CubePath, FVector(0.0f, 0.0f, 0.11f), FVector(0.12f, 0.08f, 0.14f),
			TVPanel);
		AddPart(TEXT("Panel"), CubePath, FVector(0.0f, 0.0f, 0.50f), FVector(1.10f, 0.07f, 0.64f),
			TVPanel);
		// The screen sits a few millimetres proud of the panel's front face, on -Y, which is
		// the side the camera sees when the set is facing the room.
		AddPart(TEXT("Screen"), CubePath, FVector(0.0f, -0.04f, 0.50f), FVector(1.02f, 0.012f, 0.56f),
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
		AddPart(TEXT("Body"), CubePath, FVector(0.0f, 0.0f, 0.28f), FVector(0.52f, 0.46f, 0.56f),
			SafeBody);
		AddPart(TEXT("Door"), CubePath, FVector(0.0f, -0.235f, 0.28f), FVector(0.42f, 0.03f, 0.46f),
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
	const FVector2D& CarrierPos, float CarrierYaw)
{
	if (!bBuilt)
	{
		return;
	}

	FVector2D Where = Item.Pos;
	float Height = 0.0f;
	FRotator Facing = FRotator::ZeroRotator;

	if (Item.bDelivered)
	{
		// Piled at the drop point rather than stacked on one coordinate: three props at the
		// same place z-fight and read as one object, which hides that anything was taken.
		Where = FHISim::Drop + FVector2D(((LootIndex % 3) - 1) * 0.75f, (LootIndex / 3) * 0.8f);
		Facing = FRotator(0.0f, 180.0f, 0.0f);
	}
	else if (bCarried)
	{
		// In his hands, at chest height and just in front of him. `AimDir` is not used
		// because a man carrying a safe does not hold it out at arm's length; a fixed
		// chest offset is the honest read of "carrying".
		const FVector2D Forward(FMath::Sin(CarrierYaw), FMath::Cos(CarrierYaw));
		Where = CarrierPos + Forward * 0.42f;
		Height = 1.05f;
		Facing = FRotator(0.0f, 90.0f - FMath::RadiansToDegrees(CarrierYaw), 0.0f);
	}

	SetActorLocation(FVector(Where.X, Where.Y, Height) * HI_METERS_TO_UU);
	SetActorRotation(Facing);

	// A taken item that nobody is carrying is a state the sim reaches for exactly one frame
	// while ownership moves between bodies. Hiding it is better than drawing it on the floor
	// where it is not, and it cannot flicker for longer than that.
	const bool bVisible = !Item.bTaken || bCarried || Item.bDelivered;
	SetActorHiddenInGame(!bVisible);
}
