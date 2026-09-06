#include "stdafx.h"
#include "ogg.h"
#include "CSasamiPianoRollDlg.h"
#include "CSasamiMidiScoreDlg.h"
#include "CSasamiFmScoreDlg.h"
#include "CSasamiNotePaletteDlg.h"
#include "CSasamiLayoutPaletteDlg.h"
#include "CSasamiScoreArrange.h"
#include "CSasamiCmdHelpDlg.h"
#include "CCustomPopupMenu.h"
#include "OfflineHelp.h"
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
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSEWHEEL()
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
			b.Create(t, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(0, 0, 40, 22), this, id);
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
			c.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
				CRect(0, 0, 80, 120), this, id);
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
	int x = 6, y = cap + 4;
	const int bh = 22, gap = 2, bw = 44;
	auto place = [&](CWnd& w, int wdt) {
		if (w.GetSafeHwnd()) { w.MoveWindow(x, y, wdt, bh); x += wdt + gap; }
	};
	place(m_btnOpen, bw); place(m_btnSave, bw); place(m_btnNew, bw);
	place(m_btnPlay, bw + 8); place(m_btnExport, bw + 8);
	place(m_btnPencil, bw); place(m_btnErase, bw); place(m_btnSel, bw);
	place(m_btnPal, bw); place(m_btnTempo, bw);
	place(m_btnText, bw); place(m_btnLayout, bw); place(m_btnArr, bw);
	if (!m_isFm) { place(m_btnChord, bw); place(m_btnPatt, bw); place(m_btnFx, bw); }
	else place(m_btnVoice, bw);
	place(m_btnScore, bw + 8); place(m_btnHelp, 28);
	place(m_ch, 72); place(m_pasteMode, 90); place(m_follow, 90);
	const int row1 = y + bh + 4;
	x = 6; y = row1;
	place(m_stripLanes, 80); place(m_stripKind0, 110); place(m_stripDraw, 90); place(m_stripStep, 80);
	const int toolBottom = y + bh + 4;
	m_toolbarRc = CRect(0, cap, rc.right, toolBottom);
	const int helpH = 40, statusH = 20, stripH = 72;
	const int bottom = rc.bottom;
	if (m_helpBar.GetSafeHwnd())
		m_helpBar.MoveWindow(6, bottom - helpH - 2, rc.Width() - 12, helpH);
	if (m_status.GetSafeHwnd())
		m_status.MoveWindow(6, bottom - helpH - statusH - 4, rc.Width() - 12, statusH);
	m_stripRc = CRect(6, bottom - helpH - statusH - stripH - 8, rc.right - 6, bottom - helpH - statusH - 6);
	m_gridRc = CRect(0, toolBottom, rc.right, m_stripRc.top - 2);
	m_bodyRc = m_gridRc;
	if (m_btnChord.GetSafeHwnd()) m_btnChord.ShowWindow(m_isFm ? SW_HIDE : SW_SHOW);
	if (m_btnPatt.GetSafeHwnd()) m_btnPatt.ShowWindow(m_isFm ? SW_HIDE : SW_SHOW);
	if (m_btnFx.GetSafeHwnd()) m_btnFx.ShowWindow(m_isFm ? SW_HIDE : SW_SHOW);
	if (m_btnVoice.GetSafeHwnd()) m_btnVoice.ShowWindow(m_isFm ? SW_SHOW : SW_HIDE);
	if (m_btnNew.GetSafeHwnd()) m_btnNew.ShowWindow(m_isFm ? SW_HIDE : SW_SHOW);
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
	wchar_t buf[512];
	ScStaffFormatHelpBar(buf, 512, m_ui, m_isFm, m_curPart ? *m_curPart : 0);
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
		if (m_stripKind0.GetCount() > 0)
			m_stripKind0.SetCurSel(0);
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
	if (m_stripDraw.GetSafeHwnd()) {
		int s = m_stripDraw.GetCurSel();
		if (s >= 0) m_ui->stripDraw = s;
	}
	Refresh();
}

