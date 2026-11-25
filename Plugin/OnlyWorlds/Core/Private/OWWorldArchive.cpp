#include "OWWorldArchive.h"
#include "OWUnicode.h"

#include <algorithm>
#include <charconv>
#include <set>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace OnlyWorlds {
namespace OWWorldArchive {

const char* const MediaType = "application/vnd.onlyworlds.world+zip";
const char* const Marker = "mimetype";
const char* const Extension = "owworld";
const char* const Reasons[12] = {"unsafe_path", "too_big", "too_many", "not_a_world", "more_than_one", "not_openable",
                                 "duplicate",   "collision", "link",   "method",      "name",          "marker"};

namespace {

const char* const World = "world.json";
constexpr uint32_t SIfReg = 0x8000;
constexpr uint32_t SIfDir = 0x4000;

std::vector<std::string> Split(const std::string& s) {
    std::vector<std::string> out;
    size_t a = 0;
    while (true) {
        size_t b = s.find('/', a);
        out.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) return out;
        a = b + 1;
    }
}

bool EndsWith(const std::string& s, const std::string& tail) {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

bool StartsWith(const std::string& s, const std::string& head) { return s.compare(0, head.size(), head) == 0; }

std::string RStripSlash(std::string s) {
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

// A name for a message: printable ASCII as itself, anything else as \xNN.
std::string Show(const std::string& s) {
    static const char* d = "0123456789abcdef";
    std::string out = "'";
    for (unsigned char c : s) {
        if (c >= 0x20 && c < 0x7F && c != '\\' && c != '\'') {
            out.push_back(static_cast<char>(c));
        } else {
            out += "\\x";
            out.push_back(d[c >> 4]);
            out.push_back(d[c & 15]);
        }
    }
    return out + "'";
}

bool IsJunk(const std::string& name) {
    for (const std::string& seg : Split(name))
        if (seg == "__MACOSX" || seg == ".DS_Store" || seg == "Thumbs.db") return true;
    return false;
}

OWArchiveRead& Refuse(OWArchiveRead& r, const char* reason, std::string message) {
    r.ok = false;
    r.reason = reason;
    r.message = std::move(message);
    r.files.clear();
    r.world = OWJsonValue();
    return r;
}

bool IsReserved(const std::string& seg) {
    // The base before the first dot, upper-cased as ASCII (the reserved names are ASCII).
    std::string base = seg.substr(0, seg.find('.'));
    for (char& c : base)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    static const std::set<std::string> reserved = {"CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4",
                                                   "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
                                                   "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    return reserved.count(base) > 0;
}

}  // namespace

std::string PathFault(const std::string& name) {
    if (name.find('\\') != std::string::npos) return "holds a backslash";
    if (!name.empty() && name[0] == '/') return "is absolute";
    if (name.size() >= 2 && name[1] == ':' &&
        ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z')))
        return "names a drive";
    for (const std::string& seg : Split(name))
        if (seg.empty() || seg == "." || seg == "..") return "has an empty, '.' or '..' segment";
    return "";
}

std::string NameFault(const std::string& name) {
    for (const std::string& seg : Split(name)) {
        for (char32_t c : OWUnicode::Decode(seg)) {
            // General category Cc (U+0000..U+001F, U+007F..U+009F), or one of <>:"|?*
            if (c < 0x20 || (c >= 0x7F && c <= 0x9F) || (c < 0x80 && std::string("<>:\"|?*").find(static_cast<char>(c)) != std::string::npos))
                return "segment " + Show(seg) + " holds a reserved or control character";
        }
        if (!seg.empty() && (seg.back() == '.' || seg.back() == ' '))
            return "segment " + Show(seg) + " ends in a dot or a space";
        if (IsReserved(seg)) return "segment " + Show(seg) + " is a Windows reserved name";
    }
    return "";
}

OWArchiveRead Read(const OWBytes& raw, const OWArchiveBudget& budget) {
    OWArchiveRead r;
    OWZipWalk w = OWZip::Walk(raw.data(), raw.size());
    if (!w.error.empty()) return Refuse(r, w.error == "name" ? "name" : "not_openable", w.message);
    r.entries = w.entries;

    // Every entry, from the central directory alone, before anything is inflated.
    std::unordered_set<std::string> seen;
    std::vector<std::string> seen_order;
    std::vector<const OWZipEntry*> files;
    uint64_t declared = 0;
    for (const OWZipEntry& e : r.entries) {
        const std::string& name = e.name;
        bool is_dir = e.IsDir();
        std::string fault = PathFault(is_dir ? name.substr(0, name.size() - 1) : name);
        if (!fault.empty()) return Refuse(r, "unsafe_path", "entry " + Show(name) + " " + fault);
        uint32_t kind = e.UnixType();
        if (kind != 0 && kind != SIfReg && kind != SIfDir)
            return Refuse(r, "link", "entry " + Show(name) + " has a Unix type that is a link or a special file");
        if (e.flags & (OWZip::FlagEncrypted | OWZip::FlagStrong))
            return Refuse(r, "method", "entry " + Show(name) + " is encrypted");
        if (e.method != 0 && e.method != 8)
            return Refuse(r, "method", "entry " + Show(name) + " uses method " + std::to_string(e.method) + " (only 0 and 8)");
        if (!e.Utf8Flag() && !OWUnicode::IsAscii(name))
            return Refuse(r, "name", "entry " + Show(name) + " is not ASCII and lacks the UTF-8 flag");
        fault = NameFault(RStripSlash(name));
        if (!fault.empty()) return Refuse(r, "name", "entry " + Show(name) + ": " + fault);
        if (!seen.insert(name).second) return Refuse(r, "duplicate", "entry " + Show(name) + " appears twice");
        seen_order.push_back(name);
        if (is_dir) continue;
        files.push_back(&e);
        if (files.size() > budget.max_entries)
            return Refuse(r, "too_many", "more than " + std::to_string(budget.max_entries) + " files");
        declared += e.usize;
        if (declared > budget.max_bytes)
            return Refuse(r, "too_big", "declares more than " + std::to_string(budget.max_bytes) + " bytes");
    }

    // OW-18: no two names (or folders) one after folding (NFC, then simple case folding).
    {
        std::unordered_set<std::string> prefixes;
        std::unordered_map<std::string, std::string> folded;
        for (const std::string& name : seen_order) {
            std::string prefix;
            for (const std::string& seg : Split(RStripSlash(name))) {
                prefix = prefix.empty() ? seg : prefix + "/" + seg;
                if (!prefixes.insert(prefix).second) continue;
                std::string key = OWUnicode::Fold(prefix);
                auto it = folded.find(key);
                if (it != folded.end())
                    return Refuse(r, "collision",
                                  "names collide after NFC and case folding: " + Show(it->second) + ", " + Show(prefix));
                folded.emplace(std::move(key), prefix);
            }
        }
    }

    // OW-19: no path is both a file and a folder.
    std::unordered_map<std::string, const OWZipEntry*> names;
    for (const OWZipEntry* e : files) names.emplace(e->name, e);
    for (const OWZipEntry* e : files) {
        std::string prefix;
        std::vector<std::string> segs = Split(e->name);
        for (size_t i = 0; i + 1 < segs.size(); ++i) {
            prefix = i == 0 ? segs[i] : prefix + "/" + segs[i];
            if (names.count(prefix)) return Refuse(r, "unsafe_path", Show(prefix) + " is a file and a folder");
        }
    }

    std::string reason, message;
    // OW-6: a marker is never required; one present with other bytes is refused. Its shape binds the writer only.
    auto marker = names.find(Marker);
    if (marker != names.end()) {
        OWBytes got;
        if (!OWZip::Inflate(raw.data(), raw.size(), *marker->second, got, reason, message, &r.largest_allocation))
            return Refuse(r, reason == "too_big" ? "too_big" : "not_openable", message);
        std::string want = MediaType;
        if (got != OWBytes(want.begin(), want.end()))
            return Refuse(r, "marker", "mimetype holds other bytes than the media type");
    }

    // OW-2: exactly one world; OW-4: anything beside its folder is ignored, a dot-folder never a world.
    std::vector<std::string> candidates;
    for (const OWZipEntry* e : files)
        if (e->name != Marker && !IsJunk(e->name)) candidates.push_back(e->name);
    if (std::find(candidates.begin(), candidates.end(), World) != candidates.end()) {
        r.prefix = "";
    } else {
        std::set<std::string> roots;
        for (const std::string& n : candidates)
            if (std::count(n.begin(), n.end(), '/') == 1 && EndsWith(n, std::string("/") + World) && n[0] != '.')
                roots.insert(n.substr(0, n.find('/') + 1));
        if (roots.empty()) return Refuse(r, "not_a_world", "no world.json at the root or in one top-level folder");
        if (roots.size() > 1) {
            std::string list;
            for (const std::string& x : roots) list += (list.empty() ? "" : ", ") + Show(x);
            return Refuse(r, "more_than_one", "worlds in " + list);
        }
        r.prefix = *roots.begin();
    }

    // OW-5: no world.json inside the world but its own, except a producer's under records/.
    std::vector<std::string> inside;
    for (const std::string& n : candidates) {
        if (!StartsWith(n, r.prefix)) continue;
        inside.push_back(n);
        std::string rel = n.substr(r.prefix.size());
        if (rel != World && EndsWith(rel, std::string("/") + World) && !StartsWith(rel, "records/"))
            return Refuse(r, "more_than_one", "a second world.json inside the world: " + Show(n));
    }

    // OW-14: every entry inflated within what it declares, its CRC and length right.
    std::unordered_map<std::string, OWBytes> content;
    for (const OWZipEntry* e : files) {
        OWBytes data;
        if (!OWZip::Inflate(raw.data(), raw.size(), *e, data, reason, message, &r.largest_allocation))
            return Refuse(r, reason == "too_big" ? "too_big" : "not_openable", message);
        content[e->name] = std::move(data);
    }

    // OW-3: the world's own world.json is a JSON object whose id is a non-empty string once trimmed.
    r.held_world_json = content[r.prefix + World];
    OWJsonParseResult parsed = OWJson::Parse(r.held_world_json);
    if (!parsed.ok) return Refuse(r, "not_a_world", "world.json does not parse: " + parsed.error);
    if (!parsed.value.IsObject()) return Refuse(r, "not_a_world", "world.json is not an object");
    const OWJsonValue* id = parsed.value.Find("id");
    if (!id || !id->IsString() || OWUnicode::StripWhite(id->Text()).empty())
        return Refuse(r, "not_a_world", "world.json has no id that is a non-empty string");

    for (const std::string& n : inside) r.files[n.substr(r.prefix.size())] = std::move(content[n]);
    r.world_id = id->Text();
    r.world = std::move(parsed.value);
    if (r.world.Erase("api"))  // OW-10: stripped, never returned, the file still read
        r.files[World] = OWJson::ToBytes(r.world);
    r.ok = true;
    r.message = "world " + r.world_id + ", " + std::to_string(r.files.size()) + " files";
    return r;
}

OWArchiveRead ReadFile(const std::string& path, const OWArchiveBudget& budget) {
    OWBytes raw;
    if (!OWFiles::Read(path, raw)) {
        OWArchiveRead r;
        return Refuse(r, "not_openable", "no file can be read at " + path);
    }
    return Read(raw, budget);
}

// -- pack ---------------------------------------------------------------------------------------------------------------

OWPacked PackFiles(const OWFileMap& files, const std::string& top) {
    OWPacked out;
    OWZip::Writer zw;
    std::string marker = MediaType;
    zw.Add(Marker, OWBytes(marker.begin(), marker.end()), false);
    out.names.push_back(Marker);
    std::string pre = top.empty() ? "" : top + "/";
    for (const auto& kv : files) {  // std::map: UTF-8 byte order (OW-13)
        if (!zw.Add(pre + kv.first, kv.second, true)) {
            out.message = "could not deflate " + kv.first;
            return out;
        }
        out.names.push_back(pre + kv.first);
    }
    out.bytes = zw.Finish();
    out.ok = true;
    return out;
}

namespace {

bool EndsWithMeta(const std::string& name) {
    if (name.size() < 5) return false;
    std::string tail = name.substr(name.size() - 5);
    for (char& c : tail)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return tail == ".meta";
}

// Unity's own .meta: named after a file or folder beside it, and opening with Unity's two lines
// ("fileFormatVersion: N" and "guid: " with 32 lowercase hex digits).
bool IsUnityMeta(const std::string& dir, const std::string& name) {
    if (!EndsWithMeta(name)) return false;
    std::string owner = dir + "/" + name.substr(0, name.size() - 5);
    if (!OWFiles::IsFile(owner) && !OWFiles::IsDir(owner)) return false;
    OWBytes head;
    if (!OWFiles::Read(dir + "/" + name, head)) return false;
    std::string s(head.begin(), head.begin() + std::min<size_t>(head.size(), 128));
    const std::string a = "fileFormatVersion: ";
    if (!StartsWith(s, a)) return false;
    size_t i = a.size(), digits = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i, ++digits;
    if (!digits) return false;
    if (i < s.size() && s[i] == '\r') ++i;
    if (i >= s.size() || s[i] != '\n') return false;
    ++i;
    const std::string g = "guid: ";
    if (s.compare(i, g.size(), g) != 0) return false;
    i += g.size();
    for (int k = 0; k < 32; ++k, ++i)
        if (i >= s.size() || !((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
    if (i < s.size() && s[i] == '\r') ++i;
    return i >= s.size() || s[i] == '\n';
}

// A producer's directory whole: its files are its own, dot-files and its own .meta included; only Unity's .meta goes.
void CollectProducer(const std::string& dir, const std::string& rel, OWFileMap& into) {
    for (const std::string& name : OWFiles::List(dir, false))
        if (!IsUnityMeta(dir, name)) OWFiles::Read(dir + "/" + name, into[rel + "/" + name]);
    for (const std::string& name : OWFiles::List(dir, true)) CollectProducer(dir + "/" + name, rel + "/" + name, into);
}

// The producers a RECORDS.json marks "private": true, each by OW-18's fold. "" or why it cannot be told.
std::string PrivateProducers(const OWBytes& manifest, std::set<std::string>& into) {
    OWJsonParseResult m = OWJson::Parse(manifest);
    auto why = [](const std::string& what) {
        return "records/RECORDS.json " + what +
               ", so which records are private cannot be told; fix it, or pack with include_private";
    };
    if (!m.ok) return why("does not parse");
    if (!m.value.IsArray()) return why("is not a JSON array");
    for (const OWJsonValue& entry : m.value.Items()) {
        if (!entry.IsObject()) return why("holds an entry that is not an object");
        const OWJsonValue* p = entry.Find("private");
        if (!p || !p->IsBool() || !p->AsBool()) continue;  // anything but the boolean true marks nothing
        const OWJsonValue* producer = entry.Find("producer");
        if (!producer || !producer->IsString() || producer->Text().empty() || producer->Text() == "." ||
            producer->Text() == ".." || producer->Text().find_first_of("/\\") != std::string::npos)
            return why("marks private an entry whose producer is not one directory name");
        into.insert(OWUnicode::Fold(producer->Text()));
    }
    return "";
}

}  // namespace

void CollectContent(const std::string& dir, const std::string& rel, OWFileMap& into) {
    for (const std::string& name : OWFiles::List(dir, false)) {
        if (name[0] == '.' || EndsWithMeta(name)) continue;
        OWFiles::Read(dir + "/" + name, into[rel + "/" + name]);
    }
    for (const std::string& name : OWFiles::List(dir, true))
        if (name[0] != '.') CollectContent(dir + "/" + name, rel + "/" + name, into);
}

void CollectRecords(const std::string& folder, OWFileMap& into) {
    std::string records = folder + "/records";
    if (!OWFiles::IsDir(records)) return;
    for (const std::string& name : OWFiles::List(records, false))
        if (!IsUnityMeta(records, name)) OWFiles::Read(records + "/" + name, into["records/" + name]);
    for (const std::string& name : OWFiles::List(records, true))
        CollectProducer(records + "/" + name, "records/" + name, into);
}

const char* const AuthorFields[5] = {"created_by", "changed_by", "changed_via", "changed_client", "doors"};

namespace {

// The deepest nesting the npm SDK reads a file it did not write to (OUTSIDE_JSON_MAX_DEPTH): an object or an array at
// the top is 1. The core's parser reads to 512.
constexpr int OutsideJsonMaxDepth = 200;

// Whether a number's text, which the core writes back in section 5's spelling (OWJson::CanonicalNumber), is written
// as JSON.stringify writes it after JSON.parse. A number with a fraction or an exponent is read to the nearest double
// either way, so it is, unless it is past the largest double (the core keeps its text, JavaScript has no form for it).
// An integer the core writes as written, where JavaScript writes the double it reads: past 2^53 the two can differ.
bool WrittenAsJavaScriptWrites(const std::string& text) {
    double d = 0;
    auto res = std::from_chars(text.data(), text.data() + text.size(), d);
    if (res.ec == std::errc::result_out_of_range) {
        // Too large keeps its text in the core; too small is 0 in both. An integer is never too small.
        return text.find_first_of(".eE") != std::string::npos && OWJson::CanonicalNumber(text) == "0";
    }
    if (res.ec != std::errc()) return false;
    return OWJson::CanonicalNumber(text) == OWJson::NumberToString(d);
}

// Whether a parsed value nests at most OutsideJsonMaxDepth deep and holds only numbers written as JavaScript writes
// them. A loop with its own stack, never recursion.
bool AsTheNpmReaderReadsIt(const OWJsonValue& top) {
    std::vector<std::pair<const OWJsonValue*, int>> stack{{&top, 1}};
    while (!stack.empty()) {
        auto [v, depth] = stack.back();
        stack.pop_back();
        if (v->IsNumber()) {
            if (!WrittenAsJavaScriptWrites(v->Text())) return false;
        } else if (v->IsArray() || v->IsObject()) {
            if (depth > OutsideJsonMaxDepth) return false;
            for (const OWJsonValue& child : v->Items()) stack.push_back({&child, depth + 1});
        }
    }
    return true;
}

}  // namespace

bool WithoutAuthorFields(const OWBytes& bytes, OWBytes& out) {
    // The core's parser is the npm SDK's grammar: strict UTF-8 (a byte order mark read past), RFC 8259, no raw control
    // character in a string, one value and only white space after it; a key given twice keeps its first place and its
    // last value; a lone surrogate's escape is kept and written back escaped. Then the npm reader's depth and numbers.
    OWJsonParseResult r = OWJson::Parse(bytes);
    if (!r.ok || !r.value.IsObject()) return false;
    bool present = false;
    for (const char* field : AuthorFields) present = present || r.value.Has(field);
    if (!present || !AsTheNpmReaderReadsIt(r.value)) return false;
    for (const char* field : AuthorFields) r.value.Erase(field);
    out = OWJson::ToBytes(r.value);
    return true;
}

std::vector<std::string> StripAuthorFields(OWFileMap& files) {
    std::vector<std::string> left;
    for (auto& [rel, bytes] : files) {  // std::map: UTF-8 byte order
        if (!StartsWith(rel, "elements/") && !StartsWith(rel, "spatial/")) continue;
        OWBytes bare;
        if (!WithoutAuthorFields(bytes, bare)) continue;
        bytes = std::move(bare);
        left.push_back(rel);
    }
    return left;
}

OWPacked PackWorld(const OWFileMap& files_in, const std::string& top, bool keep_api, bool include_private,
                   bool include_author_fields) {
    OWPacked out;
    auto world = files_in.find(World);
    if (world == files_in.end()) {
        out.message = "no world.json";
        return out;
    }
    OWJsonParseResult info = OWJson::Parse(world->second);
    const OWJsonValue* id = info.ok && info.value.IsObject() ? info.value.Find("id") : nullptr;
    if (!id || !id->IsString() || OWUnicode::StripWhite(id->Text()).empty()) {
        out.message = "world.json is not an object with an id (a non-empty string)";
        return out;
    }
    OWFileMap files = files_in;
    if (!keep_api && info.value.Erase("api")) files[World] = OWJson::ToBytes(info.value);  // OW-10
    std::vector<std::string> left_out;
    auto manifest = files.find("records/RECORDS.json");
    if (!include_private && manifest != files.end()) {
        // Section 4.5 rule 11: a producer marked private is left out whole; RECORDS.json travels byte-exact.
        std::set<std::string> excluded;
        std::string err = PrivateProducers(manifest->second, excluded);
        if (!err.empty()) {
            out.message = err;
            return out;
        }
        for (auto it = files.begin(); it != files.end();) {
            const std::string& rel = it->first;
            size_t slash = rel.compare(0, 8, "records/") == 0 ? rel.find('/', 8) : std::string::npos;
            std::string producer = slash == std::string::npos ? "" : rel.substr(8, slash - 8);
            if (!producer.empty() && excluded.count(OWUnicode::Fold(producer))) {
                if (std::find(left_out.begin(), left_out.end(), producer) == left_out.end()) left_out.push_back(producer);
                it = files.erase(it);
            } else {
                ++it;
            }
        }
    }
    std::vector<std::string> authored;
    if (!include_author_fields) authored = StripAuthorFields(files);
    out = PackFiles(files, top);
    out.left_out = left_out;
    out.author_fields_left_out = authored;
    return out;
}

OWPacked PackFolder(const std::string& folder_in, bool keep_api, bool include_private, bool include_author_fields) {
    OWPacked out;
    std::string folder = folder_in;
    while (!folder.empty() && (folder.back() == '/' || folder.back() == '\\')) folder.pop_back();
    if (!OWFiles::IsDir(folder)) {
        out.message = "no world folder at " + folder;
        return out;
    }
    OWFileMap files;
    if (!OWFiles::Read(folder + "/" + World, files[World])) {
        out.message = "no world.json in " + folder;
        return out;
    }
    for (const char* part : {"elements", "spatial", "media"})
        if (OWFiles::IsDir(folder + "/" + part)) CollectContent(folder + "/" + part, part, files);
    CollectRecords(folder, files);
    std::string top = OWFiles::LastSegment(OWFiles::Absolute(folder));
    if (top.empty() || top.find(':') != std::string::npos) top = "world";
    out = PackWorld(files, top, keep_api, include_private, include_author_fields);
    if (!out.ok) out.message += " (" + folder + ")";
    return out;
}

OWPacked Pack(const std::string& folder, const std::string& archive_path, bool keep_api, bool include_private,
              bool include_author_fields) {
    std::string abs_folder = OWUnicode::Fold(OWFiles::Absolute(folder));
    std::string abs_archive = OWUnicode::Fold(OWFiles::Absolute(archive_path));
    if (StartsWith(abs_archive, abs_folder + "/")) {
        OWPacked bad;
        bad.message = "the archive " + archive_path + " would be written inside the folder it packs";
        return bad;
    }
    OWPacked p = PackFolder(folder, keep_api, include_private, include_author_fields);
    if (!p.ok) return p;
    if (!OWFiles::Write(archive_path, p.bytes)) {
        p.ok = false;
        p.message = "cannot write " + archive_path;
    }
    return p;
}

}  // namespace OWWorldArchive
}  // namespace OnlyWorlds
