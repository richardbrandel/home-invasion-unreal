#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "HIHouse.h"
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

#endif // WITH_DEV_AUTOMATION_TESTS
