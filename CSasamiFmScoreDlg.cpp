#include "stdafx.h"
#include "ogg.h"
#include "CSasamiFmScoreDlg.h"
#include "CSasamiTextDlg.h"
#include "CSasamiFmVoiceDlg.h"
#include "CSasamiNotePaletteDlg.h"
#include "CSasamiNotePropsDlg.h"
#include "CCustomPopupMenu.h"
#include "OfflineHelp.h"
#include "PlayList.h"
#include "VstMidiEngine.h"
#include "kb_sasami/source/sasami_write.h"

extern CPlayList* pl;

enum { kScFmPreviewTimer = 7102 };

CSasamiFmScoreDlg* CSasamiFmScoreDlg::s_inst = NULL;

uint8_t CSasamiFmScoreDlg::MidiToFmNoteByte(int midiNote)
{
	int oct = midiNote / 12 - 1;
	int scale = midiNote % 12;
	if (oct < 1) oct = 1;
	if (oct > 7) oct = 7;
	if (scale < 0) scale = 0;
	if (scale > 11) scale = 11;
	return (uint8_t)(((oct & 0x0F) << 4) | (scale & 0x0F));
}

IMPLEMENT_DYNAMIC(CSasamiFmScoreDlg, CCustomBlurDialogExBase)

CSasamiFmScoreDlg::CSasamiFmScoreDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(CSasamiFmScoreDlg::IDD, pParent)
	, m_curCh(0), m_placeRest(0), m_accidental(0), m_noteCur(NULL), m_sbDrag(0), m_bInLayout(FALSE)
{
	m_lastOut[0] = 0;
	m_lastHoverSt[0] = 0;
	ScFmDocClear(&m_doc);
	static const uint8_t kDefVoice[25] = {
		0x3B, 0x00, 0x00, 0x20, 0x28, 0x20, 0x1A, 0x0D, 0x9F, 0x9E, 0xDE, 0x9E,
		0x05, 0x05, 0x05, 0x05, 0x0F, 0x0B, 0x0C, 0x0B, 0x8A, 0xF6, 0x86, 0xF7, 0x1B
	};
	ScFmAllocVoice(&m_doc, kDefVoice);
	ScStaffUiInit(&m_ui, SC_FM_TOTAL, 1);
}

CSasamiFmScoreDlg::~CSasamiFmScoreDlg()
{
	if (m_noteCur) { ::DestroyCursor(m_noteCur); m_noteCur = NULL; }
}

CSasamiFmScoreDlg* CSasamiFmScoreDlg::Instance() { return s_inst; }

void CSasamiFmScoreDlg::OpenOwned(CWnd* owner)
{
	if (s_inst && ::IsWindow(s_inst->GetSafeHwnd())) {
		CCC_PresentOwnedHelp(s_inst, owner ? owner : AfxGetMainWnd());
		return;
	}
	s_inst = new CSasamiFmScoreDlg(owner);
	CWnd* parent = AfxGetMainWnd();
	if (!parent) parent = owner;
	if (!s_inst->Create(IDD_SASAMI_FM_SCORE, parent)) {
		delete s_inst;
		s_inst = NULL;
		AfxMessageBox(L"FM Score: Create() failed (check resource IDD_SASAMI_FM_SCORE).", MB_ICONERROR);
		return;
	}
	CCC_PresentOwnedHelp(s_inst, parent);
}

void CSasamiFmScoreDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SASAMI_FM_OPEN, m_btnOpen);
	DDX_Control(pDX, IDC_SASAMI_FM_SAVE, m_btnSave);
	DDX_Control(pDX, IDC_SASAMI_FM_PLAY, m_btnPlay);
	DDX_Control(pDX, IDC_SASAMI_FM_EXPORT, m_btnExport);
	DDX_Control(pDX, IDC_SASAMI_FM_HELP, m_btnHelp);
	DDX_Control(pDX, IDC_SASAMI_FM_TEMPO, m_btnTempo);
	DDX_Control(pDX, IDC_SASAMI_FM_VOICEBTN, m_btnVoice);
	DDX_Control(pDX, IDC_SASAMI_FM_TOOL_PENCIL, m_btnPencil);
	DDX_Control(pDX, IDC_SASAMI_FM_TOOL_ERASE, m_btnErase);
	DDX_Control(pDX, IDC_SASAMI_FM_TOOL_SEL, m_btnSel);
	DDX_Control(pDX, IDC_SASAMI_FM_PAL, m_btnPal);
	DDX_Control(pDX, IDC_SASAMI_FM_PROP_UPD, m_btnPropUpd);
	DDX_Control(pDX, IDC_SASAMI_FM_PROP_NOTE, m_edNote);
	DDX_Control(pDX, IDC_SASAMI_FM_PROP_GT, m_edGt);
	DDX_Control(pDX, IDC_SASAMI_FM_PROP_VEL, m_edVel);
	DDX_Control(pDX, IDC_SASAMI_FM_CH, m_ch);
	DDX_Control(pDX, IDC_SASAMI_FM_STATUS, m_status);
	DDX_Control(pDX, IDC_SASAMI_FM_MARK, m_btnMark);
	DDX_Control(pDX, IDC_SASAMI_FM_LOOPA, m_btnLoopA);
	DDX_Control(pDX, IDC_SASAMI_FM_LOOPB, m_btnLoopB);
	DDX_Control(pDX, IDC_SASAMI_FM_LOOPCLR, m_btnLoopClr);
	DDX_Control(pDX, IDC_SASAMI_FM_SHOWALL, m_btnShowAll);
	DDX_Control(pDX, IDC_SASAMI_FM_TEXT, m_btnText);
}

BEGIN_MESSAGE_MAP(CSasamiFmScoreDlg, CCustomBlurDialogExBase)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_CLOSE()
	ON_WM_DESTROY()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_MOUSEMOVE()
	ON_WM_SETCURSOR()
	ON_WM_MOUSEWHEEL()
	ON_WM_HSCROLL()
	ON_WM_VSCROLL()
	ON_WM_CONTEXTMENU()
	ON_WM_TIMER()
	ON_BN_CLICKED(IDC_SASAMI_FM_OPEN, &CSasamiFmScoreDlg::OnBnClickedOpen)
	ON_BN_CLICKED(IDC_SASAMI_FM_SAVE, &CSasamiFmScoreDlg::OnBnClickedSave)
	ON_BN_CLICKED(IDC_SASAMI_FM_PLAY, &CSasamiFmScoreDlg::OnBnClickedPlay)
	ON_BN_CLICKED(IDC_SASAMI_FM_EXPORT, &CSasamiFmScoreDlg::OnBnClickedExport)
	ON_BN_CLICKED(IDC_SASAMI_FM_HELP, &CSasamiFmScoreDlg::OnBnClickedHelp)
	ON_BN_CLICKED(IDC_SASAMI_FM_TEMPO, &CSasamiFmScoreDlg::OnBnClickedTempo)
	ON_BN_CLICKED(IDC_SASAMI_FM_VOICEBTN, &CSasamiFmScoreDlg::OnBnClickedVoice)
	ON_BN_CLICKED(IDC_SASAMI_FM_TOOL_PENCIL, &CSasamiFmScoreDlg::OnBnClickedPencil)
	ON_BN_CLICKED(IDC_SASAMI_FM_TOOL_ERASE, &CSasamiFmScoreDlg::OnBnClickedErase)
	ON_BN_CLICKED(IDC_SASAMI_FM_TOOL_SEL, &CSasamiFmScoreDlg::OnBnClickedSel)
	ON_BN_CLICKED(IDC_SASAMI_FM_PAL, &CSasamiFmScoreDlg::OnBnClickedPal)
	ON_BN_CLICKED(IDC_SASAMI_FM_PROP_UPD, &CSasamiFmScoreDlg::OnBnClickedPropUpd)
	ON_BN_CLICKED(IDC_SASAMI_FM_MARK, &CSasamiFmScoreDlg::OnBnClickedMark)
	ON_BN_CLICKED(IDC_SASAMI_FM_LOOPA, &CSasamiFmScoreDlg::OnBnClickedLoopA)
	ON_BN_CLICKED(IDC_SASAMI_FM_LOOPB, &CSasamiFmScoreDlg::OnBnClickedLoopB)
	ON_BN_CLICKED(IDC_SASAMI_FM_LOOPCLR, &CSasamiFmScoreDlg::OnBnClickedLoopClr)
	ON_BN_CLICKED(IDC_SASAMI_FM_SHOWALL, &CSasamiFmScoreDlg::OnBnClickedShowAll)
	ON_BN_CLICKED(IDC_SASAMI_FM_TEXT, &CSasamiFmScoreDlg::OnBnClickedText)
	ON_CBN_SELCHANGE(IDC_SASAMI_FM_CH, &CSasamiFmScoreDlg::OnCbnSelchangeCh)
	ON_MESSAGE(WM_SASAMI_PAL_DUR, &CSasamiFmScoreDlg::OnPalDur)
	ON_MESSAGE(WM_SASAMI_NOTE_PROPS, &CSasamiFmScoreDlg::OnNoteProps)
	ON_MESSAGE(WM_APP + 61, &CSasamiFmScoreDlg::OnDeferredInit)
END_MESSAGE_MAP()

