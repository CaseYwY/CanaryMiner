// Helper-side read-only NVML ABI. No tuning, reset, enumeration or shared display sample is used here.
#include "safety_telemetry.h"
#include "../core/gpu_identity.h"
#include <cstddef>
#include <cstring>
#if !defined(CANARY_SAFETY_TELEMETRY_TESTING)
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif
namespace om {
#if defined(CANARY_SAFETY_TELEMETRY_TESTING)
void *safetyTelemetryTestSymbol(const char *); // linked only in the CPU native-adapter test
#endif
namespace {
using S=SafetyTelemetryStatus;
struct TemperatureV1 {unsigned version;int sensorType,temperature;};
static_assert(sizeof(TemperatureV1)==12&&sizeof(unsigned)==4&&sizeof(int)==4,"NVML temperature v1 ABI");
constexpr unsigned temperatureVersion=sizeof(TemperatureV1)|(1u<<24);
union FieldValue {double d;int si;unsigned ui;unsigned long ul;unsigned long long ull;long long sll;unsigned short us;};
struct Field {unsigned id,scope;long long timestamp,latency;int type,status;FieldValue value;};
static_assert(sizeof(FieldValue)==8&&sizeof(Field)==40&&offsetof(Field,timestamp)==8&&offsetof(Field,latency)==16&&
    offsetof(Field,type)==24&&offsetof(Field,status)==28&&offsetof(Field,value)==32,"NVML field-value ABI");
using Fields=int (*)(void *,int,Field *);
class Library {
   public:
    Library(){
#if defined(CANARY_SAFETY_TELEMETRY_TESTING)
        handle_=this;
#elif defined(_WIN32)
        wchar_t system[32768];const auto n=GetSystemDirectoryW(system,32768);
        if(n&&n<32768){const std::wstring path=std::wstring(system,n)+L"\\nvml.dll";
            handle_=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);}
#else
        handle_=dlopen("libnvidia-ml.so.1",RTLD_NOW|RTLD_LOCAL);
#endif
    }
    ~Library(){
#if !defined(CANARY_SAFETY_TELEMETRY_TESTING)
#if defined(_WIN32)
        if(handle_)FreeLibrary(static_cast<HMODULE>(handle_));
#else
        if(handle_)dlclose(handle_);
#endif
#endif
    }
    void *symbol(const char *name)const{
        if(!handle_)return nullptr;
#if defined(CANARY_SAFETY_TELEMETRY_TESTING)
        return safetyTelemetryTestSymbol(name);
#elif defined(_WIN32)
        return reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(handle_),name));
#else
        return dlsym(handle_,name);
#endif
    }
    bool available()const{return handle_!=nullptr;}
   private:void *handle_=nullptr;
};
S result(int code){return code==15||code==16?S::DeviceFault:code==3?S::Unsupported:S::Unavailable;}
bool terminal(S status){return status==S::DeviceFault||status==S::Identity||status==S::Invalid||status==S::Clock;}
bool within(GameGuardTime began,GameGuardTime end,unsigned budget){
    if(end.utcMs<=0||end.utcMs>253402300799999LL||end.utcMs<began.utcMs||end.monotonicMs<began.monotonicMs)return false;
    const auto wall=static_cast<uint64_t>(end.utcMs-began.utcMs),mono=end.monotonicMs-began.monotonicMs;
    return wall<=budget&&mono<=budget&&(wall>mono?wall-mono:mono-wall)<=100;
}
bool integerValue(const Field &f,int64_t &out){
    switch(f.type){
        case 1:out=f.value.ui;return true;
        case 2:if(f.value.ul>static_cast<unsigned long long>(INT64_MAX))return false;out=static_cast<int64_t>(f.value.ul);return true;
        case 3:if(f.value.ull>static_cast<unsigned long long>(INT64_MAX))return false;out=static_cast<int64_t>(f.value.ull);return true;
        case 4:out=f.value.sll;return true;
        case 5:out=f.value.si;return true;
        case 6:out=f.value.us;return true;
        default:return false;
    }
}
unsigned severity(S s){switch(s){case S::DeviceFault:return 6;case S::Identity:return 5;case S::Invalid:return 4;case S::Clock:return 3;
    case S::Unavailable:return 2;case S::Unsupported:return 1;default:return 0;}}
