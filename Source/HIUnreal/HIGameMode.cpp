#include "HIGameMode.h"

#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PointLight.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HIAudio.h"
#include "HICharAnim.h"
#include "HIHouse.h"
#include "HIHUD.h"
#include "HIIntruder.h"
#include "HILootProp.h"
#include "HIMarks.h"
#include "HIMaterials.h"
#include "HIPlayerCharacter.h"
#include "InputKeyEventArgs.h"
#include "GameFramework/PlayerInput.h"
#include "HIUnits.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * The raw text after `Key` on the command line, up to the next whitespace.
	 *
	 * NOT `FParse::Value`, and this is the whole reason it exists. `FParse::Value` treats a
	 * COMMA as a separator, so `-HIShotPos=0,16` comes back as `"0"` -- measured, not
	 * assumed: it reported `HI: -HIShotPos='0' is not 'x,y' -- ignored`, and the capture was
	 * silently taken from the spawn point instead. A failure that produces a perfectly
	 * plausible screenshot is the kind that costs an afternoon, so the two-value argument is
	 * read here by hand rather than through a parser with rules of its own.
	 */
	bool RawCommandValue(const TCHAR* Key, FString& Out)
	{
		const FString Cmd = FCommandLine::Get();
		const int32 At = Cmd.Find(Key, ESearchCase::IgnoreCase, ESearchDir::FromStart);
		if (At == INDEX_NONE)
		{
			return false;
		}
		int32 End = At + FCString::Strlen(Key);
		const int32 Start = End;
		while (End < Cmd.Len() && !FChar::IsWhitespace(Cmd[End]))
		{
			++End;
		}
		Out = Cmd.Mid(Start, End - Start);
		return !Out.IsEmpty();
	}

	/** Bullet holes and blood are pooled: a long firefight must not leak actors. */
	constexpr int32 MarkPoolSize = 96;

	/**
	 * Tracers and muzzle flashes, pooled separately from the marks.
	 *
	 * Their lifetimes are opposites -- a bullet hole is meant to stay, a tracer is meant to
	 * be gone in fifty milliseconds -- so sharing one ring would mean a bullet hole
	 * disappearing because a tracer needed its slot.
	 */
	constexpr int32 FlashPoolSize = 32;

	const FLinearColor HoleColour = FLinearColor(0.03f, 0.03f, 0.03f);
}

AHIGameMode::AHIGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	// PrePhysics, and it matters: the round has to advance before anything reads it to
	// place a body or draw a frame, or every view is one step behind the model.
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	// The pawn is a view of the sim's player, so the framework spawning it is fine -- it
	// owns nothing, and SyncFromSim puts it where the round says it is.
	DefaultPawnClass = AHIPlayerCharacter::StaticClass();
	// Likewise the controller: this port reads input through the game mode rather than
	// through a PlayerController subclass, so the plain one is correct.
	PlayerControllerClass = APlayerController::StaticClass();

	// A canvas HUD rather than a UMG widget: a widget is a .uasset and cannot be authored
	// from text, and this project builds headlessly. See HIHUD.h.
	HUDClass = AHIHUD::StaticClass();
}

void AHIGameMode::HandleCommandLine()
{
	const TCHAR* Cmd = FCommandLine::Get();

	// Thieves first: the round cannot be started before its size is known.
	int32 Thieves = 0;
	if (FParse::Value(Cmd, TEXT("HIThieves="), Thieves) && Thieves > 0)
	{
		IntruderCount = Thieves;
	}

	FParse::Value(Cmd, TEXT("HIShot="), ShotName);

	// `-HISnap=NAME`: the MANUAL capture -- the same frames, series and quit as `-HIShot`,
	// with no scripted player and no autopilot. See `bSnapRun` in the header for the dispute
	// that made it: a "no gun in her hands" report from a real session that NO scripted frame
	// could reproduce, because a scripted frame is never what a person at the keyboard sees.
	// Parsed BEFORE the -HIShotSeries check below so a series has a name to write to.
	{
		FString SnapName;
		if (FParse::Value(Cmd, TEXT("HISnap="), SnapName) && !SnapName.IsEmpty())
		{
			if (!ShotName.IsEmpty())
			{
				UE_LOG(LogTemp, Warning,
					TEXT("HI: -HISnap=%s ignored -- -HIShot=%s already named this run, and one run cannot be both scripted and manual"),
					*SnapName, *ShotName);
			}
			else
			{
				ShotName = SnapName;
				bSnapRun = true;
			}
		}
	}
	bPlaytest = FParse::Param(Cmd, TEXT("HIPlaytest"));
	FParse::Value(Cmd, TEXT("HIShotAfter="), ShotAfterSeconds);

	// `-HIShotSeries=t1,t2,...`: MANY frames from ONE run. One build and one launch for a whole
	// sweep of poses, which is the difference between seeing a walk cycle and guessing at one
	// from a picture of the man standing still -- see the note on the capture gate in
	// `TickHarness`, which is exactly the trap this project fell into twice.
	{
		FString SeriesArg;
		// `bShouldStopOnSeparator=false` IS LOAD-BEARING, and this is the THIRD time this project
		// has been bitten by it. `FParse::Value` stops at the first comma by default, so
		// `-HIShotSeries=1,2,3,4,5` parses as `1` and the run silently takes ONE frame and quits
		// -- a sweep that reports success and is not one. The same default made
		// `-HIGunRot=0,-90,0` parse as `0` (four candidate rotations, four identical
		// measurements that looked like evidence about the rig) and made `-HIShotPos=0,16`
		// ignore the position outright.
		if (FParse::Value(Cmd, TEXT("HIShotSeries="), SeriesArg, /*bShouldStopOnSeparator=*/false))
		{
			TArray<FString> Parts;
			SeriesArg.ParseIntoArray(Parts, TEXT(","), /*bCullEmpty=*/true);
			for (const FString& Part : Parts)
			{
				const float At = FCString::Atof(*Part);
				if (At > 0.0f)
				{
					ShotSeries.Add(At);
				}
			}
			// Ascending, because the schedule is consumed in order and `t2,t1` should not turn
			// into a capture that can never be reached.
			ShotSeries.Sort();
		}
	}
	// A series with no name has nowhere to write, and a silent no-op capture is the exact
	// failure this harness has already been bitten by. Say so instead.
	if (ShotSeries.Num() > 0 && ShotName.IsEmpty())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("HI: -HIShotSeries lists %d frame(s) but -HIShot=NAME is missing, so there is nowhere to write them -- ignored"),
			ShotSeries.Num());
		ShotSeries.Reset();
	}

	// `-HIWeaponAt=t:key,t:key,...`: switch weapons at chosen SIM seconds WITHIN one run.
	//
	// The point is the launch cost. A pistol+shotgun sweep used to be two runs, and each run
	// pays about eighteen seconds of engine startup for the privilege; this folds them into one.
	//
	// `bShouldStopOnSeparator=false` AGAIN -- the fourth site in this file. Without it
	// `-HIWeaponAt=2:shotgun,4:pistol` parses as `2:shotgun`, the second switch silently never
	// happens, and the sweep covers half of what it claims while reporting success.
	{
		FString ScheduleArg;
		if (FParse::Value(Cmd, TEXT("HIWeaponAt="), ScheduleArg, /*bShouldStopOnSeparator=*/false))
		{
			TArray<FString> Cues;
			ScheduleArg.ParseIntoArray(Cues, TEXT(","), /*bCullEmpty=*/true);
			for (const FString& Cue : Cues)
			{
				FString TimePart;
				FString KeyPart;
				if (!Cue.Split(TEXT(":"), &TimePart, &KeyPart))
				{
					UE_LOG(LogTemp, Warning, TEXT("HI: -HIWeaponAt entry '%s' is not 'seconds:weapon' -- ignored"), *Cue);
					continue;
				}
				FHIWeaponCue Entry;
				Entry.Time = FCString::Atof(*TimePart);
				Entry.Key = KeyPart.TrimStartAndEnd();
				// Validated HERE as well as in the sim, so a typo is loud at startup rather than
				// silently doing nothing at the exact moment the sweep needed it.
				if (Entry.Key.IsEmpty() || Entry.Time < 0.0f || FHISim::Weapon(Entry.Key).Mag <= 0)
				{
					UE_LOG(LogTemp, Warning,
						TEXT("HI: -HIWeaponAt entry '%s' is not 'seconds:pistol|shotgun' -- ignored"), *Cue);
					continue;
				}
				WeaponSchedule.Add(Entry);
			}
			WeaponScheduleFired.Init(false, WeaponSchedule.Num());
			if (WeaponSchedule.Num() > 0)
			{
				UE_LOG(LogTemp, Display, TEXT("HI: -HIWeaponAt scheduled %d weapon switch(es) inside this run"),
					WeaponSchedule.Num());
			}
		}
	}
	FParse::Value(Cmd, TEXT("HISoak="), SoakSeconds);
	bHideUIForShot = FParse::Param(Cmd, TEXT("HIShotNoUI"));
	const bool bAutoAsked = FParse::Param(Cmd, TEXT("HIAuto"));
	// `-HINoAuto` is the PASSIVE probe, and it is the Unreal answer to the Godot project's
	// `probe_heist.gd`. A soak with the autopilot on measures how fast an aimbot can clear the
	// house -- measured, 5.0 s for three intruders -- which says nothing about whether the
	// HEIST works. Standing the homeowner still and letting the intruders work is the test
	// that catches a thief who cannot path, cannot grab, or cannot leave.
	const bool bAutoOff = FParse::Param(Cmd, TEXT("HINoAuto"));
	bNoThiefDamage = FParse::Param(Cmd, TEXT("HINoThiefDamage"));
	// Start with the house dark, so the effect of the light switch can be MEASURED rather than
	// asserted: two passive soaks, one lit and one dark, is the only way to say whether the
	// verb changes anything a player would feel.
	if (FParse::Param(Cmd, TEXT("HIDark")))
	{
		bLightsOn = false;
	}
	int32 Rounds = 0;
	if (FParse::Value(Cmd, TEXT("HIRounds="), Rounds) && Rounds > 1)
	{
		RoundsToPlay = Rounds;
	}
	// A posed capture wants the world to hold still, so an explicit position turns the
	// autopilot off unless it was asked for by name. A capture WITHOUT a position is a
	// picture of the round in progress, which needs the round driven or it photographs an
	// empty hallway for eight seconds.
	FString PositionArg;
	RawCommandValue(TEXT("HIShotPos="), PositionArg);
	const bool bPosed = !PositionArg.IsEmpty();
	// A snap run is NOT scripted and must not turn the autopilot on either: its whole value is
	// that nothing but the shutter differs from the session the report came from.
	bScripted = (!ShotName.IsEmpty() && !bSnapRun) || SoakSeconds > 0.0f;
	bAuto = bAutoAsked || (SoakSeconds > 0.0f && !bAutoOff) || (!ShotName.IsEmpty() && !bPosed && !bSnapRun);
	if (bAutoOff)
	{
		bAuto = false;
	}

	if (bScripted || bAuto || bSnapRun)
	{
		// "capture run", not "scripted run": a snap run is captured but NOT scripted, and the
		// line that says which kind it was is the line a reader trusts to know what drove the
		// player. capture.sh greps both spellings.
		UE_LOG(LogTemp, Display, TEXT("HI: capture run -- thieves=%d shot='%s' after=%.1f series=%d soak=%.1f auto=%d posed=%d snap=%d no_thief_damage=%d"),
			IntruderCount, *ShotName, ShotAfterSeconds, ShotSeries.Num(), SoakSeconds, bAuto ? 1 : 0, bPosed ? 1 : 0,
			bSnapRun ? 1 : 0, bNoThiefDamage ? 1 : 0);
		// Printed rather than assumed: the screenshot path is the difference between seeing
		// a frame and believing one exists, and this project has already been bitten once by
		// a file that was never written.
		UE_LOG(LogTemp, Display, TEXT("HI: screenshots will be written under %s"), *FPaths::ScreenShotDir());
	}
}

