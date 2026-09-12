#include "stdafx.h"
#include "ogg.h"
#include "CSasamiPianoRollDlg.h"
#include "CSasamiMidiScoreDlg.h"
#include "CSasamiFmScoreDlg.h"
#include "CSasamiNotePaletteDlg.h"
#include "CSasamiLayoutPaletteDlg.h"
#include "CSasamiScoreArrange.h"
#include "CSasamiCmdHelpDlg.h"
#include "CSasamiSimpleInputDlg.h"
#include "CCustomPopupMenu.h"
#include "OfflineHelp.h"
#include "VstMidiEngine.h"
#include "resource.h"

CSasamiPianoRollDlg* CSasamiPianoRollDlg::s_midi = NULL;
CSasamiPianoRollDlg* CSasamiPianoRollDlg::s_fm = NULL;

IMPLEMENT_DYNAMIC(CSasamiPianoRollDlg, CCustomBlurDialogExBase)

CSasamiPianoRollDlg::CSasamiPianoRollDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(IDD_SASAMI_PIANO_ROLL, pParent)
	, m_ev(NULL), m_evCount(NULL), m_ui(NULL), m_curPart(NULL), m_isFm(0)
	, m_midiScore(NULL), m_fmScore(NULL)
	, m_dragMode(0), m_dragLastX(0), m_dragLastY(0), m_histDragPushed(0)
	, m_resizeEv(-1), m_marquee(0), m_bInLayout(FALSE)
	, m_sbDrag(0), m_sbDragScroll0(0), m_sbDragAnchor(0)
{
	ScPianoRollInit(&m_roll);
	m_marquee0 = m_marquee1 = CPoint(0, 0);
}

CSasamiPianoRollDlg* CSasamiPianoRollDlg::InstanceMidi()
{
	return (s_midi && ::IsWindow(s_midi->GetSafeHwnd())) ? s_midi : NULL;
}
CSasamiPianoRollDlg* CSasamiPianoRollDlg::InstanceFm()
{
	return (s_fm && ::IsWindow(s_fm->GetSafeHwnd())) ? s_fm : NULL;
}
CSasamiPianoRollDlg* CSasamiPianoRollDlg::Instance()
{
	if (InstanceMidi()) return InstanceMidi();
	return InstanceFm();
}

void CSasamiPianoRollDlg::OpenForMidi(CSasamiMidiScoreDlg* score)
{
	if (!score) return;
	if (s_midi && ::IsWindow(s_midi->GetSafeHwnd())) {
		s_midi->BindMidi(score);
		s_midi->EnableAero(FALSE);
		s_midi->ShowWindow(SW_SHOW);
		s_midi->SetForegroundWindow();
		s_midi->ApplyLang();
		s_midi->Refresh();
		return;
	}
	s_midi = new CSasamiPianoRollDlg(score);
	s_midi->BindMidi(score);
	if (!s_midi->Create(IDD_SASAMI_PIANO_ROLL, score)) {
		delete s_midi; s_midi = NULL; return;
	}
	s_midi->EnableAero(FALSE);
	s_midi->ShowWindow(SW_SHOW);
}

void CSasamiPianoRollDlg::OpenForFm(CSasamiFmScoreDlg* score)
{
	if (!score) return;
	if (s_fm && ::IsWindow(s_fm->GetSafeHwnd())) {
		s_fm->BindFm(score);
		s_fm->EnableAero(FALSE);
		s_fm->ShowWindow(SW_SHOW);
		s_fm->SetForegroundWindow();
		s_fm->ApplyLang();
		s_fm->Refresh();
		return;
	}
	s_fm = new CSasamiPianoRollDlg(score);
	s_fm->BindFm(score);
	if (!s_fm->Create(IDD_SASAMI_PIANO_ROLL, score)) {
		delete s_fm; s_fm = NULL; return;
	}
	s_fm->EnableAero(FALSE);
	s_fm->ShowWindow(SW_SHOW);
}

void CSasamiPianoRollDlg::OpenOwned(CWnd* owner, ScEvent* ev, int* evCount, ScStaffUi* ui, int* curPart, int isFm)
{
	(void)owner;
	if (isFm) {
		CSasamiFmScoreDlg* sc = CSasamiFmScoreDlg::Instance();
		if (sc) OpenForFm(sc);
		else if (s_fm && ::IsWindow(s_fm->GetSafeHwnd())) {
			s_fm->Bind(ev, evCount, ui, curPart, 1);
			s_fm->ShowWindow(SW_SHOW);
			s_fm->Refresh();
		}
	} else {
		CSasamiMidiScoreDlg* sc = CSasamiMidiScoreDlg::Instance();
		if (sc) OpenForMidi(sc);
		else if (s_midi && ::IsWindow(s_midi->GetSafeHwnd())) {
			s_midi->Bind(ev, evCount, ui, curPart, 0);
			s_midi->ShowWindow(SW_SHOW);
			s_midi->Refresh();
		}
	}
}

void CSasamiPianoRollDlg::Bind(ScEvent* ev, int* evCount, ScStaffUi* ui, int* curPart, int isFm)
{
	m_ev = ev; m_evCount = evCount; m_ui = ui; m_curPart = curPart; m_isFm = isFm;
}

void CSasamiPianoRollDlg::BindMidi(CSasamiMidiScoreDlg* score)
{
	m_midiScore = score; m_fmScore = NULL; m_isFm = 0;
	if (!score) return;
	Bind(score->Doc()->ev, &score->Doc()->evCount, score->Ui(), score->CurPartPtr(), 0);
}

void CSasamiPianoRollDlg::BindFm(CSasamiFmScoreDlg* score)
{
	m_fmScore = score; m_midiScore = NULL; m_isFm = 1;
	if (!score) return;
	Bind(score->DocMutable()->ev, &score->DocMutable()->evCount, score->Ui(), score->CurPartPtr(), 1);
}

void CSasamiPianoRollDlg::Refresh()
{
	if (::IsWindow(m_hWnd)) Invalidate(FALSE);
}

void CSasamiPianoRollDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CSasamiPianoRollDlg, CCustomBlurDialogExBase)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_CLOSE()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSEWHEEL()
	ON_WM_HSCROLL()
	ON_WM_VSCROLL()
	ON_WM_TIMER()
	ON_WM_CONTEXTMENU()
	ON_WM_KEYDOWN()
	ON_BN_CLICKED(IDC_SASAMI_ROLL_OPEN, &CSasamiPianoRollDlg::OnBnOpen)
	ON_BN_CLICKED(IDC_SASAMI_ROLL_SAVE, &CSasamiPianoRollDlg::OnBnSave)
	ON_BN_CLICKED(IDC_SASAMI_ROLL_NEW, &CSasamiPianoRollDlg::OnBnNew)
	ON_BN_CLICKED(IDC_SASAMI_ROLL_PLAY, &CSasamiPianoRollDlg::OnBnPlay)
	ON_BN_CLICKED(IDC_SASAMI_ROLL_EXPORT, &CSasamiPianoRollDlg::OnBnExport)
	ON_BN_CLICKED(IDC_SASAMI_ROLL_HELP, &CSasamiPianoRollDlg::OnBnHelp)
	ON_BN_CLICKED(IDC_SASAMI_ROLL_PENCIL, &CSasamiPianoRollDlg::OnBnPencil)
	ON_BN_CLICKED(IDC_SASAMI_ROLL_ERASE, &CSasamiPianoRollDlg::OnBnErase)
	ON_BN_CLICKED(IDC_SASAMI_ROLL_SEL, &CSasamiPianoRollDlg::OnBnSel)
	ON_BN_CLICKED(IDC_SASAMI_ROLL_PAL, &CSasamiPianoRollDlg::OnBnPal)
	ON_BN_CLICKED(IDC_SASAMI_ROLL_TEMPO, &CSasamiPianoRollDlg::OnBnTempo)
	ON_BN_CLICKED(IDC_SASAMI_ROLL_TEXT, &CSasamiPianoRollDlg::OnBnText)
	ON_BN_CLICKED(IDC_SASAMI_ROLL_LAYOUT, &CSasamiPianoRollDlg::OnBnLayout)
	ON_BN_CLICKED(IDC_SASAMI_ROLL_ARR, &CSasamiPianoRollDlg::OnBnArr)
	ON_BN_CLICKED(IDC_SASAMI_ROLL_CHORD, &CSasamiPianoRollDlg::OnBnChord)
	ON_BN_CLICKED(IDC_SASAMI_ROLL_PATT, &CSasamiPianoRollDlg::OnBnPatt)
	ON_BN_CLICKED(IDC_SASAMI_ROLL_FX, &CSasamiPianoRollDlg::OnBnFx)
	ON_BN_CLICKED(IDC_SASAMI_ROLL_VOICE, &CSasamiPianoRollDlg::OnBnVoice)
	ON_BN_CLICKED(IDC_SASAMI_ROLL_SCORE, &CSasamiPianoRollDlg::OnBnScore)
	ON_CBN_SELCHANGE(IDC_SASAMI_ROLL_CH, &CSasamiPianoRollDlg::OnCbnCh)
	ON_CBN_SELCHANGE(IDC_SASAMI_ROLL_PASTE, &CSasamiPianoRollDlg::OnCbnPaste)
	ON_CBN_SELCHANGE(IDC_SASAMI_ROLL_FOLLOW, &CSasamiPianoRollDlg::OnCbnFollow)
	ON_CBN_SELCHANGE(IDC_SASAMI_ROLL_STRIPKIND0, &CSasamiPianoRollDlg::OnCbnStrip)
	ON_CBN_SELCHANGE(IDC_SASAMI_ROLL_STRIPLANES, &CSasamiPianoRollDlg::OnCbnStrip)
	ON_CBN_SELCHANGE(IDC_SASAMI_ROLL_STRIPDRAW, &CSasamiPianoRollDlg::OnCbnStrip)
	ON_CBN_SELCHANGE(IDC_SASAMI_ROLL_STRIPSTEP, &CSasamiPianoRollDlg::OnCbnStrip)
	ON_MESSAGE(WM_SASAMI_PAL_DUR, &CSasamiPianoRollDlg::OnPalDur)
	ON_MESSAGE(WM_SASAMI_PAL_QUERY_STATE, &CSasamiPianoRollDlg::OnPalQueryState)
	ON_MESSAGE(WM_SASAMI_PAL_LAYOUT, &CSasamiPianoRollDlg::OnPalLayout)
END_MESSAGE_MAP()

static void FlatBtn(CCustomStandardButton& b)
{
	if (b.GetSafeHwnd()) { b.SetAeroMode(FALSE); b.SetFlat(TRUE); }
}

void CSasamiPianoRollDlg::CreateChrome()
{
	auto mkBtn = [&](CCustomStandardButton& b, UINT id, LPCWSTR t) {
		if (!b.GetSafeHwnd())
			b.Create(t, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | BS_PUSHBUTTON, CRect(8, 8, 88, 36), this, id);
		FlatBtn(b);
	};
	mkBtn(m_btnOpen, IDC_SASAMI_ROLL_OPEN, L"Open");
	mkBtn(m_btnSave, IDC_SASAMI_ROLL_SAVE, L"Save");
	mkBtn(m_btnNew, IDC_SASAMI_ROLL_NEW, L"New");
	mkBtn(m_btnPlay, IDC_SASAMI_ROLL_PLAY, L"Play");
	mkBtn(m_btnExport, IDC_SASAMI_ROLL_EXPORT, L"Export");
	mkBtn(m_btnHelp, IDC_SASAMI_ROLL_HELP, L"?");
	mkBtn(m_btnPencil, IDC_SASAMI_ROLL_PENCIL, L"Pencil");
	mkBtn(m_btnErase, IDC_SASAMI_ROLL_ERASE, L"Erase");
	mkBtn(m_btnSel, IDC_SASAMI_ROLL_SEL, L"Select");
	mkBtn(m_btnPal, IDC_SASAMI_ROLL_PAL, L"Notes");
	mkBtn(m_btnTempo, IDC_SASAMI_ROLL_TEMPO, L"Tempo");
	mkBtn(m_btnText, IDC_SASAMI_ROLL_TEXT, L"Text");
	mkBtn(m_btnLayout, IDC_SASAMI_ROLL_LAYOUT, L"Layout");
	mkBtn(m_btnArr, IDC_SASAMI_ROLL_ARR, L"Arr");
	mkBtn(m_btnChord, IDC_SASAMI_ROLL_CHORD, L"Chord");
	mkBtn(m_btnPatt, IDC_SASAMI_ROLL_PATT, L"Patt");
	mkBtn(m_btnFx, IDC_SASAMI_ROLL_FX, L"FX");
	mkBtn(m_btnVoice, IDC_SASAMI_ROLL_VOICE, L"Voice");
	mkBtn(m_btnScore, IDC_SASAMI_ROLL_SCORE, L"Score");
	auto mkCb = [&](CCustomComboBox& c, UINT id) {
		if (!c.GetSafeHwnd())
			c.Create(WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
				CRect(8, 400, 88, 432), this, id);
		c.SetAeroMode(FALSE);
	};
	mkCb(m_ch, IDC_SASAMI_ROLL_CH);
	mkCb(m_pasteMode, IDC_SASAMI_ROLL_PASTE);
	mkCb(m_follow, IDC_SASAMI_ROLL_FOLLOW);
	mkCb(m_stripKind0, IDC_SASAMI_ROLL_STRIPKIND0);
	mkCb(m_stripLanes, IDC_SASAMI_ROLL_STRIPLANES);
	mkCb(m_stripDraw, IDC_SASAMI_ROLL_STRIPDRAW);
	mkCb(m_stripStep, IDC_SASAMI_ROLL_STRIPSTEP);
	if (!m_status.GetSafeHwnd())
		m_status.Create(L"", WS_CHILD | WS_VISIBLE | SS_LEFT, CRect(0, 0, 100, 18), this, IDC_SASAMI_ROLL_STATUS);
	if (!m_helpBar.GetSafeHwnd())
		m_helpBar.Create(L"", WS_CHILD | WS_VISIBLE | SS_LEFT, CRect(0, 0, 100, 36), this, IDC_SASAMI_ROLL_HELPBAR);
	m_status.SetAeroMode(FALSE);
	m_helpBar.SetAeroMode(FALSE);
}

