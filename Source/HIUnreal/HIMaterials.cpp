#include "HIMaterials.h"

#include "Components/MeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture.h"

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
const FLinearColor FHIMaterials::GTOGreen      = FLinearColor(0.06f, 0.13f, 0.07f);
const FLinearColor FHIMaterials::Tyre          = FLinearColor(0.05f, 0.05f, 0.055f);
const FLinearColor FHIMaterials::Coat          = FLinearColor(0.14f, 0.13f, 0.18f);
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
		if (Cached != nullptr)
		{
			Cached->AddToRoot();
		}
	}
	return Cached;
}

UMaterialInterface* FHIMaterials::Generated(const TCHAR* Name)
{
	// Cached by name, because a LoadObject per component per rebuild is a name lookup the
	// engine can do once. A miss is remembered as nullptr so a missing asset is not re-searched
	// every frame either.
	static TMap<FString, UMaterialInterface*> Cache;
	const FString Key(Name);
	if (UMaterialInterface** Found = Cache.Find(Key))
	{
		return *Found;
	}
	// NOT named `Path`: this struct already has a static `Path` (the driveway colour), and a
	// local that shadows it is `-Wshadow` + `-Werror` here. The same class of trap as the
	// `PI`/`I`/`J` macros -- a short, ordinary-looking name that already means something.
	const FString AssetPath = FString::Printf(TEXT("/Game/HIGenerated/%s.%s"), Name, Name);
	UMaterialInterface* Loaded = LoadObject<UMaterialInterface>(nullptr, *AssetPath);
	if (Loaded != nullptr)
	{
		// Rooted: an asset held only by a raw static pointer is invisible to the GC, and the
		// failure is a use-after-free minutes later rather than a null. See HIAudio.cpp, where
		// this exact mistake ended a play session.
		Loaded->AddToRoot();
	}
	Cache.Add(Key, Loaded);
	return Loaded;
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

UMaterialInstanceDynamic* FHIMaterials::Textured(UMeshComponent* Mesh, const TCHAR* TextureName,
	float Tiling, int32 Slot)
{
	if (Mesh == nullptr || TextureName == nullptr)
	{
		return nullptr;
	}
	UMaterialInterface* Base = Generated(TEXT("M_HITextured"));
	if (Base == nullptr)
	{
		return nullptr;
	}

	// The importer names an asset after its file stem, so door.png becomes "door.door".
	const FString AssetPath =
		FString::Printf(TEXT("/Game/HIImported/textures/%s.%s"), TextureName, TextureName);
	UTexture* Tex = LoadObject<UTexture>(nullptr, *AssetPath);
	if (Tex == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("HI: no texture at %s -- run tools/import-assets.sh"),
			*AssetPath);
		return nullptr;
	}

	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, Mesh);
	if (Mid == nullptr)
	{
		return nullptr;
	}
	Mid->SetTextureParameterValue(TEXT("BaseTexture"), Tex);
	Mid->SetScalarParameterValue(TEXT("UVTiling"), Tiling);
	Mesh->SetMaterial(Slot, Mid);
	return Mid;
}
