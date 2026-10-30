#pragma once
// FOWAddress and the core's address, both ways: shared by UOWAddresses and UOWOpeners.

#include "CoreMinimal.h"

#include "OWAddress.h"
#include "OWAddressLibrary.h"
#include "OWJson.h"

namespace OWAddressDoor
{
	// A part is given when its string is not empty, or its flag says an empty one was given.
	inline bool Given(const FString& Part, bool bHas) { return bHas || !Part.IsEmpty(); }

	// A moment the core's typed parts can hold (32 bits); any other is refused as bad_moment, in the writer's order.
	inline bool MomentFits(int64 Moment) { return Moment >= MIN_int32 && Moment <= MAX_int32; }

	// The address in the shape the packages take and the core's WriteJson writes: every given part, an absent one no key.
	OnlyWorlds::OWJsonValue AsJson(const FOWAddress& Address);

	// The core's typed parts, for a moment that fits (MomentFits).
	OnlyWorlds::OWAddressParts AsParts(const FOWAddress& Address);

	FOWAddress FromReading(const OnlyWorlds::OWAddressReading& Reading);
}
