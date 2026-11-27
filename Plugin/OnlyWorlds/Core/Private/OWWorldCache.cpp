#include "OWWorldCache.h"

#include "OWClient.h"
#include "OWFiles.h"

#include <charconv>
#include <cmath>
#include <system_error>
#include <utility>

namespace OnlyWorlds {

namespace {

// A body's own id: its "id" when that is a non-empty string, else "".
std::string BodyId(const OWJsonValue& body) {
    const OWJsonValue* id = body.IsObject() ? body.Find("id") : nullptr;
    return id && id->IsString() ? id->Text() : std::string();
}

// A JSON integer as written (a number past 2^53 keeps its digits), or absent for anything else.
std::optional<int64_t> IntOf(const OWJsonValue* v) {
    if (!v || !v->IsNumber()) return std::nullopt;
    const std::string& text = v->Text();
    int64_t n = 0;
    auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), n);
    if (ec == std::errc() && end == text.data() + text.size()) return n;
    double d = v->AsDouble();
    if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 9.2e18) return static_cast<int64_t>(d);
    return std::nullopt;
}

std::string TextOf(const OWJsonValue* v) { return v && v->IsString() ? v->Text() : std::string(); }

void Say(std::string* out, std::string why) {
    if (out) *out = std::move(why);
}

}  // namespace

const char* const OWWorldCache::Format = "onlyworlds-unreal-cache";

OWWorldCache::OWWorldCache(std::string id, std::string name, bool frozen)
    : world_id(std::move(id)), world_name(std::move(name)), read_only(frozen) {}

size_t OWWorldCache::Count() const { return elements_.size(); }

const OWCachedElement* OWWorldCache::Get(const std::string& id) const {
    auto it = by_id_.find(id);
    return it == by_id_.end() ? nullptr : &elements_[it->second];
}

bool OWWorldCache::Has(const std::string& id) const { return by_id_.count(id) > 0; }

bool OWWorldCache::IsKnownOf(const std::string& id) const {
    const OWCachedElement* e = Get(id);
    return e && OWClient::IsKnownOf(e->body);
}

std::vector<const OWCachedElement*> OWWorldCache::Elements() const {
    std::vector<const OWCachedElement*> out;
    out.reserve(elements_.size());
    for (const OWCachedElement& e : elements_) out.push_back(&e);
    return out;
}

std::vector<const OWCachedElement*> OWWorldCache::OfType(const std::string& type) const {
    std::vector<const OWCachedElement*> out;
    for (const OWCachedElement& e : elements_)
        if (e.type == type) out.push_back(&e);
    return out;
}

std::vector<const OWCachedElement*> OWWorldCache::Resolve(const std::vector<std::string>& ids) const {
    std::vector<const OWCachedElement*> out;
    for (const std::string& id : ids)
        if (const OWCachedElement* e = Get(id)) out.push_back(e);
    return out;
}

std::vector<const OWCachedElement*> OWWorldCache::Resolve(const OWJsonValue& link) const {
    std::vector<std::string> ids;
    if (link.IsString()) {
        ids.push_back(link.Text());
    } else if (link.IsArray()) {
        for (const OWJsonValue& v : link.Items())
            if (v.IsString()) ids.push_back(v.Text());
    }
    return Resolve(ids);
}

bool OWWorldCache::Upsert(const std::string& type, OWJsonValue body) {
    std::string id = BodyId(body);
    if (id.empty() || type.empty()) return false;
    auto it = by_id_.find(id);
    if (it != by_id_.end()) {
        OWCachedElement& held = elements_[it->second];
        held.type = type;
        held.body = std::move(body);
        return true;
    }
    by_id_.emplace(id, elements_.size());
    elements_.push_back(OWCachedElement{std::move(id), type, std::move(body)});
    return true;
}

bool OWWorldCache::Remove(const std::string& id) {
    auto it = by_id_.find(id);
    if (it == by_id_.end()) return false;
    size_t at = it->second;
    elements_.erase(elements_.begin() + static_cast<std::ptrdiff_t>(at));
    by_id_.erase(it);
    // Every element after it moved down one place.
    for (size_t i = at; i < elements_.size(); ++i) by_id_[elements_[i].id] = i;
    return true;
}

void OWWorldCache::Clear() {
    elements_.clear();
    by_id_.clear();
    cursor_ = 0;
    cursor_token_.clear();
    epoch_.reset();
}

int64_t OWWorldCache::Cursor() const { return cursor_; }
const std::string& OWWorldCache::CursorToken() const { return cursor_token_; }
const std::optional<int64_t>& OWWorldCache::Epoch() const { return epoch_; }
const std::string& OWWorldCache::SyncedAt() const { return synced_at_; }

