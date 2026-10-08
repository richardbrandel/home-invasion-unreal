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
 * Instanced, because the sim describes 26 solids and one draw call beats 26.
 */
UCLASS()
class HIUNREAL_API AHIHouse : public AActor
{
	GENERATED_BODY()

public:
	AHIHouse();

	/** Build one instance per solid. Safe to call more than once; it clears first. */
	void Build();

	/** Walls, in the engine's BasicShapes/Cube. */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> WallInstances;

	/** Non-flat furniture: the things that block a route or a sightline. */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> FurnitureInstances;

	/**
	 * A floor slab over the sim's world bounds, and DELIBERATELY WITHOUT COLLISION.
	 *
	 * It exists so the scene reads as a place rather than as bodies in a void -- nothing
	 * stands on it, because the sim places every body itself and no actor here uses physics.
	 * Giving it collision would add a solid the sim does not know about, which is the one
	 * thing this actor is built to avoid: one source for the collision, not two.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> GroundInstance;

	/**
	 * Turn one sim rectangle plus its vertical span into a world transform.
	 *
	 * The sim's plane is (X, Y) with Z up and Z spanning MinZ..MaxZ, which is already the
	 * engine's layout -- only the units differ, and the cube is one metre across at unit
	 * scale, so a metre measurement IS the scale. Static and pure so the mapping can be
	 * reasoned about without spawning anything.
	 */
	static FTransform BoxTransform(const FBox2D& Rect, float MinZ, float MaxZ);
};
