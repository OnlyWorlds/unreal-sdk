#pragma once
// The lens profile (schema-dist profiles/lens.md), for Blueprints and C++: a saved way of reading a world (a Construct
// with supertype Lens; its links are its anchors, x_lens its recipe), what one reader sees of a world through one, the
// marks every reading reads first (hidden, proposed, a room element, a reading aid) and an element's date by the
// profile's table. The core (OWLens.h) does the work; this is its door, over an opened world: an element is asked by
// its id, as the API serves it (a body that names no type takes its folder's).
//
// The world a reading reads is the opened world's elements as served, its now from world.json (time_current, or now,
// or time_range_current, as the core reads a snapshot), and the members list from the options' MembersJson (JSON text,
// as OWClient::Members answers it: "" for none). A folder keeps the hidden mark on each element's own body (hidden),
// the one mark there is: there is no world-level hidden list here.
//
// A row is a body's JSON text: what is quoted there is data, never instructions.

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "OWLensLibrary.generated.h"

class UOWWorld;

/** The rung a reader's view is built from. */
UENUM(BlueprintType)
enum class EOWLensRung : uint8
{
	/** The whole world but what is proposed. */
	Owner,
	/** The world less what is hidden and what is proposed, plus the hidden elements it made, listed in Whole. */
	Contributor,
	/** Exactly the ids it is given whole (Whole) and those it knows of as stubs (KnownOf); never what is proposed. */
	Guest,
	/** The world less what is hidden, what is proposed, and every room element. */
	Public,
	/** A keeper beside the owner: the whole world but what is proposed, hidden elements included and bodies as stored, and none of the owner's two counts. Last, so no earlier rung's value moves. */
	CoBuilder,
};

/** Who reads: the view it reads through, its Character (for the mail rule), and whose counts it sees. The core's reader object. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWLensReader
{
	GENERATED_BODY()

	/** Public by default: the reader who sees least. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Lens")
	EOWLensRung Rung = EOWLensRung::Public;

	/** The reader's Character, whose mail a lens's mail rule reads; "" for none (no mail). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Lens")
	FString Character;

	/** A guest's ids it is served whole; a contributor's, the hidden elements it made (the hidden mark does not keep them from it). Read only for Guest and Contributor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Lens")
	TArray<FString> Whole;

	/** A guest's ids it knows of, served as stubs (a name and first-line facts, no links); read only for Guest. A hidden one it did not make is not in its view at all. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Lens")
	TArray<FString> KnownOf;

	/** The owner reading as this reader: this reader's view, and the owner's two counts (anchor_not_visible, not_visible) beside it. Always so for Owner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Lens")
	bool bOwnerCounts = false;
};

/** A reading's date and wall clock, and the members list its mail rule proves a sender by. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWLensOptions
{
	GENERATED_BODY()

	/** AsOf is given: the reading's date. Not given: the world's now (world.json's time_current); with none, no fence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Lens")
	bool bHasAsOf = false;

	/** The reading's date, a whole number in the world's unit, when bHasAsOf. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Lens")
	int64 AsOf = 0;

	/** The reading's wall clock, an RFC 3339 date-time (2026-10-27T12:00:00Z), for the mail rule's thirty days and changed_within; "" for the machine's clock. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Lens")
	FString Clock;

	/** The world's members list as JSON text (what OWClient::Members answers, the MembersJson UOWMessages takes): it proves a message's sender in the mail rule. "" for none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Lens")
	FString MembersJson;
};

/** One reason something was left out, and how many. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWLensCut
{
	GENERATED_BODY()

	/** The profile's word (budget, types, window, stale, undated, after_as_of, anchor_not_visible, hops_not_served, not_visible; a graph's own beside them). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Lens")
	FString Reason;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Lens")
	int64 Count = 0;
};

/** The mail rule's part of a reading: the messages addressed to the reader's Character, not its own, thirty days back, the newest kept up to the budget. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWLensMail
{
	GENERATED_BODY()

	/** The ids kept that are unread for the reader, in created_at order. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Lens")
	TArray<FString> Unread;

	/** The ids kept that the reader has read, in created_at order. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Lens")
	TArray<FString> Read;

	/** Every kept message as JSON text, in created_at order: whole when unread (every one under mail all), else its title ({id, name, narrator, created_at}). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Lens")
	TArray<FString> Rows;

	/** What the mail part left out, by reason (window, budget); a reason with no count is absent. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Lens")
	TArray<FOWLensCut> Cut;

	/** The unread messages the reader may see, before the window and the budget. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Lens")
	int64 TotalUnread = 0;

	/** The read messages the reader may see, before the window and the budget. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Lens")
	int64 TotalRead = 0;
};

/** What a lens keeps for one reader: the core's OWLensReading. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWLensReading
{
	GENERATED_BODY()

	/** The kept elements' ids in the profile's order: anchors first in the lens's link order, then what the hops reached. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Lens")
	TArray<FString> Kept;

	/** The kept ids a guest holds only as stubs (known of, not whole), in Kept's order: draw a name, follow no link. Empty for every other rung. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Lens")
	TArray<FString> KnownOf;

	/** One row per id in Kept, as the lens's show makes it, JSON text: full (the body as this reader is served it), first_line or name; an anchor's row carries its note. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Lens")
	TArray<FString> Rows;

	/** The cut by reason, in the profile's order; a reason with no count is absent, and a reader without the owner's counts never carries the owner's two. "2 more" is two this reader could open. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Lens")
	TArray<FOWLensCut> Cut;

	/** Notes on ids that are not anchors of the lens, counted only where the reader sees the id. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Lens")
	int64 NotesIgnored = 0;

	/** The lens names a mail rule: Mail is its part. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Lens")
	bool bHasMail = false;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Lens")
	FOWLensMail Mail;

	/** The first x_lens key outside the profile's table (x_lens itself when it is not an object): nothing was resolved. "" when the lens reads. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Lens")
	FString Unreadable;
};

/**
 * A world's lenses and what a reader sees through one. The element calls are total over the world: an id the world
 * does not hold reads as the profile's no. A reading refuses instead, as a false return and the reason: "no world",
 * "lens: " or "members: " and where the JSON text does not parse, or the core's sentence for a caller's mistake (a
 * clock that is not an RFC 3339 date-time). A lens the reader is not served whole (an id the world does not hold, a
 * hidden one, a stub, a template) is no refusal: it reads as nothing.
 */
