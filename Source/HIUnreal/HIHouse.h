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
 * Instanced throughout: phase 2's house is 32 wall rows, 48 furniture rows, 9 decks and
 * three railing/screen solids, and one draw call per component beats ~90 boxes.
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

	/** The fork's slice: second-floor slab decks. These collide -- the slab is what stops a
	 *  bullet, a body and a sightline from crossing storeys except through the hole. */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> SlabInstances;

	/** The fork's slice: the visible stair treads under the sim's band. These collide. */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> StairInstances;

	/** The fork's slice: railings at the slab hole's edge. These collide. */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> RailingInstances;

	/**
	 * Phase 2: the below-grade walls. Richard's brief for the basement is an open area
	 * with CINDERBLOCK walls, and cinderblock is not plaster -- so the Floor -1 rows of
	 * WallRows() get their own colliding component with their own material. The split is
	 * paint, not physics: the collision setup is WallInstances' exactly.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> WallBasementInstances;

	/**
	 * Phase-2 realism (2026-10-19): furniture is no longer one box per piece. Each piece is
	 * a BODY -- one colliding box that keeps the sim's exact footprint -- plus decor detail
	 * parts in the same material. The three `*Instances` below collide and keep the collision
	 * contract; the three `*Decor` ones do NOT (a cushion must not stop a bullet the sim's
	 * sofa rect would not). One component per material, as everywhere in this class.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> FurnitureSoftInstances;
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> FurnitureApplianceInstances;
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> FurnitureDarkInstances;
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> FurnitureSoftDecorInstances;
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> FurnitureApplianceDecorInstances;
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> FurnitureDarkDecorInstances;
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> FurnitureWoodDecorInstances;

	/** Phase-2 realism scenery (2026-10-19): the street, sidewalk and driveway (one grey
	 *  material), the neighbor house shells, and the tree canopies. All decor -- the play
	 *  area is unchanged, so none of this may collide. */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> StreetInstances;
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> NeighborInstances;
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> TreeInstances;
	/** Round wheels (the engine Cylinder, not a cube) -- the van and the cars share them.
	 *  One dark component so a wheel is a disc, never a block (2026-10-19). */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> WheelInstances;

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
	 * Phase 2: the basement's floor slab, top face 2 cm proud of `BasementZ` -- the same
	 * proud convention as FloorInstance. It blocks corpses ONLY (PhysicsBody), exactly
	 * like the floors above it: a body shot down there has to land on something, and the
	 * lawn that catches everything else is 2.44 m over its head.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> BasementFloorInstances;

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
	 * The GLAZING alone, split out from the frames so it can be translucent.
	 *
	 * `AddWindow` used to put the pane, the four frame sides, the mullion and the sill into
	 * one component with one material -- so making the pane real glass would have made the
	 * frame and the sill glass too. The glazing is the one thing that wants
	 * `/Game/HIGenerated/M_HIGlass`, and everything else wants the flat trim colour.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> GlassInstances;

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
	 * The coat, and only the coat, because it needs a colour the appliances do not.
	 *
	 * Every prop shared one component and one colour, `Counter` (0.72, 0.73, 0.71), against a
	 * wall of (0.74, 0.70, 0.62) -- so the coat by the front door rendered as a pale cabinet
	 * on the wall. A kettle and a toaster *should* be pale metal; a coat should not, and an
	 * instanced component carries one material, so the coat needs its own.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> CoatInstances;

	/**
	 * The door SURROUND of the front doorway: jambs and a head trim around the opening.
	 *
	 * History: `Walls()` splits the south wall into two runs either side of `DoorGap` -- a
	 * split in a 2-D model, which can only be horizontal -- so the opening was a slot from
	 * the floor to the ceiling and the front of the house had a letterbox in it. Godot
	 * filled the letterbox with a decoration, and the slice followed.
	 *
	 * PHASE 2 CHANGED THAT (an ADDITION, veto-able): with the facade now running to 5.33,
	 * the letterbox became a 3.2 m invisible slot, and this file's own invariant forbids
	 * decoration that lies about geometry -- a drawn wall a pellet flies through. So the
	 * wall above the door head is now a real sim row (`WallRows()`, MinZ 2.10, excluded
	 * from the 2-D projection), and what is left here is a door frame: jambs and a head
	 * trim, decoration in front of real geometry rather than instead of it.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UInstancedStaticMeshComponent> DoorHeadInstances;

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
	 * One wall-mounted fitting: a socket, a switch or the thermostat, in metres.
	 *
	 * Pure data rather than a scatter of `AddBox` calls, because the calls were WRONG once
	 * already and no amount of reading them would have said so: the switch "by the front
	 * door" sat inside `DoorGap`, hanging in the opening with no wall behind it.
	 * `HI.Unreal.WallFittings` asserts each of these is flush against a face of some row
	 * in `FHISim::WallRows()` -- phase 2 rewrote the table for the three-floor house.
	 */
	struct FFitting
	{
		float X, Y, Z, SX, SY, SZ;
	};

	/** Every wall fitting, in the order they are drawn. See the .cpp for the arithmetic. */
	static const TArray<FFitting>& WallFittings();

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

	/** One decorative box with a full rotation -- the gable roof's pitched planes. */
	void AddBoxR(UInstancedStaticMeshComponent* Target, const FVector& Centre,
		const FVector& SizeMeters, const FRotator& Rot);

	/** Compose one furniture row into a colliding body plus decor detail, by its label. */
	void BuildFurniture(const FString& Label, const FBox2D& Rect, float Base, float Height);

	/** The street, sidewalk, driveway, neighbor shells, cars, trees and a street light. */
	void BuildNeighborhood();

	/**
	 * One window in one wall. `Along` is the axis the wall runs down. `FloorZ` lifts the
	 * whole window onto an upper storey; `SillUp`/`HeadUp` are the sill and head heights
	 * ABOVE that floor -- the defaults are the ground floor's ordinary window, and the
	 * small high bathroom windows (§4.6: 0.6 m square at 1.4 m up) override both.
	 */
	void AddWindow(bool bAlongX, float Along, float WallCoord, float Facing, float Width,
		float FloorZ = 0.0f, float SillUp = 0.95f, float HeadUp = 2.15f);

	/** The roof, the floor, the trim, the windows, the fittings, the props and the van. */
	void BuildDressing();
};
