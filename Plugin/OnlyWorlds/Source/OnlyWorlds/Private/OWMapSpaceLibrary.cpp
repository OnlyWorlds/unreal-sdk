#include "OWMapSpaceLibrary.h"

#include "OWConvert.h"
#include "OWJson.h"
#include "OWLensDoor.h"
#include "OWMapSpace.h"
#include "OWMapSpaceDoor.h"
#include "OWOpenersLibrary.h"
#include "OWWorld.h"
#include "OWWorldObject.h"

#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace
{
	namespace MapCore = OnlyWorlds::OWMapSpace;
	using OnlyWorlds::OWJsonValue;
	using OWConvert::Str;
	using OWConvert::Utf8;

	FString TextOrEmpty(const std::optional<std::string>& V) { return V ? Str(*V) : FString(); }

	TArray<FOWLensCut> Counted(std::initializer_list<std::pair<const char*, int64_t>> Counts)
	{
		TArray<FOWLensCut> Out;
		for (const std::pair<const char*, int64_t>& C : Counts)
		{
			if (C.second <= 0) continue;
			FOWLensCut& Row = Out.AddDefaulted_GetRef();
			Row.Reason = Str(C.first);
			Row.Count = C.second;
		}
		return Out;
	}

	// The type a row is read by, as the core reads it: its `type` when a string, else none.
	bool IsOfType(const OWJsonValue& Row, std::string_view Type)
	{
		const OWJsonValue* T = Row.IsObject() ? Row.Find("type") : nullptr;
		return T && T->IsString() && T->Text() == Type;
	}

	// The last Map row with this id, as the core reads two rows of one id (the last served), or nullptr.
	const OWJsonValue* MapRowOf(const OWJsonValue& Rows, const std::string& Id)
	{
		const OWJsonValue* Found = nullptr;
		for (const OWJsonValue& Row : Rows.Items())
		{
			if (!IsOfType(Row, "map")) continue;
			const OWJsonValue* RowId = Row.Find("id");
			if (RowId && RowId->IsString() && !Id.empty() && RowId->Text() == Id) Found = &Row;
		}
		return Found;
	}

	// The rows of one type, in the order served.
	OWJsonValue RowsOfType(const OWJsonValue& Rows, std::string_view Type)
	{
		OWJsonValue Out = OWJsonValue::Array();
		for (const OWJsonValue& Row : Rows.Items())
		{
			if (IsOfType(Row, Type)) Out.Push(Row);
		}
		return Out;
	}
}

namespace OWMapSpaceDoor
{
	bool RowsOf(const TArray<FString>& Texts, const TCHAR* Name, OWJsonValue& Rows, FString& Reason)
	{
		Rows = OWJsonValue::Array();
		for (int32 I = 0; I < Texts.Num(); ++I)
		{
			const std::string Text = Utf8(Texts[I]);
			OnlyWorlds::OWJsonParseResult Parsed = OnlyWorlds::OWJson::Parse(std::string_view(Text));
			if (!Parsed.ok)
			{
				Reason = FString::Printf(TEXT("%s[%d]: "), Name, I) + Str(Parsed.error);
				return false;
			}
			Rows.Push(std::move(Parsed.value));
		}
		return true;
	}

