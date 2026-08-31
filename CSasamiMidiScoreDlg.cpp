#include "stdafx.h"
#include "ogg.h"
#include "CSasamiMidiScoreDlg.h"
#include "CSasamiNotePaletteDlg.h"
#include "CSasamiSimpleInputDlg.h"
#include "CSasamiNotePropsDlg.h"
#include "CSasamiVstPickDlg.h"
#include "CSasamiVstPartMenu.h"
#include "CSasamiTextDlg.h"
#include "CSasamiExcRpnDlg.h"
#include "CSasamiInsertFxDlg.h"
#include "CCustomPopupMenu.h"
#include "OfflineHelp.h"
#include "PlayList.h"
#include "VstMidiEngine.h"
#include "SasamiToneNames.h"
#include "kb_sasami/source/sasami_write.h"
#include "kb_sasami/source/sasami_file.h"
#include "kb_sasami/source/sasami_midi.h"

void MmBindVstActiveSlot();

extern CPlayList* pl;

enum { kScPreviewTimer = 7101 };

static void ScMidiSetVstLabel(ScStaffUi* u, int part0, const wchar_t* path)
{
	if (!u || part0 < 0 || part0 >= 32 || !path || !path[0]) return;
	const wchar_t* base = wcsrchr(path, L'\\');
	if (!base) base = wcsrchr(path, L'/');
	base = base ? base + 1 : path;
	wcsncpy_s(u->vstLabel[part0], base, _TRUNCATE);
	wchar_t* dot = wcsrchr(u->vstLabel[part0], L'.');
	if (dot) *dot = 0;
	/* Kit plugs: default grand → drum staff (C1 bottom) so octave-1 pads sit on lines. */
	if (u->clef[part0] == 2) {
		const wchar_t* n = u->vstLabel[part0];
		if (StrStrIW(n, L"Groove") || StrStrIW(n, L"Battery")
			|| StrStrIW(n, L"Drum") || StrStrIW(n, L"BFD"))
			u->clef[part0] = 3;
	}
}


CSasamiMidiScoreDlg* CSasamiMidiScoreDlg::s_inst = NULL;

static void StripPaintLine(ScStaffUi* u, int lane, int c0, int v0, int c1, int v1)
{
	if (!u || lane < 0 || lane >= SC_STRIP_LANES_MAX) return;
	if (c0 > c1) { int t = c0; c0 = c1; c1 = t; t = v0; v0 = v1; v1 = t; }
	if (c0 < 0) c0 = 0;
	if (c1 > 255) c1 = 255;
	if (c0 == c1) {
		int v = v0;
		if (v < 0) v = 0;
		if (v > 127) v = 127;
		u->strip[lane][c0] = (uint8_t)v;
		return;
	}
	for (int c = c0; c <= c1; c++) {
		int v = v0 + (v1 - v0) * (c - c0) / (c1 - c0);
		if (v < 0) v = 0;
		if (v > 127) v = 127;
		u->strip[lane][c] = (uint8_t)v;
	}
}

IMPLEMENT_DYNAMIC(CSasamiMidiScoreDlg, CCustomBlurDialogExBase)

CSasamiMidiScoreDlg::CSasamiMidiScoreDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(CSasamiMidiScoreDlg::IDD, pParent)
	, m_curCh(0), m_placeRest(0), m_accidental(0), m_blankCur(NULL), m_sbDrag(0), m_bInLayout(FALSE), m_propMode(0)
	, m_pendingEdClosePart(0), m_pendingEdCloseProg(-1), m_pendingEdOpenPart(0)
	, m_clipN(0), m_clipBase(0)
{
	m_lastOut[0] = 0;
	m_lastHoverSt[0] = 0;
	ScMidiDocClear(&m_doc);
	m_doc.tempoT = 13000;
	ScStaffUiInit(&m_ui, SC_MIDI_CH, 0);
}

CSasamiMidiScoreDlg::~CSasamiMidiScoreDlg()
{
	if (m_blankCur) { ::DestroyCursor(m_blankCur); m_blankCur = NULL; }
}

CSasamiMidiScoreDlg* CSasamiMidiScoreDlg::Instance() { return s_inst; }

void CSasamiMidiScoreDlg::OpenOwned(CWnd* owner)
{
	const int created = !(s_inst && ::IsWindow(s_inst->GetSafeHwnd()));
	if (!created) {
		CCC_PresentOwnedHelp(s_inst, owner ? owner : AfxGetMainWnd());
		s_inst->PullDocFromText();
		return;
	}
	s_inst = new CSasamiMidiScoreDlg(owner);
	CWnd* parent = AfxGetMainWnd();
	if (!parent) parent = owner;
	if (!s_inst->Create(IDD_SASAMI_MIDI_SCORE, parent)) {
		delete s_inst;
		s_inst = NULL;
		AfxMessageBox(L"MIDI Score: Create() failed (check resource IDD_SASAMI_MIDI_SCORE).", MB_ICONERROR);
		return;
	}
	/* Session/text compile after Create — keep large work off the Create path. */
	s_inst->PullDocFromText();
	if (s_inst->m_doc.evCount <= 0) {
		ScMidiDoc* tmp = (ScMidiDoc*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ScMidiDoc));
		if (tmp) {
			if (ScSessionLoadLastMidi(tmp, NULL, 0))
				s_inst->LoadFromDoc(*tmp);
			ScMidiDocClear(tmp);
			HeapFree(GetProcessHeap(), 0, tmp);
		}
	}
	s_inst->SyncMeterFromDoc();
	s_inst->RefreshProgLabels();
	ScStaffUpdateContentExtent(&s_inst->m_ui, s_inst->m_doc.ev, s_inst->m_doc.evCount);
	s_inst->InvalidateRect(s_inst->m_bodyRc, FALSE);
	CCC_PresentOwnedHelp(s_inst, parent);
}

void CSasamiMidiScoreDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SASAMI_MIDI_OPEN, m_btnOpen);
	DDX_Control(pDX, IDC_SASAMI_MIDI_SAVE, m_btnSave);
	DDX_Control(pDX, IDC_SASAMI_MIDI_NEW, m_btnNew);
	DDX_Control(pDX, IDC_SASAMI_MIDI_PLAY, m_btnPlay);
	DDX_Control(pDX, IDC_SASAMI_MIDI_EXPORT, m_btnExport);
	DDX_Control(pDX, IDC_SASAMI_MIDI_HELP, m_btnHelp);
	DDX_Control(pDX, IDC_SASAMI_MIDI_TEMPO, m_btnTempo);
	DDX_Control(pDX, IDC_SASAMI_MIDI_TOOL_PENCIL, m_btnPencil);
	DDX_Control(pDX, IDC_SASAMI_MIDI_TOOL_ERASE, m_btnErase);
	DDX_Control(pDX, IDC_SASAMI_MIDI_TOOL_SEL, m_btnSel);
	DDX_Control(pDX, IDC_SASAMI_MIDI_PAL, m_btnPal);
	DDX_Control(pDX, IDC_SASAMI_MIDI_PROP_UPD, m_btnPropUpd);
	DDX_Control(pDX, IDC_SASAMI_MIDI_PROP_NOTE, m_edNote);
	DDX_Control(pDX, IDC_SASAMI_MIDI_PROP_GT, m_edGt);
	DDX_Control(pDX, IDC_SASAMI_MIDI_PROP_VEL, m_edVel);
	DDX_Control(pDX, IDC_SASAMI_MIDI_CH, m_ch);
	DDX_Control(pDX, IDC_SASAMI_MIDI_STATUS, m_status);
	DDX_Control(pDX, IDC_SASAMI_STRIP_KIND0, m_stripKind0);
	DDX_Control(pDX, IDC_SASAMI_STRIP_KIND1, m_stripKind1);
	DDX_Control(pDX, IDC_SASAMI_STRIP_DRAW, m_stripDraw);
	DDX_Control(pDX, IDC_SASAMI_STRIP_LANES, m_stripLanes);
	DDX_Control(pDX, IDC_SASAMI_MIDI_MARK, m_btnMark);
	DDX_Control(pDX, IDC_SASAMI_MIDI_LOOPA, m_btnLoopA);
	DDX_Control(pDX, IDC_SASAMI_MIDI_LOOPB, m_btnLoopB);
	DDX_Control(pDX, IDC_SASAMI_MIDI_LOOPCLR, m_btnLoopClr);
	DDX_Control(pDX, IDC_SASAMI_MIDI_SHOWALL, m_btnShowAll);
	DDX_Control(pDX, IDC_SASAMI_MIDI_TEXT, m_btnText);
	DDX_Control(pDX, IDC_SASAMI_MIDI_FX, m_btnFx);
}

BEGIN_MESSAGE_MAP(CSasamiMidiScoreDlg, CCustomBlurDialogExBase)
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
	ON_WM_KEYDOWN()
	ON_BN_CLICKED(IDC_SASAMI_MIDI_OPEN, &CSasamiMidiScoreDlg::OnBnClickedOpen)
	ON_BN_CLICKED(IDC_SASAMI_MIDI_SAVE, &CSasamiMidiScoreDlg::OnBnClickedSave)
	ON_BN_CLICKED(IDC_SASAMI_MIDI_NEW, &CSasamiMidiScoreDlg::OnBnClickedNew)
	ON_BN_CLICKED(IDC_SASAMI_MIDI_PLAY, &CSasamiMidiScoreDlg::OnBnClickedPlay)
	ON_BN_CLICKED(IDC_SASAMI_MIDI_EXPORT, &CSasamiMidiScoreDlg::OnBnClickedExport)
	ON_BN_CLICKED(IDC_SASAMI_MIDI_HELP, &CSasamiMidiScoreDlg::OnBnClickedHelp)
	ON_BN_CLICKED(IDC_SASAMI_MIDI_TEMPO, &CSasamiMidiScoreDlg::OnBnClickedTempo)
	ON_BN_CLICKED(IDC_SASAMI_MIDI_TOOL_PENCIL, &CSasamiMidiScoreDlg::OnBnClickedPencil)
	ON_BN_CLICKED(IDC_SASAMI_MIDI_TOOL_ERASE, &CSasamiMidiScoreDlg::OnBnClickedErase)
	ON_BN_CLICKED(IDC_SASAMI_MIDI_TOOL_SEL, &CSasamiMidiScoreDlg::OnBnClickedSel)
	ON_BN_CLICKED(IDC_SASAMI_MIDI_PAL, &CSasamiMidiScoreDlg::OnBnClickedPal)
	ON_BN_CLICKED(IDC_SASAMI_MIDI_PROP_UPD, &CSasamiMidiScoreDlg::OnBnClickedPropUpd)
	ON_BN_CLICKED(IDC_SASAMI_MIDI_MARK, &CSasamiMidiScoreDlg::OnBnClickedMark)
	ON_BN_CLICKED(IDC_SASAMI_MIDI_LOOPA, &CSasamiMidiScoreDlg::OnBnClickedLoopA)
	ON_BN_CLICKED(IDC_SASAMI_MIDI_LOOPB, &CSasamiMidiScoreDlg::OnBnClickedLoopB)
	ON_BN_CLICKED(IDC_SASAMI_MIDI_LOOPCLR, &CSasamiMidiScoreDlg::OnBnClickedLoopClr)
	ON_BN_CLICKED(IDC_SASAMI_MIDI_SHOWALL, &CSasamiMidiScoreDlg::OnBnClickedShowAll)
	ON_BN_CLICKED(IDC_SASAMI_MIDI_TEXT, &CSasamiMidiScoreDlg::OnBnClickedText)
	ON_BN_CLICKED(IDC_SASAMI_MIDI_FX, &CSasamiMidiScoreDlg::OnBnClickedFx)
	ON_CBN_SELCHANGE(IDC_SASAMI_MIDI_CH, &CSasamiMidiScoreDlg::OnCbnSelchangeCh)
	ON_CBN_SELCHANGE(IDC_SASAMI_STRIP_KIND0, &CSasamiMidiScoreDlg::OnCbnStrip)
	ON_CBN_SELCHANGE(IDC_SASAMI_STRIP_KIND1, &CSasamiMidiScoreDlg::OnCbnStrip)
	ON_CBN_SELCHANGE(IDC_SASAMI_STRIP_DRAW, &CSasamiMidiScoreDlg::OnCbnStrip)
	ON_CBN_SELCHANGE(IDC_SASAMI_STRIP_LANES, &CSasamiMidiScoreDlg::OnCbnStrip)
	ON_MESSAGE(WM_SASAMI_PAL_DUR, &CSasamiMidiScoreDlg::OnPalDur)
	ON_MESSAGE(WM_SASAMI_NOTE_PROPS, &CSasamiMidiScoreDlg::OnNoteProps)
	ON_MESSAGE(WM_SASAMI_EXC_RPN_CHANGED, &CSasamiMidiScoreDlg::OnExcRpnChanged)
	ON_MESSAGE(WM_SASAMI_INSERT_FX_CHANGED, &CSasamiMidiScoreDlg::OnInsertFxChanged)
	ON_MESSAGE(WM_APP + 61, &CSasamiMidiScoreDlg::OnDeferredInit)
	ON_MESSAGE(WM_APP + 62, &CSasamiMidiScoreDlg::OnDeferredPushText)
	ON_MESSAGE(WM_APP + 7204, &CSasamiMidiScoreDlg::OnDeferredOpenVst)
	ON_MESSAGE(WM_APP + 7206, &CSasamiMidiScoreDlg::OnDeferredProgLabels)
	ON_MESSAGE(WM_VST_LIVE_EDITOR_CLOSED, &CSasamiMidiScoreDlg::OnVstEditorClosed)
	ON_MESSAGE(WM_VST_LIVE_EDITOR_CLOSED_UI, &CSasamiMidiScoreDlg::OnVstEditorClosedUi)
END_MESSAGE_MAP()

void CSasamiMidiScoreDlg::ApplyLang()
{
	SetWindowText(LL14(L"SASAMI MIDI\u8b5c\u9762", L"SASAMI MIDI Score", L"Partition MIDI SASAMI", L"Partitura MIDI SASAMI", L"Partitura MIDI SASAMI",
		L"SASAMI MIDI Score", L"SASAMI MIDI Score", L"SASAMI MIDI Score", L"SASAMI MIDI Score", L"SASAMI MIDI-Partitur",
		L"Partitura MIDI SASAMI", L"SASAMI MIDI-partituur", L"Partytura SASAMI MIDI", L"SASAMI MIDI Skor"));
	m_btnOpen.SetWindowText(LL14(L"\u958b\u304f", L"Open", L"Ouvrir", L"Apri", L"Abrir", L"Open", L"Open", L"Open", L"Open", L"Offnen", L"Abrir", L"Openen", L"Otworz", L"Ac"));
	m_btnSave.SetWindowText(LL14(L"\u4fdd\u5b58", L"Save", L"Enregistrer", L"Salva", L"Guardar", L"Save", L"Save", L"Save", L"Save", L"Speichern", L"Salvar", L"Opslaan", L"Zapisz", L"Kaydet"));
	if (m_btnNew.GetSafeHwnd())
		m_btnNew.SetWindowText(LL14(L"\u65b0\u898f", L"New", L"Nouveau", L"Nuovo", L"Nuevo", L"New", L"New", L"New", L"New", L"Neu", L"Novo", L"Nieuw", L"Nowy", L"Yeni"));
	m_btnPlay.SetWindowText(LL14(L"\u518d\u751f\u78ba\u8a8d", L"Preview", L"Apercu", L"Anteprima", L"Vista previa", L"Preview", L"Preview", L"Preview", L"Preview", L"Vorschau", L"Previa", L"Voorbeeld", L"Podglad", L"Onizle"));
	m_btnExport.SetWindowText(LL14(L"\u66f8\u304d\u51fa\u3057", L"Export", L"Exporter", L"Esporta", L"Exportar", L"Export", L"Export", L"Export", L"Export", L"Export", L"Exportar", L"Exporteren", L"Eksport", L"Disa aktar"));
	m_btnHelp.SetWindowText(LL14(L"\u30d8\u30eb\u30d7", L"Help", L"Aide", L"Guida", L"Ayuda", L"Help", L"Help", L"Help", L"Help", L"Hilfe", L"Ajuda", L"Help", L"Pomoc", L"Yardim"));
	m_btnTempo.SetWindowText(LL14(L"\u30c6\u30f3\u30dd", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"Tempo"));
	m_btnPencil.SetWindowText(LL14(L"\u925b\u7b46", L"Pencil", L"Crayon", L"Matita", L"Lapiz", L"Pencil", L"Pencil", L"Pencil", L"Pencil", L"Stift", L"Lapis", L"Potlood", L"Olowek", L"Kalem"));
	m_btnErase.SetWindowText(LL14(L"\u6d88\u3057\u30b4\u30e0", L"Erase", L"Gomme", L"Gomma", L"Borrar", L"Erase", L"Erase", L"Erase", L"Erase", L"Radierer", L"Borracha", L"Gum", L"Gumka", L"Silgi"));
	m_btnSel.SetWindowText(LL14(L"\u9078\u629e", L"Select", L"Selection", L"Selezione", L"Seleccionar", L"Select", L"Select", L"Select", L"Select", L"Auswahl", L"Selecionar", L"Selecteren", L"Zaznacz", L"Sec"));
	m_btnPal.SetWindowText(LL14(L"\u97f3\u7b26", L"Notes", L"Notes", L"Note", L"Notas", L"Notes", L"Notes", L"Notes", L"Notes", L"Noten", L"Notas", L"Noten", L"Nuty", L"Notalar"));
	m_btnPropUpd.SetWindowText(LL14(L"\u66f4\u65b0", L"Update", L"Maj", L"Aggiorna", L"Actualizar", L"Update", L"Update", L"Update", L"Update", L"Aktualisieren", L"Atualizar", L"Bijwerken", L"Aktualizuj", L"Guncelle"));
	m_btnMark.SetWindowText(L">|");
	m_btnLoopA.SetWindowText(L"A");
	m_btnLoopB.SetWindowText(L"B");
	m_btnLoopClr.SetWindowText(L"A-Bx");
	m_btnShowAll.SetWindowText(L"32ch");
	if (m_btnText.GetSafeHwnd())
		m_btnText.SetWindowText(LL14(L"\u30c6\u30ad\u30b9\u30c8", L"Text", L"Texte", L"Testo", L"Texto", L"Text", L"Text", L"Text", L"Text", L"Text", L"Texto", L"Tekst", L"Tekst", L"Metin"));
	if (m_btnFx.GetSafeHwnd())
		m_btnFx.SetWindowText(LL14(
			L"\u30A4\u30F3\u30B5\u30FC\u30C8FX", L"Insert FX", L"Insert FX", L"Insert FX", L"Insert FX",
			L"Insert FX", L"Insert FX", L"Insert FX", L"Insert FX", L"Insert FX",
			L"Insert FX", L"Insert FX", L"Insert FX", L"Insert FX"));
}

