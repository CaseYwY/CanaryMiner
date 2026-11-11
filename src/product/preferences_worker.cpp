// Fixed private preference service over inherited owned channels. No shell, argument paths, GPU or network operations.
#include "preferences_service.h"
#include "duplex_task_process.h"
#include "startup_config_service.h"
#include <chrono>
#include <thread>
int main(int argc,char **argv){using namespace om;
    try {
        DuplexTaskChannel channel;if(!channel.adopt(argc,argv))return 2;
        PreferencesService service;std::string input,output;size_t sent=0;bool first=true;
        while(channel.parentAlive()){
            if(!output.empty()){
                const auto result=channel.send(output.data()+sent,output.size()-sent);
                if(result.status==DuplexTaskIO::Closed||result.status==DuplexTaskIO::Error)return 1;
                if(result.status==DuplexTaskIO::Bytes){sent+=result.bytes;if(sent==output.size()){output.clear();sent=0;}}
            }else{
                char bytes[4096];const auto result=channel.receive(bytes,sizeof(bytes));
                if(result.status==DuplexTaskIO::Closed)return input.empty()?0:2;
                if(result.status==DuplexTaskIO::Error)return 1;
                if(result.status==DuplexTaskIO::Bytes){
                    if(result.bytes>preferencesFrameLimit-input.size())return 2;
                    input.append(bytes,result.bytes);const auto end=input.find('\n');
                    if(end!=input.npos){
                        if(end+1!=input.size())return 2; // exactly one frame in this batch; the parent separately owns one-flight admission
                        if(first){StartupConfigRequest config;if(decodeStartupConfigRequest(input,&config))return serveStartupConfigRead(channel,config);}
                        first=false;
                        PreferencesRequest request;PreferencesReply reply;
                        if(!decodePreferencesRequest(input,&request)||!channel.parentAlive()||!service.transact(request,&reply)||!encodePreferencesReply(reply,&output))return 2;
                        input.clear();
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }catch(...){return 1;}
    return 0;
}
