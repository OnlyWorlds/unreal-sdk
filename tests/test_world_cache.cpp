// OWWorldCache: the held world OWSync fills. Ported test by test from the Godot SDK's tests/test_world_cache.gd, itself
// the Unity SDK's OWWorldCacheTests, each named as it is there; Unity's serialization tests are round trips through
// the cache's own file, as Godot's are. The core holds bodies, not typed models, so a "typed" check reads the held
// type and the body. Then Unreal's own: the OWWorld view and the file's edges.

#include "ow_test.h"

#include "OWFiles.h"
#include "OWJson.h"
#include "OWWorld.h"
#include "OWWorldCache.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace OnlyWorlds;
using owtest::P;

namespace {

OWWorldCache Cache() { return OWWorldCache("w-1", "Test World"); }

OWJsonValue Body(const std::string& text) { return OWJson::Parse(text).value; }

void Add(OWWorldCache& cache, const std::string& id, const std::string& type, const std::string& name) {
    cache.Upsert(type, Body("{\"id\":\"" + id + "\",\"type\":\"" + type + "\",\"name\":\"" + name + "\"}"));
}

std::string Field(const OWWorldCache& cache, const std::string& id, const char* key) {
    const OWCachedElement* e = cache.Get(id);
    const OWJsonValue* v = e ? e->body.Find(key) : nullptr;
    return v && (v->IsString() || v->IsNumber()) ? v->Text() : "<none>";
}

// A path in the tests' scratch folder for one test's file, removed first.
std::string PathFor(const std::string& name) {
    OWFiles::MakeDirs(P().tmp);
    std::string path = P().tmp + "/world-cache-" + name + ".json";
    std::remove(path.c_str());
    return path;
}

void WriteText(const std::string& path, const std::string& text) {
    OWFiles::Write(path, OWBytes(text.begin(), text.end()));
}

}  // namespace

OW_TEST(world_cache_get_by_id_returns_typed_element) {
    OWWorldCache cache = Cache();
    Add(cache, "c1", "character", "Quillon");
    const OWCachedElement* c = cache.Get("c1");
    t.Ok(c != nullptr, "held");
    t.Eq(c ? c->type : std::string(), "character", "its type");
    t.Eq(c ? c->id : std::string(), "c1", "its id");
    t.Eq(Field(cache, "c1", "name"), "Quillon", "");
}

OW_TEST(world_cache_a_known_of_stub_says_so_by_id_and_on_its_typed_model) {
    // Folder format section 4.4: a stub's unset fields read as a whole element with nothing filled in would; the flag
    // is what tells the two apart. The core has no typed model: the body's own flag stands for it.
    OWWorldCache cache = Cache();
    cache.Upsert("character", Body("{\"id\":\"c1\",\"type\":\"character\",\"name\":\"Dev\",\"supertype\":\"Ferryman\","
                                   "\"known_of\":true}"));
    Add(cache, "c2", "character", "Mara");
    cache.Upsert("character", Body("{\"id\":\"c3\",\"type\":\"character\",\"name\":\"Oz\",\"known_of\":false}"));
    t.Ok(cache.IsKnownOf("c1"), "");
    const OWJsonValue* flag = cache.Get("c1") ? cache.Get("c1")->body.Find("known_of") : nullptr;
    t.Ok(flag && flag->IsBool() && flag->AsBool(), "on the body");
    t.Eq(Field(cache, "c1", "supertype"), "Ferryman", "");
    t.Ok(!cache.IsKnownOf("c2"), "");
    t.Ok(cache.Get("c2") && !cache.Get("c2")->body.Find("known_of"), "");
    t.Ok(!cache.IsKnownOf("c3"), "only true marks a stub");
    t.Ok(!cache.IsKnownOf("nope"), "");
    t.Ok(!cache.IsKnownOf(""), "");
}

OW_TEST(world_cache_missing_id_returns_null_not_an_exception) {
    OWWorldCache cache = Cache();
    t.Ok(cache.Get("nope") == nullptr, "");
    t.Ok(cache.Get("") == nullptr, "");
    t.Ok(!cache.Has("nope"), "");
}

OW_TEST(world_cache_upsert_replaces_rather_than_duplicates) {
    OWWorldCache cache = Cache();
    Add(cache, "c1", "character", "First");
    Add(cache, "c1", "character", "Second");
    t.Eq(cache.Count(), size_t(1), "");
    t.Eq(Field(cache, "c1", "name"), "Second", "");
    t.Ok(!cache.Upsert("character", Body("{\"name\":\"No id\"}")), "an element needs an id");
    t.Ok(!cache.Upsert("character", Body("{\"id\":\"\",\"name\":\"Empty id\"}")), "a non-empty one");
    t.Ok(!cache.Upsert("character", Body("{\"id\":7,\"name\":\"Number id\"}")), "a string");
    t.Ok(!cache.Upsert("character", Body("[\"c9\"]")), "an object");
    t.Ok(!cache.Upsert("", Body("{\"id\":\"c9\"}")), "and a type");
    t.Eq(cache.Count(), size_t(1), "");
}

