#include "forecast_owner.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
namespace om {
namespace {
constexpr int64_t lastUtc=253402300799999LL;
constexpr size_t intervalLimit=4096,periodLimit=64;
ForecastTime systemTime(){return {std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(),
    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count())};}
bool instant(int64_t t){return t>0&&t<=lastUtc;}
bool boundedKey(const std::string &s){return s.size()<=128;}
bool boundedBinding(const CreditSourceContract &c){return boundedKey(c.provider)&&boundedKey(c.contractVersion)&&boundedKey(c.scopeId)&&boundedKey(c.scopeGeneration)&&
    (!c.cursor||(boundedKey(c.cursor->version)&&c.cursor->initial.size()<=512));}
bool boundedEvidence(const PowerEvidence &e){return boundedKey(e.scope)&&boundedKey(e.adapter);}
bool sameSource(const CreditCursorCheckpoint &a,const CreditCursorCheckpoint &b){return a.sequence==b.sequence&&a.snapshotId==b.snapshotId&&a.next==b.next&&a.sourceAsOfMs==b.sourceAsOfMs&&a.observedAtMs==b.observedAtMs;}
bool boundedFx(const std::optional<ForecastCurrencyFx> &fx){return !fx||(boundedKey(fx->source)&&fx->baseCurrency.size()<=3);}
bool boundedWork(const ForecastWork &w,const CreditSourceContract &binding){return w.intervals.size()<=intervalLimit&&boundedKey(w.profileKey)&&validCreditSourceContract(w.binding)&&sameCreditSourceContract(w.binding,binding);}
bool boundedScenario(const ForecastScenario &s,const CreditSourceContract &binding){
    if(!s.marketPolicy.maxObservationAgeMs||s.marketPolicy.maxObservationAgeMs>86400000||s.marketPolicy.maxCrossFeedSkewMs>s.marketPolicy.maxObservationAgeMs)return false;
    if(!instant(s.reportUtcMs)||s.clock.zone.size()>128||s.calibration.size()>periodLimit||!boundedKey(s.modelKey)||!boundedKey(s.profileKey)||!boundedKey(s.earningAlignmentContract)||
       !boundedKey(s.remaining.assumptionKey)||!boundedWork(s.remaining.work,binding)||!boundedFx(s.electricity.fx))return false;
    size_t intervals=0;
    for(const auto &p:s.calibration){
        if(!instant(p.beginUtcMs)||!instant(p.endUtcMs)||p.endUtcMs<=p.beginUtcMs||p.endUtcMs-1>s.reportUtcMs||!boundedWork(p.work,binding))return false;
        intervals+=p.work.intervals.size();if(intervals>intervalLimit)return false;
    }
    const auto &e=s.electricity;const auto &p=e.plan;
    if(e.fullDayPower.intervals.size()>intervalLimit||!boundedEvidence(e.fullDayPower.evidence)||!boundedEvidence(e.opening.evidence)||
       !boundedKey(e.opening.planKey)||!boundedKey(e.opening.timeDatabaseVersion)||!boundedKey(p.key)||!boundedKey(p.territory)||!boundedKey(p.supplier)||!boundedKey(p.product)||p.currency.size()>3||p.clock.zone.size()>128||p.revisions.size()>128)return false;
    if(!boundedBinding(e.attributedProduction)||(e.configured&&(!validCreditSourceContract(e.attributedProduction)||!sameCreditSourceContract(e.attributedProduction,binding))))return false;
    // The in-memory job bound is independent of the tariff core's larger standalone dynamic-table capacity.
    size_t rows=0;for(const auto &r:p.revisions){
        if(!boundedKey(r.key)||r.calendar.size()>64||r.bands.size()>64||r.dynamic.size()>intervalLimit)return false;
        rows+=r.calendar.size()+r.bands.size()+r.dynamic.size();if(rows>intervalLimit)return false;
        for(const auto &c:r.calendar)if(!boundedKey(c.key))return false;
    }
    return true;
}
bool reviewable(const CreditProcessReply &r){
    return r.opened==CreditLedgerStatus::Ok&&!r.applied&&r.cursor&&r.cursor->status==CreditLedgerStatus::Ok&&r.cursor->sequence&&r.recovery&&r.recovery->status==CreditLedgerStatus::Ok&&
        (r.recovery->state==CreditRecoveryState::Confirmed||r.recovery->state==CreditRecoveryState::Rejected);
}
}
struct IncomeForecastOwner::Impl {
    struct Job {
        uint64_t ticket=0,generation=0,epoch=0;
        std::atomic<bool> cancel{false};
        std::optional<ForecastScenario> scenario;
        std::shared_ptr<const IncomeForecastInput> cached;
        std::optional<MarketQuote> token,fx;
        std::optional<ForecastCurrencyFx> tariffFx;
    };
    ForecastQueryProcessClient client;
    std::filesystem::path helper,file;
    CreditSourceContract binding;
    ForecastOwnerPolicy policy;
    ForecastOwnerClock clock;
    mutable std::mutex mutex;
    std::mutex lifecycle;
    std::condition_variable wake;
    std::thread thread;
    bool attempted=false,stopped=true;
    uint64_t epoch=0,lastProjectionMono=0;
    ForecastTime base,last;
    std::shared_ptr<Job> queued,flight;
    std::shared_ptr<const IncomeForecastInput> cached;
    std::optional<CreditCursorCheckpoint> proven;
    ForecastOwnerSnapshot current;
    std::shared_ptr<const ForecastOwnerSnapshot> published=std::make_shared<const ForecastOwnerSnapshot>();
    const std::shared_ptr<const ForecastOwnerSnapshot> emptyTerminal=published;
    uint64_t publishedEpoch=0;
    Impl(std::filesystem::path exe,std::filesystem::path path,CreditSourceContract source,ForecastOwnerPolicy options,ForecastOwnerClock c)
        :client(exe),helper(std::move(exe)),file(std::move(path)),binding(std::move(source)),policy(options),clock(c?std::move(c):ForecastOwnerClock(systemTime)){}
    FinancialView projection(const IncomeForecast &value)const{
        auto view=incomeForecastDashboardView(value);view.electricityConfigured=value.electricityConfigured;
        if(policy.suppressMarket)view.exactLines.push_back("Session override: --no-market; quote valuation disabled.");
        if(policy.suppressEnergyCosts)view.exactLines.push_back("Session override: --no-energy; energy-cost input disabled.");
        return view;
    }
    void publish(){current.pending=!stopped&&static_cast<bool>(queued||flight);++current.revision;published=std::make_shared<const ForecastOwnerSnapshot>(current);publishedEpoch=epoch;}
    void unavailable(ForecastStatus status){current.status=status;current.result.reset();IncomeForecast empty;empty.status=status;
        empty.electricityConfigured=current.view.electricityConfigured;current.view=projection(empty);}
    void cancelJobs(){if(queued)queued->cancel=true;if(flight)flight->cancel=true;queued.reset();cached.reset();}
    bool time(ForecastTime now){
        if(!instant(now.utcMs)||now.utcMs<last.utcMs||now.monotonicMs<last.monotonicMs||now.monotonicMs-base.monotonicMs>static_cast<uint64_t>(lastUtc)){
            stopped=true;cancelJobs();current.state=ForecastOwnerState::ClockFault;unavailable(ForecastStatus::Clock);publish();wake.notify_all();return false;}
        const auto elapsed=static_cast<int64_t>(now.monotonicMs-base.monotonicMs),wall=now.utcMs-base.utcMs;
        if(elapsed>wall+2000||wall>elapsed+2000){stopped=true;cancelJobs();current.state=ForecastOwnerState::ClockFault;unavailable(ForecastStatus::Clock);publish();wake.notify_all();return false;}
        last=now;return true;
    }
    bool expired(ForecastTime now)const{
        if(!cached||!current.result)return false;
        const auto &in=*cached;const auto &v=*current.result;
        if(now.utcMs>=v.windows.today.endUtcMs)return true;
        if(v.expectedRemaining&&in.reward&&(now.utcMs>in.reward->input().validUntilUtcMs||static_cast<uint64_t>(now.utcMs-in.remaining.declaredUtcMs)>in.remaining.maxAgeMs))return true;
        const auto quoteOld=[&](const std::optional<MarketQuote> &q){return q&&(now.monotonicMs<q->observation.receivedMonoMs||now.monotonicMs-q->observation.receivedMonoMs>in.marketPolicy.maxObservationAgeMs||
            static_cast<uint64_t>(now.utcMs/1000)<q->observation.observedUtc||static_cast<uint64_t>(now.utcMs/1000)-q->observation.observedUtc>in.marketPolicy.maxObservationAgeMs/1000);};
        if((v.latest.tokenStatus==MarketFreshness::Available&&quoteOld(in.tokenQuote))||(v.latest.usdStatus==MarketFreshness::Available&&quoteOld(in.fxQuote)))return true;
        return v.electricityUsd&&in.electricity.fx&&static_cast<uint64_t>(now.utcMs-in.electricity.fx->observedUtcMs)>in.electricity.fx->maxAgeMs;
    }
    ForecastAdmission enqueue(std::shared_ptr<Job> job){
        if(stopped)return ForecastAdmission::Stopped;
        if(queued||flight)return ForecastAdmission::NotReady;
        if(current.lastTicket>=UINT64_MAX/32-1)return ForecastAdmission::Invalid;
        job->ticket=current.lastTicket+1;job->generation=current.generation;job->epoch=epoch;
        auto next=current;next.lastTicket=job->ticket;next.state=ForecastOwnerState::Busy;next.pending=true;next.result.reset();
        IncomeForecast empty;empty.status=ForecastStatus::Unavailable;empty.electricityConfigured=job->scenario?job->scenario->electricity.configured:current.view.electricityConfigured;
        next.view=projection(empty);++next.revision;
        auto view=std::make_shared<const ForecastOwnerSnapshot>(next);
        queued=std::move(job);current=std::move(next);published=std::move(view);publishedEpoch=epoch;wake.notify_one();return ForecastAdmission::Queued;
    }
    struct ReadResult {ForecastStatus status=ForecastStatus::Unavailable;LocalTaskStatus transport=LocalTaskStatus::Unavailable;uint64_t reads=0;std::optional<CreditCursorCheckpoint> head;std::shared_ptr<IncomeForecastInput> input;};
    ReadResult read(const std::shared_ptr<Job> &job){
        ReadResult result;const auto &s=*job->scenario;const auto windows=incomeForecastWindows(s.clock,binding,s.reportUtcMs);
        if(windows.status!=ForecastStatus::Ok){result.status=windows.status;return result;}
        std::vector<CreditQuerySpec> queries={windows.todayCredits,windows.rolling24h,windows.cumulative};
        for(const auto &p:s.calibration)queries.push_back({false,p.beginUtcMs-1,p.endUtcMs-1});
        std::vector<CreditTotals> totals;const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(policy.readBudgetMs);
        for(size_t begin=0;begin<queries.size();begin+=4){
            if(job->cancel)return result;
            ForecastTime now;{std::lock_guard<std::mutex> lock(mutex);if(stopped||job->epoch!=epoch)return result;now=clock();if(!time(now))return result;}
            if(s.reportUtcMs>now.utcMs){result.status=ForecastStatus::Clock;return result;}
            const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
            if(remaining<50){result.transport=LocalTaskStatus::Timeout;return result;}
            CreditProcessRequest r;r.generation=job->generation;r.ticket=job->ticket*32+begin/4;r.file=file;r.binding=binding;r.limits=policy.ledger;r.nowUtcMs=now.utcMs;r.recovery=CreditRecoveryMode::Inspect;
            r.queries.assign(queries.begin()+static_cast<std::ptrdiff_t>(begin),queries.begin()+static_cast<std::ptrdiff_t>((std::min)(queries.size(),begin+4)));
            const auto reply=client.transact(r,job->cancel,(std::min)(policy.helperTimeoutMs,static_cast<unsigned>(remaining)));++result.reads;result.transport=reply.transport;
            if(reply.transport!=LocalTaskStatus::Ok||!reply.reply||!reviewable(*reply.reply)){
                if(reply.reply&&reply.reply->opened==CreditLedgerStatus::Clock)result.status=ForecastStatus::Clock;
                return result;
            }
            const auto &head=*reply.reply->cursor;
            if(result.head&&!sameSource(*result.head,head)){result.status=ForecastStatus::Stale;return result;}
            result.head=head;
            totals.insert(totals.end(),reply.reply->totals.begin(),reply.reply->totals.end());
        }
        if(job->cancel)return result;
        auto in=std::make_shared<IncomeForecastInput>();in->binding=binding;in->clock=s.clock;in->nowUtcMs=s.reportUtcMs;
        const auto credit=[&](size_t i){return ForecastCredits{binding,totals.at(i),result.head->sequence,result.head->snapshotId};};
        in->today=credit(0);in->rolling24h=credit(1);in->cumulative=credit(2);in->remaining=s.remaining;in->tokenQuote=s.tokenQuote;in->fxQuote=s.fxQuote;in->marketPolicy=s.marketPolicy;in->electricity=s.electricity;
        if(policy.suppressMarket){in->tokenQuote.reset();in->fxQuote.reset();in->electricity.fx.reset();}
        if(policy.suppressEnergyCosts)in->electricity.fullDayPower.intervals.clear();
        if(!s.calibration.empty()){
            RewardCalibrationInput c;c.qualification=s.qualification;c.binding=binding;c.modelKey=s.modelKey;c.profileKey=s.profileKey;c.earningAlignmentContract=s.earningAlignmentContract;c.calibratedUtcMs=s.reportUtcMs;c.validUntilUtcMs=s.modelValidUntilUtcMs;
            for(size_t i=0;i<s.calibration.size();++i){const auto &p=s.calibration[i];c.periods.push_back({p.beginUtcMs,p.endUtcMs,credit(i+3),p.work});}
            in->reward=calibrateReward(c); // actual facts remain usable if a complete reward model cannot be formed
        }
        result.input=std::move(in);result.status=ForecastStatus::Ok;return result;
    }
    void run()noexcept{
        try{
            for(;;){std::shared_ptr<Job> job;
                {std::unique_lock<std::mutex> lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(25),[&]{return stopped||queued;});if(stopped)return;
                    const auto now=clock();if(!time(now))return;
                    if(!queued&&cached&&(expired(now)||now.monotonicMs-lastProjectionMono>=policy.projectionRefreshMs)){
                        auto next=std::make_shared<Job>();next->cached=cached;next->token=cached->tokenQuote;next->fx=cached->fxQuote;next->tariffFx=cached->electricity.fx;
                        if(enqueue(next)!=ForecastAdmission::Queued)continue;
                    }
                    if(!queued)continue;
                    flight=queued;queued.reset();job=flight;
                }
                ReadResult result;
                if(job->scenario)result=read(job);
                else{result.status=ForecastStatus::Ok;result.transport=LocalTaskStatus::Ok;result.input=std::make_shared<IncomeForecastInput>(*job->cached);
                    result.input->tokenQuote=job->token;result.input->fxQuote=job->fx;result.input->electricity.fx=job->tariffFx;}
                ForecastTime valuation;
                {std::lock_guard<std::mutex> lock(mutex);if(stopped)return;valuation=clock();if(!time(valuation))return;}
                std::shared_ptr<const IncomeForecast> computed;
                FinancialView formatted;
                if(result.input&&!job->cancel){result.input->valuationUtcMs=valuation.utcMs;result.input->nowMonoMs=valuation.monotonicMs;computed=std::make_shared<const IncomeForecast>(incomeForecast(*result.input));formatted=projection(*computed);}
                std::lock_guard<std::mutex> lock(mutex);flight.reset();if(stopped)return;
                if(job->cancel||job->epoch!=epoch){current.state=ForecastOwnerState::Waiting;unavailable(ForecastStatus::Stale);publish();continue;}
                if(!time(clock()))return;
                current.helperReads+=result.reads;current.transport=result.transport;current.completedTicket=job->ticket;
                if(result.transport==LocalTaskStatus::Unreaped){stopped=true;cached.reset();current.state=ForecastOwnerState::Faulted;unavailable(ForecastStatus::Unavailable);publish();return;}
                if(result.head&&proven&&(result.head->sequence<proven->sequence||(result.head->sequence==proven->sequence&&!sameSource(*result.head,*proven)))){
                    stopped=true;cached.reset();current.state=ForecastOwnerState::Faulted;unavailable(ForecastStatus::Attribution);publish();return;}
                if(computed){
                    if(result.head){proven=result.head;current.source=result.head;}
                    cached=std::move(result.input);lastProjectionMono=valuation.monotonicMs;current.result=std::move(computed);current.status=current.result->status;
                    current.view=std::move(formatted);current.state=ForecastOwnerState::Ready;
                    if(current.result->status!=ForecastStatus::Ok)cached.reset(); // invalid context/day rollover needs a newly read report
                    else if(expired(last)){unavailable(ForecastStatus::Stale);current.state=ForecastOwnerState::Waiting;lastProjectionMono=0;} // never publish a value that expired during calculation
                }else{cached.reset();current.state=ForecastOwnerState::Waiting;unavailable(result.status);}
                publish();
            }
        }catch(...){try{std::lock_guard<std::mutex> lock(mutex);if(stopped)return;stopped=true;cancelJobs();flight.reset();current.state=ForecastOwnerState::Faulted;unavailable(ForecastStatus::Unavailable);publish();}catch(...){} }
    }
};
IncomeForecastOwner::IncomeForecastOwner(std::filesystem::path helper,std::filesystem::path file,CreditSourceContract binding,ForecastOwnerPolicy policy,ForecastOwnerClock clock)
    :impl_(new Impl(std::move(helper),std::move(file),std::move(binding),policy,std::move(clock))){}
