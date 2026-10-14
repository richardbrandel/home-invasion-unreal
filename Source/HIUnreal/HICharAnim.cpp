#include "HICharAnim.h"

#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimationAsset.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "HIUnits.h"

// ---------------------------------------------------------------- the cast

const TCHAR* FHICharAnim::PlayerTag()
{
	return TEXT("louise");
}

const TCHAR* FHICharAnim::IntruderTagForId(int32 Id)
{
	// Three named men, cycling: the default round is three intruders, so within a
	// default round no two men share a face. A bigger --thieves count reuses faces,
	// which is the honest failure mode -- inventing unnamed men would be worse.
	static const TCHAR* Cast[] = { TEXT("james"), TEXT("brute"), TEXT("martha") };
	return Cast[((Id % 3) + 3) % 3];
}

FString FHICharAnim::CharacterDir(const TCHAR* Tag)
{
	return FString::Printf(TEXT("/Game/HIImported/characters_%s/"), Tag);
}

FString FHICharAnim::MeshPathFor(const TCHAR* Tag)
{
	// The mesh-carrying export (`idle.fbx`) imports its skeletal mesh as `idle`.
	return CharacterDir(Tag) + TEXT("idle.idle");
}

FString FHICharAnim::ClipPathFor(const TCHAR* Tag, const TCHAR* Clip)
{
	return CharacterDir(Tag) + FString::Printf(TEXT("%s.%s"), Clip, Clip);
}

const TCHAR* FHICharAnim::ClipAim()        { return TEXT("aim"); }
const TCHAR* FHICharAnim::ClipWalk()       { return TEXT("walk"); }
const TCHAR* FHICharAnim::ClipRun()        { return TEXT("run"); }
const TCHAR* FHICharAnim::ClipCrouchIdle() { return TEXT("crouch_idle"); }
const TCHAR* FHICharAnim::ClipCrouchWalk() { return TEXT("crouch_walk"); }
const TCHAR* FHICharAnim::ClipIdle()       { return TEXT("idle_Anim"); }
const TCHAR* FHICharAnim::ClipPistolIdle() { return TEXT("pistol_idle"); }
const TCHAR* FHICharAnim::ClipReload()     { return TEXT("reload"); }
const TCHAR* FHICharAnim::ClipShoot()      { return TEXT("shoot"); }
const TCHAR* FHICharAnim::ClipDeath()      { return TEXT("death"); }

float FHICharAnim::AuthoredCmPerSec(const TCHAR* Tag, const TCHAR* Clip)
{
	// Travel per clip length, measured by UHIAnimTool from the Hips keys of the imported
	// clips BEFORE it pinned them (import run 2026-10-13; every line in
	// /tmp/hi_import_assets.log's normalize report). The same walk clip travels a
	// different distance per character because Mixamo scales travel to the body:
	// brute is ~23% longer-strided than james. The Godot probe, measuring the same FBX
	// files on its side, agrees with Louise's row to the centimetre (174.8 cm walk,
	// 355.2 cm run, 152.9 cm crouch_walk).
	//
	//           walk    run     crouch_walk   (cm/s; everything else does not travel)
	// louise   169.2   561.1    89.9
	// james    163.1   540.8    86.7
	// brute    204.8   679.1   108.9
	// martha   163.1   540.6    86.6
	struct FRow { const TCHAR* Tag; float Walk, Run, CrouchWalk; };
	static const FRow Rows[] = {
		{ TEXT("louise"), 169.2f, 561.1f,  89.9f },
		{ TEXT("james"),  163.1f, 540.8f,  86.7f },
		{ TEXT("brute"),  204.8f, 679.1f, 108.9f },
		{ TEXT("martha"), 163.1f, 540.6f,  86.6f },
	};
	for (const FRow& R : Rows)
	{
		if (Tag != nullptr && FCString::Strcmp(Tag, R.Tag) == 0)
		{
			if (FCString::Strcmp(Clip, ClipWalk()) == 0)       { return R.Walk; }
			if (FCString::Strcmp(Clip, ClipRun()) == 0)        { return R.Run; }
			if (FCString::Strcmp(Clip, ClipCrouchWalk()) == 0) { return R.CrouchWalk; }
			return 0.0f;
		}
	}
	// An unknown tag has no measured row. Guessing a stride would be worse than not
	// matching one: the clip plays at rate 1 and the feet skate, which is visible and
	// fixable, instead of strobing at a made-up rate, which is also visible and is a lie.
	return 0.0f;
}

