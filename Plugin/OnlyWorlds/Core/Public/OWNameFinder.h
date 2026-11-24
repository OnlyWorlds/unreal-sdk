#pragma once
// Element names found in prose: OWNameFinder::Of reads a world's names once, and the finder's Find finds them in any
// text. It finds and never writes: what a tool does with a found name (a hover, a suggestion, a link a person chooses
// to make) is the tool's. A rule of the TypeScript SDK (`nameFinder`, 4.198.0) and the Python package (`name_finder`),
// not of the standard; held to their shared cases (tests/data/name-finder.cases.json, the same bytes).
//
//     OWNameFinder find = OWNameFinder::Of(characters, OWNameCase::Folded);  // a JSON list of element bodies
//     for (const OWNameFound& f : find.Find(text))
//         Hover(f.start, f.end, characters.Items()[f.element]);
//
// Strings are UTF-8, and `start` and `end` are byte offsets into the text, as OWAddress::FindAll's are (the TypeScript
// SDK counts UTF-16 units, the Python package code points).

#include "OWApi.h"
#include "OWJson.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace OnlyWorlds {

// How a name is compared with the text: Exact as written, Folded by simple case folding (Unicode 15.1, CaseFolding's
// C and S rows, the fold the room profile uses). There is no default and no zero value, so a tool never gets the other
// rule by leaving it out.
enum class OWNameCase : uint8_t { Exact = 1, Folded = 2 };

// One name found in a text.
struct OWNameFound {
    size_t start = 0;    // the byte where the name's first character starts
    size_t end = 0;      // the byte just past its last character
    size_t element = 0;  // the element the name belongs to: its place in the list Of was given
};

// A name two or more elements hold: Find answers nothing for it, and this says that it is there.
struct OWNameShared {
    std::string name;               // as the finder compares it: trimmed of white space, folded when Folded
    std::vector<size_t> elements;   // the elements that hold it, two at least, by their place in the list given, in order
};

class OW_API OWNameFinder {
public:
    // The names one element is found by: each value that is a JSON string is a name, any other is passed over.
    using NamesOf = std::function<std::vector<OWJsonValue>(const OWJsonValue& element)>;

    // A finder of the names of `elements` (a JSON list of objects, each with a non-empty string `id`), each found by
    // its `name` (a value that is not a string is passed over), or with `names`, by the names it gives.
    //
    // Find(text) lists every name found, in order, none overlapping:
    // - A whole name only: the character on each side of it is not a letter, a combining mark, a decimal digit or an
    //   underscore, in any script (IsWord). So "Kira's" and "Kira-led" hold the name "Kira", and "Kiran" does not.
    // - A name is trimmed of Unicode's White_Space (its 25 code points) and is three code points at least; a shorter
    //   one is never looked for.
    // - Where several names start at one place, the longest whole one is taken ("Iron Gate" over "Iron").
    // - A name two elements share is never answered, and nothing inside it is: the text stays plain there. One
    //   element listed twice (by id), or found by two of its own names, is one element.
    // - Nothing is read inside an address (what OWAddress::FindAll finds) or a Markdown link `[label](target)`, and no
    //   name is read across the edge of one.
    // - Text is compared as written: no Unicode normalisation, and a space in a name matches that one space.
    //
    // The work for one text grows with its length alone, whatever the names and however many: the text is read once,
    // from its end to its start, and no character is read again for another name. Reading the names grows with their
    // total length. Nothing given is changed, and the finder keeps nothing of it but the names and ids, so `elements`
    // may go after the call.
    //
    // A call that is a mistake (elements that are not a list, an element that is not an object or has no non-empty
    // string id, a case that is neither Exact nor Folded, an empty `names`) answers a finder whose Error() names it,
    // which finds nothing and shares nothing.
    static OWNameFinder Of(const OWJsonValue& elements, OWNameCase name_case);
    static OWNameFinder Of(const OWJsonValue& elements, OWNameCase name_case, const NamesOf& names);

    // Every name found in `text`, as above. The text is UTF-8 (WTF-8 too: a lone surrogate is a code point of its
    // own, never a word character). A byte that does not begin a whole UTF-8 sequence reads as one character, one byte
    // wide, that is no word character.
    std::vector<OWNameFound> Find(std::string_view text) const;

    // Every name two or more elements hold, in the order each name was first met. Find never answers one of these, so
    // a tool reads this list to say why a name stays plain. A name too short to be looked for is not on it.
    const std::vector<OWNameShared>& Shared() const;

    // Empty unless the call was a mistake.
    const std::string& Error() const;

    // Whether a name beside this code point is inside a word: a letter of any script or a combining mark (general
    // category L or M), a decimal digit (Nd) or an underscore, as Unicode 15.1 assigns them.
    static bool IsWord(char32_t cp);

    struct Impl;

private:
    explicit OWNameFinder(std::shared_ptr<const Impl> impl);
    std::shared_ptr<const Impl> impl_;
};

}  // namespace OnlyWorlds
