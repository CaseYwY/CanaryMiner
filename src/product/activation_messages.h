// Human activation descriptions only. No enum parsing, permission, scheduling or runtime activation.
#pragma once
#include "localization.h"
#include "activation_source.h"
namespace om { namespace i18n {
struct ActivationLocalePack {std::string_view id;bool rtl;ReviewStatus review;std::array<std::string_view,activationMessageKeyCount> messages;};
inline const std::array<ActivationLocalePack,16> &activationLocalePacks(){
    static const std::array<ActivationLocalePack,16> packs={{
        {"en",false,ReviewStatus::Source,{{activationEnglishText(ActivationMessageKey::Presentation),activationEnglishText(ActivationMessageKey::WorkBoundary),activationEnglishText(ActivationMessageKey::NextStart),activationEnglishText(ActivationMessageKey::SeparatePermission),activationEnglishText(ActivationMessageKey::Unsupported)}}},
        {"zh-Hans",false,ReviewStatus::MachineAssisted,{{u8"即时显示",u8"安全工作边界",u8"下次明确启动；不会自动重启",u8"需要单独许可",u8"不支持"}}},
        {"zh-Hant",false,ReviewStatus::MachineAssisted,{{u8"即時顯示",u8"安全工作邊界",u8"下次明確啟動；不會自動重啟",u8"需要單獨許可",u8"不支援"}}},
        {"es",false,ReviewStatus::MachineAssisted,{{u8"presentación en vivo",u8"límite de trabajo seguro",u8"siguiente inicio explícito; sin reinicio automático","se requiere permiso separado","no compatible"}}},
        {"pt-BR",false,ReviewStatus::MachineAssisted,{{u8"apresentação ao vivo","limite seguro de trabalho",u8"próximo início explícito; sem reinício automático",u8"permissão separada necessária",u8"não suportado"}}},
        {"fr",false,ReviewStatus::MachineAssisted,{{u8"affichage immédiat",u8"limite de travail sûre",u8"prochain démarrage explicite ; aucun redémarrage automatique",u8"autorisation distincte requise",u8"non pris en charge"}}},
        {"de",false,ReviewStatus::MachineAssisted,{{"sofortige Darstellung","sichere Arbeitsgrenze",u8"nächster ausdrücklicher Start; kein automatischer Neustart","gesonderte Erlaubnis erforderlich",u8"nicht unterstützt"}}},
        {"ru",false,ReviewStatus::MachineAssisted,{{u8"отображение в реальном времени",u8"безопасная граница работы",u8"следующий явный запуск; без автоматического перезапуска",u8"требуется отдельное разрешение",u8"не поддерживается"}}},
        {"ja",false,ReviewStatus::MachineAssisted,{{u8"即時表示",u8"安全な作業境界",u8"次の明示的起動。自動再起動なし",u8"個別の許可が必要",u8"非対応"}}},
        {"ko",false,ReviewStatus::MachineAssisted,{{u8"실시간 표시",u8"안전한 작업 경계",u8"다음 명시적 시작; 자동 재시작 없음",u8"별도 허가 필요",u8"지원되지 않음"}}},
        {"ar",true,ReviewStatus::MachineAssisted,{{u8"عرض مباشر",u8"حد عمل آمن",u8"البدء الصريح التالي؛ لا إعادة تشغيل تلقائية",u8"يتطلب إذنا منفصلا",u8"غير مدعوم"}}},
        {"hi",false,ReviewStatus::MachineAssisted,{{u8"तत्काल प्रदर्शन",u8"सुरक्षित कार्य सीमा",u8"अगला स्पष्ट आरंभ; स्वचालित पुनः आरंभ नहीं",u8"अलग अनुमति आवश्यक",u8"समर्थित नहीं"}}},
        {"bn",false,ReviewStatus::MachineAssisted,{{u8"তাৎক্ষণিক প্রদর্শন",u8"নিরাপদ কাজের সীমানা",u8"পরের স্পষ্ট শুরু; স্বয়ংক্রিয় পুনরারম্ভ নয়",u8"পৃথক অনুমতি প্রয়োজন",u8"সমর্থিত নয়"}}},
        {"id",false,ReviewStatus::MachineAssisted,{{"tampilan langsung","batas kerja aman","mulai eksplisit berikutnya; tanpa mulai ulang otomatis","memerlukan izin terpisah","tidak didukung"}}},
        {"tr",false,ReviewStatus::MachineAssisted,{{u8"anlık görünüm",u8"güvenli iş sınırı",u8"sonraki açık başlatma; otomatik yeniden başlatma yok",u8"ayrı izin gerekli","desteklenmiyor"}}},
        {"vi",false,ReviewStatus::MachineAssisted,{{u8"hiển thị trực tiếp",u8"ranh giới công việc an toàn",u8"lần khởi động rõ ràng tiếp theo; không tự khởi động lại",u8"cần quyền riêng",u8"không được hỗ trợ"}}}
    }};return packs;
}
inline std::string_view activationMessageKeyName(ActivationMessageKey key){
    static constexpr std::array<std::string_view,activationMessageKeyCount> names={{"activation.description.presentation","activation.description.work_boundary","activation.description.next_start","activation.description.separate_permission","activation.description.unsupported"}};
    const auto index=static_cast<std::size_t>(key);return index<names.size()?names[index]:std::string_view{};
}
inline LocalizedMessage activationMessage(std::string_view language,ActivationMessageKey key){
    const auto index=static_cast<std::size_t>(key);if(index>=activationMessageKeyCount)return {};
    const auto &source=activationLocalePacks()[0];const auto *selected=&source;for(const auto &pack:activationLocalePacks())if(pack.id==language){selected=&pack;break;}
    if(selected->id!=language||!validTemplateTranslation(source.messages[index],selected->messages[index]))return {source.messages[index],source.id,source.review,true,false};
    return {selected->messages[index],selected->id,selected->review,false,selected->rtl};
}
} } // namespace om::i18n
