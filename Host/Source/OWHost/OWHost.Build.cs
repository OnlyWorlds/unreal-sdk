using UnrealBuildTool;

// The host's own module: empty. The host exists to build the plugin and run its automation tests.
public class OWHost : ModuleRules
{
	public OWHost(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
	}
}