void CSasamiPianoRollDlg::LayoutChrome()
{
	if (!::IsWindow(m_hWnd) || m_bInLayout) return;
	m_bInLayout = TRUE;
	CRect rc; GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	const int pad = 8, btnH = 28, cbH = 32, gap = 4;
	if (m_btnChord.GetSafeHwnd()) m_btnChord.ShowWindow(m_isFm ? SW_HIDE : SW_SHOW);
	if (m_btnPatt.GetSafeHwnd()) m_btnPatt.ShowWindow(m_isFm ? SW_HIDE : SW_SHOW);
	if (m_btnFx.GetSafeHwnd()) m_btnFx.ShowWindow(m_isFm ? SW_HIDE : SW_SHOW);
	if (m_btnVoice.GetSafeHwnd()) m_btnVoice.ShowWindow(m_isFm ? SW_SHOW : SW_HIDE);
	if (m_btnNew.GetSafeHwnd()) m_btnNew.ShowWindow(m_isFm ? SW_HIDE : SW_SHOW);
	int x = pad, y = cap + pad;
	const int maxX = max(pad + 80, rc.right - pad);
	CDC* pdc = GetDC();
	auto textW = [&](CWnd& w, int minW) -> int {
		if (!w.GetSafeHwnd() || !pdc) return minW;
		CString t; w.GetWindowText(t);
		CFont* f = w.GetFont();
		CFont* old = f ? pdc->SelectObject(f) : NULL;
		CSize sz = pdc->GetTextExtent(t.IsEmpty() ? L"W" : t);
		if (old) pdc->SelectObject(old);
		return max(minW, sz.cx + 24);
	};
	int rowBottom = y;
	auto endRow = [&]() {
		y = rowBottom + 8;
		x = pad;
		rowBottom = y;
	};
	auto placeBtn = [&](CWnd& w, int minW) {
		if (!w.GetSafeHwnd() || !w.IsWindowVisible()) return;
		const int ww = textW(w, minW);
		if (x > pad && x + ww > maxX) { y = rowBottom + 6; x = pad; }
		w.MoveWindow(x, y, ww, btnH);
		rowBottom = max(rowBottom, y + btnH);
		x += ww + gap;
	};
	auto layoutCombo = [&](CCustomComboBox& c, int ww, int dropW) {
		if (!c.GetSafeHwnd()) return;
		if (x > pad && x + ww > maxX) { y = rowBottom + 6; x = pad; }
		const int closedH = (cbH - 4 > 22) ? (cbH - 4) : 22;
		const int dropH = (closedH > 28) ? closedH : 28;
		c.SetItemHeight(-1, closedH);
		c.SetItemHeight(0, dropH);
		if (dropW > 0) c.SetDroppedWidth(dropW);
		c.MoveWindow(x, y, ww, cbH);
		rowBottom = max(rowBottom, y + cbH);
		x += ww + gap;
	};
	placeBtn(m_btnOpen, 56);
	placeBtn(m_btnSave, 56);
	placeBtn(m_btnNew, 56);
	placeBtn(m_btnPlay, 88);
	placeBtn(m_btnExport, 72);
	endRow();
	placeBtn(m_btnPencil, 56);
	placeBtn(m_btnErase, 80);
	placeBtn(m_btnSel, 56);
	placeBtn(m_btnPal, 56);
	placeBtn(m_btnTempo, 64);
	placeBtn(m_btnText, 72);
	placeBtn(m_btnLayout, 56);
	placeBtn(m_btnArr, 72);
	placeBtn(m_btnChord, 56);
	placeBtn(m_btnPatt, 80);
	placeBtn(m_btnFx, 96);
	placeBtn(m_btnVoice, 56);
	placeBtn(m_btnScore, 56);
	placeBtn(m_btnHelp, 56);
	endRow();
	layoutCombo(m_ch, 104, 140);
	layoutCombo(m_pasteMode, 108, 140);
	layoutCombo(m_follow, 108, 140);
	endRow();
	layoutCombo(m_stripLanes, 120, 160);
	layoutCombo(m_stripKind0, 140, 180);
	layoutCombo(m_stripDraw, 108, 140);
	layoutCombo(m_stripStep, 88, 120);
	endRow();
	if (pdc) ReleaseDC(pdc);
	int toolBottom = rowBottom;
	m_toolbarRc = CRect(0, cap, rc.right, toolBottom);
	const int helpH = 36, statusH = 18;
	const int sbW = ScStaffScrollGutterW();
	const int sbH = ScStaffScrollGutterH();
	const int stripH = (m_ui && m_ui->stripCount > 0) ? max(48, ScStaffStripTotalH(m_ui)) : 0;
	const int bottom = rc.bottom;
	if (m_helpBar.GetSafeHwnd())
		m_helpBar.MoveWindow(8, bottom - helpH - 2, max(40, rc.Width() - 16), helpH);
	if (m_status.GetSafeHwnd())
		m_status.MoveWindow(8, bottom - helpH - statusH - 4, max(40, rc.Width() - 16), statusH);
	const int aboveStatus = bottom - helpH - statusH - 6;
	if (stripH > 0)
		m_stripRc = CRect(0, aboveStatus - stripH, rc.right, aboveStatus);
	else
		m_stripRc.SetRect(0, aboveStatus, rc.right, aboveStatus);
	m_gridRc = CRect(0, toolBottom, max(sbW + 40, rc.right - sbW), max(toolBottom + 40, m_stripRc.top - sbH));
	m_bodyRc = CRect(m_gridRc.left, m_gridRc.top, rc.right, m_gridRc.bottom + sbH);
	FitRollKeys();
	UpdateScrollBars();
	m_bInLayout = FALSE;
}

BOOL CSasamiPianoRollDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	EnableAero(FALSE);
	if (m_isFm) s_fm = this; else s_midi = this;
	CreateChrome();
	ApplyLang();
	SyncPartCombo();
	SyncPasteFollow();
	SyncStripCombos();
	SetupTooltips();
	UpdateHelpBar();
	if (m_ui) {
		m_ui->tool = SC_TOOL_SELECT;
		m_ui->helpTopic = SC_HELP_SELECT;
		m_ui->markerSolidTrack = m_curPart ? *m_curPart : 0;
	}
	LayoutChrome();
	SetTimer(1, 50, NULL);
	return TRUE;
}

void CSasamiPianoRollDlg::PostNcDestroy()
{
	if (s_midi == this) s_midi = NULL;
	if (s_fm == this) s_fm = NULL;
	CCustomBlurDialogExBase::PostNcDestroy();
	delete this;
}

void CSasamiPianoRollDlg::OnClose() { DestroyWindow(); }

void CSasamiPianoRollDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	LayoutChrome();
	Invalidate(FALSE);
	(void)cx; (void)cy;
}

BOOL CSasamiPianoRollDlg::OnEraseBkgnd(CDC* pDC)
{
	if (!pDC) return TRUE;
	CRect rc; GetClientRect(&rc);
#if CCUSTOM_AERO_SUPPORT
	CCC_FillRectAlpha(pDC->GetSafeHdc(), rc, RGB(32, 34, 40), 255);
#else
	pDC->FillSolidRect(&rc, RGB(32, 34, 40));
#endif
	return TRUE;
}

void CSasamiPianoRollDlg::ApplyLang()
{
	if (m_isFm)
		SetWindowText(LL14(L"SASAMI FMピアノロール", L"SASAMI FM Piano Roll", L"Piano roll FM SASAMI", L"Piano roll FM SASAMI", L"Piano roll FM SASAMI",
			L"SASAMI FM 피아노 롤", L"SASAMI FM 钢琴卷帘", L"رول بيانو SASAMI FM", L"SASAMI FM-пианоролл", L"SASAMI FM-Klavierrolle",
			L"Piano roll FM SASAMI", L"SASAMI FM-piano-roll", L"Rolka SASAMI FM", L"SASAMI FM piyano rulosu"));
	else
		SetWindowText(LL14(L"SASAMI MIDIピアノロール", L"SASAMI MIDI Piano Roll", L"Piano roll MIDI SASAMI", L"Piano roll MIDI SASAMI", L"Piano roll MIDI SASAMI",
			L"SASAMI MIDI 피아노 롤", L"SASAMI MIDI 钢琴卷帘", L"رول بيانو SASAMI MIDI", L"SASAMI MIDI-пианоролл", L"SASAMI MIDI-Klavierrolle",
			L"Piano roll MIDI SASAMI", L"SASAMI MIDI-piano-roll", L"Rolka SASAMI MIDI", L"SASAMI MIDI piyano rulosu"));
	auto set = [](CCustomStandardButton& b, LPCWSTR s) { if (b.GetSafeHwnd()) b.SetWindowText(s); };
	set(m_btnOpen, LL14(L"開く", L"Open", L"Ouvrir", L"Apri", L"Abrir", L"열기", L"打开", L"فتح", L"Открыть", L"Öffnen", L"Abrir", L"Openen", L"Otwórz", L"Aç"));
	set(m_btnSave, LL14(L"保存", L"Save", L"Enregistrer", L"Salva", L"Guardar", L"저장", L"保存", L"حفظ", L"Сохранить", L"Speichern", L"Salvar", L"Opslaan", L"Zapisz", L"Kaydet"));
	set(m_btnNew, LL14(L"新規", L"New", L"Nouveau", L"Nuovo", L"Nuevo", L"새로", L"新建", L"جديد", L"Новый", L"Neu", L"Novo", L"Nieuw", L"Nowy", L"Yeni"));
	set(m_btnPlay, LL14(L"再生確認", L"Preview", L"Aperçu", L"Anteprima", L"Vista previa", L"미리듣기", L"预览", L"معاينة", L"Превью", L"Vorschau", L"Prévia", L"Voorbeeld", L"Podgląd", L"Önizle"));
	set(m_btnExport, LL14(L"書き出し", L"Export", L"Exporter", L"Esporta", L"Exportar", L"내보내기", L"导出", L"تصدير", L"Экспорт", L"Export", L"Exportar", L"Exporteren", L"Eksport", L"Dışa aktar"));
	set(m_btnHelp, LL14(L"ヘルプ", L"Help", L"Aide", L"Guida", L"Ayuda", L"도움말", L"帮助", L"مساعدة", L"Справка", L"Hilfe", L"Ajuda", L"Help", L"Pomoc", L"Yardım"));
	set(m_btnPencil, LL14(L"音符", L"Notes", L"Notes", L"Note", L"Notas", L"음표", L"音符", L"نغمات", L"Ноты", L"Noten", L"Notas", L"Noten", L"Nuty", L"Nota"));
	set(m_btnErase, LL14(L"消しゴム", L"Erase", L"Gomme", L"Gomma", L"Borrar", L"지우개", L"橡皮", L"ممحاة", L"Ластик", L"Radierer", L"Borracha", L"Gum", L"Gumka", L"Silgi"));
	set(m_btnSel, LL14(L"選択", L"Select", L"Sélection", L"Selezione", L"Seleccionar", L"선택", L"选择", L"تحديد", L"Выбор", L"Auswahl", L"Selecionar", L"Selecteren", L"Zaznacz", L"Seç"));
	set(m_btnPal, LL14(L"音符", L"Notes", L"Notes", L"Note", L"Notas", L"음표", L"音符", L"نغمات", L"Ноты", L"Noten", L"Notas", L"Noten", L"Nuty", L"Nota"));
	set(m_btnTempo, LL14(L"テンポ", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"템포", L"速度", L"إيقاع", L"Темп", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"Tempo"));
	set(m_btnText, LL14(L"テキスト", L"Text", L"Texte", L"Testo", L"Texto", L"텍스트", L"文本", L"نص", L"Текст", L"Text", L"Texto", L"Tekst", L"Tekst", L"Metin"));
	set(m_btnLayout, LL14(L"譜表", L"Layout", L"Portée", L"Impaginazione", L"Diseño", L"보표", L"谱表", L"تخطيط", L"Партитура", L"Notation", L"Layout", L"Layout", L"Układ", L"Düzen"));
	set(m_btnArr, LL14(L"アレンジ", L"Arrange", L"Arranger", L"Arrangia", L"Arreglar", L"어레인지", L"编曲", L"ترتيب", L"Аранжировка", L"Arrange", L"Arranjo", L"Arrangeren", L"Aranż", L"Aranje"));
	set(m_btnChord, LL14(L"和音", L"Chord", L"Accord", L"Accordo", L"Acorde", L"화음", L"和弦", L"وتر", L"Аккорд", L"Akkord", L"Acorde", L"Akkoord", L"Akor", L"Akor"));
	set(m_btnPatt, LL14(L"パターン", L"Pattern", L"Motif", L"Pattern", L"Patrón", L"패턴", L"型", L"نمط", L"Паттерн", L"Muster", L"Padrão", L"Patroon", L"Wzorzec", L"Desen"));
	set(m_btnFx, LL14(L"インサートFX", L"Insert FX", L"FX d’insertion", L"FX insert", L"FX de inserción", L"인서트 FX", L"插入FX", L"إدراج FX", L"Вставка FX", L"Insert-FX", L"FX de inserção", L"Insert-FX", L"FX wstawiania", L"Insert FX"));
	set(m_btnVoice, LL14(L"音色", L"Voice", L"Timbre", L"Voce", L"Voz", L"음색", L"音色", L"صوت", L"Голос", L"Stimme", L"Voz", L"Stem", L"Głos", L"Ses"));
	set(m_btnScore, LL14(L"譜面", L"Score", L"Partition", L"Partitura", L"Partitura", L"악보", L"乐谱", L"نوتة", L"Партитура", L"Partitur", L"Partitura", L"Partituur", L"Partytura", L"Skor"));
	SyncPasteFollow();
	UpdateHelpBar();
}

