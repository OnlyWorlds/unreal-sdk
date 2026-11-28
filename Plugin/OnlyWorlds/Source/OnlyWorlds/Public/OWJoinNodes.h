#pragma once
// Joining a world by invite link on Blueprints, over the core's join (OWJoin.h; OWClient::AcceptJoin): a pure call that
// reads the code out of a pasted link (UOWJoin::ParseJoinLink), and three async nodes with OnSuccess and OnFailure. The
// two keyless ones, PreviewJoin (what the code opens, the code unspent) and RedeemJoin (a player's read key, or an AI
// agent's seat key and secret), are sent with no credential at all; AcceptJoin takes a person's link on an account
// token. As every client node (OWClientNodes.h), each sends through FOWHttpTransport, so it answers later, on the game
// thread; a refusal answers at once, inside the node's activation.
//
// The key RedeemJoin answers is shown once: the code is spent and a lost answer cannot be replayed, so keep Key (and an
// agent's Pin) in your own save at once, never in an asset. It goes to OnSuccess and nowhere else: the nodes log
// nothing, and an error from RedeemJoin or AcceptJoin carries no body (RawBody is "", its message says the body was
// withheld), since a body may hold the key. The core keeps the rest of the answer for C++ (OWJoined's env and served).
//
// A text a graph cannot leave absent is "" for none, both ways. keel's refusals are told apart by Error.Status and
// Error.Code, as each factory's comment lists them; none spends the code.

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "OWCharterLibrary.h"
#include "OWClientNodes.h"
#include "OWJoin.h"

#include "OWJoinNodes.generated.h"

