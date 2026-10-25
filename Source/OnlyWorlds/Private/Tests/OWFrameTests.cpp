// A world opens in a level, framed: Moppetopia's root Map (the map with no parent) laid out as a plane carrying the
// map's own image from the world folder's media/, each of its Pins a marker at the pin's x and y, shot from above by
// a scene capture into a PNG under Saved/OWFrames. Needs a renderer: run with -RenderOffscreen (NonNullRHI keeps it
// out of a -nullrhi run). The markers are found again in the frame's pixels, where the pins' coordinates put them.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AssetCompilingManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "TextureResource.h"
#include "UObject/Package.h"

#include "OWLibrary.h"
#include "OWModels.h"
#include "OWWorldObject.h"
#include "OWTestPaths.h"

namespace
{
	constexpr int32 FrameW = 1800;
	constexpr int32 FrameH = 1200;

	// An unlit material whose emissive colour is one texture, or one colour (made here: the plugin ships no content).
	// Unlit, so the frame needs no light and shows the surfaces' own colours.
	UMaterial* Unlit(UTexture2D* Texture, const FLinearColor& Colour)
	{
		UMaterial* M = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
		M->SetShadingModel(MSM_Unlit);
		UMaterialExpression* Source = nullptr;
		if (Texture)
		{
			UMaterialExpressionTextureSampleParameter2D* T = NewObject<UMaterialExpressionTextureSampleParameter2D>(M);
			T->ParameterName = TEXT("MapImage");
			T->Texture = Texture;
			T->SamplerType = SAMPLERTYPE_Color;
			Source = T;
		}
		else
		{
			UMaterialExpressionConstant3Vector* C = NewObject<UMaterialExpressionConstant3Vector>(M);
			C->Constant = Colour;
			Source = C;
		}
		M->GetExpressionCollection().AddExpression(Source);
		M->GetEditorOnlyData()->EmissiveColor.Expression = Source;
		M->PostEditChange();
		return M;
	}

	AStaticMeshActor* Spawn(UWorld* World, UStaticMesh* Mesh, const FVector& At, const FRotator& Turn, const FVector& Scale,
		UMaterialInterface* Material)
	{
		AStaticMeshActor* A = World->SpawnActor<AStaticMeshActor>(At, Turn);
		A->SetMobility(EComponentMobility::Movable);
		A->GetStaticMeshComponent()->SetStaticMesh(Mesh);
		A->GetStaticMeshComponent()->SetMaterial(0, Material);
		A->SetActorScale3D(Scale);
		return A;
	}

	// The editor compiles a new material's shaders only when asked (or when a frame first needs them): ask, wait, and
	// let every primitive in the world pick the finished shaders up.
	void FinishCompiling(UWorld* World, std::initializer_list<UMaterial*> Materials)
	{
		FAssetCompilingManager::Get().FinishAllCompilation();
		for (UMaterial* M : Materials) M->CacheShaders(EMaterialShaderPrecompileMode::Synchronous);
		UMaterialInterface::SubmitRemainingJobsForWorld(World, EMaterialShaderPrecompileMode::Synchronous);
		if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
		FlushRenderingCommands();
		for (TActorIterator<AActor> It(World); It; ++It) It->ReregisterAllComponents();
		FlushRenderingCommands();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWFrameTest, "OnlyWorlds.Frame.MoppetopiaRootMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::ProductFilter)

bool FOWFrameTest::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddError(TEXT("no renderer: run with -RenderOffscreen, not -nullrhi"));
		return false;
	}
	const FString Root = OWTestPaths::Moppetopia();
	FString Error;
	UOWWorld* OW = UOWLibrary::OpenWorldFolder(Root, Error);
	if (!TestNotNull(TEXT("Moppetopia opens (") + Error + TEXT(")"), OW)) return false;

	// The root map, its size and its pins, all from the plugin's structs.
	FOWMap Map;
	for (const FOWMap& M : UOWElements::GetAllMaps(OW))
	{
		if (M.ParentMap.IsEmpty()) Map = M;
	}
	if (!TestFalse(TEXT("a root map (no parent_map)"), Map.Id.IsEmpty())) return false;
	const double W = static_cast<double>(Map.Width.Get(0));
	const double H = static_cast<double>(Map.Height.Get(0));
	if (!TestTrue(TEXT("the map has a width and a height"), W > 0 && H > 0)) return false;
	TArray<FOWPin> Pins;
	for (const FOWPin& P : UOWElements::GetAllPins(OW))
	{
		if (P.Map == Map.Id && P.X.IsSet() && P.Y.IsSet()) Pins.Add(P);
	}
	TestTrue(TEXT("the root map has pins"), Pins.Num() > 0);

	// The map's image, from the world folder (media/<map id>/base-image/*.png).
	TArray<FString> Images;
	const FString ImageDir = Root / TEXT("media") / Map.Id / TEXT("base-image");
	IFileManager::Get().FindFiles(Images, *(ImageDir / TEXT("*.png")), true, false);
	UTexture2D* Image = Images.Num() > 0 ? FImageUtils::ImportFileAsTexture2D(ImageDir / Images[0]) : nullptr;

