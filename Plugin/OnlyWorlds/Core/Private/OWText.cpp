#include "OWText.h"

#include "OWMessage.h"

// The two steps live in OWMessage (Trim and Fold), where the message profile first needed them and where every
// profile reader here calls them; this file gives them the profiles' own names.

namespace OnlyWorlds {
namespace OWText {

std::string TrimAndFold(std::string_view utf8) { return OWMessage::Fold(utf8); }

int CompareByCodePoint(std::string_view a, std::string_view b) {
    // std::char_traits<char> compares as unsigned char (memcmp), so a UTF-8 string's bytes give its code points' order.
    const int c = a.compare(b);
    return c < 0 ? -1 : c > 0 ? 1 : 0;
}

}  // namespace OWText
}  // namespace OnlyWorlds
