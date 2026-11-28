#pragma once

#include "local_review_archive.h"
#include "market_data.h"

namespace om::market_detail {

inline void encodeMarketAssetEvidence(std::string &archive, const PublicJsonResponse &response) {
    archive += response.assetEvidence ? "2\n" : "1\n";
    if (response.assetEvidence) {
        const auto &asset = *response.assetEvidence;
        archive += std::to_string(asset.observedUtc) + '\n' + std::to_string(asset.body.size()) + '\n' + asset.body;
    }
}

inline bool readMarketAssetEvidence(const std::string &archive, size_t &offset, uint64_t clockLimit,
                                    PublicJsonResponse &response) {
    int64_t present = 0;
    if (!local_review_archive::readArchiveNumber(archive, offset, 2, present)) return false;
    if (present == 1) return true;
    int64_t observed = 0;
    auto &asset = response.assetEvidence.emplace();
    if (!local_review_archive::readArchiveNumber(archive, offset, clockLimit, observed) ||
        !local_review_archive::readArchivePart(archive, offset, maxMarketAssetBodyBytes, asset.body)) return false;
    asset.observedUtc = static_cast<uint64_t>(observed);
    return true;
}

inline bool appendMarketAssetEvidence(std::string &report, const char *provider, PublicFeed metadataFeed,
                                      const PublicJsonResponse &response, uint64_t sentUtc, uint64_t receivedUtc) {
    const bool refused = response.status == PublicHttpsStatus::AssetIdentity;
    const bool invalidTime = response.status == PublicHttpsStatus::AssetObservation;
    const auto &asset = response.assetEvidence;
    if (!asset) {
        if (refused || response.status == PublicHttpsStatus::Ok || (invalidTime && response.httpStatus)) return false;
        report += std::string(provider) + " metadata: unavailable\n";
        return true;
    }
    const bool marketObservedWithinRequest = response.status == PublicHttpsStatus::Ok &&
        response.observedUtc >= sentUtc && response.observedUtc <= receivedUtc;
    if (!asset->observedUtc || asset->observedUtc < sentUtc || asset->observedUtc > receivedUtc ||
        (marketObservedWithinRequest && asset->observedUtc > response.observedUtc) ||
        (refused && response.httpStatus != 200) ||
        (invalidTime && (response.httpStatus != 200 || response.observedUtc >= asset->observedUtc)) ||
        !boundedPublicJson(asset->body, maxMarketAssetBodyBytes))
        return false;
    if (marketIdentityMatches(metadataFeed, asset->body) == refused) return false;
    report += std::string(provider) + (refused ? " metadata: identity refused" : " metadata: identity checked") +
        " | observed UTC " + std::to_string(asset->observedUtc) + '\n';
    return true;
}

}  // namespace om::market_detail
