#include "HISim.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, HISim);

// Definitions for the non-constexpr statics.
const FBox2D FHISim::House(FVector2D(-8.0f, -19.0f), FVector2D(8.0f, -8.0f));
const FVector2D FHISim::DoorGap(-1.0f, 1.0f);
const FVector2D FHISim::HallGap(-2.0f, 2.0f);
const FBox2D FHISim::Bounds(FVector2D(-30.0f, -28.0f), FVector2D(30.0f, 24.0f));
const FBox2D FHISim::Van(FVector2D(-2.75f, 7.0f), FVector2D(2.75f, 9.4f));
const FVector2D FHISim::Drop(0.0f, 4.5f);

float FHISim::CarrySpeed(const FString& Label)
{
	if (Label == TEXT("TV"))     return 0.70f;
	if (Label == TEXT("Safe"))   return 0.50f;
	if (Label == TEXT("Laptop")) return 0.92f;
	return 1.0f;
}

int32 FHISim::ReserveFor(const FString& Weapon)
{
	if (Weapon == TEXT("pistol"))  return 36;
	if (Weapon == TEXT("shotgun")) return 18;
	return 0;
}

FHISim::FHIWeapon FHISim::Weapon(const FString& Key)
{
	// sim.gd WEAPONS. `mag` here is the magazine CAPACITY; the rounds currently in the
	// magazine are entity state (FHIPlayer::Mag), which is why `load_magazine` fills
	// against this number and why the two must not be confused.
	if (Key == TEXT("pistol"))
	{
		FHIWeapon W;
		W.Name = TEXT("Pistol");
		W.Damage = 26.0f;
		W.Cd = 0.24f;
		W.Mag = 12;
		W.Reload = 1.15f;
		W.Pellets = 1;
		W.Spread = 0.02f;   // radians
		return W;
	}
	if (Key == TEXT("shotgun"))
	{
		FHIWeapon W;
		W.Name = TEXT("Shotgun");
		W.Damage = 13.0f;
		W.Cd = 0.85f;
		W.Mag = 6;
		W.Reload = 1.60f;
		W.Pellets = 9;
		W.Spread = 0.15f;   // radians
		return W;
	}
	// Unknown key: Godot would raise on WEAPONS[w] and crash. Returning a zeroed weapon
	// keeps an unported weapon from taking the whole test run down with it.
	return FHIWeapon();
}

int32 FHISim::WeaponMag(const FString& Key)
{
	return Weapon(Key).Mag;
}

// ---------------------------------------------------------------------- entities

FHISim::FHIPlayer FHISim::CreatePlayer()
{
	FHIPlayer P;
	// SHE DOES NOT START IN THE DOORWAY. sim.gd -- and this port until 2026-10-15 -- spawned
	// her at (0, -10.6): front zone, dead-centre on the door axis. That is the far end of a
	// straight sight channel running from the intruders' Drop (0, 4.5) through the door gap
	// (x -1..1 at y=-8) and the hall gap (x -2..2 at y=-14), so a man who has not yet crossed
	// the driveway can put a round on her the instant his opening cooldown lapses -- the spawn
	// kill zone. Richard's ruling (2026-10-15): the opening is brutal because "the fender ...
	// is standing in the door way while the intruders are coming at her. I think she should be
	// more inside the building." So she now starts in the BACK zone, west of the hall, off
	// that axis and behind the InteriorZ divider. From (-3, -15.5) all three Drop lanes are
	// stopped by the south-west wall -- their sight lines cross y=-8 at x=-1.6 to -2.2, west
	// of the door gap -- so the crew must come inside and through the hall to reach her. The
	// bedroom rug underfoot is `flat` (never solid) and the bed is 2 m west, so the spot is
	// clear floor beside the bedroom nav node. She faces north-east (yaw +1.0), which looks
	// through the hall gap toward the front door rather than into the divider wall.
	P.Pos = FVector2D(-3.0f, -15.5f);
	P.Yaw = 1.0f;
	P.Pitch = 0.24f;
	P.Hp = ThiefMaxHp;
	P.bAlive = true;
	P.Weapon = TEXT("pistol");
	P.bCrouching = false;
	P.bSprinting = false;
	P.Vel = FVector2D::ZeroVector;
	// Godot starts with a part-full pistol magazine and the OTHER weapon's magazine at
	// full, which is what `mags` records. Both capacities come from WEAPONS.
	P.Mag = 12;
	P.Mags.Add(TEXT("pistol"), 12);
	P.Mags.Add(TEXT("shotgun"), 6);
	P.ShoveCd = 0.0f;
	P.Stagger = 0.0f;
	// RESERVE is a constant dictionary in Godot and `.duplicate()` is what stops one
	// player spending another's ammunition. The C++ equivalent is a fresh copy per player.
	P.Reserve.Add(TEXT("pistol"), ReserveFor(TEXT("pistol")));
	P.Reserve.Add(TEXT("shotgun"), ReserveFor(TEXT("shotgun")));
	P.Cd = 0.0f;
	P.Reloading = 0.0f;
	return P;
}

bool FHISim::LoadMagazine(FHIPlayer& Player)
{
	// The reserve is sorted by WEAPON, not by magazine: `mag` is the magazine currently
	// held and `reserve[weapon]` is the bag it is filled from. Nothing here touches the
	// other weapon's magazine, which is the point of `mags`.
	const FString W = Player.Weapon;
	const int32 Want = WeaponMag(W);
	const int32* Found = Player.Reserve.Find(W);
	const int32 Have = Found ? *Found : 0;
	if (Have <= 0 || Player.Mag >= Want)
	{
		return false;
	}
	const int32 Take = FMath::Min(Want - Player.Mag, Have);
	Player.Mag += Take;
	Player.Reserve.Add(W, Have - Take);
	// The weapon's own magazine record follows the held magazine, so switching away and
	// back does not conjure rounds.
	Player.Mags.Add(W, Player.Mag);
	return true;
}

bool FHISim::CanLoadMagazine(const FHIPlayer& Player)
{
	const FString W = Player.Weapon;
	const int32* Found = Player.Reserve.Find(W);
	return (Found ? *Found : 0) > 0 && Player.Mag < WeaponMag(W);
}

bool FHISim::Shove(FHIPlayer& Player, FHIThief& Thief, TArray<FHILootItem>& Loot, TArray<FHIEvent>& Events)
{
	if (!Thief.bAlive || Thief.bEscaped)
	{
		return false;
	}
	if (Player.ShoveCd > 0.0f)
	{
		return false;
	}
	const FVector2D To = Thief.Pos - Player.Pos;
	const float Dist = To.Size();
	if (Dist > ShoveRange || Dist < 0.001f)
	{
		return false;
	}
	// He has to be roughly in FRONT of you; you cannot shove a man behind your own back.
	// (Sin, Cos) and NOT (Cos, Sin): the same forward convention the movement code uses,
	// where yaw 0 faces +Z. The other way round puts the arc ninety degrees out and the
	// shove lands on people standing beside you.
	const FVector2D Dir = To / Dist;
	const FVector2D Facing(FMath::Sin(Player.Yaw), FMath::Cos(Player.Yaw));
	if (FVector2D::DotProduct(Facing, Dir) < FMath::Cos(ShoveArc))
	{
		return false;
	}

	Player.ShoveCd = ShoveCd;
	Thief.Stagger = FMath::Max(Thief.Stagger, ShoveStagger);
	Thief.Aim = 0.0f;
	Thief.Fear = FMath::Min(Thief.Fear + ShoveFear, 8.0f);
	Thief.Pos = ResolveCircle(Thief.Pos + Dir * ShovePush, ThiefRadius);
	Thief.Route.Empty();
	// He has to stoop for whatever he just dropped, or he simply picks it straight back up.
	Thief.Regrab = RegrabTime;

	// Read the label BEFORE the drop clears it, because the event reports what was lost.
	const FString Dropped = Thief.Carry;
	DropCarried(Thief, Loot, Events);
	FHIEvent Ev;
	Ev.Type = TEXT("shoved");
	// FHIEvent carries one string payload; Godot's "shoved" calls it `dropped` and its
	// "dropped" event calls the same value `label`. One field serves both.
	Ev.Label = Dropped;
	Events.Add(Ev);
	return true;
}

void FHISim::DropCarried(FHIThief& Thief, TArray<FHILootItem>& Loot, TArray<FHIEvent>& Events)
{
	const FString Dropped = Thief.Carry;
	if (Dropped.IsEmpty())
	{
		return;
	}
	for (FHILootItem& L : Loot)
	{
		if (L.Label == Dropped)
		{
			// Back into the world exactly where he stood, and now KNOWN -- he has seen it.
			L.bTaken = false;
			L.bDelivered = false;
			L.Pos = Thief.Pos;
			L.bKnown = true;
		}
	}
	Thief.Carry.Empty();
	Thief.Regrab = RegrabTime;
	FHIEvent Ev;
	Ev.Type = TEXT("dropped");
	Ev.Label = Dropped;
	Events.Add(Ev);
}

bool FHISim::ThiefShove(FHIThief& Thief, FHIPlayer& Player, TArray<FHIEvent>& Events)
{
	if (!Thief.bAlive || Thief.bEscaped)
	{
		return false;
	}
	// His hands are full, so he cannot grab you -- which is also when he is harmless.
	if (!Thief.Carry.IsEmpty())
	{
		return false;
	}
	if (Thief.ShoveCd > 0.0f)
	{
		return false;
	}
	const FVector2D To = Player.Pos - Thief.Pos;
	const float Dist = To.Size();
	if (Dist > ThiefShoveRange || Dist < 0.001f)
	{
		return false;
	}
	Thief.ShoveCd = ThiefShoveCd;
	Player.Stagger = FMath::Max(Player.Stagger, ThiefShoveStagger);
	Player.Pos = ResolveCircle(Player.Pos + To / Dist * ThiefShovePush, PlayerRadius);
	FHIEvent Ev;
	Ev.Type = TEXT("thiefShove");
	Events.Add(Ev);
	return true;
}

void FHISim::Frighten(FHIThief& Thief, const FVector2D& From, float Loudness)
{
	if (!Thief.bAlive || Thief.bEscaped)
	{
		return;
	}
	Thief.LastSeen = From;
	Thief.Fear = FMath::Min(Thief.Fear + FearPerShot * Loudness, 8.0f);
}

void FHISim::AlertThief(FHIThief& Thief, const FVector2D& From, float Loudness)
{
	Frighten(Thief, From, Loudness);
	if (!Thief.bAlive || Thief.bEscaped)
	{
		return;
	}
	// The first shot of the round starts the police clock. -1 means nobody has called.
	if (Thief.Alarm < 0.0f)
	{
		Thief.Alarm = 0.0f;
	}
}

