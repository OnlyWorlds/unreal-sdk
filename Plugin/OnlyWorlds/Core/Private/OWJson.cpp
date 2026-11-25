#include "OWJson.h"
#include "OWUnicode.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace OnlyWorlds {

// -- the value ----------------------------------------------------------------------------------------------------------

OWJsonValue OWJsonValue::Bool(bool b) {
    OWJsonValue v;
    v.kind_ = Kind::Bool;
    v.bool_ = b;
    return v;
}

OWJsonValue OWJsonValue::String(std::string utf8) {
    OWJsonValue v;
    v.kind_ = Kind::String;
    v.text_ = std::move(utf8);
    return v;
}

OWJsonValue OWJsonValue::NumberText(std::string text) {
    OWJsonValue v;
    v.kind_ = Kind::Number;
    v.text_ = std::move(text);
    return v;
}

OWJsonValue OWJsonValue::Number(int64_t n) { return NumberText(std::to_string(n)); }

OWJsonValue OWJsonValue::Number(double d) {
    if (!std::isfinite(d)) return Null();
    return NumberText(OWJson::NumberToString(d));
}

OWJsonValue OWJsonValue::Array() {
    OWJsonValue v;
    v.kind_ = Kind::Array;
    return v;
}

OWJsonValue OWJsonValue::Object() {
    OWJsonValue v;
    v.kind_ = Kind::Object;
    return v;
}

double OWJsonValue::AsDouble() const {
    if (kind_ != Kind::Number) return 0;
    double d = 0;
    std::from_chars(text_.data(), text_.data() + text_.size(), d);
    return d;
}

const OWJsonValue* OWJsonValue::Find(std::string_view key) const {
    for (size_t i = 0; i < keys_.size(); ++i)
        if (keys_[i] == key) return &items_[i];
    return nullptr;
}

OWJsonValue* OWJsonValue::Find(std::string_view key) {
    return const_cast<OWJsonValue*>(static_cast<const OWJsonValue*>(this)->Find(key));
}

void OWJsonValue::Set(std::string_view key, OWJsonValue value) {
    if (OWJsonValue* at = Find(key)) {
        *at = std::move(value);
        return;
    }
    keys_.emplace_back(key);
    items_.push_back(std::move(value));
}

bool OWJsonValue::Erase(std::string_view key) {
    for (size_t i = 0; i < keys_.size(); ++i) {
        if (keys_[i] == key) {
            keys_.erase(keys_.begin() + i);
            items_.erase(items_.begin() + i);
            return true;
        }
    }
    return false;
}

bool OWJsonValue::operator==(const OWJsonValue& o) const {
    return kind_ == o.kind_ && bool_ == o.bool_ && text_ == o.text_ && keys_ == o.keys_ && items_ == o.items_;
}

// -- the parser ---------------------------------------------------------------------------------------------------------

class OWJsonParser {
public:
    explicit OWJsonParser(std::string_view s) : s_(s) {}

    OWJsonParseResult Run() {
        OWJsonParseResult r;
        if (s_.size() >= 3 && s_.substr(0, 3) == "\xEF\xBB\xBF") at_ = 3;
        if (!OWUnicode::IsUtf8(s_)) return Fail(r, "not well-formed UTF-8");
        Space();
        if (!Value(r.value, 0)) {
            Fail(r, error_);
            r.too_deep = too_deep_;
            return r;
        }
        Space();
        if (at_ != s_.size()) return Fail(r, "data after the value");
        r.ok = true;
        return r;
    }

private:
    std::string_view s_;
    size_t at_ = 0;
    std::string error_;
    bool too_deep_ = false;

    OWJsonParseResult& Fail(OWJsonParseResult& r, const std::string& why) {
        r.ok = false;
        r.value = OWJsonValue();
        r.error = why + " (at byte " + std::to_string(at_) + ")";
        return r;
    }

    bool Err(const char* why) {
        if (error_.empty()) error_ = why;
        return false;
    }

    void Space() {
        while (at_ < s_.size()) {
            char c = s_[at_];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
            ++at_;
        }
    }

    bool Literal(std::string_view word) {
        if (s_.substr(at_, word.size()) != word) return Err("an unknown literal");
        at_ += word.size();
        return true;
    }

