// OWViews on views.json's conformance fixture (schema-dist views.fixture.json, version 3 since serial 115; the vendored
// copies, their sha256 held to MANIFEST.json): the baked table is the file, every case gives its row, its sentence, its
// banners and, where it names one, its order (a case that names a table of the fixture's own reads against that table,
// through the private ViewIn), and the fixture's cites name fields their rows name and their types have, each quote at
// its line of the vendored page. Beside the fixture, the Godot and Unity SDKs' vectors for what the file's words state
// (tests/test_views.gd, Tests/Editor/OWViewsTests.cs). No count is pinned: each total is read from the fixture or
// schema.json as it stands, and a guard of at least one stands where none would pass by itself.

#include "ow_test.h"
#include "sha256.h"

#include "../Plugin/OnlyWorlds/Core/Private/OWViewsIn.h"
#include "OWFiles.h"
#include "OWJson.h"
#include "OWLens.h"
#include "OWViews.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace OnlyWorlds;
using owtest::P;

namespace {

const char* const World = "0199c3a1-7b2e-7c3d-8e4f-5a6b7c8d9e0f";

std::string DistPath(const std::string& rel) { return P().root + "/codegen/schema-dist/" + rel; }

std::shared_ptr<OWJsonValue> Load(const std::string& rel) {
    OWBytes bytes;
    OWFiles::Read(DistPath(rel), bytes);
    OWJsonParseResult r = OWJson::Parse(bytes);
    return std::make_shared<OWJsonValue>(r.ok ? std::move(r.value) : OWJsonValue());
}

std::shared_ptr<OWJsonValue> Fixture() {
    static std::shared_ptr<OWJsonValue> doc = Load("views.fixture.json");
    return doc;
}

std::shared_ptr<OWJsonValue> TheFile() {
    static std::shared_ptr<OWJsonValue> doc = Load("views.json");
    return doc;
}

std::shared_ptr<OWJsonValue> Schema() {
    static std::shared_ptr<OWJsonValue> doc = Load("schema.json");
    return doc;
}

OWJsonValue J(const std::string& text) {
    OWJsonParseResult r = OWJson::Parse(std::string_view(text));
    return r.ok ? std::move(r.value) : OWJsonValue();
}

std::string Str(const OWJsonValue* v) { return v && v->IsString() ? v->Text() : std::string(); }
std::string Str(const OWJsonValue& v, const char* key) { return Str(v.Find(key)); }

std::vector<std::string> Strings(const OWJsonValue* list) {
    std::vector<std::string> out;
    if (list && list->IsArray())
        for (const OWJsonValue& x : list->Items()) out.push_back(x.IsString() ? x.Text() : std::string("?"));
    return out;
}

std::string Join(const std::vector<std::string>& xs) {
    std::string out = "[";
    for (size_t i = 0; i < xs.size(); ++i) out += (i ? ", " : "") + xs[i];
    return out + "]";
}

std::string Text(const OWJsonValue& v) { return OWJson::Stringify(v); }

// A row as views.json (or a fixture table) writes it; a key it leaves out is empty.
OWViewRow RowOf(const OWJsonValue& r) {
    OWViewRow row;
    row.id = Str(r, "id");
    const OWJsonValue* kind = r.Find("kind");
    if (kind && kind->Find("profile")) {
        row.kind.profile = Str(*kind, "profile");
        row.kind.pair = Str(*kind, "pair");
        size_t slash = row.kind.pair.find('/');
        row.kind.type = row.kind.pair.substr(0, slash);
        row.kind.supertype = slash == std::string::npos ? std::string() : row.kind.pair.substr(slash + 1);
    } else if (kind) {
        row.kind.type = Str(*kind, "type");
        row.kind.supertype = Str(*kind, "supertype");
    }
    row.from = Str(r, "from");
    row.sentence = Strings(r.Find("sentence"));
    row.lead = Strings(r.Find("lead"));
    if (const OWJsonValue* folds = r.Find("fold"))
        for (const OWJsonValue& f : folds->Items()) row.fold.push_back({Str(f, "field"), Str(f, "label")});
    if (const OWJsonValue* layout = r.Find("layout")) row.layout = {Str(*layout, "name"), Str(*layout, "by")};
    row.banner = Strings(r.Find("banner"));
    return row;
}

struct Table {
    std::vector<OWViewBanner> banners;
    std::vector<OWViewRow> rows;
};

// A table of the fixture's own (its `tables`), built past the generator's checks: `two-rows-one-kind` is at fault on
// purpose. Each is built once and kept, so an answer's row stays valid.
const Table* FixtureTable(const std::string& name) {
    static std::map<std::string, std::unique_ptr<Table>> built;
    auto it = built.find(name);
    if (it != built.end()) return it->second.get();
    const OWJsonValue* tables = Fixture()->Find("tables");
    const OWJsonValue* t = tables ? tables->Find(name) : nullptr;
    if (!t || !t->IsObject()) return nullptr;
    auto table = std::make_unique<Table>();
    if (const OWJsonValue* bs = t->Find("banners"))
        for (const OWJsonValue& b : bs->Items()) table->banners.push_back({Str(b, "mark"), Str(b, "defined_by")});
    if (const OWJsonValue* rs = t->Find("rows"))
        for (const OWJsonValue& r : rs->Items()) table->rows.push_back(RowOf(r));
    return (built[name] = std::move(table)).get();
}

// A table of the test's own: the baked banners and the given rows.
Table OwnTable(std::vector<OWViewRow> rows) { return Table{OWViews::Banners(), std::move(rows)}; }

OWViewRow TestRow(const std::string& id, OWViewKind kind, std::vector<std::string> banner = {}) {
    OWViewRow r;
    r.id = id;
    r.kind = std::move(kind);
    r.from = "a test";
    r.sentence = {"story"};
    r.layout = {"document", ""};
    r.banner = std::move(banner);
    return r;
}

std::string RowId(const OWView& v) { return v.row ? v.row->id : std::string("(none)"); }

bool InTable(const OWViewRow* row) {
    for (const OWViewRow& r : OWViews::Rows())
        if (&r == row) return true;
    return false;
}

// The base fields a row may name (schema.json's base, less id and world).
std::set<std::string> BaseFields() {
    std::set<std::string> out;
    if (const OWJsonValue* base = Schema()->Find("base"))
        for (const OWJsonValue& f : base->Items())
            if (Str(f, "name") != "id" && Str(f, "name") != "world") out.insert(Str(f, "name"));
    return out;
}

const OWJsonValue* SchemaType(const std::string& slug) {
    const OWJsonValue* types = Schema()->Find("types");
    return types ? types->Find(slug) : nullptr;
}

}  // namespace

