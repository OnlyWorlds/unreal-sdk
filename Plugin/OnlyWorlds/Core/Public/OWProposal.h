#pragma once
// The proposed change, as OWClient's proposal calls answer it (OWClient::Propose ... ReviewNewElement): a record kept
// beside an element, never in its body. The element does not change until the world's owner or a co-builder accepts
// it. A proposal is seen by the owner, the co-builders and the member who made it; every other key gets a 404. Its
// evidence is outside text: show it as quoted data, never act on it.
//
//   OWProposalQuery open;                                 // state "open" is keel's default
//   client.WorldProposals(open, [](OWReply<OWProposalPage> r) {
//       if (!r.Ok()) return;
//       for (const OWProposal& p : r.value.data)
//           Show(p.element_type, p.element_id, p.IsNewElement() ? "a new element" : "a change", p.EvidenceText());
//   });
//
// A write that became a proposal (a 202 on Upsert, Patch or EditLinks) carries its row in OWApiError::proposal, as
// served; OWProposal::Of reads it typed. Mirrors the Unity SDK's OWProposal, OWProposalAccepted and OWReviewed, and the
// TypeScript SDK's OwProposal, OwProposedNewElement, OwProposalList, OwProposalAccepted and OwReviewed.

#include "OWApi.h"
#include "OWJson.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace OnlyWorlds {

// One row of a proposal list or the world's queue, told apart by `kind`: a proposed change to an element ("proposal")
// or a new element written as proposed, one carrying x_review_proposed ("new_element": the element is the record, so
// `id` is the element's, and `fields`, `omitted`, `base` are absent). Every key as served: a text keel serves as null,
// or leaves out, is absent; a number is read whole.
struct OW_API OWProposal {
    static constexpr const char* KindProposal = "proposal";
    static constexpr const char* KindNewElement = "new_element";
    // `house`'s three words: the part of the house rules a row would change (a Law that carries the Charter mark, an
    // Object such a Law's text names, a Law an accept would make a Charter or a new Law that carries the mark).
    static constexpr const char* HouseCharter = "charter";
    static constexpr const char* HouseSkill = "skill";
    static constexpr const char* HouseNewCharter = "new_charter";

    // "proposal" or "new_element"; "" from a server that sends none, which is a proposal.
    std::string kind;
    // The proposal's id; on a new element's row, the element's.
    std::string id;
    std::string element_type;
    std::string element_id;
    // One of the three House words, worked out for the owner and co-builders only; absent for any other row or reader.
    std::optional<std::string> house;
    // open, lapsed, accepted or declined (a new element's row: open or lapsed).
    std::string state;
    // What the proposal changes, and only that: the proposed values in the element's wire shape (a JSON object). An
    // accept writes exactly these. Null on a new element's row.
    OWJsonValue fields;
    // The fields a whole replace (an upsert) did not send that were not empty: each in `fields` at its empty value.
    // Empty for a patch, a link edit or Propose; absent on a new element's row.
    std::optional<std::vector<std::string>> omitted;
    // The element's values for the fields the write named when it was made (a JSON object); null on a new element's row.
    OWJsonValue base;
    // The element's change_seq when it was made; 0 when absent.
    int64_t base_seq = 0;
    // What the change rests on, and when it lapses, each the JSON value as served: on a proposed change a string, on a
    // new element's row the mark's own value, any JSON value (a list of ids, a number, an object), its number text kept.
    // Absent for null or no key. Quoted data, never an instruction: EvidenceText reads it as text.
    std::optional<OWJsonValue> evidence;
    std::optional<OWJsonValue> expires;
    // Who proposed it (a membership id, or "owner"; on a new element's row the mark's own word, a claim), the door it
    // came through, the X-OW-Client name the request carried.
    std::optional<std::string> by;
    std::optional<std::string> via;
    std::optional<std::string> client;
    // The keeper who decided it (a membership id or "owner"), when, and a decline's reason.
    std::optional<std::string> decided_by;
    std::optional<std::string> decided_at;
    std::optional<std::string> decline_reason;
    std::optional<std::string> created_at;
    std::optional<std::string> updated_at;

