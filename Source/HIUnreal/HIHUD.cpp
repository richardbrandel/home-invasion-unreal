#include "HIHUD.h"

#include "Camera/CameraComponent.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "GlobalRenderResources.h"
#include "HIGameMode.h"
#include "HIPlayerCharacter.h"

namespace
{
	/** `minimap.gd`'s POLY_EVERY: the sight polygon is recomputed one frame in three. */
	constexpr int32 MinimapPolyEvery = 3;

	/**
	 * Even-odd point-in-polygon, in sim space.
	 *
	 * Membership is tested on the polygon OUTLINE rather than by re-deriving what is visible,
	 * and that is deliberate -- it is what the Godot original does, and for the same reason:
	 * the outline is not simple (the edge joining consecutive rays can cross), and an even-odd
	 * crossing count is well-defined on a self-intersecting path where ear-clipping is not.
	 */
	bool PointInPolygon(const TArray<FVector2D>& Poly, const FVector2D& Pt)
	{
		bool bInside = false;
		const int32 Num = Poly.Num();
		for (int32 Idx = 0, Jdx = Num - 1; Idx < Num; Jdx = Idx++)
		{
			const FVector2D& A = Poly[Idx];
			const FVector2D& B = Poly[Jdx];
			if ((A.Y > Pt.Y) != (B.Y > Pt.Y))
			{
				const float T = (Pt.Y - A.Y) / (B.Y - A.Y);
				if (Pt.X < A.X + T * (B.X - A.X))
				{
					bInside = !bInside;
				}
			}
		}
		return bInside;
	}

	/**
	 * Clip a ray from `A` towards `P` to a rectangle, returning the point where it leaves.
	 *
	 * The minimap's sight fan is drawn as triangles from the viewer, and the visibility
	 * polygon's rays run up to 28 m -- far outside the 38 x 32 m the panel shows. An
	 * unclipped fan therefore paints a translucent wedge over the game ABOVE the panel, which
	 * is what the second capture batch showed. Because every triangle shares the viewer as its
	 * apex and the panel is convex and contains him, clipping each ray's far end is exact:
	 * it is the same fan, intersected with the rectangle.
	 */
	FVector2D ClipRayToRect(const FVector2D& A, const FVector2D& P, const FVector2D& Lo,
		const FVector2D& Hi)
	{
		const FVector2D D = P - A;
		float T = 1.0f;
		for (int32 Axis = 0; Axis < 2; ++Axis)
		{
			const float Dir = Axis == 0 ? D.X : D.Y;
			if (FMath::IsNearlyZero(Dir))
			{
				continue;
			}
			const float Bound = Dir > 0.0f ? (Axis == 0 ? Hi.X : Hi.Y) : (Axis == 0 ? Lo.X : Lo.Y);
			T = FMath::Min(T, (Bound - (Axis == 0 ? A.X : A.Y)) / Dir);
		}
		return A + D * FMath::Clamp(T, 0.0f, 1.0f);
	}
}

AHIHUD::AHIHUD()
{
	PrimaryActorTick.bCanEverTick = false;
}

