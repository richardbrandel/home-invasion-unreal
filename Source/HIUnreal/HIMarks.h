#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HIMarks.generated.h"

#ifndef HIUNREAL_API
#define HIUNREAL_API
#endif

/**
 * A mark left on the world: a bullet hole, or what a body leaves behind.
 *
 * Fix-list item 19 -- "a hit leaves a bullet hole and a stagger. Nothing on the person."
 * Nothing here is on the person either; what this adds is that a hit leaves a mark WHERE it
 * landed, and a dead man leaves one where he fell. That is the part that makes a firefight
 * legible after the fact: without it, a fight in a room leaves the room exactly as it was.
 *
 * A flattened disc (a cylinder squashed thin) rather than a decal, because a decal material
 * is a .uasset and this project has no editor session. It is a few millimetres thick and
 * dropped 3 mm through the surface it sits on, so it cannot z-fight with the thing it marks.
 */
UCLASS()
class HIUNREAL_API AHIMark : public AActor
{
	GENERATED_BODY()

public:
	AHIMark();

	/**
	 * Become a mark at `Where`, facing `Normal`, scaled by `SizeMeters` and coloured.
	 *
	 * Reused rather than spawned fresh: a firefight produces hundreds and the Godot original
	 * found this out -- it keeps a fixed pool (`MARK_MAX`) and rewrites the oldest, because
	 * unbounded decals are a slow leak that only shows up in a long round.
	 */
	void Stamp(const FVector& Where, const FVector& Normal, float SizeMeters, const FLinearColor& Colour);

	/**
	 * Throw away the cached dynamic material, so the next Stamp builds a fresh one.
	 *
	 * THE BLOOD DECAL CRASHED THE GAME THREE ROUNDS IN (2026-11-04). A pooled mark keeps its
	 * `DecalMat` between rounds; round 4 stamped through it and called a virtual on a material
	 * instance whose object had been destroyed, which is a jump to 0x0 -- measured under gdb:
	 * `AHIMark::Stamp` -> `UMaterialInstanceDynamic::SetVectorParameterValue` ->
	 * `GameThread_UpdateMIParameter` -> 0x0000000000000000. Rebuilding the instance once per
	 * round costs one object per mark and removes the stale-object window entirely.
	 */
	void ResetDecalMaterial();

	/**
	 * The rotation that lays the slab in the plane a normal implies: its thin Z along the
	 * normal.
	 *
	 * Pure and static so the orientation is ASSERTED rather than looked at -- a still frame of
	 * a wall with holes in it did not reveal that they were all standing out of it for two
	 * sessions. See the definition for what the obvious `Normal.Rotation()` gets wrong.
	 */
	static FRotator SlabRotation(const FVector& Normal);

	/**
	 * Become a LENGTH of something between two points: a tracer, or a muzzle flash's streak.
	 *
	 * The same primitive as `Stamp`, stretched along its own X and aimed down the segment.
	 * A tracer is not a decal and must not persist, so the caller owns its lifetime -- see
	 * AHIGameMode's flash pool, which hides these on a timer.
	 */
	void StampLine(const FVector& From, const FVector& To, float ThicknessMeters, const FLinearColor& Colour);

	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UStaticMeshComponent> Quad;

	// UPROPERTY ON ALL THREE (2026-11-04). `Quad` had it; these three did not, so the garbage
	// collector could not see them and was free to destroy the objects they point at while the
	// actor stayed alive. That is the whole of the crash that killed three separate runs three
	// rounds in: `AHIMark::Stamp` is handed a pooled mark, `DecalMat` points at a collected
	// material instance, and the next call jumps through a null vtable -- or, with the guard
	// that was tried first, walks a stale object index and dies inside `IndexToObject`.
	// A `TObjectPtr` WITHOUT `UPROPERTY` IS JUST A RAW POINTER; the smart-pointer spelling buys
	// nothing for lifetime. Measured under gdb, twice, from two different builds.
	/** The plane mesh for textured holes, and the cylinder the tracers are drawn with. */
	UPROPERTY()
	TObjectPtr<class UStaticMesh> PlaneMesh;
	UPROPERTY()
	TObjectPtr<class UStaticMesh> CylinderMesh;
	/** M_HIMark instance while this slab is a bullet hole; null when it is a tracer rod. */
	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> DecalMat;
	bool bUsingDecal = false;

private:
	/**
	 * The colour currently on the mesh, so a reused mark is only re-tinted when it changes.
	 *
	 * A dynamic material instance per stamp would be one object per PELLET -- a shotgun is
	 * eight of them and the fire rate is four a second -- which is pure churn through the
	 * garbage collector for a colour that is almost always the same as last time.
	 * Sentinel-initialised to a colour nothing else can be, so the first stamp always tints.
	 */
	FLinearColor Tinted = FLinearColor(-1.0f, -1.0f, -1.0f, -1.0f);
};
