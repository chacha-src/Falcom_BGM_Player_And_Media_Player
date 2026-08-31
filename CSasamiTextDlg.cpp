#include "stdafx.h"
#include "ogg.h"
#include "CSasamiMidiScoreDlg.h"
#include "CSasamiFmScoreDlg.h"
#include "CSasamiTextDlg.h"
#include "CSasamiFmVoiceDlg.h"
#include "CCustomPopupMenu.h"
#include "OfflineHelp.h"
#include "PlayList.h"
#include "kb_sasami/source/sasami_write.h"
#include <shlobj.h>

extern CPlayList* pl;

CSasamiTextDlg* CSasamiTextDlg::s_inst = NULL;

IMPLEMENT_DYNAMIC(CSasamiTextDlg, CCustomBlurDialogExBase)

CSasamiTextDlg::CSasamiTextDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(CSasamiTextDlg::IDD, pParent)
	, m_modeFm(0), m_b64Expanded(0), m_b64RefreshLock(FALSE)
{
	m_lastOut[0] = 0;
	ScMidiDocClear(&m_midi);
	ScFmDocClear(&m_fm);
}

CSasamiTextDlg::~CSasamiTextDlg() {}

CSasamiTextDlg* CSasamiTextDlg::Instance() { return s_inst; }

void CSasamiTextDlg::OpenOwned(CWnd* owner)
{
	const int created = !(s_inst && ::IsWindow(s_inst->GetSafeHwnd()));
	if (!created) {
		CCC_PresentOwnedHelp(s_inst, owner ? owner : AfxGetMainWnd());
		if (CSasamiMidiScoreDlg* sc = CSasamiMidiScoreDlg::Instance())
			sc->PushDocToText();
		return;
	}
	s_inst = new CSasamiTextDlg(owner);
	if (!s_inst->Create(IDD_SASAMI_TEXT, owner ? owner : AfxGetMainWnd())) {
		delete s_inst;
		s_inst = NULL;
		return;
	}
	if (CSasamiMidiScoreDlg* sc = CSasamiMidiScoreDlg::Instance())
		sc->PushDocToText();
	else {
		const int mmlCch = (int)(SC_TEXT_MAX / sizeof(wchar_t));
		wchar_t* mml = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
			(SIZE_T)mmlCch * sizeof(wchar_t));
		ScMidiDoc* doc = (ScMidiDoc*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ScMidiDoc));
		if (mml && doc && ScSessionLoadLastMidi(doc, mml, mmlCch) && mml[0]) {
			s_inst->m_textFull = mml;
			s_inst->m_b64Expanded = ScMmlContainsVstB64(mml) ? 0 : 1;
			s_inst->ApplyB64ViewToEdit();
		}
		if (doc) {
			ScMidiDocClear(doc);
			HeapFree(GetProcessHeap(), 0, doc);
		}
		if (mml) HeapFree(GetProcessHeap(), 0, mml);
	}
	CCC_PresentOwnedHelp(s_inst, owner ? owner : AfxGetMainWnd());
}

void CSasamiTextDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SASAMI_TEXT_EDIT, m_edit);
	DDX_Control(pDX, IDC_SASAMI_TEXT_COMPILE, m_btnCompile);
	DDX_Control(pDX, IDC_SASAMI_TEXT_SAVE, m_btnSave);
	DDX_Control(pDX, IDC_SASAMI_TEXT_OPEN, m_btnOpen);
	DDX_Control(pDX, IDC_SASAMI_TEXT_NEW, m_btnNew);
	DDX_Control(pDX, IDC_SASAMI_TEXT_PLAY, m_btnPlay);
	DDX_Control(pDX, IDC_SASAMI_TEXT_HELP, m_btnHelp);
	DDX_Control(pDX, IDC_SASAMI_TEXT_SCORE, m_btnScore);
	DDX_Control(pDX, IDC_SASAMI_TEXT_MODE, m_btnMode);
	DDX_Control(pDX, IDC_SASAMI_TEXT_VST, m_btnVst);
	DDX_Control(pDX, IDC_SASAMI_TEXT_EXPORT, m_btnExport);
	DDX_Control(pDX, IDC_SASAMI_TEXT_B64FOLD, m_btnB64Fold);
	DDX_Control(pDX, IDC_SASAMI_TEXT_STATUS, m_status);
}

BEGIN_MESSAGE_MAP(CSasamiTextDlg, CCustomBlurDialogExBase)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_CLOSE()
	ON_WM_DESTROY()
	ON_WM_CONTEXTMENU()
	ON_BN_CLICKED(IDC_SASAMI_TEXT_COMPILE, &CSasamiTextDlg::OnBnClickedCompile)
	ON_BN_CLICKED(IDC_SASAMI_TEXT_SAVE, &CSasamiTextDlg::OnBnClickedSave)
	ON_BN_CLICKED(IDC_SASAMI_TEXT_OPEN, &CSasamiTextDlg::OnBnClickedOpen)
	ON_BN_CLICKED(IDC_SASAMI_TEXT_NEW, &CSasamiTextDlg::OnBnClickedNew)
	ON_BN_CLICKED(IDC_SASAMI_TEXT_PLAY, &CSasamiTextDlg::OnBnClickedPlay)
	ON_BN_CLICKED(IDC_SASAMI_TEXT_HELP, &CSasamiTextDlg::OnBnClickedHelp)
	ON_BN_CLICKED(IDC_SASAMI_TEXT_VST, &CSasamiTextDlg::OnBnClickedInsertVst)
	ON_BN_CLICKED(IDC_SASAMI_TEXT_EXPORT, &CSasamiTextDlg::OnBnClickedExport)
	ON_BN_CLICKED(IDC_SASAMI_TEXT_SCORE, &CSasamiTextDlg::OnBnClickedScore)
	ON_BN_CLICKED(IDC_SASAMI_TEXT_MODE, &CSasamiTextDlg::OnBnClickedMode)
	ON_BN_CLICKED(IDC_SASAMI_TEXT_B64FOLD, &CSasamiTextDlg::OnBnClickedB64Fold)
	ON_EN_CHANGE(IDC_SASAMI_TEXT_EDIT, &CSasamiTextDlg::OnEnChange)
	ON_WM_TIMER()
END_MESSAGE_MAP()

