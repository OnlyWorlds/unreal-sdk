#include "OWLensDoor.h"

#include "OWConvert.h"
#include "OWOpenersLibrary.h"
#include "OWWorld.h"
#include "OWWorldObject.h"

namespace OWLensDoor
{
	const OnlyWorlds::OWWorld* NativeOf(const UOWWorld* World)
	{
		return World ? World->Native() : nullptr;
	}

	OnlyWorlds::OWJsonValue SnapshotOf(const OnlyWorlds::OWWorld& Native, const OnlyWorlds::OWJsonValue* Members, bool bOpen)
	{
		using OnlyWorlds::OWJsonValue;
		OWJsonValue Snapshot = OWJsonValue::Object();
		OWJsonValue Bodies = OWJsonValue::Array();
		for (const OnlyWorlds::OWElement& E : Native.elements) Bodies.Push(UOWOpeners::ServedElement(E));
		Snapshot.Set("elements", std::move(Bodies));
		// The world's now as the core reads a snapshot: now, else time_current (a folder's world.json), else
		// time_range_current (the API's world).
		if (Native.info.IsObject())
		{
			for (const char* Key : { "now", "time_current", "time_range_current" })
			{
				if (const OWJsonValue* V = Native.info.Find(Key)) Snapshot.Set(Key, *V);
			}
		}
		if (Members) Snapshot.Set("members", *Members);
		if (bOpen) Snapshot.Set("open", OWJsonValue::Bool(true));
		return Snapshot;
	}

	OnlyWorlds::OWJsonValue ReaderObjectOf(const FOWLensReader& Reader)
	{
		using OnlyWorlds::OWJsonValue;
		const char* Rung = "public";
		switch (Reader.Rung)
		{
		case EOWLensRung::Owner: Rung = "owner"; break;
		case EOWLensRung::CoBuilder: Rung = "co-builder"; break;
		case EOWLensRung::Contributor: Rung = "contributor"; break;
		case EOWLensRung::Guest: Rung = "guest"; break;
		case EOWLensRung::Public: Rung = "public"; break;
		}
		OWJsonValue Seen = OWJsonValue::Object();
		Seen.Set("rung", OWJsonValue::String(Rung));
		Seen.Set("character", Reader.Character.IsEmpty() ? OWJsonValue::Null() : OWJsonValue::String(OWConvert::Utf8(Reader.Character)));
		// A guest's whole is what it is served whole; a contributor's, the hidden elements it made.
		if (Reader.Rung == EOWLensRung::Guest || Reader.Rung == EOWLensRung::Contributor)
		{
			OWJsonValue WholeIds = OWJsonValue::Array();
			for (const FString& Id : Reader.Whole) WholeIds.Push(OWJsonValue::String(OWConvert::Utf8(Id)));
			Seen.Set("whole", std::move(WholeIds));
		}
		if (Reader.Rung == EOWLensRung::Guest)
		{
			OWJsonValue KnownIds = OWJsonValue::Array();
			for (const FString& Id : Reader.KnownOf) KnownIds.Push(OWJsonValue::String(OWConvert::Utf8(Id)));
			Seen.Set("known_of", std::move(KnownIds));
		}
		if (!Reader.bOwnerCounts || Reader.Rung == EOWLensRung::Owner) return Seen;
		OWJsonValue AsOwner = OWJsonValue::Object();
		AsOwner.Set("rung", OWJsonValue::String("owner"));
		AsOwner.Set("as", std::move(Seen));
		return AsOwner;
	}

	bool ReadText(const FString& Text, const TCHAR* Prefix, OnlyWorlds::OWJsonValue& Out, bool& bGiven, FString& Reason)
	{
		bGiven = false;
		if (Text.IsEmpty()) return true;
		const std::string Utf8Text = OWConvert::Utf8(Text);
		OnlyWorlds::OWJsonParseResult Parsed = OnlyWorlds::OWJson::Parse(std::string_view(Utf8Text));
		if (!Parsed.ok)
		{
			Reason = FString(Prefix) + OWConvert::Str(Parsed.error);
			return false;
		}
		Out = std::move(Parsed.value);
		bGiven = true;
		return true;
	}

	OnlyWorlds::OWJsonValue ServedById(const UOWWorld* World, const FString& Id)
	{
		OnlyWorlds::OWWorld* Native = World ? World->Native() : nullptr;
		const OnlyWorlds::OWElement* E = Native && !Id.IsEmpty() ? Native->Get(OWConvert::Utf8(Id)) : nullptr;
		return E ? UOWOpeners::ServedElement(*E) : OnlyWorlds::OWJsonValue();
	}

	FString TextOf(const OnlyWorlds::OWJsonValue& Value)
	{
		return OWConvert::Str(OnlyWorlds::OWJson::Stringify(Value, /*keep_number_text=*/true));
	}

	TArray<FString> IdsOf(const std::vector<std::string>& Ids)
	{
		TArray<FString> Out;
		for (const std::string& Id : Ids) Out.Add(OWConvert::Str(Id));
		return Out;
	}

	TArray<FOWLensCut> CutsOf(const std::vector<std::pair<std::string, int64_t>>& Counts)
	{
		TArray<FOWLensCut> Out;
		for (const std::pair<std::string, int64_t>& C : Counts)
		{
			FOWLensCut& Row = Out.AddDefaulted_GetRef();
			Row.Reason = OWConvert::Str(C.first);
			Row.Count = C.second;
		}
		return Out;
	}

	TArray<FString> StubsOf(const std::vector<std::string>& Kept, const FOWLensReader& Reader)
	{
		TArray<FString> Out;
		if (Reader.Rung != EOWLensRung::Guest) return Out;
		for (const std::string& Id : Kept)
		{
			const FString Kid = OWConvert::Str(Id);
			// Ids are compared exactly, as the core compares them (FString's own Contains ignores case).
			const bool bWhole = Reader.Whole.ContainsByPredicate([&Kid](const FString& W) { return W.Equals(Kid, ESearchCase::CaseSensitive); });
			if (!bWhole) Out.Add(Kid);
		}
		return Out;
	}
}
