#include "billing_input_model.h"
#include "billing_input_query.h"
#include "local_store_lease.h"
#include "hash.h"
#include <cstdio>
#include <cstdlib>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#endif
namespace om {
namespace {
using namespace billing_input_detail;using namespace billing_detail;
using S=BillingStatus;using R=BillingRecoveryState;using P=BillingInputPhase;using A=BillingInputAction;using M=BillingInputMode;
std::filesystem::path suffix(std::filesystem::path p,const char *s){p+=s;return p;}
bool absent(const std::filesystem::path &p){std::error_code ec;const auto s=std::filesystem::symlink_status(p,ec);return ec==std::errc::no_such_file_or_directory||(!ec&&s.type()==std::filesystem::file_type::not_found);}
bool readFile(const std::filesystem::path &path,std::string &out){
#if defined(_WIN32)
    const auto f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(f==INVALID_HANDLE_VALUE)return false;
    BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
    bool ok=GetFileType(f)==FILE_TYPE_DISK&&GetFileInformationByHandle(f,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&
        GetFileSizeEx(f,&size)&&size.QuadPart>=0&&size.QuadPart<=static_cast<LONGLONG>(stageLimit);
    if(ok){out.assign(stageLimit+1,'\0');DWORD n=0;ok=ReadFile(f,&out[0],static_cast<DWORD>(out.size()),&n,nullptr)&&n<=stageLimit;out.resize(n);}
    CloseHandle(f);return ok;
#else
    const int f=::open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);if(f<0)return false;
    struct stat info{};bool ok=fstat(f,&info)==0&&S_ISREG(info.st_mode)&&info.st_size>=0&&static_cast<uint64_t>(info.st_size)<=stageLimit;
    out.clear();char buffer[8192];
    while(ok){const auto n=::read(f,buffer,sizeof(buffer));if(n<0){if(errno==EINTR)continue;ok=false;break;}if(!n)break;
        if(static_cast<size_t>(n)>stageLimit-out.size()){ok=false;break;}out.append(buffer,static_cast<size_t>(n));}
    ::close(f);return ok;
#endif
}
bool writeFile(const std::filesystem::path &path,const std::string &text){
    if(text.size()>stageLimit)return false;
    std::error_code ec;const auto target=std::filesystem::symlink_status(path,ec);
    if((ec&&ec!=std::errc::no_such_file_or_directory)||(!ec&&std::filesystem::exists(target)&&!std::filesystem::is_regular_file(target)))return false;
    // One bounded sibling temporary slot. An interrupted pre-replacement original is never deleted or adopted on restart.
    const auto temp=suffix(path,".tmp");
#if defined(_WIN32)
    int fd=-1;if(_wsopen_s(&fd,temp.c_str(),_O_WRONLY|_O_CREAT|_O_EXCL|_O_BINARY|_O_NOINHERIT,_SH_DENYRW,_S_IREAD|_S_IWRITE))return false;
    FILE *f=_fdopen(fd,"wb");
#else
    const int fd=::open(temp.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)return false;
    FILE *f=fdopen(fd,"wb");
#endif
    if(!f){
#if defined(_WIN32)
        _close(fd);
#else
        ::close(fd);
#endif
        return false;
    }
    bool ok=std::fwrite(text.data(),1,text.size(),f)==text.size()&&std::fflush(f)==0;
#if defined(_WIN32)
    ok=(_commit(fd)==0)&&ok;
#else
    ok=(fsync(fd)==0)&&ok;
#endif
    ok=(std::fclose(f)==0)&&ok;
#ifdef CANARY_BILLING_TESTING
    if(ok&&path.filename().u8string()=="fixture.billing-input-temp.db.billing-input")std::_Exit(101);
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
    // On uncertainty retain the only attempted bytes, including a failed-write temporary file, for explicit inspection.
    return ok;
}
struct Stage {
    BillingInputManifest manifest;P phase=P::Receiving;std::optional<BillingInputCertificate> certificate;
    std::string payload,revision;
    BillingBinding binding;BillingLimits limits;std::optional<BillingChunk> chunk;
    bool completePayload(){
        if(payload.size()!=manifest.bindingBytes+manifest.chunkBytes)return false;
        const auto bound=payload.substr(0,manifest.bindingBytes),original=payload.substr(manifest.bindingBytes);
        if(sha256Hex(bound)!=manifest.bindingDigest||sha256Hex(original)!=manifest.chunkDigest||!parseBinding(bound,binding,limits)||validateBinding(binding,limits,manifest.preparedUtcMs)!=S::Ok)return false;
        chunk.reset();
        if(manifest.mode==M::CreateNew)return original=="null";
        BillingChunk c;if(!parseChunk(original,binding.evidence,c)||c.sequence!=manifest.priorSequence+1||c.observedUtcMs>manifest.preparedUtcMs||
            (c.receipt&&c.receipt->request.source.nowUtcMs>manifest.preparedUtcMs))return false;
        chunk=std::move(c);return true;
    }
    bool load(const std::filesystem::path &path){
        std::string wire,meta,model;size_t at=6;
        if(!readFile(path,wire)||wire.rfind("CMBI1\n",0)!=0||!part(wire,at,manifestLimit,model)||!part(wire,at,16*1024,meta)||
            !part(wire,at,billingInputPayloadLimit,payload)||wire.size()-at!=65||wire.back()!='\n')return false;
        revision=wire.substr(at,64);if(!hash(revision)||sha256Hex(wire.substr(0,at))!=revision)return false;
        JsonValue m,s;uint64_t p=0;
        if(!parseJsonStrict(model,&m,nullptr)||!parseManifest(m,manifest)||!parseJsonStrict(meta,&s,nullptr)||!s.isArray()||s.items.size()!=2||!s.items[0].asUint64(&p)||p<1||p>3||
            !parseCertificate(s.items[1],certificate))return false;
        phase=static_cast<P>(p);const auto total=manifest.bindingBytes+manifest.chunkBytes;
        if(payload.size()>total||(payload.size()!=total&&payload.size()%billingInputPageBytes)||((phase==P::Complete)!=certificate.has_value())||
            (payload.size()==total&&!completePayload())||(phase!=P::Receiving&&payload.size()!=total)||
            (certificate&&!proof(manifest,binding,limits,chunk,*certificate)))return false;
        return stageBody(manifest,phase,certificate,payload)==wire.substr(0,at);
    }
    bool save(const std::filesystem::path &path){const auto body=stageBody(manifest,phase,certificate,payload);const auto digest=sha256Hex(body);
        if(!writeFile(path,body+digest+"\n"))return false;
        revision=digest;return true;}
    bool capture(const BillingRecoverySnapshot &s,bool unchanged=false){
        if(s.status!=S::Ok||s.committed.status!=S::Ok)return false;
        BillingInputCertificate c;
        if(unchanged&&manifest.mode==M::Submit&&s.revision==manifest.priorRevision&&s.committed.sequence==manifest.priorSequence&&s.committed.digest==manifest.priorDigest&&s.state!=R::Diverged&&s.state!=R::Prepared){
            c.result=S::Conflict;c.revision=s.revision;c.digest=s.committed.digest;
        }else{
            if(s.recordedUtcMs!=manifest.preparedUtcMs||s.priorSequence!=manifest.priorSequence||s.priorDigest!=manifest.priorDigest||
                (s.original?chunkText(*s.original):"null")!=payload.substr(manifest.bindingBytes))return false;
            c.state=s.state;c.result=s.applied.value_or(S::Ok);c.applied=s.applied;c.revision=s.revision;c.digest=s.committed.digest;c.committed=stateText(s.committed);
        }
        if(!proof(manifest,binding,limits,chunk,c))return false;
        certificate=std::move(c);phase=P::Complete;return true;
    }
    BillingInputReply reply(const BillingInputRequest &r)const{
        BillingInputReply s;s.status=S::Ok;s.phase=phase;s.revision=revision;s.received=payload.size();s.payloadDigest=sha256Hex(payload);s.certificate=certificate;
        if(r.action==A::Inspect)s.data=payload.substr(r.offset,billingInputPageBytes);
        return s;
    }
};
BillingInputReply failure(S s){BillingInputReply r;r.status=s;return r;}
}
BillingInputReply executeBillingInputRequest(const BillingInputRequest &r){
    if(!valid(r))return failure(S::Invalid);
    try{
        const auto path=suffix(r.manifest.file,".billing-input");std::error_code ec;
        if(r.action==A::Begin&&r.manifest.mode==M::CreateNew)std::filesystem::create_directories(path.parent_path(),ec);
        if(ec)return failure(S::Storage);
        LocalStoreLease lease;if(!lease.acquire(suffix(path,".lock")))return failure(S::Storage);
        Stage s;
        if(r.action==A::Begin){
            if(absent(path)){if(!r.expectedRevision.empty())return failure(S::Conflict);}
            else{if(!s.load(path))return failure(S::Storage);
                if(s.manifest.file!=r.manifest.file||s.phase!=P::Complete||r.expectedRevision!=s.revision)return failure(S::Conflict);}
            s=Stage{};s.manifest=r.manifest;if(!s.save(path))return failure(S::Storage);return s.reply(r);
        }
        if(!s.load(path))return failure(S::Storage);
        if(manifestText(s.manifest)!=manifestText(r.manifest)||(!r.expectedRevision.empty()&&s.revision!=r.expectedRevision))return failure(S::Conflict);
        if(r.action==A::Inspect){if(r.offset>s.payload.size())return failure(S::Conflict);return s.reply(r);}
        if(r.action==A::Append){
            if(s.phase!=P::Receiving||r.offset!=s.payload.size())return failure(S::Conflict);
            s.payload+=r.data;
            if(s.payload.size()==r.manifest.bindingBytes+r.manifest.chunkBytes&&!s.completePayload())return failure(S::Invalid);
            if(!s.save(path))return failure(S::Storage);
#ifdef CANARY_BILLING_TESTING
            if(r.manifest.file.filename().u8string()=="fixture.billing-input-page.db")std::_Exit(102);
#endif
            return s.reply(r);
        }
        if(!s.completePayload())return failure(S::Conflict);
        if(r.action==A::Reconcile){
            if(s.phase==P::Complete)return s.reply(r);
            // Empty initialization has no durable operation identity: never adopt a coincidentally matching existing pair.
            if(s.phase!=P::Issued||s.manifest.mode==M::CreateNew)return failure(S::Conflict);
            BillingRecoveryStore store;const auto opened=store.openQuery(s.manifest.file,s.manifest.bindingDigest,s.manifest.preparedUtcMs);
            if(opened!=S::Ok)return failure(opened);
            if(!s.capture(store.inspect(s.manifest.preparedUtcMs),true))return failure(S::Conflict);
            if(!s.save(path))return failure(S::Storage);
            return s.reply(r);
        }
        if(s.phase!=P::Receiving)return failure(S::Conflict);
        s.phase=P::Issued;if(!s.save(path))return failure(S::Storage);
#ifdef CANARY_BILLING_TESTING
        const auto name=s.manifest.file.filename().u8string();
        if(name=="fixture.billing-input-issued.db")std::_Exit(103);
#endif
        BillingRecoveryStore store;
#ifdef CANARY_BILLING_TESTING
        if(name=="fixture.billing-input-prepared.db")BillingRecoveryTestPeer::interrupt(store,BillingRecoveryTestPoint::AfterPrepared);
        if(name=="fixture.billing-input-precommit.db")BillingRecoveryTestPeer::interrupt(store,BillingRecoveryTestPoint::BeforeCommit);
        if(name=="fixture.billing-input-committed.db")BillingRecoveryTestPeer::interrupt(store,BillingRecoveryTestPoint::AfterCommit);
        if(name=="fixture.billing-input-settled.db")BillingRecoveryTestPeer::interrupt(store,BillingRecoveryTestPoint::AfterSettled);
#endif
        const auto applied=s.manifest.mode==M::CreateNew?store.open(s.manifest.file,s.binding,s.manifest.preparedUtcMs,BillingRecoveryOpen::CreateNew,s.limits):
            store.submitReviewed(s.manifest.file,s.manifest.bindingDigest,s.manifest.priorRevision,s.manifest.priorSequence,s.manifest.priorDigest,*s.chunk,s.manifest.preparedUtcMs);
        if(!s.capture(store.inspect(s.manifest.preparedUtcMs)))return failure(applied==S::Ok||applied==S::Replay?S::Storage:applied);
#ifdef CANARY_BILLING_TESTING
        if(name=="fixture.billing-input-created.db")std::_Exit(104);
#endif
        if(!s.save(path))return failure(S::Storage);
        return s.reply(r);
    }catch(...){return failure(S::Storage);}
}
BillingQueryPageReply executeBillingInputQuery(const BillingQueryPageRequest &r){
    BillingQueryPageReply result;
    if(!validBillingInputQuery(r)){result.status=S::Invalid;return result;}
    try{
        const auto path=suffix(r.file,".billing-input");LocalStoreLease lease;
        if(!lease.acquire(suffix(path,".lock")))return result;
        std::error_code ec;const auto temporary=std::filesystem::symlink_status(suffix(path,".tmp"),ec);
        if(ec&&ec!=std::errc::no_such_file_or_directory)return result;
        const std::string pending=!ec&&std::filesystem::exists(temporary)?"1":"0";
        std::string view="CMBIV1\nabsent\n"+pending+"\n";
        if(!absent(path)){
            Stage s;if(!s.load(path))return result;
            if(s.manifest.file!=r.file||s.manifest.bindingDigest!=r.bindingDigest){result.status=S::Attribution;return result;}
            if(s.manifest.preparedUtcMs>r.nowUtcMs){result.status=S::Clock;return result;}
            const auto metadata="["+std::to_string(static_cast<unsigned>(s.phase))+","+certificateText(s.certificate)+","+jsonQuote(s.revision)+","+
                std::to_string(s.payload.size())+","+jsonQuote(sha256Hex(s.payload))+","+pending+"]";
            view="CMBIV1\n"+frame(manifestText(s.manifest))+frame(metadata)+frame(s.payload.size()>s.manifest.bindingBytes?s.payload.substr(s.manifest.bindingBytes):"");
        }
        if(view.size()>billingInputViewLimit){result.status=S::Capacity;return result;}
        const auto digest=sha256Hex(view);
        if((!r.expectedView.empty()&&r.expectedView!=digest)||r.offset>=view.size()){result.status=S::Conflict;return result;}
        result.status=S::Ok;result.total=view.size();result.viewDigest=digest;result.data=view.substr(r.offset,billingQueryPageBytes);return result;
    }catch(...){return result;}
}
} // namespace om
