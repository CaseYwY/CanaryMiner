#include "billing_recovery.h"
#include "billing_model.h"
#include "local_store_lease.h"
#include "json_dom.h"
#include "hash.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#endif
namespace om {
namespace {
using S=BillingStatus;using R=BillingRecoveryState;using O=BillingRecoveryOpen;
using namespace billing_detail;
constexpr size_t metadataLimit=16*1024,fileLimit=bindingLimit+chunkLimit+metadataLimit+128;
bool hash(const std::string &s){return s.size()==64&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
bool accepted(S s){return s==S::Ok||s==S::Replay;}
bool absent(const std::filesystem::path &p){std::error_code ec;const auto st=std::filesystem::symlink_status(p,ec);return ec==std::errc::no_such_file_or_directory||(!ec&&st.type()==std::filesystem::file_type::not_found);}
std::string meta(const BillingRecoverySnapshot &s,const std::filesystem::path &p){
    return "["+std::to_string(static_cast<unsigned>(s.state))+","+(s.applied?std::to_string(static_cast<unsigned>(*s.applied)):"null")+","+
        jsonQuote(p.u8string())+","+std::to_string(s.recordedUtcMs)+","+std::to_string(s.priorSequence)+","+jsonQuote(s.priorDigest)+"]";
}
std::string frame(const std::string &s){return std::to_string(s.size())+"\n"+s+"\n";}
std::string body(const BillingRecoverySnapshot &s,const std::filesystem::path &p,const std::string &binding,const std::string &original){
    return "CMBJ1\n"+frame(meta(s,p))+frame(binding)+frame(original);
}
bool part(const std::string &s,size_t &offset,size_t limit,std::string &out){
    const auto end=s.find('\n',offset);if(end==std::string::npos||end==offset||end-offset>10||(end-offset>1&&s[offset]=='0'))return false;
    size_t count=0;for(size_t i=offset;i<end;++i){const auto c=s[i];if(c<'0'||c>'9'||count>(limit-static_cast<unsigned>(c-'0'))/10)return false;count=count*10+static_cast<unsigned>(c-'0');}
    offset=end+1;if(count>limit||count>=s.size()-offset||s[offset+count]!='\n')return false;
    out=s.substr(offset,count);offset+=count+1;return true;
}
}
struct BillingRecoveryStore::Impl {
    LocalStoreLease lease; // destroyed AFTER ledger: release in reverse acquisition order
    BillingLedger ledger;
    std::filesystem::path file,path;
    BillingBinding binding;BillingLimits limits;
    BillingRecoverySnapshot state;
    std::string bound,original="null";
    bool attempted=false,ready=false,queryOnly=false;
#ifdef CANARY_BILLING_TESTING
    BillingRecoveryTestPoint point=BillingRecoveryTestPoint::None;
#endif
    S fail(S status){ready=false;state={};state.status=status;return status;}
    S write(BillingRecoverySnapshot next,const std::string &input){
        const auto content=body(next,file,bound,input);const auto revision=sha256Hex(content);const auto text=content+revision+"\n";
        if(meta(next,file).size()>metadataLimit||text.size()>fileLimit)return fail(S::Capacity);
        std::error_code ec;const auto target=std::filesystem::symlink_status(path,ec);
        if((ec&&ec!=std::errc::no_such_file_or_directory)||(!ec&&std::filesystem::exists(target)&&!std::filesystem::is_regular_file(target)))return fail(S::Storage);
#ifdef CANARY_BILLING_TESTING
        if(point==BillingRecoveryTestPoint::FailSettlement&&next.state!=R::Prepared)return fail(S::Storage);
#endif
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
#ifdef CANARY_BILLING_TESTING
        if(ok&&point==BillingRecoveryTestPoint::BeforeJournalReplace)std::_Exit(93);
#endif
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
        next.revision=revision;state=std::move(next);original=input;return S::Ok;
    }
    S load(int64_t now){
        std::error_code ec;if(!std::filesystem::is_regular_file(std::filesystem::symlink_status(path,ec))||ec)return S::Storage;
        std::ifstream in(path,std::ios::binary);if(!in)return S::Storage;
        std::string text(fileLimit+1,'\0');in.read(&text[0],static_cast<std::streamsize>(text.size()));
        if(in.bad()||in.gcount()>static_cast<std::streamsize>(fileLimit))return S::Storage;
        text.resize(static_cast<size_t>(in.gcount()));size_t at=6;std::string metadata,saved;
        if(text.rfind("CMBJ1\n",0)!=0||!part(text,at,metadataLimit,metadata)||!part(text,at,bindingLimit,saved)||!part(text,at,chunkLimit,original)||text.size()-at!=65||text.back()!='\n')return S::Storage;
        state.revision=text.substr(at,64);if(!hash(state.revision)||sha256Hex(text.substr(0,at))!=state.revision)return S::Storage;
        JsonValue root;uint64_t kind=0,result=0,recorded=0;
        if(!parseJsonStrict(metadata,&root,nullptr)||!root.isArray()||root.items.size()!=6)return S::Storage;
        const auto &a=root.items;
        if(!a[0].asUint64(&kind)||kind<1||kind>4||!a[2].isString()||!a[3].asUint64(&recorded)||recorded>static_cast<uint64_t>(lastUtc)||!recorded||
            !a[4].asUint64(&state.priorSequence)||state.priorSequence>limits.maxBatches||!a[5].isString()||!hash(a[5].text))return S::Storage;
        state.state=static_cast<R>(kind);state.recordedUtcMs=static_cast<int64_t>(recorded);state.priorDigest=a[5].text;
        if(a[1].type!=JsonValue::Type::Null){if(!a[1].asUint64(&result)||result>=static_cast<unsigned>(S::Storage))return S::Storage;state.applied=static_cast<S>(result);}
        if(a[2].text!=file.u8string()||saved!=bound)return S::Attribution;
        if(metadata!=meta(state,file))return S::Storage;
        if(!state.priorSequence&&state.priorDigest!=sha256Hex(bound))return S::Storage;
        if(state.state==R::Empty){if(original!="null"||state.applied||state.priorSequence)return S::Storage;}
        else{
            BillingChunk chunk;if(!parseChunk(original,binding.evidence,chunk)||chunk.sequence!=state.priorSequence+1||chunk.observedUtcMs>state.recordedUtcMs||
                (chunk.receipt&&chunk.receipt->request.source.nowUtcMs>state.recordedUtcMs))return S::Storage;
            state.original=std::move(chunk);
            if((state.state==R::Prepared&&state.applied)||(state.state==R::Confirmed&&(!state.applied||!accepted(*state.applied)))||
                (state.state==R::Rejected&&(!state.applied||accepted(*state.applied))))return S::Storage;
        }
        if(state.recordedUtcMs<binding.cycleBeginUtcMs)return S::Storage;
        if(now<state.recordedUtcMs)return S::Clock;
        state.status=S::Ok;return S::Ok;
    }
    BillingRecoverySnapshot inspect(int64_t now)const{
        if(!ready){BillingRecoverySnapshot result;result.status=state.status;return result;}
        auto result=state;result.committed=ledger.snapshot(now);
        if(!instant(now)||now<state.recordedUtcMs||result.committed.status!=S::Ok){BillingRecoverySnapshot failed;failed.status=result.committed.status==S::Ok?S::Clock:result.committed.status;return failed;}
        const auto &c=result.committed;const bool prior=c.sequence==state.priorSequence&&c.digest==state.priorDigest;
        bool after=false;
        if(state.original&&c.sequence==state.original->sequence&&c.digest==sha256Hex(state.priorDigest+"|"+original+"|"+stateText(c))){
            const auto verified=ledger.verifyCommitted(*state.original,now);
            if(verified==S::Storage){BillingRecoverySnapshot failed;return failed;}
            after=verified==S::Replay;
        }
        const bool consistent=state.state==R::Prepared?(prior||after):state.state==R::Confirmed?after:prior;
        if(!consistent)result.state=R::Diverged;
        return result;
    }
    S apply(int64_t now,bool retry){
#ifdef CANARY_BILLING_TESTING
        if(point==BillingRecoveryTestPoint::AfterPrepared)std::_Exit(94);
        if(point==BillingRecoveryTestPoint::BeforeCommit)BillingLedgerTestPeer::crashBeforeCommit(ledger);
#endif
        const auto applied=ledger.apply(*state.original,now);
#ifdef CANARY_BILLING_TESTING
        if(point==BillingRecoveryTestPoint::AfterCommit)std::_Exit(95);
#endif
        // Storage/page-cap failure can poison the handle; failed retry must retain the exact Prepared bytes in all cases.
        if(applied==S::Storage||(!accepted(applied)&&(retry||ledger.snapshot(now).status!=S::Ok)))return applied;
        const auto checked=inspect(now);if(checked.status!=S::Ok)return checked.status;if(checked.state==R::Diverged)return S::Conflict;
        auto next=state;next.state=accepted(applied)?R::Confirmed:R::Rejected;next.applied=applied;
        const auto saved=write(std::move(next),original);if(saved!=S::Ok)return saved;
#ifdef CANARY_BILLING_TESTING
        if(point==BillingRecoveryTestPoint::AfterSettled)std::_Exit(96);
#endif
        return applied;
    }
};
BillingRecoveryStore::BillingRecoveryStore():impl_(new Impl){}
BillingRecoveryStore::~BillingRecoveryStore()=default;
S BillingRecoveryStore::open(const std::filesystem::path &file,const BillingBinding &binding,int64_t now,O mode,BillingLimits limits){
    auto &s=*impl_;if(s.attempted)return S::Invalid;s.attempted=true;
    const auto valid=validateBinding(binding,limits,now);if(valid!=S::Ok)return s.fail(valid);
    const auto pathText=file.u8string();
    if(!file.is_absolute()||pathText.size()>4096||pathText.find('\0')!=std::string::npos||(mode!=O::Existing&&mode!=O::CreateNew&&mode!=O::QueryOnly))return s.fail(S::Invalid);
    s.file=file;s.path=file;s.path+=".billing-recovery";s.binding=binding;s.limits=limits;s.bound=bindingText(binding,limits);s.queryOnly=mode==O::QueryOnly;
    try{
        std::error_code ec;if(mode==O::CreateNew)std::filesystem::create_directories(file.parent_path(),ec);if(ec)return s.fail(S::Storage);
        auto lock=s.path;lock+=".lock";if(!s.lease.acquire(lock))return s.fail(S::Storage);
        if(mode==O::CreateNew){if(!absent(s.path))return s.fail(S::Conflict);}
        else{const auto loaded=s.load(now);if(loaded!=S::Ok)return s.fail(loaded);}
        const auto opened=s.ledger.open(file,binding,now,mode==O::CreateNew?BillingOpen::CreateNew:s.queryOnly?BillingOpen::Inspect:BillingOpen::Existing,limits);
        if(opened!=S::Ok)return s.fail(opened);
        if(mode==O::CreateNew){BillingRecoverySnapshot empty;empty.status=S::Ok;empty.state=R::Empty;empty.recordedUtcMs=now;empty.priorDigest=s.ledger.snapshot(now).digest;
            const auto saved=s.write(std::move(empty),"null");if(saved!=S::Ok)return saved;}
        s.ready=true;return S::Ok;
    }catch(...){return s.fail(S::Storage);}
}
BillingRecoverySnapshot BillingRecoveryStore::inspect(int64_t now)const{return impl_->inspect(now);}
S BillingRecoveryStore::submit(const BillingChunk &chunk,int64_t now){
    auto &s=*impl_;if(!s.ready)return S::Storage;if(s.queryOnly)return S::Unsupported;
    try{
        const auto checked=s.inspect(now);if(checked.status!=S::Ok)return checked.status;if(checked.state==R::Prepared||checked.state==R::Diverged)return S::Conflict;
        if(!boundedChunk(chunk))return S::Invalid;
        if(chunk.observedUtcMs>now||(chunk.receipt&&chunk.receipt->request.source.nowUtcMs>now))return S::Clock;
        const auto input=chunkText(chunk);if(input.empty())return S::Invalid;if(input.size()>chunkLimit)return S::Capacity;
        BillingChunk parsed;if(!parseChunk(input,s.binding.evidence,parsed))return S::Attribution;
        const auto replay=s.ledger.verifyCommitted(chunk,now);if(replay!=S::SequenceGap)return replay;
        if(chunk.sequence!=checked.committed.sequence+1)return S::SequenceGap;
        auto next=BillingRecoverySnapshot{};next.status=S::Ok;next.state=R::Prepared;next.priorSequence=checked.committed.sequence;next.priorDigest=checked.committed.digest;next.recordedUtcMs=now;next.original=chunk;
        const auto saved=s.write(std::move(next),input);return saved==S::Ok?s.apply(now,false):saved;
    }catch(...){return s.fail(S::Storage);}
}
S BillingRecoveryStore::retry(const BillingChunk &chunk,const std::string &revision,int64_t now){
    auto &s=*impl_;if(!s.ready)return S::Storage;if(s.queryOnly)return S::Unsupported;
    try{
        const auto checked=s.inspect(now);if(checked.status!=S::Ok)return checked.status;
        if((checked.state!=R::Prepared&&checked.state!=R::Confirmed)||!hash(revision)||revision!=checked.revision||!boundedChunk(chunk)||chunkText(chunk)!=s.original)return S::Conflict;
        if(checked.state==R::Confirmed)return S::Replay;
        return s.apply(now,true);
    }catch(...){return s.fail(S::Storage);}
}
#ifdef CANARY_BILLING_TESTING
void BillingRecoveryTestPeer::interrupt(BillingRecoveryStore &store,BillingRecoveryTestPoint point){store.impl_->point=point;}
#endif
} // namespace om
