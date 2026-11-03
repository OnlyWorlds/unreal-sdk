#include "OWFrame.h"

#include "OWWorldObject.h"

bool OWFrame::RootMap(const UOWWorld* World, FOWMap& Map, TArray<FOWPin>& Pins)
{
	Map = FOWMap();
	Pins.Reset();
	for (const FOWMap& M : UOWElements::GetAllMaps(World))
	{
		if (M.ParentMap.IsEmpty()) Map = M;
	}
	if (Map.Id.IsEmpty()) return false;
	for (const FOWPin& P : UOWElements::GetAllPins(World))
	{
		if (P.Map == Map.Id && P.bHasX && P.bHasY) Pins.Add(P);
	}
	return true;
}
