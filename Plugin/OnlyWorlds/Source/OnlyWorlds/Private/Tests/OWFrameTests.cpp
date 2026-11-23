// A world opens in a level, framed: Moppetopia's root Map (the map with no parent) laid out as a plane carrying the
// map's own image from the world folder's media/, each of its Pins a marker at the pin's x and y, shot from above by
// a scene capture into a PNG under Saved/OWFrames. Needs a renderer: run with -RenderOffscreen (NonNullRHI keeps it
// out of a -nullrhi run). The markers are found again in the frame's pixels, where the pins' coordinates put them.
// The scene is the plugin's (OWFrame.h, the one a packaged game shoots); the materials are made here, in code. Below
// it, OnlyWorlds.Frame.RootMapSkipsLayoutMaps holds the root Map's choice on worlds made in memory (no renderer).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR && OW_REPO_TESTS

#include "AssetCompilingManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "UObject/Package.h"

#include "OWFrame.h"
#include "OWJson.h"
#include "OWLibrary.h"
#include "OWModels.h"
#include "OWWorld.h"
#include "OWWorldObject.h"
#include "OWTestPaths.h"

#include <memory>
#include <utility>
#include <vector>

namespace
{
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
	if (!OWTestPaths::WorldGiven(*this, TEXT("OW_MOPPETOPIA"))) return true;
	const FString Root = OWTestPaths::Moppetopia();
	FString Error;
	UOWWorld* OW = UOWLibrary::OpenWorldFolder(Root, Error);
	if (!TestNotNull(TEXT("Moppetopia opens (") + Error + TEXT(")"), OW)) return false;

	// The root map, its size and its pins, all from the plugin's structs.
	FOWMap Map;
	TArray<FOWPin> Pins;
	OWFrame::RootMap(OW, Map, Pins);
	if (!TestFalse(TEXT("a root map (no parent_map)"), Map.Id.IsEmpty())) return false;
	const double W = static_cast<double>(Map.Width);
	const double H = static_cast<double>(Map.Height);
	if (!TestTrue(TEXT("the map has a width and a height"), W > 0 && H > 0)) return false;
	TestTrue(TEXT("the root map has pins"), Pins.Num() > 0);

	// The map's image, from the world's files as read (media/<map id>/base-image/*.png).
	FString ImageFile;
	UTexture2D* Image = OWFrame::MapImage(OW, Map.Id, ImageFile);

