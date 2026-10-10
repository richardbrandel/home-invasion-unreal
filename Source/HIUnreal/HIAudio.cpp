#include "HIAudio.h"

#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "HISim.h"
#include "HIUnits.h"
#include "Engine/Attenuation.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundSubmix.h"

namespace
{
	/** The engine's own bus layout. Using it rather than assets is the whole trick. */
	const TCHAR* SfxClassPath = TEXT("/Engine/EngineSounds/SFX.SFX");
	const TCHAR* ReverbSubmixPath =
		TEXT("/Engine/EngineSounds/Submixes/MasterReverbSubmixDefault.MasterReverbSubmixDefault");

	/**
	 * Everything audible now comes from /Game/HIImported, put there by
	 * `tools/import-assets.py` from files that already existed in the Godot repository --
	 * plus nine voice lines rendered on the Mac by `tools/make-voices.sh`.
	 *
	 * The engine's template weapon cue used to be the gunshot, and it is gone. It was a
	 * grenade launcher's report standing in for a pistol, and it carried the attenuation that
	 * crashed the game the first time a person played it. See `FHIAudio::Attenuation`.
	 *
	 * The path prefixes are spelled out at each call site rather than held in a constant,
	 * because `FString::Printf` takes its format string through a COMPILE-TIME checker and a
	 * `const TCHAR*` variable is rejected -- measured, not assumed: it fails with
	 * "no matching function for call to 'Printf'" and an undeclared `UCFS_FChecker`.
	 */

	/**
	 * How far a cue carries, in centimetres. A house is 16 m across and the lawn runs 20 m
	 * past it, so a gunshot heard across the whole property is the point.
	 */
	constexpr float AttenuationInnerRadius = 400.0f;
	constexpr float AttenuationFalloff = 4000.0f;

	/** A voice line from /Game/HIImported/voice, or nullptr. Separate folder from foley. */
	USoundBase* LoadVoice(const TCHAR* Line)
	{
		static TMap<FString, USoundBase*> Cache;
		const FString Key(Line);
		if (USoundBase** Found = Cache.Find(Key))
		{
			return *Found;
		}
		const FString AssetPath =
			FString::Printf(TEXT("/Game/HIImported/voice/%s.%s"), Line, Line);
		USoundBase* Loaded = LoadObject<USoundBase>(nullptr, *AssetPath);
		if (Loaded == nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("HI: no voice line at %s -- run tools/import-assets.sh"),
				*AssetPath);
		}
		Cache.Add(Key, Loaded);
		return Loaded;
	}

	/**
	 * How much of each cue is sent to the reverb.
	 *
	 * INDOOR IS NOT SUBTLE and outdoor is not zero: a shot fired on the lawn still arrives at
	 * the house from outside, and cutting it to silence would make the approach inaudible --
	 * which is the one thing the player needs to hear.
	 */
	constexpr float IndoorSend = 1.0f;
	constexpr float OutdoorSend = 0.12f;
}

USoundClass* FHIAudio::SfxClass()
{
	static USoundClass* Cached = nullptr;
	if (Cached == nullptr)
	{
		Cached = LoadObject<USoundClass>(nullptr, SfxClassPath);
	}
	return Cached;
}

USoundSubmix* FHIAudio::ReverbSubmix()
{
	static USoundSubmix* Cached = nullptr;
	if (Cached == nullptr)
	{
		Cached = LoadObject<USoundSubmix>(nullptr, ReverbSubmixPath);
	}
	return Cached;
}

USoundBase* FHIAudio::Imported(const TCHAR* Name)
{
	// Cached per name, and a miss is cached as nullptr so a missing file is a single failed
	// lookup rather than one per shot.
	static TMap<FString, USoundBase*> Cache;
	const FString Key(Name);
	if (USoundBase** Found = Cache.Find(Key))
	{
		return *Found;
	}
	const FString AssetPath =
		FString::Printf(TEXT("/Game/HIImported/audio/%s.%s"), Name, Name);
	USoundBase* Loaded = LoadObject<USoundBase>(nullptr, *AssetPath);
	if (Loaded == nullptr)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("HI: no sound at %s -- run tools/import-assets.sh"), *AssetPath);
	}
	Cache.Add(Key, Loaded);
	return Loaded;
}

USoundAttenuation* FHIAudio::Attenuation()
{
	// Built once, at first use, because it needs the engine's UObject system up.
	static USoundAttenuation* Cached = nullptr;
	if (Cached != nullptr)
	{
		return Cached;
	}

	Cached = NewObject<USoundAttenuation>(GetTransientPackage(), TEXT("HIAttention"));
	if (Cached == nullptr)
	{
		return nullptr;
	}

	// These four lines are the crash fix. `AttenuationShape` must be one of Sphere, Capsule,
	// Box or Cone -- `FBaseAttenuationSettings::GetMaxDimension` asserts on anything else, and
	// an inherited shape from a cue whose companion assets were never copied is exactly that.
	// Sphere with a 4 m inner radius and a 40 m falloff is also just the right shape for a
	// gunshot in a house and its garden.
	Cached->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
	Cached->Attenuation.AttenuationShapeExtents = FVector(AttenuationInnerRadius, 0.0f, 0.0f);
	Cached->Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::Linear;
	Cached->Attenuation.FalloffDistance = AttenuationFalloff;
	Cached->Attenuation.dBAttenuationAtMax = -60.0f;
	return Cached;
}

