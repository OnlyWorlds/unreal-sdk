// OWOpeners on the npm SDK's and the Python package's shared fixture (tests/data/openers.fixture.json, the same bytes,
// held by its sha256): every case through OpenersForJson, each expected row compared whole, each refusal by the address
// refusal's reason or by the key its message names and its kind (an unknown key, worded "unknown key 'k'", or a refused
// value). Then OWTools::All(), the baked tools list, through the typed OpenersFor.

#include "ow_test.h"
#include "sha256.h"

#include "OWAddress.h"
#include "OWFiles.h"
#include "OWGraph.h"
#include "OWJson.h"
#include "OWOpeners.h"

#include <algorithm>
#include <memory>
#include <set>

using namespace OnlyWorlds;
using owtest::P;

namespace {

// The npm SDK's test/fixtures/openers.fixture.json at 8f2986b (4.132.0: element_types, the types a tool's element door
// opens, 8 cases more), read at v4.132.2.
const char* const FixtureSha256 = "9df2b52916223969f8b48284c770f933673cef308b2f5158cb2007715e80c17b";
const size_t CaseCount = 110;

const std::string World = "0199c3a1-7b2e-7c3d-8e4f-5a6b7c8d9e0f";
const std::string Element = "0199c3a1-7b2e-7c3d-8e4f-5a6b7c8d9e1a";

std::string FixturePath() { return P().root + "/tests/data/openers.fixture.json"; }

std::shared_ptr<OWJsonValue> Fixture() {
    static std::shared_ptr<OWJsonValue> doc;
    if (!doc) {
        OWBytes bytes;
        OWFiles::Read(FixturePath(), bytes);
        OWJsonParseResult r = OWJson::Parse(bytes);
        doc = std::make_shared<OWJsonValue>(r.ok ? std::move(r.value) : OWJsonValue());
    }
    return doc;
}

std::string Str(const OWJsonValue& v, const char* key) {
    const OWJsonValue* x = v.Find(key);
    return x && x->IsString() ? x->Text() : std::string();
}

std::string Compact(const OWJsonValue& v) {
    std::string s = OWJson::Stringify(v), out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\n') {
            out.push_back(s[i]);
            continue;
        }
        while (i + 1 < s.size() && s[i + 1] == ' ') ++i;
    }
    return out;
}

// A row as the fixture writes one: id, name, href, state (null when none).
OWJsonValue RowJson(const OWOpener& r) {
    OWJsonValue o = OWJsonValue::Object();
    o.Set("id", OWJsonValue::String(r.id));
    o.Set("name", OWJsonValue::String(r.name));
    o.Set("href", OWJsonValue::String(r.href));
    o.Set("state", r.state ? OWJsonValue::String(*r.state) : OWJsonValue::Null());
    return o;
}

OWJsonValue Rows(const std::vector<OWOpener>& rows) {
    OWJsonValue a = OWJsonValue::Array();
    for (const OWOpener& r : rows) a.Push(RowJson(r));
    return a;
}

OWJsonValue One(const char* key, OWJsonValue v) {
    OWJsonValue o = OWJsonValue::Object();
    o.Set(key, std::move(v));
    return o;
}

// What the fixture says: {expect} or {refuse}.
OWJsonValue Want(const OWJsonValue& c) {
    if (const OWJsonValue* refuse = c.Find("refuse")) return One("refuse", *refuse);
    const OWJsonValue* expect = c.Find("expect");
    return One("expect", expect ? *expect : OWJsonValue());
}

// One case's answer in the fixture's own shape.
OWJsonValue Answer(const OWJsonValue& c) {
    const OWJsonValue* lists = Fixture()->Find("lists");
    const OWJsonValue* tools = lists ? lists->Find(Str(c, "tools")) : nullptr;
    const OWJsonValue* address = c.Find("address");
    const OWJsonValue* surface = c.Find("surface");
    if (!tools || !address || !surface) return One("threw", OWJsonValue::String("the case names no list, address or surface"));
    // The fixture's options, when present, are the fifth argument as written ({element}).
    OWOpenersResult r = OWOpeners::OpenersForJson(*tools, Str(c, "kind"), *address, *surface, c.Find("options"));
    if (r.ok) return One("expect", Rows(r.rows));
    OWJsonValue refusal = OWJsonValue::Object();
    if (!r.reason.empty()) {
        refusal.Set("error", OWJsonValue::String("address"));
        refusal.Set("reason", OWJsonValue::String(r.reason));
        return One("refuse", refusal);
    }
    const OWJsonValue* refuse = c.Find("refuse");
    if (refuse && Str(*refuse, "error") == "type" && r.message.find(Str(*refuse, "names")) != std::string::npos) {
        // This package's own words for a key the object does not take; any other refusal is of a value.
        const std::string names = Str(*refuse, "names");
        const bool unknown = r.message.find("unknown key '" + names + "'") != std::string::npos;
        refusal.Set("error", OWJsonValue::String("type"));
        refusal.Set("names", OWJsonValue::String(names));
        refusal.Set("kind", OWJsonValue::String(unknown ? "unknown_key" : "bad_value"));
        return One("refuse", refusal);
    }
    refusal.Set("error", OWJsonValue::String("type"));
    refusal.Set("message", OWJsonValue::String(r.message));
    return One("refuse", refusal);
}

