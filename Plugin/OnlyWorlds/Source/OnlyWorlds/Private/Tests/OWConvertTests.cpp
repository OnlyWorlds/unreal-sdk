// The plugin's string seam (OWConvert.h: Str, UTF-8 into an FString; Utf8, back). Every string the plugin hands a
// game, and every one a game hands the core, passes through it, so it keeps every Unicode scalar value as it is.
//
// - OnlyWorlds.Convert.AboveTheBmp: U+1F6A2 and U+10FFFF (a surrogate pair in an FString), and the noncharacters
//   U+FFFF, U+FFFE and U+FDD0, each Str'd and Utf8'd back, are the bytes they were. Then every id of the map-space
//   fixture's two cases that sort by code point (code-point-order, hand-in-code-point-order) through the same round
//   trip, none changed.
// - OnlyWorlds.Convert.EveryScalar: every scalar value, U+0000 to U+10FFFF less the surrogates, as its own string: Str
//   gives its UTF-16 units, Utf8 gives back its bytes.
// - OnlyWorlds.Convert.LoneSurrogates: the core holds a lone surrogate (from a JSON escape such as "\ud800") as the
//   three bytes UTF-8 would give that code point (WTF-8); Str makes it the one UTF-16 unit, and Utf8 the three bytes
//   again, so the core writes it back as its escape. A pair of units is one code point, four bytes.
// - OnlyWorlds.Convert.NotUtf8: bytes that are not UTF-8 read as U+FFFD, one for each maximal subpart (the Unicode
//   standard's practice, chapter 3, and the WHATWG decoder's), never as '?' and never dropped.
//
// Every helper is in the namespace OWConvertTest, never at file scope (C4459, an error here).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "OWConvert.h"
#include "OWJson.h"
#include "OWTestPaths.h"

#include <type_traits>
#include <string>
#include <vector>

#if WITH_DEV_AUTOMATION_TESTS && OW_REPO_TESTS

namespace OWConvertTest
{
	using OnlyWorlds::OWJsonValue;

	constexpr EAutomationTestFlags Flags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

	// A string's code points as U+XXXX words (its UTF-8 read by hand, so no conversion under test is used to show it).
	FString CodePoints(const std::string& Bytes)
	{
		FString Out;
		for (size_t I = 0; I < Bytes.size();)
		{
			const unsigned char C = static_cast<unsigned char>(Bytes[I]);
			uint32 Cp = C;
			size_t N = 1;
			if (C >= 0xF0) { Cp = C & 0x07; N = 4; }
			else if (C >= 0xE0) { Cp = C & 0x0F; N = 3; }
			else if (C >= 0xC0) { Cp = C & 0x1F; N = 2; }
			for (size_t K = 1; K < N && I + K < Bytes.size(); ++K) Cp = (Cp << 6) | (static_cast<unsigned char>(Bytes[I + K]) & 0x3F);
			Out += FString::Printf(TEXT("%sU+%04X"), Out.IsEmpty() ? TEXT("") : TEXT(" "), Cp);
			I += N;
		}
		return Out;
	}

	FString Hex(const std::string& Bytes)
	{
		FString Out;
		for (size_t I = 0; I < Bytes.size(); ++I)
			Out += FString::Printf(TEXT("%s%02X"), I ? TEXT(" ") : TEXT(""), static_cast<uint32>(static_cast<unsigned char>(Bytes[I])));
		return Out;
	}

	uint32 UnitAt(const FString& S, int32 I) { return static_cast<uint32>(static_cast<std::make_unsigned_t<TCHAR>>(S[I])); }

	FString Units(const FString& S)
	{
		FString Out;
		for (int32 I = 0; I < S.Len(); ++I) Out += FString::Printf(TEXT("%s%04X"), I ? TEXT(" ") : TEXT(""), UnitAt(S, I));
		return Out;
	}

	FString Units(const std::vector<uint32>& Want)
	{
		FString Out;
		for (uint32 U : Want) Out += FString::Printf(TEXT("%s%04X"), Out.IsEmpty() ? TEXT("") : TEXT(" "), U);
		return Out;
	}

	// Unit for unit, an embedded U+0000 included (FString's own compares stop at one).
	bool SameUnits(const FString& S, const std::vector<uint32>& Want)
	{
		if (S.Len() != static_cast<int32>(Want.size())) return false;
		int32 I = 0;
		for (uint32 U : Want)
		{
			if (UnitAt(S, I++) != U) return false;
		}
		return true;
	}

