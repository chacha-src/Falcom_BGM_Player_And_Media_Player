#include "stdafx.h"
#include "CPromptDlg.h"
#include "CPromptEngine.h"

extern save savedata;
extern void MpPersistSavedataQuick();
static CPromptDlg* g_promptDlg = nullptr;
static BOOL g_histSelChanging = FALSE;

IMPLEMENT_DYNAMIC(CPromptDlg, CCustomBlurDialogExBase)

CPromptDlg::CPromptDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(CPromptDlg::IDD, pParent)
{
}

CPromptDlg::~CPromptDlg()
{
	if (g_promptDlg == this)
		g_promptDlg = nullptr;
}

void CPromptDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_MPP_TEXT, m_edit);
	DDX_Control(pDX, IDC_MPP_LEGEND, m_legend);
	DDX_Control(pDX, IDC_MPP_RUN, m_run);
	DDX_Control(pDX, IDC_MPP_STOP, m_stop);
	DDX_Control(pDX, IDC_MPP_RESET, m_reset);
	DDX_Control(pDX, IDC_MPP_CLEAR, m_clear);
	DDX_Control(pDX, IDC_MPP_CLOSE, m_close);
	DDX_Control(pDX, IDC_MPP_HIST, m_hist);
	DDX_Control(pDX, IDC_MPP_SAVEHIST, m_saveHist);
}

BEGIN_MESSAGE_MAP(CPromptDlg, CCustomBlurDialogExBase)
	ON_BN_CLICKED(IDC_MPP_RUN, &CPromptDlg::OnRun)
	ON_BN_CLICKED(IDC_MPP_STOP, &CPromptDlg::OnStop)
	ON_BN_CLICKED(IDC_MPP_RESET, &CPromptDlg::OnReset)
	ON_BN_CLICKED(IDC_MPP_CLEAR, &CPromptDlg::OnClear)
	ON_BN_CLICKED(IDC_MPP_CLOSE, &CPromptDlg::OnCloseBtn)
	ON_BN_CLICKED(IDC_MPP_SAVEHIST, &CPromptDlg::OnSaveHist)
	ON_CBN_SELCHANGE(IDC_MPP_HIST, &CPromptDlg::OnHistSel)
	ON_EN_CHANGE(IDC_MPP_TEXT, &CPromptDlg::OnTextChanged)
	ON_WM_SIZE()
	ON_WM_ENTERSIZEMOVE()
	ON_WM_EXITSIZEMOVE()
	ON_WM_MOVING()
	ON_WM_GETMINMAXINFO()
	ON_WM_MOUSEWHEEL()
	ON_WM_CTLCOLOR()
#if CCUSTOM_AERO_SUPPORT
	ON_MESSAGE(CCC_MSG_REAPPLY_OPAQUE_FIXERS, OnReapplyOpaqueFixers)
#endif
END_MESSAGE_MAP()

