#include "HICaptive.h"

#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "HISim.h"
#include "HICharAnim.h"
#include "HIMaterials.h"
#include "HIUnits.h"

AHICaptive::AHICaptive()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
	// The body is the root: a hanging man has no character movement component and no capsule to
	// stand on, so its transform IS the cell.
	SetRootComponent(Body);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetGenerateOverlapEvents(false);

	Hit = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Hit"));
	Hit->SetupAttachment(Body);
	Hit->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Hit->SetCollisionResponseToAllChannels(ECR_Ignore);
	Hit->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Hit->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	// The same man the sim models: a capsule of ThiefRadius and the intruder's height. The intruder
	// makes its capsule for exactly this reason -- the sim decides hit points, not the mesh.
	Hit->SetCapsuleSize(FHISim::ThiefRadius * HI_METERS_TO_UU, 0.85f * HI_METERS_TO_UU);
	Hit->SetRelativeLocation(FVector(0.0f, 0.0f, 0.85f * HI_METERS_TO_UU));
}

void AHICaptive::BeginPlay()
{
	Super::BeginPlay();
}

void AHICaptive::Hang(int32 InThiefId, const FString& InTag, const FVector& Station)
{
	ThiefId = InThiefId;
	CastTag = InTag;

	// THE SAME BODY HE HAD. Same mesh, same materials, same face -- and the same character setup,
	// which is what hides the brute's baked axe.
	const FString MeshPath = FHICharAnim::MeshPathFor(*CastTag);
	if (USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath))
	{
		Body->SetSkeletalMeshAsset(Mesh);
		FHICharAnim::SetupCharacter(Body, *CastTag);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("HI: captive %d's mesh did not load from %s"), ThiefId, *MeshPath);
	}

	// THE HANG CLIP. `HIAnimTool::MakeHangClip` wrote it: one key, this rig's own reference pose with
	// both arms swung overhead and the knees bent, so his wrists are above his head and his feet are
	// clear of the slab.
	const FString HangPath = FString::Printf(TEXT("/Game/HIImported/characters_%s/hang"), *CastTag);
	HangClip = LoadObject<UAnimationAsset>(nullptr, *HangPath);
	if (HangClip != nullptr)
	{
		Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		// LOOPED, because the clip is one breath of sway (1.6 s) and it has to keep breathing.
		Body->SetAnimation(HangClip);
		Body->PlayAnimation(HangClip, /*bLooping=*/true);
	}
	else
	{
		// NOT SILENT: without the clip he stands in the reference pose with his arms out, which is
		// what the restraint looked like before this actor existed.
		UE_LOG(LogTemp, Warning,
			TEXT("HI: captive %s has no /hang clip (looked for %s) -- run tools/make-hang-clips.py"),
			*CastTag, *HangPath);
	}

	// FEET DANGLING. 0.23 m was measured against the basement's 2.05 m headroom. One transform, not
	// two: `Body` is the ROOT, so a "relative" move on it is a move of the whole actor -- setting the
	// station and then the relative offset left him at (0, 0, 23) cm, i.e. the world origin on the
	// ground floor, which is why the first captive capture showed chains and no man.
	// OFF THE FLOOR -- BUT THE BRUTE HANGS LOW ENOUGH FOR HIS ARMS TO BE STRAIGHT UP (Richard,
	// 2026-11-05: "lower the Brute by enough so that he can hang by his arms. Lower him about 2 feet
	// and then I hope there will be enough room for his body."). He is 2.2 m of man in a 3.29 m
	// cellar: hung like the women, his head and shoulders were jammed against the deck and the arms
	// had nowhere to go. 2 ft = 0.61 m, so he hangs at 0.19 and the two women keep 0.80.
	const float Lift = (CastTag == TEXT("brute")) ? 0.19f : 0.80f;
	SetActorLocation(Station + FVector(0.0f, 0.0f, Lift * HI_METERS_TO_UU));
	// FACING THE DOOR (Richard, 2026-11-05: "turn the prisoners 90 degrees to the left so that they
	// are facing the entrance to the cell"). The doorway is the 1.00 m gap in the EAST face, and the
	// cell was built so that +X is east, so a quarter turn from the spawn yaw points them at it --
	// and then straightens the spawn yaw out, because a prisoner rotates to face his visitor.
	SetActorRotation(FRotator(7.0f, 90.0f, 3.0f));

	// Chains and cuffs: instanced cubes, restamped every frame.
	const TCHAR* CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");
	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, CubePath))
	{
		ChainInstances = NewObject<UInstancedStaticMeshComponent>(this);
		ChainInstances->SetStaticMesh(Cube);
		ChainInstances->SetMobility(EComponentMobility::Movable);
		ChainInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ChainInstances->RegisterComponent();
		ChainInstances->SetWorldLocation(FVector::ZeroVector);
		FHIMaterials::Tint(ChainInstances, FLinearColor(0.10f, 0.10f, 0.11f));

		CuffInstances = NewObject<UInstancedStaticMeshComponent>(this);
		CuffInstances->SetStaticMesh(Cube);
		CuffInstances->SetMobility(EComponentMobility::Movable);
		CuffInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		CuffInstances->RegisterComponent();
		CuffInstances->SetWorldLocation(FVector::ZeroVector);
		FHIMaterials::Tint(CuffInstances, FLinearColor(0.34f, 0.35f, 0.38f));
	}
	// The ceiling of the basement: the underside of the ground floor's deck.
	const float CeilingZ = (FHISim::BasementZ + FHISim::BasementCeiling) * HI_METERS_TO_UU;
	ChainAnchor[0] = FVector(Station.X, Station.Y, CeilingZ);
	ChainAnchor[1] = FVector(Station.X, Station.Y, CeilingZ);

	bHanging = true;
	UE_LOG(LogTemp, Display, TEXT("HI: %s (%d) chained in the basement at (%.2f, %.2f)"),
		*CastTag, ThiefId, Station.X / HI_METERS_TO_UU, Station.Y / HI_METERS_TO_UU);
}