TArray<FString> AHIHUD::RoundLines(const FHISim::FHIRound& Round)
{
	TArray<FString> Lines;
	const FHISim::FHIPlayer& P = Round.Player;

	Lines.Add(FString::Printf(TEXT("HP  %d"), FMath::RoundToInt(P.Hp)));

	// The magazine and the bag separately, because running dry mid-house is the whole
	// tension: one number would hide whether a reload is even possible.
	const int32* Reserve = P.Reserve.Find(P.Weapon);
	// "RELOADING" rather than the count, for the same reason Godot prints it: a man mid-reload
	// glancing at a number cannot tell whether it is going to change on its own.
	const FString Ammo = P.Reloading > 0.0f
		? FString(TEXT("RELOADING"))
		: FString::Printf(TEXT("%d / %d"), P.Mag, Reserve != nullptr ? *Reserve : 0);
	Lines.Add(FString::Printf(TEXT("%s   %s"), *P.Weapon.ToUpper(), *Ammo));

	// Three valuables, so "stolen" is what is NOT still in the world.
	Lines.Add(FString::Printf(TEXT("LOOT STOLEN  %d / 3"), 3 - Round.Remaining()));

	Lines.Add(FString::Printf(TEXT("INTRUDERS DOWN  %d / %d"),
		Round.ThievesDown, Round.Intruders.Num()));

	// The stance, which is worth a line because crouching now genuinely changes the view --
	// see AHIPlayerCharacter::EyePivot. Before that fix this would have read STANDING
	// forever, and a readout that never changes is worse than none. DOWN heads the list for
	// the same reason: a stance line that keeps calling a corpse STANDING -- measured on the
	// 2026-10-14 death frames, reading STANDING beside "You were killed defending the
	// house" -- is a readout arguing with the screen it is drawn on.
	const TCHAR* Stance = !P.bAlive ? TEXT("DOWN")
		: (P.bCrouching ? TEXT("CROUCHING")
		: (P.bSprinting ? TEXT("SPRINTING") : TEXT("STANDING")));
	Lines.Add(FString::Printf(TEXT("%s"), Stance));

	// The police clock, once the first shot has been fired. Reported as a countdown because
	// that is what it is: the sirens end the round whether or not anyone is winning.
	// Read from whichever intruder is still up, because the alarm lives on the body that
	// heard the shot and a corpse's clock must not be the one on screen.
	for (const FHISim::FHIThief& T : Round.Intruders)
	{
		if (T.bAlive && T.Alarm >= 0.0f)
		{
			const float Left = FMath::Max(0.0f, FHISim::PoliceTime - T.Alarm);
			Lines.Add(FString::Printf(TEXT("POLICE  %d:%02d"), FMath::FloorToInt(Left) / 60,
				FMath::FloorToInt(Left) % 60));
			break;
		}
	}

	switch (Round.State)
	{
	case FHISim::EHIRoundState::Won:
		Lines.Add(TEXT("ROUND WON"));
		break;
	case FHISim::EHIRoundState::Lost:
		Lines.Add(TEXT("ROUND LOST"));
		break;
	default:
		break;
	}
	return Lines;
}

bool AHIHUD::ReticleFraction(const FVector& AimDir, const FVector& CameraForward,
	const FVector& CameraRight, const FVector& CameraUp, float HalfFovTangent,
	float AspectRatio, FVector2D& OutFraction)
{
	const float Z = FVector::DotProduct(AimDir, CameraForward);
	if (Z <= 0.01f)
	{
		// Behind the camera. There is no screen position for it, and returning a mirrored
		// one would put the reticle somewhere plausible and wrong.
		return false;
	}
	const float X = FVector::DotProduct(AimDir, CameraRight) / Z;
	const float Y = FVector::DotProduct(AimDir, CameraUp) / Z;
	// 0.5 is the centre; the aim's tangent over the half-FOV tangent is the offset.
	OutFraction = FVector2D(0.5f + (X / HalfFovTangent) * 0.5f * AspectRatio,
		0.5f - (Y / HalfFovTangent) * 0.5f);
	return true;
}

void AHIHUD::DrawCrosshair(const FVector2D& Fraction)
{
	// Dark outline under a light core, and a centre dot. One line of white on a cream wall
	// is the fault the Godot original already fixed; there is no reason to rediscover it.
	const float CX = Canvas->ClipX * Fraction.X;
	const float CY = Canvas->ClipY * Fraction.Y;
	const float Arm = 9.0f;
	const float Gap = 4.0f;
	const float Thick = 2.0f;

	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const bool bOutline = (Pass == 0);
		const float T = bOutline ? Thick + 2.0f : Thick;
		const FLinearColor Colour = bOutline ? FLinearColor(0.0f, 0.0f, 0.0f, 0.55f)
			: FLinearColor(1.0f, 1.0f, 1.0f, 0.95f);
		// Four arms, drawn as filled rectangles. DrawRect rather than Canvas->DrawTile:
		// DrawTile's last parameter is a BLEND MODE, not a colour, and passing a colour there
		// is a compile error rather than a wrong-looking reticle -- which is the good kind.
		DrawRect(Colour, CX - Gap - Arm, CY - T * 0.5f, Arm, T);
		DrawRect(Colour, CX + Gap, CY - T * 0.5f, Arm, T);
		DrawRect(Colour, CX - T * 0.5f, CY - Gap - Arm, T, Arm);
		DrawRect(Colour, CX - T * 0.5f, CY + Gap, T, Arm);
	}
	// The centre dot, so a shot that lands exactly where you aimed is visibly different
	// from one that did not.
	DrawRect(FLinearColor(1.0f, 1.0f, 1.0f, 0.95f), CX - 1.0f, CY - 1.0f, 2.0f, 2.0f);
}

