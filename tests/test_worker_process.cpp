// Owned CPU subprocesses only. No installed miner, GPU, provider or game.
#include "product/worker_process.h"
#include "product/worker_identity.h"
#include "product/worker_process_owner.h"
#include "product_test_util.h"
#include <cstdio>
#include <thread>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif
using namespace om;
namespace {
using S=WorkerProcessState;using D=WorkerDirective;using K=WorkerReceiptKind;
std::atomic<bool> cancel{false};
WorkerControlBinding binding(){return {"11111111111111111111111111111111","22222222222222222222222222222222",1,1,0};}
WorkerCommand command(uint64_t revision=1,D directive=D::Pause,unsigned lease=3000){const auto now=workerControlNowMs();return {binding(),revision,now,now+lease,directive};}
void sleep(){std::this_thread::sleep_for(std::chrono::milliseconds(5));}
uint64_t pid(){
#if defined(_WIN32)
    return GetCurrentProcessId();
#else
    return static_cast<uint64_t>(getpid());
#endif
}
int probe(int argc,char **argv){
    if(argc!=7||std::string(argv[3])!="--worker-control-v1")return 91;
    WorkerChannelHandles handles;if(!parseWorkerChannelHandles(argv[4],argv[5],argv[6],&handles))return 92;
    const std::string mode=argv[2];
    if(mode=="stalled"){for(;;)sleep();}
    WorkerControlClient client;if(!client.adopt(handles))return 93;
    std::thread stopper;
    if(mode=="cancel-idle")stopper=std::thread([&]{std::this_thread::sleep_for(std::chrono::milliseconds(100));client.requestStop();});
    std::printf("CPUPID %llu\n",static_cast<unsigned long long>(pid()));std::fflush(stdout);
    if(mode=="flood"){
        const std::string longLine(9000,'x'),line(1100,'y');std::printf("%s\n",longLine.c_str());
        for(unsigned i=0;i<500;++i)std::printf("%s\n",line.c_str());
        std::fflush(stdout);
    }
    unsigned batches=0,drains=0;const auto until=workerControlNowMs()+10000;WorkPauseResult result=WorkPauseResult::Failed;
    while(workerControlNowMs()<until){
        if(mode=="no-boundary"){
            const auto state=client.requestState();if(state==WorkControlRequest::Stop){result=WorkPauseResult::Stopped;break;}
            if(state==WorkControlRequest::Fault)break;
            sleep();continue;
        }
        result=pauseAtWorkBoundary(client,[]{return false;},[&]{++drains;return true;},[](bool){},workerControlNowMs,[](unsigned ms){std::this_thread::sleep_for(std::chrono::milliseconds(ms));});
        if(result!=WorkPauseResult::Ready)break;
        ++batches;std::printf("CPUBATCH %u\n",batches);std::fflush(stdout);sleep();
    }
    client.stop();if(stopper.joinable())stopper.join();std::printf("CPUDONE %u %u\n",batches,drains);std::fflush(stdout);
    return result==WorkPauseResult::Stopped?0:8;
}
WorkerProcessPoll waitFor(WorkerControlProcess &process,const std::function<bool(const WorkerProcessPoll &)> &done,std::string *log=nullptr,unsigned ms=4000){
    const auto end=workerControlNowMs()+ms;WorkerProcessPoll view;
    do{view=process.poll();if(log)*log+=view.output;if(done(view))return view;sleep();}while(workerControlNowMs()<end);
    return view;
}
bool receipt(WorkerControlProcess &process,uint64_t revision,K kind,std::string *output=nullptr){bool found=false;
    waitFor(process,[&](const auto &view){for(const auto &r:view.receipts)if(r.command.revision==revision&&r.kind==kind)found=true;
        return found||view.state==S::Exited||view.state==S::Failed;},output);return found;
}
void normal(const std::filesystem::path &exe){
    WorkerControlProcess process;auto initial=command();CHECK(process.start(exe,{"--probe","normal"},initial,cancel));
    CHECK(!process.start(exe,{"--probe","normal"},initial,cancel));std::string output;
    CHECK(receipt(process,1,K::Paused,&output)&&output.find("CPUBATCH")==output.npos);
    CHECK(process.submit(command(2,D::Run))&&receipt(process,2,K::Accepted,&output));
    waitFor(process,[](const auto &view){return view.output.find("CPUBATCH")!=view.output.npos;},&output);
    CHECK(process.submit(command(3))&&receipt(process,3,K::Paused,&output));
    CHECK(process.submit(command(4))&&receipt(process,4,K::Paused,&output)); // held renewal is a new proof
    CHECK(process.submit(command(5,D::Run))&&receipt(process,5,K::Accepted,&output));
    process.requestStop();CHECK(!process.submit(command(6,D::Run)));
    const auto final=waitFor(process,[](const auto &v){return v.state==S::Exited;},&output);
    CHECK(final.state==S::Exited&&final.exitCode==0&&process.reaped());
    CHECK(output.find("CPUBATCH")!=output.npos&&output.find("CMWC1")==output.npos&&output.find("CMWA1")==output.npos&&output.find(binding().workerKey)==output.npos);
}
void refusals(const std::filesystem::path &exe){
    for(unsigned mode=0;mode<7;++mode){WorkerControlProcess process;auto c=command();std::atomic<bool> stopped{mode==0};
        if(mode==1)c.directive=D::Run;
        if(mode==2)c.revision=2;
        if(mode==3)c.binding.workerKey.clear();
        if(mode==4)c.deadlineMonoMs=c.issuedMonoMs;
        CHECK(!process.start(mode==5?std::filesystem::path("relative"):exe,mode==6?std::vector<std::string>{"--worker-control-v1"}:std::vector<std::string>{"--probe","normal"},c,stopped));
        CHECK(!process.start(exe,{"--probe","normal"},command(),cancel));}
    {WorkerControlProcess process;process.requestStop();CHECK(!process.start(exe,{"--probe","normal"},command(),cancel));}
    {WorkerControlProcess process;CHECK(!process.start(exe.parent_path()/"missing-fixture-worker",{"--probe","normal"},command(),cancel)||process.terminate());}
    for(unsigned mode=0;mode<4;++mode){WorkerControlProcess process;auto c=command(1,D::Pause,mode==0?500:3000);
        CHECK(process.start(exe,{"--probe",mode==1?"no-boundary":"normal"},c,cancel));
        CHECK(receipt(process,1,mode==1?K::Accepted:K::Paused));
        if(mode==1){c=command(2,D::Run);CHECK(process.submit(c));} // Accepted is not initial Pause permission
        if(mode==2){c=command(1,D::Run);CHECK(process.submit(c));} // changed meaning under same revision
        if(mode==3){c=command(3,D::Run);CHECK(process.submit(c));} // skipped revision
        const auto final=waitFor(process,[](const auto &v){return v.state==S::Exited;});
        CHECK(final.state==S::Exited&&final.exitCode==8&&process.reaped());}
    {WorkerControlProcess process;CHECK(process.start(exe,{"--probe","normal"},command(),cancel));
        CHECK(receipt(process,1,K::Paused));auto foreign=command(2,D::Run);foreign.binding.workerGeneration=2;
        CHECK(!process.submit(foreign));process.requestStop();CHECK(waitFor(process,[](const auto &v){return v.state==S::Exited;}).exitCode==0);}
    {WorkerControlProcess process;const auto begin=workerControlNowMs();CHECK(process.start(exe,{"--probe","cancel-idle"},command(),cancel));
        const auto result=waitFor(process,[](const auto &v){return v.state==S::Exited;});
        CHECK(result.exitCode==0&&process.reaped()&&workerControlNowMs()-begin<2000);}
}
void backpressure(const std::filesystem::path &exe){
    WorkerControlProcess process;const auto begin=workerControlNowMs();CHECK(process.start(exe,{"--probe","stalled"},command(1,D::Pause,500),cancel));
    unsigned refused=0;for(unsigned i=0;i<2000;++i){process.poll();if(!process.submit(command(i+2,D::Pause,500)))++refused;}
    CHECK(refused>0&&workerControlNowMs()-begin<3000);
    CHECK(waitFor(process,[](const auto &v){return v.state==S::Failed;},nullptr,2000).state==S::Failed&&!process.reaped());
    process.requestStop();CHECK(!process.reaped());CHECK(process.terminate(500)&&process.reaped());
    CHECK(!process.submit(command(3000,D::Run)));
}
void identity(const std::filesystem::path &helper){
    uint64_t parent=0;unsigned count=0;
    CHECK(workerIdentityRequest("CMWK1 1 1\n",&parent,&count)&&parent==1&&count==1);
    CHECK(workerIdentityRequest("CMWK1 4294967295 257\n",&parent,&count)&&count==257);
    for(const auto *wire:{"CMWK1 0 1\n","CMWK1 01 1\n","CMWK1 1 0\n","CMWK1 1 258\n","CMWK1 1 1\r\n","CMWK1 1 1 extra\n","CMWK1 1 1\n\n"})CHECK(!workerIdentityRequest(wire,&parent,&count));
    for(unsigned n:{1u,2u,257u}){WorkerIdentitySource source(helper);const auto result=source.request(n,cancel);
        CHECK(result.status==LocalTaskStatus::Ok&&result.keys.size()==n);CHECK(source.request(n,cancel).status==LocalTaskStatus::Busy);}
    {WorkerIdentitySource source(helper);std::atomic<bool> stop{true};CHECK(source.request(2,stop).status==LocalTaskStatus::Cancelled);}
    {WorkerIdentitySource source(helper);CHECK(source.request(258,cancel).status==LocalTaskStatus::Invalid);}
    WorkerChannelHandles handles;
    CHECK(!parseWorkerChannelHandles("03","3","1",&handles)&&!parseWorkerChannelHandles("3","3","0",&handles)&&!parseWorkerChannelHandles("-1","3","1",&handles));
    WorkerControlClient client;CHECK(!client.adopt({})&&client.requestState()==WorkControlRequest::Fault);client.requestStop();CHECK(client.requestState()==WorkControlRequest::Stop&&!client.adopt({}));
}
void nativeEntries(const std::filesystem::path &shared,const std::filesystem::path &cli){
    {WorkerControlProcess process;std::string output;
        CHECK(process.start(shared,{"--channel-fixture"},command(),cancel)&&receipt(process,1,K::Paused,&output));
        CHECK(output.find("NATIVEPAUSE 0 0")!=output.npos);
        CHECK(process.submit(command(2,D::Run))&&receipt(process,2,K::Accepted,&output));
        waitFor(process,[](const auto &v){return v.output.find("SOLUTION ACCEPTED")!=v.output.npos;},&output);
        CHECK(process.submit(command(3))&&receipt(process,3,K::Paused,&output));
        CHECK(process.submit(command(4,D::Run))&&receipt(process,4,K::Accepted,&output));
        std::this_thread::sleep_for(std::chrono::milliseconds(25));process.requestStop();
        const auto final=waitFor(process,[](const auto &v){return v.state==S::Exited;},&output);
        CHECK(final.exitCode==0&&output.find("NATIVEFINAL ")!=output.npos);}
    const std::vector<std::string> args={"run","--algo","pearl-pow","--worker-child","--worker-id","0","--pool","fixture.invalid:1","--wallet",std::string("prl1")+std::string(59,'q'),
        "--pearl-network","mainnet","--pearl-target-semantics","base-before-penalty"};
    for(unsigned mode=0;mode<3;++mode){WorkerControlProcess process;std::string output;
        CHECK(process.start(cli,args,command(1,D::Pause,mode==1?700:3000),cancel)&&receipt(process,1,K::Paused,&output));
        if(mode==0)process.requestStop();
        if(mode==2)CHECK(process.submit(command(2,D::Run)));
        const auto final=waitFor(process,[](const auto &v){return v.state==S::Exited;},&output);
        CHECK(final.state==S::Exited&&final.exitCode==(mode==0?0:mode==1?8:97));
        CHECK((output.find("FORBIDDEN_DEVICE_SETUP")!=output.npos)==(mode==2));}
}
WorkerOwnerSnapshot waitOwner(WorkerProcessOwner &owner,const std::function<bool(const WorkerOwnerSnapshot &)> &done){
    const auto until=workerControlNowMs()+5000;WorkerOwnerSnapshot view;
    do{view=owner.snapshot();if(done(view))return view;sleep();}while(workerControlNowMs()<until);return view;
}
void backgroundOwner(const std::filesystem::path &exe){
    {WorkerProcessOwner owner;auto initial=command(1,D::Run);CHECK(!owner.start(exe,{"--probe","normal"},initial));
        const auto done=owner.snapshot();CHECK(done.revoked);CHECK(done.done&&!done.reaped&&done.state==WorkerOwnerState::Faulted&&done.exitCode==8);}
    {WorkerProcessOwner owner;CHECK(owner.start(exe.parent_path()/"missing-fixture-worker",{"--probe","normal"},command()));
        const auto done=waitOwner(owner,[](const auto &v){return v.done;});
        CHECK(done.revoked);CHECK(done.done&&!done.reaped&&done.state==WorkerOwnerState::Faulted&&done.exitCode==8);}
    {WorkerProcessOwner owner;CHECK(owner.start(exe,{"--probe","cancel-idle"},command(),500));
        const auto done=waitOwner(owner,[](const auto &v){return v.done;});
        CHECK(done.revoked);CHECK(done.done&&done.reaped&&!owner.submit(command(2,D::Run)));}
    {WorkerProcessOwner owner;owner.requestStop();CHECK(!owner.start(exe,{"--probe","normal"},command())&&owner.snapshot().done&&owner.snapshot().revoked);}
    {WorkerProcessOwner owner;CHECK(owner.start(exe,{"--probe","normal"},command(),500));owner.requestStop();owner.stop();
        CHECK(owner.snapshot().done&&owner.snapshot().revoked&&!owner.submit(command(2,D::Run))&&!owner.start(exe,{"--probe","normal"},command()));}
    for(const auto *mode:{"normal","flood"}){WorkerProcessOwner owner;CHECK(owner.start(exe,{"--probe",mode},command(),500));
        const auto paused=waitOwner(owner,[](const auto &v){return v.receipt&&v.receipt->kind==K::Paused;});
        CHECK(paused.state==WorkerOwnerState::Running&&paused.receipt&&paused.receipt->kind==K::Paused);
        if(std::string(mode)=="flood"){
            auto lines=owner.takeLines();size_t bytes=0;bool complete=true;for(const auto &line:lines){bytes+=line.size();complete=complete&&line.size()<=8192&&line.find('x')==line.npos;}
            CHECK(owner.snapshot().droppedLines>0&&lines.size()<=32&&bytes<=32768&&complete);
        }
        CHECK(owner.submit(command(2,D::Run)));
        const auto running=waitOwner(owner,[](const auto &v){return v.receipt&&v.receipt->command.revision==2;});
        CHECK(running.receipt&&running.receipt->command.directive==D::Run&&running.receipt->kind==K::Accepted);
        CHECK(owner.submit(command(3)));
        const auto held=waitOwner(owner,[](const auto &v){return v.receipt&&v.receipt->command.revision==3&&v.receipt->kind==K::Paused;});
        CHECK(held.receipt&&held.receipt->kind==K::Paused);owner.requestStop();
        CHECK(owner.snapshot().revoked&&!owner.submit(command(4,D::Run)));owner.stop();
        CHECK(owner.snapshot().done&&owner.snapshot().reaped&&owner.snapshot().exitCode==0);
    }
    {WorkerProcessOwner owner;CHECK(owner.start(exe,{"--probe","normal"},command(1,D::Pause,500),500));
        const auto done=waitOwner(owner,[](const auto &v){return v.done;});
        CHECK(done.done&&done.state==WorkerOwnerState::Faulted&&done.exitCode==8&&done.reaped&&done.revoked&&!owner.submit(command(2,D::Run)));}
    {WorkerProcessOwner owner;CHECK(owner.start(exe,{"--probe","stalled"},command(),0));
        waitOwner(owner,[](const auto &v){return v.state==WorkerOwnerState::Running;});owner.stop();
        CHECK(owner.snapshot().done&&owner.snapshot().reaped&&owner.snapshot().exitCode==7);}
}
int parentDeath(const std::filesystem::path &exe){
    WorkerControlProcess process;if(!process.start(exe,{"--probe","normal"},command(1,D::Pause,5000),cancel))return 94;
    bool sent=false;waitFor(process,[&](const auto &v){const auto at=v.output.find("CPUPID ");if(at!=v.output.npos){
        const auto end=v.output.find('\n',at);if(end!=v.output.npos){std::printf("%s\n",v.output.substr(at,end-at).c_str());std::fflush(stdout);sent=true;}}
        return sent;});
    if(!sent)return 95;
    for(;;){process.poll();sleep();}
}
#if !defined(_WIN32)
int stolenReap(const std::filesystem::path &exe){
    {WorkerControlProcess process;std::string output;
        CHECK(process.start(exe,{"--probe","normal"},command(),cancel)&&receipt(process,1,K::Paused,&output));
        const auto at=output.find("CPUPID ");CHECK(at!=output.npos);if(at==output.npos)return 98;
        const auto child=static_cast<pid_t>(std::stol(output.substr(at+7)));process.requestStop();
        int status=0;pid_t reaped=0;const auto until=workerControlNowMs()+3000;
        do{reaped=waitpid(child,&status,WNOHANG);if(reaped)break;sleep();}while(workerControlNowMs()<until);
        CHECK(reaped==child);CHECK(process.poll().state==S::Unreaped&&!process.terminate(0)&&!process.reaped());
        WorkerControlProcess second;CHECK(!second.start(exe,{"--probe","normal"},command(),cancel));}
    WorkerControlProcess replacement;
    CHECK(!replacement.start(exe,{"--probe","normal"},command(),cancel));
    return finishTests("unknown worker reaping quarantine");
}
#endif
}
int main(int argc,char **argv){
    if(argc>1&&std::string(argv[1])=="--probe")return probe(argc,argv);
    const auto exe=std::filesystem::absolute(std::filesystem::u8path(argv[0]));
    if(argc==2&&std::string(argv[1])=="--parent-death")return parentDeath(exe);
#if !defined(_WIN32)
    if(argc==2&&std::string(argv[1])=="--stolen-reap")return stolenReap(exe);
#endif
    if(argc!=2&&argc!=4)return 96;
    normal(exe);refusals(exe);backpressure(exe);identity(std::filesystem::absolute(std::filesystem::u8path(argv[1])));
    backgroundOwner(exe);
    if(argc==4)nativeEntries(std::filesystem::absolute(std::filesystem::u8path(argv[2])),std::filesystem::absolute(std::filesystem::u8path(argv[3])));
    return finishTests("owned worker IPC and identity");
}
