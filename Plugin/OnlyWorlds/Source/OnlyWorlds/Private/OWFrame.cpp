#include "OWFrame.h"

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
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "UObject/Package.h"

#include "OWConvert.h"
#include "OWMapSpace.h"
#include "OWModelRead.h"
#include "OWWorldObject.h"

#include <string>

namespace
{
	// The map-space profile's rule 8, read by the core's LayoutOf: a Map whose x_layout is an object with a non-empty
	// string method is a layout map. Its Pins are placed by a method, not where the places stand, so it is no ground.
	bool IsLayoutMap(const UOWWorld* World, const FString& Id)
	{
		const OnlyWorlds::OWJsonValue* Body = OWModelRead::Find(World, Id, "map");
		return Body && OnlyWorlds::OWMapSpace::LayoutOf(*Body, OnlyWorlds::OWJsonValue::Array(), OnlyWorlds::OWJsonValue::Array())
			.layout.has_value();
	}

	AStaticMeshActor* SpawnMesh(UWorld* World, UStaticMesh* Mesh, const FVector& At, const FRotator& Turn, const FVector& Scale,
		UMaterialInterface* Material)
	{
		AStaticMeshActor* A = World->SpawnActor<AStaticMeshActor>(At, Turn);
		A->SetMobility(EComponentMobility::Movable);
		A->GetStaticMeshComponent()->SetStaticMesh(Mesh);
		A->GetStaticMeshComponent()->SetMaterial(0, Material);
		A->SetActorScale3D(Scale);
		return A;
	}
}

bool OWFrame::RootMap(const UOWWorld* World, FOWMap& Map, TArray<FOWPin>& Pins)
{
	Map = FOWMap();
	Pins.Reset();
	for (const FOWMap& M : UOWElements::GetAllMaps(World))
	{
		if (M.ParentMap.IsEmpty() && !IsLayoutMap(World, M.Id)) Map = M;
	}
	if (Map.Id.IsEmpty()) return false;
	for (const FOWPin& P : UOWElements::GetAllPins(World))
	{
		if (P.Map == Map.Id && P.bHasX && P.bHasY) Pins.Add(P);
	}
	return true;
}

UTexture2D* OWFrame::MapImage(const UOWWorld* World, const FString& MapId, FString& File)
{
	File.Empty();
	const OnlyWorlds::OWWorld* Native = World ? World->Native() : nullptr;
	if (!Native || MapId.IsEmpty()) return nullptr;
	// The map's folder named as on disk, where Windows takes the id in any case: compared without case, as the files
	// are keyed by their path inside the world ("media/<id>/base-image/<name>").
	const FString Prefix = TEXT("media/") + MapId + TEXT("/base-image/");
	for (const auto& Entry : Native->other_files)
	{
		const FString Rel = OWConvert::Str(Entry.first);
		if (!Rel.StartsWith(Prefix, ESearchCase::IgnoreCase) || !Rel.EndsWith(TEXT(".png"), ESearchCase::IgnoreCase)) continue;
		// The image itself, not one in a folder beneath it.
		if (Rel.Mid(Prefix.Len()).Contains(TEXT("/"))) continue;
		UTexture2D* Image = FImageUtils::ImportBufferAsTexture2D(
			TArrayView64<const uint8>(Entry.second.data(), static_cast<int64>(Entry.second.size())));
		if (Image) File = Rel;
		return Image;
	}
	return nullptr;
}

UWorld* OWFrame::NewFrameWorld(FName Name)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, Name);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	return World;
}

void OWFrame::EndFrameWorld(UWorld* World)
{
	if (!World) return;
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
}

void OWFrame::LayOut(UWorld* World, const FOWMap& Map, const TArray<FOWPin>& Pins, UStaticMesh* Plane, UStaticMesh* Sphere,
	UMaterialInterface* PlaneMaterial, UMaterialInterface* MarkerMaterial)
{
	const double W = static_cast<double>(Map.Width);
	const double H = static_cast<double>(Map.Height);
	// The plane mesh is 100 units square, centred on the origin, its texture's U along its local X and V along its
	// local Y: turned 90 degrees, the image's right lies along +Y and its top along +X, as the pins'.
	SpawnMesh(World, Plane, FVector::ZeroVector, FRotator(0, 90, 0), FVector(W / 100.0, H / 100.0, 1.0), PlaneMaterial);
	const double Radius = W / 80.0;
	for (const FOWPin& P : Pins)
	{
		const FVector At(static_cast<double>(P.Y) - H / 2.0, static_cast<double>(P.X) - W / 2.0, 0.0);
		SpawnMesh(World, Sphere, At, FRotator::ZeroRotator, FVector(Radius / 50.0), MarkerMaterial);
	}
}

USceneCaptureComponent2D* OWFrame::PlaceCamera(UWorld* World, const FOWMap& Map)
{
	UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
	Target->RenderTargetFormat = RTF_RGBA8;
	Target->ClearColor = FLinearColor::Black;
	Target->InitAutoFormat(FrameW, FrameH);
	Target->UpdateResourceImmediate(true);

	const double W = static_cast<double>(Map.Width);
	ASceneCapture2D* Camera = World->SpawnActor<ASceneCapture2D>(FVector(0, 0, W / 2.0), FRotator(-90, 0, 0));
	USceneCaptureComponent2D* Capture = Camera->GetCaptureComponent2D();
	Capture->ProjectionType = ECameraProjectionMode::Perspective;
	Capture->FOVAngle = 90.f;
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
	return Capture;
}

int32 OWFrame::MeshesWaiting(UWorld* World)
{
	if (!World) return 0;
	World->SendAllEndOfFrameUpdates();
	int32 Waiting = 0;
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		const UStaticMeshComponent* Mesh = It->GetStaticMeshComponent();
		if (Mesh && Mesh->GetStaticMesh() && !Mesh->GetSceneProxy()) ++Waiting;
	}
	return Waiting;
}

bool OWFrame::Shoot(USceneCaptureComponent2D* Capture, TArray<FColor>& Pixels)
{
	Pixels.Reset();
	UWorld* World = Capture ? Capture->GetWorld() : nullptr;
	if (!World || !Capture->TextureTarget) return false;
	World->SendAllEndOfFrameUpdates();
	FlushRenderingCommands();
	Capture->CaptureScene();
	FlushRenderingCommands();
	Capture->CaptureScene();  // a second shot, once every resource the first one touched is in place
	FlushRenderingCommands();
	Capture->TextureTarget->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
	for (FColor& C : Pixels) C.A = 255;
	return Pixels.Num() == FrameW * FrameH;
}

bool OWFrame::SavePng(const TArray<FColor>& Pixels, const FString& Path)
{
	if (Pixels.Num() != FrameW * FrameH) return false;
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	return FImageUtils::SaveImageByExtension(*Path, FImageView(Pixels.GetData(), FrameW, FrameH));
}
