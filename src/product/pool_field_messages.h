// Human pool-declaration labels only. No endpoint, TLS mode, consent phrase or network authority.
#pragma once
#include "localization.h"
namespace om { namespace i18n {
enum class PoolFieldMessageKey { Scope, Mode, TlsUrl, TcpUrl, PlaintextConsent, AddUserPool, RemoveUserPool, Invalid, Count };
constexpr std::size_t poolFieldMessageKeyCount=static_cast<std::size_t>(PoolFieldMessageKey::Count);
struct PoolFieldLocalePack {std::string_view id;bool rtl;ReviewStatus review;std::array<std::string_view,poolFieldMessageKeyCount> messages;};
inline const std::array<PoolFieldLocalePack,16> &poolFieldLocalePacks(){
    static const std::array<PoolFieldLocalePack,16> packs={{
        {"en",false,ReviewStatus::Source,{{"Scope","Mode","TLS URL","TCP URL","Plaintext consent","Add user pool","Remove user pool","Invalid"}}},
        {"zh-Hans",false,ReviewStatus::MachineAssisted,{{u8"\u8303\u56f4",u8"\u6a21\u5f0f","TLS URL","TCP URL",u8"\u660e\u6587\u8bb8\u53ef",u8"\u6dfb\u52a0\u7528\u6237\u77ff\u6c60",u8"\u79fb\u9664\u7528\u6237\u77ff\u6c60",u8"\u65e0\u6548"}}},
        {"zh-Hant",false,ReviewStatus::MachineAssisted,{{u8"\u7bc4\u570d",u8"\u6a21\u5f0f","TLS URL","TCP URL",u8"\u660e\u6587\u8a31\u53ef",u8"\u65b0\u589e\u4f7f\u7528\u8005\u7926\u6c60",u8"\u79fb\u9664\u4f7f\u7528\u8005\u7926\u6c60",u8"\u7121\u6548"}}},
        {"es",false,ReviewStatus::MachineAssisted,{{u8"\u00c1mbito","Modo","TLS URL","TCP URL","Consentimiento de texto plano",u8"A\u00f1adir pool de usuario","Eliminar pool de usuario",u8"No v\u00e1lido"}}},
        {"pt-BR",false,ReviewStatus::MachineAssisted,{{"Escopo","Modo","TLS URL","TCP URL","Consentimento de texto simples",u8"Adicionar pool de usu\u00e1rio",u8"Remover pool de usu\u00e1rio",u8"Inv\u00e1lido"}}},
        {"fr",false,ReviewStatus::MachineAssisted,{{u8"Port\u00e9e","Mode","TLS URL","TCP URL","Consentement au texte en clair","Ajouter un pool utilisateur","Retirer un pool utilisateur","Invalide"}}},
        {"de",false,ReviewStatus::MachineAssisted,{{"Bereich","Modus","TLS URL","TCP URL","Klartextzustimmung","Benutzerpool hinzufuegen","Benutzerpool entfernen",u8"Ung\u00fcltig"}}},
        {"ru",false,ReviewStatus::MachineAssisted,{{u8"\u041e\u0431\u043b\u0430\u0441\u0442\u044c",u8"\u0420\u0435\u0436\u0438\u043c","TLS URL","TCP URL",u8"\u0421\u043e\u0433\u043b\u0430\u0441\u0438\u0435 \u043d\u0430 \u043e\u0442\u043a\u0440\u044b\u0442\u044b\u0439 \u0442\u0435\u043a\u0441\u0442",u8"\u0414\u043e\u0431\u0430\u0432\u0438\u0442\u044c \u043f\u0443\u043b \u043f\u043e\u043b\u044c\u0437\u043e\u0432\u0430\u0442\u0435\u043b\u044f",u8"\u0423\u0434\u0430\u043b\u0438\u0442\u044c \u043f\u0443\u043b \u043f\u043e\u043b\u044c\u0437\u043e\u0432\u0430\u0442\u0435\u043b\u044f",u8"\u041d\u0435\u0434\u043e\u043f\u0443\u0441\u0442\u0438\u043c\u043e"}}},
        {"ja",false,ReviewStatus::MachineAssisted,{{u8"\u7bc4\u56f2",u8"\u30e2\u30fc\u30c9","TLS URL","TCP URL",u8"\u5e73\u6587\u3078\u306e\u540c\u610f",u8"\u30e6\u30fc\u30b6\u30fc\u30d7\u30fc\u30eb\u3092\u8ffd\u52a0",u8"\u30e6\u30fc\u30b6\u30fc\u30d7\u30fc\u30eb\u3092\u524a\u9664",u8"\u7121\u52b9"}}},
        {"ko",false,ReviewStatus::MachineAssisted,{{u8"\ubc94\uc704",u8"\ubaa8\ub4dc","TLS URL","TCP URL",u8"\ud3c9\ubb38 \ub3d9\uc758",u8"\uc0ac\uc6a9\uc790 \ud480 \ucd94\uac00",u8"\uc0ac\uc6a9\uc790 \ud480 \uc81c\uac70",u8"\uc798\ubabb\ub428"}}},
        {"ar",true,ReviewStatus::MachineAssisted,{{u8"\u0627\u0644\u0646\u0637\u0627\u0642",u8"\u0627\u0644\u0648\u0636\u0639","TLS URL","TCP URL",u8"\u0645\u0648\u0627\u0641\u0642\u0629 \u0627\u0644\u0646\u0635 \u0627\u0644\u0635\u0631\u064a\u062d",u8"\u0625\u0636\u0627\u0641\u0629 \u0645\u062c\u0645\u0639 \u0645\u0633\u062a\u062e\u062f\u0645",u8"\u0625\u0632\u0627\u0644\u0629 \u0645\u062c\u0645\u0639 \u0645\u0633\u062a\u062e\u062f\u0645",u8"\u063a\u064a\u0631 \u0635\u0627\u0644\u062d"}}},
        {"hi",false,ReviewStatus::MachineAssisted,{{u8"\u0926\u093e\u092f\u0930\u093e",u8"\u092e\u094b\u0921","TLS URL","TCP URL",u8"\u0938\u093e\u0926\u0947 \u092a\u093e\u0920 \u0915\u0940 \u0938\u0939\u092e\u0924\u093f",u8"\u0909\u092a\u092f\u094b\u0917\u0915\u0930\u094d\u0924\u093e \u092a\u0942\u0932 \u091c\u094b\u0921\u093c\u0947\u0902",u8"\u0909\u092a\u092f\u094b\u0917\u0915\u0930\u094d\u0924\u093e \u092a\u0942\u0932 \u0939\u091f\u093e\u090f\u0901",u8"\u0905\u092e\u093e\u0928\u094d\u092f"}}},
        {"bn",false,ReviewStatus::MachineAssisted,{{u8"\u09aa\u09b0\u09bf\u09b8\u09b0",u8"\u09ae\u09cb\u09a1","TLS URL","TCP URL",u8"\u09b8\u09be\u09a7\u09be\u09b0\u09a3 \u09aa\u09be\u09a0\u09cd\u09af\u09c7\u09b0 \u09b8\u09ae\u09cd\u09ae\u09a4\u09bf",u8"\u09ac\u09cd\u09af\u09ac\u09b9\u09be\u09b0\u0995\u09be\u09b0\u09c0\u09b0 \u09aa\u09c1\u09b2 \u09af\u09cb\u0997 \u0995\u09b0\u09c1\u09a8",u8"\u09ac\u09cd\u09af\u09ac\u09b9\u09be\u09b0\u0995\u09be\u09b0\u09c0\u09b0 \u09aa\u09c1\u09b2 \u09b8\u09b0\u09be\u09a8",u8"\u0985\u09ac\u09c8\u09a7"}}},
        {"id",false,ReviewStatus::MachineAssisted,{{"Cakupan","Mode","TLS URL","TCP URL","Persetujuan teks biasa","Tambah pool pengguna","Hapus pool pengguna","Tidak valid"}}},
        {"tr",false,ReviewStatus::MachineAssisted,{{"Kapsam","Mod","TLS URL","TCP URL",u8"D\u00fcz metin onay\u0131",u8"Kullan\u0131c\u0131 havuzu ekle",u8"Kullan\u0131c\u0131 havuzunu kald\u0131r",u8"Ge\u00e7ersiz"}}},
        {"vi",false,ReviewStatus::MachineAssisted,{{u8"Ph\u1ea1m vi",u8"Ch\u1ebf \u0111\u1ed9","TLS URL","TCP URL",u8"\u0110\u1ed3ng \u00fd v\u0103n b\u1ea3n thu\u1ea7n",u8"Th\u00eam pool ng\u01b0\u1eddi d\u00f9ng",u8"X\u00f3a pool ng\u01b0\u1eddi d\u00f9ng",u8"Kh\u00f4ng h\u1ee3p l\u1ec7"}}}
    }};return packs;
}
inline const std::array<PoolFieldMessageKey,7> &poolFieldKeys(){
    static constexpr std::array<PoolFieldMessageKey,7> keys={{PoolFieldMessageKey::Scope,PoolFieldMessageKey::Mode,PoolFieldMessageKey::TlsUrl,PoolFieldMessageKey::TcpUrl,PoolFieldMessageKey::PlaintextConsent,PoolFieldMessageKey::AddUserPool,PoolFieldMessageKey::RemoveUserPool}};return keys;
}
inline std::string_view poolFieldMessageKeyName(PoolFieldMessageKey key){
    static constexpr std::array<std::string_view,poolFieldMessageKeyCount> names={{"pool.field.scope","pool.field.mode","pool.field.tls_url","pool.field.tcp_url","pool.field.plaintext_consent","pool.field.add_user_pool","pool.field.remove_user_pool","pool.field.invalid"}};
    const auto index=static_cast<std::size_t>(key);return index<names.size()?names[index]:std::string_view{};
}
inline LocalizedMessage poolFieldMessage(std::string_view language,PoolFieldMessageKey key){
    const auto index=static_cast<std::size_t>(key);if(index>=poolFieldMessageKeyCount)return {};
    const auto &source=poolFieldLocalePacks()[0];const auto *selected=&source;for(const auto &pack:poolFieldLocalePacks())if(pack.id==language){selected=&pack;break;}
    if(selected->id!=language||!validTemplateTranslation(source.messages[index],selected->messages[index]))return {source.messages[index],source.id,source.review,true,false};
    return {selected->messages[index],selected->id,selected->review,false,selected->rtl};
}
inline std::string poolFieldEnglish(PoolFieldMessageKey key){return std::string(poolFieldMessage("en",key).text);}
} } // namespace om::i18n
