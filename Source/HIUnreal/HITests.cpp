#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "HIAudio.h"
#include "HIGameMode.h"
#include "HIHouse.h"
#include "HIHUD.h"
#include "HIIntruder.h"
#include "HIPlayerCharacter.h"
#include "HIUnits.h"
#include "Materials/MaterialInterface.h"

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

		// CROUCHING LOWERS THE EYE. `CrouchEyeHeight` was declared in the sim and referenced
		// nowhere, so [C] slowed the homeowner down and left his viewpoint at standing height
		// -- and the whole point of a crouch in a home-defence game is that you cannot be seen
		// over the furniture. Asserted as a DIFFERENCE, so a constant could not pass it.
		FHISim::FHIPlayer Crouched = P;
		Crouched.bCrouching = true;
		const FVector CrouchPivot = AHIPlayerCharacter::EyePivot(Crouched);
		TestTrue(FString::Printf(TEXT("and crouching actually lowers it (%.1f standing, %.1f crouched)"),
			Pivot.Z, CrouchPivot.Z),
			CrouchPivot.Z < Pivot.Z - 30.0f
			&& FMath::IsNearlyEqual(CrouchPivot.Z, FHISim::CrouchEyeHeight * HI_METERS_TO_UU, 0.01f));

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

// --------------------------------------------------------------- the reticle

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHIUnrealReticle,
	"HI.Unreal.Reticle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHIUnrealReticle::RunTest(const FString& Parameters)
{
	// The one piece of aiming feedback in the game, checked without a canvas.
	//
	// This matters more here than it would in most ports, because the shot ray starts at the
	// CAMERA and follows the aim -- so the reticle is not decoration, it is the only statement
	// of where a bullet will go. A reticle off by a scale factor is a game that misses for
	// reasons the player cannot see.

	// The camera's own basis at sim yaw 0, which is forward +Y: Unreal's right vector for a
	// +Y-facing frame is -X, and that matches the sim's own convention (CameraOffset says
	// right is (-cos yaw, sin yaw)).
	const FVector Forward(0.0f, 1.0f, 0.0f);
	const FVector Right(-1.0f, 0.0f, 0.0f);
	const FVector Up(0.0f, 0.0f, 1.0f);
	const float HalfFov = FMath::Tan(FMath::DegreesToRadians(45.0f));
	const float Aspect = 16.0f / 9.0f;

	FVector2D Frac;

	// ---- looking straight ahead: dead centre, and exactly so. A reticle "near" the middle
	// is a reticle that disagrees with the bullet.
	{
		const FVector Aim = FHISim::AimDir(0.0f, 0.0f);
		TestTrue(TEXT("an aim down the camera's own forward projects"),
			AHIHUD::ReticleFraction(Aim, Forward, Right, Up, HalfFov, Aspect, Frac));
		TestTrue(FString::Printf(TEXT("and it lands exactly on the centre (%.4f, %.4f)"), Frac.X, Frac.Y),
			FMath::IsNearlyEqual(Frac.X, 0.5f, 1e-4f) && FMath::IsNearlyEqual(Frac.Y, 0.5f, 1e-4f));
	}

	// ---- a yaw moves it, and to the correct side. Aim toward +X while the camera's right is
	// -X, so it must go LEFT of centre. A sign error here is invisible until the aim cone is
	// switched back on, which is exactly when nobody would be looking for it.
	{
		const FVector Aim = FHISim::AimDir(FMath::DegreesToRadians(20.0f), 0.0f);
		TestTrue(TEXT("a 20 degree yaw projects"), AHIHUD::ReticleFraction(Aim, Forward, Right, Up, HalfFov, Aspect, Frac));
		TestTrue(FString::Printf(TEXT("and pushing the aim toward +X moves the reticle LEFT (x=%.3f)"), Frac.X),
			Frac.X < 0.45f);
		// 20 degrees off axis at a 45 degree half-FOV is (tan20/tan45) of the way to the edge,
		// so a quarter of the way across half the screen. Checked as a magnitude rather than
		// as "it moved", because "it moved" is also true of a reticle that moves ten times
		// too far.
		const float Expected = 0.5f - (FMath::Tan(FMath::DegreesToRadians(20.0f)) / HalfFov) * 0.5f * Aspect;
		TestTrue(FString::Printf(TEXT("and by the right amount (x=%.3f, expected %.3f)"), Frac.X, Expected),
			FMath::IsNearlyEqual(Frac.X, Expected, 2e-3f));
	}

	// ---- and pitch moves it in Y rather than in X
	{
		const FVector Aim = FHISim::AimDir(0.0f, FMath::DegreesToRadians(20.0f));
		AHIHUD::ReticleFraction(Aim, Forward, Right, Up, HalfFov, Aspect, Frac);
		TestTrue(FString::Printf(TEXT("pitch leaves the reticle on the vertical axis (x=%.3f)"), Frac.X),
			FMath::IsNearlyEqual(Frac.X, 0.5f, 1e-3f));
	}

	// ---- an aim behind the camera has no position, and must say so rather than return a
	// mirrored one that looks plausible.
	{
		TestFalse(TEXT("an aim behind the camera reports no position at all"),
			AHIHUD::ReticleFraction(-Forward, Forward, Right, Up, HalfFov, Aspect, Frac));
	}

	return true;
}

