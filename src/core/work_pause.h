// Cooperative pause only at a reconciled work boundary. A request is not an acknowledgement or a session-start permit.
#pragma once
#include <cstdint>
namespace om {
constexpr unsigned kWorkPausePollMs=10;
enum class WorkPauseResult { Ready, Stopped, Failed };
class RunPauseControl {
   public:
    virtual ~RunPauseControl()=default;
    virtual bool pauseRequested()const noexcept=0; // bounded memory-only read; independent stop always wins
    virtual void pauseBoundary(bool paused)noexcept=0; // worker-thread notification after drain / before resumed work
};
template<class Stopped,class Drain,class Changed,class Now,class Sleep>
WorkPauseResult pauseAtWorkBoundary(RunPauseControl &control,Stopped stopped,Drain drain,Changed changed,Now now,Sleep sleep){
    if(stopped())return WorkPauseResult::Stopped;
    if(!control.pauseRequested())return WorkPauseResult::Ready;
    if(!drain())return WorkPauseResult::Failed;
    if(stopped())return WorkPauseResult::Stopped;
    uint64_t previous=now();
    changed(true);control.pauseBoundary(true);
    for(;;){
        if(stopped())return WorkPauseResult::Stopped; // keep productive accounting frozen through shutdown
        const auto current=now();if(current<previous)return WorkPauseResult::Failed;previous=current;
        if(!control.pauseRequested()){
            if(stopped())return WorkPauseResult::Stopped;
            changed(false);
            // Stop can arrive inside the accounting hook or boundary notification. Refreeze before returning.
            if(stopped()){changed(true);return WorkPauseResult::Stopped;}
            control.pauseBoundary(false);
            if(stopped()){changed(true);return WorkPauseResult::Stopped;}
            return WorkPauseResult::Ready;
        }
        sleep(kWorkPausePollMs);
    }
}
} // namespace om
