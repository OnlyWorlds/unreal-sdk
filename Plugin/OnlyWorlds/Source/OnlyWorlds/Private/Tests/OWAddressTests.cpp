// The address through UOWAddresses as a Blueprint calls it (OWBlueprintCall.h): ParseAddress and WriteAddress found by
// name, the text or the parts set by property (FOWAddress member by member, as a Make node), ProcessEvent, and the
// answer read back by property (as a Break node). Every case of the profile's fixture, as the core's runner reads
// them, then every element of a real world (Sikelia) written and read back, then the door's own rule for an empty part.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "OWAddress.h"
#include "OWAddressLibrary.h"
#include "OWConvert.h"
#include "OWJson.h"
#include "OWWorld.h"
#include "OWWorldObject.h"
#include "OWAddressMembers.h"
#include "OWBlueprintCall.h"
#include "OWTestPaths.h"

#include <cmath>
#include <string>

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using OnlyWorlds::OWJsonValue;
	using OWBlueprintTest::BoolMember;
	using OWBlueprintTest::FCall;
	using OWBlueprintTest::Member;
	using OWConvert::Str;

	constexpr EAutomationTestFlags OWTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

	using OWAddressMembers::BreakAddress;
	using OWAddressMembers::MakeAddress;

	struct FParsed
	{
		bool bOk = false;
		FOWAddress Address;
		FString Reason;
	};

	// With Stale, the out struct's slot holds a reading before the call, as a graph's pin terminal may from the last one.
	FParsed CallParse(FAutomationTestBase& Test, const FString& Text, const FOWAddress* Stale = nullptr)
	{
		FParsed R;
		FCall Call(Test, UOWAddresses::StaticClass(), TEXT("ParseAddress"));
		Call.SetString(TEXT("Text"), Text);
		if (Stale)
		{
			if (FStructProperty* P = CastField<FStructProperty>(Call.Property(TEXT("Address"))))
				MakeAddress(Test, P->ContainerPtrToValuePtr<void>(Call.GetFrame()), *Stale);
		}
		Call.Invoke();
		R.bOk = Call.ReturnBool();
		R.Address = BreakAddress(Test, Call.OutStruct(TEXT("Address"), FOWAddress::StaticStruct()));
		R.Reason = Call.Out<FStrProperty>(TEXT("Reason"));
		return R;
	}

	struct FWritten
	{
		bool bOk = false;
		FString Text, Reason;
	};

	FWritten CallWrite(FAutomationTestBase& Test, const FOWAddress& Parts)
	{
		FWritten R;
		FCall Call(Test, UOWAddresses::StaticClass(), TEXT("WriteAddress"));
		if (void* In = Call.StructSlot(TEXT("Address"), FOWAddress::StaticStruct())) MakeAddress(Test, In, Parts);
		Call.Invoke();
		R.bOk = Call.ReturnBool();
		R.Text = Call.Out<FStrProperty>(TEXT("Text"));
		R.Reason = Call.Out<FStrProperty>(TEXT("Reason"));
		return R;
	}

	FString TextOf(const OWJsonValue* V) { return V && V->IsString() ? Str(V->Text()) : FString(); }

	// A fixture's part ("world", ...) as the door's string and flag: a string given, null or missing absent.
	void PartOf(const OWJsonValue& Json, const char* Key, FString& To, bool& bHas)
	{
		const OWJsonValue* V = Json.Find(Key);
		bHas = V && V->IsString();
		To = TextOf(V);
	}

	// Is this every member a reading of the fixture's expect gives? An absent part is "" with its flag clear.
	bool SameReading(const FOWAddress& Got, const OWJsonValue& Want, FString& Why)
	{
		auto Part = [&](const TCHAR* Name, const FString& Value, bool bHas, const OWJsonValue* W)
		{
			const bool bWant = W && W->IsString();
			if (bHas != bWant || Value != TextOf(W)) Why += FString::Printf(TEXT(" %s=\"%s\"(%s)"), Name, *Value, bHas ? TEXT("given") : TEXT("absent"));
		};
		Part(TEXT("World"), Got.World, Got.bHasWorld, Want.Find("world"));
		Part(TEXT("Type"), Got.Type, Got.bHasType, Want.Find("type"));
		Part(TEXT("Id"), Got.Id, Got.bHasId, Want.Find("id"));
		Part(TEXT("Lens"), Got.Lens, Got.bHasLens, Want.Find("lens"));
		if (Got.Form != TextOf(Want.Find("form"))) Why += TEXT(" Form=") + Got.Form;
		const OWJsonValue* M = Want.Find("moment");
		const bool bMoment = M && M->IsNumber();
		if (Got.bHasMoment != bMoment || (bMoment && Got.Moment != std::stoll(M->Text())))
			Why += FString::Printf(TEXT(" Moment=%lld(%s)"), Got.Moment, Got.bHasMoment ? TEXT("given") : TEXT("absent"));
		const OWJsonValue* Slug = Want.Find("world_is_slug");
		if (Got.bWorldIsSlug != (Slug && Slug->IsBool() && Slug->AsBool())) Why += TEXT(" bWorldIsSlug");
		const OWJsonValue* Canon = Want.Find("canonical");
		if (Got.bCanonical != (Canon && Canon->IsBool() && Canon->AsBool())) Why += TEXT(" bCanonical");
		return Why.IsEmpty();
	}

	bool IsEmptyAddress(const FOWAddress& A)
	{
		return A.Form.IsEmpty() && A.World.IsEmpty() && A.Type.IsEmpty() && A.Id.IsEmpty() && A.Lens.IsEmpty() && A.Moment == 0
			&& !A.bHasWorld && !A.bHasType && !A.bHasId && !A.bHasLens && !A.bHasMoment && !A.bWorldIsSlug && !A.bCanonical;
	}

	const TCHAR* const SampleWorld = TEXT("0199c3a1-7b2e-7c3d-8e4f-5a6b7c8d9e0f");
	const TCHAR* const SampleElement = TEXT("019a1f3c-2b4e-7d10-8c3a-6f2e9b0d4a71");
	const TCHAR* const SampleLens = TEXT("019a2b7e-4c1d-7f20-9b3e-1a2c3d4e5f60");

	FOWAddress Parts(const TCHAR* Form, const TCHAR* World, const TCHAR* Type, const TCHAR* Id)
	{
		FOWAddress A;
		A.Form = Form;
		A.World = World ? World : TEXT("");
		A.Type = Type ? Type : TEXT("");
		A.Id = Id ? Id : TEXT("");
		return A;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWAddressFixtureTest, "OnlyWorlds.Address.Fixture", OWTestFlags)

