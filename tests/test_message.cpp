// OWMessage on the message profile's conformance fixture (schema-dist profiles/message.fixture.json, dist.130's copy):
// every case by name, dispatched on its kind, each answer as the fixture gives it. Then tests/data/
// message-beside-the-fixture.json: 79 cases in the fixture's own shapes that it does not hold (the pair's trim and fold,
// addressing and sender edges, the read mark's ties and times, created_at readings, threads through a Note, the Note's
// proposed and hidden marks and its newest by created_at listed later), each answer as npm @onlyworlds/sdk 4.102.4
// gave it, replayed through the same dispatcher.

#include "ow_test.h"
#include "sha256.h"

#include "OWFiles.h"
#include "OWJson.h"
#include "OWMessage.h"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

using namespace OnlyWorlds;
using owtest::P;

namespace {

// The fixture's sha256 as dist.130's MANIFEST.json records it.
const char* const DistSha256 = "04a2ebf2c5efa66365a93b389e41f58c190aa0af250cbe87c86173872667a307";

std::string FixturePath() { return P().root + "/codegen/schema-dist/profiles/message.fixture.json"; }
std::string BesidePath() { return P().root + "/tests/data/message-beside-the-fixture.json"; }

std::shared_ptr<OWJsonValue> Load(const std::string& path) {
    OWBytes bytes;
    OWFiles::Read(path, bytes);
    OWJsonParseResult r = OWJson::Parse(bytes);
    return std::make_shared<OWJsonValue>(r.ok ? std::move(r.value) : OWJsonValue());
}

std::shared_ptr<OWJsonValue> Fixture() {
    static std::shared_ptr<OWJsonValue> doc = Load(FixturePath());
    return doc;
}

std::shared_ptr<OWJsonValue> Beside() {
    static std::shared_ptr<OWJsonValue> doc = Load(BesidePath());
    return doc;
}

std::string Str(const OWJsonValue* v) { return v && v->IsString() ? v->Text() : std::string(); }
std::string Str(const OWJsonValue& v, const char* key) { return Str(v.Find(key)); }

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

OWJsonValue OptStr(const std::optional<std::string>& s) { return s ? OWJsonValue::String(*s) : OWJsonValue::Null(); }
OWJsonValue OptBool(const std::optional<bool>& b) { return b ? OWJsonValue::Bool(*b) : OWJsonValue::Null(); }

// The ids of the bodies, as a JSON list (an id as written: a string, or whatever the body holds).
OWJsonValue Ids(const std::vector<const OWJsonValue*>& bodies) {
    OWJsonValue out = OWJsonValue::Array();
    for (const OWJsonValue* b : bodies) {
        const OWJsonValue* id = b->Find("id");
        out.Push(id ? *id : OWJsonValue::Null());
    }
    return out;
}

void Same(owtest::T& t, const OWJsonValue& got, const OWJsonValue* want, const std::string& what) {
    t.Ok(want && got == *want, what + ": gave " + Compact(got) + ", the case says " + (want ? Compact(*want) : "nothing"));
}

// One case, by its kind. The members list: the case's own when it has the key (null: no list), else the world's.
void Case(owtest::T& t, const OWJsonValue& c, const OWJsonValue& world) {
    std::string kind = Str(c, "kind"), name = kind + "/" + Str(c, "name");
    const OWJsonValue* members = c.Has("members") ? c.Find("members") : world.Find("members");
    if (members && members->IsNull()) members = nullptr;
    static const OWJsonValue none;
    auto at = [&](const char* key) -> const OWJsonValue& {
        const OWJsonValue* v = c.Find(key);
        if (!v) t.Fail(name + ": no " + key);
        return v ? *v : none;
    };
    if (kind == "pair") {
        Same(t, OWJsonValue::Bool(OWMessage::IsMessage(at("element"))), c.Find("is_message"), name);
    } else if (kind == "addressed" || kind == "unseen") {
        const OWJsonValue& m = at("message");
        std::string who = Str(c, "character");
        Same(t, OWJsonValue::Bool(OWMessage::IsAddressedTo(m, who)), c.Find("addressed"), name + " addressed");
        if (c.Has("unseen_recipients"))
            Same(t, OptBool(OWMessage::HasUnseenRecipients(m)), c.Find("unseen_recipients"), name + " unseen");
    } else if (kind == "everyone") {
        const OWJsonValue& m = at("message");
        const OWJsonValue* k = c.Find("keyless");
        bool keyless = k && k->IsBool() && k->AsBool();
        Same(t, OWJsonValue::Bool(OWMessage::IsToEveryone(m, keyless)), c.Find("to_everyone"), name + " to everyone");
        Same(t, OptBool(OWMessage::HasUnseenRecipients(m, keyless)), c.Find("unseen_recipients"), name + " unseen");
    } else if (kind == "recipients") {
        OWJsonValue got = OWJsonValue::Array();
        for (const std::string& id : OWMessage::RecipientsOf(at("message"))) got.Push(OWJsonValue::String(id));
        Same(t, got, c.Find("recipients"), name);
    } else if (kind == "sent_by") {
        Same(t, OWJsonValue::Bool(OWMessage::IsSentBy(at("message"), Str(c, "character"))), c.Find("sent_by"), name);
    } else if (kind == "sender") {
        OWSender s = OWMessage::SenderStatus(at("message"), members);
        Same(t, OWJsonValue::String(s.status), c.Find("status"), name + " status");
        Same(t, OptStr(s.claimed), c.Find("claimed"), name + " claimed");
        // written_by: the case's when it names one, else none.
        OWJsonValue want_by = c.Has("written_by") ? *c.Find("written_by") : OWJsonValue::Null();
        Same(t, OptStr(s.written_by), &want_by, name + " written_by");
    } else if (kind == "edited") {
        OWEdited e = OWMessage::EditedBy(at("message"), members);
        Same(t, OWJsonValue::Bool(e.edited), c.Find("edited"), name + " edited");
        Same(t, OptStr(e.by), c.Find("edited_by"), name + " edited_by");
    } else if (kind == "mark") {
        // A case without keyless has a credential, or is a folder.
        const OWJsonValue* k = c.Find("keyless");
        const OWJsonValue* mark = OWMessage::ReadMarkOf(OWMessage::List(at("relations")), Str(c, "reader"), members,
                                                        k && k->IsBool() && k->AsBool());
        const OWJsonValue* id = mark ? mark->Find("id") : nullptr;
        Same(t, id ? *id : OWJsonValue::Null(), c.Find("mark"), name);
    } else if (kind == "unread") {
        const OWJsonValue* mark = c.Find("read_mark");
        auto unread = OWMessage::UnreadFor(OWMessage::List(at("messages")), Str(c, "reader"),
                                           mark && !mark->IsNull() ? mark : nullptr, members);
        Same(t, Ids(unread), c.Find("unread"), name);
    } else if (kind == "thread") {
        Same(t, Ids(OWMessage::ThreadOf(OWMessage::List(at("messages")), Str(c, "root"))), c.Find("thread"), name);
    } else if (kind == "note") {
        std::optional<std::string> line = OWMessage::NoteOf(OWMessage::List(at("notes")), Str(c, "character"), members);
        Same(t, OptStr(line), c.Find("line"), name);
    } else {
        t.Fail(name + ": a case of unknown kind");
    }
}

owtest::Cases CasesOf(const std::shared_ptr<OWJsonValue>& doc, const std::string& prefix) {
    owtest::Cases out;
    const OWJsonValue* cases = doc->Find("cases");
    if (!cases) {
        out.emplace_back(prefix + "not-found", [](owtest::T& t) { t.Fail("the cases did not load"); });
        return out;
    }
    for (const OWJsonValue& c : cases->Items())
        out.emplace_back(prefix + Str(c, "name"), [doc, &c](owtest::T& t) { Case(t, c, *Fixture()); });
    return out;
}

}  // namespace