void CSasamiFmScoreDlg::ApplyLang()
{
	SetWindowText(LL14(L"SASAMI FM\u8b5c\u9762", L"SASAMI FM Score", L"Partition FM SASAMI", L"Partitura FM SASAMI", L"Partitura FM SASAMI",
		L"SASAMI FM Score", L"SASAMI FM Score", L"SASAMI FM Score", L"SASAMI FM Score", L"SASAMI FM-Partitur",
		L"Partitura FM SASAMI", L"SASAMI FM-partituur", L"Partytura SASAMI FM", L"SASAMI FM Skor"));
	m_btnOpen.SetWindowText(LL14(L"\u958b\u304f", L"Open", L"Ouvrir", L"Apri", L"Abrir", L"Open", L"Open", L"Open", L"Open", L"Offnen", L"Abrir", L"Openen", L"Otworz", L"Ac"));
	m_btnSave.SetWindowText(LL14(L"\u4fdd\u5b58", L"Save", L"Enregistrer", L"Salva", L"Guardar", L"Save", L"Save", L"Save", L"Save", L"Speichern", L"Salvar", L"Opslaan", L"Zapisz", L"Kaydet"));
	m_btnPlay.SetWindowText(LL14(L"\u518d\u751f\u78ba\u8a8d", L"Preview", L"Apercu", L"Anteprima", L"Vista previa", L"Preview", L"Preview", L"Preview", L"Preview", L"Vorschau", L"Previa", L"Voorbeeld", L"Podglad", L"Onizle"));
	m_btnExport.SetWindowText(LL14(L"\u66f8\u304d\u51fa\u3057", L"Export", L"Exporter", L"Esporta", L"Exportar", L"Export", L"Export", L"Export", L"Export", L"Export", L"Exportar", L"Exporteren", L"Eksport", L"Disa aktar"));
	m_btnHelp.SetWindowText(LL14(L"\u30d8\u30eb\u30d7", L"Help", L"Aide", L"Guida", L"Ayuda", L"Help", L"Help", L"Help", L"Help", L"Hilfe", L"Ajuda", L"Help", L"Pomoc", L"Yardim"));
	m_btnTempo.SetWindowText(LL14(L"\u30c6\u30f3\u30dd", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"Tempo"));
	m_btnVoice.SetWindowText(LL14(L"\u97f3\u8272", L"Voice", L"Timbre", L"Voce", L"Voz", L"Voice", L"Voice", L"Voice", L"Voice", L"Klang", L"Voz", L"Klank", L"Glos", L"Ses"));
	m_btnPencil.SetWindowText(LL14(L"\u925b\u7b46", L"Pencil", L"Crayon", L"Matita", L"Lapiz", L"Pencil", L"Pencil", L"Pencil", L"Pencil", L"Stift", L"Lapis", L"Potlood", L"Olowek", L"Kalem"));
	m_btnErase.SetWindowText(LL14(L"\u6d88\u3057\u30b4\u30e0", L"Erase", L"Gomme", L"Gomma", L"Borrar", L"Erase", L"Erase", L"Erase", L"Erase", L"Radierer", L"Borracha", L"Gum", L"Gumka", L"Silgi"));
	m_btnSel.SetWindowText(LL14(L"\u9078\u629e", L"Select", L"Selection", L"Selezione", L"Seleccionar", L"Select", L"Select", L"Select", L"Select", L"Auswahl", L"Selecionar", L"Selecteren", L"Zaznacz", L"Sec"));
	m_btnPal.SetWindowText(LL14(L"\u97f3\u7b26", L"Notes", L"Notes", L"Note", L"Notas", L"Notes", L"Notes", L"Notes", L"Notes", L"Noten", L"Notas", L"Noten", L"Nuty", L"Notalar"));
	m_btnPropUpd.SetWindowText(LL14(L"\u66f4\u65b0", L"Update", L"Maj", L"Aggiorna", L"Actualizar", L"Update", L"Update", L"Update", L"Update", L"Aktualisieren", L"Atualizar", L"Bijwerken", L"Aktualizuj", L"Guncelle"));
	m_btnMark.SetWindowText(L">|");
	m_btnLoopA.SetWindowText(L"A");
	m_btnLoopB.SetWindowText(L"B");
	m_btnLoopClr.SetWindowText(L"A-Bx");
	m_btnShowAll.SetWindowText(L"All");
	if (m_btnText.GetSafeHwnd())
		m_btnText.SetWindowText(LL14(L"\u30c6\u30ad\u30b9\u30c8", L"Text", L"Texte", L"Testo", L"Texto", L"Text", L"Text", L"Text", L"Text", L"Text", L"Texto", L"Tekst", L"Tekst", L"Metin"));
}

BOOL CSasamiFmScoreDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	s_inst = this;

	auto flatBtn = [](CCustomStandardButton& b) {
		if (!b.GetSafeHwnd()) return;
		b.SetAeroMode(FALSE);
		b.SetFlat(TRUE);
	};
	flatBtn(m_btnOpen); flatBtn(m_btnSave); flatBtn(m_btnPlay); flatBtn(m_btnExport);
	flatBtn(m_btnHelp); flatBtn(m_btnTempo); flatBtn(m_btnVoice); flatBtn(m_btnPencil);
	flatBtn(m_btnErase); flatBtn(m_btnSel); flatBtn(m_btnPal); flatBtn(m_btnPropUpd);
	flatBtn(m_btnMark); flatBtn(m_btnLoopA); flatBtn(m_btnLoopB); flatBtn(m_btnLoopClr);
	flatBtn(m_btnShowAll);
	if (m_btnText.GetSafeHwnd()) flatBtn(m_btnText);
	if (m_btnPencil.GetSafeHwnd()) m_btnPencil.SetIcon(IDI_SCORE_PENCIL);
	if (m_btnErase.GetSafeHwnd()) m_btnErase.SetIcon(IDI_SCORE_ERASE);
	if (m_btnPal.GetSafeHwnd()) m_btnPal.SetIcon(IDI_SCORE_NOTE_Q);
	if (m_btnTempo.GetSafeHwnd()) m_btnTempo.SetIcon(IDI_UI_MUSIC);
	if (m_btnPlay.GetSafeHwnd()) m_btnPlay.SetIcon(IDI_CTL_PLAY);
	m_edNote.SetAeroMode(FALSE);
	m_edGt.SetAeroMode(FALSE);
	m_edVel.SetAeroMode(FALSE);
	m_ch.SetAeroMode(FALSE);
	m_status.SetAeroMode(FALSE);
	ApplyLang();
	m_ch.ResetContent();
	for (int i = 0; i < SC_FM_CH; i++) {
		wchar_t s[32];
		if (i < 3)
			_snwprintf_s(s, _TRUNCATE, L"FM%d", i + 1);
		else if (i < 6)
			_snwprintf_s(s, _TRUNCATE, L"SSG%d", i - 2);
		else if (i == 6)
			wcscpy_s(s, L"RHY");
		else
			_snwprintf_s(s, _TRUNCATE, L"FM%d", i - 2);
		m_ch.AddString(s);
	}
	for (int i = 0; i < SC_FM_MISAO; i++) {
		wchar_t s[32];
		_snwprintf_s(s, _TRUNCATE, L"Misao %d", i + 1);
		m_ch.AddString(s);
	}
	m_ch.SetCurSel(0);
	m_edVel.SetWindowText(L"100");
	m_status.SetWindowText(L"FM score: pencil on staff. Dbl-click / Voice = timbre. Tone gauge shows @neiro / Voice#.");
	RefreshToneLabels();
	RefreshStrip();
	UpdateNoteCursor();
	LayoutChrome();
	RestoreUiGeom();
	PostMessage(WM_APP + 61, 0, 0);
	return TRUE;
}


void CSasamiFmScoreDlg::SetupTooltips()
{
	if (!CCustomControlUtility::BeginDialogToolTip(m_tooltip, this, TTS_NOPREFIX)) return;
	auto tip = [&](CWnd& w, LPCWSTR s) { if (w.GetSafeHwnd()) m_tooltip.AddTool(&w, s); };
	tip(m_btnOpen, L"Open FM MML/DAT (.mml .txt .dat .f) into score");
	tip(m_btnSave, L"Save As FPY");
	tip(m_btnPlay, L"Preview FPY (tick gaps must export as rests)");
	tip(m_btnExport, L"Audio export");
	tip(m_btnVoice, L"FM voice editor: params + preview beep, then apply to part");
	tip(m_btnText, L"Text composer");
	tip(m_btnHelp, L"Help");
	tip(m_status, L"Status");
	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip);
}

LRESULT CSasamiFmScoreDlg::OnDeferredInit(WPARAM, LPARAM)
{
	return 0;
}

BOOL CSasamiFmScoreDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}

