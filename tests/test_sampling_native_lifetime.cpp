// Fixed CPU display readers; the unavailable-NVML shim makes even accidental native fallback unable to load a driver.
#include "cpu_telemetry_fixture.h"
#include "product/supervisor_sampling.h"
#include "product/local_task_process.h"
#include "product/native_process_admission.h"
#include "product_test_util.h"
#include <cerrno>
#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <spawn.h>
#if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2,40)
#define OM_TEST_SAMPLING_NATIVE_QUEUE 1
#endif
#endif
#endif
namespace {
using namespace om;
std::atomic<bool> armed{false},releaseThread{false},returned{false},faultAdmitted{false},destroyed{false},helperOk{false};
std::atomic<unsigned> factories{0},reads{0},spawns{0};
void fault(){NativeProcessAdmission other;const bool accepted=other.begin();faultAdmitted=accepted;if(accepted){other.markUnresolved();other.settled();}}
template<class P>bool until(P p){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(4);
    while(!p()&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return p();}
class Reader final:public DisplaySampleReader {
    bool fail_;
   public:
    explicit Reader(bool fail):fail_(fail){}
    ~Reader()override{destroyed=true;}
    DisplaySampleReading read(size_t,const std::atomic<bool> &)override{++reads;if(fail_)fault();DisplaySampleReading value;
        value.statsObserved=displaySampleTime();value.accepted=5;return value;}
};
#if defined(OM_TEST_SAMPLING_NATIVE_QUEUE)
void *(*entry)(void *)=nullptr;void *argument=nullptr;
void *gated(void *){while(!releaseThread.load())sched_yield();void *result=entry(argument);returned=true;return result;}
#endif
}
#if defined(OM_TEST_SAMPLING_NATIVE_QUEUE)
extern "C" int pthread_create(pthread_t *thread,const pthread_attr_t *attributes,void *(*function)(void *),void *arg)noexcept{
    using Create=int (*)(pthread_t *,const pthread_attr_t *,void *(*)(void *),void *);
    static const auto real=reinterpret_cast<Create>(dlsym(RTLD_NEXT,"pthread_create"));if(!real)return EAGAIN;
    if(armed.exchange(false)){entry=function;argument=arg;return real(thread,attributes,gated,nullptr);}return real(thread,attributes,function,arg);
}
extern "C" int __real_pidfd_spawn(int *,const char *,const posix_spawn_file_actions_t *,const posix_spawnattr_t *,char *const[],char *const[]);
extern "C" int __wrap_pidfd_spawn(int *fd,const char *path,const posix_spawn_file_actions_t *actions,const posix_spawnattr_t *attributes,char *const argv[],char *const env[]){
    const auto result=__real_pidfd_spawn(fd,path,actions,attributes,argv,env);if(!result)++spawns;return result;
}
#endif
int main(int argc,char **argv){
    if(argc>=2&&std::string(argv[1])=="--local-task-v1"){std::string input;if(!readLocalTaskInput(argc,argv,&input)||input!="SYNTHETIC\n")return 2;
        return writeLocalTaskOutput("SYNTHETIC SAMPLING FIXTURE ONLY\n")?0:3;}
    if(argc!=2)return 4;
    const std::string action=argv[1];const bool queued=action=="queue-fault"||action=="queue-ordinary",foreign=action=="foreign";
    if(action!="scope"&&action!="ordinary"&&action!="factory-fault"&&action!="read-fault"&&!queued&&!foreign)return 4;
#if !defined(OM_TEST_SAMPLING_NATIVE_QUEUE)
    if(queued){std::puts("SKIP: native sampling queue fixture requires glibc >= 2.40");return 77;}
#endif
    const auto original=nativeProcessEpoch();if(action=="scope"||foreign)fault();
    const auto exe=std::filesystem::absolute(argv[0]);DisplaySampleFactory factory=[&](const std::vector<DisplaySampleTarget> &){++factories;
        if(action=="factory-fault")fault();
        if(queued){LocalTaskProcess process(exe);std::atomic<bool> cancel{false};const auto result=process.run("SYNTHETIC\n",cancel,1000);
            helperOk=result.status==LocalTaskStatus::Ok&&result.response=="SYNTHETIC SAMPLING FIXTURE ONLY\n";}
        return std::make_unique<Reader>(action=="read-fault");};
    SupervisorSampling owner;
    if(action=="scope"){{NativeProcessScope old(original);CHECK(!owner.start({{"fixture-never-device",1}},factory));}owner.stop();
        SupervisorSampling fresh;CHECK(fresh.start({{"fixture-never-device",1}},factory));fresh.stop();CHECK(factories==0&&reads==0);}
    else{
        armed=queued;CHECK(owner.start({{"fixture-never-device",1}},factory));CHECK(owner.request({7}));
        if(queued){CHECK(!armed&&factories==0&&spawns==0);if(action=="queue-fault")fault();releaseThread=true;CHECK(until([&]{return factories>0||returned.load();}));}
        if(action=="factory-fault")CHECK(until([&]{return reads>0||destroyed.load();}));
        std::shared_ptr<const DisplaySampleBatch> result;
        if(foreign){CHECK(until([&]{return reads>0;}));bool taken=false;const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(150);
            {NativeProcessScope old(original);do{if(owner.take()){taken=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(std::chrono::steady_clock::now()<end);}
            CHECK(!taken);}
        CHECK(until([&]{if(!result)result=owner.take();return result||owner.faulted()||!owner.pending();}));
        const bool failed=action=="factory-fault"||action=="read-fault"||action=="queue-fault";
        if(failed){CHECK(faultAdmitted&&!result&&owner.faulted()&&!owner.pending());CHECK(!owner.request({7}));
            if(action=="factory-fault"||action=="queue-fault")CHECK(reads==0);
            if(action=="queue-fault")CHECK(factories==0&&spawns==0);}
        else{CHECK(result&&result->generations==std::vector<uint64_t>{7}&&result->readings.size()==1&&result->readings[0].accepted==5&&!owner.faulted());
            if(queued)CHECK(helperOk&&spawns==1);}
        owner.stop();
    }
    CHECK(!nativeProcessUnresolved());return finishTests("display sampling original scope, callback return and queued native factory (CPU unavailable-NVML fixture)");
}
