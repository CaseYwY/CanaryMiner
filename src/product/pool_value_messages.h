// Pool display envelopes only; opaque invariant requests remain separate from human scope/action descriptions.
#pragma once
#include "localization.h"
namespace om { namespace i18n {
enum class PoolValueMessageKey { FieldValue, HumanFieldValue, UserPool, Donation, Unset, ConsentEnabled, ConsentDisabled, AddAction, RemoveAction, EditInput, Count };
constexpr std::size_t poolValueMessageKeyCount=static_cast<std::size_t>(PoolValueMessageKey::Count);
struct PoolValueLocalePack {std::string_view id;bool rtl;ReviewStatus review;std::array<std::string_view,poolValueMessageKeyCount> messages;};
inline const std::array<PoolValueLocalePack,16> &poolValueLocalePacks(){
    static const std::array<PoolValueLocalePack,16> packs={{
        {"en",false,ReviewStatus::Source,{{"{field}: {value}","{field}: {value}","User pool {slot}","Donation (independent)","unset","YES (Enter revokes)","off (explicit phrase required)","[Enter] strict TLS, no consent","[Enter] explicit confirmation","> {input}_"}}},
        {"zh-Hans",false,ReviewStatus::MachineAssisted,{{u8"{field}：{value}",u8"{field}：{value}",u8"用户矿池 {slot}",u8"捐赠（独立）",u8"未设置",u8"YES（Enter 撤销）",u8"off（需要明确短语）",u8"[Enter] 严格 TLS，无许可",u8"[Enter] 明确确认","> {input}_"}}},
        {"zh-Hant",false,ReviewStatus::MachineAssisted,{{u8"{field}：{value}",u8"{field}：{value}",u8"使用者礦池 {slot}",u8"捐贈（獨立）",u8"未設定",u8"YES（Enter 撤銷）",u8"off（需要明確短語）",u8"[Enter] 嚴格 TLS，無許可",u8"[Enter] 明確確認","> {input}_"}}},
        {"es",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {value}","Pool de usuario {slot}",u8"Donación (independiente)","sin configurar","YES (Enter revoca)",u8"off (se requiere frase explícita)","[Enter] TLS estricto, sin consentimiento",u8"[Enter] confirmación explícita","> {input}_"}}},
        {"pt-BR",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {value}",u8"Pool de usuário {slot}",u8"Doação (independente)",u8"não definido","YES (Enter revoga)",u8"off (frase explícita necessária)","[Enter] TLS estrito, sem consentimento",u8"[Enter] confirmação explícita","> {input}_"}}},
        {"fr",false,ReviewStatus::MachineAssisted,{{"{field} : {value}","{field} : {value}","Pool utilisateur {slot}",u8"Don (indépendant)",u8"non défini",u8"YES (Enter révoque)","off (phrase explicite requise)","[Enter] TLS strict, sans consentement","[Enter] confirmation explicite","> {input}_"}}},
        {"de",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {value}","Benutzerpool {slot}",u8"Spende (unabhängig)","nicht gesetzt","YES (Enter widerruft)",u8"off (ausdrückliche Phrase erforderlich)","[Enter] striktes TLS, keine Zustimmung",u8"[Enter] ausdrückliche Bestätigung","> {input}_"}}},
        {"ru",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {value}",u8"Пул пользователя {slot}",u8"Пожертвование (независимое)",u8"не задано",u8"YES (Enter отменяет)",u8"off (нужна явная фраза)",u8"[Enter] строгий TLS, без согласия",u8"[Enter] явное подтверждение","> {input}_"}}},
        {"ja",false,ReviewStatus::MachineAssisted,{{u8"{field}：{value}",u8"{field}：{value}",u8"ユーザープール {slot}",u8"寄付（独立）",u8"未設定",u8"YES（Enter で取消）",u8"off（明示的な語句が必要）",u8"[Enter] 厳格 TLS、同意なし",u8"[Enter] 明示的な確認","> {input}_"}}},
        {"ko",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {value}",u8"사용자 풀 {slot}",u8"기부 (독립)",u8"미설정",u8"YES (Enter로 철회)",u8"off (명시적 문구 필요)",u8"[Enter] 엄격 TLS, 동의 없음",u8"[Enter] 명시적 확인","> {input}_"}}},
        {"ar",true,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {value}",u8"مجمع المستخدم {slot}",u8"التبرع (مستقل)",u8"غير معين",u8"YES (يلغي Enter)",u8"off (تلزم عبارة صريحة)",u8"[Enter] TLS صارم، بلا موافقة",u8"[Enter] تأكيد صريح","> {input}_"}}},
        {"hi",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {value}",u8"उपयोगकर्ता पूल {slot}",u8"दान (स्वतंत्र)",u8"सेट नहीं",u8"YES (Enter रद्द करता है)",u8"off (स्पष्ट वाक्यांश आवश्यक)",u8"[Enter] सख्त TLS, सहमति नहीं",u8"[Enter] स्पष्ट पुष्टि","> {input}_"}}},
        {"bn",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {value}",u8"ব্যবহারকারীর পুল {slot}",u8"দান (স্বাধীন)",u8"নির্ধারিত নয়",u8"YES (Enter বাতিল করে)",u8"off (স্পষ্ট বাক্যাংশ চাই)",u8"[Enter] কঠোর TLS, সম্মতি নেই",u8"[Enter] স্পষ্ট নিশ্চিতকরণ","> {input}_"}}},
        {"id",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {value}","Pool pengguna {slot}","Donasi (independen)","belum diatur","YES (Enter mencabut)","off (perlu frasa eksplisit)","[Enter] TLS ketat, tanpa persetujuan","[Enter] konfirmasi eksplisit","> {input}_"}}},
        {"tr",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {value}",u8"Kullanıcı havuzu {slot}",u8"Bağış (bağımsız)",u8"ayarlanmadı","YES (Enter iptal eder)",u8"off (açık ifade gerekli)",u8"[Enter] katı TLS, onay yok",u8"[Enter] açık onay","> {input}_"}}},
        {"vi",false,ReviewStatus::MachineAssisted,{{"{field}: {value}","{field}: {value}",u8"Pool người dùng {slot}",u8"Đóng góp (độc lập)",u8"chưa đặt",u8"YES (Enter thu hồi)",u8"off (cần cụm từ rõ ràng)",u8"[Enter] TLS nghiêm ngặt, chưa đồng ý",u8"[Enter] xác nhận rõ ràng","> {input}_"}}}
    }};return packs;
}
inline std::string_view poolValueMessageKeyName(PoolValueMessageKey key){
    static constexpr std::array<std::string_view,poolValueMessageKeyCount> names={{"pool.value.field_request","pool.value.field_human","pool.value.user_pool","pool.value.donation","pool.value.unset","pool.value.consent_enabled","pool.value.consent_disabled","pool.value.add_action","pool.value.remove_action","pool.value.edit_input"}};
    const auto index=static_cast<std::size_t>(key);return index<names.size()?names[index]:std::string_view{};
}
inline LocalizedMessage poolValueMessage(std::string_view language,PoolValueMessageKey key){
    const auto index=static_cast<std::size_t>(key);if(index>=poolValueMessageKeyCount)return {};
    const auto &source=poolValueLocalePacks()[0];const auto *selected=&source;for(const auto &pack:poolValueLocalePacks())if(pack.id==language){selected=&pack;break;}
    if(selected->id!=language||!validTemplateTranslation(source.messages[index],selected->messages[index]))return {source.messages[index],source.id,source.review,true,false};
    return {selected->messages[index],selected->id,selected->review,false,selected->rtl};
}
inline std::string poolValueEnglish(PoolValueMessageKey key,const MessageArguments &arguments={}){
    if(arguments.size()>2)return {};
    const auto text=poolValueMessage("en",key).text;std::vector<std::string_view> expected,supplied;
    if(text.empty()||!templateParameters(text,&expected))return {};
    for(const auto &arg:arguments){if(arg.first.size()>32||arg.second.size()>300||!std::all_of(arg.second.begin(),arg.second.end(),[](unsigned char c){return c>=32&&c<=126;}))return {};supplied.push_back(arg.first);}
    std::sort(supplied.begin(),supplied.end());if(supplied!=expected)return {};
    std::string result;std::size_t at=0;
    while(at<text.size()){const auto start=text.find('{',at);if(start==text.npos){result.append(text.substr(at));break;}result.append(text.substr(at,start-at));const auto end=text.find('}',start);
        const auto name=text.substr(start+1,end-start-1);const auto found=std::find_if(arguments.begin(),arguments.end(),[&](const auto &arg){return arg.first==name;});if(found==arguments.end())return {};
        result+=found->second;if(result.size()>4096)return {};at=end+1;
    }
    return result.size()<=4096?result:std::string{};
}
} } // namespace om::i18n
