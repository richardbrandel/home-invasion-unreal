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

const TArray<FHISim::FHIWallRow>& FHISim::WallRows()
{
	static const TArray<FHIWallRow> W = []()
	{
		const float T = WallThickness;
		const FBox2D H = House;
		const float IZ = InteriorZ;
		TArray<FHIWallRow> Out;
		auto R = [](float X, float Z, float Wd, float Dp, int32 Floor = 0, float TopZ = 0.0f)
		{
			FHIWallRow Row;
			Row.Rect = FBox2D(FVector2D(X, Z), FVector2D(X + Wd, Z + Dp));
			Row.Floor = Floor;
			Row.TopZ = TopZ;
			return Row;
		};
		// ---- GROUND FLOOR: Godot's seven walls, unchanged. The fork adds floors, it does
		// not move the house the port was proven against.
		Out.Add(R(H.Min.X, H.Min.Y, H.GetSize().X, T));               // north
		Out.Add(R(H.Min.X, H.Max.Y - T, DoorGap.X - H.Min.X, T));     // south-west of the door
		Out.Add(R(DoorGap.Y, H.Max.Y - T, H.Max.X - DoorGap.Y, T));   // south-east of the door
		Out.Add(R(H.Min.X, H.Min.Y, T, H.GetSize().Y));               // west
		Out.Add(R(H.Max.X - T, H.Min.Y, T, H.GetSize().Y));           // east
		// interior divider with a central opening
		Out.Add(R(H.Min.X, IZ - T * 0.5f, HallGap.X - H.Min.X, T));
		Out.Add(R(HallGap.Y, IZ - T * 0.5f, H.Max.X - HallGap.Y, T));
		// ---- THE SLICE (HOUSE-DESIGN.md §3, 2026-10-15): the stair stringer wall, west
		// side of the run -- you enter the stairs from the open kitchen end, not through
		// the side. Interior thickness.
		// IT RUNS SOUTH TO THE DIVIDER (-13.875), not to the band's top (-13.5): the
		// 0.375 m pocket between two walls is narrower than a body (0.84) -- a trap any
		// wall-slide could feed, and real stringers meet the wall anyway.
		// AND IT STOPS AT -9.5, NOT AT THE BAND'S FOOT (-9.1) -- measured, 2026-10-15,
		// ThiefDescends frozen 72 s at (7.04, -9.5): the corridor between the stringer's
		// foot corner and the south wall's face is 0.85 m, and a body needs 0.42 of
		// clearance from EACH side -- 0.01 m of passage. No slide rule threads that; the
		// west leg out of the stair foot bit into a corner push-out equilibrium whatever
		// the corner rounding. Ending the stringer 0.4 m short of the foot leaves a
		// 0.41 m centre corridor, which is a door a body fits through, and is also what
		// an open bottom tread looks like in a real house: you board the last steps from
		// the side. What the shortening gives up -- nothing: a body on those last two
		// treads (Z <= 0.27) walking west simply steps off onto the kitchen floor, the
		// same 13-27 cm step as walking off the foot.
		// TOP AT 2.70, NOT 3.0 -- and the original comment here claimed the strict-overlap
		// rule made the step-off work "with no gap geometry at all". That was WRONG TWICE,
		// and both errors were measured (ThiefUpstairs traces, 2026-10-15):
		//  * [0, 3.0]: the top tread's standing height is 2.97 (the north wall's push-out
		//    stops a body 0.045 m short of the band's end), its cylinder is [2.97, 4.77],
		//    and 2.97 < 3.0 -- NO position on the flight cleared a full-storey stringer.
		//    Frozen 76 s at x 7.04 = the stringer's east face + the body radius.
		//  * [0, 2.95]: frozen again, at (7.04, -13.35), Z 2.90 -- the step-off waypoint's
		//    own latitude, where the band height is 2.90. A 2 cm header is not a corridor;
		//    the desired heading there is due west, exactly opposite the push normal, so
		//    the wall-slide projection is zero and the stuck detector watches a statue.
		// The legal crossing is the west railing's step-off gap, y -13.5..-13.1, where the
		// band height runs 3.00 down to 2.73 -- so the header must sit BELOW 2.73, and
		// 2.70 still seals the flight: band height 2.70 is latitude y -13.05, and every
		// latitude south of the gap is inside the west railing's zone (y -13.1..-10.5,
		// whose [3, 4] span catches any cylinder from Z 1.2 up) -- a body tall enough to
		// see over the stringer there is a body the railing holds.
		Out.Add(R(6.50f, -13.875f, 0.12f, 4.375f, 0, 2.70f));          // stringer, -13.875..-9.5
		// ---- SECOND FLOOR exterior, over the front half (y -14..-8) only. The south run
		// is full width -- the front door's gap is the ground floor's business, not the
		// storey above it. The north run at the divider line is the two-storey section's
		// back wall; it has no gap, because the stair top opens WEST onto the deck.
		Out.Add(R(H.Min.X, H.Max.Y - T, H.GetSize().X, T, 1));        // south, floor 1
		Out.Add(R(H.Min.X, IZ - T * 0.5f, H.GetSize().X, T, 1));      // north, floor 1
		Out.Add(R(H.Min.X, IZ, T, H.Max.Y - IZ, 1));                  // west, floor 1
		Out.Add(R(H.Max.X - T, IZ, T, H.Max.Y - IZ, 1));              // east, floor 1
		return Out;
	}();
	return W;
}

