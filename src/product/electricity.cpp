#include "electricity.h"
#include <algorithm>
#include <set>
#include <type_traits>

namespace om {
namespace {
constexpr int64_t lastUtcMs = 253402300799999LL, minuteMs = 60000, dayMs = 1440 * minuteMs;
constexpr size_t maxTrace = 100000, maxCuts = 200000, maxExportRates = 256;
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
const ExactDecimal &intervalQuantity(const PowerInterval &interval) { return interval.watts; }
const ExactDecimal &intervalQuantity(const MeterImportInterval &interval) { return interval.importedKwh.numerator; }
template<class Trace>
ElectricityStatus validateTrace(const Trace &trace, int64_t begin, int64_t end) {
    if (!evidenceValid(trace.evidence)) return ElectricityStatus::Invalid;
    if constexpr (std::is_same_v<Trace, MeterImportTrace>) {
        if (trace.evidence.basis != PowerBasis::WholeMeterImport) return ElectricityStatus::Invalid;
    }
    if (trace.intervals.size() > maxTrace) return ElectricityStatus::Capacity;
    int64_t cursor = begin;
    for (const auto &i : trace.intervals) {
        if (!instant(i.beginUtcMs) || !instant(i.endUtcMs) || i.beginUtcMs < cursor || i.endUtcMs <= i.beginUtcMs || i.endUtcMs > end || intervalQuantity(i).negative()) return ElectricityStatus::Invalid;
        if (i.beginUtcMs != cursor) return ElectricityStatus::Unavailable;
        cursor = i.endUtcMs;
    }
    return cursor == end ? ElectricityStatus::Ok : ElectricityStatus::Unavailable;
}
ElectricityStatus validateExportRule(const ExportPaymentRule &rule, int64_t decision) {
    if (rule.dynamic.empty()) {
        if (!instant(rule.publishedUtcMs) || rule.ratePerKwh.negative()) return ElectricityStatus::Invalid;
        return rule.publishedUtcMs > decision ? ElectricityStatus::NotYetKnown : ElectricityStatus::Ok;
    }
    if (rule.publishedUtcMs != 0 || !rule.ratePerKwh.zero()) return ElectricityStatus::Invalid;
    if (rule.dynamic.size() > maxExportRates) return ElectricityStatus::Capacity;
    int64_t previousEnd = 0;
    for (const auto &rate : rule.dynamic) {
        if (!instant(rate.beginUtcMs) || !instant(rate.endUtcMs) || !instant(rate.publishedUtcMs) ||
            rate.beginUtcMs >= rate.endUtcMs) return ElectricityStatus::Invalid;
        if (rate.beginUtcMs < previousEnd) return ElectricityStatus::Conflict;
        previousEnd = rate.endUtcMs;
    }
    return ElectricityStatus::Ok;
}
template<class Trace>
ElectricityStatus priceExport(const ExportPaymentRule &rule, const Trace &trace, int64_t decision,
                              ElectricityAmount &amount, ElectricityAmount &payment,
                              const std::atomic<bool> *cancel, const std::function<bool()> &stop) {
    size_t rateIndex=0;
    for (const auto &interval : trace.intervals) {
        if ((cancel && cancel->load(std::memory_order_relaxed)) || (stop && stop()))
            return ElectricityStatus::Unavailable;
        ExactDecimal energy;
        if constexpr (std::is_same_v<Trace,MeterImportTrace>) {
            if (!interval.exportedKwh) return ElectricityStatus::ExportEvidenceMissing;
            energy=interval.exportedKwh->numerator;
            if (energy.negative()) return ElectricityStatus::Invalid;
        } else {
            if (!interval.exportWatts) return ElectricityStatus::ExportEvidenceMissing;
            const auto &watts=*interval.exportWatts;
            if (watts.negative() || (!watts.zero() && !interval.watts.zero()))
                return ElectricityStatus::Invalid;
            if (!watts.multiply(number(interval.endUtcMs-interval.beginUtcMs),&energy))
                return ElectricityStatus::Capacity;
        }
        if (!accumulate(amount.numerator,energy)) return ElectricityStatus::Capacity;
        if (rule.dynamic.empty()) continue;

        const ExactDecimal *registerRate=nullptr;
        bool differentRates=false;
        for (auto cursor=interval.beginUtcMs;cursor<interval.endUtcMs;) {
            while (rateIndex<rule.dynamic.size() && rule.dynamic[rateIndex].endUtcMs<=cursor) ++rateIndex;
            if (rateIndex==rule.dynamic.size() || rule.dynamic[rateIndex].beginUtcMs>cursor)
                return ElectricityStatus::MissingPrice;
            const auto &rate=rule.dynamic[rateIndex];
            if (rate.publishedUtcMs>decision) return ElectricityStatus::NotYetKnown;
            const auto next=std::min(interval.endUtcMs,rate.endUtcMs);
            if constexpr (std::is_same_v<Trace,MeterImportTrace>) {
                if (!registerRate) registerRate=&rate.ratePerKwh;
                differentRates=differentRates || registerRate->compare(rate.ratePerKwh)!=0;
            } else {
                ExactDecimal pricedEnergy;
                if (!interval.exportWatts->multiply(number(next-cursor),&pricedEnergy) ||
                    !productAdd(payment.numerator,pricedEnergy,rate.ratePerKwh))
                    return ElectricityStatus::Capacity;
            }
            cursor=next;
        }
        if constexpr (std::is_same_v<Trace,MeterImportTrace>) {
            // The energy register has no information about how much was exported on either side.
            if (differentRates && !energy.zero()) return ElectricityStatus::ExportRateBoundary;
            if (!productAdd(payment.numerator,energy,*registerRate)) return ElectricityStatus::Capacity;
        }
    }
    if (rule.dynamic.empty() && !amount.numerator.multiply(rule.ratePerKwh,&payment.numerator))
        return ElectricityStatus::Capacity;
    return ElectricityStatus::Ok;
}
template<class Trace>
ExportPeriodEstimate estimateExportPeriodImpl(const ExportPaymentRule &rule, const Trace &trace,
    int64_t begin, int64_t end, int64_t observed, const std::function<bool()> &stop) {
    if (stop && stop()) return {};
    if (!instant(begin) || !instant(end) || !instant(observed) || end <= begin ||
        trace.evidence.basis != PowerBasis::WholeMeterImport)
        return {ElectricityStatus::Invalid, {}, {}};
    if (end > observed) return {ElectricityStatus::NotYetKnown, {}, {}};
    const auto ruleStatus = validateExportRule(rule, observed);
    if (ruleStatus != ElectricityStatus::Ok) return {ruleStatus, {}, {}};
    const auto traceStatus = validateTrace(trace, begin, end);
    if (traceStatus != ElectricityStatus::Ok) return {traceStatus, {}, {}};
    ExportPeriodEstimate result;
    const auto status = priceExport(rule, trace, observed, result.exportedKwh, result.payment, nullptr, stop);
    if (status != ElectricityStatus::Ok) return {status, {}, {}};
    result.status = ElectricityStatus::Ok;
    return result;
}
constexpr int64_t meterHalfHourMs = 30 * 60 * 1000;
ElectricityStatus validateDemand(const TariffPlan &plan,const BillOpening &opening,int64_t end,int64_t decision) {
    if (!plan.demandCharge)
        return opening.apparentDemand || opening.peakDemand || !opening.demandHistory.empty()
            ? ElectricityStatus::Invalid : ElectricityStatus::Ok;
    const auto &rule=*plan.demandCharge;
    if (opening.evidence.basis!=PowerBasis::WholeMeterImport) return ElectricityStatus::Unsupported;
    if (opening.apparentDemand!=rule.apparent() || !opening.peakDemand ||
        opening.peakDemand->numerator.negative() || rule.windowMinutes<5 ||
        rule.windowMinutes>60 || 60%rule.windowMinutes || rule.offsetMinutes>=rule.windowMinutes ||
        rule.ratePerUnit.negative() || !instant(rule.publishedUtcMs) ||
        (rule.powerFactorVaPlaces && (!rule.apparent() || *rule.powerFactorVaPlaces>18))) return ElectricityStatus::Invalid;
    if (rule.ratchet) {
        const auto &ratchet=*rule.ratchet;
        if (!ratchet.lookbackMonths || ratchet.lookbackMonths>12 ||
            ratchet.fraction.negative() || ratchet.fraction.zero() || ratchet.fraction.compare(number(1))>0 ||
            opening.demandHistory.size()!=ratchet.lookbackMonths)
            return ElectricityStatus::Invalid;
        for (const auto &peak:opening.demandHistory)
            if (peak.numerator.negative()) return ElectricityStatus::Invalid;
    } else if (!opening.demandHistory.empty()) return ElectricityStatus::Invalid;
    const int64_t span=rule.windowMinutes*minuteMs, offset=rule.offsetMinutes*minuteMs;
    for (const auto t:{opening.cycleBeginUtcMs,opening.throughUtcMs,opening.cycleEndUtcMs,end})
        if ((t-offset)%span) return ElectricityStatus::DemandWindowBoundary;
    // Total window energy bounds the peak from both sides.
    ExactDecimal scaledImport, maximumScaledImport;
    const auto &peak=opening.peakDemand->numerator;
    const auto windows=(opening.throughUtcMs-opening.cycleBeginUtcMs)/span;
    if (!opening.importedKwh.numerator.multiply(number(60/rule.windowMinutes),&scaledImport) ||
        !peak.multiply(number(windows),&maximumScaledImport)) return ElectricityStatus::Capacity;
    // Active imports bound the apparent peak from below. With no elapsed windows its peak is zero.
    if (scaledImport.compare(maximumScaledImport)>0 ||
        ((!rule.apparent() || !windows) && peak.compare(scaledImport)>0))
        return ElectricityStatus::Invalid;
    return rule.publishedUtcMs>decision ? ElectricityStatus::NotYetKnown : ElectricityStatus::Ok;
}

template<class Trace>
ElectricityStatus measureDemand(const DemandChargeRule &rule,const Trace &trace,int64_t begin,
                                ElectricityAmount &peak,const std::atomic<bool> *cancel,
                                const std::function<bool()> &stop) {
    const int64_t span=rule.windowMinutes*minuteMs;
    const auto kwNumeratorScale=number(60/rule.windowMinutes);
    auto windowEnd=begin+span;
    ExactDecimal integrated;
    const auto retainMaximum=[&](const ExactDecimal &energy){
        ExactDecimal kwNumerator;
        if (!energy.multiply(kwNumeratorScale,&kwNumerator)) return false;
        if (kwNumerator.compare(peak.numerator)>0) peak.numerator=std::move(kwNumerator);
        return true;
    };
    for (const auto &interval:trace.intervals) {
        if ((cancel && cancel->load(std::memory_order_relaxed)) || (stop && stop()))
            return ElectricityStatus::Unavailable;
        const auto &active=intervalQuantity(interval);
        const auto *quantity=&active;
        if (rule.apparent()) {
            const auto *apparent=[&]() -> const ExactDecimal * {
                if constexpr (std::is_same_v<Trace,MeterImportTrace>)
                    return interval.apparentKvah ? &interval.apparentKvah->numerator : nullptr;
                else
                    return interval.apparentVa ? &*interval.apparentVa : nullptr;
            }();
            if (!apparent) return ElectricityStatus::DemandApparentEvidence;
            if (apparent->compare(active)<0) return ElectricityStatus::Invalid;
            quantity=apparent;
        }
        if constexpr (std::is_same_v<Trace,MeterImportTrace>) {
            // A nonzero register delta cannot be assigned to two demand windows.
            if (interval.endUtcMs>windowEnd&&!quantity->zero())
                return ElectricityStatus::DemandWindowBoundary;
        }
        const auto energyFor=[&](int64_t duration,ExactDecimal &energy){
            if constexpr (std::is_same_v<Trace,MeterImportTrace>) {
                energy=*quantity;
                return true;
            } else return quantity->multiply(number(duration),&energy);
        };
        auto through=(std::min)(interval.endUtcMs,windowEnd);
        ExactDecimal energy;
        if (!energyFor(through-interval.beginUtcMs,energy)||!accumulate(integrated,energy))
            return ElectricityStatus::Capacity;
        if (through<windowEnd) continue;
        if (!retainMaximum(integrated)) return ElectricityStatus::Capacity;
        integrated={};
        // Complete intervening windows have the same constant power, or zero register import.
        const auto complete=(interval.endUtcMs-through)/span;
        if (complete) {
            if (!energyFor(span,energy)||!retainMaximum(energy)) return ElectricityStatus::Capacity;
            through+=complete*span;
        }
        windowEnd=through+span;
        if (interval.endUtcMs>through&&!energyFor(interval.endUtcMs-through,integrated))
            return ElectricityStatus::Capacity;
    }
    return ElectricityStatus::Ok;
}

ElectricityStatus foldHalfHours(const MeterImportTrace &trace,std::vector<MeterImportInterval> &halfHours,
                                const std::atomic<bool> *cancel,const std::function<bool()> &stop) {
    auto begin=trace.intervals.front().beginUtcMs;
    auto boundary=begin+meterHalfHourMs;
    ExactDecimal usage;
    for (const auto &interval:trace.intervals) {
        if ((cancel && cancel->load(std::memory_order_relaxed)) || (stop && stop()))
            return ElectricityStatus::Unavailable;
        if (interval.endUtcMs>boundary&&!interval.importedKwh.numerator.zero())
            return ElectricityStatus::MeterRoundingBoundary;
        if (!accumulate(usage,interval.importedKwh.numerator)) return ElectricityStatus::Capacity;
        while (interval.endUtcMs>=boundary) {
            if ((cancel && cancel->load(std::memory_order_relaxed)) || (stop && stop()))
                return ElectricityStatus::Unavailable;
            halfHours.push_back({begin,boundary,{std::move(usage)}});
            usage={};
            begin=boundary;
            boundary+=meterHalfHourMs;
        }
    }
    return ElectricityStatus::Ok;
}

bool validTaxRules(const std::vector<OrderedTaxRule> &rules) {
    if (rules.empty() || rules.size() > maxTaxRules) return false;
    constexpr unsigned componentMask = (1u << taxComponentCount) - 1;
    std::set<std::string> keys;
    for (size_t i = 0; i < rules.size(); ++i) {
        const auto &rule = rules[i];
        if (rule.key.empty() || rule.key.size() > 32 ||
            rule.key.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos ||
            !keys.insert(rule.key).second || rule.rate.negative() || rule.rate.digits() > 38 || rule.rate.scale() > 18)
            return false;
        if (rule.kind == TaxKind::Fraction) {
            if (((rule.taxable | rule.included) & ~componentMask) || (rule.taxable & rule.included) ||
                (!rule.included && !rule.addsCharge()) || rule.priorTaxes.size() > i) return false;
            std::set<size_t> prior;
            for (const auto dependency : rule.priorTaxes)
                if (dependency >= i || !rules[dependency].addsCharge() || !prior.insert(dependency).second) return false;
        } else if ((rule.kind != TaxKind::ImportedKwh && rule.kind != TaxKind::BilledKwh) ||
                   rule.taxable || !rule.priorTaxes.empty() ||
                   (rule.included && rule.included != taxComponentBit(TaxComponent::Energy))) return false;
    }
    return true;
}

ElectricityAmount &taxComponent(BillTaxOpening &opening, TaxComponent part) {
    return opening.components[static_cast<size_t>(part)];
}

bool sumTaxComponents(const BillTaxOpening &opening, ExactDecimal &subtotal) {
    for (const auto &component : opening.components)
        if (!accumulate(subtotal, component.numerator)) return false;
    return true;
}

ElectricityStatus validateRoundedTaxOpening(const BillTaxOpening &bases, const ExactDecimal &measured,
                                           int64_t elapsed) {
    const auto &billed = bases.billedKwh.numerator;
    std::string billedText;
    ExactDecimal kwh;
    if (!bases.billedKwh.format(2, &billedText) || !ExactDecimal::parse(billedText, &kwh))
        return ElectricityStatus::Capacity;
    const auto represented = electricityKwh(kwh);
    if (!represented) return ElectricityStatus::Capacity;
    if (billed.compare(represented->numerator) != 0) return ElectricityStatus::Invalid;

    const auto intervals = elapsed / meterHalfHourMs;
    const auto intervalCents = number(intervals * (wattMsPerKwh / 100));
    const auto &positiveCents = billed.compare(intervalCents) < 0 ? billed : intervalCents;
    ExactDecimal lowerError, lower, upper;
    if (!positiveCents.divideExact(2, &lowerError) || !billed.subtract(lowerError, &lower) ||
        !billed.add(number(intervals * (wattMsPerKwh / 200)), &upper)) return ElectricityStatus::Capacity;

    // At most min(billed cents, intervals) slots can have a positive billed quantity.
    // A half-cent endpoint is included only when all slots at that endpoint round to even cents.
    const bool even = (billedText.back() - '0') % 2 == 0;
    ExactDecimal positiveEvenCents;
    if (!positiveCents.add(positiveCents, &positiveEvenCents)) return ElectricityStatus::Capacity;
    const bool lowerIncluded = even && billed.compare(positiveEvenCents) >= 0;
    const int from = measured.compare(lower), to = measured.compare(upper);
    return from < 0 || (from == 0 && !lowerIncluded) || to > 0 || (to == 0 && !even)
        ? ElectricityStatus::Invalid : ElectricityStatus::Ok;
}

struct TaxSlice {
    const TariffTaxPeriod *period;
    int64_t begin, end;
};

ElectricityStatus selectTaxPeriods(const TariffPlan &p, const BillOpening &s, int64_t end, int64_t decision,
                                   std::vector<TaxSlice> &slices) {
    if (p.taxPeriods.empty())
        return s.tax || s.taxSegments || p.taxByServiceTime ? ElectricityStatus::Invalid : ElectricityStatus::Ok;
    if (p.taxByServiceTime ? (!s.taxSegments || s.tax) : (!s.tax || s.taxSegments)) return ElectricityStatus::Invalid;
    if (s.evidence.basis != PowerBasis::WholeMeterImport || s.restrictedCredit ||
        p.subtotalMultiplier->compare(number(1)) != 0) return ElectricityStatus::Unsupported;
    if (p.taxPeriods.size() > maxTaxPeriods) return ElectricityStatus::Capacity;
    int64_t previousEnd = 0;
    for (const auto &period : p.taxPeriods) {
        if (!instant(period.beginUtcMs) || !instant(period.endUtcMs) || !instant(period.publishedUtcMs) ||
            period.endUtcMs <= period.beginUtcMs || !validTaxRules(period.rules)) return ElectricityStatus::Invalid;
        if (period.beginUtcMs < previousEnd) return ElectricityStatus::Conflict;
        previousEnd = period.endUtcMs;
    }
    int64_t covered = s.cycleBeginUtcMs;
    for (const auto &period : p.taxPeriods) {
        if (period.endUtcMs <= s.cycleBeginUtcMs || period.beginUtcMs >= s.cycleEndUtcMs) continue;
        if (!p.taxByServiceTime && (period.beginUtcMs > s.cycleBeginUtcMs || period.endUtcMs < s.cycleEndUtcMs))
            return ElectricityStatus::TaxPeriodBoundary;
        const auto begin = (std::max)(period.beginUtcMs, s.cycleBeginUtcMs);
        const auto finish = (std::min)(period.endUtcMs, s.cycleEndUtcMs);
        if (begin != covered) return ElectricityStatus::TaxScheduleBoundary;
        if (p.rounding == TariffRounding::MeterHalfHourKwhHalfEven2 && finish % meterHalfHourMs)
            return ElectricityStatus::MeterRoundingBoundary;
        if (begin < end && period.publishedUtcMs > decision) return ElectricityStatus::NotYetKnown;
        slices.push_back({&period, begin, finish});
        covered = finish;
    }
    if (covered == s.cycleEndUtcMs) return ElectricityStatus::Ok;
    return p.taxByServiceTime ? ElectricityStatus::TaxScheduleBoundary : ElectricityStatus::TaxPeriodBoundary;
}

ElectricityStatus validateTaxQuantity(bool roundMeter, const BillTaxOpening &bases, const ExactDecimal &measured,
                                      int64_t elapsed) {
    const auto &billed = bases.billedKwh.numerator;
    const auto &energyQuantity = roundMeter ? billed : measured;
    if (measured.negative() || billed.negative() || ((!roundMeter || measured.zero()) && !billed.zero()) ||
        (energyQuantity.zero() && !bases.components[static_cast<size_t>(TaxComponent::Energy)].numerator.zero()))
        return ElectricityStatus::Invalid;
    return roundMeter && elapsed ? validateRoundedTaxOpening(bases, measured, elapsed) : ElectricityStatus::Ok;
}

bool taxDemandBase(const TariffPlan &p, const BillOpening &state, ElectricityAmount &amount) {
    ElectricityAmount billable, floor;
    return !p.demandCharge || state.throughUtcMs == state.cycleBeginUtcMs ||
        (billableDemand(*p.demandCharge, state, billable, floor) && p.demandCharge->charge(billable, &amount));
}

ElectricityStatus validateTaxOpening(const TariffPlan &p, const BillOpening &s, const std::vector<TaxSlice> &slices) {
    if (slices.empty()) return ElectricityStatus::Ok;
    const bool cycleStart = s.throughUtcMs == s.cycleBeginUtcMs;
    const bool roundMeter = p.rounding == TariffRounding::MeterHalfHourKwhHalfEven2;
    ExactDecimal subtotal, expectedCycleFee;
    ElectricityAmount expectedDemand;
    if ((!cycleStart && !p.cycleCharge.multiply(number(wattMsPerKwh), &expectedCycleFee)) ||
        !taxDemandBase(p, s, expectedDemand)) return ElectricityStatus::Capacity;
    if (s.taxSegments) {
        const auto &segments = *s.taxSegments;
        const auto elapsed = std::count_if(slices.begin(), slices.end(), [&](const auto &slice) {
            return slice.begin < s.throughUtcMs;
        });
        if (segments.size() != static_cast<size_t>(elapsed)) return ElectricityStatus::Invalid;
        ExactDecimal measured;
        for (size_t i = 0; i < segments.size(); ++i) {
            const auto &segment = segments[i];
            const auto &slice = slices[i];
            const auto &bases = segment.bases;
            if (segment.beginUtcMs != slice.begin ||
                bases.components[static_cast<size_t>(TaxComponent::CycleFee)].numerator.compare(i ? ExactDecimal{} : expectedCycleFee) != 0 ||
                !bases.components[static_cast<size_t>(TaxComponent::Demand)].numerator.zero()) return ElectricityStatus::Invalid;
            const auto status = validateTaxQuantity(roundMeter, bases, segment.importedKwh.numerator,
                (std::min)(slice.end, s.throughUtcMs) - slice.begin);
            if (status != ElectricityStatus::Ok) return status;
            if (!accumulate(measured, segment.importedKwh.numerator) || !sumTaxComponents(bases, subtotal))
                return ElectricityStatus::Capacity;
        }
        if (!accumulate(subtotal, expectedDemand.numerator)) return ElectricityStatus::Capacity;
        return measured.compare(s.importedKwh.numerator) == 0 && subtotal.compare(s.accruedSubtotal.numerator) == 0
            ? ElectricityStatus::Ok : ElectricityStatus::Invalid;
    }
    const auto &parts = s.tax->components;
    if (cycleStart && std::any_of(parts.begin(), parts.end(), [](const auto &part) {return !part.numerator.zero();}))
        return ElectricityStatus::Invalid;
    const auto quantityStatus = validateTaxQuantity(roundMeter, *s.tax, s.importedKwh.numerator,
        s.throughUtcMs - s.cycleBeginUtcMs);
    if (quantityStatus != ElectricityStatus::Ok) return quantityStatus;
    if (!sumTaxComponents(*s.tax, subtotal)) return ElectricityStatus::Capacity;
    if (subtotal.compare(s.accruedSubtotal.numerator) != 0 ||
        parts[static_cast<size_t>(TaxComponent::CycleFee)].numerator.compare(expectedCycleFee) != 0 ||
        parts[static_cast<size_t>(TaxComponent::Demand)].numerator.compare(expectedDemand.numerator) != 0)
        return ElectricityStatus::Invalid;
    return ElectricityStatus::Ok;
}

bool accumulateCharge(ExactDecimal &cost, BillOpening &state, TaxComponent component, const ExactDecimal &amount,
                      BillTaxSegment *segment = nullptr) {
    return accumulate(cost, amount) &&
        (!state.tax || accumulate(taxComponent(*state.tax, component).numerator, amount)) &&
        (!segment || accumulate(taxComponent(segment->bases, component).numerator, amount));
}

bool calculateTaxLines(const TariffPlan &p, const TariffTaxPeriod &period, const BillTaxOpening &bases,
                       const ExactDecimal &measured, const ExactDecimal &minimum, BillTaxTotal &result,
                       std::optional<int64_t> periodBegin = {}) {
    const auto firstLine = result.lines.size();
    for (const auto &rule : period.rules) {
        BillTaxLine line{rule.key, rule.kind, rule.included, {}, {}, periodBegin};
        if (rule.addsCharge()) {
            auto &base = line.base.numerator;
            if (rule.kind == TaxKind::Fraction) {
                for (size_t i = 0; i < bases.components.size(); ++i)
                    if ((rule.taxable & (1u << i)) && !accumulate(base, bases.components[i].numerator)) return false;
                if ((rule.taxable & taxComponentBit(TaxComponent::MinimumAdjustment)) &&
                    !accumulate(base, minimum)) return false;
                for (const auto earlier : rule.priorTaxes)
                    if (!accumulate(base, result.lines[firstLine + earlier].added.numerator)) return false;
            } else {
                const bool priced = rule.kind == TaxKind::BilledKwh && p.rounding == TariffRounding::MeterHalfHourKwhHalfEven2;
                base = priced ? bases.billedKwh.numerator : measured;
            }
            if (!base.multiply(rule.rate, &line.added.numerator) ||
                !accumulate(result.added.numerator, line.added.numerator)) return false;
        }
        result.lines.push_back(std::move(line));
    }
    return true;
}

bool calculateTaxes(const TariffPlan &p, const std::vector<TaxSlice> &slices, const BillOpening &state,
                    BillTaxTotal &result) {
    ExactDecimal baseSubtotal;
    if (state.taxSegments) {
        ElectricityAmount demand;
        if (!taxDemandBase(p, state, demand)) return false;
        baseSubtotal = demand.numerator;
        const auto &segments = *state.taxSegments;
        for (const auto &segment : segments)
            if (!sumTaxComponents(segment.bases, baseSubtotal)) return false;
        if (!state.accruedSubtotal.numerator.subtract(baseSubtotal, &result.minimumAdjustment.numerator)) return false;
        for (size_t i = 0; i < segments.size(); ++i) {
            const auto &segment = segments[i];
            auto bases = segment.bases;
            const bool closing = state.throughUtcMs == state.cycleEndUtcMs && i + 1 == segments.size();
            if (closing) taxComponent(bases, TaxComponent::Demand) = demand;
            if (!calculateTaxLines(p, *slices[i].period, bases, segment.importedKwh.numerator,
                                   closing ? result.minimumAdjustment.numerator : ExactDecimal{}, result, segment.beginUtcMs))
                return false;
        }
    } else if (!sumTaxComponents(*state.tax, baseSubtotal) ||
               !state.accruedSubtotal.numerator.subtract(baseSubtotal, &result.minimumAdjustment.numerator) ||
               !calculateTaxLines(p, *slices.front().period, *state.tax, state.importedKwh.numerator,
                                  result.minimumAdjustment.numerator, result)) return false;
    return state.accruedSubtotal.numerator.add(result.added.numerator, &result.total.numerator);
}

ElectricityStatus validatePlan(const TariffPlan &p, const BillOpening &s, int64_t end, int64_t decision,bool accrual=false,
                              std::vector<TaxSlice> *taxSlices=nullptr) {
    if (!p.declared || !s.declared) return ElectricityStatus::Unavailable;
    const bool roundMeter = p.rounding == TariffRounding::MeterHalfHourKwhHalfEven2;
    if (p.unsupportedRules || (!roundMeter && p.rounding != TariffRounding::FinalDisplayOnly) ||
        (roundMeter && (s.evidence.basis != PowerBasis::WholeMeterImport || p.tiers != TierMode::None)))
        return ElectricityStatus::Unsupported;
    if (!evidenceValid(s.evidence) || (p.origin != TariffOrigin::SyntheticFixture && p.origin != TariffOrigin::UserDeclaredOffline) || !textKey(p.key) || !textKey(p.territory) || !textKey(p.supplier) || !textKey(p.product) || p.currency.size() != 3 ||
        !std::all_of(p.currency.begin(),p.currency.end(),[](char c){return c >= 'A' && c <= 'Z';}) || s.planKey != p.key || !p.subtotalMultiplier || p.subtotalMultiplier->negative() ||
        (p.minimumCycleSubtotal && p.minimumCycleSubtotal->negative()) || s.importedKwh.numerator.negative()) return ElectricityStatus::Invalid;
    if (!instant(decision) || !instant(s.cycleBeginUtcMs) || !instant(s.cycleEndUtcMs) || s.cycleEndUtcMs <= s.cycleBeginUtcMs ||
        s.cycleEndUtcMs-s.cycleBeginUtcMs > 370*dayMs || s.throughUtcMs < s.cycleBeginUtcMs || end <= s.throughUtcMs || end > s.cycleEndUtcMs) return ElectricityStatus::Invalid;
    if (roundMeter && (s.cycleBeginUtcMs % meterHalfHourMs || s.cycleEndUtcMs % meterHalfHourMs ||
                       s.throughUtcMs % meterHalfHourMs || end % meterHalfHourMs))
        return ElectricityStatus::MeterRoundingBoundary;
    if (s.throughUtcMs == s.cycleBeginUtcMs && (!s.importedKwh.numerator.zero() || !s.accruedSubtotal.numerator.zero())) return ElectricityStatus::Invalid;
    if (p.restrictedCreditAtClose!=s.restrictedCredit.has_value()) return ElectricityStatus::Invalid;
    if (s.restrictedCredit) {
        if (s.evidence.basis!=PowerBasis::WholeMeterImport || p.exportPayment ||
            (p.demandCharge && p.demandCharge->apparent())) return ElectricityStatus::Unsupported;
        const auto &credit=*s.restrictedCredit;
        if (s.importedKwh.numerator.zero() && !credit.accruedImportSubtotal.numerator.zero())
            return ElectricityStatus::Invalid;
        if (credit.lots.size()>maxRestrictedCreditLots) return ElectricityStatus::Capacity;
        auto previousExpiry=s.throughUtcMs;
        for (const auto &lot:credit.lots) {
            if (!instant(lot.expiresUtcMs) || lot.expiresUtcMs<=previousExpiry ||
                lot.remaining.numerator.negative() || lot.remaining.numerator.zero())
                return ElectricityStatus::Invalid;
            previousExpiry=lot.expiresUtcMs;
        }
    }
    if (p.exportPayment.has_value()!=s.exports.has_value()) return ElectricityStatus::Invalid;
    if (p.exportPayment) {
        if (s.evidence.basis!=PowerBasis::WholeMeterImport || (p.demandCharge && p.demandCharge->apparent()))
            return ElectricityStatus::Unsupported;
        const auto &exportRule=*p.exportPayment;
        const auto &exported=s.exports->exportedKwh.numerator;
        const auto &earned=s.exports->accruedPayment.numerator;
        if (exported.negative() || (exported.zero() && !earned.zero()) ||
            (s.throughUtcMs==s.cycleBeginUtcMs && !exported.zero()))
            return ElectricityStatus::Invalid;
        if (exportRule.dynamic.empty() && earned.negative()) return ElectricityStatus::Invalid;
        const auto status = validateExportRule(exportRule, decision);
        if (status != ElectricityStatus::Ok) return status;
    }
    const auto demandStatus=validateDemand(p,s,end,decision);
    if (demandStatus!=ElectricityStatus::Ok) return demandStatus;
    std::vector<TaxSlice> selectedTaxes;
    auto taxStatus = selectTaxPeriods(p, s, end, decision, selectedTaxes);
    if (taxStatus != ElectricityStatus::Ok) return taxStatus;
    taxStatus = validateTaxOpening(p, s, selectedTaxes);
    if (taxStatus != ElectricityStatus::Ok) return taxStatus;
    if (taxSlices) *taxSlices = std::move(selectedTaxes);
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
const ExactDecimal &allUnitsRate(const std::vector<TariffBand> &bands, const ExactDecimal &quantity) {
    const auto band = std::find_if(bands.begin(), bands.end(), [&](const auto &candidate) {
        return !candidate.throughKwh || quantity.compare(electricityKwh(*candidate.throughKwh)->numerator) <= 0;
    });
    return band->ratePerKwh;  // Valid plans always end with an unbounded band.
}

bool tierCost(const std::vector<TariffBand> &bands, TierMode mode, const ExactDecimal &quantity, ExactDecimal *cost) {
    if (mode == TierMode::AllUnits) return quantity.multiply(allUnitsRate(bands, quantity), cost);
    ExactDecimal sum, lower;
    for (const auto &band : bands) {
        const auto upper = band.throughKwh ? electricityKwh(*band.throughKwh)->numerator : quantity;
        if (quantity.compare(lower) <= 0) break;
        const auto top = quantity.compare(upper) < 0 ? quantity : upper;
        ExactDecimal part;
        if (!top.subtract(lower,&part) || !productAdd(sum,part,band.ratePerKwh)) return false;
        lower = upper;
    }
    *cost = sum; return true;
}
bool sameMeterRates(const ExactDecimal &firstRate, const TariffRevision &first,
                    const ExactDecimal &rate, const TariffRevision &current, TierMode mode) {
    if (firstRate.compare(rate) != 0) return false;
    if (mode != TierMode::Progressive || &first == &current) return true;
    // Plan validation has already required identical thresholds.
    return std::equal(first.bands.begin(),first.bands.end(),current.bands.begin(),
        [](const TariffBand &a,const TariffBand &b){return a.ratePerKwh.compare(b.ratePerKwh) == 0;});
}
void cut(int64_t candidate, int64_t now, int64_t &next) { if (candidate > now && candidate < next) next = candidate; }
ElectricityStatus timeRate(const TariffPlan &p, const TariffRevision &r, int64_t now, int64_t decision, ExactDecimal &rate, int64_t &next, CivilStamp &stamp) {
    if (r.publishedUtcMs > decision) return ElectricityStatus::NotYetKnown;
    stamp = civilAt(p.clock,now);
    const auto day = civilDayAt(p.clock,now);
    if (stamp.status != CivilStatus::Ok || day.status != CivilStatus::Ok) return ElectricityStatus::Unavailable;
    if (now < day.beginUtcMs || now >= day.endUtcMs || !civilDayIsContiguous(p.clock,day))
        return ElectricityStatus::Unsupported;
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
ElectricityStatus addDailyFee(const TariffPlan &plan, const BillOpening &opening, const TariffRevision &first,
                              int64_t now, int64_t decision, DailyFeeAllocation allocation, ExactDecimal &cost) {
    if (allocation == DailyFeeAllocation::WholeDay)
        return productAdd(cost,first.chargePerCivilDay,number(wattMsPerKwh)) ?
            ElectricityStatus::Ok : ElectricityStatus::Capacity;

    constexpr int64_t minutesPerDay = dayMs / minuteMs;
    static_assert(wattMsPerKwh % minutesPerDay == 0, "A civil-day minute must have an exact amount numerator");
    const auto day = civilDayAt(plan.clock,now);
    if (day.status != CivilStatus::Ok) return ElectricityStatus::Unavailable;
    const auto begin = (std::max)(opening.cycleBeginUtcMs,day.beginUtcMs);
    const auto end = (std::min)(opening.cycleEndUtcMs,day.endUtcMs);
    const auto duration = end-begin;
    const auto dayDuration = day.endUtcMs-day.beginUtcMs;
    if (dayDuration<=0 || dayDuration%minuteMs || duration<=0 || duration%minuteMs)
        return ElectricityStatus::Unsupported;

    // Both sides of a reset must use the same fee, even if a different revision governs energy at the reset.
    const auto rate = std::find_if(plan.revisions.begin(),plan.revisions.end(),[&](const TariffRevision &revision) {
        return revision.beginUtcMs <= day.beginUtcMs && day.beginUtcMs < revision.endUtcMs;
    });
    if (rate == plan.revisions.end()) return ElectricityStatus::Unavailable;
    if (rate->publishedUtcMs > decision) return ElectricityStatus::NotYetKnown;
    if(dayDuration==dayMs||duration==dayDuration){
        const auto numerator=duration==dayDuration?wattMsPerKwh:(wattMsPerKwh/minutesPerDay)*(duration/minuteMs);
        return productAdd(cost,rate->chargePerCivilDay,number(numerator))?ElectricityStatus::Ok:ElectricityStatus::Capacity;
    }
    ExactDecimal weighted, share;
    if(!rate->chargePerCivilDay.multiply(number(wattMsPerKwh*(duration/minuteMs)),&weighted))
        return ElectricityStatus::Capacity;
    if(!weighted.divideExact(static_cast<uint32_t>(dayDuration/minuteMs),&share))
        return ElectricityStatus::Unsupported;
    return accumulate(cost,share)?ElectricityStatus::Ok:ElectricityStatus::Capacity;
}

ElectricityStatus validateTaxDailyFees(const TariffPlan &plan, const BillOpening &opening, int64_t decision,
                                       DailyFeeAllocation allocation, const std::vector<TaxSlice> &taxSlices) {
    if (taxSlices.empty() || opening.throughUtcMs == opening.cycleBeginUtcMs) return ElectricityStatus::Ok;
    std::array<ExactDecimal, maxTaxPeriods> expected;
    size_t revision = 0, segment = 0;
    std::optional<CivilDate> previous;
    for (auto cursor = opening.cycleBeginUtcMs; cursor < opening.throughUtcMs;) {
        const auto day = civilDayAt(plan.clock, cursor);
        if (day.status != CivilStatus::Ok) return ElectricityStatus::Unavailable;
        if (cursor < day.beginUtcMs || cursor >= day.endUtcMs || !civilDayIsContiguous(plan.clock, day) ||
            (previous && civilSerialDay(day.date) <= civilSerialDay(*previous))) return ElectricityStatus::Unsupported;
        while (revision < plan.revisions.size() && plan.revisions[revision].endUtcMs <= cursor) ++revision;
        if (revision == plan.revisions.size() || plan.revisions[revision].beginUtcMs > cursor)
            return ElectricityStatus::Unavailable;
        const auto &rate = plan.revisions[revision];
        if (rate.publishedUtcMs > decision) return ElectricityStatus::NotYetKnown;
        while (taxSlices[segment].end <= cursor) ++segment;
        const auto status = addDailyFee(plan, opening, rate, cursor, decision, allocation, expected[segment]);
        if (status != ElectricityStatus::Ok) return status;
        previous = day.date;
        cursor = day.endUtcMs;
    }
    constexpr auto daily = static_cast<size_t>(TaxComponent::DailyFee);
    if (opening.tax)
        return expected.front().compare(opening.tax->components[daily].numerator) == 0
            ? ElectricityStatus::Ok : ElectricityStatus::Invalid;
    for (size_t i = 0; i < opening.taxSegments->size(); ++i)
        if (expected[i].compare((*opening.taxSegments)[i].bases.components[daily].numerator) != 0)
            return ElectricityStatus::Invalid;
    return ElectricityStatus::Ok;
}

ElectricityStatus validateTaxEnergy(const TariffPlan &plan, const BillOpening &opening, int64_t decision,
                                    const std::vector<TaxSlice> &slices) {
    if (!opening.taxSegments) return ElectricityStatus::Ok;
    const bool roundMeter = plan.rounding == TariffRounding::MeterHalfHourKwhHalfEven2;
    const auto &segments = *opening.taxSegments;
    ExactDecimal cumulative;
    size_t revision = 0, cuts = 0;
    for (size_t i = 0; i < segments.size(); ++i) {
        const auto &segment = segments[i];
        const auto &measured = segment.importedKwh.numerator;
        const auto &rated = roundMeter ? segment.bases.billedKwh.numerator : measured;
        ExactDecimal nextCumulative;
        if (!cumulative.add(measured, &nextCumulative)) return ElectricityStatus::Capacity;
        if (!rated.zero()) {
            // Segment totals omit the distribution of energy across rate changes.
            // Bound the possible charge using every published marginal rate.
            ExactDecimal lowest, highest;
            std::vector<TariffBand> lowBands, highBands;
            const auto end = (std::min)(slices[i].end, opening.throughUtcMs);
            bool covered = true, seenRate = false;
            for (auto cursor = slices[i].begin; cursor < end;) {
                if (++cuts > maxCuts) return ElectricityStatus::Capacity;
                while (plan.revisions[revision].endUtcMs <= cursor) ++revision;
                const auto &entry = plan.revisions[revision];
                auto next = (std::min)(end, entry.endUtcMs);
                ExactDecimal rate;
                CivilStamp stamp;
                const auto status = timeRate(plan, entry, cursor, decision, rate, next, stamp);
                if (status == ElectricityStatus::MissingPrice || status == ElectricityStatus::NotYetKnown) {
                    covered = false;
                    break;
                }
                if (status != ElectricityStatus::Ok) return status;
                if (next <= cursor) return ElectricityStatus::Invalid;
                if (plan.tiers == TierMode::Progressive) {
                    if (!seenRate)
                        lowBands = highBands = entry.bands;
                    // The tariff's energy rate and tier surcharge apply at the same time.
                    for (size_t j = 0; j < entry.bands.size(); ++j) {
                        ExactDecimal marginalRate;
                        if (!rate.add(entry.bands[j].ratePerKwh, &marginalRate)) return ElectricityStatus::Capacity;
                        if (!seenRate || marginalRate.compare(lowBands[j].ratePerKwh) < 0)
                            lowBands[j].ratePerKwh = marginalRate;
                        if (!seenRate || marginalRate.compare(highBands[j].ratePerKwh) > 0)
                            highBands[j].ratePerKwh = std::move(marginalRate);
                    }
                } else {
                    if (!seenRate || rate.compare(lowest) < 0) lowest = rate;
                    if (!seenRate || rate.compare(highest) > 0) highest = std::move(rate);
                }
                seenRate = true;
                cursor = next;
            }
            if (covered) {
                const auto amountAt = [&](const ExactDecimal &rate, const std::vector<TariffBand> &bands,
                                          ExactDecimal &amount) {
                    if (plan.tiers == TierMode::Progressive)
                        return progressiveTierChange(bands, cumulative, nextCumulative, amount);
                    if (!rated.multiply(rate, &amount)) return false;
                    return plan.tiers != TierMode::AllUnits ||
                        productAdd(amount, rated, allUnitsRate(plan.revisions.front().bands, opening.importedKwh.numerator));
                };
                ExactDecimal lowCharge, highCharge;
                if (!amountAt(lowest, lowBands, lowCharge) || !amountAt(highest, highBands, highCharge))
                    return ElectricityStatus::Capacity;
                const auto &declared = segment.bases.components[static_cast<size_t>(TaxComponent::Energy)].numerator;
                if (declared.compare(lowCharge) < 0 || declared.compare(highCharge) > 0)
                    return ElectricityStatus::Invalid;
            }
        }
        cumulative = std::move(nextCumulative);
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
        case ElectricityStatus::MeterBoundary: return "meter register needs a reading at an energy-rate boundary";
        case ElectricityStatus::MeterRoundingBoundary: return "meter rounding needs readings and cycle boundaries on the UTC half hour";
        case ElectricityStatus::SeasonalDayBoundary: return "seasonal allowance requires complete tariff civil days without skipped or disjoint dates";
        case ElectricityStatus::DemandWindowBoundary: return "demand requires complete aligned windows and nonzero meter deltas within each window";
        case ElectricityStatus::DemandHistoryNotYetKnown: return "demand history extends beyond the decision instant";
        case ElectricityStatus::DemandApparentEvidence: return "apparent demand needs whole-meter VA, an allowed power factor, or paired kWh/kVAh readings";
        case ElectricityStatus::ExportEvidenceMissing: return "export payment needs signed whole-meter power or both cumulative energy registers";
        case ElectricityStatus::ExportRateBoundary: return "export register needs a reading at an export-rate boundary";
        case ElectricityStatus::TaxPeriodBoundary: return "one declared tax period must cover the full supply cycle";
        case ElectricityStatus::TaxScheduleBoundary: return "declared tax periods must cover the supply cycle without gaps";
        case ElectricityStatus::TaxMeterBoundary: return "a nonzero meter interval crosses a tax period boundary";
    }
    return "unknown electricity status";
}
std::optional<ElectricityAmount> electricityKwh(const ExactDecimal &kwh) {
    ElectricityAmount result;
    if (kwh.negative() || !kwh.multiply(number(wattMsPerKwh),&result.numerator)) return std::nullopt;
    return result;
}
bool progressiveTierChange(const std::vector<TariffBand> &bands,const ExactDecimal &before,
                           const ExactDecimal &after,ExactDecimal &change) {
    ExactDecimal oldCost, newCost;
    return tierCost(bands,TierMode::Progressive,before,&oldCost) &&
           tierCost(bands,TierMode::Progressive,after,&newCost) &&
           newCost.subtract(oldCost,&change);
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
bool apparentDemandFromPowerFactor(const DemandChargeRule &rule,const ExactDecimal &watts,
                                   const ExactDecimal &factor,ExactDecimal &voltAmps) {
    if (!rule.apparent() || !rule.powerFactorVaPlaces || *rule.powerFactorVaPlaces>18 || watts.negative() ||
        factor.negative() || factor.zero() || factor.compare(number(1))>0)
        return false;
    std::string formatted;
    ExactDecimal converted;
    if (!watts.formatRatio(factor,*rule.powerFactorVaPlaces,&formatted) ||
        !ExactDecimal::parse(formatted,&converted) || converted.compare(watts)<0)
        return false;
    voltAmps=std::move(converted);
    return true;
}

bool billableDemand(const DemandChargeRule &rule,const BillOpening &opening,
                    ElectricityAmount &peak,ElectricityAmount &ratchetFloor) {
    if (opening.apparentDemand!=rule.apparent() || !opening.peakDemand) return false;
    ratchetFloor={};
    if (rule.ratchet) {
        const auto &history=opening.demandHistory;
        const auto largest=std::max_element(history.begin(),history.end(),[](const auto &a,const auto &b){
            return a.numerator.compare(b.numerator)<0;
        });
        if (largest==history.end() ||
            !largest->numerator.multiply(rule.ratchet->fraction,&ratchetFloor.numerator))
            return false;
    }
    peak=opening.peakDemand->numerator.compare(ratchetFloor.numerator)>=0
        ? *opening.peakDemand : ratchetFloor;
    return true;
}

ElectricityStatus validateTariffOpening(const TariffPlan &p, const BillOpening &opening, DailyFeeAllocation allocation) {
    std::vector<TaxSlice> taxSlices;
    const auto status = validatePlan(p,opening,opening.cycleEndUtcMs,lastUtcMs,false,&taxSlices);
    if (status != ElectricityStatus::Ok) return status;
    if (allocation != DailyFeeAllocation::WholeDay && allocation != DailyFeeAllocation::ProrateByCycleMinutes)
        return ElectricityStatus::Unsupported;
    const auto dailyStatus = validateTaxDailyFees(p, opening, lastUtcMs, allocation, taxSlices);
    if (dailyStatus != ElectricityStatus::Ok) return dailyStatus;
    return validateTaxEnergy(p, opening, lastUtcMs, taxSlices);
}
static bool accrueRestrictedCredit(BillOpening &state,int64_t end,const ExactDecimal &multiplier,
                                   BillRestrictedCreditAccrual &credit) {
    auto &opening=*state.restrictedCredit;
    auto &lots=opening.lots;
    if (!opening.accruedImportSubtotal.numerator.negative() &&
        !opening.accruedImportSubtotal.numerator.multiply(multiplier,&credit.eligible.numerator)) return false;

    // Expiry has effect at the exact instant, before a settlement at that instant.
    const auto firstLive=std::find_if(lots.begin(),lots.end(),[end](const RestrictedCreditLot &lot){
        return lot.expiresUtcMs>end;
    });
    for (auto it=lots.begin();it!=firstLive;++it)
        if (!accumulate(credit.expired.numerator,it->remaining.numerator)) return false;
    lots.erase(lots.begin(),firstLive);

    if (end==state.cycleEndUtcMs) {
        auto &cash=credit.cashDue.emplace().numerator;
        if (!state.accruedSubtotal.numerator.negative() &&
            !state.accruedSubtotal.numerator.multiply(multiplier,&cash)) return false;
        auto limit=cash.compare(credit.eligible.numerator)<0?cash:credit.eligible.numerator;
        for (auto &lot:lots) {
            auto &balance=lot.remaining.numerator;
            const auto applied=balance.compare(limit)<0?balance:limit;
            if (!balance.subtract(applied,&balance) || !limit.subtract(applied,&limit) ||
                !accumulate(credit.applied.numerator,applied)) return false;
            if (limit.zero()) break;
        }
        if (!cash.subtract(credit.applied.numerator,&cash)) return false;
        lots.erase(std::remove_if(lots.begin(),lots.end(),[](const RestrictedCreditLot &lot){
            return lot.remaining.numerator.zero();
        }),lots.end());
    }
    for (const auto &lot:lots)
        if (!accumulate(credit.remaining.numerator,lot.remaining.numerator)) return false;
    return true;
}

template<class Trace>
static BillEstimate evaluateBillImpl(const TariffPlan &p,const BillOpening &opening,const Trace &trace,int64_t end,int64_t decision,bool accrual,
                                     const std::atomic<bool> *cancel=nullptr, const std::function<bool()> &stop={},
                                     DailyFeeAllocation dailyFeeAllocation=DailyFeeAllocation::WholeDay) {
    BillEstimate result; result.currency = p.currency; result.evidence = trace.evidence; result.beginUtcMs = opening.throughUtcMs; result.endUtcMs = end;
    result.planKey = p.key; result.territory = p.territory; result.supplier = p.supplier; result.product = p.product; result.origin = p.origin; result.clock = p.clock; result.decisionUtcMs = decision;
    const bool roundMeter = p.rounding == TariffRounding::MeterHalfHourKwhHalfEven2;
    if constexpr (!std::is_same_v<Trace, MeterImportTrace>) {
        if (roundMeter) { result.status = ElectricityStatus::Unsupported; return result; }
    }
    std::vector<TaxSlice> taxSlices;
    result.status = validatePlan(p,opening,end,decision,accrual,&taxSlices);
    if (result.status != ElectricityStatus::Ok) return result;
    if (dailyFeeAllocation != DailyFeeAllocation::WholeDay &&
        (dailyFeeAllocation != DailyFeeAllocation::ProrateByCycleMinutes ||
         opening.cycleBeginUtcMs % minuteMs || opening.cycleEndUtcMs % minuteMs)) {
        result.status = ElectricityStatus::Unsupported;
        return result;
    }
    result.status = validateTaxDailyFees(p, opening, decision, dailyFeeAllocation, taxSlices);
    if (result.status != ElectricityStatus::Ok) return result;
    result.status = validateTaxEnergy(p, opening, decision, taxSlices);
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
    if (state.taxSegments) {
        auto &segments = *state.taxSegments;
        for (size_t i = segments.size(); i < taxSlices.size() && taxSlices[i].begin < end; ++i)
            segments.push_back({taxSlices[i].begin, {}, {}});
    }
    std::optional<ElectricityAmount> exportPayment;
    if (p.exportPayment) {
        ElectricityAmount newExport;
        result.status=priceExport(*p.exportPayment,trace,decision,newExport,exportPayment.emplace(),cancel,stop);
        if (result.status!=ElectricityStatus::Ok) return result;
        if (!accumulate(state.exports->exportedKwh.numerator,newExport.numerator) ||
            !accumulate(state.exports->accruedPayment.numerator,exportPayment->numerator)) {
            result.status=ElectricityStatus::Capacity;
            return result;
        }
    }
    ExactDecimal cost, billedQuantity, quantity = state.importedKwh.numerator;
    if (p.demandCharge) {
        result.status=measureDemand(*p.demandCharge,trace,opening.throughUtcMs,*state.peakDemand,cancel,stop);
        if (result.status!=ElectricityStatus::Ok) return result;
        ElectricityAmount nextBillable, openingBillable, floor;
        ExactDecimal increase, demandCharge;
        // A new cycle has no posted demand subtotal yet, even if a ratchet floor already applies.
        if (!billableDemand(*p.demandCharge,state,nextBillable,floor) ||
            (opening.throughUtcMs!=opening.cycleBeginUtcMs &&
             !billableDemand(*p.demandCharge,opening,openingBillable,floor)) ||
            !nextBillable.numerator.subtract(openingBillable.numerator,&increase) ||
            !increase.multiply(p.demandCharge->ratePerUnit, &demandCharge) ||
            !accumulateCharge(cost, state, TaxComponent::Demand, demandCharge)) {
            result.status=ElectricityStatus::Capacity;
            return result;
        }
    }
    if (opening.throughUtcMs == opening.cycleBeginUtcMs) {
        ExactDecimal cycleFee;
        if (!p.cycleCharge.multiply(number(wattMsPerKwh), &cycleFee) ||
            !accumulateCharge(cost, state, TaxComponent::CycleFee, cycleFee,
                              state.taxSegments ? &state.taxSegments->front() : nullptr)) {
            result.status = ElectricityStatus::Capacity;
            return result;
        }
    }
    size_t revision = 0, count = 0, taxIndex = 0;
    std::optional<CivilDate> previousDate;
    if (opening.throughUtcMs > opening.cycleBeginUtcMs) {
        const auto past = civilAt(p.clock,opening.throughUtcMs-1);
        if (past.status != CivilStatus::Ok) { result.status = ElectricityStatus::Unavailable; return result; }
        previousDate = past.date;
    }
    using Intervals=std::decay_t<decltype(trace.intervals)>;
    Intervals halfHours;
    const auto *billingIntervals=&trace.intervals;
    if constexpr (std::is_same_v<Trace,MeterImportTrace>) {
        const bool scheduledExport=p.exportPayment && !p.exportPayment->dynamic.empty();
        if (roundMeter && (p.demandCharge || scheduledExport)) {
            // Demand and scheduled exports use the original deltas. Round imports once per half hour.
            halfHours.reserve(static_cast<size_t>((end-opening.throughUtcMs)/meterHalfHourMs));
            result.status=foldHalfHours(trace,halfHours,cancel,stop);
            if (result.status!=ElectricityStatus::Ok) return result;
            billingIntervals=&halfHours;
        }
    }
    for (const auto &i : *billingIntervals) {
        if (roundMeter && (i.beginUtcMs % meterHalfHourMs || i.endUtcMs - i.beginUtcMs != meterHalfHourMs)) {
            result.status = ElectricityStatus::MeterRoundingBoundary;
            return result;
        }
        int64_t now = i.beginUtcMs;
        [[maybe_unused]] const TariffRevision *firstRevision = nullptr;
        [[maybe_unused]] ExactDecimal firstRate;
        while (now < i.endUtcMs) {
            if ((cancel && cancel->load(std::memory_order_relaxed)) || (stop && stop())) {
                result.status = ElectricityStatus::Unavailable;
                return result;
            }
            if (++count > maxCuts) { result.status = ElectricityStatus::Capacity; return result; }
            while (revision < p.revisions.size() && p.revisions[revision].endUtcMs <= now) ++revision;
            if (revision == p.revisions.size()) { result.status = ElectricityStatus::Unavailable; return result; }
            const auto &r = p.revisions[revision];
            int64_t next = (std::min)(i.endUtcMs,r.endUtcMs);
            BillTaxSegment *taxSegment = nullptr;
            if (state.taxSegments) {
                while (taxSlices[taxIndex].end <= now) ++taxIndex;
                const auto taxEnd = taxSlices[taxIndex].end;
                taxSegment = &(*state.taxSegments)[taxIndex];
                if constexpr (std::is_same_v<Trace, MeterImportTrace>) {
                    if (i.endUtcMs > taxEnd && !i.importedKwh.numerator.zero()) {
                        result.status = ElectricityStatus::TaxMeterBoundary;
                        return result;
                    }
                }
                cut(taxEnd,now,next);
            }
            ExactDecimal rate; CivilStamp stamp;
            result.status = timeRate(p,r,now,decision,rate,next,stamp);
            if (result.status != ElectricityStatus::Ok) return result;
            if (next <= now) { result.status = ElectricityStatus::Invalid; return result; }
            if (!previousDate || !sameCivilDate(*previousDate,stamp.date)) {
                if (previousDate && civilSerialDay(stamp.date) < civilSerialDay(*previousDate)) { result.status = ElectricityStatus::Unsupported; return result; }
                ExactDecimal dailyFee;
                result.status = addDailyFee(p,opening,r,now,decision,dailyFeeAllocation,dailyFee);
                if (result.status != ElectricityStatus::Ok) return result;
                if (!accumulateCharge(cost, state, TaxComponent::DailyFee, dailyFee, taxSegment)) {
                    result.status = ElectricityStatus::Capacity;
                    return result;
                }
                previousDate = stamp.date;
            }
            ExactDecimal energy, newQuantity;
            if constexpr (std::is_same_v<Trace, MeterImportTrace>) {
                energy = i.importedKwh.numerator;
                if (!firstRevision) {
                    firstRevision = &r;
                    firstRate = rate;
                } else if (!energy.zero() && !sameMeterRates(firstRate,*firstRevision,rate,r,p.tiers)) {
                    result.status = ElectricityStatus::MeterBoundary;
                    return result;
                }
                // Walk every time segment for fees and rate validation, then apply the known energy once.
                if (next < i.endUtcMs) { now = next; continue; }
            } else if (!i.watts.multiply(number(next-now),&energy)) {
                result.status = ElectricityStatus::Capacity;
                return result;
            }
            if (!quantity.add(energy,&newQuantity)) { result.status = ElectricityStatus::Capacity; return result; }
            if (taxSegment && !accumulate(taxSegment->importedKwh.numerator, energy)) {
                result.status = ElectricityStatus::Capacity;
                return result;
            }
            if (roundMeter) {
                // Keep the physical register exact. Round only the quantity rated for this complete interval.
                std::string rounded;
                ExactDecimal roundedKwh;
                if (!energy.formatRatio(wattMsPerKwh, 2, &rounded) || !ExactDecimal::parse(rounded, &roundedKwh)) {
                    result.status = ElectricityStatus::Capacity;
                    return result;
                }
                const auto billed = electricityKwh(roundedKwh);
                if (!billed || !accumulate(billedQuantity, billed->numerator) ||
                    (taxSegment && !accumulate(taxSegment->bases.billedKwh.numerator, billed->numerator))) {
                    result.status = ElectricityStatus::Capacity;
                    return result;
                }
                energy = billed->numerator;
            }
            ExactDecimal energyCharge;
            if (!energy.multiply(rate, &energyCharge) ||
                !accumulateCharge(cost, state, TaxComponent::Energy, energyCharge, taxSegment) ||
                (state.restrictedCredit && !accumulate(state.restrictedCredit->accruedImportSubtotal.numerator, energyCharge))) {
                result.status = ElectricityStatus::Capacity;
                return result;
            }
            if (p.tiers == TierMode::Progressive) {
                ExactDecimal delta;
                if (!progressiveTierChange(r.bands,quantity,newQuantity,delta) ||
                    !accumulateCharge(cost,state,TaxComponent::Energy,delta,taxSegment) ||
                    (state.restrictedCredit && !accumulate(state.restrictedCredit->accruedImportSubtotal.numerator,delta))) {
                    result.status = ElectricityStatus::Capacity;
                    return result;
                }
            }
            quantity = newQuantity; now = next;
        }
    }
    if (p.tiers == TierMode::AllUnits) {
        ExactDecimal before, after, delta;
        if (!tierCost(p.revisions[0].bands,p.tiers,opening.importedKwh.numerator,&before) || !tierCost(p.revisions[0].bands,p.tiers,quantity,&after) || !after.subtract(before,&delta) ||
            !accumulateCharge(cost,state,TaxComponent::Energy,delta) ||
            (state.restrictedCredit && !accumulate(state.restrictedCredit->accruedImportSubtotal.numerator,delta))) {
            result.status = ElectricityStatus::Capacity;
            return result;
        }
        if (state.taxSegments) {
            const auto &bands = p.revisions.front().bands;
            const auto &oldRate = allUnitsRate(bands, opening.importedKwh.numerator);
            const auto &newRate = allUnitsRate(bands, quantity);
            const auto &earlier = *opening.taxSegments;
            ExactDecimal rateChange;
            if (!newRate.subtract(oldRate, &rateChange)) {
                result.status = ElectricityStatus::Capacity;
                return result;
            }
            for (size_t i = 0; i < state.taxSegments->size(); ++i) {
                auto &segment = (*state.taxSegments)[i];
                const auto &previous = i < earlier.size() ? earlier[i].importedKwh.numerator : ExactDecimal{};
                ExactDecimal added;
                auto &base = taxComponent(segment.bases, TaxComponent::Energy).numerator;
                if (!segment.importedKwh.numerator.subtract(previous, &added) ||
                    !productAdd(base, previous, rateChange) || !productAdd(base, added, newRate)) {
                    result.status = ElectricityStatus::Capacity;
                    return result;
                }
            }
        }
    }
    if (!opening.accruedSubtotal.numerator.add(cost,&state.accruedSubtotal.numerator)) { result.status = ElectricityStatus::Capacity; return result; }
    if (p.minimumCycleSubtotal && end==opening.cycleEndUtcMs) {
        ExactDecimal floor;
        if (!p.minimumCycleSubtotal->multiply(number(wattMsPerKwh),&floor)) { result.status = ElectricityStatus::Capacity; return result; }
        if (state.accruedSubtotal.numerator.compare(floor) < 0) state.accruedSubtotal.numerator = floor;
        if (!state.accruedSubtotal.numerator.subtract(opening.accruedSubtotal.numerator,&cost)) { result.status = ElectricityStatus::Capacity; return result; }
    }
    ElectricityAmount charge;
    state.importedKwh.numerator = quantity; state.throughUtcMs = end;
    if (!taxSlices.empty()) {
        BillTaxTotal before, after;
        if ((roundMeter && state.tax && !accumulate(state.tax->billedKwh.numerator, billedQuantity)) ||
            !calculateTaxes(p, taxSlices, opening, before) || !calculateTaxes(p, taxSlices, state, after) ||
            !after.total.numerator.subtract(before.total.numerator, &charge.numerator)) {
            result.status = ElectricityStatus::Capacity;
            return result;
        }
        result.tax = std::move(after);
    } else if (!cost.multiply(*p.subtotalMultiplier,&charge.numerator)) { result.status = ElectricityStatus::Capacity; return result; }
    if (state.restrictedCredit) {
        BillRestrictedCreditAccrual credit;
        if (!accrueRestrictedCredit(state,end,*p.subtotalMultiplier,credit)) {
            result.status=ElectricityStatus::Capacity;
            return result;
        }
        result.restrictedCredit=std::move(credit);
    }
    if (roundMeter) result.billedKwh = ElectricityAmount{billedQuantity};
    result.ending = state; result.charge = charge; result.cycleSettled = end == state.cycleEndUtcMs; result.status = ElectricityStatus::Ok;
    result.exportPayment=std::move(exportPayment);
    return result;
}
BillEstimate evaluateBill(const TariffPlan &p,const BillOpening &opening,const PowerTrace &trace,
                          int64_t end,int64_t decision,DailyFeeAllocation allocation,const std::atomic<bool> *cancel) {
    return evaluateBillImpl(p,opening,trace,end,decision,false,cancel,{},allocation);
}
template<class Trace>
static BillAccrual accrueBillImpl(const TariffPlan &p,const BillOpening &opening,const Trace &trace,int64_t end,int64_t observed,
                                 const std::function<bool()> &stop,DailyFeeAllocation dailyFeeAllocation) {
    BillAccrual result;
    if(stop&&stop())return result;
    if(end>observed){result.status=ElectricityStatus::NotYetKnown;return result;}
    auto step=evaluateBillImpl(p,opening,trace,end,observed,true,nullptr,stop,dailyFeeAllocation);result.status=step.status;
    if(step.status!=ElectricityStatus::Ok)return result;
    ElectricityAmount total;
    if (step.tax) total=step.tax->total;
    else if(!step.ending->accruedSubtotal.numerator.multiply(*p.subtotalMultiplier,&total.numerator)){result.status=ElectricityStatus::Capacity;return result;}
    if (p.exportPayment) {
        BillExportAccrual exported;
        exported.paymentChange=*step.exportPayment;
        exported.paymentTotal=step.ending->exports->accruedPayment;
        if (!step.ending->exports->exportedKwh.numerator.subtract(opening.exports->exportedKwh.numerator,&exported.newKwh.numerator) ||
            !step.netChange(exported.netChange) ||
            !total.numerator.subtract(exported.paymentTotal.numerator,&exported.netTotal.numerator)) {
            result.status=ElectricityStatus::Capacity;
            return result;
        }
        result.exportAccrual=std::move(exported);
    }
    result.ending=step.ending;result.provisionalChange=step.charge;result.provisionalTotal=total;
    result.billedKwh=step.billedKwh;
    result.restrictedCredit=step.restrictedCredit;
    result.tax=std::move(step.tax);
    if(step.cycleSettled)result.settledTotal=total;
    else result.settlementDependent=p.tiers==TierMode::AllUnits||p.minimumCycleSubtotal.has_value()||p.demandCharge.has_value();
    return result;
}
BillAccrual accrueBill(const TariffPlan &p,const BillOpening &opening,const PowerTrace &trace,int64_t end,int64_t observed,
                      const std::function<bool()> &stop,DailyFeeAllocation dailyFeeAllocation) {
    return accrueBillImpl(p,opening,trace,end,observed,stop,dailyFeeAllocation);
}
BillAccrual accrueMeterImport(const TariffPlan &p,const BillOpening &opening,const MeterImportTrace &trace,int64_t end,int64_t observed,
                             const std::function<bool()> &stop,DailyFeeAllocation dailyFeeAllocation) {
    return accrueBillImpl(p,opening,trace,end,observed,stop,dailyFeeAllocation);
}
ExportPeriodEstimate estimateExportPeriod(const ExportPaymentRule &rule, const PowerTrace &trace,
    int64_t begin, int64_t end, int64_t observed, const std::function<bool()> &stop) {
    return estimateExportPeriodImpl(rule, trace, begin, end, observed, stop);
}
ExportPeriodEstimate estimateExportPeriod(const ExportPaymentRule &rule, const MeterImportTrace &trace,
    int64_t begin, int64_t end, int64_t observed, const std::function<bool()> &stop) {
    return estimateExportPeriodImpl(rule, trace, begin, end, observed, stop);
}
CounterfactualBill counterfactualBill(const TariffPlan &p, const BillOpening &opening, const PowerTrace &base, const PowerTrace &with, int64_t end, int64_t decision,
                                     const std::atomic<bool> *cancel, DailyFeeAllocation dailyFeeAllocation) {
    if (cancel && cancel->load(std::memory_order_relaxed)) return {};
    const auto &a = base.evidence, &b = with.evidence;
    if (!evidenceValid(a) || !evidenceValid(b) || a.basis != PowerBasis::WholeMeterImport || b.basis != a.basis || a.origin != b.origin || a.scope != b.scope || a.adapter != b.adapter)
        return {ElectricityStatus::Invalid, {}, {}, {}};
    // Reject truncated shared-meter scenarios even if this plan happens to have no discontinuous components.
    if (end != opening.cycleEndUtcMs) return {ElectricityStatus::Unsupported, {}, {}, {}};
    auto withoutMining = evaluateBillImpl(p,opening,base,end,decision,false,cancel,{},dailyFeeAllocation);
    if (cancel && cancel->load(std::memory_order_relaxed)) return {};
    if (withoutMining.status != ElectricityStatus::Ok) return {withoutMining.status, std::move(withoutMining), {}, {}};
    auto withMining = evaluateBillImpl(p,opening,with,end,decision,false,cancel,{},dailyFeeAllocation);
    if (cancel && cancel->load(std::memory_order_relaxed)) return {};
    if (withMining.status != ElectricityStatus::Ok) return {withMining.status, std::move(withoutMining), std::move(withMining), {}};
    ElectricityAmount delta, withCost, withoutCost;
    if (!withMining.netChange(withCost) || !withoutMining.netChange(withoutCost) ||
        !withCost.numerator.subtract(withoutCost.numerator,&delta.numerator))
        return {ElectricityStatus::Capacity, std::move(withoutMining), std::move(withMining), {}};
    return {ElectricityStatus::Ok, std::move(withoutMining), std::move(withMining), std::move(delta)};
}
}  // namespace om
