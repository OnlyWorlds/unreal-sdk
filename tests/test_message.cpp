// OWMessage on the message profile's conformance fixture (schema-dist profiles/message.fixture.json, dist.95's copy):
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

#include <map>
#include <memory>
#include <set>
#include <string>

using namespace OnlyWorlds;
using owtest::P;

namespace {

// The fixture's sha256 as dist.95's MANIFEST.json records it.
const char* const DistSha256 = "c7dafdab913996f6c5ccdaf454a156abce413ba59a97d5d64a5abaddba96b88f";

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
    t.Eq(owtest::Sha256Hex(raw), std::string(DistSha256), "the fixture's bytes are dist.95's");
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
    t.Eq(cases->Size(), size_t(88), "88 cases");
    t.Eq(names.size(), size_t(88), "each name its own");
    const std::pair<const char*, size_t> want[] = {{"pair", 10},  {"addressed", 11}, {"sent_by", 3}, {"sender", 14},
                                                   {"unread", 17}, {"mark", 11},     {"thread", 4},  {"unseen", 3},
                                                   {"everyone", 3}, {"edited", 4},   {"note", 8}};
    size_t sum = 0;
    for (const auto& [kind, n] : want) {
        t.Eq(kinds[kind], n, std::string("kind ") + kind);
        sum += n;
    }
    t.Eq(sum, size_t(88), "the eleven kinds are every case");
    t.Eq(kinds.size(), size_t(11), "and no other kind");
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
    // recipientsOf: the non-empty string ids, as written (a repeat kept); none for a Note.
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
