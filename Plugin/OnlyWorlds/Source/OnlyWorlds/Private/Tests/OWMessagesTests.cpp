// The message profile through UOWMessages as a Blueprint calls it (OWBlueprintCall.h): each function found by name,
// its parameters set by property, ProcessEvent, the answer read back by property (an enum by its number, a struct's
// members as a Break node reads them, an array's rows). On a world built here in memory (a folder's bodies, two without
// a type, which read as their folder's), then every case of the profile's fixture and of the readings beside it
// (tests/data/message-beside-the-fixture.json), each case placed in a world of its own and asked by id, then every
// element of Sikelia against the core.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#include "OWConvert.h"
#include "OWJson.h"
#include "OWMessage.h"
#include "OWMessagesLibrary.h"
#include "OWWorld.h"
#include "OWWorldObject.h"
#include "OWBlueprintCall.h"
#include "OWTestPaths.h"

#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using OnlyWorlds::OWJsonValue;
	using OWBlueprintTest::FCall;
	using OWBlueprintTest::Member;
	using OWConvert::Str;
	using OWConvert::Utf8;

	constexpr EAutomationTestFlags OWTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

	OWJsonValue J(const char* Text)
	{
		OnlyWorlds::OWJsonParseResult R = OnlyWorlds::OWJson::Parse(std::string_view(Text));
		return R.ok ? R.value : OWJsonValue();
	}

	FString TextOf(const OWJsonValue* V) { return V && V->IsString() ? Str(V->Text()) : FString(); }

	bool BoolOf(const OWJsonValue* V) { return V && V->IsBool() && V->AsBool(); }

	FString Show(const TArray<FString>& Ids) { return TEXT("[") + FString::Join(Ids, TEXT(", ")) + TEXT("]"); }

	// -- the calls, each as a graph makes it ------------------------------------------------------------------------

	// An array of strings read by its property (the return when Param is nullptr), as an array pin holds it.
	TArray<FString> Strings(FAutomationTestBase& Test, FCall& Call, const TCHAR* Param)
	{
		TArray<FString> Out;
		const FArrayProperty* P = CastField<FArrayProperty>(Param ? Call.Property(Param) : Call.Return());
		const FStrProperty* Inner = P ? CastField<FStrProperty>(P->Inner) : nullptr;
		if (!Inner || !Call.GetFrame() || !OWBlueprintTest::IsOutputPin(P))
		{
			Test.AddError(FString::Printf(TEXT("%s has no TArray<FString> %s"), Call.GetName(), Param ? Param : TEXT("return")));
			return Out;
		}
		FScriptArrayHelper Array(P, P->ContainerPtrToValuePtr<void>(Call.GetFrame()));
		for (int32 I = 0; I < Array.Num(); ++I) Out.Add(*reinterpret_cast<const FString*>(Array.GetRawPtr(I)));
		return Out;
	}

	// An enum read by its property (its UENUM asserted), as its number; -1 (an error logged) when it is not that enum.
	int64 EnumOf(FAutomationTestBase& Test, const FProperty* Prop, const void* Container, const UEnum* Enum, const FString& What)
	{
		const FEnumProperty* P = CastField<FEnumProperty>(Prop);
		if (!P || !Container || P->GetEnum() != Enum)
		{
			Test.AddError(FString::Printf(TEXT("%s is not an %s"), *What, *Enum->GetName()));
			return -1;
		}
		return P->GetUnderlyingProperty()->GetSignedIntPropertyValue(P->ContainerPtrToValuePtr<void>(Container));
	}

	TArray<FString> CallIds(FAutomationTestBase& Test, const TCHAR* Name, const UOWWorld* World)
	{
		FCall Call(Test, UOWMessages::StaticClass(), Name);
		Call.SetWorld(World);
		Call.Invoke();
		return Strings(Test, Call, nullptr);
	}

	// IsMessage, IsNote: one id.
	bool CallIsKind(FAutomationTestBase& Test, const TCHAR* Name, const UOWWorld* World, const FString& ElementId)
	{
		FCall Call(Test, UOWMessages::StaticClass(), Name);
		Call.SetWorld(World);
		Call.SetString(TEXT("ElementId"), ElementId);
		Call.Invoke();
		return Call.ReturnBool();
	}

	TArray<FString> CallRecipientsOf(FAutomationTestBase& Test, const UOWWorld* World, const FString& MessageId)
	{
		FCall Call(Test, UOWMessages::StaticClass(), TEXT("RecipientsOf"));
		Call.SetWorld(World);
		Call.SetString(TEXT("MessageId"), MessageId);
		Call.Invoke();
		return Strings(Test, Call, nullptr);
	}

	// IsAddressedTo, IsSentBy: a message and a Character.
	bool CallWithCharacter(FAutomationTestBase& Test, const TCHAR* Name, const UOWWorld* World, const FString& MessageId,
		const FString& CharacterId)
	{
		FCall Call(Test, UOWMessages::StaticClass(), Name);
		Call.SetWorld(World);
		Call.SetString(TEXT("MessageId"), MessageId);
		Call.SetString(TEXT("CharacterId"), CharacterId);
		Call.Invoke();
		return Call.ReturnBool();
	}

	bool CallIsToEveryone(FAutomationTestBase& Test, const UOWWorld* World, const FString& MessageId, bool bKeyless)
	{
		FCall Call(Test, UOWMessages::StaticClass(), TEXT("IsToEveryone"));
		Call.SetWorld(World);
		Call.SetString(TEXT("MessageId"), MessageId);
		Call.SetBool(TEXT("bKeyless"), bKeyless);
		Call.Invoke();
		return Call.ReturnBool();
	}

	EOWUnseenRecipients CallHasUnseenRecipients(FAutomationTestBase& Test, const UOWWorld* World, const FString& MessageId,
		bool bKeyless)
	{
		FCall Call(Test, UOWMessages::StaticClass(), TEXT("HasUnseenRecipients"));
		Call.SetWorld(World);
		Call.SetString(TEXT("MessageId"), MessageId);
		Call.SetBool(TEXT("bKeyless"), bKeyless);
		Call.Invoke();
		const int64 V = EnumOf(Test, Call.Return(), Call.GetFrame(), StaticEnum<EOWUnseenRecipients>(),
			TEXT("HasUnseenRecipients's return"));
		return static_cast<EOWUnseenRecipients>(V < 0 ? 0 : V);
	}

	// A refusable call's bool and reason.
	struct FAnswer
	{
		bool bOk = false;
		FString Reason;
	};

	FAnswer AnswerOf(FCall& Call)
	{
		FAnswer A;
		A.bOk = Call.ReturnBool();
		A.Reason = Call.Out<FStrProperty>(TEXT("Reason"));
		return A;
	}

	struct FSenderRead : FAnswer
	{
		EOWSenderStatus Status = EOWSenderStatus::Unverifiable;
		FString Claimed, WrittenBy;
	};

	FSenderRead CallSenderStatus(FAutomationTestBase& Test, const UOWWorld* World, const FString& MessageId,
		const FString& MembersJson)
	{
		FSenderRead R;
		FCall Call(Test, UOWMessages::StaticClass(), TEXT("SenderStatus"));
		Call.SetWorld(World);
		Call.SetString(TEXT("MessageId"), MessageId);
		Call.SetString(TEXT("MembersJson"), MembersJson);
		Call.Invoke();
		static_cast<FAnswer&>(R) = AnswerOf(Call);
		const UScriptStruct* S = FOWSender::StaticStruct();
		const void* Data = Call.OutStruct(TEXT("Sender"), S);
		if (!Data) return R;
		const int64 V = EnumOf(Test, S->FindPropertyByName(TEXT("Status")), Data, StaticEnum<EOWSenderStatus>(),
			TEXT("FOWSender.Status"));
		R.Status = static_cast<EOWSenderStatus>(V < 0 ? 0 : V);
		R.Claimed = Member<FStrProperty>(Test, S, Data, TEXT("Claimed"));
		R.WrittenBy = Member<FStrProperty>(Test, S, Data, TEXT("WrittenBy"));
		return R;
	}

	struct FEditedRead : FAnswer
	{
		bool bEdited = false;
		FString By;
	};

	FEditedRead CallEditedBy(FAutomationTestBase& Test, const UOWWorld* World, const FString& MessageId, const FString& MembersJson)
	{
		FEditedRead R;
		FCall Call(Test, UOWMessages::StaticClass(), TEXT("EditedBy"));
		Call.SetWorld(World);
		Call.SetString(TEXT("MessageId"), MessageId);
		Call.SetString(TEXT("MembersJson"), MembersJson);
		Call.Invoke();
		static_cast<FAnswer&>(R) = AnswerOf(Call);
		const UScriptStruct* S = FOWEdited::StaticStruct();
		const void* Data = Call.OutStruct(TEXT("Edited"), S);
		if (!Data) return R;
		R.bEdited = OWBlueprintTest::BoolMember(Test, S, Data, TEXT("bEdited"));
		R.By = Member<FStrProperty>(Test, S, Data, TEXT("By"));
		return R;
	}

	// ReadMarkOf (MarkId) and NoteOf (NoteId): one id, "" for none.
	struct FIdRead : FAnswer
	{
		FString Id;
	};

	FIdRead CallReadMarkOf(FAutomationTestBase& Test, const UOWWorld* World, const FString& CharacterId,
		const FString& MembersJson, bool bKeyless)
	{
		FIdRead R;
		FCall Call(Test, UOWMessages::StaticClass(), TEXT("ReadMarkOf"));
		Call.SetWorld(World);
		Call.SetString(TEXT("CharacterId"), CharacterId);
		Call.SetString(TEXT("MembersJson"), MembersJson);
		Call.SetBool(TEXT("bKeyless"), bKeyless);
		Call.Invoke();
		static_cast<FAnswer&>(R) = AnswerOf(Call);
		R.Id = Call.Out<FStrProperty>(TEXT("MarkId"));
		return R;
	}

	FIdRead CallNoteOf(FAutomationTestBase& Test, const UOWWorld* World, const FString& CharacterId, const FString& MembersJson)
	{
		FIdRead R;
		FCall Call(Test, UOWMessages::StaticClass(), TEXT("NoteOf"));
		Call.SetWorld(World);
		Call.SetString(TEXT("CharacterId"), CharacterId);
		Call.SetString(TEXT("MembersJson"), MembersJson);
		Call.Invoke();
		static_cast<FAnswer&>(R) = AnswerOf(Call);
		R.Id = Call.Out<FStrProperty>(TEXT("NoteId"));
		return R;
	}

	struct FIdsRead : FAnswer
	{
		TArray<FString> Ids;
	};

	FIdsRead CallUnreadFor(FAutomationTestBase& Test, const UOWWorld* World, const FString& CharacterId,
		const FString& ReadMarkId, const FString& MembersJson)
	{
		FIdsRead R;
		FCall Call(Test, UOWMessages::StaticClass(), TEXT("UnreadFor"));
		Call.SetWorld(World);
		Call.SetString(TEXT("CharacterId"), CharacterId);
		Call.SetString(TEXT("ReadMarkId"), ReadMarkId);
		Call.SetString(TEXT("MembersJson"), MembersJson);
		Call.Invoke();
		static_cast<FAnswer&>(R) = AnswerOf(Call);
		R.Ids = Strings(Test, Call, TEXT("Unread"));
		return R;
	}

	TArray<FString> CallThreadOf(FAutomationTestBase& Test, const UOWWorld* World, const FString& RootId)
	{
		FCall Call(Test, UOWMessages::StaticClass(), TEXT("ThreadOf"));
		Call.SetWorld(World);
		Call.SetString(TEXT("RootId"), RootId);
		Call.Invoke();
		return Strings(Test, Call, nullptr);
	}

	// -- worlds made here ---------------------------------------------------------------------------------------------

	// A world in memory: world.json's id, then each body added under its folder's type, in the order given (a body that
	// names no type reads as its folder's, as a world folder keeps it).
	UOWWorld* MakeWorld(std::vector<std::pair<std::string, OWJsonValue>> Bodies)
	{
		auto Native = std::make_unique<OnlyWorlds::OWWorld>();
		Native->info = OWJsonValue::Object();
		Native->info.Set("id", OWJsonValue::String("019a3b10-0000-7000-8000-00000000f000"));
		for (std::pair<std::string, OWJsonValue>& B : Bodies) Native->Add(B.first, std::move(B.second));
		UOWWorld* World = NewObject<UOWWorld>(GetTransientPackage());
		World->SetNative(std::move(Native));
		return World;
	}

	// A case's body as a world holds it: under the folder its type names ("" for a body that names none as a string, so
	// it reads as no type, as the bare body does).
	std::string FolderOf(const OWJsonValue& Body)
	{
		const OWJsonValue* T = Body.IsObject() ? Body.Find("type") : nullptr;
		return T && T->IsString() ? T->Text() : std::string();
	}

	bool HasId(const OWJsonValue& Body)
	{
		const OWJsonValue* Id = Body.IsObject() ? Body.Find("id") : nullptr;
		return Id && Id->IsString() && !Id->Text().empty();
	}

	// The fixture's members list for a case, as the JSON text a Blueprint passes: the case's own when it has the key,
	// else the fixture's; null is no list ("").
	FString MembersOf(const OWJsonValue& Case, const OWJsonValue& Fixture)
	{
		const OWJsonValue* M = Case.Has("members") ? Case.Find("members") : Fixture.Find("members");
		if (!M || M->IsNull()) return FString();
		return Str(OnlyWorlds::OWJson::Stringify(*M, true));
	}

	EOWUnseenRecipients UnseenOf(const OWJsonValue* Want)
	{
		if (Want && Want->IsBool()) return Want->AsBool() ? EOWUnseenRecipients::Yes : EOWUnseenRecipients::No;
		return EOWUnseenRecipients::Unknown;
	}

	bool StatusOf(const FString& Word, EOWSenderStatus& Out)
	{
		if (Word == TEXT("verified")) Out = EOWSenderStatus::Verified;
		else if (Word == TEXT("mismatch")) Out = EOWSenderStatus::Mismatch;
		else if (Word == TEXT("unverifiable")) Out = EOWSenderStatus::Unverifiable;
		else if (Word == TEXT("unseen")) Out = EOWSenderStatus::Unseen;
		else return false;
		return true;
	}

	// The ids a case expects (a list of strings); false when one is not a string (no world holds such an id).
	bool IdsOf(const OWJsonValue* Want, TArray<FString>& Out)
	{
		if (!Want || !Want->IsArray()) return false;
		for (const OWJsonValue& V : Want->Items())
		{
			if (!V.IsString()) return false;
			Out.Add(Str(V.Text()));
		}
		return true;
	}

	struct FTally
	{
		int32 Carried = 0, Failed = 0;
		TArray<FString> NotCarried;
	};

	// One case of the fixture's shapes through the door, its bodies placed in a world of their own and asked by id. The
	// fixture's members list for every case, as the core's test reads both files. A case no world can hold is named in
	// NotCarried, and not counted.
	void RunCase(FAutomationTestBase& Test, const OWJsonValue& Case, const OWJsonValue& Fixture, FTally& Tally)
	{
		const FString Kind = TextOf(Case.Find("kind"));
		const FString Name = Kind + TEXT("/") + TextOf(Case.Find("name"));
		const FString Members = MembersOf(Case, Fixture);
		const bool bKeyless = BoolOf(Case.Find("keyless"));
		bool bHeld = true;
		auto Check = [&](bool bCond, const FString& What)
		{
			if (!Test.TestTrue(Name + TEXT(": ") + What, bCond)) bHeld = false;
		};
		auto NotCarried = [&](const TCHAR* Why) { Tally.NotCarried.Add(Name + TEXT(" (") + Why + TEXT(")")); };

		const OWJsonValue* Single = Case.Find(Kind == TEXT("pair") ? "element" : "message");
		const bool bSingle = Kind == TEXT("pair") || Kind == TEXT("addressed") || Kind == TEXT("unseen")
			|| Kind == TEXT("everyone") || Kind == TEXT("sent_by") || Kind == TEXT("sender") || Kind == TEXT("edited")
			|| Kind == TEXT("recipients");
		if (bSingle)
		{
			if (!Single || !Single->IsObject()) return NotCarried(TEXT("its body is not an object, so no element holds it"));
			// None of these kinds reads the id: a body without one is given one to be asked by.
			OWJsonValue Body = *Single;
			if (!HasId(Body)) Body.Set("id", OWJsonValue::String("019a3b10-0000-7000-8000-00000000f001"));
			const FString Id = TextOf(Body.Find("id"));
			std::vector<std::pair<std::string, OWJsonValue>> Bodies;
			Bodies.emplace_back(FolderOf(Body), std::move(Body));
			const UOWWorld* World = MakeWorld(std::move(Bodies));
			const FString Who = TextOf(Case.Find("character"));
			if (Kind == TEXT("pair"))
			{
				Check(CallIsKind(Test, TEXT("IsMessage"), World, Id) == BoolOf(Case.Find("is_message")), TEXT("is_message"));
			}
			else if (Kind == TEXT("addressed") || Kind == TEXT("unseen"))
			{
				Check(CallWithCharacter(Test, TEXT("IsAddressedTo"), World, Id, Who) == BoolOf(Case.Find("addressed")), TEXT("addressed"));
				if (Case.Has("unseen_recipients"))
					Check(CallHasUnseenRecipients(Test, World, Id, false) == UnseenOf(Case.Find("unseen_recipients")), TEXT("unseen_recipients"));
			}
			else if (Kind == TEXT("everyone"))
			{
				Check(CallIsToEveryone(Test, World, Id, bKeyless) == BoolOf(Case.Find("to_everyone")), TEXT("to_everyone"));
				Check(CallHasUnseenRecipients(Test, World, Id, bKeyless) == UnseenOf(Case.Find("unseen_recipients")), TEXT("unseen_recipients"));
			}
			else if (Kind == TEXT("sent_by"))
			{
				Check(CallWithCharacter(Test, TEXT("IsSentBy"), World, Id, Who) == BoolOf(Case.Find("sent_by")), TEXT("sent_by"));
			}
			else if (Kind == TEXT("sender"))
			{
				const FSenderRead S = CallSenderStatus(Test, World, Id, Members);
				EOWSenderStatus Want = EOWSenderStatus::Unverifiable;
				Check(StatusOf(TextOf(Case.Find("status")), Want), TEXT("a status the profile names"));
				Check(S.bOk && S.Reason.IsEmpty(), TEXT("answered, got the refusal ") + S.Reason);
				Check(S.Status == Want, TEXT("status"));
				Check(S.Claimed == TextOf(Case.Find("claimed")), TEXT("claimed, got ") + S.Claimed);
				Check(S.WrittenBy == TextOf(Case.Find("written_by")), TEXT("written_by, got ") + S.WrittenBy);
			}
			else if (Kind == TEXT("recipients"))
			{
				// Each once, in its first place; only a message has any (serial 128).
				TArray<FString> Want;
				Check(IdsOf(Case.Find("recipients"), Want), TEXT("the recipients the case gives"));
				const TArray<FString> Got = CallRecipientsOf(Test, World, Id);
				Check(Got == Want, TEXT("recipients ") + Show(Want) + TEXT(", got ") + Show(Got));
			}
			else
			{
				const FEditedRead E = CallEditedBy(Test, World, Id, Members);
				Check(E.bOk && E.Reason.IsEmpty(), TEXT("answered, got the refusal ") + E.Reason);
				Check(E.bEdited == BoolOf(Case.Find("edited")), TEXT("edited"));
				Check(E.By == TextOf(Case.Find("edited_by")), TEXT("edited_by, got ") + E.By);
			}
		}
		else
		{
			const char* ListKey = Kind == TEXT("mark") ? "relations" : Kind == TEXT("note") ? "notes" : "messages";
			const OWJsonValue* List = Case.Find(ListKey);
			if (!List || !List->IsArray())
			{
				Test.AddError(Name + TEXT(": no ") + Str(ListKey));
				++Tally.Failed;
				return;
			}
			std::vector<std::pair<std::string, OWJsonValue>> Bodies;
			for (const OWJsonValue& B : List->Items()) Bodies.emplace_back(FolderOf(B), B);
			FString ReadMarkId;
			if (Kind == TEXT("unread"))
			{
				const OWJsonValue* Mark = Case.Find("read_mark");
				if (Mark && !Mark->IsNull())
				{
					if (!HasId(*Mark)) return NotCarried(TEXT("its read mark is not a body with an id, so no element holds it"));
					ReadMarkId = TextOf(Mark->Find("id"));
					Bodies.emplace_back(FolderOf(*Mark), *Mark);
				}
			}
			TArray<FString> WantIds;
			const char* WantKey = Kind == TEXT("unread") ? "unread" : Kind == TEXT("thread") ? "thread" : nullptr;
			if (WantKey && !IdsOf(Case.Find(WantKey), WantIds))
				return NotCarried(TEXT("it expects an id that is not a string, which no world holds"));
			const UOWWorld* World = MakeWorld(std::move(Bodies));
			if (Kind == TEXT("mark"))
			{
				const FIdRead M = CallReadMarkOf(Test, World, TextOf(Case.Find("reader")), Members, bKeyless);
				Check(M.bOk && M.Reason.IsEmpty(), TEXT("answered, got the refusal ") + M.Reason);
				Check(M.Id == TextOf(Case.Find("mark")), TEXT("mark, got ") + M.Id);
			}
			else if (Kind == TEXT("unread"))
			{
				const FIdsRead U = CallUnreadFor(Test, World, TextOf(Case.Find("reader")), ReadMarkId, Members);
				Check(U.bOk && U.Reason.IsEmpty(), TEXT("answered, got the refusal ") + U.Reason);
				Check(U.Ids == WantIds, TEXT("unread ") + Show(WantIds) + TEXT(", got ") + Show(U.Ids));
			}
			else if (Kind == TEXT("thread"))
			{
				const TArray<FString> T = CallThreadOf(Test, World, TextOf(Case.Find("root")));
				Check(T == WantIds, TEXT("thread ") + Show(WantIds) + TEXT(", got ") + Show(T));
			}
			else if (Kind == TEXT("note"))
			{
				const FIdRead N = CallNoteOf(Test, World, TextOf(Case.Find("character")), Members);
				Check(N.bOk && N.Reason.IsEmpty(), TEXT("answered, got the refusal ") + N.Reason);
				Check(N.Id == TextOf(Case.Find("line")), TEXT("line, got ") + N.Id);
			}
			else
			{
				Test.AddError(Name + TEXT(": a case of unknown kind"));
				bHeld = false;
			}
		}
		++Tally.Carried;
		if (!bHeld) ++Tally.Failed;
	}

	bool LoadDoc(FAutomationTestBase& Test, const TCHAR* Rel, OWJsonValue& Doc)
	{
		const FString Path = OWTestPaths::InRepo(Rel);
		if (OWTestPaths::ReadJson(Path, Doc)) return true;
		Test.AddError(TEXT("no message cases at ") + Path);
		return false;
	}

	// -- the hand-made world ------------------------------------------------------------------------------------------

	const TCHAR* const Ada = TEXT("019a3b10-0000-7000-8000-0000000000a1");   // the owner's Character
	const TCHAR* const Mara = TEXT("019a3b10-0000-7000-8000-0000000000a2");  // membership m2
	const TCHAR* const Wren = TEXT("019a3b10-0000-7000-8000-0000000000a3");  // membership m3

	// The members list as OWClient::Members answers it, the text a Blueprint passes.
	const char* const HandMembersJson = R"([{"id": null, "kind": "owner", "character": "019a3b10-0000-7000-8000-0000000000a1"},
		{"id": "m2", "kind": "person", "character": "019a3b10-0000-7000-8000-0000000000a2"},
		{"id": "m3", "kind": "agent", "character": "019a3b10-0000-7000-8000-0000000000a3"},
		{"id": "m5", "kind": "person", "character": null}])";

	// Added out of created order; c3 and r1 name no type (their folder's: narrative, relation).
	UOWWorld* HandWorld()
	{
		std::vector<std::pair<std::string, OWJsonValue>> B;
		B.emplace_back("narrative", J(R"({"id": "c3", "supertype": "Message", "name": "Third",
			"narrator": "019a3b10-0000-7000-8000-0000000000a2", "characters": ["019a3b10-0000-7000-8000-0000000000a1"],
			"parent_narrative": "c1", "created_at": "2026-10-26T09:03:00Z", "created_by": "m2"})"));
		B.emplace_back("narrative", J(R"({"id": "c1", "type": "narrative", "supertype": "Message", "name": "First",
			"narrator": "019a3b10-0000-7000-8000-0000000000a1", "characters": [], "created_at": "2026-10-26T09:01:00Z",
			"created_by": null})"));
		B.emplace_back("narrative", J(R"({"id": "c2", "type": "Narrative", "supertype": " message ", "name": "Second",
			"narrator": "019a3b10-0000-7000-8000-0000000000a3", "characters": ["019a3b10-0000-7000-8000-0000000000a2"],
			"parent_narrative": "c1", "created_at": "2026-10-26T09:02:00Z", "created_by": "m2", "changed_by": "m3"})"));
		B.emplace_back("narrative", J(R"({"id": "c4", "type": "narrative", "supertype": "Message", "name": "Fourth",
			"narrator": null, "characters": [], "x_served_trimmed": ["narrator", "characters"],
			"created_at": "2026-10-26T09:04:00Z"})"));
		B.emplace_back("narrative", J(R"({"id": "c5", "type": "narrative", "supertype": "Message", "name": "Fifth",
			"narrator": "019a3b10-0000-7000-8000-0000000000a1", "characters": ["019a3b10-0000-7000-8000-0000000000a2"],
			"parent_narrative": "n1", "created_at": "2026-10-26T09:05:00Z", "created_by": null})"));
		B.emplace_back("narrative", J(R"({"id": "n1", "type": "narrative", "supertype": "Note", "name": "Mara's line",
			"narrator": "019a3b10-0000-7000-8000-0000000000a2", "characters": ["019a3b10-0000-7000-8000-0000000000a1"],
			"parent_narrative": "c1", "created_at": "2026-10-26T08:00:00Z", "created_by": "m2"})"));
		B.emplace_back("narrative", J(R"({"id": "n2", "type": "narrative", "supertype": "Note", "name": "Mara's hidden line",
			"narrator": "019a3b10-0000-7000-8000-0000000000a2", "created_at": "2026-10-26T10:00:00Z", "created_by": "m2",
			"hidden": true})"));
		B.emplace_back("narrative", J(R"({"id": "l1", "type": "narrative", "supertype": "Legend", "name": "A legend",
			"characters": [], "created_at": "2026-10-26T07:00:00Z"})"));
		B.emplace_back("character", J(R"({"id": "k1", "type": "character", "supertype": "Message", "name": "Not a message"})"));
		B.emplace_back("relation", J(R"({"id": "r1", "supertype": "Read", "name": "Read: Ada",
			"actor": "019a3b10-0000-7000-8000-0000000000a1", "narratives": ["c3"], "updated_at": "2026-10-26T11:00:00Z",
			"created_by": null})"));
		B.emplace_back("relation", J(R"({"id": "r2", "type": "relation", "supertype": "Read", "name": "Read: Ada",
			"actor": "019a3b10-0000-7000-8000-0000000000a1", "narratives": ["c3", "c4", "c5"],
			"updated_at": "2026-10-26T12:00:00Z", "created_by": "m3"})"));
		return MakeWorld(std::move(B));
	}

	TArray<FString> Ids(std::initializer_list<const TCHAR*> L)
	{
		TArray<FString> Out;
		for (const TCHAR* S : L) Out.Add(S);
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWMessagesWorldTest, "OnlyWorlds.Messages.World", OWTestFlags)

