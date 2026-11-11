#include "stdafx.h"
#include "ogg.h"
#include "CSasamiToneMapDlg.h"
#include "CSasamiVstPartMenu.h"
#include "SasamiToneNames.h"
#include "VstMidiEngine.h"
#include "CSasamiStaffCore.h"

IMPLEMENT_DYNAMIC(CSasamiToneMapDlg, CCustomBlurDialogExBase)

struct ScToneMapInfo {
	const wchar_t* name;
	int mapId;
	int isXg;
	int defMsb;
	int defLsb;
};

static const ScToneMapInfo kMaps[] = {
	{ L"GMmap", 5, 0, 0, 0 },
	{ L"GM2map", 9, 0, 121, 0 },
	{ L"LAmap", 8, 0, 127, 0 },
	{ L"55map", 1, 0, 0, 1 },
	{ L"88map", 2, 0, 0, 2 },
	{ L"88Promap", 3, 0, 0, 3 },
	{ L"8850map", 4, 0, 0, 4 },
	{ L"XGmap", 0, 1, 0, 0 },
	{ L"SDmap", 6, 0, 64, 0 },
};
static const int kMapCount = (int)(sizeof(kMaps) / sizeof(kMaps[0]));

CSasamiToneMapDlg::CSasamiToneMapDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(IDD_SASAMI_TONE_MAP, pParent)
	, m_part(1), m_prog(0), m_bankMsb(0), m_bankLsb(0), m_mapSel(0)
	, m_pickedVst3(0), m_isDrum(0), m_bind(NULL), m_scroll(0), m_cellH(18), m_auditionHold(0)
{
	memset(m_names, 0, sizeof(m_names));
}

int CSasamiToneMapDlg::PickForPart(CWnd* owner, int part1to32, ScMidiVstBind* bind, int isDrum)
{
	CSasamiToneMapDlg dlg(owner);
	dlg.m_part = part1to32;
	dlg.m_bind = bind;
	dlg.m_isDrum = (isDrum >= 0) ? (isDrum ? 1 : 0)
		: ((part1to32 == 10 || part1to32 == 26) ? 1 : 0);
	if (bind) {
		if (bind->vstProg[part1to32 - 1] >= 0)
			dlg.m_prog = bind->vstProg[part1to32 - 1] & 127;
		if (bind->vstBankMsb[part1to32 - 1] >= 0)
			dlg.m_bankMsb = bind->vstBankMsb[part1to32 - 1] & 127;
		if (bind->vstBankLsb[part1to32 - 1] >= 0)
			dlg.m_bankLsb = bind->vstBankLsb[part1to32 - 1] & 127;
	}
	const INT_PTR r = dlg.DoModal();
	if (r != IDOK) return (int)r;
	if (dlg.m_pickedVst3)
		return IDOK;
	if (bind) {
		bind->isMpw3 = 1;
		bind->vstProg[part1to32 - 1] = dlg.m_prog;
		bind->vstBankMsb[part1to32 - 1] = dlg.m_bankMsb;
		bind->vstBankLsb[part1to32 - 1] = dlg.m_bankLsb;
	}
	return IDOK;
}

void CSasamiToneMapDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SASAMI_TM_OK, m_ok);
	DDX_Control(pDX, IDC_SASAMI_TM_CANCEL, m_cancel);
	DDX_Control(pDX, IDC_SASAMI_TM_EDITOR, m_editor);
	DDX_Control(pDX, IDC_SASAMI_TM_VST, m_vst3);
	DDX_Control(pDX, IDC_SASAMI_TM_MAP, m_map);
	DDX_Control(pDX, IDC_SASAMI_TM_BANKM, m_bankM);
	DDX_Control(pDX, IDC_SASAMI_TM_BANKL, m_bankL);
	DDX_Control(pDX, IDC_SASAMI_TM_HINT, m_hint);
	DDX_Control(pDX, IDC_SASAMI_TM_LBL_MAP, m_lblMap);
	DDX_Control(pDX, IDC_SASAMI_TM_LBL_MSB, m_lblMsb);
	DDX_Control(pDX, IDC_SASAMI_TM_LBL_LSB, m_lblLsb);
}

