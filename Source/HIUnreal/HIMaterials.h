#pragma once

#include "CoreMinimal.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMeshComponent;

/**
 * Colour, without authoring a single material asset.
 *
 * This is the project's no-Blueprints rule reaching one step further than it has before.
 * A material is a .uasset, so a port that has no editor session cannot make one -- and
 * without one every primitive in the game renders the same default grey: walls, floors,
 * furniture, loot and bodies all indistinguishable. The engine ships one material whose
 * base colour is already exposed as a parameter (`/Engine/BasicShapes/BasicShapeMaterial`,
 * parameter `Color`, confirmed by reading the asset's own name table), so a dynamic
 * instance of it is a colour you can set from C++.
 *
 * What this is NOT: real materials. There is no roughness, no normal map, no texture. It
 * buys legibility, which is the difference between a scene you can read and a grey void.
 * Art is still art.
 */
struct FHIMaterials
{
	/** The engine's parameterised shape material, or nullptr if the engine content moved. */
	static UMaterialInterface* ShapeMaterial();

	/**
	 * A material from `/Game/HIGenerated`, or nullptr if the generator has not been run.
	 *
	 * These are REAL .uassets, and until 2026-10-09 the project's own README asserted that a
	 * .uasset could not exist without a human in an editor -- which is why every surface in
	 * the game is `BasicShapeMaterial` with a flat colour and why the windows are opaque.
	 * `tools/make-assets.py` creates them headlessly through the engine's Python plugin, so
	 * they are derived state like the engine art: reproducible, and `Content/` is gitignored
	 * for both.
	 *
	 * A MISSING asset returns nullptr rather than a default, and every caller must have a
	 * fallback: a renamed asset would otherwise become a silent white surface on screen --
	 * which is exactly the class of failure the port keeps having to guard against.
	 */
	static UMaterialInterface* Generated(const TCHAR* Name);

	/**
	 * Give one material slot of one mesh a flat colour.
	 *
	 * The instance is outered to the mesh and installed into its material array, which is a
	 * UPROPERTY -- so the mesh keeps it alive and no separate cache or bookkeeping is
	 * needed. Returns the instance so a caller that wants to animate the colour later can.
	 */
	static UMaterialInstanceDynamic* Tint(UMeshComponent* Mesh, const FLinearColor& Colour, int32 Slot = 0);

	// The palette, in one place so two things that should match do not drift apart.
	static const FLinearColor Grass;
	static const FLinearColor Path;
	static const FLinearColor Wall;
	static const FLinearColor WallExterior;
	static const FLinearColor Floor;
	static const FLinearColor Ceiling;
	static const FLinearColor Wood;
	static const FLinearColor Sofa;
	static const FLinearColor Counter;
	static const FLinearColor Door;
	static const FLinearColor Trim;
	static const FLinearColor Glass;
	static const FLinearColor Van;
	/** Tyres, and van glass reuses `Glass`. Contrast is the only detail this project has. */
	static const FLinearColor Tyre;
	/**
	 * A coat, and the reason it has its own colour is that it did not.
	 *
	 * Props shared `Counter` (0.72, 0.73, 0.71) while the wall is (0.74, 0.70, 0.62) -- within
	 * a few percent -- so the coat hanging by the front door rendered as a pale cabinet
	 * attached to the wall. Same failure the interior palette was already tuned to avoid:
	 * two things that should read as different objects landing on the same colour.
	 */
	static const FLinearColor Coat;
	static const FLinearColor PlayerSkin;
	static const FLinearColor IntruderSkin;
	static const FLinearColor Gun;
	static const FLinearColor Blood;
	static const FLinearColor WarmLight;
};
