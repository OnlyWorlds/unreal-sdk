// Joining a world by link through a fake transport (tests/fake_transport.h): no test touches the network. The code's
// shape in a pasted link (OWJoin::ParseLink), the two keyless POSTs (OWJoin::Preview, OWJoin::Redeem) and both of
// /join's answers, the refusals keel answers and the ones made before sending, and a person's join on an account token
// (OWClient::AcceptJoin). Ported from the Godot SDK's tests/test_join.gd, itself from the Unity SDK's OWJoinTests and
// OWAccountRouteTests (AcceptJoin) with the TypeScript SDK's vectors (test/player-join.test.mjs, test/v2-client.test.mjs)
// where they ask more. The bodies are keel's schemas field for field (accounts/join_api.py: PLAYER_JOINED_SCHEMA,
// JOIN_SCHEMA, PREVIEW_SCHEMA). Every key and PIN here is a made-up string.

#include "ow_test.h"
#include "fake_transport.h"

#include "OWCharter.h"
#include "OWClient.h"
#include "OWJoin.h"
#include "OWJson.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace OnlyWorlds;
using owtest::FakeTransport;

namespace {

const std::string Base = "https://example.test/api/v2";
const std::string Code = "ow_j_EoEUrAfQ9xumy-lbBJqXix3q1xl49-P4";

const char* const PlayerJoined =
    "{\"kind\":\"player\",\"key\":\"ow_r_guest\","
    "\"world\":{\"id\":\"0196f0a2-0000-7000-8000-000000000001\",\"name\":\"Moppetopia\",\"description\":\"\"},"
    "\"character\":{\"id\":\"0196f0a2-0000-7000-8000-0000000000c1\",\"name\":\"Pip\"},\"role\":\"guest\"}";

const char* const AgentJoined =
    "{\"kind\":\"agent\","
    "\"world\":{\"id\":\"0196f0a2-0000-7000-8000-000000000001\",\"name\":\"Moppetopia\",\"description\":\"\"},"
    "\"seat\":{\"id\":\"s1\",\"role\":\"contributor\",\"agent_name\":\"Scribe\"},"
    "\"with_character\":false,\"character\":null,\"key\":\"ow_w_seat\",\"pin\":\"ow_s_for_tests\","
    "\"base_url\":\"https://example.test/api/v2\",\"env\":\"OW_KEY=ow_w_seat\",\"next\":[],\"members\":[],"
    "\"front_door\":{\"sponsor\":{\"id\":\"m-sam\",\"kind\":\"person\",\"name\":\"The Mapmaker\"},"
    "\"sponsor_character\":\"c-map\",\"my_character\":null,\"charter\":\"law-1\",\"charter_skill\":[\"obj-1\",\"obj-2\"]},"
    "\"reading_aids\":{\"charter\":{\"id\":\"law-1\",\"name\":\"House rules\",\"rules\":[],\"skill\":[\"obj-1\",\"obj-2\"]},"
    "\"guide\":null,\"domains\":[{\"id\":\"con-d1\",\"name\":\"The Harbour\",\"sentence\":\"Where the ships come in.\","
    "\"hubs\":[]}]}}";

const char* const PreviewBody =
    "{\"reader\":\"player\",\"world\":{\"name\":\"Moppetopia\",\"description\":\"A demo\"},"
    "\"invited_by\":\"Sam\",\"note\":null,\"owner_character\":\"The Mapmaker\",\"role\":\"guest\","
    "\"with_character\":true,\"expires_at\":\"2026-10-26T14:00:00+00:00\",\"glimpse\":null}";

// A person link's preview with the note its maker wrote (keel's preview, accounts/join_api.py: note, a person link's
// only, plain text with its line breaks, else null).
const char* const PersonPreview =
    "{\"reader\":\"person\",\"world\":{\"name\":\"Moppetopia\",\"description\":\"A demo\"},"
    "\"invited_by\":\"Sam\",\"note\":\"Come and help with the harbour.\\nBring the maps.\",\"owner_character\":null,"
    "\"role\":\"contributor\",\"with_character\":false,\"expires_at\":\"2026-10-26T14:00:00+00:00\",\"glimpse\":null}";

// The glimpse the TypeScript SDK's test sends (4.189.0): a contributor's link, two things, one link between them.
const char* const Glimpse =
    "{\"picked_by\":\"links\",\"things\":[{\"name\":\"Gela\",\"type\":\"location\"},{\"name\":\"Ada\",\"type\":"
    "\"character\"}],\"links\":[{\"from\":1,\"to\":0,\"field\":\"location\",\"label\":\"is at\"}]}";

// A party link's preview: keel's table_preview keys after the seven (accounts/agents.py, PR 110).
const char* const TablePreview =
    "{\"reader\":\"player\",\"world\":{\"name\":\"Moppetopia\",\"description\":\"A demo\"},"
    "\"invited_by\":\"Sam\",\"note\":null,\"owner_character\":null,\"role\":\"guest\",\"with_character\":true,"
    "\"expires_at\":\"2026-10-26T14:00:00+00:00\",\"glimpse\":null,"
    "\"max_uses\":4,\"uses_left\":3,\"name_required\":false,"
    "\"characters\":[{\"id\":\"c-ana\",\"name\":\"Ana\",\"image_url\":null,\"taken\":false},"
    "{\"id\":\"c-bo\",\"name\":\"Bo\",\"image_url\":\"https://media.onlyworlds.com/bo.webp\",\"taken\":true}]}";

const char* const Membership =
    "{\"id\":\"m7\",\"world_id\":\"w1\",\"world_name\":\"Ostvik\",\"role\":\"contributor\","
    "\"joined_at\":\"2026-10-31T01:00:00+00:00\",\"roster_character\":null}";

OWJsonValue Parse(std::string_view text) { return OWJson::Parse(text).value; }

std::shared_ptr<FakeTransport> Answering(int status, std::string body) {
    auto f = std::make_shared<FakeTransport>();
    f->status = status;
    f->body = std::move(body);
    return f;
}

std::shared_ptr<FakeTransport> Refusing(int status, const std::string& code, std::optional<std::string> param) {
    std::string p = param ? OWJson::Quote(*param) : "null";
    return Answering(status, "{\"error\":{\"type\":\"invalid_request\",\"code\":" + OWJson::Quote(code) +
                                 ",\"param\":" + p + ",\"message\":\"refused\"}}");
}

OWJoinConfig Where(const std::shared_ptr<FakeTransport>& fake) {
    OWJoinConfig where;
    where.transport = fake;
    where.base_url = Base;
    return where;
}

// A handler that keeps the reply and counts its calls.
template <typename T>
struct Got {
    OWReply<T> reply;
    int calls = 0;
    OWHandler<T> H() {
        return [this](OWReply<T> r) {
            reply = std::move(r);
            ++calls;
        };
    }
};

OWReply<OWJoined> RedeemOn(const std::shared_ptr<FakeTransport>& fake, const OWRedeemOptions& options = {}) {
    Got<OWJoined> got;
    OWJoin::Redeem(Code, options, Where(fake), got.H());
    return got.reply;
}

OWReply<OWJoinPreview> PreviewOn(const std::shared_ptr<FakeTransport>& fake) {
    Got<OWJoinPreview> got;
    OWJoin::Preview(Code, Where(fake), got.H());
    return got.reply;
}

std::vector<std::string> HeaderNames(const OWRequest& r) {
    std::vector<std::string> out;
    for (const auto& h : r.headers) out.push_back(h.first);
    std::sort(out.begin(), out.end());
    return out;
}

void NoCredential(owtest::T& t, const OWRequest& r, const std::string& label) {
    for (const char* h : {"API-Key", "API-Pin", "Authorization"})
        t.Ok(r.Header(h) == nullptr, label + ": no " + h);
}

template <typename V>
void Refused(owtest::T& t, const OWReply<V>& r, int calls, const FakeTransport& fake, const std::string& words,
             const std::string& label) {
    t.Eq(calls, 1, label + ": answered once, before the call returned");
    t.Ok(r.error && r.error->kind == OWApiError::Kind::Refused &&
             r.error->message.find(words) != std::string::npos,
         label + ": refused, '" + words + "' (got " + (r.error ? r.error->ToString() : std::string("no error")) + ")");
    if (r.error) {
        const std::string tail = "Nothing was sent.";
        const std::string& m = r.error->message;
        t.Ok(m.size() >= tail.size() && m.compare(m.size() - tail.size(), tail.size(), tail) == 0,
             label + ": the refusal says so (" + m + ")");
    }
    t.Eq(fake.calls.size(), size_t(0), label + ": nothing sent");
}

std::vector<bool> Flags(const OWApiError& e) { return {e.IsCharacterTaken(), e.IsCharacterInvalid(), e.IsWorldFull()}; }

}  // namespace

