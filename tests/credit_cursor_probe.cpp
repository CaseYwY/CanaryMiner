// Crash seam linked only against the test ledger. No arbitrary source records or imports.
#include "credit_fixture.h"
#include <iostream>
using namespace om;
using namespace credit_fixture;
int main(int argc,char **argv){
    if(argc!=3)return 2;
    const std::string action=argv[2];
    if(action!="init"&&action!="empty"&&action!="third"&&action!="correct"&&action!="crash"&&action!="rollback"&&action!="query")return 2;
    CreditLedger ledger;const auto opened=ledger.open(std::filesystem::absolute(std::filesystem::u8path(argv[1])),cursorContract(),T+10000);
    if(opened!=CreditLedgerStatus::Ok){std::cout<<"SYNTHETIC open="<<creditLedgerStatusName(opened)<<'\n';return 1;}
    CreditLedgerStatus status=CreditLedgerStatus::Ok;
    if(action=="init")status=ledger.apply(page(1,T,{record("fixture.credit-A",1,T,"4.25")}),T+10000);
    if(action=="empty")status=ledger.apply(page(2,T+1000),T+10000);
    if(action=="third")status=ledger.apply(page(3,T+2000),T+10000);
    if(action=="correct"||action=="crash"||action=="rollback"){
        auto b=page(2,T+1000,{record("fixture.credit-A",2,T,"3.75")});
        if(action=="rollback")b.records.push_back(record("fixture.zzz-invalid",1,T,"-10",CreditKind::PostedAdjustment,"fixture.credit-A"));
        if(action=="crash")CreditLedgerTestPeer::crashBeforeCommit(ledger);
        status=ledger.apply(b,T+10000);
    }
    const auto c=ledger.cursor(T+10000);const auto total=ledger.cumulative(T+10000,T+10000);
    std::cout<<"SYNTHETIC status="<<creditLedgerStatusName(status)<<" quantity="<<total.knownPostedNet.text()<<" sequence="<<c.sequence<<" next="<<c.next<<" observed="<<c.observedAtMs<<'\n';
    return c.status==CreditLedgerStatus::Ok&&total.status==CreditLedgerStatus::Ok?0:1;
}
