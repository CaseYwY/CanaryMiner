// Opaque electricity saved-request/input envelopes, distinct human toggles/clocks and literal missing declarations.
#pragma once
#include "localization.h"
namespace om { namespace i18n {
enum class ElectricityValueMessageKey { FieldValue, FieldChange, HumanFieldValue, HumanFieldChange, MissingFieldValue, FieldBeforeOnly, FieldAfterOnly, EditHeading, EditInput, On, Off, NotSupplied, FixedClock, ExplicitUtc, Count };
constexpr std::size_t electricityValueMessageKeyCount=static_cast<std::size_t>(ElectricityValueMessageKey::Count);
struct ElectricityValueLocalePack {std::string_view id;bool rtl;ReviewStatus review;std::array<std::string_view,electricityValueMessageKeyCount> messages;};
inline const std::array<ElectricityValueLocalePack,16> &electricityValueLocalePacks(){
    static const std::array<ElectricityValueLocalePack,16> packs={{
        {"en",false,ReviewStatus::Source,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}","{field}: not supplied","{field}: {before} -> not supplied","{field}: not supplied -> {after}","Edit {field}","> {input}_","on","off","not supplied","fixed STANDARD (no DST)","explicit UTC"}}},
        {"zh-Hans",false,ReviewStatus::MachineAssisted,{{u8"{field}：{value}",u8"{field}：{before} -> {after}",u8"{field}：{value}",u8"{field}：{before} -> {after}",u8"{field}：未提供",u8"{field}：{before} -> 未提供",u8"{field}：未提供 -> {after}",u8"编辑 {field}","> {input}_",u8"开启",u8"关闭",u8"未提供",u8"固定 STANDARD（无 DST）",u8"明确 UTC"}}},
        {"zh-Hant",false,ReviewStatus::MachineAssisted,{{u8"{field}：{value}",u8"{field}：{before} -> {after}",u8"{field}：{value}",u8"{field}：{before} -> {after}",u8"{field}：未提供",u8"{field}：{before} -> 未提供",u8"{field}：未提供 -> {after}",u8"編輯 {field}","> {input}_",u8"開啟",u8"關閉",u8"未提供",u8"固定 STANDARD（無 DST）",u8"明確 UTC"}}},
        {"es",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}","{field}: no suministrado","{field}: {before} -> no suministrado","{field}: no suministrado -> {after}","Editar {field}","> {input}_","activado","desactivado","no suministrado","STANDARD fijo (sin DST)",u8"UTC explícito"}}},
        {"pt-BR",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}",u8"{field}: não fornecido",u8"{field}: {before} -> não fornecido",u8"{field}: não fornecido -> {after}","Editar {field}","> {input}_","ativado","desativado",u8"não fornecido","STANDARD fixo (sem DST)",u8"UTC explícito"}}},
        {"fr",false,ReviewStatus::MachineAssisted,{{"{field} : {value}","{field} : {before} -> {after}","{field} : {value}","{field} : {before} -> {after}","{field} : non fourni","{field} : {before} -> non fourni","{field} : non fourni -> {after}","Modifier {field}","> {input}_",u8"activé",u8"désactivé","non fourni","STANDARD fixe (sans DST)","UTC explicite"}}},
        {"de",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}","{field}: nicht angegeben","{field}: {before} -> nicht angegeben","{field}: nicht angegeben -> {after}","Bearbeiten: {field}","> {input}_","an","aus","nicht angegeben","feste STANDARD-Zeit (ohne DST)","explizite UTC"}}},
        {"ru",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}",u8"{field}: не задано",u8"{field}: {before} -> не задано",u8"{field}: не задано -> {after}",u8"Править {field}","> {input}_",u8"вкл",u8"выкл",u8"не задано",u8"фиксированное STANDARD (без DST)",u8"явное UTC"}}},
        {"ja",false,ReviewStatus::MachineAssisted,{{u8"{field}：{value}",u8"{field}：{before} -> {after}",u8"{field}：{value}",u8"{field}：{before} -> {after}",u8"{field}：未入力",u8"{field}：{before} -> 未入力",u8"{field}：未入力 -> {after}",u8"{field} を編集","> {input}_",u8"有効",u8"無効",u8"未入力",u8"固定 STANDARD（DST なし）",u8"明示的な UTC"}}},
        {"ko",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}",u8"{field}: 미입력",u8"{field}: {before} -> 미입력",u8"{field}: 미입력 -> {after}",u8"{field} 편집","> {input}_",u8"켜짐",u8"꺼짐",u8"미입력",u8"고정 STANDARD (DST 없음)",u8"명시적 UTC"}}},
        {"ar",true,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}",u8"{field}: غير مقدم",u8"{field}: {before} -> غير مقدم",u8"{field}: غير مقدم -> {after}",u8"تعديل {field}","> {input}_",u8"تشغيل",u8"إيقاف",u8"غير مقدم",u8"STANDARD ثابت (بلا DST)",u8"UTC صريح"}}},
        {"hi",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}",u8"{field}: नहीं दिया",u8"{field}: {before} -> नहीं दिया",u8"{field}: नहीं दिया -> {after}",u8"{field} संपादित करें","> {input}_",u8"चालू",u8"बंद",u8"नहीं दिया",u8"निश्चित STANDARD (DST नहीं)",u8"स्पष्ट UTC"}}},
        {"bn",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}",u8"{field}: দেওয়া হয়নি",u8"{field}: {before} -> দেওয়া হয়নি",u8"{field}: দেওয়া হয়নি -> {after}",u8"{field} সম্পাদনা","> {input}_",u8"চালু",u8"বন্ধ",u8"দেওয়া হয়নি",u8"স্থির STANDARD (DST নেই)",u8"স্পষ্ট UTC"}}},
        {"id",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}","{field}: belum diberikan","{field}: {before} -> belum diberikan","{field}: belum diberikan -> {after}","Edit {field}","> {input}_","aktif","nonaktif","belum diberikan","STANDARD tetap (tanpa DST)","UTC eksplisit"}}},
        {"tr",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}","{field}: verilmedi","{field}: {before} -> verilmedi","{field}: verilmedi -> {after}",u8"{field} düzenle","> {input}_",u8"açık",u8"kapalı","verilmedi","sabit STANDARD (DST yok)",u8"açık UTC"}}},
        {"vi",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {before} -> {after}","{field}: {value}","{field}: {before} -> {after}",u8"{field}: chưa cung cấp",u8"{field}: {before} -> chưa cung cấp",u8"{field}: chưa cung cấp -> {after}",u8"Sửa {field}","> {input}_",u8"bật",u8"tắt",u8"chưa cung cấp",u8"STANDARD cố định (không DST)",u8"UTC rõ ràng"}}}
    }};return packs;
}
inline std::string_view electricityValueMessageKeyName(ElectricityValueMessageKey key){
    static constexpr std::array<std::string_view,electricityValueMessageKeyCount> names={{"electricity.value.field_request","electricity.value.field_change","electricity.value.field_human","electricity.value.human_change","electricity.value.field_missing","electricity.value.before_only","electricity.value.after_only","electricity.value.edit_heading","electricity.value.edit_input","electricity.value.on","electricity.value.off","electricity.value.not_supplied","electricity.value.fixed_clock","electricity.value.explicit_utc"}};
    const auto index=static_cast<std::size_t>(key);return index<names.size()?names[index]:std::string_view{};
}
inline LocalizedMessage electricityValueMessage(std::string_view language,ElectricityValueMessageKey key){
    const auto index=static_cast<std::size_t>(key);if(index>=electricityValueMessageKeyCount)return {};
    const auto &source=electricityValueLocalePacks()[0];const auto *selected=&source;for(const auto &pack:electricityValueLocalePacks())if(pack.id==language){selected=&pack;break;}
    if(selected->id!=language||!validTemplateTranslation(source.messages[index],selected->messages[index]))return {source.messages[index],source.id,source.review,true,false};
    return {selected->messages[index],selected->id,selected->review,false,selected->rtl};
}
inline std::string electricityValueEnglish(ElectricityValueMessageKey key,const MessageArguments &arguments={}){
    if(arguments.size()>3)return {};
    const auto text=electricityValueMessage("en",key).text;std::vector<std::string_view> expected,supplied;
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
