#pragma once
// A world's Map laid out in a level and shot from above, for C++. The frame test (Private/Tests/OWFrameTests.cpp)
// and a packaged game (the host's -OWFrame, Host/Source/OWHost/OWBoot.cpp) both start from the world's root Map.

#include "CoreMinimal.h"

#include "OWModels.h"

class UOWWorld;

namespace OWFrame
{
	// The world's root Map (one with no parent_map; the last in read order when there are several) and its Pins that
	// have both an x and a y, in read order. False, Map left empty and Pins none, when the world holds no root Map.
	ONLYWORLDS_API bool RootMap(const UOWWorld* World, FOWMap& Map, TArray<FOWPin>& Pins);
}
