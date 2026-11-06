#include "HIIntruder.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"

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
	// The effective tag: the preferred character if her assets are imported, else the stand-in.
	// See FHICharAnim::EffectiveIntruderTag for why the fallback exists.
	CastTag = FHICharAnim::EffectiveIntruderTag(ThiefId);
	const FString MeshPath = FHICharAnim::MeshPathFor(*CastTag);
	if (USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath))
	{
		Body->SetSkeletalMeshAsset(Mesh);
		FHICharAnim::SetupCharacter(Body, *CastTag);
		// ---- THE BRUTE'S AXE IS STILL THERE (2026-11-04). It is baked into the mesh (slot 9,
		// `phong4`, with its own three textures in `SourceAssets/characters/brute/idle.fbx`), and
		// replacing that slot with a zero-opacity glass instance did NOT hide it -- the axe
		// rendered bright white instead. Removing it means editing the model and re-importing,
		// which is recorded in docs/REALISM-BACKLOG.md rather than half-done here.
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

FRotator AHIIntruder::BodyRotation(float SimYaw, const TCHAR* Tag)
{
	// t = 90 - a - the mesh's own facing; the derivation and the measured facing constant
	// live in FHICharAnim now, in one place instead of one per actor. A quarter turn out is
	// silent -- it WAS, on 2026-10-13, at exactly 90.0 degrees, measured by `-HIProbe`'s
	// clavicle line before anything was believed.
	return FHICharAnim::BodyRotationFor(SimYaw, Tag);
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

	// TAKEN ALIVE FIRST, AND BEFORE THE SIM MOVES HIM (2026-11-05). The sim's thief keeps the
	// position where the dart landed, so applying it after the hang teleported the hanging man back
	// up to the ground floor every frame -- measured: the actor was on the hook at (-55, -1140) in
	// the basement and back at (-79, 278) a frame later, which is why the basement looked empty.
	if (T.bCaptured)
	{
		if (!bCapturedSeen)
		{
			bCapturedSeen = true;
			CapturedT = 0.0f;
			GoLimp();
			UE_LOG(LogTemp, Display, TEXT("HI: %s darted -- down and out, alive"), *CastTag);
		}
		CapturedT += Dt;
		if (!bHanging && CapturedT > 2.2f)
		{
			// The beat between falling and disappearing is what separates the two events: the
			// player sees him go down, then he is gone, and he is not a corpse either.
			HangInBasement();
		}
		if (bHanging)
		{
			UpdateHangVisuals();
		}
		return;
	}

	if (Root != nullptr)
	{
		Root->SetWorldLocation(FVector(T.Pos.X * HI_METERS_TO_UU, T.Pos.Y * HI_METERS_TO_UU, T.Z * HI_METERS_TO_UU));
	}
	if (Body != nullptr && !bRagdolled)
	{
		// `T.Yaw` is a BEARING; the body-rotation formula is in the sim's (sin, cos) convention.
		Body->SetWorldRotation(BodyRotation(FHISim::YawFromBearing(T.Yaw), *CastTag));
	}

	// Wounds follow health every frame: see the header for why this is derived here rather
	// than pushed from the damage site. A dead man shows all five.
	ShowWounds(T.bAlive ? WoundsForHealth(T.Hp) : MaxWounds);

	// TAKEN ALIVE (2026-11-05). He is not dying -- a dart does not wound -- so this deliberately
	// comes BEFORE the death branch: the same ragdoll takes him down, then he disappears and is
	// found hanging in the basement.
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
	// The hands-free flag, which drives BOTH the gun's visibility below AND the standing
	// pose: a man carrying a television has his pistol put away and stands in the unarmed
	// `idle`, while a man with his hands free is holding his pistol, so he stands in the
	// armed `pistol_idle` (see DriveCharacter's selection comment).
	const bool bFree = T.Carry.IsEmpty();
	FHICharAnim::DriveCharacter(Body, *CastTag, Speed, true, /*bCrouching=*/false, /*bArmed=*/bFree);

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
			FHISim::AimDir(FHISim::YawFromBearing(T.Yaw), 0.0f)));
	}
}

// ------------------------------------------------------------------ taken alive (2026-11-05)

FVector AHIIntruder::HangStation(int32 Id)
{
	// Three stations in the basement's open floor, along the middle of the room. The basement is
	// 2.44 m below the ground floor, x -6.1..6.1 and y -17.4..-8.0, with the lally columns at
	// x -3.125 and the workbench against the north wall, so a line down x = 0 at y -11.2..-13.6
	// is clear floor with nothing in front of it.
	const float X = (Id % 2 == 0) ? -0.55f : 0.55f;
	const float Y = -11.4f - 1.15f * float(Id / 2) - 1.15f * float(Id % 2 == 0 ? 0 : 1);
	return FVector(X, Y, FHISim::BasementZ);
}

