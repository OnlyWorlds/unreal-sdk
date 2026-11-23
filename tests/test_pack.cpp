// OWWorldArchive's pack: its own zip bytes, held to the writer's MUST rules (the marker stored first and bare, only the
// folder's parts, UTF-8 byte order, 1980 stamps, methods 0 and 8), read back by the reader, and the folder rules
// (records/ whole, private producers left out unless asked, tool litter dropped). Each archive made here is also
// written to build/out/ for the reference checker's own check_writer: `python tests/check_writer.py build/out/*.owworld`.

#include "ow_test.h"
#include "sha256.h"
#include "worlds.h"

#include "OWJson.h"
#include "OWUnicode.h"
#include "OWWorldArchive.h"
#include "OWZip.h"

#include <chrono>
#include <cstdio>
#include <iterator>
#include <set>

using namespace OnlyWorlds;
using owtest::P;

namespace {

OWJsonValue VerdictsDoc() {
    OWBytes bytes;
    OWFiles::Read(P().fixtures + "/verdicts.json", bytes);
    return OWJson::Parse(bytes).value;
}

OWBytes B(const std::string& s) { return OWBytes(s.begin(), s.end()); }

uint16_t U16(const OWBytes& b, size_t at) { return static_cast<uint16_t>(b[at] | (b[at + 1] << 8)); }

// The writer's MUST rules on an archive's bytes (the Godot SDK's test_pack faults, the reference checker's
// check_writer in C++); `source` is a verdicts.json source: the files a conforming writer carries byte-exact.
std::vector<std::string> WriterFaults(const OWBytes& bytes, const OWJsonValue& source) {
    std::vector<std::string> faults;
    OWZipWalk w = OWZip::Walk(bytes.data(), bytes.size());
    if (!w.error.empty() || w.entries.empty()) return {"does not walk: " + w.message};
    const OWZipEntry& first = w.entries[0];
    if (first.name != OWWorldArchive::Marker || first.offset != 0) faults.push_back("OW-6: the marker is not the first entry");
    if (first.method != 0 || !first.extra.empty() || U16(bytes, 28) != 0)
        faults.push_back("OW-6: the marker is compressed or carries an extra field");
    for (size_t i = 0; i < w.entries.size(); ++i) {
        const OWZipEntry& e = w.entries[i];
        if (U16(bytes, e.offset + 10) != 0 || U16(bytes, e.offset + 12) != 0x21)
            faults.push_back("OW-12: " + e.name + "'s local header is not stamped 1980-01-01 00:00");
        if (e.dos_time != 0 || e.dos_date != 0x21) faults.push_back("OW-12: " + e.name + "'s directory record is not stamped 1980");
        if (e.method != 0 && e.method != 8) faults.push_back("OW-21: " + e.name + " uses method " + std::to_string(e.method));
        if (i > 0 && e.usize > 0 && e.method != 8) faults.push_back(e.name + " is stored, not deflated");
        if (i > 1 && !(w.entries[i - 1].name < e.name)) faults.push_back("OW-12/13: " + e.name + " is out of UTF-8 byte order");
        if (e.Utf8Flag() != !OWUnicode::IsAscii(e.name)) faults.push_back("OW-22: " + e.name + "'s UTF-8 flag is wrong");
        if (e.UnixType() != 0) faults.push_back(e.name + " claims a Unix mode");
    }
    OWArchiveRead r = OWWorldArchive::Read(bytes);
    if (!r.ok) {
        faults.push_back("the reader refuses it: " + r.message);
        return faults;
    }
    const OWJsonValue* files = source.Find("files");
    if (!files) return {"the source lists no files"};
    for (size_t i = 0; i < files->Size(); ++i) {
        const std::string& rel = files->Keys()[i];
        auto it = r.files.find(rel);
        if (it == r.files.end()) faults.push_back("OW-8/9: " + rel + " from the source is missing");
        else if (owtest::Sha256Hex(it->second) != files->Items()[i].Text()) faults.push_back("OW-8/11: " + rel + " differs from the source's bytes");
    }
    if (r.files.size() != files->Size())
        faults.push_back(std::to_string(r.files.size()) + " files, the source has " + std::to_string(files->Size()));
    return faults;
}

std::string Join(const std::vector<std::string>& v) {
    std::string s;
    for (auto& x : v) s += (s.empty() ? "" : "; ") + x;
    return s.empty() ? "none" : s;
}

void WriteTree(const std::string& root, const OWFileMap& files) {
    for (auto& kv : files) OWFiles::Write(root + "/" + kv.first, kv.second);
}

}  // namespace

