#include "core/work_pause.h"
#include "product_test_util.h"
#include <vector>
using namespace om;
struct Control:RunPauseControl {
    bool requested=true;std::vector<bool> boundaries;
    bool *stopOnResume=nullptr;
    bool pauseRequested()const noexcept override{return requested;}
    void pauseBoundary(bool paused)noexcept override{boundaries.push_back(paused);if(!paused&&stopOnResume)*stopOnResume=true;}
};
struct FencedControl:Control {
    WorkControlRequest state=WorkControlRequest::Pause;
    unsigned held=0;bool pauseOnResume=false;
    WorkControlRequest requestState()const noexcept override{return state;}
    void pauseBoundary(bool paused)noexcept override{Control::pauseBoundary(paused);if(!paused&&pauseOnResume){pauseOnResume=false;state=WorkControlRequest::Pause;}}
    void pauseHeld()noexcept override{++held;}
};
int main(){
    for(unsigned mode=0;mode<6;++mode){
        Control control;bool stop=mode==1;uint64_t now=100;unsigned drains=0,sleeps=0;std::vector<bool> changes;
        if(mode==0)control.requested=false;
        const auto result=pauseAtWorkBoundary(control,[&]{return stop;},[&]{++drains;return mode!=2;},[&](bool p){changes.push_back(p);},[&]{return now;},[&](unsigned ms){
            CHECK(ms==10);++sleeps;if(mode==3)stop=true;else if(mode==4)--now;else{now+=ms;if(sleeps==3)control.requested=false;}});
        if(mode==0||mode==1)CHECK(!drains&&!sleeps&&changes.empty()&&control.boundaries.empty());
        else if(mode==2)CHECK(drains==1&&!sleeps&&changes.empty()&&control.boundaries.empty());
        else if(mode==3||mode==4)CHECK(drains==1&&sleeps==1&&changes==std::vector<bool>{true}&&control.boundaries==changes);
        else CHECK(drains==1&&sleeps==3&&changes==std::vector<bool>({true,false})&&control.boundaries==changes);
        CHECK(result==(mode==1||mode==3?WorkPauseResult::Stopped:mode==2||mode==4?WorkPauseResult::Failed:WorkPauseResult::Ready));
    }
    {Control control;bool stop=false;unsigned sleeps=0;
        CHECK(pauseAtWorkBoundary(control,[&]{return stop;},[&]{stop=true;return true;},[](bool){std::abort();},[]{return 0;},[&](unsigned){++sleeps;})==WorkPauseResult::Stopped);
        CHECK(!sleeps&&control.boundaries.empty());}
    for(bool duringNotification:{false,true}){Control control;bool stop=false;std::vector<bool> changes;
        if(duringNotification)control.stopOnResume=&stop;
        CHECK(pauseAtWorkBoundary(control,[&]{return stop;},[]{return true;},[&](bool paused){
            changes.push_back(paused);if(!paused&&!duringNotification)stop=true;},[]{return 100;},[&](unsigned){control.requested=false;})==WorkPauseResult::Stopped);
        CHECK(changes==std::vector<bool>({true,false,true})&&control.boundaries==
            (duringNotification?std::vector<bool>({true,false}):std::vector<bool>{true}));}
    for(auto state:{WorkControlRequest::Stop,WorkControlRequest::Fault}){FencedControl control;control.state=state;unsigned drains=0;
        CHECK(pauseAtWorkBoundary(control,[]{return false;},[&]{++drains;return true;},[](bool){std::abort();},[]{return 100;},[](unsigned){std::abort();})==
            (state==WorkControlRequest::Stop?WorkPauseResult::Stopped:WorkPauseResult::Failed));CHECK(!drains&&control.boundaries.empty());}
    {FencedControl control;control.pauseOnResume=true;unsigned drains=0;std::vector<bool> changes;
        CHECK(pauseAtWorkBoundary(control,[]{return false;},[&]{++drains;return true;},[&](bool p){changes.push_back(p);},[]{return 100;},
            [&](unsigned){control.state=WorkControlRequest::Run;})==WorkPauseResult::Ready);
        CHECK(drains==1&&control.held==2&&changes==std::vector<bool>({true,false,true,false})&&changes==control.boundaries);}
    {FencedControl control;std::vector<bool> changes;
        CHECK(pauseAtWorkBoundary(control,[]{return false;},[]{return true;},[&](bool p){changes.push_back(p);},[]{return 100;},
            [&](unsigned){control.state=WorkControlRequest::Fault;})==WorkPauseResult::Failed);
        CHECK(changes==std::vector<bool>{true}&&control.boundaries==changes);}
    return finishTests("cooperative work pause boundary");
}
