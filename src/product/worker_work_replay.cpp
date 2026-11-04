#include "worker_work_replay.h"
namespace om {
WorkerWorkReplay::WorkerWorkReplay(WorkerControlBinding b,const SessionIntent &i,SessionToken t,unsigned runs)
    :binding_(b),intent_(i),token_(t),capture_({true,runs,b.workerKey},i,t,b.workerGeneration){
    if(!validWorkerControlBinding(b)||b.epoch!=t.epoch||!i.permits(t))fail();
}
bool WorkerWorkReplay::fail()noexcept{failed_=true;capture_.requestStop();return false;}
void WorkerWorkReplay::requestStop()noexcept{if(!finished_)fail();}
bool WorkerWorkReplay::accept(const WorkerWorkEvent &e)noexcept{
    using K=WorkerWorkKind;
    if(failed_||finished_||!intent_.permits(token_)||!validWorkerWorkEvent(e)||!sameWorkerWorkBinding(binding_,e.binding)||
        sequence_==UINT64_MAX||e.sequence!=sequence_+1||(sequence_==0)!=(e.kind==K::Start))return fail();
    bool ok=false;
    switch(e.kind){
        case K::Start:ok=capture_.start(e.observed);if(ok)anchor_=e.observed;break;
        case K::Begin:ok=capture_.begin(e.lane,e.observed);break;
        case K::Completed:ok=capture_.completed({e.observed,e.workUnits,e.attempts,e.macsPerUnit,e.appliedHostWaitMs,e.flag});break;
        case K::Paused:case K::Runnable:case K::Leaving:
            ok=capture_.boundary({e.observed,e.kind==K::Paused?CompletedWorkBoundaryKind::Paused:
                e.kind==K::Runnable?CompletedWorkBoundaryKind::Runnable:CompletedWorkBoundaryKind::Leaving,e.appliedHostWaitMs});break;
        case K::End:ok=capture_.end(e.flag,e.observed);break;
        case K::Finish:
            // No invented terminal interval or restamped snapshot. Ready means every begun run explicitly reconciled.
            ok=capture_.snapshot().state==CompletedWorkState::Ready&&e.observed.utcMs>=capture_.snapshot().observed.utcMs&&
                e.observed.monotonicMs>=capture_.snapshot().observed.monotonicMs;
            if(ok){const auto wall=static_cast<uint64_t>(e.observed.utcMs-anchor_.utcMs),mono=e.observed.monotonicMs-anchor_.monotonicMs;
                ok=(wall>mono?wall-mono:mono-wall)<=2000;}
            if(ok){capture_.requestStop();finished_=true;}break;
    }
    if(!ok)return fail();
    sequence_=e.sequence;return true;
}
} // namespace om
