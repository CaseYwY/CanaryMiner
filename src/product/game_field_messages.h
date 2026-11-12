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
        {"zh-Hans",false,ReviewStatus::MachineAssisted,{{u8"\u6a21\u5f0f",u8"\u76ee\u6807",u8"\u8303\u56f4",u8"\u8f6e\u8be2 ms",u8"\u9000\u51fa\u53bb\u6296 ms",u8"\u6700\u5927\u65f6\u6548 ms",u8"\u65e0\u6548",u8"Windows \u4f1a\u8bdd",u8"Linux \u539f\u751f UID"}}},
        {"zh-Hant",false,ReviewStatus::MachineAssisted,{{u8"\u6a21\u5f0f",u8"\u76ee\u6a19",u8"\u7bc4\u570d",u8"\u8f2a\u8a62 ms",u8"\u9000\u51fa\u53bb\u5f48\u8df3 ms",u8"\u6700\u5927\u6642\u6548 ms",u8"\u7121\u6548",u8"Windows \u5de5\u4f5c\u968e\u6bb5",u8"Linux \u539f\u751f UID"}}},
        {"es",false,ReviewStatus::MachineAssisted,{{"Modo","Objetivo",u8"\u00c1mbito","Sondeo ms",u8"Antirrebote de salida ms",u8"Antig\u00fcedad m\u00e1xima ms",u8"No v\u00e1lido",u8"Sesi\u00f3n de Windows","UID nativo de Linux"}}},
        {"pt-BR",false,ReviewStatus::MachineAssisted,{{"Modo","Alvo","Escopo","Consulta ms",u8"Estabiliza\u00e7\u00e3o de sa\u00edda ms",u8"Idade m\u00e1xima ms",u8"Inv\u00e1lido",u8"Sess\u00e3o Windows","UID nativo Linux"}}},
        {"fr",false,ReviewStatus::MachineAssisted,{{"Mode","Cible",u8"Port\u00e9e","Interrogation ms",u8"Anti-rebond de sortie ms",u8"\u00c2ge maximal ms","Invalide","Session Windows","UID natif Linux"}}},
        {"de",false,ReviewStatus::MachineAssisted,{{"Modus","Ziel","Bereich","Abfrage ms","Austrittsentprellung ms","Max. Alter ms",u8"Ung\u00fcltig","Windows-Sitzung","Native Linux-UID"}}},
        {"ru",false,ReviewStatus::MachineAssisted,{{u8"\u0420\u0435\u0436\u0438\u043c",u8"\u0426\u0435\u043b\u044c",u8"\u041e\u0431\u043b\u0430\u0441\u0442\u044c",u8"\u041e\u043f\u0440\u043e\u0441 ms",u8"\u0417\u0430\u0434\u0435\u0440\u0436\u043a\u0430 \u043f\u043e\u0434\u0442\u0432\u0435\u0440\u0436\u0434\u0435\u043d\u0438\u044f \u0432\u044b\u0445\u043e\u0434\u0430 ms",u8"\u041c\u0430\u043a\u0441. \u0432\u043e\u0437\u0440\u0430\u0441\u0442 ms",u8"\u041d\u0435\u0434\u043e\u043f\u0443\u0441\u0442\u0438\u043c\u043e",u8"\u0421\u0435\u0430\u043d\u0441 Windows",u8"\u041d\u0430\u0442\u0438\u0432\u043d\u044b\u0439 UID Linux"}}},
        {"ja",false,ReviewStatus::MachineAssisted,{{u8"\u30e2\u30fc\u30c9",u8"\u5bfe\u8c61",u8"\u7bc4\u56f2",u8"\u30dd\u30fc\u30ea\u30f3\u30b0 ms",u8"\u7d42\u4e86\u30c7\u30d0\u30a6\u30f3\u30b9 ms",u8"\u6700\u5927\u7d4c\u904e\u6642\u9593 ms",u8"\u7121\u52b9",u8"Windows \u30bb\u30c3\u30b7\u30e7\u30f3",u8"Linux \u30cd\u30a4\u30c6\u30a3\u30d6 UID"}}},
        {"ko",false,ReviewStatus::MachineAssisted,{{u8"\ubaa8\ub4dc",u8"\ub300\uc0c1",u8"\ubc94\uc704",u8"\ud3f4\ub9c1 ms",u8"\uc885\ub8cc \ub514\ubc14\uc6b4\uc2a4 ms",u8"\ucd5c\ub300 \uacbd\uacfc ms",u8"\uc798\ubabb\ub428",u8"Windows \uc138\uc158",u8"Linux \uae30\ubcf8 UID"}}},
        {"ar",true,ReviewStatus::MachineAssisted,{{u8"\u0627\u0644\u0648\u0636\u0639",u8"\u0627\u0644\u0647\u062f\u0641",u8"\u0627\u0644\u0646\u0637\u0627\u0642",u8"\u0627\u0644\u0627\u0633\u062a\u0642\u0635\u0627\u0621 ms",u8"\u062a\u062b\u0628\u064a\u062a \u0627\u0644\u062e\u0631\u0648\u062c ms",u8"\u0627\u0644\u0639\u0645\u0631 \u0627\u0644\u0623\u0642\u0635\u0649 ms",u8"\u063a\u064a\u0631 \u0635\u0627\u0644\u062d",u8"\u062c\u0644\u0633\u0629 Windows",u8"UID \u0623\u0635\u0644\u064a \u0641\u064a Linux"}}},
        {"hi",false,ReviewStatus::MachineAssisted,{{u8"\u092e\u094b\u0921",u8"\u0932\u0915\u094d\u0937\u094d\u092f",u8"\u0926\u093e\u092f\u0930\u093e",u8"\u092a\u094b\u0932 ms",u8"\u0928\u093f\u0915\u093e\u0938 \u0938\u094d\u0925\u093f\u0930\u0940\u0915\u0930\u0923 ms",u8"\u0905\u0927\u093f\u0915\u0924\u092e \u0906\u092f\u0941 ms",u8"\u0905\u092e\u093e\u0928\u094d\u092f",u8"Windows \u0938\u0924\u094d\u0930",u8"Linux \u092e\u0942\u0932 UID"}}},
        {"bn",false,ReviewStatus::MachineAssisted,{{u8"\u09ae\u09cb\u09a1",u8"\u09b2\u0995\u09cd\u09b7\u09cd\u09af",u8"\u09aa\u09b0\u09bf\u09b8\u09b0",u8"\u09aa\u09cb\u09b2 ms",u8"\u09aa\u09cd\u09b0\u09b8\u09cd\u09a5\u09be\u09a8 \u09b8\u09cd\u09a5\u09bf\u09a4\u09bf\u0995\u09b0\u09a3 ms",u8"\u09b8\u09b0\u09cd\u09ac\u09cb\u099a\u09cd\u099a \u09ac\u09af\u09bc\u09b8 ms",u8"\u0985\u09ac\u09c8\u09a7",u8"Windows \u09b8\u09c7\u09b6\u09a8",u8"Linux \u09a8\u09c7\u099f\u09bf\u09ad UID"}}},
        {"id",false,ReviewStatus::MachineAssisted,{{"Mode","Target","Cakupan","Poll ms","Stabilisasi keluar ms","Usia maks ms","Tidak valid","Sesi Windows","UID native Linux"}}},
        {"tr",false,ReviewStatus::MachineAssisted,{{"Mod",u8"Hedef","Kapsam",u8"Yoklama ms",u8"\u00c7\u0131k\u0131\u015f kararl\u0131l\u0131\u011f\u0131 ms",u8"Azami ya\u015f ms",u8"Ge\u00e7ersiz","Windows oturumu",u8"Linux yerel UID"}}},
        {"vi",false,ReviewStatus::MachineAssisted,{{u8"Ch\u1ebf \u0111\u1ed9",u8"M\u1ee5c ti\u00eau",u8"Ph\u1ea1m vi",u8"Th\u0103m d\u00f2 ms",u8"\u1ed4n \u0111\u1ecbnh tho\u00e1t ms",u8"Tu\u1ed5i t\u1ed1i \u0111a ms",u8"Kh\u00f4ng h\u1ee3p l\u1ec7",u8"Phi\u00ean Windows",u8"UID g\u1ed1c Linux"}}}
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