/** A world, as a join answer names it. The core's OWJoinWorld. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWJoinWorld
{
	GENERATED_BODY()

	/** The world's id; "" in a preview, where the code is not spent and the id is not handed out. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString Description;
};

/** A Character by id and name, as a join answer names it. The core's OWJoinCharacter. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWJoinCharacter
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString Name;
};

/** One Character a party link offers: the player picks one not bTaken and gives its Id to RedeemJoin as Character. The core's OWJoinPick. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWJoinPick
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString Name;

	/** The Character's picture, or "". */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString ImageUrl;

	/** A live seat in the world plays it already. keel's order puts these last. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	bool bTaken = false;
};

/** An AI agent's seat in the world, from an agent code's answer. The core's OWJoinSeat. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWJoinSeat
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString Role;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString AgentName;
};

/** One thing in a preview's glimpse: its name and its element type (character, location, ...), no id. The core's OWJoinGlimpseThing. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWJoinGlimpseThing
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString Type;
};

/** One link in a preview's glimpse, between two of its things. The core's OWJoinGlimpseLink. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWJoinGlimpseLink
{
	GENERATED_BODY()

	/** The index in Things of the thing whose field holds the link; -1 when keel sent no place. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	int32 From = -1;

	/** The index in Things of the thing linked to; -1 when keel sent no place. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	int32 To = -1;

	/** The name of the field that holds the link. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString Field;

	/** The words to show for the link. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString Label;
};

/** Up to five things of the world by name, and the links among them, as a visitor would read them. The core's OWJoinGlimpse. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWJoinGlimpse
{
	GENERATED_BODY()

	/** strand (the world's chosen story) or links (the most linked thing and its most linked neighbours); "" when the preview has no glimpse. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString PickedBy;

	/** At most five, in the order PickedBy gives. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	TArray<FOWJoinGlimpseThing> Things;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	TArray<FOWJoinGlimpseLink> Links;
};

/** What a join code holds, read without using it: what a player sees before saying yes. The core's OWJoinPreview. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWJoinPreview
{
	GENERATED_BODY()

	/** player (RedeemJoin gives a read key), agent (RedeemJoin gives a seat's key and secret) or person (a signed-in account takes it with AcceptJoin). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString Reader;

	/** The world's name and description; its Id is "" (the code is not spent). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FOWJoinWorld World;

	/** The name the inviter signed the link with, unchecked; "" for none. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString InvitedBy;

	/** The owner's chosen Character's name, never a username; "" for none. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString OwnerCharacter;

	/** The role joining gives: always guest for a player. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString Role;

	/** Always true for a player, false for a person. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	bool bWithCharacter = false;

	/** When the link stops working, ISO 8601 as sent. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString ExpiresAt;

	/** A person link's note, plain text with its line breaks: show it as text, never as markup. "" for none. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString Note;

	/** A person link's Character, the one the person would play once they join; "" when it names none (or it was since deleted, hidden or taken). Show it as text. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString CharacterName;

	/** For a contributor's or a co-builder's link: up to five things and their links. PickedBy "" when there is none. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FOWJoinGlimpse Glimpse;

	/** A party link: one link for a table of 2 to 50 players, each joining as a picked or a fresh Character. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	bool bIsTable = false;

	/** How many players the link seats; 0 when keel sent none (a link for one). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	int64 MaxUses = 0;

	/** How many places are left at the table; 0 when keel sent none. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	int64 UsesLeft = 0;

	/** The table lists no Characters: redeem with a Name and no Character, and the player gets a fresh one. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	bool bNameRequired = false;

	/** The table's picks, in the owner's order; empty on a link for one, and when bNameRequired. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	TArray<FOWJoinPick> Characters;

	/** The answer as served, JSON text, for what is not modelled here. A preview holds no secret. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString Json;
};

/**
 * A player's or an agent's answer from RedeemJoin, told apart by Kind. Shown once: keep Key (and an agent's Pin) at
 * once, in your own save, never in an asset. The core's OWJoined, without its env and served (each holds the key again;
 * C++ reads them there).
 */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWJoined
{
	GENERATED_BODY()

	/** player or agent. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString Kind;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	bool bIsPlayer = false;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	bool bIsAgent = false;

	/** A player's read key (ow_r_, no PIN), or an agent seat's write key: the ApiKey of the config every other node takes. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString Key;

	/** An agent seat's secret (ow_s_), the config's ApiPin; "" for a player. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString Pin;

	/** The world joined, its Id included now that the code is spent. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FOWJoinWorld World;

	/** The Character a player reads as; an agent's when its link gave one; Id "" for none. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FOWJoinCharacter Character;

	/** The role in the world, wherever this kind of answer carries it: a player's on the answer (guest), an agent's on its seat. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString Role;

	/** An agent's seat; Id "" for a player. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FOWJoinSeat Seat;

	/** An agent's: the API root its key is for; "" for a player. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString BaseUrl;

	/** An agent's: the world's front door for the new seat (its sponsor, its Character, the Charter to read first) as JSON text, as ReadMe serves it; "" for a player. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString FrontDoorJson;

	/** An agent's answer carried the world's reading aids (a player's never does). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	bool bHasReadingAids = false;

	/** The reading aids as the new seat reads them, the packet ReadMe's answer carries. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FOWReadingAids ReadingAids;
};

/** AcceptJoin's answer: the account's new membership, the same row as accepting an email invite. The core's OWJoinedMembership. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWJoinedMembership
{
	GENERATED_BODY()

	/** The membership's id, as the members list and an element's created_by name it. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString WorldId;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString WorldName;

	/** co_builder, contributor or guest. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString Role;

	/** ISO 8601 as served; "" for none. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString JoinedAt;

	/** The id of the joiner's roster Character; "" for none. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Join")
	FString RosterCharacter;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOWJoinPreviewAnswer, const FOWJoinPreview&, Preview, const FOWApiError&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOWJoinedAnswer, const FOWJoined&, Joined, const FOWApiError&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOWJoinMembershipAnswer, const FOWJoinedMembership&, Membership, const FOWApiError&, Error);

/** The join link's pure call. */
UCLASS()
class ONLYWORLDS_API UOWJoin : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * The join code in a pasted link (https://www.onlyworlds.com/join#ow_j_... or .../join/<world_id>#ow_j_..., any
	 * host) or a bare code, or "" when the text holds none: trimmed of Unicode's white space, read after the last '#'.
	 * Only the shape is checked; the server is the only judge of a code. Pure. The core's OWJoin::ParseLink.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Join")
	static FString ParseJoinLink(const FString& Text);
};

/**
 * What a join code opens, without using it: who invites, to which world, in what role, for which reader, and at a
 * table the places left and the Characters to pick from. Sent with no credential.
 */