bool FOWMessagesWorldTest::RunTest(const FString& Parameters)
{
	const UOWWorld* World = HandWorld();
	const FString Members = Str(std::string(HandMembersJson));
	auto SameIds = [&](const FString& What, const TArray<FString>& Got, const TArray<FString>& Want)
	{
		TestTrue(What + TEXT(": ") + Show(Want) + TEXT(", got ") + Show(Got), Got == Want);
	};

	// The world's messages and Notes, in created order; c3 and c2 by their folder's type and the pair folded.
	SameIds(TEXT("GetMessages"), CallIds(*this, TEXT("GetMessages"), World), Ids({ TEXT("c1"), TEXT("c2"), TEXT("c3"), TEXT("c4"), TEXT("c5") }));
	SameIds(TEXT("GetNotes"), CallIds(*this, TEXT("GetNotes"), World), Ids({ TEXT("n1"), TEXT("n2") }));
	TestEqual(TEXT("GetMessages, no world"), CallIds(*this, TEXT("GetMessages"), nullptr).Num(), 0);

	TestTrue(TEXT("IsMessage c3: no type, its folder's"), CallIsKind(*this, TEXT("IsMessage"), World, TEXT("c3")));
	TestTrue(TEXT("IsMessage c2: the pair trimmed and folded"), CallIsKind(*this, TEXT("IsMessage"), World, TEXT("c2")));
	for (const TCHAR* Id : { TEXT("n1"), TEXT("l1"), TEXT("k1"), TEXT("r1"), TEXT("nope"), TEXT("") })
		TestFalse(FString::Printf(TEXT("IsMessage %s"), Id), CallIsKind(*this, TEXT("IsMessage"), World, Id));
	TestFalse(TEXT("IsMessage, no world"), CallIsKind(*this, TEXT("IsMessage"), nullptr, TEXT("c1")));
	TestTrue(TEXT("IsNote n1"), CallIsKind(*this, TEXT("IsNote"), World, TEXT("n1")));
	TestFalse(TEXT("IsNote c1"), CallIsKind(*this, TEXT("IsNote"), World, TEXT("c1")));
	TestFalse(TEXT("IsNote nope"), CallIsKind(*this, TEXT("IsNote"), World, TEXT("nope")));

	// Addressing.
	SameIds(TEXT("RecipientsOf c3"), CallRecipientsOf(*this, World, TEXT("c3")), Ids({ Ada }));
	SameIds(TEXT("RecipientsOf c1 (to everyone)"), CallRecipientsOf(*this, World, TEXT("c1")), {});
	SameIds(TEXT("RecipientsOf n1 (a Note has none)"), CallRecipientsOf(*this, World, TEXT("n1")), {});
	SameIds(TEXT("RecipientsOf nope"), CallRecipientsOf(*this, World, TEXT("nope")), {});
	TestTrue(TEXT("IsAddressedTo c3 Ada"), CallWithCharacter(*this, TEXT("IsAddressedTo"), World, TEXT("c3"), Ada));
	TestFalse(TEXT("IsAddressedTo c3 Mara (its sender)"), CallWithCharacter(*this, TEXT("IsAddressedTo"), World, TEXT("c3"), Mara));
	TestTrue(TEXT("IsAddressedTo c1 Mara (to everyone)"), CallWithCharacter(*this, TEXT("IsAddressedTo"), World, TEXT("c1"), Mara));
	TestFalse(TEXT("IsAddressedTo c4 Ada (trimmed)"), CallWithCharacter(*this, TEXT("IsAddressedTo"), World, TEXT("c4"), Ada));
	TestFalse(TEXT("IsAddressedTo n1 Ada (a Note)"), CallWithCharacter(*this, TEXT("IsAddressedTo"), World, TEXT("n1"), Ada));
	TestFalse(TEXT("IsAddressedTo c1 \"\""), CallWithCharacter(*this, TEXT("IsAddressedTo"), World, TEXT("c1"), TEXT("")));
	TestTrue(TEXT("IsToEveryone c1"), CallIsToEveryone(*this, World, TEXT("c1"), false));
	TestFalse(TEXT("IsToEveryone c1 keyless"), CallIsToEveryone(*this, World, TEXT("c1"), true));
	TestFalse(TEXT("IsToEveryone c4 (trimmed)"), CallIsToEveryone(*this, World, TEXT("c4"), false));
	TestFalse(TEXT("IsToEveryone c3"), CallIsToEveryone(*this, World, TEXT("c3"), false));
	TestTrue(TEXT("HasUnseenRecipients c4: Yes"), CallHasUnseenRecipients(*this, World, TEXT("c4"), false) == EOWUnseenRecipients::Yes);
	TestTrue(TEXT("HasUnseenRecipients c1: No"), CallHasUnseenRecipients(*this, World, TEXT("c1"), false) == EOWUnseenRecipients::No);
	TestTrue(TEXT("HasUnseenRecipients c1 keyless: Unknown"), CallHasUnseenRecipients(*this, World, TEXT("c1"), true) == EOWUnseenRecipients::Unknown);
	TestTrue(TEXT("HasUnseenRecipients n1 keyless: No (a Note)"), CallHasUnseenRecipients(*this, World, TEXT("n1"), true) == EOWUnseenRecipients::No);
	TestTrue(TEXT("HasUnseenRecipients nope: No"), CallHasUnseenRecipients(*this, World, TEXT("nope"), false) == EOWUnseenRecipients::No);
	TestTrue(TEXT("IsSentBy c3 Mara"), CallWithCharacter(*this, TEXT("IsSentBy"), World, TEXT("c3"), Mara));
	TestFalse(TEXT("IsSentBy c3 Ada"), CallWithCharacter(*this, TEXT("IsSentBy"), World, TEXT("c3"), Ada));
	TestFalse(TEXT("IsSentBy c4 Ada (no narrator)"), CallWithCharacter(*this, TEXT("IsSentBy"), World, TEXT("c4"), Ada));

	// The sender and the edit mark, against the members list.
	auto SameSender = [&](const FString& What, const FSenderRead& S, EOWSenderStatus Status, const TCHAR* Claimed, const TCHAR* WrittenBy)
	{
		TestTrue(What + TEXT(": answered, got ") + S.Reason, S.bOk && S.Reason.IsEmpty());
		TestTrue(What + TEXT(": status"), S.Status == Status);
		TestEqual(What + TEXT(": Claimed"), S.Claimed, FString(Claimed));
		TestEqual(What + TEXT(": WrittenBy"), S.WrittenBy, FString(WrittenBy));
	};
	SameSender(TEXT("SenderStatus c1 (the owner)"), CallSenderStatus(*this, World, TEXT("c1"), Members), EOWSenderStatus::Verified, Ada, TEXT(""));
	SameSender(TEXT("SenderStatus c2"), CallSenderStatus(*this, World, TEXT("c2"), Members), EOWSenderStatus::Mismatch, Wren, Mara);
	SameSender(TEXT("SenderStatus c3"), CallSenderStatus(*this, World, TEXT("c3"), Members), EOWSenderStatus::Verified, Mara, TEXT(""));
	SameSender(TEXT("SenderStatus c4"), CallSenderStatus(*this, World, TEXT("c4"), Members), EOWSenderStatus::Unseen, TEXT(""), TEXT(""));
	SameSender(TEXT("SenderStatus c3, no list"), CallSenderStatus(*this, World, TEXT("c3"), TEXT("")), EOWSenderStatus::Unverifiable, Mara, TEXT(""));
	SameSender(TEXT("SenderStatus c3, a list that is not an array"), CallSenderStatus(*this, World, TEXT("c3"), TEXT("{}")),
		EOWSenderStatus::Unverifiable, Mara, TEXT(""));
	{
		const FSenderRead S = CallSenderStatus(*this, World, TEXT("c3"), TEXT("[{"));
		TestTrue(TEXT("SenderStatus, members that do not parse: refused, got ") + S.Reason, !S.bOk && S.Reason.StartsWith(TEXT("members: ")));
		TestTrue(TEXT("and the Sender is the default"), S.Status == EOWSenderStatus::Unverifiable && S.Claimed.IsEmpty());
		const FSenderRead N = CallSenderStatus(*this, World, TEXT("nope"), Members);
		TestTrue(TEXT("SenderStatus nope: refused, got ") + N.Reason, !N.bOk && N.Reason == TEXT("no element nope in this world"));
		const FSenderRead W = CallSenderStatus(*this, nullptr, TEXT("c1"), Members);
		TestTrue(TEXT("SenderStatus, no world: refused, got ") + W.Reason, !W.bOk && W.Reason == TEXT("no world"));
	}
	{
		const FEditedRead E = CallEditedBy(*this, World, TEXT("c2"), Members);
		TestTrue(TEXT("EditedBy c2: by Wren"), E.bOk && E.bEdited && E.By == Wren);
		const FEditedRead L = CallEditedBy(*this, World, TEXT("c2"), TEXT(""));
		TestTrue(TEXT("EditedBy c2, no list: edited by another hand"), L.bOk && L.bEdited && L.By.IsEmpty());
		const FEditedRead C = CallEditedBy(*this, World, TEXT("c1"), Members);
		TestTrue(TEXT("EditedBy c1: not edited"), C.bOk && !C.bEdited && C.By.IsEmpty());
		const FEditedRead N = CallEditedBy(*this, World, TEXT("nope"), Members);
		TestTrue(TEXT("EditedBy nope: refused, got ") + N.Reason, !N.bOk && N.Reason == TEXT("no element nope in this world"));
		const FEditedRead B = CallEditedBy(*this, World, TEXT("c2"), TEXT("nope"));
		TestTrue(TEXT("EditedBy, members that do not parse: refused, got ") + B.Reason, !B.bOk && B.Reason.StartsWith(TEXT("members: ")));
	}

	// The read mark: r1 (its folder's type) is Ada's own; r2, newer, is in Wren's hand.
	{
		const FIdRead M = CallReadMarkOf(*this, World, Ada, Members, false);
		TestTrue(TEXT("ReadMarkOf Ada: r1, got ") + M.Id, M.bOk && M.Id == TEXT("r1"));
		const FIdRead L = CallReadMarkOf(*this, World, Ada, TEXT(""), false);
		TestTrue(TEXT("ReadMarkOf Ada, no list: none proved, got ") + L.Id, L.bOk && L.Id.IsEmpty());
		const FIdRead K = CallReadMarkOf(*this, World, Ada, Members, true);
		TestTrue(TEXT("ReadMarkOf Ada keyless: none, got ") + K.Id, K.bOk && K.Id.IsEmpty());
		const FIdRead O = CallReadMarkOf(*this, World, Mara, Members, false);
		TestTrue(TEXT("ReadMarkOf Mara: none, got ") + O.Id, O.bOk && O.Id.IsEmpty());
		const FIdRead B = CallReadMarkOf(*this, World, Ada, TEXT("["), false);
		TestTrue(TEXT("ReadMarkOf, members that do not parse: refused, got ") + B.Reason, !B.bOk && B.Reason.StartsWith(TEXT("members: ")));
		const FIdRead W = CallReadMarkOf(*this, nullptr, Ada, Members, false);
		TestTrue(TEXT("ReadMarkOf, no world: refused, got ") + W.Reason, !W.bOk && W.Reason == TEXT("no world"));
	}

	// Unread, in created order: the messages only (the Legend l1, to everyone by its empty list, is no message).
	{
		const FIdsRead A = CallUnreadFor(*this, World, Ada, TEXT("r1"), Members);
		TestTrue(TEXT("UnreadFor Ada with r1: none, got ") + Show(A.Ids), A.bOk && A.Ids.Num() == 0);
		const FIdsRead N = CallUnreadFor(*this, World, Ada, TEXT(""), Members);
		TestTrue(TEXT("UnreadFor Ada, no mark: [c3], got ") + Show(N.Ids), N.bOk && N.Ids == Ids({ TEXT("c3") }));
		const FIdsRead M = CallUnreadFor(*this, World, Mara, TEXT(""), Members);
		TestTrue(TEXT("UnreadFor Mara: [c1, c2, c5], got ") + Show(M.Ids), M.bOk && M.Ids == Ids({ TEXT("c1"), TEXT("c2"), TEXT("c5") }));
		const FIdsRead R = CallUnreadFor(*this, World, Ada, TEXT("nope"), Members);
		TestTrue(TEXT("UnreadFor, a read mark the world does not hold: refused, got ") + R.Reason,
			!R.bOk && R.Reason == TEXT("no element nope in this world"));
		const FIdsRead B = CallUnreadFor(*this, World, Ada, TEXT(""), TEXT("[1,"));
		TestTrue(TEXT("UnreadFor, members that do not parse: refused, got ") + B.Reason, !B.bOk && B.Reason.StartsWith(TEXT("members: ")));
	}

	// A thread: c5 answers the Note n1, which answers c1, so it is in c1's thread; the Note is not.
	SameIds(TEXT("ThreadOf c1"), CallThreadOf(*this, World, TEXT("c1")), Ids({ TEXT("c1"), TEXT("c2"), TEXT("c3"), TEXT("c5") }));
	SameIds(TEXT("ThreadOf c2"), CallThreadOf(*this, World, TEXT("c2")), Ids({ TEXT("c2") }));
	SameIds(TEXT("ThreadOf \"\""), CallThreadOf(*this, World, TEXT("")), {});
	SameIds(TEXT("ThreadOf nope"), CallThreadOf(*this, World, TEXT("nope")), {});

	// A Character's line: Mara's n1 (n2 is newer and hidden).
	{
		const FIdRead L = CallNoteOf(*this, World, Mara, Members);
		TestTrue(TEXT("NoteOf Mara: n1, got ") + L.Id, L.bOk && L.Id == TEXT("n1"));
		const FIdRead N = CallNoteOf(*this, World, Mara, TEXT(""));
		TestTrue(TEXT("NoteOf Mara, no list: none proved, got ") + N.Id, N.bOk && N.Id.IsEmpty());
		const FIdRead A = CallNoteOf(*this, World, Ada, Members);
		TestTrue(TEXT("NoteOf Ada: none, got ") + A.Id, A.bOk && A.Id.IsEmpty());
		const FIdRead B = CallNoteOf(*this, World, Mara, TEXT("x"));
		TestTrue(TEXT("NoteOf, members that do not parse: refused, got ") + B.Reason, !B.bOk && B.Reason.StartsWith(TEXT("members: ")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWMessagesFixtureTest, "OnlyWorlds.Messages.Fixture", OWTestFlags)

bool FOWMessagesFixtureTest::RunTest(const FString& Parameters)
{
	OWJsonValue Fixture;
	if (!LoadDoc(*this, TEXT("codegen/schema-dist/profiles/message.fixture.json"), Fixture)) return false;
	const OWJsonValue* Cases = Fixture.Find("cases");
	if (!TestTrue(TEXT("the fixture holds cases"), Cases && Cases->IsArray())) return false;
	FTally Tally;
	for (const OWJsonValue& Case : Cases->Items()) RunCase(*this, Case, Fixture, Tally);
	TestEqual(TEXT("every one of the 102 cases carried"), Tally.Carried, 102);
	TestEqual(TEXT("none left out"), Tally.NotCarried.Num(), 0);
	TestEqual(TEXT("cases failed"), Tally.Failed, 0);
	AddInfo(FString::Printf(TEXT("message fixture: %d cases through the Blueprint path, %d failed"), Tally.Carried, Tally.Failed));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWMessagesBesideTest, "OnlyWorlds.Messages.Beside", OWTestFlags)

bool FOWMessagesBesideTest::RunTest(const FString& Parameters)
{
	OWJsonValue Fixture, Beside;
	if (!LoadDoc(*this, TEXT("codegen/schema-dist/profiles/message.fixture.json"), Fixture)) return false;
	if (!LoadDoc(*this, TEXT("tests/data/message-beside-the-fixture.json"), Beside)) return false;
	const OWJsonValue* Cases = Beside.Find("cases");
	if (!TestTrue(TEXT("the readings beside the fixture hold cases"), Cases && Cases->IsArray())) return false;
	FTally Tally;
	for (const OWJsonValue& Case : Cases->Items()) RunCase(*this, Case, Fixture, Tally);
	// Three cases no world can hold: a body that is a list, a message whose id is a number, a read mark that is a list.
	const TArray<FString> Left = Ids({
		TEXT("pair/pair-a-list-is-not-a-body (its body is not an object, so no element holds it)"),
		TEXT("unread/unread-message-id-a-number (it expects an id that is not a string, which no world holds)"),
		TEXT("unread/unread-read-mark-not-a-body (its read mark is not a body with an id, so no element holds it)") });
	TestEqual(TEXT("76 of the 79 cases carried"), Tally.Carried, 76);
	TestTrue(TEXT("the three left out: ") + FString::Join(Tally.NotCarried, TEXT("; ")), Tally.NotCarried == Left);
	TestEqual(TEXT("cases failed"), Tally.Failed, 0);

	// recipientsOf: the non-empty string ids as written, a repeat once in its first place (serial 128); none for a Note.
	const OWJsonValue* R = Beside.Find("recipients");
	if (!TestTrue(TEXT("the recipients block"), R && R->IsObject())) return false;
	const OWJsonValue* Message = R->Find("message");
	const OWJsonValue* Note = R->Find("note");
	if (!TestTrue(TEXT("its message and its Note are bodies with ids"), Message && Note && HasId(*Message) && HasId(*Note))) return false;
	std::vector<std::pair<std::string, OWJsonValue>> Bodies;
	Bodies.emplace_back(FolderOf(*Message), *Message);
	Bodies.emplace_back(FolderOf(*Note), *Note);
	const UOWWorld* World = MakeWorld(std::move(Bodies));
	TArray<FString> Of, OfNote;
	TestTrue(TEXT("the recipients expected"), IdsOf(R->Find("of"), Of) && IdsOf(R->Find("of_note"), OfNote));
	const TArray<FString> Got = CallRecipientsOf(*this, World, TextOf(Message->Find("id")));
	const TArray<FString> GotNote = CallRecipientsOf(*this, World, TextOf(Note->Find("id")));
	TestTrue(TEXT("RecipientsOf ") + Show(Of) + TEXT(", got ") + Show(Got), Got == Of);
	TestTrue(TEXT("RecipientsOf a Note ") + Show(OfNote) + TEXT(", got ") + Show(GotNote), GotNote == OfNote);
	AddInfo(FString::Printf(TEXT("beside the fixture: %d cases through the Blueprint path, %d failed, %d left out; and the "
		"recipients block"), Tally.Carried, Tally.Failed, Tally.NotCarried.Num()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWMessagesSikeliaTest, "OnlyWorlds.Messages.Sikelia", OWTestFlags)

bool FOWMessagesSikeliaTest::RunTest(const FString& Parameters)
{
	UOWWorld* World = OWTestPaths::OpenWorld(*this, OWTestPaths::Sikelia(), TEXT("OW_SIKELIA"));
	if (!World) return false;
	// Every element as the door asks it, against the core over the element as served (its folder's type when it names none).
	std::vector<OWJsonValue> Served;
	for (const OnlyWorlds::OWElement& E : World->Native()->elements)
	{
		OWJsonValue B = E.body;
		if (B.IsObject() && !B.Find("type")) B.Set("type", OWJsonValue::String(E.Type()));
		Served.push_back(std::move(B));
	}
	std::vector<const OWJsonValue*> Messages, Notes;
	int32 Same = 0, Asked = 0;
	for (size_t I = 0; I < Served.size(); ++I)
	{
		const FString Id = Str(World->Native()->elements[I].Id());
		const bool bMessage = OnlyWorlds::OWMessage::IsMessage(Served[I]);
		const bool bNote = OnlyWorlds::OWMessage::IsNote(Served[I]);
		if (bMessage) Messages.push_back(&Served[I]);
		if (bNote) Notes.push_back(&Served[I]);
		++Asked;
		if (CallIsKind(*this, TEXT("IsMessage"), World, Id) == bMessage && CallIsKind(*this, TEXT("IsNote"), World, Id) == bNote) ++Same;
		else if (Asked - Same <= 3) AddError(FString::Printf(TEXT("%s: the door and the core part"), *Id));
	}
	TestTrue(TEXT("Sikelia holds elements"), Asked > 0);
	TestEqual(TEXT("IsMessage and IsNote on every element, as the core reads it"), Same, Asked);
	auto IdsIn = [](const std::vector<const OWJsonValue*>& Bodies)
	{
		TArray<FString> Out;
		for (const OWJsonValue* B : OnlyWorlds::OWMessage::InCreatedOrder(Bodies)) Out.Add(TextOf(B->Find("id")));
		return Out;
	};
	TestTrue(TEXT("GetMessages, the core's"), CallIds(*this, TEXT("GetMessages"), World) == IdsIn(Messages));
	TestTrue(TEXT("GetNotes, the core's"), CallIds(*this, TEXT("GetNotes"), World) == IdsIn(Notes));
	AddInfo(FString::Printf(TEXT("Sikelia: %d elements asked, %d messages, %d Notes"), Asked, static_cast<int32>(Messages.size()),
		static_cast<int32>(Notes.size())));
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
