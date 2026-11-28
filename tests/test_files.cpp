// OWClient's files on elements through a fake transport (tests/fake_transport.h): no test touches the network.
// ListFiles, WorldFiles, CreateFileTicket, UploadFile, FileDownloadLink and RemoveFile: each route's method, path and
// body; no Idempotency-Key on the ticket; the kind read from the name as keel reads it; the two-step upload (the ticket
// from keel, the raw bytes to upload_url with the ticket alone); keel's and the upload host's refusals as typed errors;
// X-OW-Agent on the ticket alone; on_progress once after success; an account token refused with nothing sent. The
// TypeScript SDK's test/files.test.mjs, ported test by test (its four XMLHttpRequest tests have no twin here: the
// transport reports no progress), with its account-token rows; added_by.owner as served and a ticket or a download link
// never in an error's words are the Unity SDK's OWFileRouteTests.

#include "ow_test.h"
#include "fake_transport.h"

#include "OWClient.h"
#include "OWJson.h"
#include "OWMedia.h"

#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <utility>
#include <vector>

using namespace OnlyWorlds;
using owtest::FakeTransport;

namespace {

const std::string Base = "https://example.test/api/v2";
const std::string El = "018f0000-0000-7000-8000-0000000000e1";
const std::string Fid = "018f0000-0000-7000-8000-0000000000f1";

const std::string Row = R"({"id":")" + Fid + R"(","name":"Map notes.pdf","kind":"pdf","size":5,"label":"description",)"
                        R"("added_by":{"name":"Ada","membership":null,"agent":false,"owner":true},)"
                        R"("added_at":"2026-10-19T18:00:00Z"})";
const std::string World = R"({"data":[{"id":")" + Fid + R"(","name":"Map notes.pdf","kind":"pdf","size":5,)"
                          R"("label":"description","added_by":{"name":"Ada","membership":null,"agent":false,)"
                          R"("owner":true},"added_at":"2026-10-19T18:00:00Z","element_type":"location",)"
                          R"("element_id":")" + El + R"("}],"used_bytes":42991616,"cap_bytes":262144000})";
const std::string Ticket = R"({"ticket":"f1.sig","upload_url":"https://upload.example/v1/file","prefix":"w/w1/)" + El +
                           R"(/","key":"w/w1/)" + El + "/" + Fid + R"(.pdf","max_bytes":8,"exp":1900000000,"uses":1,)"
                           R"("issued_to":{"account":"a1","membership":null},"kind":"file","file_id":")" + Fid + "\"}";
const std::string Stored = R"({"key":"w/w1/)" + El + "/" + Fid + R"(.pdf","fid":")" + Fid +
                           R"(","bytes":5,"type":"application/pdf","etag":"e"})";
const std::string Link = R"({"url":"https://files.example/v1/get?t=g1.sig","exp":1900000300,"name":"Map notes.pdf",)"
                         R"("kind":"pdf","size":5})";

std::shared_ptr<FakeTransport> Fake() { return std::make_shared<FakeTransport>(); }

OWClient Make(const std::shared_ptr<FakeTransport>& fake, bool agent = false, const std::string& key = "ow_w_test",
              std::optional<std::string> world = std::nullopt) {
    OWClientConfig config;
    config.api_key = key;
    config.api_pin = key == "ow_w_test" ? "1234" : "";
    config.base_url = Base;
    config.transport = fake;
    config.agent = agent;
    config.world = std::move(world);
    return OWClient(std::move(config));
}

template <typename T>
struct Got {
    OWReply<T> reply;
    int calls = 0;
    OWHandler<T> H() {
        return [this](OWReply<T> r) {
            reply = std::move(r);
            ++calls;
        };
    }
    bool Kind(OWApiError::Kind k) const { return reply.error && reply.error->kind == k; }
    std::string Why() const { return reply.error ? reply.error->ToString() : "no error"; }
};

std::string Headers(const OWRequest& r) {
    std::string out;
    for (auto& [k, v] : r.headers) out += (out.empty() ? "" : "|") + k + "=" + v;
    return out;
}
std::string HeaderOr(const OWRequest& r, const char* name) {
    const std::string* v = r.Header(name);
    return v ? *v : "<absent>";
}
std::string BodyOr(const OWRequest& r) { return r.body ? *r.body : "<none>"; }
std::string KindSent(const OWRequest& r) {
    OWJsonParseResult p = OWJson::Parse(BodyOr(r));
    const OWJsonValue* k = p.ok ? p.value.Find("kind") : nullptr;
    return k && k->IsString() ? k->Text() : "<none>";
}