void AHICaptive::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bHanging && !bDead)
	{
		UpdateChains();
	}
}

void AHICaptive::GoLimp()
{
	if (bDead)
	{
		return;
	}
	bDead = true;
	// Killed on the hook: the chains are cut and the body drops to the slab. The animation must let
	// go first or it writes over the physics every frame (see HIIntruder::GoLimp).
	Body->SetAnimInstanceClass(nullptr);
	Body->SetAnimation(nullptr);
	Body->SetCollisionProfileName(TEXT("Ragdoll"));
	Body->SetAllBodiesSimulatePhysics(true);
	Body->SetSimulatePhysics(true);
	if (ChainInstances != nullptr)
	{
		ChainInstances->ClearInstances();
	}
	if (CuffInstances != nullptr)
	{
		CuffInstances->ClearInstances();
	}
	// Nothing left to aim at: he is a corpse on the floor now, and the sim has already counted him
	// dead. Leaving the capsule enabled would let the player keep shooting a dead body's hit volume.
	if (Hit != nullptr)
	{
		Hit->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	UE_LOG(LogTemp, Display, TEXT("HI: %s (%d) killed in the chains"), *CastTag, ThiefId);
}

void AHICaptive::UpdateChains()
{
	if (Body == nullptr || ChainInstances == nullptr)
	{
		return;
	}
	ChainInstances->ClearInstances();
	if (CuffInstances != nullptr)
	{
		CuffInstances->ClearInstances();
	}
	static const TCHAR* const Hands[2] = { TEXT("LeftHand"), TEXT("RightHand") };
	const int32 Links = 6;
	for (int32 i = 0; i < 2; ++i)
	{
		// Now that the engine ticks this actor, the hand bones ARE the drawn pose -- the thing the
		// intruder version could never rely on.
		const FVector Wrist = Body->GetSocketLocation(Hands[i]);
		// The anchor is the wrist's own column at the ceiling, recomputed every frame: the first
		// version stored them once in Hang() and the drawn chains converged on a point in the middle
		// of the room instead of hanging plumb over the two men.
		const float CeilingZ = (FHISim::BasementZ + FHISim::BasementCeiling) * HI_METERS_TO_UU;
		ChainAnchor[i] = FVector(Wrist.X, Wrist.Y, CeilingZ);
		if (FVector::Dist(Wrist, GetActorLocation()) > 2.5f * HI_METERS_TO_UU)
		{
			continue;   // a socket this far out means no pose; better no chain than a chain to nowhere
		}
		const FVector Span = Wrist - ChainAnchor[i];
		const float Len = Span.Size();
		if (Len < 2.0f)
		{
			continue;
		}
		const FVector Dir = Span / Len;
		for (int32 L = 0; L < Links; ++L)
		{
			const float T = (float(L) + 0.5f) / float(Links);
			const FVector At = ChainAnchor[i] + Dir * (Len * T);
			// Alternating roll is what reads as a chain rather than a rod.
			const FRotator Rot = FRotationMatrix::MakeFromZ(Dir).Rotator()
				+ FRotator(0.0f, (L % 2 == 0) ? 0.0f : 90.0f, 0.0f);
			ChainInstances->AddInstance(FTransform(Rot, At,
				FVector(0.022f, 0.022f, Len / float(Links) * 0.62f)));
		}
		if (CuffInstances != nullptr)
		{
			const FRotator CuffRot = FRotationMatrix::MakeFromZ(Dir).Rotator();
			CuffInstances->AddInstance(FTransform(CuffRot, Wrist, FVector(0.085f, 0.070f, 0.070f)));
		}
	}
}
