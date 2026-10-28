// Private scratch-database probe for crash/recovery tests. Fixed synthetic facts, no arbitrary import or network.
#include "credit_fixture.h"
#include <iostream>
using namespace om;
using namespace credit_fixture;
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    const std::string action = argv[2];
    if (action != "init" && action != "query" && action != "crash" && action != "correct" && action != "rollback" && action != "replay") return 2;
    CreditLedger ledger;
    const auto opened = ledger.open(std::filesystem::absolute(std::filesystem::u8path(argv[1])),contract(),T+10000);
    if (opened != CreditLedgerStatus::Ok) { std::cout << "SYNTHETIC FIXTURE open=" << creditLedgerStatusName(opened) << '\n'; return 1; }
    CreditLedgerStatus status = CreditLedgerStatus::Ok;
    if (action == "init" || action == "replay") status = ledger.apply(batch(1,T,{record("credit-A",1,T,"4.25")}),T+10000);
    if (action == "correct" || action == "crash" || action == "rollback") {
        auto b = batch(2,T+1000,{record("credit-A",2,T,"3.75")});
        if (action == "crash") CreditLedgerTestPeer::crashBeforeCommit(ledger);
        if (action == "rollback") b.records.push_back(record("Z-invalid-adjustment",1,T+1000,"-99",CreditKind::PostedAdjustment,"credit-A"));
        status = ledger.apply(b,T+10000);
    }
    const auto sum = ledger.cumulative(T,T+10000);
    std::cout << "SYNTHETIC FIXTURE status=" << creditLedgerStatusName(status) << " quantity=" << sum.knownPostedNet.text() <<
        " complete=" << (sum.completePostedNet ? 1 : 0) << " observed=" << sum.observedAtMs << '\n';
    return (status == CreditLedgerStatus::Ok || status == CreditLedgerStatus::Replay || (action == "rollback" && status == CreditLedgerStatus::Invalid)) && sum.status == CreditLedgerStatus::Ok ? 0 : 1;
}
