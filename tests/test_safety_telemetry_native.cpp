// Production adapter compiled against synthetic symbols only. The test cannot load NVML/CUDA or address real devices.
#include "product/safety_telemetry.h"
#include "product_test_util.h"
#include <cstring>
#include <thread>
using namespace om;
namespace {
using S=SafetyTelemetryStatus;
std::string missing,failed;int error=0,shutdownError=0;unsigned initialized=0,closed=0,lookups=0,uuidCalls=0,target=0;
bool mismatch=false,unterminated=false,substitute=false,badVersion=false,slow=false,unknown=false,secondFailure=false;
bool slowInit=false,slowShutdown=false,slowSymbol=false,missingTemperature=false,badSensor=false,nullDevice=false,abi=true;
unsigned long long supportedMask=511;
int degrees=0;
const char *id1="GPU-00112233-4455-6677-8899-aabbccddee01",*id2="GPU-00112233-4455-6677-8899-aabbccddee02";
struct Temp {unsigned version;int sensorType,temperature;};
int rc(const char *name){return failed==name?error:0;}
int init(){++initialized;if(slowInit)std::this_thread::sleep_for(std::chrono::milliseconds(40));return rc("init");}
int shutdown(){++closed;if(slowShutdown)std::this_thread::sleep_for(std::chrono::milliseconds(40));return shutdownError?shutdownError:rc("shutdown");}
int handle(const char *uuid,void **device){++lookups;target=std::strcmp(uuid,id1)?2:1;*device=nullDevice?nullptr:reinterpret_cast<void *>(static_cast<uintptr_t>(target));return rc("handle");}
int uuid(void *,char *out,unsigned n){++uuidCalls;
    if(unterminated)std::memset(out,'x',n);
    else std::snprintf(out,n,"%s",mismatch||(substitute&&uuidCalls%2==0)?id2:target==1?id1:id2);
    return rc("uuid");}
int temperature(void *,Temp *out){abi=abi&&out->version==(12u|(1u<<24))&&out->sensorType==0;
    if(slow)std::this_thread::sleep_for(std::chrono::milliseconds(40));
    if(!missingTemperature)out->temperature=degrees;
    if(badVersion)++out->version;
    if(badSensor)++out->sensorType;
    return secondFailure&&target==2?15:rc("temperature");}
int active(void *,unsigned long long *out){*out=unknown?512:0;return rc("active");}
int supported(void *,unsigned long long *out){*out=supportedMask;return rc("supported");}
void reset(){missing=failed="";error=shutdownError=0;initialized=closed=lookups=uuidCalls=target=0;
    mismatch=unterminated=substitute=badVersion=slow=unknown=secondFailure=false;
    slowInit=slowShutdown=slowSymbol=missingTemperature=badSensor=nullDevice=false;abi=true;supportedMask=511;degrees=0;}
SafetyTelemetryRequest request(){return {true,7,1,safetyTelemetryTime(),500,{id1}};}
}
namespace om {
void *safetyTelemetryTestSymbol(const char *name){
    if(slowSymbol){slowSymbol=false;std::this_thread::sleep_for(std::chrono::milliseconds(40));}
    if(missing==name)return nullptr;
    if(!std::strcmp(name,"nvmlInit_v2"))return reinterpret_cast<void *>(&init);
    if(!std::strcmp(name,"nvmlShutdown"))return reinterpret_cast<void *>(&shutdown);
    if(!std::strcmp(name,"nvmlDeviceGetHandleByUUID"))return reinterpret_cast<void *>(&handle);
    if(!std::strcmp(name,"nvmlDeviceGetUUID"))return reinterpret_cast<void *>(&uuid);
    if(!std::strcmp(name,"nvmlDeviceGetTemperatureV"))return reinterpret_cast<void *>(&temperature);
    if(!std::strcmp(name,"nvmlDeviceGetCurrentClocksEventReasons"))return reinterpret_cast<void *>(&active);
    if(!std::strcmp(name,"nvmlDeviceGetSupportedClocksEventReasons"))return reinterpret_cast<void *>(&supported);
    std::abort(); // any unexpected symbol, particularly a setter or device enumeration, is a test failure
}
}
int main(){
    reset();auto r=request();r.enabled=false;auto v=collectSafetyTelemetry(r);CHECK(v.status==S::Disabled&&initialized==0&&v.readings.empty());
    r.enabled=true;r.devices.clear();v=collectSafetyTelemetry(r);CHECK(v.status==S::Invalid&&initialized==0);
    for(const char *symbol:{"nvmlInit_v2","nvmlShutdown","nvmlDeviceGetHandleByUUID","nvmlDeviceGetUUID","nvmlDeviceGetTemperatureV","nvmlDeviceGetCurrentClocksEventReasons","nvmlDeviceGetSupportedClocksEventReasons"}){
        reset();missing=symbol;v=collectSafetyTelemetry(request());CHECK(v.status==S::Unsupported&&initialized==0&&closed==0&&v.readings.empty());}
    for(const char *call:{"init","handle","uuid","temperature","active","supported","shutdown"})for(int code:{3,4,15,16,999}){
        reset();failed=call;error=code;v=collectSafetyTelemetry(request());CHECK(v.status==(code==3?S::Unsupported:code==15||code==16?S::DeviceFault:S::Unavailable)&&v.readings.empty());
        CHECK(initialized==1&&closed==(failed=="init"?0u:1u));}
    for(int temperatureC:{-273,0,40,250}){reset();degrees=temperatureC;r=request();v=collectSafetyTelemetry(r);
        CHECK(v.status==S::Ok&&v.readings.size()==1&&v.readings[0].temperatureC==temperatureC&&v.began.monotonicMs>=r.issued.monotonicMs);
        CHECK(initialized==1&&closed==1&&lookups==1&&uuidCalls==2&&abi);}
    for(unsigned mode=0;mode<6;++mode){reset();mismatch=mode==0;unterminated=mode==1;substitute=mode==2;badVersion=mode==3;unknown=mode==4;if(mode==5)degrees=251;
        v=collectSafetyTelemetry(request());CHECK(v.status==(mode<3?S::Identity:S::Invalid)&&v.readings.empty()&&closed==1);}
    reset();r=request();r.devices.push_back(id2);secondFailure=true;v=collectSafetyTelemetry(r);CHECK(v.status==S::DeviceFault&&v.readings.empty()&&lookups==2&&closed==1);
    reset();r=request();r.collectionBudgetMs=25;slow=true;v=collectSafetyTelemetry(r);CHECK(v.status==S::Clock&&v.readings.empty()&&closed==1);
    CHECK(v.finished.monotonicMs-v.began.monotonicMs>=25);
    reset();r=request();r.issued.utcMs+=10000;v=collectSafetyTelemetry(r);CHECK(v.status==S::Clock&&initialized==0&&v.began.utcMs<r.issued.utcMs);
    std::string wire;SafetyTelemetryReply decoded;CHECK(encodeSafetyTelemetryReply(r,v,&wire)&&decodeSafetyTelemetryReply(r,wire,&decoded)&&decoded.began.utcMs==v.began.utcMs);
    for(unsigned mode=0;mode<3;++mode){reset();missingTemperature=mode==0;badSensor=mode==1;nullDevice=mode==2;
        v=collectSafetyTelemetry(request());CHECK(v.status==(mode==2?S::Identity:S::Invalid)&&v.readings.empty()&&closed==1);}
    reset();failed="temperature";error=3;shutdownError=15;v=collectSafetyTelemetry(request());CHECK(v.status==S::DeviceFault&&v.readings.empty()&&closed==1);
    reset();failed="temperature";error=15;shutdownError=4;v=collectSafetyTelemetry(request());CHECK(v.status==S::DeviceFault&&v.readings.empty()&&closed==1);
    for(unsigned mode=0;mode<3;++mode){reset();slowInit=mode==0;slowShutdown=mode==1;slowSymbol=mode==2;r=request();r.collectionBudgetMs=25;
        v=collectSafetyTelemetry(r);CHECK(v.status==S::Clock&&v.readings.empty()&&closed==1&&v.finished.monotonicMs-v.began.monotonicMs>=25);}
    reset();failed="temperature";error=4;slowShutdown=true;r=request();r.collectionBudgetMs=25;v=collectSafetyTelemetry(r);
    CHECK(v.status==S::Clock&&v.readings.empty()&&closed==1&&v.finished.monotonicMs-v.began.monotonicMs>=25);
    reset();failed="temperature";error=15;slowShutdown=true;r=request();r.collectionBudgetMs=25;v=collectSafetyTelemetry(r);
    CHECK(v.status==S::DeviceFault&&v.readings.empty()&&closed==1&&v.finished.monotonicMs-v.began.monotonicMs>=25);
    reset();unknown=true;supportedMask=UINT64_MAX;v=collectSafetyTelemetry(request());CHECK(v.status==S::Ok&&v.readings[0].activeClockEvents==512&&v.readings[0].supportedClockEvents==UINT64_MAX);
    reset();supportedMask=0;v=collectSafetyTelemetry(request());CHECK(v.status==S::Ok&&v.readings[0].supportedClockEvents==0);
    reset();r=request();r.devices.push_back(id2);v=collectSafetyTelemetry(r);CHECK(v.status==S::Ok&&v.readings.size()==2&&lookups==2&&uuidCalls==4&&closed==1);
    return finishTests("native safety telemetry ABI");
}