static CString MpPromptLegendText()
{
	return LL14(
		L"━━ 使い方 ━━\r\n"
		L"1. 上の入力欄にコマンドを書く (複数行可、1行に複数 @ も可)\r\n"
		L"2. [実行] で解析・有効化 → 演奏中、時刻になると自動適用\r\n"
		L"3. [停止]=適用停止(値維持)  [リセット]=実行前に戻す  [クリア]=本文消去\r\n"
		L"※ 時刻はメディアプレイヤー・バナー(GDI)の時計と同じ基準です\r\n"
		L"\r\n"
		L"【形式】 @<cmd><時刻>[-<終了時刻>][<値>[-<終了値>]]\r\n"
		L"【時刻】 秒(50) または 分:秒(1:20)。例: 1:20 = 80秒\r\n"
		L"【値】 0〜200 (100=原曲)。[]内2値 = 開始〜終了を線形補間\r\n"
		L"【基本】 p=ピッチ  t=テンポ  d=DirectSound音量\r\n"
		L"【EQ周波数帯】(小文字 a-o = イコライザー15帯)\r\n"
		L"  a=25 b=40 c=63 d=100 e=160 f=250 g=400 h=630 i=1k\r\n"
		L"  j=1.6k k=2.5k l=4k m=6.3k n=10k o=16k (Hz)\r\n"
		L"【EQ項目】(大文字 M/N/K/I/S)\r\n"
		L"  M=マスター N=鮮明 K=高低(バランス) I=密度 S=立体\r\n"
		L"  (互換: 小文字 s も立体。sb/sl 演出は2文字)\r\n"
		L"【効果】 r=リバーブ  c=コーラス  y=ディレイ\r\n"
		L"【演出】 sb=しょんぼり  br=明るめ  sl=スロー  fa=ファスト (値不要)\r\n"
		L"【例1】 @p50-1:20[100-120]  … 50秒〜1:20でピッチ100→120%\r\n"
		L"【例2】 @p1:50[100]  @d2:00[80]  @sb1:30  @br2:00\r\n"
		L"【例3】 @t0-30[100-80]  … 曲頭から30秒かけてテンポ100→80%",
		L"[Format] @<cmd><time>[-<end>][<val>[-<endVal>]]\r\n"
		L"[Time] sec(50) or min:sec(1:20). Same as GDI banner clock.\r\n"
		L"[Value] 0-200 (100=original). Two values in [] = linear ramp.\r\n"
		L"[Basic] p=pitch t=tempo d=DirectSound volume\r\n"
		L"[EQ Hz bands] lowercase a-o: a=25..h=630 i=1k j=1.6k k=2.5k l=4k m=6.3k n=10k o=16k\r\n"
		L"[EQ controls] uppercase M/N/K/I/S: master/clarity/balance/density/stereo\r\n"
		L"[FX] r=reverb c=chorus y=delay\r\n"
		L"[Presets] sb=melancholy br=bright sl=slow fa=fast (no value)\r\n"
		L"[Ex1] @p50-1:20[100-120]  [Ex2] @p1:50[100] @d2:00[80] @sb1:30",
		L"[Format] @<cmd><heure>[-<fin>][<val>[-<finVal>]]\r\n"
		L"[Heure] sec(50) ou min:sec(1:20). Meme base que l'horloge GDI.\r\n"
		L"[Valeur] 0-200 (100=original). Deux valeurs [] = rampe lineaire.\r\n"
		L"[Base] p=hauteur t=tempo d=volume DS\r\n"
		L"[EQ] a-o  m=master n=clarte k=tonalite i=densite s=stereo\r\n"
		L"[FX] r=reverb c=chorus y=delay  [Presets] sb/br/sl/fa\r\n"
		L"[Ex] @p50-1:20[100-120]  @p1:50[100]  @sb1:30  @br2:00",
		L"[Formato] @<cmd><tempo>[-<fine>][<val>[-<fineVal>]]\r\n"
		L"[Tempo] sec(50) o min:sec(1:20). Come l'orologio GDI del banner.\r\n"
		L"[Valore] 0-200 (100=originale). Due valori [] = rampa lineare.\r\n"
		L"[Base] p=intonazione t=tempo d=volume DS\r\n"
		L"[EQ] a-o  m=master n=chiarezza k=tono i=densita s=stereo\r\n"
		L"[FX] r=reverb c=chorus y=delay  [Preset] sb/br/sl/fa\r\n"
		L"[Es] @p50-1:20[100-120]  @p1:50[100]  @sb1:30  @br2:00",
		L"[Formato] @<cmd><tiempo>[-<fin>][<val>[-<finVal>]]\r\n"
		L"[Tiempo] seg(50) o min:seg(1:20). Igual que el reloj GDI del banner.\r\n"
		L"[Valor] 0-200 (100=original). Dos valores [] = rampa lineal.\r\n"
		L"[Base] p=tono t=tempo d=volumen DS\r\n"
		L"[EQ] a-o  m=master n=claridad k=tono i=densidad s=estereo\r\n"
		L"[FX] r=reverb c=chorus y=delay  [Preset] sb/br/sl/fa\r\n"
		L"[Ej] @p50-1:20[100-120]  @p1:50[100]  @sb1:30  @br2:00",
		L"[형식] @<cmd><시각>[-<종료>][<값>[-<종료값>]]\r\n"
		L"[시각] 초(50) 또는 분:초(1:20). GDI 배너 시계와 동일.\r\n"
		L"[값] 0-200 (100=원곡). []에 2값 = 선형 보간.\r\n"
		L"[기본] p=피치 t=템포 d=DS음량  [EQ] a-o m/n/k/i/s  [FX] r/c/y\r\n"
		L"[연출] sb/br/sl/fa  [예] @p50-1:20[100-120] @p1:50[100] @sb1:30",
		L"【格式】 @<cmd><时间>[-<结束>][<值>[-<结束值>]]\r\n"
		L"【时间】 秒(50)或分:秒(1:20)。与GDI横幅时钟相同。\r\n"
		L"【值】 0-200 (100=原曲)。[]内两值=线性插值。\r\n"
		L"【基本】 p=音高 t=速度 d=DS音量  【EQ】 a-o m/n/k/i/s  【效果】 r/c/y\r\n"
		L"【演出】 sb/br/sl/fa  【例】 @p50-1:20[100-120] @p1:50[100] @sb1:30",
		L"[الصيغة] @<cmd><وقت>[-<نهاية>][<قيمة>[-<قيمة النهاية>]]\r\n"
		L"[الوقت] كساعة GDI على اللافتة. [القيمة] 0-200، [] منحدر.\r\n"
		L"p=طبقة t=إيقاع d=صوت DS  a-o EQ  r/c/y  sb/br/sl/fa\r\n"
		L"مثال: @p50-1:20[100-120] @p1:50[100] @sb1:30 @br2:00",
		L"[Формат] @<cmd><время>[-<конец>][<знач>[-<конЗнач>]]\r\n"
		L"[Время] Как часы GDI на баннере. [Знач] 0-200, [] рампа.\r\n"
		L"p=высота t=темп d=DS  a-o EQ  r/c/y  sb/br/sl/fa\r\n"
		L"Прим: @p50-1:20[100-120] @p1:50[100] @sb1:30 @br2:00",
		L"[Format] @<cmd><zeit>[-<ende>][<wert>[-<endWert>]]\r\n"
		L"[Zeit] Wie GDI-Banner. [Wert] 0-200, [] Rampe.\r\n"
		L"p/Tonhoehe t/Tempo d/DS  a-o EQ  r/c/y FX  sb/br/sl/fa\r\n"
		L"z.B.: @p50-1:20[100-120] @p1:50[100] @sb1:30 @br2:00",
		L"[Formato] @<cmd><tempo>[-<fim>][<val>[-<valFim>]]\r\n"
		L"[Tempo] Igual ao GDI. [Valor] 0-200, [] rampa.\r\n"
		L"p=tom t=andamento d=DS  a-o EQ  r/c/y  sb/br/sl/fa\r\n"
		L"Ex: @p50-1:20[100-120] @p1:50[100] @sb1:30 @br2:00",
		L"[Format] @<cmd><tijd>[-<einde>][<waarde>[-<eindWaarde>]]\r\n"
		L"[Tijd] Zelfde als GDI-banner. [Waarde] 0-200, [] ramp.\r\n"
		L"p=toon t=tempo d=DS  a-o EQ  r/c/y  sb/br/sl/fa\r\n"
		L"Vb: @p50-1:20[100-120] @p1:50[100] @sb1:30 @br2:00",
		L"[Format] @<cmd><czas>[-<koniec>][<wart>[-<wartKoniec>]]\r\n"
		L"[Czas] Jak zegar GDI. [Wartosc] 0-200, [] rampa.\r\n"
		L"p=wysokosc t=tempo d=DS  a-o EQ  r/c/y  sb/br/sl/fa\r\n"
		L"Np.: @p50-1:20[100-120] @p1:50[100] @sb1:30 @br2:00",
		L"[Format] @<cmd><zaman>[-<bitis>][<deger>[-<bitisDeger>]]\r\n"
		L"[Zaman] GDI banner saati ile ayni. [Deger] 0-200, [] rampa.\r\n"
		L"p=perde t=tempo d=DS  a-o EQ  r/c/y  sb/br/sl/fa\r\n"
		L"Orn: @p50-1:20[100-120] @p1:50[100] @sb1:30 @br2:00");
}

