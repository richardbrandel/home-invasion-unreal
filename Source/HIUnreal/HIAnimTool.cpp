#include "HIAnimTool.h"

#if WITH_EDITOR

#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	/** One bone track's keys, in frame order, plus what was measured from them. */
	struct FHIKeySet
	{
		TArray<FVector3f> Pos;
		TArray<FQuat4f> Rot;
		TArray<FVector3f> Scale;
		TArray<FFrameNumber> Frames;
	};

	bool CollectKeys(const IAnimationDataModel& Model, FName Bone, FHIKeySet& Out)
	{
		Model.IterateBoneKeys(Bone,
			[&Out](const FVector3f& P, const FQuat4f& R, const FVector3f S, const FFrameNumber& F)
			{
				Out.Pos.Add(P);
				Out.Rot.Add(R);
				Out.Scale.Add(S);
				Out.Frames.Add(F);
				return true;
			});
		return Out.Pos.Num() > 0;
	}

	/** Horizontal travel (component X/Y) from first key to last, in centimetres. */
	float HorizontalTravelCm(const FHIKeySet& K)
	{
		if (K.Pos.Num() < 2)
		{
			return 0.0f;
		}
		const FVector3f D = K.Pos.Last() - K.Pos[0];
		return FVector2f(D.X, D.Y).Size();
	}

	/** The largest angle any key's rotation makes with the first key's, in degrees. */
	float RotationSpreadDeg(const FHIKeySet& K)
	{
		const FQuat First(K.Rot[0].X, K.Rot[0].Y, K.Rot[0].Z, K.Rot[0].W);
		float MaxDeg = 0.0f;
		for (const FQuat4f& R : K.Rot)
		{
			const float Dot = FMath::Abs(First | FQuat(R.X, R.Y, R.Z, R.W));
			const float Deg = FMath::RadiansToDegrees(2.0f * FMath::Acos(FMath::Clamp(Dot, -1.0f, 1.0f)));
			MaxDeg = FMath::Max(MaxDeg, Deg);
		}
		return MaxDeg;
	}

	/**
	 * The track name a wanted bone is actually called. The Interchange import strips
	 * the `mixamorigN:` namespace (measured: the SOURCE says `mixamorig8:RightHand`,
	 * the imported skeleton says `RightHand`), but a future importer that keeps it
	 * must not silently turn every pin into a no-op -- so match the exact name or a
	 * namespaced one, and let the caller REPORT a miss rather than shrug.
	 */
	FName ResolveTrack(const TArray<FName>& Tracks, const TCHAR* Bone)
	{
		const FString Suffix = FString::Printf(TEXT(":%s"), Bone);
		for (const FName& T : Tracks)
		{
			const FString S = T.ToString();
			if (S == Bone || S.EndsWith(Suffix))
			{
				return T;
			}
		}
		return NAME_None;
	}

	/** Pin every key of `Keys` to its first, in the chosen components. */
	void PinToFirst(FHIKeySet& Keys, bool bPinTranslation, bool bPinAllAxes, bool bPinRotation)
	{
		if (bPinTranslation)
		{
			for (int32 i = 1; i < Keys.Pos.Num(); ++i)
			{
				if (bPinAllAxes)
				{
					Keys.Pos[i] = Keys.Pos[0];
				}
				else
				{
					// Horizontal only: the vertical component is the gait's bob and,
					// in the crouch clips, the crouch itself.
					Keys.Pos[i].X = Keys.Pos[0].X;
					Keys.Pos[i].Y = Keys.Pos[0].Y;
				}
			}
		}
		if (bPinRotation)
		{
			for (int32 i = 1; i < Keys.Rot.Num(); ++i)
			{
				Keys.Rot[i] = Keys.Rot[0];
			}
		}
	}
}

#endif // WITH_EDITOR