void AHIHUD::DrawHealthBar(const FHISim::FHIRound& Round, float X, float Y)
{
	const float W = 236.0f;
	const float H = 14.0f;
	const float Frac = FMath::Clamp(Round.Player.Hp / 100.0f, 0.0f, 1.0f);

	// Colour-coded, because a bar you have to read the length of is a bar you read too late.
	FLinearColor Fill(0.13f, 0.77f, 0.37f, 1.0f);
	if (Round.Player.Hp <= 25.0f)
	{
		Fill = FLinearColor(0.94f, 0.27f, 0.27f, 1.0f);
	}
	else if (Round.Player.Hp <= 50.0f)
	{
		Fill = FLinearColor(0.96f, 0.62f, 0.04f, 1.0f);
	}

	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), X, Y, W, H);
	DrawRect(Fill, X + 2.0f, Y + 2.0f, (W - 4.0f) * Frac, H - 4.0f);
}

void AHIHUD::DrawMessage(const FString& Text, float SecondsLeft)
{
	if (Text.IsEmpty() || SecondsLeft <= 0.0f)
	{
		return;
	}
	// Fades out over the last half second, so it leaves rather than vanishes.
	const float Alpha = FMath::Clamp(SecondsLeft / 0.5f, 0.0f, 1.0f);
	UFont* Font = GEngine->GetMediumFont();
	const float Scale = 1.5f;
	float W = 0.0f;
	float H = 0.0f;
	GetTextSize(Text, W, H, Font, Scale);

	const float X = Canvas->ClipX * 0.5f - W * 0.5f;
	const float Y = Canvas->ClipY * 0.78f;
	// A plate behind it for the same reason the readout has one: white on a cream wall.
	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.40f * Alpha), X - 14.0f, Y - 6.0f, W + 28.0f, H + 12.0f);
	DrawText(Text, FLinearColor(1.0f, 0.94f, 0.78f, Alpha), X, Y, Font, Scale);
}

void AHIHUD::DrawDamageFlash(float SecondsSinceHit)
{
	// Two seconds of decay, easing out. Drawn as four edge bars rather than a full-screen
	// wash: a red sheet over the whole view hides the thing that is shooting you.
	const float Life = 2.0f;
	if (SecondsSinceHit < 0.0f || SecondsSinceHit > Life)
	{
		return;
	}
	const float Alpha = FMath::Square(1.0f - (SecondsSinceHit / Life)) * 0.55f;
	const FLinearColor Red(0.75f, 0.02f, 0.02f, Alpha);
	const float Band = Canvas->ClipX * 0.10f;
	const float VBand = Canvas->ClipY * 0.12f;
	DrawRect(Red, 0.0f, 0.0f, Canvas->ClipX, VBand);
	DrawRect(Red, 0.0f, Canvas->ClipY - VBand, Canvas->ClipX, VBand);
	DrawRect(Red, 0.0f, 0.0f, Band, Canvas->ClipY);
	DrawRect(Red, Canvas->ClipX - Band, 0.0f, Band, Canvas->ClipY);
}