static CString MpPromptEditLabelText()
{
	return LL14(
		L"プロンプト入力欄",
		L"Prompt input",
		L"Saisie du prompt",
		L"Input prompt",
		L"Entrada de prompte",
		L"프롬프트 입력",
		L"提示输入",
		L"إدخال الموجه",
		L"Ввод промпта",
		L"Prompt-Eingabe",
		L"Entrada de prompt",
		L"Prompt invoer",
		L"Pole promptu",
		L"Istem girisi");
}

static void MpInitPromptStatic(CCustomStatic& st, CFont* pFont, BOOL bAero)
{
	if (pFont)
		st.SetFont(pFont, FALSE);
	st.SetGradation(0, 0, 0, FALSE);
	st.SetDropShadow(0, 0, 0, 0, FALSE);
	st.SetPreferWideMode(FALSE);
	st.SetAeroMode(bAero);
}

static void MpSetPromptLabelText(CCustomStatic& st, LPCTSTR plain)
{
	if (!plain || !*plain)
		return;
	CString s;
	s.Format(_T("!@C404858%s"), plain);
	st.SetWindowText(s);
}

void CPromptDlg::StyleButtons()
{
	m_run.SetGradation(RGB(200, 240, 200), RGB(130, 205, 140), 0, TRUE);
	m_stop.SetGradation(RGB(255, 215, 220), RGB(255, 165, 180), 0, TRUE);
	m_reset.SetGradation(RGB(215, 235, 255), RGB(165, 205, 245), 0, TRUE);
	m_clear.SetGradation(RGB(255, 235, 205), RGB(255, 205, 150), 0, TRUE);
	m_close.SetGradation(RGB(235, 230, 240), RGB(205, 195, 215), 0, TRUE);
	m_saveHist.SetGradation(RGB(215, 235, 255), RGB(165, 205, 245), 0, TRUE);
}

