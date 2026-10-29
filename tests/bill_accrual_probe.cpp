#include "electricity_fixture.h"
#include <iostream>
#include <sstream>
using namespace electricity_fixture;
int main(){
    std::string line;
    while(std::getline(std::cin,line)){
        if(line.size()>8192)return 2;
        std::istringstream input(line);std::string mode,watts,r1,r2,threshold,base,factor,floor,cycle,daily;int64_t elapsed=0;unsigned count=0;
        input>>mode>>watts>>r1>>r2>>threshold>>base>>factor>>floor>>cycle>>daily>>elapsed>>count;
        if(!input||elapsed<=0||elapsed>10*day||!count||count>128)return 3;
        auto p=plan(T,T+elapsed);auto state=opening(T,T+elapsed);p.revisions[0].baseRatePerKwh=d(base);p.subtotalMultiplier=d(factor);p.cycleCharge=d(cycle);p.revisions[0].chargePerCivilDay=d(daily);
        if(mode!="none"){p.tiers=mode=="progressive"?TierMode::Progressive:TierMode::AllUnits;p.revisions[0].bands={{d(threshold),d(r1)},{std::nullopt,d(r2)}};}
        if(floor!="none")p.minimumCycleSubtotal=d(floor);
        for(unsigned i=0;i<count;++i){int64_t cut=0;input>>cut;if(!input)return 4;
            const auto result=accrueBill(p,state,load(watts,state.throughUtcMs,T+cut),T+cut,T+cut);if(result.status!=ElectricityStatus::Ok)return 5;
            std::cout<<result.provisionalTotal->numerator.text()<<' '<<result.provisionalChange->numerator.text()<<' '<<result.ending->importedKwh.numerator.text()<<' '<<display(*result.provisionalTotal,12)<<' '<<result.settlementDependent<<' '<<static_cast<bool>(result.settledTotal)<<'\n';
            state=*result.ending;
        }
        if(state.throughUtcMs!=T+elapsed)return 6;
    }
}