void AHIIntruder::HangInBasement()
{
	if (bHanging || Body == nullptr)
	{
		return;
	}
	bHanging = true;

	// He is a prisoner now, not a threat: the pistol goes away and the hit capsule is switched off,
	// so a dart or a pellet cannot kill a man who is already hanging in the basement.
	if (Gun != nullptr)
	{
		Gun->SetVisibility(false);
	}
	SetActorEnableCollision(false);

	// METRES -> CENTIMETRES. The sim speaks metres and the actor transform speaks centimetres; the
	// first pass put him at (-0.6, -11.4, -2.4) CM -- a two-centimetre man -- and the wrist
	// constraints then pulled his hands across the whole map.
	const FVector Station = HangStation(ThiefId) * HI_METERS_TO_UU;

	// STILL, NOT SIMULATED -- and that is a deliberate retreat. The hanging was first built as a
	// real suspension: two physics constraints pinning the HAND BODIES to the ceiling, which is by
	// far the best-looking answer because the body then hangs off its own arms. It works on Alina
	// and fails on the Dancer, whose rig carries the glTF import's 100x encoding: her physics bodies
	// come back from `RecreatePhysicsState` at ~90 m from the actor, so the constraints yank her
	// across the map (measured: hands at (8999, -1946) cm against a station at (-55, -1140)). Debugging
	// one rig's physics encoding is not worth the feature, so the body is FROZEN instead: the
	// animation is released, which leaves him in the reference pose -- arms out, which is exactly a
	// man shackled by both wrists -- and the chains are drawn to those wrists every frame.
	Body->SetAllBodiesSimulatePhysics(false);
	Body->SetSimulatePhysics(false);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// A POSE HAS TO BE EVALUATED AT THE NEW PLACE. A ragdoll's bones are cached in WORLD space
	// wherever the simulation left them, so moving the component without re-evaluating leaves the
	// mesh -- and the wrists the chains are drawn to -- on the ground floor where he fell.
	// Playing a clip at the station rewrites them there, every frame, with no physics involved.
	// THE MESH DOES NOT TICK. This actor is stepped by the game mode ("like the pawn it does not
	// tick"), so a clip set here is never evaluated and the bones stay in the world space the
	// ragdoll left them -- which is why the body and its chains drew on the ground floor while the
	// actor sat in the basement. The evaluation is driven by hand, every frame, in
	// UpdateHangVisuals; this only arms it.
	// THE HANG CLIP, authored by `HIAnimTool::MakeHangClip`: arms overhead, legs bent. It is a
	// duplicate of this character's own idle, so it is bound to this skeleton.
	const FString HangPath = FString::Printf(TEXT("/Game/HIImported/characters_%s/hang"), *CastTag);
	FString StandPath = HangPath;
	if (!FPackageName::DoesPackageExist(StandPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("HI: %s has no /hang clip -- run tools/make-hang-clips.py; using idle"), *CastTag);
		StandPath = FHICharAnim::ClipPathFor(*CastTag, FHICharAnim::ClipIdle());
	}
	StandClip = LoadObject<UAnimationAsset>(nullptr, *StandPath);
	if (StandClip != nullptr)
	{
		Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Body->SetAnimation(StandClip);
		Body->Play(true);
	}

	// He hangs: feet clear of the floor, and a slight lean, which is what a limp body on two
	// wrist chains looks like. The hang is deliberately still -- see the note in the header.
		// 0.23 m, not "as high as he goes": the basement is 2.05 m to the ceiling and a reference-pose
	// body puts its wrists 1.42 m above its feet, so 0.23 m of lift leaves a ~0.40 m chain and his
	// feet dangling clear of the slab. Lifting him further put his shoulders into the ceiling and
	// the chain length went to zero, which is why the chains vanished from the frame at 0.62 m.
	const FVector Hanging = Station + FVector(0.0f, 0.0f, 0.23f * HI_METERS_TO_UU);
	if (Root != nullptr)
	{
		Root->SetWorldLocation(Hanging);
	}
	// METRES -> CENTIMETRES: the sim speaks metres and actor transforms speak centimetres.
	Body->SetWorldLocation(Hanging);
	// THE COMPONENT KEEPS ITS OWN ROTATION, and the ragdoll left it lying wherever he fell -- so
	// without this the "hanging" man floats horizontally, feet out, which is exactly what the
	// first framed capture showed. The actor's small lean is what makes him look limp.
	Body->SetRelativeRotation(FRotator::ZeroRotator);
	SetActorRotation(FRotator(7.0f, GetActorRotation().Yaw, 3.0f));
	// AND HE TICKS FROM HERE ON. Every intruder is stepped by the game mode rather than ticking, so
	// the skeletal mesh's own tick -- which is what evaluates an animation -- never runs, and the
	// hang clip's keys were written, loaded, and then never sampled: the body stayed in the pose the
	// ragdoll left (the first frame of the hang showed the raised arm, the next showed the T-pose
	// again). A prisoner is a static actor with one animation and nothing else to do, so giving him
	// the tick is the cheapest correct answer.
	PrimaryActorTick.bCanEverTick = true;
	SetActorTickEnabled(true);
	// Setting bCanEverTick after the actor is in the world is not enough on its own: the tick
	// function has to be registered, or the flag is true and nothing ever calls it.
	RegisterAllActorTickFunctions(/*bRegister=*/true, /*bDoComponentRegistration=*/true);
	// Chains and cuffs: instanced cubes, stamped every frame in UpdateHangVisuals.
	const TCHAR* CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, CubePath);
	if (Cube != nullptr)
	{
		ChainInstances = NewObject<UInstancedStaticMeshComponent>(this);
		ChainInstances->SetStaticMesh(Cube);
		ChainInstances->SetMobility(EComponentMobility::Movable);
		ChainInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ChainInstances->RegisterComponent();
		ChainInstances->SetWorldLocation(FVector::ZeroVector);
		FHIMaterials::Tint(ChainInstances, FLinearColor(0.10f, 0.10f, 0.11f));

		CuffInstances = NewObject<UInstancedStaticMeshComponent>(this);
		CuffInstances->SetStaticMesh(Cube);
		CuffInstances->SetMobility(EComponentMobility::Movable);
		CuffInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		CuffInstances->RegisterComponent();
		CuffInstances->SetWorldLocation(FVector::ZeroVector);
		FHIMaterials::Tint(CuffInstances, FLinearColor(0.34f, 0.35f, 0.38f));
	}
	UE_LOG(LogTemp, Display, TEXT("HI: %s taken alive -- hanging in the basement at (%.2f, %.2f)"),
		*CastTag, HangStation(ThiefId).X, HangStation(ThiefId).Y);
}