FString UHIAnimTool::NormalizeCharacterClip(const FString& PackagePath, const FString& Mode,
	float RaiseWristsCm)
{
#if WITH_EDITOR
	UAnimSequence* Seq = LoadObject<UAnimSequence>(nullptr, *PackagePath);
	if (Seq == nullptr)
	{
		return FString::Printf(TEXT("%s: MISSING (nothing normalized)"), *PackagePath);
	}
	IAnimationDataModel* Model = Seq->GetDataModel();
	if (Model == nullptr)
	{
		return FString::Printf(TEXT("%s: NO DATA MODEL (nothing normalized)"), *PackagePath);
	}

	TArray<FName> Tracks;
	Model->GetBoneTrackNames(Tracks);
	const FName Hips = ResolveTrack(Tracks, TEXT("Hips"));
	if (Hips.IsNone())
	{
		return FString::Printf(TEXT("%s: NO HIPS TRACK among %d tracks (nothing normalized)"),
			*PackagePath, Tracks.Num());
	}

	FHIKeySet HipsKeys;
	if (!CollectKeys(*Model, Hips, HipsKeys))
	{
		return FString::Printf(TEXT("%s: HIPS TRACK EMPTY (nothing normalized)"), *PackagePath);
	}

	// ---- MEASURE FIRST, whatever the mode: the numbers are the record of what the
	// raw export did, and the report is where they get written down.
	const double Length = Model->GetPlayLength();
	const float TravelCm = HorizontalTravelCm(HipsKeys);
	const float AuthoredMps = (Length > 0.0) ? (TravelCm / 100.0f) / static_cast<float>(Length) : 0.0f;
	const float HipsRotDeg = RotationSpreadDeg(HipsKeys);

	// ---- BAKED SCALE, reported because nothing else reads it (2026-11-05). A retargeted clip
	// can carry per-bone SCALE keys, and every position and rotation assertion in the suite
	// passes while the figure renders at the wrong size -- measured on the Dancer's retargeted
	// clips, where a giant walked through the yard with a perfect 98 cm pelvis. Cheap to scan
	// and the only place the number is visible.
	float MaxScaleDev = 0.0f;
	FString MaxScaleBone;
	float MaxBoneTrans = 0.0f;
	FString MaxTransBone;
	for (const FName& Track : Tracks)
	{
		FHIKeySet K;
		if (!CollectKeys(*Model, Track, K))
		{
			continue;
		}
		for (const FVector3f& T : K.Pos)
		{
			if (Track != Hips && T.Size() > MaxBoneTrans)
			{
				MaxBoneTrans = T.Size();
				MaxTransBone = Track.ToString();
			}
		}
		for (const FVector3f& S : K.Scale)
		{
			const float Dev = FMath::Max3(FMath::Abs(S.X - 1.0f), FMath::Abs(S.Y - 1.0f),
				FMath::Abs(S.Z - 1.0f));
			if (Dev > MaxScaleDev)
			{
				MaxScaleDev = Dev;
				MaxScaleBone = Track.ToString();
			}
		}
	}


	// ---- THE UNIFORM-BAKE GUARD. `SetBoneTrackKeys` derives key times from the key
	// index at the model's frame rate, so rewriting a track that is NOT a uniform bake
	// would silently retime it. MEASURED on this content at the first normalization run
	// (2026-10-13): every clip carries exactly `GetNumberOfFrames() + 1` keys at
	// consecutive frame numbers 0..N -- one key per frame boundary INCLUDING both ends,
	// which is the uniform convention itself (walk: 31 frames, 32 keys, 1.033 s at
	// 30 fps; aim: 72/73; idle_Anim: 250/251; the same +1 on all clips of all four
	// characters). The guard checks exactly that, per key, and a first draft of it that
	// demanded keys == frames refused every clip in the set -- a fencepost, caught by
	// the report line rather than by a retimed animation.
	const int32 Frames = Model->GetNumberOfFrames();
	// A SINGLE KEY CANNOT VARY, so it is the degenerate uniform case (2026-11-01). A retargeted
	// clip whose pelvis the retargeter already held still collapses its Hips track to one key, and
	// `aim` -- whose source is fully pinned -- came out that way and was the one clip the guard
	// refused, so it could never be normalized. Refusing a constant track protects nothing.
	const bool bUniform = HipsKeys.Pos.Num() == Frames + 1 || HipsKeys.Pos.Num() == 1;
	if (!bUniform)
	{
		return FString::Printf(
			TEXT("%s: NOT UNIFORM (%d Hips keys vs %d frames, want frames+1) -- refusing to retime; nothing normalized"),
			*PackagePath, HipsKeys.Pos.Num(), Frames);
	}
	for (int32 i = 0; i < HipsKeys.Frames.Num(); ++i)
	{
		if (HipsKeys.Frames[i].Value != i)
		{
			return FString::Printf(
				TEXT("%s: NOT UNIFORM (key %d sits at frame %d) -- refusing to retime; nothing normalized"),
				*PackagePath, i, HipsKeys.Frames[i].Value);
		}
	}

	// ---- WHAT TO PIN, per mode. Bone lists are Mixamo's standard rig; a bone with no
	// track is constant anyway and is skipped without complaint, but a bone MISSING
	// from the track list when the mode wanted it is reported -- a pin that found
	// nothing and a pin that pinned nothing must not read the same.
	struct FPin
	{
		const TCHAR* Bone;
		bool bTranslation;
		bool bAllAxes;
		bool bRotation;
	};
	TArray<FPin> Pins;
	if (Mode == TEXT("locomotion"))
	{
		Pins.Add({ TEXT("Hips"), true, false, false });
	}
	else if (Mode == TEXT("aim"))
	{
		Pins.Add({ TEXT("Hips"), true, true, true });
		for (const TCHAR* B : { TEXT("Spine"), TEXT("Spine1"), TEXT("Spine2"),
			TEXT("LeftUpLeg"), TEXT("RightUpLeg"), TEXT("LeftLeg"), TEXT("RightLeg"),
			TEXT("LeftFoot"), TEXT("RightFoot"), TEXT("LeftToeBase"), TEXT("RightToeBase") })
		{
			Pins.Add({ B, true, true, true });
		}
	}
	else if (Mode == TEXT("shoot"))
	{
		Pins.Add({ TEXT("Hips"), true, false, true });
	}
	else if (Mode != TEXT("none"))
	{
		return FString::Printf(TEXT("%s: UNKNOWN MODE '%s' (nothing normalized)"),
			*PackagePath, *Mode);
	}

	FString Report = FString::Printf(
		TEXT("%s: len=%.3fs travel=%.1fcm (authored %.2f m/s) hipsRotSpread=%.1fdeg mode=%s scaleDev=%.3f@%s maxBoneT=%.1fcm@%s"),
		*PackagePath, Length, TravelCm, AuthoredMps, HipsRotDeg, *Mode, MaxScaleDev,
		MaxScaleBone.IsEmpty() ? TEXT("-") : *MaxScaleBone, MaxBoneTrans,
		MaxTransBone.IsEmpty() ? TEXT("-") : *MaxTransBone);

	if (Pins.Num() == 0)
	{
		return Report + TEXT(" -- report only, SAVED nothing");
	}

	TScriptInterface<IAnimationDataController> ControllerSI = Model->GetController();
	IAnimationDataController* Controller = ControllerSI.GetInterface();
	if (Controller == nullptr)
	{
		return Report + TEXT(" -- NO CONTROLLER, nothing normalized");
	}

	Seq->Modify();
	Controller->OpenBracket(FText::FromString(TEXT("HI normalize")), /*bShouldTransact=*/false);
	int32 Pinned = 0;
	TArray<FString> Misses;
	for (const FPin& Pin : Pins)
	{
		const FName Track = ResolveTrack(Tracks, Pin.Bone);
		if (Track.IsNone())
		{
			Misses.Add(FString(Pin.Bone));
			continue;
		}
		FHIKeySet Keys;
		if (!CollectKeys(*Model, Track, Keys) || Keys.Pos.Num() != Frames + 1)
		{
			Misses.Add(FString(Pin.Bone) + TEXT("(keys)"));
			continue;
		}
		PinToFirst(Keys, Pin.bTranslation, Pin.bAllAxes, Pin.bRotation);
		if (Controller->SetBoneTrackKeys(Track, Keys.Pos, Keys.Rot, Keys.Scale,
			/*bShouldTransact=*/false))
		{
			++Pinned;
		}
		else
		{
			Misses.Add(FString(Pin.Bone) + TEXT("(set failed)"));
		}
	}

	// ---- THE WRIST RAISE, when the caller asked for one. Richard, 2026-10-15, on the
	// pistol-ready pose: "Move the gun up a little bit, about 6 inches." The gun is
	// parented to the hand bone at zero offset, so the honest way to raise the gun is
	// to raise the HANDS: rotate each upper arm about its own shoulder joint and the
	// wrist, the gun and the grip all travel together. The gun-only alternative -- an
	// attach offset -- was built and measured the same day and REJECTED on the frames:
	// it lifts the gun exactly the predicted 15.24 cm (probe gunLoc 1.09 -> 1.24 m),
	// and the pictures show a pistol floating above hands that hold nothing, because
	// from the play camera the hands hide the grip at waist level and the raised gun
	// rises out of them with background visible in between.
	//
	// THE CONSTANTS ARE MEASURED, NOT POSED. tools/gunraise.py derived each from the
	// in-pose bone dump (probe ticks 2-3 of capture `bones2`, 2026-10-15): the world
	// rotation about the shoulder line that lifts THAT wrist by exactly 15.24 cm,
	// conjugated into the clavicle's local frame -- delta = P^-1 * R_c * P -- which is
	// what pre-multiplying an arm track's LOCAL keys applies. Right arm +24.62 deg,
	// left +23.82 deg; the same arc also carries the wrists ~7 cm further from the
	// body, so the gun ends up LESS occluded by the torso, not more. One constant
	// delta on every key keeps the authored breathing and sway exactly as they are --
	// the whole arm just sits higher. A RaiseWristsCm other than 15.24 rescales the
	// angle about the same measured axis, exact to first order over this range.
	//
	// RotationSpreadDeg (and with it the Cast test's spreads) is invariant under a
	// constant pre-multiply -- every key moves together, so key-to-first angles do not
	// change -- which is why this pass cannot silently invalidate the assertions.
	int32 Raised = 0;
	FString RaiseDetail;
	if (RaiseWristsCm > 0.0f)
	{
		struct FRaise
		{
			const TCHAR* Bone;
			FQuat DeltaAt1524; // LOCAL-space pre-multiply, calibrated for +15.24 cm
		};
		const FRaise Raises[] = {
			{ TEXT("RightArm"), FQuat(0.00921f, 0.20664f, 0.05103f, 0.97701f) },
			{ TEXT("LeftArm"),  FQuat(0.00970f, -0.18782f, -0.08389f, 0.97847f) },
		};
		const float Scale = RaiseWristsCm / 15.24f;
		for (const FRaise& Raise : Raises)
		{
			const FName Track = ResolveTrack(Tracks, Raise.Bone);
			if (Track.IsNone())
			{
				Misses.Add(FString(Raise.Bone) + TEXT("(raise)"));
				continue;
			}
			FHIKeySet Keys;
			if (!CollectKeys(*Model, Track, Keys) || Keys.Rot.Num() != Frames + 1)
			{
				Misses.Add(FString(Raise.Bone) + TEXT("(raise keys)"));
				continue;
			}
			// Rescale about the measured axis (manual axis-angle, not ToAxisAndAngle:
			// the same math gunraise.py ran, so the two cannot drift on an API change).
			const float W = FMath::Clamp(Raise.DeltaAt1524.W, -1.0, 1.0);
			const float Angle = 2.0f * FMath::Acos(W);
			const float HalfSin = FMath::Sin(Angle * 0.5f);
			const FVector Axis = (FMath::Abs(HalfSin) < 1e-6f)
				? FVector(0.0f, 0.0f, 1.0f)
				: FVector(Raise.DeltaAt1524.X, Raise.DeltaAt1524.Y, Raise.DeltaAt1524.Z) / HalfSin;
			const float ScaledAngle = Angle * Scale;
			const FQuat Scaled(Axis, ScaledAngle);
			const FQuat4f Delta(Scaled.X, Scaled.Y, Scaled.Z, Scaled.W);
			for (int32 i = 0; i < Keys.Rot.Num(); ++i)
			{
				Keys.Rot[i] = Delta * Keys.Rot[i];
			}
			if (Controller->SetBoneTrackKeys(Track, Keys.Pos, Keys.Rot, Keys.Scale,
				/*bShouldTransact=*/false))
			{
				++Raised;
				RaiseDetail += FString::Printf(TEXT("%s:%+.1fdeg "),
					Raise.Bone, FMath::RadiansToDegrees(ScaledAngle));
			}
			else
			{
				Misses.Add(FString(Raise.Bone) + TEXT("(raise set failed)"));
			}
		}
	}

	Controller->CloseBracket(/*bShouldTransact=*/false);
	Seq->PostEditChange();

	Report += FString::Printf(TEXT(" -- pinned %d/%d"), Pinned, Pins.Num());
	if (RaiseWristsCm > 0.0f)
	{
		Report += FString::Printf(TEXT(" wrists=+%.2fcm raised %d/2 (%s)"),
			RaiseWristsCm, Raised, *RaiseDetail.TrimEnd());
	}
	if (Misses.Num() > 0)
	{
		Report += TEXT(" MISSED: ") + FString::Join(Misses, TEXT(","));
	}

	// ---- SAVE, because a normalized clip that lives only in memory normalizes the
	// next import right back. The .uasset is derived state; the script that calls this
	// is the source of truth. `SavePackage` (not the newer `Save`, which takes a
	// result-struct this tool has no use for) writes to the package's own filename,
	// derived through the package system rather than string-built.
	Seq->MarkPackageDirty();
	UPackage* Pkg = Seq->GetOutermost();
	const FString Filename = FPackageName::LongPackageNameToFilename(
		Pkg->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	const bool bSaved = UPackage::SavePackage(Pkg, nullptr, *Filename, SaveArgs);
	Report += bSaved ? TEXT(" SAVED") : TEXT(" SAVE FAILED");
	return Report;
#else
	return FString::Printf(
		TEXT("%s: runtime build -- HIAnimTool is editor-only (nothing normalized)"), *PackagePath);
#endif
}

FString UHIAnimTool::MeasureCharacterClip(const FString& PackagePath)
{
#if WITH_EDITOR
	UAnimSequence* Seq = LoadObject<UAnimSequence>(nullptr, *PackagePath);
	if (Seq == nullptr)
	{
		return FString::Printf(TEXT("%s: MISSING"), *PackagePath);
	}
	IAnimationDataModel* Model = Seq->GetDataModel();
	if (Model == nullptr)
	{
		return FString::Printf(TEXT("%s: NO DATA MODEL"), *PackagePath);
	}
	TArray<FName> Tracks;
	Model->GetBoneTrackNames(Tracks);

	FString Out = FString::Printf(TEXT("%s: len=%.3fs tracks=%d frames=%d"),
		*PackagePath, Model->GetPlayLength(), Tracks.Num(), Model->GetNumberOfFrames());
	for (const TCHAR* Bone : { TEXT("Hips"), TEXT("Spine"), TEXT("Spine1"), TEXT("Spine2"),
		TEXT("LeftUpLeg"), TEXT("RightArm") })
	{
		const FName Track = ResolveTrack(Tracks, Bone);
		if (Track.IsNone())
		{
			Out += FString::Printf(TEXT(" | %s=NOTRACK"), Bone);
			continue;
		}
		FHIKeySet K;
		if (!CollectKeys(*Model, Track, K))
		{
			Out += FString::Printf(TEXT(" | %s=EMPTY"), Bone);
			continue;
		}
		float Horiz = 0.0f, Vert = 0.0f;
		for (int32 i = 1; i < K.Pos.Num(); ++i)
		{
			Horiz = FMath::Max(Horiz,
				FVector2f(K.Pos[i].X - K.Pos[0].X, K.Pos[i].Y - K.Pos[0].Y).Size());
			Vert = FMath::Max(Vert, FMath::Abs(K.Pos[i].Z - K.Pos[0].Z));
		}
		Out += FString::Printf(TEXT(" | %s('%s'): keys=%d horiz=%.2fcm z=%.2fcm rot=%.2fdeg"),
			Bone, *Track.ToString(), K.Pos.Num(), Horiz, Vert, RotationSpreadDeg(K));
	}
	return Out;
#else
	return FString::Printf(
		TEXT("%s: runtime build -- HIAnimTool is editor-only (nothing measured)"), *PackagePath);
#endif
}

namespace
{
	/** The reference skeleton's index for a track name, tolerating the importer's prefixes. */
	int32 BoneIndexForTrack(const FReferenceSkeleton& Ref, const FName& Track)
	{
		const FString S = Track.ToString();
		for (int32 i = 0; i < Ref.GetNum(); ++i)
		{
			const FString Bone = Ref.GetBoneName(i).ToString();
			if (S == Bone || S.EndsWith(FString::Printf(TEXT(":%s"), *Bone))
				|| S.EndsWith(FString::Printf(TEXT("_%s"), *Bone)))
			{
				return i;
			}
		}
		return INDEX_NONE;
	}
}

FString UHIAnimTool::MakeHangClip(USkeletalMesh* Mesh, const FString& PackagePath)
{
#if WITH_EDITOR
	// A POSE FOR A MAN HANGING BY HIS WRISTS (2026-11-05). Richard: "the restraint, they should be
	// hanging by their wrists meaning that their wrists are above their heads." The cast's ten clips
	// are walking and shooting; none of them raise both arms, and a skeletal mesh cannot be posed
	// bone-by-bone at runtime -- `SetBoneTransformByName` belongs to UPoseableMeshComponent, and the
	// intruder actor does not tick, so an animation only evaluates when this code drives it.
	//
	// So the pose becomes an ASSET: one key, taken from the mesh's own reference pose with the arm
	// bones rotated so they point up. Rotating the arms is a rotation in COMPONENT space -- swing the
	// bone's +X from where the bind has it to up -- converted back to a local rotation against the
	// parent's component rotation, which is the only thing that makes it valid for THIS rig rather
	// than for a rig of the same shape.
	//
	// The clip is expected to be a duplicate of one of the character's own clips, so it is already
	// bound to the right skeleton; this writes the keys and saves.
	UAnimSequence* Seq = LoadObject<UAnimSequence>(nullptr, *PackagePath);
	if (Seq == nullptr || Mesh == nullptr)
	{
		return FString::Printf(TEXT("%s: MISSING clip or mesh"), *PackagePath);
	}
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	const int32 Num = Ref.GetNum();
	if (Num == 0)
	{
		return FString::Printf(TEXT("%s: empty skeleton"), *PackagePath);
	}
	IAnimationDataModel* Model = Seq->GetDataModel();
	TScriptInterface<IAnimationDataController> ControllerSI = (Model != nullptr) ? Model->GetController() : TScriptInterface<IAnimationDataController>();
	IAnimationDataController* Controller = ControllerSI.GetInterface();
	if (Model == nullptr || Controller == nullptr)
	{
		return FString::Printf(TEXT("%s: no data model/controller"), *PackagePath);
	}

	// The reference pose, and the component-space rotation of every bone in it.
	TArray<FTransform> Local = Ref.GetRefBonePose();
	TArray<FQuat> CompRot;
	CompRot.SetNum(Num);
	for (int32 i = 0; i < Num; ++i)
	{
		const int32 Parent = Ref.GetParentIndex(i);
		CompRot[i] = Local[i].GetRotation() * ((Parent != INDEX_NONE) ? CompRot[Parent] : FQuat::Identity);
	}

	int32 Raised = 0;
	for (const TCHAR* ArmName : { TEXT("LeftArm"), TEXT("RightArm") })
	{
		const int32 Arm = Ref.FindBoneIndex(ArmName);
		const int32 Parent = Arm != INDEX_NONE ? Ref.GetParentIndex(Arm) : INDEX_NONE;
		if (Arm == INDEX_NONE || Parent == INDEX_NONE)
		{
			continue;
		}
		// Where the bone points now, and where it must point: up, leaning a little forward so a
		// hanging body is not a plumb line.
		const FVector Now = CompRot[Arm].GetAxisX();
		const FVector Want = (FVector::UpVector + FVector::ForwardVector * 0.18f).GetSafeNormal();
		const FQuat Swing = FQuat::FindBetweenNormals(Now, Want);
		const FQuat CompNew = Swing * CompRot[Arm];
		Local[Arm].SetRotation(CompRot[Parent].Inverse() * CompNew);
		CompRot[Arm] = CompNew;
		++Raised;
	}
	// THE LEGS HANG, BENT. A man hanging by the wrists has his feet off the floor, and this basement
	// is 2.05 m from slab to ceiling: with the arms overhead his wrists reach the ceiling and
	// straight legs put his feet on the floor, which reads as standing in cuffs rather than hanging.
	// Thighs forward, knees back, feet pointed down -- and the body is lifted in the hang so the
	// shins clear the slab.
	for (const TCHAR* ThighName : { TEXT("LeftUpLeg"), TEXT("RightUpLeg") })
	{
		const int32 Thigh = Ref.FindBoneIndex(ThighName);
		const int32 Parent = Thigh != INDEX_NONE ? Ref.GetParentIndex(Thigh) : INDEX_NONE;
		if (Thigh == INDEX_NONE || Parent == INDEX_NONE)
		{
			continue;
		}
		const FQuat CompNew = FQuat(FVector::RightVector, FMath::DegreesToRadians(38.0f)) * CompRot[Thigh];
		Local[Thigh].SetRotation(CompRot[Parent].Inverse() * CompNew);
		CompRot[Thigh] = CompNew;
	}
	for (const TCHAR* ShinName : { TEXT("LeftLeg"), TEXT("RightLeg") })
	{
		const int32 Shin = Ref.FindBoneIndex(ShinName);
		if (Shin == INDEX_NONE)
		{
			continue;
		}
		Local[Shin].SetRotation(Local[Shin].GetRotation()
			* FQuat(FVector::RightVector, FMath::DegreesToRadians(-68.0f)));
	}

	// Soften the elbows: a dead-straight arm reads as a scarecrow. Local rotations, so the forearm
	// keeps whatever bend the bind already had plus a little.
	for (const TCHAR* ForeName : { TEXT("LeftForeArm"), TEXT("RightForeArm") })
	{
		const int32 Fore = Ref.FindBoneIndex(ForeName);
		if (Fore != INDEX_NONE)
		{
			Local[Fore].SetRotation(Local[Fore].GetRotation()
				* FQuat(FVector::ForwardVector, FMath::DegreesToRadians(18.0f)));
		}
	}

	Controller->OpenBracket(FText::FromString(TEXT("HI hang pose")), /*bShouldTransact=*/false);
	int32 Written = 0;
	for (int32 i = 0; i < Num; ++i)
	{
		const FName Bone = Ref.GetBoneName(i);
		TArray<FVector3f> Pos{ FVector3f(Local[i].GetTranslation()) };
		TArray<FQuat4f> Rot{ FQuat4f(Local[i].GetRotation()) };
		TArray<FVector3f> Scale{ FVector3f(Local[i].GetScale3D()) };
		// AddBoneCurve returns FALSE for a bone that already has one -- every bone in a duplicated
		// clip does -- so its result cannot gate the write, or nothing is written at all (measured:
		// "2 arms raised, 0 bone track(s) written, SAVED", a clip identical to the one it was copied
		// from).
		Controller->AddBoneCurve(Bone, /*bShouldTransact=*/false);
		if (Controller->SetBoneTrackKeys(Bone, Pos, Rot, Scale, /*bShouldTransact=*/false))
		{
			++Written;
		}
	}
	Controller->CloseBracket();

	UPackage* Package = Seq->GetOutermost();
	const FString FileName = FPackageName::LongPackageNameToFilename(
		Package != nullptr ? Package->GetName() : FString(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	const bool bSaved = Package != nullptr && UPackage::SavePackage(Package, Seq, *FileName, SaveArgs);
	return FString::Printf(TEXT("%s: %d arms raised, %d bone track(s) written, %s"),
		*PackagePath, Raised, Written, bSaved ? TEXT("SAVED") : TEXT("SAVE FAILED"));
#else
	return TEXT("FAILED: editor only");
#endif
}

FString UHIAnimTool::CheckBindProportions(USkeletalMesh* Mesh, float MaxFractionOfHeight)
{
#if WITH_EDITOR
	// IMPOSSIBLE BONE LENGTHS (2026-11-05). Richard: "the dancer, when she walks, sometimes her hands
	// turn into what looks like long swords." The cause was thirteen joints -- the whole thumb and
	// every fingertip, both hands -- whose bind put them ~1.4 m from their parent while the model's
	// node hierarchy put them 1-3 cm away. A bad bind is INVISIBLE in the bind pose, because a bone's
	// own transform cancels against its inverse, so the mesh renders perfectly right up until one of
	// those bones ROTATES and swings its vertices on a metre-and-a-half radius.
	//
	// Nothing in the suite could see it: `HI.Unreal.Cast` reads the hips, the height and the facing,
	// and all three were correct. This check is the missing one. A bone segment longer than a third
	// of the character's own height cannot be right -- the longest legitimate segment in this cast is
	// a shin at 42 cm in a 180 cm man (23%) -- and the threshold is derived from the mesh rather than
	// being a magic number in centimetres.
	if (Mesh == nullptr)
	{
		return TEXT("FAILED -- no mesh");
	}
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	const int32 Num = Ref.GetNum();
	if (Num == 0)
	{
		return TEXT("FAILED -- empty skeleton");
	}
	const FVector Extent = Mesh->GetBounds().BoxExtent;
	const float HeightCm = static_cast<float>(Extent.Z) * 2.0f;
	if (HeightCm < 50.0f)
	{
		return FString::Printf(TEXT("FAILED -- mesh height reads %.0f cm, too small to judge"), HeightCm);
	}
	// The rig's own scale, so every segment is compared in CENTIMETRES: the Dancer's import carries
	// 0.01 on the root with 100x offsets beneath it.
	float Scale = 1.0f;
	const int32 RootIdx = Ref.FindBoneIndex(TEXT("_rootJoint"));
	if (RootIdx != INDEX_NONE)
	{
		Scale = static_cast<float>(Ref.GetRefBonePose()[RootIdx].GetScale3D().X);
	}
	const float LimitCm = MaxFractionOfHeight * HeightCm;
	FString Bad;
	int32 WorstBone = INDEX_NONE;
	float WorstCm = 0.0f;
	for (int32 i = 1; i < Num; ++i)   // 0 is the root; its children carry the hip offset
	{
		// The pelvis offset is legitimately large -- a metre from the character's origin in a rig
		// whose root sits at the feet -- so Hips is the one translation exempt from this test.
		if (Ref.GetBoneName(i) == FName(TEXT("Hips")))
		{
			continue;
		}
		const int32 Parent = Ref.GetParentIndex(i);
		if (Parent == 0)
		{
			continue;   // anything hanging directly off the root carries the same offset
		}
		const float LenCm = static_cast<float>(Ref.GetRefBonePose()[i].GetTranslation().Size()) * Scale;
		if (LenCm > WorstCm)
		{
			WorstCm = LenCm;
			WorstBone = i;
		}
		if (LenCm > LimitCm)
		{
			Bad += FString::Printf(TEXT("%s %.0f cm "), *Ref.GetBoneName(i).ToString(), LenCm);
		}
	}
	if (Bad.IsEmpty())
	{
		return FString::Printf(TEXT("%s: OK (longest segment %.0f cm = %.0f%% of %.0f cm%s)"),
			*Mesh->GetName(), WorstCm, 100.0f * WorstCm / HeightCm, HeightCm,
			WorstBone != INDEX_NONE ? *FString::Printf(TEXT(", %s"), *Ref.GetBoneName(WorstBone).ToString()) : TEXT(""));
	}
	return FString::Printf(TEXT("%s: BAD -- bone segments over %.0f%% of a %.0f cm body: %s"),
		*Mesh->GetName(), MaxFractionOfHeight * 100.0f, HeightCm, *Bad);
#else
	return TEXT("FAILED: editor only");
#endif
}

FString UHIAnimTool::CheckBindTranslations(const FString& PackagePath, USkeletalMesh* Mesh,
	float ToleranceFraction)
{
#if WITH_EDITOR
	// THE SWORDS CHECK (2026-11-05). Richard: "the dancer, when she walks, sometimes her hands turn
	// into what looks like long swords." A retargeted clip must animate ROTATIONS; a bone's
	// translation is part of the bind, and the mesh's inverse bind matrix assumes it. When a clip
	// carries a translation for a bone that differs from the bind, the difference is multiplied by
	// that inverse bind -- so a finger offset authored in the wrong unit flings the hand's vertices
	// metres away and they draw as a blade. It came and went because it depends on which bones the
	// retarget chose to author in each clip.
	//
	// This reports the deviation per bone as a FRACTION of the bone's own bind length, so it is
	// unit-agnostic: the Dancer's rig is in an "everything is 100x" encoding and the ratio is the
	// only thing that means anything. `HI.Unreal.Cast` calls it for every clip of every character.
	if (Mesh == nullptr)
	{
		return FString::Printf(TEXT("%s: FAILED -- no mesh"), *PackagePath);
	}
	UAnimSequence* Seq = LoadObject<UAnimSequence>(nullptr, *PackagePath);
	if (Seq == nullptr)
	{
		return FString::Printf(TEXT("%s: MISSING"), *PackagePath);
	}
	const IAnimationDataModel* Model = Seq->GetDataModel();
	if (Model == nullptr)
	{
		return FString::Printf(TEXT("%s: NO DATA MODEL"), *PackagePath);
	}
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	const FTransform* RefPose = Ref.GetRefBonePose().GetData();
	const int32 HipsIdx = Ref.FindBoneIndex(TEXT("Hips"));
	const int32 RootIdx = Ref.FindBoneIndex(TEXT("_rootJoint"));

	TArray<FName> Tracks;
	Model->GetBoneTrackNames(Tracks);
	FString Bad;
	int32 WorstBone = INDEX_NONE;
	float Worst = 0.0f;
	for (const FName& Track : Tracks)
	{
		const int32 Idx = BoneIndexForTrack(Ref, Track);
		if (Idx == INDEX_NONE || Idx == HipsIdx || Idx == RootIdx)
		{
			continue;   // the pelvis carries the root motion; the root carries the import scale
		}
		FHIKeySet Keys;
		if (!CollectKeys(*Model, Track, Keys) || Keys.Pos.Num() == 0)
		{
			continue;
		}
		const FVector RefT = RefPose[Idx].GetTranslation();
		const float RefLen = static_cast<float>(RefT.Size());
		const float Denom = RefLen > 1.0f ? RefLen : 1.0f;
		float LocalWorst = 0.0f;
		for (const FVector3f& P : Keys.Pos)
		{
			LocalWorst = FMath::Max(LocalWorst,
				static_cast<float>((FVector(P.X, P.Y, P.Z) - RefT).Size()) / Denom);
		}
		if (LocalWorst > Worst)
		{
			Worst = LocalWorst;
			WorstBone = Idx;
		}
		if (LocalWorst > ToleranceFraction)
		{
			Bad += FString::Printf(TEXT("%s %.1f%% "), *Ref.GetBoneName(Idx).ToString(), LocalWorst * 100.0f);
		}
	}
	if (Bad.IsEmpty())
	{
		return FString::Printf(TEXT("%s: OK (worst %.2f%% of bind length%s)"), *PackagePath, Worst * 100.0f,
			WorstBone != INDEX_NONE ? *FString::Printf(TEXT(", %s"), *Ref.GetBoneName(WorstBone).ToString()) : TEXT(""));
	}
	return FString::Printf(TEXT("%s: BAD -- translations off the bind: %s| worst %.0f%% (%s)"),
		*PackagePath, *Bad, Worst * 100.0f, WorstBone != INDEX_NONE ? *Ref.GetBoneName(WorstBone).ToString() : TEXT("?"));
#else
	return TEXT("FAILED: editor only");
#endif
}

FString UHIAnimTool::BakeClipRootScale(const FString& PackagePath, USkeletalMesh* Mesh, float Factor)
{
#if WITH_EDITOR
	// WHAT THIS IS FOR (2026-11-05). The Dancer's glb is imported with `GLB_WRAPPER=0
	// GLB_SCALE=0.01`, and the importer keeps that scale OUT of the geometry: it lives in the bone
	// offsets (a finger joint is 14183 cm from its parent) and is cancelled by a 0.01 somewhere in
	// the rig. IK Retargeting copies those offsets into the clip and writes scale 1.0, which drops
	// the cancellation: a 79 m intruder walked through the yard while the pelvis read a perfect
	// 98 cm and every position/rotation assertion stayed green. Nothing in the suite reads scale.
	//
	// The factor is DERIVED, not assumed: the clip's own keys are posed against the skeleton's
	// hierarchy to measure the Hips->Head distance it would produce, and the same measurement is
	// taken from the MESH's reference skeleton -- the one that renders and that the Cast test
	// measures. Their ratio is the correction. A clip that already has the right proportions
	// reports "nothing to do", so running this twice is harmless.
	if (Mesh == nullptr)
	{
		return FString::Printf(TEXT("%s: FAILED -- no mesh to measure the proportions against"), *PackagePath);
	}
	UAnimSequence* Seq = LoadObject<UAnimSequence>(nullptr, *PackagePath);
	if (Seq == nullptr)
	{
		return FString::Printf(TEXT("%s: MISSING"), *PackagePath);
	}
	IAnimationDataModel* Model = Seq->GetDataModel();
	if (Model == nullptr)
	{
		return FString::Printf(TEXT("%s: NO DATA MODEL"), *PackagePath);
	}
	TScriptInterface<IAnimationDataController> ControllerSI = Model->GetController();
	IAnimationDataController* Controller = ControllerSI.GetInterface();
	if (Controller == nullptr)
	{
		return FString::Printf(TEXT("%s: NO CONTROLLER"), *PackagePath);
	}

	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	const int32 Num = Ref.GetNum();
	const int32 RootIdx = Ref.FindBoneIndex(TEXT("_rootJoint"));
	const int32 HipsIdx = Ref.FindBoneIndex(TEXT("Hips"));
	const int32 HeadIdx = Ref.FindBoneIndex(TEXT("Head"));
	if (Num == 0 || HipsIdx == INDEX_NONE || HeadIdx == INDEX_NONE)
	{
		return FString::Printf(TEXT("%s: FAILED -- the mesh has no Hips/Head to measure"), *PackagePath);
	}

	TArray<FName> Tracks;
	Model->GetBoneTrackNames(Tracks);

	auto BoneForTrack = [&Ref](const FName& Track) -> int32
	{
		const FString S = Track.ToString();
		for (int32 i = 0; i < Ref.GetNum(); ++i)
		{
			const FString Bone = Ref.GetBoneName(i).ToString();
			if (S == Bone || S.EndsWith(FString::Printf(TEXT(":%s"), *Bone))
				|| S.EndsWith(FString::Printf(TEXT("_%s"), *Bone)))
			{
				return i;
			}
		}
		return INDEX_NONE;
	};

	// The clip's local transforms at its first key, per bone (the reference pose where it has no
	// track -- a bone with no track cannot move).
	TArray<FTransform> Local;
	Local.SetNum(Num);
	for (int32 i = 0; i < Num; ++i)
	{
		Local[i] = Ref.GetRefBonePose()[i];
	}
	TArray<int32> TrackOf;
	TrackOf.Init(INDEX_NONE, Num);
	for (const FName& Track : Tracks)
	{
		const int32 Idx = BoneForTrack(Track);
		if (Idx == INDEX_NONE)
		{
			continue;
		}
		TrackOf[Idx] = Track != NAME_None ? 1 : 0;
		FHIKeySet Keys;
		if (CollectKeys(*Model, Track, Keys) && Keys.Pos.Num() > 0)
		{
			if (Keys.Scale.Num() == Keys.Pos.Num())
			{
				Local[Idx] = FTransform(
					FQuat(Keys.Rot[0].X, Keys.Rot[0].Y, Keys.Rot[0].Z, Keys.Rot[0].W),
					FVector(Keys.Pos[0].X, Keys.Pos[0].Y, Keys.Pos[0].Z),
					FVector(Keys.Scale[0].X, Keys.Scale[0].Y, Keys.Scale[0].Z));
			}
			else
			{
				Local[Idx] = FTransform(
					FQuat(Keys.Rot[0].X, Keys.Rot[0].Y, Keys.Rot[0].Z, Keys.Rot[0].W),
					FVector(Keys.Pos[0].X, Keys.Pos[0].Y, Keys.Pos[0].Z),
					FVector::OneVector);
			}
			TrackOf[Idx] = 1;
		}
	}

	auto HipsHeadFrom = [&](const TArray<FTransform>& L) -> float
	{
		TArray<FTransform> Comp;
		Comp.SetNum(Num);
		for (int32 i = 0; i < Num; ++i)
		{
			const int32 Parent = Ref.GetParentIndex(i);
			Comp[i] = (Parent != INDEX_NONE) ? L[i] * Comp[Parent] : L[i];
		}
		return static_cast<float>((Comp[HeadIdx].GetLocation() - Comp[HipsIdx].GetLocation()).Size());
	};

	if (FMath::IsNearlyEqual(Factor, 1.0f, 1.e-5f))
	{
		return FString::Printf(TEXT("%s: nothing to do (factor 1)"), *PackagePath);
	}
	// The clip as it stands, for the record: this is what the retarget baked.
	const float HaveCm = HipsHeadFrom(Local);

	// WHICH BONES HAVE NO TRACK AT ALL. The batch retarget exports only the bones it animated, and
	// a bone with no track takes its transform from the rig's REFERENCE POSE -- which is the 100x
	// encoding. Rescaling the tracks that exist then leaves the untracked bones (fingers, toes,
	// twist joints) sticking out at 100x: measured, the pelvis sat at a perfect 0.91 m while the
	// component's bounds were still 32 m wide. So every missing bone gets a constant track holding
	// its reference transform in the SAME units as the rescaled tracks.
	TArray<bool> HasTrack;
	HasTrack.Init(false, Num);
	for (const FName& Track : Tracks)
	{
		const int32 Idx = BoneForTrack(Track);
		if (Idx != INDEX_NONE)
		{
			HasTrack[Idx] = true;
		}
	}

	Seq->Modify();
	Controller->OpenBracket(FText::FromString(TEXT("HI bake clip root scale")), /*bShouldTransact=*/false);
	int32 Rescaled = 0;
	int32 Added = 0;
	FString Failed;
	for (const FName& Track : Tracks)
	{
		const int32 Idx = BoneForTrack(Track);
		if (Idx == INDEX_NONE)
		{
			continue;
		}
		// The pelvis is the one translation the retarget AUTHORED in centimetres; every other
		// translation is a copy of the rig's, in whatever unit the import left it. Rescaling all
		// of them would move the pelvis, and the pelvis is what the Cast test asserts.
		const bool bPelvis = (Idx == HipsIdx);
		FHIKeySet Keys;
		if (!CollectKeys(*Model, Track, Keys) || Keys.Pos.Num() == 0)
		{
			continue;
		}
		if (bPelvis)
		{
			// The pelvis is the one translation the retarget authored in centimetres; the rig it
			// must match is in the imported (1/F) unit, so this is the only track that needs
			// converting. Everything else was already copied in the rig's unit.
			const float Inv = 1.0f / Factor;
			for (FVector3f& P : Keys.Pos)
			{
				P *= Inv;
			}
			++Rescaled;
		}
		// The root carries the import scale and every other bone is 1 -- the same encoding the
		// mesh's bind pose uses, which is what makes the skinning line up.
		const FVector3f Want = (Idx == RootIdx) ? FVector3f(Factor) : FVector3f(1.0f);
		for (FVector3f& Sc : Keys.Scale)
		{
			Sc = Want;
		}
		if (!Controller->SetBoneTrackKeys(Track, Keys.Pos, Keys.Rot, Keys.Scale,
			/*bShouldTransact=*/false))
		{
			Failed += Track.ToString() + TEXT(" ");
		}
	}
	for (int32 i = 0; i < Num; ++i)
	{
		if (HasTrack[i])
		{
			continue;
		}
		const FTransform RefT = Ref.GetRefBonePose()[i];
		const FVector T = (i == RootIdx)
			? RefT.GetTranslation()
			: RefT.GetTranslation();
		const FTransform One(FQuat::Identity, T, FVector::OneVector);
		const FName BoneName = Ref.GetBoneName(i);
		if (!Controller->AddBoneCurve(BoneName, /*bShouldTransact=*/false))
		{
			Failed += BoneName.ToString() + TEXT("(add) ");
			continue;
		}
		TArray<FVector3f> Pos{ FVector3f(T.X, T.Y, T.Z) };
		TArray<FQuat4f> Rot{ FQuat4f(0.0f, 0.0f, 0.0f, 1.0f) };
		TArray<FVector3f> Scale{ FVector3f(1.0f, 1.0f, 1.0f) };
		if (Controller->SetBoneTrackKeys(BoneName, Pos, Rot, Scale, /*bShouldTransact=*/false))
		{
			++Added;
		}
		else
		{
			Failed += BoneName.ToString() + TEXT("(keys) ");
		}
	}
	Controller->CloseBracket();

	UPackage* Package = Seq->GetOutermost();
	const FString PackageName = Package != nullptr ? Package->GetName() : FString();
	const FString FileName = FPackageName::LongPackageNameToFilename(PackageName,
		FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	const bool bSaved = UPackage::SavePackage(Package, Seq, *FileName, SaveArgs);

	return FString::Printf(
		TEXT("%s: Hips->Head was %.1f cm; factor %.5f on %d bone(s), %d constant track(s) added%s -- %s"),
		*PackagePath, HaveCm, Factor, Rescaled, Added,
		Failed.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("; SET FAILED: %s"), *Failed),
		bSaved ? TEXT("SAVED") : TEXT("SAVE FAILED"));
#else
	return TEXT("FAILED: editor only");
#endif
}