const OWBytes Pdf = {0x25, 0x50, 0x44, 0x46, 0x2d};  // %PDF-
std::string Raw(const OWBytes& b) { return std::string(b.begin(), b.end()); }

OWFileTicket TicketOf() { return *OWFileTicket::Of(OWJson::Parse(Ticket).value); }

OWUploadFileOptions Named(const std::string& name) {
    OWUploadFileOptions o;
    o.name = name;
    return o;
}

// keel answers the ticket with (keel_status, keel_body), the host answers (host_status, host_body).
std::shared_ptr<FakeTransport> UploadFake(int host_status = 201, std::string host_body = Stored, int keel_status = 201,
                                          std::string keel_body = Ticket, OWHeaders keel_headers = {}) {
    auto f = Fake();
    f->Then(keel_status, std::move(keel_body), std::move(keel_headers));
    f->Then(host_status, std::move(host_body));
    return f;
}

}  // namespace

// -- the API routes ---------------------------------------------------------------------------------------------------

OW_TEST(files_list_files_gets_the_elements_files_with_the_key_and_reads_the_rows) {
    auto fake = Fake();
    fake->body = "{\"data\":[" + Row + "]}";
    Got<std::vector<OWFileRow>> g;
    Make(fake).ListFiles("location", El, g.H());
    t.Ok(g.reply.Ok(), "the rows: " + g.Why());
    t.Eq(fake->calls.size(), size_t{1}, "one request");
    if (fake->calls.empty() || g.reply.value.size() != 1) return t.Fail("one row");
    const OWRequest& r = fake->calls[0];
    t.Eq(r.url, Base + "/location/" + El + "/files", "the route");
    t.Eq(r.method, std::string("GET"), "a GET");
    t.Eq(BodyOr(r), std::string("<none>"), "no body");
    t.Eq(Headers(r), std::string("API-Key=ow_w_test"), "the key alone: a read sends no PIN");
    const OWFileRow& row = g.reply.value[0];
    t.Eq(row.id, Fid, "id");
    t.Eq(row.name, std::string("Map notes.pdf"), "name");
    t.Eq(row.kind, std::string("pdf"), "kind");
    t.Eq(row.size, int64_t{5}, "size");
    t.Eq(row.label.value_or("<absent>"), std::string("description"), "label");
    t.Eq(row.added_by.name, std::string("Ada"), "added_by.name");
    t.Ok(!row.added_by.membership, "added_by.membership null is absent");
    t.Ok(!row.added_by.agent && row.added_by.owner, "added_by.agent and owner as served");
    t.Eq(row.added_at, std::string("2026-10-19T18:00:00Z"), "added_at");
}

OW_TEST(files_list_files_reads_added_bys_owner_as_served_never_from_a_null_membership) {
    auto fake = Fake();
    fake->body = R"({"data":[{"id":"a","added_by":{"name":"Seat","membership":null,"agent":true,"owner":false}},)"
                 R"({"id":"b","added_by":{"name":"Ada","membership":"m1","agent":false,"owner":true}},)"
                 R"({"id":"c","label":null}]})";
    Got<std::vector<OWFileRow>> g;
    Make(fake).ListFiles("location", El, g.H());
    if (g.reply.value.size() != 3) return t.Fail("three rows: " + g.Why());
    t.Ok(!g.reply.value[0].added_by.owner && g.reply.value[0].added_by.agent, "a null membership is not the owner");
    t.Ok(g.reply.value[1].added_by.owner, "owner true with a membership");
    t.Eq(g.reply.value[1].added_by.membership.value_or("<absent>"), std::string("m1"), "the membership");
    t.Ok(!g.reply.value[2].label, "a null label is absent");
    auto none = Fake();
    none->body = "{}";
    Got<std::vector<OWFileRow>> empty;
    Make(none).ListFiles("location", El, empty.H());
    t.Ok(empty.reply.Ok() && empty.reply.value.empty(), "no data: no rows, " + empty.Why());
}

