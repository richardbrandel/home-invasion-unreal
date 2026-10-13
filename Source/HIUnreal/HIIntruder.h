#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HISim.h"
#include "HIIntruder.generated.h"

#ifndef HIUNREAL_API
#define HIUNREAL_API
#endif

/**
 * One intruder, as a VIEW of an FHIThief.
 *
 * THE HIT VOLUME IS A CAPSULE, NOT THE MESH, and that is deliberate rather than lazy. The
 * sim models a man as a circle of ThiefRadius that blocks nothing -- every hit in this game
 * is decided by `DamageIntruder(id, damage)`, with no notion of where on the body a pellet
 * landed. Tracing against the character's physics asset would therefore be a SECOND, more
 * detailed opinion than the model has, and the two would disagree at the edges: a shot that
 * clipped a shoulder would wound a man the sim considers untouched. A capsule of exactly
 * the sim's radius and height is the honest hit target. The mesh is what you look at.
 *
 * Like the pawn it does not tick. The game mode steps the round and calls SyncFromSim.
 */
UCLASS()
class HIUNREAL_API AHIIntruder : public AActor
{
	GENERATED_BODY()

public:
	AHIIntruder();

	virtual void BeginPlay() override;

	/**
	 * Matches FHIThief::Id, so the game mode can pair a body with its sim state -- AND so
	 * the body knows which of the cast it is (`FHICharAnim::IntruderTagForId`). The Id is
	 * assigned AFTER SpawnActor returns, which is why the visuals are built on the first
	 * SyncFromSim rather than in BeginPlay: built there, every man would wear Id 0's face.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	int32 ThiefId = 0;

	/**
	 * A timed one-shot on the body -- `shoot` when this man fires. Same handle the
	 * homeowner has, for the same reason: the events live in the game mode and the
	 * body lives here.
	 */
	void PlayActionClip(const TCHAR* Clip, float HoldSec);

	/**
	 * Put the body where the sim says it is.
	 *
	 * `Dt` is needed only for animation: the sim carries `Prev`, so measured speed is
	 * (Pos - Prev) / Dt. Reading a speed FIELD instead would be wrong -- the sim's `Speed`
	 * is the distance he intends to cover this frame, not a rate.
	 */
	void SyncFromSim(const FHISim::FHIThief& T, float Dt);

	/**
	 * The sim's plane angle as a world rotation.
	 *
	 * The derivation (t = 90 - a - the mesh's own facing) lives in
	 * `FHICharAnim::BodyRotationFor`, with the facing as a MEASURED constant the Cast test
	 * recomputes from the bind pose. A quarter turn out is silent, which is why it is a
	 * measurement and a test rather than a comment.
	 */
	static FRotator BodyRotation(float SimYaw);

	/**
	 * The actor's origin, at the sim's own coordinate: on the FLOOR, between his feet.
	 *
	 * An explicit root rather than making the capsule the root, because a capsule's origin is
	 * its centre and a body's is between its feet. Making either one the root means one of the
	 * two needs a standing offset, and an offset applied to a root component is the ambiguous
	 * case -- it is both "relative to the actor" and "the actor's own position", and which one
	 * it behaves as depends on when it is set. A root at the feet makes both children plain.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class USceneComponent> Root;

	/** The hit volume: a capsule of the sim's own radius and height. Invisible, blocks shots. */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UCapsuleComponent> HitVolume;

	/** What you actually look at. No collision of its own. */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class USkeletalMeshComponent> Body;

	/** The pistol, parented into the right hand's socket. Hidden when his hands are full. */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class UStaticMeshComponent> Gun;

	/**
	 * WOUNDS: blood ON the man, not only on the wall behind him.
	 *
	 * Fix-list item 19, and its exact wording is *"a hit produces a bullet hole and a stagger.
	 * Nothing on the person"* -- the hole was on the wall, the stagger was a badge the HUD
	 * printed, and the man himself was untouched. These are small dark quads PARENTED TO
	 * BONES, which is the detail that matters: parented to the mesh they would slide across
	 * the skin as he walks, because a skeletal mesh's component space is not its pose.
	 *
	 * The count is derived from health in `SyncFromSim`, not pushed from the damage site, so
	 * it stays right across a round restart for free.
	 */
	static constexpr int32 MaxWounds = 5;

	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TArray<TObjectPtr<class UStaticMeshComponent>> Wounds;

	/** Show the first `Count` wounds, hide the rest. Cheap enough to call every frame. */
	void ShowWounds(int32 Count);

	/**
	 * Where wound number `Index` goes: bone, local offset in cm, and size in cm.
	 *
	 * Pure and static so the placement is asserted without a world -- the lesson of every bug
	 * in this port is that the mapping is where the mistake is, not the rendering.
	 */
	static bool WoundPlacement(int32 Index, FName& OutBone, FVector& OutOffset, FVector& OutSize);

	/** How many wounds a man at `Hp` should be showing. Pure, so it can be asserted. */
	static int32 WoundsForHealth(float Hp);

	/**
	 * How long after the death clip starts the body is handed to PHYSICS.
	 *
	 * HISTORY: the engine's six `MM_Death_*` clips were flinches, not falls -- measured in a
	 * driven soak on 2026-10-12, across `MM_Death_Back_01` the pelvis went 96.6 cm -> 87.5 cm
	 * and never lower, and the clip held that pose. That is why the fall became physics at
	 * all: *"when the intruders get shot, they slouch over. They should fall to the ground."*
	 *
	 * The cast's own `death` clip IS a real fall -- Mixamo's, 3.03 s, Hips 0.96 m -> 0.14 m,
	 * measured from the FBX keys by the Godot project's anim probe. The ragdoll handover
	 * stays anyway, mid-fall at 0.85 s: physics finishes the fall against whatever he
	 * actually fell onto (a clip cannot know about the coffee table), and the corpse settles
	 * instead of floating a baked floor-slide. The delay is asserted against the clip's own
	 * length by `HI.Unreal.DeathFall`.
	 *
	 * IT IS A TIMER, NOT A COUNT OF FRAMES, and that is the whole reason it works. Once the
	 * round is decided the game mode stops calling `SyncFromSim` -- "a decided round is inert at
	 * the sim level too" -- so a handover that depended on being called every frame would never
	 * happen for the LAST man killed, who is precisely the one the player is looking at. A world
	 * timer fires whether or not anyone is still syncing the corpse.
	 */
	static constexpr float RagdollAfterDeath = 0.85f;

private:
	void BuildVisuals();

	bool bVisualsBuilt = false;

	/** Which of the cast this body is: James, Brute or Martha, by ThiefId. */
	FString CastTag;

	/** Reload-anim edge detector: `Reloading` is a countdown, and a one-shot is an event. */
	bool bWasReloading = false;

	/** Latched so the death clip is started once rather than restarted every frame. */
	bool bDeathPlayed = false;
	/** Latched when the body is given to physics, after which the animation must let go of it. */
	bool bRagdolled = false;
	/** Fires `GoLimp` a fixed time after the death clip starts, decided round or not. */
	FTimerHandle RagdollTimer;

	/** Let go: simulate physics, so the man actually falls and settles on the floor. */
	void GoLimp();
};
