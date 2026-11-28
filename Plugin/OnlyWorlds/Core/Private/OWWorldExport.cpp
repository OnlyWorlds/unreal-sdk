#include "OWWorldExport.h"
#include "OWAddress.h"

#include <unordered_map>
#include <utility>

namespace OnlyWorlds {
namespace OWWorldExport {

const char* const Format = "onlyworlds-world-export";
const char* const Reasons[5] = {"no_file", "not_json", "too_deep", "not_an_export", "no_world_id"};

}  // namespace OWWorldExport

namespace {

bool IsTypeWord(const std::string& s) {
    for (const char* w : OWAddress::TypeWords)
        if (s == w) return true;
    return false;
}

std::string StringField(const OWJsonValue& v, const char* key) {
    const OWJsonValue* f = v.IsObject() ? v.Find(key) : nullptr;
    return f && f->IsString() ? f->Text() : std::string();
}

OWExportRead Refused(const char* reason, std::string message) {
    OWExportRead r;
    r.reason = reason;
    r.message = std::move(message);
    return r;
}

}  // namespace

// Holds each body as read into the world: no file (its path is given by section 3.2 when the world is written), not
// edited, its bytes the body in section 5's form with each number as it was written.
struct OWExportBuilder {
    OWWorld& w;
    std::unordered_map<std::string, std::string> place_of;  // each held id, where it was read

    void World(OWJsonValue block) {
        w.info = std::move(block);
        w.world_bytes = OWJson::ToBytes(w.info, /*keep_number_text=*/true);
    }

    void Element(const std::string& at, const std::string& group, OWJsonValue body) {
        const std::string id = StringField(body, "id");
        if (id.empty()) {
            // Folder spec 4.1: a body without a non-empty string id is skipped and logged.
            w.errors.push_back(at + ": no id that is a non-empty string; skipped");
            return;
        }
        auto found = place_of.find(id);
        if (found != place_of.end()) {
            // One id, one element (section 4): the first read is the one served.
            w.errors.push_back(at + ": id " + id + " is also " + found->second + "; skipped");
            return;
        }
        place_of.emplace(id, at);
        // Folder spec 4.1: a declared type is a string exactly one of the 22 type slugs; else the group's key fills it.
        const std::string declared = StringField(body, "type");
        OWElement e;
        e.folder_type = IsTypeWord(declared) ? declared : group;
        e.source_bytes = OWJson::ToBytes(body, /*keep_number_text=*/true);
        e.body = std::move(body);
        w.by_id_.emplace(id, w.elements.size());
        w.elements.push_back(std::move(e));
    }
};

namespace OWWorldExport {

OWExportRead Read(const OWBytes& raw, const std::string& source) {
    const std::string where = source.empty() ? std::string("the export") : source;

    OWJsonParseResult parsed = OWJson::Parse(raw);
    if (!parsed.ok && parsed.too_deep)
        return Refused("too_deep", where + " is refused, " + OWJson::TooDeepWords +
                                       ": the file is one JSON text, read whole");
    if (!parsed.ok) return Refused("not_json", where + " is not valid JSON (" + parsed.error + ")");
    OWJsonValue& doc = parsed.value;
    if (!doc.IsObject()) return Refused("not_json", where + " is not a JSON object");

    const OWJsonValue* format = doc.Find("format");
    const bool named = format && format->IsString();
    if (!named || format->Text() != Format) {
        OWExportRead r = Refused("not_an_export", where + " is not an OnlyWorlds world export (format is '" +
                                                      (named ? format->Text() : std::string("missing")) + "', not '" +
                                                      Format + "')");
        r.format = named ? format->Text() : std::string();
        return r;
    }

    OWJsonValue* block = doc.Find("world");
    const std::string world_id = block ? StringField(*block, "id") : std::string();
    if (world_id.empty()) {
        OWExportRead r = Refused("no_world_id", where + " has no world id (a non-empty string): a world without one "
                                                        "cannot be keyed or compared");
        r.format = Format;
        return r;
    }

    OWExportRead r;
    r.ok = true;
    r.format = Format;
    r.schema_version = StringField(doc, "schema_version");
    r.exported_at = StringField(doc, "exported_at");
    r.world_id = world_id;
    r.message = where + " is read";
    r.world = std::make_unique<OWWorld>();
    OWExportBuilder build{*r.world, {}};
    build.World(std::move(*block));

    OWJsonValue* groups = doc.Find("elements");
    if (!groups) return r;  // only types with an element appear (section 8c): none is an empty world
    if (!groups->IsObject()) {
        r.world->errors.push_back(where + "#elements: not an object; nothing read");
        return r;
    }
    for (size_t g = 0; g < groups->Size(); ++g) {
        const std::string& slug = groups->Keys()[g];
        OWJsonValue& list = groups->Items()[g];
        const std::string group_at = where + "#elements/" + slug;
        if (!IsTypeWord(slug)) {
            // As the folder reader treats elements/vessel/: a group is a type slug as the standard spells it.
            r.world->errors.push_back(group_at + ": not a type (one of the 22 type words, in lower case); not read");
            continue;
        }
        if (!list.IsArray()) {
            r.world->errors.push_back(group_at + ": not a list; not read");
            continue;
        }
        for (size_t i = 0; i < list.Size(); ++i) {
            const std::string at = group_at + "/" + std::to_string(i);
            OWJsonValue& body = list.Items()[i];
            if (!body.IsObject()) {
                r.world->errors.push_back(at + ": not an object; skipped");
                continue;
            }
            build.Element(at, slug, std::move(body));
        }
    }
    return r;
}

OWExportRead ReadFile(const std::string& path) {
    OWBytes bytes;
    if (!OWFiles::IsFile(path) || !OWFiles::Read(path, bytes))
        return Refused("no_file", "no export file at " + path);
    return Read(bytes, path);
}

}  // namespace OWWorldExport
}  // namespace OnlyWorlds
