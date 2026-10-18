// Classification shared by run accounting and CPU-only outcome tests.
#pragma once

#include <string>

namespace om {

constexpr const char *kPearlLocalStaleBeforeSendError =
    "job superseded or retired before submission; proof not sent";

enum class PearlSubmitFailureKind { LocalStale, DeliveryFailure, OtherLocalFailure };

inline PearlSubmitFailureKind pearlSubmitFailureKind(const std::string &error) noexcept {
    if (error == kPearlLocalStaleBeforeSendError)
        return PearlSubmitFailureKind::LocalStale;
    if (error == "could not send the share" ||
        error == "only part of the share request reached the socket; outcome unknown")
        return PearlSubmitFailureKind::DeliveryFailure;
    return PearlSubmitFailureKind::OtherLocalFailure;
}

}  // namespace om
