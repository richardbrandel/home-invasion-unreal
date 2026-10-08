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
	// Fails loudly if any constant disagrees with the Godot original.
	FHISim::SelfCheck();

	TestEqual(TEXT("wall count"), FHISim::Walls().Num(), 7);
	TestEqual(TEXT("nav node count"), FHISim::NodeNames().Num(), 9);
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
			FHISim::LosBlocked(NodePos, FHISim::LootPositions()[Idx]));
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
			TestFalse(FString::Printf(TEXT("nav edge %s -> %s crosses a solid"), *From, *To),
				FHISim::LosBlocked(A, FHISim::NodePositions()[Jdx]));
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

	// Now take his route away and watch him coast to a halt. `halted > 0` is the whole
	// assertion: a body that stopped dead would read 0 on the first frame.
	const float Peak = Thief.Speed;
	Thief.Route.Empty();
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
		M.Player.Yaw = 0.0f;                       // yaw 0 faces +Z
		FHISim::FHIInput In;
		In.Axis = FVector2D(0.0f, 1.0f);           // walk forward
		const FVector2D StartPos = M.Player.Pos;
		M.Step(In, Dt);
		const float FirstFrame = FVector2D::Distance(M.Player.Pos, StartPos);
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

#endif // WITH_DEV_AUTOMATION_TESTS
