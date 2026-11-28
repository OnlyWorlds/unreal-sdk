#pragma once
// Images and files: what OWClient's ten image and file calls take and answer (OWClient.h, "Images" and "Files on
// elements"), and the two readings a file picker wants (FileKindOf, FileExtensions). Mirrors the TypeScript SDK's
// OwMediaTicket ... OwFileDownload and its fileKindOf and FILE_EXTENSIONS, and the Unity SDK's OWMedia and OWFiles.
//
// An upload is two steps: a ticket from the API (keel), then the bytes straight to the upload host the ticket names,
// with the ticket as a Bearer and nothing else of the client's: the key and the PIN never leave for the host. A ticket
// is short-lived (exp, Unix seconds) and single-use, and it is a credential: keep it out of logs. So is a file's
// download link (OWFileDownload::url): anyone holding it can fetch the file until exp. Neither is ever in an error's
// words.
//
//   OWUploadImageOptions put;
//   client.UploadImage(png_bytes, put, [](OWReply<OWUploadedImage> r) {
//       if (r.Ok()) SetImageUrl(r.value.url);                // patch it into an element's image_url yourself
//   });
//   OWUploadFileOptions file;
//   file.name = "Ledger.pdf";                                // the kind is read from the extension
//   client.UploadFile("location", id, pdf_bytes, file, [](OWReply<OWUploadedFile> r) { /* r.value.fid */ });
//
// A text keel serves as null, or leaves out, is absent (an optional) or "" (a plain string); a number it leaves out
// is 0.

#include "OWApi.h"
#include "OWFiles.h"
#include "OWJson.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace OnlyWorlds {

// Who answers for an upload: the account and the membership a ticket was issued to, each absent when keel sends null.
struct OW_API OWIssuedTo {
    std::optional<std::string> account;
    std::optional<std::string> membership;

    static const std::vector<std::string>& Keys();
};

// POST /media/ticket's 201: permission to upload one image into this world (the TypeScript SDK's OwMediaTicket).
struct OW_API OWMediaTicket {
    std::string ticket;      // opaque: sent to upload_url as Authorization: Bearer <ticket>, as is
    std::string upload_url;  // where the image goes (https://upload.onlyworlds.com/v1/upload on production)
    std::string prefix;      // u/<world_id>/: every upload with this ticket lands under it
    int64_t max_bytes = 0;   // the most this ticket accepts
    int64_t exp = 0;         // Unix seconds after which the host refuses the ticket
    int64_t uses = 0;        // always 1
    OWIssuedTo issued_to;

    // The ticket read from keel's answer; absent for anything but an object.
    static std::optional<OWMediaTicket> Of(const OWJsonValue& body);
    static const std::vector<std::string>& Keys();
};

// The upload host's 201 for one image (OwUploadedImage). Put `url` in an element's image_url.
struct OW_API OWUploadedImage {
    std::string url;   // the permanent public URL (https://media.onlyworlds.com/<key>)
    std::string key;   // the object key, under the ticket's prefix
    int64_t bytes = 0;
    std::string type;  // the MIME type the host read from the bytes
    std::string etag;
    OWJsonValue json;  // the answer as served: the host may say more than these keys

    static std::optional<OWUploadedImage> Of(const OWJsonValue& body);
    static const std::vector<std::string>& Keys();
};

// POST /media/remove-ticket's 200: permission to remove one image (OwRemovalTicket).
struct OW_API OWRemovalTicket {
    std::string ticket;      // opaque: sent to remove_url as Authorization: Bearer <ticket>, as is
    std::string remove_url;  // the host's /v1/remove
    int64_t exp = 0;         // Unix seconds, 10 minutes after issue
    std::string key;         // the object key the ticket removes
    // How many of the world's elements show the image. Removal does not change them: clear their image_url yourself.
    int64_t referenced = 0;

    static std::optional<OWRemovalTicket> Of(const OWJsonValue& body);
    static const std::vector<std::string>& Keys();
};

// The upload host's 200 for one removal (OwRemovedImage).
struct OW_API OWRemovedImage {
    std::string removed;  // the object key removed
    int64_t bytes = 0;    // given back to the uploading account's storage
    // may_linger: copies the network or a browser already holds can keep answering; purging: the cache is cleared too.
    std::string cache;
    OWJsonValue json;     // the answer as served

    static std::optional<OWRemovedImage> Of(const OWJsonValue& body);
    static const std::vector<std::string>& Keys();
};

// Who added a file (OwFileAddedBy).
struct OW_API OWFileAddedBy {
    std::string name;                       // a member's or a seat's name; the owner's username; "a former member"
    std::optional<std::string> membership;  // absent for the owner's files and for a membership no longer there
    bool agent = false;                     // an agent added it: an AI seat, the MCP surface, a declared agent
    // The world's current owner added it, through any door, as keel serves it: never read from a null membership.
    bool owner = false;

    static const std::vector<std::string>& Keys();
};

// One file on an element, as ListFiles answers it (OwFileRow).
struct OW_API OWFileRow {
    std::string id;
    std::string name;  // as uploaded
    std::string kind;  // pdf, txt, md, json, csv, yaml, toml, zip, png, jpg, webp or avif
    int64_t size = 0;  // bytes, as the host stored them
    std::optional<std::string> label;  // a field of the element's type the file belongs to, free text, or absent
    OWFileAddedBy added_by;
    std::string added_at;  // ISO 8601

    static std::optional<OWFileRow> Of(const OWJsonValue& row);
    static const std::vector<std::string>& Keys();
};