void Case(owtest::T& t, const OWJsonValue& c) {
    OWJsonValue want = Want(c), got = Answer(c);
    t.Ok(got == want, Str(c, "name") + ": gave " + Compact(got) + ", the fixture says " + Compact(want));
}

OWOpenerSurface Member(std::optional<bool> signed_in) {
    OWOpenerSurface s;
    s.audience = OWOpeners::Member;
    s.signed_in = signed_in;
    return s;
}

}  // namespace

OW_TEST(openers_fixture_is_the_shared_bytes) {
    OWBytes raw;
    t.Ok(OWFiles::Read(FixturePath(), raw), "the openers fixture is vendored: " + FixturePath());
    t.Eq(owtest::Sha256Hex(raw), std::string(FixtureSha256), "the fixture's bytes are the packages'");
}

OW_TEST(openers_every_case_is_answered_and_none_is_dropped) {
    const OWJsonValue* cases = Fixture()->Find("cases");
    t.Ok(cases && cases->IsArray(), "the fixture parses, with its cases");
    if (!cases) return;
    t.Eq(cases->Size(), CaseCount, std::to_string(CaseCount) + " cases");
    std::set<std::string> names;
    size_t answered = 0;
    for (const OWJsonValue& c : cases->Items()) {
        names.insert(Str(c, "name"));
        Case(t, c);
        ++answered;
    }
    t.Eq(names.size(), CaseCount, "every case's name is its own");
    t.Eq(answered, CaseCount, "every case answered");
}

OW_CASES(openers_cases) {
    owtest::Cases out;
    const OWJsonValue* cases = Fixture()->Find("cases");
    if (!cases || !cases->IsArray()) {
        out.emplace_back("openers:fixture-not-found", [](owtest::T& t) { t.Fail("the openers fixture was not found: " + FixturePath()); });
        return out;
    }
    for (const OWJsonValue& c : cases->Items()) {
        OWJsonValue copy = c;
        out.emplace_back("openers:" + Str(c, "name"), [copy](owtest::T& t) { Case(t, copy); });
    }
    return out;
}

OW_TEST(openers_a_reading_and_the_parts_give_the_same_rows_as_json) {
    std::string link = "https://www.onlyworlds.com/w/" + World + "/character/" + Element + "@3?lens=" + Element;
    OWOpenerSurface surface = Member(false);
    OWAddressParts parts;
    parts.form = OWAddress::Text;
    parts.world = World;
    parts.type = "character";
    parts.id = Element;
    OWOpenersResult read = OWOpeners::OpenersFor(OWTools::All(), OWOpeners::Element, OWAddress::Parse(link), surface);
    OWOpenersResult typed = OWOpeners::OpenersFor(OWTools::All(), OWOpeners::Element, parts, surface);
    OWJsonParseResult address = OWJson::Parse("{\"form\": \"text\", \"world\": \"" + World +
                                              "\", \"type\": \"character\", \"id\": \"" + Element + "\"}");
    OWJsonParseResult surf = OWJson::Parse("{\"audience\": \"member\", \"signed_in\": false}");
    OWJsonValue tools = OWJsonValue::Array();
    for (const OWTool& x : OWTools::All()) {
        OWJsonValue row = OWJsonValue::Object(), open = OWJsonValue::Object();
        auto opt = [](const std::optional<std::string>& s) { return s ? OWJsonValue::String(*s) : OWJsonValue::Null(); };
        row.Set("id", OWJsonValue::String(x.id));
        row.Set("name", OWJsonValue::String(x.name));
        row.Set("url", OWJsonValue::String(x.url));
        row.Set("client", opt(x.client));
        row.Set("rung", OWJsonValue::String(x.rung));
        if (x.open) {
            open.Set("world", opt(x.open->world));
            open.Set("element", opt(x.open->element));
            open.Set("needs", opt(x.open->needs));
            OWJsonValue skips = OWJsonValue::Array();
            for (const std::string& w : x.open->element_skips) skips.Push(OWJsonValue::String(w));
            open.Set("element_skips", skips);
            if (x.open->element_types) {
                OWJsonValue types = OWJsonValue::Array();
                for (const std::string& w : *x.open->element_types) types.Push(OWJsonValue::String(w));
                open.Set("element_types", types);
            } else {
                open.Set("element_types", OWJsonValue::Null());
            }
            row.Set("open", open);
        } else {
            row.Set("open", OWJsonValue::Null());
        }
        tools.Push(row);
    }
    OWOpenersResult json = OWOpeners::OpenersForJson(tools, OWOpeners::Element, address.value, surf.value);
    t.Ok(read.ok && typed.ok && json.ok, "all three read: " + read.message + typed.message + json.message);
    t.Ok(Rows(read.rows) == Rows(typed.rows), "a reading gives the parts' rows: " + Compact(Rows(read.rows)));
    t.Ok(Rows(json.rows) == Rows(typed.rows), "JSON gives the parts' rows: " + Compact(Rows(json.rows)));
}

