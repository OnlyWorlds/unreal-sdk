// What a world never loses to a read or a write: a file holds one JSON value and nothing after it but white space, at
// every door; a writer never writes over a file it holds, read or not (folder spec section 3.2's collision rule, and a
// file the reader could not read); a file that is not UTF-8 travels as its bytes; a number no double holds is written
// back as its text after an edit elsewhere in the element.

#include "fake_transport.h"
#include "ow_test.h"

#include "OWClient.h"
#include "OWFiles.h"
#include "OWJoin.h"
#include "OWJson.h"
#include "OWWorld.h"
#include "OWWorldArchive.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace OnlyWorlds;
using owtest::P;

namespace {

OWBytes Bytes(const std::string& s) { return OWBytes(s.begin(), s.end()); }

const std::string WorldJson = "{\"id\": \"019a9200-0000-7000-8000-000000000001\", \"name\": \"W\"}";

void WriteTree(const std::string& root, const OWFileMap& files) {
    OWFiles::RemoveTree(root);
    for (const auto& kv : files) OWFiles::Write(root + "/" + kv.first, kv.second, true);
}

OWBytes FileAt(const std::string& path) {
    OWBytes b;
    OWFiles::Read(path, b);
    return b;
}

// The id a file's bytes hold, or "" when they hold none.
std::string IdIn(const OWBytes& b) {
    OWJsonParseResult r = OWJson::Parse(b);
    const OWJsonValue* id = r.ok && r.value.IsObject() ? r.value.Find("id") : nullptr;
    return id && id->IsString() ? id->Text() : "";
}

OWJsonValue Body(const std::string& id, const std::string& name) {
    OWJsonValue b = OWJsonValue::Object();
    b.Set("id", OWJsonValue::String(id));
    b.Set("name", OWJsonValue::String(name));
    return b;
}

// A world read from a folder written from `files`, or from an .owworld of them.
std::unique_ptr<OWWorld> ReadBack(const OWFileMap& files, bool archive, const std::string& root) {
    if (!archive) {
        WriteTree(root, files);
        return OWWorld::ReadFolder(root);
    }
    OWArchiveRead r = OWWorldArchive::Read(OWWorldArchive::PackFiles(files, "w").bytes);
    return r.ok ? OWWorld::FromFiles(r.files) : nullptr;
}

const char* const Base = "https://example.test/api/v2";

}  // namespace

// -- one JSON value -----------------------------------------------------------------------------------------------------