void CPromptDlg::SetupTooltips()
{
	if (!CCustomControlUtility::BeginDialogToolTip(m_tooltip, this))
		return;
	auto addTip = [this](CWnd& w, LPCTSTR text) {
		if (!text || !w.GetSafeHwnd()) return;
		m_tooltip.AddTool(&w, text);
	};
	addTip(m_run, LL14(L"プロンプトを解析し、演奏中にパラメータを自動変更します。", L"Parse the prompt and apply parameter changes during playback.", L"Analyser le prompt et appliquer les changements pendant la lecture.", L"Analizza il prompt e applica le modifiche durante l'esecuzione.", L"Analizar el prompte y aplicar cambios durante la reproduccion.", L"프롬프트를 해석해 연주 중 파라미터를 자동 변경합니다.", L"解析提示并在播放中自动更改参数。", L"تحليل الموجه وتطبيق التغييرات أثناء التشغيل.", L"Разобрать промпт и применять изменения при воспроизведении.", L"Prompt parsen und waehrend der Wiedergabe anwenden.", L"Analisar o prompt e aplicar alteracoes durante a reproducao.", L"Prompt parseren en tijdens afspelen toepassen.", L"Parsuj prompt i stosuj zmiany podczas odtwarzania.", L"Istemi ayristirip calma sirasinda uygula."));
	addTip(m_stop, LL14(L"プロンプト実行を停止します(設定値は維持)。", L"Stop prompt execution (keep current settings).", L"Arreter l'execution du prompt (conserver les reglages).", L"Ferma l'esecuzione del prompt (mantieni i valori).", L"Detener la ejecucion del prompte (mantener ajustes).", L"프롬프트 실행을 중지합니다(설정값 유지).", L"停止提示执行(保留当前设置)。", L"إيقاف تنفيذ الموجه (الإبقاء على الإعدادات).", L"Остановить промпт (настройки сохраняются).", L"Prompt-Ausfuehrung stoppen (Einstellungen behalten).", L"Parar execucao do prompt (manter configuracoes).", L"Prompt uitvoering stoppen (instellingen behouden).", L"Zatrzymaj prompt (zachowaj ustawienia).", L"Istem calistirmasini durdur (ayarlari koru)."));
	addTip(m_reset, LL14(L"実行前の設定に戻し、プロンプト実行を停止します。", L"Restore settings from before execution and stop.", L"Restaurer les reglages d'avant execution et arreter.", L"Ripristina le impostazioni precedenti e ferma.", L"Restaurar ajustes previos y detener.", L"실행 전 설정으로 되돌리고 중지합니다.", L"恢复到执行前设置并停止。", L"استعادة الإعدادات قبل التشغيل والإيقاف.", L"Восстановить настройки до запуска и остановить.", L"Einstellungen vor Ausfuehrung wiederherstellen.", L"Restaurar configuracoes anteriores e parar.", L"Instellingen voor uitvoering herstellen en stoppen.", L"Przywroc ustawienia sprzed uruchomienia i zatrzymaj.", L"Calistirmadan onceki ayarlara don ve durdur."));
	addTip(m_clear, LL14(L"プロンプト本文を消去し、設定も初期状態に戻します。", L"Clear prompt text and restore initial settings.", L"Effacer le prompt et restaurer les reglages initiaux.", L"Cancella il prompt e ripristina le impostazioni.", L"Borrar el prompte y restaurar ajustes iniciales.", L"프롬프트 본문을 지우고 설정도 초기화합니다.", L"清除提示文本并恢复初始设置。", L"مسح نص الموجه واستعادة الإعدادات الأولية.", L"Очистить промпт и восстановить исходные настройки.", L"Prompt loeschen und Ausgangseinstellungen wiederherstellen.", L"Limpar prompt e restaurar configuracoes iniciais.", L"Prompt wissen en begininstellingen herstellen.", L"Wyczysc prompt i przywroc ustawienia poczatkowe.", L"Istem metnini temizle ve baslangic ayarlarina don."));
	addTip(m_close, LL14(L"プロンプトウィンドウを閉じます(入力内容は保存)。", L"Close the prompt window (text is saved).", L"Fermer la fenetre de prompt (texte sauvegarde).", L"Chiudi la finestra prompt (testo salvato).", L"Cerrar la ventana de prompte (se guarda el texto).", L"프롬프트 창을 닫습니다(입력 내용 저장).", L"关闭提示窗口(保存输入内容)。", L"إغلاق نافذة الموجه (يُحفظ النص).", L"Закрыть окно промпта (текст сохраняется).", L"Prompt-Fenster schliessen (Text wird gespeichert).", L"Fechar janela de prompt (texto salvo).", L"Promptvenster sluiten (tekst wordt opgeslagen).", L"Zamknij okno promptu (tekst jest zapisywany).", L"Istem penceresini kapat (metin kaydedilir)."));
	addTip(m_saveHist, LL14(L"現在のプロンプト本文を履歴に保存します。", L"Save current prompt text to history.", L"Enregistrer le prompt dans l'historique.", L"Salva il prompt nella cronologia.", L"Guardar el prompte en el historial.", L"현재 프롬프트를 기록에 저장합니다.", L"将当前提示保存到历史。", L"حفظ الموجه في السجل.", L"Сохранить промпт в историю.", L"Prompt in Verlauf speichern.", L"Salvar prompt no historico.", L"Prompt in geschiedenis opslaan.", L"Zapisz prompt w historii.", L"Promptu gecmise kaydet."));
	addTip(m_hist, LL14(L"保存したプロンプト履歴から読み込みます。", L"Load a saved prompt from history.", L"Charger un prompt depuis l'historique.", L"Carica un prompt dalla cronologia.", L"Cargar un prompte del historial.", L"저장된 프롬프트 기록에서 불러옵니다.", L"从历史记录加载提示。", L"تحميل موجه من السجل.", L"Загрузить промпт из истории.", L"Prompt aus Verlauf laden.", L"Carregar prompt do historico.", L"Prompt uit geschiedenis laden.", L"Wczytaj prompt z historii.", L"Gecmisten prompt yukle."));
	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 360, 10000);
}

