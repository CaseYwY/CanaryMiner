// Manual saved-request and editor envelopes; missing-price literals and human toggles are not numeric facts.
#pragma once
#include "localization.h"
namespace om { namespace i18n {
enum class ManualMarketValueMessageKey { FieldValue, FieldChange, HumanFieldValue, HumanFieldChange, MissingFieldValue, FieldBeforeOnly, FieldAfterOnly, EditHeading, EditInput, On, Off, NotSupplied, Count };
constexpr std::size_t manualMarketValueMessageKeyCount=static_cast<std::size_t>(ManualMarketValueMessageKey::Count);
struct ManualMarketValueLocalePack {std::string_view id;bool rtl;ReviewStatus review;std::array<std::string_view,manualMarketValueMessageKeyCount> messages;};
inline const std::array<ManualMarketValueLocalePack,16> &manualMarketValueLocalePacks(){
    static const std::array<ManualMarketValueLocalePack,16> packs={{
        {"en",false,ReviewStatus::Source,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}","{field}: not supplied","{field}: {before} -> not supplied","{field}: not supplied -> {after}","Edit {field}","> {input}_","on","off","not supplied"}}},
        {"zh-Hans",false,ReviewStatus::MachineAssisted,{{u8"{field}\uff1a{value}",u8"{field}\uff1a{before} -> {after}",u8"{field}\uff1a{value}",u8"{field}\uff1a{before} -> {after}",u8"{field}\uff1a\u672a\u63d0\u4f9b",u8"{field}\uff1a{before} -> \u672a\u63d0\u4f9b",u8"{field}\uff1a\u672a\u63d0\u4f9b -> {after}",u8"\u7f16\u8f91 {field}","> {input}_",u8"\u5f00\u542f",u8"\u5173\u95ed",u8"\u672a\u63d0\u4f9b"}}},
        {"zh-Hant",false,ReviewStatus::MachineAssisted,{{u8"{field}\uff1a{value}",u8"{field}\uff1a{before} -> {after}",u8"{field}\uff1a{value}",u8"{field}\uff1a{before} -> {after}",u8"{field}\uff1a\u672a\u63d0\u4f9b",u8"{field}\uff1a{before} -> \u672a\u63d0\u4f9b",u8"{field}\uff1a\u672a\u63d0\u4f9b -> {after}",u8"\u7de8\u8f2f {field}","> {input}_",u8"\u958b\u555f",u8"\u95dc\u9589",u8"\u672a\u63d0\u4f9b"}}},
        {"es",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}","{field}: no suministrado","{field}: {before} -> no suministrado","{field}: no suministrado -> {after}","Editar {field}","> {input}_","activado","desactivado","no suministrado"}}},
        {"pt-BR",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}",u8"{field}: n\u00e3o fornecido",u8"{field}: {before} -> n\u00e3o fornecido",u8"{field}: n\u00e3o fornecido -> {after}","Editar {field}","> {input}_","ativado","desativado",u8"n\u00e3o fornecido"}}},
        {"fr",false,ReviewStatus::MachineAssisted,{{"{field} : {value}","{field} : {before} -> {after}","{field} : {value}","{field} : {before} -> {after}","{field} : non fourni","{field} : {before} -> non fourni","{field} : non fourni -> {after}","Modifier {field}","> {input}_",u8"activ\u00e9",u8"d\u00e9sactiv\u00e9","non fourni"}}},
        {"de",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}","{field}: nicht angegeben","{field}: {before} -> nicht angegeben","{field}: nicht angegeben -> {after}","Bearbeiten: {field}","> {input}_","an","aus","nicht angegeben"}}},
        {"ru",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}",u8"{field}: \u043d\u0435 \u0437\u0430\u0434\u0430\u043d\u043e",u8"{field}: {before} -> \u043d\u0435 \u0437\u0430\u0434\u0430\u043d\u043e",u8"{field}: \u043d\u0435 \u0437\u0430\u0434\u0430\u043d\u043e -> {after}",u8"\u041f\u0440\u0430\u0432\u0438\u0442\u044c {field}","> {input}_",u8"\u0432\u043a\u043b",u8"\u0432\u044b\u043a\u043b",u8"\u043d\u0435 \u0437\u0430\u0434\u0430\u043d\u043e"}}},
        {"ja",false,ReviewStatus::MachineAssisted,{{u8"{field}\uff1a{value}",u8"{field}\uff1a{before} -> {after}",u8"{field}\uff1a{value}",u8"{field}\uff1a{before} -> {after}",u8"{field}\uff1a\u672a\u5165\u529b",u8"{field}\uff1a{before} -> \u672a\u5165\u529b",u8"{field}\uff1a\u672a\u5165\u529b -> {after}",u8"{field} \u3092\u7de8\u96c6","> {input}_",u8"\u6709\u52b9",u8"\u7121\u52b9",u8"\u672a\u5165\u529b"}}},
        {"ko",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}",u8"{field}: \ubbf8\uc785\ub825",u8"{field}: {before} -> \ubbf8\uc785\ub825",u8"{field}: \ubbf8\uc785\ub825 -> {after}",u8"{field} \ud3b8\uc9d1","> {input}_",u8"\ucf1c\uc9d0",u8"\uaebc\uc9d0",u8"\ubbf8\uc785\ub825"}}},
        {"ar",true,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}",u8"{field}: \u063a\u064a\u0631 \u0645\u0642\u062f\u0645",u8"{field}: {before} -> \u063a\u064a\u0631 \u0645\u0642\u062f\u0645",u8"{field}: \u063a\u064a\u0631 \u0645\u0642\u062f\u0645 -> {after}",u8"\u062a\u0639\u062f\u064a\u0644 {field}","> {input}_",u8"\u062a\u0634\u063a\u064a\u0644",u8"\u0625\u064a\u0642\u0627\u0641",u8"\u063a\u064a\u0631 \u0645\u0642\u062f\u0645"}}},
        {"hi",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}",u8"{field}: \u0928\u0939\u0940\u0902 \u0926\u093f\u092f\u093e",u8"{field}: {before} -> \u0928\u0939\u0940\u0902 \u0926\u093f\u092f\u093e",u8"{field}: \u0928\u0939\u0940\u0902 \u0926\u093f\u092f\u093e -> {after}",u8"{field} \u0938\u0902\u092a\u093e\u0926\u093f\u0924 \u0915\u0930\u0947\u0902","> {input}_",u8"\u091a\u093e\u0932\u0942",u8"\u092c\u0902\u0926",u8"\u0928\u0939\u0940\u0902 \u0926\u093f\u092f\u093e"}}},
        {"bn",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}",u8"{field}: \u09a6\u09c7\u0993\u09af\u09bc\u09be \u09b9\u09af\u09bc\u09a8\u09bf",u8"{field}: {before} -> \u09a6\u09c7\u0993\u09af\u09bc\u09be \u09b9\u09af\u09bc\u09a8\u09bf",u8"{field}: \u09a6\u09c7\u0993\u09af\u09bc\u09be \u09b9\u09af\u09bc\u09a8\u09bf -> {after}",u8"{field} \u09b8\u09ae\u09cd\u09aa\u09be\u09a6\u09a8\u09be","> {input}_",u8"\u099a\u09be\u09b2\u09c1",u8"\u09ac\u09a8\u09cd\u09a7",u8"\u09a6\u09c7\u0993\u09af\u09bc\u09be \u09b9\u09af\u09bc\u09a8\u09bf"}}},
        {"id",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}","{field}: belum diberikan","{field}: {before} -> belum diberikan","{field}: belum diberikan -> {after}","Edit {field}","> {input}_","aktif","nonaktif","belum diberikan"}}},
        {"tr",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}","{field}: verilmedi","{field}: {before} -> verilmedi","{field}: verilmedi -> {after}",u8"{field} d\u00fczenle","> {input}_",u8"a\u00e7\u0131k",u8"kapal\u0131","verilmedi"}}},
        {"vi",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}",u8"{field}: ch\u01b0a cung c\u1ea5p",u8"{field}: {before} -> ch\u01b0a cung c\u1ea5p",u8"{field}: ch\u01b0a cung c\u1ea5p -> {after}",u8"S\u1eeda {field}","> {input}_",u8"b\u1eadt",u8"t\u1eaft",u8"ch\u01b0a cung c\u1ea5p"}}}
    }};return packs;
}
inline std::string_view manualMarketValueMessageKeyName(ManualMarketValueMessageKey key){
    static constexpr std::array<std::string_view,manualMarketValueMessageKeyCount> names={{"manual_market.value.field_request","manual_market.value.field_change","manual_market.value.field_human","manual_market.value.human_change","manual_market.value.field_missing","manual_market.value.before_only","manual_market.value.after_only","manual_market.value.edit_heading","manual_market.value.edit_input","manual_market.value.on","manual_market.value.off","manual_market.value.not_supplied"}};
    const auto index=static_cast<std::size_t>(key);return index<names.size()?names[index]:std::string_view{};
}
inline LocalizedMessage manualMarketValueMessage(std::string_view language,ManualMarketValueMessageKey key){
    const auto index=static_cast<std::size_t>(key);if(index>=manualMarketValueMessageKeyCount)return {};
    const auto &source=manualMarketValueLocalePacks()[0];const auto *selected=&source;for(const auto &pack:manualMarketValueLocalePacks())if(pack.id==language){selected=&pack;break;}
    if(selected->id!=language||!validTemplateTranslation(source.messages[index],selected->messages[index]))return {source.messages[index],source.id,source.review,true,false};
    return {selected->messages[index],selected->id,selected->review,false,selected->rtl};
}
inline std::string manualMarketValueEnglish(ManualMarketValueMessageKey key,const MessageArguments &arguments={}){
    if(arguments.size()>3)return {};
    const auto text=manualMarketValueMessage("en",key).text;std::vector<std::string_view> expected,supplied;
    if(text.empty()||!templateParameters(text,&expected))return {};
    for(const auto &arg:arguments){if(arg.first.size()>32||arg.second.size()>64||(arg.first!="input"&&arg.second.empty())||!std::all_of(arg.second.begin(),arg.second.end(),[](unsigned char c){return c>=32&&c<=126;}))return {};supplied.push_back(arg.first);}
    std::sort(supplied.begin(),supplied.end());if(supplied!=expected)return {};
    std::string result;std::size_t at=0;
    while(at<text.size()){const auto start=text.find('{',at);if(start==text.npos){result.append(text.substr(at));break;}result.append(text.substr(at,start-at));const auto end=text.find('}',start);
        const auto name=text.substr(start+1,end-start-1);const auto found=std::find_if(arguments.begin(),arguments.end(),[&](const auto &arg){return arg.first==name;});if(found==arguments.end())return {};
        result+=found->second;if(result.size()>4096)return {};at=end+1;
    }
    return result.size()<=4096?result:std::string{};
}
} } // namespace om::i18n