// -- ParseLink --------------------------------------------------------------------------------------------------------

OW_TEST(join_parse_link_finds_the_code) {
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"https://www.onlyworlds.com/join#" + Code, "the site's link"},
        {"  http://localhost:8000/join#" + Code + "\n", "any host, white space round it"},
        {"https://www.onlyworlds.com/join/0196f0a2-0000-7000-8000-000000000001#" + Code, "the link with the preview on"},
        {Code, "a bare code"},
        {"#" + Code, "the fragment alone"},
        {"https://www.onlyworlds.com/join# " + Code + " ", "white space after the #"},
        {"\xE3\x80\x80" + Code + "\xC2\xA0", "Unicode's white space trimmed (U+3000, U+00A0)"},
        {"\xE2\x80\xA8" + Code + "\xC2\x85", "a line separator and a next line trimmed (U+2028, U+0085)"},
        {"https://x.test/join#a#" + Code, "after the last #"},
    };
    for (const auto& [text, label] : cases) t.Eq(OWJoin::ParseLink(text), Code, label);
    t.Eq(std::string(OWJoin::CodePrefix), std::string("ow_j_"), "the prefix");
}

OW_TEST(join_parse_link_refuses_text_with_no_code) {
    const std::vector<std::string> texts = {
        "",
        "   ",
        "https://www.onlyworlds.com/join",
        "https://www.onlyworlds.com/join#",
        "ow_j_",
        "ow_j_ ",
        "ow_r_readkey",
        "ow_j_two words",
        "ow_j_two\xC2\xA0words",  // U+00A0 inside
        "ow_j_two\xE3\x80\x80words",  // U+3000 inside
        "OW_J_upper",
        "https://www.onlyworlds.com/join#" + Code + "#",  // the last # has nothing after it
        "\x1F" + Code,  // U+001F is no White_Space (Python's strip takes it; Unity's and Godot's do not)
        std::string("ow_j_\xFF\xFE"),  // not UTF-8
    };
    for (size_t i = 0; i < texts.size(); ++i)
        t.Eq(OWJoin::ParseLink(texts[i]), std::string(), "no code in text " + std::to_string(i));
}

// -- The requests -----------------------------------------------------------------------------------------------------

