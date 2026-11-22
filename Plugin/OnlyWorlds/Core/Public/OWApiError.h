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
struct OWFieldsRefusal {
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

struct OWApiError {
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
    // Worth sending again later: a 429 or a 5xx, and the three 409s that clear by themselves; never a used-up daily
    // allowance or a service turned off. Mirrors the Unity SDK's IsRetryable (the TypeScript SDK names each code:
    // `isBusy`, `isRateLimited`, `isIdempotencyInFlight`).
    bool IsRetryable() const;
};

}  // namespace OnlyWorlds
