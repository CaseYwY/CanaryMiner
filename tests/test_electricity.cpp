#include "electricity_fixture.h"
#include "product_test_util.h"
using namespace electricity_fixture;
namespace {
void charge(const BillEstimate &b, const std::string &amount) {
    CHECK(b.status == ElectricityStatus::Ok && b.charge && b.ending);
    CHECK(b.charge && display(*b.charge) == amount);
}
void delta(const CounterfactualBill &b, const std::string &amount) {
    CHECK(b.status == ElectricityStatus::Ok && b.incrementalCharge);
    CHECK(b.incrementalCharge && display(*b.incrementalCharge) == amount);
}
int64_t dateUtc(CivilDate date) { return civilResolve({},date,0).utcMs.value(); }
}
int main(int argc, char **argv) {
    // Exact W*ms, not nominal polling duration, kW/kWh guessing, extrapolated last sample or float money.
    auto e = evidence(); e.basis = PowerBasis::GpuBoard;
    auto energy = sampleHoldEnergy(e,{{T,100,d("2000")},{T+1800000,1800100,d("2000")}},T,T+1800000,1800000);
    CHECK(energy.status == ElectricityStatus::Ok && energy.completeKwh && display(*energy.completeKwh) == "1.000000");
    CHECK(energy.coveredTrace.evidence.basis == PowerBasis::GpuBoard);
    auto p = plan(T,T+1800000); auto s = opening(T,T+1800000);
    s.evidence = e;
    charge(evaluateBill(p,s,energy.coveredTrace,T+1800000,T),"3.000000"); // T01
    auto irregular = sampleHoldEnergy(e,{{T,0,d("1")},{T+7,7,d("2")},{T+31,31,d("9999")}},T+2,T+30,100);
    CHECK(irregular.status == ElectricityStatus::Ok && irregular.knownKwh.numerator.text() == "51");
    CHECK(display(*irregular.completeKwh,12) == "0.000000014167");
    auto gaps = sampleHoldEnergy(e,{{T,0,d("100")},{T+10,10,std::nullopt},{T+20,20,d("0")},{T+30,30,d("1")}},T,T+40,20);
    CHECK(gaps.status == ElectricityStatus::Partial && gaps.coveredMs == 20 && !gaps.completeKwh && gaps.coveredTrace.intervals.size() == 2);
    auto zero = sampleHoldEnergy(e,{{T,0,d("0")},{T+10,10,std::nullopt}},T,T+10,10);
    CHECK(zero.status == ElectricityStatus::Ok && zero.completeKwh && zero.completeKwh->numerator.zero());
    CHECK(sampleHoldEnergy(e,{{T,0,d("1")},{T+11,11,d("1")}},T,T+11,10).status == ElectricityStatus::Unavailable);
    auto step = sampleHoldEnergy(e,{{T,0,d("1")},{T+10,11,d("1")},{T+20,21,d("1")}},T,T+20,100);
    CHECK(step.clockDiscontinuity && step.status == ElectricityStatus::Partial && step.coveredMs == 10);
    CHECK(sampleHoldEnergy(e,{{T,20,d("1")},{T+1,19,d("1")}},T,T+1,100).status == ElectricityStatus::ClockFault);
    CHECK(sampleHoldEnergy(e,{{T,0,d("1")},{T,1,d("1")}},T,T+1,100).status == ElectricityStatus::ClockFault);
    CHECK(sampleHoldEnergy(e,{{T,0,d("-1")}},T,T+1,100).status == ElectricityStatus::Invalid);
    CHECK(sampleHoldEnergy({}, {},T,T+1,100).status == ElectricityStatus::Invalid);
    CHECK(sampleHoldEnergy(e,{},T,T+1,0).status == ElectricityStatus::Invalid);
    CHECK(!electricityKwh(d("-1")));

    // Same full-remainder opening state in both branches; fixed civil-day fees really cancel. T02, T03, T04, T10.
    p = plan(T,T+2*day); s = opening(T,T+2*day); p.revisions[0].baseRatePerKwh = d("2"); p.revisions[0].chargePerCivilDay = d("4");
    auto b = counterfactualBill(p,s,load("0",T,T+2*day),load("1000",T,T+2*day),T+2*day,T);
    charge(b.withoutMining,"8.000000"); charge(b.withMining,"104.000000"); delta(b,"96.000000");
    p = plan(); s = opening(); tier(p,TierMode::Progressive);
    b = counterfactualBill(p,s,load("99000"),load("101000"),T+hour,T);
    charge(b.withoutMining,"99.000000"); charge(b.withMining,"102.000000"); delta(b,"3.000000");
    delta(counterfactualBill(p,s,load("99000"),load("100000"),T+hour,T),"1.000000"); // two separate 1-kWh scenarios cannot sum to the joint 3-U scenario
    tier(p,TierMode::AllUnits);
    b = counterfactualBill(p,s,load("100000"),load("101000"),T+hour,T);
    charge(b.withoutMining,"100.000000"); charge(b.withMining,"202.000000"); delta(b,"102.000000");
    CHECK(evaluateBill(p,s,load("1000",T,T+hour/2),T+hour/2,T).status == ElectricityStatus::Unsupported);
    p = plan(); p.revisions[0].baseRatePerKwh = d("-2");
    delta(counterfactualBill(p,s,load("0"),load("1000"),T+hour,T),"-2.000000");
    p.tiers = TierMode::Progressive; p.revisions[0].bands = {{std::nullopt,d("3")}};
    delta(counterfactualBill(p,s,load("0"),load("1000"),T+hour,T),"1.000000");

    // Simple whole-subtotal multiplier/floor are explicit, not inferred taxes. T23/T25.
    p = plan(); p.revisions[0].baseRatePerKwh = d("10"); p.cycleCharge = d("2"); p.subtotalMultiplier = d("1.1");
    charge(evaluateBill(p,s,load("1000"),T+hour,T),"13.200000");
    p = plan(); p.revisions[0].baseRatePerKwh = d("1"); p.minimumCycleSubtotal = d("10");
    delta(counterfactualBill(p,s,load("8000"),load("9000"),T+hour,T),"0.000000");
    delta(counterfactualBill(p,s,load("8000"),load("11000"),T+hour,T),"1.000000");

    // Immutable chronological price revisions do not replenish tier allowances. T17, T33/T34.
    p = plan(T,T+2*hour); s = opening(T,T+2*hour); p.revisions[0].baseRatePerKwh = d("1");
    auto r = p.revisions[0]; r.key = "fixture.revision.2"; r.beginUtcMs = T+hour; r.baseRatePerKwh = d("3");
    p.revisions[0].endUtcMs = T+hour; p.revisions.push_back(r);
    charge(evaluateBill(p,s,load("1000",T,T+2*hour),T+2*hour,T),"4.000000");
    tier(p,TierMode::Progressive); p.revisions[1].baseRatePerKwh = d("0"); p.revisions[1].bands = p.revisions[0].bands;
    p.revisions[1].bands[1].ratePerKwh = d("3");
    auto full = evaluateBill(p,s,load("60000",T,T+2*hour),T+2*hour,T);
    charge(full,"160.000000");
    auto first = evaluateBill(p,s,load("60000",T,T+hour),T+hour,T); charge(first,"60.000000");
    auto second = evaluateBill(p,*first.ending,load("60000",T+hour,T+2*hour),T+2*hour,T); charge(second,"100.000000");
    CHECK(full.ending && second.ending && full.ending->accruedSubtotal.numerator.compare(second.ending->accruedSubtotal.numerator) == 0);
    CHECK(s.throughUtcMs == T && s.importedKwh.numerator.zero() && s.accruedSubtotal.numerator.zero());
    charge(evaluateBill(p,s,load("60000",T,T+2*hour),T+2*hour,T),"160.000000");
    p.revisions[1].bands[0].throughKwh = d("101");
    CHECK(evaluateBill(p,s,load("60000",T,T+2*hour),T+2*hour,T).status == ElectricityStatus::Unsupported);

    // Late-cycle all-units settlement reprices the already accumulated quantity, not just the new kWh.
    p = plan(T,T+2*hour); s = opening(T,T+2*hour); tier(p,TierMode::AllUnits);
    s.throughUtcMs = T+hour; s.importedKwh = kwh("100"); s.accruedSubtotal = kwh("100"); // numerator convention, currency not physical kWh
    s.timeDatabaseVersion = "explicit-fixed-clock";
    charge(evaluateBill(p,s,load("1000",T+hour,T+2*hour),T+2*hour,T),"102.000000");
    CHECK(s.importedKwh.numerator.compare(kwh("100").numerator) == 0);
    s.timeDatabaseVersion = "fixture.wrong-version";
    CHECK(evaluateBill(p,s,load("1000",T+hour,T+2*hour),T+2*hour,T).status == ElectricityStatus::Conflict);
    s.timeDatabaseVersion.clear();
    CHECK(evaluateBill(p,s,load("1000",T+hour,T+2*hour),T+2*hour,T).status == ElectricityStatus::Unavailable);
    // Daily charges and cycle charge are not recharged when the pure evaluator is resumed in the same civil day.
    p = plan(T,T+day+hour); s = opening(T,T+day+hour); p.cycleCharge = d("5"); p.revisions[0].chargePerCivilDay = d("4");
    first = evaluateBill(p,s,load("0",T,T+hour),T+hour,T); charge(first,"9.000000");
    CHECK(first.planKey == p.key && first.origin == p.origin && first.clock.kind == CivilClockKind::Utc && first.timeDatabaseVersion == "explicit-fixed-clock" && first.ending->timeDatabaseVersion == first.timeDatabaseVersion);
    second = evaluateBill(p,*first.ending,load("0",T+hour,T+day+hour),T+day+hour,T); charge(second,"4.000000");
    charge(evaluateBill(p,s,load("0",T,T+day+hour),T+day+hour,T),"13.000000");

    // Dynamic UTC intervals are half-open, bounded, causal and never carried through a gap. T11/T12/T33.
    p = plan(); s = opening(); auto &dynamic = p.revisions[0]; dynamic.timeMode = TimeRateMode::Dynamic; dynamic.baseRatePerKwh = d("0");
    dynamic.dynamic = {{T,T+hour/2,T-1,d("1")},{T+hour/2,T+hour,T-1,d("3")}};
    charge(evaluateBill(p,s,load("1000"),T+hour,T),"2.000000");
    auto saved = dynamic.dynamic;
    dynamic.dynamic[1].beginUtcMs++; CHECK(evaluateBill(p,s,load("1000"),T+hour,T).status == ElectricityStatus::MissingPrice);
    CHECK(!evaluateBill(p,s,load("0"),T+hour,T).charge); // no coverage is not a known zero cost even with zero load
    dynamic.dynamic = saved; dynamic.dynamic[1].beginUtcMs--; CHECK(evaluateBill(p,s,load("1000"),T+hour,T).status == ElectricityStatus::Conflict);
    dynamic.dynamic = saved; dynamic.dynamic[1].publishedUtcMs = T+1; CHECK(evaluateBill(p,s,load("1000"),T+hour,T).status == ElectricityStatus::NotYetKnown);
    dynamic.dynamic = saved; dynamic.publishedUtcMs = T+1; CHECK(evaluateBill(p,s,load("1000"),T+hour,T).status == ElectricityStatus::NotYetKnown);

    // Calendar-specific priority; unannounced events do not alter the past. Global tiers plus TOU are additive/shared.
    p = plan(T,T+day); s = opening(T,T+day); auto &cal = p.revisions[0]; cal.timeMode = TimeRateMode::Calendar; cal.baseRatePerKwh = d("1");
    CalendarRate peak; peak.key = "fixture.peak"; peak.beginMinute = 17*60; peak.endMinute = 19*60; peak.weekdayMask = 31; peak.ratePerKwh = d("3");
    cal.calendar.push_back(peak);
    charge(evaluateBill(p,s,load("1000",T,T+day),T+day,T),"28.000000");
    auto holiday = peak; holiday.key = "fixture.holiday"; holiday.beginMinute = 0; holiday.endMinute = 1440; holiday.date = CivilDate{2026,10,28}; holiday.priority = 1; holiday.ratePerKwh = d("1"); cal.calendar.push_back(holiday);
    auto event = holiday; event.key = "fixture.event"; event.beginMinute = 18*60; event.endMinute = 19*60; event.priority = 2; event.ratePerKwh = d("7"); event.publishedUtcMs = T+1; cal.calendar.push_back(event);
    charge(evaluateBill(p,s,load("1000",T,T+day),T+day,T),"24.000000");
    charge(evaluateBill(p,s,load("1000",T,T+day),T+day,T+1),"30.000000");
    auto tied = holiday; tied.key = "fixture.conflict"; cal.calendar.push_back(tied);
    CHECK(evaluateBill(p,s,load("1000",T,T+day),T+day,T).status == ElectricityStatus::Conflict);
    // Two lower-priority ties cannot hide an unambiguous winning override.
    auto winner = holiday; winner.key = "fixture.top"; winner.priority = 3; cal.calendar.push_back(winner);
    charge(evaluateBill(p,s,load("1000",T,T+day),T+day,T),"24.000000");
    p = plan(T,T+2*hour); s = opening(T,T+2*hour); tier(p,TierMode::Progressive); p.revisions[0].baseRatePerKwh = d("1");
    charge(evaluateBill(p,s,load("60000",T,T+2*hour),T+2*hour,T),"260.000000"); // T05 shared total 120, tier=140 plus 120 additive

    // Missing input is not a fabricated zero. Unsupported rule families stay out of the evaluator.
    p = plan(); s = opening();
    for (uint32_t flag = 1; flag; flag <<= 1) { p.unsupportedRules = flag; CHECK(evaluateBill(p,s,load("1000"),T+hour,T).status == ElectricityStatus::Unsupported); }
    p.unsupportedRules = 0; p.rounding = TariffRounding::Unsupported;
    CHECK(evaluateBill(p,s,load("1000"),T+hour,T).status == ElectricityStatus::Unsupported);
    p = plan(); p.declared = false; CHECK(!evaluateBill(p,s,load("1000"),T+hour,T).charge);
    p = plan(); s.declared = false; CHECK(!evaluateBill(p,s,load("1000"),T+hour,T).charge);
    s = opening(); s.planKey = "fixture.wrong-plan"; CHECK(evaluateBill(p,s,load("1000"),T+hour,T).status == ElectricityStatus::Invalid);
    s = opening(); s.importedKwh = kwh("1"); CHECK(evaluateBill(p,s,load("1000"),T+hour,T).status == ElectricityStatus::Invalid);
    s = opening(); auto bad = load("1000"); bad.intervals[0].beginUtcMs++; CHECK(evaluateBill(p,s,bad,T+hour,T).status == ElectricityStatus::Unavailable);
    bad = load("1000"); bad.intervals.push_back(bad.intervals[0]); CHECK(evaluateBill(p,s,bad,T+hour,T).status == ElectricityStatus::Invalid);
    CHECK(evaluateBill(p,s,load("-1"),T+hour,T).status == ElectricityStatus::Invalid);
    CHECK(evaluateBill(p,s,load("0"),T+hour,T).charge->numerator.zero());
    bad = load("1000"); bad.evidence.basis = PowerBasis::GpuBoard;
    CHECK(counterfactualBill(p,s,load("0"),bad,T+hour,T).status == ElectricityStatus::Invalid);
    p.tiers = TierMode::Progressive; p.revisions[0].bands = {{std::nullopt,d("1")}};
    bad.evidence = evidence(); bad.evidence.basis = PowerBasis::GpuBoard;
    s.evidence = bad.evidence;
    CHECK(evaluateBill(p,s,bad,T+hour,T).status == ElectricityStatus::Unsupported);
    p = plan(); s = opening();
    bad = load("1000"); bad.evidence.scope = "fixture.unrelated-meter";
    CHECK(counterfactualBill(p,s,load("0"),bad,T+hour,T).status == ElectricityStatus::Invalid);
    CHECK(evaluateBill(p,s,bad,T+hour,T).status == ElectricityStatus::Invalid);
    CHECK(counterfactualBill(p,s,load("0",T,T+1),load("0",T,T+1),T+1,T).status == ElectricityStatus::Unsupported);
    p.subtotalMultiplier.reset(); CHECK(!evaluateBill(p,s,load("1000"),T+hour,T).charge);
    p = plan(); p.origin = TariffOrigin::Unknown; CHECK(!evaluateBill(p,s,load("1000"),T+hour,T).charge);
    p = plan(); bad = load("1000"); bad.evidence.origin = PowerOrigin::ObservedTelemetry; s.evidence = bad.evidence;
    CHECK(evaluateBill(p,s,bad,T+hour,T).status == ElectricityStatus::NotYetKnown);
    charge(evaluateBill(p,s,bad,T+hour,T+hour),"3.000000");
    s = opening();
    p = plan(); p.currency = "$"; CHECK(evaluateBill(p,s,load("1000"),T+hour,T).status == ElectricityStatus::Invalid);
    p = plan(); p.tiers = TierMode::AllUnits; p.revisions[0].bands = {{d("100"),d("1")}};
    CHECK(evaluateBill(p,s,load("1000"),T+hour,T).status == ElectricityStatus::Invalid); // no unbounded final band
    p.revisions[0].bands = {{d("0"),d("1")},{std::nullopt,d("2")}};
    CHECK(evaluateBill(p,s,load("1000"),T+hour,T).status == ElectricityStatus::Invalid);
    p = plan(); auto tooLarge = load("0"); tooLarge.intervals.resize(100001);
    CHECK(evaluateBill(p,s,tooLarge,T+hour,T).status == ElectricityStatus::Capacity);
    p.revisions[0].calendar.resize(65); CHECK(evaluateBill(p,s,load("0"),T+hour,T).status == ElectricityStatus::Capacity);
    p = plan(); p.revisions[0].bands.resize(65); CHECK(evaluateBill(p,s,load("0"),T+hour,T).status == ElectricityStatus::Capacity);
    p = plan(); p.revisions[0].baseRatePerKwh = d(std::string(96,'9'));
    CHECK(evaluateBill(p,s,load("1000"),T+hour,T).status == ElectricityStatus::Capacity);
    CHECK(sampleHoldEnergy(e,{{T,0,d(std::string(96,'9'))},{T+10,10,d("0")}},T,T+10,100).status == ElectricityStatus::Capacity);

    if (argc == 2) {
        CHECK(configureCivilTimeData(std::filesystem::u8path(argv[1]),"2026e") == CivilStatus::Ok);
        CivilClock ny{CivilClockKind::NamedIana,"America/New_York",0};
        for (const auto date : {CivilDate{2026,3,8},CivilDate{2026,11,1}}) {
            const auto range = civilDay(ny,date); const auto len = range.endUtcMs-range.beginUtcMs;
            p = plan(range.beginUtcMs,range.endUtcMs); s = opening(range.beginUtcMs,range.endUtcMs); p.clock = ny; p.revisions[0].baseRatePerKwh = d("1"); p.revisions[0].chargePerCivilDay = d("4");
            charge(evaluateBill(p,s,load("1000",range.beginUtcMs,range.endUtcMs),range.endUtcMs,range.beginUtcMs),len == 23*hour ? "27.000000" : "29.000000");
            const auto observed = sampleHoldEnergy(e,{{range.beginUtcMs,0,d("1000")},{range.endUtcMs,len,d("1000")}},range.beginUtcMs,range.endUtcMs,day);
            if (len > day) CHECK(observed.status == ElectricityStatus::Unavailable); // explicit max gap, not a 24h clamp on energy
            else CHECK(observed.completeKwh && display(*observed.completeKwh) == "23.000000");
        }
        // Both occurrences of 01:00-02:00 billed during a fold; nonexistent spring hour bills nothing. T13/T14.
        for (const auto date : {CivilDate{2026,3,8},CivilDate{2026,11,1}}) {
            const auto range = civilDay(ny,date);
            p = plan(range.beginUtcMs,range.endUtcMs); s = opening(range.beginUtcMs,range.endUtcMs); p.clock = ny;
            auto &r = p.revisions[0]; r.timeMode = TimeRateMode::Calendar; r.baseRatePerKwh = d("0");
            CalendarRate peak; peak.key = "fixture.dst.peak"; peak.beginMinute = date.month == 3 ? 120 : 60; peak.endMinute = peak.beginMinute+60; peak.ratePerKwh = d("1"); r.calendar.push_back(peak);
            charge(evaluateBill(p,s,load("1000",range.beginUtcMs,range.endUtcMs),range.endUtcMs,range.beginUtcMs),date.month == 3 ? "0.000000" : "2.000000");
            if (date.month == 11) {
                r.calendar[0].beginMinute = 90;
                charge(evaluateBill(p,s,load("1000",range.beginUtcMs,range.endUtcMs),range.endUtcMs,range.beginUtcMs),"1.000000");
            }
        }
        // Fixed standard clock and civil daylight clock are intentionally different. T15.
        const auto start = dateUtc({2026,1,1});
        p = plan(start,start+hour); s = opening(start,start+hour); p.clock = {CivilClockKind::FixedStandardOffset,"",10*3600};
        CalendarRate peak; peak.key = "fixture.standard.peak"; peak.beginMinute = 10*60; peak.endMinute = 11*60; peak.ratePerKwh = d("3");
        p.revisions[0].timeMode = TimeRateMode::Calendar; p.revisions[0].baseRatePerKwh = d("1"); p.revisions[0].calendar = {peak};
        charge(evaluateBill(p,s,load("1000",start,start+hour),start+hour,start),"3.000000");
        p.clock = {CivilClockKind::NamedIana,"Australia/Sydney",0};
        charge(evaluateBill(p,s,load("1000",start,start+hour),start+hour,start),"1.000000");
    }
    return finishTests("electricity");
}