void AHIGameMode::BeginPlay()
{
	Super::BeginPlay();

	// BEFORE the round exists: the command line can change how many intruders there are.
	HandleCommandLine();

	Round = FHISim::FHIRound::Start(IntruderCount);
	ApplyStartWeapon();

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// The house FIRST, so the walls exist before any body is placed among them. It is
	// generated from the sim's own rectangle list rather than from a level, so the geometry
	// the player collides with and the geometry the AI reasons about cannot drift apart.
	FActorSpawnParameters HouseParams;
	HouseParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AHIHouse* House = World->SpawnActor<AHIHouse>(
		AHIHouse::StaticClass(), FTransform::Identity, HouseParams))
	{
		House->Build();
	}

	SetupLighting();
	if (!bLightsOn)
	{
		// SetupLighting spawns them lit, so a dark start pushes the flag onto them. NOT the
		// toggle: that flips the flag back and the run is lit after all.
		ApplyLights();
	}
	SpawnLoot();

	IntruderActors.Reset();
	for (const FHISim::FHIThief& T : Round.Intruders)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AHIIntruder* Actor = World->SpawnActor<AHIIntruder>(
			AHIIntruder::StaticClass(), FTransform::Identity, Params);
		if (Actor != nullptr)
		{
			Actor->ThiefId = T.Id;
			Actor->SyncFromSim(T, 1.0f / 60.0f);
			IntruderActors.Add(Actor);
		}
	}

	// A capture can be aimed, because a screenshot from the spawn point shows one room and
	// the questions worth asking are usually about a different one. Mirrors game.gd's
	// --shot-pos / --shot-yaw / --shot-pitch, and exists for the same reason: without it,
	// the only photographable moment is the first two seconds.
	FString PosArg;
	if (RawCommandValue(TEXT("HIShotPos="), PosArg))
	{
		// "x,y" on one switch -- the sim's plane, so the second number is the sim's Y.
		TArray<FString> Bits;
		PosArg.ParseIntoArray(Bits, TEXT(","), true);
		if (Bits.Num() >= 2)
		{
			Round.Player.Pos = FVector2D(FCString::Atof(*Bits[0]), FCString::Atof(*Bits[1]));
			UE_LOG(LogTemp, Display, TEXT("HI: posed at (%.2f, %.2f) from '%s'"),
				Round.Player.Pos.X, Round.Player.Pos.Y, *PosArg);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("HI: -HIShotPos='%s' is not 'x,y' -- ignored"), *PosArg);
		}
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("HI: no -HIShotPos; the homeowner stays at the spawn (%.2f, %.2f)"),
			Round.Player.Pos.X, Round.Player.Pos.Y);
	}

	float Deg = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("HIShotYaw="), Deg))
	{
		Round.Player.Yaw = FMath::DegreesToRadians(Deg);
	}
	if (FParse::Value(FCommandLine::Get(), TEXT("HIShotPitch="), Deg))
	{
		Round.Player.Pitch = FMath::Clamp(FMath::DegreesToRadians(Deg),
			FHISim::PitchMin, FHISim::PitchMax);
	}
}

void AHIGameMode::SetupLighting()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// ---- the sun. Yaw 216 rather than 36, and that is inherited from the Godot original:
	// a light emits along its own -Z, so the other side of the compass backlights the
	// intruder into an unreadable silhouette as he walks up the drive.
	if (ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(
		ADirectionalLight::StaticClass(), FTransform(FRotator(-48.0f, 216.0f, 0.0f)), Params))
	{
		if (ULightComponent* Light = Sun->GetLightComponent())
		{
			// MOVABLE, and it is not a preference: this project has no level and therefore no
			// built lighting, and a Static light that has never been built renders nothing at
			// all -- which looks exactly like the sun not being spawned.
			Light->SetMobility(EComponentMobility::Movable);
			Light->SetIntensity(4.0f);
			Light->SetLightColor(FLinearColor(1.0f, 0.95f, 0.86f));
			Light->SetCastShadows(true);
		}
	}

	// ---- a sky to capture, so the outdoor half reads as outdoors and the sky light has
	// something other than black to bounce into the rooms.
	if (ASkyAtmosphere* Sky = World->SpawnActor<ASkyAtmosphere>(
		ASkyAtmosphere::StaticClass(), FTransform::Identity, Params))
	{
		(void)Sky;
	}

	// ---- ambient fill. Without this the shadowed side of every wall is pure black and the
	// house reads as a set of cardboard cut-outs.
	if (ASkyLight* SkyLight = World->SpawnActor<ASkyLight>(
		ASkyLight::StaticClass(), FTransform::Identity, Params))
	{
		if (USkyLightComponent* Fill = SkyLight->GetLightComponent())
		{
			Fill->SetMobility(EComponentMobility::Movable);
			Fill->SourceType = SLS_CapturedScene;
			Fill->SetIntensity(1.0f);
			// The lower hemisphere is NOT forced black: doing so removes the bounce off the
			// lawn, and the whole ground floor is lit by it.
			Fill->bLowerHemisphereIsBlack = false;
			// Captured at spawn. Recaptured once more a moment later by the tick, because a
			// capture taken on the very first frame sees a world that has not been drawn yet.
			Fill->RecaptureSky();
		}
	}

	// ---- distance fog, and it is doing a job rather than adding atmosphere.
	// The sky atmosphere draws nothing BELOW the horizon line, so a ground plane of any finite
	// size leaves a black band between its far edge and the sky. The first renders had exactly
	// that. The lawn is now enormous, which pushes the seam out to within a fraction of a
	// degree of the horizon; the fog is what makes the remaining distance read as haze rather
	// than as a hard edge, and it also gives the house a sense of scale from the drive.
	if (AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>(
		AExponentialHeightFog::StaticClass(), FTransform::Identity, Params))
	{
		if (UExponentialHeightFogComponent* Air = Fog->GetComponent())
		{
			Air->SetFogDensity(0.010f);
			Air->SetFogHeightFalloff(0.02f);
			// A short start distance keeps the interiors clear: fog that begins inside the
			// house would wash out every room the moment a door opened.
			Air->SetStartDistance(1200.0f);
			Air->SetFogInscatteringColor(FLinearColor(0.62f, 0.72f, 0.86f));
		}
	}

	// ---- interior lamps. The house HAS a roof, so the sun does not reach inside and the
	// rooms would otherwise be lit only by the sky fill leaking through the doors. These are
	// placed by hand against the sim's own room layout rather than generated, because a
	// light is a DESIGN object: where a lamp hangs is not derivable from a rectangle list.
	// The positions match the ceiling roses in AHIHouse::BuildDressing -- a lamp with no
	// fitting above it is a room lit by nothing.
	struct FLamp
	{
		float X;
		float Y;
		float Intensity;
	};
	// `LampSpecs`, NOT `Lamps`: `Lamps` is the member the light switch iterates, and a local
	// array of the same name shadows it -- which the -Wshadow the project builds with turns
	// into a compile error rather than a silent surprise, and that is the only reason this
	// was caught before it became a switch that toggled a table nobody could see.
	static const FLamp LampSpecs[] =
	{
		// 800-ish lumens, which is one ordinary LED bulb rather than the 2600 this started at.
		// The first value was chosen against a fixed exposure that has since been reversed,
		// and against auto-exposure a house lit to 2600 lumens a room looks like an office.
		{ -5.00f, -11.00f,  900.0f },   // living room
		{  4.00f, -11.00f,  800.0f },   // kitchen
		{ -4.00f, -16.50f,  700.0f },   // bedroom
		{  4.00f, -16.50f,  700.0f },   // study
		{  0.00f, -14.00f,  700.0f },   // hall
	};
	for (const FLamp& L : LampSpecs)
	{
		if (APointLight* Lamp = World->SpawnActor<APointLight>(
			APointLight::StaticClass(),
			FTransform(FRotator::ZeroRotator, FVector(L.X, L.Y, 2.60f) * HI_METERS_TO_UU), Params))
		{
			Lamps.Add(Lamp);
			if (UPointLightComponent* Bulb = Cast<UPointLightComponent>(Lamp->GetLightComponent()))
			{
				Bulb->SetMobility(EComponentMobility::Movable);
				// Lumens, because that is what a lamp is specified in. Setting candelas and
				// calling it "intensity" is how a room ends up either unlit or nuclear.
				Bulb->SetIntensityUnits(ELightUnits::Lumens);
				Bulb->SetIntensity(L.Intensity);
				LampLitIntensity.Add(L.Intensity);
				Bulb->SetLightColor(FHIMaterials::WarmLight);
				Bulb->SetAttenuationRadius(9.0f * HI_METERS_TO_UU);
				Bulb->SetCastShadows(true);
			}
		}
	}
}

void AHIGameMode::SpawnLoot()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	LootActors.Reset();
	for (int32 Idx = 0; Idx < Round.Loot.Num(); ++Idx)
	{
		AHILootProp* Prop = World->SpawnActor<AHILootProp>(
			AHILootProp::StaticClass(), FTransform::Identity, Params);
		if (Prop != nullptr)
		{
			Prop->Init(Idx, Round.Loot[Idx].Label);
			LootActors.Add(Prop);
		}
	}
}


void AHIGameMode::LookDelta(float MouseX, float MouseY, float& OutYawDelta, float& OutPitchDelta)
{
	// Pure, so the signs can be asserted without a mouse -- see `HI.Unreal.LookMapping`.
	//
	// Straight transcription of `game.gd`'s `_input`, fixed branch:
	//
	//     yaw   -= event.relative.x * AIM_SENS_X      # 0.0022
	//     pitch  = clamp(pitch + event.relative.y * AIM_SENS_Y, ...)   # 0.0018
	//
	// MINUS on yaw and PLUS on pitch, and the first version of this had both the wrong way
	// round. The pitch sign reads backwards until you remember that Godot's mouse `relative.y`
	// grows downwards and the sim's positive pitch is "looking down", so the two agree.
	OutYawDelta = -MouseX * GodotYawRadiansPerPixel;
	OutPitchDelta = MouseY * GodotPitchRadiansPerPixel;
}

void AHIGameMode::PlaytestTick(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	APlayerController* PC = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (PC == nullptr)
	{
		return;
	}

	const float Dt = FMath::Max(DeltaSeconds, 1.0f / 240.0f);
	PlaytestTime += Dt;

	// Each step runs for a second of round time, then the next key goes down. Injected with
	// InputKey so it enters the input stack the same way a keyboard event does.
	// `CreateSimulated` is the engine's own factory for exactly this -- a synthetic key event
	// that goes in through the same stack a keyboard uses. `FInputKeyParams`, the obvious
	// first choice, is deprecated in 5.6 and only forward-declared where it is used.
	auto Hold = [&](const FKey& Key, bool bDown)
	{
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key,
			bDown ? EInputEvent::IE_Pressed : EInputEvent::IE_Released, /*AmountDepressed=*/1.0f));
	};

	const int32 Step = FMath::FloorToInt(PlaytestTime);
	if (Step != PlaytestStep)
	{
		// Release everything from the previous step before starting the next.
		for (const FKey& Key : { EKeys::W, EKeys::A, EKeys::S, EKeys::D, EKeys::LeftMouseButton })
		{
			Hold(Key, false);
		}
		PlaytestStep = Step;

		switch (Step)
		{
		case 1:   // baseline, nothing held
			PlaytestStartPos = Round.Player.Pos;
			PlaytestStartYaw = Round.Player.Yaw;
			PlaytestStartPitch = Round.Player.Pitch;
			PlaytestStartMag = Round.Player.Mag;
			UE_LOG(LogTemp, Display, TEXT("PLAYTEST step1 baseline pos=(%.2f,%.2f) yaw=%.3f mag=%d"),
				Round.Player.Pos.X, Round.Player.Pos.Y, Round.Player.Yaw, Round.Player.Mag);
			break;
		case 2:
			UE_LOG(LogTemp, Display, TEXT("PLAYTEST step2 holding W"));
			Hold(EKeys::W, true);
			break;
		case 3:
			PlaytestPosAfterW = FVector2D::Distance(Round.Player.Pos, PlaytestStartPos);
			UE_LOG(LogTemp, Display,
				TEXT("PLAYTEST step3 after 1s of W: moved %.3f m, pos=(%.2f,%.2f) -- forward is %s"),
				PlaytestPosAfterW, Round.Player.Pos.X, Round.Player.Pos.Y,
				(Round.Player.Pos.Y > PlaytestStartPos.Y ? TEXT("+Y") : TEXT("-Y")));
			// Look is NOT injected here: there is no `APlayerController::InputAxis`, and the
			// view reads the raw mouse delta rather than an axis, so there is nothing honest
			// to synthesise. The SIGN of that mapping is what was wrong, and a sign is a pure
			// function of two numbers -- so it is asserted in `HI.Unreal.LookMapping`
			// instead, which is a better test than a fake mouse could be.
			break;
		case 4:
			PlaytestYawAfterMouse = Round.Player.Yaw - PlaytestStartYaw;
			UE_LOG(LogTemp, Display,
				TEXT("PLAYTEST step4 yaw unchanged at %.3f (look is covered by HI.Unreal.LookMapping)"),
				Round.Player.Yaw);
			UE_LOG(LogTemp, Display, TEXT("PLAYTEST step4 holding fire"));
			Hold(EKeys::LeftMouseButton, true);
			break;
		case 5:
			PlaytestMagAfterFire = Round.Player.Mag;
			UE_LOG(LogTemp, Display,
				TEXT("PLAYTEST step5 after 1s of fire: mag %d -> %d (spent %d)"),
				PlaytestStartMag, PlaytestMagAfterFire, PlaytestStartMag - PlaytestMagAfterFire);
			break;
		case 6:
			// The verdict, in one line, and it is the check that could not fail before.
			UE_LOG(LogTemp, Display, TEXT("PLAYTEST RESULT moved=%.2f m yaw=%.3f rad shots_fired=%d"),
				PlaytestPosAfterW, PlaytestYawAfterMouse, PlaytestStartMag - PlaytestMagAfterFire);
			UE_LOG(LogTemp, Display, TEXT("PLAYTEST %s"),
				(PlaytestPosAfterW > 0.5f && FMath::Abs(PlaytestYawAfterMouse) > 0.001f
					&& PlaytestMagAfterFire < PlaytestStartMag)
					? TEXT("PASS -- keys move, look turns, fire spends ammunition")
					: TEXT("FAIL -- at least one of move/look/fire did nothing through the real input path"));
			break;
		case 7:
			// `[ESC]` must give the mouse back. This is the same real input path the keys above
			// use, so it tests the hatch rather than a private call to it -- and it is worth
			// testing because the failure mode is a player with no cursor and no title bar, which
			// is unrecoverable from inside the game.
			UE_LOG(LogTemp, Display, TEXT("PLAYTEST step7 pressing ESC -- the mouse must come back"));
			Hold(EKeys::Escape, true);
			break;
		case 8:
			UE_LOG(LogTemp, Display, TEXT("PLAYTEST step8 after ESC: mouse_released=%d showCursor=%d"),
				bMouseReleased ? 1 : 0, (PC != nullptr && PC->bShowMouseCursor) ? 1 : 0);
			UE_LOG(LogTemp, Display, TEXT("PLAYTEST %s"),
				(bMouseReleased && PC != nullptr && PC->bShowMouseCursor)
					? TEXT("PASS -- [ESC] gives the mouse back and shows the cursor")
					: TEXT("FAIL -- [ESC] did not release the mouse; the player would have no way out"));
			break;
		default:
			break;
		}
	}
}

