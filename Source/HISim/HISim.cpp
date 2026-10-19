#include "HISim.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, HISim);

// Definitions for the non-constexpr statics.
// PHASE 2 (2026-10-15): the house becomes Richard's three-floor design. House is the
// OUTER shell, walls built inward from its edges exactly as in the slice -- so the
// interior faces land on the doc's room coordinates (x +-6.10, y -17.14 / -8.00), and
// HOUSE-DESIGN.md §2's "footprint 12.2 x 9.14" is that interior programme rect, 0.25
// inside the shell on every side but the south, whose wall band sits inside the rect.
// As-built doc correction, recorded until §2/§4.1 catch up.
// Bounds stay: the yard grows around the smaller footprint rather than the world
// shrinking (the doc said "recomputed"; this is the decision, recorded here).
const FBox2D FHISim::House(FVector2D(-6.35f, -17.39f), FVector2D(6.35f, -7.75f));
const FVector2D FHISim::DoorGap(-1.0f, 1.0f);
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
		// 0.15 -> 0.1125, ~25% tighter (Richard, 2026-10-19): "the shotgun spread is a little
		// bit wide, bring it in by about 25%."
		W.Spread = 0.1125f;   // radians
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
	// SHE DOES NOT START IN THE DOORWAY -- and the phase-2 design doc's own §4.6 number
	// ((0.3, -9.8), the entry hall) quietly violated that, so it is corrected here before
	// it ever shipped. The ruling is Richard's (2026-10-15, when the slice moved her off
	// the Godot spawn for exactly this reason): the opening is brutal because "the fender
	// ... is standing in the door way while the intruders are coming at her. I think she
	// should be more inside the building."
	//
	// The geometry, re-derived for the new house: the Drop is (0, 4.5), the door gap is
	// x -1..1 in the south wall, and from (0.3, -9.8) the sight line to the Drop crosses
	// the wall band at x 0.26 -- inside the gap. The whole entry hall within |x| < 1.16
	// of the door axis is one spawn-kill cone. So she starts in the LIVING ROOM at
	// (-2.0, -10.6): the Drop line now crosses the south wall at x -1.64, west of the
	// gap, i.e. buried in wall -- no one can see her until they come inside -- and she
	// still commands the entry hall through the living-room doorway (her sight line to
	// (0.3, -9.5) crosses the x -1.2 partition at y -10.22, inside the opening).
	// Defender by position, not by script. She faces the doorway (yaw 0.45).
	P.Pos = FVector2D(-2.0f, -10.6f);
	P.Floor = 0;
	P.Z = FloorHeight(0);
	P.Yaw = 0.45f;
	P.Pitch = 0.0f;
	P.Hp = PlayerMaxHp;
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
	// 3-D distance, and the floors take care of themselves: a man standing under you on
	// the ground floor is 2.75 m away through the slab, further than any arm, so no
	// special floor gate is needed -- and a shove on the stairs stays honest, because
	// treads 1.4 m apart in height are 2 m apart in space.
	const float Dist = FMath::Sqrt(To.SizeSquared()
		+ FMath::Square(Thief.Z - Player.Z));
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
	Thief.Pos = ResolveCircleZ(Thief.Pos + Dir * ShovePush, ThiefRadius, Thief.Z);
	// A shove can land him on the stairs or back down them; elevation is re-derived
	// from wherever he ended up, never carried from before the push.
	ApplyElevation(Thief.Pos, Thief.Floor, Thief.Z);
	Thief.Route.Empty();
	// He has to stoop for whatever he just dropped, or he simply picks it straight back up.
	Thief.Regrab = RegrabTime;

	// Read the label BEFORE the drop clears it, because the event reports what was lost.
	const FString Dropped = Thief.Carry;
	DropCarried(Thief, Loot, Events);
	FHIEvent Ev;
	Ev.Type = TEXT("shoved");
	Ev.Id = Thief.Id;   // so the actor layer can voice the man who was shoved, not the player
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
			L.Floor = Thief.Floor;
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
	// 3-D, for the same reason as the homeowner's shove: through a slab is out of reach.
	const float Dist = FMath::Sqrt(To.SizeSquared()
		+ FMath::Square(Player.Z - Thief.Z));
	if (Dist > ThiefShoveRange || Dist < 0.001f)
	{
		return false;
	}
	Thief.ShoveCd = ThiefShoveCd;
	Player.Stagger = FMath::Max(Player.Stagger, ThiefShoveStagger);
	Player.Pos = ResolveCircleZ(Player.Pos + To / Dist * ThiefShovePush, PlayerRadius, Player.Z);
	ApplyElevation(Player.Pos, Player.Floor, Player.Z);
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

	TArray<float> Zs;
	Zs.Reserve(Intruders.Num() + 1);
	Zs.Add(Player.Z);
	for (const FHIThief& T : Intruders)
	{
		Zs.Add(T.Z);
	}

	const TArray<FVector2D> After = SeparateAllZ(Stepped, Radii, Zs);
	Player.Pos = After[0];
	ApplyElevation(Player.Pos, Player.Floor, Player.Z);
	for (int32 Idx = 0; Idx < Intruders.Num(); ++Idx)
	{
		FHIThief& T = Intruders[Idx];
		T.Pos = After[Idx + 1];
		ApplyElevation(T.Pos, T.Floor, T.Z);
		// `prev` is re-anchored on the FRAME-START position, not the resolved one, so the
		// stuck detector measures the whole frame's travel -- the step plus whatever the
		// body-push added. `StepThief` already wrote exactly this value; assigning it again
		// from the list is what makes that explicit rather than incidental.
		T.Prev = AtFrameStart[Idx + 1];
		// ---- and now the group path can SEE a jam (2026-10-18). The net travel is walk
		// plus crowd push-back together; StepThief's own accounting ran before the push-back
		// existed and measured only the walk. Dead or escaped bodies never reached the solo
		// detector (StepThief returns early), so they do not reach this one either.
		if (T.bAlive && !T.bEscaped)
		{
			const int32 Mark = Events.Num();
			StuckStep(T, FVector2D::Distance(After[Idx + 1], AtFrameStart[Idx + 1]), Dt, Loot, Events);
			for (int32 EvIdx = Mark; EvIdx < Events.Num(); ++EvIdx)
			{
				Events[EvIdx].Id = T.Id;
			}
		}
	}
	return Seen;
}

TArray<FVector2D> FHISim::SeparateBodiesZ(const FVector2D& PlayerPos, float PlayerZ,
	const FVector2D& ThiefPos, float ThiefZ)
{
	// Stairs are single file: a pair with either body inside a stair band is left exactly
	// where it stands. A push sideways on a flight is a push through a stringer or over a
	// railing, and this is the one place the no-overlap contract yields to architecture --
	// two men on one flight overlap in plan by design.
	if (InStairBand(PlayerPos) || InStairBand(ThiefPos))
	{
		return { PlayerPos, ThiefPos };
	}
	const float MinDist = PlayerRadius + ThiefRadius;
	const FVector2D D = ThiefPos - PlayerPos;
	const float Dist = D.Size();
	// Different storeys cannot touch: body cylinders are BodyHeight tall, and no two
	// floors in this house are less than 2.75 apart outside a band.
	if (Dist >= MinDist || FMath::Abs(PlayerZ - ThiefZ) >= BodyHeight)
	{
		return { PlayerPos, ThiefPos };
	}
	// THE HOMEOWNER YIELDS -- the load-bearing choice, kept from SeparateBodies.
	const FVector2D N = (Dist < 1e-5f) ? FVector2D(1.0f, 0.0f) : D / Dist;

	FVector2D T = ThiefPos;
	FVector2D P = ClampToWorld(
		ResolveCircleZ(PlayerPos - N * (MinDist - Dist), PlayerRadius, PlayerZ), PlayerRadius);

	const FVector2D Gap = T - P;
	const float GapLen = Gap.Size();
	if (GapLen < MinDist - 1e-4f)
	{
		// he is against something immovable, so the remainder comes out of the intruder
		const FVector2D BN = (GapLen < 1e-5f) ? N : Gap / GapLen;
		T = ClampToWorld(ResolveCircleZ(P + BN * MinDist, ThiefRadius, ThiefZ), ThiefRadius);
	}
	return { P, T };
}

