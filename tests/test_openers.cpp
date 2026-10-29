// OWOpeners on the npm SDK's and the Python package's shared fixture (tests/data/openers.fixture.json, the same bytes,
// held by its sha256): every case through OpenersForJson, each expected row compared whole, each refusal by the address
// refusal's reason or by the key its message names. Then OWTools::All(), the baked tools list, through the typed
// OpenersFor.

#include "ow_test.h"
#include "sha256.h"

#include "OWAddress.h"
#include "OWFiles.h"
#include "OWJson.h"
#include "OWOpeners.h"

#include <memory>
#include <set>

using namespace OnlyWorlds;
using owtest::P;

namespace {

const char* const FixtureSha256 = "d10485997d9149805284eb1d2b907a4e349dedd7524f514f923ad25bf50e7e45";
const size_t CaseCount = 41;

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
    OWOpenersResult r = OWOpeners::OpenersForJson(*tools, Str(c, "kind"), *address, *surface);
    if (r.ok) return One("expect", Rows(r.rows));
    OWJsonValue refusal = OWJsonValue::Object();
    if (!r.reason.empty()) {
        refusal.Set("error", OWJsonValue::String("address"));
        refusal.Set("reason", OWJsonValue::String(r.reason));
        return One("refuse", refusal);
    }
    const OWJsonValue* refuse = c.Find("refuse");
    if (refuse && Str(*refuse, "error") == "type" && r.message.find(Str(*refuse, "names")) != std::string::npos)
        return One("refuse", *refuse);
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
    t.Eq(cases->Size(), CaseCount, "41 cases");
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
    for (const OWTool& x : all) {
        if (x.rung != "listed" || !x.open || !x.open->world) continue;
        want.push_back(x.id);
        if (!x.open->needs) want_public.push_back(x.id);
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
    for (const OWOpener& r : OWOpeners::OpenersFor(all, OWOpeners::World, world, pub).rows) got_public.push_back(r.id);
    t.Ok(got_public == want_public, "the public's rows");
    t.Eq(std::string(OWTools::Commit).size(), size_t(40), "the pinned commit");
    t.Eq(std::string(OWTools::Sha256).size(), size_t(64), "the list's sha256");
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
