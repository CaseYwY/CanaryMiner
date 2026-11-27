#pragma once

#include <algorithm>
#include <initializer_list>
#include <string_view>
#include <utility>
#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#endif

namespace om {

inline bool nativeCliArguments(int argc, const char *const *argv,
                               std::initializer_list<std::pair<int, std::string_view>> expected) {
#if defined(_WIN32)
    (void)argv;
    // A best-fit conversion in the CRT must not turn other spellings into network options.
    struct WideArguments {
        LPWSTR *values;
        ~WideArguments() { if (values) LocalFree(values); }
    };
    int count = 0;
    const WideArguments args{CommandLineToArgvW(GetCommandLineW(), &count)};
    if (!args.values || count != argc) return false;
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

}  // namespace om
