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
#include "HIPlayerCharacter.h"
#include "HIUnits.h"

namespace
{
	/**
	 * The five places a man gets shot, in the order the wounds appear.
	 *
	 * The cast is Mixamo now, and its bone names are the standard ones with the importer's
	 * namespace stripped (`mixamorig8:Spine2` in the source FBX, `Spine2` in the skeleton --
	 * measured, and asserted per character by `HI.Unreal.Cast`). The old table's mannequin
	 * names (`spine_03`, `upperarm_r`, ...) exist in none of these rigs.
	 *
	 * The offsets are in the BONE's space, and bone-space axes are a per-rig property that
	 * nobody has measured yet: `HI.Unreal.Cast` logs each wound bone's ref-pose child
	 * direction, and a capture of a shot intruder is the verdict. The numbers here are
	 * deliberately MODEST until both have spoken -- a wound slightly sunk into the body reads
	 * far better than one floating a hand-span off it in a direction only the old rig had.
	 */
	struct FWoundSpot
	{
		const TCHAR* Bone;
		float X, Y, Z;      // cm, relative to the bone -- PROVISIONAL, see above
		float Size;         // cm across
	};
	const FWoundSpot WoundSpots[5] = {
		{ TEXT("Spine2"),     3.0f, -5.0f, 0.0f, 7.0f },   // chest, his left
		{ TEXT("RightArm"),   3.0f,  0.0f, -4.0f, 6.0f },  // right arm
		{ TEXT("Spine1"),     3.0f,  4.0f, 0.0f, 7.0f },   // abdomen
		{ TEXT("LeftUpLeg"),  0.0f, -4.0f, 5.0f, 6.0f },   // left thigh
		{ TEXT("LeftArm"),    3.0f,  0.0f,  4.0f, 6.0f },  // left arm
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
	// Same rule as the pawn's body, and it is load-bearing in two directions: a pellet that
	// the sim let through must not be stopped by the mesh (it is not the hit volume), and the
	// mesh must not stop the CAMERA trace either -- a wall that blocked it would pull the view
	// inside the house and show the front garden from the bedroom. The hit volume above is the
	// only part of him that answers a trace, and only on Visibility.
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetGenerateOverlapEvents(false);
	Body->SetCastShadow(true);

	Gun = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Gun"));
	Gun->SetupAttachment(Body);
	Gun->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Gun->SetVisibility(false);

	// WOUNDS, hidden until SyncFromSim shows the ones his health has earned. Created here and
	// parented in BuildVisuals, which is where the mesh is loaded and therefore the only place
	// a BONE name can be resolved.
	for (int32 Idx = 0; Idx < MaxWounds; ++Idx)
	{
		UStaticMeshComponent* Wound = CreateDefaultSubobject<UStaticMeshComponent>(
			*FString::Printf(TEXT("Wound%d"), Idx));
		Wound->SetupAttachment(Body);
		Wound->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Wound->SetGenerateOverlapEvents(false);
		Wound->SetCastShadow(false);
		Wound->SetVisibility(false);
		if (UStaticMesh* Quad = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")))
		{
			Wound->SetStaticMesh(Quad);
		}
		FHIMaterials::Tint(Wound, FHIMaterials::Blood);
		Wounds.Add(Wound);
	}
}

void AHIIntruder::BeginPlay()
{
	Super::BeginPlay();
	// VISUALS ARE BUILT ON THE FIRST SyncFromSim, NOT HERE. ThiefId decides which of the
	// cast this body is, and the game mode can only assign it after SpawnActor has returned
	// -- which is after BeginPlay has run. Building here would give every man Id 0's face
	// (all of them James), the exact failure the deferral exists to avoid.
}

void AHIIntruder::BuildVisuals()
{
	if (bVisualsBuilt)
	{
		return;
	}
	bVisualsBuilt = true;

	// THREE DIFFERENT MEN, and that is the point: with one mesh for all of them, three men
	// coming through the door would be indistinguishable from each other. The mannequin era
	// met this by using a different mannequin from the homeowner's; the cast era gives each
	// Id its own named person (cycling if a round runs more than three).
	CastTag = FHICharAnim::IntruderTagForId(ThiefId);
	const FString MeshPath = FHICharAnim::MeshPathFor(*CastTag);
	if (USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath))
	{
		Body->SetSkeletalMeshAsset(Mesh);
		FHICharAnim::SetupCharacter(Body, *CastTag);
	}
	else
	{
		// NOT SILENT, and not fatal: the round still plays around a reference-pose body.
		// The usual cause is a Content/ that predates tools/import-assets.py.
		UE_LOG(LogTemp, Warning, TEXT("HI: intruder %d's mesh did not load from %s"),
			ThiefId, *MeshPath);
	}

	// The weapon, attached to the right hand. Parented into the bone so it follows the
	// animation; if the mesh has no hand bone, this returns nullptr and the gun is simply
	// not shown rather than floating at the actor's origin.
	if (UStaticMesh* PistolMesh = LoadObject<UStaticMesh>(nullptr, FHICharAnim::PistolMeshPath()))
	{
		Gun->SetStaticMesh(PistolMesh);
		if (FHICharAnim::AttachPistol(this, Body, Gun) != nullptr)
		{
			Gun->SetVisibility(false);
		}
	}

	// The wound quads were created in the constructor and parented to `Body`; only now that
	// the mesh is loaded can a BONE be named. A bone that does not exist leaves the wound
	// parented to the mesh's origin, which would slide across the skin -- exactly what the
	// bone attachment is here to prevent -- so it is hidden instead, and says so.
	for (int32 Idx = 0; Idx < Wounds.Num(); ++Idx)
	{
		FName Bone; FVector Offset; FVector Size;
		if (!WoundPlacement(Idx, Bone, Offset, Size) || Wounds[Idx] == nullptr)
		{
			continue;
		}
		if (Body->GetSkeletalMeshAsset() == nullptr
			|| Body->GetBoneIndex(Bone) == INDEX_NONE)
		{
			UE_LOG(LogTemp, Warning, TEXT("HI: wound %d names bone '%s', which this mesh does not have"),
				Idx, *Bone.ToString());
			Wounds[Idx]->SetVisibility(false);
			continue;
		}
		Wounds[Idx]->AttachToComponent(Body,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale, Bone);
		Wounds[Idx]->SetRelativeLocation(Offset);
		// A blood spatter lying in the plane of the skin, not sticking out of it.
		Wounds[Idx]->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));
		// `Size` is ALREADY metres -- see WoundPlacement's unit note. Dividing here too
		// would make every wound a millimetre across: invisible, and just as wrong as
		// the seven-metre box this asymmetry once produced.
		Wounds[Idx]->SetRelativeScale3D(Size);
		Wounds[Idx]->SetVisibility(false);
	}
}

