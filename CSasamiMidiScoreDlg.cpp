#include "stdafx.h"
#include "ogg.h"
#include "CSasamiMidiScoreDlg.h"
#include "CSasamiNotePaletteDlg.h"
#include "CSasamiLayoutPaletteDlg.h"
#include "CSasamiSimpleInputDlg.h"
#include "CSasamiNotePropsDlg.h"
#include "CSasamiVstPickDlg.h"
#include "CSasamiVstPartMenu.h"
#include "CSasamiTextDlg.h"
#include "CSasamiExcRpnDlg.h"
#include "CSasamiInsertFxDlg.h"
#include "CSasamiScoreArrange.h"
#include "CSasamiPianoRollDlg.h"
#include "CCustomPopupMenu.h"
#include "OfflineHelp.h"
#include "CSasamiCmdHelpDlg.h"
#include "PlayList.h"
#include "VstMidiEngine.h"
#include "SasamiToneNames.h"
#include "kb_sasami/source/sasami_write.h"
#include "kb_sasami/source/sasami_file.h"
#include "kb_sasami/source/sasami_midi.h"

void MmBindVstActiveSlot();

extern CPlayList* pl;

enum { kScPreviewTimer = 7101 };
enum { kScMetroTimer = 7102 };

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
	const int colMax = ScStaffStripColCount(u);
	if (c0 > c1) { int t = c0; c0 = c1; c1 = t; t = v0; v0 = v1; v1 = t; }
	if (c0 < 0) c0 = 0;
	if (c1 >= colMax) c1 = colMax - 1;
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
	, m_curCh(0), m_placeRest(0), m_accidental(0), m_blankCur(NULL), m_sbDrag(0), m_sbDragScroll0(0), m_sbDragAnchor(0), m_bInLayout(FALSE), m_propMode(0)
	, m_pendingEdClosePart(0), m_pendingEdCloseProg(-1), m_pendingEdOpenPart(0)
	, m_clipN(0), m_clipBase(0), m_clipSpan(0), m_dragLastX(0), m_dragLastY(0), m_histDragPushed(0)
	, m_metroBeat(0), m_metroTimer(0)
{
	m_drawCursors.pencil = NULL;
	m_drawCursors.line = NULL;
	m_drawCursors.curve = NULL;
	m_lastOut[0] = 0;
	m_lastHoverSt[0] = 0;
	ScMidiDocClear(&m_doc);
	m_doc.tempoT = 13000;
	ScStaffUiInit(&m_ui, SC_MIDI_CH, 0);
	ScScoreMidiInInit(&m_midiIn);
	ScScoreHistInit(&m_hist);
	ScPianoRollInit(&m_rollView);
}

CSasamiMidiScoreDlg::~CSasamiMidiScoreDlg()
{
	if (m_blankCur) { ::DestroyCursor(m_blankCur); m_blankCur = NULL; }
	ScStaffDrawCursorsFree(&m_drawCursors);
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
	DDX_Control(pDX, IDC_SASAMI_STRIP_KIND2, m_stripKind2);
	DDX_Control(pDX, IDC_SASAMI_STRIP_DRAW, m_stripDraw);
	DDX_Control(pDX, IDC_SASAMI_STRIP_LANES, m_stripLanes);
	DDX_Control(pDX, IDC_SASAMI_STRIP_STEP, m_stripStep);
	DDX_Control(pDX, IDC_SASAMI_SCORE_HELPBAR, m_helpBar);
	DDX_Control(pDX, IDC_SASAMI_MIDI_MARK, m_btnMark);
	DDX_Control(pDX, IDC_SASAMI_MIDI_LOOPA, m_btnLoopA);
	DDX_Control(pDX, IDC_SASAMI_MIDI_LOOPB, m_btnLoopB);
	DDX_Control(pDX, IDC_SASAMI_MIDI_LOOPCLR, m_btnLoopClr);
	DDX_Control(pDX, IDC_SASAMI_MIDI_SHOWALL, m_btnShowAll);
	DDX_Control(pDX, IDC_SASAMI_MIDI_TEXT, m_btnText);
	DDX_Control(pDX, IDC_SASAMI_MIDI_FX, m_btnFx);
	DDX_Control(pDX, IDC_SASAMI_MIDI_INDEV, m_midiInDev);
	DDX_Control(pDX, IDC_SASAMI_MIDI_INCH, m_midiInCh);
	DDX_Control(pDX, IDC_SASAMI_MIDI_INMODE, m_midiInMode);
	DDX_Control(pDX, IDC_SASAMI_MIDI_ARR, m_btnArr);
	DDX_Control(pDX, IDC_SASAMI_MIDI_LAYOUT, m_btnLayout);
	DDX_Control(pDX, IDC_SASAMI_MIDI_CHORD, m_btnChord);
	DDX_Control(pDX, IDC_SASAMI_MIDI_PATT, m_btnPatt);
	DDX_Control(pDX, IDC_SASAMI_MIDI_ROLL, m_btnRoll);
	DDX_Control(pDX, IDC_SASAMI_MIDI_FOLLOW, m_follow);
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
	ON_BN_CLICKED(IDC_SASAMI_MIDI_ARR, &CSasamiMidiScoreDlg::OnBnClickedArr)
	ON_BN_CLICKED(IDC_SASAMI_MIDI_LAYOUT, &CSasamiMidiScoreDlg::OnBnClickedLayout)
	ON_BN_CLICKED(IDC_SASAMI_MIDI_CHORD, &CSasamiMidiScoreDlg::OnBnClickedChord)
	ON_BN_CLICKED(IDC_SASAMI_MIDI_PATT, &CSasamiMidiScoreDlg::OnBnClickedPatt)
	ON_BN_CLICKED(IDC_SASAMI_MIDI_ROLL, &CSasamiMidiScoreDlg::OnBnClickedRoll)
	ON_CBN_SELCHANGE(IDC_SASAMI_MIDI_CH, &CSasamiMidiScoreDlg::OnCbnSelchangeCh)
	ON_CBN_SELCHANGE(IDC_SASAMI_MIDI_INDEV, &CSasamiMidiScoreDlg::OnCbnMidiIn)
	ON_CBN_SELCHANGE(IDC_SASAMI_MIDI_INCH, &CSasamiMidiScoreDlg::OnCbnMidiIn)
	ON_CBN_SELCHANGE(IDC_SASAMI_MIDI_INMODE, &CSasamiMidiScoreDlg::OnCbnMidiIn)
	ON_CBN_SELCHANGE(IDC_SASAMI_MIDI_FOLLOW, &CSasamiMidiScoreDlg::OnCbnFollow)
	ON_CBN_SELCHANGE(IDC_SASAMI_STRIP_KIND0, &CSasamiMidiScoreDlg::OnCbnStrip)
	ON_CBN_SELCHANGE(IDC_SASAMI_STRIP_KIND1, &CSasamiMidiScoreDlg::OnCbnStrip)
	ON_CBN_SELCHANGE(IDC_SASAMI_STRIP_KIND2, &CSasamiMidiScoreDlg::OnCbnStrip)
	ON_CBN_SELCHANGE(IDC_SASAMI_STRIP_HGT0, &CSasamiMidiScoreDlg::OnCbnStrip)
	ON_CBN_SELCHANGE(IDC_SASAMI_STRIP_HGT1, &CSasamiMidiScoreDlg::OnCbnStrip)
	ON_CBN_SELCHANGE(IDC_SASAMI_STRIP_HGT2, &CSasamiMidiScoreDlg::OnCbnStrip)
	ON_CBN_SELCHANGE(IDC_SASAMI_STRIP_DRAW, &CSasamiMidiScoreDlg::OnCbnStrip)
	ON_CBN_SELCHANGE(IDC_SASAMI_STRIP_LANES, &CSasamiMidiScoreDlg::OnCbnStrip)
	ON_CBN_SELCHANGE(IDC_SASAMI_STRIP_STEP, &CSasamiMidiScoreDlg::OnCbnStrip)
	ON_CBN_SELCHANGE(IDC_SASAMI_PASTE_MODE, &CSasamiMidiScoreDlg::OnCbnPasteMode)
	ON_MESSAGE(WM_SASAMI_PAL_DUR, &CSasamiMidiScoreDlg::OnPalDur)
	ON_MESSAGE(WM_SASAMI_PAL_QUERY_STATE, &CSasamiMidiScoreDlg::OnPalQueryState)
	ON_MESSAGE(WM_SASAMI_PAL_LAYOUT, &CSasamiMidiScoreDlg::OnPalLayout)
	ON_MESSAGE(WM_SASAMI_NOTE_PROPS, &CSasamiMidiScoreDlg::OnNoteProps)
	ON_MESSAGE(WM_SASAMI_EXC_RPN_CHANGED, &CSasamiMidiScoreDlg::OnExcRpnChanged)
	ON_MESSAGE(WM_SASAMI_INSERT_FX_CHANGED, &CSasamiMidiScoreDlg::OnInsertFxChanged)
	ON_MESSAGE(WM_APP + 61, &CSasamiMidiScoreDlg::OnDeferredInit)
	ON_MESSAGE(WM_APP + 62, &CSasamiMidiScoreDlg::OnDeferredPushText)
	ON_MESSAGE(WM_APP + 7204, &CSasamiMidiScoreDlg::OnDeferredOpenVst)
	ON_MESSAGE(WM_APP + 7206, &CSasamiMidiScoreDlg::OnDeferredProgLabels)
	ON_MESSAGE(WM_VST_LIVE_EDITOR_CLOSED, &CSasamiMidiScoreDlg::OnVstEditorClosed)
	ON_MESSAGE(WM_VST_LIVE_EDITOR_CLOSED_UI, &CSasamiMidiScoreDlg::OnVstEditorClosedUi)
	ON_MESSAGE(WM_SASAMI_SCORE_MIDI, &CSasamiMidiScoreDlg::OnScoreMidi)
END_MESSAGE_MAP()

void CSasamiMidiScoreDlg::ApplyLang()
{
	SetWindowText(LL14(L"SASAMI MIDI譜面", L"SASAMI MIDI Score", L"Partition MIDI SASAMI", L"Partitura MIDI SASAMI", L"Partitura MIDI SASAMI",
		L"SASAMI MIDI 악보", L"SASAMI MIDI 谱面", L"نتيجة SASAMI MIDI", L"Партитура SASAMI MIDI", L"SASAMI MIDI-Partitur",
		L"Partitura MIDI SASAMI", L"SASAMI MIDI-partituur", L"Partytura SASAMI MIDI", L"SASAMI MIDI Skor"));
	m_btnOpen.SetWindowText(LL14(L"開く", L"Open", L"Ouvrir", L"Apri", L"Abrir",
		L"열기", L"打开", L"فتح", L"Открыть", L"Öffnen", L"Abrir", L"Openen", L"Otwórz", L"Aç"));
	m_btnSave.SetWindowText(LL14(L"保存", L"Save", L"Enregistrer", L"Salva", L"Guardar",
		L"저장", L"保存", L"حفظ", L"Сохранить", L"Speichern", L"Salvar", L"Opslaan", L"Zapisz", L"Kaydet"));
	if (m_btnNew.GetSafeHwnd())
		m_btnNew.SetWindowText(LL14(L"新規", L"New", L"Nouveau", L"Nuovo", L"Nuevo",
			L"새로", L"新建", L"جديد", L"Новый", L"Neu", L"Novo", L"Nieuw", L"Nowy", L"Yeni"));
	m_btnPlay.SetWindowText(LL14(L"再生確認", L"Preview", L"Aperçu", L"Anteprima", L"Vista previa",
		L"미리듣기", L"试听", L"معاينة", L"Просмотр", L"Vorschau", L"Prévia", L"Voorbeeld", L"Podgląd", L"Önizle"));
	m_btnExport.SetWindowText(LL14(L"書き出し", L"Export", L"Exporter", L"Esporta", L"Exportar",
		L"내보내기", L"导出", L"تصدير", L"Экспорт", L"Exportieren", L"Exportar", L"Exporteren", L"Eksport", L"Dışa aktar"));
	m_btnHelp.SetWindowText(LL14(L"ヘルプ", L"Help", L"Aide", L"Guida", L"Ayuda",
		L"도움말", L"帮助", L"مساعدة", L"Справка", L"Hilfe", L"Ajuda", L"Help", L"Pomoc", L"Yardım"));
	m_btnTempo.SetWindowText(LL14(L"テンポ", L"Tempo", L"Tempo", L"Tempo", L"Tempo",
		L"템포", L"速度", L"إيقاع", L"Темп", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"Tempo"));
	m_btnPencil.SetWindowText(LL14(L"音符", L"Notes", L"Notes", L"Note", L"Notas",
		L"음표", L"音符", L"نغمات", L"Ноты", L"Noten", L"Notas", L"Noten", L"Nuty", L"Nota"));
	m_btnErase.SetWindowText(LL14(L"消しゴム", L"Erase", L"Gomme", L"Gomma", L"Borrar",
		L"지우개", L"橡皮", L"ممحاة", L"Ластик", L"Radierer", L"Borracha", L"Gum", L"Gumka", L"Silgi"));
	m_btnSel.SetWindowText(LL14(L"選択", L"Select", L"Sélection", L"Selezione", L"Seleccionar",
		L"선택", L"选择", L"تحديد", L"Выбор", L"Auswahl", L"Selecionar", L"Selecteren", L"Zaznacz", L"Seç"));
	m_btnPal.SetWindowText(LL14(L"音符", L"Notes", L"Notes", L"Note", L"Notas",
		L"음표", L"音符", L"نغمات", L"Ноты", L"Noten", L"Notas", L"Noten", L"Nuty", L"Notalar"));
	m_btnPropUpd.SetWindowText(LL14(L"更新", L"Update", L"MAJ", L"Aggiorna", L"Actualizar",
		L"업데이트", L"更新", L"تحديث", L"Обновить", L"Aktualisieren", L"Atualizar", L"Bijwerken", L"Aktualizuj", L"Güncelle"));
	m_btnMark.SetWindowText(L">|");
	m_btnLoopA.SetWindowText(L"A");
	m_btnLoopB.SetWindowText(L"B");
	m_btnLoopClr.SetWindowText(L"A-Bx");
	m_btnShowAll.SetWindowText(L"32ch");
	if (m_btnText.GetSafeHwnd())
		m_btnText.SetWindowText(LL14(L"テキスト", L"Text", L"Texte", L"Testo", L"Texto",
			L"텍스트", L"文本", L"نص", L"Текст", L"Text", L"Texto", L"Tekst", L"Tekst", L"Metin"));
	if (m_btnFx.GetSafeHwnd())
		m_btnFx.SetWindowText(LL14(L"インサートFX", L"Insert FX", L"FX d’insertion", L"FX insert", L"FX de inserción",
			L"인서트 FX", L"插入FX", L"إدراج FX", L"Вставка FX", L"Insert-FX", L"FX de inserção", L"Insert-FX", L"FX wstawiania", L"Insert FX"));
	if (m_btnArr.GetSafeHwnd())
		m_btnArr.SetWindowText(LL14(L"アレンジ", L"Arrange", L"Arranger", L"Arrangia", L"Arreglar",
			L"어레인지", L"编曲", L"ترتيب", L"Аранжировка", L"Arrange", L"Arranjo", L"Arrangeren", L"Aranż", L"Aranje"));
	if (m_btnLayout.GetSafeHwnd())
		m_btnLayout.SetWindowText(LL14(L"譜表", L"Layout", L"Portée", L"Impaginazione", L"Diseño",
			L"보표", L"谱表", L"تخطيط", L"Партитура", L"Notation", L"Layout", L"Layout", L"Układ", L"Düzen"));
	if (m_btnChord.GetSafeHwnd())
		m_btnChord.SetWindowText(LL14(L"和音", L"Chord", L"Accord", L"Accordo", L"Acorde", L"화음", L"和弦", L"وتر", L"Аккорд", L"Akkord", L"Acorde", L"Akkoord", L"Akor", L"Akor"));
	if (m_btnPatt.GetSafeHwnd())
		m_btnPatt.SetWindowText(LL14(L"パターン", L"Pattern", L"Motif", L"Pattern", L"Patrón", L"패턴", L"型", L"نمط", L"Паттерн", L"Muster", L"Padrão", L"Patroon", L"Wzorzec", L"Desen"));
	if (m_btnRoll.GetSafeHwnd())
		m_btnRoll.SetWindowText(LL14(L"ピアノロール", L"Piano roll", L"Piano roll", L"Piano roll", L"Piano roll",
			L"피아노 롤", L"钢琴卷帘", L"رول البيانو", L"Пианоролл", L"Klavierrolle", L"Piano roll", L"Piano-roll", L"Rolka", L"Piyano rulosu"));
	if (m_stripLanes.GetSafeHwnd())
		SyncStripCombos();
	SyncMidiInCombos();
	UpdateHelpBar();
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
	if (m_btnArr.GetSafeHwnd()) flatBtn(m_btnArr);
	if (m_btnLayout.GetSafeHwnd()) flatBtn(m_btnLayout);
	if (m_btnChord.GetSafeHwnd()) flatBtn(m_btnChord);
	if (m_btnPatt.GetSafeHwnd()) flatBtn(m_btnPatt);
	if (m_btnRoll.GetSafeHwnd()) flatBtn(m_btnRoll);
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
	m_stripKind2.SetAeroMode(FALSE);
	m_stripDraw.SetAeroMode(FALSE);
	m_stripLanes.SetAeroMode(FALSE);
	if (m_stripStep.GetSafeHwnd()) m_stripStep.SetAeroMode(FALSE);
	auto mkStripHgt = [&](CCustomComboBox& cb, int id) {
		if (!cb.GetSafeHwnd())
			cb.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
				CRect(0, 0, 44, 120), this, id);
		cb.SetAeroMode(FALSE);
	};
	mkStripHgt(m_stripHgt0, IDC_SASAMI_STRIP_HGT0);
	mkStripHgt(m_stripHgt1, IDC_SASAMI_STRIP_HGT1);
	mkStripHgt(m_stripHgt2, IDC_SASAMI_STRIP_HGT2);
	if (m_midiInDev.GetSafeHwnd()) m_midiInDev.SetAeroMode(FALSE);
	if (m_midiInCh.GetSafeHwnd()) m_midiInCh.SetAeroMode(FALSE);
	if (m_midiInMode.GetSafeHwnd()) m_midiInMode.SetAeroMode(FALSE);
	if (m_follow.GetSafeHwnd()) m_follow.SetAeroMode(FALSE);
	if (m_helpBar.GetSafeHwnd()) m_helpBar.SetAeroMode(FALSE);
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
	m_status.SetWindowText(LL14(L"Tone=VST。Exc/RPNレーン=SysEx・RPN。ツールバー「Insert FX」=パート・インサートEFX。", L"Tone=VST. Exc/RPN lane=SysEx/RPN. Toolbar Insert FX=per-part insert EFX.", L"Tone=VST. Piste Exc/RPN=SysEx/RPN. Insert FX=EFX d’insertion par partie.", L"Tone=VST. Corsia Exc/RPN=SysEx/RPN. Insert FX=EFX insert per parte.", L"Tone=VST. Pista Exc/RPN=SysEx/RPN. Insert FX=EFX de inserción por parte.", L"Tone=VST. Exc/RPN 레인=SysEx/RPN. Insert FX=파트 인서트 EFX.", L"Tone=VST。Exc/RPN 条带=SysEx/RPN。Insert FX=声部插入 EFX。", L"Tone=VST. مسار Exc/RPN=SysEx/RPN. Insert FX=EFX إدراج لكل جزء.", L"Tone=VST. Полоса Exc/RPN=SysEx/RPN. Insert FX=EFX вставки по партии.", L"Tone=VST. Exc/RPN-Spur=SysEx/RPN. Insert FX=Insert-EFX je Part.", L"Tone=VST. Faixa Exc/RPN=SysEx/RPN. Insert FX=EFX de inserção por parte.", L"Tone=VST. Exc/RPN-baan=SysEx/RPN. Insert FX=insert-EFX per partij.", L"Tone=VST. Pas Exc/RPN=SysEx/RPN. Insert FX=EFX wstawiania na partię.", L"Tone=VST. Exc/RPN şeridi=SysEx/RPN. Insert FX=parti insert EFX."));
	ScStaffLoadPartStrip(&m_ui, m_curCh);
	SyncStripCombos();
	if (!m_pasteMode.GetSafeHwnd())
		m_pasteMode.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
			CRect(0, 0, 100, 120), this, IDC_SASAMI_PASTE_MODE);
	m_pasteMode.ResetContent();
	m_pasteMode.AddString(LL14(L"上書き", L"Overwrite", L"Écraser", L"Sovrascrivi", L"Sobrescribir",
		L"덮어쓰기", L"覆盖", L"استبدال", L"Замена", L"Überschreiben", L"Sobrescrever", L"Overschrijven", L"Nadpisz", L"Üzerine yaz"));
	m_pasteMode.AddString(LL14(L"挿入", L"Insert", L"Insérer", L"Inserisci", L"Insertar",
		L"삽입", L"插入", L"إدراج", L"Вставка", L"Einfügen", L"Inserir", L"Invoegen", L"Wstaw", L"Ekle"));
	m_pasteMode.SetCurSel(0);
	m_pasteMode.SetAeroMode(FALSE);
	m_ui.tool = SC_TOOL_SELECT;
	m_ui.helpTopic = SC_HELP_SELECT;
	m_ui.markerSolidTrack = m_curCh;
	m_ui.pasteInsert = 0;
	UpdateNoteCursor();
	RefreshProgLabels();
	ScStaffDrawCursorsInit(&m_drawCursors);
	RefreshStrip();
	LayoutChrome();
	RestoreUiGeom();
	RefreshPartEnabled();
	SetupTooltips();
	UpdateHelpBar();
	/* Defer VST sync + ednotify poll: sync on init can trip unloaded engine CS. */
	PostMessage(WM_APP + 61, 0, 0);
	return TRUE;
}

BOOL CSasamiMidiScoreDlg::PreTranslateMessage(MSG* pMsg)
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
		const int ctrl = (GetKeyState(VK_CONTROL) & 0x8000) ? 1 : 0;
		const int shift = (GetKeyState(VK_SHIFT) & 0x8000) ? 1 : 0;
		if (pMsg->wParam == VK_DELETE || pMsg->wParam == VK_BACK || pMsg->wParam == VK_ESCAPE ||
			(ctrl && (pMsg->wParam == 'Z' || pMsg->wParam == 'z' || pMsg->wParam == 'Y' || pMsg->wParam == 'y' ||
				pMsg->wParam == 'C' || pMsg->wParam == 'c' || pMsg->wParam == 'V' || pMsg->wParam == 'v' ||
				pMsg->wParam == 'X' || pMsg->wParam == 'x' || pMsg->wParam == 'A' || pMsg->wParam == 'a' ||
				pMsg->wParam == 'T' || pMsg->wParam == 't' || pMsg->wParam == 'I' || pMsg->wParam == 'i')) ||
			(ctrl && shift && (pMsg->wParam == 'V' || pMsg->wParam == 'v' ||
				pMsg->wParam == 'I' || pMsg->wParam == 'i'))) {
			OnKeyDown((UINT)pMsg->wParam, 1, 0);
			return TRUE;
		}
	}
	return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}

