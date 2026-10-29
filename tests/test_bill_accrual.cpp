#include "electricity_fixture.h"
#include "product_test_util.h"
#include <algorithm>
using namespace electricity_fixture;
namespace {
void compare(const TariffPlan &plan,const BillOpening &opening,const PowerTrace &trace,const std::vector<int64_t> &cuts){
    const auto full=evaluateBill(plan,opening,trace,opening.cycleEndUtcMs,opening.cycleEndUtcMs);CHECK(full.status==ElectricityStatus::Ok);if(!full.charge)return;
    auto state=opening;ExactDecimal sum;
    for(const auto end:cuts){PowerTrace part;part.evidence=trace.evidence;
        for(const auto &i:trace.intervals){const auto begin=(std::max)(i.beginUtcMs,state.throughUtcMs),through=(std::min)(i.endUtcMs,end);if(through>begin)part.intervals.push_back({begin,through,i.watts});}
        const auto result=accrueBill(plan,state,part,end,end);
        CHECK(result.status==ElectricityStatus::Ok&&result.ending&&result.provisionalChange&&result.provisionalTotal);if(!result.ending)return;
        CHECK(result.settledTotal.has_value()==(end==opening.cycleEndUtcMs));CHECK(sum.add(result.provisionalChange->numerator,&sum));state=*result.ending;
        if(result.settledTotal)CHECK(result.settledTotal->numerator.compare(full.charge->numerator)==0);
    }
    CHECK(sum.compare(full.charge->numerator)==0&&state.accruedSubtotal.numerator.compare(full.ending->accruedSubtotal.numerator)==0&&state.importedKwh.numerator.compare(full.ending->importedKwh.numerator)==0);
}
}
int main(int argc,char **argv){
    auto p=plan(T,T+2*hour);auto s=opening(T,T+2*hour);
    for(auto mode:{TierMode::None,TierMode::Progressive,TierMode::AllUnits})for(bool floor:{false,true}){
        p=plan(T,T+2*hour);if(mode!=TierMode::None)tier(p,mode);
        p.minimumCycleSubtotal=floor?std::optional<ExactDecimal>(d("180")):std::nullopt;p.subtotalMultiplier=d("1.123456789012345678");
        p.revisions[0].baseRatePerKwh=d("-0.15");p.cycleCharge=d("2");p.revisions[0].chargePerCivilDay=d("1.5");
        compare(p,s,load("60000",T,T+2*hour),{T+1,T+hour/2,T+hour,T+2*hour-1,T+2*hour});
    }
    // A floor is deferred, not charged once per batch; all-units changes reprice the complete accumulated register.
    p=plan(T,T+2*hour);p.minimumCycleSubtotal=d("10");p.revisions[0].baseRatePerKwh=d("1");
    auto first=accrueBill(p,s,load("1000",T,T+hour),T+hour,T+hour);CHECK(first.settlementDependent&&!first.settledTotal&&display(*first.provisionalTotal)=="1.000000");
    auto second=accrueBill(p,*first.ending,load("1000",T+hour,T+2*hour),T+2*hour,T+2*hour);CHECK(!second.settlementDependent&&display(*second.settledTotal)=="10.000000"&&display(*second.provisionalChange)=="9.000000");
    CHECK(evaluateBill(p,s,load("1000",T,T+hour),T+hour,T+hour).status==ElectricityStatus::Unsupported);
    p=plan(T,T+2*hour);tier(p,TierMode::AllUnits);
    first=accrueBill(p,s,load("100000",T,T+hour),T+hour,T+hour);second=accrueBill(p,*first.ending,load("1000",T+hour,T+2*hour),T+2*hour,T+2*hour);
    CHECK(first.settlementDependent&&display(*first.provisionalTotal)=="100.000000"&&display(*second.provisionalChange)=="102.000000"&&display(*second.settledTotal)=="202.000000");
    // Signed tariffs and downward all-units repricing remain signed exact amounts.
    p.revisions[0].bands[1].ratePerKwh=d("0.5");
    compare(p,s,load("60000",T,T+2*hour),{T+hour,T+2*hour});
    // The board-energy proxy can continue its own energy-only prefix, but cannot consume household allowances or fees.
    p=plan(T,T+2*hour);s.evidence.basis=PowerBasis::GpuBoard;auto board=load("123.45",T,T+2*hour);board.evidence=s.evidence;
    compare(p,s,board,{T+1,T+hour,T+2*hour});
    first=accrueBill(p,s,{board.evidence,{{T,T+hour,d("123.45")}}},T+hour,T+hour);
    CHECK(evaluateBill(p,*first.ending,{board.evidence,{{T+hour,T+2*hour,d("123.45")}}},T+2*hour,T+2*hour).status==ElectricityStatus::Unsupported);
    p.cycleCharge=d("1");CHECK(accrueBill(p,s,board,T+2*hour,T+2*hour).status==ElectricityStatus::Unsupported);
    s=opening(T,T+day+hour);p=plan(T,T+day+hour);p.cycleCharge=d("5");p.revisions[0].chargePerCivilDay=d("4");
    compare(p,s,load("0",T,T+day+hour),{T+1,T+hour,T+day,T+day+1,T+day+hour});
    // Effective revisions and dynamic rates retain the historical decision instant and exact boundary cuts.
    s=opening(T,T+2*hour);p=plan(T,T+2*hour);tier(p,TierMode::Progressive);auto rev=p.revisions[0];rev.key="fixture.next";rev.beginUtcMs=T+hour;rev.bands[1].ratePerKwh=d("4");p.revisions[0].endUtcMs=T+hour;p.revisions.push_back(rev);
    compare(p,s,load("60000",T,T+2*hour),{T+hour-1,T+hour,T+hour+1,T+2*hour});
    p=plan(T,T+2*hour);p.revisions[0].timeMode=TimeRateMode::Dynamic;p.revisions[0].baseRatePerKwh={};p.revisions[0].dynamic={{T,T+hour,T,d("-0.1")},{T+hour,T+2*hour,T+hour,d("2.3")}};
    compare(p,s,load("1234.56789",T,T+2*hour),{T+1,T+hour,T+hour+1,T+2*hour});
    auto bad=load("1000",T+1,T+hour);CHECK(!accrueBill(p,s,bad,T+hour,T+hour).ending);
    CHECK(accrueBill(p,s,load("1000",T,T+hour),T+hour,T).status==ElectricityStatus::NotYetKnown);
    p.revisions[0].dynamic[0].publishedUtcMs=T+hour+1;CHECK(accrueBill(p,s,load("1000",T,T+hour),T+hour,T+hour).status==ElectricityStatus::NotYetKnown);
    p.revisions[0].dynamic[0].publishedUtcMs=T;p.revisions[0].dynamic[0].endUtcMs=T+hour-1;CHECK(accrueBill(p,s,load("0",T,T+hour),T+hour,T+hour).status==ElectricityStatus::MissingPrice);
    if(argc==2){CHECK(configureCivilTimeData(std::filesystem::u8path(argv[1]),"2026e")==CivilStatus::Ok);
        const CivilClock ny{CivilClockKind::NamedIana,"America/New_York",0};
        for(const auto date:{CivilDate{2026,3,8},CivilDate{2026,11,1}}){const auto civil=civilDay(ny,date);CHECK(civil.status==CivilStatus::Ok);
            if(civil.status!=CivilStatus::Ok)continue;
            s=opening(civil.beginUtcMs,civil.endUtcMs);p=plan(civil.beginUtcMs,civil.endUtcMs);p.clock=ny;tier(p,TierMode::AllUnits);p.minimumCycleSubtotal=d("30");p.revisions[0].chargePerCivilDay=d("4");
            p.revisions[0].timeMode=TimeRateMode::Calendar;CalendarRate peak;peak.key="fixture.fold";peak.beginMinute=90;peak.endMinute=180;peak.ratePerKwh=d("2");p.revisions[0].calendar={peak};
            compare(p,s,load("1000",civil.beginUtcMs,civil.endUtcMs),{civil.beginUtcMs+hour,civil.beginUtcMs+2*hour,civil.beginUtcMs+3*hour,civil.endUtcMs-1,civil.endUtcMs});}}
    return finishTests("bill accrual");
}
