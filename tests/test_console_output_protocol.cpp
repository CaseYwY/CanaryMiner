#include "product/console_output_protocol.h"
#include "product_test_util.h"
int main(){using namespace om;
    for(std::size_t size:{0u,1u,4096u,262144u}){
        std::string bytes(size,'x');for(std::size_t i=0;i<size;++i)bytes[i]=static_cast<char>(i%256);
        std::string wire="preserved";CHECK(encodeConsoleOutput(UINT64_MAX,bytes,&wire));
        const auto end=wire.find('\n');uint64_t ticket=0;std::size_t n=0;
        CHECK(console_output_detail::header(std::string_view(wire).substr(0,end+1),"CMTO1 ",&ticket,&n)&&ticket==UINT64_MAX&&n==size&&wire.substr(end+1)==bytes);
        CHECK(console_output_detail::header(consoleOutputAcknowledgement(ticket,n),"CMTA1 ",&ticket,&n)&&ticket==UINT64_MAX&&n==size);
    }
    for(const char *bad:{"","CMTO1 1 0","CMTO1 0 0\n","CMTO1 01 0\n","CMTO1 1 00\n","CMTO1 1 -1\n","CMTO1 +1 0\n","CMTO1 1 262145\n","CMTO1 18446744073709551616 0\n","CMTO1 1 0\r\n","CMTO1 1 0\nextra","CMTA1 1 0\n","CMTO1 1 0 \n"}){
        uint64_t ticket=77;std::size_t size=88;CHECK(!console_output_detail::header(bad,"CMTO1 ",&ticket,&size)&&ticket==77&&size==88);
    }
    std::string wire="preserved";CHECK(!encodeConsoleOutput(0,"",&wire)&&wire=="preserved");
    CHECK(!encodeConsoleOutput(1,std::string(consoleOutputLimit+1,'x'),&wire)&&wire=="preserved");
    CHECK(!encodeConsoleOutput(1,"",nullptr));
    for(unsigned value:{0u,113u,255u,0x10001u,0x10008u}){
        uint64_t ticket=0;std::size_t size=0;unsigned columns=0,rows=0;int key=-1;
        const auto ack=consoleTerminalAcknowledgement(UINT64_MAX,consoleOutputLimit,32767,32767,value);
        CHECK(decodeConsoleTerminalAcknowledgement(ack,&ticket,&size,&columns,&rows,&key)&&ticket==UINT64_MAX&&size==consoleOutputLimit&&columns==32767&&rows==32767&&key==static_cast<int>(value));
        CHECK(!console_output_detail::header(ack,"CMTA1 ",&ticket,&size));
    }
    for(const char *bad:{"","CMTA1 1 0\n","CMTA2 1 0 80 24 0","CMTA2 0 0 80 24 0\n","CMTA2 01 0 80 24 0\n","CMTA2 1 262145 80 24 0\n",
        "CMTA2 1 0 0 24 0\n","CMTA2 1 0 80 32768 0\n","CMTA2 1 0 80 24 65536\n","CMTA2 1 0 80 24 65545\n","CMTA2 1 0 80 24 -1\n",
        "CMTA2 1 0 80 24 00\n","CMTA2 1 0 80 24 0 \n","CMTA2 1 0 80 24 0\r\n","CMTA2 1 0 80 24 0\nextra"}){
        uint64_t ticket=7;std::size_t size=8;unsigned columns=9,rows=10;int key=11;
        CHECK(!decodeConsoleTerminalAcknowledgement(bad,&ticket,&size,&columns,&rows,&key)&&ticket==7&&size==8&&columns==9&&rows==10&&key==11);
    }
    return finishTests("console byte protocol: canonical length/ticket, binary originals and bounds");
}
