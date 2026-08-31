#pragma once
#include "afxdialogex.h"
#include "CCustomControl.h"
#include "SasamiComposerDoc.h"
#include "CSasamiStaffCore.h"

class CSasamiMidiScoreDlg : public CCustomBlurDialogExBase
{
	DECLARE_DYNAMIC(CSasamiMidiScoreDlg)
public:
	CSasamiMidiScoreDlg(CWnd* pParent = nullptr);
	virtual ~CSasamiMidiScoreDlg();
	enum { IDD = IDD_SASAMI_MIDI_SCORE };
	static CSasamiMidiScoreDlg* Instance();
	static void OpenOwned(CWnd* owner);
	ScMidiDoc* Doc() { return &m_doc; }
	void LoadFromDoc(const ScMidiDoc& src);
	void SyncMeterFromDoc();
	void PushDocToText();
	void PullDocFromText();
	void NewDocument();
	void PersistSession();
	void PersistUiGeom();
	void RestoreUiGeom();

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual void PostNcDestroy();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	/* tooltips via m_tooltip */
	DECLARE_MESSAGE_MAP()
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnClose();
	afx_msg void OnDestroy();
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnBnClickedOpen();
	afx_msg void OnBnClickedSave();
	afx_msg void OnBnClickedNew();
	afx_msg void OnBnClickedPlay();
	afx_msg void OnBnClickedHelp();
	afx_msg void OnBnClickedTempo();
	afx_msg void OnBnClickedExport();
	afx_msg void OnBnClickedPencil();
	afx_msg void OnBnClickedErase();
	afx_msg void OnBnClickedSel();
	afx_msg void OnBnClickedPal();
	afx_msg void OnBnClickedPropUpd();
	afx_msg void OnBnClickedMark();
	afx_msg void OnBnClickedLoopA();
	afx_msg void OnBnClickedLoopB();
	afx_msg void OnBnClickedLoopClr();
	afx_msg void OnBnClickedShowAll();
	afx_msg void OnBnClickedText();
	afx_msg void OnCbnSelchangeCh();
	afx_msg void OnCbnStrip();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg LRESULT OnPalDur(WPARAM w, LPARAM l);
	afx_msg LRESULT OnNoteProps(WPARAM w, LPARAM l);
	afx_msg LRESULT OnDeferredInit(WPARAM w, LPARAM l);
	afx_msg LRESULT OnDeferredOpenVst(WPARAM w, LPARAM l);
	afx_msg LRESULT OnDeferredProgLabels(WPARAM w, LPARAM l);
	afx_msg LRESULT OnVstEditorClosed(WPARAM w, LPARAM l);
	afx_msg LRESULT OnVstEditorClosedUi(WPARAM w, LPARAM l);

	void LayoutChrome();
	void ApplyLang();
	void SyncPropFromSel();
	void UpdateScrollBars();
	void SyncStripCombos();
	int BuildToTemp(wchar_t* outPath, int outCch);
	void OpenVstForPart(int part1to32, int editorOnly = 0);
	void SyncVstBindsFromLive();
	void RefreshProgLabels();
	void EditProgForPart(int ch0);
	void SyncProgPropFromCh(int ch0);
	void ApplyProgPropToCh();
	void SetupTooltips();
	void PlaceOrEditAt(CPoint pt);
	void RefreshStrip();
	void UpdateNoteCursor();
	void UpdateHover(CPoint pt);
	void UpdateHoverStatus(CPoint pt);
	void OpenNotePropsForSel();

	CCustomStandardButton m_btnOpen, m_btnSave, m_btnNew, m_btnPlay, m_btnHelp, m_btnTempo, m_btnExport;
	CCustomStandardButton m_btnPencil, m_btnErase, m_btnSel, m_btnPal, m_btnPropUpd;
	CCustomStandardButton m_btnMark, m_btnLoopA, m_btnLoopB, m_btnLoopClr, m_btnShowAll, m_btnText;
	CCustomComboBox m_ch, m_stripKind0, m_stripKind1, m_stripDraw, m_stripLanes;
	CCustomEdit m_edNote, m_edGt, m_edVel;
	CCustomStatic m_status;
	CToolTipCtrl m_tooltip;
	CRect m_bodyRc, m_trackRc, m_gridRc, m_stripRc;
	ScMidiDoc m_doc;
	ScStaffUi m_ui;
	int m_curCh;
	int m_placeRest;
	int m_accidental;
	HCURSOR m_blankCur;
	int m_sbDrag; /* 0=none, 1=vert, 2=horz */
	BOOL m_bInLayout;
	int m_propMode; /* 0=note N/GT/Vel  1=Prog/BankMSB/BankLSB */
	int m_pendingEdClosePart;
	int m_pendingEdCloseProg;
	int m_pendingEdOpenPart;
	wchar_t m_lastOut[MAX_PATH];
	wchar_t m_lastHoverSt[256];
	static CSasamiMidiScoreDlg* s_inst;
};