	FOWWorldMapSpace MapSpaceOfCore(const OnlyWorlds::OWWorldMapSpace& Got)
	{
		FOWWorldMapSpace Out;
		for (const OnlyWorlds::OWMapSpaceMap& M : Got.maps)
		{
			FOWMapSpaceMap& Row = Out.Maps.AddDefaulted_GetRef();
			Row.Id = Str(M.id);
			Row.bHasWidth = M.width.has_value();
			Row.Width = M.width.value_or(0);
			Row.bHasHeight = M.height.has_value();
			Row.Height = M.height.value_or(0);
			Row.bHasDepth = M.depth.has_value();
			Row.Depth = M.depth.value_or(0);
			Row.bHasZMax = M.z_max.has_value();
			Row.ZMax = M.z_max.value_or(0);
			Row.bHasScale = M.scale.IsNumber();
			Row.Scale = Row.bHasScale ? M.scale.AsDouble() : 0.0;
			Row.Location = TextOrEmpty(M.location);
			Row.ParentMap = TextOrEmpty(M.parent_map);
			Row.bMeasured = M.measured;
		}
		for (const OnlyWorlds::OWMapSpaceOutline& O : Got.zones)
		{
			FOWMapSpaceOutline& Row = Out.Zones.AddDefaulted_GetRef();
			Row.Zone = Str(O.zone);
			Row.Map = Str(O.map);
			Row.Shape = Str(O.shape);
			for (const OnlyWorlds::OWMapSpacePoint& P : O.points)
			{
				FOWMapSpacePoint& Point = Row.Points.AddDefaulted_GetRef();
				Point.X = P.x;
				Point.Y = P.y;
				Point.Z = P.z;
			}
		}
		for (const OnlyWorlds::OWMapSpacePin& P : Got.pins)
		{
			FOWMapSpacePin& Row = Out.Pins.AddDefaulted_GetRef();
			Row.Pin = Str(P.pin);
			Row.Map = Str(P.map);
			Row.bHasElement = P.element_type.has_value() && P.element_id.has_value();
			Row.ElementType = Row.bHasElement ? Str(*P.element_type) : FString();
			Row.ElementId = Row.bHasElement ? Str(*P.element_id) : FString();
			Row.X = P.point.x;
			Row.Y = P.point.y;
			Row.Z = P.point.z;
		}
		// In OutReasons()'s order: no_point, no_markers, dangling, no_map.
		const std::vector<std::string>& Reasons = MapCore::OutReasons();
		Out.Out = Counted({ { Reasons[0].c_str(), Got.out.no_point }, { Reasons[1].c_str(), Got.out.no_markers },
			{ Reasons[2].c_str(), Got.out.dangling }, { Reasons[3].c_str(), Got.out.no_map } });
		return Out;
	}

	FOWMapLayout LayoutOfCore(const OnlyWorlds::OWMapLayout& Got)
	{
		FOWMapLayout Out;
		Out.bHasLayout = Got.layout.has_value();
		Out.Layout = TextOrEmpty(Got.layout);
		Out.Ground = TextOrEmpty(Got.ground);
		Out.Hand = OWLensDoor::IdsOf(Got.hand);
		return Out;
	}

	FOWMapCarry CarryOfCore(const OnlyWorlds::OWMapCarry& Got)
	{
		FOWMapCarry Out;
		Out.bHasParentFrame = Got.parent_frame.has_value();
		if (Got.parent_frame)
		{
			Out.ParentFrameMin = FVector2D((*Got.parent_frame)[0], (*Got.parent_frame)[1]);
			Out.ParentFrameMax = FVector2D((*Got.parent_frame)[2], (*Got.parent_frame)[3]);
		}
		Out.Lint = TextOrEmpty(Got.lint);
		for (const OnlyWorlds::OWMapCarryPoint& P : Got.points)
		{
			FOWMapCarryPoint& Row = Out.Points.AddDefaulted_GetRef();
			Row.Map = Str(P.map);
			Row.Point = FVector2D(P.x, P.y);
		}
		Out.Stop = TextOrEmpty(Got.stop);
		return Out;
	}

	FOWPinsKnownTo PinsKnownToOfCore(const OnlyWorlds::OWPinsKnownTo& Got)
	{
		FOWPinsKnownTo Out;
		for (const OnlyWorlds::OWPinKnown& K : Got.known)
		{
			FOWPinKnown& Row = Out.Known.AddDefaulted_GetRef();
			Row.Pin = Str(K.pin);
			Row.Map = TextOrEmpty(K.map);
			Row.ElementType = Str(K.element_type);
			Row.ElementId = Str(K.element_id);
			Row.How = Str(K.how);
		}
		Out.Unknown = Got.unknown;
		Out.Out = Counted({ { "no_link", Got.out.no_link } });
		return Out;
	}
}

