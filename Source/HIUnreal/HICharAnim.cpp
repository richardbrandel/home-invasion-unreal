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
	Single->SetBlendSpacePosition(FVector(SpeedMps * HI_METERS_TO_UU, 0.0f, 0.0f));
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
