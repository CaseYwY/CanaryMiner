#pragma once

#include <cstddef>
#include <cstdio>
#include <string>

namespace om {

/** Format a CUDA/Vulkan 16-byte device UUID the way NVML names a GPU. */
inline std::string nvidiaGpuUuid(const unsigned char *bytes, size_t size) {
    if (!bytes || size != 16) return {};
    char out[41];
    const int n = std::snprintf(
        out, sizeof(out),
        "GPU-%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
        "%02x%02x%02x%02x%02x%02x",
        static_cast<unsigned>(bytes[0]), static_cast<unsigned>(bytes[1]),
        static_cast<unsigned>(bytes[2]), static_cast<unsigned>(bytes[3]),
        static_cast<unsigned>(bytes[4]), static_cast<unsigned>(bytes[5]),
        static_cast<unsigned>(bytes[6]), static_cast<unsigned>(bytes[7]),
        static_cast<unsigned>(bytes[8]), static_cast<unsigned>(bytes[9]),
        static_cast<unsigned>(bytes[10]), static_cast<unsigned>(bytes[11]),
        static_cast<unsigned>(bytes[12]), static_cast<unsigned>(bytes[13]),
        static_cast<unsigned>(bytes[14]), static_cast<unsigned>(bytes[15]));
    return n == 40 ? std::string(out) : std::string();
}

inline bool gpuUuidEqual(const std::string &left, const std::string &right) {
    if (left.size() != right.size() || left.empty()) return false;
    for (size_t i = 0; i < left.size(); ++i) {
        unsigned char a = static_cast<unsigned char>(left[i]);
        unsigned char b = static_cast<unsigned char>(right[i]);
        if (a >= 'A' && a <= 'Z')
            a = static_cast<unsigned char>(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z')
            b = static_cast<unsigned char>(b + ('a' - 'A'));
        if (a != b) return false;
    }
    return true;
}

}  // namespace om
