// Language-panel labels/review descriptions only. Machine IDs, policy names and effective state stay independent.
#pragma once
#include "localization.h"
namespace om { namespace i18n {
enum class LanguageFieldMessageKey { LanguageId, FormatLocale, DigitStyle, TextOrder, Direction, AmbiguousCells, EmojiCells, Grouping,
    Invalid, SourceReview, DraftReview, Count };
constexpr std::size_t languageFieldMessageKeyCount=static_cast<std::size_t>(LanguageFieldMessageKey::Count);
struct LanguageFieldLocalePack {std::string_view id;bool rtl;ReviewStatus review;std::array<std::string_view,languageFieldMessageKeyCount> messages;};
inline const std::array<LanguageFieldLocalePack,16> &languageFieldLocalePacks(){
    static const std::array<LanguageFieldLocalePack,16> packs={{
        {"en",false,ReviewStatus::Source,{{"Language ID","Format locale","Digit style","Text order","Direction","Ambiguous cells","Emoji cells","Grouping","Invalid","English source (not externally certified)","machine-assisted draft (not human reviewed)"}}},
        {"zh-Hans",false,ReviewStatus::MachineAssisted,{{u8"语言 ID",u8"格式区域",u8"数字样式",u8"文本顺序",u8"方向",u8"模糊宽度字符列数",u8"表情列数",u8"分组",u8"无效",u8"英文源文（未经外部认证）",u8"机器辅助草稿（未经人工审阅）"}}},
        {"zh-Hant",false,ReviewStatus::MachineAssisted,{{u8"語言 ID",u8"格式地區",u8"數字樣式",u8"文字順序",u8"方向",u8"模糊寬度字元欄數",u8"表情欄數",u8"分組",u8"無效",u8"英文原文（未經外部認證）",u8"機器輔助草稿（未經人工檢閱）"}}},
        {"es",false,ReviewStatus::MachineAssisted,{{"ID de idioma",u8"Configuración regional de formato",u8"Estilo de dígitos","Orden del texto",u8"Dirección","Columnas ambiguas","Columnas de emoji",u8"Agrupación",u8"No válido",u8"Fuente en inglés (sin certificación externa)",u8"borrador asistido por máquina (sin revisión humana)"}}},
        {"pt-BR",false,ReviewStatus::MachineAssisted,{{"ID do idioma",u8"Localidade de formatação",u8"Estilo de dígitos","Ordem do texto",u8"Direção",u8"Colunas ambíguas","Colunas de emoji","Agrupamento",u8"Inválido",u8"Fonte em inglês (sem certificação externa)",u8"rascunho assistido por máquina (sem revisão humana)"}}},
        {"fr",false,ReviewStatus::MachineAssisted,{{"ID de langue",u8"Paramètres régionaux de format","Style des chiffres","Ordre du texte","Direction",u8"Colonnes ambiguës","Colonnes des emoji","Groupement","Invalide",u8"Source anglaise (sans certification externe)",u8"brouillon assisté par machine (sans révision humaine)"}}},
        {"de",false,ReviewStatus::MachineAssisted,{{"Sprach-ID",u8"Formatgebietsschema","Ziffernstil","Textreihenfolge","Richtung",u8"Mehrdeutige Zeichenbreite","Emoji-Zeichenbreite","Gruppierung",u8"Ungültig",u8"Englische Quelle (nicht extern zertifiziert)",u8"maschinenunterstützter Entwurf (nicht menschlich geprüft)"}}},
        {"ru",false,ReviewStatus::MachineAssisted,{{u8"ID языка",u8"Локаль формата",u8"Стиль цифр",u8"Порядок текста",u8"Направление",u8"Неоднозначная ширина",u8"Ширина эмодзи",u8"Группировка",u8"Недопустимо",u8"Английский оригинал (без внешней сертификации)",u8"Машинный черновик (не проверен человеком)"}}},
        {"ja",false,ReviewStatus::MachineAssisted,{{u8"言語 ID",u8"書式ロケール",u8"数字スタイル",u8"テキスト順序",u8"方向",u8"曖昧幅文字の列数",u8"絵文字の列数",u8"桁区切り",u8"無効",u8"英語原文（外部認証なし）",u8"機械支援の下書き（人による確認なし）"}}},
        {"ko",false,ReviewStatus::MachineAssisted,{{u8"언어 ID",u8"형식 로캘",u8"숫자 스타일",u8"텍스트 순서",u8"방향",u8"모호한 문자 열 수",u8"이모지 열 수",u8"그룹 구분",u8"잘못됨",u8"영어 원문 (외부 인증 없음)",u8"기계 보조 초안 (사람 검토 없음)"}}},
        {"ar",true,ReviewStatus::MachineAssisted,{{u8"معرف اللغة",u8"منطقة التنسيق",u8"نمط الأرقام",u8"ترتيب النص",u8"الاتجاه",u8"أعمدة العرض الملتبس",u8"أعمدة الرموز التعبيرية",u8"التجميع",u8"غير صالح",u8"المصدر الإنجليزي (غير معتمد خارجيا)",u8"مسودة بمساعدة الآلة (دون مراجعة بشرية)"}}},
        {"hi",false,ReviewStatus::MachineAssisted,{{u8"भाषा ID",u8"प्रारूप लोकेल",u8"अंक शैली",u8"पाठ क्रम",u8"दिशा",u8"अस्पष्ट चौड़ाई स्तंभ",u8"इमोजी स्तंभ",u8"समूहीकरण",u8"अमान्य",u8"अंग्रेज़ी स्रोत (बाहरी प्रमाणन नहीं)",u8"मशीन-सहायता मसौदा (मानव समीक्षा नहीं)"}}},
        {"bn",false,ReviewStatus::MachineAssisted,{{u8"ভাষা ID",u8"বিন্যাস লোকেল",u8"অঙ্কের ধরন",u8"পাঠের ক্রম",u8"দিক",u8"দ্ব্যর্থক প্রস্থের কলাম",u8"ইমোজি কলাম",u8"গ্রুপিং",u8"অবৈধ",u8"ইংরেজি উৎস (বাহ্যিক প্রত্যয়ন নেই)",u8"যন্ত্র-সহায়ক খসড়া (মানব পর্যালোচনা নেই)"}}},
        {"id",false,ReviewStatus::MachineAssisted,{{"ID bahasa","Lokal format","Gaya digit","Urutan teks","Arah","Kolom ambigu","Kolom emoji","Pengelompokan","Tidak valid","Sumber Inggris (tanpa sertifikasi eksternal)","draf berbantuan mesin (belum ditinjau manusia)"}}},
        {"tr",false,ReviewStatus::MachineAssisted,{{u8"Dil kimliği",u8"Biçim yereli",u8"Rakam stili",u8"Metin sırası",u8"Yön",u8"Belirsiz genişlik sütunları",u8"Emoji sütunları",u8"Gruplama",u8"Geçersiz",u8"İngilizce kaynak (haricen onaylanmadı)",u8"makine destekli taslak (insan incelemesi yok)"}}},
        {"vi",false,ReviewStatus::MachineAssisted,{{u8"ID ngôn ngữ",u8"Vùng định dạng",u8"Kiểu chữ số",u8"Thứ tự văn bản",u8"Hướng",u8"Số cột ký tự độ rộng mơ hồ",u8"Số cột emoji",u8"Phân nhóm",u8"Không hợp lệ",u8"Nguồn tiếng Anh (chưa chứng nhận bên ngoài)",u8"bản nháp có máy hỗ trợ (chưa được người xét duyệt)"}}}
    }};return packs;
}
inline const std::array<LanguageFieldMessageKey,8> &languageFieldKeys(){
    static constexpr std::array<LanguageFieldMessageKey,8> keys={{LanguageFieldMessageKey::LanguageId,LanguageFieldMessageKey::FormatLocale,LanguageFieldMessageKey::DigitStyle,LanguageFieldMessageKey::TextOrder,LanguageFieldMessageKey::Direction,LanguageFieldMessageKey::AmbiguousCells,LanguageFieldMessageKey::EmojiCells,LanguageFieldMessageKey::Grouping}};return keys;
}
inline std::string_view languageFieldMessageKeyName(LanguageFieldMessageKey key){
    static constexpr std::array<std::string_view,languageFieldMessageKeyCount> names={{"language.field.language_id","language.field.format_locale","language.field.digit_style","language.field.text_order","language.field.direction","language.field.ambiguous_cells","language.field.emoji_cells","language.field.grouping","language.value.invalid_label","language.review.source","language.review.draft"}};
    const auto index=static_cast<std::size_t>(key);return index<names.size()?names[index]:std::string_view{};
}
inline LocalizedMessage languageFieldMessage(std::string_view language,LanguageFieldMessageKey key){
    const auto index=static_cast<std::size_t>(key);if(index>=languageFieldMessageKeyCount)return {};
    const auto &source=languageFieldLocalePacks()[0];const auto *selected=&source;for(const auto &pack:languageFieldLocalePacks())if(pack.id==language){selected=&pack;break;}
    if(selected->id!=language||!validTemplateTranslation(source.messages[index],selected->messages[index]))return {source.messages[index],source.id,source.review,true,false};
    return {selected->messages[index],selected->id,selected->review,false,selected->rtl};
}
inline std::string languageFieldEnglish(LanguageFieldMessageKey key){return std::string(languageFieldMessage("en",key).text);}
} } // namespace om::i18n