OW_TEST(views_the_files_are_the_pinned_ones) {
    auto manifest = Load("MANIFEST.json");
    const OWJsonValue* files = manifest->Find("files");
    t.Ok(files != nullptr, "MANIFEST.json parses, with its files");
    if (!files) return;
    for (const char* name : {"views.json", "views.fixture.json"}) {
        OWBytes raw;
        t.Ok(OWFiles::Read(DistPath(name), raw), std::string("vendored: ") + name);
        t.Eq(owtest::Sha256Hex(raw), Str(files->Find(name)), std::string(name) + "'s bytes are the manifest's");
    }
    t.Eq(std::string(OWViews::Sha256), Str(files->Find("views.json")),
         "the baked table is the pinned views.json's: regenerate with codegen/generate_views.py");
}

OW_TEST(views_the_table_is_the_file) {
    auto doc = TheFile();
    const OWJsonValue* meta = doc->Find("_meta");
    const OWJsonValue* version = meta ? meta->Find("version") : nullptr;
    t.Eq(version ? version->Text() : std::string(), std::to_string(OWViews::Version), "the baked version is the file's");
    t.Eq(OWViews::Version, 3, "version 3");

    const OWJsonValue* banners = doc->Find("banners");
    t.Ok(banners && banners->Size() == OWViews::Banners().size(), "a banner for each of the file's");
    if (banners)
        for (size_t i = 0; i < banners->Size() && i < OWViews::Banners().size(); ++i) {
            t.Eq(OWViews::Banners()[i].mark, Str(banners->Items()[i], "mark"), "banner " + std::to_string(i));
            t.Eq(OWViews::Banners()[i].defined_by, Str(banners->Items()[i], "defined_by"), "banner " + std::to_string(i));
        }

    const OWJsonValue* layouts = doc->Find("layouts");
    t.Ok(layouts && layouts->Size() == OWViews::Layouts().size(), "a layout for each of the file's");
    if (layouts)
        for (size_t i = 0; i < layouts->Size() && i < OWViews::Layouts().size(); ++i) {
            t.Eq(OWViews::Layouts()[i].first, layouts->Keys()[i], "layout " + std::to_string(i) + "'s name");
            t.Eq(OWViews::Layouts()[i].second, Str(&layouts->Items()[i]), "layout " + layouts->Keys()[i]);
        }

    const OWJsonValue* rows = doc->Find("rows");
    t.Ok(!OWViews::Rows().empty(), "a table with no rows would pass every case that wants no row");
    t.Ok(rows && rows->Size() == OWViews::Rows().size(), "a row for each of the file's");
    if (!rows) return;
    const std::set<std::string> keys = {"id", "kind", "from", "sentence", "lead", "fold", "layout", "banner"};
    for (size_t i = 0; i < rows->Size() && i < OWViews::Rows().size(); ++i) {
        const OWJsonValue& r = rows->Items()[i];
        const OWViewRow want = RowOf(r);
        const OWViewRow& got = OWViews::Rows()[i];
        const std::string id = want.id;
        t.Eq(got.id, want.id, "row " + std::to_string(i));
        t.Eq(got.kind.type, want.kind.type, id + "'s kind type");
        t.Eq(got.kind.supertype, want.kind.supertype, id + "'s kind supertype");
        t.Eq(got.kind.profile, want.kind.profile, id + "'s profile");
        t.Eq(got.kind.pair, want.kind.pair, id + "'s pair");
        t.Eq(got.from, want.from, id + "'s from");
        t.Eq(Join(got.sentence), Join(want.sentence), id + "'s sentence");
        t.Eq(Join(got.lead), Join(want.lead), id + "'s lead");
        t.Eq(got.fold.size(), want.fold.size(), id + "'s folds");
        for (size_t k = 0; k < got.fold.size() && k < want.fold.size(); ++k) {
            t.Eq(got.fold[k].field, want.fold[k].field, id + "'s fold field");
            t.Eq(got.fold[k].label, want.fold[k].label, id + "'s fold label");
        }
        t.Eq(got.layout.name, want.layout.name, id + "'s layout");
        t.Eq(got.layout.by, want.layout.by, id + "'s layout.by");
        t.Eq(Join(got.banner), Join(want.banner), id + "'s banner");
        for (const std::string& k : r.Keys()) t.Ok(keys.count(k) == 1, "row " + id + " carries a column OWViews does not read: " + k);
    }
}