bool FOWAddressFixtureTest::RunTest(const FString& Parameters)
{
	const FString Path = OWTestPaths::InRepo(TEXT("codegen/schema-dist/profiles/address.fixture.json"));
	OWJsonValue Doc;
	if (!OWTestPaths::ReadJson(Path, Doc))
	{
		AddError(TEXT("no address fixture at ") + Path);
		return false;
	}
	const OWJsonValue* Cases = Doc.Find("cases");
	if (!TestTrue(TEXT("the fixture holds cases"), Cases && Cases->IsArray())) return false;

	int32 Parsed = 0, Written = 0, Failed = 0, RoundTrips = 0;
	TArray<FString> NotCarried;
	for (const OWJsonValue& Case : Cases->Items())
	{
		const FString Name = TextOf(Case.Find("name"));
		const FString Kind = TextOf(Case.Find("kind"));
		const FString Refuse = TextOf(Case.Find("refuse"));
		const int32 ErrorsBefore = ExecutionInfo.GetErrorTotal();
		if (Kind == TEXT("parse"))
		{
			++Parsed;
			const FString Text = TextOf(Case.Find("text"));
			const FParsed R = CallParse(*this, Text);
			if (!Refuse.IsEmpty())
			{
				TestFalse(Name + TEXT(": refused"), R.bOk);
				TestEqual(Name + TEXT(": the reason"), R.Reason, Refuse);
				TestTrue(Name + TEXT(": a refusal leaves the address empty"), IsEmptyAddress(R.Address));
			}
			else
			{
				TestTrue(Name + TEXT(": read (refused as ") + R.Reason + TEXT(")"), R.bOk);
				TestEqual(Name + TEXT(": no reason"), R.Reason, FString());
				FString Why;
				const OWJsonValue* Want = Case.Find("expect");
				const bool bSame = Want && SameReading(R.Address, *Want, Why);
				TestTrue(Name + TEXT(": the reading the fixture gives;") + Why, bSame);

				// The reading's out struct wired into WriteAddress, as a graph wires a Break into a Make: what the
				// core's writer writes for this reading (a slug refused as bad_world).
				const OnlyWorlds::OWAddressWritten Core = OnlyWorlds::OWAddress::Write(OnlyWorlds::OWAddress::Parse(OWConvert::Utf8(Text)));
				const FWritten Back = CallWrite(*this, R.Address);
				TestEqual(Name + TEXT(": written back as the core writes it"), Back.Text, Str(Core.address));
				TestEqual(Name + TEXT(": or refused as the core refuses it"), Back.Reason, Str(Core.reason));
				if (R.Address.bCanonical) TestEqual(Name + TEXT(": a canonical string is what the writer writes"), Back.Text, Text);
				++RoundTrips;
			}
		}
		else if (Kind == TEXT("format"))
		{
			const OWJsonValue* Json = Case.Find("address");
			if (!TestTrue(Name + TEXT(": an address"), Json && Json->IsObject())) continue;
			FOWAddress A;
			A.Form = TextOf(Json->Find("form"));
			PartOf(*Json, "world", A.World, A.bHasWorld);
			PartOf(*Json, "type", A.Type, A.bHasType);
			PartOf(*Json, "id", A.Id, A.bHasId);
			PartOf(*Json, "lens", A.Lens, A.bHasLens);
			// A moment the door's Integer64 pin can hold: a whole number. A fraction, a string or a boolean cannot
			// be set on the pin at all, so the case is not carried (and is named).
			if (const OWJsonValue* M = Json->Find("moment"); M && !M->IsNull())
			{
				const bool bWhole = M->IsNumber() && M->Text().find_first_of(".eE") == std::string::npos;
				if (!bWhole)
				{
					NotCarried.Add(FString::Printf(TEXT("%s (moment %s)"), *Name, *Str(OnlyWorlds::OWJson::Stringify(*M))));
					continue;
				}
				A.Moment = std::stoll(M->Text());
				A.bHasMoment = true;
			}
			++Written;
			const FWritten W = CallWrite(*this, A);
			if (!Refuse.IsEmpty())
			{
				TestFalse(Name + TEXT(": refused (wrote ") + W.Text + TEXT(")"), W.bOk);
				TestEqual(Name + TEXT(": the reason"), W.Reason, Refuse);
				TestEqual(Name + TEXT(": no text"), W.Text, FString());
			}
			else
			{
				TestTrue(Name + TEXT(": written (refused as ") + W.Reason + TEXT(")"), W.bOk);
				TestEqual(Name + TEXT(": the address"), W.Text, TextOf(Case.Find("expect")));
				TestEqual(Name + TEXT(": no reason"), W.Reason, FString());
			}
		}
		else
		{
			AddError(Name + TEXT(": a case of unknown kind ") + Kind);
		}
		if (ExecutionInfo.GetErrorTotal() > ErrorsBefore) ++Failed;
	}
	TestEqual(TEXT("140 cases"), static_cast<int32>(Cases->Items().size()), 140);
	TestEqual(TEXT("every parse case through ParseAddress"), Parsed, 104);
	TestEqual(TEXT("the format cases through WriteAddress"), Written, 33);
	// The three moments no Integer64 pin holds; the core's own runner reads them through WriteJson.
	TestEqual(TEXT("not carried: a moment that is not a whole number"), NotCarried.Num(), 3);
	TestEqual(TEXT("cases failed"), Failed, 0);
	AddInfo(FString::Printf(TEXT("address fixture: %d parse cases (%d readings written back), %d format cases through the "
		"Blueprint path; not carried, by the pin's type: %s"), Parsed, RoundTrips, Written, *FString::Join(NotCarried, TEXT(", "))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWAddressWorldTest, "OnlyWorlds.Address.World", OWTestFlags)

bool FOWAddressWorldTest::RunTest(const FString& Parameters)
{
	UOWWorld* World = OWTestPaths::OpenWorld(*this, OWTestPaths::Sikelia(), TEXT("OW_SIKELIA"));
	if (!World) return false;
	const FString Id = World->GetId();

	// Every element of the world, in the three spellings: written through the door, then read back through it to the
	// same parts, canonical; each answer the core's own for the same parts.
	int32 Elements = 0, Written = 0, Same = 0;
	TMap<FString, int32> Refused;
	const TCHAR* const Spellings[][2] = { { TEXT("human"), TEXT("world") }, { TEXT("text"), TEXT("world") }, { TEXT("text"), nullptr } };
	for (const OnlyWorlds::OWElement& E : World->Native()->elements)
	{
		++Elements;
		for (const auto& Spelling : Spellings)
		{
			FOWAddress A = Parts(Spelling[0], Spelling[1] ? *Id : nullptr, *Str(E.Type()), *Str(E.Id()));
			A.Moment = Elements % 7 == 0 ? -(Elements * 13) : Elements;
			A.bHasMoment = Elements % 3 == 0;
			OnlyWorlds::OWAddressParts Core;
			Core.form = OWConvert::Utf8(A.Form);
			if (Spelling[1]) Core.world = OWConvert::Utf8(Id);
			Core.type = E.Type();
			Core.id = E.Id();
			if (A.bHasMoment) Core.moment = static_cast<int32_t>(A.Moment);
			const OnlyWorlds::OWAddressWritten Want = OnlyWorlds::OWAddress::Write(Core);

			const FWritten W = CallWrite(*this, A);
			bool bSame = W.bOk == Want.ok && W.Text == Str(Want.address) && W.Reason == Str(Want.reason);
			if (W.bOk)
			{
				++Written;
				const FParsed R = CallParse(*this, W.Text);
				bSame = bSame && R.bOk && R.Address.bCanonical && R.Address.Form == A.Form
					&& R.Address.bHasWorld == (Spelling[1] != nullptr) && R.Address.World == (Spelling[1] ? Id.ToLower() : FString())
					&& R.Address.bHasType && R.Address.Type == A.Type && R.Address.bHasId && R.Address.Id == A.Id.ToLower()
					&& R.Address.bHasMoment == A.bHasMoment && R.Address.Moment == (A.bHasMoment ? A.Moment : 0) && !R.Address.bHasLens;
			}
			else
			{
				Refused.FindOrAdd(W.Reason)++;
			}
			if (bSame) ++Same;
			else if (Elements * 3 - Same <= 3) AddError(FString::Printf(TEXT("%s %s: \"%s\" (%s), the core \"%s\" (%s)"),
				Spelling[0], *A.Id, *W.Text, *W.Reason, *Str(Want.address), *Str(Want.reason)));
		}
	}
	TestTrue(TEXT("Sikelia holds elements"), Elements > 0);
	TestEqual(TEXT("every element's three addresses as the core writes them, read back to the same parts"), Same, Elements * 3);
	FString RefusedText;
	for (const TPair<FString, int32>& R : Refused) RefusedText += FString::Printf(TEXT(" %s %d"), *R.Key, R.Value);
	AddInfo(FString::Printf(TEXT("Sikelia (%s): %d elements, %d addresses written and read back; refused:%s"), *Id, Elements,
		Written, RefusedText.IsEmpty() ? TEXT(" none") : *RefusedText));

	// The world's own address, both forms.
	FOWAddress Own = Parts(TEXT("human"), *Id, nullptr, nullptr);
	TestEqual(TEXT("the world's page"), CallWrite(*this, Own).Text, TEXT("https://www.onlyworlds.com/w/") + Id.ToLower());
	Own.Form = TEXT("text");
	const FWritten Text = CallWrite(*this, Own);
	TestEqual(TEXT("the world's text address"), Text.Text, TEXT("ow://") + Id.ToLower());
	const FParsed Read = CallParse(*this, Text.Text);
	TestTrue(TEXT("read back: a world, no type, no id"), Read.bOk && Read.Address.bHasWorld && !Read.Address.bHasType
		&& !Read.Address.bHasId && Read.Address.Type.IsEmpty() && Read.Address.Id.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWAddressPartsTest, "OnlyWorlds.Address.Parts", OWTestFlags)

bool FOWAddressPartsTest::RunTest(const FString& Parameters)
{
	auto Reason = [this](const FOWAddress& A) { return CallWrite(*this, A).Reason; };
	auto Text = [this](const FOWAddress& A) { return CallWrite(*this, A).Text; };
	const FString Element = SampleElement;

	// An empty string with its flag set is given, and refuses by that part's own reason (serial 97).
	FOWAddress A = Parts(TEXT("text"), TEXT(""), TEXT("character"), SampleElement);
	A.bHasWorld = true;
	TestEqual(TEXT("world \"\" given: bad_world"), Reason(A), FString(TEXT("bad_world")));
	A.bHasWorld = false;
	TestEqual(TEXT("world \"\" not given: absent, the text form inside a world"), Text(A), TEXT("ow://character/") + Element);
	A = Parts(TEXT("text"), SampleWorld, TEXT(""), SampleElement);
	A.bHasType = true;
	TestEqual(TEXT("type \"\" given: unknown_type"), Reason(A), FString(TEXT("unknown_type")));
	A.bHasType = false;
	TestEqual(TEXT("type \"\" not given, an id: segments"), Reason(A), FString(TEXT("segments")));
	A = Parts(TEXT("text"), SampleWorld, TEXT("character"), TEXT(""));
	A.bHasId = true;
	TestEqual(TEXT("id \"\" given: bad_id"), Reason(A), FString(TEXT("bad_id")));
	A = Parts(TEXT("text"), SampleWorld, nullptr, nullptr);
	A.bHasLens = true;
	TestEqual(TEXT("lens \"\" given: bad_lens"), Reason(A), FString(TEXT("bad_lens")));
	A.bHasLens = false;
	TestEqual(TEXT("lens \"\" not given: no lens"), Text(A), TEXT("ow://") + FString(SampleWorld));

	// A string that is not empty is given, its flag set or not.
	A = Parts(TEXT("text"), SampleWorld, TEXT("location"), SampleElement);
	A.Lens = SampleLens;
	TestEqual(TEXT("parts given by their strings alone"), Text(A),
		FString::Printf(TEXT("ow://%s/location/%s?lens=%s"), SampleWorld, SampleElement, SampleLens));

	// The moment: given by its flag alone (0 is a step), within 32 bits.
	A = Parts(TEXT("human"), SampleWorld, nullptr, nullptr);
	TestEqual(TEXT("no moment"), Text(A), TEXT("https://www.onlyworlds.com/w/") + FString(SampleWorld));
	A.bHasMoment = true;
	TestEqual(TEXT("moment 0 given"), Text(A), TEXT("https://www.onlyworlds.com/w/") + FString(SampleWorld) + TEXT("@0"));
	A.Moment = 7;
	A.bHasMoment = false;
	TestEqual(TEXT("moment 7 not given: none"), Text(A), TEXT("https://www.onlyworlds.com/w/") + FString(SampleWorld));
	A = Parts(TEXT("text"), nullptr, TEXT("event"), SampleElement);
	A.bHasMoment = true;
	const struct { int64 Moment; const TCHAR* Want; } Moments[] = {
		{ MAX_int32, TEXT("@2147483647") }, { MIN_int32, TEXT("@-2147483648") },
		{ static_cast<int64>(MAX_int32) + 1, nullptr }, { static_cast<int64>(MIN_int32) - 1, nullptr },
		{ 5000000000LL, nullptr }, { 4294967296LL + 5, nullptr },
	};
	for (const auto& M : Moments)
	{
		A.Moment = M.Moment;
		const FWritten W = CallWrite(*this, A);
		if (M.Want) TestEqual(FString::Printf(TEXT("moment %lld"), M.Moment), W.Text, TEXT("ow://event/") + Element + M.Want);
		else TestEqual(FString::Printf(TEXT("moment %lld: bad_moment"), M.Moment), W.Reason, FString(TEXT("bad_moment")));
	}
	// The writer's order holds past 32 bits: no_world before the moment, bad_world, unknown_type and bad_id too.
	A = Parts(TEXT("human"), nullptr, TEXT("event"), SampleElement);
	A.Moment = 5000000000LL;
	A.bHasMoment = true;
	TestEqual(TEXT("no_world before a moment past 32 bits"), Reason(A), FString(TEXT("no_world")));
	A.World = TEXT("hyperion");
	TestEqual(TEXT("bad_world before it"), Reason(A), FString(TEXT("bad_world")));
	A.World = SampleWorld;
	A.Id = TEXT("e1");
	TestEqual(TEXT("bad_id before it"), Reason(A), FString(TEXT("bad_id")));
	A.Id = SampleElement;
	A.Lens = TEXT("noir");
	TestEqual(TEXT("bad_moment before bad_lens"), Reason(A), FString(TEXT("bad_moment")));

	// The form: "human" or "text", lowercase; "" is no form.
	TestEqual(TEXT("form \"\": bad_form"), Reason(Parts(TEXT(""), SampleWorld, nullptr, nullptr)), FString(TEXT("bad_form")));
	TestEqual(TEXT("form \"Human\": bad_form"), Reason(Parts(TEXT("Human"), SampleWorld, nullptr, nullptr)), FString(TEXT("bad_form")));
	// A reading's two flags are ignored by the writer.
	A = Parts(TEXT("text"), SampleWorld, nullptr, nullptr);
	A.bWorldIsSlug = true;
	A.bCanonical = true;
	TestEqual(TEXT("bWorldIsSlug and bCanonical ignored"), Text(A), TEXT("ow://") + FString(SampleWorld));

	// A refused write leaves no text; a refused read leaves an empty address, over a stale one.
	TestEqual(TEXT("refused: no text"), Text(Parts(TEXT("human"), nullptr, nullptr, nullptr)), FString());
	FOWAddress Stale = Parts(TEXT("text"), SampleWorld, TEXT("character"), SampleElement);
	Stale.bHasWorld = Stale.bHasType = Stale.bHasId = Stale.bHasMoment = Stale.bCanonical = true;
	Stale.Moment = 12;
	const FParsed Bad = CallParse(*this, TEXT("ow://dragon/") + Element, &Stale);
	TestTrue(TEXT("refused: unknown_type, the address empty"), !Bad.bOk && Bad.Reason == TEXT("unknown_type") && IsEmptyAddress(Bad.Address));
	const FParsed Good = CallParse(*this, TEXT("ow://object/") + Element, &Stale);
	TestTrue(TEXT("read over a stale one: no world, no moment"), Good.bOk && Good.Reason.IsEmpty() && !Good.Address.bHasWorld
		&& Good.Address.World.IsEmpty() && !Good.Address.bHasMoment && Good.Address.Moment == 0 && Good.Address.bCanonical);
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
