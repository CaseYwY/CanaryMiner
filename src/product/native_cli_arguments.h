#pragma once

#include <algorithm>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#endif

namespace om {

#if defined(_WIN32)
struct NativeWideArguments {
    int count = 0;
    LPWSTR *values = CommandLineToArgvW(GetCommandLineW(), &count);
    NativeWideArguments() = default;
    NativeWideArguments(const NativeWideArguments &) = delete;
    NativeWideArguments &operator=(const NativeWideArguments &) = delete;
    ~NativeWideArguments() { if (values) LocalFree(values); }
};
#endif

inline bool nativeCliArguments(int argc, const char *const *argv,
                               std::initializer_list<std::pair<int, std::string_view>> expected) {
#if defined(_WIN32)
    (void)argv;
    // A best-fit conversion in the CRT must not turn other spellings into network options.
    const NativeWideArguments args;
    if (!args.values || args.count != argc) return false;
#else
    if (!argv) return false;
#endif
    for (const auto &[index, ascii] : expected) {
        if (index <= 0 || index >= argc) return false;
#if defined(_WIN32)
        const std::wstring_view actual(args.values[index]);
        if (!std::equal(ascii.begin(), ascii.end(), actual.begin(), actual.end())) return false;
#else
        if (std::string_view(argv[index]) != ascii) return false;
#endif
    }
    return true;
}

inline std::optional<std::string> nativeCliPathArgument(int argc, const char *const *argv, int index) {
    if (index <= 0 || index >= argc) return std::nullopt;
#if defined(_WIN32)
    (void)argv;
    const NativeWideArguments args;
    if (!args.values || args.count != argc || !args.values[index][0]) return std::nullopt;
    try {
        return std::filesystem::path(args.values[index]).u8string();
    } catch (const std::filesystem::filesystem_error &) {
        return std::nullopt;
    }
#else
    if (!argv || !argv[index] || !argv[index][0]) return std::nullopt;
    return std::string(argv[index]);
#endif
}

}  // namespace om
