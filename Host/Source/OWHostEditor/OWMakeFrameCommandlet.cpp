#include "OWMakeFrameCommandlet.h"

#include "CoreGlobals.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogOWMakeFrame, Log, All);

namespace
{
	// The material's package and name; OWBoot.cpp loads it by the same path. Content/OW/ is generated and left out of
	// git (.gitignore), cooked by DefaultGame.ini's DirectoriesToAlwaysCook although no map references it.
	const TCHAR* const OWFramePackage = TEXT("/Game/OW/M_OWFrame");
	const TCHAR* const OWFrameName = TEXT("M_OWFrame");

	// The texture parameter's default: the engine's white square, loaded at every start (BaseEngine.ini's
	// PackagesToBeFullyLoadedAtStartup) and so in every pak.
	const TCHAR* const WhiteSquare = TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture");

	// An expression placed as the material editor places one (UMaterialEditingLibrary::CreateMaterialExpressionEx): in
	// the material's collection, told its material, given its GUIDs.
	template <typename T>
	T* Place(UMaterial* Material)
	{
		T* Made = NewObject<T>(Material, NAME_None, RF_Transactional);
		Material->GetExpressionCollection().AddExpression(Made);
		Made->Material = Material;
		Made->UpdateMaterialExpressionGuid(true, false);
		Made->UpdateParameterGuid(true, false);
		return Made;
	}
}

UOWMakeFrameCommandlet::UOWMakeFrameCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
	HelpDescription = TEXT("Writes the material the packaged host draws its -OWFrame frame with into Content/OW/.");
}

int32 UOWMakeFrameCommandlet::Main(const FString& Params)
{
	UPackage* Package = CreatePackage(OWFramePackage);
	if (!Package || FindObject<UMaterial>(Package, OWFrameName))
	{
		UE_LOG(LogOWMakeFrame, Error, TEXT("OWMakeFrame: %s is already loaded in this process; nothing written"), OWFramePackage);
		return 1;
	}
	UTexture2D* White = LoadObject<UTexture2D>(nullptr, WhiteSquare);
	if (!White)
	{
		UE_LOG(LogOWMakeFrame, Error, TEXT("OWMakeFrame: the engine's %s does not load; nothing written"), WhiteSquare);
		return 1;
	}

	// Unlit, as the frame test's materials (OWFrameTests.cpp, Unlit): the frame needs no light and shows the surfaces'
	// own colours. Emissive = MapImage's RGB times Tint's RGB.
	UMaterial* M = NewObject<UMaterial>(Package, FName(OWFrameName), RF_Public | RF_Standalone | RF_Transactional);
	M->SetShadingModel(MSM_Unlit);
	UMaterialExpressionTextureSampleParameter2D* Image = Place<UMaterialExpressionTextureSampleParameter2D>(M);
	Image->ParameterName = TEXT("MapImage");
	Image->Texture = White;
	// The sampler that fits the default texture, so the material compiles; a map image set on an instance is sampled
	// through its own resource.
	Image->AutoSetSampleType();
	UMaterialExpressionVectorParameter* Tint = Place<UMaterialExpressionVectorParameter>(M);
	Tint->ParameterName = TEXT("Tint");
	Tint->DefaultValue = FLinearColor::White;
	UMaterialExpressionMultiply* Times = Place<UMaterialExpressionMultiply>(M);
	Image->ConnectExpression(&Times->A, 0);  // RGB
	Tint->ConnectExpression(&Times->B, 0);   // RGB
	Times->ConnectExpression(&M->GetEditorOnlyData()->EmissiveColor, 0);
	M->PostEditChange();
	if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();

	// Saved as OWMakeGraph saves its graphs (and the engine's own commandlets a package).
	const FString Filename = FPackageName::LongPackageNameToFilename(OWFramePackage, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.Error = GWarn;
	if (!UPackage::SavePackage(Package, M, *Filename, SaveArgs))
	{
		UE_LOG(LogOWMakeFrame, Error, TEXT("OWMakeFrame: %s: the save to %s failed"), OWFramePackage, *Filename);
		return 1;
	}
	UE_LOG(LogOWMakeFrame, Display, TEXT("OWMakeFrame: wrote %s (%lld bytes): unlit, emissive MapImage (default %s, sampler %d) x Tint; exit 0"),
		*FPaths::ConvertRelativePathToFull(Filename), IFileManager::Get().FileSize(*Filename), WhiteSquare, static_cast<int32>(Image->SamplerType.GetValue()));
	return 0;
}
