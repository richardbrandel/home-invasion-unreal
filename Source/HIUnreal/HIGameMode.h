#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "HISim.h"
#include "HIGameMode.generated.h"

// UBT does not define a module API macro for a game module, so a UCLASS decorated with one
// will not compile. Declared empty here rather than left out, so the decoration is correct
// if HIUnreal is ever split into its own DLL -- the same trap docs/BUILD.md records for
// HISIM_API.
#ifndef HIUNREAL_API
#define HIUNREAL_API
#endif

/**
 * Owns the round, and is the only thing that ticks it.
 *
 * The sim is authoritative and the actors are VIEWS of it: nothing else steps the round,
 * and nothing writes sim state except through here. Driving it from one Tick rather than
 * letting the pawn and each intruder tick themselves is deliberate -- the order between two
 * actors in the same tick group is a priority setting, not a guarantee, and a half-stepped
 * round is a bug that stays invisible until three bodies are in the same doorway.
 */
UCLASS()
class HIUNREAL_API AHIGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AHIGameMode();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** How many intruders a round sends. Godot's THIEF_COUNT default is 3. */
	UPROPERTY(EditDefaultsOnly, Category = "Home Invasion")
	int32 IntruderCount = 3;

	/** The authoritative round state. Actors mirror this; they never own it. */
	FHISim::FHIRound Round;

	/** What the round raised on the last tick, for the actors and the HUD to read. */
	TArray<FHISim::FHIEvent> LastEvents;

	/**
	 * One actor per intruder, index-aligned with Round.Intruders: the round owns how many
	 * there are, so the actors follow it rather than the other way round.
	 */
	UPROPERTY()
	TArray<TObjectPtr<class AHIIntruder>> IntruderActors;

	/** True once the round has been decided, either way. */
	bool IsRoundOver() const { return Round.State != FHISim::EHIRoundState::Play; }

	/**
	 * One frame of player intent, gathered from the axes and keys bound in
	 * Config/DefaultInput.ini. Public so the HUD can show what the round was told.
	 */
	FHISim::FHIInput ReadInput();

	/**
	 * Radians of view per unit of the Turn / LookUp axis.
	 *
	 * TUNING, NOT PORTING, and worth saying so: the Godot original uses 0.0022 rad per mouse
	 * PIXEL, while Unreal's axis is already scaled by its own sensitivity from
	 * DefaultInput.ini, so the two numbers are not comparable and this cannot be derived
	 * from the original. It needs a person looking at it, which is exactly what I cannot do.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Home Invasion")
	float LookRadiansPerAxisUnit = 0.05f;

	/**
	 * Apply this frame's look input to the sim's OWN yaw and pitch, rather than keeping the
	 * view in the pawn.
	 *
	 * That is deliberate and it fixes a real bug in the original: game.gd never writes
	 * player["yaw"] at all, so the only thing that reads it -- FHISim::Shove's front arc --
	 * was frozen at its spawn value and you could only shove a man standing toward +Z.
	 * FHIRound's movement is already camera-relative off the same field, so driving it from
	 * the view fixes the shove arc and keeps the movement honest in one place.
	 */
	void ApplyLook();
};