void CSasamiTextDlg::ApplyLang()
{
	CString title = m_modeFm
		? LL14(L"SASAMI テキスト [FM/OPNA]", L"SASAMI Text [FM/OPNA]", L"Texte SASAMI [FM/OPNA]", L"Testo SASAMI [FM/OPNA]", L"Texto SASAMI [FM/OPNA]",
			L"SASAMI 텍스트 [FM/OPNA]", L"SASAMI 文本 [FM/OPNA]", L"نص SASAMI [FM/OPNA]", L"Текст SASAMI [FM/OPNA]", L"SASAMI Text [FM/OPNA]",
			L"Texto SASAMI [FM/OPNA]", L"SASAMI-tekst [FM/OPNA]", L"Tekst SASAMI [FM/OPNA]", L"SASAMI Metin [FM/OPNA]")
		: LL14(L"SASAMI テキスト [MIDI/MICP]", L"SASAMI Text [MIDI/MICP]", L"Texte SASAMI [MIDI/MICP]", L"Testo SASAMI [MIDI/MICP]", L"Texto SASAMI [MIDI/MICP]",
			L"SASAMI 텍스트 [MIDI/MICP]", L"SASAMI 文本 [MIDI/MICP]", L"نص SASAMI [MIDI/MICP]", L"Текст SASAMI [MIDI/MICP]", L"SASAMI Text [MIDI/MICP]",
			L"Texto SASAMI [MIDI/MICP]", L"SASAMI-tekst [MIDI/MICP]", L"Tekst SASAMI [MIDI/MICP]", L"SASAMI Metin [MIDI/MICP]");
	SetWindowText(title);
	m_btnCompile.SetWindowText(LL14(L"コンパイル", L"Compile", L"Compiler", L"Compila", L"Compilar", L"컴파일", L"编译", L"تجميع", L"Собрать", L"Kompilieren", L"Compilar", L"Compileren", L"Kompiluj", L"Derle"));
	m_btnSave.SetWindowText(LL14(L"保存", L"Save", L"Enregistrer", L"Salva", L"Guardar", L"저장", L"保存", L"حفظ", L"Сохранить", L"Speichern", L"Salvar", L"Opslaan", L"Zapisz", L"Kaydet"));
	m_btnOpen.SetWindowText(LL14(L"開く", L"Open", L"Ouvrir", L"Apri", L"Abrir", L"열기", L"打开", L"فتح", L"Открыть", L"Öffnen", L"Abrir", L"Openen", L"Otwórz", L"Aç"));
	if (m_btnNew.GetSafeHwnd())
		m_btnNew.SetWindowText(LL14(L"新規", L"New", L"Nouveau", L"Nuovo", L"Nuevo", L"새로", L"新建", L"جديد", L"Новый", L"Neu", L"Novo", L"Nieuw", L"Nowy", L"Yeni"));
	m_btnPlay.SetWindowText(LL14(L"再生確認", L"Preview", L"Aperçu", L"Anteprima", L"Vista previa", L"미리듣기", L"试听", L"معاينة", L"Просмотр", L"Vorschau", L"Prévia", L"Voorbeeld", L"Podgląd", L"Önizle"));
	if (m_btnScore.GetSafeHwnd()) m_btnScore.SetWindowText(LL14(L"\u8b5c\u9762", L"Score", L"Partition", L"Partitura", L"Partitura", L"Score", L"Score", L"Score", L"Score", L"Partitur", L"Partitura", L"Partituur", L"Partytura", L"Skor"));
	if (m_btnMode.GetSafeHwnd()) m_btnMode.SetWindowText(m_modeFm ? L"->MIDI" : L"->FM");
	m_btnHelp.SetWindowText(LL14(L"ヘルプ", L"Help", L"Aide", L"Guida", L"Ayuda", L"도움말", L"帮助", L"مساعدة", L"Справка", L"Hilfe", L"Ajuda", L"Help", L"Pomoc", L"Yardım"));
	m_btnVst.SetWindowText(m_modeFm
		? LL14(L"FM音色編集", L"FM voice edit", L"Note timbre FM", L"Nota voce FM", L"Nota voz FM", L"FM 음색 메모", L"FM音色备注", L"ملاحظة صوت FM", L"Заметка FM", L"FM-Klang-Notiz", L"Nota voz FM", L"FM-klanknotitie", L"Notatka FM", L"FM ses notu")
		: LL14(L"VST音色挿入", L"Insert VST", L"Insérer VST", L"Inserisci VST", L"Insertar VST", L"VST 삽입", L"插入VST", L"إدراج VST", L"Вставить VST", L"VST einfügen", L"Inserir VST", L"VST invoegen", L"Wstaw VST", L"VST ekle"));
	m_btnExport.SetWindowText(LL14(L"書き出し", L"Export", L"Exporter", L"Esporta", L"Exportar", L"내보내기", L"导出", L"تصدير", L"Экспорт", L"Export", L"Exportar", L"Exporteren", L"Eksport", L"Disa aktar"));
}

