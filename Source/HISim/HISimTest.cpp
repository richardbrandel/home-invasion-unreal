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

#endif // WITH_DEV_AUTOMATION_TESTS