bool FHISim::DamageThief(FHIThief& Thief, float Damage, TArray<FHIEvent>& Events)
{
	if (!Thief.bAlive)
	{
		return false;
	}
	Thief.Hp -= Damage;
	// A gunshot wound is a behavioural event, not just a number going down. Nothing used to
	// consult hp except the lethal check, so a man at 1 hp walked, aimed and fired exactly
	// like a man at 100 -- no flinch, no stagger, no reaction of any kind.
	Thief.Stagger = FMath::Max(Thief.Stagger, FMath::Clamp(Damage * 0.035f, 0.35f, 1.4f));
	// His sight picture is gone: he has to re-settle before he can shoot again.
	Thief.Aim = 0.0f;
	Thief.Hits += 1;
	// Being hit frightens him far more than being missed.
	Thief.Fear = FMath::Min(Thief.Fear + FearPerHit, 8.0f);

	FHIEvent Ev;
	if (Thief.Hp <= 0.0f)
	{
		Thief.Hp = 0.0f;
		Thief.bAlive = false;
		Ev.Type = TEXT("thiefDown");
		Events.Add(Ev);
		return true;
	}
	Ev.Type = TEXT("thiefHit");
	Events.Add(Ev);
	return false;
}

bool FHISim::DamagePlayer(FHIPlayer& Player, float Damage, TArray<FHIEvent>& Events)
{
	if (!Player.bAlive)
	{
		return false;
	}
	Player.Hp -= Damage;

	FHIEvent Ev;
	if (Player.Hp <= 0.0f)
	{
		Player.Hp = 0.0f;
		Player.bAlive = false;
		Ev.Type = TEXT("playerDown");
		Events.Add(Ev);
		return true;
	}
	Ev.Type = TEXT("playerHit");
	Events.Add(Ev);
	return false;
}

FHISim::FHIThief FHISim::CreateThief()
{
	// sim.gd create_thief(). Every default lives on FHIThief's member initialisers, which
	// mirror that literal key for key -- with ONE deliberate exception, recorded on the
	// constant itself: `Cd` opens at ThiefOpeningCd (4.0 s), not Godot's 1.2, because
	// Richard judged the original's opening seconds too brutal (2026-10-15). It does NOT
	// stagger the spawn point -- that is create_intruders()'s job, for more than one body.
	return FHIThief();
}

TArray<FHISim::FHIThief> FHISim::CreateIntruders(int32 Count)
{
	TArray<FHIThief> Out;
	const int32 N = FMath::Max(Count, 1);
	Out.Reserve(N);
	for (int32 Idx = 0; Idx < N; ++Idx)
	{
		FHIThief T = CreateThief();
		// The id is what lets the game tell the bodies apart; it rides on every event.
		T.Id = Idx;
		// Fan out along the driveway: -1, 0, +1, then wrap for a fourth.
		const float Lane = static_cast<float>(Idx) - static_cast<float>(N - 1) * 0.5f;
		const FVector2D Off(FMath::RoundToFloat(Lane) * (ThiefRadius * 2.2f),
			-FMath::Abs(Lane) * 0.9f);
		T.Pos = ClampToWorld(Drop + Off + FVector2D(0.0f, 0.5f), ThiefRadius);
		T.Prev = T.Pos;
		// The fan-out staggers their BODIES; this staggers their FIRST SHOTS, so a round
		// opens with one man firing, then another, instead of a three-gun volley inside
		// the same tenth of a second. See ThiefOpeningCd -- Richard's 2026-10-15 call.
		T.Cd = ThiefOpeningCd + static_cast<float>(Idx) * ThiefOpeningStagger;
		Out.Add(T);
	}
	return Out;
}

TArray<int32> FHISim::StepIntruders(TArray<FHIThief>& Intruders, FHIPlayer& Player,
	TArray<FHILootItem>& Loot, TArray<FHIEvent>& Events, float Dt)
{
	TArray<int32> Seen;
	// WHERE EVERY BODY STOOD AT THE START OF THE FRAME. Kept only for `Prev`, so the stuck
	// detector measures travel from the frame's start; the positions the step produces are
	// captured separately below.
	TArray<FVector2D> AtFrameStart;
	AtFrameStart.Reserve(Intruders.Num() + 1);
	AtFrameStart.Add(Player.Pos);
	for (const FHIThief& T : Intruders)
	{
		AtFrameStart.Add(T.Pos);
	}

	for (int32 Idx = 0; Idx < Intruders.Num(); ++Idx)
	{
		FHIThief& T = Intruders[Idx];
		const int32 Mark = Events.Num();
		if (StepThief(T, Player, Loot, Events, Dt, false))
		{
			Seen.Add(Idx);
		}
		// Tag at the producer, which is the only place the identity is known. Inferring it
		// afterwards does not work: a `grabbed` carries a loot label, not a body.
		for (int32 EvIdx = Mark; EvIdx < Events.Num(); ++EvIdx)
		{
			FHIEvent& Ev = Events[EvIdx];
			// `sirens` and `allStolen` are deliberately global -- the police are not a
			// thief, and "everything is gone" is not attributable to one of them either.
			if (Ev.Id == INDEX_NONE && Ev.Type != TEXT("sirens") && Ev.Type != TEXT("allStolen"))
			{
				Ev.Id = T.Id;
			}
		}
	}

	// THE SEPARATION RUNS ON THE POST-STEP POSITIONS, and this is the single most important
	// line in the port.
	//
	// It used to run on the frame-START positions and write the result straight back, which
	// discarded every body's movement on every frame: `separate_all` of an already-separated
	// set is that same set, so each intruder was teleported back to where he began. The
	// intruders therefore NEVER MOVED -- measured, one intruder and three alike, after 180
	// simulated seconds: `pos=(0.00, 5.00)`, which is the spawn point exactly, with a valid
	// four-node route and a target room.
	//
	// It survived every test this project had, because they all exercise `StepThief` alone:
	// `HI.Sim.ThiefAI` clears three valuables in 137 s that way. Nothing ran a heist through
	// the group path, which is the only path the game itself uses.
	//
	// The Godot original has the same ordering, and the same probe run against it reports
	// the same thing: solo travel 2.1 m, group travel **0.000 m**. So this is not a porting
	// slip; it is a bug inherited with the algorithm, and it is why the port keeps `Prev` in
	// a separate list rather than reusing one array for both jobs.
	TArray<FVector2D> Stepped;
	Stepped.Reserve(Intruders.Num() + 1);
	Stepped.Add(Player.Pos);
	for (const FHIThief& T : Intruders)
	{
		Stepped.Add(T.Pos);
	}

	TArray<float> Radii;
	Radii.Reserve(Intruders.Num() + 1);
	Radii.Add(PlayerRadius);
	for (int32 Idx = 0; Idx < Intruders.Num(); ++Idx)
	{
		Radii.Add(ThiefRadius);
	}

	const TArray<FVector2D> After = SeparateAll(Stepped, Radii);
	Player.Pos = After[0];
	for (int32 Idx = 0; Idx < Intruders.Num(); ++Idx)
	{
		FHIThief& T = Intruders[Idx];
		T.Pos = After[Idx + 1];
		// `prev` is re-anchored on the FRAME-START position, not the resolved one, so the
		// stuck detector measures the whole frame's travel -- the step plus whatever the
		// body-push added. `StepThief` already wrote exactly this value; assigning it again
		// from the list is what makes that explicit rather than incidental.
		T.Prev = AtFrameStart[Idx + 1];
	}
	return Seen;
}

FVector2D FHISim::MoveToward(const FVector2D& Current, const FVector2D& Target, float MaxDelta)
{
	// Godot's Vector2.move_toward. A vector overload rather than a componentwise one,
	// because the "close enough, snap to target" case differs: moving each axis
	// independently would slide along one while stalling on the other, and a body arriving
	// at a wall at 45 degrees would creep into it instead of stopping.
	const FVector2D Delta = Target - Current;
	const float Len = Delta.Size();
	if (Len <= MaxDelta || Len == 0.0f)
	{
		return Target;
	}
	return Current + Delta / Len * MaxDelta;
}

FHISim::FHIRound FHISim::FHIRound::Start(int32 IntruderCount)
{
	FHIRound R;
	R.Player = CreatePlayer();
	R.Intruders = CreateIntruders(IntruderCount);
	R.Loot = CreateLoot();
	R.State = EHIRoundState::Play;
	R.ThievesDown = 0;

	// game.gd's `reset()` starts the VIEW looking 0.03 rad down, not the 0.24 that
	// `create_player()` puts in the player dictionary -- and in Godot those are two different
	// variables, because the camera reads the game layer's own `pitch`, not `player.pitch`.
	// The player-dictionary value is vestigial there.
	//
	// The port merged them, because the sim now owns the view (it is what Shove's front arc
	// and the movement basis are computed from). So the reset value has to be applied
	// explicitly, or every round starts with the camera pitched thirteen degrees down at the
	// floor. That is not a cosmetic difference: the first captured frame of this port showed
	// the top of the homeowner's head and very little else, and the cause was this one
	// number, not the camera distance.
	R.Player.Pitch = 0.03f;
	return R;
}

FHISim::FHIThief* FHISim::FHIRound::NearestIntruder()
{
	FHIThief* Best = nullptr;
	float BestDist = TNumericLimits<float>::Max();
	for (FHIThief& T : Intruders)
	{
		if (!T.bAlive)
		{
			continue;
		}
		const float Dist = FVector2D::Distance(Player.Pos, T.Pos);
		if (Dist < BestDist)
		{
			BestDist = Dist;
			Best = &T;
		}
	}
	return Best;
}

void FHISim::FHIRound::SwitchWeapon(const FString& Key)
{
	// An unknown key must not be able to strip him of a weapon: `Weapon(Key)` returns a
	// zeroed spec for anything unrecognised, so switching to a typo would leave him holding
	// nothing that can fire. Refuse it rather than discover that at the worst moment.
	if (Key.IsEmpty() || Key == Player.Weapon || Weapon(Key).Mag <= 0)
	{
		return;
	}
	// Bank the magazine he is holding against the weapon he is holding it for. Without this
	// the new weapon either inherits his current magazine or starts empty, and neither is
	// what `mags` is for.
	Player.Mags.Add(Player.Weapon, Player.Mag);
	Player.Weapon = Key;
	const int32* Banked = Player.Mags.Find(Key);
	Player.Mag = Banked != nullptr ? *Banked : 0;
	// Canceled, not carried: a man who swaps mid-reload starts the new weapon's reload from
	// scratch. game.gd zeroes it here for the same reason.
	Player.Reloading = 0.0f;
}

bool FHISim::FHIRound::Shout()
{
	if (Player.ShoutCd > 0.0f || !Player.bAlive)
	{
		return false;
	}
	Player.ShoutCd = ShoutCd;
	bool bHeard = false;
	for (FHIThief& T : Intruders)
	{
		if (!T.bAlive || T.bEscaped)
		{
			continue;
		}
		// Frighten, NOT AlertThief: a man shouting in his own hall must not summon the
		// police, or the shout and the telephone would be the same verb.
		Frighten(T, Player.Pos, ShoutLoudness);
		bHeard = true;
	}
	if (bHeard)
	{
		FHIEvent Ev;
		Ev.Type = TEXT("shouted");
		Events.Add(Ev);
	}
	return bHeard;
}

