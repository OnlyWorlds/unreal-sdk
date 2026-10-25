#include "OWZip.h"
#include "OWUnicode.h"

#include <algorithm>
#include <climits>
#include <zlib.h>

namespace OnlyWorlds {
namespace OWZip {

namespace {

constexpr uint32_t SigLocal = 0x04034b50;
constexpr uint32_t SigCentral = 0x02014b50;
constexpr uint32_t SigEnd = 0x06054b50;
constexpr uint32_t SigEnd64 = 0x06064b50;
constexpr uint32_t SigLoc64 = 0x07064b50;
constexpr uint64_t Max32 = 0xFFFFFFFFull;

constexpr uint16_t DosTime = 0;                              // 00:00:00
constexpr uint16_t DosDate = (0 << 9) | (1 << 5) | 1;        // 1980-01-01: fixed numbers, so no time zone can shift them

uint16_t U16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t U32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
uint64_t U64(const uint8_t* p) { return U32(p) | (static_cast<uint64_t>(U32(p + 4)) << 32); }

void Put16(OWBytes& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v));
    b.push_back(static_cast<uint8_t>(v >> 8));
}
void Put32(OWBytes& b, uint32_t v) {
    for (int k = 0; k < 4; ++k) b.push_back(static_cast<uint8_t>(v >> (8 * k)));
}
void Put64(OWBytes& b, uint64_t v) {
    for (int k = 0; k < 8; ++k) b.push_back(static_cast<uint8_t>(v >> (8 * k)));
}

bool Fail(OWZipWalk& w, const char* reason, std::string message) {
    w.error = reason;
    w.message = std::move(message);
    w.entries.clear();
    return false;
}

std::string Hex(const std::string& raw) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (unsigned char c : raw) {
        s.push_back(d[c >> 4]);
        s.push_back(d[c & 15]);
    }
    return s;
}

// The zip64 extended information extra field: the true sizes and offset where the record says 0xFFFFFFFF.
std::string Zip64Extra(OWZipEntry& e) {
    const std::vector<uint8_t>& x = e.extra;
    size_t q = 0;
    while (q + 4 <= x.size()) {
        uint16_t tag = U16(&x[q]);
        uint16_t len = U16(&x[q + 2]);
        if (q + 4 + len > x.size()) return "a corrupt extra field on " + e.name;
        if (tag == 0x0001) {
            size_t r = q + 4, end = q + 4 + len;
            if (e.usize == Max32) {
                if (r + 8 > end) return "a corrupt zip64 field on " + e.name;
                e.usize = U64(&x[r]);
                r += 8;
            }
            if (e.csize == Max32) {
                if (r + 8 > end) return "a corrupt zip64 field on " + e.name;
                e.csize = U64(&x[r]);
                r += 8;
            }
            if (e.offset == Max32) {
                if (r + 8 > end) return "a corrupt zip64 field on " + e.name;
                e.offset = U64(&x[r]);
                r += 8;
            }
        }
        q += 4 + len;
    }
    return "";
}

bool Refuse(std::string& reason, std::string& message, const char* why, std::string text) {
    reason = why;
    message = std::move(text);
    return false;
}

}  // namespace

uint32_t Crc32(const uint8_t* data, size_t size) {
    return static_cast<uint32_t>(crc32_z(0, data, size));
}

