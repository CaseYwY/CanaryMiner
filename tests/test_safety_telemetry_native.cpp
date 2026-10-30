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
bool missingActive=false;
unsigned long long supportedMask=511;
int degrees=0;
unsigned fieldCalls=0,fieldIndex=0,fieldType=1;int fieldError=0,fieldMode=0;
int64_t fieldValue=0;bool overrideValue=false,fieldFaultAfterFailure=false,secondFieldFailure=false;
const char *id1="GPU-00112233-4455-6677-8899-aabbccddee01",*id2="GPU-00112233-4455-6677-8899-aabbccddee02";
struct Temp {unsigned version;int sensorType,temperature;};
union Value {double d;int si;unsigned ui;unsigned long ul;unsigned long long ull;long long sll;unsigned short us;};
struct Field {unsigned id,scope;long long timestamp,latency;int type,status;Value value;};
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
int active(void *,unsigned long long *out){if(!missingActive)*out=unknown?512:0;return rc("active");}
int supported(void *,unsigned long long *out){*out=supportedMask;return rc("supported");}
int fields(void *,int count,Field *out){
    ++fieldCalls;const unsigned ids[]={82,193,194,195,196,230};abi=abi&&sizeof(Field)==40&&count==6;
    if(count!=6)std::abort();
    const auto stamp=safetyTelemetryTime().utcMs*1000;
    for(unsigned i=0;i<6;++i){auto &f=out[i];abi=abi&&f.id==ids[i]&&f.scope==0&&f.status==-1&&f.type==-1&&f.timestamp==-1&&f.latency==-1;
        if(fieldMode==1&&i==fieldIndex)continue; // success without writing a field must never manufacture zero
        f.timestamp=stamp;f.latency=100;f.type=1;f.status=0;
        if(fieldMode==10&&i==fieldIndex)continue; // metadata alone must not create a zero temperature/recovery value
        f.value.ui=i==5?0:50;
        if(i==fieldIndex){f.status=fieldError;const auto value=overrideValue?fieldValue:static_cast<int64_t>(f.value.ui);f.type=static_cast<int>(fieldType);
            switch(fieldType){case 0:f.value.d=static_cast<double>(value);break;case 1:f.value.ui=static_cast<unsigned>(value);break;
                case 2:f.value.ul=static_cast<unsigned long>(value);break;case 3:f.value.ull=static_cast<unsigned long long>(value);break;
                case 4:f.value.sll=value;break;case 5:f.value.si=static_cast<int>(value);break;case 6:f.value.us=static_cast<unsigned short>(value);break;default:break;}
            if(fieldMode==2)++f.id;
            if(fieldMode==3)f.scope=1;
            if(fieldMode==4)f.timestamp=0;
            if(fieldMode==5)f.timestamp-=1000000;
            if(fieldMode==6)f.timestamp+=1000000;
            if(fieldMode==7)f.latency=-1;
            if(fieldMode==8)f.latency=1000001;
            if(fieldMode==9)f.value.ull=UINT64_MAX;
        }
    }
    if(fieldFaultAfterFailure){out[0].status=3;out[5].value.ui=1;}
    return secondFieldFailure&&target==2?15:rc("fields");
}
void reset(){missing=failed="";error=shutdownError=0;initialized=closed=lookups=uuidCalls=target=0;
    mismatch=unterminated=substitute=badVersion=slow=unknown=secondFailure=false;
    slowInit=slowShutdown=slowSymbol=missingTemperature=badSensor=nullDevice=missingActive=false;abi=true;supportedMask=511;degrees=0;
    fieldCalls=fieldIndex=0;fieldType=1;fieldError=fieldMode=0;fieldValue=0;overrideValue=fieldFaultAfterFailure=secondFieldFailure=false;}