bool FHIAudio::bInsideHouse(const FVector& WorldLocation)
{
	// Read from the sim's own house rectangle rather than from a collision query, so the
	// question "does this sound sound indoor" and the question "is this body inside" cannot
	// give different answers. The world position is in centimetres and the rectangle is in
	// metres, which is the one conversion this project ever needs.
	const FVector Metres = WorldLocation / HI_METERS_TO_UU;
	const FBox2D& H = FHISim::House;
	return Metres.X >= H.Min.X && Metres.X <= H.Max.X
		&& Metres.Y >= H.Min.Y && Metres.Y <= H.Max.Y;
}

UAudioComponent* FHIAudio::Play(UWorld* World, USoundBase* Sound, const FVector& Where,
	float Volume, float Pitch, bool bSpatialized)
{
	if (World == nullptr || Sound == nullptr)
	{
		return nullptr;
	}

	UAudioComponent* Comp = NewObject<UAudioComponent>(World);
	if (Comp == nullptr)
	{
		return nullptr;
	}
	Comp->bAutoDestroy = true;
	Comp->bStopWhenOwnerDestroyed = false;
	Comp->bAllowSpatialization = bSpatialized;
	// NEVER leave this to inherit. The cue's own attenuation is what crashed the game: see
	// the header. Assigned only when spatialised, because a non-spatialised cue does not
	// consult it and the homeowner's own gun must not be attenuated by his own ears.
	if (bSpatialized)
	{
		Comp->AttenuationSettings = Attenuation();
	}
	// The bus, which is item 20's whole substance: not Master.
	Comp->SoundClassOverride = SfxClass();
	Comp->SetSound(Sound);
	Comp->SetVolumeMultiplier(Volume);
	Comp->SetPitchMultiplier(Pitch);
	if (USoundSubmix* Reverb = ReverbSubmix())
	{
		Comp->SetSubmixSend(Reverb, bInsideHouse(Where) ? IndoorSend : OutdoorSend);
	}
	else
	{
		// Once, loudly. A missing submix makes every cue dry and there would otherwise be
		// nothing at all to distinguish "no reverb asset" from "the reverb send does nothing".
		static bool bWarned = false;
		if (!bWarned)
		{
			bWarned = true;
			UE_LOG(LogTemp, Warning,
				TEXT("HI: no reverb submix at %s -- every cue will be dry"), ReverbSubmixPath);
		}
	}
	Comp->SetWorldLocation(Where);
	Comp->RegisterComponentWithWorld(World);
	Comp->Play();
	return Comp;
}

void FHIAudio::PlayerGunshot(UWorld* World, const FVector& MuzzleWorld, bool bShotgun)
{
	// At the muzzle rather than centred, so a shot fired into a wall arrives from the wall's
	// direction -- but not attenuated by the listener's own position, because it is his own
	// gun and the listener IS the muzzle.
	USoundBase* Shot = Imported(bShotgun ? TEXT("gunshot_shotgun") : TEXT("gunshot_pistol"));
	Play(World, Shot, MuzzleWorld, 0.95f, 1.0f, /*bSpatialized=*/false);
}

void FHIAudio::IntruderGunshot(UWorld* World, const FVector2D& SimPos, float SimZ, bool bIndoorHint)
{
	const FVector Where(SimPos.X, SimPos.Y, SimZ + 1.30f);
	Play(World, Imported(TEXT("gunshot_pistol")), Where * HI_METERS_TO_UU, 0.85f, 1.0f,
		/*bSpatialized=*/true);
}

void FHIAudio::Impact(UWorld* World, const FVector& Where, bool bFlesh)
{
	// The Godot original's `impact.wav`, which is a short dull thud rather than the gunshot
	// pitched down -- the old stand-in, and audibly a placeholder once you have the real one.
	Play(World, Imported(TEXT("impact")), Where,
		bFlesh ? 0.70f : 0.45f,
		bFlesh ? 1.0f : 0.85f,
		/*bSpatialized=*/true);
}

void FHIAudio::Voice(UWorld* World, const FVector& Where, const TCHAR* Line, float Volume)
{
	// No pitch shift and no randomisation: a voice that changes pitch per event sounds like
	// a synthesiser, which is the thing this replaced. The variety has to come from the
	// lines, and there are nine of them.
	Play(World, LoadVoice(Line), Where, Volume, 1.0f, /*bSpatialized=*/true);
}

void FHIAudio::Siren(UWorld* World, const FVector& Where)
{
	// Not spatialised to the listener's ear but positioned where the police are, which is the
	// far end of the drive: the original plays it from outside, and a siren that appears to
	// come from inside your own kitchen reads as a bug.
	Play(World, Imported(TEXT("siren")), Where, 0.80f, 1.0f, /*bSpatialized=*/true);
}