BOOL CSasamiMidiScoreDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	s_inst = this;

	auto flatBtn = [](CCustomStandardButton& b) {
		if (!b.GetSafeHwnd()) return;
		b.SetAeroMode(FALSE);
		b.SetFlat(TRUE);
	};
	flatBtn(m_btnOpen); flatBtn(m_btnSave); flatBtn(m_btnNew); flatBtn(m_btnPlay); flatBtn(m_btnExport);
	flatBtn(m_btnHelp); flatBtn(m_btnTempo); flatBtn(m_btnPencil); flatBtn(m_btnErase);
	flatBtn(m_btnSel); flatBtn(m_btnPal); flatBtn(m_btnPropUpd); flatBtn(m_btnMark);
	flatBtn(m_btnLoopA); flatBtn(m_btnLoopB); flatBtn(m_btnLoopClr); flatBtn(m_btnShowAll);
	if (m_btnText.GetSafeHwnd()) flatBtn(m_btnText);
	if (m_btnFx.GetSafeHwnd()) flatBtn(m_btnFx);
	/* Score tool icons (embedded res\\ui_score / ui_ctl / ui_icons). */
	if (m_btnOpen.GetSafeHwnd()) m_btnOpen.SetIcon(IDI_UI_FOLDER);
	if (m_btnSave.GetSafeHwnd()) m_btnSave.SetIcon(IDI_UI_FILE);
	if (m_btnNew.GetSafeHwnd()) m_btnNew.SetIcon(IDI_CTL_PLUS);
	if (m_btnPencil.GetSafeHwnd()) m_btnPencil.SetIcon(IDI_SCORE_PENCIL);
	if (m_btnErase.GetSafeHwnd()) m_btnErase.SetIcon(IDI_SCORE_ERASE);
	if (m_btnSel.GetSafeHwnd()) m_btnSel.SetIcon(IDI_CTL_SEARCH);
	if (m_btnPal.GetSafeHwnd()) m_btnPal.SetIcon(IDI_SCORE_NOTE_Q);
	if (m_btnTempo.GetSafeHwnd()) m_btnTempo.SetIcon(IDI_UI_MUSIC);
	if (m_btnPlay.GetSafeHwnd()) m_btnPlay.SetIcon(IDI_CTL_PLAY);
	if (m_btnExport.GetSafeHwnd()) m_btnExport.SetIcon(IDI_UI_EXPORT);
	if (m_btnHelp.GetSafeHwnd()) m_btnHelp.SetIcon(IDI_UI_HELP);
	m_edNote.SetAeroMode(FALSE);
	m_edGt.SetAeroMode(FALSE);
	m_edVel.SetAeroMode(FALSE);
	m_ch.SetAeroMode(FALSE);
	m_status.SetAeroMode(FALSE);
	m_stripKind0.SetAeroMode(FALSE);
	m_stripKind1.SetAeroMode(FALSE);
	m_stripDraw.SetAeroMode(FALSE);
	m_stripLanes.SetAeroMode(FALSE);
	ApplyLang();
	m_ch.ResetContent();
	for (int i = 0; i < SC_MIDI_CH; i++) {
		wchar_t s[32];
		_snwprintf_s(s, _TRUNCATE, L"Ch %d", i + 1);
		m_ch.AddString(s);
	}
	m_ch.SetCurSel(0);
	m_edNote.SetWindowText(L"");
	m_edGt.SetWindowText(L"100");
	m_edVel.SetWindowText(L"100");
	m_status.SetWindowText(
		L"Tone=VST. Exc/RPN lane=SysEx・RPN. Toolbar「Insert FX」=パート・インサートEFX。");
	SyncStripCombos();
	RefreshProgLabels();
	UpdateNoteCursor();
	RefreshStrip();
	LayoutChrome();
	RestoreUiGeom();
	/* Defer VST sync + ednotify poll: sync on init can trip unloaded engine CS. */
	PostMessage(WM_APP + 61, 0, 0);
	return TRUE;
}

BOOL CSasamiMidiScoreDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	if (pMsg->message == WM_KEYDOWN) {
		if (pMsg->wParam == VK_DELETE || pMsg->wParam == VK_BACK || pMsg->wParam == VK_ESCAPE) {
			OnKeyDown((UINT)pMsg->wParam, 1, 0);
			return TRUE;
		}
	}
	return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}


void CSasamiMidiScoreDlg::SetupTooltips()
{
	if (!CCustomControlUtility::BeginDialogToolTip(m_tooltip, this, TTS_NOPREFIX)) return;
	auto tip = [&](CWnd& w, LPCWSTR s) { if (w.GetSafeHwnd()) m_tooltip.AddTool(&w, s); };
	tip(m_btnOpen, L"Open (re-edit binary via Text composer)");
	tip(m_btnSave, L"Save As MPW2/3 (includes VST paths if Tone assigned)");
	tip(m_btnNew, L"New — clear score / text session");
	tip(m_btnPlay, L"Preview: apply VST binds then play");
	tip(m_btnExport, L"Audio export");
	tip(m_btnPencil, L"Pencil tool");
	tip(m_btnErase, L"Eraser — click or drag over notes to delete");
	tip(m_btnSel, L"Select notes (click / drag). Delete or Backspace removes selection.");
	tip(m_btnPal, L"Note duration palette");
	tip(m_btnTempo, L"Tempo tool");
	tip(m_btnHelp, L"Help");
	tip(m_btnText, L"Open text composer");
	tip(m_btnFx, L"VST Insert FX (effect chain) for the current MIDI part — knobs / bypass / editor");
	tip(m_btnShowAll, L"Show all 32 channels");
	tip(m_stripKind0, L"Strip lane 1 kind (Expression/Volume/Pitch/Gate)");
	tip(m_stripKind1, L"Strip lane 2 kind");
	tip(m_stripDraw, L"Strip draw mode");
	tip(m_status, L"Status");
	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip);
}

void CSasamiMidiScoreDlg::LayoutChrome()
{
	if (!::IsWindow(m_hWnd) || m_bInLayout) return;
	m_bInLayout = TRUE;
	CRect rc;
	GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	const int pad = 8, btnH = 28, btnW = 64;
	const int sbW = ScStaffScrollTrackW();
	const int sbH = ScStaffScrollTrackH();
	int y = cap + pad;
	int x = pad;
	auto place = [&](CWnd& w, int ww) {
		if (w.GetSafeHwnd()) { w.MoveWindow(x, y, ww, btnH); x += ww + 4; }
	};
	place(m_btnOpen, btnW);
	place(m_btnSave, btnW);
	place(m_btnNew, btnW);
	place(m_btnPlay, 100);
	place(m_btnExport, btnW);
	place(m_btnPencil, btnW);
	place(m_btnErase, btnW);
	place(m_btnSel, btnW);
	place(m_btnPal, 48);
	place(m_btnTempo, btnW);
	if (m_ch.GetSafeHwnd()) { m_ch.MoveWindow(x, y, 72, 220); x += 80; }
	place(m_btnHelp, 48);
	y += btnH + 6;
	x = pad;
	const int propY = y;
	if (m_edNote.GetSafeHwnd()) m_edNote.MoveWindow(x + 28, propY, 44, 22);
	if (m_edGt.GetSafeHwnd()) m_edGt.MoveWindow(x + 104, propY, 44, 22);
	if (m_edVel.GetSafeHwnd()) m_edVel.MoveWindow(x + 180, propY, 44, 22);
	if (m_btnPropUpd.GetSafeHwnd()) m_btnPropUpd.MoveWindow(x + 232, propY, 52, btnH);
	/* Strip combos: fixed readable widths on their own row (no crush). */
	y += btnH + 6;
	x = pad;
	if (m_stripKind0.GetSafeHwnd()) { m_stripKind0.MoveWindow(x, y, 180, 220); x += 186; }
	if (m_stripKind1.GetSafeHwnd()) { m_stripKind1.MoveWindow(x, y, 180, 220); x += 186; }
	if (m_stripDraw.GetSafeHwnd()) { m_stripDraw.MoveWindow(x, y, 100, 140); x += 106; }
	if (m_stripLanes.GetSafeHwnd()) { m_stripLanes.MoveWindow(x, y, 64, 140); x += 70; }
	if (m_stripKind0.GetSafeHwnd())
		m_stripKind0.ShowWindow(m_ui.stripCount >= 1 ? SW_SHOW : SW_HIDE);
	if (m_stripKind1.GetSafeHwnd())
		m_stripKind1.ShowWindow(m_ui.stripCount >= 2 ? SW_SHOW : SW_HIDE);
	if (m_stripDraw.GetSafeHwnd())
		m_stripDraw.ShowWindow(m_ui.stripCount >= 1 ? SW_SHOW : SW_HIDE);
	y += btnH + 6;
	x = pad;
	place(m_btnMark, 36);
	place(m_btnLoopA, 28);
	place(m_btnLoopB, 28);
	place(m_btnLoopClr, 40);
	place(m_btnShowAll, 44);
	place(m_btnText, 64);
	place(m_btnFx, 78);
	y += btnH + 4;
	if (m_status.GetSafeHwnd())
		m_status.MoveWindow(pad, y, max(200, rc.Width() - pad * 2 - sbW), 22);
	y += 26;
	if (y > rc.Height() - pad - sbH - 80)
		y = max(cap + pad, rc.Height() - pad - sbH - 80);
	m_bodyRc.SetRect(pad, y, max(pad + 40, rc.Width() - pad - sbW), max(y + 40, rc.Height() - pad));
	/* H-scroll track reserved at bottom of body; strip sits above it (not crushed). */
	const int bodyBot = m_bodyRc.bottom - sbH;
	int stripH = ScStaffStripTotalH(&m_ui);
	if (stripH > 0) {
		const int minStrip = stripH;
		if (bodyBot - m_bodyRc.top - minStrip < 40)
			stripH = max(0, bodyBot - m_bodyRc.top - 40);
		else
			stripH = minStrip;
	}
	m_trackRc.SetRect(m_bodyRc.left, m_bodyRc.top, m_bodyRc.left + SC_TRACK_COL_W, max(m_bodyRc.top + 8, bodyBot - stripH));
	m_gridRc.SetRect(m_trackRc.right, m_bodyRc.top, m_bodyRc.right, m_trackRc.bottom);
	m_stripRc.SetRect(m_gridRc.left, m_trackRc.bottom, m_bodyRc.right, bodyBot);
	m_ui.followViewW = max(1, m_gridRc.Width());
	/* Scrollbars are in dialog STYLE — never ShowScrollBar/ModifyStyle here (re-enters OnSize→crash). */
	UpdateScrollBars();
	m_bInLayout = FALSE;
	/* Only staff body — never Invalidate full client (covers toolbar buttons → flicker). */
	if (m_bodyRc.Width() > 0)
		InvalidateRect(m_bodyRc, FALSE);
}

void CSasamiMidiScoreDlg::UpdateScrollBars()
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

void CSasamiMidiScoreDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	if (!m_bInLayout && m_btnOpen.GetSafeHwnd() && cx > 0 && cy > 0)
		LayoutChrome();
}

BOOL CSasamiMidiScoreDlg::OnEraseBkgnd(CDC* pDC)
{
	if (!pDC) return TRUE;
	CRect rc;
	GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	/* Chrome above staff body only — body is double-buffered in OnPaint. */
	CRect chrome(0, cap, rc.right, (m_bodyRc.top > cap) ? m_bodyRc.top : rc.bottom);
	if (chrome.Height() > 0) {
#if CCUSTOM_AERO_SUPPORT
		CCC_FillRectAlpha(pDC->GetSafeHdc(), chrome, RGB(236, 237, 242), 255);
#else
		pDC->FillSolidRect(&chrome, RGB(236, 237, 242));
#endif
	}
	CRect below(0, m_bodyRc.bottom, rc.right, rc.bottom);
	if (below.Height() > 0)
		pDC->FillSolidRect(&below, RGB(236, 237, 242));
	CRect side(m_bodyRc.right, m_bodyRc.top, rc.right, m_bodyRc.bottom);
	if (side.Width() > 0)
		pDC->FillSolidRect(&side, RGB(236, 237, 242));
	return TRUE;
}

void CSasamiMidiScoreDlg::SyncStripCombos()
{
	auto fillKind = [](CCustomComboBox& cb, int sel) {
		cb.ResetContent();
		for (int k = 0; k < SC_STRIP_KIND_COUNT; k++)
			cb.AddString(ScStaffStripKindName(k));
		if (sel < 0 || sel >= SC_STRIP_KIND_COUNT) sel = 0;
		cb.SetCurSel(sel);
	};
	fillKind(m_stripKind0, m_ui.stripKind[0]);
	fillKind(m_stripKind1, m_ui.stripKind[1]);
	m_stripDraw.ResetContent();
	m_stripDraw.AddString(L"Pencil");
	m_stripDraw.AddString(L"Line");
	m_stripDraw.AddString(L"Curve");
	if (m_ui.stripDraw < 0 || m_ui.stripDraw > 2) m_ui.stripDraw = SC_STRIP_DRAW_PENCIL;
	m_stripDraw.SetCurSel(m_ui.stripDraw);
	m_stripLanes.ResetContent();
	m_stripLanes.AddString(L"なし");
	m_stripLanes.AddString(L"1");
	m_stripLanes.AddString(L"2");
	int lanes = m_ui.stripCount;
	if (lanes < 0) lanes = 0;
	if (lanes > 2) lanes = 2;
	m_stripLanes.SetCurSel(lanes);
}

void CSasamiMidiScoreDlg::OnCbnStrip()
{
	int k0 = m_stripKind0.GetCurSel();
	int k1 = m_stripKind1.GetCurSel();
	int draw = m_stripDraw.GetCurSel();
	int lanes = m_stripLanes.GetCurSel();
	if (k0 >= 0) m_ui.stripKind[0] = k0;
	if (k1 >= 0) m_ui.stripKind[1] = k1;
	if (draw >= 0) m_ui.stripDraw = draw;
	if (lanes < 0) lanes = 0;
	if (lanes > 2) lanes = 2;
	m_ui.stripCount = lanes;
	RefreshStrip();
	LayoutChrome();
	Invalidate(FALSE);
}

void CSasamiMidiScoreDlg::RefreshStrip()
{
	ScStaffEnsureStripFromDoc(&m_ui, m_doc.ev, m_doc.evCount, m_curCh);
	ScStaffUpdateContentExtent(&m_ui, m_doc.ev, m_doc.evCount);
	UpdateScrollBars();
}

void CSasamiMidiScoreDlg::SyncPropFromSel()
{
	m_propMode = 0;
	if (m_ui.selEv < 0 || m_ui.selEv >= m_doc.evCount) {
		m_edNote.SetWindowText(L"");
		m_edGt.SetWindowText(L"");
		m_edVel.SetWindowText(L"");
		return;
	}
	const ScEvent& e = m_doc.ev[m_ui.selEv];
	if (e.kind != SC_EV_NOTE) return;
	CString s;
	s.Format(L"%d", (int)e.a);
	m_edNote.SetWindowText(s);
	int gate = (e.c >= 1 && e.c <= 100) ? (int)e.c : 100;
	s.Format(L"%d", gate);
	m_edGt.SetWindowText(s);
	s.Format(L"%d", (int)e.b);
	m_edVel.SetWindowText(s);
}

void CSasamiMidiScoreDlg::OpenNotePropsForSel()
{
	if (m_ui.selEv < 0 || m_ui.selEv >= m_doc.evCount) return;
	ScEvent& e = m_doc.ev[m_ui.selEv];
	if (e.kind != SC_EV_NOTE) return;
	SyncPropFromSel();
	/* MICP [midiCh:dataArea]: VST slot follows midiCh (trackPart), not dataArea. */
	int part = (int)e.ch + 1;
	if (e.ch < SC_MIDI_CH && m_doc.trackPart[e.ch] != 0xFF)
		part = (int)m_doc.trackPart[e.ch] + 1;
	if (part < 1) part = 1;
	if (part > 32) part = 32;
	SyncVstBindsFromLive();
	CSasamiNotePropsDlg::OpenForEvent(this, &e, 0, part);
}

