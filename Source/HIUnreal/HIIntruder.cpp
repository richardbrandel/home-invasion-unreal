#include "HIIntruder.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "TimerManager.h"
#include "HICharAnim.h"
#include "HIMaterials.h"
#include "HIUnits.h"

namespace
{
	// A DIFFERENT mannequin from the homeowner's, and that is the point: with one mesh for
	// both, three men coming through the door would be indistinguishable from each other and
	// from you. Fix-list item 5's "walls" and item 2's "characters" are separate items; being
	// able to tell who is who is its own requirement and this is the cheapest way to meet it.
	const TCHAR* IntruderMeshPath = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple");
	const TCHAR* DeathPath = TEXT("/Game/Characters/Mannequins/Anims/Death/MM_Death_Back_01.MM_Death_Back_01");

	/**
	 * The five places a man gets shot, in the order the wounds appear.
	 *
	 * UE5's skeletons share these bone names between Manny and Quinn, so the same table
	 * serves both. The offsets are in the BONE's space, which for these bones runs down the
	 * limb -- so a wound on `spine_03` is pushed out along the bone's own X/Z rather than
	 * along world axes, and it stays on the chest as he turns.
	 */
	struct FWoundSpot
	{
		const TCHAR* Bone;
		float X, Y, Z;      // cm, relative to the bone
		float Size;         // cm across
	};
	const FWoundSpot WoundSpots[5] = {
		{ TEXT("spine_03"),     6.0f, -9.0f,  0.0f, 7.0f },   // chest, his left
		{ TEXT("upperarm_r"),   5.0f,  0.0f, -7.0f, 6.0f },   // right arm
		{ TEXT("spine_01"),     7.0f,  8.0f,  0.0f, 7.0f },   // abdomen
		{ TEXT("thigh_l"),      0.0f, -7.0f,  9.0f, 6.0f },   // left thigh
		{ TEXT("upperarm_l"),   5.0f,  0.0f,  7.0f, 6.0f },   // left arm
	};
}

AHIIntruder::AHIIntruder()
{
	// The game mode is the only ticker. See the class comment.
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	HitVolume = CreateDefaultSubobject<UCapsuleComponent>(TEXT("HitVolume"));
	HitVolume->SetupAttachment(Root);
	// A capsule's half-height covers the cylinder only; the hemispherical caps add the
	// radius at each end, so the TOTAL height is (2 * HalfHeight + 2 * Radius). The sim's
	// man is 2.0 m tall and 0.84 m across, so the cylinder part is 2.0 - 0.84 = 1.16 m.
	HitVolume->InitCapsuleSize(FHISim::ThiefRadius * HI_METERS_TO_UU, 0.58f * HI_METERS_TO_UU);
	// HalfHeight + Radius = 58 + 42 = 100 cm, so the capsule's centre sits one metre above
	// the root -- which is the root's whole purpose: the root is on the floor, so the sim's
	// own position drops straight in and both children are plain offsets from it.
	HitVolume->SetRelativeLocation(FVector(0.0f, 0.0f, 1.0f * HI_METERS_TO_UU));

	// Query-only, and only for Visibility. The sim owns every MOVEMENT and overlap decision,
	// so the physics engine must never push a body around -- but a pellet trace has to be
	// able to find him, and Visibility is the channel the shot is cast on.
	HitVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	HitVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	HitVolume->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	HitVolume->SetGenerateOverlapEvents(false);

	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	// The mesh's own origin is between the feet, like the root, so no offset is needed at all.
	Body->SetRelativeLocation(FVector::ZeroVector);
	// The MESH does not collide: the capsule above is the hit volume, and a second, finer
	// one would disagree with the sim about what a hit is. See the class comment.
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetGenerateOverlapEvents(false);

	Gun = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Gun"));
	Gun->SetupAttachment(Body);
	Gun->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Gun->SetVisibility(false);

	// ---- the wounds. Built here rather than in BeginPlay because a component created
	// outside a constructor cannot use CreateDefaultSubobject (it is protected, and only
	// valid while the CDO is being built) -- see the porting hazards.
	Wounds.Reserve(MaxWounds);
	for (int32 Idx = 0; Idx < MaxWounds; ++Idx)
	{
		const FName Name(*FString::Printf(TEXT("Wound%d"), Idx));
		UStaticMeshComponent* Wound = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Wound->SetupAttachment(Body);
		Wound->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Wound->SetGenerateOverlapEvents(false);
		Wound->SetCastShadow(false);
		Wound->SetVisibility(false);
		Wounds.Add(Wound);
	}
}

