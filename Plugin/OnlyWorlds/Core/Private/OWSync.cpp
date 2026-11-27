#include "OWSync.h"

#include "OWAddress.h"

#include <algorithm>
#include <cstddef>
#include <ctime>
#include <memory>
#include <utility>
#include <vector>

namespace OnlyWorlds {

namespace {

// When a sync stopped: ISO 8601, UTC, to the second.
std::string Now() {
    std::time_t now = std::time(nullptr);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char text[32];
    std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%SZ", &utc);
    return text;
}

// A body's own id: its "id" when that is a non-empty string, else "".
std::string BodyId(const OWJsonValue& body) {
    const OWJsonValue* id = body.IsObject() ? body.Find("id") : nullptr;
    return id && id->IsString() ? id->Text() : std::string();
}

// delete, and remove as the Unity SDK reads it (keel sends only delete), in any case.
bool IsDelete(const std::string& op) {
    std::string o = op;
    std::transform(o.begin(), o.end(), o.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; });
    return o == "delete" || o == "remove";
}

template <typename T>
OWReply<OWSyncResult> Failed(const OWReply<T>& r) {
    OWReply<OWSyncResult> out;
    out.error = r.error;
    out.response = r.response;
    return out;
}

template <typename T>
void Refuse(const OWHandler<T>& done, std::string why) {
    OWReply<T> r;
    r.error = OWApiError::Of(OWApiError::Kind::Refused, std::move(why));
    done(std::move(r));
}

// Why a cache may not be synced now, or "".
std::string Refusal(const OWWorldCache& cache) {
    // A snapshot exists to stay what it was; syncing live data into one destroys what it was made to keep.
    if (cache.read_only)
        return "OWSync: the cache '" + cache.world_id +
               "' is read-only (a frozen snapshot). Syncing would overwrite the capture it exists to preserve. Nothing "
               "was sent.";
    if (cache.Syncing())
        return "OWSync: a sync is already in flight on the cache '" + cache.world_id +
               "'; two walks would interleave on one copy. Nothing was sent.";
    return "";
}

}  // namespace

bool OWSyncResult::ChangedAnything() const { return fetched > 0 || removed > 0 || was_rebaselined; }

// One sync: its state, and the steps it takes. Each answer is not handled inside the transport's call but queued as
// the next step and run by Pump, so a transport that answers inside Send (the tests' fake) walks a long feed with a
// flat stack, and one that answers later (Unreal's HTTP) runs the same steps. Held by shared_ptr, captured by every
// request's handler, so it lives as long as its walk.
class OWSync::Run : public std::enable_shared_from_this<OWSync::Run> {
public:
    using Then = std::function<void(OWReply<OWSyncResult>)>;

    Run(OWClient& c, OWWorldCache& w, OWHandler<OWSyncResult> d) : client(c), cache(w), done(std::move(d)) {}

    OWClient& client;
    OWWorldCache& cache;
    OWHandler<OWSyncResult> done;
    std::function<void(const std::string&, size_t)> on_type;

    void Start() { cache.syncing_ = true; }

    // -- The baseline: the tip, then the 22 list routes ------------------------------------------------------------

    void Baseline(Then then, bool report_types) {
        auto self = shared_from_this();
        client.ChangesHead(Queued<OWChangesPage>([self, then, report_types](OWReply<OWChangesPage> tip_r) {
            if (!tip_r.Ok()) return then(Failed(tip_r));
            auto walk = std::make_shared<BaseWalk>();
            walk->tip = std::move(tip_r.value);
            walk->response = std::move(tip_r.response);
            walk->held = Held::Of(self->cache);
            walk->report_types = report_types;
            self->cache.Clear();
            self->NextType(walk, then);
        }));
    }

    // -- The incremental walk ---------------------------------------------------------------------------------------

    void Incremental() {
        if (cache.Cursor() <= 0 && cache.CursorToken().empty())
            return Baseline(Finisher(), false);
        auto self = shared_from_this();
        client.ChangesHead(Queued<OWChangesPage>([self](OWReply<OWChangesPage> tip_r) {
            if (!tip_r.Ok()) return self->Finish(Failed(tip_r));
            self->tip = std::move(tip_r.value);
            OWWorldCache& cache = self->cache;
            if (cache.Cursor() > self->tip.head) {
                // Captured before the walk, which moves the cache's cursor to the new tip.
                int64_t stale = cache.Cursor(), head = self->tip.head;
                return self->Rewalk(true, [self, stale, head](OWReply<OWSyncResult> r) {
                    if (r.Ok())
                        r.value.rebaseline_reason =
                            "Cursor " + std::to_string(stale) + " was ahead of server head " + std::to_string(head) +
                            " -- the server was likely restored from a backup. Walked again rather than trusting the "
                            "cursor, and kept " +
                            std::to_string(r.value.kept) + " element(s) the new walk no longer names.";
                    self->Finish(std::move(r));
                });
            }
            // Current only when the epoch stands too: a guest's view can change with head unmoved.
            if (cache.Cursor() == self->tip.head && cache.Epoch() == self->tip.epoch) {
                OWReply<OWSyncResult> none;
                none.value.cursor = cache.Cursor();
                none.response = std::move(tip_r.response);
                return self->Finish(std::move(none));
            }
            self->since = cache.CursorToken().empty() ? std::to_string(cache.Cursor()) : cache.CursorToken();
            self->highest = cache.Cursor();
            self->epoch = self->tip.epoch;
            self->last = std::move(tip_r.response);
            self->NextPage();
        }));
    }

