#pragma once
// The snapshot and the reader as the core's lens readers take them (OWLens::Resolve, OWGraph::WorldGraph,
// OWCharter::CharterOf), made from an opened world and an FOWLensReader, and their answers in Blueprint shapes: shared
// by UOWLens, UOWGraph and UOWCharter, so a reader means one thing in all three.

#include "CoreMinimal.h"

#include "OWJson.h"
#include "OWLensLibrary.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class UOWWorld;

namespace OnlyWorlds
{
	class OWWorld;
}

namespace OWLensDoor
{
	// The core's world, or nullptr.
	const OnlyWorlds::OWWorld* NativeOf(const UOWWorld* World);

	// The snapshot: `elements`, every element as the API serves it (UOWOpeners::ServedElement), in the world's order;
	// world.json's now, time_current and time_range_current, when present; `members` when given; `open` when bOpen.
	OnlyWorlds::OWJsonValue SnapshotOf(const OnlyWorlds::OWWorld& Native, const OnlyWorlds::OWJsonValue* Members, bool bOpen);

	// The reader object: {rung, character, and a guest's whole and known_of}; with bOwnerCounts below the owner,
	// {rung: owner, as: that reader}.
	OnlyWorlds::OWJsonValue ReaderObjectOf(const FOWLensReader& Reader);

	// JSON text a Blueprint hands over: "" is none (bGiven false, true returned); text that does not parse is refused,
	// Reason Prefix and where it fails.
	bool ReadText(const FString& Text, const TCHAR* Prefix, OnlyWorlds::OWJsonValue& Out, bool& bGiven, FString& Reason);

	// One element as served, or a null body for an id the world does not hold (the profile's no).
	OnlyWorlds::OWJsonValue ServedById(const UOWWorld* World, const FString& Id);

	// A value as the JSON text the plugin hands Blueprints elsewhere (UOWWorld::GetElementJson's form: section 5's
	// layout, each number as it was spelled).
	FString TextOf(const OnlyWorlds::OWJsonValue& Value);

	TArray<FString> IdsOf(const std::vector<std::string>& Ids);

	// Counts by reason, in the order the core gives them.
	TArray<FOWLensCut> CutsOf(const std::vector<std::pair<std::string, int64_t>>& Counts);

	// The kept ids a guest holds only as stubs: those not in its whole ids (a kept id is in its view, and a guest's
	// view is its whole ids and the known-of ids it was not given whole). Empty for every other rung.
	TArray<FString> StubsOf(const std::vector<std::string>& Kept, const FOWLensReader& Reader);
}
