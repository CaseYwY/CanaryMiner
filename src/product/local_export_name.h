#pragma once
#include <string>
#include <string_view>

namespace om {
inline bool localExportName(const std::string &value,std::string_view suffix){
    const auto alnum=[](char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9');};
    if(value.size()<=suffix.size()||value.size()>96||!alnum(value[0])||std::string_view(value).substr(value.size()-suffix.size())!=suffix||value.find("..")!=value.npos)return false;
    for(char c:value)if(!alnum(c)&&c!='.'&&c!='_'&&c!='-')return false;
    std::string stem=value.substr(0,value.find('.'));
    for(char &c:stem)if(c>='a'&&c<='z')c=static_cast<char>(c-'a'+'A');
    if(stem=="CON"||stem=="PRN"||stem=="AUX"||stem=="NUL")return false;
    if(stem.size()>3&&(stem.substr(0,3)=="COM"||stem.substr(0,3)=="LPT")){
        bool digits=true;for(size_t i=3;i<stem.size();++i)if(stem[i]<'0'||stem[i]>'9')digits=false;
        if(digits)return false;
    }
    return true;
}

}  // namespace om