    // -- Pumping ------------------------------------------------------------------------------------------------------

    // A handler that queues its answer as the next step.
    template <typename T>
    OWHandler<T> Queued(std::function<void(OWReply<T>)> step) {
        auto self = shared_from_this();
        return [self, step](OWReply<T> r) {
            auto box = std::make_shared<OWReply<T>>(std::move(r));
            self->next = [step, box]() { step(std::move(*box)); };
            self->Pump();
        };
    }

    // Runs the queued steps until none is left; a step queued from inside one runs after it returns.
    void Pump() {
        if (pumping) return;
        auto self = shared_from_this();
        pumping = true;
        while (next) {
            std::function<void()> step = std::move(next);
            next = nullptr;
            step();
        }
        pumping = false;
    }

    void Finish(OWReply<OWSyncResult> r) {
        if (answered) return;
        answered = true;
        cache.syncing_ = false;
        done(std::move(r));
    }

    Then Finisher() {
        auto self = shared_from_this();
        return [self](OWReply<OWSyncResult> r) { self->Finish(std::move(r)); };
    }

private:
    // What a cache held before a walk: its elements and where its last sync stopped, to put back.
    struct Held {
        std::vector<OWCachedElement> elements;
        int64_t cursor = 0;
        std::string token;
        std::optional<int64_t> epoch;
        std::string at;

        static Held Of(const OWWorldCache& cache) {
            Held h;
            for (const OWCachedElement* e : cache.Elements()) h.elements.push_back(*e);
            h.cursor = cache.Cursor();
            h.token = cache.CursorToken();
            h.epoch = cache.Epoch();
            h.at = cache.SyncedAt();
            return h;
        }

        void PutBack(OWWorldCache& cache) const {
            cache.Clear();
            for (const OWCachedElement& e : elements) cache.Upsert(e.type, e.body);
            cache.SetCursor(cursor, token, epoch, at);
        }
    };

    struct BaseWalk {
        OWChangesPage tip;
        std::optional<OWResponse> response;
        Held held;
        size_t type = 0;
        int64_t fetched = 0;
        bool report_types = false;
    };

    void NextType(const std::shared_ptr<BaseWalk>& walk, const Then& then) {
        if (walk->type == 22) {
            cache.SetCursor(walk->tip.head, walk->tip.cursor, walk->tip.epoch, Now());
            OWReply<OWSyncResult> r;
            r.value.fetched = walk->fetched;
            r.value.cursor = walk->tip.head;
            r.value.was_rebaselined = true;
            r.value.rebaseline_reason = "Full baseline requested.";
            r.response = walk->response;
            return then(std::move(r));
        }
        std::string type = OWAddress::TypeWords[walk->type];
        auto self = shared_from_this();
        client.ListAll(type, {}, Queued<std::vector<OWJsonValue>>([self, walk, then, type](OWReply<std::vector<OWJsonValue>> r) {
            if (!r.Ok()) {
                // A walk that fails leaves the cache as it was: a baseline never empties a copy on its own.
                walk->held.PutBack(self->cache);
                return then(Failed(r));
            }
            size_t count = r.value.size();
            for (OWJsonValue& body : r.value)
                if (self->cache.Upsert(type, std::move(body))) ++walk->fetched;
            if (walk->report_types && self->on_type) self->on_type(type, count);
            ++walk->type;
            self->NextType(walk, then);
        }));
    }

    // A baseline that puts back, when `keep`, every element the cache held that the new walk does not name.
    void Rewalk(bool keep, Then then) {
        auto held = std::make_shared<Held>(Held::Of(cache));
        auto self = shared_from_this();
        // A baseline that fails has put the cache back itself.
        Baseline([self, keep, held, then](OWReply<OWSyncResult> r) {
            if (r.Ok() && keep) {
                for (const OWCachedElement& e : held->elements) {
                    if (self->cache.Has(e.id)) continue;
                    self->cache.Upsert(e.type, e.body);
                    ++r.value.kept;
                }
            }
            then(std::move(r));
        }, false);
    }

