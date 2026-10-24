// The test runner: every registered test, in file and registration order, then the cases each suite builds.
//
//   build\ow_tests.exe [--filter=<substring>] [--require-all]
//
// Prints one line per test (ok, FAIL with its failures, SKIP with its reason) and the counts. Exits 0 only when no
// test failed; with --require-all, a skip fails the run too (a machine that should hold both worlds).

#include "ow_test.h"
#include "OWFiles.h"
#include "OWVersion.h"
#include "OWUnicode.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

namespace owtest {

namespace {
std::vector<std::pair<std::string, Fn>>& Tests() {
    static std::vector<std::pair<std::string, Fn>> t;
    return t;
}
std::vector<std::function<Cases()>>& Suites() {
    static std::vector<std::function<Cases()>> s;
    return s;
}
Paths g_paths;

std::string Env(const char* name, const std::string& fallback) {
    std::string out = fallback;
#ifdef _WIN32
    char* v = nullptr;
    size_t n = 0;
    if (_dupenv_s(&v, &n, name) == 0 && v && *v) out = v;
    std::free(v);
#else
    if (const char* v = std::getenv(name); v && *v) out = v;
#endif
    return out;
}
}  // namespace

void Register(const std::string& name, Fn fn) { Tests().emplace_back(name, std::move(fn)); }
void RegisterCases(std::function<Cases()> make) { Suites().push_back(std::move(make)); }
const Paths& P() { return g_paths; }

}  // namespace owtest

int main(int argc, char** argv) {
    using namespace owtest;
    std::string filter;
    bool require_all = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--filter=", 0) == 0) filter = a.substr(9);
        else if (a == "--require-all") require_all = true;
    }
    // The repository is the folder above the one holding the exe (build\ow_tests.exe).
    std::filesystem::path exe = std::filesystem::absolute(argv[0]);
    std::string root = OnlyWorlds::OWFiles::Absolute(exe.parent_path().parent_path().u8string());
    g_paths.root = root;
    g_paths.fixtures = root + "/codegen/schema-dist/fixtures/owworld";
    g_paths.example = root + "/codegen/schema-dist/example/hyperion";
    g_paths.sikelia = Env("OW_SIKELIA", "C:/Users/Titus/Development/OnlyWorlds/sikelia-world");
    g_paths.moppetopia = Env("OW_MOPPETOPIA", "C:/Users/Titus/Carrier/Armature/projects/moppetopia-world");
    g_paths.normalization_test = Env("OW_NORMALIZATION_TEST",
                                     "C:/Users/Titus/Carrier/Armature/tools/owworld-fixtures/NormalizationTest-15.1.0.txt");
    g_paths.tmp = root + "/build/tmp";
    g_paths.out = root + "/build/out";
    std::printf("ow_tests: zlib %s, Unicode %s, repository %s\n\n", OnlyWorlds::ZlibVersion(),
                OnlyWorlds::OWUnicode::Version(), root.c_str());

    Cases all = Tests();
    for (auto& make : Suites())
        for (auto& c : make()) all.push_back(std::move(c));

    int passed = 0, failed = 0, skipped = 0, checks = 0;
    auto t_all = std::chrono::steady_clock::now();
    for (auto& [name, fn] : all) {
        if (!filter.empty() && name.find(filter) == std::string::npos) continue;
        T t;
        auto t0 = std::chrono::steady_clock::now();
        try {
            fn(t);
        } catch (const std::exception& e) {
            t.failures.push_back(std::string("threw: ") + e.what());
        } catch (...) {
            t.failures.push_back("threw something that is not a std::exception");
        }
        long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        checks += t.checks;
        if (!t.failures.empty()) {
            ++failed;
            std::printf("FAIL %s\n", name.c_str());
            for (auto& f : t.failures) std::printf("       %s\n", f.c_str());
        } else if (!t.skipped.empty()) {
            ++skipped;
            std::printf("SKIP %s: %s\n", name.c_str(), t.skipped.c_str());
        } else {
            ++passed;
            std::printf("ok   %s (%d checks, %lld ms)\n", name.c_str(), t.checks, ms);
        }
        std::fflush(stdout);
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_all).count();
    std::printf("\nTOTAL: %d tests, %d passed, %d failed, %d skipped (%d checks) in %.1f s\n", passed + failed + skipped,
                passed, failed, skipped, checks, secs);
    if (failed > 0) return 1;
    if (require_all && skipped > 0) {
        std::printf("--require-all: %d skipped\n", skipped);
        return 2;
    }
    return 0;
}
