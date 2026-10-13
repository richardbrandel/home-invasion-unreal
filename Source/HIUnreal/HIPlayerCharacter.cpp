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
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

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
		// A SWAP IS AN EVENT, so it gets a line of its own rather than being inferred later from
		// a mesh happening to load somewhere else in the log. "The [1]/[2] keys do not switch
		// guns" was investigated from a still frame and a still frame cannot answer it: this
		// line is the one that does, and it costs nothing.
		const bool bSwapped = Gun->GetStaticMesh() != Mesh;
		Gun->SetStaticMesh(Mesh);
		Gun->SetVisibility(true);
		if (bSwapped)
		{
			// The PIVOT matters as much as the length. `SM_Pistol` is 26 cm long with its pivot
			// at its CENTRE, which is why the grip offset is 2 cm and not half the gun; a mesh
			// whose pivot sits at its muzzle or its stock needs a different number, and nothing
			// said so.
			const FBoxSphereBounds Bounds = Mesh->GetBounds();
			UE_LOG(LogTemp, Display,
				TEXT("HI: weapon visual -> %s (mesh %s) extent=(%.1f,%.1f,%.1f)cm pivot=(%.1f,%.1f,%.1f)cm"),
				*Weapon, *Mesh->GetName(),
				Bounds.BoxExtent.X, Bounds.BoxExtent.Y, Bounds.BoxExtent.Z,
				Bounds.Origin.X, Bounds.Origin.Y, Bounds.Origin.Z);
		}
		FHICharAnim::AttachPistol(this, Body, Gun);
	}
}

FVector AHIPlayerCharacter::EyePivot(const FHISim::FHIPlayer& P)
{
	// The sim's plane is (X, Y) with Z up, so a 2-D position plus Z is already a world
	// position -- only the units need converting.
	//
	// CROUCHING LOWERS THE EYE, and this was broken until it was measured: `CrouchEyeHeight`
	// was declared in the sim and referenced NOWHERE, so pressing [C] slowed the homeowner
	// down and left his viewpoint at standing height. The whole point of crouching in a
	// home-defence game is that you cannot be seen over the furniture, and a crouch that
	// changes nothing but your walking speed is not a crouch.
	const float Eye = P.bCrouching ? FHISim::CrouchEyeHeight : FHISim::EyeHeight;
	return FVector(P.Pos.X, P.Pos.Y, P.Z + Eye) * HI_METERS_TO_UU;
}

void AHIPlayerCharacter::ViewTransform(const FHISim::FHIPlayer& P, FVector& OutLocation, FVector& OutDirection)
{
	// DEV SWITCHES, for the same reason the gun offsets have them: framing is judged by looking
	// at it. `-HICamBack=` / `-HICamUp=` / `-HICamSide=`, in metres, the same three the Godot
	// original exposed as `--cam-back` / `--cam-up` / `--cam-side`.
	static bool bCamParsed = false;
	static float CamBack = FHISim::CamBack;
	static float CamUp = FHISim::CamUp;
	static float CamSide = FHISim::CamSide;
	if (!bCamParsed)
	{
		bCamParsed = true;
		const TCHAR* Cmd = FCommandLine::Get();
		FString Value;
		if (FParse::Value(Cmd, TEXT("HICamBack="), Value)) { CamBack = FCString::Atof(*Value); }
		if (FParse::Value(Cmd, TEXT("HICamUp="), Value)) { CamUp = FCString::Atof(*Value); }
		if (FParse::Value(Cmd, TEXT("HICamSide="), Value)) { CamSide = FCString::Atof(*Value); }
	}

	// The offset is scaled with the pivot; the direction is a unit vector and must NOT be.
	OutLocation = EyePivot(P) + FHISim::CameraOffset(P.Yaw, P.Pitch, CamBack, CamUp, CamSide) * HI_METERS_TO_UU;
	OutDirection = FHISim::AimDir(P.Yaw, P.Pitch);
}

FRotator AHIPlayerCharacter::BodyRotation(float SimYaw)
{
	return FRotator(0.0f, 90.0f - FMath::RadiansToDegrees(SimYaw), 0.0f);
}