OW_TEST(message_fixture_is_the_pinned_one) {
    OWBytes raw;
    t.Ok(OWFiles::Read(FixturePath(), raw), "the message fixture is vendored: " + FixturePath());
    t.Eq(owtest::Sha256Hex(raw), std::string(DistSha256), "the fixture's bytes are dist.130's");
    OWBytes manifest;
    OWFiles::Read(P().root + "/codegen/schema-dist/MANIFEST.json", manifest);
    OWJsonParseResult m = OWJson::Parse(manifest);
    const OWJsonValue* files = m.ok ? m.value.Find("files") : nullptr;
    const OWJsonValue* sha = files ? files->Find("profiles/message.fixture.json") : nullptr;
    t.Ok(sha && sha->IsString() && sha->Text() == DistSha256, "MANIFEST.json records the same sha256");
    auto doc = Fixture();
    t.Eq(Str(*doc, "profile"), std::string("message"), "the message profile's fixture");
    const OWJsonValue* cases = doc->Find("cases");
    t.Ok(cases && cases->IsArray(), "it parses, with its cases");
    if (!cases) return;
    std::map<std::string, size_t> kinds;
    std::set<std::string> names;
    for (const OWJsonValue& c : cases->Items()) {
        ++kinds[Str(c, "kind")];
        names.insert(Str(c, "name"));
    }
    t.Eq(cases->Size(), size_t(103), "103 cases");
    t.Eq(names.size(), size_t(103), "each name its own");
    const std::pair<const char*, size_t> want[] = {{"pair", 10},  {"addressed", 11}, {"sent_by", 3}, {"sender", 14},
                                                   {"unread", 22}, {"mark", 18},     {"thread", 4},  {"unseen", 3},
                                                   {"everyone", 4}, {"edited", 4},   {"note", 8},    {"recipients", 2}};
    size_t sum = 0;
    for (const auto& [kind, n] : want) {
        t.Eq(kinds[kind], n, std::string("kind ") + kind);
        sum += n;
    }
    t.Eq(sum, size_t(103), "the twelve kinds are every case");
    t.Eq(kinds.size(), size_t(12), "and no other kind");
}