OW_TEST(files_world_files_gets_world_files_and_reads_the_rows_and_the_total) {
    auto fake = Fake();
    fake->body = World;
    Got<OWWorldFiles> g;
    Make(fake).WorldFiles(g.H());
    t.Ok(g.reply.Ok(), "the files: " + g.Why());
    if (fake->calls.empty()) return t.Fail("one request");
    t.Eq(fake->calls[0].url, Base + "/world/files", "the route");
    t.Eq(fake->calls[0].method, std::string("GET"), "a GET");
    t.Eq(g.reply.value.used_bytes, int64_t{42991616}, "used_bytes");
    t.Eq(g.reply.value.cap_bytes, int64_t{262144000}, "cap_bytes");
    if (g.reply.value.data.size() != 1) return t.Fail("one row");
    t.Eq(g.reply.value.data[0].element_type, std::string("location"), "element_type");
    t.Eq(g.reply.value.data[0].element_id, El, "element_id");
    t.Eq(g.reply.value.data[0].name, std::string("Map notes.pdf"), "the row beside them");
}

OW_TEST(files_create_file_ticket_posts_name_kind_size_label_with_the_key_and_pin_and_no_idempotency_key) {
    auto fake = Fake();
    fake->status = 201;
    fake->body = Ticket;
    OWClient c = Make(fake);
    Got<OWFileTicket> a, b;
    OWFileTicketRequest req;
    req.name = "Map notes.pdf";
    req.size = 5;
    req.label = "description";
    c.CreateFileTicket("location", El, req, a.H());
    req.label.reset();
    c.CreateFileTicket("location", El, req, b.H());
    t.Ok(a.reply.Ok(), "the ticket: " + a.Why());
    t.Eq(a.reply.value.ticket, std::string("f1.sig"), "ticket");
    t.Eq(a.reply.value.upload_url, std::string("https://upload.example/v1/file"), "upload_url");
    t.Eq(a.reply.value.key, "w/w1/" + El + "/" + Fid + ".pdf", "key");
    t.Eq(a.reply.value.file_id, Fid, "file_id");
    t.Eq(a.reply.value.kind, std::string("file"), "kind");
    t.Eq(a.reply.value.max_bytes, int64_t{8}, "max_bytes");
    if (fake->calls.size() != 2) return t.Fail("two requests");
    t.Eq(fake->calls[0].url, Base + "/location/" + El + "/files", "the route");
    t.Eq(fake->calls[0].method, std::string("POST"), "a POST");
    t.Eq(BodyOr(fake->calls[0]), std::string(R"({"name":"Map notes.pdf","kind":"pdf","size":5,"label":"description"})"),
         "name, kind, size, label");
    t.Eq(BodyOr(fake->calls[1]), std::string(R"({"name":"Map notes.pdf","kind":"pdf","size":5})"), "no label: none sent");
    for (const OWRequest& r : fake->calls)
        t.Eq(Headers(r), std::string("API-Key=ow_w_test|API-Pin=1234|Content-Type=application/json"),
             "the key and the PIN; no Idempotency-Key, no X-OW-Client, no X-OW-Agent on a person's client");
}

OW_TEST(files_the_kind_is_read_from_the_name_as_keel_reads_it) {
    auto fake = Fake();
    fake->body = Ticket;
    OWClient c = Make(fake);
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"rules.yml", "yaml"}, {"rules.yaml", "yaml"},       {"README.markdown", "md"},  {"notes.md", "md"},
        {"MAP.PDF", "pdf"},    {"a.tar.zip", "zip"},         {"C:\\worlds\\lore.txt", "txt"}, {"data/table.csv", "csv"},
        {"x.json", "json"},    {"Cargo.toml", "toml"},       {"home.png", "png"},        {"shot.jpg", "jpg"},
        {"Photo.JPEG", "jpg"}, {"mock.webp", "webp"},        {"frame.avif", "avif"},
    };
    for (const auto& [name, kind] : cases) {
        OWFileTicketRequest req;
        req.name = name;
        req.size = 1;
        Got<OWFileTicket> g;
        c.CreateFileTicket("character", El, req, g.H());
    }
    if (fake->calls.size() != cases.size()) return t.Fail("one request a name: " + std::to_string(fake->calls.size()));
    for (size_t i = 0; i < cases.size(); ++i) t.Eq(KindSent(fake->calls[i]), cases[i].second, cases[i].first);
    t.Ok(BodyOr(fake->calls[6]).find(R"("name":"C:\\worlds\\lore.txt")") != std::string::npos,
         "the name goes as given: keel cleans it");
}