// ------------------------------------------------------------- action latch

namespace
{
	/**
	 * Which bodies are mid-one-shot, and for how long the clip may hold them.
	 *
	 * A static map because `FHICharAnim` is a struct of statics with nowhere per-body
	 * to hang state, and the components are owned by exactly two actors that outlive
	 * the latch entries. WEAK pointers, so a destroyed body cannot be resurrected by a
	 * stale key, and every touch purges the dead -- the map only ever holds a handful
	 * of entries (one per body), so the purge costs nothing.
	 */
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, float>& ActionLatches()
	{
		static TMap<TWeakObjectPtr<USkeletalMeshComponent>, float> Latches;
		return Latches;
	}

	void PurgeDeadLatches()
	{
		TMap<TWeakObjectPtr<USkeletalMeshComponent>, float>& Latches = ActionLatches();
		for (auto It = Latches.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				It.RemoveCurrent();
			}
		}
	}
}

// ---------------------------------------------------------------- driving

void FHICharAnim::SetupCharacter(USkeletalMeshComponent* Mesh, const TCHAR* Tag)
{
	if (Mesh == nullptr || Mesh->GetSkeletalMeshAsset() == nullptr || Tag == nullptr)
	{
		return;
	}
	// The starting clip is `pistol_idle` because everyone spawns armed and standing, so the
	// calm ready is the right first frame (2026-10-15: `aim` was dropped from selection --
	// see DriveCharacter). The first DriveCharacter tick would replace a different start
	// anyway; starting in it keeps "aim is deliberately never played" literally true.
	const FString Start = ClipPathFor(Tag, ClipPistolIdle());
	UAnimationAsset* Clip = LoadObject<UAnimationAsset>(nullptr, *Start);
	if (Clip == nullptr)
	{
		// NOT SILENT. A character whose clips did not import stands in the reference
		// pose forever, and "the man is a statue" without this line looks like an
		// animation bug rather than a missing asset.
		UE_LOG(LogTemp, Warning, TEXT("HI: no starting clip at %s -- %s will hold the reference pose"),
			*Start, Tag);
		return;
	}
	// Single node is the mode that evaluates ONE asset with no graph. Setting it
	// explicitly matters: a mesh left in AnimationBlueprint mode with no AnimBlueprint
	// assigned evaluates nothing at all and holds the reference pose, which looks like
	// the character failing to load rather than the mode being wrong.
	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Mesh->SetAnimation(Clip);
	Mesh->Play(true);
}

