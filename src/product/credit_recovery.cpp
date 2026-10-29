#include "credit_recovery.h"
#include "runtime_preferences.h" // cooperative OS lease only; no runtime-default activation
#include "hash.h"

namespace om {
namespace {
constexpr size_t fileLimit = creditRecoveryRequestLimit + 256;
bool sameStore(const CreditProcessRequest &a, const CreditProcessRequest &b) {
    return a.file == b.file && sameCreditSourceContract(a.binding,b.binding) &&
        a.limits.maxRecords == b.limits.maxRecords && a.limits.maxVersions == b.limits.maxVersions &&
        a.limits.maxSnapshots == b.limits.maxSnapshots && a.limits.maxDatabaseBytes == b.limits.maxDatabaseBytes;
}
std::string batchIdentity(CreditProcessRequest r) {
    if (!r.batch) return {};
    r.generation = r.ticket = 1; r.nowUtcMs = r.batch->observedAtMs;
    r.queries = {{true,0,r.nowUtcMs}}; r.recovery = CreditRecoveryMode::Volatile;
    std::string wire;
    return encodeCreditRequest(r,&wire) ? wire : std::string();
}
std::string recordText(CreditRecoveryState state, std::optional<CreditLedgerStatus> applied, const std::string &request) {
    const auto body = std::string(request.rfind("[\"CMCR3\",",0)==0?"[\"CMCJ2\",":"[\"CMCJ1\",")+std::to_string(static_cast<unsigned>(state))+","+
        (applied?std::to_string(static_cast<unsigned>(*applied)):"null")+","+request;
    // Corruption detection only, not authentication/encryption against a local user who can rewrite the directory.
    return body+","+jsonQuote(sha256Hex(body))+"]";
}
bool definite(CreditLedgerStatus s) { return s != CreditLedgerStatus::Storage && s != CreditLedgerStatus::StaleBatch; }
}
struct CreditRecoveryJournal::Impl {
    RuntimePreferencesLease lease;
    std::filesystem::path path;
    CreditProcessRequest saved;
    CreditProcessRequest bound;
    std::string wire;
    CreditRecoveryState state = CreditRecoveryState::Unavailable;
    CreditLedgerStatus health = CreditLedgerStatus::Storage;
    std::optional<CreditLedgerStatus> applied;
    bool opened = false, attempted = false;
    CreditLedgerStatus fail(CreditLedgerStatus s) { health=s; return s; }
    CreditLedgerStatus write(CreditRecoveryState next, std::optional<CreditLedgerStatus> result, const std::string &request) {
        const auto text=recordText(next,result,request);
        if (text.size()>fileLimit) return fail(CreditLedgerStatus::Capacity);
        std::error_code ec;
        const auto target=std::filesystem::symlink_status(path,ec);
        if (ec && ec!=std::errc::no_such_file_or_directory) return fail(CreditLedgerStatus::Storage);
        if (!ec && std::filesystem::exists(target) && !std::filesystem::is_regular_file(target)) return fail(CreditLedgerStatus::Storage);
        static std::atomic<unsigned> serial{0}; auto temporary=path;
#if defined(_WIN32)
        temporary += ".tmp-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(++serial);
#else
        temporary += ".tmp-"+std::to_string(getpid())+"-"+std::to_string(++serial);
#endif
        int fd=-1;
#if defined(_WIN32)
        if (_wsopen_s(&fd,temporary.c_str(),_O_WRONLY|_O_CREAT|_O_EXCL|_O_BINARY|_O_NOINHERIT,_SH_DENYRW,_S_IREAD|_S_IWRITE)!=0) return fail(CreditLedgerStatus::Storage);
        FILE *f=_fdopen(fd,"wb");
#else
        fd=::open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
        if (fd<0) return fail(CreditLedgerStatus::Storage);
        FILE *f=fdopen(fd,"wb");
#endif
        if (!f) {
#if defined(_WIN32)
            _close(fd);
#else
            ::close(fd);
#endif
            std::filesystem::remove(temporary,ec); return fail(CreditLedgerStatus::Storage);
        }
        bool ok=fwrite(text.data(),1,text.size(),f)==text.size() && fflush(f)==0;
#if defined(_WIN32)
        ok=(_commit(fd)==0)&&ok;
#else
        ok=(fsync(fd)==0)&&ok;
#endif
        ok=(fclose(f)==0)&&ok;
        if (ok) {
#if defined(_WIN32)
            ok=MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
            std::filesystem::rename(temporary,path,ec); ok=!ec;
            if (ok) {
                const int directory=::open(path.parent_path().c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
                ok=directory>=0;
                if (directory>=0) { ok=(fsync(directory)==0)&&ok; ok=(::close(directory)==0)&&ok; }
            }
#endif
        }
        if (!ok) { std::filesystem::remove(temporary,ec); return fail(CreditLedgerStatus::Storage); }
        state=next; applied=result; wire=request; health=CreditLedgerStatus::Ok;
        return health;
    }
};
CreditRecoveryJournal::CreditRecoveryJournal() : impl_(new Impl) {}
CreditRecoveryJournal::~CreditRecoveryJournal() = default;
CreditLedgerStatus CreditRecoveryJournal::open(const CreditProcessRequest &request) {
    auto &s=*impl_;
    std::string checked;
    if (s.attempted || !encodeCreditRequest(request,&checked) || checked.size()>creditRecoveryRequestLimit) return s.fail(CreditLedgerStatus::Invalid);
    s.attempted=true;
    s.bound=request; s.bound.batch.reset();
    s.path=request.file; s.path += ".credit-recovery.json";
    std::error_code ec; std::filesystem::create_directories(s.path.parent_path(),ec);
    if (ec) return s.fail(CreditLedgerStatus::Storage);
    auto lock=s.path; lock += ".lock";
    if (!s.lease.acquire(lock)) return s.fail(CreditLedgerStatus::Storage);
    const auto status=std::filesystem::symlink_status(s.path,ec);
    if (ec==std::errc::no_such_file_or_directory || (!ec && status.type()==std::filesystem::file_type::not_found)) {
        s.opened=true; s.state=CreditRecoveryState::Empty; s.health=CreditLedgerStatus::Ok; return s.health;
    }
    if (ec || !std::filesystem::is_regular_file(status)) return s.fail(CreditLedgerStatus::Storage);
    std::ifstream input(s.path,std::ios::binary);
    if (!input) return s.fail(CreditLedgerStatus::Storage);
    std::string text(fileLimit+1,'\0'); input.read(&text[0],static_cast<std::streamsize>(text.size()));
    if (input.bad() || input.gcount()>static_cast<std::streamsize>(fileLimit)) return s.fail(CreditLedgerStatus::Storage);
    text.resize(static_cast<size_t>(input.gcount()));
    JsonValue root; uint64_t state=0, result=0;
    if (!parseJsonStrict(text,&root,nullptr) || !root.isArray() || root.items.size()!=5 || !root.items[0].isString() || (root.items[0].text!="CMCJ1" && root.items[0].text!="CMCJ2") ||
        !root.items[1].asUint64(&state) || state<static_cast<unsigned>(CreditRecoveryState::Prepared) || state>static_cast<unsigned>(CreditRecoveryState::Rejected)) return s.fail(CreditLedgerStatus::Storage);
    s.state=static_cast<CreditRecoveryState>(state);
    if (root.items[2].type!=JsonValue::Type::Null) {
        if (!root.items[2].asUint64(&result) || result>static_cast<unsigned>(CreditLedgerStatus::Storage)) return s.fail(CreditLedgerStatus::Storage);
        s.applied=static_cast<CreditLedgerStatus>(result);
    }
    if ((s.state==CreditRecoveryState::Prepared && s.applied) ||
        (s.state==CreditRecoveryState::Confirmed && (!s.applied || (*s.applied!=CreditLedgerStatus::Ok && *s.applied!=CreditLedgerStatus::Replay))) ||
        (s.state==CreditRecoveryState::Rejected && (!s.applied || !definite(*s.applied) || *s.applied==CreditLedgerStatus::Ok || *s.applied==CreditLedgerStatus::Replay))) return s.fail(CreditLedgerStatus::Storage);
    // Decode the original request from the canonical envelope without accepting a second serialization dialect.
    const auto requestBegin=text.find(root.items[0].text=="CMCJ2"?",[\"CMCR3\",":",[\"CMCR1\",");
    const auto digestBegin=text.rfind(",\"");
    if (requestBegin==std::string::npos || digestBegin==std::string::npos || digestBegin<=requestBegin || digestBegin-requestBegin-1>creditRecoveryRequestLimit) return s.fail(CreditLedgerStatus::Storage);
    s.wire=text.substr(requestBegin+1,digestBegin-requestBegin-1);
    std::string canonical;
    if (!decodeCreditRequest(s.wire,&s.saved) || !s.saved.batch || s.saved.recovery!=CreditRecoveryMode::Volatile ||
        !encodeCreditRequest(s.saved,&canonical) || canonical!=s.wire || recordText(s.state,s.applied,s.wire)!=text) return s.fail(CreditLedgerStatus::Storage);
    if (!sameStore(s.saved,request)) return s.fail(CreditLedgerStatus::Attribution);
    s.opened=true; s.health=CreditLedgerStatus::Ok; return s.health;
}
bool CreditRecoveryJournal::matches(const CreditProcessRequest &request) const {
    const auto &s=*impl_;
    if (!s.opened || !s.saved.batch || !sameStore(s.saved,request)) return false;
    std::string checked;
    if (!encodeCreditRequest(request,&checked) || checked.size()>creditRecoveryRequestLimit) return false;
    const auto identity=batchIdentity(request);
    return !identity.empty() && identity==batchIdentity(s.saved);
}
CreditLedgerStatus CreditRecoveryJournal::prepare(const CreditProcessRequest &request) {
    auto &s=*impl_;
    if (!s.opened || !request.batch || s.state==CreditRecoveryState::Prepared || !sameStore(s.bound,request)) return s.fail(CreditLedgerStatus::Conflict);
    std::string checked;
    if (!encodeCreditRequest(request,&checked)) return s.fail(CreditLedgerStatus::Invalid);
    if (checked.size()>creditRecoveryRequestLimit) return s.fail(CreditLedgerStatus::Capacity);
    auto original=request; original.recovery=CreditRecoveryMode::Volatile;
    std::string wire;
    if (!encodeCreditRequest(original,&wire)) return s.fail(CreditLedgerStatus::Invalid);
    if (wire.size()>creditRecoveryRequestLimit) return s.fail(CreditLedgerStatus::Capacity);
    const auto result=s.write(CreditRecoveryState::Prepared,{},wire);
    if (result==CreditLedgerStatus::Ok) s.saved=std::move(original);
    return result;
}
CreditLedgerStatus CreditRecoveryJournal::settle(CreditLedgerStatus applied) {
    auto &s=*impl_;
    if (!s.opened || !s.saved.batch || !definite(applied)) return s.fail(CreditLedgerStatus::Invalid);
    return s.write(applied==CreditLedgerStatus::Ok || applied==CreditLedgerStatus::Replay ? CreditRecoveryState::Confirmed : CreditRecoveryState::Rejected,applied,s.wire);
}
CreditRecoverySnapshot CreditRecoveryJournal::snapshot() const {
    const auto &s=*impl_; CreditRecoverySnapshot result; result.status=s.health;
    if (!s.opened || s.health!=CreditLedgerStatus::Ok) return result;
    result.state=s.state; result.applied=s.applied;
    if (s.saved.batch) result.batch=s.saved.batch;
    return result;
}
}  // namespace om
