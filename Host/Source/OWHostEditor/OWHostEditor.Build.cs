using UnrealBuildTool;

// The host's editor-only module: the commandlets that write the Blueprint graphs the packaged game runs
// (OWMakeGraphCommandlet.cpp, -run=OWMakeGraph) and the material it draws its frame with (OWMakeFrameCommandlet.cpp,
// -run=OWMakeFrame; Engine's material expressions). "Type": "Editor" in OWHost.uproject, so a game target never builds it.
public class OWHostEditor : ModuleRules
{
	public OWHostEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Engine: UCommandlet and the graph's nodes (EdGraph). UnrealEd: FKismetEditorUtilities, FBlueprintEditorUtils,
		// FCompilerResultsLog. BlueprintGraph: the K2 schema and nodes. OnlyWorlds: the plugin's classes the graph calls.
		PrivateDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "UnrealEd", "BlueprintGraph", "OnlyWorlds" });
	}
}
