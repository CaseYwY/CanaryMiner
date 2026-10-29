#include "billing_fixture.h"
#include "product_test_util.h"
#include <fstream>
using namespace billing_fixture;
namespace {
std::string bytes(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
}
int main(int argc,char **argv){
    const auto root=std::filesystem::path(makeScratchDir("billing-ledger"));auto b=binding();const auto file=root/"fixture.db";const auto now=T+2*hour;
    {BillingLedger absent;CHECK(absent.open(root/"missing"/"ledger.db",b,now)==BillingStatus::Storage&&!std::filesystem::exists(root/"missing"));}
    {auto real=b;real.evidence.origin=PowerOrigin::ObservedTelemetry;BillingLedger unsupported;CHECK(unsupported.open(root/"real.db",real,now,BillingOpen::CreateNew)==BillingStatus::Unsupported&&!std::filesystem::exists(root/"real.db"));}
    for(const auto *suffix:{"-wal","-shm","-journal"}){auto path=root/(std::string("orphan")+suffix+".db"),sidecar=path;sidecar+=suffix;
        {std::ofstream output(sidecar,std::ios::binary);output<<"fixture retained original";}
        BillingLedger ledger;CHECK(ledger.open(path,b,now,BillingOpen::CreateNew)==BillingStatus::Storage&&!std::filesystem::exists(path)&&bytes(sidecar)=="fixture retained original");}
    std::string firstDigest;
    {BillingLedger ledger;CHECK(ledger.open(file,b,now,BillingOpen::CreateNew)==BillingStatus::Ok);const auto empty=ledger.snapshot(now);
        CHECK(empty.status==BillingStatus::Ok&&!empty.provisionalTotal&&!empty.settledTotal&&empty.sequence==0&&empty.opening.importedKwh.numerator.zero());
        {BillingLedger rival;CHECK(rival.open(file,b,now)==BillingStatus::Storage);}
        CHECK(ledger.apply(chunk(2),now)==BillingStatus::SequenceGap);CHECK(ledger.apply(chunk(),T)==BillingStatus::Clock);
        CHECK(ledger.apply(chunk(),now)==BillingStatus::Ok);firstDigest=ledger.snapshot(now).digest;
        CHECK(ledger.apply(chunk(),now)==BillingStatus::Replay&&ledger.snapshot(now).digest==firstDigest&&ledger.snapshot(now).observedUtcMs==T+hour);
        CHECK(display(*ledger.snapshot(now).provisionalTotal)=="3.000000"&&!ledger.snapshot(now).settledTotal);
        auto changed=chunk();changed.trace.intervals[0].watts=d("999");CHECK(ledger.apply(changed,now)==BillingStatus::Conflict);
        changed=chunk(2);changed.id=chunk().id;CHECK(ledger.apply(changed,now)==BillingStatus::Conflict);
        changed=chunk(2);changed.trace.evidence.scope="fixture.other";CHECK(ledger.apply(changed,now)==BillingStatus::Attribution);
        changed=chunk(2);++changed.trace.intervals[0].beginUtcMs;CHECK(ledger.apply(changed,now)==BillingStatus::Calculation);
        changed=chunk(2);--changed.trace.intervals[0].beginUtcMs;CHECK(ledger.apply(changed,now)==BillingStatus::Calculation);
        CHECK(ledger.snapshot(now).digest==firstDigest&&ledger.snapshot(T).status==BillingStatus::Clock);
        CHECK(ledger.apply(chunk(2),now)==BillingStatus::Ok&&display(*ledger.snapshot(now).settledTotal)=="6.000000");
        CHECK(ledger.apply(chunk(3),T+3*hour)==BillingStatus::Calculation);
        CHECK(ledger.verifyCommitted(chunk(),now)==BillingStatus::Replay&&ledger.verifyCommitted(chunk(2),now)==BillingStatus::Replay);
        CHECK(ledger.open(file,b,now)==BillingStatus::Invalid);}
    const auto original=bytes(file);
    {BillingLedger inspect;CHECK(inspect.open(file,b,now,BillingOpen::Inspect)==BillingStatus::Ok&&inspect.snapshot(now).sequence==2);
        CHECK(inspect.apply(chunk(2),now)==BillingStatus::Unsupported&&inspect.verifyCommitted(chunk(2),now)==BillingStatus::Replay);}
    CHECK(bytes(file)==original);
    {BillingLedger restart;CHECK(restart.open(file,b,now)==BillingStatus::Ok&&restart.apply(chunk(2),now)==BillingStatus::Replay&&display(*restart.snapshot(now).settledTotal)=="6.000000");}
    {BillingLedger exists;CHECK(exists.open(file,b,now,BillingOpen::CreateNew)==BillingStatus::Conflict);}
    for(unsigned variant=0;variant<8;++variant){auto changed=b;BillingLimits limits;
        if(variant==0)changed.plan.currency="EUR";
        if(variant==1)changed.plan.revisions[0].baseRatePerKwh=d("4");
        if(variant==2)changed.cycleKey="fixture.other-cycle";
        if(variant==3)changed.evidence.adapter="fixture.other-adapter";
        if(variant==4)changed.plan.subtotalMultiplier=d("1.1");
        if(variant==5)changed.plan.clock.fixedOffsetSeconds=3600,changed.plan.clock.kind=CivilClockKind::FixedStandardOffset;
        if(variant==6)limits.maxBatches=10;
        if(variant==7)changed.plan.supplier="fixture.other-supplier";
        BillingLedger mismatch;CHECK(mismatch.open(file,changed,now,BillingOpen::Inspect,limits)==BillingStatus::Attribution);}
    {BillingLedger past;CHECK(past.open(file,b,T+hour)==BillingStatus::Clock);}
    for(bool intervals:{false,true}){BillingLimits limits;if(intervals)limits.maxIntervals=1;else limits.maxBatches=1;
        const auto path=root/(intervals?"intervals.db":"batches.db");BillingLedger bounded;CHECK(bounded.open(path,b,now,BillingOpen::CreateNew,limits)==BillingStatus::Ok);
        CHECK(bounded.apply(chunk(),now)==BillingStatus::Ok&&bounded.apply(chunk(2),now)==BillingStatus::Capacity&&bounded.snapshot(now).sequence==1);}
    {auto composed=b;composed.plan.minimumCycleSubtotal=d("10");composed.plan.revisions[0].baseRatePerKwh=d("1");tier(composed.plan,TierMode::AllUnits);
        const auto path=root/"floor.db";
        {BillingLedger ledger;CHECK(ledger.open(path,composed,now,BillingOpen::CreateNew)==BillingStatus::Ok&&ledger.apply(chunk(),now)==BillingStatus::Ok);
            CHECK(ledger.snapshot(now).settlementDependent&&display(*ledger.snapshot(now).provisionalTotal)=="1.000000"&&!ledger.snapshot(now).settledTotal);}
        BillingLedger restarted;CHECK(restarted.open(path,composed,now)==BillingStatus::Ok&&restarted.apply(chunk(2),now)==BillingStatus::Ok);
        CHECK(!restarted.snapshot(now).settlementDependent&&display(*restarted.snapshot(now).settledTotal)=="10.000000");}
    {auto board=b;board.evidence.basis=PowerBasis::GpuBoard;auto a=chunk(),c=chunk(2);a.trace.evidence=board.evidence;c.trace.evidence=board.evidence;
        const auto path=root/"board.db";
        {BillingLedger ledger;CHECK(ledger.open(path,board,now,BillingOpen::CreateNew)==BillingStatus::Ok&&ledger.apply(a,now)==BillingStatus::Ok);}
        BillingLedger ledger;CHECK(ledger.open(path,board,now)==BillingStatus::Ok&&ledger.apply(c,now)==BillingStatus::Ok&&display(*ledger.snapshot(now).settledTotal)=="6.000000");}
    {auto whole=b;tier(whole.plan,TierMode::AllUnits);auto first=chunk();first.trace.intervals[0].watts=d("100000");
        BillingLedger ledger;CHECK(ledger.open(root/"counterfactual.db",whole,now,BillingOpen::CreateNew)==BillingStatus::Ok);
        const auto base=load("0",T+hour,T+2*hour),with=load("1000",T+hour,T+2*hour);
        CHECK(ledger.projectRemainder(base,with,now).status==ElectricityStatus::Unavailable);
        CHECK(ledger.apply(first,now)==BillingStatus::Ok);const auto original=ledger.snapshot(now);
        const auto delta=ledger.projectRemainder(base,with,T+hour);CHECK(delta.status==ElectricityStatus::Ok&&display(*delta.incrementalCharge)=="102.000000");
        CHECK(ledger.snapshot(now).digest==original.digest&&!ledger.snapshot(now).settledTotal);
        CHECK(ledger.projectRemainder(base,with,T).status==ElectricityStatus::ClockFault);
        auto incomplete=base;incomplete.intervals[0].endUtcMs--;CHECK(!ledger.projectRemainder(incomplete,with,now).incrementalCharge);
        CHECK(ledger.apply(chunk(2),now)==BillingStatus::Ok&&ledger.projectRemainder(base,with,now).status==ElectricityStatus::Unsupported);}
    {BillingLedger bounded;CHECK(bounded.open(root/"bounds.db",b,now,BillingOpen::CreateNew)==BillingStatus::Ok);
        auto bad=chunk();bad.trace.intervals.resize(513);CHECK(bounded.apply(bad,now)==BillingStatus::Invalid);
        bad=chunk();bad.id=std::string(129,'x');CHECK(bounded.apply(bad,now)==BillingStatus::Invalid&&bounded.snapshot(now).sequence==0);}
    {auto zero=b;zero.plan.revisions[0].baseRatePerKwh=d("0");auto a=chunk();a.trace.intervals[0].watts=d("0");BillingLedger ledger;
        CHECK(ledger.open(root/"zero.db",zero,now,BillingOpen::CreateNew)==BillingStatus::Ok&&ledger.apply(a,now)==BillingStatus::Ok&&ledger.snapshot(now).provisionalTotal->numerator.zero());}
    // A logical SQLite page cap rolls the entire original/derived-state/head transaction back; no rounding/drop fallback.
    {const auto path=root/"full.db";BillingLimits limits;limits.maxDatabaseBytes=1024*1024;BillingSnapshot last;bool full=false;
        {BillingLedger ledger;CHECK(ledger.open(path,b,now,BillingOpen::CreateNew,limits)==BillingStatus::Ok);
            for(unsigned seq=1;seq<=128&&!full;++seq){auto input=chunk();input.sequence=seq;input.id="fixture.capacity."+std::to_string(seq);input.trace.intervals.clear();
                for(unsigned i=0;i<512;++i){const int64_t begin=T+static_cast<int64_t>((seq-1)*512+i);input.trace.intervals.push_back({begin,begin+1,d("12345.6789012345678901234567890123456789")});}
                input.observedUtcMs=input.trace.intervals.back().endUtcMs;last=ledger.snapshot(now);const auto status=ledger.apply(input,now);
                if(status==BillingStatus::Capacity){full=true;CHECK(ledger.snapshot(now).status==BillingStatus::Storage);}else CHECK(status==BillingStatus::Ok);}
            CHECK(full&&last.sequence>0);}
        BillingLedger restored;CHECK(restored.open(path,b,now,BillingOpen::Inspect,limits)==BillingStatus::Ok&&restored.snapshot(now).sequence==last.sequence&&restored.snapshot(now).digest==last.digest);}
    if(argc==2){CHECK(configureCivilTimeData(std::filesystem::u8path(argv[1]),"2026e")==CivilStatus::Ok);
        const CivilClock zone{CivilClockKind::NamedIana,"America/New_York",0};
        for(bool spring:{true,false}){const auto civil=civilDay(zone,spring?CivilDate{2026,3,8}:CivilDate{2026,11,1});CHECK(civil.status==CivilStatus::Ok);if(civil.status!=CivilStatus::Ok)continue;
            auto named=b;named.plan=plan(civil.beginUtcMs,civil.endUtcMs);named.plan.clock=zone;named.plan.cycleCharge=d("2");named.plan.revisions[0].chargePerCivilDay=d("4");
            named.cycleBeginUtcMs=civil.beginUtcMs;named.cycleEndUtcMs=civil.endUtcMs;const auto path=root/(spring?"spring.db":"fall.db");
            auto a=chunk(),c=chunk(2);a.observedUtcMs=civil.beginUtcMs+2*hour;a.trace=load("1000",civil.beginUtcMs,a.observedUtcMs);
            c.observedUtcMs=civil.endUtcMs;c.trace=load("1000",a.observedUtcMs,civil.endUtcMs);BillingSnapshot before;
            {BillingLedger ledger;CHECK(ledger.open(path,named,civil.endUtcMs,BillingOpen::CreateNew)==BillingStatus::Ok&&ledger.apply(a,civil.endUtcMs)==BillingStatus::Ok);
                before=ledger.snapshot(civil.endUtcMs);CHECK(before.opening.timeDatabaseVersion=="2026e"&&!before.settledTotal&&display(*before.provisionalTotal)=="12.000000");}
            {BillingLedger ledger;CHECK(ledger.open(path,named,civil.endUtcMs)==BillingStatus::Ok&&ledger.snapshot(civil.endUtcMs).digest==before.digest&&ledger.apply(c,civil.endUtcMs)==BillingStatus::Ok);
                const auto settled=ledger.snapshot(civil.endUtcMs);CHECK(settled.settledTotal&&display(*settled.settledTotal)==(spring?"75.000000":"81.000000"));}
            BillingLedger inspect;CHECK(inspect.open(path,named,civil.endUtcMs,BillingOpen::Inspect)==BillingStatus::Ok&&inspect.verifyCommitted(a,civil.endUtcMs)==BillingStatus::Replay&&inspect.verifyCommitted(c,civil.endUtcMs)==BillingStatus::Replay);}}
    std::filesystem::remove_all(root);return finishTests("billing ledger");
}
