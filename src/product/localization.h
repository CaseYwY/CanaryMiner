// Bounded message-key foundation. No global locale, parsing, money, timezone, protocol or runtime activation.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace om { namespace i18n {
enum class ReviewStatus { Source, MachineAssisted, HumanReviewed };
enum class MessageKey { Save, Cancel, Back, Stop, Review, Settings, Language, Unavailable, Stale, Unknown, Draft, Saved, SourceValue, Count };
constexpr std::size_t messageKeyCount=static_cast<std::size_t>(MessageKey::Count);
struct LocalePack {
    std::string_view id,autonym;bool rtl=false;ReviewStatus review=ReviewStatus::MachineAssisted;
    std::array<std::string_view,messageKeyCount> messages;
};
inline const std::array<LocalePack,16> &localePacks(){
    static const std::array<LocalePack,16> packs={{
        {"en","English",false,ReviewStatus::Source,{{"Save","Cancel","Back","Stop","Review","Settings","Language","Unavailable","Stale","Unknown","Draft","Saved","{source}: {value}"}}},
        {"zh-Hans",u8"简体中文",false,ReviewStatus::MachineAssisted,{{u8"保存",u8"取消",u8"返回",u8"停止",u8"审阅",u8"设置",u8"语言",u8"不可用",u8"已过期",u8"未知",u8"草稿",u8"已保存",u8"{source}：{value}"}}},
        {"zh-Hant",u8"繁體中文",false,ReviewStatus::MachineAssisted,{{u8"儲存",u8"取消",u8"返回",u8"停止",u8"檢閱",u8"設定",u8"語言",u8"無法使用",u8"已過期",u8"未知",u8"草稿",u8"已儲存",u8"{source}：{value}"}}},
        {"es",u8"Español",false,ReviewStatus::MachineAssisted,{{"Guardar","Cancelar",u8"Atrás","Detener","Revisar",u8"Configuración","Idioma","No disponible","Desactualizado","Desconocido","Borrador","Guardado","{source}: {value}"}}},
        {"pt-BR",u8"Português (Brasil)",false,ReviewStatus::MachineAssisted,{{"Salvar","Cancelar","Voltar","Parar","Revisar",u8"Configurações","Idioma",u8"Indisponível","Desatualizado","Desconhecido","Rascunho","Salvo","{source}: {value}"}}},
        {"fr",u8"Français",false,ReviewStatus::MachineAssisted,{{"Enregistrer","Annuler","Retour",u8"Arrêter",u8"Vérifier",u8"Paramètres","Langue","Indisponible",u8"Périmé","Inconnu","Brouillon",u8"Enregistré","{source} : {value}"}}},
        {"de","Deutsch",false,ReviewStatus::MachineAssisted,{{"Speichern","Abbrechen",u8"Zurück","Stoppen",u8"Prüfen","Einstellungen","Sprache",u8"Nicht verfügbar","Veraltet","Unbekannt","Entwurf","Gespeichert","{source}: {value}"}}},
        {"ru",u8"Русский",false,ReviewStatus::MachineAssisted,{{u8"Сохранить",u8"Отмена",u8"Назад",u8"Остановить",u8"Проверить",u8"Настройки",u8"Язык",u8"Недоступно",u8"Устарело",u8"Неизвестно",u8"Черновик",u8"Сохранено","{source}: {value}"}}},
        {"ja",u8"日本語",false,ReviewStatus::MachineAssisted,{{u8"保存",u8"キャンセル",u8"戻る",u8"停止",u8"確認",u8"設定",u8"言語",u8"利用不可",u8"期限切れ",u8"不明",u8"下書き",u8"保存済み",u8"{source}：{value}"}}},
        {"ko",u8"한국어",false,ReviewStatus::MachineAssisted,{{u8"저장",u8"취소",u8"뒤로",u8"중지",u8"검토",u8"설정",u8"언어",u8"사용 불가",u8"오래된 정보",u8"알 수 없음",u8"초안",u8"저장됨",u8"{source}: {value}"}}},
        {"ar",u8"العربية",true,ReviewStatus::MachineAssisted,{{u8"حفظ",u8"إلغاء",u8"رجوع",u8"إيقاف",u8"مراجعة",u8"الإعدادات",u8"اللغة",u8"غير متاح",u8"قديم",u8"غير معروف",u8"مسودة",u8"تم الحفظ",u8"{source}: {value}"}}},
        {"hi",u8"हिन्दी",false,ReviewStatus::MachineAssisted,{{u8"सहेजें",u8"रद्द करें",u8"वापस",u8"रोकें",u8"समीक्षा",u8"सेटिंग्स",u8"भाषा",u8"अनुपलब्ध",u8"पुराना",u8"अज्ञात",u8"मसौदा",u8"सहेजा गया",u8"{source}: {value}"}}},
        {"bn",u8"বাংলা",false,ReviewStatus::MachineAssisted,{{u8"সংরক্ষণ",u8"বাতিল",u8"ফিরে যান",u8"থামান",u8"পর্যালোচনা",u8"সেটিংস",u8"ভাষা",u8"অনুপলব্ধ",u8"পুরোনো",u8"অজানা",u8"খসড়া",u8"সংরক্ষিত",u8"{source}: {value}"}}},
        {"id","Bahasa Indonesia",false,ReviewStatus::MachineAssisted,{{"Simpan","Batal","Kembali","Hentikan","Tinjau","Pengaturan","Bahasa","Tidak tersedia","Kedaluwarsa","Tidak diketahui","Draf","Tersimpan","{source}: {value}"}}},
        {"tr",u8"Türkçe",false,ReviewStatus::MachineAssisted,{{"Kaydet",u8"İptal",u8"Geri","Durdur",u8"İncele","Ayarlar","Dil",u8"Kullanılamıyor",u8"Güncel değil","Bilinmiyor","Taslak","Kaydedildi","{source}: {value}"}}},
        {"vi",u8"Tiếng Việt",false,ReviewStatus::MachineAssisted,{{u8"Lưu",u8"Hủy",u8"Quay lại",u8"Dừng",u8"Xem lại",u8"Cài đặt",u8"Ngôn ngữ",u8"Không khả dụng",u8"Đã cũ",u8"Không rõ",u8"Bản nháp",u8"Đã lưu",u8"{source}: {value}"}}}
    }};return packs;
}
inline std::string_view messageKeyName(MessageKey key){
    static constexpr std::array<std::string_view,messageKeyCount> names={{"common.save","common.cancel","common.back","common.stop","common.review","common.settings","language.title","state.unavailable","state.stale","state.unknown","review.draft","review.saved","common.source_value"}};
    const auto i=static_cast<std::size_t>(key);return i<names.size()?names[i]:std::string_view{};
}
// Strict scalar validation only, NOT a grapheme, cell-width, bidi or terminal-shaping implementation.
inline bool safeMessageText(std::string_view text){
    if(text.size()>4096)return false;
    for(std::size_t i=0;i<text.size();){const auto first=static_cast<unsigned char>(text[i++]);uint32_t scalar=first;unsigned extra=0;
        if(first<0x80)extra=0;else if(first>=0xc2&&first<=0xdf){scalar=first&31;extra=1;}else if(first>=0xe0&&first<=0xef){scalar=first&15;extra=2;}else if(first>=0xf0&&first<=0xf4){scalar=first&7;extra=3;}else return false;
        if(extra>text.size()-i)return false;
        for(unsigned j=0;j<extra;++j){const auto next=static_cast<unsigned char>(text[i++]);if((next&0xc0)!=0x80)return false;scalar=(scalar<<6)|(next&63);}
        if((extra==1&&scalar<0x80)||(extra==2&&scalar<0x800)||(extra==3&&scalar<0x10000)||scalar>0x10ffff||(scalar>=0xd800&&scalar<=0xdfff)||
            scalar<0x20||(scalar>=0x7f&&scalar<=0x9f)||(scalar>=0x202a&&scalar<=0x202e)||(scalar>=0x2066&&scalar<=0x2069)||scalar==0x2028||scalar==0x2029)return false;
    }
    return true;
}
inline bool templateParameters(std::string_view text,std::vector<std::string_view> *out){
    if(!out||!safeMessageText(text))return false;
    std::vector<std::string_view> names;
    for(std::size_t i=0;i<text.size();){if(text[i]=='}')return false;if(text[i]!='{'){++i;continue;}
        const auto end=text.find('}',i+1);if(end==text.npos||end-i<=1||end-i>33)return false;const auto name=text.substr(i+1,end-i-1);
        for(char ch:name)if(!((ch>='a'&&ch<='z')||ch=='_'))return false;
        if(names.size()>=8)return false;
        names.push_back(name);i=end+1;
    }
    std::sort(names.begin(),names.end());*out=std::move(names);return true;
}
inline bool validTemplateTranslation(std::string_view source,std::string_view translation){std::vector<std::string_view> a,b;
    return !translation.empty()&&templateParameters(source,&a)&&templateParameters(translation,&b)&&a==b;
}
struct LocalizedMessage {std::string_view text,locale;ReviewStatus review=ReviewStatus::Source;bool fallback=false,rtl=false;};
inline LocalizedMessage message(std::string_view locale,MessageKey key){
    const auto index=static_cast<std::size_t>(key);if(index>=messageKeyCount)return {};
    const auto &source=localePacks()[0];const LocalePack *selected=&source;
    for(const auto &pack:localePacks())if(pack.id==locale){selected=&pack;break;}
    const auto translated=selected->messages[index];
    if(selected->id!=locale||!validTemplateTranslation(source.messages[index],translated))return {source.messages[index],source.id,source.review,true,false};
    return {translated,selected->id,selected->review,false,selected->rtl};
}
using MessageArguments=std::vector<std::pair<std::string,std::string>>;
inline bool formatMessage(std::string_view locale,MessageKey key,const MessageArguments &arguments,std::string *out,LocalizedMessage *metadata=nullptr){
    if(!out||arguments.size()>8)return false;
    const auto selected=message(locale,key);if(selected.text.empty())return false;
    std::vector<std::string_view> names;if(!templateParameters(selected.text,&names))return false;
    std::vector<std::string_view> supplied;for(const auto &entry:arguments){if(!safeMessageText(entry.second))return false;supplied.push_back(entry.first);}
    std::sort(supplied.begin(),supplied.end());if(std::adjacent_find(supplied.begin(),supplied.end())!=supplied.end())return false;
    names.erase(std::unique(names.begin(),names.end()),names.end());if(names!=supplied)return false;
    std::string result;
    for(std::size_t i=0;i<selected.text.size();){if(selected.text[i]!='{'){if(result.size()==4096)return false;result+=selected.text[i++];continue;}
        const auto end=selected.text.find('}',i+1);const auto name=selected.text.substr(i+1,end-i-1);
        const auto value=std::find_if(arguments.begin(),arguments.end(),[&](const auto &entry){return entry.first==name;});
        if(value==arguments.end()||value->second.size()>4096-result.size())return false;
        result+=value->second;i=end+1;
    }
    *out=std::move(result);if(metadata)*metadata=selected;return true;
}
} } // namespace om::i18n
