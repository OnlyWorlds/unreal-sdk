// The front door's packet as a server serves it (schema-dist profiles/strand.md rule 2, serial 100, and
// profiles/charter.md rule 2): GET /me and an agent's join reply carry reading_aids, the Charter, the Guide, the
// Domains, then strand, the six slots of the strand as that key reads the world, six null slots when the world has none
// or its story is out of the key's view. The bodies are keel's READING_AIDS_SCHEMA (accounts/front_door.py) field for
// field, the slots as keel's tests/test_strand_front_door.py serves them.

#include "ow_test.h"

#include "OWCharter.h"
#include "OWJson.h"
#include "OWStrand.h"

#include <string>
#include <vector>

using namespace OnlyWorlds;

namespace {

OWJsonValue Parse(std::string_view text) {
    OWJsonParseResult r = OWJson::Parse(text);
    return r.ok ? std::move(r.value) : OWJsonValue();
}

const char* const Full = R"({"narrative": {"id": "019a7f00-0000-7000-8000-000000000301", "type": "narrative", "name": "The Night the Lighthouse Went Dark"},
    "event": {"id": "019a7f00-0000-7000-8000-000000000311", "type": "event", "name": "The lamp fails"},
    "location": {"id": "019a7f00-0000-7000-8000-000000000321", "type": "location", "name": "The Lighthouse"},
    "character": {"id": "019a7f00-0000-7000-8000-000000000331", "type": "character", "name": "Ilse the Keeper"},
    "now": {"id": "019a7f00-0000-7000-8000-000000000322", "type": "location", "name": "The Docks"},
    "same": false})";

const char* const NoStrand =
    R"({"narrative": null, "event": null, "location": null, "character": null, "now": null, "same": null})";

// A GET /me answer whose packet carries `strand` (nullptr: a packet from before serial 100, with no key).
OWJsonValue Me(const char* strand) {
    std::string aids = R"({"charter": {"id": "law-1", "type": "law", "name": "House rules", "rules": [{"rule": "no-orphans"}], "skill": ["obj-1"]},
        "guide": {"id": "con-g", "type": "construct", "name": "Start here"},
        "domains": [{"id": "con-d1", "type": "construct", "name": "The Harbour", "sentence": "Where the ships come in.",
                     "hubs": [{"type": "location", "id": "loc-1"}]}])";
    if (strand) aids += std::string(R"(, "strand": )") + strand;
    aids += "}";
    return Parse(R"({"world": {"id": "w1", "name": "W"}, "kind": "owner", "front_door": {"charter": "law-1"},
        "reading_aids": )" + aids + "}");
}

// An object's keys in order, joined by commas.
std::string Keys(const OWJsonValue& v) {
    std::string out;
    for (const std::string& k : v.Keys()) out += (out.empty() ? "" : ",") + k;
    return out;
}

}  // namespace

OW_TEST(front_door_reads_the_served_strand_every_slot) {
    OWJsonValue me = Me(Full);
    std::optional<OWReadingAids> aids = OWCharter::Served(me);
    t.Ok(aids.has_value(), "the packet is read");
    if (!aids) return;
    t.Ok(aids->strand.has_value(), "a served strand is read as the engine's strand");
    if (!aids->strand) return;
    const OWWorldStrand& s = *aids->strand;
    t.Eq(s.narrative ? s.narrative->id : std::string(), std::string("019a7f00-0000-7000-8000-000000000301"), "the story");
    t.Eq(s.narrative ? s.narrative->name : std::string(), std::string("The Night the Lighthouse Went Dark"), "its name");
    t.Eq(s.event ? s.event->id : std::string(), std::string("019a7f00-0000-7000-8000-000000000311"), "the happening");
    t.Eq(s.location ? s.location->id : std::string(), std::string("019a7f00-0000-7000-8000-000000000321"), "then");
    t.Eq(s.character ? s.character->name : std::string(), std::string("Ilse the Keeper"), "the hero");
    t.Eq(s.now ? s.now->id : std::string(), std::string("019a7f00-0000-7000-8000-000000000322"), "now");
    t.Ok(s.same.has_value() && *s.same == false, "same, as served");
    t.Ok(s.ToJson() == Parse(Full), "the six slots as served");
    t.Ok(aids->ToJson() == *me.Find("reading_aids"), "read back, the packet as served, the strand in it");
}