void CSasamiMidiScoreDlg::OnPaint()
{
	CPaintDC dc(this);
	CRect rc;
	GetClientRect(&rc);
	CCC_CaptionPaint(dc, m_hWnd);

	/* Chrome labels — never blit staff bitmap over toolbar buttons. */
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(RGB(40, 40, 55));
	CFont* oldLab = dc.SelectObject(GetFont());
	if (m_propMode == 1) {
		if (m_edNote.GetSafeHwnd()) {
			CRect er; m_edNote.GetWindowRect(&er); ScreenToClient(&er);
			dc.TextOut(er.left - 36, er.top + 2, L"Prog");
		}
		if (m_edGt.GetSafeHwnd()) {
			CRect er; m_edGt.GetWindowRect(&er); ScreenToClient(&er);
			dc.TextOut(er.left - 36, er.top + 2, L"MSB");
		}
		if (m_edVel.GetSafeHwnd()) {
			CRect er; m_edVel.GetWindowRect(&er); ScreenToClient(&er);
			dc.TextOut(er.left - 32, er.top + 2, L"LSB");
		}
	} else {
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
			dc.TextOut(er.left - 28, er.top + 2, L"Vel");
		}
	}
	dc.SelectObject(oldLab);

	CRect body = m_bodyRc;
	if (body.Width() < 8 || body.Height() < 8) return;
	CDC mem;
	mem.CreateCompatibleDC(&dc);
	CBitmap bmp;
	if (!bmp.CreateCompatibleBitmap(&dc, body.Width(), body.Height())) return;
	CBitmap* old = mem.SelectObject(&bmp);
	if (!old) return;
	mem.FillSolidRect(0, 0, body.Width(), body.Height(), RGB(236, 237, 242));
	CRect track = m_trackRc; track.OffsetRect(-body.left, -body.top);
	CRect grid = m_gridRc; grid.OffsetRect(-body.left, -body.top);
	CRect strip = m_stripRc; strip.OffsetRect(-body.left, -body.top);
	if (track.Width() > 2 && track.Height() > 2)
		ScStaffPaintTracks(mem, track, &m_ui, m_curCh);
	if (grid.Width() > 2 && grid.Height() > 2)
		ScStaffPaintStaves(mem, grid, &m_ui, m_doc.ev, m_doc.evCount, 0, m_curCh, m_doc.tempoT);
	if (strip.Width() > 2 && strip.Height() > 2)
		ScStaffPaintStrip(mem, strip, &m_ui);
	ScStaffPaintMarquee(mem, &m_ui);
	CRect bodyRel(0, 0, body.Width(), body.Height());
	ScStaffPaintScrollThumbs(mem, bodyRel, bodyRel, &m_ui, max(1, m_gridRc.Width()), max(1, m_gridRc.Height()));
	CCC_BlitStretchOpaque(dc.GetSafeHdc(), body.left, body.top, body.Width(), body.Height(),
		mem.GetSafeHdc(), 0, 0, body.Width(), body.Height());
	mem.SelectObject(old);
}

void CSasamiMidiScoreDlg::UpdateNoteCursor()
{
	if (m_blankCur) {
		::DestroyCursor(m_blankCur);
		m_blankCur = NULL;
	}
	if (m_ui.tool == SC_TOOL_PENCIL || m_ui.tool == SC_TOOL_TEMPO)
		m_blankCur = ScStaffCreateBlankCursor();
}

void CSasamiMidiScoreDlg::UpdateHover(CPoint pt)
{
	m_ui.hoverValid = 0;
	if (!m_gridRc.PtInRect(pt)) return;
	/* ruler / Prog strip: no ghost note */
	if (pt.y < m_gridRc.top + SC_RULER_H) return;
	if (ScStaffPtInScoreCtrlStrip(m_gridRc, &m_ui, pt, NULL)) return;
	if (m_ui.tool != SC_TOOL_PENCIL && m_ui.tool != SC_TOOL_TEMPO) return;
	int hitTr = -1;
	int hit = ScStaffHitNote(m_gridRc, &m_ui, m_doc.ev, m_doc.evCount, 0, pt, &hitTr);
	/* Same recovery as PlaceOrEditAt — HitNote==-1 on gauge edge still allows staff hover. */
	if (hit != -2 || hitTr < 0) {
		int yCursor = m_gridRc.top + SC_RULER_H - m_ui.scrollY;
		hitTr = -1;
		hit = -1;
		for (int tr = 0; tr < m_ui.trackCount; tr++) {
			const int rowH = ScStaffRowH(&m_ui, tr);
			const int rowTop = yCursor;
			yCursor += rowH;
			if (!m_ui.visible[tr]) continue;
			const int noteTop = ScStaffRowNoteAreaTop(rowTop);
			if (pt.y >= noteTop && pt.y < rowTop + rowH - SC_PART_GAP) {
				hit = -2;
				hitTr = tr;
				break;
			}
		}
	}
	if (hit != -2 || hitTr < 0) return;
	int staffTop = ScStaffVisibleLaneStaffTop(m_gridRc, &m_ui, hitTr);
	if (staffTop < 0) return;
	const int quant = ScStaffPlaceQuant(&m_ui);
	m_ui.hoverTick = ScStaffXToTick(pt.x, m_ui.scrollX, m_gridRc.left + SC_CLEF_MARGIN, m_ui.pxBeat, quant);
	if (m_ui.tool == SC_TOOL_PENCIL) {
		m_ui.placeRest = m_placeRest;
		m_ui.hoverNote = ScStaffYToMidiNoteTrack(&m_ui, hitTr, staffTop, pt.y) + m_accidental;
	}
	m_ui.hoverTrack = hitTr;
	m_ui.hoverX = pt.x;
	m_ui.hoverY = pt.y;
	m_ui.hoverValid = 1;
}

void CSasamiMidiScoreDlg::UpdateHoverStatus(CPoint pt)
{
	wchar_t st[256];
	if (!ScStaffFormatPointerStatus(&m_ui, m_doc.ev, m_doc.evCount, 0, m_gridRc, pt, st, 256))
		return;
	if (wcscmp(st, m_lastHoverSt) == 0) return;
	wcsncpy_s(m_lastHoverSt, st, _TRUNCATE);
	if (m_status.GetSafeHwnd())
		m_status.SetWindowText(st);
}

void CSasamiMidiScoreDlg::PlaceOrEditAt(CPoint pt)
{
	/* Prog/Bank strip on score: select chip, never place notes */
	int ctrlTr = -1;
	int ctrl = ScStaffHitScoreCtrl(m_gridRc, &m_ui, m_doc.ev, m_doc.evCount, 0, pt, &ctrlTr);
	int markTr = -1;
	int mark = ScStaffHitStaffMark(m_gridRc, &m_ui, m_doc.ev, m_doc.evCount, 0, pt, &markTr);
	int hitTr = -1;
	int hit = ScStaffHitNote(m_gridRc, &m_ui, m_doc.ev, m_doc.evCount, 0, pt, &hitTr);

	if (m_ui.tool == SC_TOOL_ERASER) {
		int del = -1;
		if (ctrl >= 0) del = ctrl;
		else if (mark >= 0) del = mark;
		else if (hit >= 0) del = hit;
		if (del >= 0) {
			ScStaffSelSetPrimary(&m_ui, del);
			ScStaffSelDelete(m_doc.ev, &m_doc.evCount, &m_ui);
			RefreshStrip();
			PushDocToText();
			InvalidateRect(m_bodyRc, FALSE);
			m_status.SetWindowText(L"Deleted");
		} else if (m_ui.stripCount > 0 && m_stripRc.PtInRect(pt)) {
			int lane = 0, col = 0, val = 0;
			if (ScStaffHitStrip(m_stripRc, &m_ui, pt, &lane, &col, &val)) {
				m_ui.strip[lane][col & 255] = (m_ui.stripKind[lane] == SC_STRIP_PITCH) ? 64 : 0;
				ScStaffApplyStripToDocMidi(&m_doc, m_curCh, &m_ui);
				InvalidateRect(m_stripRc, FALSE);
			}
		} else {
			m_ui.eraseDrag = 1;
			SetCapture();
		}
		return;
	}

	if (ctrl >= 0) {
		m_ui.selEv = ctrl;
		m_curCh = m_doc.ev[ctrl].ch;
		if (m_ch.GetSafeHwnd()) m_ch.SetCurSel(m_curCh);
		if (m_doc.ev[ctrl].kind == SC_EV_PROG || m_doc.ev[ctrl].kind == SC_EV_BANK) {
			/* Score Prog/Bank chip → tone/VST assign (not just PC number edit). */
			int part = m_curCh + 1;
			if (m_curCh >= 0 && m_curCh < 32 && m_doc.trackPart[m_curCh] != 0xFF)
				part = (int)m_doc.trackPart[m_curCh] + 1;
			OpenVstForPart(part);
		}
		InvalidateRect(m_gridRc, FALSE);
		return;
	}
	if (m_ui.tool == SC_TOOL_SELECT && mark >= 0) {
		if (GetKeyState(VK_SHIFT) & 0x8000)
			ScStaffSelAdd(&m_ui, mark);
		else
			ScStaffSelSetPrimary(&m_ui, mark);
		m_curCh = m_doc.ev[mark].ch;
		if (m_ch.GetSafeHwnd()) m_ch.SetCurSel(m_curCh);
		m_status.SetWindowText(L"Mark selected — Delete/Backspace removes");
		InvalidateRect(m_bodyRc, FALSE);
		return;
	}
	if (ScStaffPtInScoreCtrlStrip(m_gridRc, &m_ui, pt, &ctrlTr))
		return;
	if (m_ui.tool == SC_TOOL_SELECT) {
		if (hit >= 0) {
			if (GetKeyState(VK_SHIFT) & 0x8000)
				ScStaffSelAdd(&m_ui, hit);
			else
				ScStaffSelSetPrimary(&m_ui, hit);
			m_ui.dragEv = hit;
			m_ui.dragOriginX = pt.x;
			m_ui.dragMode = 2;
			m_curCh = m_doc.ev[hit].ch;
			m_ch.SetCurSel(m_curCh);
			SyncPropFromSel();
			RefreshStrip();
			m_status.SetWindowText(L"Selected — Delete/Backspace removes. Exp/Vol/Pitch apply to this MIDI ch only.");
			InvalidateRect(m_bodyRc, FALSE);
		} else {
			ScStaffSelClear(&m_ui);
			m_ui.marqueeOn = 1;
			m_ui.marqueeX0 = m_ui.marqueeX1 = pt.x;
			m_ui.marqueeY0 = m_ui.marqueeY1 = pt.y;
			SetCapture();
			InvalidateRect(m_bodyRc, FALSE);
		}
		return;
	}
	if (m_ui.tool == SC_TOOL_TIE) {
		if (hit >= 0) {
			if (!ScStaffSelHas(&m_ui, hit))
				ScStaffSelAdd(&m_ui, hit);
			else
				ScStaffTieSelected(m_doc.ev, m_doc.evCount, &m_ui);
			InvalidateRect(m_bodyRc, FALSE);
		}
		return;
	}
	if (m_ui.tool == SC_TOOL_TEMPO) {
		if (hit == -2 && hitTr >= 0) {
			const int quant = ScStaffPlaceQuant(&m_ui);
			uint32_t tick = ScStaffXToTick(pt.x, m_ui.scrollX, m_gridRc.left + SC_CLEF_MARGIN, m_ui.pxBeat, quant);
			if (m_doc.evCount < SC_EV_MAX) {
				ScEvent& te = m_doc.ev[m_doc.evCount++];
				te.tick = tick;
				te.ch = 0;
				te.kind = SC_EV_TEMPO;
				te.a = (uint8_t)(m_doc.tempoT & 0xFF);
				te.b = (uint8_t)((m_doc.tempoT >> 8) & 0xFF);
				te.c = 0;
				te.dur = 0;
				RefreshStrip();
				InvalidateRect(m_bodyRc, FALSE);
			}
		}
		return;
	}
	/* pencil */
	if (hit >= 0) {
		m_ui.selEv = hit;
		ScStaffSelClear(&m_ui);
		ScStaffSelAdd(&m_ui, hit);
		m_ui.dragEv = hit;
		m_ui.dragOriginX = pt.x;
		{
			const int pxBeat = m_ui.pxBeat > 0 ? m_ui.pxBeat : SC_PX_BEAT_DEFAULT;
			int x1 = ScStaffTickToX(m_doc.ev[hit].tick + (m_doc.ev[hit].dur ? m_doc.ev[hit].dur : SC_PPQN / 4),
				m_ui.scrollX, m_gridRc.left + SC_CLEF_MARGIN, pxBeat);
			m_ui.dragMode = (pt.x >= x1 - 8) ? 1 : 2;
		}
		SyncPropFromSel();
		m_status.SetWindowText(L"Note selected — Delete/Backspace to remove. Exp/Pitch are per-channel.");
		InvalidateRect(m_bodyRc, FALSE);
		return;
	}
	/* Empty staff: HitNote returns -2. Also recover if hit==-1 but Y is in a staff lane. */
	if (hit != -2 || hitTr < 0) {
		if (!m_gridRc.PtInRect(pt) || pt.y < m_gridRc.top + SC_RULER_H) return;
		int yCursor = m_gridRc.top + SC_RULER_H - m_ui.scrollY;
		for (int tr = 0; tr < m_ui.trackCount; tr++) {
			const int rowH = ScStaffRowH(&m_ui, tr);
			const int rowTop = yCursor;
			yCursor += rowH;
			if (!m_ui.visible[tr]) continue;
			const int noteTop = ScStaffRowNoteAreaTop(rowTop);
			if (pt.y >= noteTop && pt.y < rowTop + rowH - SC_PART_GAP) {
				hit = -2;
				hitTr = tr;
				break;
			}
		}
	}
	if (hit == -2 && hitTr >= 0) {
		m_curCh = hitTr;
		m_ch.SetCurSel(m_curCh);
		m_ui.visible[hitTr] = 1;
		const int quant = ScStaffPlaceQuant(&m_ui);
		uint32_t tick = ScStaffXToTick(pt.x, m_ui.scrollX, m_gridRc.left + SC_CLEF_MARGIN, m_ui.pxBeat, quant);
		int staffTop = ScStaffVisibleLaneStaffTop(m_gridRc, &m_ui, hitTr);
		if (staffTop < 0) return;
		int note = ScStaffYToMidiNoteTrack(&m_ui, hitTr, staffTop, pt.y) + m_accidental;
		if (note < 0) note = 0;
		if (note > 127) note = 127;
		m_ui.dragMode = 0;
		if (m_placeRest) {
			ScMidiAddRest(&m_doc, tick, hitTr, m_ui.placeDur);
			if (m_doc.evCount > 0)
				m_ui.selEv = m_doc.evCount - 1;
		} else {
			int vel = 100;
			CString vs; m_edVel.GetWindowText(vs);
			if (!vs.IsEmpty()) vel = _wtoi(vs);
			if (vel < 1) vel = 1;
			if (vel > 127) vel = 127;
			if (ScMidiAddNote(&m_doc, tick, hitTr, note, m_ui.placeDur, vel)) {
				m_doc.ev[m_doc.evCount - 1].c = 100;
				m_ui.selEv = m_doc.evCount - 1;
				m_ui.dragEv = -1; /* don't drag-steal pitch on same click */
				SyncPropFromSel();
				CString st;
				st.Format(L"N=%d (O%d%s) tick=%u dur=%d",
					note, note / 12,
					(note % 12 == 0) ? L"C" : (note % 12 == 2) ? L"D" : (note % 12 == 4) ? L"E" :
					(note % 12 == 5) ? L"F" : (note % 12 == 7) ? L"G" : (note % 12 == 9) ? L"A" :
					(note % 12 == 11) ? L"B" : L"?",
					(unsigned)tick, m_ui.placeDur);
				m_status.SetWindowText(st);
			}
		}
		RefreshStrip();
		InvalidateRect(m_bodyRc, FALSE);
		PushDocToText();
	}
}


void CSasamiMidiScoreDlg::RefreshProgLabels()
{
	for (int i = 0; i < m_ui.trackCount && i < 32; i++) {
		if (!m_doc.bind.vstPath[i][0])
			m_ui.vstLabel[i][0] = 0;
		else
			ScMidiSetVstLabel(&m_ui, i, m_doc.bind.vstPath[i]);
		m_ui.progLabel[i][0] = 0;
	}
	for (int i = 0; i < m_doc.evCount; i++) {
		const ScEvent& e = m_doc.ev[i];
		const int ch = e.ch;
		if (ch < 0 || ch >= 32) continue;
		if (e.kind == SC_EV_PROG) {
			/* Score PROG is canonical for this channel (tone map writes it). */
			m_doc.bind.vstProg[ch] = (int)e.a;
		} else if (e.kind == SC_EV_BANK) {
			m_doc.bind.vstBankMsb[ch] = (int)e.a;
			m_doc.bind.vstBankLsb[ch] = (int)e.b;
		}
	}
	for (int i = 0; i < m_ui.trackCount && i < 32; i++) {
		wchar_t name[48];
		name[0] = 0;
		const int prog = m_doc.bind.vstProg[i];
		const int msb = m_doc.bind.vstBankMsb[i] >= 0 ? m_doc.bind.vstBankMsb[i] : 0;
		const int lsb = m_doc.bind.vstBankLsb[i] >= 0 ? m_doc.bind.vstBankLsb[i] : 0;
		const int pc = prog >= 0 ? (prog & 127) : 0;

		/* Never VstLiveProgramName/Current here — Host64 PROGRAMS IPC freezes
		   the UI on tone-map return (and after preview). GS/XG table + tip only. */
		if (m_ui.vstLabel[i][0] && prog < 0)
			wcsncpy_s(name, m_ui.vstLabel[i], _TRUNCATE);
		if (!name[0]) {
			const int isDrum = (i == 9 || i == 25);
			SasamiToneLookupAuto(msb, lsb, pc, isDrum, name, 48);
		}
		if (name[0])
			_snwprintf_s(m_ui.progLabel[i], _TRUNCATE, L"%s", name);
		else if (prog >= 0)
			_snwprintf_s(m_ui.progLabel[i], _TRUNCATE, L"Prog  %d", prog);
		else
			wcscpy_s(m_ui.progLabel[i], L"Prog  —");
		/* VST3 state chunks live in binds — not as Bank chips on the staff. */
		if (m_doc.bind.vstCompLen[i] > 0) {
			wchar_t withSt[80];
			_snwprintf_s(withSt, _TRUNCATE, L"%s  [%uk]",
				m_ui.progLabel[i],
				(unsigned)((m_doc.bind.vstCompLen[i] + 1023) / 1024));
			wcsncpy_s(m_ui.progLabel[i], withSt, _TRUNCATE);
		}
	}
}