void OWWorldCache::SetCursor(int64_t seq, std::string token, std::optional<int64_t> epoch, std::string at) {
    cursor_ = seq;
    cursor_token_ = std::move(token);
    epoch_ = epoch;
    synced_at_ = std::move(at);
}

bool OWWorldCache::Syncing() const { return syncing_; }

OWJsonValue OWWorldCache::ToJson() const {
    OWJsonValue doc = OWJsonValue::Object();
    doc.Set("format", OWJsonValue::String(Format));
    doc.Set("version", OWJsonValue::Number(Version));
    doc.Set("world_id", OWJsonValue::String(world_id));
    doc.Set("world_name", OWJsonValue::String(world_name));
    doc.Set("read_only", OWJsonValue::Bool(read_only));
    doc.Set("cursor", OWJsonValue::Number(cursor_));
    doc.Set("cursor_token", OWJsonValue::String(cursor_token_));
    doc.Set("epoch", epoch_ ? OWJsonValue::Number(*epoch_) : OWJsonValue::Null());
    doc.Set("synced_at", OWJsonValue::String(synced_at_));
    OWJsonValue rows = OWJsonValue::Array();
    for (const OWCachedElement& e : elements_) {
        OWJsonValue row = OWJsonValue::Object();
        row.Set("type", OWJsonValue::String(e.type));
        row.Set("element", e.body);
        rows.Push(std::move(row));
    }
    doc.Set("elements", std::move(rows));
    return doc;
}

std::unique_ptr<OWWorldCache> OWWorldCache::FromJson(const OWJsonValue& doc, std::string* why) {
    if (!doc.IsObject()) {
        Say(why, "not a JSON object");
        return nullptr;
    }
    const OWJsonValue* format = doc.Find("format");
    if (!format || !format->IsString() || format->Text() != Format) {
        Say(why, std::string("its format is not ") + Format);
        return nullptr;
    }
    std::optional<int64_t> version = IntOf(doc.Find("version"));
    if (!version || *version > Version) {
        Say(why, "its version is not a number at or below " + std::to_string(Version));
        return nullptr;
    }
    const OWJsonValue* frozen = doc.Find("read_only");
    auto out = std::make_unique<OWWorldCache>(TextOf(doc.Find("world_id")), TextOf(doc.Find("world_name")),
                                              frozen && frozen->IsBool() && frozen->AsBool());
    out->SetCursor(IntOf(doc.Find("cursor")).value_or(0), TextOf(doc.Find("cursor_token")), IntOf(doc.Find("epoch")),
                   TextOf(doc.Find("synced_at")));
    if (const OWJsonValue* rows = doc.Find("elements"); rows && rows->IsArray()) {
        for (const OWJsonValue& row : rows->Items()) {
            if (!row.IsObject()) continue;
            const OWJsonValue* type = row.Find("type");
            const OWJsonValue* element = row.Find("element");
            if (type && type->IsString() && element && element->IsObject()) out->Upsert(type->Text(), *element);
        }
    }
    return out;
}

bool OWWorldCache::WriteFile(const std::string& path, std::string* error) const {
    if (OWFiles::Write(path, OWJson::ToBytes(ToJson(), /*keep_number_text=*/true))) return true;
    Say(error, "could not write " + path);
    return false;
}

std::unique_ptr<OWWorldCache> OWWorldCache::ReadFile(const std::string& path, std::string* error) {
    if (!OWFiles::IsFile(path)) return std::make_unique<OWWorldCache>();
    OWBytes bytes;
    if (!OWFiles::Read(path, bytes)) {
        Say(error, path + ": could not be read");
        return nullptr;
    }
    OWJsonParseResult parsed = OWJson::Parse(bytes);
    if (!parsed.ok) {
        Say(error, path + ": not JSON (" + parsed.error + ")");
        return nullptr;
    }
    std::string why;
    std::unique_ptr<OWWorldCache> out = FromJson(parsed.value, &why);
    if (!out) Say(error, path + ": " + why);
    return out;
}

std::unique_ptr<OWWorld> OWWorldCache::ToWorld() const {
    auto w = std::make_unique<OWWorld>();
    OWJsonValue info = OWJsonValue::Object();
    info.Set("id", OWJsonValue::String(world_id));
    info.Set("name", OWJsonValue::String(world_name));
    w->info = std::move(info);
    w->elements.reserve(elements_.size());
    for (const OWCachedElement& e : elements_) w->Add(e.type, e.body);
    return w;
}

}  // namespace OnlyWorlds
