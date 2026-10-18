#pragma once
// Self-contained gzip (RFC 1952) around one fixed-Huffman DEFLATE block (RFC 1951).
// H-019 task 3: the Kryptex/F2Pool v2 dialect sends gzip(plain_proof). The build
// host has no zlib headers, and installing a system package is outside this task,
// so the encoder lives here. It is deterministic (mtime 0, no name) and is
// round-trip tested by tests/test_pearl_gzip.cpp.
#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace pearl_gzip {

inline uint32_t crc32(const uint8_t *data, size_t n) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

namespace detail {

// RFC 1951 length and distance code tables.
inline const uint16_t kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                      31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
inline const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                      2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
inline const uint16_t kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,
                                       33,  49,  65,  97,  129, 193,  257,  385,  513,  769,
                                       1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
inline const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                       6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

/** LSB-first bit writer, as DEFLATE requires. */
class BitWriter {
   public:
    std::string out;

    void put(uint32_t value, int count) {
        acc_ |= (value & ((1u << count) - 1u)) << nbits_;
        nbits_ += count;
        while (nbits_ >= 8) {
            out.push_back(static_cast<char>(acc_ & 0xFFu));
            acc_ >>= 8;
            nbits_ -= 8;
        }
    }

    /** Huffman codes are packed most-significant bit first. */
    void putCode(uint32_t code, int length) {
        uint32_t reversed = 0;
        for (int i = 0; i < length; ++i) reversed |= ((code >> i) & 1u) << (length - 1 - i);
        put(reversed, length);
    }

    void flush() {
        if (nbits_) out.push_back(static_cast<char>(acc_ & 0xFFu));
        acc_ = 0;
        nbits_ = 0;
    }

   private:
    uint32_t acc_ = 0;
    int nbits_ = 0;
};

inline void putLiteral(BitWriter &w, uint32_t sym) {
    if (sym < 144) w.putCode(0x30 + sym, 8);
    else if (sym < 256) w.putCode(0x190 + (sym - 144), 9);
    else if (sym < 280) w.putCode(sym - 256, 7);
    else w.putCode(0xC0 + (sym - 280), 8);
}

inline void putMatch(BitWriter &w, uint32_t length, uint32_t distance) {
    int code = 28;
    while (kLenBase[code] > length) --code;
    putLiteral(w, 257 + static_cast<uint32_t>(code));
    w.put(length - kLenBase[code], kLenExtra[code]);
    int dcode = 29;
    while (kDistBase[dcode] > distance) --dcode;
    w.putCode(static_cast<uint32_t>(dcode), 5);
    w.put(distance - kDistBase[dcode], kDistExtra[dcode]);
}

inline uint32_t hash3(const uint8_t *p) {
    return (((uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | uint32_t(p[2])) * 2654435761u) >> 17;
}

/** One final fixed-Huffman block with greedy LZ77 over a 32 KiB window. */
inline std::string deflateFixed(const uint8_t *s, size_t n) {
    BitWriter w;
    w.put(1, 1);  // BFINAL
    w.put(1, 2);  // BTYPE = 01, fixed Huffman
    std::vector<int64_t> head(1u << 15, -1), prev(n, -1);
    size_t i = 0;
    while (i < n) {
        size_t bestLen = 0, bestDist = 0;
        if (i + 2 < n) {
            const uint32_t h = hash3(s + i);
            int64_t cand = head[h];
            const size_t maxLen = n - i < 258 ? n - i : 258;
            for (int chain = 0; cand >= 0 && chain < 64 && i - size_t(cand) <= 32768; ++chain) {
                size_t l = 0;
                while (l < maxLen && s[size_t(cand) + l] == s[i + l]) ++l;
                if (l > bestLen) {
                    bestLen = l;
                    bestDist = i - size_t(cand);
                    if (l == maxLen) break;
                }
                cand = prev[size_t(cand)];
            }
            prev[i] = head[h];
            head[h] = int64_t(i);
        }
        if (bestLen >= 3) {
            putMatch(w, uint32_t(bestLen), uint32_t(bestDist));
            for (size_t k = 1; k < bestLen; ++k) {
                const size_t j = i + k;
                if (j + 2 < n) {
                    const uint32_t h = hash3(s + j);
                    prev[j] = head[h];
                    head[h] = int64_t(j);
                }
            }
            i += bestLen;
        } else {
            putLiteral(w, s[i]);
            ++i;
        }
    }
    putLiteral(w, 256);  // end of block
    w.flush();
    return w.out;
}

/** Stored (BTYPE 00) blocks: the fallback when Huffman coding would expand the input. */
inline std::string deflateStored(const uint8_t *s, size_t n) {
    std::string out;
    size_t pos = 0;
    do {
        const size_t len = n - pos < 65535 ? n - pos : 65535;
        const bool last = pos + len == n;
        out.push_back(last ? 1 : 0);  // BFINAL, BTYPE 00, then padded to a byte boundary
        out.push_back(static_cast<char>(len & 0xFFu));
        out.push_back(static_cast<char>(len >> 8));
        out.push_back(static_cast<char>(~len & 0xFFu));
        out.push_back(static_cast<char>((~len >> 8) & 0xFFu));
        out.append(reinterpret_cast<const char *>(s + pos), len);
        pos += len;
    } while (pos < n);
    return out;
}

/** The smaller of fixed-Huffman LZ77 and stored blocks. */
inline std::string deflateBest(const uint8_t *s, size_t n) {
    std::string fixed = deflateFixed(s, n);
    std::string stored = deflateStored(s, n);
    return fixed.size() <= stored.size() ? fixed : stored;
}

}  // namespace detail

/** gzip container of `input`: 10-byte header, DEFLATE data, CRC32 and ISIZE. */
inline std::string gzipCompress(const std::string &input) {
    const uint8_t *data = reinterpret_cast<const uint8_t *>(input.data());
    std::string out;
    const uint8_t header[10] = {0x1F, 0x8B, 0x08, 0x00, 0, 0, 0, 0, 0x00, 0xFF};
    out.append(reinterpret_cast<const char *>(header), sizeof(header));
    out += detail::deflateBest(data, input.size());
    const uint32_t crc = crc32(data, input.size());
    const uint32_t size = static_cast<uint32_t>(input.size());
    for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<char>((crc >> shift) & 0xFFu));
    for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<char>((size >> shift) & 0xFFu));
    return out;
}

}  // namespace pearl_gzip
