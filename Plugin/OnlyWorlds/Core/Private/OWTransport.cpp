#include "OWTransport.h"

namespace OnlyWorlds {

namespace {
char LowerAscii(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
}  // namespace

const std::string* OWHeader(const OWHeaders& headers, std::string_view name) {
    for (const auto& [key, value] : headers) {
        if (key.size() != name.size()) continue;
        bool same = true;
        for (size_t i = 0; i < key.size() && same; ++i) same = LowerAscii(key[i]) == LowerAscii(name[i]);
        if (same) return &value;
    }
    return nullptr;
}

}  // namespace OnlyWorlds