OW_TEST(front_door_six_null_slots_read_as_an_empty_strand_not_as_none) {
    std::optional<OWReadingAids> aids = OWCharter::Served(Me(NoStrand));
    t.Ok(aids && aids->strand.has_value(), "six null slots are a strand served empty, not an absent key");
    if (!aids || !aids->strand) return;
    const OWWorldStrand& s = *aids->strand;
    t.Ok(!s.narrative && !s.event && !s.location && !s.character && !s.now && !s.same, "six nulls");
    const OWJsonValue packet = aids->ToJson();
    const OWJsonValue* written = packet.Find("strand");
    t.Ok(written && *written == Parse(NoStrand), "six nulls written back");
}

OW_TEST(front_door_a_packet_without_the_key_has_no_strand) {
    // A keel from before serial 100 serves {charter, guide, domains}.
    std::optional<OWReadingAids> aids = OWCharter::Served(Me(nullptr));
    t.Ok(aids.has_value(), "the packet is read");
    if (!aids) return;
    t.Ok(!aids->strand.has_value(), "an absent key reads as absent");
    t.Eq(Keys(aids->ToJson()), std::string("charter,guide,domains"), "and writes none");
}

OW_TEST(front_door_the_strand_comes_fourth) {
    // The charter profile's rule 2: the Charter, the Guide, the Domains, then the strand.
    std::optional<OWReadingAids> aids = OWCharter::Served(Me(NoStrand));
    t.Eq(aids ? Keys(aids->ToJson()) : std::string(), std::string("charter,guide,domains,strand"), "the order");
}

OW_TEST(front_door_the_three_aids_are_read_as_served) {
    std::optional<OWReadingAids> aids = OWCharter::Served(Me(NoStrand));
    if (!aids) return t.Fail("no packet");
    t.Eq(aids->charter ? aids->charter->id : std::string(), std::string("law-1"), "the Charter");
    t.Ok(aids->charter && aids->charter->skill == std::vector<std::string>{"obj-1"}, "its skill");
    t.Eq(aids->guide ? aids->guide->name : std::string(), std::string("Start here"), "the Guide");
    t.Eq(aids->domains.size(), size_t(1), "one Domain");
    if (aids->domains.size() == 1) {
        t.Eq(aids->domains[0].sentence, std::string("Where the ships come in."), "its sentence");
        t.Eq(aids->domains[0].hubs.size(), size_t(1), "its hub");
    }
    // A Charter served without rules reads an empty list, as CharterOf answers it.
    std::optional<OWReadingAids> bare = OWCharter::Served(Parse(
        R"({"reading_aids": {"charter": {"id": "law-1", "name": "House rules", "rules": null, "skill": []}, "guide": null, "domains": []}})"));
    t.Ok(bare && bare->charter && bare->charter->rules == OWJsonValue::Array(), "rules null reads []");
}

OW_TEST(front_door_a_served_slot_is_read_as_served) {
    // Then and now one place: same as served. A slot served with no name reads "".
    std::optional<OWReadingAids> aids = OWCharter::Served(Me(R"({"narrative": {"id": "n-1", "name": "The Crossing"},
        "event": null, "location": {"id": "loc-1", "name": null}, "character": {"id": "c-1", "name": "The Judge"},
        "now": {"id": "loc-1", "name": null}, "same": true})"));
    if (!aids || !aids->strand) return t.Fail("no strand");
    const OWWorldStrand& s = *aids->strand;
    t.Ok(s.same.has_value() && *s.same == true, "same, as served");
    t.Ok(!s.event, "an empty slot");
    t.Ok(s.location && s.location->id == "loc-1" && s.location->name.empty(), "a nameless slot reads an empty name");
    t.Ok(s.now && s.now->id == "loc-1", "the place, now");
}

OW_TEST(front_door_an_agents_join_answer_carries_the_strand) {
    // keel's test: the hero hidden from the new seat, so the hero's two slots and same are null.
    const char* const partial = R"({"narrative": {"id": "n-1", "type": "narrative", "name": "The Gang"},
        "event": {"id": "e-1", "type": "event", "name": "The Crossing"},
        "location": {"id": "loc-1", "type": "location", "name": "The River"}, "character": null, "now": null, "same": null})";
    OWJsonValue joined = Parse(std::string(R"({"kind": "agent", "key": "ow_w_seat", "front_door": {"charter": null},
        "reading_aids": {"charter": null, "guide": null, "domains": [], "strand": )") + partial + "}}");
    std::optional<OWReadingAids> aids = OWCharter::Served(joined);
    if (!aids || !aids->strand) return t.Fail("no strand");
    t.Ok(!aids->charter, "no Charter");
    t.Ok(aids->strand->ToJson() == Parse(partial), "the strand as the seat reads it");
}

