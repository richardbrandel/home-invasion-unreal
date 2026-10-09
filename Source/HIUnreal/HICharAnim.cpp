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

void FHICharAnim::DriveLocomotion(USkeletalMeshComponent* Mesh, float SpeedMps, bool bAlive)
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
		// The mannequin's hand socket points along the forearm; the pistol is modelled with
		// its barrel down -Y and its grip at the origin, so it is turned in the hand rather
		// than left at whatever angle the socket happens to have. Values chosen from a
		// render, not derived -- a socket's conventions are asset-specific.
		Pistol->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));
		Pistol->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
		Pistol->SetRelativeScale3D(FVector(1.0f));
		Pistol->SetVisibility(true);
		return Pistol;
	}
	return nullptr;
}
