#include "OWMedia.h"

#include <charconv>
#include <cmath>
#include <system_error>

namespace OnlyWorlds {

namespace {

// A text keel serves: a string's, or a number's source. Absent for null, an absent member, or anything else.
std::optional<std::string> TextOf(const OWJsonValue* v) {
    if (!v || !(v->IsString() || v->IsNumber())) return std::nullopt;
    return v->Text();
}
std::string Text(const OWJsonValue& body, const char* key) { return TextOf(body.Find(key)).value_or(""); }

// A whole number keel serves, 0 for anything else (a byte count, a Unix time, a count).
int64_t Int(const OWJsonValue& body, const char* key) {
    const OWJsonValue* v = body.Find(key);
    if (!v || !v->IsNumber()) return 0;
    const std::string& text = v->Text();
    int64_t n = 0;
    auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), n);
    if (ec == std::errc() && end == text.data() + text.size()) return n;
    double d = v->AsDouble();
    if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 9.2e18) return static_cast<int64_t>(d);
    return 0;
}

bool Flag(const OWJsonValue& body, const char* key) {
    const OWJsonValue* v = body.Find(key);
    return v && v->IsBool() && v->AsBool();
}

OWIssuedTo IssuedToOf(const OWJsonValue* v) {
    OWIssuedTo out;
    if (!v || !v->IsObject()) return out;
    out.account = TextOf(v->Find("account"));
    out.membership = TextOf(v->Find("membership"));
    return out;
}

void RowOf(const OWJsonValue& row, OWFileRow& r) {
    r.id = Text(row, "id");
    r.name = Text(row, "name");
    r.kind = Text(row, "kind");
    r.size = Int(row, "size");
    r.label = TextOf(row.Find("label"));
    if (const OWJsonValue* by = row.Find("added_by"); by && by->IsObject()) {
        r.added_by.name = Text(*by, "name");
        r.added_by.membership = TextOf(by->Find("membership"));
        r.added_by.agent = Flag(*by, "agent");
        r.added_by.owner = Flag(*by, "owner");
    }
    r.added_at = Text(row, "added_at");
}

}  // namespace

const std::vector<std::string>& OWIssuedTo::Keys() {
    static const std::vector<std::string> keys = {"account", "membership"};
    return keys;
}

std::optional<OWMediaTicket> OWMediaTicket::Of(const OWJsonValue& body) {
    if (!body.IsObject()) return std::nullopt;
    OWMediaTicket t;
    t.ticket = Text(body, "ticket");
    t.upload_url = Text(body, "upload_url");
    t.prefix = Text(body, "prefix");
    t.max_bytes = Int(body, "max_bytes");
    t.exp = Int(body, "exp");
    t.uses = Int(body, "uses");
    t.issued_to = IssuedToOf(body.Find("issued_to"));
    return t;
}
const std::vector<std::string>& OWMediaTicket::Keys() {
    static const std::vector<std::string> keys = {"ticket", "upload_url", "prefix", "max_bytes", "exp", "uses",
                                                  "issued_to"};
    return keys;
}

std::optional<OWUploadedImage> OWUploadedImage::Of(const OWJsonValue& body) {
    if (!body.IsObject()) return std::nullopt;
    OWUploadedImage u;
    u.url = Text(body, "url");
    u.key = Text(body, "key");
    u.bytes = Int(body, "bytes");
    u.type = Text(body, "type");
    u.etag = Text(body, "etag");
    u.json = body;
    return u;
}
const std::vector<std::string>& OWUploadedImage::Keys() {
    static const std::vector<std::string> keys = {"url", "key", "bytes", "type", "etag"};
    return keys;
}

std::optional<OWRemovalTicket> OWRemovalTicket::Of(const OWJsonValue& body) {
    if (!body.IsObject()) return std::nullopt;
    OWRemovalTicket t;
    t.ticket = Text(body, "ticket");
    t.remove_url = Text(body, "remove_url");
    t.exp = Int(body, "exp");
    t.key = Text(body, "key");
    t.referenced = Int(body, "referenced");
    return t;
}
const std::vector<std::string>& OWRemovalTicket::Keys() {
    static const std::vector<std::string> keys = {"ticket", "remove_url", "exp", "key", "referenced"};
    return keys;
}

