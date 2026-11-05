// Human manual-reference field labels only; no quote, FX, timestamp, income or control authority.
#pragma once
#include "localization.h"
namespace om { namespace i18n {
enum class ManualMarketFieldMessageKey { DisplayEnabled, PrlPrice, PrlReference, FxEnabled, FxPrice, FxReference, Validity, MaximumFxSkew, Count };
constexpr std::size_t manualMarketFieldMessageKeyCount=static_cast<std::size_t>(ManualMarketFieldMessageKey::Count);
struct ManualMarketFieldLocalePack {std::string_view id;bool rtl;ReviewStatus review;std::array<std::string_view,manualMarketFieldMessageKeyCount> messages;};
inline const std::array<ManualMarketFieldLocalePack,16> &manualMarketFieldLocalePacks(){
    static const std::array<ManualMarketFieldLocalePack,16> packs={{
        {"en",false,ReviewStatus::Source,{{"Display enabled","PRL price (USDT/PRL)","PRL reference UTC seconds","FX enabled","FX price (USD/USDT)","FX reference UTC seconds","Validity seconds","Maximum FX skew seconds"}}},
        {"zh-Hans",false,ReviewStatus::MachineAssisted,{{u8"启用显示",u8"PRL 价格（USDT/PRL）",u8"PRL 参考 UTC 秒",u8"启用 FX",u8"FX 价格（USD/USDT）",u8"FX 参考 UTC 秒",u8"有效期秒数",u8"最大 FX 参考时间差秒数"}}},
        {"zh-Hant",false,ReviewStatus::MachineAssisted,{{u8"啟用顯示",u8"PRL 價格（USDT/PRL）",u8"PRL 參考 UTC 秒",u8"啟用 FX",u8"FX 價格（USD/USDT）",u8"FX 參考 UTC 秒",u8"有效期秒數",u8"最大 FX 參考時間差秒數"}}},
        {"es",false,ReviewStatus::MachineAssisted,{{u8"Visualización habilitada","Precio PRL (USDT/PRL)","Referencia PRL en segundos UTC","FX habilitado","Precio FX (USD/USDT)","Referencia FX en segundos UTC","Validez en segundos",u8"Desfase máximo FX en segundos"}}},
        {"pt-BR",false,ReviewStatus::MachineAssisted,{{u8"Exibição habilitada",u8"Preço PRL (USDT/PRL)",u8"Referência PRL em segundos UTC","FX habilitado",u8"Preço FX (USD/USDT)",u8"Referência FX em segundos UTC","Validade em segundos",u8"Desvio máximo FX em segundos"}}},
        {"fr",false,ReviewStatus::MachineAssisted,{{u8"Affichage activé","Prix PRL (USDT/PRL)",u8"Référence PRL en secondes UTC",u8"FX activé","Prix FX (USD/USDT)",u8"Référence FX en secondes UTC",u8"Validité en secondes",u8"Écart maximal FX en secondes"}}},
        {"de",false,ReviewStatus::MachineAssisted,{{"Anzeige aktiviert","PRL-Preis (USDT/PRL)","PRL-Referenz in UTC-Sekunden","FX aktiviert","FX-Preis (USD/USDT)","FX-Referenz in UTC-Sekunden",u8"Gültigkeit in Sekunden","Maximale FX-Zeitabweichung in Sekunden"}}},
        {"ru",false,ReviewStatus::MachineAssisted,{{u8"Отображение включено",u8"Цена PRL (USDT/PRL)",u8"Время справочного PRL в секундах UTC",u8"FX включён",u8"Цена FX (USD/USDT)",u8"Время справочного FX в секундах UTC",u8"Срок действия в секундах",u8"Максимальный сдвиг времени FX в секундах"}}},
        {"ja",false,ReviewStatus::MachineAssisted,{{u8"表示を有効化",u8"PRL 価格（USDT/PRL）",u8"PRL 参照 UTC 秒",u8"FX を有効化",u8"FX 価格（USD/USDT）",u8"FX 参照 UTC 秒",u8"有効期間（秒）",u8"最大 FX 参照時刻差（秒）"}}},
        {"ko",false,ReviewStatus::MachineAssisted,{{u8"표시 활성화",u8"PRL 가격 (USDT/PRL)",u8"PRL 기준 UTC 초",u8"FX 활성화",u8"FX 가격 (USD/USDT)",u8"FX 기준 UTC 초",u8"유효 기간 초",u8"최대 FX 기준 시간차 초"}}},
        {"ar",true,ReviewStatus::MachineAssisted,{{u8"تفعيل العرض",u8"سعر PRL (USDT/PRL)",u8"وقت مرجع PRL بثواني UTC",u8"تفعيل FX",u8"سعر FX (USD/USDT)",u8"وقت مرجع FX بثواني UTC",u8"مدة الصلاحية بالثواني",u8"أقصى فرق وقت FX بالثواني"}}},
        {"hi",false,ReviewStatus::MachineAssisted,{{u8"प्रदर्शन सक्षम",u8"PRL मूल्य (USDT/PRL)",u8"PRL संदर्भ UTC सेकंड",u8"FX सक्षम",u8"FX मूल्य (USD/USDT)",u8"FX संदर्भ UTC सेकंड",u8"वैधता सेकंड",u8"अधिकतम FX संदर्भ समय अंतर सेकंड"}}},
        {"bn",false,ReviewStatus::MachineAssisted,{{u8"প্রদর্শন সক্রিয়",u8"PRL মূল্য (USDT/PRL)",u8"PRL রেফারেন্স UTC সেকেন্ড",u8"FX সক্রিয়",u8"FX মূল্য (USD/USDT)",u8"FX রেফারেন্স UTC সেকেন্ড",u8"বৈধতার সেকেন্ড",u8"সর্বোচ্চ FX রেফারেন্স সময়ের ব্যবধান সেকেন্ড"}}},
        {"id",false,ReviewStatus::MachineAssisted,{{"Tampilan aktif","Harga PRL (USDT/PRL)","Referensi PRL dalam detik UTC","FX aktif","Harga FX (USD/USDT)","Referensi FX dalam detik UTC","Masa berlaku dalam detik","Selisih waktu FX maksimum dalam detik"}}},
        {"tr",false,ReviewStatus::MachineAssisted,{{u8"Görüntüleme etkin",u8"PRL fiyatı (USDT/PRL)",u8"PRL referansı UTC saniye","FX etkin",u8"FX fiyatı (USD/USDT)",u8"FX referansı UTC saniye",u8"Geçerlilik saniyesi",u8"En yüksek FX referans zaman farkı saniyesi"}}},
        {"vi",false,ReviewStatus::MachineAssisted,{{u8"Bật hiển thị",u8"Giá PRL (USDT/PRL)",u8"Tham chiếu PRL theo giây UTC",u8"Bật FX",u8"Giá FX (USD/USDT)",u8"Tham chiếu FX theo giây UTC",u8"Thời hạn theo giây",u8"Chênh lệch thời gian FX tối đa theo giây"}}}
    }};return packs;
}
inline const std::array<ManualMarketFieldMessageKey,8> &manualMarketFieldKeys(){
    static constexpr std::array<ManualMarketFieldMessageKey,8> keys={{ManualMarketFieldMessageKey::DisplayEnabled,ManualMarketFieldMessageKey::PrlPrice,ManualMarketFieldMessageKey::PrlReference,ManualMarketFieldMessageKey::FxEnabled,ManualMarketFieldMessageKey::FxPrice,ManualMarketFieldMessageKey::FxReference,ManualMarketFieldMessageKey::Validity,ManualMarketFieldMessageKey::MaximumFxSkew}};return keys;
}
inline std::string_view manualMarketFieldMessageKeyName(ManualMarketFieldMessageKey key){
    static constexpr std::array<std::string_view,manualMarketFieldMessageKeyCount> names={{"manual_market.field.display_enabled","manual_market.field.prl_price","manual_market.field.prl_reference","manual_market.field.fx_enabled","manual_market.field.fx_price","manual_market.field.fx_reference","manual_market.field.validity","manual_market.field.maximum_fx_skew"}};
    const auto index=static_cast<std::size_t>(key);return index<names.size()?names[index]:std::string_view{};
}
inline LocalizedMessage manualMarketFieldMessage(std::string_view language,ManualMarketFieldMessageKey key){
    const auto index=static_cast<std::size_t>(key);if(index>=manualMarketFieldMessageKeyCount)return {};
    const auto &source=manualMarketFieldLocalePacks()[0];const auto *selected=&source;for(const auto &pack:manualMarketFieldLocalePacks())if(pack.id==language){selected=&pack;break;}
    if(selected->id!=language||!validTemplateTranslation(source.messages[index],selected->messages[index]))return {source.messages[index],source.id,source.review,true,false};
    return {selected->messages[index],selected->id,selected->review,false,selected->rtl};
}
inline std::string manualMarketFieldEnglish(ManualMarketFieldMessageKey key){return std::string(manualMarketFieldMessage("en",key).text);}
} } // namespace om::i18n
