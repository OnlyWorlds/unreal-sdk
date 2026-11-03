#include "OWGraphLibrary.h"

#include "OWConvert.h"
#include "OWGraph.h"
#include "OWJson.h"
#include "OWLensDoor.h"
#include "OWWorld.h"
#include "OWWorldObject.h"

#include <string>
#include <utility>
#include <vector>

namespace
{
	namespace GraphCore = OnlyWorlds::OWGraph;
	using OnlyWorlds::OWJsonValue;

	FOWWorldGraph GraphOfCore(const OnlyWorlds::OWWorldGraph& Got)
	{
		FOWWorldGraph Out;
		for (const OnlyWorlds::OWGraphNode& N : Got.nodes)
		{
			FOWGraphNode& Node = Out.Nodes.AddDefaulted_GetRef();
			Node.Id = OWConvert::Str(N.id);
			Node.Type = OWConvert::Str(N.type);
			Node.bHasName = N.name.IsString();
			Node.Name = Node.bHasName ? OWConvert::Str(N.name.Text()) : FString();
			Node.Family = OWConvert::Str(N.family);
			Node.Icon = OWConvert::Str(N.icon);
			Node.Marks = OWLensDoor::IdsOf(N.marks);
			Node.bKnownOf = N.known_of;
		}
		for (const OnlyWorlds::OWGraphEdge& E : Got.edges)
		{
			FOWGraphEdge& Edge = Out.Edges.AddDefaulted_GetRef();
			Edge.Source = OWConvert::Str(E.source);
			Edge.Field = OWConvert::Str(E.field);
			Edge.Target = OWConvert::Str(E.target);
			Edge.Index = E.index;
		}
		Out.EdgesCut = OWLensDoor::CutsOf(Got.edges_cut);
		Out.Cut = OWLensDoor::CutsOf(Got.cut);
		Out.Unreadable = Got.unreadable ? OWConvert::Str(*Got.unreadable) : FString();
		return Out;
	}

	// The keys the two counts read, back in the core's shape: each node's id, each edge whole.
	OnlyWorlds::OWWorldGraph CoreOfGraph(const FOWWorldGraph& Given)
	{
		OnlyWorlds::OWWorldGraph Out;
		for (const FOWGraphNode& N : Given.Nodes)
		{
			OnlyWorlds::OWGraphNode Node;
			Node.id = OWConvert::Utf8(N.Id);
			Node.type = OWConvert::Utf8(N.Type);
			Out.nodes.push_back(std::move(Node));
		}
		for (const FOWGraphEdge& E : Given.Edges)
		{
			Out.edges.push_back({ OWConvert::Utf8(E.Source), OWConvert::Utf8(E.Field), OWConvert::Utf8(E.Target), E.Index });
		}
		return Out;
	}

	TArray<FOWGraphDegree> DegreesOf(const std::vector<std::pair<std::string, int64_t>>& Counts)
	{
		TArray<FOWGraphDegree> Out;
		for (const std::pair<std::string, int64_t>& C : Counts)
		{
			FOWGraphDegree& Row = Out.AddDefaulted_GetRef();
			Row.Id = OWConvert::Str(C.first);
			Row.Degree = C.second;
		}
		return Out;
	}
}

bool UOWGraph::Read(const UOWWorld* World, const FOWLensReader& Reader, const FOWGraphOptions& Options, FOWWorldGraph& Graph,
	FString& Reason)
{
	Graph = FOWWorldGraph();
	Reason.Reset();
	const OnlyWorlds::OWWorld* Native = OWLensDoor::NativeOf(World);
	if (!Native)
	{
		Reason = TEXT("no world");
		return false;
	}
	OnlyWorlds::OWGraphOptions CoreOptions;
	if (!Options.LensId.IsEmpty() && !Options.LensJson.IsEmpty())
	{
		Reason = TEXT("lens: an id or a body, not both");
		return false;
	}
	if (!Options.LensId.IsEmpty()) CoreOptions.lens = OWJsonValue::String(OWConvert::Utf8(Options.LensId));
	bool bBody = false;
	if (!OWLensDoor::ReadText(Options.LensJson, TEXT("lens: "), CoreOptions.lens, bBody, Reason)) return false;
	OWJsonValue Members;
	bool bMembers = false;
	if (!OWLensDoor::ReadText(Options.MembersJson, TEXT("members: "), Members, bMembers, Reason)) return false;
	if (Options.bHasAsOf) CoreOptions.as_of = Options.AsOf;
	if (!Options.Clock.IsEmpty()) CoreOptions.clock = OWConvert::Utf8(Options.Clock);
	CoreOptions.rooms = Options.bRooms;
	CoreOptions.hidden = Options.bHidden;
	CoreOptions.proposed = Options.bProposed;
	CoreOptions.map_space = Options.bMapSpace;
	CoreOptions.reading_aids = Options.bReadingAids;
	const OnlyWorlds::OWWorldGraph Got = GraphCore::WorldGraph(
		OWLensDoor::SnapshotOf(*Native, bMembers ? &Members : nullptr, false), OWLensDoor::ReaderObjectOf(Reader), CoreOptions);
	// A flag from the wrong rung is refused by its word, before anything is read; a caller's mistake by the core's sentence.
	if (!Got.refused.empty())
	{
		Reason = OWConvert::Str(Got.refused);
		return false;
	}
	if (!Got.error.empty())
	{
		Reason = OWConvert::Str(Got.error);
		return false;
	}
	Graph = GraphOfCore(Got);
	return true;
}

TArray<FOWGraphDegree> UOWGraph::DegreeOf(const FOWWorldGraph& Graph)
{
	return DegreesOf(GraphCore::DegreeOf(CoreOfGraph(Graph)));
}

TArray<FOWGraphDegree> UOWGraph::OutDegreeOf(const FOWWorldGraph& Graph)
{
	return DegreesOf(GraphCore::OutDegreeOf(CoreOfGraph(Graph)));
}
