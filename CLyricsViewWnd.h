#pragma once

// カラオケ風: 現在行を中央付近に保ち、切替時はピクセル単位で追従スクロールする歌詞ビュー。
// （Static では滑らか追従できないため GDI owner-draw）
class CLyricsViewWnd : public CWnd
{
	DECLARE_DYNAMIC(CLyricsViewWnd)
public:
	static const int kMaxLines = 300;

	CLyricsViewWnd();
	virtual ~CLyricsViewWnd();

	BOOL Create(CWnd* pParent, UINT nID);
	void Clear();
	// lines[0..count) を表示対象にする（終端番兵は含めない）
	void SetLines(const CString* lines, int count);
	void SetCurrent(int idx);
	void EnsureFonts(int dpiPointTenths, LPCTSTR face);

protected:
	CString m_line[kMaxLines];
	int m_count;
	int m_cur;
	int m_lineH;
	double m_scrollY;
	double m_targetY;
	CFont m_font;
	CFont m_fontHi;
	int m_fontPt;
	CString m_fontFace;
	UINT_PTR m_timer;

	void RecalcTarget();
	void StartAnim();
	void StopAnim();

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);

	DECLARE_MESSAGE_MAP()
};
