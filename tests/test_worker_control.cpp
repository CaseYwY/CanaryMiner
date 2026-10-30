#include "product/worker_control.h"
#include "product_test_util.h"
#include <atomic>
#include <limits>
#include <stdexcept>
#include <thread>
using namespace om;
namespace {
using S=WorkerControlState;using D=WorkerDirective;using K=WorkerReceiptKind;using C=WorkerCommandResult;using A=WorkerReceiptResult;
WorkerControlBinding binding(){return {"11111111111111111111111111111111","22222222222222222222222222222222",1,1,0};}
WorkerCommand command(uint64_t revision=1,uint64_t issued=1000,D directive=D::Pause){return {binding(),revision,issued,issued+1000,directive};}
WorkerReceipt receipt(WorkerCommand c=command(),K k=K::Paused,uint64_t observed=1000){return {std::move(c),k,observed};}
void codecs(){
    for(auto d:{D::Pause,D::Run}){auto c=command(1,1000,d);std::string wire;WorkerCommand back;
        CHECK(encodeWorkerCommand(c,&wire)&&wire.size()<=workerControlFrameLimit&&decodeWorkerCommand(wire,&back)&&sameWorkerCommand(c,back));
        for(auto k:{K::Accepted,K::Paused}){auto r=receipt(c,k);WorkerReceipt decoded;
            if(k==K::Paused&&d==D::Run){CHECK(!encodeWorkerReceipt(r,&wire));continue;}
            CHECK(encodeWorkerReceipt(r,&wire)&&decodeWorkerReceipt(wire,&decoded)&&decoded.kind==k&&decoded.observedMonoMs==1000&&sameWorkerCommand(c,decoded.command));}}
    const std::vector<std::function<void(WorkerCommand &)>> invalid={
        [](auto &c){c.binding.sessionKey="";},[](auto &c){c.binding.workerKey=std::string(33,'1');},[](auto &c){c.binding.workerKey=std::string(32,'0');},
        [](auto &c){c.binding.sessionKey[0]='A';},[](auto &c){c.binding.epoch=0;},[](auto &c){c.binding.workerGeneration=0;},[](auto &c){c.binding.slot=256;},
        [](auto &c){c.revision=0;},[](auto &c){c.issuedMonoMs=0;},[](auto &c){c.deadlineMonoMs=c.issuedMonoMs;},
        [](auto &c){c.deadlineMonoMs=c.issuedMonoMs+workerControlMaxLeaseMs+1;},[](auto &c){c.directive=static_cast<D>(99);}};
    for(const auto &change:invalid){auto c=command();change(c);std::string wire="preserve";CHECK(!encodeWorkerCommand(c,&wire)&&wire=="preserve");}
    std::string canonical;CHECK(encodeWorkerCommand(command(),&canonical));
    std::vector<std::string> bad={"",canonical+"\n",canonical.substr(0,canonical.size()-1)," "+canonical,canonical.substr(1),std::string(385,'x')};
    auto change=[&](const std::string &from,const std::string &to){auto w=canonical;const auto p=w.find(from);w.replace(p,from.size(),to);bad.push_back(w);};
    change("CMWC1","CMWC2");change(" 0 "," 00 ");change(" 0 "," -1 ");change(" 0 "," +1 ");change(" 0 "," 256 ");
    change(" 1000 "," 18446744073709551616 ");change(" 1000 "," 1e3 ");change(" 1000 "," 1000\t");change(" P\n"," X\n");
    change(" P\n"," P extra\n");change(" P\n"," P\r\n");change(" 0 ",std::string(" \0 ",3));
    for(const auto &w:bad){auto out=command(7);CHECK(!decodeWorkerCommand(w,&out)&&out.revision==7);}
    auto maximum=command(std::numeric_limits<uint64_t>::max(),std::numeric_limits<uint64_t>::max()-workerControlMaxLeaseMs);
    maximum.binding.epoch=maximum.binding.workerGeneration=std::numeric_limits<uint64_t>::max();maximum.binding.slot=255;
    maximum.deadlineMonoMs=std::numeric_limits<uint64_t>::max();auto r=receipt(maximum,K::Paused,maximum.deadlineMonoMs-1);WorkerReceipt back;
    CHECK(encodeWorkerReceipt(r,&canonical)&&canonical.size()<workerControlFrameLimit&&decodeWorkerReceipt(canonical,&back)&&sameWorkerCommand(maximum,back.command));
    for(uint64_t observed:{999ull,2000ull}){auto invalidReceipt=receipt(command(),K::Paused,observed);CHECK(!encodeWorkerReceipt(invalidReceipt,&canonical));}
    r=receipt();r.kind=static_cast<K>(99);CHECK(!encodeWorkerReceipt(r,&canonical));
    CHECK(!decodeWorkerReceipt("CMWA1\n",&r)&&!decodeWorkerCommand("CMWC1\n",nullptr));
}
void framing(){
    std::string wire;encodeWorkerCommand(command(),&wire);
    for(size_t split=0;split<=wire.size();++split){WorkerControlFramer f;unsigned calls=0;
        const auto frame=[&](std::string_view data){++calls;WorkerCommand c;return decodeWorkerCommand(data,&c);};
        CHECK(f.feed(std::string_view(wire).substr(0,split),frame)&&f.feed(std::string_view(wire).substr(split),frame)&&f.complete()&&calls==1);}
    {WorkerControlFramer f;unsigned calls=0;const auto frame=[&](std::string_view){++calls;return true;};
        for(char c:wire)CHECK(f.feed(std::string_view(&c,1),frame));
        CHECK(calls==1&&f.complete());}
    for(const std::string &bad:{std::string(385,'x'),std::string(4097,'x'),std::string(17,'\n'),std::string("x\0",2),std::string("x\r")}){
        WorkerControlFramer f;CHECK(!f.feed(bad,[](auto){return true;})&&f.failed()&&!f.complete()&&!f.feed(wire,[](auto){return true;}));}
    {WorkerControlFramer f;CHECK(!f.feed(wire,[](auto){return false;})&&f.failed());}
    {WorkerControlFramer f;CHECK(!f.feed(wire,[](auto)->bool{throw std::runtime_error("fixture");})&&f.failed());}
    {WorkerControlFramer f;CHECK(f.feed("CMWC1",[](auto){return true;})&&!f.complete());}
}
void endToEnd(){
    SessionIntent intent;auto token=intent.explicitStart();WorkerControlIssuer parent(binding(),intent,token);uint64_t now=1000;
    WorkerControlGate gate(binding(),[&]{return now;});CHECK(parent.start(now)&&!parent.start(now)&&gate.requestState()==WorkControlRequest::Pause);
    auto hold=parent.issue(D::Pause,now,1000);CHECK(hold&&gate.receive(*hold)==C::Accepted&&!parent.issue(D::Run,now,1000));
    auto received=*gate.snapshot().receipt;CHECK(received.kind==K::Accepted&&parent.acknowledge(received,now)==A::Waiting&&!parent.paused(now));
    unsigned sleeps=0,drains=0;std::vector<bool> changed;
    const auto result=pauseAtWorkBoundary(gate,[]{return false;},[&]{++drains;return true;},[&](bool p){changed.push_back(p);},[&]{return now;},[&](unsigned ms){
        ++sleeps;now+=ms;auto proof=*gate.snapshot().receipt;CHECK(proof.kind==K::Paused&&parent.acknowledge(proof,now)==A::Settled&&parent.paused(now));
        if(sleeps==1){auto renew=parent.issue(D::Pause,now,1000);CHECK(renew&&gate.receive(*renew)==C::Accepted&&!parent.paused(now));}
        else{auto run=parent.issue(D::Run,now,1000);CHECK(run&&gate.receive(*run)==C::Accepted);auto ack=*gate.snapshot().receipt;
            CHECK(ack.kind==K::Accepted&&parent.acknowledge(ack,now)==A::Settled&&!parent.paused(now));}});
    CHECK(result==WorkPauseResult::Ready&&sleeps==2&&drains==1&&changed==std::vector<bool>({true,false}));
    CHECK(gate.requestState()==WorkControlRequest::Run&&!gate.snapshot().atPausedBoundary);
    auto old=*gate.snapshot().receipt;now+=100;CHECK(gate.receive(old.command)==C::Duplicate&&gate.snapshot().receipt->observedMonoMs==old.observedMonoMs);
    CHECK(parent.acknowledge(old,now)==A::Duplicate);now=old.command.deadlineMonoMs;
    CHECK(gate.requestState()==WorkControlRequest::Fault&&gate.snapshot().state==S::Expired&&parent.snapshot(now).state==S::Expired);
    CHECK(!parent.issue(D::Run,now,1000)&&gate.receive(command(4,now,D::Run))==C::Rejected);
    gate.requestStop();parent.requestStop();CHECK(gate.requestState()==WorkControlRequest::Stop&&parent.snapshot(0).state==S::Stopped);
}
void refusal(){
    const std::vector<std::function<void(WorkerCommand &)>> wrong={
        [](auto &c){c.binding.sessionKey[0]='3';},[](auto &c){c.binding.workerKey[0]='3';},[](auto &c){++c.binding.epoch;},
        [](auto &c){++c.binding.slot;},[](auto &c){++c.binding.workerGeneration;},[](auto &c){c.revision=2;},
        [](auto &c){c.directive=D::Run;},[](auto &c){c.issuedMonoMs=1001;},[](auto &c){c.deadlineMonoMs=1000;}};
    for(const auto &change:wrong){uint64_t now=1000;WorkerControlGate gate(binding(),[&]{return now;});auto c=command();change(c);
        CHECK(gate.receive(c)==C::Rejected&&gate.requestState()==WorkControlRequest::Fault&&gate.receive(command())==C::Rejected);}
    for(unsigned mode=0;mode<5;++mode){uint64_t now=1000;WorkerControlGate gate(binding(),[&]{return now;});CHECK(gate.receive(command())==C::Accepted);
        gate.pauseBoundary(true);auto modified=command();
        if(mode==0)modified.directive=D::Run;
        if(mode==1)++modified.deadlineMonoMs;
        if(mode==2)modified.revision=3;
        if(mode==3){modified=command(2,999);}
        if(mode==4){now=2000;modified=command(2,2000);}
        CHECK(gate.receive(modified)==C::Rejected&&gate.pauseRequested());}
    {uint64_t now=1000;WorkerControlGate gate(binding(),[&]{return now;});now=999;CHECK(gate.requestState()==WorkControlRequest::Fault&&gate.snapshot().state==S::ClockFault);
        now=1000;CHECK(gate.receive(command())==C::Rejected);gate.requestStop();CHECK(gate.requestState()==WorkControlRequest::Stop);}
    {uint64_t now=1000;WorkerControlGate gate(binding(),[&]{return now;});now+=workerControlMaxLeaseMs;CHECK(gate.snapshot().state==S::Expired);}
    {WorkerControlGate gate(binding(),[]{return std::numeric_limits<uint64_t>::max();});CHECK(gate.snapshot().state==S::ClockFault);}
    {WorkerControlGate gate(binding(),[]()->uint64_t{throw std::runtime_error("clock");});CHECK(gate.snapshot().state==S::ClockFault);}
    {WorkerControlGate gate({},[]{return 1000;});CHECK(gate.requestState()==WorkControlRequest::Fault);}
    {WorkerControlGate gate(binding(),[]{return 1000;});CHECK(gate.receiveFrame("garbage\n")==C::Rejected&&gate.snapshot().state==S::Faulted);}
    {WorkerControlGate gate(binding(),[]{return 1000;});gate.requestStop();gate.fail();CHECK(gate.receive(command())==C::Rejected&&gate.snapshot().state==S::Stopped);}
    {uint64_t now=1000;auto b=binding();WorkerControlGate gate(b,[&]{return now;});b.workerKey[0]='3';
        CHECK(gate.receive(command())==C::Accepted);gate.pauseBoundary(true);const auto proof=*gate.snapshot().receipt;
        now=1100;gate.pauseHeld();CHECK(gate.receive(command())==C::Duplicate&&gate.snapshot().receipt->observedMonoMs==proof.observedMonoMs);
        now=2000;CHECK(gate.receive(command())==C::Rejected&&gate.snapshot().receipt->observedMonoMs==proof.observedMonoMs);}
}
void parentFences(){
    for(unsigned mode=0;mode<10;++mode){SessionIntent intent;const auto token=intent.explicitStart();WorkerControlIssuer p(binding(),intent,token);CHECK(p.start(1000));
        auto c=p.issue(D::Pause,1000,1000);CHECK(c.has_value());auto r=receipt(*c);
        if(mode==0)r.command.binding.workerKey[0]='3';
        if(mode==1)++r.command.revision;
        if(mode==2)++r.command.deadlineMonoMs;
        if(mode==3)r.observedMonoMs=999;
        if(mode==4)r.observedMonoMs=1001;
        if(mode==5)r.command.directive=D::Run;
        if(mode==6)r.kind=static_cast<K>(99);
        if(mode==7)r.command.binding.slot=1;
        if(mode==8)r.command.binding.workerGeneration=2;
        if(mode==9)r.command.binding.epoch=2;
        CHECK(p.acknowledge(r,1000)==A::Rejected&&p.snapshot(1000).state==S::Faulted&&!p.issue(D::Run,1000,1000)&&!p.paused(1000));
        CHECK(p.snapshot(1000).pending.has_value());}
    for(bool stopped:{false,true}){SessionIntent intent;const auto token=intent.explicitStart();WorkerControlIssuer p(binding(),intent,token);CHECK(p.start(1000));
        auto c=*p.issue(D::Pause,1000,1000);if(stopped)intent.stop();
        CHECK(p.snapshot(2000).state==(stopped?S::Stopped:S::Expired)&&p.snapshot(2000).pending.has_value());
        CHECK(p.acknowledge(receipt(c),2000)==A::Terminal&&!p.snapshot(2000).pending&&!p.paused(2000));
        intent.stop();intent.explicitStart();CHECK(!p.issue(D::Run,2000,1000)&&!p.start(2000));}
    for(unsigned mode=0;mode<5;++mode){SessionIntent intent;auto token=intent.explicitStart();WorkerControlIssuer p(binding(),intent,token);CHECK(p.start(1000));
        CHECK(!p.issue(mode==0?D::Run:mode==1?static_cast<D>(99):D::Pause,mode==4?std::numeric_limits<uint64_t>::max():1000,mode==2?0:mode==3?5001:1000));
        CHECK(p.snapshot(1000).state==S::Faulted);}
    {SessionIntent intent;auto token=intent.explicitStart();WorkerControlIssuer p(binding(),intent,token);CHECK(p.start(1000));
        CHECK(!p.issue(D::Pause,999,1000)&&p.snapshot(1000).state==S::ClockFault);}
    {SessionIntent intent;auto token=intent.explicitStart();WorkerControlIssuer p(binding(),intent,token);p.requestStop();CHECK(!p.start(1000));}
    {SessionIntent intent;auto token=intent.explicitStart();WorkerControlIssuer p(binding(),intent,token);CHECK(p.acknowledge(receipt(),1000)==A::Rejected&&!p.start(1000));}
    {SessionIntent intent;auto token=intent.explicitStart();auto b=binding();b.epoch=2;WorkerControlIssuer p(b,intent,token);CHECK(!p.start(1000));}
    {SessionIntent intent;auto token=intent.explicitStart();WorkerControlIssuer p(binding(),intent,token);CHECK(!p.start(0)&&!p.start(1000));}
    {SessionIntent intent;auto token=intent.explicitStart();WorkerControlIssuer p(binding(),intent,token);CHECK(p.start(1000));intent.stop();
        CHECK(!p.issue(D::Run,0,0)&&p.snapshot(0).state==S::Stopped);}
    {SessionIntent intent;auto token=intent.explicitStart();WorkerControlIssuer p(binding(),intent,token);CHECK(p.start(1000));auto c=*p.issue(D::Pause,1000,1000);auto r=receipt(c);
        CHECK(p.acknowledge(r,1000)==A::Settled&&p.paused(1000));auto next=p.issue(D::Run,1001,1000);CHECK(next&&!p.paused(1001));
        CHECK(p.acknowledge(r,1002)==A::Duplicate&&p.snapshot(1002).pending->revision==2);
        ++r.observedMonoMs;CHECK(p.acknowledge(r,1002)==A::Rejected&&p.snapshot(1002).state==S::Faulted);}
}
void concurrency(){
    std::atomic<uint64_t> now{1000};WorkerControlGate gate(binding(),[&]{return now.load();});std::atomic<bool> done{false};
    std::thread worker([&]{while(!done.load()){if(gate.requestState()==WorkControlRequest::Pause){gate.pauseBoundary(true);gate.pauseHeld();}else gate.pauseBoundary(false);}});
    bool good=gate.receive(command())==C::Accepted;
    for(unsigned i=0;i<200;++i){const auto view=gate.snapshot();good=good&&view.state==S::Active;}
    gate.requestStop();done=true;worker.join();CHECK(good&&gate.requestState()==WorkControlRequest::Stop&&gate.receive(command(2))==C::Rejected);
}
}
int main(){codecs();framing();endToEnd();refusal();parentFences();concurrency();return finishTests("fenced worker command protocol");}
