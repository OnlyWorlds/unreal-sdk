// The .owworld conformance set through the plugin: every generated fixture of the vendored schema-dist
// (fixtures/owworld/verdicts.json) opened with UOWLibrary, the reader verdict and the reason asserted, as the core's
// own runner (tests/test_archive.cpp) and the Unity SDK's NUnit class assert them.

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "OWConvert.h"
#include "OWFiles.h"
#include "OWJson.h"
#include "OWLibrary.h"
#include "OWWorldArchive.h"
#include "OWWorldObject.h"
#include "OWTestPaths.h"

#if WITH_DEV_AUTOMATION_TESTS && OW_REPO_TESTS

namespace
{
	using OnlyWorlds::OWJsonValue;

	constexpr EAutomationTestFlags OWTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

	// verdicts.json, parsed once.
	const OWJsonValue& Verdicts()
	{
		static OWJsonValue Doc;
		static bool bLoaded = false;
		if (!bLoaded)
		{
			bLoaded = true;
			OnlyWorlds::OWBytes Bytes;
			OnlyWorlds::OWFiles::Read(OWConvert::Utf8(OWTestPaths::Fixtures() / TEXT("verdicts.json")), Bytes);
			OnlyWorlds::OWJsonParseResult R = OnlyWorlds::OWJson::Parse(Bytes);
			if (R.ok) Doc = std::move(R.value);
		}
		return Doc;
	}

	FString Field(const OWJsonValue& V, const char* Key)
	{
		const OWJsonValue* X = V.Find(Key);
		return X && X->IsString() ? OWConvert::Str(X->Text()) : FString();
	}

	const OWJsonValue* FixtureList()
	{
		const OWJsonValue* F = Verdicts().Find("fixtures");
		return F && F->IsArray() ? F : nullptr;
	}

	bool Generated(const OWJsonValue& V) { return Field(V, "status") != TEXT("not_generated"); }

	// A fixture's test name: its file name, dots made underscores (a dot would split the automation tree).
	FString CaseName(const FString& File) { return File.Replace(TEXT("."), TEXT("_")); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOWArchiveSetTest, "OnlyWorlds.Archive.Set", OWTestFlags)

bool FOWArchiveSetTest::RunTest(const FString& Parameters)
{
	const OWJsonValue* Fixtures = FixtureList();
	if (!TestNotNull(TEXT("verdicts.json parses, with its fixtures (") + OWTestPaths::Fixtures() + TEXT(")"), Fixtures))
	{
		return false;
	}
	int32 NumGenerated = 0;
	for (const OWJsonValue& V : Fixtures->Items())
	{
		if (Generated(V)) ++NumGenerated;
		else AddInfo(TEXT("not generated (on demand only, not run): ") + Field(V, "file"));
	}
	TArray<FString> OnDisk;
	IFileManager::Get().FindFiles(OnDisk, *(OWTestPaths::Fixtures() / TEXT("*.owworld")), true, false);
	TArray<FString> Zips;
	IFileManager::Get().FindFiles(Zips, *(OWTestPaths::Fixtures() / TEXT("*.zip")), true, false);
	TestEqual(TEXT("every generated fixture is on disk, and nothing else"), OnDisk.Num() + Zips.Num(), NumGenerated);
	TestEqual(TEXT("the set's 109 generated fixtures"), NumGenerated, 109);

	// The core's thirteen causes: section 4.7's twelve and section 4.8's name_case. A set from before the serial that adds
	// it (serial 145 lists twelve) holds all of them but that one; a set from after it, the same thirteen.
	const OWJsonValue* Reasons = Verdicts().Find("reasons");
	TSet<FString> Theirs, Mine;
	if (Reasons && Reasons->IsArray())
	{
		for (const OWJsonValue& R : Reasons->Items()) Theirs.Add(OWConvert::Str(R.Text()));
	}
	for (const char* R : OnlyWorlds::OWWorldArchive::Reasons) Mine.Add(OWConvert::Str(R));
	TestTrue(TEXT("the reasons are the spec's list"), Mine.Num() == 13 && Mine.Includes(Theirs) && Theirs.Num() >= 12);
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FOWArchiveConformanceTest, "OnlyWorlds.Archive.Conformance", OWTestFlags)

void FOWArchiveConformanceTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	const OWJsonValue* Fixtures = FixtureList();
	if (!Fixtures) return;
	for (const OWJsonValue& V : Fixtures->Items())
	{
		if (!Generated(V)) continue;
		OutBeautifiedNames.Add(CaseName(Field(V, "file")));
		OutTestCommands.Add(Field(V, "file"));
	}
}

bool FOWArchiveConformanceTest::RunTest(const FString& Parameters)
{
	const OWJsonValue* Case = nullptr;
	for (const OWJsonValue& V : FixtureList()->Items())
	{
		if (Field(V, "file") == Parameters) Case = &V;
	}
	if (!TestNotNull(TEXT("the case is in verdicts.json"), Case)) return false;

	const FString Path = OWTestPaths::Fixtures() / Parameters;
	if (!TestTrue(TEXT("the fixture is on disk: ") + Path, FPaths::FileExists(Path))) return false;

	const FOWArchiveVerdict Verdict = UOWLibrary::CheckWorldFile(Path);
	const FString Expected = Field(*Case, "reader");
	TestEqual(FString::Printf(TEXT("reader verdict (%s: %s)"), *Verdict.Reason, *Verdict.Message),
		Verdict.bAccepted ? FString(TEXT("accept")) : FString(TEXT("refuse")), Expected);
	if (Expected == TEXT("refuse"))
	{
		TestEqual(TEXT("reason (") + Verdict.Message + TEXT(")"), Verdict.Reason, Field(*Case, "reason"));
		return true;
	}

	// Accepted: the world opens, under the id the reader found.
	FOWArchiveVerdict Again;
	UOWWorld* World = UOWLibrary::OpenWorldFile(Path, Again);
	if (!TestNotNull(TEXT("the world opens"), World)) return true;
	TestEqual(TEXT("the world's id"), World->GetId(), Verdict.WorldId);
	const FString Strips = Field(*Case, "strips");
	if (!Strips.IsEmpty())
	{
		TestFalse(Strips + TEXT(" stripped from the world"), World->Native()->info.Has(OWConvert::Utf8(Strips)));
	}
	// OW-9's round trip, as the core's runner makes it: read, pack the files, read again, every entry byte-equal.
	const OWJsonValue* RoundTrip = Case->Find("roundtrip");
	if (RoundTrip && RoundTrip->IsBool() && RoundTrip->AsBool())
	{
		OnlyWorlds::OWArchiveRead R = OnlyWorlds::OWWorldArchive::ReadFile(OWConvert::Utf8(Path));
		const std::string Top = R.prefix.empty() ? std::string() : R.prefix.substr(0, R.prefix.size() - 1);
		OnlyWorlds::OWPacked Packed = OnlyWorlds::OWWorldArchive::PackFiles(R.files, Top);
		OnlyWorlds::OWArchiveRead Back = OnlyWorlds::OWWorldArchive::Read(Packed.bytes);
		TestTrue(TEXT("round trip: the repacked file reads, every entry byte-equal"), Packed.ok && Back.ok && Back.files == R.files);
	}
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS && OW_REPO_TESTS