OW_TEST(pack_base_world_packs_to_a_conforming_file) {
    OWJsonValue doc = VerdictsDoc();
    OWArchiveRead r = OWWorldArchive::ReadFile(P().fixtures + "/pass-good.owworld");
    t.Ok(r.ok, "the base world reads");
    OWPacked packed = OWWorldArchive::PackFiles(r.files, "abbey");
    t.Ok(packed.ok, "packed");
    t.Eq(packed.names.empty() ? std::string() : packed.names[0], std::string("mimetype"), "the marker first");
    std::vector<std::string> faults = WriterFaults(packed.bytes, *doc.Find("source"));
    t.Ok(faults.empty(), "writer faults: " + Join(faults));
    t.Ok(OWWorldArchive::PackFiles(r.files, "abbey").bytes == packed.bytes, "same input, same bytes (OW-12)");
    OWFiles::Write(P().out + "/pass-good-repacked.owworld", packed.bytes);
}

OW_TEST(pack_folder_on_disk_packs_the_parts_only) {
    OWJsonValue doc = VerdictsDoc();
    std::string root = P().tmp + "/abbey";
    OWFiles::RemoveTree(P().tmp + "/abbey");
    OWArchiveRead r = OWWorldArchive::ReadFile(P().fixtures + "/pass-good.owworld");
    WriteTree(root, r.files);
    // What a folder in a repository carries that is not the world.
    OWFileMap litter;
    litter["README.md"] = B("# abbey");
    litter[".git/HEAD"] = B("ref");
    litter[".atlas/charts.json"] = B("{}");
    litter["elements/character/.DS_Store"] = OWBytes(1, 0);
    litter["elements/character/ana--0000ab01.json.meta"] = B("fileFormatVersion: 2\nguid: 0123456789abcdef0123456789abcdef\n");
    WriteTree(root, litter);
    OWPacked packed = OWWorldArchive::PackFolder(root);
    t.Ok(packed.ok, "packed (" + packed.message + ")");
    std::vector<std::string> faults = WriterFaults(packed.bytes, *doc.Find("source"));
    t.Ok(faults.empty(), "writer faults: " + Join(faults));
    OWArchiveRead back = OWWorldArchive::Read(packed.bytes);
    t.Ok(back.files.count("records/lab/ledger.bin.meta") == 1, "a producer's own .meta travels");
    t.Ok(back.files.count("records/lab/.cursor") == 1, "a producer's dot-file travels");
    std::string out = P().tmp + "/abbey.owworld";
    OWPacked written = OWWorldArchive::Pack(root, out);
    t.Ok(written.ok, "written to disk (" + written.message + ")");
    OWBytes on_disk;
    OWFiles::Read(out, on_disk);
    t.Ok(on_disk == packed.bytes, "the file is the bytes");
    t.Ok(!OWWorldArchive::Pack(root, root + "/self.owworld").ok, "refuses to write inside the folder it packs");
    OWFiles::Write(P().out + "/abbey-from-folder.owworld", packed.bytes);
    OWFiles::RemoveTree(root);
    OWFiles::RemoveTree(out);
}