void AHIIntruder::PlayActionClip(const TCHAR* Clip, float HoldSec)
{
	FHICharAnim::PlayAction(Body, *CastTag, Clip, HoldSec);
}

void AHIIntruder::ShowWounds(int32 Count)
{
	for (int32 Idx = 0; Idx < Wounds.Num(); ++Idx)
	{
		if (Wounds[Idx] != nullptr)
		{
			Wounds[Idx]->SetVisibility(Idx < Count);
		}
	}
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
	// that is exactly what was shipped once: every wound was a 7 m x 7 m x 2 m dark red box,
	// reported from play as "when I fire my gun a giant dark red rectangular box appears. It
	// kind of fills up the room."
	//
	// Note what is NOT converted here: `OutOffset` above is in centimetres, and should be,
	// because `SetRelativeLocation` is centimetre-denominated. Only the scale is in metres.
	// That asymmetry is the whole trap, and it is why `HI.Unreal.Wounds` asserts the SIZE of
	// a shown wound and not merely that it is shown -- and why the 2026-10-13 rewrite of
	// this table for the Mixamo bones, which dropped the conversion, was caught by the test
	// within one build instead of by a room-filling box in a screenshot.
	OutSize = FVector(Spot.Size, Spot.Size, 2.0f) / HI_METERS_TO_UU;
	return true;
}

int32 AHIIntruder::WoundsForHealth(float Hp)
{
	// A band is 20 HP. The first whole band lost shows the first wound; a corpse shows all
	// of them. The clamp is what makes 0 HP mean "all five" rather than "six, off the end".
	const int32 Bands = static_cast<int32>((FHISim::ThiefHealth - Hp) / 20.0f);
	return FMath::Clamp(Bands, 0, MaxWounds);
}

FRotator AHIIntruder::BodyRotation(float SimYaw)
{
	// t = 90 - a - the mesh's own facing; the derivation and the measured facing constant
	// live in FHICharAnim now, in one place instead of one per actor. A quarter turn out is
	// silent -- it WAS, on 2026-10-13, at exactly 90.0 degrees, measured by `-HIProbe`'s
	// clavicle line before anything was believed.
	return FHICharAnim::BodyRotationFor(SimYaw);
}

void AHIIntruder::GoLimp()
{
	if (bRagdolled || Body == nullptr)
	{
		return;
	}
	bRagdolled = true;
	// Animation must let go first. A pose still being evaluated writes over the physics
	// every frame and the body snaps back upright -- which is what "still standing" looked
	// like when the handover was tried without this.
	Body->SetAnimInstanceClass(nullptr);
	Body->SetCollisionProfileName(TEXT("Ragdoll"));
	Body->SetAllBodiesSimulatePhysics(true);
	Body->SetSimulatePhysics(true);
	// NOT silent. `SetSimulatePhysics` does nothing at all without a physics asset, and the
	// result is indistinguishable from the bug being fixed -- a corpse that stays standing.
	// The cast's imported meshes each carry their own auto-generated asset (`idle_PhysicsAsset`,
	// created by the FBX import beside the mesh); `HI.Unreal.DeathFall` asserts it per
	// character, because "it worked on the mannequin" is precisely the kind of inherited
	// assumption this port keeps having to unlearn.
	if (Body->GetPhysicsAsset() == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("HI: %s has no physics asset, so the corpse cannot fall"),
			*GetName());
	}
}