OW_CASES(views_cases) {
    owtest::Cases out;
    auto doc = Fixture();
    const OWJsonValue* cases = doc->Find("cases");
    if (!cases || !cases->IsArray() || cases->Size() == 0) {
        out.emplace_back("views:not-found", [](owtest::T& t) { t.Fail("the views fixture's cases did not load"); });
        return out;
    }
    for (const OWJsonValue& c : cases->Items())
        out.emplace_back("views:" + Str(c, "name"), [doc, &c](owtest::T& t) {
            const OWJsonValue* element = c.Find("element");
            const OWJsonValue* expect = c.Find("expect");
            if (!element || !expect) return t.Fail("the case has no element or no expect");
            OWView v;
            if (const OWJsonValue* table = c.Find("table")) {
                const Table* own = FixtureTable(Str(table));
                if (!own) return t.Fail("the case names the table " + Str(table) + ", which the fixture does not hold");
                v = OWViews::ViewIn(own->banners, own->rows, *element);
            } else {
                v = OWViews::ViewOf(*element);
                if (v.row) t.Ok(InTable(v.row), "the row is the table's own");
            }
            OWJsonValue got = v.ToJson();
            for (const char* key : {"row", "sentence", "banners", "order"}) {
                const OWJsonValue* want = expect->Find(key);
                if (!want) {
                    if (std::string(key) != "order") t.Fail(std::string("the case gives no ") + key);
                    continue;
                }
                t.Eq(Text(*got.Find(key)), Text(*want), key);
            }
        });
    return out;
}

