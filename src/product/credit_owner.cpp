#include "credit_owner.h"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace om {
namespace {
int64_t utcNowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
bool accepted(CreditLedgerStatus s) { return s == CreditLedgerStatus::Ok || s == CreditLedgerStatus::Replay || s == CreditLedgerStatus::StaleBatch; }
}
struct CreditLedgerOwner::Impl {
    struct Job { CreditProcessRequest request; size_t bytes = 0; bool recovery = false; };
    CreditProcessClient client;
    std::filesystem::path file;
    CreditSourceContract binding;
    CreditOwnerPolicy policy;
    CreditOwnerClock clock;
    mutable std::mutex mutex;
    std::mutex lifecycle;
    std::condition_variable wait;
    std::atomic<bool> cancel{true};
    std::thread thread;
    std::deque<std::shared_ptr<const Job>> jobs;
    std::shared_ptr<const Job> inFlight;
    std::shared_ptr<const Job> uncertain;
    CreditOwnerSnapshot current;
    std::shared_ptr<const CreditOwnerSnapshot> published = std::make_shared<const CreditOwnerSnapshot>();
    bool opened = false;
    uint64_t greatestGeneration = 0;
    int64_t lastUtc = 0;
    Impl(std::filesystem::path helper, std::filesystem::path path, CreditSourceContract c, CreditOwnerPolicy p, CreditOwnerClock time)
        : client(std::move(helper)), file(std::move(path)), binding(std::move(c)), policy(p), clock(time?std::move(time):CreditOwnerClock(utcNowMs)) {}
    void publish() { // mutex held; no disk, process wait, callbacks or provider work
        current.pendingJobs = jobs.size()+(inFlight?1:0);
        current.writeOutcomeUnknown = static_cast<bool>(uncertain);
        current.uncertainSequence = uncertain?uncertain->request.batch->sequence:0; current.uncertainSnapshotId = uncertain?uncertain->request.batch->snapshotId:"";
        ++current.revision; published = std::make_shared<const CreditOwnerSnapshot>(current);
    }
    void discard() {
        current.droppedJobs += jobs.size();
        for (const auto &j : jobs) current.reservedWireBytes -= j->bytes;
        jobs.clear();
    }
    CreditEnqueueResult enqueue(const CreditBatch *batch, const std::vector<CreditQuerySpec> &queries, bool recovery, bool initial = false) {
        CreditEnqueueResult result;
        if (cancel.load() || (!initial && !opened)) return result;
        if (queries.size() > 4) { result.status = CreditEnqueueStatus::Invalid; return result; }
        if (uncertain && batch && !recovery) { result.status = CreditEnqueueStatus::NeedsReconciliation; return result; }
        if (jobs.size()+(inFlight?1:0) >= policy.maxJobs) { result.status = CreditEnqueueStatus::Full; return result; }
        const auto now = clock();
        if (now < lastUtc || current.lastAcceptedTicket == UINT64_MAX) { result.status = CreditEnqueueStatus::Invalid; return result; }
        auto j = std::make_shared<Job>(); auto &r = j->request;
        r.generation = current.generation; r.ticket = current.lastAcceptedTicket+1; r.file = file; r.binding = binding; r.limits = policy.ledger; r.nowUtcMs = now;
        r.queries = queries.empty() ? std::vector<CreditQuerySpec>{{false,now > 86400000 ? now-86400000:0,now},{true,0,now}} : queries;
        // Validate borrowed input before copying any caller-controlled vectors/strings into the bounded queue.
        if (batch && (!boundedCreditBatchInput(*batch,now) || !sameCreditSourceContract(binding,batch->binding))) { result.status = CreditEnqueueStatus::Invalid; return result; }
        if (batch) r.batch = *batch;
        std::string wire;
        if (!encodeCreditRequest(r,&wire)) { result.status = CreditEnqueueStatus::Invalid; return result; }
        if (wire.size() > policy.maxWireBytes-current.reservedWireBytes) { result.status = CreditEnqueueStatus::Full; return result; }
        j->bytes = wire.size(); j->recovery = recovery; jobs.push_back(j);
        const auto previousTicket = current.lastAcceptedTicket; const auto previousUtc = lastUtc;
        current.reservedWireBytes += j->bytes; current.lastAcceptedTicket = r.ticket; lastUtc = now;
        try { publish(); } catch (...) { jobs.pop_back(); current.reservedWireBytes -= j->bytes; current.lastAcceptedTicket = previousTicket; lastUtc = previousUtc; throw; }
        wait.notify_one();
        result.status = CreditEnqueueStatus::Queued; result.ticket = r.ticket; return result;
    }
    void run() noexcept {
        std::shared_ptr<const Job> executing;
        try {
            for (;;) {
                std::shared_ptr<const Job> job;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    wait.wait(lock,[&]{return cancel.load() || !jobs.empty();});
                    if (cancel.load()) return;
                    inFlight = jobs.front(); jobs.pop_front(); job = inFlight; executing = job;
                    if (opened) current.state = CreditOwnerState::Busy;
                    publish();
                }
                auto result = client.transact(job->request,cancel,policy.operationTimeoutMs);
                std::lock_guard<std::mutex> lock(mutex);
                current.reservedWireBytes -= job->bytes; inFlight.reset();
                if (cancel.load()) return; // requestStop already revoked publication; storage may nevertheless have committed
                current.transport = result.transport; current.reply = std::move(result.reply);
                bool ok = current.transport == LocalTaskStatus::Ok && current.reply && current.reply->opened == CreditLedgerStatus::Ok;
                if (current.reply) {
                    current.lastCompletedTicket = job->request.ticket;
                    if (current.reply->applied && !accepted(*current.reply->applied)) ok = false;
                    for (const auto &t : current.reply->totals) if (t.status != CreditLedgerStatus::Ok) ok = false;
                }
                if (result.writeOutcomeUnknown && job->request.batch) uncertain = job;
                if (job->recovery) {
                    if (ok && current.reply->applied && (*current.reply->applied == CreditLedgerStatus::Ok || *current.reply->applied == CreditLedgerStatus::Replay)) uncertain.reset();
                    else ok = false; // a stale stream cursor does not prove that this old uncertain content was committed
                }
                if (!ok) { current.state = CreditOwnerState::Faulted; cancel = true; opened = false; discard(); publish(); return; }
                opened = true; current.lastSuccessfulTicket = job->request.ticket;
                current.state = uncertain ? CreditOwnerState::NeedsReconciliation : CreditOwnerState::Ready; publish(); executing.reset();
            }
        } catch (...) {
            try {
                std::lock_guard<std::mutex> lock(mutex);
                if (cancel.load()) return;
                if (executing && executing->request.batch) uncertain = executing;
                if (inFlight) current.reservedWireBytes -= inFlight->bytes;
                inFlight.reset(); discard(); opened = false; cancel = true; current.transport = LocalTaskStatus::Unavailable; current.state = CreditOwnerState::Faulted; publish();
            } catch (...) {}
        }
    }
};
CreditLedgerOwner::CreditLedgerOwner(std::filesystem::path helper, std::filesystem::path file, CreditSourceContract binding, CreditOwnerPolicy policy, CreditOwnerClock clock)
    : impl_(new Impl(std::move(helper),std::move(file),std::move(binding),policy,std::move(clock))) {}
