#pragma once
// The test harness, no framework: a test is a function taking a T, registered by OW_TEST (or a suite's cases by
// OW_CASES, built at run time, one per fixture). A test passes when it records no failure; a skip is never a pass.

#include <functional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace owtest {

struct T {
    std::vector<std::string> failures;
    int checks = 0;
    std::string skipped;

    void Ok(bool cond, const std::string& msg) {
        ++checks;
        if (!cond) failures.push_back("not ok: " + msg);
    }
    template <typename A, typename B>
    void Eq(const A& got, const B& want, const std::string& msg) {
        ++checks;
        if (!(got == want)) {
            std::ostringstream s;
            s << msg << ": expected " << Show(want) << ", got " << Show(got);
            failures.push_back(s.str());
        }
    }
    void Fail(const std::string& msg) {
        ++checks;
        failures.push_back(msg);
    }
    // Skip the rest of the test, naming why (a missing world).
    void Skip(const std::string& why) { skipped = why; }

    template <typename V>
    static std::string Show(const V& v) {
        std::ostringstream s;
        s << v;
        std::string out = s.str();
        return out.size() < 300 ? out : out.substr(0, 300) + "...";
    }
    static std::string Show(const std::string& v) { return "\"" + (v.size() < 300 ? v : v.substr(0, 300) + "...") + "\""; }
    static std::string Show(const char* v) { return Show(std::string(v)); }
    static std::string Show(bool v) { return v ? "true" : "false"; }
};

using Fn = std::function<void(T&)>;
using Cases = std::vector<std::pair<std::string, Fn>>;

void Register(const std::string& name, Fn fn);
void RegisterCases(std::function<Cases()> make);

struct Registrar {
    Registrar(const char* name, void (*fn)(T&)) { Register(name, fn); }
    explicit Registrar(Cases (*make)()) { RegisterCases(make); }
};

// Where the tests find their data: the repository's vendored set and example, and two optional real worlds named by
// OW_SIKELIA and OW_MOPPETOPIA ("" when not given; a world not given or not there is a skip, never a pass).
struct Paths {
    std::string root;        // the repository
    std::string fixtures;    // codegen/schema-dist/fixtures/owworld
    std::string example;     // codegen/schema-dist/example/hyperion
    std::string sikelia;
    std::string moppetopia;
    std::string normalization_test;  // Unicode's NormalizationTest-15.1.0.txt (OW_NORMALIZATION_TEST)
    std::string tmp;         // build/tmp, a scratch folder the tests remove
    std::string out;         // build/out: what the tests write for checking outside (the packed archives)
};
const Paths& P();

}  // namespace owtest

#define OW_CAT2(a, b) a##b
#define OW_CAT(a, b) OW_CAT2(a, b)
#define OW_TEST(name)                                                       \
    static void name(owtest::T& t);                                         \
    static owtest::Registrar OW_CAT(ow_reg_, name)(#name, name);            \
    static void name(owtest::T& t)
#define OW_CASES(name)                                                      \
    static owtest::Cases name();                                            \
    static owtest::Registrar OW_CAT(ow_reg_, name)(name);                   \
    static owtest::Cases name()