/* Tone-map OK → tick-0 BANK/PROG from bind only (no edit boxes, no live IPC). */
void CSasamiMidiScoreDlg::ApplyBindProgToScore(int ch0)
{
	if (ch0 < 0 || ch0 >= 32) return;
	int prog = m_doc.bind.vstProg[ch0];
	if (prog < 0) prog = 0;
	if (prog > 127) prog = 127;
	int msb = m_doc.bind.vstBankMsb[ch0];
	if (msb < 0) msb = 0;
	if (msb > 127) msb = 127;
	int lsb = m_doc.bind.vstBankLsb[ch0];
	if (lsb < 0) lsb = 0;
	if (lsb > 127) lsb = 127;
	m_doc.bind.vstProg[ch0] = prog;
	m_doc.bind.vstBankMsb[ch0] = msb;
	m_doc.bind.vstBankLsb[ch0] = lsb;

	int foundProg = -1, foundBank = -1;
	for (int i = 0; i < m_doc.evCount; i++) {
		if (m_doc.ev[i].ch != (uint8_t)ch0) continue;
		if (m_doc.ev[i].kind == SC_EV_PROG && m_doc.ev[i].tick == 0) foundProg = i;
		if (m_doc.ev[i].kind == SC_EV_BANK && m_doc.ev[i].tick == 0) foundBank = i;
	}
	auto putEv = [&](int* found, uint8_t kind, uint8_t a, uint8_t b) {
		if (*found >= 0) {
			m_doc.ev[*found].a = a;
			m_doc.ev[*found].b = b;
			m_doc.ev[*found].c = 0;
			m_doc.ev[*found].dur = 0;
			return;
		}
		if (m_doc.evCount >= SC_EV_MAX) return;
		ScEvent& e = m_doc.ev[m_doc.evCount++];
		e.tick = 0; e.seq = (uint32_t)(m_doc.evCount - 1);
		e.ch = (uint8_t)ch0; e.kind = kind;
		e.a = a; e.b = b; e.c = 0; e.dur = 0;
		*found = m_doc.evCount - 1;
	};
	putEv(&foundBank, SC_EV_BANK, (uint8_t)msb, (uint8_t)lsb);
	putEv(&foundProg, SC_EV_PROG, (uint8_t)prog, (uint8_t)prog);
	if (foundBank >= 0 && foundProg >= 0 &&
		m_doc.ev[foundBank].tick == 0 && m_doc.ev[foundProg].tick == 0 &&
		m_doc.ev[foundBank].seq > m_doc.ev[foundProg].seq) {
		const uint32_t tmp = m_doc.ev[foundBank].seq;
		m_doc.ev[foundBank].seq = m_doc.ev[foundProg].seq;
		m_doc.ev[foundProg].seq = tmp;
	}

	wchar_t name[48];
	name[0] = 0;
	SasamiToneLookupAuto(msb, lsb, prog, (ch0 == 9 || ch0 == 25) ? 1 : 0, name, 48);
	if (name[0])
		_snwprintf_s(m_ui.progLabel[ch0], _TRUNCATE, L"%s", name);
	else
		_snwprintf_s(m_ui.progLabel[ch0], _TRUNCATE, L"Prog  %d", prog);

	InvalidateRect(m_trackRc, FALSE);
	if (m_bodyRc.Width() > 0)
		InvalidateRect(m_bodyRc, FALSE);
	CString st;
	st.Format(L"MIDI %d  Prog %d (PC#%d)  Bank %d/%d",
		ch0 + 1, prog, prog + 1, msb, lsb);
	m_status.SetWindowText(st);
}

void CSasamiMidiScoreDlg::SyncVstBindsFromLive()
{
	__try {
		for (int i = 1; i <= 32; i++) {
			if (!VstLivePartIsLoaded(i)) continue;
			wchar_t path[520];
			path[0] = 0;
			if (!VstLivePartGetPath(i, path, 520) || !path[0]) continue;
			wcsncpy_s(m_doc.bind.vstPath[i - 1], path, _TRUNCATE);
			m_doc.bind.isMpw3 = 1;
			ScMidiSetVstLabel(&m_ui, i - 1, path);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		/* VST engine may not be ready — leave binds as-is */
	}
	if (m_trackRc.Width() > 0)
		InvalidateRect(m_trackRc, FALSE);
}

void CSasamiMidiScoreDlg::SyncFxBindsToLive()
{
	__try {
		for (int ch = 0; ch < 32; ++ch) {
			for (int sl = 0; sl < ScMidiFxBind::SC_FX_SLOTS; ++sl) {
				const wchar_t* path = m_doc.fxBind.fxPath[ch][sl];
				if (!path[0]) continue;
				wchar_t cur[520] = {};
				int already = 0;
				if (VstLiveFxIsLoaded(ch + 1, sl) &&
					VstLiveFxGetPath(ch + 1, sl, cur, 520) && cur[0] &&
					_wcsicmp(cur, path) == 0) {
					already = 1;
				} else {
					const size_t n = wcslen(path);
					const int is3 = (n >= 5 && _wcsicmp(path + n - 5, L".vst3") == 0) ? 1 : 0;
					if (VstLiveLoadFx(ch + 1, sl, path, is3) != 0)
						continue;
				}
				VstLiveFxSetBypass(ch + 1, sl, m_doc.fxBind.fxBypass[ch][sl]);
				if (!already && m_doc.fxBind.fxStateLen[ch][sl] && m_doc.fxBind.fxState[ch][sl])
					VstLiveFxApplyState(ch + 1, sl, m_doc.fxBind.fxState[ch][sl],
						(int)m_doc.fxBind.fxStateLen[ch][sl]);
			}
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
	}
}

LRESULT CSasamiMidiScoreDlg::OnDeferredInit(WPARAM, LPARAM)
{
	__try {
		SyncVstBindsFromLive();
		SyncFxBindsToLive();
		RefreshProgLabels();
	} __except (EXCEPTION_EXECUTE_HANDLER) {
	}
	if (m_bodyRc.Width() > 0)
		InvalidateRect(m_bodyRc, FALSE);
	/* Start ednotify poll only after the window is fully up (not during Create). */
	VstLiveEditorSetNotifyHwnd(m_hWnd);
	SetTimer(9122, 250, NULL);
	/* Note palette always visible while score is open. */
	{
		CRect r;
		if (m_btnPal.GetSafeHwnd()) m_btnPal.GetWindowRect(&r);
		else GetWindowRect(&r);
		CSasamiNotePaletteDlg::OpenNear(this, CPoint(r.left, r.bottom + 4));
	}
	return 0;
}

LRESULT CSasamiMidiScoreDlg::OnDeferredPushText(WPARAM, LPARAM)
{
	PushDocToText();
	return 0;
}

void CSasamiMidiScoreDlg::OpenVstForPart(int part1to32, int editorOnly)
{
	if (part1to32 < 1 || part1to32 > 32) return;
	if (GetCapture() == this) ReleaseCapture();
	SyncVstBindsFromLive();

	/* editorOnly unused: tone / note-props always go Assign → editor (no menu).
	   Right-click track uses ScVstShowPartMenu separately. */
	(void)editorOnly;

	const int assignRc = ScVstAssignToneForPart(this, part1to32, &m_doc.bind);
	const int ok = (assignRc != 0) ? 1 : 0;

	wchar_t path[520];
	path[0] = 0;
	if (VstLivePartIsLoaded(part1to32) && VstLivePartGetPath(part1to32, path, 520) && path[0]) {
		wcsncpy_s(m_doc.bind.vstPath[part1to32 - 1], path, _TRUNCATE);
		m_doc.bind.isMpw3 = 1;
		ScMidiSetVstLabel(&m_ui, part1to32 - 1, path);
		m_doc.bind.vstForceCh[part1to32 - 1] = VstLiveSendChannel(part1to32);
	} else if (!m_doc.bind.vstPath[part1to32 - 1][0]) {
		m_ui.vstLabel[part1to32 - 1][0] = 0;
	} else {
		ScMidiSetVstLabel(&m_ui, part1to32 - 1, m_doc.bind.vstPath[part1to32 - 1]);
	}
	if (m_doc.bind.vstPath[part1to32 - 1][0])
		ScMidiSetVstLabel(&m_ui, part1to32 - 1, m_doc.bind.vstPath[part1to32 - 1]);
	/* Tone map OK → score BANK/PROG only. MML sync deferred (never during EndDialog). */
	if (ok) {
		ApplyBindProgToScore(part1to32 - 1);
		KillTimer(9124);
		SetTimer(9124, 100, NULL);
		if (m_bodyRc.Width() > 0)
			InvalidateRect(m_bodyRc, FALSE);
	}
	CString st;
	if (ok) {
		st.Format(L"MIDI %d tone applied (Prog/Bank → score)", part1to32);
	} else if (VstLivePartIsLoaded(part1to32)) {
		st.Format(L"MIDI %d tone cancelled", part1to32);
	} else {
		st.Format(L"MIDI %d tone cleared / cancelled", part1to32);
	}
	m_status.SetWindowText(st);
	InvalidateRect(m_trackRc, FALSE);
	/* Auto-open editor ONLY for dedicated VST3 (return 2). Tone-map / SC-VA VST2
	   (return 1) must never open editor — effEditOpen / Host64 freezes on 入力. */
	if (assignRc == 2 && VstLivePartIsLoaded(part1to32) && path[0]) {
		const size_t n = wcslen(path);
		const int isVst3 = (n >= 5 && _wcsicmp(path + n - 5, L".vst3") == 0) ? 1 : 0;
		if (isVst3) {
			m_pendingEdOpenPart = part1to32;
			KillTimer(9123);
			SetTimer(9123, 400, NULL);
		}
	}
}

LRESULT CSasamiMidiScoreDlg::OnDeferredProgLabels(WPARAM w, LPARAM)
{
	const int part = (int)w;
	/* Never PROGRAMS IPC from UI timers — freezes Host64 (tone map / preview). */
	(void)part;
	RefreshProgLabels();
	if (m_trackRc.Width() > 0)
		InvalidateRect(m_trackRc, FALSE);
	return 0;
}

LRESULT CSasamiMidiScoreDlg::OnDeferredOpenVst(WPARAM w, LPARAM l)
{
	const int part = (int)w;
	(void)l;
	if (part < 1 || part > 32) return 0;
	if (!::IsWindow(m_hWnd)) return 0;
	KillTimer(9122);
	m_status.SetWindowText(L"Opening VST editor…");
	VstLiveEditorOpenAsync(part);
	m_status.SetWindowText(L"VST editor requested — Home/MediaBay で音色を選び閉じると @VSTSTATEB64");
	SetTimer(9122, 250, NULL);
	return 0;
}

LRESULT CSasamiMidiScoreDlg::OnVstEditorClosed(WPARAM w, LPARAM l)
{
	const int part = (int)w;
	if (part < 1 || part > 32) return 0;
	const int prog = (int)l;
	m_doc.bind.isMpw3 = 1;
	wchar_t path[520];
	path[0] = 0;
	{
		if (VstLivePartGetPath(part, path, 520) && path[0]) {
			wcsncpy_s(m_doc.bind.vstPath[part - 1], path, _TRUNCATE);
			ScMidiSetVstLabel(&m_ui, part - 1, path);
		}
	}
	if (prog >= 0)
		m_doc.bind.vstProg[part - 1] = prog;
	/* Capture via close-snap / GET (SampleTank: Host64 snap only, no live getState). */
	KillTimer(9121);
	m_pendingEdClosePart = part;
	m_pendingEdCloseProg = prog;
	SetTimer(9121, 400, NULL);
	CString st;
	st.Format(L"VST editor closed — part %d (capturing state…)", part);
	m_status.SetWindowText(st);
	return 0;
}

LRESULT CSasamiMidiScoreDlg::OnVstEditorClosedUi(WPARAM w, LPARAM l)
{
	(void)l;
	const int part = (int)w;
	SetTimer(9122, 250, NULL);
	VstLiveEditorClearClosingQuiet();
	/* Do not drain SHM while song preview owns Host64 audio. */
	if (!m_ui.previewActive)
		VstLiveMonitorEnsure();
	RefreshProgLabels();
	InvalidateRect(m_trackRc, FALSE);
	if (part >= 1 && part <= 32)
		CSasamiNotePropsDlg::NotifyVstResult(part, 1);
	return 0;
}

void CSasamiMidiScoreDlg::SyncProgPropFromCh(int ch0)
{
	if (ch0 < 0 || ch0 >= 32) return;
	m_propMode = 1;
	m_curCh = ch0;
	CString s;
	int prog = m_doc.bind.vstProg[ch0];
	if (prog < 0) prog = 0;
	s.Format(L"%d", prog);
	if (m_edNote.GetSafeHwnd()) m_edNote.SetWindowText(s);
	int msb = m_doc.bind.vstBankMsb[ch0];
	if (msb < 0) msb = 0;
	s.Format(L"%d", msb);
	if (m_edGt.GetSafeHwnd()) m_edGt.SetWindowText(s);
	int lsb = m_doc.bind.vstBankLsb[ch0];
	if (lsb < 0) lsb = 0;
	s.Format(L"%d", lsb);
	if (m_edVel.GetSafeHwnd()) m_edVel.SetWindowText(s);
	InvalidateRect(CRect(0, 0, 480, max(1, m_bodyRc.top)), FALSE);
}

void CSasamiMidiScoreDlg::ApplyProgPropToCh()
{
	if (m_curCh < 0 || m_curCh >= 32) return;
	CString t;
	m_edNote.GetWindowText(t);
	int prog = _wtoi(t); if (prog < 0) prog = 0; if (prog > 127) prog = 127;
	m_edGt.GetWindowText(t);
	int msb = _wtoi(t); if (msb < 0) msb = 0; if (msb > 127) msb = 127;
	m_edVel.GetWindowText(t);
	int lsb = _wtoi(t); if (lsb < 0) lsb = 0; if (lsb > 127) lsb = 127;
	m_doc.bind.vstProg[m_curCh] = prog;
	m_doc.bind.vstBankMsb[m_curCh] = msb;
	m_doc.bind.vstBankLsb[m_curCh] = lsb;
	int foundProg = -1, foundBank = -1;
	for (int i = 0; i < m_doc.evCount; i++) {
		if (m_doc.ev[i].ch != (uint8_t)m_curCh) continue;
		if (m_doc.ev[i].kind == SC_EV_PROG && m_doc.ev[i].tick == 0) foundProg = i;
		if (m_doc.ev[i].kind == SC_EV_BANK && m_doc.ev[i].tick == 0) foundBank = i;
	}
	auto putEv = [&](int* found, uint8_t kind, uint8_t a, uint8_t b) {
		if (*found >= 0) {
			m_doc.ev[*found].a = a;
			m_doc.ev[*found].b = b;
			m_doc.ev[*found].c = 0;
			m_doc.ev[*found].dur = 0;
			return;
		}
		if (m_doc.evCount >= SC_EV_MAX) return;
		ScEvent& e = m_doc.ev[m_doc.evCount++];
		e.tick = 0; e.seq = (uint32_t)(m_doc.evCount - 1);
		e.ch = (uint8_t)m_curCh; e.kind = kind;
		e.a = a; e.b = b; e.c = 0; e.dur = 0;
		*found = m_doc.evCount - 1;
	};
	/* Bank before Prog so same-tick sort / SMF emit CC0/CC32 then PC. */
	putEv(&foundBank, SC_EV_BANK, (uint8_t)msb, (uint8_t)lsb);
	putEv(&foundProg, SC_EV_PROG, (uint8_t)prog, (uint8_t)prog);
	/* Keep seq order: bank then prog when both newly created. */
	if (foundBank >= 0 && foundProg >= 0 &&
		m_doc.ev[foundBank].tick == 0 && m_doc.ev[foundProg].tick == 0 &&
		m_doc.ev[foundBank].seq > m_doc.ev[foundProg].seq) {
		const uint32_t tmp = m_doc.ev[foundBank].seq;
		m_doc.ev[foundBank].seq = m_doc.ev[foundProg].seq;
		m_doc.ev[foundProg].seq = tmp;
	}
	RefreshProgLabels();
	InvalidateRect(m_trackRc, FALSE);
	if (m_bodyRc.Width() > 0)
		InvalidateRect(m_bodyRc, FALSE);
	CString st;
	st.Format(L"MIDI %d  Prog %d (PC#%d)  Bank %d/%d",
		m_curCh + 1, prog, prog + 1, msb, lsb);
	m_status.SetWindowText(st);
}

void CSasamiMidiScoreDlg::EditProgForPart(int ch0)
{
	if (ch0 < 0 || ch0 >= 32) return;
	m_curCh = ch0;
	if (m_ch.GetSafeHwnd()) m_ch.SetCurSel(m_curCh);
	SyncProgPropFromCh(ch0);
	m_status.SetWindowText(L"Prog edit: set Prog / MSB / LSB then Apply. Tone row opens VST.");
}

void CSasamiMidiScoreDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	if (capH > 0 && point.y >= 0 && point.y < capH) {
		CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
		return;
	}
	CRect client; GetClientRect(&client);
	/* Thumbs are painted in body-local space (see OnPaint) — hit-test the same way. */
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
	int zone = SC_GAUGE_NONE;
	int tr = ScStaffHitGauge(m_trackRc, &m_ui, point, &zone);
	if (tr < 0) tr = ScStaffHitTrack(m_trackRc, &m_ui, point);
	if (tr >= 0) {
		int mid = m_trackRc.left + 28;
		if (point.x < mid) {
			m_ui.visible[tr] = m_ui.visible[tr] ? 0 : 1;
			UpdateScrollBars();
		} else if (zone == SC_GAUGE_CLEF) {
			m_ui.clef[tr] = (m_ui.clef[tr] + 1) % 4; /* G -> F -> Grand -> Drum */
			m_curCh = tr;
			if (m_ch.GetSafeHwnd()) m_ch.SetCurSel(m_curCh);
			UpdateScrollBars();
			InvalidateRect(m_bodyRc, FALSE);
			static const wchar_t* kClefName[] = {
				L"Treble (G)", L"Bass (F)", L"Grand (G+F)", L"Drum/kit (C1=bottom)"
			};
			CString st;
			st.Format(L"MIDI %d clef: %s (click chip to cycle)", tr + 1, kClefName[m_ui.clef[tr] % 4]);
			m_status.SetWindowText(st);
		} else if (zone == SC_GAUGE_TONE) {
			m_curCh = tr;
			m_ch.SetCurSel(m_curCh);
			m_ui.visible[tr] = 1;
			OpenVstForPart(tr + 1);
		} else if (zone == SC_GAUGE_PROG) {
			m_curCh = tr;
			m_ch.SetCurSel(m_curCh);
			m_ui.visible[tr] = 1;
			CSasamiExcRpnDlg::OpenOwned(this, &m_doc, tr, m_ui.markerTick);
		} else {
			m_curCh = tr;
			m_ch.SetCurSel(m_curCh);
			m_ui.visible[tr] = 1;
			m_propMode = 0;
			RefreshStrip();
			SyncPropFromSel();
		}
		InvalidateRect(m_bodyRc, FALSE);
		return;
	}
	int lane = 0, col = 0, val = 0;
	if (ScStaffHitStrip(m_stripRc, &m_ui, point, &lane, &col, &val)) {
		if (m_ui.stripDraw == SC_STRIP_DRAW_LINE) {
			m_ui.stripLineAnchorCol = col;
			m_ui.stripLineAnchorVal = val;
			m_ui.stripLineLane = lane;
		}
		m_ui.strip[lane][col] = (uint8_t)val;
		m_ui.dragEv = -2000 - lane;
		InvalidateRect(m_stripRc, FALSE);
		return;
	}
	PlaceOrEditAt(point);
}

void CSasamiMidiScoreDlg::OnMouseMove(UINT nFlags, CPoint point)
{
	if ((nFlags & MK_LBUTTON) && m_ui.marqueeOn) {
		m_ui.marqueeX1 = point.x;
		m_ui.marqueeY1 = point.y;
		InvalidateRect(m_bodyRc, FALSE);
		return;
	}
	if ((nFlags & MK_LBUTTON) && m_ui.eraseDrag) {
		int hitTr = -1;
		int hit = ScStaffHitNote(m_gridRc, &m_ui, m_doc.ev, m_doc.evCount, 0, point, &hitTr);
		int markTr = -1;
		int mark = ScStaffHitStaffMark(m_gridRc, &m_ui, m_doc.ev, m_doc.evCount, 0, point, &markTr);
		int ctrlTr = -1;
		int ctrl = ScStaffHitScoreCtrl(m_gridRc, &m_ui, m_doc.ev, m_doc.evCount, 0, point, &ctrlTr);
		int del = (ctrl >= 0) ? ctrl : ((mark >= 0) ? mark : hit);
		if (del >= 0) {
			ScStaffSelSetPrimary(&m_ui, del);
			ScStaffSelDelete(m_doc.ev, &m_doc.evCount, &m_ui);
			PushDocToText();
			InvalidateRect(m_bodyRc, FALSE);
		}
		return;
	}
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
	const int onRuler = m_gridRc.PtInRect(point) && point.y < m_gridRc.top + SC_RULER_H;
	int gaugeZone = SC_GAUGE_NONE;
	const int onGauge = (ScStaffHitGauge(m_trackRc, &m_ui, point, &gaugeZone) >= 0 &&
		gaugeZone != SC_GAUGE_NONE);
	const int onCtrlStrip = ScStaffPtInScoreCtrlStrip(m_gridRc, &m_ui, point, NULL);
	if (onGauge || onCtrlStrip) {
		::SetCursor(::LoadCursor(NULL, IDC_HAND));
		m_ui.hoverValid = 0;
	} else if (onRuler) {
		::SetCursor(::LoadCursor(NULL, IDC_ARROW));
		m_ui.hoverValid = 0;
	} else if (m_gridRc.PtInRect(point) && m_blankCur &&
		(m_ui.tool == SC_TOOL_PENCIL || m_ui.tool == SC_TOOL_TEMPO))
		::SetCursor(m_blankCur);
	UpdateHover(point);
	UpdateHoverStatus(point);
	if (!(nFlags & MK_LBUTTON)) {
		if (m_ui.hoverValid)
			InvalidateRect(m_gridRc, FALSE);
		CCustomBlurDialogExBase::OnMouseMove(nFlags, point);
		return;
	}
	if (m_ui.dragEv <= -2000) {
		int dragLane = -(m_ui.dragEv + 2000);
		int lane = 0, col = 0, val = 0;
		if (ScStaffHitStrip(m_stripRc, &m_ui, point, &lane, &col, &val)) {
			if (m_ui.stripDraw == SC_STRIP_DRAW_LINE && m_ui.stripLineAnchorCol >= 0) {
				StripPaintLine(&m_ui, dragLane,
					m_ui.stripLineAnchorCol, m_ui.stripLineAnchorVal, col, val);
			} else {
				m_ui.strip[dragLane][col] = (uint8_t)val;
			}
			InvalidateRect(m_stripRc, FALSE);
		}
	} else if (m_ui.dragEv >= 0 && m_ui.dragEv < m_doc.evCount &&
		(m_ui.tool == SC_TOOL_SELECT || m_ui.tool == SC_TOOL_PENCIL)) {
		ScEvent& e = m_doc.ev[m_ui.dragEv];
		if (e.kind == SC_EV_NOTE) {
			if (m_ui.dragMode == 2) {
				/* move pitch + tick */
				int hitTr = (int)e.ch;
				int staffTop = ScStaffVisibleLaneStaffTop(m_gridRc, &m_ui, hitTr);
				if (staffTop >= 0) {
					const int gap = ScStaffLineGap(&m_ui);
					int note = ScStaffYToMidiNoteTrack(&m_ui, hitTr, staffTop, point.y);
					if (note < 0) note = 0;
					if (note > 127) note = 127;
					e.a = (uint8_t)note;
					const int quant = ScStaffPlaceQuant(&m_ui);
					e.tick = ScStaffXToTick(point.x, m_ui.scrollX, m_gridRc.left + SC_CLEF_MARGIN, m_ui.pxBeat, quant);
					SyncPropFromSel();
					InvalidateRect(m_gridRc, FALSE);
				}
			} else {
				/* resize duration (edge drag) */
				int dx = point.x - m_ui.dragOriginX;
				int dTicks = (dx * SC_PPQN) / max(1, m_ui.pxBeat);
				int nd = (int)e.dur + dTicks;
				const int minD = SC_PPQN / 8;
				if (nd < minD) nd = minD;
				if (nd > 32000) nd = 32000;
				e.dur = (uint16_t)nd;
				m_ui.dragOriginX = point.x;
				SyncPropFromSel();
				InvalidateRect(m_gridRc, FALSE);
			}
		}
	}
	CCustomBlurDialogExBase::OnMouseMove(nFlags, point);
}

BOOL CSasamiMidiScoreDlg::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
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

void CSasamiMidiScoreDlg::OnLButtonUp(UINT nFlags, CPoint point)
{
	m_sbDrag = 0;
	if (m_ui.marqueeOn) {
		m_ui.marqueeX1 = point.x;
		m_ui.marqueeY1 = point.y;
		CRect r(min(m_ui.marqueeX0, m_ui.marqueeX1), min(m_ui.marqueeY0, m_ui.marqueeY1),
			max(m_ui.marqueeX0, m_ui.marqueeX1), max(m_ui.marqueeY0, m_ui.marqueeY1));
		ScStaffSelClear(&m_ui);
		for (int i = 0; i < m_doc.evCount; i++) {
			const ScEvent& e = m_doc.ev[i];
			if (e.kind != SC_EV_NOTE && e.kind != SC_EV_REST) continue;
			int st = ScStaffVisibleLaneStaffTop(m_gridRc, &m_ui, e.ch);
			if (st < 0) continue;
			int x = ScStaffTickToX(e.tick, m_ui.scrollX, m_gridRc.left + SC_CLEF_MARGIN, m_ui.pxBeat);
			int y = (e.kind == SC_EV_REST) ? (st + 8) : ScStaffMidiNoteYTrack(&m_ui, e.ch, st, e.a);
			if (r.PtInRect(CPoint(x, y)))
				ScStaffSelAdd(&m_ui, i);
		}
		m_ui.marqueeOn = 0;
		InvalidateRect(m_bodyRc, FALSE);
	}
	m_ui.eraseDrag = 0;
	if (m_ui.dragEv <= -2000) {
		ScStaffApplyStripToDocMidi(&m_doc, m_curCh, &m_ui);
		m_ui.stripLineAnchorCol = -1;
		m_ui.dragEv = -1;
	} else {
		m_ui.dragEv = -1;
	}
	m_ui.dragMode = 0;
	ReleaseCapture();
	CCustomBlurDialogExBase::OnLButtonUp(nFlags, point);
}

void CSasamiMidiScoreDlg::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	if (capH > 0 && point.y >= 0 && point.y < capH) {
		CCustomBlurDialogExBase::OnLButtonDblClk(nFlags, point);
		return;
	}
	/* SSW-style: Prog/Bank chips on the score timeline (right), not left track list */
	int ctrlTr = -1;
	int ctrl = ScStaffHitScoreCtrl(m_gridRc, &m_ui, m_doc.ev, m_doc.evCount, 0, point, &ctrlTr);
	if (ctrl >= 0) {
		m_ui.selEv = ctrl;
		m_curCh = m_doc.ev[ctrl].ch;
		if (m_ch.GetSafeHwnd()) m_ch.SetCurSel(m_curCh);
		const ScEvent& e = m_doc.ev[ctrl];
		if (e.kind == SC_EV_PROG || e.kind == SC_EV_BANK) {
			/* Double-click = edit PC/Bank numbers; single-click opens tone/VST. */
			m_propMode = 1;
			EditProgForPart(m_curCh);
			m_status.SetWindowText(L"Prog/Bank 数値編集。音色/VSTはシングルクリックまたは左Tone行。");
			InvalidateRect(m_gridRc, FALSE);
			return;
		}
	}
	int hitTr = -1;
	int hit = ScStaffHitNote(m_gridRc, &m_ui, m_doc.ev, m_doc.evCount, 0, point, &hitTr);
	if (hit >= 0) {
		m_ui.selEv = hit;
		m_curCh = m_doc.ev[hit].ch;
		m_ch.SetCurSel(m_curCh);
		OpenNotePropsForSel();
	} else {
		int zone = SC_GAUGE_NONE;
		int tr = ScStaffHitGauge(m_trackRc, &m_ui, point, &zone);
		if (tr < 0) tr = ScStaffHitTrack(m_trackRc, &m_ui, point);
		if (tr >= 0) {
			/* Left list: Tone opens VST; Prog row is summary only (edit chips on score). */
			if (zone == SC_GAUGE_CLEF) {
				m_ui.clef[tr] = (m_ui.clef[tr] + 1) % 4;
				UpdateScrollBars();
				InvalidateRect(m_bodyRc, FALSE);
			} else if (zone == SC_GAUGE_TONE) OpenVstForPart(tr + 1);
			else if (zone == SC_GAUGE_PROG) {
				m_curCh = tr;
				m_ch.SetCurSel(m_curCh);
				CSasamiExcRpnDlg::OpenOwned(this, &m_doc, tr, m_ui.markerTick);
			} else
				OpenVstForPart(tr + 1);
		}
	}
	CCustomBlurDialogExBase::OnLButtonDblClk(nFlags, point);
}

