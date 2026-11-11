// Helper-only stdout role: native writes can block here, never on the parent control thread.
#pragma once
#include "console_output_protocol.h"
#include "duplex_task_process.h"
#if defined(_WIN32)
#include "dashboard.h" // KeyReader/geometry are invoked only inside the creation-bound terminal helper
#endif
#include <chrono>
#include <thread>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <unistd.h>
#endif
namespace om {
static_assert(consoleOutputLimit+consoleOutputHeaderLimit<=duplexTaskFrameLimit,"Console frames must not widen private IPC");
inline bool writeConsoleOutput(DuplexTaskChannel &channel,const std::string &bytes){
    if(channel.outputRole()==DuplexTaskOutput::Discard)return false;
    std::size_t sent=0;
    while(sent<bytes.size()&&channel.parentAlive()){
#if defined(_WIN32)
        DWORD count=0;const auto length=static_cast<DWORD>(bytes.size()-sent);
        if(!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),bytes.data()+sent,length,&count,nullptr)||!count||count>length)return false;
#else
        const auto count=::write(STDOUT_FILENO,bytes.data()+sent,bytes.size()-sent);
        if(count<0&&errno==EINTR)continue;
        if(count<=0||static_cast<std::size_t>(count)>bytes.size()-sent)return false;
#endif
        sent+=static_cast<std::size_t>(count);
    }
    return sent==bytes.size()&&channel.parentAlive();
}
inline int serveConsoleOutput(DuplexTaskChannel &channel){
    if(channel.outputRole()==DuplexTaskOutput::Discard)return 2;
#if defined(_WIN32)
    KeyReader keys;
#endif
    std::string input;uint64_t previous=0;
    while(channel.parentAlive()){
        char bytes[4096];const auto got=channel.receive(bytes,sizeof(bytes));
        if(got.status==DuplexTaskIO::Closed)return input.empty()?0:2;
        if(got.status==DuplexTaskIO::Error)return 1;
        if(got.status==DuplexTaskIO::Pending){std::this_thread::sleep_for(std::chrono::milliseconds(2));continue;}
        if(got.bytes>consoleOutputLimit+consoleOutputHeaderLimit-input.size())return 2;
        input.append(bytes,got.bytes);const auto end=input.find('\n');
        if(end==input.npos){if(input.size()>=consoleOutputHeaderLimit)return 2;continue;}
        uint64_t ticket=0;std::size_t length=0;
        if(!console_output_detail::header(std::string_view(input).substr(0,end+1),"CMTO1 ",&ticket,&length)||previous==UINT64_MAX||ticket!=previous+1||(!previous&&length))return 2;
        const auto total=end+1+length;
        if(input.size()>total)return 2;
        if(input.size()<total)continue;
        std::string ack;
#if defined(_WIN32)
        if(channel.outputRole()==DuplexTaskOutput::InheritedTerminal){
            const auto columns=terminalColumns(80),rows=terminalRows(24);const auto key=keys.poll();
            ack=consoleTerminalAcknowledgement(ticket,length,static_cast<unsigned>(columns),static_cast<unsigned>(rows),static_cast<unsigned>(key));
        }else
#endif
        ack=consoleOutputAcknowledgement(ticket,length);
        if(!writeConsoleOutput(channel,input.substr(end+1)))return 1;
        std::size_t sent=0;
        while(sent<ack.size()&&channel.parentAlive()){
            const auto wrote=channel.send(ack.data()+sent,ack.size()-sent);
            if(wrote.status==DuplexTaskIO::Closed||wrote.status==DuplexTaskIO::Error)return 1;
            sent+=wrote.bytes;
            if(wrote.status==DuplexTaskIO::Pending)std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if(sent!=ack.size())return 1;
        previous=ticket;input.clear();
    }
    return 0;
}
} // namespace om
