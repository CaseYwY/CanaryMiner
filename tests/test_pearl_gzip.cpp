// H-019 task 3/4: the self-contained gzip encoder round-trips through an
// independent fixed-Huffman inflate, matches the standard CRC-32 check value,
// and is deterministic.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/pearl_gzip.h"

namespace {

int failures = 0;

void check(bool condition, const char *name) {
    std::printf("  %-68s %s\n", name, condition ? "ok" : "FAIL");
    if (!condition) ++failures;
}

/** Minimal RFC 1951 reader for the fixed-Huffman blocks the encoder emits.
 * Returns false on anything else, so a wrong block type is a test failure. */
class Inflate {
   public:
    Inflate(const uint8_t *data, size_t size) : data_(data), size_(size) {}

    bool run(std::string *out) {
        for (;;) {
            const uint32_t final = bit();
            const uint32_t type = bits(2);
            if (type == 0) {
                if (!stored(out)) return false;
            } else if (type == 1) {
                if (!fixedBlock(out)) return false;
            } else {
                return false;  // dynamic Huffman is not produced by the encoder under test
            }
            if (final) return true;
        }
    }

    bool stored(std::string *out) {
        pos_ = (pos_ + 7) & ~size_t(7);  // stored data starts on a byte boundary
        if (pos_ / 8 + 4 > size_) return false;
        const uint32_t len = data_[pos_ / 8] | (uint32_t(data_[pos_ / 8 + 1]) << 8);
        const uint32_t nlen = data_[pos_ / 8 + 2] | (uint32_t(data_[pos_ / 8 + 3]) << 8);
        if ((len ^ 0xFFFFu) != nlen) return false;
        pos_ += 32;
        if (pos_ / 8 + len > size_) return false;
        out->append(reinterpret_cast<const char *>(data_ + pos_ / 8), len);
        pos_ += 8 * size_t(len);
        return true;
    }

    bool fixedBlock(std::string *out) {
        for (;;) {
            const uint32_t sym = fixedSymbol();
            if (sym == 0xFFFFFFFFu) return false;
            if (sym < 256) {
                out->push_back(static_cast<char>(sym));
            } else if (sym == 256) {
                return true;
            } else {
                const uint32_t lc = sym - 257;
                if (lc >= 29) return false;
                const uint32_t length = pearl_gzip::detail::kLenBase[lc] + bits(pearl_gzip::detail::kLenExtra[lc]);
                const uint32_t dc = bitsMsb(5);
                if (dc >= 30) return false;
                const uint32_t dist = pearl_gzip::detail::kDistBase[dc] + bits(pearl_gzip::detail::kDistExtra[dc]);
                if (dist == 0 || dist > out->size()) return false;
                for (uint32_t i = 0; i < length; ++i) out->push_back((*out)[out->size() - dist]);
            }
        }
    }


   private:
    uint32_t bit() {
        if (pos_ >= size_ * 8) {
            overrun_ = true;
            return 0;
        }
        const uint32_t b = (data_[pos_ >> 3] >> (pos_ & 7)) & 1u;
        ++pos_;
        return b;
    }
    uint32_t bits(int count) {  // LSB first
        uint32_t v = 0;
        for (int i = 0; i < count; ++i) v |= bit() << i;
        return v;
    }
    uint32_t bitsMsb(int count) {  // Huffman codes, MSB first
        uint32_t v = 0;
        for (int i = 0; i < count; ++i) v = (v << 1) | bit();
        return v;
    }
    uint32_t fixedSymbol() {
        uint32_t code = bitsMsb(7);
        if (code <= 0x17) return 256 + code;
        code = (code << 1) | bit();
        if (code >= 0x30 && code <= 0xBF) return code - 0x30;
        if (code >= 0xC0 && code <= 0xC7) return 280 + (code - 0xC0);
        code = (code << 1) | bit();
        if (code >= 0x190 && code <= 0x1FF) return 144 + (code - 0x190);
        return 0xFFFFFFFFu;
    }

    const uint8_t *data_;
    size_t size_;
    size_t pos_ = 0;
    bool overrun_ = false;

