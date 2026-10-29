#include "electricity.h"
#include <algorithm>
#include <set>

namespace om {
namespace {
constexpr int64_t lastUtcMs = 253402300799999LL, dayMs = 86400000;
constexpr size_t maxTrace = 100000, maxCuts = 200000;
bool instant(int64_t t) { return t >= 0 && t <= lastUtcMs; }
ExactDecimal number(int64_t n) { ExactDecimal value; ExactDecimal::parse(std::to_string(n),&value); return value; }
bool accumulate(ExactDecimal &a, const ExactDecimal &b) { return a.add(b,&a); }
bool productAdd(ExactDecimal &sum, const ExactDecimal &a, const ExactDecimal &b) {
    ExactDecimal product; return a.multiply(b,&product) && accumulate(sum,product);
}
bool textKey(const std::string &s) { return !s.empty() && s.size() <= 128 && std::all_of(s.begin(),s.end(),[](unsigned char c){return c >= 32 && c != 127;}); }
bool evidenceValid(const PowerEvidence &e) {
    return (e.basis == PowerBasis::GpuBoard || e.basis == PowerBasis::WholeSystemWall || e.basis == PowerBasis::DirectIncrementalWall || e.basis == PowerBasis::WholeMeterImport) &&
           (e.origin == PowerOrigin::SyntheticFixture || e.origin == PowerOrigin::ObservedTelemetry || e.origin == PowerOrigin::DeclaredForecast) && textKey(e.scope) && textKey(e.adapter);
}
ElectricityStatus validateTrace(const PowerTrace &trace, int64_t begin, int64_t end) {
    if (!evidenceValid(trace.evidence)) return ElectricityStatus::Invalid;
    if (trace.intervals.size() > maxTrace) return ElectricityStatus::Capacity;
    int64_t cursor = begin;
    for (const auto &i : trace.intervals) {
        if (!instant(i.beginUtcMs) || !instant(i.endUtcMs) || i.beginUtcMs < cursor || i.endUtcMs <= i.beginUtcMs || i.endUtcMs > end || i.watts.negative()) return ElectricityStatus::Invalid;
        if (i.beginUtcMs != cursor) return ElectricityStatus::Unavailable;
        cursor = i.endUtcMs;
    }
    return cursor == end ? ElectricityStatus::Ok : ElectricityStatus::Unavailable;
}
ElectricityStatus validatePlan(const TariffPlan &p, const BillOpening &s, int64_t end, int64_t decision,bool accrual=false) {
    if (!p.declared || !s.declared) return ElectricityStatus::Unavailable;
    if (p.unsupportedRules || p.rounding != TariffRounding::FinalDisplayOnly) return ElectricityStatus::Unsupported;
    if (!evidenceValid(s.evidence) || (p.origin != TariffOrigin::SyntheticFixture && p.origin != TariffOrigin::UserDeclaredOffline) || !textKey(p.key) || !textKey(p.territory) || !textKey(p.supplier) || !textKey(p.product) || p.currency.size() != 3 ||
        !std::all_of(p.currency.begin(),p.currency.end(),[](char c){return c >= 'A' && c <= 'Z';}) || s.planKey != p.key || !p.subtotalMultiplier || p.subtotalMultiplier->negative() ||
        (p.minimumCycleSubtotal && p.minimumCycleSubtotal->negative()) || s.importedKwh.numerator.negative()) return ElectricityStatus::Invalid;
    if (!instant(decision) || !instant(s.cycleBeginUtcMs) || !instant(s.cycleEndUtcMs) || s.cycleEndUtcMs <= s.cycleBeginUtcMs ||
        s.cycleEndUtcMs-s.cycleBeginUtcMs > 370*dayMs || s.throughUtcMs < s.cycleBeginUtcMs || end <= s.throughUtcMs || end > s.cycleEndUtcMs) return ElectricityStatus::Invalid;
    if (s.throughUtcMs == s.cycleBeginUtcMs && (!s.importedKwh.numerator.zero() || !s.accruedSubtotal.numerator.zero())) return ElectricityStatus::Invalid;
    if (p.revisions.empty()) return ElectricityStatus::Unavailable;
    if (p.revisions.size() > 128) return ElectricityStatus::Capacity;
    if (p.tiers != TierMode::None && p.tiers != TierMode::Progressive && p.tiers != TierMode::AllUnits) return ElectricityStatus::Unsupported;
    if (!accrual && (p.tiers == TierMode::AllUnits || p.minimumCycleSubtotal) && end != s.cycleEndUtcMs) return ElectricityStatus::Unsupported;
    if (p.tiers == TierMode::AllUnits && p.revisions.size() != 1) return ElectricityStatus::Unsupported;
    std::set<std::string> keys;
    int64_t previousEnd = p.revisions.front().beginUtcMs;
    for (const auto &r : p.revisions) {
        if (!textKey(r.key) || !keys.insert(r.key).second || !instant(r.beginUtcMs) || !instant(r.endUtcMs) || r.endUtcMs <= r.beginUtcMs || r.beginUtcMs != previousEnd || !instant(r.publishedUtcMs)) return ElectricityStatus::Invalid;
        previousEnd = r.endUtcMs;
        if (r.calendar.size() > 64 || r.dynamic.size() > 20000 || r.bands.size() > 64) return ElectricityStatus::Capacity;
        if (r.timeMode != TimeRateMode::Constant && r.timeMode != TimeRateMode::Calendar && r.timeMode != TimeRateMode::Dynamic) return ElectricityStatus::Unsupported;
        if ((r.timeMode != TimeRateMode::Calendar && !r.calendar.empty()) || (r.timeMode != TimeRateMode::Dynamic && !r.dynamic.empty()) || (r.timeMode == TimeRateMode::Dynamic && !r.baseRatePerKwh.zero())) return ElectricityStatus::Invalid;
        std::set<std::string> calendars;
        for (const auto &c : r.calendar)
            if (!textKey(c.key) || !calendars.insert(c.key).second || c.beginMinute >= c.endMinute || c.endMinute > 1440 || !c.weekdayMask || c.weekdayMask > 127 || !c.monthMask || c.monthMask > 4095 || c.priority > 1000 || (c.date && !validCivilDate(*c.date)) || !instant(c.publishedUtcMs)) return ElectricityStatus::Invalid;
        int64_t dynamicEnd = -1;
        for (const auto &d : r.dynamic) {
            if (!instant(d.beginUtcMs) || !instant(d.endUtcMs) || d.endUtcMs <= d.beginUtcMs || !instant(d.publishedUtcMs)) return ElectricityStatus::Invalid;
            if (d.beginUtcMs < dynamicEnd) return ElectricityStatus::Conflict;
            dynamicEnd = d.endUtcMs;
        }
        if (p.tiers == TierMode::None ? !r.bands.empty() : r.bands.empty()) return ElectricityStatus::Invalid;
        ExactDecimal threshold;
        for (size_t j = 0; j < r.bands.size(); ++j) {
            const auto &b = r.bands[j];
            if (j+1 == r.bands.size()) { if (b.throughKwh) return ElectricityStatus::Invalid; }
            else {
                if (!b.throughKwh || b.throughKwh->compare(threshold) <= 0 || !electricityKwh(*b.throughKwh)) return ElectricityStatus::Invalid;
                threshold = *b.throughKwh;
            }
        }
        const auto &first = p.revisions.front().bands;
        if (r.bands.size() != first.size()) return ElectricityStatus::Unsupported;
        for (size_t j = 0; j < first.size(); ++j) {
            if (r.bands[j].throughKwh.has_value() != first[j].throughKwh.has_value() ||
                (first[j].throughKwh && r.bands[j].throughKwh->compare(*first[j].throughKwh) != 0)) return ElectricityStatus::Unsupported;
        }
    }
    if (p.revisions.front().beginUtcMs > s.cycleBeginUtcMs || previousEnd < s.cycleEndUtcMs) return ElectricityStatus::Unavailable;
    const auto civil = civilAt(p.clock,s.throughUtcMs);
    if (civil.status != CivilStatus::Ok) return civil.status == CivilStatus::Invalid ? ElectricityStatus::Invalid : ElectricityStatus::Unavailable;
    if (s.timeDatabaseVersion.empty() && s.throughUtcMs > s.cycleBeginUtcMs) return ElectricityStatus::Unavailable;
    if (!s.timeDatabaseVersion.empty() && s.timeDatabaseVersion != civil.databaseVersion) return ElectricityStatus::Conflict;
    return ElectricityStatus::Ok;
}
bool tierCost(const std::vector<TariffBand> &bands, TierMode mode, const ExactDecimal &quantity, ExactDecimal *cost) {
    ExactDecimal sum, lower;
    for (const auto &band : bands) {
        const auto upper = band.throughKwh ? electricityKwh(*band.throughKwh)->numerator : quantity;
        if (mode == TierMode::AllUnits) {
            if (quantity.compare(upper) <= 0) return quantity.multiply(band.ratePerKwh,cost);
        } else {
            if (quantity.compare(lower) <= 0) break;
            const auto top = quantity.compare(upper) < 0 ? quantity : upper;
            ExactDecimal part;
            if (!top.subtract(lower,&part) || !productAdd(sum,part,band.ratePerKwh)) return false;
        }
        lower = upper;
    }
    *cost = sum; return true;
}
void cut(int64_t candidate, int64_t now, int64_t &next) { if (candidate > now && candidate < next) next = candidate; }
ElectricityStatus timeRate(const TariffPlan &p, const TariffRevision &r, int64_t now, int64_t decision, ExactDecimal &rate, int64_t &next, CivilStamp &stamp) {
    if (r.publishedUtcMs > decision) return ElectricityStatus::NotYetKnown;
    stamp = civilAt(p.clock,now);
    const auto day = civilDayAt(p.clock,now);
    if (stamp.status != CivilStatus::Ok || day.status != CivilStatus::Ok) return ElectricityStatus::Unavailable;
    cut(day.endUtcMs,now,next);
    if (const auto transition = civilNextTransition(p.clock,now)) cut(*transition,now,next);
    rate = r.baseRatePerKwh;
    if (r.timeMode == TimeRateMode::Dynamic) {
        const auto found = std::lower_bound(r.dynamic.begin(),r.dynamic.end(),now,[](const DynamicRate &d, int64_t t){return d.endUtcMs <= t;});
        if (found == r.dynamic.end() || found->beginUtcMs > now) return ElectricityStatus::MissingPrice;
        if (found->publishedUtcMs > decision) return ElectricityStatus::NotYetKnown;
        rate = found->ratePerKwh; cut(found->endUtcMs,now,next);
    }
    if (r.timeMode == TimeRateMode::Calendar) {
        const CalendarRate *winner = nullptr;
        bool tied = false;
        for (const auto &c : r.calendar) {
            if (c.publishedUtcMs > decision) continue;  // an unannounced event cannot influence a backtest
            for (unsigned boundary : {c.beginMinute,c.endMinute}) {
                const auto date = boundary == 1440 ? civilAddDays(stamp.date,1) : std::optional<CivilDate>{stamp.date};
                if (!date) return ElectricityStatus::Invalid;
                for (const auto choice : {CivilChoose::Earlier,CivilChoose::Later}) {
                    const auto resolved = civilResolve(p.clock,*date,(boundary%1440)*60000,choice);
                    if (resolved.status != CivilStatus::Ok) return ElectricityStatus::Unavailable;
                    cut(*resolved.utcMs,now,next);
                }
            }
            const auto minute = stamp.millisecondOfDay / 60000;
            if (minute < c.beginMinute || minute >= c.endMinute || !(c.weekdayMask & (1U << (stamp.isoWeekday-1))) || !(c.monthMask & (1U << (stamp.date.month-1))) || (c.date && !sameCivilDate(*c.date,stamp.date))) continue;
            if (winner && winner->priority == c.priority) tied = true;
            if (!winner || c.priority > winner->priority) { winner = &c; tied = false; }
        }
        if (tied) return ElectricityStatus::Conflict;
        if (winner) rate = winner->ratePerKwh;
    }
    return ElectricityStatus::Ok;
}
}  // namespace

const char *electricityStatusName(ElectricityStatus s) {
    switch (s) {
        case ElectricityStatus::Ok: return "declared arithmetic available (not supplier-certified)";
        case ElectricityStatus::Partial: return "partial sample-and-hold coverage";
        case ElectricityStatus::Unavailable: return "missing configuration/state/coverage/time data";
        case ElectricityStatus::Invalid: return "invalid identity/unit/interval/state";
        case ElectricityStatus::Unsupported: return "unsupported rule/rounding/settlement horizon";
        case ElectricityStatus::MissingPrice: return "dynamic price gap; no carry-forward";
        case ElectricityStatus::Conflict: return "overlapping/unresolved rate definitions";
        case ElectricityStatus::NotYetKnown: return "rate published after decision instant";
        case ElectricityStatus::ClockFault: return "regressed or inconsistent sample clocks";
        case ElectricityStatus::Capacity: return "bounded arithmetic/input/evaluation capacity exceeded";
    }
    return "unknown electricity status";
}
std::optional<ElectricityAmount> electricityKwh(const ExactDecimal &kwh) {
    ElectricityAmount result;
    if (kwh.negative() || !kwh.multiply(number(wattMsPerKwh),&result.numerator)) return std::nullopt;
    return result;
}
EnergyEstimate sampleHoldEnergy(const PowerEvidence &e, const std::vector<PowerObservation> &samples, int64_t begin, int64_t end, int64_t maxGap) {
    EnergyEstimate result; result.beginUtcMs = begin; result.endUtcMs = end; result.coveredTrace.evidence = e;
    if (!evidenceValid(e) || !instant(begin) || !instant(end) || end <= begin || maxGap <= 0 || maxGap > dayMs) { result.status = ElectricityStatus::Invalid; return result; }
    if (samples.size() > maxTrace) { result.status = ElectricityStatus::Capacity; return result; }
    for (size_t i = 0; i < samples.size(); ++i) {
        const auto &a = samples[i];
        if (!instant(a.utcMs) || a.monotonicMs < 0 || (a.watts && a.watts->negative())) { result.status = ElectricityStatus::Invalid; return result; }
        if (i && (a.utcMs <= samples[i-1].utcMs || a.monotonicMs <= samples[i-1].monotonicMs)) { result.status = ElectricityStatus::ClockFault; result.clockDiscontinuity = true; return result; }
    }
    ExactDecimal integral;
    for (size_t i = 1; i < samples.size(); ++i) {
        const auto &a = samples[i-1], &b = samples[i];
        const auto elapsed = b.monotonicMs-a.monotonicMs, utcElapsed = b.utcMs-a.utcMs;
        if (utcElapsed != elapsed) result.clockDiscontinuity = true;
        const int64_t from = (std::max)(begin,a.utcMs), through = (std::min)(end,b.utcMs);
        if (through <= from || !a.watts || elapsed > maxGap || utcElapsed != elapsed) continue;
        if (!productAdd(integral,*a.watts,number(through-from))) { result.status = ElectricityStatus::Capacity; result.coveredTrace.intervals.clear(); return result; }
        result.coveredTrace.intervals.push_back({from,through,*a.watts}); result.coveredMs += through-from;
    }
    result.knownKwh.numerator = integral;
    if (result.coveredMs == end-begin) { result.completeKwh = result.knownKwh; result.status = ElectricityStatus::Ok; }
    else result.status = result.coveredMs ? ElectricityStatus::Partial : ElectricityStatus::Unavailable;
    return result;
}
ElectricityStatus validateTariffOpening(const TariffPlan &p, const BillOpening &opening) {
    return validatePlan(p,opening,opening.cycleEndUtcMs,lastUtcMs);
}
static BillEstimate evaluateBillImpl(const TariffPlan &p,const BillOpening &opening,const PowerTrace &trace,int64_t end,int64_t decision,bool accrual) {
    BillEstimate result; result.currency = p.currency; result.evidence = trace.evidence; result.beginUtcMs = opening.throughUtcMs; result.endUtcMs = end;
    result.planKey = p.key; result.territory = p.territory; result.supplier = p.supplier; result.product = p.product; result.origin = p.origin; result.clock = p.clock; result.decisionUtcMs = decision;
    result.status = validatePlan(p,opening,end,decision,accrual);
    if (result.status != ElectricityStatus::Ok) return result;
    result.timeDatabaseVersion = civilAt(p.clock,opening.throughUtcMs).databaseVersion;
    result.status = validateTrace(trace,opening.throughUtcMs,end);
    if (result.status != ElectricityStatus::Ok) return result;
    const auto &binding = opening.evidence, &evidence = trace.evidence;
    if (binding.scope != evidence.scope || binding.adapter != evidence.adapter || binding.origin != evidence.origin || binding.basis != evidence.basis) { result.status = ElectricityStatus::Invalid; return result; }
    if (evidence.origin == PowerOrigin::ObservedTelemetry && end > decision) { result.status = ElectricityStatus::NotYetKnown; return result; }
    if (trace.evidence.basis != PowerBasis::WholeMeterImport) {
        // Standalone GPU/PC/incremental-wall energy proxies cannot establish shared allowances or a total meter bill.
        if (p.tiers != TierMode::None || p.minimumCycleSubtotal || !p.cycleCharge.zero() || (!accrual&&(!opening.importedKwh.numerator.zero() || !opening.accruedSubtotal.numerator.zero())) ||
            std::any_of(p.revisions.begin(),p.revisions.end(),[](const TariffRevision &r){return !r.chargePerCivilDay.zero();})) {
            result.status = ElectricityStatus::Unsupported; return result;
        }
    }
    auto state = opening; state.timeDatabaseVersion = result.timeDatabaseVersion;
    ExactDecimal cost, quantity = state.importedKwh.numerator;
    if (opening.throughUtcMs == opening.cycleBeginUtcMs && !productAdd(cost,p.cycleCharge,number(wattMsPerKwh))) { result.status = ElectricityStatus::Capacity; return result; }
    size_t revision = 0, count = 0;
    std::optional<CivilDate> previousDate;
    if (opening.throughUtcMs > opening.cycleBeginUtcMs) {
        const auto past = civilAt(p.clock,opening.throughUtcMs-1);
        if (past.status != CivilStatus::Ok) { result.status = ElectricityStatus::Unavailable; return result; }
        previousDate = past.date;
    }
    for (const auto &i : trace.intervals) {
        int64_t now = i.beginUtcMs;
        while (now < i.endUtcMs) {
            if (++count > maxCuts) { result.status = ElectricityStatus::Capacity; return result; }
            while (revision < p.revisions.size() && p.revisions[revision].endUtcMs <= now) ++revision;
            if (revision == p.revisions.size()) { result.status = ElectricityStatus::Unavailable; return result; }
            const auto &r = p.revisions[revision];
            int64_t next = (std::min)(i.endUtcMs,r.endUtcMs);
            ExactDecimal rate; CivilStamp stamp;
            result.status = timeRate(p,r,now,decision,rate,next,stamp);
            if (result.status != ElectricityStatus::Ok) return result;
            if (next <= now) { result.status = ElectricityStatus::Invalid; return result; }
            if (!previousDate || !sameCivilDate(*previousDate,stamp.date)) {
                if (previousDate && civilSerialDay(stamp.date) < civilSerialDay(*previousDate)) { result.status = ElectricityStatus::Unsupported; return result; }
                if (!productAdd(cost,r.chargePerCivilDay,number(wattMsPerKwh))) { result.status = ElectricityStatus::Capacity; return result; }
                previousDate = stamp.date;
            }
            ExactDecimal energy, newQuantity;
            if (!i.watts.multiply(number(next-now),&energy) || !quantity.add(energy,&newQuantity) || !productAdd(cost,energy,rate)) { result.status = ElectricityStatus::Capacity; return result; }
            if (p.tiers == TierMode::Progressive) {
                ExactDecimal before, after, delta;
                if (!tierCost(r.bands,p.tiers,quantity,&before) || !tierCost(r.bands,p.tiers,newQuantity,&after) || !after.subtract(before,&delta) || !accumulate(cost,delta)) { result.status = ElectricityStatus::Capacity; return result; }
            }
            quantity = newQuantity; now = next;
        }
    }
    if (p.tiers == TierMode::AllUnits) {
        ExactDecimal before, after, delta;
        if (!tierCost(p.revisions[0].bands,p.tiers,opening.importedKwh.numerator,&before) || !tierCost(p.revisions[0].bands,p.tiers,quantity,&after) || !after.subtract(before,&delta) || !accumulate(cost,delta)) { result.status = ElectricityStatus::Capacity; return result; }
    }
    if (!opening.accruedSubtotal.numerator.add(cost,&state.accruedSubtotal.numerator)) { result.status = ElectricityStatus::Capacity; return result; }
    if (p.minimumCycleSubtotal && end==opening.cycleEndUtcMs) {
        ExactDecimal floor;
        if (!p.minimumCycleSubtotal->multiply(number(wattMsPerKwh),&floor)) { result.status = ElectricityStatus::Capacity; return result; }
        if (state.accruedSubtotal.numerator.compare(floor) < 0) state.accruedSubtotal.numerator = floor;
        if (!state.accruedSubtotal.numerator.subtract(opening.accruedSubtotal.numerator,&cost)) { result.status = ElectricityStatus::Capacity; return result; }
    }
    ElectricityAmount charge;
    if (!cost.multiply(*p.subtotalMultiplier,&charge.numerator)) { result.status = ElectricityStatus::Capacity; return result; }
    state.importedKwh.numerator = quantity; state.throughUtcMs = end;
    result.ending = state; result.charge = charge; result.cycleSettled = end == state.cycleEndUtcMs; result.status = ElectricityStatus::Ok;
    return result;
}
BillEstimate evaluateBill(const TariffPlan &p,const BillOpening &opening,const PowerTrace &trace,int64_t end,int64_t decision) {
    return evaluateBillImpl(p,opening,trace,end,decision,false);
}
BillAccrual accrueBill(const TariffPlan &p,const BillOpening &opening,const PowerTrace &trace,int64_t end,int64_t observed) {
    BillAccrual result;
    if(end>observed){result.status=ElectricityStatus::NotYetKnown;return result;}
    const auto step=evaluateBillImpl(p,opening,trace,end,observed,true);result.status=step.status;
    if(step.status!=ElectricityStatus::Ok)return result;
    ElectricityAmount total;
    if(!step.ending->accruedSubtotal.numerator.multiply(*p.subtotalMultiplier,&total.numerator)){result.status=ElectricityStatus::Capacity;return result;}
    result.ending=step.ending;result.provisionalChange=step.charge;result.provisionalTotal=total;
    if(step.cycleSettled)result.settledTotal=total;
    else result.settlementDependent=p.tiers==TierMode::AllUnits||p.minimumCycleSubtotal.has_value();
    return result;
}
CounterfactualBill counterfactualBill(const TariffPlan &p, const BillOpening &opening, const PowerTrace &base, const PowerTrace &with, int64_t end, int64_t decision) {
    CounterfactualBill result;
    const auto &a = base.evidence, &b = with.evidence;
    if (!evidenceValid(a) || !evidenceValid(b) || a.basis != PowerBasis::WholeMeterImport || b.basis != a.basis || a.origin != b.origin || a.scope != b.scope || a.adapter != b.adapter) { result.status = ElectricityStatus::Invalid; return result; }
    // Reject truncated shared-meter scenarios even if this plan happens to have no discontinuous components.
    if (end != opening.cycleEndUtcMs) { result.status = ElectricityStatus::Unsupported; return result; }
    result.withoutMining = evaluateBill(p,opening,base,end,decision);
    if (result.withoutMining.status != ElectricityStatus::Ok) { result.status = result.withoutMining.status; return result; }
    result.withMining = evaluateBill(p,opening,with,end,decision);
    if (result.withMining.status != ElectricityStatus::Ok) { result.status = result.withMining.status; return result; }
    ElectricityAmount delta;
    if (!result.withMining.charge->numerator.subtract(result.withoutMining.charge->numerator,&delta.numerator)) { result.status = ElectricityStatus::Capacity; return result; }
    result.incrementalCharge = delta; result.status = ElectricityStatus::Ok; return result;
}
}  // namespace om