UCLASS()
class ONLYWORLDS_API UOWClientJoinPreview : public UOWClientNode
{
	GENERATED_BODY()

public:
	/** The preview (Preview), or why not (Error). */
	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Join")
	FOWJoinPreviewAnswer OnSuccess;

	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Join")
	FOWJoinPreviewAnswer OnFailure;

	/**
	 * POST /join/preview. Code is the link's code (ParseJoinLink reads it out of a pasted link; the whole link sent
	 * as the code is a bad code). BaseUrl is keel's API root, "" for onlyworlds.com's. Every bad code (unknown, used,
	 * revoked, expired) is Error.Status 404, Error.Code not_found. A code with no text is refused, nothing sent.
	 * The core's OWJoin::Preview.
	 */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Join", meta = (BlueprintInternalUseOnly = "true", AdvancedDisplay = "BaseUrl"))
	static UOWClientJoinPreview* PreviewJoin(const FString& Code, const FString& BaseUrl = TEXT(""));

	virtual void Activate() override;

private:
	void Answer(OnlyWorlds::OWReply<OnlyWorlds::OWJoinPreview> Reply);

	FString CodeText;
};

/**
 * Redeem a player's or an agent's code: 201 with a player's read key and the Character it reads as, or an agent seat's
 * write key and secret. Sent with no credential. Shown once: keep Joined.Key at once. A person's code is a 404 here
 * (AcceptJoin takes it).
 */
UCLASS()
class ONLYWORLDS_API UOWClientJoinRedeem : public UOWClientNode
{
	GENERATED_BODY()

public:
	/** The answer (Joined), or why not (Error, with no body: it may hold the key). */
	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Join")
	FOWJoinedAnswer OnSuccess;

	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Join")
	FOWJoinedAnswer OnFailure;

	/**
	 * POST /join. Name: a player's Character's name (keel names it Player when ""). AgentName: an agent's name, required
	 * for an agent code. Character: a party link's pick, a Characters entry's Id (a table that lists none wants a Name
	 * instead). Each "" is not sent. BaseUrl as on PreviewJoin. keel's refusals, none of which spends the code: 404
	 * not_found (every bad code, and a person's); 422 character_taken (the pick is played already); 422 invalid_request
	 * with Error.Param character (the pick is off the list: preview again); 422 world_full (the world has its 50
	 * players); 422 with Error.Param agent_name (an agent code wants AgentName). An Error.Kind Transport whose message
	 * says the answer was lost while it was read came after keel answered: the code may be spent, so preview it again
	 * (a 404 says it was). A code with no text is refused, nothing sent. The core's OWJoin::Redeem.
	 */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Join", meta = (BlueprintInternalUseOnly = "true", AdvancedDisplay = "BaseUrl"))
	static UOWClientJoinRedeem* RedeemJoin(const FString& Code, const FString& Name, const FString& AgentName, const FString& Character,
		const FString& BaseUrl = TEXT(""));

	virtual void Activate() override;

private:
	void Answer(OnlyWorlds::OWReply<OnlyWorlds::OWJoined> Reply);

	FString CodeText;
	FString NameText;
	FString AgentText;
	FString CharacterText;
};

/** Accept a person's invite link as the account signed in: the account becomes a member of the world. */
UCLASS()
class ONLYWORLDS_API UOWClientJoinAccept : public UOWClientNode
{
	GENERATED_BODY()

public:
	/** The new membership (Membership), or why not (Error, with no body). */
	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Join")
	FOWJoinMembershipAnswer OnSuccess;

	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Join")
	FOWJoinMembershipAnswer OnFailure;

	/**
	 * POST /account/join with the code alone, on Config's account token (ApiKey ow_a_) as its Bearer, never a PIN.
	 * keel's refusals leave the code unspent: 404 not_found (every bad code, and an agent's); 409 already_member.
	 * Refused, nothing sent: any key but an account token (a player's or an agent's code is RedeemJoin's, with no key),
	 * a code with no text. The core's OWClient::AcceptJoin.
	 */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Join", meta = (BlueprintInternalUseOnly = "true"))
	static UOWClientJoinAccept* AcceptJoin(const FOWClientConfig& Config, const FString& Code);

	virtual void Activate() override;

private:
	void Answer(OnlyWorlds::OWReply<OnlyWorlds::OWJoinedMembership> Reply);

	FString CodeText;
};