OW_TEST(pack_private_records_left_out_unless_asked) {
    OWJsonValue doc = VerdictsDoc();
    OWArchiveRead asked = OWWorldArchive::ReadFile(P().fixtures + "/pass-OW-8-private-asked.owworld");
    OWArchiveRead left = OWWorldArchive::ReadFile(P().fixtures + "/pass-OW-8-private-left-out.owworld");
    t.Ok(asked.ok && left.ok, "both sides read");
    std::string root = P().tmp + "/abbey";
    OWFiles::RemoveTree(root);
    WriteTree(root, asked.files);
    OWPacked by_default = OWWorldArchive::PackFolder(root);
    t.Ok(OWWorldArchive::Read(by_default.bytes).files == left.files, "the default side: records/fold/ out whole, RECORDS.json byte-exact");
    t.Ok(by_default.left_out == std::vector<std::string>{"fold"}, "and the pack names what it left out");
    OWPacked when_asked = OWWorldArchive::PackFolder(root, false, true);
    t.Ok(OWWorldArchive::Read(when_asked.bytes).files == asked.files, "the asked side: records/fold/ whole");
    std::vector<std::string> faults = WriterFaults(when_asked.bytes, *doc.Find("sources")->Find("private"));
    t.Ok(faults.empty(), "writer faults, asked: " + Join(faults));
    // The same from a world in memory (OWWorld::Pack goes through PackWorld too).
    std::unique_ptr<OWWorld> w = OWWorld::FromFiles(asked.files);
    t.Ok(OWWorldArchive::Read(w->Pack("abbey").bytes).files == left.files, "a world's pack: private left out by default");
    t.Ok(OWWorldArchive::Read(w->Pack("abbey", false, true).bytes).files == asked.files, "and kept when asked");
    OWFiles::Write(P().out + "/private-asked.owworld", when_asked.bytes);
    OWFiles::Write(P().out + "/private-left-out.owworld", by_default.bytes);
    OWFiles::RemoveTree(root);
}

OW_TEST(pack_api_block_stripped_unless_kept) {
    OWArchiveRead r = OWWorldArchive::ReadFile(P().fixtures + "/pass-OW-10-api.owworld");
    std::string root = P().tmp + "/abbey";
    OWFiles::RemoveTree(root);
    OWFiles::Write(root + "/world.json", r.held_world_json);
    OWArchiveRead stripped = OWWorldArchive::Read(OWWorldArchive::PackFolder(root).bytes);
    std::string held(stripped.held_world_json.begin(), stripped.held_world_json.end());
    t.Ok(stripped.ok && held.find("\"api\"") == std::string::npos, "a portable file has no api block");
    OWArchiveRead kept = OWWorldArchive::Read(OWWorldArchive::PackFolder(root, true).bytes);
    t.Ok(kept.held_world_json == r.held_world_json, "keep_api: world.json byte for byte");
    OWFiles::RemoveTree(root);
}

