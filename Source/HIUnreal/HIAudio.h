#pragma once

#include "CoreMinimal.h"

class UAudioComponent;
class USoundAttenuation;
class USoundBase;
class USoundClass;
class USoundSubmix;
class UWorld;

/**
 * Sound, and the buses it goes through.
 *
 * Fix-list item 20 is "everything on Master -- no indoor reverb vs outdoor, no ducking", and
 * in this port the answer to it started from further back than the item assumes: there was no
 * audio at all. A round was silent. A gunfight happened in your front room and the only
 * evidence was a number in the corner going down.
 *
 * WHAT IS REAL HERE. Every cue is routed through the engine's own `SFX` sound class rather
 * than the default Master, so a master/SFX/music balance is possible; and every cue carries a
 * submix send to the engine's `MasterReverbSubmixDefault`, whose effect chain was checked
 * before it was relied on (`SubmixEffectReverbPreset` referencing `MasterReverbEffectPreset`).
 * The send level is set from whether the cue is INSIDE the house, which is the indoor/outdoor
 * split the item asks for -- a shot in the kitchen rings, a shot on the lawn does not.
 *
 * WHAT IS REAL NOW THAT WAS A STAND-IN. This file used to say, in as many words, that "the
 * gunshots are the engine's template weapon-fire cue, and the impacts and barks are that same
 * cue and a sine tone at shifted pitch... recording is the part of items 16 and 20 that no
 * amount of C++ supplies." Both halves of that were wrong. The audio already existed in the
 * Godot repository (`assets/audio/`), and the voices did not need recording at all -- macOS
 * ships 177 voices and `tools/make-voices.sh` renders nine lines to WAV offline. Everything
 * here now plays a real file.
 */
struct FHIAudio
{
	// ---- assets, by path so a missing one is a log line rather than a crash
	static USoundClass* SfxClass();
	static USoundSubmix* ReverbSubmix();

	/**
	 * A sound imported by `tools/import-assets.py`, or nullptr if that has not been run.
	 *
	 * `Name` is the file's stem: "gunshot_pistol", "impact", "thief_down". A missing one
	 * returns nullptr and the caller falls back, because a renamed asset becoming a silent
	 * game is exactly the failure this project keeps having to guard against.
	 */
	static USoundBase* Imported(const TCHAR* Name);

	/**
	 * An explicit attenuation, and it is NOT optional -- leaving it out CRASHES the game.
	 *
	 * Measured, from the crash report of the first session a person ever played:
	 *
	 *     Assertion failed: false [File: ./Runtime/Engine/Private/Attenuation.cpp] [Line: 55]
	 *       FBaseAttenuationSettings::GetMaxDimension()
	 *       UAudioComponent::OnRegister()
	 *       FHIAudio::Play() -> FHIAudio::IntruderGunshot() -> AHIGameMode::ResolveThiefShot()
	 *
	 * `GetMaxDimension` switches on `AttenuationShape` and `check(false)`s on anything
	 * outside Sphere/Capsule/Box/Cone. A component built with `NewObject` and given no
	 * attenuation inherits the SOUND's, and the engine's template weapon cue carries one whose
	 * shape does not survive being copied into this project without the template's own
	 * content -- so the shape read as garbage. It fired on the first INTRUDER shot, which is
	 * why the round died within seconds whether or not the player did anything.
	 *
	 * The fix is to stop inheriting: build one valid attenuation and hand it to every
	 * spatialised cue. It also gives this file the falloff it actually wants, instead of
	 * whatever the engine's template happened to use.
	 */
	static USoundAttenuation* Attenuation();

	/**
	 * Is this world point inside the house? Drives the reverb send.
	 *
	 * Read from the sim's own house rectangle rather than from a collision query, so the
	 * question "does this sound sound indoor" and the question "is this body inside" cannot
	 * give different answers.
	 */
	static bool bInsideHouse(const FVector& WorldLocation);

	/**
	 * Play one cue.
	 *
	 * Built by hand rather than with `UGameplayStatics::SpawnSoundAtLocation`, because the
	 * routing has to be set BEFORE the sound starts: a submix send installed after `Play()`
	 * lands in `PendingSubmixSends` on some engine paths and is silently dropped, which is
	 * the kind of no-op that looks exactly like "reverb does not work on this box".
	 *
	 * @param bSpatialized  false for the homeowner's own weapon, which is at the camera and
	 *                      must not be attenuated by the listener's own position.
	 */
	static UAudioComponent* Play(UWorld* World, USoundBase* Sound, const FVector& Where,
		float Volume, float Pitch, bool bSpatialized);

	/** The homeowner fires. Loud, local, and pitched by the weapon. */
	static void PlayerGunshot(UWorld* World, const FVector& MuzzleWorld, bool bShotgun);

	/**
	 * An intruder fires, positioned in the world.
	 *
	 * POSITIONED IS THE POINT: with three of them, an unattributable gunshot is not a
	 * playable game -- the Godot original says the same thing about its tracers, and a report
	 * you cannot place is worse than silence because it makes you turn the wrong way.
	 */
	static void IntruderGunshot(UWorld* World, const FVector2D& SimPos, float SimZ, bool bIndoorHint);

	/** Something was hit: a body, or the house. */
	static void Impact(UWorld* World, const FVector& Where, bool bFlesh);

	/**
	 * A VOICE LINE, by name, at a place.
	 *
	 * Fix-list item 16 asked for a "bark system"; the hook existed and the cue was a pitched
	 * sine, and the file used to say that making it sound like a person "needs recordings".
	 * It needed a speech synthesiser that ships with the OS. `tools/make-voices.sh` renders
	 * these offline; `Line` is the file's stem -- "thief_down", "owner_challenge".
	 *
	 * Clearly synthetic next to a real actor, and still a different category of thing from a
	 * sine: the words are intelligible and the player hears a man. A recording is still the
	 * right end state.
	 */
	static void Voice(UWorld* World, const FVector& Where, const TCHAR* Line, float Volume = 1.0f);

	/**
	 * The line's asset, by stem -- the same lookup `Voice` performs, exposed so that "all nine
	 * lines are actually there" can be ASSERTED instead of assumed.
	 *
	 * A missing line is SILENT: it logs once and plays nothing, which is indistinguishable from
	 * a working game with the volume down. And re-importing a whole asset folder -- which is
	 * what replacing the voices with the neural renders required -- is exactly the operation
	 * that can quietly import eight of nine.
	 */
	static USoundBase* VoiceCue(const TCHAR* Line);

	/** The police arrive. Fix-list item 17's clock, with the Godot original's siren behind it. */
	static void Siren(UWorld* World, const FVector& Where);
};
