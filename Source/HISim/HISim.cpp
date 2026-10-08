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
		bool bFlat;
	};
	const TArray<FFurnRow>& FurnitureRows()
	{
		static const TArray<FFurnRow> Rows = {
			// living room (front west)
			{ -7.40f, -12.60f, 1.20f, 3.20f, false },   // sofa
			{ -7.00f, -14.10f, 0.40f, 0.40f, false },   // side_table
			{ -5.60f, -11.70f, 1.40f, 1.40f, false },   // coffee_table
			{ -6.40f, -13.60f, 2.20f, 0.50f, false },   // tv_stand
			{ -7.60f,  -8.60f, 4.40f, 1.80f, true  },   // rug
			{ -7.30f,  -9.90f, 0.45f, 0.45f, false },   // lamp
			// kitchen (front east)
			{  6.60f, -13.00f, 1.15f, 4.00f, false },   // counter
			{  6.70f, -13.85f, 1.00f, 0.80f, false },   // fridge
			{  2.60f, -12.40f, 2.00f, 1.50f, false },   // dining_table
			{  3.15f, -10.75f, 0.55f, 0.50f, false },   // chair
			{  3.55f, -13.00f, 0.55f, 0.45f, false },   // chair
			// bedroom (back west)
			{ -7.40f, -18.60f, 2.40f, 2.20f, false },   // bed
			{ -7.40f, -16.20f, 0.50f, 0.50f, false },   // nightstand
			{ -4.60f, -18.70f, 1.20f, 0.60f, false },   // wardrobe
			{ -6.80f, -15.60f, 3.80f, 1.00f, true  },   // rug
			// study (back east)
			{  5.50f, -17.40f, 2.00f, 1.10f, false },   // desk
			{  5.00f, -16.10f, 0.60f, 0.60f, false },   // desk_chair
			{  0.60f, -18.60f, 0.80f, 2.60f, false },   // bookshelf
			{  0.70f, -15.60f, 0.60f, 0.60f, false },   // cabinet
		};
		return Rows;
	}
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
	const float Side = (FMath::Abs(Cross) < 1e-4f) ? 1.0f : FMath::Sign(Cross);
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
	if (FromKey == ToKey)
	{
		Out.Add(ToKey);
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
	FString Step = ToKey;
	while (!Step.IsEmpty())
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
