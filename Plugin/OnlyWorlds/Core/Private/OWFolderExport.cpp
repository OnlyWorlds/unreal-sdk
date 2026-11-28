#include "OWFolderExport.h"

#include "OWClient.h"
#include "OWFiles.h"
#include "OWWorld.h"
#include "OWWorldArchive.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <random>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace OnlyWorlds {

size_t OWFolderExportReport::Total() const {
    size_t n = 0;
    for (const auto& kv : counts) n += kv.second;
    return n;
}

namespace OWFolderExport {

const char* const FormatVersion = "0.3.25";
const char* const ProducerTool = "onlyworlds-unreal";
const char* const ProducerVersion = "0.16.1";
const char* const KnownOfKeeps[10] = {"id",       "type",      "name",       "description", "supertype",
                                      "subtype",  "image_url", "created_at", "updated_at",  "change_seq"};
const char* const MemberRoles[2] = {"co_builder", "contributor"};
const char* const Types[22] = {"ability",  "collective", "character", "construct", "creature", "event",
                               "family",   "institution", "language", "law",       "location", "map",
                               "marker",   "narrative",  "object",    "phenomenon", "pin",     "relation",
                               "species",  "title",      "trait",     "zone"};

namespace {

bool IsKept(const std::string& key) {
    for (const char* k : KnownOfKeeps)
        if (key == k) return true;
    return false;
}

// A value is anything but "", null and []: 0 and false are values.
bool IsSet(const OWJsonValue& v) {
    if (v.IsNull()) return false;
    if (v.IsString()) return !v.Text().empty();
    if (v.IsArray()) return v.Size() > 0;
    return true;
}

const OWJsonValue* StringMember(const OWJsonValue& body, std::string_view key) {
    const OWJsonValue* v = body.IsObject() ? body.Find(key) : nullptr;
    return v && v->IsString() ? v : nullptr;
}

// The scheme and host of the API's base URL (https://www.onlyworlds.com), with its port when it names one.
std::string OriginOf(const std::string& base_url) {
    size_t scheme = base_url.find("://");
    if (scheme == std::string::npos) return base_url;
    size_t slash = base_url.find('/', scheme + 3);
    return slash == std::string::npos ? base_url : base_url.substr(0, slash);
}

int64_t FloorDiv(int64_t a, int64_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }

// Unix milliseconds as JavaScript's toISOString writes them: UTC, milliseconds, Z (Hinnant's civil_from_days).
std::string Iso(int64_t ms) {
    int64_t secs = FloorDiv(ms, 1000);
    int ms_part = static_cast<int>(ms - secs * 1000);
    int64_t days = FloorDiv(secs, 86400);
    int64_t sod = secs - days * 86400;
    int64_t z = days + 719468;
    int64_t era = FloorDiv(z, 146097);
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int64_t d = doy - (153 * mp + 2) / 5 + 1;
    int64_t m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) ++y;
    char buf[40];
    std::snprintf(buf, sizeof buf, "%04lld-%02lld-%02lldT%02lld:%02lld:%02lld.%03dZ", static_cast<long long>(y),
                  static_cast<long long>(m), static_cast<long long>(d), static_cast<long long>(sod / 3600),
                  static_cast<long long>(sod / 60 % 60), static_cast<long long>(sod % 60), ms_part);
    return buf;
}

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string TempName() {
    std::random_device rd;
    char buf[16];
    std::snprintf(buf, sizeof buf, "%08x", static_cast<unsigned>(rd()));
    return std::string(".ow-") + buf;
}

// Why `root` cannot be written, or "": it exists and is not an empty directory.
std::string Occupied(const std::string& root) {
    bool full = OWFiles::IsFile(root) ||
                (OWFiles::IsDir(root) && (!OWFiles::List(root, false).empty() || !OWFiles::List(root, true).empty()));
    return full ? "'" + root + "' exists and is not an empty directory; a world folder is written whole, once." : "";
}

struct Run : std::enable_shared_from_this<Run> {
    Run(OWClient& c, std::string r, OWFolderExportOptions o, std::function<void(OWFolderExportReport)> d)
        : client(c), root(std::move(r)), options(std::move(o)), done(std::move(d)) {}

