#include "HIPlayerCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HIIntruder.h"
#include "HICharAnim.h"
#include "HIMaterials.h"
#include "HIUnits.h"

namespace
{
	// The engine's own Mannequin, copied out of the engine's template resources into this
	// project's Content. Paths are the packages' OWN internal names, which is why they read
	// /Game/Characters/... while the source files sat under a template folder of a different
	// shape: an asset refers to its neighbours by package path, so the copy has to land where
	// those references point or every material and skeleton silently goes missing.
	const TCHAR* PlayerMeshPath = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple");
	const TCHAR* ShotgunMeshPath = TEXT("/Game/Weapons/Rifle/Meshes/SM_Rifle.SM_Rifle");
}

AHIPlayerCharacter::AHIPlayerCharacter()
{
	// The game mode is the only ticker. See the class comment.
	PrimaryActorTick.bCanEverTick = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	// The camera IS the root: the sim owns the body's position, and this actor is placed
	// wherever the view goes, so nothing about the body needs to be relative to the view.
	RootComponent = Camera;
	Camera->bUsePawnControlRotation = false;

	// FOV, DERIVED FROM THE ORIGINAL RATHER THAN PICKED. game.gd sets `cam.fov = 68.0`, and
	// Godot's Camera3D reports a VERTICAL field of view (keep_aspect defaults to
	// KEEP_HEIGHT). Unreal's UCameraComponent::FieldOfView is HORIZONTAL. At the project's
	// 16:9 the two are far apart:
	//
	//     2 * atan(tan(34 deg) * 16/9) = 100.3 deg horizontal
	//
	// Unreal's default is 90, which is 59 deg vertical -- a materially narrower view than the
	// original's, and it is why the homeowner's head filled a quarter of the first captured
	// frame while the world the camera was meant to be showing sat off the edges of it.
	//
	// If this ever needs checking against a render, measure it: put two known objects at
	// known distances and compare their apparent separation, rather than trusting this sum.
	Camera->FieldOfView = 100.3f;

	// Created here, filled in BeginPlay. A ConstructorHelpers::FObjectFinder for a /Game
	// asset runs while the class default object is being built, which is a fragile place to
	// depend on project content existing -- and if it is absent the CDO is left permanently
	// meshless rather than merely starting without one.
	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Camera);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// The sim owns every hit. Nothing traces against the homeowner -- the intruder's shots
	// are resolved inside the sim -- so his body is scenery and must never stop a pellet
	// that the sim would have let through.
	Body->SetGenerateOverlapEvents(false);
	Body->SetCastShadow(true);

	Gun = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Gun"));
	Gun->SetupAttachment(Body);
	Gun->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Gun->SetVisibility(false);
}

void AHIPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();
	BuildVisuals();
}

void AHIPlayerCharacter::BuildVisuals()
{
	if (bVisualsBuilt)
	{
		return;
	}
	bVisualsBuilt = true;

	if (USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, PlayerMeshPath))
	{
		// SetSkeletalMeshAsset takes the loaded mesh; the deprecated SetSkeletalMesh would
		// do the same thing and warn.
		Body->SetSkeletalMeshAsset(Mesh);
		// NOTE: he carries the mesh's OWN material, so the flat-colour trick used for every
		// primitive in this port does not apply to a character -- BasicShapeMaterial has no
		// skinning. Telling the two men apart is therefore done by using two DIFFERENT
		// mannequins (Manny for the homeowner, Quinn for the intruders) rather than by tint.
		FHICharAnim::SetupLocomotion(Body);
	}

	SetWeaponVisual(ShownWeapon);
}

void AHIPlayerCharacter::SetWeaponVisual(const FString& Weapon)
{
	ShownWeapon = Weapon;

	// The shotgun is the engine's rifle mesh standing in for one: this project has no
	// shotgun asset, and a long gun in the hand reads as a long gun. Disclosed rather than
	// hidden, because "the shotgun is a rifle" is the kind of thing that should be a line in
	// a file and not a surprise in a screenshot.
	const TCHAR* Path = (Weapon == TEXT("shotgun")) ? ShotgunMeshPath
		: TEXT("/Game/Weapons/Pistol/Meshes/SM_Pistol.SM_Pistol");

	if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Path))
	{
		Gun->SetStaticMesh(Mesh);
		Gun->SetVisibility(true);
		FHICharAnim::AttachPistol(this, Body, Gun);
	}
}