void FHICharAnim::DriveCharacter(USkeletalMeshComponent* Mesh, const TCHAR* Tag, float SpeedMps,
	bool bAlive, bool bCrouching, bool bArmed)
{
	if (Mesh == nullptr || Tag == nullptr || !bAlive)
	{
		return;
	}
	UAnimSingleNodeInstance* Single = Mesh->GetSingleNodeInstance();
	if (Single == nullptr)
	{
		return;
	}

	// ---- A ONE-SHOT IN PROGRESS OWNS THE BODY until it ends or its hold expires.
	// Both conditions, because they fail differently: a non-looping single node stops
	// at the last key, and a clip longer than its mechanic (reload: 3.3 s against a
	// 1.15 s magazine change) must be cut off even while still playing.
	{
		TMap<TWeakObjectPtr<USkeletalMeshComponent>, float>& Latches = ActionLatches();
		if (const float* Hold = Latches.Find(Mesh))
		{
			const bool bRunning = Single->IsPlaying()
				&& Single->GetCurrentTime() < (*Hold - KINDA_SMALL_NUMBER);
			if (bRunning)
			{
				return;
			}
			Latches.Remove(Mesh);
		}
	}

	// ---- CLIP SELECTION. Thresholds in cm/s against the sim's own speeds; see the header.
	//
	// THE ARMED STANDING POSE IS `pistol_idle` (Mixamo "Pistol Idle": a calm 8.3 s ready,
	// pistol gripped up near the chest, locomotion-pinned so it travels nowhere). The unarmed
	// standing `idle` is for the one body that stands with both hands full -- an intruder
	// mid-carry, whose gun is hidden. This is the THIRD pose this choice has had, and the
	// header plus PARITY.md carry the whole aim(looped) -> idle(gun too low, read as unarmed)
	// -> pistol_idle history; the point here is only that `aim` is deliberately never played
	// and that locomotion and crouch are weapon-agnostic, because no pistol-walk or
	// pistol-crouch clip exists -- the gun rides low in the relaxed walk hand, as the Godot
	// original's intruders did.
	const float Cm = SpeedMps * HI_METERS_TO_UU;
	const TCHAR* Want;
	if (bCrouching)
	{
		Want = (Cm < 15.0f) ? ClipCrouchIdle() : ClipCrouchWalk();
	}
	else if (Cm < 15.0f)
	{
		Want = bArmed ? ClipPistolIdle() : ClipIdle();
	}
	else
	{
		Want = (Cm < 190.0f) ? ClipWalk() : ClipRun();
	}

	const FString WantPath = ClipPathFor(Tag, Want);
	UAnimationAsset* WantAsset = LoadObject<UAnimationAsset>(nullptr, *WantPath);
	if (WantAsset == nullptr)
	{
		// A missing clip must not leave the body frozen for ever; the reference pose
		// plus the SetupCharacter warning is the degraded state, logged once there.
		return;
	}

	if (Single->GetCurrentAsset() != WantAsset)
	{
		// Only on a CHANGE, or the sequence restarts every frame and the man moonwalks
		// in place: SetAnimation resets the playhead, the same trap the death handover
		// and the mannequin-era code both had to avoid.
		Mesh->SetAnimation(WantAsset);
		Mesh->Play(true);
		if (UAnimSingleNodeInstance* Fresh = Mesh->GetSingleNodeInstance())
		{
			// Item 15's phase variation: three men must not march in step. Random
			// start inside the loop, and honest about what it is not -- phase
			// variation, not pose variation; the pose set is the ten clips.
			const float Length = Fresh->GetLength();
			if (Length > 0.0f)
			{
				Fresh->SetPosition(FMath::FRand() * Length, /*bFireNotifies=*/false);
			}
		}
	}

	// ---- STRIDE MATCHING, the half of fix-list item 14 that needs no anim graph:
	// rate = ground speed over the speed THIS CHARACTER's clip was authored at (the
	// travel is per body -- Mixamo scales it -- so the table is per tag), clamped so a
	// body shoved backwards, a laden thief at 0.5 m/s or a sprint past the top of the
	// set does not strobe. Clips with no authored travel (idle, aim, crouch_idle) play
	// at rate 1 -- dividing by zero to "match" a clip that goes nowhere is meaningless.
	const float Authored = AuthoredCmPerSec(Tag, Want);
	const float Rate = (Authored > 0.0f)
		? FMath::Clamp(Cm / Authored, 0.35f, 1.8f)
		: 1.0f;
	Single->SetPlayRate(Rate);
}

void FHICharAnim::PlayAction(USkeletalMeshComponent* Mesh, const TCHAR* Tag, const TCHAR* Clip,
	float HoldSec)
{
	if (Mesh == nullptr || Tag == nullptr || Clip == nullptr)
	{
		return;
	}
	UAnimationAsset* Asset = LoadObject<UAnimationAsset>(nullptr, *ClipPathFor(Tag, Clip));
	if (Asset == nullptr)
	{
		return;
	}
	PurgeDeadLatches();

	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Mesh->SetAnimation(Asset);
	// NON-LOOPING, and from the FIRST frame -- a re-fired shot restarts the burst,
	// which is what a trigger pull does; a random phase here would play the middle of
	// a recoil nobody caused.
	Mesh->Play(false);
	ActionLatches().Add(Mesh, FMath::Max(HoldSec, 0.05f));
}