BOOL CSasamiTextDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	s_inst = this;
	m_edit.SetAeroMode(FALSE);

	auto flatBtn = [](CCustomStandardButton& b) {
		if (!b.GetSafeHwnd()) return;
		b.SetAeroMode(FALSE);
		b.SetFlat(TRUE);
	};
	flatBtn(m_btnOpen); flatBtn(m_btnSave); flatBtn(m_btnNew); flatBtn(m_btnCompile); flatBtn(m_btnPlay);
	flatBtn(m_btnHelp); flatBtn(m_btnVst); flatBtn(m_btnExport);
	if (m_btnScore.GetSafeHwnd()) flatBtn(m_btnScore);
	if (m_btnMode.GetSafeHwnd()) flatBtn(m_btnMode);
	if (m_btnB64Fold.GetSafeHwnd()) flatBtn(m_btnB64Fold);
	m_status.SetAeroMode(FALSE);
	if (m_btnOpen.GetSafeHwnd()) m_btnOpen.SetIcon(IDI_UI_FOLDER);
	if (m_btnSave.GetSafeHwnd()) m_btnSave.SetIcon(IDI_UI_FILE);
	if (m_btnNew.GetSafeHwnd()) m_btnNew.SetIcon(IDI_CTL_PLUS);
	if (m_btnPlay.GetSafeHwnd()) m_btnPlay.SetIcon(IDI_CTL_PLAY);
	if (m_btnExport.GetSafeHwnd()) m_btnExport.SetIcon(IDI_UI_EXPORT);
	if (m_btnHelp.GetSafeHwnd()) m_btnHelp.SetIcon(IDI_UI_HELP);
	m_edit.SetLimitText(4u * 1024u * 1024u);
	m_edit.SetMmlSyntax(TRUE);
	ApplyLang();
	m_textFull = L"; SASAMI MML / MML3\r\n; #1 channel, t120 tempo, cdefgab>c notes\r\n#1\r\nt120\r\nl4 o4\r\ncdef gab>c\r\n";
	m_b64Expanded = 1;
	ApplyB64ViewToEdit();
	m_status.SetWindowText(LL14(
		L"MIDI: #ch / tBPM / 音符。FMは先頭に OPNA。", L"MIDI: #ch / tBPM / notes. FM: start with OPNA.",
		L"MIDI: #ch / tBPM. FM: OPNA.", L"MIDI: #ch / tBPM. FM: OPNA.", L"MIDI: #ch / tBPM. FM: OPNA.",
		L"MIDI: #ch / tBPM. FM: OPNA.", L"MIDI: #ch / tBPM。FM: OPNA。", L"MIDI: #ch / tBPM. FM: OPNA.",
		L"MIDI: #ch / tBPM. FM: OPNA.", L"MIDI: #ch / tBPM. FM: OPNA.", L"MIDI: #ch / tBPM. FM: OPNA.",
		L"MIDI: #ch / tBPM. FM: OPNA.", L"MIDI: #ch / tBPM. FM: OPNA.", L"MIDI: #ch / tBPM. FM: OPNA."));
	LayoutChrome();
	RestoreUiGeom();
	ColorizeEdit();
	SetupTooltips();
	UpdateB64FoldButton();
	return TRUE;
}

void CSasamiTextDlg::LayoutChrome()
{
	if (!::IsWindow(m_hWnd)) return;
	CRect rc;
	GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	const int pad = 8;
	const int btnH = 28;
	const int btnW = 88;
	int y = cap + pad;
	int x = pad;
	auto place = [&](CWnd& w, int ww) {
		if (w.GetSafeHwnd()) { w.MoveWindow(x, y, ww, btnH); x += ww + 4; }
	};
	place(m_btnOpen, btnW);
	place(m_btnSave, btnW);
	place(m_btnNew, 64);
	place(m_btnCompile, btnW);
	place(m_btnPlay, btnW + 12);
	place(m_btnVst, btnW + 24);
	place(m_btnExport, btnW);
	place(m_btnHelp, 64);
	place(m_btnScore, 64);
	place(m_btnMode, 72);
	place(m_btnB64Fold, 72);
	y += btnH + 6;
	if (m_status.GetSafeHwnd())
		m_status.MoveWindow(pad, y, max(80, rc.Width() - pad * 2), 22);
	y += 26;
	m_bodyRc.SetRect(pad, y, rc.Width() - pad, rc.Height() - pad);
	if (m_edit.GetSafeHwnd()) m_edit.MoveWindow(m_bodyRc);
}

void CSasamiTextDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	if (m_edit.GetSafeHwnd()) LayoutChrome();
}

BOOL CSasamiTextDlg::OnEraseBkgnd(CDC* pDC)
{
	if (!pDC) return TRUE;
	CRect rc;
	GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	CRect body(0, cap, rc.right, rc.bottom);
	if (body.Height() > 0) {
#if CCUSTOM_AERO_SUPPORT
		CCC_FillRectAlpha(pDC->GetSafeHdc(), body, RGB(240, 240, 245), 255);
#else
		pDC->FillSolidRect(&body, RGB(240, 240, 245));
#endif
	}
	return TRUE;
}

void CSasamiTextDlg::OnPaint()
{
	CPaintDC dc(this);
	CRect rc;
	GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	CRect body(0, cap, rc.right, rc.bottom);
	if (body.Height() > 0) {
#if CCUSTOM_AERO_SUPPORT
		CCC_FillRectAlpha(dc.GetSafeHdc(), body, RGB(240, 240, 245), 255);
#else
		dc.FillSolidRect(&body, RGB(240, 240, 245));
#endif
	}
	CCC_CaptionPaint(dc, m_hWnd);
}

void CSasamiTextDlg::RefreshChromeOpaque()
{
	if (!::IsWindow(m_hWnd)) return;
	/* Re-force solid buttons after status/label updates poke acrylic holes. */
	auto flatBtn = [](CCustomStandardButton& b) {
		if (!b.GetSafeHwnd()) return;
		b.SetAeroMode(FALSE);
		b.SetFlat(TRUE);
		b.Invalidate(FALSE);
	};
	flatBtn(m_btnOpen); flatBtn(m_btnSave); flatBtn(m_btnNew); flatBtn(m_btnCompile); flatBtn(m_btnPlay);
	flatBtn(m_btnHelp); flatBtn(m_btnVst); flatBtn(m_btnExport);
	if (m_btnScore.GetSafeHwnd()) flatBtn(m_btnScore);
	if (m_btnMode.GetSafeHwnd()) flatBtn(m_btnMode);
	if (m_btnB64Fold.GetSafeHwnd()) flatBtn(m_btnB64Fold);
	if (m_status.GetSafeHwnd()) {
		m_status.SetAeroMode(FALSE);
		m_status.Invalidate(FALSE);
	}
	CRect rc;
	GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	CRect chrome(0, cap, rc.right, m_bodyRc.top > cap ? m_bodyRc.top : (cap + 70));
	InvalidateRect(&chrome, FALSE);
	RedrawWindow(&chrome, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}


void CSasamiTextDlg::SetupTooltips()
{
	if (!CCustomControlUtility::BeginDialogToolTip(m_tooltip, this, TTS_NOPREFIX)) return;
	auto tip = [&](CWnd& w, LPCWSTR s) { if (w.GetSafeHwnd()) m_tooltip.AddTool(&w, s); };
	tip(m_btnOpen, L"Open MML/DAT/text");
	tip(m_btnSave, L"Save As MML/text (binary = Export)");
	tip(m_btnNew, L"New — clear text / score session");
	tip(m_btnCompile, L"Compile to temp FPY/MPW");
	tip(m_btnPlay, L"Compile and preview via playlist");
	tip(m_btnVst, L"FM: open voice editor (params + preview). MIDI: insert @VST commands");
	tip(m_btnExport, L"Export compiled binary / audio");
	tip(m_btnHelp, L"Help");
	tip(m_btnScore, L"Open staff score for current mode");
	tip(m_btnMode, L"Toggle MIDI/MICP <-> FM/OPNA (auto-detect on compile)");
	tip(m_btnB64Fold, L"Fold/unfold @VSTSTATEB64 and @VSTCTRLB64 blocks");
	tip(m_status, L"Status / compile errors");
	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip);
}

