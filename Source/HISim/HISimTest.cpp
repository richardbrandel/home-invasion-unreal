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
		const int32 VanIdx = FHISim::NodeNames().IndexOfByKey(TEXT("van"));
		const int32 RoomIdx = FHISim::NodeNames().IndexOfByKey(Room);
		TestTrue(FString::Printf(TEXT("route to %s starts at the van"), *Room),
			Route[0].Equals(FHISim::NodePositions()[VanIdx], 0.001f));
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

#endif // WITH_DEV_AUTOMATION_TESTS
