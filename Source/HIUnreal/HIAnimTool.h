#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HIAnimTool.generated.h"

/**
 * Pinning the Mixamo clips down, at import time, in the assets themselves.
 *
 * WHY THIS EXISTS: the sim owns position. A clip whose Hips track travels moves the
 * MESH away from the body the sim is stepping, and a clip whose Hips track ROTATES
 * fights the yaw the game sets every frame. The raw Mixamo exports do both --
 * measured on the same FBX files by the Godot project's `test/anim_probe.gd`:
 *
 *     walk  +1.748 m over 1.033 s        run   +3.552 m over 0.633 s
 *     crouch_walk +1.529 m over 1.700 s  aim   +0.735 m AND a 143.2 deg Hips turn
 *     shoot a +/-70 deg Hips swivel (returns)    death +1.368 m (a real fall)
 *
 * Godot fixed this at LOAD time (`_flatten_root_travel`, `_strip_to_upper_body` in
 * actor.gd). This port fixes it at IMPORT time, in the .uasset, because the runtime
 * here evaluates compressed sequences through `AnimationSingleNode` and has no
 * per-frame hook where tracks could be rewritten. `tools/import-assets.py` calls
 * `normalize_character_clip` on every imported clip; `Content/` stays gitignored
 * derived state and this tool plus that script remain the source of truth.
 *
 * WHAT EACH MODE DOES, and why it is not one rule:
 *
 *   "locomotion"  pin the Hips' HORIZONTAL translation to its first key. The vertical
 *                 component is kept deliberately -- it is the bob, and the crouch
 *                 clips' low Hips height IS the crouch. Rotation is kept: a walk's
 *                 few degrees of Hips counter-swing is the gait, not a fight.
 *   "aim"         pin the Hips completely (all translation AND rotation) and pin the
 *                 rotations of Spine/Spine1/Spine2 and every leg bone to their first
 *                 keys. This is the UE-shaped equivalent of Godot's upper-body strip:
 *                 the 143 deg turn, the 0.74 m travel and the mid-clip 25 cm Hips
 *                 drop all live in those tracks, and what remains -- shoulders, arms,
 *                 hands, head -- is the aiming motion the clip was fetched for. A
 *                 stripped-to-upper-body overlay is impossible in single-node mode;
 *                 frozen-from-the-waist-down with live arms is what one node can do,
 *                 and it is also what the Godot strip amounted to on screen, because
 *                 the removed bones held a standing pose there too.
 *   "shoot"       pin the Hips' rotation (the swivel) and horizontal translation, keep
 *                 everything else: the spine's small counter-twist and the arm's
 *                 recoil burst ARE the shot.
 *   "none"        measure and report only. This is what `death` gets -- the fall IS
 *                 the root motion, the clip genuinely puts the man on the floor
 *                 (Hips 0.96 m -> 0.14 m), and nothing has to snap back afterwards.
 *
 * Every call returns a one-line REPORT with the measured travel and rotation spread
 * and what was pinned, because a tool that silently found nothing to do and a tool
 * that silently did nothing are the same failure this project keeps guarding against.
 *
 * Editor-only by nature (it rewrites and saves assets); the runtime build compiles a
 * stub that reports exactly that.
 */
UCLASS()
class HIUNREAL_API UHIAnimTool : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Normalize one imported animation sequence, in place, and save the package.
	 *
	 * @param PackagePath    e.g. `/Game/HIImported/characters_louise/walk.walk`
	 * @param Mode           "locomotion" | "aim" | "shoot" | "none"
	 * @param RaiseWristsCm  when > 0, pre-multiply measured LOCAL-space deltas onto the
	 *                       LeftArm/RightArm tracks so the wrists -- and a gun parented
	 *                       to a hand bone -- sit this many centimetres higher, with the
	 *                       hands carrying the gun instead of it floating away from them.
	 *                       The deltas were derived by `tools/gunraise.py` from the
	 *                       in-pose `-HIProbe` bone dump of 2026-10-15 (capture
	 *                       `bones2`) and are calibrated for 15.24 cm = 6 inches; other
	 *                       amounts rescale the angle linearly, exact to first order
	 *                       over that range. A raise that finds no track reports a MISS
	 *                       like a pin does -- it must not read as a success.
	 * @returns a report line: measurements, what was pinned, and SAVED/FAILED.
	 */
	UFUNCTION(BlueprintCallable, Category = "Home Invasion")
	static FString NormalizeCharacterClip(const FString& PackagePath, const FString& Mode,
		float RaiseWristsCm = 0.0f);

	/**
	 * Measure one imported sequence's SAVED key data without touching it: per bone, the
	 * translation spread and the rotation spread against the first key, for the bones the
	 * normalization recipe and the Cast test care about (Hips, Spine, Spine1, Spine2,
	 * LeftUpLeg, RightArm).
	 *
	 * This exists because "the tool reported pinned 12/12 SAVED" and "the Cast test reads
	 * a 3-18 deg spread on the same tracks" were both true on 2026-10-13, and the only
	 * way to tell a write that did not stick from a test that measures the wrong thing is
	 * a third reader with no stake in either answer. Python-callable, read-only, and it
	 * changes no asset.
	 */
	UFUNCTION(BlueprintCallable, Category = "Home Invasion")
	static FString MeasureCharacterClip(const FString& PackagePath);
};