FRotator AHIPlayerCharacter::WeaponAimRotation(const FVector& Aim)
{
	// THE WEAPON IS AIMED; THE ARM IS ONLY HOLDING IT.
	//
	// This exists because of a measurement, not a theory. `-HIProbe` in the running game, same
	// build, same player, only the pose differing:
	//
	//     standing (MF_Pistol_Idle_ADS):  barrel  3.3 deg off the aim,  rolled  8 deg
	//     walking  (MF_Pistol_Walk_Fwd):  barrel 26-49 deg off the aim, rolled 27-74 deg
	//
	// The pistol-pose LOCOMOTION clips are ordinary walk cycles. The arm swings because the man
	// is walking, not because he is aiming, so the barrel wanders up to fifty degrees off the
	// crosshair and the pistol rolls onto its side. That single measurement is both of the
	// reports that survived the last round of fixes: *"the crosshairs still point to the side"*
	// -- a gun carried forty degrees off IS pointing to the side -- and *"sometimes there is a
	// gun in the defender's hand and sometimes not"*, which is what a pistol rolled seventy
	// degrees and swung behind the forearm looks like from over his shoulder.
	//
	// The honest fix is an upper-body aim offset, and that needs an animation BLUEPRINT. This
	// port deliberately has none -- it evaluates one asset per body through
	// `AnimationSingleNode`, which is why `DriveLocomotion` picks clips by hand in the first
	// place. So the correction is made where it can be made: the weapon's world ROTATION is
	// driven from the aim every frame, and ONLY its rotation. Its position still comes from the
	// hand socket, so the grip stays in the palm however the arm is swinging, and the arm is
	// left alone to animate a walk.
	//
	// A static mesh's local axes ARE its component's axes, and this mesh's barrel is its +Y
	// (asserted in `HI.Unreal.Weapons`). So it is the component's **+Y** that has to be put on
	// the aim -- not its +X, which is what reaching for `SetActorRotation`-style thinking gives.
	const FVector Barrel = Aim.GetSafeNormal();
	if (Barrel.IsNearlyZero())
	{
		return FRotator::ZeroRotator;
	}

	// And roll it upright. A pistol pinned on the aim but lying on its side is still wrong, and
	// the roll is the half of this that no direction check can see.
	const FVector Up = FVector::UpVector;
	FVector GunUp = Up - FVector::DotProduct(Up, Barrel) * Barrel;
	if (!GunUp.Normalize())
	{
		// Looking straight up or straight down: "upright" has no meaning there, so take the
		// world's forward for the third axis rather than returning NaNs.
		GunUp = FVector::ForwardVector - FVector::DotProduct(FVector::ForwardVector, Barrel) * Barrel;
		GunUp.Normalize();
	}
	return FRotationMatrix::MakeFromYZ(Barrel, GunUp).Rotator();
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
	FHICharAnim::DriveLocomotion(Body, P.Vel.Size(), P.bAlive, /*bArmed=*/true);

	// Dying puts the gun away, so a man on the floor is not still aiming at you.
	Gun->SetVisibility(P.bAlive && Body->IsVisible() && Gun->GetStaticMesh() != nullptr);

	// AIM THE WEAPON, NOT THE ARM. See `WeaponAimRotation` for the measurement this answers.
	// Done AFTER the visibility line and BEFORE the probe, so a probe line always describes the
	// rotation the player is actually looking at rather than the raw pose.
	if (Gun->GetStaticMesh() != nullptr)
	{
		Gun->SetWorldRotation(WeaponAimRotation(FHISim::AimDir(P.Yaw, P.Pitch)));
	}

	// ---- `-HIProbe`: WHAT THE CAMERA, THE BODY AND THE GUN ARE ACTUALLY DOING.
	//
	// "The crosshairs still point to the side" and "sometimes there is no gun in his hand"
	// have both now survived a round of fixes argued from the code and confirmed from a single
	// still frame, and neither question can be settled that way:
	//
	//   * a barrel at the wrong ANGLE and a barrel in the wrong PLACE project to the same
	//     picture, so a screenshot cannot tell them apart;
	//   * "no gun" looks identical whether the component is hidden, is BEHIND the man, or is
	//     rolled onto its side against a white arm and a cream wall.
	//
	// So this prints the numbers. The BARREL is the gun component's +Y and not its +X, because
	// a static mesh's local axes ARE its component's axes: GetForwardVector() is the mesh's +X
	// and GetRightVector() is its +Y, which is the barrel. GetUpVector() is the half nobody has
	// measured yet, and it is the difference between a pistol held upright and one lying on its
	// side -- a rolled pistol still has its barrel on the aim.
	static const bool bProbe = FParse::Param(FCommandLine::Get(), TEXT("HIProbe"));
	if (bProbe && Gun != nullptr && Camera != nullptr)
	{
		const float Dt = GetWorld() != nullptr ? GetWorld()->GetDeltaSeconds() : 0.0f;
		ProbeTime += Dt;
		// A SWAP LOGS AT ONCE. Waiting up to a second for the line is how a capture taken right
		// after the key press comes back describing the weapon before it.
		const bool bChanged = ShownWeapon != LastProbeWeapon;
		if (ProbeTime >= 1.0f || bChanged)
		{
			ProbeTime = 0.0f;
			LastProbeWeapon = ShownWeapon;

			const FVector CamFwd = Camera->GetForwardVector();
			const FVector CamLoc = Camera->GetComponentLocation();
			const FVector BodyFwd = Body != nullptr ? Body->GetForwardVector() : FVector::ZeroVector;
			const FVector GunLoc = Gun->GetComponentLocation();
			const FVector Barrel = Gun->GetRightVector();
			const FVector GunUp = Gun->GetUpVector();
			const FVector ToGun = GunLoc - CamLoc;
			// How far the barrel is off the direction the player is looking. Under the fixed aim
			// scheme this should be the hand's own few degrees, not tens.
			const float BarrelVsAimDeg = FMath::RadiansToDegrees(FMath::Acos(
				FMath::Clamp(FVector::DotProduct(Barrel.GetSafeNormal(), CamFwd), -1.0f, 1.0f)));
			// How far the gun has rolled off the world's up. 0 means upright; 90 means it is on
			// its side, which is what "held sideways" looks like and what the barrel angle alone
			// cannot see.
			const float GunRollDeg = FMath::RadiansToDegrees(FMath::Acos(
				FMath::Clamp(FVector::DotProduct(GunUp.GetSafeNormal(), FVector::UpVector), -1.0f, 1.0f)));
			UE_LOG(LogTemp, Display,
				TEXT("HI: probe weapon=%s mesh=%s bodyVis=%d gunVis=%d behind=%.2f ")
				TEXT("barrelVsAim=%.1fdeg gunRollFromUp=%.1fdeg gunDist=%.2f inFrontOfCamera=%d"),
				*ShownWeapon,
				(Gun->GetStaticMesh() != nullptr ? *Gun->GetStaticMesh()->GetName() : TEXT("NONE")),
				(Body != nullptr && Body->IsVisible()) ? 1 : 0,
				Gun->IsVisible() ? 1 : 0,
				BehindHim,
				BarrelVsAimDeg, GunRollDeg,
				ToGun.Size() / HI_METERS_TO_UU,
				(FVector::DotProduct(ToGun.GetSafeNormal(), CamFwd) > 0.0f) ? 1 : 0);
			UE_LOG(LogTemp, Display,
				TEXT("HI: probe   camfwd=(%.2f,%.2f,%.2f) bodyfwd=(%.2f,%.2f,%.2f) barrel=(%.2f,%.2f,%.2f) gunup=(%.2f,%.2f,%.2f)"),
				CamFwd.X, CamFwd.Y, CamFwd.Z,
				BodyFwd.X, BodyFwd.Y, BodyFwd.Z,
				Barrel.X, Barrel.Y, Barrel.Z,
				GunUp.X, GunUp.Y, GunUp.Z);
		}
	}
}
