// AudioSelect.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "AudioSelect.h"

extern CString streamname[40];
extern CString streamname2[40];
extern IAMStreamSelect* iam;
extern int audionum;
extern int au;
extern int streamidx[40];
extern int streamidx2[40];

IMPLEMENT_DYNAMIC(CAudioSelect, CCustomBlurDialogBase)

CAudioSelect::CAudioSelect(CWnd* pParent /*=NULL*/)
	: CCustomBlurDialogBase(CAudioSelect::IDD, pParent)
	, audioCount(0)
	, subCount(0)
	, no(0)
	, subNo(-1)
{
}

CAudioSelect::~CAudioSelect()
{
}

void CAudioSelect::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST1, m_lb);
	DDX_Control(pDX, IDC_AS_SUBLIST, m_lbSub);
	DDX_Control(pDX, IDOK, m_okdummy);
	DDX_Control(pDX, IDC_STATIC, m_desc);
	DDX_Control(pDX, IDC_AS_AUDIOLBL, m_audioLbl);
	DDX_Control(pDX, IDC_AS_SUBLBL, m_subLbl);
}

BEGIN_MESSAGE_MAP(CAudioSelect, CCustomBlurDialogBase)
	ON_LBN_DBLCLK(IDC_LIST1, &CAudioSelect::OnLbnDblclkList1)
	ON_LBN_DBLCLK(IDC_AS_SUBLIST, &CAudioSelect::OnLbnDblclkSubList)
	ON_BN_CLICKED(IDOK, &CAudioSelect::OnBnClickedOk)
	ON_BN_CLICKED(IDCANCEL, &CAudioSelect::OnBnClickedCancel)
END_MESSAGE_MAP()

static BOOL AudioNameMatchesLang(const CString& low, WORD prim)
{
	switch (prim) {
	case LANG_JAPANESE:
		return low.Find(L"japanese") >= 0 || low.Find(L"日本語") >= 0
			|| low.Find(L"jpn") >= 0 || low.Find(L"ja-") >= 0 || low.Find(L"[ja]") >= 0
			|| low.Find(L"(ja)") >= 0 || low.Find(L" jap") >= 0;
	case LANG_ENGLISH:
		return low.Find(L"english") >= 0 || low.Find(L"eng") >= 0
			|| low.Find(L"en-") >= 0 || low.Find(L"[en]") >= 0 || low.Find(L"(en)") >= 0;
	case LANG_KOREAN:
		return low.Find(L"korean") >= 0 || low.Find(L"한국") >= 0 || low.Find(L"한국어") >= 0
			|| low.Find(L"kor") >= 0 || low.Find(L"ko-") >= 0;
	case LANG_CHINESE:
		return low.Find(L"chinese") >= 0 || low.Find(L"中文") >= 0 || low.Find(L"chi") >= 0
			|| low.Find(L"zh-") >= 0 || low.Find(L"mandarin") >= 0 || low.Find(L"cantonese") >= 0;
	case LANG_FRENCH:
		return low.Find(L"french") >= 0 || low.Find(L"français") >= 0 || low.Find(L"francais") >= 0
			|| low.Find(L"fra") >= 0 || low.Find(L"fre") >= 0 || low.Find(L"fr-") >= 0;
	case LANG_GERMAN:
		return low.Find(L"german") >= 0 || low.Find(L"deutsch") >= 0
			|| low.Find(L"ger") >= 0 || low.Find(L"deu") >= 0 || low.Find(L"de-") >= 0;
	case LANG_SPANISH:
		return low.Find(L"spanish") >= 0 || low.Find(L"español") >= 0 || low.Find(L"espanol") >= 0
			|| low.Find(L"spa") >= 0 || low.Find(L"es-") >= 0;
	case LANG_RUSSIAN:
		return low.Find(L"russian") >= 0 || low.Find(L"рус") >= 0
			|| low.Find(L"rus") >= 0 || low.Find(L"ru-") >= 0;
	case LANG_ITALIAN:
		return low.Find(L"italian") >= 0 || low.Find(L"italiano") >= 0
			|| low.Find(L"ita") >= 0 || low.Find(L"it-") >= 0;
	case LANG_PORTUGUESE:
		return low.Find(L"portuguese") >= 0 || low.Find(L"portugu") >= 0
			|| low.Find(L"por") >= 0 || low.Find(L"pt-") >= 0;
	default:
		return FALSE;
	}
}