OW_TEST(views_the_fixture_is_whole_and_every_row_is_reached) {
    auto doc = Fixture();
    t.Eq(Str(*doc, "views"), std::string("views.json"), "the fixture's table");
    const OWJsonValue* cases = doc->Find("cases");
    t.Ok(cases && cases->Size() > 0, "the fixture holds cases");
    if (!cases) return;
    std::set<std::string> names;
    size_t own_table = 0, own_with_order = 0;
    std::set<std::string> seen;
    for (const OWJsonValue& c : cases->Items()) {
        t.Ok(names.insert(Str(c, "name")).second, "each case's name once: " + Str(c, "name"));
        if (c.Find("table")) {
            ++own_table;
            continue;
        }
        if (c.Find("expect") && c.Find("expect")->Find("order")) ++own_with_order;
        if (const OWJsonValue* el = c.Find("element"))
            if (const OWViewRow* r = OWViews::ViewOf(*el).row) seen.insert(r->id);
    }
    t.Eq(views_cases().size(), cases->Size(), "every case runs here, one test each");
    t.Ok(own_table > 0, "a case reads a table of the fixture's own");
    t.Ok(own_with_order > 0, "a case on the file's own table names the order drawn");
    for (const OWViewRow& r : OWViews::Rows()) t.Ok(seen.count(r.id) == 1, "no case reaches the row " + r.id);
}

// The fixture's third note: each cite names a field its row names, each field a row names has a cite, each is a field of
// the row's type in the standard (a base field included), and each quote is at its line of the vendored page (the page
// split at LF, the line 1-based), as codegen/generate_views.py checks them before it bakes the table.
OW_TEST(views_the_cites_name_fields_their_rows_name_each_quote_at_its_line) {
    const OWJsonValue* cites = Fixture()->Find("cites");
    t.Ok(cites && cites->Size() > 0, "the fixture holds cites");
    if (!cites) return;
    std::set<std::string> named;
    std::map<std::string, std::string> type_of;
    for (const OWViewRow& r : OWViews::Rows()) {
        type_of[r.id] = r.kind.type;
        for (const std::string& f : r.sentence) named.insert(r.id + "." + f);
        for (const std::string& f : r.lead) named.insert(r.id + "." + f);
        for (const OWViewFold& f : r.fold) named.insert(r.id + "." + f.field);
        if (!r.layout.by.empty()) named.insert(r.id + "." + r.layout.by);
    }
    const std::set<std::string> base = BaseFields();
    std::set<std::string> cited;
    for (const OWJsonValue& c : cites->Items()) {
        const std::string key = Str(c, "row") + "." + Str(c, "field");
        t.Ok(named.count(key) == 1, "the cite of " + key + " names a field its row does not name");
        std::set<std::string> fields = base;
        if (const OWJsonValue* fs = SchemaType(type_of[Str(c, "row")]))
            for (const OWJsonValue& f : fs->Items()) fields.insert(Str(f, "name"));
        t.Ok(fields.count(Str(c, "field")) == 1, key + ": not a field of " + type_of[Str(c, "row")]);
        OWBytes page;
        OWFiles::Read(DistPath(Str(c, "page")), page);
        std::vector<std::string> lines;
        std::string line;
        for (uint8_t b : page) {
            if (b == '\n') {
                lines.push_back(line);
                line.clear();
            } else {
                line.push_back(char(b));
            }
        }
        lines.push_back(line);
        const OWJsonValue* at = c.Find("line");
        const long n = at && at->IsNumber() ? long(at->AsDouble()) : 0;
        t.Ok(!page.empty() && n >= 1 && size_t(n) <= lines.size() &&
                 lines[size_t(n) - 1].find(Str(c, "quote")) != std::string::npos,
             key + ": the quote is not at " + Str(c, "page") + ":" + std::to_string(n));
        cited.insert(key);
    }
    for (const std::string& k : named) t.Ok(cited.count(k) == 1, "a field a row names has no cite: " + k);
}

