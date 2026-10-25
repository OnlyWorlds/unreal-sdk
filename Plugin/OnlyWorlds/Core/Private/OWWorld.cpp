#include "OWWorld.h"
#include "OWUnicode.h"

#include <algorithm>
#include <unordered_set>

namespace OnlyWorlds {

const char* const OWWorld::ElementFolders[2] = {"elements", "spatial"};

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
    std::string declared = StringField(body, "type");
    return declared.empty() ? folder_type : declared;
}

void OWElement::Set(const std::string& key, OWJsonValue value) {
    body.Set(key, std::move(value));
    edited = true;
}

// -- reading ------------------------------------------------------------------------------------------------------------

std::unique_ptr<OWWorld> OWWorld::ReadFolder(const std::string& path_in, bool other_parts) {
    std::string path = path_in;
    std::replace(path.begin(), path.end(), '\\', '/');
    while (!path.empty() && path.back() == '/') path.pop_back();
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
            for (const std::string& name : OWFiles::List(dir, false)) {
                if (name[0] == '.') continue;
                std::string rel = std::string(top) + "/" + type + "/" + name;
                OWBytes b;
                OWFiles::Read(dir + "/" + name, b);
                if (EndsWith(name, ".json")) {
                    w->AddParsed(rel, type, std::move(b));
                } else if (other_parts && !EndsWith(AsciiLower(name), ".meta")) {
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
    auto w = std::make_unique<OWWorld>();
    bool have_world = false;
    for (const auto& kv : files) {
        const std::string& rel = kv.first;
        if (rel == World) {
            w->SetWorldJson(kv.second);
            have_world = true;
            continue;
        }
        size_t a = rel.find('/');
        size_t b = a == std::string::npos ? a : rel.find('/', a + 1);
        bool element = b != std::string::npos && rel.find('/', b + 1) == std::string::npos &&
                       (rel.compare(0, a, "elements") == 0 || rel.compare(0, a, "spatial") == 0) &&
                       rel[a + 1] != '.' && rel[b + 1] != '.' && EndsWith(rel, ".json");
        if (element) {
            w->AddParsed(rel, rel.substr(a + 1, b - a - 1), kv.second);
        } else {
            w->other_files[rel] = kv.second;
        }
    }
    if (!have_world) w->SetWorldJson(OWBytes());
    return w;
}

void OWWorld::SetWorldJson(OWBytes bytes) {
    world_bytes = std::move(bytes);
    OWJsonParseResult r = OWJson::Parse(world_bytes);
    if (!r.ok || !r.value.IsObject()) {
        errors.push_back(std::string("world.json: ") + (r.ok ? "not a JSON object" : "not JSON (" + r.error + ")"));
        info = OWJsonValue::Object();
        return;
    }
    info = std::move(r.value);
}

void OWWorld::AddParsed(const std::string& rel, const std::string& type, OWBytes bytes) {
    OWJsonParseResult r = OWJson::Parse(bytes);
    if (!r.ok || !r.value.IsObject()) {
        errors.push_back(rel + ": " + (r.ok ? "not a JSON object" : "not JSON (" + r.error + ")"));
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
        // A combining mark belongs to the letter before it: dropped, not a break.
        if (OWUnicode::IsNonspacingMark(c)) continue;
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

void OWWorld::AssignPaths() {
    std::vector<OWElement*> fresh;
    std::unordered_set<std::string> taken;  // case-folded as ASCII: on NTFS and APFS two spellings are one file
    for (OWElement& e : elements) {
        if (e.source_path.empty()) fresh.push_back(&e);
        else taken.insert(AsciiLower(e.source_path));
    }
    for (const auto& kv : other_files) taken.insert(AsciiLower(kv.first));
    // The writer collision rule (section 3.2): a taken short path falls back to the full id, lowest id first.
    std::sort(fresh.begin(), fresh.end(), [](OWElement* a, OWElement* b) { return a->Id() < b->Id(); });
    for (OWElement* e : fresh) {
        std::string id = e->Id();
        if (id.empty()) {
            errors.push_back("a new " + e->folder_type + " has no id; not written");
            continue;
        }
        std::string dir = std::string("elements/") + e->folder_type + "/";
        std::string path = dir + ElementFilename(id, e->Name());
        if (taken.count(AsciiLower(path))) path = dir + ElementFilename(id, e->Name(), true);
        taken.insert(AsciiLower(path));
        e->source_path = path;
    }
}

OWFileMap OWWorld::ToFiles(bool only_edited) {
    AssignPaths();
    OWFileMap out;
    if (!only_edited) out = other_files;
    if (info_edited) out[World] = OWJson::ToBytes(info);
    else if (!only_edited) out[World] = world_bytes;
    for (const OWElement& e : elements) {
        if (e.source_path.empty() || (only_edited && !e.edited)) continue;
        out[e.source_path] = e.edited ? OWJson::ToBytes(e.body) : e.source_bytes;
    }
    return out;
}

bool OWWorld::WriteFolder(const std::string& path, bool only_edited, std::string* error) {
    std::unordered_set<std::string> made;  // each folder made once, not once per file
    for (const auto& kv : ToFiles(only_edited)) {
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

OWPacked OWWorld::Pack(const std::string& top, bool keep_api, bool include_private) {
    return OWWorldArchive::PackWorld(ToFiles(), top, keep_api, include_private);
}

}  // namespace OnlyWorlds