void AHIGameMode::ClaimInput()
{
	// THIS WAS THE WHOLE BUG, for both the mouse and the keys.
	//
	// The port never told the engine it wanted the keyboard and the mouse. There was no
	// `SetInputMode` anywhere in the project, no `bShowMouseCursor = false`, no capture --
	// so the default viewport input mode applied, Slate kept the mouse, and the game saw
	// nothing: `IsInputKeyDown(W)` false, `GetInputMouseDelta` zero. Both symptoms in one
	// cause, which is why "WASD does nothing" and "the mouse does not aim" arrived together.
	//
	// Godot does the equivalent explicitly -- `_input` only turns the view when
	// `Input.get_mouse_mode() == Input.MOUSE_MODE_CAPTURED`, and Escape releases it -- and
	// this is the Unreal spelling of the same statement.
	//
	// Idempotent and cheap, called from Tick because the player controller does not exist
	// yet in BeginPlay on every path.
	if (bInputClaimed)
	{
		return;
	}
	UWorld* World = GetWorld();
	APlayerController* PC = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (PC == nullptr)
	{
		return;
	}
	PC->bShowMouseCursor = false;
	PC->SetInputMode(FInputModeGameOnly());
	bInputClaimed = true;
	UE_LOG(LogTemp, Display, TEXT("HI: input claimed -- game-only mode, mouse captured"));
}

void AHIGameMode::PollMouseRelease()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (PC == nullptr)
	{
		return;
	}
	if (!PC->WasInputKeyJustPressed(EKeys::Escape))
	{
		return;
	}

	// A TOGGLE, and that is one deliberate divergence from `game.gd`. The original only ever
	// RELEASES -- it captures once at start and never again -- so escaping there is a one-way door
	// until the round resets. A door that opens one way, in a game whose window has no title bar,
	// is not a hatch, it is a trap. So: `[ESC]` gives the cursor back, `[ESC]` again takes it.
	bMouseReleased = !bMouseReleased;

	if (bMouseReleased)
	{
		PC->bShowMouseCursor = true;
		// GameAndUI rather than UIOnly: `[ENTER]` must still restart the round and `[ESC]` must
		// still take the cursor back, and UIOnly would swallow both.
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
	}
	else
	{
		PC->bShowMouseCursor = false;
		PC->SetInputMode(FInputModeGameOnly());
	}

	UE_LOG(LogTemp, Display, TEXT("HI: [ESC] -- mouse %s (showCursor=%d)"),
		bMouseReleased ? TEXT("RELEASED, the cursor is yours") : TEXT("captured again"),
		PC->bShowMouseCursor ? 1 : 0);
}

FHISim::FHIInput AHIGameMode::ReadInput()
{
	FHISim::FHIInput In;

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return In;
	}
	const APlayerController* PC = World->GetFirstPlayerController();
	if (PC == nullptr)
	{
		return In;
	}

	// ---- MOVEMENT: raw keys, NOT GetInputAxisValue, and the log is why.
	//
	// The first real play session reported "do the A W S keys work for movement, didn't work
	// for me", and every frame the log carried four copies of this:
	//
	//   LogPlayerController: Warning: Request for value of axis 'MoveForward' returning 0 as
	//   it is not bound on this input component.
	//
	// The mappings in DefaultInput.ini are correct -- W/S/A/D are there -- but the axis is
	// never BOUND on the controller's input component, so the accessor returns 0 forever and
	// warns each time. The same was true of `Turn` and `LookUp`, which is why the mouse did
	// not turn the view either. Reading the keys directly is what actually works, and it also
	// stops four warnings per frame from burying the log.
	//
	// The sim's plane is (X, Y) with +Y forward, so `MoveRight` is the X component and
	// `MoveForward` the Y one -- and "+1 forward" is what FHIRound::Step already expects.
	// AND THE ARROW KEYS, because Godot binds both: `_move_axis` reads
	// `KEY_W or KEY_UP`, `KEY_S or KEY_DOWN`, `KEY_A or KEY_LEFT`, `KEY_D or KEY_RIGHT`.
	// The port had only the letters, which is half of what the original accepts.
	const bool bForward = PC->IsInputKeyDown(EKeys::W) || PC->IsInputKeyDown(EKeys::Up);
	const bool bBack = PC->IsInputKeyDown(EKeys::S) || PC->IsInputKeyDown(EKeys::Down);
	const bool bLeft = PC->IsInputKeyDown(EKeys::A) || PC->IsInputKeyDown(EKeys::Left);
	const bool bRight = PC->IsInputKeyDown(EKeys::D) || PC->IsInputKeyDown(EKeys::Right);
	In.Axis = FVector2D(
		(bRight ? 1.0f : 0.0f) - (bLeft ? 1.0f : 0.0f),
		(bForward ? 1.0f : 0.0f) - (bBack ? 1.0f : 0.0f));
	InputSource = TEXT("keys");
	In.bCrouch = PC->IsInputKeyDown(EKeys::C) || PC->IsInputKeyDown(EKeys::LeftControl);
	In.bSprint = PC->IsInputKeyDown(EKeys::LeftShift);
	In.bFire = PC->IsInputKeyDown(EKeys::LeftMouseButton);

	// The three verbs that had no key wired to them at all until now. JustPressed rather
	// than IsKeyDown, because each is an EDGE: holding [R] must not restart the reload every
	// frame, and holding [F] must not spend a cooldown it does not have.
	if (PC->WasInputKeyJustPressed(EKeys::R))
	{
		In.bReload = true;
	}
	if (PC->WasInputKeyJustPressed(EKeys::F))
	{
		In.bShove = true;
	}
	// Item 17's two non-lethal verbs. Both are EDGES like the others: holding [P] must not
	// re-call the police every frame, and holding [Q] must not burn the cooldown it does not
	// have.
	if (PC->WasInputKeyJustPressed(EKeys::Q))
	{
		In.bShout = true;
	}
	if (PC->WasInputKeyJustPressed(EKeys::P))
	{
		In.bCallPolice = true;
	}
	// The light switch is NOT a sim verb either -- it changes what the game layer does with a
	// shot, not what the round does -- so it is polled here rather than through FHIInput.
	if (PC->WasInputKeyJustPressed(EKeys::L))
	{
		ToggleLights();
	}
	if (PC->WasInputKeyJustPressed(EKeys::One))
	{
		In.SwitchWeapon = TEXT("pistol");
	}
	else if (PC->WasInputKeyJustPressed(EKeys::Two))
	{
		In.SwitchWeapon = TEXT("shotgun");
	}

	return In;
}

void AHIGameMode::PollRestart()
{
	if (!IsRoundOver())
	{
		return;
	}
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	APlayerController* PC = World->GetFirstPlayerController();
	if (PC == nullptr)
	{
		return;
	}
	if (PC->WasInputKeyJustPressed(EKeys::Enter))
	{
		RestartRound();
	}
}

void AHIGameMode::ApplyLook()
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	const APlayerController* PC = World->GetFirstPlayerController();
	if (PC == nullptr)
	{
		return;
	}

	// THE SAME CONE QUESTION THE GODOT ORIGINAL SETTLED, and settled as FIXED.
	//
	// `game.gd` carries both schemes behind `aim_cone`, whose default is `false`: with the
	// cone off, `aim_yaw`/`aim_pitch` are forced to zero and the body's own yaw and pitch ARE
	// the aim. That is what Richard chose as the default and what he asked for here. This
	// function has only ever done the fixed thing -- there is no cone in the port to disable,
	// and the comment on `FHISim::AimDir` calling it "a limited cone of free aim" describes
	// where the code came from rather than what it does.
	//
	// What was NOT matching was the SPEED. `game.gd` uses AIM_SENS_X = 0.0022 radians per
	// unit of mouse movement; this used `GetInputAxisValue() * 0.05` against a MouseX axis
	// whose own sensitivity is 0.07, which is 0.0035 -- about 60% faster than the original.
	// The axis value is already scaled by that 0.07, so the conversion back to radians per
	// PIXEL has to divide it out. Both paths below therefore agree with Godot.
	// RAW MOUSE DELTA, and the SIGNS AND THE SPEEDS are Godot's, taken from `_input`:
	//
	//     yaw   -= event.relative.x * AIM_SENS_X      (0.0022)
	//     pitch  = clamp(pitch + event.relative.y * AIM_SENS_Y, PITCH_MIN, PITCH_MAX)   (0.0018)
	//
	// The first version of this had BOTH axes the wrong way round -- `yaw +=` and
	// `pitch -=` -- which makes the mouse feel inverted rather than dead, and a single
	// sensitivity for both axes when the original uses two. Reported from play: "the mouse
	// doesn't aim and shoot like in the Godot version".
	float DeltaX = 0.0f;
	float DeltaY = 0.0f;
	PC->GetInputMouseDelta(DeltaX, DeltaY);

	// UNREAL'S "MOUSE DELTA" IS NOT IN PIXELS, and that is the whole of "you have to move the
	// mouse a lot for the screen to respond".
	//
	// `APlayerController::GetInputMouseDelta` is `PlayerInput->GetKeyValue(EKeys::MouseX)` --
	// the AXIS VALUE, not the raw hardware delta. The axis value has already been scaled by
	// the MouseX/MouseY sensitivity from DefaultInput.ini, which is 0.07. Godot's number is
	// radians per PIXEL (`event.relative.x`, raw), so multiplying an axis value by 0.0022 gave
	// a view 1/0.07 = **14x too slow**. Nothing about the sign or the scheme was wrong any
	// more; there was simply a missing division.
	//
	// Dividing by the configured sensitivity rather than hardcoding 1/0.07 keeps this correct
	// if the config changes, and it is read from the same object the value came from. Note the
	// port reads the axis VALUE deliberately -- it is not using the `Turn`/`LookUp` mappings,
	// which are not bound to the input component at all.
	const float SensitivityX = PC->PlayerInput != nullptr ? PC->PlayerInput->GetMouseSensitivityX() : 1.0f;
	const float SensitivityY = PC->PlayerInput != nullptr ? PC->PlayerInput->GetMouseSensitivityY() : 1.0f;
	const float PixelsX = (SensitivityX != 0.0f && SensitivityX != 1.0f) ? DeltaX / SensitivityX : DeltaX;
	const float PixelsY = (SensitivityY != 0.0f && SensitivityY != 1.0f) ? DeltaY / SensitivityY : DeltaY;

	float DeltaYaw = 0.0f;
	float DeltaPitch = 0.0f;
	LookDelta(PixelsX, PixelsY, DeltaYaw, DeltaPitch);

	Round.Player.Yaw += DeltaYaw;
	// The look axis is bound inverted (see DefaultInput.ini), so pushing the mouse up
	// raises the pitch rather than lowering it.
	Round.Player.Pitch = FMath::Clamp(
		Round.Player.Pitch + DeltaPitch,
		FHISim::PitchMin, FHISim::PitchMax);

	// RECOIL RECOVERY, and the first version of this was wrong in a way worth writing down:
	// it applied the kick gradually and then had nothing left to give back, which is a view
	// that drifts upward permanently -- one shot and your aim is never the same again. The
	// kick is applied IMMEDIATELY where the shot happens (`ResolvePlayerShot`); this only
	// hands it back, at `game.gd`'s RECOIL_RECOVER rad/s, until the whole kick is returned.
	//
	// Negative pitch is "looking up" in the sim, so the kick subtracts and the recovery adds.
	{
		const float Dt = GetWorld() != nullptr ? GetWorld()->GetDeltaSeconds() : 0.0f;
		const float GiveBack = FMath::Min(RecoilRecoverRate * Dt, RecoilPending);
		if (GiveBack > 0.0f)
		{
			Round.Player.Pitch = FMath::Clamp(Round.Player.Pitch + GiveBack,
				FHISim::PitchMin, FHISim::PitchMax);
			RecoilPending -= GiveBack;
		}
	}

	// Logged once a second, and only while something is happening: this is the line that
	// turns "the controls do not work" into a fact on the next run.
	InputLogTime += GetWorld()->GetDeltaSeconds();
	if (InputLogTime >= 1.0f && (!FMath::IsNearlyZero(DeltaYaw) || !FMath::IsNearlyZero(DeltaPitch)))
	{
		InputLogTime = 0.0f;
		UE_LOG(LogTemp, Display, TEXT("HI: look src=%s turn=%.3f up=%.3f -> yaw=%.2f pitch=%.2f"),
			*InputSource, DeltaYaw, DeltaPitch, Round.Player.Yaw, Round.Player.Pitch);
	}
}