BOOL CSasamiMidiScoreDlg::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
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
		CString s;
		s.Format(LL14(
			L"�g�� %d px/��  �i %d%%  Fit%s", L"Zoom %d px/beat  staff %d%%  Fit%s",
			L"Zoom %d px/temps  portee %d%%  Fit%s", L"Zoom %d px/batt  pentagramma %d%%  Fit%s",
			L"Zoom %d px/pulso  pentagrama %d%%  Fit%s", L"?? %d px/?  ?? %d%%  Fit%s",
			L"?�� %d px/��  ��? %d%%  Fit%s", L"????? %d px/???  staff %d%%  Fit%s",
			L"�M�p�������p�q %d px/�t���|��  staff %d%%  Fit%s", L"Zoom %d px/Schlag  staff %d%%  Fit%s",
			L"Zoom %d px/tempo  staff %d%%  Fit%s", L"Zoom %d px/maat  staff %d%%  Fit%s",
			L"Zoom %d px/beat  staff %d%%  Fit%s", L"Yak?nla?t?r %d px/vuru?  staff %d%%  Fit%s"),
			m_ui.pxBeat, m_ui.staffScale, m_ui.snapFit ? L"ON" : L"OFF");
		PersistUiGeom();
		m_status.SetWindowText(s);
		UpdateScrollBars();
	} else if (nFlags & MK_SHIFT) {
		m_ui.scrollX -= (zDelta / WHEEL_DELTA) * m_ui.pxBeat * 2;
		if (m_ui.scrollX < 0) m_ui.scrollX = 0;
		UpdateScrollBars();
	} else {
		/* default: vertical (Cubase/SSW-like); Shift = horizontal */
		m_ui.scrollY -= (zDelta / WHEEL_DELTA) * max(12, ScStaffH(&m_ui) / 2);
		if (m_ui.scrollY < 0) m_ui.scrollY = 0;
		UpdateScrollBars();
	}
	InvalidateRect(m_bodyRc, FALSE);
	return TRUE;
}

void CSasamiMidiScoreDlg::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
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
	/* Do not forward to base — parent GDI must not steal score scroll. */
}

void CSasamiMidiScoreDlg::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
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

void CSasamiMidiScoreDlg::OnCbnSelchangeCh()
{
	int s = m_ch.GetCurSel();
	if (s >= 0 && s < SC_MIDI_CH) {
		m_curCh = s;
		m_ui.visible[s] = 1;
		RefreshStrip();
		InvalidateRect(m_bodyRc, FALSE);
	}
}

void CSasamiMidiScoreDlg::OnBnClickedPencil()
{
	m_ui.tool = SC_TOOL_PENCIL;
	UpdateNoteCursor();
}

void CSasamiMidiScoreDlg::OnBnClickedErase()
{
	m_ui.tool = SC_TOOL_ERASER;
	UpdateNoteCursor();
}

void CSasamiMidiScoreDlg::OnBnClickedSel()
{
	m_ui.tool = SC_TOOL_SELECT;
	UpdateNoteCursor();
}

void CSasamiMidiScoreDlg::OnBnClickedPal()
{
	CRect r;
	m_btnPal.GetWindowRect(&r);
	CSasamiNotePaletteDlg::OpenNear(this, CPoint(r.left, r.bottom + 4));
}

LRESULT CSasamiMidiScoreDlg::OnPalDur(WPARAM w, LPARAM l)
{
	if ((l & SASAMI_PAL_CMD) == SASAMI_PAL_CMD) {
		switch ((int)(l & 0xFF)) {
		case SASAMI_PAL_CMD_FIT: {
			m_ui.snapFit ^= 1;
			CString s;
			s.Format(L"Grid fit %s", m_ui.snapFit ? L"ON" : L"OFF");
			m_status.SetWindowText(s);
			InvalidateRect(m_gridRc, FALSE);
			return 0;
		}
		case SASAMI_PAL_CMD_TEMPO:
			m_ui.tool = SC_TOOL_TEMPO;
			UpdateNoteCursor();
			m_status.SetWindowText(L"Tempo tool — click staff");
			return 0;
		case SASAMI_PAL_CMD_GT:
			m_edGt.SetFocus();
			return 0;
		case SASAMI_PAL_CMD_PENCIL: OnBnClickedPencil(); return 0;
		case SASAMI_PAL_CMD_ERASE: OnBnClickedErase(); return 0;
		case SASAMI_PAL_CMD_SEL: OnBnClickedSel(); return 0;
		case SASAMI_PAL_CMD_MARK: OnBnClickedMark(); return 0;
		case SASAMI_PAL_CMD_LOOP_A: OnBnClickedLoopA(); return 0;
		case SASAMI_PAL_CMD_LOOP_B: OnBnClickedLoopB(); return 0;
		case SASAMI_PAL_CMD_LOOP_CLR: OnBnClickedLoopClr(); return 0;
		case SASAMI_PAL_CMD_MARK_REPLACE:
			m_ui.markStack = 0;
			savedata.sasamiMarkStack = 0;
			m_status.SetWindowText(L"マーク配置: 1重（同じ位置は置換）");
			return 0;
		case SASAMI_PAL_CMD_MARK_STACK:
			m_ui.markStack = 1;
			savedata.sasamiMarkStack = 1;
			m_status.SetWindowText(L"マーク配置: ネスト（同じ位置に積み上げ）");
			return 0;
		case SASAMI_PAL_CMD_TIE:
			m_ui.tool = SC_TOOL_TIE;
			UpdateNoteCursor();
			m_status.SetWindowText(L"Tie tool");
			return 0;
		case SASAMI_PAL_CMD_LOOP_START:
		case SASAMI_PAL_CMD_LOOP_END:
		case SASAMI_PAL_CMD_PED_ON:
		case SASAMI_PAL_CMD_PED_OFF: {
			/* Palette is a separate window — hover stays stale. Insert at red bar. */
			const int ch = m_curCh;
			uint32_t atTick = m_ui.markerTick;
			int ok = 0;
			if ((int)(l & 0xFF) == SASAMI_PAL_CMD_LOOP_START) {
				int n = 2;
				if (CSasamiSimpleInputDlg::AskNumber(this, L"ループ開始 |:",
					L"繰り返し回数 (1–99)", 2, 1, 99, &n) != IDOK)
					return 0;
				ok = ScMidiAddLoopStart(&m_doc, atTick, ch, n, m_ui.markStack);
			} else if ((int)(l & 0xFF) == SASAMI_PAL_CMD_LOOP_END) {
				ok = ScMidiAddLoopEnd(&m_doc, atTick, ch, m_ui.markStack);
			} else if ((int)(l & 0xFF) == SASAMI_PAL_CMD_PED_ON) {
				ok = ScMidiAddPedalOn(&m_doc, atTick, ch, m_ui.markStack);
			} else {
				ok = ScMidiAddPedalOff(&m_doc, atTick, ch, m_ui.markStack);
			}
			if (ok) {
				m_ui.visible[ch] = 1;
				RefreshStrip();
				PushDocToText();
				InvalidateRect(m_bodyRc, FALSE);
				CString st;
				st.Format(L"MIDI %d mark @ %u (marker)", ch + 1, (unsigned)atTick);
				m_status.SetWindowText(st);
			}
			return 0;
		}
		default:
			return 0;
		}
	}
	m_ui.tool = SC_TOOL_PENCIL;
	m_placeRest = (int)(l & 1);
	m_ui.placeRest = m_placeRest;
	m_ui.dotted = (l & 2) ? 1 : 0;
	m_ui.tuplet = (int)((l >> 4) & 0xF);
	if (m_ui.tuplet != 3 && m_ui.tuplet != 5 && m_ui.tuplet != 6 && m_ui.tuplet != 8)
		m_ui.tuplet = 0;
	m_ui.triplet = (m_ui.tuplet == 3) ? 1 : 0;
	m_accidental = (int)(signed char)((l >> 8) & 0xFF);
	m_ui.placeAccidental = m_accidental;
	int base = (int)((l >> 16) & 0xFFFF);
	if (base > 0) m_ui.baseDur = base;
	ScStaffRecomputePlaceDur(&m_ui);
	/* wParam is authoritative final ticks from palette (guards recompute mismatch). */
	if ((int)w > 0) m_ui.placeDur = (int)w;
	if (m_ui.placeDur < 1) m_ui.placeDur = 1;
	UpdateNoteCursor();
	CString s;
	s.Format(L"Dur=%d%s%s%s%s Fit%s",
		m_ui.placeDur,
		m_placeRest ? L" rest" : L"",
		m_ui.dotted ? L" dot" : L"",
		m_ui.tuplet ? L" tup" : L"",
		m_accidental > 0 ? L" #" : (m_accidental < 0 ? L" b" : L""),
		m_ui.snapFit ? L"ON" : L"OFF");
	if (m_ui.tuplet) {
		CString t;
		t.Format(L"%d", m_ui.tuplet);
		s += L" ";
		s += t;
	}
	m_status.SetWindowText(s);
	InvalidateRect(m_gridRc, FALSE);
	return 0;
}