OW_TEST(world_cache_upsert_moves_an_element_when_its_type_changes) {
    OWWorldCache cache = Cache();
    Add(cache, "x1", "character", "Was a character");
    t.Eq(cache.OfType("character").size(), size_t(1), "");
    Add(cache, "x1", "location", "Now a location");
    t.Eq(cache.Count(), size_t(1), "it is still one element, not two");
    t.Eq(cache.OfType("character").size(), size_t(0), "the old type lets go of it");
    t.Eq(cache.OfType("location").size(), size_t(1), "the new type picks it up");
    t.Eq(cache.Get("x1") ? cache.Get("x1")->type : std::string(), "location", "");
}

OW_TEST(world_cache_all_filters_by_type) {
    OWWorldCache cache = Cache();
    Add(cache, "c1", "character", "A");
    Add(cache, "c2", "character", "B");
    Add(cache, "p1", "pin", "P");
    t.Eq(cache.OfType("character").size(), size_t(2), "");
    t.Eq(cache.OfType("pin").size(), size_t(1), "");
    t.Eq(cache.OfType("location").size(), size_t(0), "");
    t.Eq(cache.Elements().size(), size_t(3), "");
    std::vector<const OWCachedElement*> all = cache.Elements();
    t.Ok(all.size() == 3 && all[0]->id == "c1" && all[1]->id == "c2" && all[2]->id == "p1", "in the order first held");
}

OW_TEST(world_cache_resolve_skips_dangling_links) {
    OWWorldCache cache = Cache();
    Add(cache, "c1", "character", "A");
    Add(cache, "c2", "character", "B");
    t.Eq(cache.Resolve(std::vector<std::string>{"c1", "missing", "c2"}).size(), size_t(2),
         "a dangling link is normal, not an error");
    std::vector<const OWCachedElement*> back = cache.Resolve(std::vector<std::string>{"c2", "c1"});
    t.Ok(back.size() == 2 && back[0]->id == "c2", "in the list's order");
    t.Eq(cache.Resolve(Body("[\"c2\", 7, null]")).size(), size_t(1), "a link field's own list");
    t.Eq(cache.Resolve(Body("\"c1\"")).size(), size_t(1), "a single link");
    t.Eq(cache.Resolve(OWJsonValue()).size(), size_t(0), "");
}

OW_TEST(world_cache_remove_rebuilds_the_index_correctly) {
    OWWorldCache cache = Cache();
    Add(cache, "c1", "character", "A");
    Add(cache, "c2", "character", "B");
    Add(cache, "c3", "character", "C");
    t.Ok(cache.Remove("c2"), "");
    t.Ok(!cache.Remove("c2"), "not held any more");
    t.Eq(cache.Count(), size_t(2), "");
    t.Ok(!cache.Has("c2"), "");
    t.Eq(Field(cache, "c1", "name"), "A", "");
    t.Eq(Field(cache, "c3", "name"), "C", "");
    t.Eq(cache.OfType("character").size(), size_t(2), "");
}

OW_TEST(world_cache_extension_fields_survive_caching) {
    OWWorldCache cache = Cache();
    cache.Upsert("character", Body("{\"id\":\"c1\",\"name\":\"K\",\"x_atlas_pinned\":true}"));
    t.Ok(cache.Get("c1") && OWJson::Stringify(cache.Get("c1")->body).find("x_atlas_pinned") != std::string::npos, "");
}

OW_TEST(world_cache_nullable_state_survives_caching) {
    OWWorldCache cache = Cache();
    cache.Upsert("character", Body("{\"id\":\"c1\",\"name\":\"K\",\"level\":0,\"charisma\":null}"));
    std::string path = PathFor("nullable");
    t.Ok(cache.WriteFile(path), "written");
    std::unique_ptr<OWWorldCache> back = OWWorldCache::ReadFile(path);
    const OWCachedElement* c = back ? back->Get("c1") : nullptr;
    const OWJsonValue* level = c ? c->body.Find("level") : nullptr;
    const OWJsonValue* charisma = c ? c->body.Find("charisma") : nullptr;
    t.Ok(level && level->IsNumber() && level->Text() == "0", "a deliberate zero survived");
    t.Ok(charisma && charisma->IsNull(), "an unset field stayed unset");
    std::remove(path.c_str());
}

