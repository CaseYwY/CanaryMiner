// Bounded synthetic oracle input, independently checked with Python Fraction; never shipped or network-enabled.
#include "income_forecast_fixture.h"
#include <chrono>
#include <iostream>
using namespace om;
using namespace income_fixture;
int main(){
    const auto root=std::filesystem::temp_directory_path()/("canary-income-oracle-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if(!std::filesystem::create_directory(root))return 1;
    {
        Fixture fixture(root/"fixture.db");
        std::string credits,rate,actual,future,power,price,fx,tariff,currencyFx;int64_t calibrationMs=0,userMs=0;
        while(std::cin>>credits>>rate>>calibrationMs>>actual>>future>>userMs>>power>>price>>fx>>tariff>>currencyFx){
            auto in=fixture.input;auto c=fixture.calibration;
            if(calibrationMs<1||calibrationMs>day||userMs<0||userMs>12*hour)return 2;
            auto &p=c.periods[0];p.beginUtcMs=p.endUtcMs-calibrationMs;p.work=work(in.binding,p.beginUtcMs,p.endUtcMs,rate.c_str());
            p.credits.totals.fromExclusiveMs=p.beginUtcMs-1;p.credits.totals.requestedMs=p.credits.totals.coveredMs=static_cast<uint64_t>(calibrationMs);
            p.credits.totals.knownPostedNet=decimal(credits);p.credits.totals.completePostedNet=decimal(credits);
            in.reward=calibrateReward(c);if(!in.reward)return 3;
            in.today.totals.knownPostedNet=decimal(actual);in.today.totals.completePostedNet=decimal(actual);
            const auto begin=in.nowUtcMs+1,end=fixture.begin+day;
            auto &segments=in.remaining.work.intervals;segments.clear();
            if(userMs)segments.push_back({begin,begin+userMs,ForecastWorkState::UserMining,decimal(future)});
            if(begin+userMs<end)segments.push_back({begin+userMs,end,ForecastWorkState::Paused,{}});
            in.tokenQuote->last=decimal(price);in.fxQuote->last=decimal(fx);
            in.electricity.fullDayPower.intervals[0].watts=decimal(power);in.electricity.plan.revisions[0].baseRatePerKwh=decimal(tariff);in.electricity.plan.currency="EUR";
            in.electricity.fx=ForecastCurrencyFx{ForecastQualification::SyntheticFixture,"EUR","fixture.currency-fx",decimal(currencyFx),in.nowUtcMs,120000};
            const auto value=incomeForecast(in);if(!value.todayTokens||!value.todayUsd||!value.netTodayUsd)return 4;
            for(const auto &r:{value.todayTokens,value.todayUsd,value.netTodayUsd}){std::string shown;if(!r->format(8,&shown))return 5;std::cout<<r->numerator.text()<<' '<<r->denominator.text()<<' '<<shown<<' ';}
            std::cout<<'\n';
        }
    }
    std::filesystem::remove_all(root);return 0;
}
