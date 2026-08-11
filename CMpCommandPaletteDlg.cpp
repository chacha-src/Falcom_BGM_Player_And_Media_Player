#include "stdafx.h"
#include "ogg.h"
#include "CMpCommandPaletteDlg.h"
#include "CMediaPlayerDlg.h"
#include "oggDlg.h"
#include "CAnalyzerDlg.h"
#include "CCommandRollDlg.h"

extern CMediaPlayerDlg* mp;
extern COggDlg* og;
extern save savedata;

namespace {

// コマンド ID(このパレット内だけの番号)
enum {
	PAL_PLAY = 45000,
	PAL_PAUSE,
	PAL_STOP,
	PAL_PREV,
	PAL_NEXT,
	PAL_PHRASE_AB,
	PAL_SLEEP15,
	PAL_SLEEP30,
	PAL_SLEEP60,
	PAL_SLEEP_OFF,

	PAL_BANNER3D_TOGGLE,
	PAL_BANNER3D_RESET,
	PAL_ANA3D_TOGGLE,
	PAL_ANA3D_RESET,
	PAL_ROLL3D_TOGGLE,
	PAL_ROLL3D_RESET,
	PAL_PIANO3D_TOGGLE,
	PAL_PIANO3D_RESET,
	PAL_LRC_EXPAND,
	PAL_DESK_LRC,

	PAL_GUIDE,
	PAL_COPY_SHORTCUTS,
	PAL_TAGEDIT,
	PAL_BPM,
	PAL_EXPORT_AB,
	PAL_DUPES,
	PAL_FOLDER_SYNC,
	PAL_SMART,
	PAL_MISSING,
	PAL_RENDER,
	PAL_FOLDER_SET,

