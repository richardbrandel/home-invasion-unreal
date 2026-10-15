// Automation tests for the ported simulation.
//
// These are ports of the assertions in test/test_sim.gd, and they exist for one reason:
// a port that passes the same tests as the original is correct by construction, whereas a
// port judged by eye is a guess. Run headlessly with:
//
//   ~/tools/ue-run-tests.sh <uproject> HI.Sim
//
// Named HI.* rather than HITest.* so the wrapper's filter can select them precisely.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "HISim.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr float PI_F = 3.14159265358979f;
}

// --------------------------------------------------------------------- world data

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimWorldData,
	"HI.Sim.WorldData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimWorldData::RunTest(const FString& Parameters)
{
	// Fails loudly if the world data disagrees with itself or with the slice spec
	// (HOUSE-DESIGN.md §3). This block used to pin Godot's literals; the 2026-10-15
	// fork moved the pins to the slice's own numbers, and SelfCheck v2 now COMPUTES the
	// interesting invariants (graph symmetry, reachability, stair headroom) instead of
	// counting rows.
	FHISim::SelfCheck();

	TestEqual(TEXT("wall count"), FHISim::Walls().Num(), 8);  // Godot's 7 + the stair stringer
	TestEqual(TEXT("nav node count"), FHISim::NodeNames().Num(), 14);
	TestEqual(TEXT("loot count"), FHISim::LootPositions().Num(), 3);

	// "each loot spot is reachable from its nav node" -- straight line from the node to
	// the item must not cross a solid.
	for (int32 Idx = 0; Idx < FHISim::LootPositions().Num(); ++Idx)
	{
		const FString NodeKey = FHISim::LootNodes()[Idx];
		const int32 NodeIdx = FHISim::NodeNames().IndexOfByKey(NodeKey);
		TestTrue(FString::Printf(TEXT("loot '%s' has a real node '%s'"),
			*FHISim::LootLabels()[Idx], *NodeKey), NodeIdx != INDEX_NONE);
		if (NodeIdx == INDEX_NONE)
		{
			continue;
		}
		const FVector2D NodePos = FHISim::NodePositions()[NodeIdx];
		TestFalse(FString::Printf(TEXT("loot '%s' reachable from '%s'"),
			*FHISim::LootLabels()[Idx], *NodeKey),
			FHISim::WalkBlocked(NodePos, FHISim::LootPositions()[Idx],
				FHISim::NodeFloors()[NodeIdx]));
	}

	// "every nav edge is a clear straight line" -- the graph is only usable if its edges
	// run through open floor.
	for (int32 Idx = 0; Idx < FHISim::NodeNames().Num(); ++Idx)
	{
		const FString& From = FHISim::NodeNames()[Idx];
		const FVector2D A = FHISim::NodePositions()[Idx];
		for (const FString& To : FHISim::NodeEdges()[Idx])
		{
			const int32 Jdx = FHISim::NodeNames().IndexOfByKey(To);
			TestTrue(FString::Printf(TEXT("edge %s -> %s names a real node"), *From, *To),
				Jdx != INDEX_NONE);
			if (Jdx == INDEX_NONE)
			{
				continue;
			}
			// Floor-aware, tested on the edge's LOW floor. A stair edge's collisions
			// live at climbing height -- the stringer below, nothing at the treads --
			// and the top half of a flight legitimately crosses a railing FOOTPRINT at
			// a height where the body is below the railing's span. The 2026-10-15 run
			// measured exactly that: the old 2-D assertion failed three stair edges
			// that are physically clear.
			const int32 EdgeFloor = FMath::Min(
				FHISim::NodeFloors()[Idx], FHISim::NodeFloors()[Jdx]);
			TestFalse(FString::Printf(TEXT("nav edge %s -> %s crosses a solid on floor %d"),
				*From, *To, EdgeFloor),
				FHISim::WalkBlocked(A, FHISim::NodePositions()[Jdx], EdgeFloor));
		}
	}

	// "the door gap is open" -- the only way in or out.
	TestFalse(TEXT("the door gap is open"),
		FHISim::LosBlocked(FVector2D(0.0f, 2.5f), FVector2D(0.0f, -4.5f)));

	// "LOS blocked through a wall, clear inside"
	TestTrue(TEXT("LOS blocked through a wall"),
		FHISim::LosBlocked(FVector2D(0.0f, -19.0f), FVector2D(0.0f, -25.0f)));
	TestFalse(TEXT("LOS clear inside the house"),
		FHISim::LosBlocked(FVector2D(0.0f, -8.0f), FVector2D(0.0f, -12.0f)));

	return true;
}

// ----------------------------------------------------------------------- geometry

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimGeometry,
	"HI.Sim.Geometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimGeometry::RunTest(const FString& Parameters)
{
	// ---- collision pushes an entity out (test_sim.gd "collision pushes an entity out")
	const FVector2D Pushed = FHISim::ResolveCircle(FVector2D(0.0f, -19.0f), 0.42f);
	TestTrue(TEXT("resolve_circle moved the body out of the north wall"),
		!Pushed.Equals(FVector2D(0.0f, -19.0f), 0.001f));

	// ---- rayCast stops at a wall, and travels full range over open ground
	// spec: sim.gd `ray_cast_px(Vector2(0, -18.5), Vector2(0, -1), 60.0) < 2.0` -- the
	// direction is -Y, toward the north wall at z=-19, 0.5 m away.
	TestTrue(TEXT("rayCast stops at a wall"),
		FHISim::RayCastPx(FVector2D(0.0f, -18.5f), FVector2D(0.0f, -1.0f), 60.0f) < 2.0f);
	TestTrue(TEXT("rayCast travels full range over open ground"),
		FMath::IsNearlyEqual(
			FHISim::RayCastPx(FVector2D(0.0f, 10.0f), FVector2D(0.0f, 1.0f), 60.0f), 60.0f, 0.01f));

	// ---- can_see: false through a wall, true across open floor, and range-clamped
	TestFalse(TEXT("canSee false through a wall"),
		FHISim::CanSee(FVector2D(0.0f, -18.0f), FVector2D(0.0f, -23.0f)));
	// spec: sim.gd `can_see(Vector2(0, -8), Vector2(0, -13))` -- through the door gap at
	// x=0, which is the only opening in the south wall (DoorGap spans x in [-1, 1]).
	TestTrue(TEXT("canSee true across open floor"),
		FHISim::CanSee(FVector2D(0.0f, -8.0f), FVector2D(0.0f, -13.0f)));
	TestFalse(TEXT("canSee respects the sight radius"),
		FHISim::CanSee(FVector2D(0.0f, 20.0f), FVector2D(0.0f, 20.0f - 40.0f), 28.0f));

	// ---- THE REQUIREMENT: hidden behind a wall, visible through the doorway
	TestFalse(TEXT("intruder hidden behind a wall"),
		FHISim::CanSee(FVector2D(-5.0f, -11.0f), FVector2D(5.0f, -11.0f), 28.0f));
	TestTrue(TEXT("visible through the doorway"),
		FHISim::CanSee(FVector2D(0.0f, -13.0f), FVector2D(0.0f, -15.0f), 28.0f));

	// ---- the intruder's field of view: he cannot see directly behind himself
	// Facing +Y (yaw PI/2 in this convention), a target 2 m behind must be invisible even
	// with clear line of sight. This is ThiefSees, deliberately separate from CanSee.
	const FVector2D Facing(0.0f, 1.0f);
	const float YawForward = FMath::Atan2(Facing.Y, Facing.X);
	const FVector2D Behind = FVector2D(0.0f, -14.0f);   // 2 m "south" of the eye
	const FVector2D Eye = FVector2D(0.0f, -12.0f);
	TestFalse(TEXT("thief does NOT see a player directly behind him"),
		FHISim::ThiefSees(Eye, YawForward, Behind));
	TestTrue(TEXT("thief DOES see a player in front of him"),
		FHISim::ThiefSees(Eye, YawForward, Eye + Facing * 3.0f));
	// and the FOV is 200 degrees, so a target well off to the side is still seen
	const FVector2D Side = Eye + FVector2D(FMath::Cos(YawForward + PI_F * 0.45f),
		FMath::Sin(YawForward + PI_F * 0.45f)) * 3.0f;
	TestTrue(TEXT("thief sees within the 200-degree cone"),
		FHISim::ThiefSees(Eye, YawForward, Side));

	// ---- visibility polygon: every vertex in range, none inside a solid.
	// spec: test_sim.gd banner "visibility + minimap fog of war",
	// "visibility polygon stays in range and out of solids".
	{
		const FVector2D PolyEye(0.0f, -18.0f);
		const float PolyRange = 28.0f;
		const TArray<FVector2D> Poly = FHISim::VisibilityPolygon(PolyEye, PolyRange);

		float MaxD = 0.0f;
		FString BadPoint;
		for (const FVector2D& P : Poly)
		{
			MaxD = FMath::Max(MaxD, FVector2D::Distance(P, PolyEye));
			for (const FBox2D& S : FHISim::Solids())
			{
				// Strictly inside, matching the spec's 1e-6 insets: a vertex resting on an
				// edge is correct and must not be reported as a failure.
				if (P.X > S.Min.X + 1e-6f && P.X < S.Max.X - 1e-6f
					&& P.Y > S.Min.Y + 1e-6f && P.Y < S.Max.Y - 1e-6f)
				{
					BadPoint = FString::Printf(TEXT("vertex inside a solid: (%.3f, %.3f)"), P.X, P.Y);
				}
			}
		}
		TestTrue(FString::Printf(TEXT("visibility polygon stays in range and out of solids (%d points, max reach %.1f m)"),
			Poly.Num(), MaxD),
			Poly.Num() > 20 && BadPoint.IsEmpty() && MaxD <= PolyRange + 0.01f);
	}

	// ---- polygon cost. The spec budgets 4 ms per polygon; the port is expected to be far
	// cheaper, so a failure here means the ray loop regressed, not that the box is slow.
	{
		const double Start = FPlatformTime::Seconds();
		constexpr int32 Samples = 200;
		for (int32 Idx = 0; Idx < Samples; ++Idx)
		{
			// static_cast: FMath::Sin is overloaded for float and double, so an int32
			// argument is ambiguous rather than implicitly converted.
			FHISim::VisibilityPolygon(FVector2D(FMath::Sin(static_cast<float>(Idx)) * 3.0f, -11.5f), 30.0f);
		}
		const double MsPerPolygon = (FPlatformTime::Seconds() - Start) * 1000.0 / Samples;
		TestTrue(FString::Printf(TEXT("visibility polygon is cheap enough for every frame (%.3f ms per polygon)"),
			MsPerPolygon), MsPerPolygon < 4.0);
	}

	return true;
}

// -------------------------------------------------------------------- separation

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimSeparation,
	"HI.Sim.Separation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimSeparation::RunTest(const FString& Parameters)
{
	// "close bodies are pushed apart"
	const TArray<FVector2D> Near = FHISim::SeparateBodies(FVector2D(0.0f, 0.0f), FVector2D(0.2f, 0.1f));
	TestTrue(TEXT("close bodies are pushed apart"),
		FVector2D::Distance(Near[0], Near[1]) >= FHISim::PlayerRadius + FHISim::ThiefRadius - 0.01f);

	// "coincident bodies resolve deterministically"
	const TArray<FVector2D> Co = FHISim::SeparateBodies(FVector2D(3.0f, 3.0f), FVector2D(3.0f, 3.0f));
	TestTrue(TEXT("coincident bodies separate"),
		FVector2D::Distance(Co[0], Co[1]) >= FHISim::PlayerRadius + FHISim::ThiefRadius - 0.01f);

	// "bodies already apart are untouched"
	const TArray<FVector2D> Apart = FHISim::SeparateBodies(FVector2D(0.0f, 0.0f), FVector2D(0.0f, 5.0f));
	TestTrue(TEXT("apart bodies are untouched"),
		Apart[0].Equals(FVector2D(0.0f, 0.0f), 0.001f)
		&& Apart[1].Equals(FVector2D(0.0f, 5.0f), 0.001f));

	// GODOT test_sim.gd:125-127, ported 2026-10-15 (PARITY item 14): the DIRECTION of
	// yield is the documented choice that caused 142 re-routes when it was wrong. The
	// distance assertions above would all pass with the two bodies swapping roles.
	const TArray<FVector2D> Yield = FHISim::SeparateBodies(FVector2D(0.0f, 0.0f), FVector2D(0.0f, 0.3f));
	TestTrue(FString::Printf(TEXT("the homeowner is the body that yields (player ended at y=%.3f)"), Yield[0].Y),
		Yield[0].Y < 0.0f);

	// THE CONTRACT, over many forced contacts: no two of N bodies ever overlap, and nobody
	// leaves the world. This is what separate_all exists for and what the Godot suite
	// measures over 900 frames.
	{
		const float MinGap = FHISim::PlayerRadius + FHISim::ThiefRadius;
		TArray<FVector2D> Pos;
		TArray<float> Radii;
		Pos.Add(FVector2D(0.0f, 5.0f));  Radii.Add(FHISim::PlayerRadius);
		Pos.Add(FVector2D(0.1f, 5.0f));  Radii.Add(FHISim::ThiefRadius);
		Pos.Add(FVector2D(0.2f, 5.0f));  Radii.Add(FHISim::ThiefRadius);

		float Worst = TNumericLimits<float>::Max();
		bool bOutOfBounds = false;
		for (int32 Frame = 0; Frame < 900; ++Frame)
		{
			// drive the player round a tight circle so contact is continuous
			const float Ang = Frame * 0.11f;
			Pos[0] = FVector2D(FMath::Cos(Ang) * 1.2f, -11.0f + FMath::Sin(Ang) * 1.2f);
			Pos = FHISim::SeparateAll(Pos, Radii);
			for (int32 Idx = 0; Idx < Pos.Num(); ++Idx)
			{
				for (int32 Jdx = Idx + 1; Jdx < Pos.Num(); ++Jdx)
				{
					Worst = FMath::Min(Worst, FVector2D::Distance(Pos[Idx], Pos[Jdx]));
				}
				if (Pos[Idx].X < FHISim::Bounds.Min.X - 0.01f || Pos[Idx].X > FHISim::Bounds.Max.X + 0.01f
					|| Pos[Idx].Y < FHISim::Bounds.Min.Y - 0.01f || Pos[Idx].Y > FHISim::Bounds.Max.Y + 0.01f)
				{
					bOutOfBounds = true;
				}
			}
		}
		TestTrue(FString::Printf(TEXT("no two bodies ever overlap over 900 forced-contact frames (closest %.4f, want >= %.2f)"),
			Worst, MinGap), Worst >= MinGap - 0.02f);
		TestFalse(TEXT("nobody is pushed outside the world bounds"), bOutOfBounds);
	}

	// Steer-away: at AVOID_TURN = 0 the heist stalls, so this must actually change the
	// heading when the player is close and in front, and must NOT when he is behind.
	{
		const FVector2D From(0.0f, 0.0f);
		const float Ahead = FMath::Atan2(1.0f, 0.0f);      // facing +Y
		const float Steered = FHISim::SteerAround(Ahead, From, FVector2D(0.5f, 1.0f));
		TestNotEqual(TEXT("steer_around turns away from a player in front"), Steered, Ahead);
		// test_sim.gd:131-132, ported 2026-10-15 (PARITY item 14): Godot asserts the
		// MAGNITUDE, and the port asserted only "not equal" -- AVOID_TURN could be 0.001
		// and that passes. A check that cannot fail is not a check.
		TestTrue(FString::Printf(TEXT("and it turns by more than 0.1 rad (%.3f against %.3f)"), Steered, Ahead),
			FMath::Abs(Steered - Ahead) > 0.1f);

		// test_sim.gd:137-140, ported 2026-10-15 (PARITY item 14): the swerve must turn
		// AWAY from the homeowner, not into him. Godot's own inputs, unchanged: heading
		// north (-PI/2), homeowner ahead and to one side, heading must come out > north.
		const float North = -UE_PI * 0.5f;
		const float Swerve = FHISim::SteerAround(North, FVector2D(0.0f, 0.0f), FVector2D(-0.4f, -1.5f));
		TestTrue(FString::Printf(TEXT("the swerve turns away from the homeowner, not into him (%.3f > %.3f)"),
			Swerve, North), Swerve > North);
		const float DeadAhead = FHISim::SteerAround(North, FVector2D(0.0f, 0.0f), FVector2D(0.0f, -1.5f));
		TestTrue(FString::Printf(TEXT("and a homeowner dead ahead also moves the heading by more than 0.1 rad (%.3f)"),
			DeadAhead), FMath::Abs(DeadAhead - North) > 0.1f);

		const float BehindAhead = FHISim::SteerAround(Ahead, From, FVector2D(0.0f, -1.0f));
		TestEqual(TEXT("steer_around leaves a heading alone when the player is behind"),
			BehindAhead, Ahead);

		const float FarAhead = FHISim::SteerAround(Ahead, From, FVector2D(0.0f, 10.0f));
		TestEqual(TEXT("steer_around leaves a heading alone beyond AVOID_RANGE"),
			FarAhead, Ahead);
	}

	return true;
}

// -------------------------------------------------------------------- navigation

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimNavigation,
	"HI.Sim.Navigation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimNavigation::RunTest(const FString& Parameters)
{
	// The route from the van to a loot room must exist, start at the van and end at the
	// room, and be a contiguous walk of the graph.
	for (const FString& Room : FHISim::SearchNodes())
	{
		const TArray<FVector2D> Route = FHISim::RouteTo(TEXT("van"), Room);
		TestTrue(FString::Printf(TEXT("a route exists from van to %s"), *Room), Route.Num() > 0);
		if (Route.Num() == 0)
		{
			continue;
		}
		const int32 RoomIdx = FHISim::NodeNames().IndexOfByKey(Room);
		// spec: test_sim.gd asserts only `route.size() >= 3`. The earlier assertion here --
		// that Route[0] is the van's own position -- encoded a port bug: Godot EXCLUDES the
		// origin from the path (`while k != from_key`), so the first waypoint is the van's
		// NEIGHBOUR. That difference is not cosmetic; it is what lets a fleeing intruder's
		// route to the van actually empty so he can escape.
		TestTrue(FString::Printf(TEXT("pathfinding returns a connected route to %s (%d hops)"), *Room, Route.Num()),
			Route.Num() >= 3);
		TestTrue(FString::Printf(TEXT("route to %s ends at the room"), *Room),
			Route.Last().Equals(FHISim::NodePositions()[RoomIdx], 0.001f));

		// and every consecutive pair must be a real edge, not a teleport
		for (int32 K = 0; K + 1 < Route.Num(); ++K)
		{
			int32 A = INDEX_NONE, B = INDEX_NONE;
			for (int32 N = 0; N < FHISim::NodePositions().Num(); ++N)
			{
				if (FHISim::NodePositions()[N].Equals(Route[K], 0.001f))     { A = N; }
				if (FHISim::NodePositions()[N].Equals(Route[K + 1], 0.001f)) { B = N; }
			}
			TestTrue(FString::Printf(TEXT("route step %d -> %d is a graph edge"), K, K + 1),
				A != INDEX_NONE && B != INDEX_NONE
				&& FHISim::NodeEdges()[A].Contains(FHISim::NodeNames()[B]));
		}
	}

	// Nearest-node must snap a point in the living room to the living-room node, and the
	// front hall to frontHub. A mis-snapped start poisons every route from it.
	TestEqual(TEXT("a point in the living room snaps to the living node"),
		FHISim::NearestNode(FVector2D(-3.6f, -11.0f)), FString(TEXT("living")));
	TestEqual(TEXT("a point in the front hall snaps to frontHub"),
		FHISim::NearestNode(FVector2D(0.0f, -11.0f)), FString(TEXT("frontHub")));
	TestEqual(TEXT("a point out at the van snaps to the van node"),
		FHISim::NearestNode(FVector2D(0.0f, 4.8f)), FString(TEXT("van")));

	// A degenerate route is not a crash: asking for nowhere must come back empty.
	TestEqual(TEXT("a route to a node that does not exist is empty"),
		FHISim::RouteTo(TEXT("van"), TEXT("nowhere")).Num(), 0);

	return true;
}