OW_TEST(openers_a_reading_by_slug_is_bad_world) {
    OWOpenersResult r = OWOpeners::OpenersFor(OWTools::All(), OWOpeners::World,
                                              OWAddress::Parse("https://www.onlyworlds.com/w/old-harbour"), Member(std::nullopt));
    t.Ok(!r.ok, "refused");
    t.Eq(r.reason, std::string("bad_world"), "the reason");
    t.Eq(r.rows.size(), size_t(0), "no rows");
}

OW_TEST(openers_the_baked_list_gives_the_sites_row_then_each_listed_tool_with_a_door) {
    const std::vector<OWTool>& all = OWTools::All();
    t.Ok(!all.empty(), "a row at least");
    OWAddressParts world;
    world.form = OWAddress::Human;
    world.world = World;
    OWOpenersResult member = OWOpeners::OpenersFor(all, OWOpeners::World, world, Member(true));
    t.Ok(member.ok, "read: " + member.message);
    std::vector<std::string> want{"onlyworlds"}, want_public{"onlyworlds"}, got, got_public;
    std::vector<std::string> want_public_state{""}, got_public_state;
    for (const OWTool& x : all) {
        if (x.rung != "listed" || !x.open || !x.open->world) continue;
        want.push_back(x.id);
        // The public: a tool that needs the device is not there; one that needs an account says so.
        if (x.open->needs && *x.open->needs == "device") continue;
        want_public.push_back(x.id);
        want_public_state.push_back(x.open->needs ? OWOpeners::NeedsAnAccount : "");
    }
    for (const OWOpener& r : member.rows) got.push_back(r.id);
    t.Ok(got == want, "the site, then the list's order");
    t.Eq(member.rows.empty() ? std::string() : member.rows[0].href, "https://www.onlyworlds.com/w/" + World, "the site's row");
    for (size_t i = 1; i < member.rows.size(); ++i) {
        const std::string& href = member.rows[i].href;
        t.Ok(href.rfind("https://", 0) == 0 && href.find(World) != std::string::npos && href.find('{') == std::string::npos, href);
    }
    OWOpenerSurface pub;
    pub.audience = OWOpeners::Public;
    for (const OWOpener& r : OWOpeners::OpenersFor(all, OWOpeners::World, world, pub).rows) {
        got_public.push_back(r.id);
        got_public_state.push_back(r.state.value_or(""));
    }
    t.Ok(got_public == want_public, "the public's rows");
    t.Ok(got_public_state == want_public_state, "the public's words: an account tool's says it needs one");
    t.Eq(std::string(OWTools::Commit).size(), size_t(40), "the pinned commit");
    t.Eq(std::string(OWTools::Sha256).size(), size_t(64), "the list's sha256");
}

OW_TEST(openers_the_baked_list_has_little_lens_and_the_menu_offers_it_to_anyone) {
    const OWTool* lens = nullptr;
    for (const OWTool& x : OWTools::All())
        if (x.id == "little-lens") lens = &x;
    t.Ok(lens != nullptr, "Little Lens is a row of the baked list");
    if (!lens) return;
    t.Eq(lens->rung, std::string("listed"), "listed");
    t.Ok(lens->open && !lens->open->needs, "it needs no account");
    OWOpenerSurface pub;
    pub.audience = OWOpeners::Public;
    OWAddressParts world;
    world.form = OWAddress::Human;
    world.world = World;
    OWAddressParts element = world;
    element.type = "character";
    element.id = Element;
    auto Href = [&](const char* kind, const OWAddressParts& a) {
        OWOpenersResult r = OWOpeners::OpenersFor(OWTools::All(), kind, a, pub);
        t.Ok(r.ok, "read: " + r.message);
        for (const OWOpener& o : r.rows)
            if (o.id == "little-lens") return o.href;
        return std::string("(not offered)");
    };
    t.Eq(Href(OWOpeners::World, world), "https://little-lens.onlyworlds.com/#/open/" + World, "offered for a world");
    t.Eq(Href(OWOpeners::Element, element), "https://little-lens.onlyworlds.com/#/open/" + World + "/element/" + Element, "offered for an element");
}

