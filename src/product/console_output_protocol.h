// Private bounded display bytes only. No file path, preference, network or worker operation is representable.
#pragma once
#include <charconv>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
namespace om {
constexpr std::size_t consoleOutputLimit=262144,consoleOutputHeaderLimit=64;
namespace console_output_detail {
inline bool number(std::string_view text,uint64_t *out){
    if(text.empty()||text.size()>20||(text.size()>1&&text.front()=='0')||text.front()<'0'||text.front()>'9')return false;
    uint64_t value=0;const auto result=std::from_chars(text.data(),text.data()+text.size(),value);
    if(result.ec!=std::errc{}||result.ptr!=text.data()+text.size())return false;
    *out=value;return true;
}
inline bool header(std::string_view wire,std::string_view magic,uint64_t *ticket,std::size_t *size){
    if(!ticket||!size||wire.empty()||wire.size()>consoleOutputHeaderLimit||wire.back()!='\n'||wire.substr(0,magic.size())!=magic)return false;
    wire.remove_prefix(magic.size());wire.remove_suffix(1);
    const auto space=wire.find(' ');uint64_t t=0,n=0;
    if(space==wire.npos||!number(wire.substr(0,space),&t)||!t||!number(wire.substr(space+1),&n)||n>consoleOutputLimit)return false;
    *ticket=t;*size=static_cast<std::size_t>(n);return true;
}
} // namespace console_output_detail
inline bool encodeConsoleOutput(uint64_t ticket,const std::string &bytes,std::string *wire){
    if(!wire||!ticket||bytes.size()>consoleOutputLimit)return false;
    std::string value="CMTO1 "+std::to_string(ticket)+" "+std::to_string(bytes.size())+"\n";value+=bytes;*wire=std::move(value);return true;
}
inline std::string consoleOutputAcknowledgement(uint64_t ticket,std::size_t size){
    return "CMTA1 "+std::to_string(ticket)+" "+std::to_string(size)+"\n";
}
inline std::string consoleTerminalAcknowledgement(uint64_t ticket,std::size_t size,unsigned columns,unsigned rows,unsigned key){
    return "CMTA2 "+std::to_string(ticket)+" "+std::to_string(size)+" "+std::to_string(columns)+" "+std::to_string(rows)+" "+std::to_string(key)+"\n";
}
inline bool decodeConsoleTerminalAcknowledgement(std::string_view wire,uint64_t *ticket,std::size_t *size,unsigned *columns,unsigned *rows,int *key){
    if(!ticket||!size||!columns||!rows||!key||wire.size()>consoleOutputHeaderLimit||wire.size()<7||wire.substr(0,6)!="CMTA2 "||wire.back()!='\n')return false;
    wire.remove_prefix(6);wire.remove_suffix(1);std::array<uint64_t,5> values{};
    for(size_t i=0;i<values.size();++i){const auto space=wire.find(' ');const bool last=i+1==values.size();
        if((last&&space!=wire.npos)||(!last&&space==wire.npos)||!console_output_detail::number(last?wire:wire.substr(0,space),&values[i]))return false;
        if(!last)wire.remove_prefix(space+1);
    }
    if(!values[0]||values[1]>consoleOutputLimit||!values[2]||values[2]>32767||!values[3]||values[3]>32767||
       (values[4]>255&&(values[4]<0x10001||values[4]>0x10008)))return false;
    *ticket=values[0];*size=static_cast<size_t>(values[1]);*columns=static_cast<unsigned>(values[2]);*rows=static_cast<unsigned>(values[3]);*key=static_cast<int>(values[4]);return true;
}
} // namespace om
