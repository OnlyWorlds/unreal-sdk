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
		// Precise floating point on every target. A game, client or server target defaults to MSVC's /fp:fast, which lets
		// the compiler reorder arithmetic and, where the CPU target has FMA, fuse a multiply and an add into one rounding;
		// the default layout (OWLayout.cpp) must round each operation as JavaScript does to give the npm SDK's points to
		// the bit. Precise is /fp:precise under MSVC and -ffp-contract=off under clang.
		FPSemantics = FPSemanticsMode.Precise;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
		// zlib: the engine's own (ThirdParty/zlib/1.3), the same one build.cmd links. Projects: IPluginManager, so
		// the tests find the conformance set beside the plugin. RenderCore and ImageCore: the frame's capture and
		// PNG (OWFrame.cpp, shot by the frame test and by a packaged game). HTTP: FOWHttpTransport, the core client's transport.
		PrivateDependencyModuleNames.AddRange(new string[] { "zlib", "Projects", "RenderCore", "ImageCore", "HTTP" });

		// The graph tests only (Private/Tests/OWGraphTests.cpp, all of it under WITH_EDITOR): a Blueprint built, its
		// K2 nodes (BlueprintGraph), compiled by the Kismet compiler through FKismetEditorUtilities (UnrealEd). An editor
		// target only, so a game's build of this module gains no editor module.
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "BlueprintGraph" });
		}

		// The automation tests (Private/Tests/) read the repository around the plugin: the vendored schema-dist and
		// tests/data, two folders above the plugin's own. They are compiled only there (OW_REPO_TESTS 1), so the plugin
		// copied into another project lists no OnlyWorlds test that cannot find its inputs.
		bool bInRepository = Directory.Exists(Path.Combine(PluginDirectory, "..", "..", "codegen", "schema-dist"));
		PrivateDefinitions.Add("OW_REPO_TESTS=" + (bInRepository ? "1" : "0"));
	}
}
