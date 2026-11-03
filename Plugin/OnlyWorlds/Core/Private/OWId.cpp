#include "OWId.h"

namespace OnlyWorlds {
namespace OWId {

// Not written yet: the declarations, so their tests build and run red first.
const char* const KeyedNamespace = "";
std::string NewId() { return ""; }
std::string NewId(int64_t) { return ""; }
std::string Keyed(std::string_view, int64_t) { return ""; }
std::string Mint(std::string_view, int64_t) { return ""; }

}  // namespace OWId
}  // namespace OnlyWorlds
