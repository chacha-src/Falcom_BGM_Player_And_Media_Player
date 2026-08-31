#include "stdafx.h"
#include "ogg.h"
#include "CSasamiVstPickDlg.h"
#include "VstMidiEngine.h"

IMPLEMENT_DYNAMIC(CSasamiVstPickDlg, CCustomBlurDialogExBase)

CSasamiVstPickDlg::CSasamiVstPickDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(IDD_SASAMI_VST_PICK, pParent)
	, m_isVst3(0), m_part(1), m_scroll(0), m_sel(0), m_count(0)
{
	m_path[0] = 0;
}

int CSasamiVstPickDlg::PickForPart(CWnd* owner, int part1to32, wchar_t* outPath, int outCch, int* outIs3)
{
	/* Always show the instrument list. Opening HALion/MediaBay editor here hid the
	   picker behind the score and forced MediaBay dialogs the user did not ask for. */
	CSasamiVstPickDlg dlg(owner);
	dlg.m_part = part1to32;
	const INT_PTR r = dlg.DoModal();
	if (r != IDOK) return (int)r;
	if (outPath && outCch > 0)
		wcsncpy_s(outPath, outCch, dlg.m_path, _TRUNCATE);
	if (outIs3) *outIs3 = dlg.m_isVst3;
	return IDOK;
}

void CSasamiVstPickDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SASAMI_VST_OK, m_ok);
	DDX_Control(pDX, IDC_SASAMI_VST_CANCEL, m_cancel);
	DDX_Control(pDX, IDC_SASAMI_VST_RESCAN, m_rescan);
	DDX_Control(pDX, IDC_SASAMI_VST_HINT, m_hint);
}

BEGIN_MESSAGE_MAP(CSasamiVstPickDlg, CCustomBlurDialogExBase)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_SIZE()
	ON_WM_MOUSEWHEEL()
	ON_BN_CLICKED(IDC_SASAMI_VST_OK, &CSasamiVstPickDlg::OnBnClickedOk)
	ON_BN_CLICKED(IDC_SASAMI_VST_CANCEL, &CSasamiVstPickDlg::OnBnClickedCancel)
	ON_BN_CLICKED(IDC_SASAMI_VST_RESCAN, &CSasamiVstPickDlg::OnBnClickedRescan)
END_MESSAGE_MAP()

void CSasamiVstPickDlg::ApplyLang()
{
	SetWindowText(LL14(L"VST音色を選択", L"Select VST", L"Choisir VST", L"Scegli VST", L"Elegir VST",
		L"VST 선택", L"选择 VST", L"اختر VST", L"Выбор VST", L"VST wählen",
		L"Escolher VST", L"VST kiezen", L"Wybierz VST", L"VST seç"));
	m_ok.SetWindowText(L"OK");
	m_cancel.SetWindowText(LL14(L"取消", L"Cancel", L"Annuler", L"Annulla", L"Cancelar", L"취소", L"取消", L"إلغاء", L"Отмена", L"Abbrechen", L"Cancelar", L"Annuleren", L"Anuluj", L"İptal"));
	m_rescan.SetWindowText(LL14(L"再スキャン", L"Rescan", L"Rescan", L"Rescan", L"Rescan",
		L"재스캔", L"重新扫描", L"Rescan", L"Rescan", L"Rescan", L"Rescan", L"Rescan", L"Rescan", L"Rescan"));
	CString h;
	h.Format(LL14(
		L"パート %d — スキャン済み音源（ダブルクリックで決定）。HALion等の音色数0は正常（MediaBayは設定画面内）",
		L"Part %d — scanned instruments (double-click). HALion 0 programs is normal (MediaBay in editor)",
		L"Partie %d", L"Parte %d", L"Parte %d", L"파트 %d", L"声部 %d", L"الجزء %d", L"Партия %d", L"Part %d", L"Parte %d", L"Deel %d", L"Partia %d", L"Parti %d"), m_part);
	m_hint.SetWindowText(h);
}

