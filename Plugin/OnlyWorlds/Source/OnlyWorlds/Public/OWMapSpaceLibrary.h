#pragma once
// The map-space profile (schema-dist profiles/map-space.md), for Blueprints and C++: a world's Maps, Zones, Markers
// and Pins as geometry. Each Map's frame, each Zone's outline from its Markers, each Pin's point, in the Map's own
// pixels counted from its bottom left, z upward (an elevation, never a height); one Map's layout keys; a point on a
// child Map carried up to its parents; and which of a map's Pins one reader knows. The core (OWMapSpace.h) does the
// work; this is its door. Nothing here draws, and nothing is stored.
//
// Every reading takes the elements as served, one element's JSON text each: an opened world's through ServedElements
// (the owner's view: every element it holds), or the API's, the Elements of ListAllElements for the Map, Zone, Marker
// and Pin types appended into one list (a v2 body carries its type). A pure node runs again for every pin it feeds and
// reads the whole list each time: keep the answer in a variable.

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "OWLensLibrary.h"

#include "OWMapSpaceLibrary.generated.h"

class UOWWorld;

/** A point in a Map's pixels: whole numbers, z 0 when the row has none or one that is not whole. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWMapSpacePoint
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	int64 X = 0;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	int64 Y = 0;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	int64 Z = 0;
};

/** One Map's frame. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWMapSpaceMap
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	FString Id;

	/** The frame has a width: the Map's own when a whole number, else measured from its own placed points (bMeasured); false with no point. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	bool bHasWidth = false;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	int64 Width = 0;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	bool bHasHeight = false;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	int64 Height = 0;

	/** The Map's depth when a whole number: the z extent. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	bool bHasDepth = false;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	int64 Depth = 0;

	/** The largest z among the Map's own outline and Pin points, kept as written (so a z past Depth shows). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	bool bHasZMax = false;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	int64 ZMax = 0;

	/** Metres per pixel: the Map's x_metres_per_pixel when it is a positive number. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	bool bHasScale = false;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	double Scale = 0;

	/** The Map's location, "" for none. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	FString Location;

	/** The Map's parent_map, "" for none. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	FString ParentMap;

	/** Width or Height was measured from the points, not written (the profile's extent: points). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	bool bMeasured = false;
};

/** One Zone's outline on one Map: its Markers' points in order, a repeated point kept. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWMapSpaceOutline
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	FString Zone;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	FString Map;

	/** polygon or line: the Zone's own shape when it is one of the two, else polygon. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	FString Shape;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	TArray<FOWMapSpacePoint> Points;
};

/** One Pin's point and the element it places. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWMapSpacePin
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	FString Pin;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	FString Map;

	/** The Pin places an element: its element_type and element_id, both or neither (a Pin with only one places none). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	bool bHasElement = false;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	FString ElementType;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	FString ElementId;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	int64 X = 0;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	int64 Y = 0;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	int64 Z = 0;
};

/** What MapSpaceOf answers: the core's OWWorldMapSpace. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWWorldMapSpace
{
	GENERATED_BODY()

	/** By id in code point order. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	TArray<FOWMapSpaceMap> Maps;

	/** By Zone id, then Map id. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	TArray<FOWMapSpaceOutline> Zones;

	/** By id in code point order. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	TArray<FOWMapSpacePin> Pins;

	/** What was left aside, by reason (no_point, no_markers, dangling, no_map, in that order), a reason only when counted. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	TArray<FOWLensCut> Out;
};

/** What LayoutOf answers: one Map's layout keys. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWMapLayout
{
	GENERATED_BODY()

	/** The Map is a layout map: its x_layout is an object whose method is a non-empty string. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	bool bHasLayout = false;

	/** The method word ("hand" for a board, whose Pins are all placed by hand). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	FString Layout;

	/** The ground Map's id, when x_layout.settings.ground.map names a served Map other than this one; "" for none. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	FString Ground;

	/** The Pins a new lay-out keeps where they are, by id in code point order: every Pin on a board, the Pins whose x_hand is true on any other layout map, none on a Map that is not one. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	TArray<FString> Hand;
};

/** One point of a carry: a Map and the point in its pixels, from its bottom left. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWMapCarryPoint
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	FString Map;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	FVector2D Point = FVector2D::ZeroVector;
};

/** What CarryOf answers: the core's OWMapCarry. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWMapCarry
{
	GENERATED_BODY()

	/** The Map's own x_parent_frame is a parent frame: four numbers, x0 < x1 and y0 < y1, its parent served. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	bool bHasParentFrame = false;

	/** The frame's corner (x0, y0) on the parent, as written. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	FVector2D ParentFrameMin = FVector2D::ZeroVector;

	/** The frame's corner (x1, y1) on the parent, as written. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	FVector2D ParentFrameMax = FVector2D::ZeroVector;

	/** What is wrong with the Map's own x_parent_frame (no_parent, not_four_numbers, not_a_box); "" for nothing. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	FString Lint;

	/** The point on this Map as given, then on each Map it is carried to, up the parent_map chain. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	TArray<FOWMapCarryPoint> Points;

	/** Why the last point is carried no further: top, dangling, cycle, no_frame or no_carry. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	FString Stop;
};

/** One Pin a reader knows. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWPinKnown
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	FString Pin;

	/** The Pin's map as written when a non-empty string, "" for none; not checked against any Map. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	FString Map;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	FString ElementType;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	FString ElementId;

	/** whole, or known_of for an element the reader only knows of. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	FString How;
};

/** What PinsKnownTo answers. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWPinsKnownTo
{
	GENERATED_BODY()

	/** By Pin id in code point order. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	TArray<FOWPinKnown> Known;

	/** Pins that place an element the reader was not served: a count, never ids. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	int64 Unknown = 0;

	/** no_link, a Pin that places nothing, when one was counted. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Map Space")
	TArray<FOWLensCut> Out;
};

/**
 * A world's map space. A refusal is a false return and the reason: "elements[n]: " (or "pins[n]: ", "served[n]: ") and
 * where a row's text does not parse; "no map " and the id, for a MapId no Map row of the elements carries (compared
 * case-exactly); or the core's sentence. Rows are read by their type as written, case-exactly (map, zone, marker,
 * pin), and of two rows of one type with one id the last served is read.
 */