bool AHIGameMode::NearestStanding(FVector2D& OutPos, float& OutDist) const
{
	bool bFound = false;
	float Best = TNumericLimits<float>::Max();
	for (const FHISim::FHIThief& T : Round.Intruders)
	{
		if (!T.bAlive)
		{
			continue;
		}
		const float Dist = FVector2D::Distance(Round.Player.Pos, T.Pos);
		if (Dist < Best)
		{
			Best = Dist;
			OutPos = T.Pos;
			bFound = true;
		}
	}
	OutDist = Best;
	return bFound;
}

void AHIGameMode::Autopilot(FHISim::FHIInput& In)
{
	FVector2D Target;
	float Dist = 0.0f;
	if (NearestStanding(Target, Dist))
	{
		// Turn onto him. The sim measures the shove arc from Player.Yaw and movement from the
		// same field, so aiming the body IS aiming.
		const FVector2D To = Target - Round.Player.Pos;
		const float Want = FMath::Atan2(To.X, To.Y);   // yaw 0 faces +Y, so x=sin, y=cos
		float Delta = Want - Round.Player.Yaw;
		while (Delta > UE_PI) { Delta -= 2.0f * UE_PI; }
		while (Delta < -UE_PI) { Delta += 2.0f * UE_PI; }
		Round.Player.Yaw += FMath::Clamp(Delta, -3.0f / 60.0f, 3.0f / 60.0f);

		// AIM THE PITCH AT HIS CHEST, and that one requirement is the difference between a
		// soak that wins and a soak that loses — the CAMERA is what made it necessary.
		//
		// The player's shot in this port is a real world trace from the real camera, so the
		// crosshair is honest by construction (see ResolvePlayerShot). The camera sits CamUp
		// above the 1.62 m eye — 2.06 m since 2026-10-13, when up 0.44 was picked for gun
		// visibility — and the intruder's hit capsule tops out at EXACTLY 2.0 m. A level ray
		// (StartView pitches 0.03 down; each shot's recoil kicks the pitch back up toward
		// level) therefore passes six centimetres over every standing man's head at ANY
		// distance. Measured after the camera change: the driven soak missed 12 of 12, the
		// intruders' fear (FearPerShot 0.7 vs FearFlee 4.0 — spec numbers, asserted by
		// HI.Sim.Fear) saturated after about six heard shots, and since they spawn beside
		// the van they flee to, the round ended LOST in 3.3 s with everyone alive. The
		// pre-camera baseline (up 0.22, camera at 1.84 m) put the ray under the capsule top,
		// so level fire hit and the same soak WON in 5.0 s — and no soak ran between the
		// camera change and this one, which is how a shot-placement regression sat in HEAD
		// behind 41 green tests: no test carries a camera, a capsule and a world trace
		// together. A human never notices the bug — putting the crosshair ON a man means
		// pitching down, which is the fix — but the autopilot aimed yaw only. So aim both.
		//
		// Chest height = the hit capsule's centre = 1.0 m (AHIIntruder's constructor offsets
		// it there); the horizontal leg is Dist. The same per-frame slew as the yaw.
		const float WantPitch = FMath::Atan2(
			FHISim::EyeHeight + FHISim::CamUp - 1.0f, FMath::Max(Dist, 0.5f));
		Round.Player.Pitch = FMath::Clamp(Round.Player.Pitch
			+ FMath::Clamp(WantPitch - Round.Player.Pitch, -3.0f / 60.0f, 3.0f / 60.0f),
			FHISim::PitchMin, FHISim::PitchMax);

		// Close the distance, and fire once he is worth shooting at rather than at the sky.
		if (Dist > 2.5f)
		{
			In.Axis = FVector2D(0.0f, 1.0f);
		}
		// Only pull the trigger on a target he can actually SEE, and only once the pitch has
		// converged onto it. The line-of-sight gate did NOT explain the 3.3 s LOST — measured,
		// `CanSee` was true for all twelve of those shots, the sim's 2-D line through the front
		// door to the van is clear, and the misses were the pitch problem above. It stays
		// anyway because a human does not empty a magazine into a wall either, and every shot
		// heard is 0.7 of the fear that ends rounds; and the pitch gate keeps the first shot of
		// an engagement from flying where the slew has not arrived yet. CanSee is the sim's own
		// line-of-sight — the same call the intruders' perception uses.
		In.bFire = FMath::Abs(Delta) < 0.25f
			&& FMath::Abs(WantPitch - Round.Player.Pitch) < 0.05f
			&& FHISim::CanSee(Round.Player.Pos, Target);
		if (Dist < FHISim::ShoveRange)
		{
			In.bShove = true;
		}
		if (Round.Player.Mag <= 0)
		{
			In.bReload = true;
		}
	}
	else
	{
		// Nobody up: stand still rather than walking into a wall for the rest of the soak.
		In.Axis = FVector2D::ZeroVector;
	}
}

void AHIGameMode::ResolvePlayerShot(const FHISim::FHIEvent& Shot)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	const APlayerController* PC = World->GetFirstPlayerController();
	AHIPlayerCharacter* Pawn = PC ? Cast<AHIPlayerCharacter>(PC->GetPawn()) : nullptr;
	if (Pawn == nullptr)
	{
		return;
	}

	// The ray starts at the CAMERA, not the eye, and the original says why: with the camera
	// pulled back behind him those are different points, and casting from the eye while the
	// crosshair sits on the camera's forward is what put shots 0.6 m left and 0.3 m low.
	// From the camera the crosshair is honest by construction, whatever the offset is.
	//
	// Read from the PAWN'S ACTUAL CAMERA rather than recomputed from the sim, and that
	// distinction became load-bearing when the camera learned to pull itself out of walls:
	// the pure ViewTransform still returns where the camera WOULD be, so firing along it
	// would put the shot through the wall the camera had just backed away from.
	FVector Origin = Pawn->Camera->GetComponentLocation();
	FVector Dir = Pawn->Camera->GetForwardVector();
	// A pawn with no camera component cannot happen with this class, but the framework does
	// not guarantee one exists on the first frame; fall back rather than fire from the origin.
	if (Dir.IsNearlyZero())
	{
		AHIPlayerCharacter::ViewTransform(Round.Player, Origin, Dir);
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(HIShot), /*bTraceComplex=*/false);
	Params.AddIgnoredActor(Pawn);

	// The player's own weapon, once per trigger pull rather than once per pellet: eight
	// shotgun pellets are one bang.
	const bool bShotgun = Round.Player.Weapon == TEXT("shotgun");
	FHIAudio::PlayerGunshot(World, Origin, bShotgun);

	// THE MUZZLE, and the flash and tracers come from it rather than from the camera.
	//
	// Neither existed on the player's own weapon, and it was the most visible thing missing
	// from firing: `game.gd:1485` calls `_add_flash(muzzle)` on EVERY trigger pull and
	// `game.gd:1537` draws a tracer per pellet, both from `player_actor.muzzle()`. The port
	// had `FlashPoint` and `FlashLine` -- used only for the intruders' fire.
	//
	// From the GUN, not the camera, and that is the whole reason Godot uses a muzzle marker:
	// the camera is where the ray starts, and a tracer that begins inside your own head is
	// invisible. The gun component stands in for the marker.
	const FVector Muzzle = (Pawn->Gun != nullptr) ? Pawn->Gun->GetComponentLocation() : Origin;
	// Godot's flash quad is 0.34 m and its colour is the original's own warm white.
	FlashPoint(Muzzle, 0.34f, FLinearColor(1.0f, 0.92f, 0.55f));
	// `game.gd`'s recoil: the kick lands on THIS frame, and ApplyLook hands it back over the
	// next fraction of a second. Kicking the body pitch rather than a separate aim pitch,
	// because with the cone off there is only one pitch.
	{
		const float Kick = bShotgun ? RecoilShotgun : RecoilPistol;
		Round.Player.Pitch = FMath::Clamp(Round.Player.Pitch - Kick,
			FHISim::PitchMin, FHISim::PitchMax);
		// 65% of the kick comes back, not all of it: game.gd:1456 does
		// `recoil_recover += kick * 0.65`, so the original leaves a residue on the sight
		// picture -- sustained fire climbs and the player has to pull back down. The port
		// returned 100%, making the climb free (PARITY item 13, 2026-10-15).
		RecoilPending += Kick * 0.65f;
	}

	for (int32 Pellet = 0; Pellet < Shot.Pellets; ++Pellet)
	{
		// The scatter keeps the original's SHAPE -- an independent uniform offset per axis
		// rather than a cone -- so the pattern this port produces is the pattern the Godot
		// one produced. Both are unseeded, which the project's deferred list already notes as
		// the thing that would desync two machines if this ever went multiplayer.
		const FVector Scatter(
			FMath::FRandRange(-Shot.Spread, Shot.Spread),
			FMath::FRandRange(-Shot.Spread, Shot.Spread),
			FMath::FRandRange(-Shot.Spread, Shot.Spread));

		FHitResult Hit;
		const FVector End = Origin + (Dir + Scatter).GetSafeNormal() * ShotRangeMeters * HI_METERS_TO_UU;
		const bool bHit = World->LineTraceSingleByChannel(Hit, Origin, End, ECC_Visibility, Params);

		// One tracer PER PELLET, from the muzzle -- `game.gd:1545-1566`. 24 mm across in the
		// original (a 0.012 m cylinder radius) against the intruders' 20 mm, and the same warm
		// colour, so a shotgun's nine pellets are nine visible lines rather than one.
		FlashLine(Muzzle, bHit ? Hit.ImpactPoint : End, 0.024f, FLinearColor(1.0f, 0.92f, 0.66f));

		if (bHit)
		{
			if (AHIIntruder* HitIntruder = Cast<AHIIntruder>(Hit.GetActor()))
			{
				Round.DamageIntruder(HitIntruder->ThiefId, Shot.Damage);
			}

			// A mark wherever the pellet stopped: on a man or on the wall behind him. Godot's
			// _add_impact does the same, and it is what makes a missed shot legible -- a
			// tracer alone leaves nothing to aim the next one by.
			if (!Marks.IsEmpty())
			{
				AHIMark* Mark = Marks[MarkCursor % Marks.Num()];
				MarkCursor = (MarkCursor + 1) % Marks.Num();
				if (Mark != nullptr)
				{
					const bool bFlesh = Hit.GetActor() != nullptr && Hit.GetActor()->IsA(AHIIntruder::StaticClass());
					// game.gd's _add_impact, all three of its numbers: the quad is 0.16 m,
					// a shotgun hit scales it x1.7 (`big`), and flesh scales it x0.7. The
					// port kept only the flesh ratio and invented a 0.10 base with no
					// shotgun case at all (PARITY item 13, 2026-10-15) -- a shotgun's
					// nine 0.27 m sheets ARE the feedback that a shotgun hit.
					float HoleSize = bShotgun ? 0.16f * 1.7f : 0.16f;
					if (bFlesh)
					{
						HoleSize *= 0.7f;
					}
					Mark->Stamp(Hit.ImpactPoint, Hit.ImpactNormal, HoleSize,
						bFlesh ? FHIMaterials::Blood : HoleColour);
				}
			}
		}
	}
}

