#pragma once
// JSON for the folder format: a strict parser (RFC 8259) whose value keeps each object's key order and each number's
// source text, and a writer in the folder spec's section 5 form, the bytes JavaScript's JSON.stringify(value, null, 2)
// gives, plus one trailing newline.
//
// Numbers. A parsed number keeps its text, so the core never turns a number it did not edit into a double. The
// section 5 writer spells a number as JSON.stringify would after JSON.parse: an integer literal as written (so an
// integer past 2^53 keeps its value, as the Unity writer keeps it; JavaScript would round it), any other number as the
// shortest digits that read back to the same double, in ECMAScript's Number::toString layout (`1.0` -> `1`,
// `1e3` -> `1000`, `0.10` -> `0.1`). A number too large for a double (`1E400`) keeps its text: JSON.stringify would
// write `null` and lose it.
//
// Strings are UTF-8. A \u escape of a lone surrogate is kept as that code point in WTF-8 and written back escaped,
// as JSON.stringify writes one.

#include "OWApi.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace OnlyWorlds {

class OW_API OWJsonValue {
public:
    enum class Kind : uint8_t { Null, Bool, Number, String, Array, Object };

    OWJsonValue() = default;
    static OWJsonValue Null() { return OWJsonValue(); }
    static OWJsonValue Bool(bool b);
    static OWJsonValue String(std::string utf8);
    // A number from its JSON text, which must be a valid JSON number (not checked).
    static OWJsonValue NumberText(std::string text);
    static OWJsonValue Number(int64_t n);
    // NaN and the infinities have no JSON form: they become null, as JSON.stringify writes them, a null that remembers
    // the number (NonFinite). OWClient refuses a request body that holds one, before sending.
    static OWJsonValue Number(double d);
    static OWJsonValue Array();
    static OWJsonValue Object();

    Kind kind() const { return kind_; }
    bool IsNull() const { return kind_ == Kind::Null; }
    bool IsBool() const { return kind_ == Kind::Bool; }
    bool IsNumber() const { return kind_ == Kind::Number; }
    bool IsString() const { return kind_ == Kind::String; }
    bool IsArray() const { return kind_ == Kind::Array; }
    bool IsObject() const { return kind_ == Kind::Object; }

    bool AsBool() const { return bool_; }
    // A string's UTF-8, or a number's source text.
    const std::string& Text() const { return text_; }
    // A number as a double (0 when it is not a number).
    double AsDouble() const;
    // The NaN or infinity a null was made from by Number(double): "NaN", "Infinity" or "-Infinity", as JavaScript's
    // String(value) spells it; nullptr for every other value, a parsed null included. Equality and every writer read
    // such a value as the null it is.
    const char* NonFinite() const;

    // Array items, or an object's values (in key order).
    const std::vector<OWJsonValue>& Items() const { return items_; }
    std::vector<OWJsonValue>& Items() { return items_; }
    // An object's keys, parallel to Items().
    const std::vector<std::string>& Keys() const { return keys_; }
    size_t Size() const { return items_.size(); }

    // An object's member, or nullptr.
    const OWJsonValue* Find(std::string_view key) const;
    OWJsonValue* Find(std::string_view key);
    bool Has(std::string_view key) const { return Find(key) != nullptr; }
    // Set a member: replaced in place when present, else appended (key order kept).
    void Set(std::string_view key, OWJsonValue value);
    // Remove a member; true when it was there.
    bool Erase(std::string_view key);
    void Push(OWJsonValue value) { items_.push_back(std::move(value)); }

    // Equal as values: the same kinds, key order and texts (a number by its text).
    bool operator==(const OWJsonValue& o) const;
    bool operator!=(const OWJsonValue& o) const { return !(*this == o); }

private:
    Kind kind_ = Kind::Null;
    bool bool_ = false;
    uint8_t non_finite_ = 0;  // a null's number: 1 NaN, 2 Infinity, 3 -Infinity (Number(double) alone sets it)
    std::string text_;
    std::vector<std::string> keys_;
    std::vector<OWJsonValue> items_;

    friend class OWJsonParser;
};

struct OWJsonParseResult {
    bool ok = false;
    OWJsonValue value;
    std::string error;  // what failed, and at which byte
    bool too_deep = false;  // failed on an object or array past OWJson::MaxDepth (error starts with OWJson::TooDeep)
};

namespace OWJson {

// Parse UTF-8 JSON bytes strictly: RFC 8259's grammar, well-formed UTF-8, no raw control character in a string, one
// value and only white space after it. A byte order mark at the start is read past. A key given twice keeps its first
// position and takes the last value (JSON.parse and Python's json.loads do the same). An object or array nested past
// MaxDepth fails by name (too_deep), the text's top value at 1, objects and arrays both counted (the folder format's
// OW-25), so a plain value inside the 200th is read.
inline constexpr int MaxDepth = 200;
inline constexpr const char* TooDeep = "too_deep";
// A world file's refusal of its world.json nested past MaxDepth, and PackWorld's, in words alone: the code is
// OWArchiveRead::world_fault (the TypeScript SDK 4.212.3's world file says it so). The parser's own error, which a
// folder's error and a skipped file's carry, keeps the code in front: "too_deep: " and these words.
inline constexpr const char* TooDeepWords = "nested more than 200 deep";
OW_API OWJsonParseResult Parse(std::string_view bytes);
OW_API OWJsonParseResult Parse(const std::vector<uint8_t>& bytes);

// The value in section 5's form: 2-space indent, LF, key order kept, a trailing newline. With keep_number_text, each
// number is written as it was spelled (the layout alone is section 5's).
OW_API std::string Stringify(const OWJsonValue& value, bool keep_number_text = false);
OW_API std::vector<uint8_t> ToBytes(const OWJsonValue& value, bool keep_number_text = false);

// A string as a JSON string literal, escaped as JSON.stringify escapes it.
OW_API std::string Quote(std::string_view utf8);

// A double as JavaScript writes a number (Number::toString): "1", "0.1", "1e+21", "1.5e-7"; "null" for NaN and the
// infinities, "0" for -0.
OW_API std::string NumberToString(double d);

// A number's text in section 5's spelling (see the header comment).
OW_API std::string CanonicalNumber(std::string_view text);

}  // namespace OWJson
}  // namespace OnlyWorlds
