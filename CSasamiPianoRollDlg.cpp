#include "stdafx.h"
#include "ogg.h"
#include "CSasamiPianoRollDlg.h"
#include "resource.h"

CSasamiPianoRollDlg* CSasamiPianoRollDlg::s_inst = NULL;

IMPLEMENT_DYNAMIC(CSasamiPianoRollDlg, CCustomBlurDialogExBase)

CSasamiPianoRollDlg::CSasamiPianoRollDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(IDD_SASAMI_PIANO_ROLL, pParent)
	, m_ev(NULL), m_evCount(NULL), m_ui(NULL), m_curPart(NULL), m_isFm(0)
{
	ScPianoRollInit(&m_roll);
}

CSasamiPianoRollDlg* CSasamiPianoRollDlg::Instance()
{
	return (s_inst && ::IsWindow(s_inst->GetSafeHwnd())) ? s_inst : NULL;
}

void CSasamiPianoRollDlg::OpenOwned(CWnd* owner, ScEvent* ev, int* evCount, ScStaffUi* ui, int* curPart, int isFm)
{
	if (s_inst && ::IsWindow(s_inst->GetSafeHwnd())) {
		s_inst->Bind(ev, evCount, ui, curPart, isFm);
		s_inst->EnableAero(FALSE);
		s_inst->ShowWindow(SW_SHOW);
		s_inst->SetForegroundWindow();
		s_inst->Refresh();
		return;
	}
	s_inst = new CSasamiPianoRollDlg(owner);
	s_inst->Bind(ev, evCount, ui, curPart, isFm);
	if (!s_inst->Create(IDD_SASAMI_PIANO_ROLL, owner)) {
		delete s_inst;
		s_inst = NULL;
		return;
	}
	s_inst->EnableAero(FALSE);
	s_inst->ShowWindow(SW_SHOW);
}

void CSasamiPianoRollDlg::Bind(ScEvent* ev, int* evCount, ScStaffUi* ui, int* curPart, int isFm)
{
	m_ev = ev; m_evCount = evCount; m_ui = ui; m_curPart = curPart; m_isFm = isFm;
}

void CSasamiPianoRollDlg::Refresh()
{
	if (::IsWindow(m_hWnd))
		Invalidate(FALSE);
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
	ON_WM_MOUSEWHEEL()
	ON_WM_TIMER()
END_MESSAGE_MAP()

BOOL CSasamiPianoRollDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	/* Body stays opaque (EnableAero FALSE). Paint path uses CCC_BlitStretchOpaque like FM monitor. */
	EnableAero(FALSE);
	s_inst = this;
	SetWindowText(LL14(L"ピアノロール", L"Piano Roll", L"Piano roll", L"Piano roll", L"Piano roll",
		L"피아노 롤", L"钢琴卷帘", L"رول البيانو", L"Пианоролл", L"Klavierrolle", L"Piano roll", L"Piano-roll", L"Rolka", L"Piyano rulosu"));
	CRect rc; GetClientRect(&rc);
	m_bodyRc = rc;
	SetTimer(1, 50, NULL);
	return TRUE;
}

void CSasamiPianoRollDlg::PostNcDestroy()
{
	if (s_inst == this) s_inst = NULL;
	CCustomBlurDialogExBase::PostNcDestroy();
	delete this;
}

void CSasamiPianoRollDlg::OnClose()
{
	DestroyWindow();
}

void CSasamiPianoRollDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	GetClientRect(&m_bodyRc);
	Invalidate(FALSE);
}

BOOL CSasamiPianoRollDlg::OnEraseBkgnd(CDC* pDC)
{
	if (!pDC) return TRUE;
	CRect rc;
	GetClientRect(&rc);
#if CCUSTOM_AERO_SUPPORT
	CCC_FillRectAlpha(pDC->GetSafeHdc(), rc, RGB(32, 34, 40), 255);
#else
	pDC->FillSolidRect(&rc, RGB(32, 34, 40));
#endif
	return TRUE;
}

void CSasamiPianoRollDlg::OnPaint()
{
	CPaintDC dc(this);
	CRect rc;
	GetClientRect(&rc);
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	CRect body(0, capH, rc.right, rc.bottom);
	m_bodyRc = body;
	if (body.Width() >= 8 && body.Height() >= 8) {
		if (m_ev && m_evCount && m_ui && m_curPart)
			ScPianoRollPaint(dc, body, &m_roll, m_ev, *m_evCount, m_ui, *m_curPart);
		else {
#if CCUSTOM_AERO_SUPPORT
			CCC_FillRectAlpha(dc.GetSafeHdc(), body, RGB(32, 34, 40), 255);
#else
			dc.FillSolidRect(body, RGB(32, 34, 40));
#endif
		}
	}
	CCC_CaptionPaintGdi(dc, m_hWnd);
}

void CSasamiPianoRollDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
	CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
	if (!m_ev || !m_evCount || !m_ui || !m_curPart) return;
	if (point.y < m_bodyRc.top) return;
	const int hit = ScPianoRollHitNote(&m_roll, m_bodyRc, m_ev, *m_evCount, m_ui, *m_curPart, point);
	if (hit >= 0) {
		ScStaffSelClear(m_ui);
		ScStaffSelAdd(m_ui, hit);
		m_ui->markerTick = m_ev[hit].tick;
		Invalidate(FALSE);
		if (GetParent()) GetParent()->Invalidate(FALSE);
	} else {
		m_ui->markerTick = ScPianoRollXToTick(&m_roll, m_bodyRc, m_ui, point.x);
		Invalidate(FALSE);
	}
	(void)nFlags;
}

BOOL CSasamiPianoRollDlg::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	m_roll.scrollY -= (zDelta / 30) * m_roll.rowH;
	if (m_roll.scrollY < 0) m_roll.scrollY = 0;
	Invalidate(FALSE);
	return TRUE;
}

void CSasamiPianoRollDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == 1 && m_ui && m_ui->previewActive)
		Invalidate(FALSE);
	CCustomBlurDialogExBase::OnTimer(nIDEvent);
}