OW_TEST(join_redeem_posts_the_code_and_name_with_no_credential) {
    auto fake = Answering(201, PlayerJoined);
    OWRedeemOptions as;
    as.name = "Ana";
    Got<OWJoined> got;
    OWJoin::Redeem(Code, as, Where(fake), got.H());
    t.Eq(got.calls, 1, "answered once");
    t.Ok(got.reply.Ok(), "answered (" + (got.reply.error ? got.reply.error->ToString() : std::string()) + ")");
    t.Eq(fake->calls.size(), size_t(1), "one request");
    const OWRequest& a = fake->Last();
    t.Eq(a.method, std::string("POST"), "the verb");
    t.Eq(a.url, Base + "/join/", "the path, with the trailing slash the client writes, no query");
    t.Eq(a.body.value_or(""), "{\"code\":\"" + Code + "\",\"name\":\"Ana\"}", "the code and the name, in keel's spelling");
    t.Ok(HeaderNames(a) == std::vector<std::string>{"Content-Type"}, "the media type alone: no credential");
    t.Eq(a.Header("Content-Type") ? *a.Header("Content-Type") : std::string(), std::string("application/json"), "JSON");
    NoCredential(t, a, "redeem");
    t.Ok(got.reply.response && got.reply.response->status == 201, "the transport's answer kept");
}

OW_TEST(join_redeem_leaves_out_what_the_caller_left_out) {
    // A blank name means the Character's own name; an option left unset must not arrive at all, nor as null.
    auto fake = Answering(201, PlayerJoined);
    RedeemOn(fake);
    t.Eq(fake->calls.size(), size_t(1), "sent");
    t.Eq(fake->Last().body.value_or(""), "{\"code\":\"" + Code + "\"}", "the code alone");
    OWRedeemOptions blank;
    blank.name = "";
    RedeemOn(fake, blank);
    t.Eq(fake->Last().body.value_or(""), "{\"code\":\"" + Code + "\",\"name\":\"\"}", "a name set to \"\" is sent");
    t.Eq(fake->calls.size(), size_t(2), "both sent");
}

OW_TEST(join_redeem_sends_an_agents_name) {
    auto fake = Answering(201, AgentJoined);
    OWRedeemOptions as;
    as.agent_name = "Scribe";
    RedeemOn(fake, as);
    t.Eq(fake->Last().body.value_or(""), "{\"code\":\"" + Code + "\",\"agent_name\":\"Scribe\"}", "agent_name, and no name");
}

OW_TEST(join_redeem_sends_the_pick) {
    // The TypeScript SDK's table vector: the pick as character, with or without a name, in keel's order.
    auto fake = Answering(201, PlayerJoined);
    OWRedeemOptions pick;
    pick.character = "c-ana";
    RedeemOn(fake, pick);
    t.Eq(fake->Last().body.value_or(""), "{\"code\":\"" + Code + "\",\"character\":\"c-ana\"}",
         "the code and the pick, nothing else");
    pick.name = "Ana";
    RedeemOn(fake, pick);
    t.Eq(fake->Last().body.value_or(""), "{\"code\":\"" + Code + "\",\"name\":\"Ana\",\"character\":\"c-ana\"}",
         "name before character");
    OWRedeemOptions every;
    every.character = "c1";
    every.agent_name = "A";
    every.name = "N";
    RedeemOn(fake, every);
    t.Eq(fake->Last().body.value_or(""),
         "{\"code\":\"" + Code + "\",\"name\":\"N\",\"agent_name\":\"A\",\"character\":\"c1\"}",
         "every option, in keel's order");
    OWRedeemOptions quoted;
    quoted.name = "Ana \"the Bold\"\n";
    RedeemOn(fake, quoted);
    t.Eq(fake->Last().body.value_or(""), "{\"code\":\"" + Code + "\",\"name\":\"Ana \\\"the Bold\\\"\\n\"}",
         "a name escaped as JSON.stringify escapes it");
}

OW_TEST(join_preview_posts_the_code_with_no_credential) {
    auto fake = Answering(200, PreviewBody);
    OWReply<OWJoinPreview> r = PreviewOn(fake);
    t.Ok(r.Ok(), "answered (" + (r.error ? r.error->ToString() : std::string()) + ")");
    const OWRequest& a = fake->Last();
    t.Eq(a.method, std::string("POST"), "the verb");
    t.Eq(a.url, Base + "/join/preview/", "the path, with the trailing slash the client writes, no query");
    t.Eq(a.body.value_or(""), "{\"code\":\"" + Code + "\"}", "the code alone");
    t.Ok(HeaderNames(a) == std::vector<std::string>{"Content-Type"}, "the media type alone: no credential");
    NoCredential(t, a, "preview");
}

OW_TEST(join_the_default_host_is_production) {
    auto fake = Answering(200, PreviewBody);
    OWJoinConfig plain;
    plain.transport = fake;
    OWJoin::Preview(Code, plain, [](OWReply<OWJoinPreview>) {});
    t.Eq(fake->Last().url, std::string("https://www.onlyworlds.com/api/v2/join/preview/"), "production, by default");
    t.Eq(std::string(OWJoinConfig{}.base_url), std::string(OWClient::DefaultBaseUrl), "the client's default");
    OWJoinConfig empty;
    empty.transport = fake;
    empty.base_url = "";
    OWJoin::Preview(Code, empty, [](OWReply<OWJoinPreview>) {});
    t.Eq(fake->Last().url, std::string("https://www.onlyworlds.com/api/v2/join/preview/"), "\"\" means production");
    OWJoinConfig slashes = Where(fake);
    slashes.base_url = Base + "//";
    OWJoin::Redeem(Code, {}, slashes, [](OWReply<OWJoined>) {});
    t.Eq(fake->Last().url, Base + "/join/", "a base_url's trailing slashes trimmed");
}

// -- The answers ------------------------------------------------------------------------------------------------------

