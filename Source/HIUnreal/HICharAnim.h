#pragma once

#include "CoreMinimal.h"

class UAnimationAsset;
class USkeletalMeshComponent;
class UStaticMeshComponent;

/**
 * Locomotion animation, with real blending, and no AnimBlueprint anywhere.
 *
 * This is fix-list item 13 ("animation blending: `AnimationTree` absent, clips cut rather
 * than blend") answered the way Unreal answers it -- but NOT by the route the port's own
 * notes assumed. The plan was "animation blending is built in", which is true of a normal
 * Unreal project and false of this one: an AnimBlueprint is a .uasset, this project has no
 * editor session, and a hand-written `FAnimInstanceProxy` is a large amount of fragile
 * engine API to reproduce something the engine already ships in data form.
 *
 * So it uses the engine's ONE node mode instead -- `EAnimationMode::AnimationSingleNode`
 * -- playing a 2-D BLEND SPACE over speed and direction. That is a genuine blend: the clips
 * cross-fade continuously rather than cutting, which is the fault that was reported. The
 * blend space (`BS_Idle_Walk_Run`) is an Unreal template asset, so nothing is authored here.
 *
 * THE UNIT TRAP, and it is the reason `SpeedCmPerSec` is named that way: a blend space is
 * authored in the engine's own units, so its speed axis runs in CENTIMETRES per second.
 * The sim is in metres. Feeding the sim's 4.2 straight in pins the blend at the idle end
 * and a walking man reads as a statue sliding across the floor -- which looks like broken
 * animation rather than a unit error. Conversion happens here, once, and the name says so.
 */
struct FHICharAnim
{
	/** Where the engine's own locomotion blend space lives (an Unreal template asset). */
	static const TCHAR* LocomotionPath();
	/** The engine's pistol, a static mesh that needs no skeleton of its own. */
	static const TCHAR* PistolMeshPath();

	/**
	 * Put a body into single-node mode on the locomotion blend space.
	 *
	 * Safe to call on a mesh with no asset set: it does nothing, and the caller keeps the
	 * default pose rather than crashing. A missing asset must degrade, not abort a round.
	 */
	static void SetupLocomotion(USkeletalMeshComponent* Mesh);

	/**
	 * Drive the blend from a speed in METRES per second. This is the only place the
	 * metre-to-centimetre conversion for animation happens.
	 */
	static void DriveLocomotion(USkeletalMeshComponent* Mesh, float SpeedMps, bool bAlive);

	/**
	 * Hard-switch to a one-shot clip and hold its last pose (a death), or back to the blend
	 * space when `SequencePath` is null.
	 *
	 * Deliberately NOT blended: a man who has been shot does not cross-fade into falling
	 * over, and the one-shot is the end of his animation rather than a layer over it.
	 */
	static void PlayOneShot(USkeletalMeshComponent* Mesh, const TCHAR* SequencePath);

	/**
	 * Hang the engine's pistol off a skeleton's right hand.
	 *
	 * Returns the component, or nullptr when the mesh has no such socket -- in which case
	 * the weapon is not spawned at all rather than left floating at the actor's origin,
	 * which reads as a gun stuck through the middle of a man's chest.
	 */
	static UStaticMeshComponent* AttachPistol(AActor* Owner, USkeletalMeshComponent* Mesh, UStaticMeshComponent* Pistol);
};