// -- offline (the surface's online, false) ----------------------------------------------------------------------

// The fixture's online-not-a-boolean case names only "online", which a reader that does not know the key also names
// (as an unknown key): the refusal itself, by its words, for each value the Python package's tests try.
OW_TEST(openers_an_online_that_is_not_a_boolean_is_refused_by_name) {
    OWJsonParseResult address = OWJson::Parse("{\"form\": \"human\", \"world\": \"" + World + "\"}");
    for (const char* v : {"\"no\"", "null", "0", "1", "\"false\""}) {
        OWJsonParseResult surface = OWJson::Parse(std::string("{\"audience\": \"member\", \"online\": ") + v + "}");
        OWOpenersResult r = OWOpeners::OpenersForJson(OWJsonValue::Array(), OWOpeners::World, address.value, surface.value);
        t.Ok(!r.ok && r.reason.empty() && r.rows.empty(), std::string("online ") + v + ": refused, not an address's refusal");
        t.Ok(r.message.find("surface online is a boolean") != std::string::npos,
             std::string("online ") + v + ": the message says so: " + r.message);
    }
    OWJsonParseResult yes = OWJson::Parse("{\"audience\": \"member\", \"online\": true}");
    OWJsonParseResult absent = OWJson::Parse("{\"audience\": \"member\"}");
    OWOpenersResult a = OWOpeners::OpenersForJson(OWJsonValue::Array(), OWOpeners::World, address.value, yes.value);
    OWOpenersResult b = OWOpeners::OpenersForJson(OWJsonValue::Array(), OWOpeners::World, address.value, absent.value);
    t.Ok(a.ok && b.ok && Rows(a.rows) == Rows(b.rows), "online true is online absent");
}

OW_TEST(openers_offline_the_baked_list_keeps_every_href) {
    OWAddressParts parts;
    parts.form = OWAddress::Human;
    parts.world = World;
    parts.type = "character";
    parts.id = Element;
    OWOpenerSurface on = Member(true), off = Member(true);
    off.online = false;
    OWOpenersResult a = OWOpeners::OpenersFor(OWTools::All(), OWOpeners::Element, parts, on);
    OWOpenersResult b = OWOpeners::OpenersFor(OWTools::All(), OWOpeners::Element, parts, off);
    t.Ok(a.ok && b.ok && a.rows.size() == b.rows.size() && !b.rows.empty(), "the same rows, online and off");
    for (size_t i = 0; i < b.rows.size() && i < a.rows.size(); ++i) {
        t.Eq(b.rows[i].href, a.rows[i].href, b.rows[i].id + ": the href stays");
        t.Eq(b.rows[i].state.value_or(""), std::string(OWOpeners::NeedsAConnection), b.rows[i].id + ": a web row, offline");
    }
    t.Ok(OWOpenerSurface().online, "online by default");
}

OW_TEST(openers_offline_the_scheme_in_any_case_and_a_link_with_no_scheme) {
    auto tool = [](const char* id, const char* world) {
        OWTool x;
        x.id = id;
        x.name = id;
        x.url = "https://example.org";
        x.client = id;
        x.rung = "listed";
        x.open = OWToolOpen{std::string(world), std::nullopt, std::nullopt};
        return x;
    };
    std::vector<OWTool> tools{tool("upper", "HTTPS://upper.example/{world}"), tool("mixed", "Http://mixed.example/{world}"),
                              tool("app", "app://open?world={world}"), tool("ftp", "ftp://ftp.example/{world}")};
    OWAddressParts parts;
    parts.form = OWAddress::Human;
    parts.world = World;
    OWOpenerSurface off = Member(std::nullopt);
    off.online = false;
    OWOpenersResult r = OWOpeners::OpenersFor(tools, OWOpeners::World, parts, off);
    t.Ok(r.ok && r.rows.size() == 5, "the site and four rows: " + r.message);
    if (r.rows.size() != 5) return;
    const std::string word = OWOpeners::NeedsAConnection;
    t.Eq(r.rows[0].state.value_or(""), word, "the site's row");
    t.Eq(r.rows[1].state.value_or(""), word, "HTTPS: in capitals is a web address");
    t.Eq(r.rows[2].state.value_or(""), word, "Http: in mixed case is one");
    t.Ok(!r.rows[3].state, "an app's link is not one");
    t.Ok(!r.rows[4].state, "ftp: is not one");
    // A link with no scheme (//host, or https// with no colon) is refused since 4.123.2: it begins with its scheme.
    for (const char* bare : {"//relative.example/{world}", "https//bare.example/{world}"}) {
        OWOpenersResult no = OWOpeners::OpenersFor({tool("bare", bare)}, OWOpeners::World, parts, off);
        t.Ok(!no.ok && no.reason.empty() && no.rows.empty(), std::string(bare) + ": refused, not an address's refusal");
        t.Ok(no.message.find("tool bare: open.world begins with its scheme") != std::string::npos,
             std::string(bare) + ": the message names the row and the key: " + no.message);
    }
    OWOpenerSurface own = off;
    own.tool = "upper";
    OWOpenersResult here = OWOpeners::OpenersFor(tools, OWOpeners::World, parts, own);
    t.Eq(here.rows.size() > 1 ? here.rows[1].state.value_or("") : std::string(), std::string(OWOpeners::YouAreHere),
         "You are here before Needs a connection");
}