OWZipWalk Walk(const uint8_t* b, size_t n) {
    OWZipWalk w;
    // The end record: its signature within the last 22 + 65,535 bytes (a comment may follow it).
    int64_t eocd = -1;
    if (n >= 22) {
        int64_t floor_at = std::max<int64_t>(0, static_cast<int64_t>(n) - 65557);
        for (int64_t i = static_cast<int64_t>(n) - 22; i >= floor_at; --i) {
            if (b[i] == 0x50 && b[i + 1] == 0x4b && b[i + 2] == 0x05 && b[i + 3] == 0x06) {
                eocd = i;
                break;
            }
        }
    }
    if (eocd < 0) {
        Fail(w, "not_openable", "not a zip: no end of central directory record");
        return w;
    }
    uint32_t this_disk = U16(b + eocd + 4);
    uint32_t cd_disk = U16(b + eocd + 6);
    uint64_t cd_size = U32(b + eocd + 12);
    uint64_t cd_offset = U32(b + eocd + 16);
    int64_t end_at = eocd;
    if (eocd >= 20 && U32(b + eocd - 20) == SigLoc64) {
        int64_t loc = eocd - 20;
        uint32_t disk64 = U32(b + loc + 4);
        uint32_t disks = U32(b + loc + 16);
        if (disk64 != 0 || disks > 1) {
            Fail(w, "not_openable", "a zip64 archive spanning several disks");
            return w;
        }
        int64_t rec = loc - 56;
        if (rec < 0 || U32(b + rec) != SigEnd64) {
            Fail(w, "not_openable", "a zip64 locator without its end record");
            return w;
        }
        w.zip64 = true;
        this_disk = U32(b + rec + 16);
        cd_disk = U32(b + rec + 20);
        cd_size = U64(b + rec + 40);
        cd_offset = U64(b + rec + 48);
        end_at = rec;
    }
    if (!((this_disk == 0 && cd_disk == 0) || (this_disk == 0xFFFF && cd_disk == 0xFFFF))) {
        Fail(w, "not_openable", "its end record says disk " + std::to_string(this_disk) + ", the directory on disk " +
                                    std::to_string(cd_disk) + ": a spanned archive");
        return w;
    }
    // Bytes before the archive (a stub); 0 for a plain zip.
    int64_t concat = end_at - static_cast<int64_t>(cd_size) - static_cast<int64_t>(cd_offset);
    if (cd_size > n || cd_offset > n || concat < 0) {
        Fail(w, "not_openable", "the central directory's size and offset do not fit the file");
        return w;
    }
    uint64_t p = cd_offset + concat;
    uint64_t stop = p + cd_size;
    if (stop > n) {
        Fail(w, "not_openable", "the central directory runs past the end of the file");
        return w;
    }
    while (p < stop) {
        if (p + 46 > n || U32(b + p) != SigCentral) {
            Fail(w, "not_openable", "a bad record in the central directory at byte " + std::to_string(p));
            return w;
        }
        OWZipEntry e;
        e.flags = U16(b + p + 8);
        e.method = U16(b + p + 10);
        e.dos_time = U16(b + p + 12);
        e.dos_date = U16(b + p + 14);
        e.crc = U32(b + p + 16);
        e.csize = U32(b + p + 20);
        e.usize = U32(b + p + 24);
        uint16_t nlen = U16(b + p + 28), xlen = U16(b + p + 30), clen = U16(b + p + 32);
        e.external = U32(b + p + 38);
        e.offset = U32(b + p + 42);
        if (p + 46 + nlen + xlen + clen > n) {
            Fail(w, "not_openable", "a directory record runs past the end of the file");
            return w;
        }
        e.name.assign(reinterpret_cast<const char*>(b + p + 46), nlen);
        e.extra.assign(b + p + 46 + nlen, b + p + 46 + nlen + xlen);
        if ((e.flags & FlagUtf8) && !OWUnicode::IsUtf8(e.name)) {
            Fail(w, "name", "an entry flagged UTF-8 is not UTF-8 (bytes " + Hex(e.name) + ")");
            return w;
        }
        std::string err = Zip64Extra(e);
        if (!err.empty()) {
            Fail(w, "not_openable", err);
            return w;
        }
        e.offset += concat;
        w.entries.push_back(std::move(e));
        p += 46ull + nlen + xlen + clen;
    }
    return w;
}

