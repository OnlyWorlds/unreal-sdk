#include "OWVersion.h"
#include <zlib.h>

namespace OnlyWorlds {
const char* ZlibVersion() { return zlibVersion(); }
}
