#pragma once
#include "afxwin.h"
#include "CCustomControl.h"
#include "resource.h"
#include "GdiSoft3D.h"

class CS3mView : public CCustomStatic
{
	DECLARE_DYNAMIC(CS3mView)
public:
	CS3mView();
	void RequestRedraw() { if (GetSafeHwnd()) Invalidate(FALSE); }
	GdiSoft3D::Context m_ctx;
	BOOL m_ready;
	virtual BOOL PaintCustomOpaque(CDC& dc);
protected:
	afx_msg void OnPaint();
	afx_msg LRESULT OnPrintClient(WPARAM, LPARAM);
	afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
	afx_msg void OnSize(UINT, int, int);
	afx_msg void OnContextMenu(CWnd*, CPoint);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnMButtonDown(UINT nFlags, CPoint point);
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	// 0=左旋回 1=右旋回 2=前進 3=後退
	int HitMoveDir(CPoint pt) const;
	CPoint m_dragOrigin;
	int m_dragging;
	int m_dragTurnAcc;
	DECLARE_MESSAGE_MAP()
};

class CSoft3DMazeDlg : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CSoft3DMazeDlg)
public:
	CSoft3DMazeDlg(CWnd* p = NULL);
	virtual ~CSoft3DMazeDlg();
	enum { IDD = IDD_SOFT3DMAZE };
	enum {
		S3M_MAX = 400,
		S3M_MIN = 10,
		S3M_TIMER = 96,
		CELL_WALL = 1,
		CELL_FLOOR = 0,
		CELL_START = 2,
		CELL_GOAL = 3,
		CELL_TEMPO = 4,
		CELL_PITCH_UP = 5,
		CELL_PITCH_DN = 6,
		CELL_NEXT = 7,
		CELL_EQ = 8,
		CELL_WINDOW = 9,
		ITEM_TEMPO = 1,
		ITEM_PITCH_UP = 2,
		ITEM_PITCH_DN = 4,
		ITEM_NEXT = 8,
		ITEM_EQ = 16,
		ITEM_WINDOW = 32,
		ITEM_ALL = 63,
		CLEAR_IDLE = 0,
		CLEAR_TEXT_IN = 1,
		CLEAR_TEXT_HOLD = 2,
		CLEAR_TEXT_OUT = 3,
		CLEAR_FADE_OUT = 4,
		CLEAR_FADE_IN = 5
	};
protected:
	virtual void DoDataExchange(CDataExchange*);
	virtual BOOL PreTranslateMessage(MSG*);
	virtual void PostNcDestroy();
	DECLARE_MESSAGE_MAP()
	void LayoutHelpBtn();
	void LayoutAll();
	void PersistUi();
	void PersistRun();
	BOOL LoadRun();
	void FreeGrid();
	BOOL AllocGrid(int n);
	BYTE& Cell(int x, int z) { return m_grid[(size_t)z * (size_t)m_n + (size_t)x]; }
	BYTE CellAt(int x, int z) const { return m_grid[(size_t)z * (size_t)m_n + (size_t)x]; }
	BYTE& Visit(int x, int z) { return m_visit[(size_t)z * (size_t)m_n + (size_t)x]; }
	BYTE VisitAt(int x, int z) const { return m_visit[(size_t)z * (size_t)m_n + (size_t)x]; }
	int ReadSizeFromUi();
	void SetSizeToUi(int n);
	void GenerateMaze();
	void RenderScene();
	void DrawMinimap();
	void TickMove(float dt);
	void MarkVisited();
	void TryPickup();
	void ApplyItem(int kind);
	void RestoreAudioBaseline();
	void CaptureAudioBaseline();
	void UpdateStatus();
	void ShowHelpSheet();
	void ResetClearFx();
	void BeginClearSequence();
	void TickClear(float dt);
	BOOL IsBlocked(float x, float z) const;
	// Soft3D Project と同型のカメラ基底（前=sin/cos、右=cos/-sin）
	void CamBasisYaw(float yaw, float& fwdX, float& fwdZ, float& rightX, float& rightZ) const;
	// 3D 描画用 eye（論理位置より前方 0.8）。ミニマップは m_px/m_pz のまま
	void GetRenderEye(float& ex, float& ez) const;
	void WorldToCam(float wx, float wz, float& lx, float& lz) const;
	void WorldToMap(float wx, float wz, float& mx, float& my) const;
	BOOL TryTurn(int dir);
	BOOL TryStep(int mx, int mz);
public:
	BOOL InputTurn(int dir) { return TryTurn(dir); }
	BOOL InputStep(int mx, int mz) { return TryStep(mx, mz); }
	virtual BOOL OnInitDialog();
	virtual void OnOK() {}
	virtual void OnCancel() { DestroyWindow(); }
	afx_msg void OnGen();
	afx_msg void OnCloseBtn();
	afx_msg void OnHelp();
	afx_msg void OnSizeChanged();
	afx_msg void OnSizeEditChange();
	afx_msg void OnTimer(UINT_PTR);
	afx_msg void OnSize(UINT, int, int);
	afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
	afx_msg void OnDestroy();
	afx_msg void OnContextMenu(CWnd*, CPoint);
	void ShowContextMenu(CPoint screenPt);
	void PaintClearOverlay(CDC& dc, const CRect& rc);

	CCustomStandardButton m_help, m_gen, m_close;
	CCustomStatic m_sizeL, m_hint, m_status;
	CCustomComboBox m_size;
	CS3mView m_view;
	CToolTipCtrl m_tooltip;

	BYTE* m_grid;
	BYTE* m_visit;
	int m_n;
	float m_px, m_pz, m_yaw;
	float m_yawTarget;
	float m_pxTarget, m_pzTarget;
	int m_turning;
	int m_turnHeld;
	int m_moving;
	int m_moveHeld;
	float m_bob;
	int m_won;
	int m_clearPhase;
	float m_clearT;
	float m_clearTextA;
	float m_clearScreenA;
	int m_itemsLeft;
	int m_baseTempoPos;
	int m_basePitchPos;
	DWORD m_lastTick;
	DWORD m_rng;
	DWORD m_lastAutosave;
	int m_runDirty;
};

void OpenSoft3DMazeModeless(CWnd*);
void CloseSoft3DMazeIfOpen();
BOOL IsSoft3DMazeActive();