CreditLedgerOwner::~CreditLedgerOwner() { stop(); }
bool CreditLedgerOwner::start(uint64_t generation) {
    auto &s = *impl_; std::lock_guard<std::mutex> control(s.lifecycle); std::lock_guard<std::mutex> lock(s.mutex);
    if (s.thread.joinable() || !generation || generation <= s.greatestGeneration || !localTaskPath(s.file) || !validCreditSourceContract(s.binding) ||
        !s.policy.maxJobs || s.policy.maxJobs > 32 || s.policy.maxWireBytes < 1024 || s.policy.maxWireBytes > 8*1024*1024 || s.policy.operationTimeoutMs < 50 || s.policy.operationTimeoutMs > 60000) return false;
    try {
        s.greatestGeneration = generation;
        s.current = {}; s.current.binding = s.binding; s.current.generation = generation; s.current.state = CreditOwnerState::Starting;
        s.cancel = false; s.opened = false; s.lastUtc = 0;
        if (s.enqueue(nullptr,{},false,true).status != CreditEnqueueStatus::Queued) { s.cancel = true; s.current.state = CreditOwnerState::Stopped; s.publish(); return false; }
        s.thread = std::thread([p=&s]{p->run();}); return true;
    } catch (...) { s.cancel = true; s.discard(); s.current.state = CreditOwnerState::Faulted; try {s.publish();} catch (...) {} return false; }
}
CreditEnqueueResult CreditLedgerOwner::submit(uint64_t generation, const CreditBatch &batch, const std::vector<CreditQuerySpec> &queries) {
    auto &s = *impl_; std::lock_guard<std::mutex> lock(s.mutex);
    if (generation != s.current.generation) return {CreditEnqueueStatus::StaleGeneration,0};
    try { return s.enqueue(&batch,queries,false); } catch (...) { return {CreditEnqueueStatus::Invalid,0}; }
}
CreditEnqueueResult CreditLedgerOwner::refresh(uint64_t generation, const std::vector<CreditQuerySpec> &queries) {
    auto &s = *impl_; std::lock_guard<std::mutex> lock(s.mutex);
    if (generation != s.current.generation) return {CreditEnqueueStatus::StaleGeneration,0};
    try { return s.enqueue(nullptr,queries,false); } catch (...) { return {CreditEnqueueStatus::Invalid,0}; }
}
CreditEnqueueResult CreditLedgerOwner::retryUncertain(uint64_t generation) {
    auto &s = *impl_; std::lock_guard<std::mutex> lock(s.mutex);
    if (generation != s.current.generation) return {CreditEnqueueStatus::StaleGeneration,0};
    if (!s.uncertain || !s.jobs.empty() || s.inFlight) return {CreditEnqueueStatus::NotReady,0};
    try { return s.enqueue(&*s.uncertain->request.batch,{},true); } catch (...) { return {CreditEnqueueStatus::Invalid,0}; }
}
void CreditLedgerOwner::requestStop() {
    auto &s = *impl_; std::lock_guard<std::mutex> lock(s.mutex);
    s.cancel = true; s.opened = false;
    if (s.inFlight && s.inFlight->request.batch) s.uncertain = s.inFlight;
    s.discard(); s.current.state = CreditOwnerState::Stopped; try {s.publish();} catch (...) {} s.wait.notify_all();
}
void CreditLedgerOwner::stop() {
    auto &s = *impl_; std::lock_guard<std::mutex> control(s.lifecycle);
    requestStop(); if (s.thread.joinable()) s.thread.join();
    std::lock_guard<std::mutex> lock(s.mutex); s.current.reservedWireBytes = 0; s.inFlight.reset(); try {s.publish();} catch (...) {}
}
std::shared_ptr<const CreditOwnerSnapshot> CreditLedgerOwner::snapshot() const { std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->published; }
}  // namespace om
