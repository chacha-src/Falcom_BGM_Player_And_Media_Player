#include "stdafx.h"
#include "ogg.h"
#include "CSasamiFmScoreDlg.h"
#include "CSasamiTextDlg.h"
#include "CSasamiFmVoiceDlg.h"
#include "CSasamiNotePaletteDlg.h"
#include "CSasamiLayoutPaletteDlg.h"
#include "CSasamiSimpleInputDlg.h"
#include "CSasamiNotePropsDlg.h"
#include "CCustomPopupMenu.h"
#include "OfflineHelp.h"
#include "CSasamiCmdHelpDlg.h"
#include "CSasamiScoreArrange.h"
#include "CSasamiPianoRollDlg.h"
#include "CSasamiScoreMidiIn.h"
#include "CSasamiScoreHist.h"
#include "PlayList.h"
#include "VstMidiEngine.h"
#include "kb_sasami/source/sasami_write.h"
#include <shlwapi.h>

#pragma comment(lib, "Shlwapi.lib")

extern CPlayList* pl;

enum { kScFmPreviewTimer = 7102, kScFmMetroTimer = 7103 };

CSasamiFmScoreDlg* CSasamiFmScoreDlg::s_inst = NULL;

static void ScFmDirOfPath(const wchar_t* path, wchar_t* out, int outCch)
{
	if (!out || outCch <= 0) return;
	out[0] = 0;
	if (!path || !path[0]) return;
	wcsncpy_s(out, outCch, path, _TRUNCATE);
	wchar_t* sl = wcsrchr(out, L'\\');
	if (!sl) sl = wcsrchr(out, L'/');
	if (sl) *sl = 0;
	else out[0] = 0;
}

static void ScFmKpiPluginDir(wchar_t* out, int outCch)
{
	if (!out || outCch <= 0) return;
	out[0] = 0;
	wchar_t exe[MAX_PATH];
	if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return;
	ScFmDirOfPath(exe, out, outCch);
	if (out[0])
		wcsncat_s(out, outCch, L"\\Plugins\\kbsasami", _TRUNCATE);
}

static int ScFmRelativePathForSample(const wchar_t* full, const wchar_t* baseDir, wchar_t* out, int outCch)
{
	if (!full || !full[0] || !out || outCch <= 0) return 0;
	out[0] = 0;
	wchar_t rel[MAX_PATH];
	if (baseDir && baseDir[0] &&
		PathRelativePathToW(rel, baseDir, FILE_ATTRIBUTE_DIRECTORY, full, FILE_ATTRIBUTE_NORMAL) &&
		rel[0] && rel[1] != L':') {
		const wchar_t* p = rel;
		if (p[0] == L'.' && (p[1] == L'\\' || p[1] == L'/')) p += 2;
		wcsncpy_s(out, outCch, p, _TRUNCATE);
		return 1;
	}
	const wchar_t* leaf = wcsrchr(full, L'\\');
	if (!leaf) leaf = wcsrchr(full, L'/');
	wcsncpy_s(out, outCch, leaf ? leaf + 1 : full, _TRUNCATE);
	return out[0] != 0;
}

uint8_t CSasamiFmScoreDlg::MidiToFmNoteByte(int midiNote)
{
	int oct = midiNote / 12 - 1;
	int scale = midiNote % 12;
	if (oct < 0) oct = 0;
	if (oct > 9) oct = 9;
	if (scale < 0) scale = 0;
	if (scale > 11) scale = 11;
	return (uint8_t)(((oct & 0x0F) << 4) | (scale & 0x0F));
}

IMPLEMENT_DYNAMIC(CSasamiFmScoreDlg, CCustomBlurDialogExBase)

CSasamiFmScoreDlg::CSasamiFmScoreDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(CSasamiFmScoreDlg::IDD, pParent)
	, m_curCh(0), m_placeRest(0), m_accidental(0), m_noteCur(NULL), m_sbDrag(0), m_sbDragScroll0(0), m_sbDragAnchor(0), m_bInLayout(FALSE)
	, m_clipN(0), m_clipBase(0), m_metroBeat(0)
{
	m_drawCursors.pencil = NULL;
	m_drawCursors.line = NULL;
	m_drawCursors.curve = NULL;
	m_lastOut[0] = 0;
	m_lastHoverSt[0] = 0;
	ScFmDocClear(&m_doc);
	static const uint8_t kDefVoice[25] = {
		0x3B, 0x00, 0x00, 0x20, 0x28, 0x20, 0x1A, 0x0D, 0x9F, 0x9E, 0xDE, 0x9E,
		0x05, 0x05, 0x05, 0x05, 0x0F, 0x0B, 0x0C, 0x0B, 0x8A, 0xF6, 0x86, 0xF7, 0x1B
	};
	ScFmAllocVoice(&m_doc, kDefVoice);
	ScStaffUiInit(&m_ui, SC_FM_TOTAL, 1);
	ScScoreMidiInInit(&m_midiIn);
	ScScoreHistInit(&m_hist);
	ScPianoRollInit(&m_rollView);
}

CSasamiFmScoreDlg::~CSasamiFmScoreDlg()
{
	if (m_noteCur) { ::DestroyCursor(m_noteCur); m_noteCur = NULL; }
	ScStaffDrawCursorsFree(&m_drawCursors);
}

CSasamiFmScoreDlg* CSasamiFmScoreDlg::Instance() { return s_inst; }

void CSasamiFmScoreDlg::OpenOwned(CWnd* owner)
{
	const int created = !(s_inst && ::IsWindow(s_inst->GetSafeHwnd()));
	if (!created) {
		CCC_PresentOwnedHelp(s_inst, owner ? owner : AfxGetMainWnd());
		s_inst->PullDocFromText(1);
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
	s_inst->PullDocFromText(1);
	if (s_inst->m_doc.evCount <= 0) {
		ScFmDoc* tmp = (ScFmDoc*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ScFmDoc));
		if (tmp) {
			if (ScSessionLoadLastFm(tmp, NULL, 0))
				s_inst->LoadFromDoc(*tmp);
			ScFmDocClear(tmp);
			HeapFree(GetProcessHeap(), 0, tmp);
		}
	}
	s_inst->SyncMeterFromDoc();
	s_inst->RefreshToneLabels();
	ScStaffUpdateContentExtent(&s_inst->m_ui, s_inst->m_doc.ev, s_inst->m_doc.evCount);
	s_inst->InvalidateRect(s_inst->m_bodyRc, FALSE);
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
	DDX_Control(pDX, IDC_SASAMI_FM_ARR, m_btnArr);
	DDX_Control(pDX, IDC_SASAMI_FM_LAYOUT, m_btnLayout);
	DDX_Control(pDX, IDC_SASAMI_FM_ROLL, m_btnRoll);
	DDX_Control(pDX, IDC_SASAMI_FM_INDEV, m_midiInDev);
	DDX_Control(pDX, IDC_SASAMI_FM_INMODE, m_midiInMode);
	DDX_Control(pDX, IDC_SASAMI_STRIP_KIND0, m_stripKind0);
	DDX_Control(pDX, IDC_SASAMI_STRIP_KIND1, m_stripKind1);
	DDX_Control(pDX, IDC_SASAMI_STRIP_KIND2, m_stripKind2);
	DDX_Control(pDX, IDC_SASAMI_STRIP_DRAW, m_stripDraw);
	DDX_Control(pDX, IDC_SASAMI_STRIP_LANES, m_stripLanes);
	DDX_Control(pDX, IDC_SASAMI_STRIP_STEP, m_stripStep);
	DDX_Control(pDX, IDC_SASAMI_SCORE_HELPBAR, m_helpBar);
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
	ON_BN_CLICKED(IDC_SASAMI_FM_ARR, &CSasamiFmScoreDlg::OnBnClickedArr)
	ON_BN_CLICKED(IDC_SASAMI_FM_LAYOUT, &CSasamiFmScoreDlg::OnBnClickedLayout)
	ON_BN_CLICKED(IDC_SASAMI_FM_ROLL, &CSasamiFmScoreDlg::OnBnClickedRoll)
	ON_CBN_SELCHANGE(IDC_SASAMI_FM_INDEV, &CSasamiFmScoreDlg::OnCbnMidiIn)
	ON_CBN_SELCHANGE(IDC_SASAMI_FM_INMODE, &CSasamiFmScoreDlg::OnCbnMidiIn)
	ON_CBN_SELCHANGE(IDC_SASAMI_FM_CH, &CSasamiFmScoreDlg::OnCbnSelchangeCh)
	ON_MESSAGE(WM_SASAMI_SCORE_MIDI, &CSasamiFmScoreDlg::OnScoreMidi)
	ON_CBN_SELCHANGE(IDC_SASAMI_STRIP_KIND0, &CSasamiFmScoreDlg::OnCbnStrip)
	ON_CBN_SELCHANGE(IDC_SASAMI_STRIP_KIND1, &CSasamiFmScoreDlg::OnCbnStrip)
	ON_CBN_SELCHANGE(IDC_SASAMI_STRIP_KIND2, &CSasamiFmScoreDlg::OnCbnStrip)
	ON_CBN_SELCHANGE(IDC_SASAMI_STRIP_HGT0, &CSasamiFmScoreDlg::OnCbnStrip)
	ON_CBN_SELCHANGE(IDC_SASAMI_STRIP_HGT1, &CSasamiFmScoreDlg::OnCbnStrip)
	ON_CBN_SELCHANGE(IDC_SASAMI_STRIP_HGT2, &CSasamiFmScoreDlg::OnCbnStrip)
	ON_CBN_SELCHANGE(IDC_SASAMI_STRIP_DRAW, &CSasamiFmScoreDlg::OnCbnStrip)
	ON_CBN_SELCHANGE(IDC_SASAMI_STRIP_LANES, &CSasamiFmScoreDlg::OnCbnStrip)
	ON_CBN_SELCHANGE(IDC_SASAMI_STRIP_STEP, &CSasamiFmScoreDlg::OnCbnStrip)
	ON_WM_KEYDOWN()
	ON_MESSAGE(WM_SASAMI_PAL_DUR, &CSasamiFmScoreDlg::OnPalDur)
	ON_MESSAGE(WM_SASAMI_PAL_QUERY_STATE, &CSasamiFmScoreDlg::OnPalQueryState)
	ON_MESSAGE(WM_SASAMI_PAL_LAYOUT, &CSasamiFmScoreDlg::OnPalLayout)
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
	m_btnPencil.SetWindowText(LL14(L"\u97f3\u7b26", L"Notes", L"Notes", L"Note", L"Notas", L"Notes", L"音符", L"音符", L"Notes", L"Noten", L"Notas", L"Noten", L"Nuty", L"Nota"));
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
	if (m_stripLanes.GetSafeHwnd())
		SyncStripCombos();
	UpdateHelpBar();
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
	if (m_btnArr.GetSafeHwnd()) flatBtn(m_btnArr);
	if (m_btnLayout.GetSafeHwnd()) flatBtn(m_btnLayout);
	if (m_btnRoll.GetSafeHwnd()) flatBtn(m_btnRoll);
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
	m_status.SetWindowText(LL14(
		L"FM譜面: 音符で入力。Voice=@neiro/@CVn/カスタム。Tone行に音色表示。",
		L"FM score: Notes tool to enter. Voice=@neiro/@CVn/custom. Tone row shows timbre.",
		L"Partition FM: crayon. Voice=@neiro/perso. Ligne Tone = timbre.",
		L"Partitura FM: matita. Voice=@neiro/custom. Riga Tone = timbro.",
		L"Partitura FM: lápiz. Voice=@neiro/custom. Fila Tone = timbre.",
		L"FM 악보: 연필 입력. Voice=@neiro/커스텀. Tone 행에 음색.",
		L"FM谱面: 铅笔输入。Voice=@neiro/自定义。Tone行显示音色。",
		L"نوتة FM: قلم. Voice=@neiro. صف Tone للطابع.",
		L"FM-партия: карандаш. Voice=@neiro/своё. Строка Tone — тембр.",
		L"FM-Partitur: Stift. Voice=@neiro/custom. Tone-Zeile = Klang.",
		L"Partitura FM: lápis. Voice=@neiro/custom. Linha Tone = timbre.",
		L"FM-partituur: potlood. Voice=@neiro/custom. Tone-rij = klank.",
		L"Partytura FM: ołówek. Voice=@neiro/własny. Wiersz Tone = barwa.",
		L"FM parti: kalem. Voice=@neiro/özel. Tone satırı = tını."));
	if (m_stripKind0.GetSafeHwnd()) {
		m_stripKind0.SetAeroMode(FALSE);
		m_stripKind1.SetAeroMode(FALSE);
		m_stripKind2.SetAeroMode(FALSE);
		m_stripDraw.SetAeroMode(FALSE);
		m_stripLanes.SetAeroMode(FALSE);
		auto mkStripHgt = [&](CCustomComboBox& cb, int id) {
			if (!cb.GetSafeHwnd())
				cb.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
					CRect(0, 0, 44, 120), this, id);
			cb.SetAeroMode(FALSE);
		};
		mkStripHgt(m_stripHgt0, IDC_SASAMI_STRIP_HGT0);
		mkStripHgt(m_stripHgt1, IDC_SASAMI_STRIP_HGT1);
		mkStripHgt(m_stripHgt2, IDC_SASAMI_STRIP_HGT2);
		if (m_helpBar.GetSafeHwnd()) m_helpBar.SetAeroMode(FALSE);
		ScStaffLoadPartStrip(&m_ui, m_curCh);
		SyncStripCombos();
	}
	SyncMeterFromDoc();
	RefreshToneLabels();
	RefreshStrip();
	UpdateNoteCursor();
	ScStaffDrawCursorsInit(&m_drawCursors);
	SyncMidiInCombos();
	LayoutChrome();
	RestoreUiGeom();
	RefreshPartEnabled();
	UpdateHelpBar();
	PostMessage(WM_APP + 61, 0, 0);
	return TRUE;
}

void CSasamiFmScoreDlg::UpdateHelpBar()
{
	if (!m_helpBar.GetSafeHwnd()) return;
	wchar_t buf[512];
	ScStaffFormatHelpBar(buf, 512, &m_ui, 1, m_curCh);
	m_helpBar.SetWindowText(buf);
}

void CSasamiFmScoreDlg::SyncStripCombos()
{
	auto fillKind = [](CCustomComboBox& cb, int sel) {
		cb.ResetContent();
		for (int k = 0; k < SC_STRIP_KIND_COUNT; k++)
			cb.AddString(ScStaffStripKindNameFm(k));
		if (sel < 0 || sel >= SC_STRIP_KIND_COUNT) sel = 0;
		cb.SetCurSel(sel);
		cb.SetDroppedWidth(260);
	};
	auto fillHgt = [](CCustomComboBox& cb, int sel) {
		cb.ResetContent();
		for (int h = 0; h < SC_STRIP_HGT_COUNT; h++)
			cb.AddString(ScStaffStripHeightName(h));
		if (sel < 0 || sel >= SC_STRIP_HGT_COUNT) sel = SC_STRIP_HGT_WIDE;
		cb.SetCurSel(sel);
		cb.SetDroppedWidth(100);
	};
	fillKind(m_stripKind0, m_ui.stripKind[0]);
	fillKind(m_stripKind1, m_ui.stripKind[1]);
	fillKind(m_stripKind2, m_ui.stripKind[2]);
	fillHgt(m_stripHgt0, m_ui.stripLaneHgt[0]);
	fillHgt(m_stripHgt1, m_ui.stripLaneHgt[1]);
	fillHgt(m_stripHgt2, m_ui.stripLaneHgt[2]);
	m_stripDraw.ResetContent();
	m_stripDraw.AddString(ScStaffStripDrawModeName(SC_STRIP_DRAW_PENCIL));
	m_stripDraw.AddString(ScStaffStripDrawModeName(SC_STRIP_DRAW_LINE));
	m_stripDraw.AddString(ScStaffStripDrawModeName(SC_STRIP_DRAW_CURVE));
	if (m_ui.stripDraw < 0 || m_ui.stripDraw > 2) m_ui.stripDraw = SC_STRIP_DRAW_PENCIL;
	m_stripDraw.SetCurSel(m_ui.stripDraw);
	m_stripDraw.SetDroppedWidth(120);
	m_stripLanes.ResetContent();
	for (int n = 0; n <= SC_STRIP_LANES_MAX; n++)
		m_stripLanes.AddString(ScStaffStripLanesLabel(n));
	int lanes = m_ui.stripCount;
	if (lanes < 0) lanes = 0;
	if (lanes > SC_STRIP_LANES_MAX) lanes = SC_STRIP_LANES_MAX;
	m_stripLanes.SetCurSel(lanes);
	m_stripStep.ResetContent();
	m_stripStep.AddString(L"1/4");
	m_stripStep.AddString(L"1/8");
	m_stripStep.AddString(L"1/16");
	m_stripStep.AddString(L"1/32");
	m_stripStep.AddString(L"1/64");
	ScStaffNormalizeStripStep(&m_ui);
	static const int kSteps[] = { SC_PPQN, SC_PPQN / 2, SC_PPQN / 4, SC_PPQN / 8, SC_PPQN / 16 };
	int stepSel = 1;
	for (int i = 0; i < 5; i++)
		if (m_ui.stripStepTicks == kSteps[i]) { stepSel = i; break; }
	m_stripStep.SetCurSel(stepSel);
}

