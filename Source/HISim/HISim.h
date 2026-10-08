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

	/**
	 * One entry of sim.gd's WEAPONS dictionary. `Mag` is the magazine CAPACITY, not the
	 * rounds currently in it -- the current count lives on the entity (see FHIPlayer).
	 */
	struct FHIWeapon
	{
		FString Name;
		float Damage = 0.0f;
		float Cd = 0.0f;
		int32 Mag = 0;
		float Reload = 0.0f;
		int32 Pellets = 1;
		float Spread = 0.0f;
	};
	/** sim.gd WEAPONS: {"pistol": {...}, "shotgun": {...}}. An unknown key is a no-weapon. */
	static FHIWeapon Weapon(const FString& Key);
	/** Magazine CAPACITY for a weapon key -- WEAPONS[key]["mag"], against which reloads fill. */
	static int32 WeaponMag(const FString& Key);

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

	// ------------------------------------------------------------------ entities
	//
	// In Godot both of these are untyped `Dictionary`s, with a different key set for each
	// and no check that a key exists. `create_thief()` omits keys the code later reads
	// (`speed_scale`, `escaped` via .get) and `shove()` writes two keys (`aim`, `fear`)
	// that `create_thief()` does set but `create_player()` does not. C++ cannot model
	// "absent key with a default", so every key either constructor touches is a declared
	// member with its Godot default, and the `.get(key, default)` call sites become plain
	// member reads. Members are named after the Godot keys in PascalCase so a diff against
	// sim.gd stays readable.

	/**
	 * sim.gd's `loot` entries: {"pos", "label", "node", "taken", "delivered", "known"}.
	 * Held by value in an array, exactly as Godot passes it around, so a function that
	 * mutates it must take the array by reference.
	 */
	struct FHILootItem
	{
		FVector2D Pos = FVector2D::ZeroVector;
		FString Label;
		FString Node;
		bool bTaken = false;
		bool bDelivered = false;
		/** False until he has stood in the room it is in: he does not know the layout. */
		bool bKnown = false;
	};

	/** sim.gd create_player(), key for key. */
	struct FHIPlayer
	{
		FVector2D Pos = FVector2D(0.0f, -10.6f);
		float Yaw = 0.0f;
		float Pitch = 0.24f;
		float Hp = ThiefMaxHp;
		bool bAlive = true;
		FString Weapon = TEXT("pistol");
		bool bCrouching = false;
		bool bSprinting = false;
		FVector2D Vel = FVector2D::ZeroVector;
		/** Rounds in the magazine RIGHT NOW. Kept in step with Mags[Weapon]. */
		int32 Mag = 12;
		/** Rounds in the magazine of each weapon he is not currently holding. Godot's `mags`. */
		TMap<FString, int32> Mags;
		float ShoveCd = 0.0f;
		float Stagger = 0.0f;
		/** Rounds left in the bag, per weapon. Godot's `reserve`. A COPY of RESERVE. */
		TMap<FString, int32> Reserve;
		/** The firearm's own cooldown between shots, in seconds. */
		float Cd = 0.0f;
		/** Seconds left on a reload; 0 means not reloading. */
		float Reloading = 0.0f;
	};

	/** sim.gd create_thief(), key for key. */
	struct FHIThief
	{
		FVector2D Pos = Drop + FVector2D(0.0f, 0.5f);
		FVector2D Prev = Drop + FVector2D(0.0f, 0.5f);
		/** yaw PI faces -Z, i.e. back towards the house from the van. */
		float Yaw = 3.14159265358979f;
		float Hp = float(ThiefHealth);
		bool bAlive = true;
		int32 Mag = ThiefMag;
		float Cd = 1.2f;
		float Reloading = 0.0f;
		float Think = 2.0f;
		FString Carry;
		FString Target;
		TArray<FVector2D> Route;
		FString Mode = TEXT("hunt");
		float StuckT = 0.0f;
		float Aim = 0.0f;
		float Stagger = 0.0f;
		int32 Hits = 0;
		float Fear = 0.0f;
		bool bEscaped = false;
		FVector2D LastSeen = FVector2D::ZeroVector;
		float Speed = 0.0f;
		TArray<FString> Searched;
		/** -1 means nobody has called the police yet. */
		float Alarm = -1.0f;
		bool bSirens = false;
		float Regrab = 0.0f;
		float ShoveCd = 0.0f;
		/** Body identity for event attribution. Godot's `id`. */
		int32 Id = 0;
		/** Distance he is TRYING to cover this frame; the stuck detector compares against it. */
		float Step = 0.0f;
		/**
		 * Not in `create_thief()`: the movement code writes `speed_scale` every frame from
		 * CARRY_SPEED, and Godot reads it back with `.get("speed_scale", 1.0)`. Declared
		 * with the default that read falls back to.
		 */
		float SpeedScale = 1.0f;
	};

	/** A drained-per-frame event. Godot raises `{"type": ...}` dictionaries. */
	struct FHIEvent
	{
		FString Type;
		/** Attribution; INDEX_NONE until step_intruders stamps it. */
		int32 Id = INDEX_NONE;
		/** `label` for grabbed/dropped/delivered, `carried` for escaped. */
		FString Label;
		/** Shove/alert payloads: spread, dmg, dropped. */
		float Spread = 0.0f;
		float Damage = 0.0f;
	};

	static FHIPlayer CreatePlayer();
	/**
	 * sim.gd create_thief(), key for key. FHIThief's member initialisers already carry
	 * every default from that literal, so this exists so callers name what they are
	 * building and so the defaults have one place to change. Note it does NOT stagger
	 * the spawn point -- that is create_intruders()'s job, for when there is more than one.
	 */
	static FHIThief CreateThief();
	/** The three loot spots, all unknown, none taken, none delivered. */
	static TArray<FHILootItem> CreateLoot();
	/** Delivered is what empties the round, so this counts what is NOT delivered. */
	static int32 Remaining(const TArray<FHILootItem>& Loot);

	/**
	 * Load the current weapon's magazine out of the reserve. True if anything moved.
	 *
	 * In the sim rather than the game layer so it can be tested headlessly: the reserve is
	 * a gameplay rule, and the game layer is not reachable from the test harness.
	 */
	static bool LoadMagazine(FHIPlayer& Player);
	static bool CanLoadMagazine(const FHIPlayer& Player);

	// ------------------------------------------------------------------ thief AI
	/**
	 * One frame of the intruder. Returns true when he currently has line of sight to the
	 * homeowner.
	 *
	 * `separate` is false only when called from step_intruders, which does the pairwise
	 * separation itself -- running the per-body version three times would let each thief
	 * shove the player in turn. Default true keeps the single-intruder path unchanged.
	 *
	 * NOT YET PORTED, and each is a deliberate hole rather than an oversight:
	 * `thief_shove` (the arm's-length grab), the police-clock event payload for the
	 * game layer, and the shove/regrab interaction that depends on it. Every branch that
	 * runs without them is ported exactly.
	 */
	static bool StepThief(FHIThief& Thief, FHIPlayer& Player, TArray<FHILootItem>& Loot,
		TArray<FHIEvent>& Events, float Dt, bool bSeparate = true);

	/** Move `Current` toward `Target` by at most `MaxDelta`. Godot's move_toward(). */
	static float MoveToward(float Current, float Target, float MaxDelta);

	// ------------------------------------------------------------------ pathing
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
