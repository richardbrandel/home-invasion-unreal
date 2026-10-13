#pragma once

#include "CoreMinimal.h"

class UAnimationAsset;
class USkeletalMeshComponent;
class UStaticMeshComponent;

/**
 * The cast, and locomotion animation for it -- with clip selection by hand and no
 * AnimBlueprint anywhere.
 *
 * HISTORY, because the shape of this file is a record of two wrong assumptions.
 *
 * It first played the ENGINE's mannequin content: one unarmed blend space
 * (`BS_Idle_Walk_Run`) plus three pistol-pose sequences, in `AnimationSingleNode` mode.
 * That was fix-list item 13's answer without an AnimBlueprint, and it worked -- on the
 * mannequins, whose skeleton those assets are authored for.
 *
 * Then the cast became REAL CHARACTERS (2026-10-13): Louise the homeowner and James,
 * Brute and Martha the intruders, fetched from Mixamo with nine clips each and imported
 * by `tools/import-assets.py`. Two facts from that import drive everything below:
 *
 * 1. EACH CHARACTER HAS ITS OWN SKELETON. They are all the same Mixamo rig, but four
 *    separate `USkeleton` assets, so a clip from `characters_james` binds to James and
 *    not to Louise. Every path is therefore resolved PER CHARACTER -- a single global
 *    clip prefix cannot work.
 * 2. THE ENGINE'S CLIPS DO NOT BIND TO THEM, and their own clips have no blend space.
 *    So the blend space is gone too: this struct picks clips by speed the way
 *    `DriveLocomotion` always picked the pistol clips -- thresholds instead of a blend,
 *    cross-fading is what single-node mode cannot do, and honest naming is what keeps
 *    that limitation from being forgotten.
 *
 * THE CLIPS ARE NOT WHAT MIXAMO SHIPPED. The raw exports carry root motion baked into
 * the Hips track -- walk travels +1.75 m, run +3.55 m, and `aim` both travels 0.74 m
 * and turns 143 degrees -- all measured on the same files by the Godot project's
 * `test/anim_probe.gd`, which had to flatten them for exactly the same reason: the SIM
 * owns position, so a clip that moves the mesh moves it away from the body the sim is
 * stepping. `tools/import-assets.py` runs `UHIAnimTool` over the imported clips to pin
 * the Hips (translation for locomotion, translation AND rotation plus spine and legs
 * for `aim`), and `HI.Unreal.Cast` asserts the result from the key data.
 *
 * THE UNIT TRAP survives from the mannequin era and still applies to the stride
 * matching: the sim is in metres, animation references are in centimetres per second,
 * and the conversion happens here, once.
 */
struct FHICharAnim
{
	// ---------------------------------------------------------------- the cast
	/** Louise, the homeowner -- the woman the Godot version used, chosen by Richard. */
	static const TCHAR* PlayerTag();
	/** James, Brute, Martha, cycling with the thief Id so three men are three people. */
	static const TCHAR* IntruderTagForId(int32 Id);

	/** `/Game/HIImported/characters_<Tag>/` -- where a character's assets live. */
	static FString CharacterDir(const TCHAR* Tag);
	/** The skeletal mesh: the mesh-carrying export imports as `idle`. */
	static FString MeshPathFor(const TCHAR* Tag);
	/** One clip of one character. Each clip binds ONLY to its own character's skeleton. */
	static FString ClipPathFor(const TCHAR* Tag, const TCHAR* Clip);

	// ------------------------------------------------------------- clip names
	// Nine per character. The mesh-carrying export's own take imports as `idle_Anim`
	// beside the mesh named `idle` -- the name collision is the importer's, not ours.
	static const TCHAR* ClipAim();        // "aim"        Mixamo "Aiming Gun", Hips pinned by the tool
	static const TCHAR* ClipWalk();       // "walk"       authored 1.69 m/s
	static const TCHAR* ClipRun();        // "run"        authored 5.61 m/s
	static const TCHAR* ClipCrouchIdle(); // "crouch_idle"
	static const TCHAR* ClipCrouchWalk(); // "crouch_walk" authored 0.90 m/s
	static const TCHAR* ClipIdle();       // "idle_Anim"  the mesh export's own take
	static const TCHAR* ClipReload();     // "reload"     3.30 s, in place
	static const TCHAR* ClipShoot();      // "shoot"      2.67 s, Hips rotation pinned by the tool
	static const TCHAR* ClipDeath();      // "death"      3.03 s, a real fall: Hips 0.96 m -> 0.14 m

	/**
	 * The ground speed a clip was AUTHORED at, in cm/s, for stride matching: play it at
	 * `speed / authored` and the feet stop skating.
	 *
	 * PER CHARACTER, and that was a measurement, not a preference: Mixamo scales each
	 * clip's travel to the body, so the SAME walk clip travels 174.8 cm for Louise,
	 * 168.5 cm for James and Martha, and 211.6 cm for Brute (the import run of
	 * 2026-10-13 logged travel per clip before pinning it away -- the numbers below are
	 * that log's, divided by the clip lengths). A single global table would stride-match
	 * Brute ~23% slow and skate his feet. The Godot project's anim probe measured the
	 * same files and agrees on Louise (walk +1.748 m / 1.033 s).
	 *
	 * The travel is PINNED OUT of the saved assets (that is what normalization is), so
	 * this table is the only surviving record of it: it cannot be recomputed from the
	 * clips at runtime, and its provenance is the import log plus this comment.
	 * 0 for clips that do not travel; those play at rate 1.
	 */
	static float AuthoredCmPerSec(const TCHAR* Tag, const TCHAR* Clip);

