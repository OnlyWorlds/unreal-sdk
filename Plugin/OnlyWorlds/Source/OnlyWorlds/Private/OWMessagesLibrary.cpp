#include "OWMessagesLibrary.h"

#include "OWConvert.h"
#include "OWJson.h"
#include "OWMessage.h"
#include "OWOpenersLibrary.h"
#include "OWWorld.h"
#include "OWWorldObject.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

using OWConvert::Str;
using OWConvert::Utf8;

namespace
{
	using OnlyWorlds::OWElement;
	using OnlyWorlds::OWJsonValue;
	namespace Msg = OnlyWorlds::OWMessage;

	// The type an element is served with, folded as the pair is (the body's own when it names one, anything but a
	// string being no type; else its folder's), read without copying the body.
	bool ServedAs(const OWElement& E, const char* Type)
	{
		if (!E.body.IsObject()) return false;
		const OWJsonValue* T = E.body.Find("type");
		if (T && !T->IsString()) return false;
		return Msg::Fold(T ? T->Text() : E.Type()) == Type;
	}

	// The world's elements of one type ("narrative", "relation") as the API serves them (UOWOpeners::ServedElement: a
	// body that names no type takes its folder's), in the world's order. List points into Bodies.
	struct FServed
	{
		std::vector<OWJsonValue> Bodies;
		std::vector<const OWJsonValue*> List;
	};

	void Serve(const OnlyWorlds::OWWorld& World, const char* Type, FServed& Out)
	{
		for (const OWElement& E : World.elements)
		{
			if (ServedAs(E, Type)) Out.Bodies.push_back(UOWOpeners::ServedElement(E));
		}
		for (const OWJsonValue& B : Out.Bodies) Out.List.push_back(&B);
	}

	// The messages, the Notes, or both, of a list of narratives. The core's list readers read every body they are given
	// (UnreadFor reads any but a Note as a message), so a door hands them only the pair they read.
	std::vector<const OWJsonValue*> Keep(const std::vector<const OWJsonValue*>& List, bool bMessages, bool bNotes)
	{
		std::vector<const OWJsonValue*> Out;
		for (const OWJsonValue* B : List)
		{
			if ((bMessages && Msg::IsMessage(*B)) || (bNotes && Msg::IsNote(*B))) Out.push_back(B);
		}
		return Out;
	}

	// A body's id ("" when it has none that is a string: every element of a world read from a folder has one).
	FString IdOf(const OWJsonValue* Body)
	{
		const OWJsonValue* Id = Body ? Body->Find("id") : nullptr;
		return Id && Id->IsString() ? Str(Id->Text()) : FString();
	}

	TArray<FString> IdsOf(const std::vector<const OWJsonValue*>& Bodies)
	{
		TArray<FString> Out;
		for (const OWJsonValue* B : Bodies)
		{
			const OWJsonValue* Id = B->Find("id");
			if (Id && Id->IsString()) Out.Add(Str(Id->Text()));
		}
		return Out;
	}

	// One element as served, or a null body for an id the world does not hold (no body: the profile's no).
	OWJsonValue One(const UOWWorld* World, const FString& Id)
	{
		OnlyWorlds::OWWorld* Native = World ? World->Native() : nullptr;
		const OWElement* E = Native && !Id.IsEmpty() ? Native->Get(Utf8(Id)) : nullptr;
		return E ? UOWOpeners::ServedElement(*E) : OWJsonValue();
	}

	// The world a refusable call reads, or nullptr and the reason.
	const OnlyWorlds::OWWorld* WorldOf(const UOWWorld* World, FString& Reason)
	{
		const OnlyWorlds::OWWorld* Native = World ? World->Native() : nullptr;
		if (!Native) Reason = TEXT("no world");
		return Native;
	}

	// The element a refusable call reads, as served, or false and the reason.
	bool Element(const UOWWorld* World, const FString& Id, OWJsonValue& Out, FString& Reason)
	{
		OnlyWorlds::OWWorld* Native = World ? World->Native() : nullptr;
		if (!Native)
		{
			Reason = TEXT("no world");
			return false;
		}
		const OWElement* E = Native->Get(Utf8(Id));
		if (!E)
		{
			Reason = FString::Printf(TEXT("no element %s in this world"), *Id);
			return false;
		}
		Out = UOWOpeners::ServedElement(*E);
		return true;
	}