OW_TEST(message_pairs_and_mark_are_the_fixtures) {
    auto doc = Fixture();
    const OWJsonValue* pair = doc->Find("pair");
    const OWJsonValue* note = doc->Find("note_pair");
    const OWJsonValue* mark = doc->Find("read_mark");
    t.Ok(pair && note && mark, "the fixture names the pair, the Note's pair and the read mark");
    if (!pair || !note || !mark) return;
    t.Eq(Str(*pair, "type"), std::string(OWMessage::Type), "the pair's type");
    t.Eq(Str(*pair, "supertype"), std::string(OWMessage::Supertype), "the pair's supertype");
    t.Eq(Str(*note, "type"), std::string(OWMessage::Type), "the Note's type");
    t.Eq(Str(*note, "supertype"), std::string(OWMessage::NoteSupertype), "the Note's supertype");
    t.Eq(Str(*mark, "type"), std::string(OWMessage::ReadMarkType), "the mark's type");
    t.Eq(Str(*mark, "supertype"), std::string(OWMessage::ReadMarkSupertype), "the mark's supertype");
    t.Eq(Str(*mark, "name"), std::string(OWMessage::ReadMarkName), "the mark's name");
    t.Eq(Str(*mark, "reader_field"), std::string(OWMessage::ReaderField), "the reader's field");
    t.Eq(Str(*mark, "messages_field"), std::string(OWMessage::MessagesField), "the messages' field");
}

OW_CASES(message_fixture_cases) { return CasesOf(Fixture(), "message_fixture:"); }

// -- the readings the fixture leaves open, as npm answers them -------------------------------------------------------

OW_TEST(message_beside_the_fixture_is_npms) {
    auto doc = Beside();
    const OWJsonValue* cases = doc->Find("cases");
    t.Ok(cases && cases->IsArray(), "tests/data/message-beside-the-fixture.json loads");
    if (!cases) return;
    t.Eq(cases->Size(), size_t(79), "79 cases");
    std::set<std::string> names, fixture_names;
    for (const OWJsonValue& c : cases->Items()) names.insert(Str(c, "name"));
    if (const OWJsonValue* f = Fixture()->Find("cases"))
        for (const OWJsonValue& c : f->Items()) fixture_names.insert(Str(c, "name"));
    t.Eq(names.size(), size_t(79), "each name its own");
    for (const std::string& n : names) t.Ok(!fixture_names.count(n), "not a fixture case's name: " + n);
    // recipientsOf: the non-empty string ids, as written, a repeat once in its first place (serial 128); none for a Note.
    const OWJsonValue* r = doc->Find("recipients");
    if (!r) return t.Fail("no recipients");
    OWJsonValue got = OWJsonValue::Array();
    for (const std::string& id : OWMessage::RecipientsOf(*r->Find("message"))) got.Push(OWJsonValue::String(id));
    Same(t, got, r->Find("of"), "recipientsOf");
    OWJsonValue note = OWJsonValue::Array();
    for (const std::string& id : OWMessage::RecipientsOf(*r->Find("note"))) note.Push(OWJsonValue::String(id));
    Same(t, note, r->Find("of_note"), "recipientsOf a Note");
}

OW_CASES(message_beside_the_fixture_cases) { return CasesOf(Beside(), "message_beside:"); }

