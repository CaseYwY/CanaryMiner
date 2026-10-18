// Pearl job `extra` encoding and strict 256-bit decimal parsing, shared by the pool client and the proof path.
//
// Split out of the retired gateway client: only these two helpers are used by the pool protocol. The `extra` format
// ("<152 hex chars><colon><cert version>") is local bookkeeping inside Job; it never appears on the wire.

#pragma once

#include <stdint.h>
#include <string.h>

#include <string>

#include "algo.h"
#include "json_lite.h"
#include "http.h"
#include "platform.h"
#include "run.h"
#include "../algos/pearl-pow/job.h"
#include "../algos/pearl-pow/solution_identity.h"

namespace om {

/**
 * Encodes the pieces of a Pearl job that do not fit Job's fixed fields.
 *
 * Job carries a 32-byte message and a 256-bit target because that covers every
 * other PoW here; Pearl's header stub is 76 bytes and the certificate version
 * has nowhere to live at all. Both ride in `extra`, which the core treats as
 * opaque, as "<152 hex chars><colon><cert version>".
 */
inline std::string packPearlExtra(const uint8_t header[76], int certVersion) {
    static const char *d = "0123456789abcdef";
    std::string s;
    s.reserve(160);
    for (int i = 0; i < 76; i++) {
        s += d[header[i] >> 4];
        s += d[header[i] & 15];
    }
    s += ':';
    s += std::to_string(certVersion);
    return s;
}

inline bool unpackPearlExtra(const std::string &extra, uint8_t header[76],
                             int *certVersion) noexcept {
    if (!header || !certVersion || extra.size() < 154 || extra[152] != ':' ||
        extra[153] < '1' || extra[153] > '3' ||
        (extra.size() != 154 && extra[154] != ':'))
        return false;

    uint8_t parsed[76];
    for (size_t i = 0; i < sizeof(parsed); ++i) {
        auto nibble = [](unsigned char ch, uint8_t *out) noexcept {
            if (ch >= '0' && ch <= '9') {
                *out = static_cast<uint8_t>(ch - '0');
                return true;
            }
            if (ch >= 'a' && ch <= 'f') {
                *out = static_cast<uint8_t>(ch - 'a' + 10);
                return true;
            }
            if (ch >= 'A' && ch <= 'F') {
                *out = static_cast<uint8_t>(ch - 'A' + 10);
                return true;
            }
            return false;
        };
        uint8_t hi = 0, lo = 0;
        if (!nibble(static_cast<unsigned char>(extra[2 * i]), &hi) ||
            !nibble(static_cast<unsigned char>(extra[2 * i + 1]), &lo))
            return false;
        parsed[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    memcpy(header, parsed, sizeof(parsed));
    *certVersion = extra[153] - '0';
    return true;
}

/** Parse one canonical, unsigned JSON integer into 256-bit limbs.
 *
 * `decimalToLimbs` predates hostile protocol-boundary use: it skips non-digits
 * and discards carry beyond 256 bits. Pool and job parsing must instead reject the
 * whole value when it is fractional, quoted, negative, malformed or too wide.
 */
inline bool pearlDecimalU256(const std::string &dec,
                                    uint64_t out[4]) noexcept {
    if (!out || dec.empty() || (dec.size() > 1 && dec[0] == '0')) return false;
    uint64_t parsed[4] = {};
    for (char ch : dec) {
        if (ch < '0' || ch > '9') return false;
        uint64_t carry = (uint64_t)(ch - '0');
        for (int i = 0; i < 4; ++i) {
            uint64_t hi = 0;
            parsed[i] = mulAdd64(parsed[i], 10ULL, carry, &hi);
            carry = hi;
        }
        if (carry) return false;
    }
    memcpy(out, parsed, sizeof(parsed));
    return true;
}

}  // namespace om
