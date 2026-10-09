#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "HISim.h"
#include "HIHUD.generated.h"

#ifndef HIUNREAL_API
#define HIUNREAL_API
#endif

/**
 * The HUD, drawn on the canvas.
 *
 * Canvas rather than UMG, and that is the project's no-Blueprints rule biting in a good
 * way: a UMG widget is a .uasset, which cannot be authored from text, so a canvas HUD is
 * the only kind this project can build headlessly. It also needs no font asset -- the
 * engine's own fonts are enough for lines of text.
 *
 * The FORMATTING is a static function that takes a round and returns strings, so what the
 * HUD says can be asserted without a canvas, a viewport, or a world.
 *
 * THE CROSSHAIR IS THE POINT OF THIS FILE, and it was missing. The Godot original has one,
 * and it is not decoration: the shot ray is cast from the CAMERA along the aim, so without a
 * reticle the player has no way to know where that is -- and the aim scheme puts it somewhere
 * other than the screen centre whenever the cone is on. The original also learned this the
 * hard way: its first reticle was 2 px of white on a cream wall and aliased away below the
 * reference resolution, so it read as "the crosshair only sometimes exists". Hence a dark
 * outline under a light core rather than a single line.
 */
UCLASS()
class HIUNREAL_API AHIHUD : public AHUD
{
	GENERATED_BODY()

public:
	AHIHUD();

	virtual void DrawHUD() override;

	/**
	 * What the HUD says, one line per entry.
	 *
	 * Pure, and separated from the drawing on purpose: a HUD that reports the wrong number
	 * is a bug you can only see by looking, and looking is the one thing this port cannot
	 * do for itself. Everything that can be checked without eyes is checked here instead.
	 */
	static TArray<FString> RoundLines(const FHISim::FHIRound& Round);

	/**
	 * Where the reticle sits, as a 0..1 fraction of the viewport, given the aim direction.
	 *
	 * Pure and static so the projection can be asserted without a canvas. It projects the aim
	 * by hand into the camera's own basis rather than calling `ProjectWorldToScreen`: that
	 * returns PIXELS, and a HUD laid out in fractions has to divide by the viewport size
	 * anyway -- doing it twice is how the reticle ends up in the wrong place at any
	 * resolution but the one it was written at. Godot hit exactly this and wrote it out.
	 *
	 * Returns false when the aim is behind the camera, in which case there is no position.
	 */
	static bool ReticleFraction(const FVector& AimDir, const FVector& CameraForward,
		const FVector& CameraRight, const FVector& CameraUp, float HalfFovTangent,
		float AspectRatio, FVector2D& OutFraction);

	/**
	 * The patch of world the minimap shows.
	 *
	 * NOT `FHISim::Bounds`. The world is 60 x 52 m and the house is 16 x 11, so a map drawn
	 * to the world bounds spends most of its area on empty lawn and renders the house as a
	 * small pale blob -- which is what the first version did, and a map you have to squint at
	 * is a map that does not answer the only question it is asked. This is the house, the
	 * driveway and the van, sized to the panel's own aspect so nothing is letterboxed.
	 */
	static FBox2D MinimapView();

	/**
	 * Sim coordinates to a 0..1 position on the minimap. Pure, so it can be asserted.
	 *
	 * The V axis is FLIPPED: the sim's +Y is the direction of the driveway, and on a map the
	 * driveway must be at the top rather than the bottom, or every reading of it is mirrored.
	 */
	static FVector2D MinimapFraction(const FVector2D& SimPos);

private:
	/** The reticle, at whatever fraction of the viewport the aim projects to. */
	void DrawCrosshair(const FVector2D& Fraction);

	/** A bar for the homeowner's health, because a number alone is easy to miss. */
	void DrawHealthBar(const FHISim::FHIRound& Round, float X, float Y);

	/** The end-of-round banner: what happened, and how to play again. */
	void DrawOutcome(const FHISim::FHIRound& Round);

	/**
	 * The minimap: the house, what the homeowner can see, and who is standing in it.
	 *
	 * The Godot original has a sight-cone minimap and this port had none, which is a much
	 * bigger deal than it sounds. The ported sim already carries `VisibilityPolygon` --
	 * ported, tested, and until now referenced by NOTHING, because its only consumer was the
	 * minimap. Without it a player has no way to know that a man is in the kitchen, and the
	 * game becomes one you lose to a stranger walking in behind you.
	 *
	 * INTRUDERS ARE ONLY DRAWN WHEN THE HOMEOWNER CAN ACTUALLY SEE THEM, which is the same
	 * fog of war the original keeps: the minimap is not a wallhack, and printing every
	 * intruder's position would delete the reason to look anywhere.
	 */
	void DrawMinimap(const FHISim::FHIRound& Round);

	/**
	 * The sight polygon, recomputed every `MinimapPolyEvery` frames rather than every frame.
	 *
	 * `VisibilityPolygon` casts one ray per candidate angle -- a few thousand of them against
	 * every solid in the world -- and the Godot original caches it for exactly this reason
	 * (`minimap.gd`'s `POLY_EVERY`, "the polygon costs ~2.1 ms"). At 60 fps a three-frame
	 * cache reads as instant and pays the cost a third as often.
	 */
	const TArray<FVector2D>& CachedSightPolygon(const FVector2D& From);

	/** The latest event's message, in the middle of the screen where it is read. */
	void DrawMessage(const FString& Text, float SecondsLeft);

	/**
	 * A red edge while the homeowner is being hurt.
	 *
	 * The GameMode sets the timestamp; this only reads it. It exists because losing health
	 * with no cue is the single most confusing thing a shooter can do -- the player's health
	 * was already shown as a number, and a number in the corner is not a thing you notice
	 * while being shot at from behind.
	 */
	void DrawDamageFlash(float SecondsSinceHit);

	/** Frame counter and cache for `CachedSightPolygon`. */
	int32 MinimapFrame = 0;
	TArray<FVector2D> SightPolygon;
};