void AHIHUD::DrawOutcome(const FHISim::FHIRound& Round)
{
	if (Round.State == FHISim::EHIRoundState::Play)
	{
		return;
	}
	const bool bWon = Round.State == FHISim::EHIRoundState::Won;
	const FString Title = bWon ? TEXT("HOME DEFENDED") : TEXT("FAILURE");

	// What actually happened, not a generic line: the Godot original's first failure text
	// said "the van drove off" while the van sat still on screen, which is worse than saying
	// nothing. Each outcome names the thing that ended the round.
	FString Sub;
	if (bWon)
	{
		Sub = FString::Printf(
			TEXT("%d of %d intruders neutralised - %d of 3 valuables still in the house."),
			Round.ThievesDown, Round.Intruders.Num(), Round.Remaining());
	}
	else if (!Round.Player.bAlive)
	{
		Sub = TEXT("You were killed defending the house.");
	}
	else
	{
		const int32 Gone = 3 - Round.Remaining();
		Sub = FString::Printf(TEXT("The van got away with %d of 3 valuables."), Gone);
	}

	const float CX = Canvas->ClipX * 0.5f;
	UFont* Big = GEngine->GetLargeFont();
	UFont* Small = GEngine->GetMediumFont();
	float W = 0.0f;
	float H = 0.0f;
	GetTextSize(Title, W, H, Big, 2.0f);
	DrawText(Title, bWon ? FLinearColor(0.13f, 0.87f, 0.42f) : FLinearColor(0.95f, 0.30f, 0.30f),
		CX - W * 0.5f, Canvas->ClipY * 0.36f, Big, 2.0f);
	GetTextSize(Sub, W, H, Small, 1.3f);
	DrawText(Sub, FLinearColor::White, CX - W * 0.5f, Canvas->ClipY * 0.46f, Small, 1.3f);
	const FString Again = TEXT("[ENTER] to play again");
	GetTextSize(Again, W, H, Small, 1.3f);
	DrawText(Again, FLinearColor(1.0f, 0.85f, 0.35f), CX - W * 0.5f, Canvas->ClipY * 0.52f, Small, 1.3f);
}

FBox2D AHIHUD::MinimapView()
{
	// 38 x 32 m: the house (x -8..8, y -19..-8) with a working margin, and far enough up the
	// drive to take in the van at y 7..9.4. 38/32 = 1.1875 against the panel inner's 1.178,
	// so the distortion is under one percent and nothing has to be letterboxed.
	return FBox2D(FVector2D(-19.0f, -21.0f), FVector2D(19.0f, 11.0f));
}

FVector2D AHIHUD::MinimapFraction(const FVector2D& SimPos)
{
	const FBox2D B = MinimapView();
	const FVector2D Size = B.GetSize();
	if (Size.X <= 0.0f || Size.Y <= 0.0f)
	{
		return FVector2D(0.5f, 0.5f);
	}
	return FVector2D((SimPos.X - B.Min.X) / Size.X, 1.0f - (SimPos.Y - B.Min.Y) / Size.Y);
}

const TArray<FVector2D>& AHIHUD::CachedSightPolygon(const FVector2D& From)
{
	// `SightPolygon.IsEmpty()` rather than only the modulo: the first frame has to build it
	// whether or not that frame happens to be a multiple of three, and a HUD drawn before any
	// round exists must not hand an empty ring to the fan.
	if (SightPolygon.IsEmpty() || MinimapFrame % MinimapPolyEvery == 0)
	{
		SightPolygon = FHISim::VisibilityPolygon(From);
	}
	++MinimapFrame;
	return SightPolygon;
}

