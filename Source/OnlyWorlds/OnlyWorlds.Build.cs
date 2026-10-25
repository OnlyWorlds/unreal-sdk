using System.IO;
using EpicGames.Core;
using UnrealBuildTool;

// The Runtime module: the plain C++ core (../../Core, engine-independent, built and tested on its own by build.cmd) is
// compiled in as part of this module's sources, with UObject only at the edges (UOWWorld, the Blueprint libraries,
// the generated element structs).
public class OnlyWorlds : ModuleRules
{
	public OnlyWorlds(ReadOnlyTargetRules Target) : base(Target)
	{
		// Core/ becomes one of this module's directories: its Private/*.cpp are compiled here, its Public/ is on the
		// public include path, its Private/ on the private one (UEBuildModuleCPP.AddDefaultIncludePaths).
		ConditionalAddModuleDirectory(new DirectoryReference(Path.Combine(PluginDirectory, "Core")));

		// No precompiled header: a shared PCH is force-included into every file of a module, which would put
		// CoreMinimal (and its macros: check, TEXT, ...) into the core's translation units. The edge files include
		// what they use.
		PCHUsage = PCHUsageMode.NoPCHs;
		// No unity build: it would merge the core's files with the UObject files (and with each other, whose
		// anonymous namespaces share names) into one translation unit.
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
		// zlib: the engine's own (ThirdParty/zlib/1.3), the same one build.cmd links. Projects: IPluginManager, so
		// the tests find the conformance set beside the plugin.
		PrivateDependencyModuleNames.AddRange(new string[] { "zlib", "Projects" });
	}
}