static void RaiseChildZOrder(CWnd* pWnd)
{
	if (pWnd && pWnd->GetSafeHwnd())
		pWnd->SetWindowPos(&CWnd::wndTop, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void CPromptDlg::LayoutControls()
{
	if (!::IsWindow(GetSafeHwnd())) return;
	CRect rc;
	GetClientRect(&rc);
	const int W = rc.Width(), H = rc.Height();
	if (W < 200) return;

	const int M = 8;
	const int btnH = 36;
	const int btnGap = 6;
	const int remainH = 18;
	const int histH = 24;
	const int histGap = 10;
	const int gapSm = 4;
	const int gapMd = 6;
	const int editMinH = 72;
	const int legendMinH = 80;
	const int editLblH = 18;

	const int iw = max(1, W - M * 2);
	const int lockGap = 6;
	const int btnW = max(60, min(80, (iw - btnGap * 4) / 5));

	CCC_MainLockSetHeaderRow(m_hWnd, M, editLblH);
	CRect lockRc;
	CCC_MainLockGetOverlayRect(m_hWnd, lockRc);
	const int lblW = lockRc.IsRectEmpty()
		? max(80, iw - CCC_MainLockGetReserveWidth(m_hWnd) - lockGap)
		: max(80, lockRc.left - M - lockGap);

	// 下から順に確保(ボタン → 履歴 → 残り文字 → 説明 → 入力)
	const int btnY = max(M, H - M - btnH);
	const int histY = btnY - histGap - histH;
	const int remainY = histY - gapSm - remainH;
	const int legendBottom = remainY - gapMd;

	int legendH = max(legendMinH, (H * 7) / 30);
	int legendTop = legendBottom - legendH;
	if (legendTop < M + editLblH + gapSm + editMinH + gapMd) {
		legendTop = M + editLblH + gapSm + editMinH + gapMd;
		legendH = legendBottom - legendTop;
	}
	if (legendH < 48)
		legendH = max(48, legendBottom - legendTop);
	if (legendTop + legendH > legendBottom)
		legendH = max(48, legendBottom - legendTop);

	const int editTop = M + editLblH + gapSm;
	int editH = legendTop - gapMd - editTop;
	if (editH < editMinH) {
		editH = editMinH;
		legendTop = editTop + editH + gapMd;
		legendH = max(48, legendBottom - legendTop);
		if (legendTop + legendH > legendBottom)
			legendH = max(48, legendBottom - legendTop);
	}

	if (m_lblEdit.GetSafeHwnd())
		m_lblEdit.MoveWindow(M, M, lblW, editLblH);
	if (m_edit.GetSafeHwnd())
		m_edit.MoveWindow(M, editTop, iw, editH);
	if (m_legend.GetSafeHwnd())
		m_legend.MoveWindow(M, legendTop, iw, max(48, legendH));
	if (CWnd* pRem = GetDlgItem(IDC_MPP_REMAIN))
		pRem->MoveWindow(M, remainY, iw, remainH);
	if (CWnd* pHistL = GetDlgItem(IDC_MPP_HIST_L))
		pHistL->MoveWindow(M, histY + 3, 34, 18);
	const int saveHistW = 68;
	const int histComboW = max(120, min(220, iw - 36 - saveHistW - 8));
	if (m_hist.GetSafeHwnd()) {
		m_hist.MoveWindow(M + 36, histY, histComboW, histH);
		m_hist.SetWindowPos(&CWnd::wndBottom, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	}
	if (m_saveHist.GetSafeHwnd())
		m_saveHist.MoveWindow(M + 36 + histComboW + 6, histY, saveHistW, histH);

	int bx = M;
	if (m_run.GetSafeHwnd()) { m_run.MoveWindow(bx, btnY, btnW, btnH); bx += btnW + btnGap; }
	if (m_stop.GetSafeHwnd()) { m_stop.MoveWindow(bx, btnY, btnW, btnH); bx += btnW + btnGap; }
	if (m_reset.GetSafeHwnd()) { m_reset.MoveWindow(bx, btnY, btnW, btnH); bx += btnW + btnGap; }
	if (m_clear.GetSafeHwnd()) { m_clear.MoveWindow(bx, btnY, btnW, btnH); bx += btnW + btnGap; }
	if (m_close.GetSafeHwnd())
		m_close.MoveWindow(max(bx, W - M - btnW), btnY, btnW, btnH);

	// ボタンを最前面へ(履歴コンボと重ならないよう)
	RaiseChildZOrder(&m_lblEdit);
	RaiseChildZOrder(&m_edit);
	RaiseChildZOrder(&m_legend);
	RaiseChildZOrder(GetDlgItem(IDC_MPP_REMAIN));
	RaiseChildZOrder(GetDlgItem(IDC_MPP_HIST_L));
	RaiseChildZOrder(&m_hist);
	RaiseChildZOrder(&m_saveHist);
	RaiseChildZOrder(&m_run);
	RaiseChildZOrder(&m_stop);
	RaiseChildZOrder(&m_reset);
	RaiseChildZOrder(&m_clear);
	RaiseChildZOrder(&m_close);
	CCC_MainLockBringToFront(m_hWnd);
}

void CPromptDlg::RefreshAfterLayout(BOOL bSyncRedraw)
{
	if (!::IsWindow(GetSafeHwnd())) return;

#if CCUSTOM_AERO_SUPPORT
	if (CCC_IsAeroEnabled() && CCC_IsWin11())
		CCC_GroupBoxesBack(m_hWnd);
#endif

	m_run.RepaintClient();
	m_stop.RepaintClient();
	m_reset.RepaintClient();
	m_clear.RepaintClient();
	m_close.RepaintClient();

	if (m_lblEdit.GetSafeHwnd())
		m_lblEdit.Invalidate(TRUE);
	if (m_edit.GetSafeHwnd())
		m_edit.Invalidate(TRUE);
	if (m_legend.GetSafeHwnd())
		m_legend.Invalidate(TRUE);
	if (CWnd* pRem = GetDlgItem(IDC_MPP_REMAIN))
		pRem->Invalidate(TRUE);

#if CCUSTOM_AERO_SUPPORT
	if (CCC_IsAeroEnabled() && CCC_IsWin11())
		CCC_RefreshKids(m_hWnd);
#endif

	UINT rdw = RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME;
	if (bSyncRedraw)
		rdw |= RDW_UPDATENOW;
	RedrawWindow(NULL, NULL, rdw);
}

void CPromptDlg::RefreshOpaqueFixers(BOOL bSync)
{
	if (!GetSafeHwnd()) return;
#if CCUSTOM_AERO_SUPPORT
	if (bSync)
		SendMessage(CCC_MSG_REAPPLY_OPAQUE_FIXERS, 0, 0);
	else
		PostMessage(CCC_MSG_REAPPLY_OPAQUE_FIXERS, 0, 0);
#else
	UNREFERENCED_PARAMETER(bSync);
#endif
}

void CPromptDlg::SyncLayoutAndPaint(BOOL bSyncRedraw, BOOL bReapplyOpaqueFixers)
{
	LayoutControls();
	RefreshAfterLayout(bSyncRedraw);
	if (bReapplyOpaqueFixers)
		RefreshOpaqueFixers(bSyncRedraw);
}

void CPromptDlg::SavePosToSavedata()
{
	if (!::IsWindow(GetSafeHwnd()) || IsIconic()) return;
	CRect r;
	GetWindowRect(&r);
	savedata.mpPromptX = r.left;
	savedata.mpPromptY = r.top;
	savedata.mpPromptW = r.Width();
	savedata.mpPromptH = r.Height();
	savedata.mpPromptHasPos = 1;
}

void CPromptDlg::RestorePosFromSavedata()
{
	int x = savedata.mpPromptX, y = savedata.mpPromptY;
	int w = savedata.mpPromptW, h = savedata.mpPromptH;
	if (!savedata.mpPromptHasPos || w < 280 || h < 340 || w > 10000 || h > 10000) {
		w = 375;
		h = 368;
		if (GetParent() && ::IsWindow(GetParent()->GetSafeHwnd())) {
			CRect pr;
			GetParent()->GetWindowRect(&pr);
			x = pr.left + 40;
			y = pr.top + 40;
		}
		else {
			x = 100;
			y = 100;
		}
	}
	RECT rcWork{};
	SystemParametersInfo(SPI_GETWORKAREA, 0, &rcWork, 0);
	if (x < rcWork.left - 50 || x > rcWork.right - 50) x = rcWork.left + 40;
	if (y < rcWork.top - 10 || y > rcWork.bottom - 50) y = rcWork.top + 40;
	MoveWindow(x, y, w, h);
	m_posRestored = TRUE;
}

static void ScrollLegendEdit(CEdit& legend, short zDelta)
{
	if (!legend.GetSafeHwnd()) return;
	const int lines = (zDelta > 0) ? -3 : 3;
	legend.SendMessage(EM_LINESCROLL, 0, lines);
}

BOOL CPromptDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	RestorePosFromSavedata();

	SetWindowText(LL14(L"プロンプト", L"Prompt", L"Prompt", L"Prompt", L"Prompte", L"프롬프트", L"提示", L"موجه", L"Промпт", L"Prompt", L"Prompt", L"Prompt", L"Prompt", L"Istem"));
	ModifyStyle(WS_MINIMIZEBOX, 0);
	SetIcon(nullptr, TRUE);
	SetIcon(nullptr, FALSE);
#if CCUSTOM_AERO_SUPPORT
	if (!CCC_IsAeroEnabled())
#endif
		ModifyStyleEx(0, WS_EX_DLGMODALFRAME);
	m_legend.SetWindowText(MpPromptLegendText());
	SetDlgItemText(IDC_MPP_RUN, LL14(L"実行", L"Run", L"Executer", L"Esegui", L"Ejecutar", L"실행", L"执行", L"تشغيل", L"Запуск", L"Ausfuehren", L"Executar", L"Uitvoeren", L"Uruchom", L"Calistir"));
	SetDlgItemText(IDC_MPP_STOP, LL14(L"停止", L"Stop", L"Arret", L"Stop", L"Detener", L"중지", L"停止", L"إيقاف", L"Стоп", L"Stopp", L"Parar", L"Stoppen", L"Stop", L"Durdur"));
	SetDlgItemText(IDC_MPP_RESET, LL14(L"リセット", L"Reset", L"Reinit.", L"Ripristina", L"Restablecer", L"리셋", L"重置", L"إعادة ضبط", L"Сброс", L"Zuruecksetzen", L"Redefinir", L"Reset", L"Reset", L"Sifirla"));
	SetDlgItemText(IDC_MPP_CLEAR, LL14(L"クリア", L"Clear", L"Effacer", L"Cancella", L"Borrar", L"지우기", L"清除", L"مسح", L"Очистить", L"Leeren", L"Limpar", L"Wissen", L"Wyczysc", L"Temizle"));
	SetDlgItemText(IDC_MPP_CLOSE, LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	SetDlgItemText(IDC_MPP_HIST_L, LL14(L"履歴:", L"History:", L"Historique:", L"Cronologia:", L"Historial:", L"기록:", L"历史:", L"السجل:", L"История:", L"Verlauf:", L"Historico:", L"Geschiedenis:", L"Historia:", L"Gecmis:"));
	SetDlgItemText(IDC_MPP_SAVEHIST, LL14(L"履歴保存", L"Save history", L"Enregistrer", L"Salva", L"Guardar", L"기록 저장", L"保存历史", L"حفظ", L"Сохранить", L"Speichern", L"Salvar", L"Opslaan", L"Zapisz", L"Kaydet"));

	if (m_legend.GetSafeHwnd()) {
		m_legend.SetReadOnly(TRUE);
		m_legend.ModifyStyle(WS_BORDER | WS_TABSTOP, 0);
		m_legend.ModifyStyle(0, ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL);
		LOGFONT lf{};
		CFont* pDef = GetFont();
		if (pDef && pDef->GetLogFont(&lf)) {
			lf.lfHeight = (lf.lfHeight < 0) ? (lf.lfHeight * 92 / 100) : -(abs(lf.lfHeight) * 92 / 100);
			if (m_fontLegend.GetSafeHandle()) m_fontLegend.DeleteObject();
			if (m_fontLegend.CreateFontIndirect(&lf))
				m_legend.SetFont(&m_fontLegend);
		}
	}
	{
		LOGFONT lf{};
		CFont* pDef = GetFont();
		if (pDef && pDef->GetLogFont(&lf)) {
			lf.lfHeight = (lf.lfHeight < 0) ? (lf.lfHeight * 11 / 10) : -(abs(lf.lfHeight) * 11 / 10);
			if (m_fontBtn.GetSafeHandle()) m_fontBtn.DeleteObject();
			if (m_fontBtn.CreateFontIndirect(&lf))
			{
				m_run.SetFont(&m_fontBtn);
				m_stop.SetFont(&m_fontBtn);
				m_reset.SetFont(&m_fontBtn);
				m_clear.SetFont(&m_fontBtn);
				m_close.SetFont(&m_fontBtn);
				m_saveHist.SetFont(&m_fontBtn);
			}
		}
	}

#if CCUSTOM_AERO_SUPPORT
	const BOOL bAero = CCC_IsAeroEnabled();
#else
	const BOOL bAero = FALSE;
#endif
	if (!m_lblEdit.GetSafeHwnd()) {
		m_lblEdit.Create(_T(""), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOTIFY,
			CRect(0, 0, 1, 1), this, IDC_MPP_EDIT_L);
		LOGFONT lfLbl{};
		CFont* pDef = GetFont();
		if (pDef && pDef->GetLogFont(&lfLbl)) {
			lfLbl.lfWeight = FW_SEMIBOLD;
			if (m_fontEditLbl.GetSafeHandle()) m_fontEditLbl.DeleteObject();
			if (m_fontEditLbl.CreateFontIndirect(&lfLbl))
				MpInitPromptStatic(m_lblEdit, &m_fontEditLbl, bAero);
			else
				MpInitPromptStatic(m_lblEdit, pDef, bAero);
		}
		else {
			MpInitPromptStatic(m_lblEdit, pDef, bAero);
		}
		MpSetPromptLabelText(m_lblEdit, MpPromptEditLabelText());
	}

	StyleButtons();
	SetupTooltips();
	LoadTextFromSavedata();
	ReloadHistoryCombo();
	UpdateRemainLabel();
	EnableMainWindowLock(&savedata.mpPromptMainLock);
	SyncLayoutAndPaint(TRUE, TRUE);
	return TRUE;
}

void CPromptDlg::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	if (lpMMI) {
		lpMMI->ptMinTrackSize.x = 315;
		lpMMI->ptMinTrackSize.y = 340;
	}
	CCustomBlurDialogExBase::OnGetMinMaxInfo(lpMMI);
}

void CPromptDlg::OnEnterSizeMove()
{
	m_inSizeMove = TRUE;
	Default();
}

void CPromptDlg::OnExitSizeMove()
{
	m_inSizeMove = FALSE;
	if (::IsWindow(m_hWnd) && !IsIconic()) {
		LayoutControls();
		RedrawWindow(NULL, NULL,
			RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
		RefreshOpaqueFixers(FALSE);
		if (m_posRestored)
			SavePosToSavedata();
	}
	Default();
}

void CPromptDlg::OnMoving(UINT fwSide, LPRECT pRect)
{
	CCustomBlurDialogExBase::OnMoving(fwSide, pRect);
	if (m_posRestored)
		SavePosToSavedata();
}

void CPromptDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	if (nType == SIZE_MINIMIZED)
		return;
	LayoutControls();
	if (m_inSizeMove) {
		RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
	}
	else {
		RedrawWindow(NULL, NULL,
			RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
		RefreshOpaqueFixers(FALSE);
	}
	if (m_posRestored)
		SavePosToSavedata();
}

static BOOL LegendHitTestWheelPoint(const CEdit& legend, CPoint screenPt)
{
	if (!legend.GetSafeHwnd()) return FALSE;
	CRect rc;
	legend.GetWindowRect(&rc);
	return rc.PtInRect(screenPt);
}

BOOL CPromptDlg::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	if (LegendHitTestWheelPoint(m_legend, pt)) {
		ScrollLegendEdit(m_legend, zDelta);
		return TRUE;
	}
	return CCustomBlurDialogExBase::OnMouseWheel(nFlags, zDelta, pt);
}

BOOL CPromptDlg::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_MOUSEWHEEL && m_legend.GetSafeHwnd()) {
		CPoint pt(GET_X_LPARAM(pMsg->lParam), GET_Y_LPARAM(pMsg->lParam));
		if (LegendHitTestWheelPoint(m_legend, pt)) {
			ScrollLegendEdit(m_legend, (short)HIWORD(pMsg->wParam));
			return TRUE;
		}
	}
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}

