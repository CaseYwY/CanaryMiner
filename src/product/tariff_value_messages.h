// Whole browse-row envelopes around original JSON tokens; descriptions do not parse, validate or authorize private values.
#pragma once
#include "localization.h"
namespace om { namespace i18n {
enum class TariffValueMessageKey { FieldJson, FieldJsonFixed, FieldFields, FieldRows, RowJson, RowFields, RowRows, Count };
constexpr std::size_t tariffValueMessageKeyCount=static_cast<std::size_t>(TariffValueMessageKey::Count);
struct TariffValueLocalePack {std::string_view id;bool rtl;ReviewStatus review;std::array<std::string_view,tariffValueMessageKeyCount> messages;};
inline const std::array<TariffValueLocalePack,16> &tariffValueLocalePacks(){
    static const std::array<TariffValueLocalePack,16> packs={{
        {"en",false,ReviewStatus::Source,{{"{field}: {value}","{field}: {value} [fixed]","{field}: [Enter] fields","{field}: [Enter] {count} rows","row {position}: {value}","row {position}: [Enter] fields","row {position}: [Enter] {count} rows"}}},
        {"zh-Hans",false,ReviewStatus::MachineAssisted,{{u8"{field}：{value}",u8"{field}：{value} [固定]",u8"{field}：[Enter] 字段",u8"{field}：[Enter] {count} 行",u8"行 {position}：{value}",u8"行 {position}：[Enter] 字段",u8"行 {position}：[Enter] {count} 行"}}},
        {"zh-Hant",false,ReviewStatus::MachineAssisted,{{u8"{field}：{value}",u8"{field}：{value} [固定]",u8"{field}：[Enter] 欄位",u8"{field}：[Enter] {count} 列",u8"列 {position}：{value}",u8"列 {position}：[Enter] 欄位",u8"列 {position}：[Enter] {count} 列"}}},
        {"es",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {value} [fijo]","{field}: [Enter] campos","{field}: [Enter] {count} filas","fila {position}: {value}","fila {position}: [Enter] campos","fila {position}: [Enter] {count} filas"}}},
        {"pt-BR",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {value} [fixo]","{field}: [Enter] campos","{field}: [Enter] {count} linhas","linha {position}: {value}","linha {position}: [Enter] campos","linha {position}: [Enter] {count} linhas"}}},
        {"fr",false,ReviewStatus::MachineAssisted,{{"{field} : {value}","{field} : {value} [fixe]","{field} : [Enter] champs","{field} : [Enter] {count} lignes","ligne {position} : {value}","ligne {position} : [Enter] champs","ligne {position} : [Enter] {count} lignes"}}},
        {"de",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {value} [fest]","{field}: [Enter] Felder","{field}: [Enter] {count} Zeilen","Zeile {position}: {value}","Zeile {position}: [Enter] Felder","Zeile {position}: [Enter] {count} Zeilen"}}},
        {"ru",false,ReviewStatus::MachineAssisted,{{"{field}: {value}",u8"{field}: {value} [фиксировано]",u8"{field}: [Enter] поля",u8"{field}: [Enter] {count} строк",u8"строка {position}: {value}",u8"строка {position}: [Enter] поля",u8"строка {position}: [Enter] {count} строк"}}},
        {"ja",false,ReviewStatus::MachineAssisted,{{u8"{field}：{value}",u8"{field}：{value} [固定]",u8"{field}：[Enter] 項目",u8"{field}：[Enter] {count} 行",u8"行 {position}：{value}",u8"行 {position}：[Enter] 項目",u8"行 {position}：[Enter] {count} 行"}}},
        {"ko",false,ReviewStatus::MachineAssisted,{{"{field}: {value}",u8"{field}: {value} [고정]",u8"{field}: [Enter] 필드",u8"{field}: [Enter] {count}행",u8"행 {position}: {value}",u8"행 {position}: [Enter] 필드",u8"행 {position}: [Enter] {count}행"}}},
        {"ar",true,ReviewStatus::MachineAssisted,{{"{field}: {value}",u8"{field}: {value} [ثابت]",u8"{field}: [Enter] حقول",u8"{field}: [Enter] {count} صفوف",u8"صف {position}: {value}",u8"صف {position}: [Enter] حقول",u8"صف {position}: [Enter] {count} صفوف"}}},
        {"hi",false,ReviewStatus::MachineAssisted,{{"{field}: {value}",u8"{field}: {value} [निश्चित]",u8"{field}: [Enter] फ़ील्ड",u8"{field}: [Enter] {count} पंक्तियाँ",u8"पंक्ति {position}: {value}",u8"पंक्ति {position}: [Enter] फ़ील्ड",u8"पंक्ति {position}: [Enter] {count} पंक्तियाँ"}}},
        {"bn",false,ReviewStatus::MachineAssisted,{{"{field}: {value}",u8"{field}: {value} [স্থির]",u8"{field}: [Enter] ক্ষেত্র",u8"{field}: [Enter] {count} সারি",u8"সারি {position}: {value}",u8"সারি {position}: [Enter] ক্ষেত্র",u8"সারি {position}: [Enter] {count} সারি"}}},
        {"id",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {value} [tetap]","{field}: [Enter] kolom","{field}: [Enter] {count} baris","baris {position}: {value}","baris {position}: [Enter] kolom","baris {position}: [Enter] {count} baris"}}},
        {"tr",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {value} [sabit]","{field}: [Enter] alanlar",u8"{field}: [Enter] {count} satır",u8"satır {position}: {value}",u8"satır {position}: [Enter] alanlar",u8"satır {position}: [Enter] {count} satır"}}},
        {"vi",false,ReviewStatus::MachineAssisted,{{"{field}: {value}",u8"{field}: {value} [cố định]",u8"{field}: [Enter] trường",u8"{field}: [Enter] {count} dòng",u8"dòng {position}: {value}",u8"dòng {position}: [Enter] trường",u8"dòng {position}: [Enter] {count} dòng"}}}
    }};return packs;
}
inline std::string_view tariffValueMessageKeyName(TariffValueMessageKey key){
    static constexpr std::array<std::string_view,tariffValueMessageKeyCount> names={{"tariff.value.field_json","tariff.value.field_json_fixed","tariff.value.field_fields","tariff.value.field_rows","tariff.value.row_json","tariff.value.row_fields","tariff.value.row_rows"}};
    const auto index=static_cast<std::size_t>(key);return index<names.size()?names[index]:std::string_view{};
}
inline LocalizedMessage tariffValueMessage(std::string_view language,TariffValueMessageKey key){
    const auto index=static_cast<std::size_t>(key);if(index>=tariffValueMessageKeyCount)return {};
    const auto &source=tariffValueLocalePacks()[0];const auto *selected=&source;for(const auto &pack:tariffValueLocalePacks())if(pack.id==language){selected=&pack;break;}
    if(selected->id!=language||!validTemplateTranslation(source.messages[index],selected->messages[index]))return {source.messages[index],source.id,source.review,true,false};
    return {selected->messages[index],selected->id,selected->review,false,selected->rtl};
}
// Preserve source-only native originals, not terminal-safe typed admission or a JSON parser.
inline std::string tariffValueEnglish(TariffValueMessageKey key,const MessageArguments &arguments={}){
    const auto text=tariffValueMessage("en",key).text;std::vector<std::string_view> expected,supplied;
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