// One file in the world's list: a file row with its element's type and id (OwWorldFileRow).
struct OW_API OWWorldFileRow : OWFileRow {
    std::string element_type;
    std::string element_id;

    static std::optional<OWWorldFileRow> Of(const OWJsonValue& row);
    static const std::vector<std::string>& Keys();
};

// GET /world/files: every file this key may see, newest first, and the world's total against its cap (OwWorldFiles).
struct OW_API OWWorldFiles {
    std::vector<OWWorldFileRow> data;
    int64_t used_bytes = 0;  // all the world's live files, also those on elements this key cannot see
    int64_t cap_bytes = 0;   // the world's room for files (250 MB on onlyworlds.com)

    static std::optional<OWWorldFiles> Of(const OWJsonValue& body);
    static const std::vector<std::string>& Keys();
};

// POST /{type}/{id}/files's 201: permission to upload one file onto one element (OwFileTicket).
struct OW_API OWFileTicket {
    std::string ticket;      // opaque: sent to upload_url as Authorization: Bearer <ticket>, as is
    std::string upload_url;  // where the bytes go (https://upload.onlyworlds.com/v1/file on production)
    std::string prefix;      // w/<world_id>/<element_id>/
    std::string key;         // w/<world_id>/<element_id>/<file_id>.<kind>
    int64_t max_bytes = 0;   // 25 MB, or what the world has left, whichever is smaller
    int64_t exp = 0;         // Unix seconds, 10 minutes after issue
    int64_t uses = 0;        // always 1
    OWIssuedTo issued_to;
    std::string kind;        // always "file"
    std::string file_id;     // the id the file's row will have

    static std::optional<OWFileTicket> Of(const OWJsonValue& body);
    static const std::vector<std::string>& Keys();
};

// The upload host's 201 for one file (OwUploadedFile).
struct OW_API OWUploadedFile {
    std::string key;   // the ticket's key
    std::string fid;   // the file's id, the ticket's file_id
    int64_t bytes = 0;
    std::string type;  // the MIME type the file is served with
    std::string etag;
    OWJsonValue json;  // the answer as served

    static std::optional<OWUploadedFile> Of(const OWJsonValue& body);
    static const std::vector<std::string>& Keys();
};

// GET /{type}/{id}/files/{file_id}/download: a link to the file, and when it stops working (OwFileDownload).
struct OW_API OWFileDownload {
    // The file on the download host, as an attachment. The link is the credential: anyone holding it can fetch the file
    // until exp, more than once. Ask for it when the file is opened, use it at once, never log, store or share it.
    std::string url;
    int64_t exp = 0;  // Unix seconds, 5 minutes after issue
    std::string name;
    std::string kind;
    int64_t size = 0;

    static std::optional<OWFileDownload> Of(const OWJsonValue& body);
    static const std::vector<std::string>& Keys();
};

// UploadImage's options (the TypeScript SDK's uploadImage options).
struct OWUploadImageOptions {
    // The object key, sent as X-Key; it must start with the ticket's prefix. "": the host picks one.
    std::string key;
    // A ticket from CreateMediaTicket to use instead of asking keel for one.
    std::optional<OWMediaTicket> ticket;
};

// CreateFileTicket's request (the TypeScript SDK's createFileTicket request).
struct OWFileTicketRequest {
    std::string name;                 // the file's name with its extension, sent as given (keel cleans it)
    std::optional<std::string> kind;  // sent as given; absent: read from the name's extension (OWMedia::FileKindOf)
    int64_t size = 0;                 // the file's size in bytes
    std::optional<std::string> label; // a field of the element's type, or your own words (at most 80); absent: none
};

// An upload's progress: (the bytes sent, the bytes in all).
using OWUploadProgress = std::function<void(int64_t, int64_t)>;

// UploadFile's options (the TypeScript SDK's uploadFile options).
struct OWUploadFileOptions {
    std::string name;                  // the file's name with its extension; needed unless `ticket` is given
    std::optional<std::string> kind;   // as OWFileTicketRequest's
    std::optional<std::string> label;  // as OWFileTicketRequest's
    // A ticket from CreateFileTicket to use instead of asking keel for one: then name, kind and label are not read.
    std::optional<OWFileTicket> ticket;
    // Called once with (the bytes' length, the bytes' length) when the host has stored the file; never on a failure.
    // The transport reports no progress of its own (the TypeScript SDK's onProgress where there is no XMLHttpRequest).
    OWUploadProgress on_progress;
};

namespace OWMedia {

// The kind a file name's extension gives, read as keel reads it: the last path part ('/' or '\'), then what follows
// its last dot, which must have something before it, trimmed and lower-cased (.yml is yaml, .markdown md, .jpeg jpg).
// "" when there is none or keel takes no file with it (svg, gif, docx). Mirrors the TypeScript SDK's fileKindOf.
OW_API std::string FileKindOf(std::string_view name);
// Every extension keel takes a file with and its kind, in the TypeScript SDK's FILE_EXTENSIONS order, for a file
// picker: pdf txt md markdown json csv yaml yml toml zip png jpg jpeg webp avif.
OW_API const std::vector<std::pair<std::string, std::string>>& FileExtensions();
// An image's object key: `key_or_url` as given, or for a whole http(s) URL its path without its leading slashes,
// percent-decoded (the query and the fragment left out). Mirrors the TypeScript SDK's objectKey.
OW_API std::string ObjectKey(std::string_view key_or_url);

}  // namespace OWMedia

}  // namespace OnlyWorlds