	// The level: a new game world with the plane, the markers and a camera above.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("OWFrame"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);

	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!TestTrue(TEXT("the engine's plane and sphere load"), Plane && Sphere)) return false;
	UMaterial* PlaneMaterial = Unlit(Image, FLinearColor(0.8f, 0.75f, 0.6f));
	UMaterial* Marker = Unlit(nullptr, FLinearColor(1.f, 0.f, 0.f));

	// Map x runs to the right of the frame (world +Y), map y up from the bottom (world +X). The plane mesh is 100
	// units square, centred on the origin, its texture's U along its local X and V along its local Y: turned 90
	// degrees, the image's right lies along +Y and its top along +X, as the pins'.
	Spawn(World, Plane, FVector::ZeroVector, FRotator(0, 90, 0), FVector(W / 100.0, H / 100.0, 1.0), PlaneMaterial);
	// Each marker's centre sits on the plane, so it projects to the pin's own point from any height.
	const double Radius = W / 80.0;
	for (const FOWPin& P : Pins)
	{
		const FVector At(static_cast<double>(P.Y.GetValue()) - H / 2.0, static_cast<double>(P.X.GetValue()) - W / 2.0, 0.0);
		Spawn(World, Sphere, At, FRotator::ZeroRotator, FVector(Radius / 50.0), Marker);
	}

	UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
	Target->RenderTargetFormat = RTF_RGBA8;
	Target->ClearColor = FLinearColor::Black;
	Target->InitAutoFormat(FrameW, FrameH);
	Target->UpdateResourceImmediate(true);

	// A camera straight above, its 90-degree view exactly as wide as the map at the plane (half the width up).
	ASceneCapture2D* Camera = World->SpawnActor<ASceneCapture2D>(FVector(0, 0, W / 2.0), FRotator(-90, 0, 0));
	USceneCaptureComponent2D* Capture = Camera->GetCaptureComponent2D();
	Capture->ProjectionType = ECameraProjectionMode::Perspective;
	Capture->FOVAngle = 90.f;
	// The final colour, with a fixed exposure and no lens effects, so an unlit colour comes out as itself (bar the
	// tonemapper's curve).
	Capture->CaptureSource = SCS_FinalColorLDR;
	FPostProcessSettings& Post = Capture->PostProcessSettings;
	Post.bOverride_AutoExposureMethod = true;
	Post.AutoExposureMethod = AEM_Manual;
	Post.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Post.AutoExposureApplyPhysicalCameraExposure = false;
	Post.bOverride_AutoExposureBias = true;
	Post.AutoExposureBias = 0.f;
	Post.bOverride_VignetteIntensity = true;
	Post.VignetteIntensity = 0.f;
	Post.bOverride_BloomIntensity = true;
	Post.BloomIntensity = 0.f;
	Capture->PostProcessBlendWeight = 1.f;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->TextureTarget = Target;

	const double T0 = FPlatformTime::Seconds();
	FinishCompiling(World, { PlaneMaterial, Marker });
	World->SendAllEndOfFrameUpdates();
	Capture->CaptureScene();
	FlushRenderingCommands();
	Capture->CaptureScene();  // a second shot, once every resource the first one touched is in place
	FlushRenderingCommands();
	AddInfo(FString::Printf(TEXT("shaders compiled and the frame captured in %.1f s; texture %dx%d"),
		FPlatformTime::Seconds() - T0, Image ? Image->GetSizeX() : 0, Image ? Image->GetSizeY() : 0));

	TArray<FColor> Pixels;
	Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
	const bool bRead = TestEqual(TEXT("the frame's pixels"), Pixels.Num(), FrameW * FrameH);

	if (bRead)
	{
		for (FColor& C : Pixels) C.A = 255;
		const FString Png = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("OWFrames/moppetopia-root-map.png"));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Png), true);
		TestTrue(TEXT("the frame is written: ") + Png, FImageUtils::SaveImageByExtension(*Png, FImageView(Pixels.GetData(), FrameW, FrameH)));
		AddInfo(FString::Printf(TEXT("frame %s: map %s (%.0f x %.0f), %d pins, image %s"), *Png, *Map.Name, W, H, Pins.Num(),
			Image ? *Images[0] : TEXT("none (a plain plane)")));

		// Each marker is where its pin's coordinates put it.
		auto At = [&Pixels](int32 X, int32 Y) { return Pixels[FMath::Clamp(Y, 0, FrameH - 1) * FrameW + FMath::Clamp(X, 0, FrameW - 1)]; };
		int32 Found = 0;
		for (const FOWPin& P : Pins)
		{
			const int32 X = FMath::RoundToInt32(P.X.GetValue() / W * FrameW);
			const int32 Y = FMath::RoundToInt32((1.0 - P.Y.GetValue() / H) * FrameH);
			const FColor C = At(X, Y);
			if (C.R > 150 && C.G < 90 && C.B < 90) ++Found;
			else AddError(FString::Printf(TEXT("pin %s at (%lld, %lld): frame pixel (%d, %d) is %s, not a marker"), *P.Id,
				P.X.GetValue(), P.Y.GetValue(), X, Y, *C.ToString()));
		}
		TestEqual(TEXT("every pin's marker in the frame"), Found, Pins.Num());
		// The plane carries the map: many colours, not one.
		TSet<uint32> Distinct;
		for (int32 i = 0; i < Pixels.Num(); i += 97) Distinct.Add(Pixels[i].ToPackedARGB());
		AddInfo(FString::Printf(TEXT("%d distinct colours in a sample of %d pixels"), Distinct.Num(), Pixels.Num() / 97));
		TestTrue(TEXT("the frame is not one colour"), Distinct.Num() > (Image ? 200 : 2));
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