int CAudioSelect::PickOsLocaleAudio() const
{
	const int n = (audioCount > 0) ? audioCount : audionum;
	if (n <= 0 || !iam) return -1;

	const LANGID ui = GetUserDefaultUILanguage();
	const WORD prim = PRIMARYLANGID(ui);
	int byLcid = -1;
	int byName = -1;

	for (int i = 0; i < n && i < 40; ++i) {
		const int idx = (streamidx[i] >= 0) ? streamidx[i] : (i + au);
		LCID lcid = 0;
		LPWSTR pname = NULL;
		if (FAILED(iam->Info(idx, NULL, NULL, &lcid, NULL, &pname, NULL, NULL)))
			continue;
		CString nm = (pname && pname[0]) ? pname : streamname[i];
		if (pname) CoTaskMemFree(pname);

		if (lcid != 0 && PRIMARYLANGID(LANGIDFROMLCID(lcid)) == prim) {
			byLcid = i;
			break;
		}
		CString low = nm;
		low.MakeLower();
		if (byName < 0 && AudioNameMatchesLang(low, prim))
			byName = i;
	}
	if (byLcid >= 0) return byLcid;
	return byName;
}

void CAudioSelect::LayoutNoSubtitles()
{
	if (m_subLbl.GetSafeHwnd())
		m_subLbl.ShowWindow(SW_HIDE);
	if (m_lbSub.GetSafeHwnd())
		m_lbSub.ShowWindow(SW_HIDE);

	// 字幕欄ぶんだけ縮め、再生開始ボタンを字幕ラベル位置へ上げる
	CRect rcSubLbl;
	if (m_subLbl.GetSafeHwnd())
		m_subLbl.GetWindowRect(&rcSubLbl);
	else
		return;
	ScreenToClient(&rcSubLbl);

	CRect rcOk;
	m_okdummy.GetWindowRect(&rcOk);
	ScreenToClient(&rcOk);
	const int btnH = rcOk.Height();
	const int btnW = rcOk.Width();
	CRect rcClient;
	GetClientRect(&rcClient);
	const int btnX = (rcClient.Width() - btnW) / 2;
	m_okdummy.SetWindowPos(NULL, btnX, rcSubLbl.top, btnW, btnH,
		SWP_NOZORDER | SWP_NOACTIVATE);

	CRect rcList;
	m_lb.GetWindowRect(&rcList);
	ScreenToClient(&rcList);
	const int bottomPad = MulDiv(10, LOWORD(GetDialogBaseUnits()), 8);
	const int needClientH = rcSubLbl.top + btnH + bottomPad;

	CRect rcWnd;
	GetWindowRect(&rcWnd);
	const int nonClient = rcWnd.Height() - rcClient.Height();
	SetWindowPos(NULL, 0, 0, rcWnd.Width(), needClientH + nonClient,
		SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void CAudioSelect::CommitAndClose(BOOL forceSubOff)
{
	int a = m_lb.GetCurSel();
	if (a < 0) a = 0;
	no = a;

	if (forceSubOff || subCount <= 0 || !m_lbSub.IsWindowVisible()) {
		subNo = -1;
	} else {
		const int s = m_lbSub.GetCurSel();
		// 0 = なし
		subNo = (s <= 0) ? -1 : (s - 1);
		if (subNo >= subCount) subNo = -1;
	}
	EndDialog(IDOK);
}

void CAudioSelect::OnLbnDblclkList1()
{
	// 音声ダブルクリック: 字幕なしで開始（従来動作）
	CommitAndClose(TRUE);
}

void CAudioSelect::OnLbnDblclkSubList()
{
	// 字幕ダブルクリック: 音声は選択行のまま、字幕だけ反映して開始
	CommitAndClose(FALSE);
}

void CAudioSelect::OnBnClickedOk()
{
	// 再生開始: 音声／字幕のアクティブ行を両方反映
	CommitAndClose(FALSE);
}

void CAudioSelect::OnBnClickedCancel()
{
	EndDialog(IDCANCEL);
}

void CAudioSelect::OnCancel()
{
	EndDialog(IDCANCEL);
}

BOOL CAudioSelect::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();

	const int nAudio = (audioCount > 0) ? audioCount : ((no > 0) ? no : audionum);
	audioCount = nAudio;

	SetWindowText(LL14(L"音声／字幕の選択", L"Select Audio / Subtitles", L"Audio / sous-titres", L"Audio / sottotitoli", L"Audio / subtítulos", L"오디오/자막 선택", L"选择音频/字幕", L"الصوت/الترجمة", L"Аудио / субтитры", L"Audio / Untertitel", L"Áudio / legendas", L"Audio / ondertitels", L"Audio / napisy", L"Ses / altyazı"));
	m_desc.SetWindowText(LL14(
		L"複数の音声があるときに表示されます。\n音声と字幕を選び、「再生開始」で開始します。\n\n音声のダブルクリック＝字幕なしで開始\n字幕のダブルクリック＝その字幕で開始\n\n右クリックメニューからも切替できます。",
		L"Shown when multiple audio tracks exist.\nPick audio and subtitles, then Start.\n\nDouble-click audio = start with no subtitles\nDouble-click subtitle = start with that subtitle\n\nYou can also switch from the right-click menu.",
		L"Affiche s'il y a plusieurs pistes audio.\nChoisissez audio et sous-titres, puis Demarrer.\n\nDouble-clic audio = sans sous-titres\nDouble-clic sous-titre = avec ce sous-titre\n\nAussi via le menu contextuel.",
		L"Mostrato con piu tracce audio.\nScegli audio e sottotitoli, poi Avvia.\n\nDoppio clic audio = senza sottotitoli\nDoppio clic sottotitolo = con quel sottotitolo\n\nAnche dal menu contestuale.",
		L"Se muestra con varias pistas de audio.\nElija audio y subtitulos, luego Iniciar.\n\nDoble clic audio = sin subtitulos\nDoble clic subtitulo = con ese subtitulo\n\nTambien desde el menu contextual.",
		L"오디오 트랙이 여러 개일 때 표시됩니다.\n오디오와 자막을 고른 뒤 재생 시작.\n\n오디오 더블클릭=자막 없이 시작\n자막 더블클릭=해당 자막으로 시작\n\n우클릭 메뉴에서도 전환할 수 있습니다.",
		L"有多个音轨时显示。\n选择音频和字幕后点“开始播放”。\n\n双击音频=无字幕开始\n双击字幕=用该字幕开始\n\n也可在右键菜单切换。",
		L"يظهر عند وجود عدة مسارات صوت.\nاختر الصوت والترجمة ثم ابدأ.\n\nنقر مزدوج للصوت=بدون ترجمة\nنقر مزدوج للترجمة=بتلك الترجمة\n\nيمكن أيضا من قائمة النقر الأيمن.",
		L"Показывается при нескольких аудиодорожках.\nВыберите аудио и субтитры, затем Старт.\n\nДвойной клик по аудио = без субтитров\nДвойной клик по субтитрам = с ними\n\nТакже из контекстного меню.",
		L"Wird bei mehreren Audiospuren angezeigt.\nAudio und Untertitel wahlen, dann Start.\n\nDoppelklick Audio = ohne Untertitel\nDoppelklick Untertitel = mit diesem\n\nAuch uber das Kontextmenu.",
		L"Exibido com varias faixas de audio.\nEscolha audio e legendas e inicie.\n\nDuplo clique no audio = sem legendas\nDuplo clique na legenda = com ela\n\nTambem pelo menu de contexto.",
		L"Verschijnt bij meerdere audiosporen.\nKies audio en ondertitels, start daarna.\n\nDubbelklik audio = zonder ondertitels\nDubbelklik ondertitel = met die ondertitel\n\nOok via het snelmenu.",
		L"Pokazywane przy wielu sciezkach audio.\nWybierz audio i napisy, potem Start.\n\nPodwojne klikniecie audio = bez napisow\nPodwojne klikniecie napisow = z nimi\n\nTakze z menu kontekstowego.",
		L"Birden fazla ses izi varken gosterilir.\nSes ve altyaziyi secip Baslat.\n\nSese cift tik = altyazisiz baslat\nAltyaziya cift tik = o altyaziyla baslat\n\nSag tik menusunden de degistirilebilir."));

	m_audioLbl.SetWindowText(LL14(L"音声", L"Audio", L"Audio", L"Audio", L"Audio", L"오디오", L"音频", L"صوت", L"Аудио", L"Audio", L"Áudio", L"Audio", L"Audio", L"Ses"));
	m_subLbl.SetWindowText(LL14(L"字幕", L"Subtitles", L"Sous-titres", L"Sottotitoli", L"Subtítulos", L"자막", L"字幕", L"ترجمة", L"Субтитры", L"Untertitel", L"Legendas", L"Ondertitels", L"Napisy", L"Altyazı"));
	m_okdummy.SetWindowText(LL14(L"再生開始", L"Start", L"Démarrer", L"Avvia", L"Iniciar", L"재생 시작", L"开始播放", L"بدء", L"Старт", L"Start", L"Iniciar", L"Start", L"Start", L"Başlat"));

	CCustomControlUtility::BeginDialogToolTip(m_tooltip, this);
	m_tooltip.AddTool(&m_okdummy, LL14(L"選択中の音声と字幕で再生を開始します", L"Start playback with the selected audio and subtitles", L"Demarrer avec l'audio et les sous-titres selectionnes", L"Avvia con audio e sottotitoli selezionati", L"Iniciar con el audio y subtitulos seleccionados", L"선택한 오디오와 자막으로 재생을 시작합니다", L"使用所选音频和字幕开始播放", L"بدء التشغيل بالصوت والترجمة المحددين", L"Запуск с выбранными аудио и субтитрами", L"Wiedergabe mit gewahltem Audio und Untertiteln starten", L"Iniciar com o audio e legendas selecionados", L"Start met geselecteerde audio en ondertitels", L"Uruchom z wybranym audio i napisami", L"Secili ses ve altyaziyla baslat"));
	m_tooltip.AddTool(GetDlgItem(IDC_LIST1), LL14(L"再生する音声ストリーム。ダブルクリックで字幕なし開始。", L"Audio stream to play. Double-click starts with no subtitles.", L"Flux audio. Double-clic = sans sous-titres.", L"Flusso audio. Doppio clic = senza sottotitoli.", L"Flujo de audio. Doble clic = sin subtitulos.", L"재생할 오디오. 더블클릭 시 자막 없이 시작.", L"要播放的音频。双击则以无字幕开始。", L"دفق الصوت. نقر مزدوج=بدون ترجمة.", L"Аудиопоток. Двойной клик = без субтитров.", L"Audiostream. Doppelklick = ohne Untertitel.", L"Fluxo de audio. Duplo clique = sem legendas.", L"Audiostroom. Dubbelklik = zonder ondertitels.", L"Strumien audio. Podwojne klikniecie = bez napisow.", L"Ses akisi. Cift tik = altyazisiz baslat."));
	if (subCount > 0)
		m_tooltip.AddTool(GetDlgItem(IDC_AS_SUBLIST), LL14(L"字幕ストリーム。既定はなし。ダブルクリックでその字幕で開始。", L"Subtitle stream. Default is Off. Double-click starts with that subtitle.", L"Sous-titres. Defaut: aucun. Double-clic pour demarrer avec.", L"Sottotitoli. Predefinito: nessuno. Doppio clic per avviare.", L"Subtitulos. Predeterminado: ninguno. Doble clic para iniciar.", L"자막. 기본은 없음. 더블클릭 시 해당 자막으로 시작.", L"字幕。默认无。双击则以该字幕开始。", L"الترجمة. الافتراضي: بدون. نقر مزدوج للبدء بها.", L"Субтитры. По умолчанию нет. Двойной клик — старт с ними.", L"Untertitel. Standard: aus. Doppelklick startet damit.", L"Legendas. Padrao: nenhuma. Duplo clique inicia com ela.", L"Ondertitels. Standaard: uit. Dubbelklik start ermee.", L"Napisy. Domyslnie brak. Podwojne klikniecie uruchamia z nimi.", L"Altyazi. Varsayilan: yok. Cift tik ile baslat."));
	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 512, 10000);

	for (int i = 0; i < nAudio && i < 40; i++) {
		CString str;
		str.Format(LL14(L"音声%d:%s", L"Audio %d:%s", L"Audio %d:%s", L"Audio %d:%s", L"Audio %d:%s", L"오디오 %d:%s", L"音频%d：%s", L"صوت %d:%s", L"Аудио %d:%s", L"Audio %d:%s", L"Áudio %d:%s", L"Audio %d:%s", L"Dźwięk %d:%s", L"Ses %d:%s"), i + 1, streamname[i]);
		m_lb.AddString(str);
	}

	int sel = PickOsLocaleAudio();
	if (sel < 0) {
		for (int l = 0; l < nAudio && l < 40; l++) {
			const int num = (streamidx[l] >= 0) ? streamidx[l] : (l + au);
			DWORD flags = 0;
			if (iam && SUCCEEDED(iam->Info(num, NULL, &flags, NULL, NULL, NULL, NULL, NULL))) {
				if (flags & (AMSTREAMSELECTINFO_ENABLED | AMSTREAMSELECTINFO_EXCLUSIVE)) {
					sel = l;
					break;
				}
			}
		}
	}
	if (sel < 0) sel = 0;
	if (nAudio > 0)
		m_lb.SetCurSel(sel);
	no = sel;
	subNo = -1;

	if (subCount > 0) {
		m_lbSub.AddString(LL14(L"なし", L"Off", L"Aucun", L"Nessuno", L"Ninguno", L"없음", L"无", L"إيقاف", L"Выкл.", L"Aus", L"Nenhuma", L"Uit", L"Brak", L"Yok"));
		for (int i = 0; i < subCount && i < 40; i++) {
			CString str;
			str.Format(LL14(L"字幕%d:%s", L"Subtitle %d:%s", L"Sous-titre %d:%s", L"Sottotitolo %d:%s", L"Subtítulo %d:%s", L"자막 %d:%s", L"字幕%d：%s", L"ترجمة %d:%s", L"Субтитры %d:%s", L"Untertitel %d:%s", L"Legenda %d:%s", L"Ondertitel %d:%s", L"Napisy %d:%s", L"Altyazı %d:%s"),
				i + 1, streamname2[i]);
			m_lbSub.AddString(str);
		}
		m_lbSub.SetCurSel(0); // 既定: なし
	} else {
		LayoutNoSubtitles();
	}

	return TRUE;
}

BOOL CAudioSelect::PreTranslateMessage(MSG* pMsg)
{
	m_tooltip.RelayEvent(pMsg);
	return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
}
