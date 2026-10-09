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

	/**
	 * A solid's vertical extent, in metres above the ground floor.
	 *
	 * The sim was XZ-only, so a wall blocked sight at EVERY height and no floor could
	 * separate two storeys. The span is what makes both expressible. Declared here rather
	 * than beside Solids() because the geometry group needs it and comes first.
	 */
	struct FHISolidSpan
	{
		float MinZ = 0.0f;
		/**
		 * A full storey by default, which is exactly the pre-elevation behaviour: every
		 * solid blocked sight at any eye height.
		 */
		float MaxZ = WallHeight;
	};
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

	// ------------------------------------------------------------------- the view
	/** game.gd's aim scheme: body yaw plus a limited cone of free aim. */
	static constexpr float PitchMin = -0.30f;   // looking up
	static constexpr float PitchMax = 0.85f;    // looking down
	static constexpr float CamBack = 0.90f;
	static constexpr float CamUp = 0.22f;
	static constexpr float CamSide = 0.42f;

	/**
	 * Where the player is looking, as a unit vector in the port's world space: X and Y are
	 * the sim's own plane (FVector2D.X and FVector2D.Y) and Z is height.
	 *
	 * Godot's vector is (x, y_up, z) -- its `y` is up. This maps that y_up to Z and its `z`
	 * to Y, so the sim's 2-D plane stays exactly the XY plane it always was. Unit length
	 * already: cos^2(ap)*(sin^2+cos^2)(ay) + sin^2(ap) = 1.
	 */
	static FVector AimDir(float Yaw, float Pitch);

	/**
	 * The chase camera's position relative to a pivot at the player's eyes.
	 *
	 * Godot computes `pivot - view * back + UP * up + right * side`, with right being
	 * (-cos yaw, sin yaw) in the plane. The camera sits at -view, so at yaw 0 -- facing +Y
	 * -- it is at -Y, which is behind him rather than in front.
	 */
	static FVector CameraOffset(float Yaw, float Pitch,
		float Back = CamBack, float Up = CamUp, float Side = CamSide);

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

	/** A shout's reach and its effect. Loud enough to carry a house, weaker than a shot. */
	static constexpr float ShoutLoudness = 1.4f;
	static constexpr float ShoutCd = 3.0f;

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

	/**
	 * Height-aware segment test: blocked only by a solid whose VERTICAL span the segment
	 * also reaches. Against a world whose solids all span a full storey this returns
	 * exactly what the 2-D LosBlocked returns, which is why adding it changed no behaviour.
	 *
	 * The vertical test is conservative: a span is rejected only when the segment's whole Z
	 * range lies outside it, so a ray that climbs above a low wall is clear while one that
	 * merely passes over one of its corners is not. Documented rather than hidden, because
	 * the approximation is visible to anyone who leans over a low obstacle.
	 */
	static bool LosBlocked3D(const FVector& A, const FVector& B,
		const TArray<FBox2D>& Solids, const TArray<FHISolidSpan>& Spans);
	/** The same test against the real world. */
	static bool LosBlocked3D(const FVector& A, const FVector& B);
	/** Distance to the first solid along a unit direction, or MaxT when clear. */
	static float RayCastPx(const FVector2D& From, const FVector2D& Dir, float MaxT = 60.0f);
	static bool CanSee(const FVector2D& A, const FVector2D& B, float MaxRange = 28.0f);
	/** The intruder's perception: sight PLUS a 200-degree field of view. */
	static bool ThiefSees(const FVector2D& ThiefPos, float ThiefYaw, const FVector2D& PlayerPos);
	/**
	 * Angular samples cast outward from From, one point per direction, returned in ascending
	 * angle order. Not a true angular sweep: Godot casts at every solid corner (+/-1e-4 either
	 * side of it) plus 64 even steps, and that ring is what the minimap already consumes.
	 */
	static TArray<FVector2D> VisibilityPolygon(const FVector2D& From, float MaxRange = 28.0f);

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
		/**
		 * The third axis, in metres above the ground floor. Pos stays XZ -- FVector2D.X is
		 * world X and .Y is world Z -- so this is additive and every existing assertion
		 * still means exactly what it meant. A ground-floor round leaves it at 0.
		 */
		float Z = 0.0f;
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
		/** Seconds before he can shout again. The shout had no cooldown and could be spammed. */
		float ShoutCd = 0.0f;
	};

	/** sim.gd create_thief(), key for key. */
	struct FHIThief
	{
		FVector2D Pos = Drop + FVector2D(0.0f, 0.5f);
		FVector2D Prev = Drop + FVector2D(0.0f, 0.5f);
		/** The third axis: see FHIPlayer::Z. A body spawned in the drive is at 0. */
		float Z = 0.0f;
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
		/**
		 * How many traces a `playerShot` wants. Carried on the event so the actor layer
		 * resolves the shot without having to look the weapon up again and risk disagreeing
		 * with the round about which one was fired.
		 */
		int32 Pellets = 1;
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

	// ------------------------------------------------------------- multiple bodies
	/**
	 * `Count` intruders (at least one), each with its own id, fanned out along the
	 * driveway so they do not all start inside one another.
	 *
	 * create_thief() puts a single body exactly on the DROP point, which was fine when
	 * there could only ever be one; two bodies at the same coordinate are the one case
	 * separate_all() has no direction to resolve.
	 */
	static TArray<FHIThief> CreateIntruders(int32 Count);

	/**
	 * Step every intruder for one frame, then resolve ALL the bodies against each other.
	 *
	 * The per-body separation inside StepThief is deliberately skipped here (bSeparate
	 * false): it knows about the homeowner and one intruder only, so running it three
	 * times would let each thief shove the player in turn. The pairwise pass at the end is
	 * the general version.
	 *
	 * Stamps the emitting body's id onto every per-body event, because with three of them
	 * an unattributable tracer or gunshot is not a playable game -- and the producer is the
	 * only place the identity is known.
	 *
	 * @returns the indices of the intruders that currently have line of sight to the player.
	 */
	static TArray<int32> StepIntruders(TArray<FHIThief>& Intruders, FHIPlayer& Player,
		TArray<FHILootItem>& Loot, TArray<FHIEvent>& Events, float Dt);

	// -------------------------------------------------------------------- the round
	/** A man changing a magazine is not walking at full pace. game.gd RELOAD_PACE. */
	static constexpr float ReloadPace = 0.62f;

	/** Godot's Vector2.move_toward: step `Current` toward `Target` by at most MaxDelta. */
	static FVector2D MoveToward(const FVector2D& Current, const FVector2D& Target, float MaxDelta);

	/** One frame of player intent, in the sim's XZ convention. */
	struct FHIInput
	{
		/** Strafe (X) and forward (Y), each -1..1. +Y is forward. */
		FVector2D Axis = FVector2D::ZeroVector;
		bool bCrouch = false;
		bool bSprint = false;
		/** Trigger held. The weapon's cooldown and the magazine still gate the shot. */
		bool bFire = false;
		/**
		 * One frame of [R]. Starts a reload only if one CAN start, so holding the key does
		 * not restart the timer every frame and freeze him mid-magazine forever.
		 */
		bool bReload = false;
		/** One frame of [F]. Swings at the nearest man in front; the cooldown gates it. */
		bool bShove = false;
		/**
		 * One frame of the shout key. FIX-LIST ITEM 17, "more non-lethal verbs".
		 *
		 * A shout frightens every man who can hear it and tells him where you are, without
		 * spending a round and -- deliberately -- without starting the police clock. It is the
		 * cheap deterrent: it costs you your position, which is exactly the trade a homeowner
		 * makes when he yells at someone in his hall.
		 */
		bool bShout = false;
		/** One frame of the call-police key. Starts the clock without a gunshot. */
		bool bCallPolice = false;
		/**
		 * One frame of the weapon-select keys: "pistol", "shotgun", or EMPTY for no change.
		 *
		 * A string rather than an enum, and deliberately: sim.gd keys its weapons by name and
		 * the magazine and reserve maps are keyed the same way, so an enum would need a
		 * second mapping table -- and a second place for the two to disagree.
		 */
		FString SwitchWeapon;
	};

	enum class EHIRoundState : uint8
	{
		Play,
		Won,
		Lost,
	};

	/**
	 * The round: the homeowner, the intruders, the loot, and the rules that end it.
	 *
	 * Ported from game.gd, which could never be tested because it is a SceneTree node with
	 * rendering attached to it. Everything here is logic, so it can be -- and it is the
	 * layer the Unreal actors will mirror rather than own.
	 *
	 * DELIBERATELY NOT HERE: hit resolution. Casting pellets at a body needs Unreal's
	 * collision, so the actor layer does the trace and calls DamageThief with the result.
	 * Everything the trace cannot decide -- ammunition, cooldown, recoil bookkeeping, and
	 * who heard the shot -- is here, where it can be asserted.
	 */
	struct FHIRound
	{
		FHIPlayer Player;
		TArray<FHIThief> Intruders;
		TArray<FHILootItem> Loot;
		/**
		 * Events raised since the last ConsumeEvents(). Step does NOT clear it, so damage
		 * applied from outside -- which is how the actor layer resolves a hit, because a
		 * pellet trace needs Unreal's collision -- is seen by the round rules rather than
		 * discarded. Call ConsumeEvents() every frame or it grows without bound.
		 */
		TArray<FHIEvent> Events;
		EHIRoundState State = EHIRoundState::Play;
		int32 ThievesDown = 0;

		static FHIRound Start(int32 IntruderCount);

		/** Move the homeowner, step every intruder, then apply the round rules. */
		void Step(const FHIInput& Input, float Dt);

		/**
		 * The actor layer's hit resolution routes here. Applies the damage AND its lethal
		 * consequences immediately -- the loot he was carrying drops where he fell, he is
		 * counted, and the round is won if he was the last one standing -- so nothing
		 * depends on a thiefDown event surviving to the next frame.
		 *
		 * @returns true when the damage was lethal.
		 */
		bool DamageIntruder(int32 Id, float Damage);

		/** Hand back this frame's events and start a fresh list. Call once per frame. */
		TArray<FHIEvent> ConsumeEvents();

		bool AnyIntruderStanding() const;
		FHIThief* FindIntruder(int32 Id);
		int32 Remaining() const { return FHISim::Remaining(Loot); }

		/**
		 * The living intruder nearest the homeowner, or nullptr when none is standing.
		 *
		 * Empty-handed nearest, exactly as game.gd's `_nearest_intruder()` picks: it does NOT
		 * test the shove arc. The arc is Shove's own rule, and duplicating it here would give
		 * the two copies a place to drift apart.
		 */
		FHIThief* NearestIntruder();

		/**
		 * Swap the held weapon. Each weapon keeps its OWN magazine.
		 *
		 * game.gd learned this the hard way: handing over a full magazine on every switch made
		 * tapping 1 / 2 a free reload, so ammunition stopped being a resource. The held
		 * magazine is banked under the old weapon and the new one's is restored, and any
		 * reload in progress is CANCELED rather than carried across.
		 *
		 * An unknown or already-held key is a no-op.
		 */
		void SwitchWeapon(const FString& Key);

		/**
		 * Swing at the nearest man in front.
		 *
		 * Returns true if it landed. Lives on the round rather than in the caller so the
		 * target choice and the arc rule are reachable from a headless test -- which is the
		 * whole reason the shove is in the sim at all.
		 */
		bool TryShove();

		/**
		 * Shout at the house. True if anyone heard.
		 *
		 * Raises every standing intruder's fear and marks the homeowner's position for them,
		 * WITHOUT starting the police clock and without a shot. Enough shouts and he breaks
		 * off and runs, which is the only way to end an intrusion without killing anyone --
		 * and killing is the alternative the fix list wanted a non-lethal answer to.
		 */
		bool Shout();

		/**
		 * Call the police. True the first time, false after.
		 *
		 * The clock already starts on the first gunshot, so this exists for the player who
		 * would rather not fire at all. Idempotent, because the police do not arrive sooner
		 * for being told twice.
		 */
		bool CallPolice();

		/** True once the police have been called by any means. */
		bool bPoliceCalled = false;

	private:
		/** Spend a round, start the cooldown, and tell every intruder he heard it. */
		void FireWeapon();
		void ApplyRoundRules();
	};

	/**
	 * Load the current weapon's magazine out of the reserve. True if anything moved.
	 *
	 * In the sim rather than the game layer so it can be tested headlessly: the reserve is
	 * a gameplay rule, and the game layer is not reachable from the test harness.
	 */
	static bool LoadMagazine(FHIPlayer& Player);
	static bool CanLoadMagazine(const FHIPlayer& Player);

	// ------------------------------------------------------------------ the shove
	/**
	 * Drive him back and make him drop what he is carrying. True if it landed.
	 *
	 * Range, an arc in front of the homeowner, and a cooldown -- which is why it lives in
	 * the sim rather than the game layer: the harness has to be able to reach it.
	 */
	static bool Shove(FHIPlayer& Player, FHIThief& Thief, TArray<FHILootItem>& Loot,
		TArray<FHIEvent>& Events);
	/**
	 * Put whatever he is carrying back into the world at his feet and make him stoop for
	 * it. Raises a `dropped` event carrying the label, or nothing at all when his hands
	 * were empty.
	 */
	static void DropCarried(FHIThief& Thief, TArray<FHILootItem>& Loot, TArray<FHIEvent>& Events);
	/**
	 * The intruder shoves the homeowner. True if it landed.
	 *
	 * He had no answer at contact range -- he would simply stand and fire, so a man close
	 * enough to grab you was LESS dangerous than one across the room. It knocks the
	 * homeowner back, spoils his aim and stops him firing for a moment. He will not do it
	 * while carrying anything: his hands are full, which is also when he is harmless.
	 */
	static bool ThiefShove(FHIThief& Thief, FHIPlayer& Player, TArray<FHIEvent>& Events);

	// ------------------------------------------------------------- damage and fear
	/**
	 * Tell him he is being shot at. Called when the homeowner fires; `Loudness` scales with
	 * how close and how loud, so a shotgun at 3 m is not a pistol across the house. The
	 * first shot of the round also starts the police clock.
	 */
	static void AlertThief(FHIThief& Thief, const FVector2D& From, float Loudness = 1.0f);

	/**
	 * The fear-and-position half of AlertThief, WITHOUT the police clock.
	 *
	 * Split out for the shout, which must frighten him without summoning the police: a
	 * homeowner yelling at a burglar is not a 999 call, and having the shout start the clock
	 * would have made the two verbs the same verb.
	 */
	static void Frighten(FHIThief& Thief, const FVector2D& From, float Loudness = 1.0f);
	/** Apply damage to the intruder. True when it was lethal; emits thiefDown or thiefHit. */
	static bool DamageThief(FHIThief& Thief, float Damage, TArray<FHIEvent>& Events);
	/** Apply damage to the homeowner. True when it was lethal; emits playerDown or playerHit. */
	static bool DamagePlayer(FHIPlayer& Player, float Damage, TArray<FHIEvent>& Events);

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

	/**
	 * The vertical extent of each entry in Solids(), in the same order. The FHISolidSpan
	 * type itself is declared up with the world constants, because the geometry group needs
	 * it and that comes first.
	 *
	 * DELIBERATELY UNIFORM, and this is a modelling decision worth stating plainly. The
	 * Godot source carries no furniture heights -- only a `flat` flag -- so inventing them
	 * here would silently change which obstacles the AI can see over, and there is no test
	 * that could tell me whether the invented numbers are right. Giving a sofa its real
	 * height is a DESIGN decision, not a porting one, so every solid spans the full storey
	 * for now and the original 19 tests hold unchanged.
	 *
	 * What this does buy, without inventing anything: a FLOOR SLAB is new structure rather
	 * than a re-description of old, so it can carry its own span and block sight between
	 * storeys. That is the part of fix-list item 18 that blocks a multi-level house.
	 */
	static const TArray<FHISolidSpan>& SolidSpans();
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
