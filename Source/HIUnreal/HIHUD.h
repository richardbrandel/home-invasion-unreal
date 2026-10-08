#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "HISim.h"
#include "HIHUD.generated.h"

#ifndef HIUNREAL_API
#define HIUNREAL_API
#endif

/**
 * The HUD, drawn on the canvas.
 *
 * Canvas rather than UMG, and that is the project's no-Blueprints rule biting in a good
 * way: a UMG widget is a .uasset, which cannot be authored from text, so a canvas HUD is
 * the only kind this project can build headlessly. It also needs no font asset -- the
 * engine's own fonts are enough for lines of text.
 *
 * The FORMATTING is a static function that takes a round and returns strings, so what the
 * HUD says can be asserted without a canvas, a viewport, or a world.
 */
UCLASS()
class HIUNREAL_API AHIHUD : public AHUD
{
	GENERATED_BODY()

public:
	AHIHUD();

	virtual void DrawHUD() override;

	/**
	 * What the HUD says, one line per entry.
	 *
	 * Pure, and separated from the drawing on purpose: a HUD that reports the wrong number
	 * is a bug you can only see by looking, and looking is the one thing this port cannot
	 * do for itself. Everything that can be checked without eyes is checked here instead.
	 */
	static TArray<FString> RoundLines(const FHISim::FHIRound& Round);
};