bool FHISim::FHIRound::CallPolice()
{
	if (bPoliceCalled)
	{
		return false;
	}
	bPoliceCalled = true;
	for (FHIThief& T : Intruders)
	{
		if (T.bAlive && !T.bEscaped && T.Alarm < 0.0f)
		{
			T.Alarm = 0.0f;
		}
	}
	FHIEvent Ev;
	Ev.Type = TEXT("calledPolice");
	Events.Add(Ev);
	return true;
}

bool FHISim::FHIRound::TryShove()
{
	FHIThief* Target = NearestIntruder();
	if (Target == nullptr)
	{
		return false;
	}
	return Shove(Player, *Target, Loot, Events);
}

bool FHISim::FHIRound::AnyIntruderStanding() const
{
	for (const FHIThief& T : Intruders)
	{
		if (T.bAlive)
		{
			return true;
		}
	}
	return false;
}

FHISim::FHIThief* FHISim::FHIRound::FindIntruder(int32 Id)
{
	for (FHIThief& T : Intruders)
	{
		if (T.Id == Id)
		{
			return &T;
		}
	}
	return nullptr;
}

void FHISim::FHIRound::FireWeapon()
{
	const FHIWeapon Spec = Weapon(Player.Weapon);
	Player.Mag -= 1;
	Player.Cd = Spec.Cd;

	// THEY hear it -- all of them, each scaled by his own distance. Alerting only the
	// primary left the other two calmly working the house while a shotgun went off two
	// rooms away. A shotgun at room range is not the same event as a pistol across the
	// house, and this is what gives the firearm a deterrent value beyond its damage.
	const bool bShotgun = Player.Weapon == TEXT("shotgun");
	for (FHIThief& T : Intruders)
	{
		if (!T.bAlive)
		{
			continue;
		}
		float Heard = FMath::Clamp(
			1.6f - FVector2D::Distance(Player.Pos, T.Pos) / 18.0f, 0.15f, 1.6f);
		if (bShotgun)
		{
			Heard *= 1.5f;
		}
		AlertThief(T, Player.Pos, Heard);
	}

	// The shot itself, for the actor layer to trace. Everything a trace cannot decide -- the
	// round spent, the cooldown, and who heard it -- has already happened above, so this
	// carries the weapon's own numbers and nothing else. Hit resolution needs Unreal's
	// collision, which is the one thing that cannot live in here.
	//
	// This is the port's answer to game.gd's _fire() doing both jobs at once: the round
	// reports that a shot left the barrel, and whoever owns a world decides where it went.
	FHIEvent Shot;
	Shot.Type = TEXT("playerShot");
	Shot.Spread = Spec.Spread;
	Shot.Damage = Spec.Damage;
	Shot.Pellets = Spec.Pellets;
	Events.Add(Shot);
}

void FHISim::FHIRound::ApplyRoundRules()
{
	// Only the ways to LOSE are scanned here, and all three are idempotent, so re-reading
	// an event the caller has not drained yet cannot double-count anything. The fourth
	// outcome, thiefDown, is handled where the damage is applied (DamageIntruder), because
	// a hit is resolved by the actor layer rather than inside Step.
	//
	// WITNESS LINE (earned its place 2026-10-14): a runtime round once refused to end with a
	// `playerDown` demonstrably pending in this list before the step -- logged by the actor
	// layer at the killing blow AND by the pre-step watch in Tick -- while the unit test of
	// the same sequence passed on the same binary. The disagreement was never explained to
	// the last decimal (the suspect was a stale HISim build artifact, replaced by the next
	// relink; a clean rebuild followed), and a scan that has once been doubted gets to say
	// what it saw. One line per death, not one per scan.
	int32 Downs = 0;
	for (const FHIEvent& Ev : Events)
	{
		if (Ev.Type == TEXT("playerDown"))
		{
			++Downs;
		}
		if (Ev.Type == TEXT("allStolen") || Ev.Type == TEXT("escaped")
			|| Ev.Type == TEXT("playerDown"))
		{
			// Three different ways to lose, and the text has to say which one happened,
			// but the round state is the same for all of them.
			State = EHIRoundState::Lost;
		}
	}
	if (Downs > 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("HI: the round rules saw %d playerDown among %d event(s) -- state is now %d"),
			Downs, Events.Num(), static_cast<int32>(State));
	}
}

bool FHISim::FHIRound::DamageIntruder(int32 Id, float Damage)
{
	FHIThief* Who = FindIntruder(Id);
	if (Who == nullptr)
	{
		return false;
	}
	const bool bKilled = DamageThief(*Who, Damage, Events);
	if (!bKilled)
	{
		return false;
	}

	// Whatever he was carrying falls where he does rather than vanishing with him, so the
	// valuables are still findable after the round is won.
	if (!Who->Carry.IsEmpty())
	{
		for (FHILootItem& L : Loot)
		{
			if (L.Label == Who->Carry)
			{
				L.bTaken = false;
				L.bDelivered = false;
				L.Pos = Who->Pos;
			}
		}
		Who->Carry.Empty();
	}
	++ThievesDown;
	// NOT over while any of them is still up. His death ending the round was right for one
	// intruder and is wrong for three: shooting the first man through the door would hand
	// you a win with two still inside the house.
	if (!AnyIntruderStanding())
	{
		State = EHIRoundState::Won;
	}
	return true;
}

TArray<FHISim::FHIEvent> FHISim::FHIRound::ConsumeEvents()
{
	TArray<FHIEvent> Out = MoveTemp(Events);
	Events.Reset();
	return Out;
}

void FHISim::FHIRound::Step(const FHIInput& Input, float Dt)
{
	if (State != EHIRoundState::Play)
	{
		// A finished round is inert. Ticking it would let bodies keep robbing a house whose
		// outcome has already been decided.
		return;
	}

	// ---- the durable verbs, BEFORE the movement they modify
	//
	// Order matters and is not arbitrary. Switching goes first because it decides which
	// magazine a reload would fill; the reload goes before the movement because a man
	// changing a magazine walks slower THIS frame, which is the whole cost of reloading and
	// the reason reloading while sprinting through a doorway is no longer free. game.gd gets
	// the same ordering for free, because Godot delivers _input before _process.
	if (!Input.SwitchWeapon.IsEmpty())
	{
		SwitchWeapon(Input.SwitchWeapon);
	}
	if (Input.bReload && Player.Reloading <= 0.0f && CanLoadMagazine(Player))
	{
		Player.Reloading = Weapon(Player.Weapon).Reload;
	}
	if (Input.bShove)
	{
		// The cooldown and the front arc are both inside Shove, so a shove that lands
		// nothing costs nothing -- exactly as firing a weapon on cooldown does.
		TryShove();
	}
	if (Input.bShout)
	{
		Shout();
	}
	if (Input.bCallPolice)
	{
		// Idempotent, so holding the key cannot make the sirens arrive sooner.
		CallPolice();
	}

	// ---- the homeowner
	// RECORDED, not just read. These two fields existed and were never written after spawn,
	// so anything that asked him how he was moving got the answer he gave at birth: the HUD's
	// stance readout said STANDING forever, and the crouch eye height below was unreachable.
	// game.gd writes both every frame; this is the port catching up.
	Player.bCrouching = Input.bCrouch;
	// Sprinting needs BOTH the key and somewhere to go, and crouching wins over it -- the
	// same three-way condition game.gd uses. A sprint flag set while standing still would
	// make the stance readout lie and would let a future speed calculation trust it.
	Player.bSprinting = Input.bSprint && !Input.bCrouch && Input.Axis.Size() > 0.0f;

	float Speed = PlayerSpeed;
	if (Input.bCrouch)
	{
		Speed = PlayerCrouchSpeed;
	}
	else if (Input.bSprint)
	{
		Speed = PlayerSprintSpeed;
	}
	if (Player.Reloading > 0.0f)
	{
		Speed *= ReloadPace;
	}

	FVector2D Wish = FVector2D::ZeroVector;
	if (Input.Axis.Size() > 0.0f)
	{
		// Camera-relative: forward is the yaw direction in XZ, and camera-RIGHT is computed
		// rather than guessed. At yaw 0 he looks toward +Z, so his right is -X; the mirrored
		// (cos, -sin) is exactly what made A strafe right and D left.
		//
		// `+Input.Axis.Y`, NOT `-`: this port's axis is "+1 is forward", which is what the game
		// layer builds and what every caller and test assumes. `game.gd`'s `_move_axis` gives W
		// `v.y -= 1` -- MINUS one -- and then negates it here, so carrying that negation across
		// while flipping the input to +1 turned it into a double negative: **W walked
		// backwards.** Reported from play as *"switch the W and S button, right now they work
		// counter intuitive"*, which is a precise description of two sign flips that no longer
		// cancel.
		const FVector2D Fwd(FMath::Sin(Player.Yaw), FMath::Cos(Player.Yaw));
		const FVector2D Right(-FMath::Cos(Player.Yaw), FMath::Sin(Player.Yaw));
		Wish = (Fwd * Input.Axis.Y + Right * Input.Axis.X).GetSafeNormal() * Speed;
	}

	// He carries momentum between frames instead of being assigned a velocity. This is the
	// whole reason he stopped reading as a sliding prop: previously he was at full speed on
	// the first frame of a keypress and stationary on the frame after release.
	FVector2D V = MoveToward(Player.Vel, Wish,
		(Wish != FVector2D::ZeroVector ? PlayerAccel : PlayerDecel) * Dt);
	if (V.Size() > 0.02f)
	{
		FVector2D P = Player.Pos + V * Dt;
		P = ResolveCircle(P, PlayerRadius);
		P = ClampToWorld(P, PlayerRadius);
		// A wall has to STOP him, not let momentum pile up against it.
		const FVector2D Moved = P - Player.Pos;
		if (Moved.Size() < V.Size() * Dt * 0.5f)
		{
			V = Moved / FMath::Max(Dt, 1e-4f);
		}
		Player.Pos = P;
	}
	else
	{
		V = FVector2D::ZeroVector;
	}
	Player.Vel = V;

	if (Player.Reloading > 0.0f)
	{
		Player.Reloading -= Dt;
		if (Player.Reloading <= 0.0f)
		{
			// Out of the reserve, not out of thin air.
			LoadMagazine(Player);
		}
	}
	Player.Cd -= Dt;
	Player.ShoveCd = FMath::Max(0.0f, Player.ShoveCd - Dt);
	Player.ShoutCd = FMath::Max(0.0f, Player.ShoutCd - Dt);
	Player.Stagger = FMath::Max(0.0f, Player.Stagger - Dt);

	// A man with his hands on you is a man you cannot aim through.
	if (Input.bFire && Player.Cd <= 0.0f && Player.Reloading <= 0.0f
		&& Player.Mag > 0 && Player.Stagger <= 0.0f)
	{
		FireWeapon();
	}

	// ---- every intruder, then the round rules
	StepIntruders(Intruders, Player, Loot, Events, Dt);
	ApplyRoundRules();
}

