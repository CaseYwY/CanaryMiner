// Development-only offline Windows private-chain inspection; not a TLS handshake or current mining permission.
#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
namespace om {
using PearlTlsDer=std::vector<unsigned char>;
struct PearlTlsPrivateTrustInput {
    bool enabled=false;
    std::string referenceDnsName; // literal IP support intentionally unavailable until separately qualified
    PearlTlsDer leaf;
    std::vector<PearlTlsDer> anchors,intermediates,crls;
};
enum class PearlTlsPrivateTrustStatus { Disabled, Unavailable, InvalidInput, UnsupportedReference, Cancelled, NativeFailure, Rejected, Verified };
enum class PearlTlsPrivateTrustStage { None, Store, Engine, Chain, SslPolicy };
struct PearlTlsPrivateTrustResult {
    PearlTlsPrivateTrustStatus status=PearlTlsPrivateTrustStatus::Disabled;
    bool chainEvaluated=false,policyEvaluated=false;
    uint32_t chainErrors=0,policyError=0;
    PearlTlsPrivateTrustStage nativeStage=PearlTlsPrivateTrustStage::None;
    uint32_t nativeError=0;
};
// All supplied objects are bounded public DER in new memory stores; never installs a root, selects/uses a client private key,
// or writes a system store. Native chain building can still consult ambient public supporting stores/caches.
// Cache-only/AIA/root-update restrictions retain strict chain-excluding-root revocation: unknown/offline refuses.
// Synchronous Crypt32 processing is NOT a hard cancellation/deadline guarantee or automatic Schannel-policy equivalence.
PearlTlsPrivateTrustResult inspectPearlTlsPrivateTrust(const PearlTlsPrivateTrustInput &,const std::atomic<bool> &cancel)noexcept;
} // namespace om
