#pragma once
#include "hash.h"
#include "local_review_input.h"
#include "meter_bill.h"
#include "tariff_declaration.h"

namespace om::meter_bill_detail {

constexpr size_t maxMeterFileBytes = 1024 * 1024;
constexpr size_t maxExactResultsBytes = 32 * 1024;
constexpr size_t maxArchiveBytes = tariffDeclarationMaxBytes + maxMeterFileBytes + maxExactResultsBytes + 256;
constexpr std::string_view archiveHeader = "CANARY-METER-REVIEW-1\n";

struct ReviewOriginal {
    std::string tariff, intervals, results;
    int64_t cutoff = 0;
};

inline std::string encodeReviewOriginal(const ReviewOriginal &review) {
    std::string body(archiveHeader);
    body += std::to_string(review.cutoff) + '\n';
    for (const auto *part : {&review.tariff, &review.intervals, &review.results}) {
        body += std::to_string(part->size()) + '\n';
        body += *part;
    }
    return body + sha256Hex(body) + '\n';
}

inline bool readArchiveNumber(const std::string &bytes, size_t &offset, int64_t limit, int64_t &out) {
    const auto end = bytes.find('\n', offset);
    if (end == std::string::npos || end == offset || end - offset > 15 ||
        bytes[offset] < '1' || bytes[offset] > '9')
        return false;
    const auto *last = bytes.data() + end;
    const auto parsed = std::from_chars(bytes.data() + offset, last, out);
    if (parsed.ec != std::errc{} || parsed.ptr != last || out > limit) return false;
    offset = end + 1;
    return true;
}

inline bool readArchivePart(const std::string &bytes, size_t &offset, size_t limit, std::string &out) {
    int64_t length = 0;
    if (!readArchiveNumber(bytes, offset, static_cast<int64_t>(limit), length) ||
        static_cast<size_t>(length) > bytes.size() - offset)
        return false;
    out = bytes.substr(offset, static_cast<size_t>(length));
    offset += static_cast<size_t>(length);
    return true;
}

inline bool decodeReviewOriginal(const std::string &bytes, ReviewOriginal &out) {
    if (bytes.size() > maxArchiveBytes || std::string_view(bytes).substr(0, archiveHeader.size()) != archiveHeader)
        return false;

    ReviewOriginal candidate;
    size_t offset = archiveHeader.size();
    if (!readArchiveNumber(bytes, offset, meterBillMaxUtcMs, candidate.cutoff) ||
        !readArchivePart(bytes, offset, tariffDeclarationMaxBytes, candidate.tariff) ||
        !readArchivePart(bytes, offset, maxMeterFileBytes, candidate.intervals) ||
        !readArchivePart(bytes, offset, maxExactResultsBytes, candidate.results) ||
        bytes.size() - offset != 65 || bytes.back() != '\n' ||
        bytes.compare(offset, 64, sha256Hex(bytes.substr(0, offset))) != 0)
        return false;
    out = std::move(candidate);
    return true;
}

enum class ReviewSave { Written, Unavailable, Stopped, Uncertain };

// A synced sibling is published exclusively. The CLI requires a trusted local working directory.
inline ReviewSave saveReviewOriginal(const std::string &name, const std::string &bytes,
                                     const volatile sig_atomic_t *stop) {
    if (!localExportName(name, ".cmr") || bytes.size() > maxArchiveBytes)
        return ReviewSave::Unavailable;
    if (stop && *stop) return ReviewSave::Stopped;
    const auto path = localReviewPath(std::filesystem::path(name));
    if (!path) return ReviewSave::Unavailable;

#if defined(_WIN32)
    auto temporary = *path;
    temporary += L".tmp";
    const auto file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return ReviewSave::Unavailable;
    DWORD written = 0;
    bool ok = GetFileType(file) == FILE_TYPE_DISK &&
              WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
              written == bytes.size() && FlushFileBuffers(file);
    if (!CloseHandle(file)) ok = false;
    const bool stopped = stop && *stop;
    if (ok && !stopped && MoveFileExW(temporary.c_str(), path->c_str(), MOVEFILE_WRITE_THROUGH))
        return ReviewSave::Written;
    if (!DeleteFileW(temporary.c_str())) return ReviewSave::Uncertain;
    return stopped ? ReviewSave::Stopped : ReviewSave::Unavailable;
#else
    const auto temporary = name + ".tmp";
    const int directory = ::open(path->parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (directory < 0) return ReviewSave::Unavailable;
    const int file = ::openat(directory, temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (file < 0) {
        ::close(directory);
        return ReviewSave::Unavailable;
    }

    size_t offset = 0;
    while (offset < bytes.size() && !(stop && *stop)) {
        const auto written = ::write(file, bytes.data() + offset, bytes.size() - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) break;
        offset += static_cast<size_t>(written);
    }
    const bool complete = offset == bytes.size() && fsync(file) == 0;
    const bool closed = ::close(file) == 0;
    const bool stopped = stop && *stop;
    const bool published = complete && closed && !stopped &&
                           ::linkat(directory, temporary.c_str(), directory, name.c_str(), 0) == 0;
    const bool removed = ::unlinkat(directory, temporary.c_str(), 0) == 0;
    const bool synced = !published || fsync(directory) == 0;
    ::close(directory);
    if (!removed || !synced) return ReviewSave::Uncertain;
    if (published) return ReviewSave::Written;
    return stopped ? ReviewSave::Stopped : ReviewSave::Unavailable;
#endif
}

}  // namespace om::meter_bill_detail
