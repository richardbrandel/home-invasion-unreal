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
	/**
	 * @param bLightsOn  false when the homeowner has killed the lights.
	 *
	 * LIGHTS, fix-list item 17's remaining half, and the whole effect lives HERE rather than
	 * in the sim -- on purpose. The port's own design rule is that the sim decides THAT an
	 * intruder fires and the game layer decides WHERE the shot goes, so degrading his aim in
	 * the dark belongs in this function and needs no change to any ported AI function.
	 *
	 * A shot taken across a dark room is harder than one taken across a lit one, and that
	 * makes the light switch a real tactical choice: you give up your own view of the house
	 * to make theirs worse. A fuller model -- per-man vision, furniture you can actually hide
	 * behind -- is the same open decision item 18 records, and needs furniture heights.
	 */
	static float ThiefHitChance(float RangeMeters, bool bLightsOn = true);

	/**
	 * Push `bLightsOn` onto the lamps. IDEMPOTENT.
	 *
	 * Separate from the toggle below because a start-dark run that called the TOGGLE flipped
	 * the flag straight back to lit -- so the "dark" measurement and the lit one were the same
	 * run, and the comparison proving the verb does something proved nothing. A setter and a
	 * flipper are different operations and conflating them is exactly how a check stops being
	 * able to fail.
	 */
	void ApplyLights();

	/** Turn the house lights on or off. Bound to [L]. */
	void ToggleLights();

	/** True while the house lights are on. */
	bool bLightsOn = true;

	/** The interior lamps, so the switch can actually do something visible. */
	UPROPERTY()
	TArray<TObjectPtr<class APointLight>> Lamps;

	/**
	 * Each lamp's intensity with the lights ON, index-aligned with `Lamps`.
	 *
	 * Kept because the switch has to be reversible, and reading the intensity back off a
	 * dimmed lamp would give the dimmed value -- so the second press would dim it again, and
	 * the third, and the room would darken by a factor each time. That is a one-way switch
	 * wearing a two-way label.
	 */
	TArray<float> LampLitIntensity;

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
	 * DERIVED FROM THE ORIGINAL, which the first version of this comment said could not be
	 * done. `game.gd` uses AIM_SENS_X = 0.0022 radians per mouse PIXEL, and the view is driven
	 * from the raw mouse delta (`GetInputMouseDelta`), which is also in pixels -- so the
	 * original's number transfers exactly, with no axis-sensitivity conversion anywhere.
	 *
	 * The port previously used an axis value times 0.05, which was both the wrong speed AND
	 * dead: the `Turn` axis is not bound on the controller, so it read 0 and the view never
	 * moved at all.
	 */
	static constexpr float GodotYawRadiansPerPixel = 0.0022f;    // game.gd AIM_SENS_X
	static constexpr float GodotPitchRadiansPerPixel = 0.0018f;  // game.gd AIM_SENS_Y

	/**
	 * Recoil: `game.gd` kicks the view on every shot and drifts it back at 0.85 rad/s.
	 *
	 * Ported here rather than left out, because it is the difference between firing feeling
	 * like a gun going off and firing feeling like a cursor click -- and because the port
	 * already claimed to have "recoil bookkeeping" in a comment while having none.
	 */
	static constexpr float RecoilPistol = 0.022f;
	static constexpr float RecoilShotgun = 0.062f;
	static constexpr float RecoilRecoverRate = 0.85f;

	/** Radians of kick still to be handed back to the view. */
	float RecoilPending = 0.0f;

	/**
	 * The mouse-to-view mapping, pure: pixels in, radians out.
	 *
	 * Extracted so its SIGN can be asserted. Both axes were inverted in the first version and
	 * no capture or soak could see it -- a still frame cannot show which way a mouse turns.
	 */
	static void LookDelta(float MouseX, float MouseY, float& OutYawDelta, float& OutPitchDelta);

	/** Claim the keyboard and mouse once. See the definition -- omitting it broke both. */
	void ClaimInput();
	bool bInputClaimed = false;

	/**
	 * `-HIPlaytest`: drive the game through the REAL input path and check that it responds.
	 *
	 * WHY THIS EXISTS, and it is the most important thing learned from the first play
	 * sessions: `-HIAuto` calls `ReadInput()` and then THROWS IT AWAY -- `Autopilot(In)`
	 * overwrites every field. So every soak and every capture this project ever ran exercised
	 * the simulation and never once executed the code a keyboard goes through. That is how
	 * "WASD does nothing" and "the mouse does not aim" both shipped: no test could have seen
	 * them, because no test pressed a key.
	 *
	 * This injects real key events with `APlayerController::InputKey`, lets the game read them
	 * through `ReadInput` exactly as it would from a person, and then ASSERTS on the result --
	 * did the body move, did the view turn, did the magazine drop. A dumb bot with
	 * assertions, which for this class of bug beats a clever observer.
	 */
	void PlaytestTick(float DeltaSeconds);

	/** Injected-input state for the playtest. */
	bool bPlaytest = false;
	float PlaytestTime = 0.0f;
	int32 PlaytestStep = -1;
	/** Where the body and view were at the start, so movement can be measured. */
	FVector2D PlaytestStartPos = FVector2D::ZeroVector;
	float PlaytestStartYaw = 0.0f;
	float PlaytestStartPitch = 0.0f;
	int32 PlaytestStartMag = 0;
	float PlaytestPosAfterW = 0.0f;
	float PlaytestYawAfterMouse = 0.0f;
	int32 PlaytestMagAfterFire = 0;

	/** Which path fed the last look input: "axes", "keys", or "raw mouse". Diagnostics. */
	FString InputSource = TEXT("none");
	/** Seconds since the last input log line, so a stuck control can be told from a dead one. */
	float InputLogTime = 0.0f;

	/** Counters for the things that must not repeat every frame: not every hit is a cry. */
	int32 WoundCries = 0;
	int32 ShoutsMade = 0;

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

	/**
	 * Watch for [ENTER] after the round has been decided, and start another one.
	 *
	 * SEPARATE FROM ReadInput, and this is a bug fix rather than tidying. ReadInput is only
	 * called from the branch of Tick that runs while the round is LIVE, so the Enter check
	 * that used to live inside it had never once executed: the outcome banner promised
	 * "[ENTER] to play again" and the key did nothing, leaving a process restart as the only
	 * way to play a second round.
	 */
	void PollRestart();

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

	/**
	 * `-HIShotNoUI`: capture a frame with no HUD on it.
	 *
	 * This getter exists because the switch did not work for eleven captures. It was read,
	 * and it was handed to `FScreenshotRequest::RequestScreenshot(bShowUI=...)` -- and that
	 * flag suppresses SLATE, not the canvas the HUD is drawn on, so every "no UI" frame came
	 * back with the readout, the minimap and the damage bars on it. Reading the engine's own
	 * `UGameViewportClient::ProcessScreenShots` is what explained it. Measured, not assumed:
	 * the frames are in `shots/`.
	 */
	bool ShouldHideHUD() const { return bHideUIForShot; }

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
	/** `-HIWeapon=<key>`: start the round holding it. Dev switch for captures. */
	void ApplyStartWeapon();

	/**
	 * `-HIWeaponAt=t:key,...`: switch weapons at chosen SIM seconds inside ONE run.
	 *
	 * A pistol+shotgun sweep used to be two launches and pay ~18 s of engine startup twice.
	 *
	 * This only LATCHES the cue into `PendingWeaponCue`; the step loop delivers it. It cannot
	 * write into `In` directly, because `ReadInput()` rebuilds that struct every frame -- a cue
	 * that came due on a frame where the accumulator did not reach a whole sim step was dropped
	 * silently, which is exactly what the first version of this did.
	 */
	void ApplyScheduledWeapon();

	/**
	 * `[ESC]` gives the mouse back, and `[ESC]` again takes it.
	 *
	 * THE GODOT ORIGINAL HAS THIS AND THE PORT DID NOT. `game.gd` captures the mouse at start and
	 * then, on `KEY_ESCAPE`, calls `Input.set_mouse_mode(MOUSE_MODE_VISIBLE)`. This port captured
	 * the cursor (`bShowMouseCursor = false` + `FInputModeGameOnly()`) and offered NO WAY BACK AT
	 * ALL, with `DefaultViewportMouseCaptureMode=CapturePermanently_...` and no escape binding in
	 * `DefaultInput.ini` -- and the window has no title bar, so there is not even a close button to
	 * reach for. A player whose cursor is gone has nothing to click anywhere.
	 *
	 * Polled in EVERY round state, not just while playing: being unable to free the cursor must not
	 * depend on the round still being live.
	 */
	void PollMouseRelease();

	/** True while `[ESC]` is holding the mouse released. */
	bool bMouseReleased = false;

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
	/**
	 * `-HIShotSeries=t1,t2,...`: capture at each of these SIM seconds, in ONE run.
	 *
	 * Empty means the single `-HIShotAfter` capture, which is what every earlier capture used.
	 * When present, this list IS the schedule and `-HIShotAfter` is ignored. One build, one
	 * launch, a whole sweep of poses -- which is the point. See `TickHarness` for why a sweep
	 * matters more here than it would in an engine that starts in two seconds.
	 */
	TArray<float> ShotSeries;
	/** Index into `ShotSeries` of the next frame not yet asked for. */
	int32 ShotSeriesNext = 0;
	/** How many scheduled frames the round ended too early for, so the log can say so. */
	int32 ShotSeriesMissed = 0;

	/** One `-HIWeaponAt=t:key` cue. */
	struct FHIWeaponCue
	{
		float Time = 0.0f;
		FString Key;
	};
	TArray<FHIWeaponCue> WeaponSchedule;
	/** One flag per cue, so a cue is an EDGE and not a per-frame re-assignment. */
	TArray<bool> WeaponScheduleFired;
	/**
	 * A cue that is due but has not yet reached a sim step.
	 *
	 * It has to survive across frames: `ReadInput()` rebuilds the input struct every Tick, so a
	 * cue written into it is lost on any frame where the accumulator does not reach a whole sim
	 * step -- one frame in six, at the rates this box runs at.
	 */
	FString PendingWeaponCue;
	/** Guards against reporting a soak twice, now that two paths can end one. */
	bool bSoakReported = false;
	/** `-HIRounds=N`: play N rounds back to back. 0 or 1 means one. */
	int32 RoundsToPlay = 1;
	int32 RoundsPlayed = 0;
};
