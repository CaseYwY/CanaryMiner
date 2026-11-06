// Actual game fleet's deferred identity thread. No worker launch, game scan, sensor, network or real entropy adapter.
#include "product/game_worker_session.h"
#include "product/worker_identity.h"
#include "product_test_util.h"
#include <cerrno>
#include <chrono>
#include <thread>
#include <dlfcn.h>
#include <pthread.h>
#include <sched.h>
#include <spawn.h>
#if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2,40)
#define OM_TEST_GAME_IDENTITY_NATIVE 1
#endif
#endif
#if defined(OM_TEST_GAME_IDENTITY_NATIVE)
namespace {
using namespace om;
std::atomic<bool> armed{true},releaseThread{false},returned{false};std::atomic<unsigned> spawns{0};
void *(*entry)(void *)=nullptr;void *argument=nullptr;
void *gated(void *){while(!releaseThread.load())sched_yield();void *result=entry(argument);returned=true;return result;}
void fault(){NativeProcessAdmission other;CHECK(other.begin());other.markUnresolved();other.settled();}
int helper(int argc,char **argv){
    std::string input;if(!readLocalTaskInput(argc,argv,&input))return 2;
    if(input=="SYNTHETIC\n")return writeLocalTaskOutput("SYNTHETIC IDENTITY FIXTURE ONLY\n")?0:3;
    uint64_t parent=0;unsigned count=0;if(!workerIdentityRequest(input,&parent,&count)||count!=2||!bindLocalTaskParent(parent))return 2;
    return writeLocalTaskOutput("CMWK1 2\n"+std::string(32,'1')+"\n"+std::string(32,'2')+"\n")?0:3;
}
}
extern "C" int pthread_create(pthread_t *thread,const pthread_attr_t *attributes,void *(*function)(void *),void *arg)noexcept{
    using Create=int (*)(pthread_t *,const pthread_attr_t *,void *(*)(void *),void *);
    static const auto real=reinterpret_cast<Create>(dlsym(RTLD_NEXT,"pthread_create"));if(!real)return EAGAIN;
    if(armed.exchange(false)){entry=function;argument=arg;return real(thread,attributes,gated,nullptr);}return real(thread,attributes,function,arg);
}
extern "C" int __real_pidfd_spawn(int *,const char *,const posix_spawn_file_actions_t *,const posix_spawnattr_t *,char *const[],char *const[]);
extern "C" int __wrap_pidfd_spawn(int *fd,const char *path,const posix_spawn_file_actions_t *actions,const posix_spawnattr_t *attributes,char *const argv[],char *const env[]){
    const auto result=__real_pidfd_spawn(fd,path,actions,attributes,argv,env);if(!result)++spawns;return result;
}
int main(int argc,char **argv){
    if(argc>=2&&std::string(argv[1])=="--local-task-v1")return helper(argc,argv);
    if(argc!=2)return 4;
    const std::string action=argv[1];if(action!="ordinary"&&action!="fault"&&action!="stop")return 4;
    SessionIntent intent;const auto token=intent.explicitStart();CHECK(token.epoch!=0);
    GameGuardPolicy game;game.mode=GameIntent::Off;GameProfitBinding binding;binding.gameKey="fixture.game";GameWorkerPolicy worker;worker.maximumIncarnations=1;
    const auto exe=std::filesystem::absolute(argv[0]);GameWorkerSession owner(exe,exe,{"fixture.game","fixture-unused-game",GameProcessAdapter::LinuxUidNativeImage},{},game,binding,worker,intent,token);
    CHECK(owner.start(1,gameDetectionTime()));CHECK(!armed&&spawns==0);
    if(action=="fault")fault();
    if(action=="stop")owner.requestStop();
    releaseThread=true;const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!returned&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(returned);CHECK(action=="ordinary"?spawns==1:spawns==0);
    owner.tick(gameDetectionTime(),{});CHECK(action=="ordinary"?owner.state()==WorkerFleetState::Active:owner.state()==WorkerFleetState::Stopped);
    owner.stop();CHECK(!nativeProcessUnresolved());return finishTests("game fleet original deferred identity thread (fixed synthetic helper, no workers)");
}
#else
int main(){std::puts("SKIP: game identity lifetime fixture requires glibc >= 2.40");return 77;}
#endif