void CSasamiMidiScoreDlg::OnCbnPasteMode()
{
	m_ui.pasteInsert = (m_pasteMode.GetCurSel() == 1) ? 1 : 0;
}

void CSasamiMidiScoreDlg::SetupTooltips()
{
	if (!CCustomControlUtility::BeginDialogToolTip(m_tooltip, this, TTS_NOPREFIX)) return;
	auto tip = [&](CWnd& w, LPCWSTR s) { if (w.GetSafeHwnd()) m_tooltip.AddTool(&w, s); };
	tip(m_btnOpen, LL14(L"MPY/MPW/MMLを開く（テキスト作曲で再編集）", L"Open MPY/MPW/MML — re-edit via text composer", L"Ouvrir MPY/MPW/MML", L"Apri MPY/MPW/MML", L"Abrir MPY/MPW/MML",
		L"MPY/MPW/MML 열기", L"打开 MPY/MPW/MML", L"فتح MPY/MPW/MML", L"Открыть MPY/MPW/MML", L"MPY/MPW/MML öffnen", L"Abrir MPY/MPW/MML", L"MPY/MPW/MML openen", L"Otwórz MPY/MPW/MML", L"MPY/MPW/MML aç"));
	tip(m_btnSave, LL14(L".mpw2 / .mpsmv として保存（VST binds → mpsmv）", L"Save As .mpw2 / .mpsmv (VST binds → mpsmv)", L"Enregistrer .mpw2 / .mpsmv", L"Salva .mpw2 / .mpsmv", L"Guardar .mpw2 / .mpsmv",
		L".mpw2 / .mpsmv로 저장", L"另存为 .mpw2 / .mpsmv", L"حفظ .mpw2 / .mpsmv", L"Сохранить .mpw2 / .mpsmv", L"Speichern als .mpw2 / .mpsmv", L"Salvar .mpw2 / .mpsmv", L"Opslaan als .mpw2 / .mpsmv", L"Zapisz jako .mpw2 / .mpsmv", L".mpw2 / .mpsmv kaydet"));
	tip(m_btnNew, LL14(L"新規 — 譜面とテキストセッションをクリア", L"New — clear score and text session", L"Nouveau — effacer", L"Nuovo — svuota", L"Nuevo — limpiar",
		L"새로 — 악보/텍스트 지우기", L"新建 — 清空谱面与文本", L"جديد — مسح", L"Новый — очистить", L"Neu — leeren", L"Novo — limpar", L"Nieuw — wissen", L"Nowy — wyczyść", L"Yeni — temizle"));
	tip(m_btnPlay, LL14(L"マーカーからプレビュー（Space=再生/停止）", L"Preview from marker (Space = play/stop)", L"Aperçu depuis le marqueur (Espace)", L"Anteprima dal marcatore (Spazio)", L"Vista previa desde marcador (Espacio)",
		L"마커부터 미리듣기 (Space)", L"从标记预览（Space）", L"معاينة من العلامة (Space)", L"Превью с маркера (Space)", L"Vorschau ab Markierung (Leertaste)", L"Prévia do marcador (Espaço)", L"Voorbeeld vanaf markering (Spatie)", L"Podgląd od znacznika (Spacja)", L"İşaretten önizle (Space)"));
	tip(m_btnExport, LL14(L"コンパイルMIDIからオーディオ書き出し", L"Audio export from compiled MIDI", L"Export audio depuis MIDI compilé", L"Esporta audio da MIDI compilato", L"Exportar audio desde MIDI compilado",
		L"컴파일 MIDI에서 오디오보내기", L"从编译MIDI导出音频", L"تصدير صوت من MIDI", L"Экспорт аудио из MIDI", L"Audio-Export aus MIDI", L"Exportar áudio do MIDI", L"Audio exporteren uit MIDI", L"Eksport audio z MIDI", L"MIDI'den ses dışa aktar"));
	tip(m_btnPencil, LL14(L"音符ツール — 五線に音符・休符を配置（CCレーン描画は下の「鉛筆/直線/曲線」コンボ）", L"Note tool — place notes/rests on staff (CC lanes use Pencil/Line/Curve combo below)", L"Notes — portée (CC = combo)", L"Note — pentagramma (CC = combo)", L"Notas — pentagrama (CC = combo)",
		L"음표 — 보표 (CC는 아래 콤보)", L"音符 — 五线谱（CC用下方组合框）", L"نغمات", L"Ноты", L"Noten", L"Notas", L"Noten", L"Nuty", L"Nota"));
	tip(m_btnErase, LL14(L"消しゴム — クリック/ドラッグで削除", L"Eraser — click or drag over notes to delete", L"Gomme — clic/glisser pour supprimer", L"Gomma — clic/trascina per cancellare", L"Borrar — clic/arrastrar para borrar",
		L"지우개 — 클릭/드래그 삭제", L"橡皮 — 点击/拖动删除", L"ممحاة — انقر/اسحب للحذف", L"Ластик — клик/перетаскивание", L"Radierer — Klick/Ziehen löscht", L"Borracha — clique/arraste", L"Gum — klik/sleep wissen", L"Gumka — klik/przeciągnij", L"Silgi — tıkla/sürükle"));
	tip(m_btnSel, LL14(L"選択 — クリック/ドラッグ、Deleteで削除", L"Select — click/drag notes or strip chips; Delete removes", L"Sélection — clic/glisser; Suppr", L"Selezione — clic/trascina; Canc", L"Seleccionar — clic/arrastrar; Supr",
		L"선택 — 클릭/드래그, Delete 삭제", L"选择 — 点击/拖动；Delete删除", L"تحديد — نقر/سحب؛ Delete", L"Выбор — клик/перетаскивание; Delete", L"Auswahl — Klick/Ziehen; Entf", L"Selecionar — clique/arraste; Del", L"Selecteren — klik/sleep; Del", L"Zaznacz — klik/przeciągnij; Del", L"Seç — tıkla/sürükle; Del"));
	tip(m_pasteMode, LL14(L"ペースト: 上書き / 挿入（Ctrl+Shift+V=常に挿入）", L"Paste: overwrite / insert (Ctrl+Shift+V=always insert)", L"Coller: écraser / insérer (Ctrl+Shift+V)", L"Incolla: sovrascrivi / inserisci (Ctrl+Shift+V)", L"Pegar: sobrescribir / insertar (Ctrl+Shift+V)",
		L"붙여넣기: 덮어쓰기/삽입 (Ctrl+Shift+V)", L"粘贴：覆盖/插入（Ctrl+Shift+V）", L"لصق: استبدال/إدراج (Ctrl+Shift+V)", L"Вставка: замена/вставка (Ctrl+Shift+V)", L"Einfügen: überschreiben/einfügen (Ctrl+Shift+V)", L"Colar: sobrescrever/inserir (Ctrl+Shift+V)", L"Plakken: overschrijven/invoegen (Ctrl+Shift+V)", L"Wklej: nadpisz/wstaw (Ctrl+Shift+V)", L"Yapıştır: üzerine yaz/ekle (Ctrl+Shift+V)"));
	tip(m_btnPal, LL14(L"音価パレット", L"Note duration palette", L"Palette de durées", L"Tavolozza durate", L"Paleta de duraciones",
		L"음가 팔레트", L"时值调色板", L"لوحة المدد", L"Палитра длительностей", L"Notenwerte-Palette", L"Paleta de durações", L"Duur-palet", L"Paleta wartości", L"Süre paleti"));
	tip(m_btnTempo, LL14(L"テンポ — 赤マーカー位置から次の変化点まで BPM を一括設定", L"Tempo — set BPM at red marker through next tempo change", L"Tempo — BPM au marqueur", L"Tempo — BPM al marcatore", L"Tempo — BPM en marcador",
		L"템포 — 마커부터 BPM", L"速度 — 红标记到下一变化点", L"إيقاع", L"Темп", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"Tempo"));
	tip(m_btnHelp, LL14(L"コマンド説明・譜面操作ガイド", L"Command help and score guide", L"Aide commandes / partition", L"Guida comandi / partitura", L"Ayuda comandos / partitura",
		L"명령 설명·악보 가이드", L"命令说明与谱面指南", L"دليل الأوامر والدرجات", L"Справка по командам и партитуре", L"Befehls-/Partiturhilfe", L"Ajuda de comandos / partitura", L"Commando-/partituurhulp", L"Pomoc: komendy / partytura", L"Komut / parti yardımı"));
	tip(m_btnText, LL14(L"テキスト作曲を開く（MIDI/MICP MML）", L"Open text composer (MIDI/MICP MML)", L"Ouvrir compositeur texte", L"Apri compositore testo", L"Abrir compositor de texto",
		L"텍스트 작곡 열기", L"打开文本作曲", L"فتح مؤلف النص", L"Открыть текстовый композер", L"Textkomponist öffnen", L"Abrir compositor de texto", L"Tekstcomponist openen", L"Otwórz kompozytor tekstowy", L"Metin besteciyi aç"));
	tip(m_btnFx, LL14(L"インサートFX — VSTエフェクトチェーン", L"Insert FX — VST effect chain (@VSTFX)", L"Insert FX — chaîne VST", L"Insert FX — catena VST", L"Insert FX — cadena VST",
		L"인서트 FX — VST 이펙트", L"插入FX — VST效果链", L"إدراج FX — سلسلة VST", L"Insert FX — цепочка VST", L"Insert FX — VST-Kette", L"Insert FX — cadeia VST", L"Insert FX — VST-keten", L"Insert FX — łańcuch VST", L"Insert FX — VST zinciri"));
	tip(m_btnMark, LL14(L"マーカー — ルーラークリックで再生位置", L"Mark tool — ruler click sets play-from marker", L"Marqueur — clic règle", L"Marcatore — clic righello", L"Marcador — clic regla",
		L"마커 — 눈금자 클릭", L"标记 — 点标尺", L"علامة — نقر المسطرة", L"Маркер — клик по линейке", L"Markierung — Klick Lineal", L"Marcador — clique régua", L"Markering — klik liniaal", L"Znacznik — klik linijki", L"İşaret — cetvele tıkla"));
	tip(m_btnShowAll, LL14(L"全32 MIDIチャンネルを表示", L"Show all 32 MIDI channels on staff", L"Afficher les 32 canaux MIDI", L"Mostra tutti i 32 canali MIDI", L"Mostrar los 32 canales MIDI",
		L"MIDI 32채널 모두 표시", L"显示全部32个MIDI通道", L"إظهار كل 32 قناة MIDI", L"Показать все 32 MIDI-канала", L"Alle 32 MIDI-Kanäle zeigen", L"Mostrar os 32 canais MIDI", L"Toon alle 32 MIDI-kanalen", L"Pokaż wszystkie 32 kanały MIDI", L"32 MIDI kanalını göster"));
	tip(m_ch, LL14(L"編集対象チャンネル (MIDI 1–32)", L"Edit target channel (MIDI 1–32)", L"Canal cible (MIDI 1–32)", L"Canale destinazione (MIDI 1–32)", L"Canal objetivo (MIDI 1–32)",
		L"편집 채널 (MIDI 1–32)", L"编辑通道 (MIDI 1–32)", L"قناة التحرير (MIDI 1–32)", L"Канал правки (MIDI 1–32)", L"Zielkanal (MIDI 1–32)", L"Canal de edição (MIDI 1–32)", L"Bewerkkanaal (MIDI 1–32)", L"Kanał edycji (MIDI 1–32)", L"Düzenleme kanalı (MIDI 1–32)"));
	tip(m_stripKind0, LL14(L"レーン1の種類: Exp/Vol/Pitch/Gate/Pan", L"Lane 1 kind: Exp/Vol/Pitch/Gate/Pan", L"Type piste 1: Exp/Vol/Pitch/Gate/Pan", L"Tipo corsia 1: Exp/Vol/Pitch/Gate/Pan", L"Tipo pista 1: Exp/Vol/Pitch/Gate/Pan",
		L"레인1 종류: Exp/Vol/Pitch/Gate/Pan", L"车道1类型: Exp/Vol/Pitch/Gate/Pan", L"نوع الممر 1: Exp/Vol/Pitch/Gate/Pan", L"Тип дорожки 1: Exp/Vol/Pitch/Gate/Pan", L"Spur 1-Art: Exp/Vol/Pitch/Gate/Pan", L"Tipo faixa 1: Exp/Vol/Pitch/Gate/Pan", L"Baan 1-soort: Exp/Vol/Pitch/Gate/Pan", L"Rodzaj toru 1: Exp/Vol/Pitch/Gate/Pan", L"Şerit 1 türü: Exp/Vol/Pitch/Gate/Pan"));
	tip(m_stripKind1, LL14(L"レーン2の種類: Exp/Vol/Pitch/Gate/Pan", L"Lane 2 kind: Exp/Vol/Pitch/Gate/Pan", L"Type piste 2", L"Tipo corsia 2", L"Tipo pista 2",
		L"레인2 종류", L"车道2类型", L"نوع الممر 2", L"Тип дорожки 2", L"Spur 2-Art", L"Tipo faixa 2", L"Baan 2-soort", L"Rodzaj toru 2", L"Şerit 2 türü"));
	tip(m_stripKind2, LL14(L"レーン3の種類: Exp/Vol/Pitch/Gate/Pan", L"Lane 3 kind: Exp/Vol/Pitch/Gate/Pan", L"Type piste 3", L"Tipo corsia 3", L"Tipo pista 3",
		L"레인3 종류", L"车道3类型", L"نوع الممر 3", L"Тип дорожки 3", L"Spur 3-Art", L"Tipo faixa 3", L"Baan 3-soort", L"Rodzaj toru 3", L"Şerit 3 türü"));
	tip(m_stripDraw, LL14(L"CCレーン描画: 鉛筆/直線/曲線（テンポ帯・パートCC・下ストリップ）", L"CC lane draw: Pencil/Line/Curve (tempo band, part CC, bottom strip)", L"Dessin CC", L"Disegno CC", L"Dibujo CC",
		L"스트립 그리기: 연필/직선/곡선", L"条带绘制: 铅笔/直线/曲线", L"رسم الشريط: قلم/خط/منحنى", L"Рисование полосы: карандаш/линия/кривая", L"Strip-Zeichnung: Stift/Linie/Kurve", L"Desenho faixa: Lápis/Linha/Curva", L"Strooktekenen: Potlood/Lijn/Kromme", L"Rysowanie pasa: Ołówek/Linia/Krzywa", L"Şerit çizimi: Kalem/Çizgi/Eğri"));
	tip(m_stripLanes, LL14(L"CCレーン数: なし / ×1 / ×2 / ×3（パート別）", L"CC lane count: none / ×1 / ×2 / ×3 (per part)", L"Nb pistes CC: aucune / ×1–3", L"N. corsie CC: nessuna / ×1–3", L"Nº pistas CC: ninguna / ×1–3",
		L"CC 레인 수: 없음 / ×1–3 (파트별)", L"CC车道数: 无 / ×1–3（按声部）", L"عدد ممرات CC: بلا / ×1–3", L"Число CC-дорожек: нет / ×1–3", L"CC-Spuren: keine / ×1–3", L"Nº faixas CC: nenhuma / ×1–3", L"CC-banen: geen / ×1–3", L"Liczba torów CC: brak / ×1–3", L"CC şerit sayısı: yok / ×1–3"));
	tip(m_stripStep, LL14(L"ストリップ横解像度: 4分〜64分", L"Strip horizontal resolution: 1/4–1/64", L"Résolution horizontale: 1/4–1/64", L"Risoluzione orizzontale: 1/4–1/64", L"Resolución horizontal: 1/4–1/64",
		L"스트립 가로 해상도: 1/4–1/64", L"条带横向分辨率: 1/4–1/64", L"دقة أفقية: 1/4–1/64", L"Гор. разрешение: 1/4–1/64", L"Horiz. Auflösung: 1/4–1/64", L"Resolução horizontal: 1/4–1/64", L"Horiz. resolutie: 1/4–1/64", L"Rozdzielczość pozioma: 1/4–1/64", L"Yatay çözünürlük: 1/4–1/64"));
	tip(m_status, LL14(L"Space=マーカーから再生/停止 · 鉛筆で配置時発音 · Select+Delete", L"Space=play/stop from marker · Pencil auditions · Select+Delete", L"Espace=lecture/arrêt · Crayon audition · Sélection+Suppr", L"Spazio=play/stop · Matita audizione · Selezione+Canc", L"Espacio=play/stop · Lápiz audición · Seleccionar+Supr",
		L"Space=마커 재생/정지 · 연필 미리듣기 · 선택+Delete", L"Space=从标记播放/停止 · 铅笔试听 · 选择+Delete", L"Space=تشغيل/إيقاف · قلم يسمع · تحديد+Delete", L"Space=старт/стоп · Карандаш озвучивает · Выбор+Delete", L"Leertaste=Play/Stop · Stift hörbar · Auswahl+Entf", L"Espaço=play/parar · Lápis audiciona · Selecionar+Del", L"Spatie=play/stop · Potlood auditeert · Selecteren+Del", L"Spacja=play/stop · Ołówek gra · Zaznacz+Del", L"Space=çal/dur · Kalem duyulur · Seç+Del"));
	tip(m_midiInDev, LL14(L"譜面専用 MIDI In デバイス（Host Inとは別）", L"Score MIDI In device (independent of Host In)", L"Périphérique MIDI In partition", L"Dispositivo MIDI In partitura", L"Dispositivo MIDI In partitura",
		L"악보 전용 MIDI In", L"谱面专用 MIDI In", L"جهاز MIDI In للنوتة", L"MIDI In партитуры", L"Partitur-MIDI-In", L"MIDI In da partitura", L"Partituur-MIDI-In", L"MIDI In partytury", L"Parti MIDI In"));
	tip(m_midiInCh, LL14(L"MIDI In チャンネルフィルタ (All / 1–16)", L"MIDI In channel filter (All / 1–16)", L"Filtre canal MIDI In", L"Filtro canale MIDI In", L"Filtro canal MIDI In",
		L"MIDI In 채널 필터", L"MIDI In 通道过滤", L"مرشح قناة MIDI In", L"Фильтр канала MIDI In", L"MIDI-In-Kanalfilter", L"Filtro de canal MIDI In", L"MIDI-In-kanaalfilter", L"Filtr kanału MIDI In", L"MIDI In kanal filtresi"));
	tip(m_midiInMode, LL14(L"OFF / Step入力 / Realtime録音", L"OFF / Step entry / Realtime record", L"OFF / Pas / Temps réel", L"OFF / Step / Realtime", L"OFF / Paso / Tiempo real",
		L"OFF / 스텝 / 실시간", L"关 / 步进 / 实时", L"OFF / خطوة / فوري", L"OFF / Шаг / Реалтайм", L"AUS / Schritt / Echtzeit", L"OFF / Passo / Tempo real", L"UIT / Stap / Realtime", L"WYŁ / Krok / Realtime", L"KAPALI / Adım / Gerçek zaman"));
	tip(m_follow, LL14(L"再生ヘッド追従: OFF / 中央 / ページ", L"Playhead follow: OFF / Center / Page", L"Suivi tête: OFF / Centre / Page", L"Segui playhead: OFF / Centro / Pagina", L"Seguir playhead: OFF / Centro / Página",
		L"재생헤드 추종", L"播放头跟随", L"تتبع رأس التشغيل", L"Следование за курсором", L"Playhead-Folge", L"Seguir playhead", L"Playhead volgen", L"Śledzenie playhead", L"Playhead takibi"));
	tip(m_btnArr, LL14(L"アレンジプリセット（Humanize等）", L"Arrange presets (Humanize etc.)", L"Préréglages arrange", L"Preset arrange", L"Presets arrange",
		L"어레인지 프리셋", L"编曲预设", L"إعدادات الترتيب", L"Пресеты аранжировки", L"Arrange-Presets", L"Presets de arranjo", L"Arrange-presets", L"Presety aranżacji", L"Arrange önayarları"));
	tip(m_btnLayout, LL14(L"譜表 — 拍子・調号・移調（マーカー位置の小節頭）", L"Layout — meter, key, transpose (marker bar line)", L"Portée — mesure, armure, transposition", L"Impag. — misura, armatura, trasposizione", L"Layout — compás, armadura, transposición",
		L"보표 — 박자·조표·이조", L"谱表 — 拍号·调号·移调", L"تخطيط — إيقاع ونقل", L"Партитура — размер, тональность", L"Notation — Takt, Tonart, Transponieren", L"Layout — compasso, armadura", L"Layout — maat, toonsoort", L"Układ — metrum, tonacja", L"Düzen — ölçü, armatür, transpoz"));
	tip(m_btnChord, LL14(L"和音タイプを選ぶとレ点が付き、外すまで鉛筆配置が和音のまま。ホバーも和音表示", L"Pick a chord type (check sticks until unchecked). Pencil places full chords; hover matches.", L"Type d'accord sticky", L"Tipo accordo sticky", L"Tipo de acorde sticky",
		L"화음 유형 선택 시 체크 유지", L"选和弦类型后勾选保持", L"نوع الوتر يبقى محدداً", L"Тип аккорда с галочкой", L"Akkordtyp bleibt angehakt", L"Tipo de acorde fica marcado", L"Akkoordtype blijft aangevinkt", L"Typ akordu zostaje zaznaczony", L"Akor tipi isaretli kalir"));
	tip(m_btnPatt, LL14(L"パターンを選ぶとレ点が付き、外すまでクリックで配置。ホバーもパターン表示", L"Pick a pattern (check sticks until unchecked). Click places it; hover shows the rhythm.", L"Motif sticky", L"Pattern sticky", L"Patrón sticky",
		L"패턴 선택 시 체크 유지", L"选型后勾选保持", L"النمط يبقى محدداً", L"Паттерн с галочкой", L"Muster bleibt angehakt", L"Padrão fica marcado", L"Patroon blijft aangevinkt", L"Wzorzec zostaje zaznaczony", L"Desen isaretli kalir"));
	tip(m_btnRoll, LL14(L"ピアノロール分割表示＋別窓", L"Piano roll split + floating window", L"Piano roll split + fenêtre", L"Piano roll split + finestra", L"Piano roll split + ventana",
		L"피아노 롤 분할+창", L"钢琴卷帘分割+浮动窗", L"رول بيانو مقسوم+نافذة", L"Пианоролл split+окно", L"Klavierrolle Split+Fenster", L"Piano roll split+janela", L"Piano-roll split+venster", L"Rolka split+okno", L"Piyano rulosu split+pencere"));
	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip);
}

