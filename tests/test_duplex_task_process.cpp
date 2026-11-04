#include "product/duplex_task_process.h"
#include "product_test_util.h"
#include <thread>
using namespace om;
int main(int argc,char **argv){
    if(argc!=2)return 2;
    const auto helper=std::filesystem::u8path(argv[1]);std::atomic<bool> cancel{false};
    {DuplexTaskProcess p;p.requestStop();CHECK(!p.start(helper,cancel));}
    {DuplexTaskProcess p;cancel=true;CHECK(!p.start(helper,cancel));cancel=false;CHECK(!p.start(helper,cancel));}
    {DuplexTaskProcess p;CHECK(!p.start("relative-executable",cancel));}
    for(std::size_t size:{1u,4096u,65536u,262408u}){DuplexTaskProcess p;CHECK(p.start(helper,cancel)&&!p.start(helper,cancel));
        std::string bytes(size,'x');bytes[0]='E';for(std::size_t i=1;i<size;++i)bytes[i]=static_cast<char>(i%256);
        CHECK(p.send(bytes)&&!p.send("overlap"));std::string received;bool flushed=false;
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(received.size()<bytes.size()&&std::chrono::steady_clock::now()<until){const auto r=p.poll();CHECK(r.state==DuplexTaskState::Running&&!r.channelFault&&r.bytes.size()<=4096);
            received+=r.bytes;flushed=r.flushed;if(r.bytes.empty())std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        CHECK(flushed&&received==bytes);CHECK(p.terminate());CHECK(!p.start(helper,cancel));}
    {DuplexTaskProcess p;CHECK(p.start(helper,cancel));CHECK(!p.send(std::string(duplexTaskFrameLimit+1,'x')));CHECK(p.send("S"));
        for(unsigned i=0;i<20;++i){p.poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        CHECK(p.send(std::string(duplexTaskFrameLimit,'b')));const auto pollBegin=std::chrono::steady_clock::now();bool flushed=false;
        for(unsigned i=0;i<100;++i){const auto r=p.poll();CHECK(r.state==DuplexTaskState::Running&&!r.channelFault);flushed=r.flushed;}
        CHECK(!flushed&&std::chrono::steady_clock::now()-pollBegin<std::chrono::seconds(2));
        const auto begin=std::chrono::steady_clock::now();CHECK(p.terminate());CHECK(std::chrono::steady_clock::now()-begin<std::chrono::seconds(2));}
    {DuplexTaskProcess p;CHECK(p.start(helper,cancel)&&p.send("T"));std::string tail;bool complete=false;
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(std::chrono::steady_clock::now()<until){const auto r=p.poll();CHECK(!r.channelFault&&r.bytes.size()<=4096);tail+=r.bytes;
            if(r.eof&&r.state==DuplexTaskState::Exited){complete=r.exitCode==0;break;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        CHECK(complete&&tail==std::string(12000,'t')&&p.terminate());}
    {DuplexTaskChannel channel;char bytes[4];CHECK(!channel.parentAlive()&&channel.receive(bytes,sizeof(bytes)).status==DuplexTaskIO::Error);}
    return finishTests("owned duplex task process (synthetic child only)");
}
