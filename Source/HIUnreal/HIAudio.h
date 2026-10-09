#pragma once

#include "CoreMinimal.h"

class UAudioComponent;
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
 * WHAT IS A STAND-IN. The gunshots are the engine's template weapon-fire cue, and the impacts
 * and barks are that same cue and a sine tone at shifted pitch. They are placeholders that
 * make the timing and the mix real; the foley is not. Recording is the part of items 16 and
 * 20 that no amount of C++ supplies.
 */
struct FHIAudio
{
	// ---- assets, by path so a missing one is a log line rather than a crash
	static USoundClass* SfxClass();
	static USoundSubmix* ReverbSubmix();
	static USoundBase* Gunshot();
	static USoundBase* PistolShot();
	static USoundBase* SineTone();

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
	 * A short non-verbal bark: a placeholder for a voice.
	 *
	 * Fix-list item 16 asks for a "bark system" and that is what this is -- the hook, at the
	 * right moments, with a cue attached. The cue is a pitched sine and sounds like a piece of
	 * equipment, not a person. Making it sound like a person needs recordings.
	 */
	static void Bark(UWorld* World, const FVector& Where, float BasePitch);
};