void AHIGameMode::FlashLine(const FVector& From, const FVector& To, float ThicknessMeters,
	const FLinearColor& Colour)
{
	if (Flashes.IsEmpty())
	{
		return;
	}
	AHIMark* Slot = Flashes[FlashCursor % Flashes.Num()];
	const int32 SlotIdx = FlashCursor % Flashes.Num();
	FlashCursor = (FlashCursor + 1) % Flashes.Num();
	if (Slot != nullptr)
	{
		Slot->StampLine(From, To, ThicknessMeters, Colour);
		FlashHideAt[SlotIdx] = RoundSeconds + 0.05f;
	}
}

void AHIGameMode::FlashPoint(const FVector& Where, float SizeMeters, const FLinearColor& Colour)
{
	if (Flashes.IsEmpty())
	{
		return;
	}
	const int32 SlotIdx = FlashCursor % Flashes.Num();
	AHIMark* Slot = Flashes[SlotIdx];
	FlashCursor = (FlashCursor + 1) % Flashes.Num();
	if (Slot != nullptr)
	{
		// Facing the camera is not knowable here without reaching for a view; a blob is a
		// blob from any side, and a muzzle flash lasts fifty milliseconds.
		Slot->Stamp(Where, FVector::UpVector, SizeMeters, Colour);
		FlashHideAt[SlotIdx] = RoundSeconds + 0.05f;
	}
}

void AHIGameMode::TickFlashes()
{
	for (int32 Idx = 0; Idx < Flashes.Num() && Idx < FlashHideAt.Num(); ++Idx)
	{
		if (Flashes[Idx] != nullptr && FlashHideAt[Idx] >= 0.0f && RoundSeconds >= FlashHideAt[Idx])
		{
			FlashHideAt[Idx] = -1.0f;
			Flashes[Idx]->SetActorHiddenInGame(true);
		}
	}
}

float AHIGameMode::ThiefHitChance(float RangeMeters, bool bLightsOn)
{
	// Falloff 0.035 per metre, floor 0.05. Both are the Godot original's CURRENT values,
	// read out of game.gd's `_drain_events` rather than from a note about them -- the notes
	// in the project memory carry an older pair (0.02 and 0.55) that is no longer true.
	const float Lit = FMath::Clamp(1.0f - RangeMeters * 0.035f, 0.05f, 1.0f);

	// DARKNESS. Not a floor and not a bonus: a flat multiplier on the whole curve, so the
	// advantage of distance and the advantage of darkness compose instead of one hiding the
	// other. 0.55 was chosen against the arithmetic rather than by feel -- in the dark a
	// three-man crew that would kill an idle homeowner in 74 s takes roughly twice as long,
	// which is enough time for the switch to have been worth reaching for.
	return bLightsOn ? Lit : Lit * 0.55f;
}

void AHIGameMode::ToggleLights()
{
	bLightsOn = !bLightsOn;
	ApplyLights();
	Message = bLightsOn ? TEXT("Lights on.") : TEXT("Lights off. They can see you less well - and you them.");
	MessageTime = 1.8f;
}

void AHIGameMode::ApplyLights()
{
	// DIMMED, not zeroed. A house with the lights off is not a sealed void: moonlight through
	// the windows is not modelled here, and a room the player cannot see AT ALL is a room he
	// cannot play in. A quarter is dark enough to change how the house reads and bright enough
	// that the layout is still legible.
	constexpr float DarkScale = 0.25f;

	for (int32 Idx = 0; Idx < Lamps.Num(); ++Idx)
	{
		APointLight* Lamp = Lamps[Idx];
		if (Lamp == nullptr || !LampLitIntensity.IsValidIndex(Idx))
		{
			continue;
		}
		if (ULightComponent* Bulb = Lamp->GetLightComponent())
		{
			Bulb->SetIntensity(bLightsOn ? LampLitIntensity[Idx] : LampLitIntensity[Idx] * DarkScale);
		}
	}
}

void AHIGameMode::ResolveThiefShot(const FHISim::FHIEvent& Shot)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	const FHISim::FHIThief* Who = Round.FindIntruder(Shot.Id);
	if (Who == nullptr || !Who->bAlive)
	{
		return;
	}

	// Chest height at both ends, in metres converted once. The Godot original uses 1.3 for
	// both, which is what makes this a level line across a room rather than one that dives
	// into the floor.
	const FVector From(Who->Pos.X, Who->Pos.Y, 1.30f);
	const FVector To(Round.Player.Pos.X, Round.Player.Pos.Y, 1.30f);
	const FVector FromW = From * HI_METERS_TO_UU;
	const FVector ToW = To * HI_METERS_TO_UU;

	const float RangeM = FVector::Dist(From, To);

	// The muzzle flash, drawn whether or not the round goes anywhere. It is the only cue
	// that he has fired at all, and without it a player losing health has no idea why.
	FlashPoint(FromW + FVector(0.0f, 0.0f, 10.0f), 0.22f, FLinearColor(1.0f, 0.92f, 0.55f));

	// And the report, POSITIONED on him. Three intruders firing from three rooms is a game
	// you can play by ear; three unattributable bangs is a game that ambushes you.
	FHIAudio::IntruderGunshot(World, Who->Pos, Who->Z, true);

	// HIS ROUND IS A RAY. Trace the world between the two, ignoring the intruders so he
	// cannot shoot his own hitbox, and ignoring the player's pawn -- his body has no
	// collision at all, so any hit here is scenery and the round STOPS at it.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HIThiefShot), /*bTraceComplex=*/false);
	for (const AHIIntruder* Other : IntruderActors)
	{
		if (Other != nullptr)
		{
			Params.AddIgnoredActor(Other);
		}
	}
	if (const APlayerController* PC = World->GetFirstPlayerController())
	{
		if (const APawn* Pawn = PC->GetPawn())
		{
			Params.AddIgnoredActor(Pawn);
		}
	}

	FHitResult WallHit;
	if (World->LineTraceSingleByChannel(WallHit, FromW, ToW, ECC_Visibility, Params))
	{
		// The tracer stops at the wall rather than passing through to you.
		FlashLine(FromW + FVector(0.0f, 0.0f, 10.0f), WallHit.ImpactPoint, 0.02f,
			FLinearColor(1.0f, 0.9f, 0.5f));
		FHIAudio::Impact(World, WallHit.ImpactPoint, /*bFlesh=*/false);
		if (!Marks.IsEmpty())
		{
			AHIMark* Mark = Marks[MarkCursor % Marks.Num()];
			MarkCursor = (MarkCursor + 1) % Marks.Num();
			if (Mark != nullptr)
			{
				// 0.16 m, the same _add_impact base the homeowner's holes use: game.gd's
				// thiefShot wall case calls it with big=false, on_flesh=false. His shot
				// chipping the wall next to your head is the cue that the miss was close,
				// and at the port's old 0.10 it was a cue you had to squint at.
				Mark->Stamp(WallHit.ImpactPoint, WallHit.ImpactNormal, 0.16f, HoleColour);
			}
		}
		return;
	}

	// Clear line. Range falls off with a floor of 0.05, so distance is worth keeping: the
	// Godot original's older 0.55 floor meant he hit more than half the time at ANY range,
	// and closing or opening the gap bought the player nothing.
	const float Chance = ThiefHitChance(RangeM, bLightsOn);
	if (FMath::FRand() <= Chance)
	{
			// DIAGNOSTIC, and it exists because a measurement said the light switch did nothing:
		// a lit and a dark passive soak returned identical outcomes to the sim step, which is
		// only possible if either the darkness is not reaching this code or the intruders are
		// not shooting at all. Silence and a no-op look the same from the outside; this line
		// is what tells them apart.
		// VERBOSE, not Display: this is per-shot and a firefight produces hundreds. It earned
		// its place -- it is what showed that the light switch WAS reaching this code when two
		// soak runs looked identical -- so it stays, one log level down.
		UE_LOG(LogTemp, Verbose,
			TEXT("HI: thief %d fires at %.1f m -- chance %.2f (lights=%d) HIT (hp %.0f)"),
			Shot.Id, RangeM, Chance, bLightsOn ? 1 : 0, Round.Player.Hp);
		// BEARING TO THE SHOOTER, before the wound and in the same branch as it, exactly as
		// game.gd:1142-1146 orders them (PARITY item 8, ported 2026-10-15). Under
		// -HINoThiefDamage the marker still draws: the roll hit, and the direction is a fact
		// about the shot rather than about the withheld wound.
		HitMarkBearing = AHIHUD::HitBearing(Who->Pos, Round.Player.Pos, Round.Player.Yaw);
		HitMarkTime = 1.1f;
		// The report goes into the ROUND's event list, not into LastEvents, because that is
		// the list the round's own rules read -- `playerDown` has to reach ApplyRoundRules or
		// a lethal hit would leave the round in Play forever.
		//
		// The trace, the tracer and the roll all still happen under -HINoThiefDamage; only
		// the wound is withheld, so the probe exercises the same code path a real hit does.
		if (!bNoThiefDamage)
		{
			const bool bKilled = FHISim::DamagePlayer(Round.Player, Shot.Damage, Round.Events);
			if (bKilled)
			{
				// DEATH IS A FACT, AND IT ONCE FAILED TO BECOME ONE. Measured 2026-10-14 from
				// a manual session: the homeowner reached 0 hp, her gun vanished with the
				// dead-player rule, and the round simply KEPT GOING -- no decision, no banner,
				// intruders robbing the house over her corpse for as long as anyone watched.
				// The playtester's report was "the home defender does not have a gun": he was
				// watching a dead woman who had never been told to die. This line and the two
				// watches in Tick (pending playerDown, state transitions) print the whole
				// chain, so the next disagreement is a grep instead of an investigation.
				UE_LOG(LogTemp, Warning,
					TEXT("HI: KILLING BLOW at %.2f s -- playerDown queued, %d round event(s) pending, state %d"),
					RoundSeconds, Round.Events.Num(), static_cast<int32>(Round.State));
			}
			LastDamageTime = RoundSeconds;
			FHIAudio::Impact(World, ToW, /*bFlesh=*/true);
		}
	}
	else
	{
		// It goes wide: end the tracer off to one side rather than on you, so a miss is
		// visibly a miss rather than indistinguishable from a hit that did no damage.
		const FVector Wide(FMath::FRandRange(-1.0f, 1.0f), FMath::FRandRange(-0.5f, 0.5f),
			FMath::FRandRange(-1.0f, 1.0f));
		FlashLine(FromW + FVector(0.0f, 0.0f, 10.0f),
			ToW + Wide.GetSafeNormal() * 1.4f * HI_METERS_TO_UU, 0.02f,
			FLinearColor(1.0f, 0.85f, 0.4f));
		UE_LOG(LogTemp, Verbose,
			TEXT("HI: thief %d fires at %.1f m -- chance %.2f (lights=%d) miss"),
			Shot.Id, RangeM, Chance, bLightsOn ? 1 : 0);
	}
}