OW_TEST(join_a_players_answer_is_a_read_key_and_its_character) {
    OWReply<OWJoined> r = RedeemOn(Answering(201, PlayerJoined));
    t.Ok(r.Ok(), "answered (" + (r.error ? r.error->ToString() : std::string()) + ")");
    const OWJoined& j = r.value;
    t.Ok(j.IsPlayer(), "a player's");
    t.Ok(!j.IsAgent(), "not an agent's");
    t.Eq(j.key, std::string("ow_r_guest"), "the key");
    t.Ok(OWClient::KindOf(j.key) == OWKeyKind::Read, "a read key");
    t.Ok(!j.pin, "no PIN");
    t.Ok(j.world.has_value(), "the world");
    if (j.world) {
        t.Eq(j.world->id.value_or(""), std::string("0196f0a2-0000-7000-8000-000000000001"), "the world's id, the code spent");
        t.Eq(j.world->name, std::string("Moppetopia"), "its name");
        t.Eq(j.world->description.value_or("?"), std::string(""), "its description, \"\" as served");
    }
    t.Ok(j.character.has_value(), "the Character");
    if (j.character) {
        t.Eq(j.character->id, std::string("0196f0a2-0000-7000-8000-0000000000c1"), "the Character it reads as");
        t.Eq(j.character->name, std::string("Pip"), "its name");
    }
    t.Eq(j.RoleInWorld().value_or(""), std::string("guest"), "the role, on the answer");
    t.Ok(!j.seat, "no seat");
    t.Ok(!j.base_url && !j.env, "no base URL, no .env");
    t.Ok(j.served == Parse(PlayerJoined), "the answer as served");
}

OW_TEST(join_an_agents_answer_is_a_seat_key_and_secret) {
    OWRedeemOptions as;
    as.agent_name = "Scribe";
    OWReply<OWJoined> r = RedeemOn(Answering(201, AgentJoined), as);
    t.Ok(r.Ok(), "answered (" + (r.error ? r.error->ToString() : std::string()) + ")");
    const OWJoined& j = r.value;
    t.Ok(j.IsAgent(), "an agent's");
    t.Ok(!j.IsPlayer(), "not a player's");
    t.Eq(j.key, std::string("ow_w_seat"), "the seat's write key");
    t.Eq(j.pin.value_or(""), std::string("ow_s_for_tests"), "its secret");
    t.Ok(!j.role, "no role on the answer itself");
    t.Eq(j.RoleInWorld().value_or(""), std::string("contributor"), "the role, from the seat");
    t.Ok(j.seat.has_value(), "a seat");
    if (j.seat) {
        t.Eq(j.seat->id, std::string("s1"), "the seat");
        t.Eq(j.seat->role, std::string("contributor"), "its role");
        t.Eq(j.seat->agent_name, std::string("Scribe"), "its agent's name");
    }
    t.Ok(!j.character, "no Character (null as served)");
    t.Eq(j.base_url.value_or(""), std::string("https://example.test/api/v2"), "the base URL its key is for");
    t.Eq(j.env.value_or(""), std::string("OW_KEY=ow_w_seat"), "the .env lines");
    const OWJsonValue* next = j.served.Find("next");
    t.Ok(next && next->IsArray(), "what is not modelled stays in served");
}

OW_TEST(join_an_agents_answer_carries_the_front_door_and_the_reading_aids) {
    // The sponsor who answers for the seat, their Character and the Charter to read first; the same packet as GET /me's.
    OWReply<OWJoined> r = RedeemOn(Answering(201, AgentJoined));
    const OWJoined& j = r.value;
    t.Ok(j.front_door.has_value(), "a front door");
    if (j.front_door) {
        t.Ok(*j.front_door == *Parse(AgentJoined).Find("front_door"), "the front door as served");
        const OWJsonValue* sponsor = j.front_door->Find("sponsor");
        t.Eq(sponsor && sponsor->Find("id") ? sponsor->Find("id")->Text() : "", std::string("m-sam"), "the sponsor");
        const OWJsonValue* sc = j.front_door->Find("sponsor_character");
        t.Eq(sc ? sc->Text() : "", std::string("c-map"), "the sponsor's Character");
        const OWJsonValue* skill = j.front_door->Find("charter_skill");
        t.Eq(skill ? skill->Size() : size_t(0), size_t(2), "the Charter's skill");
    }
    t.Ok(j.reading_aids.has_value(), "the reading aids, as OWCharter::Served reads them");
    if (j.reading_aids) {
        t.Ok(j.reading_aids->charter.has_value(), "the Charter");
        if (j.reading_aids->charter) {
            t.Eq(j.reading_aids->charter->id, std::string("law-1"), "its id");
            t.Ok(j.reading_aids->charter->skill == std::vector<std::string>{"obj-1", "obj-2"}, "its skill");
        }
        t.Ok(!j.reading_aids->guide, "no Guide");
        t.Eq(j.reading_aids->domains.size(), size_t(1), "one Domain");
        if (j.reading_aids->domains.size() == 1)
            t.Eq(j.reading_aids->domains[0].name, std::string("The Harbour"), "its name");
    }
}

OW_TEST(join_a_players_answer_has_no_front_door) {
    OWReply<OWJoined> r = RedeemOn(Answering(201, PlayerJoined));
    t.Ok(r.Ok(), "answered");
    t.Ok(!r.value.front_door, "no front door");
    t.Ok(!r.value.reading_aids, "nor the aids");
}