// -- beside the fixture ----------------------------------------------------------------------------------------------

OW_TEST(views_anything_that_is_not_an_element_has_no_row_no_sentence_no_banners_no_fields) {
    const std::string none = Text(J(R"({"row": null, "sentence": null, "banners": [], "order": []})"));
    for (const char* bad : {"null", "1", "\"narrative\"", "[]", "true", "{}"})
        t.Eq(Text(OWViews::ViewOf(J(bad)).ToJson()), none, bad);
}

// The fixture's carriage-return case is CRLF, which a cut at the line feed alone also passes (the trim takes the CR left
// behind); a lone CR tells the two apart.
OW_TEST(views_the_sentence_is_cut_at_a_lone_carriage_return_as_at_a_line_feed) {
    for (const char* story : {"One.\\rTwo.", "One.\\nTwo.", "  \\r\\n One. \\rTwo."}) {
        OWView v = OWViews::ViewOf(
            J(std::string(R"({"type": "narrative", "supertype": "Message", "story": ")") + story + "\"}"));
        t.Eq(v.sentence.value_or("(none)"), std::string("One."), story);
    }
}

OW_TEST(views_each_answer_is_its_own) {
    const OWJsonValue el = J(R"({"type": "narrative", "supertype": "Message", "story": "Hello.", "hidden": true})");
    OWView first = OWViews::ViewOf(el);
    t.Eq(Join(first.banners), std::string("[hidden]"), "the banners");
    first.banners.push_back("changed");
    first.order.push_back("changed");
    t.Eq(Join(OWViews::ViewOf(el).banners), std::string("[hidden]"), "a caller changing one answer changes no later one");
    t.Eq(Join(OWViews::ViewOf(el).order), std::string("[story]"), "nor its order");
    t.Ok(first.row && InTable(first.row), "the row is the table's own");
    t.Eq(Text(OWViews::ViewOf(el).ToJson()),
         Text(J(R"({"row": "message", "sentence": "Hello.", "banners": ["hidden"], "order": ["story"]})")), "ToJson");
}

// The file's words (its `banners` sentence): a row's own banner marks are "each read by presence exactly as the room
// profile reads a mark"; since serial 126 an empty list or object is no mark.
OW_TEST(views_a_rows_own_banner_mark_is_read_by_presence) {
    const Table table = OwnTable({TestRow("noted", {"character", "", "", ""}, {"x_note"})});
    const std::pair<std::string, bool> vectors[] = {
        {std::string("\"") + World + "\"", true}, {"\"  \"", false}, {"\"\"", false}, {"7", true}, {"false", true},
        {"{}", false}, {"[]", false}, {R"({"by":"x"})", true}, {"null", false},
    };
    for (const auto& [mark, shown] : vectors) {
        OWView v = OWViews::ViewIn(table.banners, table.rows, J(R"({"type": "character", "x_note": )" + mark + "}"));
        t.Eq(RowId(v), std::string("noted"), mark);
        t.Eq(Join(v.banners), shown ? std::string("[x_note]") : std::string("[]"), mark);
    }
    t.Eq(Join(OWViews::ViewIn(table.banners, table.rows, J(R"({"type": "character"})")).banners), std::string("[]"),
         "an absent mark is no banner");
}