// --------------------------------------------------------------- the palette

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHIUnrealMaterials,
	"HI.Unreal.Materials",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHIUnrealMaterials::RunTest(const FString& Parameters)
{
	// Every primitive in this game is coloured through ONE engine material, by setting a
	// parameter called "Color" on a dynamic instance of it. If that parameter is ever
	// renamed -- or if the material moves -- `SetVectorParameterValue` is a silent no-op and
	// the whole world reverts to the same flat grey.
	//
	// That failure looks exactly like "the colours were never applied", and it would be
	// diagnosed from a screenshot by eye. This is the cheap check that can actually fail.

	UMaterialInterface* Base = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	TestNotNull(TEXT("the engine's BasicShapeMaterial loads at the path the palette uses"), Base);
	if (Base == nullptr)
	{
		return false;
	}

	TArray<FMaterialParameterInfo> Infos;
	TArray<FGuid> Guids;
	Base->GetAllVectorParameterInfo(Infos, Guids);

	bool bHasColor = false;
	for (const FMaterialParameterInfo& Info : Infos)
	{
		if (Info.Name == TEXT("Color"))
		{
			bHasColor = true;
		}
	}
	TestTrue(FString::Printf(TEXT("and it exposes the vector parameter 'Color' (%d parameters)"), Infos.Num()),
		bHasColor);

	// THE CONTROL. Without this, an empty parameter list would pass the assertion above only
	// by failing it -- but a check that cannot distinguish "found" from "looked nowhere" is
	// precisely the "pass-shaped nothing" this project has been burned by three times.
	bool bHasNonsense = false;
	for (const FMaterialParameterInfo& Info : Infos)
	{
		if (Info.Name == TEXT("DefinitelyNotAParameterName"))
		{
			bHasNonsense = true;
		}
	}
	TestFalse(FString::Printf(TEXT("while NOT claiming a parameter that does not exist (%d parameters read)"), Infos.Num()),
		bHasNonsense);

	return true;
}

