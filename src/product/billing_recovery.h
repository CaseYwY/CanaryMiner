// Synchronous helper-side monetary recovery. Owns journal then ledger leases; never queries the live power source.
#pragma once
#include "billing_ledger.h"
namespace om {
enum class BillingRecoveryOpen { Existing, CreateNew, QueryOnly };
enum class BillingRecoveryState { Unavailable, Empty, Prepared, Confirmed, Rejected, Diverged };
struct BillingRecoverySnapshot {
    BillingStatus status=BillingStatus::Storage;
    BillingRecoveryState state=BillingRecoveryState::Unavailable;
    std::optional<BillingStatus> applied;
    uint64_t priorSequence=0;
    std::string priorDigest,revision; // revision binds exact private journal bytes, including the original receipt
    int64_t recordedUtcMs=0;
    std::optional<BillingChunk> original;
    BillingSnapshot committed; // committed prefix remains separate from Prepared input
};
class BillingRecoveryStore {
   public:
    BillingRecoveryStore();
    ~BillingRecoveryStore();
    BillingRecoveryStore(const BillingRecoveryStore &)=delete;
    BillingRecoveryStore &operator=(const BillingRecoveryStore &)=delete;
    BillingStatus open(const std::filesystem::path &absoluteFile,const BillingBinding &binding,int64_t nowUtcMs,
                       BillingRecoveryOpen mode=BillingRecoveryOpen::Existing,BillingLimits limits={});
    BillingRecoverySnapshot inspect(int64_t nowUtcMs)const; // never settles, retries or repairs
    // Existing-only READONLY lookup for an isolated reader. The caller must already approve the complete binding digest.
    BillingStatus openQuery(const std::filesystem::path &absoluteFile,const std::string &expectedBindingDigest,int64_t nowUtcMs);
    // One existing-only recovery operation. Review tokens are checked under the journal lease before writable ledger access.
    BillingStatus retryReviewed(const std::filesystem::path &absoluteFile,const std::string &expectedBindingDigest,
                               const std::string &reviewedRevision,const std::string &reviewedOriginalDigest,int64_t nowUtcMs);
    // New input requires the exact reviewed journal AND audited monetary head under the same ordered leases.
    BillingStatus submitReviewed(const std::filesystem::path &absoluteFile,const std::string &expectedBindingDigest,
                                const std::string &reviewedRevision,uint64_t priorSequence,const std::string &priorDigest,
                                const BillingChunk &chunk,int64_t nowUtcMs);
    BillingStatus submit(const BillingChunk &chunk,int64_t nowUtcMs);
    BillingStatus retry(const BillingChunk &reviewedOriginal,const std::string &reviewedRevision,int64_t nowUtcMs);
   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
#ifdef CANARY_BILLING_TESTING
    friend struct BillingRecoveryTestPeer;
#endif
};
std::string billingJournalRevision(const std::filesystem::path &file,const std::string &canonicalBinding,const BillingRecoverySnapshot &snapshot);
#ifdef CANARY_BILLING_TESTING
enum class BillingRecoveryTestPoint { None, BeforeJournalReplace, AfterPrepared, BeforeCommit, AfterCommit, AfterSettled, FailSettlement };
struct BillingRecoveryTestPeer {static void interrupt(BillingRecoveryStore &store,BillingRecoveryTestPoint point);};
#endif
} // namespace om