void CSasamiVstPickDlg::RebuildList()
{
	m_count = 0;
	VstScanEnsure(m_hWnd);
	const int n = VstScanGetCount();
	for (int i = 0; i < n && m_count < 512; i++) {
		const VstPluginInfo* p = VstScanGet(i);
		if (!p) continue;
		if (!p->isInstrument) continue;
		if (p->isLiveOk == 0) continue; /* prefer verified */
		m_idx[m_count++] = i;
	}
	/* if none verified, show all instruments */
	if (m_count == 0) {
		for (int i = 0; i < n && m_count < 512; i++) {
			const VstPluginInfo* p = VstScanGet(i);
			if (!p || !p->isInstrument) continue;
			m_idx[m_count++] = i;
		}
	}
	if (m_sel >= m_count) m_sel = m_count - 1;
	if (m_sel < 0) m_sel = 0;
}

BOOL CSasamiVstPickDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	m_ok.SetAeroMode(FALSE);
	m_cancel.SetAeroMode(FALSE);
	m_rescan.SetAeroMode(FALSE);
	m_hint.SetAeroMode(FALSE);
	ApplyLang();
	RebuildList();
	LayoutChrome();
	CCC_BringDialogToForeground(this);
	if (!ScRestoreWndGeom(this, savedata.sasamiVstPickX, savedata.sasamiVstPickY,
		savedata.sasamiVstPickW, savedata.sasamiVstPickH, 420, 320))
		SetWindowPos(NULL, 0, 0, 520, 480, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	LayoutChrome();
	return TRUE;
}

void CSasamiVstPickDlg::LayoutChrome()
{
	CRect rc; GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	m_hint.MoveWindow(8, cap + 6, rc.Width() - 16, 36);
	m_rescan.MoveWindow(8, rc.Height() - 30, 72, 24);
	m_ok.MoveWindow(rc.Width() - 120, rc.Height() - 30, 50, 24);
	m_cancel.MoveWindow(rc.Width() - 64, rc.Height() - 30, 56, 24);
	m_listRc.SetRect(8, cap + 46, rc.Width() - 8, rc.Height() - 40);
	Invalidate(FALSE);
}

void CSasamiVstPickDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	if (m_ok.GetSafeHwnd()) LayoutChrome();
}

BOOL CSasamiVstPickDlg::OnEraseBkgnd(CDC* pDC) { return CCustomBlurDialogExBase::OnEraseBkgnd(pDC); }

int CSasamiVstPickDlg::HitRow(CPoint pt) const
{
	if (!m_listRc.PtInRect(pt)) return -1;
	const int rowH = 22;
	int r = (pt.y - m_listRc.top) / rowH + m_scroll;
	if (r < 0 || r >= m_count) return -1;
	return r;
}