IncomeForecastOwner::~IncomeForecastOwner(){stop();}
bool IncomeForecastOwner::start(uint64_t generation){
    auto &s=*impl_;std::lock_guard<std::mutex> life(s.lifecycle);std::lock_guard<std::mutex> lock(s.mutex);if(s.attempted)return false;s.attempted=true;
    if(!generation||!s.policy.enabled||!localTaskPath(s.helper)||!localTaskPath(s.file)||!validCreditSourceContract(s.binding)||!s.binding.cursor||
        s.policy.projectionRefreshMs<100||s.policy.projectionRefreshMs>60000||s.policy.helperTimeoutMs<50||s.policy.helperTimeoutMs>10000||s.policy.readBudgetMs<500||s.policy.readBudgetMs>60000)return false;
    try{s.base=s.last=s.clock();if(!instant(s.base.utcMs))return false;
        CreditProcessRequest probe;probe.generation=generation;probe.ticket=1;probe.file=s.file;probe.binding=s.binding;probe.limits=s.policy.ledger;probe.nowUtcMs=s.base.utcMs;probe.recovery=CreditRecoveryMode::Inspect;probe.queries={{true,0,s.base.utcMs}};
        if(!validForecastQuery(probe))return false;
        s.stopped=false;s.current.generation=generation;s.current.state=ForecastOwnerState::Waiting;s.unavailable(ForecastStatus::Unavailable);s.publish();
        s.thread=std::thread([p=&s]{p->run();});return true;}catch(...){s.stopped=true;s.current.state=ForecastOwnerState::Faulted;try{s.publish();}catch(...){}return false;}
}
ForecastAdmission IncomeForecastOwner::submit(uint64_t generation,const ForecastScenario &scenario){
    auto &s=*impl_;if(!boundedScenario(scenario,s.binding))return ForecastAdmission::Invalid;
    std::lock_guard<std::mutex> lock(s.mutex);if(generation!=s.current.generation)return ForecastAdmission::StaleGeneration;
    if(s.stopped)return ForecastAdmission::Stopped;
    if(s.queued||s.flight)return ForecastAdmission::NotReady;
    try{if(!s.time(s.clock()))return ForecastAdmission::Stopped;auto job=std::make_shared<Impl::Job>();job->scenario=scenario;
        const auto result=s.enqueue(std::move(job));if(result==ForecastAdmission::Queued)s.cached.reset();return result;}catch(...){return ForecastAdmission::Invalid;}
}
ForecastAdmission IncomeForecastOwner::updateMarket(uint64_t generation,std::optional<MarketQuote> token,std::optional<MarketQuote> fx,ForecastFxUpdate tariffFx){
    auto &s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);if(generation!=s.current.generation)return ForecastAdmission::StaleGeneration;
    if(s.stopped)return ForecastAdmission::Stopped;
    if(s.policy.suppressMarket)return ForecastAdmission::NotReady;
    if(!s.cached)return ForecastAdmission::NotReady;
    if(!boundedFx(tariffFx.value)||(!tariffFx.replace&&tariffFx.value))return ForecastAdmission::Invalid;
    try{if(!s.time(s.clock()))return ForecastAdmission::Stopped;auto job=std::make_shared<Impl::Job>();job->cached=s.cached;job->token=std::move(token);job->fx=std::move(fx);
        job->tariffFx=tariffFx.replace?std::move(tariffFx.value):s.cached->electricity.fx;return s.enqueue(std::move(job));}catch(...){return ForecastAdmission::Invalid;}
}
bool IncomeForecastOwner::invalidate(uint64_t generation){
    auto &s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);if(s.stopped||generation!=s.current.generation||s.epoch==UINT64_MAX)return false;
    ++s.epoch;s.cancelJobs();s.current.state=ForecastOwnerState::Waiting;
    try{s.unavailable(ForecastStatus::Stale);s.publish();}catch(...){s.current.result.reset();}
    s.wake.notify_all();return true;
}
void IncomeForecastOwner::requestStop(){
    auto &s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);s.attempted=true;if(s.current.state==ForecastOwnerState::Stopped)return;
    s.stopped=true;s.cancelJobs();s.current.state=ForecastOwnerState::Stopped;try{s.unavailable(ForecastStatus::Unavailable);s.publish();}catch(...){}s.wake.notify_all();
}
void IncomeForecastOwner::stop(){auto &s=*impl_;std::lock_guard<std::mutex> life(s.lifecycle);requestStop();if(s.thread.joinable())s.thread.join();std::lock_guard<std::mutex> lock(s.mutex);s.flight.reset();}
std::shared_ptr<const ForecastOwnerSnapshot> IncomeForecastOwner::snapshot()const{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    // Revocation cannot expose the old numeric report even if allocating its replacement snapshot failed.
    if(impl_->publishedEpoch!=impl_->epoch||(impl_->stopped&&impl_->published->state!=impl_->current.state))return impl_->emptyTerminal;
    return impl_->published;
}
} // namespace om
