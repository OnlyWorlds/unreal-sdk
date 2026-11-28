#include "OWWorld.h"
#include "OWAddress.h"
#include "OWUnicode.h"

#include <algorithm>
#include <set>
#include <unordered_set>

namespace OnlyWorlds {

const char* const OWWorld::ElementFolders[2] = {"elements", "spatial"};
const char* const OWWorld::FixedNames[3] = {"world.json", "elements", "spatial"};

namespace {

const char* const World = "world.json";

std::string AsciiLower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool EndsWith(const std::string& s, const std::string& tail) {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

// The four types the dated spatial/ layout held, the only type folders read under it (ruled 2026-11-25).
const char* const SpatialTypes[4] = {"map", "pin", "zone", "marker"};

// Names are read as written (ruled 2026-11-23). A type folder is one of the 22 type words in lower case, and under
// spatial/ one of the four it held: any other folder (elements/Character/, elements/vessel/, spatial/character/,
// spatial/Pin/) is not read.
bool IsTypeFolder(const std::string& top, const std::string& name) {
    if (top == "spatial") {
        for (const char* t : SpatialTypes)
            if (name == t) return true;
        return false;
    }
    for (const char* t : OWAddress::TypeWords)
        if (name == t) return true;
    return false;
}

// An element file's extension is .json in lower case: a name that ends so only in another case (X.JSON) is named and
// not read, never left to a file system that folds the name.
bool IsJsonInOtherCase(const std::string& name) { return !EndsWith(name, ".json") && EndsWith(AsciiLower(name), ".json"); }

const char* const NotJsonLower = ": the extension is not .json in lower case; not read, kept as it is";
const char* const NotTypeFolder = "/: not a type folder (one of the 22 type words, in lower case); not read, kept as it is";
const char* const NotSpatialFolder =
    "/: not a type folder under spatial/ (map, pin, zone, marker, in lower case); not read, kept as it is";

const char* NotTypeFolderWhy(const std::string& top) { return top == "spatial" ? NotSpatialFolder : NotTypeFolder; }

std::string StringField(const OWJsonValue& body, const char* key) {
    const OWJsonValue* v = body.IsObject() ? body.Find(key) : nullptr;
    return v && v->IsString() ? v->Text() : std::string();
}

std::string IdTail(const std::string& id) {
    const size_t n = 8;
    if (id.size() <= n) return id;
    std::string raw = id.substr(id.size() - n);
    if (raw.find("--") == std::string::npos && raw[0] != '-') return raw;
    std::string compact;
    for (char c : id)
        if (c != '-') compact.push_back(c);
    return compact.size() <= n ? compact : compact.substr(compact.size() - n);
}

}  // namespace

// -- the element --------------------------------------------------------------------------------------------------------

std::string OWElement::Id() const { return StringField(body, "id"); }
std::string OWElement::Name() const { return StringField(body, "name"); }

std::string OWElement::Type() const {
    // Folder spec 4.1 (ruled 2026-11-22): a declared type is a string exactly one of the 22 type slugs; anything else
    // ("Location", " location", "widget", 7, a list) declares nothing, and the folder fills it. Under spatial/ too: a
    // body there may declare any of the 22, as the elements/ arm reads it.
    std::string declared = StringField(body, "type");
    return IsTypeFolder("elements", declared) ? declared : folder_type;
}

void OWElement::Set(const std::string& key, OWJsonValue value) {
    body.Set(key, std::move(value));
    edited = true;
}

// -- the fixed names ----------------------------------------------------------------------------------------------------

std::string OWWorld::FixedNameFault(const std::vector<std::string>& names) {
    return FixedNameFault(names, std::vector<std::string>(std::begin(FixedNames), std::end(FixedNames)));
}

std::string OWWorld::FixedNameFault(const std::vector<std::string>& names, const std::vector<std::string>& wanted) {
    for (const std::string& want : wanted) {
        if (std::find(names.begin(), names.end(), want) != names.end()) continue;
        const std::string key = OWUnicode::Fold(want);
        std::vector<std::string> others;
        for (const std::string& n : names)
            if (OWUnicode::Fold(n) == key) others.push_back(n);
        if (others.empty()) continue;
        std::sort(others.begin(), others.end());
        others.erase(std::unique(others.begin(), others.end()), others.end());
        std::string list;
        for (const std::string& o : others) list += (list.empty() ? "'" : ", '") + o + "'";
        return "name_case: it holds " + list + " and no '" + want + "', and the name is read as written, so '" + others[0] +
               "' is not " + want + " and the world would read without it (folder spec section 4.8); rename it '" + want + "'";
    }
    return "";
}

std::string OWWorld::FolderFault(const std::string& path) {
    if (!OWFiles::IsDir(path)) return "";
    std::vector<std::string> names = OWFiles::List(path, false);
    for (const std::string& d : OWFiles::List(path, true)) names.push_back(d);
    return FixedNameFault(names);
}

std::string OWWorld::TypeFolderFault(const std::string& path, const std::vector<std::string>& rels) {
    for (const char* top : ElementFolders) {
        std::set<std::string> held;  // the type folders the target lists, as written
        if (OWFiles::IsDir(path + "/" + top))
            for (const std::string& d : OWFiles::List(path + "/" + top, true)) held.insert(d);
        std::set<std::string> written;  // the type folders this write puts files into
        for (const std::string& rel : rels) {
            size_t a = rel.find('/');
            size_t b = a == std::string::npos ? a : rel.find('/', a + 1);
            if (b != std::string::npos && rel.compare(0, a, top) == 0) written.insert(rel.substr(a + 1, b - a - 1));
        }
        std::set<std::string> all = held;
        all.insert(written.begin(), written.end());
        for (const std::string& name : written) {
            const std::string key = OWUnicode::Fold(name);
            for (const std::string& other : all) {
                if (other == name || OWUnicode::Fold(other) != key || (held.count(name) && held.count(other))) continue;
                const std::string& first = name < other ? name : other;
                const std::string& second = name < other ? other : name;
                return std::string(top) + "/" + first + "/ and " + top + "/" + second +
                       "/ are one folder on a disk that ignores case, and a type folder is read as written (folder spec "
                       "section 4.8), so a file written into " + top + "/" + name + "/ would be read by no one; rename the folder";
            }
        }
    }
    return "";
}

// -- reading ------------------------------------------------------------------------------------------------------------

std::unique_ptr<OWWorld> OWWorld::ReadFolder(const std::string& path_in, bool other_parts) {
    std::string path = path_in;
    std::replace(path.begin(), path.end(), '\\', '/');
    while (!path.empty() && path.back() == '/') path.pop_back();
    // Section 4.8: the root is listed and compared, so the lookups below find world.json, elements and spatial only as
    // written, on a disk that folds case too.
    if (!FolderFault(path).empty()) return nullptr;
    OWBytes bytes;
    if (!OWFiles::Read(path + "/" + World, bytes)) return nullptr;
    auto w = std::make_unique<OWWorld>();
    w->root = path;
    w->SetWorldJson(std::move(bytes));
    for (const char* top : ElementFolders) {
        std::string base = path + "/" + top;
        if (!OWFiles::IsDir(base)) continue;
        for (const std::string& name : OWFiles::List(base, false))  // a file loose in elements/: not an element
            if (name[0] != '.' && other_parts && !EndsWith(AsciiLower(name), ".meta")) OWFiles::Read(base + "/" + name, w->other_files[std::string(top) + "/" + name]);
        for (const std::string& type : OWFiles::List(base, true)) {
            if (type[0] == '.') continue;
            std::string dir = base + "/" + type;
            if (!IsTypeFolder(top, type)) {
                w->errors.push_back(std::string(top) + "/" + type + NotTypeFolderWhy(top));
                if (other_parts) OWWorldArchive::CollectContent(dir, std::string(top) + "/" + type, w->other_files);
                continue;
            }
            for (const std::string& name : OWFiles::List(dir, false)) {
                if (name[0] == '.') continue;
                std::string rel = std::string(top) + "/" + type + "/" + name;
                OWBytes b;
                OWFiles::Read(dir + "/" + name, b);
                if (EndsWith(name, ".json")) {
                    w->AddParsed(rel, type, std::move(b));
                    continue;
                }
                if (IsJsonInOtherCase(name)) w->errors.push_back(rel + NotJsonLower);
                if (other_parts && !EndsWith(AsciiLower(name), ".meta")) {
                    w->other_files[rel] = std::move(b);
                }
            }
            if (other_parts)
                for (const std::string& sub : OWFiles::List(dir, true))
                    if (sub[0] != '.') OWWorldArchive::CollectContent(dir + "/" + sub, std::string(top) + "/" + type + "/" + sub, w->other_files);
        }
    }
    if (other_parts) {
        // The rest of the world's parts as Pack selects them (records/ whole, private producers included: a folder
        // written from this world is the same world, not a share).
        if (OWFiles::IsDir(path + "/media")) OWWorldArchive::CollectContent(path + "/media", "media", w->other_files);
        OWWorldArchive::CollectRecords(path, w->other_files);
    }
    return w;
}

std::unique_ptr<OWWorld> OWWorld::FromFiles(const OWFileMap& files) {
    std::vector<std::string> tops;
    for (const auto& kv : files) tops.push_back(kv.first.substr(0, kv.first.find('/')));
    if (!FixedNameFault(tops).empty()) return nullptr;
    auto w = std::make_unique<OWWorld>();
    bool have_world = false;
    std::set<std::string> no_type;  // each folder that is no type, named once
    for (const auto& kv : files) {
        const std::string& rel = kv.first;
        if (rel == World) {
            w->SetWorldJson(kv.second);
            have_world = true;
            continue;
        }
        size_t a = rel.find('/');
        size_t b = a == std::string::npos ? a : rel.find('/', a + 1);
        bool in_folder = b != std::string::npos && (rel.compare(0, a, "elements") == 0 || rel.compare(0, a, "spatial") == 0) &&
                         rel[a + 1] != '.';
        std::string type = in_folder ? rel.substr(a + 1, b - a - 1) : std::string();
        if (in_folder && !IsTypeFolder(rel.substr(0, a), type)) {
            if (no_type.insert(rel.substr(0, b)).second) w->errors.push_back(rel.substr(0, b) + NotTypeFolderWhy(rel.substr(0, a)));
            w->other_files[rel] = kv.second;
            continue;
        }
        bool direct = in_folder && rel.find('/', b + 1) == std::string::npos && rel[b + 1] != '.';
        if (direct && EndsWith(rel, ".json")) {
            w->AddParsed(rel, type, kv.second);
        } else {
            if (direct && IsJsonInOtherCase(rel)) w->errors.push_back(rel + NotJsonLower);
            w->other_files[rel] = kv.second;
        }
    }
    if (!have_world) {
        w->SetWorldJson(OWBytes());
        w->world_unread_.clear();  // no file to write over: a draft takes the caller's id
    }
    return w;
}

void OWWorld::SetWorldJson(OWBytes bytes) {
    world_bytes = std::move(bytes);
    OWJsonParseResult r = OWJson::Parse(world_bytes);
    if (!r.ok || !r.value.IsObject()) {
        world_unread_ = std::string("world.json: ")
                        + (r.ok ? "not a JSON object" : r.too_deep ? r.error : "not JSON (" + r.error + ")");
        errors.push_back(world_unread_);
        info = OWJsonValue::Object();
        return;
    }
    world_unread_.clear();
    info = std::move(r.value);
}

void OWWorld::AddParsed(const std::string& rel, const std::string& type, OWBytes bytes) {
    OWJsonParseResult r = OWJson::Parse(bytes);
    if (!r.ok || !r.value.IsObject()) {
        // A file nested past the depth is skipped by its name (OW-25), as the TypeScript SDK's folder reader skips it.
        errors.push_back(rel + ": " + (r.ok ? "not a JSON object" : r.too_deep ? r.error : "not JSON (" + r.error + ")"));
        other_files[rel] = std::move(bytes);
        return;
    }
    const OWJsonValue* id = r.value.Find("id");
    if (!id || !id->IsString() || id->Text().empty()) {
        // Folder spec 4.1: a file without a non-empty string id is skipped, left untouched, and logged.
        errors.push_back(rel + ": no id that is a non-empty string; skipped, kept as it is");
        other_files[rel] = std::move(bytes);
        return;
    }
    OWElement e;
    e.folder_type = type;
    e.source_path = rel;
    e.source_bytes = std::move(bytes);
    e.body = std::move(r.value);
    std::string eid = id->Text();
    auto found = by_id_.find(eid);
    if (found != by_id_.end()) {
        // One id, one file (section 4): the first read (elements/ before spatial/) is the one served.
        errors.push_back(rel + ": id " + eid + " is also " + elements[found->second].source_path);
    } else {
        by_id_.emplace(eid, elements.size());
    }
    elements.push_back(std::move(e));
}

// -- the world ----------------------------------------------------------------------------------------------------------

std::string OWWorld::Id() const { return StringField(info, "id"); }

OWElement* OWWorld::Get(const std::string& id) {
    auto it = by_id_.find(id);
    return it == by_id_.end() ? nullptr : &elements[it->second];
}

std::vector<OWElement*> OWWorld::OfType(const std::string& type) {
    std::vector<OWElement*> out;
    for (OWElement& e : elements)
        if (e.Type() == type) out.push_back(&e);
    return out;
}

void OWWorld::SetInfo(const std::string& key, OWJsonValue value) {
    info.Set(key, std::move(value));
    info_edited = true;
}

OWElement& OWWorld::Add(const std::string& type, OWJsonValue body) {
    OWElement e;
    e.folder_type = type;
    e.body = std::move(body);
    e.edited = true;
    std::string id = e.Id();
    if (!id.empty() && !by_id_.count(id)) by_id_.emplace(id, elements.size());
    elements.push_back(std::move(e));
    return elements.back();
}

// -- writing ------------------------------------------------------------------------------------------------------------

std::string OWWorld::Slugify(const std::string& name) {
    std::string slug;
    bool pending = false;
    for (char32_t c : OWUnicode::CompatDecompose(OWUnicode::Decode(name))) {
        // The combining diacritics (U+0300 to U+036F) belong to the letter before them: dropped, not a break.
        // Every other mark is outside a-z and 0-9 like any other character (section 3.2, as the packages do).
        if (c >= 0x300 && c <= 0x36F) continue;
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (alnum) {
            if (pending && !slug.empty()) slug.push_back('-');
            pending = false;
            slug.push_back(static_cast<char>(c));
        } else {
            pending = true;
        }
    }
    if (slug.size() > 40) {
        slug.resize(40);  // cut, then strip a trailing hyphen (section 3.2)
        while (!slug.empty() && slug.back() == '-') slug.pop_back();
    }
    return slug;
}

std::string OWWorld::ElementFilename(const std::string& id, const std::string& name, bool full_id) {
    std::string slug = Slugify(name);
    if (slug.empty()) return id + ".json";
    return slug + "--" + (full_id ? id : IdTail(id)) + ".json";
}

namespace {

// Why a new element's id cannot name its file, or "" (folder spec section 3.2): a file name is made from an id only when
// the id is one plain file name and the name passes the archive's own name checks. First the rules the TypeScript and
// Python packages hold, in their order and their words (their shared cases, unsafe-ids.cases.json): empty, a '/' or
// '\', a colon, a control character U+0000 to U+001F, "." or "..". Then the archive's name checks
// (OWWorldArchive::PathFault and NameFault, section 4.7's OW-16 and OW-22), wider than theirs, in the archive's own
// words: U+007F and U+0080 to U+009F, another character Windows refuses, a device name, a trailing dot or space. The
// short name and the full-id form are both asked, whatever the name, since the collision rule may take the full id at
// any time.
std::string IdRefusal(const OWElement& e) {
    const std::string id = e.Id(), type = e.folder_type;
    std::string why;
    if (id.empty()) why = "it is empty";
    else if (id.find_first_of("/\\") != std::string::npos) why = "it holds a path separator, / or \\";
    else if (id.find(':') != std::string::npos) why = "it holds a colon";
    else if (std::any_of(id.begin(), id.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; }))
        why = "it holds a control character";
    else if (id == "." || id == "..") why = "it is . or ..";
    else {
        for (bool full : {false, true}) {
            std::string file = OWWorld::ElementFilename(id, e.Name(), full);
            std::string fault = OWWorldArchive::PathFault(file);
            if (fault.empty()) fault = OWWorldArchive::NameFault(file);
            if (fault.empty()) continue;
            why = "its file name " + OWJson::Quote(file) + " " + fault;
            break;
        }
    }
    return why.empty() ? why : type + " element id " + OWJson::Quote(id) + " cannot be a file name: " + why + ".";
}

}  // namespace

std::vector<std::pair<size_t, std::string>> OWWorld::NewPaths() const {
    std::vector<size_t> fresh;
    std::unordered_set<std::string> taken;  // case-folded as ASCII: on NTFS and APFS two spellings are one file
    for (size_t i = 0; i < elements.size(); ++i) {
        if (elements[i].source_path.empty()) fresh.push_back(i);
        else taken.insert(AsciiLower(elements[i].source_path));
    }
    // A file the reader could not read lives here too, so a new element never takes its name.
    for (const auto& kv : other_files) taken.insert(AsciiLower(kv.first));
    // The writer collision rule (section 3.2): a taken short path falls back to the full id, lowest id first.
    std::sort(fresh.begin(), fresh.end(), [this](size_t a, size_t b) { return elements[a].Id() < elements[b].Id(); });
    std::vector<std::pair<size_t, std::string>> out;
    for (size_t i : fresh) {
        const OWElement& e = elements[i];
        std::string id = e.Id();
        if (id.empty()) continue;  // WriteRefusal names it
        // No folder a reader reads, or an id that names no file in it: no path, and no name taken from a sibling.
        if (!IsTypeFolder("elements", e.folder_type) || !IdRefusal(e).empty()) {
            out.emplace_back(i, std::string());
            continue;
        }
        std::string dir = std::string("elements/") + e.folder_type + "/";
        std::string path = dir + ElementFilename(id, e.Name());
        if (taken.count(AsciiLower(path))) path = dir + ElementFilename(id, e.Name(), true);
        if (!taken.insert(AsciiLower(path)).second) path.clear();  // both names held: never written over
        out.emplace_back(i, path);
    }
    return out;
}

void OWWorld::AssignPaths() {
    for (const OWElement& e : elements)
        if (e.source_path.empty() && e.Id().empty()) errors.push_back("a new " + e.folder_type + " has no id; not written");
    for (const auto& [i, path] : NewPaths())
        if (!path.empty()) elements[i].source_path = path;
}

OWFileMap OWWorld::ToFiles(bool only_edited) {
    AssignPaths();
    OWFileMap out;
    if (!only_edited) out = other_files;
    if (info_edited && world_unread_.empty()) out[World] = OWJson::ToBytes(info);
    else if (!only_edited) out[World] = world_bytes;
    for (const OWElement& e : elements) {
        if (e.source_path.empty() || (only_edited && !e.edited)) continue;
        out[e.source_path] = e.edited ? OWJson::ToBytes(e.body) : e.source_bytes;
    }
    return out;
}

std::string OWWorld::WriteRefusal(bool only_edited) const {
    if (!world_unread_.empty() && (info_edited || !only_edited))
        return world_unread_ + ", so it could not be read, and a writer never writes over a file it could not read: "
               "mend the file and read the world again";
    if (info_edited || !only_edited) {
        const OWJsonValue* id = info.IsObject() ? info.Find("id") : nullptr;
        if (!id || !id->IsString() || OWUnicode::StripWhite(id->Text()).empty())
            return "world.json has no id (a non-empty string), and a writer mints none: set it first (SetInfo(\"id\", ...))";
    }
    for (const OWElement& e : elements) {
        if (only_edited && !e.edited) continue;
        const OWJsonValue* id = e.body.Find("id");
        if ((e.edited || e.source_path.empty()) && !(id && id->IsString() && !id->Text().empty()))
            return "a new or edited " + e.folder_type + " has no id (a non-empty string), and a writer mints none: set it first";
        // A new element's file goes to elements/<folder_type>/, and a reader reads a type folder only when it is one of
        // the 22 as written (section 4.8): any other type ("Character", "widget", "", a path such as "../x", which would
        // put the file outside elements/) is refused. An element read from a file always has one of the 22 here.
        if (!IsTypeFolder("elements", e.folder_type)) {
            if (e.folder_type.empty())
                return "a new element (" + e.Id() + ") has no type, and a writer has no type folder to put it in: Add it "
                       "with one of the 22 type slugs";
            std::string words;
            for (const char* w : OWAddress::TypeWords) words += (words.empty() ? "" : ", ") + std::string(w);
            return "a new element (" + e.Id() + ") has the type '" + e.folder_type + "', which is not one of the 22 type "
                   "slugs as the standard spells them (" + words + "): a reader reads no other type folder, so nothing is "
                   "written";
        }
        // A new element's file name is made from its id: one that cannot be one file name in its type folder is refused,
        // whatever its name (IdRefusal; NewPaths gives it no path, so it stays new after ToFiles).
        if (e.source_path.empty())
            if (std::string why = IdRefusal(e); !why.empty()) return why;
    }
    for (const auto& [i, path] : NewPaths()) {
        if (!path.empty()) continue;
        const OWElement& e = elements[i];
        std::string dir = std::string("elements/") + e.folder_type + "/";
        return "a new " + e.folder_type + " (" + e.Id() + ") would be written at " + dir + ElementFilename(e.Id(), e.Name()) +
               " or " + dir + ElementFilename(e.Id(), e.Name(), true) +
               ", and the world holds both: a writer never writes over a file it holds; rename it, or move one of those files";
    }
    return "";
}

bool OWWorld::WriteFolder(const std::string& path, bool only_edited, std::string* error) {
    if (std::string why = WriteRefusal(only_edited); !why.empty()) {
        if (error) *error = why;
        return false;
    }
    if (std::string why = FolderFault(path); !why.empty()) {
        if (error) *error = "nothing written to the world folder " + path + ", " + why;
        return false;
    }
    const OWFileMap files = ToFiles(only_edited);
    std::vector<std::string> rels;
    for (const auto& kv : files) rels.push_back(kv.first);
    if (std::string why = TypeFolderFault(path, rels); !why.empty()) {
        if (error) *error = "nothing written to the world folder " + path + ": " + why;
        return false;
    }
    // The second guard, whatever made a name: every file's resolved path lies inside the folder written.
    const std::string inside = OWFiles::Absolute(path) + "/";
    for (const auto& kv : files) {
        if (OWFiles::Absolute(path + "/" + kv.first).rfind(inside, 0) != 0) {
            if (error)
                *error = "nothing written to the world folder " + path + ": the file " + OWJson::Quote(kv.first) +
                         " would not lie inside it";
            return false;
        }
    }
    std::unordered_set<std::string> made;  // each folder made once, not once per file
    for (const auto& kv : files) {
        std::string full = path + "/" + kv.first;
        std::string dir = full.substr(0, full.rfind('/'));
        if (made.insert(dir).second && !OWFiles::MakeDirs(dir)) {
            if (error) *error = "cannot make " + dir;
            return false;
        }
        if (!OWFiles::Write(full, kv.second, false)) {
            if (error) *error = "cannot write " + path + "/" + kv.first;
            return false;
        }
    }
    return true;
}

OWPacked OWWorld::Pack(const std::string& top, bool keep_api, bool include_private, bool include_author_fields,
                       bool include_hidden) {
    if (std::string why = WriteRefusal(); !why.empty()) {
        OWPacked refused;
        refused.message = why;
        return refused;
    }
    return OWWorldArchive::PackWorld(ToFiles(), top, keep_api, include_private, include_author_fields, include_hidden);
}

}  // namespace OnlyWorlds
