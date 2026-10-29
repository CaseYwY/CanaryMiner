// Synchronous helper-only write-ahead input/receipt. Inspect never applies or settles an observation batch.
#pragma once
#include "energy_history_process.h"
namespace om {
enum class EnergyRecoveryOpen { ReadWrite, QueryOnly };
class EnergyRecoveryJournal {
   public:
    EnergyRecoveryJournal();
    ~EnergyRecoveryJournal();
    EnergyRecoveryJournal(const EnergyRecoveryJournal &)=delete;
    EnergyRecoveryJournal &operator=(const EnergyRecoveryJournal &)=delete;
    EnergyHistoryStatus open(const EnergyHistoryRequest &request,EnergyRecoveryOpen mode=EnergyRecoveryOpen::ReadWrite);
    bool exists() const;
    EnergyHistoryStatus initialize(const EnergyHistoryCheckpoint &empty);
    EnergyHistoryStatus prepare(const EnergyHistoryRequest &request,const EnergyHistoryCheckpoint &prior);
    EnergyHistoryStatus settle(EnergyHistoryStatus applied);
    bool matches(const EnergyHistoryRequest &request) const;
    EnergyRecoverySnapshot snapshot() const;
    EnergyRecoverySnapshot inspect(EnergyHistoryLedger &ledger,int64_t nowUtcMs) const;
   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace om