void AHIGameMode::RestartRound()
{
	Round = FHISim::FHIRound::Start(IntruderCount);
	ApplyStartWeapon();
	RoundSeconds = 0.0f;
	SimAccumulator = 0.0f;
	SimSteps = 0;
	Frames = 0;
	PlayerShotsFired = 0;
	bShotTaken = false;
	QuitCountdownFrames = INDEX_NONE;

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// The intruders are rebuilt rather than reset: a corpse needs its collision back, its
	// death latch cleared and its animation restarted, and destroying three actors is more
	// obviously correct than remembering every field that has to be undone.
	for (AHIIntruder* Old : IntruderActors)
	{
		if (Old != nullptr)
		{
			Old->Destroy();
		}
	}
	IntruderActors.Reset();

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (const FHISim::FHIThief& T : Round.Intruders)
	{
		if (AHIIntruder* Actor = World->SpawnActor<AHIIntruder>(
			AHIIntruder::StaticClass(), FTransform::Identity, Params))
		{
			Actor->ThiefId = T.Id;
			Actor->SyncFromSim(T, 1.0f / 60.0f);
			IntruderActors.Add(Actor);
		}
	}

	// The marks of the last round are cleared, so a fresh round does not open on the
	// bloodstains of the previous one.
	for (AHIMark* Mark : Marks)
	{
		if (Mark != nullptr)
		{
			Mark->SetActorHiddenInGame(true);
		}
	}
	MarkCursor = 0;

	// And the flashes, which are mid-air at the moment the last round ended: a tracer left
	// hanging where a corpse fell is the most obvious kind of stale state.
	for (int32 Idx = 0; Idx < Flashes.Num(); ++Idx)
	{
		if (Flashes[Idx] != nullptr)
		{
			Flashes[Idx]->SetActorHiddenInGame(true);
		}
		if (FlashHideAt.IsValidIndex(Idx))
		{
			FlashHideAt[Idx] = -1.0f;
		}
	}
	FlashCursor = 0;
	LastDamageTime = -100.0f;
	Message = TEXT("DEFEND YOUR HOME");
	MessageTime = 3.0f;

	// A capture that restarted would re-photograph the same instant forever, so the shot
	// timer restarts with the round only when no capture was asked for.
}

void AHIGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// The marks are built on the first tick rather than in BeginPlay, because a spawned
	// actor is not fully in the world during BeginPlay and a component registered there can
	// miss its first update.
	//
	// THE MOUSE MUST BE GIVEABLE BACK IN EVERY ROUND STATE, so both of these are polled here
	// rather than inside the live-round branch below. `ClaimInput` is idempotent; `[ESC]` is the
	// only way out of a captured cursor, and a player who cannot free it cannot reach anything --
	// not the round-restart prompt, and not a window control, because this window has no title bar.
	ClaimInput();
	PollMouseRelease();

	// The hit marker's fade, ticked here rather than in the HUD so the marker runs down even
	// while the round itself is over and the HUD is only drawing the outcome banner.
	//
	// `-HIHoldHitMark` is Godot's `--dbg-hit` (game.gd:2170-2172), and the hold has to live
	// HERE rather than in the draw: Godot clamps the timer itself so it never reaches zero,
	// and a clamp applied only to the drawn alpha would still let the state decay and the
	// mark vanish -- a debug switch that cannot hold anything is a switch that does nothing.
	static const bool bHoldHitMark = FParse::Param(FCommandLine::Get(), TEXT("HIHoldHitMark"));
	if (!bHoldHitMark)
	{
		HitMarkTime = FMath::Max(0.0f, HitMarkTime - DeltaSeconds);
	}

	if (Marks.IsEmpty())
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		for (int32 Idx = 0; Idx < MarkPoolSize; ++Idx)
		{
			if (AHIMark* Mark = World->SpawnActor<AHIMark>(
				AHIMark::StaticClass(), FTransform::Identity, Params))
			{
				Mark->SetActorHiddenInGame(true);
				Marks.Add(Mark);
			}
		}
		// Tracers and muzzle flashes: fewer of them, because only a few dozen can overlap
		// within fifty milliseconds even in a full firefight.
		for (int32 Idx = 0; Idx < FlashPoolSize; ++Idx)
		{
			if (AHIMark* Flash = World->SpawnActor<AHIMark>(
				AHIMark::StaticClass(), FTransform::Identity, Params))
			{
				Flash->SetActorHiddenInGame(true);
				Flashes.Add(Flash);
				FlashHideAt.Add(-1.0f);
			}
		}
	}

	// Flashes expire on the round clock rather than on a timer of their own, so a paused or
	// decided round cannot leave a tracer hanging in the air forever.
	TickFlashes();

	if (MessageTime > 0.0f)
	{
		MessageTime -= DeltaSeconds;
	}

	if (Round.State != FHISim::EHIRoundState::Play)
	{
		// Report the round that just ended. In a multi-round run every round reports, so the
		// LAST line is not the only evidence that the earlier ones happened.
		if (SoakSeconds > 0.0f && !bSoakReported)
		{
			ReportSoak();
			++RoundsPlayed;
			if (RoundsPlayed < RoundsToPlay)
			{
				// ANOTHER ROUND, through the same RestartRound the player's [ENTER] uses --
				// so an automated multi-round run exercises the restart path rather than a
				// private copy of it.
				bSoakReported = false;
				RestartRound();
				return;
			}
			bSoakReported = true;
			FPlatformMisc::RequestExit(false);
			return;
		}

		// And the player's own way to do it.
		PollRestart();
		if (Round.State == FHISim::EHIRoundState::Play)
		{
			return;   // he restarted; the next Tick steps the new round
		}

		// A decided round is inert at the sim level too, so there is nothing to gain by
		// stepping it -- and stepping it anyway would keep bodies robbing a house whose
		// outcome is already settled.
		TickHarness(DeltaSeconds);
		return;
	}

	++Frames;

	// Look first, then step: FHIRound moves the homeowner relative to Player.Yaw, so a view
	// applied afterwards would steer him one frame behind where the player is looking.
	ApplyLook();
	ClaimInput();
	FHISim::FHIInput In = ReadInput();

	// -HIPlaytest / -HITour replace what the player does, not how it is read. That
	// distinction is the entire point: -HIAuto overwrites the input struct, so it can never
	// test the input path. These two leave ReadInput's result alone.
	if (bPlaytest)
	{
		PlaytestTick(DeltaSeconds);
	}
	if (bAuto)
	{
		Autopilot(In);
	}

	// `-HIWeaponAt`: LATCHES a cue; the step loop below delivers it, because `In` is rebuilt
	// every frame and a cue written straight into it is lost on a frame with no sim step.
	ApplyScheduledWeapon();

	// THE PENDING-DEATH WATCH. A killing blow is resolved by the actor layer DURING event
	// processing -- after this frame's drain, and after the rule scan inside the last Step --
	// so `playerDown` spends exactly one frame boundary sitting in the round's event list
	// before the NEXT step's ApplyRoundRules has to see it and end the round. Printing it
	// here turns "the rules and the runtime disagree" from an afternoon of code reading into
	// one grep: this line appearing ONCE is the healthy handover; appearing EVERY FRAME is
	// the round refusing to end, which is exactly what it was written for (2026-10-14).
	for (const FHISim::FHIEvent& Pending : Round.Events)
	{
		if (Pending.Type == TEXT("playerDown"))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("HI: playerDown PENDING before the step at %.2f s (%d event(s) in the round list, state %d)"),
				RoundSeconds, Round.Events.Num(), static_cast<int32>(Round.State));
			break;
		}
	}

	// ---- THE FIXED TIMESTEP, and this is not polish. It is what makes the port playable on
	// anything slower than the machine it was written on.
	//
	// The sim is FRAME-RATE SENSITIVE, and the arithmetic is unforgiving: the waypoint arrival
	// radius is 0.6 m and a laden intruder walks at 1.3 m/s * CarrySpeed, so any frame longer
	// than about 0.46 s lets him step clean over a waypoint without ever being inside it. He
	// then never "arrives", never advances along his route, and stands in the lawn forever.
	// Godot never showed this because Godot ran at 60 fps on a display.
	//
	// MEASURED 2026-10-09, and it is the whole reason this exists: an offscreen probe on this
	// 4-core box rendered at roughly 2 fps, so DeltaSeconds was about 0.5 s, and a 200-second
	// soak produced ZERO LOOT DELIVERED with three living, apparently unstuck intruders --
	// against a ported sim whose own test completes the same heist in 137 s at 1/60. A port
	// verified only at 60 fps would have shipped with this.
	//
	// The accumulator is CLAMPED, because one frame that took ten seconds (a shader compile,
	// a stalled GPU, a debugger) must not become six hundred sim steps at once: that is a
	// spiral of death, and it also guarantees a huge DeltaSeconds can never reach the sim.
	constexpr float SimFixedDt = 1.0f / 60.0f;
	constexpr int32 SimMaxStepsPerFrame = 8;
	SimAccumulator = FMath::Min(SimAccumulator + DeltaSeconds, SimFixedDt * SimMaxStepsPerFrame);

	int32 StepsThisFrame = 0;
	FString SwitchDelivered;
	// THE LOOP STOPS AT A DECIDED ROUND, MID-FRAME INCLUDED. `Step` itself refuses a decided
	// round, but this counter did not: inert steps still counted, and `RoundSeconds` below
	// still counted them, so the clock ran on past the moment of the decision -- up to eight
	// steps a frame past it. That quietly breaks the harness contract the whole capture gate
	// is written around ("the clock stops when the round is decided"), and a decision landing
	// mid-frame could even overshoot a capture target and suppress the very warning that is
	// supposed to say the round ended early.
	while (Round.State == FHISim::EHIRoundState::Play
		&& SimAccumulator >= SimFixedDt && StepsThisFrame < SimMaxStepsPerFrame)
	{
		// THE CUE IS DELIVERED HERE, NOT INTO `In` EARLIER, and that is the whole fix. `In` is
		// rebuilt by `ReadInput()` every Tick, so a cue that came due on a frame where the
		// accumulator did not reach a whole sim step simply vanished -- and it vanished SILENTLY,
		// because the cue's own log line had already fired. The first version of `-HIWeaponAt`
		// did exactly that and reported success over a sweep that covered one weapon.
		if (!PendingWeaponCue.IsEmpty())
		{
			In.SwitchWeapon = PendingWeaponCue;
			SwitchDelivered = PendingWeaponCue;
			PendingWeaponCue.Empty();
		}
		Round.Step(In, SimFixedDt);
		SimAccumulator -= SimFixedDt;
		++StepsThisFrame;
		++SimSteps;
	}

	// A REQUESTED SWITCH THAT DID NOT HAPPEN IS A FACT, NOT A NO-OP. `FHIRound::SwitchWeapon`
	// refuses an empty key, a key he is already holding, and an unknown weapon -- all three
	// silently, which is right for a keypress and wrong for a scripted sweep that then reports
	// success over half the frames it claimed to cover. This says so.
	if (!SwitchDelivered.IsEmpty() && Round.Player.Weapon != SwitchDelivered)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("HI: the weapon switch to '%s' was REFUSED -- still holding '%s' with %d round(s)"),
			*SwitchDelivered, *Round.Player.Weapon, Round.Player.Mag);
	}

	// THE ROUND CLOCK IS SIM TIME, not wall time, and the difference is not academic. Every
	// timer in this game -- the police clock, the weapon cooldown, the intruder's fear decay
	// -- advances by the sim's own dt, so a clock counting wall seconds would disagree with
	// the game it is describing.
	//
	// CORRECTED 2026-10-12, because the stale half of this note cost a plan. It used to add:
	// "an offscreen probe on this box renders at ~2 fps and is clamped to 8 sub-steps a frame,
	// so the sim advances at roughly a QUARTER of real speed. A soak budget read as real
	// seconds would therefore end a '240 second' probe after about 64 seconds of game." The
	// conclusion still holds; the number does not. Measured from the shot log's own timestamps,
	// one sim second costs 1.83-1.90 s of wall clock in the first interval and 0.99-1.01 s in
	// the second, in BOTH offscreen and windowed runs -- about 1x realtime, not a quarter. It
	// had `tools/capture.sh` about to be designed around "windowed is ten times faster".
	RoundSeconds += StepsThisFrame * SimFixedDt;

	// Drained ONCE for the whole frame rather than per sub-step: an event is an event, and
	// resolving the same tracer twice because two sub-steps happened inside one frame would
	// double both the marks on the wall and, for `thiefShot`, the damage.
	LastEvents = Round.ConsumeEvents();

	// Resolve the shots the round announced. This is the actor layer's half of firing: the
	// round has already spent the round, started the cooldown and told everyone who heard
	// it, so all that is left is the one thing a sim with no world cannot do.
	// Every event the sim raises is handled here, and until this was written only two of the
	// fifteen were. An event nobody reads is not a cosmetic omission: a dropped `thiefShot`
	// meant intruders could not hurt the homeowner at all, and a dropped `grabbed` meant the
	// theft of the TV was reported by a prop quietly sliding out of the room.
	for (const FHISim::FHIEvent& Ev : LastEvents)
	{
		if (Ev.Type == TEXT("playerShot"))
		{
			++PlayerShotsFired;
			ResolvePlayerShot(Ev);

			// The firing ANIMATION, driven off the round's own event like the flash and the
			// report are -- a trigger pull the sim never recorded must not twitch the body.
			// The hold is the clip's first fire burst (0.33-0.89 s, measured from the FBX
			// keys by the Godot probe), so one shot shows one shot, and a burst re-triggers
			// faster than the hold expires, which is what continuous fire should look like.
			if (APlayerController* ShotPC = GetWorld()->GetFirstPlayerController())
			{
				if (AHIPlayerCharacter* ShotPawn = Cast<AHIPlayerCharacter>(ShotPC->GetPawn()))
				{
					ShotPawn->PlayActionClip(FHICharAnim::ClipShoot(), 0.95f);
				}
			}
		}
		else if (Ev.Type == TEXT("thiefShot"))
		{
			ResolveThiefShot(Ev);

			// His firing animation, from the same event that carries his tracer and his
			// report: `Ev.Id` says who fired, and the actor paired with that Id plays it.
			// An unattributable twitch would be the animation version of the unattributable
			// bang the positioned gunshot exists to avoid.
			for (AHIIntruder* Shooter : IntruderActors)
			{
				if (Shooter != nullptr && Shooter->ThiefId == Ev.Id)
				{
					Shooter->PlayActionClip(FHICharAnim::ClipShoot(), 0.95f);
					break;
				}
			}
		}
		else if (Ev.Type == TEXT("thiefDown"))
		{
			// A body leaves a mark where it fell. Every intruder carries an Id, and the event
			// is stamped with it by the sim -- so this is the one place the corpse's position
			// is known without guessing which body it was.
			if (const FHISim::FHIThief* Who = Round.FindIntruder(Ev.Id))
			{
				if (!Marks.IsEmpty())
				{
					AHIMark* Mark = Marks[MarkCursor % Marks.Num()];
					MarkCursor = (MarkCursor + 1) % Marks.Num();
					if (Mark != nullptr)
					{
						const FVector Where(Who->Pos.X * HI_METERS_TO_UU, Who->Pos.Y * HI_METERS_TO_UU, 1.0f);
						Mark->Stamp(Where, FVector::UpVector, 0.9f, FHIMaterials::Blood);
					}
				}
				Message = FString::Printf(TEXT("Intruder down.  %d of %d neutralised."),
					Round.ThievesDown, Round.Intruders.Num());
				MessageTime = 2.5f;
				FHIAudio::Impact(World, FVector(Who->Pos.X, Who->Pos.Y, 0.4f) * HI_METERS_TO_UU,
					/*bFlesh=*/true);
				// Item 16, now with a VOICE in it. A man going down is the moment a line
				// carries furthest, and it is the one the player is listening for: it tells
				// him how many are left without looking at the HUD.
				FHIAudio::Voice(World, FVector(Who->Pos.X, Who->Pos.Y, 1.4f) * HI_METERS_TO_UU,
					TEXT("thief_down"));
			}
		}
		else if (Ev.Type == TEXT("thiefHit"))
		{
			// Shooting a man has to look like it did something. The pellet mark is placed by
			// the trace; this adds the flinch, which is the part a player reads.
			if (const FHISim::FHIThief* Who = Round.FindIntruder(Ev.Id))
			{
				const FVector Where = FVector(Who->Pos.X, Who->Pos.Y, 1.15f) * HI_METERS_TO_UU;
				FlashPoint(Where, 0.16f, FHIMaterials::Blood);
				FHIAudio::Impact(World, Where, /*bFlesh=*/true);
				// Not every hit: a grunt per pellet would machine-gun the line into noise.
				// One in three is enough to make a hit audible as a person being hurt.
				if ((WoundCries++ % 3) == 0)
				{
					FHIAudio::Voice(World, Where, TEXT("thief_pain"), 0.85f);
				}
			}
		}
		else if (Ev.Type == TEXT("playerHit"))
		{
			// The red edge the HUD draws is driven off LastDamageTime. It is also set at the
			// moment of the shot, so the flash starts on the same frame as the wound rather
			// than one frame later; this is the event-driven copy, which keeps the cue tied
			// to the sim's own record of having been hit.
			LastDamageTime = RoundSeconds;
		}
		else if (Ev.Type == TEXT("grabbed"))
		{
			Message = FString::Printf(TEXT("They grabbed the %s!"), *Ev.Label);
			MessageTime = 2.0f;
		}
		else if (Ev.Type == TEXT("delivered"))
		{
			Message = FString::Printf(TEXT("They got the %s to the van!"), *Ev.Label);
			MessageTime = 2.0f;
		}
		else if (Ev.Type == TEXT("dropped"))
		{
			Message = FString::Printf(TEXT("The %s is on the floor."), *Ev.Label);
			MessageTime = 2.0f;
		}
		else if (Ev.Type == TEXT("shoved"))
		{
			Message = Ev.Label.IsEmpty()
				? FString(TEXT("You shoved him off."))
				: FString::Printf(TEXT("You shoved him off - he dropped the %s."), *Ev.Label);
			MessageTime = 2.0f;
		}
		else if (Ev.Type == TEXT("thiefShove"))
		{
			// He is on you, and this is the one moment the game should feel like a fight
			// rather than a duel, so it shoves the VIEW as well as the message --
			// game.gd:1181-1189 in full, text and timings included (PARITY item 13, ported
			// 2026-10-15; the port's first version had the message only, and with different
			// words). The recovery drift STACKS on the instant kick exactly as the original
			// does: +0.10 now, and the 0.06 rides the same give-back the shots use.
			Message = TEXT("He shoved you off!");
			MessageTime = 1.4f;
			Round.Player.Pitch = FMath::Clamp(Round.Player.Pitch + 0.10f,
				FHISim::PitchMin, FHISim::PitchMax);
			RecoilPending += 0.06f;
			FHIAudio::Impact(World,
				FVector(Round.Player.Pos.X, Round.Player.Pos.Y, 1.2f) * HI_METERS_TO_UU,
				/*bFlesh=*/false);
		}
		else if (Ev.Type == TEXT("shouted"))
		{
			Message = TEXT("You shouted. They know where you are.");
			MessageTime = 1.6f;
			// Item 16's bark hook, from the homeowner this time, alternating between his two
			// lines so shouting twice does not sound like a broken record.
			FHIAudio::Voice(World, FVector(Round.Player.Pos.X, Round.Player.Pos.Y, 1.6f) * HI_METERS_TO_UU,
				(ShoutsMade++ % 2) == 0 ? TEXT("owner_challenge") : TEXT("owner_shout"));
		}
		else if (Ev.Type == TEXT("calledPolice"))
		{
			Message = TEXT("You called the police. They are on their way.");
			MessageTime = 3.0f;
			FHIAudio::Voice(World, FVector(Round.Player.Pos.X, Round.Player.Pos.Y, 1.6f) * HI_METERS_TO_UU,
				TEXT("owner_police"));
		}
		else if (Ev.Type == TEXT("allStolen"))
		{
			Message = TEXT("They cleared the house.");
			MessageTime = 4.0f;
		}
		else if (Ev.Type == TEXT("escaped"))
		{
			Message = FString::Printf(TEXT("One of them ran for it with the %s."), *Ev.Label);
			MessageTime = 3.0f;
		}
		else if (Ev.Type == TEXT("sirens"))
		{
			Message = TEXT("The police are here.");
			MessageTime = 3.0f;
			// From up the drive rather than from the player's own feet: the Godot original
			// plays its siren from outside, and a siren coming out of your own kitchen reads
			// as a bug rather than as rescue.
			FHIAudio::Siren(World, FVector(0.0f, 12.0f, 1.5f) * HI_METERS_TO_UU);
		}
	}

	// THE RULES RUN AT THE END OF EVERY FRAME, NOT ONLY AT THE END OF A STEP.
	//
	// A killing blow is resolved above, inside the drain-and-process phase: the trace runs in
	// the actor layer, `DamagePlayer` queues `playerDown`, and the loss rule that ends the
	// round lives at the end of `FHIRound::Step`. But steps do not run every frame -- at the
	// observed 2.7-3.0x realtime (~180 fps) the accumulator is usually below 1/60 s -- while
	// the drain above runs EVERY frame. So the round's decision raced the frame rate: when the
	// next frame stepped, the rule saw `playerDown` and the round ended; when it did not, the
	// drain ate the event and the heist played on over the homeowner's corpse -- no banner, no
	// restart, `STANDING` on the HUD, "They grabbed the TV!" twelve seconds after her death.
	// Measured in three manual runs on 2026-10-14: drained at 8.08 s (no decision, clock ran
	// to 20 s), stepped (decided at 8.10 s, FAILURE banner), drained at 9.42 s (no decision)
	// -- same code, different frame timing, different outcomes. A stale-build-artifact theory
	// born between runs two and three was disproved by run three landing on a from-scratch
	// binary; the nondeterminism across runs of one binary was the race's signature all along.
	// Godot's original cannot have the race: game.gd sets `state = "lose"` while consuming the
	// event, in the actor layer (scripts/game.gd:1239). This is that, ported. The scan is
	// idempotent by its own contract, and the win is decided where the damage is applied
	// (`DamageIntruder`), so re-running it here can only catch what no step has caught yet.
	Round.ApplyRoundRules();

	// A ROUND DECISION IS A FACT AND GETS SAID OUT LOUD -- and it is said HERE, at the end of
	// the frame, because decisions are made in three places and two of them happen after the
	// substep loop: `DamageIntruder` (the win, resolved by a player-shot trace in the
	// processing loop above) and the rules scan just before this line (the loss by death). A
	// watch placed before the drain -- where this one lived until 2026-10-14 -- could never
	// print either, because the NEXT frame returns through the decided branch near the top of
	// Tick before reaching it. A round that never printed its decision is indistinguishable in
	// the log from one that never decided, which is how a homeowner who died at 12 s got
	// "verified" alive at 20 s. The clock reads the same here as before the drain; nothing
	// advances it in between. The name is spelled out rather than printed as a number for the
	// same reason the soak spells out its result.
	if (Round.State != LastLoggedState)
	{
		LastLoggedState = Round.State;
		const TCHAR* What = Round.State == FHISim::EHIRoundState::Won ? TEXT("WON")
			: (Round.State == FHISim::EHIRoundState::Lost ? TEXT("LOST") : TEXT("PLAY"));
		UE_LOG(LogTemp, Warning, TEXT("HI: round decided -> %s at %.2f s of round"), What, RoundSeconds);
	}

	// Pull every view into place AFTER the step, from the same Tick. Nothing else writes an
	// actor transform for this round, so there is no frame where a body is drawn from a
	// half-updated model.
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (AHIPlayerCharacter* Pawn = Cast<AHIPlayerCharacter>(PC->GetPawn()))
		{
			Pawn->SetWeaponVisual(Round.Player.Weapon);
			Pawn->SyncFromSim(Round.Player);
		}
	}

	const int32 Count = FMath::Min(IntruderActors.Num(), Round.Intruders.Num());
	for (int32 Idx = 0; Idx < Count; ++Idx)
	{
		if (IntruderActors[Idx] != nullptr)
		{
			IntruderActors[Idx]->SyncFromSim(Round.Intruders[Idx], DeltaSeconds);
		}
	}

	// Loot: find who is carrying each item, so a carried prop rides the right man.
	for (int32 Idx = 0; Idx < LootActors.Num() && Idx < Round.Loot.Num(); ++Idx)
	{
		AHILootProp* Prop = LootActors[Idx];
		if (Prop == nullptr)
		{
			continue;
		}
		const FHISim::FHILootItem& Item = Round.Loot[Idx];

		bool bCarried = false;
		FVector2D CarrierPos = FVector2D::ZeroVector;
		float CarrierYaw = 0.0f;
		FVector HandCm = FVector::ZeroVector;
		bool bHaveHand = false;
		if (Item.bTaken && !Item.bDelivered)
		{
			for (int32 Man = 0; Man < Round.Intruders.Num(); ++Man)
			{
				const FHISim::FHIThief& T = Round.Intruders[Man];
				if (T.Carry != Item.Label)
				{
					continue;
				}
				bCarried = true;
				CarrierPos = T.Pos;
				CarrierYaw = T.Yaw;
				// The HAND, because the original takes the carried item's position from the
				// hand bone and says why: "the hand bone moves with the animation, so taking
				// the position from it is what makes the object look carried rather than glued
				// to his middle". The port held it at a FIXED 1.05 m chest height instead,
				// which puts a 1.1 m television at the eye line -- reported from play as the
				// carried item taking up most of the screen. A hand rides at hip height, so
				// the load stops blocking the sight line.
				if (IntruderActors.IsValidIndex(Man) && IntruderActors[Man] != nullptr
					&& IntruderActors[Man]->Body != nullptr)
				{
					const FVector Hand = IntruderActors[Man]->Body->GetSocketLocation(TEXT("hand_r"));
					if (!Hand.IsNearlyZero())
					{
						HandCm = Hand;
						bHaveHand = true;
					}
				}
				break;
			}
		}
		Prop->SyncFromSim(Item, bCarried, CarrierPos, CarrierYaw, HandCm, bHaveHand);
	}

	TickHarness(DeltaSeconds);
}