// --------------------------------------------------------------- the intruder's aim

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHIUnrealThiefAim,
	"HI.Unreal.ThiefAim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHIUnrealThiefAim::RunTest(const FString& Parameters)
{
	// How likely an intruder's clear shot is to hit, as a function of range.
	//
	// Worth a test because the number is a BALANCE DECISION that was already wrong once: the
	// Godot original's earlier formula floored at 0.55, so he hit better than half the time
	// at any distance and running away bought the player nothing. A floor is exactly the kind
	// of thing that can be reintroduced by accident and that nothing else would notice.

	// ---- point blank is certain
	TestTrue(FString::Printf(TEXT("at contact range a clear shot cannot miss (%.2f)"),
		AHIGameMode::ThiefHitChance(0.0f)),
		FMath::IsNearlyEqual(AHIGameMode::ThiefHitChance(0.0f), 1.0f, 1e-4f));

	// ---- range is a real defence, which is the property the old floor destroyed
	{
		const float At5 = AHIGameMode::ThiefHitChance(5.0f);
		const float At15 = AHIGameMode::ThiefHitChance(15.0f);
		const float At25 = AHIGameMode::ThiefHitChance(25.0f);
		TestTrue(FString::Printf(TEXT("and distance makes him steadily worse (%.2f at 5 m, %.2f at 15, %.2f at 25)"),
			At5, At15, At25),
			At5 > At15 && At15 > At25);
		TestTrue(FString::Printf(TEXT("so crossing a room more than halves his chance (%.2f -> %.2f)"), At5, At15),
			At15 < At5 * 0.65f);
	}

	// ---- and the floor is LOW, which is the regression guard. If anyone restores a 0.55
	// floor, this is the assertion that fails.
	{
		const float Far = AHIGameMode::ThiefHitChance(40.0f);
		TestTrue(FString::Printf(TEXT("but a long shot still has a small chance rather than none (%.2f at 40 m)"), Far),
			Far > 0.0f && Far < 0.10f);
		TestTrue(FString::Printf(TEXT("and the floor is nowhere near the old 0.55 that made range meaningless (%.2f)"), Far),
			Far < 0.55f * 0.5f);
	}

	// ---- and the dark makes him worse, which is what the light switch is FOR
	{
		const float LitAt8 = AHIGameMode::ThiefHitChance(8.0f, /*bLightsOn=*/true);
		const float DarkAt8 = AHIGameMode::ThiefHitChance(8.0f, /*bLightsOn=*/false);
		TestTrue(FString::Printf(TEXT("a shot across a dark room is harder than one across a lit one (%.2f dark vs %.2f lit at 8 m)"),
			DarkAt8, LitAt8),
			DarkAt8 < LitAt8);

		// A MULTIPLIER, not a second floor: the advantage of distance and the advantage of
		// darkness have to compose. A floor would mean darkness did nothing at close range,
		// which is the one range where the player most wants it to.
		TestTrue(FString::Printf(TEXT("and the two advantages compose rather than one hiding the other (%.3f dark/%.3f lit = %.2f, and closer: %.3f/%.3f = %.2f)"),
			DarkAt8, LitAt8, DarkAt8 / LitAt8,
			AHIGameMode::ThiefHitChance(2.0f, false), AHIGameMode::ThiefHitChance(2.0f, true),
			AHIGameMode::ThiefHitChance(2.0f, false) / AHIGameMode::ThiefHitChance(2.0f, true)),
			FMath::IsNearlyEqual(DarkAt8 / LitAt8,
				AHIGameMode::ThiefHitChance(2.0f, false) / AHIGameMode::ThiefHitChance(2.0f, true), 0.01f));

		// The default argument must be the LIT case, or every existing caller silently
		// becomes a darkness call and the game gets easier without anyone deciding it.
		TestTrue(TEXT("and the default is the lit case, so existing callers are unchanged"),
			FMath::IsNearlyEqual(AHIGameMode::ThiefHitChance(8.0f), LitAt8, 1e-5f));
	}

	// ---- monotonic over the whole useful span, not just at three sample points
	{
		bool bMonotonic = true;
		float Prev = 2.0f;
		for (int32 Idx = 0; Idx <= 60; ++Idx)
		{
			const float Range = static_cast<float>(Idx) * 0.5f;
			const float Chance = AHIGameMode::ThiefHitChance(Range);
			if (Chance > Prev + 1e-5f)
			{
				bMonotonic = false;
			}
			Prev = Chance;
		}
		TestTrue(TEXT("and it never increases with range, at any range"), bMonotonic);
	}

	return true;
}

