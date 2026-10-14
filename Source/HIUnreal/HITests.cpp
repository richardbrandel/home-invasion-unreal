#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "HIAudio.h"
#include "MaterialShared.h"
#include "HICharAnim.h"
#include "HIGameMode.h"
#include "HIMarks.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundSubmix.h"
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

	// ---- a body's facing: the sim's yaw 0 faces +Y, AND THE MESH FACES ITS OWN +Y, NOT ITS +X
	//
	// THIS TEST USED TO ASSERT THE BUG. It pinned `BodyRotation(0) == yaw 90`, justified with "a
	// body spawned facing +Y really faces +Y" -- and that reasoning is about the ACTOR's +X axis.
	// A mannequin skeleton faces its own +Y (which is why the engine's own `ACharacter` carries a
	// -90 yaw on its mesh), so putting the component's +X on the aim left every man in the game a
	// quarter turn out. Reported from play 2026-10-13: *"The cross hairs are 90 degrees to the right
	// of the player instead of being in front"*, and measured at exactly 90.0 deg with `-HIProbe`.
	//
	// So it asserts the PROPERTY rather than two magic numbers, and against `FHISim::AimDir`, which
	// is the sim's own independent statement of the direction: whatever the yaw, the MESH'S OWN
	// forward axis must land on it. Which axis that is, is a property of the ASSET and has now
	// changed casts once -- the mannequin faced +Y, the Mixamo characters face `MeshFacingYawDeg`,
	// a constant `HI.Unreal.Cast` recomputes from every character's bind-pose shoulders. Note the
	// division of labour: THIS test checks the composition (the formula maps a sim yaw to a world
	// rotation consistently) and cannot catch a wrong facing constant, because both sides would
	// read the same one; the Cast test is the check that can.
	{
		const FVector MeshFwd = FHICharAnim::MeshForwardAxis();
		const float SimYaws[] = { 0.0f, UE_PI * 0.5f, UE_PI, -UE_PI * 0.5f, 1.1f };
		for (const float SimYaw : SimYaws)
		{
			const FVector Want = FHISim::AimDir(SimYaw, 0.0f).GetSafeNormal();
			const FVector World = AHIIntruder::BodyRotation(SimYaw).RotateVector(MeshFwd).GetSafeNormal();
			const float OnAim = FVector::DotProduct(World, Want);
			TestTrue(*FString::Printf(
				TEXT("a body at sim yaw %.2f faces where the sim says (meshFwd . aim = %.4f)"), SimYaw, OnAim),
				OnAim > 0.9999f);
		}
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

	// ---- the imported assets really are where the code says they are. Every one of these is
	// a LoadObject by string, and the engine's template cue that used to be here is GONE --
	// it carried the attenuation that crashed the first real play session.
	TestNotNull(TEXT("the imported pistol gunshot loads"),
		FHIAudio::Imported(TEXT("gunshot_pistol")));
	TestNotNull(TEXT("the imported shotgun blast loads"),
		FHIAudio::Imported(TEXT("gunshot_shotgun")));
	TestNotNull(TEXT("the imported impact loads -- the impact.wav from the Godot repo"),
		FHIAudio::Imported(TEXT("impact")));
	TestNotNull(TEXT("the imported siren loads"), FHIAudio::Imported(TEXT("siren")));

	// ---- AND ALL NINE VOICE LINES RESOLVE. Asserted rather than assumed because a missing one
	// plays NOTHING and says so once in the log -- indistinguishable from a working game with
	// the volume down, which is the failure this whole test was built around.
	//
	// It has a live reason to exist: the nine lines were re-rendered with a local NEURAL model
	// (Kokoro, offline, on this Mac) on 2026-10-12 in place of macOS `say`, and re-importing a
	// whole asset folder is exactly the operation that quietly imports eight of nine.
	{
		const TCHAR* Lines[] = {
			TEXT("thief_spot"), TEXT("thief_order"), TEXT("thief_pain"), TEXT("thief_shoved"),
			TEXT("thief_flee"), TEXT("thief_down"),
			TEXT("owner_challenge"), TEXT("owner_shout"), TEXT("owner_police"),
		};
		const int32 Expected = static_cast<int32>(UE_ARRAY_COUNT(Lines));
		int32 Missing = 0;
		FString MissingList;
		for (const TCHAR* Line : Lines)
		{
			if (FHIAudio::VoiceCue(Line) == nullptr)
			{
				++Missing;
				MissingList += FString::Printf(TEXT(" %s"), Line);
			}
		}
		TestEqual(*FString::Printf(TEXT("all %d voice lines resolve (missing:%s)"),
			Expected, MissingList.IsEmpty() ? TEXT(" none") : *MissingList), Missing, 0);
	}

	// ---- AND THE ATTENUATION MUST BE GC-ROOTED. This is the bug that ended the second play
	// session: a NewObject'd asset held only by a raw static pointer is invisible to the
	// collector, so it is freed once the auto-destroying audio components are gone, and the
	// next gunshot dereferences freed memory. It worked for minutes and then segfaulted inside
	// the engine's own audio code, which is a hard thing to find from the crash alone.
	{
		USoundAttenuation* Atten = FHIAudio::Attenuation();
		TestNotNull(TEXT("the attenuation object exists"), Atten);
		if (Atten != nullptr)
		{
			TestTrue(TEXT("and is GC-ROOTED, or it is freed mid-session"), Atten->IsRooted());
			// The other half of the original crash: an invalid shape makes GetMaxDimension
			// assert. Sphere is the value that was not being set.
			TestEqual(TEXT("with a valid attenuation shape"),
				static_cast<int32>(Atten->Attenuation.AttenuationShape.GetValue()),
				static_cast<int32>(EAttenuationShape::Sphere));
		}
	}

	// ---- EVERY RUNTIME-LOADED ASSET MUST BE ROOTED, and this loop is the point.
	//
	// The first version of this test checked the ATTENUATION ONLY, because that is the object
	// whose address appeared in the crash. So when the sound class turned out to have the same
	// defect, nothing failed and a person lost a third play session to it. Checking one
	// instance of a bug class is not checking the class -- iterate the list instead.
	//
	// A raw pointer held in a `static` is invisible to the collector, the audio components are
	// `bAutoDestroy`, and the failure is a use-after-free somewhere deep in the engine on a
	// later cue rather than a null here.
	{
		USoundClass* Class = FHIAudio::SfxClass();
		USoundSubmix* Submix = FHIAudio::ReverbSubmix();
		TestNotNull(TEXT("the sound class loads"), Class);
		TestNotNull(TEXT("the reverb submix loads"), Submix);
		if (Class != nullptr)
		{
			TestTrue(TEXT("the SOUND CLASS is GC-rooted (this is what crashed the third session)"),
				Class->IsRooted());
		}
		if (Submix != nullptr)
		{
			TestTrue(TEXT("the REVERB SUBMIX is GC-rooted"), Submix->IsRooted());
		}

		// And every cue and voice line, since they are cached the same way.
		const TCHAR* Cues[] = { TEXT("gunshot_pistol"), TEXT("gunshot_shotgun"), TEXT("impact"),
			TEXT("siren"), TEXT("footstep"), TEXT("door"), TEXT("van_idle") };
		for (const TCHAR* Name : Cues)
		{
			if (USoundBase* Cue = FHIAudio::Imported(Name))
			{
				TestTrue(*FString::Printf(TEXT("the '%s' cue is GC-rooted"), Name), Cue->IsRooted());
			}
		}
	}

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
		// THE UNIT IS METRES, and getting that wrong is the whole reason a seven-metre red box
		// shipped. The first version of this line said "cm" and checked `1.0 < Size.X < 20.0`
		// -- the same numbers, in the wrong unit -- so it PASSED for a value that
		// `SetRelativeScale3D` turns into a seven-metre cube. An assertion in the wrong unit
		// is worse than a weak one: it actively confirms the bug, and this one did, in a
		// green test run, for as long as the wound code existed.
		//
		// 1 cm to 20 cm, expressed in metres, because metres is what the caller consumes.
		TestTrue(*FString::Printf(TEXT("wound %d is a sane size (%.3f m across, not %.1f)"),
			Idx, Size.X, Size.X),
			Size.X > 0.01f && Size.X < 0.20f);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHIUnrealLookMapping,
	"HI.Unreal.LookMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The mouse-to-view signs, against `game.gd`'s own two lines.
 *
 * Both axes were inverted in the first version, and nothing could have caught it: a still
 * frame cannot show which way a mouse turns, the autopilot never reads the mouse, and the
 * crash hunts were all in the logs. Reported from play as "the mouse doesn't aim and shoot
 * like in the Godot version", which is exactly what an inverted pair of axes feels like.
 *
 * The original:
 *
 *     yaw   -= event.relative.x * AIM_SENS_X
 *     pitch  = clampf(pitch + event.relative.y * AIM_SENS_Y, PITCH_MIN, PITCH_MAX)
 */
bool FHIUnrealLookMapping::RunTest(const FString& Parameters)
{
	float Yaw = 0.0f;
	float Pitch = 0.0f;

	// Mouse RIGHT (positive x) must turn the view the way Godot turns it: yaw DOWN.
	AHIGameMode::LookDelta(100.0f, 0.0f, Yaw, Pitch);
	TestTrue(*FString::Printf(TEXT("mouse right gives a NEGATIVE yaw delta (got %.4f)"), Yaw), Yaw < 0.0f);
	TestTrue(TEXT("and does not touch the pitch"), FMath::IsNearlyZero(Pitch));

	// Mouse DOWN (positive y in both engines) must look DOWN, which is positive pitch here.
	AHIGameMode::LookDelta(0.0f, 100.0f, Yaw, Pitch);
	TestTrue(*FString::Printf(TEXT("mouse down gives a POSITIVE pitch delta (got %.4f)"), Pitch), Pitch > 0.0f);
	TestTrue(TEXT("and does not touch the yaw"), FMath::IsNearlyZero(Yaw));

	// The magnitudes are the original's, and they are DIFFERENT per axis: 0.0022 and 0.0018.
	// A single sensitivity for both was the other half of the first version's mistake.
	AHIGameMode::LookDelta(1000.0f, 0.0f, Yaw, Pitch);
	TestEqual(TEXT("1000 px of x is 2.2 rad, as in game.gd"), Yaw, -2.2f, 0.001f);
	AHIGameMode::LookDelta(0.0f, 1000.0f, Yaw, Pitch);
	TestEqual(TEXT("1000 px of y is 1.8 rad, as in game.gd"), Pitch, 1.8f, 0.001f);

	// A control that would pass if the function ignored its arguments entirely.
	AHIGameMode::LookDelta(0.0f, 0.0f, Yaw, Pitch);
	TestTrue(TEXT("no mouse movement means no view movement"), Yaw == 0.0f && Pitch == 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHIUnrealMarkOrientation,
	"HI.Unreal.MarkOrientation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A bullet hole lies IN the wall, and a corpse mark lies FLAT on the floor.
 *
 * Both were wrong, in the same way and for two sessions, and no still frame showed it: the
 * slab is thin in Z and SizeMeters across X and Y, and `Normal.Rotation()` builds a rotation
 * whose **X** points along the normal. So every wall hole stood OUT of the wall as a 10 cm
 * blade, and the 0.9 m blood mark stood UP off the floor as a red sheet -- which is very
 * probably the "unidentified two-tone object, dark red above green, 1.2 m wide and 2.06 m
 * tall" that this project had recorded in the README and could not name. A red 0.9 m plate
 * standing vertically, seen through the doorway against the lawn, is exactly that.
 *
 * The assertion is the one that was missing: after the rotation, is the slab's THIN axis
 * along the normal?
 */
bool FHIUnrealMarkOrientation::RunTest(const FString& Parameters)
{
	// The slab's THIN axis is its local Z; its SizeMeters face is X by Y. `FRotator::Vector()`
	// returns the X axis, which is the one we must NOT be testing -- the first version of this
	// test did exactly that and failed for the wrong reason. Ask for Z explicitly.
	auto ThinAxis = [](const FRotator& R)
	{
		return R.RotateVector(FVector::UpVector);
	};

	// A wall facing the player: the normal is horizontal.
	const FVector WallNormal(0.0f, -1.0f, 0.0f);
	const FRotator WallRot = AHIMark::SlabRotation(WallNormal);
	TestTrue(*FString::Printf(TEXT("a wall hole's THIN axis points along the wall normal (got %s)"),
		*ThinAxis(WallRot).ToCompactString()),
		FVector::DotProduct(ThinAxis(WallRot), WallNormal) > 0.99f);

	// The SizeMeters face must lie IN the wall, i.e. the local X must be perpendicular to the
	// normal. This is the assertion that actually distinguishes the fix from the bug.
	TestTrue(*FString::Printf(TEXT("and its FACE lies in the wall (local X %s)"),
		*WallRot.Vector().ToCompactString()),
		FMath::Abs(FVector::DotProduct(WallRot.Vector(), WallNormal)) < 0.01f);

	// The wrong answer, asserted against directly, because it is the one that was shipped:
	// `Normal.Rotation()` puts the local X -- the size extent -- along the normal.
	const FRotator OldRot = WallNormal.Rotation();
	TestTrue(TEXT("the OLD rotation really did stand the size extent out of the wall"),
		FMath::Abs(FVector::DotProduct(OldRot.Vector(), WallNormal)) > 0.99f);
	TestTrue(TEXT("and the old rotation left the thin axis in the wall plane, which is the bug"),
		FMath::Abs(FVector::DotProduct(ThinAxis(OldRot), WallNormal)) < 0.01f);

	// A floor mark: an upward normal needs no rotation at all. Identity already has its Z up,
	// so this is exactly where the first version of this test was wrong as well -- it expected
	// -90 degrees.
	const FRotator FloorRot = AHIMark::SlabRotation(FVector::UpVector);
	TestTrue(FString::Printf(TEXT("a floor mark lies flat, thin axis up (got %s)"),
		*ThinAxis(FloorRot).ToCompactString()),
		FVector::DotProduct(ThinAxis(FloorRot), FVector::UpVector) > 0.99f);
	TestTrue(FString::Printf(TEXT("which is the identity rotation (pitch %.1f)"), FloorRot.Pitch),
		FMath::IsNearlyZero(FloorRot.Pitch, 0.01f));

	// A downward normal (a ceiling) is the one case that must flip, and it must not produce a
	// degenerate or NaN basis.
	const FRotator DownRot = AHIMark::SlabRotation(FVector(0.0f, 0.0f, -1.0f));
	TestTrue(TEXT("a mark on a ceiling points its thin axis down"),
		FVector::DotProduct(ThinAxis(DownRot), FVector(0.0f, 0.0f, -1.0f)) > 0.99f);

	// A degenerate normal must not produce a NaN rotation.
	const FRotator ZeroRot = AHIMark::SlabRotation(FVector::ZeroVector);
	TestFalse(TEXT("a zero normal does not yield a NaN rotation"), ZeroRot.ContainsNaN());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHIUnrealDeathFall,
	"HI.Unreal.DeathFall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHIUnrealDeathFall::RunTest(const FString& Parameters)
{
	// "When the intruders get shot, they slouch over. They should fall to the ground."
	//
	// FOUR things have to be true for a man to end up on the floor, and three of them were
	// false on 2026-10-12. None of them needs a rendered frame, and that is the point: this
	// took far too long because the only instrument being used was a running game, and a
	// running game cannot be asked a question this precise. The whole route is: the clip plays,
	// the body can go limp, it has a floor to land on, and the handover does not depend on a
	// frame loop that stops the moment the round is decided.

	// ---- 1. THE CLIP CAN BE PLAYED AT ALL. A montage cannot play in single-node mode -- it
	// needs a slot in an AnimGraph -- and it would fail SILENTLY. The cast version loops:
	// every intruder is a different character with his OWN `death` clip, and one character's
	// clip cannot play on another's skeleton -- the fact the import ordering bug of
	// 2026-10-13 turned on.
	double ShortestDeath = 0.0;
	const TCHAR* DeadMen[] = {
		FHICharAnim::IntruderTagForId(0),
		FHICharAnim::IntruderTagForId(1),
		FHICharAnim::IntruderTagForId(2),
	};
	for (const TCHAR* Tag : DeadMen)
	{
		UAnimationAsset* Clip = LoadObject<UAnimationAsset>(nullptr,
			*FHICharAnim::ClipPathFor(Tag, FHICharAnim::ClipDeath()));
		TestNotNull(*FString::Printf(TEXT("%s's death clip loads"), Tag), Clip);
		if (Clip == nullptr)
		{
			continue;
		}
		TestTrue(*FString::Printf(
			TEXT("%s's death is an AnimSequence, because single node cannot play a montage"), Tag),
			Clip->IsA<UAnimSequence>());
		// RECORDED, the fall itself asserted from the source keys by HI.Unreal.Cast. Measured
		// from the FBX by the Godot project's anim probe: Hips 0.96 m -> 0.14 m across 3.03 s
		// -- a REAL fall, which is why the ragdoll is now a settle rather than the fall. The
		// engine-era `MM_Death_Back_01` flinched and held upright at 87 cm; that number was
		// the reason the ragdoll existed at all.
		AddInfo(FString::Printf(TEXT("%s's death clip is %.2f s long"), Tag, Clip->GetPlayLength()));
		if (ShortestDeath <= 0.0 || Clip->GetPlayLength() < ShortestDeath)
		{
			ShortestDeath = Clip->GetPlayLength();
		}
	}

	// ---- 2. THE BODY CAN GO LIMP. `GoLimp` calls SetSimulatePhysics, which does NOTHING AT
	// ALL without a physics asset -- it would leave the corpse standing exactly as before,
	// which is the bug being fixed. The cast's physics assets are auto-generated beside each
	// mesh by the FBX import (`idle_PhysicsAsset`); "it worked on the mannequin" is precisely
	// the inherited assumption this port keeps having to unlearn, so it is asserted per man.
	for (const TCHAR* Tag : DeadMen)
	{
		USkeletalMesh* Man = LoadObject<USkeletalMesh>(nullptr, *FHICharAnim::MeshPathFor(Tag));
		TestNotNull(*FString::Printf(TEXT("%s's mesh loads"), Tag), Man);
		if (Man != nullptr)
		{
			TestNotNull(*FString::Printf(
				TEXT("%s carries a physics asset, or going limp is a silent no-op"), Tag),
				Man->GetPhysicsAsset());
		}
	}

	// ---- 3. IT HAS SOMETHING TO LAND ON. This is the half that took longest to find. The
	// house blocks ONLY the Visibility channel -- `SetCollisionResponseToAllChannels(
	// ECR_Ignore)` -- and the lawn and the interior floor are decoration with collision
	// switched off entirely, because the file's governing invariant is that decoration must
	// never stop a bullet. So the corpse fell straight through the world: measured, component
	// Z 0 -> -1330 cm in 1.5 s and still accelerating.
	//
	// A corpse is a PHYSICS BODY, and `ECC_PhysicsBody` is a channel nothing else in this
	// project uses, so the fix costs the invariant nothing. Both halves are asserted here --
	// that physics is stopped, AND that bullets still are not.
	const AHIHouse* House = GetDefault<AHIHouse>();
	TestNotNull(TEXT("the house's default object"), House);
	if (House != nullptr)
	{
		struct FNeed
		{
			UInstancedStaticMeshComponent* Comp;
			const TCHAR* What;
		};
		const FNeed Needs[] = {
			{ House->WallInstances,      TEXT("the walls") },
			{ House->FurnitureInstances, TEXT("the furniture") },
			{ House->GroundInstance,     TEXT("the lawn") },
			{ House->FloorInstance,      TEXT("the interior floor") },
		};
		for (const FNeed& Need : Needs)
		{
			TestNotNull(*FString::Printf(TEXT("%s exists as a component"), Need.What), Need.Comp);
			if (Need.Comp == nullptr)
			{
				continue;
			}
			TestTrue(*FString::Printf(TEXT("%s BLOCKS a physics body, so a corpse lands on it"),
				Need.What), Need.Comp->GetCollisionResponseToChannel(ECC_PhysicsBody) == ECR_Block);
		}

		// THE INVARIANT, both ways: the floor must still NOT stop a bullet -- the pellet trace
		// is cast on Visibility and a floor that stopped it would change hit resolution -- while
		// the walls must still stop one.
		TestTrue(TEXT("the lawn still does NOT block a bullet, which is why decoration has no collision"),
			House->GroundInstance != nullptr
			&& House->GroundInstance->GetCollisionResponseToChannel(ECC_Visibility) != ECR_Block);
		TestTrue(TEXT("and neither does the interior floor"),
			House->FloorInstance != nullptr
			&& House->FloorInstance->GetCollisionResponseToChannel(ECC_Visibility) != ECR_Block);
		TestTrue(TEXT("while the walls and furniture still stop a bullet"),
			House->WallInstances != nullptr && House->FurnitureInstances != nullptr
			&& House->WallInstances->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block
			&& House->FurnitureInstances->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block);
	}

	// ---- 4. THE HANDOVER IS NOT ON THE FRAME LOOP. Once the round is decided the game mode
	// stops calling `SyncFromSim` -- "a decided round is inert at the sim level too" -- so the
	// LAST man killed, who is the one the player is watching, would never have gone limp. It is
	// a world timer for that reason, and the delay is asserted to be a real wait INSIDE the
	// clip -- mid-fall -- so the animation and the physics read as one motion.
	TestTrue(FString::Printf(TEXT("the handover waits %.2f s -- after the hit, before the pose holds"),
		AHIIntruder::RagdollAfterDeath), AHIIntruder::RagdollAfterDeath > 0.2f);
	if (ShortestDeath > 0.0)
	{
		TestTrue(FString::Printf(
			TEXT("and lands inside even the SHORTEST death clip (%.2f s) so the two are one motion"),
			ShortestDeath), AHIIntruder::RagdollAfterDeath < ShortestDeath);
	}

	return true;
}

// ---------------------------------------------------------------------------
// HI.Unreal.Cast -- the four imported characters themselves.
//
// Everything else in this port reasons about "the homeowner" and "the intruders"
// abstractly; this is where those words meet assets, and it asserts every fact the
// runtime silently assumes about them:
//
//   1. each mesh loads, with a skeleton and a physics asset (no physics asset = a
//      corpse that cannot fall, silently -- the DeathFall bug, per character);
//   2. each character's ten clips load, are AnimSequences (a montage cannot play in
//      single-node mode), and bind to THAT character's own skeleton -- four separate
//      USkeleton assets, so a clip used across characters silently fails to evaluate;
//   3. clip lengths match the source FBX as measured by the Godot project's anim
//      probe: a wrong-take import shows up here rather than as a man who skates;
//   4. the rig carries the standard Mixamo bone names with the importer's namespace
//      stripped (`mixamorig8:Hips` in the FBX, `Hips` here) -- the fact the gun grip,
//      the wound placement and the whole normalization recipe stand on;
//   5. the bind-pose facing matches FHICharAnim::MeshFacingYawDeg, RECOMPUTED from
//      the two shoulders rather than trusted: this is the check that makes the Sizing
//      test's facing property meaningful, since that one cannot catch a wrong constant;
//   6. the normalized clips are normalized IN THE SAVED ASSET: Hips horizontal travel
//      pinned for the locomotion set, Hips fully pinned plus spine frozen for `aim`,
//      Hips rotation pinned but the arms still recoiling for `shoot` -- because the
//      sim owns position, and a clip that travels moves the mesh away from the body
//      the sim is stepping. This section FAILS UNTIL `tools/import-assets.py` has run
//      `UHIAnimTool` over the content, which is the point: raw and normalized must
//      not test the same.
//   7. `death` really falls -- Hips descends -- asserted from the key data, which is
//      what licenses the ragdoll handover to be a settle rather than the fall.
//
// It also LOGS what cannot be asserted without eyes: each wound bone's ref-pose
// child direction in bone space -- the measurement the provisional wound offsets in
// HIIntruder.cpp are tuned against.
#if WITH_EDITORONLY_DATA
namespace
{
	/** One bone track's keys, sampled from a sequence's data model. */
	bool HICastGetTrack(UAnimSequence* Seq, const TCHAR* Bone,
		TArray<FVector3f>& OutPos, TArray<FQuat4f>& OutRot)
	{
		IAnimationDataModel* Model = Seq->GetDataModel();
		if (Model == nullptr)
		{
			return false;
		}
		FName Target = NAME_None;
		TArray<FName> Tracks;
		Model->GetBoneTrackNames(Tracks);
		const FString Suffix = FString::Printf(TEXT(":%s"), Bone);
		for (const FName& T : Tracks)
		{
			const FString S = T.ToString();
			if (S == Bone || S.EndsWith(Suffix))
			{
				Target = T;
				break;
			}
		}
		if (Target.IsNone())
		{
			return false;
		}
		Model->IterateBoneKeys(Target,
			[&OutPos, &OutRot](const FVector3f& P, const FQuat4f& R, const FVector3f, const FFrameNumber&)
			{
				OutPos.Add(P);
				OutRot.Add(R);
				return true;
			});
		return OutPos.Num() > 0;
	}

	/** Largest horizontal (component X/Y) distance any key sits from the first, in cm. */
	float HICastHorizSpreadCm(const TArray<FVector3f>& Pos)
	{
		float Max = 0.0f;
		for (int32 i = 1; i < Pos.Num(); ++i)
		{
			Max = FMath::Max(Max, FVector2f(Pos[i].X - Pos[0].X, Pos[i].Y - Pos[0].Y).Size());
		}
		return Max;
	}

	/** Largest vertical distance any key sits from the first, in cm. */
	float HICastZSpreadCm(const TArray<FVector3f>& Pos)
	{
		float Max = 0.0f;
		for (int32 i = 1; i < Pos.Num(); ++i)
		{
			Max = FMath::Max(Max, FMath::Abs(Pos[i].Z - Pos[0].Z));
		}
		return Max;
	}

	/** Largest angle any key's rotation makes with the first key's, in degrees. */
	float HICastRotSpreadDeg(const TArray<FQuat4f>& Rot)
	{
		const FQuat First(Rot[0].X, Rot[0].Y, Rot[0].Z, Rot[0].W);
		float MaxDeg = 0.0f;
		for (const FQuat4f& R : Rot)
		{
			const float Dot = FMath::Abs(First | FQuat(R.X, R.Y, R.Z, R.W));
			MaxDeg = FMath::Max(MaxDeg,
				FMath::RadiansToDegrees(2.0f * FMath::Acos(FMath::Clamp(Dot, -1.0f, 1.0f))));
		}
		return MaxDeg;
	}
}
#endif // WITH_EDITORONLY_DATA

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHIUnrealCast,
	"HI.Unreal.Cast",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHIUnrealCast::RunTest(const FString& Parameters)
{
	// ---- the cast IS four different people. "Three men are three people" is the
	// feature; a cycling bug that sent every Id to one tag would be silent.
	const TCHAR* CastTags[] = {
		FHICharAnim::PlayerTag(),
		FHICharAnim::IntruderTagForId(0),
		FHICharAnim::IntruderTagForId(1),
		FHICharAnim::IntruderTagForId(2),
	};
	for (int32 i = 0; i < UE_ARRAY_COUNT(CastTags); ++i)
	{
		for (int32 j = i + 1; j < UE_ARRAY_COUNT(CastTags); ++j)
		{
			TestTrue(*FString::Printf(TEXT("cast slot %d (%s) and slot %d (%s) are different people"),
				i, CastTags[i], j, CastTags[j]),
				FCString::Strcmp(CastTags[i], CastTags[j]) != 0);
		}
	}

	// ---- the standard Mixamo rig, as the importer leaves it: namespace STRIPPED
	// (the source FBX says `mixamorig8:Hips`, the imported skeleton says `Hips`).
	// Every consumer -- the gun grip, the wounds, the normalization recipe, the
	// facing measurement below -- addresses these names, and a re-import that kept
	// the namespace would break all of them at once.
	const TCHAR* RigBones[] = {
		TEXT("Hips"), TEXT("Spine"), TEXT("Spine1"), TEXT("Spine2"), TEXT("Neck"), TEXT("Head"),
		TEXT("LeftShoulder"), TEXT("RightShoulder"), TEXT("LeftArm"), TEXT("RightArm"),
		TEXT("LeftForeArm"), TEXT("RightForeArm"), TEXT("LeftHand"), TEXT("RightHand"),
		TEXT("LeftUpLeg"), TEXT("RightUpLeg"), TEXT("LeftLeg"), TEXT("RightLeg"),
		TEXT("LeftFoot"), TEXT("RightFoot"), TEXT("LeftToeBase"), TEXT("RightToeBase"),
	};

	// ---- the ten clips and the lengths measured from the SAME FBX files by the
	// Godot project's anim probe. +/-5%: tight enough that a wrong take (idle has two,
	// 8.333 s and 9.080 s) or a retimed bake fails, loose enough for import rounding.
	// idle_Anim is logged, not asserted, BECAUSE which take the importer picked is its
	// choice and both are acceptable idles -- but the choice must be visible.
	struct FHIClipCheck
	{
		const TCHAR* Name;
		double Len;      // seconds; 0 = log only
		const TCHAR* Mode;   // the UHIAnimTool mode it must have been normalized with
	};
	const FHIClipCheck Clips[] = {
		{ FHICharAnim::ClipAim(),        2.400, TEXT("aim") },
		{ FHICharAnim::ClipWalk(),       1.033, TEXT("locomotion") },
		{ FHICharAnim::ClipRun(),        0.633, TEXT("locomotion") },
		{ FHICharAnim::ClipCrouchIdle(), 2.100, TEXT("locomotion") },
		{ FHICharAnim::ClipCrouchWalk(), 1.700, TEXT("locomotion") },
		{ FHICharAnim::ClipIdle(),       0.000, TEXT("locomotion") },
		{ FHICharAnim::ClipPistolIdle(), 8.300, TEXT("locomotion") },
		{ FHICharAnim::ClipReload(),     3.300, TEXT("locomotion") },
		{ FHICharAnim::ClipShoot(),      2.667, TEXT("shoot") },
		{ FHICharAnim::ClipDeath(),      3.033, TEXT("none") },
	};

	for (const TCHAR* Tag : CastTags)
	{
		// ---- 1. the mesh, its skeleton, its physics asset.
		USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *FHICharAnim::MeshPathFor(Tag));
		TestNotNull(*FString::Printf(TEXT("%s's mesh loads from %s"), Tag, *FHICharAnim::MeshPathFor(Tag)), Mesh);
		if (Mesh == nullptr)
		{
			AddError(FString::Printf(TEXT("%s: mesh missing -- run tools/import-assets.py; nothing else about this character can be checked"), Tag));
			continue;
		}
		USkeleton* Skel = Mesh->GetSkeleton();
		TestNotNull(*FString::Printf(TEXT("%s's mesh has a skeleton"), Tag), Skel);
		TestNotNull(*FString::Printf(TEXT("%s's mesh has a physics asset, or the corpse cannot fall"), Tag),
			Mesh->GetPhysicsAsset());

		const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
		AddInfo(FString::Printf(TEXT("%s: %d bones, bone[0] is '%s'"),
			Tag, Ref.GetNum(), Ref.GetNum() > 0 ? *Ref.GetBoneName(0).ToString() : TEXT("(none)")));

		// ---- 4. the rig's named bones, all of them, present.
		for (const TCHAR* Bone : RigBones)
		{
			TestTrue(*FString::Printf(TEXT("%s's rig has a '%s' bone"), Tag, Bone),
				Ref.FindBoneIndex(Bone) != INDEX_NONE);
		}

		// ---- 5. the bind-pose facing, MEASURED from the shoulders. Accumulate the
		// ref pose into component space (Comp[i] = local[i] * Comp[parent]), then the
		// line LeftShoulder -> RightShoulder is the rig's lateral axis and
		// lateral x up is the direction the chest faces. This is the number
		// FHICharAnim::MeshFacingYawDeg must carry; a quarter turn of disagreement is
		// silent everywhere else, which is what 2026-10-13's crosshair report cost.
		if (Skel != nullptr)
		{
			const int32 LS = Ref.FindBoneIndex(TEXT("LeftShoulder"));
			const int32 RS = Ref.FindBoneIndex(TEXT("RightShoulder"));
			if (LS != INDEX_NONE && RS != INDEX_NONE)
			{
				TArray<FTransform> Comp;
				Comp.SetNum(Ref.GetNum());
				for (int32 i = 0; i < Ref.GetNum(); ++i)
				{
					const int32 Parent = Ref.GetParentIndex(i);
					Comp[i] = (Parent != INDEX_NONE)
						? Ref.GetRefBonePose()[i] * Comp[Parent]
						: Ref.GetRefBonePose()[i];
				}
				const FVector Lateral = (Comp[RS].GetLocation() - Comp[LS].GetLocation()).GetSafeNormal();
				const FVector Fwd = FVector::CrossProduct(Lateral, FVector::UpVector).GetSafeNormal();
				const float Phi = FMath::RadiansToDegrees(FMath::Atan2(Fwd.Y, Fwd.X));
				TestTrue(*FString::Printf(
					TEXT("%s's bind pose faces yaw %.1f deg, so FHICharAnim::MeshFacingYawDeg() must say so (it says %.1f)"),
					Tag, Phi, FHICharAnim::MeshFacingYawDeg()),
					FMath::Abs(FMath::FindDeltaAngleDegrees(Phi, FHICharAnim::MeshFacingYawDeg())) < 2.0f);
			}
			else
			{
				AddError(FString::Printf(TEXT("%s: no shoulders to measure the facing from"), Tag));
			}
		}

		// ---- the wound bones' ref-pose child directions, LOGGED: bone-space axes are
		// per-rig, the offsets in HIIntruder.cpp are provisional, and this is the
		// measurement they get tuned against (a direction mostly +X means the bone's
		// X runs down the limb, and so on).
		for (const TCHAR* WB : { TEXT("Spine2"), TEXT("RightArm"), TEXT("Spine1"), TEXT("LeftUpLeg"), TEXT("LeftArm") })
		{
			const int32 Bi = Ref.FindBoneIndex(WB);
			if (Bi == INDEX_NONE)
			{
				AddInfo(FString::Printf(TEXT("%s: wound bone %s is missing from the rig"), Tag, WB));
				continue;
			}
			// The first bone whose parent is this one -- FReferenceSkeleton exposes
			// parents, not children, so the child is found by scanning. Bone order in a
			// ref skeleton is parents-before-children, so this is the natural first
			// child, and for these five bones there is exactly one candidate each
			// (Spine2->Neck chain, arms->forearms, upleg->leg) -- logged with its name
			// so a rig that surprises us is visible rather than averaged away.
			int32 Child = INDEX_NONE;
			for (int32 i = 0; i < Ref.GetNum(); ++i)
			{
				if (Ref.GetParentIndex(i) == Bi)
				{
					Child = i;
					break;
				}
			}
			if (Child == INDEX_NONE)
			{
				AddInfo(FString::Printf(TEXT("%s: wound bone %s has no child to measure"), Tag, WB));
				continue;
			}
			const FVector Dir = Ref.GetRefBonePose()[Child].GetLocation().GetSafeNormal();
			AddInfo(FString::Printf(TEXT("%s: %s -> '%s', child dir in bone space (%+.2f, %+.2f, %+.2f)"),
				Tag, WB, *Ref.GetBoneName(Child).ToString(), Dir.X, Dir.Y, Dir.Z));
		}

		// ---- 2, 3, 6, 7: the clips.
		for (const FHIClipCheck& C : Clips)
		{
			UAnimationAsset* Asset = LoadObject<UAnimationAsset>(nullptr,
				*FHICharAnim::ClipPathFor(Tag, C.Name));
			TestNotNull(*FString::Printf(TEXT("%s/%s loads"), Tag, C.Name), Asset);
			if (Asset == nullptr)
			{
				continue;
			}
			UAnimSequence* Seq = Cast<UAnimSequence>(Asset);
			TestTrue(*FString::Printf(TEXT("%s/%s is an AnimSequence -- single-node cannot play anything else"), Tag, C.Name),
				Seq != nullptr);
			if (Seq == nullptr)
			{
				continue;
			}
			// THE BINDING. Four characters, four skeletons; a clip that loaded but bound
			// to the wrong skeleton evaluates to nothing and the body stands in ref pose.
			TestTrue(*FString::Printf(TEXT("%s/%s binds to %s's own skeleton"), Tag, C.Name, Tag),
				Skel != nullptr && Seq->GetSkeleton() == Skel);

			if (C.Len > 0.0)
			{
				TestTrue(*FString::Printf(
					TEXT("%s/%s is %.3f s, within 5%% of the %.3f s measured from the source FBX"),
					Tag, C.Name, Seq->GetPlayLength(), C.Len),
					FMath::IsWithin(Seq->GetPlayLength(), C.Len * 0.95, C.Len * 1.05));
			}
			else
			{
				AddInfo(FString::Printf(TEXT("%s/%s is %.3f s (logged, not asserted: which idle take the importer picked is its choice)"),
					Tag, C.Name, Seq->GetPlayLength()));
			}

#if WITH_EDITORONLY_DATA
			// ---- 6/7: the normalization, asserted from the SAVED key data.
			TArray<FVector3f> HipPos; TArray<FQuat4f> HipRot;
			if (!HICastGetTrack(Seq, TEXT("Hips"), HipPos, HipRot))
			{
				AddError(FString::Printf(TEXT("%s/%s has no Hips track to check normalization against"), Tag, C.Name));
				continue;
			}

			if (FCString::Strcmp(C.Mode, TEXT("locomotion")) == 0)
			{
				// The sim owns position: the mesh must not travel. The vertical
				// component is NOT pinned -- it is the bob, and in the crouch clips
				// it is the crouch.
				const float Spread = HICastHorizSpreadCm(HipPos);
				TestTrue(*FString::Printf(
					TEXT("%s/%s: Hips horizontal travel is pinned (spread %.2f cm; run tools/import-assets.py if this fails)"),
					Tag, C.Name, Spread), Spread < 2.0f);
			}
			else if (FCString::Strcmp(C.Mode, TEXT("aim")) == 0)
			{
				// Godot's strip, in key form: Hips frozen completely (the raw clip
				// travels 0.735 m AND turns 143.2 deg AND drops 25 cm mid-clip), and
				// the spine frozen with it -- while the ARMS still move, or the clip
				// has been over-pinned into a statue and the aiming is gone.
				const float HSpread = HICastHorizSpreadCm(HipPos);
				const float ZSpread = HICastZSpreadCm(HipPos);
				const float HRot = HICastRotSpreadDeg(HipRot);
				TestTrue(*FString::Printf(TEXT("%s/aim: Hips translation fully pinned (%.2f cm horiz, %.2f cm vert)"), Tag, HSpread, ZSpread),
					HSpread < 2.0f && ZSpread < 2.0f);
				TestTrue(*FString::Printf(TEXT("%s/aim: Hips rotation pinned (%.2f deg spread)"), Tag, HRot), HRot < 1.0f);
				for (const TCHAR* SB : { TEXT("Spine"), TEXT("Spine1"), TEXT("Spine2") })
				{
					// FRESH ARRAYS PER BONE, and that is not a style choice: the
					// collector APPENDS, so an array reused across bones measures the
					// concatenation against the FIRST bone's key0 -- which is how this
					// test came to report a "3.28 deg spread" on a perfectly pinned
					// Spine1 on 2026-10-13 (Spine's keys, then Spine1's, measured
					// against Spine). The failure looked like the normalization tool
					// silently not sticking; it was the reader aliasing its own buffer.
					TArray<FVector3f> SpinePos; TArray<FQuat4f> SpineRot;
					if (HICastGetTrack(Seq, SB, SpinePos, SpineRot))
					{
						const float SRot = HICastRotSpreadDeg(SpineRot);
						TestTrue(*FString::Printf(TEXT("%s/aim: %s rotation frozen (%.2f deg spread)"), Tag, SB, SRot), SRot < 1.0f);
					}
				}
				TArray<FVector3f> ArmPos; TArray<FQuat4f> ArmRot;
				if (HICastGetTrack(Seq, TEXT("RightArm"), ArmPos, ArmRot))
				{
					const float ARot = HICastRotSpreadDeg(ArmRot);
					TestTrue(*FString::Printf(TEXT("%s/aim: the RightArm STILL moves (%.2f deg spread), so the aim survived the pinning"), Tag, ARot),
						ARot > 1.0f);
				}
			}
			else if (FCString::Strcmp(C.Mode, TEXT("shoot")) == 0)
			{
				// The Hips swivel (measured: +/-70 deg) is pinned -- the sim owns
				// facing -- but the recoil in the arms is the whole point of the clip
				// and must have survived.
				const float HSpread = HICastHorizSpreadCm(HipPos);
				const float HRot = HICastRotSpreadDeg(HipRot);
				TestTrue(*FString::Printf(TEXT("%s/shoot: Hips pinned (%.2f cm, %.2f deg)"), Tag, HSpread, HRot),
					HSpread < 2.0f && HRot < 1.0f);
				TArray<FVector3f> ArmPos; TArray<FQuat4f> ArmRot;
				if (HICastGetTrack(Seq, TEXT("RightArm"), ArmPos, ArmRot))
				{
					const float ARot = HICastRotSpreadDeg(ArmRot);
					TestTrue(*FString::Printf(TEXT("%s/shoot: the RightArm STILL recoils (%.2f deg spread)"), Tag, ARot),
						ARot > 1.0f);
				}
			}
			else
			{
				// ---- 7. `death` is a REAL FALL, asserted from the keys: the Hips
				// must descend by more than half a metre (measured from the FBX:
				// 0.96 m -> 0.14 m, i.e. 82 cm). The engine-era clip flinched and
				// held upright at 87 cm -- if a re-export ever regresses to that,
				// the ragdoll stops being a settle and has to become the fall again,
				// and this line is what says so.
				const float Drop = HipPos[0].Z - HipPos.Last().Z;
				const float Travel = HICastHorizSpreadCm(HipPos);
				AddInfo(FString::Printf(TEXT("%s/death: Hips drops %.0f cm and travels %.0f cm horizontally (left as authored -- the fall IS the root motion)"),
					Tag, Drop, Travel));
				TestTrue(*FString::Printf(TEXT("%s/death really falls: Hips descends %.0f cm (> 40)"), Tag, Drop),
					Drop > 40.0f);
			}
#endif // WITH_EDITORONLY_DATA
		}

		// ---- the stride table is sane FOR THIS CHARACTER. The travel was measured at
		// import and then pinned out of the asset, so the table in AuthoredCmPerSec is
		// the only surviving record of it and cannot be recomputed from the clips here.
		// What CAN be checked is internal consistency: the moving clips carry positive
		// numbers inside the band a human gait produces (brute's longer stride and the
		// others' shorter ones all sit in it), run outruns walk by a gait's worth, the
		// crouch is slower than the walk, and nothing that does not travel claims to.
		const float AW = FHICharAnim::AuthoredCmPerSec(Tag, FHICharAnim::ClipWalk());
		const float AR = FHICharAnim::AuthoredCmPerSec(Tag, FHICharAnim::ClipRun());
		const float AC = FHICharAnim::AuthoredCmPerSec(Tag, FHICharAnim::ClipCrouchWalk());
		TestTrue(*FString::Printf(TEXT("%s's walk stride (%.1f cm/s) is a walk"), Tag, AW),
			AW >= 100.0f && AW <= 260.0f);
		TestTrue(*FString::Printf(TEXT("%s's run stride (%.1f cm/s) is a run, well over the walk"), Tag, AR),
			AR >= 300.0f && AR <= 750.0f && AR > 2.5f * AW);
		TestTrue(*FString::Printf(TEXT("%s's crouch-walk stride (%.1f cm/s) is slower than the walk"), Tag, AC),
			AC >= 60.0f && AC <= 200.0f && AC < AW);
		for (const TCHAR* Still : { FHICharAnim::ClipIdle(), FHICharAnim::ClipAim(),
			FHICharAnim::ClipCrouchIdle(), FHICharAnim::ClipReload(),
			FHICharAnim::ClipShoot(), FHICharAnim::ClipDeath() })
		{
			TestEqual(*FString::Printf(TEXT("%s/%s claims no travel to stride-match"), Tag, Still),
				FHICharAnim::AuthoredCmPerSec(Tag, Still), 0.0f);
		}

#if WITH_EDITORONLY_DATA
		// ---- THE CROUCH IS ACTUALLY LOW. `DriveCharacter` picks the crouch clips on the
		// sim's crouch flag, but a crouch that plays a clip whose Hips never came down is
		// a standing man sliding around at 2 m/s -- invisible to every other check here,
		// because nothing else reads the HEIGHT the normalization deliberately preserved
		// (pinning horizontal travel while KEEPING the vertical component is the whole
		// reason the crouch clips work at all). Godot's probe measured the source FBX:
		// crouch_idle's Hips sit at ~0.47 m against the standing idle's ~0.96 m. Asserted
		// from the imported keys: both bands, and crouch strictly below stand.
		auto MeanHipsZCm = [](UAnimSequence* S) -> float
		{
			TArray<FVector3f> Pos; TArray<FQuat4f> Rot;
			if (S == nullptr || !HICastGetTrack(S, TEXT("Hips"), Pos, Rot))
			{
				return -1.0f;
			}
			double Sum = 0.0;
			for (const FVector3f& P : Pos)
			{
				Sum += P.Z;
			}
			return static_cast<float>(Sum / Pos.Num());
		};
		UAnimSequence* StandSeq = Cast<UAnimSequence>(LoadObject<UAnimationAsset>(nullptr,
			*FHICharAnim::ClipPathFor(Tag, FHICharAnim::ClipIdle())));
		UAnimSequence* CrouchSeq = Cast<UAnimSequence>(LoadObject<UAnimationAsset>(nullptr,
			*FHICharAnim::ClipPathFor(Tag, FHICharAnim::ClipCrouchIdle())));
		const float StandZ = MeanHipsZCm(StandSeq);
		const float CrouchZ = MeanHipsZCm(CrouchSeq);
		// The standing band is WIDE because the cast is not one size: louise, james and
		// martha idle at ~96 cm and brute at 121 cm -- measured, first run of this check,
		// which an 80-115 band rejected. What the band is for is "a pelvis, not the floor
		// and not a hover"; the crouch-vs-stand relation below is the real invariant, and
		// it is relative so it holds for every scale in the cast.
		TestTrue(*FString::Printf(
			TEXT("%s: the standing idle's Hips mean %.0f cm high (a pelvis, not the floor)"), Tag, StandZ),
			StandZ > 80.0f && StandZ < 135.0f);
		TestTrue(*FString::Printf(
			TEXT("%s: the crouch idle's Hips mean %.0f cm high -- visibly below the %.0f cm stand"),
			Tag, CrouchZ, StandZ),
			CrouchZ > 30.0f && CrouchZ < 70.0f && CrouchZ < StandZ - 20.0f);
#endif // WITH_EDITORONLY_DATA
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHIUnrealWeapons,
	"HI.Unreal.Weapons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHIUnrealWeapons::RunTest(const FString& Parameters)
{
	// "I don't see the pistols." Four separate things have to be true for a gun to be visible
	// in a hand, and the FIRST one was the only one that was broken -- which is why this test
	// asserts all four rather than the one that failed. The rest are the ways it can break
	// again silently, and silence is what this whole class of bug looks like.

	// ---- 1. THE MESHES LOAD. A `LoadObject` by string that misses returns null, and the
	// component then simply has no mesh -- which renders as nothing at all, with no error.
	struct FMesh
	{
		const TCHAR* Path;
		const TCHAR* What;
	};
	const FMesh Meshes[] = {
		{ TEXT("/Game/Weapons/Pistol/Meshes/SM_Pistol.SM_Pistol"), TEXT("the pistol") },
		// Code-built since 2026-10-14 (tools/make-shotgun-glb.py); before that it was the
		// engine's rifle standing in. The assertions below are unchanged by the swap -- that
		// is the point of having written them as a contract instead of as a description.
		{ TEXT("/Game/HIImported/weapons/shotgun/StaticMeshes/shotgun.shotgun"), TEXT("the shotgun (code-built)") },
	};
	for (const FMesh& M : Meshes)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, M.Path);
		TestNotNull(*FString::Printf(TEXT("%s loads"), M.What), Mesh);
		if (Mesh == nullptr)
		{
			continue;
		}

		// ---- 1b. ITS BARREL IS ITS +Y, because the grip rotation written for it assumes so.
		// The fix for "the crosshairs point one way and the defender another" was to stop
		// rotating the pistol at all -- the hand socket's +Y is the pointing direction, so the
		// identity rotation aligns the barrel with the aim. That only holds while the mesh's
		// LONG axis is Y; swap the mesh for one modelled along X or Z and the gun goes sideways
		// again, silently, which is what this catches.
		{
			const FVector Extent = Mesh->GetBounds().BoxExtent;
			TestTrue(*FString::Printf(
				TEXT("%s is longest along +Y (%.1f, %.1f, %.1f), which is the barrel the grip assumes"),
				M.What, Extent.X, Extent.Y, Extent.Z),
				Extent.Y > Extent.X && Extent.Y > Extent.Z);
		}

		// ---- 2. ITS MATERIAL IS OPAQUE. An instance on a masked or translucent parent whose
		// opacity texture did not come across renders as literally nothing, with the mesh
		// present, visible and in the right place -- indistinguishable from this bug.
		UMaterialInterface* MI = Mesh->GetMaterial(0);
		TestNotNull(*FString::Printf(TEXT("%s has a material"), M.What), MI);
		if (MI != nullptr)
		{
			UMaterial* Base = MI->GetMaterial();
			TestNotNull(*FString::Printf(TEXT("%s resolves to a base material"), M.What), Base);
			if (Base != nullptr)
			{
				TestTrue(*FString::Printf(TEXT("%s is opaque, so it cannot render as nothing"), M.What),
					Base->GetBlendMode() == EBlendMode::BLEND_Opaque);
			}
		}
	}

	// ---- 1c. THE WEAPON IS AIMED, WHATEVER THE ARM IS DOING.
	//
	// The props hang off the hand socket, so the POSE decides where the gun points, and the
	// pistol-pose locomotion clips are walk cycles. Measured with `-HIProbe`: standing, the
	// barrel is 3.3 deg off the aim; walking, it is 26-49 deg off and rolled up to 74 deg onto
	// its side. `WeaponAimRotation` corrects the weapon's world rotation to compensate, and this
	// is the property that correction has to hold -- asserted as a pure function, because that
	// is the part that can be checked without a world, a pose or a frame of animation.
	{
		const float Yaws[] = { 0.0f, 0.7f, -1.3f, 3.0f, -2.9f };
		const float Pitches[] = { 0.0f, 0.35f, -0.3f };
		for (const float Yaw : Yaws)
		{
			for (const float Pitch : Pitches)
			{
				const FVector Aim = FHISim::AimDir(Yaw, Pitch);
				const FVector AimUnit = Aim.GetSafeNormal();
				const FRotationMatrix M(AHIPlayerCharacter::WeaponAimRotation(Aim));
				// The BARREL is the mesh's +Y, so it is the rotation's Y axis that must land on
				// the aim. Checking the X axis here would pass while the gun pointed sideways.
				const FVector AxisY = M.GetUnitAxis(EAxis::Y);
				const FVector AxisZ = M.GetUnitAxis(EAxis::Z);
				const float OnAim = FVector::DotProduct(AxisY, AimUnit);
				TestTrue(*FString::Printf(
					TEXT("the weapon's +Y (its barrel) lies on the aim at yaw %.2f pitch %.2f (dot %.5f)"),
					Yaw, Pitch, OnAim), OnAim > 0.99999f);
				TestTrue(*FString::Printf(
					TEXT("and rolls it upright rather than onto its side at yaw %.2f pitch %.2f (up.Z %.4f, off-axis %.5f)"),
					Yaw, Pitch, AxisZ.Z, FVector::DotProduct(AxisZ, AimUnit)),
					AxisZ.Z > 0.0f && FMath::Abs(FVector::DotProduct(AxisZ, AimUnit)) < 1e-4f);
			}
		}
		// The degenerate case, because "upright" has no meaning looking straight up or down and
		// a NaN there would propagate into the component transform rather than fail loudly.
		const FRotationMatrix StraightUp(AHIPlayerCharacter::WeaponAimRotation(FVector(0.0f, 0.0f, 1.0f)));
		TestTrue(TEXT("straight up still yields a finite, unit rotation"),
			StraightUp.GetUnitAxis(EAxis::Y).Equals(FVector(0.0f, 0.0f, 1.0f), 1e-4f));
	}

	// ---- 3. THE GRIP EXISTS. The attachment is by a hand-bone socket, which is resolved
	// against the mesh and its skeleton; the BONE is the thing that must be there, because a
	// swap to a mesh without one leaves the weapon at the man's feet with only a log line.
	// The cast is Mixamo now, whose hand is `RightHand` rather than the mannequin's `hand_r`;
	// `FHICharAnim::HandBoneFor` accepts either, and every one of the four must answer one.
	//
	// (The mannequin-era section 4 asserted the three MF_Pistol pose clips loaded, because
	// armed locomotion was chosen by hand from engine content. The cast carries its own ten
	// clips each, bound to its own skeleton, and their loading is asserted by
	// `HI.Unreal.Cast` -- which is where the whole imported-asset story lives now.)
	const TCHAR* GripCast[] = {
		FHICharAnim::PlayerTag(),
		FHICharAnim::IntruderTagForId(0),
		FHICharAnim::IntruderTagForId(1),
		FHICharAnim::IntruderTagForId(2),
	};
	for (const TCHAR* Tag : GripCast)
	{
		USkeletalMesh* Body = LoadObject<USkeletalMesh>(nullptr, *FHICharAnim::MeshPathFor(Tag));
		TestNotNull(*FString::Printf(TEXT("%s's mesh loads"), Tag), Body);
		if (Body != nullptr && Body->GetSkeleton() != nullptr)
		{
			const FReferenceSkeleton& Ref = Body->GetSkeleton()->GetReferenceSkeleton();
			TestTrue(*FString::Printf(TEXT("%s has a 'RightHand' bone the weapon is gripped with"), Tag),
				Ref.FindBoneIndex(TEXT("RightHand")) != INDEX_NONE);
		}
		else
		{
			AddError(FString::Printf(TEXT("%s's mesh has no skeleton, so no grip can be resolved"), Tag));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

