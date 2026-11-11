// OWLens::IsProposed on the review profile's rule 1 (schema-dist profiles/review.md, serial 87): the npm and Python
// packages' 24 shared cases (npm test/review.test.mjs at 4.102.2, Python tests/test_review.py at 0.84.1), same names,
// same order, and npm 4.151.0's two of rule 5 (serial 114: an object holding a key is a mark, whatever its keys) in
// npm's place, and npm 4.152.0's three doubts (1, true and ["a"]) in npm's places, 29 in all. Each case's answer is the packages' handsOnAsProposed column: whether a reader leaves the element out as
// proposed, a doubt included. Their proposedMarkOf column (the mark's by, expires and lapsed) has no reader here, so it
// is not ported. Each element is JSON text, so a character past ASCII is a \u escape.

#include "ow_test.h"

#include "OWJson.h"
#include "OWLens.h"

#include <string>
#include <vector>

using namespace OnlyWorlds;

namespace {

struct ReviewCase {
    const char* what;
    // The mark's value as JSON text, or nullptr for no key at all.
    const char* mark;
    bool proposed;
};

// In the packages' order.
const std::vector<ReviewCase>& Cases() {
    static const std::vector<ReviewCase> cases = {
        {"no key", nullptr, false},
        {"null: settled", "null", false},
        {"a string: not a mark, a doubt", R"("yes")", true},
        {"an empty string: an empty mark, settled", R"("")", false},
        {"ASCII spaces only: an empty mark, settled", R"("   ")", false},
        {"an empty array: an empty mark, settled", "[]", false},
        {"an array holding an empty array: present, not a mark, a doubt", "[[]]", true},
        {"false: a value, a doubt", "false", true},
        {"zero: a value, a doubt", "0", true},
        {"one: a value, a doubt", "1", true},
        {"true: a value, a doubt", "true", true},
        {"a list holding a string: present, not a mark, a doubt", R"(["a"])", true},
        {"an empty object: an empty mark, settled", "{}", false},
        {"an object with no key the profile names: present, a mark", R"({"x": null})", true},
        {R"(rule 5: only a key the table does not name ({"note": "x"}), a mark that never lapses)",
         R"({"note": "x"})", true},
        {R"(rule 5: a named key whose value is null ({"by": null}), a mark, no proposer named)", R"({"by": null})",
         true},
        {"a whole mark, open", R"({"by": "m1", "evidence": "seen", "expires": "2026-11-01"})", true},
        {"trimmed by and expires", R"({"by": "  m1 ", "expires": " 2026-11-01 "})", true},
        {"lapses on its own day", R"({"by": "m1", "expires": "2026-10-26"})", true},
        {"lapsed before today", R"({"by": "m1", "expires": "2026-10-01"})", true},
        {"an expires not shaped as a day", R"({"by": "m1", "expires": "2026-1-1"})", true},
        {"a blank or non-string by", R"({"by": "   ", "expires": 7})", true},
        {"U+3000 only: White_Space, an empty mark, settled", R"("　")", false},
        {"U+001C only: not White_Space, present, a doubt", R"("\u001c")", true},
        {"a byte order mark only: not White_Space, present, a doubt", R"("﻿")", true},
        {"a by wrapped in U+001C keeps it", R"({"by": "\u001cm1\u001c"})", true},
        {"a by wrapped in a byte order mark keeps it", R"({"by": "﻿m1"})", true},
        {"an expires in Arabic-Indic digits is no day", R"({"by": "m1", "expires": "٢٠٢٦-10-01"})",
         true},
        {"an expires wrapped in a no-break space is trimmed", R"({"expires": " 2026-10-01"})", true},
    };
    return cases;
}

std::string Element(const ReviewCase& c) {
    std::string body = R"({"id": "a", "name": "A")";
    if (c.mark) body += std::string(R"(, ")") + OWLens::ProposedMark + R"(": )" + c.mark;
    return body + "}";
}

}  // namespace

OW_TEST(review_the_cases_are_the_packages_29) {
    t.Eq(Cases().size(), size_t(29), "case count");
    t.Eq(std::string(OWLens::ProposedMark), std::string("x_review_proposed"), "the mark key is the profile's");
}

OW_TEST(review_is_proposed_answers_each_case) {
    size_t run = 0;
    for (const ReviewCase& c : Cases()) {
        OWJsonParseResult r = OWJson::Parse(Element(c));
        if (!r.ok) {
            t.Ok(false, std::string(c.what) + ": the element parses");
            continue;
        }
        t.Eq(OWLens::IsProposed(r.value), c.proposed, std::string(c.what) + ": is_proposed");
        ++run;
    }
    t.Eq(run, size_t(29), "every case runs");
}

// The packages' own extra case: what is not an element reads as settled.
OW_TEST(review_not_an_element_reads_as_settled) {
    for (const char* text : {"null", R"("x")", "3", R"(["a"])"}) {
        OWJsonParseResult r = OWJson::Parse(text);
        t.Ok(r.ok, std::string(text) + " parses");
        if (r.ok) t.Eq(OWLens::IsProposed(r.value), false, text);
    }
}