OW_TEST(openers_a_game_outside_a_browser_says_opens_in_your_browser) {
    OWTool zephyr;
    zephyr.id = "zephyr";
    zephyr.name = "Zephyr";
    zephyr.url = "https://zephyr.example";
    zephyr.client = "zephyr";
    zephyr.rung = "listed";
    zephyr.open = OWToolOpen{"https://zephyr.example/{world}", std::nullopt, "account"};
    OWAddressParts parts;
    parts.form = OWAddress::Human;
    parts.world = World;
    parts.type = "character";
    parts.id = Element;
    OWOpenerSurface s = Member(false);
    s.browser = false;
    OWOpenersResult r = OWOpeners::OpenersFor({zephyr}, OWOpeners::Element, parts, s);
    t.Ok(r.ok, "read: " + r.message);
    t.Eq(r.rows.size(), size_t(2), "two rows");
    if (r.rows.size() < 2) return;
    t.Eq(r.rows[1].state.value_or(""), std::string(OWOpeners::OpensInYourBrowser), "the word");
    t.Eq(r.rows[1].href, "https://zephyr.example/" + World, "the world door");
}

// -- element_skips beside the fixture ---------------------------------------------------------------------------

OW_TEST(openers_the_baked_list_little_lens_skips_four_marks_atlas_and_lantern_none) {
    std::vector<std::string> lens, atlas, lantern;
    for (const OWTool& x : OWTools::All()) {
        if (!x.open) continue;
        if (x.id == "little-lens") lens = x.open->element_skips;
        if (x.id == "atlas") atlas = x.open->element_skips;
        if (x.id == "lantern") lantern = x.open->element_skips;
    }
    t.Ok(lens == std::vector<std::string>{"room", "hidden", "proposed", "map_space"}, "Little Lens skips four marks");
    t.Ok(atlas.empty() && lantern.empty(), "Atlas and Lantern none");
    OWJsonParseResult pin = OWJson::Parse("{\"id\": \"" + Element + "\", \"type\": \"pin\", \"name\": \"A pin\"}");
    OWAddressParts parts;
    parts.form = OWAddress::Human;
    parts.world = World;
    parts.type = "pin";
    parts.id = Element;
    OWOpenersOptions options;
    options.element = &pin.value;
    OWOpenersResult r = OWOpeners::OpenersFor(OWTools::All(), OWOpeners::Element, parts, Member(std::nullopt), options);
    std::vector<std::string> ids;
    for (const OWOpener& o : r.rows) ids.push_back(o.id);
    t.Ok(r.ok && ids == std::vector<std::string>{"onlyworlds", "atlas", "lantern"}, "a Pin's menu leaves Little Lens out: " + r.message);
}

OW_TEST(openers_a_skip_outside_the_words_is_refused_a_row_built_in_code_too) {
    OWTool thorn;
    thorn.id = "thorn";
    thorn.name = "Thorn";
    thorn.url = "https://thorn.example";
    thorn.rung = "listed";
    thorn.open = OWToolOpen{"https://thorn.example/{world}", "https://thorn.example/{world}/{id}", std::nullopt, {"private"}};
    OWAddressParts world;
    world.form = OWAddress::Human;
    world.world = World;
    OWOpenersResult r = OWOpeners::OpenersFor({thorn}, OWOpeners::World, world, Member(std::nullopt));
    t.Ok(!r.ok && r.reason.empty() && r.message.find("tool thorn: open.element_skips") != std::string::npos, "refused: " + r.message);
    OWJsonParseResult address = OWJson::Parse("{\"form\": \"human\", \"world\": \"" + World + "\"}");
    OWJsonParseResult surface = OWJson::Parse("{\"audience\": \"member\"}");
    for (const char* skips : {"\"hidden\"", "[7]", "{}"}) {
        OWJsonParseResult tools = OWJson::Parse(std::string("[{\"id\": \"thorn\", \"name\": \"Thorn\", \"rung\": \"listed\", \"open\": "
                                                            "{\"world\": \"https://thorn.example/{world}\", \"element\": null, \"needs\": null, "
                                                            "\"element_skips\": ") + skips + "}}]");
        OWOpenersResult j = OWOpeners::OpenersForJson(tools.value, OWOpeners::World, address.value, surface.value);
        t.Ok(!j.ok && j.message.find("tool thorn: open.element_skips") != std::string::npos, std::string(skips) + ": refused: " + j.message);
    }
    OWJsonParseResult bad = OWJson::Parse("[1]");
    OWOpenersResult o = OWOpeners::OpenersForJson(OWJsonValue::Array(), OWOpeners::World, address.value, surface.value, &bad.value);
    t.Ok(!o.ok && o.message.find("the options are an object") != std::string::npos, "options that are not an object: " + o.message);
}

