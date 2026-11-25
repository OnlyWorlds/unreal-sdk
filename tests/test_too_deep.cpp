// A file nested past 200, the text's top value at 1, objects and arrays both counted (the folder format's OW-25): the
// core's parser refuses it by name (too_deep), a world skips such an element file into `errors` and keeps its bytes in
// `other_files`, as the TypeScript SDK's folder reader skips it, a share leaves it out as unread, and a world.json nested
// that deep is no world. A file whose deepest object or array sits at 200 is read whole, a plain value inside it
// included.

#include "ow_test.h"

#include "OWJson.h"
#include "OWWorld.h"
#include "OWWorldArchive.h"

#include <algorithm>

using namespace OnlyWorlds;

namespace {
const char* const B = "elements/character/b--0000000b.json";

OWBytes Bytes(const std::string& s) { return OWBytes(s.begin(), s.end()); }

// A value whose deepest object or array sits at `deepest` as a field of the file's top object (at 1): an array and an
// object in turn, a plain value inside the last, as the set's OW-25 cases nest.
std::string Nest(int deepest, bool arrays_only = false) {
    std::string open, close;
    for (int k = 0; k < deepest - 1; ++k) {
        bool array = arrays_only || k % 2 == 0;
        open += array ? "[" : "{\"k\":";
        close.insert(0, array ? "]" : "}");
    }
    return open + "1" + close;
}

std::string Character(const std::string& tail, const std::string& name, const std::string& extra = "") {
    return "{\n  \"id\": \"019a9200-0000-7000-8000-00000000000" + tail + "\",\n  \"name\": \"" + name + "\"" + extra +
           "\n}\n";
}

OWFileMap Files(const std::string& deep, const std::string& world_deep = "") {
    OWFileMap f;
    f["world.json"] = Bytes("{\n  \"id\": \"019a9200-0000-7000-8000-000000000001\",\n  \"name\": \"Deep\"" +
                            (world_deep.empty() ? std::string() : ",\n  \"x_depth\": " + world_deep) + "\n}\n");
    f["elements/character/a--0000000a.json"] = Bytes(Character("a", "Ana"));
    f[B] = Bytes(Character("b", "Bo", ",\n  \"x_depth\": " + deep));
    f["elements/character/c--0000000c.json"] = Bytes(Character("c", "Cy"));
    return f;
}

// The deepest bracket in a text with no bracket inside a string: a count no parser makes.
int DeepestInText(const std::string& text) {
    int depth = 0, deepest = 0;
    for (char c : text) {
        if (c == '{' || c == '[') deepest = std::max(deepest, ++depth);
        else if (c == '}' || c == ']') --depth;
    }
    return deepest;
}

std::vector<std::string> Names(const OWWorld& w) {
    std::vector<std::string> out;
    for (const OWElement& e : w.elements) out.push_back(e.Name());
    return out;
}

bool BeginsWith(const std::string& s, const std::string& head) { return s.compare(0, head.size(), head) == 0; }
}  // namespace

OW_TEST(too_deep_the_fixture_nests_where_it_says) {
    t.Ok(DeepestInText(Character("b", "Bo", ",\"x\":" + Nest(200))) == 200, "200");
    t.Ok(DeepestInText(Character("b", "Bo", ",\"x\":" + Nest(201))) == 201, "201");
    std::string objects = Nest(201);
    objects.erase(std::remove_if(objects.begin(), objects.end(), [](char c) { return c == '[' || c == ']'; }),
                  objects.end());
    t.Ok(DeepestInText(objects) == 100, "half the nest is arrays");
}

OW_TEST(too_deep_the_parser_reads_200_and_refuses_201_by_name) {
    for (bool arrays_only : {false, true}) {
        std::string label = arrays_only ? " (arrays only)" : " (arrays and objects)";
        OWJsonParseResult at200 = OWJson::Parse(Character("b", "Bo", ",\"x\":" + Nest(200, arrays_only)));
        t.Ok(at200.ok && !at200.too_deep, "200 is read" + label + ": " + at200.error);
        OWJsonParseResult at201 = OWJson::Parse(Character("b", "Bo", ",\"x\":" + Nest(201, arrays_only)));
        t.Ok(!at201.ok && at201.too_deep, "201 is refused as too deep" + label);
        t.Ok(BeginsWith(at201.error, OWJson::TooDeep), "by the word: " + at201.error);
    }
    std::string far(100000, '[');
    far += std::string(100000, ']');
    OWJsonParseResult r = OWJson::Parse(far);
    t.Ok(!r.ok && r.too_deep, "100,000 deep: refused by the word, not a stack overflow");
}

OW_TEST(too_deep_a_fault_before_the_depth_is_not_this_refusal) {
    OWJsonParseResult r = OWJson::Parse("{\"id\": , \"x\": " + Nest(300) + "}");
    t.Ok(!r.ok && !r.too_deep, "not JSON first: " + r.error);
}

OW_TEST(too_deep_a_file_at_200_is_read_whole) {
    std::unique_ptr<OWWorld> w = OWWorld::FromFiles(Files(Nest(200)));
    t.Ok(w != nullptr, "a world");
    if (!w) return;
    t.Ok(Names(*w) == std::vector<std::string>{"Ana", "Bo", "Cy"}, "all three read");
    t.Ok(w->errors.empty(), "no error");
}

OW_TEST(too_deep_a_file_past_200_is_skipped_by_name_and_carried_whole) {
    OWFileMap files = Files(Nest(201));
    std::unique_ptr<OWWorld> w = OWWorld::FromFiles(files);
    t.Ok(w != nullptr, "a world");
    if (!w) return;
    t.Ok(Names(*w) == std::vector<std::string>{"Ana", "Cy"}, "A and C read, B not");
    t.Ok(w->errors.size() == 1 && BeginsWith(w->errors[0], std::string(B) + ": too_deep"),
         "the error names the file and the word: " + (w->errors.empty() ? std::string() : w->errors[0]));
    t.Ok(w->other_files.count(B) && w->other_files[B] == files[B], "B's bytes kept, unchanged");

    OWPacked packed = w->Pack("deep");
    t.Ok(packed.ok, "the world packs: " + packed.message);
    OWArchiveRead back = OWWorldArchive::Read(packed.bytes);
    t.Ok(back.ok, "the pack reads back");
    t.Ok(back.files.count(B) && back.files[B] == files[B], "the pack carries B unchanged");
    std::unique_ptr<OWWorld> bw = OWWorld::FromFiles(back.files);
    t.Ok(bw && Names(*bw) == std::vector<std::string>{"Ana", "Cy"}, "the .owworld: A and C read, B not");
    t.Ok(bw && bw->errors.size() == 1 && BeginsWith(bw->errors[0], std::string(B) + ": too_deep"), "the .owworld names it");
}

OW_TEST(too_deep_a_share_leaves_it_out_as_unread) {
    OWPacked share = OWWorldArchive::PackWorld(Files(Nest(201)), "deep", false, false, false, false);
    t.Ok(share.ok, "the share packs: " + share.message);
    t.Ok(share.unread_left_out == std::vector<std::string>{B}, "B named unread");
}

OW_TEST(too_deep_a_world_json_past_200_is_no_world) {
    std::unique_ptr<OWWorld> w = OWWorld::FromFiles(Files(Nest(5), Nest(201)));
    t.Ok(w != nullptr, "a world object, its world.json unread");
    if (!w) return;
    bool named = false;
    for (const std::string& e : w->errors) named = named || BeginsWith(e, "world.json: too_deep");
    t.Ok(named, "world.json named by the word");
    t.Ok(!w->Pack("deep").ok, "and it does not pack");
}