void CSasamiFmScoreDlg::LayoutChrome()
{
	if (!::IsWindow(m_hWnd) || m_bInLayout) return;
	m_bInLayout = TRUE;
	CRect rc;
	GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	const int pad = 8, btnH = 26, btnW = 52;
	const int sbW = ScStaffScrollTrackW();
	const int sbH = ScStaffScrollTrackH();
	int y = cap + pad;
	int x = pad;
	auto place = [&](CWnd& w, int ww) {
		if (w.GetSafeHwnd()) { w.MoveWindow(x, y, ww, btnH); x += ww + 4; }
	};
	place(m_btnOpen, btnW);
	place(m_btnSave, btnW);
	place(m_btnPlay, 88);
	place(m_btnExport, btnW);
	place(m_btnPencil, btnW);
	place(m_btnErase, btnW);
	place(m_btnSel, btnW);
	place(m_btnPal, 48);
	place(m_btnTempo, btnW);
	place(m_btnVoice, btnW);
	if (m_ch.GetSafeHwnd()) { m_ch.MoveWindow(x, y, 80, 220); x += 88; }
	place(m_btnHelp, 48);
	y += btnH + 6;
	x = pad;
	if (m_edNote.GetSafeHwnd()) m_edNote.MoveWindow(x + 28, y, 44, 22);
	if (m_edGt.GetSafeHwnd()) m_edGt.MoveWindow(x + 104, y, 44, 22);
	if (m_edVel.GetSafeHwnd()) m_edVel.MoveWindow(x + 180, y, 44, 22);
	if (m_btnPropUpd.GetSafeHwnd()) m_btnPropUpd.MoveWindow(x + 232, y, 52, btnH);
	y += btnH + 6;
	x = pad;
	place(m_btnMark, 36);
	place(m_btnLoopA, 28);
	place(m_btnLoopB, 28);
	place(m_btnLoopClr, 40);
	place(m_btnShowAll, 40);
	place(m_btnText, 64);
	y += btnH + 4;
	if (m_status.GetSafeHwnd())
		m_status.MoveWindow(pad, y, max(200, rc.Width() - pad * 2 - sbW), 22);
	y += 26;
	if (y > rc.Height() - pad - sbH - 80)
		y = max(cap + pad, rc.Height() - pad - sbH - 80);
	m_bodyRc.SetRect(pad, y, max(pad + 40, rc.Width() - pad - sbW), max(y + 40, rc.Height() - pad - sbH));
	const int stripH = min(ScStaffStripTotalH(&m_ui), max(8, m_bodyRc.Height() / 3));
	m_trackRc.SetRect(m_bodyRc.left, m_bodyRc.top, m_bodyRc.left + SC_TRACK_COL_W, max(m_bodyRc.top + 8, m_bodyRc.bottom - stripH));
	m_gridRc.SetRect(m_trackRc.right, m_bodyRc.top, m_bodyRc.right, m_trackRc.bottom);
	m_stripRc.SetRect(m_gridRc.left, m_trackRc.bottom, m_bodyRc.right, m_bodyRc.bottom);
	m_ui.followViewW = max(1, m_gridRc.Width());
	/* Scrollbars in dialog STYLE — no ShowScrollBar/ModifyStyle (OnSize re-entry crash). */
	UpdateScrollBars();
	m_bInLayout = FALSE;
	InvalidateRect(m_bodyRc, FALSE);
}

void CSasamiFmScoreDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	if (!m_bInLayout && m_btnOpen.GetSafeHwnd() && cx > 0 && cy > 0)
		LayoutChrome();
}

BOOL CSasamiFmScoreDlg::OnEraseBkgnd(CDC* pDC)
{
	if (!pDC) return TRUE;
	CRect rc;
	GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	CRect body(0, cap, rc.right, min(rc.bottom, m_bodyRc.top));
	if (body.Height() > 0) {
#if CCUSTOM_AERO_SUPPORT
		CCC_FillRectAlpha(pDC->GetSafeHdc(), body, RGB(236, 240, 238), 255);
#else
		pDC->FillSolidRect(&body, RGB(236, 240, 238));
#endif
	}
	return TRUE;
}

void CSasamiFmScoreDlg::UpdateScrollBars()
{
	if (!::IsWindow(m_hWnd)) return;
	const int pxBeat = m_ui.pxBeat > 0 ? m_ui.pxBeat : SC_PX_BEAT_DEFAULT;
	int contentW = (m_ui.contentTicks * pxBeat) / SC_PPQN + SC_CLEF_MARGIN;
	int pageW = max(1, m_gridRc.Width());
	int maxScrollX = max(0, contentW - pageW);
	if (m_ui.scrollX > maxScrollX) m_ui.scrollX = maxScrollX;
	if (m_ui.scrollX < 0) m_ui.scrollX = 0;
	SCROLLINFO si = {};
	si.cbSize = sizeof(si);
	si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
	si.nMin = 0;
	si.nMax = max(0, contentW);
	si.nPage = (UINT)pageW;
	si.nPos = m_ui.scrollX;
	SetScrollInfo(SB_HORZ, &si, TRUE);
	int contentH = ScStaffContentHeight(&m_ui);
	int pageH = max(1, m_gridRc.Height());
	int maxScrollY = max(0, contentH - pageH);
	if (m_ui.scrollY > maxScrollY) m_ui.scrollY = maxScrollY;
	if (m_ui.scrollY < 0) m_ui.scrollY = 0;
	si.nMax = max(0, contentH);
	si.nPage = (UINT)pageH;
	si.nPos = m_ui.scrollY;
	SetScrollInfo(SB_VERT, &si, TRUE);
}

void CSasamiFmScoreDlg::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	SCROLLINFO si = { sizeof(si), SIF_ALL };
	GetScrollInfo(SB_HORZ, &si);
	int pos = si.nPos;
	const int pxBeat = m_ui.pxBeat > 0 ? m_ui.pxBeat : SC_PX_BEAT_DEFAULT;
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
	m_ui.scrollX = pos;
	si.fMask = SIF_POS;
	si.nPos = pos;
	SetScrollInfo(SB_HORZ, &si, TRUE);
	InvalidateRect(m_bodyRc, FALSE);
}

void CSasamiFmScoreDlg::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	SCROLLINFO si = { sizeof(si), SIF_ALL };
	GetScrollInfo(SB_VERT, &si);
	int pos = si.nPos;
	const int rowH = max(8, ScStaffH(&m_ui));
	switch (nSBCode) {
	case SB_TOP: pos = si.nMin; break;
	case SB_BOTTOM: pos = max(0, (int)si.nMax - (int)si.nPage + 1); break;
	case SB_LINEUP: pos -= rowH / 2; break;
	case SB_LINEDOWN: pos += rowH / 2; break;
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
	m_ui.scrollY = pos;
	si.fMask = SIF_POS;
	si.nPos = pos;
	SetScrollInfo(SB_VERT, &si, TRUE);
	InvalidateRect(m_bodyRc, FALSE);
}


void CSasamiFmScoreDlg::RefreshStrip()
{
	ScStaffEnsureStripFromDoc(&m_ui, m_doc.ev, m_doc.evCount, m_curCh);
	ScStaffUpdateContentExtent(&m_ui, m_doc.ev, m_doc.evCount);
	UpdateScrollBars();
}

void CSasamiFmScoreDlg::SyncPropFromSel()
{
	if (m_ui.selEv < 0 || m_ui.selEv >= m_doc.evCount) return;
	const ScEvent& e = m_doc.ev[m_ui.selEv];
	if (e.kind != SC_EV_FM_NOTE) return;
	int midi = (((e.a >> 4) & 0x0F) * 12 + (e.a & 0x0F) + 12);
	CString s;
	s.Format(L"%d", midi);
	m_edNote.SetWindowText(s);
	s.Format(L"%d", (int)e.dur);
	m_edGt.SetWindowText(s);
	s.Format(L"%d", e.b ? (int)e.b : 100);
	m_edVel.SetWindowText(s);
}

void CSasamiFmScoreDlg::OnPaint()
{
	CPaintDC dc(this);
	CRect panel = m_bodyRc;
	if (panel.Width() < 8 || panel.Height() < 8) {
		CCC_CaptionPaint(dc, m_hWnd);
		return;
	}
	CDC mem;
	mem.CreateCompatibleDC(&dc);
	CBitmap bmp;
	if (!bmp.CreateCompatibleBitmap(&dc, panel.Width(), panel.Height())) {
		CCC_CaptionPaint(dc, m_hWnd);
		return;
	}
	CBitmap* old = mem.SelectObject(&bmp);
	if (!old) {
		CCC_CaptionPaint(dc, m_hWnd);
		return;
	}
	mem.FillSolidRect(0, 0, panel.Width(), panel.Height(), RGB(236, 240, 238));
	CRect track = m_trackRc; track.OffsetRect(-panel.left, -panel.top);
	CRect grid = m_gridRc; grid.OffsetRect(-panel.left, -panel.top);
	CRect strip = m_stripRc; strip.OffsetRect(-panel.left, -panel.top);
	if (track.Width() > 2 && track.Height() > 2)
		ScStaffPaintTracks(mem, track, &m_ui, m_curCh);
	if (grid.Width() > 2 && grid.Height() > 2)
		ScStaffPaintStaves(mem, grid, &m_ui, m_doc.ev, m_doc.evCount, 1, m_curCh, m_doc.tempoT);
	if (strip.Width() > 2 && strip.Height() > 2)
		ScStaffPaintStrip(mem, strip, &m_ui);
	mem.SetBkMode(TRANSPARENT);
	mem.SetTextColor(RGB(40, 60, 50));
	CFont* oldF = mem.SelectObject(GetFont());
	mem.SelectObject(oldF);
	CRect bodyRel = m_bodyRc; bodyRel.OffsetRect(-panel.left, -panel.top);
	CRect clientRel(0, 0, panel.Width(), panel.Height());
	ScStaffPaintScrollThumbs(mem, clientRel, bodyRel, &m_ui, max(1, m_gridRc.Width()), max(1, m_gridRc.Height()));
	CCC_BlitStretchOpaque(dc.GetSafeHdc(), panel.left, panel.top, panel.Width(), panel.Height(),
		mem.GetSafeHdc(), 0, 0, panel.Width(), panel.Height());
	mem.SelectObject(old);
	CCC_CaptionPaint(dc, m_hWnd);
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(RGB(40, 55, 45));
	CFont* oldLab = dc.SelectObject(GetFont());
	if (m_edNote.GetSafeHwnd()) {
		CRect er; m_edNote.GetWindowRect(&er); ScreenToClient(&er);
		dc.TextOut(er.left - 20, er.top + 2, L"N");
	}
	if (m_edGt.GetSafeHwnd()) {
		CRect er; m_edGt.GetWindowRect(&er); ScreenToClient(&er);
		dc.TextOut(er.left - 24, er.top + 2, L"GT");
	}
	if (m_edVel.GetSafeHwnd()) {
		CRect er; m_edVel.GetWindowRect(&er); ScreenToClient(&er);
		dc.TextOut(er.left - 28, er.top + 2, L"Vol");
	}
	dc.SelectObject(oldLab);
}