BEGIN_MESSAGE_MAP(CSasamiToneMapDlg, CCustomBlurDialogExBase)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_SIZE()
	ON_WM_MOUSEWHEEL()
	ON_CBN_SELCHANGE(IDC_SASAMI_TM_MAP, OnCbnSelchangeMap)
	ON_CBN_SELCHANGE(IDC_SASAMI_TM_BANKM, OnCbnSelchangeBank)
	ON_CBN_SELCHANGE(IDC_SASAMI_TM_BANKL, OnCbnSelchangeBank)
	ON_BN_CLICKED(IDC_SASAMI_TM_OK, OnBnClickedOk)
	ON_BN_CLICKED(IDC_SASAMI_TM_CANCEL, OnBnClickedCancel)
	ON_BN_CLICKED(IDC_SASAMI_TM_EDITOR, OnBnClickedEditor)
	ON_BN_CLICKED(IDC_SASAMI_TM_VST, OnBnClickedVst3)
	ON_WM_DESTROY()
END_MESSAGE_MAP()

BOOL CSasamiToneMapDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	SetWindowText(LL14(L"トーンマップ", L"Tone map", L"Table de sons", L"Mappa timbri", L"Mapa de tonos", L"톤 맵", L"音色表", L"خريطة النغمات", L"Карта тембров", L"Klangzuordnung", L"Mapa de timbres", L"Toonkaart", L"Mapa brzmień", L"Ton haritası"));
	m_ok.SetWindowText(LL14(L"決定", L"OK", L"OK", L"OK", L"Aceptar", L"결정", L"确定", L"موافق", L"ОК", L"OK", L"OK", L"OK", L"OK", L"Tamam"));
	m_cancel.SetWindowText(LL14(L"キャンセル", L"Cancel", L"Annuler", L"Annulla", L"Cancelar", L"취소", L"取消", L"إلغاء", L"Отмена", L"Abbrechen", L"Cancelar", L"Annuleren", L"Anuluj", L"İptal"));
	m_editor.SetWindowText(LL14(L"設定画面", L"Settings", L"Réglages", L"Impostazioni", L"Ajustes", L"설정 화면", L"设置画面", L"الإعدادات", L"Настройки", L"Einstellungen", L"Ajustes", L"Instellingen", L"Ustawienia", L"Ayarlar"));
	m_vst3.SetWindowText(L"VST3…");
	m_lblMap.SetWindowText(L"Map");
	m_lblMsb.SetWindowText(L"MSB");
	m_lblLsb.SetWindowText(L"LSB");
	auto flat = [](CCustomStandardButton& b) {
		if (b.GetSafeHwnd()) { b.SetAeroMode(FALSE); b.SetFlat(TRUE); }
	};
	flat(m_ok); flat(m_cancel); flat(m_editor); flat(m_vst3);
	if (m_map.GetSafeHwnd()) m_map.SetAeroMode(FALSE);
	if (m_bankM.GetSafeHwnd()) m_bankM.SetAeroMode(FALSE);
	if (m_bankL.GetSafeHwnd()) m_bankL.SetAeroMode(FALSE);
	if (m_hint.GetSafeHwnd()) m_hint.SetAeroMode(FALSE);
	if (m_lblMap.GetSafeHwnd()) m_lblMap.SetAeroMode(FALSE);
	if (m_lblMsb.GetSafeHwnd()) m_lblMsb.SetAeroMode(FALSE);
	if (m_lblLsb.GetSafeHwnd()) m_lblLsb.SetAeroMode(FALSE);

	m_map.ResetContent();
	for (int i = 0; i < kMapCount; ++i)
		m_map.AddString(kMaps[i].name);

	auto pathOk = [](const wchar_t* p) -> int {
		if (!p || !p[0]) return 0;
		return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES ? 1 : 0;
	};
	const int haveGs = pathOk(savedata.vstMultiDll);
	const int haveXg = pathOk(savedata.vstExtraPath);
	const int bankUnset = (m_bankMsb == 0 && m_bankLsb == 0 && m_prog == 0);
	m_mapSel = 0;
	if (haveGs) {
		for (int i = 0; i < kMapCount; ++i)
			if (kMaps[i].mapId == 4) { m_mapSel = i; break; }
		if (bankUnset) {
			m_bankMsb = kMaps[m_mapSel].defMsb;
			m_bankLsb = kMaps[m_mapSel].defLsb;
		}
	} else if (haveXg) {
		for (int i = 0; i < kMapCount; ++i)
			if (kMaps[i].isXg) { m_mapSel = i; break; }
		if (bankUnset) {
			m_bankMsb = kMaps[m_mapSel].defMsb;
			m_bankLsb = kMaps[m_mapSel].defLsb;
		}
	} else {
		for (int i = 0; i < kMapCount; ++i) {
			if (kMaps[i].defLsb == m_bankLsb && kMaps[i].defMsb == m_bankMsb) {
				m_mapSel = i; break;
			}
			if (m_bankLsb >= 1 && m_bankLsb <= 4 && kMaps[i].defLsb == m_bankLsb)
				m_mapSel = i;
			if (m_bankMsb == 127 && kMaps[i].mapId == 8) m_mapSel = i;
			if (m_bankMsb == 121 && kMaps[i].mapId == 9) m_mapSel = i;
		}
	}
	m_map.SetCurSel(m_mapSel);

	if (!ScRestoreWndGeom(this, savedata.sasamiToneMapX, savedata.sasamiToneMapY,
		savedata.sasamiToneMapW, savedata.sasamiToneMapH, 560, 420)) {
		CRect wr; GetWindowRect(&wr);
		const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
		const int chrome = cap + 8 + 18 + 26 + 22 + 8;
		const int gridH = 32 * 18;
		const int wantH = chrome + gridH + 12;
		const int wantW = max(wr.Width(), 640);
		SetWindowPos(NULL, 0, 0, wantW, wantH,
			SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	}

	RebuildBankCombos();
	LayoutChrome();
	RebuildGrid();
	CString h;
	h.Format(m_isDrum
		? LL14(
			L"パート %d（ドラム）\nクリックで試聴、ダブルクリックで決定。VST3…で専用音源。",
			L"Part %d (drums)\nClick to preview, double-click to apply. VST3… for a dedicated instrument.",
			L"Partie %d (batterie)\nClic pour écouter, double-clic pour valider. VST3… pour un instrument dédié.",
			L"Parte %d (batteria)\nClic per ascoltare, doppio clic per confermare. VST3… per uno strumento dedicato.",
			L"Parte %d (batería)\nClic para oír, doble clic para aceptar. VST3… para un instrumento propio.",
			L"파트 %d (드럼)\n클릭으로 미리듣기, 더블클릭으로 결정. VST3…로 전용 음원.",
			L"声部 %d（鼓）\n单击试听，双击确定。VST3… 用于专用音源。",
			L"الجزء %d (طبول)\nنقرة للمعاينة، نقرة مزدوجة للتأكيد. VST3… لآلة مخصصة.",
			L"Партия %d (ударные)\nЩелчок — прослушать, двойной — принять. VST3… для своего инструмента.",
			L"Part %d (Schlagzeug)\nKlick zum Hören, Doppelklick zum Übernehmen. VST3… für ein eigenes Instrument.",
			L"Parte %d (bateria)\nClique para ouvir, duplo clique para aceitar. VST3… para um instrumento próprio.",
			L"Deel %d (drums)\nKlik om te horen, dubbelklik om te kiezen. VST3… voor een eigen instrument.",
			L"Partia %d (perkusja)\nKlik — odsłuch, podwójny — zatwierdź. VST3… dla własnego instrumentu.",
			L"Parti %d (davul)\nTıkla dinle, çift tıkla onayla. VST3… özel çalgı için.")
		: LL14(
			L"パート %d\nクリックで試聴、ダブルクリックで決定。VST3…で専用音源。",
			L"Part %d\nClick to preview, double-click to apply. VST3… for a dedicated instrument.",
			L"Partie %d\nClic pour écouter, double-clic pour valider. VST3… pour un instrument dédié.",
			L"Parte %d\nClic per ascoltare, doppio clic per confermare. VST3… per uno strumento dedicato.",
			L"Parte %d\nClic para oír, doble clic para aceptar. VST3… para un instrumento propio.",
			L"파트 %d\n클릭으로 미리듣기, 더블클릭으로 결정. VST3…로 전용 음원.",
			L"声部 %d\n单击试听，双击确定。VST3… 用于专用音源。",
			L"الجزء %d\nنقرة للمعاينة، نقرة مزدوجة للتأكيد. VST3… لآلة مخصصة.",
			L"Партия %d\nЩелчок — прослушать, двойной — принять. VST3… для своего инструмента.",
			L"Part %d\nKlick zum Hören, Doppelklick zum Übernehmen. VST3… für ein eigenes Instrument.",
			L"Parte %d\nClique para ouvir, duplo clique para aceitar. VST3… para um instrumento próprio.",
			L"Deel %d\nKlik om te horen, dubbelklik om te kiezen. VST3… voor een eigen instrument.",
			L"Partia %d\nKlik — odsłuch, podwójny — zatwierdź. VST3… dla własnego instrumentu.",
			L"Parti %d\nTıkla dinle, çift tıkla onayla. VST3… özel çalgı için."), m_part);
	m_hint.SetWindowText(h);
	CCC_BringDialogToForeground(this);
	return TRUE;
}

