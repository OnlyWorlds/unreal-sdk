#pragma once
// Joining a world by link, with no account: a player's code (a friend reading as one Character, a table's seat
// included) or an AI agent's, read with OWJoin::Preview and redeemed with OWJoin::Redeem. Neither sends a credential. A
// person's link is accepted signed in instead: OWClient::AcceptJoin, on an account token.
//
//   std::string code = OWJoin::ParseLink(pasted);           // "" when the text holds none
//   OWJoinConfig where;
//   where.transport = my_transport;                          // the plugin's, on Unreal's HTTP; a test's fake
//   OWJoin::Preview(code, where, [](OWReply<OWJoinPreview> seen) {
//       if (!seen.Ok() || seen.value.reader != "player") return;
//       // ...
//   });
//   OWRedeemOptions as;
//   as.name = "Ana";
//   OWJoin::Redeem(code, as, where, [](OWReply<OWJoined> r) {
//       if (r.Ok()) Keep(r.value.key);                       // a read key, no PIN: shown once, keep it
//   });
//
// Free functions beside OWClient rather than on it, as the Unity SDK's static OWJoin is: a client is built with a key,
// and joining is how a program gets one. The TypeScript SDK puts the same two calls on its client (previewJoin,
// redeemJoin) and sends them with no credential whatever key it holds; the wire is the same. Each answers an OWReply,
// once, as OWClient's calls do: keel's refusals an OWApiError of kind Api, a caller's mistake one of kind Refused with
// nothing sent.
//
// A link is <site>/join#<code>, or <site>/join/<world_id>#<code> when its owner turned the preview on: the code sits
// after '#', so it never reaches a server log. Every bad code (unknown, used, revoked, expired, or for another reader)
// is the same 404 not_found, byte for byte: OWApiError::IsNotFound. A refusal never spends the code.

#include "OWCharter.h"
#include "OWClient.h"
#include "OWJson.h"
#include "OWTransport.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace OnlyWorlds {

// A world, as a join code's answers name it.
struct OWJoinWorld {
    // Absent in a preview: the code is not spent, so the world's id is not handed out.
    std::optional<std::string> id;
    std::string name;
    // Absent when keel sends none.
    std::optional<std::string> description;

    static const std::vector<std::string>& Keys();
};

// A Character, by id and name, as a join answer names it.
struct OWJoinCharacter {
    std::string id;
    std::string name;

    static const std::vector<std::string>& Keys();
};

// One Character a party link offers, from its preview's `characters`: the player picks one not `taken` and sends its id
// to OWJoin::Redeem as OWRedeemOptions::character.
struct OWJoinPick {
    std::string id;
    std::string name;
    std::optional<std::string> image_url;  // the Character's picture, or absent
    // A live seat in the world plays it already, a person's included. keel's order puts these last.
    bool taken = false;

    static const std::vector<std::string>& Keys();
};

// An agent's seat in the world, from an agent code's answer.
struct OWJoinSeat {
    std::string id;
    std::string role;
    std::string agent_name;

    static const std::vector<std::string>& Keys();
};

// One thing in a join preview's glimpse: its name and its element type (character, location, ...). It carries no id.
struct OWJoinGlimpseThing {
    std::string name;
    std::string type;

    static const std::vector<std::string>& Keys();
};

// One link in a join preview's glimpse, between two of its things.
struct OWJoinGlimpseLink {
    // The place in OWJoinGlimpse::things, from 0, of the thing whose field holds the link; -1 when keel sent no whole
    // number.
    int64_t from = -1;
    // The place in OWJoinGlimpse::things, from 0, of the thing linked to; -1 when keel sent no whole number.
    int64_t to = -1;
    std::string field;  // the name of the field that holds the link
    std::string label;  // the words to show for the link

    static const std::vector<std::string>& Keys();
};

// A join preview's glimpse (the TypeScript SDK's OwJoinGlimpse): up to five things of the world by name, and the links
// among them, read as a visitor with no account would read the world if it were public (nothing hidden, nothing
// proposed). It names no id.
struct OWJoinGlimpse {
    // How the things were chosen: "strand", the world's chosen story (its happening, where it happened, its hero and
    // where the hero is now), or "links", the most linked thing and the most linked of its neighbours.
    std::string picked_by;
    std::vector<OWJoinGlimpseThing> things;  // at most five, in the order picked_by gives
    std::vector<OWJoinGlimpseLink> links;

