// Synthetic exact-choice oracle interface only. No I/O except bounded fixture input/output.
#include "economic_fixture.h"
#include "product/economic_choice.h"
#include <iostream>
#include <sstream>
using namespace economic_fixture;
int main(){std::string line;unsigned rows=0;
    while(std::getline(std::cin,line)){if(++rows>1024||line.size()>4096)return 2;
        std::string w0,w1,credit,price,tariff,resume,gain,fraction,extra;unsigned ms0=0,ms1=0,mw0=0,mw1=0;int current=0;std::istringstream row(line);
        if(!(row>>w0>>w1>>ms0>>ms1>>mw0>>mw1>>credit>>price>>tariff>>resume>>gain>>fraction>>current)||(row>>extra))return 3;
        try{auto in=input(profile(w0,w1,ms0,ms1,mw0,mw1));reward(in,credit);prices(in,price);in.electricity.plan.revisions[0].baseRatePerKwh=d(tariff);
            const auto study=buildEconomicStudy(in);if(!study)return 4;
            EconomicChoicePolicy p;p.enabled=true;p.resumeMinimumUsdPerSecond=d(resume);p.switchMinimumGainUsdPerSecond=d(gain);p.switchMinimumFraction=d(fraction);
            EconomicCurrentChoice c;if(current==-1)c.kind=EconomicCurrentKind::Paused;else if(current>=0){c.kind=EconomicCurrentKind::AppliedWait;c.hostWaitMs=static_cast<unsigned>(current);}
            const auto choice=economicChoice(study.get(),p,c);
            std::cout<<static_cast<unsigned>(choice.state)<<' '<<(choice.point?static_cast<int>(*choice.point):-1)<<' '<<(choice.hostWaitMs?static_cast<int>(*choice.hostWaitMs):-1)<<'\n';
        }catch(...){return 5;}
    }return 0;
}
