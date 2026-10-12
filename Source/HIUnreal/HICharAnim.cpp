#include "HICharAnim.h"

#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimationAsset.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "HIUnits.h"

const TCHAR* FHICharAnim::LocomotionPath()
{
	return TEXT("/Game/Characters/Mannequins/Anims/Unarmed/BS_Idle_Walk_Run.BS_Idle_Walk_Run");
}

const TCHAR* FHICharAnim::PistolMeshPath()
{
	return TEXT("/Game/Weapons/Pistol/Meshes/SM_Pistol.SM_Pistol");
}

void FHICharAnim::SetupLocomotion(USkeletalMeshComponent* Mesh)
{
	if (Mesh == nullptr || Mesh->GetSkeletalMeshAsset() == nullptr)
	{
		return;
	}
	UAnimationAsset* Blend = LoadObject<UAnimationAsset>(nullptr, LocomotionPath());
	if (Blend == nullptr)
	{
		return;
	}
	// Single node is the mode that evaluates ONE asset with no graph. Setting it explicitly
	// matters: a mesh left in AnimationBlueprint mode with no AnimBlueprint assigned
	// evaluates nothing at all and holds the reference pose, which looks like the character
	// failing to load rather than the mode being wrong.
	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Mesh->SetAnimation(Blend);
	Mesh->Play(true);

	// IDLE VARIATION, or as much of fix-list item 15 as can be had without more clips.
	//
	// Every body shares one idle clip, so without this they stand in the SAME POSE at the
	// SAME MOMENT -- three men breathing in unison, which is the single most artificial thing
	// a crowd of identical rigs can do. Starting each loop at a different phase means that at
	// any instant they are at different points in the cycle, so they read as three men rather
	// than one man drawn three times.
	//
	// It is honest to say what this is NOT: it is phase variation, not pose variation. A
	// genuinely different idle -- shifting weight, checking a phone, glancing at the door --
	// needs more recorded clips, and the engine ships exactly one.
	if (UAnimSingleNodeInstance* Single = Mesh->GetSingleNodeInstance())
	{
		const float Length = Single->GetLength();
		if (Length > 0.0f)
		{
			Single->SetPosition(FMath::FRand() * Length, /*bFireNotifies=*/false);
		}
	}
}

const TCHAR* FHICharAnim::PistolIdlePath()
{
	return TEXT("/Game/Characters/Mannequins/Anims/Pistol/MF_Pistol_Idle_ADS.MF_Pistol_Idle_ADS");
}

const TCHAR* FHICharAnim::PistolWalkPath()
{
	return TEXT("/Game/Characters/Mannequins/Anims/Pistol/Walk/MF_Pistol_Walk_Fwd.MF_Pistol_Walk_Fwd");
}

const TCHAR* FHICharAnim::PistolJogPath()
{
	return TEXT("/Game/Characters/Mannequins/Anims/Pistol/Jog/MF_Pistol_Jog_Fwd.MF_Pistol_Jog_Fwd");
}

void FHICharAnim::DriveLocomotion(USkeletalMeshComponent* Mesh, float SpeedMps, bool bAlive,
	bool bArmed)
{
	if (Mesh == nullptr || !bAlive)
	{
		return;
	}
	UAnimSingleNodeInstance* Single = Mesh->GetSingleNodeInstance();
	if (Single == nullptr)
	{
		return;
	}

	if (bArmed)
	{
		// ---- ARMED: pick the pistol-pose clip by speed, because there is no blend space to
		// pick it for us. See the header. The thresholds are the clips' own authored speeds,
		// in cm/s: standing, walking, and the jog this port calls a run.
		const float ArmedCm = SpeedMps * HI_METERS_TO_UU;
		const TCHAR* Want = (ArmedCm < 15.0f) ? PistolIdlePath()
			: (ArmedCm < 190.0f ? PistolWalkPath() : PistolJogPath());
		UAnimationAsset* WantAsset = LoadObject<UAnimationAsset>(nullptr, Want);
		if (WantAsset != nullptr)
		{
			// Only on a CHANGE, or the sequence is restarted every frame and the man moonwalks
			// in place: SetAnimation resets the playhead, which is the same trap the death
			// handover had to avoid.
			if (Single->GetCurrentAsset() != WantAsset)
			{
				Mesh->SetAnimation(WantAsset);
				Mesh->Play(true);
				if (UAnimSingleNodeInstance* Fresh = Mesh->GetSingleNodeInstance())
				{
					// Item 15's phase variation, same as the idle loop: three men must not
					// march in step.
					const float Len = Fresh->GetLength();
					if (Len > 0.0f)
					{
						Fresh->SetPosition(FMath::FRand() * Len, /*bFireNotifies=*/false);
					}
				}
			}
			// Stride matching, the same rule as the unarmed path but against each clip's own
			// authored speed -- a walk clip at 400 cm/s would skate badly.
			const float Reference = (Want == PistolIdlePath()) ? 150.0f
				: (Want == PistolWalkPath() ? 150.0f : 400.0f);
			Single->SetPlayRate(FMath::Clamp(ArmedCm / Reference, 0.6f, 1.6f));
			return;
		}
		// A missing clip must not leave the body frozen in the pistol pose for ever.
		UE_LOG(LogTemp, Warning, TEXT("HI: no pistol locomotion clip at %s"), Want);
	}
	// X is the blend space's Speed axis and Y its Direction axis. Direction stays 0: the sim
	// turns the BODY to face where it is going, so a body is never strafing sideways in its
	// own frame -- forward is the only direction the blend space ever needs to be told about.
	const float SpeedCm = SpeedMps * HI_METERS_TO_UU;
	Single->SetBlendSpacePosition(FVector(SpeedCm, 0.0f, 0.0f));

	// STRIDE MATCHING, which is the half of "the feet skate" that needs no anim graph. The
	// blend space picks a clip by speed; it does not make that clip's stride cover the ground
	// the body is actually crossing. The mannequin's jog is authored at roughly 400 cm/s, so
	// the rate is the ratio -- clamped, because a body at 6.6 m/s sprinting past the top of
	// the blend, or one shoved backwards at a negative speed, must not strobe the clip.
	constexpr float ReferenceCmPerSec = 400.0f;
	const float Rate = FMath::Clamp(SpeedCm / ReferenceCmPerSec, 0.35f, 1.8f);
	Single->SetPlayRate(Rate);
}