TArray<FString> UOWMapSpace::ServedElements(const UOWWorld* World)
{
	TArray<FString> Out;
	const OnlyWorlds::OWWorld* Native = OWLensDoor::NativeOf(World);
	if (!Native) return Out;
	for (const OnlyWorlds::OWElement& E : Native->elements) Out.Add(OWLensDoor::TextOf(UOWOpeners::ServedElement(E)));
	return Out;
}

bool UOWMapSpace::MapSpaceOf(const TArray<FString>& Elements, FOWWorldMapSpace& MapSpace, FString& Reason)
{
	MapSpace = FOWWorldMapSpace();
	Reason.Reset();
	OWJsonValue Rows;
	if (!OWMapSpaceDoor::RowsOf(Elements, TEXT("elements"), Rows, Reason)) return false;
	const OnlyWorlds::OWWorldMapSpace Got = MapCore::MapSpaceOf(Rows);
	if (!Got.error.empty())
	{
		Reason = Str(Got.error);
		return false;
	}
	MapSpace = OWMapSpaceDoor::MapSpaceOfCore(Got);
	return true;
}

bool UOWMapSpace::LayoutOf(const TArray<FString>& Elements, const FString& MapId, FOWMapLayout& Layout, FString& Reason)
{
	Layout = FOWMapLayout();
	Reason.Reset();
	OWJsonValue Rows;
	if (!OWMapSpaceDoor::RowsOf(Elements, TEXT("elements"), Rows, Reason)) return false;
	const OWJsonValue* Map = MapRowOf(Rows, Utf8(MapId));
	if (!Map)
	{
		Reason = TEXT("no map ") + MapId;
		return false;
	}
	const OnlyWorlds::OWMapLayout Got = MapCore::LayoutOf(*Map, RowsOfType(Rows, "map"), RowsOfType(Rows, "pin"));
	if (!Got.error.empty())
	{
		Reason = Str(Got.error);
		return false;
	}
	Layout = OWMapSpaceDoor::LayoutOfCore(Got);
	return true;
}

bool UOWMapSpace::CarryOf(const TArray<FString>& Elements, const FString& MapId, FVector2D Point, FOWMapCarry& Carry, FString& Reason)
{
	Carry = FOWMapCarry();
	Reason.Reset();
	OWJsonValue Rows;
	if (!OWMapSpaceDoor::RowsOf(Elements, TEXT("elements"), Rows, Reason)) return false;
	const OWJsonValue* Map = MapRowOf(Rows, Utf8(MapId));
	if (!Map)
	{
		Reason = TEXT("no map ") + MapId;
		return false;
	}
	const OnlyWorlds::OWMapCarry Got = MapCore::CarryOf(*Map, Point.X, Point.Y, Rows);
	if (!Got.error.empty())
	{
		Reason = Str(Got.error);
		return false;
	}
	Carry = OWMapSpaceDoor::CarryOfCore(Got);
	return true;
}

bool UOWMapSpace::PinsKnownTo(const TArray<FString>& Pins, const TArray<FString>& Served, FOWPinsKnownTo& Known, FString& Reason)
{
	Known = FOWPinsKnownTo();
	Reason.Reset();
	OWJsonValue PinRows, ServedRows;
	if (!OWMapSpaceDoor::RowsOf(Pins, TEXT("pins"), PinRows, Reason)) return false;
	if (!OWMapSpaceDoor::RowsOf(Served, TEXT("served"), ServedRows, Reason)) return false;
	const OnlyWorlds::OWPinsKnownTo Got = MapCore::PinsKnownTo(PinRows, ServedRows);
	if (!Got.error.empty())
	{
		Reason = Str(Got.error);
		return false;
	}
	Known = OWMapSpaceDoor::PinsKnownToOfCore(Got);
	return true;
}