std::optional<OWRemovedImage> OWRemovedImage::Of(const OWJsonValue& body) {
    if (!body.IsObject()) return std::nullopt;
    OWRemovedImage r;
    r.removed = Text(body, "removed");
    r.bytes = Int(body, "bytes");
    r.cache = Text(body, "cache");
    r.json = body;
    return r;
}
const std::vector<std::string>& OWRemovedImage::Keys() {
    static const std::vector<std::string> keys = {"removed", "bytes", "cache"};
    return keys;
}

const std::vector<std::string>& OWFileAddedBy::Keys() {
    static const std::vector<std::string> keys = {"name", "membership", "agent", "owner"};
    return keys;
}

std::optional<OWFileRow> OWFileRow::Of(const OWJsonValue& row) {
    if (!row.IsObject()) return std::nullopt;
    OWFileRow r;
    RowOf(row, r);
    return r;
}
const std::vector<std::string>& OWFileRow::Keys() {
    static const std::vector<std::string> keys = {"id", "name", "kind", "size", "label", "added_by", "added_at"};
    return keys;
}

std::optional<OWWorldFileRow> OWWorldFileRow::Of(const OWJsonValue& row) {
    if (!row.IsObject()) return std::nullopt;
    OWWorldFileRow r;
    RowOf(row, r);
    r.element_type = Text(row, "element_type");
    r.element_id = Text(row, "element_id");
    return r;
}
const std::vector<std::string>& OWWorldFileRow::Keys() {
    static const std::vector<std::string> keys = {"id",       "name",     "kind",         "size",      "label",
                                                  "added_by", "added_at", "element_type", "element_id"};
    return keys;
}

std::optional<OWWorldFiles> OWWorldFiles::Of(const OWJsonValue& body) {
    if (!body.IsObject()) return std::nullopt;
    OWWorldFiles w;
    if (const OWJsonValue* data = body.Find("data"); data && data->IsArray())
        for (const OWJsonValue& row : data->Items())
            if (std::optional<OWWorldFileRow> r = OWWorldFileRow::Of(row)) w.data.push_back(std::move(*r));
    w.used_bytes = Int(body, "used_bytes");
    w.cap_bytes = Int(body, "cap_bytes");
    return w;
}
const std::vector<std::string>& OWWorldFiles::Keys() {
    static const std::vector<std::string> keys = {"data", "used_bytes", "cap_bytes"};
    return keys;
}

std::optional<OWFileTicket> OWFileTicket::Of(const OWJsonValue& body) {
    if (!body.IsObject()) return std::nullopt;
    OWFileTicket t;
    t.ticket = Text(body, "ticket");
    t.upload_url = Text(body, "upload_url");
    t.prefix = Text(body, "prefix");
    t.key = Text(body, "key");
    t.max_bytes = Int(body, "max_bytes");
    t.exp = Int(body, "exp");
    t.uses = Int(body, "uses");
    t.issued_to = IssuedToOf(body.Find("issued_to"));
    t.kind = Text(body, "kind");
    t.file_id = Text(body, "file_id");
    return t;
}
const std::vector<std::string>& OWFileTicket::Keys() {
    static const std::vector<std::string> keys = {"ticket", "upload_url", "prefix",    "key",  "max_bytes",
                                                  "exp",    "uses",       "issued_to", "kind", "file_id"};
    return keys;
}

std::optional<OWUploadedFile> OWUploadedFile::Of(const OWJsonValue& body) {
    if (!body.IsObject()) return std::nullopt;
    OWUploadedFile u;
    u.key = Text(body, "key");
    u.fid = Text(body, "fid");
    u.bytes = Int(body, "bytes");
    u.type = Text(body, "type");
    u.etag = Text(body, "etag");
    u.json = body;
    return u;
}
const std::vector<std::string>& OWUploadedFile::Keys() {
    static const std::vector<std::string> keys = {"key", "fid", "bytes", "type", "etag"};
    return keys;
}