OW_TEST(files_a_kind_passed_is_sent_as_passed_with_no_reading_of_the_name) {
    auto fake = Fake();
    fake->body = Ticket;
    OWFileTicketRequest req;
    req.name = "odd.bin";
    req.kind = "txt";
    req.size = 1;
    Got<OWFileTicket> g;
    Make(fake).CreateFileTicket("character", El, req, g.H());
    if (fake->calls.empty()) return t.Fail("sent: " + g.Why());
    t.Eq(KindSent(fake->calls[0]), std::string("txt"), "the kind passed");
}

OW_TEST(files_a_name_with_no_extension_keel_takes_is_refused_naming_the_ones_it_takes) {
    auto fake = UploadFake();
    OWClient c = Make(fake);
    const std::regex names(R"(\.pdf .*\.yml .*\.zip)");
    for (const std::string name : {"report.docx", "noextension", ".pdf", "trailing.", "image.gif", "logo.svg"}) {
        OWFileTicketRequest req;
        req.name = name;
        req.size = 1;
        Got<OWFileTicket> a;
        c.CreateFileTicket("character", El, req, a.H());
        t.Ok(a.Kind(OWApiError::Kind::Refused) && std::regex_search(a.reply.error->message, names),
             name + ": refused naming the extensions: " + a.Why());
        Got<OWUploadedFile> b;
        c.UploadFile("character", El, Pdf, Named(name), b.H());
        t.Ok(b.Kind(OWApiError::Kind::Refused), name + ": the upload refused: " + b.Why());
    }
    OWFileTicketRequest empty;
    empty.size = 1;
    Got<OWFileTicket> e;
    c.CreateFileTicket("character", El, empty, e.H());
    t.Ok(e.Kind(OWApiError::Kind::Refused), "an empty name refused: " + e.Why());
    t.Eq(fake->calls.size(), size_t{0}, "nothing sent");
}

OW_TEST(files_file_download_link_gets_download_and_reads_the_link_and_its_exp) {
    auto fake = Fake();
    fake->body = Link;
    Got<OWFileDownload> g;
    Make(fake).FileDownloadLink("location", El, Fid, g.H());
    t.Ok(g.reply.Ok(), "the link: " + g.Why());
    if (fake->calls.empty()) return t.Fail("one request");
    t.Eq(fake->calls[0].url, Base + "/location/" + El + "/files/" + Fid + "/download", "the route");
    t.Eq(fake->calls[0].method, std::string("GET"), "a GET");
    t.Eq(Headers(fake->calls[0]), std::string("API-Key=ow_w_test"), "the key alone");
    t.Eq(g.reply.value.url, std::string("https://files.example/v1/get?t=g1.sig"), "url");
    t.Eq(g.reply.value.exp, int64_t{1900000300}, "exp");
    t.Eq(g.reply.value.name, std::string("Map notes.pdf"), "name");
    t.Eq(g.reply.value.kind, std::string("pdf"), "kind");
    t.Eq(g.reply.value.size, int64_t{5}, "size");
}

OW_TEST(files_remove_file_deletes_with_the_pin_and_a_204_answers_nothing) {
    auto fake = Fake();
    fake->status = 204;
    fake->body = "";
    Got<OWJsonValue> g;
    Make(fake).RemoveFile("location", El, Fid, g.H());
    t.Ok(g.reply.Ok() && g.reply.value.IsNull(), "null: " + g.Why());
    if (fake->calls.empty()) return t.Fail("one request");
    t.Eq(fake->calls[0].url, Base + "/location/" + El + "/files/" + Fid, "the route");
    t.Eq(fake->calls[0].method, std::string("DELETE"), "a DELETE");
    t.Eq(BodyOr(fake->calls[0]), std::string("<none>"), "no body");
    t.Eq(Headers(fake->calls[0]), std::string("API-Key=ow_w_test|API-Pin=1234"), "the key and the PIN, no X-OW-*");
}

