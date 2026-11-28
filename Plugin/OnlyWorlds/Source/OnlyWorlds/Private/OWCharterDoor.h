#pragma once
// The charter door's one shared half, for UOWCharter and the join nodes (OWJoinNodes.cpp, an agent's join answer): the
// core's reading aids as the Blueprint struct. Defined in OWCharterLibrary.cpp.

#include "CoreMinimal.h"

#include "OWCharter.h"
#include "OWCharterLibrary.h"

namespace OWCharterDoor
{
	FOWReadingAids AidsOfCore(const OnlyWorlds::OWReadingAids& Got);
}
