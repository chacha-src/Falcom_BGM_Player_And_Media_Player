#include "stdafx.h"
#include "ogg.h"
#include "CSasamiInsertFxDlg.h"
#include "CSasamiVstPickDlg.h"
#include "VstMidiEngine.h"

CSasamiInsertFxDlg* CSasamiInsertFxDlg::s_inst = NULL;

IMPLEMENT_DYNAMIC(CSasamiInsertFxDlg, CCustomBlurDialogExBase)

CSasamiInsertFxDlg::CSasamiInsertFxDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(IDD_SASAMI_INSERT_FX, pParent)
	, m_doc(NULL), m_part(1), m_notify(NULL)
	, m_dragKnob(-1), m_dragStartY(0), m_dragStartVal(0.0f), m_paramPage(0)
{
	for (int i = 0; i < 12; ++i) m_knobRc[i].SetRectEmpty();
}

CSasamiInsertFxDlg* CSasamiInsertFxDlg::OpenOwned(CWnd* owner, ScMidiDoc* doc, int part1to32)
{
	if (!doc) return NULL;
	if (part1to32 < 1) part1to32 = 1;
	if (part1to32 > 32) part1to32 = 32;
	if (s_inst && ::IsWindow(s_inst->GetSafeHwnd())) {
		s_inst->m_doc = doc;
		s_inst->m_part = part1to32;
		s_inst->m_notify = owner ? owner->GetSafeHwnd() : NULL;
		s_inst->m_paramPage = 0;
		s_inst->RefreshHint();
		s_inst->ShowWindow(SW_SHOW);
		s_inst->BringWindowToTop();
		return s_inst;
	}
	s_inst = new CSasamiInsertFxDlg(owner);
	s_inst->m_doc = doc;
	s_inst->m_part = part1to32;
	s_inst->m_notify = owner ? owner->GetSafeHwnd() : NULL;
	CWnd* parent = AfxGetMainWnd();
	if (!parent) parent = owner;
	if (!s_inst->Create(IDD_SASAMI_INSERT_FX, parent)) {
		delete s_inst;
		s_inst = NULL;
		return NULL;
	}
	s_inst->ShowWindow(SW_SHOW);
	return s_inst;
}

void CSasamiInsertFxDlg::CloseOpen(void)
{
	if (s_inst && ::IsWindow(s_inst->GetSafeHwnd()))
		s_inst->DestroyWindow();
}

void CSasamiInsertFxDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SASAMI_FX_SLOT, m_slot);
	DDX_Control(pDX, IDC_SASAMI_FX_PICK, m_pick);
	DDX_Control(pDX, IDC_SASAMI_FX_BYPASS, m_bypass);
	DDX_Control(pDX, IDC_SASAMI_FX_EDITOR, m_editor);
	DDX_Control(pDX, IDC_SASAMI_FX_CLOSE, m_close);
	DDX_Control(pDX, IDC_SASAMI_FX_HINT, m_hint);
}

BEGIN_MESSAGE_MAP(CSasamiInsertFxDlg, CCustomBlurDialogExBase)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSEWHEEL()
	ON_WM_CLOSE()
	ON_CBN_SELCHANGE(IDC_SASAMI_FX_SLOT, &CSasamiInsertFxDlg::OnCbnSlot)
	ON_BN_CLICKED(IDC_SASAMI_FX_PICK, &CSasamiInsertFxDlg::OnBnClickedPick)
	ON_BN_CLICKED(IDC_SASAMI_FX_BYPASS, &CSasamiInsertFxDlg::OnBnClickedBypass)
	ON_BN_CLICKED(IDC_SASAMI_FX_EDITOR, &CSasamiInsertFxDlg::OnBnClickedEditor)
	ON_BN_CLICKED(IDC_SASAMI_FX_CLOSE, &CSasamiInsertFxDlg::OnBnClickedClose)
END_MESSAGE_MAP()

void CSasamiInsertFxDlg::ApplyLang()
{
	SetWindowText(L"SASAMI Insert FX (VST effect)");
	m_pick.SetWindowText(L"Pick...");
	m_bypass.SetWindowText(L"Bypass");
	m_editor.SetWindowText(L"Editor");
	m_close.SetWindowText(L"Close");
}

