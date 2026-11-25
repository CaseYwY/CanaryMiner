#pragma once
#include "local_preferences_store.h"

namespace om {

inline bool readLocalReviewInput(const std::filesystem::path &filename, size_t limit, std::string &out) {
    std::error_code ec;
    const auto path = std::filesystem::absolute(filename, ec);
    if (ec) return false;
#if defined(_WIN32)
    const auto root = path.root_name().native();
    if (root.size() != 2 || root[1] != L':' || path.native().find(L':', 2) != std::wstring::npos)
        return false;
    const auto drive = GetDriveTypeW(path.root_path().c_str());
    if (drive != DRIVE_FIXED && drive != DRIVE_REMOVABLE && drive != DRIVE_RAMDISK) return false;
    auto parent = path.root_path();
    const auto directories = path.parent_path().relative_path();
    for (const auto &component : directories) {
        parent /= component;
        const auto attributes = GetFileAttributesW(parent.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    }
#endif
    return readBoundedLocalFile(path, limit, &out) == LocalFileRead::Ok;
}
}  // namespace om
