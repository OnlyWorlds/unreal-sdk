#pragma once
// Three layout methods for a world nobody laid out, for Blueprints and C++, read from its graph as UOWGraph::Read
// answers it (Maps, Pins and Markers are out of that graph by default, as by-links-3d asks): by-kind and around in two
// dimensions, by-links-3d in three, and for each an Unplaced reading, which elements a saved layout Map of the method
// does not place. The core (OWLayout.h) does the work; this is its door. The same graph gives the same points as the
// npm SDK and the Python package (by-kind to the last bit, around within about 1e-12, by-links-3d to the bit or within
// 6e-8 as the core says), and every point is a 64-bit float here as there. Points are keyed by id, never by name.
//
// A refusal is a false return and the reason: the core's sentence for a graph it cannot read (a type that is not one of
// the 22, an id twice, ...), a steps that is not 1 or 2, a focus that is not a node; by-links-3d's own refusals by the
// page's word alone (held-not-object, held-not-point, held-unit, held-box); "elements[n]: " and where a row's text does
// not parse.

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "OWGraphLibrary.h"

#include "OWLayoutLibrary.generated.h"

/** One element's point, in a frame of -1 to 1 on the picture's longer axis, centred. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWLayoutPoint
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Layout")
	FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Layout")
	FVector2D Point = FVector2D::ZeroVector;
};

/** One element's point round the focus: the focus at 0, 0, and how many links away from it the element is. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWLayoutAroundPoint
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Layout")
	FString Id;

	/** 0 for the focus, 1 for a neighbour, 2 for a neighbour's neighbour. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Layout")
	int32 Depth = 0;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Layout")
	FVector2D Point = FVector2D::ZeroVector;
};

/** One element placed by by-links-3d: its point, in units where a link at rest is 60 long, and how it got there. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWLayout3dPoint
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Layout")
	FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Layout")
	FVector Point = FVector::ZeroVector;

	/** centre (a group's centre), with (in a centre's group), arm, link (its only link is to the body) or map (at its Pin on the world's map). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Layout")
	FString How;

	/** The id of the centre it stands with, "" for none. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Layout")
	FString Group;

	/** "" where Group is an id; else map (pinned) or one-link (arm or link). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Layout")
	FString Part;

	/** It stands at its held point (a held element that is not pinned). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Layout")
	bool bHeld = false;
};

/** What LayoutByLinks3d answers. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWLayoutByLinks3d
{
	GENERATED_BODY()

	/** The version of the method's answers: raised whenever an answer to a case of the method's fixture moves, so a picture kept to hand back as held can be told from one the method no longer gives. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Layout")
	int32 Version = 0;

	/** One row per placed element, in the order the graph's nodes are. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Layout")
	TArray<FOWLayout3dPoint> Points;

	/** The centres: the main shape's in rank order, then each other piece's. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Layout")
	TArray<FString> Centres;

	/** The elements it does not place (linked to no other and pinned on no map), by id in code point order. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Layout")
	TArray<FString> Unplaced;
};

/** The three layout methods and what a saved layout Map of each does not place. */
UCLASS()
class ONLYWORLDS_API UOWLayout : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Lay out a whole graph by kind: each kind of element gets a point on a circle, links act as springs, neighbours push
	 * apart, for a fixed number of seeded steps, then the answer is fitted into -1 to 1 on its longer axis. One point per
	 * node, in the graph's node order. Pure. The Unity SDK's OWLayout.LayoutByKind.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Layout")
	static bool LayoutByKind(const FOWWorldGraph& Graph, TArray<FOWLayoutPoint>& Points, FString& Reason);

	/**
	 * Lay out one element and what is linked to it: the focus at 0, 0, its neighbours on rings round it (by colour group,
	 * type word, then name), and with Steps 2 their neighbours further out. Steps is 1 or 2 (no default: 0 is refused). Pure. The Unity SDK's
	 * OWLayout.LayoutAround.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Layout")
	static bool LayoutAround(const FOWWorldGraph& Graph, const FString& Focus, int32 Steps, TArray<FOWLayoutAroundPoint>& Points,
		FString& Reason);

	/**
	 * Lay out a whole graph in three dimensions by its links. Elements: the world's elements as served (only Maps and Pins
	 * are read: a Map that holds three or more of the graph's elements by a Pin becomes a flat plate), empty for none.
	 * Held: the Points of an earlier answer, handed back so those elements stand where they stood (empty for none; of two
	 * rows with one id the last is read); HeldUnit, with bHasHeldUnit, the step they were rounded to, from 0 to an eighth
	 * of a rest. Pure. The Unity SDK's OWLayout.LayoutByLinks3d.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Layout", meta = (AutoCreateRefTerm = "Elements,Held"))
	static bool LayoutByLinks3d(const FOWWorldGraph& Graph, const TArray<FString>& Elements, const TArray<FOWLayout3dPoint>& Held,
		bool bHasHeldUnit, double HeldUnit, FOWLayoutByLinks3d& Layout, FString& Reason);

	/** The nodes a saved by-kind Map does not place: every node whose id is not in Placed (the element ids of the Map's Pins), by id in code point order. Pure. The Unity SDK's OWLayout.UnplacedByKind. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Layout", meta = (AutoCreateRefTerm = "Placed"))
	static bool UnplacedByKind(const FOWWorldGraph& Graph, const TArray<FString>& Placed, TArray<FString>& Unplaced, FString& Reason);

	/** The same for a saved around Map: the focus and every node within its steps that is not in Placed. Pure. The Unity SDK's OWLayout.UnplacedAround. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Layout", meta = (AutoCreateRefTerm = "Placed"))
	static bool UnplacedAround(const FOWWorldGraph& Graph, const FString& Focus, const TArray<FString>& Placed, int32 Steps,
		TArray<FString>& Unplaced, FString& Reason);

	/** The same for a saved by-links-3d Map: every node of the graph not in Placed, also one the method would not place. Pure. The Unity SDK's OWLayout.UnplacedByLinks3d. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Layout", meta = (AutoCreateRefTerm = "Placed"))
	static bool UnplacedByLinks3d(const FOWWorldGraph& Graph, const TArray<FString>& Placed, TArray<FString>& Unplaced, FString& Reason);
};