OW_TEST(join_a_preview_reads_every_field) {
    OWReply<OWJoinPreview> r = PreviewOn(Answering(200, PreviewBody));
    t.Ok(r.Ok(), "answered (" + (r.error ? r.error->ToString() : std::string()) + ")");
    const OWJoinPreview& p = r.value;
    t.Eq(p.reader, std::string("player"), "reader");
    t.Ok(p.world.has_value(), "the world");
    if (p.world) {
        t.Eq(p.world->name, std::string("Moppetopia"), "the world's name");
        t.Eq(p.world->description.value_or(""), std::string("A demo"), "its description");
        t.Ok(!p.world->id, "no id: the code is unspent");
    }
    t.Eq(p.invited_by.value_or(""), std::string("Sam"), "invited_by");
    t.Eq(p.owner_character.value_or(""), std::string("The Mapmaker"), "owner_character");
    t.Eq(p.role, std::string("guest"), "role");
    t.Ok(p.with_character, "with_character");
    t.Eq(p.expires_at, std::string("2026-10-26T14:00:00+00:00"), "expires_at, as sent");
    t.Ok(!p.note, "a player link keeps no note");
    t.Ok(!p.glimpse, "a player's link shows no glimpse");
    // A link for one carries none of a table's keys.
    t.Ok(!p.IsTable(), "not a table");
    t.Ok(!p.max_uses && !p.uses_left && !p.name_required && !p.characters, "no table keys");
}

OW_TEST(join_a_person_links_preview_reads_the_note_as_written_line_break_kept) {
    // The TypeScript SDK's OwJoinPreview.note (4.188.0): a string or null. Kills: the note left unread, read under
    // another key, or its line break lost.
    OWReply<OWJoinPreview> r = PreviewOn(Answering(200, PersonPreview));
    t.Ok(r.Ok(), "answered");
    const OWJoinPreview& p = r.value;
    t.Eq(p.reader, std::string("person"), "a person's link");
    t.Eq(p.note.value_or(""), std::string("Come and help with the harbour.\nBring the maps."), "the note, its line break kept");
    t.Eq(p.invited_by.value_or(""), std::string("Sam"), "the note is not the inviter's name");
    t.Ok(!p.with_character, "no Character for a person");
    t.Ok(!p.owner_character, "no owner's Character set");
}

OW_TEST(join_a_preview_reads_the_glimpse_a_group_or_null) {
    // The TypeScript SDK's previewJoin glimpse vector (4.189.0): the group comes back as it was sent, and null reads
    // absent. Written back out, the group is the JSON that came in, key for key and in order, so a thing read with an
    // id, a link without its label or a key read under another name fails here.
    OWJsonValue with_one = Parse(PersonPreview);
    with_one.Set("note", OWJsonValue::Null());
    with_one.Set("glimpse", Parse(Glimpse));
    std::string body = OWJson::Stringify(with_one);
    OWReply<OWJoinPreview> r = PreviewOn(Answering(200, body));
    t.Ok(r.Ok() && r.value.glimpse.has_value(), "a contributor's link shows its glimpse");
    if (r.value.glimpse) {
        const OWJoinGlimpse& g = *r.value.glimpse;
        t.Eq(g.picked_by, std::string("links"), "picked_by");
        t.Ok(g.IsLinks(), "by links");
        t.Ok(!g.IsStrand(), "not the strand");
        t.Eq(g.things.size(), size_t(2), "two things");
        if (g.things.size() == 2) {
            t.Eq(g.things[0].name + "," + g.things[1].name, std::string("Gela,Ada"), "the things' names, in order");
            t.Eq(g.things[0].type + "," + g.things[1].type, std::string("location,character"), "their types");
        }
        t.Eq(g.links.size(), size_t(1), "one link");
        if (g.links.size() == 1) {
            t.Eq(g.links[0].from, int64_t(1), "from is the thing whose field holds the link: Ada");
            t.Eq(g.links[0].to, int64_t(0), "to");
            t.Eq(g.links[0].field, std::string("location"), "the field");
            t.Eq(g.links[0].label, std::string("is at"), "the words");
        }
        t.Ok(g.ToJson() == Parse(Glimpse), "the glimpse written back is the one sent: " + OWJson::Stringify(g.ToJson()));
    }
    OWJsonValue strand = with_one;
    strand.Find("glimpse")->Set("picked_by", OWJsonValue::String("strand"));
    OWReply<OWJoinPreview> s = PreviewOn(Answering(200, OWJson::Stringify(strand)));
    t.Ok(s.value.glimpse && s.value.glimpse->IsStrand() && !s.value.glimpse->IsLinks(), "the strand");
    OWReply<OWJoinPreview> none = PreviewOn(Answering(200, PersonPreview));
    t.Ok(none.Ok() && !none.value.glimpse, "null reads absent");
    // A place that is no whole number, or below 0, reads -1; a thing or a link that is no object is left out.
    OWJsonValue odd = with_one;
    odd.Set("glimpse", Parse("{\"picked_by\":\"links\",\"things\":[{\"name\":\"Gela\",\"type\":\"location\"},7],"
                             "\"links\":[{\"from\":1.5,\"to\":-1,\"field\":\"f\",\"label\":\"l\"},\"x\",{\"from\":1.0,"
                             "\"to\":\"0\",\"field\":\"f\",\"label\":\"l\"}]}"));
    OWReply<OWJoinPreview> o = PreviewOn(Answering(200, OWJson::Stringify(odd, true)));
    t.Ok(o.value.glimpse.has_value(), "read");
    if (o.value.glimpse) {
        t.Eq(o.value.glimpse->things.size(), size_t(1), "a thing that is no object left out");
        t.Eq(o.value.glimpse->links.size(), size_t(2), "a link that is no object left out");
        if (o.value.glimpse->links.size() == 2) {
            t.Eq(o.value.glimpse->links[0].from, int64_t(-1), "1.5 is no place");
            t.Eq(o.value.glimpse->links[0].to, int64_t(-1), "-1 is no place");
            t.Eq(o.value.glimpse->links[1].from, int64_t(1), "1.0 is the place 1");
            t.Eq(o.value.glimpse->links[1].to, int64_t(-1), "a string is no place");
        }
    }
}

