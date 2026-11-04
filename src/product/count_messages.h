// Complete count-domain templates, not numeric plural rules. Non-English text remains machine-assisted draft.
#pragma once
#include "localization.h"
#include <optional>
namespace om { namespace i18n {
enum class CountMessageKey { Workers, AcceptedShares, Count };
using CountPluralForms=std::array<std::string_view,6>; // zero, one, two, few, many, other
struct CountLocalePack {std::string_view id;bool rtl;ReviewStatus review;std::array<CountPluralForms,2> messages;};
constexpr CountPluralForms uniformCountForms(std::string_view text){return {{text,text,text,text,text,text}};}
// Authoring shorthand for languages whose relevant forms are one versus plural; ICU still chooses the category.
constexpr CountPluralForms binaryCountForms(std::string_view one,std::string_view plural){return {{plural,one,plural,plural,plural,plural}};}
inline const std::array<CountLocalePack,16> &countLocalePacks(){
    static constexpr std::array<CountLocalePack,16> packs={{
        {"en",false,ReviewStatus::Source,{{binaryCountForms("{count} worker","{count} workers"),binaryCountForms("{count} accepted share","{count} accepted shares")}}},
        {"zh-Hans",false,ReviewStatus::MachineAssisted,{{uniformCountForms(u8"{count} 个工作进程"),uniformCountForms(u8"{count} 个已接受份额")}}},
        {"zh-Hant",false,ReviewStatus::MachineAssisted,{{uniformCountForms(u8"{count} 個工作行程"),uniformCountForms(u8"{count} 個已接受份額")}}},
        {"es",false,ReviewStatus::MachineAssisted,{{binaryCountForms("{count} proceso de trabajo","{count} procesos de trabajo"),binaryCountForms(u8"{count} participación aceptada",u8"{count} participaciones aceptadas")}}},
        {"pt-BR",false,ReviewStatus::MachineAssisted,{{binaryCountForms("{count} processo de trabalho","{count} processos de trabalho"),binaryCountForms(u8"{count} participação aceita",u8"{count} participações aceitas")}}},
        {"fr",false,ReviewStatus::MachineAssisted,{{uniformCountForms("{count} processus de travail"),binaryCountForms(u8"{count} part acceptée",u8"{count} parts acceptées")}}},
        {"de",false,ReviewStatus::MachineAssisted,{{binaryCountForms("{count} Arbeitsprozess","{count} Arbeitsprozesse"),binaryCountForms("{count} akzeptierter Share","{count} akzeptierte Shares")}}},
        {"ru",false,ReviewStatus::MachineAssisted,{{
            {{u8"{count} рабочих процессов",u8"{count} рабочий процесс",u8"{count} рабочих процесса",u8"{count} рабочих процесса",u8"{count} рабочих процессов",u8"{count} рабочего процесса"}},
            {{u8"{count} принятых долей",u8"{count} принятая доля",u8"{count} принятые доли",u8"{count} принятые доли",u8"{count} принятых долей",u8"{count} принятой доли"}}}}},
        {"ja",false,ReviewStatus::MachineAssisted,{{uniformCountForms(u8"{count} ワーカー"),uniformCountForms(u8"{count} 件の承認済みシェア")}}},
        {"ko",false,ReviewStatus::MachineAssisted,{{uniformCountForms(u8"워커 {count}개"),uniformCountForms(u8"승인된 공유 {count}개")}}},
        {"ar",true,ReviewStatus::MachineAssisted,{{
            {{u8"لا عمّال ({count})",u8"عامل واحد ({count})",u8"عاملان ({count})",u8"{count} عمّال",u8"{count} عاملًا",u8"{count} عامل"}},
            {{u8"لا حصص مقبولة ({count})",u8"حصة واحدة مقبولة ({count})",u8"حصتان مقبولتان ({count})",u8"{count} حصص مقبولة",u8"{count} حصة مقبولة",u8"{count} حصة مقبولة"}}}}},
        {"hi",false,ReviewStatus::MachineAssisted,{{uniformCountForms(u8"{count} वर्कर"),uniformCountForms(u8"{count} स्वीकृत शेयर")}}},
        {"bn",false,ReviewStatus::MachineAssisted,{{uniformCountForms(u8"{count}টি ওয়ার্কার"),uniformCountForms(u8"{count}টি গৃহীত শেয়ার")}}},
        {"id",false,ReviewStatus::MachineAssisted,{{uniformCountForms("{count} proses pekerja"),uniformCountForms("{count} share diterima")}}},
        {"tr",false,ReviewStatus::MachineAssisted,{{uniformCountForms(u8"{count} işçi süreci"),uniformCountForms(u8"{count} kabul edilen paylaşım")}}},
        {"vi",false,ReviewStatus::MachineAssisted,{{uniformCountForms(u8"{count} tiến trình worker"),uniformCountForms(u8"{count} share được chấp nhận")}}}
    }};return packs;
}
inline std::string_view countMessageKeyName(CountMessageKey key){switch(key){case CountMessageKey::Workers:return "counts.workers";case CountMessageKey::AcceptedShares:return "counts.accepted_shares";default:return {};}}
inline std::string_view neutralCountTemplate(CountMessageKey key){switch(key){case CountMessageKey::Workers:return "Workers: {count}";case CountMessageKey::AcceptedShares:return "Accepted shares: {count}";default:return {};}}
inline std::optional<std::size_t> countCategoryIndex(std::string_view category){
    static constexpr std::array<std::string_view,6> names={{"zero","one","two","few","many","other"}};
    for(std::size_t i=0;i<names.size();++i)if(names[i]==category)return i;
    return {};
}
} } // namespace om::i18n