FVector AHIPlayerCharacter::EyePivot(const FHISim::FHIPlayer& P)
{
	// The sim's plane is (X, Y) with Z up, so a 2-D position plus Z is already a world
	// position -- only the units need converting. Eye height is added here rather than
	// stored, so a crouch is a change of constant and not a change of state.
	return FVector(P.Pos.X, P.Pos.Y, P.Z + FHISim::EyeHeight) * HI_METERS_TO_UU;
}

void AHIPlayerCharacter::ViewTransform(const FHISim::FHIPlayer& P, FVector& OutLocation, FVector& OutDirection)
{
	// The offset is scaled with the pivot; the direction is a unit vector and must NOT be.
	OutLocation = EyePivot(P) + FHISim::CameraOffset(P.Yaw, P.Pitch) * HI_METERS_TO_UU;
	OutDirection = FHISim::AimDir(P.Yaw, P.Pitch);
}

FRotator AHIPlayerCharacter::BodyRotation(float SimYaw)
{
	return FRotator(0.0f, 90.0f - FMath::RadiansToDegrees(SimYaw), 0.0f);
}

void AHIPlayerCharacter::ViewTransformCollided(const FHISim::FHIPlayer& P, FVector& OutLocation,
	FVector& OutDirection)
{
	FVector Ideal;
	ViewTransform(P, Ideal, OutDirection);

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		OutLocation = Ideal;
		return;
	}

	const FVector Pivot = EyePivot(P);

	// A sphere rather than a ray, and 12 cm rather than 0: a ray thicker than nothing is
	// what stops the near plane poking through a wall the ray itself passed cleanly beside.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HICamera), /*bTraceComplex=*/false);
	Params.AddIgnoredActor(this);
	// Intruders block the Visibility channel so pellets can find them, but a man walking
	// between the camera and the homeowner must not yank the view forward -- he is not
	// scenery, and the camera sliding in whenever one of them crosses the room is worse
	// than the camera briefly seeing through him.
	for (TActorIterator<AHIIntruder> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}

	FHitResult Hit;
	if (World->SweepSingleByChannel(Hit, Pivot, Ideal, FQuat::Identity, ECC_Visibility,
		FCollisionShape::MakeSphere(12.0f), Params))
	{
		OutLocation = Hit.Location;
	}
	else
	{
		OutLocation = Ideal;
	}
}

void AHIPlayerCharacter::SyncFromSim(const FHISim::FHIPlayer& P)
{
	FVector Location;
	FVector Direction;
	ViewTransformCollided(P, Location, Direction);

	SetActorLocation(Location);
	SetActorRotation(Direction.Rotation());

	// Place the body in WORLD space, not relative to the camera: the camera is behind and
	// above him, so a relative placement would slide him away from his own feet every time
	// the aim moved.
	Body->SetWorldLocation(FVector(P.Pos.X, P.Pos.Y, P.Z) * HI_METERS_TO_UU);
	// He faces the BODY yaw, not the aim and not the camera. With the aim scheme fixed the
	// two agree, and if the cone is ever re-enabled this is the line that keeps his shoulders
	// pointing the way he is walking rather than the way he is glancing.
	Body->SetWorldRotation(BodyRotation(P.Yaw));

	// CAM_FADE, inherited from the Godot original. When the camera has been pulled in so
	// hard that it is inside the homeowner, showing him fills the screen with the inside of
	// his own back; hiding him shows the room, which is the information the player wanted.
	const float BehindHim = FVector::Dist(Location, EyePivot(P)) / HI_METERS_TO_UU;
	Body->SetVisibility(BehindHim > 0.35f);

	// `Vel` is already in metres per second, and it is what makes him lean into a walk
	// instead of skating: the blend space is driven by MEASURED speed, so a man who is
	// accelerating from a standing start animates as one who is accelerating.
	FHICharAnim::DriveLocomotion(Body, P.Vel.Size(), P.bAlive);

	// Dying puts the gun away, so a man on the floor is not still aiming at you.
	Gun->SetVisibility(P.bAlive && Body->IsVisible() && Gun->GetStaticMesh() != nullptr);
}