#if CCUSTOM_AERO_SUPPORT
LRESULT CPromptDlg::OnReapplyOpaqueFixers(WPARAM wParam, LPARAM lParam)
{
	return CCustomBlurDialogExBase::OnReapplyOpaqueFixers(wParam, lParam);
}
#endif

void CPromptDlg::LoadTextFromSavedata()
{
	if (savedata.mpPromptText[0])
		SetDlgItemText(IDC_MPP_TEXT, savedata.mpPromptText);
}

void CPromptDlg::SaveTextToSavedata()
{
	CString s;
	GetDlgItemText(IDC_MPP_TEXT, s);
	if (s.GetLength() > kMaxChars)
		s = s.Left(kMaxChars);
	_tcsncpy(savedata.mpPromptText, s, _countof(savedata.mpPromptText) - 1);
	savedata.mpPromptText[_countof(savedata.mpPromptText) - 1] = 0;
}

void CPromptDlg::UpdateRemainLabel()
{
	CString s;
	GetDlgItemText(IDC_MPP_TEXT, s);
	int remain = kMaxChars - s.GetLength();
	if (remain < 0) remain = 0;
	CString lbl;
	lbl.Format(LL14(L"残り %d 文字", L"%d characters left", L"%d caracteres restants", L"%d caratteri rimasti", L"%d caracteres restantes", L"%d자 남음", L"剩余 %d 字", L"%d حرف متبقٍ", L"Осталось %d симв.", L"Noch %d Zeichen", L"%d caracteres restantes", L"%d tekens over", L"Pozostalo %d znakow", L"%d karakter kaldi"), remain);
	SetDlgItemText(IDC_MPP_REMAIN, lbl);
}