// ---------------------------------------------------------------------- entities

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimEntities,
	"HI.Sim.Entities",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimEntities::RunTest(const FString& Parameters)
{
	// Ported from the state the "ammunition" banner sets up with (test_sim.gd 392-431):
	// the player's starting magazine and reserve, and the two weapons' magazines being
	// separate records rather than one shared number.
	//
	// The Godot assertions this pass deliberately does NOT carry are the ones that need
	// the not-yet-ported banners: `Sim.shove` / `Sim.thief_shove` (the shove and the
	// intruder's hands), `Sim.damage_thief` / `Sim.damage_player` (damage),
	// `alert_thief` (gunfire drives him off), and `create_intruders` / `step_intruders`
	// (multiple intruders).

	// ---- ammunition
	FHISim::FHIPlayer Player = FHISim::CreatePlayer();
	// "he starts with a part-full magazine and a reserve": mag 12, reserve 36. `12` is
	// asserted literally in test_sim.gd; the reserve is checked against the table.
	TestEqual(TEXT("the player starts with a part-full magazine"), Player.Mag, 12);
	TestEqual(TEXT("and a reserve"), Player.Reserve[TEXT("pistol")], 36);
	TestEqual(TEXT("the reserve constant is the table's, not the test's"),
		Player.Reserve[TEXT("pistol")], FHISim::ReserveFor(TEXT("pistol")));
	// Godot: `player["mags"] = {"pistol": 12, "shotgun": 6}`.
	TestEqual(TEXT("each weapon starts with its own magazine"), Player.Mags[TEXT("pistol")], 12);
	TestEqual(TEXT("the shotgun's magazine is its own"), Player.Mags[TEXT("shotgun")], 6);

	// "a reload comes out of the reserve and nowhere else"
	Player.Mag = 0;
	const bool bMoved = FHISim::LoadMagazine(Player);
	TestTrue(TEXT("a reload comes out of the reserve and nowhere else"), bMoved);
	TestEqual(TEXT("and the magazine is full again"), Player.Mag, 12);
	TestEqual(TEXT("and the reserve is smaller by exactly what was loaded"),
		Player.Reserve[TEXT("pistol")], 24);

	// "a partial reload takes only the shortfall"
	Player.Mag = 9;
	FHISim::LoadMagazine(Player);
	TestEqual(TEXT("a partial reload fills the magazine"), Player.Mag, 12);
	TestEqual(TEXT("and takes only the shortfall"), Player.Reserve[TEXT("pistol")], 21);

	// "the last rounds in the reserve still load"
	Player.Reserve.Add(TEXT("pistol"), 2);
	Player.Mag = 0;
	FHISim::LoadMagazine(Player);
	TestEqual(TEXT("the last rounds in the reserve still load"), Player.Mag, 2);

	// "but an empty reserve loads nothing at all"
	Player.Mag = 0;
	const bool bEmptyReload = FHISim::LoadMagazine(Player);
	TestFalse(TEXT("but an empty reserve loads nothing at all"), bEmptyReload);
	TestEqual(TEXT("and the magazine stays empty"), Player.Mag, 0);
	TestFalse(TEXT("and he is correctly told he cannot reload"),
		FHISim::CanLoadMagazine(Player));

	// "the two weapons do not share a magazine". test_sim.gd performs the weapon switch
	// by hand (mag = 3 -> weapon = shotgun -> mag = mags["shotgun"]), so the switch itself
	// is NOT ported here: it asserts the magazine records survive it. The shotgun's
	// capacity is taken from the WEAPONS table rather than hard-coded, which is the
	// property test_sim.gd's literal `6` is checking.
	Player = FHISim::CreatePlayer();
	Player.Mag = 3;
	Player.Mags.Add(TEXT("pistol"), 3);
	Player.Weapon = TEXT("shotgun");
	Player.Mag = Player.Mags[TEXT("shotgun")];
	TestEqual(TEXT("the two weapons do not share a magazine"),
		Player.Mag, FHISim::WeaponMag(TEXT("shotgun")));
	TestEqual(TEXT("and the pistol's magazine is remembered as it was"),
		Player.Mags[TEXT("pistol")], 3);
	TestTrue(TEXT("the two capacities genuinely differ, so that check can fail"),
		FHISim::WeaponMag(TEXT("pistol")) != FHISim::WeaponMag(TEXT("shotgun")));

	return true;
}

// ---------------------------------------------------------------------- momentum

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimMomentum,
	"HI.Sim.Momentum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimMomentum::RunTest(const FString& Parameters)
{
	// Ported from the "momentum" banner (test_sim.gd 358-388): acceleration builds speed
	// and coasting decays it rather than stopping dead. This is the first test that drives
	// step_thief, so it is also what proves the tick function is wired up at all.
	//
	// Two of the banner's assertions are NOT ported: neither is about momentum, and both
	// need banners this pass excludes.
	//   * `speed_scale` is CARRY_SPEED[carry], which is only observable while carrying,
	//     and the carry/deliver leg it depends on lives in the theft behaviour.
	//   * the thief's arm's-length grab (`thief_shove`), the police clock and damage are
	//     all excluded by the task's scope.

	const float Dt = 1.0f / 60.0f;
	FHISim::FHIPlayer Player = FHISim::CreatePlayer();
	FHISim::FHIThief Thief = FHISim::CreateThief();
	TArray<FHISim::FHILootItem> Loot = FHISim::CreateLoot();
	TArray<FHISim::FHIEvent> Events;

	Thief.Pos = FVector2D(0.0f, 2.0f);
	Thief.Prev = Thief.Pos;
	// Past the threshold hesitation, or he simply stands there for the first two seconds.
	Thief.Think = 0.0f;

	TArray<float> Speeds;
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		FHISim::StepThief(Thief, Player, Loot, Events, Dt);
		Speeds.Add(Thief.Speed);
	}

	TestTrue(FString::Printf(TEXT("he starts from a standstill rather than at full pace (first frame %.3f m/s)"),
		Speeds[0]), Speeds[0] < 0.2f);
	TestTrue(FString::Printf(TEXT("and builds speed instead of snapping to it (%.3f -> %.3f m/s over 10 frames, full pace %.2f)"),
		Speeds[0], Speeds[9], FHISim::ThiefSpeed),
		Speeds[9] > Speeds[0] && Speeds[9] < FHISim::ThiefSpeed);

	// Now stop him mid-stride and watch him coast to a halt. `halted > 0` is the whole
	// assertion: a body that stopped dead would read 0 on the first frame.
	//
	// He is stopped by DWELL, not by taking his route away. Emptying the route used to be
	// enough -- but only because of the frontHub freeze: the search branch never rebuilt an
	// emptied route, so the "coast" was really the freeze with a speed on it. Since the
	// 2026-10-15 self-heal an empty route is refilled on the next step (that is the point of
	// the fix), so this gates the walk the honest way: a thief listening at a threshold does
	// not move, and his speed decays while he listens.
	const float Peak = Thief.Speed;
	Thief.Think = 5.0f;
	int32 Halted = -1;
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		FHISim::StepThief(Thief, Player, Loot, Events, Dt);
		if (Thief.Speed <= 0.0f)
		{
			Halted = Frame;
			break;
		}
	}
	TestTrue(FString::Printf(TEXT("and coasts to a halt rather than stopping dead (halted after %d frames from %.2f m/s)"),
		Halted, Peak), Halted > 0);

	return true;
}

// -------------------------------------------------------------------- thief AI

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimThiefAI,
	"HI.Sim.ThiefAI",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimThiefAI::RunTest(const FString& Parameters)
{
	// spec: test_sim.gd banner "thief AI" -- a full heist against an idle homeowner.
	//
	// The first test that runs the AI to completion rather than for a handful of frames:
	// it only passes if StepThief can find all three valuables, carry each one back to the
	// van, and finish, within the spec's 180 simulated seconds.
	FHISim::FHIPlayer Player = FHISim::CreatePlayer();
	// CONTROLLED position, not the round's spawn. This is a pathfinding-completion check (one
	// man, three valuables, 180 s), calibrated with the homeowner in the front zone. The spawn
	// moved into the back zone beside the Safe on 2026-10-15, and a SINGLE idle homeowner there
	// measured 1/3 delivered in 180 s with only 1 re-route -- he is not lost, he gets tied up
	// with the idle body parked on the very loot he is coming for and never wins a clean
	// GRAB_RANGE. That is an artifact of ONE idle mannequin, not of the spawn: the same
	// back-zone spawn clears 3/3 in 72.8 s with the three-intruder crew (HI.Sim.Round), and a
	// live homeowner does not stand still to be pinned. Stand her on the old front-zone spot so
	// this measures the pathfinding it means to.
	Player.Pos = FVector2D(0.0f, -10.6f);
	FHISim::FHIThief Thief = FHISim::CreateThief();
	TArray<FHISim::FHILootItem> Loot = FHISim::CreateLoot();
	TArray<FHISim::FHIEvent> Events;

	const float Dt = 1.0f / 60.0f;
	float T = 0.0f;
	int32 Reroutes = 0;

	while (T < 180.0f && FHISim::Remaining(Loot) > 0)
	{
		FHISim::StepThief(Thief, Player, Loot, Events, Dt);
		for (const FHISim::FHIEvent& Ev : Events)
		{
			if (Ev.Type == TEXT("reroute"))
			{
				++Reroutes;
			}
		}
		// The spec drains the array every frame, so a reroute is counted once per frame
		// rather than accumulating for the whole run.
		Events.Reset();
		T += Dt;
	}

	const int32 Left = FHISim::Remaining(Loot);
	// The spec prints this too, and it is the only record of HOW the heist went: TestEqual
	// says nothing when it passes, so a slow-but-still-correct run would otherwise leave no
	// trace. Godot's original clears three items in 137 s with 0 re-routes; a large drift
	// here is the early warning that a movement constant has gone wrong.
	AddInfo(FString::Printf(TEXT("cleared %d/3 items in %.1fs, %d re-routes"),
		3 - Left, T, Reroutes));
	TestEqual(FString::Printf(TEXT("thief completes a full heist against an idle player (delivered %d/3 in %.1fs)"),
		3 - Left, T), Left, 0);
	// He reroutes when he genuinely has to; a body that re-plans every frame is thrashing
	// rather than walking, which the spec bounds at 2 re-routes for a whole heist.
	TestTrue(FString::Printf(TEXT("thief does not thrash while heisting (%d re-routes)"), Reroutes),
		Reroutes <= 2);

	return true;
}

// ---------------------------------------------------------------------- the shove

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimShove,
	"HI.Sim.Shove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimShove::RunTest(const FString& Parameters)
{
	// spec: test_sim.gd banner "the shove".
	const float Dt = 1.0f / 60.0f;
	FHISim::FHIPlayer Player = FHISim::CreatePlayer();
	FHISim::FHIThief Thief = FHISim::CreateThief();
	TArray<FHISim::FHILootItem> Loot = FHISim::CreateLoot();
	TArray<FHISim::FHIEvent> Events;

	Player.Pos = FVector2D(0.0f, -11.0f);
	Player.Yaw = 0.0f;                        // yaw 0 faces +Z
	Thief.Pos = FVector2D(0.0f, -9.0f);       // 2.0 m in front, out of reach
	TestFalse(FString::Printf(TEXT("out of reach a shove does nothing (2.0 m away, reach is %.1f m)"),
		FHISim::ShoveRange), FHISim::Shove(Player, Thief, Loot, Events));

	// in range but behind you
	Player.Yaw = UE_PI;
	Thief.Pos = FVector2D(0.0f, -9.6f);
	Events.Reset();
	TestFalse(TEXT("and you cannot shove a man behind your own back (facing away, 1.4 m)"),
		FHISim::Shove(Player, Thief, Loot, Events));

	// properly in front, and in reach
	Player.Yaw = 0.0f;
	const float HpBefore = Thief.Hp;
	Events.Reset();
	const bool bLanded = FHISim::Shove(Player, Thief, Loot, Events);
	const float PushedTo = Thief.Pos.Y;
	TestTrue(TEXT("in reach and in front it lands"), bLanded);
	TestTrue(FString::Printf(TEXT("it staggers him hard (stagger=%.2f s)"), Thief.Stagger),
		Thief.Stagger >= FHISim::ShoveStagger);
	TestTrue(FString::Printf(TEXT("it wounds him not at all (hp %.1f -> %.1f)"), HpBefore, Thief.Hp),
		Thief.Hp == HpBefore);
	TestTrue(FString::Printf(TEXT("and it drives him back (from -9.60 m to %.2f m)"), PushedTo),
		PushedTo > -9.6f);
	Events.Reset();
	TestFalse(FString::Printf(TEXT("but you cannot do it twice in a row (cooldown is %.1f s)"),
		FHISim::ShoveCd), FHISim::Shove(Player, Thief, Loot, Events));

	// And it is the only way to take something back off him without killing him.
	Thief = FHISim::CreateThief();
	Loot = FHISim::CreateLoot();
	Thief.Pos = FVector2D(0.0f, -9.6f);
	Thief.Carry = TEXT("Laptop");
	Loot[1].bTaken = true;                    // index 1 is the Laptop, per LootLabels()
	Player.ShoveCd = 0.0f;
	Events.Reset();
	const bool bGot = FHISim::Shove(Player, Thief, Loot, Events);
	TestTrue(FString::Printf(TEXT("shoving a laden intruder makes him drop it (carry now '%s')"), *Thief.Carry),
		bGot && Thief.Carry.IsEmpty());
	TestTrue(FString::Printf(TEXT("and the valuable is back in the world where he stood (taken=%d delivered=%d)"),
		Loot[1].bTaken ? 1 : 0, Loot[1].bDelivered ? 1 : 0),
		!Loot[1].bTaken && !Loot[1].bDelivered);
	// PARITY item 14, 2026-10-15: Godot's message says "where he stood" but its assertion
	// checks only the flags, and so did this one. The POSITION is the part that can silently
	// break -- loot that "returns to the world" at the origin or at the drop point would
	// pass both suites and read as a teleport in play.
	TestTrue(FString::Printf(TEXT("and it landed at his feet, not elsewhere (loot (%.2f,%.2f) vs thief (%.2f,%.2f))"),
		Loot[1].Pos.X, Loot[1].Pos.Y, Thief.Pos.X, Thief.Pos.Y),
		Loot[1].Pos.Equals(Thief.Pos, 0.001f));

	// And he must not simply snatch it straight back off the floor.
	Thief.Think = 0.0f;
	Thief.Stagger = 0.0f;
	Events.Reset();
	for (int32 N = 0; N < 30; ++N)
	{
		FHISim::StepThief(Thief, Player, Loot, Events, Dt);
	}
	TestTrue(FString::Printf(TEXT("and he cannot pick it straight back up (carry='%s' half a second later)"), *Thief.Carry),
		Thief.Carry.IsEmpty());

	// but he does come back for it once he has recovered
	bool bBack = false;
	for (int32 N = 0; N < static_cast<int32>(20.0f / Dt); ++N)
	{
		// The spec pins stagger to zero every frame here, so the regrab delay is the only
		// thing holding him off rather than the stagger itself.
		Thief.Stagger = 0.0f;
		FHISim::StepThief(Thief, Player, Loot, Events, Dt);
		if (Thief.Carry == TEXT("Laptop"))
		{
			bBack = true;
			break;
		}
	}
	TestTrue(FString::Printf(TEXT("though he does come back for it (carrying '%s' again)"), *Thief.Carry), bBack);

	return true;
}

// -------------------------------------------------------- the intruder's hands

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimHands,
	"HI.Sim.Hands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimHands::RunTest(const FString& Parameters)
{
	// spec: test_sim.gd banner "the intruder's hands" -- he can put his hands on YOU.
	FHISim::FHIPlayer Player = FHISim::CreatePlayer();
	FHISim::FHIThief Thief = FHISim::CreateThief();
	TArray<FHISim::FHIEvent> Events;

	Player.Pos = FVector2D(0.0f, -11.0f);
	Thief.Pos = FVector2D(0.0f, -8.0f);       // 3 m away
	TestFalse(FString::Printf(TEXT("at three metres he does not lay a hand on you (reach is %.1f m)"),
		FHISim::ThiefShoveRange), FHISim::ThiefShove(Thief, Player, Events));

	Thief.Pos = FVector2D(0.0f, -9.9f);       // 1.1 m
	const float PPosBefore = Player.Pos.Y;
	Events.Reset();
	const bool bGrabbed = FHISim::ThiefShove(Thief, Player, Events);
	const float KnockedTo = Player.Pos.Y;
	TestTrue(TEXT("inside reach he does"), bGrabbed);
	TestTrue(FString::Printf(TEXT("it spoils your aim (stagger=%.2f s)"), Player.Stagger),
		Player.Stagger >= FHISim::ThiefShoveStagger);
	TestTrue(FString::Printf(TEXT("and it drives you back (from %.2f m to %.2f m)"), PPosBefore, KnockedTo),
		KnockedTo < PPosBefore);
	Events.Reset();
	TestFalse(FString::Printf(TEXT("but not twice in a row (cooldown is %.1f s)"), FHISim::ThiefShoveCd),
		FHISim::ThiefShove(Thief, Player, Events));

	// His hands are full when he is carrying, which is also when he is harmless.
	Thief = FHISim::CreateThief();
	Player = FHISim::CreatePlayer();
	Player.Pos = FVector2D(0.0f, -11.0f);
	Thief.Pos = FVector2D(0.0f, -9.9f);
	Thief.Carry = TEXT("TV");
	Events.Reset();
	TestFalse(FString::Printf(TEXT("a laden intruder has no hands free to grab you (carrying the %s)"), *Thief.Carry),
		FHISim::ThiefShove(Thief, Player, Events));

	// ---- AND THE SAME THING THROUGH THE GROUP PATH, which is the part that was missing.
	//
	// Everything above calls `ThiefShove` DIRECTLY. That is why it all passed while the game
	// never shoved anybody: `StepThief` had no call to it, and `StepIntruders` is the only
	// path the game uses. This project has now had this exact failure three times -- the
	// reload and weapon switch, `CrouchEyeHeight`, and now this -- so the assertion that
	// matters is not "is the function right" but "is the function REACHED".
	//
	// One man at 0.8 m, inside the 1.5 m reach, stepped through `StepIntruders`.
	{
		FHISim::FHIPlayer GroupPlayer = FHISim::CreatePlayer();
		GroupPlayer.Pos = FVector2D(0.0f, -11.0f);
		TArray<FHISim::FHIThief> Group = FHISim::CreateIntruders(1);
		TArray<FHISim::FHILootItem> GroupLoot = FHISim::CreateLoot();
		Group[0].Pos = FVector2D(0.0f, -10.2f);
		Group[0].Mode = TEXT("hunt");

		const float Before = GroupPlayer.Pos.Y;
		TArray<FHISim::FHIEvent> GroupEvents;
		FHISim::StepIntruders(Group, GroupPlayer, GroupLoot, GroupEvents, 1.0f / 60.0f);

		bool bShoved = false;
		for (const FHISim::FHIEvent& Ev : GroupEvents)
		{
			if (Ev.Type == TEXT("thiefShove"))
			{
				bShoved = true;
			}
		}
		TestTrue(TEXT("a man at arm's length SHOVES YOU through the group path the game calls"),
			bShoved);
		TestTrue(FString::Printf(TEXT("and you are staggered (%.2f s)"), GroupPlayer.Stagger),
			GroupPlayer.Stagger >= FHISim::ThiefShoveStagger);
		TestTrue(FString::Printf(TEXT("and driven back (%.2f m -> %.2f m)"), Before, GroupPlayer.Pos.Y),
			GroupPlayer.Pos.Y < Before);
	}

	return true;
}

