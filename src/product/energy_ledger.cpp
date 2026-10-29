#include "energy_ledger.h"
#include "energy_history_model.h"
#include "hash.h"
#include "local_store_lease.h"
#include "sqlite_runtime.h"
#include <algorithm>
#include <cstdlib>

namespace om {
namespace {
using namespace energy_history_detail;
constexpr int64_t application = 0x434d4548;
const std::string metaSql = "CREATE TABLE meta(id INTEGER PRIMARY KEY CHECK(id=1),binding TEXT NOT NULL,sequence INTEGER NOT NULL,digest TEXT NOT NULL)";
const std::string eventsSql = "CREATE TABLE events(sequence INTEGER PRIMARY KEY,id TEXT NOT NULL UNIQUE,body TEXT NOT NULL,digest TEXT NOT NULL)";
struct Param {
    bool isText=false;int64_t number=0;std::string value;
    Param(int64_t n):number(n){}
    Param(const std::string &s):isText(true),value(s){}
};
}
struct EnergyHistoryLedger::Impl {
    SqliteRuntime api;
    SqliteDb *db=nullptr;
    LocalStoreLease lease;
    bool attempted=false,ready=false;
    EnergyHistoryBinding binding;
    EnergyHistoryLimits limits;
    Model model;
    std::string digest;
#ifdef CANARY_ENERGY_HISTORY_TESTING
    bool crash=false;
#endif
    ~Impl(){if(db)api.closeV2(db);}
    struct Statement {
        Impl *owner;SqliteStatement *value=nullptr;
        Statement(Impl *o,const char *sql,std::initializer_list<Param> params={}):owner(o){
            if(owner->api.prepare(owner->db,sql,-1,&value,nullptr)!=0){if(value)owner->api.finalize(value);value=nullptr;return;}
            int i=0;
            for(const auto &p:params){const int code=p.isText?owner->api.bindText(value,++i,p.value.data(),static_cast<int>(p.value.size()),reinterpret_cast<void (*)(void *)>(static_cast<intptr_t>(-1))):owner->api.bindInteger(value,++i,p.number);
                if(code){owner->api.finalize(value);value=nullptr;return;}}
        }
        ~Statement(){if(value)owner->api.finalize(value);}
        int step(){return value?owner->api.step(value):1;}
    };
    bool exec(const std::string &sql){return api.execute(db,sql.c_str(),nullptr,nullptr,nullptr)==0;}
    bool write(const char *sql,std::initializer_list<Param> params={}){Statement q(this,sql,params);return q.step()==101;}
    bool number(SqliteStatement *q,int col,int64_t *out){if(api.columnType(q,col)!=1)return false;*out=api.columnInteger(q,col);return true;}
    bool text(SqliteStatement *q,int col,std::string *out){
        if(api.columnType(q,col)!=3)return false;
        const auto *p=api.columnText(q,col);const auto n=api.columnBytes(q,col);
        if(!p||n<0||n>static_cast<int>(batchWireLimit)||std::find(p,p+n,0)!=p+n)return false;
        out->assign(reinterpret_cast<const char *>(p),static_cast<size_t>(n));return true;
    }
    bool scalar(const char *sql,int64_t *out){Statement q(this,sql);return q.step()==100&&number(q.value,0,out)&&q.step()==101;}
    bool isText(const char *sql,const std::string &expected){Statement q(this,sql);std::string s;return q.step()==100&&text(q.value,0,&s)&&s==expected&&q.step()==101;}
    EnergyHistoryStatus fail(){ready=false;return EnergyHistoryStatus::Storage;}
    bool audit(){
        int64_t count=0,seq=0;std::string bound,head;
        if(!scalar("SELECT count(*) FROM meta",&count)||count!=1)return false;
        {Statement q(this,"SELECT binding,sequence,digest FROM meta WHERE id=1");
            if(q.step()!=100||!text(q.value,0,&bound)||bound!=bindingText(binding)||!number(q.value,1,&seq)||seq<0||static_cast<uint64_t>(seq)>limits.maxBatches||
                !text(q.value,2,&head)||q.step()!=101)return false;}
        Model checked;checked.head.status=EnergyHistoryStatus::Ok;
        std::string previous=sha256Hex(bound);int code=0;Statement q(this,"SELECT sequence,id,body,digest FROM events ORDER BY sequence");
        while((code=q.step())==100){
            int64_t n=0;std::string id,body,hash;EnergyHistoryBatch b;
            if(!number(q.value,0,&n)||n<=0||static_cast<uint64_t>(n)!=checked.head.sequence+1||n>seq||!text(q.value,1,&id)||!text(q.value,2,&body)||!text(q.value,3,&hash)||
                !parseBatch(body,binding,&b)||batchText(b)!=body||b.sequence!=static_cast<uint64_t>(n)||b.id!=id||hash!=sha256Hex(previous+"|"+body)||
                checked.advance(binding,b,limits)!=EnergyHistoryStatus::Ok)return false;
            previous=std::move(hash);
        }
        if(code!=101||checked.head.sequence!=static_cast<uint64_t>(seq)||previous!=head)return false;
        model=std::move(checked);digest=std::move(previous);return true;
    }
};
EnergyHistoryLedger::EnergyHistoryLedger():impl_(new Impl){}
EnergyHistoryLedger::~EnergyHistoryLedger()=default;
EnergyHistoryStatus EnergyHistoryLedger::open(const std::filesystem::path &path,const EnergyHistoryBinding &binding,int64_t now,EnergyHistoryOpen mode,EnergyHistoryLimits limits){
    using S=EnergyHistoryStatus;auto &s=*impl_;
    if(s.attempted)return S::Invalid;
    s.attempted=true;
    if(!validEnergyHistoryBinding(binding)||!validEnergyHistoryLimits(limits)||!instant(now)||binding.historyBeginUtcMs>now||!path.is_absolute()||
        (mode!=EnergyHistoryOpen::Existing&&mode!=EnergyHistoryOpen::CreateNew))return S::Invalid;
    s.binding=binding;s.limits=limits;
    try{
        if(!s.api.load())return S::Storage;
        std::error_code ec;
        if(mode==EnergyHistoryOpen::CreateNew)std::filesystem::create_directories(path.parent_path(),ec);
        if(ec)return S::Storage;
        auto lock=path;lock+=".lock";if(!s.lease.acquire(lock))return S::Storage;
        const auto status=std::filesystem::symlink_status(path,ec);
        const bool absent=ec==std::errc::no_such_file_or_directory||(!ec&&status.type()==std::filesystem::file_type::not_found);
        if(mode==EnergyHistoryOpen::CreateNew){if(!absent)return S::Conflict;
#if defined(_WIN32)
            HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(file==INVALID_HANDLE_VALUE)return S::Storage;
            CloseHandle(file);
#else
            const int fd=::open(path.c_str(),O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
            if(fd<0)return S::Storage;
            ::close(fd);
#endif
        }else if(absent||ec||!std::filesystem::is_regular_file(status)||std::filesystem::file_size(path)>limits.maxDatabaseBytes)return S::Storage;
        if(s.api.openV2(path.u8string().c_str(),&s.db,2|0x10000|0x1000000,nullptr)!=0)return S::Storage;
        s.api.busyTimeout(s.db,0);
        s.api.limit(s.db,0,static_cast<int>(batchWireLimit+4096));s.api.limit(s.db,1,65536);s.api.limit(s.db,2,32);s.api.limit(s.db,3,32);s.api.limit(s.db,7,0);
        int64_t app=0,version=0;
        if(!s.scalar("PRAGMA application_id",&app)||!s.scalar("PRAGMA user_version",&version)||(mode==EnergyHistoryOpen::Existing&&(app!=application||version!=1)))return S::Storage;
        if(!s.exec("PRAGMA trusted_schema=OFF; PRAGMA foreign_keys=ON; PRAGMA temp_store=MEMORY; PRAGMA cache_size=-2048;"))return S::Storage;
        if(mode==EnergyHistoryOpen::Existing){
            int64_t extra=0;
            if(!s.isText("PRAGMA quick_check(1)","ok")||!s.scalar("SELECT count(*) FROM sqlite_master WHERE type IN ('trigger','view') OR (type='table' AND name NOT IN ('meta','events'))",&extra)||extra||
                !s.isText("SELECT sql FROM sqlite_master WHERE type='table' AND name='meta'",metaSql)||!s.isText("SELECT sql FROM sqlite_master WHERE type='table' AND name='events'",eventsSql))return S::Storage;
            if(!s.isText("SELECT binding FROM meta WHERE id=1",bindingText(binding)))return S::Attribution;
            if(!s.audit())return S::Storage;
            if(now<s.model.head.observedAtMs)return S::Clock;
        }
        if(!s.isText("PRAGMA journal_mode=WAL","wal")||!s.exec("PRAGMA synchronous=FULL; PRAGMA wal_autocheckpoint=256; PRAGMA journal_size_limit=1048576;"))return S::Storage;
        int64_t page=0;
        if(!s.scalar("PRAGMA page_size",&page)||page<512||page>65536||!s.exec("PRAGMA max_page_count="+std::to_string(limits.maxDatabaseBytes/static_cast<uint64_t>(page))))return S::Storage;
        if(mode==EnergyHistoryOpen::CreateNew){
            s.digest=sha256Hex(bindingText(binding));
            if(!s.exec("BEGIN IMMEDIATE;"+metaSql+";"+eventsSql+";")||!s.write("INSERT INTO meta VALUES(1,?,0,?)",{bindingText(binding),s.digest})||
                !s.exec("PRAGMA application_id="+std::to_string(application)+"; PRAGMA user_version=1; COMMIT;")){s.exec("ROLLBACK;");return S::Storage;}
            s.model.head.status=S::Ok;
        }
        s.ready=true;return S::Ok;
    }catch(...){return s.fail();}
}
EnergyHistoryStatus EnergyHistoryLedger::apply(const EnergyHistoryBatch &b,int64_t now){
    using S=EnergyHistoryStatus;auto &s=*impl_;
    if(!s.ready)return S::Storage;
    if(!boundedEnergyHistoryBatch(s.binding,b,now))return S::Invalid;
    if(now<s.model.head.observedAtMs)return S::Clock;
    try{
        const auto body=batchText(b);
        if(b.sequence<=s.model.head.sequence){ // any retained committed sequence, not merely the most recent receipt
            Impl::Statement q(&s,"SELECT id,body FROM events WHERE sequence=?",{static_cast<int64_t>(b.sequence)});std::string id,stored;
            if(q.step()!=100||!s.text(q.value,0,&id)||!s.text(q.value,1,&stored)||q.step()!=101)return s.fail();
            return id==b.id&&stored==body?S::Replay:S::Conflict;
        }
        auto next=s.model;const auto valid=next.advance(s.binding,b,s.limits);if(valid!=S::Ok)return valid;
        const auto digest=sha256Hex(s.digest+"|"+body);
        if(!s.exec("BEGIN IMMEDIATE;"))return s.fail();
        if(!s.write("INSERT INTO events VALUES(?,?,?,?)",{static_cast<int64_t>(b.sequence),b.id,body,digest})||
            !s.write("UPDATE meta SET sequence=?,digest=? WHERE id=1",{static_cast<int64_t>(b.sequence),digest})){s.exec("ROLLBACK;");return s.fail();}
#ifdef CANARY_ENERGY_HISTORY_TESTING
        if(s.crash)std::_Exit(86);
#endif
        if(!s.exec("COMMIT;")){s.exec("ROLLBACK;");return s.fail();}
        s.model=std::move(next);s.digest=digest;return S::Ok;
    }catch(...){s.exec("ROLLBACK;");return s.fail();}
}
EnergyHistoryCheckpoint EnergyHistoryLedger::checkpoint(int64_t now) const {
    const auto &s=*impl_;EnergyHistoryCheckpoint c;
    if(!s.ready)return c;
    if(!instant(now)){c.status=EnergyHistoryStatus::Invalid;return c;}
    if(now<s.model.head.observedAtMs){c.status=EnergyHistoryStatus::Clock;return c;}
    return s.model.head;
}
EnergyHistoryTotals EnergyHistoryLedger::query(int64_t begin,int64_t end,int64_t now) const {
    const auto &s=*impl_;EnergyHistoryTotals t;t.beginUtcMs=begin;t.endUtcMs=end;
    if(!s.ready)return t;
    return s.model.query(s.binding,begin,end,now);
}
EnergyHistoryTotals EnergyHistoryLedger::registerTotal(const std::string &reg,const std::string &period,int64_t through,int64_t now) const {
    const auto &s=*impl_;EnergyHistoryTotals t;
    if(!s.ready)return t;
    const auto i=s.model.windows.find({reg,period});
    if(i==s.model.windows.end()){t.status=EnergyHistoryStatus::Invalid;return t;}
    return s.model.query(s.binding,i->second.beginUtcMs,(std::min)(through,i->second.endUtcMs),now);
}
#ifdef CANARY_ENERGY_HISTORY_TESTING
void EnergyHistoryTestPeer::crashBeforeCommit(EnergyHistoryLedger &ledger){ledger.impl_->crash=true;}
#endif
} // namespace om
