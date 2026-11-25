// A new element's slug (the folder format's section 3.2, "How the slug is made"): NFKD; U+0300 to U+036F dropped and no
// other mark; lowercased; every run outside a-z and 0-9 one hyphen; hyphens at both ends dropped; cut at 40 and the new
// end's hyphens dropped. A name that leaves no slug gives the full id.
//
// The cases (tests/data/file-names.fixture.json) are the shared file-name set, held by their sha256: 37 names, each
// slug the one the TypeScript package's folder writer and the Python package's slugify both give.

#include "ow_test.h"
#include "sha256.h"

#include "OWFiles.h"
#include "OWJson.h"
#include "OWWorld.h"

#include <string>

using namespace OnlyWorlds;
using owtest::P;

namespace {

const char* const FixtureSha256 = "8b19110baf6c4c96cbe08f741c219a9e3803a1b495144aaadd91715fd5b7c741";

const OWBytes& FixtureBytes() {
    static OWBytes bytes;
    static bool read = false;
    if (!read) {
        OWFiles::Read(P().root + "/tests/data/file-names.fixture.json", bytes);
        read = true;
    }
    return bytes;
}

}  // namespace

OW_TEST(file_names_fixture_is_the_shared_one) {
    t.Eq(owtest::Sha256Hex(FixtureBytes()), std::string(FixtureSha256), "the file-name set's sha256");
}

OW_TEST(file_names_every_case_is_the_sentences_slug) {
    OWJsonParseResult r = OWJson::Parse(FixtureBytes());
    const OWJsonValue* cases = r.ok ? r.value.Find("cases") : nullptr;
    t.Ok(cases != nullptr, "the set parses and holds cases");
    if (!cases) return;
    t.Eq(cases->Items().size(), size_t(37), "37 cases");
    const std::string id = "019a9300-0000-7000-8000-0000000000c9";
    for (const OWJsonValue& c : cases->Items()) {
        const std::string& name = c.Find("element_name")->Text();
        const std::string& slug = c.Find("slug")->Text();
        const std::string& label = c.Find("name")->Text();
        t.Eq(OWWorld::Slugify(name), slug, label);
        t.Eq(OWWorld::ElementFilename(id, name), slug.empty() ? id + ".json" : slug + "--000000c9.json",
             label + ": the file name");
    }
}