// The menu reads an element's marks as OWGraph::WorldGraph does: on every element of the lens fixture's worlds, a door
// that skips one word is left out exactly when the graph reader, asked about that element alone, says it carries that
// mark: room, hidden and proposed from a node's marks with every flag on, map space from the cut with that one flag
// off, a reading aid from the one kind no flag brings back. An element under the world's hidden list carries
// hidden: true, as the owner is served it. The npm SDK's openers-marks.test.mjs, ported.
OW_TEST(openers_every_skip_word_leaves_a_door_out_exactly_when_the_graph_reads_that_mark) {
    OWBytes bytes;
    t.Ok(OWFiles::Read(P().root + "/codegen/schema-dist/profiles/lens.fixture.json", bytes), "the lens fixture is vendored");
    OWJsonParseResult doc = OWJson::Parse(bytes);
    const OWJsonValue* worlds = doc.ok ? doc.value.Find("worlds") : nullptr;
    t.Ok(worlds && worlds->IsObject(), "the lens fixture parses, with its worlds");
    if (!worlds) return;
    auto door_id = [](std::string word) {
        std::replace(word.begin(), word.end(), '_', '-');
        return "skip-" + word;
    };
    std::vector<OWTool> tools;
    for (const std::string& word : OWOpeners::ElementMarks()) {
        OWTool x;
        x.id = door_id(word);
        x.name = word;
        x.url = "https://x.example";
        x.rung = "listed";
        x.open = OWToolOpen{"https://x.example/#/w/{world}", "https://x.example/#/w/{world}/e/{id}", std::nullopt, {word}};
        tools.push_back(x);
    }
    OWGraphOptions all;
    all.rooms = all.hidden = all.proposed = all.map_space = true;
    OWGraphOptions no_map = all;
    no_map.map_space = false;
    OWJsonParseResult owner = OWJson::Parse("{\"rung\": \"owner\"}");
    auto count = [](const std::vector<std::pair<std::string, int64_t>>& cut, const char* reason) {
        for (const auto& [k, n] : cut)
            if (k == reason) return n;
        return int64_t(0);
    };
    size_t n = 0;
    std::set<std::string> seen;
    std::vector<std::string> wrong;
    for (const std::string& key : worlds->Keys()) {
        const OWJsonValue& w = *worlds->Find(key);
        std::set<std::string> hidden;
        if (const OWJsonValue* h = w.Find("hidden"); h && h->IsArray())
            for (const OWJsonValue& id : h->Items())
                if (id.IsString()) hidden.insert(id.Text());
        const OWJsonValue* elements = w.Find("elements");
        if (!elements || !elements->IsArray()) continue;
        for (const OWJsonValue& raw : elements->Items()) {
            OWJsonValue el = raw;
            const std::string id = Str(el, "id");
            if (hidden.count(id)) el.Set("hidden", OWJsonValue::Bool(true));
            OWJsonValue world = OWJsonValue::Object(), list = OWJsonValue::Array();
            list.Push(el);
            world.Set("elements", list);
            OWWorldGraph full = OWGraph::WorldGraph(world, owner.value, all);
            OWWorldGraph less_map = OWGraph::WorldGraph(world, owner.value, no_map);
            std::set<std::string> want;
            if (full.nodes.size() == 1)
                for (const std::string& m : full.nodes[0].marks) want.insert(m);
            if (full.nodes.size() == 1 && less_map.nodes.empty() && count(less_map.cut, "map_space") == 1) want.insert("map_space");
            if (full.nodes.empty() && count(full.cut, "reading_aid") == 1) want.insert("reading_aid");
            seen.insert(want.begin(), want.end());
            std::string type = Str(el, "type");
            std::transform(type.begin(), type.end(), type.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? char(c + 32) : c; });
            OWAddressParts parts;
            parts.form = OWAddress::Human;
            parts.world = Str(w, "id");
            parts.type = type;
            parts.id = id;
            OWOpenersOptions options;
            options.element = &el;
            OWOpenersResult r = OWOpeners::OpenersFor(tools, OWOpeners::Element, parts, Member(std::nullopt), options);
            if (!r.ok) {
                wrong.push_back(Str(w, "name") + " " + Str(el, "name") + ": refused, " + r.message);
                continue;
            }
            for (const std::string& word : OWOpeners::ElementMarks()) {
                bool gone = std::none_of(r.rows.begin(), r.rows.end(), [&](const OWOpener& o) { return o.id == door_id(word); });
                if (gone != (want.count(word) > 0))
                    wrong.push_back(Str(w, "name") + " " + type + " " + Str(el, "name") + ": " + word + (gone ? " skipped" : " kept") +
                                    ", the graph " + (want.count(word) ? "marks it" : "does not"));
            }
            ++n;
        }
    }
    std::string lines;
    for (const std::string& line : wrong) lines += "\n  " + line;
    t.Ok(wrong.empty(), "the menu and the graph agree:" + lines);
    t.Ok(n >= 100, "read " + std::to_string(n) + " elements");
    const std::vector<std::string>& marks = OWOpeners::ElementMarks();
    t.Ok(seen == std::set<std::string>(marks.begin(), marks.end()), "every word met at least once");
}