   public:
    bool overrun() const { return overrun_; }
};

uint32_t le32(const std::string &s, size_t at) {
    return uint32_t(uint8_t(s[at])) | (uint32_t(uint8_t(s[at + 1])) << 8) |
           (uint32_t(uint8_t(s[at + 2])) << 16) | (uint32_t(uint8_t(s[at + 3])) << 24);
}

/** Decode a whole gzip member and verify header, CRC-32 and ISIZE. */
bool gunzipCheck(const std::string &gz, const std::string &original) {
    if (gz.size() < 18) return false;
    if (uint8_t(gz[0]) != 0x1F || uint8_t(gz[1]) != 0x8B || uint8_t(gz[2]) != 0x08) return false;
    const size_t body = 10;
    const size_t trailerAt = gz.size() - 8;
    Inflate in(reinterpret_cast<const uint8_t *>(gz.data()) + body, trailerAt - body);
    std::string out;
    if (!in.run(&out) || in.overrun()) return false;
    return out == original && le32(gz, trailerAt) == pearl_gzip::crc32(
               reinterpret_cast<const uint8_t *>(original.data()), original.size()) &&
           le32(gz, trailerAt + 4) == static_cast<uint32_t>(original.size());
}

std::string lcg(size_t n, uint32_t seed) {
    std::string s(n, '\0');
    for (size_t i = 0; i < n; ++i) {
        seed = seed * 1664525u + 1013904223u;
        s[i] = static_cast<char>(seed >> 24);
    }
    return s;
}

}  // namespace

int main(int argc, char **argv) {
    std::puts("self-contained gzip for the v2 submit dialect:");
    const std::string check9 = "123456789";
    check(pearl_gzip::crc32(reinterpret_cast<const uint8_t *>(check9.data()), check9.size()) == 0xCBF43926u,
          "CRC-32 matches the standard check value");

    const std::string empty;
    check(gunzipCheck(pearl_gzip::gzipCompress(empty), empty), "empty input round-trips");

    const std::string one = "a";
    check(gunzipCheck(pearl_gzip::gzipCompress(one), one), "one byte round-trips");

    std::string repetitive;
    for (int i = 0; i < 4000; ++i) repetitive += "ZERO-SEEDED-PROOF-BLOCK-";
    const std::string gz = pearl_gzip::gzipCompress(repetitive);
    check(gunzipCheck(gz, repetitive), "repetitive input round-trips");
    check(gz.size() * 10 < repetitive.size(), "repetitive input compresses by more than 10x");

    const std::string random = lcg(60000, 0x5eed);
    const std::string randomGz = pearl_gzip::gzipCompress(random);
    check(gunzipCheck(randomGz, random), "incompressible input round-trips");
    check(randomGz.size() <= random.size() + 18 + 5 * ((random.size() + 65534) / 65535),
          "incompressible input expands only by stored-block framing");

    const std::string longRun(70000, 'x');
    check(gunzipCheck(pearl_gzip::gzipCompress(longRun), longRun), "matches longer than one window round-trip");

    check(pearl_gzip::gzipCompress(repetitive) == gz, "output is deterministic");
    check(gz.compare(0, 10, std::string("\x1F\x8B\x08\0\0\0\0\0\0\xFF", 10)) == 0, "fixed 10-byte gzip header");

    // With two paths, also write the gzip stream and its raw input for the Python cross-check (tests/test_pearl_gzip.py).
    if (argc >= 3) {
        FILE *gzOut = std::fopen(argv[1], "wb");
        FILE *rawOut = std::fopen(argv[2], "wb");
        if (!gzOut || !rawOut) {
            std::puts("  cannot write the Python cross-check files                           FAIL");
            ++failures;
        } else {
            std::fwrite(gz.data(), 1, gz.size(), gzOut);
            std::fwrite(repetitive.data(), 1, repetitive.size(), rawOut);
        }
        if (gzOut) std::fclose(gzOut);
        if (rawOut) std::fclose(rawOut);
    }
    std::printf("\nself-contained gzip: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