void AHIIntruder::UpdateHangVisuals()
{
	if (!bHanging || Body == nullptr || ChainInstances == nullptr)
	{
		return;
	}
	ChainInstances->ClearInstances();
	if (CuffInstances != nullptr)
	{
		CuffInstances->ClearInstances();
	}

	// THE CHAINS GO TO THE WRISTS, and the wrist offsets are taken from the POSE HE IS DRAWN IN
	// rather than from the hand bones. Those two disagree here: he is drawn in the reference pose
	// (arms out) because the animation is only ticked by hand, so `GetSocketLocation` reports a
	// frame that is not the one on screen and the chains hung beside him instead of onto him.
	// The reference pose has both arms straight out, so the wrists are a fixed 0.62 m either side
	// of the spine at shoulder height -- measured off the drawn body in shots/hung3_01.png.
	const FVector Base = GetActorLocation();
	const float CeilingZ = (FHISim::BasementZ + 2.05f) * HI_METERS_TO_UU;
	const int32 Links = 6;
	static const TCHAR* const WristBones[2] = { TEXT("LeftHand"), TEXT("RightHand") };
	for (int32 i = 0; i < 2; ++i)
	{
		// The wrists come from the hand BONES of the hang pose -- that is the whole point of posing it
		// as an asset -- with a sanity fallback: a socket more than 1.5 m from the body means the pose
		// is not evaluated, and a chain to nowhere looks worse than a chain to his head.
		const FVector Socket = Body->GetSocketLocation(WristBones[i]);
		const bool bSane = FVector::Dist(Socket, Base) < 1.5f * HI_METERS_TO_UU;
		const float Lean = (i == 0) ? -0.16f : 0.16f;
		const FVector WristPos = bSane
			? Socket
			: FVector(Base.X + Lean * HI_METERS_TO_UU, Base.Y, Base.Z + 1.95f * HI_METERS_TO_UU);
		const FVector Anchor(WristPos.X, WristPos.Y, CeilingZ);
		const FVector Span = WristPos - Anchor;
		const float Len = Span.Size();
		if (Len < 2.0f)
		{
			continue;
		}
		const FVector Dir = Span / Len;
		for (int32 L = 0; L < Links; ++L)
		{
			const float T = (float(L) + 0.5f) / float(Links);
			const FVector At = Anchor + Dir * (Len * T);
			// Alternating roll: what makes a run of boxes read as a chain rather than a stick.
			const FRotator Rot = FRotationMatrix::MakeFromZ(Dir).Rotator()
				+ FRotator(0.0f, (L % 2 == 0) ? 0.0f : 90.0f, 0.0f);
			ChainInstances->AddInstance(FTransform(Rot, At,
				FVector(0.022f, 0.022f, Len / float(Links) * 0.62f)));
		}
		if (CuffInstances != nullptr)
		{
			// The cuff is the thing round the wrist: a short, heavier box, and it is what makes
			// "chained" read as restraint rather than as a rope tied to a hand.
			const FRotator CuffRot = FRotationMatrix::MakeFromZ(Dir).Rotator();
			CuffInstances->AddInstance(FTransform(CuffRot, WristPos,
				FVector(0.085f, 0.085f, 0.070f)));
		}
	}
}