void CPromptDlg::OnTextChanged()
{
	CString s;
	GetDlgItemText(IDC_MPP_TEXT, s);
	if (s.GetLength() > kMaxChars) {
		s = s.Left(kMaxChars);
		m_edit.SetWindowText(s);
		m_edit.SetSel(s.GetLength(), s.GetLength());
	}
	UpdateRemainLabel();
	SaveTextToSavedata();
}

void CPromptDlg::OnRun()
{
	CString text, err;
	GetDlgItemText(IDC_MPP_TEXT, text);
	SaveTextToSavedata();
	if (!MpPromptExecute(text, &err)) {
		AfxMessageBox(err.IsEmpty()
			? LL14(L"プロンプトの解析に失敗しました。", L"Failed to parse prompt.", L"Echec analyse prompt.", L"Analisi prompt fallita.", L"Error al analizar prompte.", L"프롬프트 해석 실패.", L"提示解析失败。", L"فشل تحليل الموجه.", L"Ошибка разбора промпта.", L"Prompt parsen fehlgeschlagen.", L"Falha ao analisar prompt.", L"Prompt parseren mislukt.", L"Blad parsowania promptu.", L"Istem ayrıştırılamadı.")
			: err);
		return;
	}
}

void CPromptDlg::OnStop()
{
	MpPromptStop();
}