// A text after the value is no file and no answer at any door: the element, world.json and RECORDS.json read from a
// folder and an .owworld, and a client's or a join's answer. White space after it reads.
OW_TEST(held_one_value_and_nothing_after_it_at_every_door) {
    const std::string c9 = "{\"id\": \"019a9300-0000-7000-8000-0000000000c9\", \"name\": \"Nine\"}";
    const std::string c9path = "elements/character/nine--000000c9.json";
    const std::string joined =
        "{\"kind\":\"player\",\"key\":\"ow_r_guest\","
        "\"world\":{\"id\":\"0196f0a2-0000-7000-8000-000000000001\",\"name\":\"Moppetopia\",\"description\":\"\"},"
        "\"character\":{\"id\":\"0196f0a2-0000-7000-8000-0000000000c1\",\"name\":\"Pip\"},\"role\":\"guest\"}";
    const std::string root = P().tmp + "/held-one-value";
    const std::vector<std::pair<std::string, bool>> tails = {
        {" {\"hidden\": true}", false}, {" x", false}, {",", false}, {"\n\n\t", true}};
    for (const auto& [tail, reads] : tails) {
        const std::string label = "a tail of " + std::to_string(tail.size()) + " bytes (" + (reads ? "white space" : "not") + ")";
        t.Eq(OWJson::Parse(c9 + tail).ok, reads, label + ": Parse");
        OWFileMap files;
        files["world.json"] = Bytes(WorldJson);
        files[c9path] = Bytes(c9 + tail);
        for (bool archive : {false, true}) {
            const std::string at = label + (archive ? ", .owworld" : ", folder");
            std::unique_ptr<OWWorld> w = ReadBack(files, archive, root);
            t.Ok(w != nullptr, at + ": the world reads");
            if (!w) continue;
            t.Eq(w->elements.size(), size_t(reads ? 1 : 0), at + ": the element");
            t.Eq(w->errors.size(), size_t(reads ? 0 : 1), at + ": one error when not read");
            if (!reads) t.Ok(w->other_files.count(c9path) == 1, at + ": its bytes kept");
        }
        OWFileMap world;
        world["world.json"] = Bytes(WorldJson + tail);
        std::unique_ptr<OWWorld> wf = ReadBack(world, false, root);
        t.Eq(wf->Id(), std::string(reads ? "019a9200-0000-7000-8000-000000000001" : ""), label + ": world.json from a folder");
        OWArchiveRead ar = OWWorldArchive::Read(OWWorldArchive::PackFiles(world, "w").bytes);
        t.Eq(ar.ok, reads, label + ": world.json in an .owworld");
        if (!reads) t.Eq(ar.reason, std::string("not_a_world"), label + ": refused as no world");
        t.Eq(OWWorldArchive::PackWorld(world, "w").ok, reads, label + ": PackWorld");
        t.Eq(OWWorldArchive::PackFolder(root).ok, reads, label + ": PackFolder");
        OWFileMap records;
        records["world.json"] = Bytes(WorldJson);
        records["records/RECORDS.json"] = Bytes("[{\"producer\": \"p\", \"private\": true}]" + tail);
        records["records/p/a.txt"] = Bytes("a");
        t.Eq(OWWorldArchive::PackWorld(records, "w").ok, reads, label + ": RECORDS.json");

        auto fake = std::make_shared<owtest::FakeTransport>();
        fake->body = c9 + tail;
        OWClientConfig config;
        config.api_key = "ow_w_test";
        config.api_pin = "0000";
        config.base_url = Base;
        config.transport = fake;
        OWReply<OWJsonValue> got;
        OWClient(std::move(config)).Get("character", "c9", {}, [&](OWReply<OWJsonValue> r) { got = std::move(r); });
        t.Eq(got.Ok(), reads, label + ": a client's answer");
        if (!reads && got.error) t.Ok(got.error->kind == OWApiError::Kind::Answer, label + ": an answer that cannot be read");

        auto jf = std::make_shared<owtest::FakeTransport>();
        jf->body = joined + tail;
        OWJoinConfig where;
        where.transport = jf;
        where.base_url = Base;
        OWReply<OWJoined> j;
        OWJoin::Redeem("ow_j_EoEUrAfQ9xumy-lbBJqXix3q1xl49-P4", {}, where, [&](OWReply<OWJoined> r) { j = std::move(r); });
        t.Eq(j.Ok(), reads, label + ": a join's answer");
    }
    OWFiles::RemoveTree(root);
}

// -- a writer and the names it holds ------------------------------------------------------------------------------------

namespace {

const std::string BoId = "019a9300-0000-7000-8000-00000000000b";
const std::string Bo2Id = "019a9300-0000-7000-8000-10000000000b";
const std::string BoShort = "elements/character/bo--0000000b.json";
const std::string BoFull = "elements/character/bo--019a9300-0000-7000-8000-00000000000b.json";
const std::string Bo2Full = "elements/character/bo--019a9300-0000-7000-8000-10000000000b.json";

}  // namespace

