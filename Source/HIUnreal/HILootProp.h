#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HISim.h"
#include "HILootProp.generated.h"

#ifndef HIUNREAL_API
#define HIUNREAL_API
#endif

/**
 * One valuable, as a thing you can see.
 *
 * Before this existed the entire objective of the game was invisible: the sim tracked three
 * loot items, an intruder picked them up, carried them to a van and the round ended -- and
 * on screen nothing moved. The Godot original models each one from primitives rather than
 * importing a mesh, and so does this, for the same reason: nothing is licensed, nothing is
 * downloaded, and a TV that reads as a TV is enough.
 *
 * DELIBERATELY WITHOUT COLLISION. The sim's own rectangle list is the only source of
 * collision in this port, and a prop that stopped a pellet would be a solid the AI cannot
 * see and cannot route around. Decoration must not be able to change the simulation.
 */
UCLASS()
class HIUNREAL_API AHILootProp : public AActor
{
	GENERATED_BODY()

public:
	AHILootProp();

	/** Build the shape this label calls for, once. Idempotent. */
	void Init(int32 InIndex, const FString& InLabel);

	/**
	 * Put it where the sim says it is.
	 *
	 * The three states are the three the sim has: ON THE FLOOR where it was taken from,
	 * IN THE HANDS of whichever intruder is carrying it, and DELIVERED at the van.
	 * `CarrierYaw` is only read while carried.
	 */
	void SyncFromSim(const FHISim::FHILootItem& Item, bool bCarried, const FVector2D& CarrierPos,
		float CarrierYaw);

	/** The sim's index into FHIRound::Loot, so state and prop can be paired. */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	int32 LootIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	FString Label;

	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TObjectPtr<class USceneComponent> Root;

	/** Every primitive this prop is made of, so they can be tinted and hidden together. */
	UPROPERTY(VisibleAnywhere, Category = "Home Invasion")
	TArray<TObjectPtr<class UStaticMeshComponent>> Parts;

private:
	class UStaticMeshComponent* AddPart(const TCHAR* Name, const TCHAR* MeshPath,
		const FVector& Centre, const FVector& SizeMeters, const FLinearColor& Colour,
		const FRotator& Rotation = FRotator::ZeroRotator);

	bool bBuilt = false;
};
