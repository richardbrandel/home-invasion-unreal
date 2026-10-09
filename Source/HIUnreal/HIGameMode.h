#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "HISim.h"
#include "HIGameMode.generated.h"

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

	/** One actor per valuable, index-aligned with Round.Loot. */
	UPROPERTY()
	TArray<TObjectPtr<class AHILootProp>> LootActors;

	/**
	 * A fixed pool of bullet holes and blood, written round-robin.
	 *
	 * Pooled rather than spawned per hit, because a shotgun puts eight pellets into a wall
	 * and a long round produces hundreds; the Godot original keeps the same bounded ring for
	 * the same reason. Bounded means the OLDEST mark is overwritten, which is the right
	 * trade: nobody notices that the hole from thirty seconds ago has gone.
	 */
	UPROPERTY()
	TArray<TObjectPtr<class AHIMark>> Marks;

	int32 MarkCursor = 0;

	/**
	 * Resolve one announced intruder shot.
	 *
	 * THIS IS THE HALF OF THE GAME THAT WAS MISSING. The sim decides THAT an intruder fires,
	 * emits `thiefShot`, and stops -- the hit/miss roll deliberately lives in the game layer
	 * so the sim stays pure and its tests stay deterministic. But nothing in this port ever
	 * read that event, so the intruders' bullets did nothing at all: the homeowner's health
	 * could not fall, and the only way to lose was to let the van fill up. A round was
	 * unloseable by gunfire.
	 *
	 * Ported from game.gd's `_drain_events`, including the part that matters most: HIS ROUND
	 * IS A RAY. It used to be a probability roll that never consulted geometry, so a wall
	 * between the two of you was not part of the calculation and he could shoot you through
	 * the house.
	 */
	void ResolveThiefShot(const FHISim::FHIEvent& Shot);

	/** A short-lived streak, for a tracer. Hidden by TickFlashes, not by the pool. */
	void FlashLine(const FVector& From, const FVector& To, float ThicknessMeters,
		const FLinearColor& Colour);

	/** A short-lived blob, for a muzzle flash. */
	void FlashPoint(const FVector& Where, float SizeMeters, const FLinearColor& Colour);

	/** Hide every flash whose moment has passed. */
	void TickFlashes();

	/**
	 * The chance an intruder's clear shot hits, given the range in METRES.
	 *
	 * Static and pure so it can be asserted without a world -- and it is worth asserting,
	 * because the number encodes a balance decision that was already got wrong once. The
	 * Godot original's older formula had a 0.55 FLOOR, which meant he hit better than half
	 * the time at any range at all: keeping your distance bought the player nothing. The
	 * floor is now 0.05 and falloff is 0.035 per metre, so range is a real defence.
	 *
	 * It lives here rather than in the sim on purpose: sim.gd's rule is that randomness
	 * stays out of the simulation, so the sim decides THAT he fires and the game layer
	 * decides where it went.
	 */
	static float ThiefHitChance(float RangeMeters);

	/**
	 * Tracers and muzzle flashes, pooled and hidden on a timer.
	 *
	 * Separate from `Marks` because their lifetimes are opposite: a bullet hole is meant to
	 * stay and a tracer is meant to vanish in a few frames. Sharing one ring would mean a
	 * bullet hole disappearing because a tracer needed its slot.
	 */
	UPROPERTY()
	TArray<TObjectPtr<class AHIMark>> Flashes;

	TArray<float> FlashHideAt;
	int32 FlashCursor = 0;

	/** Round time at which the last damage was taken, for the HUD's damage flash. */
	float LastDamageTime = -100.0f;

	/**
	 * Latest short message for the HUD, and how long it has left.
	 *
	 * The Godot original carries `msg`/`msg_t` for exactly this: "They grabbed the TV!" is
	 * information the player needs and cannot get from a loot prop sliding behind a wall.
	 */
	FString Message = TEXT("DEFEND YOUR HOME   -   [F] shoves   [R] reloads   [1]/[2] weapon   [Q] shout   [P] police");
	float MessageTime = 8.0f;

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

	/**
	 * Resolve one announced shot: cast its pellets, and hand whatever they hit back to the
	 * sim. The trace decides WHERE a pellet went; the sim decides what that MEANS.
	 */
	void ResolvePlayerShot(const FHISim::FHIEvent& Shot);

	/** How far a pellet carries, in METRES -- the sim's unit. Converted inside the trace. */
	UPROPERTY(EditDefaultsOnly, Category = "Home Invasion")
	float ShotRangeMeters = 60.0f;

	/** Start a fresh round: new intruders, new loot, the same house. Bound to [ENTER]. */
	UFUNCTION(BlueprintCallable, Category = "Home Invasion")
	void RestartRound();

	/** Seconds of round time elapsed, for the HUD and for the capture harness. */
	float RoundSeconds = 0.0f;

	/**
	 * Leftover real time not yet handed to the sim, and the counters that expose it.
	 *
	 * The sim is stepped at a FIXED rate regardless of frame rate, and these are why: see
	 * the comment in Tick. `SimSteps` and `Frames` are reported by the soak so that a probe
	 * running at 2 fps cannot be mistaken for a probe running at 60.
	 */
	float SimAccumulator = 0.0f;
	int32 SimSteps = 0;
	int32 Frames = 0;

	/** Shots the homeowner has fired this round. Reported by the soak, shown nowhere. */
	int32 PlayerShotsFired = 0;

	/** True while a soak or a capture is driving the round, so nothing waits for a human. */
	bool bScripted = false;

	/**
	 * Diagnostic only: do not apply the intruders' damage to the homeowner.
	 *
	 * This is the Unreal equivalent of a decision `probe_heist.gd` already made and recorded,
	 * and it exists for the same reason: with the intruder's damage applied, an idle
	 * homeowner is shot dead in seconds and the HEIST -- the thing the probe is for -- never
	 * gets far enough to be observed. Measured in this port: 6.6 s, standing still, three
	 * intruders, zero loot moved. A soak with this set can therefore say whether a thief can
	 * find, grab and deliver; it says NOTHING about balance, and the report says so.
	 */
	bool bNoThiefDamage = false;

