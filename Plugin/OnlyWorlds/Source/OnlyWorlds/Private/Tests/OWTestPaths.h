#pragma once
// Where the automation tests find their inputs: the conformance set in the repository that holds the plugin (the
// vendored schema-dist, two folders above the plugin's own; OW_FIXTURES for a plugin copied elsewhere; never a path of
// this machine), the two real worlds by OW_SIKELIA and OW_MOPPETOPIA (with the defaults build.cmd's runner uses), and
// a scratch folder under the project's Saved/.

#include "CoreMinimal.h"
#include "HAL/PlatformMisc.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#include "OWConvert.h"
#include "OWFiles.h"
#include "OWJson.h"
#include "OWLibrary.h"
#include "OWWorldObject.h"

namespace OWTestPaths
{
	inline FString PluginDir()
	{
		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("OnlyWorlds"));
		return Plugin.IsValid() ? FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir()) : FString();
	}

	inline FString Env(const TCHAR* Name, const FString& Default)
	{
		FString V = FPlatformMisc::GetEnvironmentVariable(Name);
		return V.IsEmpty() ? Default : V;
	}

	inline FString Fixtures()
	{
		FString InRepo = FPaths::Combine(PluginDir(), TEXT("../../codegen/schema-dist/fixtures/owworld"));
		FPaths::CollapseRelativeDirectories(InRepo);
		return Env(TEXT("OW_FIXTURES"), InRepo);
	}

	// A file of the repository that holds the plugin ("codegen/schema-dist/profiles/address.fixture.json",
	// "tests/data/openers.fixture.json"), two folders above the plugin's own.
	inline FString InRepo(const TCHAR* Rel)
	{
		FString Path = FPaths::Combine(PluginDir(), TEXT("../.."), Rel);
		FPaths::CollapseRelativeDirectories(Path);
		return Path;
	}

	// A JSON file read by the core's strict reader; false when it is missing or does not parse.
	inline bool ReadJson(const FString& Path, OnlyWorlds::OWJsonValue& Out)
	{
		OnlyWorlds::OWBytes Bytes;
		if (!OnlyWorlds::OWFiles::Read(OWConvert::Path(Path), Bytes)) return false;
		OnlyWorlds::OWJsonParseResult Doc = OnlyWorlds::OWJson::Parse(Bytes);
		if (!Doc.ok) return false;
		Out = std::move(Doc.value);
		return true;
	}

	inline FString Sikelia()
	{
		return Env(TEXT("OW_SIKELIA"), TEXT("C:/Users/Titus/Development/OnlyWorlds/sikelia-world"));
	}

	inline FString Moppetopia()
	{
		return Env(TEXT("OW_MOPPETOPIA"), TEXT("C:/Users/Titus/Carrier/Armature/projects/moppetopia-world"));
	}

	// A real world folder opened through UOWLibrary. A missing world is a failure, never a pass (nullptr, an error logged).
	inline UOWWorld* OpenWorld(FAutomationTestBase& Test, const FString& Root, const TCHAR* EnvName)
	{
		if (!FPaths::FileExists(Root / TEXT("world.json")))
		{
			Test.AddError(FString::Printf(TEXT("no world at %s (set %s): a missing world is a failure here, never a pass"),
				*Root, EnvName));
			return nullptr;
		}
		FString Error;
		UOWWorld* World = UOWLibrary::OpenWorldFolder(Root, Error);
		if (!World) Test.AddError(FString::Printf(TEXT("the world at %s does not open: %s"), *Root, *Error));
		return World;
	}

	inline FString Scratch(const FString& Name)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OWTests"), Name));
	}
}
