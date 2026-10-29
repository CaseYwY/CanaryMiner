#include "energy_history_fixture.h"
#include <iostream>
using namespace om;
using namespace energy_history_fixture;
int main(int argc,char **argv){
    if(argc!=3)return 2;
    const std::string action=argv[2];
    if(action!="init"&&action!="append"&&action!="crash"&&action!="after"&&action!="query")return 2;
    EnergyHistoryLedger l;const auto opened=l.open(std::filesystem::absolute(std::filesystem::u8path(argv[1])),binding(),T+20000,action=="init"?EnergyHistoryOpen::CreateNew:EnergyHistoryOpen::Existing);
    if(opened!=EnergyHistoryStatus::Ok){std::cout<<"SYNTHETIC open="<<energyHistoryStatusName(opened)<<'\n';return 1;}
    if(action=="crash")EnergyHistoryTestPeer::crashBeforeCommit(l);
    auto applied=EnergyHistoryStatus::Ok;
    if(action=="init")applied=l.apply(first(),T+20000);
    if(action=="append"||action=="crash"||action=="after")applied=l.apply(second(),T+20000);
    if(action=="after"&&applied==EnergyHistoryStatus::Ok)std::_Exit(87);
    const auto c=l.checkpoint(T+20000); // fixed synthetic output only
    const auto t=l.query(T,T+4000,T+20000);
    std::cout<<"SYNTHETIC status="<<energyHistoryStatusName(applied)<<" sequence="<<c.sequence<<" numerator="<<t.knownKwh.numerator.text()<<" covered="<<t.coveredDeviceMs<<" windows="<<c.windowCount<<'\n';
    return c.status==EnergyHistoryStatus::Ok&&t.status==EnergyHistoryStatus::Ok?0:1;
}
