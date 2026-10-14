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
	// The shotgun is CODE-BUILT, and has been since 2026-10-14: tools/make-shotgun-glb.py
	// writes eight boxes and two cylinders into a .glb -- barrel along the mesh's +Y, pivot
	// at the grip centre, the exact contract HI.Unreal.Weapons asserts -- and the ordinary
	// import turnstile (drop it in SourceAssets/, run tools/import-assets.sh) makes this
	// asset. It replaced the engine's rifle mesh, which stood in for the shotgun and was
	// disclosed as a stand-in everywhere until the day it stopped being true. The BODY is
	// not the engine's mannequin any more either: the cast became the Mixamo characters on
	// 2026-10-13, and Louise is the homeowner -- the same woman the Godot version used,
	// chosen by Richard. Her mesh and clips resolve per character through FHICharAnim,
	// because each character's clips bind ONLY to the skeleton its own mesh-carrying
	// export created.
	const TCHAR* ShotgunMeshPath = TEXT("/Game/HIImported/weapons/shotgun/StaticMeshes/shotgun.shotgun");
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

	CastTag = FHICharAnim::PlayerTag();
	const FString MeshPath = FHICharAnim::MeshPathFor(*CastTag);
	if (USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath))
	{
		// SetSkeletalMeshAsset takes the loaded mesh; the deprecated SetSkeletalMesh would
		// do the same thing and warn.
		Body->SetSkeletalMeshAsset(Mesh);
		// NOTE: she carries the mesh's OWN materials -- the imported Mixamo textures and
		// the Ch07_body/Ch07_hair materials beside them -- so the flat-colour trick used
		// for every primitive in this port does not apply to a character. Telling the two
		// sides apart no longer needs tint or two mannequins at all: the homeowner is
		// Louise and the intruders are three named men.
		FHICharAnim::SetupCharacter(Body, *CastTag);
	}
	else
	{
		// NOT SILENT, and not fatal: the round still plays around a reference-pose body.
		// The usual cause is a Content/ that predates tools/import-assets.py -- the fix
		// is to run it, not to rebuild.
		UE_LOG(LogTemp, Warning, TEXT("HI: the homeowner's mesh did not load from %s"), *MeshPath);
	}

	SetWeaponVisual(ShownWeapon);
}

void AHIPlayerCharacter::PlayActionClip(const TCHAR* Clip, float HoldSec)
{
	FHICharAnim::PlayAction(Body, *CastTag, Clip, HoldSec);
}