void CSasamiFmScoreDlg::PlaceOrEditAt(CPoint pt)
{
	int ctrlTr = -1;
	int ctrl = ScStaffHitScoreCtrl(m_gridRc, &m_ui, m_doc.ev, m_doc.evCount, 1, pt, &ctrlTr);
	if (ctrl >= 0) {
		m_ui.selEv = ctrl;
		m_curCh = m_doc.ev[ctrl].ch;
		if (m_ch.GetSafeHwnd()) m_ch.SetCurSel(m_curCh);
		InvalidateRect(m_gridRc, FALSE);
		return;
	}
	if (ScStaffPtInScoreCtrlStrip(m_gridRc, &m_ui, pt, &ctrlTr))
		return;
	int hitTr = -1;
	int hit = ScStaffHitNote(m_gridRc, &m_ui, m_doc.ev, m_doc.evCount, 1, pt, &hitTr);
	if (m_ui.tool == SC_TOOL_ERASER) {
		if (hit >= 0) {
			for (int j = hit; j + 1 < m_doc.evCount; j++)
				m_doc.ev[j] = m_doc.ev[j + 1];
			m_doc.evCount--;
			if (m_ui.selEv == hit) m_ui.selEv = -1;
			else if (m_ui.selEv > hit) m_ui.selEv--;
			RefreshStrip();
			InvalidateRect(m_bodyRc, FALSE);
		}
		return;
	}
	if (m_ui.tool == SC_TOOL_SELECT) {
		if (hit >= 0) {
			m_ui.selEv = hit;
			m_ui.dragEv = hit;
			m_ui.dragOriginX = pt.x;
			m_curCh = m_doc.ev[hit].ch;
			m_ch.SetCurSel(m_curCh);
			SyncPropFromSel();
			RefreshStrip();
			InvalidateRect(m_bodyRc, FALSE);
		}
		return;
	}
	if (hit >= 0) {
		m_ui.selEv = hit;
		m_ui.dragEv = hit;
		m_ui.dragOriginX = pt.x;
		SyncPropFromSel();
		InvalidateRect(m_bodyRc, FALSE);
		return;
	}
	if (hit == -2 && hitTr >= 0) {
		m_curCh = hitTr;
		m_ch.SetCurSel(m_curCh);
		m_ui.visible[hitTr] = 1;
		const int quant = m_ui.snapFit ? m_ui.placeDur : (SC_PPQN / 4);
		uint32_t tick = ScStaffXToTick(pt.x, m_ui.scrollX, m_gridRc.left + SC_CLEF_MARGIN, m_ui.pxBeat, quant);
		int staffTop = ScStaffVisibleLaneStaffTop(m_gridRc, &m_ui, hitTr);
		if (staffTop < 0) return;
		int note = ScStaffYToMidiNoteTrack(&m_ui, hitTr, staffTop, pt.y) + m_accidental;
		/* FM channels are monophonic at a tick: replace any note/rest already there */
		for (int i = m_doc.evCount - 1; i >= 0; --i) {
			const ScEvent& e = m_doc.ev[i];
			if ((int)e.ch != hitTr) continue;
			if (e.tick != tick) continue;
			if (e.kind != SC_EV_FM_NOTE && e.kind != SC_EV_FM_REST) continue;
			for (int j = i; j + 1 < m_doc.evCount; j++)
				m_doc.ev[j] = m_doc.ev[j + 1];
			m_doc.evCount--;
			if (m_ui.selEv == i) m_ui.selEv = -1;
			else if (m_ui.selEv > i) m_ui.selEv--;
		}
		if (m_placeRest) {
			ScFmAddRest(&m_doc, tick, hitTr, m_ui.placeDur);
		} else {
			uint8_t nb;
			if (hitTr == 6) {
				int pad = (note - 36) / 2;
				if (pad < 0) pad = 0;
				if (pad > 5) pad = 5;
				nb = (uint8_t)pad;
			} else if (ScStaffIsFmSsgTrack(1, hitTr)) {
				const int oct = note / 12;
				const int sc = note % 12;
				nb = (uint8_t)(((oct & 0x0F) << 4) | (sc & 0x0F));
			} else {
				nb = MidiToFmNoteByte(note);
			}
			ScFmAddNote(&m_doc, tick, hitTr, nb, m_ui.placeDur);
			m_ui.selEv = m_doc.evCount - 1;
			m_ui.dragEv = m_ui.selEv;
			m_ui.dragOriginX = pt.x;
			SyncPropFromSel();
		}
		RefreshStrip();
		InvalidateRect(m_bodyRc, FALSE);
	}
}

void CSasamiFmScoreDlg::UpdateNoteCursor()
{
	if (m_noteCur) {
		::DestroyCursor(m_noteCur);
		m_noteCur = NULL;
	}
	if (m_ui.tool == SC_TOOL_PENCIL)
		m_noteCur = ScStaffCreateBlankCursor();
	else
		m_noteCur = NULL;
}

void CSasamiFmScoreDlg::UpdateHover(CPoint pt)
{
	m_ui.hoverValid = 0;
	if (!m_gridRc.PtInRect(pt) || m_ui.tool != SC_TOOL_PENCIL) return;
	if (pt.y < m_gridRc.top + SC_RULER_H) return;
	if (ScStaffPtInScoreCtrlStrip(m_gridRc, &m_ui, pt, NULL)) return;
	int hitTr = -1;
	int hit = ScStaffHitNote(m_gridRc, &m_ui, m_doc.ev, m_doc.evCount, 1, pt, &hitTr);
	if (hit != -2 || hitTr < 0) return;
	int staffTop = ScStaffVisibleLaneStaffTop(m_gridRc, &m_ui, hitTr);
	if (staffTop < 0) return;
	const int quant = m_ui.snapFit ? m_ui.placeDur : (SC_PPQN / 4);
	m_ui.hoverTick = ScStaffXToTick(pt.x, m_ui.scrollX, m_gridRc.left + SC_CLEF_MARGIN, m_ui.pxBeat, quant);
	m_ui.hoverNote = ScStaffYToMidiNoteTrack(&m_ui, hitTr, staffTop, pt.y) + m_accidental;
	m_ui.hoverTrack = hitTr;
	m_ui.placeRest = m_placeRest;
	m_ui.hoverValid = 1;
}

void CSasamiFmScoreDlg::UpdateHoverStatus(CPoint pt)
{
	wchar_t st[256];
	if (!ScStaffFormatPointerStatus(&m_ui, m_doc.ev, m_doc.evCount, 1, m_gridRc, pt, st, 256))
		return;
	if (wcscmp(st, m_lastHoverSt) == 0) return;
	wcsncpy_s(m_lastHoverSt, st, _TRUNCATE);
	if (m_status.GetSafeHwnd())
		m_status.SetWindowText(st);
}

void CSasamiFmScoreDlg::RefreshToneLabels()
{
	for (int i = 0; i < m_ui.trackCount && i < 32; i++) {
		m_ui.vstLabel[i][0] = 0;
		m_ui.progLabel[i][0] = 0;
	}
	int voiceForTrack[32];
	for (int i = 0; i < 32; ++i) voiceForTrack[i] = (m_doc.voiceCount > 0) ? 0 : -1;
	for (int i = 0; i < m_doc.evCount; i++) {
		const ScEvent& e = m_doc.ev[i];
		if (e.kind != SC_EV_FM_VOICE) continue;
		int ch = e.ch;
		if (ch < 0 || ch >= 32) continue;
		if (e.b == 1 && e.a < m_doc.voiceCount) voiceForTrack[ch] = e.a;
	}
	for (int ch = 0; ch < m_ui.trackCount && ch < 32; ++ch) {
		const int vi = voiceForTrack[ch];
		if (vi >= 0 && vi < m_doc.voiceCount) {
			_snwprintf_s(m_ui.vstLabel[ch], _TRUNCATE, L"Voice#%d", vi);
			const int af = m_doc.voices[vi][24];
			_snwprintf_s(m_ui.progLabel[ch], _TRUNCATE, L"ALG %d  FB %d", af & 7, (af >> 3) & 7);
		} else {
			wcscpy_s(m_ui.vstLabel[ch], L"(no voice)");
			wcscpy_s(m_ui.progLabel[ch], L"ALG --  FB --");
		}
	}
}

void CSasamiFmScoreDlg::OpenVoiceEditor()
{
	uint8_t v[25];
	int vi = (m_doc.voiceCount > 0) ? 0 : -1;
	for (int i = 0; i < m_doc.evCount; ++i) {
		const ScEvent& e = m_doc.ev[i];
		if (e.kind == SC_EV_FM_VOICE && e.ch == m_curCh && e.b == 1 && e.a < m_doc.voiceCount)
			vi = e.a;
	}
	if (vi >= 0)
		memcpy(v, m_doc.voices[vi], 25);
	else {
		memset(v, 0, 25);
		v[24] = 0x3C;
	}
	CSasamiFmVoiceDlg dlg(this);
	if (dlg.DoEdit(this, v) == IDOK) {
		if (vi >= 0) {
			memcpy(m_doc.voices[vi], v, 25);
		} else {
			vi = ScFmAllocVoice(&m_doc, v);
			if (vi < 0) vi = 0;
		}
		/* Bind custom voice + audible TL on current part */
		ScFmAddVoiceSelect(&m_doc, 0, m_curCh, vi, 1);
		ScFmAddVolTl(&m_doc, 0, m_curCh, 16);
		RefreshToneLabels();
		InvalidateRect(m_trackRc, FALSE);
		m_status.SetWindowText(L"Voice updated (custom bank + FNEIRO/FVOL on current part)");
	}
}

void CSasamiFmScoreDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	if (capH > 0 && point.y >= 0 && point.y < capH) {
		CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
		return;
	}
	int gaugeZone = SC_GAUGE_NONE;
	const int gaugeTrack = ScStaffHitGauge(m_trackRc, &m_ui, point, &gaugeZone);
	if (gaugeTrack >= 0 && gaugeZone != SC_GAUGE_NONE) {
		m_curCh = gaugeTrack;
		m_ch.SetCurSel(m_curCh);
		OpenVoiceEditor();
		return;
	}
	CRect client; GetClientRect(&client);
	CPoint bp(point.x - m_bodyRc.left, point.y - m_bodyRc.top);
	CRect bodyRel(0, 0, m_bodyRc.Width(), m_bodyRc.Height());
	int sbPos = 0;
	int sbHit = ScStaffHitScroll(bodyRel, bodyRel, &m_ui,
		max(1, m_gridRc.Width()), max(1, m_gridRc.Height()), bp, &sbPos);
	if (sbHit == 1) {
		m_sbDrag = 1;
		m_ui.scrollY = sbPos;
		UpdateScrollBars();
		InvalidateRect(m_bodyRc, FALSE);
		SetCapture();
		return;
	}
	if (sbHit == 2) {
		m_sbDrag = 2;
		m_ui.scrollX = sbPos;
		UpdateScrollBars();
		InvalidateRect(m_bodyRc, FALSE);
		SetCapture();
		return;
	}
	SetCapture();
	uint32_t rulerTick = 0;
	if (ScStaffHitRulerTick(m_gridRc, &m_ui, point, &rulerTick)) {
		if (m_ui.transportMode == 2) {
			m_ui.loopATick = (int)rulerTick;
			if (m_ui.loopBTick >= 0 && m_ui.loopBTick <= m_ui.loopATick)
				m_ui.loopBTick = -1;
		} else if (m_ui.transportMode == 3) {
			m_ui.loopBTick = (int)rulerTick;
			if (m_ui.loopATick < 0) m_ui.loopATick = 0;
			if (m_ui.loopBTick <= m_ui.loopATick)
				m_ui.loopBTick = m_ui.loopATick + SC_PPQN * SC_MEASURE_BEATS;
		} else {
			m_ui.markerTick = rulerTick;
		}
		m_ui.transportMode = 0;
		InvalidateRect(m_bodyRc, FALSE);
		return;
	}
	int tr = ScStaffHitTrack(m_trackRc, &m_ui, point);
	if (tr >= 0) {
		int mid = m_trackRc.left + 28;
		if (point.x < mid)
			m_ui.visible[tr] = m_ui.visible[tr] ? 0 : 1;
		else {
			m_curCh = tr;
			m_ch.SetCurSel(m_curCh);
			m_ui.visible[tr] = 1;
			RefreshStrip();
		}
		InvalidateRect(m_bodyRc, FALSE);
		return;
	}
	int lane = 0, scol = 0, sval = 0;
	if (ScStaffHitStrip(m_stripRc, &m_ui, point, &lane, &scol, &sval)) {
		m_ui.strip[lane][scol] = (uint8_t)sval;
		m_ui.dragEv = -2000 - lane;
		InvalidateRect(m_stripRc, FALSE);
		return;
	}
	PlaceOrEditAt(point);
}

void CSasamiFmScoreDlg::OnMouseMove(UINT nFlags, CPoint point)
{
	if ((nFlags & MK_LBUTTON) && m_sbDrag) {
		CPoint bp(point.x - m_bodyRc.left, point.y - m_bodyRc.top);
		CRect bodyRel(0, 0, m_bodyRc.Width(), m_bodyRc.Height());
		int sbPos = 0;
		if (m_sbDrag == 1) {
			CPoint p2(bodyRel.right - 2, bp.y);
			ScStaffHitScroll(bodyRel, bodyRel, &m_ui,
				max(1, m_gridRc.Width()), max(1, m_gridRc.Height()), p2, &sbPos);
			m_ui.scrollY = sbPos;
		} else {
			CPoint p2(bp.x, bodyRel.bottom - 2);
			ScStaffHitScroll(bodyRel, bodyRel, &m_ui,
				max(1, m_gridRc.Width()), max(1, m_gridRc.Height()), p2, &sbPos);
			m_ui.scrollX = sbPos;
		}
		UpdateScrollBars();
		InvalidateRect(m_bodyRc, FALSE);
		return;
	}
	int gaugeZone = SC_GAUGE_NONE;
	const int onGauge = (ScStaffHitGauge(m_trackRc, &m_ui, point, &gaugeZone) >= 0 &&
		gaugeZone != SC_GAUGE_NONE);
	const int onCtrlStrip = ScStaffPtInScoreCtrlStrip(m_gridRc, &m_ui, point, NULL);
	if (onGauge || onCtrlStrip) {
		::SetCursor(::LoadCursor(NULL, IDC_HAND));
		m_ui.hoverValid = 0;
	} else if (m_gridRc.PtInRect(point) && m_noteCur)
		::SetCursor(m_noteCur);
	UpdateHover(point);
	UpdateHoverStatus(point);
	if (!(nFlags & MK_LBUTTON)) {
		if (m_ui.hoverValid)
			InvalidateRect(m_gridRc, FALSE);
		CCustomBlurDialogExBase::OnMouseMove(nFlags, point);
		return;
	}
	if (m_ui.dragEv <= -2000) {
		int lane = -1, scol = 0, sval = 0;
		if (!ScStaffHitStrip(m_stripRc, &m_ui, point, &lane, &scol, &sval)) {
			lane = -(m_ui.dragEv + 2000);
			if (lane < 0) lane = 0;
			if (lane >= SC_STRIP_LANES_MAX) lane = 0;
			const int left = m_stripRc.left + SC_CLEF_MARGIN;
			const int colW = max(2, m_ui.pxBeat / 2);
			scol = (point.x - left + m_ui.scrollX) / colW;
			if (scol < 0) scol = 0;
			if (scol > 255) scol = 255;
			sval = 64;
		}
		if (lane < 0) lane = 0;
		m_ui.strip[lane][scol] = (uint8_t)sval;
		InvalidateRect(m_stripRc, FALSE);
	} else if (m_ui.dragEv >= 0 && m_ui.dragEv < m_doc.evCount) {
		ScEvent& e = m_doc.ev[m_ui.dragEv];
		if (e.kind == SC_EV_FM_NOTE) {
			int dx = point.x - m_ui.dragOriginX;
			int dTicks = (dx * SC_PPQN) / max(1, m_ui.pxBeat);
			int nd = (int)e.dur + dTicks;
			if (nd < SC_PPQN / 8) nd = SC_PPQN / 8;
			if (nd > 32000) nd = 32000;
			e.dur = (uint16_t)nd;
			m_ui.dragOriginX = point.x;
			SyncPropFromSel();
			InvalidateRect(m_gridRc, FALSE);
		}
	}
	CCustomBlurDialogExBase::OnMouseMove(nFlags, point);
}

BOOL CSasamiFmScoreDlg::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	if (nHitTest == HTCLIENT) {
		CPoint pt; ::GetCursorPos(&pt); ScreenToClient(&pt);
		int zone = SC_GAUGE_NONE;
		if (ScStaffHitGauge(m_trackRc, &m_ui, pt, &zone) >= 0 && zone != SC_GAUGE_NONE) {
			::SetCursor(::LoadCursor(NULL, IDC_HAND));
			return TRUE;
		}
		if (ScStaffPtInScoreCtrlStrip(m_gridRc, &m_ui, pt, NULL)) {
			::SetCursor(::LoadCursor(NULL, IDC_HAND));
			return TRUE;
		}
	}
	return CCustomBlurDialogExBase::OnSetCursor(pWnd, nHitTest, message);
}

void CSasamiFmScoreDlg::OnLButtonUp(UINT nFlags, CPoint point)
{
	m_sbDrag = 0;
	if (m_ui.dragEv <= -2000) {
		ScStaffApplyStripToDocFm(&m_doc, m_curCh, &m_ui);
		m_ui.dragEv = -1;
	} else {
		m_ui.dragEv = -1;
	}
	ReleaseCapture();
	CCustomBlurDialogExBase::OnLButtonUp(nFlags, point);
}

void CSasamiFmScoreDlg::OpenNotePropsForSel()
{
	if (m_ui.selEv < 0 || m_ui.selEv >= m_doc.evCount) return;
	ScEvent& e = m_doc.ev[m_ui.selEv];
	if (e.kind != SC_EV_FM_NOTE) return;
	SyncPropFromSel();
	CSasamiNotePropsDlg::OpenForEvent(this, &e, 1, (int)e.ch + 1);
}

