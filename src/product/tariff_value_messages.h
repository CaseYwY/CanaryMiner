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
        {"zh-Hans",false,ReviewStatus::MachineAssisted,{{u8"{field}\uff1a{value}",u8"{field}\uff1a{value} [\u56fa\u5b9a]",u8"{field}\uff1a[Enter] \u5b57\u6bb5",u8"{field}\uff1a[Enter] {count} \u884c",u8"\u884c {position}\uff1a{value}",u8"\u884c {position}\uff1a[Enter] \u5b57\u6bb5",u8"\u884c {position}\uff1a[Enter] {count} \u884c"}}},
        {"zh-Hant",false,ReviewStatus::MachineAssisted,{{u8"{field}\uff1a{value}",u8"{field}\uff1a{value} [\u56fa\u5b9a]",u8"{field}\uff1a[Enter] \u6b04\u4f4d",u8"{field}\uff1a[Enter] {count} \u5217",u8"\u5217 {position}\uff1a{value}",u8"\u5217 {position}\uff1a[Enter] \u6b04\u4f4d",u8"\u5217 {position}\uff1a[Enter] {count} \u5217"}}},
        {"es",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {value} [fijo]","{field}: [Enter] campos","{field}: [Enter] {count} filas","fila {position}: {value}","fila {position}: [Enter] campos","fila {position}: [Enter] {count} filas"}}},
        {"pt-BR",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {value} [fixo]","{field}: [Enter] campos","{field}: [Enter] {count} linhas","linha {position}: {value}","linha {position}: [Enter] campos","linha {position}: [Enter] {count} linhas"}}},
        {"fr",false,ReviewStatus::MachineAssisted,{{"{field} : {value}","{field} : {value} [fixe]","{field} : [Enter] champs","{field} : [Enter] {count} lignes","ligne {position} : {value}","ligne {position} : [Enter] champs","ligne {position} : [Enter] {count} lignes"}}},
        {"de",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {value} [fest]","{field}: [Enter] Felder","{field}: [Enter] {count} Zeilen","Zeile {position}: {value}","Zeile {position}: [Enter] Felder","Zeile {position}: [Enter] {count} Zeilen"}}},
        {"ru",false,ReviewStatus::MachineAssisted,{{"{field}: {value}",u8"{field}: {value} [\u0444\u0438\u043a\u0441\u0438\u0440\u043e\u0432\u0430\u043d\u043e]",u8"{field}: [Enter] \u043f\u043e\u043b\u044f",u8"{field}: [Enter] {count} \u0441\u0442\u0440\u043e\u043a",u8"\u0441\u0442\u0440\u043e\u043a\u0430 {position}: {value}",u8"\u0441\u0442\u0440\u043e\u043a\u0430 {position}: [Enter] \u043f\u043e\u043b\u044f",u8"\u0441\u0442\u0440\u043e\u043a\u0430 {position}: [Enter] {count} \u0441\u0442\u0440\u043e\u043a"}}},
        {"ja",false,ReviewStatus::MachineAssisted,{{u8"{field}\uff1a{value}",u8"{field}\uff1a{value} [\u56fa\u5b9a]",u8"{field}\uff1a[Enter] \u9805\u76ee",u8"{field}\uff1a[Enter] {count} \u884c",u8"\u884c {position}\uff1a{value}",u8"\u884c {position}\uff1a[Enter] \u9805\u76ee",u8"\u884c {position}\uff1a[Enter] {count} \u884c"}}},
        {"ko",false,ReviewStatus::MachineAssisted,{{"{field}: {value}",u8"{field}: {value} [\uace0\uc815]",u8"{field}: [Enter] \ud544\ub4dc",u8"{field}: [Enter] {count}\ud589",u8"\ud589 {position}: {value}",u8"\ud589 {position}: [Enter] \ud544\ub4dc",u8"\ud589 {position}: [Enter] {count}\ud589"}}},
        {"ar",true,ReviewStatus::MachineAssisted,{{"{field}: {value}",u8"{field}: {value} [\u062b\u0627\u0628\u062a]",u8"{field}: [Enter] \u062d\u0642\u0648\u0644",u8"{field}: [Enter] {count} \u0635\u0641\u0648\u0641",u8"\u0635\u0641 {position}: {value}",u8"\u0635\u0641 {position}: [Enter] \u062d\u0642\u0648\u0644",u8"\u0635\u0641 {position}: [Enter] {count} \u0635\u0641\u0648\u0641"}}},
        {"hi",false,ReviewStatus::MachineAssisted,{{"{field}: {value}",u8"{field}: {value} [\u0928\u093f\u0936\u094d\u091a\u093f\u0924]",u8"{field}: [Enter] \u092b\u093c\u0940\u0932\u094d\u0921",u8"{field}: [Enter] {count} \u092a\u0902\u0915\u094d\u0924\u093f\u092f\u093e\u0901",u8"\u092a\u0902\u0915\u094d\u0924\u093f {position}: {value}",u8"\u092a\u0902\u0915\u094d\u0924\u093f {position}: [Enter] \u092b\u093c\u0940\u0932\u094d\u0921",u8"\u092a\u0902\u0915\u094d\u0924\u093f {position}: [Enter] {count} \u092a\u0902\u0915\u094d\u0924\u093f\u092f\u093e\u0901"}}},
        {"bn",false,ReviewStatus::MachineAssisted,{{"{field}: {value}",u8"{field}: {value} [\u09b8\u09cd\u09a5\u09bf\u09b0]",u8"{field}: [Enter] \u0995\u09cd\u09b7\u09c7\u09a4\u09cd\u09b0",u8"{field}: [Enter] {count} \u09b8\u09be\u09b0\u09bf",u8"\u09b8\u09be\u09b0\u09bf {position}: {value}",u8"\u09b8\u09be\u09b0\u09bf {position}: [Enter] \u0995\u09cd\u09b7\u09c7\u09a4\u09cd\u09b0",u8"\u09b8\u09be\u09b0\u09bf {position}: [Enter] {count} \u09b8\u09be\u09b0\u09bf"}}},
        {"id",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {value} [tetap]","{field}: [Enter] kolom","{field}: [Enter] {count} baris","baris {position}: {value}","baris {position}: [Enter] kolom","baris {position}: [Enter] {count} baris"}}},
        {"tr",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {value} [sabit]","{field}: [Enter] alanlar",u8"{field}: [Enter] {count} sat\u0131r",u8"sat\u0131r {position}: {value}",u8"sat\u0131r {position}: [Enter] alanlar",u8"sat\u0131r {position}: [Enter] {count} sat\u0131r"}}},
        {"vi",false,ReviewStatus::MachineAssisted,{{"{field}: {value}",u8"{field}: {value} [c\u1ed1 \u0111\u1ecbnh]",u8"{field}: [Enter] tr\u01b0\u1eddng",u8"{field}: [Enter] {count} d\u00f2ng",u8"d\u00f2ng {position}: {value}",u8"d\u00f2ng {position}: [Enter] tr\u01b0\u1eddng",u8"d\u00f2ng {position}: [Enter] {count} d\u00f2ng"}}}
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
