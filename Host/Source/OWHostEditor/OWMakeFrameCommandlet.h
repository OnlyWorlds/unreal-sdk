#pragma once
// Writes the material the packaged game draws its frame with (-OWFrame, OWBoot.cpp), as a saved asset in the host's
// Content/OW/, cooked into the pak by DefaultGame.ini's /Game/OW line:
//   /Game/OW/M_OWFrame   unlit, its emissive colour the texture parameter MapImage times the vector parameter Tint
//                        (defaults: the engine's white square, and white). The game dresses one dynamic instance per
//                        use: the map's image on the plane (Tint white), red on the markers (MapImage left white).
// A cooked game cannot make a material of its own: the editor-only graph (UMaterial::GetEditorOnlyData) and the shader
// compiler are not in it, so the frame test's materials, made in code, are made here once, saved, and cooked.
// Run headless: UnrealEditor-Cmd.exe Host/OWHost.uproject -run=OWMakeFrame. Exits 0 when the material is saved.

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"

#include "OWMakeFrameCommandlet.generated.h"

UCLASS()
class UOWMakeFrameCommandlet : public UCommandlet
{
	GENERATED_UCLASS_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