// A new element whose short name is held by a file the reader could not read (cut short, not UTF-8, no id), or by a
// read element's own file, takes its full id; the held file keeps its bytes at every door that writes or packs. Two
// new elements of one name and id tail are two files, the lower id at the short name.
OW_TEST(held_a_new_element_never_takes_a_held_name) {
    const std::string in = P().tmp + "/held-name-in";
    const std::string out = P().tmp + "/held-name-out";
    const std::vector<std::pair<std::string, std::string>> held = {
        {"a file cut short", "{\"id\": \"" + Bo2Id + "\", \"name\": \"Bo\""},
        {"a file not UTF-8", "{\"id\": \"" + Bo2Id + "\", \"name\": \"Bo\", \"description\": \"caf\xE9\"}"},
        {"a file with no id", "{\"name\": \"Bo\"}"},
        {"a read element", "{\"id\": \"" + Bo2Id + "\", \"name\": \"Bo\"}"}};
    for (const auto& [what, text] : held) {
        OWFileMap files;
        files["world.json"] = Bytes(WorldJson);
        files[BoShort] = Bytes(text);
        for (bool archive : {false, true}) {
            const std::string label = what + (archive ? ", from an .owworld" : ", from a folder");
            std::unique_ptr<OWWorld> w = ReadBack(files, archive, in);
            if (!w) {
                t.Fail(label + ": did not read");
                continue;
            }
            w->Add("character", Body(BoId, "Bo"));
            t.Eq(w->WriteRefusal(), std::string(), label + ": no refusal");
            OWFileMap whole = w->ToFiles();
            t.Ok(whole[BoShort] == files[BoShort], label + ": ToFiles keeps the held file");
            t.Eq(IdIn(whole[BoFull]), BoId, label + ": and the new element takes its full id");
            OWFileMap edited = w->ToFiles(true);
            t.Ok(edited.count(BoShort) == 0, label + ": an edited-only write leaves the held name alone");
            OWFiles::RemoveTree(out);
            t.Ok(w->WriteFolder(out), label + ": written whole");
            t.Ok(FileAt(out + "/" + BoShort) == files[BoShort], label + ": the held file whole, elsewhere");
            if (!archive) {
                t.Ok(w->WriteFolder(in, true), label + ": written edited-only in place");
                t.Ok(FileAt(in + "/" + BoShort) == files[BoShort], label + ": the held file in place");
                t.Eq(IdIn(FileAt(in + "/" + BoFull)), BoId, label + ": the new element beside it");
            }
            OWPacked p = w->Pack("w");
            OWArchiveRead back = OWWorldArchive::Read(p.bytes);
            t.Ok(p.ok && back.ok && back.files[BoShort] == files[BoShort], label + ": the pack carries the held file");
        }
    }
    OWFileMap files;
    files["world.json"] = Bytes(WorldJson);
    std::unique_ptr<OWWorld> w = OWWorld::FromFiles(files);
    w->Add("character", Body(Bo2Id, "Bo"));
    w->Add("character", Body(BoId, "Bo"));
    OWFileMap both = w->ToFiles(true);
    t.Eq(both.size(), size_t(2), "two new elements of one name and tail: two files");
    t.Eq(IdIn(both[BoShort]), BoId, "the lower id at the short name");
    t.Eq(IdIn(both[Bo2Full]), Bo2Id, "the other at its full id");
    OWFiles::RemoveTree(in);
    OWFiles::RemoveTree(out);
}

// Both names held, the short by one unread file and the full id by another: the writer refuses, names both paths, and
// writes and packs nothing; ToFiles leaves the element out rather than give its bytes a held path.
OW_TEST(held_both_names_held_is_a_refusal) {
    const std::string in = P().tmp + "/held-both-in";
    const std::string out = P().tmp + "/held-both-out";
    OWFileMap files;
    files["world.json"] = Bytes(WorldJson);
    files[BoShort] = Bytes("{\"id\": \"x1\", \"name\": \"Bo\"");
    files[BoFull] = Bytes("{\"id\": \"x2\", \"name\": \"Bo\"");
    for (bool archive : {false, true}) {
        const std::string label = archive ? "from an .owworld" : "from a folder";
        std::unique_ptr<OWWorld> w = ReadBack(files, archive, in);
        if (!w) {
            t.Fail(label + ": did not read");
            continue;
        }
        w->Add("character", Body(BoId, "Bo"));
        const std::string why = w->WriteRefusal();
        t.Ok(why.find(BoShort) != std::string::npos && why.find(BoFull) != std::string::npos,
             label + ": a refusal naming both paths, got \"" + why + "\"");
        t.Eq(w->WriteRefusal(true), why, label + ": the same for an edited-only write");
        OWFileMap whole = w->ToFiles();
        t.Ok(whole[BoShort] == files[BoShort] && whole[BoFull] == files[BoFull], label + ": ToFiles keeps both held files");
        OWFiles::RemoveTree(out);
        std::string err;
        t.Ok(!w->WriteFolder(out, false, &err), label + ": WriteFolder refuses");
        t.Eq(err, why, label + ": and says why");
        t.Ok(!OWFiles::IsDir(out), label + ": nothing written");
        if (!archive) {
            t.Ok(!w->WriteFolder(in, true), label + ": refused in place too");
            t.Ok(FileAt(in + "/" + BoFull) == files[BoFull], label + ": the held file at the full id untouched");
        }
        t.Ok(!w->Pack("w").ok, label + ": Pack refuses");
    }
    OWFiles::RemoveTree(in);
    OWFiles::RemoveTree(out);
}

// -- a file the reader could not read -----------------------------------------------------------------------------------