void CSasamiToneMapDlg::LayoutChrome()
{
	CRect rc; GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	const int top = cap + 6;
	const int comboH = 24;
	const int lblH = 16;
	int x = 8;
	if (m_lblMap.GetSafeHwnd()) m_lblMap.MoveWindow(x, top, 40, lblH);
	if (m_map.GetSafeHwnd()) m_map.MoveWindow(x, top + lblH, 100, comboH);
	x += 108;
	if (m_lblMsb.GetSafeHwnd()) m_lblMsb.MoveWindow(x, top, 40, lblH);
	if (m_bankM.GetSafeHwnd()) m_bankM.MoveWindow(x, top + lblH, 72, comboH);
	x += 80;
	if (m_lblLsb.GetSafeHwnd()) m_lblLsb.MoveWindow(x, top, 40, lblH);
	if (m_bankL.GetSafeHwnd()) m_bankL.MoveWindow(x, top + lblH, 72, comboH);
	x += 84;
	if (m_vst3.GetSafeHwnd()) {
		const int ww = ScChromeButtonWidth(&m_vst3, 72, 100);
		m_vst3.MoveWindow(x, top + lblH, ww, 22);
		x += ww + 6;
	}
	if (m_editor.GetSafeHwnd()) {
		const int ww = ScChromeButtonWidth(&m_editor, 80, 120);
		m_editor.MoveWindow(x, top + lblH, ww, 22);
		x += ww + 6;
	}
	{
		const int cancelW = ScChromeButtonWidth(&m_cancel, 96, 130);
		const int okW = ScChromeButtonWidth(&m_ok, 64, 90);
		int cx = rc.right - 8 - cancelW;
		int ox = cx - 6 - okW;
		if (ox < x) ox = x;
		if (cx < ox + okW + 6) cx = ox + okW + 6;
		if (m_ok.GetSafeHwnd()) m_ok.MoveWindow(ox, top + lblH, okW, 22);
		if (m_cancel.GetSafeHwnd()) m_cancel.MoveWindow(cx, top + lblH, cancelW, 22);
	}
	const int hintY = top + lblH + comboH + 4;
	if (m_hint.GetSafeHwnd()) m_hint.MoveWindow(8, hintY, rc.Width() - 16, 36);
	m_gridRc.SetRect(8, hintY + 40, rc.right - 8, rc.bottom - 8);
	m_cellH = 18;
	if (m_gridRc.Height() > 32) {
		int h = m_gridRc.Height() / 32;
		if (h < 16) h = 16;
		if (h > 20) h = 20;
		m_cellH = h;
	}
}