BOOL CSasamiTextDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}

void CSasamiTextDlg::ColorizeEdit()
{
	if (m_edit.GetSafeHwnd()) m_edit.Invalidate(FALSE);
}

void CSasamiTextDlg::SyncTextFullFromEdit()
{
	if (!m_edit.GetSafeHwnd()) return;
	if (!m_b64Expanded) return;
	m_edit.GetWindowText(m_textFull);
}

void CSasamiTextDlg::ApplyB64ViewToEdit()
{
	if (!m_edit.GetSafeHwnd()) return;
	m_b64RefreshLock = TRUE;
	if (m_modeFm || m_b64Expanded || !ScMmlContainsVstB64(m_textFull)) {
		m_edit.SetWindowText(m_textFull);
	} else {
		const int bufCch = (int)(SC_TEXT_MAX / sizeof(wchar_t));
		wchar_t* collapsed = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
			(SIZE_T)bufCch * sizeof(wchar_t));
		if (collapsed && ScMmlCollapseVstB64(m_textFull, collapsed, bufCch))
			m_edit.SetWindowText(collapsed);
		else
			m_edit.SetWindowText(m_textFull);
		if (collapsed) HeapFree(GetProcessHeap(), 0, collapsed);
	}
	m_b64RefreshLock = FALSE;
	ColorizeEdit();
	UpdateB64FoldButton();
}

void CSasamiTextDlg::UpdateB64FoldButton()
{
	if (!m_btnB64Fold.GetSafeHwnd()) return;
	const int show = (!m_modeFm && ScMmlContainsVstB64(m_textFull)) ? SW_SHOW : SW_HIDE;
	m_btnB64Fold.ShowWindow(show);
	if (show == SW_HIDE) return;
	m_btnB64Fold.SetWindowText(m_b64Expanded
		? LL14(L"B64▼", L"B64 expand", L"B64 ouvrir", L"B64 apri", L"B64 abrir", L"B64 펼침", L"B64展开", L"B64 فتح", L"B64 раскрыть", L"B64 auf", L"B64 abrir", L"B64 open", L"B64 rozwin", L"B64 ac")
		: LL14(L"B64▶", L"B64 fold", L"B64 plier", L"B64 piega", L"B64 plegar", L"B64 접기", L"B64折叠", L"B64 طي", L"B64 свернуть", L"B64 zu", L"B64 dobrar", L"B64 dicht", L"B64 zwin", L"B64 katla"));
}

void CSasamiTextDlg::ReloadFullTextFromScore()
{
	CSasamiMidiScoreDlg* sc = CSasamiMidiScoreDlg::Instance();
	if (!sc || !::IsWindow(sc->GetSafeHwnd())) return;
	const int mmlCch = (int)(SC_TEXT_MAX / sizeof(wchar_t));
	wchar_t* mml = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		(SIZE_T)mmlCch * sizeof(wchar_t));
	if (!mml) return;
	if (ScMidiDocToMmlFull(sc->Doc(), mml, mmlCch))
		m_textFull = mml;
	HeapFree(GetProcessHeap(), 0, mml);
}

void CSasamiTextDlg::ScheduleScoreSync()
{
	if (m_modeFm || m_b64RefreshLock) return;
	KillTimer(kTextScoreSyncTimer);
	SetTimer(kTextScoreSyncTimer, 600, NULL);
}

void CSasamiTextDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == kTextScoreSyncTimer) {
		KillTimer(kTextScoreSyncTimer);
		if (!m_modeFm && !m_b64RefreshLock)
			PushTextToScore();
		return;
	}
	CCustomBlurDialogExBase::OnTimer(nIDEvent);
}

void CSasamiTextDlg::OnBnClickedB64Fold()
{
	if (m_modeFm) return;
	if (!ScMmlContainsVstB64(m_textFull)) {
		ReloadFullTextFromScore();
		if (!ScMmlContainsVstB64(m_textFull)) return;
	}
	if (m_b64Expanded) {
		SyncTextFullFromEdit();
		m_b64Expanded = 0;
	} else {
		ReloadFullTextFromScore();
		m_b64Expanded = 1;
	}
	ApplyB64ViewToEdit();
	CString st;
	st.Format(L"%s @VSTSTATEB64 / @VSTCTRLB64",
		m_b64Expanded
			? LL14(L"展開中 — 編集可", L"Expanded — editable", L"Ouvert", L"Aperto", L"Abierto", L"펼침", L"已展开", L"مفتوح", L"Развернуто", L"Aufgeklappt", L"Aberto", L"Open", L"Rozwiniete", L"Acik")
			: LL14(L"折りたたみ中 — 展開で編集", L"Folded — expand to edit blobs", L"Replié", L"Ripiegato", L"Plegado", L"접힘", L"已折叠", L"مطوي", L"Свернуто", L"Eingeklappt", L"Dobrado", L"Ingeklapt", L"Zwiniete", L"Katli"));
	m_status.SetWindowText(st);
	RefreshChromeOpaque();
}

void CSasamiTextDlg::OnEnChange()
{
	if (m_b64RefreshLock) return;
	if (!m_b64Expanded && !m_modeFm && ScMmlContainsVstB64(m_textFull)) {
		m_b64Expanded = 1;
		ApplyB64ViewToEdit();
	}
	SyncTextFullFromEdit();
	ColorizeEdit();
	UpdateB64FoldButton();
	if (!m_modeFm && !m_b64RefreshLock)
		ScheduleScoreSync();
}

