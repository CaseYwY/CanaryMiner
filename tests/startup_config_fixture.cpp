// Owned inherited-channel CPU fixture only. Deliberately corrupt receipts; never linked into the real helper.
#include "product/startup_config_protocol.h"
#include "product/duplex_task_process.h"
#include <algorithm>
#include <chrono>
#include <thread>
int main(int argc,char **argv){using namespace om;
    try{
        DuplexTaskChannel channel;if(!channel.adopt(argc,argv))return 2;
        std::string input;
        while(channel.parentAlive()){
            char bytes[4096];const auto result=channel.receive(bytes,sizeof(bytes));
            if(result.status==DuplexTaskIO::Closed||result.status==DuplexTaskIO::Error)return 2;
            if(result.bytes>startupConfigRequestLimit-input.size())return 2;
            input.append(bytes,result.bytes);
            if(input.find('\n')!=input.npos)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        StartupConfigRequest request;if(!decodeStartupConfigRequest(input,&request))return 2;
        const auto mode=std::filesystem::u8path(request.target).filename().u8string();
        const std::string document="{\"schema_version\":1,\"worker\":\"fixture-loaded\"}";
        StartupConfigHeader header;header.ticket=request.ticket;header.status=StartupConfigReadStatus::Loaded;header.bytes=document.size();
        if(mode=="wrong-ticket")++header.ticket;
        std::string wire;if(!encodeStartupConfigHeader(header,&wire))return 2;
        wire+=document;
        if(mode=="truncated")wire.pop_back();
        if(mode=="extra")wire+='x';
        if(mode=="bad-header")wire="{bad-private-header}\n";
        if(mode=="empty")wire.clear();
        size_t sent=0;
        while(sent<wire.size()&&channel.parentAlive()){
            const auto result=channel.send(wire.data()+sent,std::min<size_t>(4096,wire.size()-sent));
            if(result.status==DuplexTaskIO::Closed||result.status==DuplexTaskIO::Error)return 1;
            sent+=result.bytes;
            if(result.status==DuplexTaskIO::Pending)std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return mode=="nonzero"?1:0;
    }catch(...){return 1;}
}
