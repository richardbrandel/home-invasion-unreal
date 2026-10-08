#include "HIHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "HIGameMode.h"

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
	Lines.Add(FString::Printf(TEXT("%s   %d / %d"),
		*P.Weapon.ToUpper(), P.Mag, Reserve != nullptr ? *Reserve : 0));

	// Three valuables, so "stolen" is what is NOT still in the world.
	Lines.Add(FString::Printf(TEXT("LOOT STOLEN  %d / 3"), 3 - Round.Remaining()));

	Lines.Add(FString::Printf(TEXT("INTRUDERS DOWN  %d / %d"),
		Round.ThievesDown, Round.Intruders.Num()));

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

void AHIHUD::DrawHUD()
{
	Super::DrawHUD();

	UWorld* World = GetWorld();
	AHIGameMode* Mode = World != nullptr ? World->GetAuthGameMode<AHIGameMode>() : nullptr;
	if (Mode == nullptr || Canvas == nullptr)
	{
		return;
	}

	const TArray<FString> Lines = RoundLines(Mode->Round);
	const float Scale = 1.6f;
	float Y = 24.0f;
	for (const FString& Line : Lines)
	{
		// The game's own state first, then what just happened: a banner nobody reads because
		// it is buried under a stat readout is a banner that does not exist.
		DrawText(Line, FLinearColor::White, 24.0f, Y, GEngine->GetMediumFont(), Scale);
		Y += 26.0f * Scale;
	}

	// The frame's events, so the round can say what it just did rather than only what is
	// true. Last, so nothing shifts as they come and go.
	float Ey = Canvas->ClipY - 120.0f;
	for (const FHISim::FHIEvent& Ev : Mode->LastEvents)
	{
		DrawText(Ev.Type, FLinearColor(1.0f, 0.8f, 0.2f), 24.0f, Ey, GEngine->GetMediumFont(), Scale);
		Ey += 26.0f * Scale;
	}
}