	PAL_WIN_EQ,
	PAL_WIN_PIANO,
	PAL_WIN_ANALYZER,
	PAL_WIN_PROTOOLS,
	PAL_WIN_PROMPT,
	PAL_WIN_CMDROLL,
	PAL_WIN_QUEUE,
	PAL_WIN_RECORD,
	PAL_WIN_CAPTURE,
	PAL_WIN_DJPAD,
	PAL_WIN_ALARM,
	PAL_WIN_MIRROR,
	PAL_WIN_REMOTE,
	PAL_WIN_SSVIZ,
	PAL_WIN_PLAYLIST,
	PAL_WIN_OGGHELP,
	PAL_SWITCH_FALCOM
};

// 簡易ピアノロールのコンテキストメニュー ID(CPianoRoll 内の private 定数と同値)。
// ON_COMMAND / ON_COMMAND_RANGE で受けているので WM_COMMAND を投げれば動く。
const UINT kPianoViewBase = 42221;  // +0=2D +1=簡易3D
const UINT kPianoCamReset = 42240;

struct MpPalCmd {
	int id;
	int cat; // 0=再生 1=表示 2=ツール 3=ウィンドウ
};

const MpPalCmd kCmds[] = {
	{ PAL_PLAY,             0 },
	{ PAL_PAUSE,            0 },
	{ PAL_STOP,             0 },
	{ PAL_PREV,             0 },
	{ PAL_NEXT,             0 },
	{ PAL_PHRASE_AB,        0 },
	{ PAL_SLEEP15,          0 },
	{ PAL_SLEEP30,          0 },
	{ PAL_SLEEP60,          0 },
	{ PAL_SLEEP_OFF,        0 },

	{ PAL_BANNER3D_TOGGLE,  1 },
	{ PAL_BANNER3D_RESET,   1 },
	{ PAL_ANA3D_TOGGLE,     1 },
	{ PAL_ANA3D_RESET,      1 },
	{ PAL_ROLL3D_TOGGLE,    1 },
	{ PAL_ROLL3D_RESET,     1 },
	{ PAL_PIANO3D_TOGGLE,   1 },
	{ PAL_PIANO3D_RESET,    1 },
	{ PAL_LRC_EXPAND,       1 },
	{ PAL_DESK_LRC,         1 },

	{ PAL_GUIDE,            2 },
	{ PAL_COPY_SHORTCUTS,   2 },
	{ PAL_TAGEDIT,          2 },
	{ PAL_BPM,              2 },
	{ PAL_EXPORT_AB,        2 },
	{ PAL_DUPES,            2 },
	{ PAL_FOLDER_SYNC,      2 },
	{ PAL_SMART,            2 },
	{ PAL_MISSING,          2 },
	{ PAL_RENDER,           2 },
	{ PAL_FOLDER_SET,       2 },

	{ PAL_WIN_EQ,           3 },
	{ PAL_WIN_PIANO,        3 },
	{ PAL_WIN_ANALYZER,     3 },
	{ PAL_WIN_PROTOOLS,     3 },
	{ PAL_WIN_PROMPT,       3 },
	{ PAL_WIN_CMDROLL,      3 },
	{ PAL_WIN_QUEUE,        3 },
	{ PAL_WIN_RECORD,       3 },
	{ PAL_WIN_CAPTURE,      3 },
	{ PAL_WIN_DJPAD,        3 },
	{ PAL_WIN_ALARM,        3 },
	{ PAL_WIN_MIRROR,       3 },
	{ PAL_WIN_REMOTE,       3 },
	{ PAL_WIN_SSVIZ,        3 },
	{ PAL_WIN_PLAYLIST,     3 },
	{ PAL_WIN_OGGHELP,      3 },
	{ PAL_SWITCH_FALCOM,    3 }
};

const int kCmdCount = (int)(sizeof(kCmds) / sizeof(kCmds[0]));

const wchar_t* PalCatName(int cat)
{
	switch (cat) {
	case 0:
		return LL14(L"再生", L"Playback", L"Lecture", L"Riproduzione", L"Reproduccion",
			L"재생", L"播放", L"التشغيل", L"Воспроизведение", L"Wiedergabe",
			L"Reproducao", L"Weergave", L"Odtwarzanie", L"Calma");
	case 1:
		return LL14(L"表示", L"View", L"Affichage", L"Vista", L"Vista",
			L"표시", L"显示", L"العرض", L"Вид", L"Ansicht",
			L"Exibicao", L"Weergave", L"Widok", L"Gorunum");
	case 2:
		return LL14(L"ツール", L"Tools", L"Outils", L"Strumenti", L"Herramientas",
			L"도구", L"工具", L"الأدوات", L"Инструменты", L"Werkzeuge",
			L"Ferramentas", L"Extra", L"Narzedzia", L"Araclar");
	default:
		return LL14(L"ウィンドウ", L"Windows", L"Fenetres", L"Finestre", L"Ventanas",
			L"창", L"窗口", L"النوافذ", L"Окна", L"Fenster",
			L"Janelas", L"Vensters", L"Okna", L"Pencereler");
	}
}

const wchar_t* PalCmdName(int id)
{
	switch (id) {
	case PAL_PLAY:
		return LL14(L"再生", L"Play", L"Lecture", L"Riproduci", L"Reproducir",
			L"재생", L"播放", L"تشغيل", L"Воспроизвести", L"Abspielen",
			L"Reproduzir", L"Afspelen", L"Odtwarzaj", L"Cal");
	case PAL_PAUSE:
		return LL14(L"一時停止 / 再開 [Space]", L"Pause / Resume [Space]", L"Pause / Reprendre [Space]", L"Pausa / Riprendi [Space]", L"Pausa / Reanudar [Space]",
			L"일시정지 / 재개 [Space]", L"暂停 / 继续 [Space]", L"إيقاف مؤقت / متابعة [Space]", L"Пауза / Продолжить [Space]", L"Pause / Fortsetzen [Space]",
			L"Pausar / Retomar [Space]", L"Pauze / Hervatten [Space]", L"Pauza / Wznow [Space]", L"Duraklat / Surdur [Space]");
	case PAL_STOP:
		return LL14(L"停止", L"Stop", L"Arret", L"Stop", L"Detener",
			L"정지", L"停止", L"إيقاف", L"Стоп", L"Stopp",
			L"Parar", L"Stoppen", L"Stop", L"Durdur");
	case PAL_PREV:
		return LL14(L"前の曲", L"Previous track", L"Piste precedente", L"Traccia precedente", L"Pista anterior",
			L"이전 곡", L"上一曲", L"المقطع السابق", L"Предыдущий трек", L"Vorheriger Titel",
			L"Faixa anterior", L"Vorig nummer", L"Poprzedni utwor", L"Onceki parca");
	case PAL_NEXT:
		return LL14(L"次の曲", L"Next track", L"Piste suivante", L"Traccia successiva", L"Pista siguiente",
			L"다음 곡", L"下一曲", L"المقطع التالي", L"Следующий трек", L"Naechster Titel",
			L"Proxima faixa", L"Volgend nummer", L"Nastepny utwor", L"Sonraki parca");
	case PAL_PHRASE_AB:
		return LL14(L"フレーズ A-B [R]", L"Phrase A-B [R]", L"Phrase A-B [R]", L"Frase A-B [R]", L"Frase A-B [R]",
			L"프레이즈 A-B [R]", L"乐句 A-B [R]", L"عبارة A-B [R]", L"Фраза A-B [R]", L"Phrase A-B [R]",
			L"Frase A-B [R]", L"Frase A-B [R]", L"Fraza A-B [R]", L"Cumle A-B [R]");
	case PAL_SLEEP15:
		return LL14(L"スリープタイマー 15分", L"Sleep timer 15 min", L"Minuterie 15 min", L"Timer 15 min", L"Temporizador 15 min",
			L"슬립 타이머 15분", L"睡眠定时 15 分钟", L"مؤقت النوم 15 دقيقة", L"Таймер сна 15 мин", L"Sleeptimer 15 Min",
			L"Timer de sono 15 min", L"Slaaptimer 15 min", L"Wylacznik 15 min", L"Uyku zamanlayici 15 dk");
	case PAL_SLEEP30:
		return LL14(L"スリープタイマー 30分", L"Sleep timer 30 min", L"Minuterie 30 min", L"Timer 30 min", L"Temporizador 30 min",
			L"슬립 타이머 30분", L"睡眠定时 30 分钟", L"مؤقت النوم 30 دقيقة", L"Таймер сна 30 мин", L"Sleeptimer 30 Min",
			L"Timer de sono 30 min", L"Slaaptimer 30 min", L"Wylacznik 30 min", L"Uyku zamanlayici 30 dk");
	case PAL_SLEEP60:
		return LL14(L"スリープタイマー 60分", L"Sleep timer 60 min", L"Minuterie 60 min", L"Timer 60 min", L"Temporizador 60 min",
			L"슬립 타이머 60분", L"睡眠定时 60 分钟", L"مؤقت النوم 60 دقيقة", L"Таймер сна 60 мин", L"Sleeptimer 60 Min",
			L"Timer de sono 60 min", L"Slaaptimer 60 min", L"Wylacznik 60 min", L"Uyku zamanlayici 60 dk");
	case PAL_SLEEP_OFF:
		return LL14(L"スリープタイマー解除", L"Sleep timer off", L"Minuterie desactivee", L"Timer disattivato", L"Temporizador apagado",
			L"슬립 타이머 해제", L"关闭睡眠定时", L"إلغاء مؤقت النوم", L"Отключить таймер сна", L"Sleeptimer aus",
			L"Desligar timer de sono", L"Slaaptimer uit", L"Wylacz wylacznik", L"Uyku zamanlayici kapali");

	case PAL_BANNER3D_TOGGLE:
		return LL14(L"バナー簡易3D 切替", L"Toggle banner Soft 3D", L"Banniere 3D simplifie", L"3D semplificato banner", L"3D simple del banner",
			L"배너 간이 3D 전환", L"横幅简易3D 切换", L"تبديل 3D المبسط للشريط", L"Простой 3D баннера", L"Banner Soft-3D umschalten",
			L"Alternar Soft 3D do banner", L"Banner Soft 3D wisselen", L"Przelacz Soft 3D banera", L"Banner Soft 3B degistir");
	case PAL_BANNER3D_RESET:
		return LL14(L"バナー簡易3D 視点リセット [0]", L"Reset banner Soft 3D view [0]", L"Reinitialiser vue banniere [0]", L"Reimposta vista banner [0]", L"Restablecer vista del banner [0]",
			L"배너 간이 3D 시점 초기화 [0]", L"重置横幅简易3D视角 [0]", L"إعادة ضبط عرض الشريط [0]", L"Сбросить вид баннера [0]", L"Banner-Ansicht zuruecksetzen [0]",
			L"Redefinir vista do banner [0]", L"Bannerweergave resetten [0]", L"Resetuj widok banera [0]", L"Banner gorunumunu sifirla [0]");
	case PAL_ANA3D_TOGGLE:
		return LL14(L"アナライザー簡易3D 切替", L"Toggle analyzer Soft 3D", L"Analyseur 3D simplifie", L"3D semplificato analizzatore", L"3D simple del analizador",
			L"애널라이저 간이 3D 전환", L"分析器简易3D 切换", L"تبديل 3D المبسط للمحلل", L"Простой 3D анализатора", L"Analyzer Soft-3D umschalten",
			L"Alternar Soft 3D do analisador", L"Analyser Soft 3D wisselen", L"Przelacz Soft 3D analizatora", L"Analizor Soft 3B degistir");
	case PAL_ANA3D_RESET:
		return LL14(L"アナライザー簡易3D 視点リセット", L"Reset analyzer Soft 3D view", L"Reinitialiser vue analyseur", L"Reimposta vista analizzatore", L"Restablecer vista del analizador",
			L"애널라이저 간이 3D 시점 초기화", L"重置分析器简易3D视角", L"إعادة ضبط عرض المحلل", L"Сбросить вид анализатора", L"Analyzer-Ansicht zuruecksetzen",
			L"Redefinir vista do analisador", L"Analyserweergave resetten", L"Resetuj widok analizatora", L"Analizor gorunumunu sifirla");
	case PAL_ROLL3D_TOGGLE:
		return LL14(L"コマンドロール簡易3D 切替", L"Toggle command roll Soft 3D", L"Rouleau 3D simplifie", L"3D semplificato command roll", L"3D simple del command roll",
			L"커맨드 롤 간이 3D 전환", L"命令卷轴简易3D 切换", L"تبديل 3D المبسط للفة الأوامر", L"Простой 3D командного ролла", L"Command Roll Soft-3D umschalten",
			L"Alternar Soft 3D do command roll", L"Command roll Soft 3D wisselen", L"Przelacz Soft 3D rolki", L"Komut rulosu Soft 3B degistir");
	case PAL_ROLL3D_RESET:
		return LL14(L"コマンドロール簡易3D 視点リセット", L"Reset command roll Soft 3D view", L"Reinitialiser vue rouleau", L"Reimposta vista command roll", L"Restablecer vista del command roll",
			L"커맨드 롤 간이 3D 시점 초기화", L"重置命令卷轴简易3D视角", L"إعادة ضبط عرض لفة الأوامر", L"Сбросить вид командного ролла", L"Command-Roll-Ansicht zuruecksetzen",
			L"Redefinir vista do command roll", L"Command roll-weergave resetten", L"Resetuj widok rolki", L"Komut rulosu gorunumunu sifirla");
	case PAL_PIANO3D_TOGGLE:
		return LL14(L"ピアノロール簡易3D 切替", L"Toggle piano roll Soft 3D", L"Piano roll 3D simplifie", L"3D semplificato piano roll", L"3D simple del piano roll",
			L"피아노 롤 간이 3D 전환", L"钢琴卷帘简易3D 切换", L"تبديل 3D المبسط للفة البيانو", L"Простой 3D пианоролла", L"Piano-Roll Soft-3D umschalten",
			L"Alternar Soft 3D do piano roll", L"Piano roll Soft 3D wisselen", L"Przelacz Soft 3D piano roll", L"Piano roll Soft 3B degistir");
	case PAL_PIANO3D_RESET:
		return LL14(L"ピアノロール簡易3D 視点リセット", L"Reset piano roll Soft 3D view", L"Reinitialiser vue piano roll", L"Reimposta vista piano roll", L"Restablecer vista del piano roll",
			L"피아노 롤 간이 3D 시점 초기화", L"重置钢琴卷帘简易3D视角", L"إعادة ضبط عرض لفة البيانو", L"Сбросить вид пианоролла", L"Piano-Roll-Ansicht zuruecksetzen",
			L"Redefinir vista do piano roll", L"Piano roll-weergave resetten", L"Resetuj widok piano roll", L"Piano roll gorunumunu sifirla");
	case PAL_LRC_EXPAND:
		return LL14(L"歌詞パネル拡大", L"Expand lyrics panel", L"Agrandir les paroles", L"Espandi testi", L"Expandir letra",
			L"가사 패널 확대", L"扩大歌词面板", L"توسيع لوحة الكلمات", L"Развернуть панель текста", L"Textpanel vergroessern",
			L"Expandir painel de letra", L"Songtekstpaneel vergroten", L"Rozszerz panel tekstu", L"Soz panelini genislet");
	case PAL_DESK_LRC:
		return LL14(L"歌詞ウィンドウ表示", L"Show lyrics window", L"Afficher fenetre paroles", L"Mostra finestra testi", L"Mostrar ventana de letra",
			L"가사 창 표시", L"显示歌词窗口", L"عرض نافذة الكلمات", L"Показать окно текста", L"Textfenster anzeigen",
			L"Mostrar janela de letra", L"Songtekstvenster tonen", L"Pokaz okno tekstu", L"Soz penceresini goster");

	case PAL_GUIDE:
		return LL14(L"操作ガイド [?]", L"Operation guide [?]", L"Guide d'utilisation [?]", L"Guida operativa [?]", L"Guia de operacion [?]",
			L"조작 가이드 [?]", L"操作指南 [?]", L"دليل التشغيل [?]", L"Руководство [?]", L"Bedienungsanleitung [?]",
			L"Guia de operacao [?]", L"Handleiding [?]", L"Przewodnik [?]", L"Islem kilavuzu [?]");
	case PAL_COPY_SHORTCUTS:
		return LL14(L"ショートカット一覧をコピー", L"Copy shortcuts to clipboard", L"Copier les raccourcis", L"Copia le scorciatoie", L"Copiar los atajos",
			L"단축키 목록 복사", L"复制快捷键列表", L"نسخ قائمة الاختصارات", L"Скопировать список горячих клавиш", L"Tastenkuerzel kopieren",
			L"Copiar os atalhos", L"Sneltoetsen kopieren", L"Kopiuj liste skrotow", L"Kisayollari kopyala");
	case PAL_TAGEDIT:
		return LL14(L"タグ編集", L"Edit tags", L"Editer les tags", L"Modifica tag", L"Editar etiquetas",
			L"태그 편집", L"编辑标签", L"تحرير الوسوم", L"Правка тегов", L"Tags bearbeiten",
			L"Editar tags", L"Tags bewerken", L"Edytuj tagi", L"Etiket duzenle");
	case PAL_BPM:
		return LL14(L"BPM 検出", L"Detect BPM", L"Detecter le BPM", L"Rileva BPM", L"Detectar BPM",
			L"BPM 검출", L"检测 BPM", L"كشف BPM", L"Определить BPM", L"BPM erkennen",
			L"Detectar BPM", L"BPM detecteren", L"Wykryj BPM", L"BPM algila");
	case PAL_EXPORT_AB:
		return LL14(L"A-B 区間を WAV 書き出し", L"Export A-B range to WAV", L"Exporter A-B en WAV", L"Esporta A-B in WAV", L"Exportar A-B a WAV",
			L"A-B 구간 WAV 내보내기", L"将 A-B 区间导出为 WAV", L"تصدير مقطع A-B إلى WAV", L"Экспорт участка A-B в WAV", L"A-B-Bereich als WAV",
			L"Exportar trecho A-B para WAV", L"A-B-bereik naar WAV", L"Eksportuj zakres A-B do WAV", L"A-B araligini WAV olarak ver");
	case PAL_DUPES:
		return LL14(L"重複曲をスキャン", L"Scan duplicates", L"Rechercher les doublons", L"Cerca duplicati", L"Buscar duplicados",
			L"중복 곡 검사", L"扫描重复曲目", L"فحص التكرارات", L"Найти дубликаты", L"Duplikate suchen",
			L"Procurar duplicados", L"Duplicaten zoeken", L"Skanuj duplikaty", L"Yinelenenleri tara");
	case PAL_FOLDER_SYNC:
		return LL14(L"フォルダとの差分", L"Folder sync diff", L"Comparer au dossier", L"Differenze con la cartella", L"Diferencias con la carpeta",
			L"폴더와의 차이", L"与文件夹的差异", L"الفروق مع المجلد", L"Различия с папкой", L"Ordnerabgleich",
			L"Diferencas com a pasta", L"Verschillen met map", L"Roznice z folderem", L"Klasor farklari");
	case PAL_SMART:
		return LL14(L"スマートプレイリスト", L"Smart playlists", L"Playlists intelligentes", L"Playlist smart", L"Listas inteligentes",
			L"스마트 재생목록", L"智能播放列表", L"قوائم ذكية", L"Умные списки", L"Smart-Playlists",
			L"Playlists inteligentes", L"Slimme afspeellijsten", L"Inteligentne listy", L"Akilli listeler");
	case PAL_MISSING:
		return LL14(L"欠損を整理", L"Manage missing files", L"Gerer les manquants", L"Gestisci i mancanti", L"Gestionar faltantes",
			L"결손 정리", L"整理缺失文件", L"إدارة الملفات المفقودة", L"Управление отсутствующими", L"Fehlende verwalten",
			L"Gerir ausentes", L"Ontbrekende beheren", L"Zarzadzaj brakujacymi", L"Eksikleri yonet");
	case PAL_RENDER:
		return LL14(L"レンダリング設定", L"Rendering settings", L"Parametres de rendu", L"Impostazioni di rendering", L"Ajustes de renderizado",
			L"렌더링 설정", L"渲染设置", L"إعدادات العرض", L"Настройки рендеринга", L"Rendering-Einstellungen",
			L"Configuracoes de renderizacao", L"Rendering-instellingen", L"Ustawienia renderowania", L"Isleme ayarlari");
	case PAL_FOLDER_SET:
		return LL14(L"フォルダ設定", L"Folder settings", L"Parametres de dossier", L"Impostazioni cartella", L"Ajustes de carpeta",
			L"폴더 설정", L"文件夹设置", L"إعدادات المجلد", L"Настройки папки", L"Ordnereinstellungen",
			L"Configuracoes de pasta", L"Mapinstellingen", L"Ustawienia folderu", L"Klasor ayarlari");

	case PAL_WIN_EQ:
		return LL14(L"イコライザー", L"Equalizer", L"Egaliseur", L"Equalizzatore", L"Ecualizador",
			L"이퀄라이저", L"均衡器", L"المعادل", L"Эквалайзер", L"Equalizer",
			L"Equalizador", L"Equalizer", L"Equalizer", L"Equalizer");
	case PAL_WIN_PIANO:
		return LL14(L"簡易ピアノロール", L"Piano roll", L"Piano roll", L"Piano roll", L"Piano roll",
			L"피아노 롤", L"钢琴卷帘", L"لفة البيانو", L"Пианоролл", L"Piano Roll",
			L"Piano roll", L"Piano roll", L"Piano roll", L"Piano roll");
	case PAL_WIN_ANALYZER:
		return LL14(L"アナライザー", L"Analyzer", L"Analyseur", L"Analizzatore", L"Analizador",
			L"애널라이저", L"分析器", L"المحلل", L"Анализатор", L"Analyzer",
			L"Analisador", L"Analyser", L"Analizator", L"Analizor");
	case PAL_WIN_PROTOOLS:
		return LL14(L"再生詳細", L"Playback details", L"Details de lecture", L"Dettagli riproduzione", L"Detalles de reproduccion",
			L"재생 상세", L"播放详情", L"تفاصيل التشغيل", L"Подробности воспроизведения", L"Wiedergabedetails",
			L"Detalhes de reproducao", L"Afspeeldetails", L"Szczegoly odtwarzania", L"Calma ayrintilari");
	case PAL_WIN_PROMPT:
		return LL14(L"プロンプト", L"Prompt", L"Prompt", L"Prompt", L"Prompt",
			L"프롬프트", L"提示", L"موجه", L"Промпт", L"Prompt",
			L"Prompt", L"Prompt", L"Prompt", L"Istem");
	case PAL_WIN_CMDROLL:
		return LL14(L"コマンドロール", L"Command roll", L"Rouleau de commandes", L"Command roll", L"Command roll",
			L"커맨드 롤", L"命令卷轴", L"لفة الأوامر", L"Командный ролл", L"Command Roll",
			L"Command roll", L"Command roll", L"Rolka komend", L"Komut rulosu");
	case PAL_WIN_QUEUE:
		return LL14(L"Up Next キュー", L"Up Next queue", L"File Up Next", L"Coda Up Next", L"Cola Up Next",
			L"Up Next 큐", L"Up Next 队列", L"طابور Up Next", L"Очередь Up Next", L"Up-Next-Warteschlange",
			L"Fila Up Next", L"Up Next-wachtrij", L"Kolejka Up Next", L"Up Next kuyrugu");
	case PAL_WIN_RECORD:
		return LL14(L"デバイス録音", L"Device recording", L"Enregistrement peripherique", L"Registrazione dispositivo", L"Grabacion de dispositivo",
			L"디바이스 녹음", L"设备录音", L"تسجيل الجهاز", L"Запись с устройства", L"Geraeteaufnahme",
			L"Gravacao de dispositivo", L"Apparaatopname", L"Nagrywanie urzadzenia", L"Aygit kaydi");
	case PAL_WIN_CAPTURE:
		return LL14(L"画面キャプチャ", L"Screen capture", L"Capture d'ecran", L"Cattura schermo", L"Captura de pantalla",
			L"화면 캡처", L"屏幕捕获", L"التقاط الشاشة", L"Захват экрана", L"Bildschirmaufnahme",
			L"Captura de tela", L"Schermopname", L"Przechwytywanie ekranu", L"Ekran yakalama");
	case PAL_WIN_DJPAD:
		return LL14(L"DJ パッド", L"DJ pad", L"Pad DJ", L"Pad DJ", L"Pad DJ",
			L"DJ 패드", L"DJ 垫", L"لوحة DJ", L"DJ-панель", L"DJ-Pad",
			L"Pad DJ", L"DJ-pad", L"Pad DJ", L"DJ paneli");
	case PAL_WIN_ALARM:
		return LL14(L"アラーム", L"Alarm", L"Alarme", L"Sveglia", L"Alarma",
			L"알람", L"闹钟", L"منبه", L"Будильник", L"Wecker",
			L"Alarme", L"Wekker", L"Budzik", L"Alarm");
	case PAL_WIN_MIRROR:
		return LL14(L"ミラー", L"Mirror", L"Miroir", L"Mirror", L"Espejo",
			L"미러", L"镜像", L"مرآة", L"Зеркало", L"Spiegel",
			L"Espelho", L"Spiegel", L"Lustro", L"Ayna");
	case PAL_WIN_REMOTE:
		return LL14(L"リモート", L"Remote", L"Remote", L"Remote", L"Remoto",
			L"리모트", L"遥控", L"تحكم", L"Пульт", L"Remote",
			L"Remoto", L"Remote", L"Pilot", L"Uzaktan");
	case PAL_WIN_SSVIZ:
		return LL14(L"SS ビジュアライザ", L"SS visualizer", L"Visualiseur SS", L"Visualizzatore SS", L"Visualizador SS",
			L"SS 비주얼라이저", L"SS 可视化", L"عارض SS", L"SS-визуализатор", L"SS-Visualizer",
			L"Visualizador SS", L"SS-visualizer", L"Wizualizator SS", L"SS gorselleyici");
	case PAL_WIN_PLAYLIST:
		return LL14(L"プレイリスト画面", L"Playlist window", L"Fenetre playlist", L"Finestra playlist", L"Ventana de lista",
			L"재생목록 창", L"播放列表窗口", L"نافذة قائمة التشغيل", L"Окно плейлиста", L"Playlist-Fenster",
			L"Janela de playlist", L"Afspeellijstvenster", L"Okno listy", L"Liste penceresi");
	case PAL_WIN_OGGHELP:
		return LL14(L"ファルコム画面の操作ガイド", L"Falcom view operation guide", L"Guide de la vue Falcom", L"Guida della vista Falcom", L"Guia de la vista Falcom",
			L"팔콤 화면 조작 가이드", L"Falcom 界面操作指南", L"دليل تشغيل شاشة Falcom", L"Руководство по экрану Falcom", L"Falcom-Ansicht Anleitung",
			L"Guia da tela Falcom", L"Handleiding Falcom-scherm", L"Przewodnik ekranu Falcom", L"Falcom ekrani kilavuzu");
	case PAL_SWITCH_FALCOM:
		return LL14(L"ファルコム特化型画面へ切替", L"Switch to Falcom view", L"Basculer vers la vue Falcom", L"Passa alla vista Falcom", L"Cambiar a la vista Falcom",
			L"팔콤 특화 화면으로 전환", L"切换到 Falcom 专用界面", L"التبديل إلى شاشة Falcom", L"Переключиться на экран Falcom", L"Zur Falcom-Ansicht wechseln",
			L"Mudar para a tela Falcom", L"Naar Falcom-weergave", L"Przelacz na ekran Falcom", L"Falcom ekranina gec");
	default:
		return L"";
	}
}

// ショートカット一覧(コピー用)。パレット・操作ガイドと同じ内容を1行1件で並べる。
CString PalBuildShortcutText()
{
	CString s;
	s += LL14(L"メディアプレイヤー ショートカット一覧", L"Media Player shortcuts", L"Raccourcis du lecteur", L"Scorciatoie del player", L"Atajos del reproductor",
		L"미디어 플레이어 단축키", L"媒体播放器快捷键", L"اختصارات مشغل الوسائط", L"Горячие клавиши плеера", L"Media-Player-Tastenkuerzel",
		L"Atalhos do player", L"Sneltoetsen mediaspeler", L"Skroty odtwarzacza", L"Medya oynatici kisayollari");
	s += L"\r\n";
	s += L"Ctrl+K\t";
	s += LL14(L"コマンドパレット", L"Command palette", L"Palette de commandes", L"Palette comandi", L"Paleta de comandos",
		L"명령 팔레트", L"命令面板", L"لوحة الأوامر", L"Палитра команд", L"Befehlspalette",
		L"Paleta de comandos", L"Opdrachtpalet", L"Paleta polecen", L"Komut paleti");
	s += L"\r\n?\t";
	s += LL14(L"操作ガイド", L"Operation guide", L"Guide d'utilisation", L"Guida operativa", L"Guia de operacion",
		L"조작 가이드", L"操作指南", L"دليل التشغيل", L"Руководство", L"Bedienungsanleitung",
		L"Guia de operacao", L"Handleiding", L"Przewodnik", L"Islem kilavuzu");
	s += L"\r\nSpace\t";
	s += LL14(L"再生 / 一時停止", L"Play / Pause", L"Lecture / Pause", L"Riproduci / Pausa", L"Reproducir / Pausa",
		L"재생 / 일시정지", L"播放 / 暂停", L"تشغيل / إيقاف مؤقت", L"Воспроизведение / Пауза", L"Wiedergabe / Pause",
		L"Reproduzir / Pausar", L"Afspelen / Pauze", L"Odtwarzaj / Pauza", L"Cal / Duraklat");
	s += L"\r\nR\t";
	s += LL14(L"現在位置を中心にフレーズ A-B", L"Set phrase A-B around now", L"Phrase A-B autour de maintenant", L"Frase A-B attorno ad ora", L"Frase A-B alrededor de ahora",
		L"현재 위치 기준 프레이즈 A-B", L"以当前位置设乐句 A-B", L"عبارة A-B حول الموضع الحالي", L"Фраза A-B вокруг текущей позиции", L"Phrase A-B um Jetzt",
		L"Frase A-B em torno de agora", L"Frase A-B rond nu", L"Fraza A-B wokol teraz", L"Su anin etrafinda cumle A-B");
	s += L"\r\n1-8\t";
	s += LL14(L"キューへジャンプ", L"Jump to cue", L"Aller au cue", L"Vai al cue", L"Ir al cue",
		L"큐로 점프", L"跳转到标记", L"الانتقال إلى الإشارة", L"Перейти к метке", L"Zu Cue springen",
		L"Ir para o cue", L"Naar cue", L"Skocz do cue", L"Cue'ya atla");
	s += L"\r\n0\t";
	s += LL14(L"簡易3D の視点リセット", L"Reset Soft 3D view", L"Reinitialiser la vue 3D", L"Reimposta vista Soft 3D", L"Restablecer vista Soft 3D",
		L"간이 3D 시점 초기화", L"重置简易3D视角", L"إعادة ضبط عرض 3D المبسط", L"Сбросить вид Soft 3D", L"Soft-3D-Ansicht zuruecksetzen",
		L"Redefinir vista Soft 3D", L"Soft 3D-weergave resetten", L"Resetuj widok Soft 3D", L"Soft 3B gorunumunu sifirla");
	s += L"\r\nEnter\t";
	s += LL14(L"検索欄では次の候補へ", L"Next match in the search box", L"Occurrence suivante", L"Prossima corrispondenza", L"Siguiente coincidencia",
		L"검색란에서 다음 후보", L"搜索框中跳到下一个", L"التالي في مربع البحث", L"Следующее совпадение в поиске", L"Naechster Treffer im Suchfeld",
		L"Proxima correspondencia", L"Volgende overeenkomst", L"Nastepne dopasowanie", L"Arama kutusunda sonraki");
	s += L"\r\n";
	return s;
}

void PalCopyToClipboard(CWnd* owner, const CString& text)
{
	if (!owner || !owner->GetSafeHwnd()) return;
	if (!::OpenClipboard(owner->GetSafeHwnd())) return;
	::EmptyClipboard();
	const size_t bytes = (size_t)(text.GetLength() + 1) * sizeof(wchar_t);
	HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
	if (h) {
		wchar_t* buf = (wchar_t*)::GlobalLock(h);
		if (buf) {
			memcpy(buf, (LPCWSTR)text, bytes);
			::GlobalUnlock(h);
			::SetClipboardData(CF_UNICODETEXT, h);
		}
		else {
			::GlobalFree(h);
		}
	}
	::CloseClipboard();
}

// mp / og の既存ハンドラへ WM_COMMAND を中継する。lParam=0 なので
// ON_COMMAND / ON_BN_CLICKED どちらの登録でも同じように届く。
void PalPostToMp(UINT cmd)
{
	if (mp && ::IsWindow(mp->GetSafeHwnd()))
		mp->PostMessage(WM_COMMAND, cmd, 0);
}

void PalPostToOg(UINT cmd)
{
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->PostMessage(WM_COMMAND, cmd, 0);
}

// 簡易3D 系はコンテキストメニューの Track() 戻り値でしか処理されていないため、
// savedata を直接書いて各窓へ同期させる。窓が閉じていれば savedata だけ残す。
void PalEnsureAnalyzerOpen()
{
	if (savedata.analyzerwindow != 1)
		PalPostToMp(ID_MP_OPEN_ANALYZER);
}

void PalEnsurePianoOpen()
{
	if (savedata.pianorollwindow != 1)
		PalPostToMp(ID_MP_OPEN_PIANOROLL);
}

// 簡易ピアノロール窓(未生成なら nullptr)。ID を投げるだけなので CWnd で扱う。
CWnd* PalPianoWnd()
{
	if (!og || !og->m_PianoRollDlg) return nullptr;
	CWnd* w = (CWnd*)og->m_PianoRollDlg;
	return ::IsWindow(w->GetSafeHwnd()) ? w : nullptr;
}

} // namespace