void CSasamiMidiScoreDlg::OnBnClickedPropUpd()
{
	if (m_propMode == 1) {
		ApplyProgPropToCh();
		return;
	}
	if (m_ui.selEv < 0 || m_ui.selEv >= m_doc.evCount) return;
	ScEvent& e = m_doc.ev[m_ui.selEv];
	if (e.kind != SC_EV_NOTE) return;
	CString sn, sg, sv;
	m_edNote.GetWindowText(sn);
	m_edGt.GetWindowText(sg);
	m_edVel.GetWindowText(sv);
	int note = _wtoi(sn);
	int gate = _wtoi(sg);
	int vel = _wtoi(sv);
	if (note < 0) note = 0;
	if (note > 127) note = 127;
	if (gate < 1) gate = 1;
	if (gate > 100) gate = 100;
	if (vel < 1) vel = 1;
	if (vel > 127) vel = 127;
	e.a = (uint8_t)note;
	e.b = (uint8_t)vel;
	e.c = (uint8_t)gate;
	InvalidateRect(m_gridRc, FALSE);
}

void CSasamiMidiScoreDlg::OnBnClickedTempo()
{
	int bpm = (int)((13000.0 * 120.0) / (double)max(1, m_doc.tempoT) + 0.5);
	static const int kBpm[] = { 80, 100, 120, 140, 160, 180 };
	int ni = 0;
	for (int i = 0; i < 6; i++) {
		if (kBpm[i] == bpm) { ni = (i + 1) % 6; break; }
		if (kBpm[i] > bpm) { ni = i; break; }
	}
	bpm = kBpm[ni];
	m_doc.tempoT = (int)((13000.0 * 120.0) / (double)bpm + 0.5);
	m_ui.tool = SC_TOOL_TEMPO;
	UpdateNoteCursor();
	CString s;
	s.Format(L"BPM %d (T=%d) — click staff to place tempo", bpm, m_doc.tempoT);
	m_status.SetWindowText(s);
	InvalidateRect(m_gridRc, FALSE);
}

int CSasamiMidiScoreDlg::BuildToTemp(wchar_t* outPath, int outCch)
{
	SyncVstBindsFromLive();
	int anySolo = 0;
	for (int i = 0; i < m_ui.trackCount; i++) if (m_ui.solo[i]) { anySolo = 1; break; }
	ScMidiDoc* tmp = (ScMidiDoc*)HeapAlloc(GetProcessHeap(), 0, sizeof(ScMidiDoc));
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

	/* Do NOT VstLiveCaptureStates here — pipe GET_STATE while Host64 is busy
	   (SampleTank soft-hide / multi-load) crashes on 再生確認/保存. States must
	   already be in m_doc.bind from editor-close snap. */

	SasamiWriteMidi* wr = (SasamiWriteMidi*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SasamiWriteMidi));
	if (!wr) { HeapFree(GetProcessHeap(), 0, tmp); return 0; }
	if (!ScMidiDocToWrite(tmp, wr)) {
		HeapFree(GetProcessHeap(), 0, wr);
		HeapFree(GetProcessHeap(), 0, tmp);
		const wchar_t* why = ScGetLastWriteErr();
		m_status.SetWindowText(why && why[0] ? why : L"Build failed");
		return 0;
	}
	HeapFree(GetProcessHeap(), 0, tmp);
	/* Any VST path forces .mpsmv trailer so preview/host load HALion etc. */
	for (int i = 0; i < 32; i++) {
		if (m_doc.bind.vstPath[i][0]) {
			wr->isMpw3 = 1;
			wcsncpy_s(wr->vstPath[i], m_doc.bind.vstPath[i], _TRUNCATE);
			wr->vstProg[i] = m_doc.bind.vstProg[i];
			wr->vstBankMsb[i] = m_doc.bind.vstBankMsb[i];
			wr->vstBankLsb[i] = m_doc.bind.vstBankLsb[i];
			wr->vstForceCh[i] = m_doc.bind.vstForceCh[i];
			if (m_doc.bind.vstCompLen[i] && m_doc.bind.vstComp[i])
				SasamiVstBlobSet(&wr->vstComp[i], &wr->vstCompLen[i],
					m_doc.bind.vstComp[i], m_doc.bind.vstCompLen[i]);
			if (m_doc.bind.vstCtrlLen[i] && m_doc.bind.vstCtrl[i])
				SasamiVstBlobSet(&wr->vstCtrl[i], &wr->vstCtrlLen[i],
					m_doc.bind.vstCtrl[i], m_doc.bind.vstCtrlLen[i]);
		}
	}
	if (m_doc.bind.isMpw3) wr->isMpw3 = 1;
	uint8_t* bin = (uint8_t*)HeapAlloc(GetProcessHeap(), 0, SASAMI_WRITE_MAX);
	if (!bin) { SasamiWriteMidiClear(wr); HeapFree(GetProcessHeap(), 0, wr); return 0; }
	uint32_t sz = 0;
	if (wr->isMpw3)
		sz = SasamiBuildMpw3(wr, bin, SASAMI_WRITE_MAX);
	else
		sz = SasamiBuildMpw2(wr, bin, SASAMI_WRITE_MAX);
	const int outMpsmv = wr->isMpw3 ? 1 : 0;
	SasamiWriteMidiClear(wr); HeapFree(GetProcessHeap(), 0, wr);
	if (!sz) {
		HeapFree(GetProcessHeap(), 0, bin);
		m_status.SetWindowText(L"Output size 0");
		return 0;
	}
	wchar_t dir[MAX_PATH];
	GetTempPathW(MAX_PATH, dir);
	_snwprintf_s(outPath, outCch, _TRUNCATE, L"%sogg_sasami_score.%s", dir, outMpsmv ? L"mpsmv" : L"mpw2");
	if (!SasamiWriteFileW(outPath, bin, sz)) {
		HeapFree(GetProcessHeap(), 0, bin);
		m_status.SetWindowText(L"Write failed");
		return 0;
	}
	wcsncpy_s(m_lastOut, outPath, _TRUNCATE);
	HeapFree(GetProcessHeap(), 0, bin);
	/* Sidecar .mid next to temp so ResolvePlayPath can fall back if convert fails. */
	{
		wchar_t midTmp[MAX_PATH]; midTmp[0] = 0;
		if (SasamiConvertPathToMidiFile(outPath, midTmp, MAX_PATH) && midTmp[0]) {
			wchar_t side[MAX_PATH];
			wcsncpy_s(side, outPath, _TRUNCATE);
			wchar_t* d = wcsrchr(side, L'.');
			if (d) {
				wcscpy_s(d, MAX_PATH - (int)(d - side), L".mid");
				CopyFileW(midTmp, side, FALSE);
			}
		}
	}
	CString ok;
	ok.Format(L"OK %u bytes -> %s", sz, outPath);
	m_status.SetWindowText(ok);
	return 1;
}

void CSasamiMidiScoreDlg::OnBnClickedSave()
{
	wchar_t path[MAX_PATH];
	if (!BuildToTemp(path, MAX_PATH)) return;
	int mpw3 = m_doc.bind.isMpw3;
	CFileDialog dlg(FALSE, mpw3 ? L"mpsmv" : L"mpw2",
		mpw3 ? L"song.mpsmv" : L"song.mpw2",
		OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
		L"MPSMV/MPW2 (*.mpsmv;*.mpw2)|*.mpsmv;*.mpw2|MPY (*.mpy)|*.mpy||", this);
	if (dlg.DoModal() != IDOK) return;
	CString dest = dlg.GetPathName();
	if (dest.Right(4).CompareNoCase(L".mpy") == 0) {
		SasamiWriteMidi* wr = (SasamiWriteMidi*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SasamiWriteMidi));
		if (!wr) return;
		if (!ScMidiDocToWrite(&m_doc, wr)) { HeapFree(GetProcessHeap(), 0, wr); return; }
		uint8_t* bin = (uint8_t*)HeapAlloc(GetProcessHeap(), 0, SASAMI_WRITE_MAX);
		if (!bin) { SasamiWriteMidiClear(wr); HeapFree(GetProcessHeap(), 0, wr); return; }
		uint32_t sz = SasamiBuildMpy(wr, bin, SASAMI_WRITE_MAX);
		if (sz) SasamiWriteFileW(dest, bin, sz);
		HeapFree(GetProcessHeap(), 0, bin);
		SasamiWriteMidiClear(wr); HeapFree(GetProcessHeap(), 0, wr);
	} else {
		CopyFileW(path, dest, FALSE);
	}
	m_status.SetWindowText(dest);
	PersistSession();
	PushDocToText();
}

void CSasamiMidiScoreDlg::OnBnClickedNew()
{
	NewDocument();
}

void CSasamiMidiScoreDlg::SyncMeterFromDoc()
{
	ScStaffSetMeter(&m_ui, m_doc.numer > 0 ? m_doc.numer : 4, m_doc.denom > 0 ? m_doc.denom : 4);
}

void CSasamiMidiScoreDlg::LoadFromDoc(const ScMidiDoc& src)
{
	/* Deep-copy bind heaps — operator= would alias malloc'd state blobs. */
	ScMidiVstBindFreeStates(&m_doc.bind);
	ScMidiFxBindFreeStates(&m_doc.fxBind);
	m_doc = src;
	memset(&m_doc.bind.vstComp, 0, sizeof(m_doc.bind.vstComp));
	memset(&m_doc.bind.vstCtrl, 0, sizeof(m_doc.bind.vstCtrl));
	memset(&m_doc.bind.vstCompLen, 0, sizeof(m_doc.bind.vstCompLen));
	memset(&m_doc.bind.vstCtrlLen, 0, sizeof(m_doc.bind.vstCtrlLen));
	memset(&m_doc.fxBind.fxState, 0, sizeof(m_doc.fxBind.fxState));
	memset(&m_doc.fxBind.fxStateLen, 0, sizeof(m_doc.fxBind.fxStateLen));
	for (int i = 0; i < 32; i++) {
		if (src.bind.vstCompLen[i] && src.bind.vstComp[i])
			SasamiVstBlobSet(&m_doc.bind.vstComp[i], &m_doc.bind.vstCompLen[i],
				src.bind.vstComp[i], src.bind.vstCompLen[i]);
		if (src.bind.vstCtrlLen[i] && src.bind.vstCtrl[i])
			SasamiVstBlobSet(&m_doc.bind.vstCtrl[i], &m_doc.bind.vstCtrlLen[i],
				src.bind.vstCtrl[i], src.bind.vstCtrlLen[i]);
		for (int sl = 0; sl < ScMidiFxBind::SC_FX_SLOTS; ++sl) {
			if (src.fxBind.fxStateLen[i][sl] && src.fxBind.fxState[i][sl])
				SasamiVstBlobSet(&m_doc.fxBind.fxState[i][sl], &m_doc.fxBind.fxStateLen[i][sl],
					src.fxBind.fxState[i][sl], src.fxBind.fxStateLen[i][sl]);
		}
	}
	SyncFxBindsToLive();
	SyncMeterFromDoc();
	RefreshProgLabels();
	ScStaffEnsureStripFromDoc(&m_ui, m_doc.ev, m_doc.evCount, m_curCh);
	ScStaffUpdateContentExtent(&m_ui, m_doc.ev, m_doc.evCount);
	UpdateScrollBars();
	if (m_bodyRc.Width() > 0)
		InvalidateRect(m_bodyRc, FALSE);
}

void CSasamiMidiScoreDlg::PushDocToText()
{
	/* No text window → nothing to sync (tone-map OK must not allocate 1MB MML). */
	CSasamiTextDlg* t = CSasamiTextDlg::Instance();
	if (!t || !::IsWindow(t->GetSafeHwnd())) return;
	const int mmlCch = (int)(SC_TEXT_MAX / sizeof(wchar_t));
	wchar_t* mml = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		(SIZE_T)mmlCch * sizeof(wchar_t));
	if (!mml) return;
	if (!ScMidiDocToMml(&m_doc, mml, mmlCch)) {
		HeapFree(GetProcessHeap(), 0, mml);
		return;
	}
	t->SetMmlFromScore(mml);
	HeapFree(GetProcessHeap(), 0, mml);
}

void CSasamiMidiScoreDlg::PullDocFromText()
{
	CSasamiTextDlg* t = CSasamiTextDlg::Instance();
	if (!t || !::IsWindow(t->GetSafeHwnd())) return;
	CString text = t->GetMmlText();
	if (text.IsEmpty()) return;
	ScMidiDoc* tmp = (ScMidiDoc*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ScMidiDoc));
	if (!tmp) return;
	wchar_t err[128];
	int errLine = 0;
	if (!ScCompileMidiMml(text, tmp, &errLine, err, 128)) {
		HeapFree(GetProcessHeap(), 0, tmp);
		m_status.SetWindowText(L"Text compile failed — score left unchanged");
		return;
	}
	LoadFromDoc(*tmp);
	/* LoadFromDoc copies events; free bind heaps owned by tmp. */
	ScMidiDocClear(tmp);
	HeapFree(GetProcessHeap(), 0, tmp);
	m_status.SetWindowText(L"Score synced from text");
}

void CSasamiMidiScoreDlg::NewDocument()
{
	ScMidiDocClear(&m_doc);
	m_doc.tempoT = 13000;
	m_ui.selEv = -1;
	SyncMeterFromDoc();
	RefreshProgLabels();
	ScStaffUpdateContentExtent(&m_ui, m_doc.ev, m_doc.evCount);
	UpdateScrollBars();
	ScSessionClearLast();
	if (CSasamiTextDlg* t = CSasamiTextDlg::Instance()) {
		if (::IsWindow(t->GetSafeHwnd()))
			t->SetMmlFromScore(L"; SASAMI MML / MML3\r\n@METER 4/4\r\n#1\r\nt120\r\nl4 o4\r\n");
	}
	m_status.SetWindowText(L"New document");
	if (m_bodyRc.Width() > 0)
		InvalidateRect(m_bodyRc, FALSE);
}

void CSasamiMidiScoreDlg::PersistUiGeom()
{
	ScSaveWndGeom(this, &savedata.sasamiMidiX, &savedata.sasamiMidiY,
		&savedata.sasamiMidiW, &savedata.sasamiMidiH);
	savedata.sasamiMidiPxBeat = m_ui.pxBeat;
	savedata.sasamiMidiStaffScale = m_ui.staffScale;
	savedata.sasamiMidiScrollX = m_ui.scrollX;
	savedata.sasamiMidiScrollY = m_ui.scrollY;
}

void CSasamiMidiScoreDlg::RestoreUiGeom()
{
	if (savedata.sasamiMidiPxBeat >= SC_PX_BEAT_MIN && savedata.sasamiMidiPxBeat <= SC_PX_BEAT_MAX)
		m_ui.pxBeat = savedata.sasamiMidiPxBeat;
	if (savedata.sasamiMidiStaffScale >= SC_STAFF_SCALE_MIN && savedata.sasamiMidiStaffScale <= SC_STAFF_SCALE_MAX)
		m_ui.staffScale = savedata.sasamiMidiStaffScale;
	if (savedata.sasamiMidiScrollX >= 0)
		m_ui.scrollX = savedata.sasamiMidiScrollX;
	if (savedata.sasamiMidiScrollY >= 0)
		m_ui.scrollY = savedata.sasamiMidiScrollY;
	m_ui.markStack = savedata.sasamiMarkStack ? 1 : 0;
	if (!ScRestoreWndGeom(this, savedata.sasamiMidiX, savedata.sasamiMidiY,
		savedata.sasamiMidiW, savedata.sasamiMidiH, 720, 420))
		SetWindowPos(NULL, 0, 0, 1100, 720, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	LayoutChrome();
	UpdateScrollBars();
}

void CSasamiMidiScoreDlg::PersistSession()
{
	PersistUiGeom();
	const int mmlCch = (int)(SC_TEXT_MAX / sizeof(wchar_t));
	wchar_t* mml = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		(SIZE_T)mmlCch * sizeof(wchar_t));
	if (!mml) return;
	if (CSasamiTextDlg* t = CSasamiTextDlg::Instance()) {
		if (::IsWindow(t->GetSafeHwnd())) {
			CString s = t->GetMmlText();
			if (!s.IsEmpty())
				wcsncpy_s(mml, mmlCch, s, _TRUNCATE);
		}
	}
	if (!mml[0])
		ScMidiDocToMml(&m_doc, mml, mmlCch);
	ScSessionSaveLastMidi(&m_doc, mml[0] ? mml : NULL);
	HeapFree(GetProcessHeap(), 0, mml);
}