void CSasamiMidiScoreDlg::LayoutChrome()
{
	if (!::IsWindow(m_hWnd) || m_bInLayout) return;
	m_bInLayout = TRUE;
	CRect rc;
	GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	const int pad = 8, btnH = 28, cbH = 32, btnW = 64;
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
	place(m_btnNew, btnW);
	place(m_btnPlay, 100);
	place(m_btnExport, btnW);
	place(m_btnPencil, btnW);
	place(m_btnErase, btnW);
	place(m_btnSel, btnW);
	place(m_btnTempo, btnW);
	if (m_btnPal.GetSafeHwnd()) m_btnPal.ShowWindow(SW_HIDE);
	if (m_ch.GetSafeHwnd()) { layoutCombo(m_ch, x, y, 104, btnH + 4); x += 112; }
	place(m_btnHelp, 48);
	y += btnH + 6;
	x = pad;
	const int propY = y;
	if (m_edNote.GetSafeHwnd()) m_edNote.MoveWindow(x + 28, propY, 44, 22);
	if (m_edGt.GetSafeHwnd()) m_edGt.MoveWindow(x + 104, propY, 44, 22);
	if (m_edVel.GetSafeHwnd()) m_edVel.MoveWindow(x + 180, propY, 44, 22);
	if (m_btnPropUpd.GetSafeHwnd()) m_btnPropUpd.MoveWindow(x + 232, propY, 52, btnH);
	y += btnH + 6;
	x = pad;
	layoutCombo(m_stripLanes, x, y, 120, cbH, 140); x += 126;
	layoutCombo(m_stripStep, x, y, 88, cbH, 120); x += 94;
	layoutCombo(m_stripDraw, x, y, 108, cbH, 140); x += 114;
	layoutCombo(m_pasteMode, x, y, 100, cbH, 120);
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
	place(m_btnLoopClr, 44);
	place(m_btnShowAll, 48);
	place(m_btnText, 68);
	place(m_btnRoll, 88);
	place(m_btnFx, 88);
	y += btnH + 6;
	x = pad;
	/* MIDI In / Arr row — combo closed height synced via layoutCombo */
	layoutCombo(m_midiInDev, x, y, 150, cbH); x += 156;
	layoutCombo(m_midiInCh, x, y, 64, cbH); x += 70;
	layoutCombo(m_midiInMode, x, y, 96, cbH); x += 102;
	place(m_btnArr, 64);
	if (m_btnLayout.GetSafeHwnd()) m_btnLayout.ShowWindow(SW_HIDE);
	place(m_btnChord, 52);
	place(m_btnPatt, 72);
	if (m_follow.GetSafeHwnd()) { m_follow.MoveWindow(x, y, 84, btnH); x += 90; }
	y += cbH + 4;
	if (m_status.GetSafeHwnd())
		m_status.MoveWindow(pad, y, max(200, rc.Width() - pad * 2), 20);
	y += 22;
	const int helpH = 56;
	const int rollH = m_ui.showRollSplit ? 140 : 0;
	const int bodyBotLimit = rc.Height() - pad - helpH - rollH;
	const int chromeBottom = y;
	m_bodyRc.SetRect(pad, chromeBottom, max(pad + 40, rc.Width()), max(chromeBottom + 40, bodyBotLimit));
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
	UpdateScrollBars();
	m_bInLayout = FALSE;
	if (m_bodyRc.Width() > 0)
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

void CSasamiMidiScoreDlg::UpdateScrollBars()
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
	if (below.Height() > 0) {
#if CCUSTOM_AERO_SUPPORT
		CCC_FillRectAlpha(pDC->GetSafeHdc(), below, RGB(236, 237, 242), 255);
#else
		pDC->FillSolidRect(&below, RGB(236, 237, 242));
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
		CCC_FillRectAlpha(pDC->GetSafeHdc(), side, RGB(236, 237, 242), 255);
#else
		pDC->FillSolidRect(&side, RGB(236, 237, 242));
#endif
	}
	return TRUE;
}

void CSasamiMidiScoreDlg::UpdateHelpBar()
{
	if (!m_helpBar.GetSafeHwnd()) return;
	wchar_t buf[512];
	ScStaffFormatHelpBar(buf, 512, &m_ui, 0, m_curCh);
	m_helpBar.SetWindowText(buf);
}