void AHIHUD::DrawMinimap(const FHISim::FHIRound& Round)
{
	// Top right, and sized from the sim's own world bounds so the map and the world cannot
	// disagree about how much ground there is.
	const float PanelW = 232.0f;
	const float PanelH = 200.0f;
	const float X0 = Canvas->ClipX - PanelW - 22.0f;
	const float Y0 = 22.0f;
	const float Pad = 10.0f;
	const float InnerW = PanelW - Pad * 2.0f;
	const float InnerH = PanelH - Pad * 2.0f;

	auto ToPanel = [&](const FVector2D& SimPos)
	{
		const FVector2D F = MinimapFraction(SimPos);
		return FVector2D(X0 + Pad + F.X * InnerW, Y0 + Pad + F.Y * InnerH);
	};

	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.42f), X0, Y0, PanelW, PanelH);
	DrawRect(FLinearColor(0.6f, 0.6f, 0.6f, 0.30f), X0, Y0, PanelW, 2.0f);

	// ---- the house floor, DIM, under everything: the layout as known rather than as seen.
	// `minimap.gd` draws its dim layer -- floor, furniture, then walls -- before the lit fan.
	{
		const FVector2D A = ToPanel(FHISim::House.Min);
		const FVector2D B = ToPanel(FHISim::House.Max);
		DrawRect(FLinearColor(0.106f, 0.122f, 0.141f, 0.9f),
			FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y),
			FMath::Abs(B.X - A.X), FMath::Abs(B.Y - A.Y));
	}

	// ---- what the homeowner can SEE, cast as the sim's own visibility polygon.
	//
	// DRAWN AS A FILLED TRIANGLE FAN, which is what the Godot original draws. An earlier
	// version of this function drew a LINE from the homeowner to every vertex of the polygon
	// and claimed in a comment that this "reads as a sight cone -- which is exactly what the
	// Godot original draws". Both halves of that were wrong: `minimap.gd` FILLS the lit floor
	// as a fan of (viewer, pts[Idx], pts[Idx+1]) triangles, and drawing only the rays of a fan
	// leaves the fan's interior empty, so the map rendered as a starburst of spokes radiating
	// to the panel corners. Measured on a capture, not inferred.
	//
	// `Sim::VisibilityPolygon` returns its vertices sorted by angle around the viewer, so the
	// fan is star-shaped about that point by construction and the triangles tile the visible
	// region exactly. The ring is drawn all the way round -- `Idx` wrapping to 0 -- where the
	// Godot original stops two triangles short; Godot's own probe measured that version at
	// 98.85% of the visible area lit, and the missing wedges are the sliver either side of
	// angle zero. The fill colour is `minimap.gd`'s C_FLOOR_LIT.
	const FVector2D PlayerPos = Round.Player.Pos;
	const FVector2D PlayerPx = ToPanel(PlayerPos);
	const TArray<FVector2D>& Poly = CachedSightPolygon(PlayerPos);
	// The panel's inner rectangle. Every sight ray is clipped to it, because the polygon's
	// rays reach 28 m and the panel shows 38 x 32 m -- an unclipped fan paints over the game
	// outside the panel, which is exactly what the captures showed before this line existed.
	const FVector2D InnerLo(X0 + Pad, Y0 + Pad);
	const FVector2D InnerHi(X0 + Pad + InnerW, Y0 + Pad + InnerH);
	const bool bPlayerOnPanel = PlayerPx.X >= InnerLo.X && PlayerPx.X <= InnerHi.X
		&& PlayerPx.Y >= InnerLo.Y && PlayerPx.Y <= InnerHi.Y;
	if (Poly.Num() > 2 && bPlayerOnPanel)
	{
		TArray<FCanvasUVTri> Tris;
		Tris.Reserve(Poly.Num());
		for (int32 Idx = 0; Idx < Poly.Num(); ++Idx)
		{
			FCanvasUVTri Tri;
			Tri.V0_Pos = PlayerPx;
			Tri.V1_Pos = ClipRayToRect(PlayerPx, ToPanel(Poly[Idx]), InnerLo, InnerHi);
			Tri.V2_Pos = ClipRayToRect(PlayerPx, ToPanel(Poly[(Idx + 1) % Poly.Num()]), InnerLo, InnerHi);
			Tri.V0_UV = Tri.V1_UV = Tri.V2_UV = FVector2D::ZeroVector;
			Tri.V0_Color = Tri.V1_Color = Tri.V2_Color = FLinearColor(0.227f, 0.271f, 0.314f, 0.92f);
			Tris.Add(Tri);
		}
		// FCanvasTriangleItem + the engine's own white texture, which is the same pair
		// AHUD::DrawRect uses. There is no FCanvasTriangleItem overload that takes no texture.
		FCanvasTriangleItem Item(Tris, GWhiteTexture);
		Item.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Item);
	}

	// ---- the house: the sim's own wall list, so the map is drawn from the same data the
	// player is actually walking into, and OVER the lit floor. A wall the sight polygon does
	// not reach is drawn dim -- the fog-of-war half of the original, which tests membership on
	// the rect's CENTRE because a canvas has no clip in a draw pass.
	for (const FBox2D& W : FHISim::Walls())
	{
		const FVector2D A = ToPanel(W.Min);
		const FVector2D B = ToPanel(W.Max);
		const bool bSeen = PointInPolygon(Poly, W.GetCenter());
		DrawRect(bSeen ? FLinearColor(0.788f, 0.824f, 0.863f, 0.85f)
					   : FLinearColor(0.204f, 0.227f, 0.255f, 0.85f),
			FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y),
			FMath::Max(FMath::Abs(B.X - A.X), 1.5f), FMath::Max(FMath::Abs(B.Y - A.Y), 1.5f));
	}

	// ---- the van, because it is where the valuables are being taken and the minimap is
	// otherwise a map of a house with no reason to leave it.
	{
		const FVector2D A = ToPanel(FHISim::Van.Min);
		const FVector2D B = ToPanel(FHISim::Van.Max);
		DrawRect(FLinearColor(0.35f, 0.62f, 0.38f, 0.70f),
			FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y),
			FMath::Max(FMath::Abs(B.X - A.X), 2.0f), FMath::Max(FMath::Abs(B.Y - A.Y), 2.0f));
	}

	// ---- the valuables. Shown unconditionally: they are the homeowner's own possessions in
	// his own house, and a defender who does not know where his own safe is would be absurd.
	for (const FHISim::FHILootItem& L : Round.Loot)
	{
		const FVector2D P = ToPanel(L.Pos);
		const FLinearColor Colour = L.bDelivered
			? FLinearColor(0.45f, 0.45f, 0.45f, 0.9f)
			: (L.bTaken ? FLinearColor(0.95f, 0.55f, 0.15f, 0.95f)
						: FLinearColor(0.35f, 0.85f, 0.95f, 0.95f));
		DrawRect(Colour, P.X - 3.0f, P.Y - 3.0f, 6.0f, 6.0f);
	}

	// ---- the intruders, ONLY when the homeowner can see them.
	for (const FHISim::FHIThief& T : Round.Intruders)
	{
		if (!T.bAlive || T.bEscaped)
		{
			continue;
		}
		if (!FHISim::CanSee(PlayerPos, T.Pos, FHISim::ThiefSight))
		{
			continue;
		}
		const FVector2D P = ToPanel(T.Pos);
		DrawRect(FLinearColor(0.95f, 0.25f, 0.25f, 0.95f), P.X - 3.5f, P.Y - 3.5f, 7.0f, 7.0f);
	}

	// ---- the homeowner, last and on top: a dot and a nose, so his facing is readable.
	{
		const float A = Round.Player.Yaw;
		// The sim's yaw 0 faces +Y, which is UP the map; the screen's Y grows downward.
		const FVector2D Nose(PlayerPx.X + FMath::Sin(A) * 9.0f, PlayerPx.Y - FMath::Cos(A) * 9.0f);
		DrawLine(PlayerPx.X, PlayerPx.Y, Nose.X, Nose.Y, FLinearColor(1.0f, 1.0f, 0.4f, 0.95f), 2.0f);
		DrawRect(FLinearColor(1.0f, 1.0f, 1.0f, 0.95f), PlayerPx.X - 3.0f, PlayerPx.Y - 3.0f, 6.0f, 6.0f);
	}
}