TArray<FVector2D> FHISim::SeparateAllZ(const TArray<FVector2D>& Positions,
	const TArray<float>& Radii, const TArray<float>& Zs)
{
	constexpr int32 PairSweeps = 3;
	TArray<FVector2D> Out = Positions;
	const int32 N = Out.Num();
	if (N < 2)
	{
		return Out;
	}
	// Stair membership is sampled ONCE, at the frame's positions: separation must not
	// decide who is on the stairs from positions separation itself is moving.
	TArray<bool> OnStairs;
	OnStairs.SetNum(N);
	for (int32 Idx = 0; Idx < N; ++Idx)
	{
		OnStairs[Idx] = InStairBand(Positions[Idx]);
	}

	for (int32 Sweep = 0; Sweep < PairSweeps; ++Sweep)
	{
		bool bMoved = false;
		for (int32 Idx = 0; Idx < N; ++Idx)
		{
			for (int32 Jdx = Idx + 1; Jdx < N; ++Jdx)
			{
				if (OnStairs[Idx] || OnStairs[Jdx])
				{
					continue;
				}
				// Different storeys cannot touch -- body cylinders are BodyHeight tall.
				if (FMath::Abs(Zs[Idx] - Zs[Jdx]) >= BodyHeight)
				{
					continue;
				}
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
					Out[0] = ClampToWorld(ResolveCircleZ(Out[0] - Dir * Push, Radii[0], Zs[0]), Radii[0]);
					PJ = ClampToWorld(ResolveCircleZ(Out[Jdx], Radii[Jdx], Zs[Jdx]), Radii[Jdx]);
					Out[Jdx] = PJ;
				}
				else
				{
					// two intruders: split it evenly so neither is privileged
					const float Half = Push * 0.5f;
					Out[Idx] = ClampToWorld(ResolveCircleZ(PosI - Dir * Half, Radii[Idx], Zs[Idx]), Radii[Idx]);
					Out[Jdx] = ClampToWorld(ResolveCircleZ(PJ + Dir * Half, Radii[Jdx], Zs[Jdx]), Radii[Jdx]);
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
		// 3-D: the shove it feeds gates on 3-D range, so a man on the storey above
		// must not WIN the selection and then fail the shove while an adjacent one waits.
		const float Dist = FVector::Dist(
			FVector(Player.Pos.X, Player.Pos.Y, Player.Z),
			FVector(T.Pos.X, T.Pos.Y, T.Z));
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
				L.Floor = Who->Floor;
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
		// Stairs cost pace -- everyone climbs at StairPace of their walk, sprint
		// included. Checked at his START position, so the slowdown begins the frame he
		// commits to the flight.
		if (InStairBand(Player.Pos))
		{
			Wish *= StairPace;
		}
	}

	// He carries momentum between frames instead of being assigned a velocity. This is the
	// whole reason he stopped reading as a sliding prop: previously he was at full speed on
	// the first frame of a keypress and stationary on the frame after release.
	FVector2D V = MoveToward(Player.Vel, Wish,
		(Wish != FVector2D::ZeroVector ? PlayerAccel : PlayerDecel) * Dt);
	if (V.Size() > 0.02f)
	{
		FVector2D P = Player.Pos + V * Dt;
		// Z-aware: the same push-out, but each solid only counts when his body cylinder
		// [Z, Z+1.8] strictly overlaps its span -- so the ground-floor fridge cannot
		// shove a man on the second floor, and the stringer wall catches a climber.
		P = ResolveCircleZ(P, PlayerRadius, Player.Z);
		P = ClampToWorld(P, PlayerRadius);
		// A wall has to STOP him, not let momentum pile up against it.
		const FVector2D Moved = P - Player.Pos;
		if (Moved.Size() < V.Size() * Dt * 0.5f)
		{
			V = Moved / FMath::Max(Dt, 1e-4f);
		}
		Player.Pos = P;
		// Elevation is DERIVED, never integrated: wherever the step landed him is where
		// the floor is, and the floor flips at the band's midpoint. One function owns
		// that truth for every body in the sim.
		ApplyElevation(Player.Pos, Player.Floor, Player.Z);
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
	const TArray<int32>& Floors = LootFloors();
	for (int32 Idx = 0; Idx < Poses.Num(); ++Idx)
	{
		FHILootItem L;
		L.Pos = Poses[Idx];
		L.Label = Labels[Idx];
		L.Node = Nodes[Idx];
		L.Floor = Floors[Idx];
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

const TArray<FHISim::FHIWallRow>& FHISim::WallRows()
{
	static const TArray<FHIWallRow> W = []()
	{
		const float T = WallThickness;          // 0.25 exterior
		const float I = 0.12f;                  // interior partitions
		const float H = I * 0.5f;               // half of it, for centre-line layouts
		const FBox2D Ho = House;                // OUTER faces: (-6.35, -17.39)..(6.35, -7.75)
		// Interior faces are the doc's room coordinates: x +-6.10, y -17.14 / -8.00.
		// HOUSE-DESIGN.md §2 calls x -6.1..6.1, y -17.14..-8.0 the "footprint" -- that is
		// the INTERIOR programme rect; the shell is 0.25 larger on the west, east and
		// north and the south wall band (-8.25..-8.0) sits inside it. As-built correction
		// for §4.1, recorded here until the doc catches up.
		const float NX = Ho.Min.X;              // -6.35
		const float XX = Ho.Max.X;              //  6.35
		const float NY = Ho.Min.Y;              // -17.39
		const float SY = Ho.Max.Y;              // -7.75
		const float NIf = NY + T;               // -17.14 north interior face
		const float SIf = SY - T;               // -8.00 south interior face
		const float WIf = NX + T;               // -6.10
		const float EIf = XX - T;               //  6.10
		const float D = SY - NY;                // full north-south depth for side walls
		TArray<FHIWallRow> Out;
		auto R = [](float X, float Y, float Wd, float Dp, int32 Floor = 0, float TopZ = 0.0f, float MinZ = 0.0f)
		{
			FHIWallRow Row;
			Row.Rect = FBox2D(FVector2D(X, Y), FVector2D(X + Wd, Y + Dp));
			Row.Floor = Floor;
			Row.TopZ = TopZ;
			Row.MinZ = MinZ;
			return Row;
		};
		// ---- EXTERIOR, two rows per side: a basement run (Floor -1, default top =
		// FloorHeight(0) = 0, the deck underside) and an above-grade run (Floor 0,
		// TopZ = RoofTop -- one span 0..5.33 through both upper storeys; splitting it at
		// 2.74 would buy nothing and double the rows). The front door gap is the GROUND
		// floor's business only -- below grade the south wall is unbroken.
		Out.Add(R(NX, NY, XX - NX, T, -1));                    // north, below grade
		Out.Add(R(NX, NY, XX - NX, T, 0, RoofTop));            // north
		Out.Add(R(NX, SY - T, XX - NX, T, -1));                // south, below grade (full)
		Out.Add(R(NX, SY - T, DoorGap.X - NX, T, 0, RoofTop)); // south-west of the door
		Out.Add(R(DoorGap.Y, SY - T, XX - DoorGap.Y, T, 0, RoofTop)); // south-east of the door
		// THE TRANSOM WALL over the door (phase-2 ADDITION, veto-able): the Godot
		// original's 2-D door split left the gap open from lintel to eaves, and the slice
		// honoured that with a decorative panel -- tolerable at a 0.9 m letterbox, but
		// this facade runs to 5.33, and HIHouse's own invariant forbids decoration that
		// lies about geometry (a drawn wall a pellet flies through). So the facade above
		// the door head is a real sim row: sill at 2.10, top at the eaves. Nothing below
		// 2.10 changes -- a walking cylinder [0,1.8] and every eye at 1.62 pass under it
		// untouched -- and Walls() excludes MinZ>0 rows, so the legacy 2-D projection
		// keeps its open doorway exactly as the original described it.
		Out.Add(R(DoorGap.X, SY - T, DoorGap.Y - DoorGap.X, T, 0, RoofTop, 2.10f));
		Out.Add(R(NX, NY, T, D, -1));                          // west, below grade
		Out.Add(R(NX, NY, T, D, 0, RoofTop));                  // west
		Out.Add(R(XX - T, NY, T, D, -1));                      // east, below grade
		Out.Add(R(XX - T, NY, T, D, 0, RoofTop));              // east

		// ---- INTERIOR PARTITIONS, 0.12 thick on the doc's centre-lines (HOUSE-DESIGN.md
		// §4.1-§4.3). Each door/opening splits its wall into literal segments -- the
		// slice's Walls() gap-splitting retires with this rewrite. Ground rows default to
		// TopZ = FloorHeight(1) = 2.74, second rows to RoofTop, basement rows to 0 -- see
		// WorldSolids for the span rule.
		const float WX = -1.20f;   // living|hall / master|hall / bedrooms|up-hall centre-line
		const float KX = 1.83f;    // hall | kitchen+dining (ground)
		const float OX = 2.00f;    // up-hall | linen+office (second)
		const float BX = 3.60f;    // linen | bath (second)
		const float MX = 3.00f;    // open area | mechanical (basement)
		const float LM = -13.00f;  // living | master (ground)
		const float BB = -12.40f;  // bedroom 2 | bedroom 3 (second)
		const float OB = -13.40f;  // office | linen+bath (second)
		const float MB = -14.90f;  // master | master bath (ground)
		const float MBX = -3.90f;  // master bath west wall (ground)
		const float MY = -13.00f;  // mechanical room south wall (basement)

		// -- ground floor (Floor 0)
		// x -1.2: living-room opening y -10.0..-11.2, master door y -13.6..-14.8.
		// R()'s Y argument is the rect's MINIMUM y -- the NORTH end of a segment -- and
		// Dp runs south from it. WRONG GUESS RECORDED (2026-10-18): the twelve vertical
		// partition rows below were first written passing each segment's SOUTH end, so
		// every rect landed mirrored to the wrong side of its intended span (the row
		// commented "y -11.20..-13.60" actually built -11.20..-8.80). Nothing else
		// caught it -- counts, standability, furniture discipline all passed -- until
		// SelfCheck's edge-walk probe blocked entryHall->living across the phantom wall.
		Out.Add(R(WX - H, -10.00f, I, 2.00f, 0));               // y -10.00..-8.00
		Out.Add(R(WX - H, -13.60f, I, 2.40f, 0));               // y -13.60..-11.20
		Out.Add(R(WX - H, NIf, I, 2.34f, 0));                   // y -17.14..-14.80
		// y -13: living | master, solid.
		Out.Add(R(WIf, LM - H, WX - WIf, I, 0));               // x -6.10..-1.20
		// x 1.83: entry<->dining opening y -10.40..-8.75, hall<->kitchen y -13.80..-12.20.
		// AS-BUILT 2026-10-18, both openings widened (were -10.4..-9.2 and -13.6..-12.4):
		// a radius-0.42 body walking the entryHall->dining graph line crosses this wall at
		// y -9.34, and its 0.84-wide envelope [-9.76,-8.92] caught the old -9.20 south
		// jamb by 0.28 m. Worse, a thief rerouting from off-node (1.35,-9.15) -- exactly
		// the west face minus a body radius -- aimed within 0.04 rad of perpendicular at
		// that jamb, where the wall-slide tangent (0,0.04) falls under the 0.0025
		// deadzone: a full stop the stuck->reroute loop reproduces byte-identically
		// forever (measured: frozen 133 s at (1.35,-9.15), reroute every 1.2 s,
		// TraceProbe section B; it is the exact end position of the old MovingHeist and
		// ThiefAI failures). The new south edge is flush with the coat closet's north end
		// and leaves the graph line's body envelope 0.174 m of clearance (0.636 north);
		// the closet's NE corner stays 0.17 off that walking line. The kitchen opening
		// gets the same treatment: the hallSouth->kitchen line crosses at y -13.14 with
		// only 0.045 m of envelope margin against the old -13.60 jamb -- the knife-edge
		// class the br3 door widening (second-floor note below) was authored against --
		// now 0.245/0.515. NO wall at y -12.2: kitchen and dining are one open space --
		// the programme leaves the front-east quarter unassigned and a real house runs
		// them together (the dining ADDITION in §4.1, veto-able like the rest).
		Out.Add(R(KX - H, -8.75f, I, 0.75f, 0));                // y -8.75..-8.00
		Out.Add(R(KX - H, -12.20f, I, 1.80f, 0));               // y -12.20..-10.40
		Out.Add(R(KX - H, NIf, I, 3.34f, 0));                   // y -17.14..-13.80
		// Master bath, north-west corner of the master: y -14.9 wall with a door at
		// x -3.4..-2.5, plus the bath's west wall.
		Out.Add(R(MBX, MB - H, 0.50f, I, 0));                  // x -3.90..-3.40
		Out.Add(R(-2.50f, MB - H, 1.24f, I, 0));               // x -2.50..-1.26
		Out.Add(R(MBX - H, NIf, I, MB - NIf, 0));              // y -17.14..-14.90

		// -- second floor (Floor 1)
		// x -1.2: bedroom 2 door y -10.0..-11.2, bedroom 3 door y -14.2..-15.6. The br3
		// opening grew 0.2 m north at authoring time: the upHallN->br3 walk line crosses
		// this wall at y -15.27, and a -15.4 opening edge would have left it 0.06 m of
		// margin -- knife edges on nav lines are how the slice's freezes were born.
		Out.Add(R(WX - H, -10.00f, I, 2.00f, 1));               // y -10.00..-8.00
		Out.Add(R(WX - H, -14.20f, I, 3.00f, 1));               // y -14.20..-11.20
		Out.Add(R(WX - H, NIf, I, 1.54f, 1));                   // y -17.14..-15.60
		// y -12.4: bedroom 2 | bedroom 3, solid.
		Out.Add(R(WIf, BB - H, WX - WIf, I, 1));               // x -6.10..-1.20
		// x 2.0: office opening y -10.0..-12.0, linen door y -14.0..-15.2.
		Out.Add(R(OX - H, -10.00f, I, 2.00f, 1));               // y -10.00..-8.00
		Out.Add(R(OX - H, -14.00f, I, 2.00f, 1));               // y -14.00..-12.00
		Out.Add(R(OX - H, NIf, I, 1.94f, 1));                   // y -17.14..-15.20
		// x 3.6: linen | bath, solid. y -13.4: office | linen+bath with the BATH DOOR at
		// x 4.3..5.5 -- the bath opens off the office, not through the linen closet.
		// (The design note that put the bath door in the x 3.6 wall would have made the
		// closet the bath's only hallway; recorded as corrected here.)
		// WRONG GUESS RECORDED (2026-10-18, the survivor of the mirrored-rows fix): this
		// row first read R(BX-H, OB, I, NIf-OB, 1) -- the same two corners, but with Y and
		// Dp swapped, so the rect came out INVERTED (Min.Y -13.40 > Max.Y -17.14). FBox2D's
		// two-point constructor does not normalise, and every query answers "empty" for an
		// inverted box: the wall was a ghost that blocked nothing. It slipped past SelfCheck
		// (no nav edge crosses it -- the bath hangs off the office, the linen off upHallN)
		// and past the furniture-overlap probe (an inverted rect never overlaps). The
		// linen|bath sight pin in CrossFloorSight and its WalkBlocked twin in NodeFloors
		// are the checks that fail if it ever goes ghost again.
		Out.Add(R(BX - H, NIf, I, OB - NIf, 1));               // y -17.14..-13.40
		Out.Add(R(OX - H, OB - H, 2.36f, I, 1));               // x 1.94..4.30
		Out.Add(R(5.50f, OB - H, EIf - 5.50f, I, 1));          // x 5.50..6.10

		// -- basement (Floor -1): the mechanical room only; everything else is Richard's
		// open cinderblock area (§4.2).
		Out.Add(R(MX - H, NIf, I, MY - NIf, -1));              // x 3.0 wall, y -17.14..-13.00
		Out.Add(R(4.05f, MY - H, EIf - 4.05f, I, -1));         // y -13.0 wall, x 4.05..6.10
		//   (door gap x 3.0..4.05; §4.2 asked for 0.9 m, but the x 3.0 wall's band eats
		//   0.06 of that, leaving EXACTLY the 0.84 m body diameter -- zero clearance per
		//   side, the pinch class the slice measured as a 72 s freeze. 1.05 m centre-to-
		//   centre leaves 0.075 per side; recorded as a correction to the doc.)
		return Out;
	}();
	return W;
}

const TArray<FBox2D>& FHISim::Walls()
{
	// GROUND FLOOR ONLY, and deliberately: every 2-D query in the game -- the minimap's
	// rays, the legacy LosBlocked, the old ResolveCircle and every test written against
	// them -- means "the floor you stand on", which until the fork was the only floor.
	// Floor-aware work reads WallRows()/WorldSolids(). PHASE 2: rows are authored as
	// literal per-segment runs (each door/opening splits its wall at authoring time), so
	// this is a pure Floor==0 filter -- no gap-splitting arithmetic -- with one carve-out:
	// rows that start above their floor (MinZ > 0, the door transom) are 3-D structure
	// only and must NOT plug the 2-D doorway the projection has carried since Godot.
	// The ground count is 15: five exterior runs (south split by DoorGap) plus ten
	// partitions. The slice's stair stringer retired -- the stacked stairwell is enclosed
	// by the x -1.2 partition and the east screen (WorldSolids), not by a stringer wall.
	static const TArray<FBox2D> W = []()
	{
		TArray<FBox2D> Out;
		for (const FHIWallRow& Row : WallRows())
		{
			if (Row.Floor == 0 && Row.MinZ <= 0.0f)
			{
				Out.Add(Row.Rect);
			}
		}
		return Out;
	}();
	return W;
}

namespace
{
	/** One furniture item: min corner (X, Z), size (W, D), height H, and -- phase 2 --
	 * the floor it stands on. Rect is (X, Z)..(X+W, Z+D); Z is the Y axis, named for the
	 * Godot literal this table was ported from. */
	struct FFurnRow
	{
		float X, Z, W, D;
		/**
		 * `h` -- the item's HEIGHT.
		 *
		 * This field was DROPPED by the first port of the Godot table, and its absence is
		 * the whole reason every couch and table in the game rendered as a floor-to-ceiling
		 * column: `game.gd:483` draws with `_add_box(body, f["rect"], f["h"])`. The lesson
		 * outlives the table it was learned from (the slice's Godot rows were replaced
		 * wholesale by the phase-2 house): heights are DATA, every row carries one.
		 *
		 * The sim's SIGHT model still ignores them: furniture spans a full storey in
		 * WorldSolids because Godot's own `los_blocked` was 2-D. Draw tall, see through --
		 * that mismatch is REALISM-BACKLOG item 18, and it is the original's, not ours.
		 */
		float H;
		bool bFlat;
		/** The sim's own `kind`, so the game layer can name what it draws. */
		const TCHAR* Label;
		/** Floor this item stands on; its base is FloorHeight(Floor). */
		int32 Floor;
	};
	const TArray<FFurnRow>& FurnitureRows()
	{
		// PHASE 2 (HOUSE-DESIGN.md §4.1-§4.3): the slice's Godot-literal rows are gone --
		// new house, new furniture. Richard's fixtures (furnace, water heater) keep the
		// doc's exact rectangles; everything else is free choice within his rooms, with
		// nav-node and door clearances derived row by row (WorldData/SelfCheck assert
		// furniture never overlaps a wall and every nav edge is a clear straight line, so
		// an authoring slip fails the build, not the player).
		static const TArray<FFurnRow> Rows = {
			// ---- BASEMENT (floor -1). Doc §4.2: the mechanical room's fixtures at their
			// doc rectangles; the open area gets the lally columns, west shelving and the
			// north workbench.
			{  4.70f, -16.60f, 0.75f, 0.75f, 1.40f, false, TEXT("furnace"), -1 },
			{  3.50f, -16.60f, 0.60f, 0.60f, 1.50f, false, TEXT("water_heater"), -1 },
			// AS-BUILT 2026-10-18: the doc's "side by side at y -14.2..-13.5" became a
			// north-south stack 1.0 east (the washer keeps the doc's y band; the dryer
			// sits south of it, both 0.10 off the east wall face at 6.10). The
			// furniture-envelope probe measured the baseDoor->utility walk line
			// (2.3x + y + 4.65 = 0) passing 0.1356 from the washer's NW corner at the
			// old spot -- a body-radius clip on the one route into the utility room.
			// No side-by-side row survives there: west of the line the room is the
			// column band, east of it the pair would need x 4.76..6.20 against a wall
			// face at 6.10. Stacked at x 5.30..6.00 the line clears all four corners by
			// >= 0.733 (envelope margin >= 0.31), the utility node stands 0.80 west of
			// the dryer, and the furnace keeps 0.85 to the south. ADDITION, veto-able,
			// as the doc says -- HOUSE-DESIGN.md §4.2 to carry the correction.
			{  5.30f, -14.20f, 0.70f, 0.70f, 1.00f, false, TEXT("washer"), -1 },
			{  5.30f, -15.00f, 0.70f, 0.70f, 1.00f, false, TEXT("dryer"), -1 },
			{ -3.125f, -12.725f, 0.25f, 0.25f, 2.29f, false, TEXT("lally_column"), -1 },
			{ -3.125f, -15.525f, 0.25f, 0.25f, 2.29f, false, TEXT("lally_column"), -1 }, // ADDITION (2nd), veto-able
			{ -6.10f, -13.50f, 0.40f, 2.40f, 1.80f, false, TEXT("shelving"), -1 },
			{ -5.50f, -17.14f, 1.80f, 0.60f, 0.90f, false, TEXT("workbench"), -1 },
			// ---- GROUND, living room (front west)
			{ -5.95f, -11.60f, 0.90f, 2.40f, 0.80f, false, TEXT("sofa"), 0 },
			{ -5.00f, -11.30f, 1.00f, 1.40f, 0.42f, false, TEXT("coffee_table"), 0 },
			{ -4.50f, -12.94f, 2.00f, 0.45f, 0.50f, false, TEXT("tv_stand"), 0 },
			{ -5.20f, -11.60f, 3.00f, 2.20f, 0.02f, true,  TEXT("rug"), 0 },
			{ -5.90f,  -8.90f, 0.40f, 0.40f, 1.60f, false, TEXT("lamp"), 0 },
			{ -2.40f,  -9.60f, 0.85f, 0.85f, 0.75f, false, TEXT("armchair"), 0 },
			// ---- GROUND, entry hall: the coat closet only; the hall and the stairwell
			// stay clear -- they are the whole game's foot traffic.
			{  1.22f,  -8.75f, 0.55f, 0.70f, 2.10f, false, TEXT("coat_closet"), 0 },
			// ---- GROUND, master (back west) + master bath
			{ -6.10f, -16.80f, 2.00f, 2.00f, 0.55f, false, TEXT("bed"), 0 },
			{ -5.90f, -14.70f, 0.50f, 0.50f, 0.55f, false, TEXT("nightstand"), 0 },
			{ -3.20f, -13.56f, 1.80f, 0.50f, 2.00f, false, TEXT("wardrobe"), 0 },
			{ -5.70f, -14.65f, 2.40f, 1.30f, 0.02f, true,  TEXT("rug"), 0 },
			{ -3.80f, -17.14f, 1.65f, 0.80f, 0.60f, false, TEXT("tub"), 0 },
			{ -1.75f, -17.14f, 0.45f, 0.60f, 0.45f, false, TEXT("toilet"), 0 },
			{ -1.76f, -16.30f, 0.50f, 1.20f, 0.85f, false, TEXT("vanity"), 0 },
			// ---- GROUND, kitchen (back east): L-counter north + east, fridge, island.
			{  2.40f, -17.14f, 2.10f, 0.65f, 0.92f, false, TEXT("counter"), 0 },
			{  4.60f, -17.14f, 0.85f, 0.75f, 1.85f, false, TEXT("fridge"), 0 },
			{  5.45f, -16.49f, 0.65f, 2.60f, 0.92f, false, TEXT("counter"), 0 },
			{  3.30f, -15.40f, 1.80f, 0.90f, 0.92f, false, TEXT("island"), 0 },
			// ---- GROUND, dining (front east -- the ADDITION, veto-able)
			// AS-BUILT 2026-10-18: the set moved 0.50 west and its north pair 0.50 north
			// (chairs pushed in, 0.10 off the table). The dining->kitchen walk line passed
			// 0.186 m east of the table's NE corner -- inside the 0.42 body envelope -- and
			// a rerouting thief froze at the corner-snap equilibrium (4.60,-9.98) bearing
			// due south into it: measured, TraceProbe B, 128 s of 1.2 s reroute cycles, the
			// end position of MovingHeist/ThiefAI/GroupHeist alike. The entryHall->dining
			// line likewise passed 0.17 south of the north chairs. Clearances now, measured
			// perpendicular: table NE 0.683 and SE 0.602 on dining->kitchen; chair SW 0.581
			// on entryHall->dining. The envelope rule is asserted for EVERY edge against
			// every non-flat row -- see SelfCheck's furniture discipline.
			{  2.60f, -11.40f, 1.60f, 1.00f, 0.75f, false, TEXT("dining_table"), 0 },
			{  2.50f, -11.95f, 0.45f, 0.45f, 0.90f, false, TEXT("chair"), 0 },
			{  3.15f, -11.95f, 0.45f, 0.45f, 0.90f, false, TEXT("chair"), 0 },
			{  2.50f, -10.30f, 0.45f, 0.45f, 0.90f, false, TEXT("chair"), 0 },
			{  3.15f, -10.30f, 0.45f, 0.45f, 0.90f, false, TEXT("chair"), 0 },
			{  5.50f,  -9.60f, 0.55f, 1.60f, 0.90f, false, TEXT("sideboard"), 0 },
			// ---- SECOND, bedroom 2 (front west)
			{ -6.10f, -11.80f, 2.00f, 2.20f, 0.55f, false, TEXT("bed"), 1 },
			{ -4.05f, -11.80f, 0.50f, 0.50f, 0.55f, false, TEXT("nightstand"), 1 },
			{ -5.90f,  -8.60f, 1.60f, 0.60f, 2.00f, false, TEXT("wardrobe"), 1 },
			{ -5.40f, -10.80f, 2.60f, 1.80f, 0.02f, true,  TEXT("rug"), 1 },
			// ---- SECOND, bedroom 3 (back west)
			{ -6.10f, -16.40f, 2.00f, 2.20f, 0.55f, false, TEXT("bed"), 1 },
			{ -4.05f, -16.40f, 0.50f, 0.50f, 0.55f, false, TEXT("nightstand"), 1 },
			{ -3.60f, -17.14f, 1.40f, 0.55f, 1.20f, false, TEXT("dresser"), 1 },
			{ -5.40f, -15.40f, 2.60f, 1.80f, 0.02f, true,  TEXT("rug"), 1 },
			// ---- SECOND, bath + linen (linen is the ADDITION, veto-able)
			{  4.40f, -17.14f, 1.60f, 0.75f, 0.60f, false, TEXT("tub"), 1 },
			{  3.75f, -17.14f, 0.45f, 0.60f, 0.45f, false, TEXT("toilet"), 1 },
			{  5.55f, -15.60f, 0.55f, 1.00f, 0.85f, false, TEXT("vanity"), 1 },
			// AS-BUILT 2026-10-18: shortened 2.40 -> 1.54 (south end -14.74 -> -15.60).
			// The upHallN->linen walk line (1.55x - 2.5y - 39.775 = 0) passed 0.323 from
			// the old SE corner (2.50,-14.74) -- a body-radius clip on the only route into
			// the closet, caught by the furniture-envelope probe. The linen node cannot
			// move out of this: it is squeezed between the y -13.4 row (body clearance
			// wants y <= -13.88) and the shelving envelope (at -13.85 the line still
			// passes 0.413 off the corner), and pulling upHallN south rotates its
			// stairTopG line to 0.046 off the screen's NW corner -- the very fight that
			// placed stairTopG at -15.50. With the south end at -15.60 the line clears
			// the new SE corner by 1.054 and the SW by 0.843 (envelope margins 0.63/0.42),
			// the inflated-rect test by 0.31+, and the node stands 1.63 off the corner.
			{  2.10f, -17.14f, 0.40f, 1.54f, 1.80f, false, TEXT("linen_shelving"), 1 },
			// ---- SECOND, office (front east): the laptop's desk, against the window wall.
			{  3.40f,  -8.70f, 1.80f, 0.70f, 0.75f, false, TEXT("desk"), 1 },
			// MOVED east 2026-10-18 (was x 4.05): dead on the office->laptop grab line the
			// thief stopped against it 1.73 m out -- past GrabRange 1.4, laptop unstealable.
			// East of the line the walking body clears it by 0.18, the bookshelf by 0.55 in y.
			{  4.90f,  -9.65f, 0.55f, 0.55f, 0.95f, false, TEXT("desk_chair"), 1 },
			{  5.55f, -12.40f, 0.55f, 2.20f, 2.00f, false, TEXT("bookshelf"), 1 },
			{  2.80f, -11.60f, 2.40f, 1.80f, 0.02f, true,  TEXT("rug"), 1 },
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

const TArray<int32>& FHISim::FurnitureFloors()
{
	// Phase 2: the floor each row stands on, index-aligned with FurnitureRects. The
	// builder places a row's base at FloorHeight(FurnitureFloors()[Idx]); the sight
	// model spans it to the next floor's height (WorldSolids).
	static const TArray<int32> R = []()
	{
		TArray<int32> Out;
		for (const FFurnRow& F : FurnitureRows())
		{
			Out.Add(F.Floor);
		}
		return Out;
	}();
	return R;
}

const TArray<FBox2D>& FHISim::Solids()
{
	// GROUND-FLOOR walls plus every non-flat GROUND furniture rect. Rugs are drawn, never
	// solid -- a rug that blocks a route would stall a heist for no reason a player could
	// see. Phase 2: same contract as Walls() -- "the floor you stand on" for the legacy
	// 2-D queries means floor 0, so basement and second-floor rows are filtered out here
	// exactly as their walls are filtered out of Walls().
	static const TArray<FBox2D> S = []()
	{
		TArray<FBox2D> Out = Walls();
		const TArray<FBox2D>& Furn = FurnitureRects();
		const TArray<bool>& Flat = FurnitureFlat();
		const TArray<int32>& Floors = FurnitureFloors();
		for (int32 Idx = 0; Idx < Furn.Num(); ++Idx)
		{
			if (!Flat[Idx] && Floors[Idx] == 0)
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

// ------------------------------------------------------------------ floors (2026-10-15 fork)
//
// The three-floor house's core machinery, specified in HOUSE-DESIGN.md §1: explicit
// Floor per body, height derived from position, stairs as elevation bands, slabs as
// spanned deck solids with the stairwell a literal hole.

const TArray<FHISim::FHISolid>& FHISim::WorldSolids()
{
	static const TArray<FHISolid> S = []()
	{
		TArray<FHISolid> Out;
		auto Add = [&Out](const FBox2D& R, const FHISolidSpan& Span, int32 Floor, bool bDeck)
		{
			FHISolid S;
			S.Rect = R;
			S.Span = Span;
			S.Floor = Floor;
			S.bDeck = bDeck;
			Out.Add(S);
		};
		// A storey-topping helper: walls and furniture span from their floor's height to
		// the surface above -- the next floor's height, or the roof over the top storey.
		// The top of a ground wall lands EXACTLY on SecondZ, and the strict span-overlap
		// rule means a second-floor body (cylinder from 2.74) is above that wall's
		// storey, not inside it -- the same contract the slice proved with [0, 3.0].
		auto StoreyTop = [](int32 Floor)
		{
			return Floor < 1 ? FloorHeight(Floor + 1) : RoofTop;
		};
		// Walls: each at its floor, each spanning its storey (TopZ overrides for the
		// exterior above-grade runs, which climb 0..RoofTop in one span; MinZ overrides
		// for the door transom, which starts at the 2.10 door head).
		for (const FHIWallRow& Row : WallRows())
		{
			FHISolidSpan Span;
			Span.MinZ = Row.MinZ > 0.0f ? Row.MinZ : FloorHeight(Row.Floor);
			Span.MaxZ = Row.TopZ > 0.0f ? Row.TopZ : StoreyTop(Row.Floor);
			Add(Row.Rect, Span, Row.Floor, false);
		}
		// Furniture: ALL floors now (the slice commented its floor-1 rows out; phase 2
		// authors them for real). Sight spans stay uniform-at-a-storey -- real heights
		// draw, storey heights block -- REALISM-BACKLOG item 18, Richard's call.
		{
			const TArray<FBox2D>& Furn = FurnitureRects();
			const TArray<bool>& Flat = FurnitureFlat();
			const TArray<int32>& Floors = FurnitureFloors();
			for (int32 Idx = 0; Idx < Furn.Num(); ++Idx)
			{
				if (!Flat[Idx])
				{
					FHISolidSpan Span;
					Span.MinZ = FloorHeight(Floors[Idx]);
					Span.MaxZ = StoreyTop(Floors[Idx]);
					Add(Furn[Idx], Span, Floors[Idx], false);
				}
			}
		}
		// ---- DECKS (HOUSE-DESIGN.md §4). Three horizontal structures, each a set of
		// rectangles around its stairwell hole. The shell rect is the House outer box;
		// the decks run under the exterior walls.
		const FBox2D Shell = House;
		// Ground deck [-0.15, 0] -- the basement's ceiling. Hole x -1.25..-0.15,
		// y -11.05..-14.74: its edges line up with what guards them -- west with the
		// x -1.2 partition (band -1.26..-1.14 covers the lip), east with the screen
		// below, south with the stair band's own Rect (treads clamp flush at h 0),
		// north with the B->G flight's head (flush at z 0). Every crossing of the hole
		// is a stair, so NO ground-level railing exists -- a fall in is only possible
		// by walking the flights, and the doc's [0, 1.0] railing (§4.2) is superseded:
		// recorded as an as-built correction.
		const FHISolidSpan GroundDeck{ FloorHeight(0) - SlabThickness, FloorHeight(0) };
		Add(FBox2D(FVector2D(Shell.Min.X, -11.05f), FVector2D(Shell.Max.X, Shell.Max.Y)), GroundDeck, 0, true);   // south
		Add(FBox2D(FVector2D(Shell.Min.X, Shell.Min.Y), FVector2D(Shell.Max.X, -14.74f)), GroundDeck, 0, true);   // north
		Add(FBox2D(FVector2D(Shell.Min.X, -14.74f), FVector2D(-1.25f, -11.05f)), GroundDeck, 0, true);           // west
		Add(FBox2D(FVector2D(-0.15f, -14.74f), FVector2D(Shell.Max.X, -11.05f)), GroundDeck, 0, true);           // east
		// Second-floor slab [2.59, 2.74]. Hole x -1.25..-0.15, y -12.2..-15.03: south
		// edge is the headroom limit (h(y) + 1.8 <= 2.59 while y >= -12.2), north edge
		// IS the flight's top riser -- ending the hole past it would leave a slab-free
		// lip where a second-floor body stands on nothing. West lip: the partition.
		// East lip: the screen (its span tops out above the railing height below).
		// South lip: a railing, next solid.
		const FHISolidSpan Slab{ SecondZ - SlabThickness, SecondZ };
		Add(FBox2D(FVector2D(Shell.Min.X, -12.20f), FVector2D(Shell.Max.X, Shell.Max.Y)), Slab, 1, true);        // south
		Add(FBox2D(FVector2D(Shell.Min.X, Shell.Min.Y), FVector2D(Shell.Max.X, -15.03f)), Slab, 1, true);        // north
		Add(FBox2D(FVector2D(Shell.Min.X, -15.03f), FVector2D(-1.25f, -12.20f)), Slab, 1, true);                 // west
		Add(FBox2D(FVector2D(-0.15f, -15.03f), FVector2D(Shell.Max.X, -12.20f)), Slab, 1, true);                 // east
		// Roof [5.18, 5.33]: a deck for collision, but its top is nobody's floor --
		// DeckCovers' MaxZ match excludes it. The gable is visual only (actor layer).
		Add(Shell, FHISolidSpan{ RoofBottom, RoofTop }, 1, true);
		// ---- THE STAIRWELL'S EAST SIDE, in two pieces (design catch, 2026-10-15):
		// * A LOW RAILING over y -11.05..-12.2, span [0, 1.0]: guards the deck hole's
		//   east lip at ground level while staying see-over -- eyes at 1.62 (and even
		//   crouched at 1.05) keep the hall-into-stairwell sight line the doc wants.
		// * A FULL SCREEN over y -12.2..-15.03, span [0, 3.74]: without it a body could
		//   walk laterally off the G->2 flight onto... nothing (the slab hole under it),
		//   and a second-floor body could stroll east-west across the slab hole's own
		//   east lip. 3.74 = SecondZ + 1.0, railing height above the slab. It stops at
		//   the flight's top riser (-15.03) where the slab takes over.
		// Both sit OUTSIDE the band's x range (-1.2..-0.2) by 0.05, which is also the
		// hole lips' width: a resolved body centre can never be over the hole except
		// on the treads themselves.
		Add(FBox2D(FVector2D(-0.20f, -12.20f), FVector2D(-0.15f, -11.05f)),
			FHISolidSpan{ 0.0f, 1.0f }, 0, false);
		Add(FBox2D(FVector2D(-0.20f, -15.03f), FVector2D(-0.15f, -12.20f)),
			FHISolidSpan{ 0.0f, SecondZ + 1.0f }, 0, false);
		// The slab hole's south lip railing [2.74, 3.79]: the one edge of the second
		// floor's hole not covered by a wall, the screen or the flight itself. This is
		// also the solid the ResolveCircleZ skip exists for -- a descending G->2 body
		// passes under it, and a railing that caught his cylinder tip would freeze him
		// mid-flight (the slice measured exactly that).
		Add(FBox2D(FVector2D(-1.25f, -12.25f), FVector2D(-0.15f, -12.20f)),
			FHISolidSpan{ SecondZ, SecondZ + 1.05f }, 1, false);
		return Out;
	}();
	return S;
}

const TArray<FHISim::FHIStairBand>& FHISim::Stairs()
{
	static const TArray<FHIStairBand> S = []()
	{
		// PHASE 2: TWO FLIGHTS STACKED IN ONE STAIRWELL (HOUSE-DESIGN.md §4.3), plan
		// x -1.2..-0.2 (1.0 m wide), both climbing north from a shared foot at y -11.1.
		//
		// G->2: 15 risers x 0.183 = 2.74 over a 14 x 0.281 = 3.93 run, y -11.1..-15.03.
		// The north end IS the band top (-15.03), not the doc's -15.1: ending past the
		// top riser would leave a slab-free lip beyond the flight where a second-floor
		// body stands on nothing (the same sliver class the slice recorded at 7.875).
		//
		// B->G: 14 risers x 0.174 = 2.44 over a 13 x 0.28 = 3.64 run, y -11.1..-14.74.
		// Its top is flush with the ground deck at z 0, and the deck's stairwell hole
		// ends at the same y (-14.74): a ground-floor body walking south off the deck
		// steps onto THIS head, never onto the G->2 treads 2.5 m up in the same plan
		// point. That pairing is what makes Z-nearest band selection not merely
		// convenient but required -- see BandNearestZ.
		//
		// Both Rects run 0.05 south of the bottom riser (to -11.05, the deck hole's
		// south edge); HeightAt clamps at Bottom, so the sliver reads as h = BottomH
		// and no strip exists where a body would stand on air between deck and band.
		FHIStairBand G2;
		G2.Rect = FBox2D(FVector2D(-1.20f, -15.03f), FVector2D(-0.20f, -11.05f));
		G2.Bottom = FVector2D(-0.70f, -11.10f);
		G2.Top = FVector2D(-0.70f, -15.03f);
		G2.BottomH = 0.0f;
		G2.TopH = SecondZ;
		G2.FloorLow = 0;
		G2.FloorHigh = 1;

		FHIStairBand BG;
		BG.Rect = FBox2D(FVector2D(-1.20f, -14.74f), FVector2D(-0.20f, -11.05f));
		BG.Bottom = FVector2D(-0.70f, -11.10f);
		BG.Top = FVector2D(-0.70f, -14.74f);
		BG.BottomH = BasementZ;
		BG.TopH = 0.0f;
		BG.FloorLow = -1;
		BG.FloorHigh = 0;

		return TArray<FHIStairBand>{ G2, BG };
	}();
	return S;
}

bool FHISim::InStairBand(const FVector2D& Pos)
{
	for (const FHIStairBand& B : Stairs())
	{
		if (B.Contains(Pos))
		{
			return true;
		}
	}
	return false;
}

float FHISim::FloorHeight(int32 Floor)
{
	// The phase-2 table (HOUSE-DESIGN.md §2). Floors are -1..1; SelfCheck v2 asserts the
	// domain, so the default is a loud no-op rather than a plausible extrapolation --
	// Floor * WallHeight would silently answer 3.0 for floor 1 and hide an out-of-range
	// caller behind a number that looks like a storey.
	switch (Floor)
	{
	case -1: return BasementZ;
	case 0:  return 0.0f;
	case 1:  return SecondZ;
	default: return 0.0f;
	}
}

namespace
{
	// The band that owns a body whose LIVE height is known: of the bands containing Pos,
	// the one whose HeightAt is nearest Z -- but only within BodyHeight, because a
	// surface farther away than a body is tall is one the body is UNDER (or OVER), not
	// ON. Both halves of the rule are load-bearing (design-phase catches, 2026-10-15):
	//   * FIRST-MATCH selection teleports a basement climber at the B->G mid-point --
	//     his floor label flips to 0 there, and resolving "floor 0 in the stairwell"
	//     against the G->2 treads moves him -1.19 -> +1.26 m in one step.
	//   * NO threshold teleports a basement walker passing UNDER the upper flight (same
	//     plan footprint, 3.8 m of Z between the surfaces) up onto the second-floor run.
	const FHISim::FHIStairBand* BandNearestZ(const FVector2D& Pos, float Z)
	{
		const FHISim::FHIStairBand* Best = nullptr;
		float BestD = TNumericLimits<float>::Max();
		for (const FHISim::FHIStairBand& B : FHISim::Stairs())
		{
			if (!B.Contains(Pos))
			{
				continue;
			}
			const float D = FMath::Abs(B.HeightAt(Pos) - Z);
			if (D <= FHISim::BodyHeight && D < BestD)
			{
				Best = &B;
				BestD = D;
			}
		}
		return Best;
	}

	// Band choice for queries with no live Z (nav walks, floor-labelled probes): the
	// flight you DEPART from this floor wins over the one you arrive on it -- a floor-0
	// query in the stairwell means the ground-to-second run, a floor -1 query the
	// basement run.
	const FHISim::FHIStairBand* BandForFloor(const FVector2D& Pos, int32 Floor)
	{
		const FHISim::FHIStairBand* Arrives = nullptr;
		for (const FHISim::FHIStairBand& B : FHISim::Stairs())
		{
			if (!B.Contains(Pos))
			{
				continue;
			}
			if (B.FloorLow == Floor)
			{
				return &B;
			}
			if (!Arrives && B.FloorHigh == Floor)
			{
				Arrives = &B;
			}
		}
		return Arrives;
	}
}

bool FHISim::DeckCovers(const FVector2D& Pos, int32 Floor)
{
	// The basement's walking surface is the ground at FloorHeight(-1), not a deck --
	// the deck above it has its TOP at floor 0 -- so floor -1 never finds one and the
	// flat fallback answers. Roofs are bDeck for collision but their top (RoofTop) is
	// nobody's floor, which the MaxZ match excludes.
	if (Floor < 0)
	{
		return false;
	}
	const float H = FloorHeight(Floor);
	for (const FHISolid& S : WorldSolids())
	{
		if (S.bDeck && S.Span.MinZ < H
			&& FMath::IsNearlyEqual(S.Span.MaxZ, H, 0.01f)
			&& S.Rect.IsInsideOrOn(Pos))   // IsInside is STRICT -- the SelfCheck lesson
		{
			return true;
		}
	}
	return false;
}

float FHISim::FloorZAt(const FVector2D& Pos, int32 Floor)
{
	// DECK BEATS BAND (the slice's lesson, kept): a deck strip can lie above a flight's
	// foot inside the band's own footprint, and a body standing there is ON THE DECK --
	// deriving it from the band would teleport it down onto the treads below.
	if (DeckCovers(Pos, Floor))
	{
		return FloorHeight(Floor);
	}
	if (const FHIStairBand* B = BandForFloor(Pos, Floor))
	{
		return B->HeightAt(Pos);
	}
	return FloorHeight(Floor);
}

float FHISim::FloorZAt(const FVector2D& Pos, int32 Floor, float Z)
{
	if (DeckCovers(Pos, Floor))
	{
		return FloorHeight(Floor);
	}
	if (const FHIStairBand* B = BandNearestZ(Pos, Z))
	{
		return B->HeightAt(Pos);
	}
	return FloorHeight(Floor);
}

void FHISim::ApplyElevation(const FVector2D& Pos, int32& Floor, float& Z)
{
	// A deck matching the CARRIED floor confirms it; a deck never changes a floor (that
	// is what keeps a ground-floor body under the second-floor slab from being derived
	// up onto it). The band claim is by live Z, so a basement climber stays on his own
	// flight through the mid-point flip -- see BandNearestZ.
	if (DeckCovers(Pos, Floor))
	{
		Z = FloorHeight(Floor);
		return;
	}
	if (const FHIStairBand* B = BandNearestZ(Pos, Z))
	{
		Floor = B->FloorAt(Pos);
		Z = B->HeightAt(Pos);
		return;
	}
	Z = FloorHeight(Floor);
}

FVector2D FHISim::ResolveCircleZ(const FVector2D& InPos, float Radius, float Z)
{
	FVector2D Pos = InPos;
	// ON THE FLIGHT, THE FLIGHT OWNS THE BODY -- the slice's freeze lesson, measured
	// 2026-10-15: a railing guarding a deck edge can catch a CLIMBER's cylinder tip
	// while his feet are metres below the deck, and pin him at the push-out equilibrium
	// mid-flight with the stuck detector silent (the push-out still "moves" him). So
	// railing-class solids -- about a metre tall, standing AT the arrival floor's
	// height -- do not push a body that is on the flight arriving there. Walls and
	// screens (taller than railing class) still stop a climber: a wall doing its job.
	// Phase 2 generalises the slice's hardcoded Floor == 1: with two stacked flights
	// "the arrival floor" is per-band, and the band is chosen by live Z like everywhere
	// else. The phase-2 stairwell also avoids the trap by construction -- the upper
	// flight passes ~2 m under its deck-edge railing, and the hole lips sit 0.08 m
	// outside a body's reach -- so this skip is the belt to that geometry's braces,
	// kept because the freeze it prevents is silent and geometries change.
	const FHIStairBand* OnBand = BandNearestZ(Pos, Z);
	const bool bOnStairs = OnBand && Z < OnBand->TopH - 0.02f;
	for (const FHISolid& S : WorldSolids())
	{
		// Decks are stood ON, never walked into.
		if (S.bDeck)
		{
			continue;
		}
		if (bOnStairs
			&& S.Span.MaxZ - S.Span.MinZ <= 1.1f
			&& FMath::IsNearlyEqual(S.Span.MinZ, FloorHeight(OnBand->FloorHigh), 0.05f))
		{
			continue;
		}
		// STRICT span overlap against the body cylinder [Z, Z+BodyHeight]: a
		// second-floor body at Z == a ground-floor wall's MaxZ is above that wall's
		// storey, not inside it, and the stringer catches a man mid-flight while the
		// railing catches him at the top -- one rule, both floors, no special cases.
		if (!(Z < S.Span.MaxZ && Z + BodyHeight > S.Span.MinZ))
		{
			continue;
		}
		// The same push-out ResolveCircle performs, solid by solid -- with ONE addition:
		// ROUNDED CORNERS -- NARROWED 2026-10-15 after measuring both failure modes.
		// A run's END becomes the closest feature when the body is PAST THE END PLANE on
		// the run's long axis: the closest point snaps to the end corner and the push aims
		// diagonally off it. Square corners make statues: measured 2026-10-15
		// (ThiefUpstairs at the stringer's top face) -- a face push normal points straight
		// out of the wall, a waypoint can sit exactly along the wall on the far side,
		// desired and normal become antiparallel, the wall-slide projection is zero, and
		// the stuck reroute just re-aims at the same pin. A diagonal corner push always
		// leaves the slide a component along the wall toward the open side.
		//
		// The snap must NOT fire for a body BESIDE THE LONG FACE near an end. The first,
		// broader version did, and built the Shove orbit (measured 2026-10-15): a thief
		// sliding west along the divider's push-out equilibrium, bound for the 4 m hall
		// gap that would have let him through, reached 0.385 m from the east run's west
		// end (x 2.0). The corner snap replaced the true face point (0.341 m away -- he
		// genuinely overlapped the band by 0.079) with the farther corner (0.514 m --
		// "free"), so he walked south off the equilibrium into the band; the radial corner
		// push and its slide tangent then pumped him east along the corner circle until
		// the snap condition lapsed, where the face push re-acquired at nearly full depth
		// and launched him 0.3 m north back to the equilibrium. A closed 16-frame limit
		// cycle at x 2.35...2.42 with the stuck detector silent -- Moved ~ Want on every
		// frame -- orbiting for the whole 20 s window. Beside the face, the face push is
		// the geometrically correct answer and the slide already handles the end: once the
		// projection passes the run's end, the clamp itself picks the corner and he walks
		// around it into the gap.
		const FBox2D& R = S.Rect;
		FVector2D Closest(
			FMath::Clamp(Pos.X, R.Min.X, R.Max.X),
			FMath::Clamp(Pos.Y, R.Min.Y, R.Max.Y));
		if (R.Max.X - R.Min.X >= R.Max.Y - R.Min.Y)
		{
			// Long axis X: the ends are the Min.X / Max.X faces (the divider's runs, the
			// north and south walls). Past an end plane, snap the thin axis to its end.
			if ((Pos.X < R.Min.X || Pos.X > R.Max.X) && Pos.Y >= R.Min.Y && Pos.Y <= R.Max.Y)
			{
				const float ToMin = Pos.Y - R.Min.Y;
				const float ToMax = R.Max.Y - Pos.Y;
				if (ToMin < Radius && ToMin <= ToMax)      { Closest.Y = R.Min.Y; }
				else if (ToMax < Radius && ToMax < ToMin)  { Closest.Y = R.Max.Y; }
			}
		}
		else
		{
			// Long axis Y: the ends are the Min.Y / Max.Y faces (the stringer).
			if ((Pos.Y < R.Min.Y || Pos.Y > R.Max.Y) && Pos.X >= R.Min.X && Pos.X <= R.Max.X)
			{
				const float ToMin = Pos.X - R.Min.X;
				const float ToMax = R.Max.X - Pos.X;
				if (ToMin < Radius && ToMin <= ToMax)      { Closest.X = R.Min.X; }
				else if (ToMax < Radius && ToMax < ToMin)  { Closest.X = R.Max.X; }
			}
		}
		const FVector2D D = Pos - Closest;
		const float D2 = D.SizeSquared();
		if (D2 >= Radius * Radius)
		{
			continue;
		}
		if (D2 < 1e-9f)
		{
			const float Left = Pos.X - R.Min.X;
			const float Right = R.Max.X - Pos.X;
			const float Near = Pos.Y - R.Min.Y;
			const float Far = R.Max.Y - Pos.Y;
			const float M = FMath::Min(FMath::Min(Left, Right), FMath::Min(Near, Far));
			if (M == Left)       { Pos.X = R.Min.X - Radius; }
			else if (M == Right) { Pos.X = R.Max.X + Radius; }
			else if (M == Near)  { Pos.Y = R.Min.Y - Radius; }
			else                 { Pos.Y = R.Max.Y + Radius; }
			continue;
		}
		const float Dist = FMath::Sqrt(D2);
		Pos += D * ((Radius - Dist) / Dist);
	}
	return Pos;
}

const TArray<FVector2D>& FHISim::LootPositions()
{
	// Phase 2 (HOUSE-DESIGN.md §4.5): the TV on its stand in the living room, the safe on
	// the master's closet wall, the laptop on the office desk -- the last one upstairs, so
	// a full-clear heist has to hold two floors.
	static const TArray<FVector2D> L = {
		FVector2D(-3.50f, -12.72f),
		FVector2D( 4.30f,  -8.35f),
		FVector2D(-2.30f, -13.75f),
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
	static const TArray<FString> L = { TEXT("living"), TEXT("office"), TEXT("master") };
	return L;
}

const TArray<int32>& FHISim::LootFloors()
{
	static const TArray<int32> L = { 0, 1, 0 };
	return L;
}

const TArray<FString>& FHISim::SearchNodes()
{
	// Nine searchable rooms, one per floor's programme (§4.7): the thief's uncertainty has
	// somewhere to live on every level. Hall nodes, closets-as-rooms (masterBath, linen,
	// bath2) and the stair nodes are deliberately absent -- searching that cannot come up
	// empty is not searching, and a body standing in a doorway is not a room.
	static const TArray<FString> S = {
		TEXT("living"), TEXT("dining"), TEXT("kitchen"), TEXT("master"),
		TEXT("baseOpen"), TEXT("utility"),
		TEXT("br2"), TEXT("br3"), TEXT("office"),
	};
	return S;
}

const TArray<FString>& FHISim::NodeNames()
{
	static const TArray<FString> N = {
		// ground, outside-in
		TEXT("van"), TEXT("doorOut"), TEXT("doorIn"), TEXT("entryHall"),
		TEXT("living"), TEXT("dining"), TEXT("kitchen"),
		TEXT("hallSouth"), TEXT("hallNorth"),
		TEXT("master"), TEXT("masterBath"),
		TEXT("stairFootG"), TEXT("stairHeadB"), TEXT("screenPass"),
		// basement
		TEXT("stairFootB"), TEXT("baseOpen"), TEXT("baseDoor"), TEXT("utility"),
		// second floor
		TEXT("stairTopG"), TEXT("upHallN"), TEXT("upHall"),
		TEXT("br2"), TEXT("br3"), TEXT("bath2"), TEXT("office"), TEXT("linen"),
	};
	return N;
}

const TArray<FVector2D>& FHISim::NodePositions()
{
	// PHASE 2 -- every coordinate derived against the wall rows, furniture schedule and
	// stair bands, not eyeballed: each node stands clear of solids by at least a body
	// radius, and each edge is a straight WalkBlocked-clear line -- and since 2026-10-18
	// (the dining-table freeze, measured in TraceProbe B) each edge's line also keeps a
	// full body-radius envelope off every non-flat furniture rect on its floor. SelfCheck
	// asserts all of it, so a drift fails the build. Where the doc's §4.7 numbers moved,
	// the reason is on the line.
	static const TArray<FVector2D> P = {
		FVector2D(  0.00f,   5.00f), // van          (the Getaway's centre, unchanged)
		FVector2D(  0.00f,  -5.00f), // doorOut      (driveway, south of the stoop)
		FVector2D(  0.00f,  -8.60f), // doorIn       (inside the south wall, on the door axis)
		FVector2D(  0.30f,  -9.50f), // entryHall    (§4.7)
		FVector2D( -3.20f, -10.80f), // living       (doc -3.6,-10.5 moved: its walk line to
		                             //  entryHall crossed the x -1.2 wall 0.03 m south of the
		                             //  opening; -10.8 crosses mid-opening with 0.9 m to spare)
		FVector2D(  5.00f,  -9.00f), // dining       (doc 4.0,-10.0 moved: that spot is 0.35 m
		                             //  from the table corner; east of the chairs, clear of the
		                             //  sideboard by 0.08 at the body edge)
		FVector2D(  4.60f, -13.90f), // kitchen      (doc 4.0,-14.6 moved: the island owns that
		                             //  spot; south of it, 0.18 clear at the body edge)
		FVector2D(  0.90f, -12.90f), // hallSouth    (hall east of the stairwell screen)
		FVector2D(  0.40f, -15.90f), // hallNorth    (the landing north of the hole, where the
		                             //  B->G head steps off; the screen's north end is -15.03,
		                             //  so walk lines to/from here clear it by 0.16+.
		                             //  2026-10-18: "clear it by 0.16+" measured RADIUS 0 --
		                             //  the old stairHeadB line in fact passed 0.104 m from
		                             //  the screen's NW corner (-0.20,-15.03), inside the
		                             //  0.42 body, and pinned every master-bound walker on
		                             //  the east face. That edge now runs through screenPass.)
		FVector2D( -3.40f, -14.10f), // master       (doc value kept)
		FVector2D( -2.60f, -15.70f), // masterBath   (doc -2.5,-16.0 moved: that is inside the tub)
		FVector2D( -0.70f, -11.35f), // stairFootG   (§4.7; on the G->2 treads, h ~0.17)
		FVector2D( -0.70f, -14.60f), // stairHeadB   (AT THE NORTH HEAD, floor 0 -- the doc's
		                             //  placement at the shared foot predates the Z-proximity
		                             //  analysis: with stacked flights the foot belongs to the
		                             //  B->G band only in BASEMENT Z, and its floor-0 node
		                             //  would fight the treads under it. The head is flush with
		                             //  the deck at z 0.)
		FVector2D( -0.70f, -15.80f), // screenPass   (ADDITION 2026-10-18, veto-able: the pass
		                             //  SOUTH of the screen, on the north deck strip. The
		                             //  direct hallNorth->stairHeadB edge was not walkable at
		                             //  BODY radius: the line passes 0.104 m from the screen's
		                             //  NW corner (-0.20,-15.03) -- clear at radius 0, which is
		                             //  all the edge probe measures, and inside the 0.42 body
		                             //  the walker actually has. Measured in play (180 s probe):
		                             //  a reroute walks him south to hallNorth, the 0.6 m
		                             //  waypoint pop cuts the corner tighter still,
		                             //  ResolveCircleZ shoves him back onto the east face at
		                             //  (0.27,-14.61), and the re-aim at stairHeadB from there
		                             //  is due-west into an ENE corner push -- dot -0.998,
		                             //  slide tangent |T|^2 0.0002 vs the 0.0025 deadzone:
		                             //  full stop, stuck 1.2 s, reroute, repeat every ~2.4 s
		                             //  (74/138 reroutes in the 400 s MovingHeist, deliveries
		                             //  stalled at 0/3 and 1/3). No hall-side node clears that
		                             //  corner in ONE straight line (hallNorth at y -16.4/-16.9
		                             //  computes 0.245/0.311 -- the corner sits between the
		                             //  pair), so the edge gets an intermediate node: the
		                             //  stairLanding precedent, HOUSE-DESIGN §3 item 6.
		                             //  From (-0.70,-15.80), measured: the vertical approach
		                             //  to stairHeadB runs 0.50 off the screen's west face --
		                             //  the same 0.08 body pinch the stairwell is built to
		                             //  everywhere; hallNorth->screenPass passes 0.812 from
		                             //  the NW corner node-to-node and 0.473 in the worst
		                             //  0.6 m pop, and runs 0.35 south of the screen's north
		                             //  end. The node stands on the north deck strip (the
		                             //  GroundHole ends at -14.74), 1.34 off the south wall,
		                             //  body 0.02 clear of the x -1.2 wall's east face and
		                             //  0.14 clear of the vanity, in NEITHER stair band. From
		                             //  the old east-face equilibrium the first leg here
		                             //  clips the corner's danger disc, but south of it: the
		                             //  rect push-out slides him south along the face, the
		                             //  bearing improves monotonically, and the body clears
		                             //  the screen's north end at y -15.45 -- no deadzone,
		                             //  which is the point of putting the pass south.)
		FVector2D( -0.70f, -11.35f), // stairFootB   (same plan as stairFootG, floor -1, h ~-2.27)
		FVector2D( -3.80f, -11.80f), // baseOpen     (doc -3.0,-12.6 moved: that point IS lally
		                             //  column 1's centre. AS-BUILT 2026-10-18, moved again,
		                             //  0.40 north: from -12.20 the walk line to stairFootB
		                             //  passed 0.4446 from the column's NW corner -- an envelope
		                             //  margin of 0.025 -- the same corner-snap freeze class as
		                             //  the dining table, caught by the furniture-envelope probe
		                             //  before a thief ever found it. From -11.80, measured: 0.765
		                             //  to that corner on the stairFootB line (margin 0.345),
		                             //  0.557 NE-corner on the baseDoor line (margin 0.137),
		                             //  and the node itself stands 0.955 off the column.)
		FVector2D(  3.50f, -12.70f), // baseDoor     (in the mech-room doorway, floor -1; the
		                             //  doc's graph had no node here -- without it the utility
		                             //  room hangs off baseOpen by a line that crosses the
		                             //  x 3.0 wall. ADDITION, veto-able.)
		FVector2D(  4.50f, -15.00f), // utility      (§4.7)
		FVector2D( -0.70f, -15.50f), // stairTopG    (§4.7 said (-0.70,-14.80), the top tread.
		                             //  MOVED 2026-10-18: from the top tread NO node east of
		                             //  the screen is reachable in a straight line. The screen's
		                             //  north-west corner (-0.20,-15.03) sits 0.55 m from
		                             //  (-0.70,-14.80) at bearing -24.6 deg, and the body-radius
		                             //  (0.42) danger cone around that bearing spans -74..+25
		                             //  deg -- every in-house second-floor target east of the
		                             //  screen lands inside it. Measured: the old
		                             //  stairTopG->upHallN line passed 0.086 m from the corner,
		                             //  so ResolveCircleZ shoved EVERY arriving climber around
		                             //  it; the ThiefUpstairs thief never reached br2 in 90 s.
		                             //  On the landing at -15.50 the same line clears the corner
		                             //  by 0.51 m, the node still settles on the north slab
		                             //  piece (deck covers, Z 2.74), and the body keeps its
		                             //  0.02 m clearance of the floor-1 x -1.2 seg-c east face
		                             //  (reach -1.12 vs -1.14), same as on the tread.)
		FVector2D(  0.50f, -15.60f), // upHallN      (second-floor landing north of the slab
		                             //  hole; a direct stairTopG->upHall line crosses the
		                             //  screen at y -13.7. ADDITION, veto-able.)
		FVector2D(  0.40f, -11.50f), // upHall       (§4.7, floor 1)
		FVector2D( -3.40f, -10.20f), // br2          (doc -3.6 moved 0.2 east: the bed's body
		                             //  clearance)
		FVector2D( -3.60f, -14.80f), // br3          (§4.7, floor 1)
		FVector2D(  4.80f, -15.00f), // bath2        (§4.7, floor 1)
		FVector2D(  4.00f, -10.50f), // office       (§4.7, floor 1)
		FVector2D(  3.00f, -14.05f), // linen        (doc 2.8,-14.5 moved: the shelving's body
		                             //  clearance. 2026-10-18: the shelving shortened instead
		                             //  of the node moving again -- see linen_shelving's row.)
	};
	return P;
}

const TArray<TArray<FString>>& FHISim::NodeEdges()
{
	// Symmetric adjacency. Stair edges are exactly the pairs whose floors differ
	// (IsStairEdge): stairFootG-stairTopG climbs G->2, stairHeadB-stairFootB descends
	// B->G. Everything else is flat-floor walking.
	static const TArray<TArray<FString>> E = {
		{ TEXT("doorOut") },                                        // van
		{ TEXT("van"), TEXT("doorIn") },                            // doorOut
		{ TEXT("doorOut"), TEXT("entryHall") },                     // doorIn
		{ TEXT("doorIn"), TEXT("living"), TEXT("dining"),
		  TEXT("hallSouth"), TEXT("stairFootG") },                  // entryHall
		{ TEXT("entryHall") },                                      // living
		{ TEXT("entryHall"), TEXT("kitchen") },                     // dining
		{ TEXT("dining"), TEXT("hallSouth") },                      // kitchen
		{ TEXT("entryHall"), TEXT("kitchen"), TEXT("hallNorth") },  // hallSouth
		{ TEXT("hallSouth"), TEXT("screenPass") },                  // hallNorth
		{ TEXT("stairHeadB"), TEXT("masterBath") },                 // master
		{ TEXT("master") },                                         // masterBath
		{ TEXT("entryHall"), TEXT("stairTopG") },                   // stairFootG
		{ TEXT("screenPass"), TEXT("master"), TEXT("stairFootB") }, // stairHeadB
		{ TEXT("hallNorth"), TEXT("stairHeadB") },                  // screenPass
		{ TEXT("stairHeadB"), TEXT("baseOpen") },                   // stairFootB
		{ TEXT("stairFootB"), TEXT("baseDoor") },                   // baseOpen
		{ TEXT("baseOpen"), TEXT("utility") },                      // baseDoor
		{ TEXT("baseDoor") },                                       // utility
		{ TEXT("stairFootG"), TEXT("upHallN") },                    // stairTopG
		{ TEXT("stairTopG"), TEXT("upHall"), TEXT("br3"),
		  TEXT("linen") },                                          // upHallN
		{ TEXT("upHallN"), TEXT("br2"), TEXT("office") },           // upHall
		{ TEXT("upHall") },                                         // br2
		{ TEXT("upHallN") },                                        // br3
		{ TEXT("office") },                                         // bath2
		{ TEXT("upHall"), TEXT("bath2") },                          // office
		{ TEXT("upHallN") },                                        // linen
	};
	return E;
}

const TArray<int32>& FHISim::NodeFloors()
{
	static const TArray<int32> F = {
		 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // ground, 14
		-1, -1, -1, -1,                                        // basement, 4
		 1,  1,  1,  1,  1,  1,  1,  1,                        // second, 8
	};
	return F;
}

bool FHISim::IsStairEdge(const FString& A, const FString& B)
{
	const int32 Ia = NodeNames().IndexOfByKey(A);
	const int32 Ib = NodeNames().IndexOfByKey(B);
	if (Ia == INDEX_NONE || Ib == INDEX_NONE)
	{
		return false;
	}
	return NodeFloors()[Ia] != NodeFloors()[Ib];
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

namespace
{
	/**
	 * One solid's contribution to the height-aware sight test, EXACTLY: clip the segment
	 * to the t-interval where its height lies inside the span, then intersect only that
	 * sub-segment with the footprint. z(t) is linear and spans are flat, so the clip is
	 * two lerps.
	 *
	 * This replaced a conservative form on 2026-10-15 (the three-floor fork). The old
	 * test rejected a span only when the segment's WHOLE Z range lay outside it and then
	 * ran the WHOLE plan segment against the footprint. With one storey the approximation
	 * never fired; with slabs it fires exactly where it must not -- a homeowner at the
	 * stair foot (6.0, -11, eyes 1.62) looking up at a man on the second floor (7.1, -12,
	 * eyes 4.62) crosses slab height 2.75 at (6.41, -11.38), inside the stairwell hole,
	 * but his own feet are inside the slab's footprint and the conservative form called
	 * the sight line blocked. Defending the stairwell is a core scenario. Exact is a
	 * strict refinement: it only ever clears false blocks, so every truly-blocked
	 * assertion still holds. HOUSE-DESIGN.md §1.
	 */
	bool SpannedRectBlocks(const FVector& A, const FVector& B, const FBox2D& Rect,
		const FHISim::FHISolidSpan& Span)
	{
		const float DZ = B.Z - A.Z;
		float TLo = 0.0f;
		float THi = 1.0f;
		if (FMath::Abs(DZ) < KINDA_SMALL_NUMBER)
		{
			// Flat segment: inside the span along its whole length, or never inside.
			if (A.Z < Span.MinZ || A.Z > Span.MaxZ)
			{
				return false;
			}
		}
		else
		{
			float T1 = (Span.MinZ - A.Z) / DZ;
			float T2 = (Span.MaxZ - A.Z) / DZ;
			if (T1 > T2)
			{
				Swap(T1, T2);
			}
			TLo = FMath::Max(TLo, T1);
			THi = FMath::Min(THi, T2);
			if (THi <= TLo)
			{
				return false;
			}
		}
		const FVector P0 = A + (B - A) * TLo;
		const FVector P1 = A + (B - A) * THi;
		return FHISim::SegRect(FVector2D(P0.X, P0.Y), FVector2D(P1.X, P1.Y), Rect);
	}
}

bool FHISim::LosBlocked3D(const FVector& A, const FVector& B,
	const TArray<FBox2D>& InSolids, const TArray<FHISolidSpan>& Spans)
{
	for (int32 Idx = 0; Idx < InSolids.Num(); ++Idx)
	{
		if (SpannedRectBlocks(A, B, InSolids[Idx], Spans[Idx]))
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

bool FHISim::LosBlockedWorld(const FVector& A, const FVector& B)
{
	for (const FHISolid& S : WorldSolids())
	{
		if (SpannedRectBlocks(A, B, S.Rect, S.Span))
		{
			return true;
		}
	}
	return false;
}

namespace
{

// The longhand ray-vs-AABB core, shared by the legacy ground-floor cast and the
// floor-aware visibility polygon. Takes the rectangle list so the two cannot drift.
float RayCastRects(const FVector2D& From, const FVector2D& Dir, float MaxT, const TArray<FBox2D>& Rects)
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

	for (const FBox2D& S : Rects)
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

} // namespace

float FHISim::RayCastPx(const FVector2D& From, const FVector2D& Dir, float MaxT)
{
	// Ground-floor semantics preserved exactly: legacy callers cast against Solids(), the
	// same walls+furniture projection they always did.
	return RayCastRects(From, Dir, MaxT, Solids());
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

bool FHISim::WalkBlocked(const FVector2D& A, const FVector2D& B, int32 Floor)
{
	const float Z = FloorHeight(Floor);
	for (const FHISolid& S : WorldSolids())
	{
		if (S.bDeck || S.Floor != Floor)
		{
			continue;
		}
		// Only what a walking body's cylinder strictly overlaps can stop the walk --
		// the same rule ResolveCircleZ enforces, applied to a segment instead of a disc.
		if (!(Z < S.Span.MaxZ && Z + BodyHeight > S.Span.MinZ))
		{
			continue;
		}
		if (SegRect(A, B, S.Rect))
		{
			return true;
		}
	}
	return false;
}

bool FHISim::CanSee3D(const FVector& A, const FVector& B, float MaxRange)
{
	// Feet in, eyes computed here -- callers pass positions carrying Z from
	// FloorZAt, never pre-raised eye points, so the eye height cannot drift between
	// call sites. Range is 3-D: a man two metres below you down a stairwell is not
	// at pistol range just because his plan projection is close.
	if (FVector::Dist(A, B) > MaxRange)
	{
		return false;
	}
	const FVector EyeA(A.X, A.Y, A.Z + EyeHeight);
	const FVector EyeB(B.X, B.Y, B.Z + EyeHeight);
	return !LosBlockedWorld(EyeA, EyeB);
}

bool FHISim::ThiefSees3D(const FVector& ThiefPos, float ThiefYaw, const FVector& PlayerPos)
{
	if (!CanSee3D(ThiefPos, PlayerPos, ThiefSight))
	{
		return false;
	}
	// The FOV cone stays a plan test: a man turns his head on the flat, whatever his
	// elevation, and the bearing to a target up or down a stairwell is the plan bearing.
	const float Bearing = FMath::Atan2(PlayerPos.Y - ThiefPos.Y, PlayerPos.X - ThiefPos.X);
	return FMath::Abs(FMath::FindDeltaAngleRadians(Bearing, ThiefYaw))
		<= FMath::DegreesToRadians(ThiefFovDeg * 0.5f);
}

TArray<FBox2D> FHISim::FloorSolidRects(int32 Floor)
{
	// The same filter WalkBlocked uses: decks are stood ON, never looked through at, and
	// every other solid belongs to exactly one storey by its Floor tag.
	TArray<FBox2D> Out;
	for (const FHISolid& S : WorldSolids())
	{
		if (!S.bDeck && S.Floor == Floor)
		{
			Out.Add(S.Rect);
		}
	}
	return Out;
}

TArray<FVector2D> FHISim::VisibilityPolygon(const FVector2D& From, float MaxRange, int32 Floor)
{
	// FLOOR-AWARE since the fork: a man on the second floor must not get the ground
	// floor's walls on his minimap, and vice versa. Floor 0 takes Solids() exactly as
	// before -- the legacy behaviour is the floor-0 case of this function, not a second
	// code path.
	// The +/-1e-4 neighbours of each corner angle are the whole trick: the centre ray grazes
	// the corner exactly, and its two neighbours land just inside and just outside, so the
	// ring has vertices on both sides of every occluder edge instead of one sample slipping
	// through the seam between two solids.
	const TArray<FBox2D> FloorRects = Floor == 0 ? TArray<FBox2D>() : FloorSolidRects(Floor);
	const TArray<FBox2D>& Rects = Floor == 0 ? Solids() : FloorRects;

	TArray<float> Angles;
	Angles.Reserve(Rects.Num() * 12 + 64);

	for (const FBox2D& S : Rects)
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
		Points.Add(From + Dir * RayCastRects(From, Dir, MaxRange, Rects));
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
	// GROUND FLOOR, deliberately: every legacy caller means "the floor you stand on",
	// which before the fork was the only floor. The Navigation test caught the day this
	// briefly returned `upstairs` for a point in the front hall -- plan distance 0.0 to
	// a node 3 m overhead. AnyFloor (any floor) belongs to the safety-net fall-through
	// inside the two-argument form, not to the default.
	return NearestNode(P, 0);
}

FString FHISim::NearestNode(const FVector2D& P, int32 Floor)
{
	// Floor == AnyFloor means "any floor" -- the legacy single-argument behaviour. A
	// filtered call considers only that floor's nodes, because an unfiltered Voronoi
	// hands a ground-floor thief the node stacked directly above him and routes him into
	// a slab. AS-BUILT 2026-10-18: the sentinel used to be INDEX_NONE == -1, which IS
	// the basement's floor number -- every basement query ran unfiltered (see HISim.h).
	// Safety net: if the filter finds NOTHING (a floor with no nodes -- cannot
	// happen in this graph, but a silent frontHub default is how the freeze of
	// 2026-10-15 hid), fall through to the unfiltered nearest.
	FString Best;
	float BestD = TNumericLimits<float>::Max();
	const TArray<FString>& Names = NodeNames();
	const TArray<FVector2D>& Poses = NodePositions();
	const TArray<int32>& Floors = NodeFloors();
	for (int32 Idx = 0; Idx < Names.Num(); ++Idx)
	{
		if (Floor != AnyFloor && Floors[Idx] != Floor)
		{
			continue;
		}
		const float D = FVector2D::DistSquared(Poses[Idx], P);
		if (D < BestD)
		{
			BestD = D;
			Best = Names[Idx];
		}
	}
	if (Best.IsEmpty())
	{
		// The filtered pass found nothing -- the promised unfiltered fall-through. It
		// cannot recurse: AnyFloor never skips a node, and the graph is never empty.
		float AnyD = TNumericLimits<float>::Max();
		for (int32 Idx = 0; Idx < Names.Num(); ++Idx)
		{
			const float D = FVector2D::DistSquared(Poses[Idx], P);
			if (D < AnyD)
			{
				AnyD = D;
				Best = Names[Idx];
			}
		}
	}
	return Best;
}

FString FHISim::NearestClearNode(const FVector2D& P, int32 Floor)
{
	// See HISim.h: the walkable FROM vertex for thief routing. The measured freeze this
	// answers (2026-10-18): pinned at (0.27,-14.10) on the stairwell screen's east face,
	// plain NearestNode answered stairHeadB -- 1.09 m away through a full-height screen
	// (hallSouth, 1.36 m, is the nearest actually reachable) -- and RouteTo's From-node
	// exclusion turned that into the single leg [master], due west, perpendicular into
	// the screen: tangent (0,0) in the slide deadzone, stuck->reroute->same node->same
	// bearing forever. WalkBlocked sees the screen (WorldSolids, strict body-span rule),
	// so the filter asks exactly the question the walk itself answers.
	if (Floor == AnyFloor)
	{
		// Cross-floor lines are not walkable lines; the distance-only contract is the
		// honest answer when the caller did not name a floor.
		return NearestNode(P, Floor);
	}
	FString Best;
	float BestD = TNumericLimits<float>::Max();
	const TArray<FString>& Names = NodeNames();
	const TArray<FVector2D>& Poses = NodePositions();
	const TArray<int32>& Floors = NodeFloors();
	for (int32 Idx = 0; Idx < Names.Num(); ++Idx)
	{
		if (Floors[Idx] != Floor)
		{
			continue;
		}
		const float D = FVector2D::DistSquared(Poses[Idx], P);
		if (D < BestD && !WalkBlocked(P, Poses[Idx], Floor))
		{
			BestD = D;
			Best = Names[Idx];
		}
	}
	if (Best.IsEmpty())
	{
		// Pinned somewhere no node has a clear line (a corner, a shoved-off position):
		// degrade to the old behaviour rather than emit no route.
		return NearestNode(P, Floor);
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
	 * WalkBlocked inflated by the walking BODY -- the routing-decision twin, added
	 * 2026-10-18. WalkBlocked answers "does the LINE cross a solid" (radius 0, pinned
	 * by contract: the nav-graph edge validation and NearestClearNode both speak line
	 * clearance). The prepend guard below needs a different answer: "can the 0.84 m
	 * body SWEEP this leg". The gap is measured, and it is this project's favourite
	 * lesson wearing a new face -- radius-0 clearance is not body-radius walkability.
	 * TraceProbe E/F0, post-commitment: a searcher at (0.27,-14.85) on the second
	 * floor, due west of the stairwell hole's east lip, target br3, rerouted to the
	 * single leg [br3]. The line passes 0.06 m north of the lip -- clear at radius 0,
	 * so the guard never prepended -- but the body needs its centre at y <= -15.16 to
	 * sweep it. ResolveCircleZ held him at the push-out equilibrium x = -0.15 + 0.42 =
	 * 0.270, the heading (yaw 3.14, due west) made the contact exactly head-on, the
	 * wall-slide deadzoned (tangent ~ 0), and the 1.2 s stuck detector rerouted him
	 * into the same infeasible leg 220+ times in 400 s while every clean run died
	 * there with the laptop undelivered.
	 *
	 * TWO gates, and the second one cost an iteration to learn (measured, run 15):
	 *
	 * 1. The solid filter mirrors ResolveCircleZ, NOT WalkBlocked: span-overlap
	 *    against the walker's cylinder [Z, Z+BodyHeight], with NO floor-tag filter.
	 *    The first version inherited WalkBlocked's `S.Floor != Floor` skip and the pin
	 *    survived verbatim -- because the solid doing the pinning is the stairwell
	 *    SCREEN, tagged floor 0, span [0, 3.74]: deliberately full-height, its top
	 *    metre IS the second floor's east-lip railing ("East lip: the screen", in the
	 *    solid table). WalkBlocked at floor 1 cannot see it; the physics can and does.
	 *    Walls and slabs keep their storeys by construction -- a ground wall's span
	 *    tops out at exactly 2.74, where the strict-overlap test lets a second-floor
	 *    cylinder pass -- so dropping the tag filter adds no phantom blockers.
	 * 2. SegRect's inflation is SQUARE, conservative at diagonals -- the safe
	 *    direction here: a false positive merely prepends the From node, one extra
	 *    leg that NearestClearNode already guaranteed at line clearance.
	 */
	bool WalkBlockedBody(const FVector2D& A, const FVector2D& B, int32 Floor, float Radius)
	{
		const float Z = FHISim::FloorHeight(Floor);
		for (const FHISim::FHISolid& S : FHISim::WorldSolids())
		{
			// Decks are stood ON, never walked into -- same as the physics.
			if (S.bDeck)
			{
				continue;
			}
			if (!(Z < S.Span.MaxZ && Z + FHISim::BodyHeight > S.Span.MinZ))
			{
				continue;
			}
			if (FHISim::SegRect(A, B, S.Rect, Radius))
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * Routing key for a hunt target (2026-10-18): the item's BIRTH node while the
	 * appended final leg from it can actually be walked, the nearest clear node on the
	 * item's CURRENT floor once the item has been displaced where that leg cannot. The
	 * appended final leg of RouteToWithAppend is a straight walk with no graph path --
	 * born single-floor, where "straight at it" always had a floor under it. A shove
	 * that drops the item on another storey makes the straight leg unwalkable: measured
	 * in the Shove pin (run 21), the hunter climbed to office (f1) for a laptop lying at
	 * (0.11,-9.59) f0, walked west along the slab, popped the floor-blind append
	 * waypoint 0.39 m of plan distance above it, and froze -- the z-gated grab refusing
	 * (correctly) with hunt holding no empty-route rebuild. Re-keying sends the graph
	 * path, stairs included, to the floor the item is really on.
	 *
	 * WRONG-GUESS-RECORDED, two iterations. V1 re-keyed on distance (>= SearchRange
	 * from the birth node). V2 required a clear radius-0 line from birth node to item.
	 * BOTH broke every at-home item, because ITEMS LIVE ON FURNITURE: the TV stands on
	 * its console, the line living->TV ends INSIDE the console solid, and SegRect is
	 * inclusive -- v2 declared the birth key unusable for the TV and re-keyed it to
	 * NearestClearNode(TV.Pos), which answered "master", across the divider. Measured
	 * (runs 22-24, byte-identical across all three rule variants -- the divergence from
	 * run 21 was this helper, NOT the search override, whose parameter changes proved
	 * inert): E churned 330 s / 85 reroutes hunting the TV from the wrong side of the
	 * x -1.2 wall, 0/3 delivered; B2 lost ~18 s to the same stall, arrived at the
	 * br2/br3 doorways out of phase, hit the prepend-guard ping-pong there twice (8 +
	 * 10 reroutes) and died at 180 s with 2/3; F0 slipped 174.7 -> 190.5 s; G's
	 * reroutes went 40/9/32 -> 38/53/50.
	 *
	 * V3 trims the test line short of the item by (GrabRange - 0.15): the append leg's
	 * job ends at the grab, not at the item's centre -- the furniture it stands on is
	 * the destination, never a blocker. Lines at or under the trim length are usable
	 * without a test (the grab fires before any walking matters). With this, every
	 * at-home item keeps its birth node bit-identically (run 21 behaviour), and only
	 * genuinely displaced items re-key. See the hunt branch of StepThief.
	 */
	FString HuntRouteKey(const FHISim::FHILootItem& L)
	{
		FString Key = L.Node;
		const int32 BirthIdx = FHISim::NodeNames().IndexOfByKey(Key);
		if (BirthIdx != INDEX_NONE && FHISim::NodeFloors()[BirthIdx] == L.Floor)
		{
			const FVector2D NodePos = FHISim::NodePositions()[BirthIdx];
			const FVector2D ToItem = L.Pos - NodePos;
			const float Len = ToItem.Size();
			const float Trim = FHISim::GrabRange - 0.15f;
			if (Len <= Trim
				|| !WalkBlockedBody(NodePos, NodePos + ToItem * ((Len - Trim) / Len),
					L.Floor, 0.0f))
			{
				return Key;
			}
		}
		const FString ClearKey = FHISim::NearestClearNode(L.Pos, L.Floor);
		return ClearKey.IsEmpty() ? Key : ClearKey;
	}

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

	/**
	 * AS-BUILT 2026-10-18: the walking FROM-leg of every reroute. NearestClearNode
	 * guarantees the straight line P->From is walkable, but RouteTo DROPS From (Godot
	 * parity, pinned), so a bare RouteTo(NearestClearNode(P, Floor), To) sends the
	 * thief's first leg to From's SUCCESSOR -- a leg nothing guarantees. Measured: a
	 * searcher pinned on the stairwell screen's east face at (0.27,-14.60) got clear
	 * node hallNorth, dropped path [stairHeadB, master], and stairHeadB (-0.70,-14.60)
	 * bears due west from that exact push-out equilibrium -- perpendicular into the
	 * face pinning him, tangent under the wall-slide deadzone: a reroute every ~1.2 s
	 * (99 in a 180 s heist, 279 in 400 s), route stuck at 2, position never changing.
	 * Prepending From's position makes him walk the guaranteed-clear line first;
	 * RouteTo's contract and the pins are untouched. TWO guards, both measured:
	 *
	 * 1. ONLY when the path is non-empty: From == To must keep returning the empty
	 *    route -- the fleeing thief's escape fires on that emptiness (see PathNodes),
	 *    and a prepended [vanPos] would refill forever with him standing in the drive.
	 * 2. ONLY when the DIRECT first leg P->Out[0] is blocked FOR THE BODY
	 *    (WalkBlockedBody above; this said radius-0 WalkBlocked until the evening of
	 *    2026-10-18, and the second-floor rim pin -- a 0.06 m line clearance the
	 *    0.42 m body could not sweep, 222 reroutes in 400 s -- walked straight
	 *    through that gap). An unconditional
	 *    prepend adds a backtracking leg whenever From lies BEHIND the thief, and
	 *    HI.Sim.SolidBodies caught the idle-lawn case the same day: a held thief at
	 *    (0,0) tied van against doorOut at 5.00 m (NearestClearNode keeps the first
	 *    node on a tie), PathNodes(van, Room) came back non-empty, and the prepended
	 *    van (0,5) walked him 4.41 m south while the homeowner strolled up from the
	 *    north. (The pin's "z" is Godot's depth axis, this port's +Y -- it measures
	 *    drift toward the van, not height.) When the direct leg is clear, walking it
	 *    is strictly shorter AND is the pre-fix behavior; when it is blocked -- the
	 *    screen-face storm above -- the prepend is the guaranteed-clear detour, and
	 *    From->Out[0] is then a graph edge. (This comment used to claim the
	 *    furniture-envelope probe keeps that edge walkable at body radius -- it does
	 *    not: the probe covers FURNITURE, and the screen is a rail-class solid. See
	 *    the update below.)
	 *
	 * UPDATE 2026-10-18, later the same day: the prepend alone did not end the
	 * screen-face storm, it changed its shape (99 -> 49 reroutes in the 180 s ThiefAI
	 * replay). The detour [hallNorth, stairHeadB, master] walked south correctly, but
	 * the walker pops hallNorth up to 0.6 m early and the re-aim at stairHeadB clipped
	 * the screen's NW corner -- the hallNorth->stairHeadB edge itself passed 0.104 m
	 * from that corner, inside the 0.42 body, while every probe stayed green (the edge
	 * test measures radius 0). ResolveCircleZ shoved him back onto this same east-face
	 * equilibrium and the due-west re-aim deadzoned: a ~2.4 s stuck/reroute loop, 74
	 * reroutes in the 400 s MovingHeist, deliveries stalled. Fixed in DATA, not here:
	 * the edge now runs through screenPass, south of the screen (see the node's comment
	 * in HISim.cpp). The prepend and both guards stay -- the From-drop guarantee is
	 * unchanged, and from the old equilibrium the first leg to screenPass slides south
	 * along the face and walks him out.
	 */
	TArray<FVector2D> RouteFromClear(const FVector2D& P, int32 Floor, const FString& ToKey,
		const FHISim::FHIThief* Th = nullptr)
	{
		const FString FromKey = FHISim::NearestClearNode(P, Floor);
		TArray<FVector2D> Out = FHISim::RouteTo(FromKey, ToKey);
		const int32 FromIdx = FHISim::NodeNames().IndexOfByKey(FromKey);
		// HYSTERESIS (2026-10-19, SoloProbe S/S2, see FHIThief::LastPoppedWp): do not
		// prepend the From node the walker JUST popped -- he would turn around, walk
		// back to a known-bad aim point, and pop it again, and each flip of the first
		// waypoint clears the corner-rounding commitment AND resets WpT, so neither the
		// slide nor the orbit breaker ever finishes. The 8 s window is measured, not
		// tuned by taste: the first cut used 2.0 s and S still pinned -- the flip cycle
		// is a THREE-way race at the living doorway (Louise shoved to (-2.50,-10.82)):
		// SteerAround's orbit deflection aims him north into the jamb while WpT < 2.5,
		// the tangential slide returns him south, net motion ~0 (yaw 2.93, corner=0 in
		// every sample -- a tangential branch, never the deadzone), and the 1.2 s
		// windowed reroute therefore fires BEFORE the 2.5 s orbit break, at
		// LastPoppedAgo ~2.3-3.0: a 2 s window had already expired and the prepend went
		// through (route 1<->2 at t=112/124, popped=2.8-3.0 at the flips). 8 s covers
		// orbit break (2.5) plus the slide through the door (~1 s) with margin. The
		// screen-face storm the prepend was written for is unaffected twice over: its
		// pin forms on a DIFFERENT From than the popped one in the ThiefAI replay, and
		// even a suppressed prepend there leaves the documented escape -- "the first leg
		// to screenPass slides south along the face and walks him out" (2026-10-18).
		const bool bJustPopped = Th != nullptr && Th->LastPoppedAgo < 8.0f
			&& FromIdx != INDEX_NONE
			&& FVector2D::Distance(FHISim::NodePositions()[FromIdx], Th->LastPoppedWp) < 0.3f;
		if (Out.Num() > 0 && FromIdx != INDEX_NONE && !bJustPopped
			&& WalkBlockedBody(P, Out[0], Floor, FHISim::ThiefRadius))
		{
			Out.Insert(FHISim::NodePositions()[FromIdx], 0);
		}
		return Out;
	}

	/** RouteFromClear with the loot position appended -- see RouteToWithAppend. */
	TArray<FVector2D> RouteFromClearWithAppend(const FVector2D& P, int32 Floor,
		const FString& ToKey, const FVector2D& AppendPos, const FHISim::FHIThief* Th = nullptr)
	{
		const FString FromKey = FHISim::NearestClearNode(P, Floor);
		TArray<FVector2D> Out = RouteToWithAppend(FromKey, ToKey, AppendPos);
		const int32 FromIdx = FHISim::NodeNames().IndexOfByKey(FromKey);
		// Same hysteresis, same measured window -- see RouteFromClear above.
		const bool bJustPopped = Th != nullptr && Th->LastPoppedAgo < 8.0f
			&& FromIdx != INDEX_NONE
			&& FVector2D::Distance(FHISim::NodePositions()[FromIdx], Th->LastPoppedWp) < 0.3f;
		if (Out.Num() > 1 && FromIdx != INDEX_NONE && !bJustPopped
			&& WalkBlockedBody(P, Out[0], Floor, FHISim::ThiefRadius))
		{
			Out.Insert(FHISim::NodePositions()[FromIdx], 0);
		}
		return Out;
	}
}

void FHISim::StuckStep(FHIThief& Thief, float NetMoved, float Dt,
	const TArray<FHILootItem>& Loot, TArray<FHIEvent>& Events)
{
	// Measured against what he was actually trying to do this frame, not against
	// THIEF_SPEED: he legitimately covers less ground with a safe in his arms and mid-turn,
	// and while dwelling at the threshold he does not move at all.
	//
	// `NetMoved` is travel from the FRAME-START position to where he stands AFTER body
	// resolution. Getting that second half right is the whole reason this is a function:
	// the solo path resolves bodies in-step, but the group path resolves them in
	// `StepIntruders` AFTER this function's caller has returned -- so a group thief jammed
	// against another body measured a full walking step every frame (bodies are not solids;
	// the walk always "succeeds") while the crowd pass handed him straight back. Measured
	// 2026-10-18, the stair-well mouth run: three intruders, the last loot behind them,
	// `stuck=0.00` on every one of them for 250+ simulated seconds -- the 2026-10-15
	// freeze class with the detector structurally blind instead of mis-routed. There is no
	// Godot parity to preserve here: the original's group path never moved an intruder at
	// all (see the post-step-separation note in StepIntruders), so it never had a jam to
	// detect. The threshold, the 1.2 s window and the re-route target logic below are the
	// solo detector's, unchanged.
	const float Want = Thief.Step;
	if (Want > 0.0f && NetMoved < FMath::Max(Want * 0.25f, 0.0008f))
	{
		Thief.StuckT += Dt;
	}
	else
	{
		Thief.StuckT = 0.0f;
	}

	// WINDOWED NET PROGRESS (2026-10-18). The per-frame rule above still has a blind
	// spot, and the group replay measured it: three intruders at the 1.2 m living
	// doorway -- two walking west, one carrying the TV east -- met head-on, and the
	// thief-thief half-pushes cancelled most but not all of each walking step. Net
	// travel settled at ~0.006 m/frame against the 0.0054 m threshold: "moving" every
	// single frame, 280+ s of standstill with stuck=0.00 and zero reroutes (TraceProbe
	// G). Same shape as the solo bisector flap (0.003-0.007 m/frame), which search
	// commitment removed at the source; this one is bodies, not targets, and no route
	// rebuild dissolves a mutual push. So: over the same 1.2 s window, compare the
	// DISPLACEMENT between the window's ends against 25% of the travel he intended
	// over it (summed per frame, so carries, turns and stair pace all count
	// honestly). An orbiting or wall-sliding walker keeps well over half his intent
	// in displacement; a mutual-push stall keeps ~0. The window only runs on frames
	// he tried to move, so dwelling is not stuckness.
	if (Want > 0.0f)
	{
		if (Thief.StuckAnchorT == 0.0f)
		{
			Thief.StuckAnchor = Thief.Pos;
		}
		Thief.StuckAnchorT += Dt;
		Thief.StuckWant += Want;
	}
	const bool bWindowElapsed = Thief.StuckAnchorT >= 1.2f;
	// EXEMPT WHILE ROUNDING A CORNER (measured, run 20, probe F0): the committed
	// corner slide needs ~1.8-2.2 s to clear the 2.83 m screen face, longer than this
	// window -- and the reroute cycle it feeds is self-sustaining: the window's round
	// trip (0.9 s prepend-walk to upHallN, pop, re-aim, slide south back to the start)
	// nets 0.02 m, the window reroutes, the waypoint change clears the commitment, and
	// F0 ran it 220 times in 400 s while E escaped only because its first slide
	// happened to straddle a window boundary. Rounding IS progress; give it the
	// window. A genuinely stalled commitment is bounded by CornerSideT's 3 s
	// staleness release, after which this window resumes -- worst case a ~4.2 s
	// reroute cadence instead of 1.2 s, still policed.
	const bool bWindowStuck = bWindowElapsed && Thief.CornerSide == 0
		&& FVector2D::Distance(Thief.Pos, Thief.StuckAnchor) < FMath::Max(0.25f * Thief.StuckWant, 0.05f);
	if (bWindowElapsed)
	{
		Thief.StuckAnchorT = 0.0f;
		Thief.StuckWant = 0.0f;
	}

	if (Thief.StuckT > 1.2f || bWindowStuck)
	{
		Thief.StuckT = 0.0f;
		FString Back = TEXT("entryHall");
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
		Thief.Route = RouteFromClear(Thief.Pos, Thief.Floor, Back, &Thief);
		FHIEvent Ev;
		Ev.Type = TEXT("reroute");
		Events.Add(Ev);
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
	// Effective distance to a point ON A FLOOR: plan distance plus TWELVE metres per
	// storey of difference. Twelve is measured, not tuned: the slice detour from the
	// front hub to the upstairs room is 3.1 m of plan to the stair foot + 4.4 m of
	// flight at 0.7x pace + 3.3 m across the deck ~= 12.7 m of walking. A penalty of
	// 6 let `upstairs` (plan-near everything, being 3 m above the middle of the house)
	// beat unsearched ground rooms -- measured in the MovingHeist trace of 2026-10-15:
	// at (1.24,-10.21) it tied `study` at 7.47 and won on float noise, and the heist
	// died in an orbit around the camped homeowner. For TARGET CHOICE only: routes are
	// graph paths, and the graph knows the real way round.
	auto FloorDist = [&Thief, &TPos](const FVector2D& Plan, int32 OnFloor)
	{
		return FVector2D::Distance(TPos, Plan) + 12.0f * FMath::Abs(OnFloor - Thief.Floor);
	};

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
	// AND THE PUSH MUST SURVIVE THE REST OF THE FRAME (2026-10-18, the second half of the
	// missing-call bug above). `PPos` was snapshotted at the top of this function, before
	// the shove ran, and the separation pass at the bottom writes `Player.Pos = PPos`
	// unconditionally -- so the 0.55 m knock-back was applied and then CLOBBERED by the
	// stale snapshot every single frame on this path. The shove fired (event, stagger and
	// cooldown all observed in the ThiefAI probe: one every 2.6 s for 74 s straight) and
	// the homeowner never moved a millimetre, which turned a corridor stand-off into a
	// permanent deadlock: measured, the thief boxed in a 0.35 m pocket east of an idle
	// mannequin from t=106 to the 180 s horn. Same shape as the missing call itself --
	// HI.Sim.Shove pins the displacement by calling ThiefShove DIRECTLY, so it stayed green
	// while the integrated path threw the result away. The group path never had this
	// clobber: its separation runs in StepIntruders, reading Player.Pos live.
	// Not verifiable against sim.gd from this machine (the Godot checkout is elsewhere);
	// what is verified is that the ported contract -- ThiefShove writes Player.Pos -- was
	// dead on arrival inside StepThief until this line.
	PPos = Player.Pos;

	// ---- decide
	if (Thief.Mode == TEXT("flee") || Thief.Fear >= FearFlee)
	{
		// First entry into flee is the bark moment (2026-10-19): fix-list item 16's
		// "he flees" hook was documented as firing but never had an event behind it. The
		// mode is sticky, so emitting only on the transition keeps it one line per man.
		if (Thief.Mode != TEXT("flee"))
		{
			FHIEvent FleeEv;
			FleeEv.Type = TEXT("thiefFlee");
			FleeEv.Id = Thief.Id;
			Events.Add(FleeEv);
		}
		Thief.Mode = TEXT("flee");
		if (Thief.Route.Num() == 0)
		{
			Thief.Route = RouteFromClear(TPos, Thief.Floor, TEXT("van"), &Thief);
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
			Thief.Route = RouteFromClear(TPos, Thief.Floor, TEXT("van"), &Thief);
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
					if (FloorDist(Loot[Cand].Pos, Loot[Cand].Floor)
						< FloorDist(Loot[Best].Pos, Loot[Best].Floor))
					{
						Best = Cand;
					}
				}
				Thief.Target = Loot[Best].Label;
				TgtIdx = Best;
				// A DROPPED ITEM IS ROUTED TO VIA ITS CURRENT FLOOR, not its birth room
				// -- HuntRouteKey carries the measurement (Shove pin, run 21). While the
				// item is home this is bit-identical to the old call.
				Thief.Route = RouteFromClearWithAppend(TPos, Thief.Floor,
					HuntRouteKey(Loot[Best]), Loot[Best].Pos, &Thief);
			}
			// The plan distance is not enough: a second-floor node can sit 3 m above a
			// ground-floor room IN PLAN ONLY. He reaches an item when his hands are at its
			// height, not when his shadow is next to it.
			const float LootZ = FloorZAt(Loot[TgtIdx].Pos, Loot[TgtIdx].Floor);
			if (FVector2D::Distance(TPos, Loot[TgtIdx].Pos) < GrabRange
				&& FMath::Abs(Thief.Z - LootZ) < 0.6f && Thief.Regrab <= 0.0f)
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
			else if (Thief.Route.Num() == 0)
			{
				// HUNT'S EMPTY-ROUTE SELF-HEAL (2026-10-18): the fourth branch to need
				// one -- the fronthub comment's claim that hunt already rebuilds on an
				// emptied route was wrong; it rebuilds only on a TARGET change. Shove()
				// empties the route mid-hunt and the target stays valid, so the hunter
				// stood with Want=0 forever, invisible to BOTH stuck rules (the window
				// only accrues on Want>0 frames, and a standing body does not move).
				// Same freeze class as the fronthub, reached by a different door.
				// Rebuilt with the drop-aware key: standing on the dropped item,
				// RouteTo(X, X) is empty by contract, the route is the append alone,
				// it pops at once, and the grab lands the moment Regrab expires.
				Thief.Route = RouteFromClearWithAppend(TPos, Thief.Floor,
					HuntRouteKey(Loot[TgtIdx]), Loot[TgtIdx].Pos, &Thief);
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
				// COMMIT TO THE ROOM YOU ARE WALKING TO (2026-10-18). Re-picking the
				// nearest unsearched room on EVERY think tick has a degenerate
				// equilibrium on the phase-2 house: on the first tread of the ground
				// stair, at (-0.62,-11.73), br2 (up one storey, 3.18 m of plan) and
				// baseOpen (down one storey, 3.18 m of plan) tie to within 0.003 m
				// after the symmetric 12 m storey penalties, and the tread sits on the
				// perpendicular bisector -- a step north makes baseOpen nearer, a step
				// south makes br2 nearer. Position jitter of +/-0.03 m flips the winner
				// every tick, every flip rebuilds the route (4 nodes up vs 7 nodes
				// down), and the walk alternates with zero net travel. Measured in the
				// TraceProbe battery: a CLEAN run (homeowner on the lawn, zero
				// interference) delivered the TV at 35.2 s and the Safe at 95.6 s and
				// then spent every remaining second of the 180 s spec oscillating on
				// that tread; MovingHeist modes 3 and 4 died at the same point, and the
				// GroupHeist replay parked ALL THREE intruders on it (inside the stair
				// band, where the separation exemption lets bodies stand overlapped)
				// for 300+ s with stuck=0.00 and no reroutes -- each frame IS a real
				// walking step, so the stuck detector sees a thief that is moving,
				// because he is; it is the TARGET that will not stand still.
				//
				// Committing until the room is searched keeps the ordering strictly
				// nearest-unsearched-at-completion, still adaptive between rooms. It
				// gives up the benign mid-flight flip (br2->office at 0.05 m in the
				// 2026-10-15 trace, endorsed there as "correct AI"): the same
				// mechanism at a harmless amplitude, which the degenerate case
				// outweighs. Godot's sim.gd re-picks every tick like the old code did;
				// the v1 slice had no two rooms a symmetric storey apart on a shared
				// stair approach, so the pathology is phase-2 geometry. (Not
				// verifiable against sim.gd from this machine.)
				FString Room;
				const bool bCommitted = !Thief.Target.IsEmpty()
					&& !Thief.Searched.Contains(Thief.Target)
					&& SearchNodes().Contains(Thief.Target);
				// STANDING IN AN UNSEARCHED ROOM OVERRIDES THE COMMITMENT (2026-10-18):
				// the commitment is about WALKING targets; a burglar standing in a room
				// looks around it. Without the override the Searching pin fails (first
				// full-suite execution of the commitment, run 21): teleported into the
				// living room while committed to the room nearest his spawn, he turned
				// around and walked back out -- searched=0, tv known=0 in the single
				// frame the pin allows. The override CANNOT reintroduce the bisector
				// oscillation above: that equilibrium sits on a stair tread where both
				// tied rooms are on other floors and the floor-match gate excludes
				// them, and the override consumes itself -- the mark fires the same
				// frame, the room leaves Rooms, and it can never re-trigger.
				FString Standing;
				for (const FString& N : Rooms)
				{
					const int32 NIdx = NodeNames().IndexOfByKey(N);
					if (NIdx != INDEX_NONE
						// POP RADIUS, NOT SEARCH RANGE (2026-10-18): the override means
						// STANDING IN THE ROOM, and 0.6 m -- the waypoint pop radius --
						// is the honest measure of that. A routed arrival always gets
						// inside it, so genuine room entry still fires the mark; the
						// 0.6-1.2 m annulus is doorway-and-wall territory, where plan
						// proximity is not presence (the search twin of the grab
						// z-gate). The walkable-line gate below guards the screen class
						// of solids, whose own floor tags lie (see WalkBlockedBody).
						// HISTORY, WRONG-GUESS-RECORDED: the first version used
						// SearchRange ungated, and the run 22-24 stalls it was blamed
						// for (E 330 s / 85 reroutes; B2's 8 + 10-reroute doorway
						// stalls, dead at 180 s with 2/3) turned out to be
						// HuntRouteKey v2's fault instead -- three override parameter
						// sets (1.2 ungated / 1.2 gated / 0.6 gated) produced
						// byte-identical probe runs, proving the override inert on
						// those paths. 0.6 stayed because it is the right semantics,
						// not because it changed a measured outcome.
						&& FVector2D::Distance(TPos, NodePositions()[NIdx]) < 0.6f
						&& Thief.Floor == NodeFloors()[NIdx]
						&& !WalkBlockedBody(TPos, NodePositions()[NIdx], Thief.Floor, 0.0f))
					{
						Standing = N;
						break;
					}
				}
				if (!Standing.IsEmpty())
				{
					Room = Standing;
				}
				else if (bCommitted)
				{
					Room = Thief.Target;
				}
				else
				{
					Room = Rooms[0];
					for (const FString& N : Rooms)
					{
						const int32 NIdx = NodeNames().IndexOfByKey(N);
						const int32 RIdx = NodeNames().IndexOfByKey(Room);
						if (NIdx == INDEX_NONE || RIdx == INDEX_NONE)
						{
							continue;
						}
						if (FloorDist(NodePositions()[NIdx], NodeFloors()[NIdx])
							< FloorDist(NodePositions()[RIdx], NodeFloors()[RIdx]))
						{
							Room = N;
						}
					}
				}
				if (Thief.Target != Room)
				{
					Thief.Target = Room;
					Thief.Route = RouteFromClear(TPos, Thief.Floor, Room, &Thief);
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
					Thief.Route = RouteFromClear(TPos, Thief.Floor, Room, &Thief);
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
							// Same floor wins outright: 1e6 is not a distance, it is a storey
							// change. Only a floor with no other node falls through to the next one.
							const float D = FVector2D::DistSquared(Poses[Idx], TPos)
								+ (NodeFloors()[Idx] == Thief.Floor ? 0.0f : 1e6f);
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
					&& FVector2D::Distance(TPos, NodePositions()[RoomIdx]) < SearchRange
					// Plan proximity is not presence: the `upstairs` node sits 3 m above
					// the living room IN PLAN. He searches the room only from its own floor.
					&& Thief.Floor == NodeFloors()[RoomIdx])
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
	// Prepend-hysteresis clock (2026-10-19): ages every step, reset by a waypoint pop.
	if (Thief.LastPoppedAgo < 1e8f)
	{
		Thief.LastPoppedAgo += Dt;
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
			// Prepend hysteresis bookkeeping (2026-10-19): remember WHAT was popped and
			// start the clock -- RouteFromClear consults it to stop the flip cycle.
			Thief.LastPoppedWp = WP;
			Thief.LastPoppedAgo = 0.0f;
			Thief.Route.RemoveAt(0);
			continue;
		}
		float A = FMath::Atan2(WP.Y - TPos.Y, WP.X - TPos.X);   // (wp - tpos).angle()
		// Waypoint dwell: the orbit breaker below needs to know how long THIS waypoint has
		// been the target. Reset on change -- a rebuilt route with the same first waypoint
		// keeps the clock, which is the point: the trap is per-waypoint, not per-route.
		if (!WP.Equals(Thief.LastWp, 1e-3f))
		{
			Thief.LastWp = WP;
			Thief.WpT = 0.0f;
			// The corner-rounding commitment dies with the waypoint, for the same
			// reason: it is a choice about THIS leg's geometry. A reroute that keeps
			// the same first waypoint keeps the commitment, deliberately -- same
			// wall, same side.
			Thief.CornerSide = 0;
			Thief.CornerSideT = 0.0f;
			Thief.CornerDir = FVector2D::ZeroVector;
		}
		Thief.WpT += Dt;
		if (Thief.CornerSide != 0)
		{
			// Staleness bound: 3 s of committed stepping is ~3.9 m at full pace, more
			// than any face in this house (the stairwell screen runs 2.83 m). If he is
			// STILL rounding after that, the situation is not a corner -- release and
			// let the stuck detectors own him.
			Thief.CornerSideT += Dt;
			if (Thief.CornerSideT > 3.0f)
			{
				Thief.CornerSide = 0;
				Thief.CornerSideT = 0.0f;
				Thief.CornerDir = FVector2D::ZeroVector;
			}
		}
		// No avoidance orbit inside a stair band and none above the ground floor.
		// The band is single file -- a deflection steers a climber into the stringer --
		// and the slice's upstairs is one open room with nothing to pincer on. The
		// inherited ground-floor behaviour, pincer fix included, is untouched.
		// ...and none forever: after OrbitBreakTime on one waypoint he walks straight at
		// it and shoulders past (the separation rule already says the homeowner yields,
		// and ThiefShove is firing by then anyway). Without this the tangential orbit
		// around a camped waypoint node is a stable attractor -- the MovingHeist trace
		// ran it for 120 s at stuck=0.00, moving every frame, arriving never.
		if (Thief.Floor == 0 && !InStairBand(TPos) && Thief.WpT < OrbitBreakTime)
		{
			A = SteerAround(A, TPos, PPos);
		}
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
		// Stairs cost pace: the flight is climbed at StairPace whatever the hurry.
		const float Pace = InStairBand(TPos) ? SpeedScale * StairPace : SpeedScale;
		const float Wish = ThiefSpeed * Pace * Align;
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
			const FVector2D Resolved = ResolveCircleZ(Stepped, ThiefRadius, Thief.Z);
			if (!Resolved.Equals(Stepped, 1e-4f))
			{
				FVector2D PushN = Resolved - Stepped;              // out of the solid
				PushN.Normalize();
				const FVector2D StepDir(FMath::Cos(A), FMath::Sin(A));
				FVector2D Tangent = StepDir - StepDir.Dot(PushN) * PushN;
				const FVector2D Perp(-PushN.Y, PushN.X);
				if (Thief.CornerSide != 0)
				{
					// COMMITTED ROUNDING (measured, run 17): the corner side is chosen
					// once, on the first deadzone frame, and then EVERY contact frame
					// steps along it -- including frames where the projection becomes
					// tangential again. Without the commitment the two branches fight:
					// the tangential slide aims at the waypoint and is a restoring force
					// toward its isocline, the deadzone rounding pushes away from it,
					// and the pin becomes a harmonic orbit -- E ran y -14.62 <-> -15.00
					// about br3's y -14.80, amplitude exactly the 0.19 m where the
					// bearing's y component crosses the sqrt(0.0025) deadzone, ~196
					// windowed-detector reroutes in 400 s, zero net travel. The
					// commitment outlives free frames (run 18: clearing it on the corner's
					// one push-less frame let the tangential branch march him back up the
					// face -- a 2.8 m shuttle at full pace, stuck=0.00 throughout); it ends
					// at the waypoint change or the 3 s staleness bound, both above.
					//
					// THE DIRECTION IS FROZEN AT PICK TIME (run 19): recomputing Perp
					// from the live push normal rotates it toward the face inside a
					// corner's push-out, and the 0.5 m probe along it then hits the
					// body-radius-inflated end of the very face being rounded -- the
					// commitment flipped at both ends and he patrolled the full 2.83 m
					// at full pace, stuck=0.00, wpt climbing past every detector, E/F0
					// still dead at 400 s. CornerDir is the Perp of the frame that made
					// the commitment; only the probes can re-aim it (the flip).
					const FVector2D ProbeFromC = Resolved + PushN * 0.05f;
					if (WalkBlockedBody(ProbeFromC, ProbeFromC + Thief.CornerDir * 0.5f,
							Thief.Floor, ThiefRadius)
						&& !WalkBlockedBody(ProbeFromC, ProbeFromC - Thief.CornerDir * 0.5f,
							Thief.Floor, ThiefRadius))
					{
						// Walled ahead, open behind: flip and keep the flip.
						Thief.CornerSide = -Thief.CornerSide;
						Thief.CornerDir = -Thief.CornerDir;
					}
					if (WalkBlockedBody(ProbeFromC, ProbeFromC + Thief.CornerDir * 0.5f,
							Thief.Floor, ThiefRadius))
					{
						TPos = Resolved;   // both sides walled: a pocket, not a corner -- stand
					}
					else
					{
						TPos = ResolveCircleZ(
							Resolved + Thief.CornerDir * Thief.Step, ThiefRadius, Thief.Z);
					}
				}
				else if (Tangent.SizeSquared() > 0.0025f)               // head-on contact: no slide
				{
					TPos = ResolveCircleZ(Resolved + Tangent.GetSafeNormal() * Thief.Step, ThiefRadius, Thief.Z);
				}
				else
				{
					// HEAD-ON DEADZONE: ROUND THE CORNER -- deliberate divergence #2 from
					// the ported walk, added 2026-10-18, and the third recorded member of
					// one family: the 2026-10-15 stringer-top statue, the (0.27,-14.61)
					// east-face pin that got the screenPass node, and now the second-floor
					// rim pin measured in TraceProbe E/F0 runs 15-16 -- a searcher at
					// (0.27,-14.9) f1, due west of br3, held at the screen's push-out
					// equilibrium x = -0.15 + 0.42 (the screen's span [0,3.74] makes its
					// top metre the slab hole's east-lip railing), yaw 3.14 dead into it,
					// slide tangent |T|^2 ~ 0.001 under the 0.0025 deadzone, 220 reroutes
					// in 400 s and every clean run dead with the laptop undelivered. The
					// body-radius prepend guard (WalkBlockedBody) DOES fire here -- the
					// reroute log alternates route 1<->2 on the 1.2 s stuck cadence -- and
					// is useless alone: the prepended From (upHallN) sits 0.7 m away, the
					// 0.6 m waypoint pop re-releases it almost immediately, and the
					// post-pop re-aim at br3 passes 0.36 m from the screen's south-end
					// corner, inside the 0.42 body. Routing cannot see that; only the
					// contact can. So when the projection deadzones, step along the face
					// instead of standing: a physical body pressed head-on into a wall
					// with anywhere to go along it does not hover.
					//
					// THE SIDE IS THE WAYPOINT'S SIDE, chosen once and kept (the
					// commitment above): per-frame re-choosing dithers across the
					// waypoint's isocline -- br3's y is 0.04-0.20 m from the pin's, so
					// the sign flips as the body crosses it.
					//
					// THE PROBES START 5 cm OFF THE FACE: at push-out equilibrium the
					// centre sits at exactly body-radius distance, i.e. ON the inflated
					// boundary, and SegRect is inclusive -- probing from Resolved itself
					// reads "blocked" along the whole face, both directions, always.
					//
					// Both sides walled stays a stand (TPos = Resolved, the old
					// behaviour): that is a real pocket, not a corner, and the windowed
					// stuck detector owns it. Floor-sloppiness is not a risk: search
					// marks and grabs are floor-gated ("plan proximity is not presence"),
					// and if the rounding walks him off the slab hole he lands on the
					// flight below and the empty-route self-heal stairs him back up.
					Thief.CornerSide = (Thief.Route.Num() > 0
						&& (Thief.Route[0] - TPos).Dot(Perp) < 0.0f) ? -1 : 1;
					Thief.CornerSideT = 0.0f;
					Thief.CornerDir = Perp * (float)Thief.CornerSide;
					const FVector2D ProbeFrom = Resolved + PushN * 0.05f;
					if (WalkBlockedBody(ProbeFrom, ProbeFrom + Thief.CornerDir * 0.5f,
							Thief.Floor, ThiefRadius)
						&& !WalkBlockedBody(ProbeFrom, ProbeFrom - Thief.CornerDir * 0.5f,
							Thief.Floor, ThiefRadius))
					{
						Thief.CornerSide = -Thief.CornerSide;
						Thief.CornerDir = -Thief.CornerDir;
					}
					if (WalkBlockedBody(ProbeFrom, ProbeFrom + Thief.CornerDir * 0.5f,
							Thief.Floor, ThiefRadius))
					{
						TPos = Resolved;
					}
					else
					{
						TPos = ResolveCircleZ(
							Resolved + Thief.CornerDir * Thief.Step, ThiefRadius, Thief.Z);
					}
				}
			}
			else
			{
				TPos = Resolved;
				// CornerSide deliberately SURVIVES a free frame: rounding a face end
				// produces one push-less frame as the body clears the corner, and
				// clearing there lets the tangential branch walk him straight back up
				// the face (measured, run 18: south end rounded at y -12.20, one free
				// frame, then the tangential slide -- br3's bearing on that side of its
				// isocline -- marched him north again: a 2.8 m shuttle at full pace,
				// stuck=0.00 throughout, windowed detector blind to it). The commitment
				// ends at the waypoint change or the 3 s staleness bound.
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
	// Wherever the step landed him is where the floor is. Derived, never integrated.
	ApplyElevation(Thief.Pos, Thief.Floor, Thief.Z);

	// ---- solid bodies. Applied once both have moved; the homeowner's own step is taken
	// by the caller before this. Skipped when step_intruders resolves the group at once.
	if (bSeparate)
	{
		const TArray<FVector2D> Pair = SeparateBodiesZ(PPos, Player.Z, TPos, Thief.Z);
		PPos = Pair[0];
		TPos = Pair[1];
		Player.Pos = PPos;
		ApplyElevation(Player.Pos, Player.Floor, Player.Z);
		Thief.Pos = TPos;
		ApplyElevation(Thief.Pos, Thief.Floor, Thief.Z);
	}

	// ---- stuck detection: re-route rather than stand still forever.
	// Lives in StuckStep since 2026-10-18 because the GROUP path needs the same accounting
	// and can only make it after `StepIntruders` has resolved the crowd -- measuring inside
	// this function, on the group path, sees the walk but never the push-back that undoes it
	// (see StuckStep's call site there). TPos at this point is already post-separation, so
	// Dist(TPos, Prev) is exactly the net travel the helper wants.
	StuckStep(Thief, FVector2D::Distance(TPos, Thief.Prev), Dt, Loot, Events);

	// ---- aim, then shoot. Sight is recomputed AFTER moving: he can step behind cover
	// during this same step, and firing through a wall would be a bug.
	// 3-D sight: eyes at Z + EyeHeight against every floor's walls, the slab with
	// its hole, the railings and both roofs -- exact span clipping, so looking UP the
	// stairwell through the hole works and looking through the deck does not.
	const bool bCan = Player.bAlive && ThiefSees3D(
		FVector(TPos.X, TPos.Y, Thief.Z), Thief.Yaw,
		FVector(PPos.X, PPos.Y, Player.Z));
	if (bCan)
	{
		Thief.Aim = FMath::Min(Thief.Aim + Dt, ThiefAimTime);
		// First direct sight of the homeowner is the "he spots you" bark (2026-10-19):
		// item 16 documented it as firing; the hook never existed. One per man -- the latch
		// keeps a break-then-resight silent, which is right: re-barks are noise, not news.
		if (!Thief.bSpottedPlayer)
		{
			Thief.bSpottedPlayer = true;
			FHIEvent SpotEv;
			SpotEv.Type = TEXT("thiefSpot");
			SpotEv.Id = Thief.Id;
			Events.Add(SpotEv);
		}
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
	// SELF-CONSISTENCY v3 (2026-10-16, phase 2). v1 asserted Godot's literals, v2 the
	// slice's; v3 asserts the full house -- and keeps v2's principle: counts pin the
	// tables against accidental edits, but the interesting checks are COMPUTED, because
	// a count can be right while the geometry is nonsense. Every number here was derived
	// by hand at authoring time; the check's job is to make sure the file still says what
	// the derivation proved. A check that cannot fail is not a check.
	checkf(WallRows().Num() == 32, TEXT("expected 32 wall rows (9 exterior + door transom, 10 ground, 10 second, 2 basement), got %d"), WallRows().Num());
	checkf(Walls().Num() == 15, TEXT("expected 15 ground-floor walls, got %d"), Walls().Num());
	checkf(House.Min.Equals(FVector2D(-6.35f, -17.39f)), TEXT("house min wrong (outer shell convention)"));
	checkf(House.Max.Equals(FVector2D(6.35f, -7.75f)), TEXT("house max wrong (outer shell convention)"));
	checkf(Stairs().Num() == 2, TEXT("expected 2 stair bands (G->2, B->G), got %d"), Stairs().Num());
	checkf(FurnitureRects().Num() == 48, TEXT("expected 48 furniture rows (8 basement, 24 ground, 16 second), got %d"), FurnitureRects().Num());
	checkf(FurnitureFloors().Num() == FurnitureRects().Num(), TEXT("furniture floor/rect mismatch"));
	checkf(NodeNames().Num() == 26, TEXT("expected 26 nav nodes (14 ground, 4 basement, 8 second), got %d"), NodeNames().Num());
	checkf(NodePositions().Num() == NodeNames().Num(), TEXT("nav node pos/name mismatch"));
	checkf(NodeEdges().Num() == NodeNames().Num(), TEXT("nav node edge/name mismatch"));
	checkf(NodeFloors().Num() == NodeNames().Num(), TEXT("nav node floor/name mismatch"));
	checkf(LootPositions().Num() == 3, TEXT("expected 3 loot spots"));
	checkf(LootLabels().Num() == 3 && LootNodes().Num() == 3 && LootFloors().Num() == 3, TEXT("loot array mismatch"));
	for (int32 Idx = 0; Idx < NodeFloors().Num(); ++Idx)
	{
		checkf(NodeFloors()[Idx] >= -1 && NodeFloors()[Idx] <= 1,
			TEXT("node '%s' on floor %d -- FloorHeight's domain is -1..1"),
			*NodeNames()[Idx], NodeFloors()[Idx]);
	}

	// Every search node must exist, and every loot spot's node must be searchable or the
	// item can never be discovered and the round cannot be completed.
	for (const FString& S : SearchNodes())
	{
		checkf(NodeNames().Contains(S), TEXT("search node '%s' is not a nav node"), *S);
	}
	for (int32 Idx = 0; Idx < LootNodes().Num(); ++Idx)
	{
		checkf(SearchNodes().Contains(LootNodes()[Idx]),
			TEXT("loot '%s' sits in node '%s', which is not searchable"),
			*LootLabels()[Idx], *LootNodes()[Idx]);
	}

	// ---- the graph must be SYMMETRIC (BFS walks edges one way only; an asymmetric edge
	// is a route there and no route back) and every node must be reachable from the van,
	// or a thief can be ordered somewhere he can never go -- the shape of the 2026-10-15
	// frontHub freeze, checked at load time instead of discovered at runtime.
	const TArray<FString>& Names = NodeNames();
	const TArray<TArray<FString>>& Edges = NodeEdges();
	for (int32 Idx = 0; Idx < Names.Num(); ++Idx)
	{
		for (const FString& Next : Edges[Idx])
		{
			const int32 Jdx = Names.IndexOfByKey(Next);
			checkf(Jdx != INDEX_NONE, TEXT("edge from '%s' to unknown node '%s'"),
				*Names[Idx], *Next);
			checkf(Edges[Jdx].Contains(Names[Idx]),
				TEXT("edge '%s'->'%s' is not symmetric"), *Names[Idx], *Next);
		}
	}
	{
		TSet<FString> Reach;
		TArray<FString> Queue{ TEXT("van") };
		Reach.Add(TEXT("van"));
		while (Queue.Num() > 0)
		{
			const FString Cur = Queue.Pop();
			const int32 Idx = Names.IndexOfByKey(Cur);
			for (const FString& Next : Edges[Idx])
			{
				if (!Reach.Contains(Next))
				{
					Reach.Add(Next);
					Queue.Add(Next);
				}
			}
		}
		checkf(Reach.Num() == Names.Num(),
			TEXT("nav graph not fully reachable from van: %d of %d"), Reach.Num(), Names.Num());
	}

	// ---- stairs: a stair edge is exactly a floor change, floors change by exactly one
	// per flight, and every stair edge's endpoints are carried by a band whose
	// FloorLow/FloorHigh are exactly those two floors -- an edge cannot claim a flight
	// that is not there. Carried means ON the band rect, except that the high end may
	// stand on the arrival landing just past the top tread (the as-built rule below).
	for (int32 Idx = 0; Idx < Names.Num(); ++Idx)
	{
		for (const FString& Next : Edges[Idx])
		{
			const int32 Jdx = Names.IndexOfByKey(Next);
			const bool bStair = IsStairEdge(Names[Idx], Next);
			const int32 DFl = NodeFloors()[Jdx] - NodeFloors()[Idx];
			checkf(bStair == (DFl != 0),
				TEXT("edge '%s'->'%s': stair/floor mismatch"), *Names[Idx], *Next);
			checkf(!bStair || FMath::Abs(DFl) == 1,
				TEXT("edge '%s'->'%s' skips storeys"), *Names[Idx], *Next);
			if (bStair)
			{
				bool bOnABand = false;
				for (const FHIStairBand& B : Stairs())
				{
					const bool bFloors = (B.FloorLow == NodeFloors()[Idx] && B.FloorHigh == NodeFloors()[Jdx])
						|| (B.FloorHigh == NodeFloors()[Idx] && B.FloorLow == NodeFloors()[Jdx]);
					if (!bFloors)
					{
						continue;
					}
					// Which end is the high one depends on the edge's direction; the low
					// foot must stand ON the band, full stop.
					const bool bIdxHigh = NodeFloors()[Idx] == B.FloorHigh;
					const FVector2D Low = NodePositions()[bIdxHigh ? Jdx : Idx];
					const FVector2D High = NodePositions()[bIdxHigh ? Idx : Jdx];
					bool bEnds = B.Contains(Low);
					if (!B.Contains(High))
					{
						// AS-BUILT 2026-10-18: the high end may instead stand on the
						// ARRIVAL LANDING -- within 1.0 m past the top tread along the
						// run, inside the band's x span, on a real deck at the arrival
						// floor's height. stairTopG sits 0.47 m north of G2's top tread
						// on the slab's north piece (moved off the stairwell screen's
						// corner scrape line -- see the node table); the band rects stay
						// flush with the physical flight so HeightAt/BandNearestZ never
						// read treads where a slab carries the body (Stairs()'s "the
						// north end IS the band top" note). The x span is what rejects
						// a room node posing as a landing: upHall (0.40,-11.50) has a
						// floor-1 deck under it but sits 0.60 m east of the span.
						const FVector2D Run = (B.Top - B.Bottom).GetSafeNormal();
						const float Along = FVector2D::DotProduct(High - B.Top, Run);
						bEnds = Along > 0.0f && Along <= 1.0f
							&& High.X >= B.Rect.Min.X && High.X <= B.Rect.Max.X
							&& DeckCovers(High, B.FloorHigh);
					}
					bOnABand |= bEnds;
				}
				checkf(bOnABand, TEXT("stair edge '%s'->'%s' has no band carrying it"),
					*Names[Idx], *Next);
			}
		}
	}

	// ---- headroom: anywhere on a flight where a standing body's head clears the slab
	// above, no deck may cover that spot. 21 samples per flight is one per ~20 cm --
	// finer than a tread, so no hole can hide between samples. This is the check the
	// first draft of the slice house failed (stairs arriving under a 3.0 m roof), and
	// phase 2's stacked flights make it load-bearing twice: the G->2 run passes UNDER
	// the slab for its first metre, and through the slab's hole for the rest.
	for (const FHIStairBand& B : Stairs())
	{
		for (int32 K = 0; K <= 20; ++K)
		{
			const FVector2D P = B.Bottom + (B.Top - B.Bottom) * (K / 20.0f);
			const float H = B.HeightAt(P);
			const float Head = H + BodyHeight;
			for (const FHISolid& S : WorldSolids())
			{
				if (!S.bDeck)
				{
					continue;
				}
				const bool bOverlaps = H < S.Span.MaxZ && Head > S.Span.MinZ;
				checkf(!(bOverlaps && S.Rect.IsInsideOrOn(P)),
					TEXT("stair headroom violated at (%.2f, %.2f) h=%.2f by deck [%.2f,%.2f]"),
					P.X, P.Y, H, S.Span.MinZ, S.Span.MaxZ);
			}
		}
		// The band's top must arrive ON its upper floor THROUGH the hole in the deck
		// above. The top riser is FLUSH with the hole's edge by design (WorldSolids: the
		// deck piece starts exactly at the flight's top), so the arrival point itself
		// sits on a deck BOUNDARY and DeckCovers -- inclusive -- says yes. What must hold
		// instead: the hole extends at least a step back DOWN the flight (a top riser
		// under solid slab is a head-bang), and one step further north is solid landing
		// (a hole that ends short of the top riser is a ledge to fall off, and one that
		// overshoots is a gap in the landing). WRONG GUESS RECORDED: the first draft
		// asserted !DeckCovers(B.Top) and fired on its first ever run (2026-10-18) --
		// it contradicted both the flush-edge deck layout and the ApplyElevation pin two
		// checks below it.
		const FVector2D Back(B.Top.X, B.Top.Y + 0.10f);    // 0.10 back down the flight (south)
		checkf(!DeckCovers(Back, B.FloorHigh),
			TEXT("stair top (%.2f, %.2f): the hole does not reach 0.10 m back down the flight -- arriving UNDER a deck"),
			Back.X, Back.Y);
		const FVector2D Past(B.Top.X, B.Top.Y - 0.10f);   // flights run north; landing is north
		checkf(DeckCovers(Past, B.FloorHigh),
			TEXT("one step past stair top (%.2f, %.2f) is a hole, not a landing"), Past.X, Past.Y);
		int32 Fl = B.FloorHigh;
		float Z = FloorHeight(B.FloorHigh);
		ApplyElevation(B.Top, Fl, Z);
		checkf(Fl == B.FloorHigh && FMath::IsNearlyEqual(Z, B.TopH, 0.01f),
			TEXT("stair top does not arrive on floor %d (got %d, z=%.2f)"), B.FloorHigh, Fl, Z);
		// The bottom, approached from ITS floor's Z, must be claimed by its own flight:
		// at (−0.7, −11.1) both bands overlap in plan, and the ground-floor walker must
		// get G->2 (d 0.035) while the basement walker gets B->G (d 0) -- never the far
		// flight's treads. This is the Z-proximity veto's reason to exist.
		int32 FlLow = B.FloorLow;
		float ZLow = FloorHeight(B.FloorLow);
		ApplyElevation(B.Bottom, FlLow, ZLow);
		checkf(FlLow == B.FloorLow && FMath::IsNearlyEqual(ZLow, B.BottomH, 0.01f),
			TEXT("stair bottom not claimed by its own flight from floor %d (got %d, z=%.2f)"),
			B.FloorLow, FlLow, ZLow);
	}
	// The two stacked feet: a ground-floor body at the shared foot rect gets G->2's
	// treads, never B->G's (2.4 m below), and vice versa from the basement.
	{
		const FVector2D Foot(-0.70f, -11.15f);
		int32 Fl = 0;
		float Z = 0.0f;
		ApplyElevation(Foot, Fl, Z);
		checkf(Fl == 0 && Z > -0.1f, TEXT("ground walker at the stacked foot fell to floor %d z=%.2f"), Fl, Z);
		Fl = -1;
		Z = BasementZ;
		ApplyElevation(Foot, Fl, Z);
		checkf(Fl == -1 && Z < BasementZ + 0.1f, TEXT("basement walker at the stacked foot was lifted to floor %d z=%.2f"), Fl, Z);
	}

	// ---- nodes must be STANDABLE: ApplyElevation from the node's own carried state is
	// a fixed point within 0.35 m of the floor height (stair nodes sit on treads, up to
	// a riser off), keeps the node's floor, and the body disc (radius inflated) touches
	// no wall or solid furniture on that floor. A node inside a wardrobe is a thief
	// frozen inside a wardrobe -- the pinch class the 0.84 m body turned into two
	// measured 70+ s statues in the slice.
	for (int32 Idx = 0; Idx < Names.Num(); ++Idx)
	{
		const FVector2D P = NodePositions()[Idx];
		const int32 Fl = NodeFloors()[Idx];
		int32 RFl = Fl;
		float RZ = FloorHeight(Fl);
		ApplyElevation(P, RFl, RZ);
		checkf(RFl == Fl, TEXT("node '%s' (floor %d) resolves to floor %d"), *Names[Idx], Fl, RFl);
		checkf(FMath::Abs(RZ - FloorHeight(Fl)) <= 0.35f,
			TEXT("node '%s' resolves to z=%.2f, %.2f from its floor height"),
			*Names[Idx], RZ, FMath::Abs(RZ - FloorHeight(Fl)));
		int32 RFl2 = RFl;
		float RZ2 = RZ;
		ApplyElevation(P, RFl2, RZ2);
		checkf(RFl2 == RFl && FMath::IsNearlyEqual(RZ2, RZ, 0.001f),
			TEXT("node '%s' does not settle: z %.3f -> %.3f"), *Names[Idx], RZ, RZ2);
		for (const FHISolid& S : WorldSolids())
		{
			if (S.bDeck || S.Floor != Fl)
			{
				continue;
			}
			const bool bBodyHeight = FloorHeight(Fl) < S.Span.MaxZ
				&& FloorHeight(Fl) + BodyHeight > S.Span.MinZ;
			const bool bDisc = P.X > S.Rect.Min.X - ThiefRadius && P.X < S.Rect.Max.X + ThiefRadius
				&& P.Y > S.Rect.Min.Y - ThiefRadius && P.Y < S.Rect.Max.Y + ThiefRadius;
			checkf(!(bBodyHeight && bDisc),
				TEXT("node '%s' body overlaps solid (%.2f,%.2f)-(%.2f,%.2f)"),
				*Names[Idx], S.Rect.Min.X, S.Rect.Min.Y, S.Rect.Max.X, S.Rect.Max.Y);
		}
	}
	// The player spawn gets the same treatment -- she must not start inside the
	// armchair, and Louise's spot is the one position in the house every balance
	// decision is measured from.
	{
		const FHIPlayer P0 = CreatePlayer();
		int32 Fl = P0.Floor;
		float Z = P0.Z;
		ApplyElevation(P0.Pos, Fl, Z);
		checkf(Fl == P0.Floor && FMath::IsNearlyEqual(Z, P0.Z, 0.01f),
			TEXT("player spawn does not settle on floor %d"), P0.Floor);
	}

	// ---- flat-floor edges must be WALKABLE as drawn: RouteTo walks straight lines
	// between waypoints, so an edge whose line crosses a wall (anywhere but its doorway)
	// is a thief face-planted into plaster, shuffling forever. Stair edges are exempt --
	// they ride their band, checked above -- and the deck/railing spans do not block
	// their own floor's walking by construction (WalkBlocked skips decks).
	for (int32 Idx = 0; Idx < Names.Num(); ++Idx)
	{
		for (const FString& Next : Edges[Idx])
		{
			if (IsStairEdge(Names[Idx], Next))
			{
				continue;
			}
			const int32 Jdx = Names.IndexOfByKey(Next);
			checkf(NodeFloors()[Idx] == NodeFloors()[Jdx],
				TEXT("flat edge '%s'->'%s' spans floors without being a stair edge"),
				*Names[Idx], *Next);
			if (WalkBlocked(NodePositions()[Idx], NodePositions()[Jdx], NodeFloors()[Idx]))
			{
				// Name the culprit: replay WalkBlocked's own loop solid by solid so the
				// crash log says WHICH rectangle blocks the walk line, not just that one
				// does. A blocked edge with no named solid would mean the replay and
				// WalkBlocked disagree, which is its own (worse) bug.
				const float Z = FloorHeight(NodeFloors()[Idx]);
				for (const FHISolid& S : WorldSolids())
				{
					if (S.bDeck || S.Floor != NodeFloors()[Idx])
					{
						continue;
					}
					if (!(Z < S.Span.MaxZ && Z + BodyHeight > S.Span.MinZ))
					{
						continue;
					}
					checkf(!SegRect(NodePositions()[Idx], NodePositions()[Jdx], S.Rect),
						TEXT("edge '%s'->'%s' (floor %d) is blocked by solid (%.2f,%.2f)-(%.2f,%.2f) span [%.2f,%.2f]"),
						*Names[Idx], *Next, NodeFloors()[Idx],
						S.Rect.Min.X, S.Rect.Min.Y, S.Rect.Max.X, S.Rect.Max.Y,
						S.Span.MinZ, S.Span.MaxZ);
				}
				checkf(false,
					TEXT("edge '%s'->'%s' is blocked on floor %d but no single solid reproduces it"),
					*Names[Idx], *Next, NodeFloors()[Idx]);
			}
		}
	}

	// ---- furniture discipline: nothing overlaps a wall on its own floor (flat or not --
	// a rug sticking through a wall is the visible version of the same authoring slip),
	// and no two SOLID items overlap (a rug under a table is normal; a table inside a
	// fridge is not).
	{
		const TArray<FBox2D>& Rects = FurnitureRects();
		const TArray<bool>& Flat = FurnitureFlat();
		const TArray<int32>& Floors = FurnitureFloors();
		// Touching is flush placement, not overlap: the beds meet the west wall, the
		// counters meet the fridge at a corner, the TV stand meets the y -13 wall --
		// that is how furniture is authored. FBox2D::Intersect counts edge contact as
		// intersection, so measure the overlap DEPTH and require real penetration.
		// (1e-4: authoring is on a 0.01 grid; FP noise from computed rows is ~1e-7.
		// Caught by reasoning, not by a run, on 2026-10-18: this check had never
		// executed -- earlier checks crashed first -- and would have fired on every
		// flush-placed piece the moment the wall rows were fixed.)
		auto ReallyOverlaps = [](const FBox2D& A, const FBox2D& B)
		{
			const float Ox = FMath::Min(A.Max.X, B.Max.X) - FMath::Max(A.Min.X, B.Min.X);
			const float Oy = FMath::Min(A.Max.Y, B.Max.Y) - FMath::Max(A.Min.Y, B.Min.Y);
			return Ox > 1e-4f && Oy > 1e-4f;
		};
		for (int32 Idx = 0; Idx < Rects.Num(); ++Idx)
		{
			for (const FHIWallRow& Row : WallRows())
			{
				if (Row.Floor != Floors[Idx])
				{
					continue;
				}
				checkf(!ReallyOverlaps(Rects[Idx], Row.Rect),
					TEXT("furniture #%d (floor %d) overlaps a wall (%.2f,%.2f)-(%.2f,%.2f)"),
					Idx, Floors[Idx], Row.Rect.Min.X, Row.Rect.Min.Y, Row.Rect.Max.X, Row.Rect.Max.Y);
			}
			if (Flat[Idx])
			{
				continue;
			}
			for (int32 Jdx = Idx + 1; Jdx < Rects.Num(); ++Jdx)
			{
				if (Flat[Jdx] || Floors[Jdx] != Floors[Idx])
				{
					continue;
				}
				checkf(!ReallyOverlaps(Rects[Idx], Rects[Jdx]),
					TEXT("solid furniture #%d and #%d overlap on floor %d"),
					Idx, Jdx, Floors[Idx]);
			}
		}
		// And no furniture may stand in a stairwell hole: it would hang over the open
		// stair, blocking the flights and drawing a couch in mid-air.
		const FBox2D GroundHole(FVector2D(-1.25f, -14.74f), FVector2D(-0.15f, -11.05f));
		const FBox2D SlabHole(FVector2D(-1.25f, -15.03f), FVector2D(-0.15f, -12.20f));
		for (int32 Idx = 0; Idx < Rects.Num(); ++Idx)
		{
			if (Floors[Idx] == 0)
			{
				checkf(!Rects[Idx].Intersect(SlabHole),
					TEXT("ground furniture #%d hangs over the second-floor stairwell hole"), Idx);
			}
			else if (Floors[Idx] == 1)
			{
				checkf(!Rects[Idx].Intersect(SlabHole),
					TEXT("second-floor furniture #%d falls through the stairwell hole"), Idx);
			}
			checkf(Floors[Idx] != -1 || !Rects[Idx].Intersect(GroundHole),
				TEXT("basement furniture #%d blocks the stairwell from below"), Idx);
		}

		// ---- AS-BUILT 2026-10-18, the furniture ENVELOPE rule: every nav edge's walk
		// line must clear every non-flat furniture rect on the edge's floor(s) by a full
		// ThiefRadius. WalkBlocked-clear at radius 0 is not enough: ResolveCircleZ
		// deflects a body off anything within 0.42, and a deflection on a near-
		// perpendicular bearing lands in the wall-slide tangent deadzone -- the
		// dining->kitchen line passing 0.186 m east of the table's NE corner froze a
		// rerouting thief at the corner-snap equilibrium forever (measured: TraceProbe B;
		// the fix moved the dining set, the comment there carries the numbers). Walls get
		// their envelope at authoring time (the KX openings, br3's door); furniture now
		// gets it asserted. SegRect's inflation is SQUARE, conservative at diagonal
		// corners by up to r*(sqrt(2)-1) = 0.17: it can demand margin, never miss a
		// violation. Flat rows are walked ON (never in Solids()), so they are exempt;
		// stair edges are included on both endpoint floors -- their lines ride the band,
		// which the hole checks above keep furniture-free, so this only bites an author
		// who parks a dresser on a landing.
		for (int32 Idx = 0; Idx < Names.Num(); ++Idx)
		{
			for (const FString& Next : Edges[Idx])
			{
				const int32 Jdx = Names.IndexOfByKey(Next);
				for (int32 FIdx = 0; FIdx < Rects.Num(); ++FIdx)
				{
					if (Flat[FIdx]
						|| (Floors[FIdx] != NodeFloors()[Idx] && Floors[FIdx] != NodeFloors()[Jdx]))
					{
						continue;
					}
					checkf(!SegRect(NodePositions()[Idx], NodePositions()[Jdx],
							Rects[FIdx], ThiefRadius),
						TEXT("edge '%s'->'%s' passes within a body radius of furniture #%d "
							"(%s) (%.2f,%.2f)-(%.2f,%.2f) floor %d"),
						*Names[Idx], *Next, FIdx, *FurnitureLabels()[FIdx],
						Rects[FIdx].Min.X, Rects[FIdx].Min.Y,
						Rects[FIdx].Max.X, Rects[FIdx].Max.Y, Floors[FIdx]);
				}
			}
		}
	}
}
