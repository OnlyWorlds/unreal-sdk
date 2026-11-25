// The fault the folder format's doors share when world.json, elements/ or spatial/ sits in another case (section 4.8,
// ruled 2026-11-25): OWWorld::FixedNameFault, whose words PackFolder's message and WriteFolder's error carry, and which
// a caller asks (FolderFault) when ReadFolder or FromFiles answers nullptr.

#include "ow_test.h"

#include "OWWorld.h"

#include <string>

using namespace OnlyWorlds;

OW_TEST(fixed_name_fault_names_both_spellings) {
    std::string why = OWWorld::FixedNameFault({"world.json", "Elements", "media"});
    t.Ok(why.find("'Elements'") != std::string::npos && why.find("'elements'") != std::string::npos,
         "both spellings named (" + why + ")");
    t.Ok(why.rfind("name_case: ", 0) == 0, "the cause first (" + why + ")");
    t.Eq(OWWorld::FixedNameFault({"world.json", "elements", "spatial", "Media"}), std::string(),
         "the names as written: no fault (media/ is none of the three)");
    t.Eq(OWWorld::FixedNameFault({"world.json", "elements", "Elements"}), std::string(),
         "both spellings (a disk that keeps case): the name as written is read, the other left unread");
    t.Ok(OWWorld::FixedNameFault({"world.json", "\xC5\xBFpatial"}).find("'spatial'") != std::string::npos,
         "OW-18's fold: a long s (U+017F) is an s, so this is spatial in another case");
    t.Ok(OWWorld::FixedNameFault({"World.JSON", "elements"}).find("'World.JSON'") != std::string::npos,
         "world.json in another case");
    t.Eq(OWWorld::FolderFault("no/such/folder"), std::string(), "a folder not there holds nothing");
}
