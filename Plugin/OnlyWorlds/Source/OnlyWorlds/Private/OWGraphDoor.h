#pragma once
// A Blueprint graph (FOWWorldGraph) back in the core's shape, for the core's readers that take one: the two degree
// counts (UOWGraph) and the three layouts (UOWLayout). Shared, so a graph means one thing to both.

#include "CoreMinimal.h"

#include "OWGraph.h"

struct FOWWorldGraph;

namespace OWGraphDoor
{
	// Each node's id, type and name (a name only when bHasName, else null, which every reader of the core takes as no
	// name), each edge whole. Family, icon, marks and the cuts are not carried: no reader that takes a graph reads them.
	OnlyWorlds::OWWorldGraph CoreOf(const FOWWorldGraph& Graph);
}