void CSasamiMidiScoreDlg::OnBnClickedOpen()
{
	CFileDialog dlg(TRUE, L"mpsmv", NULL, OFN_FILEMUSTEXIST,
		L"MPSMV/MPW2 (*.mpsmv;*.mpw2)|*.mpsmv;*.mpw2|All|*.*||", this);
	if (dlg.DoModal() != IDOK) return;
	const CString path = dlg.GetPathName();
	SasamiSong song;
	SasamiSongInit(&song);
	if (!SasamiLoadFileW(path, &song)) {
		SasamiSongFree(&song);
		m_status.SetWindowText(L"Open failed");
		return;
	}
	int trailerN = 0;
	ScMidiVstBindFreeStates(&m_doc.bind);
	if (song.hasMpw3Trailer) {
		m_doc.bind.isMpw3 = 1;
		for (int i = 0; i < 32; i++) {
			wcsncpy_s(m_doc.bind.vstPath[i], song.vstPath[i], _TRUNCATE);
			m_doc.bind.vstProg[i] = song.vstProg[i];
			m_doc.bind.vstBankMsb[i] = song.vstBankMsb[i];
			m_doc.bind.vstBankLsb[i] = song.vstBankLsb[i];
			m_doc.bind.vstForceCh[i] = song.vstForceCh[i];
			if (song.vstCompLen[i] && song.vstComp[i])
				SasamiVstBlobSet(&m_doc.bind.vstComp[i], &m_doc.bind.vstCompLen[i],
					song.vstComp[i], song.vstCompLen[i]);
			if (song.vstCtrlLen[i] && song.vstCtrl[i])
				SasamiVstBlobSet(&m_doc.bind.vstCtrl[i], &m_doc.bind.vstCtrlLen[i],
					song.vstCtrl[i], song.vstCtrlLen[i]);
			if (song.vstPath[i][0]) ++trailerN;
		}
	}
	if (song.titleSjis[0])
		memcpy(m_doc.titleSjis, song.titleSjis, sizeof(m_doc.titleSjis));
	SasamiSongFree(&song);

	/* Inject VST binds from .mpsmv trailer (load + state / SET_PROGRAM). */
	const int applied = VstApplyMpw3Binds(path, 1);
	for (int i = 0; i < 32; i++) {
		if (m_doc.bind.vstPath[i][0])
			ScMidiSetVstLabel(&m_ui, i, m_doc.bind.vstPath[i]);
	}
	RefreshProgLabels();
	InvalidateRect(m_trackRc, FALSE);

	/* HALion Home (Single/Multi) is UI-only — API PC list is empty slots until
	   the user picks Single once (and ideally checks Don't Show Home). */
	int halionPart = 0;
	for (int i = 0; i < 32; i++) {
		const wchar_t* p = m_doc.bind.vstPath[i];
		if (!p[0]) continue;
		if (wcsstr(p, L"HALion") && !wcsstr(p, L"Sonic") && VstLivePartIsLoaded(i + 1)) {
			halionPart = i + 1;
			break;
		}
	}
	CString st;
	if (applied > 0 && halionPart > 0) {
		st.Format(L"Opened %s — %d VST bind(s). HALion: editor→Single Instrument "
			L"(check Don't Show Home), then MediaBay.", (LPCTSTR)path, applied);
		VstLiveEditorOpenAsync(halionPart);
	} else if (applied > 0) {
		st.Format(L"Opened %s — injected %d VST bind(s)", (LPCTSTR)path, applied);
	} else if (trailerN > 0) {
		st.Format(L"Opened %s — trailer has %d path(s) but load failed", (LPCTSTR)path, trailerN);
	} else {
		st.Format(L"Opened %s — no MPW3 VST trailer (notes: Text composer / new on staff)",
			(LPCTSTR)path);
	}
	m_status.SetWindowText(st);
}

void CSasamiMidiScoreDlg::OnBnClickedPlay()
{
	/* Song playback and live-monitor both drain Host64 SHM — stop monitor first
	   or preview is silent/jerky and editors go mute. */
	VstLiveMonitorStop();
	SyncFxBindsToLive();
	/* Bake expression/volume strips into the doc before export. */
	for (int i = 0; i < m_ui.trackCount; i++)
		ScStaffApplyStripToDocMidi(&m_doc, i, &m_ui);
	wchar_t path[MAX_PATH];
	if (!BuildToTemp(path, MAX_PATH)) {
		if (VstLivePartIsLoaded(1) || VstLiveAnyRemotePart())
			VstLiveMonitorEnsure();
		return;
	}
	/* Load VST3 binds before host open; host also reapplies for .mpsmv */
	MmBindVstActiveSlot();
	const int nBind = VstApplyMpw3Binds(path, 0);
	for (int i = 0; i < 32; i++) {
		if (m_doc.bind.vstPath[i][0])
			ScMidiSetVstLabel(&m_ui, i, m_doc.bind.vstPath[i]);
	}
	InvalidateRect(m_trackRc, FALSE);
	if (!pl) {
		m_status.SetWindowText(L"Playlist not ready");
		VstLiveMonitorEnsure();
		return;
	}
	m_ui.previewActive = 1;
	m_ui.playheadTick = m_ui.markerTick;
	m_ui.markerSeekArmed = (m_ui.markerTick > 0) ? 1 : 0;
	if (!ScStaffStartHostPreview(path, &m_ui, m_doc.tempoT)) {
		m_ui.previewActive = 0;
		m_status.SetWindowText(L"Failed to start preview");
		VstLiveMonitorEnsure();
		return;
	}
	/* Host prepare can reset live slots — re-apply binds after restart */
	MmBindVstActiveSlot();
	const int nBind2 = VstApplyMpw3Binds(path, 0);
	VstLiveMonitorStop(); /* play() may have re-armed via editor-closed UI */
	PushDocToText();
	SetTimer(kScPreviewTimer, 33, NULL);
	CString st;
	const unsigned stBytes = (m_doc.bind.vstCompLen[0] + m_doc.bind.vstCtrlLen[0]);
	if (stBytes > 0)
		st.Format(L"Preview: %d VST bind(s), state %u bytes in score/text",
			nBind > 0 ? nBind : nBind2, stBytes);
	else
		st.Format(L"Preview: %d VST bind(s). No @VSTSTATEB64 yet — close editor after picking a patch.",
			nBind > 0 ? nBind : nBind2);
	m_status.SetWindowText(st);
	InvalidateRect(m_bodyRc, FALSE);
}

void CSasamiMidiScoreDlg::OnBnClickedMark()
{
	m_ui.transportMode = 1;
	m_status.SetWindowText(L"Click ruler = play-from marker");
}

void CSasamiMidiScoreDlg::OnBnClickedLoopA()
{
	m_ui.transportMode = 2;
	m_status.SetWindowText(L"Click ruler for loop A");
}

void CSasamiMidiScoreDlg::OnBnClickedLoopB()
{
	m_ui.transportMode = 3;
	m_status.SetWindowText(L"Click ruler for loop B");
}

void CSasamiMidiScoreDlg::OnBnClickedLoopClr()
{
	m_ui.loopATick = -1;
	m_ui.loopBTick = -1;
	m_ui.transportMode = 0;
	InvalidateRect(m_bodyRc, FALSE);
}

void CSasamiMidiScoreDlg::OnBnClickedShowAll()
{
	for (int i = 0; i < m_ui.trackCount; i++)
		m_ui.visible[i] = 1;
	UpdateScrollBars();
	InvalidateRect(m_bodyRc, FALSE);
	m_status.SetWindowText(L"Showing all MIDI 1-32");
}

void CSasamiMidiScoreDlg::OnBnClickedFx()
{
	int part = m_curCh + 1;
	if (m_curCh >= 0 && m_curCh < 32 && m_doc.trackPart[m_curCh] != 0xFF)
		part = (int)m_doc.trackPart[m_curCh] + 1;
	if (part < 1) part = 1;
	if (part > 32) part = 32;
	SyncFxBindsToLive();
	CSasamiInsertFxDlg::OpenOwned(this, &m_doc, part);
}

void CSasamiMidiScoreDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == kScPreviewTimer) {
		if (ScStaffSyncPreviewPlayhead(&m_ui, m_doc.tempoT)) {
			UpdateScrollBars();
			InvalidateRect(m_bodyRc, FALSE);
		}
		/* Preview finished — return SHM drain to editor monitor. */
		if (m_ui.previewActive) {
			extern int endflg;
			extern int gameon;
			if (endflg || !gameon) {
				m_ui.previewActive = 0;
				KillTimer(kScPreviewTimer);
				VstLiveMonitorEnsure();
			}
		}
		return;
	}
	if (nIDEvent == 9122) {
		VstLivePollRemoteEditorClosed();
		return;
	}
	if (nIDEvent == 9123) {
		KillTimer(9123);
		const int part = m_pendingEdOpenPart;
		m_pendingEdOpenPart = 0;
		if (part >= 1 && part <= 32)
			PostMessage(WM_APP + 7204, (WPARAM)part, 1);
		return;
	}
	if (nIDEvent == 9124) {
		KillTimer(9124);
		PushDocToText();
		return;
	}
	if (nIDEvent == 9121) {
		KillTimer(9121);
		const int part = m_pendingEdClosePart;
		const int prog = m_pendingEdCloseProg;
		m_pendingEdClosePart = 0;
		if (part >= 1 && part <= 32) {
			unsigned char* comp = NULL; int compLen = 0;
			unsigned char* ctrl = NULL; int ctrlLen = 0;
			const int got = VstLiveCaptureStates(part, &comp, &compLen, &ctrl, &ctrlLen);
			if (got) {
				ScMidiVstBindSetState(&m_doc.bind, part - 1, comp, (uint32_t)compLen,
					ctrl, (uint32_t)ctrlLen);
				m_doc.bind.isMpw3 = 1;
				PushDocToText();
				CString st;
				st.Format(L"VST editor closed — part %d: state → score/text (%u+%u bytes)",
					part, (unsigned)m_doc.bind.vstCompLen[part - 1],
					(unsigned)m_doc.bind.vstCtrlLen[part - 1]);
				m_status.SetWindowText(st);
			} else {
				CString st;
				wchar_t path[520]; path[0] = 0;
				VstLivePartGetPath(part, path, 520);
				if (wcsstr(path, L"SampleTank") || wcsstr(path, L"Sample Tank") ||
					wcsstr(path, L"Kontakt"))
					st.Format(L"VST editor closed — part %d SampleTank/Kontakt (no state chunk yet)",
						part);
				else if (prog >= 0)
					st.Format(L"VST editor closed — part %d prog %d (no state chunk)", part, prog);
				else
					st.Format(L"VST editor closed — part %d (pick a patch in the editor, then close)", part);
				m_status.SetWindowText(st);
				PushDocToText();
			}
			if (comp) free(comp);
			if (ctrl) free(ctrl);
		}
		PostMessage(WM_VST_LIVE_EDITOR_CLOSED_UI, (WPARAM)part, (LPARAM)prog);
		return;
	}
	CCustomBlurDialogExBase::OnTimer(nIDEvent);
}

LRESULT CSasamiMidiScoreDlg::OnNoteProps(WPARAM w, LPARAM l)
{
	if (w == 1) {
		SyncPropFromSel();
		InvalidateRect(m_bodyRc, FALSE);
	} else if (w == 2 && l >= 1 && l <= 32) {
		wchar_t path[520];
		path[0] = 0;
		if (!VstLivePartGetPath((int)l, path, 520) || !path[0])
			wcsncpy_s(path, m_doc.bind.vstPath[(int)l - 1], _TRUNCATE);
		if (path[0]) {
			wcsncpy_s(m_doc.bind.vstPath[(int)l - 1], path, _TRUNCATE);
			ScMidiSetVstLabel(&m_ui, (int)l - 1, path);
		} else {
			m_ui.vstLabel[(int)l - 1][0] = 0;
			m_doc.bind.vstPath[(int)l - 1][0] = 0;
		}
		m_doc.bind.isMpw3 = 1;
		if (VstLivePartIsLoaded((int)l)) {
			m_doc.bind.vstForceCh[(int)l - 1] = VstLiveSendChannel((int)l);
			/* Multi/remote (SC-VA VST2): never ProgramCurrent — Host64 PROGRAMS
			   IPC freezes. Keep bind prog from tone map / score. */
			if (!VstLivePartIsMulti((int)l) && path[0] &&
				!VstDetectMultiTimbral(path)) {
				const int cur = VstLiveProgramCurrent((int)l);
				if (cur >= 0)
					m_doc.bind.vstProg[(int)l - 1] = cur;
			}
		}
		RefreshProgLabels();
		InvalidateRect(m_trackRc, FALSE);
	} else if (w == 4 && l >= 1 && l <= 32) {
		/* From note-props VST: Assign (tone map / editor). NotifyVstResult inside. */
		OpenVstForPart((int)l, 0);
	}
	return 0;
}

LRESULT CSasamiMidiScoreDlg::OnExcRpnChanged(WPARAM, LPARAM)
{
	RefreshStrip();
	ScStaffUpdateContentExtent(&m_ui, m_doc.ev, m_doc.evCount);
	InvalidateRect(m_bodyRc, FALSE);
	PushDocToText();
	PersistSession();
	m_status.SetWindowText(L"Inserted Exc/RPN event");
	return 0;
}

LRESULT CSasamiMidiScoreDlg::OnInsertFxChanged(WPARAM, LPARAM)
{
	SyncFxBindsToLive();
	PushDocToText();
	PersistSession();
	m_status.SetWindowText(L"Insert FX updated");
	return 0;
}

void CSasamiMidiScoreDlg::OnBnClickedHelp()
{
	OfflineHelpOpenTopic(m_hWnd, L"sasami-composer");
}

void CSasamiMidiScoreDlg::OnBnClickedExport()
{
	wchar_t path[MAX_PATH];
	if (!BuildToTemp(path, MAX_PATH)) return;
	ScOpenAudioExport(this, path);
}