OW_TEST(front_door_entries_served_without_a_type_read_their_fixed_type) {
    // A keel from before serial 133 serves {id, name}: each aid's type is fixed by its pair, each slot's by its slot.
    std::optional<OWReadingAids> aids = OWCharter::Served(Parse(R"({"reading_aids": {
        "charter": {"id": "law-1", "name": "House rules", "rules": [], "skill": []},
        "guide": {"id": "con-g", "name": "Start here"},
        "domains": [{"id": "con-d1", "name": "The Harbour", "sentence": "", "hubs": []}],
        "strand": {"narrative": {"id": "n-1", "name": "N"}, "event": {"id": "e-1", "name": "E"},
            "location": {"id": "loc-1", "name": "L"}, "character": {"id": "c-1", "name": "C"},
            "now": {"id": "loc-2", "name": "M"}, "same": false}}})"));
    if (!aids || !aids->charter || !aids->guide || aids->domains.size() != 1 || !aids->strand) return t.Fail("no packet");
    t.Eq(aids->charter->type, std::string("law"), "the Charter");
    t.Eq(aids->guide->type, std::string("construct"), "the Guide");
    t.Eq(aids->domains[0].type, std::string("construct"), "a Domain");
    const OWWorldStrand& s = *aids->strand;
    t.Ok(s.narrative && s.narrative->type == "narrative" && s.event && s.event->type == "event" && s.location &&
             s.location->type == "location" && s.character && s.character->type == "character" && s.now &&
             s.now->type == "location",
         "each slot's kind, now a location");
    const OWJsonValue packet = aids->ToJson();
    t.Eq(Keys(*packet.Find("charter")), std::string("id,type,name,rules,skill"), "the page's order");
    t.Eq(Keys(packet.Find("domains")->Items()[0]), std::string("id,type,name,sentence,hubs"), "the page's order");
    t.Eq(Keys(*packet.Find("strand")->Find("now")), std::string("id,type,name"), "the page's order");
}

OW_TEST(front_door_a_served_entry_hands_over_its_kinds_keys_alone) {
    // The charter profile's shape: each entry is its kind's named keys, and the packet carries none of the text an AI
    // follows. A key the reader does not know (a host's `description`, its text in it) is dropped, on a hub as well.
    std::optional<OWReadingAids> aids = OWCharter::Served(Parse(R"({"reading_aids": {
        "charter": {"id": "law-1", "type": "law", "name": "House rules", "rules": [], "skill": ["obj-1"],
            "description": "Be kind to the keeper."},
        "guide": {"id": "con-g", "type": "construct", "name": "Start here", "description": "Walk the harbour first."},
        "domains": [{"id": "con-d1", "type": "construct", "name": "The Harbour", "sentence": "Where ships come in.",
            "hubs": [{"type": "location", "id": "loc-1", "name": "The Quay"}], "description": "Where ships come in."}]}})"));
    if (!aids || !aids->charter || !aids->guide || aids->domains.size() != 1) return t.Fail("no packet");
    const OWJsonValue packet = aids->ToJson();
    t.Eq(Keys(*packet.Find("charter")), std::string("id,type,name,rules,skill"), "the Charter's five");
    t.Eq(Keys(*packet.Find("guide")), std::string("id,type,name"), "the Guide's three");
    const OWJsonValue& domain = packet.Find("domains")->Items()[0];
    t.Eq(Keys(domain), std::string("id,type,name,sentence,hubs"), "a Domain's five");
    t.Eq(Keys(domain.Find("hubs")->Items()[0]), std::string("type,id"), "a hub's two");
    t.Ok(aids->charter->skill == std::vector<std::string>{"obj-1"}, "a known key as served");
}

OW_TEST(front_door_an_answer_without_the_packet_reads_none) {
    // A player's join answer, an older keel: no reading_aids at all.
    t.Ok(!OWCharter::Served(Parse(R"({"kind": "player", "key": "ow_r_guest"})")).has_value(), "no packet");
    t.Ok(!OWCharter::Served(OWJsonValue::Null()).has_value(), "nothing");
}
