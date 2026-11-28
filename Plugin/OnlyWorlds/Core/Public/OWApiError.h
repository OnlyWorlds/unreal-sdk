#pragma once
// Why an OWClient call has no value: keel's error envelope, typed, or the client's own word when no answer came or
// nothing was sent. The core throws nothing, so a call answers an OWReply whose `error` is one of these, or absent.
//
// `kind` says which of four it is, since each has another remedy: Api (keel answered and said no: branch on `code`,
// point a person at `doc_url`), Transport (no answer came: offline, a timeout; or a list walk that would never end),
// Refused (the client refused the call before sending anything: a caller's mistake, the message names it) and Answer
// (a 2xx whose body is not the answer the route documents). keel's envelope is {"error": {type, code, message, param,
// doc_url}}; a body without the wrapper is read as the envelope itself, and the upload host's {"error": "<code>"} as
// its code, as the Unity SDK's BuildError reads them.
//
// A write that became a proposal (202 with {proposal}: an agent seat's upsert, patch or link edit in a world that takes
// its writes as proposals) is kind Api with status 202 and `proposal` set, IsProposed true: the element did not change,
// and the write waits for a person. The Unity SDK's OWProposedError, an OWApiError of status 202.

#include "OWApi.h"
#include "OWJson.h"
#include "OWTransport.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace OnlyWorlds {

// What a 422 with `param` `fields` carries beside its message, when a read's `fields` list (a List or a Get) names
// fields its type does not have: keel refuses the whole list once, with every unknown name, so a caller corrects it in
// one try. Read from OWApiError::FieldsRefusal(). Mirrors the TypeScript SDK's `OwFieldsRefusal` (4.179.0).
struct OW_API OWFieldsRefusal {
    // Every name in the list that the type does not have, in the order sent.
    std::vector<std::string> unknown_fields;
    // For each unknown name another element type has, those types, in the order keel served the names. A name no type
    // has is not here.
    std::vector<std::pair<std::string, std::vector<std::string>>> types_with_field;
    // The names this type's `fields` takes. An extension field name (`x_`, `atlas_`, `shadow_`) passes too and is not
    // listed.
    std::vector<std::string> allowed_fields;

    // The types that have `name`, or nullptr when no type has it.
    const std::vector<std::string>* TypesWith(std::string_view name) const;
    // npm's shape: {unknown_fields, types_with_field, allowed_fields}, `types_with_field` an object of lists.
    OWJsonValue ToJson() const;
};

// One proposed field that moved since the proposal was made, on a 409 stale_proposal (OWApiError::Conflicts): its value
// then and now, each the JSON value as served. Mirrors the Unity SDK's `OWProposalConflict` and the TypeScript SDK's
// `OwProposalConflict`.
struct OWProposalConflict {
    std::string field;
    OWJsonValue base;
    OWJsonValue current;
};

struct OW_API OWApiError {
    enum class Kind : uint8_t { Api, Transport, Refused, Answer };

    Kind kind = Kind::Api;
    int status = 0;        // the HTTP status; 0 when no answer came or nothing was sent
    std::string type;      // the broad class (invalid_request); "" when the answer carried none
    std::string code;      // keel's machine-readable code (not_found); "" when the answer carried none
    std::string message;   // keel's message, the body's start when it was not the envelope, or the client's words
    std::string param;     // the field the error is about, or ""
    std::string doc_url;   // the documentation link for `code`, or ""
    std::string raw_body;  // the body as received, for what the envelope does not model
    // Seconds to wait before a retry, from Retry-After (whole seconds, or an HTTP date read against the clock); absent
    // when it was absent or unreadable. keel sends it on 503 server_busy, 429 rate_limited and quota_exceeded, and 409
    // idempotency_in_flight. The client never retries on its own.
    std::optional<int32_t> retry_after;
    // On a write that became a proposal (IsProposed): the proposal row keel answered, as served; absent otherwise.
    std::optional<OWJsonValue> proposal;

    // keel's answer as an error: the envelope read defensively (a gateway's HTML page is a message, never a parse
    // error), Retry-After read from the headers by any case against `now` (Unix seconds). Mirrors the TypeScript SDK's
    // `errorFromResponse`, read as the Unity SDK's BuildError reads it (the wrapper's keys before the top level's; a
    // body that is not a JSON object its first 200 characters as the message).
    static OWApiError FromResponse(const OWResponse& response, int64_t now);
    // The client's own error: Transport, Refused or Answer, with the status and body of the answer when there was one.
    // Mirrors the TypeScript SDK's `OwNetworkError` (no answer) and the TypeError it throws before sending.
    static OWApiError Of(Kind kind, std::string message, int status = 0, std::string raw_body = "");

