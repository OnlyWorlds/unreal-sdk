#include "OWId.h"

#include <array>
#include <chrono>
#include <cstring>
#include <random>
#include <vector>

namespace OnlyWorlds {
namespace OWId {

const char* const KeyedNamespace = "0199b6a0-6f0e-7c2a-9d1e-3b5c7a8e4f21";

namespace {

using Bytes16 = std::array<uint8_t, 16>;

std::string Format(const Bytes16& b) {
    static const char* const Hex = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    for (size_t i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out.push_back('-');
        out.push_back(Hex[b[i] >> 4]);
        out.push_back(Hex[b[i] & 15]);
    }
    return out;
}

// SHA-1 (FIPS 180-4), for the UUIDv5 alone: RFC 9562 names it, and no secret rides on it.
std::array<uint8_t, 20> Sha1(const std::vector<uint8_t>& data) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::vector<uint8_t> m = data;
    const uint64_t bits = static_cast<uint64_t>(data.size()) * 8;
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    for (int i = 7; i >= 0; --i) m.push_back(static_cast<uint8_t>(bits >> (8 * i)));
    auto rol = [](uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
    for (size_t chunk = 0; chunk < m.size(); chunk += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            const uint8_t* p = &m[chunk + 4 * i];
            w[i] = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
        }
        for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t next = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = next;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
    std::array<uint8_t, 20> out{};
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 4; ++j) out[4 * i + j] = static_cast<uint8_t>(h[i] >> (24 - 8 * j));
    return out;
}

uint8_t HexValue(char c) {
    if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
    return static_cast<uint8_t>(c - 'a' + 10);
}

}  // namespace

std::string NewId() {
    int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();
    return NewId(ms);
}

std::string NewId(int64_t unix_ms) {
    // The platform's random device, one per thread (on Windows the CRT's rand_s).
    thread_local std::random_device device;
    Bytes16 b{};
    for (size_t i = 0; i < 16; i += 4) {
        uint32_t r = device();
        std::memcpy(&b[i], &r, 4);
    }
    const uint64_t ms = static_cast<uint64_t>(unix_ms);
    for (int i = 0; i < 6; ++i) b[i] = static_cast<uint8_t>(ms >> (8 * (5 - i)));
    b[6] = static_cast<uint8_t>(0x70 | (b[6] & 0x0f));  // version 7
    b[8] = static_cast<uint8_t>(0x80 | (b[8] & 0x3f));  // variant 10
    return Format(b);
}

std::string Keyed(std::string_view idempotency_key, int64_t index) {
    if (idempotency_key.empty()) return "";
    std::vector<uint8_t> input;
    for (const char* p = KeyedNamespace; *p; ++p) {
        if (*p == '-') continue;
        uint8_t hi = HexValue(*p++);
        input.push_back(static_cast<uint8_t>((hi << 4) | HexValue(*p)));
    }
    std::string name = std::string(idempotency_key) + "\n" + std::to_string(index);
    input.insert(input.end(), name.begin(), name.end());
    std::array<uint8_t, 20> hash = Sha1(input);
    Bytes16 b{};
    std::memcpy(b.data(), hash.data(), 16);
    b[6] = static_cast<uint8_t>(0x50 | (b[6] & 0x0f));  // version 5
    b[8] = static_cast<uint8_t>(0x80 | (b[8] & 0x3f));  // variant 10
    return Format(b);
}

std::string Mint(std::string_view idempotency_key, int64_t index) {
    return idempotency_key.empty() ? NewId() : Keyed(idempotency_key, index);
}

}  // namespace OWId
}  // namespace OnlyWorlds