	// The members list as a Blueprint hands it over, JSON text: "" is no list (Members nullptr, the core's no list);
	// text that does not parse is refused; any other value goes to the core, which reads one that is not an array as
	// no list.
	struct FMembers
	{
		OWJsonValue Value;
		bool bHas = false;
		const OWJsonValue* Get() const { return bHas ? &Value : nullptr; }
	};

	bool ReadMembers(const FString& Json, FMembers& Out, FString& Reason)
	{
		if (Json.IsEmpty()) return true;
		const std::string Text = Utf8(Json);
		OnlyWorlds::OWJsonParseResult R = OnlyWorlds::OWJson::Parse(std::string_view(Text));
		if (!R.ok)
		{
			Reason = TEXT("members: ") + Str(R.error);
			return false;
		}
		Out.Value = std::move(R.value);
		Out.bHas = true;
		return true;
	}

	EOWSenderStatus StatusOf(const std::string& Word)
	{
		if (Word == Msg::Verified) return EOWSenderStatus::Verified;
		if (Word == Msg::Mismatch) return EOWSenderStatus::Mismatch;
		if (Word == Msg::Unseen) return EOWSenderStatus::Unseen;
		return EOWSenderStatus::Unverifiable;
	}
}

TArray<FString> UOWMessages::GetMessages(const UOWWorld* World)
{
	const OnlyWorlds::OWWorld* Native = World ? World->Native() : nullptr;
	if (!Native) return TArray<FString>();
	FServed Narratives;
	Serve(*Native, Msg::Type, Narratives);
	return IdsOf(Msg::InCreatedOrder(Keep(Narratives.List, true, false)));
}

TArray<FString> UOWMessages::GetNotes(const UOWWorld* World)
{
	const OnlyWorlds::OWWorld* Native = World ? World->Native() : nullptr;
	if (!Native) return TArray<FString>();
	FServed Narratives;
	Serve(*Native, Msg::Type, Narratives);
	return IdsOf(Msg::InCreatedOrder(Keep(Narratives.List, false, true)));
}

bool UOWMessages::IsMessage(const UOWWorld* World, const FString& ElementId)
{
	return Msg::IsMessage(One(World, ElementId));
}

bool UOWMessages::IsNote(const UOWWorld* World, const FString& ElementId)
{
	return Msg::IsNote(One(World, ElementId));
}

TArray<FString> UOWMessages::RecipientsOf(const UOWWorld* World, const FString& MessageId)
{
	TArray<FString> Out;
	for (const std::string& Id : Msg::RecipientsOf(One(World, MessageId))) Out.Add(Str(Id));
	return Out;
}

bool UOWMessages::IsAddressedTo(const UOWWorld* World, const FString& MessageId, const FString& CharacterId)
{
	const std::string Character = Utf8(CharacterId);
	return Msg::IsAddressedTo(One(World, MessageId), Character);
}

bool UOWMessages::IsToEveryone(const UOWWorld* World, const FString& MessageId, bool bKeyless)
{
	return Msg::IsToEveryone(One(World, MessageId), bKeyless);
}

EOWUnseenRecipients UOWMessages::HasUnseenRecipients(const UOWWorld* World, const FString& MessageId, bool bKeyless)
{
	const std::optional<bool> Unseen = Msg::HasUnseenRecipients(One(World, MessageId), bKeyless);
	if (!Unseen) return EOWUnseenRecipients::Unknown;
	return *Unseen ? EOWUnseenRecipients::Yes : EOWUnseenRecipients::No;
}

bool UOWMessages::IsSentBy(const UOWWorld* World, const FString& MessageId, const FString& CharacterId)
{
	const std::string Character = Utf8(CharacterId);
	return Msg::IsSentBy(One(World, MessageId), Character);
}