// An element file that is not UTF-8 is no element: nothing served, nothing to edit, and its bytes travel whole through
// every write and pack, with other elements edited beside it.
OW_TEST(held_a_file_not_utf8_travels_as_its_bytes) {
    const std::string in = P().tmp + "/held-utf8-in";
    const std::string out = P().tmp + "/held-utf8-out";
    const std::string c1p = "elements/character/ana--000000c1.json";
    const std::string c2p = "elements/character/lat--000000c2.json";
    OWFileMap files;
    files["world.json"] = Bytes(WorldJson);
    files[c1p] = Bytes("{\n  \"id\": \"019a9300-0000-7000-8000-0000000000c1\",\n  \"name\": \"Ana\"\n}\n");
    files[c2p] = Bytes("{\n  \"id\": \"019a9300-0000-7000-8000-0000000000c2\",\n  \"name\": \"Lat\",\n  \"description\": \"caf\xE9\"\n}\n");
    for (bool archive : {false, true}) {
        const std::string label = archive ? "from an .owworld" : "from a folder";
        std::unique_ptr<OWWorld> w = ReadBack(files, archive, in);
        if (!w) {
            t.Fail(label + ": did not read");
            continue;
        }
        t.Ok(w->Get("019a9300-0000-7000-8000-0000000000c2") == nullptr, label + ": not served");
        t.Ok(w->other_files[c2p] == files[c2p], label + ": kept as its bytes");
        for (OWElement& e : w->elements) e.Set("supertype", OWJsonValue::String("hero"));
        t.Ok(w->ToFiles()[c2p] == files[c2p], label + ": ToFiles, every element edited");
        OWFiles::RemoveTree(out);
        t.Ok(w->WriteFolder(out) && FileAt(out + "/" + c2p) == files[c2p], label + ": written whole elsewhere");
        if (!archive) t.Ok(w->WriteFolder(in, true) && FileAt(in + "/" + c2p) == files[c2p], label + ": edited-only in place");
        OWArchiveRead back = OWWorldArchive::Read(w->Pack("w").bytes);
        t.Ok(back.ok && back.files[c2p] == files[c2p], label + ": packed");
    }
    WriteTree(in, files);
    OWArchiveRead folder = OWWorldArchive::Read(OWWorldArchive::PackFolder(in).bytes);
    t.Ok(folder.ok && folder.files[c2p] == files[c2p], "PackFolder");
    OWFiles::RemoveTree(in);
    OWFiles::RemoveTree(out);
}

// A world.json the reader could not read (not UTF-8, cut short, a value after the value, not an object) is never
// written over, even once the caller sets an id: the writer refuses, names the file, and writes nothing. A world with
// no world.json at all still takes the caller's id and is written.
OW_TEST(held_an_unread_world_json_is_never_written_over) {
    const std::string in = P().tmp + "/held-world-in";
    const std::string c1p = "elements/character/ana--000000c1.json";
    const std::vector<std::pair<std::string, std::string>> unread = {
        {"not UTF-8", "{\n  \"id\": \"019a9200-0000-7000-8000-000000000001\",\n  \"name\": \"caf\xE9\"\n}\n"},
        {"cut short", "{\n  \"id\": \"019a9200-0000-7000-8000-000000000001\",\n  \"name\": \"W\"\n"},
        {"a value after the value", WorldJson + " {\"hidden\": true}"},
        {"not an object", "[\"019a9200-0000-7000-8000-000000000001\"]"}};
    for (const auto& [what, text] : unread) {
        OWFileMap files;
        files["world.json"] = Bytes(text);
        files[c1p] = Bytes("{\n  \"id\": \"019a9300-0000-7000-8000-0000000000c1\",\n  \"name\": \"Ana\"\n}\n");
        for (bool from_files : {false, true}) {
            const std::string label = "world.json " + what + (from_files ? ", FromFiles" : ", a folder");
            std::unique_ptr<OWWorld> w = from_files ? OWWorld::FromFiles(files) : ReadBack(files, false, in);
            t.Eq(w->Id(), std::string(), label + ": no id served");
            w->Get("019a9300-0000-7000-8000-0000000000c1")->Set("supertype", OWJsonValue::String("hero"));
            t.Eq(w->WriteRefusal(true), std::string(), label + ": an element's edit alone is no refusal");
            w->SetInfo("id", OWJsonValue::String("019a9200-0000-7000-8000-000000000001"));
            const std::string why = w->WriteRefusal(true);
            t.Ok(why.find("world.json") != std::string::npos && why.find("could not be read") != std::string::npos,
                 label + ": set an id, a refusal naming world.json, got \"" + why + "\"");
            t.Eq(w->WriteRefusal(), why, label + ": the same for a whole write");
            t.Ok(w->ToFiles(true).count("world.json") == 0, label + ": ToFiles gives no world.json to write over it");
            t.Ok(w->ToFiles()["world.json"] == files["world.json"], label + ": and a whole ToFiles its bytes as read");
            if (!from_files) {
                std::string err;
                t.Ok(!w->WriteFolder(in, true, &err), label + ": WriteFolder refuses");
                t.Eq(err, why, label + ": and says why");
                t.Ok(FileAt(in + "/world.json") == files["world.json"], label + ": world.json as it was");
            }
            t.Ok(!w->Pack("w").ok, label + ": Pack refuses");
        }
    }
    std::unique_ptr<OWWorld> draft = OWWorld::FromFiles(OWFileMap());
    draft->SetInfo("id", OWJsonValue::String("019a9200-0000-7000-8000-000000000001"));
    t.Eq(draft->WriteRefusal(), std::string(), "no world.json at all: the caller's id is written");
    OWFiles::RemoveTree(in);
}