IMPLEMENT_DYNAMIC(CMpCommandPaletteDlg, CCustomBlurDialogExBase)

// モードレスシングルトン
static CMpCommandPaletteDlg* g_pal = nullptr;

CMpCommandPaletteDlg::CMpCommandPaletteDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(CMpCommandPaletteDlg::IDD, pParent)
{
}

CMpCommandPaletteDlg::~CMpCommandPaletteDlg()
{
}

void CMpCommandPaletteDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_CMDPAL_FILTER, m_filter);
	DDX_Control(pDX, IDC_CMDPAL_LIST, m_list);
	DDX_Control(pDX, IDC_CMDPAL_RUN, m_run);
	DDX_Control(pDX, IDC_CMDPAL_CLOSE, m_close);
}

BEGIN_MESSAGE_MAP(CMpCommandPaletteDlg, CCustomBlurDialogExBase)
	ON_EN_CHANGE(IDC_CMDPAL_FILTER, &CMpCommandPaletteDlg::OnFilterChange)
	ON_BN_CLICKED(IDC_CMDPAL_RUN, &CMpCommandPaletteDlg::OnBnClickedRun)
	ON_BN_CLICKED(IDC_CMDPAL_CLOSE, &CMpCommandPaletteDlg::OnBnClickedClose)
	ON_NOTIFY(NM_DBLCLK, IDC_CMDPAL_LIST, &CMpCommandPaletteDlg::OnDblclkList)
	ON_WM_CLOSE()
	ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

