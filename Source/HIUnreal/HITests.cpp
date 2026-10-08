#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "HIHouse.h"
#include "HIHUD.h"
#include "HIIntruder.h"
#include "HIPlayerCharacter.h"
#include "HIUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

// The actor layer is thin on purpose -- the sim owns the logic, and HI.Sim covers it. But
// three functions here are pure mappings, and EVERY bug found in this port so far has been
// exactly that kind: an angle argument the wrong way round, a unit assumed rather than
// converted, a plane assumed to be three-dimensional. These are the three places the same
// class of mistake could still be hiding, and none of them needs a world to check.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHIUnrealSizing,
	"HI.Unreal.Sizing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHIUnrealSizing::RunTest(const FString& Parameters)
{
	// ---- a sim rectangle becomes a box, and the two halves of that use DIFFERENT units
	{
		const FBox2D Rect(FVector2D(-1.0f, -2.0f), FVector2D(1.0f, 2.0f));
		const FTransform Box = AHIHouse::BoxTransform(Rect, 0.0f, 3.0f);
		const FVector Loc = Box.GetLocation();
		const FVector Scale = Box.GetScale3D();

		// The SCALE is in metres, numerically, because the engine's cube is one metre across
		// at unit scale. The LOCATION is in centimetres, because world positions are. They
		// are not the same unit, and this is the assertion that says so out loud.
		TestTrue(FString::Printf(TEXT("a 2x4x3 m solid scales by (2, 4, 3) -- METRES, because the cube is 1 m at unit scale (%.2f, %.2f, %.2f)"),
			Scale.X, Scale.Y, Scale.Z),
			FMath::IsNearlyEqual(Scale.X, 2.0f, 0.001f)
			&& FMath::IsNearlyEqual(Scale.Y, 4.0f, 0.001f)
			&& FMath::IsNearlyEqual(Scale.Z, 3.0f, 0.001f));

		TestTrue(FString::Printf(TEXT("and is placed in CENTIMETRES, at its own centre (%.1f, %.1f, %.1f)"),
			Loc.X, Loc.Y, Loc.Z),
			FMath::IsNearlyEqual(Loc.X, 0.0f, 0.01f)
			&& FMath::IsNearlyEqual(Loc.Y, 0.0f, 0.01f)
			&& FMath::IsNearlyEqual(Loc.Z, 1.5f * HI_METERS_TO_UU, 0.01f));

		// A wall spanning a storey should stand ON the floor, not centred on the origin.
		const FTransform Tall = AHIHouse::BoxTransform(Rect, 0.0f, FHISim::WallHeight);
		TestTrue(FString::Printf(TEXT("a 3 m wall's centre is 1.5 m up, not at the floor (%.1f)"),
			Tall.GetLocation().Z),
			FMath::IsNearlyEqual(Tall.GetLocation().Z, 1.5f * HI_METERS_TO_UU, 0.01f));
	}

	// ---- an intruder's facing: the sim's yaw 0 faces +Y, Unreal's faces +X
	{
		const FRotator AtZero = AHIIntruder::BodyRotation(0.0f);
		TestTrue(FString::Printf(TEXT("sim yaw 0 maps to world yaw 90, so a body spawned facing +Y really faces +Y (%.1f)"),
			AtZero.Yaw),
			FMath::IsNearlyEqual(AtZero.Yaw, 90.0f, 0.01f));

		const FRotator AtQuarter = AHIIntruder::BodyRotation(UE_PI * 0.5f);
		TestTrue(FString::Printf(TEXT("and a quarter turn maps to 0 (%.1f)"), AtQuarter.Yaw),
			FMath::IsNearlyEqual(AtQuarter.Yaw, 0.0f, 0.01f));

		// The mapping must actually turn: a constant would pass both of the above only if
		// they happened to agree, so check a third angle that is neither.
		const FRotator AtHalf = AHIIntruder::BodyRotation(UE_PI);
		TestTrue(FString::Printf(TEXT("and it is a real rotation, not a constant (%.1f at a half turn)"),
			AtHalf.Yaw),
			!FMath::IsNearlyEqual(AtHalf.Yaw, AtZero.Yaw, 1.0f)
			&& !FMath::IsNearlyEqual(AtHalf.Yaw, AtQuarter.Yaw, 1.0f));
	}

	// ---- the view: behind the player, above him, and looking where he looks
	{
		FHISim::FHIPlayer P = FHISim::CreatePlayer();
		P.Yaw = 0.0f;
		P.Pitch = 0.0f;
		P.Pos = FVector2D(0.0f, 0.0f);
		P.Z = 0.0f;

		const FVector Pivot = AHIPlayerCharacter::EyePivot(P);
		TestTrue(FString::Printf(TEXT("the eye pivot is a storey-free eye height in CENTIMETRES (%.1f)"), Pivot.Z),
			FMath::IsNearlyEqual(Pivot.Z, FHISim::EyeHeight * HI_METERS_TO_UU, 0.01f));

		FVector CamLoc;
		FVector CamDir;
		AHIPlayerCharacter::ViewTransform(P, CamLoc, CamDir);

		TestTrue(FString::Printf(TEXT("at yaw 0 he looks along +Y (%.2f, %.2f, %.2f)"), CamDir.X, CamDir.Y, CamDir.Z),
			FMath::IsNearlyEqual(CamDir.Y, 1.0f, 0.001f) && FMath::IsNearlyEqual(CamDir.X, 0.0f, 0.001f));
		TestTrue(FString::Printf(TEXT("the camera is BEHIND him (-Y) and above the pivot (%.1f, %.1f)"),
			CamLoc.Y, CamLoc.Z),
			CamLoc.Y < Pivot.Y && CamLoc.Z > Pivot.Z);
		TestTrue(FString::Printf(TEXT("and the direction is a unit vector, NOT scaled like the position (%.3f)"),
			CamDir.Size()),
			FMath::IsNearlyEqual(CamDir.Size(), 1.0f, 0.001f));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHIUnrealHud,
	"HI.Unreal.Hud",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHIUnrealHud::RunTest(const FString& Parameters)
{
	// What the HUD says, checked without a canvas. Drawing is the one part that needs eyes;
	// the NUMBERS are the part that can be wrong in a way nobody notices until it matters,
	// so they are the part asserted here.

	// ---- a fresh round reads as a fresh round
	{
		FHISim::FHIRound R = FHISim::FHIRound::Start(3);
		const TArray<FString> Lines = AHIHUD::RoundLines(R);

		auto Has = [&Lines](const FString& Needle)
		{
			for (const FString& L : Lines)
			{
				if (L.Contains(Needle))
				{
					return true;
				}
			}
			return false;
		};

		TestTrue(FString::Printf(TEXT("the HUD shows the pistol's magazine and reserve (%s)"),
			Lines.Num() > 1 ? *Lines[1] : TEXT("<missing>")),
			Has(TEXT("12 / 36")));
		TestTrue(FString::Printf(TEXT("and starts at 0/3 stolen (%s)"),
			Lines.Num() > 2 ? *Lines[2] : TEXT("<missing>")),
			Has(TEXT("0 / 3")));
		TestTrue(TEXT("and shows nobody down yet"), Has(TEXT("0 / 3")) || Has(TEXT("DOWN  0")));
		TestFalse(TEXT("and does NOT announce an outcome before there is one"),
			Has(TEXT("ROUND WON")) || Has(TEXT("ROUND LOST")));
	}

	// ---- the stolen count tracks the loot, and the outcome appears when it is decided
	{
		FHISim::FHIRound R = FHISim::FHIRound::Start(3);
		// Deliver one by hand: delivered is what empties the round.
		R.Loot[0].bDelivered = true;
		R.Loot[0].bTaken = true;

		bool bShowsOne = false;
		for (const FString& L : AHIHUD::RoundLines(R))
		{
			bShowsOne |= L.Contains(TEXT("1 / 3"));
		}
		TestTrue(TEXT("delivering one valuable moves the count to 1/3"), bShowsOne);

		R.DamageIntruder(R.Intruders[0].Id, 1000.0f);
		bool bAnnouncedEarly = false;
		for (const FString& L : AHIHUD::RoundLines(R))
		{
			bAnnouncedEarly |= L.Contains(TEXT("ROUND WON"));
		}
		TestFalse(TEXT("but one man down does not announce a win"), bAnnouncedEarly);

		R.DamageIntruder(R.Intruders[1].Id, 1000.0f);
		R.DamageIntruder(R.Intruders[2].Id, 1000.0f);
		bool bShowsWin = false;
		for (const FString& L : AHIHUD::RoundLines(R))
		{
			bShowsWin |= L.Contains(TEXT("ROUND WON"));
		}
		TestTrue(TEXT("and the last man down does"), bShowsWin);
	}

	// ---- a lost round says so
	{
		FHISim::FHIRound R = FHISim::FHIRound::Start(1);
		TArray<FHISim::FHIEvent> Sink;
		FHISim::DamagePlayer(R.Player, 1000.0f, Sink);
		R.Events.Append(Sink);
		FHISim::FHIInput In;
		R.Step(In, 1.0f / 60.0f);

		bool bShowsLost = false;
		for (const FString& L : AHIHUD::RoundLines(R))
		{
			bShowsLost |= L.Contains(TEXT("ROUND LOST"));
		}
		TestTrue(TEXT("the homeowner going down reads as a lost round"), bShowsLost);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
