#pragma once
#include "hash.h"
#include "local_review_archive.h"
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

inline bool decodeReviewOriginal(const std::string &bytes, ReviewOriginal &out) {
    if (bytes.size() > maxArchiveBytes || std::string_view(bytes).substr(0, archiveHeader.size()) != archiveHeader)
        return false;

    using local_review_archive::readArchiveNumber;
    using local_review_archive::readArchivePart;
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

}  // namespace om::meter_bill_detail