int CSasamiTextDlg::CompileAndBuild(wchar_t* outPath, int outCch, int* isFm)
{
	SyncTextFullFromEdit();
	CString text = m_textFull;
	wchar_t err[256];
	int errLine = 0;
	/* Mode button is authoritative. Only explicit file tags may override. */
	if (text.Find(L";MIDI") >= 0 || text.Find(L";midi") >= 0 || text.Find(L";MICP") >= 0)
		m_modeFm = 0;
	else if (text.Find(L";FM") >= 0 || text.Find(L";fm") >= 0
		|| text.Find(L"OPNA") >= 0 || text.Find(L"opna") >= 0)
		m_modeFm = 1;
	/* Do NOT sniff MICP/[n] here — that blocked intentional ->FM / FPY export. */
	ApplyLang();
	uint8_t* bin = (uint8_t*)HeapAlloc(GetProcessHeap(), 0, SASAMI_WRITE_MAX);
	if (!bin) return 0;
	uint32_t sz = 0;
	if (m_modeFm) {
		if (!ScCompileFmText(text, &m_fm, &errLine, err, 256)) {
			CString s;
			s.Format(L"L%d: %s", errLine, err);
			m_status.SetWindowText(s);
			HeapFree(GetProcessHeap(), 0, bin);
			RefreshChromeOpaque();
			return 0;
		}
		SasamiWriteFm* w = (SasamiWriteFm*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SasamiWriteFm));
		if (!w) { HeapFree(GetProcessHeap(), 0, bin); return 0; }
		if (!ScFmDocToWrite(&m_fm, w)) {
			HeapFree(GetProcessHeap(), 0, w);
			m_status.SetWindowText(LL14(L"FPY\u69cb\u7bc9\u5931\u6557", L"FPY build failed", L"Échec FPY", L"FPY fallito", L"FPY falló", L"FPY 실패", L"FPY失败", L"فشل FPY", L"Ошибка FPY", L"FPY fehlgeschlagen", L"Falha FPY", L"FPY mislukt", L"Błąd FPY", L"FPY başarısız"));
			HeapFree(GetProcessHeap(), 0, bin);
			RefreshChromeOpaque();
			return 0;
		}
		sz = SasamiBuildFpy(w, bin, SASAMI_WRITE_MAX);
		SasamiWriteFmClear(w);
		HeapFree(GetProcessHeap(), 0, w);
		if (isFm) *isFm = 1;
	} else {
		if (!ScCompileMidiMml(text, &m_midi, &errLine, err, 256)) {
			CString s;
			s.Format(L"L%d: %s", errLine, err);
			m_status.SetWindowText(s);
			HeapFree(GetProcessHeap(), 0, bin);
			RefreshChromeOpaque();
			return 0;
		}
		SasamiWriteMidi* w = (SasamiWriteMidi*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SasamiWriteMidi));
		if (!w) { HeapFree(GetProcessHeap(), 0, bin); return 0; }
		if (!ScMidiDocToWrite(&m_midi, w)) {
			HeapFree(GetProcessHeap(), 0, w);
			m_status.SetWindowText(LL14(L"MPY\u69cb\u7bc9\u5931\u6557", L"MPY build failed", L"Échec MPY", L"MPY fallito", L"MPY falló", L"MPY 실패", L"MPY失败", L"فشل MPY", L"Ошибка MPY", L"MPY fehlgeschlagen", L"Falha MPY", L"MPY mislukt", L"Błąd MPY", L"MPY başarısız"));
			HeapFree(GetProcessHeap(), 0, bin);
			RefreshChromeOpaque();
			return 0;
		}
		if (w->isMpw3)
			sz = SasamiBuildMpw3(w, bin, SASAMI_WRITE_MAX);
		else
			sz = SasamiBuildMpy(w, bin, SASAMI_WRITE_MAX); /* classic MPY like DO--.MPY */
		SasamiWriteMidiClear(w);
		HeapFree(GetProcessHeap(), 0, w);
		if (isFm) *isFm = 0;
	}
	if (!sz) {
		m_status.SetWindowText(LL14(L"\u51fa\u529b\u30b5\u30a4\u30ba0", L"Output size 0", L"Taille 0", L"Dimensione 0", L"Tamaño 0", L"크기 0", L"大小为0", L"الحجم 0", L"Размер 0", L"Größe 0", L"Tamanho 0", L"Grootte 0", L"Rozmiar 0", L"Boyut 0"));
		HeapFree(GetProcessHeap(), 0, bin);
		RefreshChromeOpaque();
		return 0;
	}
	wchar_t dir[MAX_PATH];
	GetTempPathW(MAX_PATH, dir);
	if (m_modeFm) {
		_snwprintf_s(outPath, outCch, _TRUNCATE, L"%sogg_sasami_fm.fpy", dir);
		/* Also refresh preview.fpy so old path keeps matching the latest compile. */
		wchar_t prevPath[MAX_PATH];
		_snwprintf_s(prevPath, _TRUNCATE, L"%sogg_sasami_preview.fpy", dir);
		SasamiWriteFileW(prevPath, bin, sz);
	} else if (m_midi.bind.isMpw3)
		_snwprintf_s(outPath, outCch, _TRUNCATE, L"%sogg_sasami_preview.mpsmv", dir);
	else
		_snwprintf_s(outPath, outCch, _TRUNCATE, L"%sogg_sasami_preview.mpy", dir);
	if (!SasamiWriteFileW(outPath, bin, sz)) {
		m_status.SetWindowText(LL14(L"\u66f8\u304d\u8fbc\u307f\u5931\u6557", L"Write failed", L"Écriture échouée", L"Scrittura fallita", L"Escritura fallida", L"쓰기 실패", L"写入失败", L"فشل الكتابة", L"Ошибка записи", L"Schreiben fehlgeschlagen", L"Falha ao gravar", L"Schrijven mislukt", L"Zapis nieudany", L"Yazma başarısız"));
		HeapFree(GetProcessHeap(), 0, bin);
		RefreshChromeOpaque();
		return 0;
	}
	if (!m_modeFm && !m_midi.bind.isMpw3) {
		wchar_t alt[MAX_PATH];
		_snwprintf_s(alt, _TRUNCATE, L"%sogg_sasami_preview.mpw2", dir);
		SasamiWriteFileW(alt, bin, sz);
	}
	wcsncpy_s(m_lastOut, outPath, _TRUNCATE);
	HeapFree(GetProcessHeap(), 0, bin);
	CString ok;
	if (m_modeFm) {
		const uint32_t mx = ScFmDocMaxTick(&m_fm);
		const int notes = ScFmDocNoteCount(&m_fm);
		int nLoop = 0, nJump = 0;
		for (int i = 0; i < m_fm.evCount; i++) {
			if (m_fm.ev[i].kind == SC_EV_FM_LOOP_START) nLoop++;
			if (m_fm.ev[i].kind == SC_EV_FM_JUMP) nJump++;
		}
		const int bpm = (int)((13000.0 * 120.0) / (double)max(1, m_fm.tempoT) + 0.5);
		const double sec1 = (mx / (double)SC_PPQN) * (60.0 / max(1, bpm));
		ok.Format(L"OK %uB notes=%d loop|=%d J=%d onePass~%.1fs @%dBPM (native |: cmd13/14) %s",
			sz, notes, nLoop, nJump, sec1, bpm, outPath);
	} else {
		int nLoop = 0, nJump = 0, notes = 0;
		for (int i = 0; i < m_midi.evCount; i++) {
			if (m_midi.ev[i].kind == SC_EV_FM_LOOP_START) nLoop++;
			if (m_midi.ev[i].kind == SC_EV_FM_JUMP) nJump++;
			if (m_midi.ev[i].kind == SC_EV_NOTE) notes++;
		}
		ok.Format(L"OK %uB notes=%d loop|=%d J=%d [%s] (native |: cmd23/24) %s",
			sz, notes, nLoop, nJump,
			m_midi.bind.isMpw3 ? L"MPSMV" : L"MPY", outPath);
	}
	m_status.SetWindowText(ok);
	RefreshChromeOpaque();
	return 1;
}

