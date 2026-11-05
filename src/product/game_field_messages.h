// Game-panel human labels only. Machine modes, adapter identities, timings and private targets stay independent.
#pragma once
#include "localization.h"
namespace om { namespace i18n {
enum class GameFieldMessageKey { Mode, Target, Scope, PollMs, ExitDebounceMs, MaximumAgeMs, Invalid, WindowsSession, LinuxNativeUid, Count };
constexpr std::size_t gameFieldMessageKeyCount=static_cast<std::size_t>(GameFieldMessageKey::Count);
struct GameFieldLocalePack {std::string_view id;bool rtl;ReviewStatus review;std::array<std::string_view,gameFieldMessageKeyCount> messages;};
inline const std::array<GameFieldLocalePack,16> &gameFieldLocalePacks(){
    static const std::array<GameFieldLocalePack,16> packs={{
        {"en",false,ReviewStatus::Source,{{"Mode","Target","Scope","Poll ms","Exit debounce ms","Max age ms","Invalid","Windows session","Linux native UID"}}},
        {"zh-Hans",false,ReviewStatus::MachineAssisted,{{u8"模式",u8"目标",u8"范围",u8"轮询 ms",u8"退出去抖 ms",u8"最大时效 ms",u8"无效",u8"Windows 会话",u8"Linux 原生 UID"}}},
        {"zh-Hant",false,ReviewStatus::MachineAssisted,{{u8"模式",u8"目標",u8"範圍",u8"輪詢 ms",u8"退出去彈跳 ms",u8"最大時效 ms",u8"無效",u8"Windows 工作階段",u8"Linux 原生 UID"}}},
        {"es",false,ReviewStatus::MachineAssisted,{{"Modo","Objetivo",u8"Ámbito","Sondeo ms",u8"Antirrebote de salida ms",u8"Antigüedad máxima ms",u8"No válido",u8"Sesión de Windows","UID nativo de Linux"}}},
        {"pt-BR",false,ReviewStatus::MachineAssisted,{{"Modo","Alvo","Escopo","Consulta ms",u8"Estabilização de saída ms",u8"Idade máxima ms",u8"Inválido",u8"Sessão Windows","UID nativo Linux"}}},
        {"fr",false,ReviewStatus::MachineAssisted,{{"Mode","Cible",u8"Portée","Interrogation ms",u8"Anti-rebond de sortie ms",u8"Âge maximal ms","Invalide","Session Windows","UID natif Linux"}}},
        {"de",false,ReviewStatus::MachineAssisted,{{"Modus","Ziel","Bereich","Abfrage ms","Austrittsentprellung ms","Max. Alter ms",u8"Ungültig","Windows-Sitzung","Native Linux-UID"}}},
        {"ru",false,ReviewStatus::MachineAssisted,{{u8"Режим",u8"Цель",u8"Область",u8"Опрос ms",u8"Задержка подтверждения выхода ms",u8"Макс. возраст ms",u8"Недопустимо",u8"Сеанс Windows",u8"Нативный UID Linux"}}},
        {"ja",false,ReviewStatus::MachineAssisted,{{u8"モード",u8"対象",u8"範囲",u8"ポーリング ms",u8"終了デバウンス ms",u8"最大経過時間 ms",u8"無効",u8"Windows セッション",u8"Linux ネイティブ UID"}}},
        {"ko",false,ReviewStatus::MachineAssisted,{{u8"모드",u8"대상",u8"범위",u8"폴링 ms",u8"종료 디바운스 ms",u8"최대 경과 ms",u8"잘못됨",u8"Windows 세션",u8"Linux 기본 UID"}}},
        {"ar",true,ReviewStatus::MachineAssisted,{{u8"الوضع",u8"الهدف",u8"النطاق",u8"الاستقصاء ms",u8"تثبيت الخروج ms",u8"العمر الأقصى ms",u8"غير صالح",u8"جلسة Windows",u8"UID أصلي في Linux"}}},
        {"hi",false,ReviewStatus::MachineAssisted,{{u8"मोड",u8"लक्ष्य",u8"दायरा",u8"पोल ms",u8"निकास स्थिरीकरण ms",u8"अधिकतम आयु ms",u8"अमान्य",u8"Windows सत्र",u8"Linux मूल UID"}}},
        {"bn",false,ReviewStatus::MachineAssisted,{{u8"মোড",u8"লক্ষ্য",u8"পরিসর",u8"পোল ms",u8"প্রস্থান স্থিতিকরণ ms",u8"সর্বোচ্চ বয়স ms",u8"অবৈধ",u8"Windows সেশন",u8"Linux নেটিভ UID"}}},
        {"id",false,ReviewStatus::MachineAssisted,{{"Mode","Target","Cakupan","Poll ms","Stabilisasi keluar ms","Usia maks ms","Tidak valid","Sesi Windows","UID native Linux"}}},
        {"tr",false,ReviewStatus::MachineAssisted,{{"Mod",u8"Hedef","Kapsam",u8"Yoklama ms",u8"Çıkış kararlılığı ms",u8"Azami yaş ms",u8"Geçersiz","Windows oturumu",u8"Linux yerel UID"}}},
        {"vi",false,ReviewStatus::MachineAssisted,{{u8"Chế độ",u8"Mục tiêu",u8"Phạm vi",u8"Thăm dò ms",u8"Ổn định thoát ms",u8"Tuổi tối đa ms",u8"Không hợp lệ",u8"Phiên Windows",u8"UID gốc Linux"}}}
    }};return packs;
}
inline const std::array<GameFieldMessageKey,6> &gameFieldKeys(){
    static constexpr std::array<GameFieldMessageKey,6> keys={{GameFieldMessageKey::Mode,GameFieldMessageKey::Target,GameFieldMessageKey::Scope,GameFieldMessageKey::PollMs,GameFieldMessageKey::ExitDebounceMs,GameFieldMessageKey::MaximumAgeMs}};return keys;
}
inline std::string_view gameFieldMessageKeyName(GameFieldMessageKey key){
    static constexpr std::array<std::string_view,gameFieldMessageKeyCount> names={{"game.field.mode","game.field.target","game.field.scope","game.field.poll_ms","game.field.exit_debounce_ms","game.field.maximum_age_ms","game.value.invalid_label","game.value.windows_session","game.value.linux_native_uid"}};
    const auto index=static_cast<std::size_t>(key);return index<names.size()?names[index]:std::string_view{};
}
inline LocalizedMessage gameFieldMessage(std::string_view language,GameFieldMessageKey key){
    const auto index=static_cast<std::size_t>(key);if(index>=gameFieldMessageKeyCount)return {};
    const auto &source=gameFieldLocalePacks()[0];const auto *selected=&source;for(const auto &pack:gameFieldLocalePacks())if(pack.id==language){selected=&pack;break;}
    if(selected->id!=language||!validTemplateTranslation(source.messages[index],selected->messages[index]))return {source.messages[index],source.id,source.review,true,false};
    return {selected->messages[index],selected->id,selected->review,false,selected->rtl};
}
inline std::string gameFieldEnglish(GameFieldMessageKey key){return std::string(gameFieldMessage("en",key).text);}
} } // namespace om::i18n
