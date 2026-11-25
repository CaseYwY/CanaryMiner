#pragma once
#include "local_preferences_store.h"
#include <optional>

namespace om {

inline std::optional<std::filesystem::path> localReviewPath(const std::filesystem::path &filename) {
    std::error_code ec;
    const auto path = std::filesystem::absolute(filename, ec);
    if (ec) return std::nullopt;
#if defined(_WIN32)
    const auto root = path.root_name().native();
    if (root.size() != 2 || root[1] != L':' || path.native().find(L':', 2) != std::wstring::npos)
        return std::nullopt;
    const auto drive = GetDriveTypeW(path.root_path().c_str());
    if (drive != DRIVE_FIXED && drive != DRIVE_REMOVABLE && drive != DRIVE_RAMDISK) return std::nullopt;
    auto parent = path.root_path();
    const auto directories = path.parent_path().relative_path();
    for (const auto &component : directories) {
        parent /= component;
        const auto attributes = GetFileAttributesW(parent.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return std::nullopt;
    }
#endif
    return path;
}

inline bool readLocalReviewInput(const std::filesystem::path &filename, size_t limit, std::string &out) {
    const auto path = localReviewPath(filename);
    return path && readBoundedLocalFile(*path, limit, &out) == LocalFileRead::Ok;
}
}  // namespace om
