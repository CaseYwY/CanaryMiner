// Offline editor/path/count envelopes only. Native originals stay verbatim; independent typed requests validate display roles.
#pragma once
#include "localization.h"
namespace om { namespace i18n {
enum class TariffContextMessageKey { EditField, EditRow, EditBlank, EditInput, RemoveRow, RowName, RowsPrompt, Path, RootPath, FieldPosition, Count };
constexpr std::size_t tariffContextMessageKeyCount=static_cast<std::size_t>(TariffContextMessageKey::Count);
struct TariffContextLocalePack {std::string_view id;bool rtl;ReviewStatus review;std::array<std::string_view,tariffContextMessageKeyCount> messages;};
inline const std::array<TariffContextLocalePack,16> &tariffContextLocalePacks(){
    static const std::array<TariffContextLocalePack,16> packs={{
        {"en",false,ReviewStatus::Source,{{"Edit {field}","Edit row {position}","Edit ","> {input}_","Remove draft row {position}?","row {position}","[Enter] {count} rows","Path: {path}","Path: declaration","Field {position}/{count}"}}},
        {"zh-Hans",false,ReviewStatus::MachineAssisted,{{u8"\u7f16\u8f91 {field}",u8"\u7f16\u8f91\u884c {position}",u8"\u7f16\u8f91 ","> {input}_",u8"\u79fb\u9664\u8349\u7a3f\u884c {position}\uff1f",u8"\u884c {position}",u8"[Enter] {count} \u884c",u8"\u8def\u5f84\uff1a{path}",u8"\u8def\u5f84\uff1a\u58f0\u660e",u8"\u5b57\u6bb5 {position}/{count}"}}},
        {"zh-Hant",false,ReviewStatus::MachineAssisted,{{u8"\u7de8\u8f2f {field}",u8"\u7de8\u8f2f\u5217 {position}",u8"\u7de8\u8f2f ","> {input}_",u8"\u79fb\u9664\u8349\u7a3f\u5217 {position}\uff1f",u8"\u5217 {position}",u8"[Enter] {count} \u5217",u8"\u8def\u5f91\uff1a{path}",u8"\u8def\u5f91\uff1a\u5ba3\u544a",u8"\u6b04\u4f4d {position}/{count}"}}},
        {"es",false,ReviewStatus::MachineAssisted,{{"Editar {field}","Editar fila {position}","Editar ","> {input}_",u8"\u00bfQuitar fila {position} del borrador?","fila {position}","[Enter] {count} filas","Ruta: {path}",u8"Ruta: declaraci\u00f3n","Campo {position}/{count}"}}},
        {"pt-BR",false,ReviewStatus::MachineAssisted,{{"Editar {field}","Editar linha {position}","Editar ","> {input}_","Remover linha {position} do rascunho?","linha {position}","[Enter] {count} linhas","Caminho: {path}",u8"Caminho: declara\u00e7\u00e3o","Campo {position}/{count}"}}},
        {"fr",false,ReviewStatus::MachineAssisted,{{"Modifier {field}","Modifier ligne {position}","Modifier ","> {input}_","Retirer ligne {position} du brouillon ?","ligne {position}","[Enter] {count} lignes","Chemin : {path}",u8"Chemin : d\u00e9claration","Champ {position}/{count}"}}},
        {"de",false,ReviewStatus::MachineAssisted,{{"Bearbeiten {field}","Zeile {position} bearbeiten","Bearbeiten ","> {input}_","Entwurfszeile {position} entfernen?","Zeile {position}","[Enter] {count} Zeilen","Pfad: {path}",u8"Pfad: Erkl\u00e4rung","Feld {position}/{count}"}}},
        {"ru",false,ReviewStatus::MachineAssisted,{{u8"\u041f\u0440\u0430\u0432\u0438\u0442\u044c {field}",u8"\u041f\u0440\u0430\u0432\u0438\u0442\u044c \u0441\u0442\u0440\u043e\u043a\u0443 {position}",u8"\u041f\u0440\u0430\u0432\u0438\u0442\u044c ","> {input}_",u8"\u0423\u0434\u0430\u043b\u0438\u0442\u044c \u0441\u0442\u0440\u043e\u043a\u0443 {position} \u0438\u0437 \u0447\u0435\u0440\u043d\u043e\u0432\u0438\u043a\u0430?",u8"\u0441\u0442\u0440\u043e\u043a\u0430 {position}",u8"[Enter] {count} \u0441\u0442\u0440\u043e\u043a",u8"\u041f\u0443\u0442\u044c: {path}",u8"\u041f\u0443\u0442\u044c: \u0434\u0435\u043a\u043b\u0430\u0440\u0430\u0446\u0438\u044f",u8"\u041f\u043e\u043b\u0435 {position}/{count}"}}},
        {"ja",false,ReviewStatus::MachineAssisted,{{u8"\u7de8\u96c6 {field}",u8"\u884c {position} \u3092\u7de8\u96c6",u8"\u7de8\u96c6 ","> {input}_",u8"\u4e0b\u66f8\u304d\u306e\u884c {position} \u3092\u524a\u9664\uff1f",u8"\u884c {position}",u8"[Enter] {count} \u884c",u8"\u30d1\u30b9\uff1a{path}",u8"\u30d1\u30b9\uff1a\u7533\u544a",u8"\u9805\u76ee {position}/{count}"}}},
        {"ko",false,ReviewStatus::MachineAssisted,{{u8"\ud3b8\uc9d1 {field}",u8"\ud589 {position} \ud3b8\uc9d1",u8"\ud3b8\uc9d1 ","> {input}_",u8"\ucd08\uc548 \ud589 {position} \uc81c\uac70?",u8"\ud589 {position}",u8"[Enter] {count}\ud589",u8"\uacbd\ub85c: {path}",u8"\uacbd\ub85c: \uc120\uc5b8",u8"\ud544\ub4dc {position}/{count}"}}},
        {"ar",true,ReviewStatus::MachineAssisted,{{u8"\u062a\u0639\u062f\u064a\u0644 {field}",u8"\u062a\u0639\u062f\u064a\u0644 \u0627\u0644\u0635\u0641 {position}",u8"\u062a\u0639\u062f\u064a\u0644 ","> {input}_",u8"\u0625\u0632\u0627\u0644\u0629 \u0635\u0641 \u0627\u0644\u0645\u0633\u0648\u062f\u0629 {position}\u061f",u8"\u0635\u0641 {position}",u8"[Enter] {count} \u0635\u0641\u0648\u0641",u8"\u0627\u0644\u0645\u0633\u0627\u0631: {path}",u8"\u0627\u0644\u0645\u0633\u0627\u0631: \u0627\u0644\u062a\u0635\u0631\u064a\u062d",u8"\u0627\u0644\u062d\u0642\u0644 {position}/{count}"}}},
        {"hi",false,ReviewStatus::MachineAssisted,{{u8"\u0938\u0902\u092a\u093e\u0926\u0928 {field}",u8"\u092a\u0902\u0915\u094d\u0924\u093f {position} \u0938\u0902\u092a\u093e\u0926\u093f\u0924 \u0915\u0930\u0947\u0902",u8"\u0938\u0902\u092a\u093e\u0926\u0928 ","> {input}_",u8"\u092e\u0938\u094c\u0926\u0947 \u0915\u0940 \u092a\u0902\u0915\u094d\u0924\u093f {position} \u0939\u091f\u093e\u090f\u0901?",u8"\u092a\u0902\u0915\u094d\u0924\u093f {position}",u8"[Enter] {count} \u092a\u0902\u0915\u094d\u0924\u093f\u092f\u093e\u0901",u8"\u092a\u0925: {path}",u8"\u092a\u0925: \u0918\u094b\u0937\u0923\u093e",u8"\u092b\u093c\u0940\u0932\u094d\u0921 {position}/{count}"}}},
        {"bn",false,ReviewStatus::MachineAssisted,{{u8"\u09b8\u09ae\u09cd\u09aa\u09be\u09a6\u09a8\u09be {field}",u8"\u09b8\u09be\u09b0\u09bf {position} \u09b8\u09ae\u09cd\u09aa\u09be\u09a6\u09a8\u09be",u8"\u09b8\u09ae\u09cd\u09aa\u09be\u09a6\u09a8\u09be ","> {input}_",u8"\u0996\u09b8\u09a1\u09bc\u09be\u09b0 \u09b8\u09be\u09b0\u09bf {position} \u09b8\u09b0\u09be\u09ac\u09c7\u09a8?",u8"\u09b8\u09be\u09b0\u09bf {position}",u8"[Enter] {count} \u09b8\u09be\u09b0\u09bf",u8"\u09aa\u09a5: {path}",u8"\u09aa\u09a5: \u0998\u09cb\u09b7\u09a3\u09be",u8"\u0995\u09cd\u09b7\u09c7\u09a4\u09cd\u09b0 {position}/{count}"}}},
        {"id",false,ReviewStatus::MachineAssisted,{{"Edit {field}","Edit baris {position}","Edit ","> {input}_","Hapus baris draf {position}?","baris {position}","[Enter] {count} baris","Jalur: {path}","Jalur: deklarasi","Kolom {position}/{count}"}}},
        {"tr",false,ReviewStatus::MachineAssisted,{{u8"D\u00fczenle {field}",u8"Sat\u0131r {position} d\u00fczenle",u8"D\u00fczenle ","> {input}_",u8"Taslak sat\u0131r\u0131 {position} kald\u0131r\u0131ls\u0131n m\u0131?",u8"sat\u0131r {position}",u8"[Enter] {count} sat\u0131r",u8"Yol: {path}","Yol: beyan","Alan {position}/{count}"}}},
        {"vi",false,ReviewStatus::MachineAssisted,{{u8"S\u1eeda {field}",u8"S\u1eeda d\u00f2ng {position}",u8"S\u1eeda ","> {input}_",u8"B\u1ecf d\u00f2ng nh\u00e1p {position}?",u8"d\u00f2ng {position}",u8"[Enter] {count} d\u00f2ng",u8"\u0110\u01b0\u1eddng d\u1eabn: {path}",u8"\u0110\u01b0\u1eddng d\u1eabn: khai b\u00e1o",u8"Tr\u01b0\u1eddng {position}/{count}"}}}
    }};return packs;
}
inline std::string_view tariffContextMessageKeyName(TariffContextMessageKey key){
    static constexpr std::array<std::string_view,tariffContextMessageKeyCount> names={{"tariff.context.edit_field","tariff.context.edit_row","tariff.context.edit_blank","tariff.context.edit_input","tariff.context.remove_row","tariff.context.row_name","tariff.context.rows_prompt","tariff.context.path","tariff.context.root_path","tariff.context.field_position"}};
    const auto index=static_cast<std::size_t>(key);return index<names.size()?names[index]:std::string_view{};
}
inline LocalizedMessage tariffContextMessage(std::string_view language,TariffContextMessageKey key){
    const auto index=static_cast<std::size_t>(key);if(index>=tariffContextMessageKeyCount)return {};
    const auto &source=tariffContextLocalePacks()[0];const auto *selected=&source;for(const auto &pack:tariffContextLocalePacks())if(pack.id==language){selected=&pack;break;}
    if(selected->id!=language||!validTemplateTranslation(source.messages[index],selected->messages[index]))return {source.messages[index],source.id,source.review,true,false};
    return {selected->messages[index],selected->id,selected->review,false,selected->rtl};
}
// Source-only one-pass interpolation: preserve even invalid developer originals. This is neither a sanitizer nor typed admission.
inline std::string tariffContextEnglish(TariffContextMessageKey key,const MessageArguments &arguments={}){
    const auto text=tariffContextMessage("en",key).text;std::vector<std::string_view> expected,supplied;
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