void CSasamiPianoRollDlg::SetupTooltips()
{
	if (!CCustomControlUtility::BeginDialogToolTip(m_tooltip, this, TTS_NOPREFIX)) return;
	auto tip = [&](CWnd& w, LPCWSTR s) { if (w.GetSafeHwnd()) m_tooltip.AddTool(&w, s); };
	tip(m_btnOpen, LL14(L"ファイルを開く", L"Open file", L"Ouvrir", L"Apri", L"Abrir", L"파일 열기", L"打开文件", L"فتح ملف", L"Открыть", L"Öffnen", L"Abrir", L"Openen", L"Otwórz", L"Aç"));
	tip(m_btnSave, LL14(L"保存", L"Save", L"Enregistrer", L"Salva", L"Guardar", L"저장", L"保存", L"حفظ", L"Сохранить", L"Speichern", L"Salvar", L"Opslaan", L"Zapisz", L"Kaydet"));
	tip(m_btnPlay, LL14(L"マーカーからプレビュー", L"Preview from marker", L"Aperçu", L"Anteprima", L"Vista previa", L"미리듣기", L"预览", L"معاينة", L"Превью", L"Vorschau", L"Prévia", L"Voorbeeld", L"Podgląd", L"Önizle"));
	tip(m_btnPencil, LL14(L"鉛筆 — グリッドに音符配置", L"Pencil — place notes on grid", L"Crayon", L"Matita", L"Lápiz", L"연필", L"铅笔", L"قلم", L"Карандаш", L"Stift", L"Lápis", L"Potlood", L"Ołówek", L"Kalem"));
	tip(m_btnErase, LL14(L"消しゴム", L"Eraser", L"Gomme", L"Gomma", L"Borrar", L"지우개", L"橡皮", L"ممحاة", L"Ластик", L"Radierer", L"Borracha", L"Gum", L"Gumka", L"Silgi"));
	tip(m_btnSel, LL14(L"選択・移動・範囲", L"Select / move / range", L"Sélection", L"Selezione", L"Seleccionar", L"선택", L"选择", L"تحديد", L"Выбор", L"Auswahl", L"Selecionar", L"Selecteren", L"Zaznacz", L"Seç"));
	tip(m_btnScore, LL14(L"譜面ウィンドウを表示", L"Show score window", L"Afficher partition", L"Mostra partitura", L"Mostrar partitura", L"악보 창 표시", L"显示乐谱窗口", L"إظهار النوتة", L"Показать партитуру", L"Partitur zeigen", L"Mostrar partitura", L"Partituur tonen", L"Pokaż partyturę", L"Skoru göster"));
	tip(m_pasteMode, LL14(L"ペースト: 上書き / 挿入", L"Paste: overwrite / insert", L"Coller", L"Incolla", L"Pegar", L"붙여넣기", L"粘贴", L"لصق", L"Вставка", L"Einfügen", L"Colar", L"Plakken", L"Wklej", L"Yapıştır"));
	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip);
}

void CSasamiPianoRollDlg::UpdateHelpBar()
{
	if (!m_helpBar.GetSafeHwnd() || !m_ui) return;
	wchar_t buf[768];
	ScStaffFormatHelpBar(buf, 512, m_ui, m_isFm, m_curPart ? *m_curPart : 0);
	wcsncat_s(buf, LL14(
		L"  Shift+ホイール=鍵盤高さ  Ctrl+ホイール=横  鍵盤クリック=試聴  Ctrl+鍵盤=赤バー配置",
		L"  Shift+wheel=key height  Ctrl+wheel=horizontal  click keys=audition  Ctrl+key=place at marker",
		L"  Shift+molette=hauteur  Ctrl+molette=horiz.  clic clavier=audition",
		L"  Shift+rotella=altezza  Ctrl+rotella=oriz.  clic tasti=audizione",
		L"  Shift+rueda=altura  Ctrl+rueda=horiz.  clic teclas=audicion",
		L"  Shift+휠=건반높이  Ctrl+휠=가로  건반클릭=시청  Ctrl+건반=적바 배치",
		L"  Shift+滚轮=琴键高度  Ctrl+滚轮=横向  点击琴键=试听  Ctrl+琴键=红条放置",
		L"  Shift+wheel=key height  Ctrl+wheel=h-scroll",
		L"  Shift+колесо=высота  Ctrl+колесо=гориз.",
		L"  Shift+Rad=Tastenhoehe  Strg+Rad=horizontal",
		L"  Shift+roda=altura  Ctrl+roda=horiz.",
		L"  Shift+wiel=toetshoogte  Ctrl+wiel=horiz.",
		L"  Shift+kolo=wysokosc  Ctrl+kolo=poziomo",
		L"  Shift+teker=tus yuksekligi  Ctrl+teker=yatay"), _TRUNCATE);
	m_helpBar.SetWindowText(buf);
}

void CSasamiPianoRollDlg::SyncPartCombo()
{
	if (!m_ch.GetSafeHwnd() || !m_curPart) return;
	m_ch.ResetContent();
	const int n = m_isFm ? 16 : 32;
	for (int i = 0; i < n; i++) {
		wchar_t s[32];
		_snwprintf_s(s, _TRUNCATE, m_isFm ? L"Part %d" : L"Ch %d", i + 1);
		m_ch.AddString(s);
	}
	int cur = *m_curPart;
	if (cur < 0) cur = 0;
	if (cur >= n) cur = n - 1;
	m_ch.SetCurSel(cur);
}

void CSasamiPianoRollDlg::SyncPasteFollow()
{
	if (m_pasteMode.GetSafeHwnd()) {
		m_pasteMode.ResetContent();
		m_pasteMode.AddString(LL14(L"上書き", L"Overwrite", L"Écraser", L"Sovrascrivi", L"Sobrescribir", L"덮어쓰기", L"覆盖", L"استبدال", L"Замена", L"Überschreiben", L"Sobrescrever", L"Overschrijven", L"Nadpisz", L"Üzerine yaz"));
		m_pasteMode.AddString(LL14(L"挿入", L"Insert", L"Insérer", L"Inserisci", L"Insertar", L"삽입", L"插入", L"إدراج", L"Вставка", L"Einfügen", L"Inserir", L"Invoegen", L"Wstaw", L"Ekle"));
		m_pasteMode.SetCurSel(m_ui && m_ui->pasteInsert ? 1 : 0);
	}
	if (m_follow.GetSafeHwnd() && m_ui) {
		m_follow.ResetContent();
		m_follow.AddString(LL14(L"追従OFF", L"Follow OFF", L"Suivi OFF", L"Segui OFF", L"Seguir OFF", L"추종OFF", L"跟随关", L"تتبع OFF", L"След OFF", L"Folge AUS", L"Seguir OFF", L"Volgen UIT", L"Śledź OFF", L"Takip KAPALI"));
		m_follow.AddString(LL14(L"中央", L"Center", L"Centre", L"Centro", L"Centro", L"중앙", L"居中", L"وسط", L"Центр", L"Mitte", L"Centro", L"Midden", L"Środek", L"Merkez"));
		m_follow.AddString(LL14(L"ページ", L"Page", L"Page", L"Pagina", L"Página", L"페이지", L"翻页", L"صفحة", L"Страница", L"Seite", L"Página", L"Pagina", L"Strona", L"Sayfa"));
		m_follow.SetCurSel(m_ui->followMode);
	}
}

void CSasamiPianoRollDlg::SyncStripCombos()
{
	if (!m_ui) return;
	if (m_stripLanes.GetSafeHwnd()) {
		m_stripLanes.ResetContent();
		m_stripLanes.AddString(LL14(L"レーンなし", L"No lanes", L"Aucune", L"Nessuna", L"Ninguna", L"없음", L"无", L"بلا", L"Нет", L"Keine", L"Nenhuma", L"Geen", L"Brak", L"Yok"));
		m_stripLanes.AddString(LL14(L"レーン×1", L"Lanes ×1", L"×1", L"×1", L"×1", L"×1", L"×1", L"×1", L"×1", L"×1", L"×1", L"×1", L"×1", L"×1"));
		m_stripLanes.AddString(LL14(L"レーン×2", L"Lanes ×2", L"×2", L"×2", L"×2", L"×2", L"×2", L"×2", L"×2", L"×2", L"×2", L"×2", L"×2", L"×2"));
		m_stripLanes.AddString(LL14(L"レーン×3", L"Lanes ×3", L"×3", L"×3", L"×3", L"×3", L"×3", L"×3", L"×3", L"×3", L"×3", L"×3", L"×3", L"×3"));
		int lc = m_ui->stripCount;
		if (lc < 0) lc = 0;
		if (lc > 3) lc = 3;
		m_stripLanes.SetCurSel(lc);
	}
	if (m_stripKind0.GetSafeHwnd()) {
		m_stripKind0.ResetContent();
		for (int k = 0; k <= SC_STRIP_VEL; k++) {
			LPCWSTR nm = m_isFm ? ScStaffStripKindNameFm(k) : ScStaffStripKindName(k);
			if (nm && nm[0] && wcscmp(nm, L"?") != 0)
				m_stripKind0.AddString(nm);
		}
		int sk = m_ui->stripKind[0];
		if (sk < 0) sk = 0;
		if (sk >= m_stripKind0.GetCount()) sk = 0;
		if (m_stripKind0.GetCount() > 0)
			m_stripKind0.SetCurSel(sk);
	}
	if (m_stripDraw.GetSafeHwnd()) {
		m_stripDraw.ResetContent();
		m_stripDraw.AddString(LL14(L"鉛筆", L"Pencil", L"Crayon", L"Matita", L"Lápiz", L"연필", L"铅笔", L"قلم", L"Карандаш", L"Stift", L"Lápis", L"Potlood", L"Ołówek", L"Kalem"));
		m_stripDraw.AddString(LL14(L"直線", L"Line", L"Ligne", L"Linea", L"Línea", L"직선", L"直线", L"خط", L"Линия", L"Linie", L"Linha", L"Lijn", L"Linia", L"Çizgi"));
		m_stripDraw.AddString(LL14(L"曲線", L"Curve", L"Courbe", L"Curva", L"Curva", L"곡선", L"曲线", L"منحنى", L"Кривая", L"Kurve", L"Curva", L"Kromme", L"Krzywa", L"Eğri"));
		m_stripDraw.SetCurSel(m_ui->stripDraw);
	}
	if (m_stripStep.GetSafeHwnd()) {
		m_stripStep.ResetContent();
		const wchar_t* steps[] = { L"1/4", L"1/8", L"1/16", L"1/32", L"1/64" };
		for (int i = 0; i < 5; i++) m_stripStep.AddString(steps[i]);
		m_stripStep.SetCurSel(1);
		static const int kSteps[] = { SC_PPQN, SC_PPQN / 2, SC_PPQN / 4, SC_PPQN / 8, SC_PPQN / 16 };
		int stepSel = 1;
		for (int i = 0; i < 5; i++)
			if (m_ui->stripStepTicks == kSteps[i]) { stepSel = i; break; }
		m_stripStep.SetCurSel(stepSel);
	}
}