namespace {

void PackReal(owtest::T& t, const std::string& root, const std::string& label, bool from_folder) {
    owtest::LoadedWorld& lw = owtest::Loaded(root);
    if (!lw.missing.empty()) {
        const char* var = label == "Sikelia" ? "OW_SIKELIA" : "OW_MOPPETOPIA";
        t.Skip(lw.missing + " (set " + var + " to a " + label + " world folder)");
        return;
    }
    OWWorld& w = *lw.world;
    std::string top = OWFiles::LastSegment(root);
    auto t0 = std::chrono::steady_clock::now();
    // Moppetopia packs from its folder, re-read (and the two routes must agree); Sikelia from the world already read,
    // so its 20,570 files are read once per run.
    OWPacked packed = from_folder ? OWWorldArchive::PackFolder(root) : w.Pack(top);
    long long ms_pack = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    t.Ok(packed.ok, label + " packs (" + packed.message + ")");
    if (from_folder) t.Ok(packed.bytes == w.Pack(top).bytes, label + ": the folder and the world read pack the same bytes");
    t0 = std::chrono::steady_clock::now();
    OWArchiveRead r = OWWorldArchive::Read(packed.bytes);
    long long ms_read = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    t.Ok(r.ok, label + " reads back (" + r.reason + " " + r.message + ")");
    OWFileMap files = w.ToFiles();
    // Section 4.5 rule 11: a producer the world's records/RECORDS.json marks "private": true is left out of the pack whole
    // (Sikelia marks its build and lab records so since its world commit 1958bdc). The marks are read here from the
    // world's own RECORDS.json, not from the pack's left_out, so a pack that kept a private producer is caught.
    std::set<std::string> marked, left(packed.left_out.begin(), packed.left_out.end());
    auto manifest = files.find("records/RECORDS.json");
    if (manifest != files.end()) {
        OWJsonParseResult m = OWJson::Parse(manifest->second);
        if (m.ok && m.value.IsArray())
            for (const OWJsonValue& entry : m.value.Items()) {
                const OWJsonValue* p = entry.IsObject() ? entry.Find("private") : nullptr;
                const OWJsonValue* producer = entry.IsObject() ? entry.Find("producer") : nullptr;
                if (p && p->IsBool() && p->AsBool() && producer && producer->IsString()) marked.insert(producer->Text());
            }
    }
    // A producer marked private with no files in the world has nothing to leave out (Sikelia marks lab, holds no
    // records/lab/), so the pack names only the marked producers the world holds files for.
    for (auto it = marked.begin(); it != marked.end();) {
        std::string prefix = "records/" + *it + "/";
        auto at = files.lower_bound(prefix);
        bool held = at != files.end() && at->first.compare(0, prefix.size(), prefix) == 0;
        it = held ? std::next(it) : marked.erase(it);
    }
    auto join = [](const std::set<std::string>& s) {
        std::string out;
        for (const std::string& x : s) out += (out.empty() ? "" : " ") + x;
        return "[" + out + "]";
    };
    t.Eq(join(left), join(marked), label + ": the pack leaves out exactly the producers RECORDS.json marks private");
    for (const std::string& producer : packed.left_out) {
        std::string prefix = "records/" + producer + "/";
        for (auto it = files.begin(); it != files.end();)
            it = it->first.compare(0, prefix.size(), prefix) == 0 ? files.erase(it) : std::next(it);
    }
    t.Ok(r.files == files, label + ": every packed file is the world's bytes, private producers left out, and no other");
    std::unique_ptr<OWWorld> again = OWWorld::FromFiles(r.files);
    t.Eq(again->elements.size(), owtest::CountElementFiles(root), label + ": every element in the archive");
    std::printf("       %s: %zu entries, %.1f MB, packed in %lld ms, read back (every rule, every CRC) in %lld ms; "
                "largest inflate buffer %llu bytes\n", label.c_str(), packed.names.size(), packed.bytes.size() / 1e6,
                ms_pack, ms_read, static_cast<unsigned long long>(r.largest_allocation));
    OWFiles::Write(P().out + "/" + top + ".owworld", packed.bytes);
}

}  // namespace

OW_TEST(pack_moppetopia) { PackReal(t, P().moppetopia, "Moppetopia", true); }
OW_TEST(pack_sikelia) { PackReal(t, P().sikelia, "Sikelia", false); }

OW_TEST(pack_zip64_from_65535_entries) {
    OWFileMap files;
    files["world.json"] = B("{\n  \"id\": \"w\"\n}\n");
    char name[32];
    for (int i = 0; i < 65534; ++i) {
        std::snprintf(name, sizeof name, "media/%05d.txt", i);
        files[name] = OWBytes(1, static_cast<uint8_t>(i & 0xFF));
    }
    OWPacked packed = OWWorldArchive::PackFiles(files, "big");
    OWZipWalk w = OWZip::Walk(packed.bytes.data(), packed.bytes.size());
    t.Ok(w.zip64, "the zip64 end record is written");
    t.Eq(w.entries.size(), size_t(65536), "every entry in the directory (the marker too)");
    OWArchiveRead r = OWWorldArchive::Read(packed.bytes);
    t.Ok(r.ok, "reads (" + r.message + ")");
    t.Eq(r.files.size(), size_t(65535), "every file back");
    OWPacked small = OWWorldArchive::PackFiles({{"world.json", B("{\"id\":\"w\"}")}}, "w");
    t.Ok(!OWZip::Walk(small.bytes.data(), small.bytes.size()).zip64, "a small world is a classic zip");
}
