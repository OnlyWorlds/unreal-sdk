#include "OWFiles.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace OnlyWorlds {
namespace OWFiles {

namespace {
// UTF-8 in and out of a path, in C++17 (build.cmd) and C++20 (the Unreal module: 5.8 builds no module as C++17),
// where u8path is deprecated and u8string() returns std::u8string.
#if defined(__cpp_char8_t)
fs::path P(const std::string& utf8) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}
std::string U8(const fs::path& p) {
    std::u8string s = p.u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}
#else
fs::path P(const std::string& utf8) { return fs::u8path(utf8); }
std::string U8(const fs::path& p) { return p.u8string(); }
#endif
}  // namespace

bool Read(const std::string& path, OWBytes& out) {
    out.clear();
#ifdef _WIN32
    FILE* f = nullptr;
    if (_wfopen_s(&f, P(path).c_str(), L"rb") != 0) f = nullptr;
#else
    FILE* f = std::fopen(P(path).c_str(), "rb");
#endif
    if (!f) return false;
    bool ok = true;
    if (std::fseek(f, 0, SEEK_END) == 0) {
#ifdef _WIN32
        long long size = _ftelli64(f);
#else
        long long size = ftello(f);
#endif
        if (size < 0 || std::fseek(f, 0, SEEK_SET) != 0) {
            ok = false;
        } else {
            out.resize(static_cast<size_t>(size));
            if (size > 0 && std::fread(out.data(), 1, out.size(), f) != out.size()) ok = false;
        }
    } else {
        ok = false;
    }
    std::fclose(f);
    if (!ok) out.clear();
    return ok;
}

bool MakeDirs(const std::string& dir) {
    std::error_code ec;
    fs::create_directories(P(dir), ec);
    return fs::is_directory(P(dir), ec);
}

bool Write(const std::string& path, const OWBytes& data, bool make_dirs) {
    fs::path p = P(path);
    std::error_code ec;
    if (make_dirs && p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
#ifdef _WIN32
    FILE* f = nullptr;
    if (_wfopen_s(&f, p.c_str(), L"wb") != 0) f = nullptr;
#else
    FILE* f = std::fopen(p.c_str(), "wb");
#endif
    if (!f) return false;
    bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
    return std::fclose(f) == 0 && ok;
}

bool IsFile(const std::string& path) {
    std::error_code ec;
    return fs::is_regular_file(P(path), ec);
}

bool IsDir(const std::string& path) {
    std::error_code ec;
    return fs::is_directory(P(path), ec);
}

std::vector<std::string> List(const std::string& dir, bool dirs) {
    std::vector<std::string> out;
    std::error_code ec;
    for (fs::directory_iterator it(P(dir), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        bool is_dir = it->is_directory(e2);
        if (is_dir == dirs && (dirs || it->is_regular_file(e2))) out.push_back(U8(it->path().filename()));
    }
    std::sort(out.begin(), out.end());  // std::string orders by unsigned bytes: UTF-8 byte order
    return out;
}

bool RemoveTree(const std::string& dir) {
    std::error_code ec;
    fs::remove_all(P(dir), ec);
    return !fs::exists(P(dir), ec);
}

bool Move(const std::string& from, const std::string& to) {
    std::error_code ec;
    if (fs::exists(P(to), ec)) return false;
    fs::rename(P(from), P(to), ec);
    return !ec;
}

std::string Absolute(const std::string& path) {
    std::error_code ec;
    fs::path p = fs::absolute(P(path), ec).lexically_normal();
    std::string s = U8(p);
    std::replace(s.begin(), s.end(), '\\', '/');
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    return s;
}

std::string LastSegment(const std::string& path) {
    std::string s = path;
    std::replace(s.begin(), s.end(), '\\', '/');
    while (!s.empty() && s.back() == '/') s.pop_back();
    size_t at = s.rfind('/');
    return at == std::string::npos ? s : s.substr(at + 1);
}

}  // namespace OWFiles
}  // namespace OnlyWorlds
