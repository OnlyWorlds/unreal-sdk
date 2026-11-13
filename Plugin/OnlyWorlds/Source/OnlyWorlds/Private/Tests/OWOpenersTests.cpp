// The Open with rows through UOWOpeners as a Blueprint calls it (OWBlueprintCall.h): OpenersFor and OpenersForAddress
// found by name, the surface and the address set member by member (as Make nodes), ProcessEvent, the rows read back
// from the out array by property. On a real world (Sikelia) with the baked tools list, then the packages' shared
// fixture (tests/data/openers.fixture.json) wherever the door can carry a case.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "OWAddress.h"
#include "OWConvert.h"
#include "OWJson.h"
#include "OWOpeners.h"
#include "OWOpenersLibrary.h"
#include "OWWorld.h"
#include "OWWorldObject.h"
#include "OWAddressMembers.h"
#include "OWBlueprintCall.h"
#include "OWTestPaths.h"

#include <algorithm>
#include <string>
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

	void MakeSurface(FAutomationTestBase& Test, void* Data, const FOWOpenerSurface& S)
	{
		using namespace OWBlueprintTest;
		const UScriptStruct* T = FOWOpenerSurface::StaticStruct();
		SetEnumMember(Test, T, Data, TEXT("Audience"), StaticEnum<EOWAudience>(), static_cast<int64>(S.Audience));
		SetMember<FStrProperty>(Test, T, Data, TEXT("Tool"), S.Tool);
		SetEnumMember(Test, T, Data, TEXT("SignIn"), StaticEnum<EOWSignIn>(), static_cast<int64>(S.SignIn));
		SetBoolMember(Test, T, Data, TEXT("bBrowser"), S.bBrowser);
		SetBoolMember(Test, T, Data, TEXT("bPhone"), S.bPhone);
		SetBoolMember(Test, T, Data, TEXT("bOnline"), S.bOnline);
	}

	struct FRow
	{
		FString Id, Name, Href, State;
		bool operator==(const FRow& O) const { return Id == O.Id && Name == O.Name && Href == O.Href && State == O.State; }
		FString Show() const { return FString::Printf(TEXT("{%s, %s, %s, \"%s\"}"), *Id, *Name, *Href, *State); }
	};

	struct FMenu
	{
		bool bOk = false;
		TArray<FRow> Rows;
		FString Reason;
		FString Show() const
		{
			TArray<FString> Parts;
			for (const FRow& R : Rows) Parts.Add(R.Show());
			return bOk ? FString::Join(Parts, TEXT(" ")) : TEXT("refused: ") + Reason;
		}
	};

	// The answer: the bool, the out array's rows (each FOWOpener member by its property) and the reason.
	FMenu ReadMenu(FAutomationTestBase& Test, FCall& Call)
	{
		FMenu M;
		M.bOk = Call.ReturnBool();
		M.Reason = Call.Out<FStrProperty>(TEXT("Reason"));
		const FArrayProperty* Rows = CastField<FArrayProperty>(Call.Property(TEXT("Rows")));
		const FStructProperty* Inner = Rows ? CastField<FStructProperty>(Rows->Inner) : nullptr;
		if (!Inner || Inner->Struct != FOWOpener::StaticStruct() || !OWBlueprintTest::IsOutputPin(Rows) || !Call.GetFrame())
		{
			Test.AddError(FString::Printf(TEXT("%s has no TArray<FOWOpener> out parameter Rows"), Call.GetName()));
			return M;
		}
		FScriptArrayHelper Array(Rows, Rows->ContainerPtrToValuePtr<void>(Call.GetFrame()));
		for (int32 I = 0; I < Array.Num(); ++I)
		{
			const void* R = Array.GetRawPtr(I);
			const UScriptStruct* S = Inner->Struct;
			M.Rows.Add({ Member<FStrProperty>(Test, S, R, TEXT("Id")), Member<FStrProperty>(Test, S, R, TEXT("Name")),
				Member<FStrProperty>(Test, S, R, TEXT("Href")), Member<FStrProperty>(Test, S, R, TEXT("State")) });
		}
		return M;
	}

	FMenu CallOpenersFor(FAutomationTestBase& Test, const UOWWorld* World, const FString& ElementId, const FOWOpenerSurface& Surface)
	{
		FCall Call(Test, UOWOpeners::StaticClass(), TEXT("OpenersFor"));
		Call.SetWorld(World);
		Call.SetString(TEXT("ElementId"), ElementId);
		if (void* S = Call.StructSlot(TEXT("Surface"), FOWOpenerSurface::StaticStruct())) MakeSurface(Test, S, Surface);
		Call.Invoke();
		return ReadMenu(Test, Call);
	}

	FMenu CallOpenersForAddress(FAutomationTestBase& Test, const FOWAddress& Address, const FOWOpenerSurface& Surface)
	{
		FCall Call(Test, UOWOpeners::StaticClass(), TEXT("OpenersForAddress"));
		if (void* A = Call.StructSlot(TEXT("Address"), FOWAddress::StaticStruct())) OWAddressMembers::MakeAddress(Test, A, Address);
		if (void* S = Call.StructSlot(TEXT("Surface"), FOWOpenerSurface::StaticStruct())) MakeSurface(Test, S, Surface);
		Call.Invoke();
		return ReadMenu(Test, Call);
	}

	// The core's rows for the same question, with the baked list.
	OnlyWorlds::OWOpenerSurface CoreSurface(const FOWOpenerSurface& S)
	{
		OnlyWorlds::OWOpenerSurface C;
		if (!S.Tool.IsEmpty()) C.tool = Utf8(S.Tool);
		C.audience = S.Audience == EOWAudience::Member ? "member" : "public";
		if (S.SignIn != EOWSignIn::Unknown) C.signed_in = S.SignIn == EOWSignIn::SignedIn;
		C.browser = S.bBrowser;
		C.phone = S.bPhone;
		C.online = S.bOnline;
		return C;
	}

	FMenu CoreMenu(const OnlyWorlds::OWOpenersResult& R)
	{
		FMenu M;
		M.bOk = R.ok;
		M.Reason = Str(R.reason.empty() ? R.message : R.reason);
		for (const OnlyWorlds::OWOpener& O : R.rows) M.Rows.Add({ Str(O.id), Str(O.name), Str(O.href), O.state ? Str(*O.state) : FString() });
		return M;
	}

	bool SameMenu(const FMenu& A, const FMenu& B) { return A.bOk == B.bOk && A.Reason == B.Reason && A.Rows == B.Rows; }

	FOWOpenerSurface Surface(EOWAudience Audience, bool bBrowser = false, EOWSignIn SignIn = EOWSignIn::Unknown,
		const TCHAR* Tool = TEXT(""), bool bPhone = false)
	{
		FOWOpenerSurface S;
		S.Audience = Audience;
		S.bBrowser = bBrowser;
		S.SignIn = SignIn;
		S.Tool = Tool;
		S.bPhone = bPhone;
		return S;
	}

	FString TextOf(const OWJsonValue* V) { return V && V->IsString() ? Str(V->Text()) : FString(); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWOpenersWorldTest, "OnlyWorlds.Openers.World", OWTestFlags)