void AHIHUD::DrawHUD()
{
	Super::DrawHUD();

	UWorld* World = GetWorld();
	AHIGameMode* Mode = World != nullptr ? World->GetAuthGameMode<AHIGameMode>() : nullptr;
	if (Mode == nullptr || Canvas == nullptr)
	{
		return;
	}

	// `-HIShotNoUI` has to be honoured HERE, and that is a correction rather than a choice.
	// The flag was already being passed to `FScreenshotRequest::RequestScreenshot` as its
	// `bShowUI` argument, and that argument suppresses SLATE -- the engine's own
	// `UGameViewportClient::ProcessScreenShots` is what says so. The HUD is a canvas drawn
	// into the viewport, so it survived, and every frame captured with the switch on came
	// back with the full readout on it. A documented switch with no effect is the
	// "pass-shaped nothing" this project keeps having to guard against; this makes it real.
	if (Mode->ShouldHideHUD())
	{
		return;
	}

	// The reticle first, so it is always under the rest rather than hidden by it.
	{
		const APlayerController* PC = World->GetFirstPlayerController();
		const AHIPlayerCharacter* Pawn = PC ? Cast<AHIPlayerCharacter>(PC->GetPawn()) : nullptr;
		// Always drawn, even with no pawn: a shooter with no reticle is unplayable, and a
		// missing pawn is not a reason to remove the one piece of aiming feedback there is.
		FVector2D Frac(0.5f, 0.5f);
		if (Pawn != nullptr && Pawn->Camera != nullptr)
		{
			const FVector Forward = Pawn->Camera->GetForwardVector();
			const FVector Right = Pawn->Camera->GetRightVector();
			const FVector Up = Pawn->Camera->GetUpVector();
			const FVector Aim = FHISim::AimDir(Mode->Round.Player.Yaw, Mode->Round.Player.Pitch);
			const float HalfFov = FMath::Tan(FMath::DegreesToRadians(Pawn->Camera->FieldOfView * 0.5f));
			const float Aspect = Canvas->ClipY > 0.0f ? Canvas->ClipX / Canvas->ClipY : 1.78f;

			// PROJECTED, not assumed to be the centre. Under the aim scheme this port ships
			// ("fixed") the two coincide, so a hardcoded centre would look identical -- and
			// would silently stop being right the moment the aim cone is switched back on.
			FVector2D Projected;
			if (ReticleFraction(Aim, Forward, Right, Up, HalfFov, Aspect, Projected))
			{
				Frac = Projected;
			}
		}
		DrawCrosshair(Frac);
	}

	const TArray<FString> Lines = RoundLines(Mode->Round);
	const float Scale = 1.6f;
	float Y = 20.0f;
	// A dark plate behind the readout: white text on a cream wall was legible in a test and
	// invisible in the hall, which is the same class of mistake as the original crosshair.
	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.38f), 12.0f, 12.0f, 330.0f,
		34.0f + 26.0f * Scale * (Lines.Num() - 1));

	for (const FString& Line : Lines)
	{
		// The game's own state first, then what just happened: a banner nobody reads because
		// it is buried under a stat readout is a banner that does not exist.
		DrawText(Line, FLinearColor::White, 24.0f, Y, GEngine->GetMediumFont(), Scale);
		Y += 26.0f * Scale;
	}

	DrawHealthBar(Mode->Round, 24.0f, 14.0f + 26.0f * Scale * Lines.Num() + 6.0f);

	// NOTE: the raw event names used to be printed down here. They were a debugging aid that
	// shipped into the picture, and they read as an unfinished game: a column of the words
	// "playerShot", "thiefShot" and "grabbed" on top of the firefight. Every one of them now
	// has a human message -- see the Message handling in AHIGameMode::Tick -- so the dump is
	// gone rather than merely hidden.

	DrawMinimap(Mode->Round);

	// The damage edge goes UNDER the readout and the banner but over the world.
	DrawDamageFlash(Mode->RoundSeconds - Mode->LastDamageTime);
	DrawMessage(Mode->Message, Mode->MessageTime);
	DrawOutcome(Mode->Round);
}
