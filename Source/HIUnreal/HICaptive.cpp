#include "HICaptive.h"
#include "HIAudio.h"
#include "HIGameMode.h"
#include "Kismet/GameplayStatics.h"

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
	// AND THE PAWN, or the player walks straight through him (Richard, 2026-11-05: "in the cell,
	// Alina and the Brute are walk through"). A character's own capsule is on ECC_Pawn, which is
	// neither of the two channels above, so the chained men were only shootable, not solid. The
	// dancer read as solid because her imported MESH carries collision of its own; the other two
	// meshes do not, which is exactly the kind of difference that looks like one working body.
	Hit->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	// The same man the sim models: a capsule of ThiefRadius and the intruder's height. The intruder
	// makes its capsule for exactly this reason -- the sim decides hit points, not the mesh.
	Hit->SetCapsuleSize(FHISim::ThiefRadius * HI_METERS_TO_UU, 0.85f * HI_METERS_TO_UU);
	Hit->SetRelativeLocation(FVector(0.0f, 0.0f, 0.85f * HI_METERS_TO_UU));

	// The chains are NOT components of this actor any more: any extra mesh component here -- instanced
	// or plain, cube or cylinder, attached or not -- made the house's GROUND FLOOR lose its wood
	// material and go white, and a captive spawns the moment a man is darted. Bisected to exactly that:
	// chains off, floor is wood; chains on, floor is white. They are stamped through the game mode's
	// mark pool in UpdateChains instead, which is machinery gunfire already uses and which leaves the
	// floors alone.
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
		// One source of solidity -- the capsule -- so all three behave the same whether or not the
		// imported mesh happens to carry its own collision (the dancer's does).
		Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
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
	// The head snap, from the same generator family: `HIAnimTool::MakeFlinchClip` wrote it as a copy of
	// the hang pose with only Head/Neck/Spine2 replaced by a flinch arc.
	FlinchClip = LoadObject<UAnimationAsset>(nullptr,
		*FString::Printf(TEXT("/Game/HIImported/characters_%s/flinch"), *CastTag));
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

	bHanging = true;
	// He wakes up in the cell: one head snap, so the first thing the player sees is a man moving.
	Flinch(-1);
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

	UpdateFlinch(DeltaSeconds);

	// ---- A SQUIRM, if one is running. Each reaction moves a different thing, and every one of them
	// shakes the body as well: the clip alone was not enough -- reaction 2 played the idle clip and
	// the brute simply stood there, which read as calm rather than as a man being beaten.
	if (SquirmKind >= 0 && !bDead)
	{
		SquirmT += DeltaSeconds;
		if (SquirmT >= SquirmLen)
		{
			UE_LOG(LogTemp, Display, TEXT("HI: STICK %s reaction %d done at %.2f s"), *CastTag, SquirmKind, SquirmT);
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

void AHICaptive::SayHitLine()
{
	// One list per character: the brute is a deep man with three macho lines, Alina is a woman who
	// flirts and threatens, and the Dancer is the one who is actually scared.
	TArray<const TCHAR*> Lines;
	if (CastTag == TEXT("brute"))
	{
		Lines = { TEXT("brute_1"), TEXT("brute_2"), TEXT("brute_3") };
	}
	else if (CastTag == TEXT("alina"))
	{
		Lines = { TEXT("alina_1"), TEXT("alina_2"), TEXT("alina_3"), TEXT("alina_4"), TEXT("alina_5") };
	}
	else
	{
		Lines = { TEXT("dancer_1"), TEXT("dancer_2"), TEXT("dancer_3"), TEXT("dancer_4"), TEXT("dancer_5") };
	}
	if (Lines.Num() == 0)
	{
		return;
	}
	int32 Pick = FMath::RandRange(0, Lines.Num() - 1);
	if (Pick == LastHitLine)
	{
		Pick = (Pick + 1) % Lines.Num();
	}
	LastHitLine = Pick;
	if (UWorld* World = GetWorld())
	{
		const FVector Mouth = Body != nullptr ? Body->GetSocketLocation(TEXT("Head")) : GetActorLocation();
		FHIAudio::Voice(World, Mouth, Lines[Pick]);
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
	// AND HE SAYS SOMETHING (Richard, 2026-11-05): each character has its own lines and they are picked
	// at random per blow, so a second hit does not repeat the first. Rendered by Piper (see
	// SourceAssets/voices and tools/make-voices.sh) and imported to /Game/HIImported/voice.
	SayHitLine();
	// A blow lands on the neck first, then the body answers: flinch, then the reaction.
	UE_LOG(LogTemp, Display, TEXT("HI: STICK %s squirm requested -- reaction %d"), *CastTag, Kind);
	Flinch(Kind);
}

void AHICaptive::SquirmAs(int32 Kind)
{
	if (bDead || !bHanging)
	{
		return;
	}
	LastSquirmKind = Kind;
	SayHitLine();
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
	UE_LOG(LogTemp, Display, TEXT("HI: STICK %s reaction %d begins (%.1f s)"), *CastTag, Kind,
		(Kind == 0) ? 2.0f : ((Kind == 1) ? 1.4f : 1.6f));
	SquirmLen = (Kind == 0) ? 2.0f : ((Kind == 1) ? 1.4f : 1.6f);
	HangRotation = GetActorRotation();
	HangLocation = GetActorLocation();

	if (Kind == 0)
	{
		// THE LOWER-BODY RUN, not the walk clip. `HIAnimTool::MakeLowerBodyClip` built `<tag>/hang_walk`:
		// the hang pose above the hips and this character's own WALK legs below, so he runs in place
		// with his wrists still in the shackles. Playing the plain walk clip dropped his arms to his
		// sides and made the whole body a stride -- which is what Richard corrected: "it should only
		// happen for the lower half of the body, the arms should stay in the shackles."
		const FString RunPath = FString::Printf(TEXT("/Game/HIImported/characters_%s/hang_walk"), *CastTag);
		if (UAnimationAsset* Walk = LoadObject<UAnimationAsset>(nullptr, *RunPath))
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
	UE_LOG(LogTemp, Display, TEXT("HI: STICK %s back to hanging"), *CastTag);
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

void AHICaptive::Flinch(int32 ThenSquirmKind)
{
	if (bDead || !bHanging)
	{
		return;
	}
	PendingSquirmKind = ThenSquirmKind;
	bFlinching = true;
	FlinchT = 0.0f;
	if (FlinchClip != nullptr)
	{
		Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Body->SetAnimation(FlinchClip);
		Body->PlayAnimation(FlinchClip, /*bLooping=*/false);
		Body->SetPlayRate(1.0f);
	}
	else
	{
		// No clip (a Content/ that predates the generator): the reaction alone still happens.
		bFlinching = false;
		if (PendingSquirmKind >= 0)
		{
			BeginSquirm(PendingSquirmKind);
		}
	}
}

void AHICaptive::UpdateFlinch(float DeltaSeconds)
{
	if (!bFlinching)
	{
		return;
	}
	FlinchT += DeltaSeconds;
	if (FlinchT < FlinchLen)
	{
		return;
	}
	bFlinching = false;
	// The head snap is the FIRST half of the answer to a blow; the body reaction follows it.
	if (PendingSquirmKind >= 0)
	{
		const int32 Kind = PendingSquirmKind;
		PendingSquirmKind = -1;
		BeginSquirm(Kind);
	}
	else if (HangClip != nullptr)
	{
		Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Body->SetAnimation(HangClip);
		Body->PlayAnimation(HangClip, /*bLooping=*/true);
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
	if (Body == nullptr || !bHanging || bDead)
	{
		return;
	}
	// THE CHAINS GO THROUGH THE MARK POOL, not through components of this actor. Six links per wrist
	// plus a cuff: 14 stamps a frame for a full cell, well inside the chain slice of the pool.
	AHIGameMode* GM = Cast<AHIGameMode>(UGameplayStatics::GetGameMode(this));
	if (GM == nullptr)
	{
		return;
	}
	static const TCHAR* const Hands[2] = { TEXT("LeftHand"), TEXT("RightHand") };
	static const TCHAR* const Elbows[2] = { TEXT("LeftForeArm"), TEXT("RightForeArm") };
	const float CeilingZ = (FHISim::BasementZ + FHISim::BasementCeiling) * HI_METERS_TO_UU;
	const int32 Links = 6;
	const FLinearColor ChainColour(0.10f, 0.10f, 0.11f);
	const FLinearColor CuffColour(0.34f, 0.35f, 0.38f);

	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FVector Palm = Body->GetSocketLocation(Hands[Side]);
		const FVector Elbow = Body->GetSocketLocation(Elbows[Side]);
		const float ForeLen = FVector::Dist(Palm, Elbow);
		const FVector Wrist = (ForeLen > 1.0f) ? Palm + (Elbow - Palm) * 0.22f : Palm;
		const float CuffR = FMath::Clamp(0.55f * ForeLen, 0.12f * HI_METERS_TO_UU, 0.19f * HI_METERS_TO_UU);

		const FVector Anchor(Wrist.X, Wrist.Y, CeilingZ);
		const FVector Span = Wrist - Anchor;
		const float Len = Span.Size();
		if (FVector::Dist(Wrist, GetActorLocation()) > 2.5f * HI_METERS_TO_UU || Len < 2.0f)
		{
			continue;   // a socket this far out means no pose; no chain is better than a chain to nowhere
		}
		const FVector Dir = Span / Len;
		for (int32 L = 0; L < Links; ++L)
		{
			const float T0 = float(L) / float(Links);
			const float T1 = float(L + 1) / float(Links);
			// A small gap between links is what reads as a chain rather than a rod.
			const FVector A = Anchor + Dir * (Len * T0);
			const float LinkEnd = Len * T1 - Len / float(Links) * 0.18f;
			const FVector B = Anchor + Dir * LinkEnd;
			GM->DrawChainSegment(A, B, 0.022f, ChainColour);
		}
		// The cuff: a short, fat segment square across the wrist.
		// The cuff: a short, fat segment along the wrist, 3.5 cm deep and as wide as the wrist.
		GM->DrawChainSegment(Wrist - Dir * (0.0175f * HI_METERS_TO_UU),
			Wrist + Dir * (0.0175f * HI_METERS_TO_UU), CuffR / HI_METERS_TO_UU, CuffColour);
	}
}