    OWClient& client;
    std::string root;
    OWFolderExportOptions options;
    std::function<void(OWFolderExportReport)> done;
    OWFolderExportReport report;
    OWJsonValue world;
    std::string at;
    std::vector<std::pair<std::string, std::vector<OWJsonValue>>> by_type;
    size_t next = 0;

    void Finish() {
        auto d = std::move(done);
        done = nullptr;
        if (d) d(std::move(report));
    }
    void Refuse(std::string message) {
        report.ok = false;
        report.message = std::move(message);
        report.counts.clear();
        Finish();
    }
    void Failed(const OWApiError& e) {
        report.error = e;
        Refuse(e.ToString());
    }

    void Start() {
        at = Iso(options.clock ? options.clock() : NowMs());
        auto self = shared_from_this();
        client.GetWorld([self](OWReply<OWJsonValue> r) {
            if (!r.Ok()) return self->Failed(*r.error);
            if (!r.value.IsObject()) return self->Refuse("GET /world/ answered no world object.");
            self->world = std::move(r.value);
            self->AskMe();
        });
    }

    // exported_as for the client's key: GET /me/, and for a guest its Character's name. A refusal or a body that is not
    // what was asked gives none; so does a client that names its world (an account token or no key), which has no /me.
    void AskMe() {
        if (client.World()) return Provenance(std::nullopt);
        auto self = shared_from_this();
        client.Me([self](OWReply<OWJsonValue> me) {
            if (!me.Ok() || !me.value.IsObject()) return self->Provenance(std::nullopt);
            const OWJsonValue* role = StringMember(me.value, "role");
            const OWJsonValue* cid = StringMember(me.value, "character");
            if (role && role->Text() == "guest" && cid && !cid->Text().empty()) {
                std::string id = cid->Text();
                OWJsonValue body = std::move(me.value);
                self->client.Get("character", id, {}, [self, body](OWReply<OWJsonValue> c) {
                    std::optional<std::string> name;
                    if (c.Ok())
                        if (const OWJsonValue* n = StringMember(c.value, "name")) name = n->Text();
                    self->Provenance(ExportedAs(&body, [&](const std::string&) { return name; }));
                });
                return;
            }
            self->Provenance(ExportedAs(&me.value, [](const std::string&) { return std::optional<std::string>(); }));
        });
    }

    // The provenance lines after the wire's keys. An export is a folder's birth, so source and producer are this
    // export's own: a body that already carries either names the maker of another folder, and its line is replaced
    // where it stands. origin, the world's first import, is the server's and rides along as served.
    void Provenance(std::optional<OWJsonValue> slice) {
        OWJsonValue source = OWJsonValue::Object();
        source.Set("server", OWJsonValue::String(OriginOf(client.BaseUrl())));
        const OWJsonValue* id = world.Find("id");
        source.Set("world", id ? *id : OWJsonValue::Null());
        source.Set("at", OWJsonValue::String(at));
        world.Set("source", std::move(source));
        if (slice) world.Set("exported_as", std::move(*slice));
        world.Set("producer", Producer());
        ListNext();
    }

    void ListNext() {
        if (next == 22) return Write();
        auto self = shared_from_this();
        const std::string type = Types[next];
        client.ListAll(type, {}, [self, type](OWReply<std::vector<OWJsonValue>> r) {
            if (!r.Ok()) return self->Failed(*r.error);
            if (!r.value.empty()) self->by_type.emplace_back(type, std::move(r.value));
            ++self->next;
            if (self->options.on_progress) self->options.on_progress(type, static_cast<int>(self->next), 22);
            self->ListNext();
        });
    }

