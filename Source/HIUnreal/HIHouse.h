#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HISim.h"
#include "HIHouse.generated.h"

#ifndef HIUNREAL_API
#define HIUNREAL_API
#endif

/**
 * The house, generated from the sim's own description of itself.
 *
 * There is no level geometry and no .umap authored by hand: FHISim::Walls(),
 * FurnitureRects() and SolidSpans() already say where every solid is and how tall it is,
 * and this turns each one into a box. That is not a convenience -- it means the collision
 * the AI reasons about and the collision the player walks into are derived from ONE
 * source, so they cannot drift apart. A hand-built level would be a second opinion.
 *
 * THE INVARIANT THAT GOVERNS EVERYTHING ELSE IN THIS FILE: only walls and non-flat furniture
 * may collide. Everything decorative -- the roof, the skirting, the window frames, the
 * fittings, the props, the van -- is built with NO COLLISION AND NO QUERY RESPONSE, because
 * the sim's rectangle list is the only set of solids the AI knows about. A picture frame
 * that stopped a pellet would be a wall the intruder cannot see and cannot route around, and
 * the player would be shot by a man aiming through what he thought was cover. Decoration is
 * allowed to be wrong about beauty; it is not allowed to be wrong about geometry.
 *
 * Instanced throughout: the sim describes 26 solids, and one draw call beats 26.
 */
UCLASS()
class HIUNREAL_API AHIHouse : public AActor
{
	GENERATED_BODY()

public:
	AHIHouse();

	/** Build one instance per solid plus all the dressing. Idempotent; it clears first. */
	void Build();

	/** Walls, in the engine's BasicShapes/Cube. These collide. */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> WallInstances;

	/** Non-flat furniture: the things that block a route or a sightline. These collide. */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> FurnitureInstances;

	/**
	 * The lawn, over the sim's world bounds, DELIBERATELY WITHOUT COLLISION.
	 *
	 * It exists so the scene reads as a place rather than as bodies in a void -- nothing
	 * stands on it, because the sim places every body itself and no actor here uses physics.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> GroundInstance;

	/**
	 * The interior floor, and the reason fix-list item 3's z-fighting is gone.
	 *
	 * The lawn and the interior floor are two different surfaces at the same height, and two
	 * coplanar faces render as a jagged flicker of whichever one the depth test happens to
	 * prefer. Godot hit exactly this and solved it the same way: the interior floor's top
	 * face sits 2 cm proud, which is under a shoe and far over a depth-buffer epsilon.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> FloorInstance;

	/**
	 * The roof and its ceiling underside.
	 *
	 * Fix-list item 8's neighbours and the daylight work both need this. Without a roof the
	 * sun lands on the interior floor and the house reads as a roofless diorama -- the same
	 * fault the Godot original hit when it went to daylight. It must not collide: the sim has
	 * no roof, so a colliding one would be a solid that stops pellets and the AI's sightlines
	 * but not the AI's own idea of the world.
	 *
	 * It DOES cast a shadow, and that is the point -- a shadow is a rendering property, not a
	 * collision one, and occlusion is exactly what a roof is for.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> RoofInstance;

	/**
	 * The ceiling's underside, painted separately from the roof above it.
	 *
	 * Roof and ceiling are the same slab seen from two sides, and they cannot be one
	 * component because a component carries ONE colour: tinting for the outside made the
	 * inside of every room brown, which is the first thing visible in an interior frame.
	 * Two thin slabs are cheaper than two materials and read correctly from both sides.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> CeilingInstance;

	/** Skirting and picture rail: derived from Walls(), not hand-placed. Fix-list item 3. */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> TrimInstances;

	/** Window frames, sills and glazing. Fix-list item 5. */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> WindowInstances;

	/**
	 * Sockets, switches, thermostats, light fittings and a smoke alarm. Fix-list item 8.
	 *
	 * Hand-placed rather than generated, and that is honest rather than lazy: "nothing on the
	 * walls except windows" describes an absence, and there is no rule in the sim from which
	 * the position of a light switch could be derived. A person decides where a switch goes.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> FittingInstances;

	/** Coats, keys, a phone, a kettle: the small stuff. Fix-list items 10 and 11. */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> PropInstances;

	/**
	 * The van the intruders arrive in.
	 *
	 * Its rectangle is in the sim (`FHISim::Van`) but the actor layer had never drawn it, so
	 * the premise of the game -- a thief arrives BY VAN, carries loot TO THE VAN -- had no
	 * van in it. Scenery: it never moves, and it does not collide.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> VanInstances;

	/**
	 * Tyres, and the reason is contrast rather than detail.
	 *
	 * The first render of the van showed a flat green slab standing on four green legs, which
	 * reads as a garden table. Colour is the only cue this project has -- there are no
	 * textures and no normal maps -- so the wheels have to be a different COLOUR from the
	 * body or they are not wheels.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> VanDetailInstances;

	/**
	 * Turn one sim rectangle plus its vertical span into a world transform.
	 *
	 * The sim's plane is (X, Y) with Z up and Z spanning MinZ..MaxZ, which is already the
	 * engine's layout -- only the units differ, and the cube is one metre across at unit
	 * scale, so a metre measurement IS the scale. Static and pure so the mapping can be
	 * reasoned about without spawning anything.
	 */
	static FTransform BoxTransform(const FBox2D& Rect, float MinZ, float MaxZ);

private:
	/**
	 * Create one decorating component. A MEMBER rather than a free helper, because
	 * `CreateDefaultSubobject` is protected and only valid inside a constructor -- which is
	 * why this is called from AHIHouse's constructor and from nowhere else.
	 */
	class UInstancedStaticMeshComponent* MakeDecor(const TCHAR* Name, class UStaticMesh* Cube,
		class USceneComponent* Parent);

	/** One decorative box, in metres, with its centre given rather than its corner. */
	void AddBox(UInstancedStaticMeshComponent* Target, const FVector& Centre,
		const FVector& SizeMeters, float YawDegrees = 0.0f);

	/** One window in one wall. `Along` is the axis the wall runs down. */
	void AddWindow(bool bAlongX, float Along, float WallCoord, float Facing, float Width);

	/** The roof, the floor, the trim, the windows, the fittings, the props and the van. */
	void BuildDressing();
};