void CSasamiFmScoreDlg::OnCbnStrip()
{
	int k0 = m_stripKind0.GetCurSel();
	int k1 = m_stripKind1.GetCurSel();
	int k2 = m_stripKind2.GetCurSel();
	int h0 = m_stripHgt0.GetCurSel();
	int h1 = m_stripHgt1.GetCurSel();
	int h2 = m_stripHgt2.GetCurSel();
	int draw = m_stripDraw.GetCurSel();
	int lanes = m_stripLanes.GetCurSel();
	int stepSel = m_stripStep.GetCurSel();
	if (k0 >= 0) m_ui.stripKind[0] = k0;
	if (k1 >= 0) m_ui.stripKind[1] = k1;
	if (k2 >= 0) m_ui.stripKind[2] = k2;
	if (h0 >= 0) m_ui.stripLaneHgt[0] = h0;
	if (h1 >= 0) m_ui.stripLaneHgt[1] = h1;
	if (h2 >= 0) m_ui.stripLaneHgt[2] = h2;
	if (draw >= 0) m_ui.stripDraw = draw;
	if (lanes < 0) lanes = 0;
	if (lanes > SC_STRIP_LANES_MAX) lanes = SC_STRIP_LANES_MAX;
	m_ui.stripCount = lanes;
	static const int kSteps[] = { SC_PPQN, SC_PPQN / 2, SC_PPQN / 4, SC_PPQN / 8, SC_PPQN / 16 };
	if (stepSel >= 0 && stepSel < 5)
		m_ui.stripStepTicks = kSteps[stepSel];
	ScStaffNormalizeStripStep(&m_ui);
	ScStaffSavePartStrip(&m_ui, m_curCh);
	m_ui.helpTopic = SC_HELP_STRIP;
	RefreshStrip();
	LayoutChrome();
	Invalidate(FALSE);
	UpdateHelpBar();
}

void CSasamiFmScoreDlg::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	const int ctrl = (GetKeyState(VK_CONTROL) & 0x8000) ? 1 : 0;
	if (ctrl && (nChar == 'Z' || nChar == 'z')) {
		if (ScScoreHistUndo(&m_hist, m_doc.ev, &m_doc.evCount, SC_EV_MAX)) {
			RefreshStrip();
			InvalidateRect(m_bodyRc, FALSE);
			m_status.SetWindowText(LL14(L"元に戻す", L"Undo", L"Annuler", L"Annulla", L"Deshacer", L"실행 취소", L"撤销", L"تراجع", L"Отмена", L"Rückgängig", L"Desfazer", L"Ongedaan", L"Cofnij", L"Geri al"));
		}
		return;
	}
	if (ctrl && (nChar == 'Y' || nChar == 'y')) {
		if (ScScoreHistRedo(&m_hist, m_doc.ev, &m_doc.evCount, SC_EV_MAX)) {
			RefreshStrip();
			InvalidateRect(m_bodyRc, FALSE);
			m_status.SetWindowText(LL14(L"やり直し", L"Redo", L"Rétablir", L"Ripeti", L"Rehacer", L"다시 실행", L"重做", L"إعادة", L"Повтор", L"Wiederholen", L"Refazer", L"Opnieuw", L"Ponów", L"Yinele"));
		}
		return;
	}
	if (ctrl && (nChar == VK_UP || nChar == VK_DOWN)) {
		HistPush();
		const int semis = (nChar == VK_UP) ? 1 : -1;
		if (ScTransposeSelected(m_doc.ev, m_doc.evCount, &m_ui, semis))
			InvalidateRect(m_bodyRc, FALSE);
		return;
	}
	if (ctrl && (nChar == VK_LEFT || nChar == VK_RIGHT)) {
		HistPush();
		const int mul = (nChar == VK_RIGHT) ? 2 : 1;
		const int div = (nChar == VK_RIGHT) ? 1 : 2;
		if (ScScaleDurSelected(m_doc.ev, m_doc.evCount, &m_ui, mul, div))
			InvalidateRect(m_bodyRc, FALSE);
		return;
	}
	if (nChar == VK_HOME && !ctrl) {
		m_ui.markerTick = 0;
		m_status.SetWindowText(L"Marker → 0 (Home). Space plays from start.");
		InvalidateRect(m_bodyRc, FALSE);
		return;
	}
	if (nChar == VK_SPACE) {
		if (m_ui.previewActive) {
			ScStaffStopHostPreview(&m_ui);
			KillTimer(1);
			KillTimer(kScFmPreviewTimer);
			m_status.SetWindowText(L"Stopped (Space). Marker stays — Space again from marker.");
			InvalidateRect(m_bodyRc, FALSE);
		} else {
			OnBnClickedPlay();
		}
		return;
	}
	if (nChar == VK_DELETE || nChar == VK_BACK) {
		HistPush();
		if (ScStaffSelDelete(m_doc.ev, &m_doc.evCount, &m_ui)) {
			RefreshStrip();
			InvalidateRect(m_bodyRc, FALSE);
		}
		return;
	}
	if (ctrl && (nChar == 'A' || nChar == 'a')) {
		ScStaffSelClear(&m_ui);
		for (int i = 0; i < m_doc.evCount; i++) {
			uint8_t k = m_doc.ev[i].kind;
			if (k == SC_EV_FM_NOTE || k == SC_EV_FM_REST || k == SC_EV_TIE)
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
	CCustomBlurDialogExBase::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CSasamiFmScoreDlg::SetupTooltips()
{
	if (!CCustomControlUtility::BeginDialogToolTip(m_tooltip, this, TTS_NOPREFIX)) return;
	auto tip = [&](CWnd& w, LPCWSTR s) { if (w.GetSafeHwnd()) m_tooltip.AddTool(&w, s); };
	tip(m_btnOpen, L"Open FM MML/DAT (.mml .txt .dat .f) — score and text sync");
	tip(m_btnSave, L"Save As .fpy / .fpy2 (loop nest≥2 → fpy2)");
	tip(m_btnPlay, L"Preview from marker (Space = play/stop)");
	tip(m_btnExport, L"Audio export from compiled FM");
	tip(m_btnPencil, L"Pencil — place notes on staff");
	tip(m_btnErase, L"Eraser — click/drag to delete notes");
	tip(m_btnSel, L"Select tool — click note/mark, Delete/Backspace removes");
	tip(m_btnPal, L"Note duration palette (quarter/eighth/etc.)");
	tip(m_btnVoice, L"FM voice editor — 25-byte params, preview beep, apply to part");
	tip(m_btnText, L"Open text composer (FM/OPNA MML)");
	tip(m_btnHelp, L"コマンド説明・譜面操作ガイド（FM/共通/譜面タブ）");
	tip(m_btnArr, LL14(L"アレンジ／和音／パターン", L"Arrange / Chord / Pattern", L"Arrange / Accords / Motifs", L"Arrange / Accordi / Pattern", L"Arrange / Acordes / Patrones",
		L"어레인지/화음/패턴", L"编曲/和弦/型", L"ترتيب/وتر/نمط", L"Аранжировка/аккорд/паттерн", L"Arrange/Akkord/Muster", L"Arranjo/acorde/padrão", L"Arrange/akkoord/patroon", L"Aranż/akord/wzorzec", L"Arrange/akor/desen"));
	tip(m_btnLayout, LL14(L"譜表 — 拍子・調号・移調（表示用）", L"Layout — meter, key, transpose (score display)", L"Portée — mesure, armure (affichage)", L"Impag. — misura (visualizzazione)", L"Layout — compás (visual)",
		L"보표 — 박자·조표·이조", L"谱表 — 拍号·调号·移调（显示）", L"تخطيط", L"Партитура", L"Notation", L"Layout", L"Layout", L"Układ", L"Düzen"));
	tip(m_btnRoll, LL14(L"ピアノロール分割＋別窓", L"Piano roll split + floating window", L"Piano roll split + fenêtre", L"Piano roll split + finestra", L"Piano roll split + ventana",
		L"피아노 롤 분할+창", L"钢琴卷帘分割+浮动窗", L"رول بيانو+نافذة", L"Пианоролл+окно", L"Klavierrolle+Fenster", L"Piano roll+janela", L"Piano-roll+venster", L"Rolka+okno", L"Piyano rulosu+pencere"));
	tip(m_midiInDev, LL14(L"譜面専用 MIDI In（Hostとは別）", L"Score MIDI In (independent of Host)", L"MIDI In partition", L"MIDI In partitura", L"MIDI In partitura",
		L"악보 전용 MIDI In", L"谱面专用 MIDI In", L"MIDI In للنوتة", L"MIDI In партитуры", L"Partitur-MIDI-In", L"MIDI In da partitura", L"Partituur-MIDI-In", L"MIDI In partytury", L"Parti MIDI In"));
	tip(m_midiInMode, LL14(L"OFF / Step / Realtime", L"OFF / Step / Realtime", L"OFF / Pas / Temps réel", L"OFF / Step / Realtime", L"OFF / Paso / Tiempo real",
		L"OFF / 스텝 / 실시간", L"关 / 步进 / 实时", L"OFF / خطوة / فوري", L"OFF / Шаг / Реалтайм", L"AUS / Schritt / Echtzeit", L"OFF / Passo / Tempo real", L"UIT / Stap / Realtime", L"WYŁ / Krok / Realtime", L"KAPALI / Adım / Gerçek zaman"));
	tip(m_ch, L"編集対象パート (FM/SSG/RHY/Misao)");
	tip(m_stripKind0, L"レーン1の種類: Exp/Vol(TL)/Pitch/Gate%/Pan");
	tip(m_stripKind1, L"レーン2の種類");
	tip(m_stripDraw, L"ストリップ描画: 鉛筆 / 直線 / 曲線");
	tip(m_stripLanes, L"CC/パラメータレーン数: なし / ×1 / ×2");
	tip(m_stripStep, L"ストリップ横解像度: 4分〜64分音符");
	tip(m_status, L"Space=マーカーから再生/停止 · ルーラークリックで再生位置");
	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip);
}

LRESULT CSasamiFmScoreDlg::OnDeferredInit(WPARAM, LPARAM)
{
	CRect wr; GetWindowRect(&wr);
	CSasamiNotePaletteDlg::OpenNear(this, CPoint(wr.left + 40, wr.top + 100));
	CSasamiLayoutPaletteDlg::OpenNear(this, CPoint(wr.left + 420, wr.top + 100));
	return 0;
}

BOOL CSasamiFmScoreDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	if (pMsg->message == WM_KEYDOWN) {
		if (pMsg->wParam == VK_SPACE || pMsg->wParam == VK_HOME) {
			CWnd* f = GetFocus();
			const BOOL inEdit = (f && f->IsKindOf(RUNTIME_CLASS(CEdit)));
			if (!inEdit) {
				OnKeyDown((UINT)pMsg->wParam, 1, 0);
				return TRUE;
			}
		}
		if (pMsg->wParam == VK_DELETE || pMsg->wParam == VK_BACK ||
			((GetKeyState(VK_CONTROL) & 0x8000) && (pMsg->wParam == 'C' || pMsg->wParam == 'V' || pMsg->wParam == 'X' || pMsg->wParam == 'A' || pMsg->wParam == 'T'))) {
			OnKeyDown((UINT)pMsg->wParam, 1, 0);
			return TRUE;
		}
	}
	return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}

void CSasamiFmScoreDlg::LayoutChrome()
{
	if (!::IsWindow(m_hWnd) || m_bInLayout) return;
	m_bInLayout = TRUE;
	CRect rc;
	GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	const int pad = 8, btnH = 26, cbH = 32, btnW = 52;
	const int sbW = ScStaffScrollGutterW();
	const int sbH = ScStaffScrollGutterH();
	auto layoutCombo = [&](CCustomComboBox& c, int x, int y, int w, int h, int dropW = 0) {
		if (!c.GetSafeHwnd()) return;
		c.MoveWindow(x, y, w, h);
		const int closedH = (h - 4 > 22) ? (h - 4) : 22;
		const int dropH = (closedH > 28) ? closedH : 28;
		c.SetItemHeight(-1, closedH);
		c.SetItemHeight(0, dropH);
		if (dropW > 0) c.SetDroppedWidth(dropW);
	};
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
	place(m_btnTempo, btnW);
	if (m_btnPal.GetSafeHwnd()) m_btnPal.ShowWindow(SW_HIDE);
	place(m_btnVoice, btnW);
	if (m_ch.GetSafeHwnd()) { layoutCombo(m_ch, x, y, 104, btnH + 4); x += 112; }
	place(m_btnHelp, 48);
	y += btnH + 6;
	x = pad;
	if (m_edNote.GetSafeHwnd()) m_edNote.MoveWindow(x + 28, y, 44, 22);
	if (m_edGt.GetSafeHwnd()) m_edGt.MoveWindow(x + 104, y, 44, 22);
	if (m_edVel.GetSafeHwnd()) m_edVel.MoveWindow(x + 180, y, 44, 22);
	if (m_btnPropUpd.GetSafeHwnd()) m_btnPropUpd.MoveWindow(x + 232, y, 52, btnH);
	y += btnH + 6;
	x = pad;
	layoutCombo(m_stripLanes, x, y, 120, cbH, 140); x += 126;
	layoutCombo(m_stripStep, x, y, 88, cbH, 120); x += 94;
	layoutCombo(m_stripDraw, x, y, 108, cbH, 140);
	y += cbH + 6;
	if (m_ui.stripCount >= 1) {
		x = pad;
		const int avail = max(480, rc.Width() - pad * 2);
		const int hgtW = 88;
		const int kindW = min(260, max(200, (avail - hgtW * 3 - 24) / 3));
		layoutCombo(m_stripKind0, x, y, kindW, cbH, kindW + 64); x += kindW + 4;
		layoutCombo(m_stripHgt0, x, y, hgtW, cbH, 100); x += hgtW + 4;
		layoutCombo(m_stripKind1, x, y, kindW, cbH, kindW + 64); x += kindW + 4;
		layoutCombo(m_stripHgt1, x, y, hgtW, cbH, 100); x += hgtW + 4;
		layoutCombo(m_stripKind2, x, y, kindW, cbH, kindW + 64); x += kindW + 4;
		layoutCombo(m_stripHgt2, x, y, hgtW, cbH, 100);
		if (m_stripKind0.GetSafeHwnd()) m_stripKind0.ShowWindow(SW_SHOW);
		if (m_stripHgt0.GetSafeHwnd()) m_stripHgt0.ShowWindow(SW_SHOW);
		if (m_stripKind1.GetSafeHwnd())
			m_stripKind1.ShowWindow(m_ui.stripCount >= 2 ? SW_SHOW : SW_HIDE);
		if (m_stripHgt1.GetSafeHwnd())
			m_stripHgt1.ShowWindow(m_ui.stripCount >= 2 ? SW_SHOW : SW_HIDE);
		if (m_stripKind2.GetSafeHwnd())
			m_stripKind2.ShowWindow(m_ui.stripCount >= 3 ? SW_SHOW : SW_HIDE);
		if (m_stripHgt2.GetSafeHwnd())
			m_stripHgt2.ShowWindow(m_ui.stripCount >= 3 ? SW_SHOW : SW_HIDE);
		y += cbH + 6;
	} else {
		if (m_stripKind0.GetSafeHwnd()) m_stripKind0.ShowWindow(SW_HIDE);
		if (m_stripHgt0.GetSafeHwnd()) m_stripHgt0.ShowWindow(SW_HIDE);
		if (m_stripKind1.GetSafeHwnd()) m_stripKind1.ShowWindow(SW_HIDE);
		if (m_stripHgt1.GetSafeHwnd()) m_stripHgt1.ShowWindow(SW_HIDE);
		if (m_stripKind2.GetSafeHwnd()) m_stripKind2.ShowWindow(SW_HIDE);
		if (m_stripHgt2.GetSafeHwnd()) m_stripHgt2.ShowWindow(SW_HIDE);
		y += 2;
	}
	x = pad;
	place(m_btnMark, 36);
	place(m_btnLoopA, 28);
	place(m_btnLoopB, 28);
	place(m_btnLoopClr, 40);
	place(m_btnShowAll, 40);
	place(m_btnText, 64);
	place(m_btnRoll, 88);
	y += btnH + 6;
	x = pad;
	layoutCombo(m_midiInDev, x, y, 150, cbH); x += 156;
	layoutCombo(m_midiInMode, x, y, 96, cbH); x += 102;
	place(m_btnArr, 64);
	if (m_btnLayout.GetSafeHwnd()) m_btnLayout.ShowWindow(SW_HIDE);
	y += cbH + 4;
	if (m_status.GetSafeHwnd())
		m_status.MoveWindow(pad, y, max(200, rc.Width() - pad * 2), 20);
	y += 22;
	const int helpH = 52;
	const int rollH = m_ui.showRollSplit ? 140 : 0;
	const int bodyBotLimit = rc.Height() - pad - helpH - rollH;
	const int chromeBottom = y;
	m_bodyRc.SetRect(pad, chromeBottom, max(pad + 40, rc.Width()), max(chromeBottom + 40, bodyBotLimit));
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
	const int contentRight = m_bodyRc.right - sbW;
	m_gridRc.SetRect(m_trackRc.right, m_bodyRc.top, contentRight, m_trackRc.bottom);
	m_stripRc.SetRect(m_gridRc.left, m_trackRc.bottom, contentRight, bodyBot);
	if (rollH > 0)
		m_rollRc.SetRect(pad, m_bodyRc.bottom + 2, max(pad + 40, rc.Width()), m_bodyRc.bottom + rollH);
	else
		m_rollRc.SetRect(0, 0, 0, 0);
	m_ui.followViewW = max(1, m_gridRc.Width());
	if (m_helpBar.GetSafeHwnd())
		m_helpBar.MoveWindow(pad, rc.Height() - pad - helpH, max(200, rc.Width() - pad * 2), helpH);
	/* Scrollbars in dialog STYLE — no ShowScrollBar/ModifyStyle (OnSize re-entry crash). */
	UpdateScrollBars();
	m_bInLayout = FALSE;
	InvalidateRect(m_bodyRc, FALSE);
	if (m_rollRc.Width() > 0)
		InvalidateRect(m_rollRc, FALSE);
	{
		CRect chrome(0, cap, rc.Width(), m_bodyRc.top);
		if (chrome.Height() > 0)
			InvalidateRect(chrome, FALSE);
	}
	UpdateHelpBar();
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
	CRect chrome(0, cap, rc.right, (m_bodyRc.top > cap) ? m_bodyRc.top : rc.bottom);
	if (chrome.Height() > 0) {
#if CCUSTOM_AERO_SUPPORT
		CCC_FillRectAlpha(pDC->GetSafeHdc(), chrome, RGB(236, 240, 238), 255);
#else
		pDC->FillSolidRect(&chrome, RGB(236, 240, 238));
#endif
	}
	CRect below(0, m_bodyRc.bottom, rc.right, rc.bottom);
	if (below.Height() > 0) {
#if CCUSTOM_AERO_SUPPORT
		CCC_FillRectAlpha(pDC->GetSafeHdc(), below, RGB(236, 240, 238), 255);
#else
		pDC->FillSolidRect(&below, RGB(236, 240, 238));
#endif
	}
	if (m_ui.showRollSplit && m_rollRc.Width() > 4 && m_rollRc.Height() > 4) {
#if CCUSTOM_AERO_SUPPORT
		CCC_FillRectAlpha(pDC->GetSafeHdc(), m_rollRc, RGB(32, 34, 40), 255);
#else
		pDC->FillSolidRect(&m_rollRc, RGB(32, 34, 40));
#endif
	}
	CRect side(m_bodyRc.right, m_bodyRc.top, rc.right, m_bodyRc.bottom);
	if (side.Width() > 0) {
#if CCUSTOM_AERO_SUPPORT
		CCC_FillRectAlpha(pDC->GetSafeHdc(), side, RGB(236, 240, 238), 255);
#else
		pDC->FillSolidRect(&side, RGB(236, 240, 238));
#endif
	}
	return TRUE;
}

