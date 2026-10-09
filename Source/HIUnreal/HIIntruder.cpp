#include "HIIntruder.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "HICharAnim.h"
#include "HIUnits.h"

namespace
{
	// A DIFFERENT mannequin from the homeowner's, and that is the point: with one mesh for
	// both, three men coming through the door would be indistinguishable from each other and
	// from you. Fix-list item 5's "walls" and item 2's "characters" are separate items; being
	// able to tell who is who is its own requirement and this is the cheapest way to meet it.
	const TCHAR* IntruderMeshPath = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple");
	const TCHAR* DeathPath = TEXT("/Game/Characters/Mannequins/Anims/Death/MM_Death_Back_01.MM_Death_Back_01");
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
