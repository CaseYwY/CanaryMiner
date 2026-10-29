#include "income_forecast_fixture.h"
#include "product/forecast_owner.h"
#include "product/credit_recovery.h"
#include "product_test_util.h"
#include <chrono>
#include <fstream>
#include <mutex>
#include <thread>
using namespace om;
using namespace income_fixture;
namespace {
template<class P>bool until(P p){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(std::chrono::steady_clock::now()<end){if(p())return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return p();}
std::filesystem::path suffix(std::filesystem::path p,const char *s){p+=s;return p;}
std::string bytes(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
struct Clock {
    std::mutex mutex;ForecastTime value;
    explicit Clock(int64_t utc):value{utc,10000}{}
    ForecastTime get(){std::lock_guard<std::mutex> lock(mutex);return value;}
    void advance(int64_t utc,int64_t mono){std::lock_guard<std::mutex> lock(mutex);value.utcMs+=utc;value.monotonicMs=static_cast<uint64_t>(static_cast<int64_t>(value.monotonicMs)+mono);}
};
ForecastOwnerPolicy policy(){ForecastOwnerPolicy p;p.enabled=true;p.projectionRefreshMs=100;p.helperTimeoutMs=3000;p.readBudgetMs=10000;return p;}
ForecastScenario scenario(const Fixture &f,const CreditSourceContract &c,unsigned periods=2){
    ForecastScenario s;s.clock=f.input.clock;s.reportUtcMs=f.now;s.modelValidUntilUtcMs=f.now+day;s.qualification=ForecastQualification::SyntheticFixture;
    s.modelKey=f.calibration.modelKey;s.profileKey=f.calibration.profileKey;s.earningAlignmentContract=f.calibration.earningAlignmentContract;
    for(unsigned i=0;i<periods;++i){const auto begin=f.begin-day+day*i/periods,end=f.begin-day+day*(i+1)/periods;s.calibration.push_back({begin,end,work(c,begin,end)});}
    s.remaining=f.input.remaining;s.remaining.work.binding=c;s.tokenQuote=f.input.tokenQuote;s.fxQuote=f.input.fxQuote;s.electricity=f.input.electricity;s.electricity.attributedProduction=c;return s;
}
CreditProcessRequest request(const std::filesystem::path &p,const CreditSourceContract &c,int64_t now){
    CreditProcessRequest r;r.generation=r.ticket=1;r.file=p;r.binding=c;r.nowUtcMs=now;r.recovery=CreditRecoveryMode::Inspect;r.queries={{true,0,now}};return r;
}
CreditProcessReply write(CreditProcessClient &client,const std::filesystem::path &path,const CreditSourceContract &c,const Fixture &f,unsigned sequence=1){
    auto r=request(path,c,f.now);r.recovery=CreditRecoveryMode::Submit;CreditBatch b;b.binding=c;b.sequence=sequence;b.snapshotId="synthetic-snapshot-"+std::to_string(sequence);
    b.sourceAsOfMs=b.observedAtMs=f.now;b.coverage={{c.historyStartMs-1,f.now}};
    b.cursor=CreditCursorStep{sequence==1?c.cursor->initial:"fixture:page-"+std::to_string(sequence-1),"fixture:page-"+std::to_string(sequence)};
    if(sequence==1)b.records={credit_fixture::record("cutoff",1,f.now-day,"100"),credit_fixture::record("prior-day",1,f.begin-1,"20"),credit_fixture::record("midnight",1,f.begin,"1"),credit_fixture::record("current",1,f.now,"2")};
    else b.records={credit_fixture::record("midnight",sequence,f.begin,"4")};
    r.batch=b;std::atomic<bool> cancel{false};const auto result=client.transact(r,cancel,5000);CHECK(result.reply&&result.reply->applied==CreditLedgerStatus::Ok);return result.reply?*result.reply:CreditProcessReply{};
}
std::string token(const std::shared_ptr<const ForecastOwnerSnapshot> &s){std::string out;if(s->result&&s->result->todayTokens)s->result->todayTokens->format(8,&out);return out;}
bool completed(IncomeForecastOwner &o){return until([&]{const auto s=o.snapshot();return !s->pending&&s->state!=ForecastOwnerState::Busy;});}
}
int main(int argc,char **argv){
    CHECK(argc==4||argc==5);if(argc!=4&&argc!=5)return finishTests("forecast owner");
    const auto writer=std::filesystem::absolute(argv[1]),reader=std::filesystem::absolute(argv[2]),fixture=std::filesystem::absolute(argv[3]);
    const auto root=std::filesystem::path(makeScratchDir("forecast-owner"));
    {
        Fixture f(root/"input-fixture.db");auto c=f.input.binding;c.cursor=CreditCursorContract{"fixture.exclusive-acyclic-v1","fixture:initial"};
        const auto s=scenario(f,c);Clock clock(f.now);CreditProcessClient import(writer);ForecastQueryProcessClient query(reader);std::atomic<bool> cancel{false};
        auto file=root/"read.db";write(import,file,c,f);auto r=request(file,c,f.now);std::string wire;uint64_t parent=0;CreditProcessRequest parsed;
        CHECK(encodeForecastQuery(r,localTaskProcessId(),&wire)&&decodeForecastQuery(wire,&parsed,&parent)&&parent==localTaskProcessId());
        CHECK(!decodeForecastQuery("CMFQ1\n0\n{}",&parsed,&parent)&&!decodeForecastQuery(wire+"x",&parsed,&parent));
        CHECK(!decodeForecastQuery(std::string(forecastQueryLimit+1,'x'),&parsed,&parent));
        for(unsigned bad=0;bad<4;++bad){auto invalid=r;if(bad==0)invalid.batch=credit_fixture::batch(1,f.now);if(bad==1)invalid.binding.cursor.reset();if(bad==2)invalid.recovery=CreditRecoveryMode::Retry;if(bad==3)invalid.queries.resize(5);
            CHECK(!validForecastQuery(invalid)&&query.transact(invalid,cancel,100).transport==LocalTaskStatus::Invalid);}
        const auto journal=suffix(file,".credit-recovery.json");const auto before=bytes(journal);
        auto result=query.transact(r,cancel,3000);CHECK(result.reply&&result.reply->opened==CreditLedgerStatus::Ok&&result.reply->cursor->sequence==1&&bytes(journal)==before&&!result.writeOutcomeUnknown);
        for(const auto &missing:{root/"absent"/"no.db",root/"orphan.db"}){if(missing.filename()=="orphan.db")std::ofstream(suffix(missing,".credit-recovery.json"))<<before;
            auto absent=r;absent.file=missing;result=query.transact(absent,cancel,3000);CHECK(result.reply&&result.reply->opened==CreditLedgerStatus::Storage&&!std::filesystem::exists(missing));}
        CHECK(!std::filesystem::exists(root/"absent"));
        {CreditLedger db;CHECK(db.open(root/"never-created"/"existing.db",c,f.now,{},CreditLedgerOpen::ExistingQuery)==CreditLedgerStatus::Storage&&!std::filesystem::exists(root/"never-created"));}
        {CreditLedger db;CHECK(db.open(file,c,f.now,{},CreditLedgerOpen::ExistingQuery)==CreditLedgerStatus::Ok);CHECK(db.apply({},f.now)==CreditLedgerStatus::Unsupported&&db.cursor(f.now).sequence==1);}
        {CreditRecoveryJournal recovery;CHECK(recovery.open(r,true)==CreditLedgerStatus::Ok);CHECK(recovery.prepare(r)==CreditLedgerStatus::Unsupported&&recovery.settle(CreditLedgerStatus::Ok)==CreditLedgerStatus::Unsupported&&bytes(journal)==before);}
        {IncomeForecastOwner disabled(reader,root/"disabled.db",c);CHECK(!disabled.start(1)&&!std::filesystem::exists(root/"disabled.db"));}
        {IncomeForecastOwner stopped(reader,root/"prestop.db",c,policy(),[&]{return clock.get();});stopped.requestStop();CHECK(!stopped.start(1));}
        {auto invalid=policy();invalid.ledger.maxRecords=0;IncomeForecastOwner owner(reader,file,c,invalid,[&]{return clock.get();});CHECK(!owner.start(1));}
        {auto missing=request(root/"no-journal.db",c,f.now);std::filesystem::copy_file(file,missing.file);
            const auto original=bytes(missing.file);result=query.transact(missing,cancel,3000);CHECK(result.reply&&result.reply->opened==CreditLedgerStatus::Storage&&bytes(missing.file)==original&&!std::filesystem::exists(suffix(missing.file,".credit-recovery.json")));}
        for(const auto &helper:{writer,root/"no-helper"}){Clock local(f.now);IncomeForecastOwner owner(helper,file,c,policy(),[&]{return local.get();});
            CHECK(owner.start(11)&&owner.submit(11,s)==ForecastAdmission::Queued&&completed(owner));CHECK(!owner.snapshot()->result&&owner.snapshot()->helperReads==1&&bytes(journal)==before);}
        // A logical read never settles a Prepared original, even when the committed cursor is readable.
        {const auto p=root/"prepared.db";write(import,p,c,f);auto pending=request(p,c,f.now);pending.recovery=CreditRecoveryMode::Submit;
            auto b=credit_fixture::page(2,f.now);b.binding=c;b.cursor=CreditCursorStep{"fixture:page-1","fixture:page-2"};pending.batch=b;
            {CreditRecoveryJournal recovery;CHECK(recovery.open(pending)==CreditLedgerStatus::Ok&&recovery.prepare(pending)==CreditLedgerStatus::Ok);}
            const auto original=bytes(suffix(p,".credit-recovery.json"));Clock local(f.now);IncomeForecastOwner owner(reader,p,c,policy(),[&]{return local.get();});
            CHECK(owner.start(12)&&owner.submit(12,s)==ForecastAdmission::Queued&&completed(owner));CHECK(!owner.snapshot()->result&&bytes(suffix(p,".credit-recovery.json"))==original);
            CreditLedger db;CHECK(db.open(p,c,f.now,{},CreditLedgerOpen::ExistingQuery)==CreditLedgerStatus::Ok&&db.cursor(f.now).sequence==1);}
        // One asynchronous read job, at most four queries per helper, all chunks bound to one committed source head.
        {IncomeForecastOwner owner(reader,file,c,policy(),[&]{return clock.get();});CHECK(owner.start(7));CHECK(owner.snapshot()->helperReads==0&&!owner.snapshot()->result);
            CHECK(owner.submit(6,s)==ForecastAdmission::StaleGeneration);CHECK(owner.submit(7,s)==ForecastAdmission::Queued);
            CHECK(completed(owner));auto snap=owner.snapshot();CHECK(snap->state==ForecastOwnerState::Ready&&token(snap)=="33.00000000"&&snap->helperReads==2&&snap->source->sequence==1);
            CHECK(snap->source->sequence!=snap->completedTicket*32);const auto source=*snap->source;
            auto quote=s.tokenQuote;quote->last=decimal("5");CHECK(owner.updateMarket(7,quote,s.fxQuote)==ForecastAdmission::Queued);CHECK(completed(owner));snap=owner.snapshot();
            std::string value;CHECK(snap->result&&snap->result->todayUsd&&snap->result->todayUsd->format(2,&value)&&value=="161.70"&&snap->helperReads==2);
            clock.advance(120001,120001);CHECK(until([&]{const auto a=owner.snapshot();return a->state==ForecastOwnerState::Ready&&a->result&&!a->result->todayTokens&&!a->result->todayUsd;}));
            CHECK(owner.snapshot()->source->sequence==source.sequence&&owner.snapshot()->helperReads==2);
            const auto start=std::chrono::steady_clock::now();owner.requestStop();CHECK(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(100));
            const auto stopped=owner.snapshot();owner.stop();CHECK(owner.snapshot()==stopped&&!stopped->pending&&!stopped->result&&stopped->state==ForecastOwnerState::Stopped);
            CHECK(owner.submit(7,s)==ForecastAdmission::Stopped&&owner.updateMarket(7,{},{})==ForecastAdmission::Stopped&&!owner.invalidate(7)&&!owner.start(8));}
        {Clock local(f.now);IncomeForecastOwner owner(reader,file,c,policy(),[&]{return local.get();});CHECK(owner.start(1)&&owner.submit(1,scenario(f,c,64))==ForecastAdmission::Queued&&completed(owner));
            CHECK(token(owner.snapshot())=="33.00000000"&&owner.snapshot()->helperReads==17);auto huge=s;huge.calibration.resize(65);CHECK(owner.submit(1,huge)==ForecastAdmission::Invalid);}
        // Concurrent quote admission must not reverse clock samples merely by delaying the background lock acquisition.
        {std::atomic<unsigned> calls{0},inside{0};std::atomic<bool> concurrent{false};
            const auto advancing=[&]{if(inside.fetch_add(1))concurrent=true;const auto tick=++calls;std::this_thread::yield();--inside;return ForecastTime{f.now+tick,10000+tick};};
            IncomeForecastOwner owner(reader,file,c,policy(),advancing);CHECK(owner.start(19)&&owner.submit(19,s)==ForecastAdmission::Queued&&completed(owner));
            const auto update=[&]{for(unsigned i=0;i<500;++i){owner.updateMarket(19,s.tokenQuote,s.fxQuote);std::this_thread::yield();}};
            std::thread first(update),second(update);first.join();second.join();CHECK(completed(owner));
            CHECK(!concurrent&&calls>50&&owner.snapshot()->state==ForecastOwnerState::Ready&&token(owner.snapshot())=="33.00000000");}
        for(bool market:{false,true}){Clock local(f.now);auto limits=policy();limits.suppressMarket=market;limits.suppressEnergyCosts=!market;
            IncomeForecastOwner owner(reader,file,c,limits,[&]{return local.get();});CHECK(owner.start(13)&&owner.submit(13,s)==ForecastAdmission::Queued&&completed(owner));
            const auto snapshot=owner.snapshot();CHECK(token(snapshot)=="33.00000000"&&!snapshot->result->netTodayUsd&&snapshot->result->electricityConfigured);
            if(market)CHECK(!snapshot->result->todayUsd&&owner.updateMarket(13,s.tokenQuote,s.fxQuote)==ForecastAdmission::NotReady);else CHECK(snapshot->result->todayUsd.has_value());}
        {Clock local(f.now);auto currencies=s;currencies.electricity.plan.currency="EUR";currencies.electricity.fx=ForecastCurrencyFx{ForecastQualification::SyntheticFixture,"EUR","fixture.fx",decimal("1.2"),f.now,120000};
            IncomeForecastOwner owner(reader,file,c,policy(),[&]{return local.get();});CHECK(owner.start(14)&&owner.submit(14,currencies)==ForecastAdmission::Queued&&completed(owner));
            CHECK(owner.updateMarket(14,s.tokenQuote,s.fxQuote)==ForecastAdmission::Queued&&completed(owner));std::string net;
            CHECK(owner.snapshot()->result->netTodayUsd->format(2,&net)&&net=="80.13");
            CHECK(owner.updateMarket(14,s.tokenQuote,s.fxQuote,{true,{}})==ForecastAdmission::Queued&&completed(owner));CHECK(!owner.snapshot()->result->netTodayUsd);}
        // Revisions invalidate cached calculations; a new explicit read recalibrates from the new committed snapshot.
        {Clock local(f.now);IncomeForecastOwner owner(reader,file,c,policy(),[&]{return local.get();});CHECK(owner.start(2)&&owner.submit(2,s)==ForecastAdmission::Queued&&completed(owner));
            write(import,file,c,f,2);CHECK(owner.invalidate(2)&&!owner.snapshot()->result&&owner.updateMarket(2,s.tokenQuote,s.fxQuote)==ForecastAdmission::NotReady);
            CHECK(owner.submit(2,s)==ForecastAdmission::Queued&&completed(owner));CHECK(token(owner.snapshot())=="36.00000000"&&owner.snapshot()->source->sequence==2);}
        for(unsigned fault=0;fault<3;++fault){Clock local(f.now);IncomeForecastOwner owner(reader,file,c,policy(),[&]{return local.get();});CHECK(owner.start(3));
            local.advance(fault==0?-1:fault==1?3001:0,fault==2?-1:0);
            CHECK(until([&]{return owner.snapshot()->state==ForecastOwnerState::ClockFault;}));CHECK(!owner.snapshot()->result&&owner.submit(3,s)==ForecastAdmission::Stopped&&!owner.start(4));}
        {Clock local(f.now);IncomeForecastOwner owner(reader,file,c,policy(),[&]{return local.get();});CHECK(owner.start(4)&&owner.submit(4,s)==ForecastAdmission::Queued&&completed(owner));
            local.advance(day,day);CHECK(until([&]{const auto a=owner.snapshot();return a->status==ForecastStatus::Stale&&(!a->result||!a->result->todayTokens);}));}
        // A competing committed page between chunks cannot produce a mixed-period total.
        {const auto p=root/"fixture-query-between.db";write(import,p,c,f);Clock local(f.now);IncomeForecastOwner owner(fixture,p,c,policy(),[&]{return local.get();});
            CHECK(owner.start(5)&&owner.submit(5,s)==ForecastAdmission::Queued);CHECK(until([&]{return std::filesystem::exists(suffix(p,".fixture-ready"));}));
            write(import,p,c,f,2);std::ofstream(suffix(p,".fixture-release"))<<"1";CHECK(completed(owner));CHECK(owner.snapshot()->status==ForecastStatus::Stale&&!owner.snapshot()->result);}
        {const auto p=root/"fixture-query-hang.db";write(import,p,c,f);Clock local(f.now);IncomeForecastOwner owner(fixture,p,c,policy(),[&]{return local.get();});
            CHECK(owner.start(6)&&owner.submit(6,s)==ForecastAdmission::Queued);CHECK(until([&]{return std::filesystem::exists(suffix(p,".fixture-ready"));}));
            CHECK(owner.submit(6,s)==ForecastAdmission::NotReady);const auto start=std::chrono::steady_clock::now();owner.requestStop();CHECK(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(100));
            owner.stop();CHECK(std::chrono::steady_clock::now()-start<std::chrono::seconds(2)&&!owner.snapshot()->result);}
        {const auto p=root/"rollback.db";write(import,p,c,f);const auto originalDb=bytes(p),originalJournal=bytes(suffix(p,".credit-recovery.json"));write(import,p,c,f,2);
            Clock local(f.now);IncomeForecastOwner owner(reader,p,c,policy(),[&]{return local.get();});CHECK(owner.start(15)&&owner.submit(15,s)==ForecastAdmission::Queued&&completed(owner));
            std::ofstream(p,std::ios::binary|std::ios::trunc)<<originalDb;std::ofstream(suffix(p,".credit-recovery.json"),std::ios::binary|std::ios::trunc)<<originalJournal;
            CHECK(owner.submit(15,s)==ForecastAdmission::Queued&&completed(owner));CHECK(owner.snapshot()->state==ForecastOwnerState::Faulted&&owner.snapshot()->status==ForecastStatus::Attribution&&!owner.snapshot()->result);}
        {const auto dir=root/"timeout";std::filesystem::create_directory(dir);const auto p=dir/"fixture-query-hang.db";write(import,p,c,f);Clock local(f.now);auto limits=policy();limits.readBudgetMs=500;
            IncomeForecastOwner owner(fixture,p,c,limits,[&]{return local.get();});CHECK(owner.start(16)&&owner.submit(16,s)==ForecastAdmission::Queued&&completed(owner));
            CHECK(owner.snapshot()->transport==LocalTaskStatus::Timeout&&!owner.snapshot()->result&&owner.snapshot()->helperReads==1);
            std::this_thread::sleep_for(std::chrono::milliseconds(150));CHECK(owner.snapshot()->helperReads==1&&owner.snapshot()->lastTicket==1);}
        {const auto dir=root/"invalidate-flight";std::filesystem::create_directory(dir);const auto p=dir/"fixture-query-hang.db";write(import,p,c,f);Clock local(f.now);
            IncomeForecastOwner owner(fixture,p,c,policy(),[&]{return local.get();});CHECK(owner.start(17)&&owner.submit(17,s)==ForecastAdmission::Queued);
            CHECK(until([&]{return std::filesystem::exists(suffix(p,".fixture-ready"));}));CHECK(owner.invalidate(17)&&completed(owner)&&!owner.snapshot()->result);
            std::ofstream(suffix(p,".fixture-release"))<<"1";CHECK(owner.submit(17,s)==ForecastAdmission::Queued&&completed(owner)&&token(owner.snapshot())=="33.00000000");}
        {Clock local(f.now);IncomeForecastOwner owner(reader,file,c,policy(),[&]{return local.get();});CHECK(owner.start(18));
            auto excessive=s;excessive.remaining.work.intervals.resize(4097);CHECK(owner.submit(18,excessive)==ForecastAdmission::Invalid&&owner.snapshot()->helperReads==0);
            excessive=s;excessive.electricity.plan.revisions[0].dynamic.resize(4097);CHECK(owner.submit(18,excessive)==ForecastAdmission::Invalid&&owner.snapshot()->helperReads==0);
            excessive=s;excessive.modelKey=std::string(129,'x');CHECK(owner.submit(18,excessive)==ForecastAdmission::Invalid&&owner.snapshot()->helperReads==0);
            excessive=s;excessive.marketPolicy.maxObservationAgeMs=UINT64_MAX;CHECK(owner.submit(18,excessive)==ForecastAdmission::Invalid&&owner.snapshot()->helperReads==0);
            excessive=s;excessive.marketPolicy.maxCrossFeedSkewMs=excessive.marketPolicy.maxObservationAgeMs+1;CHECK(owner.submit(18,excessive)==ForecastAdmission::Invalid&&owner.snapshot()->helperReads==0);
            excessive=s;excessive.electricity.configured=false;excessive.electricity.attributedProduction.scopeId=std::string(129,'x');CHECK(owner.submit(18,excessive)==ForecastAdmission::Invalid&&owner.snapshot()->helperReads==0);}
        CHECK(bytes(journal)!=before); // the explicit importer revision, never a forecast, changed the source journal
    }
    if(argc==5){
        CHECK(configureCivilTimeData(std::filesystem::u8path(argv[4]),"2026e")==CivilStatus::Ok);
        const CivilClock zone{CivilClockKind::NamedIana,"America/New_York",0};
        for(bool spring:{true,false}){
            const auto civil=civilDay(zone,spring?CivilDate{2026,3,8}:CivilDate{2026,11,1});CHECK(civil.status==CivilStatus::Ok);
            if(civil.status!=CivilStatus::Ok)continue;
            Fixture f(root/(spring?"spring-model.db":"fall-model.db"),zone,civil.beginUtcMs);
            auto c=f.input.binding;c.cursor=CreditCursorContract{"fixture.exclusive-acyclic-v1","fixture:initial"};
            CreditProcessClient importer(writer);const auto file=root/(spring?"spring-credit.db":"fall-credit.db");write(importer,file,c,f);
            Clock clock(f.now);IncomeForecastOwner owner(reader,file,c,policy(),[&]{return clock.get();});
            CHECK(owner.start(20)&&owner.submit(20,scenario(f,c))==ForecastAdmission::Queued&&completed(owner));
            const auto snapshot=owner.snapshot();CHECK(snapshot->result&&snapshot->result->windows.today.endUtcMs-snapshot->result->windows.today.beginUtcMs==(spring?23:25)*hour);
            std::string cost;CHECK(token(snapshot)=="33.00000000"&&snapshot->result->electricityUsd&&snapshot->result->electricityUsd->format(3,&cost)&&cost==(spring?"0.575":"0.625"));
            const auto toMidnight=civil.endUtcMs-f.now;clock.advance(toMidnight,toMidnight);
            CHECK(until([&]{const auto v=owner.snapshot();return v->status==ForecastStatus::Stale&&(!v->result||!v->result->todayTokens);}));
        }
    }
    std::filesystem::remove_all(root);return finishTests("forecast owner");
}
