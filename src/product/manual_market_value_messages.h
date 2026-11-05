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
        {"zh-Hans",false,ReviewStatus::MachineAssisted,{{u8"{field}：{value}",u8"{field}：{before} -> {after}",u8"{field}：{value}",u8"{field}：{before} -> {after}",u8"{field}：未提供",u8"{field}：{before} -> 未提供",u8"{field}：未提供 -> {after}",u8"编辑 {field}","> {input}_",u8"开启",u8"关闭",u8"未提供"}}},
        {"zh-Hant",false,ReviewStatus::MachineAssisted,{{u8"{field}：{value}",u8"{field}：{before} -> {after}",u8"{field}：{value}",u8"{field}：{before} -> {after}",u8"{field}：未提供",u8"{field}：{before} -> 未提供",u8"{field}：未提供 -> {after}",u8"編輯 {field}","> {input}_",u8"開啟",u8"關閉",u8"未提供"}}},
        {"es",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}","{field}: no suministrado","{field}: {before} -> no suministrado","{field}: no suministrado -> {after}","Editar {field}","> {input}_","activado","desactivado","no suministrado"}}},
        {"pt-BR",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}",u8"{field}: não fornecido",u8"{field}: {before} -> não fornecido",u8"{field}: não fornecido -> {after}","Editar {field}","> {input}_","ativado","desativado",u8"não fornecido"}}},
        {"fr",false,ReviewStatus::MachineAssisted,{{"{field} : {value}","{field} : {before} -> {after}","{field} : {value}","{field} : {before} -> {after}","{field} : non fourni","{field} : {before} -> non fourni","{field} : non fourni -> {after}","Modifier {field}","> {input}_",u8"activé",u8"désactivé","non fourni"}}},
        {"de",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}","{field}: nicht angegeben","{field}: {before} -> nicht angegeben","{field}: nicht angegeben -> {after}","Bearbeiten: {field}","> {input}_","an","aus","nicht angegeben"}}},
        {"ru",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}",u8"{field}: не задано",u8"{field}: {before} -> не задано",u8"{field}: не задано -> {after}",u8"Править {field}","> {input}_",u8"вкл",u8"выкл",u8"не задано"}}},
        {"ja",false,ReviewStatus::MachineAssisted,{{u8"{field}：{value}",u8"{field}：{before} -> {after}",u8"{field}：{value}",u8"{field}：{before} -> {after}",u8"{field}：未入力",u8"{field}：{before} -> 未入力",u8"{field}：未入力 -> {after}",u8"{field} を編集","> {input}_",u8"有効",u8"無効",u8"未入力"}}},
        {"ko",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}",u8"{field}: 미입력",u8"{field}: {before} -> 미입력",u8"{field}: 미입력 -> {after}",u8"{field} 편집","> {input}_",u8"켜짐",u8"꺼짐",u8"미입력"}}},
        {"ar",true,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}",u8"{field}: غير مقدم",u8"{field}: {before} -> غير مقدم",u8"{field}: غير مقدم -> {after}",u8"تعديل {field}","> {input}_",u8"تشغيل",u8"إيقاف",u8"غير مقدم"}}},
        {"hi",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}",u8"{field}: नहीं दिया",u8"{field}: {before} -> नहीं दिया",u8"{field}: नहीं दिया -> {after}",u8"{field} संपादित करें","> {input}_",u8"चालू",u8"बंद",u8"नहीं दिया"}}},
        {"bn",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}",u8"{field}: দেওয়া হয়নি",u8"{field}: {before} -> দেওয়া হয়নি",u8"{field}: দেওয়া হয়নি -> {after}",u8"{field} সম্পাদনা","> {input}_",u8"চালু",u8"বন্ধ",u8"দেওয়া হয়নি"}}},
        {"id",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}","{field}: belum diberikan","{field}: {before} -> belum diberikan","{field}: belum diberikan -> {after}","Edit {field}","> {input}_","aktif","nonaktif","belum diberikan"}}},
        {"tr",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}","{field}: verilmedi","{field}: {before} -> verilmedi","{field}: verilmedi -> {after}",u8"{field} düzenle","> {input}_",u8"açık",u8"kapalı","verilmedi"}}},
        {"vi",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}",u8"{field}: chưa cung cấp",u8"{field}: {before} -> chưa cung cấp",u8"{field}: chưa cung cấp -> {after}",u8"Sửa {field}","> {input}_",u8"bật",u8"tắt",u8"chưa cung cấp"}}}
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
