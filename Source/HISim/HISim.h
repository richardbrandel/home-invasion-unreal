#pragma once

#include "CoreMinimal.h"

// Game modules are compiled monolithically, so Unreal does not define this for us. Declared
// here as empty rather than removed, so the struct's API decoration is correct if HISim is
// ever split into a separate DLL.
#ifndef HISIM_API
	#define HISIM_API
#endif

/**
 * Home Invasion simulation, ported from scripts/sim.gd.
 *
 * COORDINATES. Godot uses Vector2 where .x is world X and .y is world Z; the sim is a
 * top-down XZ plane and height exists only in the renderer. This port keeps that mapping
 * exactly -- FVector2D.X = world X, FVector2D.Y = world Z -- so the Godot tests remain a
 * direct specification. The 3-D work (fix-list item 18, elevation) adds a real height axis
 * to the *structures* without changing this 2-D convention, so a 2-D assertion still means
 * what it meant.
 *
 * RECTANGLES. Godot's Rect2 has position (min corner) and end (max corner); this uses
 * FBox2D, whose Min/Max are the same thing. Godot's get_center() is (Min+Max)*0.5.
 *
 * Units are metres. The player is ~1.8 tall, walls 3.0, the van 2.4.
 */
struct HISIM_API FHISim
{
	// ------------------------------------------------------------------ world
	/** x, z, width, depth -- matches Godot's Rect2(-8, -19, 16, 11). */
	static const FBox2D House;
	static constexpr float WallHeight = 3.0f;
	static constexpr float WallThickness = 0.25f;
	/** Front door, in the south wall. Godot Vector2(-1, 1). */
	static const FVector2D DoorGap;
	/** Opening in the interior wall. Godot Vector2(-2, 2). */
	static const FVector2D HallGap;
	static constexpr float InteriorZ = -14.0f;
	static const FBox2D Bounds;

	static const FBox2D Van;
	/** Godot Vector2(0, 4.5) -- the drop point at the van. */
	static const FVector2D Drop;

	// ------------------------------------------------------------------ tuning
	static constexpr float PlayerSpeed = 4.2f;
	static constexpr float PlayerSprintSpeed = 6.6f;
	static constexpr float PlayerCrouchSpeed = 2.0f;
	static constexpr float PlayerAccel = 20.0f;
	static constexpr float PlayerDecel = 30.0f;
	static constexpr float ThiefAccel = 4.0f;
	static constexpr float ThiefDecel = 6.0f;
	static constexpr float PlayerRadius = 0.42f;
	static constexpr float EyeHeight = 1.62f;
	static constexpr float CrouchEyeHeight = 1.05f;

	static constexpr float ThiefSpeed = 1.30f;
	static constexpr float ThiefRadius = 0.42f;
	static constexpr int32 ThiefHealth = 100;
	static constexpr float ThiefDamage = 7.0f;
	static constexpr float ThiefMaxHp = 78.0f;
	static constexpr float ThiefSight = 26.0f;
	static constexpr float ThiefFovDeg = 200.0f;
	static constexpr float ThiefTurnRate = 4.5f;
	static constexpr float ThiefFireHunting = 1.35f;
	static constexpr int32 ThiefMag = 8;
	static constexpr float ThiefReload = 1.5f;
	static constexpr float ThiefAimTime = 0.45f;

	static constexpr float FearFlee = 4.0f;
	static constexpr float FearDecay = 0.18f;
	static constexpr float FearPerShot = 0.7f;
	static constexpr float FearPerHit = 2.0f;

	static constexpr float SearchTime = 1.5f;
	static constexpr float SearchRange = 1.2f;
	static constexpr float PoliceTime = 110.0f;

	static constexpr float ShoveRange = 1.9f;
	static constexpr float ShoveArc = 0.6f;
	static constexpr float ShoveStagger = 1.2f;
	static constexpr float ShoveFear = 2.0f;
	static constexpr float ShovePush = 0.45f;
	static constexpr float ShoveCd = 0.8f;

	static constexpr float ThiefShoveRange = 1.5f;
	static constexpr float ThiefShoveCd = 2.6f;
	static constexpr float ThiefShoveStagger = 0.9f;
	static constexpr float ThiefShovePush = 0.55f;
	static constexpr float RegrabTime = 2.5f;

	static constexpr float GrabRange = 1.4f;
	static constexpr float DropRange = 1.6f;
	static constexpr float AvoidRange = 2.2f;
	static constexpr float AvoidTurn = 0.6f;

	/** Pace kept while carrying. "Safe" is a two-person job; a laptop is one hand. */
	static float CarrySpeed(const FString& Label);
	/** Total rounds owned for the round, magazine included. */
	static int32 ReserveFor(const FString& Weapon);

	// ------------------------------------------------------------------ geometry
	/** Liang-Barsky segment vs axis-aligned rect, in XZ. */
	static bool SegRect(const FVector2D& A, const FVector2D& B, const FBox2D& R, float RRadius = 0.0f);
	static bool SegCircle(const FVector2D& A, const FVector2D& B, const FVector2D& C, float Radius);
	static bool LosBlocked(const FVector2D& A, const FVector2D& B);
	/** Distance to the first solid along a unit direction, or MaxT when clear. */
	static float RayCastPx(const FVector2D& From, const FVector2D& Dir, float MaxT = 60.0f);
	static bool CanSee(const FVector2D& A, const FVector2D& B, float MaxRange = 28.0f);
	/** The intruder's perception: sight PLUS a 200-degree field of view. */
	static bool ThiefSees(const FVector2D& ThiefPos, float ThiefYaw, const FVector2D& PlayerPos);

	static FVector2D ResolveCircle(const FVector2D& Pos, float Radius);
	static FVector2D ResolveCircle(const FVector2D& Pos, float Radius, const TArray<FBox2D>& Solids);
	static FVector2D ClampToWorld(const FVector2D& Pos, float Radius);

	/** The homeowner yields; the intruder does not. Returns [player, thief]. */
	static TArray<FVector2D> SeparateBodies(const FVector2D& PlayerPos, const FVector2D& ThiefPos);
	/** N-body separation. Iterative, because resolving one pair can overlap another. */
	static TArray<FVector2D> SeparateAll(const TArray<FVector2D>& Positions, const TArray<float>& Radii);
	static float SteerAround(float A, const FVector2D& From, const FVector2D& PlayerPos);

	// ------------------------------------------------------------------ navigation
	static FString NearestNode(const FVector2D& P);
	static TArray<FString> PathNodes(const FString& FromKey, const FString& ToKey);
	static TArray<FVector2D> RouteTo(const FString& FromKey, const FString& ToKey);

	// ------------------------------------------------------------------ world data
	static const TArray<FBox2D>& Walls();
	/** Everything collision cares about: walls plus non-flat furniture. */
	static const TArray<FBox2D>& Solids();
	static const TArray<FBox2D>& FurnitureRects();
	static const TArray<bool>& FurnitureFlat();
	static const TArray<FVector2D>& LootPositions();
	static const TArray<FString>& LootLabels();
	static const TArray<FString>& LootNodes();
	static const TArray<FString>& SearchNodes();
	/** Navigation graph, in the SAME ORDER as the Godot dictionary literal. */
	static const TArray<FString>& NodeNames();
	static const TArray<TArray<FString>>& NodeEdges();
	static const TArray<FVector2D>& NodePositions();

	/**
	 * Throws if the world data disagrees with the Godot original: seven walls, the house
	 * rectangle, the nav graph, and the loot spots. Called by the tests so a bad constant
	 * cannot pass silently.
	 */
	static void SelfCheck();
};