TArray<FHISim::FHILootItem> FHISim::CreateLoot()
{
	TArray<FHILootItem> Out;
	const TArray<FVector2D>& Poses = LootPositions();
	const TArray<FString>& Labels = LootLabels();
	const TArray<FString>& Nodes = LootNodes();
	for (int32 Idx = 0; Idx < Poses.Num(); ++Idx)
	{
		FHILootItem L;
		L.Pos = Poses[Idx];
		L.Label = Labels[Idx];
		L.Node = Nodes[Idx];
		L.bTaken = false;
		L.bDelivered = false;
		// He does not start knowing where anything is; searching is what reveals it.
		L.bKnown = false;
		Out.Add(L);
	}
	return Out;
}

int32 FHISim::Remaining(const TArray<FHILootItem>& Loot)
{
	int32 N = 0;
	for (const FHILootItem& L : Loot)
	{
		if (!L.bDelivered)
		{
			++N;
		}
	}
	return N;
}

float FHISim::MoveToward(float Current, float Target, float MaxDelta)
{
	// Godot's move_toward() has no Unreal equivalent -- FMath exposes neither MoveToward nor
	// MoveTowards -- so it is written out. Snapping to Target when the remaining distance is
	// within MaxDelta is what lets a coasting body come to rest exactly on zero rather than
	// oscillating about it by a fraction of a step.
	const float Diff = Target - Current;
	if (FMath::Abs(Diff) <= MaxDelta)
	{
		return Target;
	}
	return Current + FMath::Sign(Diff) * MaxDelta;
}

// ---------------------------------------------------------------------- world data

const TArray<FBox2D>& FHISim::Walls()
{
	static const TArray<FBox2D> W = []()
	{
		const float T = WallThickness;
		const FBox2D H = House;
		const float IZ = InteriorZ;
		TArray<FBox2D> Out;
		auto R = [](float X, float Z, float Wd, float Dp)
		{
			return FBox2D(FVector2D(X, Z), FVector2D(X + Wd, Z + Dp));
		};
		// exterior
		Out.Add(R(H.Min.X, H.Min.Y, H.GetSize().X, T));               // north
		Out.Add(R(H.Min.X, H.Max.Y - T, DoorGap.X - H.Min.X, T));     // south-west of the door
		Out.Add(R(DoorGap.Y, H.Max.Y - T, H.Max.X - DoorGap.Y, T));   // south-east of the door
		Out.Add(R(H.Min.X, H.Min.Y, T, H.GetSize().Y));               // west
		Out.Add(R(H.Max.X - T, H.Min.Y, T, H.GetSize().Y));           // east
		// interior divider with a central opening
		Out.Add(R(H.Min.X, IZ - T * 0.5f, HallGap.X - H.Min.X, T));
		Out.Add(R(HallGap.Y, IZ - T * 0.5f, H.Max.X - HallGap.Y, T));
		return Out;
	}();
	return W;
}

namespace
{
	/** Furniture exactly as sim.gd's FURNITURE literal, in the same order. */
	struct FFurnRow
	{
		float X, Z, W, D;
		/**
		 * `h` -- the item's HEIGHT, straight out of sim.gd's literal.
		 *
		 * This field was DROPPED by the first port of this table, and its absence is the
		 * whole reason every couch and table in the game rendered as a floor-to-ceiling
		 * column. The claim on record was that "the Godot source carries no furniture
		 * heights, and inventing them is a design decision" -- the Godot source carries
		 * them, one per row, and `game.gd:483` draws with them:
		 *
		 *     _add_box(body, f["rect"], f["h"])
		 *
		 * So this is a port of an existing number, not an invented one, and the sim's
		 * SIGHT model is untouched by it: `SolidSpans` stays uniform because Godot's own
		 * `los_blocked` is 2-D and ignores `h` entirely. Draw tall, see through -- that
		 * mismatch is fix-list item 18, and it is the original's, not this port's.
		 */
		float H;
		bool bFlat;
		/** The sim's own `kind`, so the game layer can name what it draws. */
		const TCHAR* Label;
	};
	const TArray<FFurnRow>& FurnitureRows()
	{
		static const TArray<FFurnRow> Rows = {
			// living room (front west)
			{ -7.40f, -12.60f, 1.20f, 3.20f, 0.80f, false, TEXT("sofa") },
			{ -7.00f, -14.10f, 0.40f, 0.40f, 0.45f, false, TEXT("side_table") },
			{ -5.60f, -11.70f, 1.40f, 1.40f, 0.42f, false, TEXT("coffee_table") },
			{ -6.40f, -13.60f, 2.20f, 0.50f, 0.50f, false, TEXT("tv_stand") },
			// z was -8.60 in both projects, which put 1.2 m of this rug OUTSIDE the south
			// wall and onto the drive -- `House` is z -19..-8, so the rug ran to -6.80. It
			// went unnoticed because a rug is `flat`, so it is excluded from `Solids()` and
			// was never drawn, until this port started drawing flat furniture. Fixed in
			// `sim.gd` first, as the spec, and matched here.
			{ -7.60f, -10.60f, 4.40f, 1.80f, 0.02f, true,  TEXT("rug") },
			{ -7.30f,  -9.90f, 0.45f, 0.45f, 1.60f, false, TEXT("lamp") },
			// kitchen (front east)
			{  6.60f, -13.00f, 1.15f, 4.00f, 0.92f, false, TEXT("counter") },
			{  6.70f, -13.85f, 1.00f, 0.80f, 1.85f, false, TEXT("fridge") },
			{  2.60f, -12.40f, 2.00f, 1.50f, 0.75f, false, TEXT("dining_table") },
			{  3.15f, -10.75f, 0.55f, 0.50f, 0.90f, false, TEXT("chair") },
			{  3.55f, -13.00f, 0.55f, 0.45f, 0.90f, false, TEXT("chair") },
			// bedroom (back west)
			{ -7.40f, -18.60f, 2.40f, 2.20f, 0.55f, false, TEXT("bed") },
			{ -7.40f, -16.20f, 0.50f, 0.50f, 0.55f, false, TEXT("nightstand") },
			{ -4.60f, -18.70f, 1.20f, 0.60f, 2.00f, false, TEXT("wardrobe") },
			{ -6.80f, -15.60f, 3.80f, 1.00f, 0.02f, true,  TEXT("rug") },
			// study (back east)
			{  5.50f, -17.40f, 2.00f, 1.10f, 0.75f, false, TEXT("desk") },
			{  5.00f, -16.10f, 0.60f, 0.60f, 0.95f, false, TEXT("desk_chair") },
			{  0.60f, -18.60f, 0.80f, 2.60f, 2.00f, false, TEXT("bookshelf") },
			{  0.70f, -15.60f, 0.60f, 0.60f, 1.30f, false, TEXT("cabinet") },
		};
		return Rows;
	}
}

const TArray<float>& FHISim::FurnitureHeights()
{
	// Index-aligned with `FurnitureRects`, which is what the builder needs: it walks the
	// non-flat half of `Solids()` and asks for the height of the row it is on.
	static const TArray<float> H = []()
	{
		TArray<float> Out;
		for (const FFurnRow& F : FurnitureRows())
		{
			Out.Add(F.H);
		}
		return Out;
	}();
	return H;
}

const TArray<FString>& FHISim::FurnitureLabels()
{
	// The sim's own `kind` per furniture row, in the same order as `FurnitureRects`.
	//
	// Descriptive only -- nothing in the sim reads it. It exists so a failure can name the
	// item it is talking about, and so the game layer can key anything per-kind without
	// guessing at a rectangle's identity from its coordinates.
	static const TArray<FString> L = []()
	{
		TArray<FString> Out;
		for (const FFurnRow& F : FurnitureRows())
		{
			Out.Add(F.Label);
		}
		return Out;
	}();
	return L;
}

const TArray<FBox2D>& FHISim::FurnitureRects()
{
	static const TArray<FBox2D> R = []()
	{
		TArray<FBox2D> Out;
		for (const FFurnRow& F : FurnitureRows())
		{
			Out.Add(FBox2D(FVector2D(F.X, F.Z), FVector2D(F.X + F.W, F.Z + F.D)));
		}
		return Out;
	}();
	return R;
}

const TArray<bool>& FHISim::FurnitureFlat()
{
	static const TArray<bool> R = []()
	{
		TArray<bool> Out;
		for (const FFurnRow& F : FurnitureRows())
		{
			Out.Add(F.bFlat);
		}
		return Out;
	}();
	return R;
}

const TArray<FBox2D>& FHISim::Solids()
{
	// Walls plus every non-flat furniture rect. Rugs are drawn, never solid -- a rug that
	// blocks a route would stall a heist for no reason a player could see.
	static const TArray<FBox2D> S = []()
	{
		TArray<FBox2D> Out = Walls();
		const TArray<FBox2D>& Furn = FurnitureRects();
		const TArray<bool>& Flat = FurnitureFlat();
		for (int32 Idx = 0; Idx < Furn.Num(); ++Idx)
		{
			if (!Flat[Idx])
			{
				Out.Add(Furn[Idx]);
			}
		}
		return Out;
	}();
	return S;
}

const TArray<FHISim::FHISolidSpan>& FHISim::SolidSpans()
{
	// Uniform by construction rather than by listing: every solid spans a full storey, so
	// the height-aware test below reproduces the 2-D one exactly and no existing assertion
	// moves. See the header for why the numbers that would make this interesting -- a sofa
	// you can see over -- are a design decision rather than a porting one.
	static const TArray<FHISolidSpan> Spans = []()
	{
		TArray<FHISolidSpan> Out;
		Out.Init(FHISolidSpan(), Solids().Num());
		return Out;
	}();
	return Spans;
}

const TArray<FVector2D>& FHISim::LootPositions()
{
	static const TArray<FVector2D> L = {
		FVector2D(-5.30f, -12.90f),
		FVector2D( 6.60f, -16.05f),
		FVector2D(-4.20f, -17.60f),
	};
	return L;
}

const TArray<FString>& FHISim::LootLabels()
{
	static const TArray<FString> L = { TEXT("TV"), TEXT("Laptop"), TEXT("Safe") };
	return L;
}

const TArray<FString>& FHISim::LootNodes()
{
	static const TArray<FString> L = { TEXT("living"), TEXT("study"), TEXT("bedroom") };
	return L;
}

const TArray<FString>& FHISim::SearchNodes()
{
	// kitchen is deliberate: searching that cannot come up empty is not searching.
	static const TArray<FString> S = { TEXT("living"), TEXT("kitchen"), TEXT("bedroom"), TEXT("study") };
	return S;
}

const TArray<FString>& FHISim::NodeNames()
{
	static const TArray<FString> N = {
		TEXT("van"), TEXT("doorOut"), TEXT("doorIn"), TEXT("frontHub"),
		TEXT("living"), TEXT("kitchen"), TEXT("opening"),
		TEXT("bedroom"), TEXT("study"),
	};
	return N;
}

