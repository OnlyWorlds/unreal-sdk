// Under the legacy spatial/ a reader reads map, pin, zone and marker, and only those four, lower case as under elements/
// (the folder format's section 4.8, ruled 2026-11-25): spatial/character/ holds no Character, and spatial/Pin/ no Pin.
// Each door: a world folder (with and without the other parts), FromFiles, an .owworld read back; and the fixture set's
// two OW-24 cases where the set holds them (a draft set, OW_OWWORLD_FIXTURES, before the serial that publishes them).

#include "ow_test.h"

#include "OWFiles.h"
#include "OWJson.h"
#include "OWWorld.h"
#include "OWWorldArchive.h"

#include <memory>
#include <string>
#include <vector>

using namespace OnlyWorlds;
using owtest::P;

namespace {

OWBytes Bytes(const std::string& s) { return OWBytes(s.begin(), s.end()); }

const std::string WorldJson = "{\"id\": \"019a9200-0000-7000-8000-000000000001\", \"name\": \"W\"}";
const std::string IdA = "019a9200-0000-7000-8000-00000000000a";
const std::string IdCleric = "019a9200-0000-7000-8000-00000000000c";
const std::string IdWell = "019a9200-0000-7000-8000-00000000000d";
const std::string IdPin = "019a9200-0000-7000-8000-00000000000e";

std::string Element(const std::string& id, const std::string& name) {
    return "{\"id\": \"" + id + "\", \"name\": \"" + name + "\"}";
}

struct Door {
    std::string name;
    std::unique_ptr<OWWorld> world;
};

std::vector<Door> Doors(owtest::T& t, const OWFileMap& files) {
    std::vector<Door> out;
    std::string dir = P().tmp + "/spatial-four";
    OWFiles::RemoveTree(dir);
    for (const auto& kv : files) OWFiles::Write(dir + "/" + kv.first, kv.second, true);
    out.push_back({"folder", OWWorld::ReadFolder(dir)});
    out.push_back({"folder, no other parts", OWWorld::ReadFolder(dir, false)});
    out.push_back({"files", OWWorld::FromFiles(files)});
    OWArchiveRead read = OWWorldArchive::Read(OWWorldArchive::PackFiles(files, "w").bytes);
    t.Ok(read.ok, "the archive reads (" + read.message + ")");
    out.push_back({"archive", read.ok ? OWWorld::FromFiles(read.files) : nullptr});
    OWFiles::RemoveTree(dir);
    return out;
}

bool Says(const OWWorld& w, const std::string& start, const std::string& words) {
    for (const std::string& e : w.errors)
        if (e.compare(0, start.size(), start) == 0 && e.find(words) != std::string::npos) return true;
    return false;
}

}  // namespace

// One world per spelling of pin's folder: on a disk that ignores case spatial/Pin/ and spatial/pin/ are one folder,
// and in an .owworld the two side by side are OW-18's collision.
OW_TEST(spatial_reads_the_four_and_only_the_four) {
    struct Case {
        OWFileMap files;
        std::string folder;
        std::string id;
        size_t count;
    };
    std::vector<Case> cases;
    cases.push_back({{{"world.json", Bytes(WorldJson)}, {"elements/character/a.json", Bytes(Element(IdA, "A"))},
                      {"spatial/character/cleric.json", Bytes(Element(IdCleric, "Cleric"))},
                      {"spatial/pin/p.json", Bytes(Element(IdPin, "P"))}},
                     "spatial/character", IdCleric, 2});
    cases.push_back({{{"world.json", Bytes(WorldJson)}, {"elements/character/a.json", Bytes(Element(IdA, "A"))},
                      {"spatial/Pin/well.json", Bytes(Element(IdWell, "Well"))}},
                     "spatial/Pin", IdWell, 1});
    for (const Case& c : cases) {
        for (Door& d : Doors(t, c.files)) {
            std::string how = d.name + ", " + c.folder;
            if (!d.world) {
                t.Fail(how + ": the world did not read");
                continue;
            }
            t.Ok(d.world->Get(c.id) == nullptr, how + ": not served (spatial/ holds map, pin, zone, marker as written)");
            t.Eq(d.world->elements.size(), c.count, how + ": nothing else is lost or added");
            if (c.files.count("spatial/pin/p.json")) {
                OWElement* pin = d.world->Get(IdPin);
                t.Ok(pin && pin->Type() == "pin", how + ": spatial/pin/ is read");
            }
            t.Ok(Says(*d.world, c.folder + "/", "map, pin, zone, marker"), how + ": named once, with the four");
        }
    }
}

OW_TEST(spatial_the_sets_two_cases_serve_nothing_from_spatial) {
    int ran = 0;
    for (const char* file : {"pass-OW-24-spatial-untyped.owworld", "pass-OW-24-spatial-type-case.owworld"}) {
        OWArchiveRead r = OWWorldArchive::ReadFile(P().fixtures + "/" + file);
        if (!OWFiles::IsFile(P().fixtures + "/" + file)) continue;
        ++ran;
        t.Ok(r.ok, std::string(file) + ": accepted (" + r.message + ")");
        auto w = r.ok ? OWWorld::FromFiles(r.files) : nullptr;
        if (!w) continue;
        t.Eq(w->elements.size(), size_t(3), std::string(file) + ": the base world's three elements");
        for (const OWElement& e : w->elements)
            t.Ok(e.source_path.compare(0, 9, "elements/") == 0, std::string(file) + ": " + e.source_path + " is not served");
    }
    if (ran == 0) t.Skip("this set has no OW-24 case (it comes after serial 145): set OW_OWWORLD_FIXTURES to a draft set");
}
