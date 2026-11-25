#pragma once
#include "hash.h"
#include "local_review_archive.h"
#include "meter_bill.h"
#include "octopus_meter_rates.h"
#include "tariff_declaration.h"

#include <algorithm>

namespace om::meter_bill_detail {

constexpr size_t maxMeterFileBytes = 1024 * 1024;
constexpr size_t maxExactResultsBytes = 32 * 1024;
constexpr size_t maxOriginalArchiveBytes = tariffDeclarationMaxBytes + maxMeterFileBytes + maxExactResultsBytes + 256;
constexpr size_t maxRatesArchiveBytes = maxOriginalArchiveBytes + maxOctopusRateFileBytes + 16;
constexpr size_t maxInvoiceArchiveBytes = maxRatesArchiveBytes + maxInvoiceBytes + 16;
constexpr size_t maxArchiveBytes = maxInvoiceArchiveBytes + 192;
static_assert(maxArchiveBytes <= maxBoundedLocalFileBytes);
constexpr std::string_view archiveHeader = "CANARY-METER-REVIEW-1\n";

constexpr std::string_view archiveRatesHeader = "CANARY-METER-REVIEW-2\n";
constexpr std::string_view archiveInvoiceHeader = "CANARY-METER-REVIEW-3\n";

constexpr std::string_view archiveExtendedHeader = "CANARY-METER-REVIEW-4\n";

struct PreviousMeterReview {
    int64_t cutoff = 0, through = 0;
    std::string archiveSha256, prefixSha256;
};

struct ReviewOriginal {
    std::string tariff, intervals, results, supplierRates, invoice;
    int64_t cutoff = 0;
    std::optional<PreviousMeterReview> previous;
};

inline bool validReviewDigest(std::string_view text) {
    return text.size() == 64 && std::all_of(text.begin(), text.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

inline std::string encodeReviewOriginal(const ReviewOriginal &review) {
    std::string body(review.previous ? archiveExtendedHeader : !review.invoice.empty() ? archiveInvoiceHeader :
        review.supplierRates.empty() ? archiveHeader : archiveRatesHeader);
    body += std::to_string(review.cutoff) + '\n';
    for (const auto *part : {&review.tariff, &review.intervals, &review.results}) {
        body += std::to_string(part->size()) + '\n';
        body += *part;
    }
    if (review.previous) {
        const auto &previous = *review.previous;
        body += std::to_string(previous.cutoff) + '\n' + std::to_string(previous.through) + '\n';
        body += previous.archiveSha256 + '\n' + previous.prefixSha256 + '\n';
        const unsigned mask = (!review.invoice.empty() ? 1u : 0u) | (!review.supplierRates.empty() ? 2u : 0u);
        body += std::to_string(mask + 1) + '\n';
    }
    for (const auto *part : {&review.invoice, &review.supplierRates}) {
        if (part->empty()) continue;
        body += std::to_string(part->size()) + '\n';
        body += *part;
    }
    return body + sha256Hex(body) + '\n';
}

inline bool readReviewDigest(const std::string &bytes, size_t &offset, std::string &out) {
    constexpr size_t width = 64;
    if (bytes.size() - offset < width + 1 || bytes[offset + width] != '\n' ||
        !validReviewDigest(std::string_view(bytes).substr(offset, width)))
        return false;
    out = bytes.substr(offset, width);
    offset += width + 1;
    return true;
}

inline bool decodeReviewOriginal(const std::string &bytes, ReviewOriginal &out) {
    const auto header = std::string_view(bytes).substr(0, archiveHeader.size());
    const bool extended = header == archiveExtendedHeader;
    bool hasRates = header == archiveRatesHeader;
    bool hasInvoice = header == archiveInvoiceHeader;
    if (bytes.size() > (extended ? maxArchiveBytes : hasInvoice ? maxInvoiceArchiveBytes :
                       hasRates ? maxRatesArchiveBytes : maxOriginalArchiveBytes) ||
        (!extended && !hasRates && !hasInvoice && header != archiveHeader))
        return false;

    using local_review_archive::readArchiveNumber;
    using local_review_archive::readArchivePart;
    ReviewOriginal candidate;
    size_t offset = archiveHeader.size();
    if (!readArchiveNumber(bytes, offset, meterBillMaxUtcMs, candidate.cutoff) ||
        !readArchivePart(bytes, offset, tariffDeclarationMaxBytes, candidate.tariff) ||
        !readArchivePart(bytes, offset, maxMeterFileBytes, candidate.intervals) ||
        !readArchivePart(bytes, offset, maxExactResultsBytes, candidate.results))
        return false;
    if (extended) {
        auto &previous = candidate.previous.emplace();
        int64_t mask = 0;
        if (!readArchiveNumber(bytes, offset, candidate.cutoff, previous.cutoff) ||
            !readArchiveNumber(bytes, offset, previous.cutoff, previous.through) ||
            !readReviewDigest(bytes, offset, previous.archiveSha256) ||
            !readReviewDigest(bytes, offset, previous.prefixSha256) ||
            !readArchiveNumber(bytes, offset, 4, mask))
            return false;
        --mask;
        hasInvoice = (mask & 1) != 0;
        hasRates = (mask & 2) != 0;
    }
    if ((hasInvoice && !readArchivePart(bytes, offset, maxInvoiceBytes, candidate.invoice)) ||
        ((hasRates || (!extended && hasInvoice && bytes.size() - offset > 65)) &&
            !readArchivePart(bytes, offset, maxOctopusRateFileBytes, candidate.supplierRates)) ||
        bytes.size() - offset != 65 || bytes.back() != '\n' ||
        bytes.compare(offset, 64, sha256Hex(bytes.substr(0, offset))) != 0)
        return false;
    out = std::move(candidate);
    return true;
}

}  // namespace om::meter_bill_detail