void CSasamiMidiScoreDlg::OnContextMenu(CWnd* pWnd, CPoint point)
{
	CPoint client = point;
	ScreenToClient(&client);
	/* 左トラック列 → 譜面五線（音符／空き／Prog帯）の順で、右クリック位置のパートを取る */
	int tr = ScStaffHitTrack(m_trackRc, &m_ui, client);
	if (tr < 0) {
		int hitTr = -1;
		ScStaffHitNote(m_gridRc, &m_ui, m_doc.ev, m_doc.evCount, 0, client, &hitTr);
		if (hitTr >= 0)
			tr = hitTr;
		else if (ScStaffPtInScoreCtrlStrip(m_gridRc, &m_ui, client, &hitTr) && hitTr >= 0)
			tr = hitTr;
	}
	int markEv = -1;
	int delMarks[16];
	int delN = 0;
	{
		int mtr = -1;
		const int ctrl = ScStaffHitScoreCtrl(m_gridRc, &m_ui, m_doc.ev, m_doc.evCount, 0, client, &mtr);
		if (ctrl >= 0 && ctrl < m_doc.evCount) {
			const uint8_t k = m_doc.ev[ctrl].kind;
			if (ScStaffIsStaffMarkKind(k, 0)
				|| k == SC_EV_PROG || k == SC_EV_BANK || k == SC_EV_RPN || k == SC_EV_NRPN
				|| k == SC_EV_SYSEX || k == SC_EV_VOL || k == SC_EV_PAN || k == SC_EV_VELO)
				markEv = ctrl;
			if (tr < 0 && mtr >= 0) tr = mtr;
		}
		delN = ScStaffCollectStaffMarksAt(m_gridRc, &m_ui, m_doc.ev, m_doc.evCount, 0, client, delMarks, 16);
		if (delN > 0) {
			markEv = delMarks[delN - 1]; /* top of stack */
			if (tr < 0) tr = (int)m_doc.ev[markEv].ch;
		} else if (markEv < 0) {
			const int sm = ScStaffHitStaffMark(m_gridRc, &m_ui, m_doc.ev, m_doc.evCount, 0, client, &mtr);
			if (sm >= 0 && sm < m_doc.evCount) {
				markEv = sm;
				if (tr < 0 && mtr >= 0) tr = mtr;
			}
		}
	}
	const int pxBeat = m_ui.pxBeat > 0 ? m_ui.pxBeat : SC_PX_BEAT_DEFAULT;
	const int quant = ScStaffPlaceQuant(&m_ui);
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
		menu.AddCommand(9011, LL14(
			L"Exc / RPN / SysEx…", L"Exc / RPN / SysEx…", L"Exc / RPN / SysEx…", L"Exc / RPN / SysEx…", L"Exc / RPN / SysEx…",
			L"Exc / RPN / SysEx…", L"Exc / RPN / SysEx…", L"Exc / RPN / SysEx…", L"Exc / RPN / SysEx…", L"Exc / RPN / SysEx…",
			L"Exc / RPN / SysEx…", L"Exc / RPN / SysEx…", L"Exc / RPN / SysEx…", L"Exc / RPN / SysEx…"));
		menu.AddCommand(9012, LL14(
			L"Insert FX（VSTエフェクト）…", L"Insert FX (VST effect)…", L"Insert FX…", L"Insert FX…", L"Insert FX…",
			L"Insert FX…", L"Insert FX…", L"Insert FX…", L"Insert FX…", L"Insert FX…",
			L"Insert FX…", L"Insert FX…", L"Insert FX…", L"Insert FX…"));
		menu.AddSeparator();
		menu.AddCommand(9020, LL14(
			L"ループ開始 (|:n)…", L"Loop start (|:n)…", L"Début de boucle (|:n)…", L"Inizio loop (|:n)…", L"Inicio de bucle (|:n)…",
			L"루프 시작 (|:n)…", L"循环开始 (|:n)…", L"بداية الحلقة (|:n)…", L"Начало цикла (|:n)…", L"Schleifenstart (|:n)…",
			L"Início do loop (|:n)…", L"Lusbegin (|:n)…", L"Początek pętli (|:n)…", L"Döngü başlangıcı (|:n)…"));
		menu.AddCommand(9021, LL14(
			L"ループ終了 (:|)", L"Loop end (:|)", L"Fin de boucle (:|)", L"Fine loop (:|)", L"Fin de bucle (:|)",
			L"루프 끝 (:|)", L"循环结束 (:|)", L"نهاية الحلقة (:|)", L"Конец цикла (:|)", L"Schleifenende (:|)",
			L"Fim do loop (:|)", L"Luseinde (:|)", L"Koniec pętli (:|)", L"Döngü sonu (:|)"));
		menu.AddCommand(9027, LL14(
			L"ペダルON (Ped.)", L"Pedal ON (Ped.)", L"Pédale ON", L"Pedale ON", L"Pedal ON",
			L"페달 ON", L"踏板ON", L"دواسة ON", L"Педаль ON", L"Pedal ON",
			L"Pedal ON", L"Pedaal ON", L"Pedał ON", L"Pedal ON"));
		menu.AddCommand(9028, LL14(
			L"ペダルOFF (＊)", L"Pedal OFF (＊)", L"Pédale OFF", L"Pedale OFF", L"Pedal OFF",
			L"페달 OFF", L"踏板OFF", L"دواسة OFF", L"Педаль OFF", L"Pedal OFF",
			L"Pedal OFF", L"Pedaal OFF", L"Pedał OFF", L"Pedal OFF"));
		if (markEv >= 0 && markEv < m_doc.evCount && m_doc.ev[markEv].kind == SC_EV_FM_LOOP_START) {
			menu.AddCommand(9029, LL14(
				L"ループ回数を変更…", L"Change loop count…", L"Changer le nombre…", L"Cambia ripetizioni…", L"Cambiar repeticiones…",
				L"루프 횟수 변경…", L"更改循环次数…", L"تغيير العدد…", L"Изменить число…", L"Wiederholungen ändern…",
				L"Alterar contagem…", L"Herhalingen wijzigen…", L"Zmień liczbę…", L"Tekrar sayısını değiştir…"));
		}
		menu.AddCommand(9022, LL14(
			L"Qマーク（ジャンプ着地）", L"Q mark (jump land)", L"Marque Q (atterrissage)", L"Segno Q (atterraggio)", L"Marca Q (aterrizaje)",
			L"Q 마크 (점프 착지)", L"Q标记（跳转着陆）", L"علامة Q", L"Метка Q", L"Q-Marke (Sprungziel)",
			L"Marca Q (pouso)", L"Q-markering", L"Znacznik Q", L"Q işareti"));
		menu.AddCommand(9023, LL14(
			L"Jジャンプ", L"J jump", L"Saut J", L"Salto J", L"Salto J",
			L"J 점프", L"J跳转", L"قفزة J", L"Прыжок J", L"J-Sprung",
			L"Salto J", L"J-sprong", L"Skok J", L"J atlayışı"));
		if (delN > 1) {
			for (int di = 0; di < delN; di++) {
				const int ei = delMarks[di];
				if (ei < 0 || ei >= m_doc.evCount) continue;
				const ScEvent& e = m_doc.ev[ei];
				CString lab;
				if (e.kind == SC_EV_FM_LOOP_START)
					lab.Format(L"削除 |:%u", (unsigned)(e.a ? e.a : 2));
				else if (e.kind == SC_EV_FM_LOOP_END)
					lab = L"削除 :|";
				else if (e.kind == SC_EV_PEDAL_ON)
					lab = L"削除 Ped.";
				else if (e.kind == SC_EV_PEDAL_OFF)
					lab = L"削除 ＊";
				else if (e.kind == SC_EV_JUMP_MARK)
					lab = L"削除 Q";
				else if (e.kind == SC_EV_FM_JUMP)
					lab = L"削除 J";
				else
					lab.Format(L"削除 #%d", di + 1);
				menu.AddCommand(9100 + di, lab);
			}
			menu.AddCommand(9120, L"重なっているマークをすべて削除");
		} else if (markEv >= 0) {
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
		if (VstLivePartIsLoaded(tr + 1)) {
			menu.AddCommand(9003, LL14(
				L"VST詳細…", L"VST details…", L"Détails VST…", L"Dettagli VST…", L"Detalles VST…",
				L"VST 상세…", L"VST详情…", L"تفاصيل VST…", L"Сведения VST…", L"VST-Details…",
				L"Detalhes VST…", L"VST-details…", L"Szczegóły VST…", L"VST ayrıntıları…"));
		}
		menu.AddCommand(9004, m_ui.visible[tr]
			? LL14(L"このトラックを隠す", L"Hide this track", L"Masquer", L"Nascondi", L"Ocultar", L"Hide", L"隐藏", L"Hide", L"Hide", L"Ausblenden", L"Ocultar", L"Verbergen", L"Ukryj", L"Gizle")
			: LL14(L"このトラックを表示", L"Show this track", L"Afficher", L"Mostra", L"Mostrar", L"Show", L"显示", L"Show", L"Show", L"Einblenden", L"Mostrar", L"Tonen", L"Pokaz", L"Goster"));
		menu.AddCommand(9005,
			m_ui.clef[tr] == 0 ? L"譜表: ト音→ヘ音"
			: (m_ui.clef[tr] == 1 ? L"譜表: ヘ音→大譜表"
			: (m_ui.clef[tr] == 2 ? L"譜表: 大譜表→ドラム"
			: L"譜表: ドラム→ト音")));
		menu.AddCommand(9007, L"全トラックを大譜表に");
		menu.AddCommand(9008, L"全トラックをト音に");
		menu.AddCommand(9009, L"全トラックをドラム(キット)に");
		menu.AddSeparator();
	}
	menu.AddCommand(9006, LL14(
		L"選択モード（鉛筆解除）", L"Select mode (exit pencil)", L"Mode selection", L"Modalita selezione", L"Modo seleccion",
		L"Select mode", L"选择模式", L"Select mode", L"Select mode", L"Auswahlmodus", L"Modo selecao", L"Selectiemodus", L"Tryb zaznaczania", L"Secim modu"));
	menu.AddCommand(IDC_SASAMI_MIDI_SHOWALL, LL14(
		L"MIDI1-32をすべて表示", L"Show all MIDI 1-32", L"Afficher MIDI 1-32", L"Mostra MIDI 1-32", L"Mostrar MIDI 1-32",
		L"Show MIDI 1-32", L"显示全部MIDI1-32", L"Show MIDI 1-32", L"Show MIDI 1-32", L"Alle MIDI 1-32", L"Mostrar MIDI 1-32", L"Toon MIDI 1-32", L"Pokaz MIDI 1-32", L"MIDI 1-32 tumu"));
	menu.AddCommand(IDC_SASAMI_MIDI_PLAY, LL14(
		L"再生確認", L"Preview", L"Apercu", L"Anteprima", L"Vista previa",
		L"Preview", L"试听", L"Preview", L"Preview", L"Vorschau", L"Previa", L"Voorbeeld", L"Podglad", L"Onizle"));
	menu.AddCommand(IDC_SASAMI_MIDI_EXPORT, LL14(
		L"音声書き出し…", L"Audio export...", L"Export audio...", L"Esporta audio...", L"Exportar audio...",
		L"Audio export...", L"导出音频...", L"Audio export...", L"Audio export...", L"Audio export...", L"Exportar audio...", L"Audio exporteren...", L"Eksport audio...", L"Ses disa aktar..."));
	menu.AddSeparator();
	menu.AddCommand(IDC_SASAMI_MIDI_HELP, LL14(
		L"ヘルプ", L"Help", L"Aide", L"Guida", L"Ayuda",
		L"Help", L"帮助", L"Help", L"Help", L"Hilfe", L"Ajuda", L"Help", L"Pomoc", L"Yardim"));
	const UINT cmd = menu.Track(point, this);
	auto afterMark = [&]() {
		m_ui.visible[tr] = 1;
		m_curCh = tr;
		if (m_ch.GetSafeHwnd()) m_ch.SetCurSel(m_curCh);
		RefreshStrip();
		PushDocToText();
		InvalidateRect(m_bodyRc, FALSE);
		CString st;
		st.Format(L"MIDI %d mark @ tick %u", tr + 1, (unsigned)atTick);
		m_status.SetWindowText(st);
	};
	if (tr >= 0 && cmd == 9010) {
		m_curCh = tr;
		if (m_ch.GetSafeHwnd()) m_ch.SetCurSel(m_curCh);
		m_ui.visible[tr] = 1;
		OpenVstForPart(tr + 1);
	}
	else if (tr >= 0 && cmd == 9011) {
		m_curCh = tr;
		if (m_ch.GetSafeHwnd()) m_ch.SetCurSel(m_curCh);
		m_ui.visible[tr] = 1;
		m_ui.markerTick = atTick;
		CSasamiExcRpnDlg::OpenOwned(this, &m_doc, tr, atTick);
		m_status.SetWindowText(L"Exc/RPN: insert SysEx・RPN・NRPN at marker.");
	}
	else if (tr >= 0 && cmd == 9012) {
		m_curCh = tr;
		if (m_ch.GetSafeHwnd()) m_ch.SetCurSel(m_curCh);
		m_ui.visible[tr] = 1;
		SyncFxBindsToLive();
		CSasamiInsertFxDlg::OpenOwned(this, &m_doc, tr + 1);
		m_status.SetWindowText(L"Insert FX: pick VST effect, tweak knobs, Bypass/Editor.");
	}
	else if (tr >= 0 && cmd == 9020) {
		int n = 2;
		if (CSasamiSimpleInputDlg::AskNumber(this, L"ループ開始 |:",
			L"繰り返し回数 (1–99)", 2, 1, 99, &n) == IDOK) {
			if (ScMidiAddLoopStart(&m_doc, atTick, tr, n, m_ui.markStack)) afterMark();
		}
	}
	else if (tr >= 0 && cmd == 9021) {
		if (ScMidiAddLoopEnd(&m_doc, atTick, tr, m_ui.markStack)) afterMark();
	}
	else if (tr >= 0 && cmd == 9027) {
		if (ScMidiAddPedalOn(&m_doc, atTick, tr, m_ui.markStack)) afterMark();
	}
	else if (tr >= 0 && cmd == 9028) {
		if (ScMidiAddPedalOff(&m_doc, atTick, tr, m_ui.markStack)) afterMark();
	}
	else if (cmd == 9029 && markEv >= 0 && markEv < m_doc.evCount
		&& m_doc.ev[markEv].kind == SC_EV_FM_LOOP_START) {
		int n = m_doc.ev[markEv].a ? (int)m_doc.ev[markEv].a : 2;
		if (CSasamiSimpleInputDlg::AskNumber(this, L"ループ回数",
			L"繰り返し回数 (1–99)", n, 1, 99, &n) == IDOK) {
			m_doc.ev[markEv].a = (uint8_t)n;
			PushDocToText();
			InvalidateRect(m_bodyRc, FALSE);
		}
	}
	else if (tr >= 0 && cmd == 9022) {
		if (ScMidiAddJumpMark(&m_doc, atTick, tr)) afterMark();
	}
	else if (tr >= 0 && cmd == 9023) {
		if (ScMidiAddJump(&m_doc, atTick, tr)) afterMark();
	}
	else if (cmd == 9024 && markEv >= 0 && markEv < m_doc.evCount) {
		for (int j = markEv; j + 1 < m_doc.evCount; j++)
			m_doc.ev[j] = m_doc.ev[j + 1];
		m_doc.evCount--;
		if (m_ui.selEv == markEv) m_ui.selEv = -1;
		else if (m_ui.selEv > markEv) m_ui.selEv--;
		RefreshStrip();
		PushDocToText();
		InvalidateRect(m_bodyRc, FALSE);
	}
	else if (cmd >= 9100 && cmd < 9100 + delN) {
		const int ei = delMarks[cmd - 9100];
		if (ei >= 0 && ei < m_doc.evCount) {
			for (int j = ei; j + 1 < m_doc.evCount; j++)
				m_doc.ev[j] = m_doc.ev[j + 1];
			m_doc.evCount--;
			if (m_ui.selEv == ei) m_ui.selEv = -1;
			else if (m_ui.selEv > ei) m_ui.selEv--;
			RefreshStrip();
			PushDocToText();
			InvalidateRect(m_bodyRc, FALSE);
		}
	}
	else if (cmd == 9120 && delN > 0) {
		/* Delete outer indices first so shifts don't invalidate. */
		int sorted[16];
		int sn = delN < 16 ? delN : 16;
		memcpy(sorted, delMarks, sizeof(int) * (size_t)sn);
		for (int a = 0; a < sn; a++)
			for (int b = a + 1; b < sn; b++)
				if (sorted[b] > sorted[a]) { int t = sorted[a]; sorted[a] = sorted[b]; sorted[b] = t; }
		for (int di = 0; di < sn; di++) {
			const int ei = sorted[di];
			if (ei < 0 || ei >= m_doc.evCount) continue;
			for (int j = ei; j + 1 < m_doc.evCount; j++)
				m_doc.ev[j] = m_doc.ev[j + 1];
			m_doc.evCount--;
			if (m_ui.selEv == ei) m_ui.selEv = -1;
			else if (m_ui.selEv > ei) m_ui.selEv--;
		}
		RefreshStrip();
		PushDocToText();
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
	else if (tr >= 0 && cmd == 9003) {
		if (ScVstShowPartMenu(this, tr + 1, point, &m_doc.bind)) {
			RefreshProgLabels();
			InvalidateRect(m_trackRc, FALSE);
		}
	}
	else if (tr >= 0 && cmd == 9004) {
		m_ui.visible[tr] = m_ui.visible[tr] ? 0 : 1;
		UpdateScrollBars();
		InvalidateRect(m_bodyRc, FALSE);
	}
	else if (tr >= 0 && cmd == 9005) {
		m_ui.clef[tr] = (m_ui.clef[tr] + 1) % 4;
		UpdateScrollBars();
		InvalidateRect(m_bodyRc, FALSE);
	}
	else if (cmd == 9007 || cmd == 9008 || cmd == 9009) {
		const int c = (cmd == 9009) ? 3 : ((cmd == 9007) ? 2 : 0);
		for (int i = 0; i < m_ui.trackCount; i++) m_ui.clef[i] = c;
		UpdateScrollBars();
		InvalidateRect(m_bodyRc, FALSE);
	}
	else if (cmd == 9006) {
		m_ui.tool = SC_TOOL_SELECT;
		m_ui.hoverValid = 0;
		UpdateNoteCursor();
		InvalidateRect(m_bodyRc, FALSE);
	}
	else if (cmd) PostMessage(WM_COMMAND, cmd);
}

void CSasamiMidiScoreDlg::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	const int ctrl = (GetKeyState(VK_CONTROL) & 0x8000) ? 1 : 0;
	if (nChar == VK_DELETE || nChar == VK_BACK) {
		if (ScStaffSelDelete(m_doc.ev, &m_doc.evCount, &m_ui)) {
			RefreshStrip();
			PushDocToText();
			InvalidateRect(m_bodyRc, FALSE);
			m_status.SetWindowText(L"Deleted selection.");
		} else {
			m_status.SetWindowText(L"Select a note/mark first (Select or click chip → Delete). Or use Eraser.");
		}
		return;
	}
	if (ctrl && (nChar == 'A' || nChar == 'a')) {
		ScStaffSelClear(&m_ui);
		for (int i = 0; i < m_doc.evCount; i++) {
			uint8_t k = m_doc.ev[i].kind;
			if (k == SC_EV_NOTE || k == SC_EV_REST || k == SC_EV_TIE)
				ScStaffSelAdd(&m_ui, i);
		}
		InvalidateRect(m_bodyRc, FALSE);
		return;
	}
	if (ctrl && (nChar == 'C' || nChar == 'c')) {
		m_clipN = ScStaffSelCopy(m_doc.ev, m_doc.evCount, &m_ui, m_clip, SC_CLIP_MAX, &m_clipBase);
		return;
	}
	if (ctrl && (nChar == 'X' || nChar == 'x')) {
		m_clipN = ScStaffSelCopy(m_doc.ev, m_doc.evCount, &m_ui, m_clip, SC_CLIP_MAX, &m_clipBase);
		if (m_clipN > 0 && ScStaffSelDelete(m_doc.ev, &m_doc.evCount, &m_ui)) {
			RefreshStrip();
			InvalidateRect(m_bodyRc, FALSE);
		}
		return;
	}
	if (ctrl && (nChar == 'V' || nChar == 'v')) {
		if (m_clipN > 0) {
			ScStaffSelPaste(m_doc.ev, &m_doc.evCount, SC_EV_MAX, m_clip, m_clipN, m_ui.markerTick, 0);
			RefreshStrip();
			InvalidateRect(m_bodyRc, FALSE);
		}
		return;
	}
	if (ctrl && (nChar == 'T' || nChar == 't')) {
		if (ScStaffTieSelected(m_doc.ev, m_doc.evCount, &m_ui))
			InvalidateRect(m_bodyRc, FALSE);
		return;
	}
	if (nChar == VK_ESCAPE) {
		m_ui.tool = SC_TOOL_SELECT;
		m_ui.hoverValid = 0;
		m_ui.marqueeOn = 0;
		UpdateNoteCursor();
		InvalidateRect(m_gridRc, FALSE);
		return;
	}
	CCustomBlurDialogExBase::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CSasamiMidiScoreDlg::OnClose()
{
	/* Drop notify first so Host64 SHM close posts cannot hit a dying HWND. */
	VstLiveEditorSetNotifyHwnd(NULL);
	VstLiveEditorOpenCancelPending();
	VstLiveMonitorStop();
	PersistSession();
	PushDocToText();
	KillTimer(kScPreviewTimer);
	KillTimer(9121);
	KillTimer(9122);
	m_ui.previewActive = 0;
	CSasamiNotePropsDlg::CloseOpen();
	CSasamiExcRpnDlg::CloseOpen();
	CSasamiInsertFxDlg::CloseOpen();
	if (CSasamiNotePaletteDlg* pal = CSasamiNotePaletteDlg::Instance())
		pal->DestroyWindow();
	DestroyWindow();
}
void CSasamiMidiScoreDlg::OnDestroy()
{
	VstLiveEditorSetNotifyHwnd(NULL);
	VstLiveEditorOpenCancelPending();
	KillTimer(kScPreviewTimer);
	KillTimer(9121);
	KillTimer(9122);
	m_ui.previewActive = 0;
	CCustomBlurDialogExBase::OnDestroy();
}
void CSasamiMidiScoreDlg::PostNcDestroy()
{
	if (s_inst == this) s_inst = NULL;
	CCustomBlurDialogExBase::PostNcDestroy();
	delete this;
}

void CSasamiMidiScoreDlg::OnBnClickedText()
{
	PushDocToText();
	CSasamiTextDlg::OpenOwned(this);
}
