#include "OWLayoutLibrary.h"

#include "OWConvert.h"
#include "OWGraph.h"
#include "OWGraphDoor.h"
#include "OWJson.h"
#include "OWLayout.h"
#include "OWLensDoor.h"
#include "OWMapSpaceDoor.h"

#include <string>
#include <utility>

namespace
{
	namespace LayoutCore = OnlyWorlds::OWLayout;
	using OnlyWorlds::OWJsonValue;
	using OWConvert::Str;
	using OWConvert::Utf8;

	OWJsonValue PlacedOf(const TArray<FString>& Placed)
	{
		OWJsonValue Out = OWJsonValue::Array();
		for (const FString& Id : Placed) Out.Push(OWJsonValue::String(Utf8(Id)));
		return Out;
	}

	bool AnswerUnplaced(const OnlyWorlds::OWLayoutUnplaced& Got, TArray<FString>& Ids, FString& Reason)
	{
		if (!Got.error.empty())
		{
			Reason = Str(Got.error);
			return false;
		}
		Ids = OWLensDoor::IdsOf(Got.unplaced);
		return true;
	}
}

bool UOWLayout::LayoutByKind(const FOWWorldGraph& Graph, TArray<FOWLayoutPoint>& Points, FString& Reason)
{
	Points.Reset();
	Reason.Reset();
	const OnlyWorlds::OWLayoutByKind Got = LayoutCore::LayoutByKind(OWGraphDoor::CoreOf(Graph));
	if (!Got.error.empty())
	{
		Reason = Str(Got.error);
		return false;
	}
	for (const OnlyWorlds::OWLayoutPoint& P : Got.points)
	{
		FOWLayoutPoint& Row = Points.AddDefaulted_GetRef();
		Row.Id = Str(P.id);
		Row.Point = FVector2D(P.x, P.y);
	}
	return true;
}

bool UOWLayout::LayoutAround(const FOWWorldGraph& Graph, const FString& Focus, int32 Steps, TArray<FOWLayoutAroundPoint>& Points,
	FString& Reason)
{
	Points.Reset();
	Reason.Reset();
	const OnlyWorlds::OWLayoutAround Got = LayoutCore::LayoutAround(OWGraphDoor::CoreOf(Graph), Utf8(Focus), Steps);
	if (!Got.error.empty())
	{
		Reason = Str(Got.error);
		return false;
	}
	for (const OnlyWorlds::OWLayoutAroundPoint& P : Got.points)
	{
		FOWLayoutAroundPoint& Row = Points.AddDefaulted_GetRef();
		Row.Id = Str(P.id);
		Row.Depth = P.depth;
		Row.Point = FVector2D(P.x, P.y);
	}
	return true;
}

bool UOWLayout::LayoutByLinks3d(const FOWWorldGraph& Graph, const TArray<FString>& Elements, const TArray<FOWLayout3dPoint>& Held,
	bool bHasHeldUnit, double HeldUnit, FOWLayoutByLinks3d& Layout, FString& Reason)
{
	Layout = FOWLayoutByLinks3d();
	Reason.Reset();
	OWJsonValue Rows;
	if (!OWMapSpaceDoor::RowsOf(Elements, TEXT("elements"), Rows, Reason)) return false;
	// The held points as the core takes them, element id to [x, y, z]; of two rows with one id the last is read.
	OWJsonValue HeldPoints = OWJsonValue::Object();
	for (const FOWLayout3dPoint& H : Held)
	{
		OWJsonValue At = OWJsonValue::Array();
		At.Push(OWJsonValue::Number(H.Point.X));
		At.Push(OWJsonValue::Number(H.Point.Y));
		At.Push(OWJsonValue::Number(H.Point.Z));
		HeldPoints.Set(Utf8(H.Id), std::move(At));
	}
	const OWJsonValue Unit = OWJsonValue::Number(HeldUnit);
	const OnlyWorlds::OWLayoutByLinks3d Got = LayoutCore::LayoutByLinks3d(OWGraphDoor::CoreOf(Graph),
		Elements.Num() ? &Rows : nullptr, Held.Num() ? &HeldPoints : nullptr, bHasHeldUnit ? &Unit : nullptr);
	// A refusal of the page's by its word; a graph it cannot read by the core's sentence.
	if (!Got.code.empty() || !Got.error.empty())
	{
		Reason = Str(Got.code.empty() ? Got.error : Got.code);
		return false;
	}
	Layout.Version = Got.version;
	for (const OnlyWorlds::OWLayout3dPoint& P : Got.points)
	{
		FOWLayout3dPoint& Row = Layout.Points.AddDefaulted_GetRef();
		Row.Id = Str(P.id);
		Row.Point = FVector(P.x, P.y, P.z);
		Row.How = Str(P.how);
		Row.Group = P.group ? Str(*P.group) : FString();
		Row.Part = P.part ? Str(*P.part) : FString();
		Row.bHeld = P.held;
	}
	Layout.Centres = OWLensDoor::IdsOf(Got.centres);
	Layout.Unplaced = OWLensDoor::IdsOf(Got.unplaced);
	return true;
}

bool UOWLayout::UnplacedByKind(const FOWWorldGraph& Graph, const TArray<FString>& Placed, TArray<FString>& Unplaced, FString& Reason)
{
	Unplaced.Reset();
	Reason.Reset();
	return AnswerUnplaced(LayoutCore::UnplacedByKind(OWGraphDoor::CoreOf(Graph), PlacedOf(Placed)), Unplaced, Reason);
}

bool UOWLayout::UnplacedAround(const FOWWorldGraph& Graph, const FString& Focus, const TArray<FString>& Placed, int32 Steps,
	TArray<FString>& Unplaced, FString& Reason)
{
	Unplaced.Reset();
	Reason.Reset();
	return AnswerUnplaced(LayoutCore::UnplacedAround(OWGraphDoor::CoreOf(Graph), Utf8(Focus), PlacedOf(Placed), Steps), Unplaced, Reason);
}

bool UOWLayout::UnplacedByLinks3d(const FOWWorldGraph& Graph, const TArray<FString>& Placed, TArray<FString>& Unplaced, FString& Reason)
{
	Unplaced.Reset();
	Reason.Reset();
	return AnswerUnplaced(LayoutCore::UnplacedByLinks3d(OWGraphDoor::CoreOf(Graph), PlacedOf(Placed)), Unplaced, Reason);
}