    bool IsStrand() const { return picked_by == "strand"; }
    bool IsLinks() const { return picked_by == "links"; }
    // The glimpse in keel's shape, key for key: {picked_by, things: [{name, type}], links: [{from, to, field, label}]}.
    OWJsonValue ToJson() const;

    static const std::vector<std::string>& Keys();
};

// POST /join/preview's answer: what a join code holds, without using it (keel's preview_join; the TypeScript SDK's
// OwJoinPreview). A text keel serves as null, or leaves out, is absent (an optional) or "" (a plain string).
struct OWJoinPreview {
    // Who the code is for: "player" (a friend with no account; OWJoin::Redeem gives a read key), "agent" (an AI agent;
    // the same call gives a seat's write key and secret), or "person" (a signed-in account accepts it with
    // OWClient::AcceptJoin, not here).
    std::string reader;
    // Its id is absent: the code is not spent. Absent when keel sends no world object.
    std::optional<OWJoinWorld> world;
    std::optional<std::string> invited_by;       // the name the inviter signed the link with, unchecked
    std::optional<std::string> owner_character;  // the owner's chosen Character's name, never a username
    std::string role;                            // the role joining gives; always "guest" for a player
    bool with_character = false;                 // always true for a player, false for a person
    std::string expires_at;                      // ISO 8601, UTC, as sent
    // The note the inviter wrote on a person link, plain text with its line breaks: show it as text, never as markup.
    // Absent when there is none, and always on an agent's or a player's link.
    std::optional<std::string> note;
    // For a link whose role reads it (contributor or co_builder): up to five things by name and type and the links
    // among them. Absent for a guest's link, and so for every player's, and when no two things a visitor could see are
    // linked.
    std::optional<OWJoinGlimpse> glimpse;
    // A party link (a table: one link, up to 50 players) adds the four below; a link for one sends none of them.
    std::optional<int64_t> max_uses;   // how many players the link seats, 2 to 50
    std::optional<int64_t> uses_left;  // how many places are left at the table
    // True when the table lists no Characters: redeem with a name and no character, and the player gets a fresh one.
    std::optional<bool> name_required;
    // The picks, in the owner's order; empty when name_required, absent on a link for one.
    std::optional<std::vector<OWJoinPick>> characters;

    // A party link: more than one player joins by it, each as a picked or a fresh Character.
    bool IsTable() const { return max_uses && *max_uses > 1; }

    static const std::vector<std::string>& Keys();
};

// POST /join's answer, a player's or an agent's, told apart by `kind` (the TypeScript SDK's OwJoined, one struct here
// as in the Unity SDK). Shown once: the code is spent and a lost answer cannot be replayed, so keep `key` (and an
// agent's `pin`).
struct OWJoined {
    std::string kind;  // "player" or "agent"
    // A player's read key (ow_r_, no PIN), or an agent seat's write key.
    std::string key;
    // An agent seat's secret (ow_s_), sent as API-Pin (OWClientConfig::api_pin); absent for a player.
    std::optional<std::string> pin;
    std::optional<OWJoinWorld> world;
    // The Character a player reads as; an agent's when its link gave one, else absent.
    std::optional<OWJoinCharacter> character;
    // A player's role (guest); absent for an agent, whose role is on its seat: read RoleInWorld.
    std::optional<std::string> role;
    std::optional<OWJoinSeat> seat;         // an agent's seat; absent for a player
    std::optional<std::string> base_url;    // an agent's: the API's base URL its key is for
    std::optional<std::string> env;         // an agent's: the lines of a .env file
    // An agent's: the world's front door for the new seat (its sponsor, its Character, the Charter to read first), the
    // object as served, as OWClient::Me serves it; absent for a player.
    std::optional<OWJsonValue> front_door;
    // An agent's: the world's reading aids as the new seat reads them (OWCharter::Served of the answer, the same packet
    // as GET /me's); absent for a player or an older keel.
    std::optional<OWReadingAids> reading_aids;
    // The answer as served, for what is not modelled here (an agent's next, members, with_character).
    OWJsonValue served;