void CSasamiFmScoreDlg::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	if (capH > 0 && point.y >= 0 && point.y < capH) {
		CCustomBlurDialogExBase::OnLButtonDblClk(nFlags, point);
		return;
	}
	int ctrlTr = -1;
	int ctrl = ScStaffHitScoreCtrl(m_gridRc, &m_ui, m_doc.ev, m_doc.evCount, 1, point, &ctrlTr);
	if (ctrl >= 0) {
		m_ui.selEv = ctrl;
		m_curCh = m_doc.ev[ctrl].ch;
		if (m_ch.GetSafeHwnd()) m_ch.SetCurSel(m_curCh);
		OpenVoiceEditor();
		return;
	}
	int gaugeZone = SC_GAUGE_NONE;
	const int gaugeTrack = ScStaffHitGauge(m_trackRc, &m_ui, point, &gaugeZone);
	if (gaugeTrack >= 0 && gaugeZone != SC_GAUGE_NONE) {
		m_curCh = gaugeTrack;
		m_ch.SetCurSel(m_curCh);
		OpenVoiceEditor();
		return;
	}
	int hitTr = -1;
	int hit = ScStaffHitNote(m_gridRc, &m_ui, m_doc.ev, m_doc.evCount, 1, point, &hitTr);
	if (hit >= 0) {
		m_ui.selEv = hit;
		m_curCh = m_doc.ev[hit].ch;
		m_ch.SetCurSel(m_curCh);
		OpenNotePropsForSel();
	}
	CCustomBlurDialogExBase::OnLButtonDblClk(nFlags, point);
}

BOOL CSasamiFmScoreDlg::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	if (nFlags & MK_CONTROL) {
		int nb = m_ui.pxBeat + (zDelta > 0 ? 4 : -4);
		if (nb < SC_PX_BEAT_MIN) nb = SC_PX_BEAT_MIN;
		if (nb > SC_PX_BEAT_MAX) nb = SC_PX_BEAT_MAX;
		m_ui.pxBeat = nb;
		int ns = m_ui.staffScale + (zDelta > 0 ? 5 : -5);
		if (ns < SC_STAFF_SCALE_MIN) ns = SC_STAFF_SCALE_MIN;
		if (ns > SC_STAFF_SCALE_MAX) ns = SC_STAFF_SCALE_MAX;
		m_ui.staffScale = ns;
		UpdateScrollBars();
		PersistUiGeom();
	} else if (nFlags & MK_SHIFT) {
		m_ui.scrollX -= (zDelta / WHEEL_DELTA) * m_ui.pxBeat * 2;
		if (m_ui.scrollX < 0) m_ui.scrollX = 0;
		UpdateScrollBars();
	} else {
		m_ui.scrollY -= (zDelta / WHEEL_DELTA) * max(12, ScStaffH(&m_ui) / 2);
		if (m_ui.scrollY < 0) m_ui.scrollY = 0;
		UpdateScrollBars();
	}
	InvalidateRect(m_bodyRc, FALSE);
	return TRUE;
}

void CSasamiFmScoreDlg::OnCbnSelchangeCh()
{
	int s = m_ch.GetCurSel();
	if (s >= 0 && s < SC_FM_TOTAL) {
		m_curCh = s;
		m_ui.visible[s] = 1;
		RefreshStrip();
		InvalidateRect(m_bodyRc, FALSE);
	}
}

void CSasamiFmScoreDlg::OnBnClickedPencil() { m_ui.tool = SC_TOOL_PENCIL; UpdateNoteCursor(); }
void CSasamiFmScoreDlg::OnBnClickedErase() { m_ui.tool = SC_TOOL_ERASER; UpdateNoteCursor(); }
void CSasamiFmScoreDlg::OnBnClickedSel() { m_ui.tool = SC_TOOL_SELECT; UpdateNoteCursor(); }

void CSasamiFmScoreDlg::OnBnClickedPal()
{
	CRect r;
	m_btnPal.GetWindowRect(&r);
	CSasamiNotePaletteDlg::OpenNear(this, CPoint(r.left, r.bottom + 4));
}

LRESULT CSasamiFmScoreDlg::OnPalDur(WPARAM w, LPARAM l)
{
	if (l & 0x40000000) {
		m_ui.snapFit ^= 1;
		InvalidateRect(m_gridRc, FALSE);
		return 0;
	}
	m_ui.placeDur = (int)w;
	if (m_ui.placeDur < 1) m_ui.placeDur = 1;
	m_placeRest = (int)(l & 1);
	m_ui.placeRest = m_placeRest;
	m_ui.dotted = (l & 2) ? 1 : 0;
	m_ui.triplet = (l & 4) ? 1 : 0;
	m_accidental = (int)(signed char)((l >> 8) & 0xFF);
	int base = (int)((l >> 16) & 0xFFFF);
	if (base > 0) m_ui.baseDur = base;
	UpdateNoteCursor();
	InvalidateRect(m_gridRc, FALSE);
	return 0;
}

void CSasamiFmScoreDlg::OnBnClickedPropUpd()
{
	if (m_ui.selEv < 0 || m_ui.selEv >= m_doc.evCount) return;
	ScEvent& e = m_doc.ev[m_ui.selEv];
	if (e.kind != SC_EV_FM_NOTE) return;
	CString sn, sg, sv;
	m_edNote.GetWindowText(sn);
	m_edGt.GetWindowText(sg);
	m_edVel.GetWindowText(sv);
	int midi = _wtoi(sn);
	int gt = _wtoi(sg);
	int vel = _wtoi(sv);
	e.a = MidiToFmNoteByte(midi);
	if (gt < 1) gt = 1;
	if (gt > 32000) gt = 32000;
	e.dur = (uint16_t)gt;
	e.b = (uint8_t)(vel < 1 ? 1 : (vel > 127 ? 127 : vel));
	InvalidateRect(m_gridRc, FALSE);
}

void CSasamiFmScoreDlg::OnBnClickedTempo()
{
	int bpm = (int)((13000.0 * 120.0) / (double)max(1, m_doc.tempoT) + 0.5);
	static const int kBpm[] = { 80, 100, 120, 140, 160 };
	int ni = 0;
	for (int i = 0; i < 5; i++) if (kBpm[i] == bpm) { ni = (i + 1) % 5; break; }
	bpm = kBpm[ni];
	m_doc.tempoT = (int)((13000.0 * 120.0) / (double)bpm + 0.5);
	CString s;
	s.Format(L"BPM %d", bpm);
	m_status.SetWindowText(s);
}

void CSasamiFmScoreDlg::OnBnClickedVoice() { OpenVoiceEditor(); }

int CSasamiFmScoreDlg::BuildToTemp(wchar_t* outPath, int outCch)
{
	int anySolo = 0;
	for (int i = 0; i < m_ui.trackCount; i++) if (m_ui.solo[i]) { anySolo = 1; break; }
	ScFmDoc* tmp = (ScFmDoc*)HeapAlloc(GetProcessHeap(), 0, sizeof(ScFmDoc));
	if (!tmp) return 0;
	*tmp = m_doc;
	int w = 0;
	for (int i = 0; i < tmp->evCount; i++) {
		int ch = tmp->ev[i].ch;
		if (ch < 0 || ch >= m_ui.trackCount) continue;
		if (m_ui.mute[ch]) continue;
		if (anySolo && !m_ui.solo[ch]) continue;
		tmp->ev[w++] = tmp->ev[i];
	}
	tmp->evCount = w;
	SasamiWriteFm* wr = (SasamiWriteFm*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SasamiWriteFm));
	if (!wr) { HeapFree(GetProcessHeap(), 0, tmp); return 0; }
	if (!ScFmDocToWrite(tmp, wr)) {
		HeapFree(GetProcessHeap(), 0, wr);
		HeapFree(GetProcessHeap(), 0, tmp);
		m_status.SetWindowText(L"FPY build failed");
		return 0;
	}
	HeapFree(GetProcessHeap(), 0, tmp);
	uint8_t* bin = (uint8_t*)HeapAlloc(GetProcessHeap(), 0, SASAMI_WRITE_MAX);
	if (!bin) { SasamiWriteFmClear(wr); HeapFree(GetProcessHeap(), 0, wr); return 0; }
	uint32_t sz = SasamiBuildFpy(wr, bin, SASAMI_WRITE_MAX);
	SasamiWriteFmClear(wr); HeapFree(GetProcessHeap(), 0, wr);
	if (!sz) {
		HeapFree(GetProcessHeap(), 0, bin);
		return 0;
	}
	wchar_t dir[MAX_PATH];
	GetTempPathW(MAX_PATH, dir);
	_snwprintf_s(outPath, outCch, _TRUNCATE, L"%sogg_sasami_fm.fpy", dir);
	if (!SasamiWriteFileW(outPath, bin, sz)) {
		HeapFree(GetProcessHeap(), 0, bin);
		return 0;
	}
	wcsncpy_s(m_lastOut, outPath, _TRUNCATE);
	HeapFree(GetProcessHeap(), 0, bin);
	CString ok;
	ok.Format(L"OK %u bytes -> %s", sz, outPath);
	m_status.SetWindowText(ok);
	return 1;
}

void CSasamiFmScoreDlg::OnBnClickedSave()
{
	wchar_t path[MAX_PATH];
	if (!BuildToTemp(path, MAX_PATH)) return;
	CFileDialog dlg(FALSE, L"fpy", L"song.fpy", OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT, L"FPY (*.fpy)|*.fpy||", this);
	if (dlg.DoModal() != IDOK) return;
	CopyFileW(path, dlg.GetPathName(), FALSE);
	m_status.SetWindowText(dlg.GetPathName());
}

void CSasamiFmScoreDlg::LoadFromDoc(const ScFmDoc& src)
{
	m_doc = src;
	m_ui.selEv = -1;
	m_ui.markerTick = 0;
	m_ui.playheadTick = 0;
	RefreshToneLabels();
	RefreshStrip();
	ScStaffEnsureStripFromDoc(&m_ui, m_doc.ev, m_doc.evCount, m_curCh);
	ScStaffUpdateContentExtent(&m_ui, m_doc.ev, m_doc.evCount);
	UpdateScrollBars();
	if (m_bodyRc.Width() > 0)
		InvalidateRect(m_bodyRc, FALSE);
}