SafetyTelemetryRequest request(){return {true,7,1,safetyTelemetryTime(),500,{id1}};}
SafetyTelemetryRequest extended(){auto r=request();r.profile=SafetyTelemetryProfile::ExtendedFields;return r;}
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
    if(!std::strcmp(name,"nvmlDeviceGetFieldValues"))return reinterpret_cast<void *>(&fields);
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
    reset();missingActive=true;v=collectSafetyTelemetry(request());CHECK(v.status==S::Invalid&&v.readings.empty());
    reset();r=request();r.devices.push_back(id2);v=collectSafetyTelemetry(r);CHECK(v.status==S::Ok&&v.readings.size()==2&&lookups==2&&uuidCalls==4&&closed==1);
    reset();v=collectSafetyTelemetry(extended());CHECK(v.status==S::Ok&&abi&&fieldCalls==1&&uuidCalls==2&&closed==1&&v.readings.size()==1);
    CHECK(v.readings[0].fields.size()==6&&v.readings[0].fields[0].id==82&&v.readings[0].fields[0].value==50&&v.readings[0].fields[5].value==0);
    reset();missing="nvmlDeviceGetFieldValues";v=collectSafetyTelemetry(extended());CHECK(v.status==S::Unsupported&&initialized==0);
    v=collectSafetyTelemetry(request());CHECK(v.status==S::Ok&&fieldCalls==0&&v.readings[0].fields.empty());
    reset();r=extended();r.enabled=false;v=collectSafetyTelemetry(r);CHECK(v.status==S::Disabled&&initialized==0&&fieldCalls==0);
    r.enabled=true;r.profile=static_cast<SafetyTelemetryProfile>(2);CHECK(collectSafetyTelemetry(r).status==S::Invalid&&initialized==0);
    for(int code:{3,4,15,16,999}){
        reset();failed="fields";error=code;v=collectSafetyTelemetry(extended());CHECK(v.status==(code==3?S::Unsupported:code==15||code==16?S::DeviceFault:S::Unavailable)&&v.readings.empty()&&closed==1);
        for(unsigned i=0;i<6;++i){reset();fieldIndex=i;fieldError=code;v=collectSafetyTelemetry(extended());
            CHECK(v.status==(code==3?S::Unsupported:code==15||code==16?S::DeviceFault:S::Unavailable)&&v.readings.empty()&&closed==1);}
    }
    for(unsigned i=0;i<6;++i)for(int mode=1;mode<=8;++mode){reset();fieldIndex=i;fieldMode=mode;v=collectSafetyTelemetry(extended());
        CHECK(v.status==(mode==1||mode==5?S::Unavailable:mode==6?S::Clock:S::Invalid)&&v.readings.empty()&&closed==1);}
    for(unsigned type:{0,1,2,3,4,5,6,7}){reset();fieldType=type;v=collectSafetyTelemetry(extended());
        CHECK(v.status==(type==0||type==7?S::Invalid:S::Ok));
        if(v.status==S::Ok)CHECK(v.readings[0].fields[0].valueType==type&&v.readings[0].fields[0].value==50);}
    for(int64_t value:{-273,0,250,-274,251}){reset();fieldType=4;overrideValue=true;fieldValue=value;v=collectSafetyTelemetry(extended());
        CHECK(v.status==(value< -273||value>250?S::Invalid:S::Ok));}
    reset();fieldIndex=1;fieldType=5;overrideValue=true;fieldValue=-17;v=collectSafetyTelemetry(extended());
    CHECK(v.status==S::Ok&&v.readings[0].fields[1].value==-17); // raw T.Limit is not treated as absolute core temperature
    reset();fieldType=3;fieldMode=9;v=collectSafetyTelemetry(extended());CHECK(v.status==S::Invalid&&v.readings.empty());
    for(int64_t action:{1,2,3,4,5,6,7,255,-1}){reset();fieldIndex=5;fieldType=4;overrideValue=true;fieldValue=action;v=collectSafetyTelemetry(extended());
        CHECK(v.status==S::DeviceFault&&v.readings.empty()&&closed==1);}
    reset();fieldFaultAfterFailure=true;v=collectSafetyTelemetry(extended());CHECK(v.status==S::DeviceFault&&v.readings.empty());
    for(unsigned index:{0,5}){reset();fieldMode=10;fieldIndex=index;v=collectSafetyTelemetry(extended());
        CHECK(v.status==(index==0?S::Invalid:S::DeviceFault)&&v.readings.empty());}
    reset();fieldIndex=0;fieldError=3;shutdownError=16;v=collectSafetyTelemetry(extended());CHECK(v.status==S::DeviceFault&&closed==1);
    reset();substitute=true;v=collectSafetyTelemetry(extended());CHECK(v.status==S::Identity&&fieldCalls==1&&v.readings.empty());
    reset();r=extended();r.devices.push_back(id2);v=collectSafetyTelemetry(r);CHECK(v.status==S::Ok&&abi&&fieldCalls==2&&v.readings.size()==2);
    CHECK(encodeSafetyTelemetryReply(r,v,&wire)&&decodeSafetyTelemetryReply(r,wire,&decoded)&&decoded.readings[1].fields[4].id==196);
    reset();r=extended();r.devices.push_back(id2);secondFieldFailure=true;v=collectSafetyTelemetry(r);CHECK(v.status==S::DeviceFault&&fieldCalls==2&&v.readings.empty()&&closed==1);
    return finishTests("native safety telemetry ABI");
}
