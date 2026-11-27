#pragma once
// What the client nodes (OWClientNodes.cpp) and the held world (OWWorldCacheObject.cpp, OWSyncNodes.cpp) share: the
// core's config from a Blueprint one, and the core's error as a Blueprint one. Defined in OWClientNodes.cpp.

#include "CoreMinimal.h"

#include "OWClient.h"
#include "OWClientNodes.h"
#include "OWSync.h"
#include "OWSyncNodes.h"
#include "OWTransport.h"

#include <memory>
#include <string>

namespace OWClientShared
{
	/** The core's config from Config, sending through Transport (FOWHttpTransport when null). */
	OnlyWorlds::OWClientConfig CoreConfig(const FOWClientConfig& Config, std::shared_ptr<OnlyWorlds::OWTransport> Transport);

	/** The core's error as the Blueprint struct every node answers. */
	FOWApiError ErrorOf(const OnlyWorlds::OWApiError& E);

	/** A refusal in the core's form: kind Refused, its line "OnlyWorlds refused: ...". */
	FOWApiError Refused(const std::string& Why);

	/** The core's sync result as the Blueprint struct (defined in OWSyncNodes.cpp). */
	FOWSyncResult SyncResultOf(const OnlyWorlds::OWSyncResult& R);
}