void CSasamiToneMapDlg::RebuildBankCombos()
{
	const ScToneMapInfo& m = kMaps[m_mapSel < 0 ? 0 : (m_mapSel >= kMapCount ? 0 : m_mapSel)];
	const int isDrum = m_isDrum;
	m_bankM.ResetContent();
	m_bankL.ResetContent();
	for (int i = 0; i < 128; ++i) {
		CString s;
		/* MSB: any tone in this MSB (LSB fixed to current / map default for probe). */
		const int lsbProbe = m.isXg ? m_bankLsb : m.defLsb;
		const int msbUsed = SasamiToneBankUsed(m.isXg, m.mapId, i, lsbProbe, isDrum)
			|| (!m.isXg && i == m.defMsb)
			|| (m.isXg && i == 0);
		if (msbUsed) {
			s.Format(L"%d", i);
			m_bankM.AddString(s, FALSE);
		} else {
			m_bankM.AddString(L"------", TRUE);
		}
		/* LSB: for GS maps usually map-select; mark unused. */
		const int msbProbe = m_bankMsb;
		const int lsbUsed = SasamiToneBankUsed(m.isXg, m.mapId, msbProbe, i, isDrum)
			|| (!m.isXg && i == m.defLsb)
			|| (m.isXg && i == 0 && msbProbe == 0);
		if (lsbUsed) {
			s.Format(L"%d", i);
			m_bankL.AddString(s, FALSE);
		} else {
			m_bankL.AddString(L"------", TRUE);
		}
	}
	if (m_bankMsb < 0) m_bankMsb = 0;
	if (m_bankLsb < 0) m_bankLsb = 0;
	m_bankM.SetCurSelPhysical(m_bankMsb & 127);
	m_bankL.SetCurSelPhysical(m_bankLsb & 127);
	/* If current landed on ------, snap to map default. */
	if (m_bankM.GetCurSel() < 0) {
		m_bankMsb = m.defMsb;
		m_bankM.SetCurSelPhysical(m_bankMsb);
	}
	if (m_bankL.GetCurSel() < 0) {
		m_bankLsb = m.defLsb;
		m_bankL.SetCurSelPhysical(m_bankLsb);
	}
}

