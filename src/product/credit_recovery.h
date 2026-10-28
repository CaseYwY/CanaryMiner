// Helper-side durable write-ahead intent/receipt. Never call this synchronous file API from a parent safety/render loop.
#pragma once
#include "credit_process.h"
namespace om {
class CreditRecoveryJournal {
   public:
    CreditRecoveryJournal();
    ~CreditRecoveryJournal();
    CreditRecoveryJournal(const CreditRecoveryJournal &) = delete;
    CreditRecoveryJournal &operator=(const CreditRecoveryJournal &) = delete;
    CreditLedgerStatus open(const CreditProcessRequest &request);
    CreditLedgerStatus prepare(const CreditProcessRequest &request);
    CreditLedgerStatus settle(CreditLedgerStatus applied);
    bool matches(const CreditProcessRequest &request) const;
    CreditRecoverySnapshot snapshot() const;
   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace om