const TArray<FBox2D>& FHISim::Walls()
{
	// GROUND FLOOR ONLY, and deliberately: every 2-D query in the game -- the minimap's
	// rays, the legacy LosBlocked, the old ResolveCircle and every test written against
	// them -- means "the floor you stand on", which until the fork was the only floor.
	// Floor-aware work reads WallRows()/WorldSolids(). The count changed 7 -> 8 with the
	// stair stringer; SelfCheck and the wall tests were migrated the same day.
	static const TArray<FBox2D> W = []()
	{
		TArray<FBox2D> Out;
		for (const FHIWallRow& Row : WallRows())
		{
			if (Row.Floor == 0)
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
			// kitchen (front east). Counter and fridge moved 1.70 m west on 2026-10-15:
			// the slice's staircase takes the east strip they stood in (HOUSE-DESIGN.md
			// §3). Relative offsets between the two rows are preserved exactly.
			// Counter shortened 4.00 -> 3.40 and set to abut the fridge (north end -9.65):
			// the kitchen->stairFootL walk line from (4.5,-9.6) to (7.24,-8.6) dips to
			// y=-9.49 at the counter's east edge, and WorldData asserts every nav edge is a
			// clear straight line on its floor. Measured, not eyeballed.
			{  4.90f, -13.05f, 1.15f, 3.40f, 0.92f, false, TEXT("counter") },
			{  5.00f, -13.85f, 1.00f, 0.80f, 1.85f, false, TEXT("fridge") },
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

// ------------------------------------------------------------------ floors (2026-10-15 fork)
//
// The three-floor house's core machinery, specified in HOUSE-DESIGN.md §1: explicit
// Floor per body, height derived from position, stairs as elevation bands, slabs as
// spanned deck solids with the stairwell a literal hole.

const TArray<FHISim::FHISolid>& FHISim::WorldSolids()
{
	static const TArray<FHISolid> S = []()
	{
		// Slice constants (HOUSE-DESIGN.md §3): the second floor is one WallHeight up,
		// its slab 0.25 thick, railings a metre tall, the front roof on a 2.75 ceiling.
		const float F1 = FloorHeight(1);
		const float SlabLo = F1 - 0.25f;
		const float RailTop = F1 + 1.0f;
		const float FrontRoofLo = F1 + 2.75f;
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
		// Walls: each at its floor, each spanning its storey.
		for (const FHIWallRow& Row : WallRows())
		{
			FHISolidSpan Span;
			Span.MinZ = FloorHeight(Row.Floor);
			Span.MaxZ = Row.TopZ > 0.0f ? Row.TopZ : Span.MinZ + WallHeight;
			Add(Row.Rect, Span, Row.Floor, false);
		}
		// Furniture: ground floor only in the slice, and still uniform-at-a-storey for
		// SIGHT -- real furniture heights are a feel decision for Richard
		// (HOUSE-DESIGN.md §1), not a side effect of the floors.
		{
			const TArray<FBox2D>& Furn = FurnitureRects();
			const TArray<bool>& Flat = FurnitureFlat();
			for (int32 Idx = 0; Idx < Furn.Num(); ++Idx)
			{
				if (!Flat[Idx])
				{
					Add(Furn[Idx], FHISolidSpan(), 0, false);
				}
			}
		}
		// The second-floor slab as three decks around the stairwell hole
		// (x 6.35..7.875, y -13.9..-10.5 -- the hole's south edge is the computed
		// headroom limit, not a round number).
		const FHISolidSpan Slab{ SlabLo, F1 };
		Add(FBox2D(FVector2D(-8.0f, -14.0f), FVector2D(6.35f, -8.0f)), Slab, 1, true);  // west
		Add(FBox2D(FVector2D(6.35f, -10.5f), FVector2D(8.0f, -8.0f)), Slab, 1, true);   // south
		Add(FBox2D(FVector2D(6.35f, -14.0f), FVector2D(8.0f, -13.5f)), Slab, 1, true);  // north strip = the header landing the top tread steps off west from
		// Railings at the hole: the west edge keeps its step-off gap at the north end,
		// level with the top tread. They collide with second-floor bodies and are
		// invisible to ground-floor sight and walking -- the span test does both.
		const FHISolidSpan Rail{ F1, RailTop };
		// West edge: starts south of the step-off (the gap is y -13.5..-13.1, level with
		// the header landing). Its push-out radius is also what makes the 0.25 m west
		// sliver between railing and band unreachable -- a resolved body centre cannot be
		// west of x 6.77 there, and 6.77 is inside the band, i.e. ON the stairs.
		Add(FBox2D(FVector2D(6.25f, -13.1f), FVector2D(6.35f, -10.5f)), Rail, 1, false);
		Add(FBox2D(FVector2D(6.35f, -10.6f), FVector2D(7.875f, -10.5f)), Rail, 1, false);
		// Roofs as decks: the rear single-storey half keeps the old 3.0 cap; the front
		// two-storey half gets its ceiling at 5.75.
		Add(FBox2D(FVector2D(-8.0f, -19.0f), FVector2D(8.0f, -14.0f)),
			FHISolidSpan{ WallHeight, WallHeight + 0.15f }, 0, true);
		Add(FBox2D(FVector2D(-8.0f, -14.0f), FVector2D(8.0f, -8.0f)),
			FHISolidSpan{ FrontRoofLo, FrontRoofLo + 0.15f }, 1, true);
		return Out;
	}();
	return S;
}

const TArray<FHISim::FHIStairBand>& FHISim::Stairs()
{
	static const TArray<FHIStairBand> S = []()
	{
		FHIStairBand B;
		// The slice's run (HOUSE-DESIGN.md §3): kitchen east strip, bottom south by the
		// exterior wall, top north at the header landing, a full storey over 4.4 m --
		// 16 risers of 0.1875, 15 treads of 0.293. TWO corrections made while writing the
		// geometry, both recorded in the doc: (1) a first draft put the run in the study
		// (rear half), where a man's head at the top tread is 4.8 m up under a 3.0 m roof;
		// (2) the band spans the FULL stairwell width to the east wall's inner face
		// (7.875), because a 1.0 m band left a 0.275 m slab-free sliver beside the top
		// tread where a second-floor body would stand on nothing.
		B.Rect = FBox2D(FVector2D(6.60f, -13.50f), FVector2D(7.875f, -9.10f));
		B.Bottom = FVector2D(7.24f, -9.10f);
		B.Top = FVector2D(7.24f, -13.50f);
		B.BottomH = 0.0f;
		B.TopH = WallHeight;
		B.FloorLow = 0;
		B.FloorHigh = 1;
		return TArray<FHIStairBand>{ B };
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
	// The slice's two floors, a full storey apart. Phase 2's basement at -2.44 turns this
	// into a table; the function shape already fits.
	return Floor * WallHeight;
}

bool FHISim::DeckCovers(const FVector2D& Pos)
{
	// A WALKABLE deck: the slab, whose top IS the second floor. The roofs are decks for
	// collision purposes but their tops (3.15 rear, 5.90 front) are nobody's floor here,
	// and the rear one is sealed off by the floor-1 north wall anyway.
	for (const FHISolid& S : WorldSolids())
	{
		if (S.bDeck && S.Span.MinZ < FloorHeight(1)
			&& FMath::IsNearlyEqual(S.Span.MaxZ, FloorHeight(1), 0.01f)
			&& S.Rect.IsInsideOrOn(Pos))   // IsInside is STRICT -- the SelfCheck lesson
		{
			return true;
		}
	}
	return false;
}

float FHISim::FloorZAt(const FVector2D& Pos, int32 Floor)
{
	for (const FHIStairBand& B : Stairs())
	{
		if (B.Contains(Pos))
		{
			// DECK BEATS BAND: the south deck strip lies above the flight's foot inside
			// the band's own footprint (band y -13.5..-9.1, deck B y -10.5..-8). A
			// second-floor body standing on that strip is ON THE DECK; deriving it from
			// the band would teleport it 2 m down onto the treads below.
			if (Floor == 1 && DeckCovers(Pos))
			{
				return FloorHeight(1);
			}
			return B.HeightAt(Pos);
		}
	}
	return FloorHeight(Floor);
}

void FHISim::ApplyElevation(const FVector2D& Pos, int32& Floor, float& Z)
{
	if (Floor == 1 && DeckCovers(Pos))
	{
		Z = FloorHeight(1);
		return;
	}
	for (const FHIStairBand& B : Stairs())
	{
		if (B.Contains(Pos))
		{
			Floor = B.FloorAt(Pos);
			Z = B.HeightAt(Pos);
			return;
		}
	}
	Z = FloorHeight(Floor);
}

FVector2D FHISim::ResolveCircleZ(const FVector2D& InPos, float Radius, float Z)
{
	FVector2D Pos = InPos;
	// ON THE FLIGHT, THE FLIGHT OWNS THE BODY. The railings model the DECK edge, and the
	// stairs pass under the hole's south railing: at the crossing a climber's head is at
	// 3.09 against the railing's 3.00 underside, and from Z=1.2 up his cylinder tip is in
	// the span while his feet are metres below the deck. Measured 2026-10-15: without this
	// skip a DESCENDING player froze mid-flight at Z=1.31 -- the railing's push-out
	// equilibrium, exactly radius 0.42 north of it, the stuck detector silent because the
	// push-out still "moved" him. Railing-CLASS solids only (floor-1, topping out about a
	// metre above the deck); the floor-1 north wall (span to 5.75) still stops a climber
	// at the top tread, which is a wall doing its job.
	// ...but only for a body actually ON the flight: the south deck strip sits above the
	// foot of the stairs inside the same footprint, and a second-floor body standing
	// there (Z = 3.0) must still be kept off the hole by its railing. The top tread's
	// standing height is 2.97, so "below deck level" cleanly separates the two.
	const bool bOnStairs = InStairBand(Pos) && Z < FloorHeight(1) - 0.02f;
	for (const FHISolid& S : WorldSolids())
	{
		// Decks are stood ON, never walked into.
		if (S.bDeck)
		{
			continue;
		}
		if (bOnStairs && S.Floor == 1 && S.Span.MaxZ <= FloorHeight(1) + 1.05f)
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
	// `upstairs` joined at the fork: the slice's single second-floor room is searchable,
	// which is what makes a thief cross floors in the headline regression test.
	static const TArray<FString> S = { TEXT("living"), TEXT("kitchen"), TEXT("bedroom"), TEXT("study"), TEXT("upstairs") };
	return S;
}

const TArray<FString>& FHISim::NodeNames()
{
	static const TArray<FString> N = {
		TEXT("van"), TEXT("doorOut"), TEXT("doorIn"), TEXT("frontHub"),
		TEXT("living"), TEXT("kitchen"), TEXT("opening"),
		TEXT("bedroom"), TEXT("study"),
		// The fork's slice nodes (HOUSE-DESIGN.md §3): the stair foot in the kitchen's
		// south-east corner, the landing in front of it, the top tread, the step-off
		// notch at the top, and the single second-floor room.
		TEXT("stairFootG"), TEXT("stairFootL"), TEXT("stairTopG"), TEXT("stairLanding"), TEXT("upstairs"),
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
		FVector2D( 7.24f,  -8.85f), // stairFootG (floor 0, in front of the bottom tread)
		// THE FOOT LANDING, due south of the bottom tread and out of the band. It exists
		// because of a measured statue, the foot mirror of the stairLanding freeze at the
		// top (ThiefDescends, 2026-10-15): the waypoint pop fired 0.3 m INSIDE the band,
		// and the kitchen waypoint sits at y -9.6 -- a descent pinned at the stringer's
		// push-out face x 7.04 slid south exactly to that latitude, where the desired
		// heading (due west) became antiparallel to the push normal (due east): zero
		// slide, zero progress, stuck at (7.04, -9.60) for the remaining 72 s while the
		// reroute rebuilt the same trap. With this node the leg out of stairFootG points
		// SOUTH, straight out of the band, before anything turns west.
		FVector2D( 7.24f,  -8.60f), // stairFootL (floor 0, the foot landing)
		FVector2D( 7.24f, -13.30f), // stairTopG  (floor 1, on the header landing)
		// THE STEP-OFF WAYPOINT, in the gap beside the west railing (y -13.5..-13.1),
		// west of the sliver on deck A. It exists because of a measured freeze, not
		// tidiness (ThiefUpstairs, 2026-10-15): with upstairs as stairTopG's only
		// neighbour, the pop 0.6 m short turned the thief WEST at mid-flight height,
		// where the stringer caught him, and the wall-slide's surviving component was
		// SOUTH -- away from the gap, back down the run. He pinned at (7.04, -12.20),
		// the upstairs waypoint's exact latitude, for the rest of the 90 s. With this
		// node the leg out of stairTopG points WEST-NORTH, so the same slide carries a
		// climber UP to the gap -- the geometry now steers toward the exit, not away.
		FVector2D( 6.00f, -13.35f), // stairLanding (floor 1, the step-off notch)
		// (0,-12.2), NOT the plan-centre (0,-11): the walk line through the step-off gap
		// has to pass north of the west railing (y -13.1..-10.5). stairLanding->upstairs
		// crosses x 6.25..6.35 at y -13.31..-13.29, clear of it; the old stairTopG->(0,-11)
		// line crossed at y -13.0, inside the railing.
		FVector2D( 0.0f,  -12.2f),  // upstairs   (floor 1, centre of the partial storey)
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
		{ TEXT("frontHub"), TEXT("stairFootL") },               // kitchen
		{ TEXT("frontHub"), TEXT("bedroom"), TEXT("study") },   // opening
		{ TEXT("opening") },                                    // bedroom
		{ TEXT("opening") },                                    // study
		{ TEXT("stairFootL"), TEXT("stairTopG") },              // stairFootG
		{ TEXT("stairFootG"), TEXT("kitchen") },                // stairFootL
		{ TEXT("stairFootG"), TEXT("stairLanding") },           // stairTopG
		{ TEXT("stairTopG"), TEXT("upstairs") },                // stairLanding
		{ TEXT("stairLanding") },                               // upstairs
	};
	return E;
}

const TArray<int32>& FHISim::NodeFloors()
{
	static const TArray<int32> F = {
		0, 0, 0, 0, 0, 0, 0, 0, 0,   // Godot's nine, all ground
		0,                           // stairFootG
		0,                           // stairFootL
		1,                           // stairTopG
		1,                           // stairLanding
		1,                           // upstairs
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
	// a node 3 m overhead. INDEX_NONE (any floor) belongs to the safety-net fall-through
	// inside the two-argument form, not to the default.
	return NearestNode(P, 0);
}

FString FHISim::NearestNode(const FVector2D& P, int32 Floor)
{
	// Floor == INDEX_NONE means "any floor" -- the legacy single-argument behaviour. A
	// filtered call considers only that floor's nodes, because an unfiltered Voronoi
	// hands a ground-floor thief the node stacked directly above him and routes him into
	// a slab. Safety net: if the filter finds NOTHING (a floor with no nodes -- cannot
	// happen in this graph, but a silent frontHub default is how the freeze of
	// 2026-10-15 hid), fall through to the unfiltered nearest.
	FString Best;
	float BestD = TNumericLimits<float>::Max();
	const TArray<FString>& Names = NodeNames();
	const TArray<FVector2D>& Poses = NodePositions();
	const TArray<int32>& Floors = NodeFloors();
	for (int32 Idx = 0; Idx < Names.Num(); ++Idx)
	{
		if (Floor != INDEX_NONE && Floors[Idx] != Floor)
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
		// cannot recurse: INDEX_NONE never skips a node, and the graph is never empty.
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

	// ---- decide
	if (Thief.Mode == TEXT("flee") || Thief.Fear >= FearFlee)
	{
		Thief.Mode = TEXT("flee");
		if (Thief.Route.Num() == 0)
		{
			Thief.Route = RouteTo(NearestNode(TPos, Thief.Floor), TEXT("van"));
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
			Thief.Route = RouteTo(NearestNode(TPos, Thief.Floor), TEXT("van"));
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
				Thief.Route = RouteToWithAppend(NearestNode(TPos, Thief.Floor), Loot[Best].Node, Loot[Best].Pos);
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
					if (FloorDist(NodePositions()[NIdx], NodeFloors()[NIdx])
						< FloorDist(NodePositions()[RIdx], NodeFloors()[RIdx]))
					{
						Room = N;
					}
				}
				if (Thief.Target != Room)
				{
					Thief.Target = Room;
					Thief.Route = RouteTo(NearestNode(TPos, Thief.Floor), Room);
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
					Thief.Route = RouteTo(NearestNode(TPos, Thief.Floor), Room);
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
		// Waypoint dwell: the orbit breaker below needs to know how long THIS waypoint has
		// been the target. Reset on change -- a rebuilt route with the same first waypoint
		// keeps the clock, which is the point: the trap is per-waypoint, not per-route.
		if (!WP.Equals(Thief.LastWp, 1e-3f))
		{
			Thief.LastWp = WP;
			Thief.WpT = 0.0f;
		}
		Thief.WpT += Dt;
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
				if (Tangent.SizeSquared() > 0.0025f)               // head-on contact: no slide
				{
					TPos = ResolveCircleZ(Resolved + Tangent.GetSafeNormal() * Thief.Step, ThiefRadius, Thief.Z);
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
		Thief.Route = RouteTo(NearestNode(TPos, Thief.Floor), Back);
		FHIEvent Ev;
		Ev.Type = TEXT("reroute");
		Events.Add(Ev);
	}

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
	// SELF-CONSISTENCY v2 (2026-10-15). v1 asserted Godot's literals; the fork's whole
	// point is that the house no longer matches Godot, so the counts are now the slice's
	// (HOUSE-DESIGN.md §3) and the interesting checks are COMPUTED: a count can be right
	// while the geometry is nonsense, and a stair that arrives inside a slab is exactly
	// the nonsense a literal cannot catch.
	checkf(Walls().Num() == 8, TEXT("expected 8 ground-floor walls (7 + stringer), got %d"), Walls().Num());
	checkf(House.Min.Equals(FVector2D(-8.0f, -19.0f)), TEXT("house min wrong"));
	checkf(House.Max.Equals(FVector2D(8.0f, -8.0f)), TEXT("house max wrong"));
	checkf(NodeNames().Num() == 14, TEXT("expected 14 nav nodes (9 + foot, foot landing, top, step-off, upstairs), got %d"), NodeNames().Num());
	checkf(NodePositions().Num() == NodeNames().Num(), TEXT("nav node pos/name mismatch"));
	checkf(NodeEdges().Num() == NodeNames().Num(), TEXT("nav node edge/name mismatch"));
	checkf(NodeFloors().Num() == NodeNames().Num(), TEXT("nav node floor/name mismatch"));
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
	// per flight, and -- the check the first draft of this house would have failed, the
	// study stairs that arrived under a 3.0 m roof -- EVERYWHERE a standing body's head
	// is above slab height, the body must be under open sky: no deck may cover that spot.
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
		}
	}
	for (const FHIStairBand& B : Stairs())
	{
		// Sample the flight: any point where a body's head clears 2.75 must not have a
		// deck over it. 21 samples over a 4.4 m run is one per ~21 cm -- finer than a
		// tread, so no hole can hide between samples.
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
		// The band's top must arrive ON its upper floor: standing at the top point, the
		// derived Z is the top height and the floor is FloorHigh -- which also proves the
		// top point is inside the band's own rectangle, the alignment slippage guard.
		int32 Fl = 0;
		float Z = 0.0f;
		ApplyElevation(B.Top, Fl, Z);
		checkf(Fl == B.FloorHigh && FMath::IsNearlyEqual(Z, B.TopH, 0.01f),
			TEXT("stair top does not arrive on floor %d (got %d, z=%.2f)"), B.FloorHigh, Fl, Z);
	}
}
