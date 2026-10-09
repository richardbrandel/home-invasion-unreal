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
#include "HIHouse.h"
#include "HIHUD.h"
#include "HIIntruder.h"
#include "HILootProp.h"
#include "HIMarks.h"
#include "HIMaterials.h"
#include "HIPlayerCharacter.h"
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
	FParse::Value(Cmd, TEXT("HIShotAfter="), ShotAfterSeconds);
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
	bScripted = !ShotName.IsEmpty() || SoakSeconds > 0.0f;
	bAuto = bAutoAsked || (SoakSeconds > 0.0f && !bAutoOff) || (!ShotName.IsEmpty() && !bPosed);
	if (bAutoOff)
	{
		bAuto = false;
	}

	if (bScripted || bAuto)
	{
		UE_LOG(LogTemp, Display, TEXT("HI: scripted run -- thieves=%d shot='%s' after=%.1f soak=%.1f auto=%d posed=%d no_thief_damage=%d"),
			IntruderCount, *ShotName, ShotAfterSeconds, SoakSeconds, bAuto ? 1 : 0, bPosed ? 1 : 0,
			bNoThiefDamage ? 1 : 0);
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
	static const FLamp Lamps[] =
	{
		// 800 lumens, which is one ordinary LED bulb rather than the 2600 this started at.
		// The first value was chosen against a fixed exposure that has since been reversed,
		// and against auto-exposure a house lit to 2600 lumens a room looks like an office.
		{ -5.00f, -11.00f,  900.0f },   // living room
		{  4.00f, -11.00f,  800.0f },   // kitchen
		{ -4.00f, -16.50f,  700.0f },   // bedroom
		{  4.00f, -16.50f,  700.0f },   // study
		{  0.00f, -14.00f,  700.0f },   // hall
	};
	for (const FLamp& L : Lamps)
	{
		if (APointLight* Lamp = World->SpawnActor<APointLight>(
			APointLight::StaticClass(),
			FTransform(FRotator::ZeroRotator, FVector(L.X, L.Y, 2.60f) * HI_METERS_TO_UU), Params))
		{
			if (UPointLightComponent* Bulb = Cast<UPointLightComponent>(Lamp->GetLightComponent()))
			{
				Bulb->SetMobility(EComponentMobility::Movable);
				// Lumens, because that is what a lamp is specified in. Setting candelas and
				// calling it "intensity" is how a room ends up either unlit or nuclear.
				Bulb->SetIntensityUnits(ELightUnits::Lumens);
				Bulb->SetIntensity(L.Intensity);
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

	// Axis names come from Config/DefaultInput.ini. The sim's plane is (X, Y) with +Y
	// forward, so MoveRight maps to the X component and MoveForward to the Y one -- and
	// "+1 forward" is the convention FHIRound::Step already expects.
	In.Axis = FVector2D(PC->GetInputAxisValue(TEXT("MoveRight")),
		PC->GetInputAxisValue(TEXT("MoveForward")));
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
	const APlayerController* PC = World->GetFirstPlayerController();
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

	Round.Player.Yaw += PC->GetInputAxisValue(TEXT("Turn")) * LookRadiansPerAxisUnit;
	// The look axis is bound inverted (see DefaultInput.ini), so pushing the mouse up
	// raises the pitch rather than lowering it.
	Round.Player.Pitch = FMath::Clamp(
		Round.Player.Pitch + PC->GetInputAxisValue(TEXT("LookUp")) * LookRadiansPerAxisUnit,
		FHISim::PitchMin, FHISim::PitchMax);
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

		// Close the distance, and fire once he is worth shooting at rather than at the sky.
		if (Dist > 2.5f)
		{
			In.Axis = FVector2D(0.0f, 1.0f);
		}
		In.bFire = FMath::Abs(Delta) < 0.25f;
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
	FHIAudio::PlayerGunshot(World, Origin, Round.Player.Weapon == TEXT("shotgun"));

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
		if (World->LineTraceSingleByChannel(Hit, Origin, End, ECC_Visibility, Params))
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
					Mark->Stamp(Hit.ImpactPoint, Hit.ImpactNormal,
						bFlesh ? 0.07f : 0.10f,
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

float AHIGameMode::ThiefHitChance(float RangeMeters)
{
	// Falloff 0.035 per metre, floor 0.05. Both are the Godot original's CURRENT values,
	// read out of game.gd's `_drain_events` rather than from a note about them -- the notes
	// in the project memory carry an older pair (0.02 and 0.55) that is no longer true.
	return FMath::Clamp(1.0f - RangeMeters * 0.035f, 0.05f, 1.0f);
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
				Mark->Stamp(WallHit.ImpactPoint, WallHit.ImpactNormal, 0.10f, HoleColour);
			}
		}
		return;
	}

	// Clear line. Range falls off with a floor of 0.05, so distance is worth keeping: the
	// Godot original's older 0.55 floor meant he hit more than half the time at ANY range,
	// and closing or opening the gap bought the player nothing.
	const float Chance = ThiefHitChance(RangeM);
	if (FMath::FRand() <= Chance)
	{
		FlashLine(FromW + FVector(0.0f, 0.0f, 10.0f), ToW, 0.02f, FLinearColor(1.0f, 0.85f, 0.4f));
		// The report goes into the ROUND's event list, not into LastEvents, because that is
		// the list the round's own rules read -- `playerDown` has to reach ApplyRoundRules or
		// a lethal hit would leave the round in Play forever.
		//
		// The trace, the tracer and the roll all still happen under -HINoThiefDamage; only
		// the wound is withheld, so the probe exercises the same code path a real hit does.
		if (!bNoThiefDamage)
		{
			FHISim::DamagePlayer(Round.Player, Shot.Damage, Round.Events);
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
	}
}

void AHIGameMode::RestartRound()
{
	Round = FHISim::FHIRound::Start(IntruderCount);
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
	FHISim::FHIInput In = ReadInput();
	if (bAuto)
	{
		Autopilot(In);
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
	while (SimAccumulator >= SimFixedDt && StepsThisFrame < SimMaxStepsPerFrame)
	{
		Round.Step(In, SimFixedDt);
		SimAccumulator -= SimFixedDt;
		++StepsThisFrame;
		++SimSteps;
	}

	// THE ROUND CLOCK IS SIM TIME, not wall time, and the difference is not academic. Every
	// timer in this game -- the police clock, the weapon cooldown, the intruder's fear decay
	// -- advances by the sim's own dt, so a clock counting wall seconds would disagree with
	// the game it is describing. It matters most exactly where it was found: an offscreen
	// probe on this box renders at ~2 fps and is clamped to 8 sub-steps a frame, so the sim
	// advances at roughly a QUARTER of real speed. A soak budget read as real seconds would
	// therefore end a "240 second" probe after about 64 seconds of game.
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
		}
		else if (Ev.Type == TEXT("thiefShot"))
		{
			ResolveThiefShot(Ev);
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
				// Item 16's bark system, with a placeholder cue: a man going down is the one
				// moment a voice would carry furthest, and the hook has to exist before a
				// recording can be dropped into it.
				FHIAudio::Bark(World, FVector(Who->Pos.X, Who->Pos.Y, 1.4f) * HI_METERS_TO_UU, 0.55f);
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
			Message = TEXT("He shoved you back!");
			MessageTime = 1.6f;
		}
		else if (Ev.Type == TEXT("shouted"))
		{
			Message = TEXT("You shouted. They know where you are.");
			MessageTime = 1.6f;
			// Item 16's bark hook, from the homeowner this time. Placeholder cue, as above.
			FHIAudio::Bark(World, FVector(Round.Player.Pos.X, Round.Player.Pos.Y, 1.6f) * HI_METERS_TO_UU, 0.85f);
		}
		else if (Ev.Type == TEXT("calledPolice"))
		{
			Message = TEXT("You called the police. They are on their way.");
			MessageTime = 3.0f;
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
		}
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
		if (Item.bTaken && !Item.bDelivered)
		{
			for (const FHISim::FHIThief& T : Round.Intruders)
			{
				if (T.Carry == Item.Label)
				{
					bCarried = true;
					CarrierPos = T.Pos;
					CarrierYaw = T.Yaw;
					break;
				}
			}
		}
		Prop->SyncFromSim(Item, bCarried, CarrierPos, CarrierYaw);
	}

	TickHarness(DeltaSeconds);
}

void AHIGameMode::TickHarness(float DeltaSeconds)
{
	if (!bScripted && !bAuto)
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
	if (ShotName.IsEmpty() || bShotTaken || RoundSeconds < ShotAfterSeconds)
	{
		return;
	}
	bShotTaken = true;

	FScreenshotRequest::RequestScreenshot(ShotName, /*bShowUI=*/!bHideUIForShot, /*bAddFilenameSuffix=*/false);

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
		UE_LOG(LogTemp, Display,
			TEXT("HI: shot '%s' at %.2f s -- sim player (%.2f, %.2f) yaw %.1f deg pitch %.1f deg; %s"),
			*ShotName, RoundSeconds, Round.Player.Pos.X, Round.Player.Pos.Y,
			FMath::RadiansToDegrees(Round.Player.Yaw), FMath::RadiansToDegrees(Round.Player.Pitch),
			*Where);
	}
	UE_LOG(LogTemp, Display, TEXT("HI: screenshot '%s' requested at %.2f s of round"),
		*ShotName, RoundSeconds);

	// The screenshot is written by the viewport on a later frame, so quitting immediately
	// would produce no file -- and a missing file looks exactly like a rendering failure.
	QuitCountdownFrames = 10;
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
