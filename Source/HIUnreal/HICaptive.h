#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HICaptive.generated.h"

#ifndef HIUNREAL_API
#define HIUNREAL_API
#endif

/**
 * One man taken alive and hanging in the basement by his wrists.
 *
 * WHY THIS IS ITS OWN ACTOR (2026-11-05). The restraint was first built by reusing the intruder's
 * body: fall, teleport, pose. It could not render the pose. `HIAnimTool::MakeHangClip` wrote a correct
 * one-key clip (verified at runtime: the `LeftArm` key is the raised rotation, and the hand sockets
 * reported the arm overhead) and the RENDERED mesh stayed in the reference pose, because the intruder
 * is an actor the game mode steps by hand and never ticks -- so its skeletal mesh never evaluates an
 * animation into the render state.
 *
 * A captive has nothing else to do, so it gets the thing the intruder cannot have: an ordinary tick.
 * The mesh is built exactly the way the intruder builds his (`SetSkeletalMeshAsset` +
 * `FHICharAnim::SetupCharacter`, which also hides the brute's baked axe), so the character looks
 * identical -- same mesh, same materials, same face. The only difference is that this one is animated
 * by the engine rather than by hand, which is the whole point.
 */
UCLASS()
class HIUNREAL_API AHICaptive : public AActor
{
	GENERATED_BODY()

public:
	AHICaptive();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Build him and put him on the hook.
	 *
	 * @param InThiefId  the sim id, so a pellet can be attributed to the right man
	 * @param InTag      the cast tag (dancer / alina / brute), for the mesh and the hang clip
	 * @param Station    world position of the cell, in CENTIMETRES (the slab under the chains)
	 */
	void Hang(int32 InThiefId, const FString& InTag, const FVector& Station);

	/** The sim id this body belongs to. */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	int32 ThiefId = 0;

	/** Killed while chained: let go of physics and drop to the slab. */
	void GoLimp();

	/**
	 * Struck with the stick. Picks one of three reactions -- see the .cpp -- and plays it for its
	 * length, then goes back to hanging. Richard, 2026-11-05: "if the prisoner gets hit with a stick,
	 * they will squirm... I think we will have some random movements with each stick hit. One of them
	 * could be running in place for 2 seconds. If we have 3 types of reactions, you randomly mix them
	 * up."
	 */
	void Squirm();

	/** Capture-only: force a specific reaction, so all three can be photographed side by side. */
	void SquirmAs(int32 Kind);

	/** The hit volume -- a capsule of the sim's man, exactly as the intruder's is. */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UCapsuleComponent> Hit;

	/** The body. Root, so the actor's transform IS the body's. */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class USkeletalMeshComponent> Body;

	/** The cast tag, so a failure names the character. */
	FString CastTag;

private:
	/** The clip the hang poses with: arms overhead, knees bent. */
	TObjectPtr<class UAnimationAsset> HangClip;

	/** Chains from the ceiling to each wrist, and a cuff at each wrist. */
	TObjectPtr<class UInstancedStaticMeshComponent> ChainInstances;
	TObjectPtr<class UInstancedStaticMeshComponent> CuffInstances;
	FVector ChainAnchor[2] = { FVector::ZeroVector, FVector::ZeroVector };
	bool bHanging = false;
	bool bDead = false;
	bool bLoggedCuff = false;

	/** Squirm state: which reaction, how long, and how far through it we are. */
	int32 SquirmKind = -1;
	float SquirmT = 0.0f;
	float SquirmLen = 0.0f;
	/** The last reaction, so the same one does not come up twice in a row. */
	int32 LastSquirmKind = -1;
	FRotator HangRotation = FRotator(7.0f, 270.0f, 3.0f);
	FVector HangLocation = FVector::ZeroVector;
	void BeginSquirm(int32 Kind);
	void EndSquirm();

	/** Redraw the chains to wherever the wrists are this frame. */
	void UpdateChains();
};