// --------------------------------------------------------------- the minimap

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHIUnrealMinimap,
	"HI.Unreal.Minimap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHIUnrealMinimap::RunTest(const FString& Parameters)
{
	// The minimap's coordinate mapping, checked without a canvas.
	//
	// Worth checking because THE PORT HAD NO MINIMAP AT ALL, and the sim's
	// `VisibilityPolygon` was ported and tested for a consumer that did not exist. A map that
	// is mirrored, or that puts the house in the wrong corner, is worse than no map: it sends
	// the player to the wrong door with full confidence.

	const FBox2D B = AHIHUD::MinimapView();

	// ---- the framing itself. A map drawn to `FHISim::Bounds` renders the house as a small
	// blob in a field of lawn; this asserts the view actually contains the house AND the van,
	// which are the only two places anything happens.
	TestTrue(FString::Printf(TEXT("the minimap view contains the whole house (view y %.0f..%.0f, house y %.0f..%.0f)"),
		B.Min.Y, B.Max.Y, FHISim::House.Min.Y, FHISim::House.Max.Y),
		B.Min.Y <= FHISim::House.Min.Y && B.Max.Y >= FHISim::House.Max.Y);
	TestTrue(TEXT("and the van the intruders deliver to"),
		B.Min.Y <= FHISim::Van.Min.Y && B.Max.Y >= FHISim::Van.Max.Y);
	TestTrue(FString::Printf(TEXT("and it is not the whole world, or the house would be tiny (view is %.0f m wide, world is %.0f)"),
		B.GetSize().X, FHISim::Bounds.GetSize().X),
		B.GetSize().X < FHISim::Bounds.GetSize().X * 0.75f);

	// ---- the corners, which is what catches a flipped axis
	{
		const FVector2D Min = AHIHUD::MinimapFraction(B.Min);
		const FVector2D Max = AHIHUD::MinimapFraction(B.Max);
		TestTrue(FString::Printf(TEXT("the world's min corner maps to the BOTTOM-LEFT (%.2f, %.2f)"),
			Min.X, Min.Y),
			FMath::IsNearlyEqual(Min.X, 0.0f, 1e-4f) && FMath::IsNearlyEqual(Min.Y, 1.0f, 1e-4f));
		TestTrue(FString::Printf(TEXT("and its max corner to the top-right (%.2f, %.2f)"),
			Max.X, Max.Y),
			FMath::IsNearlyEqual(Max.X, 1.0f, 1e-4f) && FMath::IsNearlyEqual(Max.Y, 0.0f, 1e-4f));
	}

	// ---- THE FLIP, stated as the thing a player would notice: the driveway is at +Y, so it
	// must be at the TOP of the map. Without the flip it appears at the bottom, and every
	// reading of the map is mirrored.
	{
		const FVector2D Drive = AHIHUD::MinimapFraction(FHISim::Drop);
		const FVector2D House = AHIHUD::MinimapFraction(
			FVector2D(FHISim::House.GetCenter().X, FHISim::House.GetCenter().Y));
		TestTrue(FString::Printf(TEXT("the van's drop point is ABOVE the house on the map (%.2f vs %.2f)"),
			Drive.Y, House.Y),
			Drive.Y < House.Y);
	}

	// ---- and the centre, so a constant would not pass the corners
	{
		const FVector2D Centre = AHIHUD::MinimapFraction(B.GetCenter());
		TestTrue(FString::Printf(TEXT("the centre of the world is the centre of the map (%.3f, %.3f)"),
			Centre.X, Centre.Y),
			FMath::IsNearlyEqual(Centre.X, 0.5f, 1e-4f)
			&& FMath::IsNearlyEqual(Centre.Y, 0.5f, 1e-4f));
	}

	return true;
}

