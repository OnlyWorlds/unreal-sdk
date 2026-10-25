#pragma once
// Where the automation tests find their inputs: the conformance set in the repository that holds the plugin (the
// vendored schema-dist, two folders above the plugin's own; OW_FIXTURES for a plugin copied elsewhere; never a path of
// this machine), the two real worlds by OW_SIKELIA and OW_MOPPETOPIA (with the defaults build.cmd's runner uses), and
// a scratch folder under the project's Saved/.

#include "CoreMinimal.h"
#include "HAL/PlatformMisc.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

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

	inline FString Sikelia()
	{
		return Env(TEXT("OW_SIKELIA"), TEXT("C:/Users/Titus/Development/OnlyWorlds/sikelia-world"));
	}

	inline FString Moppetopia()
	{
		return Env(TEXT("OW_MOPPETOPIA"), TEXT("C:/Users/Titus/Carrier/Armature/projects/moppetopia-world"));
	}

	inline FString Scratch(const FString& Name)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OWTests"), Name));
	}
}