// --------------------------------------------------- a hit stops the intruder

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimHit,
	"HI.Sim.Hit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimHit::RunTest(const FString& Parameters)
{
	// spec: test_sim.gd banner "a hit stops the intruder".
	//
	// The point of the banner is that a wound is a behavioural event, not just a number
	// going down: before this existed, a man at 1 hp walked, aimed and fired exactly like
	// a man at 100.
	const float Dt = 1.0f / 60.0f;
	FHISim::FHIPlayer Player = FHISim::CreatePlayer();
	FHISim::FHIThief Thief = FHISim::CreateThief();
	TArray<FHISim::FHILootItem> Loot = FHISim::CreateLoot();
	TArray<FHISim::FHIEvent> Events;

	Thief.Pos = FVector2D(-5.0f, -12.0f);
	Thief.Prev = Thief.Pos;
	Thief.Route = { FVector2D(0.0f, -12.0f) };
	// Settled on target, so losing the sight picture is observable.
	Thief.Aim = FHISim::ThiefAimTime;

	FHISim::DamageThief(Thief, 20.0f, Events);
	TestTrue(FString::Printf(TEXT("a hit staggers him (stagger=%.2f)"), Thief.Stagger),
		Thief.Stagger > 0.0f);
	TestTrue(FString::Printf(TEXT("and it takes his sight picture away (aim=%.2f)"), Thief.Aim),
		Thief.Aim == 0.0f);

	const FVector2D Before = Thief.Pos;
	Events.Reset();
	FHISim::StepThief(Thief, Player, Loot, Events, Dt);
	const float Moved = FVector2D::Distance(Thief.Pos, Before);
	TestTrue(FString::Printf(TEXT("and he does not move while staggered (moved %.4f m)"), Moved),
		Moved < 0.001f);

	// ---- damage: lethal hits down a body, and hp never goes negative
	Thief = FHISim::CreateThief();
	Events.Reset();
	const bool bKilled = FHISim::DamageThief(Thief, 150.0f, Events);
	TestTrue(TEXT("lethal damage kills the thief"), bKilled && !Thief.bAlive);
	bool bSawDown = false;
	for (const FHISim::FHIEvent& Ev : Events)
	{
		if (Ev.Type == TEXT("thiefDown"))
		{
			bSawDown = true;
		}
	}
	// The spec's label promises this event; its body only checks `alive`, so assert it.
	TestTrue(TEXT("and emits thiefDown"), bSawDown);

	Player = FHISim::CreatePlayer();
	Events.Reset();
	const bool bPlayerDown = FHISim::DamagePlayer(Player, 999.0f, Events);
	TestTrue(FString::Printf(TEXT("player death clamps hp at zero (hp=%.1f)"), Player.Hp),
		bPlayerDown && Player.Hp == 0.0f && !Player.bAlive);

	return true;
}

// ------------------------------------------------------ gunfire drives him off

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimFear,
	"HI.Sim.Fear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimFear::RunTest(const FString& Parameters)
{
	// spec: test_sim.gd banner "gunfire drives him off".
	const float Dt = 1.0f / 60.0f;
	FHISim::FHIPlayer Player = FHISim::CreatePlayer();
	FHISim::FHIThief Thief = FHISim::CreateThief();
	TArray<FHISim::FHILootItem> Loot = FHISim::CreateLoot();
	TArray<FHISim::FHIEvent> Events;

	TestTrue(FString::Printf(TEXT("he starts unafraid (fear=%.2f)"), Thief.Fear), Thief.Fear == 0.0f);

	for (int32 Idx = 0; Idx < 2; ++Idx)
	{
		FHISim::AlertThief(Thief, FVector2D(0.0f, -11.0f), 1.0f);
	}
	// The spec asserts only `mode != "flee"` here, which cannot fail as written: Mode is
	// set to "flee" by StepThief alone, and no frame has been stepped. The fear comparison
	// beneath it is the same intent and CAN fail, so both are asserted.
	TestTrue(FString::Printf(TEXT("two shots do not drive him off (fear=%.2f mode=%s)"), Thief.Fear, *Thief.Mode),
		Thief.Mode != TEXT("flee"));
	TestTrue(FString::Printf(TEXT("and leave him short of the flee threshold (fear=%.2f threshold=%.1f)"),
		Thief.Fear, FHISim::FearFlee), Thief.Fear < FHISim::FearFlee);

	for (int32 Idx = 0; Idx < 4; ++Idx)
	{
		FHISim::AlertThief(Thief, FVector2D(0.0f, -11.0f), 1.0f);
	}
	TestTrue(FString::Printf(TEXT("a sustained burst drives him off (fear=%.2f threshold=%.1f)"),
		Thief.Fear, FHISim::FearFlee), Thief.Fear >= FHISim::FearFlee);

	// fear settles when nothing is happening
	Thief.Fear = 1.0f;
	Thief.Mode = TEXT("hunt");
	Thief.Route = { FVector2D(0.0f, -12.0f) };
	Thief.Think = 0.0f;
	for (int32 Idx = 0; Idx < 60; ++Idx)
	{
		FHISim::StepThief(Thief, Player, Loot, Events, Dt);
	}
	TestTrue(FString::Printf(TEXT("and fear decays when the shooting stops (fear=%.2f)"), Thief.Fear),
		Thief.Fear < 1.0f);

	// ---- and once he breaks off he runs for the van
	Thief = FHISim::CreateThief();
	Thief.Pos = FVector2D(0.0f, -12.0f);
	Thief.Fear = FHISim::FearFlee;
	Thief.Mode = TEXT("flee");
	Thief.Stagger = 0.0f;
	Events.Reset();

	bool bGotAway = false;
	int32 Steps = 0;
	for (int32 Idx = 0; Idx < 3000; ++Idx)
	{
		Steps = Idx;
		if (Thief.bEscaped)
		{
			bGotAway = true;
			break;
		}
		FHISim::StepThief(Thief, Player, Loot, Events, Dt);
	}
	TestTrue(FString::Printf(TEXT("a frightened intruder runs for the van and gets away (ended at (%.2f, %.2f) after %d steps)"),
		Thief.Pos.X, Thief.Pos.Y, Steps), bGotAway);

	// Events are deliberately NOT drained in this loop -- the spec relies on the escaped
	// event surviving until the end, which is what the round reads to end itself.
	bool bSawEscape = false;
	for (const FHISim::FHIEvent& Ev : Events)
	{
		if (Ev.Type == TEXT("escaped"))
		{
			bSawEscape = true;
		}
	}
	TestTrue(TEXT("and announces it so the round can end (escaped event raised)"), bSawEscape);

	return true;
}

// ------------------------------------- the intruder cannot fire hands-full

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimHandsFull,
	"HI.Sim.HandsFull",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimHandsFull::RunTest(const FString& Parameters)
{
	// spec: test_sim.gd banner "the intruder cannot fire with his hands full".
	//
	// Run both ways: the assertion is a DIFFERENCE, so it only means something if the
	// unencumbered arm actually fires. A test that only checked the carrying case would
	// pass just as happily against a thief who never fires at all.
	const float Dt = 1.0f / 60.0f;
	for (int32 CarryIdx = 0; CarryIdx < 2; ++CarryIdx)
	{
		const bool bCarrying = (CarryIdx == 1);
		FHISim::FHIPlayer Player = FHISim::CreatePlayer();
		// CONTROLLED position, not the round's spawn (see HI.Sim.Aiming): the spawn moved into
		// the back zone on 2026-10-15, where "+2 m in front" crosses the InteriorZ divider and
		// blocks the very shot this test asserts, so the free-handed arm reads as not firing.
		Player.Pos = FVector2D(0.0f, -10.6f);
		FHISim::FHIThief Thief = FHISim::CreateThief();
		TArray<FHISim::FHILootItem> Loot = FHISim::CreateLoot();
		TArray<FHISim::FHIEvent> Events;

		// 2 m in front of the homeowner, well inside the 26 m sight radius.
		Thief.Pos = Player.Pos + FVector2D(0.0f, 2.0f);
		Thief.Prev = Thief.Pos;
		Thief.Carry = bCarrying ? TEXT("TV") : TEXT("");
		Thief.Cd = 0.0f;
		Thief.Mag = 8;
		Thief.Reloading = 0.0f;

		bool bFired = false;
		// Long enough to clear both the aim settle and the first cooldown.
		for (int32 Frame = 0; Frame < 60 * 3; ++Frame)
		{
			Events.Reset();
			FHISim::StepThief(Thief, Player, Loot, Events, Dt);
			for (const FHISim::FHIEvent& Ev : Events)
			{
				if (Ev.Type == TEXT("thiefShot"))
				{
					bFired = true;
				}
			}
		}
		TestTrue(FString::Printf(TEXT("%s (fired=%d carrying=%d)"),
			bCarrying ? TEXT("does NOT fire while carrying the loot") : TEXT("fires when his hands are free"),
			bFired ? 1 : 0, bCarrying ? 1 : 0),
			bFired != bCarrying);
	}
	return true;
}

// ----------------------------------------------- the intruder must aim first

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimAiming,
	"HI.Sim.Aiming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimAiming::RunTest(const FString& Parameters)
{
	// spec: test_sim.gd banner "the intruder must aim before firing".
	//
	// Without the settle he shot on the exact frame you became visible, so merely glimpsing
	// him cost health instantly -- and because the tracer is drawn from him to you, what you
	// saw was a line arriving through the wall you had just ducked behind.
	const float Dt = 1.0f / 60.0f;
	FHISim::FHIPlayer Player = FHISim::CreatePlayer();
	// CONTROLLED position, not the round's spawn. This asserts the aim settle before the shot,
	// so the pair needs a clear line. The spawn moved into the back zone on 2026-10-15, where
	// "2 m in front" (+Y) puts the intruder on the far side of the InteriorZ divider and he can
	// never see her -- a check that cannot fire cannot fail, and so cannot pass either.
	Player.Pos = FVector2D(0.0f, -10.6f);
	FHISim::FHIThief Thief = FHISim::CreateThief();
	TArray<FHISim::FHILootItem> Loot = FHISim::CreateLoot();
	TArray<FHISim::FHIEvent> Events;

	Thief.Pos = Player.Pos + FVector2D(0.0f, 2.0f);
	Thief.Prev = Thief.Pos;
	Thief.Cd = 0.0f;
	Thief.Mag = 8;

	float FirstShot = -1.0f;
	float T = 0.0f;
	while (T < 3.0f && FirstShot < 0.0f)
	{
		Events.Reset();
		FHISim::StepThief(Thief, Player, Loot, Events, Dt);
		for (const FHISim::FHIEvent& Ev : Events)
		{
			if (Ev.Type == TEXT("thiefShot"))
			{
				FirstShot = T;
			}
		}
		T += Dt;
	}
	TestTrue(FString::Printf(TEXT("the intruder does not fire the instant he sees you (first shot at %.2fs, aim time is %.2fs)"),
		FirstShot, FHISim::ThiefAimTime), FirstShot >= FHISim::ThiefAimTime - Dt);
	TestTrue(FString::Printf(TEXT("but he does fire once he has settled (first shot at %.2fs)"), FirstShot),
		FirstShot >= 0.0f && FirstShot < 1.5f);

	return true;
}

// ------------------------------------------------------------------- searching

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimSearching,
	"HI.Sim.Searching",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimSearching::RunTest(const FString& Parameters)
{
	// spec: test_sim.gd banner "searching" -- he has to go and look, because he does not
	// start knowing where anything is.
	const float Dt = 1.0f / 60.0f;
	FHISim::FHIPlayer Player = FHISim::CreatePlayer();
	FHISim::FHIThief Thief = FHISim::CreateThief();
	TArray<FHISim::FHILootItem> Loot = FHISim::CreateLoot();
	TArray<FHISim::FHIEvent> Events;

	TestTrue(TEXT("he spawns knowing where nothing is"),
		!Loot[0].bKnown && !Loot[1].bKnown && !Loot[2].bKnown);
	TestTrue(FString::Printf(TEXT("and having searched no rooms (searched=%d)"), Thief.Searched.Num()),
		Thief.Searched.Num() == 0);

	Thief.Think = 0.0f;
	Events.Reset();
	FHISim::StepThief(Thief, Player, Loot, Events, Dt);
	TestTrue(FString::Printf(TEXT("so his first move is to go and look, not to a known coordinate (mode=%s)"), *Thief.Mode),
		Thief.Mode == TEXT("search"));

	// Stand him in the living room and let him look around. Node positions are looked up
	// by name rather than hard-coded, so this cannot drift from the nav graph.
	const int32 LivingIdx = FHISim::NodeNames().IndexOfByKey(TEXT("living"));
	Thief.Pos = FHISim::NodePositions()[LivingIdx];
	Thief.Prev = Thief.Pos;
	Thief.Think = 0.0f;
	Events.Reset();
	FHISim::StepThief(Thief, Player, Loot, Events, Dt);
	TestTrue(FString::Printf(TEXT("standing in the living room finds the TV (tv known=%d, searched=%d)"),
		Loot[0].bKnown ? 1 : 0, Thief.Searched.Num()),
		Loot[0].bKnown && Thief.Searched.Contains(TEXT("living")));
	TestTrue(FString::Printf(TEXT("and it does NOT reveal what is in the study (laptop=%d safe=%d)"),
		Loot[1].bKnown ? 1 : 0, Loot[2].bKnown ? 1 : 0),
		!Loot[1].bKnown && !Loot[2].bKnown);

	// The kitchen holds nothing, and searching has to be able to come up empty.
	Thief = FHISim::CreateThief();
	Loot = FHISim::CreateLoot();
	const int32 KitchenIdx = FHISim::NodeNames().IndexOfByKey(TEXT("kitchen"));
	Thief.Pos = FHISim::NodePositions()[KitchenIdx];
	Thief.Prev = Thief.Pos;
	Thief.Think = 0.0f;
	Events.Reset();
	FHISim::StepThief(Thief, Player, Loot, Events, Dt);
	bool bFoundAny = false;
	for (const FHISim::FHILootItem& L : Loot)
	{
		if (L.bKnown)
		{
			bFoundAny = true;
		}
	}
	TestTrue(FString::Printf(TEXT("searching the kitchen, which is empty, finds nothing (searched=%d, anything found=%d)"),
		Thief.Searched.Num(), bFoundAny ? 1 : 0),
		!bFoundAny && Thief.Searched.Contains(TEXT("kitchen")));

	// Godot writes this as `searched.count("kitchen") == 1`; TArray has no Count, so the
	// occurrences are tallied by hand.
	int32 KitchenCount = 0;
	for (const FString& Name : Thief.Searched)
	{
		if (Name == TEXT("kitchen"))
		{
			++KitchenCount;
		}
	}
	TestTrue(FString::Printf(TEXT("and he does not search the same room twice (kitchen appears %d times)"), KitchenCount),
		KitchenCount == 1);

	// ---- THE FRONTHUB FREEZE, measured 2026-10-15 in a single-thief soak and reproduced
	// byte-identically across two 400 s runs: mode=search, target=bedroom, route=<none>,
	// stuck=0.00, parked 0.59 m from frontHub with the bedroom 5.66 m away and unsearched,
	// for the remaining 327 seconds. Two defects compounded: a reroute resolved the target
	// only as a loot LABEL, so the search target 'bedroom' fell through to the frontHub
	// default; and the search branch rebuilt a route only when the target CHANGED, so the
	// emptied route never came back -- while the stuck detector stayed blind because it
	// measures travel against the step he wanted, and an empty route wants nothing. Three
	// thieves hide it (the others search on); the survivor of two kills freezes the same
	// way, and then the round can never end. Both fixes live in StepThief; this is the
	// frozen state itself, asserting it walks out.
	{
		const int32 BedroomIdx = FHISim::NodeNames().IndexOfByKey(TEXT("bedroom"));
		const int32 HubIdx = FHISim::NodeNames().IndexOfByKey(TEXT("frontHub"));
		FHISim::FHIThief Frozen = FHISim::CreateThief();
		FHISim::FHIPlayer Idle = FHISim::CreatePlayer();
		TArray<FHISim::FHILootItem> House = FHISim::CreateLoot();
		TArray<FHISim::FHIEvent> FEvents;

		Frozen.Pos = FHISim::NodePositions()[HubIdx] + FVector2D(-0.30f, -0.51f);  // the soak's exact spot
		Frozen.Prev = Frozen.Pos;
		Frozen.Mode = TEXT("search");
		Frozen.Target = TEXT("bedroom");
		Frozen.Searched.Add(TEXT("living"));   // the TV was found here...
		Frozen.Searched.Add(TEXT("kitchen"));  // ...and the kitchen already came up empty,
		// so the bedroom is the NEAREST UNSEARCHED room and the target he is frozen on --
		// without this line the search branch would pick the kitchen instead and the test
		// would assert against a room the soak never named.
		House[0].bKnown = true;                            // the TV was found...
		House[0].bTaken = true;
		House[0].bDelivered = true;                        // ...and is gone

		FHISim::StepThief(Frozen, Idle, House, FEvents, Dt);
		TestTrue(FString::Printf(TEXT("a frozen searcher rebuilds his route on the next step (route=%d waypoint(s))"),
			Frozen.Route.Num()), Frozen.Route.Num() > 0);
		TestTrue(FString::Printf(TEXT("and it ends at the room he was searching, not somewhere else (last=(%.2f,%.2f))"),
			Frozen.Route.Num() > 0 ? Frozen.Route.Last().X : 0.0f,
			Frozen.Route.Num() > 0 ? Frozen.Route.Last().Y : 0.0f),
			Frozen.Route.Num() > 0
			&& Frozen.Route.Last().Equals(FHISim::NodePositions()[BedroomIdx], 0.001f));

		// And the proof that matters: he finishes the walk and the safe becomes known.
		bool bFoundSafe = false;
		for (int32 N = 0; N < 3600; ++N)
		{
			FHISim::StepThief(Frozen, Idle, House, FEvents, Dt);
			if (House[2].bKnown)                           // index 2 is the Safe
			{
				bFoundSafe = true;
				break;
			}
		}
		// The failure message carries his END STATE, because a walk that does not arrive
		// has a dozen causes -- pinned, dwelling, rerouting -- and "false" names none of
		// them. This is the message that diagnosed the divider-corner pin of 2026-10-15.
		TestTrue(FString::Printf(
			TEXT("and he reaches the bedroom and finds the safe within a minute of sim (end: pos=(%.2f,%.2f) mode=%s target=%s route=%d think=%.2f stuck=%.2f)"),
			Frozen.Pos.X, Frozen.Pos.Y, *Frozen.Mode, *Frozen.Target, Frozen.Route.Num(),
			Frozen.Think, Frozen.StuckT), bFoundSafe);
	}

	// ---- the reroute half of the same freeze, asserted where it is decided: a searcher
	// who cannot move must be rerouted to the ROOM HE IS SEARCHING. He is parked against
	// the world's north edge with a waypoint he can never reach, so the stuck detector
	// fires inside 1.2 s and the reroute target is the thing under test.
	{
		FHISim::FHIThief Pinned = FHISim::CreateThief();
		FHISim::FHIPlayer Far = FHISim::CreatePlayer();
		TArray<FHISim::FHILootItem> House = FHISim::CreateLoot();
		TArray<FHISim::FHIEvent> PEvents;

		const int32 BedroomIdx = FHISim::NodeNames().IndexOfByKey(TEXT("bedroom"));
		Pinned.Pos = FVector2D(0.0f, 23.7f);               // against ClampToWorld's north edge
		Pinned.Prev = Pinned.Pos;
		Pinned.Mode = TEXT("search");
		Pinned.Target = TEXT("bedroom");
		Pinned.Searched.Add(TEXT("living"));   // same reasoning as the frozen block: the
		Pinned.Searched.Add(TEXT("kitchen"));  // bedroom must BE the nearest unsearched room,
		// or the decide step overwrites the pinned route on frame one and nothing gets stuck
		Pinned.Route.Add(FVector2D(0.0f, 27.0f));          // a waypoint past the edge: unreachable
		Pinned.Think = 0.0f;

		bool bRerouted = false;
		// 480 frames = 8 s: the fresh thief's 2 s opening dwell, then the stuck detector's
		// own 1.2 s, then margin -- a budget that barely fits is a test that flakes.
		for (int32 N = 0; N < 480; ++N)
		{
			FHISim::StepThief(Pinned, Far, House, PEvents, Dt);
			for (const FHISim::FHIEvent& Ev : PEvents)
			{
				if (Ev.Type == TEXT("reroute"))
				{
					bRerouted = true;
				}
			}
			PEvents.Reset();
			if (bRerouted)
			{
				break;
			}
		}
		TestTrue(TEXT("a searcher who cannot move reroutes within a couple of seconds"), bRerouted);
		TestTrue(FString::Printf(TEXT("and the reroute sends him to the room he was searching (last=(%.2f,%.2f))"),
			Pinned.Route.Num() > 0 ? Pinned.Route.Last().X : 0.0f,
			Pinned.Route.Num() > 0 ? Pinned.Route.Last().Y : 0.0f),
			Pinned.Route.Num() > 0
			&& Pinned.Route.Last().Equals(FHISim::NodePositions()[BedroomIdx], 0.001f));
	}

	return true;
}