void AHIGameMode::TickHarness(float DeltaSeconds)
{
	// A snap run enters the harness for the shutter and the quit ONLY -- everything above the
	// capture block (the soak report) is inert in it, and the player stays manual below.
	if (!bScripted && !bAuto && !bSnapRun)
	{
		return;
	}

	// ---- quit, once a capture has been asked for and the viewport has had time to write it
	if (QuitCountdownFrames != INDEX_NONE)
	{
		if (--QuitCountdownFrames <= 0)
		{
			// A capture run must END. Without this the process sits in -game forever and the
			// caller sees a timeout, which is indistinguishable from a hang.
			FPlatformMisc::RequestExit(false);
		}
		return;
	}

	// A sky light captured on frame zero captured a world that had not been drawn yet, so it
	// is recaptured once the scene exists. Cheap, once.
	static bool bRecaptured = false;
	if (!bRecaptured && RoundSeconds > 0.5f)
	{
		bRecaptured = true;
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<ASkyLight> It(World); It; ++It)
			{
				if (USkyLightComponent* Fill = It->GetLightComponent())
				{
					Fill->RecaptureSky();
				}
			}
		}
	}

	// ---- the soak's own report
	if (SoakSeconds > 0.0f && !bSoakReported && RoundSeconds >= SoakSeconds)
	{
		bSoakReported = true;
		ReportSoak();
		FPlatformMisc::RequestExit(false);
		return;
	}

	// ---- a capture
	//
	// THE GATE IS A SIM CLOCK THAT STOPS, AND THAT IS THE BUG THIS BLOCK EXISTS TO FIX.
	//
	// `RoundSeconds` is only advanced inside the LIVE-round branch of `Tick` (see "the round
	// clock is sim time" there), so the moment a round is decided the clock FREEZES. A capture
	// scheduled later than the round's last second is therefore not late -- it is
	// UNREACHABLE -- and the old gate `RoundSeconds < ShotAfterSeconds` returned for ever. The
	// process sat in `-game` until it was killed, which is indistinguishable from a hang, and
	// it cost this project two runs of 75 s and 120 s of exactly that.
	//
	// It is worth being precise about WHAT it photographed, because it was worse than wasted
	// time: the autopilot clears the house in about five sim seconds, so every capture that
	// DID succeed was one where the round ran long enough -- which, with the autopilot off and
	// the homeowner standing still, means the round ran long because he was being shot. **The
	// harness could only photograph a round that outlived its timer, and the sweep that would
	// have caught bug 15 needed a round that did not.** A capture run must always end, so it
	// now captures where the clock actually stopped and says how many frames that cost.
	if (ShotName.IsEmpty() || bShotTaken)
	{
		return;
	}
	const bool bSeries = ShotSeries.Num() > 0;
	const int32 TotalShots = bSeries ? ShotSeries.Num() : 1;
	if (ShotSeriesNext >= TotalShots)
	{
		bShotTaken = true;
		return;
	}

	const bool bRoundLive = Round.State == FHISim::EHIRoundState::Play;
	const float TargetTime = bSeries ? ShotSeries[ShotSeriesNext] : ShotAfterSeconds;
	bool bGiveUpTheRest = false;
	if (RoundSeconds < TargetTime)
	{
		if (bRoundLive)
		{
			return;   // still coming; the clock is running
		}
		// Decided, and the clock has stopped short of the target. Waiting is a hang and
		// skipping is a lie about what was photographed, so take it here and account for the
		// frames that can no longer happen.
		ShotSeriesMissed += TotalShots - ShotSeriesNext - 1;
		bGiveUpTheRest = true;
		UE_LOG(LogTemp, Warning,
			TEXT("HI: round decided at %.2f s, before the %.2f s capture -- taking it at %.2f s instead; %d further frame(s) unreachable"),
			RoundSeconds, TargetTime, RoundSeconds, TotalShots - ShotSeriesNext - 1);
	}

	// One file per frame: a series that overwrote one name would look exactly like a single
	// capture, which is the failure this whole harness keeps having to be defended against.
	const FString CaptureName = bSeries
		? FString::Printf(TEXT("%s_%02d"), *ShotName, ShotSeriesNext + 1)
		: ShotName;
	++ShotSeriesNext;
	const bool bLastShot = bGiveUpTheRest || ShotSeriesNext >= TotalShots;
	if (bLastShot)
	{
		bShotTaken = true;
	}

	FScreenshotRequest::RequestScreenshot(CaptureName, /*bShowUI=*/!bHideUIForShot, /*bAddFilenameSuffix=*/false);

	// WHERE THE CAMERA ACTUALLY IS, measured rather than assumed. The first batch of aimed
	// captures produced the same interior view from three different commanded positions, and
	// a screenshot alone cannot distinguish "the override did not apply" from "the override
	// applied and the world looks like that". This line is what tells them apart.
	{
		FString Where = TEXT("<no pawn>");
		if (UWorld* World = GetWorld())
		{
			if (const APlayerController* PC = World->GetFirstPlayerController())
			{
				if (const AHIPlayerCharacter* Pawn = Cast<AHIPlayerCharacter>(PC->GetPawn()))
				{
					const FVector Cam = Pawn->Camera->GetComponentLocation() / HI_METERS_TO_UU;
					Where = FString::Printf(TEXT("camera at (%.2f, %.2f, %.2f) m"),
						Cam.X, Cam.Y, Cam.Z);
				}
			}
		}
		// The SIM SPEED is logged because it is the number that made every earlier capture feel
		// slow: an offscreen run on this box renders at about 2 fps, so a "6 second" capture is
		// nearer 24 seconds of wall clock, and a series reads as a hang when it is not one.
		// Same expression as the soak's report, so the two cannot disagree.
		const float SimSpeed = Frames / FMath::Max(RoundSeconds, 0.001f) / 60.0f;
		UE_LOG(LogTemp, Display,
			TEXT("HI: shot %d/%d '%s' at %.2f s of round (sim at %.2fx realtime) -- sim player (%.2f, %.2f) yaw %.1f deg pitch %.1f deg; %s"),
			ShotSeriesNext, TotalShots, *CaptureName, RoundSeconds, SimSpeed,
			Round.Player.Pos.X, Round.Player.Pos.Y,
			FMath::RadiansToDegrees(Round.Player.Yaw), FMath::RadiansToDegrees(Round.Player.Pitch),
			*Where);
	}

	// The screenshot is written by the viewport on a later frame, so quitting immediately
	// would produce no file -- and a missing file looks exactly like a rendering failure. Only
	// after the LAST frame: quitting after the first is what made a sweep impossible.
	if (bLastShot)
	{
		QuitCountdownFrames = 10;
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("HI: %d more frame(s) scheduled; the run continues"), TotalShots - ShotSeriesNext);
	}
}

void AHIGameMode::ApplyScheduledWeapon()
{
	for (int32 Idx = 0; Idx < WeaponSchedule.Num(); ++Idx)
	{
		// Every cue is checked every frame rather than a cursor being advanced, so the list does
		// not have to be sorted and a late cue cannot be skipped by a long frame.
		if (!WeaponScheduleFired.IsValidIndex(Idx) || WeaponScheduleFired[Idx])
		{
			continue;
		}
		if (RoundSeconds < WeaponSchedule[Idx].Time)
		{
			continue;
		}
		if (!PendingWeaponCue.IsEmpty())
		{
			// One at a time. The latched cue has not reached a sim step yet, and overwriting it
			// would lose a switch as surely as the bug this latch exists to fix.
			break;
		}
		WeaponScheduleFired[Idx] = true;
		// The SAME field the [1]/[2] keys write, delivered by the step loop, so this cannot
		// become a private path that moves the sim's weapon without the visual following it.
		PendingWeaponCue = WeaponSchedule[Idx].Key;
		UE_LOG(LogTemp, Display, TEXT("HI: -HIWeaponAt due '%s' at %.2f s of round"),
			*WeaponSchedule[Idx].Key, RoundSeconds);
	}
}

void AHIGameMode::ApplyStartWeapon()
{
	// `-HIWeapon=shotgun` starts the round holding the long gun. A capture cannot press [2], and
	// "does the shotgun read in the hand" is otherwise unobservable without a person.
	//
	// It goes through the SAME `SwitchWeapon` the key does, so this is not a private path that
	// can be right while the player's is wrong -- and `HI.Sim.Verbs` already asserts that path.
	FString Want;
	if (FParse::Value(FCommandLine::Get(), TEXT("HIWeapon="), Want) && !Want.IsEmpty())
	{
		Round.SwitchWeapon(Want);
	}
}

void AHIGameMode::ReportSoak() const
{
	int32 Alive = 0;
	for (const FHISim::FHIThief& T : Round.Intruders)
	{
		if (T.bAlive)
		{
			++Alive;
		}
	}

	const TCHAR* State = TEXT("PLAY");
	if (Round.State == FHISim::EHIRoundState::Won)  { State = TEXT("WON"); }
	if (Round.State == FHISim::EHIRoundState::Lost) { State = TEXT("LOST"); }

	// WHERE EVERY BODY ACTUALLY IS, and this exists because the suite could not answer the
	// question. `HI.Sim.ThiefAI` proves a heist works -- with ONE thief, driven by
	// `StepThief` directly. Every runtime round uses three, through `StepIntruders`, and no
	// test runs a full heist that way. So when a 240-second probe delivered nothing, the
	// summary line could say only that; these lines are what say WHY.
	for (const FHISim::FHIThief& T : Round.Intruders)
	{
		FString Route = TEXT("<none>");
		if (T.Route.Num() > 0)
		{
			Route = FString::Printf(TEXT("%d to (%.1f,%.1f)"), T.Route.Num(),
				T.Route.Last().X, T.Route.Last().Y);
		}
		UE_LOG(LogTemp, Display,
			TEXT("HISOAK THIEF id=%d alive=%d escaped=%d mode=%-8s pos=(%.2f,%.2f) carry='%s' target='%s' route=%s think=%.2f stuck=%.2f fear=%.2f regrab=%.2f"),
			T.Id, T.bAlive ? 1 : 0, T.bEscaped ? 1 : 0, *T.Mode, T.Pos.X, T.Pos.Y,
			*T.Carry, *T.Target, *Route, T.Think, T.StuckT, T.Fear, T.Regrab);
	}
	for (const FHISim::FHILootItem& L : Round.Loot)
	{
		UE_LOG(LogTemp, Display,
			TEXT("HISOAK LOOT '%s' pos=(%.2f,%.2f) node=%s known=%d taken=%d delivered=%d"),
			*L.Label, L.Pos.X, L.Pos.Y, *L.Node, L.bKnown ? 1 : 0, L.bTaken ? 1 : 0,
			L.bDelivered ? 1 : 0);
	}

	// Printed with a stable prefix so a caller can grep it and so a run that never resolved
	// is visibly different from one that did. `ended=round` and `ended=budget` are different
	// findings: the first is a game that resolved, the second is a game that ran out of time
	// without resolving, which is the failure mode the Godot probes were written to catch.
	const bool bBudget = SoakSeconds > 0.0f && RoundSeconds >= SoakSeconds;
	if (bNoThiefDamage)
	{
		UE_LOG(LogTemp, Warning, TEXT("HISOAK NOTE: -HINoThiefDamage was set, so this run says nothing about LETHALITY -- only about whether the heist can be carried out"));
	}
	UE_LOG(LogTemp, Display,
		TEXT("HISOAK RESULT=%s ended=%s sim_seconds=%.1f frames=%d sim_steps=%d realtime_ratio=%.2f intruders=%d alive=%d down=%d loot_delivered=%d loot_remaining=%d player_hp=%.0f shots=%d"),
		State, bBudget ? TEXT("budget") : TEXT("round"), RoundSeconds, Frames, SimSteps,
		SimSteps > 0 ? (Frames / FMath::Max(RoundSeconds, 0.001f)) / 60.0f : 0.0f,
		Round.Intruders.Num(), Alive,
		Round.ThievesDown, Round.Loot.Num() - Round.Remaining(), Round.Remaining(),
		Round.Player.Hp, PlayerShotsFired);

	if (Alive > 0 && Round.Player.bAlive && bBudget)
	{
		UE_LOG(LogTemp, Warning, TEXT("HISOAK INCONCLUSIVE: the round neither ended nor killed the homeowner within its budget"));
	}
}
