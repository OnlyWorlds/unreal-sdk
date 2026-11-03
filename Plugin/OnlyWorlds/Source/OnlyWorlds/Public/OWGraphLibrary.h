#pragma once
// The lens profile's graph section (schema-dist profiles/lens.md, "The graph"), for Blueprints and C++: a world read as
// nodes and edges for one reader. The elements the reader sees are the nodes, the links between them the edges, and
// what is left out is counted by reason. With a lens, the nodes are exactly what the lens keeps (UOWLens::Resolve) and
// its cut rides along. The core (OWGraph.h) does the work; this is its door, over an opened world read as UOWLens reads
// it (the elements as served, the now from world.json, the members list from the options).
//
// A node's family is a key for drawing (blue, green, pink, amber), never a label; its icon a Material Symbols name.
// Positions are not part of the graph: a layout is keyed by id, never by name.

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "OWLensLibrary.h"

#include "OWGraphLibrary.generated.h"

class UOWWorld;

/** How a graph is read: an unlensed one and its five flags, or a lens and its reading. The core's OWGraphOptions. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWGraphOptions
{
	GENERATED_BODY()

	/** A lens of this world, by id: the nodes are then exactly what it keeps for the reader, and no flag adds to them. "" for none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Graph")
	FString LensId;

	/** A lens body as JSON text, read as LensId reads one; never beside LensId. "" for none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Graph")
	FString LensJson;

	/** The lens's reading date is given (AsOf); read only with a lens. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Graph")
	bool bHasAsOf = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Graph")
	int64 AsOf = 0;

	/** The lens's wall clock, an RFC 3339 date-time, for its mail rule's window; "" for the machine's clock. Read only with a lens. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Graph")
	FString Clock;

	/** The world's members list as JSON text (OWClient::Members), for a lens's mail rule; "" for none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Graph")
	FString MembersJson;

	/** Bring back room elements, marked room. Refused for the public reader. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Graph")
	bool bRooms = false;

	/** Bring back hidden elements, marked hidden. The owner's alone: refused from any other rung. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Graph")
	bool bHidden = false;

	/** Bring back proposed elements, marked proposed. The owner's alone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Graph")
	bool bProposed = false;

	/** Bring back Maps, Pins and Markers, with no mark (a Zone is a node by default). Refused for the public reader. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Graph")
	bool bMapSpace = false;

	/** Bring back the reading aids (the charter, its guide, its domains), marked reading_aid. Any signed reader's; refused for the public reader. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|Graph")
	bool bReadingAids = false;
};

/** One element as a node. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWGraphNode
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Graph")
	FString Id;

	/** The type slug, as stored. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Graph")
	FString Type;

	/** The name as stored, when it is a text (bHasName); "" otherwise. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Graph")
	FString Name;

	/** The element carries a name that is a text. False for none (null or absent) and for a stored value that is not a text. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Graph")
	bool bHasName = false;

	/** The type's colour word (blue, green, pink or amber): a key for drawing, never a label. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Graph")
	FString Family;

	/** The type's icon, a Material Symbols name. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Graph")
	FString Icon;

	/** Why a node that is out by default is in: room, hidden, proposed, reading_aid, each that applies, in that order (map space carries none). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Graph")
	TArray<FString> Marks;

	/** A guest's known-of stub: a node with no edges out. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Graph")
	bool bKnownOf = false;
};

/** One link: one element, one field, one target. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWGraphEdge
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Graph")
	FString Source;

	/** The link field (characters, location, x_lens_leads_to, ...). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Graph")
	FString Field;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Graph")
	FString Target;

	/** The target's place in the field's list as this reader is served it (0 for a single link). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Graph")
	int64 Index = 0;
};

/** What Read answers: the core's OWWorldGraph. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWWorldGraph
{
	GENERATED_BODY()

	/** By id, compared by code point. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Graph")
	TArray<FOWGraphNode> Nodes;

	/** By source id, then field, both by code point, then index. A mirror pair is two edges. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Graph")
	TArray<FOWGraphEdge> Edges;

	/** The links left out because their target is not a node, by the target's reason; a reason with no count is absent, and a reader that is not the owner never carries the owner's. outside_lens: "3 more links" at a lens's edge. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Graph")
	TArray<FOWLensCut> EdgesCut;

	/** With a lens, its own cut, unchanged; without one, the elements out by default the reader sees, by reason. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Graph")
	TArray<FOWLensCut> Cut;

	/** With a lens the reader cannot read, the key it names (x_lens itself for a value that is not an object), and the graph is empty. "" otherwise. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Graph")
	FString Unreadable;
};

/** One node's count. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWGraphDegree
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Graph")
	FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Graph")
	int64 Degree = 0;
};

/**
 * A world as nodes and edges for one reader. A refusal is a false return and the reason: the flag's word (hidden,
 * proposed, rooms, map_space, reading_aids) for a flag asked from the wrong rung, refused before anything is read, the
 * first unmet in that order; "no world"; "lens: an id or a body, not both"; "lens: " or "members: " and where the JSON
 * text does not parse; or the core's sentence for a caller's mistake (a clock that does not read).
 */
UCLASS()
class ONLYWORLDS_API UOWGraph : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * The world's graph as the reader sees it (the profile's worldGraph). Without a lens, every element the reader sees
	 * less what is out by default (room elements, hidden and proposed ones, Maps, Pins and Markers, the reading aids),
	 * each back with its flag; with a lens, the lens's kept. The edges are the schema's links and the two declared
	 * extension links (x_lens_leads_to, x_spine_depends_on), between nodes only. Pure. Mirrors the Unity SDK's
	 * OWGraph.Read; the core's WorldGraph.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Graph", meta = (AutoCreateRefTerm = "Options"))
	static bool Read(const UOWWorld* World, const FOWLensReader& Reader, const FOWGraphOptions& Options, FOWWorldGraph& Graph,
		FString& Reason);

	/** Each node's degree: its edges in or out, each edge once for every node it touches (a mirror pair: 2 at each end; a link to itself: 1). Every node, 0 included, in the graph's node order. Mirrors the Unity SDK's OWGraph.DegreeOf. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Graph")
	static TArray<FOWGraphDegree> DegreeOf(const FOWWorldGraph& Graph);

	/** Each node's out-degree: the distinct nodes its own links point at (a target in two fields once, a link to itself not at all). Every node, in the graph's node order. Mirrors the Unity SDK's OWGraph.OutDegreeOf. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Graph")
	static TArray<FOWGraphDegree> OutDegreeOf(const FOWWorldGraph& Graph);
};
