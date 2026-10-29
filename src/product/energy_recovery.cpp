#include "energy_recovery.h"
#include "energy_history_model.h"
#include "local_store_lease.h"
#include "json_dom.h"
#include "public_https.h"
#include "hash.h"
#include <atomic>
#include <cstdio>
#include <fstream>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#endif

namespace om {
namespace {
using namespace energy_history_detail;
using S=EnergyHistoryStatus;
constexpr size_t fileLimit=energyHistoryRequestLimit+512;
bool hash(const std::string &s){return s.size()==64&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
bool accepted(S s){return s==S::Ok||s==S::Replay;}
bool sameStore(const EnergyHistoryRequest &a,const EnergyHistoryRequest &b){
    return a.file==b.file&&bindingText(a.binding)==bindingText(b.binding)&&a.limits.maxBatches==b.limits.maxBatches&&a.limits.maxReadings==b.limits.maxReadings&&
        a.limits.maxSessions==b.limits.maxSessions&&a.limits.maxWindows==b.limits.maxWindows&&a.limits.maxDatabaseBytes==b.limits.maxDatabaseBytes;
}
EnergyHistoryRequest original(EnergyHistoryRequest r){r.generation=r.ticket=1;r.queries.clear();r.mode=EnergyHistoryOpen::Existing;r.recovery=EnergyRecoveryMode::Volatile;if(r.batch)r.nowUtcMs=r.batch->observedAtMs;return r;}
std::string record(EnergyRecoveryState state,std::optional<S> applied,uint64_t prior,const std::string &digest,const std::string &request){
    const auto body="[\"CMEJ1\","+std::to_string(static_cast<unsigned>(state))+","+(applied?std::to_string(static_cast<unsigned>(*applied)):"null")+","+
        std::to_string(prior)+","+jsonQuote(digest)+","+request;
    return body+","+jsonQuote(sha256Hex(body))+"]"; // corruption checksum, not authentication against directory rewrites
}
}
struct EnergyRecoveryJournal::Impl {
    LocalStoreLease lease;
    std::filesystem::path path;
    EnergyHistoryRequest bound,saved;
    EnergyRecoverySnapshot state;
    std::string wire;
    bool attempted=false,opened=false,present=false;
    S fail(S status){state={};state.status=status;return status;}
    S write(EnergyRecoveryState next,std::optional<S> result,uint64_t prior,const std::string &digest,const std::string &request){
        const auto text=record(next,result,prior,digest,request);if(text.size()>fileLimit)return fail(S::Capacity);
        std::error_code ec;const auto target=std::filesystem::symlink_status(path,ec);
        if(ec&&ec!=std::errc::no_such_file_or_directory)return fail(S::Storage);
        if(!ec&&std::filesystem::exists(target)&&!std::filesystem::is_regular_file(target))return fail(S::Storage);
        static std::atomic<unsigned> serial{0};auto temp=path;
#if defined(_WIN32)
        temp+=".tmp-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(++serial);
        int fd=-1;if(_wsopen_s(&fd,temp.c_str(),_O_WRONLY|_O_CREAT|_O_EXCL|_O_BINARY|_O_NOINHERIT,_SH_DENYRW,_S_IREAD|_S_IWRITE))return fail(S::Storage);
        FILE *f=_fdopen(fd,"wb");
#else
        temp+=".tmp-"+std::to_string(getpid())+"-"+std::to_string(++serial);
        const int fd=::open(temp.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)return fail(S::Storage);
        FILE *f=fdopen(fd,"wb");
#endif
        if(!f){
#if defined(_WIN32)
            _close(fd);
#else
            ::close(fd);
#endif
            std::filesystem::remove(temp,ec);return fail(S::Storage);
        }
        bool ok=fwrite(text.data(),1,text.size(),f)==text.size()&&fflush(f)==0;
#if defined(_WIN32)
        ok=(_commit(fd)==0)&&ok;
#else
        ok=(fsync(fd)==0)&&ok;
#endif
        ok=(fclose(f)==0)&&ok;
        if(ok){
#if defined(_WIN32)
            ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
            std::filesystem::rename(temp,path,ec);ok=!ec;
            if(ok){const int directory=::open(path.parent_path().c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);ok=directory>=0;
                if(directory>=0){ok=(fsync(directory)==0)&&ok;ok=(::close(directory)==0)&&ok;}}
#endif
        }
        if(!ok){std::filesystem::remove(temp,ec);return fail(S::Storage);}
        state.status=S::Ok;state.state=next;state.applied=result;state.priorSequence=prior;state.priorDigest=digest;wire=request;present=true;return S::Ok;
    }
};
EnergyRecoveryJournal::EnergyRecoveryJournal():impl_(new Impl){}
EnergyRecoveryJournal::~EnergyRecoveryJournal()=default;
S EnergyRecoveryJournal::open(const EnergyHistoryRequest &r){
    auto &s=*impl_;std::string checked;
    if(s.attempted||r.recovery==EnergyRecoveryMode::Volatile||!encodeEnergyHistoryRequest(r,&checked))return s.fail(S::Invalid);
    s.attempted=true;s.bound=r;s.bound.batch.reset();s.path=r.file;s.path+=".energy-recovery.json";
    std::error_code ec;if(r.mode==EnergyHistoryOpen::CreateNew)std::filesystem::create_directories(s.path.parent_path(),ec);
    if(ec)return s.fail(S::Storage);
    auto lock=s.path;lock+=".lock";if(!s.lease.acquire(lock))return s.fail(S::Storage);
    const auto file=std::filesystem::symlink_status(s.path,ec);
    const bool absent=ec==std::errc::no_such_file_or_directory||(!ec&&file.type()==std::filesystem::file_type::not_found);
    if(absent){if(r.mode!=EnergyHistoryOpen::CreateNew)return s.fail(S::Storage);
        s.opened=true;s.state.status=S::Ok;s.state.state=EnergyRecoveryState::Empty;return S::Ok;}
    if(ec||!std::filesystem::is_regular_file(file))return s.fail(S::Storage);
    std::ifstream in(s.path,std::ios::binary);if(!in)return s.fail(S::Storage);
    std::string text(fileLimit+1,'\0');in.read(&text[0],static_cast<std::streamsize>(text.size()));
    if(in.bad()||in.gcount()>static_cast<std::streamsize>(fileLimit))return s.fail(S::Storage);
    text.resize(static_cast<size_t>(in.gcount()));JsonValue root;uint64_t state=0,applied=0;
    if(!boundedPublicJson(text)||!parseJsonStrict(text,&root,nullptr)||!root.isArray()||root.items.size()!=7||!root.items[0].isString()||root.items[0].text!="CMEJ1"||
        !root.items[1].asUint64(&state)||state<1||state>4||!root.items[3].asUint64(&s.state.priorSequence)||s.state.priorSequence>r.limits.maxBatches||
        !root.items[4].isString()||!hash(root.items[4].text))return s.fail(S::Storage);
    s.state.state=static_cast<EnergyRecoveryState>(state);s.state.priorDigest=root.items[4].text;
    if(root.items[2].type!=JsonValue::Type::Null){if(!root.items[2].asUint64(&applied)||applied>static_cast<unsigned>(S::Storage))return s.fail(S::Storage);s.state.applied=static_cast<S>(applied);}
    const auto begin=text.find(",[\"CMEQ1\","),end=text.rfind(",\"");
    if(begin==std::string::npos||end==std::string::npos||end<=begin||end-begin-1>energyHistoryRequestLimit)return s.fail(S::Storage);
    s.wire=text.substr(begin+1,end-begin-1);std::string canonical;
    if(!decodeEnergyHistoryRequest(s.wire,&s.saved)||s.saved.recovery!=EnergyRecoveryMode::Volatile||s.saved.mode!=EnergyHistoryOpen::Existing||!s.saved.queries.empty()||
        s.saved.generation!=1||s.saved.ticket!=1||!encodeEnergyHistoryRequest(original(s.saved),&canonical)||canonical!=s.wire||
        record(s.state.state,s.state.applied,s.state.priorSequence,s.state.priorDigest,s.wire)!=text)return s.fail(S::Storage);
    if(!sameStore(s.saved,r))return s.fail(S::Attribution);
    if(!s.state.priorSequence&&s.state.priorDigest!=sha256Hex(bindingText(r.binding)))return s.fail(S::Storage);
    if(s.saved.nowUtcMs>r.nowUtcMs)return s.fail(S::Clock);
    const bool empty=s.state.state==EnergyRecoveryState::Empty;
    if(empty){if(s.saved.batch||s.state.applied||s.state.priorSequence||s.state.priorDigest!=sha256Hex(bindingText(r.binding)))return s.fail(S::Storage);}
    else{
        if(!s.saved.batch||s.saved.batch->sequence!=s.state.priorSequence+1||s.saved.batch->observedAtMs!=s.saved.nowUtcMs)return s.fail(S::Storage);
        if((s.state.state==EnergyRecoveryState::Prepared&&s.state.applied)||
            (s.state.state==EnergyRecoveryState::Confirmed&&(!s.state.applied||!accepted(*s.state.applied)))||
            (s.state.state==EnergyRecoveryState::Rejected&&(!s.state.applied||accepted(*s.state.applied)||*s.state.applied==S::Storage)))return s.fail(S::Storage);
    }
    s.state.batch=s.saved.batch;s.state.status=S::Ok;s.present=s.opened=true;return S::Ok;
}
bool EnergyRecoveryJournal::exists()const{return impl_->present;}
S EnergyRecoveryJournal::initialize(const EnergyHistoryCheckpoint &c){
    auto &s=*impl_;if(!s.opened||s.present||s.bound.mode!=EnergyHistoryOpen::CreateNew||c.status!=S::Ok||c.sequence||c.digest!=sha256Hex(bindingText(s.bound.binding)))return S::Conflict;
    auto request=original(s.bound);std::string wire;if(!encodeEnergyHistoryRequest(request,&wire))return S::Invalid;
    const auto status=s.write(EnergyRecoveryState::Empty,{},0,c.digest,wire);if(status==S::Ok)s.saved=std::move(request);return status;
}
S EnergyRecoveryJournal::prepare(const EnergyHistoryRequest &r,const EnergyHistoryCheckpoint &c){
    auto &s=*impl_;std::string checked;
    if(!s.opened||!s.present||s.state.status!=S::Ok||s.state.state==EnergyRecoveryState::Prepared||r.recovery!=EnergyRecoveryMode::Submit||!r.batch||!sameStore(s.bound,r)||c.status!=S::Ok||!hash(c.digest))return S::Conflict;
    if(r.batch->sequence!=c.sequence+1)return S::SequenceGap;
    if(!encodeEnergyHistoryRequest(r,&checked))return S::Invalid;
    auto request=original(r);std::string wire;if(!encodeEnergyHistoryRequest(request,&wire))return S::Invalid;
    const auto status=s.write(EnergyRecoveryState::Prepared,{},c.sequence,c.digest,wire);
    if(status==S::Ok){s.saved=std::move(request);s.state.batch=s.saved.batch;}return status;
}
S EnergyRecoveryJournal::settle(S applied){
    auto &s=*impl_;if(!s.opened||!s.present||s.state.status!=S::Ok||s.state.state!=EnergyRecoveryState::Prepared||!s.saved.batch||
        static_cast<unsigned>(applied)>=static_cast<unsigned>(S::Storage))return S::Invalid;
    return s.write(accepted(applied)?EnergyRecoveryState::Confirmed:EnergyRecoveryState::Rejected,applied,s.state.priorSequence,s.state.priorDigest,s.wire);
}
bool EnergyRecoveryJournal::matches(const EnergyHistoryRequest &r)const{
    const auto &s=*impl_;return s.opened&&s.present&&s.state.status==S::Ok&&s.saved.batch&&r.batch&&sameStore(s.saved,r)&&
        boundedEnergyHistoryBatch(r.binding,*r.batch,r.nowUtcMs)&&batchText(*s.saved.batch)==batchText(*r.batch);
}
EnergyRecoverySnapshot EnergyRecoveryJournal::snapshot()const{return impl_->state;}
EnergyRecoverySnapshot EnergyRecoveryJournal::inspect(EnergyHistoryLedger &ledger,int64_t now)const{
    auto result=snapshot();if(result.status!=S::Ok||!exists())return result;
    const auto c=ledger.checkpoint(now);if(c.status!=S::Ok){result.status=c.status;result.state=EnergyRecoveryState::Unavailable;result.batch.reset();result.applied.reset();result.priorSequence=0;result.priorDigest.clear();return result;}
    const bool prior=c.sequence==result.priorSequence&&c.digest==result.priorDigest;
    bool after=false;
    if(result.batch&&c.sequence==result.batch->sequence&&c.digest==sha256Hex(result.priorDigest+"|"+batchText(*result.batch)))
        after=ledger.verifyCommitted(*result.batch,now)==S::Replay;
    const bool consistent=result.state==EnergyRecoveryState::Prepared?(prior||after):result.state==EnergyRecoveryState::Confirmed?after:prior;
    if(!consistent)result.state=EnergyRecoveryState::Diverged;
    return result;
}
} // namespace om