// -------------------------------------------------------------- the police clock

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimPoliceClock,
	"HI.Sim.PoliceClock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimPoliceClock::RunTest(const FString& Parameters)
{
	// spec: test_sim.gd banner "the police clock" -- a gunshot starts it and nothing else
	// does, and the sirens are what finally drives him off.
	const float Dt = 1.0f / 60.0f;
	FHISim::FHIPlayer Player = FHISim::CreatePlayer();
	FHISim::FHIThief Thief = FHISim::CreateThief();
	TArray<FHISim::FHILootItem> Loot = FHISim::CreateLoot();
	TArray<FHISim::FHIEvent> Events;

	Thief.Think = 0.0f;
	TestTrue(FString::Printf(TEXT("no clock runs before anybody shoots (alarm=%.1f)"), Thief.Alarm),
		Thief.Alarm < 0.0f);

	Events.Reset();
	for (int32 Frame = 0; Frame < 180; ++Frame)
	{
		FHISim::StepThief(Thief, Player, Loot, Events, Dt);
	}
	TestTrue(FString::Printf(TEXT("and a quiet round never starts one (alarm=%.1f after 3 s of a silent burglary)"), Thief.Alarm),
		Thief.Alarm < 0.0f);

	// A single shot is enough to make somebody call -- loudness 0.2, the quietest there is.
	FHISim::AlertThief(Thief, FVector2D(0.0f, -11.0f), 0.2f);
	TestTrue(FString::Printf(TEXT("one gunshot starts the clock (alarm=%.2f)"), Thief.Alarm),
		Thief.Alarm >= 0.0f);

	// Hold fear down, so ONLY the clock can end it, and run past the expected arrival.
	Events.Reset();
	int32 Steps = 0;
	const int32 Limit = static_cast<int32>((FHISim::PoliceTime + 5.0f) / Dt);
	while (Steps < Limit && !Thief.bSirens)
	{
		Thief.Fear = 0.0f;
		FHISim::StepThief(Thief, Player, Loot, Events, Dt);
		++Steps;
	}
	TestTrue(FString::Printf(TEXT("the sirens arrive on schedule (at %.1f s, expected %.0f s)"),
		Steps * Dt, FHISim::PoliceTime), Thief.bSirens);

	bool bHeard = false;
	for (const FHISim::FHIEvent& Ev : Events)
	{
		if (Ev.Type == TEXT("sirens"))
		{
			bHeard = true;
		}
	}
	TestTrue(TEXT("and the round is told, so it can react (sirens event raised)"), bHeard);
	TestTrue(FString::Printf(TEXT("and the sirens are what finally drives him off (fear=%.1f, threshold=%.1f)"),
		Thief.Fear, FHISim::FearFlee), Thief.Fear >= FHISim::FearFlee);

	return true;
}

// ------------------------------------------- heist against a MOVING homeowner

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimMovingHeist,
	"HI.Sim.MovingHeist",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimMovingHeist::RunTest(const FString& Parameters)
{
	// spec: test_sim.gd banner "heist against a MOVING homeowner".
	//
	// The homeowner only counts as a threat if the intruder still finishes while being
	// actively interfered with: chased, standing in the doorway, and standing on each of
	// the two waypoints the route depends on.
	const float Dt = 1.0f / 60.0f;
	const TCHAR* CaseNames[4] = {
		TEXT("chases the intruder"),
		TEXT("blocks the front doorway"),
		TEXT("stands on the front-hub waypoint"),
		TEXT("stands on the living-room waypoint"),
	};

	for (int32 Mode = 1; Mode <= 4; ++Mode)
	{
		FHISim::FHIPlayer Player = FHISim::CreatePlayer();
		FHISim::FHIThief Thief = FHISim::CreateThief();
		TArray<FHISim::FHILootItem> Loot = FHISim::CreateLoot();
		TArray<FHISim::FHIEvent> Events;

		float T = 0.0f;
		int32 Reroutes = 0;
		while (T < 180.0f && FHISim::Remaining(Loot) > 0)
		{
			switch (Mode)
			{
			case 1:
			{
				const FVector2D Chase = Thief.Pos - Player.Pos;
				if (Chase.Size() > 0.01f)
				{
					const FVector2D Wish = Player.Pos + Chase.GetSafeNormal() * FHISim::PlayerSpeed * Dt;
					Player.Pos = FHISim::ClampToWorld(
						FHISim::ResolveCircle(Wish, FHISim::PlayerRadius), FHISim::PlayerRadius);
				}
				break;
			}
			case 2: Player.Pos = FVector2D(0.0f, -8.6f); break;
			case 3: Player.Pos = FVector2D(0.0f, -11.2f); break;
			case 4: Player.Pos = FVector2D(-3.6f, -11.2f); break;
			default: break;
			}

			FHISim::StepThief(Thief, Player, Loot, Events, Dt);
			for (const FHISim::FHIEvent& Ev : Events)
			{
				if (Ev.Type == TEXT("reroute"))
				{
					++Reroutes;
				}
			}
			Events.Reset();
			T += Dt;
		}

		const int32 Left = FHISim::Remaining(Loot);
		TestTrue(FString::Printf(TEXT("the heist completes even when the homeowner %s (delivered %d/3 in %.0fs with %d re-routes)"),
			CaseNames[Mode - 1], 3 - Left, T, Reroutes), Left == 0);
	}
	return true;
}

// ------------------------------------------------------- multiple intruders

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimMultipleIntruders,
	"HI.Sim.MultipleIntruders",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimMultipleIntruders::RunTest(const FString& Parameters)
{
	// spec: test_sim.gd banner "multiple intruders". Everything here targets the pairwise
	// separation, which is the code path only a crowd of bodies reaches -- and the one
	// place a crowd can do something the single-intruder version never had to survive.
	const float Dt = 1.0f / 60.0f;
	const float MinGap = FHISim::PlayerRadius + FHISim::ThiefRadius;
	TArray<FHISim::FHIEvent> Events;

	// ---- built, distinct, and not on top of one another
	TArray<FHISim::FHIThief> Crew = FHISim::CreateIntruders(3);
	TestTrue(FString::Printf(TEXT("three bodies are built with distinct ids (%d/%d/%d)"),
		Crew[0].Id, Crew[1].Id, Crew[2].Id),
		Crew.Num() == 3 && Crew[0].Id == 0 && Crew[1].Id == 1 && Crew[2].Id == 2);

	bool bSpawnOk = true;
	for (int32 I = 0; I < Crew.Num(); ++I)
	{
		for (int32 J = I + 1; J < Crew.Num(); ++J)
		{
			if (FVector2D::Distance(Crew[I].Pos, Crew[J].Pos) < FHISim::ThiefRadius * 2.0f - 1e-3f)
			{
				bSpawnOk = false;
			}
		}
	}
	TestTrue(TEXT("they spawn clear of one another"), bSpawnOk);

	// ---- separation is a contract, not a best effort
	FHISim::FHIPlayer Player = FHISim::CreatePlayer();
	TArray<FHISim::FHILootItem> Loot = FHISim::CreateLoot();
	Crew = FHISim::CreateIntruders(3);
	float WorstGap = TNumericLimits<float>::Max();
	int32 OutOfBounds = 0;
	const int32 ContactFrames = 900;
	for (int32 Step = 0; Step < ContactFrames; ++Step)
	{
		// Deliberately walk the player into the cluster, so contact is forced.
		const float Ang = static_cast<float>(Step) * 0.11f;
		Player.Pos = FVector2D(FMath::Cos(Ang) * 1.2f, -11.0f + FMath::Sin(Ang) * 1.2f);
		Events.Reset();
		FHISim::StepIntruders(Crew, Player, Loot, Events, Dt);
		for (int32 I = 0; I < Crew.Num(); ++I)
		{
			for (int32 J = I + 1; J < Crew.Num(); ++J)
			{
				WorstGap = FMath::Min(WorstGap, FVector2D::Distance(Crew[I].Pos, Crew[J].Pos));
			}
			WorstGap = FMath::Min(WorstGap, FVector2D::Distance(Crew[I].Pos, Player.Pos));
			const FVector2D C = Crew[I].Pos;
			if (C.X < FHISim::Bounds.Min.X - 0.01f || C.X > FHISim::Bounds.Max.X + 0.01f
				|| C.Y < FHISim::Bounds.Min.Y - 0.01f || C.Y > FHISim::Bounds.Max.Y + 0.01f)
			{
				++OutOfBounds;
			}
		}
	}
	TestTrue(FString::Printf(TEXT("no two bodies ever overlap, over %d frames of forced contact (closest approach %.4f m, want >= %.2f m)"),
		ContactFrames, WorstGap, MinGap), WorstGap >= MinGap - 0.02f);
	TestTrue(FString::Printf(TEXT("nobody is pushed outside the world bounds (%d bodies out of bounds)"), OutOfBounds),
		OutOfBounds == 0);

	// ---- the homeowner is still the body that yields. If the intruder yielded instead, a
	// defender standing still would wall the crew in -- a bug that already came back from
	// play once.
	TArray<FHISim::FHIThief> Solo = FHISim::CreateIntruders(1);
	FHISim::FHIPlayer SoloPlayer = FHISim::CreatePlayer();
	TArray<FHISim::FHILootItem> SoloLoot = FHISim::CreateLoot();
	// INSIDE the collision distance, or there is nothing to resolve and the test passes
	// for the wrong reason: 0.84 m is the sum of the two radii.
	SoloPlayer.Pos = FVector2D(0.0f, FHISim::Drop.Y + 0.5f);
	Solo[0].Pos = FVector2D(0.0f, FHISim::Drop.Y + 0.8f);
	const FVector2D PlayerBefore = SoloPlayer.Pos;
	Events.Reset();
	FHISim::StepIntruders(Solo, SoloPlayer, SoloLoot, Events, Dt);
	TestTrue(FString::Printf(TEXT("a body against the homeowner still yields on the HOMEOWNER (player now (%.2f, %.2f))"),
		SoloPlayer.Pos.X, SoloPlayer.Pos.Y),
		!SoloPlayer.Pos.Equals(PlayerBefore, 1e-4f));

	// ---- attribution: every per-body event says which body emitted it
	Crew = FHISim::CreateIntruders(3);
	FHISim::FHIPlayer AttrPlayer = FHISim::CreatePlayer();
	TArray<FHISim::FHILootItem> AttrLoot = FHISim::CreateLoot();
	for (FHISim::FHIThief& T : Crew)
	{
		T.Hp = 1.0f;
	}
	AttrPlayer.Pos = FVector2D(0.0f, 4.0f);
	for (int32 Idx = 0; Idx < Crew.Num(); ++Idx)
	{
		Crew[Idx].Pos = FVector2D(-1.5f + static_cast<float>(Idx) * 1.5f, 5.5f);
	}
	bool bIdOk = true;
	FString Stray;
	TSet<int32> Ids;
	int32 CrewShots = 0;
	for (int32 Step = 0; Step < 600; ++Step)
	{
		Events.Reset();
		FHISim::StepIntruders(Crew, AttrPlayer, AttrLoot, Events, Dt);
		for (const FHISim::FHIEvent& Ev : Events)
		{
			if (Ev.Type == TEXT("thiefShot"))
			{
				++CrewShots;
			}
			if (Ev.Id != INDEX_NONE)
			{
				Ids.Add(Ev.Id);
			}
			else if (Ev.Type != TEXT("sirens") && Ev.Type != TEXT("allStolen"))
			{
				bIdOk = false;
				Stray = Ev.Type;
			}
		}
	}
	TestTrue(FString::Printf(TEXT("every per-body event carries the id of the body that emitted it (unattributed: %s)"),
		Stray.IsEmpty() ? TEXT("none") : *Stray), bIdOk);
	TestTrue(FString::Printf(TEXT("and the tagged events came from more than one body (%d ids seen)"), Ids.Num()),
		Ids.Num() >= 2);
	TestTrue(FString::Printf(TEXT("shots are among the attributable events (%d shots over 600 frames)"), CrewShots),
		CrewShots > 0);

	// ---- the separation push must not be banked as travel. Overlapping bodies are pushed
	// apart, and afterwards `prev` must have moved WITH the frame start, so the stuck
	// detector's `pos - prev` reads zero for a pair that wanted to stay still.
	TArray<FHISim::FHIThief> Quiet = FHISim::CreateIntruders(2);
	Quiet[0].Pos = FVector2D(0.0f, 5.0f);
	Quiet[1].Pos = FVector2D(0.05f, 5.0f);
	const FVector2D Q0Before = Quiet[0].Pos;
	const FVector2D Q1Before = Quiet[1].Pos;
	FHISim::FHIPlayer QuietPlayer = FHISim::CreatePlayer();
	TArray<FHISim::FHILootItem> QuietLoot = FHISim::CreateLoot();
	Events.Reset();
	// dt 0, exactly as the spec does: the push must resolve without any simulated time.
	FHISim::StepIntruders(Quiet, QuietPlayer, QuietLoot, Events, 0.0f);
	const float Drift0 = FVector2D::Distance(Quiet[0].Prev, Q0Before);
	const float Drift1 = FVector2D::Distance(Quiet[1].Prev, Q1Before);
	const bool bApart = FVector2D::Distance(Quiet[0].Pos, Quiet[1].Pos) >= MinGap - 0.02f;
	TestTrue(FString::Printf(TEXT("the push is not banked as travel (apart=%d prev_drift=%.4f/%.4f)"),
		bApart ? 1 : 0, Drift0, Drift1),
		bApart && Drift0 <= 1e-6f && Drift1 <= 1e-6f);

	return true;
}

// -------------------------------------------------------------- solid bodies

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimSolidBodies,
	"HI.Sim.SolidBodies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimSolidBodies::RunTest(const FString& Parameters)
{
	// spec: the tail of test_sim.gd's "solid bodies" banner. Its unit-level assertions live
	// in HI.Sim.Separation; these three are integration checks that need StepThief, and
	// they are the ones that catch a body walking through another.
	//
	// The banner's own comment explains why both directions are tested: every earlier check
	// held the homeowner still, and a stationary player is the ONE case that always worked.
	const float Dt = 1.0f / 60.0f;
	const float BodyGap = FHISim::PlayerRadius + FHISim::ThiefRadius;
	TArray<FHISim::FHIEvent> Events;

	// ---- he never walks through a stationary homeowner across a whole heist
	{
		FHISim::FHIPlayer Player = FHISim::CreatePlayer();
		FHISim::FHIThief Thief = FHISim::CreateThief();
		TArray<FHISim::FHILootItem> Loot = FHISim::CreateLoot();
		float Closest = TNumericLimits<float>::Max();
		for (int32 Frame = 0; Frame < 60 * 180; ++Frame)
		{
			FHISim::StepThief(Thief, Player, Loot, Events, Dt);
			Events.Reset();
			Closest = FMath::Min(Closest, FVector2D::Distance(Thief.Pos, Player.Pos));
			if (FHISim::Remaining(Loot) == 0 || !Thief.bAlive)
			{
				break;
			}
		}
		TestTrue(FString::Printf(TEXT("the thief never walks through the homeowner across a whole heist (closest approach %.3f m, want >= %.3f m)"),
			Closest, BodyGap), Closest >= BodyGap - 1e-3f);
	}

	// ---- and the OTHER direction, which is the one a player actually notices: the
	// HOMEOWNER driving into the intruder. game.gd moves the player from input and only
	// then calls step_thief, so separation has to survive an active player.
	{
		FHISim::FHIPlayer Player = FHISim::CreatePlayer();
		FHISim::FHIThief Thief = FHISim::CreateThief();
		TArray<FHISim::FHILootItem> Loot = FHISim::CreateLoot();
		// Nothing left to steal, so he holds position instead of setting off after loot.
		for (FHISim::FHILootItem& L : Loot)
		{
			L.bTaken = true;
		}
		Thief.Pos = FVector2D(0.0f, 0.0f);
		Thief.Prev = FVector2D(0.0f, 0.0f);
		Thief.Route.Empty();
		Thief.Target.Empty();
		Player.Pos = FVector2D(0.0f, -4.0f);

		float Worst = TNumericLimits<float>::Max();
		float PushedZ = 0.0f;
		for (int32 Frame = 0; Frame < 60 * 6; ++Frame)
		{
			// Exactly what game.gd does: move from input, THEN hand over to step_thief.
			FVector2D P = Player.Pos + FVector2D(0.0f, 1.0f) * FHISim::PlayerSpeed * Dt;
			P = FHISim::ResolveCircle(P, FHISim::PlayerRadius);
			P = FHISim::ClampToWorld(P, FHISim::PlayerRadius);
			Player.Pos = P;
			FHISim::StepThief(Thief, Player, Loot, Events, Dt);
			Events.Reset();
			Worst = FMath::Min(Worst, FVector2D::Distance(Thief.Pos, Player.Pos));
			PushedZ = FMath::Max(PushedZ, Thief.Pos.Y);
		}
		TestTrue(FString::Printf(TEXT("the homeowner cannot walk into the intruder (closest %.3f m, want >= %.3f m)"),
			Worst, BodyGap), Worst >= BodyGap - 1e-3f);
		TestTrue(FString::Printf(TEXT("the intruder holds his ground rather than being driven off it (he drifted to z=%.2f)"),
			PushedZ), PushedZ < 0.05f);
	}

	return true;
}

// -------------------------------------------------------------- LOS invariant

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimLosInvariant,
	"HI.Sim.LosInvariant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimLosInvariant::RunTest(const FString& Parameters)
{
	// spec: the tail of test_sim.gd's "visibility + minimap fog of war" banner.
	const float Dt = 1.0f / 60.0f;
	TArray<FHISim::FHIEvent> Events;

	// ---- he never drifts outside the world
	{
		FHISim::FHIPlayer Player = FHISim::CreatePlayer();
		FHISim::FHIThief Thief = FHISim::CreateThief();
		TArray<FHISim::FHILootItem> Loot = FHISim::CreateLoot();
		FString OutOfBounds;
		for (int32 Frame = 0; Frame < 60 * 120; ++Frame)
		{
			FHISim::StepThief(Thief, Player, Loot, Events, Dt);
			Events.Reset();
			const FVector2D P = Thief.Pos;
			if (P.X < FHISim::Bounds.Min.X - 0.01f || P.X > FHISim::Bounds.Max.X + 0.01f
				|| P.Y < FHISim::Bounds.Min.Y - 0.01f || P.Y > FHISim::Bounds.Max.Y + 0.01f)
			{
				OutOfBounds = FString::Printf(TEXT("left bounds at (%.2f, %.2f)"), P.X, P.Y);
				break;
			}
			if (!Thief.bAlive)
			{
				break;
			}
		}
		TestTrue(FString::Printf(TEXT("thief never drifts outside the world bounds (%s)"),
			OutOfBounds.IsEmpty() ? TEXT("stayed inside for 120 s") : *OutOfBounds),
			OutOfBounds.IsEmpty());
	}

	// ---- THE invariant: never fire without line of sight.
	// Walk the homeowner on a Lissajous path so the pair pass in and out of cover.
	{
		FHISim::FHIPlayer Player = FHISim::CreatePlayer();
		FHISim::FHIThief Thief = FHISim::CreateThief();
		TArray<FHISim::FHILootItem> Loot = FHISim::CreateLoot();
		int32 Shots = 0;
		int32 BlockedFrames = 0;
		FString Violation;
		for (int32 Frame = 0; Frame < 60 * 90; ++Frame)
		{
			Player.Pos = FVector2D(
				FMath::Sin(static_cast<float>(Frame) / 97.0f) * 9.0f,
				-13.0f + FMath::Cos(static_cast<float>(Frame) / 131.0f) * 6.0f);
			if (FHISim::LosBlocked(Thief.Pos, Player.Pos))
			{
				++BlockedFrames;
			}
			Events.Reset();
			FHISim::StepThief(Thief, Player, Loot, Events, Dt);
			for (const FHISim::FHIEvent& Ev : Events)
			{
				if (Ev.Type == TEXT("thiefShot"))
				{
					++Shots;
					if (FHISim::LosBlocked(Thief.Pos, Player.Pos))
					{
						Violation = FString::Printf(TEXT("fired through a wall at frame %d"), Frame);
						break;
					}
				}
			}
			if (!Violation.IsEmpty())
			{
				break;
			}
		}
		TestTrue(FString::Printf(TEXT("thief NEVER fires while line of sight is blocked (%s)"),
			Violation.IsEmpty() ? TEXT("no violation") : *Violation),
			Violation.IsEmpty());
		// A check that cannot fail is not a check: if he never fires, the invariant above is
		// vacuously true, so the spec also asserts that the path was actually exercised.
		TestTrue(FString::Printf(TEXT("the invariant test actually exercised the path (%d shots, %d blocked frames available)"),
			Shots, BlockedFrames), Shots > 0);
	}

	return true;
}

