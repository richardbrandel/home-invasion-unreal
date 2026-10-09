#include "HIAudio.h"

#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "HISim.h"
#include "HIUnits.h"
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
	 * The one weapon-fire cue the engine's templates ship, plus a sine for barks.
	 *
	 * Copied into the project by tools/fetch-engine-assets.sh, at the package path the asset
	 * refers to ITSELF by -- which is under GrenadeLauncher even though what is being fired
	 * here is a pistol, because that is where the template keeps it.
	 */
	const TCHAR* PistolShotPath =
		TEXT("/Game/Weapons/GrenadeLauncher/Audio/FirstPersonTemplateWeaponFire02."
			 "FirstPersonTemplateWeaponFire02");
	const TCHAR* SinePath = TEXT("/Engine/EngineSounds/1kSineTonePing.1kSineTonePing");

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

USoundBase* FHIAudio::PistolShot()
{
	static USoundBase* Cached = nullptr;
	if (Cached == nullptr)
	{
		Cached = LoadObject<USoundBase>(nullptr, PistolShotPath);
	}
	return Cached;
}

USoundBase* FHIAudio::Gunshot()
{
	return PistolShot();
}

USoundBase* FHIAudio::SineTone()
{
	static USoundBase* Cached = nullptr;
	if (Cached == nullptr)
	{
		Cached = LoadObject<USoundBase>(nullptr, SinePath);
	}
	return Cached;
}

bool FHIAudio::bInsideHouse(const FVector& WorldLocation)
{
	const FVector2D Sim(WorldLocation.X / HI_METERS_TO_UU, WorldLocation.Y / HI_METERS_TO_UU);
	// A small outward margin: a shot fired in the doorway should ring, and a body standing in
	// the door frame is on the line rather than clearly on one side of it.
	return FHISim::House.ExpandBy(0.25f).IsInside(Sim);
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
	Play(World, Gunshot(), MuzzleWorld,
		bShotgun ? 0.95f : 0.70f,
		bShotgun ? 0.62f : 1.0f,
		/*bSpatialized=*/false);
}

void FHIAudio::IntruderGunshot(UWorld* World, const FVector2D& SimPos, float SimZ, bool bIndoorHint)
{
	const FVector Where(SimPos.X, SimPos.Y, SimZ + 1.30f);
	Play(World, Gunshot(), Where * HI_METERS_TO_UU, 0.85f, 1.05f, /*bSpatialized=*/true);
}

void FHIAudio::Impact(UWorld* World, const FVector& Where, bool bFlesh)
{
	// The gunshot again, pitched right down and turned right down: at 0.45x it is a dull
	// report rather than a crack, which is what a body being hit sounds like from across a
	// room. It is a stand-in for foley, and it is a much better stand-in than silence.
	Play(World, Gunshot(), Where,
		bFlesh ? 0.45f : 0.28f,
		bFlesh ? 0.42f : 0.60f,
		/*bSpatialized=*/true);
}

void FHIAudio::Bark(UWorld* World, const FVector& Where, float BasePitch)
{
	Play(World, SineTone(), Where, 0.30f, BasePitch, /*bSpatialized=*/true);
}