void FHICharAnim::PlayOneShot(USkeletalMeshComponent* Mesh, const TCHAR* SequencePath)
{
	if (Mesh == nullptr)
	{
		return;
	}
	if (SequencePath == nullptr)
	{
		SetupLocomotion(Mesh);
		return;
	}
	UAnimationAsset* Clip = LoadObject<UAnimationAsset>(nullptr, SequencePath);
	if (Clip == nullptr)
	{
		return;
	}
	// Explicitly single node again: a caller may switch to a one-shot from a state where the
	// mode was never set, and in AnimationBlueprint mode this would silently do nothing.
	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Mesh->SetAnimation(Clip);
	Mesh->Play(false);
}

UStaticMeshComponent* FHICharAnim::AttachPistol(
	AActor* Owner, USkeletalMeshComponent* Mesh, UStaticMeshComponent* Pistol)
{
	if (Owner == nullptr || Mesh == nullptr || Pistol == nullptr)
	{
		return nullptr;
	}
	// A socket that does not exist must not be guessed at: AttachToComponent with a missing
	// socket falls back to the component's origin, which reads as a pistol embedded in the
	// character's pelvis and is easy to mistake for a modelling problem.
	if (Mesh->DoesSocketExist(TEXT("hand_r")))
	{
		Pistol->AttachToComponent(
			Mesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("hand_r"));
		// ---- THE PISTOL'S BARREL IS ITS +Y, AND SO IS THE SOCKET'S.
		//
		// Reported from play: *"the crosshairs are pointing one way and the defender another"*.
		// Measured in the running game, at the aiming pose:
		//
		//     camera forward yaw 90.0 | socket X 14.0 | socket Y 100.5 | socket Z 179.3
		//
		// The socket's +Y is the direction the hand points, so mapping the mesh's barrel onto it
		// is the IDENTITY rotation. An earlier version used a yaw of -90, which maps the barrel
		// onto the socket's +X -- the sideways axis -- and that is exactly what a player sees: the
		// pistol held out horizontally to the man's right while the crosshair points down the room.
		//
		// `SM_Pistol` is 26 cm long and its PIVOT IS AT ITS CENTRE, so the offset along the barrel
		// is half its length: that puts the grip in the palm instead of the pivot at the wrist.
		Pistol->SetRelativeRotation(FRotator(0.0f, 0.0f, 0.0f));
		Pistol->SetRelativeLocation(FVector(0.0f, 13.0f, 0.0f));
		Pistol->SetRelativeScale3D(FVector(1.0f));

		// ---- DEV SWITCHES, for the same reason they exist for the camera: a socket offset is
		// judged by LOOKING at it.
		//
		//     -HIGunLoc=X,Y,Z   -HIGunRot=Pitch,Yaw,Roll   -HIGunScale=S
		//
		// `bShouldStopOnSeparator=false` IS LOAD-BEARING. With the default, `FParse::Value` stops
		// at the comma, so "0,-90,0" parses as "0" and every one of these switches silently did
		// NOTHING -- which is how a whole sweep of candidate rotations produced identical
		// measurements and looked like evidence about the rig. It was evidence about the parser.
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
	// NOT SILENT. When this returned nullptr without a word, the pistol stayed parented to the
	// mesh's own origin -- between the man's feet -- and nothing anywhere said why.
	UE_LOG(LogTemp, Warning,
		TEXT("HI: %s has no 'hand_r' socket or bone, so the weapon cannot be held"),
		*Owner->GetName());
	return nullptr;
}
