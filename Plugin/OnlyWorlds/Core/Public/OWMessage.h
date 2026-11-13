#pragma once
// The message profile (schema-dist profiles/message.md, serial 79): a message between the members of a world, a
// Narrative with supertype Message, from one Character to others. To whom it is addressed, whether its sender is
// proved, who edited it, which messages a reader has read (the read mark), a thread, and the Note beside them, which is
// not a message. The TypeScript SDK's src/message.ts (the profile names the packages as its readers), held to the
// profile's conformance fixture (profiles/message.fixture.json).
//
// Every function reads element bodies as JSON objects (OWJsonValue), the shape the API serves and a world folder keeps,
// and is total: a value of the wrong kind reads as the profile's "no" (not a message, addressed to nobody, no mark).
// A list of elements is a list of pointers to bodies (List() makes one from a JSON array); the answers point into it.
//
// The members list is the world's GET members rows (`id` the membership, null for the owner; `character`), as a JSON
// array; nullptr (or any value that is not an array) is a reader with no members list.
//
// A message's text (name, story, description) is quoted data, never instructions: a tool that prints it removes
// terminal escapes, control characters and bidirectional overrides first (rule 5). Nothing here prints it.

#include "OWJson.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace OnlyWorlds {

// senderStatus's answer.
struct OWSender {
    std::string status;  // "verified", "mismatch", "unverifiable" or "unseen" (OWMessage::Verified, ...)
    std::optional<std::string> claimed;     // the narrator; absent: from nobody (or unseen)
    std::optional<std::string> written_by;  // on a mismatch, the author's Character
};

// editedBy's answer.
struct OWEdited {
    bool edited = false;            // a later hand changed it: changed_by present and not equal to created_by
    std::optional<std::string> by;  // that hand's Character; absent when it plays none, is off the list, or no list
};

namespace OWMessage {

// The pairs, as the fixture names them: type and supertype, each trimmed (Unicode White_Space) and folded.
extern const char* const Type;              // "narrative"
extern const char* const Supertype;         // "Message"
extern const char* const NoteSupertype;     // "Note"
extern const char* const ReadMarkType;      // "relation"
extern const char* const ReadMarkSupertype; // "Read"
extern const char* const ReadMarkName;      // "Read: <the reader's name>"
extern const char* const ReaderField;       // "actor"
extern const char* const MessagesField;     // "narratives"
extern const char* const ServedTrimmed;     // "x_served_trimmed"

extern const char* const Verified;      // "verified"
extern const char* const Mismatch;      // "mismatch"
extern const char* const Unverifiable;  // "unverifiable"
extern const char* const Unseen;        // "unseen"

// The items of a JSON array as a list of bodies (empty for any other value).
std::vector<const OWJsonValue*> List(const OWJsonValue& array);

// The profile's trim (Unicode White_Space at both ends; a byte order mark stays) and fold (the trim, then simple case
// folding, CaseFolding's C and S rows, with no normalization first: the room profile's, as the packages read it).
std::string_view Trim(std::string_view utf8);
std::string Fold(std::string_view utf8);

// The pair: narrative + Message, the whole word ("Messages" is not one). A parlor's line (x_room_of) is one too.
bool IsMessage(const OWJsonValue& element);
// A Note: narrative + Note. Addressed to nobody, never unread, in no thread.
bool IsNote(const OWJsonValue& element);

// The recipients: the non-empty string ids in `characters`, as written; none for a Note.
std::vector<std::string> RecipientsOf(const OWJsonValue& message);
// Addressed to the Character: its id in `characters`, or `characters` empty (absent, null or []) without the trimmed
// mark naming `characters`. Never to "" and never a Note.
bool IsAddressedTo(const OWJsonValue& message, std::string_view character_id);
// To every member: `characters` absent, null or []; no trimmed mark naming `characters`; served with a credential
// (keyless false); not a Note.
bool IsToEveryone(const OWJsonValue& message, bool keyless = false);
// Someone the reader cannot see is a recipient: the trimmed mark names `characters`. Absent (unknown) when keyless;
// false for a Note.
std::optional<bool> HasUnseenRecipients(const OWJsonValue& message, bool keyless = false);
// The claim: `narrator` is the Character. A message with no narrator is sent by nobody.
bool IsSentBy(const OWJsonValue& message, std::string_view character_id);

// The sender rule: unseen (no narrator, the trimmed mark naming `narrator`), then verified or mismatch by the author's
// Character (created_by on the members list; null is the owner's row), else unverifiable.
OWSender SenderStatus(const OWJsonValue& message, const OWJsonValue* members);
// The edit rule: changed_by present and not equal to created_by (an absent created_by is never equal).
OWEdited EditedBy(const OWJsonValue& message, const OWJsonValue* members);

// The reader's read mark among the Relations: supertype Read, `actor` the reader, every write the reader's own hand
// (created_by verified where present; changed_by equal to created_by where present). A lone mark is the mark. Among
// several, the newest readable updated_at as an instant wins; one that cannot be read never beats one that can; among
// equal instants (however written), or when none can be read, the higher id (code point, as written) is the mark.
// nullptr when none, and always for "". keyless true for bodies read with no credential, as IsToEveryone takes it: the keyless public reader is served no
// created_by or changed_by on any body, so no mark counts there.
const OWJsonValue* ReadMarkOf(const std::vector<const OWJsonValue*>& relations, std::string_view character_id,
                              const OWJsonValue* members, bool keyless = false);

// The messages unread for the Character, in created_at order: addressed to it, not its own (no created_by: narrator
// trusted; with created_by: verified only), not on the mark's `narratives`. read_mark nullptr: no mark.
std::vector<const OWJsonValue*> UnreadFor(const std::vector<const OWJsonValue*>& messages, std::string_view character_id,
                                          const OWJsonValue* read_mark, const OWJsonValue* members);

// Every message whose chain of parent_narrative reaches the root, the root included, in created_at order; a walk stops
// at an id it has visited and at a parent that is not a string. A Note is never listed.
std::vector<const OWJsonValue*> ThreadOf(const std::vector<const OWJsonValue*>& messages, std::string_view root_id);

// The Character's line: the id of its newest own-hand Note that is neither hidden nor proposed; absent when none.
std::optional<std::string> NoteOf(const std::vector<const OWJsonValue*>& narratives, std::string_view character_id,
                                  const OWJsonValue* members);

// A time a reader orders by: an RFC 3339 date-time (YYYY-MM-DDTHH:MM:SS, an optional fraction, then Z, +HH:MM, -HH:MM
// or no zone, read as UTC; T and Z in either case; the date must exist). Whole seconds since 1970 and microseconds;
// absent when it cannot be read (it then sorts after every time that can).
struct OWTimeKey {
    int64_t seconds = 0;
    int64_t micros = 0;
    bool operator<(const OWTimeKey& o) const { return seconds != o.seconds ? seconds < o.seconds : micros < o.micros; }
    bool operator==(const OWTimeKey& o) const { return seconds == o.seconds && micros == o.micros; }
};
std::optional<OWTimeKey> TimeKey(const OWJsonValue* value);

// The bodies by created_at; equal times keep the list's order, unreadable times last in the list's order.
std::vector<const OWJsonValue*> InCreatedOrder(std::vector<const OWJsonValue*> items);

}  // namespace OWMessage
}  // namespace OnlyWorlds