OW_TEST(message_fold_is_the_room_profiles) {
    // Trimmed by Unicode White_Space (U+0085 goes, a BOM stays), folded by simple case folding with no normalization.
    t.Eq(std::string(OWMessage::Trim("\xC2\x85\xE3\x80\x80Message\t")), std::string("Message"), "U+0085, U+3000, a tab");
    t.Eq(std::string(OWMessage::Trim("\xEF\xBB\xBFMessage")), std::string("\xEF\xBB\xBFMessage"), "a BOM stays");
    t.Eq(std::string(OWMessage::Trim("\x1CMessage")), std::string("\x1CMessage"), "U+001C stays (not White_Space)");
    t.Eq(OWMessage::Fold(" MESSAGE "), std::string("message"), "ASCII");
    t.Eq(OWMessage::Fold("Me\xC5\xBF\xC5\xBF" "age"), std::string("message"), "U+017F folds to s");
    t.Eq(OWMessage::Fold("\xC3\x85"), std::string("\xC3\xA5"), "U+00C5 folds to U+00E5");
    // No NFC first (npm's room.ts fold is the trim and simpleCaseFold): A + U+030A stays two code points.
    t.Eq(OWMessage::Fold("A\xCC\x8A"), std::string("a\xCC\x8A"), "a decomposed letter is not composed");
}

OW_TEST(message_time_key_reads_rfc_3339) {
    auto key = [](const char* s) {
        OWJsonValue v = OWJsonValue::String(s);
        return OWMessage::TimeKey(&v);
    };
    auto k = key("1970-01-01T00:00:00Z");
    t.Ok(k && k->seconds == 0 && k->micros == 0, "the epoch");
    k = key("2026-10-27T10:00:00+05:30");
    auto z = key("2026-10-27T04:30:00Z");
    t.Ok(k && z && *k == *z, "an offset is read");
    k = key("2026-10-27T04:30:00.1234567Z");
    t.Ok(k && z && k->seconds == z->seconds && k->micros == 123456, "a fraction past six digits is cut, not rounded");
    t.Ok(!key("2026-02-29T00:00:00Z") && key("2024-02-29T00:00:00Z"), "the date must exist");
    t.Ok(!key("2026-10-27T04:30:00+24:00"), "an offset hour past 23");
    k = key("0000-03-01T00:00:00Z");
    t.Ok(k && k->seconds == -62162035200LL, "year 0 is a leap year, proleptic Gregorian");
    OWJsonValue n = OWJsonValue::Number(int64_t(5));
    t.Ok(!OWMessage::TimeKey(&n) && !OWMessage::TimeKey(nullptr), "a number, and nothing, cannot be read");
}

namespace {

// A message to a1 from a3 (no created_by, so the narrator is trusted and a1 reads it as unread), its created_at the
// JSON text given (nullptr: no created_at key).
OWJsonValue TimedMessage(const std::string& id, const char* created_at_json) {
    std::string body = R"({"id": ")" + id + R"(", "type": "narrative", "supertype": "Message", "name": ")" + id +
                       R"(", "story": "", "narrator": "a3", "characters": ["a1"])";
    if (created_at_json) body += std::string(R"(, "created_at": )") + created_at_json;
    OWJsonParseResult r = OWJson::Parse(body + "}");
    return r.ok ? std::move(r.value) : OWJsonValue();
}

std::vector<std::string> UnreadIds(const std::vector<OWJsonValue>& messages) {
    std::vector<const OWJsonValue*> list;
    for (const OWJsonValue& m : messages) list.push_back(&m);
    std::vector<std::string> out;
    for (const OWJsonValue* m : OWMessage::UnreadFor(list, "a1", nullptr, nullptr)) out.push_back(Str(*m, "id"));
    return out;
}

std::string Joined(const std::vector<std::string>& ids) {
    std::string s;
    for (const std::string& id : ids) s += (s.empty() ? "" : " ") + id;
    return s;
}

std::optional<OWMessage::OWTimeKey> KeyOf(const char* text) {
    OWJsonValue v = OWJsonValue::String(text);
    return OWMessage::TimeKey(&v);
}

}  // namespace

