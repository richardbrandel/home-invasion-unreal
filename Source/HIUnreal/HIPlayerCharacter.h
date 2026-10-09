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
	 * The sim's yaw 0 faces +Y, so its direction is (sin a, cos a); Unreal's yaw 0 faces +X
	 * and turns (cos t, sin t). Equating the two gives t = 90 - a. The same derivation as the
	 * intruder's, deliberately duplicated rather than shared through a common base class:
	 * the two are different kinds of thing and a base class for one line of maths would
	 * couple them for nothing.
	 */
	static FRotator BodyRotation(float SimYaw);

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
};