void CSasamiToneMapDlg::RebuildGrid()
{
	const ScToneMapInfo& m = kMaps[m_mapSel < 0 ? 0 : (m_mapSel >= kMapCount ? 0 : m_mapSel)];
	const int isDrum = m_isDrum;
	for (int pc = 0; pc < 128; ++pc) {
		m_names[pc][0] = 0;
		SasamiToneLookupStrict(m.isXg, m.mapId, m_bankMsb, m_bankLsb, pc, isDrum,
			m_names[pc], 40);
		if (!m_names[pc][0])
			wcsncpy_s(m_names[pc], L"------", _TRUNCATE);
	}
	InvalidateRect(m_gridRc, FALSE);
}

static int TonePortLive(int part)
{
	if (part < 1 || part > 32) return 0;
	if (VstLivePartIsLoaded(part)) return 1;
	const int b0 = ((part - 1) / 16) * 16 + 1;
	for (int p = b0; p < b0 + 16 && p <= 32; ++p)
		if (VstLivePartIsLoaded(p)) return 1;
	return 0;
}

int CSasamiToneMapDlg::ToneIsEmpty(int pc) const
{
	if (pc < 0 || pc > 127) return 1;
	return (m_names[pc][0] == 0 || wcscmp(m_names[pc], L"------") == 0) ? 1 : 0;
}

