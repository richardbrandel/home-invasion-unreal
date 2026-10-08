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
 * A cylinder for now: the port has no character assets, and what this actor exists to prove
 * is the sim-to-world mapping, not to look like a person. A cylinder rather than a sphere
 * is deliberate -- a sphere hides whether the facing is right, and a silent quarter turn is
 * exactly the mistake worth making loud.
 *
 * Like the pawn it does not tick. The game mode steps the round and calls SyncFromSim.
 */
UCLASS()
class HIUNREAL_API AHIIntruder : public AActor
{
	GENERATED_BODY()

public:
	AHIIntruder();

	/** Matches FHIThief::Id, so the game mode can pair a body with its sim state. */
	int32 ThiefId = 0;

	/** Put the body where the sim says it is. Called by the game mode after Step. */
	void SyncFromSim(const FHISim::FHIThief& T);

	/**
	 * The sim's plane angle as a world rotation.
	 *
	 * The sim's yaw 0 faces +Y, so its direction is (sin a, cos a) in the (X, Y) plane.
	 * Unreal's FRotator yaw 0 faces +X and turns (cos t, sin t). Equating the two gives
	 * t = 90 - a. A quarter turn out is silent, so the derivation lives here with it.
	 */
	static FRotator BodyRotation(float SimYaw);

	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UStaticMeshComponent> Body;
};
