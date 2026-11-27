// The writer refuses a name its reader refuses (the folder format's section 4.7 writer rules; the .owworld fixture marks
// every such file writer: refuse), as the TypeScript SDK's writeWorldFile does: an unsafe path or top folder, a name
// some disk cannot hold, two names one after the fold, a path that is a file and a folder, a top folder named as the
// marker. OWWorldArchive::EntryNamesFault asks the reader's own checks of a list of names; PackWorld, and PackFolder
// through it, ask it before they write, and write nothing on a fault.

#include "ow_test.h"

#include "OWFiles.h"
#include "OWWorldArchive.h"

#include <string>
#include <vector>

using namespace OnlyWorlds;
using owtest::P;

namespace {

OWBytes Bytes(const std::string& s) { return OWBytes(s.begin(), s.end()); }

const std::string WorldJson = "{\n  \"id\": \"0192a000-aaaa-7bbb-8ccc-000000000001\",\n  \"name\": \"W\"\n}\n";

struct NamesCase {
    std::string label, reason;
    std::vector<std::string> files;
};

// writeWorldFile's vectors (test/world-file.test.mjs, its writer tests) as the names a pack would write under "w".
const std::vector<NamesCase> NameCases = {
    {"a .. segment", "unsafe_path", {"w/world.json", "w/../x"}},
    {"a top of ..", "unsafe_path", {"../w/world.json"}},
    {"a backslash", "unsafe_path", {"w/world.json", "w/media\\a.png"}},
    {"listed twice", "unsafe_path", {"w/world.json", "w/world.json"}},
    {"a Windows device name", "name", {"w/world.json", "w/media/aux.png"}},
    {"a top ending in a dot", "name", {"w./world.json"}},
    {"a reserved character", "name", {"w/world.json", "w/media/a?.png"}},
    {"two names one by the fold", "collision", {"w/world.json", "w/media/A.png", "w/media/a.png"}},
    {"a file and a folder", "unsafe_path", {"w/world.json", "w/media/a", "w/media/a/b.png"}},
    {"a top named mimetype", "unsafe_path", {"mimetype/world.json"}},
    {"a top named MimeType", "collision", {"MimeType/world.json"}},
};

struct PackCase {
    std::string label, reason;
    OWFileMap files;
    std::string top;
};

}  // namespace

OW_TEST(entry_names_fault_refuses_what_the_reader_refuses_by_the_writers_cause) {
    for (const NamesCase& c : NameCases) {
        std::vector<std::string> names = {OWWorldArchive::Marker};
        names.insert(names.end(), c.files.begin(), c.files.end());
        std::string reason;
        std::string why = OWWorldArchive::EntryNamesFault(names, reason);
        t.Eq(reason, c.reason, c.label + ": the cause (" + why + ")");
        t.Ok(!why.empty(), c.label + ": in words");
    }
    std::string reason = "unset";
    t.Eq(OWWorldArchive::EntryNamesFault({OWWorldArchive::Marker, "w/elements/character/a.json", "w/media/a.png", "w/world.json"},
                                         reason),
         std::string(), "a world as written passes");
    t.Eq(reason, std::string(), "and names no cause");
}

OW_TEST(entry_names_pack_world_refuses_writeworldfiles_vectors_as_a_copy_and_a_share) {
    // writeWorldFile's own vectors, each one PackWorld wrote before into a file its reader refused. A top of a/b is
    // unsafe_path, writeWorldFile's cause, where the reader answers that file not_a_world.
    const OWBytes world = Bytes(WorldJson), png = Bytes("PNGBYTES");
    const std::vector<PackCase> cases = {
        {"../x", "unsafe_path", {{"world.json", world}, {"../x", png}}, "w"},
        {"top ../w", "unsafe_path", {{"world.json", world}}, "../w"},
        {"top a/b", "unsafe_path", {{"world.json", world}}, "a/b"},
        {"media/aux.png", "name", {{"world.json", world}, {"media/aux.png", png}}, "w"},
        {"top w.", "name", {{"world.json", world}}, "w."},
        {"media/A.png and media/a.png", "collision", {{"world.json", world}, {"media/A.png", png}, {"media/a.png", png}}, "w"},
        {"media/a and media/a/b.png", "unsafe_path", {{"world.json", world}, {"media/a", png}, {"media/a/b.png", png}}, "w"},
        {"top mimetype", "unsafe_path", {{"world.json", world}}, "mimetype"},
        {"top MimeType", "collision", {{"world.json", world}}, "MimeType"},
    };
    for (const PackCase& c : cases) {
        for (bool copy : {true, false}) {
            const std::string mode = copy ? "copy" : "share";
            OWPacked p = OWWorldArchive::PackWorld(c.files, c.top, false, false, false, copy);
            t.Ok(!p.ok && p.bytes.empty() && p.names.empty(), mode + ", " + c.label + ": refused, nothing written");
            const std::string tail = "; nothing was written";
            t.Ok(p.message.find(", " + c.reason + ": ") != std::string::npos && p.message.size() >= tail.size() &&
                     p.message.compare(p.message.size() - tail.size(), tail.size(), tail) == 0,
                 mode + ", " + c.label + ": the cause in the words (" + p.message + ")");
        }
    }
    OWPacked control = OWWorldArchive::PackWorld({{"world.json", world}, {"media/a.png", png}}, "w");
    t.Ok(control.ok && control.names == std::vector<std::string>({"mimetype", "w/media/a.png", "w/world.json"}),
         "the control is written (" + control.message + ")");
}

OW_TEST(entry_names_pack_folder_of_a_folder_named_mimetype_or_MimeType_is_refused) {
    // The folder door reaches the check through PackWorld: a top folder named as the marker, in either case.
    const std::vector<std::pair<std::string, std::string>> cases = {{"mimetype", "unsafe_path"}, {"MimeType", "collision"}};
    for (const auto& [top, reason] : cases) {
        for (bool copy : {true, false}) {
            std::string base = P().tmp + "/entry-names-" + (top == "mimetype" ? "lower" : "upper");
            std::string root = base + "/" + top;
            OWFiles::RemoveTree(base);
            OWFiles::MakeDirs(root);
            OWFiles::Write(root + "/world.json", Bytes(WorldJson), true);
            OWPacked p = OWWorldArchive::PackFolder(root, false, false, false, copy);
            t.Ok(!p.ok && p.bytes.empty(), top + ": refused, nothing packed");
            t.Ok(p.message.find(", " + reason + ": ") != std::string::npos, top + ": the cause in the words (" + p.message + ")");
            OWFiles::RemoveTree(base);
        }
    }
}
