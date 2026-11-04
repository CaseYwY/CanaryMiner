// Settings shell only: field/subpanel domains are separate. Non-English text is machine-assisted draft.
#pragma once
#include "localization.h"
namespace om { namespace i18n {
enum class SettingsMessageKey { Title, UnsavedSuffix, SeparateSaves, ReviewHeading, NoChanges, Untouched, Unsaved, LeaveKeys, GroupHeading,
    FocusGroups, FocusFields, PrivacyNote, FooterReview, FooterBrowse, ShortTitle, ShortResize, ShortKeys, Saved, SaveFailed,
    Display, Automatic, Electricity, MarketTiming, ProfilesTests, Games, PoolsTls, Language, Privacy, Details, Log, Count };
constexpr std::size_t settingsMessageKeyCount=static_cast<std::size_t>(SettingsMessageKey::Count);
struct SettingsLocalePack {std::string_view id;bool rtl;ReviewStatus review;std::array<std::string_view,settingsMessageKeyCount> messages;};
inline const std::array<SettingsLocalePack,16> &settingsLocalePacks(){
    static const std::array<SettingsLocalePack,16> packs={{
        {"en",false,ReviewStatus::Source,{{"(o> CanaryMiner / Settings"," * unsaved",
            "UI, runtime and offline references save separately; no automatic miner restart.","Review {count} changes / arrows scroll","No changes.",
            "Mining config, donation and hardware settings are untouched.","Unsaved changes","[Enter] Review/save   [D] Discard   [Esc] Continue editing",
            "< {group} >  (Left/Right: group)","Focus: groups / Tab or Enter to edit","Focus: fields / Tab to choose a group","No private config values are shown here.",
            "[Enter/F2] Save  [Esc] Edit  [Q] Stop","[Arrows] Move [Tab] Pane [Space/Enter] Change [V/F2] Review [Esc] Back [Q] Stop",
            "Terminal too short.","Resize to 12+ rows.","[Q] Stop [Esc] Back","Saved. Presentation changes are live; mining settings were not changed.",
            "Save failed; active settings are unchanged.","Display","Automatic mining","Electricity","Market and timing","Profiles and tests","Games","Pools and TLS","Language","Privacy","Details","Log"}}},
        {"zh-Hans",false,ReviewStatus::MachineAssisted,{{u8"(o> CanaryMiner / 设置",u8" * 未保存",
            u8"界面、运行默认值和离线参考分别保存；不会自动重启矿工。",u8"审阅 {count} 项更改 / 方向键滚动",u8"没有更改。",
            u8"挖矿配置、捐赠和硬件设置均未改动。",u8"未保存的更改",u8"[Enter] 审阅/保存   [D] 放弃   [Esc] 继续编辑",
            u8"< {group} >  (左/右：组)",u8"焦点：组 / Tab 或 Enter 进入编辑",u8"焦点：字段 / Tab 选择组",u8"此处不显示私有配置值。",
            u8"[Enter/F2] 保存  [Esc] 编辑  [Q] 停止",u8"[Arrows] 移动 [Tab] 面板 [Space/Enter] 更改 [V/F2] 审阅 [Esc] 返回 [Q] 停止",
            u8"终端行数不足。",u8"请调整为至少 12 行。",u8"[Q] 停止 [Esc] 返回",u8"已保存。显示更改已生效；挖矿设置未改动。",
            u8"保存失败；当前设置未改动。",u8"显示",u8"自动挖矿",u8"电力",u8"行情与时间",u8"配置档与测试",u8"游戏",u8"矿池与 TLS",u8"语言",u8"隐私",u8"详情",u8"日志"}}},
        {"zh-Hant",false,ReviewStatus::MachineAssisted,{{u8"(o> CanaryMiner / 設定",u8" * 未儲存",
            u8"介面、執行預設值與離線參考分別儲存；不會自動重新啟動礦工。",u8"檢閱 {count} 項變更 / 方向鍵捲動",u8"沒有變更。",
            u8"挖礦組態、捐贈與硬體設定均未變更。",u8"未儲存的變更",u8"[Enter] 檢閱/儲存   [D] 捨棄   [Esc] 繼續編輯",
            u8"< {group} >  (左/右：群組)",u8"焦點：群組 / Tab 或 Enter 進入編輯",u8"焦點：欄位 / Tab 選擇群組",u8"此處不顯示私有組態值。",
            u8"[Enter/F2] 儲存  [Esc] 編輯  [Q] 停止",u8"[Arrows] 移動 [Tab] 面板 [Space/Enter] 變更 [V/F2] 檢閱 [Esc] 返回 [Q] 停止",
            u8"終端機行數不足。",u8"請調整為至少 12 行。",u8"[Q] 停止 [Esc] 返回",u8"已儲存。顯示變更已生效；挖礦設定未變更。",
            u8"儲存失敗；目前設定未變更。",u8"顯示",u8"自動挖礦",u8"電力",u8"行情與時間",u8"設定檔與測試",u8"遊戲",u8"礦池與 TLS",u8"語言",u8"隱私",u8"詳細資料",u8"日誌"}}},
        {"es",false,ReviewStatus::MachineAssisted,{{"(o> CanaryMiner / Ajustes"," * sin guardar",
            "La interfaz, los valores de inicio y las referencias sin red se guardan por separado; no se reinicia el minero.","Revisar {count} cambios / flechas para desplazar","Sin cambios.",
            u8"La configuración de minería, la donación y los ajustes de hardware no cambian.","Cambios sin guardar","[Enter] Revisar/guardar   [D] Descartar   [Esc] Seguir editando",
            "< {group} >  (Izquierda/Derecha: grupo)","Foco: grupos / Tab o Enter para editar","Foco: campos / Tab para elegir grupo",u8"No se muestran valores de configuración privados.",
            "[Enter/F2] Guardar  [Esc] Editar  [Q] Detener","[Arrows] Mover [Tab] Panel [Space/Enter] Cambiar [V/F2] Revisar [Esc] Volver [Q] Detener",
            "Terminal demasiado bajo.","Amplie a 12 filas o mas.","[Q] Detener [Esc] Volver",u8"Guardado. Los cambios de presentación están activos; la minería no cambia.",
            "Error al guardar; los ajustes activos no cambian.","Pantalla",u8"Minería automática","Electricidad","Mercado y tiempo","Perfiles y pruebas","Juegos","Pools y TLS","Idioma","Privacidad","Detalles","Registro"}}},
        {"pt-BR",false,ReviewStatus::MachineAssisted,{{u8"(o> CanaryMiner / Configurações",u8" * não salvo",
            u8"Interface, padrões de execução e referências offline são salvos separadamente; sem reiniciar o minerador.",u8"Revisar {count} alterações / setas para rolar",u8"Sem alterações.",
            u8"Configuração de mineração, doação e hardware permanecem intactos.",u8"Alterações não salvas","[Enter] Revisar/salvar   [D] Descartar   [Esc] Continuar editando",
            "< {group} >  (Esquerda/Direita: grupo)","Foco: grupos / Tab ou Enter para editar","Foco: campos / Tab para escolher grupo",u8"Valores privados da configuração não aparecem aqui.",
            "[Enter/F2] Salvar  [Esc] Editar  [Q] Parar","[Arrows] Mover [Tab] Painel [Space/Enter] Alterar [V/F2] Revisar [Esc] Voltar [Q] Parar",
            "Terminal muito baixo.","Redimensione para 12 linhas ou mais.","[Q] Parar [Esc] Voltar",u8"Salvo. Alterações visuais ativas; a mineração não foi alterada.",
            u8"Falha ao salvar; configurações ativas intactas.",u8"Exibição",u8"Mineração automática","Eletricidade","Mercado e tempo","Perfis e testes","Jogos","Pools e TLS","Idioma","Privacidade","Detalhes","Log"}}},
        {"fr",false,ReviewStatus::MachineAssisted,{{u8"(o> CanaryMiner / Paramètres",u8" * non enregistré",
            u8"Interface, valeurs de lancement et références hors ligne sont enregistrées séparément ; aucun redémarrage automatique.",u8"Examiner {count} modifications / défilement par flèches","Aucune modification.",
            u8"Configuration du minage, don et paramètres matériels inchangés.",u8"Modifications non enregistrées",u8"[Enter] Examiner/enregistrer   [D] Abandonner   [Esc] Continuer",
            "< {group} >  (Gauche/Droite : groupe)",u8"Focus : groupes / Tab ou Enter pour éditer","Focus : champs / Tab pour choisir un groupe",u8"Aucune valeur de configuration privée affichée ici.",
            u8"[Enter/F2] Enregistrer  [Esc] Éditer  [Q] Arrêter",u8"[Arrows] Bouger [Tab] Volet [Space/Enter] Modifier [V/F2] Examiner [Esc] Retour [Q] Arrêter",
            "Terminal trop court.","Agrandir a 12 lignes ou plus.",u8"[Q] Arrêter [Esc] Retour",u8"Enregistré. Modifications visuelles actives ; paramètres de minage inchangés.",
            u8"Échec de l'enregistrement ; paramètres actifs inchangés.","Affichage","Minage automatique",u8"Électricité",u8"Marché et temps","Profils et tests","Jeux","Pools et TLS","Langue",u8"Confidentialité",u8"Détails","Journal"}}},
        {"de",false,ReviewStatus::MachineAssisted,{{"(o> CanaryMiner / Einstellungen"," * ungespeichert",
            "Anzeige, Startvorgaben und Offline-Referenzen werden separat gespeichert; kein automatischer Miner-Neustart.",u8"{count} Änderungen prüfen / Pfeile zum Scrollen",u8"Keine Änderungen.",
            "Mining-Konfiguration, Spende und Hardware-Einstellungen bleiben unveraendert.",u8"Ungespeicherte Änderungen",u8"[Enter] Prüfen/speichern   [D] Verwerfen   [Esc] Weiter bearbeiten",
            "< {group} >  (Links/Rechts: Gruppe)","Fokus: Gruppen / Tab oder Enter zum Bearbeiten","Fokus: Felder / Tab zur Gruppenauswahl","Keine privaten Konfigurationswerte werden hier angezeigt.",
            "[Enter/F2] Speichern  [Esc] Bearbeiten  [Q] Stoppen",u8"[Arrows] Bewegen [Tab] Bereich [Space/Enter] Ändern [V/F2] Prüfen [Esc] Zurück [Q] Stoppen",
            "Terminal zu niedrig.","Auf mindestens 12 Zeilen vergroessern.",u8"[Q] Stoppen [Esc] Zurück",u8"Gespeichert. Anzeigeänderungen sind aktiv; Mining-Einstellungen bleiben unverändert.",
            "Speichern fehlgeschlagen; aktive Einstellungen bleiben unveraendert.","Anzeige","Automatisches Mining","Strom","Markt und Zeit","Profile und Tests","Spiele","Pools und TLS","Sprache","Datenschutz","Details","Protokoll"}}},
        {"ru",false,ReviewStatus::MachineAssisted,{{u8"(o> CanaryMiner / Настройки",u8" * не сохранено",
            u8"Интерфейс, параметры запуска и офлайн-данные сохраняются отдельно; майнер не перезапускается.",u8"Проверка изменений: {count} / прокрутка стрелками",u8"Нет изменений.",
            u8"Конфигурация майнинга, пожертвования и оборудование не изменяются.",u8"Несохранённые изменения",u8"[Enter] Проверить/сохранить   [D] Отменить   [Esc] Продолжить",
            u8"< {group} >  (Влево/Вправо: группа)",u8"Фокус: группы / Tab или Enter для правки",u8"Фокус: поля / Tab для выбора группы",u8"Частные значения конфигурации здесь не показаны.",
            u8"[Enter/F2] Сохранить  [Esc] Править  [Q] Стоп",u8"[Arrows] Двигать [Tab] Панель [Space/Enter] Изменить [V/F2] Проверить [Esc] Назад [Q] Стоп",
            u8"Недостаточно строк терминала.",u8"Увеличьте до 12 строк или больше.",u8"[Q] Стоп [Esc] Назад",u8"Сохранено. Изменения отображения активны; майнинг не изменён.",
            u8"Не удалось сохранить; текущие настройки не изменены.",u8"Отображение",u8"Автомайнинг",u8"Электроэнергия",u8"Рынок и время",u8"Профили и тесты",u8"Игры",u8"Пулы и TLS",u8"Язык",u8"Конфиденциальность",u8"Подробности",u8"Журнал"}}},
        {"ja",false,ReviewStatus::MachineAssisted,{{u8"(o> CanaryMiner / 設定",u8" * 未保存",
            u8"表示、起動時の既定値、オフライン参照は個別に保存されます。自動再起動はしません。",u8"変更 {count} 件を確認 / 矢印でスクロール",u8"変更はありません。",
            u8"マイニング設定、寄付、ハードウェア設定は変更されません。",u8"未保存の変更",u8"[Enter] 確認/保存   [D] 破棄   [Esc] 編集を続ける",
            u8"< {group} >  (左/右：グループ)",u8"対象：グループ / Tab または Enter で編集",u8"対象：項目 / Tab でグループ選択",u8"ここには非公開の設定値を表示しません。",
            u8"[Enter/F2] 保存  [Esc] 編集  [Q] 停止",u8"[Arrows] 移動 [Tab] ペイン [Space/Enter] 変更 [V/F2] 確認 [Esc] 戻る [Q] 停止",
            u8"端末の行数が足りません。",u8"12 行以上に変更してください。",u8"[Q] 停止 [Esc] 戻る",u8"保存しました。表示の変更は有効です。マイニング設定は変更していません。",
            u8"保存できませんでした。現在の設定は変更されません。",u8"表示",u8"自動マイニング",u8"電力",u8"市場と時刻",u8"プロファイルとテスト",u8"ゲーム",u8"プールと TLS",u8"言語",u8"プライバシー",u8"詳細",u8"ログ"}}},
        {"ko",false,ReviewStatus::MachineAssisted,{{u8"(o> CanaryMiner / 설정",u8" * 저장 안 됨",
            u8"화면, 시작 기본값과 오프라인 참조는 따로 저장하며 채굴기를 자동 재시작하지 않습니다.",u8"변경 {count}개 검토 / 화살표로 스크롤",u8"변경 사항 없음.",
            u8"채굴 구성, 기부와 하드웨어 설정은 바뀌지 않습니다.",u8"저장하지 않은 변경",u8"[Enter] 검토/저장   [D] 버리기   [Esc] 계속 편집",
            u8"< {group} >  (왼쪽/오른쪽: 그룹)",u8"초점: 그룹 / Tab 또는 Enter로 편집",u8"초점: 필드 / Tab으로 그룹 선택",u8"비공개 구성 값은 여기에 표시하지 않습니다.",
            u8"[Enter/F2] 저장  [Esc] 편집  [Q] 중지",u8"[Arrows] 이동 [Tab] 패널 [Space/Enter] 변경 [V/F2] 검토 [Esc] 뒤로 [Q] 중지",
            u8"터미널 행이 부족합니다.",u8"12행 이상으로 늘리세요.",u8"[Q] 중지 [Esc] 뒤로",u8"저장됨. 화면 변경은 적용되었으며 채굴 설정은 바뀌지 않았습니다.",
            u8"저장 실패. 현재 설정은 바뀌지 않았습니다.",u8"화면",u8"자동 채굴",u8"전력",u8"시장 및 시간",u8"프로필 및 테스트",u8"게임",u8"풀 및 TLS",u8"언어",u8"개인정보",u8"상세",u8"로그"}}},
        {"ar",true,ReviewStatus::MachineAssisted,{{u8"(o> CanaryMiner / الإعدادات",u8" * غير محفوظ",
            u8"تُحفظ الواجهة وإعدادات البدء والمراجع دون اتصال بشكل منفصل؛ لا إعادة تشغيل تلقائية للمعدّن.",u8"مراجعة التغييرات: {count} / التمرير بالأسهم",u8"لا تغييرات.",
            u8"إعدادات التعدين والتبرع والعتاد لم تتغير.",u8"تغييرات غير محفوظة",u8"[Enter] مراجعة/حفظ   [D] تجاهل   [Esc] متابعة التحرير",
            u8"< {group} >  (يسار/يمين: مجموعة)",u8"التركيز: المجموعات / Tab أو Enter للتحرير",u8"التركيز: الحقول / Tab لاختيار مجموعة",u8"لا تُعرض هنا قيم الإعدادات الخاصة.",
            u8"[Enter/F2] حفظ  [Esc] تحرير  [Q] إيقاف",u8"[Arrows] نقل [Tab] لوحة [Space/Enter] تغيير [V/F2] مراجعة [Esc] رجوع [Q] إيقاف",
            u8"عدد أسطر الطرفية غير كافٍ.",u8"وسّع الطرفية إلى 12 سطرًا على الأقل.",u8"[Q] إيقاف [Esc] رجوع",u8"تم الحفظ. تغييرات العرض فعالة؛ إعدادات التعدين لم تتغير.",
            u8"فشل الحفظ؛ الإعدادات الحالية لم تتغير.",u8"العرض",u8"التعدين التلقائي",u8"الكهرباء",u8"السوق والوقت",u8"الملفات والاختبارات",u8"الألعاب",u8"المجمعات و TLS",u8"اللغة",u8"الخصوصية",u8"التفاصيل",u8"السجل"}}},
        {"hi",false,ReviewStatus::MachineAssisted,{{u8"(o> CanaryMiner / सेटिंग",u8" * सहेजा नहीं गया",
            u8"इंटरफ़ेस, आरंभिक डिफ़ॉल्ट और ऑफ़लाइन संदर्भ अलग सहेजे जाते हैं; माइनर अपने आप पुनः चालू नहीं होता।",u8"{count} बदलाव की समीक्षा / तीर से स्क्रॉल",u8"कोई बदलाव नहीं।",
            u8"माइनिंग विन्यास, दान और हार्डवेयर सेटिंग नहीं बदली गईं।",u8"बिना सहेजे बदलाव",u8"[Enter] समीक्षा/सहेजें   [D] छोड़ें   [Esc] संपादन जारी रखें",
            u8"< {group} >  (बायाँ/दायाँ: समूह)",u8"फ़ोकस: समूह / संपादन के लिए Tab या Enter",u8"फ़ोकस: फ़ील्ड / समूह चुनने के लिए Tab",u8"निजी विन्यास मान यहाँ नहीं दिखाए जाते।",
            u8"[Enter/F2] सहेजें  [Esc] संपादित करें  [Q] रोकें",u8"[Arrows] चलें [Tab] पैनल [Space/Enter] बदलें [V/F2] समीक्षा [Esc] वापस [Q] रोकें",
            u8"टर्मिनल में पंक्तियाँ कम हैं।",u8"कम से कम 12 पंक्तियाँ करें।",u8"[Q] रोकें [Esc] वापस",u8"सहेजा गया। प्रदर्शन बदलाव लागू हैं; माइनिंग सेटिंग नहीं बदली गईं।",
            u8"सहेजना विफल; सक्रिय सेटिंग नहीं बदलीं।",u8"प्रदर्शन",u8"स्वचालित माइनिंग",u8"बिजली",u8"बाज़ार और समय",u8"प्रोफ़ाइल और परीक्षण",u8"गेम",u8"पूल और TLS",u8"भाषा",u8"गोपनीयता",u8"विवरण",u8"लॉग"}}},
        {"bn",false,ReviewStatus::MachineAssisted,{{u8"(o> CanaryMiner / সেটিংস",u8" * সংরক্ষিত নয়",
            u8"ইন্টারফেস, শুরুর ডিফল্ট ও অফলাইন তথ্য আলাদা করে সংরক্ষিত হয়; মাইনার নিজে থেকে পুনরায় চালু হয় না।",u8"পরিবর্তন পর্যালোচনা: {count} / তীর দিয়ে স্ক্রল",u8"কোনো পরিবর্তন নেই।",
            u8"মাইনিং কনফিগারেশন, অনুদান ও হার্ডওয়্যার সেটিংস অপরিবর্তিত।",u8"অসংরক্ষিত পরিবর্তন",u8"[Enter] পর্যালোচনা/সংরক্ষণ   [D] বাতিল   [Esc] সম্পাদনা চালান",
            u8"< {group} >  (বাম/ডান: গোষ্ঠী)",u8"ফোকাস: গোষ্ঠী / সম্পাদনায় Tab বা Enter",u8"ফোকাস: ক্ষেত্র / গোষ্ঠী বাছতে Tab",u8"ব্যক্তিগত কনফিগারেশনের মান এখানে দেখানো হয় না।",
            u8"[Enter/F2] সংরক্ষণ  [Esc] সম্পাদনা  [Q] থামান",u8"[Arrows] সরান [Tab] প্যানেল [Space/Enter] বদলান [V/F2] পর্যালোচনা [Esc] ফিরুন [Q] থামান",
            u8"টার্মিনালে পর্যাপ্ত সারি নেই।",u8"অন্তত 12 সারি করুন।",u8"[Q] থামান [Esc] ফিরুন",u8"সংরক্ষিত। প্রদর্শনের পরিবর্তন সক্রিয়; মাইনিং সেটিংস অপরিবর্তিত।",
            u8"সংরক্ষণ ব্যর্থ; সক্রিয় সেটিংস অপরিবর্তিত।",u8"প্রদর্শন",u8"স্বয়ংক্রিয় মাইনিং",u8"বিদ্যুৎ",u8"বাজার ও সময়",u8"প্রোফাইল ও পরীক্ষা",u8"গেম",u8"পুল ও TLS",u8"ভাষা",u8"গোপনীয়তা",u8"বিস্তারিত",u8"লগ"}}},
        {"id",false,ReviewStatus::MachineAssisted,{{"(o> CanaryMiner / Pengaturan"," * belum disimpan",
            "Antarmuka, bawaan mulai, dan referensi luring disimpan terpisah; penambang tidak dimulai ulang otomatis.","Tinjau {count} perubahan / gulir dengan panah","Tidak ada perubahan.",
            "Konfigurasi penambangan, donasi, dan perangkat keras tidak berubah.","Perubahan belum disimpan","[Enter] Tinjau/simpan   [D] Buang   [Esc] Lanjut mengedit",
            "< {group} >  (Kiri/Kanan: grup)","Fokus: grup / Tab atau Enter untuk mengedit","Fokus: kolom / Tab untuk memilih grup","Nilai konfigurasi pribadi tidak ditampilkan di sini.",
            "[Enter/F2] Simpan  [Esc] Edit  [Q] Hentikan","[Arrows] Pindah [Tab] Panel [Space/Enter] Ubah [V/F2] Tinjau [Esc] Kembali [Q] Hentikan",
            "Terminal terlalu pendek.","Perbesar menjadi minimal 12 baris.","[Q] Hentikan [Esc] Kembali","Disimpan. Perubahan tampilan aktif; pengaturan penambangan tidak berubah.",
            "Gagal menyimpan; pengaturan aktif tidak berubah.","Tampilan","Penambangan otomatis","Listrik","Pasar dan waktu","Profil dan pengujian","Gim","Pool dan TLS","Bahasa","Privasi","Detail","Log"}}},
        {"tr",false,ReviewStatus::MachineAssisted,{{u8"(o> CanaryMiner / Ayarlar",u8" * kaydedilmedi",
            u8"Arayüz, başlangıç varsayılanları ve çevrimdışı referanslar ayrı kaydedilir; madenci otomatik yeniden başlatılmaz.",u8"{count} değişikliği incele / oklarla kaydır",u8"Değişiklik yok.",
            u8"Madencilik, bağış ve donanım ayarları değişmedi.",u8"Kaydedilmemiş değişiklikler",u8"[Enter] İncele/kaydet   [D] Vazgeç   [Esc] Düzenlemeye devam",
            u8"< {group} >  (Sol/Sağ: grup)",u8"Odak: gruplar / düzenlemek için Tab veya Enter",u8"Odak: alanlar / grup seçmek için Tab",u8"Özel yapılandırma değerleri burada gösterilmez.",
            u8"[Enter/F2] Kaydet  [Esc] Düzenle  [Q] Durdur",u8"[Arrows] Taşı [Tab] Bölme [Space/Enter] Değiştir [V/F2] İncele [Esc] Geri [Q] Durdur",
            u8"Terminal satır sayısı yetersiz.",u8"En az 12 satıra büyütün.",u8"[Q] Durdur [Esc] Geri",u8"Kaydedildi. Görüntü değişiklikleri etkin; madencilik ayarları değişmedi.",
            u8"Kayıt başarısız; etkin ayarlar değişmedi.",u8"Görünüm",u8"Otomatik madencilik","Elektrik",u8"Piyasa ve zaman","Profiller ve testler","Oyunlar",u8"Havuzlar ve TLS","Dil",u8"Gizlilik",u8"Ayrıntılar",u8"Günlük"}}},
        {"vi",false,ReviewStatus::MachineAssisted,{{u8"(o> CanaryMiner / Cài đặt",u8" * chưa lưu",
            u8"Giao diện, mặc định khởi chạy và tham chiếu ngoại tuyến được lưu riêng; không tự khởi động lại trình đào.",u8"Xem lại {count} thay đổi / cuộn bằng phím mũi tên",u8"Không có thay đổi.",
            u8"Cấu hình đào, quyên góp và phần cứng không thay đổi.",u8"Thay đổi chưa lưu",u8"[Enter] Xem lại/lưu   [D] Bỏ   [Esc] Tiếp tục sửa",
            u8"< {group} >  (Trái/Phải: nhóm)",u8"Tiêu điểm: nhóm / Tab hoặc Enter để sửa",u8"Tiêu điểm: trường / Tab để chọn nhóm",u8"Không hiển thị giá trị cấu hình riêng tư ở đây.",
            u8"[Enter/F2] Lưu  [Esc] Sửa  [Q] Dừng",u8"[Arrows] Di chuyển [Tab] Khung [Space/Enter] Đổi [V/F2] Xem lại [Esc] Quay lại [Q] Dừng",
            u8"Thiết bị đầu cuối thiếu dòng.",u8"Tăng lên ít nhất 12 dòng.",u8"[Q] Dừng [Esc] Quay lại",u8"Đã lưu. Thay đổi hiển thị có hiệu lực; cài đặt đào không đổi.",
            u8"Lưu thất bại; cài đặt hiện hành không đổi.",u8"Hiển thị",u8"Đào tự động",u8"Điện",u8"Thị trường và thời gian",u8"Hồ sơ và kiểm thử",u8"Trò chơi",u8"Pool và TLS",u8"Ngôn ngữ",u8"Quyền riêng tư",u8"Chi tiết",u8"Nhật ký"}}}
    }};return packs;
}
inline const std::array<SettingsMessageKey,11> &settingsGroupKeys(){
    static constexpr std::array<SettingsMessageKey,11> keys={{SettingsMessageKey::Display,SettingsMessageKey::Automatic,SettingsMessageKey::Electricity,SettingsMessageKey::MarketTiming,
        SettingsMessageKey::ProfilesTests,SettingsMessageKey::Games,SettingsMessageKey::PoolsTls,SettingsMessageKey::Language,SettingsMessageKey::Privacy,SettingsMessageKey::Details,SettingsMessageKey::Log}};return keys;
}
inline std::string_view settingsMessageKeyName(SettingsMessageKey key){
    static constexpr std::array<std::string_view,settingsMessageKeyCount> names={{"settings.shell.title","settings.shell.unsaved_suffix","settings.shell.separate_saves","settings.shell.review_heading",
        "settings.shell.no_changes","settings.shell.untouched","settings.shell.unsaved","settings.shell.leave_keys","settings.shell.group_heading","settings.shell.focus_groups","settings.shell.focus_fields",
        "settings.shell.privacy_note","settings.shell.footer_review","settings.shell.footer_browse","settings.shell.short_title","settings.shell.short_resize","settings.shell.short_keys","settings.shell.saved","settings.shell.save_failed",
        "settings.group.display","settings.group.automatic","settings.group.electricity","settings.group.market_timing","settings.group.profiles_tests","settings.group.games","settings.group.pools_tls","settings.group.language","settings.group.privacy","settings.group.details","settings.group.log"}};
    const auto index=static_cast<std::size_t>(key);return index<names.size()?names[index]:std::string_view{};
}
inline LocalizedMessage settingsMessage(std::string_view language,SettingsMessageKey key){
    const auto index=static_cast<std::size_t>(key);if(index>=settingsMessageKeyCount)return {};
    const auto &source=settingsLocalePacks()[0];const auto *selected=&source;for(const auto &pack:settingsLocalePacks())if(pack.id==language){selected=&pack;break;}
    if(selected->id!=language||!validTemplateTranslation(source.messages[index],selected->messages[index]))return {source.messages[index],source.id,source.review,true,false};
    return {selected->messages[index],selected->id,selected->review,false,selected->rtl};
}
inline std::string settingsEnglish(SettingsMessageKey key){return std::string(settingsMessage("en",key).text);}
inline std::string settingsEnglishField(SettingsMessageKey key,const std::string &value){
    const auto marker=key==SettingsMessageKey::ReviewHeading?"{count}":key==SettingsMessageKey::GroupHeading?"{group}":"";
    if(!*marker||value.size()>4096||!std::all_of(value.begin(),value.end(),[](unsigned char c){return c>=32&&c<=126;}))return {};
    const auto text=settingsMessage("en",key).text;const auto at=text.find(marker);if(at==text.npos)return {};
    const auto result=std::string(text.substr(0,at))+value+std::string(text.substr(at+std::char_traits<char>::length(marker)));
    return result.size()<=4096?result:std::string{};
}
} } // namespace om::i18n
