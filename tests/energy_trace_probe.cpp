#include "energy_history_fixture.h"
#include <iostream>
using namespace om;
using namespace energy_history_fixture;
int main(int argc,char **argv){
    if(argc!=4)return 2;
    const std::string mode=argv[1],version=argv[3];if((mode!="create"&&mode!="inspect")||(version!="1"&&version!="2"))return 2;
    const auto path=std::filesystem::absolute(std::filesystem::u8path(argv[2]));EnergyHistoryLimits limits;if(version=="2")limits.compaction=EnergyCompactionPolicy{2,32768};
    EnergyHistoryLedger ledger;const auto opened=ledger.open(path,binding(),T+20000,mode=="create"?EnergyHistoryOpen::CreateNew:EnergyHistoryOpen::ExistingQuery,limits);
    if(opened!=EnergyHistoryStatus::Ok){std::cout<<"{\"status\":"<<static_cast<int>(opened)<<"}\n";return 0;}
    if(mode=="create")for(auto input:{first(),second()}){if(limits.compaction)input.id="energy."+std::to_string(input.sequence);if(ledger.apply(input,T+20000)!=EnergyHistoryStatus::Ok)return 3;}
    const auto result=ledger.queryTrace(T,T+1000,T+20000);if(!result.completeTrace)return 4;
    std::cout<<"{\"status\":"<<static_cast<int>(result.status)<<",\"sequence\":"<<result.checkpoint.sequence<<",\"digest\":\""<<result.checkpoint.digest<<"\",\"compacted\":"<<result.checkpoint.compactedSequence
        <<",\"watts\":\""<<result.completeTrace->intervals[0].watts.text()<<"\",\"numerator\":\""<<result.totals.knownKwh.numerator.text()<<"\"}\n";
}
