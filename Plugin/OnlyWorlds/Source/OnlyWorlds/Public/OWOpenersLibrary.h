#pragma once
// The Open with menu (the OnlyWorlds tools list's README, "Open with"), for Blueprints and C++: the rows that open a
// world, or one element of it, in a tool, in order, each with the word it says when it cannot open now. The core
// (OWOpeners.h) does the work over the tools list baked into this release (OWTools::All()); this is its door. A menu
// draws these rows and decides no word of its own.

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "OWAddressLibrary.h"
#include "OWWorld.h"

#include "OWOpenersLibrary.generated.h"

class UOWWorld;

/** Who reads the menu. */
UENUM(BlueprintType)
enum class EOWAudience : uint8
{
	/** Anyone: only the rows that need nothing first. */
	Public,
	/** A seat in the world, a guest included. */
	Member,
};

/** What the surface knows of its reader's sign-in. */
UENUM(BlueprintType)
enum class EOWSignIn : uint8
{
	/** Not known: no row says so. */
	Unknown,
	SignedIn,
	SignedOut,
};

/** What the surface drawing the menu knows of itself and its reader. Nothing is guessed. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWOpenerSurface
{
	GENERATED_BODY()

	/** Public by default: a row that needs anything does not exist for the public. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Open with")
	EOWAudience Audience = EOWAudience::Public;

	/** The surface's own tool's client name, when it is a listed tool (its row then says "You are here"); "" for none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Open with")
	FString Tool;

	/** Read only in a browser: a game cannot see the browser's sign-in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Open with")
	EOWSignIn SignIn = EOWSignIn::Unknown;

	/** False in a game (the default): a tool that needs an account says "Opens in your browser". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Open with")
	bool bBrowser = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Open with")
	bool bPhone = false;

	/** False when the device has no connection: every row whose link is a web address says "Needs a connection" (its href kept). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Open with")
	bool bOnline = true;
};

/** One row of the menu. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWOpener
{
	GENERATED_BODY()

	/** "onlyworlds" for the site's own row, else the tool's id in the list. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Open with")
	FString Id;

	/** "onlyworlds.com", or the tool's name. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Open with")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Open with")
	FString Href;

	/** What the row says ("You are here", "Needs a connection", "Opens in your browser", "Sign in to Atlas first", ...); "" when it opens now and says nothing. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Open with")
	FString State;
};

/**
 * The Open with menu's rows: the site's page first, then each listed tool with a door for that kind, in the list's
 * order. A lens and a moment on an address ride on no href. A refusal is a false return and the reason: the address
 * profile's word for what its writer refuses of the address, or a sentence for what is no address's refusal.
 */
UCLASS()
class ONLYWORLDS_API UOWOpeners : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * The rows for an element of the world (by its id and its type), or for the world itself when ElementId is "". An
	 * element's menu reads the element itself (ServedElement): a tool whose element door skips a mark it carries
	 * (room, hidden, proposed, map space, a reading aid: the list's element_skips) is not there.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Open with")
	static bool OpenersFor(const UOWWorld* World, const FString& ElementId, const FOWOpenerSurface& Surface,
		TArray<FOWOpener>& Rows, FString& Reason);

	/** An element of a world folder as the API serves it, the shape the core's OWOpenersOptions takes: its body, with its type when the body declares none (a folder keeps it in the folder's name). */
	static OnlyWorlds::OWJsonValue ServedElement(const OnlyWorlds::OWElement& Element);

	/**
	 * The rows for an address (one ParseAddress read, or parts): an element's when it gives an id, else its world's.
	 * Form "" is the site's (human). The whole address is checked as a writer checks it, its lens and moment too.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Open with")
	static bool OpenersForAddress(const FOWAddress& Address, const FOWOpenerSurface& Surface, TArray<FOWOpener>& Rows,
		FString& Reason);
};