OW_TEST(world_cache_index_rebuilds_after_deserialization) {
    // Unity's domain reload: here the cache is written to its file and read back, and its index answers.
    OWWorldCache cache = Cache();
    Add(cache, "c1", "character", "A");
    Add(cache, "p1", "pin", "P");
    std::string path = PathFor("index");
    t.Ok(cache.WriteFile(path), "written");
    std::unique_ptr<OWWorldCache> back = OWWorldCache::ReadFile(path);
    t.Ok(back != nullptr, "read");
    if (!back) return;
    t.Ok(back->Has("c1"), "");
    t.Eq(back->Get("c1") ? back->Get("c1")->type : std::string(), "character", "typed again");
    t.Eq(Field(*back, "c1", "name"), "A", "");
    t.Eq(back->OfType("pin").size(), size_t(1), "");
    t.Eq(back->world_id, "w-1", "");
    t.Eq(back->world_name, "Test World", "");
    std::remove(path.c_str());
}

OW_TEST(world_cache_read_only_flag_is_carried) {
    OWWorldCache snapshot("w-1", "Chapter 1", true);
    t.Ok(snapshot.read_only, "a frozen snapshot must say so: consumers MUST honour it");
    std::string path = PathFor("frozen");
    t.Ok(snapshot.WriteFile(path), "written");
    std::unique_ptr<OWWorldCache> back = OWWorldCache::ReadFile(path);
    t.Ok(back && back->read_only, "and through the file");
    std::remove(path.c_str());
}

OW_TEST(world_cache_all_raw_returns_wire_bodies_including_unmodeled_fields) {
    OWWorldCache cache = Cache();
    cache.Upsert("character", Body("{\"id\":\"c1\",\"name\":\"K\",\"x_atlas_pinned\":true}"));
    Add(cache, "c2", "character", "B");
    Add(cache, "p1", "pin", "P");
    std::vector<const OWCachedElement*> raw = cache.OfType("character");
    t.Eq(raw.size(), size_t(2), "");
    bool any = false;
    for (const OWCachedElement* e : raw) any = any || e->body.Has("x_atlas_pinned");
    t.Ok(any, "the body whole");
    t.Eq(cache.OfType("location").size(), size_t(0), "");
}

OW_TEST(world_cache_cursor_round_trips) {
    OWWorldCache cache = Cache();
    cache.SetCursor(202, "", std::nullopt, "2026-07-28T18:00:00Z");
    t.Eq(cache.Cursor(), int64_t(202), "");
    t.Eq(cache.SyncedAt(), "2026-07-28T18:00:00Z", "");
}

OW_TEST(world_cache_cursor_token_and_epoch_are_kept_and_a_bare_seq_drops_them) {
    OWWorldCache cache = Cache();
    cache.SetCursor(202, "202:ffffffff-ffff-ffff-ffff-ffffffffffff:4", 4, "t");
    t.Eq(cache.CursorToken(), "202:ffffffff-ffff-ffff-ffff-ffffffffffff:4", "");
    t.Eq(cache.Epoch().value_or(-1), int64_t(4), "");
    // A token beside a seq someone set by hand would resume from the wrong place.
    cache.SetCursor(150);
    t.Eq(cache.CursorToken(), "", "");
    t.Ok(!cache.Epoch(), "");
    cache.SetCursor(202, "202:x", 0, "t");
    t.Ok(cache.Epoch() && *cache.Epoch() == 0, "epoch 0 is a value, not its absence");
    cache.Clear();
    t.Eq(cache.Cursor(), int64_t(0), "");
    t.Eq(cache.CursorToken(), "", "");
    t.Ok(!cache.Epoch(), "");
}

OW_TEST(world_cache_cursor_token_survives_unity_serialization_and_an_old_cache_has_none) {
    // Here: the cache's own file. A file holding only the seq reads with no token, and the sync resumes from the seq.
    OWWorldCache cache = Cache();
    cache.SetCursor(202, "202:abc", 4, "t");
    std::string path = PathFor("token");
    t.Ok(cache.WriteFile(path), "written");
    std::unique_ptr<OWWorldCache> back = OWWorldCache::ReadFile(path);
    t.Ok(back != nullptr, "read");
    if (back) {
        t.Eq(back->Cursor(), int64_t(202), "an int again");
        t.Eq(back->CursorToken(), "202:abc", "");
        t.Eq(back->Epoch().value_or(-1), int64_t(4), "an int again");
        t.Eq(back->SyncedAt(), "t", "");
    }
    WriteText(path, "{\"format\":\"onlyworlds-unreal-cache\",\"version\":1,\"cursor\":77,\"synced_at\":\"t\"}");
    std::unique_ptr<OWWorldCache> seq_only = OWWorldCache::ReadFile(path);
    t.Ok(seq_only != nullptr, "read");
    if (seq_only) {
        t.Eq(seq_only->Cursor(), int64_t(77), "");
        t.Eq(seq_only->CursorToken(), "", "no token: the sync resumes from the bare seq");
        t.Ok(!seq_only->Epoch(), "");
        t.Eq(seq_only->Count(), size_t(0), "");
    }
    std::remove(path.c_str());
    std::unique_ptr<OWWorldCache> none = OWWorldCache::ReadFile(path);
    t.Ok(none && none->Count() == 0, "no file: an empty cache");
    WriteText(path, "{\"format\":\"something else\"}");
    std::string why;
    t.Ok(OWWorldCache::ReadFile(path, &why) == nullptr, "a file that is not a cache's: null");
    t.Ok(why.find("format") != std::string::npos, "and why: " + why);
    std::remove(path.c_str());
}

