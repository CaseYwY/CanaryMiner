// Deterministic synthetic facts/work/power only. No external feed or device identifiers.
#pragma once
#include "product/income_forecast.h"
#include "credit_fixture.h"
#include <cstdlib>
namespace income_fixture {
using namespace om;
constexpr int64_t hour=3600000,day=24*hour;
inline ExactDecimal decimal(const std::string &s){ExactDecimal d;if(!ExactDecimal::parse(s,&d))std::abort();return d;}
inline ForecastWork work(const CreditSourceContract &binding,int64_t begin,int64_t end,const char *rate="2"){
    ForecastWork w;w.qualification=ForecastQualification::SyntheticFixture;w.binding=binding;w.profileKey="fixture.local-measured-profile";
    if(end>begin)w.intervals.push_back({begin,end,ForecastWorkState::UserMining,decimal(rate)});
    return w;
}
inline MarketQuote quote(MarketSeries series,int64_t now,const char *price){
    MarketQuote q;q.observation={series,MarketOrigin::SyntheticFixture,static_cast<uint64_t>(now/1000),static_cast<uint64_t>(now/1000),10000,1};q.last=decimal(price);return q;
}
inline ForecastElectricity electricity(const IncomeForecastInput &in,const CivilDay &d,const char *rate="0.25"){
    ForecastElectricity e;e.configured=true;e.attributedProduction=in.binding;
    e.plan.declared=true;e.plan.origin=TariffOrigin::SyntheticFixture;e.plan.key="fixture.private-plan";e.plan.territory="ZZ";e.plan.supplier="fixture.supplier";e.plan.product="fixture.energy-only";
    e.plan.currency="USD";e.plan.clock=in.clock;e.plan.rounding=TariffRounding::FinalDisplayOnly;e.plan.subtotalMultiplier=decimal("1");
    TariffRevision r;r.key="fixture.revision";r.beginUtcMs=d.beginUtcMs;r.endUtcMs=d.endUtcMs;r.publishedUtcMs=d.beginUtcMs;r.baseRatePerKwh=decimal(rate);e.plan.revisions={r};
    e.opening.declared=true;e.opening.planKey=e.plan.key;e.opening.cycleBeginUtcMs=e.opening.throughUtcMs=d.beginUtcMs;e.opening.cycleEndUtcMs=d.endUtcMs;
    e.opening.evidence={PowerBasis::GpuBoard,PowerOrigin::SyntheticFixture,"fixture.private-board-scope","fixture.power"};
    e.fullDayPower.evidence=e.opening.evidence;e.fullDayPower.intervals={{d.beginUtcMs,d.endUtcMs,decimal("100")}};return e;
}
inline ForecastCredits credits(CreditLedger &db,const CreditSourceContract &c,int64_t begin,int64_t end,int64_t now){return {c,db.query(begin-1,end-1,now),1,"fixture.snapshot"};}
struct Fixture {
    CreditLedger ledger;
    IncomeForecastInput input;
    RewardCalibrationInput calibration;
    int64_t begin,now;
    explicit Fixture(const std::filesystem::path &file,CivilClock clock={},int64_t beginUtcMs=credit_fixture::T/day*day):begin(beginUtcMs),now(begin+12*hour-1){
        input.clock=std::move(clock);input.binding=credit_fixture::contract();input.binding.historyStartMs=begin-3*day;input.nowUtcMs=now;input.nowMonoMs=10000;
        if(ledger.open(file,input.binding,now)!=CreditLedgerStatus::Ok)std::abort();
        CreditBatch b;b.binding=input.binding;b.sequence=1;b.snapshotId="fixture.snapshot";b.sourceAsOfMs=b.observedAtMs=now;b.coverage={{input.binding.historyStartMs-1,now}};
        b.records={credit_fixture::record("cutoff",1,now-day,"100"),credit_fixture::record("prior-day",1,begin-1,"20"),credit_fixture::record("midnight",1,begin,"1"),credit_fixture::record("current",1,now,"2")};
        if(ledger.apply(b,now)!=CreditLedgerStatus::Ok)std::abort();
        input.today=credits(ledger,input.binding,begin,now+1,now);input.rolling24h=credits(ledger,input.binding,now-day+1,now+1,now);
        input.cumulative={input.binding,ledger.cumulative(now,now),1,"fixture.snapshot"};
        calibration.qualification=ForecastQualification::SyntheticFixture;calibration.binding=input.binding;calibration.modelKey="fixture.reward-model";
        calibration.profileKey="fixture.local-measured-profile";calibration.earningAlignmentContract="fixture.complete-earning-and-posting-aligned-v1";
        calibration.calibratedUtcMs=now;calibration.validUntilUtcMs=now+day;
        calibration.periods={{begin-day,begin,credits(ledger,input.binding,begin-day,begin,now),work(input.binding,begin-day,begin)}};
        input.reward=calibrateReward(calibration);if(!input.reward)std::abort();
        const auto d=civilDayAt(input.clock,now);input.remaining.work=work(input.binding,now+1,d.endUtcMs);auto &segments=input.remaining.work.intervals;
        segments={{now+1,begin+18*hour,ForecastWorkState::UserMining,decimal("2")},
                  {begin+18*hour,begin+19*hour,ForecastWorkState::Donation,{}},
                  {begin+19*hour,d.endUtcMs-hour,ForecastWorkState::Paused,{}},
                  {d.endUtcMs-hour,d.endUtcMs,ForecastWorkState::Switching,{}}};
        input.remaining.assumptionKey="fixture.declared-schedule";input.remaining.declaredUtcMs=now;
        input.tokenQuote=quote(MarketSeries::GatePearlMainnetUsdtV1,now,"2.5");input.fxQuote=quote(MarketSeries::KrakenUsdtUsdV1,now,"0.98");
        input.electricity=electricity(input,d);
    }
};
} // namespace income_fixture
