// Complete root-review changes only. Values are opaque presentation, never saved-setting authority.
#pragma once
#include "localization.h"
namespace om { namespace i18n {
enum class SettingsChangeMessageKey { Refresh, Color, Events, HideWorker, DetailsPage, LogPage, DetailsIdentity, DetailsPool, DetailsRestarts,
    DetailsErrors, UnclassifiedLogs, LogTimestamps, IsoUtcTime, LogFollow, LogLevel, LogRecords, Count };
constexpr std::size_t settingsChangeMessageKeyCount=static_cast<std::size_t>(SettingsChangeMessageKey::Count);
struct SettingsChangeLocalePack {std::string_view id;bool rtl;ReviewStatus review;std::array<std::string_view,settingsChangeMessageKeyCount> messages;};
inline const std::array<SettingsChangeLocalePack,16> &settingsChangeLocalePacks(){
    static const std::array<SettingsChangeLocalePack,16> packs={{
        {"en",false,ReviewStatus::Source,{{"Display refresh: {before} -> {after}","Terminal color: {before} -> {after}","Recent events: {before} -> {after}","Hide worker label: {before} -> {after}",
            "Details page: {before} -> {after}","Log page: {before} -> {after}","Details identity: {before} -> {after}","Details pool: {before} -> {after}","Details restarts: {before} -> {after}","Details errors: {before} -> {after}",
            "Unclassified logs: {before} -> {after}","Log timestamps: {before} -> {after}","ISO UTC time: {before} -> {after}","Log follow: {before} -> {after}","Log level: {before} -> {after}","Log records: {before} -> {after}"}}},
        {"zh-Hans",false,ReviewStatus::MachineAssisted,{{u8"显示刷新：{before} -> {after}",u8"终端颜色：{before} -> {after}",u8"近期事件：{before} -> {after}",u8"隐藏 worker 标签：{before} -> {after}",
            u8"详情页：{before} -> {after}",u8"日志页：{before} -> {after}",u8"详情设备标识：{before} -> {after}",u8"详情矿池状态：{before} -> {after}",u8"详情重启次数：{before} -> {after}",u8"详情错误：{before} -> {after}",
            u8"未分类日志：{before} -> {after}",u8"日志时间戳：{before} -> {after}",u8"ISO UTC 时间：{before} -> {after}",u8"日志跟随：{before} -> {after}",u8"日志级别：{before} -> {after}",u8"日志记录数：{before} -> {after}"}}},
        {"zh-Hant",false,ReviewStatus::MachineAssisted,{{u8"顯示更新：{before} -> {after}",u8"終端機色彩：{before} -> {after}",u8"近期事件：{before} -> {after}",u8"隱藏 worker 標籤：{before} -> {after}",
            u8"詳細資料頁：{before} -> {after}",u8"日誌頁：{before} -> {after}",u8"詳細裝置識別：{before} -> {after}",u8"詳細礦池狀態：{before} -> {after}",u8"詳細重新啟動次數：{before} -> {after}",u8"詳細錯誤：{before} -> {after}",
            u8"未分類日誌：{before} -> {after}",u8"日誌時間戳記：{before} -> {after}",u8"ISO UTC 時間：{before} -> {after}",u8"跟隨日誌：{before} -> {after}",u8"日誌等級：{before} -> {after}",u8"日誌記錄數：{before} -> {after}"}}},
        {"es",false,ReviewStatus::MachineAssisted,{{u8"Actualización de pantalla: {before} -> {after}","Color del terminal: {before} -> {after}","Eventos recientes: {before} -> {after}","Ocultar etiqueta de worker: {before} -> {after}",
            u8"Página de detalles: {before} -> {after}",u8"Página de registro: {before} -> {after}","Identidad en detalles: {before} -> {after}","Pool en detalles: {before} -> {after}","Reinicios en detalles: {before} -> {after}","Errores en detalles: {before} -> {after}",
            "Registros sin clasificar: {before} -> {after}","Marcas de tiempo del registro: {before} -> {after}","Hora ISO UTC: {before} -> {after}","Seguimiento del registro: {before} -> {after}","Nivel del registro: {before} -> {after}","Cantidad de registros: {before} -> {after}"}}},
        {"pt-BR",false,ReviewStatus::MachineAssisted,{{u8"Atualização da tela: {before} -> {after}","Cor do terminal: {before} -> {after}","Eventos recentes: {before} -> {after}","Ocultar rótulo do worker: {before} -> {after}",
            u8"Página de detalhes: {before} -> {after}",u8"Página de log: {before} -> {after}","Identidade nos detalhes: {before} -> {after}","Pool nos detalhes: {before} -> {after}",u8"Reinícios nos detalhes: {before} -> {after}","Erros nos detalhes: {before} -> {after}",
            u8"Logs não classificados: {before} -> {after}","Carimbos de tempo do log: {before} -> {after}","Hora ISO UTC: {before} -> {after}","Acompanhamento do log: {before} -> {after}",u8"Nível do log: {before} -> {after}","Quantidade de registros: {before} -> {after}"}}},
        {"fr",false,ReviewStatus::MachineAssisted,{{u8"Rafraîchissement : {before} -> {after}","Couleur du terminal : {before} -> {after}",u8"Événements récents : {before} -> {after}","Masquer le libellé worker : {before} -> {after}",
            u8"Page de détails : {before} -> {after}","Page du journal : {before} -> {after}",u8"Identité dans les détails : {before} -> {after}",u8"Pool dans les détails : {before} -> {after}",u8"Redémarrages dans les détails : {before} -> {after}",u8"Erreurs dans les détails : {before} -> {after}",
            u8"Journaux non classés : {before} -> {after}","Horodatage du journal : {before} -> {after}","Heure ISO UTC : {before} -> {after}","Suivi du journal : {before} -> {after}","Gravité du journal : {before} -> {after}",u8"Nombre d'entrées : {before} -> {after}"}}},
        {"de",false,ReviewStatus::MachineAssisted,{{"Anzeigeaktualisierung: {before} -> {after}","Terminalfarbe: {before} -> {after}","Letzte Ereignisse: {before} -> {after}","Worker-Bezeichnung ausblenden: {before} -> {after}",
            "Detailseite: {before} -> {after}","Protokollseite: {before} -> {after}",u8"Identität in Details: {before} -> {after}","Pool in Details: {before} -> {after}","Neustarts in Details: {before} -> {after}","Fehler in Details: {before} -> {after}",
            "Unklassifizierte Protokolle: {before} -> {after}","Protokollzeitstempel: {before} -> {after}","ISO-UTC-Zeit: {before} -> {after}","Protokoll verfolgen: {before} -> {after}","Protokollstufe: {before} -> {after}","Protokolleintraege: {before} -> {after}"}}},
        {"ru",false,ReviewStatus::MachineAssisted,{{u8"Обновление экрана: {before} -> {after}",u8"Цвет терминала: {before} -> {after}",u8"Последние события: {before} -> {after}",u8"Скрыть метку worker: {before} -> {after}",
            u8"Страница подробностей: {before} -> {after}",u8"Страница журнала: {before} -> {after}",u8"Идентификатор в подробностях: {before} -> {after}",u8"Пул в подробностях: {before} -> {after}",u8"Перезапуски в подробностях: {before} -> {after}",u8"Ошибки в подробностях: {before} -> {after}",
            u8"Неклассифицированные журналы: {before} -> {after}",u8"Время записей журнала: {before} -> {after}",u8"Время ISO UTC: {before} -> {after}",u8"Слежение за журналом: {before} -> {after}",u8"Уровень журнала: {before} -> {after}",u8"Число записей: {before} -> {after}"}}},
        {"ja",false,ReviewStatus::MachineAssisted,{{u8"表示更新：{before} -> {after}",u8"端末の色：{before} -> {after}",u8"最近のイベント：{before} -> {after}",u8"worker ラベルを隠す：{before} -> {after}",
            u8"詳細ページ：{before} -> {after}",u8"ログページ：{before} -> {after}",u8"詳細のデバイス識別：{before} -> {after}",u8"詳細のプール：{before} -> {after}",u8"詳細の再起動回数：{before} -> {after}",u8"詳細のエラー：{before} -> {after}",
            u8"未分類ログ：{before} -> {after}",u8"ログのタイムスタンプ：{before} -> {after}",u8"ISO UTC 時刻：{before} -> {after}",u8"ログ追跡：{before} -> {after}",u8"ログの重要度：{before} -> {after}",u8"ログ件数：{before} -> {after}"}}},
        {"ko",false,ReviewStatus::MachineAssisted,{{u8"화면 새로 고침: {before} -> {after}",u8"터미널 색상: {before} -> {after}",u8"최근 이벤트: {before} -> {after}",u8"worker 레이블 숨기기: {before} -> {after}",
            u8"상세 페이지: {before} -> {after}",u8"로그 페이지: {before} -> {after}",u8"상세 장치 식별: {before} -> {after}",u8"상세 풀: {before} -> {after}",u8"상세 재시작: {before} -> {after}",u8"상세 오류: {before} -> {after}",
            u8"미분류 로그: {before} -> {after}",u8"로그 타임스탬프: {before} -> {after}",u8"ISO UTC 시간: {before} -> {after}",u8"로그 따라가기: {before} -> {after}",u8"로그 심각도: {before} -> {after}",u8"로그 기록 수: {before} -> {after}"}}},
        {"ar",true,ReviewStatus::MachineAssisted,{{u8"تحديث العرض: {before} -> {after}",u8"لون الطرفية: {before} -> {after}",u8"الأحداث الأخيرة: {before} -> {after}",u8"إخفاء تسمية worker: {before} -> {after}",
            u8"صفحة التفاصيل: {before} -> {after}",u8"صفحة السجل: {before} -> {after}",u8"هوية الجهاز في التفاصيل: {before} -> {after}",u8"المجمع في التفاصيل: {before} -> {after}",u8"إعادات التشغيل في التفاصيل: {before} -> {after}",u8"الأخطاء في التفاصيل: {before} -> {after}",
            u8"السجلات غير المصنفة: {before} -> {after}",u8"الطوابع الزمنية للسجل: {before} -> {after}",u8"وقت ISO UTC: {before} -> {after}",u8"متابعة السجل: {before} -> {after}",u8"شدة السجل: {before} -> {after}",u8"عدد السجلات: {before} -> {after}"}}},
        {"hi",false,ReviewStatus::MachineAssisted,{{u8"प्रदर्शन रीफ़्रेश: {before} -> {after}",u8"टर्मिनल रंग: {before} -> {after}",u8"हाल के इवेंट: {before} -> {after}",u8"worker लेबल छिपाएँ: {before} -> {after}",
            u8"विवरण पृष्ठ: {before} -> {after}",u8"लॉग पृष्ठ: {before} -> {after}",u8"विवरण में डिवाइस पहचान: {before} -> {after}",u8"विवरण में पूल: {before} -> {after}",u8"विवरण में पुनः आरंभ: {before} -> {after}",u8"विवरण में त्रुटियाँ: {before} -> {after}",
            u8"अवर्गीकृत लॉग: {before} -> {after}",u8"लॉग टाइमस्टैम्प: {before} -> {after}",u8"ISO UTC समय: {before} -> {after}",u8"लॉग का अनुसरण: {before} -> {after}",u8"लॉग गंभीरता: {before} -> {after}",u8"लॉग रिकॉर्ड संख्या: {before} -> {after}"}}},
        {"bn",false,ReviewStatus::MachineAssisted,{{u8"প্রদর্শন রিফ্রেশ: {before} -> {after}",u8"টার্মিনালের রং: {before} -> {after}",u8"সাম্প্রতিক ঘটনা: {before} -> {after}",u8"worker লেবেল লুকান: {before} -> {after}",
            u8"বিস্তারিত পৃষ্ঠা: {before} -> {after}",u8"লগ পৃষ্ঠা: {before} -> {after}",u8"বিস্তারিত ডিভাইস পরিচয়: {before} -> {after}",u8"বিস্তারিত পুল: {before} -> {after}",u8"বিস্তারিত পুনরারম্ভ: {before} -> {after}",u8"বিস্তারিত ত্রুটি: {before} -> {after}",
            u8"অশ্রেণিবদ্ধ লগ: {before} -> {after}",u8"লগের টাইমস্ট্যাম্প: {before} -> {after}",u8"ISO UTC সময়: {before} -> {after}",u8"লগ অনুসরণ: {before} -> {after}",u8"লগের গুরুত্ব: {before} -> {after}",u8"লগ রেকর্ড সংখ্যা: {before} -> {after}"}}},
        {"id",false,ReviewStatus::MachineAssisted,{{"Penyegaran tampilan: {before} -> {after}","Warna terminal: {before} -> {after}","Peristiwa terbaru: {before} -> {after}","Sembunyikan label worker: {before} -> {after}",
            "Halaman detail: {before} -> {after}","Halaman log: {before} -> {after}","Identitas dalam detail: {before} -> {after}","Pool dalam detail: {before} -> {after}","Mulai ulang dalam detail: {before} -> {after}","Kesalahan dalam detail: {before} -> {after}",
            "Log tanpa klasifikasi: {before} -> {after}","Stempel waktu log: {before} -> {after}","Waktu ISO UTC: {before} -> {after}","Ikuti log: {before} -> {after}","Keparahan log: {before} -> {after}","Jumlah catatan log: {before} -> {after}"}}},
        {"tr",false,ReviewStatus::MachineAssisted,{{u8"Görüntü yenileme: {before} -> {after}","Terminal rengi: {before} -> {after}","Son olaylar: {before} -> {after}",u8"Worker etiketini gizle: {before} -> {after}",
            u8"Ayrıntılar sayfası: {before} -> {after}",u8"Günlük sayfası: {before} -> {after}",u8"Ayrıntılarda aygıt kimliği: {before} -> {after}",u8"Ayrıntılarda havuz: {before} -> {after}",u8"Ayrıntılarda yeniden başlatma: {before} -> {after}",u8"Ayrıntılarda hatalar: {before} -> {after}",
            u8"Sınıflandırılmamış günlükler: {before} -> {after}",u8"Günlük zaman damgaları: {before} -> {after}",u8"ISO UTC saati: {before} -> {after}",u8"Günlüğü izle: {before} -> {after}",u8"Günlük önem düzeyi: {before} -> {after}",u8"Günlük kayıt sayısı: {before} -> {after}"}}},
        {"vi",false,ReviewStatus::MachineAssisted,{{u8"Làm mới hiển thị: {before} -> {after}",u8"Màu thiết bị đầu cuối: {before} -> {after}",u8"Sự kiện gần đây: {before} -> {after}",u8"Ẩn nhãn worker: {before} -> {after}",
            u8"Trang chi tiết: {before} -> {after}",u8"Trang nhật ký: {before} -> {after}",u8"Định danh trong chi tiết: {before} -> {after}",u8"Pool trong chi tiết: {before} -> {after}",u8"Khởi động lại trong chi tiết: {before} -> {after}",u8"Lỗi trong chi tiết: {before} -> {after}",
            u8"Nhật ký chưa phân loại: {before} -> {after}",u8"Dấu thời gian nhật ký: {before} -> {after}",u8"Giờ ISO UTC: {before} -> {after}",u8"Theo dõi nhật ký: {before} -> {after}",u8"Mức độ nhật ký: {before} -> {after}",u8"Số bản ghi nhật ký: {before} -> {after}"}}}
    }};return packs;
}
inline std::string_view settingsChangeMessageKeyName(SettingsChangeMessageKey key){
    static constexpr std::array<std::string_view,settingsChangeMessageKeyCount> names={{"settings.change.refresh","settings.change.color","settings.change.events","settings.change.hide_worker","settings.change.details_page","settings.change.log_page",
        "settings.change.details_identity","settings.change.details_pool","settings.change.details_restarts","settings.change.details_errors","settings.change.unclassified_logs","settings.change.log_timestamps","settings.change.iso_utc_time","settings.change.log_follow","settings.change.log_level","settings.change.log_records"}};
    const auto index=static_cast<std::size_t>(key);return index<names.size()?names[index]:std::string_view{};
}
inline LocalizedMessage settingsChangeMessage(std::string_view language,SettingsChangeMessageKey key){
    const auto index=static_cast<std::size_t>(key);if(index>=settingsChangeMessageKeyCount)return {};
    const auto &source=settingsChangeLocalePacks()[0];const auto *selected=&source;for(const auto &pack:settingsChangeLocalePacks())if(pack.id==language){selected=&pack;break;}
    if(selected->id!=language||!validTemplateTranslation(source.messages[index],selected->messages[index]))return {source.messages[index],source.id,source.review,true,false};
    return {selected->messages[index],selected->id,selected->review,false,selected->rtl};
}
inline std::string settingsChangeEnglish(SettingsChangeMessageKey key,const std::string &before,const std::string &after){
    const auto printable=[](const std::string &value){return !value.empty()&&value.size()<=4096&&std::all_of(value.begin(),value.end(),[](unsigned char c){return c>=32&&c<=126;});};
    const auto text=settingsChangeMessage("en",key).text;if(text.empty()||!printable(before)||!printable(after))return {};
    // Walk only the catalog template: braces inside values must never be interpreted a second time.
    std::string result;std::size_t at=0;
    while(at<text.size()){const auto start=text.find('{',at);if(start==text.npos){result.append(text.substr(at));break;}
        result.append(text.substr(at,start-at));const auto end=text.find('}',start);if(end==text.npos)return {};
        const auto name=text.substr(start+1,end-start-1);if(name=="before")result+=before;else if(name=="after")result+=after;else return {};
        if(result.size()>4096)return {};
        at=end+1;
    }
    return result.size()<=4096?result:std::string{};
}
} } // namespace om::i18n