    bool IsPlayer() const { return kind == "player"; }
    bool IsAgent() const { return kind == "agent"; }
    // The role in the world, wherever this kind of answer carries it: a player's on the answer, an agent's on its seat.
    std::optional<std::string> RoleInWorld() const;

    static const std::vector<std::string>& Keys();
};

// OWClient::AcceptJoin's 201 (POST /account/join; the TypeScript SDK's OwJoinedMembership): the account's new
// membership, the same row as accepting an email invite.
struct OWJoinedMembership {
    std::string id;  // the membership's id, as Members and an element's created_by name it
    std::string world_id;
    std::string world_name;
    std::string role;                             // co_builder, contributor or guest
    std::optional<std::string> joined_at;         // ISO 8601 as served
    std::optional<std::string> roster_character;  // the id of the joiner's roster Character

    static const std::vector<std::string>& Keys();
};

// Where a keyless join call goes.
struct OWJoinConfig {
    // What the request goes through. Required: the core has no HTTP of its own.
    std::shared_ptr<OWTransport> transport;
    // keel's API root; trailing slashes are trimmed, and "" means production's (OWClient::DefaultBaseUrl).
    std::string base_url = OWClient::DefaultBaseUrl;
};

// What OWJoin::Redeem sends beside the code: each when set ("" included), in keel's spelling, in this order.
struct OWRedeemOptions {
    // A player's: the Character's name (keel names it "Player" when left out).
    std::optional<std::string> name;
    // An agent's, required for an agent code: keel answers 422 on agent_name without it.
    std::optional<std::string> agent_name;
    // A party link's pick, an id from the preview's characters; a table that lists none wants a name instead, and a
    // link for one ignores it.
    std::optional<std::string> character;
};

namespace OWJoin {

// Every join code starts with this.
extern const char* const CodePrefix;  // "ow_j_"

// The join code in a pasted link (https://www.onlyworlds.com/join#ow_j_... or .../join/<world_id>#ow_j_..., any host)
// or a bare code, or "" when the text holds none. The text is trimmed of Unicode's White_Space, the code read after the
// last '#' and trimmed again; a code holds no white space and something after CodePrefix. Text that is not UTF-8 holds
// none. Only the shape is checked: the server is the only judge of whether a code is good. Mirrors the Unity SDK's
// OWJoin.ParseLink (whose char.IsWhiteSpace is the same set: OWUnicode::IsWhite less U+001C to U+001F).
std::string ParseLink(std::string_view utf8);

// POST /join/preview: who invites, to which world, in what role, and for which reader; the code stays unused. Sent with
// no credential, the body {code}. Refused, nothing sent: an empty code, no transport. Mirrors the TypeScript SDK's
// previewJoin and the Unity SDK's OWJoin.PreviewAsync.
void Preview(const std::string& code, const OWJoinConfig& where, OWHandler<OWJoinPreview> done);

// POST /join: redeem a player's or an agent's code, 201 with an OWJoined whose kind tells the answers apart. Sent with
// no credential, the body {code, name, agent_name, character}, each option only when set. keel's refusals: every bad
// code, and a person's, is the one 404 (OWApiError::IsNotFound); a taken pick IsCharacterTaken, one off the list
// IsCharacterInvalid, a world with its 50 players IsWorldFull, an agent code with no agent_name a 422 whose param is
// agent_name. None spends the code. Refused, nothing sent: an empty code, no transport. Mirrors the TypeScript SDK's
// redeemJoin and the Unity SDK's OWJoin.RedeemAsync.
void Redeem(const std::string& code, const OWRedeemOptions& options, const OWJoinConfig& where,
            OWHandler<OWJoined> done);

// The answers read from their parsed bodies, as the calls read them: a text that is not a string "" or absent, a
// nested object that is not one absent, a list's entries that are not objects left out, a count or a place that is no
// whole number absent or -1. For a game that keeps an answer and reads it again.
OWJoinPreview PreviewOf(const OWJsonValue& answer);
OWJoined JoinedOf(const OWJsonValue& answer);
OWJoinedMembership MembershipOf(const OWJsonValue& answer);

}  // namespace OWJoin
}  // namespace OnlyWorlds
