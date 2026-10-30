// Cooperative pause only at a reconciled work boundary. A request is not an acknowledgement or a session-start permit.
#pragma once
#include <cstdint>
namespace om {
constexpr unsigned kWorkPausePollMs=10;
enum class WorkPauseResult { Ready, Stopped, Failed };
enum class WorkControlRequest { Run, Pause, Stop, Fault };
class RunPauseControl {
   public:
    virtual ~RunPauseControl()=default;
    virtual bool pauseRequested()const noexcept=0; // bounded memory-only read; independent stop always wins
    virtual WorkControlRequest requestState()const noexcept{return pauseRequested()?WorkControlRequest::Pause:WorkControlRequest::Run;}
    virtual void pauseBoundary(bool paused)noexcept=0; // worker-thread notification after drain / before resumed work
    virtual void pauseHeld()noexcept{} // still at the same drained boundary; may acknowledge a newer pause revision
    // Worker thread only, at a reconciled boundary after pause handling. Installing a wait does not prove work started.
    virtual WorkControlRequest applyWorkWait(unsigned &)noexcept{return requestState();}
};
template<class Stopped,class Drain,class Changed,class Now,class Sleep,class Observe>
WorkPauseResult pauseAtWorkBoundary(RunPauseControl &control,Stopped stopped,Drain drain,Changed changed,Now now,Sleep sleep,Observe observe){
    const auto request=[&]{return stopped()?WorkControlRequest::Stop:control.requestState();};
    const auto initial=request();
    if(initial==WorkControlRequest::Stop)return WorkPauseResult::Stopped;
    if(initial==WorkControlRequest::Fault)return WorkPauseResult::Failed;
    if(initial==WorkControlRequest::Run)return stopped()?WorkPauseResult::Stopped:WorkPauseResult::Ready;
    if(!drain())return WorkPauseResult::Failed;
    if(stopped())return WorkPauseResult::Stopped;
    uint64_t previous=now();
    changed(true);control.pauseBoundary(true);
    if(!observe(true))return WorkPauseResult::Failed;
    for(;;){
        const auto state=request();
        if(state==WorkControlRequest::Stop)return WorkPauseResult::Stopped; // keep productive accounting frozen through shutdown
        if(state==WorkControlRequest::Fault)return WorkPauseResult::Failed;
        const auto current=now();if(current<previous)return WorkPauseResult::Failed;previous=current;
        if(state==WorkControlRequest::Run){
            if(stopped())return WorkPauseResult::Stopped;
            if(!observe(false))return WorkPauseResult::Failed;
            if(stopped())return WorkPauseResult::Stopped;
            changed(false);
            // Stop can arrive inside the accounting hook or boundary notification. Refreeze before returning.
            if(stopped()){changed(true);return WorkPauseResult::Stopped;}
            control.pauseBoundary(false);
            const auto resumed=request();
            if(resumed!=WorkControlRequest::Run){
                changed(true);
                if(resumed==WorkControlRequest::Stop)return WorkPauseResult::Stopped;
                if(resumed==WorkControlRequest::Fault)return WorkPauseResult::Failed;
                control.pauseBoundary(true);if(!observe(true))return WorkPauseResult::Failed;
                continue; // a newer pause revoked resume before any new work
            }
            return WorkPauseResult::Ready;
        }
        control.pauseHeld();
        if(!observe(true))return WorkPauseResult::Failed;
        sleep(kWorkPausePollMs);
    }
}
template<class Stopped,class Drain,class Changed,class Now,class Sleep>
WorkPauseResult pauseAtWorkBoundary(RunPauseControl &control,Stopped stopped,Drain drain,Changed changed,Now now,Sleep sleep){
    return pauseAtWorkBoundary(control,stopped,drain,changed,now,sleep,[](bool){return true;});
}
} // namespace om