	// An FString of these UTF-16 units, put in one by one, no conversion.
	FString FromUnits(const std::vector<uint32>& Want)
	{
		FString Out;
		for (uint32 U : Want) Out.AppendChar(static_cast<TCHAR>(U));
		return Out;
	}

	// A scalar value's UTF-8, written here by hand.
	std::string Utf8Of(uint32 Cp)
	{
		std::string Out;
		if (Cp < 0x80) Out.push_back(static_cast<char>(Cp));
		else if (Cp < 0x800)
		{
			Out.push_back(static_cast<char>(0xC0 | (Cp >> 6)));
			Out.push_back(static_cast<char>(0x80 | (Cp & 0x3F)));
		}
		else if (Cp < 0x10000)
		{
			Out.push_back(static_cast<char>(0xE0 | (Cp >> 12)));
			Out.push_back(static_cast<char>(0x80 | ((Cp >> 6) & 0x3F)));
			Out.push_back(static_cast<char>(0x80 | (Cp & 0x3F)));
		}
		else
		{
			Out.push_back(static_cast<char>(0xF0 | (Cp >> 18)));
			Out.push_back(static_cast<char>(0x80 | ((Cp >> 12) & 0x3F)));
			Out.push_back(static_cast<char>(0x80 | ((Cp >> 6) & 0x3F)));
			Out.push_back(static_cast<char>(0x80 | (Cp & 0x3F)));
		}
		return Out;
	}

	// Whether an FString holds exactly this one scalar value: one unit, or its surrogate pair.
	bool HoldsScalar(const FString& S, uint32 Cp)
	{
		if constexpr (sizeof(TCHAR) == 4) return S.Len() == 1 && UnitAt(S, 0) == Cp;
		if (Cp < 0x10000) return S.Len() == 1 && UnitAt(S, 0) == Cp;
		const uint32 V = Cp - 0x10000;
		return S.Len() == 2 && UnitAt(S, 0) == 0xD800 + (V >> 10) && UnitAt(S, 1) == 0xDC00 + (V & 0x3FF);
	}

