#include "HIIntruder.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AHIIntruder::AHIIntruder()
{
	// The game mode is the only ticker. See the class comment.
	PrimaryActorTick.bCanEverTick = false;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	RootComponent = Body;
	// No collision: the sim owns every overlap decision, and a second opinion from the
	// physics engine would be a second source of truth for where bodies may stand.
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// An engine basic shape, so this needs no project assets. The cylinder is 100 cm tall
	// and 100 cm across by default; the sim's intruder is 1.8 m and 0.84 m wide, so the
	// scale below is the mesh being made to match the model rather than a fudge factor.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> MeshFinder(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (MeshFinder.Succeeded())
	{
		Body->SetStaticMesh(MeshFinder.Object);
		Body->SetRelativeScale3D(FVector(0.84f, 0.84f, 0.90f));
		// The cylinder's origin is its centre, so lift it half its height to stand on the
		// ground the sim places him on.
		Body->SetRelativeLocation(FVector(0.0f, 0.0f, 90.0f));
	}
}

FRotator AHIIntruder::BodyRotation(float SimYaw)
{
	return FRotator(0.0f, 90.0f - FMath::RadiansToDegrees(SimYaw), 0.0f);
}

void AHIIntruder::SyncFromSim(const FHISim::FHIThief& T)
{
	// The sim's plane is (X, Y) with Z up, so a 2-D position plus Z is already a world
	// position -- no axis swap here, unlike Godot where the plane is XZ.
	SetActorLocation(FVector(T.Pos.X, T.Pos.Y, T.Z));
	SetActorRotation(BodyRotation(T.Yaw));
}