BOOL CMpCommandPaletteDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();

	SetWindowText(LL14(
		L"コマンドパレット", L"Command Palette", L"Palette de commandes", L"Palette comandi", L"Paleta de comandos",
		L"명령 팔레트", L"命令面板", L"لوحة الأوامر", L"Палитра команд", L"Befehlspalette",
		L"Paleta de comandos", L"Opdrachtpalet", L"Paleta polecen", L"Komut paleti"));
	SetDlgItemText(IDC_CMDPAL_RUN, LL14(
		L"実行", L"Run", L"Executer", L"Esegui", L"Ejecutar",
		L"실행", L"执行", L"تشغيل", L"Запуск", L"Ausfuehren",
		L"Executar", L"Uitvoeren", L"Uruchom", L"Calistir"));
	SetDlgItemText(IDC_CMDPAL_CLOSE, LL14(
		L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar",
		L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schliessen",
		L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));

	m_run.SetAeroMode(FALSE);
	m_close.SetAeroMode(FALSE);
	m_list.SetAeroMode(FALSE);
	m_run.SetGradation(RGB(210, 240, 215), RGB(150, 210, 165), 0, TRUE);
	m_close.SetGradation(RGB(235, 235, 240), RGB(200, 200, 210), 0, TRUE);

	m_list.SetExtendedStyle(m_list.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
	m_list.InsertColumn(0, L"", LVCFMT_LEFT, 92, 0);
	m_list.InsertColumn(1, L"", LVCFMT_LEFT, 300, 0);

	CCustomControlUtility::BeginDialogToolTip(m_tooltip, this, TTS_NOPREFIX);
	m_tooltip.AddTool(&m_filter, LL14(
		L"入力するとコマンドを絞り込みます。Enter で実行、Esc で閉じます。",
		L"Type to filter commands. Enter runs, Esc closes.",
		L"Tapez pour filtrer. Entree execute, Echap ferme.",
		L"Digita per filtrare. Invio esegue, Esc chiude.",
		L"Escriba para filtrar. Enter ejecuta, Esc cierra.",
		L"입력하면 명령을 좁힙니다. Enter 실행, Esc 닫기.",
		L"输入以筛选命令。Enter 执行，Esc 关闭。",
		L"اكتب لتصفية الأوامر. Enter للتشغيل و Esc للإغلاق.",
		L"Введите текст для фильтра. Enter — выполнить, Esc — закрыть.",
		L"Tippen zum Filtern. Enter fuehrt aus, Esc schliesst.",
		L"Digite para filtrar. Enter executa, Esc fecha.",
		L"Typ om te filteren. Enter voert uit, Esc sluit.",
		L"Wpisz, aby filtrowac. Enter uruchamia, Esc zamyka.",
		L"Filtrelemek icin yazin. Enter calistirir, Esc kapatir."));
	m_tooltip.AddTool(&m_run, LL14(
		L"選択したコマンドを実行します。",
		L"Run the selected command.",
		L"Executer la commande selectionnee.",
		L"Esegui il comando selezionato.",
		L"Ejecutar el comando seleccionado.",
		L"선택한 명령을 실행합니다.",
		L"执行所选命令。",
		L"تشغيل الأمر المحدد.",
		L"Выполнить выбранную команду.",
		L"Ausgewaehlten Befehl ausfuehren.",
		L"Executar o comando selecionado.",
		L"Geselecteerde opdracht uitvoeren.",
		L"Uruchom wybrane polecenie.",
		L"Secili komutu calistir."));
	m_tooltip.AddTool(&m_close, LL14(
		L"パレットを閉じます。",
		L"Close the palette.",
		L"Fermer la palette.",
		L"Chiudi la palette.",
		L"Cerrar la paleta.",
		L"팔레트를 닫습니다.",
		L"关闭命令面板。",
		L"إغلاق اللوحة.",
		L"Закрыть палитру.",
		L"Palette schliessen.",
		L"Fechar a paleta.",
		L"Palet sluiten.",
		L"Zamknij palete.",
		L"Paleti kapat."));
	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 360, 8000);

	RebuildList();
	m_filter.SetFocus();
	return FALSE; // m_filter に置いたフォーカスを保持する
}

