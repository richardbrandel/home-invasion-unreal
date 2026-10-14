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

FString UHIAnimTool::NormalizeCharacterClip(const FString& PackagePath, const FString& Mode)
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
	if (HipsKeys.Pos.Num() != Frames + 1)
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
		TEXT("%s: len=%.3fs travel=%.1fcm (authored %.2f m/s) hipsRotSpread=%.1fdeg mode=%s"),
		*PackagePath, Length, TravelCm, AuthoredMps, HipsRotDeg, *Mode);

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
	Controller->CloseBracket(/*bShouldTransact=*/false);
	Seq->PostEditChange();

	Report += FString::Printf(TEXT(" -- pinned %d/%d"), Pinned, Pins.Num());
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