void CSasamiToneMapDlg::ApplyTone(int pc, int audition)
{
	if (pc < 0 || pc > 127) return;
	if (ToneIsEmpty(pc)) return;
	m_prog = pc;
	/* UI only while picking. Live bank/PC is applied on preview / score play —
	   SendBankProgram on every click (and on 入力) froze Host32 return path. */
	CString h;
	h.Format(L"PC#%d  %s  (Bank %d/%d)", m_prog + 1, m_names[m_prog], m_bankMsb, m_bankLsb);
	m_hint.SetWindowText(h);
	InvalidateRect(m_gridRc, FALSE);
	const int hear = TonePortLive(m_part);
	if (audition && hear) {
		/* Still send bank+PC once for audition hearing, then note — SHM only. */
		VstLiveSendBankProgram(m_part, m_bankMsb, m_bankLsb, m_prog);
		VstLiveAuditionNote(m_part, m_isDrum ? 36 : 60, 100, 280);
	} else if (audition && (ScStaffScorePreviewOut() == SC_OUT_FMMIDI || ScStaffScorePreviewOut() == SC_OUT_MAPPER)) {
		ScStaffAuditionRouteNote(m_part, m_bankMsb, m_bankLsb, m_prog, m_isDrum ? 36 : 60, 100);
	}
}

int CSasamiToneMapDlg::HitTone(CPoint pt) const
{
	if (!m_gridRc.PtInRect(pt)) return -1;
	const int cols = 4;
	const int rows = 32;
	const int cellW = max(8, m_gridRc.Width() / cols);
	const int cellH = m_cellH > 0 ? m_cellH : 18;
	const int y = pt.y - m_gridRc.top + m_scroll;
	const int x = pt.x - m_gridRc.left;
	const int col = x / cellW;
	const int row = y / cellH;
	if (col < 0 || col >= cols || row < 0 || row >= rows) return -1;
	const int pc = row * cols + col;
	return (pc >= 0 && pc < 128) ? pc : -1;
}

void CSasamiToneMapDlg::OnPaint()
{
	CPaintDC dc(this);
	CCC_CaptionPaint(dc, m_hWnd);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	CRect rc; GetClientRect(&rc);
	CRect body(0, cap, rc.right, rc.bottom);
	CCC_FillRectAlpha(dc.GetSafeHdc(), body, RGB(236, 237, 242), 255);
	if (m_gridRc.Width() < 8) return;
	const int cols = 4;
	const int cellW = max(8, m_gridRc.Width() / cols);
	const int cellH = m_cellH > 0 ? m_cellH : 18;
	dc.SetBkMode(TRANSPARENT);
	CFont* old = dc.SelectObject(CFont::FromHandle((HFONT)GetStockObject(DEFAULT_GUI_FONT)));
	dc.IntersectClipRect(m_gridRc);
	for (int pc = 0; pc < 128; ++pc) {
		const int col = pc % cols;
		const int row = pc / cols;
		CRect cell(
			m_gridRc.left + col * cellW,
			m_gridRc.top + row * cellH - m_scroll,
			m_gridRc.left + (col + 1) * cellW - 1,
			m_gridRc.top + (row + 1) * cellH - 1 - m_scroll);
		if (cell.bottom < m_gridRc.top || cell.top > m_gridRc.bottom) continue;
		const int empty = ToneIsEmpty(pc);
		const int sel = (!empty && pc == m_prog);
		dc.FillSolidRect(cell, sel ? RGB(80, 200, 220) : (empty ? RGB(230, 230, 234) : RGB(250, 250, 252)));
		dc.Draw3dRect(cell, RGB(180, 180, 190), RGB(180, 180, 190));
		CString s;
		s.Format(L"%d:%s", pc + 1, m_names[pc]);
		dc.SetTextColor(empty ? RGB(150, 150, 160) : RGB(20, 20, 30));
		CRect tr = cell; tr.DeflateRect(3, 1);
		dc.DrawText(s, tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	dc.SelectObject(old);
}

BOOL CSasamiToneMapDlg::OnEraseBkgnd(CDC*) { return TRUE; }

void CSasamiToneMapDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
	const int pc = HitTone(point);
	if (pc >= 0) {
		ApplyTone(pc, 0);
		if (TonePortLive(m_part)) {
			VstLiveSendBankProgram(m_part, m_bankMsb, m_bankLsb, m_prog);
			VstLiveAuditionNote(m_part, m_isDrum ? 36 : 60, 100, 60000);
			m_auditionHold = 1;
		} else if (ScStaffScorePreviewOut() == SC_OUT_FMMIDI || ScStaffScorePreviewOut() == SC_OUT_MAPPER) {
			ScStaffAuditionRouteNote(m_part, m_bankMsb, m_bankLsb, m_prog, m_isDrum ? 36 : 60, 100);
		}
	}
	CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
}

void CSasamiToneMapDlg::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_auditionHold) {
		VstLiveAuditionStop();
		m_auditionHold = 0;
	}
	CCustomBlurDialogExBase::OnLButtonUp(nFlags, point);
}

