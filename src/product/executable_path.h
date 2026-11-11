// Actual module identity only; never argv, environment or a current-directory helper search.
#pragma once
#include "../core/platform.h" // winsock2.h must precede windows.h
#include <filesystem>
#include <string>
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif
namespace om {
inline std::string selfExecutablePath() {
#if defined(_WIN32)
    wchar_t buf[32768];
    const DWORD n = GetModuleFileNameW(nullptr, buf, 32768);
    return n == 0 || n >= 32768 ? std::string() : std::filesystem::path(std::wstring(buf, n)).u8string();
#else
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    return n <= 0 ? std::string() : std::string(buf, static_cast<size_t>(n));
#endif
}
} // namespace om