const TArray<FVector2D>& FHISim::NodePositions()
{
	static const TArray<FVector2D> P = {
		FVector2D( 0.0f,   5.0f),   // van
		FVector2D( 0.0f,  -5.0f),   // doorOut
		FVector2D( 0.0f,  -9.6f),   // doorIn
		FVector2D( 0.0f, -11.2f),   // frontHub
		FVector2D(-3.6f, -11.2f),   // living
		FVector2D( 4.5f,  -9.6f),   // kitchen
		FVector2D( 0.0f, -14.0f),   // opening
		FVector2D(-4.4f, -15.6f),   // bedroom
		FVector2D( 6.5f, -15.5f),   // study
	};
	return P;
}

const TArray<TArray<FString>>& FHISim::NodeEdges()
{
	static const TArray<TArray<FString>> E = {
		{ TEXT("doorOut") },                                    // van
		{ TEXT("van"), TEXT("doorIn") },                        // doorOut
		{ TEXT("doorOut"), TEXT("frontHub") },                  // doorIn
		{ TEXT("doorIn"), TEXT("living"), TEXT("kitchen"), TEXT("opening") }, // frontHub
		{ TEXT("frontHub") },                                   // living
		{ TEXT("frontHub") },                                   // kitchen
		{ TEXT("frontHub"), TEXT("bedroom"), TEXT("study") },   // opening
		{ TEXT("opening") },                                    // bedroom
		{ TEXT("opening") },                                    // study
	};
	return E;
}

// ------------------------------------------------------------------------ geometry

bool FHISim::SegRect(const FVector2D& A, const FVector2D& B, const FBox2D& Rect, float RRadius)
{
	// Liang-Barsky, ported term for term from sim.gd.
	float T0 = 0.0f;
	float T1 = 1.0f;
	const float DX = B.X - A.X;
	const float DZ = B.Y - A.Y;
	const FVector2D Lo = Rect.Min - FVector2D(RRadius, RRadius);
	const FVector2D Hi = Rect.Max + FVector2D(RRadius, RRadius);
	const float P[4] = { -DX, DX, -DZ, DZ };
	const float Q[4] = { A.X - Lo.X, Hi.X - A.X, A.Y - Lo.Y, Hi.Y - A.Y };

	for (int32 Idx = 0; Idx < 4; ++Idx)
	{
		if (FMath::Abs(P[Idx]) < 1e-9f)
		{
			if (Q[Idx] < 0.0f)
			{
				return false;
			}
			continue;
		}
		const float T = Q[Idx] / P[Idx];
		if (P[Idx] < 0.0f)
		{
			if (T > T1) return false;
			if (T > T0) T0 = T;
		}
		else
		{
			if (T < T0) return false;
			if (T < T1) T1 = T;
		}
	}
	return true;
}

bool FHISim::SegCircle(const FVector2D& A, const FVector2D& B, const FVector2D& C, float Radius)
{
	const FVector2D D = B - A;
	const float Len2 = D.SizeSquared();
	float T = 0.0f;
	if (Len2 > 0.0f)
	{
		T = FMath::Clamp((C - A).Dot(D) / Len2, 0.0f, 1.0f);
	}
	const FVector2D P = A + D * T;
	return FVector2D::DistSquared(P, C) <= Radius * Radius;
}

bool FHISim::LosBlocked(const FVector2D& A, const FVector2D& B)
{
	for (const FBox2D& S : Solids())
	{
		if (SegRect(A, B, S))
		{
			return true;
		}
	}
	return false;
}

FVector FHISim::AimDir(float Yaw, float Pitch)
{
	const float Ap = FMath::Clamp(Pitch, PitchMin, PitchMax);
	const float CosAp = FMath::Cos(Ap);
	// Godot: Vector3(sin(ay)*cos(ap), -sin(ap), cos(ay)*cos(ap)), which is (x, up, z).
	// Its `up` becomes this Z and its `z` becomes this Y. The minus on the Z term is the
	// same one Godot has: NEGATIVE pitch looks UP, which is why PitchMin is the "looking
	// up" limit and PitchMax the "looking down" one.
	return FVector(FMath::Sin(Yaw) * CosAp, FMath::Cos(Yaw) * CosAp, -FMath::Sin(Ap));
}

FVector FHISim::CameraOffset(float Yaw, float Pitch, float Back, float Up, float Side)
{
	const FVector View = AimDir(Yaw, Pitch);
	// Camera-RIGHT in the plane, the same (-cos, sin) the movement code uses. At yaw 0 the
	// player faces +Y, so his right is -X.
	const FVector Right(-FMath::Cos(Yaw), FMath::Sin(Yaw), 0.0f);
	return -View * Back + FVector(0.0f, 0.0f, Up) + Right * Side;
}

bool FHISim::LosBlocked3D(const FVector& A, const FVector& B,
	const TArray<FBox2D>& InSolids, const TArray<FHISolidSpan>& Spans)
{
	const float SegLo = FMath::Min(A.Z, B.Z);
	const float SegHi = FMath::Max(A.Z, B.Z);
	for (int32 Idx = 0; Idx < InSolids.Num(); ++Idx)
	{
		// Vertical reject first, and it is the whole point of the function: a solid the
		// segment never reaches in height cannot stop it, however its footprint lies. This
		// is what lets a floor slab sit above a pair of ground-floor eyes without blocking
		// them, and what would let a low obstacle be seen over.
		const FHISolidSpan& Span = Spans[Idx];
		if (SegHi < Span.MinZ || SegLo > Span.MaxZ)
		{
			continue;
		}
		if (SegRect(FVector2D(A.X, A.Y), FVector2D(B.X, B.Y), InSolids[Idx]))
		{
			return true;
		}
	}
	return false;
}

bool FHISim::LosBlocked3D(const FVector& A, const FVector& B)
{
	return LosBlocked3D(A, B, Solids(), SolidSpans());
}

float FHISim::RayCastPx(const FVector2D& From, const FVector2D& Dir, float MaxT)
{
	// The hottest function in the game: the visibility polygon calls it hundreds of times
	// per frame. Written out longhand with no allocation, exactly as in sim.gd, because an
	// earlier version that built two arrays per solid per ray cost ~17,600 allocations per
	// polygon and ran 5.1 ms.
	float Best = MaxT;
	const float OX = From.X;
	const float OZ = From.Y;
	const float DX = Dir.X;
	const float DZ = Dir.Y;

	for (const FBox2D& S : Solids())
	{
		float TMin = 0.0f;
		float TMax = MaxT;
		bool bOk = true;

		// --- X slab
		float Lo = S.Min.X;
		float Hi = S.Max.X;
		if (FMath::Abs(DX) < 1e-9f)
		{
			if (OX < Lo || OX > Hi)
			{
				bOk = false;
			}
		}
		else
		{
			float T1s = (Lo - OX) / DX;
			float T2s = (Hi - OX) / DX;
			if (T1s > T2s) { Swap(T1s, T2s); }
			if (T1s > TMin) { TMin = T1s; }
			if (T2s < TMax) { TMax = T2s; }
			if (TMin > TMax) { bOk = false; }
		}

		// --- Z slab (stored in .Y)
		if (bOk)
		{
			Lo = S.Min.Y;
			Hi = S.Max.Y;
			if (FMath::Abs(DZ) < 1e-9f)
			{
				if (OZ < Lo || OZ > Hi)
				{
					bOk = false;
				}
			}
			else
			{
				float T3 = (Lo - OZ) / DZ;
				float T4 = (Hi - OZ) / DZ;
				if (T3 > T4) { Swap(T3, T4); }
				if (T3 > TMin) { TMin = T3; }
				if (T4 < TMax) { TMax = T4; }
				if (TMin > TMax) { bOk = false; }
			}
		}

		if (bOk && TMin < Best)
		{
			Best = TMin;
		}
	}
	return Best;
}

bool FHISim::CanSee(const FVector2D& A, const FVector2D& B, float MaxRange)
{
	if (FVector2D::Distance(A, B) > MaxRange)
	{
		return false;
	}
	return !LosBlocked(A, B);
}

bool FHISim::ThiefSees(const FVector2D& ThiefPos, float ThiefYaw, const FVector2D& PlayerPos)
{
	if (!CanSee(ThiefPos, PlayerPos, ThiefSight))
	{
		return false;
	}
	const float Bearing = FMath::Atan2(PlayerPos.Y - ThiefPos.Y, PlayerPos.X - ThiefPos.X);
	return FMath::Abs(FMath::FindDeltaAngleRadians(Bearing, ThiefYaw))
		<= FMath::DegreesToRadians(ThiefFovDeg * 0.5f);
}

TArray<FVector2D> FHISim::VisibilityPolygon(const FVector2D& From, float MaxRange)
{
	// The +/-1e-4 neighbours of each corner angle are the whole trick: the centre ray grazes
	// the corner exactly, and its two neighbours land just inside and just outside, so the
	// ring has vertices on both sides of every occluder edge instead of one sample slipping
	// through the seam between two solids.
	TArray<float> Angles;
	Angles.Reserve(Solids().Num() * 12 + 64);

	for (const FBox2D& S : Solids())
	{
		const FVector2D Corners[4] = {
			FVector2D(S.Min.X, S.Min.Y),
			FVector2D(S.Max.X, S.Min.Y),
			FVector2D(S.Max.X, S.Max.Y),
			FVector2D(S.Min.X, S.Max.Y),
		};
		for (const FVector2D& C : Corners)
		{
			// Godot's Vector2.angle() is atan2(y, x); FMath::Atan2 takes (Y, X) too.
			const float A = FMath::Atan2(C.Y - From.Y, C.X - From.X);
			Angles.Add(A - 1e-4f);
			Angles.Add(A);
			Angles.Add(A + 1e-4f);
		}
	}

	// 64 even samples so open ground is a smooth arc rather than a fan of corner rays.
	// Godot's TAU is 2*PI. UE_PI is used rather than PI, which is a deprecated macro alias.
	for (int32 Idx = 0; Idx < 64; ++Idx)
	{
		Angles.Add(static_cast<float>(Idx) / 64.0f * (2.0f * UE_PI));
	}

	Angles.Sort();

	TArray<FVector2D> Points;
	Points.Reserve(Angles.Num());
	for (const float A : Angles)
	{
		const FVector2D Dir(FMath::Cos(A), FMath::Sin(A));
		Points.Add(From + Dir * RayCastPx(From, Dir, MaxRange));
	}
	return Points;
}

FVector2D FHISim::ResolveCircle(const FVector2D& Pos, float Radius)
{
	return ResolveCircle(Pos, Radius, Solids());
}