void CSasamiTextDlg::OnBnClickedCompile()
{
	wchar_t path[MAX_PATH];
	int isFm = 0;
	if (CompileAndBuild(path, MAX_PATH, &isFm) && !isFm) {
		PushTextToScore();
		PersistSession();
	}
}

void CSasamiTextDlg::OnBnClickedSave()
{
	SyncTextFullFromEdit();
	CString text = m_textFull;
	if (CSasamiMidiScoreDlg* sc = CSasamiMidiScoreDlg::Instance()) {
		if (::IsWindow(sc->GetSafeHwnd()) && ScMmlContainsVstB64(text)) {
			const int mmlCch = (int)(SC_TEXT_MAX / sizeof(wchar_t));
			wchar_t* full = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
				(SIZE_T)mmlCch * sizeof(wchar_t));
			if (full && ScMidiDocToMmlFull(sc->Doc(), full, mmlCch))
				text = full;
			if (full) HeapFree(GetProcessHeap(), 0, full);
		}
	}
	CFileDialog dlg(FALSE, m_modeFm ? L"f" : L"mml",
		m_modeFm ? L"song.f" : L"song.mml",
		OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
		L"MML/Text (*.mml;*.txt;*.dat;*.f)|*.mml;*.txt;*.dat;*.f|All|*.*||",
		this);
	if (dlg.DoModal() != IDOK) return;
	if (!ScSaveTextFileW(dlg.GetPathName(), text))
		m_status.SetWindowText(L"Save MML failed");
	else {
		m_status.SetWindowText(dlg.GetPathName());
		RefreshChromeOpaque();
		if (!m_modeFm) {
			PushTextToScore();
			PersistSession();
		}
	}
}

void CSasamiTextDlg::OnBnClickedOpen()
{
	CFileDialog dlg(TRUE, L"mml", NULL, OFN_FILEMUSTEXIST,
		L"Text/MML/DAT (*.mml;*.mml2;*.mml3;*.m;*.txt;*.dat;*.f)|*.mml;*.mml2;*.mml3;*.m;*.txt;*.dat;*.f|All|*.*||", this);
	if (dlg.DoModal() != IDOK) return;
	const int bufCch = (int)(SC_TEXT_MAX / sizeof(wchar_t));
	wchar_t* buf = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		(SIZE_T)bufCch * sizeof(wchar_t));
	if (!buf) return;
	CString path = dlg.GetPathName();
	int ok = 0;
	if (path.Right(2).CompareNoCase(L".f") == 0)
		ok = ScExtractOldFToText(path, buf, bufCch);
	else
		ok = ScLoadTextFileW(path, buf, bufCch);
	if (!ok) {
		HeapFree(GetProcessHeap(), 0, buf);
		m_status.SetWindowText(LL14(L"\u8aad\u8fbc\u5931\u6557", L"Load failed", L"Échec lecture", L"Caricamento fallito", L"Carga fallida", L"읽기 실패", L"读取失败", L"فشل التحميل", L"Ошибка чтения", L"Laden fehlgeschlagen", L"Falha ao carregar", L"Laden mislukt", L"Wczytywanie nieudane", L"Yükleme başarısız"));
		return;
	}
	m_edit.SetWindowText(buf);
	m_textFull = buf;
	m_b64Expanded = ScMmlContainsVstB64(buf) ? 0 : 1;
	ApplyB64ViewToEdit();
	/* .dat: PC98/OPNA dumps with @n → FM by default. @VST / ;MIDI → MIDI. */
	m_modeFm = 0;
	CString low = path; low.MakeLower();
	const int dot = low.ReverseFind(L'.');
	CString ext = (dot >= 0) ? low.Mid(dot) : L"";
	CString body(buf);
	HeapFree(GetProcessHeap(), 0, buf);
	const int looksFm = (body.Find(L"OPNA") >= 0 || body.Find(L"opna") >= 0 || body.Find(L";FM") >= 0 || body.Find(L";fm") >= 0);
	const int looksVst = (body.Find(L"@VST") >= 0 || body.Find(L"@PROG") >= 0);
	if (ext == L".f" || ext == L".fpy")
		m_modeFm = 1;
	else if (ext == L".m" || ext == L".mml" || ext == L".mml2" || ext == L".mml3" || ext == L".micp")
		m_modeFm = 0;
	else if (ext == L".dat")
		m_modeFm = looksVst ? 0 : 1;
	if (looksFm) m_modeFm = 1;
	if (body.Find(L";MIDI") >= 0 || body.Find(L";MICP") >= 0 || body.Find(L";midi") >= 0)
		m_modeFm = 0;
	ApplyLang();
	CString st;
	st.Format(L"%s  [%s]  (use ->MIDI / ->FM to switch)", (LPCTSTR)path, m_modeFm ? L"FM" : L"MIDI/MICP");
	m_status.SetWindowText(st);
	ColorizeEdit();
	RefreshChromeOpaque();
}

