#include "economic_fixture.h"
#include <iostream>
using namespace economic_fixture;
int main(){
    std::string w0,w1,credits,price,fx,tariff,tariffFx,haircut,e0,e1;unsigned ms0,ms1,repeat0,repeat1;uint32_t mw0,mw1;uint64_t horizon,t0,t1;
    while(std::cin>>w0>>w1>>ms0>>ms1>>mw0>>mw1>>credits>>price>>fx>>tariff>>tariffFx>>horizon>>haircut>>t0>>e0>>t1>>e1>>repeat0>>repeat1){
        auto in=input(profile(w0,w1,ms0,ms1,mw0,mw1));in.policy.lookbackMinutes=2;prices(in,price,fx);reward(in,credits);in.policy.horizonMs=horizon;in.policy.revenueHaircut=d(haircut);
        in.electricity.plan.currency="XTS";in.electricity.plan.revisions[0].baseRatePerKwh=d(tariff);in.electricity.fx=ForecastCurrencyFx{ForecastQualification::SyntheticFixture,"XTS","fixture.fx",d(tariffFx),T,1000};
        in.candidates[0].transitionMs=t0;in.candidates[0].transitionEnergy.numerator=d(e0);in.candidates[0].repeatMeasurement=repeat0!=0;
        in.candidates[1].transitionMs=t1;in.candidates[1].transitionEnergy.numerator=d(e1);in.candidates[1].repeatMeasurement=repeat1!=0;
        EconomicStatus status;const auto study=buildEconomicStudy(in,&status);if(!study){std::cout<<"error "<<static_cast<unsigned>(status)<<'\n';continue;}
        const auto &s=study->summary();std::cout<<(s.bestPoint?static_cast<int>(*s.bestPoint):-1);
        for(const auto &c:s.candidates)for(const auto *r:{&c.expectedTokens,&c.estimatedKwh,&c.revenueUsd,&c.costUsd,&c.netUsd,&c.netUsdPerSecond})std::cout<<' '<<r->numerator.text()<<' '<<r->denominator.text();
        std::cout<<'\n';
    }
}