bool Inflate(const uint8_t* b, size_t n, const OWZipEntry& e, OWBytes& out, std::string& reason, std::string& message,
             uint64_t* largest) {
    out.clear();
    // The entry's stored bytes, located through its local header.
    uint64_t off = e.offset;
    if (off + 30 > n || U32(b + off) != SigLocal)
        return Refuse(reason, message, "not_openable", e.name + ": no local header at its offset");
    uint64_t start = off + 30 + U16(b + off + 26) + U16(b + off + 28);
    if (start > n) return Refuse(reason, message, "not_openable", e.name + ": its local header runs past the file");
    uint64_t have = std::min<uint64_t>(e.csize, n - start);  // the payload, cut at the file's end
    const uint8_t* payload = b + start;
    uint64_t declared = e.usize;

    if (e.method == 0) {
        if (have > declared)
            return Refuse(reason, message, "too_big", e.name + " holds " + std::to_string(have) + " bytes, past the " +
                                                          std::to_string(declared) + " it declares");
        out.assign(payload, payload + have);
        if (largest) *largest = std::max<uint64_t>(*largest, have);
    } else {
        // One inflate stream over exactly the declared compressed bytes, into a buffer one byte past the declared
        // size, and never past what those bytes could hold (deflate gives at most 1,032 bytes a byte: a 258-byte
        // match in two bits), so a lying size never makes a large allocation the archive's bytes cannot fill.
        uint64_t cap = std::min<uint64_t>(declared + 1, have * 1032 + 1032);
        if (cap > UINT_MAX)
            return Refuse(reason, message, "too_big", e.name + " declares more than one inflate can hold");
        if (largest) *largest = std::max<uint64_t>(*largest, cap);
        out.resize(static_cast<size_t>(cap));
        z_stream zs{};
        if (inflateInit2(&zs, -15) != Z_OK)
            return Refuse(reason, message, "not_openable", e.name + ": zlib would not start");
        zs.next_out = out.data();
        zs.avail_out = static_cast<uInt>(cap);
        uint64_t in_left = have;
        const uint8_t* in = payload;
        int ret;
        do {
            if (zs.avail_in == 0 && in_left > 0) {
                uInt chunk = static_cast<uInt>(std::min<uint64_t>(in_left, 1u << 30));
                zs.next_in = const_cast<Bytef*>(in);
                zs.avail_in = chunk;
                in += chunk;
                in_left -= chunk;
            }
            ret = inflate(&zs, Z_NO_FLUSH);
        } while (ret == Z_OK && zs.avail_out > 0 && (zs.avail_in > 0 || in_left > 0));
        uint64_t produced = cap - zs.avail_out;
        uint64_t unused = zs.avail_in + in_left;
        std::string zmsg = zs.msg ? zs.msg : "zlib " + std::to_string(ret);
        inflateEnd(&zs);
        out.resize(static_cast<size_t>(produced));
        if (produced > declared)
            return Refuse(reason, message, "too_big",
                          e.name + " inflates past the " + std::to_string(declared) + " bytes it declares");
        if (ret == Z_DATA_ERROR || ret == Z_MEM_ERROR || ret == Z_STREAM_ERROR || ret == Z_NEED_DICT)
            return Refuse(reason, message, "not_openable",
                          e.name + ": deflate stream broken (" + zmsg + ")");
        if (ret != Z_STREAM_END)  // the stream never reaches its final block's end within the declared compressed size
            return Refuse(reason, message, "not_openable", e.name + ": deflate stream ends early");
        if (unused > 0)  // it ends before the declared compressed size: the bytes after it are no part of the entry
            return Refuse(reason, message, "not_openable",
                          e.name + ": deflate stream ends " + std::to_string(unused) +
                              " bytes before its declared compressed size");
    }
    if (out.size() != declared)
        return Refuse(reason, message, "not_openable", e.name + " holds " + std::to_string(out.size()) +
                                                           " bytes, declares " + std::to_string(declared));
    if (Crc32(out.data(), out.size()) != e.crc) return Refuse(reason, message, "not_openable", e.name + " fails its CRC");
    return true;
}

// -- the writer ---------------------------------------------------------------------------------------------------------

