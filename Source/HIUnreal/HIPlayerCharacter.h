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
 * The camera is the root component rather than something hanging off a body mesh. The
 * chase offset follows the AIM, not the actor, so placing it in world space every frame is
 * simpler than fighting an attachment each time the view swings.
 */
UCLASS()
class HIUNREAL_API AHIPlayerCharacter : public APawn
{
	GENERATED_BODY()

public:
	AHIPlayerCharacter();

	/**
	 * Put the body and the camera where the sim says they are. Called by the game mode
	 * after Step, so this never runs against a half-updated round.
	 */
	void SyncFromSim(const FHISim::FHIPlayer& P);

	/** The eye pivot: the point the chase camera orbits. */
	static FVector EyePivot(const FHISim::FHIPlayer& P);

	/** Where the camera goes, and what it looks along. Pure, so it can be reasoned about. */
	static void ViewTransform(const FHISim::FHIPlayer& P, FVector& OutLocation, FVector& OutDirection);

	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UCameraComponent> Camera;
};
