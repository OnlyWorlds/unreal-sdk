#include "OWGraphDoor.h"

#include "OWConvert.h"
#include "OWGraphLibrary.h"
#include "OWJson.h"

#include <utility>

namespace OWGraphDoor
{
	OnlyWorlds::OWWorldGraph CoreOf(const FOWWorldGraph& Graph)
	{
		OnlyWorlds::OWWorldGraph Out;
		for (const FOWGraphNode& N : Graph.Nodes)
		{
			OnlyWorlds::OWGraphNode Node;
			Node.id = OWConvert::Utf8(N.Id);
			Node.type = OWConvert::Utf8(N.Type);
			Node.name = N.bHasName ? OnlyWorlds::OWJsonValue::String(OWConvert::Utf8(N.Name)) : OnlyWorlds::OWJsonValue::Null();
			Out.nodes.push_back(std::move(Node));
		}
		for (const FOWGraphEdge& E : Graph.Edges)
		{
			Out.edges.push_back({ OWConvert::Utf8(E.Source), OWConvert::Utf8(E.Field), OWConvert::Utf8(E.Target), E.Index });
		}
		return Out;
	}
}