BOOL CSasamiInsertFxDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	s_inst = this;
	m_slot.SetAeroMode(FALSE);
	m_pick.SetAeroMode(FALSE); m_pick.SetFlat(TRUE);
	m_bypass.SetAeroMode(FALSE); m_bypass.SetFlat(TRUE);
	m_editor.SetAeroMode(FALSE); m_editor.SetFlat(TRUE);
	m_close.SetAeroMode(FALSE); m_close.SetFlat(TRUE);
	m_hint.SetAeroMode(FALSE);
	m_slot.ResetContent();
	m_slot.AddString(L"FX 1");
	m_slot.AddString(L"FX 2");
	m_slot.SetCurSel(0);
	m_paramPage = 0;
	ApplyLang();
	if (!ScRestoreWndGeom(this, savedata.sasamiVstPickX, savedata.sasamiVstPickY,
		savedata.sasamiVstPickW, savedata.sasamiVstPickH, 520, 320))
		SetWindowPos(NULL, 0, 0, 640, 420, SWP_NOMOVE | SWP_NOZORDER);
	LayoutChrome();
	RefreshHint();
	return TRUE;
}

int CSasamiInsertFxDlg::CurSlot() const
{
	int s = m_slot.GetCurSel();
	if (s < 0 || s > 1) s = 0;
	return s;
}

int CSasamiInsertFxDlg::KnobParamIndex(int knobI) const
{
	return m_paramPage * 12 + knobI;
}

void CSasamiInsertFxDlg::LayoutChrome()
{
	if (!::IsWindow(m_hWnd)) return;
	CRect rc; GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	const int pad = 10;
	int y = cap + pad;
	int x = pad;
	if (m_slot.GetSafeHwnd()) { m_slot.MoveWindow(x, y, 72, 90); x += 82; }
	if (m_pick.GetSafeHwnd()) { m_pick.MoveWindow(x, y, 76, 28); x += 84; }
	if (m_bypass.GetSafeHwnd()) { m_bypass.MoveWindow(x, y, 78, 28); x += 86; }
	if (m_editor.GetSafeHwnd()) { m_editor.MoveWindow(x, y, 78, 28); x += 86; }
	if (m_close.GetSafeHwnd()) m_close.MoveWindow(x, y, 72, 28);
	y += 36;
	if (m_hint.GetSafeHwnd()) m_hint.MoveWindow(pad, y, rc.Width() - pad * 2, 48);
	y += 58;
	m_bodyRc.SetRect(pad, y, rc.Width() - pad, rc.Height() - pad);
	const int cols = 4;
	const int rows = 3;
	const int cellW = max(70, m_bodyRc.Width() / cols);
	const int cellH = max(72, m_bodyRc.Height() / rows);
	for (int i = 0; i < 12; ++i) {
		int c = i % cols;
		int r = i / cols;
		m_knobRc[i].SetRect(m_bodyRc.left + c * cellW, m_bodyRc.top + r * cellH,
			min(m_bodyRc.right, m_bodyRc.left + (c + 1) * cellW - 4),
			min(m_bodyRc.bottom, m_bodyRc.top + (r + 1) * cellH - 4));
	}
}

void CSasamiInsertFxDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	if (m_close.GetSafeHwnd()) LayoutChrome();
}

BOOL CSasamiInsertFxDlg::OnEraseBkgnd(CDC* pDC)
{
	if (!pDC) return TRUE;
	CRect rc; GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	pDC->FillSolidRect(0, cap, rc.Width(), rc.Height() - cap, RGB(28, 31, 38));
	return TRUE;
}

