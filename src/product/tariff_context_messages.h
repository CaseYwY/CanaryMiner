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
        {"zh-Hans",false,ReviewStatus::MachineAssisted,{{u8"编辑 {field}",u8"编辑行 {position}",u8"编辑 ","> {input}_",u8"移除草稿行 {position}？",u8"行 {position}",u8"[Enter] {count} 行",u8"路径：{path}",u8"路径：声明",u8"字段 {position}/{count}"}}},
        {"zh-Hant",false,ReviewStatus::MachineAssisted,{{u8"編輯 {field}",u8"編輯列 {position}",u8"編輯 ","> {input}_",u8"移除草稿列 {position}？",u8"列 {position}",u8"[Enter] {count} 列",u8"路徑：{path}",u8"路徑：宣告",u8"欄位 {position}/{count}"}}},
        {"es",false,ReviewStatus::MachineAssisted,{{"Editar {field}","Editar fila {position}","Editar ","> {input}_",u8"¿Quitar fila {position} del borrador?","fila {position}","[Enter] {count} filas","Ruta: {path}",u8"Ruta: declaración","Campo {position}/{count}"}}},
        {"pt-BR",false,ReviewStatus::MachineAssisted,{{"Editar {field}","Editar linha {position}","Editar ","> {input}_","Remover linha {position} do rascunho?","linha {position}","[Enter] {count} linhas","Caminho: {path}",u8"Caminho: declaração","Campo {position}/{count}"}}},
        {"fr",false,ReviewStatus::MachineAssisted,{{"Modifier {field}","Modifier ligne {position}","Modifier ","> {input}_","Retirer ligne {position} du brouillon ?","ligne {position}","[Enter] {count} lignes","Chemin : {path}",u8"Chemin : déclaration","Champ {position}/{count}"}}},
        {"de",false,ReviewStatus::MachineAssisted,{{"Bearbeiten {field}","Zeile {position} bearbeiten","Bearbeiten ","> {input}_","Entwurfszeile {position} entfernen?","Zeile {position}","[Enter] {count} Zeilen","Pfad: {path}",u8"Pfad: Erklärung","Feld {position}/{count}"}}},
        {"ru",false,ReviewStatus::MachineAssisted,{{u8"Править {field}",u8"Править строку {position}",u8"Править ","> {input}_",u8"Удалить строку {position} из черновика?",u8"строка {position}",u8"[Enter] {count} строк",u8"Путь: {path}",u8"Путь: декларация",u8"Поле {position}/{count}"}}},
        {"ja",false,ReviewStatus::MachineAssisted,{{u8"編集 {field}",u8"行 {position} を編集",u8"編集 ","> {input}_",u8"下書きの行 {position} を削除？",u8"行 {position}",u8"[Enter] {count} 行",u8"パス：{path}",u8"パス：申告",u8"項目 {position}/{count}"}}},
        {"ko",false,ReviewStatus::MachineAssisted,{{u8"편집 {field}",u8"행 {position} 편집",u8"편집 ","> {input}_",u8"초안 행 {position} 제거?",u8"행 {position}",u8"[Enter] {count}행",u8"경로: {path}",u8"경로: 선언",u8"필드 {position}/{count}"}}},
        {"ar",true,ReviewStatus::MachineAssisted,{{u8"تعديل {field}",u8"تعديل الصف {position}",u8"تعديل ","> {input}_",u8"إزالة صف المسودة {position}؟",u8"صف {position}",u8"[Enter] {count} صفوف",u8"المسار: {path}",u8"المسار: التصريح",u8"الحقل {position}/{count}"}}},
        {"hi",false,ReviewStatus::MachineAssisted,{{u8"संपादन {field}",u8"पंक्ति {position} संपादित करें",u8"संपादन ","> {input}_",u8"मसौदे की पंक्ति {position} हटाएँ?",u8"पंक्ति {position}",u8"[Enter] {count} पंक्तियाँ",u8"पथ: {path}",u8"पथ: घोषणा",u8"फ़ील्ड {position}/{count}"}}},
        {"bn",false,ReviewStatus::MachineAssisted,{{u8"সম্পাদনা {field}",u8"সারি {position} সম্পাদনা",u8"সম্পাদনা ","> {input}_",u8"খসড়ার সারি {position} সরাবেন?",u8"সারি {position}",u8"[Enter] {count} সারি",u8"পথ: {path}",u8"পথ: ঘোষণা",u8"ক্ষেত্র {position}/{count}"}}},
        {"id",false,ReviewStatus::MachineAssisted,{{"Edit {field}","Edit baris {position}","Edit ","> {input}_","Hapus baris draf {position}?","baris {position}","[Enter] {count} baris","Jalur: {path}","Jalur: deklarasi","Kolom {position}/{count}"}}},
        {"tr",false,ReviewStatus::MachineAssisted,{{u8"Düzenle {field}",u8"Satır {position} düzenle",u8"Düzenle ","> {input}_",u8"Taslak satırı {position} kaldırılsın mı?",u8"satır {position}",u8"[Enter] {count} satır",u8"Yol: {path}","Yol: beyan","Alan {position}/{count}"}}},
        {"vi",false,ReviewStatus::MachineAssisted,{{u8"Sửa {field}",u8"Sửa dòng {position}",u8"Sửa ","> {input}_",u8"Bỏ dòng nháp {position}?",u8"dòng {position}",u8"[Enter] {count} dòng",u8"Đường dẫn: {path}",u8"Đường dẫn: khai báo",u8"Trường {position}/{count}"}}}
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
