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
	// FACING THE DOOR. Richard, 2026-11-05: "turn the prisoners 90 degrees to the left so that they
	// are facing the entrance to the cell", and then, on the capture: "you turned the prisoners the
	// wrong way. Now if you turn them 180 degrees, I think they will be OK."
	//
	// He was right: at 90 they showed the doorway their BACKS (the camera stands outside the east
	// face, and the shot had their shoulder blades to it). 270 faces the opening. The lesson is that
	// +90 was reasoned from the cell's axes rather than read off a frame, and the bind facing of these
	// meshes is not the actor's forward -- which is the same trap that has bitten the walking yaw and
	// the hang pose before.
	SetActorRotation(FRotator(7.0f, 270.0f, 3.0f));

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

	// ---- A SQUIRM, if one is running. Each reaction moves a different thing, and every one of them
	// shakes the body as well: the clip alone was not enough -- reaction 2 played the idle clip and
	// the brute simply stood there, which read as calm rather than as a man being beaten.
	if (SquirmKind >= 0 && !bDead)
	{
		SquirmT += DeltaSeconds;
		if (SquirmT >= SquirmLen)
		{
			EndSquirm();
		}
		else
		{
			const float T = SquirmT;
			const float Fade = FMath::Min(1.0f, (SquirmLen - T) / 0.25f);   // eases out
			float YawA, PitchA, RollA, Hz, BounceA;
			if (SquirmKind == 0)
			{
				// RUNNING IN PLACE: the legs do the work (the walk clip at 1.4x), the body just
				// pitches into each stride.
				YawA = 3.0f; PitchA = 2.0f; RollA = 2.0f; Hz = 2.4f; BounceA = 0.02f;
			}
			else if (SquirmKind == 1)
			{
				// THRASHING: fast and violent.
				YawA = 10.0f; PitchA = 3.0f; RollA = 5.0f; Hz = 6.0f; BounceA = 0.03f;
			}
			else
			{
				// WRITHING: slower and bigger, on top of the idle clip at 2.6x.
				YawA = 7.0f; PitchA = 5.0f; RollA = 4.0f; Hz = 3.0f; BounceA = 0.04f;
			}
			const FRotator Shake(
				PitchA * FMath::Sin(2.0f * PI * Hz * 0.8f * T + 1.9f) * Fade,
				YawA * FMath::Sin(2.0f * PI * Hz * T) * Fade,
				RollA * FMath::Sin(2.0f * PI * Hz * 1.3f * T + 0.7f) * Fade);
			SetActorRotation(HangRotation + Shake);
			const float Bounce = BounceA * FMath::Abs(FMath::Sin(2.0f * PI * Hz * 1.7f * T)) * Fade;
			// Absolute, from the hook position: adding a delta every frame walked him upward.
			SetActorLocation(HangLocation + FVector(0.0f, 0.0f, Bounce * HI_METERS_TO_UU));
		}
	}
}

void AHICaptive::Squirm()
{
	if (bDead || !bHanging)
	{
		return;
	}
	// Three reactions, picked per hit and never the same one twice in a row -- a prisoner who
	// answers every blow with the same twitch reads as a bug, not as a man.
	int32 Kind = FMath::RandRange(0, 2);
	if (Kind == LastSquirmKind)
	{
		Kind = (Kind + 1) % 3;
	}
	LastSquirmKind = Kind;
	BeginSquirm(Kind);
}

void AHICaptive::SquirmAs(int32 Kind)
{
	if (bDead || !bHanging)
	{
		return;
	}
	LastSquirmKind = Kind;
	BeginSquirm(FMath::Clamp(Kind, 0, 2));
}