void CSasamiFmScoreDlg::OnBnClickedOpen()
{
	CFileDialog dlg(TRUE, L"mml", NULL, OFN_FILEMUSTEXIST,
		L"FM MML/DAT (*.mml;*.txt;*.dat;*.f)|*.mml;*.txt;*.dat;*.f|All|*.*||", this);
	if (dlg.DoModal() != IDOK) return;
	const CString path = dlg.GetPathName();
	if (path.Right(4).CompareNoCase(L".fpy") == 0) {
		m_status.SetWindowText(LL14(
			L".fpy はバイナリです — MML/.dat を開くか譜面で編集してください",
			L".fpy is binary — open MML/.dat source or edit on staff",
			L".fpy binaire — ouvrez MML/.dat", L".fpy binario — apri MML/.dat", L".fpy binario — abra MML/.dat",
			L".fpy binary", L".fpy 为二进制", L".fpy binary", L".fpy binary", L".fpy ist Binaer",
			L".fpy binario", L".fpy binair", L".fpy binarny", L".fpy ikili"));
		return;
	}
	const int bufCch = (int)(SC_TEXT_MAX / sizeof(wchar_t));
	wchar_t* buf = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		(SIZE_T)bufCch * sizeof(wchar_t));
	if (!buf) return;
	int ok = 0;
	if (path.Right(2).CompareNoCase(L".f") == 0)
		ok = ScExtractOldFToText(path, buf, bufCch);
	else
		ok = ScLoadTextFileW(path, buf, bufCch);
	if (!ok) {
		HeapFree(GetProcessHeap(), 0, buf);
		m_status.SetWindowText(LL14(
			L"読込失敗", L"Load failed", L"Echec lecture", L"Caricamento fallito", L"Carga fallida",
			L"Load failed", L"读取失败", L"Load failed", L"Load failed", L"Laden fehlgeschlagen",
			L"Falha", L"Laden mislukt", L"Wczytywanie nieudane", L"Yukleme basarisiz"));
		return;
	}
	ScFmDoc* tmp = (ScFmDoc*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ScFmDoc));
	if (!tmp) {
		HeapFree(GetProcessHeap(), 0, buf);
		return;
	}
	wchar_t err[256];
	int errLine = 0;
	if (!ScCompileFmText(buf, tmp, &errLine, err, 256)) {
		CString st;
		st.Format(L"Compile line %d: %s", errLine, err);
		m_status.SetWindowText(st);
		ScFmDocClear(tmp);
		HeapFree(GetProcessHeap(), 0, tmp);
		HeapFree(GetProcessHeap(), 0, buf);
		return;
	}
	if (tmp->voiceCount < 1) {
		static const uint8_t kDefVoice[25] = {
			0x3B, 0x00, 0x00, 0x20, 0x28, 0x20, 0x1A, 0x0D, 0x9F, 0x9E, 0xDE, 0x9E,
			0x05, 0x05, 0x05, 0x05, 0x0F, 0x0B, 0x0C, 0x0B, 0x8A, 0xF6, 0x86, 0xF7, 0x1B
		};
		ScFmAllocVoice(tmp, kDefVoice);
	}
	LoadFromDoc(*tmp);
	ScFmDocClear(tmp);
	HeapFree(GetProcessHeap(), 0, tmp);
	if (CSasamiTextDlg* t = CSasamiTextDlg::Instance()) {
		if (::IsWindow(t->GetSafeHwnd()))
			t->SetFmTextFromScore(buf);
	}
	HeapFree(GetProcessHeap(), 0, buf);
	CString st;
	st.Format(L"Opened %s (%d events)", (LPCTSTR)path, m_doc.evCount);
	m_status.SetWindowText(st);
}

void CSasamiFmScoreDlg::OnBnClickedPlay()
{
	wchar_t path[MAX_PATH];
	if (!BuildToTemp(path, MAX_PATH)) return;
	if (!pl) {
		m_status.SetWindowText(L"Playlist not ready");
		return;
	}
	m_ui.previewActive = 1;
	m_ui.playheadTick = m_ui.markerTick;
	m_ui.markerSeekArmed = (m_ui.markerTick > 0) ? 1 : 0;
	if (!ScStaffStartHostPreview(path, &m_ui, m_doc.tempoT)) {
		m_ui.previewActive = 0;
		m_status.SetWindowText(L"Failed to start preview");
		return;
	}
	SetTimer(1, 33, NULL);
	m_status.SetWindowText(L"FM preview playing");
	InvalidateRect(m_bodyRc, FALSE);
}


void CSasamiFmScoreDlg::OnBnClickedMark() { m_ui.transportMode = 1; }
void CSasamiFmScoreDlg::OnBnClickedLoopA() { m_ui.transportMode = 2; }
void CSasamiFmScoreDlg::OnBnClickedLoopB() { m_ui.transportMode = 3; }
void CSasamiFmScoreDlg::OnBnClickedLoopClr()
{
	m_ui.loopATick = -1;
	m_ui.loopBTick = -1;
	InvalidateRect(m_bodyRc, FALSE);
}
void CSasamiFmScoreDlg::OnBnClickedShowAll()
{
	for (int i = 0; i < m_ui.trackCount; i++) m_ui.visible[i] = 1;
	InvalidateRect(m_bodyRc, FALSE);
}

void CSasamiFmScoreDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == kScFmPreviewTimer) {
		if (ScStaffSyncPreviewPlayhead(&m_ui, m_doc.tempoT)) {
			UpdateScrollBars();
			InvalidateRect(m_bodyRc, FALSE);
		}
		return;
	}
	CCustomBlurDialogExBase::OnTimer(nIDEvent);
}

LRESULT CSasamiFmScoreDlg::OnNoteProps(WPARAM w, LPARAM)
{
	if (w == 1) {
		SyncPropFromSel();
		InvalidateRect(m_bodyRc, FALSE);
	} else if (w == 3) {
		OpenVoiceEditor();
	}
	return 0;
}

void CSasamiFmScoreDlg::OnBnClickedHelp() { OfflineHelpOpenTopic(m_hWnd, L"sasami-composer"); }

void CSasamiFmScoreDlg::OnBnClickedExport()
{
	wchar_t path[MAX_PATH];
	if (!BuildToTemp(path, MAX_PATH)) return;
	ScOpenAudioExport(this, path);
}

