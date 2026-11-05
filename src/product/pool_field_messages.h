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
        {"zh-Hans",false,ReviewStatus::MachineAssisted,{{u8"范围",u8"模式","TLS URL","TCP URL",u8"明文许可",u8"添加用户矿池",u8"移除用户矿池",u8"无效"}}},
        {"zh-Hant",false,ReviewStatus::MachineAssisted,{{u8"範圍",u8"模式","TLS URL","TCP URL",u8"明文許可",u8"新增使用者礦池",u8"移除使用者礦池",u8"無效"}}},
        {"es",false,ReviewStatus::MachineAssisted,{{u8"Ámbito","Modo","TLS URL","TCP URL","Consentimiento de texto plano",u8"Añadir pool de usuario","Eliminar pool de usuario",u8"No válido"}}},
        {"pt-BR",false,ReviewStatus::MachineAssisted,{{"Escopo","Modo","TLS URL","TCP URL","Consentimento de texto simples",u8"Adicionar pool de usuário",u8"Remover pool de usuário",u8"Inválido"}}},
        {"fr",false,ReviewStatus::MachineAssisted,{{u8"Portée","Mode","TLS URL","TCP URL","Consentement au texte en clair","Ajouter un pool utilisateur","Retirer un pool utilisateur","Invalide"}}},
        {"de",false,ReviewStatus::MachineAssisted,{{"Bereich","Modus","TLS URL","TCP URL","Klartextzustimmung","Benutzerpool hinzufuegen","Benutzerpool entfernen",u8"Ungültig"}}},
        {"ru",false,ReviewStatus::MachineAssisted,{{u8"Область",u8"Режим","TLS URL","TCP URL",u8"Согласие на открытый текст",u8"Добавить пул пользователя",u8"Удалить пул пользователя",u8"Недопустимо"}}},
        {"ja",false,ReviewStatus::MachineAssisted,{{u8"範囲",u8"モード","TLS URL","TCP URL",u8"平文への同意",u8"ユーザープールを追加",u8"ユーザープールを削除",u8"無効"}}},
        {"ko",false,ReviewStatus::MachineAssisted,{{u8"범위",u8"모드","TLS URL","TCP URL",u8"평문 동의",u8"사용자 풀 추가",u8"사용자 풀 제거",u8"잘못됨"}}},
        {"ar",true,ReviewStatus::MachineAssisted,{{u8"النطاق",u8"الوضع","TLS URL","TCP URL",u8"موافقة النص الصريح",u8"إضافة مجمع مستخدم",u8"إزالة مجمع مستخدم",u8"غير صالح"}}},
        {"hi",false,ReviewStatus::MachineAssisted,{{u8"दायरा",u8"मोड","TLS URL","TCP URL",u8"सादे पाठ की सहमति",u8"उपयोगकर्ता पूल जोड़ें",u8"उपयोगकर्ता पूल हटाएँ",u8"अमान्य"}}},
        {"bn",false,ReviewStatus::MachineAssisted,{{u8"পরিসর",u8"মোড","TLS URL","TCP URL",u8"সাধারণ পাঠ্যের সম্মতি",u8"ব্যবহারকারীর পুল যোগ করুন",u8"ব্যবহারকারীর পুল সরান",u8"অবৈধ"}}},
        {"id",false,ReviewStatus::MachineAssisted,{{"Cakupan","Mode","TLS URL","TCP URL","Persetujuan teks biasa","Tambah pool pengguna","Hapus pool pengguna","Tidak valid"}}},
        {"tr",false,ReviewStatus::MachineAssisted,{{"Kapsam","Mod","TLS URL","TCP URL",u8"Düz metin onayı",u8"Kullanıcı havuzu ekle",u8"Kullanıcı havuzunu kaldır",u8"Geçersiz"}}},
        {"vi",false,ReviewStatus::MachineAssisted,{{u8"Phạm vi",u8"Chế độ","TLS URL","TCP URL",u8"Đồng ý văn bản thuần",u8"Thêm pool người dùng",u8"Xóa pool người dùng",u8"Không hợp lệ"}}}
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
