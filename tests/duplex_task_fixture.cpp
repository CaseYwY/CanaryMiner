// Owned synthetic process only; no resolver, TLS, GPU or miner. Commands select fixture behavior on the private channel.
#include "product/duplex_task_process.h"
#include <algorithm>
#include <cstdlib>
#include <thread>
int main(int argc,char **argv){
    using namespace om;DuplexTaskChannel channel;if(!channel.adopt(argc,argv))return 2;
    if(std::getenv("CANARY_SYNTHETIC_SECRET")||std::getenv("SSLKEYLOGFILE")||std::getenv("HTTPS_PROXY"))return 3;
    char bytes[4096];bool selected=false;
    while(channel.parentAlive()){
        const auto got=channel.receive(bytes,sizeof(bytes));if(got.status==DuplexTaskIO::Closed)return 0;if(got.status==DuplexTaskIO::Error)return 4;
        if(got.status==DuplexTaskIO::Pending){std::this_thread::sleep_for(std::chrono::milliseconds(1));continue;}
        if(!selected){selected=true;
            if(bytes[0]=='P'||bytes[0]=='T'){
                const bool parent=bytes[0]=='P';const auto value=parent?std::to_string(localTaskProcessId())+"\n":std::string(12000,'t');
                std::size_t sent=0;while(sent<value.size()&&channel.parentAlive()){const auto wrote=channel.send(value.data()+sent,std::min<std::size_t>(4096,value.size()-sent));
                    if(wrote.status==DuplexTaskIO::Closed||wrote.status==DuplexTaskIO::Error)return 6;
                    sent+=wrote.bytes;if(wrote.status==DuplexTaskIO::Pending)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
                if(parent)std::this_thread::sleep_for(std::chrono::seconds(30));
                return 0;
            }
            if(bytes[0]=='S'){std::this_thread::sleep_for(std::chrono::seconds(30));return 0;}if(bytes[0]!='E')return 5;}
        std::size_t sent=0;while(sent<got.bytes&&channel.parentAlive()){const auto wrote=channel.send(bytes+sent,got.bytes-sent);
            if(wrote.status==DuplexTaskIO::Closed||wrote.status==DuplexTaskIO::Error)return 6;
            sent+=wrote.bytes;if(wrote.status==DuplexTaskIO::Pending)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    }
    return 0;
}
