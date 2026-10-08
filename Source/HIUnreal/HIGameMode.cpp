#include "HIGameMode.h"

#include "GameFramework/PlayerController.h"
#include "HIHouse.h"
#include "HIHUD.h"
#include "HIIntruder.h"
#include "HIPlayerCharacter.h"
#include "HIUnits.h"

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

void AHIGameMode::BeginPlay()
{
	Super::BeginPlay();
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
			Actor->SyncFromSim(T);
			IntruderActors.Add(Actor);
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
	return In;
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
	FVector Origin;
	FVector Dir;
	AHIPlayerCharacter::ViewTransform(Round.Player, Origin, Dir);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(HIShot), /*bTraceComplex=*/false);
	Params.AddIgnoredActor(Pawn);

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
		}
	}
}

void AHIGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (Round.State != FHISim::EHIRoundState::Play)
	{
		// A decided round is inert at the sim level too, so there is nothing to gain by
		// stepping it -- and stepping it anyway would keep bodies robbing a house whose
		// outcome is already settled.
		return;
	}

	// Look first, then step: FHIRound moves the homeowner relative to Player.Yaw, so a view
	// applied afterwards would steer him one frame behind where the player is looking.
	ApplyLook();
	Round.Step(ReadInput(), DeltaSeconds);
	LastEvents = Round.ConsumeEvents();

	// Resolve the shots the round announced. This is the actor layer's half of firing: the
	// round has already spent the round, started the cooldown and told everyone who heard
	// it, so all that is left is the one thing a sim with no world cannot do.
	for (const FHISim::FHIEvent& Ev : LastEvents)
	{
		if (Ev.Type == TEXT("playerShot"))
		{
			ResolvePlayerShot(Ev);
		}
	}

	// Pull every view into place AFTER the step, from the same Tick. Nothing else writes an
	// actor transform for this round, so there is no frame where a body is drawn from a
	// half-updated model.
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (AHIPlayerCharacter* Pawn = Cast<AHIPlayerCharacter>(PC->GetPawn()))
		{
			Pawn->SyncFromSim(Round.Player);
		}
	}

	const int32 Count = FMath::Min(IntruderActors.Num(), Round.Intruders.Num());
	for (int32 Idx = 0; Idx < Count; ++Idx)
	{
		if (IntruderActors[Idx] != nullptr)
		{
			IntruderActors[Idx]->SyncFromSim(Round.Intruders[Idx]);
		}
	}
}