void CMpCommandPaletteDlg::PostNcDestroy()
{
	CCustomBlurDialogExBase::PostNcDestroy();
	if (g_pal == this)
		g_pal = nullptr;
	delete this;
}

void CMpCommandPaletteDlg::OnOK()
{
	RunSelected();
}

void CMpCommandPaletteDlg::OnCancel()
{
	DestroyWindow();
}

void CMpCommandPaletteDlg::OnClose()
{
	DestroyWindow();
}

void CMpCommandPaletteDlg::OnBnClickedClose()
{
	DestroyWindow();
}

void CMpCommandPaletteDlg::OnBnClickedRun()
{
	RunSelected();
}

void CMpCommandPaletteDlg::OnDblclkList(NMHDR* pNMHDR, LRESULT* pResult)
{
	UNREFERENCED_PARAMETER(pNMHDR);
	if (pResult) *pResult = 0;
	RunSelected();
}

void CMpCommandPaletteDlg::OnFilterChange()
{
	RebuildList();
}

BOOL CMpCommandPaletteDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	if (pMsg->message == WM_KEYDOWN) {
		if (pMsg->wParam == VK_ESCAPE) {
			DestroyWindow();
			return TRUE;
		}
		if (pMsg->wParam == VK_RETURN) {
			RunSelected();
			return TRUE;
		}
		// フィルタ欄にフォーカスがあっても上下でリスト選択を動かせるようにする
		if ((pMsg->wParam == VK_UP || pMsg->wParam == VK_DOWN)
			&& pMsg->hwnd == m_filter.GetSafeHwnd() && m_list.GetSafeHwnd()) {
			const int n = m_list.GetItemCount();
			if (n > 0) {
				int cur = GetSelectedRow();
				cur += (pMsg->wParam == VK_DOWN) ? 1 : -1;
				if (cur < 0) cur = 0;
				if (cur > n - 1) cur = n - 1;
				m_list.SetItemState(cur, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
				m_list.EnsureVisible(cur, FALSE);
				return TRUE;
			}
		}
	}
	return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}