// The TypeScript SDK 4.159.0's edge case `created-at-order-and-the-readable-rule` (its test/fixtures/message-edges.json,
// the bytes the Unity SDK vendors), its 28 created_at texts and wanted order as the file gives them, through UnreadFor's
// created_at order. A time with no offset is UTC; a fraction counts to its sixth digit, later digits dropped, not
// rounded; a date alone, a space for the T and the other unreadable texts sort last, in the list's order. Since dist.121
// the fixture holds five unread cases on these readings (`created-at-with-no-offset-is-read-as-utc` and the four after
// it); this test and the two below stay to pin TimeKey beside them.
OW_TEST(message_created_at_order_and_the_readable_rule_as_the_typescript_sdk) {
    const std::vector<std::pair<const char*, const char*>> rows = {
        {"t01", "\"2026-10-26T10:00:00Z\""},
        {"t02", "\"2026-10-26T12:00:00+02:00\""},
        {"t03", "\"2026-10-26t09:00:00z\""},
        {"t04", "\"2026-10-26T09:30:00\""},
        {"t05", "\"2026-10-26T09:30:00.5Z\""},
        {"t06", "\"2026-10-26T09:30:00.000001Z\""},
        {"t07", "\"2026-02-29T00:00:00Z\""},
        {"t08", "\"2024-02-29T00:00:00Z\""},
        {"t09", "\"2026-10-26\""},
        {"t10", "\"2026-10-26 10:00:00Z\""},
        {"t11", "\" 2026-10-26T10:00:00Z\""},
        {"t12", "\"2026-10-26T24:00:00Z\""},
        {"t13", "\"2026-10-26T23:59:60Z\""},
        {"t14", "\"2026-10-26T10:00:00+24:00\""},
        {"t15", "\"2026-10-26T10:00Z\""},
        {"t16", "\"2026-10-26T10:00:00,5Z\""},
        {"t17", "null"},
        {"t18", nullptr},
        {"t19", "5"},
        {"t20", "\"yesterday\""},
        {"t21", "\"0000-01-01T00:00:00+01:00\""},
        {"t22", "\"9999-12-31T23:59:59.9999999Z\""},
        {"t23", "\"2026-10-26T09:30:00.0000005Z\""},
        {"t24", "\"2026-10-26T10:00:00-00:30\""},
        {"t25", "\"2026-10-26T09:61:00Z\""},
        {"t26", "\"2026-1-26T10:00:00Z\""},
        {"t27", "\"\\uff12\\uff10\\uff12\\uff16-10-26T10:00:00Z\""},
        {"t28", "\"2026-10-26T10:00:00Z\\n\""},
    };
    const std::vector<std::string> want = {"t21", "t08", "t03", "t04", "t23", "t06", "t05", "t01", "t02", "t24",
                                           "t22", "t07", "t09", "t10", "t11", "t12", "t13", "t14", "t15", "t16",
                                           "t17", "t18", "t19", "t20", "t25", "t26", "t27", "t28"};
    std::vector<OWJsonValue> messages;
    for (const auto& [id, created_at] : rows) messages.push_back(TimedMessage(id, created_at));
    t.Eq(messages.size(), size_t(28), "the case's 28 messages");
    t.Eq(Joined(UnreadIds(messages)), Joined(want), "the order the file wants");

    // On the instant reader's value: no offset is UTC, after 09:00Z; 0.0000005 is the same instant as no fraction (the
    // seventh digit dropped); 0.000001 is one microsecond later; a date alone is no time.
    auto nine = KeyOf("2026-10-26T09:00:00Z"), bare = KeyOf("2026-10-26T09:30:00"),
         zulu = KeyOf("2026-10-26T09:30:00Z"), seventh = KeyOf("2026-10-26T09:30:00.0000005Z"),
         sixth = KeyOf("2026-10-26T09:30:00.000001Z");
    t.Ok(nine && bare && *nine < *bare && bare->seconds - nine->seconds == 1800, "no offset reads as UTC, 30 min after 09:00Z");
    t.Ok(bare && zulu && *bare == *zulu, "no offset is the same instant as Z");
    t.Ok(seventh && bare && *seventh == *bare && seventh->micros == 0, "the seventh digit is dropped, not rounded");
    t.Ok(sixth && bare && sixth->seconds == bare->seconds && sixth->micros == 1, "the sixth digit counts");
    t.Ok(!KeyOf("2026-10-26"), "a date alone cannot be read");
}

