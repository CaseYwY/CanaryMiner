#include "income_forecast_fixture.h"
#include "product_test_util.h"
#include <iostream>
using namespace om;
using namespace income_fixture;
namespace {
std::string text(const std::optional<ForecastRatio> &r,unsigned places=8){std::string s;CHECK(r&&r->format(places,&s));return s;}
std::string lines(const IncomeForecast &v){std::string s;for(const auto &line:incomeForecastLines(v))s+=line+'\n';return s;}
void unavailable(const IncomeForecastInput &in,ForecastStatus status){const auto v=incomeForecast(in);CHECK(v.remainingStatus==status&&!v.expectedRemaining&&!v.todayTokens&&!v.netTodayUsd);}
void calendar(const CivilClock &clock,CivilDate date,int64_t elapsedHours){
    auto binding=credit_fixture::contract();const auto dateStart=civilDay(clock,date);CHECK(dateStart.status==CivilStatus::Ok);if(dateStart.status!=CivilStatus::Ok)return;
    binding.historyStartMs=dateStart.beginUtcMs-3*day;
    const auto start=incomeForecastWindows(clock,binding,dateStart.beginUtcMs);
    CHECK(start.status==ForecastStatus::Ok&&start.today.endUtcMs-start.today.beginUtcMs==elapsedHours*hour);
    CHECK(start.todayCredits.fromExclusiveMs==dateStart.beginUtcMs-1&&start.todayCredits.throughInclusiveMs==dateStart.beginUtcMs&&start.cutExclusiveMs==dateStart.beginUtcMs+1);
    CHECK(start.rolling24h.throughInclusiveMs-start.rolling24h.fromExclusiveMs==day&&start.cumulative.cumulative&&start.cumulative.fromExclusiveMs==0);
    const auto last=incomeForecastWindows(clock,binding,dateStart.endUtcMs-1),next=incomeForecastWindows(clock,binding,dateStart.endUtcMs);
    CHECK(last.cutExclusiveMs==last.today.endUtcMs&&next.today.beginUtcMs==dateStart.endUtcMs&&next.todayCredits.fromExclusiveMs==dateStart.endUtcMs-1);
}
}
int main(int argc,char **argv){
    const auto root=std::filesystem::path(makeScratchDir("income-forecast"));
    {
        Fixture f(root/"fixture.db");const auto &in=f.input;const auto v=incomeForecast(in);CHECK(v.status==ForecastStatus::Ok&&v.todayStatus==ForecastStatus::Ok);
        CHECK(v.todayActual.knownTokens->text()=="3"&&v.actual24h.knownTokens->text()=="23"&&v.actualCumulative.knownTokens->text()=="123");
        CHECK(in.reward->calibration().creditedTokens.text()=="120"&&in.reward->calibration().userTmacMilliseconds.text()=="172800000");
        CHECK(text(v.expectedRemaining)=="30.00000000"&&text(v.todayTokens)=="33.00000000");
        CHECK(text(v.todayUsdt,2)=="82.50"&&text(v.todayUsd,2)=="80.85"&&text(v.netTodayUsd,2)=="80.25");
        CHECK(v.userMs==6*hour&&v.donationMs==hour&&v.pausedMs==4*hour&&v.switchMs==hour);
        CHECK(v.actual24h.facts.requestedMs==day&&v.todayActual.facts.requestedMs==12*hour);
        const auto rendered=lines(v);CHECK(rendered.find("SYNTHETIC")!=std::string::npos&&rendered.find("GPU-board proxy")!=std::string::npos&&rendered.find("USDT is not USD")!=std::string::npos);
        CHECK(rendered.find(in.binding.scopeId)==std::string::npos&&rendered.find(in.reward->input().profileKey)==std::string::npos&&rendered.find(in.electricity.plan.key)==std::string::npos);
        CreditProcessRequest request;request.generation=request.ticket=1;request.file=root/"fixture.db";request.binding=in.binding;request.nowUtcMs=in.nowUtcMs;
        request.queries={v.windows.todayCredits,v.windows.rolling24h,v.windows.cumulative};std::string wire;CHECK(encodeCreditRequest(request,&wire));
        if(argc==2&&std::string(argv[1])=="--preview")std::cout<<rendered;
        // Revaluation changes references only. Missing/old FX never becomes a stablecoin peg.
        auto changed=in;changed.tokenQuote->last=decimal("5");auto p=incomeForecast(changed);
        CHECK(text(p.todayUsd,2)=="161.70"&&p.todayActual.knownTokens->text()=="3"&&text(p.todayTokens)=="33.00000000");
        changed.valuationUtcMs=in.nowUtcMs+60000;changed.nowMonoMs+=60000;
        for(auto *q:{&*changed.tokenQuote,&*changed.fxQuote}){q->observation.observedUtc+=60;q->observation.requestStartedUtc+=60;q->observation.receivedMonoMs+=60000;}
        p=incomeForecast(changed);CHECK(text(p.todayUsd,2)=="161.70"&&p.todayActual.facts.observedAtMs==in.today.totals.observedAtMs&&p.windows.nowUtcMs==in.nowUtcMs);
        changed.valuationUtcMs=in.nowUtcMs-1;p=incomeForecast(changed);CHECK(p.status==ForecastStatus::Clock&&!p.todayTokens);
        changed.valuationUtcMs=v.windows.today.endUtcMs;p=incomeForecast(changed);CHECK(p.status==ForecastStatus::Stale&&!p.todayTokens);
        changed=in;changed.valuationUtcMs=in.nowUtcMs+120001;p=incomeForecast(changed);CHECK(p.remainingStatus==ForecastStatus::Stale&&!p.todayTokens&&p.actual24h.knownTokens);
        changed=in;changed.fxQuote.reset();p=incomeForecast(changed);CHECK(!p.todayUsd&&!p.netTodayUsd&&text(p.todayUsdt,2)=="82.50");
        changed=in;changed.nowMonoMs+=120001;p=incomeForecast(changed);CHECK(!p.todayUsdt&&!p.todayUsd&&text(p.todayTokens)=="33.00000000");
        changed=in;changed.tokenQuote->sourceEventUtc=changed.tokenQuote->observation.observedUtc;p=incomeForecast(changed);CHECK(!p.todayUsd&&p.latest.tokenStatus==MarketFreshness::WrongIdentity);
        changed=in;changed.nowMonoMs=0;p=incomeForecast(changed);CHECK(p.latest.tokenStatus==MarketFreshness::InvalidClock&&!p.todayUsd);
        // Partial/unknown actual data can retain known facts and a separate remainder, never a complete today total.
        changed=in;auto &t=changed.today.totals;t.completeness=CreditCompleteness::Partial;t.completePostedNet.reset();t.coveredMs=0;
        p=incomeForecast(changed);CHECK(p.todayStatus==ForecastStatus::Partial&&!p.todayTokens&&!p.netTodayUsd&&p.expectedRemaining&&p.todayActual.knownTokens->text()=="3");
        changed.today.totals.knownPostedNet={};changed.today.totals.matchedRecords=0;changed.today.totals.completeness=CreditCompleteness::Unavailable;
        p=incomeForecast(changed);CHECK(!p.todayActual.knownTokens&&!p.todayTokens&&lines(p).find("Actual credited today: N/A")!=std::string::npos);
        changed=in;changed.today.binding.scopeId="different-worker";p=incomeForecast(changed);CHECK(p.todayActual.status==ForecastStatus::Attribution&&!p.todayTokens);
        changed=in;changed.today.totals.fromExclusiveMs++;p=incomeForecast(changed);CHECK(p.todayActual.status==ForecastStatus::Invalid&&!p.todayTokens);
        changed=in;changed.rolling24h.sequence++;p=incomeForecast(changed);CHECK(p.status==ForecastStatus::Attribution&&!p.todayTokens);
        changed=in;changed.today.sequence=changed.rolling24h.sequence=changed.cumulative.sequence=2;unavailable(changed,ForecastStatus::Stale);
        changed=in;changed.today.totals.knownPostedNet=decimal("-2");changed.today.totals.completePostedNet=decimal("-2");p=incomeForecast(changed);
        CHECK(p.todayActual.knownTokens->negative()&&text(p.todayTokens)=="28.00000000");
        // Complete future pauses are honest zero; missing slots, donation work and stale declarations are not.
        changed=in;changed.remaining.work.intervals={{v.windows.cutExclusiveMs,v.windows.today.endUtcMs,ForecastWorkState::Paused,{}}};p=incomeForecast(changed);
        CHECK(text(p.expectedRemaining)=="0.00000000"&&text(p.todayTokens)=="3.00000000"&&p.pausedMs==12*hour);
        changed=in;changed.remaining.work.intervals.erase(changed.remaining.work.intervals.begin());unavailable(changed,ForecastStatus::Coverage);
        changed=in;changed.remaining.work.intervals[1].userTmacsPerSecond=decimal("2");unavailable(changed,ForecastStatus::Invalid);
        changed=in;changed.remaining.work.profileKey="fixture.changed-profile";unavailable(changed,ForecastStatus::Attribution);
        changed=in;changed.remaining.work.binding.scopeGeneration="different-generation";unavailable(changed,ForecastStatus::Attribution);
        changed=in;changed.remaining.work.qualification=ForecastQualification::Unsupported;unavailable(changed,ForecastStatus::Unsupported);
        changed=in;changed.remaining.declaredUtcMs-=120001;unavailable(changed,ForecastStatus::Stale);
        changed=in;changed.remaining.declaredUtcMs++;unavailable(changed,ForecastStatus::Clock);
        changed=in;changed.reward.reset();unavailable(changed,ForecastStatus::Unavailable);
        // Calibrated complete zero income remains zero; incomplete, negative or mismatched work cannot train a model.
        auto c=f.calibration;ForecastStatus status=ForecastStatus::Ok;c.periods[0].credits.totals.knownPostedNet={};c.periods[0].credits.totals.completePostedNet=ExactDecimal{};
        changed=in;changed.reward=calibrateReward(c,&status);CHECK(changed.reward&&status==ForecastStatus::Ok);p=incomeForecast(changed);CHECK(text(p.expectedRemaining)=="0.00000000");
        c=f.calibration;c.periods={
            {f.begin-day,f.begin-12*hour,credits(f.ledger,in.binding,f.begin-day,f.begin-12*hour,f.now),work(in.binding,f.begin-day,f.begin-12*hour,"2")},
            {f.begin-12*hour,f.begin,credits(f.ledger,in.binding,f.begin-12*hour,f.begin,f.now),work(in.binding,f.begin-12*hour,f.begin,"4")}};
        changed=in;changed.reward=calibrateReward(c,&status);CHECK(changed.reward&&status==ForecastStatus::Ok);p=incomeForecast(changed);CHECK(text(p.expectedRemaining)=="20.00000000");
        c.periods[1].credits.snapshotId="fixture.different-read";CHECK(!calibrateReward(c,&status)&&status==ForecastStatus::Attribution);
        c=f.calibration;c.periods[0].beginUtcMs=c.periods[0].endUtcMs-4096;auto &bounded=c.periods[0];
        bounded.credits.totals.fromExclusiveMs=bounded.beginUtcMs-1;bounded.credits.totals.coveredMs=bounded.credits.totals.requestedMs=4096;
        bounded.work.intervals.clear();for(int64_t i=0;i<4096;++i)bounded.work.intervals.push_back({bounded.beginUtcMs+i,bounded.beginUtcMs+i+1,ForecastWorkState::UserMining,decimal("2")});
        CHECK(calibrateReward(c,&status)&&status==ForecastStatus::Ok);bounded.work.intervals.push_back(bounded.work.intervals.back());CHECK(!calibrateReward(c,&status)&&status==ForecastStatus::Capacity);
        c=f.calibration;c.periods[0].credits.totals.knownPostedNet=decimal(std::string(96,'9'));c.periods[0].credits.totals.completePostedNet=c.periods[0].credits.totals.knownPostedNet;
        changed=in;changed.reward=calibrateReward(c);CHECK(static_cast<bool>(changed.reward));unavailable(changed,ForecastStatus::Capacity);
        changed=in;changed.today.totals.knownPostedNet=decimal(std::string(96,'9'));changed.today.totals.completePostedNet=changed.today.totals.knownPostedNet;
        p=incomeForecast(changed);CHECK(p.todayStatus==ForecastStatus::Capacity&&!p.todayTokens&&!p.netTodayUsd);
        for(unsigned bad=0;bad<12;++bad){c=f.calibration;
            if(bad==0)c.earningAlignmentContract="unqualified-posted-time";
            if(bad==1)c.qualification=ForecastQualification::Unsupported;
            if(bad==2)c.periods[0].credits.binding.scopeId="other";
            if(bad==3)c.periods[0].credits.totals.completePostedNet.reset();
            if(bad==4)c.periods[0].work.intervals[0].endUtcMs--;
            if(bad==5)c.periods[0].work.intervals[0].userTmacsPerSecond={};
            if(bad==6){c.periods[0].credits.totals.knownPostedNet=decimal("-1");c.periods[0].credits.totals.completePostedNet=decimal("-1");}
            if(bad==7)c.validUntilUtcMs=c.calibratedUtcMs;
            if(bad==8)c.periods.push_back(c.periods[0]);
            if(bad==9)c.periods[0].credits.totals.observedAtMs++;
            if(bad==10)c.periods[0].credits.totals.requestedMs++;
            if(bad==11)c.periods[0].work.binding.scope=CreditScope::Account;
            CHECK(!calibrateReward(c,&status)&&status!=ForecastStatus::Ok);
        }
        c=f.calibration;c.calibratedUtcMs--;c.validUntilUtcMs=c.calibratedUtcMs+1;c.periods[0].credits.totals.observedAtMs=c.calibratedUtcMs;c.periods[0].credits.totals.sourceAsOfMs=c.calibratedUtcMs;
        changed=in;changed.reward=calibrateReward(c);CHECK(static_cast<bool>(changed.reward));changed.nowUtcMs++;unavailable(changed,ForecastStatus::Stale);
        // Configured cost visibility, signed tariff rates, source scope, exact horizon and independent currency FX.
        changed=in;changed.electricity.configured=false;p=incomeForecast(changed);CHECK(!p.electricityConfigured&&!p.netTodayUsd&&lines(p).find("Estimated net today")==std::string::npos);
        changed=in;changed.electricity.plan.revisions[0].baseRatePerKwh={};p=incomeForecast(changed);CHECK(text(p.netTodayUsd,2)=="80.85");
        changed=in;changed.electricity.plan.revisions[0].baseRatePerKwh=decimal("-0.25");p=incomeForecast(changed);CHECK(text(p.netTodayUsd,2)=="81.45");
        changed=in;changed.tokenQuote->last=decimal("0.01");changed.fxQuote->last=decimal("1");p=incomeForecast(changed);CHECK(text(p.netTodayUsd,2)=="-0.27");
        changed=in;changed.electricity.plan.currency="EUR";p=incomeForecast(changed);CHECK(!p.netTodayUsd&&p.electricityConfigured);
        changed.electricity.fx=ForecastCurrencyFx{ForecastQualification::SyntheticFixture,"EUR","fixture.fx",decimal("1.2"),in.nowUtcMs,120000};p=incomeForecast(changed);CHECK(text(p.netTodayUsd,2)=="80.13");
        changed.electricity.fx->observedUtcMs-=120001;p=incomeForecast(changed);CHECK(p.netStatus==ForecastStatus::Stale&&!p.netTodayUsd);
        for(unsigned bad=0;bad<6;++bad){changed=in;
            if(bad==0)changed.electricity.opening.throughUtcMs++;
            if(bad==1)changed.electricity.fullDayPower.intervals[0].endUtcMs--;
            if(bad==2)changed.electricity.attributedProduction.scopeId="unrelated-meter";
            if(bad==3)changed.electricity.fullDayPower.evidence.basis=PowerBasis::WholeMeterImport;
            if(bad==4)changed.electricity.plan.clock={CivilClockKind::FixedStandardOffset,"",3600};
            if(bad==5)changed.electricity.plan.origin=TariffOrigin::UserDeclaredOffline;
            p=incomeForecast(changed);CHECK(!p.netTodayUsd&&p.netStatus!=ForecastStatus::Ok);
        }
        changed=in;changed.binding.qualification=CreditQualification::Unsupported;p=incomeForecast(changed);CHECK(p.status==ForecastStatus::Unsupported&&!p.todayTokens);
        CHECK(lines(p).find("Estimated net today: N/A USD")!=std::string::npos);
        changed=in;changed.binding.scope=CreditScope::Account;changed.binding.accountScopeAcknowledged=true;changed.binding.scopeId="synthetic-account";
        for(auto *facts:{&changed.today,&changed.rolling24h,&changed.cumulative}){facts->binding=changed.binding;facts->totals.scope=CreditScope::Account;}
        changed.remaining.work.binding=changed.binding;changed.electricity.attributedProduction=changed.binding;
        c=f.calibration;c.binding=changed.binding;for(auto &period:c.periods){period.credits.binding=changed.binding;period.credits.totals.scope=CreditScope::Account;period.work.binding=changed.binding;}
        changed.reward=calibrateReward(c);p=incomeForecast(changed);CHECK(text(p.todayTokens)=="33.00000000"&&lines(p).find("acknowledged account scope")!=std::string::npos);
        changed.binding.accountScopeAcknowledged=false;CHECK(incomeForecast(changed).status==ForecastStatus::Unsupported);
        CHECK(lines(incomeForecast(changed)).find("acknowledged account scope")==std::string::npos);
        // Last included millisecond has no future duration: actual complete day needs no reward model.
        changed=in;changed.nowUtcMs=v.windows.today.endUtcMs-1;changed.reward.reset();
        auto &last=changed.today.totals;last.throughInclusiveMs=changed.nowUtcMs;last.sourceAsOfMs=last.observedAtMs=changed.nowUtcMs;last.requestedMs=last.coveredMs=day;
        changed.rolling24h.totals.sourceAsOfMs=changed.rolling24h.totals.observedAtMs=changed.nowUtcMs;
        changed.cumulative.totals.sourceAsOfMs=changed.cumulative.totals.observedAtMs=changed.nowUtcMs;
        p=incomeForecast(changed);CHECK(text(p.expectedRemaining)=="0.00000000"&&text(p.todayTokens)=="3.00000000");
        if(argc==2&&std::string(argv[1])!="--preview"){
            CHECK(configureCivilTimeData(std::filesystem::u8path(argv[1]),"2026e")==CivilStatus::Ok);
            calendar({CivilClockKind::NamedIana,"America/New_York",0},{2026,3,8},23);
            calendar({CivilClockKind::NamedIana,"America/New_York",0},{2026,11,1},25);
            const CivilClock ny{CivilClockKind::NamedIana,"America/New_York",0};
            {Fixture spring(root/"spring.db",ny,civilDay(ny,{2026,3,8}).beginUtcMs);const auto forecast=incomeForecast(spring.input);
                CHECK(forecast.windows.today.endUtcMs-forecast.windows.today.beginUtcMs==23*hour&&forecast.pausedMs==3*hour&&text(forecast.netTodayUsd,2)=="80.28");}
            {Fixture fall(root/"fall.db",ny,civilDay(ny,{2026,11,1}).beginUtcMs);const auto forecast=incomeForecast(fall.input);
                CHECK(forecast.windows.today.endUtcMs-forecast.windows.today.beginUtcMs==25*hour&&forecast.pausedMs==5*hour&&text(forecast.netTodayUsd,2)=="80.22");}
        }
        calendar({}, {2026,10,29},24);calendar({CivilClockKind::FixedStandardOffset,"",19800},{2026,10,29},24);
        CHECK(incomeForecastWindows({CivilClockKind::NamedIana,"Unavailable/Zone",0},in.binding,in.nowUtcMs).status==ForecastStatus::Unavailable);
        CHECK(incomeForecastWindows({},in.binding,0).status==ForecastStatus::Clock);
    }
    std::filesystem::remove_all(root);return finishTests("income forecast");
}