// ------------------------------------------------------------------- the round

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimRound,
	"HI.Sim.Round",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimRound::RunTest(const FString& Parameters)
{
	// The round model ported out of game.gd, which could never be tested because it is a
	// SceneTree node with rendering attached. These assertions are the ones the Godot
	// project has no equivalent of: its own suite stops at the sim.
	const float Dt = 1.0f / 60.0f;

	// ---- a round starts ready
	{
		FHISim::FHIRound R = FHISim::FHIRound::Start(3);
		TestTrue(FString::Printf(TEXT("a round starts in play with three intruders (%d built, %d loot)"),
			R.Intruders.Num(), R.Remaining()),
			R.State == FHISim::EHIRoundState::Play && R.Intruders.Num() == 3 && R.Remaining() == 3);
		TestTrue(TEXT("and nobody has been put down yet"), R.ThievesDown == 0);
	}

	// ---- the homeowner carries momentum rather than snapping to pace
	{
		FHISim::FHIRound M = FHISim::FHIRound::Start(1);
		// A CONTROLLED spot, not the round's spawn. The spawn moved off-axis into the back
		// zone on 2026-10-15, where a forward (+Y) walk runs into the InteriorZ divider inside
		// a second and the full-pace assertion would fail on geometry, not on momentum. Stand
		// her on the open front-zone axis instead, where +Y is clear through the door gap.
		M.Player.Pos = FVector2D(0.0f, -10.6f);
		M.Player.Yaw = 0.0f;                       // yaw 0 faces +Z
		FHISim::FHIInput In;
		In.Axis = FVector2D(0.0f, 1.0f);           // walk forward
		const FVector2D StartPos = M.Player.Pos;
		M.Step(In, Dt);
		const float FirstFrame = FVector2D::Distance(M.Player.Pos, StartPos);
		// WHICH WAY, not just how far. The distance assertions below passed while the axis was
		// INVERTED -- W walked backwards and every number here was identical -- because nothing
		// in this test ever looked at the direction. At yaw 0 forward is +Y, so a step on
		// `Axis = (0, 1)` must increase Y and leave X alone.
		TestTrue(FString::Printf(TEXT("and he walks FORWARD, not backwards (dY=%.4f, dX=%.4f)"),
			M.Player.Pos.Y - StartPos.Y, M.Player.Pos.X - StartPos.X),
			M.Player.Pos.Y > StartPos.Y && FMath::IsNearlyEqual(M.Player.Pos.X, StartPos.X, 1e-4f));
		TestTrue(FString::Printf(TEXT("he does not cross the room on the first frame (%.4f m)"), FirstFrame),
			FirstFrame > 0.0f && FirstFrame < FHISim::PlayerSpeed * Dt * 0.5f);

		for (int32 Frame = 0; Frame < 60; ++Frame)
		{
			M.Step(In, Dt);
		}
		TestTrue(FString::Printf(TEXT("but he is at full pace a second later (%.2f m/s, want %.2f)"),
			M.Player.Vel.Size(), FHISim::PlayerSpeed),
			FMath::IsNearlyEqual(M.Player.Vel.Size(), FHISim::PlayerSpeed, 0.05f));
	}

	// ---- firing spends a round, and EVERY intruder hears it
	{
		FHISim::FHIRound F = FHISim::FHIRound::Start(3);
		// Known distances, so the loudness scaling is observable rather than assumed.
		F.Intruders[0].Pos = F.Player.Pos + FVector2D(0.0f, 2.0f);     // close
		F.Intruders[1].Pos = F.Player.Pos + FVector2D(0.0f, 14.0f);    // across the house
		F.Intruders[2].Pos = F.Player.Pos + FVector2D(0.0f, 30.0f);    // barely
		F.Player.Cd = 0.0f;
		const int32 MagBefore = F.Player.Mag;

		FHISim::FHIInput In;
		In.bFire = true;
		F.Step(In, Dt);

		TestEqual(TEXT("firing spends exactly one round"), F.Player.Mag, MagBefore - 1);
		TestTrue(FString::Printf(TEXT("and the weapon cooldown starts (%.2f s)"), F.Player.Cd),
			F.Player.Cd > 0.0f);
		TestTrue(FString::Printf(TEXT("and every intruder hears it, not just the one you aimed at (fear %.3f / %.3f / %.3f)"),
			F.Intruders[0].Fear, F.Intruders[1].Fear, F.Intruders[2].Fear),
			F.Intruders[0].Fear > 0.0f && F.Intruders[1].Fear > 0.0f && F.Intruders[2].Fear > 0.0f);
		TestTrue(FString::Printf(TEXT("and the close man hears it louder than the distant one (%.3f vs %.3f)"),
			F.Intruders[0].Fear, F.Intruders[1].Fear),
			F.Intruders[0].Fear > F.Intruders[1].Fear);
		TestTrue(TEXT("and the first shot starts the police clock"), F.Intruders[0].Alarm >= 0.0f);

		// ---- and the shot is announced for the actor layer to trace, carrying the
		// weapon's own numbers rather than ones the tracer looked up for itself. A tracer
		// that disagrees with the round about which weapon was fired is how you get a
		// shotgun's spread with a pistol's damage.
		const TArray<FHISim::FHIEvent> Fired = F.ConsumeEvents();
		int32 ShotEvents = 0;
		for (const FHISim::FHIEvent& Ev : Fired)
		{
			if (Ev.Type != TEXT("playerShot"))
			{
				continue;
			}
			++ShotEvents;
			TestTrue(FString::Printf(TEXT("the shot event carries the weapon's damage (%.1f)"), Ev.Damage),
				FMath::IsNearlyEqual(Ev.Damage, FHISim::Weapon(TEXT("pistol")).Damage, 1e-4f));
			TestTrue(FString::Printf(TEXT("and its pellet count and spread (pellets=%d, spread=%.3f)"),
				Ev.Pellets, Ev.Spread),
				Ev.Pellets == 1 && FMath::IsNearlyEqual(Ev.Spread, 0.02f, 1e-6f));
		}
		TestEqual(TEXT("and exactly one shot event is raised per trigger pull"), ShotEvents, 1);
	}

	// ---- the round is NOT won until the LAST intruder is down
	{
		FHISim::FHIRound W = FHISim::FHIRound::Start(3);
		W.DamageIntruder(W.Intruders[0].Id, 1000.0f);
		TestTrue(FString::Printf(TEXT("the first man down does not win the round (%d down, %d still standing)"),
			W.ThievesDown, W.AnyIntruderStanding() ? 1 : 0),
			W.State == FHISim::EHIRoundState::Play && W.ThievesDown == 1);
		W.DamageIntruder(W.Intruders[1].Id, 1000.0f);
		TestTrue(TEXT("nor does the second"), W.State == FHISim::EHIRoundState::Play);
		W.DamageIntruder(W.Intruders[2].Id, 1000.0f);
		TestTrue(FString::Printf(TEXT("but the last one does (%d down)"), W.ThievesDown),
			W.State == FHISim::EHIRoundState::Won && W.ThievesDown == 3);
	}

	// ---- a downed intruder drops what he was carrying, where he fell
	{
		FHISim::FHIRound D = FHISim::FHIRound::Start(1);
		D.Intruders[0].Carry = TEXT("TV");
		D.Intruders[0].Pos = FVector2D(3.0f, -12.0f);
		D.Loot[0].bTaken = true;
		D.DamageIntruder(D.Intruders[0].Id, 1000.0f);
		TestTrue(FString::Printf(TEXT("his loot falls where he does (taken=%d at (%.1f, %.1f))"),
			D.Loot[0].bTaken ? 1 : 0, D.Loot[0].Pos.X, D.Loot[0].Pos.Y),
			!D.Loot[0].bTaken && D.Loot[0].Pos.Equals(D.Intruders[0].Pos, 0.001f));
	}

	// ---- and the player dying loses the round
	{
		FHISim::FHIRound L = FHISim::FHIRound::Start(1);
		TArray<FHISim::FHIEvent> Drains;
		FHISim::DamagePlayer(L.Player, 1000.0f, Drains);
		L.Events.Append(Drains);
		FHISim::FHIInput In;
		L.Step(In, Dt);
		TestTrue(TEXT("the homeowner going down loses the round"),
			L.State == FHISim::EHIRoundState::Lost);
	}

	// ---- and it STILL loses when the blow lands the way the RUNTIME lands it.
	//
	// The test above appends the event and steps. The game cannot do that: a hit is resolved
	// by the actor layer's trace, which runs while the frame's drained events are being
	// processed -- AFTER this frame's Step and its rule scan, and AFTER the drain. So the
	// `playerDown` spends one frame boundary sitting in the round's list before any rule sees
	// it, alongside the non-lethal `playerHit`s from the same volley. On 2026-10-14 that
	// runtime sequence left a homeowner at 0 hp with the round playing on for ever -- no
	// decision, no banner, a heist over her corpse -- while the test above passed, because
	// the test above is not the sequence the game runs. This one is: three shots, one kill,
	// queued after a step and a drain, and the NEXT step's scan has to end the round.
	{
		FHISim::FHIRound R = FHISim::FHIRound::Start(3);
		FHISim::FHIInput In;
		R.Step(In, Dt);                       // frame N: the sim announces the volley ...
		R.ConsumeEvents();                    // ... the actor layer drains it ...
		FHISim::DamagePlayer(R.Player, 5.0f, R.Events);      // ... traces land two wounds ...
		FHISim::DamagePlayer(R.Player, 5.0f, R.Events);
		const bool bKilled = FHISim::DamagePlayer(R.Player, 100000.0f, R.Events);   // ... and the kill.
		TestTrue(TEXT("the killing blow empties the homeowner"), bKilled && !R.Player.bAlive);
		TestTrue(FString::Printf(TEXT("and leaves playerDown pending in the round's list (%d event(s))"),
			R.Events.Num()), R.Events.Num() == 3);
		R.Step(In, Dt);                       // frame N+1: the rule scan must see it
		TestTrue(TEXT("a killing blow resolved after the drain still loses the round on the next step"),
			R.State == FHISim::EHIRoundState::Lost);
	}

	// ---- THE OPENING HOLDS FIRE (Richard, 2026-10-15: "the first few seconds are too
	// brutal. Make them easier."). The cd gate is arithmetic, so this is exact rather than
	// statistical: four seconds with an idle homeowner and not one shot; the moment the hold
	// expires the first man fires; and the crew's first shots are staggered, not a volley.
	// Before ThiefOpeningCd existed -- Godot's 1.2 s opening was ported key for key -- the
	// same idle homeowner measured HP 57 at t=3.0 and death between 8 and 12.5 s.
	{
		FHISim::FHIRound R = FHISim::FHIRound::Start(3);
		// CONTROLLED geometry, not the round's spawn. This test is about ThiefOpeningCd, so
		// the crew must actually HAVE a sight line for the hold to be the only thing gating
		// their first shot. The real spawn moved off the door axis on 2026-10-15 (the spawn
		// kill-zone fix), which would leave the Drop crew with no line at all and the "fires
		// once the hold expires" half could never trip -- a check that cannot fail is not a
		// check. Stand the homeowner back on the old on-axis spot so the Drop -> door ->
		// homeowner channel exists and ONLY the opening cooldown stands between.
		R.Player.Pos = FVector2D(0.0f, -10.6f);
		TestTrue(FString::Printf(TEXT("the crew's opening holds are staggered (%.2f / %.2f / %.2f s)"),
			R.Intruders[0].Cd, R.Intruders[1].Cd, R.Intruders[2].Cd),
			FMath::IsNearlyEqual(R.Intruders[0].Cd, FHISim::ThiefOpeningCd)
			&& FMath::IsNearlyEqual(R.Intruders[1].Cd, FHISim::ThiefOpeningCd + FHISim::ThiefOpeningStagger)
			&& FMath::IsNearlyEqual(R.Intruders[2].Cd, FHISim::ThiefOpeningCd + 2.0f * FHISim::ThiefOpeningStagger));

		FHISim::FHIInput In;
		int32 ShotsInHold = 0;
		int32 ShotsAfter = 0;
		for (int32 Frame = 0; Frame < 237; ++Frame)      // 3.95 s: inside the hold
		{
			R.Step(In, Dt);
			for (const FHISim::FHIEvent& Ev : R.ConsumeEvents())
			{
				if (Ev.Type == TEXT("thiefShot"))
				{
					++ShotsInHold;
				}
			}
		}
		TestEqual(TEXT("not one shot in the first four seconds of a round"), ShotsInHold, 0);
		for (int32 Frame = 0; Frame < 42; ++Frame)       // to 4.65 s: past the first hold
		{
			R.Step(In, Dt);
			for (const FHISim::FHIEvent& Ev : R.ConsumeEvents())
			{
				if (Ev.Type == TEXT("thiefShot"))
				{
					++ShotsAfter;
				}
			}
		}
		TestTrue(FString::Printf(TEXT("and the first man fires once the hold expires (%d shot(s) between 3.95 s and 4.65 s)"),
			ShotsAfter), ShotsAfter >= 1);
	}

	// ---- a finished round stops ticking
	{
		FHISim::FHIRound Z = FHISim::FHIRound::Start(1);
		Z.DamageIntruder(Z.Intruders[0].Id, 1000.0f);
		const FVector2D Frozen = Z.Player.Pos;
		FHISim::FHIInput In;
		In.Axis = FVector2D(0.0f, 1.0f);
		for (int32 Frame = 0; Frame < 30; ++Frame)
		{
			Z.Step(In, Dt);
		}
		TestTrue(TEXT("a won round is inert, so the homeowner does not keep walking through it"),
			Z.Player.Pos.Equals(Frozen, 0.0001f));
	}

	// ---- ConsumeEvents hands the frame's events over exactly once
	{
		FHISim::FHIRound C = FHISim::FHIRound::Start(3);
		C.Player.Cd = 0.0f;
		FHISim::FHIInput In;
		In.bFire = true;
		C.Step(In, Dt);
		const TArray<FHISim::FHIEvent> First = C.ConsumeEvents();
		const TArray<FHISim::FHIEvent> Second = C.ConsumeEvents();
		TestTrue(FString::Printf(TEXT("ConsumeEvents hands the frame over once and then empties (%d then %d)"),
			First.Num(), Second.Num()),
			Second.Num() == 0);
	}

	return true;
}

// ------------------------------------------------------------------- elevation

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimElevation,
	"HI.Sim.Elevation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimElevation::RunTest(const FString& Parameters)
{
	// Fix-list item 18. The sim was XZ-only, so a wall blocked sight at every height and no
	// floor could separate two storeys -- the blocker for a multi-level house. The mechanism
	// is a per-solid vertical span plus a height-aware segment test.
	const float Eye = FHISim::EyeHeight;

	// ---- adding the axis changed NOTHING about the existing world.
	// Every nav-node pair must agree between the 2-D test and the height-aware one at eye
	// height. This is the regression harness for the elevation work: when a future session
	// gives a sofa its real height, this is the assertion that will notice.
	{
		const TArray<FVector2D>& Poses = FHISim::NodePositions();
		int32 Pairs = 0;
		int32 Disagreements = 0;
		for (int32 I = 0; I < Poses.Num(); ++I)
		{
			for (int32 J = I + 1; J < Poses.Num(); ++J)
			{
				++Pairs;
				const FVector A(Poses[I].X, Poses[I].Y, Eye);
				const FVector B(Poses[J].X, Poses[J].Y, Eye);
				if (FHISim::LosBlocked(Poses[I], Poses[J]) != FHISim::LosBlocked3D(A, B))
				{
					++Disagreements;
				}
			}
		}
		TestTrue(FString::Printf(TEXT("the height axis changed nothing at eye level (%d node pairs, %d disagreements)"),
			Pairs, Disagreements), Pairs > 0 && Disagreements == 0);
	}

	// ---- and a LOW obstacle really can be seen over, which is why the axis exists at all.
	{
		const TArray<FBox2D> One = { FBox2D(FVector2D(-1.0f, 0.0f), FVector2D(1.0f, 0.2f)) };
		const TArray<FHISim::FHISolidSpan> Low = { FHISim::FHISolidSpan{0.0f, 0.9f} };
		const TArray<FHISim::FHISolidSpan> Tall = { FHISim::FHISolidSpan{0.0f, 3.0f} };
		const FVector A(-3.0f, 0.1f, Eye);
		const FVector B(3.0f, 0.1f, Eye);

		TestFalse(FString::Printf(TEXT("a waist-high obstacle does not block eye-level sight (span 0.0-0.9 m, eyes at %.2f m)"),
			Eye), FHISim::LosBlocked3D(A, B, One, Low));
		TestTrue(TEXT("but a full-height one in the same place does"),
			FHISim::LosBlocked3D(A, B, One, Tall));
	}

	// ---- a FLOOR SLAB separates two storeys: the part of item 18 that blocks a multi-level
	// house. A slab is new structure rather than a re-description of old, so it needs no
	// invented data -- but it is only expressible now that solids have a span.
	{
		const TArray<FBox2D> Floor = { FBox2D(FVector2D(-6.0f, -6.0f), FVector2D(6.0f, 6.0f)) };
		const TArray<FHISim::FHISolidSpan> Slab = { FHISim::FHISolidSpan{2.9f, 3.1f} };
		const float Upper = 3.0f + Eye;

		TestTrue(TEXT("a floor slab blocks sight between storeys"),
			FHISim::LosBlocked3D(FVector(-2.0f, 0.0f, Eye), FVector(2.0f, 0.0f, Upper), Floor, Slab));

		// And must NOT block two people on the same storey -- a ceiling that hides the room
		// it roofs would be worse than no ceiling at all.
		TestFalse(TEXT("but does not block two people on the SAME storey, which would hide the room it roofs"),
			FHISim::LosBlocked3D(FVector(-4.0f, 0.0f, Eye), FVector(4.0f, 0.0f, Eye), Floor, Slab));
		TestFalse(TEXT("nor two people both on the floor above"),
			FHISim::LosBlocked3D(FVector(-4.0f, 0.0f, Upper), FVector(4.0f, 0.0f, Upper), Floor, Slab));
	}

	// ---- the entities carry the axis, and the spans stay in step with the solids
	{
		const FHISim::FHIPlayer P = FHISim::CreatePlayer();
		const FHISim::FHIThief T = FHISim::CreateThief();
		TestTrue(FString::Printf(TEXT("the homeowner and the intruder both carry a height, defaulting to the ground floor (%.1f, %.1f)"),
			P.Z, T.Z), P.Z == 0.0f && T.Z == 0.0f);
		TestEqual(FString::Printf(TEXT("and the solid spans are in step with the solids (%d vs %d)"),
			FHISim::SolidSpans().Num(), FHISim::Solids().Num()),
			FHISim::SolidSpans().Num(), FHISim::Solids().Num());
	}

	return true;
}