void CSasamiPianoRollDlg::ProxyScoreCommand(UINT idc)
{
	CWnd* sc = m_isFm ? (CWnd*)m_fmScore : (CWnd*)m_midiScore;
	if (!sc) sc = m_isFm ? (CWnd*)CSasamiFmScoreDlg::Instance() : (CWnd*)CSasamiMidiScoreDlg::Instance();
	if (sc && ::IsWindow(sc->GetSafeHwnd()))
		sc->SendMessage(WM_COMMAND, MAKEWPARAM(idc, BN_CLICKED), 0);
}

void CSasamiPianoRollDlg::AfterEdit()
{
	if (m_midiScore) m_midiScore->NotifyEdited();
	else if (m_fmScore) m_fmScore->NotifyEdited();
	else Refresh();
	if (m_midiScore && ::IsWindow(m_midiScore->GetSafeHwnd()))
		m_midiScore->Invalidate(FALSE);
	if (m_fmScore && ::IsWindow(m_fmScore->GetSafeHwnd()))
		m_fmScore->Invalidate(FALSE);
	Refresh();
	UpdateHelpBar();
	UpdateScrollBars();
}

void CSasamiPianoRollDlg::HistPushOwner()
{
	if (m_midiScore) { m_midiScore->HistPush(); return; }
	if (m_fmScore) { m_fmScore->HistPush(); return; }
	ScScoreHist* h = HistPtr();
	if (h && m_ev && m_evCount)
		ScScoreHistPush(h, m_ev, *m_evCount);
}

ScScoreHist* CSasamiPianoRollDlg::HistPtr()
{
	if (m_midiScore) return m_midiScore->Hist();
	if (m_fmScore) return m_fmScore->Hist();
	return NULL;
}
ScEvent* CSasamiPianoRollDlg::ClipBuf()
{
	if (m_midiScore) return m_midiScore->ClipBuf();
	if (m_fmScore) return m_fmScore->ClipBuf();
	return NULL;
}
int* CSasamiPianoRollDlg::ClipCountPtr()
{
	if (m_midiScore) return m_midiScore->ClipCountPtr();
	if (m_fmScore) return m_fmScore->ClipCountPtr();
	return NULL;
}
uint32_t* CSasamiPianoRollDlg::ClipBasePtr()
{
	if (m_midiScore) return m_midiScore->ClipBasePtr();
	if (m_fmScore) return m_fmScore->ClipBasePtr();
	return NULL;
}
uint32_t* CSasamiPianoRollDlg::ClipSpanPtr()
{
	if (m_midiScore) return m_midiScore->ClipSpanPtr();
	if (m_fmScore) return m_fmScore->ClipSpanPtr();
	return NULL;
}

void CSasamiPianoRollDlg::Undo()
{
	ScScoreHist* h = HistPtr();
	if (!h || !m_ev || !m_evCount) return;
	if (ScScoreHistUndo(h, m_ev, m_evCount, EvMax())) {
		AfterEdit();
		if (m_status.GetSafeHwnd())
			m_status.SetWindowText(LL14(L"元に戻す", L"Undo", L"Annuler", L"Annulla", L"Deshacer", L"실행 취소", L"撤销", L"تراجع", L"Отмена", L"Rückgängig", L"Desfazer", L"Ongedaan", L"Cofnij", L"Geri al"));
	}
}
void CSasamiPianoRollDlg::Redo()
{
	ScScoreHist* h = HistPtr();
	if (!h || !m_ev || !m_evCount) return;
	if (ScScoreHistRedo(h, m_ev, m_evCount, EvMax())) {
		AfterEdit();
		if (m_status.GetSafeHwnd())
			m_status.SetWindowText(LL14(L"やり直し", L"Redo", L"Rétablir", L"Ripeti", L"Rehacer", L"다시 실행", L"重做", L"إعادة", L"Повтор", L"Wiederholen", L"Refazer", L"Opnieuw", L"Ponów", L"Yinele"));
	}
}

void CSasamiPianoRollDlg::OnBnOpen() { ProxyScoreCommand(m_isFm ? IDC_SASAMI_FM_OPEN : IDC_SASAMI_MIDI_OPEN); Refresh(); }
void CSasamiPianoRollDlg::OnBnSave() { ProxyScoreCommand(m_isFm ? IDC_SASAMI_FM_SAVE : IDC_SASAMI_MIDI_SAVE); }
void CSasamiPianoRollDlg::OnBnNew() { if (!m_isFm) ProxyScoreCommand(IDC_SASAMI_MIDI_NEW); Refresh(); SyncPartCombo(); }
void CSasamiPianoRollDlg::OnBnPlay() { ProxyScoreCommand(m_isFm ? IDC_SASAMI_FM_PLAY : IDC_SASAMI_MIDI_PLAY); }
void CSasamiPianoRollDlg::OnBnExport() { ProxyScoreCommand(m_isFm ? IDC_SASAMI_FM_EXPORT : IDC_SASAMI_MIDI_EXPORT); }
void CSasamiPianoRollDlg::OnBnHelp()
{
	CSasamiCmdHelpDlg::Show(this, m_isFm ? CSasamiCmdHelpDlg::kTabScore1 : CSasamiCmdHelpDlg::kTabScore1);
}
void CSasamiPianoRollDlg::OnBnPencil()
{
	if (!m_ui) return;
	m_ui->tool = SC_TOOL_PENCIL;
	m_ui->helpTopic = SC_HELP_PENCIL;
	UpdateHelpBar();
}
void CSasamiPianoRollDlg::OnBnErase()
{
	if (!m_ui) return;
	m_ui->tool = SC_TOOL_ERASER;
	m_ui->helpTopic = SC_HELP_ERASER;
	UpdateHelpBar();
}
void CSasamiPianoRollDlg::OnBnSel()
{
	if (!m_ui) return;
	ScStaffEnterSelectTool(m_ui);
	UpdateHelpBar();
}
void CSasamiPianoRollDlg::OnBnPal()
{
	CRect wr; GetWindowRect(&wr);
	CSasamiNotePaletteDlg::OpenNear(this, CPoint(wr.left + 40, wr.top + 80));
}
void CSasamiPianoRollDlg::OnBnTempo() { ProxyScoreCommand(m_isFm ? IDC_SASAMI_FM_TEMPO : IDC_SASAMI_MIDI_TEMPO); }
void CSasamiPianoRollDlg::OnBnText() { ProxyScoreCommand(m_isFm ? IDC_SASAMI_FM_TEXT : IDC_SASAMI_MIDI_TEXT); }
void CSasamiPianoRollDlg::OnBnLayout()
{
	CRect wr; GetWindowRect(&wr);
	CSasamiLayoutPaletteDlg::OpenNear(this, CPoint(wr.left + 200, wr.top + 80));
}
void CSasamiPianoRollDlg::OnBnArr() { ProxyScoreCommand(m_isFm ? IDC_SASAMI_FM_ARR : IDC_SASAMI_MIDI_ARR); }
void CSasamiPianoRollDlg::OnBnChord() { if (!m_isFm) ProxyScoreCommand(IDC_SASAMI_MIDI_CHORD); }
void CSasamiPianoRollDlg::OnBnPatt() { if (!m_isFm) ProxyScoreCommand(IDC_SASAMI_MIDI_PATT); }
void CSasamiPianoRollDlg::OnBnFx() { if (!m_isFm) ProxyScoreCommand(IDC_SASAMI_MIDI_FX); }
void CSasamiPianoRollDlg::OnBnVoice() { if (m_isFm) ProxyScoreCommand(IDC_SASAMI_FM_VOICEBTN); }
void CSasamiPianoRollDlg::OnBnScore()
{
	if (m_isFm) {
		CSasamiFmScoreDlg::OpenOwned(GetParent());
		if (CSasamiFmScoreDlg* s = CSasamiFmScoreDlg::Instance()) {
			s->ShowWindow(SW_SHOW); s->SetForegroundWindow();
		}
	} else {
		CSasamiMidiScoreDlg::OpenOwned(GetParent());
		if (CSasamiMidiScoreDlg* s = CSasamiMidiScoreDlg::Instance()) {
			s->ShowWindow(SW_SHOW); s->SetForegroundWindow();
		}
	}
}
void CSasamiPianoRollDlg::OnCbnCh()
{
	if (!m_curPart || !m_ch.GetSafeHwnd()) return;
	int s = m_ch.GetCurSel();
	if (s < 0) return;
	*m_curPart = s;
	if (m_ui) m_ui->markerSolidTrack = s;
	Refresh();
	if (m_midiScore) m_midiScore->Invalidate(FALSE);
	if (m_fmScore) m_fmScore->Invalidate(FALSE);
}
void CSasamiPianoRollDlg::OnCbnPaste()
{
	if (m_ui && m_pasteMode.GetSafeHwnd())
		m_ui->pasteInsert = (m_pasteMode.GetCurSel() == 1) ? 1 : 0;
}
void CSasamiPianoRollDlg::OnCbnFollow()
{
	if (m_ui && m_follow.GetSafeHwnd())
		m_ui->followMode = m_follow.GetCurSel();
}
void CSasamiPianoRollDlg::OnCbnStrip()
{
	if (!m_ui) return;
	if (m_stripLanes.GetSafeHwnd()) {
		int s = m_stripLanes.GetCurSel();
		if (s >= 0) m_ui->stripCount = s;
	}
	if (m_stripKind0.GetSafeHwnd()) {
		int s = m_stripKind0.GetCurSel();
		if (s >= 0) m_ui->stripKind[0] = s;
	}
	if (m_stripDraw.GetSafeHwnd()) {
		int s = m_stripDraw.GetCurSel();
		if (s >= 0) m_ui->stripDraw = s;
	}
	if (m_stripStep.GetSafeHwnd()) {
		static const int kSteps[] = { SC_PPQN, SC_PPQN / 2, SC_PPQN / 4, SC_PPQN / 8, SC_PPQN / 16 };
		int stepSel = m_stripStep.GetCurSel();
		if (stepSel >= 0 && stepSel < 5)
			m_ui->stripStepTicks = kSteps[stepSel];
		ScStaffNormalizeStripStep(m_ui);
	}
	if (m_curPart) ScStaffSavePartStrip(m_ui, *m_curPart);
	m_ui->helpTopic = SC_HELP_STRIP;
	LayoutChrome();
	Refresh();
	UpdateHelpBar();
}