OW_TEST(files_remove_file_a_key_neither_owners_nor_uploaders_is_a_403_permission_error) {
    auto fake = Fake();
    fake->status = 403;
    fake->body = R"({"error":{"type":"permission_error","code":"permission_error","message":"Only the file's uploader or the world's owner can remove it."}})";
    Got<OWJsonValue> g;
    Make(fake).RemoveFile("location", El, Fid, g.H());
    t.Ok(g.Kind(OWApiError::Kind::Api) && g.reply.error->status == 403 && g.reply.error->type == "permission_error",
         "403 permission_error: " + g.Why());
}

OW_TEST(files_a_file_id_that_is_not_one_segment_is_refused_nothing_sent) {
    auto fake = Fake();
    OWClient c = Make(fake);
    Got<OWFileDownload> a;
    c.FileDownloadLink("location", El, Fid + "/x", a.H());
    Got<OWJsonValue> b;
    c.RemoveFile("location", El, "..", b.H());
    t.Ok(a.Kind(OWApiError::Kind::Refused) && a.reply.error->message.rfind("fileId ", 0) == 0, "named: " + a.Why());
    t.Ok(b.Kind(OWApiError::Kind::Refused) && b.reply.error->message.rfind("fileId ", 0) == 0, "named: " + b.Why());
    t.Eq(fake->calls.size(), size_t{0}, "nothing sent");
}

// -- the upload -------------------------------------------------------------------------------------------------------

OW_TEST(files_upload_file_the_ticket_with_the_byte_size_then_the_raw_bytes_with_the_ticket_alone) {
    auto fake = UploadFake();
    Got<OWUploadedFile> g;
    OWUploadFileOptions o = Named("Map notes.pdf");
    o.label = "description";
    Make(fake).UploadFile("location", El, Pdf, o, g.H());
    t.Ok(g.reply.Ok(), "the upload: " + g.Why());
    if (fake->calls.size() != 2) return t.Fail("the ticket, then the host: " + std::to_string(fake->calls.size()));
    t.Eq(BodyOr(fake->calls[0]), std::string(R"({"name":"Map notes.pdf","kind":"pdf","size":5,"label":"description"})"),
         "the ticket's body, the size the bytes' length");
    const OWRequest& host = fake->calls[1];
    t.Eq(host.url, std::string("https://upload.example/v1/file"), "the ticket's upload_url");
    t.Eq(host.method, std::string("POST"), "a POST");
    t.Eq(BodyOr(host), Raw(Pdf), "the bytes as given");
    t.Eq(Headers(host), std::string("Authorization=Bearer f1.sig"), "the ticket alone");
    t.Eq(g.reply.value.fid, Fid, "fid");
    t.Eq(g.reply.value.key, "w/w1/" + El + "/" + Fid + ".pdf", "key");
    t.Eq(g.reply.value.bytes, int64_t{5}, "bytes");
    t.Eq(g.reply.value.type, std::string("application/pdf"), "type");
    t.Eq(g.reply.value.etag, std::string("e"), "etag");
}

OW_TEST(files_upload_file_with_no_name_and_no_ticket_is_refused_nothing_sent) {
    auto fake = UploadFake();
    OWClient c = Make(fake);
    Got<OWUploadedFile> a, b;
    c.UploadFile("location", El, Pdf, {}, a.H());
    c.UploadFile("location", El, Pdf, Named("  "), b.H());
    t.Ok(a.Kind(OWApiError::Kind::Refused), "no name: " + a.Why());
    t.Ok(b.Kind(OWApiError::Kind::Refused), "white space: " + b.Why());
    t.Eq(fake->calls.size(), size_t{0}, "nothing sent");
}

OW_TEST(files_upload_file_a_ticket_passed_is_used_keel_is_not_called_and_no_name_is_needed) {
    auto fake = Fake();
    fake->Then(201, Stored);
    Got<OWUploadedFile> g;
    OWUploadFileOptions o;
    o.ticket = TicketOf();
    Make(fake).UploadFile("location", El, Pdf, o, g.H());
    t.Ok(g.reply.Ok(), "the upload: " + g.Why());
    if (fake->calls.size() != 1) return t.Fail("the host alone");
    t.Eq(fake->calls[0].url, std::string("https://upload.example/v1/file"), "to the ticket's upload_url");
}