LRESULT CSasamiPianoRollDlg::OnPalDur(WPARAM w, LPARAM l)
{
	CWnd* sc = m_isFm ? (CWnd*)CSasamiFmScoreDlg::Instance() : (CWnd*)CSasamiMidiScoreDlg::Instance();
	if (sc) sc->SendMessage(WM_SASAMI_PAL_DUR, w, l);
	UpdateHelpBar();
	Refresh();
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

void CSasamiPianoRollDlg::PlaceNoteAt(CPoint pt)
{
	if (!m_ev || !m_evCount || !m_ui || !m_curPart) return;
	if (pt.x < m_gridRc.left + SC_ROLL_KEY_W) return;
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
	int hit = ScPianoRollHitResize(&m_roll, m_gridRc, m_ev, *m_evCount, m_ui, *m_curPart, pt);
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
	if (!m_ev || !m_evCount || !m_ui || !m_curPart) return;
	if (!m_gridRc.PtInRect(point)) {
		if (m_stripRc.PtInRect(point) && m_ui->stripCount > 0) {
			/* strip edit delegated: set marker from x */
			m_ui->markerTick = ScPianoRollXToTick(&m_roll, m_gridRc, m_ui, point.x);
			Refresh();
		}
		return;
	}
	if (m_ui->tool == SC_TOOL_PENCIL) { PlaceNoteAt(point); return; }
	if (m_ui->tool == SC_TOOL_ERASER) { EraseAt(point); m_dragMode = 4; SetCapture(); return; }
	BeginSelectOrDrag(point, nFlags);
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
	ReleaseCapture();
	Refresh();
	(void)point;
}

void CSasamiPianoRollDlg::OnMouseMove(UINT nFlags, CPoint point)
{
	CCustomBlurDialogExBase::OnMouseMove(nFlags, point);
	if (!m_ui || !m_ev || !m_evCount) return;
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
	if (nFlags & MK_CONTROL && m_ui) {
		m_ui->scrollX -= (zDelta / 4);
		if (m_ui->scrollX < 0) m_ui->scrollX = 0;
		if (m_midiScore) m_midiScore->Invalidate(FALSE);
		if (m_fmScore) m_fmScore->Invalidate(FALSE);
	} else {
		m_roll.scrollY -= (zDelta / 30) * m_roll.rowH;
		if (m_roll.scrollY < 0) m_roll.scrollY = 0;
	}
	Refresh();
	(void)pt;
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
	if (!m_ui || m_ui->tool != SC_TOOL_SELECT) return;
	CPoint pt = point;
	if (pt.x < 0) { pt = CPoint(0, 0); ClientToScreen(&pt); }
	CCustomPopupMenu menu;
	menu.AddCommand(9001, LL14(L"コピー\tCtrl+C", L"Copy\tCtrl+C", L"Copier", L"Copia", L"Copiar", L"복사", L"复制", L"نسخ", L"Копировать", L"Kopieren", L"Copiar", L"Kopiëren", L"Kopiuj", L"Kopyala"));
	menu.AddCommand(9002, LL14(L"切り取り\tCtrl+X", L"Cut\tCtrl+X", L"Couper", L"Taglia", L"Cortar", L"잘라내기", L"剪切", L"قص", L"Вырезать", L"Ausschneiden", L"Recortar", L"Knippen", L"Wytnij", L"Kes"));
	menu.AddCommand(9003, LL14(L"貼り付け\tCtrl+V", L"Paste\tCtrl+V", L"Coller", L"Incolla", L"Pegar", L"붙여넣기", L"粘贴", L"لصق", L"Вставить", L"Einfügen", L"Colar", L"Plakken", L"Wklej", L"Yapıştır"));
	menu.AddCommand(9060, LL14(L"範囲に空白を挿入\tCtrl+Shift+I", L"Insert blank in range\tCtrl+Shift+I", L"Insérer vide", L"Inserisci vuoto", L"Insertar vacío", L"빈 구간 삽입", L"插入空白", L"إدراج فراغ", L"Вставить пустоту", L"Leerraum", L"Inserir vazio", L"Leeg invoegen", L"Wstaw pustkę", L"Boş ekle"));
	menu.AddCommand(9004, LL14(L"削除\tDelete", L"Delete\tDelete", L"Supprimer", L"Elimina", L"Eliminar", L"삭제", L"删除", L"حذف", L"Удалить", L"Löschen", L"Apagar", L"Verwijderen", L"Usuń", L"Sil"));
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
	}
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
