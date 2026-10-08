#include "HIIntruder.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "HIUnits.h"
#include "UObject/ConstructorHelpers.h"

AHIIntruder::AHIIntruder()
{
	// The game mode is the only ticker. See the class comment.
	PrimaryActorTick.bCanEverTick = false;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	RootComponent = Body;
	// Query-only, and only for Visibility. The sim owns every MOVEMENT and overlap decision,
	// so the physics engine must never push a body around -- but a pellet trace has to be
	// able to find him, and Visibility is the channel the shot is cast on. So he blocks that
	// one and ignores everything else.
	Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Body->SetCollisionResponseToAllChannels(ECR_Ignore);
	Body->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// An engine basic shape, so this needs no project assets. The cylinder is 100 cm tall and
	// 100 cm across, and the sim's intruder is 2.0 m tall and 0.84 m wide -- so these are
	// the mesh being made to match the model, in the engine's units, rather than a fudge.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> MeshFinder(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (MeshFinder.Succeeded())
	{
		Body->SetStaticMesh(MeshFinder.Object);
		Body->SetRelativeScale3D(FVector(0.84f, 0.84f, 2.0f));
		// The cylinder's origin is its centre, so lift it half its height to stand on the
		// ground the sim places him on. 200 cm tall, so 100 cm up.
		Body->SetRelativeLocation(FVector(0.0f, 0.0f, 100.0f));
	}
}

FRotator AHIIntruder::BodyRotation(float SimYaw)
{
	return FRotator(0.0f, 90.0f - FMath::RadiansToDegrees(SimYaw), 0.0f);
}

void AHIIntruder::SyncFromSim(const FHISim::FHIThief& T)
{
	// The sim's plane is (X, Y) with Z up, so a 2-D position plus Z is already a world
	// position -- no axis swap here, unlike Godot, where the plane is XZ. Only the units
	// need converting, and they go through one constant so a world cannot end up a hundred
	// times too small for the engine's own assumptions.
	SetActorLocation(FVector(T.Pos.X, T.Pos.Y, T.Z) * HI_METERS_TO_UU);
	SetActorRotation(BodyRotation(T.Yaw));
}