bool FOWOpenersWorldTest::RunTest(const FString& Parameters)
{
	UOWWorld* World = OWTestPaths::OpenWorld(*this, OWTestPaths::Sikelia(), TEXT("OW_SIKELIA"));
	if (!World) return false;
	const FString Id = World->GetId().ToLower();
	const FString Site = TEXT("https://www.onlyworlds.com/w/") + Id;
	const TArray<FString> Characters = World->GetElementIds(TEXT("character"));
	if (!TestTrue(TEXT("Sikelia has characters"), Characters.Num() > 0)) return false;
	const FString Ana = Characters[0].ToLower();
	const FString AnaPage = Site + TEXT("/character/") + Ana;

	// The world's own rows. The baked list (Atlas, Lantern, Little Lens, Tactical Tangle, Subs): Lantern, Tangle and
	// Subs need an account, so the public (the signed-out reader) sees each as a row that says "Needs an account", its
	// href kept: the menu draws it as one that opens nothing. Atlas needs one too but opens a public world with none
	// (opens_public, the list at 9452529), so the public's Atlas row is live. Little Lens needs none. Tangle opens a
	// world or a Character (its element_types), so it is on the world's menu and on Ana's; Subs opens a Character and
	// has no world door (the list at 0a7539c), so it is on Ana's menu, last, and not on the world's.
	const FString NoAccount = TEXT("Needs an account");
	FMenu M = CallOpenersFor(*this, World, FString(), Surface(EOWAudience::Public));
	TestTrue(TEXT("public, the world: the site's page, Atlas, Lantern needing an account, Little Lens, Tangle needing one (") + M.Show() + TEXT(")"),
		M.bOk && M.Reason.IsEmpty() && M.Rows.Num() == 5 && M.Rows[0] == FRow{ TEXT("onlyworlds"), TEXT("onlyworlds.com"), Site, FString() }
		&& M.Rows[1] == FRow{ TEXT("atlas"), TEXT("Atlas"), TEXT("https://atlas.onlyworlds.com/#/open/") + Id, FString() }
		&& M.Rows[2] == FRow{ TEXT("lantern"), TEXT("Lantern"), TEXT("https://lantern.onlyworlds.com/#/open/") + Id, NoAccount }
		&& M.Rows[3] == FRow{ TEXT("little-lens"), TEXT("Little Lens"), TEXT("https://little-lens.onlyworlds.com/#/open/") + Id, FString() }
		&& M.Rows[4] == FRow{ TEXT("tangle"), TEXT("Tactical Tangle"), TEXT("https://tangle.onlyworlds.com/#/open/") + Id, NoAccount });
	M = CallOpenersFor(*this, World, Characters[0], Surface(EOWAudience::Public));
	TestTrue(TEXT("public, an element: its page, Atlas, Lantern needing an account, Little Lens, Tangle and Subs needing one (") + M.Show() + TEXT(")"),
		M.bOk && M.Rows.Num() == 6 && M.Rows[0] == FRow{ TEXT("onlyworlds"), TEXT("onlyworlds.com"), AnaPage, FString() }
		&& M.Rows[1] == FRow{ TEXT("atlas"), TEXT("Atlas"), TEXT("https://atlas.onlyworlds.com/#/open/") + Id + TEXT("/element/") + Ana, FString() }
		&& M.Rows[2] == FRow{ TEXT("lantern"), TEXT("Lantern"), TEXT("https://lantern.onlyworlds.com/#/open/") + Id + TEXT("/element/") + Ana, NoAccount }
		&& M.Rows[3] == FRow{ TEXT("little-lens"), TEXT("Little Lens"), TEXT("https://little-lens.onlyworlds.com/#/open/") + Id + TEXT("/element/") + Ana, FString() }
		&& M.Rows[4] == FRow{ TEXT("tangle"), TEXT("Tactical Tangle"), TEXT("https://tangle.onlyworlds.com/#/open/") + Id + TEXT("/element/") + Ana, NoAccount }
		&& M.Rows[5] == FRow{ TEXT("subs"), TEXT("Subs"), TEXT("https://subs.onlyworlds.com/#/open/") + Id + TEXT("/element/") + Ana, NoAccount });
	// Signed in or not, in a browser or not: the public's account rows say the same.
	M = CallOpenersFor(*this, World, Characters[0], Surface(EOWAudience::Public, true, EOWSignIn::SignedIn));
	TestTrue(TEXT("public, signed in, in a browser: still Needs an account (") + M.Show() + TEXT(")"),
		M.bOk && M.Rows.Num() == 6 && M.Rows[1].State.IsEmpty() && M.Rows[2].State == NoAccount && M.Rows[3].State.IsEmpty()
		&& M.Rows[4].State == NoAccount && M.Rows[5].State == NoAccount);

	// A member in a game: Atlas, Lantern, Tangle and Subs need an account it cannot see, so each says "Opens in your
	// browser"; Little Lens needs none.
	M = CallOpenersFor(*this, World, Characters[0], Surface(EOWAudience::Member));
	const TArray<FRow> Game = {
		{ TEXT("onlyworlds"), TEXT("onlyworlds.com"), AnaPage, FString() },
		{ TEXT("atlas"), TEXT("Atlas"), TEXT("https://atlas.onlyworlds.com/#/open/") + Id + TEXT("/element/") + Ana, TEXT("Opens in your browser") },
		{ TEXT("lantern"), TEXT("Lantern"), TEXT("https://lantern.onlyworlds.com/#/open/") + Id + TEXT("/element/") + Ana, TEXT("Opens in your browser") },
		{ TEXT("little-lens"), TEXT("Little Lens"), TEXT("https://little-lens.onlyworlds.com/#/open/") + Id + TEXT("/element/") + Ana, FString() },
		{ TEXT("tangle"), TEXT("Tactical Tangle"), TEXT("https://tangle.onlyworlds.com/#/open/") + Id + TEXT("/element/") + Ana, TEXT("Opens in your browser") },
		{ TEXT("subs"), TEXT("Subs"), TEXT("https://subs.onlyworlds.com/#/open/") + Id + TEXT("/element/") + Ana, TEXT("Opens in your browser") },
	};
	TestTrue(TEXT("a member in a game: six rows, each account tool's word (") + M.Show() + TEXT(")"), M.bOk && M.Rows == Game);
	M = CallOpenersFor(*this, World, FString(), Surface(EOWAudience::Member));
	TestTrue(TEXT("the world's rows in a game (") + M.Show() + TEXT(")"), M.bOk && M.Rows.Num() == 5
		&& M.Rows[1].Href == TEXT("https://atlas.onlyworlds.com/#/open/") + Id && M.Rows[2].State == TEXT("Opens in your browser")
		&& M.Rows[3].State.IsEmpty() && M.Rows[4].Href == TEXT("https://tangle.onlyworlds.com/#/open/") + Id
		&& M.Rows[4].State == TEXT("Opens in your browser"));
	// On a phone too: no tool of the list needs the device, so nothing changes.
	TestTrue(TEXT("a phone changes no row of the baked list"),
		CallOpenersFor(*this, World, Characters[0], Surface(EOWAudience::Member, false, EOWSignIn::Unknown, TEXT(""), true)).Rows == Game);

	// Offline: every row of the baked list is a web address, so each says "Needs a connection", the site's included,
	// before the browser's word, and keeps its href. A surface left as it is made is online.
	FOWOpenerSurface Offline = Surface(EOWAudience::Member);
	Offline.bOnline = false;
	M = CallOpenersFor(*this, World, Characters[0], Offline);
	bool bAllOffline = M.bOk && M.Rows.Num() == Game.Num();
	for (int32 I = 0; bAllOffline && I < M.Rows.Num(); ++I)
		bAllOffline = M.Rows[I].Href == Game[I].Href && M.Rows[I].State == TEXT("Needs a connection");
	TestTrue(TEXT("offline: every row says Needs a connection, its href kept (") + M.Show() + TEXT(")"), bAllOffline);
	Offline.Tool = TEXT("atlas");
	M = CallOpenersFor(*this, World, FString(), Offline);
	TestTrue(TEXT("offline, Atlas's own surface: You are here first (") + M.Show() + TEXT(")"), M.bOk && M.Rows.Num() == 5
		&& M.Rows[0].State == TEXT("Needs a connection") && M.Rows[1].State == TEXT("You are here")
		&& M.Rows[2].State == TEXT("Needs a connection") && M.Rows[3].State == TEXT("Needs a connection")
		&& M.Rows[4].State == TEXT("Needs a connection"));
	TestTrue(TEXT("a surface as it is made is online"), FOWOpenerSurface().bOnline);

	// In a browser: the sign-in decides, and only when it is known.
	M = CallOpenersFor(*this, World, Characters[0], Surface(EOWAudience::Member, true, EOWSignIn::SignedOut));
	TestTrue(TEXT("signed out in a browser: Sign in to <name> first (") + M.Show() + TEXT(")"), M.bOk && M.Rows.Num() == 6
		&& M.Rows[1].State == TEXT("Sign in to Atlas first") && M.Rows[2].State == TEXT("Sign in to Lantern first") && M.Rows[3].State.IsEmpty()
		&& M.Rows[4].State == TEXT("Sign in to Tactical Tangle first") && M.Rows[5].State == TEXT("Sign in to Subs first"));
	for (EOWSignIn SignIn : { EOWSignIn::SignedIn, EOWSignIn::Unknown })
	{
		M = CallOpenersFor(*this, World, Characters[0], Surface(EOWAudience::Member, true, SignIn));
		TestTrue(FString::Printf(TEXT("sign-in %d in a browser: no word (%s)"), static_cast<int32>(SignIn), *M.Show()),
			M.bOk && M.Rows.Num() == 6 && M.Rows[1].State.IsEmpty() && M.Rows[2].State.IsEmpty() && M.Rows[3].State.IsEmpty()
			&& M.Rows[4].State.IsEmpty() && M.Rows[5].State.IsEmpty());
	}
	// The surface's own tool, by its client name: You are here, before any other word.
	M = CallOpenersFor(*this, World, Characters[0], Surface(EOWAudience::Member, false, EOWSignIn::SignedOut, TEXT("atlas")));
	TestTrue(TEXT("Atlas's own surface: You are here (") + M.Show() + TEXT(")"), M.bOk && M.Rows.Num() == 6
		&& M.Rows[1].State == TEXT("You are here") && M.Rows[2].State == TEXT("Opens in your browser") && M.Rows[3].State.IsEmpty()
		&& M.Rows[4].State == TEXT("Opens in your browser") && M.Rows[5].State == TEXT("Opens in your browser"));

	// A tool whose element door opens only some types (the list's element_types) is left off every other type's menu,
	// world door and all: Tangle opens Characters, so a Location's menu has no Tangle row, through either door.
	const TArray<FString> Locations = World->GetElementIds(TEXT("location"));
	if (TestTrue(TEXT("Sikelia has locations"), Locations.Num() > 0))
	{
		const auto NoTangle = [](const FMenu& Menu) { return Menu.bOk && !Menu.Rows.ContainsByPredicate([](const FRow& R) { return R.Id == TEXT("tangle"); }); };
		M = CallOpenersFor(*this, World, Locations[0], Surface(EOWAudience::Member));
		TestTrue(TEXT("a Location's menu: four rows, no Tangle (") + M.Show() + TEXT(")"), NoTangle(M) && M.Rows.Num() == 4);
		FOWAddress L;
		L.World = Id;
		L.bHasWorld = true;
		L.Type = TEXT("location");
		L.bHasType = true;
		L.Id = Locations[0].ToLower();
		L.bHasId = true;
		M = CallOpenersForAddress(*this, L, Surface(EOWAudience::Member));
		TestTrue(TEXT("a Location by its address: no Tangle (") + M.Show() + TEXT(")"), NoTangle(M) && M.Rows.Num() == 4);
	}

	// Every element of the world, as a member in a game and as the public: the core's rows for the same parts and the
	// element as served (its marks skip a door: Sikelia's Maps, Pins and Markers have no Little Lens row).
	int32 Elements = 0, Same = 0, Skipped = 0, Unskipped = 0, Marked = 0, TangleOffered = 0, TangleWrong = 0;
	TMap<FString, int32> Refused;
	std::vector<std::string> LensSkips, TangleSkips;
	for (const OnlyWorlds::OWTool& T : OnlyWorlds::OWTools::All())
	{
		if (T.id == "little-lens" && T.open) LensSkips = T.open->element_skips;
		if (T.id == "tangle" && T.open) TangleSkips = T.open->element_skips;
	}
	TestTrue(TEXT("the baked list's Little Lens row skips marks"), !LensSkips.empty());
	for (const OnlyWorlds::OWElement& E : World->Native()->elements)
	{
		const OWJsonValue Served = UOWOpeners::ServedElement(E);
		OnlyWorlds::OWOpenersOptions Options;
		Options.element = &Served;
		bool bMarked = false;
		for (const std::string& Mark : OnlyWorlds::OWOpeners::MarksOf(Served))
			bMarked = bMarked || std::find(LensSkips.begin(), LensSkips.end(), Mark) != LensSkips.end();
		if (bMarked) ++Marked;
		// Tangle's row: on a Character its door does not skip, and on no other type (its element_types).
		bool bTangleHere = E.Type() == "character";
		for (const std::string& Mark : OnlyWorlds::OWOpeners::MarksOf(Served))
			bTangleHere = bTangleHere && std::find(TangleSkips.begin(), TangleSkips.end(), Mark) == TangleSkips.end();
		for (const FOWOpenerSurface& S : { Surface(EOWAudience::Member), Surface(EOWAudience::Public, true, EOWSignIn::SignedIn) })
		{
			++Elements;
			OnlyWorlds::OWAddressParts P;
			P.form = OnlyWorlds::OWAddress::Human;
			P.world = World->Native()->Id();
			P.type = E.Type();
			P.id = E.Id();
			const FMenu Want = CoreMenu(OnlyWorlds::OWOpeners::OpenersFor(OnlyWorlds::OWTools::All(), OnlyWorlds::OWOpeners::Element, P, CoreSurface(S), Options));
			const FMenu Got = CallOpenersFor(*this, World, Str(E.Id()), S);
			if (SameMenu(Got, Want)) ++Same;
			else if (Elements - Same <= 3) AddError(FString::Printf(TEXT("%s: %s, the core %s"), *Str(E.Id()), *Got.Show(), *Want.Show()));
			if (!Got.bOk) Refused.FindOrAdd(Got.Reason)++;
			if (Got.bOk)
			{
				const bool bLens = Got.Rows.ContainsByPredicate([](const FRow& R) { return R.Id == TEXT("little-lens"); });
				if (bMarked && !bLens) ++Skipped;
				if (!bMarked && bLens) ++Unskipped;
				const bool bTangle = Got.Rows.ContainsByPredicate([](const FRow& R) { return R.Id == TEXT("tangle"); });
				if (bTangle) ++TangleOffered;
				if (bTangle != bTangleHere) ++TangleWrong;
			}
		}
	}
	TestEqual(TEXT("every element's rows as the core gives them, the element as served"), Same, Elements);
	// Little Lens's row is gone exactly where the element carries a mark its door skips (the baked list: room, hidden,
	// proposed, map space).
	TestTrue(FString::Printf(TEXT("Sikelia carries marked elements (%d)"), Marked), Marked > 0);
	TestEqual(TEXT("a marked element's menu has no Little Lens row, as a member and as the public"), Skipped, Marked * 2);
	TestEqual(TEXT("an unmarked element's menu keeps it"), Unskipped, (Elements / 2 - Marked) * 2);
	TestTrue(FString::Printf(TEXT("Tangle is offered on Sikelia's Characters (%d menus)"), TangleOffered), TangleOffered > 0);
	TestEqual(TEXT("Tangle's row on exactly the Characters its door does not skip, and on no other type"), TangleWrong, 0);
	FString RefusedText;
	for (const TPair<FString, int32>& R : Refused) RefusedText += FString::Printf(TEXT(" %s %d"), *R.Key, R.Value);
	AddInfo(FString::Printf(TEXT("Sikelia: %d menus (each element as a member in a game and as the public); refused:%s"),
		Elements, RefusedText.IsEmpty() ? TEXT(" none") : *RefusedText));

	// What is no address's refusal: a sentence, no rows.
	M = CallOpenersFor(*this, World, TEXT("not-an-element"), Surface(EOWAudience::Member));
	TestTrue(TEXT("no such element: false, a sentence (") + M.Show() + TEXT(")"), !M.bOk && M.Rows.Num() == 0 && M.Reason.Contains(TEXT("no element")));
	M = CallOpenersFor(*this, nullptr, FString(), Surface(EOWAudience::Member));
	TestTrue(TEXT("no world: false (") + M.Show() + TEXT(")"), !M.bOk && M.Rows.Num() == 0 && M.Reason == TEXT("no world"));

	// The same element by its address (a link a game was handed) gives the same rows; its world's address the world's.
	FOWAddress A;
	A.Form = TEXT("text");
	A.World = Id;
	A.Type = TEXT("character");
	A.Id = Ana;
	A.Lens = TEXT("019a2b7e-4c1d-7f20-9b3e-1a2c3d4e5f60");
	A.Moment = -12;
	A.bHasMoment = true;
	TestTrue(TEXT("by its address, lens and moment riding on no href"),
		CallOpenersForAddress(*this, A, Surface(EOWAudience::Member)).Rows == Game);
	A.Type.Reset();
	A.Id.Reset();
	TestTrue(TEXT("the world's address: the world's rows"), SameMenu(CallOpenersForAddress(*this, A, Surface(EOWAudience::Member)),
		CallOpenersFor(*this, World, FString(), Surface(EOWAudience::Member))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWOpenersAddressTest, "OnlyWorlds.Openers.Address", OWTestFlags)

bool FOWOpenersAddressTest::RunTest(const FString& Parameters)
{
	const FString World = TEXT("0199c3a1-7b2e-7c3d-8e4f-5a6b7c8d9e0f");
	const FString Element = TEXT("0199c3a1-7b2e-7c3d-8e4f-5a6b7c8d9e1a");
	FOWAddress A;
	A.World = World;
	A.Type = TEXT("character");
	A.Id = Element;
	auto ReasonOf = [this](const FOWAddress& X) { const FMenu M = CallOpenersForAddress(*this, X, Surface(EOWAudience::Member)); return M.bOk ? FString() : M.Reason; };

	// Form "" is the site's; any other must be one the writer knows.
	TestTrue(TEXT("form \"\": the site's rows"), CallOpenersForAddress(*this, A, Surface(EOWAudience::Member)).bOk);
	A.Form = TEXT("site");
	TestEqual(TEXT("form site: bad_form"), ReasonOf(A), FString(TEXT("bad_form")));
	A.Form = TEXT("text");
	// The whole address is the writer's to check: its moment and lens too, though no href carries them.
	A.bHasMoment = true;
	A.Moment = static_cast<int64>(MAX_int32) + 1;
	TestEqual(TEXT("a moment past 32 bits: bad_moment"), ReasonOf(A), FString(TEXT("bad_moment")));
	A.Id = TEXT("e1");
	TestEqual(TEXT("and an id that is not one before it: bad_id"), ReasonOf(A), FString(TEXT("bad_id")));
	A.Id = Element;
	A.Moment = MAX_int32;
	A.Lens = TEXT("nightfall");
	TestEqual(TEXT("a lens that is not an id: bad_lens"), ReasonOf(A), FString(TEXT("bad_lens")));
	A.Lens.Reset();
	A.bHasLens = true;
	TestEqual(TEXT("an empty lens given: bad_lens"), ReasonOf(A), FString(TEXT("bad_lens")));
	A.bHasLens = false;
	A.World.Reset();
	TestEqual(TEXT("no world (the text form inside a world): no_world"), ReasonOf(A), FString(TEXT("no_world")));
	A.bHasWorld = true;
	TestEqual(TEXT("an empty world given: bad_world"), ReasonOf(A), FString(TEXT("bad_world")));

	// The packages' shared fixture, wherever the door can carry a case: its address and surface through the Make
	// nodes, its tools list not (the door offers the baked list). An address refusal is the fixture's reason, whatever
	// the list; a case with rows gives the core's rows for the baked list, and the fixture's own first row (the site's
	// page, which no list changes).
	const FString Path = OWTestPaths::InRepo(TEXT("tests/data/openers.fixture.json"));
	OWJsonValue Doc;
	if (!OWTestPaths::ReadJson(Path, Doc))
	{
		AddError(TEXT("no openers fixture at ") + Path);
		return false;
	}
	const OWJsonValue* Cases = Doc.Find("cases");
	if (!TestTrue(TEXT("the fixture holds cases"), Cases && Cases->IsArray())) return false;
	int32 RowCases = 0, Refusals = 0, Failed = 0;
	TArray<FString> NotCarried;
	for (const OWJsonValue& Case : Cases->Items())
	{
		const FString Name = TextOf(Case.Find("name"));
		const OWJsonValue* Address = Case.Find("address");
		const OWJsonValue* Surf = Case.Find("surface");
		const OWJsonValue* Refuse = Case.Find("refuse");
		const FString Kind = TextOf(Case.Find("kind"));
		const bool bAddressRefusal = Refuse && TextOf(Refuse->Find("error")) == TEXT("address");

		// What a Make node can hold: string parts, a whole moment, the five surface members of their own types.
		FString Why;
		FOWAddress X;
		OnlyWorlds::OWAddressParts P;
		if (Address && Address->IsObject())
		{
			for (const std::string& Key : Address->Keys())
			{
				const OWJsonValue* V = Address->Find(Key);
				const FString K = Str(Key);
				if (Key == "moment")
				{
					if (V->IsNull()) continue;
					if (!V->IsNumber() || V->Text().find_first_of(".eE") != std::string::npos) Why = TEXT("its moment is not a whole number");
					else { X.Moment = std::stoll(V->Text()); X.bHasMoment = true; P.moment = static_cast<int32_t>(X.Moment); }
					continue;
				}
				FString* To = K == TEXT("form") ? &X.Form : K == TEXT("world") ? &X.World : K == TEXT("type") ? &X.Type
					: K == TEXT("id") ? &X.Id : K == TEXT("lens") ? &X.Lens : nullptr;
				bool* Has = K == TEXT("world") ? &X.bHasWorld : K == TEXT("type") ? &X.bHasType : K == TEXT("id") ? &X.bHasId
					: K == TEXT("lens") ? &X.bHasLens : nullptr;
				if (!To) { Why = TEXT("the address holds a key no member is: ") + K; continue; }
				if (V->IsNull()) continue;
				if (!V->IsString()) { Why = TEXT("a part is not a string: ") + K; continue; }
				*To = Str(V->Text());
				if (Has) *Has = true;
				std::optional<std::string>* Part = Key == "world" ? &P.world : Key == "type" ? &P.type : Key == "id" ? &P.id : Key == "lens" ? &P.lens : nullptr;
				if (Part) *Part = V->Text();
				else P.form = V->Text();
			}
		}
		FOWOpenerSurface S;
		S.bBrowser = true;  // the core's own default, for a surface that does not say
		if (Surf && Surf->IsObject())
		{
			for (const std::string& Key : Surf->Keys())
			{
				const OWJsonValue* V = Surf->Find(Key);
				if (Key == "audience" && V->IsString() && (V->Text() == "member" || V->Text() == "public"))
					S.Audience = V->Text() == "member" ? EOWAudience::Member : EOWAudience::Public;
				else if (Key == "tool" && (V->IsNull() || V->IsString())) S.Tool = TextOf(V);
				else if (Key == "signed_in" && (V->IsNull() || V->IsBool()))
					S.SignIn = V->IsNull() ? EOWSignIn::Unknown : V->AsBool() ? EOWSignIn::SignedIn : EOWSignIn::SignedOut;
				else if (Key == "browser" && V->IsBool()) S.bBrowser = V->AsBool();
				else if (Key == "phone" && V->IsBool()) S.bPhone = V->AsBool();
				else if (Key == "online" && V->IsBool()) S.bOnline = V->AsBool();
				else Why = TEXT("its surface's ") + Str(Key) + TEXT(" is no member's value");
			}
			if (!Surf->Find("audience")) Why = TEXT("its surface has no audience (an enum always holds one)");
		}
		if (Kind != TEXT("world") && Kind != TEXT("element")) Why = TEXT("its kind is neither (the door's kind follows the address)");
		// The door asks for an element's rows when the address gives an id, else the world's: an address refusal
		// comes first either way, but rows of the other kind are another question.
		const bool bElement = !X.Id.IsEmpty() || X.bHasId;
		if (Why.IsEmpty() && !bAddressRefusal && bElement != (Kind == TEXT("element")))
			Why = TEXT("its kind is not the one its address gives");
		if (Why.IsEmpty() && Refuse && !bAddressRefusal) Why = TEXT("its refusal is the tools list's (the door's list is baked)");
		// An address carries no element: OpenersFor on a world reads the element as served, held on Sikelia in
		// OnlyWorlds.Openers.World (a marked element's menu has no row for a door that skips its mark).
		const OWJsonValue* Opts = Case.Find("options");
		const OWJsonValue* Served = Opts && Opts->IsObject() ? Opts->Find("element") : nullptr;
		if (Why.IsEmpty() && !bAddressRefusal && Served && !Served->IsNull())
			Why = TEXT("it carries the element as served (the address door takes none)");
		if (!Why.IsEmpty())
		{
			NotCarried.Add(Name + TEXT(" (") + Why + TEXT(")"));
			continue;
		}

		const int32 ErrorsBefore = ExecutionInfo.GetErrorTotal();
		const FMenu Got = CallOpenersForAddress(*this, X, S);
		if (bAddressRefusal)
		{
			++Refusals;
			TestTrue(Name + TEXT(": refused, no rows (") + Got.Show() + TEXT(")"), !Got.bOk && Got.Rows.Num() == 0);
			TestEqual(Name + TEXT(": the address's reason"), Got.Reason, TextOf(Refuse->Find("reason")));
		}
		else
		{
			++RowCases;
			const FMenu Want = CoreMenu(OnlyWorlds::OWOpeners::OpenersFor(OnlyWorlds::OWTools::All(),
				bElement ? OnlyWorlds::OWOpeners::Element : OnlyWorlds::OWOpeners::World, P, CoreSurface(S)));
			TestTrue(Name + TEXT(": the core's rows for the baked list (") + Got.Show() + TEXT("; the core ") + Want.Show() + TEXT(")"),
				SameMenu(Got, Want));
			const OWJsonValue* Expect = Case.Find("expect");
			const OWJsonValue* First = Expect && Expect->IsArray() && Expect->Size() > 0 ? &Expect->Items()[0] : nullptr;
			const FRow Site = First ? FRow{ TextOf(First->Find("id")), TextOf(First->Find("name")), TextOf(First->Find("href")), TextOf(First->Find("state")) } : FRow();
			TestTrue(Name + TEXT(": the fixture's first row, the site's page ") + Site.Show(), Got.Rows.Num() > 0 && Got.Rows[0] == Site);
		}
		if (ExecutionInfo.GetErrorTotal() > ErrorsBefore) ++Failed;
	}
	// The fixture at 111 cases (its id-case case split in two; both carry the element as served, so neither is carried).
	TestEqual(TEXT("111 cases"), static_cast<int32>(Cases->Items().size()), 111);
	TestEqual(TEXT("the row cases carried"), RowCases, 50);
	TestEqual(TEXT("the address refusals carried"), Refusals, 6);
	TestEqual(TEXT("cases failed"), Failed, 0);
	AddInfo(FString::Printf(TEXT("openers fixture: %d row cases and %d address refusals through the Blueprint path; not carried (%d): %s"),
		RowCases, Refusals, NotCarried.Num(), *FString::Join(NotCarried, TEXT("; "))));
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