LRESULT CSasamiPianoRollDlg::OnPalDur(WPARAM w, LPARAM l)
{
	if ((l & SASAMI_PAL_CMD) == SASAMI_PAL_CMD) {
		HandlePalCmd((int)(l & 0xFF));
		return 0;
	}
	ApplyDurationPal(w, l);
	return 0;
}
LRESULT CSasamiPianoRollDlg::OnPalQueryState(WPARAM w, LPARAM l)
{
	CWnd* sc = m_isFm ? (CWnd*)CSasamiFmScoreDlg::Instance() : (CWnd*)CSasamiMidiScoreDlg::Instance();
	if (sc) return sc->SendMessage(WM_SASAMI_PAL_QUERY_STATE, w, l);
	return 0;
}
LRESULT CSasamiPianoRollDlg::OnPalLayout(WPARAM w, LPARAM l)
{
	CWnd* sc = m_isFm ? (CWnd*)CSasamiFmScoreDlg::Instance() : (CWnd*)CSasamiMidiScoreDlg::Instance();
	if (sc) sc->SendMessage(WM_SASAMI_PAL_LAYOUT, w, l);
	Refresh();
	return 0;
}

int CSasamiPianoRollDlg::CurCh() const
{
	return m_curPart ? *m_curPart : 0;
}

void CSasamiPianoRollDlg::FitRollKeys()
{
	if (m_gridRc.Height() > 8)
		ScPianoRollFit(&m_roll, max(1, m_gridRc.Height() - SC_ROLL_MARK_H));
}

CRect CSasamiPianoRollDlg::ScrollOuter() const
{
	return CRect(m_gridRc.left, m_gridRc.top, m_bodyRc.right, m_bodyRc.bottom);
}

void CSasamiPianoRollDlg::UpdateScrollBars()
{
	if (!::IsWindow(m_hWnd) || !m_ui) return;
	const int pageW = ScPianoRollTimePageW(m_gridRc);
	const int keysH = max(1, m_gridRc.Height() - SC_ROLL_MARK_H);
	const int contentW = ScPianoRollContentWidthPx(m_ui, m_ev, m_evCount ? *m_evCount : 0);
	const int contentH = ScPianoRollContentHeightPx(&m_roll);
	int maxX = max(0, contentW - pageW);
	if (m_ui->scrollX > maxX) m_ui->scrollX = maxX;
	if (m_ui->scrollX < 0) m_ui->scrollX = 0;
	int maxY = ScPianoRollMaxScrollY(&m_roll, keysH);
	if (m_roll.scrollY > maxY) m_roll.scrollY = maxY;
	if (m_roll.scrollY < 0) m_roll.scrollY = 0;

	SCROLLINFO si = {};
	si.cbSize = sizeof(si);
	si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
	si.nMin = 0;
	si.nMax = max(0, contentW);
	si.nPage = (UINT)max(1, pageW);
	si.nPos = m_ui->scrollX;
	SetScrollInfo(SB_HORZ, &si, TRUE);
	si.nMax = max(0, contentH);
	si.nPage = (UINT)max(1, keysH);
	si.nPos = m_roll.scrollY;
	SetScrollInfo(SB_VERT, &si, TRUE);
}

void CSasamiPianoRollDlg::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	(void)nPos; (void)pScrollBar;
	if (m_sbDrag || !m_ui) return;
	SCROLLINFO si = { sizeof(si), SIF_ALL };
	GetScrollInfo(SB_HORZ, &si);
	int pos = si.nPos;
	const int pxBeat = m_ui->pxBeat > 0 ? m_ui->pxBeat : SC_PX_BEAT_DEFAULT;
	switch (nSBCode) {
	case SB_LEFT: pos = si.nMin; break;
	case SB_RIGHT: pos = max(0, (int)si.nMax - (int)si.nPage + 1); break;
	case SB_LINELEFT: pos -= pxBeat; break;
	case SB_LINERIGHT: pos += pxBeat; break;
	case SB_PAGELEFT: pos -= (int)si.nPage; break;
	case SB_PAGERIGHT: pos += (int)si.nPage; break;
	case SB_THUMBTRACK:
	case SB_THUMBPOSITION: {
		SCROLLINFO ti = { sizeof(ti), SIF_TRACKPOS };
		GetScrollInfo(SB_HORZ, &ti);
		pos = ti.nTrackPos;
		break;
	}
	default: break;
	}
	int maxPos = max(0, (int)si.nMax - (int)si.nPage + 1);
	if (pos < si.nMin) pos = si.nMin;
	if (pos > maxPos) pos = maxPos;
	m_ui->scrollX = pos;
	si.fMask = SIF_POS;
	si.nPos = pos;
	SetScrollInfo(SB_HORZ, &si, TRUE);
	if (m_midiScore) m_midiScore->Invalidate(FALSE);
	if (m_fmScore) m_fmScore->Invalidate(FALSE);
	Refresh();
}

void CSasamiPianoRollDlg::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	(void)nPos; (void)pScrollBar;
	if (m_sbDrag) return;
	SCROLLINFO si = { sizeof(si), SIF_ALL };
	GetScrollInfo(SB_VERT, &si);
	int pos = si.nPos;
	const int rowH = max(1, m_roll.rowH);
	switch (nSBCode) {
	case SB_TOP: pos = si.nMin; break;
	case SB_BOTTOM: pos = max(0, (int)si.nMax - (int)si.nPage + 1); break;
	case SB_LINEUP: pos -= rowH; break;
	case SB_LINEDOWN: pos += rowH; break;
	case SB_PAGEUP: pos -= (int)si.nPage; break;
	case SB_PAGEDOWN: pos += (int)si.nPage; break;
	case SB_THUMBTRACK:
	case SB_THUMBPOSITION: {
		SCROLLINFO ti = { sizeof(ti), SIF_TRACKPOS };
		GetScrollInfo(SB_VERT, &ti);
		pos = ti.nTrackPos;
		break;
	}
	default: break;
	}
	int maxPos = max(0, (int)si.nMax - (int)si.nPage + 1);
	if (pos < si.nMin) pos = si.nMin;
	if (pos > maxPos) pos = maxPos;
	m_roll.scrollY = pos;
	FitRollKeys();
	si.fMask = SIF_POS;
	si.nPos = m_roll.scrollY;
	SetScrollInfo(SB_VERT, &si, TRUE);
	Refresh();
}

void CSasamiPianoRollDlg::ApplyDurationPal(WPARAM w, LPARAM l)
{
	if (!m_ui) return;
	m_ui->tool = SC_TOOL_PENCIL;
	m_ui->placeRest = (int)(l & 1);
	m_ui->dotted = (l & 2) ? 1 : 0;
	m_ui->tuplet = (int)((l >> 4) & 0xF);
	if (m_ui->tuplet != 3 && m_ui->tuplet != 5 && m_ui->tuplet != 6 && m_ui->tuplet != 8)
		m_ui->tuplet = 0;
	m_ui->triplet = (m_ui->tuplet == 3) ? 1 : 0;
	m_ui->placeAccidental = (int)(signed char)((l >> 8) & 0xFF);
	int base = (int)((l >> 16) & 0xFFFF);
	if (base > 0) m_ui->baseDur = base;
	ScStaffRecomputePlaceDur(m_ui);
	if ((int)w > 0) m_ui->placeDur = (int)w;
	if (m_ui->placeDur < 1) m_ui->placeDur = 1;
	m_ui->helpTopic = m_ui->placeRest ? SC_HELP_REST : SC_HELP_PAL_NOTE;
	UpdateHelpBar();
	if (m_status.GetSafeHwnd()) {
		CString s;
		s.Format(L"Dur=%d%s", m_ui->placeDur, m_ui->placeRest ? L" rest" : L"");
		m_status.SetWindowText(s);
	}
	Refresh();
}

void CSasamiPianoRollDlg::HandlePalCmd(int cmdId)
{
	if (!m_ui) return;
	if ((cmdId >= SASAMI_PAL_CMD_METER_24 && cmdId <= SASAMI_PAL_CMD_TR_ALL_MINUS)
		|| (cmdId >= SASAMI_PAL_CMD_METER_14 && cmdId <= SASAMI_PAL_CMD_METER_74)
		|| (cmdId >= SASAMI_PAL_CMD_KEY_BASE && cmdId < SASAMI_PAL_CMD_KEY_BASE + 15)) {
		CWnd* sc = m_isFm ? (CWnd*)m_fmScore : (CWnd*)m_midiScore;
		if (!sc) sc = m_isFm ? (CWnd*)CSasamiFmScoreDlg::Instance() : (CWnd*)CSasamiMidiScoreDlg::Instance();
		if (sc && ::IsWindow(sc->GetSafeHwnd()))
			sc->SendMessage(WM_SASAMI_PAL_DUR, 0, (LPARAM)(SASAMI_PAL_CMD | cmdId));
		Refresh();
		return;
	}
	const int ch = CurCh();
	const uint32_t atTick = m_ui->markerTick;
	const int stack = m_ui->markStack;
	const int eraser = (m_ui->tool == SC_TOOL_ERASER) ? 1 : 0;
	switch (cmdId) {
	case SASAMI_PAL_CMD_FIT:
		m_ui->snapFit ^= 1;
		UpdateHelpBar();
		Refresh();
		return;
	case SASAMI_PAL_CMD_TEMPO:
		OnBnTempo();
		return;
	case SASAMI_PAL_CMD_PENCIL: OnBnPencil(); return;
	case SASAMI_PAL_CMD_ERASE: OnBnErase(); return;
	case SASAMI_PAL_CMD_SEL: OnBnSel(); return;
	case SASAMI_PAL_CMD_TIE:
		m_ui->tool = SC_TOOL_TIE;
		m_ui->helpTopic = SC_HELP_TIE;
		if (m_ev && m_evCount) {
			HistPushOwner();
			ScStaffTieSelected(m_ev, *m_evCount, m_ui);
			AfterEdit();
		}
		UpdateHelpBar();
		return;
	case SASAMI_PAL_CMD_LOOP_A: ProxyScoreCommand(m_isFm ? IDC_SASAMI_FM_LOOPA : IDC_SASAMI_MIDI_LOOPA); return;
	case SASAMI_PAL_CMD_LOOP_B: ProxyScoreCommand(m_isFm ? IDC_SASAMI_FM_LOOPB : IDC_SASAMI_MIDI_LOOPB); return;
	case SASAMI_PAL_CMD_LOOP_CLR: ProxyScoreCommand(m_isFm ? IDC_SASAMI_FM_LOOPCLR : IDC_SASAMI_MIDI_LOOPCLR); return;
	case SASAMI_PAL_CMD_MARK_REPLACE: m_ui->markStack = 0; UpdateHelpBar(); return;
	case SASAMI_PAL_CMD_MARK_STACK: m_ui->markStack = 1; UpdateHelpBar(); return;
	case SASAMI_PAL_CMD_LOOP_START:
	case SASAMI_PAL_CMD_LOOP_END:
	case SASAMI_PAL_CMD_PED_ON:
	case SASAMI_PAL_CMD_PED_OFF: {
		if (!m_ev || !m_evCount) return;
		uint8_t kind = SC_EV_FM_LOOP_START;
		if (cmdId == SASAMI_PAL_CMD_LOOP_END) kind = SC_EV_FM_LOOP_END;
		else if (cmdId == SASAMI_PAL_CMD_PED_ON) kind = SC_EV_PEDAL_ON;
		else if (cmdId == SASAMI_PAL_CMD_PED_OFF) kind = SC_EV_PEDAL_OFF;
		if (eraser) {
			HistPushOwner();
			ScDeleteMarksAt(m_ev, m_evCount, atTick, ch, kind);
			AfterEdit();
			return;
		}
		int ok = 0;
		if (cmdId == SASAMI_PAL_CMD_LOOP_START) {
			int n = 2;
			const int toggling = (!stack && ScMarkKindExists(m_ev, *m_evCount, atTick, (uint8_t)ch, SC_EV_FM_LOOP_START));
			if (!toggling) {
				if (CSasamiSimpleInputDlg::AskNumber(this, L"ループ開始 |:", L"繰り返し回数 (1–99)", 2, 1, 99, &n) != IDOK)
					return;
			}
			HistPushOwner();
			ok = m_isFm && m_fmScore
				? ScFmAddLoopStart(m_fmScore->DocMutable(), atTick, ch, toggling ? 2 : n, toggling ? 0 : stack)
				: (m_midiScore ? ScMidiAddLoopStart(m_midiScore->Doc(), atTick, ch, toggling ? 2 : n, toggling ? 0 : stack) : 0);
		} else if (cmdId == SASAMI_PAL_CMD_LOOP_END) {
			HistPushOwner();
			ok = m_isFm && m_fmScore
				? ScFmAddLoopEnd(m_fmScore->DocMutable(), atTick, ch, stack)
				: (m_midiScore ? ScMidiAddLoopEnd(m_midiScore->Doc(), atTick, ch, stack) : 0);
		} else if (!m_isFm && m_midiScore) {
			HistPushOwner();
			ok = (cmdId == SASAMI_PAL_CMD_PED_ON)
				? ScMidiAddPedalOn(m_midiScore->Doc(), atTick, ch, stack)
				: ScMidiAddPedalOff(m_midiScore->Doc(), atTick, ch, stack);
		}
		if (ok) AfterEdit();
		else Refresh();
		return;
	}
	case SASAMI_PAL_CMD_OTTAVA_8VA:
	case SASAMI_PAL_CMD_OTTAVA_8VB:
	case SASAMI_PAL_CMD_OTTAVA_16VA:
	case SASAMI_PAL_CMD_OTTAVA_16VB:
	case SASAMI_PAL_CMD_OTTAVA_32VA:
	case SASAMI_PAL_CMD_OTTAVA_32VB:
	case SASAMI_PAL_CMD_OTTAVA_LOCO: {
		if (!m_ev || !m_evCount) return;
		int oct = 0;
		if (cmdId == SASAMI_PAL_CMD_OTTAVA_8VA) oct = 1;
		else if (cmdId == SASAMI_PAL_CMD_OTTAVA_8VB) oct = -1;
		else if (cmdId == SASAMI_PAL_CMD_OTTAVA_16VA) oct = 2;
		else if (cmdId == SASAMI_PAL_CMD_OTTAVA_16VB) oct = -2;
		else if (cmdId == SASAMI_PAL_CMD_OTTAVA_32VA) oct = 3;
		else if (cmdId == SASAMI_PAL_CMD_OTTAVA_32VB) oct = -3;
		HistPushOwner();
		if (eraser) {
			ScDeleteMarksAt(m_ev, m_evCount, atTick, ch, SC_EV_OTTAVA);
			ScDeleteMarksAt(m_ev, m_evCount, atTick, ch, SC_EV_OTTAVA_END);
			AfterEdit();
			return;
		}
		int ok = 0;
		if (m_isFm && m_fmScore)
			ok = (oct == 0) ? ScFmAddOttavaEnd(m_fmScore->DocMutable(), atTick, ch, stack)
				: ScFmAddOttava(m_fmScore->DocMutable(), atTick, ch, oct, stack);
		else if (m_midiScore)
			ok = (oct == 0) ? ScMidiAddOttavaEnd(m_midiScore->Doc(), atTick, ch, stack)
				: ScMidiAddOttava(m_midiScore->Doc(), atTick, ch, oct, stack);
		if (ok) AfterEdit();
		return;
	}
	case SASAMI_PAL_CMD_SVIB:
	case SASAMI_PAL_CMD_STREM:
	case SASAMI_PAL_CMD_SPAN:
	case SASAMI_PAL_CMD_SPORTA:
	case SASAMI_PAL_CMD_SVIB_OFF:
		if (!m_ev || !m_evCount) return;
		HistPushOwner();
		if (ScStaffAskAndPlaceSoftFx(this, m_ev, m_evCount, atTick, ch, cmdId, stack, eraser))
			AfterEdit();
		else
			Refresh();
		return;
	default:
		return;
	}
}

