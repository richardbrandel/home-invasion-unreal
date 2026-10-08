#include "HIGameMode.h"

#include "GameFramework/PlayerController.h"
#include "HIIntruder.h"
#include "HIPlayerCharacter.h"

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
}

void AHIGameMode::BeginPlay()
{
	Super::BeginPlay();
	Round = FHISim::FHIRound::Start(IntruderCount);

	// One view per intruder, spawned from the round's own count so the two cannot disagree
	// about how many men are in the house.
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
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
