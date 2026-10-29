#include "energy_history_fixture.h"
#include <cstdlib>
#include <iostream>
int main(int argc,char **argv){
    using namespace om;using namespace energy_history_fixture;
    if(argc!=3)return 2;
    const auto file=std::filesystem::absolute(argv[1]);const std::string action=argv[2];
    EnergyHistoryLimits limits;limits.compaction=EnergyCompactionPolicy{2,32768};EnergyHistoryLedger ledger;
    const auto opened=ledger.open(file,binding(),T+20000,action=="init"?EnergyHistoryOpen::CreateNew:EnergyHistoryOpen::Existing,limits);
    if(opened!=EnergyHistoryStatus::Ok){std::cout<<"open="<<static_cast<unsigned>(opened)<<'\n';return 1;}
    if(action=="init"||action=="append"||action=="crash"||action=="after"){
        auto b=action=="init"?first():second();b.id="energy."+std::to_string(b.sequence);
#ifdef CANARY_ENERGY_HISTORY_TESTING
        if(action=="crash")EnergyHistoryTestPeer::crashBeforeCommit(ledger);
#endif
        const auto applied=ledger.apply(b,T+20000);if(action=="after"&&applied==EnergyHistoryStatus::Ok)std::_Exit(87);
        std::cout<<"applied="<<static_cast<unsigned>(applied)<<' ';
    }else if(action!="query")return 2;
    const auto c=ledger.checkpoint(T+20000);
    const auto t=ledger.query(T,T+4000,T+20000);
    std::cout<<"sequence="<<c.sequence<<" compacted="<<c.compactedSequence<<" numerator="<<t.knownKwh.numerator.text()<<" covered="<<t.coveredDeviceMs<<'\n';return 0;
}