FVector2D FHISim::ResolveCircle(const FVector2D& InPos, float Radius, const TArray<FBox2D>& InSolids)
{
	FVector2D Pos = InPos;
	for (const FBox2D& S : InSolids)
	{
		const FVector2D Closest(
			FMath::Clamp(Pos.X, S.Min.X, S.Max.X),
			FMath::Clamp(Pos.Y, S.Min.Y, S.Max.Y));
		const FVector2D D = Pos - Closest;
		const float D2 = D.SizeSquared();
		if (D2 >= Radius * Radius)
		{
			continue;
		}
		if (D2 < 1e-9f)
		{
			// centre is inside the solid: push out along the shallowest axis
			const float Left = Pos.X - S.Min.X;
			const float Right = S.Max.X - Pos.X;
			const float Near = Pos.Y - S.Min.Y;
			const float Far = S.Max.Y - Pos.Y;
			const float M = FMath::Min(FMath::Min(Left, Right), FMath::Min(Near, Far));
			if (M == Left)       { Pos.X = S.Min.X - Radius; }
			else if (M == Right) { Pos.X = S.Max.X + Radius; }
			else if (M == Near)  { Pos.Y = S.Min.Y - Radius; }
			else                 { Pos.Y = S.Max.Y + Radius; }
			continue;
		}
		const float Dist = FMath::Sqrt(D2);
		Pos += D * ((Radius - Dist) / Dist);
	}
	return Pos;
}

FVector2D FHISim::ClampToWorld(const FVector2D& Pos, float Radius)
{
	return FVector2D(
		FMath::Clamp(Pos.X, Bounds.Min.X + Radius, Bounds.Max.X - Radius),
		FMath::Clamp(Pos.Y, Bounds.Min.Y + Radius, Bounds.Max.Y - Radius));
}

TArray<FVector2D> FHISim::SeparateBodies(const FVector2D& PlayerPos, const FVector2D& ThiefPos)
{
	// THE HOMEOWNER YIELDS. That choice is load-bearing and cost a shipped bug: with the
	// intruder yielding instead, a player merely standing on his route was an immovable
	// wall -- 142 re-routes and nothing ever delivered.
	const float MinDist = PlayerRadius + ThiefRadius;
	const FVector2D D = ThiefPos - PlayerPos;
	const float Dist = D.Size();
	if (Dist >= MinDist)
	{
		return { PlayerPos, ThiefPos };
	}
	const FVector2D N = (Dist < 1e-5f) ? FVector2D(1.0f, 0.0f) : D / Dist;

	FVector2D T = ThiefPos;
	FVector2D P = ClampToWorld(ResolveCircle(PlayerPos - N * (MinDist - Dist), PlayerRadius), PlayerRadius);

	const FVector2D Gap = T - P;
	const float GapLen = Gap.Size();
	if (GapLen < MinDist - 1e-4f)
	{
		// he is against something immovable, so the remainder comes out of the intruder
		const FVector2D BN = (GapLen < 1e-5f) ? N : Gap / GapLen;
		T = ClampToWorld(ResolveCircle(P + BN * MinDist, ThiefRadius), ThiefRadius);
	}
	return { P, T };
}

TArray<FVector2D> FHISim::SeparateAll(const TArray<FVector2D>& Positions, const TArray<float>& Radii)
{
	// ITERATIVE, unlike the two-body version: resolving the homeowner against one intruder
	// can push him into another, and "no two bodies ever overlap" is a contract.
	constexpr int32 PairSweeps = 3;
	TArray<FVector2D> Out = Positions;
	const int32 N = Out.Num();
	if (N < 2)
	{
		return Out;
	}

	for (int32 Sweep = 0; Sweep < PairSweeps; ++Sweep)
	{
		bool bMoved = false;
		for (int32 Idx = 0; Idx < N; ++Idx)
		{
			for (int32 Jdx = Idx + 1; Jdx < N; ++Jdx)
			{
				// NB: not "PI" -- UnrealMathUtility.h defines PI as a macro, so a variable of
				// that name is expanded into the constant and the declaration will not compile.
				const FVector2D PosI = Out[Idx];
				FVector2D PJ = Out[Jdx];
				const float Want = Radii[Idx] + Radii[Jdx];
				const FVector2D D = PJ - PosI;
				const float Dist = D.Size();
				if (Dist >= Want)
				{
					continue;
				}
				bMoved = true;
				const FVector2D Dir = (Dist < 1e-5f) ? FVector2D(1.0f, 0.0f) : D / Dist;
				const float Push = Want - Dist;
				if (Idx == 0)
				{
					// body 0 is the homeowner: he takes the push, as in the two-body case
					Out[0] = ClampToWorld(ResolveCircle(Out[0] - Dir * Push, Radii[0]), Radii[0]);
					PJ = ClampToWorld(ResolveCircle(Out[Jdx], Radii[Jdx]), Radii[Jdx]);
					Out[Jdx] = PJ;
				}
				else
				{
					// two intruders: split it evenly so neither is privileged
					const float Half = Push * 0.5f;
					Out[Idx] = ClampToWorld(ResolveCircle(PosI - Dir * Half, Radii[Idx]), Radii[Idx]);
					Out[Jdx] = ClampToWorld(ResolveCircle(PJ + Dir * Half, Radii[Jdx]), Radii[Jdx]);
				}
			}
		}
		if (!bMoved)
		{
			break;
		}
	}
	return Out;
}

namespace
{
	/**
	 * Whether one stride of the given heading ends with his body inside something solid.
	 * File-local because its only caller is SteerAround's side choice; it asks the same
	 * question the move loop does, through the same ResolveCircle.
	 */
	bool BlockedAhead(const FVector2D& From, float Heading)
	{
		const FVector2D To = From + FVector2D(FMath::Cos(Heading), FMath::Sin(Heading)) * 1.0f;
		return !FHISim::ResolveCircle(To, FHISim::ThiefRadius).Equals(To, 1e-4f);
	}
}

float FHISim::SteerAround(float A, const FVector2D& From, const FVector2D& PlayerPos)
{
	const FVector2D ToP = PlayerPos - From;
	const float Gap = ToP.Size();
	if (Gap > AvoidRange || Gap < 1e-5f)
	{
		return A;
	}
	const FVector2D Fwd(FMath::Cos(A), FMath::Sin(A));
	const FVector2D Dir = ToP / Gap;
	if (Fwd.Dot(Dir) < 0.0f)
	{
		// he is behind us: nothing to steer around
		return A;
	}
	// break dead-on ties to a fixed side so he is predictable rather than jittering
	const float Cross = Fwd.X * Dir.Y - Fwd.Y * Dir.X;
	float Side = (FMath::Abs(Cross) < 1e-4f) ? 1.0f : FMath::Sign(Cross);
	// DO NOT STEER INTO A WALL -- a deliberate divergence added 2026-10-15, measured not
	// theorised. The geometric side can point straight at solid geometry: a searcher
	// north of the hall gap, with the homeowner standing between him and the bedroom node,
	// gets the west deflection every frame, and west is the divider's corner one body-radius
	// away. Head-on contact has no slide tangent, so ResolveCircle hands him straight back
	// -- a pincer between the avoid orbit and the wall that holds him to +/-5 mm for the
	// rest of the round (the HI.Sim.Searching regression reproduces it). One probe per side
	// settles it: take the geometric side unless it is blocked and the other is not. The
	// probe is 1.0 m of the deflected heading -- about three-quarters of a second at full
	// pace -- with his own radius, which is the same question ResolveCircle answers.
	if (BlockedAhead(From, A - Side * AvoidTurn) && !BlockedAhead(From, A + Side * AvoidTurn))
	{
		Side = -Side;
	}
	return A - Side * AvoidTurn;
}

// ---------------------------------------------------------------------- navigation

FString FHISim::NearestNode(const FVector2D& P)
{
	FString Best = TEXT("frontHub");
	float BestD = TNumericLimits<float>::Max();
	const TArray<FString>& Names = NodeNames();
	const TArray<FVector2D>& Poses = NodePositions();
	for (int32 Idx = 0; Idx < Names.Num(); ++Idx)
	{
		const float D = FVector2D::DistSquared(Poses[Idx], P);
		if (D < BestD)
		{
			BestD = D;
			Best = Names[Idx];
		}
	}
	return Best;
}

TArray<FString> FHISim::PathNodes(const FString& FromKey, const FString& ToKey)
{
	TArray<FString> Out;
	// Godot returns an EMPTY path when the endpoints are equal, and its caller depends on
	// that. The fleeing intruder re-plans to the van on every frame his route is empty, so
	// once he is AT the van an empty result is what lets the escape fire. Returning the
	// endpoint instead refills the route forever and he stands in the drive -- precisely
	// the failure sim.gd's comment says the route-emptying test exists to avoid.
	if (FromKey == ToKey)
	{
		return Out;
	}
	const TArray<FString>& Names = NodeNames();
	const TArray<TArray<FString>>& Edges = NodeEdges();

	// Breadth-first over a graph this small is both correct and cheap.
	TArray<FString> Queue;
	TMap<FString, FString> CameFrom;
	Queue.Add(FromKey);
	CameFrom.Add(FromKey, TEXT(""));
	while (Queue.Num() > 0)
	{
		const FString Cur = Queue[0];
		Queue.RemoveAt(0);
		if (Cur == ToKey)
		{
			break;
		}
		const int32 Idx = Names.IndexOfByKey(Cur);
		if (Idx == INDEX_NONE)
		{
			continue;
		}
		for (const FString& Next : Edges[Idx])
		{
			if (!CameFrom.Contains(Next))
			{
				CameFrom.Add(Next, Cur);
				Queue.Add(Next);
			}
		}
	}
	if (!CameFrom.Contains(ToKey))
	{
		return Out;
	}
	// Walk back from the destination. The loop stops AT FromKey, so the origin is not part
	// of the path -- Godot's `while k != from_key` excludes it, and a waypoint at the
	// walker's own feet is not a waypoint.
	FString Step = ToKey;
	while (!Step.IsEmpty() && Step != FromKey)
	{
		Out.Insert(Step, 0);
		Step = CameFrom[Step];
	}
	return Out;
}

TArray<FVector2D> FHISim::RouteTo(const FString& FromKey, const FString& ToKey)
{
	TArray<FVector2D> Out;
	const TArray<FString> Path = PathNodes(FromKey, ToKey);
	const TArray<FString>& Names = NodeNames();
	const TArray<FVector2D>& Poses = NodePositions();
	for (const FString& Key : Path)
	{
		const int32 Idx = Names.IndexOfByKey(Key);
		if (Idx != INDEX_NONE)
		{
			Out.Add(Poses[Idx]);
		}
	}
	return Out;
}

// ---------------------------------------------------------------------- thief AI

namespace
{
	/**
	 * sim.gd `route_to(from_key, to_key)` with the final loot position appended:
	 *
	 *     thief["route"] = route_to(nearest_node(tpos), best["node"])
	 *     (thief["route"] as Array).append(best["pos"])
	 *
	 * The nav graph only reaches a ROOM, and the item sits beside the furniture, so the
	 * last leg is a straight append rather than a node.
	 */
	TArray<FVector2D> RouteToWithAppend(const FString& FromKey, const FString& ToKey,
		const FVector2D& AppendPos)
	{
		TArray<FVector2D> Out = FHISim::RouteTo(FromKey, ToKey);
		Out.Add(AppendPos);
		return Out;
	}
}