// ---------------------------------------------------------------------- the view

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimView,
	"HI.Sim.View",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimView::RunTest(const FString& Parameters)
{
	// The aim and chase-camera maths out of game.gd, where it was untestable for the same
	// reason the round rules were. The axis mapping is the thing worth asserting: Godot is
	// (x, y_up, z) and this port is (x, y, height), so a transposed mapping would put the
	// view a quarter turn out and no compiler would say a word.

	// ---- facing, at the two yaws that pin the convention down
	{
		const FVector Ahead = FHISim::AimDir(0.0f, 0.0f);
		TestTrue(FString::Printf(TEXT("yaw 0 looks along +Y, not up or sideways (%.3f, %.3f, %.3f)"),
			Ahead.X, Ahead.Y, Ahead.Z),
			FMath::IsNearlyEqual(Ahead.X, 0.0f, 1e-5f)
			&& FMath::IsNearlyEqual(Ahead.Y, 1.0f, 1e-5f)
			&& FMath::IsNearlyEqual(Ahead.Z, 0.0f, 1e-5f));

		const FVector Side = FHISim::AimDir(UE_PI * 0.5f, 0.0f);
		TestTrue(FString::Printf(TEXT("a quarter turn looks along +X (%.3f, %.3f, %.3f)"), Side.X, Side.Y, Side.Z),
			FMath::IsNearlyEqual(Side.X, 1.0f, 1e-5f)
			&& FMath::IsNearlyEqual(Side.Y, 0.0f, 1e-5f));
	}

	// ---- pitch: NEGATIVE is looking up, which is the sign that is easiest to get backwards
	{
		const FVector Up = FHISim::AimDir(0.0f, -0.30f);
		const FVector Down = FHISim::AimDir(0.0f, 0.30f);
		TestTrue(FString::Printf(TEXT("negative pitch looks UP (z=%.3f) and positive looks down (z=%.3f)"),
			Up.Z, Down.Z), Up.Z > 0.0f && Down.Z < 0.0f);

		// and it is clamped to the cone the aim scheme allows
		const FVector Past = FHISim::AimDir(0.0f, 99.0f);
		const FVector Limit = FHISim::AimDir(0.0f, FHISim::PitchMax);
		TestTrue(TEXT("pitch is clamped to the aim cone rather than wrapping over the top"),
			Past.Equals(Limit, 1e-5f));
	}

	// ---- always a unit vector, whatever the angles
	{
		bool bUnit = true;
		for (int32 I = 0; I < 24; ++I)
		{
			const float Yaw = static_cast<float>(I) * 0.7f - 8.0f;
			const float Pitch = static_cast<float>(I) * 0.13f - 1.5f;
			if (!FMath::IsNearlyEqual(FHISim::AimDir(Yaw, Pitch).Size(), 1.0f, 1e-4f))
			{
				bUnit = false;
			}
		}
		TestTrue(TEXT("AimDir is unit length across the whole cone"), bUnit);
	}

	// ---- the camera sits BEHIND the view and above, and NOT off to one side.
	//
	// The lateral term is the one that matters, and it is a deliberate divergence from
	// `game.gd`'s 0.42: the reticle is the aim direction projected into the CAMERA's basis, so
	// a side offset moves the man on screen while the crosshair stays in the middle -- "the
	// crosshair is at the side of the defender; it should be in front". Zero is the value that
	// puts the homeowner under his own crosshair, so zero is what is asserted.
	{
		const FVector View = FHISim::AimDir(0.0f, 0.0f);
		const FVector Offset = FHISim::CameraOffset(0.0f, 0.0f);
		TestTrue(FString::Printf(TEXT("at yaw 0 the camera is behind (-Y) and above (%.2f, %.2f, %.2f)"),
			Offset.X, Offset.Y, Offset.Z),
			FMath::IsNearlyEqual(Offset.Y, -FHISim::CamBack, 1e-5f)
			&& FMath::IsNearlyEqual(Offset.Z, FHISim::CamUp, 1e-5f));
		TestTrue(FString::Printf(TEXT("and has NO lateral offset, so he stands under the crosshair (X=%.3f)"),
			Offset.X), FMath::IsNearlyZero(Offset.X, 1e-5f));
		TestTrue(FString::Printf(TEXT("and it stays behind him as he turns (dot=%.2f)"), FVector::DotProduct(Offset, View)),
			FVector::DotProduct(Offset, View) < 0.0f);
	}

	return true;
}

// ------------------------------------------------------------- the player's verbs

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimGroupHeist,
	"HI.Sim.GroupHeist",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimGroupHeist::RunTest(const FString& Parameters)
{
	// A FULL HEIST THROUGH THE GROUP PATH. This test did not exist, and its absence is the
	// single largest hole the port has had.
	//
	// Every heist assertion in this file drove `StepThief` for one intruder. The game only
	// ever calls `StepIntruders`. Those two are not the same code path, and the difference
	// was not cosmetic: `StepIntruders` separated the positions captured BEFORE the step and
	// wrote the result back, so each intruder was returned to where he started every frame
	// and NONE OF THEM EVER MOVED. All 24 tests passed while the game was unplayable.
	//
	// It is the same shape of mistake the project's own notes warn about: "a test suite in
	// which every case holds the one varying actor still is not covering the system". Here
	// the varying thing was the CODE PATH, not an actor.

	const float Dt = 1.0f / 60.0f;

	// ---- one intruder through the group path must MOVE. This is the cheap, sharp assertion:
	// it fails in a way that says what is wrong, rather than as a heist that quietly times out.
	{
		FHISim::FHIPlayer Player = FHISim::CreatePlayer();
		TArray<FHISim::FHIThief> Intruders = FHISim::CreateIntruders(1);
		TArray<FHISim::FHILootItem> Loot = FHISim::CreateLoot();
		TArray<FHISim::FHIEvent> Events;
		const FVector2D Start = Intruders[0].Pos;

		for (int32 Step = 0; Step < 300; ++Step)   // five seconds: Think alone is 2.0 s
		{
			FHISim::StepIntruders(Intruders, Player, Loot, Events, Dt);
			Events.Reset();
		}
		const float Travelled = FVector2D::Distance(Start, Intruders[0].Pos);
		TestTrue(FString::Printf(TEXT("an intruder driven by StepIntruders actually MOVES (%.2f m in 5 s, from (%.2f, %.2f) to (%.2f, %.2f))"),
			Travelled, Start.X, Start.Y, Intruders[0].Pos.X, Intruders[0].Pos.Y),
			Travelled > 1.0f);
	}

	// ---- and the whole heist, three of them, against an idle homeowner
	{
		FHISim::FHIPlayer Player = FHISim::CreatePlayer();
		TArray<FHISim::FHIThief> Intruders = FHISim::CreateIntruders(3);
		TArray<FHISim::FHILootItem> Loot = FHISim::CreateLoot();
		TArray<FHISim::FHIEvent> Events;

		float T = 0.0f;
		int32 Sirens = 0;
		TSet<FString> Grabbed;
		TSet<FString> Delivered;
		// 400 s, which is deliberately far more than the single-thief spec's 180: three men
		// share one 2 m doorway and two of the three valuables, so they are slower and they
		// get in each other's way. A timeout here would mean a stall, not a slow heist.
		while (T < 400.0f && FHISim::Remaining(Loot) > 0)
		{
			FHISim::StepIntruders(Intruders, Player, Loot, Events, Dt);
			for (const FHISim::FHIEvent& Ev : Events)
			{
				if (Ev.Type == TEXT("sirens"))
				{
					++Sirens;
				}
				else if (Ev.Type == TEXT("grabbed"))
				{
					Grabbed.Add(Ev.Label);
				}
				else if (Ev.Type == TEXT("delivered"))
				{
					Delivered.Add(Ev.Label);
				}
			}
			Events.Reset();
			T += Dt;
		}

		const int32 Left = FHISim::Remaining(Loot);
		AddInfo(FString::Printf(TEXT("three intruders cleared %d/3 items in %.1fs, %d grabbed, %d delivered, %d sirens"),
			3 - Left, T, Grabbed.Num(), Delivered.Num(), Sirens));
		TestEqual(FString::Printf(TEXT("three intruders empty the house through the group path (delivered %d/3 in %.1fs)"),
			3 - Left, T), Left, 0);

		// The COUNT is asserted separately from the outcome, because "delivered 3/3" could
		// also be reached by the sim's own bookkeeping being wrong. Three distinct labels must
		// have been picked up and three distinct labels must have arrived, and those are
		// different events raised by different code.
		TestEqual(FString::Printf(TEXT("and all three valuables were individually carried (grabbed %d, delivered %d)"),
			Grabbed.Num(), Delivered.Num()), Delivered.Num(), 3);
		TestEqual(TEXT("by three different men or fewer, but each one a real grab"), Grabbed.Num(), 3);
	}

	return true;
}