	// The ids of one case: every element's id.
	void Ids(const OWJsonValue& Case, std::vector<std::string>& Out)
	{
		const OWJsonValue* Elements = Case.Find("elements");
		if (!Elements || !Elements->IsArray()) return;
		for (const OWJsonValue& E : Elements->Items())
		{
			const OWJsonValue* Id = E.Find("id");
			if (Id && Id->IsString()) Out.push_back(Id->Text());
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWConvertAboveTheBmpTest, "OnlyWorlds.Convert.AboveTheBmp", OWConvertTest::Flags)

bool FOWConvertAboveTheBmpTest::RunTest(const FString& Parameters)
{
	using namespace OWConvertTest;
	struct FSample
	{
		const char* Bytes;
		const TCHAR* Name;
		int32 Units;
	};
	const FSample Samples[] = {
		{"z\xF0\x9F\x9A\xA2", TEXT("U+1F6A2 (a ship, past U+FFFF)"), 3},
		{"\xF4\x8F\xBF\xBF", TEXT("U+10FFFF (the last code point)"), 2},
		{"z\xEF\xBF\xBF", TEXT("U+FFFF (a noncharacter)"), 2},
		{"z\xEF\xBF\xBE", TEXT("U+FFFE (a noncharacter)"), 2},
		{"z\xEF\xB7\x90", TEXT("U+FDD0 (a noncharacter)"), 2},
	};
	for (const FSample& S : Samples)
	{
		const std::string In(S.Bytes);
		const FString Mid = OWConvert::Str(In);
		const std::string Back = OWConvert::Utf8(Mid);
		AddInfo(FString::Printf(TEXT("%s: in %s, FString units %s, back %s"), S.Name, *CodePoints(In), *Units(Mid), *CodePoints(Back)));
		TestTrue(FString(S.Name) + TEXT(": Utf8(Str(s)) is s, back ") + CodePoints(Back), Back == In);
		TestEqual(FString(S.Name) + TEXT(": its UTF-16 units"), Mid.Len(), S.Units);
	}
	TestTrue(TEXT("U+1F6A2 as its surrogate pair, D83D DEA2"), SameUnits(OWConvert::Str(std::string("\xF0\x9F\x9A\xA2")), {0xD83D, 0xDEA2}));
	TestTrue(TEXT("U+FFFF as its one unit, FFFF"), SameUnits(OWConvert::Str(std::string("\xEF\xBF\xBF")), {0xFFFF}));
	TestTrue(TEXT("U+FFFE as its one unit, FFFE"), SameUnits(OWConvert::Str(std::string("\xEF\xBF\xBE")), {0xFFFE}));
	TestTrue(TEXT("an FString's FFFF to UTF-8, EF BF BF"), OWConvert::Utf8(FromUnits({0xFFFF})) == "\xEF\xBF\xBF");

	// The two map-space cases that sort by code point: every id through the same round trip, none changed.
	OWJsonValue Fixture;
	const FString Path = OWTestPaths::InRepo(TEXT("codegen/schema-dist/profiles/map-space.fixture.json"));
	if (!TestTrue(TEXT("the fixture at ") + Path, OWTestPaths::ReadJson(Path, Fixture))) return true;
	struct FWhere
	{
		const char* List;
		const char* Name;
	};
	const FWhere Wanted[] = {{"cases", "code-point-order"}, {"layout_cases", "hand-in-code-point-order"}};
	int32 Found = 0;
	for (const FWhere& W : Wanted)
	{
		const OWJsonValue* List = Fixture.Find(W.List);
		if (!List || !List->IsArray()) continue;
		for (const OWJsonValue& Case : List->Items())
		{
			const OWJsonValue* Name = Case.Find("name");
			if (!Name || !Name->IsString() || Name->Text() != W.Name) continue;
			++Found;
			std::vector<std::string> CaseIds;
			Ids(Case, CaseIds);
			TArray<FString> Changed;
			for (const std::string& Id : CaseIds)
			{
				const std::string Back = OWConvert::Utf8(OWConvert::Str(Id));
				if (Back != Id) Changed.Add(CodePoints(Id) + TEXT(" -> ") + CodePoints(Back));
			}
			const FString Label = FString::Printf(TEXT("%s %s"), UTF8_TO_TCHAR(W.List), UTF8_TO_TCHAR(W.Name));
			AddInfo(FString::Printf(TEXT("%s: %d ids, changed by the round trip: %s"), *Label, static_cast<int32>(CaseIds.size()),
				Changed.Num() ? *FString::Join(Changed, TEXT("; ")) : TEXT("none")));
			TestTrue(Label + TEXT(": ids in the case"), CaseIds.size() > 0);
			TestEqual(Label + TEXT(": ids changed by the round trip"), Changed.Num(), 0);
		}
	}
	TestEqual(TEXT("both cases found in the fixture"), Found, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWConvertEveryScalarTest, "OnlyWorlds.Convert.EveryScalar", OWConvertTest::Flags)

bool FOWConvertEveryScalarTest::RunTest(const FString& Parameters)
{
	using namespace OWConvertTest;
	int32 Tried = 0;
	TArray<FString> Wrong;
	for (uint32 Cp = 0; Cp <= 0x10FFFF; ++Cp)
	{
		if (Cp >= 0xD800 && Cp <= 0xDFFF) continue;
		++Tried;
		const std::string In = Utf8Of(Cp);
		const FString Mid = OWConvert::Str(In);
		const std::string Back = OWConvert::Utf8(Mid);
		if (HoldsScalar(Mid, Cp) && Back == In) continue;
		if (Wrong.Num() < 8) Wrong.Add(FString::Printf(TEXT("U+%04X: units %s, back %s"), Cp, *Units(Mid), *Hex(Back)));
		else if (Wrong.Num() == 8) Wrong.Add(TEXT("..."));
	}
	TestEqual(TEXT("scalar values tried (U+0000 to U+10FFFF, less the 2048 surrogates)"), Tried, 0x110000 - 0x800);
	TestTrue(TEXT("every scalar value held and given back: ") + (Wrong.Num() ? FString::Join(Wrong, TEXT("; ")) : FString(TEXT("all"))),
		Wrong.Num() == 0);
	// An embedded U+0000 is kept where it stands, not taken as the string's end.
	const FString Nul = OWConvert::Str(std::string("a\0b", 3));
	TestTrue(TEXT("a U+0000 inside a string: units 0061 0000 0062, got ") + Units(Nul), SameUnits(Nul, {0x61, 0, 0x62}));
	TestTrue(TEXT("a U+0000 inside a string, back"), OWConvert::Utf8(Nul) == std::string("a\0b", 3));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWConvertLoneSurrogatesTest, "OnlyWorlds.Convert.LoneSurrogates", OWConvertTest::Flags)

bool FOWConvertLoneSurrogatesTest::RunTest(const FString& Parameters)
{
	using namespace OWConvertTest;
	struct FLone
	{
		const TCHAR* Name;
		std::vector<uint32> Units;
		const char* Bytes;
		int32 Size;
	};
	const FLone Cases[] = {
		{TEXT("a lone high surrogate at the end"), {0x78, 0xD800}, "x\xED\xA0\x80", 4},
		{TEXT("a lone low surrogate at the start"), {0xDC00, 0x78}, "\xED\xB0\x80x", 4},
		{TEXT("a low then a high: two lone ones"), {0xDC00, 0xD800}, "\xED\xB0\x80\xED\xA0\x80", 6},
		{TEXT("a high then a low: one pair, U+1F6A2"), {0xD83D, 0xDEA2}, "\xF0\x9F\x9A\xA2", 4},
	};
	for (const FLone& C : Cases)
	{
		const std::string Want(C.Bytes, static_cast<size_t>(C.Size));
		const std::string Bytes = OWConvert::Utf8(FromUnits(C.Units));
		TestTrue(FString(C.Name) + TEXT(": Utf8 gives ") + Hex(Want) + TEXT(", got ") + Hex(Bytes), Bytes == Want);
		const FString Back = OWConvert::Str(Want);
		TestTrue(FString(C.Name) + TEXT(": Str gives ") + Units(C.Units) + TEXT(", got ") + Units(Back), SameUnits(Back, C.Units));
	}

	// The core's own lone surrogate: an escape in a JSON text, read by the core, through the seam both ways.
	const OnlyWorlds::OWJsonParseResult R = OnlyWorlds::OWJson::Parse(std::string_view("\"x\\ud800\""));
	if (!TestTrue(TEXT("the core reads \"x\\ud800\""), R.ok && R.value.IsString())) return true;
	TestTrue(TEXT("the core holds it as x ED A0 80, got ") + Hex(R.value.Text()), R.value.Text() == "x\xED\xA0\x80");
	const FString Mid = OWConvert::Str(R.value.Text());
	TestTrue(TEXT("Str: units 0078 D800, got ") + Units(Mid), SameUnits(Mid, {0x78, 0xD800}));
	const std::string Written = OnlyWorlds::OWJson::Quote(OWConvert::Utf8(Mid));
	TestTrue(TEXT("and the core writes it back as its escape, got ") + OWConvert::Str(Written), Written == "\"x\\ud800\"");
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWConvertNotUtf8Test, "OnlyWorlds.Convert.NotUtf8", OWConvertTest::Flags)

bool FOWConvertNotUtf8Test::RunTest(const FString& Parameters)
{
	using namespace OWConvertTest;
	struct FBad
	{
		const TCHAR* Name;
		const char* Bytes;
		std::vector<uint32> Units;
	};
	const FBad Cases[] = {
		{TEXT("a byte that begins nothing"), "a\x80" "b", {0x61, 0xFFFD, 0x62}},
		{TEXT("a byte no UTF-8 holds"), "\xFF" "a", {0xFFFD, 0x61}},
		{TEXT("an overlong slash, two bytes"), "\xC0\xAF", {0xFFFD, 0xFFFD}},
		{TEXT("an overlong NUL, three bytes"), "\xE0\x80\x80", {0xFFFD, 0xFFFD, 0xFFFD}},
		{TEXT("a four-byte sequence cut short at the end"), "a\xF0\x9F\x9A", {0x61, 0xFFFD}},
		{TEXT("a four-byte sequence cut short before a letter"), "\xF0\x9F\x9A" "z", {0xFFFD, 0x7A}},
		{TEXT("past U+10FFFF"), "\xF4\x90\x80\x80", {0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD}},
		{TEXT("a three-byte sequence cut short by a letter"), "\xE2\x82" "z", {0xFFFD, 0x7A}},
	};
	for (const FBad& C : Cases)
	{
		const FString Got = OWConvert::Str(std::string(C.Bytes));
		TestTrue(FString(C.Name) + TEXT(": ") + Hex(std::string(C.Bytes)) + TEXT(" reads as ") + Units(C.Units) + TEXT(", got ") + Units(Got),
			SameUnits(Got, C.Units));
		TestTrue(FString(C.Name) + TEXT(": and gives back U+FFFD's own bytes, valid UTF-8"),
			OnlyWorlds::OWJson::Parse(std::string_view("\"" + OWConvert::Utf8(Got) + "\"")).ok);
	}
	return true;
}

#endif