void CSasamiToneMapDlg::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	const int pc = HitTone(point);
	if (pc >= 0 && !ToneIsEmpty(pc)) {
		/* Select only — no live PC send (same freeze as 入力 ApplyTone). */
		m_prog = pc;
		VstLiveAuditionStop();
		ScSaveWndGeom(this, &savedata.sasamiToneMapX, &savedata.sasamiToneMapY,
			&savedata.sasamiToneMapW, &savedata.sasamiToneMapH);
		EndDialog(IDOK);
		return;
	}
	CCustomBlurDialogExBase::OnLButtonDblClk(nFlags, point);
}

void CSasamiToneMapDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	if (m_ok.GetSafeHwnd()) LayoutChrome();
}

BOOL CSasamiToneMapDlg::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	(void)nFlags; (void)pt;
	const int cellH = m_cellH > 0 ? m_cellH : 18;
	m_scroll -= (zDelta / WHEEL_DELTA) * (cellH * 3);
	const int maxScroll = max(0, 32 * cellH - m_gridRc.Height());
	if (m_scroll < 0) m_scroll = 0;
	if (m_scroll > maxScroll) m_scroll = maxScroll;
	InvalidateRect(m_gridRc, FALSE);
	return TRUE;
}

void CSasamiToneMapDlg::OnCbnSelchangeMap()
{
	m_mapSel = m_map.GetCurSel();
	if (m_mapSel < 0 || m_mapSel >= kMapCount) m_mapSel = 0;
	m_bankMsb = kMaps[m_mapSel].defMsb;
	m_bankLsb = kMaps[m_mapSel].defLsb;
	RebuildBankCombos();
	RebuildGrid();
}

void CSasamiToneMapDlg::OnCbnSelchangeBank()
{
	const int msb = m_bankM.GetCurSelPhysical();
	const int lsb = m_bankL.GetCurSelPhysical();
	if (msb >= 0 && m_bankM.GetCurSel() >= 0) m_bankMsb = msb;
	else m_bankM.SetCurSelPhysical(m_bankMsb);
	if (lsb >= 0 && m_bankL.GetCurSel() >= 0) m_bankLsb = lsb;
	else m_bankL.SetCurSelPhysical(m_bankLsb);
	/* Rebuild LSB availability for new MSB (XG). */
	const int keepL = m_bankLsb;
	RebuildBankCombos();
	m_bankLsb = keepL;
	m_bankL.SetCurSelPhysical(m_bankLsb & 127);
	if (m_bankL.GetCurSel() < 0) {
		m_bankLsb = kMaps[m_mapSel].defLsb;
		m_bankL.SetCurSelPhysical(m_bankLsb);
	}
	RebuildGrid();
}