void FHICharAnim::PlayDeath(USkeletalMeshComponent* Mesh, const TCHAR* Tag)
{
	if (Mesh == nullptr || Tag == nullptr)
	{
		return;
	}
	UAnimationAsset* Asset = LoadObject<UAnimationAsset>(nullptr, *ClipPathFor(Tag, ClipDeath()));
	if (Asset == nullptr)
	{
		// SAY SO. A missing death clip leaves the body standing in its last living pose,
		// which from the player's chair is indistinguishable from "the death was never
		// triggered" -- the exact ambiguity that made the 2026-10-14 empty-hands report
		// cost two days of camera and asset checks before anyone looked at the corpse.
		UE_LOG(LogTemp, Warning, TEXT("HI: no death clip for '%s' at %s -- the body will NOT fall"),
			Tag, *ClipPathFor(Tag, ClipDeath()));
		return;
	}
	// A death cancels any action latch: the corpse must not be "still reloading" as far
	// as the latch is concerned, and nothing may resume locomotion on it afterwards --
	// DriveCharacter's bAlive guard is the other half of that.
	ActionLatches().Remove(Mesh);
	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Mesh->SetAnimation(Asset);
	Mesh->Play(false);
	UE_LOG(LogTemp, Display, TEXT("HI: death clip started for '%s'"), Tag);
}

// ---------------------------------------------------------------- facing

float FHICharAnim::MeshFacingYawDeg()
{
	// MEASURED, 2026-10-13, by HI.Unreal.Cast from each character's bind-pose shoulders
	// (RightShoulder - LeftShoulder is lateral; lateral x up is the facing):
	// louise 90.1, james 90.1, brute 90.0, martha 89.9. The imported Mixamo meshes face
	// their own **+Y** -- the same axis the mannequin faced, so BodyRotationFor reduces
	// to the mannequin era's measured t = -a. The pre-measurement guess in this spot was
	// 0 (+X), from "a Y-up FBX's +Z becomes the engine's +X"; it was wrong, and wrong by
	// exactly the quarter turn that 2026-10-13's crosshair report had just cost a
	// playtest. That is why this is a number with a measurement attached, and why the
	// Cast test fails with the measured value in its message if an asset ever changes it.
	return 90.0f;
}

FVector FHICharAnim::MeshForwardAxis()
{
	const float Rad = FMath::DegreesToRadians(MeshFacingYawDeg());
	return FVector(FMath::Cos(Rad), FMath::Sin(Rad), 0.0f);
}

FRotator FHICharAnim::BodyRotationFor(float SimYaw)
{
	// t = 90 - a - facing, in degrees. See the header for the derivation and the
	// measurement chain; for the mannequin's facing of +90 this is exactly the t = -a
	// that `-HIProbe` measured into existence on 2026-10-13.
	return FRotator(0.0f,
		90.0f - FMath::RadiansToDegrees(SimYaw) - MeshFacingYawDeg(),
		0.0f);
}

// --------------------------------------------------------------- weapons

const TCHAR* FHICharAnim::PistolMeshPath()
{
	return TEXT("/Game/Weapons/Pistol/Meshes/SM_Pistol.SM_Pistol");
}

FName FHICharAnim::HandBoneFor(const USkeletalMeshComponent* Mesh)
{
	// Two rigs have held a gun in this project: the engine mannequin (`hand_r`) and the
	// Mixamo cast (`RightHand`, the prefix stripped by the importer -- the SOURCE files
	// say `mixamorig8:RightHand` for Louise and `mixamorig9:` for James, which is why
	// guessing the prefix was forbidden and why the suffix is what gets matched).
	if (Mesh == nullptr)
	{
		return NAME_None;
	}
	if (Mesh->DoesSocketExist(TEXT("hand_r")))
	{
		return FName(TEXT("hand_r"));
	}
	if (Mesh->DoesSocketExist(TEXT("RightHand")))
	{
		return FName(TEXT("RightHand"));
	}
	return NAME_None;
}