static void FxDrawKnob(CDC& dc, const CRect& rc, float val, const wchar_t* name, const wchar_t* disp)
{
	if (val < 0.0f) val = 0.0f;
	if (val > 1.0f) val = 1.0f;
	const int rad = max(9, min(rc.Width(), rc.Height() - 26) / 2 - 3);
	CPoint c(rc.CenterPoint().x, rc.top + rad + 5);
	CBrush br(RGB(42, 51, 66));
	CPen rim(PS_SOLID, 1, RGB(120, 155, 220));
	CBrush* ob = dc.SelectObject(&br);
	CPen* op = dc.SelectObject(&rim);
	dc.Ellipse(c.x - rad, c.y - rad, c.x + rad + 1, c.y + rad + 1);
	const double a = 3.1415926535 * (0.75 + 1.5 * (double)val);
	CPen needle(PS_SOLID, 2, RGB(245, 210, 100));
	dc.SelectObject(&needle);
	dc.MoveTo(c);
	dc.LineTo(c.x + (int)(cos(a) * (rad - 3)), c.y - (int)(sin(a) * (rad - 3)));
	dc.SelectObject(op);
	dc.SelectObject(ob);
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(RGB(225, 232, 245));
	CString s;
	s.Format(L"%s %s", name ? name : L"", disp ? disp : L"");
	dc.DrawText(s, CRect(rc.left, c.y + rad + 2, rc.right, rc.bottom), DT_CENTER | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
}

void CSasamiInsertFxDlg::OnPaint()
{
	CPaintDC dc(this);
	CRect rc; GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	dc.FillSolidRect(0, cap, rc.Width(), rc.Height() - cap, RGB(28, 31, 38));
	CFont* old = dc.SelectObject(GetFont());
	const int slot = CurSlot();
	const int total = VstLiveFxParamCount(m_part, slot);
	int shown = 0;
	for (int i = 0; i < 12; ++i) {
		const int pi = KnobParamIndex(i);
		if (pi >= total) break;
		wchar_t name[64] = {};
		wchar_t disp[64] = {};
		VstLiveFxParamName(m_part, slot, pi, name, 64);
		VstLiveFxParamDisplay(m_part, slot, pi, disp, 64);
		FxDrawKnob(dc, m_knobRc[i], VstLiveFxGetParam(m_part, slot, pi), name, disp);
		++shown;
	}
	if (shown <= 0 && m_bodyRc.Width() > 0) {
		dc.SetBkMode(TRANSPARENT);
		dc.SetTextColor(RGB(210, 218, 232));
		dc.DrawText(L"Pick an insert effect (effects only). Wheel = next/prev param page.",
			m_bodyRc, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
	}
	dc.SelectObject(old);
	CCC_CaptionPaint(dc, m_hWnd);
}

void CSasamiInsertFxDlg::RefreshHint()
{
	if (!m_hint.GetSafeHwnd()) return;
	const int slot = CurSlot();
	wchar_t path[520] = {};
	VstLiveFxGetPath(m_part, slot, path, 520);
	const wchar_t* base = path;
	if (const wchar_t* p = wcsrchr(path, L'\\')) base = p + 1;
	const int total = VstLiveFxParamCount(m_part, slot);
	const int from = total > 0 ? m_paramPage * 12 + 1 : 0;
	int to = m_paramPage * 12 + 12;
	if (to > total) to = total;
	CString s;
	s.Format(L"MIDI part %d, FX slot %d: %s%s — params %d–%d / %d (wheel = page)",
		m_part, slot + 1, path[0] ? base : L"(empty)",
		VstLiveFxGetBypass(m_part, slot) ? L" [bypassed]" : L"",
		from, to, total);
	m_hint.SetWindowText(s);
	InvalidateRect(m_bodyRc, FALSE);
}

void CSasamiInsertFxDlg::RefreshKnobs()
{
	InvalidateRect(m_bodyRc, FALSE);
}

void CSasamiInsertFxDlg::NotifyChanged()
{
	if (m_notify && ::IsWindow(m_notify))
		::PostMessage(m_notify, WM_SASAMI_INSERT_FX_CHANGED, (WPARAM)m_part, 0);
}

void CSasamiInsertFxDlg::CaptureStateToDoc()
{
	if (!m_doc) return;
	for (int sl = 0; sl < ScMidiFxBind::SC_FX_SLOTS; ++sl) {
		unsigned char* blob = NULL;
		int len = 0;
		if (VstLiveFxCaptureState(m_part, sl, &blob, &len) && blob && len > 0) {
			ScMidiFxBindSetState(&m_doc->fxBind, m_part - 1, sl, blob, (uint32_t)len);
			free(blob);
			m_doc->bind.isMpw3 = 1;
		}
	}
}

void CSasamiInsertFxDlg::OnCbnSlot()
{
	m_paramPage = 0;
	RefreshHint();
}

void CSasamiInsertFxDlg::OnBnClickedPick()
{
	wchar_t path[520] = {};
	int is3 = 0;
	if (CSasamiVstPickDlg::PickEffect(this, path, 520, &is3) != IDOK || !path[0]) {
		/* Fallback: raw file dialog if scan has no effects yet */
		CFileDialog dlg(TRUE, NULL, NULL, OFN_FILEMUSTEXIST,
			L"VST Effects (*.dll;*.vst3)|*.dll;*.vst3|All files (*.*)|*.*||", this);
		if (dlg.DoModal() != IDOK) return;
		wcsncpy_s(path, dlg.GetPathName(), _TRUNCATE);
		const size_t n = wcslen(path);
		is3 = (n >= 5 && _wcsicmp(path + n - 5, L".vst3") == 0) ? 1 : 0;
	}
	const int slot = CurSlot();
	if (VstLiveLoadFx(m_part, slot, path, is3) == 0) {
		wcsncpy_s(m_doc->fxBind.fxPath[m_part - 1][slot], path, _TRUNCATE);
		m_doc->fxBind.fxBypass[m_part - 1][slot] = 0;
		m_doc->bind.isMpw3 = 1;
		if (m_doc->fxBind.fxStateLen[m_part - 1][slot] && m_doc->fxBind.fxState[m_part - 1][slot])
			VstLiveFxApplyState(m_part, slot, m_doc->fxBind.fxState[m_part - 1][slot],
				(int)m_doc->fxBind.fxStateLen[m_part - 1][slot]);
		m_paramPage = 0;
		NotifyChanged();
		RefreshHint();
	} else {
		m_hint.SetWindowText(L"Could not load FX plug-in.");
	}
}

void CSasamiInsertFxDlg::OnBnClickedBypass()
{
	const int slot = CurSlot();
	const int bypass = !VstLiveFxGetBypass(m_part, slot);
	VstLiveFxSetBypass(m_part, slot, bypass);
	m_doc->fxBind.fxBypass[m_part - 1][slot] = bypass;
	NotifyChanged();
	RefreshHint();
}

void CSasamiInsertFxDlg::OnBnClickedEditor()
{
	const int rc = VstLiveFxEditorOpen(m_part, CurSlot());
	if (rc != 0)
		m_hint.SetWindowText(L"FX editor is not available for this slot.");
}

void CSasamiInsertFxDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
	for (int i = 0; i < 12; ++i) {
		if (m_knobRc[i].PtInRect(point)) {
			const int pi = KnobParamIndex(i);
			if (pi >= VstLiveFxParamCount(m_part, CurSlot())) break;
			m_dragKnob = i;
			m_dragStartY = point.y;
			m_dragStartVal = VstLiveFxGetParam(m_part, CurSlot(), pi);
			SetCapture();
			return;
		}
	}
	CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
}

void CSasamiInsertFxDlg::OnMouseMove(UINT nFlags, CPoint point)
{
	if ((nFlags & MK_LBUTTON) && m_dragKnob >= 0) {
		float v = m_dragStartVal + (float)(m_dragStartY - point.y) / 140.0f;
		if (v < 0.0f) v = 0.0f;
		if (v > 1.0f) v = 1.0f;
		VstLiveFxSetParam(m_part, CurSlot(), KnobParamIndex(m_dragKnob), v);
		RefreshKnobs();
		return;
	}
	CCustomBlurDialogExBase::OnMouseMove(nFlags, point);
}

void CSasamiInsertFxDlg::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_dragKnob >= 0) {
		m_dragKnob = -1;
		ReleaseCapture();
		CaptureStateToDoc();
		NotifyChanged();
		RefreshHint();
		return;
	}
	CCustomBlurDialogExBase::OnLButtonUp(nFlags, point);
}

BOOL CSasamiInsertFxDlg::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	UNREFERENCED_PARAMETER(nFlags);
	UNREFERENCED_PARAMETER(pt);
	const int total = VstLiveFxParamCount(m_part, CurSlot());
	const int pages = total > 0 ? (total + 11) / 12 : 1;
	if (zDelta < 0) ++m_paramPage;
	else if (zDelta > 0) --m_paramPage;
	if (m_paramPage < 0) m_paramPage = 0;
	if (m_paramPage >= pages) m_paramPage = pages - 1;
	RefreshHint();
	return TRUE;
}

void CSasamiInsertFxDlg::OnBnClickedClose()
{
	CaptureStateToDoc();
	NotifyChanged();
	DestroyWindow();
}
void CSasamiInsertFxDlg::OnClose()
{
	CaptureStateToDoc();
	NotifyChanged();
	DestroyWindow();
}
void CSasamiInsertFxDlg::PostNcDestroy()
{
	ScSaveWndGeom(this, &savedata.sasamiVstPickX, &savedata.sasamiVstPickY,
		&savedata.sasamiVstPickW, &savedata.sasamiVstPickH);
	if (s_inst == this) s_inst = NULL;
	CCustomBlurDialogExBase::PostNcDestroy();
	delete this;
}
