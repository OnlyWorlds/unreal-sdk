#pragma once
// OW_API: what a module outside the core's own may call. Unreal's editor builds each module as its own DLL (a modular
// build), and the core is compiled into the plugin's OnlyWorlds module, so a game module's C++ links against a class,
// function or constant of the core only when it is exported from that DLL. OnlyWorlds.Build.cs defines OW_CORE_DLL for
// the module and every module that depends on it in a modular build, and OW_CORE_EXPORTS for the module alone. Without
// OW_CORE_DLL (build.cmd, a packaged game, any build outside Unreal) OW_API is empty.
//
// The mark goes on every class or struct in Core/Public with a member defined out of line or a static data member, on
// every function declared there without a body, and on every extern constant (tools/check_exports.py checks it).
// Templates, inline functions and constexpr values carry none.

#if defined(OW_CORE_DLL)
#if defined(_WIN32)
#if defined(OW_CORE_EXPORTS)
#define OW_API __declspec(dllexport)
#else
#define OW_API __declspec(dllimport)
#endif
#else
#define OW_API __attribute__((visibility("default")))
#endif
#if defined(_MSC_VER)
// Every file built in this mode is an engine module's, where the engine disables both; the plain build never sees it.
#pragma warning(disable : 4251 4275)
#endif
#else
#define OW_API
#endif
