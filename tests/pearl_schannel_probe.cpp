// Explicit owned synthetic-memory peer harness. Never installed or used as a pool/credential runtime.
#include "core/pearl_tls_stream.h"
#include "core/pearl_tls_private_trust.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace om;
namespace {
constexpr std::size_t limit=65536;
std::string hex(const std::vector<char> &v){const char *digits="0123456789abcdef";std::string out;out.reserve(2*v.size());
    for(unsigned char c:v){out+=digits[c>>4];out+=digits[c&15];}return out;}
std::vector<char> unhex(const std::string &s){if(s.size()%2||s.size()>2*limit)throw std::runtime_error("invalid fixture bytes");std::vector<char> out;
    const auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
    for(std::size_t i=0;i<s.size();i+=2){const int a=digit(s[i]),b=digit(s[i+1]);if(a<0||b<0)throw std::runtime_error("invalid fixture hex");out.push_back(static_cast<char>(a*16+b));}return out;}
PearlTlsDer file(const char *path){std::ifstream in(std::filesystem::u8path(path),std::ios::binary|std::ios::ate);
    if(!in||in.tellg()<=0||in.tellg()>static_cast<std::streamoff>(limit))throw std::runtime_error("invalid synthetic DER");PearlTlsDer out(static_cast<std::size_t>(in.tellg()));
    in.seekg(0);in.read(reinterpret_cast<char *>(out.data()),static_cast<std::streamsize>(out.size()));if(!in)throw std::runtime_error("short fixture");return out;}
struct Wire final:PearlPoolTransport {
    std::vector<char> incoming,outgoing;bool opened=true,eof=false;std::size_t writeLimit=limit,readLimit=limit;
    PearlPoolConnectStatus open(const std::string &,int)override{return PearlPoolConnectStatus::ConnectFailure;}
    bool isOpen()const override{return opened;}
    void close()override{opened=false;}
    PearlPoolWaitStatus waitReadable(int)override{return incoming.empty()?PearlPoolWaitStatus::Timeout:PearlPoolWaitStatus::Ready;}
    PearlPoolWaitStatus waitWritable(int)override{return writeLimit?PearlPoolWaitStatus::Ready:PearlPoolWaitStatus::Timeout;}
    bool setNonblocking(bool on)override{return on;}
    PearlPoolTransfer send(const char *p,std::size_t n)override{if(!writeLimit)return {PearlPoolTransferStatus::WouldBlock,0};
        const auto size=std::min(n,writeLimit);if(size>limit-outgoing.size())return {PearlPoolTransferStatus::Error,0};
        outgoing.insert(outgoing.end(),p,p+size);return {PearlPoolTransferStatus::Bytes,size};}
    PearlPoolTransfer receive(char *p,std::size_t maximum)override{if(incoming.empty())return {eof?PearlPoolTransferStatus::EndOfStream:PearlPoolTransferStatus::WouldBlock,0};
        const auto n=std::min(maximum,std::min(readLimit,incoming.size()));if(!n)return {PearlPoolTransferStatus::WouldBlock,0};
        std::memcpy(p,incoming.data(),n);incoming.erase(incoming.begin(),incoming.begin()+n);return {PearlPoolTransferStatus::Bytes,n};}
};
}
int main(int argc,char **argv){
#if !defined(_WIN32) || !defined(CANARY_POOL_TLS_SCHANNEL) || !defined(CANARY_POOL_TLS_PRIVATE_TRUST_WINDOWS)
    (void)argc;(void)argv;return 77;
#else
    try{
        if(argc!=4)return 90;std::atomic<bool> cancel{false};Wire wire;PearlTlsStream tls(wire,cancel);
        auto trust=std::make_shared<PearlTlsPrivateTrustInput>();trust->enabled=true;trust->referenceDnsName=argv[3];trust->anchors={file(argv[1])};
        if(std::string(argv[2])!="-")trust->crls={file(argv[2])};
        PearlTlsPolicy policy;policy.enabled=true;policy.serverName=argv[3];policy.handshakeDeadlineMonoMs=5000;policy.windowsPrivateTrust=trust;
        uint64_t now=1000;std::vector<char> pending;std::array<char,2*limit+32> line;
        const auto reply=[&](PearlPoolTransfer r,const std::vector<char> &plain=std::vector<char>{}){
            std::cout<<"{\"status\":"<<static_cast<unsigned>(r.status)<<",\"bytes\":"<<r.bytes<<",\"interest\":"<<static_cast<unsigned>(r.interest)
                <<",\"state\":"<<static_cast<unsigned>(tls.state())<<",\"error\":"<<static_cast<unsigned>(tls.error())
                <<",\"cipher\":\""<<hex(wire.outgoing)<<"\",\"plain\":\""<<hex(plain)<<"\"}"<<std::endl;wire.outgoing.clear();};
        reply({tls.begin(policy,now)?PearlPoolTransferStatus::Progress:PearlPoolTransferStatus::Error,0});
        while(std::cin.getline(line.data(),static_cast<std::streamsize>(line.size()))){const std::string command(line.data());
            if(command=="Q")break;PearlPoolTransfer r{PearlPoolTransferStatus::Progress,0};std::vector<char> plain;
            if(command=="H")r=tls.handshake(now);
            else if(command.rfind("I ",0)==0){const auto bytes=unhex(command.substr(2));if(bytes.size()>limit-wire.incoming.size())return 91;wire.incoming.insert(wire.incoming.end(),bytes.begin(),bytes.end());}
            else if(command.rfind("W ",0)==0){if(!pending.empty())return 92;pending=unhex(command.substr(2));if(pending.empty())return 93;r=tls.send(pending.data(),pending.size(),now);}
            else if(command=="W"){if(pending.empty())return 94;r=tls.send(pending.data(),pending.size(),now);}
            else if(command=="R"){plain.resize(97);r=tls.receive(plain.data(),plain.size(),now);plain.resize(r.status==PearlPoolTransferStatus::Bytes?r.bytes:0);}
            else if(command=="C")cancel=true;
            else if(command=="E")wire.eof=true;
            else if(command.rfind("S ",0)==0)wire.writeLimit=std::min<std::size_t>(limit,std::stoull(command.substr(2)));
            else if(command.rfind("F ",0)==0)wire.readLimit=std::min<std::size_t>(limit,std::stoull(command.substr(2)));
            else if(command.rfind("N ",0)==0)now=std::stoull(command.substr(2));
            else return 95;
            if(!command.empty()&&command[0]=='W'&&r.status==PearlPoolTransferStatus::Bytes){if(!r.bytes||r.bytes>pending.size())return 96;pending.erase(pending.begin(),pending.begin()+r.bytes);}
            reply(r,plain);
        }
        return 0;
    }catch(...){return 97;}
#endif
}
