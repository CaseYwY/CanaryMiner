// Helper-side only: one exact configured file, read-only. No store, repair, search, source or device access.
#pragma once
#include "startup_config_protocol.h"
#include "duplex_task_process.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <thread>

namespace om {
inline bool readStartupConfigDocument(const StartupConfigRequest &request,StartupConfigHeader *header,std::string *document){
    std::string canonical;
    if(!header||!document||!encodeStartupConfigRequest(request,&canonical))return false;
    const auto target=std::filesystem::u8path(request.target);
    if(!localTaskPath(target))return false;
    StartupConfigHeader next;next.ticket=request.ticket;std::string bytes;
    std::ifstream input(target,std::ios::binary);
    if(input){
        bytes.resize(startupConfigDocumentLimit+1);
        input.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));
        const auto count=input.gcount();
        if(count<0||input.bad())bytes.clear();
        else if(static_cast<uint64_t>(count)>startupConfigDocumentLimit){next.status=StartupConfigReadStatus::Oversized;bytes.clear();}
        else{bytes.resize(static_cast<size_t>(count));next.status=StartupConfigReadStatus::Loaded;next.bytes=bytes.size();}
    }
    *header=next;*document=std::move(bytes);return true;
}
inline int serveStartupConfigRead(DuplexTaskChannel &channel,const StartupConfigRequest &request){
    if(!channel.parentAlive())return 2;
    StartupConfigHeader header;std::string document,wire;
    if(!readStartupConfigDocument(request,&header,&document)||!channel.parentAlive()||!encodeStartupConfigHeader(header,&wire))return 2;
    wire+=document; // bounded header followed by exactly bytes private original bytes, not another JSON document/frame
    size_t sent=0;
    while(sent<wire.size()&&channel.parentAlive()){
        const auto result=channel.send(wire.data()+sent,std::min<size_t>(4096,wire.size()-sent));
        if(result.status==DuplexTaskIO::Closed||result.status==DuplexTaskIO::Error||result.bytes>wire.size()-sent)return 1;
        sent+=result.bytes;
        if(result.status==DuplexTaskIO::Pending)std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return sent==wire.size()?0:1; // one read/reply, then exit; no later preference/config request is admitted
}
} // namespace om