    void Write() {
        // -- Settle what will be written, before any disk ---------------------------------------------------------
        std::unordered_map<std::string, std::string> owner;
        std::vector<std::pair<std::string, std::vector<OWJsonValue>>> elements;
        for (auto& [type, rows] : by_type) {
            std::vector<OWJsonValue> slot;
            std::unordered_map<std::string, size_t> at_index;
            for (OWJsonValue& body : rows) {
                const OWJsonValue* idv = StringMember(body, "id");
                if (!idv || idv->Text().empty())
                    return Refuse("A " + type + " element came back without a non-empty string id; nothing was written.");
                const std::string id = idv->Text();
                auto o = owner.find(id);
                if (o != owner.end() && o->second != type)
                    return Refuse("Element id " + id + " came back under both " + o->second + " and " + type +
                                  "; nothing was written.");
                owner[id] = type;
                // A repeat keeps its last body at its last place (the package: slot.pop(eid), then slot[eid] = body).
                auto seen = at_index.find(id);
                if (seen != at_index.end()) {
                    slot.erase(slot.begin() + static_cast<std::ptrdiff_t>(seen->second));
                    at_index.clear();
                    for (size_t i = 0; i < slot.size(); ++i) at_index[StringMember(slot[i], "id")->Text()] = i;
                }
                at_index[id] = slot.size();
                slot.push_back(std::move(body));
            }
            report.counts[type] = slot.size();
            elements.emplace_back(type, std::move(slot));
        }
        std::optional<OWJsonValue> disk = WorldForDisk(world, &report.counts);
        if (!disk) return Refuse(WorldRefusal(world) + "; nothing was written.");

        OWFileMap files;
        files["world.json"] = OWJson::ToBytes(*disk);
        std::unique_ptr<OWWorld> w = OWWorld::FromFiles(files);
        if (!w) return Refuse("world.json could not be read back; nothing was written.");
        for (auto& [type, slot] : elements)
            for (OWJsonValue& body : slot) w->Add(type, OWClient::IsKnownOf(body) ? KnownOfForDisk(body) : std::move(body));
        if (std::string why = w->WriteRefusal(); !why.empty()) return Refuse(why + " Nothing was written.");

        // -- Build beside, then move into place -------------------------------------------------------------------
        const std::string temp = root.substr(0, root.rfind('/')) + "/" + TempName();
        if (!OWFiles::MakeDirs(temp)) return Refuse("could not make " + temp + "; nothing was written.");
        std::string error;
        if (!w->WriteFolder(temp, false, &error)) {
            OWFiles::RemoveTree(temp);
            return Refuse("could not write the world into " + temp + " (" + error + "); nothing was left.");
        }
        // Checked again: the folder may have been filled while the world was read.
        if (std::string occupied = Occupied(root); !occupied.empty()) {
            OWFiles::RemoveTree(temp);
            return Refuse(occupied + " Nothing was written.");
        }
        if (OWFiles::IsDir(root)) OWFiles::RemoveTree(root);  // empty, checked just above
        if (!OWFiles::Move(temp, root)) {
            OWFiles::RemoveTree(temp);
            return Refuse("could not move the built world to " + root + "; nothing was left.");
        }
        report.ok = true;
        report.world_id = StringMember(*disk, "id")->Text();
        report.world_name = StringMember(*disk, "name")->Text();
        Finish();
    }
};

}  // namespace

OWJsonValue Producer() {
    OWJsonValue p = OWJsonValue::Object();
    p.Set("tool", OWJsonValue::String(ProducerTool));
    p.Set("version", OWJsonValue::String(ProducerVersion));
    p.Set("input", OWJsonValue::String("server"));
    return p;
}

OWJsonValue KnownOfForDisk(const OWJsonValue& body) {
    OWJsonValue disk = OWJsonValue::Object();
    for (size_t i = 0; i < body.Size(); ++i) {
        const std::string& key = body.Keys()[i];
        if (key == "known_of") continue;
        const OWJsonValue& v = body.Items()[i];
        if (IsKept(key) || IsSet(v)) disk.Set(key, v);
    }
    disk.Set("known_of", OWJsonValue::Bool(true));
    return disk;
}

