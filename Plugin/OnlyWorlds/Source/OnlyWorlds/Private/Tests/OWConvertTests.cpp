// The plugin's string seam (OWConvert.h: Str, UTF-8 into an FString; Utf8, back), on the characters past the Basic
// Multilingual Plane and on the two noncharacters at its end. Two map-space cases did not read back through it (their
// ids hold U+1F6A2 and U+FFFF); this probe says which character changes.
//
// - OnlyWorlds.Convert.AboveTheBmp: U+1F6A2 and U+10FFFF, each Str'd and Utf8'd back, are the bytes they were, and an
//   FString holds each as its surrogate pair. U+FFFF, U+FFFE and U+FDD0 are read and recorded, not held: what the
//   engine's conversion does with them is the finding. Then every id of the fixture's two cases (code-point-order,
//   hand-in-code-point-order) through the same round trip, each one that changes named with its code points.
//
// Every helper is in the namespace OWConvertTest, never at file scope (C4459, an error here).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "OWConvert.h"
#include "OWJson.h"
#include "OWTestPaths.h"

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

	FString Units(const FString& S)
	{
		FString Out;
		for (int32 I = 0; I < S.Len(); ++I) Out += FString::Printf(TEXT("%s%04X"), I ? TEXT(" ") : TEXT(""), static_cast<uint32>(S[I]));
		return Out;
	}

	// The ids of one case: every element's id, and every string a row names an element by.
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
		bool bHeld;
		int32 Units;
	};
	const FSample Samples[] = {
		{"z\xF0\x9F\x9A\xA2", TEXT("U+1F6A2 (a ship, past U+FFFF)"), true, 3},
		{"\xF4\x8F\xBF\xBF", TEXT("U+10FFFF (the last code point)"), true, 2},
		{"z\xEF\xBF\xBF", TEXT("U+FFFF (a noncharacter)"), false, 2},
		{"z\xEF\xBF\xBE", TEXT("U+FFFE (a noncharacter)"), false, 2},
		{"z\xEF\xB7\x90", TEXT("U+FDD0 (a noncharacter)"), false, 2},
	};
	for (const FSample& S : Samples)
	{
		const std::string In(S.Bytes);
		const FString Mid = OWConvert::Str(In);
		const std::string Back = OWConvert::Utf8(Mid);
		const bool bSame = Back == In;
		AddInfo(FString::Printf(TEXT("%s: in %s, FString units %s, back %s: %s"), S.Name, *CodePoints(In), *Units(Mid),
			*CodePoints(Back), bSame ? TEXT("the same") : TEXT("CHANGED")));
		if (!S.bHeld) continue;
		TestTrue(FString(S.Name) + TEXT(": Utf8(Str(s)) is s"), bSame);
		TestEqual(FString(S.Name) + TEXT(": its UTF-16 units"), Mid.Len(), S.Units);
	}
	{
		const FString Ship = OWConvert::Str(std::string("\xF0\x9F\x9A\xA2"));
		TestTrue(TEXT("U+1F6A2 as its surrogate pair, D83D DEA2"), Ship.Len() == 2 && Ship[0] == 0xD83D && Ship[1] == 0xDEA2);
	}

	// The two map-space cases that did not read back: every id through the same round trip.
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
			AddInfo(FString::Printf(TEXT("%s %s: %d ids, changed by the round trip: %s"), UTF8_TO_TCHAR(W.List), UTF8_TO_TCHAR(W.Name),
				static_cast<int32>(CaseIds.size()), Changed.Num() ? *FString::Join(Changed, TEXT("; ")) : TEXT("none")));
		}
	}
	TestEqual(TEXT("both cases found in the fixture"), Found, 2);
	return true;
}

#endif