OW_TEST(files_upload_file_larger_than_max_bytes_is_413_too_large_and_sends_nothing) {
    auto fake = Fake();
    Got<OWUploadedFile> g;
    OWUploadFileOptions o;
    o.ticket = TicketOf();
    Make(fake).UploadFile("location", El, OWBytes(9, 0), o, g.H());
    t.Ok(g.Kind(OWApiError::Kind::Api) && g.reply.error->status == 413 && g.reply.error->code == "too_large" &&
             !g.reply.error->IsFileTooLarge(),
         "413 too_large, not IsFileTooLarge: " + g.Why());
    if (g.reply.error) {
        t.Eq(g.reply.error->message, std::string("File is 9 bytes; this ticket accepts at most 8. Nothing was sent."),
             "the TypeScript SDK's words");
        t.Eq(g.reply.error->raw_body, std::string(R"({"error":"too_large","max_bytes":8,"got":9})"), "the body");
    }
    t.Eq(fake->calls.size(), size_t{0}, "nothing sent");
}

OW_TEST(files_keels_413_file_too_large_is_is_file_too_large_with_param_size_and_the_host_never_called) {
    auto fake = UploadFake(201, Stored, 413,
                           R"({"error":{"type":"invalid_request","code":"file_too_large","message":"This file is 41 MB; a file can be at most 25 MB.","param":"size","doc_url":"https://onlyworlds.github.io/api/errors#file_too_large"}})");
    Got<OWUploadedFile> g;
    Make(fake).UploadFile("location", El, Pdf, Named("big.pdf"), g.H());
    t.Ok(g.reply.error && g.reply.error->IsFileTooLarge() && g.reply.error->param == "size" &&
             g.reply.error->message.find("41 MB") != std::string::npos && !g.reply.error->IsFilesFull(),
         "IsFileTooLarge: " + g.Why());
    t.Eq(fake->calls.size(), size_t{1}, "the host never called");
}

OW_TEST(files_keels_429_quota_exceeded_is_is_quota_exceeded_with_its_retry_after) {
    auto fake = UploadFake(201, Stored, 429,
                           R"({"error":{"type":"rate_limited","code":"quota_exceeded","message":"This world has reached its daily file upload limit."}})",
                           {{"Retry-After", "3600"}});
    Got<OWUploadedFile> g;
    Make(fake).UploadFile("location", El, Pdf, Named("a.pdf"), g.H());
    t.Ok(g.reply.error && g.reply.error->IsQuotaExceeded() && g.reply.error->retry_after == 3600,
         "IsQuotaExceeded, 3600 s: " + g.Why());
    t.Eq(fake->calls.size(), size_t{1}, "the host never called");
}

OW_TEST(files_keels_403_files_full_is_is_files_full_and_not_is_not_author) {
    auto fake = UploadFake(201, Stored, 403,
                           R"({"error":{"type":"permission_error","code":"files_full","message":"This world has 249.6 MB of its 250 MB. This file is 4 MB. Remove a file, or ask the owner."}})");
    Got<OWUploadedFile> g;
    Make(fake).UploadFile("location", El, Pdf, Named("a.pdf"), g.H());
    t.Ok(g.reply.error && g.reply.error->IsFilesFull() && !g.reply.error->IsNotAuthor() &&
             !g.reply.error->IsFileTooLarge(),
         "IsFilesFull alone: " + g.Why());
    auto other = UploadFake(201, Stored, 403,
                            R"({"error":{"type":"permission_error","code":"not_author","message":"Not yours."}})");
    Got<OWUploadedFile> n;
    Make(other).UploadFile("location", El, Pdf, Named("a.pdf"), n.H());
    t.Ok(n.reply.error && n.reply.error->IsNotAuthor() && !n.reply.error->IsFilesFull(), "IsNotAuthor: " + n.Why());
}

OW_TEST(files_the_upload_hosts_refusals_arrive_with_their_codes_and_retry_after) {
    auto exists = UploadFake(409, R"({"error":"exists","key":"k","note":"this file id is already stored; ask keel for a new ticket"})");
    Got<OWUploadedFile> a;
    Make(exists).UploadFile("location", El, Pdf, Named("a.pdf"), a.H());
    t.Ok(a.Kind(OWApiError::Kind::Api) && a.reply.error->status == 409 && a.reply.error->code == "exists",
         "409 exists: " + a.Why());
    auto wrong = UploadFake(415, R"({"error":"unsupported_type","reason":"not a pdf","accepted":["pdf"]})");
    Got<OWUploadedFile> b;
    Make(wrong).UploadFile("location", El, Pdf, Named("a.pdf"), b.H());
    t.Ok(b.Kind(OWApiError::Kind::Api) && b.reply.error->status == 415 && b.reply.error->code == "unsupported_type",
         "415 unsupported_type: " + b.Why());
    auto later = Fake();
    later->Then(201, Ticket);
    later->Then(409, R"({"error":"exists"})", {{"Retry-After", "7"}});
    Got<OWUploadedFile> c;
    Make(later).UploadFile("location", El, Pdf, Named("a.pdf"), c.H());
    t.Ok(c.reply.error && c.reply.error->retry_after == 7, "the host's Retry-After: " + c.Why());
    t.Ok(c.Why().find("f1.sig") == std::string::npos, "the ticket is not in the words");
}

OW_TEST(files_upload_file_a_host_that_cannot_be_reached_is_kind_transport) {
    auto fake = Fake();
    fake->Then(201, Ticket);
    fake->Fail("no answer: ConnectionError");
    Got<OWUploadedFile> g;
    Make(fake).UploadFile("location", El, Pdf, Named("a.pdf"), g.H());
    t.Ok(g.Kind(OWApiError::Kind::Transport), "kind Transport: " + g.Why());
    if (!g.reply.error) return;
    t.Eq(g.reply.error->message,
         std::string("OnlyWorlds file upload failed: POST https://upload.example/v1/file: no answer: ConnectionError"),
         "the host's URL named");
    t.Ok(g.Why().find("f1.sig") == std::string::npos, "the ticket is not in the words");
}

OW_TEST(files_a_host_answer_that_is_not_an_object_is_kind_answer) {
    auto fake = UploadFake(201, "[1]");
    Got<OWUploadedFile> g;
    Make(fake).UploadFile("location", El, Pdf, Named("a.pdf"), g.H());
    t.Ok(g.Kind(OWApiError::Kind::Answer), "kind Answer: " + g.Why());
}

OW_TEST(files_a_download_link_never_reaches_an_errors_words) {
    auto fake = Fake();
    fake->body = R"("https://files.example/v1/get?t=g1.sig")";
    Got<OWFileDownload> g;
    Make(fake).FileDownloadLink("location", El, Fid, g.H());
    t.Ok(g.Kind(OWApiError::Kind::Answer), "a link that is not an object is kind Answer: " + g.Why());
    t.Ok(g.Why().find("g1.sig") == std::string::npos, "the link is not in the words: " + g.Why());
}

OW_TEST(files_an_agent_client_sends_x_ow_agent_on_the_ticket_alone) {
    auto fake = Fake();
    fake->Then(200, "{\"data\":[]}");
    fake->Then(200, World);
    fake->Then(201, Ticket);
    fake->Then(201, Stored);
    fake->Then(200, Link);
    fake->Then(204, "");
    OWClient c = Make(fake, /*agent=*/true);
    Got<std::vector<OWFileRow>> a;
    Got<OWWorldFiles> b;
    Got<OWUploadedFile> d;
    Got<OWFileDownload> e;
    Got<OWJsonValue> f;
    c.ListFiles("location", El, a.H());
    c.WorldFiles(b.H());
    c.UploadFile("location", El, Pdf, Named("a.pdf"), d.H());
    c.FileDownloadLink("location", El, Fid, e.H());
    c.RemoveFile("location", El, Fid, f.H());
    if (fake->calls.size() != 6) return t.Fail("six requests: " + std::to_string(fake->calls.size()));
    for (size_t i = 0; i < fake->calls.size(); ++i) {
        const OWRequest& r = fake->calls[i];
        const bool ticket = i == 2;
        t.Eq(HeaderOr(r, "X-OW-Agent"), std::string(ticket ? "1" : "<absent>"), r.method + " " + r.url);
        t.Eq(HeaderOr(r, "X-OW-Client"), std::string("<absent>"), "no client name: " + r.method + " " + r.url);
    }
    auto plain = Fake();
    plain->body = Ticket;
    OWFileTicketRequest req;
    req.name = "a.pdf";
    req.size = 3;
    Got<OWFileTicket> p;
    Make(plain).CreateFileTicket("location", El, req, p.H());
    t.Eq(plain->calls.empty() ? std::string("<none>") : HeaderOr(plain->calls[0], "X-OW-Agent"), std::string("<absent>"),
         "a person's client never declares an agent");
}

OW_TEST(files_on_progress_is_called_once_with_the_total_after_success_and_never_on_a_failure) {
    std::vector<std::pair<int64_t, int64_t>> seen;
    OWUploadFileOptions o = Named("a.pdf");
    o.on_progress = [&seen](int64_t loaded, int64_t total) { seen.emplace_back(loaded, total); };
    Got<OWUploadedFile> g;
    Make(UploadFake()).UploadFile("location", El, Pdf, o, g.H());
    t.Ok(g.reply.Ok(), "the upload: " + g.Why());
    t.Ok(seen == std::vector<std::pair<int64_t, int64_t>>{{5, 5}}, "once, (5, 5): " + std::to_string(seen.size()));
    seen.clear();
    Got<OWUploadedFile> failed;
    Make(UploadFake(409, R"({"error":"exists"})")).UploadFile("location", El, Pdf, o, failed.H());
    t.Ok(failed.reply.error.has_value(), "the upload failed");
    t.Eq(seen.size(), size_t{0}, "never on a failure");
}

OW_TEST(files_file_extensions_and_file_kind_of_for_a_file_picker) {
    std::vector<std::string> keys;
    for (const auto& [ext, kind] : OWMedia::FileExtensions()) keys.push_back(ext);
    t.Ok(keys == std::vector<std::string>{"pdf", "txt", "md", "markdown", "json", "csv", "yaml", "yml", "toml", "zip",
                                          "png", "jpg", "jpeg", "webp", "avif"},
         "the TypeScript SDK's FILE_EXTENSIONS, in its order: " + std::to_string(keys.size()));
    t.Eq(OWMedia::FileKindOf("Notes/Letters.PDF"), std::string("pdf"), "Notes/Letters.PDF");
    t.Eq(OWMedia::FileKindOf("rules.yml"), std::string("yaml"), "rules.yml");
    t.Eq(OWMedia::FileKindOf("photo.png"), std::string("png"), "photo.png");
    t.Eq(OWMedia::FileKindOf("photo.jpeg"), std::string("jpg"), "photo.jpeg");
    t.Eq(OWMedia::FileKindOf("anim.gif"), std::string(""), "anim.gif");
    t.Eq(OWMedia::FileKindOf("logo.svg"), std::string(""), "logo.svg");
    t.Eq(OWMedia::FileKindOf(".md"), std::string(""), ".md");
    t.Eq(OWMedia::FileKindOf("a.pdf "), std::string("pdf"), "the extension trimmed");
}

OW_TEST(files_an_account_token_with_a_world_is_refused_on_every_file_call_nothing_sent) {
    auto fake = Fake();
    OWClient c = Make(fake, false, "ow_a_acct", std::string("w1"));
    std::vector<std::pair<std::string, std::string>> whys;
    auto keep = [&whys](const char* name) {
        return [&whys, name](const std::optional<OWApiError>& e) {
            whys.emplace_back(name, e && e->kind == OWApiError::Kind::Refused ? e->message : "");
        };
    };
    auto k1 = keep("listFiles");
    c.ListFiles("location", El, [k1](OWReply<std::vector<OWFileRow>> r) { k1(r.error); });
    auto k2 = keep("worldFiles");
    c.WorldFiles([k2](OWReply<OWWorldFiles> r) { k2(r.error); });
    OWFileTicketRequest req;
    req.name = "a.pdf";
    req.size = 3;
    auto k3 = keep("createFileTicket");
    c.CreateFileTicket("location", El, req, [k3](OWReply<OWFileTicket> r) { k3(r.error); });
    auto k4 = keep("uploadFile");
    c.UploadFile("location", El, OWBytes{1, 2, 3}, Named("a.pdf"), [k4](OWReply<OWUploadedFile> r) { k4(r.error); });
    auto k5 = keep("fileDownloadLink");
    c.FileDownloadLink("location", El, "f1", [k5](OWReply<OWFileDownload> r) { k5(r.error); });
    auto k6 = keep("removeFile");
    c.RemoveFile("location", El, "f1", [k6](OWReply<OWJsonValue> r) { k6(r.error); });
    t.Eq(whys.size(), size_t{6}, "each answered");
    for (const auto& [name, why] : whys)
        t.Ok(why.find("world key") != std::string::npos, name + ": refused, naming a world key: " + why);
    t.Eq(fake->calls.size(), size_t{0}, "nothing sent");
}
