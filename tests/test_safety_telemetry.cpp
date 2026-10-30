// Pure wire and real owned CPU helper tests. No driver, GPU, game or production miner.
#include "product/safety_telemetry_owner.h"
#include "product/hash.h"
#include "product_test_util.h"
#include <thread>
using namespace om;
namespace {
using S=SafetyTelemetryStatus;using O=SafetyTelemetryOwnerState;using T=LocalTaskStatus;
std::string device(char mode='1'){return std::string("GPU-00112233-4455-6677-8899-aabbccddee0")+mode;}
SafetyTelemetryRequest request(char mode='1'){return {true,7,1,safetyTelemetryTime(),500,{device(mode)}};}
bool wait(const std::function<bool()> &ready,unsigned ms=5000){const auto until=safetyTelemetryTime().monotonicMs+ms;
    do{if(ready())return true;std::this_thread::sleep_for(std::chrono::milliseconds(5));}while(safetyTelemetryTime().monotonicMs<until);return ready();}
void codec(){
    auto r=request();r.issued={1793145600000LL,1000};std::string wire;SafetyTelemetryRequest copy;uint64_t parent=0;
    CHECK(encodeSafetyTelemetryRequest(r,42,&wire)&&decodeSafetyTelemetryRequest(wire,&copy,&parent)&&parent==42&&copy.devices==r.devices&&copy.issued.monotonicMs==1000);
    for(const auto &mutate:std::vector<std::function<void(SafetyTelemetryRequest &)>>{
        [](auto &v){v.epoch=0;},[](auto &v){v.revision=0;},[](auto &v){v.issued.utcMs=-1;},[](auto &v){v.collectionBudgetMs=24;},[](auto &v){v.collectionBudgetMs=1001;},
        [](auto &v){v.devices.clear();},[](auto &v){v.devices.push_back(v.devices[0]);},[](auto &v){v.devices[0]="GPU-00000000-0000-0000-0000-000000000000";},
        [](auto &v){v.devices[0][0]='g';},[](auto &v){v.devices[0]+='\n';},[](auto &v){v.devices[0]="0";},[](auto &v){v.devices.resize(65,v.devices[0]);}}){auto bad=r;mutate(bad);CHECK(!encodeSafetyTelemetryRequest(bad,42,&wire));}
    CHECK(!encodeSafetyTelemetryRequest(r,0,&wire)&&!encodeSafetyTelemetryRequest(r,UINT64_MAX,&wire));CHECK(encodeSafetyTelemetryRequest(r,42,&wire));
    CHECK(!decodeSafetyTelemetryRequest(wire+"extra\n",&copy,&parent));CHECK(!decodeSafetyTelemetryRequest(wire.substr(0,wire.size()-1),&copy,&parent));
    auto bad=wire;bad.replace(bad.find("42\n"),3,"042\n");CHECK(!decodeSafetyTelemetryRequest(bad,&copy,&parent));
    CHECK(!decodeSafetyTelemetryRequest(std::string(safetyTelemetryWireLimit+1,'x'),&copy,&parent));
    bool truncated=false;for(size_t i=0;i<wire.size();++i)truncated=decodeSafetyTelemetryRequest(wire.substr(0,i),&copy,&parent)||truncated;
    CHECK(!truncated&&copy.epoch==7&&parent==42);
    for(const char *token:{"+42","-42","42.0","4e1","42\r","18446744073709551616"}){
        bad=wire;bad.replace(bad.find("42\n"),2,token);CHECK(!decodeSafetyTelemetryRequest(bad,&copy,&parent));}
    SafetyTelemetryReply reply{S::Ok,{r.issued.utcMs+1,1001},{r.issued.utcMs+11,1011},{{0,0,511}}},decoded;
    CHECK(encodeSafetyTelemetryReply(r,reply,&wire)&&decodeSafetyTelemetryReply(r,wire,&decoded)&&decoded.readings[0].temperatureC==0);
    for(const auto &mutate:std::vector<std::function<void(SafetyTelemetryReply &)>>{
        [](auto &v){v.status=static_cast<S>(-1);},[](auto &v){v.readings.clear();},[](auto &v){v.readings[0].temperatureC=-274;},[](auto &v){v.readings[0].temperatureC=251;},
        [](auto &v){v.readings[0].activeClockEvents=512;},[](auto &v){v.began.monotonicMs=999;},[](auto &v){v.finished.monotonicMs=2000;},
        [](auto &v){v.finished.utcMs+=101;},[](auto &v){v.status=S::Unavailable;}}){auto v=reply;mutate(v);CHECK(!encodeSafetyTelemetryReply(r,v,&wire));}
    CHECK(encodeSafetyTelemetryReply(r,reply,&wire));
    for(const auto &mutate:std::vector<std::function<void(SafetyTelemetryRequest &)>>{
        [](auto &v){++v.epoch;},[](auto &v){++v.revision;},[](auto &v){v.devices[0]=device('2');},[](auto &v){++v.issued.monotonicMs;},[](auto &v){++v.collectionBudgetMs;}}){auto v=r;mutate(v);CHECK(!decodeSafetyTelemetryReply(v,wire,&decoded));}
    CHECK(!decodeSafetyTelemetryReply(r,wire+"\n",&decoded));
    truncated=false;for(size_t i=0;i<wire.size();++i)truncated=decodeSafetyTelemetryReply(r,wire.substr(0,i),&decoded)||truncated;
    CHECK(!truncated&&decoded.readings.size()==1);
    for(auto status:{S::Unavailable,S::Unsupported,S::Identity,S::DeviceFault,S::Clock,S::Invalid}){auto v=reply;v.status=status;v.readings.clear();CHECK(encodeSafetyTelemetryReply(r,v,&wire)&&decodeSafetyTelemetryReply(r,wire,&decoded)&&decoded.readings.empty());}
    auto reversed=reply;reversed.status=S::Clock;reversed.readings.clear();reversed.finished.utcMs=reversed.began.utcMs-100;
    CHECK(encodeSafetyTelemetryReply(r,reversed,&wire)&&decodeSafetyTelemetryReply(r,wire,&decoded)&&decoded.finished.utcMs==reversed.finished.utcMs);
    reversed.readings.push_back({40,0,511});CHECK(!encodeSafetyTelemetryReply(r,reversed,&wire));
    r.devices.clear();reply.readings.clear();
    for(unsigned i=1;i<=64;++i){char suffix[13];std::snprintf(suffix,sizeof(suffix),"%012x",i);r.devices.push_back(std::string("GPU-00112233-4455-6677-8899-")+suffix);reply.readings.push_back({static_cast<int>(i)-32,UINT64_MAX,UINT64_MAX});}
    CHECK(encodeSafetyTelemetryRequest(r,42,&wire)&&decodeSafetyTelemetryRequest(wire,&copy,&parent)&&copy.devices==r.devices);
    CHECK(encodeSafetyTelemetryReply(r,reply,&wire)&&decodeSafetyTelemetryReply(r,wire,&decoded)&&decoded.readings.size()==64);
    std::swap(r.devices[0],r.devices[1]);CHECK(!decodeSafetyTelemetryReply(r,wire,&decoded));
}
void extendedCodec(){
    auto r=request();r.issued={1793145600000LL,1000};r.profile=SafetyTelemetryProfile::ExtendedFields;
    SafetyTelemetryReply reply{S::Ok,{r.issued.utcMs+1,1001},{r.issued.utcMs+11,1011},{{40,0,511}}},decoded;
    const auto stamp=reply.began.utcMs*1000;reply.readings[0].fields={{82,1,stamp,1,50},{193,5,stamp,0,-10},{194,4,stamp,0,0},
        {195,3,stamp,500000,2},{196,2,reply.finished.utcMs*1000+999,0,3},{230,6,stamp,0,0}};
    std::string wire;SafetyTelemetryRequest copy;uint64_t parent=0;
    CHECK(encodeSafetyTelemetryRequest(r,42,&wire)&&wire.find("CMSTQ2\n")==0&&decodeSafetyTelemetryRequest(wire,&copy,&parent)&&copy.profile==r.profile);
    CHECK(encodeSafetyTelemetryReply(r,reply,&wire)&&wire.find("CMSTA2\n")==0&&decodeSafetyTelemetryReply(r,wire,&decoded));
    CHECK(decoded.readings[0].fields[1].value==-10&&decoded.readings[0].fields[4].timestampUsec==reply.finished.utcMs*1000+999);
    CHECK(wire.find("82\n1\n1793145600001000\n1\n50\n193\n5\n1793145600001000\n0\n-10\n")!=std::string::npos);
    for(const auto &change:std::vector<std::function<void(SafetyTelemetryReply &)>>{
        [](auto &v){v.readings[0].fields.clear();},[](auto &v){v.readings[0].fields.pop_back();},[](auto &v){v.readings[0].fields.push_back(v.readings[0].fields[0]);},
        [](auto &v){std::swap(v.readings[0].fields[0],v.readings[0].fields[1]);},[](auto &v){++v.readings[0].fields[0].id;},
        [](auto &v){v.readings[0].fields[0].valueType=0;},[](auto &v){v.readings[0].fields[0].valueType=7;},[](auto &v){v.readings[0].fields[0].value=-1;},
        [](auto &v){v.readings[0].fields[0].value=251;},[](auto &v){v.readings[0].fields[1].value=INT64_MAX;},
        [](auto &v){v.readings[0].fields[2].valueType=1;v.readings[0].fields[2].value=static_cast<int64_t>(UINT32_MAX)+1;},
        [](auto &v){v.readings[0].fields[2].valueType=6;v.readings[0].fields[2].value=65536;},
        [](auto &v){v.readings[0].fields[3].value=-1;},[](auto &v){v.readings[0].fields[4].value=-1;},
        [](auto &v){--v.readings[0].fields[0].timestampUsec;},[](auto &v){++v.readings[0].fields[4].timestampUsec;},
        [](auto &v){v.readings[0].fields[0].latencyUsec=-1;},[](auto &v){v.readings[0].fields[0].latencyUsec=500001;},
        [](auto &v){v.readings[0].fields[5].value=1;},[](auto &v){v.status=S::Unsupported;}}){
        auto bad=reply;change(bad);CHECK(!encodeSafetyTelemetryReply(r,bad,&wire));}
    CHECK(encodeSafetyTelemetryReply(r,reply,&wire));
    for(const char *token:{"-0","+0","00","0.0","9223372036854775808","-9223372036854775809"}){
        auto bad=wire;const auto at=bad.find("\n-10\n");bad.replace(at+1,3,token);CHECK(!decodeSafetyTelemetryReply(r,bad,&decoded));}
    auto legacy=r;legacy.profile=SafetyTelemetryProfile::CoreEvents;CHECK(!decodeSafetyTelemetryReply(legacy,wire,&decoded)&&!encodeSafetyTelemetryReply(legacy,reply,&wire));
    CHECK(encodeSafetyTelemetryReply(r,reply,&wire));auto bad=wire;bad[5]='1';CHECK(!decodeSafetyTelemetryReply(r,bad,&decoded));
    bad=wire;bad.replace(7,64,sha256Hex([&]{std::string old;encodeSafetyTelemetryRequest(legacy,1,&old);return old;}()));
    CHECK(!decodeSafetyTelemetryReply(r,bad,&decoded)); // a v1 request checksum cannot be transplanted into a v2 response
    bool truncated=false;for(size_t i=0;i<wire.size();++i)truncated=decodeSafetyTelemetryReply(r,wire.substr(0,i),&decoded)||truncated;
    CHECK(!truncated&&!decodeSafetyTelemetryReply(r,wire+"0\n",&decoded)&&!decodeSafetyTelemetryReply(r,std::string(safetyTelemetryExtendedWireLimit+1,'x'),&decoded));
    auto invalid=r;invalid.profile=static_cast<SafetyTelemetryProfile>(-1);CHECK(!validSafetyTelemetryRequest(invalid));
    for(auto status:{S::Unavailable,S::Unsupported,S::DeviceFault,S::Clock,S::Invalid}){auto failure=reply;failure.status=status;failure.readings.clear();
        CHECK(encodeSafetyTelemetryReply(r,failure,&wire)&&decodeSafetyTelemetryReply(r,wire,&decoded)&&decoded.readings.empty());}
    r.issued={253402300798000LL,UINT64_MAX-2000};reply.began=r.issued;reply.finished={253402300798500LL,UINT64_MAX-1500};
    auto reading=reply.readings[0];reading.activeClockEvents=reading.supportedClockEvents=UINT64_MAX;
    for(auto &f:reading.fields){f.valueType=4;f.timestampUsec=reply.finished.utcMs*1000+999;f.latencyUsec=500000;
        f.value=f.id==82?250:f.id==230?0:INT64_MIN;}
    r.devices.clear();reply.readings.clear();
    for(unsigned i=1;i<=64;++i){char suffix[13];std::snprintf(suffix,sizeof(suffix),"%012x",i);r.devices.push_back(std::string("GPU-00112233-4455-6677-8899-")+suffix);reply.readings.push_back(reading);}
    CHECK(encodeSafetyTelemetryReply(r,reply,&wire)&&wire.size()>safetyTelemetryWireLimit&&wire.size()<safetyTelemetryExtendedWireLimit);
    CHECK(decodeSafetyTelemetryReply(r,wire,&decoded)&&decoded.readings.size()==64&&decoded.readings.back().fields[4].value==INT64_MIN);
}
void processes(const std::filesystem::path &fixture,const std::filesystem::path &native){
    std::atomic<bool> cancel{false};SafetyTelemetryClient client(fixture);
    for(char mode:{'1','2','6','7','8','9'}){auto r=request(mode);const auto got=client.sample(r,cancel,1000);CHECK(got.transport==T::Ok&&got.reply);
        if(got.reply){CHECK(got.reply->began.utcMs>=r.issued.utcMs&&got.reply->began.monotonicMs>=r.issued.monotonicMs);
            CHECK(got.reply->readings.size()==(mode=='1'||mode=='2'?1u:0u));
            if(mode=='2')CHECK(safetyTelemetryTime().monotonicMs-got.reply->began.monotonicMs>=50);}}
    for(char mode:{'4','5','c'}){const auto got=client.sample(request(mode),cancel,1000);CHECK(got.transport==T::Unavailable&&!got.reply);}
    auto r=request('3');r.collectionBudgetMs=25;const auto start=safetyTelemetryTime();CHECK(client.sample(r,cancel,50).transport==T::Timeout);
    CHECK(safetyTelemetryTime().monotonicMs-start.monotonicMs<1500);cancel=true;CHECK(client.sample(request(),cancel,1000).transport==T::Cancelled);cancel=false;
    CHECK(client.sample(request(),cancel,10).transport==T::Invalid);
    r=request();r.issued.utcMs-=5000;r.issued.monotonicMs-=5000;CHECK(client.sample(r,cancel,1000).transport==T::Invalid);
    r=request();r.issued.utcMs+=5000;CHECK(client.sample(r,cancel,1000).transport==T::Invalid);
    SafetyTelemetryClient missing(std::filesystem::path("missing"));r=request();r.enabled=false;
    const auto disabled=missing.sample(r,cancel,1000);CHECK(disabled.transport==T::Ok&&disabled.reply&&disabled.reply->status==S::Disabled);
    // Exercise the actual native helper only with a disabled request: no runtime loader call is reachable.
    LocalTaskProcess helper(native);std::string wire;CHECK(encodeSafetyTelemetryRequest(r,localTaskProcessId(),&wire));
    const auto off=helper.run(wire,cancel,1000);SafetyTelemetryReply out;
    CHECK(off.status==T::Ok&&decodeSafetyTelemetryReply(r,off.response,&out)&&out.status==S::Disabled);
    CHECK(helper.run("malformed\n",cancel,1000).status==T::Unavailable);
    r=request();r.profile=SafetyTelemetryProfile::ExtendedFields;const auto extended=client.sample(r,cancel,1000);
    CHECK(extended.transport==T::Ok&&extended.reply&&extended.reply->status==S::Ok&&extended.reply->readings[0].fields.size()==6);
    r.enabled=false;CHECK(encodeSafetyTelemetryRequest(r,localTaskProcessId(),&wire));const auto extendedOff=helper.run(wire,cancel,1000);
    CHECK(extendedOff.status==T::Ok&&decodeSafetyTelemetryReply(r,extendedOff.response,&out)&&out.status==S::Disabled);
}
void owners(const std::filesystem::path &fixture){
    SafetyTelemetryPolicy policy;policy.enabled=true;policy.pollMs=100;
    {SafetyTelemetryOwner off(fixture,{device()});CHECK(!off.start(7)&&off.snapshot()->state==O::Disabled);}
    {auto p=policy;p.helperTimeoutMs=20;SafetyTelemetryOwner bad(fixture,{device()},p);CHECK(!bad.start(7)&&bad.snapshot()->state==O::Faulted);}
    {SafetyTelemetryOwner o(fixture,{device()},policy);o.requestStop();CHECK(!o.start(7)&&o.snapshot()->state==O::Stopped);}
    {auto d=std::vector<std::string>{device(),device('2')};SafetyTelemetryOwner o(fixture,d,policy);d.clear();CHECK(o.start(7)&&!o.start(8));
        CHECK(wait([&]{return o.snapshot()->completed>=2;}));const auto before=o.snapshot();CHECK(before->state==O::Ready&&before->request&&before->reply&&before->request->devices.size()==2&&before->reply->readings.size()==2);
        CHECK(before->request->epoch==7&&before->request->revision>=2);o.requestStop();const auto stopped=o.snapshot();CHECK(stopped->state==O::Stopped&&!stopped->reply&&!stopped->pending);
        o.stop();CHECK(!o.start(9)&&o.snapshot()->completed==stopped->completed&&before->reply->readings.size()==2);}
    {SafetyTelemetryOwner o(fixture,{device('a')},policy);CHECK(o.start(7));CHECK(wait([&]{return o.snapshot()->state==O::Ready;}));
        const auto good=o.snapshot();CHECK(wait([&]{return o.snapshot()->completed>=2;}));CHECK(o.snapshot()->state==O::Waiting&&o.snapshot()->reply&&o.snapshot()->reply->status==S::Unavailable&&o.snapshot()->reply->readings.empty());CHECK(good->reply&&good->reply->status==S::Ok);}
    for(char mode:{'6','7','8','9'}){SafetyTelemetryOwner o(fixture,{device(mode)},policy);CHECK(o.start(7)&&wait([&]{return o.snapshot()->completed>=1;}));
        const auto v=o.snapshot();CHECK(v->state==(mode=='8'?O::Waiting:mode=='9'?O::ClockFault:O::Faulted));
        if(mode!='8')CHECK(!v->reply&&!o.start(8));}
    {SafetyTelemetryOwner o(fixture,{device('3')},policy);CHECK(o.start(7)&&wait([&]{return o.snapshot()->pending;}));const auto began=safetyTelemetryTime();
        o.requestStop();CHECK(o.snapshot()->state==O::Stopped&&!o.snapshot()->reply);o.stop();CHECK(safetyTelemetryTime().monotonicMs-began.monotonicMs<1500&&!o.start(8));}
    {std::atomic<bool> reverse{false};SafetyTelemetryOwner o(fixture,{device()},policy,[&]{auto t=safetyTelemetryTime();if(reverse)t.utcMs-=10000;return t;});
        CHECK(o.start(7)&&wait([&]{return o.snapshot()->completed>=1;}));reverse=true;CHECK(wait([&]{return o.snapshot()->state==O::ClockFault;}));CHECK(!o.snapshot()->reply&&!o.start(8));}
    {auto p=policy;p.profile=SafetyTelemetryProfile::ExtendedFields;SafetyTelemetryOwner o(fixture,{device()},p);p.profile=SafetyTelemetryProfile::CoreEvents;
        CHECK(o.start(7)&&wait([&]{return o.snapshot()->completed>=2;}));const auto s=o.snapshot();
        CHECK(s->state==O::Ready&&s->request->profile==SafetyTelemetryProfile::ExtendedFields&&s->reply->readings[0].fields.size()==6);
        o.requestStop();CHECK(!o.snapshot()->reply&&!o.start(8));}
    {auto p=policy;p.profile=static_cast<SafetyTelemetryProfile>(2);SafetyTelemetryOwner o(fixture,{device()},p);CHECK(!o.start(7)&&o.snapshot()->state==O::Faulted);}
}
}
int main(int argc,char **argv){
    if(argc==3&&std::string(argv[2])=="parent"){auto r=request('d');SafetyTelemetryClient client(std::filesystem::absolute(argv[1]));std::atomic<bool> cancel{false};
        client.sample(r,cancel,2000);return 0;}
    if(argc!=3)return 2;
    codec();extendedCodec();processes(std::filesystem::absolute(argv[1]),std::filesystem::absolute(argv[2]));owners(std::filesystem::absolute(argv[1]));return finishTests("isolated safety telemetry");
}