void CSasamiFmScoreDlg::OnContextMenu(CWnd* pWnd, CPoint point)
{
	CPoint client = point;
	ScreenToClient(&client);
	/* 左トラック列 → 譜面五線の順で、右クリック位置のパートを取る */
	int tr = ScStaffHitTrack(m_trackRc, &m_ui, client);
	if (tr < 0) {
		int hitTr = -1;
		ScStaffHitNote(m_gridRc, &m_ui, m_doc.ev, m_doc.evCount, 1, client, &hitTr);
		if (hitTr >= 0)
			tr = hitTr;
		else if (ScStaffPtInScoreCtrlStrip(m_gridRc, &m_ui, client, &hitTr) && hitTr >= 0)
			tr = hitTr;
	}
	int markEv = -1;
	{
		int mtr = -1;
		const int ctrl = ScStaffHitScoreCtrl(m_gridRc, &m_ui, m_doc.ev, m_doc.evCount, 1, client, &mtr);
		if (ctrl >= 0 && ctrl < m_doc.evCount) {
			const uint8_t k = m_doc.ev[ctrl].kind;
			if (k == SC_EV_JUMP_MARK || k == SC_EV_FM_JUMP || k == SC_EV_FM_LOOP_START || k == SC_EV_FM_LOOP_END)
				markEv = ctrl;
			if (tr < 0 && mtr >= 0) tr = mtr;
		}
	}
	const int pxBeat = m_ui.pxBeat > 0 ? m_ui.pxBeat : SC_PX_BEAT_DEFAULT;
	const int quant = (m_ui.snapFit && m_ui.placeDur > 0) ? m_ui.placeDur : (SC_PPQN / 4);
	uint32_t atTick = m_ui.markerTick;
	if (m_gridRc.PtInRect(client) && client.x >= m_gridRc.left + SC_CLEF_MARGIN)
		atTick = ScStaffXToTick(client.x, m_ui.scrollX, m_gridRc.left + SC_CLEF_MARGIN, pxBeat, quant);
	else if (markEv >= 0)
		atTick = m_doc.ev[markEv].tick;

	CCustomPopupMenu menu;
	menu.SetAeroMode(FALSE);
	if (tr >= 0) {
		menu.AddCommand(9010, LL14(
			L"音色設定…", L"Tone settings…", L"Réglage du timbre…", L"Impostazioni timbro…", L"Ajustes de timbre…",
			L"음색 설정…", L"音色设置…", L"إعدادات الطابع…", L"Настройки тембра…", L"Klang-Einstellungen…",
			L"Ajustes de timbre…", L"Klankinstellingen…", L"Ustawienia barwy…", L"Tını ayarları…"));
		menu.AddSeparator();
		menu.AddCommand(9020, LL14(
			L"ループ開始 (|:2)", L"Loop start (|:2)", L"Début de boucle (|:2)", L"Inizio loop (|:2)", L"Inicio de bucle (|:2)",
			L"루프 시작 (|:2)", L"循环开始 (|:2)", L"بداية الحلقة (|:2)", L"Начало цикла (|:2)", L"Schleifenstart (|:2)",
			L"Início do loop (|:2)", L"Lusbegin (|:2)", L"Początek pętli (|:2)", L"Döngü başlangıcı (|:2)"));
		menu.AddCommand(9021, LL14(
			L"ループ終了 (:|)", L"Loop end (:|)", L"Fin de boucle (:|)", L"Fine loop (:|)", L"Fin de bucle (:|)",
			L"루프 끝 (:|)", L"循环结束 (:|)", L"نهاية الحلقة (:|)", L"Конец цикла (:|)", L"Schleifenende (:|)",
			L"Fim do loop (:|)", L"Luseinde (:|)", L"Koniec pętli (:|)", L"Döngü sonu (:|)"));
		menu.AddCommand(9022, LL14(
			L"Qマーク（ジャンプ着地）", L"Q mark (jump land)", L"Marque Q (atterrissage)", L"Segno Q (atterraggio)", L"Marca Q (aterrizaje)",
			L"Q 마크 (점프 착지)", L"Q标记（跳转着陆）", L"علامة Q", L"Метка Q", L"Q-Marke (Sprungziel)",
			L"Marca Q (pouso)", L"Q-markering", L"Znacznik Q", L"Q işareti"));
		menu.AddCommand(9023, LL14(
			L"Jジャンプ", L"J jump", L"Saut J", L"Salto J", L"Salto J",
			L"J 점프", L"J跳转", L"قفزة J", L"Прыжок J", L"J-Sprung",
			L"Salto J", L"J-sprong", L"Skok J", L"J atlayışı"));
		if (markEv >= 0) {
			menu.AddCommand(9024, LL14(
				L"このマークを削除", L"Delete this mark", L"Supprimer cette marque", L"Elimina questo segno", L"Eliminar esta marca",
				L"이 마크 삭제", L"删除此标记", L"حذف هذه العلامة", L"Удалить метку", L"Marke löschen",
				L"Excluir esta marca", L"Markering verwijderen", L"Usuń znacznik", L"Bu işareti sil"));
		}
		menu.AddSeparator();
		menu.AddCommand(9025, LL14(
			L"再生ループAをここへ", L"Set preview loop A here", L"Boucle A d'aperçu ici", L"Loop A anteprima qui", L"Bucle A de vista aquí",
			L"미리듣기 루프 A 여기로", L"预览循环A到此处", L"حلقة A هنا", L"Превью A сюда", L"Vorschau-Schleife A hier",
			L"Loop A de prévia aqui", L"Voorbeeld-lus A hier", L"Podgląd pętli A tutaj", L"Önizleme döngü A buraya"));
		menu.AddCommand(9026, LL14(
			L"再生ループBをここへ", L"Set preview loop B here", L"Boucle B d'aperçu ici", L"Loop B anteprima qui", L"Bucle B de vista aquí",
			L"미리듣기 루프 B 여기로", L"预览循环B到此处", L"حلقة B هنا", L"Превью B сюда", L"Vorschau-Schleife B hier",
			L"Loop B de prévia aqui", L"Voorbeeld-lus B hier", L"Podgląd pętli B tutaj", L"Önizleme döngü B buraya"));
		menu.AddSeparator();
		menu.AddCommand(9001, m_ui.mute[tr] ? L"Unmute" : L"Mute");
		menu.AddCommand(9002, m_ui.solo[tr] ? L"Unsolo" : L"Solo");
		menu.AddSeparator();
	}
	menu.AddCommand(IDC_SASAMI_FM_PLAY, LL14(
		L"再生確認", L"Preview", L"Aperçu", L"Anteprima", L"Vista previa",
		L"미리듣기", L"试听", L"معاينة", L"Просмотр", L"Vorschau",
		L"Prévia", L"Voorbeeld", L"Podgląd", L"Önizle"));
	menu.AddCommand(IDC_SASAMI_FM_EXPORT, LL14(
		L"音声書き出し…", L"Audio export…", L"Export audio…", L"Esporta audio…", L"Exportar audio…",
		L"오디오 내보내기…", L"导出音频…", L"تصدير صوت…", L"Экспорт аудио…", L"Audio exportieren…",
		L"Exportar áudio…", L"Audio exporteren…", L"Eksport audio…", L"Ses dışa aktar…"));
	menu.AddSeparator();
	menu.AddCommand(IDC_SASAMI_FM_HELP, LL14(
		L"ヘルプ", L"Help", L"Aide", L"Guida", L"Ayuda",
		L"도움말", L"帮助", L"مساعدة", L"Справка", L"Hilfe",
		L"Ajuda", L"Help", L"Pomoc", L"Yardım"));
	const UINT cmd = menu.Track(point, this);
	auto afterMark = [&]() {
		m_ui.visible[tr] = 1;
		m_curCh = tr;
		if (m_ch.GetSafeHwnd()) m_ch.SetCurSel(m_curCh);
		RefreshToneLabels();
		InvalidateRect(m_bodyRc, FALSE);
		CString st;
		st.Format(L"FM ch %d mark @ tick %u", tr + 1, (unsigned)atTick);
		m_status.SetWindowText(st);
	};
	if (tr >= 0 && cmd == 9010) {
		m_curCh = tr;
		if (m_ch.GetSafeHwnd()) m_ch.SetCurSel(m_curCh);
		m_ui.visible[tr] = 1;
		OpenVoiceEditor();
	}
	else if (tr >= 0 && cmd == 9020) {
		if (ScFmAddLoopStart(&m_doc, atTick, tr, 2)) afterMark();
	}
	else if (tr >= 0 && cmd == 9021) {
		if (ScFmAddLoopEnd(&m_doc, atTick, tr)) afterMark();
	}
	else if (tr >= 0 && cmd == 9022) {
		if (ScFmAddJumpMark(&m_doc, atTick, tr)) afterMark();
	}
	else if (tr >= 0 && cmd == 9023) {
		if (ScFmAddJump(&m_doc, atTick, tr)) afterMark();
	}
	else if (cmd == 9024 && markEv >= 0 && markEv < m_doc.evCount) {
		for (int j = markEv; j + 1 < m_doc.evCount; j++)
			m_doc.ev[j] = m_doc.ev[j + 1];
		m_doc.evCount--;
		if (m_ui.selEv == markEv) m_ui.selEv = -1;
		else if (m_ui.selEv > markEv) m_ui.selEv--;
		RefreshToneLabels();
		InvalidateRect(m_bodyRc, FALSE);
	}
	else if (tr >= 0 && cmd == 9025) {
		m_ui.loopATick = (int)atTick;
		if (m_ui.loopBTick >= 0 && m_ui.loopBTick <= m_ui.loopATick)
			m_ui.loopBTick = -1;
		InvalidateRect(m_bodyRc, FALSE);
	}
	else if (tr >= 0 && cmd == 9026) {
		m_ui.loopBTick = (int)atTick;
		if (m_ui.loopATick < 0) m_ui.loopATick = 0;
		if (m_ui.loopBTick <= m_ui.loopATick)
			m_ui.loopBTick = m_ui.loopATick + SC_PPQN * SC_MEASURE_BEATS;
		InvalidateRect(m_bodyRc, FALSE);
	}
	else if (tr >= 0 && cmd == 9001) { m_ui.mute[tr] ^= 1; InvalidateRect(m_trackRc, FALSE); }
	else if (tr >= 0 && cmd == 9002) { m_ui.solo[tr] ^= 1; InvalidateRect(m_trackRc, FALSE); }
	else if (cmd) PostMessage(WM_COMMAND, cmd);
}

void CSasamiFmScoreDlg::PersistUiGeom()
{
	ScSaveWndGeom(this, &savedata.sasamiFmX, &savedata.sasamiFmY,
		&savedata.sasamiFmW, &savedata.sasamiFmH);
	savedata.sasamiFmPxBeat = m_ui.pxBeat;
	savedata.sasamiFmStaffScale = m_ui.staffScale;
	savedata.sasamiFmScrollX = m_ui.scrollX;
	savedata.sasamiFmScrollY = m_ui.scrollY;
}

void CSasamiFmScoreDlg::RestoreUiGeom()
{
	if (savedata.sasamiFmPxBeat >= SC_PX_BEAT_MIN && savedata.sasamiFmPxBeat <= SC_PX_BEAT_MAX)
		m_ui.pxBeat = savedata.sasamiFmPxBeat;
	if (savedata.sasamiFmStaffScale >= SC_STAFF_SCALE_MIN && savedata.sasamiFmStaffScale <= SC_STAFF_SCALE_MAX)
		m_ui.staffScale = savedata.sasamiFmStaffScale;
	if (savedata.sasamiFmScrollX >= 0)
		m_ui.scrollX = savedata.sasamiFmScrollX;
	if (savedata.sasamiFmScrollY >= 0)
		m_ui.scrollY = savedata.sasamiFmScrollY;
	if (!ScRestoreWndGeom(this, savedata.sasamiFmX, savedata.sasamiFmY,
		savedata.sasamiFmW, savedata.sasamiFmH, 720, 420))
		SetWindowPos(NULL, 0, 0, 1100, 720, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	LayoutChrome();
	UpdateScrollBars();
}

void CSasamiFmScoreDlg::OnClose()
{
	PersistUiGeom();
	KillTimer(kScFmPreviewTimer);
	m_ui.previewActive = 0;
	VstLiveEditorOpenCancelPending();
	VstLiveMonitorStop();
	CSasamiNotePropsDlg::CloseOpen();
	DestroyWindow();
}
void CSasamiFmScoreDlg::OnDestroy()
{
	KillTimer(kScFmPreviewTimer);
	m_ui.previewActive = 0;
	CCustomBlurDialogExBase::OnDestroy();
}
void CSasamiFmScoreDlg::PostNcDestroy()
{
	if (s_inst == this) s_inst = NULL;
	CCustomBlurDialogExBase::PostNcDestroy();
	delete this;
}

void CSasamiFmScoreDlg::OnBnClickedText()
{
	CSasamiTextDlg::OpenOwned(this);
}