OW_TEST(join_a_table_preview_reads_its_places_and_its_characters_in_order) {
    OWReply<OWJoinPreview> r = PreviewOn(Answering(200, TablePreview));
    t.Ok(r.Ok(), "answered");
    const OWJoinPreview& p = r.value;
    t.Ok(p.IsTable(), "a table");
    t.Eq(p.max_uses.value_or(0), int64_t(4), "max_uses, a whole number");
    t.Eq(p.uses_left.value_or(0), int64_t(3), "uses_left, a whole number");
    t.Ok(p.name_required.has_value() && !*p.name_required, "name_required, false");
    t.Ok(!p.owner_character, "owner_character");
    t.Ok(p.characters && p.characters->size() == 2, "two to pick from");
    if (p.characters && p.characters->size() == 2) {
        const auto& c = *p.characters;
        t.Eq(c[0].id + "," + c[1].id, std::string("c-ana,c-bo"), "in the owner's order");
        t.Eq(c[1].name, std::string("Bo"), "a name");
        t.Ok(!c[0].image_url, "no picture");
        t.Eq(c[1].image_url.value_or(""), std::string("https://media.onlyworlds.com/bo.webp"), "a picture");
        t.Ok(!c[0].taken && c[1].taken, "taken");
    }
    OWJsonValue fresh = Parse(TablePreview);
    fresh.Set("max_uses", OWJsonValue::Number(int64_t(6)));
    fresh.Set("uses_left", OWJsonValue::Number(int64_t(6)));
    fresh.Set("name_required", OWJsonValue::Bool(true));
    fresh.Set("characters", OWJsonValue::Array());
    OWReply<OWJoinPreview> q = PreviewOn(Answering(200, OWJson::Stringify(fresh)));
    t.Ok(q.value.IsTable() && q.value.name_required.value_or(false) && q.value.characters && q.value.characters->empty(),
         "a table that lists none");
    OWJsonValue one = Parse(TablePreview);
    one.Set("max_uses", OWJsonValue::Number(int64_t(1)));
    OWReply<OWJoinPreview> lone = PreviewOn(Answering(200, OWJson::Stringify(one)));
    t.Ok(!lone.value.IsTable(), "a link that seats one is no table");
}

// -- Refusals keel answers --------------------------------------------------------------------------------------------

OW_TEST(join_a_bad_code_is_keels_one_not_found) {
    const std::string body = "{\"error\":{\"type\":\"invalid_request\",\"code\":\"not_found\","
                             "\"message\":\"This join code is not valid. It may have been used, revoked, or expired.\"}}";
    OWReply<OWJoined> redeemed = RedeemOn(Answering(404, body));
    OWReply<OWJoinPreview> previewed = PreviewOn(Answering(404, body));
    for (const std::optional<OWApiError>* e : {&redeemed.error, &previewed.error}) {
        t.Ok(e->has_value() && (*e)->kind == OWApiError::Kind::Api, "keel's refusal");
        if (!*e) continue;
        t.Ok((*e)->IsNotFound(), "IsNotFound");
        t.Ok(!(*e)->IsAlreadyMember() && !(*e)->IsCharacterTaken(), "and nothing else");
    }
    t.Eq(redeemed.value.key, std::string(), "no answer");
    t.Ok(redeemed.response && redeemed.response->status == 404, "the answer kept");
}

OW_TEST(join_a_missing_agent_name_names_its_param) {
    OWReply<OWJoined> r = RedeemOn(Refusing(422, "invalid_request", "agent_name"));
    t.Ok(r.error && r.error->param == "agent_name", "the param");
    if (r.error) t.Ok(!r.error->IsCharacterInvalid(), "not a refused pick");
}

OW_TEST(join_a_taken_pick_is_character_taken_and_nothing_else) {
    OWRedeemOptions pick;
    pick.character = "c-bo";
    OWReply<OWJoined> r = RedeemOn(Refusing(422, "character_taken", "character"), pick);
    t.Ok(r.error.has_value(), "refused");
    if (r.error) {
        t.Ok(Flags(*r.error) == std::vector<bool>{true, false, false}, "taken");
        t.Ok(r.error->IsValidationError(), "a validation error");
    }
    OWReply<OWJoined> other = RedeemOn(Refusing(409, "character_taken", "character"));
    t.Ok(other.error && !other.error->IsCharacterTaken(), "the code on another status is not it");
}

OW_TEST(join_a_pick_off_the_list_is_character_invalid_but_a_missing_name_is_not) {
    OWRedeemOptions pick;
    pick.character = "c-zed";
    OWReply<OWJoined> off = RedeemOn(Refusing(422, "invalid_request", "character"), pick);
    t.Ok(off.error.has_value(), "refused");
    if (off.error) t.Ok(Flags(*off.error) == std::vector<bool>{false, true, false}, "off the list");
    // The TypeScript SDK's: any other param's 422, a 400, another code: never a refused pick.
    struct Case {
        int status;
        const char* code;
        std::optional<std::string> param;
    };
    for (const Case& c : std::vector<Case>{{422, "invalid_request", "name"},
                                           {422, "invalid_request", "characters"},
                                           {422, "invalid_request", "max_uses"},
                                           {422, "invalid_request", std::nullopt},
                                           {400, "invalid_request", "character"},
                                           {422, "invalid_link", "character"}}) {
        OWReply<OWJoined> e = RedeemOn(Refusing(c.status, c.code, c.param));
        t.Ok(e.error && !e.error->IsCharacterInvalid(), std::to_string(c.status) + " " + c.code + " on " +
                                                            c.param.value_or("null") + " is not a refused pick");
    }
    OWReply<OWJoined> no_name = RedeemOn(Refusing(422, "invalid_request", "name"));
    t.Eq(no_name.error ? no_name.error->param : std::string(), std::string("name"), "the missing name names its param");
}