void CPromptDlg::OnReset()
{
	MpPromptReset();
}

void CPromptDlg::OnClear()
{
	SaveCurrentToHistory();
	SetDlgItemText(IDC_MPP_TEXT, _T(""));
	SaveTextToSavedata();
	MpPromptClearAll();
	UpdateRemainLabel();
}

void CPromptDlg::SaveCurrentToHistory()
{
	CString s;
	GetDlgItemText(IDC_MPP_TEXT, s);
	s.Trim();
	if (!s.IsEmpty())
		MpPromptPushHistory(s);
	ReloadHistoryCombo();
}

void CPromptDlg::ReloadHistoryCombo()
{
	if (!m_hist.GetSafeHwnd()) return;
	g_histSelChanging = TRUE;
	const int prev = m_hist.GetCurSel();
	m_hist.ResetContent();
	m_hist.AddString(LL14(L"(履歴なし)", L"(No history)", L"(Aucun historique)", L"(Nessuna cronologia)",
		L"(Sin historial)", L"(기록 없음)", L"(无历史)", L"(لا سجل)", L"(Нет истории)", L"(Kein Verlauf)",
		L"(Sem historico)", L"(Geen geschiedenis)", L"(Brak historii)", L"(Gecmis yok)"));
	if (savedata.mpPromptHistCnt > 0) {
		for (int i = 0; i < savedata.mpPromptHistCnt && i < 20; ++i) {
			CString line = savedata.mpPromptHistText[i];
			line.Replace(_T("\r\n"), _T(" "));
			line.Replace(_T("\n"), _T(" "));
			if (line.GetLength() > 80)
				line = line.Left(80) + _T("...");
			CString label;
			label.Format(_T("%d: %s"), i + 1, (LPCTSTR)line);
			m_hist.AddString(label);
		}
	}
	if (prev >= 0 && prev < m_hist.GetCount())
		m_hist.SetCurSel(prev);
	else
		m_hist.SetCurSel(0);
	g_histSelChanging = FALSE;
}

void CPromptDlg::OnSaveHist()
{
	SaveCurrentToHistory();
}

void CPromptDlg::OnHistSel()
{
	if (g_histSelChanging) return;
	const int sel = m_hist.GetCurSel();
	if (sel <= 0) return;
	const int idx = sel - 1;
	if (idx < 0 || idx >= savedata.mpPromptHistCnt || idx >= 20) return;
	g_histSelChanging = TRUE;
	SetDlgItemText(IDC_MPP_TEXT, savedata.mpPromptHistText[idx]);
	g_histSelChanging = FALSE;
	SaveTextToSavedata();
	UpdateRemainLabel();
}

void CPromptDlg::OnCloseBtn()
{
	SaveTextToSavedata();
	SavePosToSavedata();
	DestroyWindow();
}

void CPromptDlg::OnClose()
{
	SaveTextToSavedata();
	SavePosToSavedata();
	DestroyWindow();
}

void CPromptDlg::PostNcDestroy()
{
	CCustomBlurDialogExBase::PostNcDestroy();
	if (g_promptDlg == this)
		g_promptDlg = nullptr;
	delete this;
}

HBRUSH CPromptDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	const UINT id = pWnd ? (UINT)pWnd->GetDlgCtrlID() : 0;
	if (nCtlColor == CTLCOLOR_DLG || nCtlColor == CTLCOLOR_STATIC
		|| (nCtlColor == CTLCOLOR_EDIT && id == IDC_MPP_LEGEND)) {
		if (!m_brDlg.GetSafeHandle())
			m_brDlg.CreateSolidBrush(RGB(240, 240, 245));
		pDC->SetBkColor(RGB(240, 240, 245));
		if (nCtlColor != CTLCOLOR_EDIT || id == IDC_MPP_LEGEND)
			pDC->SetTextColor(RGB(55, 55, 70));
		return m_brDlg;
	}
	return CCustomBlurDialogExBase::OnCtlColor(pDC, pWnd, nCtlColor);
}

void MpShowPromptDialog(CWnd* pParent)
{
	if (g_promptDlg && ::IsWindow(g_promptDlg->GetSafeHwnd())) {
		g_promptDlg->ShowWindow(SW_SHOW);
		g_promptDlg->SetForegroundWindow();
		return;
	}
	CPromptDlg* dlg = new CPromptDlg(pParent);
	if (!dlg->Create(IDD_MP_PROMPT, pParent)) {
		delete dlg;
		return;
	}
	dlg->ShowWindow(SW_SHOW);
	g_promptDlg = dlg;
}
