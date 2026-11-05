// Complete manual display/current-reference/UTC-N envelopes. Missing words remain human literals, never numeric facts.
#pragma once
#include "localization.h"
namespace om { namespace i18n {
enum class ManualMarketContextMessageKey { CurrentDisplay, CurrentPrlBoth, CurrentPrlPrice, CurrentPrlStamp, CurrentPrlUnset, UtcStamp, UtcUnset, Count };
constexpr std::size_t manualMarketContextMessageKeyCount=static_cast<std::size_t>(ManualMarketContextMessageKey::Count);
struct ManualMarketContextLocalePack {std::string_view id;bool rtl;ReviewStatus review;std::array<std::string_view,manualMarketContextMessageKeyCount> messages;};
inline const std::array<ManualMarketContextLocalePack,16> &manualMarketContextLocalePacks(){
    static const std::array<ManualMarketContextLocalePack,16> packs={{
        {"en",false,ReviewStatus::Source,{{"Current display: {state}","Current PRL input: {price} USDT/PRL | {stamp}","Current PRL input: {price} USDT/PRL | not supplied","Current PRL input: not supplied USDT/PRL | {stamp}","Current PRL input: not supplied USDT/PRL | not supplied","UTC: {stamp} | N explicitly stamps NOW.","UTC: not supplied | N explicitly stamps NOW."}}},
        {"zh-Hans",false,ReviewStatus::MachineAssisted,{{u8"当前显示：{state}",u8"当前 PRL 输入：{price} USDT/PRL | {stamp}",u8"当前 PRL 输入：{price} USDT/PRL | 未提供",u8"当前 PRL 输入：未提供 USDT/PRL | {stamp}",u8"当前 PRL 输入：未提供 USDT/PRL | 未提供",u8"UTC：{stamp} | N 明确标记 NOW。",u8"UTC：未提供 | N 明确标记 NOW。"}}},
        {"zh-Hant",false,ReviewStatus::MachineAssisted,{{u8"目前顯示：{state}",u8"目前 PRL 輸入：{price} USDT/PRL | {stamp}",u8"目前 PRL 輸入：{price} USDT/PRL | 未提供",u8"目前 PRL 輸入：未提供 USDT/PRL | {stamp}",u8"目前 PRL 輸入：未提供 USDT/PRL | 未提供",u8"UTC：{stamp} | N 明確標記 NOW。",u8"UTC：未提供 | N 明確標記 NOW。"}}},
        {"es",false,ReviewStatus::MachineAssisted,{{u8"Visualización actual: {state}","Entrada PRL actual: {price} USDT/PRL | {stamp}","Entrada PRL actual: {price} USDT/PRL | no suministrado","Entrada PRL actual: no suministrado USDT/PRL | {stamp}","Entrada PRL actual: no suministrado USDT/PRL | no suministrado",u8"UTC: {stamp} | N marca NOW explícitamente.",u8"UTC: no suministrado | N marca NOW explícitamente."}}},
        {"pt-BR",false,ReviewStatus::MachineAssisted,{{u8"Exibição atual: {state}","Entrada PRL atual: {price} USDT/PRL | {stamp}",u8"Entrada PRL atual: {price} USDT/PRL | não fornecido",u8"Entrada PRL atual: não fornecido USDT/PRL | {stamp}",u8"Entrada PRL atual: não fornecido USDT/PRL | não fornecido","UTC: {stamp} | N marca NOW explicitamente.",u8"UTC: não fornecido | N marca NOW explicitamente."}}},
        {"fr",false,ReviewStatus::MachineAssisted,{{"Affichage actuel : {state}",u8"Entrée PRL actuelle : {price} USDT/PRL | {stamp}",u8"Entrée PRL actuelle : {price} USDT/PRL | non fourni",u8"Entrée PRL actuelle : non fourni USDT/PRL | {stamp}",u8"Entrée PRL actuelle : non fourni USDT/PRL | non fourni","UTC : {stamp} | N marque explicitement NOW.","UTC : non fourni | N marque explicitement NOW."}}},
        {"de",false,ReviewStatus::MachineAssisted,{{"Aktuelle Anzeige: {state}","Aktuelle PRL-Eingabe: {price} USDT/PRL | {stamp}","Aktuelle PRL-Eingabe: {price} USDT/PRL | nicht angegeben","Aktuelle PRL-Eingabe: nicht angegeben USDT/PRL | {stamp}","Aktuelle PRL-Eingabe: nicht angegeben USDT/PRL | nicht angegeben",u8"UTC: {stamp} | N stempelt ausdrücklich NOW.",u8"UTC: nicht angegeben | N stempelt ausdrücklich NOW."}}},
        {"ru",false,ReviewStatus::MachineAssisted,{{u8"Текущее отображение: {state}",u8"Текущий ввод PRL: {price} USDT/PRL | {stamp}",u8"Текущий ввод PRL: {price} USDT/PRL | не задано",u8"Текущий ввод PRL: не задано USDT/PRL | {stamp}",u8"Текущий ввод PRL: не задано USDT/PRL | не задано",u8"UTC: {stamp} | N явно отмечает NOW.",u8"UTC: не задано | N явно отмечает NOW."}}},
        {"ja",false,ReviewStatus::MachineAssisted,{{u8"現在の表示：{state}",u8"現在の PRL 入力：{price} USDT/PRL | {stamp}",u8"現在の PRL 入力：{price} USDT/PRL | 未入力",u8"現在の PRL 入力：未入力 USDT/PRL | {stamp}",u8"現在の PRL 入力：未入力 USDT/PRL | 未入力",u8"UTC：{stamp} | N で明示的に NOW を記録。",u8"UTC：未入力 | N で明示的に NOW を記録。"}}},
        {"ko",false,ReviewStatus::MachineAssisted,{{u8"현재 표시: {state}",u8"현재 PRL 입력: {price} USDT/PRL | {stamp}",u8"현재 PRL 입력: {price} USDT/PRL | 미입력",u8"현재 PRL 입력: 미입력 USDT/PRL | {stamp}",u8"현재 PRL 입력: 미입력 USDT/PRL | 미입력",u8"UTC: {stamp} | N 명시적으로 NOW 기록.",u8"UTC: 미입력 | N 명시적으로 NOW 기록."}}},
        {"ar",true,ReviewStatus::MachineAssisted,{{u8"العرض الحالي: {state}",u8"إدخال PRL الحالي: {price} USDT/PRL | {stamp}",u8"إدخال PRL الحالي: {price} USDT/PRL | غير مقدم",u8"إدخال PRL الحالي: غير مقدم USDT/PRL | {stamp}",u8"إدخال PRL الحالي: غير مقدم USDT/PRL | غير مقدم",u8"UTC: {stamp} | N يختم NOW صراحة.",u8"UTC: غير مقدم | N يختم NOW صراحة."}}},
        {"hi",false,ReviewStatus::MachineAssisted,{{u8"वर्तमान प्रदर्शन: {state}",u8"वर्तमान PRL इनपुट: {price} USDT/PRL | {stamp}",u8"वर्तमान PRL इनपुट: {price} USDT/PRL | नहीं दिया",u8"वर्तमान PRL इनपुट: नहीं दिया USDT/PRL | {stamp}",u8"वर्तमान PRL इनपुट: नहीं दिया USDT/PRL | नहीं दिया",u8"UTC: {stamp} | N स्पष्ट रूप से NOW अंकित करता है।",u8"UTC: नहीं दिया | N स्पष्ट रूप से NOW अंकित करता है।"}}},
        {"bn",false,ReviewStatus::MachineAssisted,{{u8"বর্তমান প্রদর্শন: {state}",u8"বর্তমান PRL ইনপুট: {price} USDT/PRL | {stamp}",u8"বর্তমান PRL ইনপুট: {price} USDT/PRL | দেওয়া হয়নি",u8"বর্তমান PRL ইনপুট: দেওয়া হয়নি USDT/PRL | {stamp}",u8"বর্তমান PRL ইনপুট: দেওয়া হয়নি USDT/PRL | দেওয়া হয়নি",u8"UTC: {stamp} | N স্পষ্টভাবে NOW চিহ্নিত করে।",u8"UTC: দেওয়া হয়নি | N স্পষ্টভাবে NOW চিহ্নিত করে।"}}},
        {"id",false,ReviewStatus::MachineAssisted,{{"Tampilan saat ini: {state}","Masukan PRL saat ini: {price} USDT/PRL | {stamp}","Masukan PRL saat ini: {price} USDT/PRL | belum diberikan","Masukan PRL saat ini: belum diberikan USDT/PRL | {stamp}","Masukan PRL saat ini: belum diberikan USDT/PRL | belum diberikan","UTC: {stamp} | N secara eksplisit menandai NOW.","UTC: belum diberikan | N secara eksplisit menandai NOW."}}},
        {"tr",false,ReviewStatus::MachineAssisted,{{u8"Geçerli görüntü: {state}",u8"Geçerli PRL girdisi: {price} USDT/PRL | {stamp}",u8"Geçerli PRL girdisi: {price} USDT/PRL | verilmedi",u8"Geçerli PRL girdisi: verilmedi USDT/PRL | {stamp}",u8"Geçerli PRL girdisi: verilmedi USDT/PRL | verilmedi",u8"UTC: {stamp} | N açıkça NOW damgalar.",u8"UTC: verilmedi | N açıkça NOW damgalar."}}},
        {"vi",false,ReviewStatus::MachineAssisted,{{u8"Hiển thị hiện tại: {state}",u8"PRL nhập hiện tại: {price} USDT/PRL | {stamp}",u8"PRL nhập hiện tại: {price} USDT/PRL | chưa cung cấp",u8"PRL nhập hiện tại: chưa cung cấp USDT/PRL | {stamp}",u8"PRL nhập hiện tại: chưa cung cấp USDT/PRL | chưa cung cấp",u8"UTC: {stamp} | N ghi rõ NOW.",u8"UTC: chưa cung cấp | N ghi rõ NOW."}}}
    }};return packs;
}
inline std::string_view manualMarketContextMessageKeyName(ManualMarketContextMessageKey key){
    static constexpr std::array<std::string_view,manualMarketContextMessageKeyCount> names={{"manual_market.context.current_display","manual_market.context.prl_both","manual_market.context.prl_price","manual_market.context.prl_stamp","manual_market.context.prl_unset","manual_market.context.utc_stamp","manual_market.context.utc_unset"}};
    const auto index=static_cast<std::size_t>(key);return index<names.size()?names[index]:std::string_view{};
}
inline LocalizedMessage manualMarketContextMessage(std::string_view language,ManualMarketContextMessageKey key){
    const auto index=static_cast<std::size_t>(key);if(index>=manualMarketContextMessageKeyCount)return {};
    const auto &source=manualMarketContextLocalePacks()[0];const auto *selected=&source;for(const auto &pack:manualMarketContextLocalePacks())if(pack.id==language){selected=&pack;break;}
    if(selected->id!=language||!validTemplateTranslation(source.messages[index],selected->messages[index]))return {source.messages[index],source.id,source.review,true,false};
    return {selected->messages[index],selected->id,selected->review,false,selected->rtl};
}
inline std::string manualMarketContextEnglish(ManualMarketContextMessageKey key,const MessageArguments &arguments={}){
    if(arguments.size()>2)return {};
    const auto text=manualMarketContextMessage("en",key).text;std::vector<std::string_view> expected,supplied;
    if(text.empty()||!templateParameters(text,&expected))return {};
    for(const auto &arg:arguments){if(arg.first.size()>32||arg.second.empty()||arg.second.size()>64||!std::all_of(arg.second.begin(),arg.second.end(),[](unsigned char c){return c>=32&&c<=126;}))return {};supplied.push_back(arg.first);}
    std::sort(supplied.begin(),supplied.end());if(supplied!=expected)return {};
    std::string result;std::size_t at=0;
    while(at<text.size()){const auto start=text.find('{',at);if(start==text.npos){result.append(text.substr(at));break;}result.append(text.substr(at,start-at));const auto end=text.find('}',start);
        const auto name=text.substr(start+1,end-start-1);const auto found=std::find_if(arguments.begin(),arguments.end(),[&](const auto &arg){return arg.first==name;});if(found==arguments.end())return {};
        result+=found->second;if(result.size()>4096)return {};at=end+1;
    }
    return result.size()<=4096?result:std::string{};
}
} } // namespace om::i18n
