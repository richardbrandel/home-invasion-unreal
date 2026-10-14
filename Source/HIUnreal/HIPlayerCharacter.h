#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "HISim.h"
#include "HIPlayerCharacter.generated.h"

#ifndef HIUNREAL_API
#define HIUNREAL_API
#endif

/**
 * The homeowner, as a VIEW.
 *
 * He does not tick the round and he does not own his position. The game mode steps
 * FHIRound and then calls SyncFromSim. That keeps the sim authoritative and removes the
 * tick-ordering question entirely: a view that runs before its model is a frame stale, and
 * a view that runs after it off a stale copy is worse.
 *
 * The camera is the root component. The BODY hangs off it as a child but is placed in WORLD
 * space every frame rather than being parented to the view -- the camera sits behind and
 * above the man, so a body left in the camera's own frame drifts off his feet whenever the
 * view swings. Fix-list item 2 was "the homeowner is a low-poly mesh with a specular blob
 * for hair"; an invisible camera on legs would be worse than a bad mesh, not better.
 */
UCLASS()
class HIUNREAL_API AHIPlayerCharacter : public APawn
{
	GENERATED_BODY()

public:
	AHIPlayerCharacter();

	virtual void BeginPlay() override;

	/**
	 * Put the body and the camera where the sim says they are. Called by the game mode
	 * after Step, so this never runs against a half-updated round.
	 */
	void SyncFromSim(const FHISim::FHIPlayer& P);

	/** Show the pistol or the shotgun in his hand. Idempotent. */
	void SetWeaponVisual(const FString& Weapon);

	/**
	 * A timed one-shot on the body -- `shoot` when the round fires a round, `reload`
	 * when the magazine change starts. The hold cap and the why are on
	 * `FHICharAnim::PlayAction`; this is the handle the game mode needs, because the
	 * events live there and the body lives here.
	 */
	void PlayActionClip(const TCHAR* Clip, float HoldSec);

	/** The eye pivot: the point the chase camera orbits. */
	static FVector EyePivot(const FHISim::FHIPlayer& P);

	/** Where the camera goes, and what it looks along. Pure, so it can be reasoned about. */
	static void ViewTransform(const FHISim::FHIPlayer& P, FVector& OutLocation, FVector& OutDirection);

	/**
	 * The same, but pulled in so the camera cannot end up on the far side of a wall.
	 *
	 * NOT pure, because it needs a world to sweep against -- which is why `ViewTransform`
	 * above still exists and is still the one the tests use. Without this, the shoulder
	 * camera passes straight through the wall behind the homeowner and the player sees the
	 * front garden from inside the bedroom, which reads as the game having lost track of
	 * where he is. The Godot original hit the same problem and answered it with a camera fade
	 * for the same reason.
	 *
	 * The DIRECTION is never pulled in, only the position: the view must keep looking where
	 * the homeowner looks, or the reticle and the world would disagree.
	 */
	void ViewTransformCollided(const FHISim::FHIPlayer& P, FVector& OutLocation, FVector& OutDirection);

	/**
	 * The sim's plane angle as a world rotation for a BODY.
	 *
	 * The derivation (t = 90 - a - the mesh's own facing) and its measurement chain live in
	 * `FHICharAnim::BodyRotationFor` now, in one place instead of one per actor: the cast
	 * became Mixamo characters whose meshes face their own +X rather than the mannequin's
	 * +Y, and "which way does the asset face" is a property of the cast, not of the actor.
	 */
	static FRotator BodyRotation(float SimYaw);

	/**
	 * The world rotation that puts the weapon's BARREL on `Aim`, and keeps it upright.
	 *
	 * The weapon hangs off the hand SOCKET, so the animation pose decides which way it points,
	 * and the pistol-pose locomotion clips are walk cycles: the arm swings because the man is
	 * walking, not because he is aiming. Measured, that swings the barrel up to 49 degrees off
	 * the crosshair and rolls the pistol up to 74 degrees onto its side.
	 *
	 * Pure and static so the property can be asserted without a world: the returned rotation's
	 * +Y must equal `Aim` and its +Z must be as close to the world's up as that allows.
	 */
	static FRotator WeaponAimRotation(const FVector& Aim);

	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UCameraComponent> Camera;

	/** The visible homeowner. No collision: the sim decides every hit in this game. */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class USkeletalMeshComponent> Body;

	/** The pistol, parented into the right hand's socket. */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UStaticMeshComponent> Gun;

	/** "pistol" or "shotgun"; re-asserted so the right gun is in his hand every frame. */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	FString ShownWeapon = TEXT("pistol");

private:
	/** Load the body and the weapon once. A missing asset must degrade, not abort a round. */
	void BuildVisuals();

	bool bVisualsBuilt = false;

	/** Which of the cast this body is: Louise, always, for the homeowner. */
	FString CastTag;

	/** Reload-anim edge detector: `Reloading` is a countdown, and a one-shot is an event. */
	bool bWasReloading = false;

	/**
	 * Death-clip latch, the player's copy of the intruder's `bDeathPlayed`: dying is an edge,
	 * and a one-shot replayed every frame is a body that never finishes falling. Reset when
	 * the sim says she is alive again, so a restart can die properly a second time.
	 */
	bool bDeathPlayed = false;

	/** `-HIProbe`: seconds since the last geometry line (see SyncFromSim). */
	float ProbeTime = 0.0f;

	/** The weapon the last geometry line described, so a swap logs immediately. */
	FString LastProbeWeapon;
};