bool FHISim::StepThief(FHIThief& Thief, FHIPlayer& Player, TArray<FHILootItem>& Loot,
	TArray<FHIEvent>& Events, float Dt, bool bSeparate)
{
	if (!Thief.bAlive)
	{
		return false;
	}

	// `prev` is where he stood at the START of this frame and must be captured before
	// anything moves him. The stuck detector measures travel against it. It is NOT
	// written again at the end: it belongs to whoever owns body resolution -- this
	// function when `separate` is true, and step_intruders otherwise.
	Thief.Prev = Thief.Pos;

	Thief.Cd -= Dt;
	if (Thief.Reloading > 0.0f)
	{
		Thief.Reloading -= Dt;
	}
	Thief.Regrab = FMath::Max(0.0f, Thief.Regrab - Dt);
	Thief.ShoveCd = FMath::Max(0.0f, Thief.ShoveCd - Dt);
	if (Thief.bEscaped)
	{
		return false;
	}

	FVector2D TPos = Thief.Pos;
	FVector2D PPos = Player.Pos;

	// Fear settles when nothing is happening. It does not un-decide a decision already
	// taken, which is why `mode == "flee"` below is sticky.
	Thief.Fear = FMath::Max(0.0f, Thief.Fear - FearDecay * Dt);

	// The clock runs only once somebody has been called, and a gunshot is what calls.
	if (Thief.Alarm >= 0.0f)
	{
		Thief.Alarm += Dt;
		if (Thief.Alarm >= PoliceTime)
		{
			if (!Thief.bSirens)
			{
				Thief.bSirens = true;
				FHIEvent Ev;
				Ev.Type = TEXT("sirens");
				Events.Add(Ev);
			}
			// He drops everything and runs, exactly as if he had been shot at.
			Thief.Fear = FMath::Max(Thief.Fear, FearFlee);
		}
	}

	// At arm's length he puts his hands on you, and this check comes BEFORE the fear branch:
	// a man already close enough to touch you does not first decide whether to run.
	//
	// THIS CALL WAS MISSING, and it is the most dangerous shape a bug takes in this port. The
	// function was ported, the four constants were ported, and `HI.Sim.Hands` asserted its
	// reach, its stagger, its push and its cooldown -- all by calling `ThiefShove` DIRECTLY.
	// So the tests passed while the game never ran it, and the intruder was harmless at
	// contact range: no stagger, no knock-back, no `thiefShove` event, in a house where a man
	// at arm's length is supposed to be the most dangerous thing in it.
	//
	// The comment that used to sit here read "NOT YET PORTED (banner 'the intruder's hands'),
	// so no grab happens here yet", which was wrong twice over: the grab of LOOT is ported
	// elsewhere, and what Godot calls at this line (`sim.gd:897`) is the shove. A stale note
	// explaining an absence is how the absence survived.
	//
	// `ThiefShove` is self-gating -- alive, not escaped, empty-handed, off cooldown, within
	// range -- so calling it every frame is what Godot does and what this does now.
	ThiefShove(Thief, Player, Events);

	// ---- decide
	if (Thief.Mode == TEXT("flee") || Thief.Fear >= FearFlee)
	{
		Thief.Mode = TEXT("flee");
		if (Thief.Route.Num() == 0)
		{
			Thief.Route = RouteTo(NearestNode(TPos), TEXT("van"));
		}
		// Gone once the route to the van is finished AND he is clear of the house. The
		// test is the route emptying, not a distance: the "van" node sits at z 5.0 and
		// the van model at 7.0, so a distance test never fires and he stands in the
		// drive forever.
		if (Thief.Route.Num() == 0 && TPos.Y > House.Max.Y + 1.0f)
		{
			Thief.bEscaped = true;
			FHIEvent Ev;
			Ev.Type = TEXT("escaped");
			Ev.Label = Thief.Carry;
			Events.Add(Ev);
		}
	}
	else if (!Thief.Carry.IsEmpty())
	{
		Thief.Mode = TEXT("carry");
		if (Thief.Route.Num() == 0)
		{
			Thief.Route = RouteTo(NearestNode(TPos), TEXT("van"));
		}
		if (FVector2D::Distance(TPos, Drop) < DropRange)
		{
			for (FHILootItem& L : Loot)
			{
				if (L.Label == Thief.Carry)
				{
					L.bDelivered = true;
				}
			}
			FHIEvent Ev;
			Ev.Type = TEXT("delivered");
			Ev.Label = Thief.Carry;
			Events.Add(Ev);
			Thief.Carry.Empty();
			Thief.Route.Empty();
			if (Remaining(Loot) == 0)
			{
				FHIEvent AllEv;
				AllEv.Type = TEXT("allStolen");
				Events.Add(AllEv);
			}
		}
	}
	else
	{
		// What does he actually KNOW about? Only what he has stood in the room of --
		// knowing all three from spawn is a shopping list, not a burglary.
		TArray<int32> Known;
		for (int32 Idx = 0; Idx < Loot.Num(); ++Idx)
		{
			if (!Loot[Idx].bDelivered && !Loot[Idx].bTaken && Loot[Idx].bKnown)
			{
				Known.Add(Idx);
			}
		}
		if (Known.Num() > 0)
		{
			Thief.Mode = TEXT("hunt");
			int32 TgtIdx = INDEX_NONE;
			for (int32 Idx = 0; Idx < Loot.Num(); ++Idx)
			{
				if (Loot[Idx].Label == Thief.Target)
				{
					TgtIdx = Idx;
				}
			}
			if (TgtIdx == INDEX_NONE || !Loot[TgtIdx].bKnown
				|| Loot[TgtIdx].bDelivered || Loot[TgtIdx].bTaken)
			{
				int32 Best = Known[0];
				for (const int32 Cand : Known)
				{
					if (FVector2D::Distance(TPos, Loot[Cand].Pos)
						< FVector2D::Distance(TPos, Loot[Best].Pos))
					{
						Best = Cand;
					}
				}
				Thief.Target = Loot[Best].Label;
				TgtIdx = Best;
				Thief.Route = RouteToWithAppend(NearestNode(TPos), Loot[Best].Node, Loot[Best].Pos);
			}
			if (FVector2D::Distance(TPos, Loot[TgtIdx].Pos) < GrabRange && Thief.Regrab <= 0.0f)
			{
				Loot[TgtIdx].bTaken = true;
				Thief.Carry = Loot[TgtIdx].Label;
				FHIEvent Ev;
				Ev.Type = TEXT("grabbed");
				Ev.Label = Loot[TgtIdx].Label;
				Events.Add(Ev);
				Thief.Route.Empty();
				Thief.Target.Empty();
			}
		}
		else
		{
			// Nothing known: go and look in a room, nearest unsearched first. He does not
			// cross the house to a far room while an unexplored one is behind him.
			TArray<FString> Rooms;
			for (const FString& N : SearchNodes())
			{
				if (!Thief.Searched.Contains(N))
				{
					Rooms.Add(N);
				}
			}
			if (Rooms.Num() == 0)
			{
				Thief.Mode = TEXT("idle");
			}
			else
			{
				Thief.Mode = TEXT("search");
				FString Room = Rooms[0];
				for (const FString& N : Rooms)
				{
					const int32 NIdx = NodeNames().IndexOfByKey(N);
					const int32 RIdx = NodeNames().IndexOfByKey(Room);
					if (NIdx == INDEX_NONE || RIdx == INDEX_NONE)
					{
						continue;
					}
					if (FVector2D::Distance(TPos, NodePositions()[NIdx])
						< FVector2D::Distance(TPos, NodePositions()[RIdx]))
					{
						Room = N;
					}
				}
				if (Thief.Target != Room)
				{
					Thief.Target = Room;
					Thief.Route = RouteTo(NearestNode(TPos), Room);
				}
				else if (Thief.Route.Num() == 0)
				{
					// SELF-HEAL AN EMPTY ROUTE, the fourth branch to have it and the last to
					// get it: flee, carry and hunt all rebuild when the route runs out, and
					// search used to rebuild ONLY when the target changed. Measured
					// 2026-10-15 in a single-thief soak, byte-identical across two 400 s
					// runs: a reroute dropped him at frontHub, he popped the last waypoint
					// there, target 'bedroom' never changed -- so no route was ever rebuilt,
					// and the stuck detector could not see him because it measures travel
					// against the step he WANTED and an empty route wants nothing
					// (stuck=0.00 for the remaining 327 s, bedroom 5.66 m away, unsearched).
					// With three thieves the others paper over it; the survivor of a
					// homeowner who has dropped two of them freezes the same way, and the
					// round can then never end. Godot's sim.gd:978-980 has the same shape.
					Thief.Route = RouteTo(NearestNode(TPos), Room);
					if (Thief.Route.Num() == 0)
					{
						// The nearest node IS the room, and PathNodes returns an empty path
						// for equal endpoints by design. NearestNode is VORONOI, not
						// walkable-reachable: north of the divider the bedroom node can be
						// the nearest one, and walking straight at it ploughs into the wall
						// -- the hall gap is the only way in, and only the graph knows
						// that. So route from the nearest OTHER node instead; the direct
						// walk stays as the last resort for a graph that cannot answer.
						FString Other;
						float OtherD = TNumericLimits<float>::Max();
						const TArray<FString>& Names = NodeNames();
						const TArray<FVector2D>& Poses = NodePositions();
						for (int32 Idx = 0; Idx < Names.Num(); ++Idx)
						{
							if (Names[Idx] == Room)
							{
								continue;
							}
							const float D = FVector2D::DistSquared(Poses[Idx], TPos);
							if (D < OtherD)
							{
								OtherD = D;
								Other = Names[Idx];
							}
						}
						if (!Other.IsEmpty())
						{
							Thief.Route = RouteTo(Other, Room);
						}
						if (Thief.Route.Num() == 0)
						{
							const int32 DirectIdx = NodeNames().IndexOfByKey(Room);
							if (DirectIdx != INDEX_NONE)
							{
								Thief.Route.Add(NodePositions()[DirectIdx]);
							}
						}
					}
				}
				const int32 RoomIdx = NodeNames().IndexOfByKey(Room);
				if (RoomIdx != INDEX_NONE
					&& FVector2D::Distance(TPos, NodePositions()[RoomIdx]) < SearchRange)
				{
					// Dwell here while he looks, then anything in this room is known.
					Thief.Think = SearchTime;
					Thief.Searched.Add(Room);
					Thief.Route.Empty();
					Thief.Target.Empty();
					for (FHILootItem& L : Loot)
					{
						if (L.Node == Room && !L.bDelivered)
						{
							L.bKnown = true;
						}
					}
				}
			}
		}
	}

	// ---- move
	//
	// The dwell at the threshold. An intruder stops and listens before committing;
	// without it he walks on the first frame of the round.
	if (Thief.Think > 0.0f)
	{
		Thief.Think = FMath::Max(0.0f, Thief.Think - Dt);
	}

	// Carrying costs pace. The hook always existed and nothing ever wrote it, so a safe
	// and a laptop were hauled at exactly the same speed.
	Thief.SpeedScale = CarrySpeed(Thief.Carry);

	float SpeedScale = Thief.SpeedScale;
	Thief.Step = 0.0f;
	bool bWalked = false;
	// A hit stops him where he stands: a person who has just been shot breaks stride, and
	// that interruption is why a firefight reads as one rather than two men trading numbers.
	if (Thief.Stagger > 0.0f)
	{
		Thief.Stagger = FMath::Max(0.0f, Thief.Stagger - Dt);
	}
	while (Thief.Route.Num() > 0 && Thief.Think <= 0.0f && Thief.Stagger <= 0.0f)
	{
		const FVector2D WP = Thief.Route[0];
		// A waypoint the homeowner is standing on can never be reached: the bodies cannot
		// get nearer than the sum of their radii. An occupied waypoint counts as reached
		// once he is as close as collision allows. Nothing is skipped by that: both
		// interaction ranges (GRAB_RANGE 1.4, DROP_RANGE 1.6) exceed the 0.84 m gap.
		const bool bOccupied = FVector2D::Distance(PPos, WP) < PlayerRadius + ThiefRadius;
		if (FVector2D::Distance(TPos, WP) < 0.6f
			|| (bOccupied && FVector2D::Distance(TPos, WP) < 1.2f))
		{
			Thief.Route.RemoveAt(0);
			continue;
		}
		float A = FMath::Atan2(WP.Y - TPos.Y, WP.X - TPos.X);   // (wp - tpos).angle()
		A = SteerAround(A, TPos, PPos);
		// Come about at a finite rate instead of snapping, and lose pace while still
		// turning into the new heading -- a person does not walk full speed sideways.
		// FMath::FindDeltaAngleRadians(A1, A2) returns A2 - A1 wrapped into [-PI, PI] -- the
		// delta FROM A1 TO A2, not the subtraction the argument order suggests. Reading it
		// the other way made him turn AWAY from his heading, which pinned `align` at its
		// 0.55 floor and roughly halved his walking pace: the spec's heist then took over
		// 180 s against the original's 137 s.
		const float Diff = FMath::FindDeltaAngleRadians(Thief.Yaw, A);   // wrapf(a - yaw)
		// Wrapping an angle into [-PI, PI] is the same helper with 0 as the first argument.
		// Passing the angle first returns its NEGATION, which would flip his facing every frame.
		Thief.Yaw = FMath::FindDeltaAngleRadians(
			0.0f, Thief.Yaw + FMath::Clamp(Diff, -ThiefTurnRate * Dt, ThiefTurnRate * Dt));
		const float Align = FMath::Clamp(1.0f - FMath::Abs(Diff) / 3.14159265358979f, 0.55f, 1.0f);
		const float Wish = ThiefSpeed * SpeedScale * Align;
		const float Cur = Thief.Speed;
		Thief.Speed = MoveToward(Cur, Wish, (Wish > Cur ? ThiefAccel : ThiefDecel) * Dt);
		// `step` is the distance he ACTUALLY covered, which is what the stuck detector
		// needs; the intent is `wish`. They were the same before he could accelerate.
		Thief.Step = Thief.Speed * Dt;
		TPos += FVector2D(FMath::Cos(A), FMath::Sin(A)) * Thief.Step;
		// WALL SLIDE, a deliberate divergence added 2026-10-15 (Godot grinds to a halt
		// where this slides). Measured, not theorised: a searcher walking the
		// opening->bedroom leg can pin himself on the west divider's corner -- the leg
		// clears it by 0.80 m from the node, but the 0.6 m waypoint pop lets him start it
		// up to 0.6 m off-axis, which eats the clearance down to 3.6 mm over his 0.42 m
		// radius. Pinned, he jitters +/-0.01 m a frame along the corner face: enough
		// travel to stay above the stuck detector's 25%-of-intent threshold, so StuckT
		// never reaches 1.2 and no reroute ever fires (and a reroute would re-walk the
		// same leg anyway). The frontHub freeze of 2026-10-15 was this pin's escape hatch;
		// with that freeze fixed, the pin itself had to go. Projection onto the contact
		// tangent is what a physical body does: the corner's push normal points east, the
		// desired step points southwest, and the tangential remainder points SOUTH --
		// through the hall gap, which is the way the route meant all along.
		{
			const FVector2D Stepped = TPos;
			const FVector2D Resolved = ResolveCircle(Stepped, ThiefRadius);
			if (!Resolved.Equals(Stepped, 1e-4f))
			{
				FVector2D PushN = Resolved - Stepped;              // out of the solid
				PushN.Normalize();
				const FVector2D StepDir(FMath::Cos(A), FMath::Sin(A));
				FVector2D Tangent = StepDir - StepDir.Dot(PushN) * PushN;
				if (Tangent.SizeSquared() > 0.0025f)               // head-on contact: no slide
				{
					TPos = ResolveCircle(Resolved + Tangent.GetSafeNormal() * Thief.Step, ThiefRadius);
				}
				else
				{
					TPos = Resolved;
				}
			}
			else
			{
				TPos = Resolved;
			}
		}
		TPos = ClampToWorld(TPos, ThiefRadius);
		bWalked = true;
		break;
	}

	// Bleed off momentum only on frames he is NOT walking, so a stop is a stop.
	if (!bWalked)
	{
		Thief.Speed = MoveToward(Thief.Speed, 0.0f, ThiefDecel * Dt);
	}

	// THE STEP'S RESULT IS ALWAYS WRITTEN BACK, and getting this wrong is what made the
	// intruders unable to move. `bSeparate` gates the PAIRWISE SEPARATION below; it was
	// wrongly also gating the position write, so the group path -- `step_intruders`, which is
	// the only path the game calls -- computed a new `TPos` every frame and threw it away.
	//
	// The Godot original has exactly this shape too: `sim.gd` writes `thief["pos"] = tpos`
	// inside `if separate:`, so `step_intruders` never records a step. Two defects compound
	// there and here -- this one, and separating the frame-START positions in
	// `StepIntruders` below -- and together they leave every intruder standing on the
	// driveway for the whole round.
	Thief.Pos = TPos;

	// ---- solid bodies. Applied once both have moved; the homeowner's own step is taken
	// by the caller before this. Skipped when step_intruders resolves the group at once.
	if (bSeparate)
	{
		const TArray<FVector2D> Pair = SeparateBodies(PPos, TPos);
		PPos = Pair[0];
		TPos = Pair[1];
		Player.Pos = PPos;
		Thief.Pos = TPos;
	}

	// ---- stuck detection: re-route rather than stand still forever
	//
	// Measured against what he was actually trying to do this frame, not against
	// THIEF_SPEED: he legitimately covers less ground with a safe in his arms and mid-turn,
	// and while dwelling at the threshold he does not move at all.
	const float Moved = FVector2D::Distance(TPos, Thief.Prev);
	const float Want = Thief.Step;
	if (Want > 0.0f && Moved < FMath::Max(Want * 0.25f, 0.0008f))
	{
		Thief.StuckT += Dt;
	}
	else
	{
		Thief.StuckT = 0.0f;
	}

	if (Thief.StuckT > 1.2f)
	{
		Thief.StuckT = 0.0f;
		FString Back = TEXT("frontHub");
		if (!Thief.Carry.IsEmpty())
		{
			Back = TEXT("van");
		}
		else if (!Thief.Target.IsEmpty())
		{
			for (const FHILootItem& L : Loot)
			{
				if (L.Label == Thief.Target)
				{
					Back = L.Node;
				}
			}
			// A SEARCH target is a room NAME, not a loot label, and the loop above only
			// reads labels -- so a searcher who got stuck was rerouted to the frontHub
			// default instead of the room he was walking to. That is how the 2026-10-15
			// freeze started: frontHub, last waypoint popped, target unchanged, and (until
			// the self-heal above existed) no route ever rebuilt. Labels (TV, Safe, Laptop)
			// and node names (bedroom, study, ...) cannot collide, so both lookups are safe
			// in either order; loot first because that is the older contract.
			if (NodeNames().Contains(Thief.Target))
			{
				Back = Thief.Target;
			}
		}
		Thief.Route = RouteTo(NearestNode(TPos), Back);
		FHIEvent Ev;
		Ev.Type = TEXT("reroute");
		Events.Add(Ev);
	}

	// ---- aim, then shoot. Sight is recomputed AFTER moving: he can step behind cover
	// during this same step, and firing through a wall would be a bug.
	const bool bCan = Player.bAlive && ThiefSees(TPos, Thief.Yaw, PPos);
	if (bCan)
	{
		Thief.Aim = FMath::Min(Thief.Aim + Dt, ThiefAimTime);
	}
	else
	{
		Thief.Aim = 0.0f;
	}

	// He cannot fire with his hands full -- one hand is on the loot, which is the whole
	// point of the trip. A deliberate exception, and it is what makes the loot his
	// vulnerability. `can` still means line of sight, not permission to shoot.
	const bool bMayFire = bCan && Thief.Carry.IsEmpty() && Thief.Aim >= ThiefAimTime;
	if (bMayFire && Thief.Reloading <= 0.0f && Thief.Cd <= 0.0f)
	{
		if (Thief.Mag <= 0)
		{
			Thief.Reloading = ThiefReload;
			Thief.Mag = ThiefMag;
		}
		else
		{
			Thief.Mag -= 1;
			Thief.Cd = ThiefFireHunting;
			FHIEvent Ev;
			Ev.Type = TEXT("thiefShot");
			Ev.Spread = 0.10f;
			Ev.Damage = ThiefDamage;
			Events.Add(Ev);
		}
	}
	return bCan;
}

