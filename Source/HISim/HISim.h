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
	// ---- storey geometry, phase 2 (HOUSE-DESIGN.md §2). Heights are metres above the
	// ground floor's walking surface. The basement's 2.29 m ceiling + the 0.15 m deck
	// give 2.44; the ground floor's 2.59 m ceiling + the 0.15 m slab give 2.74.
	static constexpr float BasementZ = -2.44f;
	static constexpr float SecondZ = 2.74f;
	static constexpr float SlabThickness = 0.15f;
	/** Roof slab [5.18, 5.33]: the second floor's 2.44 m ceiling under a 0.15 m slab. */
	static constexpr float RoofBottom = 5.18f;
	static constexpr float RoofTop = 5.33f;
	/**
	 * The collision cylinder's height, in metres. A body occupies [Z, Z + BodyHeight];
	 * a solid blocks it only when that interval strictly overlaps the solid's span --
	 * strict, so a second-floor body standing AT a ground-floor wall's top (Z == MaxZ)
	 * is not caught by the wall below its feet. 1.8 matches the eye-height story
	 * (EyeHeight 1.62 sits inside it) and the headroom arithmetic in HOUSE-DESIGN.md.
	 */
	static constexpr float BodyHeight = 1.8f;

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

	/**
	 * A staircase: a plan rectangle inside which height is a function of position, not of
	 * floor. Bottom and Top are PLAN points (the feet of the bottom and top risers); the
	 * height at a point inside Rect interpolates linearly along Bottom→Top. That is the
	 * whole elevation model for stairs -- no stair entity state, no step simulation:
	 * walking into the band IS climbing it, for the player and the thieves alike.
	 *
	 * The sim owns all movement, including the player's (FHIRound::Step moves him and the
	 * pawn mirrors via SyncFromSim), so the stairs are sim machinery -- the claim made to
	 * Richard on 2026-10-15 that UE's CharacterMovement would do the human stairs for free
	 * was wrong, and HOUSE-DESIGN.md records the correction.
	 *
	 * Floors: a body inside the band takes FloorLow below the halfway point and FloorHigh
	 * at or above it, so floor state can never drift from geometry -- it is re-derived
	 * from position every step (ApplyElevation).
	 */
	struct FHIStairBand
	{
		FBox2D Rect;
		FVector2D Bottom;
		FVector2D Top;
		float BottomH = 0.0f;
		float TopH = WallHeight;
		int32 FloorLow = 0;
		int32 FloorHigh = 1;

		/** 0 at Bottom, 1 at Top, unclamped along the Bottom→Top axis. */
		float Progress(const FVector2D& P) const
		{
			const FVector2D Axis = Top - Bottom;
			const float Len2 = Axis.SizeSquared();
			return Len2 > KINDA_SMALL_NUMBER ? FVector2D::DotProduct(P - Bottom, Axis) / Len2 : 0.0f;
		}
		float HeightAt(const FVector2D& P) const
		{
			return BottomH + FMath::Clamp(Progress(P), 0.0f, 1.0f) * (TopH - BottomH);
		}
		int32 FloorAt(const FVector2D& P) const
		{
			return Progress(P) < 0.5f ? FloorLow : FloorHigh;
		}
		/**
		 * IsInsideOrOn, NOT IsInside, and the distinction cost a failed first run: UE's
		 * FBox2D::IsInside is STRICT (> and <), so a body exactly on the band's top edge --
		 * the top landing, where the flight arrives -- was not "on the stairs". Boundary
		 * points belong to the band: entering at the sill is entering.
		 */
		bool Contains(const FVector2D& P) const { return Rect.IsInsideOrOn(P); }
	};
	static const TArray<FHIStairBand>& Stairs();
	/** True when Pos lies inside any stair band's plan rectangle. */
	static bool InStairBand(const FVector2D& Pos);

	/**
	 * Height of a flat floor: the table {-1: BasementZ, 0: 0, 1: SecondZ}. Phase 1's
	 * Floor*WallHeight was the slice's two-storey version of this.
	 */
	static float FloorHeight(int32 Floor);

	/**
	 * The one elevation truth, read by the sim and the actor layer alike: inside a stair
	 * band the height interpolates along the run; on flat ground it is the floor's height;
	 * a deck whose surface IS FloorHeight(Floor) beats both (the south deck strip lies
	 * above the flight's foot inside the band's own footprint).
	 *
	 * PHASE 2: the stairwell stacks TWO bands on one plan footprint (B→G under G→2), so
	 * "inside a band" is ambiguous and the Floor argument picks between them: the band
	 * whose FloorLow equals Floor wins, else the band whose FloorHigh does.
	 */
	static float FloorZAt(const FVector2D& Pos, int32 Floor);
	/**
	 * Same, disambiguated by the body's LIVE height instead of its floor label: among the
	 * bands containing Pos, the one whose HeightAt is nearest Z claims it -- but only
	 * within BodyHeight, because a surface more than a body height away is one the body
	 * is UNDER (or over), not ON. A basement walker passing below the upper flight must
	 * not be teleported onto it, and a floor label alone cannot say which flight a
	 * mid-point body is on: past its midpoint the B→G climber carries floor 0, the same
	 * label the G→2 foot stands on. Callers that have a Z (eyes, grab tests, shoves)
	 * MUST use this overload.
	 */
	static float FloorZAt(const FVector2D& Pos, int32 Floor, float Z);

	/**
	 * Re-derive a body's Z and Floor from its position after any move. Order: a deck
	 * matching the carried floor confirms it; else the Z-nearest band within BodyHeight
	 * claims the body and its midpoint rule sets the floor; else the floor stands and Z
	 * is flat. Called by the player step, the thief step, and every shove displacement.
	 */
	static void ApplyElevation(const FVector2D& Pos, int32& Floor, float& Z);

	/**
	 * True when a WALKABLE deck (a floor slab, not a roof) whose top surface is
	 * FloorHeight(Floor) covers this plan position. A deck never CHANGES a body's floor,
	 * it only confirms the one it carries -- that is what keeps a ground-floor body under
	 * the second-floor slab from being derived up onto it.
	 */
	static bool DeckCovers(const FVector2D& Pos, int32 Floor);
	/** Front door gap in the south wall, x -1..1. Godot Vector2(-1, 1), unchanged. */
	static const FVector2D DoorGap;
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
	/**
	 * Everyone crosses a stair band at 70% pace -- the player too, and it is one rule
	 * rather than a per-body one, which keeps cross-floor chases a calculation instead of
	 * a coin flip. HOUSE-DESIGN.md §1.
	 */
	static constexpr float StairPace = 0.7f;
	/**
	 * Seconds on one waypoint before the avoidance orbit is dropped and he walks
	 * straight at it, shouldering past. The orbit trap, measured in MovingHeist mode 3
	 * on 2026-10-15: a homeowner camping a hub node left the thief circling at 1.3-1.5 m
	 * -- too far for the 1.2 m occupied-waypoint pop, too close to leave -- for 120 s at
	 * stuck=0.00, because orbiting IS moving. Lineage of the frontHub freeze fix: same
	 * disease (a waypoint that can never complete), new symptom.
	 */
	static constexpr float OrbitBreakTime = 2.5f;
	static constexpr float PlayerRadius = 0.42f;
	static constexpr float EyeHeight = 1.62f;
	static constexpr float CrouchEyeHeight = 1.05f;

	// ------------------------------------------------------------------- the view
	/** game.gd's aim scheme: body yaw plus a limited cone of free aim. */
	static constexpr float PitchMin = -0.30f;   // looking up
	static constexpr float PitchMax = 0.85f;    // looking down

	// CAM_BACK / CAM_UP ARE THE FRAMING. CamUp 0.44 was Richard's pick from a four-way rendered
	// comparison on 2026-10-13; CamBack 1.30 is the MEASURED MINIMUM that still shows the gun,
	// found by sweeping back at a fixed up (see below).
	//
	// THE REASON THEY MOVED IS THE GUN. With the camera on the defender's own line (CamSide 0,
	// below) and close to him, his own head and torso hid the pistol COMPLETELY -- `-HIProbe`
	// reported the gun visible, in front of the camera and 1.30 m away, while there was no gun on
	// screen at all. It was checked against the wrong occlusion.
	//
	// AND THE PARAMETER THAT ACTUALLY MATTERS IS **BACK**, NOT UP. Richard asked for the minimum
	// back and up that still shows the gun, expecting up to be the lever. Rendered at back 0.90
	// with up 0.22, 0.30, 0.38 and 0.44, the gun is invisible at EVERY one of them: raising the
	// camera on his own axis does not move the sight line past his chest, it pushes the whole man
	// -- and the gun with him -- down out of the bottom of the frame. Sweeping back at a fixed
	// up 0.44 is what moves the arm and pistol clear of his silhouette, and it happens between
	// 1.10 (nothing) and 1.30 (the arm and gun separate). Hence 1.30: the smallest value that
	// works in the WORST pose, which is standing, where the pistol is tucked against his chest.
	//
	// Measured at these values: the camera sits at 2.05 m. **The walls are 3.0 m** (see
	// `WallHeight`), so this is nowhere near the dollhouse view. A note elsewhere in this project
	// warned that CamUp was capped at about +0.38 m by "the 2.0 m walls" -- that number was
	// inherited from the GODOT project and is wrong here.
	static constexpr float CamBack = 1.30f;
	static constexpr float CamUp = 0.44f;

	/**
	 * ZERO, AND THIS IS A DELIBERATE DIVERGENCE FROM `game.gd`, which uses 0.42.
	 *
	 * The shoulder offset put the homeowner to one side of the screen while the reticle stayed
	 * at the middle of it, because the reticle is the AIM DIRECTION projected into the camera's
	 * basis -- so a lateral camera offset moves the MAN, not the crosshair. Reported from play
	 * 2026-10-12: *"in the middle of the screen there is a crosshair, but it is at the side of
	 * the defender; it should be in front."*
	 *
	 * The shot is cast from the camera along its forward (`ResolvePlayerShot`), so the crosshair
	 * at the centre is honest and moving the RETICLE instead would make it lie. The fix is to
	 * put the camera on the defender's own line: with no lateral offset he sits centred under
	 * the crosshair, which is where a player expects his own man to be.
	 */
	static constexpr float CamSide = 0.0f;

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

	/**
	 * Opening hold-fire: how long each intruder keeps his FIRST shot, and the stagger
	 * between the crew's first shots.
	 *
	 * A DELIBERATE DIVERGENCE FROM GODOT, decided by Richard 2026-10-15: "the first few
	 * seconds are too brutal. Make them easier." The original starts every intruder at
	 * cd 1.2 (sim.gd create_thief()), and the port had reproduced its lethality exactly
	 * -- every constant above matches Godot key for key -- which measured, from the
	 * player's chair: a homeowner who stands still is at HP 57 by t=3.0 s and dead
	 * between 8 and 12.5 s, in every run. The easing spends no new mechanics. The
	 * intruders still walk in, still see him, still take their 0.45 s aim settle; they
	 * hold the first shot until 4.0 s, and the crew's first shots leave 0.7 s apart
	 * instead of all inside the same tenth of a second. Everything after a first shot
	 * is untouched -- ThiefFireHunting governs the rest of the round exactly as before.
	 */
	static constexpr float ThiefOpeningCd = 4.0f;
	static constexpr float ThiefOpeningStagger = 0.7f;

	static constexpr float FearFlee = 4.0f;
	static constexpr float FearDecay = 0.18f;
	static constexpr float FearPerShot = 0.7f;
	static constexpr float FearPerHit = 2.0f;

	static constexpr float SearchTime = 1.5f;
	static constexpr float SearchRange = 1.2f;
	/**
	 * PROPOSED 180 (was 110), flagged for Richard -- HOUSE-DESIGN.md §4.4: nine
	 * searchable rooms across three floors cannot be cleared in a 116 s round; 180 s
	 * keeps a full clear possible but not comfortable. Veto restores 110.
	 */
	static constexpr float PoliceTime = 180.0f;

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
	 * actually passes through. Against a world whose solids all span a full storey this
	 * returns exactly what the 2-D LosBlocked returns, which is why adding it changed no
	 * behaviour.
	 *
	 * EXACT SINCE 2026-10-15 (the three-floor fork). The first form was conservative: it
	 * rejected a span only when the segment's whole Z range lay outside it, then tested
	 * the WHOLE plan segment against the footprint. With one storey that approximation
	 * never fired. With slabs it fires exactly where it must not: a homeowner at the stair
	 * foot (6.0, -11, eyes 1.62) looking up at a man on the second floor (7.1, -12, eyes
	 * 4.62) crosses slab height 2.75 at (6.41, -11.38) -- inside the stairwell hole, so
	 * the true answer is CLEAR -- but the conservative form finds his own feet inside the
	 * slab footprint and calls it blocked. Defending the stairwell is a core scenario, so
	 * the test now clips the segment to the t-interval where z(t) lies inside the span
	 * (z(t) is linear, spans are flat: a few ops) and runs SegRect on the clipped
	 * sub-segment. Exact is a strict refinement of conservative -- it only ever clears
	 * false blocks -- so every truly-blocked assertion still holds. HOUSE-DESIGN.md §1.
	 */
	static bool LosBlocked3D(const FVector& A, const FVector& B,
		const TArray<FBox2D>& Solids, const TArray<FHISolidSpan>& Spans);
	/** The same test against the real world. */
	static bool LosBlocked3D(const FVector& A, const FVector& B);
	/**
	 * The same test against WorldSolids() -- every floor's walls, slabs, railings and
	 * roofs, each at its real span. THIS is the multi-storey sight test; the two above
	 * stay on the ground-floor projection for the 2-D queries and the old assertions.
	 */
	static bool LosBlockedWorld(const FVector& A, const FVector& B);
	/** Distance to the first solid along a unit direction, or MaxT when clear. */
	static float RayCastPx(const FVector2D& From, const FVector2D& Dir, float MaxT = 60.0f);
	static bool CanSee(const FVector2D& A, const FVector2D& B, float MaxRange = 28.0f);
	/**
	 * The floor-aware sight test. A and B are FOOT positions carrying Z (FloorZAt); the
	 * test adds the eye height itself, measures range in 3-D, and blocks on the exact
	 * LosBlocked3D. This is what StepThief uses once the house has floors -- the 2-D
	 * CanSee underneath it has no spans and a man in a basement would see through the
	 * ceiling.
	 */
	static bool CanSee3D(const FVector& A, const FVector& B, float MaxRange = 28.0f);
	/**
	 * Walkability on one floor: the 2-D segment against every solid a body walking that
	 * floor would collide with (its cylinder [FloorHeight(F), +BodyHeight] strictly
	 * overlapping the span, decks excluded). What LosBlocked is to ground-floor sight,
	 * this is to per-floor walking -- used by the world-data checks; stair edges are
	 * tested on their LOW floor, which is where the flight's collisions live.
	 */
	static bool WalkBlocked(const FVector2D& A, const FVector2D& B, int32 Floor);
	/** The intruder's perception: sight PLUS a 200-degree field of view. */
	static bool ThiefSees(const FVector2D& ThiefPos, float ThiefYaw, const FVector2D& PlayerPos);
	/** The same perception with feet that carry Z and the 3-D sight underneath. */
	static bool ThiefSees3D(const FVector& ThiefPos, float ThiefYaw, const FVector& PlayerPos);
	/**
	 * Angular samples cast outward from From, one point per direction, returned in ascending
	 * angle order. Not a true angular sweep: Godot casts at every solid corner (+/-1e-4 either
	 * side of it) plus 64 even steps, and that ring is what the minimap already consumes.
	 */
	static TArray<FVector2D> VisibilityPolygon(const FVector2D& From, float MaxRange = 28.0f, int32 Floor = 0);

	/**
	 * The rectangles that occlude for a body standing on `Floor` -- the minimap's wall set.
	 * Same filter as WalkBlocked: decks are floors, not obstacles, and every other solid
	 * belongs to exactly one storey. Floor 0 callers keep using Solids() directly.
	 */
	static TArray<FBox2D> FloorSolidRects(int32 Floor);

	static FVector2D ResolveCircle(const FVector2D& Pos, float Radius);
	static FVector2D ResolveCircle(const FVector2D& Pos, float Radius, const TArray<FBox2D>& Solids);
	/**
	 * The floor-aware resolve: the same push-out of overlapping circles, against the
	 * WorldSolids() entries whose span STRICTLY overlaps the body cylinder [Z, Z +
	 * BodyHeight]. Decks are skipped (stood on, never walked into). Strictness is what
	 * lets a second-floor body stand at Z == a ground-floor wall's MaxZ without being
	 * shoved by the storey below. Every mover in the game -- player step, thief step,
	 * shoves, separation -- goes through this; the 2-D overloads remain for the tests and
	 * call sites that are ground floor by construction.
	 */
	static FVector2D ResolveCircleZ(const FVector2D& Pos, float Radius, float Z);
	static FVector2D ClampToWorld(const FVector2D& Pos, float Radius);

	/** The homeowner yields; the intruder does not. Returns [player, thief]. */
	static TArray<FVector2D> SeparateBodies(const FVector2D& PlayerPos, const FVector2D& ThiefPos);
	/** N-body separation. Iterative, because resolving one pair can overlap another. */
	static TArray<FVector2D> SeparateAll(const TArray<FVector2D>& Positions, const TArray<float>& Radii);
	/**
	 * The elevation-aware separations. Each body resolves against WorldSolids at its own
	 * Z (so a second-floor man is not shoved by the ground-floor fridge under his feet),
	 * and any pair with a body INSIDE A STAIR BAND is left exactly where it is: a 1 m
	 * flight is single file, and pushing sideways on a stair is pushing someone through a
	 * stringer or over a railing. Callers re-run ApplyElevation after -- a push can move
	 * a body along a band, and its Z must follow.
	 */
	static TArray<FVector2D> SeparateBodiesZ(const FVector2D& PlayerPos, float PlayerZ,
		const FVector2D& ThiefPos, float ThiefZ);
	static TArray<FVector2D> SeparateAllZ(const TArray<FVector2D>& Positions,
		const TArray<float>& Radii, const TArray<float>& Zs);
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
		/** The storey the item sits on. The grab test needs it: a thief on the second
		 *  floor standing above the ground-floor TV is 0 m away in plan and must NOT be
		 *  able to take it through the deck. */
		int32 Floor = 0;
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
		/**
		 * Which storey the body is on (0 ground, +1 up, -1 down). Plan position alone
		 * cannot say -- the same (x,y) is basement, ground and second floor where they
		 * stack -- so Floor is state and Z is derived from it (ApplyElevation). It only
		 * ever changes on a stair band.
		 */
		int32 Floor = 0;
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
		/** The storey: see FHIPlayer::Floor. Changes only on a stair band. */
		int32 Floor = 0;
		/** yaw PI faces -Z, i.e. back towards the house from the van. */
		float Yaw = 3.14159265358979f;
		float Hp = float(ThiefHealth);
		bool bAlive = true;
		int32 Mag = ThiefMag;
		float Cd = ThiefOpeningCd;
		float Reloading = 0.0f;
		float Think = 2.0f;
		FString Carry;
		FString Target;
		TArray<FVector2D> Route;
		FString Mode = TEXT("hunt");
		float StuckT = 0.0f;
		/** Fixed-window net-progress check (2026-10-18): where the window started, how
		 * long it has run, and the step length intended over it. See StuckStep -- the
		 * per-frame rule cannot see a mutual-push stall whose per-frame net travel
		 * stays just above its threshold. */
		FVector2D StuckAnchor = FVector2D::ZeroVector;
		float StuckAnchorT = 0.0f;
		float StuckWant = 0.0f;
		/** Corner-rounding memory for the wall slide's head-on deadzone
		 * (2026-10-18): the side chosen to round along the contact face, +/-1 in the
		 * frame Perp = (-PushN.Y, PushN.X); 0 = no live contact. Chosen ONCE per
		 * commitment and kept -- per-frame re-choosing from the waypoint dithers
		 * across the waypoint's isocline, and so does clearing on free frames: a
		 * rounded corner produces one push-less frame, and the tangential branch
		 * then walks him back up the face (both measured, run 17/18). The commitment
		 * ends at the waypoint change or after CornerSideT > 3 s (staleness bound,
		 * ~3.9 m of stepping -- longer than any face in the house). See the deadzone
		 * branch of the wall slide in StepIntruders. */
		int32 CornerSide = 0;
		/** Seconds the current corner-rounding commitment has been live. */
		float CornerSideT = 0.0f;
		/** The committed rounding direction, FROZEN at pick time (run 19): the
		 * per-frame Perp recomputed from the live push normal rotates toward the face
		 * inside a corner's push-out, and a 0.5 m probe along it then hits the
		 * body-radius-inflated end of the very face being rounded -- flipping the
		 * commitment at every end, a full-pace 2.8 m shuttle with stuck=0.00 that no
		 * detector owns. The frozen vector points along the face the commitment was
		 * made on and rounds the end cleanly; the probes re-aim only the flip. */
		FVector2D CornerDir = FVector2D::ZeroVector;
		/** Seconds spent on the CURRENT waypoint (Route[0]); resets when it changes. */
		float WpT = 0.0f;
		FVector2D LastWp = FVector2D::ZeroVector;
		/** Prepend hysteresis (2026-10-19, SoloProbe S/S2): the waypoint most recently
	 * popped, and how long ago. RouteFromClear's prepend guard skips prepending a From
	 * node the walker JUST popped. Measured: 10 of the 33 flat nav edges are
	 * body-radius-blocked at the door jambs (doors are 0.9-1.2 m, the body 0.84 m, and
	 * the node-to-node line passes inside the inflated jamb), so traversal depends on
	 * the corner slide finishing. The flip cycle killed it: blocked leg -> guard
	 * prepends the From node -> walker pops it up to 0.6 m short -> re-aim is still
	 * jamb-blocked -> guard prepends the same node again. Every flip changes the first
	 * waypoint, and a waypoint change clears the corner-rounding commitment, so the
	 * slide never reached the 0.16-0.36 m effective gap. Two rounds measured it: the
	 * living doorway (S: 170 s of route=1<->2 oscillation in a 0.4 m band east of the
	 * wall, stuck=0.00 the whole time) and the br2 door (S2: 112+ s of route=3<->4,
	 * Laptop never reached). The same class drove run 24's B2 stall-1 at this door. */
		FVector2D LastPoppedWp = FVector2D::ZeroVector;
		float LastPoppedAgo = 1e9f;
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

		/**
		 * Scan the pending events for a way to LOSE and set the state. Public, and it had to
		 * become public on 2026-10-14, because the actor layer now runs it at THE END OF EVERY
		 * FRAME as well as Step running it at the end of every step: Step only runs when the
		 * accumulator is ready -- at 2.7-3.0x realtime most frames run no step at all -- while
		 * the actor layer's event drain runs every frame. A `playerDown` queued by a killing
		 * blow resolved during the drain-and-process phase was therefore eaten by the next
		 * no-step frame's drain before any rule scan could see it, and the round played on over
		 * the homeowner's corpse (measured in two of three manual runs; the third decided only
		 * because its next frame happened to step). Godot's original cannot have the race:
		 * game.gd decides `state = "lose"` while consuming the event, in the actor layer. The
		 * idempotence promise below is what makes the extra call safe.
		 */
		void ApplyRoundRules();

	private:
		/** Spend a round, start the cooldown, and tell every intruder he heard it. */
		void FireWeapon();
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

	/**
	 * One frame of the stuck detector: accumulate or reset `StuckT` from how far the body
	 * ACTUALLY travelled this frame, and fire the re-route when the stall passes 1.2 s.
	 *
	 * `NetMoved` must be measured against the frame-start position AFTER body resolution.
	 * `StepThief` owns that measurement when it separates in-step; `StepIntruders` owns it
	 * when it separates the group afterwards -- and before this was split out (2026-10-18)
	 * the group path had no way to see a jam at all: its thieves measured a full walking
	 * step every frame because the push-back that undid it ran after the detector.
	 */
	static void StuckStep(FHIThief& Thief, float NetMoved, float Dt,
		const TArray<FHILootItem>& Loot, TArray<FHIEvent>& Events);

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
	/**
	 * The floor value that means "do not filter by floor" in NearestNode. AS-BUILT
	 * 2026-10-18: this used to be INDEX_NONE, but INDEX_NONE is -1 -- the basement's
	 * real floor number -- so every floor -1 query silently ran UNFILTERED and the
	 * stacked stair feet answered "stairFootG" for a body standing in the basement at
	 * (-0.70,-11.35). A basement thief rerouted from a ground node walks his route into
	 * the basement walls and reroutes again for ever: the NodeFloors pin caught it, and
	 * the heist tests never searched baseOpen/utility. INT32_MIN is no floor.
	 */
	static constexpr int32 AnyFloor = TNumericLimits<int32>::Min();
	/**
	 * The graph node nearest P ON THE GIVEN FLOOR. Floors matter: the stacked stair nodes
	 * share one plan position (a straight run under a straight run), so an unfiltered
	 * Voronoi nearest would hand a ground-floor thief the second-floor node and route him
	 * through a slab. The old single-argument form stays as floor 0 for the ground-only
	 * call sites and tests; AnyFloor is the explicit unfiltered query.
	 */
	static FString NearestNode(const FVector2D& P);
	static FString NearestNode(const FVector2D& P, int32 Floor);
	/**
	 * The FROM vertex for thief routing: the nearest node on `Floor` whose straight line
	 * from P is WalkBlocked-clear. AS-BUILT 2026-10-18, measured freeze: a searcher at
	 * (0.27,-14.10) -- the push-out equilibrium on the stairwell screen's east face --
	 * got NearestNode == stairHeadB, 1.09 m away THROUGH the full-height screen, and
	 * because RouteTo drops the From node (Godot parity, pinned) the route became the
	 * single leg [master]: a due-west bearing, perpendicular into the face pinning him,
	 * tangent under the wall-slide deadzone, reroute loop forever. NearestNode keeps its
	 * distance-only contract for every other caller; this form is the walkable answer to
	 * the same question. `Floor` must be concrete (AnyFloor degrades to NearestNode);
	 * when NOTHING is clear (a body pinned in a corner) it falls back to NearestNode so
	 * routing degrades to the old behaviour instead of producing no route at all.
	 */
	static FString NearestClearNode(const FVector2D& P, int32 Floor);
	static TArray<FString> PathNodes(const FString& FromKey, const FString& ToKey);
	static TArray<FVector2D> RouteTo(const FString& FromKey, const FString& ToKey);

	// ------------------------------------------------------------------ world data
	/** A wall row: its plan rectangle and the storey it stands on. */
	struct FHIWallRow
	{
		FBox2D Rect;
		int32 Floor = 0;
		// 0 = the storey default: FloorHeight(Floor + 1) when there is a floor above
		// (the wall meets the slab that sits on it), else RoofTop for the top storey.
		// An override means "lower than the storey" -- the slice's stringer recorded a
		// 3 cm miss here with the freeze it caused; phase 2's stairwell screen carries
		// one too (railing height above the second floor, not the roof).
		float TopZ = 0.0f;
		// 0 = stands on its floor (MinZ is FloorHeight(Floor)). An override raises the
		// sill: the row starts higher than the floor it belongs to. Phase 2 has exactly
		// one -- the transom wall over the front door (2.10 m up), which closes the
		// letterbox the Godot original's 2-D door split left open to the eaves. Rows
		// with MinZ > 0 are EXCLUDED from Walls(), so the legacy 2-D projection keeps
		// its walkable door gap exactly as before.
		float MinZ = 0.0f;
	};
	static const TArray<FHIWallRow>& WallRows();
	static const TArray<FBox2D>& Walls();
	/**
	 * Everything collision cares about: walls plus non-flat furniture, GROUND FLOOR ONLY.
	 * The projection keeps its pre-floors meaning exactly -- the 2-D queries that serve
	 * the minimap and the legacy tests see the ground floor they were written against.
	 * Floor-aware work goes through WorldSolids().
	 */
	static const TArray<FBox2D>& Solids();

	/**
	 * The master solid table: every wall, furniture block, slab, railing and roof deck on
	 * every floor, each with the span that makes it real in height.
	 *
	 * `bDeck` marks structure you stand ON -- floor slabs, roof decks. Decks never take
	 * part in collision (a second-floor body standing at Z == slab MaxZ walks on it, it
	 * does not walk into it) but they do block sight across storeys, which is the entire
	 * reason slabs are solids at all. Railings are NOT decks: they collide with bodies
	 * whose cylinder overlaps their span (second-floor walkers) and are invisible to
	 * ground-floor sight and walking (span reject) -- no special cases anywhere.
	 *
	 * `Floor` is for the queries and the builder that group by storey (minimap, actor
	 * instancing); collision itself never reads it -- the span-vs-cylinder overlap test
	 * subsumes it, stringers mid-flight included.
	 */
	struct FHISolid
	{
		FBox2D Rect;
		FHISolidSpan Span;
		int32 Floor = 0;
		bool bDeck = false;
	};
	static const TArray<FHISolid>& WorldSolids();

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
	/**
	 * The floor each furniture row stands on, index-aligned with `FurnitureRects`.
	 * Phase 2: a row's base is `FloorHeight(FurnitureFloors()[Idx])`; the builder draws
	 * it there and the sight model spans it to its storey's ceiling.
	 */
	static const TArray<int32>& FurnitureFloors();
	/**
	 * Each item's height `h`, index-aligned with `FurnitureRects`.
	 *
	 * Ported from sim.gd's own literal, where every row carries one, and used by `game.gd`
	 * exactly the same way: `_add_box(body, f["rect"], f["h"])`.
	 *
	 * The first port of this table DROPPED the field, and the consequence was that the game
	 * layer had no height to draw with, reached for `SolidSpans` instead -- which is uniform
	 * at a full storey because it models SIGHT -- and drew every couch, table and bed as a
	 * floor-to-ceiling column. Nothing in the sim reads this value, and nothing about
	 * navigation, line of sight or collision changes with it.
	 */
	static const TArray<float>& FurnitureHeights();
	/**
	 * The sim's own `kind` for each furniture row, index-aligned with `FurnitureRects`.
	 *
	 * Descriptive only -- nothing in the sim reads it. It exists so a failure can name the
	 * item it is talking about, and so the game layer can key anything per-kind without
	 * guessing at a rectangle's identity from its coordinates.
	 */
	static const TArray<FString>& FurnitureLabels();
	static const TArray<FVector2D>& LootPositions();
	static const TArray<FString>& LootLabels();
	static const TArray<FString>& LootNodes();
	/** The storey each loot item sits on, index-aligned with LootPositions(). */
	static const TArray<int32>& LootFloors();
	static const TArray<FString>& SearchNodes();
	/** Navigation graph, phase 2: the full three-floor house (HOUSE-DESIGN.md §4.7). */
	static const TArray<FString>& NodeNames();
	static const TArray<TArray<FString>>& NodeEdges();
	static const TArray<FVector2D>& NodePositions();
	/**
	 * The storey each node sits on, index-aligned with NodeNames(). Stair nodes sit ON
	 * their flights and carry the floor you step off at. An edge whose endpoints differ
	 * in floor is a STAIR EDGE by definition -- no separate flag table that could
	 * disagree with the node data.
	 */
	static const TArray<int32>& NodeFloors();
	/** True when the graph edge A—B is a stair edge (floor differs between endpoints). */
	static bool IsStairEdge(const FString& A, const FString& B);

	/**
	 * Throws if the world data disagrees with itself: the table counts, the nav graph's
	 * symmetry/reachability/walkability, stair-band arrivals and headroom, node and spawn
	 * standability, and furniture discipline. Called by the tests so a bad constant
	 * cannot pass silently. Phase 2 (v3): every hand-derived clearance in the house
	 * schedule is pinned here.
	 */
	static void SelfCheck();
};
