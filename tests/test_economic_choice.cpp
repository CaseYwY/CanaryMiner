#include "economic_fixture.h"
#include "product/economic_choice.h"
#include "product_test_util.h"
using namespace economic_fixture;
namespace {
using S=EconomicChoiceState;using K=EconomicCurrentKind;
EconomicChoicePolicy policy(){EconomicChoicePolicy p;p.enabled=true;return p;}
EconomicCurrentChoice paused(){return {K::Paused,0};}
EconomicCurrentChoice applied(unsigned ms){return {K::AppliedWait,ms};}
void choices(){
    auto in=input();auto study=buildEconomicStudy(in);CHECK(study&&study->summary().bestPoint==0);
    auto p=policy();CHECK(economicChoice(study.get(),{},paused()).state==S::Disabled);
    CHECK(economicChoice(nullptr,p,paused()).state==S::Unavailable&&economicChoice(study.get(),p,{}).state==S::Unavailable);
    for(auto current:{EconomicCurrentChoice{K::Paused,1},EconomicCurrentChoice{K::Unknown,1},applied(1001),EconomicCurrentChoice{static_cast<K>(99),0}})
        CHECK(economicChoice(study.get(),p,current).state==S::Invalid);
    for(unsigned mode=0;mode<7;++mode){auto invalid=p;
        if(mode==0)invalid.resumeMinimumUsdPerSecond=d("-0.1");
        if(mode==1)invalid.switchMinimumGainUsdPerSecond=d("-0.1");
        if(mode==2)invalid.switchMinimumFraction=d("-0.1");
        if(mode==3)invalid.switchMinimumFraction=d("1.000000000000000001");
        if(mode==4)invalid.resumeMinimumUsdPerSecond=d("0.0000000000000000001");
        if(mode==5)invalid.switchMinimumGainUsdPerSecond=d(std::string(39,'1'));
        if(mode==6)invalid.switchMinimumFraction=d("0.0000000000000000001");
        CHECK(!validEconomicChoicePolicy(invalid)&&economicChoice(study.get(),invalid,paused()).state==S::Invalid);}
    for(auto current:{paused(),applied(10),applied(0)}){const auto choice=economicChoice(study.get(),p,current);
        CHECK(choice.state==(current.kind==K::Paused?S::Resume:current.hostWaitMs==10?S::Switch:S::Hold));
        CHECK(choice.point==0&&choice.hostWaitMs==0&&choice.referenceOnly&&!choice.statisticalConfidenceAvailable);}
    p.resumeMinimumUsdPerSecond=d("1");CHECK(economicChoice(study.get(),p,paused()).state==S::PauseMargin);
    CHECK(economicChoice(study.get(),p,applied(0)).state==S::Hold); // resume threshold must not force a profitable active point to pause
    p=policy();p.switchMinimumGainUsdPerSecond=d("1");CHECK(economicChoice(study.get(),p,applied(10)).state==S::Hold);
    p=policy();p.switchMinimumFraction=d("1");CHECK(economicChoice(study.get(),p,applied(10)).state==S::Hold);
    p=policy();CHECK(economicChoice(study.get(),p,applied(11)).state==S::PauseIneligible);
    in.candidates[1].permitted=false;study=buildEconomicStudy(in);CHECK(study&&economicChoice(study.get(),p,applied(10)).state==S::PauseIneligible);
    in=input();prices(in,"0.001");study=buildEconomicStudy(in);CHECK(study!=nullptr);
    for(auto current:{paused(),applied(0),applied(10)}){const auto choice=economicChoice(study.get(),p,current);CHECK(choice.state==S::PauseNonpositive&&!choice.point&&!choice.hostWaitMs);}
    in=input();prices(in,"0.002");study=buildEconomicStudy(in);CHECK(study&&study->summary().bestPoint==1);
    CHECK(economicChoice(study.get(),p,applied(0)).state==S::PauseNonpositive); // immediately withdraw unprofitable current point, not a direct switch
    in=input(profile("200","200",2000,2000,100000,100000));study=buildEconomicStudy(in);CHECK(study&&study->summary().bestPoint==0);
    const auto tie=economicChoice(study.get(),p,applied(10));CHECK(tie.state==S::Hold&&tie.point==1&&tie.hostWaitMs==10);
    in=input(profile("0","160",2000,2000,0,100000));study=buildEconomicStudy(in);
    CHECK(study&&study->summary().bestPoint==1&&study->summary().candidates[0].netUsdPerSecond.numerator.zero());
    CHECK(economicChoice(study.get(),p,applied(0)).state==S::PauseNonpositive);
    for(auto &candidate:in.candidates)candidate.permitted=false;
    study=buildEconomicStudy(in);
    CHECK(study&&economicChoice(study.get(),p,paused()).state==S::PauseNonpositive&&economicChoice(study.get(),p,applied(0)).state==S::PauseIneligible);
}
void exactEdges(){
    // 100 / 80 user TMAC/s, zero board-cost => exact score rates 0.0001 / 0.00008 USD/s.
    auto in=input();in.electricity.plan.revisions[0].baseRatePerKwh=d("0");auto study=buildEconomicStudy(in);CHECK(study!=nullptr);
    auto p=policy();p.resumeMinimumUsdPerSecond=d("0.0001");CHECK(economicChoice(study.get(),p,paused()).state==S::Resume);
    p.resumeMinimumUsdPerSecond=d("0.000100000000000001");CHECK(economicChoice(study.get(),p,paused()).state==S::PauseMargin);
    p=policy();p.switchMinimumGainUsdPerSecond=d("0.00002");p.switchMinimumFraction=d("0.25");
    CHECK(economicChoice(study.get(),p,applied(10)).state==S::Switch);
    p.switchMinimumGainUsdPerSecond=d("0.000020000000000001");CHECK(economicChoice(study.get(),p,applied(10)).state==S::Hold);
    p.switchMinimumGainUsdPerSecond=d("0.00002");p.switchMinimumFraction=d("0.250000000000000001");CHECK(economicChoice(study.get(),p,applied(10)).state==S::Hold);
    p=policy();p.switchMinimumGainUsdPerSecond=d(std::string(38,'9'));CHECK(validEconomicChoicePolicy(p));
    const auto capacity=economicChoice(study.get(),p,applied(10));CHECK(capacity.state==S::Capacity&&!capacity.point&&!capacity.hostWaitMs);
}
}
int main(){choices();exactEdges();return finishTests("exact economic choice margins");}
