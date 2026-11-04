// Exact raw originals over owned CPU processes only. No GPU, telemetry, provider, installed miner or qualification.
#include "product/worker_work_replay.h"
#include "product/worker_process.h"
#include "product/worker_process_owner.h"
#include "product_test_util.h"
#include <thread>
#include <stdexcept>
using namespace om;
namespace {
using K=WorkerWorkKind;
WorkerControlBinding binding(){return {std::string(32,'1'),std::string(32,'2'),1,7,3};}
CompletedWorkTime at(uint64_t n=1000){return {static_cast<int64_t>(1700000000000ULL+n),n};}
void sleep(){std::this_thread::sleep_for(std::chrono::milliseconds(2));}
std::vector<WorkerWorkEvent> trace(){
    std::vector<WorkerWorkEvent> out;WorkerWorkPublisher p(binding(),[&](const auto &e){out.push_back(e);return true;});
    CHECK(p.start(at())&&p.begin(WorkLane::User,at(1001)));
    CHECK(p.boundary({at(1002),CompletedWorkBoundaryKind::Paused,0}));
    CHECK(p.boundary({at(1012),CompletedWorkBoundaryKind::Paused,0}));
    CHECK(p.boundary({at(1013),CompletedWorkBoundaryKind::Leaving,0}));
    CHECK(p.completed({at(1020),6,2,524288,10,false}));
    CHECK(p.boundary({at(1021),CompletedWorkBoundaryKind::Runnable,10}));
    CHECK(p.boundary({at(1031),CompletedWorkBoundaryKind::Leaving,10}));
    CHECK(p.end(true,at(1032))&&p.begin(WorkLane::Donation,at(1033)));
    CHECK(p.completed({at(1040),9,3,524288,0,false})&&p.end(true,at(1041))&&p.finish(at(1042))&&!p.failed());
    return out;
}
void codec(){
    const auto originals=trace();for(const auto &e:originals){std::string wire;WorkerWorkEvent decoded;
        CHECK(encodeWorkerWorkEvent(e,&wire)&&wire.size()<=workerControlFrameLimit&&decodeWorkerWorkEvent(wire,&decoded));
        std::string again;CHECK(encodeWorkerWorkEvent(decoded,&again)&&wire==again);
        for(size_t split=0;split<=wire.size();++split){WorkerControlFramer frames;unsigned count=0;
            const auto receive=[&](std::string_view s){++count;return s==wire;};
            CHECK(frames.feed(std::string_view(wire).substr(0,split),receive)&&frames.feed(std::string_view(wire).substr(split),receive)&&frames.complete()&&count==1);}
        for(const auto &bad:{wire+"\n",wire.substr(0,wire.size()-1)," "+wire,wire.substr(0,wire.size()-1)+"\r\n"})CHECK(!decodeWorkerWorkEvent(bad,&decoded));
    }
    for(unsigned field=0;field<16;++field){std::string wire;CHECK(encodeWorkerWorkEvent(originals[5],&wire));
        size_t begin=0;for(unsigned i=0;i<field;++i)begin=wire.find(' ',begin)+1;
        auto end=wire.find(' ',begin);if(end==wire.npos)end=wire.size()-1;
        for(const auto *replace:{"", "-1", "+1", "01", "18446744073709551616", "\t"}){
            const auto malformed=wire.substr(0,begin)+replace+wire.substr(end);WorkerWorkEvent e;CHECK(!decodeWorkerWorkEvent(malformed,&e));}
    }
    auto e=originals[5];e.binding.epoch=UINT64_MAX;e.binding.workerGeneration=UINT64_MAX;e.sequence=UINT64_MAX;
    e.observed={253402300799999LL,UINT64_MAX};e.workUnits=UINT64_MAX;e.attempts=1;e.macsPerUnit=UINT64_MAX;e.appliedHostWaitMs=1000;
    std::string wire;CHECK(encodeWorkerWorkEvent(e,&wire)&&wire.size()<=workerControlFrameLimit);WorkerWorkEvent decoded;CHECK(decodeWorkerWorkEvent(wire,&decoded));
    for(unsigned mode=0;mode<12;++mode){e=originals[0];
        if(mode==0)e.kind=static_cast<K>(99);
        if(mode==1)e.lane=WorkLane::Donation;
        if(mode==2)e.sequence=0;
        if(mode==3)e.observed.utcMs=-1;
        if(mode==4)e.observed.monotonicMs=0;
        if(mode==5)e.workUnits=1;
        if(mode==6)e.appliedHostWaitMs=1;
        if(mode==7)e.flag=true;
        if(mode==8)e.binding.workerKey=std::string(32,'0');
        if(mode==9)e.binding.slot=256;
        if(mode==10)e.binding.epoch=0;
        if(mode==11)e.binding.workerGeneration=0;
        CHECK(!validWorkerWorkEvent(e)&&!encodeWorkerWorkEvent(e,&wire));}
    CHECK(!encodeWorkerWorkEvent(originals[0],nullptr)&&!decodeWorkerWorkEvent(wire,nullptr));
}
void replay(){
    const auto originals=trace();SessionIntent intent;const auto token=intent.explicitStart();WorkerWorkReplay p(binding(),intent,token);
    for(const auto &e:originals)CHECK(p.accept(e));
    const auto v=p.snapshot();CHECK(p.complete()&&!p.failed()&&p.sequence()==originals.size());
    CHECK(v.generation==1&&v.counterEpoch==7&&v.runs==2&&v.batches==2&&v.attempts==5&&v.workUnitsPerAttempt==3&&v.macsPerUnit==524288);
    CHECK(v.userTmacs.text()=="0.000003145728"&&v.donationTmacs.text()=="0.000004718592");
    CHECK(v.lastBatch&&v.lastBatch->lane==WorkLane::Donation&&v.lastBatch->batch.observed.utcMs==at(1040).utcMs);
    CHECK(v.lastInterval&&v.lastInterval->sourceKey==binding().workerKey&&v.lastInterval->begin.monotonicMs==1021&&v.lastInterval->end.monotonicMs==1031);
    CHECK(v.observed.monotonicMs==1041); // Finish delivery does not restamp the final capture
    p.requestStop();CHECK(p.complete()&&p.snapshot().userTmacs.text()==v.userTmacs.text());
    for(unsigned mode=0;mode<20;++mode){WorkerWorkReplay r(binding(),intent,token);auto events=originals;
        if(mode==0)events[0].sequence=2;
        if(mode==1)events[1].sequence=1;
        if(mode==2)events[1].sequence=3;
        if(mode==3)events[1].binding.epoch=2;
        if(mode==4)events[1].binding.slot=4;
        if(mode==5)events[1].binding.workerGeneration=8;
        if(mode==6)events[1].binding.sessionKey=std::string(32,'3');
        if(mode==7)events[1].binding.workerKey=std::string(32,'4');
        if(mode==8)events[1].kind=K::Start;
        if(mode==9)events[2].observed.monotonicMs=500;
        if(mode==10)events[5].workUnits=0;
        if(mode==11)events[5].flag=true;
        if(mode==12)events[8].flag=false;
        if(mode==13)events[4].kind=K::Paused;
        if(mode==14)events[10].macsPerUnit=42;
        if(mode==15)events[12].observed.monotonicMs=1;
        if(mode==16)events[1].observed.utcMs+=5000;
        if(mode==17)events[2].appliedHostWaitMs=1001;
        if(mode==18)events[12].observed.utcMs+=3000;
        if(mode==19)events[12].observed.monotonicMs+=3000;
        bool refused=false;for(const auto &e:events)if(!r.accept(e)){refused=true;break;}
        CHECK(refused&&r.failed()&&!r.complete()&&!r.accept(originals[0]));}
    {WorkerWorkReplay r(binding(),intent,token,1);bool refused=false;for(const auto &e:originals)if(!r.accept(e)){refused=true;break;}CHECK(refused);}
    {WorkerWorkReplay r(binding(),intent,token);CHECK(r.accept(originals[0]));r.requestStop();CHECK(!r.accept(originals[1])&&!r.complete());}
    {WorkerWorkReplay r(binding(),intent,token);CHECK(r.accept(originals[0]));intent.stop();CHECK(!r.accept(originals[1]));}
    {WorkerWorkReplay r(binding(),intent,token);CHECK(r.failed()&&!r.accept(originals[0]));}
    {SessionIntent i;const auto t=i.explicitStart();auto b=binding();b.epoch=2;WorkerWorkReplay r(b,i,t);CHECK(r.failed());}
}
void publisher(){
    {WorkerWorkPublisher p(binding(),[](const auto &){return false;});CHECK(!p.start(at())&&p.failed()&&!p.start(at()));}
    {WorkerWorkPublisher p(binding(),[](const auto &)->bool{throw std::runtime_error("fixture");});CHECK(!p.start(at())&&p.failed());}
    {WorkerWorkPublisher p(binding(),{});CHECK(!p.start(at())&&p.failed());}
    {bool original=false;WorkerWorkPublisher p(binding(),[&](const auto &e){original=e.kind==K::Completed&&e.flag;return true;});
        CHECK(p.start(at())&&p.begin(WorkLane::User,at()));CHECK(!p.completed({at(),1,1,1,0,true})&&p.failed()&&original);}
    for(unsigned mode=0;mode<7;++mode){unsigned calls=0;WorkerWorkPublisher p(binding(),[&](const auto &){++calls;return true;});
        if(mode==0)CHECK(!p.begin(WorkLane::User,at()));
        if(mode==1)CHECK(!p.finish(at()));
        if(mode==2)CHECK(!p.completed({at(),1,1,1,0,false}));
        if(mode>=3){CHECK(p.start(at()));if(mode==3)CHECK(!p.start(at()));
            else{CHECK(p.begin(WorkLane::User,at()));if(mode==4)CHECK(!p.begin(WorkLane::User,at()));
                if(mode==5)CHECK(!p.finish(at()));
                if(mode==6)CHECK(!p.end(false,at()));}}
        CHECK(p.failed());const auto before=calls;CHECK(!p.start(at())&&calls==before);}
}
WorkerCommand command(uint64_t revision,WorkerDirective d=WorkerDirective::Pause){const auto now=workerControlNowMs();return {binding(),revision,now,now+3000,d,d==WorkerDirective::Run?std::optional<unsigned>(10):std::nullopt};}
int probe(int argc,char **argv){
    if(argc!=7||std::string(argv[3])!="--worker-control-v1")return 90;
    const std::string mode=argv[2];WorkerChannelHandles h;WorkerControlClient client(mode!="disabled");
    if(!parseWorkerChannelHandles(argv[4],argv[5],argv[6],&h)||!client.adopt(h))return 91;
    const auto until=workerControlNowMs()+2000;WorkerControlSnapshot view;
    do{view=client.snapshot();if(view.receipt)break;sleep();}while(workerControlNowMs()<until);
    if(!view.receipt)return 92;
    client.pauseBoundary(true);uint64_t sequence=0;
    WorkerWorkPublisher p(view.receipt->command.binding,[&](const auto &e){if(!client.publishWork(e))return false;sequence=e.sequence;return true;});
    if(mode=="disabled")return !p.start(completedWorkNow())?0:93;
    if(mode=="overflow"){
        if(!p.start(completedWorkNow())||!p.begin(WorkLane::User,completedWorkNow()))return 94;
        for(unsigned i=0;i<10000;++i)if(!p.completed({completedWorkNow(),1,1,1,0,false}))return client.requestState()==WorkControlRequest::Fault?0:95;
        return 96;
    }
    if(!p.start(completedWorkNow())||!p.begin(WorkLane::Donation,completedWorkNow())||!p.completed({completedWorkNow(),6,2,524288,0,false})||
        (mode!="many"&&!p.end(true,completedWorkNow())))return 97;
    if(mode=="many"){
        for(unsigned i=0;i<100;++i){std::this_thread::sleep_for(std::chrono::milliseconds(15));
            if(!p.completed({completedWorkNow(),6,2,524288,0,false}))return 0;}
        return 100;
    }
    if(mode!="incomplete"&&!p.finish(completedWorkNow()))return 98;
    while(client.flushedWorkSequence()!=sequence&&workerControlNowMs()<until)sleep();
    const bool sent=client.flushedWorkSequence()==sequence;client.stop();return sent?0:99;
}
void native(const std::filesystem::path &self,const std::filesystem::path &loop){
    std::atomic<bool> cancel{false};
    for(unsigned mode=0;mode<5;++mode){const bool actual=mode==4;WorkerControlProcess process;SessionIntent intent;const auto token=intent.explicitStart();
        WorkerWorkReplay replay(binding(),intent,token);const std::vector<std::string> args=actual?std::vector<std::string>{"--work-stream-fixture"}:
            std::vector<std::string>{"--probe",mode==1?"incomplete":mode==2?"overflow":mode==3?"disabled":"normal"};
        CHECK(process.start(actual?loop:self,args,command(1),cancel,true));
        const auto until=workerControlNowMs()+5000;bool paused=false,run=false,sound=true,closed=false,complete=false,reaped=false,bounded=true;
        bool deferred=false,postExitTail=false;int code=-1;size_t records=0;
        do{const auto v=process.poll();sound=sound&&!v.controlFault;
            for(const auto &r:v.receipts)if(r.kind==WorkerReceiptKind::Paused)paused=true;
            for(const auto &e:v.work){++records;sound=sound&&replay.accept(e);}
            bounded=bounded&&v.work.size()<=workerControlFramesPerChunk&&v.output.find("CMWW1")==v.output.npos&&v.output.find(binding().workerKey)==v.output.npos;
            if(actual&&paused&&!run){CHECK(process.submit(command(2,WorkerDirective::Run)));run=true;}
            closed=v.workStreamClosed;complete=v.workStreamComplete;reaped=process.reaped();code=v.exitCode;
            postExitTail=postExitTail||(reaped&&!closed&&!v.work.empty());
            if(v.state==WorkerProcessState::Exited&&closed)break;
            if(v.state==WorkerProcessState::Failed)break;
            if(mode==0&&paused&&!deferred){std::this_thread::sleep_for(std::chrono::milliseconds(500));deferred=true;}
            sleep();
        }while(workerControlNowMs()<until);
        CHECK(reaped&&code==0&&closed&&bounded); // buffered tail must survive actual exit and drain to EOF
        if(mode==2){CHECK(!complete);continue;}
        CHECK(sound&&complete==(mode==0||actual)&&replay.complete()==complete);
        if(mode==0)CHECK(records==5&&postExitTail&&replay.snapshot().donationTmacs.text()=="0.000003145728");
        if(actual)CHECK(run&&replay.snapshot().batches==2&&replay.snapshot().attempts==2&&replay.snapshot().userTmacs.text()=="0.000001048576"&&
            replay.snapshot().lastBatch->batch.appliedHostWaitMs==10);
    }
    // Parent has to opt in too: an unsolicited raw frame is a protocol fault, never an implicit activation.
    {WorkerControlProcess process;CHECK(process.start(self,{"--probe","normal"},command(1),cancel));bool fault=false;
        const auto until=workerControlNowMs()+3000;while(workerControlNowMs()<until){const auto v=process.poll();fault=fault||v.controlFault;if(fault)break;sleep();}
        CHECK(fault);process.requestStop();CHECK(process.terminate(500));}
    // The background facade retains complete originals without sensor work or IPC on the caller's thread.
    for(unsigned mode=0;mode<4;++mode){const bool actual=mode==2;WorkerProcessOwner owner;
        CHECK(owner.start(actual?loop:self,actual?std::vector<std::string>{"--work-stream-fixture"}:
            std::vector<std::string>{"--probe",mode==1?"incomplete":mode==3?"many":"normal"},command(1),500,true));
        SessionIntent intent;const auto token=intent.explicitStart();WorkerWorkReplay replay(binding(),intent,token);
        const auto until=workerControlNowMs()+5000;WorkerOwnerSnapshot v;bool run=false,sound=true;size_t records=0;
        do{v=owner.snapshot();
            if(mode!=3)for(const auto &e:owner.takeWork()){++records;sound=sound&&replay.accept(e);}
            if(actual&&!run&&v.receipt&&v.receipt->kind==WorkerReceiptKind::Paused){CHECK(owner.submit(command(2,WorkerDirective::Run)));run=true;}
            if(v.done)break;
            sleep();
        }while(workerControlNowMs()<until);
        // A final take handles the publication between snapshot() and takeWork().
        if(mode!=3)for(const auto &e:owner.takeWork()){++records;sound=sound&&replay.accept(e);}
        CHECK(v.done&&v.reaped&&v.revoked&&sound);
        if(mode==0||actual)CHECK(v.exitCode==0&&v.workStreamComplete&&replay.complete()&&v.workSequence==records);
        else CHECK(v.state==WorkerOwnerState::Faulted&&v.exitCode==8&&!v.workStreamComplete);
        if(mode==3){const auto retained=owner.takeWork();CHECK(retained.size()<=32&&!retained.empty()&&retained.back().sequence==v.workSequence);}
        CHECK(owner.takeWork().empty()&&!owner.submit(command(3,WorkerDirective::Run)));owner.stop();
        CHECK(!owner.start(self,{"--probe","normal"},command(1),500,true));
    }
    {WorkerProcessOwner owner;owner.requestStop();CHECK(!owner.start(self,{"--probe","normal"},command(1),500,true)&&owner.takeWork().empty());}
    {WorkerProcessOwner owner;CHECK(owner.start(self,{"--probe","many"},command(1),500,true));
        const auto until=workerControlNowMs()+3000;WorkerOwnerSnapshot v;
        do{v=owner.snapshot();if(v.workSequence>=3||v.done)break;sleep();}while(workerControlNowMs()<until);
        CHECK(v.workSequence>=3&&!v.done);owner.requestStop();const auto frozen=owner.snapshot().workSequence;
        const auto retained=owner.takeWork();owner.stop();v=owner.snapshot();
        CHECK(!retained.empty()&&v.done&&v.reaped&&v.revoked&&!v.workStreamComplete&&v.workSequence==frozen&&owner.takeWork().empty());
        CHECK(!owner.submit(command(2,WorkerDirective::Run)));}
}
}
int main(int argc,char **argv){
    if(argc>1&&std::string(argv[1])=="--probe")return probe(argc,argv);
    codec();replay();publisher();if(argc==2)native(std::filesystem::absolute(argv[0]),std::filesystem::absolute(argv[1]));else CHECK(false);
    return finishTests("original worker raw-work stream");
}