// ------------------------------------------------------------- the player's verbs

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimStartView,
	"HI.Sim.StartView",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimStartView::RunTest(const FString& Parameters)
{
	// Where a round STARTS looking, which is a porting trap rather than a detail.
	//
	// Godot keeps two pitches: `player.pitch` (0.24, from create_player) and the game layer's
	// own `pitch` (0.03, set by reset()). The camera reads the GAME LAYER's, so the 0.24 is
	// vestigial in the original. This port merged the two fields because the sim now owns the
	// view -- which means the reset value has to be applied deliberately, and nothing about
	// `create_player` would ever tell you so.
	//
	// It cost a render: the first captured frame of this port was the top of the homeowner's
	// head, because a 0.24 rad start is 13.7 degrees down at the floor.

	// create_player still carries the literal from sim.gd -- changing it would be changing
	// the SPEC rather than matching the game, and the two really are different numbers.
	TestTrue(FString::Printf(TEXT("create_player keeps sim.gd's 0.24 rad (%.2f)"),
		FHISim::CreatePlayer().Pitch),
		FMath::IsNearlyEqual(FHISim::CreatePlayer().Pitch, 0.24f, 1e-4f));

	// ...but a ROUND starts at game.gd reset()'s 0.03, whatever the number of intruders.
	for (int32 Count = 1; Count <= 3; ++Count)
	{
		FHISim::FHIRound R = FHISim::FHIRound::Start(Count);
		TestTrue(FString::Printf(TEXT("and a %d-intruder round starts level, at reset()'s 0.03 rad (%.3f)"),
			Count, R.Player.Pitch),
			FMath::IsNearlyEqual(R.Player.Pitch, 0.03f, 1e-4f));
	}

	// And the difference is visible, which is why it is worth pinning: it is nearly fourteen
	// degrees of view, not a rounding difference.
	TestTrue(TEXT("which is a materially different view from the spawn default"),
		FMath::Abs(FHISim::CreatePlayer().Pitch - FHISim::FHIRound::Start(1).Player.Pitch) > 0.2f);

	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimNonLethal,
	"HI.Sim.NonLethal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimNonLethal::RunTest(const FString& Parameters)
{
	// Fix-list item 17's remaining half: "no locking/barricading, no lights, no calling out,
	// no calling police deliberately". Two of those four are here, and the pair is chosen
	// because both are the SAME KIND of thing the rest of the game is not: they can end an
	// intrusion without killing anybody.
	//
	// The assertion that matters most here is the NEGATIVE one -- that a shout does NOT start
	// the police clock. Without it the shout is just a second way to do what the first
	// gunshot already does, and the item would be closed on a synonym.

	const float Dt = 1.0f / 60.0f;

	// ---- calling the police is deliberate, immediate, and idempotent
	{
		FHISim::FHIRound R = FHISim::FHIRound::Start(3);
		for (const FHISim::FHIThief& T : R.Intruders)
		{
			TestTrue(TEXT("nobody has called the police before the round starts"), T.Alarm < 0.0f);
		}

		FHISim::FHIInput In;
		In.bCallPolice = true;
		R.Step(In, Dt);

		int32 Alarmed = 0;
		for (const FHISim::FHIThief& T : R.Intruders)
		{
			if (T.Alarm >= 0.0f)
			{
				++Alarmed;
			}
		}
		TestEqual(FString::Printf(TEXT("and [P] starts the clock for every man standing (%d of %d)"),
			Alarmed, R.Intruders.Num()), Alarmed, R.Intruders.Num());

		// Idempotent. A second call that RE-ZEROED the clock would make the sirens arrive
		// sooner for being told twice; that is the bug this guards, not a tidy return value.
		const float ClockBefore = R.Intruders[0].Alarm;
		TestFalse(TEXT("a second call reports that it did nothing"), R.CallPolice());
		TestTrue(FString::Printf(TEXT("and does not reset the clock (%.2f -> %.2f)"),
			ClockBefore, R.Intruders[0].Alarm),
			R.Intruders[0].Alarm >= ClockBefore);
	}

	// ---- shouting frightens them, marks your position, and does NOT call the police
	{
		FHISim::FHIRound R = FHISim::FHIRound::Start(3);
		R.Player.Pos = FVector2D(0.0f, -11.0f);
		for (int32 Idx = 0; Idx < R.Intruders.Num(); ++Idx)
		{
			R.Intruders[Idx].Pos = FVector2D(0.0f, 10.0f + Idx);
			R.Intruders[Idx].Fear = 0.0f;
			R.Intruders[Idx].Alarm = -1.0f;
		}

		FHISim::FHIInput In;
		In.bShout = true;
		R.Step(In, Dt);

		int32 Frightened = 0;
		int32 Alarmed = 0;
		bool bAllKnowWhereItCameFrom = true;
		for (const FHISim::FHIThief& T : R.Intruders)
		{
			if (T.Fear > 0.0f)
			{
				++Frightened;
			}
			if (T.Alarm >= 0.0f)
			{
				++Alarmed;
			}
			if (FVector2D::Distance(T.LastSeen, R.Player.Pos) > 0.01f)
			{
				bAllKnowWhereItCameFrom = false;
			}
		}
		TestEqual(FString::Printf(TEXT("a shout is heard by every man in the house (%d of %d)"),
			Frightened, R.Intruders.Num()), Frightened, R.Intruders.Num());
		TestTrue(TEXT("and it tells them where it came from"), bAllKnowWhereItCameFrom);
		TestEqual(FString::Printf(TEXT("but a man shouting in his own hall does NOT call the police (%d alarmed)"),
			Alarmed), Alarmed, 0);

		// The cooldown, which is what stops fear being piled on frame after frame. Asserted as
		// "it did not go UP", not as "it stayed the same": fear DECAYS every frame
		// (FearDecay), so a held key with no cooldown would show ~1.96 here and a working
		// cooldown shows slightly less than 0.98 as the decay takes its bite.
		const float FearAfterOne = R.Intruders[0].Fear;
		R.Step(In, Dt);
		TestTrue(FString::Printf(TEXT("and it cannot be spammed (fear %.2f then %.2f -- it decayed rather than stacking)"),
			FearAfterOne, R.Intruders[0].Fear),
			R.Intruders[0].Fear <= FearAfterOne + 1e-4f);
		TestTrue(FString::Printf(TEXT("which is a real bound: an uncapped shout would have reached about %.2f"),
			FearAfterOne * 2.0f),
			R.Intruders[0].Fear < FearAfterOne * 1.5f);

		// Enough of them and he breaks off, which is the point: FearFlee ends an intrusion
		// without a corpse.
		int32 Shouts = 0;
		while (R.Intruders[2].Fear < FHISim::FearFlee && Shouts < 40)
		{
			R.Player.ShoutCd = 0.0f;
			R.Shout();
			++Shouts;
		}
		TestTrue(FString::Printf(TEXT("and repeated shouting eventually frightens a man off (fear %.2f after %d shouts, flee at %.1f)"),
			R.Intruders[2].Fear, Shouts, FHISim::FearFlee),
			R.Intruders[2].Fear >= FHISim::FearFlee);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimVerbs,
	"HI.Sim.Verbs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimVerbs::RunTest(const FString& Parameters)
{
	// The three things the homeowner can DO that this port had never wired to a key.
	//
	// HI.Sim.Shove already proves the shove FUNCTION works. It proved nothing about whether
	// the round ever calls it, and it did not -- so a round could be played with no shove,
	// no reload and no weapon switch, and every test stayed green. These assertions go
	// through FHIRound::Step, which is the layer that was missing, because that is the
	// layer where the gap was invisible.

	const float Dt = 1.0f / 60.0f;

	// A round whose intruder cannot interfere. The player-verb assertions below are about
	// KEYS, and they take tens of frames; a firefight that ended the round part-way through
	// a reload would fail them for a reason that has nothing to do with what they test.
	// Setting `bAlive` directly rather than damaging him is deliberate: DamageIntruder would
	// decide the round and freeze Step, which is the opposite of what is wanted here.
	auto Isolated = []()
	{
		FHISim::FHIRound R = FHISim::FHIRound::Start(1);
		R.Intruders[0].bAlive = false;
		return R;
	};

	// ---- [R] actually starts a reload, and finishes it out of the reserve
	{
		FHISim::FHIRound R = Isolated();
		R.Player.Mag = 0;
		R.Player.Cd = 0.0f;

		FHISim::FHIInput Dry;   // control: no key held
		R.Step(Dry, Dt);
		TestTrue(FString::Printf(TEXT("a dry pistol stays dry with no key held (mag %d, reloading %.2f)"),
			R.Player.Mag, R.Player.Reloading),
			R.Player.Mag == 0 && R.Player.Reloading <= 0.0f);

		FHISim::FHIInput In;
		In.bReload = true;
		R.Step(In, Dt);
		TestTrue(FString::Printf(TEXT("and [R] starts one (reloading %.2f s of %.2f)"),
			R.Player.Reloading, FHISim::Weapon(TEXT("pistol")).Reload),
			R.Player.Reloading > 0.0f);

		// Hold the key down for the whole reload. A naive implementation restarts the timer
		// every frame, so a held [R] freezes him mid-magazine forever and never finishes.
		int32 Steps = 0;
		while (R.Player.Reloading > 0.0f && Steps < 600)
		{
			R.Step(In, Dt);
			++Steps;
		}
		const int32* Reserve = R.Player.Reserve.Find(TEXT("pistol"));
		TestTrue(FString::Printf(TEXT("holding [R] finishes the reload rather than restarting it (%d frames, mag %d, reserve %d)"),
			Steps, R.Player.Mag, Reserve != nullptr ? *Reserve : -1),
			Steps < 600 && R.Player.Mag == 12 && Reserve != nullptr && *Reserve == 24);
	}

	// ---- and a full magazine cannot be reloaded at all
	{
		FHISim::FHIRound R = Isolated();
		FHISim::FHIInput In;
		In.bReload = true;
		R.Step(In, Dt);
		TestTrue(FString::Printf(TEXT("a full magazine is not reloadable (mag %d, reloading %.2f)"),
			R.Player.Mag, R.Player.Reloading),
			R.Player.Mag == 12 && R.Player.Reloading <= 0.0f);
	}

	// ---- [1] / [2] switch weapons, and CRUCIALLY each keeps its own magazine
	{
		FHISim::FHIRound R = Isolated();

		FHISim::FHIInput ToShotgun;
		ToShotgun.SwitchWeapon = TEXT("shotgun");
		R.Step(ToShotgun, Dt);
		TestTrue(FString::Printf(TEXT("the shotgun is reachable at all (holding '%s' with %d in it)"),
			*R.Player.Weapon, R.Player.Mag),
			R.Player.Weapon == TEXT("shotgun") && R.Player.Mag == 6);

		// Fire one, then go back to the pistol: the pistol must still hold what it held.
		FHISim::FHIInput Fire;
		Fire.bFire = true;
		R.Player.Cd = 0.0f;
		R.Step(Fire, Dt);
		TestTrue(FString::Printf(TEXT("and it fires (shotgun mag now %d)"), R.Player.Mag), R.Player.Mag == 5);

		FHISim::FHIInput ToPistol;
		ToPistol.SwitchWeapon = TEXT("pistol");
		R.Step(ToPistol, Dt);
		TestTrue(FString::Printf(TEXT("switching back does NOT conjure a fresh magazine (pistol mag %d, not 12)"),
			R.Player.Mag),
			R.Player.Weapon == TEXT("pistol") && R.Player.Mag == 12);

		// Spend a pistol round, swap away and back, and the spent round must stay spent.
		R.Player.Cd = 0.0f;
		R.Step(Fire, Dt);
		const int32 AfterFiring = R.Player.Mag;
		R.Player.Cd = 0.0f;
		R.Step(ToShotgun, Dt);
		R.Step(ToPistol, Dt);
		TestTrue(FString::Printf(TEXT("and a spent round stays spent across a swap (fired to %d, back holding %d)"),
			AfterFiring, R.Player.Mag),
			AfterFiring == 11 && R.Player.Mag == 11);

		// Re-selecting what he is already holding must not bank or reload anything.
		R.Step(ToPistol, Dt);
		TestTrue(FString::Printf(TEXT("re-selecting the held weapon is a no-op (mag %d)"), R.Player.Mag),
			R.Player.Mag == 11);

		// A typo must not leave him holding a weapon that cannot fire.
		FHISim::FHIInput ToGarbage;
		ToGarbage.SwitchWeapon = TEXT("rocket");
		R.Step(ToGarbage, Dt);
		TestTrue(FString::Printf(TEXT("an unknown weapon is refused rather than emptying his hands (holding '%s')"),
			*R.Player.Weapon),
			R.Player.Weapon == TEXT("pistol"));
	}

	// ---- firing the shotgun really does report pellets, through the round
	{
		FHISim::FHIRound R = Isolated();
		FHISim::FHIInput In;
		In.SwitchWeapon = TEXT("shotgun");
		R.Step(In, Dt);
		R.ConsumeEvents();
		R.Player.Cd = 0.0f;
		In.SwitchWeapon.Empty();
		In.bFire = true;
		R.Step(In, Dt);

		int32 Pellets = -1;
		float Damage = -1.0f;
		for (const FHISim::FHIEvent& Ev : R.ConsumeEvents())
		{
			if (Ev.Type == TEXT("playerShot"))
			{
				Pellets = Ev.Pellets;
				Damage = Ev.Damage;
			}
		}
		TestTrue(FString::Printf(TEXT("and the shot it reports carries the shotgun's pellets (%d) and damage (%.1f)"),
			Pellets, Damage),
			Pellets == FHISim::Weapon(TEXT("shotgun")).Pellets && Pellets > 1
			&& FMath::IsNearlyEqual(Damage, FHISim::Weapon(TEXT("shotgun")).Damage, 0.01f));
	}

	// ---- [F] shoves, and it is the KEY that does it
	{
		FHISim::FHIRound R = FHISim::FHIRound::Start(1);
		// Stand him a metre in front of the homeowner, well inside ShoveRange.
		R.Player.Pos = FVector2D(0.0f, -11.0f);
		R.Player.Yaw = 0.0f;                     // yaw 0 faces +Y
		R.Intruders[0].Pos = FVector2D(0.0f, -10.0f);
		R.Intruders[0].Carry = TEXT("Laptop");
		R.Intruders[0].Yaw = UE_PI;

		// Control first: with no key held he must be neither staggered nor relieved of it.
		// Without this the assertions below would pass on a build that shoved unconditionally.
		FHISim::FHIInput None;
		const float GuardedY = R.Intruders[0].Pos.Y;
		R.Step(None, Dt);
		TestTrue(FString::Printf(TEXT("with no key held [F]'s effect does not happen (stagger %.2f, carry '%s')"),
			R.Intruders[0].Stagger, *R.Intruders[0].Carry),
			R.Intruders[0].Stagger <= 0.0f && R.Intruders[0].Carry == TEXT("Laptop"));

		FHISim::FHIInput ShoveIn;
		ShoveIn.bShove = true;
		R.Intruders[0].Pos = FVector2D(0.0f, -10.0f);
		R.Step(ShoveIn, Dt);
		TestTrue(FString::Printf(TEXT("and holding it lands one (stagger %.2f s, driven from %.2f to %.2f)"),
			R.Intruders[0].Stagger, GuardedY, R.Intruders[0].Pos.Y),
			R.Intruders[0].Stagger > 0.0f && R.Intruders[0].Pos.Y > GuardedY);
		TestTrue(FString::Printf(TEXT("knocking the loot loose (carry now '%s')"), *R.Intruders[0].Carry),
			R.Intruders[0].Carry.IsEmpty());
	}

	return true;
}

// ------------------------------------------------- vertical slice: the floor model
//
// The 2026-10-15 fork gave the house a second storey. These tests pin the slice of
// HOUSE-DESIGN.md §3 AS BUILT: the stair band's geometry (x 6.60..7.875,
// y -13.50..-9.10, bottom h 0 at the south end, top h 3.0 at the north), the slab
// hole (x 6.35..7.875, y -13.50..-10.50), the railings (span [3.0, 4.0]) and the
// stringer (span [0, 3.0], x 6.50..6.62, y -13.875..-9.1).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimFloorModel,
	"HI.Sim.FloorModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimFloorModel::RunTest(const FString& Parameters)
{
	FHISim::SelfCheck();

	// ---- FloorZAt: the band interpolates, flat floors are flat.
	TestTrue(TEXT("band bottom is floor height"),
		FMath::IsNearlyEqual(FHISim::FloorZAt(FVector2D(7.24f, -9.10f), 0), 0.0f, 0.01f));
	// The exact top corner: FBox2D::IsInside is STRICT in UE -- this returned the flat
	// floor height (3.0 by luck there, 0.0 at the bottom edge) until Contains moved to
	// IsInsideOrOn, and SelfCheck v2's arrival assertion caught it on its first run.
	TestTrue(TEXT("band top is floor height (inclusive edge)"),
		FMath::IsNearlyEqual(FHISim::FloorZAt(FVector2D(7.24f, -13.50f), 1), 3.0f, 0.01f));
	TestTrue(TEXT("band interpolates linearly"),
		FMath::IsNearlyEqual(FHISim::FloorZAt(FVector2D(7.24f, -11.30f), 0), 1.5f, 0.02f));
	TestTrue(TEXT("flat ground floor is 0"),
		FMath::IsNearlyEqual(FHISim::FloorZAt(FVector2D(0.0f, -11.0f), 0), 0.0f, 0.001f));
	TestTrue(TEXT("flat second floor is one storey"),
		FMath::IsNearlyEqual(FHISim::FloorZAt(FVector2D(0.0f, -11.0f), 1), 3.0f, 0.001f));
	TestEqual(TEXT("FloorHeight is linear in storeys (basement becomes a table in phase 2)"),
		FHISim::FloorHeight(2), 6.0f);

	// ---- ApplyElevation: derived, never integrated -- and the flip is at the midpoint.
	{
		int32 F = 0; float Z = 0.0f;
		FHISim::ApplyElevation(FVector2D(7.24f, -11.29f), F, Z);
		TestEqual(TEXT("just south of the midpoint stays on the low floor"), F, 0);
		TestTrue(TEXT("and carries the band height"), FMath::IsNearlyEqual(Z, 1.493f, 0.02f));
		F = 0; Z = 0.0f;
		FHISim::ApplyElevation(FVector2D(7.24f, -11.31f), F, Z);
		TestEqual(TEXT("just north of the midpoint is the high floor"), F, 1);
		TestTrue(TEXT("and carries the band height"), FMath::IsNearlyEqual(Z, 1.507f, 0.02f));
		// The foot, from BELOW: a ground-floor body on the bottom tread derives the band.
		F = 0; Z = 3.0f;
		FHISim::ApplyElevation(FVector2D(7.24f, -9.11f), F, Z);
		TestEqual(TEXT("a ground body at the foot stays on the low floor"), F, 0);
		TestTrue(TEXT("at foot height"), FMath::IsNearlyEqual(Z, 0.007f, 0.02f));
		// ...and from ABOVE: the south deck strip covers this same plan point -- bottom
		// tread underneath, second-floor decking over it -- and DECK BEATS BAND for a
		// floor-1 body. This pin said "returns the low floor" until 2026-10-15, encoding
		// band-wins; the deck-B overlap made that a teleport, dropping a floor-1 man
		// walking north on the strip 2 m onto the treads mid-stride.
		F = 1; Z = 0.0f;
		FHISim::ApplyElevation(FVector2D(7.24f, -9.11f), F, Z);
		TestEqual(TEXT("a second-floor body at the foot stands on the deck strip"), F, 1);
		TestTrue(TEXT("at storey height, not tread height"), FMath::IsNearlyEqual(Z, 3.0f, 0.001f));
		// The same two worlds 1.2 m up the run, where the deck is 2.2 m over the treads.
		F = 0; Z = 3.0f;
		FHISim::ApplyElevation(FVector2D(7.24f, -10.3f), F, Z);
		TestTrue(FString::Printf(TEXT("under the strip, a ground body is on the treads (got %.2f)"), Z),
			F == 0 && FMath::IsNearlyEqual(Z, 0.82f, 0.02f));
		F = 1; Z = 0.0f;
		FHISim::ApplyElevation(FVector2D(7.24f, -10.3f), F, Z);
		TestTrue(TEXT("on the strip, the deck holds him"),
			F == 1 && FMath::IsNearlyEqual(Z, 3.0f, 0.001f));
		F = 1; Z = 0.0f;
		FHISim::ApplyElevation(FVector2D(6.0f, -12.0f), F, Z);
		TestTrue(TEXT("the deck holds a second-floor body at storey height"),
			FMath::IsNearlyEqual(Z, 3.0f, 0.001f) && F == 1);
	}

	// ---- The flight climbs monotonically: 0.05 m samples from foot to top.
	{
		float Prev = -1.0f;
		bool bMono = true;
		for (float Y = -9.10f; Y >= -13.50f; Y -= 0.05f)
		{
			const float H = FHISim::FloorZAt(FVector2D(7.24f, Y), 0);
			if (H < Prev - 1e-4f) { bMono = false; }
			Prev = H;
		}
		TestTrue(TEXT("height never decreases walking up the flight"), bMono);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimStairCollision,
	"HI.Sim.StairCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimStairCollision::RunTest(const FString& Parameters)
{
	FHISim::SelfCheck();

	// ---- ONE rule, both floors: the body cylinder [Z, Z+1.8] must STRICTLY overlap a
	// solid's span for it to block. At ground Z the stringer (span [0,3]) walls the
	// flight off from the kitchen; at Z=3.0 the same body steps over the stringer and
	// the railing (span [3,4]) catches it instead.
	{
		const FVector2D P(6.70f, -11.0f);
		const FVector2D AtGround = FHISim::ResolveCircleZ(P, 0.42f, 0.0f);
		TestTrue(FString::Printf(TEXT("the stringer walls off the flight at ground level (got %.2f)"), AtGround.X),
			FMath::IsNearlyEqual(AtGround.X, 7.04f, 0.01f));
		// The Z=3 body here floats over the hole -- unreachable on foot, which is why
		// it pins the RULE, not a route: at deck height the railings apply even in-band
		// (a man on the south deck strip stands above the flight's foot, inside the same
		// footprint, and must not walk off the hole's edge).
		const FVector2D Upstairs = FHISim::ResolveCircleZ(P, 0.42f, 3.0f);
		TestTrue(FString::Printf(TEXT("at Z=3 the stringer is stepped over and the railing blocks (got %.2f)"), Upstairs.X),
			FMath::IsNearlyEqual(Upstairs.X, 6.77f, 0.01f));
		// Below deck height, IN the band, the railings do not apply -- the flight owns
		// the body. Without this the south railing pinned a descending player at Z=1.31
		// (measured, 2026-10-15): his cylinder tip reached into the [3,4] span while
		// his feet were 1.8 m below the deck, and the push-out equilibrium held him
		// exactly one radius north of it, the stuck detector silent.
		const FVector2D OnFlight(7.24f, -10.75f);
		TestTrue(TEXT("a mid-flight body passes under the south railing"),
			FHISim::ResolveCircleZ(OnFlight, 0.42f, 1.12f).Equals(OnFlight, 0.001f));
	}
	{
		const FVector2D West(6.0f, -11.5f);
		const FVector2D R = FHISim::ResolveCircleZ(West, 0.42f, 3.0f);
		TestTrue(FString::Printf(TEXT("the railing's west face pushes a deck body back (got %.2f)"), R.X),
			FMath::IsNearlyEqual(R.X, 5.83f, 0.01f));
	}

	// ---- Decks never block: standing under the slab is legal (it is the kitchen), and
	// the fridge underfoot on the ground floor cannot shove a man on the second.
	{
		const FVector2D Under(7.24f, -10.0f);
		TestTrue(TEXT("ground floor under the slab is free"),
			FHISim::ResolveCircleZ(Under, 0.42f, 0.0f).Equals(Under, 0.001f));
		const FVector2D OverFridge(5.5f, -13.45f);
		TestTrue(TEXT("ground furniture does not touch a second-floor body"),
			FHISim::ResolveCircleZ(OverFridge, 0.42f, 3.0f).Equals(OverFridge, 0.001f));
		TestTrue(TEXT("...but it does touch a ground-floor body"),
			!FHISim::ResolveCircleZ(OverFridge, 0.42f, 0.0f).Equals(OverFridge, 0.01f));
	}

	// ---- Second-floor walls exist only at second-floor height. The probe sits in the
	// hall gap, where the ground floor is OPEN (the divider's doorway) and the floor-1
	// north wall above it is not -- an east-wall version of this pin would have been
	// blocked on both floors by the ground exterior wall and proven nothing.
	{
		const FVector2D Gap(0.0f, -13.80f);
		const FVector2D R = FHISim::ResolveCircleZ(Gap, 0.42f, 3.0f);
		TestTrue(FString::Printf(TEXT("the floor-1 north wall stops a deck body (got %.2f)"), R.Y),
			FMath::IsNearlyEqual(R.Y, -13.455f, 0.01f));
		TestTrue(TEXT("...and the hall gap is air to the ground floor"),
			FHISim::ResolveCircleZ(Gap, 0.42f, 0.0f).Equals(Gap, 0.001f));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimCrossFloorSight,
	"HI.Sim.CrossFloorSight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimCrossFloorSight::RunTest(const FString& Parameters)
{
	FHISim::SelfCheck();

	// Positions carry FEET Z; CanSee3D raises the eyes itself.

	// ---- THE HEADLINE: the stairwell is a sight tube. The ground-floor eyes see the
	// second-floor body through the slab hole, over the railing's span, past the
	// stringer's top corner. The conservative version of LosBlocked3D (clamp-into-span,
	// then a full-length 2-D test) false-blocked exactly this -- the exact t-interval
	// clip is what makes the flight playable.
	TestTrue(TEXT("the stairwell is a sight tube (foot of stairs sees the header landing)"),
		FHISim::CanSee3D(FVector(7.24f, -9.6f, 0.0f), FVector(7.24f, -13.6f, 3.0f)));
	TestTrue(TEXT("sight down the stairwell is symmetric"),
		FHISim::CanSee3D(FVector(7.24f, -13.6f, 3.0f), FVector(7.24f, -9.6f, 0.0f)));

	// ---- The same plan line at the same heights, ONE METRE WEST: through the slab
	// instead of the hole. Blocked. This pair is the regression pin for span-exactness.
	// x=6.1, NOT 6.0: the counter (x up to 6.05) blocks the 6.0 line at ground height,
	// so that version passed with the slab logic deleted -- a check that cannot fail.
	TestFalse(TEXT("the slab hides the second floor from the kitchen"),
		FHISim::CanSee3D(FVector(6.1f, -11.0f, 0.0f), FVector(6.1f, -13.0f, 3.0f)));

	// ---- Floor discipline on one plan line: the stringer (span [0,3]) blocks
	// ground-floor eyes at x=5.0..7.24 south-bound... the pair below looks along the
	// SAME plan segment at two heights and gets opposite answers.
	TestFalse(TEXT("the stringer blocks ground-floor sight along the flight"),
		FHISim::CanSee3D(FVector(5.0f, -11.5f, 0.0f), FVector(7.24f, -13.6f, 0.0f)));
	TestTrue(TEXT("second-floor eyes (4.62) see over the 1 m railing along the same line"),
		FHISim::CanSee3D(FVector(5.0f, -11.5f, 3.0f), FVector(7.24f, -13.6f, 3.0f)));

	// ---- Range is 3-D: two metres of storey counts against the pistol range.
	TestFalse(TEXT("CanSee3D range is three-dimensional"),
		FHISim::CanSee3D(FVector(0.0f, 5.0f, 0.0f), FVector(0.0f, 35.0f, 0.0f), 28.0f));

	// ---- ThiefSees3D: CanSee3D plus the plan cone. North is -PI/2 in atan2 terms.
	const float North = -0.5f * PI;
	TestTrue(TEXT("the thief at the foot of the stairs sees the man on the landing"),
		FHISim::ThiefSees3D(FVector(7.24f, -9.6f, 0.0f), North, FVector(7.24f, -13.6f, 3.0f)));
	TestFalse(TEXT("...but not with his back turned"),
		FHISim::ThiefSees3D(FVector(7.24f, -9.6f, 0.0f), North + PI, FVector(7.24f, -13.6f, 3.0f)));
	TestFalse(TEXT("the slab still hides the second floor from the thief's cone"),
		FHISim::ThiefSees3D(FVector(6.1f, -11.0f, 0.0f), North, FVector(6.1f, -13.0f, 3.0f)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimNodeFloors,
	"HI.Sim.NodeFloors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimNodeFloors::RunTest(const FString& Parameters)
{
	FHISim::SelfCheck();

	const TArray<FString>& Names = FHISim::NodeNames();
	const int32 IFoot = Names.IndexOfByKey(TEXT("stairFootG"));
	const int32 IFootL = Names.IndexOfByKey(TEXT("stairFootL"));
	const int32 ITop = Names.IndexOfByKey(TEXT("stairTopG"));
	const int32 IUp = Names.IndexOfByKey(TEXT("upstairs"));
	const int32 IKitchen = Names.IndexOfByKey(TEXT("kitchen"));
	TestTrue(TEXT("the slice's five stair nodes exist"),
		IFoot != INDEX_NONE && IFootL != INDEX_NONE && ITop != INDEX_NONE && IUp != INDEX_NONE);
	if (IFoot == INDEX_NONE || IFootL == INDEX_NONE || ITop == INDEX_NONE || IUp == INDEX_NONE || IKitchen == INDEX_NONE)
	{
		return false;
	}
	TestEqual(TEXT("stairFootG is on the ground floor"), FHISim::NodeFloors()[IFoot], 0);
	TestEqual(TEXT("stairFootL is on the ground floor"), FHISim::NodeFloors()[IFootL], 0);
	TestFalse(TEXT("stairFootL is out of the band -- the leg out of the flight turns west only AFTER this"),
		FHISim::InStairBand(FHISim::NodePositions()[IFootL]));
	TestEqual(TEXT("stairTopG is on the second floor"), FHISim::NodeFloors()[ITop], 1);
	TestEqual(TEXT("upstairs is on the second floor"), FHISim::NodeFloors()[IUp], 1);

	TestTrue(TEXT("the flight's graph edge IS the floor change"),
		FHISim::IsStairEdge(TEXT("stairFootG"), TEXT("stairTopG"))
		&& FHISim::IsStairEdge(TEXT("stairTopG"), TEXT("stairFootG")));
	TestFalse(TEXT("kitchen -> stairFootG walks flat"),
		FHISim::IsStairEdge(TEXT("kitchen"), TEXT("stairFootG")));
	TestFalse(TEXT("stairTopG -> upstairs walks flat"),
		FHISim::IsStairEdge(TEXT("stairTopG"), TEXT("upstairs")));

	// ---- NearestNode is floor-filtered. The single-argument form means GROUND FLOOR:
	// the day it briefly delegated INDEX_NONE (any floor), a point in the front hall
	// snapped to `upstairs` -- plan distance 0.0 to a node three metres overhead.
	TestEqual(TEXT("single-arg NearestNode is ground-floor semantics"),
		FHISim::NearestNode(FVector2D(0.0f, -11.2f)), FString(TEXT("frontHub")));
	TestEqual(TEXT("floor 0 filters to frontHub"),
		FHISim::NearestNode(FVector2D(0.0f, -11.2f), 0), FString(TEXT("frontHub")));
	TestEqual(TEXT("floor 1 filters to upstairs at the same plan point"),
		FHISim::NearestNode(FVector2D(0.0f, -11.2f), 1), FString(TEXT("upstairs")));
	TestEqual(TEXT("the upstairs room node owns its floor"),
		FHISim::NearestNode(FVector2D(0.0f, -12.2f), 1), FString(TEXT("upstairs")));

	// ---- The path to the second floor goes through the flight, in order.
	// PathNodes excludes the start node (RouteTo semantics: the route you WALK).
	const TArray<FString> Path = FHISim::PathNodes(TEXT("van"), TEXT("upstairs"));
	TestEqual(TEXT("the path's first hop from the van is the door"),
		Path[0], FString(TEXT("doorOut")));
	TestEqual(TEXT("the path ends upstairs"), Path.Last(), FString(TEXT("upstairs")));
	const int32 A = Path.IndexOfByKey(TEXT("stairFootG"));
	const int32 B = Path.IndexOfByKey(TEXT("stairTopG"));
	TestTrue(FString::Printf(TEXT("the flight is one hop, foot then top (A=%d B=%d)"), A, B),
		A != INDEX_NONE && B == A + 1);
	TestTrue(FString::Printf(TEXT("and the foot landing is the hop before the foot (A=%d)"), A),
		A > 0 && Path[A - 1] == TEXT("stairFootL"));
	const int32 C = Path.IndexOfByKey(TEXT("stairLanding"));
	TestTrue(FString::Printf(TEXT("then the step-off landing, then the room (B=%d C=%d)"), B, C),
		C == B + 1 && Path[C + 1] == TEXT("upstairs"));

	// ---- WalkBlocked: per-floor walkability, the world-data contract.
	TestFalse(TEXT("kitchen -> stairFootL is clear on floor 0 (counter moved for this)"),
		FHISim::WalkBlocked(FVector2D(4.5f, -9.6f), FVector2D(7.24f, -8.60f), 0));
	TestFalse(TEXT("stairFootL -> stairFootG is clear on floor 0 (the leg straight out of the band)"),
		FHISim::WalkBlocked(FVector2D(7.24f, -8.60f), FVector2D(7.24f, -8.85f), 0));
	TestFalse(TEXT("stairTopG -> upstairs is clear on floor 1 (node moved for this)"),
		FHISim::WalkBlocked(FVector2D(7.24f, -13.30f), FVector2D(0.0f, -12.20f), 1));
	TestFalse(TEXT("stairTopG -> stairLanding is clear on floor 1 (through the step-off gap)"),
		FHISim::WalkBlocked(FVector2D(7.24f, -13.30f), FVector2D(6.0f, -13.35f), 1));
	TestFalse(TEXT("stairLanding -> upstairs is clear on floor 1"),
		FHISim::WalkBlocked(FVector2D(6.0f, -13.35f), FVector2D(0.0f, -12.2f), 1));
	TestTrue(TEXT("the OLD upstairs node line crossed the west railing on floor 1"),
		FHISim::WalkBlocked(FVector2D(7.24f, -13.30f), FVector2D(0.0f, -11.00f), 1));
	TestTrue(TEXT("the flight's plan crosses the south railing at floor-1 height..."),
		FHISim::WalkBlocked(FVector2D(7.24f, -8.85f), FVector2D(7.24f, -13.30f), 1));
	TestFalse(TEXT("...which is why stair edges are walk-tested on their LOW floor"),
		FHISim::WalkBlocked(FVector2D(7.24f, -8.85f), FVector2D(7.24f, -13.30f), 0));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimSeparationZ,
	"HI.Sim.SeparationZ",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimSeparationZ::RunTest(const FString& Parameters)
{
	FHISim::SelfCheck();

	// ---- Legacy behaviour stands: overlapping bodies on one floor are separated.
	{
		const TArray<FVector2D> R = FHISim::SeparateBodiesZ(
			FVector2D(0.0f, -10.4f), 0.0f, FVector2D(0.0f, -10.0f), 0.0f);
		TestTrue(FString::Printf(TEXT("same-floor overlap separates to body distance (got %.2f)"),
			FVector2D::Distance(R[0], R[1])),
			FMath::IsNearlyEqual(FVector2D::Distance(R[0], R[1]), 0.84f, 0.01f));
	}

	// ---- Through a slab: the same plan point, one storey apart -- they cannot touch.
	{
		const TArray<FVector2D> R = FHISim::SeparateBodiesZ(
			FVector2D(5.0f, -11.0f), 3.0f, FVector2D(5.0f, -11.0f), 0.0f);
		TestTrue(TEXT("bodies on different storeys do not interact"),
			R[0].Equals(FVector2D(5.0f, -11.0f), 0.001f) && R[1].Equals(FVector2D(5.0f, -11.0f), 0.001f));
	}

	// ---- On the flight: single file. Both bodies inside the band, overlapping in plan,
	// 0.2 m apart in Z -- WITHOUT the stair skip they would be shoved sideways off the
	// treads; with it, they are untouched.
	{
		const TArray<FVector2D> R = FHISim::SeparateBodiesZ(
			FVector2D(7.24f, -11.0f), 1.30f, FVector2D(7.24f, -11.3f), 1.50f);
		TestTrue(TEXT("no separation inside the stair band"),
			R[0].Equals(FVector2D(7.24f, -11.0f), 0.001f) && R[1].Equals(FVector2D(7.24f, -11.3f), 0.001f));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimCrossFloorVerbs,
	"HI.Sim.CrossFloorVerbs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimCrossFloorVerbs::RunTest(const FString& Parameters)
{
	FHISim::SelfCheck();

	// ---- A shove is a 3-D reach test: the man directly overhead is 0 m away in plan
	// and three metres away in fact. Out of reach, no event.
	{
		FHISim::FHIPlayer Player = FHISim::CreatePlayer();
		Player.Pos = FVector2D(0.0f, -11.6f);
		Player.Yaw = PI;                    // facing north, toward the thief
		Player.ShoveCd = 0.0f;
		FHISim::FHIThief Thief = FHISim::CreateThief();
		Thief.Pos = FVector2D(0.0f, -12.05f);
		Thief.Floor = 1; Thief.Z = 3.0f;    // on the deck, directly "in front"
		TArray<FHISim::FHILootItem> Loot = FHISim::CreateLoot();
		TArray<FHISim::FHIEvent> Events;
		TestFalse(TEXT("a shove does not reach through a slab"),
			FHISim::Shove(Player, Thief, Loot, Events));
		TestEqual(TEXT("and raises no event"), Events.Num(), 0);

		Thief.Floor = 0; Thief.Z = 0.0f;    // same plan point, ground floor
		TestTrue(TEXT("the same shove connects on one floor"),
			FHISim::Shove(Player, Thief, Loot, Events));
	}
	{
		FHISim::FHIPlayer Player = FHISim::CreatePlayer();
		Player.Pos = FVector2D(0.0f, -12.05f);
		Player.Floor = 1; Player.Z = 3.0f;
		FHISim::FHIThief Thief = FHISim::CreateThief();
		Thief.Pos = FVector2D(0.0f, -11.6f);
		Thief.Floor = 0; Thief.Z = 0.0f;
		Thief.ShoveCd = 0.0f;
		TArray<FHISim::FHIEvent> Events;
		TestFalse(TEXT("the thief's shove does not reach up through a slab either"),
			FHISim::ThiefShove(Thief, Player, Events));
		Player.Floor = 0; Player.Z = 0.0f;
		TestTrue(TEXT("...but it connects on one floor"),
			FHISim::ThiefShove(Thief, Player, Events));
	}

	// ---- A dropped item stays on the floor it was dropped ON -- the loot carries its
	// storey so the grab gate can refuse the man standing under the deck.
	{
		FHISim::FHIThief Thief = FHISim::CreateThief();
		Thief.Pos = FVector2D(0.0f, -12.2f);
		Thief.Floor = 1; Thief.Z = 3.0f;
		Thief.Carry = TEXT("TV");
		TArray<FHISim::FHILootItem> Loot = FHISim::CreateLoot();
		int32 TvIdx = INDEX_NONE;
		for (int32 I = 0; I < Loot.Num(); ++I)
		{
			Loot[I].Floor = 0;
			if (Loot[I].Label == TEXT("TV")) { Loot[I].bTaken = true; TvIdx = I; }
		}
		TArray<FHISim::FHIEvent> Events;
		FHISim::DropCarried(Thief, Loot, Events);
		TestTrue(TEXT("the drop happened"), TvIdx != INDEX_NONE && !Loot[TvIdx].bTaken);
		TestEqual(TEXT("the dropped TV keeps the second floor"), Loot[TvIdx].Floor, 1);
		TestTrue(TEXT("it lands at his feet"),
			FVector2D::Distance(Loot[TvIdx].Pos, Thief.Pos) < 0.35f);
		TestTrue(TEXT("and the grab gate math says a ground-floor man cannot take it"),
			FMath::Abs(0.0f - FHISim::FloorZAt(Loot[TvIdx].Pos, Loot[TvIdx].Floor)) >= 0.6f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimPlayerClimb,
	"HI.Sim.PlayerClimb",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimPlayerClimb::RunTest(const FString& Parameters)
{
	FHISim::SelfCheck();

	// ---- Walk north from the kitchen into the flight: Z rises, never falls, the floor
	// flips ONCE at the midpoint, StairPace caps the climb, and the floor-1 north wall
	// stops him at the top. Then turn around: the mirror image on the way down.
	const float Dt = 1.0f / 60.0f;
	FHISim::FHIRound R = FHISim::FHIRound::Start(1);
	// Park the only thief 40 m south: this test measures the climb, not a gunfight.
	R.Intruders[0].Pos = FVector2D(0.0f, 40.0f);
	R.Player.Pos = FVector2D(7.24f, -8.95f);
	R.Player.Yaw = PI;                       // facing north, into the stairs
	FHISim::FHIInput In;
	In.Axis = FVector2D(0.0f, 1.0f);         // and holding forward

	float PrevZ = R.Player.Z;
	int32 PrevFloor = R.Player.Floor;
	int32 Flips = 0;
	float FlipZ = -1.0f;
	bool bBand = false, bWasBand = false, bMono = true, bMidFlight = false;
	float MaxSpd = 0.0f, ZMin = R.Player.Z;
	for (int32 F = 0; F < 300; ++F)
	{
		R.Step(In, Dt);
		const bool bNowBand = FHISim::InStairBand(R.Player.Pos);
		if (bNowBand)
		{
			bBand = true;
			if (R.Player.Z + 1e-4f < PrevZ) { bMono = false; }
			if (R.Player.Z > 0.3f && R.Player.Z < 2.7f) { bMidFlight = true; }
			// Pace on STEADY band frames: the entry frame still carries 4.2 m/s of
			// momentum while MoveToward decelerates to the paced wish.
			if (bWasBand) { MaxSpd = FMath::Max(MaxSpd, R.Player.Vel.Size()); }
		}
		bWasBand = bNowBand;
		if (R.Player.Floor != PrevFloor) { ++Flips; FlipZ = R.Player.Z; PrevFloor = R.Player.Floor; }
		ZMin = FMath::Min(ZMin, R.Player.Z);
		PrevZ = R.Player.Z;
	}
	TestTrue(TEXT("the walk entered the stair band"), bBand);
	TestTrue(TEXT("mid-flight happened (not a teleport between floors)"), bMidFlight);
	TestTrue(TEXT("Z never decreases while climbing"), bMono);
	TestEqual(TEXT("the floor flips exactly once"), Flips, 1);
	TestTrue(FString::Printf(TEXT("the flip is at the midpoint height (got %.2f)"), FlipZ),
		FlipZ > 1.35f && FlipZ < 1.65f);
	TestEqual(TEXT("he arrives on the second floor"), R.Player.Floor, 1);
	// 2.97, not 3.0: the north wall's push-out (y -13.455) stops him on the TOP TREAD,
	// 0.045 m south of the band's end -- the landing itself starts at the deck.
	TestTrue(FString::Printf(TEXT("at the top of the flight (got %.2f)"), R.Player.Z),
		R.Player.Z > 2.90f && R.Player.Z < 3.01f);
	TestTrue(FString::Printf(TEXT("the floor-1 north wall stopped him (y %.2f)"), R.Player.Pos.Y),
		R.Player.Pos.Y > -13.80f);
	TestTrue(FString::Printf(TEXT("stairs cost pace: max band speed %.2f <= 2.94+eps"), MaxSpd),
		MaxSpd < 3.0f);
	TestTrue(TEXT("never fell through anything"), ZMin > -0.01f);
	TestTrue(TEXT("round still in play"), R.State == FHISim::EHIRoundState::Play);

	// ---- And back down.
	R.Player.Yaw = 0.0f;                    // south
	PrevZ = R.Player.Z; PrevFloor = R.Player.Floor;
	Flips = 0; FlipZ = -1.0f; bMono = true;
	for (int32 F = 0; F < 300; ++F)
	{
		R.Step(In, Dt);
		if (FHISim::InStairBand(R.Player.Pos) && R.Player.Z > PrevZ + 1e-4f) { bMono = false; }
		if (R.Player.Floor != PrevFloor) { ++Flips; FlipZ = R.Player.Z; PrevFloor = R.Player.Floor; }
		PrevZ = R.Player.Z;
	}
	TestEqual(TEXT("the floor flips back exactly once"), Flips, 1);
	TestTrue(FString::Printf(TEXT("at the midpoint height again (got %.2f)"), FlipZ),
		FlipZ > 1.35f && FlipZ < 1.65f);
	TestEqual(TEXT("he is back on the ground floor"), R.Player.Floor, 0);
	TestTrue(FString::Printf(TEXT("at ground height (got %.2f)"), R.Player.Z),
		FMath::IsNearlyEqual(R.Player.Z, 0.0f, 0.01f));
	TestTrue(TEXT("and walked off the foot of the stairs"), !FHISim::InStairBand(R.Player.Pos));
	TestTrue(TEXT("round still in play after the round trip"),
		R.State == FHISim::EHIRoundState::Play);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimThiefUpstairs,
	"HI.Sim.ThiefUpstairs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimThiefUpstairs::RunTest(const FString& Parameters)
{
	FHISim::SelfCheck();

	// ---- THE HEADLINE AI TEST: every ground room already searched, so the only place
	// left to look is upstairs -- a thief in the front hall has to route through the
	// graph, climb the flight at StairPace, flip floors at the midpoint and arrive at a
	// second-floor search node. Bounded: the 2026-10-15 orbit trap (camped waypoint +
	// tangential steering) ran 120 s at stuck=0.00; this must finish in 45.
	const float Dt = 1.0f / 60.0f;
	FHISim::FHIThief Thief = FHISim::CreateThief();
	Thief.Pos = FVector2D(0.0f, -9.6f);      // doorIn
	Thief.Floor = 0;
	Thief.Z = 0.0f;
	const TCHAR* Done[] = { TEXT("living"), TEXT("kitchen"), TEXT("bedroom"), TEXT("study") };
	for (const TCHAR* Room : Done) { Thief.Searched.Add(Room); }

	FHISim::FHIPlayer Player = FHISim::CreatePlayer();
	Player.Pos = FVector2D(6.0f, -18.0f);    // study corner: the divider hides him, no interference
	TArray<FHISim::FHILootItem> Loot = FHISim::CreateLoot();
	TArray<FHISim::FHIEvent> Events;

	float T = 0.0f, TArrive = -1.0f, ZMax = 0.0f, ZMin = 9.9f;
	int32 ArriveFloor = -1;
	float ArriveZ = -1.0f, ArriveDist = -1.0f;
	bool bMidFlight = false;
	const int32 UpIdx = FHISim::NodeNames().IndexOfByKey(TEXT("upstairs"));
	while (T < 90.0f && TArrive < 0.0f)
	{
		FHISim::StepThief(Thief, Player, Loot, Events, Dt);
		Events.Reset();
		T += Dt;
		ZMax = FMath::Max(ZMax, Thief.Z);
		ZMin = FMath::Min(ZMin, Thief.Z);
		if (FHISim::InStairBand(Thief.Pos) && Thief.Z > 0.3f && Thief.Z < 2.7f) { bMidFlight = true; }
		if (Thief.Searched.Contains(TEXT("upstairs")))
		{
			TArrive = T;
			ArriveFloor = Thief.Floor;
			ArriveZ = Thief.Z;
			ArriveDist = FVector2D::Distance(Thief.Pos, FHISim::NodePositions()[UpIdx]);
		}
	}
	TestTrue(FString::Printf(TEXT("he searched upstairs at all (%.1fs)"), T), TArrive > 0.0f);
	TestTrue(FString::Printf(TEXT("...within the bound (%.1fs <= 45)"), TArrive), TArrive > 0.0f && TArrive < 45.0f);
	TestTrue(TEXT("he actually climbed (mid-flight, not a teleport)"), bMidFlight);
	TestEqual(TEXT("he arrived on the second floor"), ArriveFloor, 1);
	TestTrue(FString::Printf(TEXT("at storey height (got %.2f)"), ArriveZ),
		FMath::IsNearlyEqual(ArriveZ, 3.0f, 0.05f));
	TestTrue(FString::Printf(TEXT("at the node (got %.2f m)"), ArriveDist), ArriveDist < 1.7f);
	TestTrue(FString::Printf(TEXT("Z stayed physical the whole way (%.2f..%.2f)"), ZMin, ZMax),
		ZMin > -0.01f && ZMax < 3.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISimThiefDescends,
	"HI.Sim.ThiefDescends",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISimThiefDescends::RunTest(const FString& Parameters)
{
	FHISim::SelfCheck();

	// ---- The mirror, and the grab-gate's regression pin: a thief on the second floor
	// directly above the known TV is 0.0 m from it IN PLAN and must not be able to take
	// it through the deck. He has to walk to the flight, come down, and grab it ON THE
	// GROUND FLOOR -- the Z gate (|Thief.Z - FloorZAt(loot)| < 0.6) is the only thing
	// separating those two worlds.
	const float Dt = 1.0f / 60.0f;
	FHISim::FHIThief Thief = FHISim::CreateThief();
	Thief.Pos = FVector2D(-5.3f, -12.9f);    // above the living-room TV spot
	// Floor is CARRIED state off the band -- ApplyElevation derives Z from the floor you
	// are on; it does not guess you onto the deck above you. He starts ON the second floor.
	Thief.Floor = 1;
	Thief.Z = 0.0f;
	FHISim::ApplyElevation(Thief.Pos, Thief.Floor, Thief.Z);
	TestEqual(TEXT("the deck takes him to the second floor"), Thief.Floor, 1);

	TArray<FHISim::FHILootItem> Loot = FHISim::CreateLoot();
	int32 TvIdx = INDEX_NONE;
	for (int32 I = 0; I < Loot.Num(); ++I)
	{
		if (Loot[I].Label == TEXT("TV")) { Loot[I].bKnown = true; TvIdx = I; }
	}
	TestTrue(TEXT("the TV exists"), TvIdx != INDEX_NONE);

	FHISim::FHIPlayer Player = FHISim::CreatePlayer();
	Player.Pos = FVector2D(6.0f, -18.0f);    // out of sight, out of the run
	TArray<FHISim::FHIEvent> Events;

	// Phase A: three seconds standing on the loot's plan projection. The grab must NOT
	// happen -- the legacy 2-D distance test would have taken it on frame one.
	float T = 0.0f;
	for (int32 F = 0; F < 180; ++F)
	{
		FHISim::StepThief(Thief, Player, Loot, Events, Dt);
		Events.Reset();
		T += Dt;
	}
	TestFalse(TEXT("the grab gate refuses through-slab picks"), Loot[TvIdx].bTaken);

	// Phase B: bounded -- he routes to the flight, descends, and takes it on floor 0.
	int32 GrabFloor = -1;
	float GrabZ = -9.9f, TGrab = -1.0f, ZMin = Thief.Z, ZMax = Thief.Z;
	while (T < 90.0f && !Loot[TvIdx].bTaken)
	{
		FHISim::StepThief(Thief, Player, Loot, Events, Dt);
		Events.Reset();
		T += Dt;
		ZMin = FMath::Min(ZMin, Thief.Z);
		ZMax = FMath::Max(ZMax, Thief.Z);
		if (Loot[TvIdx].bTaken)
		{
			TGrab = T;
			GrabFloor = Thief.Floor;
			GrabZ = Thief.Z;
		}
	}
	TestTrue(FString::Printf(TEXT("he came down and got it (%.1fs)"), T), Loot[TvIdx].bTaken);
	TestTrue(FString::Printf(TEXT("within the bound (%.1fs <= 60)"), TGrab), TGrab > 0.0f && TGrab < 60.0f);
	TestEqual(TEXT("the grab happened on the ground floor"), GrabFloor, 0);
	TestTrue(FString::Printf(TEXT("at grab height (got %.2f)"), GrabZ), FMath::Abs(GrabZ) < 0.6f);
	TestEqual(TEXT("and he is carrying it"), Thief.Carry, FString(TEXT("TV")));
	TestTrue(FString::Printf(TEXT("Z stayed physical (%.2f..%.2f)"), ZMin, ZMax),
		ZMin > -0.01f && ZMax < 3.01f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
