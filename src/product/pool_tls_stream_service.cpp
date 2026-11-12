#include "pool_tls_stream_service.h"
#include "duplex_task_process.h"
#include <algorithm>
#include <array>
#include <thread>
namespace om {
namespace {
uint64_t nowMs(){return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
}
int runPoolTlsStreamService(int argc,char **argv,const PoolTlsOpenFactory &factory,const PoolTlsOpenFactory &plaintextFactory){
    using O=PoolTlsRpcOperation;using T=PearlPoolTransferStatus;using S=PoolTlsRpcState;using E=PearlTlsError;
    try{DuplexTaskChannel channel;if(!factory||!channel.adopt(argc,argv))return 2;
        PoolTlsRpcFramer frames;PoolTlsRpcGate gate;std::optional<PoolTlsRpcCommand> request;
        const std::atomic<bool> cancel{false};std::unique_ptr<PoolTlsConnection> connection;
        std::string output,write;std::size_t offset=0,writeOffset=0;bool done=false;
        while(channel.parentAlive()){
            if(!output.empty()){const auto sent=channel.send(output.data()+offset,std::min<std::size_t>(4096,output.size()-offset));
                if(sent.status==DuplexTaskIO::Closed||sent.status==DuplexTaskIO::Error)return 3;
                offset+=sent.bytes;if(offset==output.size()){output.clear();offset=0;if(done)return 0;}
                if(!output.empty()){std::this_thread::sleep_for(std::chrono::milliseconds(1));continue;}}
            char input[4096];const auto got=channel.receive(input,sizeof(input));
            if(got.status==DuplexTaskIO::Closed)return frames.complete()?0:4;
            if(got.status==DuplexTaskIO::Error)return 4;
            if(got.bytes&&!frames.feed(std::string_view(input,got.bytes),[&](std::string_view frame){PoolTlsRpcCommand c;
                if(request||!decodePoolTlsRpcCommand(frame,&c)||!gate.issue(c,nowMs()))return false;
                request=std::move(c);return true;}))return 5;
            if(!request){std::this_thread::sleep_for(std::chrono::milliseconds(1));continue;}
            const auto &c=*request;PoolTlsRpcReply r{c.sequence,{T::Progress,0},S::Dormant,E::None,{}};
            const auto failed=[&](E error){if(connection)connection->abort();r.transfer={T::Error,0};r.state=S::Failed;r.error=error;r.payload.clear();r.bufferedRead=false;r.ready=PoolTlsRpcReady::NotWait;};
            if(c.operation==O::Open||c.operation==O::OpenPlaintext){PoolTlsCheckRequest config;uint64_t parent=0;const auto current=nowMs();
                if(!decodePoolTlsCheckRequest(c.payload,&config,&parent)||parent!=channel.parentId())failed(E::InvalidPolicy);
                else if(current>=c.deadlineMonoMs||c.deadlineMonoMs-current<50)failed(E::Timeout);
                else{const bool plain=c.operation==O::OpenPlaintext;const auto &selected=plain?plaintextFactory:factory;
                    if(!selected)failed(E::Unavailable);
                    else{config.totalMs=static_cast<unsigned>(std::min<uint64_t>(config.totalMs,c.deadlineMonoMs-current));auto opened=selected(config,cancel);
                        if(opened.connection&&opened.failure)failed(E::Protocol);
                        else if(!opened.connection){if(opened.failure){failed(poolTlsRpcOpenError(*opened.failure));r.openFailure=opened.failure;}else failed(E::Unavailable);}
                        else if(opened.connection->state()!=(plain?S::PlaintextOpen:S::VerifiedOpen))failed(E::Protocol);
                        else connection=std::move(opened.connection);
                    }}}
            else if(!connection)failed(E::Protocol);
            else if(c.operation==O::Abort){connection->abort();r.state=S::Closed;}
            else if(c.operation==O::Wait){const auto ready=connection->waitFor(c.interest,c.waitMs);
                if(ready==PearlPoolWaitStatus::Error)failed(E::Transport);else r.ready=ready==PearlPoolWaitStatus::Ready?PoolTlsRpcReady::Ready:PoolTlsRpcReady::Timeout;}
            else if(c.operation==O::Write){if(write.empty()){write=c.payload;writeOffset=0;}
                if(write!=c.payload)failed(E::Protocol);else{r.transfer=connection->send(write.data()+writeOffset,write.size()-writeOffset);
                    if(r.transfer.status==T::Bytes){
                        if(!r.transfer.bytes||r.transfer.bytes>write.size()-writeOffset||r.transfer.interest!=PearlPoolInterest::None)failed(E::Protocol);
                        else{writeOffset+=r.transfer.bytes;
                            if(writeOffset==write.size()){r.transfer={T::Bytes,write.size()};write.clear();writeOffset=0;}
                            else r.transfer={T::Progress,0}; // retain the exact remainder; only a complete chunk is acknowledged
                        }
                    }}}
            else{r.payload.resize(c.maximum);r.transfer=connection->receive(r.payload.data(),r.payload.size());
                if(r.transfer.bytes>r.payload.size())failed(E::Protocol);else r.payload.resize(r.transfer.status==T::Bytes?r.transfer.bytes:0);}
            if(connection&&r.state!=S::Failed&&c.operation!=O::Abort){r.state=connection->state();r.error=connection->error();
                r.bufferedRead=connection->bufferedRead();
                if(r.transfer.status==T::Error&&r.error==E::None)failed(E::Protocol);}
            if(!encodePoolTlsRpcReply(c,r,&output)||!gate.settle(r,nowMs()))return 6;
            done=r.state==S::Closed||r.state==S::Failed;request.reset();
        }
        return 0;
    }catch(...){return 7;}
}
} // namespace om
