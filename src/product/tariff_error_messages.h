// Whole error envelopes; original raw reasons and caller-supplied human descriptions have distinct typed roles, not authority.
#pragma once
#include "localization.h"
namespace om { namespace i18n {
enum class TariffErrorMessageKey { OriginalPath, OriginalRoot, HumanPath, HumanRoot, Raw, Count };
constexpr std::size_t tariffErrorMessageKeyCount=static_cast<std::size_t>(TariffErrorMessageKey::Count);
struct TariffErrorLocalePack {std::string_view id;bool rtl;ReviewStatus review;std::array<std::string_view,tariffErrorMessageKeyCount> messages;};
inline const std::array<TariffErrorLocalePack,16> &tariffErrorLocalePacks(){
    static const std::array<TariffErrorLocalePack,16> packs={{
        {"en",false,ReviewStatus::Source,{{"{path}: {reason}",": {reason}","{path}: {reason}",": {reason}","{reason}"}}},
        {"zh-Hans",false,ReviewStatus::MachineAssisted,{{u8"{path}\uff1a{reason}",u8"\uff1a{reason}",u8"{path}\uff1a{reason}",u8"\uff1a{reason}","{reason}"}}},
        {"zh-Hant",false,ReviewStatus::MachineAssisted,{{u8"{path}\uff1a{reason}",u8"\uff1a{reason}",u8"{path}\uff1a{reason}",u8"\uff1a{reason}","{reason}"}}},
        {"es",false,ReviewStatus::MachineAssisted,{{"{path}: {reason}",": {reason}","{path}: {reason}",": {reason}","{reason}"}}},
        {"pt-BR",false,ReviewStatus::MachineAssisted,{{"{path}: {reason}",": {reason}","{path}: {reason}",": {reason}","{reason}"}}},
        {"fr",false,ReviewStatus::MachineAssisted,{{"{path} : {reason}",": {reason}","{path} : {reason}",": {reason}","{reason}"}}},
        {"de",false,ReviewStatus::MachineAssisted,{{"{path}: {reason}",": {reason}","{path}: {reason}",": {reason}","{reason}"}}},
        {"ru",false,ReviewStatus::MachineAssisted,{{"{path}: {reason}",": {reason}","{path}: {reason}",": {reason}","{reason}"}}},
        {"ja",false,ReviewStatus::MachineAssisted,{{u8"{path}\uff1a{reason}",u8"\uff1a{reason}",u8"{path}\uff1a{reason}",u8"\uff1a{reason}","{reason}"}}},
        {"ko",false,ReviewStatus::MachineAssisted,{{"{path}: {reason}",": {reason}","{path}: {reason}",": {reason}","{reason}"}}},
        {"ar",true,ReviewStatus::MachineAssisted,{{"{path}: {reason}",": {reason}","{path}: {reason}",": {reason}","{reason}"}}},
        {"hi",false,ReviewStatus::MachineAssisted,{{"{path}: {reason}",": {reason}","{path}: {reason}",": {reason}","{reason}"}}},
        {"bn",false,ReviewStatus::MachineAssisted,{{"{path}: {reason}",": {reason}","{path}: {reason}",": {reason}","{reason}"}}},
        {"id",false,ReviewStatus::MachineAssisted,{{"{path}: {reason}",": {reason}","{path}: {reason}",": {reason}","{reason}"}}},
        {"tr",false,ReviewStatus::MachineAssisted,{{"{path}: {reason}",": {reason}","{path}: {reason}",": {reason}","{reason}"}}},
        {"vi",false,ReviewStatus::MachineAssisted,{{"{path}: {reason}",": {reason}","{path}: {reason}",": {reason}","{reason}"}}}
    }};return packs;
}
inline std::string_view tariffErrorMessageKeyName(TariffErrorMessageKey key){
    static constexpr std::array<std::string_view,tariffErrorMessageKeyCount> names={{"tariff.error.original_path","tariff.error.original_root","tariff.error.human_path","tariff.error.human_root","tariff.error.raw"}};
    const auto index=static_cast<std::size_t>(key);return index<names.size()?names[index]:std::string_view{};
}
inline LocalizedMessage tariffErrorMessage(std::string_view language,TariffErrorMessageKey key){
    const auto index=static_cast<std::size_t>(key);if(index>=tariffErrorMessageKeyCount)return {};
    const auto &source=tariffErrorLocalePacks()[0];const auto *selected=&source;for(const auto &pack:tariffErrorLocalePacks())if(pack.id==language){selected=&pack;break;}
    if(selected->id!=language||!validTemplateTranslation(source.messages[index],selected->messages[index]))return {source.messages[index],source.id,source.review,true,false};
    return {selected->messages[index],selected->id,selected->review,false,selected->rtl};
}
// Source-only native preservation; not a sanitizer, classifier, terminal-safe renderer or disclosure boundary.
inline std::string tariffErrorEnglish(TariffErrorMessageKey key,const MessageArguments &arguments={}){
    const auto text=tariffErrorMessage("en",key).text;std::vector<std::string_view> expected,supplied;
    if(text.empty()||arguments.size()>2||!templateParameters(text,&expected))return {};
    for(const auto &arg:arguments)supplied.push_back(arg.first);
    std::sort(supplied.begin(),supplied.end());if(supplied!=expected)return {};
    std::string result;std::size_t at=0;
    while(at<text.size()){const auto start=text.find('{',at);if(start==text.npos){result.append(text.substr(at));break;}result.append(text.substr(at,start-at));const auto end=text.find('}',start);
        const auto name=text.substr(start+1,end-start-1);const auto found=std::find_if(arguments.begin(),arguments.end(),[&](const auto &arg){return arg.first==name;});if(found==arguments.end())return {};
        result+=found->second;at=end+1;
    }
    return result;
}
} } // namespace om::i18n
