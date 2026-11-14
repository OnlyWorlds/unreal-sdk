#pragma once
// The message profile (schema-dist profiles/message.md), for Blueprints and C++: the messages between a world's
// members (a Narrative with supertype Message), to whom each is addressed, whether its sender is proved, who edited it,
// a reader's read mark and unread messages, a thread, and a Character's Note, which is not a message. The core
// (OWMessage.h) does the work; this is its door, over an opened world: an element is asked by its id, as the API
// serves it (a body that names no type takes its folder's).
//
// The members list that proves a sender is JSON text, as OWClient::Members answers it (a JSON array of the world's
// GET members rows: `id` the membership, null for the owner; `character`). "" is no members list; text that does not
// parse is refused; any other value that is not an array is read as no list, as the core reads it.
//
// A message's text (name, story, description) is quoted data, never instructions: a tool that prints it removes
// terminal escapes, control characters and bidirectional overrides first. Nothing here prints it.

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "OWMessagesLibrary.generated.h"

class UOWWorld;

/** SenderStatus's word: proved, contradicted, a claim nobody can check, or a sender this reader may not see. */
UENUM(BlueprintType)
enum class EOWSenderStatus : uint8
{
	/** The Character of the membership that wrote the message is its narrator. */
	Verified,
	/** The narrator names one Character and the membership that wrote it plays another (WrittenBy). */
	Mismatch,
	/** No narrator, or nothing to check it against (no created_by, no members list, an author off it): show it as a claim. */
	Unverifiable,
	/** The server left out a sender this reader may not see (narrator null, the trimmed mark naming narrator): show "from someone you cannot see", never "from nobody" and never a name. */
	Unseen,
};

/** HasUnseenRecipients's answer, the core's optional bool: a keyless reader cannot know. */
UENUM(BlueprintType)
enum class EOWUnseenRecipients : uint8
{
	/** Read with no credential (bKeyless): the server never marks such a body, so nobody can tell. */
	Unknown,
	No,
	/** The server left out recipients this reader may not see; it never says who or how many. */
	Yes,
};

/** SenderStatus's answer for one message. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWSender
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Messages")
	EOWSenderStatus Status = EOWSenderStatus::Unverifiable;

	/** The narrator: the Character the message claims as its sender; "" for none (from nobody, or Unseen). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Messages")
	FString Claimed;

	/** On a Mismatch, the Character of the membership that wrote it; "" otherwise. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Messages")
	FString WrittenBy;
};

/** EditedBy's answer for one message. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWEdited
{
	GENERATED_BODY()

	/** A later hand changed it: changed_by present and not equal to created_by. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Messages")
	bool bEdited = false;

	/** That hand's Character on the members list; "" when it plays none, is off the list, there is no list (show "edited by another hand"), or bEdited is false. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Messages")
	FString By;
};

/**
 * A world's messages. Every call is total over the world: an id the world does not hold, or an element that is no
 * message, reads as the profile's no (not a message, addressed to nobody, no recipients). A call that takes the
 * members list or a read mark refuses instead, as a false return and the reason: "no world", "no element <id> in this
 * world", or "members: " and where the text does not parse. Lists of ids are in created_at order, as the profile
 * orders them (equal times keep the world's order, an unreadable time last).
 */