bool UOWMessages::SenderStatus(const UOWWorld* World, const FString& MessageId, const FString& MembersJson, FOWSender& Sender,
	FString& Reason)
{
	Sender = FOWSender();
	Reason.Reset();
	OWJsonValue Message;
	FMembers Members;
	if (!Element(World, MessageId, Message, Reason) || !ReadMembers(MembersJson, Members, Reason)) return false;
	const OnlyWorlds::OWSender S = Msg::SenderStatus(Message, Members.Get());
	Sender.Status = StatusOf(S.status);
	Sender.Claimed = S.claimed ? Str(*S.claimed) : FString();
	Sender.WrittenBy = S.written_by ? Str(*S.written_by) : FString();
	return true;
}

bool UOWMessages::EditedBy(const UOWWorld* World, const FString& MessageId, const FString& MembersJson, FOWEdited& Edited,
	FString& Reason)
{
	Edited = FOWEdited();
	Reason.Reset();
	OWJsonValue Message;
	FMembers Members;
	if (!Element(World, MessageId, Message, Reason) || !ReadMembers(MembersJson, Members, Reason)) return false;
	const OnlyWorlds::OWEdited E = Msg::EditedBy(Message, Members.Get());
	Edited.bEdited = E.edited;
	Edited.By = E.by ? Str(*E.by) : FString();
	return true;
}

bool UOWMessages::ReadMarkOf(const UOWWorld* World, const FString& CharacterId, const FString& MembersJson, bool bKeyless,
	FString& MarkId, FString& Reason)
{
	MarkId.Reset();
	Reason.Reset();
	const OnlyWorlds::OWWorld* Native = WorldOf(World, Reason);
	FMembers Members;
	if (!Native || !ReadMembers(MembersJson, Members, Reason)) return false;
	FServed Relations;
	Serve(*Native, Msg::ReadMarkType, Relations);
	const std::string Character = Utf8(CharacterId);
	MarkId = IdOf(Msg::ReadMarkOf(Relations.List, Character, Members.Get(), bKeyless));
	return true;
}

bool UOWMessages::UnreadFor(const UOWWorld* World, const FString& CharacterId, const FString& ReadMarkId,
	const FString& MembersJson, TArray<FString>& Unread, FString& Reason)
{
	Unread.Reset();
	Reason.Reset();
	const OnlyWorlds::OWWorld* Native = WorldOf(World, Reason);
	if (!Native) return false;
	// The read mark by its id, whatever it is: the core reads the narratives of the body it is given.
	OWJsonValue Mark;
	const bool bMark = !ReadMarkId.IsEmpty();
	if (bMark && !Element(World, ReadMarkId, Mark, Reason)) return false;
	FMembers Members;
	if (!ReadMembers(MembersJson, Members, Reason)) return false;
	FServed Narratives;
	Serve(*Native, Msg::Type, Narratives);
	const std::string Character = Utf8(CharacterId);
	Unread = IdsOf(Msg::UnreadFor(Keep(Narratives.List, true, false), Character, bMark ? &Mark : nullptr, Members.Get()));
	return true;
}

TArray<FString> UOWMessages::ThreadOf(const UOWWorld* World, const FString& RootId)
{
	const OnlyWorlds::OWWorld* Native = World ? World->Native() : nullptr;
	if (!Native) return TArray<FString>();
	FServed Narratives;
	Serve(*Native, Msg::Type, Narratives);
	// The Notes too: a chain walks through one, and the core never lists it.
	const std::string Root = Utf8(RootId);
	return IdsOf(Msg::ThreadOf(Keep(Narratives.List, true, true), Root));
}

bool UOWMessages::NoteOf(const UOWWorld* World, const FString& CharacterId, const FString& MembersJson, FString& NoteId,
	FString& Reason)
{
	NoteId.Reset();
	Reason.Reset();
	const OnlyWorlds::OWWorld* Native = WorldOf(World, Reason);
	FMembers Members;
	if (!Native || !ReadMembers(MembersJson, Members, Reason)) return false;
	FServed Narratives;
	Serve(*Native, Msg::Type, Narratives);
	const std::string Character = Utf8(CharacterId);
	const std::optional<std::string> Line = Msg::NoteOf(Keep(Narratives.List, false, true), Character, Members.Get());
	NoteId = Line ? Str(*Line) : FString();
	return true;
}