    bool Value(OWJsonValue& v, int depth) {
        if (at_ >= s_.size()) return Err("the input ends where a value should be");
        char c = s_[at_];
        if ((c == '{' || c == '[') && depth + 1 > OWJson::MaxDepth) {
            // An object or array past the standard's depth (OW-25; depth counts from 0 here, the text's top at 1 there).
            if (error_.empty()) too_deep_ = true;
            return Err("too_deep: nested more than 200 deep");
        }
        switch (c) {
            case '{': return ObjectValue(v, depth);
            case '[': return ArrayValue(v, depth);
            case '"': v.kind_ = OWJsonValue::Kind::String; return StringBody(v.text_);
            case 't': v = OWJsonValue::Bool(true); return Literal("true");
            case 'f': v = OWJsonValue::Bool(false); return Literal("false");
            case 'n': v = OWJsonValue(); return Literal("null");
            default:
                if (c == '-' || (c >= '0' && c <= '9')) return NumberValue(v);
                return Err("an unexpected character");
        }
    }

    bool NumberValue(OWJsonValue& v) {
        size_t start = at_;
        auto digit = [&](size_t k) { return k < s_.size() && s_[k] >= '0' && s_[k] <= '9'; };
        if (s_[at_] == '-') ++at_;
        if (!digit(at_)) return Err("a minus sign with no digit");
        if (s_[at_] == '0') {
            ++at_;
        } else {
            while (digit(at_)) ++at_;
        }
        if (at_ < s_.size() && s_[at_] == '.') {
            ++at_;
            if (!digit(at_)) return Err("a decimal point with no digit after it");
            while (digit(at_)) ++at_;
        }
        if (at_ < s_.size() && (s_[at_] == 'e' || s_[at_] == 'E')) {
            ++at_;
            if (at_ < s_.size() && (s_[at_] == '+' || s_[at_] == '-')) ++at_;
            if (!digit(at_)) return Err("an exponent with no digit");
            while (digit(at_)) ++at_;
        }
        v.kind_ = OWJsonValue::Kind::Number;
        v.text_.assign(s_.data() + start, at_ - start);
        return true;
    }

    static int Hex(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    bool Hex4(char32_t& out) {
        if (at_ + 4 > s_.size()) return Err("a \\u escape cut short");
        out = 0;
        for (int k = 0; k < 4; ++k) {
            int h = Hex(s_[at_ + k]);
            if (h < 0) return Err("a \\u escape with a character that is not hex");
            out = (out << 4) | static_cast<char32_t>(h);
        }
        at_ += 4;
        return true;
    }

    // The string at `at_` (its opening quote), unescaped into `out`. The input is already known to be UTF-8.
    bool StringBody(std::string& out) {
        ++at_;
        out.clear();
        while (true) {
            size_t run = at_;
            while (at_ < s_.size()) {
                unsigned char c = static_cast<unsigned char>(s_[at_]);
                if (c == '"' || c == '\\' || c < 0x20) break;
                ++at_;
            }
            out.append(s_.data() + run, at_ - run);
            if (at_ >= s_.size()) return Err("a string with no closing quote");
            unsigned char c = static_cast<unsigned char>(s_[at_]);
            if (c == '"') {
                ++at_;
                return true;
            }
            if (c < 0x20) return Err("a raw control character inside a string");
            ++at_;  // the backslash
            if (at_ >= s_.size()) return Err("an escape cut short");
            char e = s_[at_++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    char32_t cp;
                    if (!Hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && at_ + 6 <= s_.size() && s_[at_] == '\\' && s_[at_ + 1] == 'u') {
                        size_t back = at_;
                        at_ += 2;
                        char32_t low;
                        if (!Hex4(low)) return false;
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            at_ = back;  // a lone high surrogate; the next escape is read on its own
                        }
                    }
                    OWUnicode::AppendUtf8(out, cp);
                    break;
                }
                default: return Err("an unknown escape");
            }
        }
    }

    bool ArrayValue(OWJsonValue& v, int depth) {
        ++at_;
        v = OWJsonValue::Array();
        Space();
        if (at_ < s_.size() && s_[at_] == ']') {
            ++at_;
            return true;
        }
        while (true) {
            Space();
            v.items_.emplace_back();
            if (!Value(v.items_.back(), depth + 1)) return false;
            Space();
            if (at_ >= s_.size()) return Err("an array with no closing bracket");
            char c = s_[at_++];
            if (c == ']') return true;
            if (c != ',') return Err("an array item followed by neither a comma nor a bracket");
        }
    }

    bool ObjectValue(OWJsonValue& v, int depth) {
        ++at_;
        v = OWJsonValue::Object();
        Space();
        if (at_ < s_.size() && s_[at_] == '}') {
            ++at_;
            return true;
        }
        std::unordered_map<std::string, size_t> index;  // built only for an object past 32 keys
        std::string key;
        while (true) {
            Space();
            if (at_ >= s_.size() || s_[at_] != '"') return Err("an object key that is not a string");
            if (!StringBody(key)) return false;
            Space();
            if (at_ >= s_.size() || s_[at_] != ':') return Err("an object key with no colon");
            ++at_;
            Space();
            OWJsonValue item;
            if (!Value(item, depth + 1)) return false;
            // A key given twice keeps its first position and takes the last value.
            size_t found = SIZE_MAX;
            if (index.empty() && v.keys_.size() <= 32) {
                for (size_t i = 0; i < v.keys_.size(); ++i)
                    if (v.keys_[i] == key) { found = i; break; }
            } else {
                if (index.empty())
                    for (size_t i = 0; i < v.keys_.size(); ++i) index.emplace(v.keys_[i], i);
                auto it = index.find(key);
                if (it != index.end()) found = it->second;
            }
            if (found != SIZE_MAX) {
                v.items_[found] = std::move(item);
            } else {
                if (!index.empty()) index.emplace(key, v.keys_.size());
                v.keys_.push_back(key);
                v.items_.push_back(std::move(item));
            }
            Space();
            if (at_ >= s_.size()) return Err("an object with no closing brace");
            char c = s_[at_++];
            if (c == '}') return true;
            if (c != ',') return Err("an object member followed by neither a comma nor a brace");
        }
    }
};

