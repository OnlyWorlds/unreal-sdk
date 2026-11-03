using UnrealBuildTool;

// The host's own module: builds the plugin, runs its automation tests, and in a packaged game boots a world from the
// command line (OWBoot.cpp, -OWWorld=<path>) through the plugin's Blueprint libraries, and with -OWFrame draws its root
// Map through the plugin's OWFrame (Engine: the meshes, the material instances, the frame's world).
public class OWHost : ModuleRules
{
	public OWHost(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
		// OnlyWorlds: the plugin's Runtime module. Projects: IPluginManager, so the boot says which plugin it found.
		PrivateDependencyModuleNames.AddRange(new string[] { "OnlyWorlds", "Projects" });
	}
}
