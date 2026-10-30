#include "worker_identity.h"
#include <algorithm>
#include <charconv>
#include <set>
namespace om {
namespace {
bool integer(std::string_view text,uint64_t &value){
    if(text.empty()||text.size()>20||text[0]<'1'||text[0]>'9')return false;
    const auto read=std::from_chars(text.data(),text.data()+text.size(),value);return read.ec==std::errc{}&&read.ptr==text.data()+text.size();
}
}
bool workerIdentityRequest(const std::string &wire,uint64_t *parent,unsigned *count){
    if(!parent||!count||wire.size()>64||wire.size()<10||wire.compare(0,6,"CMWK1 ")||wire.back()!='\n')return false;
    const auto split=wire.find(' ',6);if(split==wire.npos)return false;
    uint64_t p=0,n=0;
    if(!integer(std::string_view(wire).substr(6,split-6),p)||p>UINT32_MAX||!integer(std::string_view(wire).substr(split+1,wire.size()-split-2),n)||n>257)return false;
    *parent=p;*count=static_cast<unsigned>(n);return true;
}
WorkerIdentityResult WorkerIdentitySource::request(unsigned count,const std::atomic<bool> &cancel,unsigned ms){
    WorkerIdentityResult out;if(attempted_){out.status=LocalTaskStatus::Busy;return out;}attempted_=true;
    if(cancel.load()){out.status=LocalTaskStatus::Cancelled;return out;}
    if(!count||count>257){out.status=LocalTaskStatus::Invalid;return out;}
    const auto result=process_.run("CMWK1 "+std::to_string(localTaskProcessId())+" "+std::to_string(count)+"\n",cancel,ms);
    out.status=result.status;if(result.status!=LocalTaskStatus::Ok)return out;
    const std::string prefix="CMWK1 "+std::to_string(count)+"\n";const auto &wire=result.response;
    if(wire.size()!=prefix.size()+33*count||wire.compare(0,prefix.size(),prefix)){out.status=LocalTaskStatus::Unavailable;return out;}
    std::set<std::string> unique;
    for(unsigned i=0;i<count;++i){const size_t at=prefix.size()+33*i;const auto key=wire.substr(at,32);
        if(wire[at+32]!='\n'||key.find_first_not_of('0')==key.npos||!std::all_of(key.begin(),key.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');})||!unique.insert(key).second){
            out.status=LocalTaskStatus::Unavailable;return out;}}
    if(cancel.load()){out.status=LocalTaskStatus::Cancelled;return out;}
    for(unsigned i=0;i<count;++i)out.keys.push_back(wire.substr(prefix.size()+33*i,32));
    return out;
}
} // namespace om