namespace OWJson {

OWJsonParseResult Parse(std::string_view bytes) { return OWJsonParser(bytes).Run(); }

OWJsonParseResult Parse(const std::vector<uint8_t>& bytes) {
    return Parse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

// -- numbers ------------------------------------------------------------------------------------------------------------

std::string NumberToString(double d) {
    if (!std::isfinite(d)) return "null";
    if (d == 0) return "0";  // -0 too, as JavaScript writes it
    // The shortest digits that read back to the same double (the closest of them), as to_chars gives them.
    char buf[64];
    auto res = std::to_chars(buf, buf + sizeof buf, d, std::chars_format::scientific);
    std::string s(buf, res.ptr);
    std::string sign;
    if (s[0] == '-') {
        sign = "-";
        s.erase(0, 1);
    }
    size_t e = s.find('e');
    int exp = std::stoi(s.substr(e + 1));
    std::string digits;
    for (size_t i = 0; i < e; ++i)
        if (s[i] != '.') digits.push_back(s[i]);
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
    // ECMAScript Number::toString: the value is digits * 10^(n - k).
    int k = static_cast<int>(digits.size());
    int n = exp + 1;
    if (k <= n && n <= 21) return sign + digits + std::string(n - k, '0');
    if (0 < n && n <= 21) return sign + digits.substr(0, n) + "." + digits.substr(n);
    if (-6 < n && n <= 0) return sign + "0." + std::string(-n, '0') + digits;
    int e1 = n - 1;
    std::string ep = std::string("e") + (e1 >= 0 ? "+" : "-") + std::to_string(e1 >= 0 ? e1 : -e1);
    if (k == 1) return sign + digits + ep;
    return sign + digits.substr(0, 1) + "." + digits.substr(1) + ep;
}

std::string CanonicalNumber(std::string_view text) {
    bool integer = true;
    for (char c : text)
        if (c == '.' || c == 'e' || c == 'E') integer = false;
    if (integer) return text == "-0" ? std::string("0") : std::string(text);
    double d = 0;
    auto res = std::from_chars(text.data(), text.data() + text.size(), d);
    if (res.ec == std::errc::result_out_of_range) {
        // Past a double either way: a magnitude too large keeps its text (JSON.stringify would write null), one too
        // small is 0, as JSON.parse reads it. The decimal exponent of the first significant digit tells them apart.
        size_t i = text[0] == '-' ? 1 : 0;
        long point = 0, first = -1, pos = 0;
        bool seen_point = false;
        for (; i < text.size() && text[i] != 'e' && text[i] != 'E'; ++i) {
            if (text[i] == '.') { seen_point = true; continue; }
            if (!seen_point) ++point;
            if (first < 0 && text[i] != '0') first = pos;
            ++pos;
        }
        // The exponent read with a cap: strtol saturates at a 32-bit long, and the sum below then overflowed, so a
        // number too large was written 0.
        long long exp = 0;
        if (i < text.size()) {
            std::string_view x = text.substr(i + 1);
            bool negative = !x.empty() && x[0] == '-';
            if (!x.empty() && (x[0] == '-' || x[0] == '+')) x.remove_prefix(1);
            for (char c : x) exp = std::min(exp * 10 + (c - '0'), 1000000000LL);
            if (negative) exp = -exp;
        }
        if (first < 0) return "0";
        return point - first + exp > 0 ? std::string(text) : std::string("0");
    }
    if (res.ec != std::errc()) return std::string(text);
    return NumberToString(d);
}

// -- the writer ---------------------------------------------------------------------------------------------------------

namespace {

const char HexDigits[] = "0123456789abcdef";

void AppendQuoted(std::string& out, std::string_view s) {
    out.push_back('"');
    size_t run = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        const char* esc = nullptr;
        char u[7];
        switch (c) {
            case '"': esc = "\\\""; break;
            case '\\': esc = "\\\\"; break;
            case '\b': esc = "\\b"; break;
            case '\f': esc = "\\f"; break;
            case '\n': esc = "\\n"; break;
            case '\r': esc = "\\r"; break;
            case '\t': esc = "\\t"; break;
            default:
                if (c < 0x20) {
                    u[0] = '\\'; u[1] = 'u'; u[2] = '0'; u[3] = '0';
                    u[4] = HexDigits[c >> 4]; u[5] = HexDigits[c & 15]; u[6] = 0;
                    esc = u;
                } else if (c == 0xED && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) >= 0xA0) {
                    // A lone surrogate (WTF-8): JSON.stringify writes it as an escape.
                    char32_t cp = 0xD000 | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) |
                                  (static_cast<unsigned char>(s[i + 2]) & 0x3F);
                    out.append(s.data() + run, i - run);
                    out += "\\u";
                    for (int k = 12; k >= 0; k -= 4) out.push_back(HexDigits[(cp >> k) & 15]);
                    i += 2;
                    run = i + 1;
                    continue;
                }
        }
        if (esc) {
            out.append(s.data() + run, i - run);
            out += esc;
            run = i + 1;
        }
    }
    out.append(s.data() + run, s.size() - run);
    out.push_back('"');
}