void AHIIntruder::BeginPlay()
{
	Super::BeginPlay();
	BuildVisuals();
}

void AHIIntruder::BuildVisuals()
{
	if (bVisualsBuilt)
	{
		return;
	}
	bVisualsBuilt = true;

	if (USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, IntruderMeshPath))
	{
		Body->SetSkeletalMeshAsset(Mesh);
		FHICharAnim::SetupLocomotion(Body);
	}
	if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, FHICharAnim::PistolMeshPath()))
	{
		Gun->SetStaticMesh(Mesh);
		FHICharAnim::AttachPistol(this, Body, Gun);
		Gun->SetVisibility(true);
	}

	// The wounds share the engine cube and a blood-coloured material. Loaded here rather
	// than in the constructor for the same reason as everything else in this function.
	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		for (UStaticMeshComponent* Wound : Wounds)
		{
			if (Wound == nullptr)
			{
				continue;
			}
			Wound->SetStaticMesh(Cube);
			FHIMaterials::Tint(Wound, FHIMaterials::Blood);
		}
	}
}

int32 AHIIntruder::WoundsForHealth(float Hp)
{
	// Five bands of 20 HP. At full health he is unmarked; a lethal hit shows all five, which
	// is the point -- the body should say what the HUD number says.
	const float Fraction = FMath::Clamp(1.0f - (Hp / static_cast<float>(FHISim::ThiefHealth)), 0.0f, 1.0f);
	return FMath::Clamp(FMath::FloorToInt(Fraction * MaxWounds + KINDA_SMALL_NUMBER), 0, MaxWounds);
}

bool AHIIntruder::WoundPlacement(int32 Index, FName& OutBone, FVector& OutOffset, FVector& OutSize)
{
	if (Index < 0 || Index >= MaxWounds)
	{
		return false;
	}
	const FWoundSpot& Spot = WoundSpots[Index];
	OutBone = FName(Spot.Bone);
	OutOffset = FVector(Spot.X, Spot.Y, Spot.Z);

	// A flattened slab, so it lies against the body instead of sinking into it or floating
	// off it. Two centimetres thick against a limb a hundred across.
	//
	// THE SCALE IS IN METRES, AND THESE NUMBERS ARE CENTIMETRES. The engine's cube is ONE
	// METRE across at unit scale, so `SetRelativeScale3D(7.0)` is a seven-metre cube -- and
	// that is exactly what was shipped: every wound was a 7 m x 7 m x 2 m dark red box.
	//
	// Reported from play as "when I fire my gun a giant dark red rectangular box appears. It
	// kind of fills up the room." It appeared on the FIRST hit and not before, because a
	// full-health intruder shows no wounds and the first hit takes him below full.
	//
	// Note what is NOT converted here: `OutOffset` above is in centimetres, and should be,
	// because `SetRelativeLocation` is centimetre-denominated. Only the scale is in metres.
	// That asymmetry is the whole trap, and it is why `HI.Unreal.Wounds` now asserts the
	// SIZE of a shown wound and not merely that it is shown.
	OutSize = FVector(Spot.Size, Spot.Size, 2.0f) / HI_METERS_TO_UU;
	return true;
}

void AHIIntruder::ShowWounds(int32 Count)
{
	for (int32 Idx = 0; Idx < Wounds.Num(); ++Idx)
	{
		UStaticMeshComponent* Wound = Wounds[Idx];
		if (Wound == nullptr)
		{
			continue;
		}
		if (Idx >= Count)
		{
			Wound->SetVisibility(false);
			continue;
		}

		FName Bone;
		FVector Offset;
		FVector Size;
		if (!WoundPlacement(Idx, Bone, Offset, Size))
		{
			Wound->SetVisibility(false);
			continue;
		}

		// Attach to the BONE, which is the whole point: the mesh animates, the bone carries
		// the mark with it, and a wound on the arm follows the arm.
		Wound->AttachToComponent(Body, FAttachmentTransformRules::KeepRelativeTransform, Bone);
		Wound->SetRelativeLocation(Offset);
		Wound->SetRelativeScale3D(Size);
		Wound->SetVisibility(true);
	}
}