void AHIPlayerCharacter::SetWeaponVisual(const FString& Weapon)
{
	ShownWeapon = Weapon;

	// The shotgun is this project's own code-built mesh (tools/make-shotgun-glb.py ->
	// SourceAssets/weapons/shotgun.glb -> import-assets.sh): boxes and cylinders with flat
	// materials, in the same visual language as every primitive prop in the port. Only the
	// last 10% needs an artist, and that door stays open -- any .fbx/.glb dropped into
	// SourceAssets/weapons/ imports the same way and only this path changes. The pistol is
	// still the engine's own; it was never a stand-in, it is a pistol.
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
	// WHICH AXIS THE MESH FACES IS A PROPERTY OF THE ASSET, and it has now been measured
	// twice, for two casts. The mannequin skeleton faced its own +Y -- reported from play
	// 2026-10-13 as *"The cross hairs are 90 degrees to the right of the player"*, measured
	// at exactly 90.0 deg by `-HIProbe`'s clavicle line, and fixed as t = -a. The Mixamo
	// cast faces differently again, so the derivation moved to `FHICharAnim::BodyRotationFor`
	// with the facing as a MEASURED CONSTANT that `HI.Unreal.Cast` recomputes from every
	// character's bind-pose shoulders and asserts. For a +Y-facing mesh the new formula
	// reduces to exactly the old t = -a, which is the check that the generalisation did not
	// quietly change the mannequin-era answer.
	return FHICharAnim::BodyRotationFor(SimYaw);
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
	// instead of skating: the clip choice and its stride are driven by MEASURED speed, so
	// a woman accelerating from a standing start animates as one who is accelerating.
	// Crouching is a STATE here, not a speed: the sim's crouch pace (2.0 m/s) is above the
	// walk/run threshold, and without the flag a crouching player would be shown running.
	// She is ALWAYS armed -- the sim never leaves the homeowner without a gun -- so her
	// standing pose is the armed `pistol_idle`, gun gripped up near the chest.
	FHICharAnim::DriveCharacter(Body, *CastTag, P.Vel.Size(), P.bAlive, P.bCrouching, /*bArmed=*/true);

	// THE RELOAD IS AN EDGE, NOT A STATE. `P.Reloading` counts down while the magazine
	// change runs, and a one-shot restarted every frame would pin the clip to its first
	// key -- the reload equivalent of dying standing up forever. The hold cap is the
	// shotgun's 1.60 s window (the pistol's is 1.15 s): the 3.3 s clip must not still be
	// fumbling after the sim has already let her fire.
	const bool bReloading = P.Reloading > 0.0f;
	if (bReloading && !bWasReloading)
	{
		PlayActionClip(FHICharAnim::ClipReload(), 1.60f);
	}
	bWasReloading = bReloading;

	// THE DEAD WOMAN FALLS. `DriveCharacter` above refuses to pose a body the sim has marked
	// dead, and `PlayDeath` is the call meant to replace it -- the intruders have had it since
	// the ragdoll work and the player pawn never did, so Louise died STANDING: frozen in her
	// aim pose, gun hidden by the dead-player rule below, hands empty while the heist carried
	// on around her. "The home defender does not have a gun" -- reported from play and chased
	// through two days of camera and asset checks on 2026-10-14 -- was this freeze, seen from
	// behind. The latch is the intruder's exactly: SetAnimation restarts the clip, and a death
	// replayed every frame is a body that never finishes falling.
	if (!P.bAlive)
	{
		if (!bDeathPlayed)
		{
			bDeathPlayed = true;
			FHICharAnim::PlayDeath(Body, *CastTag);
		}
	}
	else
	{
		bDeathPlayed = false;
	}

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
				TEXT("barrelVsAim=%.1fdeg gunRollFromUp=%.1fdeg gunDist=%.2f inFrontOfCamera=%d ")
				TEXT("gunLoc=(%.2f,%.2f,%.2f)m"),
				*ShownWeapon,
				(Gun->GetStaticMesh() != nullptr ? *Gun->GetStaticMesh()->GetName() : TEXT("NONE")),
				(Body != nullptr && Body->IsVisible()) ? 1 : 0,
				Gun->IsVisible() ? 1 : 0,
				BehindHim,
				BarrelVsAimDeg, GunRollDeg,
				ToGun.Size() / HI_METERS_TO_UU,
				(FVector::DotProduct(ToGun.GetSafeNormal(), CamFwd) > 0.0f) ? 1 : 0,
				GunLoc.X / HI_METERS_TO_UU, GunLoc.Y / HI_METERS_TO_UU, GunLoc.Z / HI_METERS_TO_UU);
			UE_LOG(LogTemp, Display,
				TEXT("HI: probe   camfwd=(%.2f,%.2f,%.2f) bodyfwd=(%.2f,%.2f,%.2f) barrel=(%.2f,%.2f,%.2f) gunup=(%.2f,%.2f,%.2f)"),
				CamFwd.X, CamFwd.Y, CamFwd.Z,
				BodyFwd.X, BodyFwd.Y, BodyFwd.Z,
				Barrel.X, Barrel.Y, Barrel.Z,
				GunUp.X, GunUp.Y, GunUp.Z);

			// ---- THE BONES THAT HOLD THE GUN, DUMPED ON TICKS 2 AND 3. `-HIGunLoc` seats
			// the grip in the HAND BONE's frame, and `AttachPistol` says plainly that
			// nobody has measured those axes -- so "the gun 6 inches higher" is not a
			// vector anyone can type until R_bone is known. Both the seat offset and any
			// arm-raise in the import tool get COMPUTED from this dump instead of
			// guessing at Mixamo's axis conventions. Mixamo names only (the rig in use);
			// a mannequin run says NOT FOUND per bone rather than pretending.
			//
			// NOT THE FIRST TICK, and that mistake was already made once: the probe also
			// fires the instant the weapon visual changes, and at round start that is
			// FRAME 1, before the anim system has evaluated a single pose. The first
			// dump was the reference T-pose -- hands out at the sides, RightHand Z
			// 1.394 m against the in-pose gunLoc Z of 1.09 m -- a measurement of
			// nothing. Ticks 2-3 are mid-loop, and two samples show the breathing
			// spread any raise has to survive.
			static int32 ProbeTicks = 0;
			++ProbeTicks;
			if ((ProbeTicks == 2 || ProbeTicks == 3) && Body != nullptr)
			{
				const TCHAR* const BoneNames[] = {
					TEXT("RightHand"), TEXT("LeftHand"),
					TEXT("RightArm"), TEXT("LeftArm"),
					TEXT("RightShoulder"), TEXT("LeftShoulder") };
				for (const TCHAR* BoneName : BoneNames)
				{
					const int32 BoneIdx = Body->GetBoneIndex(FName(BoneName));
					if (BoneIdx == INDEX_NONE)
					{
						UE_LOG(LogTemp, Warning, TEXT("HI: probe bone %s NOT FOUND"), BoneName);
						continue;
					}
					const FTransform BoneTm = Body->GetBoneTransform(BoneIdx);
					const FVector BonePos = BoneTm.GetLocation() / HI_METERS_TO_UU;
					const FQuat BoneRot = BoneTm.GetRotation();
					UE_LOG(LogTemp, Display,
						TEXT("HI: probe bone %s pos=(%.3f,%.3f,%.3f)m quat=(%.4f,%.4f,%.4f,%.4f)"),
						BoneName, BonePos.X, BonePos.Y, BonePos.Z,
						BoneRot.X, BoneRot.Y, BoneRot.Z, BoneRot.W);
				}
			}

			// ---- WHICH WAY DOES THE MESH ACTUALLY FACE?
			//
			// `bodyfwd` above is the COMPONENT's +X, and every rotation in this port is built on
			// "+X is forward". That is only true if the SKELETON faces its +X, which is a property
			// of the ASSET and not of the arithmetic -- and if it does not, every man in the game
			// is a quarter turn out while every number the probe prints agrees with every other
			// number. Reported from play 2026-10-13: *"The cross hairs are 90 degrees to the right
			// of the player instead of being in front."*
			//
			// The shoulders settle it WITHOUT any sign convention, which is why this is measured
			// this way: a humanoid's lateral axis is the line between its clavicles, so if that
			// line lies along the component's X then the man faces ±Y and the assumption is a
			// quarter turn wrong. Absolute values, because which clavicle is which is exactly the
			// kind of detail that has already produced three confident wrong answers.
			// The shoulders, whichever rig is wearing them: the mannequin calls them
			// clavicle_l/r, the Mixamo cast LeftShoulder/RightShoulder. The measurement is
			// rig-agnostic by design -- a humanoid's lateral axis is rig-agnostic -- so the
			// only thing that has to know the cast is the name lookup.
			FName ShoulderL, ShoulderR;
			if (Body != nullptr && Body->DoesSocketExist(TEXT("clavicle_l")))
			{
				ShoulderL = TEXT("clavicle_l");
				ShoulderR = TEXT("clavicle_r");
			}
			else if (Body != nullptr && Body->DoesSocketExist(TEXT("LeftShoulder")))
			{
				ShoulderL = TEXT("LeftShoulder");
				ShoulderR = TEXT("RightShoulder");
			}
			if (!ShoulderL.IsNone() && Body->DoesSocketExist(ShoulderR))
			{
				const FVector ClavL = Body->GetSocketLocation(ShoulderL);
				const FVector ClavR = Body->GetSocketLocation(ShoulderR);
				const FVector Lateral = (ClavR - ClavL).GetSafeNormal();
				const float AlongX = FMath::Abs(FVector::DotProduct(Lateral, Body->GetForwardVector()));
				const float AlongY = FMath::Abs(FVector::DotProduct(Lateral, Body->GetRightVector()));
				// Factual rather than alarming on purpose: a mannequin facing its own +Y is a
				// property of the ASSET, and `BodyRotation` is now built around it. What was alarming
				// is the opposite, because that is what this code assumed for its first day and it
				// put every man in the game a quarter turn out.
				//
				// READ THE CLEAN SAMPLE. The line between the clavicles is purely lateral only in a
				// neutral pose: a second sample in one run read 0.73/0.68 and reported 43 deg,
				// which was the arm mid-swing and nothing about the mesh at all.
				UE_LOG(LogTemp, Display,
					TEXT("HI: probe   shoulderLateral: |along body +X|=%.2f |along body +Y|=%.2f -> the skeleton faces %s"),
					AlongX, AlongY,
					(AlongX > AlongY)
						? TEXT("its own +Y, which is what BodyRotation now assumes")
						: TEXT("its own +X -- WHICH IS WHAT THE CODE USED TO ASSUME, a quarter turn out"));

				// AND THE SIGN, which the absolute values above deliberately cannot give. Deriving
				// it is not a guess: a character's RIGHT is `clavicle_l -> clavicle_r`, and in this
				// engine's Z-up left-handed frame forward = right x up. So this line names the
				// direction the man is actually facing, and the angle it makes with the crosshair is
				// the number the fix has to cancel.
				const FVector MeshFwd = Lateral.Cross(FVector::UpVector).GetSafeNormal();
				const float MeshVsAimDeg = FMath::RadiansToDegrees(FMath::Acos(
					FMath::Clamp(FVector::DotProduct(MeshFwd, CamFwd), -1.0f, 1.0f)));
				UE_LOG(LogTemp, Display,
					TEXT("HI: probe   meshFwd=(%.2f,%.2f,%.2f) yaw=%.1f  vs  camfwd yaw=%.1f  -> the BODY is %.1f deg off the crosshair"),
					MeshFwd.X, MeshFwd.Y, MeshFwd.Z,
					MeshFwd.Rotation().Yaw, CamFwd.Rotation().Yaw, MeshVsAimDeg);
			}

			// ---- IS THE GUN VISIBLE, OR MERELY "VISIBLE"?
			//
			// `gunVis` is a FLAG ON A COMPONENT. It says nothing about the man standing between the
			// camera and his own hand, and that is the entire subject of this framing work: the probe
			// reported the gun visible, in front of the camera and 1.3 m away, while there was no gun
			// on screen at all. It was checked against the wrong occlusion, three times.
			//
			// So TRACE it and name what is in the way. This is the number a camera sweep has to be
			// judged by -- "find the minimum back and up that still shows the gun" is a question
			// about line of sight, and line of sight is something a ray answers and an eye cannot
			// be asked about forty times in a row.
			if (Gun->GetStaticMesh() != nullptr && GetWorld() != nullptr)
			{
				const FBoxSphereBounds GunBounds = Gun->GetStaticMesh()->GetBounds();
				// The mesh is authored in centimetres and its barrel is its +Y, so the muzzle sits
				// Origin.Y + Extent.Y along that axis from the component's origin.
				const float MuzzleCm = GunBounds.Origin.Y + GunBounds.BoxExtent.Y;
				const FVector MuzzlePt = GunLoc + Barrel * MuzzleCm;
				const FVector MidPt = GunLoc + Barrel * (MuzzleCm * 0.5f);

				FCollisionQueryParams SightParams(SCENE_QUERY_STAT(HIGunSight), /*bTraceComplex=*/false);
				// The gun cannot obstruct its own muzzle, or every reading would be "blocked".
				// The cast is not decoration: `Gun` is a TObjectPtr and the overloads take a
				// UPrimitiveComponent and a UActorComponent, so passing it raw is AMBIGUOUS.
				SightParams.AddIgnoredComponent(static_cast<UPrimitiveComponent*>(Gun.Get()));

				auto Sight = [&](const FVector& To) -> const TCHAR*
				{
					FHitResult SightHit;
					if (!GetWorld()->LineTraceSingleByChannel(SightHit, CamLoc, To, ECC_Visibility, SightParams))
					{
						return TEXT("clear");
					}
					return (SightHit.GetComponent() == Body) ? TEXT("HIDDEN BY HIM") : TEXT("blocked");
				};
				UE_LOG(LogTemp, Display,
					TEXT("HI: probe   gunSight(WORLD ONLY, it CANNOT see the body -- no collision): muzzle=%s  mid=%s"),
					Sight(MuzzlePt), Sight(MidPt));
			}
		}
	}
}