UStaticMeshComponent* FHICharAnim::AttachPistol(
	AActor* Owner, USkeletalMeshComponent* Mesh, UStaticMeshComponent* Pistol)
{
	if (Owner == nullptr || Mesh == nullptr || Pistol == nullptr)
	{
		return nullptr;
	}
	// A socket that does not exist must not be guessed at: AttachToComponent with a
	// missing socket falls back to the component's origin, which reads as a pistol
	// embedded in the character's pelvis and is easy to mistake for a modelling problem.
	const FName Hand = HandBoneFor(Mesh);
	if (!Hand.IsNone())
	{
		Pistol->AttachToComponent(
			Mesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Hand);
		// ---- THE MANNEQUIN'S SOCKET +Y WAS THE POINTING DIRECTION, and the identity
		// rotation aligned the barrel (the mesh's +Y, asserted in `HI.Unreal.Weapons`)
		// with it, with a 2 cm offset seating the grip in the palm. A Mixamo hand BONE
		// has its own axes, which nobody has measured yet, so the honest defaults are
		// identity and ZERO offset -- the grip at the wrist -- and the dev switches
		// below plus captured frames are how the seat-in-the-hand gets tuned, the same
		// way it was tuned for the mannequin. Both actors overwrite the weapon's WORLD
		// rotation from the aim every frame anyway (`WeaponAimRotation`), so what the
		// relative rotation decides is where the grip sits, not where the barrel points.
		Pistol->SetRelativeRotation(FRotator(0.0f, 0.0f, 0.0f));
		Pistol->SetRelativeLocation(FVector::ZeroVector);
		Pistol->SetRelativeScale3D(FVector(1.0f));

		// ---- DEV SWITCHES, for the same reason they exist for the camera: a socket
		// offset is judged by LOOKING at it.
		//
		//     -HIGunLoc=X,Y,Z   -HIGunRot=Pitch,Yaw,Roll   -HIGunScale=S
		//
		// `bShouldStopOnSeparator=false` IS LOAD-BEARING. With the default,
		// `FParse::Value` stops at the comma, so "0,-90,0" parses as "0" and every one
		// of these switches silently did NOTHING -- which is how a whole sweep of
		// candidate rotations produced identical measurements and looked like evidence
		// about the rig. It was evidence about the parser.
		{
			const TCHAR* Cmd = FCommandLine::Get();
			FString LocS, RotS, ScaleS;
			if (FParse::Value(Cmd, TEXT("HIGunLoc="), LocS, /*bShouldStopOnSeparator=*/false))
			{
				TArray<FString> P;
				LocS.ParseIntoArray(P, TEXT(","), true);
				if (P.Num() == 3)
				{
					Pistol->SetRelativeLocation(FVector(
						FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2])));
				}
			}
			if (FParse::Value(Cmd, TEXT("HIGunRot="), RotS, /*bShouldStopOnSeparator=*/false))
			{
				TArray<FString> P;
				RotS.ParseIntoArray(P, TEXT(","), true);
				if (P.Num() == 3)
				{
					Pistol->SetRelativeRotation(FRotator(
						FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2])));
				}
			}
			if (FParse::Value(Cmd, TEXT("HIGunScale="), ScaleS, /*bShouldStopOnSeparator=*/false))
			{
				Pistol->SetRelativeScale3D(FVector(FCString::Atof(*ScaleS)));
			}
		}
		return Pistol;
	}
	// NOT SILENT. When this returned nullptr without a word, the pistol stayed parented
	// to the mesh's own origin -- between the man's feet -- and nothing anywhere said
	// why.
	UE_LOG(LogTemp, Warning,
		TEXT("HI: %s has no 'hand_r'/'RightHand' socket or bone, so the weapon cannot be held"),
		*Owner->GetName());
	return nullptr;
}
