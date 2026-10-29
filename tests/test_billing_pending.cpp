#include "billing_source_fixture.h"
#include "product/billing_recovery.h"
#include "product/billing_model.h"
#include "product/local_store_lease.h"
#include "product_test_util.h"
#include <fstream>
using namespace billing_source_fixture;
namespace {
using S=BillingStatus;using R=BillingRecoveryState;using O=BillingRecoveryOpen;
std::filesystem::path recoveryJournal(std::filesystem::path p){p+=".billing-recovery";return p;}
std::filesystem::path suffix(std::filesystem::path p,const char *s){p+=s;return p;}
std::string bytes(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
BillingChunk plain(unsigned seq=1){auto c=billing_fixture::chunk(seq);c.observedUtcMs=T+seq*1000;c.trace.intervals[0].beginUtcMs=T+(seq-1)*1000;c.trace.intervals[0].endUtcMs=T+seq*1000;return c;}
}
int main(){
    const auto root=std::filesystem::path(makeScratchDir("billing-pending"));auto b=billing_fixture::binding();b.cycleEndUtcMs=T+4000;b.plan=electricity_fixture::plan(T,T+4000);
    const auto file=root/"bill.db";const auto receipt=recoveryJournal(file);BillingRecoverySnapshot frozen;
    {BillingRecoveryStore store;CHECK(store.open(file,b,now,O::CreateNew)==S::Ok);const auto empty=store.inspect(now);
        CHECK(empty.status==S::Ok&&empty.state==R::Empty&&empty.committed.sequence==0&&!empty.committed.provisionalTotal&&!empty.original&&empty.revision.size()==64);
        CHECK(store.inspect(now-1).status==S::Clock&&store.submit(plain(),now-1)==S::Clock);
        CHECK(store.retry(plain(),empty.revision,now)==S::Conflict&&store.open(file,b,now)==S::Invalid);
        auto bad=plain();bad.sequence=3;CHECK(store.submit(bad,now)==S::SequenceGap&&bytes(receipt).find("CMBC")==std::string::npos);
        bad=plain();bad.trace.evidence.scope="fixture.other";CHECK(store.submit(bad,now)==S::Attribution);
        bad=plain();bad.observedUtcMs=now+1;CHECK(store.submit(bad,now)==S::Clock);
        bad=plain();bad.id="";CHECK(store.submit(bad,now)==S::Invalid);
        CHECK(store.submit(plain(),now)==S::Ok);frozen=store.inspect(now);
        CHECK(frozen.state==R::Confirmed&&frozen.committed.sequence==1&&frozen.original&&frozen.applied==S::Ok&&frozen.priorSequence==0&&frozen.committed.provisionalTotal->numerator.text()=="3000000");
        const auto saved=bytes(receipt);CHECK(store.submit(plain(),now+1000)==S::Replay&&bytes(receipt)==saved);
        CHECK(store.retry(plain(),empty.revision,now)==S::Conflict&&store.retry(plain(),frozen.revision,now)==S::Replay&&bytes(receipt)==saved);
        {BillingRecoveryStore busy;CHECK(busy.open(file,b,now,O::QueryOnly)==S::Storage);}
        {BillingLedger busy;CHECK(busy.open(file,b,now,BillingOpen::Inspect)==S::Storage);}
        // A deterministically rejected NEW input retains its original and permits a later corrected submission.
        bad=plain(2);bad.trace.intervals[0].beginUtcMs++;CHECK(store.submit(bad,now)==S::Calculation);
        const auto rejected=store.inspect(now);CHECK(rejected.state==R::Rejected&&rejected.applied==S::Calculation&&rejected.committed.sequence==1&&rejected.original->trace.intervals[0].beginUtcMs==T+1001);
        CHECK(store.retry(bad,rejected.revision,now)==S::Conflict);
        CHECK(store.submit(plain(2),now)==S::Ok&&store.inspect(now).committed.sequence==2);
        CHECK(frozen.committed.sequence==1&&frozen.original->sequence==1);}
    const auto original=bytes(receipt),database=bytes(file);
    {BillingRecoveryStore query;CHECK(query.open(file,b,now,O::QueryOnly)==S::Ok);const auto s=query.inspect(now);
        CHECK(s.state==R::Confirmed&&s.committed.sequence==2&&query.submit(plain(3),now)==S::Unsupported&&query.retry(*s.original,s.revision,now)==S::Unsupported);}
    CHECK(bytes(receipt)==original&&bytes(file)==database);
    for(unsigned k=0;k<4;++k){auto wrong=b;BillingLimits limits;if(k==0)wrong.cycleKey="fixture.other";if(k==1)wrong.plan.revisions[0].baseRatePerKwh=d("0.7");if(k==2)wrong.evidence.adapter="fixture.other";if(k==3)limits.maxBatches=128;
        BillingRecoveryStore query;CHECK(query.open(file,wrong,now,O::QueryOnly,limits)==S::Attribution&&bytes(receipt)==original&&bytes(file)==database);}
    for(O mode:{O::Existing,O::QueryOnly}){BillingRecoveryStore missing;CHECK(missing.open(root/"absent"/"bill.db",b,now,mode)==S::Storage&&!std::filesystem::exists(root/"absent"));}
    {BillingRecoveryStore exists;CHECK(exists.open(file,b,now,O::CreateNew)==S::Conflict&&bytes(receipt)==original&&bytes(file)==database);}
    {auto changed=b;changed.evidence.origin=PowerOrigin::ObservedTelemetry;BillingRecoveryStore real;CHECK(real.open(root/"real.db",changed,now,O::CreateNew)==S::Unsupported&&!std::filesystem::exists(root/"real.db"));}
    {BillingRecoveryStore relative;CHECK(relative.open("fixture.db",b,now,O::CreateNew)==S::Invalid);}
    for(const auto *tail:{"-wal","-shm","-journal"}){const auto orphan=root/(std::string("orphan")+tail+".db");{std::ofstream out(suffix(orphan,tail));out<<"fixture preserved";}
        BillingRecoveryStore refuse;CHECK(refuse.open(orphan,b,now,O::CreateNew)==S::Storage&&!std::filesystem::exists(orphan)&&!std::filesystem::exists(recoveryJournal(orphan))&&bytes(suffix(orphan,tail))=="fixture preserved");}
    {const auto standalone=root/"standalone.db";{BillingLedger ledger;CHECK(ledger.open(standalone,b,now,BillingOpen::CreateNew)==S::Ok);}
        const auto saved=bytes(standalone);BillingRecoveryStore refused;CHECK(refused.open(standalone,b,now)==S::Storage&&!std::filesystem::exists(recoveryJournal(standalone))&&bytes(standalone)==saved);}
    {LocalStoreLease lease;CHECK(lease.acquire(suffix(receipt,".lock")));BillingRecoveryStore denied;CHECK(denied.open(file,b,now)==S::Storage&&bytes(receipt)==original);}
    {LocalStoreLease lease;CHECK(lease.acquire(suffix(file,".lock")));BillingRecoveryStore denied;CHECK(denied.open(file,b,now)==S::Storage&&bytes(receipt)==original);}
    // A caller bypassing the journal is detectable and cannot silently bless the new monetary prefix.
    {BillingLedger bypass;CHECK(bypass.open(file,b,now)==S::Ok&&bypass.apply(plain(3),now)==S::Ok);}
    {BillingRecoveryStore store;CHECK(store.open(file,b,now)==S::Ok);const auto s=store.inspect(now);
        CHECK(s.state==R::Diverged&&s.committed.sequence==3&&s.original->sequence==2&&store.submit(plain(4),now)==S::Conflict&&store.retry(*s.original,s.revision,now)==S::Conflict&&bytes(receipt)==original);}
    // A source-bound receipt is frozen and remains readable after its source disappears.
    {const auto source=root/"source.db",bill=root/"source-bill.db";auto bound=binding(source);CHECK(executeEnergyHistoryRequest(input(bound,1)).applied==EnergyHistoryStatus::Ok);
        const auto q=query(bound,{0,sha256Hex(energy_history_detail::bindingText(bound.source->binding))});const auto reply=executeEnergyTraceRequest(q);BillingChunk c;
        CHECK(makeBillingSourceChunk(1,q,reply,&c)==S::Ok);
        {BillingRecoveryStore store;CHECK(store.open(bill,bound,now,O::CreateNew)==S::Ok&&store.submit(c,now)==S::Ok);}
        std::filesystem::remove(source);std::filesystem::remove(billing_source_fixture::journal(source));
        BillingRecoveryStore reopened;CHECK(reopened.open(bill,bound,now,O::QueryOnly)==S::Ok);const auto s=reopened.inspect(now);
        CHECK(s.state==R::Confirmed&&s.committed.source->checkpoint.sequence==1&&billing_detail::chunkText(*s.original)==billing_detail::chunkText(c)&&!std::filesystem::exists(source));}
    // Logical batch capacity is deterministic and preserved separately from an unknown disk failure.
    {BillingLimits cap;cap.maxBatches=1;BillingRecoveryStore store;CHECK(store.open(root/"capacity.db",b,now,O::CreateNew,cap)==S::Ok&&store.submit(plain(),now)==S::Ok);
        CHECK(store.submit(plain(2),now)==S::Capacity&&store.inspect(now).state==R::Rejected&&store.inspect(now).committed.sequence==1);}
    // Independent frame bounds allow a near-1-MiB binding plus a full chunk without widening the shared JSON reader.
    {auto large=b;large.cycleEndUtcMs=T+64000;large.plan.revisions.clear();
        for(unsigned i=0;i<64;++i){auto revision=b.plan.revisions[0];revision.key="fixture.revision."+std::to_string(i);revision.beginUtcMs=T+i*1000;revision.endUtcMs=T+(i+1)*1000;revision.timeMode=TimeRateMode::Calendar;
            for(unsigned j=0;j<64;++j){CalendarRate c;c.key="fixture.calendar."+std::to_string(j);c.beginMinute=0;c.endMinute=1440;c.weekdayMask=127;c.monthMask=4095;c.priority=j;c.publishedUtcMs=T;c.ratePerKwh=d("0.3");revision.calendar.push_back(c);}
            large.plan.revisions.push_back(revision);}
        auto over=large;size_t size=0;
        for(unsigned width=80;width<110;++width){for(auto &revision:over.plan.revisions)for(size_t j=0;j<revision.calendar.size();++j)revision.calendar[j].key="fixture.calendar."+std::to_string(j)+std::string(width,'"');
            const auto n=billing_detail::bindingText(over,{}).size();if(n>billing_detail::bindingLimit)break;large=over;size=n;}
        CHECK(size>billing_detail::bindingLimit-10*1024&&size<=billing_detail::bindingLimit&&billing_detail::bindingText(over,{}).size()>billing_detail::bindingLimit);
        const auto tooLarge=root/"over-budget.db";BillingRecoveryStore denied;CHECK(denied.open(tooLarge,over,now,O::CreateNew)==S::Capacity&&!std::filesystem::exists(tooLarge));
        auto c=plain();c.trace.intervals.clear();for(unsigned i=0;i<512;++i)c.trace.intervals.push_back({T+i,T+i+1,d("1."+std::string(47,'0')+(i%2?"1":"2"))});
        const auto path=root/std::filesystem::u8path("fixture-\xe6\xb5\x8b\xe8\xaf\x95.db");
        {BillingRecoveryStore store;CHECK(store.open(path,large,now,O::CreateNew)==S::Ok);
            // 48 fractional watt digits times the fractional rate needs a 49th digit: preserve a deterministic rejection.
            CHECK(store.submit(c,now)==S::Capacity&&store.inspect(now).state==R::Rejected&&store.inspect(now).committed.sequence==0);
            for(size_t i=0;i<c.trace.intervals.size();++i)c.trace.intervals[i].watts=d("1."+std::string(46,'0')+(i%2?"1":"2"));
            CHECK(store.submit(c,now)==S::Ok);}
        const auto saved=bytes(recoveryJournal(path));CHECK(saved.size()>1024*1024&&saved.size()<1024*1024+128*1024);
        {BillingRecoveryStore query;CHECK(query.open(path,large,now,O::QueryOnly)==S::Ok);const auto s=query.inspect(now);
            CHECK(s.state==R::Confirmed&&s.original->trace.intervals.size()==512&&billing_detail::chunkText(*s.original)==billing_detail::chunkText(c)&&bytes(recoveryJournal(path))==saved);}
        {BillingRecoveryStore query;CHECK(query.openQuery(path,sha256Hex(billing_detail::bindingText(large,{})),now)==S::Ok);const auto s=query.inspect(now);
            CHECK(s.state==R::Confirmed&&query.submit(c,now)==S::Unsupported&&query.retry(*s.original,s.revision,now)==S::Unsupported&&bytes(recoveryJournal(path))==saved);}
        {BillingRecoveryStore retry;CHECK(retry.retryReviewed(path,sha256Hex(billing_detail::bindingText(large,{})),saved.substr(saved.size()-65,64),sha256Hex(billing_detail::chunkText(c)),now)==S::Replay&&bytes(recoveryJournal(path))==saved);}
        auto excessive=c;excessive.sequence=2;excessive.trace.intervals.push_back({T+512,T+513,d("1")});
        BillingRecoveryStore store;CHECK(store.open(path,large,now)==S::Ok&&store.submit(excessive,now)==S::Invalid&&bytes(recoveryJournal(path))==saved);}
    {const auto path=root/"page-cap.db";auto bound=billing_fixture::binding();const auto at=T+2*electricity_fixture::hour;BillingLimits cap;cap.maxDatabaseBytes=1024*1024;
        BillingSnapshot last;BillingChunk pending;bool full=false;
        {BillingRecoveryStore store;CHECK(store.open(path,bound,at,O::CreateNew,cap)==S::Ok);
            for(unsigned seq=1;seq<=128&&!full;++seq){auto c=billing_fixture::chunk();c.sequence=seq;c.id="fixture.capacity."+std::to_string(seq);c.trace.intervals.clear();
                for(unsigned i=0;i<512;++i){const int64_t start=T+static_cast<int64_t>((seq-1)*512+i);c.trace.intervals.push_back({start,start+1,d("12345.6789012345678901234567890123456789")});}
                c.observedUtcMs=c.trace.intervals.back().endUtcMs;last=store.inspect(at).committed;const auto applied=store.submit(c,at);
                if(applied==S::Capacity){full=true;pending=c;CHECK(store.inspect(at).status==S::Storage);}else CHECK(applied==S::Ok);}
            CHECK(full&&last.sequence>0);}
        const auto saved=bytes(recoveryJournal(path));
        {BillingRecoveryStore store;CHECK(store.open(path,bound,at,O::QueryOnly,cap)==S::Ok);const auto s=store.inspect(at);
            CHECK(s.state==R::Prepared&&s.committed.digest==last.digest&&s.committed.sequence==last.sequence&&billing_detail::chunkText(*s.original)==billing_detail::chunkText(pending));}
        {BillingRecoveryStore store;CHECK(store.open(path,bound,at,O::Existing,cap)==S::Ok);const auto s=store.inspect(at);
            CHECK(store.retry(*s.original,s.revision,at)==S::Capacity&&bytes(recoveryJournal(path))==saved);}
        {BillingRecoveryStore store;CHECK(store.open(path,bound,at,O::QueryOnly,cap)==S::Ok&&store.inspect(at).state==R::Prepared&&store.inspect(at).committed.digest==last.digest&&bytes(recoveryJournal(path))==saved);}}
    std::filesystem::remove_all(root);return finishTests("billing pending");
}
