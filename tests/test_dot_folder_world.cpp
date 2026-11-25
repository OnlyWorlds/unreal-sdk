// A dot-folder is never a world, wherever it sits (the folder format's section 4.7, OW-5, ruled 2026-11-24): the
// second-world check does not look under a folder whose name starts with a dot, so a tool's world.json there does not
// refuse the archive. Only that check skips them; every other check still reads their entries.

#include "ow_test.h"

#include "OWWorld.h"
#include "OWWorldArchive.h"

#include <string>

using namespace OnlyWorlds;

namespace {

OWBytes Bytes(const std::string& s) { return OWBytes(s.begin(), s.end()); }

const std::string WorldJson = "{\"id\": \"019a9200-0000-7000-8000-000000000001\", \"name\": \"W\"}";
const std::string Other = "{\"id\": \"019a9200-0000-7000-8000-0000000000ff\", \"name\": \"A tool's\"}";
const std::string Element = "{\"id\": \"019a9200-0000-7000-8000-00000000000a\", \"name\": \"A\"}";

}  // namespace

OW_TEST(dot_folder_a_world_json_under_one_is_a_tools_not_a_second_world) {
    OWFileMap files = {{"abbey/world.json", Bytes(WorldJson)}, {"abbey/elements/character/a.json", Bytes(Element)},
                       {"abbey/.atlas/world.json", Bytes(Other)}, {"abbey/..atlas/world.json", Bytes(Other)},
                       {"abbey/elements/character/.trash/world.json", Bytes(Other)}};
    OWArchiveRead r = OWWorldArchive::Read(OWWorldArchive::PackFiles(files, "").bytes);
    t.Ok(r.ok, "accepted (" + r.reason + ": " + r.message + ")");
    if (!r.ok) return;
    t.Eq(r.world_id, std::string("019a9200-0000-7000-8000-000000000001"), "the world's own id");
    auto w = OWWorld::FromFiles(r.files);
    t.Ok(w && w->elements.size() == 1, "nothing under a dot-folder is an element");
}

OW_TEST(dot_folder_a_world_json_under_a_folder_that_only_holds_a_dot_is_a_second_world) {
    OWFileMap files = {{"abbey/world.json", Bytes(WorldJson)}, {"abbey/a.b/world.json", Bytes(Other)}};
    OWArchiveRead r = OWWorldArchive::Read(OWWorldArchive::PackFiles(files, "").bytes);
    t.Eq(r.reason, std::string("more_than_one"), "a.b/ is a folder like any other (" + r.message + ")");
}
