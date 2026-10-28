// Separate crash/read fixture. Each invocation is a new process; no parent-helper death/reaping claim is made here.
#include "product/credit_recovery.h"
#include "credit_fixture.h"
#include <cstdlib>
#include <iostream>
using namespace om;
using namespace credit_fixture;
int main(int argc, char **argv) {
    if(argc!=3)return 2;
    CreditProcessRequest r; r.generation=r.ticket=1; r.file=std::filesystem::absolute(argv[2]); r.binding=contract(); r.nowUtcMs=T;
    r.queries={{true,0,T}}; r.batch=batch(1,T,{record("fixture.crash-record",1,T,"2.5")}); r.recovery=CreditRecoveryMode::Submit;
    const std::string mode=argv[1];
    if(mode=="inspect" || mode=="retry") {
        r.nowUtcMs=T+1000; r.queries={{true,0,T+1000}};
        r.recovery=mode=="inspect"?CreditRecoveryMode::Inspect:CreditRecoveryMode::Retry;
        if(mode=="inspect")r.batch.reset();
        const auto reply=executeCreditRequest(r); std::string wire;
        if(!encodeCreditReply(r,reply,&wire))return 3;
        std::cout<<wire; return 0;
    }
    auto initialize=r; initialize.batch.reset(); initialize.recovery=CreditRecoveryMode::Volatile;
    if(executeCreditRequest(initialize).opened!=CreditLedgerStatus::Ok)return 8;
    CreditRecoveryJournal journal;
    if(journal.open(r)!=CreditLedgerStatus::Ok || journal.prepare(r)!=CreditLedgerStatus::Ok)return 4;
    if(mode=="prepared")std::_Exit(75);
    auto volatileRequest=r; volatileRequest.recovery=CreditRecoveryMode::Volatile;
    if(executeCreditRequest(volatileRequest).applied!=CreditLedgerStatus::Ok)return 5;
    if(mode=="applied")std::_Exit(76);
    if(mode!="confirmed" || journal.settle(CreditLedgerStatus::Ok)!=CreditLedgerStatus::Ok)return 6;
    std::_Exit(77);
}
