// Display/Details/Log root-field domain only. Native source stays English; translated text is draft.
#pragma once
#include "localization.h"
namespace om { namespace i18n {
enum class UiFieldMessageKey { Refresh, Color, Events, SamplingNotice, IntensityNotice, DetailsPage, DeviceIdentity, PoolState, Restarts, LastError,
    SafetyNotice, LogPage, Severity, IncludeUnclassified, Records, ObservedTime, TimeFormat, Follow, RawSeverity, FreezeNotice, All, Warnings, Errors, On, Off, Count };
constexpr std::size_t uiFieldMessageKeyCount=static_cast<std::size_t>(UiFieldMessageKey::Count);
struct UiFieldLocalePack {std::string_view id;bool rtl;ReviewStatus review;std::array<std::string_view,uiFieldMessageKeyCount> messages;};
inline const std::array<UiFieldLocalePack,16> &uiFieldLocalePacks(){
    static const std::array<UiFieldLocalePack,16> packs={{
        {"en",false,ReviewStatus::Source,{{"Display refresh    < {value} >","Terminal color     [{state}]","Recent events      [{state}]",
            "Display refresh does not change sampling","or mining intensity. NO_COLOR still wins.","Details page [{state}]","Device identity [{state}]","Pool state [{state}]","Restart count [{state}]","Last error [{state}]",
            "Main safety/status remain visible.","Log page [{state}]","Severity < {value} >","Include unclassified [{state}]","Records shown < {value} >","Show observed time [{state}]","Time format < {value} >","Follow new output [{state}]",
            "Raw severity: unclassified","Off freezes this view, not data collection.","all","warnings/errors","errors","on","off"}}},
        {"zh-Hans",false,ReviewStatus::MachineAssisted,{{u8"显示刷新 < {value} >",u8"终端颜色 [{state}]",u8"近期事件 [{state}]",
            u8"显示刷新不会改变采样",u8"或挖矿强度。NO_COLOR 始终优先。",u8"详情页 [{state}]",u8"设备标识 [{state}]",u8"矿池状态 [{state}]",u8"重启次数 [{state}]",u8"最近错误 [{state}]",
            u8"主界面的安全/状态始终可见。",u8"日志页 [{state}]",u8"级别 < {value} >",u8"包含未分类 [{state}]",u8"显示记录数 < {value} >",u8"显示观察时间 [{state}]",u8"时间格式 < {value} >",u8"跟随新输出 [{state}]",
            u8"原始日志级别：未分类",u8"关闭仅冻结此视图，不停止数据采集。",u8"全部",u8"警告/错误",u8"错误",u8"开启",u8"关闭"}}},
        {"zh-Hant",false,ReviewStatus::MachineAssisted,{{u8"顯示更新 < {value} >",u8"終端機色彩 [{state}]",u8"近期事件 [{state}]",
            u8"顯示更新不會改變取樣",u8"或挖礦強度。NO_COLOR 始終優先。",u8"詳細資料頁 [{state}]",u8"裝置識別 [{state}]",u8"礦池狀態 [{state}]",u8"重新啟動次數 [{state}]",u8"最近錯誤 [{state}]",
            u8"主畫面的安全/狀態始終可見。",u8"日誌頁 [{state}]",u8"等級 < {value} >",u8"包含未分類 [{state}]",u8"顯示記錄數 < {value} >",u8"顯示觀察時間 [{state}]",u8"時間格式 < {value} >",u8"跟隨新輸出 [{state}]",
            u8"原始日誌等級：未分類",u8"關閉僅凍結此檢視，不停止資料收集。",u8"全部",u8"警告/錯誤",u8"錯誤",u8"開啟",u8"關閉"}}},
        {"es",false,ReviewStatus::MachineAssisted,{{u8"Actualización de pantalla < {value} >","Color del terminal [{state}]","Eventos recientes [{state}]",
            u8"La actualización no cambia el muestreo",u8"ni la intensidad de minería. NO_COLOR tiene prioridad.",u8"Página de detalles [{state}]","Identidad del dispositivo [{state}]","Estado del pool [{state}]","Reinicios [{state}]",u8"Último error [{state}]",
            "La seguridad y el estado principales siguen visibles.",u8"Página de registro [{state}]","Nivel < {value} >","Incluir sin clasificar [{state}]","Registros mostrados < {value} >","Mostrar hora observada [{state}]","Formato de hora < {value} >","Seguir nueva salida [{state}]",
            "Nivel de lineas sin procesar: sin clasificar",u8"Desactivar congela la vista, no la recopilación.","todo","avisos/errores","errores","activado","desactivado"}}},
        {"pt-BR",false,ReviewStatus::MachineAssisted,{{u8"Atualização da tela < {value} >","Cor do terminal [{state}]","Eventos recentes [{state}]",
            u8"Atualizar a tela não altera a amostragem",u8"nem a intensidade da mineração. NO_COLOR tem prioridade.",u8"Página de detalhes [{state}]","Identidade do dispositivo [{state}]","Estado do pool [{state}]",u8"Número de reinícios [{state}]",u8"Último erro [{state}]",
            u8"Segurança e estado principais continuam visíveis.",u8"Página de log [{state}]",u8"Nível < {value} >",u8"Incluir não classificados [{state}]","Registros exibidos < {value} >","Mostrar hora observada [{state}]","Formato de hora < {value} >",u8"Seguir nova saída [{state}]",
            u8"Nível bruto: não classificado",u8"Desligar congela a visão, não a coleta.","todos","avisos/erros","erros","ligado","desligado"}}},
        {"fr",false,ReviewStatus::MachineAssisted,{{u8"Rafraîchissement < {value} >","Couleur du terminal [{state}]",u8"Événements récents [{state}]",
            u8"Le rafraîchissement ne change pas l'échantillonnage",u8"ni l'intensité du minage. NO_COLOR reste prioritaire.",u8"Page de détails [{state}]",u8"Identité de l'appareil [{state}]",u8"État du pool [{state}]",u8"Nombre de redémarrages [{state}]",u8"Dernière erreur [{state}]",
            u8"Sécurité et état principaux restent visibles.","Page du journal [{state}]",u8"Gravité < {value} >",u8"Inclure les éléments non classés [{state}]",u8"Entrées affichées < {value} >",u8"Afficher l'heure observée [{state}]","Format de l'heure < {value} >","Suivre les nouvelles sorties [{state}]",
            u8"Gravité brute : non classée",u8"Désactiver fige la vue, pas la collecte.","tout","avertissements/erreurs","erreurs",u8"activé",u8"désactivé"}}},
        {"de",false,ReviewStatus::MachineAssisted,{{"Anzeigeaktualisierung < {value} >","Terminalfarbe [{state}]","Letzte Ereignisse [{state}]",
            "Anzeigeaktualisierung aendert nicht die Abtastung",u8"oder Mining-Intensität. NO_COLOR hat Vorrang.","Detailseite [{state}]",u8"Geräteidentität [{state}]","Poolstatus [{state}]","Neustartanzahl [{state}]","Letzter Fehler [{state}]",
            "Hauptstatus und Sicherheit bleiben sichtbar.","Protokollseite [{state}]","Schweregrad < {value} >","Unklassifizierte einschliessen [{state}]","Angezeigte Eintraege < {value} >","Beobachtungszeit anzeigen [{state}]","Zeitformat < {value} >","Neue Ausgabe verfolgen [{state}]",
            "Roher Schweregrad: unklassifiziert","Aus friert nur die Ansicht ein, nicht die Erfassung.","alle","Warnungen/Fehler","Fehler","an","aus"}}},
        {"ru",false,ReviewStatus::MachineAssisted,{{u8"Обновление экрана < {value} >",u8"Цвет терминала [{state}]",u8"Последние события [{state}]",
            u8"Обновление экрана не меняет опрос",u8"или интенсивность майнинга. NO_COLOR имеет приоритет.",u8"Страница подробностей [{state}]",u8"Идентификатор устройства [{state}]",u8"Состояние пула [{state}]",u8"Число перезапусков [{state}]",u8"Последняя ошибка [{state}]",
            u8"Основной статус и безопасность остаются видимыми.",u8"Страница журнала [{state}]",u8"Уровень < {value} >",u8"Включать неклассифицированное [{state}]",u8"Количество записей < {value} >",u8"Показывать время наблюдения [{state}]",u8"Формат времени < {value} >",u8"Следовать за новым выводом [{state}]",
            u8"Уровень сырого вывода: не классифицирован",u8"Выключение замораживает вид, но не сбор данных.",u8"всё",u8"предупреждения/ошибки",u8"ошибки",u8"вкл",u8"выкл"}}},
        {"ja",false,ReviewStatus::MachineAssisted,{{u8"表示更新 < {value} >",u8"端末の色 [{state}]",u8"最近のイベント [{state}]",
            u8"表示更新はサンプリングを変更しません",u8"マイニング強度も変更しません。NO_COLOR が優先されます。",u8"詳細ページ [{state}]",u8"デバイス識別 [{state}]",u8"プールの状態 [{state}]",u8"再起動回数 [{state}]",u8"最後のエラー [{state}]",
            u8"メインの安全情報と状態は常に表示されます。",u8"ログページ [{state}]",u8"重要度 < {value} >",u8"未分類を含める [{state}]",u8"表示件数 < {value} >",u8"観測時刻を表示 [{state}]",u8"時刻形式 < {value} >",u8"新しい出力を追跡 [{state}]",
            u8"生ログの重要度：未分類",u8"オフは表示のみ固定し、収集は止めません。",u8"すべて",u8"警告/エラー",u8"エラー",u8"オン",u8"オフ"}}},
        {"ko",false,ReviewStatus::MachineAssisted,{{u8"화면 새로 고침 < {value} >",u8"터미널 색상 [{state}]",u8"최근 이벤트 [{state}]",
            u8"화면 새로 고침은 샘플링을 바꾸지 않습니다",u8"채굴 강도도 바뀌지 않습니다. NO_COLOR가 우선합니다.",u8"상세 페이지 [{state}]",u8"장치 식별 [{state}]",u8"풀 상태 [{state}]",u8"재시작 횟수 [{state}]",u8"최근 오류 [{state}]",
            u8"기본 안전 정보와 상태는 계속 표시됩니다.",u8"로그 페이지 [{state}]",u8"심각도 < {value} >",u8"미분류 포함 [{state}]",u8"표시 기록 수 < {value} >",u8"관찰 시간 표시 [{state}]",u8"시간 형식 < {value} >",u8"새 출력 따라가기 [{state}]",
            u8"원시 심각도: 미분류",u8"끄면 이 보기만 고정되며 수집은 계속됩니다.",u8"전체",u8"경고/오류",u8"오류",u8"켜짐",u8"꺼짐"}}},
        {"ar",true,ReviewStatus::MachineAssisted,{{u8"تحديث العرض < {value} >",u8"لون الطرفية [{state}]",u8"الأحداث الأخيرة [{state}]",
            u8"تحديث العرض لا يغيّر أخذ العينات",u8"ولا شدة التعدين. تبقى الأولوية لـ NO_COLOR.",u8"صفحة التفاصيل [{state}]",u8"هوية الجهاز [{state}]",u8"حالة المجمع [{state}]",u8"عدد مرات إعادة التشغيل [{state}]",u8"آخر خطأ [{state}]",
            u8"تبقى معلومات السلامة والحالة الرئيسية ظاهرة.",u8"صفحة السجل [{state}]",u8"الشدة < {value} >",u8"تضمين غير المصنف [{state}]",u8"عدد السجلات المعروضة < {value} >",u8"إظهار وقت الملاحظة [{state}]",u8"تنسيق الوقت < {value} >",u8"متابعة الإخراج الجديد [{state}]",
            u8"شدة الإخراج الخام: غير مصنفة",u8"التعطيل يجمّد العرض لا جمع البيانات.",u8"الكل",u8"تحذيرات/أخطاء",u8"أخطاء",u8"تشغيل",u8"إيقاف"}}},
        {"hi",false,ReviewStatus::MachineAssisted,{{u8"प्रदर्शन रीफ़्रेश < {value} >",u8"टर्मिनल रंग [{state}]",u8"हाल के इवेंट [{state}]",
            u8"प्रदर्शन रीफ़्रेश से सैंपलिंग नहीं बदलती",u8"न ही माइनिंग तीव्रता। NO_COLOR को प्राथमिकता है।",u8"विवरण पृष्ठ [{state}]",u8"डिवाइस पहचान [{state}]",u8"पूल की स्थिति [{state}]",u8"पुनः आरंभ संख्या [{state}]",u8"अंतिम त्रुटि [{state}]",
            u8"मुख्य सुरक्षा और स्थिति दिखाई देती रहती हैं।",u8"लॉग पृष्ठ [{state}]",u8"गंभीरता < {value} >",u8"अवर्गीकृत शामिल करें [{state}]",u8"दिखाए गए रिकॉर्ड < {value} >",u8"देखे गए समय को दिखाएँ [{state}]",u8"समय प्रारूप < {value} >",u8"नए आउटपुट का अनुसरण [{state}]",
            u8"मूल गंभीरता: अवर्गीकृत",u8"बंद करने से दृश्य रुकता है, संग्रह नहीं।",u8"सभी",u8"चेतावनी/त्रुटियाँ",u8"त्रुटियाँ",u8"चालू",u8"बंद"}}},
        {"bn",false,ReviewStatus::MachineAssisted,{{u8"প্রদর্শন রিফ্রেশ < {value} >",u8"টার্মিনালের রং [{state}]",u8"সাম্প্রতিক ঘটনা [{state}]",
            u8"প্রদর্শন রিফ্রেশে নমুনা সংগ্রহ বদলায় না",u8"মাইনিং তীব্রতাও নয়। NO_COLOR অগ্রাধিকার পায়।",u8"বিস্তারিত পৃষ্ঠা [{state}]",u8"ডিভাইসের পরিচয় [{state}]",u8"পুলের অবস্থা [{state}]",u8"পুনরারম্ভের সংখ্যা [{state}]",u8"শেষ ত্রুটি [{state}]",
            u8"মূল নিরাপত্তা ও অবস্থা দৃশ্যমান থাকে।",u8"লগ পৃষ্ঠা [{state}]",u8"গুরুত্ব < {value} >",u8"অশ্রেণিবদ্ধ অন্তর্ভুক্ত [{state}]",u8"দেখানো রেকর্ড < {value} >",u8"পর্যবেক্ষণের সময় দেখান [{state}]",u8"সময়ের বিন্যাস < {value} >",u8"নতুন আউটপুট অনুসরণ [{state}]",
            u8"মূল গুরুত্ব: অশ্রেণিবদ্ধ",u8"বন্ধ করলে দৃশ্য স্থির হয়, সংগ্রহ নয়।",u8"সব",u8"সতর্কতা/ত্রুটি",u8"ত্রুটি",u8"চালু",u8"বন্ধ"}}},
        {"id",false,ReviewStatus::MachineAssisted,{{"Penyegaran tampilan < {value} >","Warna terminal [{state}]","Peristiwa terbaru [{state}]",
            "Penyegaran tampilan tidak mengubah pencuplikan","atau intensitas penambangan. NO_COLOR tetap diutamakan.","Halaman detail [{state}]","Identitas perangkat [{state}]","Status pool [{state}]","Jumlah mulai ulang [{state}]","Kesalahan terakhir [{state}]",
            "Keamanan dan status utama tetap terlihat.","Halaman log [{state}]","Keparahan < {value} >","Sertakan tanpa klasifikasi [{state}]","Catatan ditampilkan < {value} >","Tampilkan waktu pengamatan [{state}]","Format waktu < {value} >","Ikuti keluaran baru [{state}]",
            "Keparahan mentah: tanpa klasifikasi","Nonaktif membekukan tampilan, bukan pengumpulan.","semua","peringatan/kesalahan","kesalahan","aktif","nonaktif"}}},
        {"tr",false,ReviewStatus::MachineAssisted,{{u8"Görüntü yenileme < {value} >",u8"Terminal rengi [{state}]",u8"Son olaylar [{state}]",
            u8"Görüntü yenileme örneklemeyi değiştirmez",u8"ve madencilik yoğunluğunu da değiştirmez. NO_COLOR önceliklidir.",u8"Ayrıntılar sayfası [{state}]",u8"Aygıt kimliği [{state}]","Havuz durumu [{state}]",u8"Yeniden başlatma sayısı [{state}]","Son hata [{state}]",
            u8"Ana güvenlik ve durum görünür kalır.",u8"Günlük sayfası [{state}]",u8"Önem < {value} >",u8"Sınıflandırılmamışları ekle [{state}]",u8"Gösterilen kayıtlar < {value} >",u8"Gözlem zamanını göster [{state}]",u8"Zaman biçimi < {value} >",u8"Yeni çıktıyı izle [{state}]",
            u8"Ham önem: sınıflandırılmamış",u8"Kapalı yalnızca görünümü dondurur, veri toplamayı değil.",u8"tümü",u8"uyarılar/hatalar","hatalar",u8"açık",u8"kapalı"}}},
        {"vi",false,ReviewStatus::MachineAssisted,{{u8"Làm mới hiển thị < {value} >",u8"Màu thiết bị đầu cuối [{state}]",u8"Sự kiện gần đây [{state}]",
            u8"Làm mới hiển thị không đổi việc lấy mẫu",u8"hay cường độ đào. NO_COLOR luôn ưu tiên.",u8"Trang chi tiết [{state}]",u8"Định danh thiết bị [{state}]",u8"Trạng thái pool [{state}]",u8"Số lần khởi động lại [{state}]",u8"Lỗi gần nhất [{state}]",
            u8"Thông tin an toàn và trạng thái chính luôn hiển thị.",u8"Trang nhật ký [{state}]",u8"Mức độ < {value} >",u8"Bao gồm chưa phân loại [{state}]",u8"Số bản ghi hiển thị < {value} >",u8"Hiển thị thời gian quan sát [{state}]",u8"Định dạng giờ < {value} >",u8"Theo dõi đầu ra mới [{state}]",
            u8"Mức độ thô: chưa phân loại",u8"Tắt chỉ đóng băng khung nhìn, không dừng thu thập.",u8"tất cả",u8"cảnh báo/lỗi",u8"lỗi",u8"bật",u8"tắt"}}}
    }};return packs;
}
inline std::string_view uiFieldMessageKeyName(UiFieldMessageKey key){
    static constexpr std::array<std::string_view,uiFieldMessageKeyCount> names={{"settings.field.refresh","settings.field.color","settings.field.events","settings.field.sampling_notice","settings.field.intensity_notice",
        "settings.field.details_page","settings.field.device_identity","settings.field.pool_state","settings.field.restarts","settings.field.last_error","settings.field.safety_notice","settings.field.log_page",
        "settings.field.severity","settings.field.unclassified","settings.field.records","settings.field.observed_time","settings.field.time_format","settings.field.follow","settings.field.raw_severity","settings.field.freeze_notice",
        "settings.value.all","settings.value.warnings","settings.value.errors","settings.value.on","settings.value.off"}};
    const auto index=static_cast<std::size_t>(key);return index<names.size()?names[index]:std::string_view{};
}
inline LocalizedMessage uiFieldMessage(std::string_view language,UiFieldMessageKey key){
    const auto index=static_cast<std::size_t>(key);if(index>=uiFieldMessageKeyCount)return {};
    const auto &source=uiFieldLocalePacks()[0];const auto *selected=&source;for(const auto &pack:uiFieldLocalePacks())if(pack.id==language){selected=&pack;break;}
    if(selected->id!=language||!validTemplateTranslation(source.messages[index],selected->messages[index]))return {source.messages[index],source.id,source.review,true,false};
    return {selected->messages[index],selected->id,selected->review,false,selected->rtl};
}
inline std::string uiFieldEnglish(UiFieldMessageKey key,const std::string &value={}){
    const auto text=uiFieldMessage("en",key).text;if(text.empty()||value.size()>4096||!std::all_of(value.begin(),value.end(),[](unsigned char c){return c>=32&&c<=126;}))return {};
    const auto state=text.find("{state}"),number=text.find("{value}");
    if(state==text.npos&&number==text.npos)return value.empty()?std::string(text):std::string{};
    if(state!=text.npos&&value!="x"&&value!=" ")return {};
    const auto at=state==text.npos?number:state;const auto result=std::string(text.substr(0,at))+value+std::string(text.substr(at+7));
    return result.size()<=4096?result:std::string{};
}
} } // namespace om::i18n