// -- element_types beside the fixture ---------------------------------------------------------------------------

namespace {

OWTool Kite(std::optional<std::vector<std::string>> types, bool element_door = true) {
    OWTool x;
    x.id = "kite";
    x.name = "Kite";
    x.url = "https://kite.example";
    x.client = "kite";
    x.rung = "listed";
    x.open = OWToolOpen{"https://kite.example/{world}",
                        element_door ? std::optional<std::string>("https://kite.example/{world}/{id}") : std::nullopt,
                        std::nullopt, {}, std::move(types)};
    return x;
}

OWAddressParts ElementOf(const char* type) {
    OWAddressParts p;
    p.form = OWAddress::Human;
    p.world = World;
    p.type = type;
    p.id = Element;
    return p;
}

std::vector<std::string> Ids(const OWOpenersResult& r) {
    std::vector<std::string> ids;
    for (const OWOpener& o : r.rows) ids.push_back(o.id);
    return ids;
}

const std::string NotOffered = "(not offered)";

}  // namespace

// A row built in code is refused as the JSON form refuses it: a repeat (which no fixture case holds; the npm SDK and
// the Python package refuse it by name), a word outside the 22, an uppercase one, an empty list, and types on a row
// with only a world door.
OW_TEST(openers_element_types_that_are_not_a_list_of_distinct_type_words_are_refused_a_row_built_in_code_too) {
    const std::string types = "tool kite: open.element_types is null or a non-empty list of distinct type words (";
    const std::vector<std::vector<std::string>> bad{
        {"character", "character"}, {"dragon"}, {"Character"}, {}, {"location", "character", "location"}};
    for (const std::vector<std::string>& words : bad) {
        OWOpenersResult r = OWOpeners::OpenersFor({Kite(words)}, OWOpeners::Element, ElementOf("character"), Member(true));
        t.Ok(!r.ok && r.reason.empty() && r.rows.empty() && r.message.find(types) != std::string::npos,
             std::to_string(words.size()) + " words: refused: " + r.message);
    }
    OWAddressParts world;
    world.form = OWAddress::Human;
    world.world = World;
    OWOpenersResult door =
        OWOpeners::OpenersFor({Kite(std::vector<std::string>{"character"}, false)}, OWOpeners::World, world, Member(true));
    t.Ok(!door.ok &&
             door.message.find("tool kite: open.element_types names what an element door opens, and the row has none") !=
                 std::string::npos,
         "types on a row with no element door, refused on a world's menu too: " + door.message);
    const std::string head = "[{\"id\": \"kite\", \"name\": \"Kite\", \"rung\": \"listed\", \"open\": "
                             "{\"world\": \"https://kite.example/{world}\", \"element\": \"https://kite.example/{world}/{id}\", "
                             "\"needs\": null";
    OWJsonParseResult address = OWJson::Parse("{\"form\": \"human\", \"world\": \"" + World + "\"}");
    OWJsonParseResult surface = OWJson::Parse("{\"audience\": \"member\"}");
    for (const char* v : {"\"character\"", "[7]", "{}", "[\"character\", \"character\"]", "true", "[]"}) {
        OWJsonParseResult tools = OWJson::Parse(head + ", \"element_types\": " + v + "}}]");
        OWOpenersResult j = OWOpeners::OpenersForJson(tools.value, OWOpeners::World, address.value, surface.value);
        t.Ok(tools.ok && !j.ok && j.reason.empty() && j.message.find(types) != std::string::npos,
             std::string(v) + ": refused: " + j.message);
    }
    // Null, or no key at all (a list older than the key), is every type.
    OWJsonParseResult zone =
        OWJson::Parse("{\"form\": \"human\", \"world\": \"" + World + "\", \"type\": \"zone\", \"id\": \"" + Element + "\"}");
    for (const char* v : {", \"element_types\": null", ""}) {
        OWJsonParseResult tools = OWJson::Parse(head + v + "}}]");
        OWOpenersResult j = OWOpeners::OpenersForJson(tools.value, OWOpeners::Element, zone.value, surface.value);
        t.Ok(j.ok && Ids(j) == std::vector<std::string>{"onlyworlds", "kite"},
             std::string("element_types ") + (*v ? "null" : "absent") + ": a Zone's menu offers it: " + j.message);
    }
}