// The year's two ends: any four digits are a year, the calendar runs on in both directions, and an offset may carry a
// time before year 0000 or past the end of 9999. The seconds since 1970 are the TypeScript SDK 4.159.0's and
// onlyworlds 0.120.0's for the same texts; every step of the arithmetic is int64_t (Digits, DaysFromCivil, the key).
OW_TEST(message_created_at_at_the_years_two_ends) {
    const std::vector<std::tuple<const char*, const char*, int64_t>> times = {
        {"E1", "9999-12-31T23:00:00-02:00", 253402304400LL},
        {"E2", "0000-01-01T00:00:00Z", -62167219200LL},
        {"R", "2026-11-12T10:00:00Z", 1794477600LL},
        {"E3", "0000-01-01T00:00:00+01:00", -62167222800LL},
        // The leap rule on the year number as written: 0000 is a leap year, so its 29 February reads (one day before
        // 0000-03-01, -62162035200).
        {"L", "0000-02-29T00:00:00Z", -62162121600LL},
    };
    std::vector<OWJsonValue> given;
    for (const auto& [id, text, seconds] : times) {
        auto k = KeyOf(text);
        t.Ok(k.has_value(), std::string(id) + " " + text + " reads");
        if (k) t.Eq(k->seconds, seconds, std::string(id) + " " + text + ": seconds since 1970");
        if (k) t.Eq(k->micros, int64_t(0), std::string(id) + ": no fraction");
        given.push_back(TimedMessage(id, (std::string("\"") + text + "\"").c_str()));
    }
    t.Eq(Joined(UnreadIds(given)), std::string("E3 E2 L R E1"), "as given");
    std::reverse(given.begin(), given.end());
    t.Eq(Joined(UnreadIds(given)), std::string("E3 E2 L R E1"), "reversed");
}

// The forms of a created_at, a row each: what reads and what does not.
OW_TEST(message_created_at_forms_read_or_not) {
    const std::vector<std::pair<const char*, bool>> rows = {
        {"2026-11-12t10:00:00z", true},           // lower-case t and z
        {"2026-11-12T10:00:00.1234567891Z", true},  // a ten-digit fraction
        {"2026-11-12 10:00:00Z", false},          // a space for the T
        {"2026-11-12T10:00Z", false},             // no seconds
        {"2026-11-12T10:00:00+0200", false},      // an offset without its colon
        {"2026-02-30T10:00:00Z", false},          // 30 February
        {"2026-11-12T24:00:00Z", false},          // hour 24
        {"2026-11-12T10:00:60Z", false},          // second 60
        {"2026-11-12T10:00:00+24:00", false},     // an offset of 24 hours
        {"2026-11-12T10:00:00Z\n", false},        // one line break after the time
    };
    for (const auto& [text, reads] : rows) {
        std::string shown = text;
        if (!shown.empty() && shown.back() == '\n') shown = shown.substr(0, shown.size() - 1) + "\\n";
        t.Eq(KeyOf(text).has_value(), reads, shown + (reads ? " reads" : " does not read"));
    }
    auto ten = KeyOf("2026-11-12T10:00:00.1234567891Z");
    t.Ok(ten && ten->micros == 123456, "the ten-digit fraction counts to its sixth digit");
    auto lower = KeyOf("2026-11-12t10:00:00z"), upper = KeyOf("2026-11-12T10:00:00Z");
    t.Ok(lower && upper && *lower == *upper, "t and z read as T and Z");
}

OW_TEST(message_empty_id_and_wrong_kinds_read_as_no) {
    OWJsonValue list = OWJsonValue::Array();
    t.Ok(!OWMessage::IsMessage(list) && !OWMessage::IsAddressedTo(list, "a") && !OWMessage::IsToEveryone(list),
         "a list is no body");
    t.Ok(OWMessage::HasUnseenRecipients(list) == std::optional<bool>(false), "no unseen recipients for a list");
    t.Eq(OWMessage::SenderStatus(list, nullptr).status, std::string(OWMessage::Unverifiable), "a list's sender");
    t.Ok(OWMessage::ReadMarkOf({}, "x", nullptr) == nullptr, "no relations, no mark");
    t.Ok(OWMessage::ThreadOf({}, "").empty() && OWMessage::UnreadFor({}, "", nullptr, nullptr).empty(), "nothing");
    t.Ok(!OWMessage::NoteOf({}, "x", nullptr), "no Note");
    t.Ok(OWMessage::List(OWJsonValue::String("x")).empty(), "List of a string is empty");
}

// Serial 128: each recipient once, in its first place, ids compared as written; only a message has any.
OW_TEST(message_recipients_each_once_and_only_a_message_has_any) {
    auto of = [](const std::string& json) {
        OWJsonParseResult r = OWJson::Parse(json);
        return r.ok ? OWMessage::RecipientsOf(r.value) : std::vector<std::string>{"(unparsed)"};
    };
    using V = std::vector<std::string>;
    t.Ok(of(R"({"type": "narrative", "supertype": "Message", "characters": ["a2", "a1", "a2", "a1"]})") == V{"a2", "a1"},
         "each once, in its first place");
    t.Ok(of(R"({"type": "narrative", "supertype": "Message", "characters": ["a1", "A1", "a1"]})") == V{"a1", "A1"},
         "another spelling is another entry");
    t.Ok(of(R"({"type": " Narrative ", "supertype": " MESSAGE ", "characters": ["a1"]})") == V{"a1"},
         "the pair trimmed and folded");
    t.Ok(of(R"({"characters": ["a1"]})").empty(), "no pair: not a message");
    t.Ok(of(R"({"type": "narrative", "supertype": "Story", "characters": ["a1"]})").empty(), "a Story has none");
    t.Ok(of(R"({"type": "narrative", "characters": ["a1"]})").empty(), "no supertype: not a message");
}

