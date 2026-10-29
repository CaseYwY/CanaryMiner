#include "income_forecast.h"
#include <algorithm>

namespace om {
namespace {
constexpr int64_t dayMs=86400000,lastUtcMs=253402300799999LL;
constexpr size_t maxIntervals=4096,maxPeriods=64;
bool instant(int64_t t){return t>=0&&t<=lastUtcMs;}
bool fixtureKey(const std::string &s){return s.size()>8&&s.size()<=128&&s.rfind("fixture.",0)==0&&std::all_of(s.begin(),s.end(),[](unsigned char c){return c>=33&&c<=126;});}
ExactDecimal number(uint64_t n){ExactDecimal d;ExactDecimal::parse(std::to_string(n),&d);return d;}
bool sameClock(const CivilClock &a,const CivilClock &b){return a.kind==b.kind&&a.zone==b.zone&&a.fixedOffsetSeconds==b.fixedOffsetSeconds;}
bool supported(const CreditSourceContract &c){return c.qualification==CreditQualification::SyntheticFixture&&validCreditSourceContract(c);}
bool snapshotKey(const std::string &s){return !s.empty()&&s.size()<=128&&std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c==':';});}
bool sameSnapshot(const ForecastCredits &a,const ForecastCredits &b){return a.sequence==b.sequence&&a.snapshotId==b.snapshotId&&a.totals.sourceAsOfMs==b.totals.sourceAsOfMs&&a.totals.observedAtMs==b.totals.observedAtMs;}
ForecastStatus creditsStatus(const ForecastCredits &value,const CreditSourceContract &binding,int64_t from,int64_t through,int64_t now){
    const auto &t=value.totals;
    if(!supported(value.binding)||!sameCreditSourceContract(value.binding,binding)||t.scope!=binding.scope||t.qualification!=binding.qualification)return ForecastStatus::Attribution;
    if(t.status!=CreditLedgerStatus::Ok)return t.status==CreditLedgerStatus::Clock?ForecastStatus::Clock:ForecastStatus::Unavailable;
    if((!value.sequence||value.sequence>static_cast<uint64_t>(INT64_MAX)||!snapshotKey(value.snapshotId))&&t.completeness!=CreditCompleteness::Unavailable)return ForecastStatus::Invalid;
    if(t.knownPostedNet.scale()>binding.amountScale)return ForecastStatus::Invalid;
    if(from==binding.historyStartMs-1&&t.knownPostedNet.negative())return ForecastStatus::Invalid;
    if(from<0||through<=from||through>now||t.fromExclusiveMs!=from||t.throughInclusiveMs!=through||t.requestedMs!=static_cast<uint64_t>(through-from)||t.coveredMs>t.requestedMs)return ForecastStatus::Invalid;
    if(!instant(t.sourceAsOfMs)||!instant(t.observedAtMs)||t.sourceAsOfMs>t.observedAtMs||t.observedAtMs>now)return ForecastStatus::Clock;
    if(t.completeness==CreditCompleteness::Complete){
        if(!t.completePostedNet||t.coveredMs!=t.requestedMs||t.sourceAsOfMs<through||from<binding.historyStartMs-1||t.knownPostedNet.compare(*t.completePostedNet))return ForecastStatus::Invalid;
        return ForecastStatus::Ok;
    }
    if(t.completePostedNet||t.coveredMs==t.requestedMs)return ForecastStatus::Invalid;
    if(t.completeness==CreditCompleteness::Partial)return t.coveredMs||t.matchedRecords?ForecastStatus::Partial:ForecastStatus::Invalid;
    return t.completeness==CreditCompleteness::Unavailable&&t.coveredMs==0&&t.matchedRecords==0&&t.knownPostedNet.zero()?ForecastStatus::Unavailable:ForecastStatus::Invalid;
}
ForecastStatus workIntegral(const ForecastWork &work,const CreditSourceContract &binding,const std::string &profile,int64_t begin,int64_t end,ExactDecimal &sum,IncomeForecast *view=nullptr){
    if(work.qualification!=ForecastQualification::SyntheticFixture)return ForecastStatus::Unsupported;
    if(!supported(work.binding)||!sameCreditSourceContract(work.binding,binding)||work.profileKey!=profile)return ForecastStatus::Attribution;
    if(!fixtureKey(profile)||!instant(begin)||!instant(end)||end<begin)return ForecastStatus::Invalid;
    if(work.intervals.size()>maxIntervals)return ForecastStatus::Capacity;
    int64_t cursor=begin;ExactDecimal total;
    uint64_t user=0,donation=0,paused=0,switching=0;
    for(const auto &i:work.intervals){
        if(i.beginUtcMs!=cursor||i.endUtcMs<=i.beginUtcMs||i.endUtcMs>end)return ForecastStatus::Coverage;
        if(i.userTmacsPerSecond.negative()||i.userTmacsPerSecond.digits()>38||i.userTmacsPerSecond.scale()>18)return ForecastStatus::Invalid;
        const auto duration=static_cast<uint64_t>(i.endUtcMs-i.beginUtcMs);
        if(i.state==ForecastWorkState::UserMining){ExactDecimal part;if(!i.userTmacsPerSecond.multiply(number(duration),&part)||!total.add(part,&total))return ForecastStatus::Capacity;user+=duration;}
        else{
            if(!i.userTmacsPerSecond.zero())return ForecastStatus::Invalid;
            if(i.state==ForecastWorkState::Donation)donation+=duration;
            else if(i.state==ForecastWorkState::Paused)paused+=duration;
            else if(i.state==ForecastWorkState::Switching)switching+=duration;
            else return ForecastStatus::Invalid;
        }
        cursor=i.endUtcMs;
    }
    if(cursor!=end)return ForecastStatus::Coverage;
    if(view){view->userMs=user;view->donationMs=donation;view->pausedMs=paused;view->switchMs=switching;}
    sum=total;return ForecastStatus::Ok;
}
std::optional<ForecastRatio> valued(const ForecastRatio &quantity,const std::optional<ExactDecimal> &price){
    if(!price||price->negative()||price->zero())return {};
    ForecastRatio result;result.denominator=quantity.denominator;
    if(!quantity.numerator.multiply(*price,&result.numerator))return {};
    return result;
}
ForecastProduction production(const ForecastCredits &c,const CreditSourceContract &binding,int64_t from,int64_t through,int64_t now,const MarketLatest &latest){
    ForecastProduction p;p.status=creditsStatus(c,binding,from,through,now);p.facts=c.totals;
    if(p.status!=ForecastStatus::Ok&&p.status!=ForecastStatus::Partial)return p;
    p.knownTokens=c.totals.knownPostedNet;p.complete=p.status==ForecastStatus::Ok;
    ForecastRatio r{*p.knownTokens,number(1)};p.usdt=valued(r,latest.tokenUsdt);p.usd=valued(r,latest.tokenUsd);return p;
}
ForecastStatus remaining(const IncomeForecastInput &in,IncomeForecast &out){
    const auto begin=out.windows.cutExclusiveMs,end=out.windows.today.endUtcMs;
    if(begin==end){out.expectedRemaining=ForecastRatio{number(0),number(1)};return ForecastStatus::Ok;}
    if(!in.reward)return ForecastStatus::Unavailable;
    const auto &model=in.reward->input();const auto &cal=in.reward->calibration();
    if(!sameCreditSourceContract(model.binding,in.binding))return ForecastStatus::Attribution;
    if(!sameSnapshot(model.periods.front().credits,in.today))return ForecastStatus::Stale;
    if(in.nowUtcMs<model.calibratedUtcMs||!instant(in.remaining.declaredUtcMs)||in.remaining.declaredUtcMs>in.nowUtcMs)return ForecastStatus::Clock;
    if(out.valuationUtcMs>model.validUntilUtcMs)return ForecastStatus::Stale;
    if(!fixtureKey(in.remaining.assumptionKey)||!in.remaining.maxAgeMs||in.remaining.maxAgeMs>static_cast<uint64_t>(dayMs))return ForecastStatus::Invalid;
    if(static_cast<uint64_t>(out.valuationUtcMs-in.remaining.declaredUtcMs)>in.remaining.maxAgeMs)return ForecastStatus::Stale;
    ExactDecimal work;const auto status=workIntegral(in.remaining.work,in.binding,model.profileKey,begin,end,work,&out);
    if(status!=ForecastStatus::Ok)return status;
    ForecastRatio r;r.denominator=cal.userTmacMilliseconds;
    if(!cal.creditedTokens.multiply(work,&r.numerator))return ForecastStatus::Capacity;
    out.expectedRemaining=r;return ForecastStatus::Ok;
}
ForecastStatus cost(const IncomeForecastInput &in,IncomeForecast &out){
    const auto &e=in.electricity;
    if(!e.configured)return ForecastStatus::Unavailable;
    if(!supported(e.attributedProduction)||!sameCreditSourceContract(e.attributedProduction,in.binding))return ForecastStatus::Attribution;
    out.electricityBasis=e.fullDayPower.evidence.basis;
    if(e.plan.origin!=TariffOrigin::SyntheticFixture||e.fullDayPower.evidence.origin!=PowerOrigin::SyntheticFixture)return ForecastStatus::Unsupported;
    if(out.electricityBasis!=PowerBasis::GpuBoard&&out.electricityBasis!=PowerBasis::WholeSystemWall&&out.electricityBasis!=PowerBasis::DirectIncrementalWall)return ForecastStatus::Unsupported;
    if(!sameClock(e.plan.clock,in.clock)||e.opening.throughUtcMs!=out.windows.today.beginUtcMs)return ForecastStatus::Coverage;
    if(e.fullDayPower.intervals.size()>maxIntervals)return ForecastStatus::Capacity;
    out.electricity=evaluateBill(e.plan,e.opening,e.fullDayPower,out.windows.today.endUtcMs,in.nowUtcMs);
    if(out.electricity.status!=ElectricityStatus::Ok||!out.electricity.charge)return out.electricity.status==ElectricityStatus::Unsupported?ForecastStatus::Unsupported:ForecastStatus::Unavailable;
    ElectricityAmount value=*out.electricity.charge;
    if(e.plan.currency!="USD"){
        if(!e.fx)return ForecastStatus::Unavailable;
        const auto &f=*e.fx;
        if(f.qualification!=ForecastQualification::SyntheticFixture||!fixtureKey(f.source))return ForecastStatus::Unsupported;
        if(f.baseCurrency!=e.plan.currency||f.usdPerUnit.zero()||f.usdPerUnit.negative()||f.usdPerUnit.digits()>38||f.usdPerUnit.scale()>18||!f.maxAgeMs||f.maxAgeMs>static_cast<uint64_t>(dayMs))return ForecastStatus::Invalid;
        if(!instant(f.observedUtcMs)||f.observedUtcMs>out.valuationUtcMs)return ForecastStatus::Clock;
        if(static_cast<uint64_t>(out.valuationUtcMs-f.observedUtcMs)>f.maxAgeMs)return ForecastStatus::Stale;
        if(!value.numerator.multiply(f.usdPerUnit,&value.numerator))return ForecastStatus::Capacity;
    }
    out.electricityUsd=value;
    if(!out.todayUsd)return ForecastStatus::Unavailable;
    ForecastRatio net;ExactDecimal revenue,expense;
    if(!out.todayUsd->numerator.multiply(number(wattMsPerKwh),&revenue)||!value.numerator.multiply(out.todayUsd->denominator,&expense)||
       !revenue.subtract(expense,&net.numerator)||!out.todayUsd->denominator.multiply(number(wattMsPerKwh),&net.denominator))return ForecastStatus::Capacity;
    out.netTodayUsd=net;return ForecastStatus::Ok;
}
std::string text(const std::optional<ForecastRatio> &value,unsigned places){std::string out;return value&&value->format(places,&out)?out:"N/A";}
}
const char *forecastStatusName(ForecastStatus s){switch(s){
    case ForecastStatus::Ok:return "available";case ForecastStatus::Partial:return "partial";case ForecastStatus::Unavailable:return "unavailable";
    case ForecastStatus::Unsupported:return "unsupported";case ForecastStatus::Invalid:return "invalid";case ForecastStatus::Attribution:return "attribution mismatch";
    case ForecastStatus::Coverage:return "coverage mismatch";case ForecastStatus::Stale:return "stale";case ForecastStatus::Clock:return "clock fault";case ForecastStatus::Capacity:return "arithmetic/capacity limit";}return "invalid";}