void CSasamiPianoRollDlg::AuditionKey(int note)
{
	m_roll.hoverNote = note;
	int part = CurCh() + 1;
	if (part < 1) part = 1;
	if (part > 32) part = 32;
	VstLiveAuditionNote(part, note, 100, 280);
	wchar_t nm[16];
	ScPianoRollNoteName(note, nm, 16);
	if (m_status.GetSafeHwnd()) {
		CString s;
		s.Format(L"%s  (%d)", nm, note);
		m_status.SetWindowText(s);
	}
	Refresh();
}

void CSasamiPianoRollDlg::PlaceNotePitchAtMarker(int note)
{
	if (!m_ev || !m_evCount || !m_ui || !m_curPart) return;
	uint32_t tick = m_ui->markerTick;
	const uint32_t q = (uint32_t)ScStaffPlaceQuant(m_ui);
	if (q) tick = (tick / q) * q;
	const int dur = m_ui->placeDur > 0 ? m_ui->placeDur : SC_PPQN / 4;
	HistPushOwner();
	if (m_isFm && m_fmScore)
		ScFmAddNote(m_fmScore->DocMutable(), tick, *m_curPart, CSasamiFmScoreDlg::MidiToFmNoteByte(note), dur);
	else if (m_midiScore)
		ScMidiAddNote(m_midiScore->Doc(), tick, *m_curPart, note, dur, 100);
	m_ui->markerTick = tick + (uint32_t)dur;
	AfterEdit();
}

void CSasamiPianoRollDlg::ApplyStripAt(CPoint pt, int erase)
{
	if (!m_ui || m_ui->stripCount <= 0) return;
	int lane = 0, col = 0, val = 0;
	if (!ScStaffHitStrip(m_stripRc, m_ui, pt, &lane, &col, &val)) return;
	if (erase)
		m_ui->strip[lane][col] = (uint8_t)((m_ui->stripKind[lane] == SC_STRIP_PITCH) ? 64 : 0);
	else {
		if (val < 0) val = 0;
		if (val > 127) val = 127;
		m_ui->strip[lane][col] = (uint8_t)val;
	}
	if (m_midiScore) ScStaffApplyStripToDocMidi(m_midiScore->Doc(), CurCh(), m_ui);
	else if (m_fmScore) ScStaffApplyStripToDocFm(m_fmScore->DocMutable(), CurCh(), m_ui);
	if (m_gridRc.Width() > 8)
		m_ui->markerTick = ScPianoRollXToTick(&m_roll, m_gridRc, m_ui, pt.x);
	InvalidateRect(m_stripRc, FALSE);
}

void CSasamiPianoRollDlg::PlaceNoteAt(CPoint pt)
{
	if (!m_ev || !m_evCount || !m_ui || !m_curPart) return;
	if (ScPianoRollPtInKeys(m_gridRc, pt) || pt.y < m_gridRc.top + SC_ROLL_MARK_H) return;
	uint32_t tick = ScPianoRollXToTick(&m_roll, m_gridRc, m_ui, pt.x);
	tick = (tick / (uint32_t)ScStaffPlaceQuant(m_ui)) * (uint32_t)ScStaffPlaceQuant(m_ui);
	int note = ScPianoRollYToNote(&m_roll, m_gridRc, pt.y);
	HistPushOwner();
	if (m_isFm && m_fmScore) {
		uint8_t nb = CSasamiFmScoreDlg::MidiToFmNoteByte(note);
		ScFmAddNote(m_fmScore->DocMutable(), tick, *m_curPart, nb, m_ui->placeDur > 0 ? m_ui->placeDur : SC_PPQN / 4);
	} else if (m_midiScore) {
		ScMidiAddNote(m_midiScore->Doc(), tick, *m_curPart, note, m_ui->placeDur > 0 ? m_ui->placeDur : SC_PPQN / 4, 100);
	}
	m_ui->markerTick = tick;
	AfterEdit();
}

void CSasamiPianoRollDlg::EraseAt(CPoint pt)
{
	if (!m_ev || !m_evCount || !m_ui || !m_curPart) return;
	int hit = ScPianoRollHitMark(&m_roll, m_gridRc, m_ev, *m_evCount, m_ui, *m_curPart, pt);
	if (hit < 0) hit = ScPianoRollHitResize(&m_roll, m_gridRc, m_ev, *m_evCount, m_ui, *m_curPart, pt);
	if (hit < 0) hit = ScPianoRollHitNote(&m_roll, m_gridRc, m_ev, *m_evCount, m_ui, *m_curPart, pt);
	if (hit < 0) return;
	HistPushOwner();
	ScStaffSelClear(m_ui);
	ScStaffSelAdd(m_ui, hit);
	ScStaffSelDelete(m_ev, m_evCount, m_ui);
	AfterEdit();
}

void CSasamiPianoRollDlg::BeginSelectOrDrag(CPoint pt, UINT nFlags)
{
	if (!m_ev || !m_evCount || !m_ui || !m_curPart) return;
	int mk = ScPianoRollHitMark(&m_roll, m_gridRc, m_ev, *m_evCount, m_ui, *m_curPart, pt);
	if (mk >= 0) {
		if (!(nFlags & MK_CONTROL)) {
			if (!ScStaffSelHas(m_ui, mk)) { ScStaffSelClear(m_ui); ScStaffSelAdd(m_ui, mk); }
		} else ScStaffSelAdd(m_ui, mk);
		m_ui->markerTick = m_ev[mk].tick;
		Refresh();
		return;
	}
	int rz = ScPianoRollHitResize(&m_roll, m_gridRc, m_ev, *m_evCount, m_ui, *m_curPart, pt);
	int hit = ScPianoRollHitNote(&m_roll, m_gridRc, m_ev, *m_evCount, m_ui, *m_curPart, pt);
	m_histDragPushed = 0;
	if (rz >= 0) {
		if (!(nFlags & MK_CONTROL)) {
			if (!ScStaffSelHas(m_ui, rz)) { ScStaffSelClear(m_ui); ScStaffSelAdd(m_ui, rz); }
		} else ScStaffSelAdd(m_ui, rz);
		m_dragMode = 2; m_resizeEv = rz;
		m_dragLastX = pt.x; m_dragLastY = pt.y;
		SetCapture();
		return;
	}
	if (hit >= 0) {
		if (!(nFlags & MK_CONTROL)) {
			if (!ScStaffSelHas(m_ui, hit)) { ScStaffSelClear(m_ui); ScStaffSelAdd(m_ui, hit); }
		} else ScStaffSelAdd(m_ui, hit);
		m_ui->markerTick = m_ev[hit].tick;
		m_dragMode = 1;
		m_dragLastX = pt.x; m_dragLastY = pt.y;
		SetCapture();
		Refresh();
		return;
	}
	if (!(nFlags & MK_CONTROL)) ScStaffSelClear(m_ui);
	m_ui->markerTick = ScPianoRollXToTick(&m_roll, m_gridRc, m_ui, pt.x);
	m_marquee = 1;
	m_marquee0 = m_marquee1 = pt;
	m_dragMode = 3;
	SetCapture();
	Refresh();
}

void CSasamiPianoRollDlg::OnPaint()
{
	CPaintDC dc(this);
	CRect rc; GetClientRect(&rc);
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
#if CCUSTOM_AERO_SUPPORT
	CCC_FillRectAlpha(dc.GetSafeHdc(), CRect(0, capH, rc.right, rc.bottom), RGB(32, 34, 40), 255);
#else
	dc.FillSolidRect(0, capH, rc.Width(), rc.Height() - capH, RGB(32, 34, 40));
#endif
	if (m_gridRc.Width() >= 8 && m_gridRc.Height() >= 8 && m_ev && m_evCount && m_ui && m_curPart) {
		ScPianoRollPaint(dc, m_gridRc, &m_roll, m_ev, *m_evCount, m_ui, *m_curPart);
		if (m_marquee && m_dragMode == 3) {
			CRect mr(m_marquee0, m_marquee1);
			ScPianoRollPaintSelectionMarquee(dc, mr);
		}
	}
	if (m_ui && m_bodyRc.Width() > 24)
		ScPianoRollPaintBars(dc, ScrollOuter(), &m_roll, m_ui, m_ev, m_evCount ? *m_evCount : 0);
	if (m_stripRc.Height() >= 8 && m_ui && m_ev && m_evCount) {
		CDC mem;
		if (mem.CreateCompatibleDC(&dc)) {
			CBitmap bmp;
			if (bmp.CreateCompatibleBitmap(&dc, m_stripRc.Width(), m_stripRc.Height())) {
				CBitmap* old = mem.SelectObject(&bmp);
				CRect local(0, 0, m_stripRc.Width(), m_stripRc.Height());
				mem.FillSolidRect(local, RGB(28, 30, 36));
				ScStaffPaintStrip(mem, local, m_ui);
				CCC_BlitStretchOpaque(dc.GetSafeHdc(), m_stripRc.left, m_stripRc.top, m_stripRc.Width(), m_stripRc.Height(),
					mem.GetSafeHdc(), 0, 0, m_stripRc.Width(), m_stripRc.Height());
				mem.SelectObject(old);
			}
		}
	}
	CCC_CaptionPaintGdi(dc, m_hWnd);
}

void CSasamiPianoRollDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
	CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
	if (!m_ui) return;
	int sbPos = 0;
	const int sbHit = ScPianoRollHitBar(ScrollOuter(), &m_roll, m_ui, m_ev, m_evCount ? *m_evCount : 0, point, &sbPos);
	if (sbHit == 3) { ScStaffZoomPxBeat(m_ui, -8); UpdateScrollBars(); Refresh(); if (m_midiScore) m_midiScore->Invalidate(FALSE); if (m_fmScore) m_fmScore->Invalidate(FALSE); return; }
	if (sbHit == 4) { ScStaffZoomPxBeat(m_ui, 8); UpdateScrollBars(); Refresh(); if (m_midiScore) m_midiScore->Invalidate(FALSE); if (m_fmScore) m_fmScore->Invalidate(FALSE); return; }
	if (sbHit == 5) {
		if (m_roll.zoomH <= 0) m_roll.zoomH = m_roll.rowH;
		m_roll.zoomH = max(SC_ROLL_ROW_H_MIN, m_roll.zoomH - 2);
		FitRollKeys(); UpdateScrollBars(); Refresh(); return;
	}
	if (sbHit == 6) {
		if (m_roll.zoomH <= 0) m_roll.zoomH = m_roll.rowH;
		m_roll.zoomH = min(48, m_roll.zoomH + 2);
		FitRollKeys(); UpdateScrollBars(); Refresh(); return;
	}
	if (sbHit == 1) {
		m_sbDrag = 1;
		m_sbDragScroll0 = m_roll.scrollY;
		m_sbDragAnchor = point.y;
		m_roll.scrollY = sbPos;
		FitRollKeys(); UpdateScrollBars(); Refresh(); SetCapture();
		return;
	}
	if (sbHit == 2) {
		m_sbDrag = 2;
		m_sbDragScroll0 = m_ui->scrollX;
		m_sbDragAnchor = point.x;
		m_ui->scrollX = sbPos;
		UpdateScrollBars(); Refresh(); SetCapture();
		return;
	}
	if (!m_ev || !m_evCount || !m_curPart) return;
	if (!m_gridRc.PtInRect(point)) {
		if (m_stripRc.Height() > 4 && m_stripRc.PtInRect(point) && m_ui->stripCount > 0) {
			if (!m_histDragPushed) { HistPushOwner(); m_histDragPushed = 1; }
			ApplyStripAt(point, m_ui->tool == SC_TOOL_ERASER ? 1 : 0);
			m_dragMode = 5;
			SetCapture();
		}
		return;
	}
	if (ScPianoRollPtInKeys(m_gridRc, point)) {
		int note = ScPianoRollYToNote(&m_roll, m_gridRc, point.y);
		AuditionKey(note);
		if (nFlags & MK_CONTROL)
			PlaceNotePitchAtMarker(note);
		return;
	}
	if (ScPianoRollPtInMarkLane(m_gridRc, point)) {
		m_ui->markerTick = ScPianoRollXToTick(&m_roll, m_gridRc, m_ui, point.x);
		if (m_ui->tool == SC_TOOL_ERASER) { EraseAt(point); return; }
		BeginSelectOrDrag(point, nFlags);
		return;
	}
	if (m_ui->tool == SC_TOOL_PENCIL) { PlaceNoteAt(point); return; }
	if (m_ui->tool == SC_TOOL_ERASER) { EraseAt(point); m_dragMode = 4; SetCapture(); return; }
	BeginSelectOrDrag(point, nFlags);
}

void CSasamiPianoRollDlg::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	CCustomBlurDialogExBase::OnLButtonDblClk(nFlags, point);
	if (!m_ev || !m_evCount || !m_ui || !m_curPart) return;
	if (m_gridRc.PtInRect(point) && ScPianoRollPtInKeys(m_gridRc, point)) {
		int note = ScPianoRollYToNote(&m_roll, m_gridRc, point.y);
		AuditionKey(note);
		PlaceNotePitchAtMarker(note);
	}
	(void)nFlags;
}

void CSasamiPianoRollDlg::OnLButtonUp(UINT nFlags, CPoint point)
{
	CCustomBlurDialogExBase::OnLButtonUp(nFlags, point);
	if (m_dragMode == 3 && m_marquee && m_ui && m_ev && m_evCount && m_curPart) {
		CRect mr(m_marquee0, m_marquee1);
		mr.NormalizeRect();
		int idx[SC_SEL_MAX];
		int n = ScPianoRollHitInRect(&m_roll, m_gridRc, m_ev, *m_evCount, m_ui, *m_curPart, mr, idx, SC_SEL_MAX);
		if (!(nFlags & MK_CONTROL)) ScStaffSelClear(m_ui);
		for (int i = 0; i < n; i++) ScStaffSelAdd(m_ui, idx[i]);
		if (n > 0) {
			m_ui->selRangeValid = 1;
			m_ui->selRangeT0 = ScPianoRollXToTick(&m_roll, m_gridRc, m_ui, mr.left);
			m_ui->selRangeT1 = ScPianoRollXToTick(&m_roll, m_gridRc, m_ui, mr.right);
		}
	}
	m_dragMode = 0; m_marquee = 0; m_resizeEv = -1;
	m_histDragPushed = 0;
	m_sbDrag = 0;
	ReleaseCapture();
	Refresh();
	(void)point;
}

void CSasamiPianoRollDlg::OnMouseMove(UINT nFlags, CPoint point)
{
	CCustomBlurDialogExBase::OnMouseMove(nFlags, point);
	if (!m_ui || !m_ev || !m_evCount) return;
	if (m_sbDrag && (nFlags & MK_LBUTTON)) {
		const CRect outer = ScrollOuter();
		if (m_sbDrag == 1) {
			m_roll.scrollY = ScPianoRollMapVertDrag(outer, &m_roll, max(1, m_gridRc.Height() - SC_ROLL_MARK_H),
				point.y, m_sbDragAnchor, m_sbDragScroll0);
			FitRollKeys();
		} else {
			m_ui->scrollX = ScPianoRollMapHorzDrag(outer, m_ui, m_ev, *m_evCount,
				ScPianoRollTimePageW(m_gridRc), point.x, m_sbDragAnchor, m_sbDragScroll0);
		}
		UpdateScrollBars();
		Refresh();
		return;
	}
	if (m_dragMode == 5 && (nFlags & MK_LBUTTON)) {
		ApplyStripAt(point, m_ui->tool == SC_TOOL_ERASER ? 1 : 0);
		return;
	}
	if (m_dragMode == 0 && m_gridRc.PtInRect(point) && ScPianoRollPtInKeys(m_gridRc, point)) {
		int note = ScPianoRollYToNote(&m_roll, m_gridRc, point.y);
		if (note != m_roll.hoverNote) { m_roll.hoverNote = note; Refresh(); }
	} else if (m_dragMode == 0 && m_roll.hoverNote >= 0) {
		m_roll.hoverNote = -1;
		Refresh();
	}
	if (m_dragMode == 4 && (nFlags & MK_LBUTTON)) { EraseAt(point); return; }
	if (m_dragMode == 3) { m_marquee1 = point; Refresh(); return; }
	if (m_dragMode == 1 && (nFlags & MK_LBUTTON)) {
		const int pxBeat = m_roll.pxBeat > 0 ? m_roll.pxBeat : SC_PX_BEAT_DEFAULT;
		int dTick = ((point.x - m_dragLastX) * SC_PPQN) / max(1, pxBeat);
		int dSemi = (m_dragLastY - point.y) / max(1, m_roll.rowH);
		if (dTick || dSemi) {
			if (!m_histDragPushed) { HistPushOwner(); m_histDragPushed = 1; }
			ScStaffSelMoveBy(m_ev, m_evCount, m_ui, dTick, dSemi, m_isFm);
			m_dragLastX = point.x; m_dragLastY = point.y;
			AfterEdit();
		}
		return;
	}
	if (m_dragMode == 2 && (nFlags & MK_LBUTTON) && m_resizeEv >= 0 && m_resizeEv < *m_evCount) {
		const int pxBeat = m_roll.pxBeat > 0 ? m_roll.pxBeat : SC_PX_BEAT_DEFAULT;
		int dTick = ((point.x - m_dragLastX) * SC_PPQN) / max(1, pxBeat);
		if (dTick) {
			if (!m_histDragPushed) { HistPushOwner(); m_histDragPushed = 1; }
			int nd = (int)m_ev[m_resizeEv].dur + dTick;
			if (nd < ScStaffPlaceQuant(m_ui)) nd = ScStaffPlaceQuant(m_ui);
			m_ev[m_resizeEv].dur = (uint32_t)nd;
			m_dragLastX = point.x;
			AfterEdit();
		}
	}
}

BOOL CSasamiPianoRollDlg::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	if (nFlags & MK_SHIFT) {
		if (m_roll.zoomH <= 0) m_roll.zoomH = m_roll.rowH;
		m_roll.zoomH += (zDelta > 0) ? 2 : -2;
		if (m_roll.zoomH < SC_ROLL_ROW_H_MIN) m_roll.zoomH = SC_ROLL_ROW_H_MIN;
		if (m_roll.zoomH > 48) m_roll.zoomH = 48;
		FitRollKeys();
	} else if (nFlags & MK_CONTROL && m_ui) {
		m_ui->scrollX -= (zDelta / 4);
		if (m_ui->scrollX < 0) m_ui->scrollX = 0;
		if (m_midiScore) m_midiScore->Invalidate(FALSE);
		if (m_fmScore) m_fmScore->Invalidate(FALSE);
	} else {
		m_roll.scrollY -= (zDelta / 30) * max(1, m_roll.rowH);
		FitRollKeys();
	}
	Refresh();
	(void)pt;
	UpdateScrollBars();
	return TRUE;
}

void CSasamiPianoRollDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == 1 && m_ui && m_ui->previewActive)
		Invalidate(FALSE);
	CCustomBlurDialogExBase::OnTimer(nIDEvent);
}