void CSasamiFmScoreDlg::UpdateScrollBars()
{
	if (!::IsWindow(m_hWnd)) return;
	const int pxBeat = m_ui.pxBeat > 0 ? m_ui.pxBeat : SC_PX_BEAT_DEFAULT;
	int contentW = ScStaffScoreContentWidthPx(&m_ui, m_doc.ev, m_doc.evCount, m_ui.contentTicks);
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
	if (m_sbDrag) return;
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
	if (m_sbDrag) return;
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
	ScStaffEnsureGlobalTempoFromDoc(&m_ui, m_doc.ev, m_doc.evCount, ScStaffBpmFromTempoT(m_doc.tempoT));
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
		ScStaffPaintTracks(mem, track, &m_ui, m_curCh, m_doc.ev, m_doc.evCount);
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
	CRect viewRel(grid.left, grid.top, grid.right, track.bottom);
	const int sbGutter = ScStaffScrollGutterW();
	if (sbGutter > 0 && bodyRel.Width() > sbGutter)
		mem.FillSolidRect(bodyRel.right - sbGutter, bodyRel.top, sbGutter, bodyRel.Height(), RGB(236, 240, 238));
	ScStaffPaintScrollThumbs(mem, clientRel, bodyRel, viewRel, &m_ui, max(1, m_gridRc.Width()), max(1, m_gridRc.Height()),
		m_doc.ev, m_doc.evCount);
	CCC_BlitStretchOpaque(dc.GetSafeHdc(), panel.left, panel.top, panel.Width(), panel.Height(),
		mem.GetSafeHdc(), 0, 0, panel.Width(), panel.Height());
	mem.SelectObject(old);
	if (m_ui.showRollSplit && m_rollRc.Width() > 8)
		ScPianoRollPaint(dc, m_rollRc, &m_rollView, m_doc.ev, m_doc.evCount, &m_ui, m_curCh);
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
	int snapTr = -1;
	if (ScStaffSnapMarkerToToneExcClick(m_gridRc, m_trackRc, &m_ui, m_doc.ev, m_doc.evCount, pt, &snapTr)) {
		m_curCh = snapTr;
		m_ch.SetCurSel(m_curCh);
		m_ui.visible[snapTr] = 1;
	}
	if (ScStaffIsInLayoutZone(m_gridRc, &m_ui, m_doc.ev, m_doc.evCount, pt)) {
		m_status.SetWindowText(LL14(L"譜表領域 — ここに音符は置けません",
			L"Layout zone — notes cannot be placed here", L"Zone de mesure — pas de notes", L"Zona layout — no note",
			L"Zona de compás — sin notas", L"보표 영역 — 음표 불가", L"谱表区域 — 不可放置音符",
			L"منطقة التخطيط — لا نغمات", L"Зона разметки — ноты нельзя", L"Layout-Zone — keine Noten",
			L"Zona de layout — sem notas", L"Layoutzone — geen noten", L"Strefa układu — brak nut", L"Düzen alanı — nota yok"));
		return;
	}
	int ctrlTr = -1;
	int ctrl = ScStaffHitScoreCtrl(m_gridRc, &m_ui, m_doc.ev, m_doc.evCount, 1, pt, &ctrlTr);
	int markTr = -1;
	int mark = ScStaffHitStaffMark(m_gridRc, &m_ui, m_doc.ev, m_doc.evCount, 1, pt, &markTr);
	int hitTr = -1;
	int hit = ScStaffHitNote(m_gridRc, &m_ui, m_doc.ev, m_doc.evCount, 1, pt, &hitTr);

	if (m_ui.tool == SC_TOOL_ERASER) {
		int del = -1;
		if (ctrl >= 0) del = ctrl;
		else if (mark >= 0) del = mark;
		else if (hit >= 0) del = hit;
		if (del >= 0) {
			ScStaffSelSetPrimary(&m_ui, del);
			ScStaffSelDelete(m_doc.ev, &m_doc.evCount, &m_ui);
			RefreshStrip();
			InvalidateRect(m_bodyRc, FALSE);
			m_status.SetWindowText(L"Deleted");
		}
		return;
	}

	if (ctrl >= 0) {
		m_ui.selEv = ctrl;
		m_curCh = m_doc.ev[ctrl].ch;
		if (m_ch.GetSafeHwnd()) m_ch.SetCurSel(m_curCh);
		if (ScStaffIsStaffMarkKind(m_doc.ev[ctrl].kind, 1)) {
			ScStaffSelSetPrimary(&m_ui, ctrl);
			m_status.SetWindowText(L"Mark selected — Delete/Backspace / Eraser removes |: :| 8va…");
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
			m_curCh = m_doc.ev[hit].ch;
			m_ch.SetCurSel(m_curCh);
			SyncPropFromSel();
			RefreshStrip();
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
		const int quant = ScStaffPlaceQuant(&m_ui);
		uint32_t tick = ScStaffXToTick(pt.x, m_ui.scrollX, ScStaffGridLeftPx(m_gridRc.left, &m_ui, m_doc.ev, m_doc.evCount), m_ui.pxBeat, quant, &m_ui, m_doc.ev, m_doc.evCount);
		int staffTop = ScStaffVisibleLaneStaffTop(m_gridRc, &m_ui, hitTr);
		if (staffTop < 0) return;
		int written = ScStaffYToMidiNoteTrack(&m_ui, hitTr, staffTop, pt.y, tick, m_doc.ev, m_doc.evCount) + m_accidental;
		const int oct = ScStaffOttavaOctaves(m_doc.ev, m_doc.evCount, hitTr, tick);
		int note = ScStaffWrittenToSounding(written, oct);
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
		HistPush();
		if (m_placeRest) {
			ScFmAddRest(&m_doc, tick, hitTr, m_ui.placeDur);
		} else if (m_ui.patternMode) {
			ScPatternPlace(m_doc.ev, &m_doc.evCount, SC_EV_MAX, hitTr,
				tick, note, 100, 100, m_ui.patternId, 1,
				m_ui.meterNumer, m_ui.meterDenom, SC_PPQN);
			for (int i = 0; i < m_doc.evCount; i++) {
				if (m_doc.ev[i].kind == SC_EV_NOTE && (int)m_doc.ev[i].ch == hitTr) {
					m_doc.ev[i].kind = SC_EV_FM_NOTE;
					m_doc.ev[i].a = MidiToFmNoteByte(m_doc.ev[i].a);
				} else if (m_doc.ev[i].kind == SC_EV_REST && (int)m_doc.ev[i].ch == hitTr) {
					m_doc.ev[i].kind = SC_EV_FM_REST;
				}
			}
			m_ui.markerTick = tick + (uint32_t)ScPatternSpanTicks(m_ui.patternId, 1,
				m_ui.meterNumer, m_ui.meterDenom, SC_PPQN);
			m_status.SetWindowText(ScPatternName(m_ui.patternId));
		} else if (m_ui.chordMode && hitTr != 6) {
			/* Poly chord on FM staff (visual/edit); hardware may still be mono. */
			ScChordPlaceAt(m_doc.ev, &m_doc.evCount, SC_EV_MAX, hitTr,
				tick, note, m_ui.placeDur, 100, 100, m_ui.chordType, m_ui.chordVoices,
				SC_EV_FM_NOTE, 1);
			m_ui.selEv = m_doc.evCount > 0 ? m_doc.evCount - 1 : -1;
			m_status.SetWindowText(ScChordTypeName(m_ui.chordType));
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
	if (pt.y < ScStaffGridBodyTop(m_gridRc.top, &m_ui)) return;
	if (ScStaffPtInScoreCtrlStrip(m_gridRc, &m_ui, pt, NULL)) return;
	if (ScStaffHitPartBand(m_gridRc, &m_ui, pt, NULL, NULL, NULL, NULL)) return;
	int hitTr = -1;
	int hit = ScStaffHitNote(m_gridRc, &m_ui, m_doc.ev, m_doc.evCount, 1, pt, &hitTr);
	if (hit != -2 || hitTr < 0) {
		int yCursor = ScStaffGridBodyTop(m_gridRc.top, &m_ui) - m_ui.scrollY;
		hitTr = -1;
		hit = -1;
		for (int tr = 0; tr < m_ui.trackCount; tr++) {
			const int rowH = ScStaffRowH(&m_ui, tr);
			const int rowTop = yCursor;
			yCursor += rowH;
			if (!m_ui.visible[tr]) continue;
			const int noteTop = ScStaffRowNoteAreaTop(&m_ui, tr, rowTop);
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
	m_ui.hoverTick = ScStaffXToTick(pt.x, m_ui.scrollX, ScStaffGridLeftPx(m_gridRc.left, &m_ui, m_doc.ev, m_doc.evCount), m_ui.pxBeat, quant, &m_ui, m_doc.ev, m_doc.evCount);
	m_ui.hoverNote = ScStaffWrittenToSounding(
		ScStaffYToMidiNoteTrack(&m_ui, hitTr, staffTop, pt.y, m_ui.hoverTick, m_doc.ev, m_doc.evCount) + m_accidental,
		ScStaffOttavaOctaves(m_doc.ev, m_doc.evCount, hitTr, m_ui.hoverTick));
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
	int builtinNeiro[32];
	for (int i = 0; i < 32; ++i) { voiceForTrack[i] = -1; builtinNeiro[i] = -1; }
	for (int i = 0; i < m_doc.evCount; i++) {
		const ScEvent& e = m_doc.ev[i];
		if (e.kind != SC_EV_FM_VOICE || e.tick != 0) continue;
		int ch = e.ch;
		if (ch < 0 || ch >= 32) continue;
		if (e.b == 1 && e.a < m_doc.voiceCount) { voiceForTrack[ch] = e.a; builtinNeiro[ch] = -1; }
		else if (e.b == 0) { builtinNeiro[ch] = e.a; voiceForTrack[ch] = -1; }
	}
	for (int ch = 0; ch < m_ui.trackCount && ch < 32; ++ch) {
		if (ScStaffIsFmSsgTrack(1, ch) || ScStaffIsOpnaRhythmTrack(1, ch)) {
			if (ScStaffIsOpnaRhythmTrack(1, ch)) {
				wcscpy_s(m_ui.vstLabel[ch], L"RHY pads");
				wcscpy_s(m_ui.progLabel[ch], L"BD SD TOP HH TOM RIM");
			} else {
				_snwprintf_s(m_ui.vstLabel[ch], _TRUNCATE, L"SSG%d", ch - 2);
				wcscpy_s(m_ui.progLabel[ch], L"(no @neiro)");
			}
			continue;
		}
		if (ch >= SC_FM_CH && ch < SC_FM_TOTAL) {
			const int mi = ch - SC_FM_CH;
			if (m_doc.pcmRelPath[mi][0]) {
				const wchar_t* leaf = wcsrchr(m_doc.pcmRelPath[mi], L'\\');
				if (!leaf) leaf = wcsrchr(m_doc.pcmRelPath[mi], L'/');
				_snwprintf_s(m_ui.vstLabel[ch], _TRUNCATE, L"PCM %u", (unsigned)m_doc.pcmSlot[mi]);
				wcsncpy_s(m_ui.progLabel[ch], leaf ? leaf + 1 : m_doc.pcmRelPath[mi], _TRUNCATE);
			} else {
				wcscpy_s(m_ui.vstLabel[ch], L"(no sample)");
				wcscpy_s(m_ui.progLabel[ch], L"Misao MIDI synth");
			}
			continue;
		}
		if (builtinNeiro[ch] >= 0) {
			_snwprintf_s(m_ui.vstLabel[ch], _TRUNCATE, L"@%d", builtinNeiro[ch]);
			wcscpy_s(m_ui.progLabel[ch], L"neiro builtin");
		} else {
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
}

void CSasamiFmScoreDlg::OpenVoiceEditor()
{
	if (m_curCh >= SC_FM_CH && m_curCh < SC_FM_TOTAL) {
		const int mi = m_curCh - SC_FM_CH;
		CFileDialog dlg(TRUE, L"wav", NULL, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
			L"PCM samples (*.wav;*.mp3;*.flac)|*.wav;*.mp3;*.flac|All files (*.*)|*.*||", this);
		if (dlg.DoModal() != IDOK) return;
		/* Preview / unsaved: keep full path (no copy). Save→fpy2 copies beside file. */
		wcsncpy_s(m_doc.pcmRelPath[mi], dlg.GetPathName(), _TRUNCATE);
		m_doc.pcmSlot[mi] = (uint8_t)mi;
		m_doc.pcmAbsUntilSave[mi] = 1;
		m_doc.needFpy2 = 1;
		const uint32_t atTick = m_ui.markerTick;
		ScFmAddPcmSample(&m_doc, atTick, m_curCh, m_doc.pcmSlot[mi]);
		ScFmAddVoiceSelect(&m_doc, atTick, m_curCh, m_doc.pcmSlot[mi], 0);
		RefreshToneLabels();
		InvalidateRect(m_trackRc, FALSE);
		CString st;
		st.Format(L"Misao PCM slot %u (full path until save): %s",
			(unsigned)m_doc.pcmSlot[mi], m_doc.pcmRelPath[mi]);
		m_status.SetWindowText(st);
		return;
	}
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
		HistPush();
		if (vi >= 0) {
			memcpy(m_doc.voices[vi], v, 25);
		} else {
			vi = ScFmAllocVoice(&m_doc, v);
			if (vi < 0) vi = 0;
		}
		/* Bind custom voice + audible TL at marker (same-tick: voice then vol, before notes). */
		const uint32_t atTick = m_ui.markerTick;
		ScFmAddVoiceSelect(&m_doc, atTick, m_curCh, vi, 1);
		ScFmAddVolTl(&m_doc, atTick, m_curCh, 16);
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
	CRect client; GetClientRect(&client);
	CPoint bp(point.x - m_bodyRc.left, point.y - m_bodyRc.top);
	CRect bodyRel(0, 0, m_bodyRc.Width(), m_bodyRc.Height());
	CRect viewRel(0, 0, m_gridRc.Width(), m_trackRc.bottom - m_bodyRc.top);
	int sbPos = 0;
	int sbHit = ScStaffHitScroll(bodyRel, bodyRel, viewRel, &m_ui,
		max(1, m_gridRc.Width()), max(1, m_gridRc.Height()), bp, &sbPos, m_doc.ev, m_doc.evCount);
	if (sbHit == 3) {
		ScStaffZoomPxBeat(&m_ui, -4);
		PersistUiGeom();
		UpdateScrollBars();
		InvalidateRect(m_bodyRc, FALSE);
		SetCapture();
		return;
	}
	if (sbHit == 4) {
		ScStaffZoomPxBeat(&m_ui, 4);
		PersistUiGeom();
		UpdateScrollBars();
		InvalidateRect(m_bodyRc, FALSE);
		SetCapture();
		return;
	}
	if (sbHit == 5) {
		ScStaffZoomStaffScale(&m_ui, -5);
		PersistUiGeom();
		UpdateScrollBars();
		InvalidateRect(m_bodyRc, FALSE);
		SetCapture();
		return;
	}
	if (sbHit == 6) {
		ScStaffZoomStaffScale(&m_ui, 5);
		PersistUiGeom();
		UpdateScrollBars();
		InvalidateRect(m_bodyRc, FALSE);
		SetCapture();
		return;
	}
	const int pageW = max(1, m_gridRc.Width());
	const int pageH = max(1, m_gridRc.Height());
	if (sbHit == 1) {
		m_sbDrag = 1;
		if (ScStaffPtOnVertThumb(&m_ui, pageH, bodyRel, viewRel, bp)) {
			m_sbDragScroll0 = m_ui.scrollY;
			m_sbDragAnchor = bp.y;
		} else
			m_ui.scrollY = sbPos;
		UpdateScrollBars();
		InvalidateRect(m_bodyRc, FALSE);
		SetCapture();
		return;
	}
	if (sbHit == 2) {
		m_sbDrag = 2;
		if (ScStaffPtOnHorzThumb(&m_ui, pageW, bodyRel, bodyRel, bp, m_doc.ev, m_doc.evCount)) {
			m_sbDragScroll0 = m_ui.scrollX;
			m_sbDragAnchor = bp.x;
		} else
			m_ui.scrollX = sbPos;
		UpdateScrollBars();
		InvalidateRect(m_bodyRc, FALSE);
		SetCapture();
		return;
	}
	if (m_stripDraw.GetSafeHwnd()) {
		int draw = m_stripDraw.GetCurSel();
		if (draw >= 0) m_ui.stripDraw = draw;
	}
	int lane = 0, scol = 0, sval = 0;
	int bandTr = 0, bandKind = 0, bandCol = 0, bandVal = 0, tempoCol = 0, tempoBpm = 0;
	if (ScStaffHitGlobalTempoBand(m_gridRc, &m_ui, point, &tempoCol, &tempoBpm, m_doc.ev, m_doc.evCount)) {
		ScStaffEnsureGlobalTempoFromDoc(&m_ui, m_doc.ev, m_doc.evCount, ScStaffBpmFromTempoT(m_doc.tempoT));
		m_ui.globalTempoStrip[tempoCol] = (uint8_t)tempoBpm;
		m_ui.stripLineAnchorCol = tempoCol;
		m_ui.stripLineAnchorVal = tempoBpm;
		m_ui.bandEditTrack = -2;
		m_ui.bandEditKind = 0;
		m_ui.bandEditCol = tempoCol;
		m_ui.dragEv = -3000;
		SetCapture();
		InvalidateRect(m_gridRc, FALSE);
		return;
	}
	if (ScStaffHitPartBand(m_gridRc, &m_ui, point, &bandTr, &bandKind, &bandCol, &bandVal, m_doc.ev, m_doc.evCount)) {
		ScStaffEnsurePartBandFromDoc(&m_ui, m_doc.ev, m_doc.evCount, bandTr, bandKind, m_ui.bandEditBuf);
		m_ui.bandEditBuf[bandCol] = (uint8_t)bandVal;
		m_ui.stripLineAnchorCol = bandCol;
		m_ui.stripLineAnchorVal = bandVal;
		m_ui.bandEditTrack = bandTr;
		m_ui.bandEditKind = bandKind;
		m_ui.bandEditCol = bandCol;
		m_ui.dragEv = -3000;
		SetCapture();
		InvalidateRect(m_gridRc, FALSE);
		return;
	}
	if (ScStaffHitStrip(m_stripRc, &m_ui, point, &lane, &scol, &sval)) {
		m_ui.stripLineAnchorCol = scol;
		m_ui.stripLineAnchorVal = sval;
		m_ui.stripLineLane = lane;
		m_ui.strip[lane][scol] = (uint8_t)sval;
		m_ui.dragEv = -2000 - lane;
		SetCapture();
		InvalidateRect(m_stripRc, FALSE);
		return;
	}
	SetCapture();
	uint32_t rulerTick = 0;
	if (ScStaffHitRulerTick(m_gridRc, &m_ui, point, &rulerTick, m_doc.ev, m_doc.evCount)) {
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
	int snapTr = -1;
	if (ScStaffSnapMarkerToToneExcClick(m_gridRc, m_trackRc, &m_ui, m_doc.ev, m_doc.evCount, point, &snapTr)) {
		m_curCh = snapTr;
		m_ch.SetCurSel(m_curCh);
		m_ui.visible[snapTr] = 1;
		savedata.sasamiFmPartMask = ScStaffPackPartMask(&m_ui);
		InvalidateRect(m_bodyRc, FALSE);
	}
	int gaugeZone = SC_GAUGE_NONE;
	const int gaugeTrack = ScStaffHitGauge(m_trackRc, &m_ui, point, &gaugeZone);
	if (gaugeTrack >= 0 && gaugeZone != SC_GAUGE_NONE) {
		m_curCh = gaugeTrack;
		m_ch.SetCurSel(m_curCh);
		OpenVoiceEditor();
		return;
	}
	int tr = ScStaffHitTrack(m_trackRc, &m_ui, point);
	if (tr >= 0) {
		int enTr = -1;
		if (ScStaffHitPartEnable(m_trackRc, &m_ui, point, &enTr) >= 0) {
			m_ui.visible[enTr] = m_ui.visible[enTr] ? 0 : 1;
			savedata.sasamiFmPartMask = ScStaffPackPartMask(&m_ui);
			UpdateScrollBars();
			InvalidateRect(m_bodyRc, FALSE);
			return;
		}
		m_curCh = tr;
		m_ch.SetCurSel(m_curCh);
		m_ui.visible[tr] = 1;
		RefreshStrip();
		InvalidateRect(m_bodyRc, FALSE);
		return;
	}
	PlaceOrEditAt(point);
}

void CSasamiFmScoreDlg::OnMouseMove(UINT nFlags, CPoint point)
{
	if ((nFlags & MK_LBUTTON) && m_sbDrag) {
		CPoint bp(point.x - m_bodyRc.left, point.y - m_bodyRc.top);
		CRect bodyRel(0, 0, m_bodyRc.Width(), m_bodyRc.Height());
		CRect viewRel(0, 0, m_gridRc.Width(), m_trackRc.bottom - m_bodyRc.top);
		const int pageW = max(1, m_gridRc.Width());
		const int pageH = max(1, m_gridRc.Height());
		if (m_sbDrag == 1)
			m_ui.scrollY = ScStaffMapVertScrollDrag(&m_ui, pageH, bodyRel, viewRel,
				bp.y, m_sbDragAnchor, m_sbDragScroll0);
		else
			m_ui.scrollX = ScStaffMapHorzScrollDrag(&m_ui, pageW, bodyRel, bodyRel,
				bp.x, m_sbDragAnchor, m_sbDragScroll0, m_doc.ev, m_doc.evCount);
		UpdateScrollBars();
		InvalidateRect(m_bodyRc, FALSE);
		return;
	}
	int gaugeZone = SC_GAUGE_NONE;
	const int onGauge = (ScStaffHitGauge(m_trackRc, &m_ui, point, &gaugeZone) >= 0 &&
		gaugeZone != SC_GAUGE_NONE);
	const int onCtrlStrip = ScStaffPtInScoreCtrlStrip(m_gridRc, &m_ui, point, NULL);
	CPoint bp(point.x - m_bodyRc.left, point.y - m_bodyRc.top);
	CRect bodyRel(0, 0, m_bodyRc.Width(), m_bodyRc.Height());
	CRect viewRel(0, 0, m_gridRc.Width(), m_trackRc.bottom - m_bodyRc.top);
	int sbDummy = 0;
	const int onScroll = ScStaffHitScroll(bodyRel, bodyRel, viewRel, &m_ui,
		max(1, m_gridRc.Width()), max(1, m_gridRc.Height()), bp, &sbDummy, m_doc.ev, m_doc.evCount);
	const int onBand = ScStaffPtInBandEditZone(m_gridRc, m_stripRc, &m_ui, point, m_doc.ev, m_doc.evCount);
	if (onScroll) {
		::SetCursor(::LoadCursor(NULL, IDC_ARROW));
		m_ui.hoverValid = 0;
	} else if (onBand) {
		HCURSOR bc = ScStaffDrawCursorPick(&m_drawCursors, m_ui.stripDraw);
		if (bc) ::SetCursor(bc);
	} else if (onGauge || onCtrlStrip) {
		::SetCursor(::LoadCursor(NULL, IDC_HAND));
		m_ui.hoverValid = 0;
	} else if (m_gridRc.PtInRect(point) && m_noteCur && !onScroll && !onBand)
		::SetCursor(m_noteCur);
	UpdateHover(point);
	UpdateHoverStatus(point);
	if (!(nFlags & MK_LBUTTON)) {
		if (m_ui.hoverValid)
			InvalidateRect(m_gridRc, FALSE);
		CCustomBlurDialogExBase::OnMouseMove(nFlags, point);
		return;
	}
	if (m_ui.dragEv == -3000) {
		int ncol = 0, nval = 0;
		if (m_ui.bandEditTrack == -2) {
			if (ScStaffBandColValFromPt(m_gridRc, &m_ui, 0, 0, 1, point, &ncol, &nval, m_doc.ev, m_doc.evCount)) {
				ScStaffBandDragStroke(&m_ui, m_ui.globalTempoStrip, m_ui.stripLineAnchorCol, m_ui.stripLineAnchorVal, ncol, nval, 1);
				m_ui.bandEditCol = ncol;
				InvalidateRect(m_gridRc, FALSE);
			}
		} else if (m_ui.bandEditTrack >= 0) {
			if (ScStaffBandColValFromPt(m_gridRc, &m_ui, m_ui.bandEditTrack, m_ui.bandEditKind, 0, point, &ncol, &nval, m_doc.ev, m_doc.evCount)) {
				ScStaffBandDragStroke(&m_ui, m_ui.bandEditBuf, m_ui.stripLineAnchorCol, m_ui.stripLineAnchorVal, ncol, nval, 0);
				m_ui.bandEditCol = ncol;
				InvalidateRect(m_gridRc, FALSE);
			}
		}
	} else if (m_ui.dragEv <= -2000) {
		int lane = -1, scol = 0, sval = 0;
		if (!ScStaffHitStrip(m_stripRc, &m_ui, point, &lane, &scol, &sval)) {
			lane = -(m_ui.dragEv + 2000);
			if (lane < 0) lane = 0;
			if (lane >= SC_STRIP_LANES_MAX) lane = 0;
			const int left = ScStaffGridLeftPx(m_stripRc.left, &m_ui, m_doc.ev, m_doc.evCount);
			const int step = ScStaffStripStepTicks(&m_ui);
			const int colMax = ScStaffStripColCount(&m_ui);
			const int pxBeat = m_ui.pxBeat > 0 ? m_ui.pxBeat : SC_PX_BEAT_DEFAULT;
			uint32_t tick = ScStaffXToTick(point.x, m_ui.scrollX, left, pxBeat, step);
			scol = ScStaffStripTickToCol(&m_ui, tick);
			if (scol < 0) scol = 0;
			if (scol >= colMax) scol = colMax - 1;
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
		if (ScStaffPtInBandEditZone(m_gridRc, m_stripRc, &m_ui, pt, m_doc.ev, m_doc.evCount)) {
			HCURSOR bc = ScStaffDrawCursorPick(&m_drawCursors, m_ui.stripDraw);
			if (bc) { ::SetCursor(bc); return TRUE; }
		}
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
	if (m_ui.dragEv == -3000) {
		if (m_ui.bandEditTrack == -2) {
			ScStaffApplyGlobalTempoToDocFm(&m_doc, &m_ui);
		} else if (m_ui.bandEditTrack >= 0) {
			ScStaffApplyPartBandToDocFm(&m_doc, m_ui.bandEditTrack, m_ui.bandEditKind, m_ui.bandEditBuf, &m_ui);
		}
		m_ui.bandEditTrack = -1;
		m_ui.stripLineAnchorCol = -1;
		InvalidateRect(m_gridRc, FALSE);
		m_ui.dragEv = -1;
	} else if (m_ui.dragEv <= -2000) {
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
	int tempoCol = 0, tempoBpm = 0;
	if (ScStaffHitGlobalTempoBand(m_gridRc, &m_ui, point, &tempoCol, &tempoBpm, m_doc.ev, m_doc.evCount)) {
		int bpm = tempoBpm;
		if (CSasamiSimpleInputDlg::AskNumber(this, L"テンポ", L"BPM (40–300)", bpm, 40, 300, &bpm) == IDOK) {
			ScStaffEnsureGlobalTempoFromDoc(&m_ui, m_doc.ev, m_doc.evCount, ScStaffBpmFromTempoT(m_doc.tempoT));
			ScStaffBandFillHoldSegment(m_ui.globalTempoStrip, ScStaffStripColCount(&m_ui), tempoCol, bpm, 40, 255);
			ScStaffApplyGlobalTempoToDocFm(&m_doc, &m_ui);
			InvalidateRect(m_gridRc, FALSE);
		}
		return;
	}
	int bandTr = 0, bandKind = 0, bandCol = 0, bandVal = 0;
	if (ScStaffHitPartBand(m_gridRc, &m_ui, point, &bandTr, &bandKind, &bandCol, &bandVal, m_doc.ev, m_doc.evCount)) {
		int val = bandVal;
		if (CSasamiSimpleInputDlg::AskNumber(this, ScStaffPartBandName(bandKind),
			L"Value (0–127)", val, 0, 127, &val) == IDOK) {
			ScStaffEnsurePartBandFromDoc(&m_ui, m_doc.ev, m_doc.evCount, bandTr, bandKind, m_ui.bandEditBuf);
			m_ui.bandEditBuf[bandCol] = (uint8_t)val;
			ScStaffApplyPartBandToDocFm(&m_doc, bandTr, bandKind, m_ui.bandEditBuf, &m_ui);
			InvalidateRect(m_gridRc, FALSE);
		}
		return;
	}
	int ctrlTr = -1;
	int ctrl = ScStaffHitScoreCtrl(m_gridRc, &m_ui, m_doc.ev, m_doc.evCount, 1, point, &ctrlTr);
	if (ctrl >= 0) {
		m_ui.selEv = ctrl;
		m_curCh = m_doc.ev[ctrl].ch;
		if (m_ch.GetSafeHwnd()) m_ch.SetCurSel(m_curCh);
		if (m_doc.ev[ctrl].kind == SC_EV_FM_VOICE)
			m_ui.markerTick = m_doc.ev[ctrl].tick;
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
		ScStaffSavePartStrip(&m_ui, m_curCh);
		m_curCh = s;
		m_ui.visible[s] = 1;
		ScStaffLoadPartStrip(&m_ui, m_curCh);
		SyncStripCombos();
		m_ui.helpTopic = SC_HELP_CH_PART;
		RefreshStrip();
		LayoutChrome();
		InvalidateRect(m_bodyRc, FALSE);
		UpdateHelpBar();
	}
}

void CSasamiFmScoreDlg::OnBnClickedPencil()
{
	m_ui.tool = SC_TOOL_PENCIL;
	m_ui.helpTopic = SC_HELP_PENCIL;
	UpdateNoteCursor();
	UpdateHelpBar();
}
void CSasamiFmScoreDlg::OnBnClickedErase()
{
	m_ui.tool = SC_TOOL_ERASER;
	m_ui.helpTopic = SC_HELP_ERASER;
	UpdateNoteCursor();
	UpdateHelpBar();
}
void CSasamiFmScoreDlg::OnBnClickedSel()
{
	m_ui.tool = SC_TOOL_SELECT;
	m_ui.helpTopic = SC_HELP_SELECT;
	UpdateNoteCursor();
	UpdateHelpBar();
}

void CSasamiFmScoreDlg::OnBnClickedPal()
{
	CRect r;
	m_btnPal.GetWindowRect(&r);
	CSasamiNotePaletteDlg::OpenNear(this, CPoint(r.left, r.bottom + 4));
}

LRESULT CSasamiFmScoreDlg::OnPalDur(WPARAM w, LPARAM l)
{
	if ((l & SASAMI_PAL_CMD) == SASAMI_PAL_CMD) {
		const int cmdId = (int)(l & 0xFF);
		if ((cmdId >= SASAMI_PAL_CMD_METER_24 && cmdId <= SASAMI_PAL_CMD_TR_ALL_MINUS)
			|| (cmdId >= SASAMI_PAL_CMD_METER_14 && cmdId <= SASAMI_PAL_CMD_METER_74)
			|| (cmdId >= SASAMI_PAL_CMD_KEY_BASE && cmdId < SASAMI_PAL_CMD_KEY_BASE + 15)) {
			ApplyLayoutPalCmd(cmdId);
			return 0;
		}
		switch (cmdId) {
		case SASAMI_PAL_CMD_FIT: m_ui.helpTopic = SC_HELP_FIT; break;
		case SASAMI_PAL_CMD_TEMPO: m_ui.helpTopic = SC_HELP_TEMPO; break;
		case SASAMI_PAL_CMD_PENCIL: m_ui.helpTopic = SC_HELP_PENCIL; break;
		case SASAMI_PAL_CMD_ERASE: m_ui.helpTopic = SC_HELP_ERASER; break;
		case SASAMI_PAL_CMD_SEL: m_ui.helpTopic = SC_HELP_SELECT; break;
		case SASAMI_PAL_CMD_MARK: m_ui.helpTopic = SC_HELP_MARK; break;
		case SASAMI_PAL_CMD_TIE: m_ui.helpTopic = SC_HELP_TIE; break;
		case SASAMI_PAL_CMD_LOOP_START: m_ui.helpTopic = SC_HELP_LOOP_START; break;
		case SASAMI_PAL_CMD_LOOP_END: m_ui.helpTopic = SC_HELP_LOOP_END; break;
		case SASAMI_PAL_CMD_PED_ON: m_ui.helpTopic = SC_HELP_PED_ON; break;
		case SASAMI_PAL_CMD_PED_OFF: m_ui.helpTopic = SC_HELP_PED_OFF; break;
		case SASAMI_PAL_CMD_MARK_REPLACE: m_ui.helpTopic = SC_HELP_REPLACE; break;
		case SASAMI_PAL_CMD_MARK_STACK: m_ui.helpTopic = SC_HELP_NEST; break;
		case SASAMI_PAL_CMD_OTTAVA_LOCO: m_ui.helpTopic = SC_HELP_LOCO; break;
		case SASAMI_PAL_CMD_OTTAVA_8VA: case SASAMI_PAL_CMD_OTTAVA_8VB:
		case SASAMI_PAL_CMD_OTTAVA_16VA: case SASAMI_PAL_CMD_OTTAVA_16VB:
		case SASAMI_PAL_CMD_OTTAVA_32VA: case SASAMI_PAL_CMD_OTTAVA_32VB:
			m_ui.helpTopic = SC_HELP_OTTAVA; break;
		default: break;
		}
		UpdateHelpBar();
		switch (cmdId) {
		case SASAMI_PAL_CMD_FIT:
			m_ui.snapFit ^= 1;
			InvalidateRect(m_gridRc, FALSE);
			return 0;
		case SASAMI_PAL_CMD_TEMPO:
			m_ui.tool = SC_TOOL_TEMPO;
			UpdateNoteCursor();
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
			m_status.SetWindowText(L"マーク配置: 1重（同じ位置にもう一度＝消す）");
			return 0;
		case SASAMI_PAL_CMD_MARK_STACK:
			m_ui.markStack = 1;
			savedata.sasamiMarkStack = 1;
			m_status.SetWindowText(L"マーク配置: ネスト（同じ位置に積み上げ）");
			return 0;
		case SASAMI_PAL_CMD_TIE:
			m_ui.tool = SC_TOOL_TIE;
			UpdateNoteCursor();
			return 0;
		case SASAMI_PAL_CMD_LOOP_START:
		case SASAMI_PAL_CMD_LOOP_END: {
			uint32_t atTick = m_ui.markerTick;
			const int ch = m_curCh;
			if (m_ui.tool == SC_TOOL_ERASER) {
				const uint8_t kind = (cmdId == SASAMI_PAL_CMD_LOOP_END) ? SC_EV_FM_LOOP_END : SC_EV_FM_LOOP_START;
				const int n = ScDeleteMarksAt(m_doc.ev, &m_doc.evCount, atTick, ch, kind);
				if (n > 0) {
					RefreshStrip();
					InvalidateRect(m_bodyRc, FALSE);
					CString st;
					st.Format(L"Deleted %d mark(s) at red bar (FM %d)", n, ch + 1);
					m_status.SetWindowText(st);
				} else {
					m_status.SetWindowText(L"No |: / :| at red bar — Eraser+click staff also works");
				}
				return 0;
			}
			int ok = 0;
			uint8_t placedKind = SC_EV_FM_LOOP_START;
			if (cmdId == SASAMI_PAL_CMD_LOOP_START) {
				placedKind = SC_EV_FM_LOOP_START;
				if (!m_ui.markStack
					&& ScMarkKindExists(m_doc.ev, m_doc.evCount, atTick, (uint8_t)ch, SC_EV_FM_LOOP_START)) {
					ok = ScFmAddLoopStart(&m_doc, atTick, ch, 2, 0);
				} else {
					int n = 2;
					if (CSasamiSimpleInputDlg::AskNumber(this, L"ループ開始 |:",
						L"繰り返し回数 (1–99)", 2, 1, 99, &n) != IDOK)
						return 0;
					ok = ScFmAddLoopStart(&m_doc, atTick, ch, n, m_ui.markStack);
				}
			} else {
				placedKind = SC_EV_FM_LOOP_END;
				ok = ScFmAddLoopEnd(&m_doc, atTick, ch, m_ui.markStack);
			}
			if (ok) {
				m_ui.visible[ch] = 1;
				RefreshStrip();
				InvalidateRect(m_bodyRc, FALSE);
				CString st;
				const int still = ScMarkKindExists(m_doc.ev, m_doc.evCount, atTick, (uint8_t)ch, placedKind);
				if (!m_ui.markStack && !still)
					st.Format(L"FM ch %d mark removed @ %u", ch + 1, (unsigned)atTick);
				else
					st.Format(L"FM ch %d mark @ %u (red bar)", ch + 1, (unsigned)atTick);
				m_status.SetWindowText(st);
			}
			return 0;
		}
		case SASAMI_PAL_CMD_PED_ON:
		case SASAMI_PAL_CMD_PED_OFF:
			m_status.SetWindowText(L"Pedal is MIDI-only (not used on FM score).");
			return 0;
		case SASAMI_PAL_CMD_OTTAVA_8VA:
		case SASAMI_PAL_CMD_OTTAVA_8VB:
		case SASAMI_PAL_CMD_OTTAVA_16VA:
		case SASAMI_PAL_CMD_OTTAVA_16VB:
		case SASAMI_PAL_CMD_OTTAVA_32VA:
		case SASAMI_PAL_CMD_OTTAVA_32VB:
		case SASAMI_PAL_CMD_OTTAVA_LOCO: {
			uint32_t atTick = m_ui.markerTick;
			const int ch = m_curCh;
			int oct = 0;
			switch ((int)(l & 0xFF)) {
			case SASAMI_PAL_CMD_OTTAVA_8VA: oct = 1; break;
			case SASAMI_PAL_CMD_OTTAVA_8VB: oct = -1; break;
			case SASAMI_PAL_CMD_OTTAVA_16VA: oct = 2; break;
			case SASAMI_PAL_CMD_OTTAVA_16VB: oct = -2; break;
			case SASAMI_PAL_CMD_OTTAVA_32VA: oct = 3; break;
			case SASAMI_PAL_CMD_OTTAVA_32VB: oct = -3; break;
			default: oct = 0; break;
			}
			if (m_ui.tool == SC_TOOL_ERASER) {
				int n = ScDeleteMarksAt(m_doc.ev, &m_doc.evCount, atTick, ch, SC_EV_OTTAVA);
				n += ScDeleteMarksAt(m_doc.ev, &m_doc.evCount, atTick, ch, SC_EV_OTTAVA_END);
				if (n > 0) {
					RefreshStrip();
					InvalidateRect(m_bodyRc, FALSE);
					m_status.SetWindowText(L"Deleted ottava/loco at red bar");
				} else {
					m_status.SetWindowText(L"No ottava at red bar");
				}
				return 0;
			}
			int ok = (oct == 0)
				? ScFmAddOttavaEnd(&m_doc, atTick, ch, m_ui.markStack)
				: ScFmAddOttava(&m_doc, atTick, ch, oct, m_ui.markStack);
			if (ok) {
				m_ui.visible[ch] = 1;
				ScStaffUpdateContentExtent(&m_ui, m_doc.ev, m_doc.evCount);
				RefreshStrip();
				InvalidateRect(m_bodyRc, FALSE);
				CString st;
				const uint8_t kind = (oct == 0) ? SC_EV_OTTAVA_END : SC_EV_OTTAVA;
				const int still = ScMarkKindExists(m_doc.ev, m_doc.evCount, atTick, (uint8_t)ch, kind);
				if (!m_ui.markStack && !still)
					st.Format(L"FM ch %d %s removed @ %u", ch + 1, ScStaffOttavaLabel(oct), (unsigned)atTick);
				else
					st.Format(L"FM ch %d %s @ red bar tick %u", ch + 1, ScStaffOttavaLabel(oct), (unsigned)atTick);
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
	if ((int)w > 0) m_ui.placeDur = (int)w;
	if (m_ui.placeDur < 1) m_ui.placeDur = 1;
	if (m_placeRest) m_ui.helpTopic = SC_HELP_REST;
	else if (m_accidental != 0) m_ui.helpTopic = SC_HELP_ACCIDENTAL;
	else if (m_ui.tuplet) m_ui.helpTopic = SC_HELP_TUPLET;
	else m_ui.helpTopic = SC_HELP_PAL_NOTE;
	UpdateNoteCursor();
	UpdateHelpBar();
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
	ScStaffEnsureGlobalTempoFromDoc(&m_ui, m_doc.ev, m_doc.evCount, ScStaffBpmFromTempoT(m_doc.tempoT));
	const int colMax = ScStaffStripColCount(&m_ui);
	int col = ScStaffStripTickToCol(&m_ui, m_ui.markerTick);
	if (col < 0) col = 0;
	if (col >= colMax) col = colMax - 1;
	int bpm = (int)m_ui.globalTempoStrip[col];
	if (bpm < 40) bpm = ScStaffBpmFromTempoT(m_doc.tempoT);
	if (CSasamiSimpleInputDlg::AskNumber(this, L"テンポ", L"BPM at marker through next change (40–300)", bpm, 40, 300, &bpm) != IDOK)
		return;
	ScStaffApplyTempoAtMarkerFm(&m_doc, &m_ui, m_doc.ev, m_doc.evCount, m_ui.markerTick, bpm);
	CString s;
	s.Format(L"Marker: BPM %d cols %d–%d", bpm, col, ScStaffBandSegmentEndCol(m_ui.globalTempoStrip, colMax, col));
	m_status.SetWindowText(s);
	InvalidateRect(m_gridRc, FALSE);
}

void CSasamiFmScoreDlg::OnBnClickedVoice()
{
	if (ScStaffIsFmSsgTrack(1, m_curCh)) {
		m_ui.clef[m_curCh] = (m_ui.clef[m_curCh] + 1) % 3; /* G/F/grand — no Voice chip */
		UpdateScrollBars();
		InvalidateRect(m_bodyRc, FALSE);
		m_status.SetWindowText(LL14(
			L"SSG: 譜表切替（@neiro/Voiceなし）",
			L"SSG: clef cycle (no @neiro / Voice)",
			L"SSG: cycle clé (pas @neiro/Voice)",
			L"SSG: ciclo chiave (no @neiro/Voice)",
			L"SSG: ciclo clave (sin @neiro/Voice)",
			L"SSG: 음자리표 전환 (@neiro/Voice 없음)",
			L"SSG: 谱号循环（无@neiro/Voice）",
			L"SSG: تبديل المفتاح (بدون @neiro)",
			L"SSG: смена ключа (без @neiro/Voice)",
			L"SSG: Schlüssel wechseln (kein @neiro/Voice)",
			L"SSG: ciclo de clave (sem @neiro/Voice)",
			L"SSG: sleutel wisselen (geen @neiro/Voice)",
			L"SSG: zmiana klucza (bez @neiro/Voice)",
			L"SSG: anahtar döngüsü (@neiro/Voice yok)"));
		return;
	}
	if (ScStaffIsOpnaRhythmTrack(1, m_curCh)) {
		m_ui.clef[m_curCh] = 3;
		m_status.SetWindowText(LL14(
			L"RHY: ドラム譜表固定（BD SD TOP HH TOM RIM）",
			L"RHY: drum staff fixed (BD SD TOP HH TOM RIM)",
			L"RHY: portée batterie fixe",
			L"RHY: pentagramma batteria fisso",
			L"RHY: pentagrama batería fijo",
			L"RHY: 드럼 보표 고정",
			L"RHY: 鼓谱固定",
			L"RHY: مدرج طبل ثابت",
			L"RHY: барабанный стан фиксирован",
			L"RHY: Schlagzeugsystem fest",
			L"RHY: pauta de bateria fixa",
			L"RHY: drumnotenbalk vast",
			L"RHY: pięciolinia perkusyjna stała",
			L"RHY: davul porte sabit"));
		InvalidateRect(m_bodyRc, FALSE);
		return;
	}
	CCustomPopupMenu menu;
	menu.SetAeroMode(FALSE);
	menu.AddCommand(9500, LL14(L"@neiro 選択…", L"Pick @neiro…", L"Choisir @neiro…", L"Scegli @neiro…", L"Elegir @neiro…", L"@neiro 선택…", L"选择@neiro…", L"اختيار @neiro…", L"Выбрать @neiro…", L"@neiro wählen…", L"Escolher @neiro…", L"@neiro kiezen…", L"Wybierz @neiro…", L"@neiro seç…"));
	menu.AddCommand(9501, LL14(L"カスタムVoice編集…", L"Edit custom Voice…", L"Éditer Voice…", L"Modifica Voice…", L"Editar Voice…", L"커스텀 Voice…", L"编辑自定义Voice…", L"تحرير Voice…", L"Ред. Voice…", L"Voice bearbeiten…", L"Editar Voice…", L"Voice bewerken…", L"Edytuj Voice…", L"Voice düzenle…"));
	CPoint pt; GetCursorPos(&pt);
	const UINT cmd = menu.Track(pt, this);
	if (cmd == 9500) PickBuiltinNeiro();
	else if (cmd == 9501) OpenVoiceEditor();
}

void CSasamiFmScoreDlg::PickBuiltinNeiro()
{
	int n = 0;
	if (CSasamiSimpleInputDlg::AskNumber(this,
		LL14(L"@neiro", L"@neiro", L"@neiro", L"@neiro", L"@neiro", L"@neiro", L"@neiro", L"@neiro", L"@neiro", L"@neiro", L"@neiro", L"@neiro", L"@neiro", L"@neiro"),
		LL14(L"内蔵音色番号 0〜255", L"builtin neiro index 0..255", L"index neiro 0..255", L"indice neiro 0..255", L"índice neiro 0..255",
			L"내장 음색 번호 0..255", L"内置音色编号 0..255", L"فهرس neiro 0..255", L"индекс neiro 0..255", L"Neiro-Index 0..255", L"índice neiro 0..255", L"neiro-index 0..255", L"indeks neiro 0..255", L"neiro indeksi 0..255"),
		0, 0, 255, &n) != IDOK)
		return;
	HistPush();
	ScFmAddVoiceSelect(&m_doc, m_ui.markerTick, m_curCh, n, 0);
	RefreshToneLabels();
	InvalidateRect(m_bodyRc, FALSE);
	CString st;
	st.Format(LL14(L"@%d をパートに設定（内蔵）", L"@%d on part (builtin neiro)", L"@%d sur partie (neiro intégré)", L"@%d sulla parte (neiro builtin)", L"@%d en parte (neiro integrado)",
		L"@%d 파트에 설정 (내장)", L"@%d 已设到声部（内置）", L"@%d على الجزء (مدمج)", L"@%d на партии (встроенный)", L"@%d auf Part (eingebaut)", L"@%d na parte (builtin)", L"@%d op partij (ingebouwd)", L"@%d na partii (wbudowany)", L"@%d partide (yerleşik)"), n);
	m_status.SetWindowText(st);
}

int CSasamiFmScoreDlg::BuildToTemp(wchar_t* outPath, int outCch, uint32_t fromTick)
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
	if (fromTick > 0 && tmp->evCount > 0) {
		ScEvent* shifted = (ScEvent*)HeapAlloc(GetProcessHeap(), 0, sizeof(ScEvent) * (size_t)SC_EV_MAX);
		if (shifted) {
			int nn = 0;
			ScStaffCopyEventsFromMarker(tmp->ev, tmp->evCount, fromTick, shifted, SC_EV_MAX, &nn);
			memcpy(tmp->ev, shifted, sizeof(ScEvent) * (size_t)nn);
			tmp->evCount = nn;
			HeapFree(GetProcessHeap(), 0, shifted);
		}
	}
	SasamiWriteFm* wr = (SasamiWriteFm*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SasamiWriteFm));
	if (!wr) { HeapFree(GetProcessHeap(), 0, tmp); return 0; }
	if (!ScFmDocToWrite(tmp, wr)) {
		HeapFree(GetProcessHeap(), 0, wr);
		HeapFree(GetProcessHeap(), 0, tmp);
		const wchar_t* why = ScGetLastWriteErr();
		m_status.SetWindowText(why && why[0] ? why : L"FPY build failed");
		return 0;
	}
	HeapFree(GetProcessHeap(), 0, tmp);
	uint8_t* bin = (uint8_t*)HeapAlloc(GetProcessHeap(), 0, SASAMI_WRITE_MAX);
	if (!bin) { SasamiWriteFmClear(wr); HeapFree(GetProcessHeap(), 0, wr); return 0; }
	uint32_t sz = SasamiBuildFpy(wr, bin, SASAMI_WRITE_MAX);
	const int isFpy2 = wr->fpy2 ? 1 : 0;
	SasamiWriteFmClear(wr); HeapFree(GetProcessHeap(), 0, wr);
	if (!sz) {
		HeapFree(GetProcessHeap(), 0, bin);
		return 0;
	}
	wchar_t dir[MAX_PATH];
	GetTempPathW(MAX_PATH, dir);
	_snwprintf_s(outPath, outCch, _TRUNCATE, L"%sogg_sasami_fm.%s", dir, isFpy2 ? L"fpy2" : L"fpy");
	if (!SasamiWriteFileW(outPath, bin, sz)) {
		HeapFree(GetProcessHeap(), 0, bin);
		return 0;
	}
	wcsncpy_s(m_lastOut, outPath, _TRUNCATE);
	HeapFree(GetProcessHeap(), 0, bin);
	CString ok;
	ok.Format(L"OK %u bytes -> %s%s", sz, outPath, isFpy2 ? L" (FPY2 nest)" : L"");
	m_status.SetWindowText(ok);
	return 1;
}

void CSasamiFmScoreDlg::OnBnClickedSave()
{
	wchar_t path[MAX_PATH];
	if (!BuildToTemp(path, MAX_PATH)) return;
	const int wantFpy2 = ScFmDocNeedsFpy2(&m_doc) ? 1 : 0;
	CFileDialog dlg(FALSE, wantFpy2 ? L"fpy2" : L"fpy",
		wantFpy2 ? L"song.fpy2" : L"song.fpy",
		OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
		L"FPY2/FPY (*.fpy2;*.fpy)|*.fpy2;*.fpy||", this);
	if (dlg.DoModal() != IDOK) return;
	CString dest = dlg.GetPathName();
	/* Save: copy PCM beside fpy2 and rewrite paths to relative. */
	if (wantFpy2 || dest.Right(5).CompareNoCase(L".fpy2") == 0) {
		ScFmDocCommitPcmBesideFpy2(&m_doc, dest);
		/* Rebuild so embedded cmd26 paths are relative */
		if (!BuildToTemp(path, MAX_PATH)) return;
	}
	CopyFileW(path, dest, FALSE);
	wcsncpy_s(m_lastOut, dest, _TRUNCATE);
	m_status.SetWindowText(dest);
}

void CSasamiFmScoreDlg::SyncMeterFromDoc()
{
	ScStaffSetMeter(&m_ui, m_doc.numer > 0 ? m_doc.numer : 4, m_doc.denom > 0 ? m_doc.denom : 4);
}

void CSasamiFmScoreDlg::RefreshPartEnabled(int autoUsedParts)
{
	if (autoUsedParts)
		ScStaffAutoEnableUsedParts(&m_ui, NULL, &m_doc, m_doc.ev, m_doc.evCount);
	else
		ScStaffRefreshPartEnabled(&m_ui, NULL, &m_doc, m_doc.ev, m_doc.evCount, savedata.sasamiFmPartMask);
	savedata.sasamiFmPartMask = ScStaffPackPartMask(&m_ui);
	if (m_btnShowAll.GetSafeHwnd())
		m_btnShowAll.SetWindowText(ScStaffIsExtendedChannelView(&m_ui) ? L"16ch" : L"32ch");
	UpdateScrollBars();
}

void CSasamiFmScoreDlg::LoadFromDoc(const ScFmDoc& src)
{
	m_doc = src;
	m_ui.selEv = -1;
	m_ui.markerTick = 0;
	m_ui.playheadTick = 0;
	SyncMeterFromDoc();
	RefreshToneLabels();
	RefreshPartEnabled(1);
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
		L"Text/MML/DAT (*.mml;*.mml2;*.mml3;*.m;*.txt;*.dat;*.f)|*.mml;*.mml2;*.mml3;*.m;*.txt;*.dat;*.f|All|*.*||", this);
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
	if (!BuildToTemp(path, MAX_PATH, m_ui.markerTick)) return;
	m_status.SetWindowText(LL14(L"譜面プレビュー…", L"Score preview…", L"Aperçu partition…", L"Anteprima partitura…", L"Vista previa…",
		L"악보 미리듣기…", L"乐谱预览…", L"معاينة النوتة…", L"Превью партитуры…", L"Partitur-Vorschau…", L"Prévia da partitura…", L"Partituurvoorbeeld…", L"Podgląd partytury…", L"Partisyon önizleme…"));
	int ok = ScStaffPreviewViaHost(path, &m_ui, m_doc.tempoT);
	if (!ok)
		ok = ScStaffPreviewViaWavout(path, &m_ui, m_doc.tempoT);
	if (!ok) {
		m_ui.previewActive = 0;
		m_status.SetWindowText(LL14(L"プレビュー失敗（ホスト／wavout）", L"Preview failed (host / wavout)", L"Échec aperçu", L"Anteprima non riuscita", L"Falló la vista previa",
			L"미리듣기 실패", L"预览失败", L"فشل المعاينة", L"Превью не удалось", L"Vorschau fehlgeschlagen", L"Falha na prévia", L"Voorbeeld mislukt", L"Podgląd nieudany", L"Önizleme başarısız"));
		return;
	}
	SetTimer(1, 33, NULL);
	CString st;
	st.Format(LL14(L"FMプレビュー tick %u — Spaceで停止", L"FM preview tick %u — Space to stop", L"Aperçu FM tick %u", L"Anteprima FM tick %u", L"Vista previa FM tick %u",
		L"FM 미리듣기 tick %u", L"FM预览 tick %u", L"معاينة FM tick %u", L"FM превью tick %u", L"FM-Vorschau tick %u", L"Prévia FM tick %u", L"FM-voorbeeld tick %u", L"Podgląd FM tick %u", L"FM önizleme tick %u"),
		(unsigned)m_ui.markerTick);
	m_status.SetWindowText(st);
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
	if (ScStaffIsExtendedChannelView(&m_ui))
		ScStaffSetChannelView16(&m_ui);
	else
		ScStaffSetChannelViewAll(&m_ui);
	savedata.sasamiFmPartMask = ScStaffPackPartMask(&m_ui);
	m_btnShowAll.SetWindowText(ScStaffIsExtendedChannelView(&m_ui) ? L"16ch" : L"32ch");
	UpdateScrollBars();
	InvalidateRect(m_bodyRc, FALSE);
}

void CSasamiFmScoreDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == kScFmPreviewTimer) {
		if (ScStaffSyncPreviewPlayhead(&m_ui, m_doc.tempoT)) {
			ApplyFollowScroll();
			UpdateScrollBars();
			InvalidateRect(m_bodyRc, FALSE);
			if (m_ui.showRollSplit) InvalidateRect(m_rollRc, FALSE);
		}
		return;
	}
	if (nIDEvent == kScFmMetroTimer) {
		if (m_midiIn.recording && m_midiIn.metroOn) {
			int mn = m_ui.meterNumer > 0 ? m_ui.meterNumer : 4;
			int md = m_ui.meterDenom > 0 ? m_ui.meterDenom : 4;
			ScStaffMeterAtTick(m_doc.ev, m_doc.evCount, m_ui.playheadTick, mn, md, &mn, &md);
			ScScoreMidiInMetroTick(&m_midiIn, m_metroBeat);
			m_metroBeat = (m_metroBeat + 1) % max(1, mn);
			m_ui.playheadTick = ScScoreMidiInNowTick(&m_midiIn, SC_PPQN);
			m_ui.markerTick = m_ui.playheadTick;
			ApplyFollowScroll();
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

void CSasamiFmScoreDlg::OnBnClickedHelp() { CSasamiCmdHelpDlg::Show(this, CSasamiCmdHelpDlg::kTabScore2); }

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
	int delMarks[16];
	int delN = 0;
	{
		int mtr = -1;
		const int ctrl = ScStaffHitScoreCtrl(m_gridRc, &m_ui, m_doc.ev, m_doc.evCount, 1, client, &mtr);
		if (ctrl >= 0 && ctrl < m_doc.evCount) {
			const uint8_t k = m_doc.ev[ctrl].kind;
			if (ScStaffIsStaffMarkKind(k, 1) || k == SC_EV_FM_VOICE)
				markEv = ctrl;
			if (tr < 0 && mtr >= 0) tr = mtr;
		}
		delN = ScStaffCollectStaffMarksAt(m_gridRc, &m_ui, m_doc.ev, m_doc.evCount, 1, client, delMarks, 16);
		if (delN > 0) {
			markEv = delMarks[delN - 1];
			if (tr < 0) tr = (int)m_doc.ev[markEv].ch;
		} else if (markEv < 0) {
			const int sm = ScStaffHitStaffMark(m_gridRc, &m_ui, m_doc.ev, m_doc.evCount, 1, client, &mtr);
			if (sm >= 0 && sm < m_doc.evCount) {
				markEv = sm;
				if (tr < 0 && mtr >= 0) tr = mtr;
			}
		}
	}
	const int pxBeat = m_ui.pxBeat > 0 ? m_ui.pxBeat : SC_PX_BEAT_DEFAULT;
	const int quant = ScStaffPlaceQuant(&m_ui);
	uint32_t atTick = m_ui.markerTick;
	if (m_gridRc.PtInRect(client) && client.x >= ScStaffGridLeftPx(m_gridRc.left, &m_ui, m_doc.ev, m_doc.evCount))
		atTick = ScStaffXToTick(client.x, m_ui.scrollX, ScStaffGridLeftPx(m_gridRc.left, &m_ui, m_doc.ev, m_doc.evCount), pxBeat, quant, &m_ui, m_doc.ev, m_doc.evCount);
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
			L"ループ開始 (|:n)…", L"Loop start (|:n)…", L"Début de boucle (|:n)…", L"Inizio loop (|:n)…", L"Inicio de bucle (|:n)…",
			L"루프 시작 (|:n)…", L"循环开始 (|:n)…", L"بداية الحلقة (|:n)…", L"Начало цикла (|:n)…", L"Schleifenstart (|:n)…",
			L"Início do loop (|:n)…", L"Lusbegin (|:n)…", L"Początek pętli (|:n)…", L"Döngü başlangıcı (|:n)…"));
		menu.AddCommand(9021, LL14(
			L"ループ終了 (:|)", L"Loop end (:|)", L"Fin de boucle (:|)", L"Fine loop (:|)", L"Fin de bucle (:|)",
			L"루프 끝 (:|)", L"循环结束 (:|)", L"نهاية الحلقة (:|)", L"Конец цикла (:|)", L"Schleifenende (:|)",
			L"Fim do loop (:|)", L"Luseinde (:|)", L"Koniec pętli (:|)", L"Döngü sonu (:|)"));
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
		menu.AddCommand(9037, LL14(L"調号…", L"Key signature…", L"Armature…", L"Armatura…", L"Armadura…",
			L"조표…", L"调号…", L"دليل…", L"Тональность…", L"Vorzeichen…", L"Armadura…", L"Toonsoort…", L"Tonacja…", L"Armatür…"));
		menu.AddCommand(9040, LL14(L"拍子・移調…", L"Meter / transpose…", L"Mesure / transposition…", L"Misura / trasposizione…", L"Compás / transposición…",
			L"박자·이조…", L"拍号·移调…", L"إيقاع / نقل…", L"Размер / трансп.…", L"Takt / Transponieren…", L"Compasso / transpor…", L"Maat / transp.…", L"Metrum / transp.…", L"Ölçü / transp.…"));
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
				else if (e.kind == SC_EV_OTTAVA)
					lab.Format(L"削除 %s", ScStaffOttavaLabel((int)(int8_t)e.a));
				else if (e.kind == SC_EV_OTTAVA_END)
					lab = L"削除 loco";
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
	auto afterMark = [&](uint8_t kind) {
		m_ui.visible[tr] = 1;
		m_curCh = tr;
		if (m_ch.GetSafeHwnd()) m_ch.SetCurSel(m_curCh);
		RefreshToneLabels();
		InvalidateRect(m_bodyRc, FALSE);
		CString st;
		const int still = ScMarkKindExists(m_doc.ev, m_doc.evCount, atTick, (uint8_t)tr, kind);
		if (!m_ui.markStack && !still)
			st.Format(L"FM ch %d mark removed @ tick %u", tr + 1, (unsigned)atTick);
		else
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
		if (!m_ui.markStack
			&& ScMarkKindExists(m_doc.ev, m_doc.evCount, atTick, (uint8_t)tr, SC_EV_FM_LOOP_START)) {
			if (ScFmAddLoopStart(&m_doc, atTick, tr, 2, 0)) afterMark(SC_EV_FM_LOOP_START);
		} else {
			int n = 2;
			if (CSasamiSimpleInputDlg::AskNumber(this, L"ループ開始 |:",
				L"繰り返し回数 (1–99)", 2, 1, 99, &n) == IDOK) {
				if (ScFmAddLoopStart(&m_doc, atTick, tr, n, m_ui.markStack)) afterMark(SC_EV_FM_LOOP_START);
			}
		}
	}
	else if (tr >= 0 && cmd == 9021) {
		if (ScFmAddLoopEnd(&m_doc, atTick, tr, m_ui.markStack)) afterMark(SC_EV_FM_LOOP_END);
	}
	else if (cmd == 9029 && markEv >= 0 && markEv < m_doc.evCount
		&& m_doc.ev[markEv].kind == SC_EV_FM_LOOP_START) {
		int n = m_doc.ev[markEv].a ? (int)m_doc.ev[markEv].a : 2;
		if (CSasamiSimpleInputDlg::AskNumber(this, L"ループ回数",
			L"繰り返し回数 (1–99)", n, 1, 99, &n) == IDOK) {
			m_doc.ev[markEv].a = (uint8_t)n;
			InvalidateRect(m_bodyRc, FALSE);
		}
	}
	else if (tr >= 0 && cmd == 9022) {
		if (ScFmAddJumpMark(&m_doc, atTick, tr, m_ui.markStack)) afterMark(SC_EV_JUMP_MARK);
	}
	else if (tr >= 0 && cmd == 9023) {
		if (ScFmAddJump(&m_doc, atTick, tr, m_ui.markStack)) afterMark(SC_EV_FM_JUMP);
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
	else if (cmd >= 9100 && cmd < 9100 + delN) {
		const int ei = delMarks[cmd - 9100];
		if (ei >= 0 && ei < m_doc.evCount) {
			for (int j = ei; j + 1 < m_doc.evCount; j++)
				m_doc.ev[j] = m_doc.ev[j + 1];
			m_doc.evCount--;
			if (m_ui.selEv == ei) m_ui.selEv = -1;
			else if (m_ui.selEv > ei) m_ui.selEv--;
			RefreshToneLabels();
			InvalidateRect(m_bodyRc, FALSE);
		}
	}
	else if (cmd == 9120 && delN > 0) {
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
	else if (cmd == 9037) {
		int ks = m_ui.keySig;
		if (CSasamiSimpleInputDlg::AskNumber(this, L"Key signature",
			L"sharps +1..+7 / flats -1..-7 / 0=C", m_ui.keySig, -7, 7, &ks) == IDOK) {
			m_ui.keySig = ks;
			InvalidateRect(m_bodyRc, FALSE);
		}
	}
	else if (cmd == 9040) {
		RunLayoutMenu(atTick, point);
	}
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
	savedata.sasamiFmPartMask = ScStaffPackPartMask(&m_ui);
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
	m_ui.markStack = savedata.sasamiMarkStack ? 1 : 0;
	if (!ScRestoreWndGeom(this, savedata.sasamiFmX, savedata.sasamiFmY,
		savedata.sasamiFmW, savedata.sasamiFmH, 720, 420))
		SetWindowPos(NULL, 0, 0, 1100, 720, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	LayoutChrome();
	UpdateScrollBars();
}

void CSasamiFmScoreDlg::OnClose()
{
	PersistSession();
	PushDocToText();
	PersistUiGeom();
	KillTimer(kScFmPreviewTimer);
	m_ui.previewActive = 0;
	VstLiveEditorOpenCancelPending();
	VstLiveMonitorStop();
	CSasamiNotePropsDlg::CloseOpen();
	if (CSasamiNotePaletteDlg* pal = CSasamiNotePaletteDlg::Instance())
		pal->DestroyWindow();
	DestroyWindow();
}
void CSasamiFmScoreDlg::OnDestroy()
{
	KillTimer(kScFmPreviewTimer);
	KillTimer(kScFmMetroTimer);
	ScScoreMidiInShutdown(&m_midiIn);
	m_ui.previewActive = 0;
	CCustomBlurDialogExBase::OnDestroy();
}
void CSasamiFmScoreDlg::PostNcDestroy()
{
	if (s_inst == this) s_inst = NULL;
	CCustomBlurDialogExBase::PostNcDestroy();
	delete this;
}

void CSasamiFmScoreDlg::PushDocToText()
{
	CSasamiTextDlg* t = CSasamiTextDlg::Instance();
	if (!t || !::IsWindow(t->GetSafeHwnd())) return;
	const int mmlCch = (int)(SC_TEXT_MAX / sizeof(wchar_t));
	wchar_t* mml = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		(SIZE_T)mmlCch * sizeof(wchar_t));
	if (!mml) return;
	if (!ScFmDocToMml(&m_doc, mml, mmlCch)) {
		HeapFree(GetProcessHeap(), 0, mml);
		return;
	}
	t->SetFmTextFromScore(mml);
	HeapFree(GetProcessHeap(), 0, mml);
}

void CSasamiFmScoreDlg::PullDocFromText(int force)
{
	CSasamiTextDlg* t = CSasamiTextDlg::Instance();
	if (!t || !::IsWindow(t->GetSafeHwnd()) || !t->IsFmMode()) return;
	if (!force && !t->IsTextDirty()) {
		if (t->FmDoc()->evCount > 0)
			LoadFromDoc(*t->FmDoc());
		return;
	}
	wchar_t err[256];
	int errLine = 0;
	if (!t->CompileFmCache(&errLine, err, 256)) {
		CString st;
		st.Format(L"Text compile failed L%d: %s", errLine, err);
		m_status.SetWindowText(st);
		return;
	}
	LoadFromDoc(*t->FmDoc());
	t->MarkTextSyncedToScore();
	CString st;
	st.Format(L"Score synced from text (%d events)", m_doc.evCount);
	m_status.SetWindowText(st);
}

void CSasamiFmScoreDlg::PersistSession()
{
	PersistUiGeom();
	const int mmlCch = (int)(SC_TEXT_MAX / sizeof(wchar_t));
	wchar_t* mml = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		(SIZE_T)mmlCch * sizeof(wchar_t));
	if (!mml) return;
	if (CSasamiTextDlg* t = CSasamiTextDlg::Instance()) {
		if (::IsWindow(t->GetSafeHwnd()) && t->IsFmMode()) {
			CString s = t->GetMmlText();
			if (!s.IsEmpty())
				wcsncpy_s(mml, mmlCch, s, _TRUNCATE);
		}
	}
	if (!mml[0])
		ScFmDocToMml(&m_doc, mml, mmlCch);
	ScSessionSaveLastFm(&m_doc, mml[0] ? mml : NULL);
	HeapFree(GetProcessHeap(), 0, mml);
}

void CSasamiFmScoreDlg::SyncTextIfOpen()
{
	CSasamiTextDlg* t = CSasamiTextDlg::Instance();
	if (!t || !::IsWindow(t->GetSafeHwnd()) || !t->IsFmMode()) return;
	PushDocToText();
}

void CSasamiFmScoreDlg::OnBnClickedText()
{
	PushDocToText();
	CSasamiTextDlg::OpenOwned(this);
}

void CSasamiFmScoreDlg::OnBnClickedArr()
{
	static int s_lastPreset = SC_ARR_HUMANIZE;
	CCustomPopupMenu menu;
	menu.SetAeroMode(FALSE);
	for (int i = 0; i < SC_ARR_COUNT; i++)
		menu.AddCommand(9200 + i, ScArrangePresetName(i));
	menu.AddSeparator();
	menu.AddCommand(9210, LL14(L"選択に適用", L"Apply to selection", L"Sélection", L"Selezione", L"Selección", L"선택", L"选区", L"تحديد", L"Выделение", L"Auswahl", L"Seleção", L"Selectie", L"Zaznaczenie", L"Seçim"));
	menu.AddCommand(9211, LL14(L"パートに適用", L"Apply to part", L"Partie", L"Parte", L"Parte", L"파트", L"声部", L"جزء", L"Партия", L"Part", L"Parte", L"Partij", L"Partia", L"Parti"));
	menu.AddCommand(9212, LL14(L"全体に適用", L"Apply to all", L"Tout", L"Tutto", L"Todo", L"전체", L"全部", L"الكل", L"Всё", L"Alles", L"Tudo", L"Alles", L"Wszystko", L"Tümü"));
	menu.AddSeparator();
	for (int i = 0; i < SC_CHORD_TYPE_COUNT; i++)
		menu.AddCheck(9300 + i, ScChordTypeName(i), m_ui.chordMode && m_ui.chordType == i);
	menu.AddCommand(9320, LL14(L"選択を和音化", L"Expand selection", L"Étendre sélection", L"Espandi selezione", L"Expandir selección", L"선택 화음화", L"选区成和弦", L"توسيع", L"Расширить", L"Auswahl erweitern", L"Expandir", L"Uitbreiden", L"Rozszerz", L"Seçimi genişlet"));
	menu.AddCommand(9321, LL14(L"コード記号…", L"From symbol…", L"Symbole…", L"Simbolo…", L"Símbolo…", L"코드 기호…", L"和弦记号…", L"رمز…", L"Символ…", L"Symbol…", L"Símbolo…", L"Symbool…", L"Symbol…", L"Sembol…"));
	menu.AddSeparator();
	for (int i = 0; i < SC_PAT_COUNT; i++)
		menu.AddCheck(9400 + i, ScPatternName(i), m_ui.patternMode && m_ui.patternId == i);
	CPoint pt; GetCursorPos(&pt);
	const UINT cmd = menu.Track(pt, this);
	if (cmd >= 9200 && cmd < 9200 + (UINT)SC_ARR_COUNT) {
		s_lastPreset = (int)(cmd - 9200);
		HistPush();
		ScArrangeApply(m_doc.ev, &m_doc.evCount, &m_ui, m_curCh, SC_ARR_SEL, s_lastPreset, 60);
		InvalidateRect(m_bodyRc, FALSE);
		m_status.SetWindowText(ScArrangePresetName(s_lastPreset));
	} else if (cmd >= 9210 && cmd <= 9212) {
		HistPush();
		ScArrangeApply(m_doc.ev, &m_doc.evCount, &m_ui, m_curCh, (int)(cmd - 9210), s_lastPreset, 55);
		InvalidateRect(m_bodyRc, FALSE);
	} else if (cmd >= 9300 && cmd < 9300 + (UINT)SC_CHORD_TYPE_COUNT) {
		const int t = (int)(cmd - 9300);
		if (m_ui.chordMode && m_ui.chordType == t) {
			m_ui.chordMode = 0;
			m_status.SetWindowText(LL14(L"和音モード OFF", L"Chord mode OFF", L"Mode accord OFF", L"Modo accordo OFF", L"Modo acorde OFF", L"화음모드 OFF", L"和弦模式关", L"وضع الوتر OFF", L"Режим акк OFF", L"Akkordmodus AUS", L"Modo acorde OFF", L"Akkoordmodus UIT", L"Tryb akordu OFF", L"Akor modu KAPALI"));
		} else {
			m_ui.chordMode = 1;
			m_ui.patternMode = 0;
			m_ui.chordType = t;
			int semis[8];
			m_ui.chordVoices = ScChordIntervals(t, semis, 8);
			if (m_ui.chordVoices < 2) m_ui.chordVoices = 2;
			m_ui.tool = SC_TOOL_PENCIL;
			m_status.SetWindowText(ScChordTypeName(m_ui.chordType));
		}
		InvalidateRect(m_bodyRc, FALSE);
	} else if (cmd == 9320) {
		HistPush();
		ScChordExpandNotes(m_doc.ev, &m_doc.evCount, SC_EV_MAX, m_curCh, 0, 0xFFFFFFFFu, m_ui.chordType, m_ui.chordVoices);
		for (int i = 0; i < m_doc.evCount; i++) {
			if (m_doc.ev[i].kind == SC_EV_NOTE && (int)m_doc.ev[i].ch == m_curCh) {
				m_doc.ev[i].kind = SC_EV_FM_NOTE;
				m_doc.ev[i].a = MidiToFmNoteByte(m_doc.ev[i].a);
			}
		}
		InvalidateRect(m_bodyRc, FALSE);
	} else if (cmd == 9321) {
		wchar_t symBuf[64] = L"C";
		if (CSasamiSimpleInputDlg::AskText(this,
			LL14(L"コード", L"Chord", L"Accord", L"Accordo", L"Acorde", L"코드", L"和弦", L"وتر", L"Аккорд", L"Akkord", L"Acorde", L"Akkoord", L"Akor", L"Akor"),
			L"C / Am7 / G7 …", symBuf, 64) == IDOK) {
			HistPush();
			ScChordFromSymbol(m_doc.ev, &m_doc.evCount, SC_EV_MAX, m_curCh,
				m_ui.markerTick, m_ui.placeDur > 0 ? m_ui.placeDur : SC_PPQN, 100, symBuf, 4);
			for (int i = 0; i < m_doc.evCount; i++) {
				if (m_doc.ev[i].kind == SC_EV_NOTE && (int)m_doc.ev[i].ch == m_curCh) {
					m_doc.ev[i].kind = SC_EV_FM_NOTE;
					m_doc.ev[i].a = MidiToFmNoteByte(m_doc.ev[i].a);
				}
			}
			InvalidateRect(m_bodyRc, FALSE);
		}
	} else if (cmd >= 9400 && cmd < 9400 + (UINT)SC_PAT_COUNT) {
		const int id = (int)(cmd - 9400);
		if (m_ui.patternMode && m_ui.patternId == id) {
			m_ui.patternMode = 0;
			m_status.SetWindowText(LL14(L"パターンモード OFF", L"Pattern mode OFF", L"Mode motif OFF", L"Modo pattern OFF", L"Modo patrón OFF", L"패턴모드 OFF", L"型模式关", L"وضع النمط OFF", L"Режим паттерна OFF", L"Mustermodus AUS", L"Modo padrão OFF", L"Patroonmodus UIT", L"Tryb wzorca OFF", L"Desen modu KAPALI"));
		} else {
			m_ui.patternMode = 1;
			m_ui.chordMode = 0;
			m_ui.patternId = id;
			m_ui.tool = SC_TOOL_PENCIL;
			m_status.SetWindowText(ScPatternName(id));
		}
		InvalidateRect(m_bodyRc, FALSE);
	}
}

LRESULT CSasamiFmScoreDlg::OnPalQueryState(WPARAM, LPARAM lParam)
{
	SasamiPalLayoutState* st = (SasamiPalLayoutState*)lParam;
	if (!st) return 0;
	const int defN = m_ui.meterNumer > 0 ? m_ui.meterNumer : 4;
	const int defD = m_ui.meterDenom > 0 ? m_ui.meterDenom : 4;
	const uint32_t barTick = ScStaffSnapToBarTick(m_doc.ev, m_doc.evCount, m_ui.markerTick, defN, defD);
	int mn = defN, md = defD;
	ScStaffMeterAtTick(m_doc.ev, m_doc.evCount, barTick, defN, defD, &mn, &md);
	st->meterN = mn;
	st->meterD = md;
	st->keySig = ScStaffKeySigAtTick(m_doc.ev, m_doc.evCount, barTick, m_ui.keySig);
	st->clef = ScStaffClefModeAt(&m_ui, m_curCh, barTick, m_doc.ev, m_doc.evCount);
	st->nSel = m_ui.nSel;
	st->previewNote = -1;
	st->curCh = m_curCh;
	if (m_ui.nSel > 0 && m_ui.selList[0] >= 0 && m_ui.selList[0] < m_doc.evCount
		&& m_doc.ev[m_ui.selList[0]].kind == SC_EV_NOTE)
		st->previewNote = (int)m_doc.ev[m_ui.selList[0]].a;
	return 1;
}

LRESULT CSasamiFmScoreDlg::OnPalLayout(WPARAM w, LPARAM l)
{
	const int kind = (int)(l & 0xFF);
	const int b = (int)((l >> 16) & 0xFFFF);
	if (kind == SASAMI_PAL_LAYOUT_METER) {
		const int defN = m_ui.meterNumer > 0 ? m_ui.meterNumer : 4;
		const int defD = m_ui.meterDenom > 0 ? m_ui.meterDenom : 4;
		const uint32_t barTick = ScStaffSnapToBarTick(m_doc.ev, m_doc.evCount, m_ui.markerTick, defN, defD);
		ApplyMeterAtBar(barTick, (int)w, b);
		if (CSasamiLayoutPaletteDlg* pal = CSasamiLayoutPaletteDlg::Instance())
			pal->Invalidate(FALSE);
	}
	return 0;
}

void CSasamiFmScoreDlg::ApplyLayoutPalCmd(int cmdId)
{
	const int defN = m_ui.meterNumer > 0 ? m_ui.meterNumer : 4;
	const int defD = m_ui.meterDenom > 0 ? m_ui.meterDenom : 4;
	const uint32_t barTick = ScStaffSnapToBarTick(m_doc.ev, m_doc.evCount, m_ui.markerTick, defN, defD);
	int numer = 0, denom = 0;
	if (ScStaffMeterFromPalCmd(cmdId, &numer, &denom)) {
		ApplyMeterAtBar(barTick, numer, denom);
		goto refresh_pal;
	}
	if (cmdId >= SASAMI_PAL_CMD_KEY_BASE && cmdId < SASAMI_PAL_CMD_KEY_BASE + 15) {
		const int ks = cmdId - SASAMI_PAL_CMD_KEY_BASE - 7;
		HistPush();
		ScFmAddKey(&m_doc, barTick, ks);
		m_ui.keySig = ks;
		InvalidateRect(m_bodyRc, FALSE);
		goto refresh_pal;
	}
	if (cmdId == SASAMI_PAL_CMD_METER_CUSTOM) {
		int numer = defN, denom = defD;
		if (CSasamiSimpleInputDlg::AskNumber(this, L"Time signature", L"Numerator (1–32)", numer, 1, 32, &numer) == IDOK
			&& CSasamiSimpleInputDlg::AskNumber(this, L"Time signature", L"Denominator (1–32)", denom, 1, 32, &denom) == IDOK)
			ApplyMeterAtBar(barTick, numer, denom);
		goto refresh_pal;
	}
	if (cmdId == SASAMI_PAL_CMD_METER_DEL) {
		HistPush();
		for (int i = m_doc.evCount - 1; i >= 0; i--) {
			if (m_doc.ev[i].kind != SC_EV_METER || m_doc.ev[i].tick != barTick) continue;
			for (int j = i; j + 1 < m_doc.evCount; j++)
				m_doc.ev[j] = m_doc.ev[j + 1];
			m_doc.evCount--;
		}
		SyncMeterFromDoc();
		InvalidateRect(m_bodyRc, FALSE);
		goto refresh_pal;
	}
	if (cmdId == SASAMI_PAL_CMD_KEY) {
		int ks = m_ui.keySig;
		if (CSasamiSimpleInputDlg::AskNumber(this, L"Key signature",
			L"sharps +1..+7 / flats -1..-7 / 0=C", m_ui.keySig, -7, 7, &ks) == IDOK) {
			HistPush();
			ScFmAddKey(&m_doc, barTick, ks);
			m_ui.keySig = ks;
			InvalidateRect(m_bodyRc, FALSE);
		}
		goto refresh_pal;
	}
	if (cmdId >= SASAMI_PAL_CMD_CLEF_G && cmdId <= SASAMI_PAL_CMD_CLEF_DR) {
		const int clef = cmdId - SASAMI_PAL_CMD_CLEF_G;
		HistPush();
		ScFmAddClef(&m_doc, barTick, m_curCh, clef);
		if (barTick == 0)
			m_ui.clef[m_curCh] = clef;
		ScStaffUpdateContentExtent(&m_ui, m_doc.ev, m_doc.evCount);
		UpdateScrollBars();
		InvalidateRect(m_bodyRc, FALSE);
		goto refresh_pal;
	}
	if (cmdId == SASAMI_PAL_CMD_TR_PLUS || cmdId == SASAMI_PAL_CMD_TR_MINUS) {
		HistPush();
		const int semi = (cmdId == SASAMI_PAL_CMD_TR_PLUS) ? 1 : -1;
		const int n = ScTransposeFmSelected(m_doc.ev, m_doc.evCount, &m_ui, semi);
		if (n > 0) InvalidateRect(m_bodyRc, FALSE);
		goto refresh_pal;
	}
	if (cmdId == SASAMI_PAL_CMD_TR_SEL_P12 || cmdId == SASAMI_PAL_CMD_TR_SEL_M12) {
		HistPush();
		const int semi = (cmdId == SASAMI_PAL_CMD_TR_SEL_P12) ? 12 : -12;
		const int n = ScTransposeFmSelected(m_doc.ev, m_doc.evCount, &m_ui, semi);
		if (n > 0) InvalidateRect(m_bodyRc, FALSE);
		goto refresh_pal;
	}
	if (cmdId == SASAMI_PAL_CMD_TR_PART_PLUS || cmdId == SASAMI_PAL_CMD_TR_PART_MINUS) {
		HistPush();
		const int semi = (cmdId == SASAMI_PAL_CMD_TR_PART_PLUS) ? 1 : -1;
		const int n = ScTransposeFmChannel(m_doc.ev, m_doc.evCount, m_curCh, semi);
		if (n > 0) InvalidateRect(m_bodyRc, FALSE);
		goto refresh_pal;
	}
	if (cmdId == SASAMI_PAL_CMD_TR_ALL_PLUS || cmdId == SASAMI_PAL_CMD_TR_ALL_MINUS) {
		HistPush();
		const int semi = (cmdId == SASAMI_PAL_CMD_TR_ALL_PLUS) ? 1 : -1;
		const int n = ScTransposeFmAll(m_doc.ev, m_doc.evCount, semi);
		if (n > 0) InvalidateRect(m_bodyRc, FALSE);
		goto refresh_pal;
	}
refresh_pal:
	if (CSasamiLayoutPaletteDlg* pal = CSasamiLayoutPaletteDlg::Instance())
		pal->Invalidate(FALSE);
}

void CSasamiFmScoreDlg::ApplyMeterAtBar(uint32_t tick, int numer, int denom)
{
	HistPush();
	const int defN = m_ui.meterNumer > 0 ? m_ui.meterNumer : 4;
	const int defD = m_ui.meterDenom > 0 ? m_ui.meterDenom : 4;
	tick = ScStaffSnapToBarTick(m_doc.ev, m_doc.evCount, tick, defN, defD);
	if (ScFmAddMeter(&m_doc, tick, numer, denom)) {
		SyncMeterFromDoc();
		InvalidateRect(m_bodyRc, FALSE);
		CString st;
		st.Format(L"@METER %d/%d @ bar tick %u (score display)", numer, denom, (unsigned)tick);
		m_status.SetWindowText(st);
	}
}

UINT CSasamiFmScoreDlg::RunLayoutMenu(uint32_t atTick, CPoint screenPt)
{
	static const struct { int n, d; const wchar_t* label; } kPreset[] = {
		{ 2, 4, L"2/4" }, { 3, 4, L"3/4" }, { 4, 4, L"4/4" }, { 5, 4, L"5/4" },
		{ 6, 8, L"6/8" }, { 7, 8, L"7/8" }, { 9, 8, L"9/8" },
	};
	const int defN = m_ui.meterNumer > 0 ? m_ui.meterNumer : 4;
	const int defD = m_ui.meterDenom > 0 ? m_ui.meterDenom : 4;
	const uint32_t barTick = ScStaffSnapToBarTick(m_doc.ev, m_doc.evCount, atTick, defN, defD);
	int curN = defN, curD = defD;
	ScStaffMeterAtTick(m_doc.ev, m_doc.evCount, barTick, defN, defD, &curN, &curD);

	CCustomPopupMenu menu;
	menu.SetAeroMode(FALSE);
	CString hdr;
	hdr.Format(L"tick %u  %d/%d", (unsigned)barTick, curN, curD);
	CCustomPopupMenu* meter = menu.AddSubMenu(LL14(L"拍子をここに", L"Set meter here", L"Mesure ici", L"Misura qui", L"Compás aquí",
		L"박자", L"拍号", L"إيقاع", L"Размер", L"Taktart", L"Compasso", L"Maat", L"Metrum", L"Ölçü"), hdr);
	for (int i = 0; i < (int)(sizeof(kPreset) / sizeof(kPreset[0])); i++)
		meter->AddCommand(9501 + (UINT)i, kPreset[i].label);
	meter->AddCommand(9511, LL14(L"拍子を指定…", L"Custom meter…", L"Mesure perso…", L"Misura…", L"Compás…",
		L"박자 지정…", L"自定义拍号…", L"إيقاع…", L"Свой размер…", L"Taktart…", L"Compasso…", L"Maat…", L"Metrum…", L"Ölçü…"));
	meter->AddCommand(9513, LL14(L"拍子変更を削除", L"Remove meter change", L"Supprimer mesure", L"Rimuovi misura", L"Quitar compás",
		L"박자 삭제", L"删除拍号变更", L"حذف الإيقاع", L"Удалить размер", L"Taktart entfernen", L"Remover compasso", L"Maat verwijderen", L"Usuń metrum", L"Ölçüyü sil"));
	menu.AddCommand(9512, LL14(L"調号…", L"Key signature…", L"Armature…", L"Armatura…", L"Armadura…",
		L"조표…", L"调号…", L"دليل…", L"Тональность…", L"Vorzeichen…", L"Armadura…", L"Toonsoort…", L"Tonacja…", L"Armatür…"));
	menu.AddSeparator();
	CCustomPopupMenu* tr = menu.AddSubMenu(LL14(L"移調（表示）", L"Transpose (display)", L"Transposer (affichage)", L"Trasporre (visual)", L"Transponer (visual)",
		L"이조（표시）", L"移调（显示）", L"نقل", L"Транспонирование", L"Transponieren", L"Transpor", L"Transponeren", L"Transponuj", L"Transpoze"), L"");
	tr->AddCommand(9520, LL14(L"選択 +1半音", L"Selection +1 semitone", L"Sélection +1", L"Selezione +1", L"Selección +1",
		L"선택 +1", L"选区 +1半音", L"تحديد +1", L"Выделение +1", L"Auswahl +1", L"Seleção +1", L"Selectie +1", L"Zaznaczenie +1", L"Seçim +1"));
	tr->AddCommand(9521, LL14(L"選択 -1半音", L"Selection -1 semitone", L"Sélection -1", L"Selezione -1", L"Selección -1",
		L"선택 -1", L"选区 -1半音", L"تحديد -1", L"Выделение -1", L"Auswahl -1", L"Seleção -1", L"Selectie -1", L"Zaznaczenie -1", L"Seçim -1"));
	tr->AddCommand(9522, LL14(L"選択 +1オクターブ", L"Selection +1 octave", L"Sélection +12", L"Selezione +12", L"Selección +12",
		L"선택 +12", L"选区 +1八度", L"تحديد +12", L"Выделение +12", L"Auswahl +12", L"Seleção +12", L"Selectie +12", L"Zaznaczenie +12", L"Seçim +12"));
	tr->AddCommand(9523, LL14(L"選択 -1オクターブ", L"Selection -1 octave", L"Sélection -12", L"Selezione -12", L"Selección -12",
		L"선택 -12", L"选区 -1八度", L"تحديد -12", L"Выделение -12", L"Auswahl -12", L"Seleção -12", L"Selectie -12", L"Zaznaczenie -12", L"Seçim -12"));
	tr->AddSeparator();
	tr->AddCommand(9524, LL14(L"パート +1半音", L"Part +1 semitone", L"Partie +1", L"Parte +1", L"Parte +1",
		L"파트 +1", L"声部 +1半音", L"جزء +1", L"Партия +1", L"Part +1", L"Parte +1", L"Partij +1", L"Partia +1", L"Parti +1"));
	tr->AddCommand(9525, LL14(L"パート -1半音", L"Part -1 semitone", L"Partie -1", L"Parte -1", L"Parte -1",
		L"파트 -1", L"声部 -1半音", L"جزء -1", L"Партия -1", L"Part -1", L"Parte -1", L"Partij -1", L"Partia -1", L"Parti -1"));
	tr->AddCommand(9526, LL14(L"全体 +1半音", L"All +1 semitone", L"Tout +1", L"Tutto +1", L"Todo +1",
		L"전체 +1", L"全部 +1半音", L"الكل +1", L"Всё +1", L"Alles +1", L"Tudo +1", L"Alles +1", L"Wszystko +1", L"Tümü +1"));
	tr->AddCommand(9527, LL14(L"全体 -1半音", L"All -1 semitone", L"Tout -1", L"Tutto -1", L"Todo -1",
		L"전체 -1", L"全部 -1半音", L"الكل -1", L"Всё -1", L"Alles -1", L"Tudo -1", L"Alles -1", L"Wszystko -1", L"Tümü -1"));

	const UINT cmd = menu.Track(screenPt, this);
	if (cmd >= 9501 && cmd < 9501 + (UINT)(sizeof(kPreset) / sizeof(kPreset[0]))) {
		const int i = (int)(cmd - 9501);
		ApplyMeterAtBar(barTick, kPreset[i].n, kPreset[i].d);
	} else if (cmd == 9511) {
		int numer = curN, denom = curD;
		if (CSasamiSimpleInputDlg::AskNumber(this, L"Time signature", L"Numerator (1–32)", numer, 1, 32, &numer) == IDOK
			&& CSasamiSimpleInputDlg::AskNumber(this, L"Time signature", L"Denominator (1–32)", denom, 1, 32, &denom) == IDOK)
			ApplyMeterAtBar(barTick, numer, denom);
	} else if (cmd == 9512) {
		int ks = m_ui.keySig;
		if (CSasamiSimpleInputDlg::AskNumber(this, L"Key signature",
			L"sharps +1..+7 / flats -1..-7 / 0=C", m_ui.keySig, -7, 7, &ks) == IDOK) {
			m_ui.keySig = ks;
			InvalidateRect(m_bodyRc, FALSE);
		}
	} else if (cmd == 9513) {
		HistPush();
		for (int i = m_doc.evCount - 1; i >= 0; i--) {
			if (m_doc.ev[i].kind != SC_EV_METER || m_doc.ev[i].tick != barTick) continue;
			for (int j = i; j + 1 < m_doc.evCount; j++)
				m_doc.ev[j] = m_doc.ev[j + 1];
			m_doc.evCount--;
		}
		SyncMeterFromDoc();
		InvalidateRect(m_bodyRc, FALSE);
		m_status.SetWindowText(LL14(L"拍子変更を削除", L"Meter change removed", L"Mesure supprimée", L"Misura rimossa", L"Compás eliminado",
			L"박자 삭제", L"已删除拍号变更", L"تم حذف الإيقاع", L"Размер удалён", L"Taktart entfernt", L"Compasso removido", L"Maat verwijderd", L"Usunięto metrum", L"Ölçü silindi"));
	} else if (cmd >= 9520 && cmd <= 9527) {
		HistPush();
		int n = 0;
		const int semi = (cmd == 9520 || cmd == 9524 || cmd == 9526) ? 1
			: (cmd == 9521 || cmd == 9525 || cmd == 9527) ? -1
			: (cmd == 9522) ? 12 : -12;
		if (cmd == 9520 || cmd == 9521 || cmd == 9522 || cmd == 9523)
			n = ScTransposeFmSelected(m_doc.ev, m_doc.evCount, &m_ui, semi);
		else if (cmd == 9524 || cmd == 9525)
			n = ScTransposeFmChannel(m_doc.ev, m_doc.evCount, m_curCh, semi);
		else
			n = ScTransposeFmAll(m_doc.ev, m_doc.evCount, semi);
		if (n > 0) {
			InvalidateRect(m_bodyRc, FALSE);
			CString st;
			st.Format(LL14(L"移調 %d 音（表示）", L"Transposed %d notes (display)", L"%d notes transposées", L"%d note trasposte", L"%d notas",
				L"%d음 이조", L"移调 %d 个音", L"%d نغمة", L"Транспонировано %d", L"%d Noten", L"%d notas", L"%d noten", L"%d nut", L"%d nota"), n);
			m_status.SetWindowText(st);
		}
	}
	return cmd;
}

void CSasamiFmScoreDlg::OnBnClickedLayout()
{
	CRect rc;
	if (m_btnLayout.GetSafeHwnd())
		m_btnLayout.GetWindowRect(&rc);
	else
		GetWindowRect(&rc);
	CPoint pt(rc.left, rc.bottom);
	CSasamiLayoutPaletteDlg::OpenNear(this, pt);
}

void CSasamiFmScoreDlg::OnBnClickedRoll()
{
	m_ui.showRollSplit ^= 1;
	LayoutChrome();
	CSasamiPianoRollDlg::OpenOwned(this, m_doc.ev, &m_doc.evCount, &m_ui, &m_curCh, 1);
	Invalidate(FALSE);
}

void CSasamiFmScoreDlg::HistPush()
{
	ScScoreHistPush(&m_hist, m_doc.ev, m_doc.evCount);
	SyncTextIfOpen();
}

void CSasamiFmScoreDlg::SyncMidiInCombos()
{
	if (!m_midiInDev.GetSafeHwnd()) return;
	ScScoreMidiInFillDeviceCombo(m_midiInDev.GetSafeHwnd());
	m_midiInMode.ResetContent();
	m_midiInMode.AddString(LL14(L"OFF", L"OFF", L"OFF", L"OFF", L"OFF", L"OFF", L"关", L"OFF", L"OFF", L"AUS", L"OFF", L"UIT", L"WYŁ", L"KAPALI"));
	m_midiInMode.AddString(LL14(L"Step", L"Step", L"Pas", L"Step", L"Paso", L"스텝", L"步进", L"خطوة", L"Шаг", L"Schritt", L"Passo", L"Stap", L"Krok", L"Adım"));
	m_midiInMode.AddString(LL14(L"Realtime", L"Realtime", L"Temps réel", L"Realtime", L"Tiempo real", L"실시간", L"实时", L"فوري", L"Реалтайм", L"Echtzeit", L"Tempo real", L"Realtime", L"Realtime", L"Gerçek zaman"));
	m_midiInMode.SetCurSel(m_midiIn.mode);
	if (m_midiInDev.GetSafeHwnd()) m_midiInDev.SetAeroMode(FALSE);
	if (m_midiInMode.GetSafeHwnd()) m_midiInMode.SetAeroMode(FALSE);
	if (m_btnArr.GetSafeHwnd()) { m_btnArr.SetAeroMode(FALSE); m_btnArr.SetFlat(TRUE); m_btnArr.SetWindowText(LL14(L"アレンジ", L"Arrange", L"Arrange", L"Arrange", L"Arran.", L"어레인지", L"编曲", L"ترتيب", L"Аранж", L"Arrange", L"Arranjo", L"Arrange", L"Aranż", L"Arrange")); }
	if (m_btnLayout.GetSafeHwnd()) { m_btnLayout.SetAeroMode(FALSE); m_btnLayout.SetFlat(TRUE); m_btnLayout.SetWindowText(LL14(L"譜表", L"Layout", L"Portée", L"Impag.", L"Layout", L"보표", L"谱表", L"تخطيط", L"Партитура", L"Notation", L"Layout", L"Layout", L"Układ", L"Düzen")); }
	if (m_btnRoll.GetSafeHwnd()) { m_btnRoll.SetAeroMode(FALSE); m_btnRoll.SetFlat(TRUE); m_btnRoll.SetWindowText(LL14(L"ピアノロール", L"Piano roll", L"Piano roll", L"Piano roll", L"Piano roll", L"피아노 롤", L"钢琴卷帘", L"رول بيانو", L"Пианоролл", L"Klavierrolle", L"Piano roll", L"Piano-roll", L"Rolka", L"Piyano rulosu")); }
}

void CSasamiFmScoreDlg::OnCbnMidiIn()
{
	if (!m_midiInDev.GetSafeHwnd()) return;
	const int di = m_midiInDev.GetCurSel();
	const int dev = (di >= 0) ? (int)m_midiInDev.GetItemData(di) : -1;
	const int mode = m_midiInMode.GetCurSel();
	ScScoreMidiInSetMode(&m_midiIn, mode >= 0 ? mode : 0);
	if (!ScScoreMidiInOpen(&m_midiIn, m_hWnd, dev)) {
		m_status.SetWindowText(LL14(L"MIDI In を開けません", L"MIDI In open failed", L"Échec ouverture MIDI In", L"Apertura MIDI In non riuscita", L"Error al abrir MIDI In",
			L"MIDI In 열기 실패", L"无法打开 MIDI In", L"فشل فتح MIDI In", L"Не удалось открыть MIDI In", L"MIDI-In öffnen fehlgeschlagen", L"Falha ao abrir MIDI In", L"MIDI-In openen mislukt", L"Nie udało się otworzyć MIDI In", L"MIDI In açılamadı"));
		return;
	}
	if (m_midiIn.mode == SC_MIDIIN_REALTIME) {
		double bpm = (13000.0 * 120.0) / (double)max(1, m_doc.tempoT);
		uint32_t loopEnd = 0;
		if (m_ui.loopATick >= 0 && m_ui.loopBTick > m_ui.loopATick) {
			m_ui.markerTick = (uint32_t)m_ui.loopATick;
			loopEnd = (uint32_t)m_ui.loopBTick;
		}
		if (m_midiIn.overdub && loopEnd > m_ui.markerTick) {
			HistPush();
			ScDeleteNotesInRange(m_doc.ev, &m_doc.evCount, m_curCh, m_ui.markerTick, loopEnd);
		}
		ScScoreMidiInStartRealtime(&m_midiIn, m_ui.markerTick, loopEnd, bpm);
		m_metroBeat = 0;
		const UINT ms = (UINT)max(50.0, 60000.0 / max(1.0, bpm));
		SetTimer(kScFmMetroTimer, ms, NULL);
		m_status.SetWindowText(LL14(
			L"FM Realtime録音中 — 鍵盤を弾く（停止はモードOFF）",
			L"FM Realtime MIDI In — play keyboard (mode OFF to stop)",
			L"FM temps réel — joue (mode OFF pour arrêter)",
			L"FM realtime — suona (mode OFF per fermare)",
			L"FM realtime — toca (modo OFF para parar)",
			L"FM 실시간 녹음 — 건반 (정지는 모드 OFF)",
			L"FM实时录音 — 弹键盘（OFF停止）",
			L"تسجيل FM فوري — اعزف (OFF للإيقاف)",
			L"FM реалтайм — играйте (OFF для стопа)",
			L"FM-Echtzeit — spielen (AUS zum Stoppen)",
			L"FM realtime — toque (OFF para parar)",
			L"FM-realtime — speel (UIT om te stoppen)",
			L"FM realtime — graj (WYŁ aby zatrzymać)",
			L"FM gerçek zaman — çalın (OFF ile durdur)"));
	} else {
		KillTimer(kScFmMetroTimer);
		ScScoreMidiInStopRealtime(&m_midiIn);
		if (m_midiIn.mode == SC_MIDIIN_STEP)
			m_status.SetWindowText(LL14(L"Step入力 — NoteOnで赤バーに配置", L"Step entry — NoteOn places at red bar", L"Entrée pas — NoteOn au curseur", L"Step — NoteOn sul cursore", L"Paso — NoteOn en barra", L"스텝 입력 — NoteOn으로 배치", L"步进 — NoteOn在红条放置", L"خطوة — NoteOn عند الشريط", L"Шаг — NoteOn на красной черте", L"Schritt — NoteOn an Markierung", L"Passo — NoteOn na barra", L"Stap — NoteOn bij rode balk", L"Krok — NoteOn na czerwonym pasku", L"Adım — NoteOn kırmızı çubuğa"));
		else
			m_status.SetWindowText(LL14(L"MIDI In OFF", L"MIDI In OFF", L"MIDI In OFF", L"MIDI In OFF", L"MIDI In OFF", L"MIDI In OFF", L"MIDI In 关", L"MIDI In OFF", L"MIDI In OFF", L"MIDI In AUS", L"MIDI In OFF", L"MIDI In UIT", L"MIDI In WYŁ", L"MIDI In KAPALI"));
	}
}

void CSasamiFmScoreDlg::ApplyFollowScroll()
{
	if (m_ui.followMode <= 0 || m_ui.followViewW <= 0) return;
	const int pxBeat = m_ui.pxBeat > 0 ? m_ui.pxBeat : SC_PX_BEAT_DEFAULT;
	const int margin = ScStaffClefMarginPx(&m_ui, m_doc.ev, m_doc.evCount);
	const int x = (int)((m_ui.playheadTick * (uint32_t)pxBeat) / SC_PPQN) + margin;
	if (m_ui.followMode == 1)
		m_ui.scrollX = max(0, x - m_ui.followViewW / 2);
	else if (m_ui.followMode == 2) {
		if (x < m_ui.scrollX + margin || x > m_ui.scrollX + m_ui.followViewW - 40)
			m_ui.scrollX = max(0, x - 40);
	}
}

LRESULT CSasamiFmScoreDlg::OnScoreMidi(WPARAM w, LPARAM)
{
	const DWORD msg = (DWORD)w;
	const int st = (int)(msg & 0xF0);
	const int data1 = (int)((msg >> 8) & 0xFF);
	const int data2 = (int)((msg >> 16) & 0xFF);
	if (st == 0x90 && data2 > 0) {
		if (m_midiIn.mode == SC_MIDIIN_STEP) {
			HistPush();
			int dur = m_ui.placeDur > 0 ? m_ui.placeDur : SC_PPQN;
			ScFmAddNote(&m_doc, m_ui.markerTick, m_curCh, MidiToFmNoteByte(data1), dur);
			m_ui.markerTick += (uint32_t)dur;
			RefreshStrip();
			InvalidateRect(m_bodyRc, FALSE);
		} else if (m_midiIn.mode == SC_MIDIIN_REALTIME && m_midiIn.recording) {
			uint32_t tick = ScScoreMidiInQuantizeTick(&m_midiIn, ScScoreMidiInNowTick(&m_midiIn, SC_PPQN), SC_PPQN, 1);
			m_midiIn.heldNote[data1 & 127] = data2 > 0 ? data2 : 1;
			m_midiIn.heldOnTick[data1 & 127] = tick;
			m_ui.markerTick = tick;
		}
		return 0;
	}
	if (st == 0x80 || (st == 0x90 && data2 == 0)) {
		if (m_midiIn.mode == SC_MIDIIN_REALTIME && m_midiIn.recording && m_midiIn.heldNote[data1 & 127]) {
			uint32_t t0 = m_midiIn.heldOnTick[data1 & 127];
			uint32_t t1 = ScScoreMidiInQuantizeTick(&m_midiIn, ScScoreMidiInNowTick(&m_midiIn, SC_PPQN), SC_PPQN, 1);
			int dur = (int)(t1 - t0);
			if (dur < 1) dur = SC_PPQN / 4;
			HistPush();
			ScFmAddNote(&m_doc, t0, m_curCh, MidiToFmNoteByte(data1), dur);
			m_midiIn.heldNote[data1 & 127] = 0;
			RefreshStrip();
			InvalidateRect(m_bodyRc, FALSE);
		}
		return 0;
	}
	return 0;
}

