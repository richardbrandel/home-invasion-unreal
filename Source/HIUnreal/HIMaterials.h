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
	static const FLinearColor PlayerSkin;
	static const FLinearColor IntruderSkin;
	static const FLinearColor Gun;
	static const FLinearColor Blood;
	static const FLinearColor WarmLight;
};