	// The level: a new game world with the plane, the markers and a camera above.
	UWorld* World = OWFrame::NewFrameWorld(TEXT("OWFrame"));

	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, OWFrame::PlanePath);
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, OWFrame::SpherePath);
	if (!TestTrue(TEXT("the engine's plane and sphere load"), Plane && Sphere))
	{
		OWFrame::EndFrameWorld(World);
		return false;
	}
	UMaterial* PlaneMaterial = Unlit(Image, FLinearColor(0.8f, 0.75f, 0.6f));
	UMaterial* Marker = Unlit(nullptr, FLinearColor(1.f, 0.f, 0.f));
	OWFrame::LayOut(World, Map, Pins, Plane, Sphere, PlaneMaterial, Marker);
	USceneCaptureComponent2D* Capture = OWFrame::PlaceCamera(World, Map);

	const double T0 = FPlatformTime::Seconds();
	FinishCompiling(World, { PlaneMaterial, Marker });
	TArray<FColor> Pixels;
	OWFrame::Shoot(Capture, Pixels);
	AddInfo(FString::Printf(TEXT("shaders compiled and the frame captured in %.1f s; texture %dx%d"),
		FPlatformTime::Seconds() - T0, Image ? Image->GetSizeX() : 0, Image ? Image->GetSizeY() : 0));

	using OWFrame::FrameW;
	using OWFrame::FrameH;
	const bool bRead = TestEqual(TEXT("the frame's pixels"), Pixels.Num(), FrameW * FrameH);

	if (bRead)
	{
		const FString Png = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("OWFrames/moppetopia-root-map.png"));
		TestTrue(TEXT("the frame is written: ") + Png, OWFrame::SavePng(Pixels, Png));
		AddInfo(FString::Printf(TEXT("frame %s: map %s (%.0f x %.0f), %d pins, image %s"), *Png, *Map.Name, W, H, Pins.Num(),
			Image ? *ImageFile : TEXT("none (a plain plane)")));

		// Each marker is where its pin's coordinates put it.
		auto At = [&Pixels](int32 X, int32 Y) { return Pixels[FMath::Clamp(Y, 0, FrameH - 1) * FrameW + FMath::Clamp(X, 0, FrameW - 1)]; };
		int32 Found = 0;
		for (const FOWPin& P : Pins)
		{
			const int32 X = FMath::RoundToInt32(P.X / W * FrameW);
			const int32 Y = FMath::RoundToInt32((1.0 - P.Y / H) * FrameH);
			const FColor C = At(X, Y);
			if (C.R > 150 && C.G < 90 && C.B < 90) ++Found;
			else AddError(FString::Printf(TEXT("pin %s at (%lld, %lld): frame pixel (%d, %d) is %s, not a marker"), *P.Id,
				P.X, P.Y, X, Y, *C.ToString()));
		}
		TestEqual(TEXT("every pin's marker in the frame"), Found, Pins.Num());
		// The plane carries the map: many colours, not one.
		TSet<uint32> Distinct;
		for (int32 i = 0; i < Pixels.Num(); i += 97) Distinct.Add(Pixels[i].ToPackedARGB());
		AddInfo(FString::Printf(TEXT("%d distinct colours in a sample of %d pixels"), Distinct.Num(), Pixels.Num() / 97));
		TestTrue(TEXT("the frame is not one colour"), Distinct.Num() > (Image ? 200 : 2));
	}

	OWFrame::EndFrameWorld(World);
	return true;
}

// -- the root Map is never a layout map ----------------------------------------------------------------------------------
// The map-space profile's rule 8 marks a layout map by x_layout alone, an object whose method is a non-empty string. Its
// Pins are placed by a method, so drawn as the world's ground they show an arrangement as geography. Each world below
// reads the layout map last, the place OWFrame::RootMap took before it skipped one. Needs no renderer.

namespace
{
	using OnlyWorlds::OWJsonValue;

	OWJsonValue FrameMapBody(const char* Id, const char* Name, const OWJsonValue* XLayout = nullptr)
	{
		OWJsonValue M = OWJsonValue::Object();
		M.Set("id", OWJsonValue::String(Id));
		M.Set("type", OWJsonValue::String("map"));
		M.Set("name", OWJsonValue::String(Name));
		M.Set("parent_map", OWJsonValue::Null());
		if (XLayout) M.Set("x_layout", *XLayout);
		return M;
	}

	OWJsonValue FrameRecipe(OWJsonValue Method)
	{
		OWJsonValue R = OWJsonValue::Object();
		R.Set("method", std::move(Method));
		return R;
	}