void AHICaptive::BeginSquirm(int32 Kind)
{
	// 0 -- RUNNING IN PLACE, two seconds. The character's own WALK clip, at 1.4x, on a body that is
	//      not moving anywhere: the legs pump and the arms stay overhead because the clip's arms are
	//      overridden by... nothing, so this is the one reaction that looks choreographed rather
	//      than panicked. It is the one Richard asked for by name.
	// 1 -- THRASHING, 1.4 s: the hang pose held, the whole body shaken -- fast yaw, a little roll and
	//      a bounce, which is what a man on two wrist chains does when a stick lands.
	// 2 -- WRITHING, 1.6 s: the idle clip at 2.6x, so the limbs jerk, plus a slow quarter-turn twist.
	SquirmKind = Kind;
	SquirmT = 0.0f;
	SquirmLen = (Kind == 0) ? 2.0f : ((Kind == 1) ? 1.4f : 1.6f);
	HangRotation = GetActorRotation();
	HangLocation = GetActorLocation();

	if (Kind == 0)
	{
		const FString WalkPath = FHICharAnim::ClipPathFor(*CastTag, FHICharAnim::ClipWalk());
		if (UAnimationAsset* Walk = LoadObject<UAnimationAsset>(nullptr, *WalkPath))
		{
			Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			Body->SetAnimation(Walk);
			Body->PlayAnimation(Walk, /*bLooping=*/true);
			Body->SetPlayRate(1.4f);
		}
	}
	// Kinds 1 and 2 keep the HANG clip. They are told apart by the shake itself (fast and small vs
	// slow and large), and that matters for more than taste: a full-body clip drops the arms to the
	// character's sides, and a man cuffed to the ceiling with his arms suddenly down looks released
	// rather than struggling. Measured: reaction 2 on the idle clip stood the brute quite still with
	// his hands at his sides. Only reaction 0 changes the clip -- the legs are the whole point of it.
	UE_LOG(LogTemp, Display, TEXT("HI: %s squirms -- reaction %d (%.1f s)"),
		*CastTag, Kind, SquirmLen);
}

void AHICaptive::EndSquirm()
{
	SquirmKind = -1;
	SquirmT = 0.0f;
	if (HangClip != nullptr)
	{
		Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Body->SetAnimation(HangClip);
		Body->PlayAnimation(HangClip, /*bLooping=*/true);
	}
	SetActorRotation(HangRotation);
	SetActorLocation(HangLocation);
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
	static const TCHAR* const Elbows[2] = { TEXT("LeftForeArm"), TEXT("RightForeArm") };
	const int32 Links = 6;
	for (int32 i = 0; i < 2; ++i)
	{
		// Now that the engine ticks this actor, the hand bones ARE the drawn pose -- the thing the
		// intruder version could never rely on.
		// THE WRIST, NOT THE PALM, AND SIZED FROM THE ARM ITSELF (Richard, 2026-11-05: "the brute
		// doesn't have cuffs around his wrists"). The cuff was a fixed 8.5 cm box parked on the hand
		// bone: right for a woman's wrist, and on the brute it sat INSIDE his forearm and never drew.
		// The wrist crease is a quarter of the way from the hand to the elbow, and the cuff's size is
		// a fraction of his own forearm length -- so a big man gets a big cuff without a special case.
		const FVector Palm = Body->GetSocketLocation(Hands[i]);
		const FVector Elbow = Body->GetSocketLocation(Elbows[i]);
		const float ForeLen = FVector::Dist(Palm, Elbow);
		const FVector Wrist = (ForeLen > 1.0f) ? Palm + (Elbow - Palm) * 0.22f : Palm;
		// Measured: forearm 23.2 cm (dancer), 22.7 (Alina), 29.0 (brute); a wrist is ~6-8 cm across,
		// so 0.55 of the forearm with a 12 cm floor leaves steel proud of the skin on every build.
		const float CuffR = FMath::Clamp(0.55f * ForeLen, 0.12f * HI_METERS_TO_UU, 0.19f * HI_METERS_TO_UU);
		if (!bLoggedCuff && ForeLen > 1.0f)
		{
			bLoggedCuff = true;
			UE_LOG(LogTemp, Display, TEXT("HI: CUFF %s forearm=%.1fcm cuff=%.1fcm across (wrist %.1fcm from the palm)"),
				*CastTag, ForeLen, CuffR, FVector::Dist(Palm, Wrist));
		}
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
			// A BAND, NOT A PLATE. This was 8.5 x 4.7 x 4.7 cm -- and 4.7 cm across is INSIDE a
			// forearm, which is why the brute had no visible cuff at all (and the women's were only
			// just there). A cuff has to enclose the wrist: square in section, a few centimetres along
			// the arm, and sized from the man's own forearm so a big arm gets a big shackle.
			CuffInstances->AddInstance(FTransform(CuffRot, Wrist,
				FVector(CuffR, CuffR, 0.035f * HI_METERS_TO_UU)));
		}
	}
}