std::optional<OWFileDownload> OWFileDownload::Of(const OWJsonValue& body) {
    if (!body.IsObject()) return std::nullopt;
    OWFileDownload d;
    d.url = Text(body, "url");
    d.exp = Int(body, "exp");
    d.name = Text(body, "name");
    d.kind = Text(body, "kind");
    d.size = Int(body, "size");
    return d;
}
const std::vector<std::string>& OWFileDownload::Keys() {
    static const std::vector<std::string> keys = {"url", "exp", "name", "kind", "size"};
    return keys;
}

// -- The readings -----------------------------------------------------------------------------------------------------

const std::vector<std::pair<std::string, std::string>>& OWMedia::FileExtensions() {
    static const std::vector<std::pair<std::string, std::string>> exts = {
        {"pdf", "pdf"}, {"txt", "txt"},   {"md", "md"},    {"markdown", "md"}, {"json", "json"},
        {"csv", "csv"}, {"yaml", "yaml"}, {"yml", "yaml"}, {"toml", "toml"},   {"zip", "zip"},
        {"png", "png"}, {"jpg", "jpg"},   {"jpeg", "jpg"}, {"webp", "webp"},   {"avif", "avif"},
    };
    return exts;
}

std::string OWMedia::FileKindOf(std::string_view name) {
    size_t cut = name.find_last_of("/\\");
    std::string_view base = cut == std::string_view::npos ? name : name.substr(cut + 1);
    size_t dot = base.rfind('.');
    if (dot == std::string_view::npos || dot == 0) return "";
    // What follows the dot, trimmed of white space and lower-cased, as the TypeScript SDK's trim().toLowerCase().
    std::string_view ext = base.substr(dot + 1);
    auto white = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; };
    while (!ext.empty() && white(ext.front())) ext.remove_prefix(1);
    while (!ext.empty() && white(ext.back())) ext.remove_suffix(1);
    std::string lower(ext);
    for (char& c : lower)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    for (const auto& [e, kind] : FileExtensions())
        if (e == lower) return kind;
    return "";
}

std::string OWMedia::ObjectKey(std::string_view key_or_url) {
    auto starts = [&](std::string_view scheme) {
        if (key_or_url.size() < scheme.size()) return false;
        for (size_t i = 0; i < scheme.size(); ++i) {
            char c = key_or_url[i];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (c != scheme[i]) return false;
        }
        return true;
    };
    size_t from = starts("http://") ? 7 : starts("https://") ? 8 : 0;
    if (from == 0) return std::string(key_or_url);
    // The path: from the first '/' after the host to the query or the fragment, without its leading slashes.
    std::string_view rest = key_or_url.substr(from);
    size_t slash = rest.find_first_of("/?#");
    if (slash == std::string_view::npos || rest[slash] != '/') return "";
    std::string_view path = rest.substr(slash);
    path = path.substr(0, path.find_first_of("?#"));
    while (!path.empty() && path.front() == '/') path.remove_prefix(1);
    // Unescaped as decodeURIComponent unescapes: each %XX its byte (a % without two hex digits stays as it is).
    auto hex = [](char c) {
        return c >= '0' && c <= '9'   ? c - '0'
               : c >= 'a' && c <= 'f' ? c - 'a' + 10
               : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                      : -1;
    };
    std::string out;
    for (size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '%' && i + 2 < path.size() && hex(path[i + 1]) >= 0 && hex(path[i + 2]) >= 0) {
            out.push_back(static_cast<char>(hex(path[i + 1]) * 16 + hex(path[i + 2])));
            i += 2;
        } else {
            out.push_back(path[i]);
        }
    }
    return out;
}

}  // namespace OnlyWorlds