S collectFields(Fields read,void *device,GameGuardTime began,unsigned budget,std::vector<SafetyTelemetryField> &out){
    std::array<Field,safetyTelemetryFieldIds.size()> values{};
    for(size_t i=0;i<values.size();++i){auto &f=values[i];f.id=safetyTelemetryFieldIds[i];f.timestamp=f.latency=-1;f.type=f.status=-1;f.value.ull=UINT64_MAX;}
    const auto rc=read(device,static_cast<int>(values.size()),values.data());if(rc)return result(rc);
    const auto finished=safetyTelemetryTime();S status=S::Ok;
    auto fail=[&](S error){if(severity(error)>severity(status))status=error;};
    // NVML_SUCCESS means ANY field was populated. Inspect every per-field status even after a failure,
    // so an unsupported temperature cannot hide a later device fault or requested recovery action.
    for(size_t i=0;i<values.size();++i){const auto &f=values[i];
        if(f.status){fail(result(f.status));continue;}
        if(f.id!=safetyTelemetryFieldIds[i]||f.scope){fail(S::Invalid);continue;}
        int64_t value=0;if(!integerValue(f,value)){fail(S::Invalid);continue;}
        if(f.id==230&&value!=0){fail(S::DeviceFault);continue;}
        if((f.id==82&&(value< -273||value>250))||f.timestamp<=0||f.latency<0||f.latency>static_cast<int64_t>(budget)*1000){fail(S::Invalid);continue;}
        SafetyTelemetryField field{f.id,static_cast<unsigned>(f.type),f.timestamp,f.latency,value};
        if(!validSafetyTelemetryField(field,i,began,finished,budget)){
            if(f.timestamp>0&&f.timestamp<began.utcMs*1000)fail(S::Unavailable);
            else if(f.timestamp>finished.utcMs*1000+999)fail(S::Clock);
            else fail(S::Invalid);
            continue;
        }
        out.push_back(field);
    }
    return status;
}
}
SafetyTelemetryReply collectSafetyTelemetry(const SafetyTelemetryRequest &r){
    SafetyTelemetryReply out;out.began=out.finished=r.issued;
    if(!validSafetyTelemetryRequest(r)){out.status=S::Invalid;return out;}
    if(!r.enabled){out.status=S::Disabled;return out;}
    const auto began=safetyTelemetryTime();out.began=out.finished=began;
    if(began.utcMs<r.issued.utcMs||began.utcMs>253402300799999LL||began.monotonicMs<r.issued.monotonicMs){out.status=S::Clock;return out;}
    out.status=[&]()->S{
    Library library;
    if(!library.available())return S::Unavailable;
    using Init=int (*)();using Handle=int (*)(const char *,void **);using Uuid=int (*)(void *,char *,unsigned);
    using Temp=int (*)(void *,TemperatureV1 *);using Events=int (*)(void *,unsigned long long *);
    const auto init=reinterpret_cast<Init>(library.symbol("nvmlInit_v2")),shutdown=reinterpret_cast<Init>(library.symbol("nvmlShutdown"));
    const auto handle=reinterpret_cast<Handle>(library.symbol("nvmlDeviceGetHandleByUUID"));
    const auto uuid=reinterpret_cast<Uuid>(library.symbol("nvmlDeviceGetUUID"));
    const auto temperature=reinterpret_cast<Temp>(library.symbol("nvmlDeviceGetTemperatureV"));
    const auto active=reinterpret_cast<Events>(library.symbol("nvmlDeviceGetCurrentClocksEventReasons"));
    const auto supported=reinterpret_cast<Events>(library.symbol("nvmlDeviceGetSupportedClocksEventReasons"));
    const auto fields=r.profile==SafetyTelemetryProfile::ExtendedFields?reinterpret_cast<Fields>(library.symbol("nvmlDeviceGetFieldValues")):nullptr;
    if(!init||!shutdown||!handle||!uuid||!temperature||!active||!supported||(r.profile==SafetyTelemetryProfile::ExtendedFields&&!fields))return S::Unsupported;
    const auto started=init();if(started)return result(started);
    struct Close {Init shutdown;~Close(){if(shutdown)shutdown();}} close{shutdown};
    auto status=[&]()->S{
    for(const auto &expected:r.devices){
        if(!within(began,safetyTelemetryTime(),r.collectionBudgetMs))return S::Clock;
        void *device=nullptr;int rc=handle(expected.c_str(),&device);if(rc)return result(rc);if(!device)return S::Identity;
        char actual[96]={};rc=uuid(device,actual,sizeof(actual));if(rc)return result(rc);
        if(!std::memchr(actual,'\0',sizeof(actual))||!gpuUuidEqual(expected,actual))return S::Identity;
        TemperatureV1 temp{temperatureVersion,0,-274};rc=temperature(device,&temp);if(rc)return result(rc);
        if(temp.version!=temperatureVersion||temp.sensorType!=0||temp.temperature< -273||temp.temperature>250)return S::Invalid;
        unsigned long long current=UINT64_MAX,capabilities=0;
        rc=supported(device,&capabilities);if(rc)return result(rc);rc=active(device,&current);if(rc)return result(rc);
        if(current&~capabilities)return S::Invalid;
        SafetyTelemetryReading reading{temp.temperature,static_cast<uint64_t>(current),static_cast<uint64_t>(capabilities),{}};
        if(fields){const auto status=collectFields(fields,device,began,r.collectionBudgetMs,reading.fields);if(status!=S::Ok)return status;}
        std::memset(actual,0,sizeof(actual));rc=uuid(device,actual,sizeof(actual));if(rc)return result(rc);
        if(!std::memchr(actual,'\0',sizeof(actual))||!gpuUuidEqual(expected,actual))return S::Identity;
        out.readings.push_back(std::move(reading));
    }
    return S::Ok;
    }();
    // Cleanup is checked even after an earlier failed read. A later device fault must not disappear.
    const auto closed=shutdown();close.shutdown=nullptr;
    if(closed){const auto failure=result(closed);if(status==S::Ok||failure==S::DeviceFault||(!terminal(status)&&terminal(failure)))status=failure;}
    return status;
    }(); // library unload is also inside the measured interval and helper timeout
    out.finished=safetyTelemetryTime();
    if(!terminal(out.status)&&!within(began,out.finished,r.collectionBudgetMs))out.status=S::Clock;
    if(out.status!=S::Ok)out.readings.clear();
    return out;
}
} // namespace om
