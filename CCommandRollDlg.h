#pragma once

#include "CCustomControl.h"
#include "CPromptEngine.h"
#include "GdiSoft3D.h"
#include "resource.h"

class CPromptDlg;

// ロール本体（スクロールバー・ホイール・描画をここへ集約。親のアクリルと分離）
class CCommandRollView : public CWnd
{
	DECLARE_DYNAMIC(CCommandRollView)
public:
	enum { kLaneCount = 27, kMaxEv = 2048, kHeaderH = 20 };
	enum LaneKind {
		LanePreset = 0,
		LaneN, LaneS, LaneI, LaneK, LaneM,
		LaneP, LaneT, LaneD,
		LaneR, LaneC, LaneY,
		LaneE, LaneF,
		LaneA, LaneB, LaneEe, LaneFf, LaneG, LaneH, LaneIi, LaneJ, LaneKk, LaneL, LaneMm, LaneNn, LaneO
	};

	CCommandRollView();
	BOOL CreateRoll(CWnd* pParent, const CRect& rc, UINT nId);

	BOOL IsPushing() const { return m_pushing; }
	void ReloadFromParse();
	void SetEventsFromEngine();
	void CommitToPeer();
	CString BuildPromptText();
	void SetPeer(CPromptDlg* peer) { m_peer = peer; }
	void SetSyncGen(UINT* pGen) { m_pSyncGen = pGen; }
	void TickPlayhead();
	void ZoomIn();
	void ZoomOut();
	double GetPxPerSec() const { return m_pxPerSec; }
	void SetPxPerSec(double px);
	void ClearAllEvents();

	MpPromptSnapshotEvent m_ev[kMaxEv];
	int m_evCount = 0;
	int m_sel = -1;

protected:
	CPromptDlg* m_peer = nullptr;
	UINT* m_pSyncGen = nullptr;
	BOOL m_pushing = FALSE;

	enum { kMinDurationSec = 1800 }; // 未演奏でも横スクロール可能にする下限(30分)

	double m_pxPerSec = 12.0;
	double m_scrollSec = 0.0;
	int m_scrollY = 0;
	double m_duration = (double)kMinDurationSec;
	int m_laneH = 36;
	int m_labelW = 72;
	BOOL m_followPlay = TRUE;
	DWORD m_userScrollTick = 0;
	double m_lastDrawnPlay = -1.0;

	BOOL m_dragging = FALSE;
	BOOL m_creating = FALSE;
	BOOL m_paletteDrag = FALSE;
	int m_dragLane = -1;
	double m_dragT0 = 0, m_dragT1 = 0;
	CPoint m_downPt{};

	CDC m_memDC;
	CBitmap m_memBmp;
	CBitmap* m_oldBmp = nullptr;
	int m_memW = 0, m_memH = 0;

	int LaneFromEvent(const MpPromptSnapshotEvent& ev) const;
	void LettersForLane(int lane, TCHAR& c1, TCHAR& c2, BOOL& preset) const;
	LPCTSTR LaneName(int lane) const;
	double XToSec(int x) const;
	int SecToX(double sec) const;
	int ContentWidthPx() const;
	int ContentHeightPx() const;
	void ClampScroll();
	void SyncScrollBars();
	void NoteUserScroll();
	int ValueBarHeight(const MpPromptSnapshotEvent& ev, int val, int maxH) const;
	void DrawEventBar(CDC& dc, int idx, COLORREF fill);
	CRect ClientRoll() const;
	CRect LaneRect(int lane) const;
	CRect EventRect(int idx) const;
	int HitTestEvent(CPoint pt) const;
	int HitTestLane(CPoint pt) const;
	int HitTestPalette(CPoint pt) const;
	void EnsureMemDC(int w, int h);
	void PaintRoll(CDC& dc, const CRect& rc);
	void PaintRollSoft3D(CDC& dc, const CRect& rc);
	bool IsSoft3D() const { return savedata.mpCmdRollviewmode == 1; }
	void SyncSoft3DCamFromSave();
	void PersistSoft3DCam();
	static void Soft3dYawCb(void* ctx, int value);
	static void Soft3dPitchCb(void* ctx, int value);
	static void Soft3dZoomCb(void* ctx, int value);
	BOOL RunPlaceDialog(MpPromptSnapshotEvent& ev);
	void AutoFollowPlayhead(double t);
	void InvalidateRoll();
	void EnsureDurationFloor(double hintSec = 0.0);

	afx_msg int OnCreate(LPCREATESTRUCT lp);
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	DECLARE_MESSAGE_MAP()

	GdiSoft3D::Cam m_cam3d;
	bool m_rotDragging = false;
	CPoint m_rotOrigin;
	float m_rotYaw0 = 0.f;
	float m_rotPitch0 = 0.f;
};

class CCommandRollDlg : public CCustomBlurDialogExBase
{
	DECLARE_DYNAMIC(CCommandRollDlg)
public:
	CCommandRollDlg(CWnd* pParent = nullptr);
	virtual ~CCommandRollDlg();
	enum { IDD = IDD_MP_CMDROLL };

	void ReloadFromText(const CString& text, UINT syncGen);
	void SetPromptPeer(CPromptDlg* peer);

protected:
	enum { kTimerId = 77 };

	CCustomStandardButton m_close, m_zoomIn, m_zoomOut, m_help;
	CCustomStandardButton m_analyze, m_run, m_stop, m_reset, m_clear;
	CCustomComboBox m_mode;
	CCustomStatic m_timeLbl;
	CCustomStatic m_modeLbl;
	CCustomProgressCtrl m_progress;
	CCommandRollView m_view;
	CPromptDlg* m_peer = nullptr;
	UINT m_syncGen = 0;
	CString m_lastTimeText;
	CBrush m_brDlg;
	CToolTipCtrl m_tooltip;
	BOOL m_posRestored = FALSE;
	BOOL m_inSizeMove = FALSE;
	BOOL m_analyzing = FALSE;

	void LayoutControls();
	void SavePosToSavedata();
	void RestorePosFromSavedata();
	void SetupTooltips();
	void ShowHelpSheet();
	void FillModeCombo();
	int  GetSelectedAnalyzeMode() const;
	void SetAnalyzeUiBusy(BOOL busy);
	void PersistPromptText(const CString& text);
	CString CommitAndGetText();
	static void AnalyzeProgressThunk(int percent, LPCTSTR status, void* user);

	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual void PostNcDestroy();
	virtual void OnClose();
	afx_msg void OnCloseBtn();
	afx_msg void OnZoomIn();
	afx_msg void OnZoomOut();
	afx_msg void OnHelpBtn();
	afx_msg void OnAnalyze();
	afx_msg void OnRun();
	afx_msg void OnStop();
	afx_msg void OnReset();
	afx_msg void OnClear();
	afx_msg void OnModeSel();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnEnterSizeMove();
	afx_msg void OnExitSizeMove();
	afx_msg void OnMoving(UINT fwSide, LPRECT pRect);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	DECLARE_MESSAGE_MAP()
};

void MpShowCommandRollDialog(CWnd* pParent, BOOL bActivate = TRUE);
void MpToggleCommandRollDialog(CWnd* pParent);
BOOL MpIsCommandRollOpen();
void MpCommandRollNotifyText(const CString& text, UINT syncGen);