    // The answer to a 409 resync_required: walk again from the start, then settle what the cache held that the new walk
    // does not name by the server's reason (keel's changes page, "What a reset keeps").
    void Reset(std::optional<std::string> reason) {
        bool keep = reason != std::optional<std::string>("view");
        auto self = shared_from_this();
        Rewalk(keep, [self, reason, keep](OWReply<OWSyncResult> r) {
            if (r.Ok()) {
                r.value.resync_reason = reason;
                if (keep)
                    r.value.rebaseline_reason =
                        std::string("The server asked for a full resync (409 resync_required, ") +
                        (reason ? "reason role: this key's role or membership moved"
                                : "no reason given, kept as for role") +
                        "). Walked again from the start and kept " + std::to_string(r.value.kept) +
                        " element(s) the new walk no longer names: out of view now, not gone.";
                else
                    r.value.rebaseline_reason =
                        "The server asked for a full resync (409 resync_required, reason view): something left this "
                        "key's view. Walked again from the start and dropped what the new walk no longer names.";
            }
            self->Finish(std::move(r));
        });
    }

    void NextPage() {
        OWChangesOptions o;
        o.since = since;
        auto self = shared_from_this();
        client.Changes(o, Queued<OWChangesPage>([self](OWReply<OWChangesPage> page_r) {
            if (!page_r.Ok()) {
                if (page_r.error->IsResyncRequired()) return self->Reset(page_r.error->ResyncReason());
                return self->Finish(Failed(page_r));
            }
            self->last = std::move(page_r.response);
            self->page = std::move(page_r.value);
            // A page that says "no more" is the only honest end of the feed.
            if (!self->page.has_more) self->to_the_end = true;
            self->epoch = self->page.epoch;
            self->row = 0;
            self->NextRow();
        }));
    }

    void NextRow() {
        while (row < page.changes.size()) {
            OWChange& c = page.changes[row];
            // A watermark only, never an identity: a bulk import stamps many elements with one seq.
            if (c.change_seq > highest) highest = c.change_seq;
            if (IsDelete(c.op)) {
                if (cache.Remove(c.id)) ++removed;
                ++row;
                continue;
            }
            if (c.element) {
                Hold(c, std::move(*c.element));
                ++row;
                continue;
            }
            // An upsert with no body: read it by id. Its error, a 409 too, is answered as it is.
            auto self = shared_from_this();
            client.Get(c.type, c.id, {}, Queued<OWJsonValue>([self](OWReply<OWJsonValue> got) {
                if (!got.Ok()) return self->Finish(Failed(got));
                self->Hold(self->page.changes[self->row], std::move(got.value));
                ++self->row;
                self->NextRow();
            }));
            return;
        }
        // The server's cursor is the resume point, as is; keep the last one it gave.
        if (!page.cursor.empty()) since = page.cursor;
        if (page.has_more && !page.cursor.empty()) return NextPage();
        cache.SetCursor(to_the_end ? std::max(highest, tip.head) : cache.Cursor(), since, epoch, Now());
        OWReply<OWSyncResult> r;
        r.value.fetched = fetched;
        r.value.removed = removed;
        r.value.cursor = cache.Cursor();
        r.response = std::move(last);
        Finish(std::move(r));
    }

    // Hold an upsert's body under its own id; a body with none takes the row's, so the cache and its OWWorld view index
    // one id.
    void Hold(const OWChange& c, OWJsonValue body) {
        if (!body.IsObject()) return;
        if (BodyId(body).empty()) body.Set("id", OWJsonValue::String(c.id));
        if (cache.Upsert(c.type, std::move(body))) ++fetched;
    }

    std::function<void()> next;
    bool pumping = false;
    bool answered = false;

    // The incremental walk's state.
    OWChangesPage tip;
    OWChangesPage page;
    size_t row = 0;
    std::string since;
    int64_t highest = 0;
    std::optional<int64_t> epoch;
    bool to_the_end = false;
    int64_t fetched = 0;
    int64_t removed = 0;
    std::optional<OWResponse> last;
};

void OWSync::Baseline(OWClient& client, OWWorldCache& cache, OWHandler<OWSyncResult> done,
                      std::function<void(const std::string&, size_t)> on_type) {
    std::string why = Refusal(cache);
    if (!why.empty()) return Refuse(done, std::move(why));
    auto run = std::make_shared<Run>(client, cache, std::move(done));
    run->on_type = std::move(on_type);
    run->Start();
    run->Baseline(run->Finisher(), true);
    run->Pump();
}

void OWSync::Incremental(OWClient& client, OWWorldCache& cache, OWHandler<OWSyncResult> done) {
    std::string why = Refusal(cache);
    if (!why.empty()) return Refuse(done, std::move(why));
    auto run = std::make_shared<Run>(client, cache, std::move(done));
    run->Start();
    run->Incremental();
    run->Pump();
}

void OWSync::HasChanges(OWClient& client, const OWWorldCache& cache, OWHandler<bool> done) {
    const OWWorldCache* held = &cache;
    client.ChangesHead([held, done](OWReply<OWChangesPage> r) {
        OWReply<bool> out;
        out.error = std::move(r.error);
        out.response = std::move(r.response);
        if (!out.error) out.value = r.value.head != held->Cursor() || r.value.epoch != held->Epoch();
        done(std::move(out));
    });
}

}  // namespace OnlyWorlds