// On an element, a list leaves out every other type, world door and all; a world's menu reads no types.
OW_TEST(openers_element_types_offer_a_tool_on_its_kinds_only) {
    const std::vector<OWTool> tools{Kite(std::vector<std::string>{"character", "creature"})};
    for (const char* type : OWAddress::TypeWords) {
        OWOpenersResult r = OWOpeners::OpenersFor(tools, OWOpeners::Element, ElementOf(type), Member(true));
        const bool kin = std::string(type) == "character" || std::string(type) == "creature";
        const std::vector<std::string> want =
            kin ? std::vector<std::string>{"onlyworlds", "kite"} : std::vector<std::string>{"onlyworlds"};
        t.Ok(r.ok && Ids(r) == want, std::string(type) + (kin ? ": offered" : ": left out") + " " + r.message);
    }
    OWAddressParts world;
    world.form = OWAddress::Human;
    world.world = World;
    OWOpenersResult w = OWOpeners::OpenersFor(tools, OWOpeners::World, world, Member(true));
    t.Ok(w.ok && Ids(w) == std::vector<std::string>{"onlyworlds", "kite"}, "a world's menu reads no types: " + w.message);
}

// The baked list at 60a590e: Tactical Tangle opens a world or a Character, needs an account, and skips rooms, hidden
// and proposed elements; on any other type it is not offered. Atlas, Lantern and Little Lens open every type.
OW_TEST(openers_the_baked_list_offers_tactical_tangle_on_a_world_and_a_character_only) {
    const OWTool* tangle = nullptr;
    for (const OWTool& x : OWTools::All()) {
        if (x.id == "tangle") tangle = &x;
        else t.Ok(x.open && !x.open->element_types, x.id + ": opens every type");
    }
    t.Ok(tangle && tangle->rung == "listed" && tangle->open && tangle->open->world && tangle->open->element,
         "Tactical Tangle is listed, with a world door and an element door");
    if (!tangle || !tangle->open) return;
    t.Ok(tangle->open->needs && *tangle->open->needs == "account", "it needs an account");
    t.Ok(tangle->open->element_types == std::vector<std::string>{"character"}, "it opens Characters");
    t.Ok(tangle->open->element_skips == std::vector<std::string>{"room", "hidden", "proposed"},
         "skipping rooms, hidden and proposed elements");
    auto offered = [](const OWOpenersResult& r) {
        for (const OWOpener& o : r.rows)
            if (o.id == "tangle") return o.href;
        return NotOffered;
    };
    OWAddressParts world;
    world.form = OWAddress::Human;
    world.world = World;
    t.Eq(offered(OWOpeners::OpenersFor(OWTools::All(), OWOpeners::World, world, Member(true))),
         "https://tangle.onlyworlds.com/#/open/" + World, "a world's menu");
    for (const char* type : OWAddress::TypeWords) {
        const std::string want = std::string(type) == "character"
                                     ? "https://tangle.onlyworlds.com/#/open/" + World + "/element/" + Element
                                     : NotOffered;
        t.Eq(offered(OWOpeners::OpenersFor(OWTools::All(), OWOpeners::Element, ElementOf(type), Member(true))), want, type);
    }
    OWJsonParseResult hidden =
        OWJson::Parse("{\"id\": \"" + Element + "\", \"type\": \"character\", \"name\": \"Ana\", \"hidden\": true}");
    OWOpenersOptions options;
    options.element = &hidden.value;
    t.Eq(offered(OWOpeners::OpenersFor(OWTools::All(), OWOpeners::Element, ElementOf("character"), Member(true), options)),
         NotOffered, "a hidden Character");
    OWOpenerSurface pub;
    pub.audience = OWOpeners::Public;
    OWOpenersResult visitor = OWOpeners::OpenersFor(OWTools::All(), OWOpeners::Element, ElementOf("character"), pub);
    t.Ok(visitor.ok && !visitor.rows.empty() && visitor.rows.back().id == "tangle" &&
             visitor.rows.back().state.value_or("") == OWOpeners::NeedsAnAccount,
         "the public's Character menu: Tangle last, Needs an account");
}
