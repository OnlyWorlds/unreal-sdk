#pragma once
// The address profile (schema-dist profiles/address.md), for Blueprints and C++: an ow:// link or a site URL read into
// its parts, or refused by the profile's named reason; an element's or a world's address written from its parts. The
// core (OWAddress.h) does the work; this is its door. A refusal is a false return and the reason ("bad_id", ...), never
// an error. An address is public text: nothing here resolves or fetches one, and one string is read whole.

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "OWAddressLibrary.generated.h"

/**
 * An address's parts: what ParseAddress reads, what WriteAddress writes. A part is given when its string is not empty
 * or its bHas flag is set; set the flag to have an empty string refuse as that part would (the profile: an empty
 * string is never absent). A reading sets each flag of a part it holds.
 */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWAddress
{
	GENERATED_BODY()

	/** "human" (https://www.onlyworlds.com/w/...) or "text" (ow://...). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Address")
	FString Form;

	/** The world's id (a reading's in lowercase, or its slug as written when bWorldIsSlug). None in the text form inside a world. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Address")
	FString World;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Address")
	bool bHasWorld = false;

	/** One of the 22 type words ("character"). None for a world's own address. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Address")
	FString Type;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Address")
	bool bHasType = false;

	/** The element's id (a reading's in lowercase). None for a world's own address. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Address")
	FString Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Address")
	bool bHasId = false;

	/** The lens's element id, ?lens=<id> (a reading's in lowercase). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Address")
	FString Lens;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Address")
	bool bHasLens = false;

	/** The moment, @<step>: a step of the world's time line, within the 32-bit range (a writer refuses any other as bad_moment). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Address")
	int64 Moment = 0;

	/** The moment is given (0 is a step). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Address")
	bool bHasMoment = false;

	/** A reading's: the human form named its world by slug. Never written back: a writer refuses a slug as bad_world. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Address")
	bool bWorldIsSlug = false;

	/** A reading's: the string as received is exactly what a writer writes for it. A writer ignores it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Address")
	bool bCanonical = false;
};

/** Addresses: ow:// links and the site's URLs, read and written as every OnlyWorlds package reads and writes them. */
UCLASS()
class ONLYWORLDS_API UOWAddresses : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Read one string as the profile's reader does (White_Space trimmed at both ends). False with Reason, the first
	 * check that fails ("not_an_address", "uppercase", "segments", "bad_world", "unknown_type", "bad_id", "bad_moment",
	 * "bad_lens"), and an empty Address.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Address")
	static bool ParseAddress(const FString& Text, FOWAddress& Address, FString& Reason);

	/**
	 * The address that starts exactly at Index in a text, found in prose (a sentence, a line of a report), and End, the
	 * index just past it; false, an empty Address and End 0 when the text there is not an address. Where it ends is the
	 * grammar's, never the next space's: after the path only @ and a moment, then ?lens= and a lens; any other query key,
	 * a fragment and a full stop are prose. In the site's form the run after /w/ glued to a letter, a combining mark or
	 * an underscore is no address. Index and End count the FString's characters (UTF-16 units, as Mid and Find count
	 * them). ParseAddress still reads a string whole.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Address")
	static bool ReadAddressAt(const FString& Text, int32 Index, FOWAddress& Address, int32& End);

	/**
	 * The address a writer writes: lowercase, the world's id, the moment before the lens, nothing else. False with
	 * Reason, in the writer's order: "bad_form", "segments" (a type without an id, or an id without a type),
	 * "no_world", then the reader's.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Address")
	static bool WriteAddress(const FOWAddress& Address, FString& Text, FString& Reason);
};
