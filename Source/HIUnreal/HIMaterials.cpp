#include "HIMaterials.h"

#include "Components/MeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

// Interior colours were lifted against the render rather than picked from a swatch: the
// first pass had a wall and a ceiling within a few percent of each other and the rooms read
// as one continuous surface. Each value below is deliberately separated from its neighbours.
const FLinearColor FHIMaterials::Grass         = FLinearColor(0.16f, 0.30f, 0.12f);
const FLinearColor FHIMaterials::Path          = FLinearColor(0.34f, 0.32f, 0.29f);
const FLinearColor FHIMaterials::Wall          = FLinearColor(0.74f, 0.70f, 0.62f);
const FLinearColor FHIMaterials::WallExterior  = FLinearColor(0.56f, 0.52f, 0.46f);
const FLinearColor FHIMaterials::Floor         = FLinearColor(0.38f, 0.26f, 0.15f);
const FLinearColor FHIMaterials::Ceiling       = FLinearColor(0.90f, 0.89f, 0.86f);
const FLinearColor FHIMaterials::Wood          = FLinearColor(0.30f, 0.19f, 0.11f);
const FLinearColor FHIMaterials::Sofa          = FLinearColor(0.20f, 0.26f, 0.38f);
const FLinearColor FHIMaterials::Counter       = FLinearColor(0.72f, 0.73f, 0.71f);
const FLinearColor FHIMaterials::Door          = FLinearColor(0.26f, 0.15f, 0.09f);
const FLinearColor FHIMaterials::Trim          = FLinearColor(0.94f, 0.93f, 0.90f);
const FLinearColor FHIMaterials::Glass         = FLinearColor(0.16f, 0.21f, 0.26f);
const FLinearColor FHIMaterials::Van           = FLinearColor(0.22f, 0.36f, 0.24f);
const FLinearColor FHIMaterials::Tyre          = FLinearColor(0.05f, 0.05f, 0.055f);
const FLinearColor FHIMaterials::PlayerSkin    = FLinearColor(0.62f, 0.66f, 0.74f);
const FLinearColor FHIMaterials::IntruderSkin  = FLinearColor(0.34f, 0.30f, 0.28f);
const FLinearColor FHIMaterials::Gun           = FLinearColor(0.10f, 0.10f, 0.11f);
const FLinearColor FHIMaterials::Blood         = FLinearColor(0.30f, 0.02f, 0.02f);
const FLinearColor FHIMaterials::WarmLight     = FLinearColor(1.00f, 0.86f, 0.66f);

UMaterialInterface* FHIMaterials::ShapeMaterial()
{
	// A LoadObject rather than a ConstructorHelpers::FObjectFinder, because the callers run
	// from BeginPlay and Build rather than from a constructor. The package is already in
	// memory by then, so this is a name lookup and not a disk read.
	static UMaterialInterface* Cached = nullptr;
	if (Cached == nullptr)
	{
		Cached = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
	return Cached;
}

UMaterialInstanceDynamic* FHIMaterials::Tint(UMeshComponent* Mesh, const FLinearColor& Colour, int32 Slot)
{
	if (Mesh == nullptr)
	{
		return nullptr;
	}
	UMaterialInterface* Base = ShapeMaterial();
	if (Base == nullptr)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, Mesh);
	if (Mid == nullptr)
	{
		return nullptr;
	}
	// If the parameter name is ever wrong this is a silent no-op, which would look exactly
	// like "colours do not work" in a screenshot. The name is checked against the asset's
	// own name table in HI.Unreal.Materials, so a rename fails a test rather than the eye.
	Mid->SetVectorParameterValue(TEXT("Color"), Colour);
	Mesh->SetMaterial(Slot, Mid);
	return Mid;
}