OW_TEST(join_a_full_world_is_world_full_only_as_a_422) {
    OWReply<OWJoined> full = RedeemOn(Refusing(422, "world_full", std::nullopt));
    t.Ok(full.error.has_value(), "refused");
    if (full.error) t.Ok(Flags(*full.error) == std::vector<bool>{false, false, true}, "full");
    OWReply<OWJoined> other = RedeemOn(Refusing(409, "world_full", std::nullopt));
    t.Ok(other.error && !other.error->IsWorldFull(), "the code on another status is not it");
}

OW_TEST(join_a_dropped_connection_is_a_transport_error_and_an_answer_not_an_object_is_an_answer_error) {
    auto fake = std::make_shared<FakeTransport>();
    fake->Fail("can't connect");
    OWReply<OWJoined> r = RedeemOn(fake);
    t.Ok(r.error && r.error->kind == OWApiError::Kind::Transport &&
             r.error->message.find("POST /join") != std::string::npos &&
             r.error->message.find("can't connect") != std::string::npos,
         "no answer: " + (r.error ? r.error->ToString() : std::string("no error")));
    for (const char* answer : {"not json", "[]", "", "null", "\"ow_r_x\""}) {
        OWReply<OWJoinPreview> p = PreviewOn(Answering(200, answer));
        t.Ok(p.error && p.error->kind == OWApiError::Kind::Answer && p.error->status == 200 &&
                 p.error->raw_body == answer,
             std::string("a 200 of '") + answer + "' is not the answer: " +
                 (p.error ? p.error->ToString() : std::string("no error")));
    }
    OWReply<OWJoined> j = RedeemOn(Answering(201, "[]"));
    t.Ok(j.error && j.error->kind == OWApiError::Kind::Answer && j.error->message.find("POST /join") != std::string::npos,
         "redeem too: " + (j.error ? j.error->ToString() : std::string("no error")));
}

OW_TEST(join_the_seam_may_answer_later) {
    // Unreal's HTTP answers after Send returns: the handler is called then, once.
    auto fake = Answering(201, PlayerJoined);
    fake->deferred = true;
    Got<OWJoined> got;
    OWJoin::Redeem(Code, {}, Where(fake), got.H());
    t.Eq(got.calls, 0, "not yet");
    t.Eq(fake->Flush(), size_t(1), "one answer held");
    t.Eq(got.calls, 1, "answered once");
    t.Eq(got.reply.value.key, std::string("ow_r_guest"), "the answer");
    auto twice = Answering(200, PreviewBody);
    twice->answer_twice = true;
    Got<OWJoinPreview> once;
    OWJoin::Preview(Code, Where(twice), once.H());
    t.Eq(once.calls, 1, "a second answer is dropped");
}

// -- Refusals before sending ------------------------------------------------------------------------------------------

OW_TEST(join_no_code_or_no_transport_is_the_callers_error_nothing_sent) {
    auto fake = std::make_shared<FakeTransport>();
    {
        Got<OWJoined> got;
        OWJoin::Redeem("", {}, Where(fake), got.H());
        Refused(t, got.reply, got.calls, *fake, "OWJoin::Redeem: a join code is required", "redeem, no code");
    }
    {
        Got<OWJoinPreview> got;
        OWJoin::Preview("", Where(fake), got.H());
        Refused(t, got.reply, got.calls, *fake, "OWJoin::Preview: a join code is required", "preview, no code");
    }
    OWJoinConfig none;
    none.base_url = Base;
    {
        Got<OWJoined> got;
        OWJoin::Redeem(Code, {}, none, got.H());
        Refused(t, got.reply, got.calls, *fake, "OWJoin::Redeem: a transport is required", "redeem, no transport");
    }
    {
        Got<OWJoinPreview> got;
        OWJoin::Preview(Code, none, got.H());
        Refused(t, got.reply, got.calls, *fake, "OWJoin::Preview: a transport is required", "preview, no transport");
    }
}

// -- AcceptJoin: a person's link, on an account token -----------------------------------------------------------------

namespace {

OWClient Account(const std::shared_ptr<FakeTransport>& fake, const std::string& key = "ow_a_test",
                 std::optional<std::string> world = std::nullopt) {
    OWClientConfig config;
    config.api_key = key;
    config.api_pin = "pin-for-tests";
    config.world = std::move(world);
    config.base_url = Base;
    config.transport = fake;
    config.agent = true;
    config.client = "atlas";
    return OWClient(std::move(config));
}

OWReply<OWJoinedMembership> AcceptOn(const std::shared_ptr<FakeTransport>& fake, const std::string& code = "ow_j_abc") {
    Got<OWJoinedMembership> got;
    Account(fake).AcceptJoin(code, got.H());
    return got.reply;
}

}  // namespace