// ---------------------------------------------------------------------- self check

void FHISim::SelfCheck()
{
	checkf(Walls().Num() == 7, TEXT("expected 7 walls, got %d"), Walls().Num());
	checkf(House.Min.Equals(FVector2D(-8.0f, -19.0f)), TEXT("house min wrong"));
	checkf(House.Max.Equals(FVector2D(8.0f, -8.0f)), TEXT("house max wrong"));
	checkf(NodeNames().Num() == 9, TEXT("expected 9 nav nodes, got %d"), NodeNames().Num());
	checkf(NodePositions().Num() == NodeNames().Num(), TEXT("nav node pos/name mismatch"));
	checkf(NodeEdges().Num() == NodeNames().Num(), TEXT("nav node edge/name mismatch"));
	checkf(LootPositions().Num() == 3, TEXT("expected 3 loot spots"));
	checkf(LootLabels().Num() == 3 && LootNodes().Num() == 3, TEXT("loot array mismatch"));

	// Every loot spot's node must be in SEARCH_NODES or the item can never be discovered
	// and the round cannot be completed. The Godot suite checks the same thing.
	for (int32 Idx = 0; Idx < LootNodes().Num(); ++Idx)
	{
		checkf(SearchNodes().Contains(LootNodes()[Idx]),
			TEXT("loot '%s' sits in node '%s', which is not searchable"),
			*LootLabels()[Idx], *LootNodes()[Idx]);
	}
}
