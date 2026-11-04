#include "pool_tls_check.h"
#include <algorithm>
#include <limits>
namespace om {
namespace {
using S=PoolTlsCheckStatus;using G=PoolTlsCheckStage;using E=PearlTlsError;
constexpr std::size_t materialLimit=96*1024;
bool dns(const std::string &s){
    if(s.empty()||s.size()>253||s.back()=='.'||s.find_first_not_of("0123456789.")==s.npos)return false;
    std::size_t start=0;
    for(std::size_t i=0;i<=s.size();++i){if(i==s.size()||s[i]=='.'){
            if(i==start||i-start>63||s[start]=='-'||s[i-1]=='-')return false;
            start=i+1;
        }else{const auto c=static_cast<unsigned char>(s[i]);if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'))return false;}}
    return true;
}
void number(std::string &s,uint64_t n){s+=std::to_string(n);s+='\n';}
void blob(std::string &s,const char *p,std::size_t size){number(s,size);s.append(p,size);s+='\n';}
struct Reader {
    const std::string &s;std::size_t at=0;
    bool number(uint64_t maximum,uint64_t &out){const auto end=s.find('\n',at);
        if(end==s.npos||end==at||end-at>20||(end-at>1&&s[at]=='0'))return false;
        uint64_t value=0;for(auto i=at;i<end;++i){const auto c=s[i];
            if(c<'0'||c>'9'||static_cast<unsigned>(c-'0')>maximum||value>(maximum-static_cast<unsigned>(c-'0'))/10)return false;
            value=value*10+static_cast<unsigned>(c-'0');}
        at=end+1;out=value;return true;
    }
    bool blob(std::size_t maximum,std::string &out){uint64_t n=0;if(!number(maximum,n)||n>=s.size()-at||s[at+static_cast<std::size_t>(n)]!='\n')return false;
        out=s.substr(at,static_cast<std::size_t>(n));at+=static_cast<std::size_t>(n)+1;return true;}
    bool collection(std::vector<PearlTlsDer> &out){uint64_t count=0;if(!number(16,count))return false;
        for(uint64_t i=0;i<count;++i){std::string b;if(!blob(65536,b)||b.empty())return false;out.emplace_back(b.begin(),b.end());}return true;}
};
bool validReply(const PoolTlsCheckReply &r){
    if(static_cast<unsigned>(r.status)>static_cast<unsigned>(S::Verified)||static_cast<unsigned>(r.stage)>static_cast<unsigned>(G::Complete)||
        static_cast<unsigned>(r.tlsError)>static_cast<unsigned>(E::Capacity))return false;
    if(r.status==S::Tls)return r.stage==G::Handshake&&r.tlsError!=E::None;
    if(r.tlsError!=E::None)return false;
    switch(r.status){
        case S::Disabled:case S::Unavailable:case S::Invalid:return r.stage==G::None;
        case S::Dns:return r.stage==G::Resolve;
        case S::Socket:case S::Connect:return r.stage==G::Connect;
        case S::Verified:return r.stage==G::Complete;
        case S::Cancelled:case S::Timeout:return r.stage!=G::Complete;
        default:return false;
    }
}
}
bool validPoolTlsCheckRequest(const PoolTlsCheckRequest &r){
    if(!dns(r.host)||!r.port||r.port>65535||r.totalMs<50||r.totalMs>60000||r.caPem.size()>65536)return false;
    if(r.windowsPrivateTrust){const auto &p=*r.windowsPrivateTrust;
        if(!r.caPem.empty()||!p.enabled||p.referenceDnsName!=r.host||!p.leaf.empty()||p.anchors.empty()||p.anchors.size()>16||p.intermediates.size()>16||p.crls.size()>16)return false;
        std::size_t total=0;
        for(const auto *v:{&p.anchors,&p.intermediates,&p.crls})for(const auto &b:*v){if(b.empty()||b.size()>65536||(total+=b.size())>materialLimit)return false;}
    }
    return true;
}
bool encodePoolTlsCheckRequest(const PoolTlsCheckRequest &r,uint64_t parent,std::string *out){
    if(!out||!parent||parent>UINT32_MAX||!validPoolTlsCheckRequest(r))return false;
    std::string s="CMTLSQ1\n";number(s,parent);number(s,r.enabled?1:0);blob(s,r.host.data(),r.host.size());number(s,r.port);number(s,r.totalMs);
    blob(s,r.caPem.data(),r.caPem.size());number(s,r.windowsPrivateTrust?1:0);
    if(r.windowsPrivateTrust){const auto &p=*r.windowsPrivateTrust;for(const auto *v:{&p.anchors,&p.intermediates,&p.crls}){
        number(s,v->size());for(const auto &b:*v)blob(s,reinterpret_cast<const char *>(b.data()),b.size());}}
    if(s.size()>localTaskRequestLimit)return false;
    *out=std::move(s);return true;
}
bool decodePoolTlsCheckRequest(const std::string &s,PoolTlsCheckRequest *out,uint64_t *parent){
    if(!out||!parent||s.size()>localTaskRequestLimit||s.rfind("CMTLSQ1\n",0)!=0)return false;
    Reader read{s,8};uint64_t pid=0,on=0,port=0,ms=0,custom=0;PoolTlsCheckRequest r;
    if(!read.number(UINT32_MAX,pid)||!pid||!read.number(1,on)||!read.blob(253,r.host)||!read.number(65535,port)||!read.number(60000,ms)||
        !read.blob(65536,r.caPem)||!read.number(1,custom))return false;
    r.enabled=on!=0;r.port=static_cast<unsigned>(port);r.totalMs=static_cast<unsigned>(ms);
    if(custom){auto p=std::make_shared<PearlTlsPrivateTrustInput>();p->enabled=true;p->referenceDnsName=r.host;
        if(!read.collection(p->anchors)||!read.collection(p->intermediates)||!read.collection(p->crls))return false;
        r.windowsPrivateTrust=std::move(p);}
    if(read.at!=s.size()||!validPoolTlsCheckRequest(r))return false;
    *out=std::move(r);*parent=pid;return true;
}
bool encodePoolTlsCheckReply(const PoolTlsCheckReply &r,std::string *out){
    if(!out||!validReply(r))return false;
    std::string s="CMTLSA1\n";number(s,static_cast<unsigned>(r.status));number(s,static_cast<unsigned>(r.stage));number(s,static_cast<unsigned>(r.tlsError));*out=std::move(s);return true;
}
bool decodePoolTlsCheckReply(const std::string &s,PoolTlsCheckReply *out){
    if(!out||s.size()>64||s.rfind("CMTLSA1\n",0)!=0)return false;
    Reader read{s,8};uint64_t status=0,stage=0,error=0;
    if(!read.number(static_cast<unsigned>(S::Verified),status)||!read.number(static_cast<unsigned>(G::Complete),stage)||!read.number(static_cast<unsigned>(E::Capacity),error)||read.at!=s.size())return false;
    PoolTlsCheckReply r{static_cast<S>(status),static_cast<G>(stage),static_cast<E>(error)};if(!validReply(r))return false;*out=r;return true;
}
PoolTlsCheckResult PoolTlsCheckClient::inspect(const PoolTlsCheckRequest &r,const std::atomic<bool> &cancel){
    PoolTlsCheckResult out;if(attempted_.test_and_set()){out.transport=LocalTaskStatus::Invalid;return out;}
    if(cancel.load()){out.transport=LocalTaskStatus::Cancelled;return out;}
    if(!r.enabled){out.transport=LocalTaskStatus::Ok;out.reply=PoolTlsCheckReply{S::Disabled,G::None,E::None};return out;}
    std::string wire;if(!encodePoolTlsCheckRequest(r,localTaskProcessId(),&wire)){out.transport=LocalTaskStatus::Invalid;return out;}
    const auto task=process_.run(wire,cancel,r.totalMs);out.transport=task.status;out.childStarted=task.childStarted;
    if(task.status==LocalTaskStatus::Ok){PoolTlsCheckReply reply;if(!decodePoolTlsCheckReply(task.response,&reply))out.transport=LocalTaskStatus::Unavailable;
        else if(cancel.load())out.transport=LocalTaskStatus::Cancelled;else out.reply=reply;}
    return out;
}
} // namespace om
