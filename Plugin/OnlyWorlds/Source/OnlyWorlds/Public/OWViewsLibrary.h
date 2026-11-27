#pragma once
// How an element is shown (schema-dist views.json), for Blueprints and C++: the one sentence for a card, a list row or a
// hover, the marks to show at the head of its page, and the fields its page draws in order, by the table baked at this
// release. The core (OWViews.h) does the work; this is its door. Presentation, not the standard: each game keeps its
// own drawing and reads the row. Nothing here draws, and nothing is stored.
//
// A conversation's chain (a row whose layout is "conversation", by parent_narrative) is the caller's to follow: for
// messages, UOWMessages::ThreadOf gives it, and each line is then drawn by its own view.

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "OWViewsLibrary.generated.h"

class UOWWorld;

/** A field a row shows folded, under its label. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWViewFold
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Views")
	FString Field;

	/** The words the fold is shown under, where the game has none of its own. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Views")
	FString Label;
};

/**
 * A row of views.json: how elements of one kind are shown. Its kind is a type alone (Supertype ""), a type with a
 * supertype, or a profile's pair (Profile "room" or "charter", Pair "narrative/Report"), which matches only with that
 * profile's mark. "" is none throughout: the table holds no empty word.
 */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWViewRow
{
	GENERATED_BODY()

	/** The row's own name, distinct in the table ("report", "room-line"). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Views")
	FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Views")
	FString Type;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Views")
	FString Supertype;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Views")
	FString Profile;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Views")
	FString Pair;

	/** The page that defines the kind. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Views")
	FString From;

	/** The fields that give the one sentence, the first that holds something winning. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Views")
	TArray<FString> Sentence;

	/** The fields drawn open first, in order. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Views")
	TArray<FString> Lead;

	/** The fields drawn folded, each under its label. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Views")
	TArray<FOWViewFold> Fold;

	/** The layout: "document" or "conversation". */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Views")
	FString Layout;

	/** The link a conversation follows ("parent_narrative"); "" when the layout names none. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Views")
	FString LayoutBy;

	/** Marks this kind's page shows after the table's own banners. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Views")
	TArray<FString> Banner;
};

/** A mark every page shows at its head, and the page or document that defines how it is read. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWViewBanner
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Views")
	FString Mark;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Views")
	FString DefinedBy;
};

/** A layout the table defines, and its sentence. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWViewLayoutWords
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Views")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Views")
	FString Sentence;
};

/** How one element is shown. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWView
{
	GENERATED_BODY()

	/** A row matches it; false when it draws as its type (Row is then empty). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Views")
	bool bHasRow = false;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Views")
	FOWViewRow Row;

	/** The one sentence for a card, a list row or a hover; "" when there is no row or no field it names holds anything (a sentence is never blank). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Views")
	FString Sentence;

	/** The marks to show at the head of its page, in order: the table's banners it carries, then the row's own. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Views")
	TArray<FString> Banners;

	/** The fields its page draws, in the order drawn: the row's lead, the rest of the type's section fields, the folds; only fields that hold something. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Views")
	TArray<FString> Order;
};

/** The views table as baked at this release. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWViewsTable
{
	GENERATED_BODY()

	/** views.json's format (3). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Views")
	int32 Version = 0;

	/** sha256 of the views.json the table was baked from. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Views")
	FString Sha256;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Views")
	TArray<FOWViewBanner> Banners;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Views")
	TArray<FOWViewLayoutWords> Layouts;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Views")
	TArray<FOWViewRow> Rows;
};

/**
 * How an element is shown, by its kind. Pass the element as the reader was served it: the banners read what was served
 * (a known-of stub, the hidden mark, a trimmed list).
 */
UCLASS()
class ONLYWORLDS_API UOWViews : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * How one element is shown (the core's OWViews::ViewOf): its row, its one sentence, the marks at the head of its
	 * page and the fields its page draws in order. ElementJson is the element as served (a read node's Json,
	 * UOWWorld::GetElementJson, a stub). False with Reason "element: " and where, for text that does not parse; text
	 * that is "" or parses to anything but an object is no element, true with the empty view. Pure. The Unity SDK's
	 * OWViews.ViewOf.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Views")
	static bool ViewOf(const FString& ElementJson, FOWView& View, FString& Reason);

	/** How one element of an opened world is shown, as the owner is served it; the empty view for an id the world does not hold. Pure. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Views")
	static FOWView ViewOfElement(const UOWWorld* World, const FString& ElementId);

	/**
	 * A type's section fields in the schema's own order: the order a page draws the fields no row names, and where a
	 * game that draws empty fields to be filled draws them. The type trimmed and folded; empty for a word that is not
	 * one of the 22. A Pin's generic link is its one name, element. Pure.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Views")
	static TArray<FString> SectionFieldsOf(const FString& Type);

	/** The table as baked at this release: its version and sha256, the head marks, the layouts and the rows, each in the file's order. Pure. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Views")
	static FOWViewsTable GetViewsTable();
};