UCLASS()
class ONLYWORLDS_API UOWMapSpace : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Every element of an opened world as the API serves it, one JSON text each, in the world's order: the owner's view. Empty with no world. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Map Space")
	static TArray<FString> ServedElements(const UOWWorld* World);

	/**
	 * The world's map space (the profile's mapSpace): each Map's frame, each Zone's outline per Map from its Markers by
	 * whole order (a Marker without one after every ordered one, ties by id), each Pin's point, and what was left aside,
	 * counted once by reason. Pure. The Unity SDK's OWMapSpace.Of; the core's MapSpaceOf.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Map Space")
	static bool MapSpaceOf(const TArray<FString>& Elements, FOWWorldMapSpace& MapSpace, FString& Reason);

	/** One Map's layout keys (the profile's layoutOf): its method, its ground, the Pins a new lay-out keeps. Pure. The Unity SDK's OWMapSpace.LayoutOf. */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Map Space")
	static bool LayoutOf(const TArray<FString>& Elements, const FString& MapId, FOWMapLayout& Layout, FString& Reason);

	/**
	 * Where a point on a Map lies on its parent and on each Map above it (the profile's carryOf): carried one level at a
	 * time by each Map's own x_parent_frame, in 64-bit floats; a point outside the Map is carried as written. Pure. The
	 * Unity SDK's OWMapSpace.CarryOf.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Map Space")
	static bool CarryOf(const TArray<FString>& Elements, const FString& MapId, FVector2D Point, FOWMapCarry& Carry, FString& Reason);

	/**
	 * Which Pins one reader knows, for a map drawn as one Character sees it: Pins (the rows of type pin are read, so a
	 * whole world's list may be given) against Served, the elements as that reader was served them, stubs included. A
	 * Pin is known when a served row has its element_type as type and its element_id as id, both as written. Nothing
	 * else is read, so the answer cannot show more than the reader was served. Pure. The Unity SDK's OWMapSpace.PinsKnownTo.
	 */
	UFUNCTION(BlueprintPure, Category = "OnlyWorlds|Map Space")
	static bool PinsKnownTo(const TArray<FString>& Pins, const TArray<FString>& Served, FOWPinsKnownTo& Known, FString& Reason);
};
