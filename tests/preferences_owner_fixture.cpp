// Separate CPU fixture only: real typed service + deliberately held/lost/malformed acknowledgement. Not linked into worker.
#include "product/preferences_service.h"
#include "product/duplex_task_process.h"
#include <chrono>
#include <fstream>
#include <thread>
#ifndef CANARY_PREFERENCES_FIXTURE_MODE
#error Explicit fixture mode required
#endif
int main(int argc,char **argv){using namespace om;
    try {
        DuplexTaskChannel channel;if(!channel.adopt(argc,argv))return 2;
        PreferencesService service;std::string input,output;size_t sent=0;std::array<std::string,8> targets{};
        const auto hold=[&](){while(channel.parentAlive())std::this_thread::sleep_for(std::chrono::milliseconds(5));};
        while(channel.parentAlive()){
            if(!output.empty()){
                const auto result=channel.send(output.data()+sent,output.size()-sent);
                if(result.status==DuplexTaskIO::Closed||result.status==DuplexTaskIO::Error)return 1;
                if(result.status==DuplexTaskIO::Bytes){sent+=result.bytes;if(sent==output.size()){output.clear();sent=0;}}
            }else{
                char bytes[4096];const auto result=channel.receive(bytes,sizeof(bytes));
                if(result.status==DuplexTaskIO::Closed)return 0;
                if(result.status==DuplexTaskIO::Error)return 1;
                if(result.status==DuplexTaskIO::Bytes){
                    if(result.bytes>preferencesFrameLimit-input.size())return 2;
                    input.append(bytes,result.bytes);const auto end=input.find('\n');
                    if(end!=input.npos){
                        if(end+1!=input.size())return 2;
                        PreferencesRequest request;PreferencesReply reply;
                        if(!decodePreferencesRequest(input,&request))return 2;
                        if(request.operation==PreferencesOperation::Init)targets=request.targets;
                        if(CANARY_PREFERENCES_FIXTURE_MODE==6&&request.operation==PreferencesOperation::Load){
                            const auto root=std::filesystem::u8path(targets[0]).parent_path();
                            std::ofstream(root/"fixture-preference-loads.txt",std::ios::app)<<request.ticket<<":"<<preferencesKindName(request.kind)<<"\n";
                            const auto release=root/("fixture-preference-release-"+std::to_string(request.ticket));
                            while(channel.parentAlive()&&!std::filesystem::exists(release))std::this_thread::sleep_for(std::chrono::milliseconds(2));
                            if(!channel.parentAlive())return 0;
                        }
                        if(CANARY_PREFERENCES_FIXTURE_MODE==3&&request.operation==PreferencesOperation::Load){
                            auto marker=std::filesystem::u8path(targets[static_cast<unsigned>(request.kind)]);marker+=".fixture-load-held";std::ofstream(marker).put('1');}
                        if((CANARY_PREFERENCES_FIXTURE_MODE==3&&request.operation==PreferencesOperation::Load)||
                           (CANARY_PREFERENCES_FIXTURE_MODE==5&&request.operation==PreferencesOperation::Save)){hold();return 0;}
                        if(!service.transact(request,&reply))return 2;
                        if(reply.status==PreferencesReplyStatus::Saved){
                            if(CANARY_PREFERENCES_FIXTURE_MODE==1){hold();return 0;}
                            if(CANARY_PREFERENCES_FIXTURE_MODE==2){++reply.ticket;++reply.review;}
                        }
                        if(CANARY_PREFERENCES_FIXTURE_MODE==4&&reply.status==PreferencesReplyStatus::Loaded)reply.document+=' ';
                        if(!encodePreferencesReply(reply,&output))return 2;
                        input.clear();
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }catch(...){return 1;}
    return 0;
}
