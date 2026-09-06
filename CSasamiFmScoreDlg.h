#pragma once
#include "afxdialogex.h"
#include "CCustomControl.h"
#include "SasamiComposerDoc.h"
#include "CSasamiStaffCore.h"
#include "CSasamiScoreMidiIn.h"
#include "CSasamiScoreHist.h"
#include "CSasamiPianoRollView.h"

class CSasamiFmScoreDlg : public CCustomBlurDialogExBase
{
	DECLARE_DYNAMIC(CSasamiFmScoreDlg)
public:
	CSasamiFmScoreDlg(CWnd* pParent = nullptr);
	virtual ~CSasamiFmScoreDlg();
	enum { IDD = IDD_SASAMI_FM_SCORE };
	static CSasamiFmScoreDlg* Instance();
	static void OpenOwned(CWnd* owner);
	void LoadFromDoc(const ScFmDoc& src);
	void PushDocToText();
	void PullDocFromText(int force = 0);
	void PersistSession();
	const ScFmDoc* Doc() const { return &m_doc; }
	ScFmDoc* DocMutable() { return &m_doc; }
	ScStaffUi* Ui() { return &m_ui; }
	int* CurPartPtr() { return &m_curCh; }
	ScScoreHist* Hist() { return &m_hist; }
	ScEvent* ClipBuf() { return m_clip; }
	int* ClipCountPtr() { return &m_clipN; }
	uint32_t* ClipBasePtr() { return &m_clipBase; }
	uint32_t* ClipSpanPtr() { return &m_clipSpan; }
	void NotifyEdited();
	void RefreshBoundRoll();
	static uint8_t MidiToFmNoteByte(int midiNote);
	void HistPush();

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual void PostNcDestroy();
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
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnBnClickedOpen();
	afx_msg void OnBnClickedSave();
	afx_msg void OnBnClickedPlay();
	afx_msg void OnBnClickedHelp();
	afx_msg void OnBnClickedTempo();
	afx_msg void OnBnClickedVoice();
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
	afx_msg void OnBnClickedArr();
	afx_msg void OnBnClickedLayout();
	afx_msg void OnBnClickedRoll();
	afx_msg void OnCbnSelchangeCh();
	afx_msg void OnCbnStrip();
	afx_msg void OnCbnMidiIn();
	afx_msg void OnCbnPasteMode();
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg LRESULT OnPalDur(WPARAM w, LPARAM l);
	afx_msg LRESULT OnPalQueryState(WPARAM w, LPARAM l);
	afx_msg LRESULT OnPalLayout(WPARAM w, LPARAM l);
	afx_msg LRESULT OnNoteProps(WPARAM w, LPARAM l);
	afx_msg LRESULT OnDeferredInit(WPARAM w, LPARAM l);
	afx_msg LRESULT OnScoreMidi(WPARAM w, LPARAM l);
	void LayoutChrome();
	void PersistUiGeom();
	void RestoreUiGeom();
	void SetupTooltips();
	void UpdateScrollBars();
	void ApplyLang();
	void SyncPropFromSel();
	void SyncStripCombos();
	void RefreshStrip();
	void PlaceOrEditAt(CPoint pt);
	int BuildToTemp(wchar_t* outPath, int outCch, uint32_t fromTick = 0);
	void OpenVoiceEditor();
	void PickBuiltinNeiro();
	void RefreshToneLabels();
	void RefreshPartEnabled(int autoUsedParts = 0);
	void SyncMeterFromDoc();
	void ApplyMeterAtBar(uint32_t tick, int numer, int denom);
	void ApplyLayoutPalCmd(int cmdId);
	UINT RunLayoutMenu(uint32_t atTick, CPoint screenPt);
	void UpdateNoteCursor();
	void UpdateHover(CPoint pt);
	void UpdateHoverStatus(CPoint pt);
	void UpdateHelpBar();
	void OpenNotePropsForSel();
	void SyncTextIfOpen();
	void SyncMidiInCombos();
	void ApplyFollowScroll();

	CCustomStandardButton m_btnOpen, m_btnSave, m_btnPlay, m_btnHelp, m_btnTempo, m_btnVoice, m_btnExport;
	CCustomStandardButton m_btnPencil, m_btnErase, m_btnSel, m_btnPal, m_btnPropUpd;
	CCustomStandardButton m_btnMark, m_btnLoopA, m_btnLoopB, m_btnLoopClr, m_btnShowAll, m_btnText;
	CCustomStandardButton m_btnArr, m_btnLayout, m_btnRoll;
	CCustomComboBox m_ch, m_stripKind0, m_stripKind1, m_stripKind2, m_stripHgt0, m_stripHgt1, m_stripHgt2, m_stripDraw, m_stripLanes, m_stripStep;
	CCustomComboBox m_midiInDev, m_midiInMode;
	CCustomComboBox m_pasteMode;
	CCustomEdit m_edNote, m_edGt, m_edVel;
	CCustomStatic m_status;
	CCustomStatic m_helpBar;
	CToolTipCtrl m_tooltip;
	CRect m_bodyRc, m_trackRc, m_gridRc, m_stripRc, m_rollRc;
	ScFmDoc m_doc;
	ScStaffUi m_ui;
	ScScoreMidiIn m_midiIn;
	ScScoreHist m_hist;
	ScPianoRollView m_rollView;
	ScEvent m_clip[SC_CLIP_MAX];
	int m_clipN;
	uint32_t m_clipBase;
	uint32_t m_clipSpan;
	int m_dragLastX, m_dragLastY;
	int m_histDragPushed;
	int m_curCh;
	int m_placeRest;
	int m_accidental;
	HCURSOR m_noteCur;
	ScStaffDrawCursors m_drawCursors;
	int m_sbDrag; /* 0=none, 1=vert, 2=horz */
	int m_sbDragScroll0;
	int m_sbDragAnchor;
	BOOL m_bInLayout;
	int m_metroBeat;
	wchar_t m_lastHoverSt[256];
	wchar_t m_lastOut[MAX_PATH];
	static CSasamiFmScoreDlg* s_inst;
};
