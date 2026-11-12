// Adversarial test-only IPC peer. Never linked into the native worker.
#include "product/pool_tls_rpc.h"
#include "product/duplex_task_process.h"
#include <thread>
using namespace om;
int main(int argc,char **argv){
    DuplexTaskChannel channel;if(!channel.adopt(argc,argv))return 2;
    PoolTlsRpcFramer framer;std::optional<PoolTlsRpcCommand> command;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!command&&channel.parentAlive()&&std::chrono::steady_clock::now()<deadline){
        char bytes[4096];const auto read=channel.receive(bytes,sizeof(bytes));
        if(read.status==DuplexTaskIO::Closed||read.status==DuplexTaskIO::Error)return 3;
        if(read.bytes&&!framer.feed(std::string_view(bytes,read.bytes),[&](std::string_view wire){PoolTlsRpcCommand c;
            if(command||!decodePoolTlsRpcCommand(wire,&c))return false;
            command=std::move(c);return true;}))return 4;
        if(!command)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if(!command)return 5;
    PoolTlsCheckRequest request;uint64_t parent=0;if(!decodePoolTlsCheckRequest(command->payload,&request,&parent)||parent!=channel.parentId())return 6;
    PoolTlsRpcReply reply{command->sequence,{PearlPoolTransferStatus::Error,0},PoolTlsRpcState::Failed,PearlTlsError::Transport,{}};
    reply.openFailure=PoolTlsCheckReply{PoolTlsCheckStatus::Connect,PoolTlsCheckStage::Connect,PearlTlsError::None};
    std::string wire;if(!encodePoolTlsRpcReply(*command,reply,&wire))return 7;
    if(request.host=="trailing-open.test")wire+="9\npartial";
    std::size_t offset=0;while(offset<wire.size()&&channel.parentAlive()&&std::chrono::steady_clock::now()<deadline){
        const auto sent=channel.send(wire.data()+offset,wire.size()-offset);
        if(sent.status==DuplexTaskIO::Closed||sent.status==DuplexTaskIO::Error)return 8;
        offset+=sent.bytes;if(offset<wire.size())std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if(request.host=="stall-after-failure.test")std::this_thread::sleep_for(std::chrono::seconds(30));
    return request.host=="nonzero-open.test"?9:0;
}
