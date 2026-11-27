#pragma once
// The map-space door's two halves, shared by UOWMapSpace and UOWLayout (whose by-links-3d reads Maps and Pins too):
// a Blueprint's element texts as the core's list of rows, and the core's answers as the Blueprint structs.

#include "CoreMinimal.h"

#include "OWJson.h"
#include "OWMapSpace.h"
#include "OWMapSpaceLibrary.h"

namespace OWMapSpaceDoor
{
	// Each text parsed as one row, in order (any JSON value: a row that is no element is the core's to pass over).
	// False, Reason Name[n]: and where it fails, for a text that does not parse.
	bool RowsOf(const TArray<FString>& Texts, const TCHAR* Name, OnlyWorlds::OWJsonValue& Rows, FString& Reason);

	FOWWorldMapSpace MapSpaceOfCore(const OnlyWorlds::OWWorldMapSpace& Got);
	FOWMapLayout LayoutOfCore(const OnlyWorlds::OWMapLayout& Got);
	FOWMapCarry CarryOfCore(const OnlyWorlds::OWMapCarry& Got);
	FOWPinsKnownTo PinsKnownToOfCore(const OnlyWorlds::OWPinsKnownTo& Got);
}