void CSasamiVstPickDlg::OnPaint()
{
	CPaintDC dc(this);
	CRect rc; GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	CRect body(rc.left, cap, rc.right, rc.bottom);
	CDC mem; mem.CreateCompatibleDC(&dc);
	CBitmap bmp; bmp.CreateCompatibleBitmap(&dc, body.Width(), body.Height());
	CBitmap* old = mem.SelectObject(&bmp);
	mem.FillSolidRect(0, 0, body.Width(), body.Height(), RGB(248, 248, 252));
	CRect list = m_listRc; list.OffsetRect(-body.left, -body.top);
	mem.FillSolidRect(list, RGB(255, 255, 255));
	mem.Draw3dRect(list, RGB(160, 160, 170), RGB(160, 160, 170));
	mem.SetBkMode(TRANSPARENT);
	CFont* of = mem.SelectObject(GetFont());
	const int rowH = 22;
	int y = list.top + 2;
	for (int i = m_scroll; i < m_count && y + rowH < list.bottom; i++) {
		const VstPluginInfo* p = VstScanGet(m_idx[i]);
		if (!p) continue;
		CRect row(list.left + 2, y, list.right - 2, y + rowH);
		if (i == m_sel) mem.FillSolidRect(row, RGB(200, 215, 245));
		mem.SetTextColor(RGB(20, 20, 40));
		wchar_t line[600];
		_snwprintf_s(line, _TRUNCATE, L"%s  [%s %d]", p->name, p->isVst3 ? L"VST3" : L"VST2", p->arch);
		mem.DrawText(line, row, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		y += rowH;
	}
	mem.SelectObject(of);
	CCC_BlitStretchOpaque(dc.GetSafeHdc(), body.left, body.top, body.Width(), body.Height(),
		mem.GetSafeHdc(), 0, 0, body.Width(), body.Height());
	mem.SelectObject(old);
	CCC_CaptionPaint(dc, m_hWnd);
}

void CSasamiVstPickDlg::AcceptSel()
{
	if (m_sel < 0 || m_sel >= m_count) return;
	const VstPluginInfo* p = VstScanGet(m_idx[m_sel]);
	if (!p) return;
	wcsncpy_s(m_path, p->path, _TRUNCATE);
	m_isVst3 = p->isVst3;
	EndDialog(IDOK);
}

void CSasamiVstPickDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	if (capH > 0 && point.y >= 0 && point.y < capH) {
		CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
		return;
	}
	int r = HitRow(point);
	if (r >= 0) { m_sel = r; InvalidateRect(m_listRc, FALSE); }
	CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
}

void CSasamiVstPickDlg::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	if (HitRow(point) >= 0) AcceptSel();
	else CCustomBlurDialogExBase::OnLButtonDblClk(nFlags, point);
}

BOOL CSasamiVstPickDlg::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	m_scroll -= (zDelta / WHEEL_DELTA) * 3;
	if (m_scroll < 0) m_scroll = 0;
	if (m_scroll > m_count - 1) m_scroll = max(0, m_count - 1);
	InvalidateRect(m_listRc, FALSE);
	return TRUE;
}

void CSasamiVstPickDlg::OnBnClickedOk() {
	ScSaveWndGeom(this, &savedata.sasamiVstPickX, &savedata.sasamiVstPickY,
		&savedata.sasamiVstPickW, &savedata.sasamiVstPickH); AcceptSel(); }
void CSasamiVstPickDlg::OnBnClickedCancel() {
	ScSaveWndGeom(this, &savedata.sasamiVstPickX, &savedata.sasamiVstPickY,
		&savedata.sasamiVstPickW, &savedata.sasamiVstPickH); EndDialog(IDCANCEL); }

void CSasamiVstPickDlg::OnBnClickedRescan()
{
	/* Folder re-enumerate only. Do NOT call VstScanVerifyLiveList here — that
	   LoadPart-probes free slots and races a live HALion editor / MediaBay. */
	m_hint.SetWindowText(LL14(
		L"フォルダを再スキャン中…（検証ロードなし・再生中スロットは触らない）",
		L"Rescanning folders… (no verify-load; live slots untouched)",
		L"Rescan dossiers…", L"Rescan cartelle…", L"Reescaneando…",
		L"폴더 재스캔…", L"正在重新扫描…", L"Rescan…", L"Rescan…", L"Ordner neu…",
		L"A reexaminar…", L"Mappen opnieuw…", L"Ponowne skanowanie…", L"Yeniden tarama…"));
	VstScanInvalidate();
	VstScanEnsure(m_hWnd);
	RebuildList();
	ApplyLang();
	CString h;
	h.Format(LL14(L"再スキャン完了 — %d 件（HALionの音色0は正常）",
		L"Rescan done — %d instruments (HALion 0 programs is normal)",
		L"Rescan OK — %d", L"Rescan OK — %d", L"Rescan OK — %d",
		L"재스캔 완료 — %d", L"完成 — %d", L"%d", L"%d", L"Fertig — %d",
		L"OK — %d", L"Klaar — %d", L"Gotowe — %d", L"Bitti — %d"), m_count);
	m_hint.SetWindowText(h);
	InvalidateRect(m_listRc, FALSE);
}
