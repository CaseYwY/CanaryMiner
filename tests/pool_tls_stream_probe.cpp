// Explicit local synthetic peer driver. Original native request arrives on stdin, never a public argument/log.
#include "product/pool_tls_process_transport.h"
#include "core/pearl_pool.h"
#include <cstdio>
#include <iostream>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif
int main(int argc,char **argv){using namespace om;
    if(argc!=2&&argc!=3)return 2;
    try{
#if defined(_WIN32)
        if(_setmode(_fileno(stdin),_O_BINARY)<0)return 3;
#endif
        std::string wire;char bytes[4096];for(;;){const auto n=std::fread(bytes,1,sizeof(bytes),stdin);if(n>localTaskRequestLimit-wire.size())return 3;wire.append(bytes,n);if(n<sizeof(bytes))break;}
        PoolTlsCheckRequest config;uint64_t parent=0;if(!decodePoolTlsCheckRequest(wire,&config,&parent))return 4;
        std::atomic<bool> cancel{false};PoolTlsProcessTransport transport(std::filesystem::u8path(argv[1]),config,cancel);bool ok=false;
        if(argc==3&&std::string(argv[2])=="canonical"){
            PearlPoolSource source(config.host,static_cast<int>(config.port),"placeholder","fixture",{PearlNetwork::Mainnet,PearlTargetSemantics::BaseBeforePenalty},"",&transport);
            Job job;const bool fetched=source.fetch(&job);if(fetched&&job.valid&&job.jobId=="native-rpc-job"){Solution proof;proof.nonce=7;proof.hit[0]=11;proof.extra="synthetic-proof";std::string error;
                ok=source.submit(job,proof,&error)&&source.submitCounters().accepted==1;
            }
        }else if(transport.open(config.host,static_cast<int>(config.port))==PearlPoolConnectStatus::Connected){
            std::string original(32779,'x');for(std::size_t i=0;i<original.size();++i)original[i]=static_cast<char>(i%256);std::size_t sent=0;
            for(unsigned turns=0;sent<original.size()&&turns<512;++turns){const auto r=transport.send(original.data()+sent,original.size()-sent);
                if(r.status==PearlPoolTransferStatus::Error)break;
                if(r.status==PearlPoolTransferStatus::Bytes)sent+=r.bytes;else if(r.status==PearlPoolTransferStatus::WouldBlock)transport.waitFor(r.interest,1000);}
            std::string received;for(unsigned turns=0;sent==original.size()&&received.size()<original.size()&&turns<512;++turns){const auto r=transport.receive(bytes,sizeof(bytes));
                if(r.status==PearlPoolTransferStatus::Error)break;
                if(r.status==PearlPoolTransferStatus::Bytes)received.append(bytes,r.bytes);else if(r.status==PearlPoolTransferStatus::WouldBlock)transport.waitFor(r.interest,1000);}
            ok=sent==original.size()&&received==original;
        }
        std::cout<<"{\"ok\":"<<(ok?1:0)<<",\"tls\":"<<static_cast<unsigned>(transport.error())<<"}"<<std::endl;return 0;
    }catch(...){return 5;}
}