// -- Unreal's own -------------------------------------------------------------------------------------------------------

OW_TEST(world_cache_to_world_reads_as_an_owworld) {
    OWWorldCache cache = Cache();
    Add(cache, "c1", "character", "A");
    Add(cache, "p1", "pin", "P");
    Add(cache, "c2", "character", "B");
    cache.Upsert("location", Body("{\"id\":\"l1\",\"name\":\"No declared type\",\"x_k\":1}"));
    std::unique_ptr<OWWorld> w = cache.ToWorld();
    t.Ok(w != nullptr, "a world");
    if (!w) return;
    t.Eq(w->Id(), "w-1", "world.json's id is the cache's");
    const OWJsonValue* name = w->info.Find("name");
    t.Eq(name && name->IsString() ? name->Text() : std::string(), "Test World", "and its name");
    t.Eq(w->elements.size(), cache.Count(), "every element");
    bool same = w->elements.size() == cache.Count();
    std::vector<const OWCachedElement*> held = cache.Elements();
    for (size_t i = 0; same && i < held.size(); ++i)
        same = w->elements[i].Id() == held[i]->id && w->elements[i].body == held[i]->body &&
               w->elements[i].Type() == held[i]->type;
    t.Ok(same, "in the order held, each body and type as held");
    t.Eq(w->OfType("character").size(), cache.OfType("character").size(), "");
    t.Ok(w->Get("l1") && w->Get("l1")->Type() == "location", "a body that declares no type reads as its held type");
    t.Ok(w->errors.empty(), "nothing it could not read");
}

OW_TEST(world_cache_the_file_keeps_each_number_as_it_was_served) {
    OWWorldCache cache = Cache();
    cache.Upsert("character", Body("{\"id\":\"c1\",\"name\":\"K\",\"x_big\":9007199254740993,\"x_frac\":1.50}"));
    cache.SetCursor(9007199254740993LL, "9007199254740993:z", 9007199254740993LL, "t");
    std::string path = PathFor("numbers");
    t.Ok(cache.WriteFile(path), "written");
    std::unique_ptr<OWWorldCache> back = OWWorldCache::ReadFile(path);
    t.Ok(back != nullptr, "read");
    if (back) {
        t.Eq(Field(*back, "c1", "x_big"), "9007199254740993", "past 2^53, its digits");
        t.Eq(Field(*back, "c1", "x_frac"), "1.50", "its text");
        t.Eq(back->Cursor(), int64_t(9007199254740993LL), "the cursor too");
        t.Eq(back->Epoch().value_or(0), int64_t(9007199254740993LL), "and the epoch");
    }
    std::remove(path.c_str());
}

OW_TEST(world_cache_read_file_refuses_what_is_not_a_cache) {
    std::string path = PathFor("refused");
    struct Case {
        const char* text;
        const char* why;
    };
    const Case cases[] = {
        {"not json", "not JSON"},
        {"[1,2]", "not a JSON object"},
        {"{\"format\":\"onlyworlds-godot-cache\",\"version\":1}", "format"},
        {"{\"format\":\"onlyworlds-unreal-cache\",\"version\":2}", "version"},
        {"{\"format\":\"onlyworlds-unreal-cache\",\"version\":\"1\"}", "version"},
    };
    for (const Case& c : cases) {
        WriteText(path, c.text);
        std::string why;
        t.Ok(OWWorldCache::ReadFile(path, &why) == nullptr, std::string(c.text) + ": refused");
        t.Ok(why.find(c.why) != std::string::npos, std::string(c.text) + ": " + why);
    }
    WriteText(path, "{\"format\":\"onlyworlds-unreal-cache\",\"version\":1,\"elements\":[{\"type\":\"pin\"},"
                    "{\"type\":\"pin\",\"element\":{\"id\":\"p1\"}},7,{\"type\":\"pin\",\"element\":{\"name\":\"x\"}}]}");
    std::unique_ptr<OWWorldCache> rows = OWWorldCache::ReadFile(path);
    t.Ok(rows && rows->Count() == 1 && rows->Has("p1"), "a row it would not hold is skipped");
    std::remove(path.c_str());
}
