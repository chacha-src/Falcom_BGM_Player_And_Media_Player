#include "stdafx.h"
#include "ogg.h"
#include "CSasamiToneMapDlg.h"
#include "CSasamiVstPartMenu.h"
#include "SasamiToneNames.h"
#include "VstMidiEngine.h"

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
	, m_pickedVst3(0), m_bind(NULL), m_scroll(0), m_cellH(18), m_auditionHold(0)
{
	memset(m_names, 0, sizeof(m_names));
}

int CSasamiToneMapDlg::PickForPart(CWnd* owner, int part1to32, ScMidiVstBind* bind)
{
	CSasamiToneMapDlg dlg(owner);
	dlg.m_part = part1to32;
	dlg.m_bind = bind;
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
	ON_CBN_SELCHANGE(IDC_SASAMI_TM_MAP, &CSasamiToneMapDlg::OnCbnSelchangeMap)
	ON_CBN_SELCHANGE(IDC_SASAMI_TM_BANKM, &CSasamiToneMapDlg::OnCbnSelchangeBank)
	ON_CBN_SELCHANGE(IDC_SASAMI_TM_BANKL, &CSasamiToneMapDlg::OnCbnSelchangeBank)
	ON_BN_CLICKED(IDC_SASAMI_TM_OK, &CSasamiToneMapDlg::OnBnClickedOk)
	ON_BN_CLICKED(IDC_SASAMI_TM_CANCEL, &CSasamiToneMapDlg::OnBnClickedCancel)
	ON_BN_CLICKED(IDC_SASAMI_TM_EDITOR, &CSasamiToneMapDlg::OnBnClickedEditor)
	ON_BN_CLICKED(IDC_SASAMI_TM_VST, &CSasamiToneMapDlg::OnBnClickedVst3)
	ON_WM_DESTROY()
END_MESSAGE_MAP()

BOOL CSasamiToneMapDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	SetWindowText(L"トーンマップ");
	m_ok.SetWindowText(L"入力");
	m_cancel.SetWindowText(L"キャンセル");
	m_editor.SetWindowText(L"設定画面");
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
	h.Format(L"パート %d — クリック試聴 / ダブルクリック決定。VST3…で専用音源へ", m_part);
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
	if (m_vst3.GetSafeHwnd()) m_vst3.MoveWindow(x, top + lblH, 64, 22);
	x += 70;
	if (m_editor.GetSafeHwnd()) m_editor.MoveWindow(x, top + lblH, 72, 22);
	if (m_ok.GetSafeHwnd()) m_ok.MoveWindow(rc.right - 160, top + lblH, 70, 22);
	if (m_cancel.GetSafeHwnd()) m_cancel.MoveWindow(rc.right - 84, top + lblH, 76, 22);
	const int hintY = top + lblH + comboH + 4;
	if (m_hint.GetSafeHwnd()) m_hint.MoveWindow(8, hintY, rc.Width() - 16, 18);
	m_gridRc.SetRect(8, hintY + 22, rc.right - 8, rc.bottom - 8);
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
	const int isDrum = (m_part == 10) ? 1 : 0;
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
	const int isDrum = (m_part == 10) ? 1 : 0;
	for (int pc = 0; pc < 128; ++pc) {
		m_names[pc][0] = 0;
		SasamiToneLookupStrict(m.isXg, m.mapId, m_bankMsb, m_bankLsb, pc, isDrum,
			m_names[pc], 40);
		if (!m_names[pc][0])
			wcsncpy_s(m_names[pc], L"------", _TRUNCATE);
	}
	InvalidateRect(m_gridRc, FALSE);
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
	   SendBankProgram on every click (and on 入力) froze Host64 return path. */
	CString h;
	h.Format(L"PC#%d  %s  (Bank %d/%d)", m_prog + 1, m_names[m_prog], m_bankMsb, m_bankLsb);
	m_hint.SetWindowText(h);
	InvalidateRect(m_gridRc, FALSE);
	if (audition && VstLivePartIsLoaded(m_part)) {
		/* Still send bank+PC once for audition hearing, then note — SHM only. */
		VstLiveSendBankProgram(m_part, m_bankMsb, m_bankLsb, m_prog);
		VstLiveAuditionNote(m_part, (m_part == 10 || m_part == 26) ? 36 : 60, 100, 280);
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
		if (VstLivePartIsLoaded(m_part)) {
			VstLiveSendBankProgram(m_part, m_bankMsb, m_bankLsb, m_prog);
			VstLiveAuditionNote(m_part, (m_part == 10 || m_part == 26) ? 36 : 60, 100, 60000);
			m_auditionHold = 1;
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
	/* Cut audition only — do NOT SendBankProgram here (Host64/SHM race freezes
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
	/* Do not call ProgramCount here — Host64 PROGRAMS IPC freezes after preview. */
	VstLiveAuditionStop();
	VstLiveEditorOpenAsync(m_part);
	if (m_hint.GetSafeHwnd())
		m_hint.SetWindowText(
			L"エディタ内 MediaBay で音色選択 → 閉じて保存（一覧APIは使いません）");
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