    // Retry-After as seconds: whole seconds as given (past 32 bits, the largest), or an HTTP date (an IMF-fixdate,
    // "Wed, 21 Oct 2026 07:28:00 GMT") as the seconds from `now` (Unix seconds) to it, 0 when it has passed. Absent
    // for anything else: a fraction, a sign, a word. Mirrors the TypeScript SDK's `parseRetryAfter`.
    static std::optional<int32_t> ParseRetryAfter(std::string_view value, int64_t now);

    // The line it prints: "OnlyWorlds API 404 [not_found] (field: id): No such character."; the client's own errors
    // "OnlyWorlds refused: ...", "OnlyWorlds transport: ...", "OnlyWorlds answer: ...". Mirrors the TypeScript SDK's
    // `OwApiError.message`.
    std::string ToString() const;

    // A 422 or a 400: the request was read and refused as it stands. Mirrors the TypeScript SDK's `isValidationError`.
    bool IsValidationError() const;
    // The key or PIN is wrong, unknown or revoked (world_gone is reserved). By code: a 403 such as owner_only comes
    // with a valid key. Mirrors the TypeScript SDK's `isAuthError`.
    bool IsAuthError() const;
    // 404 not_found: no such element or route. A 404 without the code (a proxy's) is not this. Mirrors the TypeScript
    // SDK's `isNotFound`.
    bool IsNotFound() const;
    // A member key tried what only the world's owner may do (403 owner_only). Mirrors the TypeScript SDK's `isOwnerOnly`.
    bool IsOwnerOnly() const;
    // A list or get sent `view_as` with a key that is not the owner's (403 view_as_owner_only). Mirrors the TypeScript
    // SDK's `isViewAsOwnerOnly`.
    bool IsViewAsOwnerOnly() const;
    // The change feed's position is no longer valid for this key (409 resync_required): walk again from the start, and
    // replace the local copy as ResyncReason says. Mirrors the TypeScript SDK's `isResyncRequired`.
    bool IsResyncRequired() const;
    // With IsResyncRequired, why, from the envelope's `reason`: "role" (this key's role or membership moved: keep what
    // the copy holds that a walk from the start no longer names) or "view" (something left its view: drop it); absent
    // otherwise, and for any other word. Never empty a copy on your own. Mirrors the TypeScript SDK's `resyncReason`.
    std::optional<std::string> ResyncReason() const;
    // Which size limit a write passed, from the envelope's `limit`: a key of the API document's `info.x-ow-limits`,
    // where its number and unit are. Set on a 422 invalid_request only, and only for the limits keel names this way
    // (an element's size, its links, its extension fields); a full world, a file too large or a request body too large
    // keep their own codes. An open string: more values may come. Absent for any other error, when keel sends none, or
    // when it is not a string. A bulk slot's error reads it too. Mirrors the TypeScript SDK's `limit`.
    std::optional<std::string> Limit() const;
    // A write would make the element larger than an element may be (422, Limit `element_bytes`). Nothing is saved.
    // Mirrors the TypeScript SDK's `isElementTooLarge`.
    bool IsElementTooLarge() const;
    // A write would give the element more link ids, across all its link fields, than it may hold (422, Limit
    // `element_link_ids`). Nothing is saved. Mirrors the TypeScript SDK's `isElementTooManyLinks`.
    bool IsElementTooManyLinks() const;
    // A write would put an element's or the World's extension fields past their cap (422, Limit `extensions_bytes`).
    // Nothing is saved. Mirrors the TypeScript SDK's `isExtensionsTooLarge`.
    bool IsExtensionsTooLarge() const;
    // A write became a proposal (status 202, `proposal` set): the element did not change; `proposal` is the row the
    // owner or a co-builder accepts or declines. Not a failure to retry. Mirrors the TypeScript SDK's `isProposed`.
    bool IsProposed() const;
    // A conditional write's If-Match was stale (412 precondition_failed: a patch, a link edit or a delete): the element
    // changed since the change_seq sent, and nothing was written. Current() is the element as it is now; decide from
    // it, then send again with its change_seq. Mirrors the TypeScript SDK's `isPreconditionFailed`.
    bool IsPreconditionFailed() const;
    // The `current` object keel sends beside the envelope on a 412 (IsPreconditionFailed) or a 409 stale_proposal: the
    // element as keel holds it now. Absent on any other answer, when keel sent none, or when the body is not JSON.
    // Mirrors the TypeScript SDK's `OwPreconditionFailedError.current`.
    std::optional<OWJsonValue> Current() const;
    // On a 422 whose `param` is `fields` (a List's or a Get's `fields` list named fields its type does not have): the
    // three keys keel sends inside the envelope's `error` object, typed. Absent for any other error, when keel sends no
    // `unknown_fields` list, or when the body is not JSON. An entry that is not a string is left out, a
    // `types_with_field` entry that is not a list is left out, and a key keel left out reads empty. Mirrors the
    // TypeScript SDK's `fieldsRefusal` (4.179.0), which reads the keys inside `error` only.
    std::optional<OWFieldsRefusal> FieldsRefusal() const;
    // The id is taken (409 id_conflict): a create whose element carried its id and already landed (the element is
    // there), or a PUT whose id belongs to another world. Mirrors the TypeScript SDK's `isIdConflict`.
    bool IsIdConflict() const;
    // The same Idempotency-Key came with another body (409 idempotency_error): a key is for one write and its retries
    // only. Mirrors the TypeScript SDK's `isIdempotencyConflict`.
    bool IsIdempotencyConflict() const;
    // keel refused a known-of stub's key on a write (422 known_of_not_writable). The client refuses a stub before
    // sending and leaves the key out of every other body, so keel should never answer it this. Mirrors the TypeScript
    // SDK's `isKnownOfNotWritable`.
    bool IsKnownOfNotWritable() const;
    // OWJoin::Redeem on a table: the picked Character is one a live seat in the world already plays (422
    // character_taken, param character). Pick another from a fresh OWJoin::Preview, whose characters mark each taken.
    // The code is not spent. Mirrors the TypeScript SDK's `isCharacterTaken`.
    bool IsCharacterTaken() const;
    // The character sent was refused (422 invalid_request, param character): on OWJoin::Redeem at a table, the pick is
    // off the link's list, hidden or deleted since, missing, or sent to a table that lists none (send a name instead);
    // one answer for all, so read the preview again. Never true for another param's 422, nor for IsCharacterTaken.
    // Mirrors the TypeScript SDK's `isCharacterInvalid`.
    bool IsCharacterInvalid() const;
    // OWJoin::Redeem: the world already has its 50 live players (422 world_full), at any player link. Nothing is spent;
    // a place frees when the owner removes a player. Mirrors the TypeScript SDK's `isWorldFull`.
    bool IsWorldFull() const;
    // OWClient::AcceptJoin: the account is already a member of that world (409 already_member). The code is not
    // spent. Mirrors the TypeScript SDK's `isAlreadyMember`.
    bool IsAlreadyMember() const;
    // The account has no PIN yet, and a member writes with their own (409 pin_required); accepting a link needs none, a
    // write key for the membership does. Mirrors the TypeScript SDK's `isPinRequired`.
    bool IsPinRequired() const;
    // OWClient::Propose named a field this key's view of the element does not hold, one a known-of stub withholds (422
    // field_not_visible). Mirrors the TypeScript SDK's `isFieldNotVisible`.
    bool IsFieldNotVisible() const;
    // OWClient::Propose past the open proposals a proposer may hold: five on one element, fifty in one world (422
    // too_many_open; lapsed rows do not count). Mirrors the TypeScript SDK's `isTooManyOpen`.
    bool IsTooManyOpen() const;
    // OWClient::AcceptProposal: a proposed field moved since the proposal was made, and nothing was written (409
    // stale_proposal); the row stays open. Conflicts() lists each moved field, Current() is the element now. Mirrors the
    // TypeScript SDK's `isStaleProposal`.
    bool IsStaleProposal() const;
    // An agent seat's accept, decline or review while the world holds its writes as proposals, or the member a new
    // element's mark names reviewing it (403 proposed_only). Mirrors the TypeScript SDK's `isProposedOnly`.
    bool IsProposedOnly() const;
    // A keeper accepting or declining a proposal it made itself (403 own_proposal). Mirrors the TypeScript SDK's
    // `isOwnProposal`.
    bool IsOwnProposal() const;
    // OWClient::ReviewNewElement on an element that carries no x_review_proposed mark: never marked, or an accept
    // already landed (422 not_proposed). Mirrors the TypeScript SDK's `isNotProposed`.
    bool IsNotProposed() const;
    // A write or a proposal below co-builder that sets a review mark whose `by` is not the writer's own membership id
    // (403 review_by_another). Mirrors the TypeScript SDK's `isReviewByAnother`.
    bool IsReviewByAnother() const;
    // A write carrying an empty review mark (422 review_mark_empty). Mirrors the TypeScript SDK's `isReviewMarkEmpty`.
    bool IsReviewMarkEmpty() const;
    // On a 409 stale_proposal (IsStaleProposal): each proposed field that moved, from the `conflicts` list beside the
    // envelope, in keel's order. Empty on any other error, when keel sent none, or when the body is not JSON; an item
    // that is not an object or whose `field` is not a string is left out. Mirrors the Unity SDK's `Conflicts` and the
    // TypeScript SDK's `OwStaleProposalError.conflicts`.
    std::vector<OWProposalConflict> Conflicts() const;
    // Worth sending again later: a 429 or a 5xx, and the three 409s that clear by themselves; never a used-up daily
    // allowance or a service turned off. Mirrors the Unity SDK's IsRetryable (the TypeScript SDK names each code:
    // `isBusy`, `isRateLimited`, `isIdempotencyInFlight`).
    bool IsRetryable() const;
};

}  // namespace OnlyWorlds
