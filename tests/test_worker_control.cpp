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
void waitCodec(){
    const std::string prefix="11111111111111111111111111111111 1 0 1 22222222222222222222222222222222 2 1000 2000 R";
    auto c=command(2,1000,D::Run);std::string wire;
    CHECK(encodeWorkerCommand(c,&wire)&&wire=="CMWC1 "+prefix+"\n");
    CHECK(encodeWorkerReceipt(receipt(c,K::Accepted),&wire)&&wire=="CMWA1 "+prefix+" A 1000\n");
    CHECK(!encodeWorkerReceipt(receipt(c,K::WaitApplied),&wire));
    for(unsigned ms:{0u,1u,1000u}){c.hostWaitMs=ms;WorkerCommand back;
        const auto suffix=prefix+" "+std::to_string(ms);
        CHECK(encodeWorkerCommand(c,&wire)&&wire=="CMWC2 "+suffix+"\n"&&decodeWorkerCommand(wire,&back)&&sameWorkerCommand(c,back));
        for(size_t split=0;split<=wire.size();++split){WorkerControlFramer framer;unsigned calls=0;
            const auto take=[&](std::string_view s){++calls;WorkerCommand parsed;return decodeWorkerCommand(s,&parsed)&&sameWorkerCommand(c,parsed);};
            CHECK(framer.feed(std::string_view(wire).substr(0,split),take)&&framer.feed(std::string_view(wire).substr(split),take)&&framer.complete()&&calls==1);}
        for(auto kind:{K::Accepted,K::WaitApplied}){WorkerReceipt out;
            CHECK(encodeWorkerReceipt(receipt(c,kind,1001),&wire)&&wire=="CMWA2 "+suffix+(kind==K::Accepted?" A ":" W ")+"1001\n"&&
                decodeWorkerReceipt(wire,&out)&&sameWorkerCommand(c,out.command)&&out.kind==kind&&out.observedMonoMs==1001);}
        CHECK(!encodeWorkerReceipt(receipt(c,K::Paused),&wire));
    }
    for(const auto *tail:{"", " -1", " +1", " 00", " 01", " 1001", " 1e2", " 0 extra", " 4294967296"}){
        WorkerCommand out=command(9);CHECK(!decodeWorkerCommand("CMWC2 "+prefix+tail+"\n",&out)&&out.revision==9);}
    for(const auto &bad:{"CMWC1 "+prefix+" 0\n","CMWC3 "+prefix+" 0\n","CMWC2 "+prefix+" 0\r\n"}){WorkerCommand out;CHECK(!decodeWorkerCommand(bad,&out));}
    for(const auto &bad:{"CMWA1 "+prefix+" W 1000\n","CMWA1 "+prefix+" 0 W 1000\n","CMWA2 "+prefix+" 0 P 1000\n",
        "CMWA2 "+prefix+" 0 W 2000\n","CMWA2 "+prefix+" 0 W 999\n","CMWA2 "+prefix+" 0 X 1000\n"}){WorkerReceipt out;CHECK(!decodeWorkerReceipt(bad,&out));}
    c.hostWaitMs=1001;CHECK(!encodeWorkerCommand(c,&wire));c.hostWaitMs=0;c.directive=D::Pause;CHECK(!encodeWorkerCommand(c,&wire));
}
struct WaitFixture {
    SessionIntent intent;SessionToken token=intent.explicitStart();uint64_t now=1000;
    WorkerControlIssuer parent{binding(),intent,token};WorkerControlGate gate{binding(),[&]{return now;}};
    WaitFixture(){CHECK(parent.start(now));const auto c=parent.issue(D::Pause,now,1000);CHECK(c&&gate.receive(*c)==C::Accepted);
        gate.pauseBoundary(true);CHECK(parent.acknowledge(*gate.snapshot().receipt,now)==A::Settled);}
    WorkerCommand run(unsigned ms=10){const auto c=parent.issue(D::Run,now,1000,ms);CHECK(c&&gate.receive(*c)==C::Accepted);return *c;}
};
void waitBoundary(){
    {WaitFixture f;unsigned applied=77;auto c=f.run();const auto accepted=*f.gate.snapshot().receipt;
        CHECK(accepted.kind==K::Accepted&&f.parent.acknowledge(accepted,f.now)==A::Waiting&&!f.parent.appliedWait(f.now));
        CHECK(!f.parent.issue(D::Run,f.now,1000,20)&&f.parent.snapshot(f.now).pending.has_value());
        CHECK(f.gate.applyWorkWait(applied)==WorkControlRequest::Pause&&applied==77&&f.gate.snapshot().receipt->kind==K::Accepted);
        f.gate.pauseBoundary(false);++f.now;
        CHECK(f.gate.applyWorkWait(applied)==WorkControlRequest::Run&&applied==10);
        const auto original=*f.gate.snapshot().receipt;
        CHECK(original.kind==K::WaitApplied&&original.observedMonoMs==1001&&sameWorkerCommand(original.command,c));
        CHECK(f.parent.acknowledge(original,f.now)==A::Settled&&f.parent.appliedWait(f.now)->command.hostWaitMs==10);
        ++f.now;CHECK(f.gate.applyWorkWait(applied)==WorkControlRequest::Run&&f.gate.snapshot().receipt->observedMonoMs==1001);
        CHECK(f.gate.receive(c)==C::Duplicate&&f.parent.acknowledge(original,f.now)==A::Duplicate);
        c=f.run(0);CHECK(!f.parent.appliedWait(f.now)&&f.gate.snapshot().receipt->kind==K::Accepted&&applied==10);
        CHECK(f.parent.acknowledge(original,f.now)==A::Duplicate&&f.parent.snapshot(f.now).pending->hostWaitMs==0);
        CHECK(f.parent.acknowledge(*f.gate.snapshot().receipt,f.now)==A::Waiting&&!f.parent.appliedWait(f.now));
        ++f.now;CHECK(f.gate.applyWorkWait(applied)==WorkControlRequest::Run&&applied==0);
        CHECK(f.parent.acknowledge(*f.gate.snapshot().receipt,f.now)==A::Settled&&f.parent.appliedWait(f.now)->observedMonoMs==1003);
        const auto pause=f.parent.issue(D::Pause,f.now,1000);CHECK(pause&&f.gate.receive(*pause)==C::Accepted&&!f.parent.appliedWait(f.now));
        CHECK(f.gate.applyWorkWait(applied)==WorkControlRequest::Pause&&applied==0);
        f.gate.pauseBoundary(true);CHECK(f.parent.acknowledge(*f.gate.snapshot().receipt,f.now)==A::Settled&&f.parent.paused(f.now));
        CHECK(!f.parent.issue(D::Run,f.now,1000)&&f.parent.snapshot(f.now).state==S::Faulted);
        auto downgrade=command(5,f.now,D::Run);CHECK(f.gate.receive(downgrade)==C::Rejected&&f.gate.applyWorkWait(applied)==WorkControlRequest::Fault);}
    for(unsigned mode=0;mode<5;++mode){WaitFixture f;unsigned applied=77;const auto c=f.run();f.gate.pauseBoundary(false);
        if(mode==0)f.now=c.deadlineMonoMs;
        if(mode==1)--f.now;
        if(mode==2)f.gate.requestStop();
        if(mode==3)f.gate.fail();
        if(mode==4){auto pause=command(3,f.now);CHECK(f.gate.receive(pause)==C::Accepted);}
        CHECK(f.gate.applyWorkWait(applied)!=(WorkControlRequest::Run)&&applied==77&&f.gate.snapshot().receipt->kind==K::Accepted);}
    for(unsigned mode=0;mode<6;++mode){WaitFixture f;unsigned applied=77;f.run();f.gate.pauseBoundary(false);
        CHECK(f.gate.applyWorkWait(applied)==WorkControlRequest::Run);auto proof=*f.gate.snapshot().receipt;
        if(mode==0)proof.command.hostWaitMs=11;
        if(mode==1)proof.command.hostWaitMs.reset();
        if(mode==2)++proof.command.revision;
        if(mode==3)++proof.command.binding.workerGeneration;
        if(mode==4)++proof.observedMonoMs;
        if(mode==5)proof.command.directive=D::Pause;
        CHECK(f.parent.acknowledge(proof,f.now)==A::Rejected&&!f.parent.appliedWait(f.now)&&f.parent.snapshot(f.now).pending.has_value());}
    for(bool stop:{false,true}){WaitFixture f;unsigned applied=77;const auto c=f.run();f.gate.pauseBoundary(false);
        CHECK(f.gate.applyWorkWait(applied)==WorkControlRequest::Run);const auto proof=*f.gate.snapshot().receipt;
        if(stop)f.intent.stop();else f.now=c.deadlineMonoMs;
        CHECK(f.parent.acknowledge(proof,f.now)==A::Terminal&&!f.parent.appliedWait(f.now)&&!f.parent.snapshot(f.now).pending);
        f.intent.stop();f.intent.explicitStart();CHECK(!f.parent.issue(D::Run,f.now,1000,0));}
    for(unsigned mode=0;mode<2;++mode){WaitFixture f;CHECK(!f.parent.issue(mode?D::Run:D::Pause,f.now,1000,mode?1001:0));CHECK(f.parent.snapshot(f.now).state==S::Faulted);}
    {WaitFixture f;const auto c=f.run();auto changed=c;changed.hostWaitMs=11;unsigned applied=77;
        CHECK(f.gate.receive(changed)==C::Rejected&&f.gate.applyWorkWait(applied)==WorkControlRequest::Fault&&applied==77);}
    for(bool wait:{false,true})for(unsigned mode=0;mode<2;++mode){WaitFixture f;
        const auto c=wait?f.run():*f.parent.issue(D::Pause,f.now,1000);auto accepted=receipt(c,K::Accepted,1002);f.now=1003;
        CHECK(f.parent.acknowledge(accepted,f.now)==A::Waiting&&f.parent.acknowledge(accepted,f.now)==A::Waiting);
        auto changed=accepted;if(mode==0){changed.kind=wait?K::WaitApplied:K::Paused;changed.observedMonoMs=1001;}else changed.observedMonoMs=1003;
        CHECK(f.parent.acknowledge(changed,f.now)==A::Rejected&&f.parent.snapshot(f.now).state==S::Faulted&&f.parent.snapshot(f.now).pending.has_value());}
}
}
int main(){codecs();framing();endToEnd();refusal();parentFences();concurrency();waitCodec();waitBoundary();return finishTests("fenced worker command protocol");}