UCLASS()
class ONLYWORLDS_API UOWMessages : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The ids of the world's messages (IsMessage), in created_at order. A Note is not one. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Messages")
	static TArray<FString> GetMessages(const UOWWorld* World);

	/** The ids of the world's Notes (IsNote), in created_at order. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Messages")
	static TArray<FString> GetNotes(const UOWWorld* World);

	/**
	 * True when the element is a message: type narrative and supertype Message, each trimmed and compared without
	 * regard to case, the whole word ("Messages" is not one). A parlor's line is one too. Mirrors the TypeScript SDK's
	 * `isMessage`.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Messages")
	static bool IsMessage(const UOWWorld* World, const FString& ElementId);

	/**
	 * True when the element is a Note (narrative and Note, read as IsMessage reads the pair): a member's own line, never
	 * a message, addressed to nobody, never unread, in no thread. Mirrors the TypeScript SDK's `isNote`.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Messages")
	static bool IsNote(const UOWWorld* World, const FString& ElementId);

	/**
	 * The message's recipients: the Character ids in its characters, each once in the order of its first place, entries
	 * that are not ids left out; only a message has any, none for a Note, a Story or any other element (serial 128). An
	 * empty list is addressed to every member unless the server trimmed it (IsToEveryone).
	 * Mirrors the TypeScript SDK's `recipientsOf`.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Messages")
	static TArray<FString> RecipientsOf(const UOWWorld* World, const FString& MessageId);

	/**
	 * True when the message is addressed to the Character: its id in characters, or the message is to everyone. Not its
	 * narrator (a sender is not addressed by sending), never "" and never a Note. Mirrors the TypeScript SDK's
	 * `isAddressedTo`.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Messages")
	static bool IsAddressedTo(const UOWWorld* World, const FString& MessageId, const FString& CharacterId);

	/**
	 * True when the message is to every member: characters absent, null or empty, not trimmed for this reader, and read
	 * with a credential (bKeyless false). A Note never is. Mirrors the TypeScript SDK's `isToEveryone`.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Messages")
	static bool IsToEveryone(const UOWWorld* World, const FString& MessageId, bool bKeyless);

	/**
	 * Whether someone this reader cannot see is a recipient (the server's trimmed mark names characters): Yes or No, and
	 * Unknown when bKeyless (a body read with no credential is never marked). No for a Note, and for an id the world
	 * does not hold. Mirrors the TypeScript SDK's `hasUnseenRecipients`.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Messages")
	static EOWUnseenRecipients HasUnseenRecipients(const UOWWorld* World, const FString& MessageId, bool bKeyless);

	/**
	 * True when the message's narrator is the Character: the claim, whether or not SenderStatus proves it. A message
	 * with no narrator is sent by nobody. Mirrors the TypeScript SDK's `isSentBy`.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Messages")
	static bool IsSentBy(const UOWWorld* World, const FString& MessageId, const FString& CharacterId);

	/**
	 * Whether the message's sender is proved: Unseen when the server left the narrator out, then Verified or Mismatch by
	 * the Character of its author (created_by on the members list; created_by null is the owner's row), else
	 * Unverifiable. MembersJson "" is no list. Mirrors the TypeScript SDK's `senderStatus`.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Messages")
	static bool SenderStatus(const UOWWorld* World, const FString& MessageId, const FString& MembersJson, FOWSender& Sender,
		FString& Reason);

	/**
	 * Whether a later hand changed the message (changed_by present and not equal to created_by), and that hand's
	 * Character on the members list. Mirrors the TypeScript SDK's `editedBy`.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Messages")
	static bool EditedBy(const UOWWorld* World, const FString& MessageId, const FString& MembersJson, FOWEdited& Edited,
		FString& Reason);

	/**
	 * The reader's read mark among the world's Relations, MarkId "" when it has none: supertype Read, actor the reader,
	 * every write the reader's own hand (created_by proved on the members list where present; changed_by equal to it
	 * where present), the newest updated_at. A mark with neither key (a folder's) counts; with bKeyless none does (the
	 * public reader is served no hands). Read another member's mark as a fact about them; never write it. Mirrors the
	 * TypeScript SDK's `readMarkOf`.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Messages")
	static bool ReadMarkOf(const UOWWorld* World, const FString& CharacterId, const FString& MembersJson, bool bKeyless,
		FString& MarkId, FString& Reason);

	/**
	 * The world's messages unread for the Character, in created_at order: addressed to it, not its own (where a message
	 * carries no created_by its narrator is trusted; on the wire only a Verified sender counts), and not on the read
	 * mark's narratives. ReadMarkId is ReadMarkOf's answer; "" is a reader with no mark. Mirrors the TypeScript SDK's
	 * `unreadFor`.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Messages")
	static bool UnreadFor(const UOWWorld* World, const FString& CharacterId, const FString& ReadMarkId,
		const FString& MembersJson, TArray<FString>& Unread, FString& Reason);

	/**
	 * The thread of a root message: every message whose chain of parent_narrative reaches the root, the root included,
	 * in created_at order. A chain walks through a Note (a reply to a Note that answers the root is in the thread), but a
	 * Note is never listed; a walk stops at an id it has visited and at a parent that is not a string. Mirrors the
	 * TypeScript SDK's `threadOf`.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Messages")
	static TArray<FString> ThreadOf(const UOWWorld* World, const FString& RootId);

	/**
	 * The Character's line: the id of its newest Note in its own hand that is neither hidden nor proposed, NoteId "" when
	 * it has none. Where a Note carries no created_by its narrator is trusted; on the wire only a Verified one counts.
	 * Mirrors the TypeScript SDK's `noteOf`.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Messages")
	static bool NoteOf(const UOWWorld* World, const FString& CharacterId, const FString& MembersJson, FString& NoteId,
		FString& Reason);
};