bool Writer::Add(const std::string& name, const OWBytes& bytes, bool compress) {
    bool utf8 = !OWUnicode::IsAscii(name);
    uint16_t flags = utf8 ? FlagUtf8 : 0;
    uint16_t method = 0;
    uint32_t crc = Crc32(bytes.data(), bytes.size());
    OWBytes packed;
    const OWBytes* data = &bytes;
    if (bytes.size() >= UINT_MAX / 2 || name.size() > 0xFFFF) return false;
    if (compress && !bytes.empty()) {
        z_stream zs{};
        if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) return false;
        packed.resize(deflateBound(&zs, static_cast<uLong>(bytes.size())));
        zs.next_in = const_cast<Bytef*>(bytes.data());
        zs.avail_in = static_cast<uInt>(bytes.size());
        zs.next_out = packed.data();
        zs.avail_out = static_cast<uInt>(packed.size());
        int ret = deflate(&zs, Z_FINISH);
        size_t produced = packed.size() - zs.avail_out;
        deflateEnd(&zs);
        if (ret != Z_STREAM_END) return false;
        packed.resize(produced);
        data = &packed;
        method = 8;
    }
    uint64_t offset = out_.size();
    bool big = offset >= Max32;
    uint16_t nlen = static_cast<uint16_t>(name.size());

    Put32(out_, SigLocal);
    Put16(out_, 20);
    Put16(out_, flags);
    Put16(out_, method);
    Put16(out_, DosTime);
    Put16(out_, DosDate);
    Put32(out_, crc);
    Put32(out_, static_cast<uint32_t>(data->size()));
    Put32(out_, static_cast<uint32_t>(bytes.size()));
    Put16(out_, nlen);
    Put16(out_, 0);
    out_.insert(out_.end(), name.begin(), name.end());
    out_.insert(out_.end(), data->begin(), data->end());

    Put32(central_, SigCentral);
    Put16(central_, big ? 45 : 20);  // made by: MS-DOS host
    Put16(central_, big ? 45 : 20);
    Put16(central_, flags);
    Put16(central_, method);
    Put16(central_, DosTime);
    Put16(central_, DosDate);
    Put32(central_, crc);
    Put32(central_, static_cast<uint32_t>(data->size()));
    Put32(central_, static_cast<uint32_t>(bytes.size()));
    Put16(central_, nlen);
    Put16(central_, big ? 12 : 0);
    Put16(central_, 0);  // comment
    Put16(central_, 0);  // disk
    Put16(central_, 0);  // internal attributes
    Put32(central_, 0);  // external attributes
    Put32(central_, big ? static_cast<uint32_t>(Max32) : static_cast<uint32_t>(offset));
    central_.insert(central_.end(), name.begin(), name.end());
    if (big) {
        Put16(central_, 0x0001);
        Put16(central_, 8);
        Put64(central_, offset);
    }
    ++count_;
    return true;
}

OWBytes Writer::Finish() {
    uint64_t start = out_.size();
    uint64_t size = central_.size();
    out_.insert(out_.end(), central_.begin(), central_.end());
    uint64_t end_at = start + size;
    bool zip64 = count_ >= 0xFFFF || start >= Max32 || size >= Max32 || end_at >= Max32;
    if (zip64) {
        Put32(out_, SigEnd64);
        Put64(out_, 44);
        Put16(out_, 45);
        Put16(out_, 45);
        Put32(out_, 0);
        Put32(out_, 0);
        Put64(out_, count_);
        Put64(out_, count_);
        Put64(out_, size);
        Put64(out_, start);
        Put32(out_, SigLoc64);
        Put32(out_, 0);
        Put64(out_, end_at);
        Put32(out_, 1);
    }
    Put32(out_, SigEnd);
    Put16(out_, 0);
    Put16(out_, 0);
    Put16(out_, static_cast<uint16_t>(std::min<uint64_t>(count_, 0xFFFF)));
    Put16(out_, static_cast<uint16_t>(std::min<uint64_t>(count_, 0xFFFF)));
    Put32(out_, static_cast<uint32_t>(std::min<uint64_t>(size, Max32)));
    Put32(out_, static_cast<uint32_t>(std::min<uint64_t>(start, Max32)));
    Put16(out_, 0);
    OWBytes done;
    done.swap(out_);
    central_.clear();
    count_ = 0;
    return done;
}

}  // namespace OWZip
}  // namespace OnlyWorlds
