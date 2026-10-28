#include "product/credit_ledger.h"
#include "credit_fixture.h"
#include "product_test_util.h"
#include <fstream>
#include <algorithm>
using namespace om;
using namespace credit_fixture;
int main() {
    const auto root = std::filesystem::path(makeScratchDir("credit-ledger"));
    auto c = contract();
    const auto file = root / std::filesystem::u8path("synthetic-\xe6\xb5\x8b\xe8\xaf\x95.db");
    {
        CreditLedger unsupported;
        CHECK(unsupported.open(root/"unsupported.db", {}, T) == CreditLedgerStatus::Unsupported);
        CHECK(!std::filesystem::exists(root/"unsupported.db"));
        CHECK(unsupported.query(T-day,T,T).completeness == CreditCompleteness::Unavailable);
    }
    for (unsigned bad = 0; bad < 8; ++bad) {
        auto broken = c;
        if (bad == 0) broken.scaleDeclared = false;
        if (bad == 1) broken.timeBasis = CreditTimeBasis::Unsupported;
        if (bad == 2) broken.revisionModel = CreditRevisionModel::Unsupported;
        if (bad == 3) broken.scopeGeneration.clear();
        if (bad == 4) broken.provider = "unqualified.real-pool";
        if (bad == 5) broken.scope = CreditScope::Account;
        if (bad == 6) broken.historyStartMs = T + 1;
        if (bad == 7) broken.amountBasis = CreditAmountBasis::Unsupported;
        CreditLedger ledger;
        CHECK(ledger.open(root/("invalid-"+std::to_string(bad)+".db"),broken,T) == CreditLedgerStatus::Invalid);
    }
    {
        CreditLedger ledger;
        CHECK(ledger.open(file,c,T) == CreditLedgerStatus::Ok);
        CreditLedger competing;
        CHECK(competing.open(file,c,T) == CreditLedgerStatus::Storage);
        const auto unknown = ledger.query(T-day,T,T);
        CHECK(unknown.status == CreditLedgerStatus::Ok && unknown.completeness == CreditCompleteness::Unavailable && !unknown.completePostedNet);
        auto b = batch(1,T,{record("credit-A",1,T-2*day,"4.25")});
        CHECK(ledger.apply(b,T) == CreditLedgerStatus::Ok);
        auto sum = ledger.cumulative(T,T);
        CHECK(sum.completeness == CreditCompleteness::Complete && sum.completePostedNet && sum.completePostedNet->text() == "4.25");
        CHECK(sum.matchedRecords == 1 && sum.coveredMs == sum.requestedMs);
        auto replay = b; replay.observedAtMs = T + 1000;
        CHECK(ledger.apply(replay,T+1000) == CreditLedgerStatus::Replay);
        CHECK(ledger.cumulative(T,T+1000).observedAtMs == T);
        CHECK(!ledger.cumulative(T+1,T+1000).completePostedNet);
        b = batch(2,T+1000,{record("credit-A",2,T-2*day,"3.75")});
        CHECK(ledger.apply(b,T+1000) == CreditLedgerStatus::Ok);
        CHECK(ledger.cumulative(T+1000,T+1000).knownPostedNet.text() == "3.75");
        b = batch(3,T+2000,{record("credit-A",1,T-2*day,"4.25")});
        CHECK(ledger.apply(b,T+2000) == CreditLedgerStatus::Ok);
        CHECK(ledger.cumulative(T+2000,T+2000).knownPostedNet.text() == "3.75");
        auto conflicting = batch(4,T+3000,{record("credit-A",2,T-2*day,"99")});
        CHECK(ledger.apply(conflicting,T+3000) == CreditLedgerStatus::Conflict);
        CHECK(ledger.cumulative(T+2000,T+3000).knownPostedNet.text() == "3.75");
        b = batch(4,T+3000,{record("adjustment-A",1,T+3000,"-1.25",CreditKind::PostedAdjustment,"credit-A")});
        CHECK(ledger.apply(b,T+3000) == CreditLedgerStatus::Ok);
        CHECK(ledger.cumulative(T+3000,T+3000).completePostedNet->text() == "2.5");
        CHECK(ledger.query(T+3000-day,T+3000,T+3000).completePostedNet->text() == "-1.25");
        b = batch(5,T+4000); b.coverage.clear();
        CHECK(ledger.apply(b,T+4000) == CreditLedgerStatus::Ok);
        sum = ledger.cumulative(T+4000,T+4000);
        CHECK(sum.knownPostedNet.text() == "2.5" && !sum.completePostedNet && sum.completeness == CreditCompleteness::Partial);
        b = batch(6,T+5000,{record("credit-A",3,T+5000,"3")});
        CHECK(ledger.apply(b,T+5000) == CreditLedgerStatus::Ok);
        CHECK(ledger.query(T+5000-day,T+5000,T+5000).completePostedNet->text() == "1.75");
        b = batch(7,T+6000,{record("credit-A",4,T+6000,"0")});
        CHECK(ledger.apply(b,T+6000) == CreditLedgerStatus::Invalid);
        CHECK(ledger.cumulative(T+5000,T+6000).knownPostedNet.text() == "1.75");
        b.records.push_back(record("adjustment-A",2,T+3000,"0",CreditKind::PostedAdjustment,"credit-A"));
        CHECK(ledger.apply(b,T+6000) == CreditLedgerStatus::Ok);
        sum = ledger.cumulative(T+6000,T+6000);
        CHECK(sum.completePostedNet && sum.completePostedNet->zero() && sum.matchedRecords == 2);
        auto reordered = b; std::reverse(reordered.records.begin(),reordered.records.end()); reordered.observedAtMs = T+6001;
        CHECK(ledger.apply(reordered,T+6001) == CreditLedgerStatus::Replay);
        auto changedCoverage = b; changedCoverage.coverage.clear();
        CHECK(ledger.apply(changedCoverage,T+6001) == CreditLedgerStatus::Conflict);
        CHECK(ledger.apply(batch(9,T+8000),T+8000) == CreditLedgerStatus::SequenceGap);
        auto reused = batch(8,T+7000); reused.snapshotId = "synthetic-snapshot-1";
        CHECK(ledger.apply(reused,T+7000) == CreditLedgerStatus::Conflict);
        CHECK(ledger.apply(batch(2,T+1000),T+7000) == CreditLedgerStatus::StaleBatch);
        auto wrong = batch(8,T+7000); wrong.binding.scopeId = "synthetic-worker-B";
        CHECK(ledger.apply(wrong,T+7000) == CreditLedgerStatus::Attribution);
        wrong = batch(8,T+7000); wrong.binding.scopeGeneration = "synthetic-generation-2";
        CHECK(ledger.apply(wrong,T+7000) == CreditLedgerStatus::Attribution);
        wrong = batch(8,T+7000); wrong.binding.asset = CreditAsset::Unsupported;
        CHECK(ledger.apply(wrong,T+7000) == CreditLedgerStatus::Attribution);
        wrong = batch(8,T+7000); wrong.observedAtMs = T+7001;
        CHECK(ledger.apply(wrong,T+7000) == CreditLedgerStatus::Clock);
        CHECK(ledger.query(T-day,T+6000,T).status == CreditLedgerStatus::Invalid);
        CHECK(ledger.query(T-day,T,T).status == CreditLedgerStatus::Clock);
        for (const auto &bad : {record("precision",1,T+7000,"0.000000001"), record("negative-credit",1,T+7000,"-1"),
                               record("future",1,T+7001,"1"),record("orphan-adjustment",1,T+7000,"-1",CreditKind::PostedAdjustment,"absent"),
                               record("nested-adjustment",1,T+7000,"1",CreditKind::PostedAdjustment,"adjustment-A"),record("bad'identifier",1,T+7000,"1")})
            CHECK(ledger.apply(batch(8,T+7000,{bad}),T+7000) == CreditLedgerStatus::Invalid);
        CHECK(ledger.apply(batch(8,T+7000,{record("credit-A",1,T-2*day,"4")}),T+7000) == CreditLedgerStatus::Conflict);  // old audit revision is immutable too
        wrong = batch(8,T+7000,{record("credit-A",5,T+7000,"1",CreditKind::PostedAdjustment,"adjustment-A")});
        CHECK(ledger.apply(wrong,T+7000) == CreditLedgerStatus::Conflict);
        auto duplicate = record("same-id",1,T+7000,"1");
        CHECK(ledger.apply(batch(8,T+7000,{duplicate,duplicate}),T+7000) == CreditLedgerStatus::Invalid);
        CHECK(ledger.apply(batch(8,T+7000),T+7000) == CreditLedgerStatus::Ok);
    }
    {
        CreditLedger reopened;
        CHECK(reopened.open(file,c,T+8000) == CreditLedgerStatus::Ok);
        CHECK(reopened.cumulative(T+7000,T+8000).completePostedNet->zero());
        auto b = batch(9,T+8000,{record("new-credit",1,T+8000,"2.5")});
        CHECK(reopened.apply(b,T+8000) == CreditLedgerStatus::Ok);
        auto sum = reopened.cumulative(T+8000,T+8000);
        MarketQuote pearl, fx;
        pearl.observation = {MarketSeries::GatePearlMainnetUsdtV1,MarketOrigin::SyntheticFixture,static_cast<uint64_t>(T/1000),static_cast<uint64_t>(T/1000),1000,1};
        fx.observation = pearl.observation; fx.observation.series = MarketSeries::KrakenUsdtUsdV1;
        ExactDecimal::parse("2.5",&pearl.last); ExactDecimal::parse("0.98",&fx.last);
        auto value = creditProductionReference(sum,latestMarketReference(&pearl,&fx,T/1000+8,9000));
        CHECK(value.complete && value.knownUsdt->text() == "6.25" && value.knownUsd->text() == "6.125");
        ExactDecimal::parse("5",&pearl.last);
        value = creditProductionReference(sum,latestMarketReference(&pearl,&fx,T/1000+8,9000));
        CHECK(value.knownUsd->text() == "12.25" && sum.knownPostedNet.text() == "2.5");
        value = creditProductionReference(sum,latestMarketReference(&pearl,nullptr,T/1000+8,9000));
        CHECK(value.knownUsdt && !value.knownUsd);
    }
    {
        auto other = c; other.scopeId = "synthetic-worker-B";
        CreditLedger mismatch;
        CHECK(mismatch.open(file,other,T+9000) == CreditLedgerStatus::Attribution);
    }
    {
        CreditLedger zero;
        CHECK(zero.open(root/"zero.db",c,T) == CreditLedgerStatus::Ok);
        CHECK(zero.apply(batch(1,T),T) == CreditLedgerStatus::Ok);
        CHECK(zero.query(T-day,T,T).completePostedNet->zero());
        auto b = batch(2,T+1); b.coverage.clear();
        CHECK(zero.apply(b,T+1) == CreditLedgerStatus::Ok);
        CHECK(zero.query(T-day,T+1,T+1).completeness == CreditCompleteness::Unavailable);
        b = batch(3,T+2); b.coverage = {{T-day,T-1000},{T-1000,T+2}};
        CHECK(zero.apply(b,T+2) == CreditLedgerStatus::Ok);
        CHECK(zero.query(T-day,T+2,T+2).completePostedNet->zero());
        b = batch(4,T+3); b.coverage = {{T-day,T-1000},{T-999,T+3}};
        CHECK(zero.apply(b,T+3) == CreditLedgerStatus::Ok);
        const auto q = zero.query(T-day,T+3,T+3);
        CHECK(!q.completePostedNet && q.coveredMs == q.requestedMs - 1);
    }
    {
        CreditLedger boundary;
        CHECK(boundary.open(root/"boundary.db",c,T) == CreditLedgerStatus::Ok);
        CHECK(boundary.apply(batch(1,T,{record("left-excluded",1,T-day,"1"),record("left-included",1,T-day+1,"2"),record("right-included",1,T,"4")}),T) == CreditLedgerStatus::Ok);
        CHECK(boundary.query(T-day,T,T).completePostedNet->text() == "6");
        CHECK(boundary.cumulative(T,T).completePostedNet->text() == "7");
        CHECK(!boundary.query(c.historyStartMs-1000,T,T).completePostedNet);  // unknown prehistory cannot become complete
    }
    {
        CreditLedger capacity;
        CreditLedgerLimits limits; limits.maxRecords = 2; limits.maxVersions = 3; limits.maxDatabaseBytes = 1024*1024;
        CHECK(capacity.open(root/"capacity.db",c,T,limits) == CreditLedgerStatus::Ok);
        CHECK(capacity.apply(batch(1,T,{record("A",1,T,"1"),record("B",1,T,"1")}),T) == CreditLedgerStatus::Ok);
        CHECK(capacity.apply(batch(2,T+1,{record("C",1,T+1,"9")}),T+1) == CreditLedgerStatus::Capacity);
        CHECK(capacity.cumulative(T,T+1).completePostedNet->text() == "2");
        CHECK(capacity.apply(batch(2,T+1,{record("A",2,T,"3")}),T+1) == CreditLedgerStatus::Ok);
        CHECK(capacity.apply(batch(3,T+2,{record("A",3,T,"4")}),T+2) == CreditLedgerStatus::Capacity);
        CHECK(capacity.cumulative(T+1,T+2).completePostedNet->text() == "4");
    }
    {
        CreditLedger exact;
        CHECK(exact.open(root/"exact.db",c,T) == CreditLedgerStatus::Ok);
        CHECK(exact.apply(batch(1,T,{record("large",1,T,"9007199254740993.00000001"),record("small",1,T,"0.00000001")}),T) == CreditLedgerStatus::Ok);
        CHECK(exact.cumulative(T,T).completePostedNet->text() == "9007199254740993.00000002");
    }
    {
        CreditLedger late;
        CHECK(late.open(root/"late.db",c,T) == CreditLedgerStatus::Ok);
        CHECK(late.apply(batch(1,T),T) == CreditLedgerStatus::Ok);
        CHECK(late.apply(batch(2,T+1000,{record("late-source-credit",1,T-2*day,"5")}),T+1000) == CreditLedgerStatus::Ok);
        CHECK(late.query(T+1000-day,T+1000,T+1000).completePostedNet->zero());
        CHECK(late.cumulative(T+1000,T+1000).completePostedNet->text() == "5");
    }
    {
        auto account = c; account.scope = CreditScope::Account; account.accountScopeAcknowledged = true;
        CreditLedger ledger;
        CHECK(ledger.open(root/"account.db",account,T) == CreditLedgerStatus::Ok);
        auto b = batch(1,T,{record("account-credit",1,T,"1")}); b.binding = account;
        CHECK(ledger.apply(b,T) == CreditLedgerStatus::Ok);
        CHECK(ledger.cumulative(T,T).scope == CreditScope::Account);
    }
    {
        const auto corrupt = root/"corrupt.db";
        std::ofstream(corrupt,std::ios::binary) << "synthetic truncated non-database";
        const auto size = std::filesystem::file_size(corrupt);
        CreditLedger ledger;
        CHECK(ledger.open(corrupt,c,T) == CreditLedgerStatus::Storage);
        CHECK(std::filesystem::file_size(corrupt) == size);
    }
    std::filesystem::remove_all(root);
    return finishTests("credit_ledger");
}
