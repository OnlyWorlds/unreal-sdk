#pragma once

#include "Logging/LogMacros.h"

// A refused archive or an unreadable file is an outcome, not a fault: the module logs it at Log, never at Error (an
// Error line fails a commandlet run and an automation test).
DECLARE_LOG_CATEGORY_EXTERN(LogOnlyWorlds, Log, All);