// The file's words (its `match` sentence): two rows on one kind are a fault "refused where it is built", and "a reader
// drawing a page never refuses over the table: where two rows match on one rung, the first in the file wins".
OW_TEST(views_a_tie_on_one_rung_the_first_row_wins) {
    const OWJsonValue el = J(R"({"type": "narrative", "supertype": "Message", "story": "Hi."})");
    const OWViewRow a = TestRow("first", {"narrative", "Message", "", ""});
    const OWViewRow b = TestRow("second", {"Narrative", " message ", "", ""});
    const OWViewRow plain = TestRow("plain", {"narrative", "", "", ""});
    Table ab = OwnTable({a, b}), ba = OwnTable({b, a}), pa = OwnTable({plain, a});
    t.Eq(RowId(OWViews::ViewIn(ab.banners, ab.rows, el)), std::string("first"), "a, b");
    t.Eq(RowId(OWViews::ViewIn(ba.banners, ba.rows, el)), std::string("second"), "b, a");
    t.Eq(RowId(OWViews::ViewIn(pa.banners, pa.rows, el)), std::string("first"), "a higher rung still wins from later in the table");
}

// The file's words (its `banners` sentence): x_served_trimmed shows "only when it names a field (its list holds at least
// one string with something left after trimming: a list of blanks shows no banner)".
OW_TEST(views_served_trimmed_is_a_banner_only_when_it_names_a_field) {
    const std::pair<const char*, bool> vectors[] = {
        {R"(["characters"])", true}, {R"(["narrator", "characters"])", true}, {R"([" ", "characters"])", true},
        {"[]", false}, {R"([""])", false}, {R"([" \t", "　"])", false}, {"[42]", false}, {"[null]", false},
        {R"("characters")", false},
    };
    for (const auto& [list, shown] : vectors) {
        OWView v = OWViews::ViewOf(
            J(std::string(R"({"type": "narrative", "supertype": "Message", "story": "Hi.", "x_served_trimmed": )") + list + "}"));
        t.Eq(Join(v.banners), shown ? std::string("[x_served_trimmed]") : std::string("[]"), list);
    }
}

// The order's section fields are the walk's: for every type, schema.json's own list in its order, and the lens
// reader's field table (the same walk) in the same order, but for the Pin's generic link, which the schema lists once as
// `element` and the table (as a body carries it) as its two keys. The type is folded; a word not one of the 22 has none.
OW_TEST(views_the_section_fields_are_the_walks_in_its_order) {
    const std::vector<std::string>& slugs = OWLens::TypeOrder();
    t.Ok(!slugs.empty(), "the schema's types");
    for (const std::string& slug : slugs) {
        std::vector<std::string> schema;
        if (const OWJsonValue* fs = SchemaType(slug))
            for (const OWJsonValue& f : fs->Items()) schema.push_back(Str(f, "name"));
        t.Ok(!schema.empty(), slug + " is in schema.json");
        t.Eq(Join(OWViews::SectionFields(slug)), Join(schema), slug + ": schema.json's fields in order");
        std::vector<std::string> lens;
        for (const OWLens::OWLensField& f : OWLens::FieldsOf(slug)) {
            if (f.kind == OWLens::OWFieldKind::GenericId) continue;
            std::string name = f.name;
            if (f.kind == OWLens::OWFieldKind::GenericType)
                for (const OWViewGenericLink& g : OWViews::GenericLinks(slug))
                    if (g.type_key == name) name = g.field;
            lens.push_back(name);
        }
        t.Eq(Join(OWViews::SectionFields(slug)), Join(lens), slug + ": the lens reader's fields, a generic link as one");
    }
    t.Eq(Join(OWViews::SectionFields(" Character ")), Join(OWViews::SectionFields("character")), "the type folded");
    for (const char* word : {"people", "", "characters"})
        t.Ok(OWViews::SectionFields(word).empty(), std::string("no fields for \"") + word + "\"");
}