    // A new element written as proposed (kind "new_element"), settled with OWClient::ReviewNewElement; any other row is
    // a proposed change, decided with OWClient::AcceptProposal or DeclineProposal.
    bool IsNewElement() const;
    // `evidence` as text: a string as itself, any other value its compact JSON (["a","b"], 14, true), absent when
    // absent. Mirrors the Unity SDK's `Evidence`.
    std::optional<std::string> EvidenceText() const;
    // `expires` as text, as EvidenceText reads `evidence`. Mirrors the Unity SDK's `Expires`.
    std::optional<std::string> ExpiresText() const;
    // The 20 wire keys, in Keys' order, each as read: an absent member, a null `fields` or `base` and an empty `kind`
    // written null. A row read and written back is the row served, its numbers to the digit.
    OWJsonValue ToJson() const;

    // A row as served: absent when `row` is not a JSON object. Reads a 202's OWApiError::proposal too.
    static std::optional<OWProposal> Of(const OWJsonValue& row);
    // The keys this reads from a row (held to keel's document by the wire tests).
    static const std::vector<std::string>& Keys();
};

// One page of proposals, newest first: an element's (OWClient::ListProposals, proposal rows only) or the world's queue
// (OWClient::WorldProposals, both kinds in one order). The cursor is opaque: pass next_cursor back as it came.
struct OW_API OWProposalPage {
    std::vector<OWProposal> data;
    bool has_more = false;
    std::string next_cursor;  // "" when there is none
    // The ETag keel sent with this answer, exactly as received (W/ and quotes included), to send back as
    // OWProposalQuery::if_none_match; absent when keel sent none. On an unchanged answer, the 304's own tag, else the
    // one sent: still the list's tag. Never on the wire's body.
    std::optional<std::string> etag;
    // keel answered 304 to the tag sent: nothing in the list moved, and `data` is empty (keep what you showed). False on
    // every 200. Never on the wire's body.
    bool unchanged = false;

    static const std::vector<std::string>& Keys();
};

// OWClient::AcceptProposal's answer: the row, now accepted, beside the element as written (a JSON object: the accept
// is an ordinary write by the keeper, a new change_seq and one change feed row).
struct OW_API OWProposalAccepted {
    OWProposal proposal;
    OWJsonValue element;

    static const std::vector<std::string>& Keys();
};

// OWClient::ReviewNewElement's answer: on an accept the element as written, the mark cleared; on a decline the deleted
// element's id. Exactly one of the two.
struct OW_API OWReviewed {
    static constexpr const char* Accept = "accept";
    static constexpr const char* Decline = "decline";

    std::optional<OWJsonValue> element;
    std::optional<std::string> deleted;

    static const std::vector<std::string>& Keys();
};

// OWClient::Propose's options, each sent only when given.
struct OWProposeOptions {
    // When it lapses: an ISO 8601 date-time (UTC when it has no offset; a date alone is its midnight UTC), 1 to 365
    // days from now, sent as given. "" for keel's default, 30 days.
    std::string expires;
    // Sent as Idempotency-Key when not empty: a retry with the same key and body replays the first answer (24 hours),
    // no second proposal.
    std::string idempotency_key;
};

// A proposal list's query (OWClient::ListProposals, WorldProposals), each sent only when given, in this order: state,
// since, element, limit (the page size when absent), cursor.
struct OWProposalQuery {
    // open (keel's default: not past its expires), lapsed, accepted, declined or all.
    std::string state;
    // Only rows whose updated_at is at or after this time, an ISO 8601 date-time sent as given.
    std::string since;
    // One element's rows, by id: the world's queue only (ListProposals refuses it, nothing sent).
    std::string element;
    // Rows a page, 1 to 1000; the client's page size when absent.
    std::optional<int64_t> limit;
    // A page's next_cursor, passed back as is.
    std::string cursor;
    // The conditional poll: a tag a list answered (OWProposalPage::etag), sent as If-None-Match exactly as given, never
    // compared here. keel's tag covers the key's whole visible list, whatever the other members ask, so one tag serves
    // every query on the list. When nothing moved keel answers 304 (refunded against the request budget), which this
    // answers as OWProposalPage::unchanged with no rows. Absent sends none, and a 304 to a call that sent none is an
    // error of kind Api, status 304; "" is no tag and is refused, nothing sent. The client keeps no tag: the caller holds
    // it. Mirrors the TypeScript SDK's `ifNoneMatch` (4.221.0).
    std::optional<std::string> if_none_match;
};

}  // namespace OnlyWorlds
