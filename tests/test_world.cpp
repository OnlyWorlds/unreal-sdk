// World folders into OWWorld, and back: an unedited world written to a scratch folder is its own bytes, every file;
// an edited file is section 5's form; a new element takes section 3.2's filename. Proven on Sikelia (about 20,570
// element files) and Moppetopia.

#include "ow_test.h"
#include "worlds.h"

#include "OWFiles.h"
#include "OWJson.h"
#include "OWWorld.h"

#include <chrono>
#include <cstdio>
#include <set>

using namespace OnlyWorlds;
using owtest::P;

namespace {

bool InParts(const std::string& rel) {
    if (rel == "world.json") return true;
    for (const char* part : {"elements/", "spatial/", "media/", "records/"})
        if (rel.compare(0, std::string(part).size(), part) == 0) return true;
    return false;
}

void CheckWorld(owtest::T& t, const std::string& root, const std::string& label, size_t expect_min) {
    owtest::LoadedWorld& lw = owtest::Loaded(root);
    if (!lw.missing.empty()) {
        t.Skip(lw.missing + " (OW_SIKELIA / OW_MOPPETOPIA)");
        return;
    }
    OWWorld& w = *lw.world;
    size_t expected = owtest::CountElementFiles(root);
    std::printf("       %s: %zu element files on disk, %zu read in %lld ms, %zu other files, %zu errors\n", label.c_str(),
                expected, w.elements.size(), lw.read_ms, w.other_files.size(), w.errors.size());
    t.Ok(expected >= expect_min, label + " has its element files (" + std::to_string(expected) + ")");
    t.Eq(w.elements.size(), expected, label + ": every element file read");
    t.Eq(w.errors.size(), size_t(0), label + ": read errors" + (w.errors.empty() ? "" : " (" + w.errors[0] + ")"));
    OWJsonParseResult info = OWJson::Parse(w.world_bytes);
    t.Ok(info.ok && !w.Id().empty() && info.value.Find("id") && w.Id() == info.value.Find("id")->Text(), label + ": world id");

    // Unedited: written to a scratch folder, every file is the file read, and nothing else is there.
    OWFileMap files = w.ToFiles();
    t.Eq(w.ToFiles(true).size(), size_t(0), label + ": nothing to write when nothing was edited");
    std::string scratch = P().tmp + "/" + label;
    OWFiles::RemoveTree(scratch);
    auto t0 = std::chrono::steady_clock::now();
    std::string err;
    t.Ok(w.WriteFolder(scratch, false, &err), label + ": written (" + err + ")");
    long long write_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::vector<std::string> written;
    owtest::ListTree(scratch, "", written);
    t.Eq(written.size(), files.size(), label + ": the scratch folder holds exactly the world's files");
    size_t same = 0, differ = 0;
    for (const std::string& rel : written) {
        OWBytes back;
        OWFiles::Read(scratch + "/" + rel, back);
        auto it = files.find(rel);
        if (it != files.end() && it->second == back) ++same;
        else if (++differ <= 3) t.Fail(label + ": " + rel + " differs from the file read");
    }
    t.Eq(same, files.size(), label + ": unedited rewrite byte-equal, every file");
    // What the read carried of the world's parts on disk: all but dot-names and .meta files in a content folder and
    // Unity's own .meta in records/ (Pack's selection).
    std::vector<std::string> on_disk;
    owtest::ListTree(root, "", on_disk);
    size_t in_parts = 0, not_carried = 0;
    std::string first_missing;
    for (const std::string& rel : on_disk) {
        if (!InParts(rel)) continue;
        ++in_parts;
        if (!files.count(rel)) {
            ++not_carried;
            if (first_missing.empty()) first_missing = rel;
        }
    }
    std::printf("       %s: %zu files written in %lld ms, %zu byte-equal; of %zu files in the world's parts on disk, %zu "
                "not carried%s\n", label.c_str(), files.size(), write_ms, same, in_parts, not_carried,
                first_missing.empty() ? "" : (" (first: " + first_missing + ")").c_str());
    t.Eq(not_carried, size_t(0), label + ": every file of the world's parts carried");
    OWFiles::RemoveTree(scratch);

    // Edited: every element in section 5's form, against its source.
    t0 = std::chrono::steady_clock::now();
    size_t equal = 0, only_numbers = 0, other = 0;
    for (const OWElement& e : w.elements) {
        OWBytes out = OWJson::ToBytes(e.body);
        if (out == e.source_bytes) {
            ++equal;
        } else if (OWJson::ToBytes(e.body, true) == e.source_bytes) {
            ++only_numbers;  // the layout is section 5's already; only a number's spelling moves
        } else if (++other <= 3) {
            t.Fail(label + ": the section 5 form differs by more than a number's spelling: " + e.source_path);
        }
    }
    std::printf("       %s edited form: %zu byte-equal, %zu differ only in a number's spelling, %zu otherwise, in %lld ms\n",
                label.c_str(), equal, only_numbers, other,
                (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
    t.Eq(other, size_t(0), label + ": files whose section 5 form differs by anything else");
}

}  // namespace

OW_TEST(world_sikelia) { CheckWorld(t, P().sikelia, "Sikelia", 20000); }
OW_TEST(world_moppetopia) { CheckWorld(t, P().moppetopia, "Moppetopia", 100); }

OW_TEST(world_edit_writes_only_the_edited_file) {
    std::unique_ptr<OWWorld> w = OWWorld::ReadFolder(P().moppetopia);
    if (!w) {
        t.Skip("no Moppetopia at " + P().moppetopia);
        return;
    }
    std::vector<OWElement*> pins = w->OfType("pin");
    t.Ok(!pins.empty(), "Moppetopia has pins");
    if (pins.empty()) return;
    OWElement& pin = *pins[0];
    const OWJsonValue* x = pin.body.Find("x");
    int64_t nx = (x && x->IsNumber() ? static_cast<int64_t>(x->AsDouble()) : 0) + 1;
    pin.Set("x", OWJsonValue::Number(nx));
    OWFileMap changed = w->ToFiles(true);
    t.Eq(changed.size(), size_t(1), "one file to write");
    t.Ok(changed.count(pin.source_path) == 1, "and it is the pin's own file");
    std::string text(changed[pin.source_path].begin(), changed[pin.source_path].end());
    t.Ok(text.find("\"x\": " + std::to_string(nx) + ",") != std::string::npos ||
             text.find("\"x\": " + std::to_string(nx) + "\n") != std::string::npos, "the new x, as an integer");
    t.Ok(text.size() > 2 && text.compare(text.size() - 2, 2, "}\n") == 0 && text.find('\r') == std::string::npos,
         "LF and one trailing newline");
    t.Ok(OWJson::Stringify(pin.body) == text, "section 5's form");
    // Write it somewhere else and read it back.
    std::string out = P().tmp + "/edit";
    OWFiles::RemoveTree(out);
    t.Ok(w->WriteFolder(out, true), "written");
    t.Ok(!OWFiles::IsFile(out + "/world.json"), "only the edited file went out (no world.json)");
    OWBytes back;
    OWFiles::Read(out + "/" + pin.source_path, back);
    OWJsonParseResult r = OWJson::Parse(back);
    t.Ok(r.ok && r.value == pin.body, "read back as written");
    OWFiles::RemoveTree(out);
}

OW_TEST(world_new_elements_take_section_3_2_names) {
    OWFileMap files;
    std::string world = "{\n  \"id\": \"w\",\n  \"name\": \"\"\n}\n";
    files["world.json"] = OWBytes(world.begin(), world.end());
    std::string ana = "{\n  \"id\": \"0695db52-0000-7000-8000-00000000ab01\",\n  \"name\": \"Ana\"\n}\n";
    files["elements/character/ana--0000ab01.json"] = OWBytes(ana.begin(), ana.end());
    std::unique_ptr<OWWorld> w = OWWorld::FromFiles(files);
    t.Eq(w->elements.size(), size_t(1), "one element read");
    auto body = [](const std::string& id, const std::string& name) {
        OWJsonValue b = OWJsonValue::Object();
        b.Set("id", OWJsonValue::String(id));
        b.Set("name", OWJsonValue::String(name));
        return b;
    };
    // A twin of Ana sharing her tail, and two new twins of each other: the collision rule, lowest id first.
    w->Add("character", body("1195db52-0000-7000-8000-00000000ab01", "ANA"));
    w->Add("character", body("0995db52-0000-7000-8000-0000000000c7", "Brother Tom\xc3\xa1s"));
    w->Add("character", body("0895db52-0000-7000-8000-0000000000c7", "Brother Tomas"));
    w->Add("location", body("0795db52-0000-7000-8000-0000000000d1", "\xe6\x97\xa5\xe6\x9c\xac"));
    OWFileMap out = w->ToFiles(true);
    std::set<std::string> names;
    for (auto& kv : out) names.insert(kv.first);
    std::set<std::string> want = {
        "elements/character/ana--1195db52-0000-7000-8000-00000000ab01.json",
        "elements/character/brother-tomas--000000c7.json",
        "elements/character/brother-tomas--0995db52-0000-7000-8000-0000000000c7.json",
        "elements/location/0795db52-0000-7000-8000-0000000000d1.json"};
    t.Ok(names == want, "the four paths (a case-blind twin, a tail twin by id order, a name with no slug)");
    for (auto& n : names) std::printf("       %s\n", n.c_str());
    const std::string& tomas = "elements/character/brother-tomas--000000c7.json";
    if (out.count(tomas)) {
        OWJsonParseResult r = OWJson::Parse(out[tomas]);
        t.Ok(r.ok && r.value.Find("id") && r.value.Find("id")->Text() == "0895db52-0000-7000-8000-0000000000c7",
             "the lower id keeps the short name");
    }
}

// -- No id, no write (folder format 4.1; 3.4, v0.3.22: a draft world conforms as any other, and a writer mints no id) --

// A world in memory with no world.json id: WriteFolder writes nothing and says why, Pack refuses; with the tool's own id
// both go.
OW_TEST(world_a_world_json_with_no_id_is_refused_and_nothing_is_written) {
    std::string out = P().tmp + "/draft-no-id";
    const std::vector<std::string> worlds = {"", "{\"name\": \"Draft\"}", "{\"id\": \"\", \"name\": \"Draft\"}",
                                             "{\"id\": \"  \", \"name\": \"Draft\"}", "{\"id\": 7, \"name\": \"Draft\"}"};
    for (const std::string& world : worlds) {
        OWFileMap files;
        if (!world.empty()) files["world.json"] = OWBytes(world.begin(), world.end());
        std::unique_ptr<OWWorld> w = OWWorld::FromFiles(files);
        const std::string label = "world.json '" + world + "'";
        OWFiles::RemoveTree(out);
        t.Ok(!w->WriteRefusal().empty(), label + ": a refusal");
        std::string err;
        t.Ok(!w->WriteFolder(out, false, &err), label + ": WriteFolder refuses");
        t.Eq(err, w->WriteRefusal(), label + ": and says why");
        t.Ok(!OWFiles::IsDir(out), label + ": nothing written");
        OWPacked packed = w->Pack("draft");
        t.Ok(!packed.ok, label + ": Pack refuses");
    }
    std::unique_ptr<OWWorld> draft = OWWorld::FromFiles(OWFileMap());
    draft->SetInfo("id", OWJsonValue::String("019a9200-0000-7000-8000-000000000001"));
    draft->SetInfo("name", OWJsonValue::String("Draft"));
    t.Eq(draft->WriteRefusal(), std::string(), "with the tool's own id, no refusal");
    t.Ok(draft->WriteFolder(out), "a draft with its id is written");
    t.Ok(OWFiles::IsFile(out + "/world.json"), "its world.json");
    t.Ok(draft->Pack("draft").ok, "and packs");
    OWFiles::RemoveTree(out);
}

// A new element with no id: the writer mints none, so WriteFolder and Pack refuse the whole world; given an id, it goes.
OW_TEST(world_a_new_element_with_no_id_is_refused_and_nothing_is_written) {
    std::string out = P().tmp + "/element-no-id";
    OWFiles::RemoveTree(out);
    OWFileMap files;
    std::string world = "{\"id\": \"w1\", \"name\": \"Draft\"}";
    files["world.json"] = OWBytes(world.begin(), world.end());
    std::unique_ptr<OWWorld> w = OWWorld::FromFiles(files);
    OWJsonValue body = OWJsonValue::Object();
    body.Set("name", OWJsonValue::String("Harbour"));
    w->Add("location", body);
    t.Ok(!w->WriteRefusal().empty(), "a refusal");
    t.Ok(!w->WriteFolder(out), "WriteFolder refuses");
    t.Ok(!OWFiles::IsDir(out), "nothing written");
    t.Ok(!w->Pack("draft").ok, "Pack refuses");
    t.Ok(!w->WriteFolder(out, true), "an edited-only write refuses too");
    w->OfType("location")[0]->Set("id", OWJsonValue::String("019a9200-0000-7000-8000-0000000e0001"));
    t.Eq(w->WriteRefusal(), std::string(), "with an id, no refusal");
    t.Ok(w->WriteFolder(out), "written");
    t.Ok(OWFiles::IsFile(out + "/elements/location/harbour--000e0001.json"), "its file");
    OWFiles::RemoveTree(out);
}