// -- a number no double holds -------------------------------------------------------------------------------------------

// An element holding such a number, edited in another field, is written with the number's text as read, through every
// door; world.json's too, through the api strip and an edit.
OW_TEST(held_a_number_past_a_double_is_written_as_read) {
    const std::string in = P().tmp + "/held-number-in";
    const std::string bp = "elements/character/bo--000000b0.json";
    const std::vector<std::string> numbers = {"1e400", "1E400", "-1e400", "1.5e+400", std::string(4301, '9')};
    for (const std::string& n : numbers) {
        const std::string label = n.size() > 20 ? "4301 nines" : n;
        OWFileMap files;
        files["world.json"] = Bytes(WorldJson);
        files[bp] = Bytes("{\n  \"id\": \"019a9300-0000-7000-8000-0000000000b0\",\n  \"name\": \"Bo\",\n  \"x_n\": " + n +
                          ",\n  \"x_deep\": [1, {\"k\": [" + n + "]}]\n}\n");
        for (bool archive : {false, true}) {
            const std::string at = label + (archive ? ", .owworld" : ", folder");
            std::unique_ptr<OWWorld> w = ReadBack(files, archive, in);
            OWElement* e = w ? w->Get("019a9300-0000-7000-8000-0000000000b0") : nullptr;
            t.Ok(e != nullptr, at + ": read as an element");
            if (!e) continue;
            e->Set("supertype", OWJsonValue::String("hero"));
            auto kept = [&](const OWBytes& b) {
                std::string s(b.begin(), b.end());
                return s.find("\"x_n\": " + n + ",") != std::string::npos && s.find("\"k\": [\n        " + n + "\n") != std::string::npos &&
                       s.find("\"supertype\": \"hero\"") != std::string::npos;
            };
            t.Ok(kept(w->ToFiles(true)[bp]), at + ": ToFiles");
            if (!archive) t.Ok(w->WriteFolder(in, true) && kept(FileAt(in + "/" + bp)), at + ": written in place");
            OWArchiveRead back = OWWorldArchive::Read(w->Pack("w").bytes);
            t.Ok(back.ok && kept(back.files[bp]), at + ": packed");
        }
    }
    OWFileMap world;
    world["world.json"] = Bytes("{\n  \"id\": \"019a9200-0000-7000-8000-000000000001\",\n  \"name\": \"W\",\n  \"x_n\": 1e400,\n"
                                "  \"api\": {\"base\": \"x\"}\n}\n");
    auto kept = [](const OWBytes& b) { return std::string(b.begin(), b.end()).find("\"x_n\": 1e400") != std::string::npos; };
    OWArchiveRead ar = OWWorldArchive::Read(OWWorldArchive::PackFiles(world, "w").bytes);
    t.Ok(ar.ok && kept(ar.files["world.json"]), "world.json: the .owworld reader's api strip");
    OWArchiveRead pw = OWWorldArchive::Read(OWWorldArchive::PackWorld(world, "w").bytes);
    t.Ok(pw.ok && kept(pw.held_world_json), "world.json: PackWorld's api strip");
    std::unique_ptr<OWWorld> w = OWWorld::FromFiles(world);
    w->SetInfo("name", OWJsonValue::String("W2"));
    t.Ok(kept(w->ToFiles(true)["world.json"]), "world.json: an edit to its name");
    OWFiles::RemoveTree(in);
}