void CSasamiPianoRollDlg::OnContextMenu(CWnd* pWnd, CPoint point)
{
	(void)pWnd;
	if (!m_ui) return;
	if (m_ui->tool == SC_TOOL_PENCIL || m_ui->tool == SC_TOOL_TEMPO)
		ScStaffEnterSelectTool(m_ui);
	CPoint pt = point;
	CPoint client = point;
	if (pt.x < 0) { pt = CPoint(0, 0); ClientToScreen(&pt); client = CPoint(0, 0); }
	else ScreenToClient(&client);
	if (m_gridRc.PtInRect(client) && client.x >= m_gridRc.left + SC_ROLL_KEY_W)
		m_ui->markerTick = ScPianoRollXToTick(&m_roll, m_gridRc, m_ui, client.x);
	CCustomPopupMenu menu;
	menu.SetAeroMode(FALSE);
	menu.AddCommand(9001, LL14(L"コピー\tCtrl+C", L"Copy\tCtrl+C", L"Copier", L"Copia", L"Copiar", L"복사", L"复制", L"نسخ", L"Копировать", L"Kopieren", L"Copiar", L"Kopiëren", L"Kopiuj", L"Kopyala"));
	menu.AddCommand(9002, LL14(L"切り取り\tCtrl+X", L"Cut\tCtrl+X", L"Couper", L"Taglia", L"Cortar", L"잘라내기", L"剪切", L"قص", L"Вырезать", L"Ausschneiden", L"Recortar", L"Knippen", L"Wytnij", L"Kes"));
	menu.AddCommand(9003, LL14(L"貼り付け\tCtrl+V", L"Paste\tCtrl+V", L"Coller", L"Incolla", L"Pegar", L"붙여넣기", L"粘贴", L"لصق", L"Вставить", L"Einfügen", L"Colar", L"Plakken", L"Wklej", L"Yapıştır"));
	menu.AddCommand(9060, LL14(L"範囲に空白を挿入\tCtrl+Shift+I", L"Insert blank in range\tCtrl+Shift+I", L"Insérer vide", L"Inserisci vuoto", L"Insertar vacío", L"빈 구간 삽입", L"插入空白", L"إدراج فراغ", L"Вставить пустоту", L"Leerraum", L"Inserir vazio", L"Leeg invoegen", L"Wstaw pustkę", L"Boş ekle"));
	menu.AddCommand(9004, LL14(L"削除\tDelete", L"Delete\tDelete", L"Supprimer", L"Elimina", L"Eliminar", L"삭제", L"删除", L"حذف", L"Удалить", L"Löschen", L"Apagar", L"Verwijderen", L"Usuń", L"Sil"));
	menu.AddCommand(9005, LL14(L"タイ\tCtrl+T", L"Tie\tCtrl+T", L"Liaison", L"Legatura", L"Ligadura", L"타이", L"连音", L"ربط", L"Лига", L"Bindebogen", L"Ligadura", L"Boog", L"Łuk", L"Bağ"));
	menu.AddSeparator();
	menu.AddCommand(9101, LL14(L"ループ開始 (|:n)…", L"Loop start (|:n)…", L"Début de boucle", L"Inizio loop", L"Inicio de bucle", L"루프 시작", L"循环开始", L"بداية الحلقة", L"Начало цикла", L"Schleifenstart", L"Início do loop", L"Lusbegin", L"Początek pętli", L"Döngü başlangıcı"));
	menu.AddCommand(9102, LL14(L"ループ終了 (:|)", L"Loop end (:|)", L"Fin de boucle", L"Fine loop", L"Fin de bucle", L"루프 끝", L"循环结束", L"نهاية الحلقة", L"Конец цикла", L"Schleifenende", L"Fim do loop", L"Luseinde", L"Koniec pętli", L"Döngü sonu"));
	if (!m_isFm) {
		menu.AddCommand(9103, LL14(L"ペダルON (Ped.)", L"Pedal ON", L"Pédale ON", L"Pedale ON", L"Pedal ON", L"페달 ON", L"踏板ON", L"دواسة ON", L"Педаль ON", L"Pedal ON", L"Pedal ON", L"Pedaal ON", L"Pedał ON", L"Pedal ON"));
		menu.AddCommand(9104, LL14(L"ペダルOFF (＊)", L"Pedal OFF", L"Pédale OFF", L"Pedale OFF", L"Pedal OFF", L"페달 OFF", L"踏板OFF", L"دواسة OFF", L"Педаль OFF", L"Pedal OFF", L"Pedal OFF", L"Pedaal OFF", L"Pedał OFF", L"Pedal OFF"));
	}
	menu.AddCommand(9105, LL14(L"8va", L"8va", L"8va", L"8va", L"8va", L"8va", L"8va", L"8va", L"8va", L"8va", L"8va", L"8va", L"8va", L"8va"));
	menu.AddCommand(9106, LL14(L"8vb", L"8vb", L"8vb", L"8vb", L"8vb", L"8vb", L"8vb", L"8vb", L"8vb", L"8vb", L"8vb", L"8vb", L"8vb", L"8vb"));
	menu.AddCommand(9107, LL14(L"loco", L"loco", L"loco", L"loco", L"loco", L"loco", L"loco", L"loco", L"loco", L"loco", L"loco", L"loco", L"loco", L"loco"));
	menu.AddSeparator();
	menu.AddCommand(9110, LL14(L"@SVIB ビブラート…", L"@SVIB vibrato…", L"@SVIB vibrato…", L"@SVIB vibrato…", L"@SVIB vibrato…", L"@SVIB 비브라토…", L"@SVIB 颤音…", L"@SVIB", L"@SVIB", L"@SVIB Vibrato…", L"@SVIB vibrato…", L"@SVIB vibrato…", L"@SVIB wibrato…", L"@SVIB vibrato…"));
	menu.AddCommand(9111, LL14(L"@STREM トレモロ…", L"@STREM tremolo…", L"@STREM tremolo…", L"@STREM tremolo…", L"@STREM trémolo…", L"@STREM 트레몰로…", L"@STREM 震音…", L"@STREM", L"@STREM", L"@STREM Tremolo…", L"@STREM tremolo…", L"@STREM tremolo…", L"@STREM tremolo…", L"@STREM tremolo…"));
	menu.AddCommand(9112, LL14(L"@SPAN パンLFO…", L"@SPAN pan LFO…", L"@SPAN pan LFO…", L"@SPAN pan LFO…", L"@SPAN pan LFO…", L"@SPAN 팬 LFO…", L"@SPAN 声像LFO…", L"@SPAN", L"@SPAN", L"@SPAN Pan-LFO…", L"@SPAN pan LFO…", L"@SPAN pan-LFO…", L"@SPAN pan LFO…", L"@SPAN pan LFO…"));
	menu.AddCommand(9113, LL14(L"@SPORTA ポルタメント…", L"@SPORTA portamento…", L"@SPORTA portamento…", L"@SPORTA portamento…", L"@SPORTA portamento…", L"@SPORTA 포르타멘토…", L"@SPORTA 滑音…", L"@SPORTA", L"@SPORTA", L"@SPORTA Portamento…", L"@SPORTA portamento…", L"@SPORTA portamento…", L"@SPORTA portamento…", L"@SPORTA portamento…"));
	menu.AddCommand(9114, LL14(L"ソフトFXオフ", L"Soft FX off", L"FX off", L"FX off", L"FX off", L"소프트 FX 끔", L"软效果关", L"FX off", L"FX выкл", L"FX aus", L"FX off", L"FX uit", L"FX wył", L"FX kapalı"));
	const UINT cmd = menu.Track(pt, this);
	if (!cmd || !m_ev || !m_evCount) return;
	ScEvent* clip = ClipBuf();
	int* clipN = ClipCountPtr();
	uint32_t* clipBase = ClipBasePtr();
	uint32_t* clipSpan = ClipSpanPtr();
	if (cmd == 9001 && clip && clipN && clipBase && clipSpan) {
		*clipN = ScStaffSelCopyEx(m_ev, *m_evCount, m_ui, m_isFm, clip, SC_CLIP_MAX, clipBase, clipSpan);
	} else if (cmd == 9002 && clip && clipN && clipBase && clipSpan) {
		*clipN = ScStaffSelCopyEx(m_ev, *m_evCount, m_ui, m_isFm, clip, SC_CLIP_MAX, clipBase, clipSpan);
		HistPushOwner();
		ScStaffSelDelete(m_ev, m_evCount, m_ui);
		AfterEdit();
	} else if (cmd == 9003 && clip && clipN && *clipN > 0 && clipBase && clipSpan) {
		HistPushOwner();
		ScStaffSelPasteEx(m_ev, m_evCount, EvMax(), m_ui, m_isFm, clip, *clipN, m_ui->markerTick, *clipSpan, m_ui->pasteInsert);
		AfterEdit();
	} else if (cmd == 9060) {
		HistPushOwner();
		ScStaffInsertBlankRange(m_ev, *m_evCount, m_ui);
		AfterEdit();
	} else if (cmd == 9004) {
		HistPushOwner();
		ScStaffSelDelete(m_ev, m_evCount, m_ui);
		AfterEdit();
	} else if (cmd == 9005) {
		HistPushOwner();
		ScStaffTieSelected(m_ev, *m_evCount, m_ui);
		AfterEdit();
	} else if (cmd == 9101) HandlePalCmd(SASAMI_PAL_CMD_LOOP_START);
	else if (cmd == 9102) HandlePalCmd(SASAMI_PAL_CMD_LOOP_END);
	else if (cmd == 9103) HandlePalCmd(SASAMI_PAL_CMD_PED_ON);
	else if (cmd == 9104) HandlePalCmd(SASAMI_PAL_CMD_PED_OFF);
	else if (cmd == 9105) HandlePalCmd(SASAMI_PAL_CMD_OTTAVA_8VA);
	else if (cmd == 9106) HandlePalCmd(SASAMI_PAL_CMD_OTTAVA_8VB);
	else if (cmd == 9107) HandlePalCmd(SASAMI_PAL_CMD_OTTAVA_LOCO);
	else if (cmd == 9110) HandlePalCmd(SASAMI_PAL_CMD_SVIB);
	else if (cmd == 9111) HandlePalCmd(SASAMI_PAL_CMD_STREM);
	else if (cmd == 9112) HandlePalCmd(SASAMI_PAL_CMD_SPAN);
	else if (cmd == 9113) HandlePalCmd(SASAMI_PAL_CMD_SPORTA);
	else if (cmd == 9114) HandlePalCmd(SASAMI_PAL_CMD_SVIB_OFF);
}

void CSasamiPianoRollDlg::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	const int ctrl = (GetKeyState(VK_CONTROL) & 0x8000) ? 1 : 0;
	const int shift = (GetKeyState(VK_SHIFT) & 0x8000) ? 1 : 0;
	if (nChar == VK_ESCAPE && m_ui) { ScStaffEnterSelectTool(m_ui); UpdateHelpBar(); Refresh(); return; }
	if (nChar == VK_DELETE || nChar == VK_BACK) {
		if (m_ev && m_evCount && m_ui && (m_ui->nSel > 0 || m_ui->selEv >= 0)) {
			HistPushOwner(); ScStaffSelDelete(m_ev, m_evCount, m_ui); AfterEdit();
		}
		return;
	}
	if (nChar == VK_SPACE) { OnBnPlay(); return; }
	if (nChar == VK_HOME && m_ui) {
		m_ui->markerTick = 0; m_ui->scrollX = 0; Refresh();
		if (m_status.GetSafeHwnd())
			m_status.SetWindowText(LL14(L"マーカー→0 (Home)", L"Marker → 0 (Home)", L"Marqueur → 0", L"Marcatore → 0", L"Marcador → 0", L"마커→0", L"标记→0", L"علامة → 0", L"Маркер → 0", L"Markierung → 0", L"Marcador → 0", L"Markering → 0", L"Znacznik → 0", L"İşaret → 0"));
		return;
	}
	if (ctrl && (nChar == 'Z' || nChar == 'z')) { if (shift) Redo(); else Undo(); return; }
	if (ctrl && (nChar == 'Y' || nChar == 'y')) { Redo(); return; }
	ScEvent* clip = ClipBuf();
	int* clipN = ClipCountPtr();
	uint32_t* clipBase = ClipBasePtr();
	uint32_t* clipSpan = ClipSpanPtr();
	if (ctrl && (nChar == 'C' || nChar == 'c') && clip && clipN && clipBase && clipSpan && m_ev && m_evCount && m_ui)
		*clipN = ScStaffSelCopyEx(m_ev, *m_evCount, m_ui, m_isFm, clip, SC_CLIP_MAX, clipBase, clipSpan);
	if (ctrl && (nChar == 'X' || nChar == 'x') && clip && clipN && clipBase && clipSpan && m_ev && m_evCount && m_ui) {
		*clipN = ScStaffSelCopyEx(m_ev, *m_evCount, m_ui, m_isFm, clip, SC_CLIP_MAX, clipBase, clipSpan);
		HistPushOwner(); ScStaffSelDelete(m_ev, m_evCount, m_ui); AfterEdit();
	}
	if (ctrl && (nChar == 'V' || nChar == 'v') && clip && clipN && *clipN > 0 && m_ev && m_evCount && m_ui) {
		int ins = m_ui->pasteInsert || (ctrl && shift);
		HistPushOwner();
		ScStaffSelPasteEx(m_ev, m_evCount, EvMax(), m_ui, m_isFm, clip, *clipN, m_ui->markerTick, clipSpan ? *clipSpan : 0, ins);
		AfterEdit();
	}
	if (ctrl && shift && (nChar == 'I' || nChar == 'i') && m_ev && m_evCount && m_ui) {
		HistPushOwner(); ScStaffInsertBlankRange(m_ev, *m_evCount, m_ui); AfterEdit();
	}
	if (ctrl && (nChar == 'A' || nChar == 'a') && m_ev && m_evCount && m_ui && m_curPart) {
		ScStaffSelClear(m_ui);
		for (int i = 0; i < *m_evCount; i++) {
			if (m_ev[i].ch != (uint8_t)*m_curPart) continue;
			if (m_ev[i].kind == SC_EV_NOTE || m_ev[i].kind == SC_EV_FM_NOTE)
				ScStaffSelAdd(m_ui, i);
		}
		Refresh();
	}
	if (ctrl && (nChar == 'T' || nChar == 't') && m_ev && m_evCount && m_ui) {
		HistPushOwner(); ScStaffTieSelected(m_ev, *m_evCount, m_ui); AfterEdit();
	}
	(void)nRepCnt; (void)nFlags;
}

BOOL CSasamiPianoRollDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd()) m_tooltip.RelayEvent(pMsg);
	if (pMsg->message == WM_KEYDOWN) {
		CWnd* f = GetFocus();
		const BOOL inEdit = (f && f->IsKindOf(RUNTIME_CLASS(CEdit)));
		if (!inEdit) {
			OnKeyDown((UINT)pMsg->wParam, 1, 0);
			return TRUE;
		}
	}
	return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}
