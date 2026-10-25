#pragma once
// The zip layer under OWWorldArchive: a walk of the archive's own central directory, one entry's bytes inflated
// through zlib and checked against every size it declares, and a writer that lays down its own bytes.
//
// Why our own (the recon, lane-unreal-recon.md section 4): the engine's FZipArchiveReader is editor-only and folds
// names; libzip is desktop-only and misses both OW-14 deflate cases; FCompression accepts an early end. Here every
// field comes from the directory as written, and zlib's own inflate says where a stream ended.

#include "OWFiles.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace OnlyWorlds {

struct OWZipEntry {
    std::string name;  // the name's bytes as written (UTF-8 when the flag says so; checked then)
    uint16_t flags = 0;
    uint16_t method = 0;
    uint32_t crc = 0;
    uint64_t csize = 0;
    uint64_t usize = 0;
    uint32_t external = 0;
    uint64_t offset = 0;  // of the local header, a stub before the archive allowed for
    std::vector<uint8_t> extra;
    uint16_t dos_time = 0;
    uint16_t dos_date = 0;

    bool IsDir() const { return !name.empty() && name.back() == '/'; }
    // The Unix file type from the external attributes (S_IFMT), 0 when none is claimed.
    uint32_t UnixType() const { return (external >> 16) & 0xF000; }
    bool Utf8Flag() const { return (flags & 0x0800) != 0; }
};

struct OWZipWalk {
    std::string error;  // "" when the directory was read, else a refusal cause
    std::string message;
    std::vector<OWZipEntry> entries;
    bool zip64 = false;
};

namespace OWZip {

constexpr uint16_t FlagEncrypted = 0x0001;
constexpr uint16_t FlagStrong = 0x0040;
constexpr uint16_t FlagUtf8 = 0x0800;

// Walk the central directory. Never inflates anything.
OWZipWalk Walk(const uint8_t* bytes, size_t size);

// An entry's bytes, checked: never more than one byte past its declared size, its deflate stream ended exactly at its
// declared compressed size, its length and CRC-32 right. On a refusal, `reason` (too_big or not_openable) and
// `message` are set and false comes back. `largest` grows to the largest output buffer allocated.
bool Inflate(const uint8_t* zip, size_t size, const OWZipEntry& e, OWBytes& out, std::string& reason,
             std::string& message, uint64_t* largest = nullptr);

// CRC-32 (zlib's).
uint32_t Crc32(const uint8_t* data, size_t size);

// A zip writer: each entry whole, so its sizes and CRC-32 go in its local header (no data descriptor); every entry
// stamped 1980-01-01 00:00 by fixed numbers; method 0 or 8 (zlib, raw deflate, level 6); the UTF-8 flag on a
// non-ASCII name; made by MS-DOS, so no Unix mode is claimed. The zip64 end record when the entries reach 65,535 or an
// offset reaches 4 GiB.
class Writer {
public:
    // Add one entry, deflated when `compress` (an empty file is stored). False when zlib fails.
    bool Add(const std::string& name, const OWBytes& bytes, bool compress);
    // The archive's bytes, after the last Add.
    OWBytes Finish();

private:
    OWBytes out_;
    OWBytes central_;
    uint64_t count_ = 0;
};

}  // namespace OWZip
}  // namespace OnlyWorlds
