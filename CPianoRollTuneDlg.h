#pragma once

#include "CCustomControl.h"
#include "resource.h"

class CPianoRollTuneDlg : public CCustomBlurDialogExBase
{
	DECLARE_DYNAMIC(CPianoRollTuneDlg)
public:
	CPianoRollTuneDlg(CWnd* pParent = nullptr);
	virtual ~CPianoRollTuneDlg();
	enum { IDD = IDD_PIANOROLL_TUNE };

protected:
	static const int kRowMax = 17;
	static const int kCols = 2;
	// 2列レイアウト固定サイズ(DLU換算前の設計ピクセル)
	static const int kDlgClientW = 708;
	static const int kDlgClientH = 304;

	int m_rowCount = 0;
	int* m_pPct[kRowMax];
	CCustomStatic m_lbl[kRowMax];
	CCustomSliderCtrl m_slider[kRowMax];
	CCustomStatic m_val[kRowMax];
	CCustomStandardButton m_reset;
	CCustomStandardButton m_ok;
	CToolTipCtrl m_tooltip;
	CFont m_fontRow;

	void BuildRows();
	void StyleRows();
	void LayoutRows();
	void ApplyDialogSize();
	void SetupTooltips();
	void SyncSlidersFromSavedata();
	void SyncSavedataFromSliders();
	void UpdateValueLabels();
	static int ClampPct(int v);

	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual void DoDataExchange(CDataExchange* pDX);
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnReset();
	afx_msg void OnOk();
	DECLARE_MESSAGE_MAP()
};

void MpShowPianoRollTuneDialog(CWnd* pParent);
