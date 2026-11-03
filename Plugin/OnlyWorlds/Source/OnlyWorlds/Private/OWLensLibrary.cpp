#include "OWLensLibrary.h"

#include "OWConvert.h"
#include "OWJson.h"
#include "OWLens.h"
#include "OWLensDoor.h"
#include "OWOpenersLibrary.h"
#include "OWWorld.h"
#include "OWWorldObject.h"

#include <optional>
#include <string>
#include <vector>

namespace
{
	namespace LensCore = OnlyWorlds::OWLens;
	using OnlyWorlds::OWJsonValue;

	// The reading in Blueprint shapes.
	FOWLensReading LensReadingOf(const OnlyWorlds::OWLensReading& Got, const FOWLensReader& Reader)
	{
		FOWLensReading Out;
		Out.Kept = OWLensDoor::IdsOf(Got.kept);
		Out.KnownOf = OWLensDoor::StubsOf(Got.kept, Reader);
		for (const OWJsonValue& Row : Got.rows) Out.Rows.Add(OWLensDoor::TextOf(Row));
		Out.Cut = OWLensDoor::CutsOf(Got.cut);
		Out.NotesIgnored = Got.notes_ignored;
		if (Got.mail)
		{
			Out.bHasMail = true;
			Out.Mail.Unread = OWLensDoor::IdsOf(Got.mail->unread);
			Out.Mail.Read = OWLensDoor::IdsOf(Got.mail->read);
			for (const OWJsonValue& Row : Got.mail->rows) Out.Mail.Rows.Add(OWLensDoor::TextOf(Row));
			Out.Mail.Cut = OWLensDoor::CutsOf(Got.mail->cut);
			Out.Mail.TotalUnread = Got.mail->total_unread;
			Out.Mail.TotalRead = Got.mail->total_read;
		}
		Out.Unreadable = Got.unreadable ? OWConvert::Str(*Got.unreadable) : FString();
		return Out;
	}

	// One reading of a lens (an id, or a body), refused as the class comment says.
	bool LensRead(const UOWWorld* World, const OWJsonValue& Lens, const FOWLensReader& Reader, const FOWLensOptions& Options,
		FOWLensReading& Reading, FString& Reason)
	{
		const OnlyWorlds::OWWorld* Native = OWLensDoor::NativeOf(World);
		if (!Native)
		{
			Reason = TEXT("no world");
			return false;
		}
		OWJsonValue Members;
		bool bMembers = false;
		if (!OWLensDoor::ReadText(Options.MembersJson, TEXT("members: "), Members, bMembers, Reason)) return false;
		OnlyWorlds::OWLensOptions CoreOptions;
		if (Options.bHasAsOf) CoreOptions.as_of = Options.AsOf;
		if (!Options.Clock.IsEmpty()) CoreOptions.clock = OWConvert::Utf8(Options.Clock);
		const OnlyWorlds::OWLensReading Got = LensCore::Resolve(OWLensDoor::SnapshotOf(*Native, bMembers ? &Members : nullptr, false),
			Lens, OWLensDoor::ReaderObjectOf(Reader), CoreOptions);
		if (!Got.error.empty())
		{
			Reason = OWConvert::Str(Got.error);
			return false;
		}
		Reading = LensReadingOf(Got, Reader);
		return true;
	}
}

TArray<FString> UOWLens::GetLenses(const UOWWorld* World)
{
	TArray<FString> Out;
	const OnlyWorlds::OWWorld* Native = OWLensDoor::NativeOf(World);
	if (!Native) return Out;
	for (const OnlyWorlds::OWElement& E : Native->elements)
	{
		const OWJsonValue Served = UOWOpeners::ServedElement(E);
		const OWJsonValue* Id = Served.IsObject() ? Served.Find("id") : nullptr;
		if (LensCore::IsLens(Served) && Id && Id->IsString()) Out.Add(OWConvert::Str(Id->Text()));
	}
	return Out;
}

bool UOWLens::IsLens(const UOWWorld* World, const FString& ElementId)
{
	return LensCore::IsLens(OWLensDoor::ServedById(World, ElementId));
}

bool UOWLens::IsTemplate(const UOWWorld* World, const FString& ElementId)
{
	return LensCore::IsTemplate(OWLensDoor::ServedById(World, ElementId));
}

TArray<FString> UOWLens::LensesFor(const UOWWorld* World, const FString& CharacterId, bool bMembershipChose,
	const TArray<FString>& Membership)
{
	OWJsonValue Chosen = OWJsonValue::Array();
	for (const FString& Id : Membership) Chosen.Push(OWJsonValue::String(OWConvert::Utf8(Id)));
	const OWJsonValue Character = OWLensDoor::ServedById(World, CharacterId);
	const OWJsonValue* Uses = Character.IsObject() ? Character.Find(LensCore::UsesField) : nullptr;
	return OWLensDoor::IdsOf(LensCore::LensesInUse(bMembershipChose ? &Chosen : nullptr, Uses));
}

bool UOWLens::IsHidden(const UOWWorld* World, const FString& ElementId)
{
	return LensCore::IsHidden(OWLensDoor::ServedById(World, ElementId));
}

bool UOWLens::IsProposed(const UOWWorld* World, const FString& ElementId)
{
	return LensCore::IsProposed(OWLensDoor::ServedById(World, ElementId));
}

bool UOWLens::IsRoomElement(const UOWWorld* World, const FString& ElementId)
{
	return LensCore::IsRoomElement(OWLensDoor::ServedById(World, ElementId));
}

bool UOWLens::IsReadingAid(const UOWWorld* World, const FString& ElementId)
{
	return LensCore::IsReadingAid(OWLensDoor::ServedById(World, ElementId));
}

bool UOWLens::DateOf(const UOWWorld* World, const FString& ElementId, int64& Date)
{
	Date = 0;
	const std::optional<int64_t> Found = LensCore::DateOf(OWLensDoor::ServedById(World, ElementId));
	if (!Found) return false;
	Date = *Found;
	return true;
}

bool UOWLens::Resolve(const UOWWorld* World, const FString& LensId, const FOWLensReader& Reader, const FOWLensOptions& Options,
	FOWLensReading& Reading, FString& Reason)
{
	Reading = FOWLensReading();
	Reason.Reset();
	// The core reads a string as the id of a lens the reader is served whole; "" names none, and reads as nothing.
	return LensRead(World, OWJsonValue::String(OWConvert::Utf8(LensId)), Reader, Options, Reading, Reason);
}

bool UOWLens::ResolveJson(const UOWWorld* World, const FString& LensJson, const FOWLensReader& Reader,
	const FOWLensOptions& Options, FOWLensReading& Reading, FString& Reason)
{
	Reading = FOWLensReading();
	Reason.Reset();
	if (!OWLensDoor::NativeOf(World))
	{
		Reason = TEXT("no world");
		return false;
	}
	OWJsonValue Body;
	bool bBody = false;
	if (!OWLensDoor::ReadText(LensJson, TEXT("lens: "), Body, bBody, Reason)) return false;
	// "" is no body, and a value that is neither an object nor a string the core refuses as a caller's mistake; an
	// object that is no lens reads as nothing.
	return LensRead(World, bBody ? Body : OWJsonValue::Object(), Reader, Options, Reading, Reason);
}
