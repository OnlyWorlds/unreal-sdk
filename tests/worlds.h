#pragma once
// The two real worlds, each read once per run and shared by the folder and pack tests (Sikelia is about 20,570
// element files: read once, never in a loop).

#include "ow_test.h"

#include "OWFiles.h"
#include "OWWorld.h"

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace owtest {

struct LoadedWorld {
    std::unique_ptr<OnlyWorlds::OWWorld> world;
    long long read_ms = 0;
    std::string missing;  // why there is no world ("" when there is)
};

inline LoadedWorld& Loaded(const std::string& root) {
    static std::map<std::string, LoadedWorld> cache;
    auto it = cache.find(root);
    if (it != cache.end()) return it->second;
    LoadedWorld& lw = cache[root];
    if (root.empty()) {
        lw.missing = "no world folder given";
        return lw;
    }
    if (!OnlyWorlds::OWFiles::IsFile(root + "/world.json")) {
        lw.missing = "no world at " + root;
        return lw;
    }
    auto t0 = std::chrono::steady_clock::now();
    lw.world = OnlyWorlds::OWWorld::ReadFolder(root);
    lw.read_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    return lw;
}

// Every element file under a folder's elements/ (and spatial/), counted by a walk of its own (listings only).
inline size_t CountElementFiles(const std::string& root) {
    size_t n = 0;
    for (const char* top : {"elements", "spatial"}) {
        std::string base = root + "/" + top;
        if (!OnlyWorlds::OWFiles::IsDir(base)) continue;
        for (const std::string& type : OnlyWorlds::OWFiles::List(base, true)) {
            if (type[0] == '.') continue;
            for (const std::string& f : OnlyWorlds::OWFiles::List(base + "/" + type, false))
                if (f[0] != '.' && f.size() > 5 && f.compare(f.size() - 5, 5, ".json") == 0) ++n;
        }
    }
    return n;
}

// Every file under a folder, by path inside it, recursively (listings only).
inline void ListTree(const std::string& dir, const std::string& rel, std::vector<std::string>& out) {
    for (const std::string& f : OnlyWorlds::OWFiles::List(dir, false)) out.push_back(rel.empty() ? f : rel + "/" + f);
    for (const std::string& d : OnlyWorlds::OWFiles::List(dir, true))
        ListTree(dir + "/" + d, rel.empty() ? d : rel + "/" + d, out);
}

}  // namespace owtest
