#include "OWStrand.h"

#include "OWLens.h"
#include "OWLensView.h"

// keel's strand_of (elements/strand.py) over a world snapshot, read through the lens reader's own view
// (OWLensView::See): who sees what, and the body each reader is served. The Godot SDK's ow_strand.gd is the closest
// port in shape.

namespace OnlyWorlds {

namespace OWStrand {
const char* const Mark = "x_strand";

const std::vector<std::string>& Members() {
    static const std::vector<std::string> members = {"narrative", "event", "location"};
    return members;
}
}  // namespace OWStrand

namespace {

const OWJsonValue* Get(const OWJsonValue& body, std::string_view key) {
    return body.IsObject() ? body.Find(key) : nullptr;
}

OWJsonValue SlotJson(const std::optional<OWStrandSlot>& slot) {
    if (!slot) return OWJsonValue::Null();
    OWJsonValue out = OWJsonValue::Object();
    out.Set("id", OWJsonValue::String(slot->id));
    out.Set("name", OWJsonValue::String(slot->name));
    return out;
}

}  // namespace

OWJsonValue OWWorldStrand::ToJson() const {
    OWJsonValue out = OWJsonValue::Object();
    out.Set("narrative", SlotJson(narrative));
    out.Set("event", SlotJson(event));
    out.Set("location", SlotJson(location));
    out.Set("character", SlotJson(character));
    out.Set("now", SlotJson(now));
    out.Set("same", same ? OWJsonValue::Bool(*same) : OWJsonValue::Null());
    return out;
}

namespace OWStrand {

OWWorldStrand StrandOf(const OWJsonValue& world, const OWJsonValue& reader) {
    OWWorldStrand out;
    OWLensView::Sight s;
    if (std::string bad = OWLensView::See(world, reader, nullptr, OWLensOptions(), "strand_of", s); !bad.empty()) {
        out.error = bad;
        return out;
    }
    const OWJsonValue* mark = world.Find(Mark);
    if (!mark || !mark->IsObject()) return out;
    // The story's member a string; the event's and the place's each a non-empty string, or null or absent (unset);
    // anything else there (an empty string, a number, an object) is no mark.
    const OWJsonValue* named[3] = {mark->Find(Members()[0]), mark->Find(Members()[1]), mark->Find(Members()[2])};
    if (!named[0] || !named[0]->IsString()) return out;
    for (size_t m = 1; m < 3; ++m) {
        if (named[m] && named[m]->IsNull()) named[m] = nullptr;
        if (named[m] && (!named[m]->IsString() || named[m]->Text().empty())) return out;
    }
    const OWJsonValue* open = world.Find("open");
    if (s.seen_rung == "public" && !(open && open->IsBool() && open->AsBool())) return out;

    // A slot holds only its own kind: the element of that id is of that type, in the reader's view.
    auto seen = [&](const OWJsonValue* id, const char* kind) -> std::optional<size_t> {
        if (!id || !id->IsString()) return std::nullopt;
        std::optional<size_t> i = s.Find(kind, id->Text());
        return i && s.view[*i] ? i : std::nullopt;
    };
    auto slot = [&](size_t i) {
        const OWJsonValue* name = Get(s.served(i), "name");
        return OWStrandSlot{s.ids[i], name && name->IsString() ? name->Text() : std::string()};
    };
    // Membership in the list the holder is served, never its position.
    auto listed = [&](size_t holder, const char* field, const std::string& id) {
        const OWJsonValue* list = Get(s.served(holder), field);
        if (!list || !list->IsArray()) return false;
        for (const OWJsonValue& entry : list->Items())
            if (entry.IsString() && entry.Text() == id) return true;
        return false;
    };

    std::optional<size_t> story = seen(named[0], "narrative");
    if (!story) return out;  // a strand hangs off its story
    out.narrative = slot(*story);
    if (std::optional<size_t> happening = seen(named[1], "event");
        happening && listed(*story, "events", named[1]->Text())) {
        out.event = slot(*happening);
        if (std::optional<size_t> then = seen(named[2], "location");
            then && listed(*happening, "locations", named[2]->Text()))
            out.location = slot(*then);
    }
    if (std::optional<size_t> hero = seen(Get(s.served(*story), "protagonist"), "character")) {
        out.character = slot(*hero);
        if (std::optional<size_t> where = seen(Get(s.served(*hero), "location"), "location")) out.now = slot(*where);
    }
    if (out.now && out.location) out.same = out.now->id == out.location->id;
    return out;
}

}  // namespace OWStrand
}  // namespace OnlyWorlds
