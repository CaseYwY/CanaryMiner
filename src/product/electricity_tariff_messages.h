// Complete frozen-current-tariff display rows, including legacy blank developer fields; not tariff/source admission.
#pragma once
#include "localization.h"
namespace om { namespace i18n {
enum class ElectricityTariffMessageKey { CurrentNone, CurrentVersion, CurrentCurrency, CurrentCurrencyVersion, CurrentRate, CurrentRateVersion, CurrentRateCurrency, CurrentAll, Off, Count };
constexpr std::size_t electricityTariffMessageKeyCount=static_cast<std::size_t>(ElectricityTariffMessageKey::Count);
struct ElectricityTariffLocalePack {std::string_view id;bool rtl;ReviewStatus review;std::array<std::string_view,electricityTariffMessageKeyCount> messages;};
inline const std::array<ElectricityTariffLocalePack,16> &electricityTariffLocalePacks(){
    static const std::array<ElectricityTariffLocalePack,16> packs={{
        {"en",false,ReviewStatus::Source,{{"Current tariff:  /kWh | ","Current tariff:  /kWh | {version}","Current tariff:  {currency}/kWh | ","Current tariff:  {currency}/kWh | {version}","Current tariff: {rate} /kWh | ","Current tariff: {rate} /kWh | {version}","Current tariff: {rate} {currency}/kWh | ","Current tariff: {rate} {currency}/kWh | {version}","Current tariff: off/no active collection"}}},
        {"zh-Hans",false,ReviewStatus::MachineAssisted,{{u8"当前费率：  /kWh | ",u8"当前费率：  /kWh | {version}",u8"当前费率：  {currency}/kWh | ",u8"当前费率：  {currency}/kWh | {version}",u8"当前费率：{rate} /kWh | ",u8"当前费率：{rate} /kWh | {version}",u8"当前费率：{rate} {currency}/kWh | ",u8"当前费率：{rate} {currency}/kWh | {version}",u8"当前费率：关闭／无活动采集"}}},
        {"zh-Hant",false,ReviewStatus::MachineAssisted,{{u8"目前費率：  /kWh | ",u8"目前費率：  /kWh | {version}",u8"目前費率：  {currency}/kWh | ",u8"目前費率：  {currency}/kWh | {version}",u8"目前費率：{rate} /kWh | ",u8"目前費率：{rate} /kWh | {version}",u8"目前費率：{rate} {currency}/kWh | ",u8"目前費率：{rate} {currency}/kWh | {version}",u8"目前費率：關閉／無活動收集"}}},
        {"es",false,ReviewStatus::MachineAssisted,{{"Tarifa actual:  /kWh | ","Tarifa actual:  /kWh | {version}","Tarifa actual:  {currency}/kWh | ","Tarifa actual:  {currency}/kWh | {version}","Tarifa actual: {rate} /kWh | ","Tarifa actual: {rate} /kWh | {version}","Tarifa actual: {rate} {currency}/kWh | ","Tarifa actual: {rate} {currency}/kWh | {version}",u8"Tarifa actual: desactivada/sin recopilación activa"}}},
        {"pt-BR",false,ReviewStatus::MachineAssisted,{{"Tarifa atual:  /kWh | ","Tarifa atual:  /kWh | {version}","Tarifa atual:  {currency}/kWh | ","Tarifa atual:  {currency}/kWh | {version}","Tarifa atual: {rate} /kWh | ","Tarifa atual: {rate} /kWh | {version}","Tarifa atual: {rate} {currency}/kWh | ","Tarifa atual: {rate} {currency}/kWh | {version}","Tarifa atual: desativada/sem coleta ativa"}}},
        {"fr",false,ReviewStatus::MachineAssisted,{{"Tarif actuel :  /kWh | ","Tarif actuel :  /kWh | {version}","Tarif actuel :  {currency}/kWh | ","Tarif actuel :  {currency}/kWh | {version}","Tarif actuel : {rate} /kWh | ","Tarif actuel : {rate} /kWh | {version}","Tarif actuel : {rate} {currency}/kWh | ","Tarif actuel : {rate} {currency}/kWh | {version}",u8"Tarif actuel : désactivé/aucune collecte active"}}},
        {"de",false,ReviewStatus::MachineAssisted,{{"Aktueller Tarif:  /kWh | ","Aktueller Tarif:  /kWh | {version}","Aktueller Tarif:  {currency}/kWh | ","Aktueller Tarif:  {currency}/kWh | {version}","Aktueller Tarif: {rate} /kWh | ","Aktueller Tarif: {rate} /kWh | {version}","Aktueller Tarif: {rate} {currency}/kWh | ","Aktueller Tarif: {rate} {currency}/kWh | {version}","Aktueller Tarif: aus/keine aktive Erfassung"}}},
        {"ru",false,ReviewStatus::MachineAssisted,{{u8"Текущий тариф:  /kWh | ",u8"Текущий тариф:  /kWh | {version}",u8"Текущий тариф:  {currency}/kWh | ",u8"Текущий тариф:  {currency}/kWh | {version}",u8"Текущий тариф: {rate} /kWh | ",u8"Текущий тариф: {rate} /kWh | {version}",u8"Текущий тариф: {rate} {currency}/kWh | ",u8"Текущий тариф: {rate} {currency}/kWh | {version}",u8"Текущий тариф: выкл/нет активного сбора"}}},
        {"ja",false,ReviewStatus::MachineAssisted,{{u8"現在の料金：  /kWh | ",u8"現在の料金：  /kWh | {version}",u8"現在の料金：  {currency}/kWh | ",u8"現在の料金：  {currency}/kWh | {version}",u8"現在の料金：{rate} /kWh | ",u8"現在の料金：{rate} /kWh | {version}",u8"現在の料金：{rate} {currency}/kWh | ",u8"現在の料金：{rate} {currency}/kWh | {version}",u8"現在の料金：無効／実行中の収集なし"}}},
        {"ko",false,ReviewStatus::MachineAssisted,{{u8"현재 요율:  /kWh | ",u8"현재 요율:  /kWh | {version}",u8"현재 요율:  {currency}/kWh | ",u8"현재 요율:  {currency}/kWh | {version}",u8"현재 요율: {rate} /kWh | ",u8"현재 요율: {rate} /kWh | {version}",u8"현재 요율: {rate} {currency}/kWh | ",u8"현재 요율: {rate} {currency}/kWh | {version}",u8"현재 요율: 꺼짐／활성 수집 없음"}}},
        {"ar",true,ReviewStatus::MachineAssisted,{{u8"السعر الحالي:  /kWh | ",u8"السعر الحالي:  /kWh | {version}",u8"السعر الحالي:  {currency}/kWh | ",u8"السعر الحالي:  {currency}/kWh | {version}",u8"السعر الحالي: {rate} /kWh | ",u8"السعر الحالي: {rate} /kWh | {version}",u8"السعر الحالي: {rate} {currency}/kWh | ",u8"السعر الحالي: {rate} {currency}/kWh | {version}",u8"السعر الحالي: إيقاف/لا جمع نشط"}}},
        {"hi",false,ReviewStatus::MachineAssisted,{{u8"वर्तमान दर:  /kWh | ",u8"वर्तमान दर:  /kWh | {version}",u8"वर्तमान दर:  {currency}/kWh | ",u8"वर्तमान दर:  {currency}/kWh | {version}",u8"वर्तमान दर: {rate} /kWh | ",u8"वर्तमान दर: {rate} /kWh | {version}",u8"वर्तमान दर: {rate} {currency}/kWh | ",u8"वर्तमान दर: {rate} {currency}/kWh | {version}",u8"वर्तमान दर: बंद/सक्रिय संग्रह नहीं"}}},
        {"bn",false,ReviewStatus::MachineAssisted,{{u8"বর্তমান দর:  /kWh | ",u8"বর্তমান দর:  /kWh | {version}",u8"বর্তমান দর:  {currency}/kWh | ",u8"বর্তমান দর:  {currency}/kWh | {version}",u8"বর্তমান দর: {rate} /kWh | ",u8"বর্তমান দর: {rate} /kWh | {version}",u8"বর্তমান দর: {rate} {currency}/kWh | ",u8"বর্তমান দর: {rate} {currency}/kWh | {version}",u8"বর্তমান দর: বন্ধ/সক্রিয় সংগ্রহ নেই"}}},
        {"id",false,ReviewStatus::MachineAssisted,{{"Tarif saat ini:  /kWh | ","Tarif saat ini:  /kWh | {version}","Tarif saat ini:  {currency}/kWh | ","Tarif saat ini:  {currency}/kWh | {version}","Tarif saat ini: {rate} /kWh | ","Tarif saat ini: {rate} /kWh | {version}","Tarif saat ini: {rate} {currency}/kWh | ","Tarif saat ini: {rate} {currency}/kWh | {version}","Tarif saat ini: nonaktif/tidak ada pengumpulan aktif"}}},
        {"tr",false,ReviewStatus::MachineAssisted,{{u8"Geçerli tarife:  /kWh | ",u8"Geçerli tarife:  /kWh | {version}",u8"Geçerli tarife:  {currency}/kWh | ",u8"Geçerli tarife:  {currency}/kWh | {version}",u8"Geçerli tarife: {rate} /kWh | ",u8"Geçerli tarife: {rate} /kWh | {version}",u8"Geçerli tarife: {rate} {currency}/kWh | ",u8"Geçerli tarife: {rate} {currency}/kWh | {version}",u8"Geçerli tarife: kapalı/etkin toplama yok"}}},
        {"vi",false,ReviewStatus::MachineAssisted,{{u8"Đơn giá hiện tại:  /kWh | ",u8"Đơn giá hiện tại:  /kWh | {version}",u8"Đơn giá hiện tại:  {currency}/kWh | ",u8"Đơn giá hiện tại:  {currency}/kWh | {version}",u8"Đơn giá hiện tại: {rate} /kWh | ",u8"Đơn giá hiện tại: {rate} /kWh | {version}",u8"Đơn giá hiện tại: {rate} {currency}/kWh | ",u8"Đơn giá hiện tại: {rate} {currency}/kWh | {version}",u8"Đơn giá hiện tại: tắt/không có thu thập đang chạy"}}}
    }};return packs;
}
inline ElectricityTariffMessageKey electricityTariffPresenceKey(bool rate,bool currency,bool version){return static_cast<ElectricityTariffMessageKey>((rate?4u:0u)+(currency?2u:0u)+(version?1u:0u));}
inline std::string_view electricityTariffMessageKeyName(ElectricityTariffMessageKey key){
    static constexpr std::array<std::string_view,electricityTariffMessageKeyCount> names={{"electricity.tariff.current_none","electricity.tariff.current_version","electricity.tariff.current_currency","electricity.tariff.current_currency_version","electricity.tariff.current_rate","electricity.tariff.current_rate_version","electricity.tariff.current_rate_currency","electricity.tariff.current_all","electricity.tariff.off"}};
    const auto index=static_cast<std::size_t>(key);return index<names.size()?names[index]:std::string_view{};
}
inline LocalizedMessage electricityTariffMessage(std::string_view language,ElectricityTariffMessageKey key){
    const auto index=static_cast<std::size_t>(key);if(index>=electricityTariffMessageKeyCount)return {};
    const auto &source=electricityTariffLocalePacks()[0];const auto *selected=&source;for(const auto &pack:electricityTariffLocalePacks())if(pack.id==language){selected=&pack;break;}
    if(selected->id!=language||!validTemplateTranslation(source.messages[index],selected->messages[index]))return {source.messages[index],source.id,source.review,true,false};
    return {selected->messages[index],selected->id,selected->review,false,selected->rtl};
}
inline std::string electricityTariffEnglish(ElectricityTariffMessageKey key,const MessageArguments &arguments={}){
    if(arguments.size()>3)return {};
    const auto text=electricityTariffMessage("en",key).text;std::vector<std::string_view> expected,supplied;
    if(text.empty()||!templateParameters(text,&expected))return {};
    for(const auto &arg:arguments){if(arg.first.size()>32||arg.second.empty()||arg.second.size()>64||!std::all_of(arg.second.begin(),arg.second.end(),[](unsigned char c){return c>=32&&c<=126;}))return {};supplied.push_back(arg.first);}
    std::sort(supplied.begin(),supplied.end());if(supplied!=expected)return {};
    std::string result;std::size_t at=0;
    while(at<text.size()){const auto start=text.find('{',at);if(start==text.npos){result.append(text.substr(at));break;}result.append(text.substr(at,start-at));const auto end=text.find('}',start);
        const auto name=text.substr(start+1,end-start-1);const auto found=std::find_if(arguments.begin(),arguments.end(),[&](const auto &arg){return arg.first==name;});if(found==arguments.end())return {};
        result+=found->second;if(result.size()>4096)return {};at=end+1;
    }
    return result.size()<=4096?result:std::string{};
}
} } // namespace om::i18n
