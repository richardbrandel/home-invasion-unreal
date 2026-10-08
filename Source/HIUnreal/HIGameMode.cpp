#include "HIGameMode.h"

#include "GameFramework/PlayerController.h"

AHIGameMode::AHIGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	// PrePhysics, and it matters: the round has to advance before anything reads it to
	// place a body or draw a frame, or every view is one step behind the model.
	PrimaryActorTick.TickGroup = TG_PrePhysics;
}

void AHIGameMode::BeginPlay()
{
	Super::BeginPlay();
	Round = FHISim::FHIRound::Start(IntruderCount);
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

	Round.Step(ReadInput(), DeltaSeconds);
	LastEvents = Round.ConsumeEvents();
}