void AHIIntruder::GoLimp()
{
	// One-shot. The timer cannot fire twice, but this is the only guard against a second
	// caller -- and a body handed to physics twice would be re-positioned in mid-fall.
	if (bRagdolled)
	{
		return;
	}
	bRagdolled = true;

	// THE FALL IS PHYSICS, BECAUSE NO CLIP IN THE ENGINE PUTS A MAN ON THE FLOOR.
	//
	// Measured, not assumed: all six `MM_Death_*` sequences end with the pelvis at about 87 cm
	// and hold there -- a hunch, which is what a player saw and reported. There is no
	// full-collapse death clip anywhere in the mannequin content (the 97 sequences under
	// `Anims/` were listed to check). So the clip gives the moment of impact and physics gives
	// the fall.
	//
	// Both prerequisites exist and BOTH are asserted by `HI.Unreal.DeathFall`, because either
	// one missing makes this function silently do nothing and leaves the corpse standing:
	// `PA_Mannequin` is assigned to `SKM_Quinn_Simple`, and the house's lawn, floors, walls and
	// furniture are InstancedStaticMeshComponents with `QueryAndPhysics` collision to land on.
	//
	// The profile is set before simulation is enabled: `Ragdoll` is the engine profile that
	// makes the mesh a PhysicsBody which blocks the world, and the component was constructed
	// with collision explicitly OFF (the capsule is the hit volume), so without this the body
	// would simulate and fall through the lawn for ever.
	Body->SetCollisionProfileName(TEXT("Ragdoll"));
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetAllBodiesSimulatePhysics(true);
	Body->SetSimulatePhysics(true);
	Body->WakeAllRigidBodies();

	// Once, at Display rather than Verbose: "the corpse never went limp" and "the corpse went
	// limp somewhere you cannot see" look identical in a log otherwise.
	UE_LOG(LogTemp, Display, TEXT("HI: intruder %d went limp -- handed to physics"), ThiefId);

}

FRotator AHIIntruder::BodyRotation(float SimYaw)
{
	return FRotator(0.0f, 90.0f - FMath::RadiansToDegrees(SimYaw), 0.0f);
}

void AHIIntruder::SyncFromSim(const FHISim::FHIThief& T, float Dt)
{
	// The sim's plane is (X, Y) with Z up, so a 2-D position plus Z is already a world
	// position -- no axis swap here, unlike Godot, where the plane is XZ. Only the units
	// need converting, and they go through one constant so a world cannot end up a hundred
	// times too small for the engine's own assumptions.
	SetActorLocation(FVector(T.Pos.X, T.Pos.Y, T.Z) * HI_METERS_TO_UU);
	SetActorRotation(BodyRotation(T.Yaw));

	// Wounds follow health every frame: see the header for why this is derived here rather
	// than pushed from the damage site.
	ShowWounds(WoundsForHealth(T.Hp));

	if (!T.bAlive)
	{
		if (!bDeathPlayed)
		{
			// Latched: replaying a one-shot every frame would pin him to its first frame and
			// he would die standing up, forever, which is the worst of both outcomes.
			bDeathPlayed = true;
			FHICharAnim::PlayOneShot(Body, DeathPath);
			// A dead man is not a target. Leaving the capsule blocking would let a player
			// keep shooting a corpse the sim has already stopped counting.
			HitVolume->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
			Gun->SetVisibility(false);
			// THE HANDOVER IS ON A TIMER, and this is not a style choice. Once the round is
			// decided the game mode stops calling this function at all -- "a decided round is
			// inert at the sim level too" -- so a handover that counted frames HERE would never
			// happen for the last man killed, who is exactly the one the player is watching.
			// A world timer fires whether or not anybody is still syncing the corpse.
			GetWorldTimerManager().SetTimer(RagdollTimer, this, &AHIIntruder::GoLimp,
				RagdollAfterDeath, /*bLooping=*/false);
		}

		return;
	}

	// Measured from the sim's own previous position rather than read from a speed field:
	// see the header. Guarded against a zero step so a paused frame cannot divide by zero.
	const float Speed = (Dt > 1e-5f) ? (T.Pos - T.Prev).Size() / Dt : 0.0f;
	FHICharAnim::DriveLocomotion(Body, Speed, true);

	// He carries a visible pistol ONLY while his hands are free, which is the same condition
	// the sim uses to decide he may fire. Drawing it otherwise would show a gun in the hands
	// of a man the sim has already decided cannot shoot.
	const bool bFree = T.Carry.IsEmpty();
	Gun->SetVisibility(bFree && Gun->GetStaticMesh() != nullptr);
}