// The generic links baked from the walk: every schema.json field of kind generic, with its two keys, and no other.
OW_TEST(views_the_generic_links_are_the_walks) {
    size_t links = 0;
    for (const std::string& slug : OWLens::TypeOrder()) {
        std::vector<std::string> want, got;
        if (const OWJsonValue* fs = SchemaType(slug))
            for (const OWJsonValue& f : fs->Items())
                if (Str(f, "kind") == "generic")
                    want.push_back(Str(f, "name") + ":" + Str(f, "type_key") + "+" + Str(f, "id_key"));
        for (const OWViewGenericLink& g : OWViews::GenericLinks(slug)) got.push_back(g.field + ":" + g.type_key + "+" + g.id_key);
        t.Eq(Join(got), Join(want), slug + "'s generic links");
        links += got.size();
    }
    t.Ok(links > 0, "the walk holds a generic link (a Pin's element)");
    t.Eq(OWViews::GenericLinks(" PIN ").size(), OWViews::GenericLinks("pin").size(), "the type folded");
}

// Version 3's document layout: a generic link (a Pin's `element`) is one field of the order, under its schema name and
// at its place, read as the two keys the walk gives it, never as two entries. It holds something only when both are
// strings with something left after trimming White_Space (U+3000 too). A body's own `element` key is never read.
OW_TEST(views_a_pins_generic_link_is_one_field_at_its_place_read_by_its_two_keys) {
    auto pin = [](const std::string& type_key, const std::string& id_key) {
        return J(R"({"type": "pin", "name": "The ferry", "map": "m1", "element_type": )" + type_key +
                 R"(, "element_id": )" + id_key + R"(, "x": 0, "y": 12, "z": 3})");
    };
    const std::string all = "[map, element, x, y, z]", none = "[map, x, y, z]";
    t.Eq(Join(OWViews::ViewOf(pin(R"("location")", R"("l1")")).order), all, "both keys hold");
    t.Eq(Join(OWViews::ViewOf(pin(R"(" location ")", R"(" l1")")).order), all, "a key with White_Space around something");
    t.Eq(Join(OWViews::ViewOf(pin(R"("location")", R"("　")")).order), none, "an ideographic space alone holds nothing");
    t.Eq(Join(OWViews::ViewOf(pin("null", R"("l1")")).order), none, "a null key");
    t.Eq(Join(OWViews::ViewOf(pin(R"("location")", "7")).order), none, "a key that is not a string");
    t.Eq(Join(OWViews::ViewOf(J(R"({"type": "Pin", "map": "m1", "element": "l1", "x": 0})")).order),
         std::string("[map, x]"), "a body's own element key is not read; the type folded");
}

// What holds something, field by field: a number (zero too) and a boolean (false too) hold it; a list of blank strings
// holds something here, whatever a banner reads of one; White_Space alone (U+3000 too) and an object with no keys hold
// nothing; an extension key and a base field are in no order of a kind with no row.
OW_TEST(views_a_field_holds_something_by_the_files_test) {
    const OWJsonValue el = J(R"({"type": " Character ", "physicality": "　 ", "mentality": [" "], "height": 0,
        "weight": false, "species": [], "background": {}, "motivations": {"a": 1}, "birthplace": null,
        "x_note": "an extension key is in no order", "name": "Sol", "description": "a base field is the page's head"})");
    t.Eq(Join(OWViews::ViewOf(el).order), std::string("[mentality, height, weight, motivations]"), "the order");
}