void CSasamiTextDlg::OnBnClickedPlay()
{
	wchar_t path[MAX_PATH];
	int isFm = 0;
	if (!CompileAndBuild(path, MAX_PATH, &isFm)) return;
	if (!pl) {
		m_status.SetWindowText(LL14(L"プレイリスト未初期化", L"Playlist not ready", L"Liste non prête", L"Lista non pronta", L"Lista no lista", L"목록 없음", L"列表未就绪", L"القائمة غير جاهزة", L"Плейлист не готов", L"Playlist nicht bereit", L"Lista não pronta", L"Afspeellijst niet klaar", L"Lista niegotowa", L"Liste hazır değil"));
		return;
	}
	pl->AddFilePath(path);
	m_status.SetWindowText(LL14(L"プレイリストに追加しました", L"Added to playlist", L"Ajouté à la liste", L"Aggiunto alla lista", L"Añadido a la lista", L"목록에 추가됨", L"已加入列表", L"أُضيف للقائمة", L"Добавлено в плейлист", L"Zur Playlist hinzugefügt", L"Adicionado à lista", L"Toegevoegd aan lijst", L"Dodano do listy", L"Listeye eklendi"));
}

void CSasamiTextDlg::OnBnClickedHelp()
{
	OfflineHelpOpenTopic(m_hWnd, L"sasami-composer");
}

void CSasamiTextDlg::OnBnClickedExport()
{
	wchar_t path[MAX_PATH];
	int isFm = 0;
	if (!CompileAndBuild(path, MAX_PATH, &isFm)) return;
	ScOpenAudioExport(this, path);
}

void CSasamiTextDlg::OnBnClickedInsertVst()
{
	if (m_modeFm) {
		uint8_t v[25];
		memset(v, 0, sizeof(v));
		if (m_fm.voiceCount > 0)
			memcpy(v, m_fm.voices[0], 25);
		CSasamiFmVoiceDlg dlg(this);
		if (dlg.DoEdit(this, v) == IDOK) {
			if (m_fm.voiceCount < 1) m_fm.voiceCount = 1;
			memcpy(m_fm.voices[0], v, 25);
			m_status.SetWindowText(L"FM voice updated (slot 0). Preview in editor, then compile.");
		}
		RefreshChromeOpaque();
		return;
	}
	CFileDialog dlg(TRUE, L"vst3", NULL, OFN_FILEMUSTEXIST,
		L"VST (*.vst3;*.dll)|*.vst3;*.dll|All|*.*||", this);
	CString path;
	if (dlg.DoModal() == IDOK)
		path = dlg.GetPathName();
	CString cur;
	m_edit.GetWindowText(cur);
	CString ins;
	if (!path.IsEmpty())
		ins.Format(L"\r\n@VST\"%s\"\r\n@PROG 0\r\n@BANK 0 0\r\n", (LPCTSTR)path);
	else
		ins = L"\r\n@VST\"\"\r\n@PROG 0\r\n@BANK 0 0\r\n";
	cur += ins;
	m_textFull = cur;
	if (!m_b64Expanded && ScMmlContainsVstB64(m_textFull))
		ApplyB64ViewToEdit();
	else {
		m_edit.SetWindowText(cur);
		SyncTextFullFromEdit();
	}
	ColorizeEdit();
	m_status.SetWindowText(L"Inserted @VST/@PROG/@BANK (also assign Tone gauge on MIDI score)");
}

void CSasamiTextDlg::OnContextMenu(CWnd* pWnd, CPoint point)
{
	CCustomPopupMenu menu;
	menu.SetAeroMode(FALSE);
	menu.AddCommand(IDC_SASAMI_TEXT_COMPILE, LL14(L"コンパイル", L"Compile", L"Compiler", L"Compila", L"Compilar", L"컴파일", L"编译", L"تجميع", L"Собрать", L"Kompilieren", L"Compilar", L"Compileren", L"Kompiluj", L"Derle"));
	menu.AddCommand(IDC_SASAMI_TEXT_EXPORT, LL14(L"音声書き出し…", L"Audio export…", L"Export audio…", L"Esporta audio…", L"Exportar audio…", L"오디오 내보내기…", L"导出音频…", L"تصدير صوت…", L"Экспорт аудио…", L"Audio export…", L"Exportar audio…", L"Audio exporteren…", L"Eksport audio…", L"Ses disa aktar…"));
	CCustomPopupMenu* sub = menu.AddSubMenu(LL14(L"挿入", L"Insert", L"Insérer", L"Inserisci", L"Insertar", L"삽입", L"插入", L"إدراج", L"Вставка", L"Einfügen", L"Inserir", L"Invoegen", L"Wstaw", L"Ekle"), L"");
	if (sub)
		sub->AddCommand(IDC_SASAMI_TEXT_VST, LL14(L"VSTコマンド", L"VST commands", L"Commandes VST", L"Comandi VST", L"Comandos VST", L"VST 명령", L"VST命令", L"أوامر VST", L"Команды VST", L"VST-Befehle", L"Comandos VST", L"VST-opdrachten", L"Polecenia VST", L"VST komutları"));
	menu.AddCommand(IDC_SASAMI_TEXT_B64FOLD, LL14(L"B64折畳/展開", L"B64 fold/expand", L"B64 plier/ouvrir", L"B64 piega/apri", L"B64 plegar/abrir", L"B64 접기/펼침", L"B64折叠/展开", L"B64 طي/فتح", L"B64 свернуть/развернуть", L"B64 zu/auf", L"B64 dobrar/abrir", L"B64 dicht/open", L"B64 zwin/rozwin", L"B64 katla/ac"));
	menu.AddSeparator();
	menu.AddCommand(IDC_SASAMI_TEXT_HELP, LL14(L"ヘルプ", L"Help", L"Aide", L"Guida", L"Ayuda", L"도움말", L"帮助", L"مساعدة", L"Справка", L"Hilfe", L"Ajuda", L"Help", L"Pomoc", L"Yardım"));
	const UINT cmd = menu.Track(point, this);
	if (cmd) PostMessage(WM_COMMAND, cmd);
}

void CSasamiTextDlg::OnClose()
{
	KillTimer(kTextScoreSyncTimer);
	PersistSession();
	/* Avoid huge ScMidiDoc on stack while tearing down — sync only if score lives. */
	if (!m_modeFm) {
		if (CSasamiMidiScoreDlg* sc = CSasamiMidiScoreDlg::Instance()) {
			if (::IsWindow(sc->GetSafeHwnd()))
				PushTextToScore();
		}
	}
	DestroyWindow();
}