void Write(std::string& out, const OWJsonValue& v, std::string& indent, bool keep) {
    switch (v.kind()) {
        case OWJsonValue::Kind::Null: out += "null"; return;
        case OWJsonValue::Kind::Bool: out += v.AsBool() ? "true" : "false"; return;
        case OWJsonValue::Kind::Number: out += keep ? v.Text() : CanonicalNumber(v.Text()); return;
        case OWJsonValue::Kind::String: AppendQuoted(out, v.Text()); return;
        case OWJsonValue::Kind::Array:
        case OWJsonValue::Kind::Object: {
            bool object = v.IsObject();
            if (v.Size() == 0) {
                out += object ? "{}" : "[]";
                return;
            }
            out += object ? "{\n" : "[\n";
            indent += "  ";
            for (size_t i = 0; i < v.Size(); ++i) {
                if (i) out += ",\n";
                out += indent;
                if (object) {
                    AppendQuoted(out, v.Keys()[i]);
                    out += ": ";
                }
                Write(out, v.Items()[i], indent, keep);
            }
            indent.resize(indent.size() - 2);
            out += "\n";
            out += indent;
            out += object ? "}" : "]";
            return;
        }
    }
}

}  // namespace

std::string Quote(std::string_view utf8) {
    std::string out;
    AppendQuoted(out, utf8);
    return out;
}

std::string Stringify(const OWJsonValue& value, bool keep_number_text) {
    std::string out, indent;
    Write(out, value, indent, keep_number_text);
    out += "\n";
    return out;
}

std::vector<uint8_t> ToBytes(const OWJsonValue& value, bool keep_number_text) {
    std::string s = Stringify(value, keep_number_text);
    return std::vector<uint8_t>(s.begin(), s.end());
}

}  // namespace OWJson
}  // namespace OnlyWorlds