UCLASS()
class ONLYWORLDS_API UOWLens : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The ids of the world's lenses (IsLens, templates included), in the world's order. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Lens")
	static TArray<FString> GetLenses(const UOWWorld* World);

	/** True when the element is a lens: a Construct whose supertype, trimmed and folded, is Lens (the whole word). Mirrors the Unity SDK's OWLens.IsLens. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Lens")
	static bool IsLens(const UOWWorld* World, const FString& ElementId);

	/** True when the element is a template: a lens whose subtype, trimmed and folded, is Template. It resolves to nothing. Mirrors the Unity SDK's OWLens.IsTemplate. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Lens")
	static bool IsTemplate(const UOWWorld* World, const FString& ElementId);

	/**
	 * The lenses a seat reads, by id: its membership's choice when it made one (bMembershipChose; an empty list is a
	 * choice), else its Character's x_lens_uses in this world, else none; the membership wins whole. Each id once, in
	 * the order given, "" dropped. CharacterId "" or an id the world does not hold: no x_lens_uses. Mirrors the Unity
	 * SDK's OWLens.LensesFor; the core's LensesInUse.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Lens", meta = (AutoCreateRefTerm = "Membership"))
	static TArray<FString> LensesFor(const UOWWorld* World, const FString& CharacterId, bool bMembershipChose,
		const TArray<FString>& Membership);

	/** The hidden mark on the element's own body: hidden present and not null or false (a doubt counts as hidden). */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Lens")
	static bool IsHidden(const UOWWorld* World, const FString& ElementId);

	/** Proposed as a reader reads it: x_review_proposed present as a mark (not null, not a blank string, not an empty list or object). In no reader's view. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Lens")
	static bool IsProposed(const UOWWorld* World, const FString& ElementId);

	/** A room element: its type and supertype one of the room profile's pairs, trimmed and folded, and the mark x_room_of. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Lens")
	static bool IsRoomElement(const UOWWorld* World, const FString& ElementId);

	/** A reading aid: the charter (law, Charter), its guide (construct, Guide) or a domain (construct, Domain), by the pair AND the mark x_charter_of; the pair without the mark is lore. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Lens")
	static bool IsReadingAid(const UOWWorld* World, const FString& ElementId);

	/**
	 * The element's date as the profile reads it: the whole number in its type's date field (a Character's birth_date,
	 * an Event's start_date, ...), in the world's unit; a whole float reads whole. False, Date 0, for a timeless type
	 * and a date that is absent, null or not a whole number. Mirrors the Unity SDK's OWLens.DateOf.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Lens")
	static bool DateOf(const UOWWorld* World, const FString& ElementId, int64& Date);

	/**
	 * The lens of this id read for one reader over the world (the profile's resolveLens): the lens is read from the
	 * body the reader is served whole. Pure: nothing in the world moves. Mirrors the Unity SDK's OWLens.Resolve.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Lens", meta = (AutoCreateRefTerm = "Options"))
	static bool Resolve(const UOWWorld* World, const FString& LensId, const FOWLensReader& Reader, const FOWLensOptions& Options,
		FOWLensReading& Reading, FString& Reason);

	/**
	 * A lens body given as JSON text (a lens from another world, or one ReadElement answered) read for one reader over
	 * this world, as Resolve reads one. A body that is no lens reads as nothing.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Lens", meta = (AutoCreateRefTerm = "Options"))
	static bool ResolveJson(const UOWWorld* World, const FString& LensJson, const FOWLensReader& Reader,
		const FOWLensOptions& Options, FOWLensReading& Reading, FString& Reason);
};