std::string WorldRefusal(const OWJsonValue& world) {
    const OWJsonValue* id = StringMember(world, "id");
    if (!id || id->Text().empty()) return "world.json requires a non-empty string id (folder spec section 4.1)";
    if (!StringMember(world, "name")) return "world.json requires a string name, which may be empty (folder spec section 4.1)";
    return "";
}

std::optional<OWJsonValue> WorldForDisk(const OWJsonValue& world, const std::map<std::string, size_t>* counts) {
    if (!WorldRefusal(world).empty()) return std::nullopt;
    OWJsonValue disk = OWJsonValue::Object();
    for (size_t i = 0; i < world.Size(); ++i) {
        const std::string& key = world.Keys()[i];
        // Renamed only when the disk spelling is not also present: with both, each keeps its own.
        const bool rename = key == "time_range_current" && !world.Has("time_current");
        disk.Set(rename ? "time_current" : key, world.Items()[i]);
    }
    if (disk.Has("snapshot_counts")) {
        if (disk.Has("snapshot_of") && counts) {
            OWJsonValue recount = OWJsonValue::Object();
            for (const char* type : Types) {
                auto n = counts->find(type);
                if (n != counts->end() && n->second != 0) recount.Set(type, OWJsonValue::Number(static_cast<int64_t>(n->second)));
            }
            disk.Set("snapshot_counts", std::move(recount));
        } else {
            disk.Erase("snapshot_counts");
        }
    }
    if (!disk.Has("format_version")) disk.Set("format_version", OWJsonValue::String(FormatVersion));
    return disk;
}

std::optional<OWJsonValue> ExportedAs(
    const OWJsonValue* me, const std::function<std::optional<std::string>(const std::string&)>& character_name) {
    if (!me || !me->IsObject()) return std::nullopt;
    const OWJsonValue* kind = StringMember(*me, "kind");
    const OWJsonValue* role = StringMember(*me, "role");
    auto line = [](const char* k, const std::string& r, OWJsonValue character) {
        OWJsonValue out = OWJsonValue::Object();
        out.Set("kind", OWJsonValue::String(k));
        out.Set("role", OWJsonValue::String(r));
        out.Set("character", std::move(character));
        return out;
    };
    if (kind && kind->Text() == "owner") return line("owner", "owner", OWJsonValue::Null());
    if (role)
        for (const char* r : MemberRoles)
            if (role->Text() == r) return line("member", r, OWJsonValue::Null());
    if (!role || role->Text() != "guest") return std::nullopt;
    const OWJsonValue* id = StringMember(*me, "character");
    if (!id || id->Text().empty()) return std::nullopt;
    std::optional<std::string> name = character_name ? character_name(id->Text()) : std::nullopt;
    if (!name) return std::nullopt;
    OWJsonValue character = OWJsonValue::Object();
    character.Set("id", OWJsonValue::String(id->Text()));
    character.Set("name", OWJsonValue::String(*name));
    return line("character", "guest", std::move(character));
}

void Export(OWClient& client, const std::string& folder, OWFolderExportOptions options,
            std::function<void(OWFolderExportReport)> done) {
    OWFolderExportReport report;
    auto refuse = [&](std::string message) {
        report.message = std::move(message);
        if (done) done(std::move(report));
    };
    if (folder.empty()) return refuse("'' names no folder. Nothing was read.");
    const std::string root = OWFiles::Absolute(folder);
    report.folder = root;
    const size_t slash = root.rfind('/');
    if (slash == std::string::npos || slash == 0 || slash + 1 == root.size())
        return refuse("'" + folder + "' has no parent directory to build the folder beside. Nothing was read.");
    if (std::string occupied = Occupied(root); !occupied.empty()) return refuse(occupied + " Nothing was read.");
    auto run = std::make_shared<Run>(client, root, std::move(options), std::move(done));
    run->report.folder = root;
    run->Start();
}

}  // namespace OWFolderExport
}  // namespace OnlyWorlds