// Serial 128: the message page's own reader gates all four addressing readers on the pair. A Story beside a message,
// each with the same characters: the Story is addressed to nobody, never to everyone, and has no unseen recipients,
// keyless or not.
OW_TEST(message_addressing_readers_answer_only_for_a_message) {
    auto body = [](const std::string& json) {
        OWJsonParseResult r = OWJson::Parse(json);
        return r.ok ? std::move(r.value) : OWJsonValue();
    };
    for (const char* characters : {R"(["a1"])", "[]", "null"}) {
        const std::string c = characters;
        OWJsonValue message = body(R"({"type": "narrative", "supertype": "Message", "characters": )" + c + "}");
        OWJsonValue story = body(R"({"type": "narrative", "supertype": "Story", "characters": )" + c + "}");
        t.Ok(OWMessage::IsAddressedTo(message, "a1"), "the message is addressed: " + c);
        t.Ok(!OWMessage::IsAddressedTo(story, "a1"), "a Story is addressed to nobody: " + c);
        t.Ok(!OWMessage::IsToEveryone(story), "a Story is never to everyone: " + c);
        t.Ok(OWMessage::RecipientsOf(story).empty(), "a Story has no recipients: " + c);
    }
    t.Ok(OWMessage::IsToEveryone(body(R"({"type": "narrative", "supertype": "Message", "characters": []})")),
         "a message to everyone");
    for (const char* bare : {"{}", R"({"characters": []})", R"({"type": "narrative", "characters": []})"}) {
        t.Ok(!OWMessage::IsToEveryone(body(bare)), std::string("not a message, not to everyone: ") + bare);
        t.Ok(!OWMessage::IsAddressedTo(body(bare), "a1"), std::string("not a message, addressed to nobody: ") + bare);
    }
    OWJsonValue trimmed =
        body(R"({"type": "narrative", "supertype": "Message", "characters": [], "x_served_trimmed": ["characters"]})");
    OWJsonValue trimmed_story =
        body(R"({"type": "narrative", "supertype": "Story", "characters": [], "x_served_trimmed": ["characters"]})");
    t.Ok(OWMessage::HasUnseenRecipients(trimmed) == std::optional<bool>(true), "the message: someone unseen");
    t.Ok(!OWMessage::HasUnseenRecipients(trimmed, true).has_value(), "the message read keyless: unknown");
    t.Ok(OWMessage::HasUnseenRecipients(trimmed_story) == std::optional<bool>(false),
         "a Story has no recipients to be unseen");
    t.Ok(OWMessage::HasUnseenRecipients(trimmed_story, true) == std::optional<bool>(false), "keyless or not");
}

namespace {

// One of Ada's read marks, a folder's (no created_by, so it counts), its updated_at the JSON text given (nullptr: no
// updated_at key).
OWJsonValue AdasMark(const std::string& id, const char* updated_at_json) {
    std::string body = R"({"id": ")" + id + R"(", "type": "relation", "supertype": "Read", "name": "Read: Ada",
        "actor": "a1", "narratives": [])";
    if (updated_at_json) body += std::string(R"(, "updated_at": )") + updated_at_json;
    OWJsonParseResult r = OWJson::Parse(body + "}");
    return r.ok ? std::move(r.value) : OWJsonValue();
}

// The id of Ada's read mark among the marks, in the order given; "" for none.
std::string MarkAmong(const std::vector<OWJsonValue>& marks) {
    std::vector<const OWJsonValue*> list;
    for (const OWJsonValue& m : marks) list.push_back(&m);
    const OWJsonValue* mark = OWMessage::ReadMarkOf(list, "a1", nullptr);
    return mark ? Str(*mark, "id") : std::string();
}

}  // namespace