	// ---------------------------------------------------------------- driving
	/**
	 * Put a body into single-node mode on its own character's starting clip (`aim` --
	 * both the homeowner and the intruders spawn armed).
	 *
	 * Safe to call with a missing asset: it logs and does nothing, and the caller keeps
	 * the reference pose rather than crashing. A missing character must degrade, not
	 * abort a round.
	 */
	static void SetupCharacter(USkeletalMeshComponent* Mesh, const TCHAR* Tag);

	/**
	 * Pick the clip by speed, crouch and weapon, and match its stride to the ground the
	 * sim actually moves the body over. Selection thresholds are the mannequin era's, in
	 * cm/s: under 15 stands, under 190 walks, above that runs -- against the sim's own
	 * numbers (thief 1.3 m/s walks, homeowner 4.2 m/s runs, crouch 2.0 m/s, laden 0.5-0.9).
	 *
	 * Clips SWITCH only on a change, because `SetAnimation` resets the playhead and a
	 * switch every frame moonwalks the body in place. Looped clips start at a RANDOM
	 * PHASE, which is fix-list item 15's phase variation: three men must not breathe or
	 * march in unison.
	 *
	 * A one-shot action (see `PlayAction`) in progress suspends this entirely until it
	 * ends or its hold expires -- that is how a shot or a reload is visible at all in a
	 * single-node setup with no montage slots.
	 */
	static void DriveCharacter(USkeletalMeshComponent* Mesh, const TCHAR* Tag, float SpeedMps,
		bool bAlive, bool bArmed, bool bCrouching);

	/**
	 * A timed one-shot: `shoot` and `reload`, the two clips that interrupt locomotion
	 * rather than replace it.
	 *
	 * `HoldSec` caps how long the clip may hold the body even though it is longer --
	 * measured against the source data, not picked: `shoot` is 2.67 s but its first fire
	 * burst runs 0.33-0.89 s (Godot plays exactly that window), and `reload` is 3.30 s
	 * while the sim's own reload windows are 1.15 s (pistol) and 1.60 s (shotgun). An
	 * animation that outlives the mechanic it illustrates is a man still fumbling a
	 * magazine he already fired from.
	 */
	static void PlayAction(USkeletalMeshComponent* Mesh, const TCHAR* Tag, const TCHAR* Clip,
		float HoldSec);

	/**
	 * The death: a hard switch to the character's own `death` clip, held at its last
	 * pose, with no hold cap and no return to locomotion.
	 *
	 * Deliberately NOT blended and NOT ragdolled here: the clip is a real fall this time
	 * (Mixamo's, Hips 0.96 m -> 0.14 m across 3.03 s, measured), unlike the engine's
	 * `MM_Death_Back_01`, which flinched and stopped upright at 87 cm. `AHIIntruder`
	 * still hands the body to physics part-way through, so the corpse settles on
	 * whatever it fell onto instead of floating a clip's worth of floor slide.
	 */
	static void PlayDeath(USkeletalMeshComponent* Mesh, const TCHAR* Tag);

	// --------------------------------------------------------------- facing
	/**
	 * Which way a Mixamo mesh faces in its own bind pose, as a component-frame yaw in
	 * degrees: the angle of the forward direction measured at the shoulders (the line
	 * RightShoulder - LeftShoulder is lateral; forward is lateral x up).
	 *
	 * A MEASURED CONSTANT, and the Cast test recomputes it from every character's bind
	 * pose and fails with the measured number in the message if this disagrees. It
	 * replaced `BodyRotation`'s old inline "+Y" assumption, which was true of the UE
	 * mannequin and is a property of the ASSET, never of the arithmetic -- a quarter
	 * turn of silent wrongness is what it cost the first time.
	 */
	static float MeshFacingYawDeg();

	/** The unit vector of `MeshFacingYawDeg` in the component frame. */
	static FVector MeshForwardAxis();

	/**
	 * The sim's plane angle as a world rotation for a BODY, for a mesh that faces
	 * `MeshFacingYawDeg` in its own frame.
	 *
	 * The sim's yaw 0 faces +Y, so its direction is (sin a, cos a); a component yaw t
	 * puts the mesh's facing at (t + facing). Equating gives t = 90 - a_deg - facing.
	 * For the mannequin's +Y-facing meshes (facing = 90) that reduces to the measured
	 * t = -a; the derivation lives here once instead of twice.
	 */
	static FRotator BodyRotationFor(float SimYaw);

	// --------------------------------------------------------------- weapons
	/** The engine's pistol, a static mesh that needs no skeleton of its own. */
	static const TCHAR* PistolMeshPath();

	/**
	 * The bone the weapon grips with: the mannequin's `hand_r` or the Mixamo rig's
	 * `RightHand`, whichever the mesh actually has. NAME_NONE when neither exists --
	 * which AttachPistol refuses to guess around.
	 */
	static FName HandBoneFor(const USkeletalMeshComponent* Mesh);

	/**
	 * Hang the engine's pistol off a skeleton's right hand.
	 *
	 * Returns the component, or nullptr when the mesh has no hand bone -- in which case
	 * the weapon is not spawned at all rather than left floating at the actor's origin,
	 * which reads as a gun stuck through the middle of a man's chest.
	 */
	static UStaticMeshComponent* AttachPistol(AActor* Owner, USkeletalMeshComponent* Mesh, UStaticMeshComponent* Pistol);
};