void CMpCommandPaletteDlg::RebuildList()
{
	wchar_t needle[256] = {};
	if (m_filter.GetSafeHwnd()) {
		CString f;
		m_filter.GetWindowText(f);
		f.Trim();
		wcsncpy_s(needle, (LPCWSTR)f, _TRUNCATE);
		::CharUpperBuffW(needle, (DWORD)wcslen(needle));
	}
	const BOOL filtering = (needle[0] != L'\0');

	m_list.SetRedraw(FALSE);
	m_list.DeleteAllItems();
	int row = 0;
	for (int i = 0; i < kCmdCount; ++i) {
		const wchar_t* cat = PalCatName(kCmds[i].cat);
		const wchar_t* name = PalCmdName(kCmds[i].id);
		if (filtering) {
			wchar_t hay[256] = {};
			_snwprintf_s(hay, _TRUNCATE, L"%s %s", cat, name);
			::CharUpperBuffW(hay, (DWORD)wcslen(hay));
			if (!wcsstr(hay, needle))
				continue;
		}
		m_list.InsertItem(row, cat);
		m_list.SetItemText(row, 1, name);
		m_list.SetItemData(row, (DWORD_PTR)kCmds[i].id);
		++row;
	}
	m_list.SetRedraw(TRUE);
	m_list.Invalidate();
	if (row > 0)
		m_list.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

int CMpCommandPaletteDlg::GetSelectedRow() const
{
	POSITION pos = m_list.GetFirstSelectedItemPosition();
	if (!pos) return -1;
	return m_list.GetNextSelectedItem(pos);
}

int CMpCommandPaletteDlg::GetSelectedCmdId() const
{
	const int row = GetSelectedRow();
	if (row < 0) return 0;
	return (int)m_list.GetItemData(row);
}

void CMpCommandPaletteDlg::RunSelected()
{
	const int id = GetSelectedCmdId();
	if (!id) return;
	// クリップボードコピー以外は実行前に閉じる。og 側のモーダル(フォルダ設定/
	// レンダリング等)を開くコマンドがあり、パレットを残すと入力を奪い合う。
	const BOOL keepOpen = (id == PAL_COPY_SHORTCUTS);
	ExecCommand(id);
	if (!keepOpen)
		DestroyWindow();
}

void CMpCommandPaletteDlg::ExecCommand(int id)
{
	switch (id) {
	// ---- 再生 ----
	case PAL_PLAY:        PalPostToMp(IDC_MP_PLAY); return;
	case PAL_PAUSE:       PalPostToMp(IDC_MP_PAUSE); return;
	case PAL_STOP:        PalPostToMp(IDC_MP_STOP); return;
	case PAL_PREV:        PalPostToMp(IDC_MP_PREV); return;
	case PAL_NEXT:        PalPostToMp(IDC_MP_NEXT); return;
	case PAL_PHRASE_AB:   PalPostToMp(ID_MP_PHRASE_AB); return;
	case PAL_SLEEP15:     PalPostToMp(ID_MP_SLEEP_15); return;
	case PAL_SLEEP30:     PalPostToMp(ID_MP_SLEEP_30); return;
	case PAL_SLEEP60:     PalPostToMp(ID_MP_SLEEP_60); return;
	case PAL_SLEEP_OFF:   PalPostToMp(ID_MP_SLEEP_OFF); return;

	// ---- 表示 ----
	case PAL_BANNER3D_TOGGLE:
		savedata.mpBannerviewmode = (savedata.mpBannerviewmode == 1) ? 0 : 1;
		if (mp && ::IsWindow(mp->GetSafeHwnd())) {
			mp->SyncBannerSoft3DCamFromSave();
			mp->Invalidate(FALSE);
		}
		MpPersistSavedataQuick();
		return;
	case PAL_BANNER3D_RESET:
		savedata.mpBanner3dyaw = -220;
		savedata.mpBanner3dpitch = 260;
		savedata.mpBanner3dzoom = 100;
		if (mp && ::IsWindow(mp->GetSafeHwnd())) {
			mp->SyncBannerSoft3DCamFromSave();
			mp->Invalidate(FALSE);
		}
		MpPersistSavedataQuick();
		return;
	case PAL_ANA3D_TOGGLE: {
		const int on = (savedata.analyzerviewmodeTop == 1 && savedata.analyzerviewmodeBot == 1) ? 0 : 1;
		savedata.analyzerviewmodeTop = on;
		savedata.analyzerviewmodeBot = on;
		PalEnsureAnalyzerOpen();
		if (og && og->m_AnalyzerDlg && ::IsWindow(og->m_AnalyzerDlg->GetSafeHwnd()))
			og->m_AnalyzerDlg->PaletteApplySoft3D();
		MpPersistSavedataQuick();
		return;
	}
	case PAL_ANA3D_RESET:
		savedata.analyzer3dyawTop = -220;
		savedata.analyzer3dpitchTop = 260;
		savedata.analyzer3dzoomTop = 100;
		savedata.analyzer3dyawBot = -220;
		savedata.analyzer3dpitchBot = 260;
		savedata.analyzer3dzoomBot = 100;
		if (og && og->m_AnalyzerDlg && ::IsWindow(og->m_AnalyzerDlg->GetSafeHwnd()))
			og->m_AnalyzerDlg->PaletteApplySoft3D();
		MpPersistSavedataQuick();
		return;
	case PAL_ROLL3D_TOGGLE:
		savedata.mpCmdRollviewmode = (savedata.mpCmdRollviewmode == 1) ? 0 : 1;
		if (!MpIsCommandRollOpen())
			PalPostToMp(IDC_MP_CMDROLL);
		else if (CCommandRollDlg* r = MpCommandRollDlgInstance())
			r->PaletteApplySoft3D();
		MpPersistSavedataQuick();
		return;
	case PAL_ROLL3D_RESET:
		savedata.mpCmdRoll3dyaw = -220;
		savedata.mpCmdRoll3dpitch = 260;
		savedata.mpCmdRoll3dzoom = 100;
		if (CCommandRollDlg* r = MpCommandRollDlgInstance())
			r->PaletteApplySoft3D();
		MpPersistSavedataQuick();
		return;
	case PAL_PIANO3D_TOGGLE: {
		const int on = (savedata.pianorollviewmode == 1) ? 0 : 1;
		savedata.pianorollviewmode = on;
		PalEnsurePianoOpen();
		if (CWnd* w = PalPianoWnd())
			w->PostMessage(WM_COMMAND, kPianoViewBase + (UINT)on, 0);
		MpPersistSavedataQuick();
		return;
	}
	case PAL_PIANO3D_RESET:
		if (CWnd* w = PalPianoWnd())
			w->PostMessage(WM_COMMAND, kPianoCamReset, 0);
		else {
			savedata.pianoroll3dyaw = -220;
			savedata.pianoroll3dpitch = 260;
			savedata.pianoroll3dzoom = 100;
			MpPersistSavedataQuick();
		}
		return;
	case PAL_LRC_EXPAND:  PalPostToMp(ID_MP_LRC_EXPAND); return;
	case PAL_DESK_LRC:    PalPostToMp(ID_MP_DESK_LRC); return;

	// ---- ツール ----
	case PAL_GUIDE:       PalPostToMp(ID_HELP_SHOWSHEET); return;
	case PAL_COPY_SHORTCUTS:
		PalCopyToClipboard(this, PalBuildShortcutText());
		return;
	case PAL_TAGEDIT:     PalPostToMp(ID_MP_TAG_EDIT); return;
	case PAL_BPM:         PalPostToMp(ID_MP_BPM_DETECT); return;
	case PAL_EXPORT_AB:   PalPostToMp(ID_MP_EXPORT_AB); return;
	case PAL_DUPES:       PalPostToMp(ID_MP_DUPES); return;
	case PAL_FOLDER_SYNC: PalPostToMp(ID_MP_FOLDER_SYNC); return;
	case PAL_SMART:       PalPostToMp(ID_MP_SMART_EDIT); return;
	case PAL_MISSING:     PalPostToMp(ID_MP_MISS_MANAGE); return;
	case PAL_RENDER:      PalPostToOg(IDC_BUTTON21); return;
	case PAL_FOLDER_SET:  PalPostToMp(IDC_MP_FOLDER); return;

	// ---- ウィンドウ ----
	case PAL_WIN_EQ:        PalPostToMp(ID_MP_OPEN_EQ); return;
	case PAL_WIN_PIANO:     PalPostToMp(ID_MP_OPEN_PIANOROLL); return;
	case PAL_WIN_ANALYZER:  PalPostToMp(ID_MP_OPEN_ANALYZER); return;
	case PAL_WIN_PROTOOLS:  PalPostToMp(ID_MP_OPEN_PROTOOLS); return;
	case PAL_WIN_PROMPT:    PalPostToMp(IDC_MP_PROMPT); return;
	case PAL_WIN_CMDROLL:   PalPostToMp(IDC_MP_CMDROLL); return;
	case PAL_WIN_QUEUE:     PalPostToMp(ID_MP_QUEUE_SHOW); return;
	case PAL_WIN_RECORD:    PalPostToMp(IDC_MP_RECORD); return;
	case PAL_WIN_CAPTURE:   PalPostToMp(IDC_MP_CAPTURE); return;
	case PAL_WIN_DJPAD:     PalPostToMp(ID_MP_DJPAD); return;
	case PAL_WIN_ALARM:     PalPostToMp(ID_MP_ALARM); return;
	case PAL_WIN_MIRROR:    PalPostToMp(ID_MP_MIRROR); return;
	case PAL_WIN_REMOTE:    PalPostToMp(ID_MP_REMOTE); return;
	case PAL_WIN_SSVIZ:     PalPostToMp(ID_MP_SSVIZ); return;
	case PAL_WIN_PLAYLIST:  PalPostToOg(IDC_BUTTON57); return;
	case PAL_WIN_OGGHELP:   PalPostToOg(IDC_OGG_HELP); return;
	case PAL_SWITCH_FALCOM: PalPostToMp(IDC_MP_SWITCHMODE); return;
	default: return;
	}
}

HBRUSH CMpCommandPaletteDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	HBRUSH hbr = CCustomBlurDialogExBase::OnCtlColor(pDC, pWnd, nCtlColor);
	if (savedata.aero != 1) {
		if (m_brDlg.GetSafeHandle() == NULL)
			m_brDlg.CreateSolidBrush(COLOR_DIALOG_BG);
		if (nCtlColor == CTLCOLOR_DLG || nCtlColor == CTLCOLOR_STATIC)
			return m_brDlg;
	}
	return hbr;
}

void CMpCommandPaletteDlg::OpenPalette(CWnd* owner)
{
	if (g_pal && !::IsWindow(g_pal->GetSafeHwnd()))
		g_pal = nullptr;
	if (g_pal) {
		g_pal->ShowWindow(SW_SHOW);
		g_pal->SetForegroundWindow();
		if (g_pal->m_filter.GetSafeHwnd()) {
			g_pal->m_filter.SetSel(0, -1);
			g_pal->m_filter.SetFocus();
		}
		return;
	}
	CMpCommandPaletteDlg* dlg = new CMpCommandPaletteDlg(owner);
	if (!dlg->Create(IDD_MP_CMDPAL, owner)) {
		delete dlg;
		return;
	}
	g_pal = dlg;
	dlg->CenterWindow(owner);
	dlg->ShowWindow(SW_SHOW);
	dlg->SetForegroundWindow();
}

void CMpCommandPaletteDlg::CloseIfOpen()
{
	if (g_pal && ::IsWindow(g_pal->GetSafeHwnd()))
		g_pal->DestroyWindow();
	g_pal = nullptr;
}

void MpOpenCommandPalette(CWnd* owner)
{
	CMpCommandPaletteDlg::OpenPalette(owner);
}