// The read mark's tie: of two marks at the same instant, the higher id wins (by code point, the id as written), in
// either list order; an instant written two ways (an offset, a fraction of zeros, a lowercase t and z) is the same
// instant. A mark whose updated_at cannot be read never wins over one that can. Two that cannot be read: the higher
// id. Kills: a tie kept by list order, a tie by the lower id, an id compared folded, the text compared in
// place of the instant.
OW_TEST(message_read_mark_tie_goes_to_the_higher_id) {
    const char* const ten = "\"2026-10-27T10:00:00Z\"";
    for (const char* same : {"\"2026-10-27T11:00:00+01:00\"", "\"2026-10-27T10:00:00.000Z\"", "\"2026-10-27t10:00:00z\"",
                             "\"2026-10-27T10:00:00Z\""}) {
        std::string shown = std::string("10:00Z and ") + same;
        t.Eq(MarkAmong({AdasMark("m-0a", ten), AdasMark("m-0b", same)}), std::string("m-0b"), shown + ", lower id first");
        t.Eq(MarkAmong({AdasMark("m-0b", same), AdasMark("m-0a", ten)}), std::string("m-0b"), shown + ", higher id first");
    }
    // By code point, as written: "m-a" (0x61) is above "m-B" (0x42), though a folded compare puts B above a.
    t.Eq(MarkAmong({AdasMark("m-a", ten), AdasMark("m-B", ten)}), std::string("m-a"), "lowercase above uppercase");
    t.Eq(MarkAmong({AdasMark("m-B", ten), AdasMark("m-a", ten)}), std::string("m-a"), "the same, listed the other way");
    // A newer instant still beats a higher id, and a readable time beats one that cannot be read, in either order.
    t.Eq(MarkAmong({AdasMark("m-0a", "\"2026-10-27T10:00:00.000001Z\""), AdasMark("m-0b", ten)}), std::string("m-0a"),
         "a microsecond newer beats the higher id");
    for (const char* unread : {"\"garbage\"", "\"2026-10-27\"", "null"}) {
        std::string shown = std::string("beside ") + unread;
        t.Eq(MarkAmong({AdasMark("m-00", ten), AdasMark("m-ff", unread)}), std::string("m-00"), shown + ", listed later");
        t.Eq(MarkAmong({AdasMark("m-ff", unread), AdasMark("m-00", ten)}), std::string("m-00"), shown + ", listed first");
    }
    t.Eq(MarkAmong({AdasMark("m-00", ten), AdasMark("m-ff", nullptr)}), std::string("m-00"), "beside no updated_at");
    t.Eq(MarkAmong({AdasMark("m-00", nullptr), AdasMark("m-ff", "\"garbage\"")}), std::string("m-ff"),
         "two that cannot be read: the higher id");
}

// The message page's read mark, the sentence as ruled: a lone mark is the mark. Among several, the newest readable
// updated_at wins; one that cannot be read never beats one that can; among equal instants, or when none can be read,
// the higher id (code point, as written) is the mark. Each case in both list orders. Kills: a tie kept by list order,
// an unreadable mark winning on its id, a lone unreadable mark dropped.
OW_TEST(message_read_mark_the_ruled_sentence) {
    auto both = [&](const OWJsonValue& a, const OWJsonValue& b, const std::string& want, const std::string& what) {
        t.Eq(MarkAmong({a, b}), want, what + ", listed one way");
        t.Eq(MarkAmong({b, a}), want, what + ", listed the other way");
    };
    // Equal instants written two ways: the higher id.
    both(AdasMark("m-1", "\"2026-10-27T10:00:00Z\""), AdasMark("m-2", "\"2026-10-27T11:00:00+01:00\""), "m-2",
         "10:00:00Z and 11:00:00+01:00");
    // Readable beats unreadable, whatever the ids.
    both(AdasMark("m-1", "\"2026-10-27T10:00:00Z\""), AdasMark("m-2", "\"garbage\""), "m-1", "readable beside garbage");
    both(AdasMark("m-1", "\"2026-10-27T10:00:00Z\""), AdasMark("m-2", nullptr), "m-1", "readable beside none");
    // None can be read: the higher id.
    both(AdasMark("m-1", nullptr), AdasMark("m-2", "\"garbage\""), "m-2", "none readable, two");
    t.Eq(MarkAmong({AdasMark("m-2", "\"2026-10-27\""), AdasMark("m-3", "null"), AdasMark("m-1", nullptr)}),
         std::string("m-3"), "none readable, three");
    // A lone mark is the mark, readable or not.
    t.Eq(MarkAmong({AdasMark("m-1", "\"garbage\"")}), std::string("m-1"), "a lone unreadable mark");
    t.Eq(MarkAmong({AdasMark("m-1", nullptr)}), std::string("m-1"), "a lone mark with no updated_at");
    t.Eq(MarkAmong({AdasMark("m-1", "\"2026-10-27T10:00:00Z\"")}), std::string("m-1"), "a lone readable mark");
}
