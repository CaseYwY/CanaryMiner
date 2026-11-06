// Statically linked actual owners must respect the caller's frozen epoch even after a foreign fault clears.
#include "product/native_process_admission.h"
#include "product/local_task_process.h"
#include "product/public_json_process.h"
#include "product/duplex_task_process.h"
#include "product/worker_process.h"
#include "product/legacy_child_process.h"
#include "product_test_util.h"
#include <chrono>
#include <cstdio>
#include <thread>
#if defined(OM_LEGACY_PIDFD_SPAWN)
extern "C" int __real_pidfd_spawn(int *,const char *,const posix_spawn_file_actions_t *,const posix_spawnattr_t *,char *const[],char *const[]);
namespace {
unsigned spawns=0;std::filesystem::path helper;std::atomic<bool> cancel{false};
void attempt(const std::string &kind,bool denied){
    if(kind=="local"){
        om::LocalTaskProcess client(helper);const auto result=client.run("SYNTHETIC\n",cancel,600);
        CHECK(result.childStarted!=denied);
        if(denied)CHECK(result.response.empty()&&result.status==om::LocalTaskStatus::Unreaped);
        else CHECK(result.status==om::LocalTaskStatus::Ok&&result.response=="SYNTHETIC\n");
    }else if(kind=="public"){
        om::PublicJsonProcessClient client(helper);const auto result=client.get({om::PublicFeed::GatePearlCandles,60,120},cancel,{300,600,8192,2048});
        if(denied)CHECK(result.body.empty()&&result.status==om::PublicHttpsStatus::Unreaped);
        else CHECK(result.status==om::PublicHttpsStatus::Ok&&!result.body.empty());
    }else if(kind=="duplex"){
        om::DuplexTaskProcess client;CHECK(client.start(helper,cancel)!=denied);CHECK(client.terminate(500));
    }else if(kind=="worker"){
        om::WorkerControlProcess client;const auto now=om::workerControlNowMs();
        om::WorkerCommand command{{"11111111111111111111111111111111","22222222222222222222222222222222",1,1,0},1,now,now+3000,om::WorkerDirective::Pause};
        CHECK(client.start(helper,{},command,cancel)!=denied);CHECK(client.terminate(500));
    }else{
        CHECK(kind=="legacy");om::ChildProcess client;std::string error;int code=-1;
        CHECK(client.start(helper.string(),{"--scoped-child"},[](const std::string &){},&error)!=denied);
        CHECK(client.started()!=denied);if(!denied)CHECK(client.waitFor(3000,&code)&&code==0);
    }
}
}
extern "C" int __wrap_pidfd_spawn(int *fd,const char *path,const posix_spawn_file_actions_t *actions,const posix_spawnattr_t *attributes,char *const argv[],char *const env[]){
    const auto result=__real_pidfd_spawn(fd,path,actions,attributes,argv,env);if(!result)++spawns;return result;
}
int main(int argc,char **argv){
    if(argc==2&&std::string(argv[1])=="--scoped-child")return 0;
    if(argc==2&&std::string(argv[1])=="--local-task-v1"){
        std::string request;if(!om::readLocalTaskInput(argc,argv,&request)||request!="SYNTHETIC\n")return 2;
        return om::writeLocalTaskOutput(request)?0:2;
    }
    if(argc==8){om::PublicJsonResponse result;result.status=om::PublicHttpsStatus::Ok;result.httpStatus=200;result.observedUtc=1;result.body="{\"synthetic\":true}";
        std::string wire;if(!om::encodePublicJsonResponse(result,&wire))return 2;return std::fwrite(wire.data(),1,wire.size(),stdout)==wire.size()?0:2;}
    if(argc==5&&(std::string(argv[1])=="--worker-control-v1"||std::string(argv[1])=="--local-stream-v1")){
        std::this_thread::sleep_for(std::chrono::milliseconds(60));return 0;
    }
    if(argc!=2)return 4;
    const std::string kind=argv[1];if(kind!="local"&&kind!="public"&&kind!="duplex"&&kind!="worker"&&kind!="legacy")return 4;
    helper=std::filesystem::absolute(argv[0]);const auto original=om::nativeProcessEpoch();om::NativeProcessAdmission other;
    CHECK(other.begin());other.markUnresolved();other.settled();CHECK(!om::nativeProcessUnresolved());
    {om::NativeProcessScope scope(original);attempt(kind,true);CHECK(spawns==0);}
    CHECK(!om::nativeProcessUnresolved());attempt(kind,false);CHECK(spawns==1&&!om::nativeProcessUnresolved());
    return finishTests("five-owner original scoped native admission (owned CPU processes only)");
}
#else
int main(){std::puts("SKIP: scoped native-owner fixture requires glibc >= 2.40");return 77;}
#endif
