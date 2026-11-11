// Actual ordinary supervisor and self-owned CPU child, with unavailable telemetry and no stats server.
#include "cpu_telemetry_fixture.h"
#include "product/supervisor.h"
#include "product_test_util.h"
#if defined(OM_LEGACY_PIDFD_SPAWN)
extern "C" int __real_pidfd_spawn(int *,const char *,const posix_spawn_file_actions_t *,const posix_spawnattr_t *,char *const[],char *const[]);
extern "C" int __real_waitid(idtype_t,id_t,siginfo_t *,int);
namespace {
unsigned spawns=0,numericSignals=0,lateProbes=0;std::atomic<int> owned{-1}; // helper waits run on another thread
bool stolen=false,error=false,lost=false,cancelCreated=false,faultCreated=false;
volatile sig_atomic_t stop=0;
om::NativeProcessAdmission foreign;
class Reader final:public om::DisplaySampleReader {public:om::DisplaySampleReading read(size_t,const std::atomic<bool> &)override{return {};}};
}
extern "C" int __wrap_pidfd_spawn(int *fd,const char *path,const posix_spawn_file_actions_t *actions,const posix_spawnattr_t *attributes,char *const argv[],char *const env[]){
    const auto result=__real_pidfd_spawn(fd,path,actions,attributes,argv,env);
    // The console/prefs helpers now share pidfd_spawn. Inject only at the actual ordinary-worker boundary.
    if(!result&&argv[1]&&std::string(argv[1])=="--ordinary-supervisor-cpu-child"){owned=*fd;++spawns;if(cancelCreated)stop=1;if(faultCreated){CHECK(foreign.begin());foreign.markUnresolved();foreign.settled();}}
    return result;
}
extern "C" int __wrap_waitid(idtype_t type,id_t id,siginfo_t *status,int flags){
    const int worker=owned.load();
    if(type!=P_PIDFD||worker<0||id!=static_cast<id_t>(worker))return __real_waitid(type,id,status,flags);
    if(lost){++lateProbes;errno=ECHILD;return -1;}
    if(error){lost=true;errno=EINVAL;return -1;}
    const auto result=__real_waitid(type,id,status,flags);
    if(!result&&status->si_pid&&stolen){lost=true;errno=ECHILD;return -1;}
    return result;
}
extern "C" int __wrap_kill(pid_t,int){++numericSignals;errno=ESRCH;return -1;}
int main(int argc,char **argv){
    if(argc>=2&&std::string(argv[1])=="--ordinary-supervisor-cpu-child"){std::this_thread::sleep_for(std::chrono::milliseconds(60));return 0;}
    CHECK(argc==2);if(argc!=2)return finishTests("ordinary supervisor ownership");
    const std::string mode=argv[1];stolen=mode=="stolen";error=mode=="error";cancelCreated=mode=="cancel-created";faultCreated=mode=="fault-created";
    CHECK(stolen||error||cancelCreated||faultCreated);
    const auto began=om::supervisorNowMs();
    {
        om::SupervisorOptions options;options.exe=std::filesystem::absolute(argv[0]).string();options.userArgs={"--ordinary-supervisor-cpu-child"};
        options.apiEnabled=false;options.noEnergy=options.noMarket=true;options.stop=&stop;options.drainMs=0;
        om::WorkerSeed seed;seed.index=0;seed.uuid="SYNTHETIC-CPU-NOT-DISCOVERED";seed.name="Owned CPU fixture";options.seeds={seed};
        options.displaySampleFactory=[](const std::vector<om::DisplaySampleTarget> &)->std::unique_ptr<om::DisplaySampleReader>{return std::make_unique<Reader>();};
        om::Supervisor supervisor(std::move(options));CHECK(supervisor.run()==(cancelCreated?om::kExitOk:om::kExitFaultLatched));
    }
    CHECK(spawns==1&&numericSignals==0&&lateProbes==0);
    CHECK(om::nativeProcessUnresolved()==(stolen||error));
    CHECK_MSG("synthetic native faults must not leave the ordinary output reader waiting for EOF",om::supervisorNowMs()-began<12000);
    if(error&&owned>=0){siginfo_t status{};CHECK(__real_waitid(P_PIDFD,static_cast<id_t>(owned.load()),&status,WEXITED)==0);}
    return finishTests("actual ordinary supervisor native uncertainty/creation-stop/final fault (CPU only)");
}
#else
int main(){std::puts("SKIP: ordinary supervisor native ownership requires glibc >= 2.40");return 77;}
#endif