	// The root RootMap picks among these Maps, read in the order given; "<none>" when it finds none.
	FString FrameRootOf(std::vector<OWJsonValue> Maps)
	{
		auto Native = std::make_unique<OnlyWorlds::OWWorld>();
		Native->info = OWJsonValue::Object();
		Native->info.Set("id", OWJsonValue::String("019a3b10-0000-7000-8000-00000000f100"));
		for (OWJsonValue& M : Maps) Native->Add("map", std::move(M));
		UOWWorld* World = NewObject<UOWWorld>(GetTransientPackage());
		World->SetNative(std::move(Native));
		FOWMap Map;
		TArray<FOWPin> Pins;
		return OWFrame::RootMap(World, Map, Pins) ? Map.Id : FString(TEXT("<none>"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWFrameLayoutMapTest, "OnlyWorlds.Frame.RootMapSkipsLayoutMaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FOWFrameLayoutMapTest::RunTest(const FString& Parameters)
{
	const OWJsonValue Board = FrameRecipe(OWJsonValue::String("hand"));
	TestEqual(TEXT("a layout map read last is not the root"),
		FrameRootOf({ FrameMapBody("ground", "Sikelia"), FrameMapBody("layout", "Sikelia: layout", &Board) }), FString(TEXT("ground")));
	TestEqual(TEXT("a world whose only map is a layout map has no root"),
		FrameRootOf({ FrameMapBody("layout", "Sikelia: layout", &Board) }), FString(TEXT("<none>")));

	// What is no mark leaves the Map a candidate: read last, it is the root.
	const std::pair<const TCHAR*, OWJsonValue> NotMarks[] = {
		{ TEXT("an object without method"), OWJsonValue::Object() },
		{ TEXT("an empty method"), FrameRecipe(OWJsonValue::String("")) },
		{ TEXT("a null method"), FrameRecipe(OWJsonValue::Null()) },
		{ TEXT("a number method"), FrameRecipe(OWJsonValue::Number(int64_t(3))) },
		{ TEXT("a string"), OWJsonValue::String("hand") },
		{ TEXT("an array"), OWJsonValue::Array() },
		{ TEXT("null"), OWJsonValue::Null() },
	};
	for (const std::pair<const TCHAR*, OWJsonValue>& Case : NotMarks)
	{
		TestEqual(FString(TEXT("x_layout as ")) + Case.first + TEXT(" is no layout mark"),
			FrameRootOf({ FrameMapBody("ground", "Sikelia"), FrameMapBody("other", "Other", &Case.second) }), FString(TEXT("other")));
	}
	return true;
}

// -- a Pin is on the root Map when its map is the Map's id as written -----------------------------------------------------
// The address profile's rule for the same id: inside a world two ids are the same when they are written the same. FString's
// == ignores case, so the frame compares exactly, as the core's map-space reader does. Needs no renderer.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWFramePinMapTest, "OnlyWorlds.Frame.RootMapPinsByTheMapsIdAsWritten",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FOWFramePinMapTest::RunTest(const FString& Parameters)
{
	auto PinBody = [](const char* Id, const char* MapId)
	{
		OWJsonValue P = OWJsonValue::Object();
		P.Set("id", OWJsonValue::String(Id));
		P.Set("type", OWJsonValue::String("pin"));
		P.Set("name", OWJsonValue::String(Id));
		P.Set("map", OWJsonValue::String(MapId));
		P.Set("x", OWJsonValue::Number(int64_t(10)));
		P.Set("y", OWJsonValue::Number(int64_t(20)));
		return P;
	};
	auto Native = std::make_unique<OnlyWorlds::OWWorld>();
	Native->info = OWJsonValue::Object();
	Native->info.Set("id", OWJsonValue::String("019a3b10-0000-7000-8000-00000000f100"));
	Native->Add("map", FrameMapBody("019a3b10-0000-7000-8000-0000000000a1", "Ground"));
	Native->Add("pin", PinBody("on", "019a3b10-0000-7000-8000-0000000000a1"));
	Native->Add("pin", PinBody("upper", "019A3B10-0000-7000-8000-0000000000A1"));
	UOWWorld* World = NewObject<UOWWorld>(GetTransientPackage());
	World->SetNative(std::move(Native));
	FOWMap Map;
	TArray<FOWPin> Pins;
	if (!TestTrue(TEXT("a root map"), OWFrame::RootMap(World, Map, Pins))) return false;
	TArray<FString> Ids;
	for (const FOWPin& P : Pins) Ids.Add(P.Id);
	TestEqual(TEXT("one Pin on the root Map"), Ids.Num(), 1);
	TestTrue(TEXT("the Pin whose map is the Map's id as written"), Ids.Num() == 1 && Ids[0].Equals(TEXT("on"), ESearchCase::CaseSensitive));
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR && OW_REPO_TESTS
