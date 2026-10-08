#include "HIPlayerCharacter.h"

#include "Camera/CameraComponent.h"

AHIPlayerCharacter::AHIPlayerCharacter()
{
	// The game mode is the only ticker. See the class comment.
	PrimaryActorTick.bCanEverTick = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	// The camera IS the root: the sim owns the body's position, and this actor is placed
	// wherever the view goes, so there is nothing for a body mesh to be relative to.
	RootComponent = Camera;
	Camera->bUsePawnControlRotation = false;
}

FVector AHIPlayerCharacter::EyePivot(const FHISim::FHIPlayer& P)
{
	// The sim's plane is (X, Y) with Z up, so a 2-D position plus Z is already a world
	// position. Eye height is added here rather than stored, so a crouch is a change of
	// constant and not a change of state.
	return FVector(P.Pos.X, P.Pos.Y, P.Z + FHISim::EyeHeight);
}

void AHIPlayerCharacter::ViewTransform(const FHISim::FHIPlayer& P, FVector& OutLocation, FVector& OutDirection)
{
	OutLocation = EyePivot(P) + FHISim::CameraOffset(P.Yaw, P.Pitch);
	OutDirection = FHISim::AimDir(P.Yaw, P.Pitch);
}

void AHIPlayerCharacter::SyncFromSim(const FHISim::FHIPlayer& P)
{
	FVector Location;
	FVector Direction;
	ViewTransform(P, Location, Direction);

	SetActorLocation(Location);
	SetActorRotation(Direction.Rotation());
}