void CSasamiMidiScoreDlg::SyncStripCombos()
{
	auto fillKind = [](CCustomComboBox& cb, int sel) {
		cb.ResetContent();
		for (int k = 0; k < SC_STRIP_KIND_COUNT; k++)
			cb.AddString(ScStaffStripKindName(k));
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

void CSasamiMidiScoreDlg::OnCbnStrip()
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

void CSasamiMidiScoreDlg::RefreshStrip()
{
	ScStaffEnsureGlobalTempoFromDoc(&m_ui, m_doc.ev, m_doc.evCount, ScStaffBpmFromTempoT(m_doc.tempoT));
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
		ScStaffPaintTracks(mem, track, &m_ui, m_curCh, m_doc.ev, m_doc.evCount);
	if (grid.Width() > 2 && grid.Height() > 2)
		ScStaffPaintStaves(mem, grid, &m_ui, m_doc.ev, m_doc.evCount, 0, m_curCh, m_doc.tempoT);
	if (strip.Width() > 2 && strip.Height() > 2)
		ScStaffPaintStrip(mem, strip, &m_ui);
	if (m_ui.marqueeOn) {
		m_ui.marqueeX0 -= body.left; m_ui.marqueeX1 -= body.left;
		m_ui.marqueeY0 -= body.top; m_ui.marqueeY1 -= body.top;
		ScStaffPaintMarquee(mem, &m_ui);
		m_ui.marqueeX0 += body.left; m_ui.marqueeX1 += body.left;
		m_ui.marqueeY0 += body.top; m_ui.marqueeY1 += body.top;
	}
	CRect bodyRel(0, 0, body.Width(), body.Height());
	CRect viewRel(0, 0, m_gridRc.Width(), m_trackRc.bottom - m_bodyRc.top);
	/* Clear scroll gutter before thumbs — staff must not bleed into +/- / scrollbar. */
	const int sbGutter = ScStaffScrollGutterW();
	if (sbGutter > 0 && body.Width() > sbGutter)
		mem.FillSolidRect(body.Width() - sbGutter, 0, sbGutter, body.Height(), RGB(236, 237, 242));
	ScStaffPaintScrollThumbs(mem, bodyRel, bodyRel, viewRel, &m_ui, max(1, m_gridRc.Width()), max(1, m_gridRc.Height()),
		m_doc.ev, m_doc.evCount);
	CCC_BlitStretchOpaque(dc.GetSafeHdc(), body.left, body.top, body.Width(), body.Height(),
		mem.GetSafeHdc(), 0, 0, body.Width(), body.Height());
	mem.SelectObject(old);
	if (m_ui.showRollSplit && m_rollRc.Width() > 8)
		ScPianoRollPaint(dc, m_rollRc, &m_rollView, m_doc.ev, m_doc.evCount, &m_ui, m_curCh);
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
	/* ruler / tempo band / Prog strip: no ghost note */
	if (pt.y < ScStaffGridBodyTop(m_gridRc.top, &m_ui)) return;
	if (ScStaffPtInScoreCtrlStrip(m_gridRc, &m_ui, pt, NULL)) return;
	if (ScStaffHitPartBand(m_gridRc, &m_ui, pt, NULL, NULL, NULL, NULL)) return;
	if (m_ui.tool != SC_TOOL_PENCIL && m_ui.tool != SC_TOOL_TEMPO) return;
	int hitTr = -1;
	int hit = ScStaffHitNote(m_gridRc, &m_ui, m_doc.ev, m_doc.evCount, 0, pt, &hitTr);
	/* Same recovery as PlaceOrEditAt — HitNote==-1 on gauge edge still allows staff hover. */
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
	if (m_ui.tool == SC_TOOL_PENCIL) {
		m_ui.placeRest = m_placeRest;
		const int written = ScStaffYToMidiNoteTrack(&m_ui, hitTr, staffTop, pt.y, m_ui.hoverTick, m_doc.ev, m_doc.evCount) + m_accidental;
		const int oct = ScStaffOttavaOctaves(m_doc.ev, m_doc.evCount, hitTr, m_ui.hoverTick);
		m_ui.hoverNote = ScStaffWrittenToSounding(written, oct);
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
	int snapTr = -1;
	if (ScStaffSnapMarkerToToneExcClick(m_gridRc, m_trackRc, &m_ui, m_doc.ev, m_doc.evCount, pt, &snapTr)) {
		m_curCh = snapTr;
		if (m_ch.GetSafeHwnd()) m_ch.SetCurSel(m_curCh);
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
			m_status.SetWindowText(LL14(L"削除しました", L"Deleted", L"Supprimé", L"Eliminato", L"Eliminado", L"삭제됨", L"已删除", L"تم الحذف", L"Удалено", L"Gelöscht", L"Apagado", L"Verwijderd", L"Usunięto", L"Silindi"));
		} else if (m_ui.stripCount > 0 && m_stripRc.PtInRect(pt)) {
			int lane = 0, col = 0, val = 0;
			if (ScStaffHitStrip(m_stripRc, &m_ui, pt, &lane, &col, &val)) {
				m_ui.strip[lane][col] = (m_ui.stripKind[lane] == SC_STRIP_PITCH) ? 64 : 0;
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
		if (ScStaffIsStaffMarkKind(m_doc.ev[ctrl].kind, 0)) {
			ScStaffSelSetPrimary(&m_ui, ctrl);
			m_status.SetWindowText(LL14(L"マーク選択 — Delete/Backspace/消しゴムで |: :| Ped. 8va… を削除", L"Mark selected — Delete/Backspace / Eraser removes |: :| Ped. 8va…", L"Marque sélectionnée — Suppr/Gomme enlève |: :| Ped. 8va…", L"Segno selezionato — Canc/Gomma rimuove |: :| Ped. 8va…", L"Marca seleccionada — Supr/Borrar quita |: :| Ped. 8va…", L"마크 선택 — Delete/Backspace/지우개로 |: :| Ped. 8va… 삭제", L"已选标记 — Delete/Backspace/橡皮删除 |: :| Ped. 8va…", L"علامة محددة — Delete/Backspace/الممحاة تزيل |: :| Ped. 8va…", L"Знак выбран — Delete/Backspace/ластик удаляет |: :| Ped. 8va…", L"Zeichen gewählt — Entf/Radierer entfernt |: :| Ped. 8va…", L"Marca selecionada — Del/borracha remove |: :| Ped. 8va…", L"Markering geselecteerd — Del/gum verwijdert |: :| Ped. 8va…", L"Zaznaczono znak — Del/gumka usuwa |: :| Ped. 8va…", L"İşaret seçili — Del/silgi |: :| Ped. 8va… siler"));
			InvalidateRect(m_bodyRc, FALSE);
			return;
		}
		if (m_doc.ev[ctrl].kind == SC_EV_PROG || m_doc.ev[ctrl].kind == SC_EV_BANK) {
			/* Score Prog/Bank chip → tone/VST assign at this chip's tick (multi VST2). */
			m_ui.markerTick = m_doc.ev[ctrl].tick;
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
		m_status.SetWindowText(LL14(L"マーク選択 — Delete/Backspace で削除", L"Mark selected — Delete/Backspace removes", L"Marque sélectionnée — Suppr/Retour arrière", L"Segno selezionato — Canc/Backspace", L"Marca seleccionada — Supr/Retroceso", L"마크 선택 — Delete/Backspace로 삭제", L"已选标记 — Delete/Backspace 删除", L"علامة محددة — Delete/Backspace للحذف", L"Знак выбран — Delete/Backspace", L"Zeichen gewählt — Entf/Rücktaste", L"Marca selecionada — Del/Backspace", L"Markering geselecteerd — Del/Backspace", L"Zaznaczono znak — Del/Backspace", L"İşaret seçili — Del/Backspace"));
		InvalidateRect(m_bodyRc, FALSE);
		return;
	}
	if (ScStaffPtInScoreCtrlStrip(m_gridRc, &m_ui, pt, &ctrlTr))
		return;
	if (m_ui.tool == SC_TOOL_SELECT) {
		if (hit >= 0) {
			const int ch = (int)m_doc.ev[hit].ch;
			m_ui.markerSolidTrack = ch;
			m_curCh = ch;
			if (m_ch.GetSafeHwnd()) m_ch.SetCurSel(m_curCh);
			if (ScStaffSelHas(&m_ui, hit) && !(GetKeyState(VK_SHIFT) & 0x8000)) {
				/* keep existing selection */
			} else if (GetKeyState(VK_SHIFT) & 0x8000) {
				ScStaffSelAddTieChain(m_doc.ev, m_doc.evCount, &m_ui, hit, 0);
			} else {
				ScStaffSelClear(&m_ui);
				ScStaffSelAddTieChain(m_doc.ev, m_doc.evCount, &m_ui, hit, 0);
			}
			m_ui.dragEv = hit;
			m_ui.dragMode = 2;
			m_dragLastX = pt.x;
			m_dragLastY = pt.y;
			m_histDragPushed = 0;
			SyncPropFromSel();
			RefreshStrip();
			m_status.SetWindowText(LL14(L"選択中 — Delete/Backspace で削除。Exp/Vol/Pitch はこの MIDI ch のみ。", L"Selected — Delete/Backspace removes. Exp/Vol/Pitch apply to this MIDI ch only.", L"Sélection — Suppr. Exp/Vol/Pitch = ce canal MIDI seulement.", L"Selezione — Canc. Exp/Vol/Pitch solo su questo ch MIDI.", L"Selección — Supr. Exp/Vol/Pitch solo en este ch MIDI.", L"선택됨 — Delete/Backspace 삭제. Exp/Vol/Pitch는 이 MIDI ch만.", L"已选 — Delete/Backspace 删除。Exp/Vol/Pitch 仅本 MIDI 通道。", L"محدد — Delete/Backspace. Exp/Vol/Pitch لهذه قناة MIDI فقط.", L"Выбрано — Delete/Backspace. Exp/Vol/Pitch только для этого MIDI ch.", L"Auswahl — Entf. Exp/Vol/Pitch nur für diesen MIDI-Kanal.", L"Selecionado — Del. Exp/Vol/Pitch só neste ch MIDI.", L"Geselecteerd — Del. Exp/Vol/Pitch alleen dit MIDI-kanaal.", L"Zaznaczono — Del. Exp/Vol/Pitch tylko ten kanał MIDI.", L"Seçili — Del. Exp/Vol/Pitch yalnızca bu MIDI ch."));
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
			uint32_t tick = ScStaffXToTick(pt.x, m_ui.scrollX, ScStaffGridLeftPx(m_gridRc.left, &m_ui, m_doc.ev, m_doc.evCount), m_ui.pxBeat, quant, &m_ui, m_doc.ev, m_doc.evCount);
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
				m_ui.scrollX, ScStaffGridLeftPx(m_gridRc.left, &m_ui, m_doc.ev, m_doc.evCount), pxBeat, &m_ui, m_doc.ev, m_doc.evCount);
			m_ui.dragMode = (pt.x >= x1 - 8) ? 1 : 2;
		}
		m_dragLastX = pt.x;
		m_dragLastY = pt.y;
		m_histDragPushed = 0;
		SyncPropFromSel();
		m_status.SetWindowText(LL14(L"音符選択 — Delete/Backspace で削除。Exp/Pitch はチャンネル単位。", L"Note selected — Delete/Backspace to remove. Exp/Pitch are per-channel.", L"Note sélectionnée — Suppr. Exp/Pitch par canal.", L"Nota selezionata — Canc. Exp/Pitch per canale.", L"Nota seleccionada — Supr. Exp/Pitch por canal.", L"음표 선택 — Delete/Backspace 삭제. Exp/Pitch는 채널별.", L"已选音符 — Delete/Backspace 删除。Exp/Pitch 按通道。", L"نغمة محددة — Delete/Backspace. Exp/Pitch لكل قناة.", L"Нота выбрана — Delete/Backspace. Exp/Pitch по каналам.", L"Note gewählt — Entf. Exp/Pitch pro Kanal.", L"Nota selecionada — Del. Exp/Pitch por canal.", L"Noot geselecteerd — Del. Exp/Pitch per kanaal.", L"Zaznaczono nutę — Del. Exp/Pitch per kanał.", L"Nota seçili — Del. Exp/Pitch kanal bazlı."));
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
			const int noteTop = ScStaffRowNoteAreaTop(&m_ui, tr, rowTop);
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
		m_ui.markerSolidTrack = hitTr;
		const int quant = ScStaffPlaceQuant(&m_ui);
		uint32_t tick = ScStaffXToTick(pt.x, m_ui.scrollX, ScStaffGridLeftPx(m_gridRc.left, &m_ui, m_doc.ev, m_doc.evCount), m_ui.pxBeat, quant, &m_ui, m_doc.ev, m_doc.evCount);
		int staffTop = ScStaffVisibleLaneStaffTop(m_gridRc, &m_ui, hitTr);
		if (staffTop < 0) return;
		int written = ScStaffYToMidiNoteTrack(&m_ui, hitTr, staffTop, pt.y, tick, m_doc.ev, m_doc.evCount) + m_accidental;
		const int oct = ScStaffOttavaOctaves(m_doc.ev, m_doc.evCount, hitTr, tick);
		int note = ScStaffWrittenToSounding(written, oct);
		if (note < 0) note = 0;
		if (note > 127) note = 127;
		m_ui.dragMode = 0;
		HistPush();
		if (m_placeRest) {
			ScMidiAddRest(&m_doc, tick, hitTr, m_ui.placeDur);
			if (m_doc.evCount > 0)
				m_ui.selEv = m_doc.evCount - 1;
		} else if (m_ui.patternMode) {
			int vel = 100;
			CString vs; m_edVel.GetWindowText(vs);
			if (!vs.IsEmpty()) vel = _wtoi(vs);
			if (vel < 1) vel = 1;
			if (vel > 127) vel = 127;
			ScPatternPlace(m_doc.ev, &m_doc.evCount, SC_EV_MAX, hitTr,
				tick, note, vel, 100, m_ui.patternId, 1,
				m_ui.meterNumer, m_ui.meterDenom, SC_PPQN);
			m_ui.markerTick = tick + (uint32_t)ScPatternSpanTicks(m_ui.patternId, 1,
				m_ui.meterNumer, m_ui.meterDenom, SC_PPQN);
			m_ui.selEv = m_doc.evCount > 0 ? m_doc.evCount - 1 : -1;
			m_status.SetWindowText(ScPatternName(m_ui.patternId));
		} else if (m_ui.chordMode) {
			int vel = 100;
			CString vs; m_edVel.GetWindowText(vs);
			if (!vs.IsEmpty()) vel = _wtoi(vs);
			if (vel < 1) vel = 1;
			if (vel > 127) vel = 127;
			const int n0 = m_doc.evCount;
			ScChordPlaceAt(m_doc.ev, &m_doc.evCount, SC_EV_MAX, hitTr,
				tick, note, m_ui.placeDur, vel, 100, m_ui.chordType, m_ui.chordVoices,
				SC_EV_NOTE, 0);
			if (m_accidental > 0 && m_doc.evCount > n0)
				m_doc.ev[n0].flags = (uint8_t)((m_doc.ev[n0].flags & ~SC_EF_ACC_MASK) | SC_EF_ACC_SHARP);
			else if (m_accidental < 0 && m_doc.evCount > n0)
				m_doc.ev[n0].flags = (uint8_t)((m_doc.ev[n0].flags & ~SC_EF_ACC_MASK) | SC_EF_ACC_FLAT);
			m_ui.selEv = m_doc.evCount > 0 ? m_doc.evCount - 1 : -1;
			m_ui.dragEv = -1;
			SyncPropFromSel();
			{
				int pitches[8];
				int nv = ScChordBuildPitches(note, m_ui.chordType, m_ui.chordVoices, pitches, 8);
				int durMs = (int)(ScStaffSecFromTick((uint32_t)m_ui.placeDur, m_doc.tempoT) * 1000.0);
				if (durMs < 80) durMs = 80;
				if (durMs > 1200) durMs = 1200;
				for (int i = 0; i < nv; i++)
					VstLiveAuditionNote(hitTr + 1, pitches[i], vel, durMs);
			}
			m_status.SetWindowText(ScChordTypeName(m_ui.chordType));
		} else {
			int vel = 100;
			CString vs; m_edVel.GetWindowText(vs);
			if (!vs.IsEmpty()) vel = _wtoi(vs);
			if (vel < 1) vel = 1;
			if (vel > 127) vel = 127;
			if (ScMidiAddNote(&m_doc, tick, hitTr, note, m_ui.placeDur, vel)) {
				m_doc.ev[m_doc.evCount - 1].c = 100;
				if (m_accidental > 0) m_doc.ev[m_doc.evCount - 1].flags |= SC_EF_ACC_SHARP;
				else if (m_accidental < 0) m_doc.ev[m_doc.evCount - 1].flags |= SC_EF_ACC_FLAT;
				m_ui.selEv = m_doc.evCount - 1;
				m_ui.dragEv = -1; /* don't drag-steal pitch on same click */
				SyncPropFromSel();
				/* DAW-like: sound on place (live VST audition). */
				{
					int durMs = (int)(ScStaffSecFromTick((uint32_t)m_ui.placeDur, m_doc.tempoT) * 1000.0);
					if (durMs < 80) durMs = 80;
					if (durMs > 1200) durMs = 1200;
					VstLiveAuditionNote(hitTr + 1, note, vel, durMs);
				}
				CString st;
				if (oct != 0)
					st.Format(L"N=%d (O%d) %s written=%d tick=%u",
						note, note / 12, ScStaffOttavaLabel(oct), written, (unsigned)tick);
				else
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
		if (e.tick != 0) continue;
		if (e.kind == SC_EV_PROG) {
			/* Score PROG at tick 0 is canonical bind default for this channel. */
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
void CSasamiMidiScoreDlg::ApplyBindProgToScore(int ch0, uint32_t atTick)
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

	uint32_t tick = atTick;
	if (!ScMidiPartAllowMidScoreTone(&m_doc.bind, ch0))
		tick = 0;

	if (!ScMidiApplyProgBankAt(&m_doc, ch0, tick, prog, msb, lsb))
		return;

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
	if (tick == 0)
		st.Format(L"MIDI %d  Prog %d (PC#%d)  Bank %d/%d @ start",
			ch0 + 1, prog, prog + 1, msb, lsb);
	else
		st.Format(L"MIDI %d  Prog %d  Bank %d/%d @ tick %u (VST2 multi)",
			ch0 + 1, prog, msb, lsb, (unsigned)tick);
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
	/* Note + layout palettes always visible while score is open. */
	{
		CRect wr; GetWindowRect(&wr);
		CSasamiNotePaletteDlg::OpenNear(this, CPoint(wr.left + 40, wr.top + 100));
	}
	{
		CRect wr; GetWindowRect(&wr);
		CSasamiLayoutPaletteDlg::OpenNear(this, CPoint(wr.left + 420, wr.top + 100));
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
		ApplyBindProgToScore(part1to32 - 1, m_ui.markerTick);
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
	m_status.SetWindowText(LL14(L"VSTエディタを開いています…", L"Opening VST editor…", L"Ouverture de l’éditeur VST…", L"Apertura editor VST…", L"Abriendo editor VST…", L"VST 편집기 여는 중…", L"正在打开 VST 编辑器…", L"جارٍ فتح محرر VST…", L"Открытие редактора VST…", L"VST-Editor wird geöffnet…", L"Abrindo editor VST…", L"VST-editor openen…", L"Otwieranie edytora VST…", L"VST düzenleyici açılıyor…"));
	VstLiveEditorOpenAsync(part);
	m_status.SetWindowText(LL14(L"VSTエディタ要求 — Home/MediaBay で音色を選び閉じると @VSTSTATEB64", L"VST editor requested — pick a tone in Home/MediaBay; closing writes @VSTSTATEB64", L"Éditeur VST demandé — choisir un timbre dans Home/MediaBay ; fermer écrit @VSTSTATEB64", L"Editor VST richiesto — scegli un suono in Home/MediaBay; chiudendo scrive @VSTSTATEB64", L"Editor VST solicitado — elige un tono en Home/MediaBay; al cerrar escribe @VSTSTATEB64", L"VST 편집기 요청 — Home/MediaBay에서 음색 선택 후 닫으면 @VSTSTATEB64", L"已请求 VST 编辑器 — 在 Home/MediaBay 选音色并关闭后写入 @VSTSTATEB64", L"طُلب محرر VST — اختر صوتًا في Home/MediaBay؛ الإغلاق يكتب @VSTSTATEB64", L"Запрошен редактор VST — выберите тембр в Home/MediaBay; закрытие пишет @VSTSTATEB64", L"VST-Editor angefordert — Klang in Home/MediaBay wählen; Schließen schreibt @VSTSTATEB64", L"Editor VST solicitado — escolha um tom em Home/MediaBay; fechar grava @VSTSTATEB64", L"VST-editor gevraagd — kies een klank in Home/MediaBay; sluiten schrijft @VSTSTATEB64", L"Poproszono edytor VST — wybierz brzmienie w Home/MediaBay; zamknięcie zapisuje @VSTSTATEB64", L"VST düzenleyici istendi — Home/MediaBay’de ton seç; kapatınca @VSTSTATEB64 yazılır"));
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
	uint32_t tick = ScMidiPartAllowMidScoreTone(&m_doc.bind, m_curCh) ? m_ui.markerTick : 0;
	if (ScMidiApplyProgBankAt(&m_doc, m_curCh, tick, prog, msb, lsb)) {
		RefreshProgLabels();
		InvalidateRect(m_trackRc, FALSE);
		if (m_bodyRc.Width() > 0)
			InvalidateRect(m_bodyRc, FALSE);
		CString st;
		if (tick == 0)
			st.Format(L"MIDI %d  Prog %d (PC#%d)  Bank %d/%d @ start",
				m_curCh + 1, prog, prog + 1, msb, lsb);
		else
			st.Format(L"MIDI %d  Prog %d  Bank %d/%d @ tick %u",
				m_curCh + 1, prog, msb, lsb, (unsigned)tick);
		m_status.SetWindowText(st);
	}
}

void CSasamiMidiScoreDlg::EditProgForPart(int ch0)
{
	if (ch0 < 0 || ch0 >= 32) return;
	m_curCh = ch0;
	if (m_ch.GetSafeHwnd()) m_ch.SetCurSel(m_curCh);
	SyncProgPropFromCh(ch0);
	m_status.SetWindowText(LL14(L"Prog編集: Prog / MSB / LSB を設定して適用。Tone行でVSTを開きます。", L"Prog edit: set Prog / MSB / LSB then Apply. Tone row opens VST.", L"Édit. Prog : réglez Prog/MSB/LSB puis Appliquer. Ligne Tone ouvre le VST.", L"Modifica Prog: imposta Prog/MSB/LSB poi Applica. Riga Tone apre il VST.", L"Editar Prog: fija Prog/MSB/LSB y Aplicar. La fila Tone abre el VST.", L"Prog 편집: Prog/MSB/LSB 설정 후 적용. Tone 행에서 VST 열기.", L"Prog 编辑：设置 Prog/MSB/LSB 后应用。Tone 行打开 VST。", L"تحرير Prog: عيّن Prog/MSB/LSB ثم طبّق. صف Tone يفتح VST.", L"Правка Prog: задайте Prog/MSB/LSB и Применить. Строка Tone открывает VST.", L"Prog-Bearbeitung: Prog/MSB/LSB setzen, dann Anwenden. Tone-Zeile öffnet VST.", L"Editar Prog: defina Prog/MSB/LSB e Aplicar. Linha Tone abre o VST.", L"Prog bewerken: stel Prog/MSB/LSB in en Toepassen. Tone-rij opent VST.", L"Edycja Prog: ustaw Prog/MSB/LSB i Zastosuj. Wiersz Tone otwiera VST.", L"Prog düzenle: Prog/MSB/LSB ayarla, Uygula. Tone satırı VST açar."));
}

void CSasamiMidiScoreDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	if (capH > 0 && point.y >= 0 && point.y < capH) {
		CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
		return;
	}
	/* Embedded piano-roll split: same tools as floating roll */
	if (m_ui.showRollSplit && m_rollRc.PtInRect(point)) {
		if (m_ui.tool == SC_TOOL_PENCIL) {
			uint32_t tick = ScPianoRollXToTick(&m_rollView, m_rollRc, &m_ui, point.x);
			tick = (tick / (uint32_t)ScStaffPlaceQuant(&m_ui)) * (uint32_t)ScStaffPlaceQuant(&m_ui);
			int note = ScPianoRollYToNote(&m_rollView, m_rollRc, point.y);
			HistPush();
			ScMidiAddNote(&m_doc, tick, m_curCh, note, m_ui.placeDur > 0 ? m_ui.placeDur : SC_PPQN / 4, 100);
			m_ui.markerTick = tick;
			PushDocToText();
			InvalidateRect(m_rollRc, FALSE);
			InvalidateRect(m_bodyRc, FALSE);
			return;
		}
		if (m_ui.tool == SC_TOOL_ERASER) {
			int hit = ScPianoRollHitNote(&m_rollView, m_rollRc, m_doc.ev, m_doc.evCount, &m_ui, m_curCh, point);
			if (hit < 0) hit = ScPianoRollHitResize(&m_rollView, m_rollRc, m_doc.ev, m_doc.evCount, &m_ui, m_curCh, point);
			if (hit >= 0) {
				HistPush();
				ScStaffSelClear(&m_ui);
				ScStaffSelAdd(&m_ui, hit);
				ScStaffSelDelete(m_doc.ev, &m_doc.evCount, &m_ui);
				PushDocToText();
				InvalidateRect(m_rollRc, FALSE);
				InvalidateRect(m_bodyRc, FALSE);
			}
			return;
		}
		int hit = ScPianoRollHitNote(&m_rollView, m_rollRc, m_doc.ev, m_doc.evCount, &m_ui, m_curCh, point);
		if (hit >= 0) {
			if (!(nFlags & MK_CONTROL)) ScStaffSelClear(&m_ui);
			ScStaffSelAdd(&m_ui, hit);
			m_ui.markerTick = m_doc.ev[hit].tick;
		} else {
			m_ui.markerTick = ScPianoRollXToTick(&m_rollView, m_rollRc, &m_ui, point.x);
		}
		InvalidateRect(m_rollRc, FALSE);
		InvalidateRect(m_bodyRc, FALSE);
		RefreshBoundRoll();
		return;
	}
	CRect client; GetClientRect(&client);
	/* Thumbs are painted in body-local space (see OnPaint) — hit-test the same way. */
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
	int lane = 0, col = 0, val = 0;
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
	if (ScStaffHitStrip(m_stripRc, &m_ui, point, &lane, &col, &val)) {
		m_ui.stripLineAnchorCol = col;
		m_ui.stripLineAnchorVal = val;
		m_ui.stripLineLane = lane;
		m_ui.strip[lane][col] = (uint8_t)val;
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
			m_ui.rulerDragOn = 1;
			m_ui.rulerT0 = m_ui.rulerT1 = rulerTick;
			m_ui.markerTick = rulerTick;
			SetCapture();
		}
		m_ui.transportMode = 0;
		InvalidateRect(m_bodyRc, FALSE);
		return;
	}
	int snapTr = -1;
	if (ScStaffSnapMarkerToToneExcClick(m_gridRc, m_trackRc, &m_ui, m_doc.ev, m_doc.evCount, point, &snapTr)) {
		m_curCh = snapTr;
		if (m_ch.GetSafeHwnd()) m_ch.SetCurSel(m_curCh);
		m_ui.visible[snapTr] = 1;
		savedata.sasamiMidiPartMask = ScStaffPackPartMask(&m_ui);
		InvalidateRect(m_bodyRc, FALSE);
	}
	int zone = SC_GAUGE_NONE;
	int tr = ScStaffHitGauge(m_trackRc, &m_ui, point, &zone);
	if (tr < 0) tr = ScStaffHitTrack(m_trackRc, &m_ui, point);
	if (tr >= 0) {
		int enTr = -1;
		if (ScStaffHitPartEnable(m_trackRc, &m_ui, point, &enTr) >= 0) {
			m_ui.visible[enTr] = m_ui.visible[enTr] ? 0 : 1;
			savedata.sasamiMidiPartMask = ScStaffPackPartMask(&m_ui);
			UpdateScrollBars();
			InvalidateRect(m_bodyRc, FALSE);
			return;
		}
		if (zone == SC_GAUGE_CLEF) {
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
	PlaceOrEditAt(point);
}

void CSasamiMidiScoreDlg::OnMouseMove(UINT nFlags, CPoint point)
{
	if ((nFlags & MK_LBUTTON) && m_ui.rulerDragOn) {
		uint32_t rt = 0;
		if (ScStaffHitRulerTick(m_gridRc, &m_ui, point, &rt, m_doc.ev, m_doc.evCount)) {
			m_ui.rulerT1 = rt;
			ScStaffSelectTickRange(&m_ui, m_doc.ev, m_doc.evCount, 0, m_ui.rulerT0, m_ui.rulerT1, -1);
			InvalidateRect(m_bodyRc, FALSE);
		}
		return;
	}
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
	CPoint bp(point.x - m_bodyRc.left, point.y - m_bodyRc.top);
	CRect bodyRel(0, 0, m_bodyRc.Width(), m_bodyRc.Height());
	CRect viewRel(0, 0, m_gridRc.Width(), m_trackRc.bottom - m_bodyRc.top);
	int sbDummy = 0;
	const int onScroll = ScStaffHitScroll(bodyRel, bodyRel, viewRel, &m_ui,
		max(1, m_gridRc.Width()), max(1, m_gridRc.Height()), bp, &sbDummy, m_doc.ev, m_doc.evCount);
	const int onRuler = m_gridRc.PtInRect(point) && point.y >= m_gridRc.top + ScStaffGlobalTempoBandH(&m_ui)
		&& point.y < ScStaffGridBodyTop(m_gridRc.top, &m_ui);
	int gaugeZone = SC_GAUGE_NONE;
	const int onGauge = (ScStaffHitGauge(m_trackRc, &m_ui, point, &gaugeZone) >= 0 &&
		gaugeZone != SC_GAUGE_NONE);
	const int onCtrlStrip = ScStaffPtInScoreCtrlStrip(m_gridRc, &m_ui, point, NULL);
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
	} else if (onRuler) {
		::SetCursor(::LoadCursor(NULL, IDC_ARROW));
		m_ui.hoverValid = 0;
	} else if (m_gridRc.PtInRect(point) && m_blankCur &&
		(m_ui.tool == SC_TOOL_PENCIL || m_ui.tool == SC_TOOL_TEMPO) && !onScroll &&
		!onBand)
		::SetCursor(m_blankCur);
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
	} else if (m_ui.dragMode == 2 && (m_ui.nSel > 0 || m_ui.selEv >= 0) &&
		(m_ui.tool == SC_TOOL_SELECT || m_ui.tool == SC_TOOL_PENCIL)) {
		const int pxBeat = m_ui.pxBeat > 0 ? m_ui.pxBeat : SC_PX_BEAT_DEFAULT;
		const int dTick = ((point.x - m_dragLastX) * SC_PPQN) / max(1, pxBeat);
		const int gap = ScStaffLineGap(&m_ui);
		const int dSemi = (m_dragLastY - point.y) / max(1, gap / 2);
		if (dTick || dSemi) {
			if (!m_histDragPushed) { HistPush(); m_histDragPushed = 1; }
			if (ScStaffSelMoveBy(m_doc.ev, &m_doc.evCount, &m_ui, dTick, dSemi, 0)) {
				m_dragLastX = point.x;
				m_dragLastY = point.y;
				SyncPropFromSel();
				InvalidateRect(m_gridRc, FALSE);
			}
		}
	} else if (m_ui.dragEv >= 0 && m_ui.dragEv < m_doc.evCount &&
		(m_ui.tool == SC_TOOL_SELECT || m_ui.tool == SC_TOOL_PENCIL)) {
		ScEvent& e = m_doc.ev[m_ui.dragEv];
		if (e.kind == SC_EV_NOTE && m_ui.dragMode == 1) {
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
	CCustomBlurDialogExBase::OnMouseMove(nFlags, point);
}

BOOL CSasamiMidiScoreDlg::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
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

void CSasamiMidiScoreDlg::OnLButtonUp(UINT nFlags, CPoint point)
{
	m_sbDrag = 0;
	if (m_ui.rulerDragOn) {
		ScStaffSelectTickRange(&m_ui, m_doc.ev, m_doc.evCount, 0, m_ui.rulerT0, m_ui.rulerT1, -1);
		m_ui.rulerDragOn = 0;
		InvalidateRect(m_bodyRc, FALSE);
	}
	if (m_ui.marqueeOn) {
		m_ui.marqueeX1 = point.x;
		m_ui.marqueeY1 = point.y;
		CRect r(min(m_ui.marqueeX0, m_ui.marqueeX1), min(m_ui.marqueeY0, m_ui.marqueeY1),
			max(m_ui.marqueeX0, m_ui.marqueeX1), max(m_ui.marqueeY0, m_ui.marqueeY1));
		ScStaffSelectInRect(m_gridRc, &m_ui, m_doc.ev, m_doc.evCount, 0, r, 1);
		m_ui.marqueeOn = 0;
		InvalidateRect(m_bodyRc, FALSE);
	}
	m_ui.eraseDrag = 0;
	if (m_ui.dragEv == -3000) {
		if (m_ui.bandEditTrack == -2) {
			ScStaffApplyGlobalTempoToDoc(&m_doc, &m_ui);
		} else if (m_ui.bandEditTrack >= 0) {
			ScStaffApplyPartBandToDoc(&m_doc, m_ui.bandEditTrack, m_ui.bandEditKind, m_ui.bandEditBuf, &m_ui);
		}
		m_ui.bandEditTrack = -1;
		m_ui.stripLineAnchorCol = -1;
		PushDocToText();
		InvalidateRect(m_gridRc, FALSE);
		m_ui.dragEv = -1;
	} else if (m_ui.dragEv <= -2000) {
		ScStaffApplyStripToDocMidi(&m_doc, m_curCh, &m_ui);
		m_ui.stripLineAnchorCol = -1;
		m_ui.dragEv = -1;
	} else {
		m_ui.dragEv = -1;
	}
	m_ui.dragMode = 0;
	if (m_histDragPushed) PushDocToText();
	m_histDragPushed = 0;
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
	int tempoCol = 0, tempoBpm = 0;
	if (ScStaffHitGlobalTempoBand(m_gridRc, &m_ui, point, &tempoCol, &tempoBpm, m_doc.ev, m_doc.evCount)) {
		int bpm = tempoBpm;
		if (CSasamiSimpleInputDlg::AskNumber(this, LL14(L"テンポ", L"Tempo", L"Tempo", L"Tempo", L"Tempo",
				L"템포", L"速度", L"إيقاع", L"Темп", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"Tempo"),
			LL14(L"BPM (40–300)", L"BPM (40–300)", L"BPM (40–300)", L"BPM (40–300)", L"BPM (40–300)",
				L"BPM (40–300)", L"BPM (40–300)", L"BPM (40–300)", L"BPM (40–300)", L"BPM (40–300)",
				L"BPM (40–300)", L"BPM (40–300)", L"BPM (40–300)", L"BPM (40–300)"),
			bpm, 40, 300, &bpm) == IDOK) {
			ScStaffEnsureGlobalTempoFromDoc(&m_ui, m_doc.ev, m_doc.evCount, ScStaffBpmFromTempoT(m_doc.tempoT));
			ScStaffBandFillHoldSegment(m_ui.globalTempoStrip, ScStaffStripColCount(&m_ui), tempoCol, bpm, 40, 255);
			ScStaffApplyGlobalTempoToDoc(&m_doc, &m_ui);
			PushDocToText();
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
			ScStaffApplyPartBandToDoc(&m_doc, bandTr, bandKind, m_ui.bandEditBuf, &m_ui);
			PushDocToText();
			InvalidateRect(m_gridRc, FALSE);
		}
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
			m_status.SetWindowText(LL14(L"Prog/Bank 数値編集。音色/VSTはシングルクリックまたは左Tone行。", L"Edit Prog/Bank numbers. Tone/VST: single-click or left Tone row.", L"Éditer Prog/Bank. Tone/VST : simple clic ou ligne Tone gauche.", L"Modifica Prog/Bank. Tone/VST: clic singolo o riga Tone a sinistra.", L"Editar números Prog/Bank. Tone/VST: un clic o fila Tone izquierda.", L"Prog/Bank 숫자 편집. 음색/VST: 한 번 클릭 또는 왼쪽 Tone 행.", L"编辑 Prog/Bank 数值。音色/VST：单击或左侧 Tone 行。", L"تحرير أرقام Prog/Bank. Tone/VST: نقرة واحدة أو صف Tone الأيسر.", L"Правка чисел Prog/Bank. Tone/VST: одиночный клик или строка Tone слева.", L"Prog/Bank-Zahlen bearbeiten. Tone/VST: Einfachklick oder linke Tone-Zeile.", L"Editar números Prog/Bank. Tone/VST: clique único ou linha Tone à esquerda.", L"Prog/Bank-cijfers bewerken. Tone/VST: enkele klik of linker Tone-rij.", L"Edycja liczb Prog/Bank. Tone/VST: pojedyncze kliknięcie lub lewy wiersz Tone.", L"Prog/Bank sayı düzenle. Tone/VST: tek tık veya sol Tone satırı."));
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
	/* Do not forward to base — parent GDI must not steal score scroll. */
}

void CSasamiMidiScoreDlg::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
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

void CSasamiMidiScoreDlg::OnCbnSelchangeCh()
{
	int s = m_ch.GetCurSel();
	if (s >= 0 && s < SC_MIDI_CH) {
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

void CSasamiMidiScoreDlg::OnBnClickedPencil()
{
	m_ui.tool = SC_TOOL_PENCIL;
	m_ui.helpTopic = SC_HELP_PENCIL;
	UpdateNoteCursor();
	UpdateHelpBar();
}

void CSasamiMidiScoreDlg::OnBnClickedErase()
{
	m_ui.tool = SC_TOOL_ERASER;
	m_ui.helpTopic = SC_HELP_ERASER;
	UpdateNoteCursor();
	UpdateHelpBar();
}

void CSasamiMidiScoreDlg::OnBnClickedSel()
{
	m_ui.tool = SC_TOOL_SELECT;
	m_ui.helpTopic = SC_HELP_SELECT;
	UpdateNoteCursor();
	UpdateHelpBar();
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
			m_status.SetWindowText(LL14(L"テンポツール — 五線をクリック", L"Tempo tool — click staff", L"Outil tempo — clic portée", L"Strumento tempo — clic pentagramma", L"Herramienta tempo — clic pentagrama", L"템포 도구 — 보표 클릭", L"速度工具 — 点击五线", L"أداة الإيقاع — انقر المدرج", L"Инструмент темпа — клик по стану", L"Tempo-Werkzeug — Notensystem klicken", L"Ferramenta de tempo — clique na pauta", L"Tempo-gereedschap — klik notenbalk", L"Narzędzie tempa — kliknij pięciolinię", L"Tempo aracı — portede tıkla"));
			return 0;
		case SASAMI_PAL_CMD_GT:
			m_edGt.SetFocus();
			return 0;
		case SASAMI_PAL_CMD_PENCIL:
			ScStaffSelClear(&m_ui);
			m_ui.marqueeOn = 0;
			OnBnClickedPencil();
			return 0;
		case SASAMI_PAL_CMD_ERASE: OnBnClickedErase(); return 0;
		case SASAMI_PAL_CMD_SEL: OnBnClickedSel(); return 0;
		case SASAMI_PAL_CMD_MARK: OnBnClickedMark(); return 0;
		case SASAMI_PAL_CMD_LOOP_A: OnBnClickedLoopA(); return 0;
		case SASAMI_PAL_CMD_LOOP_B: OnBnClickedLoopB(); return 0;
		case SASAMI_PAL_CMD_LOOP_CLR: OnBnClickedLoopClr(); return 0;
		case SASAMI_PAL_CMD_MARK_REPLACE:
			m_ui.markStack = 0;
			savedata.sasamiMarkStack = 0;
			m_status.SetWindowText(LL14(L"マーク配置: 1重（同じ位置にもう一度＝消す）", L"Mark place: single (same spot again = remove)", L"Placement: simple (même endroit = supprimer)", L"Posizione: singola (stesso punto = rimuovi)", L"Colocación: simple (mismo sitio = quitar)", L"마크 배치: 1중 (같은 위치 다시 = 삭제)", L"标记放置: 单层（同位置再放=删除）", L"وضع العلامة: مفرد (نفس الموضع = حذف)", L"Размещение: одно (то же место = удалить)", L"Zeichen: einfach (gleiche Stelle = löschen)", L"Marcação: simples (mesmo local = remover)", L"Markering: enkel (zelfde plek = wissen)", L"Znacznik: pojedynczy (to samo miejsce = usuń)", L"İşaret: tek (aynı yer = sil)"));
			return 0;
		case SASAMI_PAL_CMD_MARK_STACK:
			m_ui.markStack = 1;
			savedata.sasamiMarkStack = 1;
			m_status.SetWindowText(LL14(L"マーク配置: ネスト（同じ位置に積み上げ）", L"Mark place: nest (stack at same spot)", L"Placement: nid (empiler au même endroit)", L"Posizione: nest (impila sullo stesso punto)", L"Colocación: anidar (apilar en el mismo sitio)", L"마크 배치: 중첩 (같은 위치에 쌓기)", L"标记放置: 嵌套（同位置叠加）", L"وضع العلامة: تداخل (تكديس بنفس الموضع)", L"Размещение: вложение (стек на том же месте)", L"Zeichen: Nest (stapeln an gleicher Stelle)", L"Marcação: aninhar (empilhar no mesmo local)", L"Markering: nest (stapelen opzelfde plek)", L"Znacznik: zagnieżdżenie (stos w tym samym miejscu)", L"İşaret: yuva (aynı yerde yığ)"));
			return 0;
		case SASAMI_PAL_CMD_TIE:
			m_ui.tool = SC_TOOL_TIE;
			UpdateNoteCursor();
			m_status.SetWindowText(LL14(L"タイツール", L"Tie tool", L"Outil liaison", L"Strumento legatura", L"Herramienta ligadura", L"타이 도구", L"连音工具", L"أداة الربط", L"Инструмент лиги", L"Bindebogen-Werkzeug", L"Ferramenta de ligadura", L"Boog-gereedschap", L"Narzędzie legato", L"Bağ aracı"));
			return 0;
		case SASAMI_PAL_CMD_LOOP_START:
		case SASAMI_PAL_CMD_LOOP_END:
		case SASAMI_PAL_CMD_PED_ON:
		case SASAMI_PAL_CMD_PED_OFF: {
			/* Palette is a separate window — hover stays stale. Insert at red bar.
			   Eraser tool + palette mark = delete that mark kind at the red bar. */
			const int ch = m_curCh;
			uint32_t atTick = m_ui.markerTick;
			if (m_ui.tool == SC_TOOL_ERASER) {
				uint8_t kind = SC_EV_FM_LOOP_START;
				if (cmdId == SASAMI_PAL_CMD_LOOP_END) kind = SC_EV_FM_LOOP_END;
				else if (cmdId == SASAMI_PAL_CMD_PED_ON) kind = SC_EV_PEDAL_ON;
				else if (cmdId == SASAMI_PAL_CMD_PED_OFF) kind = SC_EV_PEDAL_OFF;
				const int n = ScDeleteMarksAt(m_doc.ev, &m_doc.evCount, atTick, ch, kind);
				if (n > 0) {
					RefreshStrip();
					PushDocToText();
					InvalidateRect(m_bodyRc, FALSE);
					CString st;
					st.Format(L"Deleted %d mark(s) at red bar (MIDI %d)", n, ch + 1);
					m_status.SetWindowText(st);
				} else {
					m_status.SetWindowText(LL14(L"赤バー位置にその種類のマークはありません — マーカーを動かすか消しゴムで記号をクリック", L"No mark of that kind at red bar — move marker or click the glyph with Eraser", L"Pas de marque de ce type à la barre rouge — déplacez le marqueur ou cliquez le glyphe avec la Gomme", L"Nessun segno di quel tipo sulla barra rossa — sposta il marcatore o clicca il glifo con la Gomma", L"No hay marca de ese tipo en la barra roja — mueve el marcador o haz clic en el glifo con Borrar", L"빨간 바에 해당 종류 마크 없음 — 마커를 옮기거나 지우개로 기호 클릭", L"红条处无该类型标记 — 移动标记或用橡皮点击符号", L"لا علامة من هذا النوع عند الشريط الأحمر — حرّك العلامة أو انقر الرمز بالممحاة", L"Нет такого знака на красной метке — сдвиньте маркер или кликните глиф ластиком", L"Kein solches Zeichen an roter Markierung — Markierung verschieben oder Glyphe mit Radierer klicken", L"Sem marca desse tipo na barra vermelha — mova o marcador ou clique no glifo com a borracha", L"Geen markering van dat type op rode balk — verplaats markering of klik glyph met gum", L"Brak takiego znaku na czerwonym pasku — przesuń znacznik lub kliknij glif gumką", L"Kırmızı çubukta o tür işaret yok — işareti taşı veya silgiyle glife tıkla"));
				}
				return 0;
			}
			int ok = 0;
			uint8_t placedKind = SC_EV_FM_LOOP_START;
			if (cmdId == SASAMI_PAL_CMD_LOOP_START) {
				placedKind = SC_EV_FM_LOOP_START;
				/* Toggle-off: no need to ask count again. */
				if (!m_ui.markStack
					&& ScMarkKindExists(m_doc.ev, m_doc.evCount, atTick, (uint8_t)ch, SC_EV_FM_LOOP_START)) {
					ok = ScMidiAddLoopStart(&m_doc, atTick, ch, 2, 0);
				} else {
					int n = 2;
					if (CSasamiSimpleInputDlg::AskNumber(this, L"ループ開始 |:",
						L"繰り返し回数 (1–99)", 2, 1, 99, &n) != IDOK)
						return 0;
					ok = ScMidiAddLoopStart(&m_doc, atTick, ch, n, m_ui.markStack);
				}
			} else if (cmdId == SASAMI_PAL_CMD_LOOP_END) {
				placedKind = SC_EV_FM_LOOP_END;
				ok = ScMidiAddLoopEnd(&m_doc, atTick, ch, m_ui.markStack);
			} else if (cmdId == SASAMI_PAL_CMD_PED_ON) {
				placedKind = SC_EV_PEDAL_ON;
				ok = ScMidiAddPedalOn(&m_doc, atTick, ch, m_ui.markStack);
			} else {
				placedKind = SC_EV_PEDAL_OFF;
				ok = ScMidiAddPedalOff(&m_doc, atTick, ch, m_ui.markStack);
			}
			if (ok) {
				m_ui.visible[ch] = 1;
				RefreshStrip();
				PushDocToText();
				InvalidateRect(m_bodyRc, FALSE);
				CString st;
				const int still = ScMarkKindExists(m_doc.ev, m_doc.evCount, atTick, (uint8_t)ch, placedKind);
				if (!m_ui.markStack && !still)
					st.Format(L"MIDI %d mark removed @ %u", ch + 1, (unsigned)atTick);
				else
					st.Format(L"MIDI %d mark @ %u (red bar)", ch + 1, (unsigned)atTick);
				m_status.SetWindowText(st);
			}
			return 0;
		}
		case SASAMI_PAL_CMD_OTTAVA_8VA:
		case SASAMI_PAL_CMD_OTTAVA_8VB:
		case SASAMI_PAL_CMD_OTTAVA_16VA:
		case SASAMI_PAL_CMD_OTTAVA_16VB:
		case SASAMI_PAL_CMD_OTTAVA_32VA:
		case SASAMI_PAL_CMD_OTTAVA_32VB:
		case SASAMI_PAL_CMD_OTTAVA_LOCO: {
			const int ch = m_curCh;
			uint32_t atTick = m_ui.markerTick;
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
				const uint8_t kind = (oct == 0) ? SC_EV_OTTAVA_END : SC_EV_OTTAVA;
				int n = ScDeleteMarksAt(m_doc.ev, &m_doc.evCount, atTick, ch, kind);
				if (oct != 0)
					n += ScDeleteMarksAt(m_doc.ev, &m_doc.evCount, atTick, ch, SC_EV_OTTAVA_END);
				else
					n += ScDeleteMarksAt(m_doc.ev, &m_doc.evCount, atTick, ch, SC_EV_OTTAVA);
				if (n > 0) {
					RefreshStrip();
					PushDocToText();
					InvalidateRect(m_bodyRc, FALSE);
					m_status.SetWindowText(LL14(L"赤バー位置のオッターバ/loco を削除しました", L"Deleted ottava/loco at red bar", L"Ottava/loco supprimé à la barre rouge", L"Ottava/loco eliminato sulla barra rossa", L"Ottava/loco borrado en la barra roja", L"빨간 바의 옥타바/loco 삭제", L"已删除红条处的八度/loco", L"تم حذف ottava/loco عند الشريط الأحمر", L"Удалены ottava/loco на красной метке", L"Ottava/loco an roter Markierung gelöscht", L"Ottava/loco apagado na barra vermelha", L"Ottava/loco op rode balk verwijderd", L"Usunięto ottava/loco na czerwonym pasku", L"Kırmızı çubuktaki ottava/loco silindi"));
				} else {
					m_status.SetWindowText(LL14(L"赤バー位置にオッターバはありません — 消しゴム+譜表ラベルクリックも可", L"No ottava at red bar — Eraser+click staff label also works", L"Pas d’ottava à la barre rouge — Gomme+clic sur l’étiquette de portée aussi", L"Nessuna ottava sulla barra rossa — Gomma+clic sull’etichetta del rigo anche", L"No hay ottava en la barra roja — Borrar+clic en la etiqueta del pentagrama también", L"빨간 바에 옥타바 없음 — 지우개+보표 라벨 클릭도 가능", L"红条处无八度 — 橡皮+点击谱表标签也可", L"لا ottava عند الشريط الأحمر — الممحاة+النقر على تسمية المدرج أيضًا", L"Нет ottava на красной метке — ластик+клик по метке нотоносца тоже", L"Keine Ottava an roter Markierung — Radierer+Klick auf Notenzeilen-Label auch", L"Sem ottava na barra vermelha — borracha+clique no rótulo da pauta também", L"Geen ottava op rode balk — gum+klik op notenbalklabel ook", L"Brak ottavy na czerwonym pasku — gumka+klik etykiety pięciolinii też", L"Kırmızı çubukta ottava yok — silgi+porte etiketi tıklama da çalışır"));
				}
				return 0;
			}
			int ok = (oct == 0)
				? ScMidiAddOttavaEnd(&m_doc, atTick, ch, m_ui.markStack)
				: ScMidiAddOttava(&m_doc, atTick, ch, oct, m_ui.markStack);
			if (ok) {
				m_ui.visible[ch] = 1;
				ScStaffUpdateContentExtent(&m_ui, m_doc.ev, m_doc.evCount);
				RefreshStrip();
				PushDocToText();
				InvalidateRect(m_bodyRc, FALSE);
				CString st;
				const uint8_t kind = (oct == 0) ? SC_EV_OTTAVA_END : SC_EV_OTTAVA;
				const int still = ScMarkKindExists(m_doc.ev, m_doc.evCount, atTick, (uint8_t)ch, kind);
				if (!m_ui.markStack && !still)
					st.Format(L"MIDI %d %s removed @ %u", ch + 1, ScStaffOttavaLabel(oct), (unsigned)atTick);
				else
					st.Format(L"MIDI %d %s @ red bar tick %u", ch + 1, ScStaffOttavaLabel(oct), (unsigned)atTick);
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
	if (m_placeRest) m_ui.helpTopic = SC_HELP_REST;
	else if (m_accidental != 0) m_ui.helpTopic = SC_HELP_ACCIDENTAL;
	else if (m_ui.tuplet) m_ui.helpTopic = SC_HELP_TUPLET;
	else m_ui.helpTopic = SC_HELP_PAL_NOTE;
	UpdateNoteCursor();
	UpdateHelpBar();
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
	CString sn, sg, sv;
	m_edNote.GetWindowText(sn);
	m_edGt.GetWindowText(sg);
	m_edVel.GetWindowText(sv);
	int note = _wtoi(sn);
	int gate = _wtoi(sg);
	int vel = _wtoi(sv);
	if (gate < 1) gate = 1;
	if (gate > 100) gate = 100;
	if (vel < 1) vel = 1;
	if (vel > 127) vel = 127;
	if (m_ui.nSel > 1 || (m_ui.nSel == 1 && m_ui.selEv < 0)) {
		HistPush();
		ScBulkPropsSelected(m_doc.ev, m_doc.evCount, &m_ui, vel, gate, 1, 1);
		InvalidateRect(m_gridRc, FALSE);
		m_status.SetWindowText(LL14(L"選択にVel/Gateを一括適用", L"Bulk Vel/Gate applied to selection", L"Vel/Gate appliqués à la sélection", L"Vel/Gate applicati alla selezione", L"Vel/Gate aplicados a la selección",
			L"선택에 Vel/Gate 일괄 적용", L"已对选区批量应用Vel/Gate", L"تطبيق Vel/Gate على التحديد", L"Vel/Gate применены к выделению", L"Vel/Gate auf Auswahl angewendet", L"Vel/Gate aplicados à seleção", L"Vel/Gate toegepast op selectie", L"Vel/Gate zastosowane do zaznaczenia", L"Vel/Gate seçime uygulandı"));
		return;
	}
	if (m_ui.selEv < 0 || m_ui.selEv >= m_doc.evCount) return;
	ScEvent& e = m_doc.ev[m_ui.selEv];
	if (e.kind != SC_EV_NOTE) return;
	HistPush();
	if (note < 0) note = 0;
	if (note > 127) note = 127;
	e.a = (uint8_t)note;
	e.b = (uint8_t)vel;
	e.c = (uint8_t)gate;
	InvalidateRect(m_gridRc, FALSE);
}

void CSasamiMidiScoreDlg::OnBnClickedTempo()
{
	ScStaffEnsureGlobalTempoFromDoc(&m_ui, m_doc.ev, m_doc.evCount, ScStaffBpmFromTempoT(m_doc.tempoT));
	const int colMax = ScStaffStripColCount(&m_ui);
	int col = ScStaffStripTickToCol(&m_ui, m_ui.markerTick);
	if (col < 0) col = 0;
	if (col >= colMax) col = colMax - 1;
	int bpm = (int)m_ui.globalTempoStrip[col];
	if (bpm < 40) bpm = ScStaffBpmFromTempoT(m_doc.tempoT);
	if (CSasamiSimpleInputDlg::AskNumber(this, LL14(L"テンポ", L"Tempo", L"Tempo", L"Tempo", L"Tempo",
			L"템포", L"速度", L"إيقاع", L"Темп", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"Tempo"),
		LL14(L"赤マーカー位置から次の変化点まで BPM (40–300)", L"BPM at marker through next change (40–300)",
			L"BPM au marqueur", L"BPM al marcatore", L"BPM en marcador", L"BPM (40–300)", L"BPM (40–300)",
			L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM"),
		bpm, 40, 300, &bpm) != IDOK)
		return;
	ScStaffApplyTempoAtMarker(&m_doc, &m_ui, m_doc.ev, m_doc.evCount, m_ui.markerTick, bpm);
	PushDocToText();
	CString s;
	const int colEnd = ScStaffBandSegmentEndCol(m_ui.globalTempoStrip, colMax, col);
	s.Format(LL14(L"マーカー tick=%u: BPM %d を col %d–%d に設定", L"Marker tick=%u: BPM %d set cols %d–%d",
		L"Marqueur tick=%u: BPM %d cols %d–%d", L"Marcatore tick=%u: BPM %d col %d–%d", L"Marcador tick=%u: BPM %d col %d–%d",
		L"마커 tick=%u: BPM %d col %d–%d", L"标记 tick=%u: BPM %d col %d–%d", L"tick=%u BPM %d", L"tick=%u BPM %d col %d–%d",
		L"Marker tick=%u: BPM %d Spalten %d–%d", L"Marcador tick=%u: BPM %d", L"Marker tick=%u: BPM %d", L"Znacznik tick=%u: BPM %d col %d–%d", L"Isaret tick=%u: BPM %d col %d–%d"),
		(unsigned)m_ui.markerTick, bpm, col, colEnd);
	m_status.SetWindowText(s);
	InvalidateRect(m_gridRc, FALSE);
}

int CSasamiMidiScoreDlg::BuildToTemp(wchar_t* outPath, int outCch, uint32_t fromTick)
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
		m_status.SetWindowText(LL14(L"出力サイズが0です", L"Output size 0", L"Taille de sortie 0", L"Dimensione output 0", L"Tamaño de salida 0", L"출력 크기 0", L"输出大小为0", L"حجم الإخراج 0", L"Размер вывода 0", L"Ausgabegröße 0", L"Tamanho de saída 0", L"Uitvoergrootte 0", L"Rozmiar wyjścia 0", L"Çıkış boyutu 0"));
		return 0;
	}
	wchar_t dir[MAX_PATH];
	GetTempPathW(MAX_PATH, dir);
	_snwprintf_s(outPath, outCch, _TRUNCATE, L"%sogg_sasami_score.%s", dir, outMpsmv ? L"mpsmv" : L"mpw2");
	if (!SasamiWriteFileW(outPath, bin, sz)) {
		HeapFree(GetProcessHeap(), 0, bin);
		m_status.SetWindowText(LL14(L"書き込みに失敗しました", L"Write failed", L"Échec écriture", L"Scrittura non riuscita", L"Error al escribir", L"쓰기 실패", L"写入失败", L"فشل الكتابة", L"Ошибка записи", L"Schreiben fehlgeschlagen", L"Falha ao gravar", L"Schrijven mislukt", L"Zapis nieudany", L"Yazma başarısız"));
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
	int mpw3 = ScMidiDocNeedsMpsmv(&m_doc) ? 1 : 0;
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

void CSasamiMidiScoreDlg::RefreshPartEnabled()
{
	ScStaffRefreshPartEnabled(&m_ui, &m_doc, NULL, m_doc.ev, m_doc.evCount, savedata.sasamiMidiPartMask);
	if (m_btnShowAll.GetSafeHwnd())
		m_btnShowAll.SetWindowText(ScStaffIsExtendedChannelView(&m_ui) ? L"16ch" : L"32ch");
	UpdateScrollBars();
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
	RefreshPartEnabled();
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
	RefreshBoundRoll();
}

void CSasamiMidiScoreDlg::PullDocFromText()
{
	CSasamiTextDlg* t = CSasamiTextDlg::Instance();
	if (!t || !::IsWindow(t->GetSafeHwnd())) return;
	/* Unedited score mirror — do not expand→fold back over the live score. */
	if (!t->IsTextDirty()) return;
	CString text = t->GetMmlText();
	if (text.IsEmpty()) return;
	ScMidiDoc* tmp = (ScMidiDoc*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ScMidiDoc));
	if (!tmp) return;
	wchar_t err[128];
	int errLine = 0;
	if (!ScCompileMidiMml(text, tmp, &errLine, err, 128)) {
		HeapFree(GetProcessHeap(), 0, tmp);
		m_status.SetWindowText(LL14(L"テキストのコンパイルに失敗 — 譜面は変更しません", L"Text compile failed — score left unchanged", L"Échec compilation texte — partition inchangée", L"Compilazione testo non riuscita — partitura invariata", L"Falló la compilación de texto — partitura sin cambios", L"텍스트 컴파일 실패 — 악보 유지", L"文本编译失败 — 谱面未改", L"فشل تجميع النص — النتيجة دون تغيير", L"Ошибка компиляции текста — партитура без изменений", L"Textkompilierung fehlgeschlagen — Partitur unverändert", L"Falha na compilação do texto — partitura inalterada", L"Tekstcompilatie mislukt — partituur ongewijzigd", L"Kompilacja tekstu nieudana — partytura bez zmian", L"Metin derlemesi başarısız — skor değişmedi"));
		return;
	}
	LoadFromDoc(*tmp);
	/* LoadFromDoc copies events; free bind heaps owned by tmp. */
	ScMidiDocClear(tmp);
	HeapFree(GetProcessHeap(), 0, tmp);
	m_status.SetWindowText(LL14(L"テキストから譜面を同期しました", L"Score synced from text", L"Partition synchronisée depuis le texte", L"Partitura sincronizzata dal testo", L"Partitura sincronizada desde el texto", L"텍스트에서 악보 동기화", L"已从文本同步谱面", L"تمت مزامنة النتيجة من النص", L"Партитура синхронизирована из текста", L"Partitur aus Text synchronisiert", L"Partitura sincronizada do texto", L"Partituur gesynchroniseerd uit tekst", L"Partytura zsynchronizowana z tekstu", L"Skor metinden eşlendi"));
}

void CSasamiMidiScoreDlg::NewDocument()
{
	ScMidiDocClear(&m_doc);
	m_doc.tempoT = 13000;
	m_ui.selEv = -1;
	SyncMeterFromDoc();
	RefreshProgLabels();
	RefreshPartEnabled();
	ScStaffUpdateContentExtent(&m_ui, m_doc.ev, m_doc.evCount);
	UpdateScrollBars();
	ScSessionClearLast();
	if (CSasamiTextDlg* t = CSasamiTextDlg::Instance()) {
		if (::IsWindow(t->GetSafeHwnd()))
			t->SetMmlFromScore(L"; SASAMI MML / MML3\r\n@METER 4/4\r\n#1\r\nt120\r\nl4 o4\r\n");
	}
	m_status.SetWindowText(LL14(L"新規ドキュメント", L"New document", L"Nouveau document", L"Nuovo documento", L"Documento nuevo", L"새 문서", L"新建文档", L"مستند جديد", L"Новый документ", L"Neues Dokument", L"Novo documento", L"Nieuw document", L"Nowy dokument", L"Yeni belge"));
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
	savedata.sasamiMidiPartMask = ScStaffPackPartMask(&m_ui);
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
		m_status.SetWindowText(LL14(L"開けませんでした", L"Open failed", L"Échec ouverture", L"Apertura non riuscita", L"Error al abrir", L"열기 실패", L"打开失败", L"فشل الفتح", L"Не удалось открыть", L"Öffnen fehlgeschlagen", L"Falha ao abrir", L"Openen mislukt", L"Nie udało się otworzyć", L"Açılamadı"));
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
	/* Score-local preview from red bar: prefer live host (VST OK), wavout fallback. */
	VstLiveMonitorStop();
	SyncFxBindsToLive();
	for (int i = 0; i < m_ui.trackCount; i++)
		ScStaffApplyStripToDocMidi(&m_doc, i, &m_ui);
	wchar_t path[MAX_PATH];
	if (!BuildToTemp(path, MAX_PATH, m_ui.markerTick)) {
		if (VstLivePartIsLoaded(1) || VstLiveAnyRemotePart())
			VstLiveMonitorEnsure();
		return;
	}
	MmBindVstActiveSlot();
	VstApplyMpw3Binds(path, 0);
	m_status.SetWindowText(LL14(L"譜面プレビュー…", L"Score preview…", L"Aperçu partition…", L"Anteprima partitura…", L"Vista previa…",
		L"악보 미리듣기…", L"乐谱预览…", L"معاينة النوتة…", L"Превью партитуры…", L"Partitur-Vorschau…", L"Prévia da partitura…", L"Partituurvoorbeeld…", L"Podgląd partytury…", L"Partisyon önizleme…"));
	int ok = ScStaffPreviewViaHost(path, &m_ui, m_doc.tempoT);
	if (!ok)
		ok = ScStaffPreviewViaWavout(path, &m_ui, m_doc.tempoT);
	if (!ok) {
		m_ui.previewActive = 0;
		m_status.SetWindowText(LL14(L"プレビュー失敗（ホスト／wavout）", L"Preview failed (host / wavout)", L"Échec aperçu", L"Anteprima non riuscita", L"Falló la vista previa",
			L"미리듣기 실패", L"预览失败", L"فشل المعاينة", L"Превью не удалось", L"Vorschau fehlgeschlagen", L"Falha na prévia", L"Voorbeeld mislukt", L"Podgląd nieudany", L"Önizleme başarısız"));
		VstLiveMonitorEnsure();
		return;
	}
	VstLiveMonitorStop();
	PushDocToText();
	SetTimer(kScPreviewTimer, 33, NULL);
	CString st;
	st.Format(LL14(L"プレビュー tick %u — Spaceで停止", L"Preview tick %u — Space to stop", L"Aperçu tick %u — Espace = stop", L"Anteprima tick %u — Spazio = stop", L"Vista previa tick %u — Espacio = stop",
		L"미리듣기 tick %u — Space 정지", L"预览 tick %u — Space停止", L"معاينة tick %u — Space للإيقاف", L"Превью tick %u — Space = стоп", L"Vorschau tick %u — Leertaste = Stop", L"Prévia tick %u — Espaço = parar", L"Voorbeeld tick %u — Spatie = stop", L"Podgląd tick %u — Spacja = stop", L"Önizleme tick %u — Space = dur"),
		(unsigned)m_ui.markerTick);
	m_status.SetWindowText(st);
	InvalidateRect(m_bodyRc, FALSE);
}

void CSasamiMidiScoreDlg::OnBnClickedMark()
{
	m_ui.transportMode = 1;
	m_status.SetWindowText(LL14(L"ルーラークリック＝再生開始マーカー", L"Click ruler = play-from marker", L"Clic règle = marqueur de départ", L"Clic righello = marcatore di inizio", L"Clic regla = marcador de inicio", L"눈금자 클릭 = 재생 시작 마커", L"点击标尺＝播放起点标记", L"نقر المسطرة = علامة بدء التشغيل", L"Клик по линейке = маркер старта", L"Klick Lineal = Startmarkierung", L"Clique na régua = marcador de início", L"Klik liniaal = startmarkering", L"Klik linijki = znacznik startu", L"Cetvele tıkla = başlangıç işareti"));
}

void CSasamiMidiScoreDlg::OnBnClickedLoopA()
{
	m_ui.transportMode = 2;
	m_status.SetWindowText(LL14(L"ルーラークリックでループA", L"Click ruler for loop A", L"Clic règle pour boucle A", L"Clic righello per loop A", L"Clic regla para bucle A", L"눈금자 클릭으로 루프 A", L"点击标尺设置循环A", L"نقر المسطرة لحلقة A", L"Клик по линейке для цикла A", L"Klick Lineal für Schleife A", L"Clique na régua para loop A", L"Klik liniaal voor lus A", L"Klik linijki dla pętli A", L"Döngü A için cetvele tıkla"));
}

void CSasamiMidiScoreDlg::OnBnClickedLoopB()
{
	m_ui.transportMode = 3;
	m_status.SetWindowText(LL14(L"ルーラークリックでループB", L"Click ruler for loop B", L"Clic règle pour boucle B", L"Clic righello per loop B", L"Clic regla para bucle B", L"눈금자 클릭으로 루프 B", L"点击标尺设置循环B", L"نقر المسطرة لحلقة B", L"Клик по линейке для цикла B", L"Klick Lineal für Schleife B", L"Clique na régua para loop B", L"Klik liniaal voor lus B", L"Klik linijki dla pętli B", L"Döngü B için cetvele tıkla"));
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
	if (ScStaffIsExtendedChannelView(&m_ui))
		ScStaffSetChannelView16(&m_ui);
	else
		ScStaffSetChannelViewAll(&m_ui);
	savedata.sasamiMidiPartMask = ScStaffPackPartMask(&m_ui);
	m_btnShowAll.SetWindowText(ScStaffIsExtendedChannelView(&m_ui) ? L"16ch" : L"32ch");
	UpdateScrollBars();
	InvalidateRect(m_bodyRc, FALSE);
	m_status.SetWindowText(ScStaffIsExtendedChannelView(&m_ui)
		? LL14(L"全32 MIDIチャンネル表示", L"Showing all MIDI 1-32", L"Canaux MIDI 1-32", L"Canali MIDI 1-32", L"Canales MIDI 1-32",
			L"MIDI 1-32 표시", L"显示全部 MIDI 1-32", L"MIDI 1-32", L"MIDI 1-32", L"Alle MIDI 1-32", L"MIDI 1-32", L"MIDI 1-32", L"MIDI 1-32", L"MIDI 1-32")
		: LL14(L"MIDI 1-16 表示", L"Showing MIDI 1-16", L"MIDI 1-16", L"MIDI 1-16", L"MIDI 1-16",
			L"MIDI 1-16", L"MIDI 1-16", L"MIDI 1-16", L"MIDI 1-16", L"MIDI 1-16", L"MIDI 1-16", L"MIDI 1-16", L"MIDI 1-16", L"MIDI 1-16"));
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
			ApplyFollowScroll();
			ApplyMuteSoloToLive();
			UpdateScrollBars();
			InvalidateRect(m_bodyRc, FALSE);
			if (m_ui.showRollSplit) InvalidateRect(m_rollRc, FALSE);
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
	if (nIDEvent == kScMetroTimer) {
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
	m_status.SetWindowText(LL14(L"Exc/RPNイベントを挿入しました", L"Inserted Exc/RPN event", L"Événement Exc/RPN inséré", L"Evento Exc/RPN inserito", L"Evento Exc/RPN insertado", L"Exc/RPN 이벤트 삽입", L"已插入 Exc/RPN 事件", L"تم إدراج حدث Exc/RPN", L"Вставлено событие Exc/RPN", L"Exc/RPN-Ereignis eingefügt", L"Evento Exc/RPN inserido", L"Exc/RPN-event ingevoegd", L"Wstawiono zdarzenie Exc/RPN", L"Exc/RPN olayı eklendi"));
	return 0;
}

LRESULT CSasamiMidiScoreDlg::OnInsertFxChanged(WPARAM, LPARAM)
{
	SyncFxBindsToLive();
	PushDocToText();
	PersistSession();
	m_status.SetWindowText(LL14(L"インサートFXを更新しました", L"Insert FX updated", L"Insert FX mis à jour", L"Insert FX aggiornato", L"Insert FX actualizado", L"인서트 FX 업데이트", L"插入FX已更新", L"تم تحديث Insert FX", L"Insert FX обновлён", L"Insert-FX aktualisiert", L"Insert FX atualizado", L"Insert-FX bijgewerkt", L"Zaktualizowano Insert FX", L"Insert FX güncellendi"));
	return 0;
}

void CSasamiMidiScoreDlg::OnBnClickedHelp()
{
	CSasamiCmdHelpDlg::Show(this, CSasamiCmdHelpDlg::kTabScore1);
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
	if (m_ui.tool == SC_TOOL_PENCIL || m_ui.tool == SC_TOOL_TEMPO) {
		ScStaffEnterSelectTool(&m_ui);
		UpdateNoteCursor();
		InvalidateRect(m_bodyRc, FALSE);
		return;
	}
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
		menu.AddCommand(9030, LL14(L"符尾↑", L"Stem up", L"Hampe haut", L"Gambo su", L"Plica arriba", L"부호↑", L"符干上", L"ساق أعلى", L"Штиль ↑", L"Hals oben", L"Haste ↑", L"Stok omhoog", L"Ogon ↑", L"Gövde ↑"));
		menu.AddCommand(9031, LL14(L"符尾↓", L"Stem down", L"Hampe bas", L"Gambo giù", L"Plica abajo", L"부호↓", L"符干下", L"ساق أسفل", L"Штиль ↓", L"Hals unten", L"Haste ↓", L"Stok omlaag", L"Ogon ↓", L"Gövde ↓"));
		menu.AddCommand(9032, LL14(L"ビーム切断", L"Break beam", L"Couper ligature", L"Spezza travatura", L"Romper barra", L"빔 끊기", L"断开符杠", L"قطع العارضة", L"Разорвать вязь", L"Balken trennen", L"Quebrar barra", L"Balk breken", L"Przerwij belkę", L"Kirişi kes"));
		menu.AddCommand(9033, LL14(L"スラー開始", L"Slur start", L"Début liaison", L"Inizio legatura", L"Inicio ligadura", L"슬러 시작", L"连线开始", L"بداية الربط", L"Начало лиги", L"Bogen Anfang", L"Início ligadura", L"Boog start", L"Początek łuku", L"Bağ başlangıç"));
		menu.AddCommand(9034, LL14(L"スラー終了", L"Slur end", L"Fin liaison", L"Fine legatura", L"Fin ligadura", L"슬러 끝", L"连线结束", L"نهاية الربط", L"Конец лиги", L"Bogen Ende", L"Fim ligadura", L"Boog einde", L"Koniec łuku", L"Bağ sonu"));
		menu.AddCommand(9035, LL14(L"クレッシェンド", L"Crescendo", L"Crescendo", L"Crescendo", L"Crescendo", L"크레셴도", L"渐强", L"cresc", L"Крещендо", L"Crescendo", L"Crescendo", L"Crescendo", L"Crescendo", L"Crescendo"));
		menu.AddCommand(9036, LL14(L"デクレッシェンド", L"Diminuendo", L"Diminuendo", L"Diminuendo", L"Diminuendo", L"디미누엔도", L"渐弱", L"dim", L"Диминуэндо", L"Diminuendo", L"Diminuendo", L"Diminuendo", L"Diminuendo", L"Diminuendo"));
		menu.AddCommand(9037, LL14(L"調号…", L"Key signature…", L"Armature…", L"Armatura…", L"Armadura…", L"조표…", L"调号…", L"دليل…", L"Тональность…", L"Vorzeichen…", L"Armadura…", L"Toonsoort…", L"Tonacja…", L"Armatür…"));
		menu.AddCommand(9040, LL14(L"拍子・移調…", L"Meter / transpose…", L"Mesure / transposition…", L"Misura / trasposizione…", L"Compás / transposición…",
			L"박자·이조…", L"拍号·移调…", L"إيقاع / نقل…", L"Размер / трансп.…", L"Takt / Transponieren…", L"Compasso / transpor…", L"Maat / transp.…", L"Metrum / transp.…", L"Ölçü / transp.…"));
		menu.AddSeparator();
		menu.AddCheck(9050, LL14(L"メトロノーム", L"Metronome", L"Métronome", L"Metronomo", L"Metrónomo", L"메트로놈", L"节拍器", L"مترونوم", L"Метроном", L"Metronom", L"Metrônomo", L"Metronoom", L"Metronom", L"Metronom"), m_midiIn.metroOn);
		menu.AddCheck(9051, LL14(L"オーバーダブ", L"Overdub", L"Overdub", L"Overdub", L"Overdub", L"오버더빙", L"叠加录音", L"تسجيل فوق", L"Овердаб", L"Overdub", L"Overdub", L"Overdub", L"Overdub", L"Overdub"), m_midiIn.overdub);
		menu.AddCheck(9052, LL14(L"クオンタイズOFF", L"Quant OFF", L"Quant OFF", L"Quant OFF", L"Cuant OFF", L"양자화 OFF", L"量化关", L"كمية OFF", L"Квант OFF", L"Quant AUS", L"Quant OFF", L"Quant UIT", L"Kwant OFF", L"Quant KAPALI"), m_midiIn.quant == SC_QUANT_OFF);
		menu.AddCheck(9053, LL14(L"クオンタイズ弱", L"Quant Weak", L"Quant faible", L"Quant debole", L"Cuant suave", L"양자화 약", L"量化弱", L"كمية ضعيفة", L"Квант слаб.", L"Quant schwach", L"Quant fraco", L"Quant zwak", L"Kwant słaby", L"Quant zayıf"), m_midiIn.quant == SC_QUANT_WEAK);
		menu.AddCheck(9054, LL14(L"クオンタイズ強", L"Quant Strong", L"Quant fort", L"Quant forte", L"Cuant fuerte", L"양자화 강", L"量化强", L"كمية قوية", L"Квант сильн.", L"Quant stark", L"Quant forte", L"Quant sterk", L"Kwant silny", L"Quant güçlü"), m_midiIn.quant == SC_QUANT_STRONG);
		menu.AddCheck(9055, LL14(L"スイング", L"Quant Swing", L"Swing", L"Swing", L"Swing", L"스윙", L"摇摆", L"سوينغ", L"Свинг", L"Swing", L"Swing", L"Swing", L"Swing", L"Swing"), m_midiIn.quant == SC_QUANT_SWING);
		menu.AddCheck(9056, LL14(L"ベロシティ線形", L"Vel Linear", L"Vel linéaire", L"Vel lineare", L"Vel lineal", L"벨로시티 선형", L"力度线性", L"سرعة خطية", L"Ве. линейн.", L"Vel linear", L"Vel linear", L"Vel lineair", L"Vel liniowa", L"Vel doğrusal"), m_midiIn.velCurve == SC_VEL_LINEAR);
		menu.AddCheck(9057, LL14(L"ベロシティ軟", L"Vel Soft", L"Vel douce", L"Vel morbida", L"Vel suave", L"벨로시티 소프트", L"力度柔和", L"سرعة ناعمة", L"Vel мягк.", L"Vel weich", L"Vel suave", L"Vel zacht", L"Vel miękka", L"Vel yumuşak"), m_midiIn.velCurve == SC_VEL_SOFT);
		menu.AddCheck(9058, LL14(L"ベロシティ固定", L"Vel Fixed", L"Vel fixe", L"Vel fissa", L"Vel fija", L"벨로시티 고정", L"力度固定", L"سرعة ثابتة", L"Vel фикс.", L"Vel fest", L"Vel fixa", L"Vel vast", L"Vel stała", L"Vel sabit"), m_midiIn.velCurve == SC_VEL_FIXED);
		menu.AddCheck(9039, LL14(L"グリッド強調", L"Grid emphasis", L"Grille", L"Griglia", L"Cuadrícula", L"그리드 강조", L"网格强调", L"شبكة", L"Сетка", L"Raster", L"Grade", L"Raster", L"Siatka", L"Izgara"), m_ui.gridEmph);
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
			menu.AddCommand(9120, LL14(
			L"重なっているマークをすべて削除", L"Delete all overlapping marks", L"Supprimer toutes les marques superposées", L"Elimina tutti i segni sovrapposti", L"Borrar todas las marcas superpuestas",
			L"겹친 마크 모두 삭제", L"删除所有重叠标记", L"حذف كل العلامات المتداخلة", L"Удалить все совпадающие знаки", L"Alle überlappenden Zeichen löschen",
			L"Apagar todas as marcas sobrepostas", L"Alle overlappende markeringen wissen", L"Usuń wszystkie nachodzące znaki", L"Çakışan tüm işaretleri sil"));
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
		menu.AddCommand(9001, m_ui.mute[tr]
			? LL14(L"ミュート解除", L"Unmute", L"Réactiver", L"Riattiva", L"Activar", L"음소거 해제", L"取消静音", L"إلغاء كتم", L"Включить звук", L"Stummschaltung aus", L"Ativar som", L"Dempen uit", L"Włącz dźwięk", L"Sessizi aç")
			: LL14(L"ミュート", L"Mute", L"Muet", L"Muto", L"Silenciar", L"음소거", L"静音", L"كتم", L"Выкл. звук", L"Stumm", L"Mudo", L"Dempen", L"Wycisz", L"Sessiz"));
		menu.AddCommand(9002, m_ui.solo[tr]
			? LL14(L"ソロ解除", L"Unsolo", L"Désolo", L"Togli solo", L"Quitar solo", L"솔로 해제", L"取消独奏", L"إلغاء منفرد", L"Снять соло", L"Solo aus", L"Tirar solo", L"Solo uit", L"Wyłącz solo", L"Soloyu kapat")
			: LL14(L"ソロ", L"Solo", L"Solo", L"Solo", L"Solo", L"솔로", L"独奏", L"منفرد", L"Соло", L"Solo", L"Solo", L"Solo", L"Solo", L"Solo"));
		if (VstLivePartIsLoaded(tr + 1)) {
			menu.AddCommand(9003, LL14(
				L"VST詳細…", L"VST details…", L"Détails VST…", L"Dettagli VST…", L"Detalles VST…",
				L"VST 상세…", L"VST详情…", L"تفاصيل VST…", L"Сведения VST…", L"VST-Details…",
				L"Detalhes VST…", L"VST-details…", L"Szczegóły VST…", L"VST ayrıntıları…"));
		}
		menu.AddCommand(9004, m_ui.visible[tr]
			? LL14(L"パートを無効（譜面から外す）", L"Disable part (hide from score)", L"Désactiver la partie", L"Disattiva parte", L"Desactivar parte", L"Disable part", L"禁用声部", L"Disable part", L"Отключить парту", L"Part deaktivieren", L"Desativar parte", L"Part uitschakelen", L"Wyłącz part", L"Parti devre dışı")
			: LL14(L"パートを有効（譜面に表示）", L"Enable part (show on score)", L"Activer la partie", L"Attiva parte", L"Activar parte", L"Enable part", L"启用声部", L"Enable part", L"Включить парту", L"Part aktivieren", L"Ativar parte", L"Part inschakelen", L"Włącz part", L"Parti etkinleştir"));
		menu.AddCommand(9005,
			m_ui.clef[tr] == 0 ? L"譜表: ト音→ヘ音"
			: (m_ui.clef[tr] == 1 ? L"譜表: ヘ音→大譜表"
			: (m_ui.clef[tr] == 2 ? L"譜表: 大譜表→ドラム"
			: L"譜表: ドラム→ト音")));
		menu.AddCommand(9007, LL14(
			L"全トラックを大譜表に", L"All tracks → grand staff", L"Toutes les pistes → grande portée", L"Tutte le tracce → pentagramma grande", L"Todas las pistas → gran pentagrama",
			L"모든 트랙 → 대보표", L"全部轨道→大谱表", L"كل المسارات → مدرج كبير", L"Все дорожки → большой стан", L"Alle Spuren → Akkolade",
			L"Todas as faixas → pauta grande", L"Alle tracks → groot notenbalk", L"Wszystkie ścieżki → wielka pięciolinia", L"Tüm parçalar → büyük porte"));
		menu.AddCommand(9008, LL14(
			L"全トラックをト音に", L"All tracks → treble", L"Toutes les pistes → clé de sol", L"Tutte le tracce → chiave di violino", L"Todas las pistas → clave de sol",
			L"모든 트랙 → 높은음자리", L"全部轨道→高音谱号", L"كل المسارات → مفتاح صول", L"Все дорожки → скрипичный ключ", L"Alle Spuren → Violinschlüssel",
			L"Todas as faixas → clave de sol", L"Alle tracks → vioolsleutel", L"Wszystkie ścieżki → klucz wiolinowy", L"Tüm parçalar → sol anahtarı"));
		menu.AddCommand(9009, LL14(
			L"全トラックをドラム(キット)に", L"All tracks → drum/kit", L"Toutes les pistes → batterie", L"Tutte le tracce → batteria", L"Todas las pistas → batería",
			L"모든 트랙 → 드럼/키트", L"全部轨道→鼓组", L"كل المسارات → طبول", L"Все дорожки → ударные", L"Alle Spuren → Drum/Kit",
			L"Todas as faixas → bateria", L"Alle tracks → drum/kit", L"Wszystkie ścieżki → perkusja", L"Tüm parçalar → davul/kit"));
		menu.AddSeparator();
	}
	menu.AddCommand(9006, LL14(
		L"選択モード（鉛筆解除）", L"Select mode (exit pencil)", L"Mode selection", L"Modalita selezione", L"Modo seleccion",
		L"Select mode", L"选择模式", L"Select mode", L"Select mode", L"Auswahlmodus", L"Modo selecao", L"Selectiemodus", L"Tryb zaznaczania", L"Secim modu"));
	if (m_ui.selRangeValid || m_ui.nSel > 0 || m_ui.selEv >= 0) {
		menu.AddCommand(9060, LL14(
			L"範囲に空白を挿入（後ろずらし）\tCtrl+Shift+I", L"Insert blank in range (shift later)\tCtrl+Shift+I", L"Insérer vide (décaler)\tCtrl+Shift+I", L"Inserisci vuoto (sposta)\tCtrl+Shift+I", L"Insertar vacío (desplazar)\tCtrl+Shift+I",
			L"범위에 빈 구간 삽입 (뒤로 밀기)\tCtrl+Shift+I", L"在范围内插入空白（后移）\tCtrl+Shift+I", L"إدراج فراغ\tCtrl+Shift+I", L"Вставить пустоту\tCtrl+Shift+I", L"Leerraum einfügen (nach rechts)\tCtrl+Shift+I",
			L"Inserir vazio (deslocar)\tCtrl+Shift+I", L"Leeg invoegen (opschuiven)\tCtrl+Shift+I", L"Wstaw pustkę (przesuń)\tCtrl+Shift+I", L"Boş ekle (kaydır)\tCtrl+Shift+I"));
	}
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
	auto afterMark = [&](uint8_t kind) {
		m_ui.visible[tr] = 1;
		m_curCh = tr;
		if (m_ch.GetSafeHwnd()) m_ch.SetCurSel(m_curCh);
		RefreshStrip();
		PushDocToText();
		InvalidateRect(m_bodyRc, FALSE);
		CString st;
		const int still = ScMarkKindExists(m_doc.ev, m_doc.evCount, atTick, (uint8_t)tr, kind);
		if (!m_ui.markStack && !still)
			st.Format(L"MIDI %d mark removed @ tick %u", tr + 1, (unsigned)atTick);
		else
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
		m_status.SetWindowText(LL14(L"Exc/RPN: マーカー位置に SysEx・RPN・NRPN を挿入。", L"Exc/RPN: insert SysEx/RPN/NRPN at marker.", L"Exc/RPN : insérer SysEx/RPN/NRPN au marqueur.", L"Exc/RPN: inserisci SysEx/RPN/NRPN sul marcatore.", L"Exc/RPN: insertar SysEx/RPN/NRPN en el marcador.", L"Exc/RPN: 마커에 SysEx/RPN/NRPN 삽입.", L"Exc/RPN：在标记处插入 SysEx/RPN/NRPN。", L"Exc/RPN: إدراج SysEx/RPN/NRPN عند العلامة.", L"Exc/RPN: вставка SysEx/RPN/NRPN на маркере.", L"Exc/RPN: SysEx/RPN/NRPN an Markierung einfügen.", L"Exc/RPN: inserir SysEx/RPN/NRPN no marcador.", L"Exc/RPN: SysEx/RPN/NRPN bij markering invoegen.", L"Exc/RPN: wstaw SysEx/RPN/NRPN na znaczniku.", L"Exc/RPN: işaretçide SysEx/RPN/NRPN ekle."));
	}
	else if (tr >= 0 && cmd == 9012) {
		m_curCh = tr;
		if (m_ch.GetSafeHwnd()) m_ch.SetCurSel(m_curCh);
		m_ui.visible[tr] = 1;
		SyncFxBindsToLive();
		CSasamiInsertFxDlg::OpenOwned(this, &m_doc, tr + 1);
		m_status.SetWindowText(LL14(L"Insert FX: VST エフェクトを選び、ノブ調整、Bypass/Editor。", L"Insert FX: pick VST effect, tweak knobs, Bypass/Editor.", L"Insert FX : choisir un effet VST, régler, Bypass/Éditeur.", L"Insert FX: scegli effetto VST, regola, Bypass/Editor.", L"Insert FX: elige efecto VST, ajusta, Bypass/Editor.", L"Insert FX: VST 이펙트 선택, 노브 조정, Bypass/Editor.", L"Insert FX：选择 VST 效果、调旋钮、Bypass/Editor。", L"Insert FX: اختر تأثير VST، اضبط، Bypass/Editor.", L"Insert FX: выберите эффект VST, крутите, Bypass/Editor.", L"Insert FX: VST-Effekt wählen, Drehregler, Bypass/Editor.", L"Insert FX: escolha efeito VST, ajuste, Bypass/Editor.", L"Insert FX: kies VST-effect, draai knoppen, Bypass/Editor.", L"Insert FX: wybierz efekt VST, pokrętła, Bypass/Editor.", L"Insert FX: VST efekti seç, düğmeleri ayarla, Bypass/Editor."));
	}
	else if (tr >= 0 && cmd == 9020) {
		if (!m_ui.markStack
			&& ScMarkKindExists(m_doc.ev, m_doc.evCount, atTick, (uint8_t)tr, SC_EV_FM_LOOP_START)) {
			if (ScMidiAddLoopStart(&m_doc, atTick, tr, 2, 0)) afterMark(SC_EV_FM_LOOP_START);
		} else {
			int n = 2;
			if (CSasamiSimpleInputDlg::AskNumber(this, L"ループ開始 |:",
				L"繰り返し回数 (1–99)", 2, 1, 99, &n) == IDOK) {
				if (ScMidiAddLoopStart(&m_doc, atTick, tr, n, m_ui.markStack)) afterMark(SC_EV_FM_LOOP_START);
			}
		}
	}
	else if (tr >= 0 && cmd == 9021) {
		if (ScMidiAddLoopEnd(&m_doc, atTick, tr, m_ui.markStack)) afterMark(SC_EV_FM_LOOP_END);
	}
	else if (tr >= 0 && cmd == 9027) {
		if (ScMidiAddPedalOn(&m_doc, atTick, tr, m_ui.markStack)) afterMark(SC_EV_PEDAL_ON);
	}
	else if (tr >= 0 && cmd == 9028) {
		if (ScMidiAddPedalOff(&m_doc, atTick, tr, m_ui.markStack)) afterMark(SC_EV_PEDAL_OFF);
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
		if (ScMidiAddJumpMark(&m_doc, atTick, tr, m_ui.markStack)) afterMark(SC_EV_JUMP_MARK);
	}
	else if (tr >= 0 && cmd == 9023) {
		if (ScMidiAddJump(&m_doc, atTick, tr, m_ui.markStack)) afterMark(SC_EV_FM_JUMP);
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
	else if (tr >= 0 && cmd == 9001) {
		m_ui.mute[tr] ^= 1;
		ApplyMuteSoloToLive();
		InvalidateRect(m_trackRc, FALSE);
	}
	else if (tr >= 0 && cmd == 9002) { m_ui.solo[tr] ^= 1; InvalidateRect(m_trackRc, FALSE); }
	else if (tr >= 0 && cmd == 9003) {
		if (ScVstShowPartMenu(this, tr + 1, point, &m_doc.bind)) {
			RefreshProgLabels();
			InvalidateRect(m_trackRc, FALSE);
		}
	}
	else if (tr >= 0 && cmd == 9004) {
		m_ui.visible[tr] = m_ui.visible[tr] ? 0 : 1;
		savedata.sasamiMidiPartMask = ScStaffPackPartMask(&m_ui);
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
	else if (cmd == 9030 || cmd == 9031 || cmd == 9032) {
		HistPush();
		for (int i = 0; i < m_doc.evCount; i++) {
			int sel = 0;
			if (m_ui.nSel > 0) { for (int s = 0; s < m_ui.nSel; s++) if (m_ui.selList[s] == i) sel = 1; }
			else if (m_ui.selEv == i) sel = 1;
			if (!sel || m_doc.ev[i].kind != SC_EV_NOTE) continue;
			if (cmd == 9030) m_doc.ev[i].flags = (uint8_t)((m_doc.ev[i].flags & ~SC_EF_STEM_MASK) | SC_EF_STEM_UP);
			else if (cmd == 9031) m_doc.ev[i].flags = (uint8_t)((m_doc.ev[i].flags & ~SC_EF_STEM_MASK) | SC_EF_STEM_DOWN);
			else m_doc.ev[i].flags |= SC_EF_BEAM_BREAK;
		}
		InvalidateRect(m_bodyRc, FALSE);
	}
	else if (cmd == 9033 || cmd == 9034) {
		HistPush();
		const uint8_t kind = (cmd == 9033) ? SC_EV_SLUR_START : SC_EV_SLUR_END;
		ScToggleOrAddMark(m_doc.ev, &m_doc.evCount, m_ui.markerTick, (uint8_t)m_curCh,
			kind, 0, 0, 0, 0, m_ui.markStack);
		PushDocToText();
		InvalidateRect(m_bodyRc, FALSE);
		CString st;
		const int still = ScMarkKindExists(m_doc.ev, m_doc.evCount, m_ui.markerTick, (uint8_t)m_curCh, kind);
		if (!m_ui.markStack && !still)
			st.Format(L"Slur mark removed @ tick %u", (unsigned)m_ui.markerTick);
		else
			st.Format(L"Slur mark @ tick %u", (unsigned)m_ui.markerTick);
		m_status.SetWindowText(st);
	}
	else if (cmd == 9035 || cmd == 9036) {
		HistPush();
		const uint8_t kind = (cmd == 9035) ? SC_EV_CRESC : SC_EV_DIM;
		const uint16_t span = (uint16_t)(m_ui.placeDur > 0 ? m_ui.placeDur * 4 : SC_PPQN * 2);
		ScToggleOrAddMark(m_doc.ev, &m_doc.evCount, m_ui.markerTick, (uint8_t)m_curCh,
			kind, 0, 0, 0, span, m_ui.markStack);
		PushDocToText();
		InvalidateRect(m_bodyRc, FALSE);
		CString st;
		const int still = ScMarkKindExists(m_doc.ev, m_doc.evCount, m_ui.markerTick, (uint8_t)m_curCh, kind);
		if (!m_ui.markStack && !still)
			st.Format(L"Dyn mark removed @ tick %u", (unsigned)m_ui.markerTick);
		else
			st.Format(L"Dyn mark @ tick %u", (unsigned)m_ui.markerTick);
		m_status.SetWindowText(st);
	}
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
	else if (cmd == 9038) {
		/* Record options already expanded as check items below — keep for legacy id */
	}
	else if (cmd == 9039) {
		m_ui.gridEmph ^= 1;
		InvalidateRect(m_bodyRc, FALSE);
	}
	else if (cmd == 9050) m_midiIn.metroOn ^= 1;
	else if (cmd == 9051) m_midiIn.overdub ^= 1;
	else if (cmd == 9052) m_midiIn.quant = SC_QUANT_OFF;
	else if (cmd == 9053) m_midiIn.quant = SC_QUANT_WEAK;
	else if (cmd == 9054) m_midiIn.quant = SC_QUANT_STRONG;
	else if (cmd == 9055) m_midiIn.quant = SC_QUANT_SWING;
	else if (cmd == 9056) m_midiIn.velCurve = SC_VEL_LINEAR;
	else if (cmd == 9057) m_midiIn.velCurve = SC_VEL_SOFT;
	else if (cmd == 9058) { m_midiIn.velCurve = SC_VEL_FIXED; m_midiIn.velFixed = 100; }
	else if (cmd == 9006) {
		m_ui.tool = SC_TOOL_SELECT;
		m_ui.hoverValid = 0;
		UpdateNoteCursor();
		InvalidateRect(m_bodyRc, FALSE);
	}
	else if (cmd == 9060) {
		HistPush();
		if (ScStaffInsertBlankRange(m_doc.ev, m_doc.evCount, &m_ui)) {
			ScStaffUpdateContentExtent(&m_ui, m_doc.ev, m_doc.evCount);
			RefreshStrip();
			PushDocToText();
			InvalidateRect(m_bodyRc, FALSE);
			m_status.SetWindowText(LL14(
				L"範囲に空白を挿入（後ろずらし）", L"Inserted blank (shifted later events)", L"Espace inséré", L"Inserito vuoto", L"Espacio insertado",
				L"빈 구간 삽입", L"已插入空白", L"أدرج فراغ", L"Вставлена пустота", L"Leerraum eingefügt", L"Vazio inserido", L"Leeg ingevoegd", L"Wstawiono pustkę", L"Boş eklendi"));
		}
	}
	else if (cmd) PostMessage(WM_COMMAND, cmd);
}

void CSasamiMidiScoreDlg::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	const int ctrl = (GetKeyState(VK_CONTROL) & 0x8000) ? 1 : 0;
	const int shift = (GetKeyState(VK_SHIFT) & 0x8000) ? 1 : 0;
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
		if (ScTransposeSelected(m_doc.ev, m_doc.evCount, &m_ui, semis)) {
			InvalidateRect(m_bodyRc, FALSE);
			m_status.SetWindowText(LL14(L"選択を移調", L"Transpose selection", L"Transposer sélection", L"Trasponi selezione", L"Transponer selección", L"선택 이조", L"移调选区", L"نقل التحديد", L"Транспонировать", L"Auswahl transponieren", L"Transpor seleção", L"Selectie transponeren", L"Transponuj zaznaczenie", L"Seçimi transpoze"));
		}
		return;
	}
	if (ctrl && (nChar == VK_LEFT || nChar == VK_RIGHT)) {
		HistPush();
		const int mul = (nChar == VK_RIGHT) ? 2 : 1;
		const int div = (nChar == VK_RIGHT) ? 1 : 2;
		if (ScScaleDurSelected(m_doc.ev, m_doc.evCount, &m_ui, mul, div)) {
			InvalidateRect(m_bodyRc, FALSE);
			m_status.SetWindowText(LL14(L"音価を伸縮", L"Scale duration", L"Échelle durée", L"Scala durata", L"Escalar duración", L"음가 신축", L"缩放时值", L"مقياس المدة", L"Масштаб длительности", L"Dauer skalieren", L"Escalar duração", L"Duur schalen", L"Skaluj czas trwania", L"Süreyi ölçekle"));
		}
		return;
	}
	if (nChar == VK_HOME && !ctrl) {
		m_ui.markerTick = 0;
		m_status.SetWindowText(LL14(L"マーカー→0 (Home)。Spaceで先頭から再生。", L"Marker → 0 (Home). Space plays from start.", L"Marqueur → 0 (Home).", L"Marcatore → 0 (Home).", L"Marcador → 0 (Home).", L"마커→0 (Home).", L"标记→0 (Home)。", L"علامة → 0.", L"Маркер → 0.", L"Markierung → 0.", L"Marcador → 0.", L"Markering → 0.", L"Znacznik → 0.", L"İşaret → 0."));
		InvalidateRect(m_bodyRc, FALSE);
		return;
	}
	if (nChar == VK_SPACE) {
		if (m_ui.previewActive) {
			ScStaffStopHostPreview(&m_ui);
			KillTimer(kScPreviewTimer);
			VstLiveMonitorEnsure();
			m_status.SetWindowText(LL14(L"停止 (Space)。マーカー位置は維持 — Space で再開。", L"Stopped (Space). Marker stays — Space again to resume from marker.", L"Arrêté (Espace). Le marqueur reste — Espace pour reprendre.", L"Fermato (Spazio). Il marcatore resta — Spazio per riprendere.", L"Detenido (Espacio). El marcador permanece — Espacio para reanudar.", L"정지 (Space). 마커 유지 — Space로 재개.", L"已停止 (Space)。标记保留 — 再按 Space 从标记继续。", L"توقف (Space). العلامة تبقى — Space للاستئناف.", L"Стоп (Space). Маркер сохранён — Space для продолжения.", L"Gestoppt (Leertaste). Markierung bleibt — Leertaste zum Fortsetzen.", L"Parado (Espaço). Marcador permanece — Espaço para retomar.", L"Gestopt (Spatie). Markering blijft — Spatie om te hervatten.", L"Zatrzymano (Spacja). Znacznik zostaje — Spacja wznawia.", L"Durdu (Space). İşaret kalır — Space ile devam."));
			InvalidateRect(m_bodyRc, FALSE);
		} else {
			OnBnClickedPlay();
		}
		return;
	}
	if (nChar == VK_DELETE || nChar == VK_BACK) {
		HistPush();
		if (ScStaffSelDelete(m_doc.ev, &m_doc.evCount, &m_ui)) {
			ScStaffShrinkContentIfNeeded(&m_ui, m_doc.ev, m_doc.evCount);
			ScStaffUpdateContentExtent(&m_ui, m_doc.ev, m_doc.evCount);
			RefreshStrip();
			PushDocToText();
			InvalidateRect(m_bodyRc, FALSE);
			m_status.SetWindowText(LL14(L"選択を削除しました。", L"Deleted selection.", L"Sélection supprimée.", L"Selezione eliminata.", L"Selección eliminada.", L"선택 삭제됨.", L"已删除选区。", L"تم حذف التحديد.", L"Выделение удалено.", L"Auswahl gelöscht.", L"Seleção apagada.", L"Selectie verwijderd.", L"Usunięto zaznaczenie.", L"Seçim silindi."));
		} else {
			m_status.SetWindowText(LL14(L"先に音符/マークを選択（選択ツールまたはチップをクリック→Delete）。または消しゴム。", L"Select a note/mark first (Select or click chip → Delete). Or use Eraser.", L"Sélectionnez d’abord une note/marque (outil Sélection ou puce → Suppr). Ou utilisez la gomme.", L"Seleziona prima una nota/segno (Selezione o chip → Canc). Oppure usa la gomma.", L"Seleccione primero una nota/marca (Seleccionar o chip → Supr). O use el borrador.", L"먼저 음표/마크 선택(선택 도구 또는 칩→Delete). 또는 지우개.", L"请先选择音符/标记（选择工具或点击芯片→Delete）。或使用橡皮。", L"حدد نغمة/علامة أولاً (أداة التحديد أو الرقاقة → Delete). أو استخدم الممحاة.", L"Сначала выберите ноту/знак (Выбор или чип → Delete). Или ластик.", L"Zuerst Note/Zeichen wählen (Auswahl oder Chip → Entf). Oder Radierer.", L"Selecione primeiro uma nota/marca (Selecionar ou chip → Del). Ou use a borracha.", L"Selecteer eerst een noot/markering (Selecteren of chip → Del). Of gebruik de gum.", L"Najpierw zaznacz nutę/znak (Zaznacz lub chip → Del). Albo użyj gumki.", L"Önce nota/işaret seçin (Seç veya çip → Del). Ya da silgi kullanın."));
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
		m_clipN = ScStaffSelCopyEx(m_doc.ev, m_doc.evCount, &m_ui, 0, m_clip, SC_CLIP_MAX, &m_clipBase, &m_clipSpan);
		return;
	}
	if (ctrl && (nChar == 'X' || nChar == 'x')) {
		HistPush();
		m_clipN = ScStaffSelCopyEx(m_doc.ev, m_doc.evCount, &m_ui, 0, m_clip, SC_CLIP_MAX, &m_clipBase, &m_clipSpan);
		if (m_clipN > 0 && ScStaffSelDelete(m_doc.ev, &m_doc.evCount, &m_ui)) {
			ScStaffShrinkContentIfNeeded(&m_ui, m_doc.ev, m_doc.evCount);
			ScStaffUpdateContentExtent(&m_ui, m_doc.ev, m_doc.evCount);
			RefreshStrip();
			PushDocToText();
			InvalidateRect(m_bodyRc, FALSE);
		}
		return;
	}
	if (ctrl && shift && (nChar == 'V' || nChar == 'v')) {
		if (m_clipN > 0) {
			HistPush();
			ScStaffSelPasteEx(m_doc.ev, &m_doc.evCount, SC_EV_MAX, &m_ui, 0, m_clip, m_clipN, m_ui.markerTick, m_clipSpan, 1);
			ScStaffShrinkContentIfNeeded(&m_ui, m_doc.ev, m_doc.evCount);
			ScStaffUpdateContentExtent(&m_ui, m_doc.ev, m_doc.evCount);
			RefreshStrip();
			PushDocToText();
			InvalidateRect(m_bodyRc, FALSE);
		}
		return;
	}
	if (ctrl && (nChar == 'V' || nChar == 'v')) {
		if (m_clipN > 0) {
			HistPush();
			int insert = m_ui.pasteInsert;
			if (GetKeyState(VK_SHIFT) & 0x8000) insert = 1;
			ScStaffSelPasteEx(m_doc.ev, &m_doc.evCount, SC_EV_MAX, &m_ui, 0, m_clip, m_clipN, m_ui.markerTick, m_clipSpan, insert);
			ScStaffShrinkContentIfNeeded(&m_ui, m_doc.ev, m_doc.evCount);
			ScStaffUpdateContentExtent(&m_ui, m_doc.ev, m_doc.evCount);
			RefreshStrip();
			PushDocToText();
			InvalidateRect(m_bodyRc, FALSE);
		}
		return;
	}
	if (ctrl && (nChar == 'T' || nChar == 't')) {
		if (ScStaffTieSelected(m_doc.ev, m_doc.evCount, &m_ui))
			InvalidateRect(m_bodyRc, FALSE);
		return;
	}
	if (ctrl && shift && (nChar == 'I' || nChar == 'i')) {
		if (m_ui.selRangeValid || m_ui.nSel > 0 || m_ui.selEv >= 0) {
			HistPush();
			if (ScStaffInsertBlankRange(m_doc.ev, m_doc.evCount, &m_ui)) {
				ScStaffUpdateContentExtent(&m_ui, m_doc.ev, m_doc.evCount);
				RefreshStrip();
				PushDocToText();
				InvalidateRect(m_bodyRc, FALSE);
				m_status.SetWindowText(LL14(
					L"範囲に空白を挿入（後ろずらし）", L"Inserted blank (shifted later events)", L"Espace inséré", L"Inserito vuoto", L"Espacio insertado",
					L"빈 구간 삽입", L"已插入空白", L"أدرج فراغ", L"Вставлена пустота", L"Leerraum eingefügt", L"Vazio inserido", L"Leeg ingevoegd", L"Wstawiono pustkę", L"Boş eklendi"));
			}
		}
		return;
	}
	if (nChar == VK_ESCAPE) {
		ScStaffEnterSelectTool(&m_ui);
		ScStaffSelClear(&m_ui);
		UpdateNoteCursor();
		InvalidateRect(m_bodyRc, FALSE);
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
	KillTimer(kScMetroTimer);
	KillTimer(9121);
	KillTimer(9122);
	m_ui.previewActive = 0;
	ScScoreMidiInShutdown(&m_midiIn);
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

void CSasamiMidiScoreDlg::HistPush()
{
	ScScoreHistPush(&m_hist, m_doc.ev, m_doc.evCount);
}

void CSasamiMidiScoreDlg::NotifyEdited()
{
	PushDocToText();
	RefreshBoundRoll();
	Invalidate(FALSE);
}

void CSasamiMidiScoreDlg::RefreshBoundRoll()
{
	if (CSasamiPianoRollDlg* r = CSasamiPianoRollDlg::InstanceMidi())
		r->Refresh();
	if (m_ui.showRollSplit)
		InvalidateRect(m_rollRc, FALSE);
}

void CSasamiMidiScoreDlg::SyncMidiInCombos()
{
	if (!m_midiInDev.GetSafeHwnd()) return;
	ScScoreMidiInFillDeviceCombo(m_midiInDev.GetSafeHwnd());
	m_midiInCh.ResetContent();
	m_midiInCh.AddString(LL14(L"全て", L"All", L"Tous", L"Tutti", L"Todos", L"전체", L"全部", L"الكل", L"Все", L"Alle", L"Todos", L"Alles", L"Wszystkie", L"Tümü"));
	for (int i = 1; i <= 16; i++) {
		wchar_t s[16]; _snwprintf_s(s, _TRUNCATE, L"%d", i);
		m_midiInCh.AddString(s);
	}
	m_midiInCh.SetCurSel(m_midiIn.chFilter);
	m_midiInMode.ResetContent();
	m_midiInMode.AddString(LL14(L"OFF", L"OFF", L"OFF", L"OFF", L"OFF", L"OFF", L"关", L"OFF", L"OFF", L"AUS", L"OFF", L"UIT", L"WYŁ", L"KAPALI"));
	m_midiInMode.AddString(LL14(L"Step", L"Step", L"Pas", L"Step", L"Paso", L"스텝", L"步进", L"خطوة", L"Шаг", L"Schritt", L"Passo", L"Stap", L"Krok", L"Adım"));
	m_midiInMode.AddString(LL14(L"Realtime", L"Realtime", L"Temps réel", L"Realtime", L"Tiempo real", L"실시간", L"实时", L"فوري", L"Реалтайм", L"Echtzeit", L"Tempo real", L"Realtime", L"Realtime", L"Gerçek zaman"));
	m_midiInMode.SetCurSel(m_midiIn.mode);
	m_follow.ResetContent();
	m_follow.AddString(LL14(L"追従OFF", L"Follow OFF", L"Suivi OFF", L"Segui OFF", L"Seguir OFF", L"추종OFF", L"跟随关", L"تتبع OFF", L"След OFF", L"Folge AUS", L"Seguir OFF", L"Volgen UIT", L"Śledź OFF", L"Takip KAPALI"));
	m_follow.AddString(LL14(L"中央", L"Center", L"Centre", L"Centro", L"Centro", L"중앙", L"居中", L"وسط", L"Центр", L"Mitte", L"Centro", L"Midden", L"Środek", L"Merkez"));
	m_follow.AddString(LL14(L"ページ", L"Page", L"Page", L"Pagina", L"Página", L"페이지", L"翻页", L"صفحة", L"Страница", L"Seite", L"Página", L"Pagina", L"Strona", L"Sayfa"));
	m_follow.SetCurSel(m_ui.followMode);
	auto flat = [](CCustomComboBox& c) { if (c.GetSafeHwnd()) c.SetAeroMode(FALSE); };
	flat(m_midiInDev); flat(m_midiInCh); flat(m_midiInMode); flat(m_follow);
	if (m_btnArr.GetSafeHwnd()) { m_btnArr.SetAeroMode(FALSE); m_btnArr.SetFlat(TRUE); }
	if (m_btnChord.GetSafeHwnd()) { m_btnChord.SetAeroMode(FALSE); m_btnChord.SetFlat(TRUE); }
	if (m_btnPatt.GetSafeHwnd()) { m_btnPatt.SetAeroMode(FALSE); m_btnPatt.SetFlat(TRUE); }
	if (m_btnRoll.GetSafeHwnd()) { m_btnRoll.SetAeroMode(FALSE); m_btnRoll.SetFlat(TRUE); }
}

void CSasamiMidiScoreDlg::OnCbnMidiIn()
{
	if (!m_midiInDev.GetSafeHwnd()) return;
	const int di = m_midiInDev.GetCurSel();
	const int dev = (di >= 0) ? (int)m_midiInDev.GetItemData(di) : -1;
	const int ch = m_midiInCh.GetCurSel();
	const int mode = m_midiInMode.GetCurSel();
	m_midiIn.chFilter = (ch > 0) ? ch : 0;
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
		m_ui.previewActive = 0;
		m_metroBeat = 0;
		const UINT ms = (UINT)max(50.0, 60000.0 / max(1.0, bpm));
		SetTimer(kScMetroTimer, ms, NULL);
		m_status.SetWindowText(LL14(
			L"Realtime録音中 — 鍵盤を弾く（停止はモードOFF）",
			L"Realtime record — play keyboard (set mode OFF to stop)",
			L"Enregistrement temps réel — joue (mode OFF pour arrêter)",
			L"Registrazione realtime — suona (mode OFF per fermare)",
			L"Grabación realtime — toca (modo OFF para parar)",
			L"실시간 녹음 — 건반 연주 (정지는 모드 OFF)",
			L"实时录音 — 弹键盘（设为OFF停止）",
			L"تسجيل فوري — اعزف (OFF للإيقاف)",
			L"Реалтайм запись — играйте (режим OFF для стопа)",
			L"Echtzeit-Aufnahme — spielen (Modus AUS zum Stoppen)",
			L"Gravação realtime — toque (modo OFF para parar)",
			L"Realtime-opname — speel (modus UIT om te stoppen)",
			L"Nagrywanie realtime — graj (tryb WYŁ aby zatrzymać)",
			L"Gerçek zaman kayıt — çalın (durdurmak için OFF)"));
	} else {
		KillTimer(kScMetroTimer);
		ScScoreMidiInStopRealtime(&m_midiIn);
		if (m_midiIn.mode == SC_MIDIIN_STEP)
			m_status.SetWindowText(LL14(L"Step入力 — NoteOnで赤バーに配置", L"Step entry — NoteOn places at red bar", L"Entrée pas — NoteOn au curseur", L"Step — NoteOn sul cursore", L"Paso — NoteOn en barra", L"스텝 입력 — NoteOn으로 배치", L"步进 — NoteOn在红条放置", L"خطوة — NoteOn عند الشريط", L"Шаг — NoteOn на красной черте", L"Schritt — NoteOn an Markierung", L"Passo — NoteOn na barra", L"Stap — NoteOn bij rode balk", L"Krok — NoteOn na czerwonym pasku", L"Adım — NoteOn kırmızı çubuğa"));
		else
			m_status.SetWindowText(LL14(L"MIDI In OFF", L"MIDI In OFF", L"MIDI In OFF", L"MIDI In OFF", L"MIDI In OFF", L"MIDI In OFF", L"MIDI In 关", L"MIDI In OFF", L"MIDI In OFF", L"MIDI In AUS", L"MIDI In OFF", L"MIDI In UIT", L"MIDI In WYŁ", L"MIDI In KAPALI"));
	}
}

void CSasamiMidiScoreDlg::OnCbnFollow()
{
	const int s = m_follow.GetCurSel();
	if (s >= 0) m_ui.followMode = s;
}

void CSasamiMidiScoreDlg::ApplyFollowScroll()
{
	if (m_ui.followMode <= 0 || m_ui.followViewW <= 0) return;
	const int pxBeat = m_ui.pxBeat > 0 ? m_ui.pxBeat : SC_PX_BEAT_DEFAULT;
	const int margin = ScStaffClefMarginPx(&m_ui, m_doc.ev, m_doc.evCount);
	const int x = (int)((m_ui.playheadTick * (uint32_t)pxBeat) / SC_PPQN) + margin;
	if (m_ui.followMode == 1) {
		m_ui.scrollX = max(0, x - m_ui.followViewW / 2);
	} else if (m_ui.followMode == 2) {
		if (x < m_ui.scrollX + margin || x > m_ui.scrollX + m_ui.followViewW - 40)
			m_ui.scrollX = max(0, x - 40);
	}
}

void CSasamiMidiScoreDlg::ApplyMuteSoloToLive()
{
	int anySolo = 0;
	for (int i = 0; i < m_ui.trackCount; i++)
		if (m_ui.solo[i]) { anySolo = 1; break; }
	for (int i = 0; i < m_ui.trackCount && i < 32; i++) {
		int muted = m_ui.mute[i] ? 1 : 0;
		if (anySolo && !m_ui.solo[i]) muted = 1;
		/* Soft mute via CC7=0 when muted during live; restore strip vol when unmuted is heavy — skip restore. */
		if (muted)
			VstLiveMidiToPart(i + 1, (DWORD)(0xB0 | (i & 0x0F)) | (7 << 8) | (0 << 16));
	}
}

void CSasamiMidiScoreDlg::SetStripCcAtTick(int kind, uint32_t tick, int value0to127)
{
	if (m_ui.stripCount <= 0) return;
	int v = value0to127;
	if (v < 0) v = 0;
	if (v > 127) v = 127;
	const int col = ScStaffStripTickToCol(&m_ui, tick);
	if (col < 0) return;
	for (int L = 0; L < m_ui.stripCount && L < SC_STRIP_LANES_MAX; L++) {
		if (m_ui.stripKind[L] != kind) continue;
		m_ui.strip[L][col] = (uint8_t)v;
	}
	ScStaffApplyStripToDocMidi(&m_doc, m_curCh, &m_ui);
}

LRESULT CSasamiMidiScoreDlg::OnScoreMidi(WPARAM w, LPARAM)
{
	const DWORD msg = (DWORD)w;
	const int st = (int)(msg & 0xF0);
	const int data1 = (int)((msg >> 8) & 0xFF);
	const int data2 = (int)((msg >> 16) & 0xFF);
	const int part = m_curCh + 1;

	if (st == 0xB0) {
		const int sk = ScStaffStripKindFromCc(data1);
		if (sk >= 0) SetStripCcAtTick(sk, m_ui.markerTick, data2);
		else if (data1 == 64) {
			HistPush();
			if (data2 >= 64) ScMidiAddPedalOn(&m_doc, m_ui.markerTick, m_curCh, m_ui.markStack);
			else ScMidiAddPedalOff(&m_doc, m_ui.markerTick, m_curCh, m_ui.markStack);
			InvalidateRect(m_bodyRc, FALSE);
		}
		VstLiveMidiToPart(part, msg);
		return 0;
	}
	if (st == 0xE0) {
		const int bend14 = data1 | (data2 << 7);
		const int as7 = bend14 >> 7; /* 0..127 */
		SetStripCcAtTick(SC_STRIP_PITCH, m_ui.markerTick, as7);
		VstLiveMidiToPart(part, msg);
		return 0;
	}
	if (st == 0x90 && data2 > 0) {
		const int vel = ScScoreMidiInMapVelocity(&m_midiIn, data2);
		if (m_midiIn.mode == SC_MIDIIN_STEP) {
			HistPush();
			int dur = m_ui.placeDur > 0 ? m_ui.placeDur : SC_PPQN;
			if (m_ui.chordMode && m_ui.chordVoices >= 2) {
				ScChordPlaceAt(m_doc.ev, &m_doc.evCount, SC_EV_MAX, m_curCh,
					m_ui.markerTick, data1, dur, vel, 100, m_ui.chordType, m_ui.chordVoices,
					SC_EV_NOTE, 0);
			} else {
				ScMidiAddNote(&m_doc, m_ui.markerTick, m_curCh, data1, dur, vel);
			}
			m_ui.markerTick += (uint32_t)dur;
			VstLiveAuditionNote(part, data1, vel, 200);
			RefreshStrip();
			InvalidateRect(m_bodyRc, FALSE);
		} else if (m_midiIn.mode == SC_MIDIIN_REALTIME && m_midiIn.recording) {
			uint32_t tick = ScScoreMidiInNowTick(&m_midiIn, SC_PPQN);
			tick = ScScoreMidiInQuantizeTick(&m_midiIn, tick, SC_PPQN, 1);
			m_midiIn.heldNote[data1 & 127] = vel > 0 ? vel : 1;
			m_midiIn.heldOnTick[data1 & 127] = tick;
			VstLiveMidiToPart(part, msg);
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
			const int vel = ScScoreMidiInMapVelocity(&m_midiIn, m_midiIn.heldNote[data1 & 127]);
			HistPush();
			ScMidiAddNote(&m_doc, t0, m_curCh, data1, dur, vel);
			m_midiIn.heldNote[data1 & 127] = 0;
			RefreshStrip();
			InvalidateRect(m_bodyRc, FALSE);
		}
		VstLiveMidiToPart(part, msg);
		return 0;
	}
	return 0;
}

LRESULT CSasamiMidiScoreDlg::OnPalQueryState(WPARAM, LPARAM lParam)
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

LRESULT CSasamiMidiScoreDlg::OnPalLayout(WPARAM w, LPARAM l)
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

void CSasamiMidiScoreDlg::ApplyLayoutPalCmd(int cmdId)
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
		ScMidiAddKey(&m_doc, barTick, ks);
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
			ScMidiAddKey(&m_doc, barTick, ks);
			m_ui.keySig = ks;
			InvalidateRect(m_bodyRc, FALSE);
		}
		goto refresh_pal;
	}
	if (cmdId >= SASAMI_PAL_CMD_CLEF_G && cmdId <= SASAMI_PAL_CMD_CLEF_DR) {
		const int clef = cmdId - SASAMI_PAL_CMD_CLEF_G;
		HistPush();
		ScMidiAddClef(&m_doc, barTick, m_curCh, clef);
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
		const int n = ScTransposeSelected(m_doc.ev, m_doc.evCount, &m_ui, semi);
		if (n > 0) {
			PushDocToText();
			InvalidateRect(m_bodyRc, FALSE);
		}
		goto refresh_pal;
	}
	if (cmdId == SASAMI_PAL_CMD_TR_SEL_P12 || cmdId == SASAMI_PAL_CMD_TR_SEL_M12) {
		HistPush();
		const int semi = (cmdId == SASAMI_PAL_CMD_TR_SEL_P12) ? 12 : -12;
		const int n = ScTransposeSelected(m_doc.ev, m_doc.evCount, &m_ui, semi);
		if (n > 0) {
			PushDocToText();
			InvalidateRect(m_bodyRc, FALSE);
		}
		goto refresh_pal;
	}
	if (cmdId == SASAMI_PAL_CMD_TR_PART_PLUS || cmdId == SASAMI_PAL_CMD_TR_PART_MINUS) {
		HistPush();
		const int semi = (cmdId == SASAMI_PAL_CMD_TR_PART_PLUS) ? 1 : -1;
		const int n = ScTransposeChannel(m_doc.ev, m_doc.evCount, m_curCh, semi);
		if (n > 0) {
			PushDocToText();
			InvalidateRect(m_bodyRc, FALSE);
		}
		goto refresh_pal;
	}
	if (cmdId == SASAMI_PAL_CMD_TR_ALL_PLUS || cmdId == SASAMI_PAL_CMD_TR_ALL_MINUS) {
		HistPush();
		const int semi = (cmdId == SASAMI_PAL_CMD_TR_ALL_PLUS) ? 1 : -1;
		const int n = ScTransposeAll(m_doc.ev, m_doc.evCount, semi);
		if (n > 0) {
			PushDocToText();
			InvalidateRect(m_bodyRc, FALSE);
		}
		goto refresh_pal;
	}
refresh_pal:
	if (CSasamiLayoutPaletteDlg* pal = CSasamiLayoutPaletteDlg::Instance())
		pal->Invalidate(FALSE);
}

void CSasamiMidiScoreDlg::ApplyMeterAtBar(uint32_t tick, int numer, int denom)
{
	HistPush();
	const int defN = m_ui.meterNumer > 0 ? m_ui.meterNumer : 4;
	const int defD = m_ui.meterDenom > 0 ? m_ui.meterDenom : 4;
	tick = ScStaffSnapToBarTick(m_doc.ev, m_doc.evCount, tick, defN, defD);
	if (ScMidiAddMeter(&m_doc, tick, numer, denom)) {
		SyncMeterFromDoc();
		PushDocToText();
		InvalidateRect(m_bodyRc, FALSE);
		CString st;
		st.Format(L"@METER %d/%d @ bar tick %u", numer, denom, (unsigned)tick);
		m_status.SetWindowText(st);
	}
}

UINT CSasamiMidiScoreDlg::RunLayoutMenu(uint32_t atTick, CPoint screenPt)
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
	meter->AddCommand(9511, LL14(L"拍子を指定…", L"Custom meter…", L"Mesure perso…", L"Misura personalizzata…", L"Compás personalizado…",
		L"박자 지정…", L"自定义拍号…", L"إيقاع مخصص…", L"Свой размер…", L"Taktart eingeben…", L"Compasso personalizado…", L"Maat invoeren…", L"Metrum…", L"Ölçü…"));
	meter->AddCommand(9513, LL14(L"拍子変更を削除", L"Remove meter change", L"Supprimer mesure", L"Rimuovi misura", L"Quitar compás",
		L"박자 삭제", L"删除拍号变更", L"حذف الإيقاع", L"Удалить размер", L"Taktart entfernen", L"Remover compasso", L"Maat verwijderen", L"Usuń metrum", L"Ölçüyü sil"));
	menu.AddCommand(9512, LL14(L"調号…", L"Key signature…", L"Armature…", L"Armatura…", L"Armadura…",
		L"조표…", L"调号…", L"دليل…", L"Тональность…", L"Vorzeichen…", L"Armadura…", L"Toonsoort…", L"Tonacja…", L"Armatür…"));
	menu.AddSeparator();
	CCustomPopupMenu* tr = menu.AddSubMenu(LL14(L"移調", L"Transpose", L"Transposer", L"Trasporre", L"Transponer",
		L"이조", L"移调", L"نقل", L"Транспонирование", L"Transponieren", L"Transpor", L"Transponeren", L"Transponuj", L"Transpoze"), L"");
	tr->AddCommand(9520, LL14(L"選択 +1半音", L"Selection +1 semitone", L"Sélection +1 demi-ton", L"Selezione +1 semitono", L"Selección +1 semitono",
		L"선택 +1", L"选区 +1半音", L"تحديد +1", L"Выделение +1", L"Auswahl +1 Halbton", L"Seleção +1", L"Selectie +1", L"Zaznaczenie +1", L"Seçim +1"));
	tr->AddCommand(9521, LL14(L"選択 -1半音", L"Selection -1 semitone", L"Sélection -1", L"Selezione -1", L"Selección -1",
		L"선택 -1", L"选区 -1半音", L"تحديد -1", L"Выделение -1", L"Auswahl -1", L"Seleção -1", L"Selectie -1", L"Zaznaczenie -1", L"Seçim -1"));
	tr->AddCommand(9522, LL14(L"選択 +1オクターブ", L"Selection +1 octave", L"Sélection +1 octave", L"Selezione +1 ottava", L"Selección +1 octava",
		L"선택 +12", L"选区 +1八度", L"تحديد +12", L"Выделение +12", L"Auswahl +1 Oktave", L"Seleção +12", L"Selectie +12", L"Zaznaczenie +12", L"Seçim +12"));
	tr->AddCommand(9523, LL14(L"選択 -1オクターブ", L"Selection -1 octave", L"Sélection -1 octave", L"Selezione -1 ottava", L"Selección -1 octava",
		L"선택 -12", L"选区 -1八度", L"تحديد -12", L"Выделение -12", L"Auswahl -1 Oktave", L"Seleção -12", L"Selectie -12", L"Zaznaczenie -12", L"Seçim -12"));
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
		if (CSasamiSimpleInputDlg::AskNumber(this, LL14(L"拍子", L"Time signature", L"Mesure", L"Misura", L"Compás",
			L"박자", L"拍号", L"إيقاع", L"Размер", L"Taktart", L"Compasso", L"Maat", L"Metrum", L"Ölçü"),
			LL14(L"分子 (1–32)", L"Numerator (1–32)", L"Numérateur", L"Numeratore", L"Numerador",
			L"분자", L"分子", L"البسط", L"Числитель", L"Zähler", L"Numerador", L"Teller", L"Licznik", L"Pay"),
			numer, 1, 32, &numer) == IDOK
			&& CSasamiSimpleInputDlg::AskNumber(this, LL14(L"拍子", L"Time signature", L"Mesure", L"Misura", L"Compás",
				L"박자", L"拍号", L"إيقاع", L"Размер", L"Taktart", L"Compasso", L"Maat", L"Metrum", L"Ölçü"),
				LL14(L"分母 (1–32)", L"Denominator (1–32)", L"Dénominateur", L"Denominatore", L"Denominador",
				L"분모", L"分母", L"المقام", L"Знаменатель", L"Nenner", L"Denominador", L"Noemer", L"Mianownik", L"Payda"),
				denom, 1, 32, &denom) == IDOK)
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
		PushDocToText();
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
			n = ScTransposeSelected(m_doc.ev, m_doc.evCount, &m_ui, semi);
		else if (cmd == 9524 || cmd == 9525)
			n = ScTransposeChannel(m_doc.ev, m_doc.evCount, m_curCh, semi);
		else
			n = ScTransposeAll(m_doc.ev, m_doc.evCount, semi);
		if (n > 0) {
			PushDocToText();
			InvalidateRect(m_bodyRc, FALSE);
			CString st;
			st.Format(LL14(L"移調 %d 音", L"Transposed %d notes", L"%d notes transposées", L"%d note trasposte", L"%d notas transpueltas",
				L"%d음 이조", L"移调 %d 个音", L"%d نغمة", L"Транспонировано %d", L"%d Noten transponiert", L"%d notas", L"%d noten", L"%d nut", L"%d nota"), n);
			m_status.SetWindowText(st);
		}
	}
	return cmd;
}

void CSasamiMidiScoreDlg::OnBnClickedLayout()
{
	CRect rc;
	if (m_btnLayout.GetSafeHwnd())
		m_btnLayout.GetWindowRect(&rc);
	else
		GetWindowRect(&rc);
	CPoint pt(rc.left, rc.bottom);
	CSasamiLayoutPaletteDlg::OpenNear(this, pt);
}

void CSasamiMidiScoreDlg::OnBnClickedArr()
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
	}
}

void CSasamiMidiScoreDlg::OnBnClickedChord()
{
	CCustomPopupMenu menu;
	menu.SetAeroMode(FALSE);
	for (int i = 0; i < SC_CHORD_TYPE_COUNT; i++)
		menu.AddCheck(9300 + i, ScChordTypeName(i), m_ui.chordMode && m_ui.chordType == i);
	menu.AddSeparator();
	menu.AddCommand(9320, LL14(L"選択を和音化", L"Expand selection", L"Étendre sélection", L"Espandi selezione", L"Expandir selección", L"선택 화음화", L"选区成和弦", L"توسيع", L"Расширить", L"Auswahl erweitern", L"Expandir", L"Uitbreiden", L"Rozszerz", L"Seçimi genişlet"));
	menu.AddCommand(9321, LL14(L"コード記号…", L"From symbol…", L"Symbole…", L"Simbolo…", L"Símbolo…", L"코드 기호…", L"和弦记号…", L"رمز…", L"Символ…", L"Symbol…", L"Símbolo…", L"Symbool…", L"Symbol…", L"Sembol…"));
	CPoint pt; GetCursorPos(&pt);
	const UINT cmd = menu.Track(pt, this);
	if (cmd >= 9300 && cmd < 9300 + (UINT)SC_CHORD_TYPE_COUNT) {
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
		uint32_t t0 = 0, t1 = 0xFFFFFFFFu;
		if (m_ui.nSel > 0 || m_ui.selEv >= 0) {
			t0 = 0xFFFFFFFFu; t1 = 0;
			for (int i = 0; i < m_doc.evCount; i++) {
				int sel = 0;
				if (m_ui.nSel > 0) {
					for (int s = 0; s < m_ui.nSel; s++) if (m_ui.selList[s] == i) sel = 1;
				} else if (m_ui.selEv == i) sel = 1;
				if (!sel) continue;
				if (m_doc.ev[i].tick < t0) t0 = m_doc.ev[i].tick;
				if (m_doc.ev[i].tick + m_doc.ev[i].dur > t1) t1 = m_doc.ev[i].tick + m_doc.ev[i].dur;
			}
		}
		ScChordExpandNotes(m_doc.ev, &m_doc.evCount, SC_EV_MAX, m_curCh, t0, t1, m_ui.chordType, m_ui.chordVoices);
		InvalidateRect(m_bodyRc, FALSE);
	} else if (cmd == 9321) {
		wchar_t symBuf[64] = L"C";
		if (CSasamiSimpleInputDlg::AskText(this,
			LL14(L"コード", L"Chord", L"Accord", L"Accordo", L"Acorde", L"코드", L"和弦", L"وتر", L"Аккорд", L"Akkord", L"Acorde", L"Akkoord", L"Akor", L"Akor"),
			L"C / Am7 / G7 …", symBuf, 64) == IDOK) {
			HistPush();
			ScChordFromSymbol(m_doc.ev, &m_doc.evCount, SC_EV_MAX, m_curCh,
				m_ui.markerTick, m_ui.placeDur > 0 ? m_ui.placeDur : SC_PPQN, 100, symBuf, 4);
			InvalidateRect(m_bodyRc, FALSE);
		}
	}
}

void CSasamiMidiScoreDlg::OnBnClickedPatt()
{
	CCustomPopupMenu menu;
	menu.SetAeroMode(FALSE);
	for (int i = 0; i < SC_PAT_COUNT; i++)
		menu.AddCheck(9400 + i, ScPatternName(i), m_ui.patternMode && m_ui.patternId == i);
	CPoint pt; GetCursorPos(&pt);
	const UINT cmd = menu.Track(pt, this);
	if (cmd >= 9400 && cmd < 9400 + (UINT)SC_PAT_COUNT) {
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

void CSasamiMidiScoreDlg::OnBnClickedRoll()
{
	m_ui.showRollSplit ^= 1;
	LayoutChrome();
	CSasamiPianoRollDlg::OpenForMidi(this);
	Invalidate(FALSE);
}