private:
	/** Build the sun, the sky fill and the interior lights. Text-authored, no level. */
	void SetupLighting();

	/** Put one actor per valuable in the world. */
	void SpawnLoot();

	/** Read `-HI*` off the command line and act on the ones that change what happens. */
	void HandleCommandLine();

	/**
	 * Drive the homeowner without a human.
	 *
	 * Exists because "it compiles and 24 tests pass" is not evidence that a round can be
	 * played, and there is no display on the build machine. The autopilot turns toward the
	 * nearest standing intruder, holds fire when one is in front of him, and walks into the
	 * house otherwise -- which is enough to make the round actually resolve rather than sit
	 * at the spawn point until it times out.
	 */
	void Autopilot(FHISim::FHIInput& In);

	/** Write the capture, or the soak report, and quit. */
	void TickHarness(float DeltaSeconds);

	/** Print how the round actually went. The point of `-HISoak`. */
	void ReportSoak() const;

	/** The nearest standing intruder's position, or false when nobody is up. */
	bool NearestStanding(FVector2D& OutPos, float& OutDist) const;

	// ---- capture / soak state, all read from the command line
	FString ShotName;
	float ShotAfterSeconds = 1.5f;
	bool bHideUIForShot = false;
	float SoakSeconds = -1.0f;
	bool bAuto = false;
	/** Frames to wait after asking for a screenshot before quitting, so it can be written. */
	int32 QuitCountdownFrames = INDEX_NONE;
	bool bShotTaken = false;
	/** Guards against reporting a soak twice, now that two paths can end one. */
	bool bSoakReported = false;
};
