// Entirely synthetic posted-credit contract, not a pool schema or source qualification.
#pragma once
#include "product/credit_ledger.h"
#include <cstdlib>
namespace credit_fixture {
constexpr int64_t T = 1800000000000LL, day = 86400000;
inline om::CreditSourceContract contract() {
    om::CreditSourceContract c;
    c.qualification = om::CreditQualification::SyntheticFixture; c.asset = om::CreditAsset::PearlMainnetNative;
    c.provider = "fixture.posted-credit-provider"; c.contractVersion = "replacement-posted-ms-v1";
    c.scopeId = "synthetic-worker-A"; c.scopeGeneration = "synthetic-generation-1";
    c.timeBasis = om::CreditTimeBasis::PostedUtcMilliseconds; c.revisionModel = om::CreditRevisionModel::ReplacementValues;
    c.amountBasis = om::CreditAmountBasis::PostedNetOfPoolCharges;
    c.scaleDeclared = true; c.amountScale = 8; c.historyStartMs = T - 3 * day;
    return c;
}
inline om::CreditRevision record(std::string id, uint64_t revision, int64_t at, const std::string &amount,
                                 om::CreditKind kind = om::CreditKind::PostedCredit, std::string reference = {}) {
    om::CreditRevision r; r.id = std::move(id); r.revision = revision; r.postedAtMs = at; r.kind = kind; r.reference = std::move(reference);
    if (!om::ExactDecimal::parse(amount, &r.amount)) std::abort();
    return r;
}
inline om::CreditBatch batch(uint64_t sequence, int64_t now, std::vector<om::CreditRevision> records = {}) {
    om::CreditBatch b; b.binding = contract(); b.sequence = sequence; b.snapshotId = "synthetic-snapshot-" + std::to_string(sequence);
    b.sourceAsOfMs = now; b.observedAtMs = now; b.records = std::move(records); b.coverage = {{b.binding.historyStartMs - 1,now}}; return b;
}
}  // namespace credit_fixture
