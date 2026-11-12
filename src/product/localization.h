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
        {"zh-Hans",u8"\u7b80\u4f53\u4e2d\u6587",false,ReviewStatus::MachineAssisted,{{u8"\u4fdd\u5b58",u8"\u53d6\u6d88",u8"\u8fd4\u56de",u8"\u505c\u6b62",u8"\u5ba1\u9605",u8"\u8bbe\u7f6e",u8"\u8bed\u8a00",u8"\u4e0d\u53ef\u7528",u8"\u5df2\u8fc7\u671f",u8"\u672a\u77e5",u8"\u8349\u7a3f",u8"\u5df2\u4fdd\u5b58",u8"{source}\uff1a{value}"}}},
        {"zh-Hant",u8"\u7e41\u9ad4\u4e2d\u6587",false,ReviewStatus::MachineAssisted,{{u8"\u5132\u5b58",u8"\u53d6\u6d88",u8"\u8fd4\u56de",u8"\u505c\u6b62",u8"\u6aa2\u95b1",u8"\u8a2d\u5b9a",u8"\u8a9e\u8a00",u8"\u7121\u6cd5\u4f7f\u7528",u8"\u5df2\u904e\u671f",u8"\u672a\u77e5",u8"\u8349\u7a3f",u8"\u5df2\u5132\u5b58",u8"{source}\uff1a{value}"}}},
        {"es",u8"Espa\u00f1ol",false,ReviewStatus::MachineAssisted,{{"Guardar","Cancelar",u8"Atr\u00e1s","Detener","Revisar",u8"Configuraci\u00f3n","Idioma","No disponible","Desactualizado","Desconocido","Borrador","Guardado","{source}: {value}"}}},
        {"pt-BR",u8"Portugu\u00eas (Brasil)",false,ReviewStatus::MachineAssisted,{{"Salvar","Cancelar","Voltar","Parar","Revisar",u8"Configura\u00e7\u00f5es","Idioma",u8"Indispon\u00edvel","Desatualizado","Desconhecido","Rascunho","Salvo","{source}: {value}"}}},
        {"fr",u8"Fran\u00e7ais",false,ReviewStatus::MachineAssisted,{{"Enregistrer","Annuler","Retour",u8"Arr\u00eater",u8"V\u00e9rifier",u8"Param\u00e8tres","Langue","Indisponible",u8"P\u00e9rim\u00e9","Inconnu","Brouillon",u8"Enregistr\u00e9","{source} : {value}"}}},
        {"de","Deutsch",false,ReviewStatus::MachineAssisted,{{"Speichern","Abbrechen",u8"Zur\u00fcck","Stoppen",u8"Pr\u00fcfen","Einstellungen","Sprache",u8"Nicht verf\u00fcgbar","Veraltet","Unbekannt","Entwurf","Gespeichert","{source}: {value}"}}},
        {"ru",u8"\u0420\u0443\u0441\u0441\u043a\u0438\u0439",false,ReviewStatus::MachineAssisted,{{u8"\u0421\u043e\u0445\u0440\u0430\u043d\u0438\u0442\u044c",u8"\u041e\u0442\u043c\u0435\u043d\u0430",u8"\u041d\u0430\u0437\u0430\u0434",u8"\u041e\u0441\u0442\u0430\u043d\u043e\u0432\u0438\u0442\u044c",u8"\u041f\u0440\u043e\u0432\u0435\u0440\u0438\u0442\u044c",u8"\u041d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438",u8"\u042f\u0437\u044b\u043a",u8"\u041d\u0435\u0434\u043e\u0441\u0442\u0443\u043f\u043d\u043e",u8"\u0423\u0441\u0442\u0430\u0440\u0435\u043b\u043e",u8"\u041d\u0435\u0438\u0437\u0432\u0435\u0441\u0442\u043d\u043e",u8"\u0427\u0435\u0440\u043d\u043e\u0432\u0438\u043a",u8"\u0421\u043e\u0445\u0440\u0430\u043d\u0435\u043d\u043e","{source}: {value}"}}},
        {"ja",u8"\u65e5\u672c\u8a9e",false,ReviewStatus::MachineAssisted,{{u8"\u4fdd\u5b58",u8"\u30ad\u30e3\u30f3\u30bb\u30eb",u8"\u623b\u308b",u8"\u505c\u6b62",u8"\u78ba\u8a8d",u8"\u8a2d\u5b9a",u8"\u8a00\u8a9e",u8"\u5229\u7528\u4e0d\u53ef",u8"\u671f\u9650\u5207\u308c",u8"\u4e0d\u660e",u8"\u4e0b\u66f8\u304d",u8"\u4fdd\u5b58\u6e08\u307f",u8"{source}\uff1a{value}"}}},
        {"ko",u8"\ud55c\uad6d\uc5b4",false,ReviewStatus::MachineAssisted,{{u8"\uc800\uc7a5",u8"\ucde8\uc18c",u8"\ub4a4\ub85c",u8"\uc911\uc9c0",u8"\uac80\ud1a0",u8"\uc124\uc815",u8"\uc5b8\uc5b4",u8"\uc0ac\uc6a9 \ubd88\uac00",u8"\uc624\ub798\ub41c \uc815\ubcf4",u8"\uc54c \uc218 \uc5c6\uc74c",u8"\ucd08\uc548",u8"\uc800\uc7a5\ub428",u8"{source}: {value}"}}},
        {"ar",u8"\u0627\u0644\u0639\u0631\u0628\u064a\u0629",true,ReviewStatus::MachineAssisted,{{u8"\u062d\u0641\u0638",u8"\u0625\u0644\u063a\u0627\u0621",u8"\u0631\u062c\u0648\u0639",u8"\u0625\u064a\u0642\u0627\u0641",u8"\u0645\u0631\u0627\u062c\u0639\u0629",u8"\u0627\u0644\u0625\u0639\u062f\u0627\u062f\u0627\u062a",u8"\u0627\u0644\u0644\u063a\u0629",u8"\u063a\u064a\u0631 \u0645\u062a\u0627\u062d",u8"\u0642\u062f\u064a\u0645",u8"\u063a\u064a\u0631 \u0645\u0639\u0631\u0648\u0641",u8"\u0645\u0633\u0648\u062f\u0629",u8"\u062a\u0645 \u0627\u0644\u062d\u0641\u0638",u8"{source}: {value}"}}},
        {"hi",u8"\u0939\u093f\u0928\u094d\u0926\u0940",false,ReviewStatus::MachineAssisted,{{u8"\u0938\u0939\u0947\u091c\u0947\u0902",u8"\u0930\u0926\u094d\u0926 \u0915\u0930\u0947\u0902",u8"\u0935\u093e\u092a\u0938",u8"\u0930\u094b\u0915\u0947\u0902",u8"\u0938\u092e\u0940\u0915\u094d\u0937\u093e",u8"\u0938\u0947\u091f\u093f\u0902\u0917\u094d\u0938",u8"\u092d\u093e\u0937\u093e",u8"\u0905\u0928\u0941\u092a\u0932\u092c\u094d\u0927",u8"\u092a\u0941\u0930\u093e\u0928\u093e",u8"\u0905\u091c\u094d\u091e\u093e\u0924",u8"\u092e\u0938\u094c\u0926\u093e",u8"\u0938\u0939\u0947\u091c\u093e \u0917\u092f\u093e",u8"{source}: {value}"}}},
        {"bn",u8"\u09ac\u09be\u0982\u09b2\u09be",false,ReviewStatus::MachineAssisted,{{u8"\u09b8\u0982\u09b0\u0995\u09cd\u09b7\u09a3",u8"\u09ac\u09be\u09a4\u09bf\u09b2",u8"\u09ab\u09bf\u09b0\u09c7 \u09af\u09be\u09a8",u8"\u09a5\u09be\u09ae\u09be\u09a8",u8"\u09aa\u09b0\u09cd\u09af\u09be\u09b2\u09cb\u099a\u09a8\u09be",u8"\u09b8\u09c7\u099f\u09bf\u0982\u09b8",u8"\u09ad\u09be\u09b7\u09be",u8"\u0985\u09a8\u09c1\u09aa\u09b2\u09ac\u09cd\u09a7",u8"\u09aa\u09c1\u09b0\u09cb\u09a8\u09cb",u8"\u0985\u099c\u09be\u09a8\u09be",u8"\u0996\u09b8\u09a1\u09bc\u09be",u8"\u09b8\u0982\u09b0\u0995\u09cd\u09b7\u09bf\u09a4",u8"{source}: {value}"}}},
        {"id","Bahasa Indonesia",false,ReviewStatus::MachineAssisted,{{"Simpan","Batal","Kembali","Hentikan","Tinjau","Pengaturan","Bahasa","Tidak tersedia","Kedaluwarsa","Tidak diketahui","Draf","Tersimpan","{source}: {value}"}}},
        {"tr",u8"T\u00fcrk\u00e7e",false,ReviewStatus::MachineAssisted,{{"Kaydet",u8"\u0130ptal",u8"Geri","Durdur",u8"\u0130ncele","Ayarlar","Dil",u8"Kullan\u0131lam\u0131yor",u8"G\u00fcncel de\u011fil","Bilinmiyor","Taslak","Kaydedildi","{source}: {value}"}}},
        {"vi",u8"Ti\u1ebfng Vi\u1ec7t",false,ReviewStatus::MachineAssisted,{{u8"L\u01b0u",u8"H\u1ee7y",u8"Quay l\u1ea1i",u8"D\u1eebng",u8"Xem l\u1ea1i",u8"C\u00e0i \u0111\u1eb7t",u8"Ng\u00f4n ng\u1eef",u8"Kh\u00f4ng kh\u1ea3 d\u1ee5ng",u8"\u0110\u00e3 c\u0169",u8"Kh\u00f4ng r\u00f5",u8"B\u1ea3n nh\u00e1p",u8"\u0110\u00e3 l\u01b0u",u8"{source}: {value}"}}}
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
