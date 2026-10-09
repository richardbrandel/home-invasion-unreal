#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HISim.h"
#include "HIIntruder.generated.h"

#ifndef HIUNREAL_API
#define HIUNREAL_API
#endif

/**
 * One intruder, as a VIEW of an FHIThief.
 *
 * THE HIT VOLUME IS A CAPSULE, NOT THE MESH, and that is deliberate rather than lazy. The
 * sim models a man as a circle of ThiefRadius that blocks nothing -- every hit in this game
 * is decided by `DamageIntruder(id, damage)`, with no notion of where on the body a pellet
 * landed. Tracing against the character's physics asset would therefore be a SECOND, more
 * detailed opinion than the model has, and the two would disagree at the edges: a shot that
 * clipped a shoulder would wound a man the sim considers untouched. A capsule of exactly
 * the sim's radius and height is the honest hit target. The mesh is what you look at.
 *
 * Like the pawn it does not tick. The game mode steps the round and calls SyncFromSim.
 */
UCLASS()
class HIUNREAL_API AHIIntruder : public AActor
{
	GENERATED_BODY()

public:
	AHIIntruder();

	virtual void BeginPlay() override;

	/** Matches FHIThief::Id, so the game mode can pair a body with its sim state. */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	int32 ThiefId = 0;

	/**
	 * Put the body where the sim says it is.
	 *
	 * `Dt` is needed only for animation: the sim carries `Prev`, so measured speed is
	 * (Pos - Prev) / Dt. Reading a speed FIELD instead would be wrong -- the sim's `Speed`
	 * is the distance he intends to cover this frame, not a rate.
	 */
	void SyncFromSim(const FHISim::FHIThief& T, float Dt);

	/**
	 * The sim's plane angle as a world rotation.
	 *
	 * The sim's yaw 0 faces +Y, so its direction is (sin a, cos a) in the (X, Y) plane.
	 * Unreal's FRotator yaw 0 faces +X and turns (cos t, sin t). Equating the two gives
	 * t = 90 - a. A quarter turn out is silent, so the derivation lives here with it.
	 */
	static FRotator BodyRotation(float SimYaw);

	/**
	 * The actor's origin, at the sim's own coordinate: on the FLOOR, between his feet.
	 *
	 * An explicit root rather than making the capsule the root, because a capsule's origin is
	 * its centre and a body's is between its feet. Making either one the root means one of the
	 * two needs a standing offset, and an offset applied to a root component is the ambiguous
	 * case -- it is both "relative to the actor" and "the actor's own position", and which one
	 * it behaves as depends on when it is set. A root at the feet makes both children plain.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class USceneComponent> Root;

	/** The hit volume: a capsule of the sim's own radius and height. Invisible, blocks shots. */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UCapsuleComponent> HitVolume;

	/** What you actually look at. No collision of its own. */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class USkeletalMeshComponent> Body;

	/** The pistol, parented into the right hand's socket. Hidden when his hands are full. */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UStaticMeshComponent> Gun;

private:
	void BuildVisuals();

	bool bVisualsBuilt = false;
	/** Latched so the death clip is started once rather than restarted every frame. */
	bool bDeathPlayed = false;
};
