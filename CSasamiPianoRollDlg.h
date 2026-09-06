#pragma once
#include "afxdialogex.h"
#include "CCustomControl.h"
#include "CSasamiPianoRollView.h"
#include "SasamiComposerDoc.h"
#include "CSasamiStaffCore.h"
#include "CSasamiScoreHist.h"

class CSasamiMidiScoreDlg;
class CSasamiFmScoreDlg;

class CSasamiPianoRollDlg : public CCustomBlurDialogExBase
{
	DECLARE_DYNAMIC(CSasamiPianoRollDlg)
public:
	CSasamiPianoRollDlg(CWnd* pParent = nullptr);
	enum { IDD = IDD_SASAMI_PIANO_ROLL };

	static CSasamiPianoRollDlg* InstanceMidi();
	static CSasamiPianoRollDlg* InstanceFm();
	static CSasamiPianoRollDlg* Instance(); /* last active, or MIDI then FM */

	static void OpenForMidi(CSasamiMidiScoreDlg* score);
	static void OpenForFm(CSasamiFmScoreDlg* score);
	/* Legacy bind API (score split button / internal) */
	static void OpenOwned(CWnd* owner, ScEvent* ev, int* evCount, ScStaffUi* ui, int* curPart, int isFm);

	void Bind(ScEvent* ev, int* evCount, ScStaffUi* ui, int* curPart, int isFm);
	void BindMidi(CSasamiMidiScoreDlg* score);
	void BindFm(CSasamiFmScoreDlg* score);
	void Refresh();
	void ApplyLang();
	void SetupTooltips();
	void UpdateHelpBar();

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual void PostNcDestroy();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	DECLARE_MESSAGE_MAP()
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnClose();
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnBnOpen();
	afx_msg void OnBnSave();
	afx_msg void OnBnNew();
	afx_msg void OnBnPlay();
	afx_msg void OnBnExport();
	afx_msg void OnBnHelp();
	afx_msg void OnBnPencil();
	afx_msg void OnBnErase();
	afx_msg void OnBnSel();
	afx_msg void OnBnPal();
	afx_msg void OnBnTempo();
	afx_msg void OnBnText();
	afx_msg void OnBnLayout();
	afx_msg void OnBnArr();
	afx_msg void OnBnChord();
	afx_msg void OnBnPatt();
	afx_msg void OnBnFx();
	afx_msg void OnBnVoice();
	afx_msg void OnBnScore();
	afx_msg void OnCbnCh();
	afx_msg void OnCbnPaste();
	afx_msg void OnCbnFollow();
	afx_msg void OnCbnStrip();
	afx_msg LRESULT OnPalDur(WPARAM w, LPARAM l);
	afx_msg LRESULT OnPalQueryState(WPARAM w, LPARAM l);
	afx_msg LRESULT OnPalLayout(WPARAM w, LPARAM l);

	void LayoutChrome();
	void CreateChrome();
	void ProxyScoreCommand(UINT idc);
	void AfterEdit();
	void HistPushOwner();
	void Undo();
	void Redo();
	void PlaceNoteAt(CPoint pt);
	void EraseAt(CPoint pt);
	void BeginSelectOrDrag(CPoint pt, UINT nFlags);
	void SyncPartCombo();
	void SyncPasteFollow();
	void SyncStripCombos();
	int EvMax() const { return SC_EV_MAX; }
	ScEvent* ClipBuf();
	int* ClipCountPtr();
	uint32_t* ClipBasePtr();
	uint32_t* ClipSpanPtr();
	ScScoreHist* HistPtr();

	ScEvent* m_ev;
	int* m_evCount;
	ScStaffUi* m_ui;
	int* m_curPart;
	int m_isFm;
	CSasamiMidiScoreDlg* m_midiScore;
	CSasamiFmScoreDlg* m_fmScore;
	ScPianoRollView m_roll;
	CRect m_bodyRc, m_gridRc, m_stripRc, m_toolbarRc;
	CCustomStandardButton m_btnOpen, m_btnSave, m_btnNew, m_btnPlay, m_btnExport, m_btnHelp;
	CCustomStandardButton m_btnPencil, m_btnErase, m_btnSel, m_btnPal, m_btnTempo;
	CCustomStandardButton m_btnText, m_btnLayout, m_btnArr, m_btnChord, m_btnPatt;
	CCustomStandardButton m_btnFx, m_btnVoice, m_btnScore;
	CCustomComboBox m_ch, m_pasteMode, m_follow;
	CCustomComboBox m_stripKind0, m_stripLanes, m_stripDraw, m_stripStep;
	CCustomStatic m_status, m_helpBar;
	CToolTipCtrl m_tooltip;
	int m_dragMode; /* 0 none, 1 move, 2 resize, 3 marquee, 4 pencil-drag erase */
	int m_dragLastX, m_dragLastY;
	int m_histDragPushed;
	int m_resizeEv;
	int m_marquee;
	CPoint m_marquee0, m_marquee1;
	BOOL m_bInLayout;
	static CSasamiPianoRollDlg* s_midi;
	static CSasamiPianoRollDlg* s_fm;
};