ForecastWindows incomeForecastWindows(const CivilClock &clock,const CreditSourceContract &binding,int64_t now){
    ForecastWindows out;out.clock=clock;out.nowUtcMs=now;
    if(!supported(binding)){out.status=ForecastStatus::Unsupported;return out;}
    if(!instant(now)||now<dayMs||binding.historyStartMs>now){out.status=ForecastStatus::Clock;return out;}
    out.today=civilDayAt(clock,now);
    if(out.today.status!=CivilStatus::Ok||out.today.beginUtcMs<=0||out.today.endUtcMs<=now||out.today.endUtcMs-out.today.beginUtcMs>2*dayMs){out.status=ForecastStatus::Unavailable;return out;}
    out.cutExclusiveMs=now+1;out.todayCredits={false,out.today.beginUtcMs-1,now};out.rolling24h={false,now-dayMs,now};
    out.cumulative={true,0,now};out.status=ForecastStatus::Ok;return out;
}
std::shared_ptr<const RewardModel> calibrateReward(const RewardCalibrationInput &input,ForecastStatus *status){
    const auto fail=[&](ForecastStatus s)->std::shared_ptr<const RewardModel>{if(status)*status=s;return {};};
    if(input.qualification!=ForecastQualification::SyntheticFixture||!supported(input.binding))return fail(ForecastStatus::Unsupported);
    if(!fixtureKey(input.modelKey)||!fixtureKey(input.profileKey)||!fixtureKey(input.earningAlignmentContract)||input.periods.empty())return fail(ForecastStatus::Invalid);
    if(input.periods.size()>maxPeriods)return fail(ForecastStatus::Capacity);
    if(!instant(input.calibratedUtcMs)||!instant(input.validUntilUtcMs)||input.validUntilUtcMs<=input.calibratedUtcMs||input.validUntilUtcMs-input.calibratedUtcMs>30*dayMs)return fail(ForecastStatus::Clock);
    RewardCalibration result;int64_t previous=0;size_t intervals=0;
    for(const auto &p:input.periods){
        if(!instant(p.beginUtcMs)||!instant(p.endUtcMs)||p.endUtcMs<=p.beginUtcMs||p.beginUtcMs<previous||p.endUtcMs-1>input.calibratedUtcMs||p.endUtcMs-p.beginUtcMs>30*dayMs)return fail(ForecastStatus::Clock);
        intervals+=p.work.intervals.size();if(intervals>maxIntervals)return fail(ForecastStatus::Capacity);
        const auto s=creditsStatus(p.credits,input.binding,p.beginUtcMs-1,p.endUtcMs-1,input.calibratedUtcMs);
        if(s!=ForecastStatus::Ok)return fail(s==ForecastStatus::Partial?ForecastStatus::Coverage:s);
        if(!sameSnapshot(p.credits,input.periods.front().credits))return fail(ForecastStatus::Attribution);
        if(p.credits.totals.knownPostedNet.negative())return fail(ForecastStatus::Invalid);
        ExactDecimal work;const auto w=workIntegral(p.work,input.binding,input.profileKey,p.beginUtcMs,p.endUtcMs,work);
        if(w!=ForecastStatus::Ok)return fail(w);
        if(work.zero()&&!p.credits.totals.knownPostedNet.zero())return fail(ForecastStatus::Coverage);
        if(!result.userTmacMilliseconds.add(work,&result.userTmacMilliseconds)||!result.creditedTokens.add(p.credits.totals.knownPostedNet,&result.creditedTokens))return fail(ForecastStatus::Capacity);
        result.coveredMs+=static_cast<uint64_t>(p.endUtcMs-p.beginUtcMs);previous=p.endUtcMs;
    }
    if(result.userTmacMilliseconds.zero())return fail(ForecastStatus::Unavailable);
    result.periods=input.periods.size();result.status=ForecastStatus::Ok;
    if(status)*status=ForecastStatus::Ok;
    return std::shared_ptr<const RewardModel>(new RewardModel(input,std::move(result)));
}
IncomeForecast incomeForecast(const IncomeForecastInput &in){
    IncomeForecast out;out.electricityConfigured=in.electricity.configured;out.scope=in.binding.scope;
    out.windows=incomeForecastWindows(in.clock,in.binding,in.nowUtcMs);out.status=out.windows.status;if(out.status!=ForecastStatus::Ok)return out;
    out.valuationUtcMs=in.valuationUtcMs.value_or(in.nowUtcMs);
    if(!instant(out.valuationUtcMs)||out.valuationUtcMs<in.nowUtcMs){out.status=ForecastStatus::Clock;return out;}
    if(out.valuationUtcMs>=out.windows.today.endUtcMs){out.status=ForecastStatus::Stale;return out;}
    if(!sameSnapshot(in.today,in.rolling24h)||!sameSnapshot(in.today,in.cumulative)){out.status=ForecastStatus::Attribution;return out;}
    out.latest=latestMarketReference(in.tokenQuote?&*in.tokenQuote:nullptr,in.fxQuote?&*in.fxQuote:nullptr,static_cast<uint64_t>(out.valuationUtcMs/1000),in.nowMonoMs,in.marketPolicy);
    const auto &w=out.windows;
    out.todayActual=production(in.today,in.binding,w.todayCredits.fromExclusiveMs,in.nowUtcMs,in.nowUtcMs,out.latest);
    out.actual24h=production(in.rolling24h,in.binding,w.rolling24h.fromExclusiveMs,in.nowUtcMs,in.nowUtcMs,out.latest);
    out.actualCumulative=production(in.cumulative,in.binding,in.binding.historyStartMs-1,in.nowUtcMs,in.nowUtcMs,out.latest);
    out.remainingStatus=remaining(in,out);
    out.todayStatus=out.todayActual.complete?out.remainingStatus:out.todayActual.status;
    if(out.todayActual.complete&&out.expectedRemaining){
        auto total=*out.expectedRemaining;ExactDecimal actual;
        if(!out.todayActual.knownTokens->multiply(total.denominator,&actual)||!actual.add(total.numerator,&total.numerator))out.todayStatus=ForecastStatus::Capacity;
        else{out.todayTokens=total;out.todayUsdt=valued(total,out.latest.tokenUsdt);out.todayUsd=valued(total,out.latest.tokenUsd);out.todayStatus=ForecastStatus::Ok;}
    }
    out.netStatus=cost(in,out);return out;
}
std::vector<std::string> incomeForecastLines(const IncomeForecast &v){
    std::vector<std::string> lines={"SYNTHETIC FORECAST CONTRACT - no real income source is qualified.",
        std::string("PRL mainnet / posted net of pool charges / ")+(v.status!=ForecastStatus::Ok?"scope unavailable":v.scope==CreditScope::Account?"acknowledged account scope":"worker scope")};
    if(v.status!=ForecastStatus::Ok){
        lines.push_back("Income: N/A ("+std::string(forecastStatusName(v.status))+")");
        if(v.electricityConfigured)lines.push_back("Estimated net today: N/A USD ("+std::string(forecastStatusName(v.status))+")");
        return lines;
    }
    const auto &w=v.windows;lines.push_back("Calendar day UTC ms ["+std::to_string(w.today.beginUtcMs)+","+std::to_string(w.today.endUtcMs)+") | as of "+std::to_string(w.nowUtcMs));
    lines.push_back("Clock: "+std::string(w.clock.kind==CivilClockKind::Utc?"explicit UTC":w.clock.kind==CivilClockKind::FixedStandardOffset?"explicit standard offset "+std::to_string(w.clock.fixedOffsetSeconds)+"s":w.clock.zone)+" | data "+w.today.databaseVersion);
    auto actual=[&](const char *label,const ForecastProduction &p){std::string n;if(p.knownTokens)p.knownTokens->formatRatio(1,8,&n);
        const bool known=p.status==ForecastStatus::Ok||p.status==ForecastStatus::Partial;
        lines.push_back(std::string(label)+": "+(n.empty()?"N/A":n+" PRL")+" | "+forecastStatusName(p.status)+" | coverage "+(known?std::to_string(p.facts.coveredMs)+"/"+std::to_string(p.facts.requestedMs)+" ms":"N/A"));
        if(known)lines.push_back("  Posted UTC ms ("+std::to_string(p.facts.fromExclusiveMs)+","+std::to_string(p.facts.throughInclusiveMs)+"] | source as of "+std::to_string(p.facts.sourceAsOfMs));
        lines.push_back("  Production at latest price: "+text(p.usdt,2)+" USDT | "+text(p.usd,2)+" USD");};
    actual("Actual credited today",v.todayActual);actual("Actual credited 24h",v.actual24h);actual("Actual credited cumulative",v.actualCumulative);
    lines.push_back("Remaining today estimate: "+text(v.expectedRemaining,8)+" PRL | "+forecastStatusName(v.remainingStatus));
    lines.push_back("Today forecast: "+text(v.todayTokens,8)+" PRL | at latest price "+text(v.todayUsdt,2)+" USDT / "+text(v.todayUsd,2)+" USD | "+forecastStatusName(v.todayStatus));
    if(v.remainingStatus==ForecastStatus::Ok)lines.push_back("Future milliseconds: user "+std::to_string(v.userMs)+" / donation "+std::to_string(v.donationMs)+" / pause "+std::to_string(v.pausedMs)+" / switching "+std::to_string(v.switchMs));
    else lines.push_back("Future schedule: N/A ("+std::string(forecastStatusName(v.remainingStatus))+")");
    lines.push_back("Latest quote observation ages ms: token "+(v.latest.tokenObservationAgeMs?std::to_string(*v.latest.tokenObservationAgeMs):"N/A")+" / FX "+(v.latest.fxObservationAgeMs?std::to_string(*v.latest.fxObservationAgeMs):"N/A")+"; USDT is not USD.");
    lines.push_back("Reference valuation UTC ms "+std::to_string(v.valuationUtcMs)+"; report/credit instants are unchanged.");
    lines.push_back("Reference series: Gate PEARL_USDT / Kraken USDTZUSD; observation time, not a qualified trade-event time.");
    if(v.electricityConfigured){const char *basis=v.electricityBasis==PowerBasis::GpuBoard?"GPU-board proxy; not whole-PC":v.electricityBasis==PowerBasis::WholeSystemWall?"whole-system estimate; not isolated mining increment":v.electricityBasis==PowerBasis::DirectIncrementalWall?"matched incremental-wall estimate":"unsupported energy basis";
        lines.push_back("Estimated net today: "+text(v.netTodayUsd,2)+" USD | "+forecastStatusName(v.netStatus)+" | "+basis);
        lines.push_back("Electricity is an explicit full-day scenario; not an accrued actual utility bill.");}
    lines.push_back("Reward assumes qualified earning/credit timing and a complete future schedule; fees/rejections/donation are not deducted twice.");
    lines.push_back("Fiat values are production references; sales, dollar receipts and current holdings are unknown.");return lines;
}
FinancialView incomeForecastDashboardView(const IncomeForecast &v){FinancialView out;out.enabled=true;out.exactLines=incomeForecastLines(v);return out;}
} // namespace om