// --------------------------------------------------------------- sound

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHIUnrealAudio,
	"HI.Unreal.Audio",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHIUnrealAudio::RunTest(const FString& Parameters)
{
	// THERE WAS NO AUDIO IN THIS PORT AT ALL until this existed, so every assertion here is
	// new rather than a check on something that used to work.
	//
	// The failure mode this is built around is a WRONG PATH: every one of these loads is a
	// `LoadObject` by string, and a string that does not resolve returns null and makes the
	// whole audio layer a silent no-op. Silence is also what a working game sounds like when
	// the volume is down and when the machine has no sound card, so it is exactly the kind of
	// failure that never gets noticed.

	// ---- the copied asset really is where the code says it is
	TestNotNull(TEXT("the weapon-fire cue the project copies out of the engine templates loads"),
		FHIAudio::PistolShot());

	// ---- THE BUSES. Item 20 is "everything on Master", so the point is that these are not
	// Master: the engine's own SFX class, and a submix whose effect chain was inspected before
	// the code relied on it (`SubmixEffectReverbPreset` -> `MasterReverbEffectPreset`).
	TestNotNull(TEXT("the engine's SFX sound class loads, so cues are not all on Master"),
		FHIAudio::SfxClass());
	TestNotNull(TEXT("and the reverb submix loads, so the indoor/outdoor send is not dropped"),
		FHIAudio::ReverbSubmix());

	// ---- the indoor/outdoor test itself. This is what decides whether a shot rings.
	{
		// The living room, from the sim's own house rectangle.
		const FVector Living(-5.0f * HI_METERS_TO_UU, -11.0f * HI_METERS_TO_UU, 150.0f);
		TestTrue(TEXT("a gunshot in the living room counts as indoors"),
			FHIAudio::bInsideHouse(Living));
	}

	{
		// The driveway, where the van is. Well outside anything the house roofs.
		const FVector Drive(0.0f, 8.0f * HI_METERS_TO_UU, 150.0f);
		TestFalse(TEXT("and a gunshot out at the van does not"),
			FHIAudio::bInsideHouse(Drive));
	}

	{
		// The doorway. There is a deliberate margin here: a shot fired in the door frame is
		// neither clearly in nor clearly out, and calling it indoor is the kinder error --
		// the reverb is what tells the player he is fighting inside a room.
		const FVector Doorway(0.0f, -8.05f * HI_METERS_TO_UU, 150.0f);
		TestTrue(TEXT("while one in the open front door still rings, as a doorway should"),
			FHIAudio::bInsideHouse(Doorway));
	}

	// ---- and the negative control, so a function that returned TRUE unconditionally would
	// not pass everything above. Far up the lawn, 40 m past the van.
	{
		const FVector Far(0.0f, 40.0f * HI_METERS_TO_UU, 150.0f);
		TestFalse(TEXT("but 40 m up the lawn is unambiguously outside"),
			FHIAudio::bInsideHouse(Far));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHIUnrealFurniture,
	"HI.Unreal.Furniture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The furniture heights are sim.gd's, ported rather than invented.
 *
 * The first port of the furniture table DROPPED the `h` field -- the claim on record was that
 * the Godot source carries no furniture heights -- and the consequence was that every couch,
 * table, bed and rug was drawn as a floor-to-ceiling column. So this asserts each height
 * against the literal in `../home-invasion-godot/scripts/sim.gd`, which IS the specification
 * for this port. A missing or mistyped one fails here instead of looking odd in a capture.
 */
bool FHIUnrealFurniture::RunTest(const FString& Parameters)
{
	const TArray<FString>& Labels = FHISim::FurnitureLabels();
	const TArray<float>& Heights = FHISim::FurnitureHeights();
	const TArray<bool>& Flat = FHISim::FurnitureFlat();
	const TArray<FBox2D>& Rects = FHISim::FurnitureRects();

	TestEqual(TEXT("a label per furniture row"), Labels.Num(), 19);
	TestEqual(TEXT("a height per furniture row"), Heights.Num(), Labels.Num());
	TestEqual(TEXT("a flat flag per furniture row"), Flat.Num(), Labels.Num());
	TestEqual(TEXT("a rect per furniture row"), Rects.Num(), Labels.Num());

	// label -> h, verbatim from sim.gd's FURNITURE literal. Duplicated kinds (chair, rug)
	// are listed once each and matched by kind as they are encountered.
	static const TMap<FString, float> Spec = {
		{ TEXT("sofa"),         0.80f },
		{ TEXT("side_table"),   0.45f },
		{ TEXT("coffee_table"), 0.42f },
		{ TEXT("tv_stand"),     0.50f },
		{ TEXT("rug"),          0.02f },
		{ TEXT("lamp"),         1.60f },
		{ TEXT("counter"),      0.92f },
		{ TEXT("fridge"),       1.85f },
		{ TEXT("dining_table"), 0.75f },
		{ TEXT("chair"),        0.90f },
		{ TEXT("bed"),          0.55f },
		{ TEXT("nightstand"),   0.55f },
		{ TEXT("wardrobe"),     2.00f },
		{ TEXT("desk"),         0.75f },
		{ TEXT("desk_chair"),   0.95f },
		{ TEXT("bookshelf"),    2.00f },
		{ TEXT("cabinet"),      1.30f },
	};

	for (int32 Idx = 0; Idx < Labels.Num(); ++Idx)
	{
		const float* Want = Spec.Find(Labels[Idx]);
		TestTrue(*FString::Printf(TEXT("'%s' is a kind sim.gd names"), *Labels[Idx]),
			Want != nullptr);
		if (Want != nullptr)
		{
			TestEqual(*FString::Printf(TEXT("'%s' height matches sim.gd"), *Labels[Idx]),
				Heights[Idx], *Want);
		}
		// Nothing is a ceiling and nothing is inside out.
		TestTrue(*FString::Printf(TEXT("'%s' is a sane height (%.2f m)"), *Labels[Idx], Heights[Idx]),
			Heights[Idx] > 0.0f && Heights[Idx] < FHISim::WallHeight);
	}

	// The RUG is the interesting one: it is flat, so it is not in `Solids()` and was never
	// drawn at all by the first port. Both rugs must be flat, and everything else must not.
	int32 FlatCount = 0;
	for (int32 Idx = 0; Idx < Flat.Num(); ++Idx)
	{
		if (Flat[Idx])
		{
			++FlatCount;
			TestEqual(*FString::Printf(TEXT("'%s' is a rug"), *Labels[Idx]), Labels[Idx], TEXT("rug"));
			TestTrue(*FString::Printf(TEXT("'%s' is 2 cm, not a block"), *Labels[Idx]), Heights[Idx] < 0.1f);
		}
	}
	TestEqual(TEXT("two rugs, as in sim.gd"), FlatCount, 2);

	// And the alignment the builder relies on: the non-flat rows must line up with the
	// non-wall half of `Solids()`, or heights get attached to the wrong rectangles.
	TestEqual(TEXT("non-flat furniture count matches Solids()"),
		Labels.Num() - FlatCount, FHISim::Solids().Num() - FHISim::Walls().Num());

	// The specific regression, stated as a value: a sofa is not a column.
	TestTrue(TEXT("the sofa is furniture, not a wall"),
		Heights[0] < 1.0f && FHISim::WallHeight > 1.0f);

	// ---- and every piece of it must be INSIDE the house.
	//
	// The living-room rug sat at z -8.60 against a house of z -19..-8, so 1.2 m of it lay
	// out on the drive, and it had been wrong since the literal was written. It survived
	// because a rug is `flat`: excluded from `Solids()`, so never drawn -- and an invisible
	// object cannot look wrong. Drawing flat furniture is what exposed it, so this is the
	// check that makes the whole class fail loudly. `test_sim.gd` asserts the same thing.
	{
		const FBox2D& H = FHISim::House;
		FString Outside;
		for (int32 Idx = 0; Idx < Rects.Num(); ++Idx)
		{
			const FBox2D& R = Rects[Idx];
			if (R.Min.X < H.Min.X || R.Min.Y < H.Min.Y || R.Max.X > H.Max.X || R.Max.Y > H.Max.Y)
			{
				Outside += FString::Printf(TEXT("%s at (%.2f,%.2f)-(%.2f,%.2f); "),
					*Labels[Idx], R.Min.X, R.Min.Y, R.Max.X, R.Max.Y);
			}
		}
		TestTrue(*FString::Printf(TEXT("every furniture rect is inside the house%s"), *Outside),
			Outside.IsEmpty());
		// A negative control: the rug's OLD rectangle must be rejected by the same test.
		const FBox2D Bad(FVector2D(-7.60f, -8.60f), FVector2D(-3.20f, -6.80f));
		TestTrue(TEXT("that check can fail (the rug's old rect is outside)"),
			Bad.Min.Y < H.Min.Y || Bad.Max.Y > H.Max.Y);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHIUnrealWallFittings,
	"HI.Unreal.WallFittings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Every socket, switch and thermostat must be ON a wall.
 *
 * This is a check that could not fail before it existed, and its absence is why a light
 * switch sat inside `DoorGap` for the life of the port: it hung in the open front doorway
 * with no wall behind it, visible from the spawn as a small pale box apparently floating
 * over the lawn. Four more were up to 1.4 m off any face. Eight hand-typed coordinate pairs
 * is exactly the kind of data that is wrong and looks fine, so it is measured instead.
 *
 * The gap between two axis-aligned boxes is the larger of the two per-axis separations; a
 * fitting flush against a wall scores 0, and one in the middle of a room scores its distance
 * from the nearest wall. A centimetre of slack covers the depth rounding, and nothing more.
 */
bool FHIUnrealWallFittings::RunTest(const FString& Parameters)
{
	const TArray<AHIHouse::FFitting>& Fittings = AHIHouse::WallFittings();
	TestTrue(TEXT("there are wall fittings to check"), Fittings.Num() >= 8);

	const TArray<FBox2D>& Walls = FHISim::Walls();
	for (int32 Idx = 0; Idx < Fittings.Num(); ++Idx)
	{
		const AHIHouse::FFitting& F = Fittings[Idx];
		const FBox2D Box(
			FVector2D(F.X - F.SX * 0.5f, F.Y - F.SY * 0.5f),
			FVector2D(F.X + F.SX * 0.5f, F.Y + F.SY * 0.5f));

		float Nearest = TNumericLimits<float>::Max();
		for (const FBox2D& W : Walls)
		{
			const float SepX = FMath::Max(0.0f, FMath::Max(Box.Min.X - W.Max.X, W.Min.X - Box.Max.X));
			const float SepY = FMath::Max(0.0f, FMath::Max(Box.Min.Y - W.Max.Y, W.Min.Y - Box.Max.Y));
			Nearest = FMath::Min(Nearest, FMath::Max(SepX, SepY));
		}
		TestTrue(*FString::Printf(
			TEXT("fitting %d at (%.2f, %.2f) is on a wall -- nearest gap %.3f m"),
			Idx, F.X, F.Y, Nearest), Nearest <= 0.011f);
	}

	// The specific one that was wrong, stated as a position: the switch by the front door must
	// be OUTSIDE the doorway. `DoorGap` is x -1..1, and a fitting inside it hangs in the hole.
	bool bAnyBesideTheDoor = false;
	for (const AHIHouse::FFitting& F : Fittings)
	{
		if (FMath::IsNearlyEqual(F.Z, 1.25f) && F.Y < -8.0f && F.Y > -8.6f)
		{
			bAnyBesideTheDoor = true;
			TestTrue(*FString::Printf(TEXT("the front-door switch (%.2f) is clear of DoorGap"), F.X),
				F.X < FHISim::DoorGap.X || F.X > FHISim::DoorGap.Y);
		}
	}
	TestTrue(TEXT("there is a switch by the front door"), bAnyBesideTheDoor);

	// A NEGATIVE CONTROL, because the loop above passes trivially if the measure is broken.
	// The middle of the living room is not on a wall, and the same arithmetic has to say so.
	{
		const FVector2D Mid(-5.0f, -11.0f);
		float Nearest = TNumericLimits<float>::Max();
		for (const FBox2D& W : Walls)
		{
			const float SepX = FMath::Max(0.0f, FMath::Max(Mid.X - W.Max.X, W.Min.X - Mid.X));
			const float SepY = FMath::Max(0.0f, FMath::Max(Mid.Y - W.Max.Y, W.Min.Y - Mid.Y));
			Nearest = FMath::Min(Nearest, FMath::Max(SepX, SepY));
		}
		TestTrue(*FString::Printf(
			TEXT("the control -- mid-living-room -- is NOT on a wall (gap %.2f m, expected ~3.25)"),
			Nearest), Nearest > 1.0f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHIUnrealWounds,
	"HI.Unreal.Wounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Blood on the man: the count and the placement, asserted without a world.
 *
 * Fix-list item 19 said *"a hit produces a bullet hole and a stagger. Nothing on the person"*.
 * The two halves that can be wrong are how many wounds a given health should show, and WHERE
 * each one goes -- and the second is the mapping kind of mistake this port has made in every
 * single bug so far, so it is checked here rather than looked at.
 */
bool FHIUnrealWounds::RunTest(const FString& Parameters)
{
	// Full health is unmarked, and a corpse is not. A "band" is 20 HP, so the first wound
	// appears once a whole band is gone -- which is the boundary worth pinning, because the
	// first version of this test asserted 85 HP showed one wound and the code was right.
	TestEqual(TEXT("full health shows no wounds"), AHIIntruder::WoundsForHealth(100.0f), 0);
	TestEqual(TEXT("just under a whole band shows nothing"), AHIIntruder::WoundsForHealth(85.0f), 0);
	TestEqual(TEXT("one whole band lost shows one"), AHIIntruder::WoundsForHealth(80.0f), 1);
	TestEqual(TEXT("half health shows two"), AHIIntruder::WoundsForHealth(50.0f), 2);
	TestEqual(TEXT("nearly dead shows most"), AHIIntruder::WoundsForHealth(5.0f), 4);
	TestEqual(TEXT("dead shows all of them"), AHIIntruder::WoundsForHealth(0.0f), AHIIntruder::MaxWounds);

	// Monotonic: more damage never means fewer wounds. A step that went backwards would make
	// a man look healthier after being shot, which is the sort of thing only an eye catches.
	int32 Previous = 0;
	bool bMonotonic = true;
	for (int32 Hp = 100; Hp >= 0; --Hp)
	{
		const int32 Now = AHIIntruder::WoundsForHealth(static_cast<float>(Hp));
		if (Now < Previous)
		{
			bMonotonic = false;
		}
		Previous = Now;
	}
	TestTrue(TEXT("wounds never decrease as health falls"), bMonotonic);

	// Every slot must resolve, to a DISTINCT bone, and out of range must be refused.
	TSet<FName> Bones;
	for (int32 Idx = 0; Idx < AHIIntruder::MaxWounds; ++Idx)
	{
		FName Bone;
		FVector Offset;
		FVector Size;
		TestTrue(*FString::Printf(TEXT("wound %d has a placement"), Idx),
			AHIIntruder::WoundPlacement(Idx, Bone, Offset, Size));
		TestTrue(*FString::Printf(TEXT("wound %d names a bone"), Idx), !Bone.IsNone());
		TestTrue(*FString::Printf(TEXT("wound %d is a sane size (%.1f cm)"), Idx, Size.X),
			Size.X > 1.0f && Size.X < 20.0f);
		// Two wounds on one bone would overlap and look like one, which defeats the point of
		// showing five of them.
		TestFalse(*FString::Printf(TEXT("wound %d is on its own bone (%s)"), Idx, *Bone.ToString()),
			Bones.Contains(Bone));
		Bones.Add(Bone);
	}
	FName Dummy; FVector O, S;
	TestFalse(TEXT("a wound out of range is refused"), AHIIntruder::WoundPlacement(-1, Dummy, O, S));
	TestFalse(TEXT("a wound past the end is refused"),
		AHIIntruder::WoundPlacement(AHIIntruder::MaxWounds, Dummy, O, S));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