void AHIIntruder::SyncFromSim(const FHISim::FHIThief& T, float Dt)
{
	// Built here, not in BeginPlay: ThiefId -- which decides the face -- is only assigned
	// after the spawn returns, and the game mode calls this immediately after assigning it.
	BuildVisuals();

	if (Root != nullptr)
	{
		Root->SetWorldLocation(FVector(T.Pos.X * HI_METERS_TO_UU, T.Pos.Y * HI_METERS_TO_UU, T.Z * HI_METERS_TO_UU));
	}
	if (Body != nullptr && !bRagdolled)
	{
		Body->SetWorldRotation(BodyRotation(T.Yaw));
	}

	// Wounds follow health every frame: see the header for why this is derived here rather
	// than pushed from the damage site. A dead man shows all five.
	ShowWounds(T.bAlive ? WoundsForHealth(T.Hp) : MaxWounds);

	if (!T.bAlive)
	{
		// Play it ONCE. Calling SetAnimation every frame restarts the death from frame 0
		// and the body never finishes falling -- which is exactly the bug this latch fixes,
		// and it looked like the ragdoll not working rather than like a restarted clip.
		if (!bDeathPlayed)
		{
			bDeathPlayed = true;
			FHICharAnim::PlayDeath(Body, *CastTag);

			// A CORPSE PUTS ITS GUN DOWN. The pistol stayed in the dead man's hand and, once
			// the ragdoll took over, dangled from the wrist of a body on the floor -- because
			// nothing after death ever touched its visibility again. Reported from play.
			if (Gun != nullptr)
			{
				Gun->SetVisibility(false);
			}

			// THE HANDOVER IS A TIMER, NOT A FRAME. Once the round is decided the game mode
			// stops calling SyncFromSim -- a decided round is inert at the sim level too -- so
			// a handover that waited to be driven would never happen for the LAST man killed,
			// who is precisely the one the player is watching fall. Measured, not assumed:
			// across `MM_Death_Back_01` the engine clip's pelvis went 96.6 cm -> 87.5 cm and
			// never lower, so the clip alone could not put anyone down; the cast's own clip is
			// a real fall (Hips 0.96 m -> 0.14 m across 3.03 s, measured from the FBX keys)
			// and physics now finishes it against the actual floor rather than a baked one.
			// 0.85 s lands mid-fall in both cases, so the two halves read as one motion.
			// See the header for why it is a timer.
			if (UWorld* World = GetWorld())
			{
				World->GetTimerManager().SetTimer(
					RagdollTimer, this, &AHIIntruder::GoLimp, RagdollAfterDeath, /*bLoop=*/false);
			}
		}

		return;
	}

	// Measured from the sim's own previous position rather than read from a speed field:
	// see the header. Guarded against a zero step so a paused frame cannot divide by zero.
	const float Speed = (Dt > 1e-5f) ? (T.Pos - T.Prev).Size() / Dt : 0.0f;
	// ARMED ONLY WITH HIS HANDS FREE, which is the same condition the gun's visibility uses --
	// a man carrying a television is not in a shooting stance, and the pistol is put away.
	const bool bFree = T.Carry.IsEmpty();
	FHICharAnim::DriveCharacter(Body, *CastTag, Speed, true, /*bArmed=*/bFree,
		/*bCrouching=*/false);

	// THE RELOAD IS AN EDGE, NOT A STATE: `T.Reloading` counts down (ThiefReload = 1.5 s)
	// and the one-shot must start once, not restart every frame. The hold is the sim's own
	// window so the animation cannot still be fumbling after he can fire again.
	const bool bReloading = T.Reloading > 0.0f;
	if (bReloading && !bWasReloading)
	{
		PlayActionClip(FHICharAnim::ClipReload(), FHISim::ThiefReload);
	}
	bWasReloading = bReloading;

	// He carries a visible pistol ONLY while his hands are free, which is the same condition
	// the sim uses to decide he may fire. Drawing it otherwise would show a gun in the hands
	// of a man the sim has already decided cannot shoot.
	Gun->SetVisibility(bFree && Gun->GetStaticMesh() != nullptr);
	if (bFree && Gun->GetStaticMesh() != nullptr)
	{
		// AND IT POINTS WHERE HE POINTS. The Mixamo hand BONE's axes are its own -- the
		// mannequin socket's "+Y is the pointing direction" was a measurement of that rig,
		// not a law -- and a pistol left at the bone's identity rotation would swing with
		// the walk cycle and point at the floor. The homeowner's weapon has been
		// world-aimed since bug 14 for exactly this reason; the intruder's now is too,
		// through the same pure helper, along his facing.
		Gun->SetWorldRotation(AHIPlayerCharacter::WeaponAimRotation(
			FHISim::AimDir(T.Yaw, 0.0f)));
	}
}