void CSasamiToneMapDlg::OnBnClickedOk()
{
	ScSaveWndGeom(this, &savedata.sasamiToneMapX, &savedata.sasamiToneMapY,
		&savedata.sasamiToneMapW, &savedata.sasamiToneMapH);
	/* Cut audition only — do NOT SendBankProgram here (Host32/SHM race freezes
	   on 入力). PickForPart writes bind; score/MML update happens after DoModal. */
	VstLiveAuditionStop();
	if (ToneIsEmpty(m_prog)) {
		for (int pc = 0; pc < 128; ++pc) {
			if (!ToneIsEmpty(pc)) { m_prog = pc; break; }
		}
	}
	EndDialog(IDOK);
}
void CSasamiToneMapDlg::OnBnClickedCancel() {
	ScSaveWndGeom(this, &savedata.sasamiToneMapX, &savedata.sasamiToneMapY,
		&savedata.sasamiToneMapW, &savedata.sasamiToneMapH);
	VstLiveAuditionStop();
	EndDialog(IDCANCEL);
}
void CSasamiToneMapDlg::OnBnClickedEditor()
{
	/* Do not call ProgramCount here — Host32 PROGRAMS IPC freezes after preview. */
	VstLiveAuditionStop();
	VstLiveEditorOpenAsync(m_part);
	if (m_hint.GetSafeHwnd())
		m_hint.SetWindowText(LL14(
			L"エディタ内 MediaBay で音色選択 → 閉じて保存（一覧APIは使いません）",
			L"Pick a sound in the editor's MediaBay, then close to save (the list API is not used).",
			L"Choisissez un son dans le MediaBay de l'éditeur, puis fermez pour enregistrer (pas d'API de liste).",
			L"Scegli un suono nel MediaBay dell'editor, poi chiudi per salvare (niente API elenco).",
			L"Elija un sonido en el MediaBay del editor y cierre para guardar (sin API de lista).",
			L"편집기 MediaBay에서 음색을 고른 뒤 닫으면 저장됩니다(목록 API는 쓰지 않습니다).",
			L"在编辑器的 MediaBay 里选音色，关闭即保存（不用列表 API）。",
			L"اختر صوتاً في MediaBay داخل المحرر ثم أغلق للحفظ (بدون واجهة القائمة).",
			L"Выберите тембр в MediaBay редактора и закройте, чтобы сохранить (API списка не используется).",
			L"Klang im MediaBay des Editors wählen, dann schließen zum Speichern (keine Listen-API).",
			L"Escolha um som no MediaBay do editor e feche para gravar (sem API de lista).",
			L"Kies een klank in de MediaBay van de editor en sluit om op te slaan (geen lijst-API).",
			L"Wybierz brzmienie w MediaBay edytora i zamknij, aby zapisać (bez API listy).",
			L"Düzenleyicinin MediaBay'inde sesi seçin, kapatınca kaydolur (liste API'si yok)."));
}

void CSasamiToneMapDlg::OnBnClickedVst3()
{
	/* Switch this part from GS/XG multi → dedicated VST3 (HALion / Groove Agent…). */
	VstLiveAuditionStop();
	if (!ScVstPickLoadForPart(this, m_part, m_bind))
		return;
	m_pickedVst3 = 1;
	if (m_bind) {
		/* Skip ProgramCurrent for multi/remote — same IPC freeze. */
		if (!VstLivePartIsMulti(m_part))
			m_bind->vstProg[m_part - 1] = VstLiveProgramCurrent(m_part);
		m_bind->isMpw3 = 1;
	}
	/* Do NOT open editor here — ScVstAssignToneForPart opens once after this
	   dialog closes. Opening here + MessageBox raced Home-dismiss / dual open. */
	EndDialog(IDOK);
}

void CSasamiToneMapDlg::OnCancel()
{
	VstLiveAuditionStop();
	ScSaveWndGeom(this, &savedata.sasamiToneMapX, &savedata.sasamiToneMapY,
		&savedata.sasamiToneMapW, &savedata.sasamiToneMapH);
	CCustomBlurDialogExBase::OnCancel();
}

void CSasamiToneMapDlg::OnDestroy()
{
	VstLiveAuditionStop();
	CCustomBlurDialogExBase::OnDestroy();
}
