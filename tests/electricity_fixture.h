#pragma once
#include "product/electricity.h"
#include <stdexcept>
namespace electricity_fixture {
using namespace om;
constexpr int64_t T = 1793145600000LL, hour = 3600000, day = 86400000; // 2026-10-28 UTC, entirely synthetic
inline ExactDecimal d(const std::string &text) { ExactDecimal n; if (!ExactDecimal::parse(text,&n)) throw std::runtime_error("invalid synthetic decimal"); return n; }
inline ElectricityAmount kwh(const std::string &s) { return *electricityKwh(d(s)); }
inline std::string display(const ElectricityAmount &a, unsigned places = 6) { std::string s; if (!a.format(places,&s)) throw std::runtime_error("synthetic amount overflow"); return s; }
inline PowerEvidence evidence() { return {PowerBasis::WholeMeterImport,PowerOrigin::SyntheticFixture,"fixture.shared-meter","fixture.held-watts"}; }
inline PowerTrace load(const std::string &watts, int64_t begin = T, int64_t end = T+hour) { return {evidence(),{{begin,end,d(watts)}}}; }
inline TariffPlan plan(int64_t begin = T, int64_t end = T+hour) {
    TariffPlan p; p.declared = true; p.origin = TariffOrigin::SyntheticFixture;
    p.key = "fixture.plan.v1"; p.territory = "fixture.region"; p.supplier = "fixture.supplier"; p.product = "fixture.product"; p.currency = "XTS";
    p.rounding = TariffRounding::FinalDisplayOnly; p.subtotalMultiplier = d("1");
    TariffRevision r; r.key = "fixture.revision.1"; r.beginUtcMs = begin; r.endUtcMs = end; r.publishedUtcMs = begin-1; r.baseRatePerKwh = d("3"); p.revisions.push_back(r);
    return p;
}
inline BillOpening opening(int64_t begin = T, int64_t end = T+hour) { BillOpening s; s.declared = true; s.planKey = "fixture.plan.v1"; s.evidence = evidence(); s.cycleBeginUtcMs = begin; s.throughUtcMs = begin; s.cycleEndUtcMs = end; return s; }
inline void tier(TariffPlan &p, TierMode mode) { p.tiers = mode; p.revisions[0].baseRatePerKwh = d("0"); p.revisions[0].bands = {{d("100"),d("1")},{std::nullopt,d("2")}}; }
}  // namespace electricity_fixture
