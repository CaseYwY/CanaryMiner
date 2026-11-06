// Hold only this fixture's first owner thread before native admission. No production test seam.
#include "product/worker_process_owner.h"
#include "product/native_process_admission.h"
#include "product_test_util.h"
#include <cstring>
#include <dlfcn.h>
#include <cerrno>
#include <pthread.h>
#include <sched.h>
#include <spawn.h>
#include <thread>
#if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2,40)
#define OM_TEST_QUEUED_ADMISSION 1
#endif
#endif
#if defined(OM_TEST_QUEUED_ADMISSION)
namespace {
std::atomic<bool> armed{true},releaseThread{false};std::atomic<unsigned> spawns{0};
void *(*entry)(void *)=nullptr;void *argument=nullptr;
void *gated(void *){while(!releaseThread.load())sched_yield();return entry(argument);}
}
extern "C" int pthread_create(pthread_t *thread,const pthread_attr_t *attributes,void *(*function)(void *),void *arg)noexcept{
    // Executable symbol interposition also reaches shared libstdc++'s thread launch; --wrap alone cannot do that.
    using Create=int (*)(pthread_t *,const pthread_attr_t *,void *(*)(void *),void *);
    static const auto real=reinterpret_cast<Create>(dlsym(RTLD_NEXT,"pthread_create"));
    if(!real)return EAGAIN;
    if(armed.exchange(false)){entry=function;argument=arg;return real(thread,attributes,gated,nullptr);}
    return real(thread,attributes,function,arg);
}
extern "C" int __real_pidfd_spawn(int *,const char *,const posix_spawn_file_actions_t *,const posix_spawnattr_t *,char *const[],char *const[]);
extern "C" int __wrap_pidfd_spawn(int *fd,const char *path,const posix_spawn_file_actions_t *a,const posix_spawnattr_t *s,char *const argv[],char *const env[]){
    const int r=__real_pidfd_spawn(fd,path,a,s,argv,env);if(!r)++spawns;return r;
}
int main(int argc,char **argv){
    if(argc==5&&!std::strcmp(argv[1],"--worker-control-v1"))return 0;
    const auto exe=std::filesystem::absolute(argv[0]);om::WorkerProcessOwner owner;
    const auto now=om::workerControlNowMs();
    om::WorkerCommand initial{{"11111111111111111111111111111111","22222222222222222222222222222222",1,1,0},1,now,now+3000,om::WorkerDirective::Pause};
    CHECK(owner.start(exe,{},initial));CHECK(!armed.load()&&spawns.load()==0);
    om::NativeProcessAdmission foreign;CHECK(foreign.begin());foreign.markUnresolved();foreign.settled();
    releaseThread=true;
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(4);om::WorkerOwnerSnapshot view;
    do{view=owner.snapshot();if(view.done)break;std::this_thread::sleep_for(std::chrono::milliseconds(5));}while(std::chrono::steady_clock::now()<until);
    owner.stop();view=owner.snapshot();
    CHECK(spawns.load()==0);
    CHECK(view.done&&view.revoked&&!view.reaped&&view.state==om::WorkerOwnerState::Faulted&&view.exitCode==8);
    return finishTests("queued native worker original-admission epoch (CPU fixture only)");
}
#else
int main(){std::puts("SKIP: queued native admission fixture requires glibc >= 2.40");return 77;}
#endif