// The TypeScript SDK's acceptJoin() (4.12.0): POST /account/join with {code} alone, the Bearer, no PIN (accepting needs
// none) and no write header. Kills: the wrong verb or path, the code under another key or beside others, the PIN or a
// write header sent, or the membership misread.
OW_TEST(join_accept_join_posts_account_join_the_code_alone_the_bearer_and_answers_the_membership) {
    auto fake = Answering(201, Membership);
    Got<OWJoinedMembership> got;
    Account(fake).AcceptJoin("ow_j_abc", got.H());
    t.Eq(got.calls, 1, "answered once");
    t.Eq(fake->calls.size(), size_t(1), "one request");
    const OWRequest& a = fake->Last();
    t.Eq(a.method, std::string("POST"), "the verb");
    t.Eq(a.url, Base + "/account/join/", "the path, with the trailing slash the client writes, no query");
    t.Eq(a.body.value_or(""), std::string("{\"code\":\"ow_j_abc\"}"), "the code alone");
    t.Ok(HeaderNames(a) == std::vector<std::string>{"Authorization", "Content-Type"},
         "the Bearer and the media type: no PIN, no write header");
    t.Eq(a.Header("Authorization") ? *a.Header("Authorization") : std::string(), std::string("Bearer ow_a_test"),
         "the account token as its Bearer");
    t.Ok(got.reply.Ok(), "answered (" + (got.reply.error ? got.reply.error->ToString() : std::string()) + ")");
    const OWJoinedMembership& m = got.reply.value;
    t.Eq(m.id + "|" + m.world_id + "|" + m.world_name + "|" + m.role, std::string("m7|w1|Ostvik|contributor"),
         "the membership");
    t.Eq(m.joined_at.value_or(""), std::string("2026-10-31T01:00:00+00:00"), "joined_at");
    t.Ok(!m.roster_character, "no roster Character");
    OWJsonValue with_character = Parse(Membership);
    with_character.Set("roster_character", OWJsonValue::String("c1"));
    OWReply<OWJoinedMembership> m2 = AcceptOn(Answering(201, OWJson::Stringify(with_character)));
    t.Eq(m2.value.roster_character.value_or(""), std::string("c1"), "the roster Character's id");
    // An account token with a world set sends no world= here: /account routes name no world.
    auto worlded = Answering(201, Membership);
    Got<OWJoinedMembership> w;
    Account(worlded, "ow_a_test", "w1").AcceptJoin("ow_j_abc", w.H());
    t.Eq(worlded->Last().url, Base + "/account/join/", "no world= on an account route");
}

// Kills: an empty code reaching the transport (keel's 404 would read as a bad code, not a missing one).
OW_TEST(join_accept_join_no_code_is_refused_nothing_sent) {
    auto fake = std::make_shared<FakeTransport>();
    Got<OWJoinedMembership> got;
    Account(fake).AcceptJoin("", got.H());
    Refused(t, got.reply, got.calls, *fake, "AcceptJoin: a join code is required", "no code");
}

// Kills: a world key, a read key, a legacy key or no key reaching /account/join, which keel answers 401 (a world key
// would carry its PIN there); the TypeScript SDK refuses a world key with a TypeError before sending.
OW_TEST(join_accept_join_any_key_but_an_account_token_is_refused_nothing_sent) {
    struct Case {
        const char* label;
        std::string key;
        std::optional<std::string> world;
        const char* holds;
    };
    for (const Case& c : std::vector<Case>{{"a write key", "ow_w_test", std::nullopt, "holds a write key"},
                                           {"a read key", "ow_r_test", std::nullopt, "holds a read key"},
                                           {"a legacy key", "0123456789", std::nullopt, "holds a legacy key"},
                                           {"a key of no known shape", "k-test", std::nullopt, "holds a key of no known shape"},
                                           {"no key, world set", "", std::string("w1"), "holds none"}}) {
        auto fake = std::make_shared<FakeTransport>();
        Got<OWJoinedMembership> got;
        OWClient client = Account(fake, c.key, c.world);
        t.Eq(client.Error(), std::string(), std::string(c.label) + ": the client is built");
        client.AcceptJoin("ow_j_abc", got.H());
        Refused(t, got.reply, got.calls, *fake, "AcceptJoin needs an account token (ow_a_)", c.label);
        if (got.reply.error) {
            t.Ok(got.reply.error->message.find(c.holds) != std::string::npos,
                 std::string(c.label) + ": says what it holds (" + got.reply.error->message + ")");
            t.Ok(got.reply.error->message.find("OWJoin::Redeem") != std::string::npos,
                 std::string(c.label) + ": names the keyless call");
        }
    }
    // A client not built answers its own refusal first.
    auto fake = std::make_shared<FakeTransport>();
    OWClientConfig bad;
    bad.api_key = "ow_a_test";
    bad.world = "  ";
    bad.transport = fake;
    OWClient unbuilt(std::move(bad));
    Got<OWJoinedMembership> got;
    unbuilt.AcceptJoin("ow_j_abc", got.H());
    Refused(t, got.reply, got.calls, *fake, "OWClient was not built", "a client not built");
}

// Kills: keel's refusals read as anything else (each leaves the code unspent). npm's two 409s, told apart by code
// without reading the message, and Unity's 404.
OW_TEST(join_accept_join_already_member_pin_required_and_not_found_are_told_apart) {
    OWReply<OWJoinedMembership> member = AcceptOn(Answering(
        409, "{\"error\":{\"type\":\"conflict_error\",\"code\":\"already_member\",\"message\":\"Already a member.\"}}"));
    t.Ok(member.error.has_value(), "refused");
    if (member.error)
        t.Ok(member.error->IsAlreadyMember() && !member.error->IsPinRequired() && !member.error->IsNotFound(), "a member");
    OWReply<OWJoinedMembership> pin = AcceptOn(Refusing(409, "pin_required", std::nullopt));
    t.Ok(pin.error && !pin.error->IsAlreadyMember() && pin.error->IsPinRequired(), "no PIN yet");
    OWReply<OWJoinedMembership> bad = AcceptOn(Answering(
        404, "{\"error\":{\"type\":\"not_found_error\",\"code\":\"not_found\",\"message\":\"Not a valid link.\"}}"));
    t.Ok(bad.error && bad.error->IsNotFound() && !bad.error->IsAlreadyMember(), "a bad code");
    OWReply<OWJoinedMembership> other = AcceptOn(Refusing(422, "already_member", std::nullopt));
    t.Ok(other.error && !other.error->IsAlreadyMember(), "the code on another status is not it");
    OWReply<OWJoinedMembership> other_pin = AcceptOn(Refusing(422, "pin_required", std::nullopt));
    t.Ok(other_pin.error && !other_pin.error->IsPinRequired(), "pin_required on another status is not it");
    OWReply<OWJoinedMembership> list = AcceptOn(Answering(201, "[]"));
    t.Ok(list.error && list.error->kind == OWApiError::Kind::Answer, "a 201 that is no object is no membership");
}