void CSasamiTextDlg::OnDestroy()
{
	CCustomBlurDialogExBase::OnDestroy();
}

void CSasamiTextDlg::PostNcDestroy()
{
	if (s_inst == this) s_inst = NULL;
	CCustomBlurDialogExBase::PostNcDestroy();
	delete this;
}


void CSasamiTextDlg::OnBnClickedScore()
{
	if (!m_modeFm)
		PushTextToScore();
	if (m_modeFm)
		CSasamiFmScoreDlg::OpenOwned(this);
	else
		CSasamiMidiScoreDlg::OpenOwned(this);
}

void CSasamiTextDlg::OnBnClickedNew()
{
	NewDocument();
}

CString CSasamiTextDlg::GetMmlText() const
{
	if (m_b64Expanded && m_edit.GetSafeHwnd()) {
		CString t;
		m_edit.GetWindowText(t);
		return t;
	}
	return m_textFull;
}

void CSasamiTextDlg::SetFmTextFromScore(const wchar_t* text)
{
	if (!text || !m_edit.GetSafeHwnd()) return;
	m_modeFm = 1;
	m_textFull = text;
	m_b64Expanded = 1;
	ApplyB64ViewToEdit();
	ApplyLang();
	ColorizeEdit();
	RefreshChromeOpaque();
	m_status.SetWindowText(L"Text synced from FM score [FM/OPNA]");
}

void CSasamiTextDlg::SetMmlFromScore(const wchar_t* mml)
{
	if (!mml || !m_edit.GetSafeHwnd()) return;
	m_textFull = mml;
	m_b64Expanded = 0;
	ApplyB64ViewToEdit();
	int tracks = 0;
	for (int i = 0; mml[i]; i++)
		if (mml[i] == L'[' && (i == 0 || mml[i - 1] == L'\n' || mml[i - 1] == L'\r'))
			tracks++;
	CString st;
	st.Format(L"Text synced from score (%d track block(s); B64 folded)", tracks);
	m_status.SetWindowText(st);
}

void CSasamiTextDlg::NewDocument()
{
	m_textFull = L"; SASAMI MML / MML3\r\n@METER 4/4\r\n#1\r\nt120\r\nl4 o4\r\n";
	m_b64Expanded = 1;
	ApplyB64ViewToEdit();
	ScMidiDocClear(&m_midi);
	ScFmDocClear(&m_fm);
	ScSessionClearLast();
	if (CSasamiMidiScoreDlg* sc = CSasamiMidiScoreDlg::Instance()) {
		if (::IsWindow(sc->GetSafeHwnd())) {
			ScMidiDoc* empty = (ScMidiDoc*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ScMidiDoc));
			if (empty) {
				empty->tempoT = 13000;
				sc->LoadFromDoc(*empty);
				HeapFree(GetProcessHeap(), 0, empty);
			}
		}
	}
	m_status.SetWindowText(L"New document");
	ColorizeEdit();
}

void CSasamiTextDlg::PersistUiGeom()
{
	ScSaveWndGeom(this, &savedata.sasamiTextX, &savedata.sasamiTextY,
		&savedata.sasamiTextW, &savedata.sasamiTextH);
}

void CSasamiTextDlg::RestoreUiGeom()
{
	if (!ScRestoreWndGeom(this, savedata.sasamiTextX, savedata.sasamiTextY,
		savedata.sasamiTextW, savedata.sasamiTextH, 640, 400))
		SetWindowPos(NULL, 0, 0, 900, 640, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	LayoutChrome();
}

void CSasamiTextDlg::PersistSession()
{
	PersistUiGeom();
	if (m_modeFm) return;
	SyncTextFullFromEdit();
	CString text = m_textFull;
	ScMidiDoc* doc = (ScMidiDoc*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ScMidiDoc));
	if (!doc) {
		ScSessionSaveLastMidi(&m_midi, text);
		return;
	}
	wchar_t err[128];
	int errLine = 0;
	if (ScCompileMidiMml(text, doc, &errLine, err, 128))
		ScSessionSaveLastMidi(doc, text);
	else
		ScSessionSaveLastMidi(&m_midi, text);
	ScMidiDocClear(doc);
	HeapFree(GetProcessHeap(), 0, doc);
}

void CSasamiTextDlg::PushTextToScore()
{
	if (m_modeFm) return;
	SyncTextFullFromEdit();
	CString text = m_textFull;
	ScMidiDoc* doc = (ScMidiDoc*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ScMidiDoc));
	if (!doc) return;
	wchar_t err[128];
	int errLine = 0;
	if (!ScCompileMidiMml(text, doc, &errLine, err, 128)) {
		HeapFree(GetProcessHeap(), 0, doc);
		if (CSasamiMidiScoreDlg* sc = CSasamiMidiScoreDlg::Instance()) {
			if (::IsWindow(sc->GetSafeHwnd())) {
				CString s;
				s.Format(L"Text→score sync failed L%d: %s", errLine, err);
				m_status.SetWindowText(s);
			}
		}
		return;
	}
	if (CSasamiMidiScoreDlg* sc = CSasamiMidiScoreDlg::Instance()) {
		if (::IsWindow(sc->GetSafeHwnd()))
			ScMidiDocMergeVstBind(doc, sc->Doc());
	}
	ScMidiDocClear(&m_midi);
	m_midi = *doc;
	memset(&doc->bind, 0, sizeof(doc->bind));
	doc->evCount = 0;
	HeapFree(GetProcessHeap(), 0, doc);
	if (CSasamiMidiScoreDlg* sc = CSasamiMidiScoreDlg::Instance()) {
		if (::IsWindow(sc->GetSafeHwnd())) {
			sc->LoadFromDoc(m_midi);
			m_status.SetWindowText(L"Score synced from text");
		}
	}
}

void CSasamiTextDlg::OnBnClickedMode()
{
	SyncTextFullFromEdit();
	m_modeFm = m_modeFm ? 0 : 1;
	if (m_modeFm)
		m_b64Expanded = 1;
	else if (ScMmlContainsVstB64(m_textFull))
		m_b64Expanded = 0;
	ApplyLang();
	ApplyB64ViewToEdit();
	CString s;
	s.Format(L"Mode: %s  (compile uses this)", m_modeFm ? L"FM/OPNA -> .fpy" : L"MIDI/MICP -> .mpw2/3");
	m_status.SetWindowText(s);
	RefreshChromeOpaque();
}
