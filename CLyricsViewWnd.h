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
	// lines[0..count) を表示。times は 1/100 秒(lrctm と同じ)。NULL ならカラオケ塗り無し。
	void SetLines(const CString* lines, int count, const DWORD* times = NULL, int timeCount = 0);
	void SetCurrent(int idx);
	// 再生位置(1/100秒)。現在行と行内進捗を更新しカラオケ塗りに使う。
	void SetPlayCentis(DWORD centis);
	void EnsureFonts(int dpiPointTenths, LPCTSTR face);
	// デスクトップ常時前面向け: 暗背景・高コントラスト文字
	void SetOverlayStyle(BOOL on);

protected:
	CString m_line[kMaxLines];
	DWORD m_tm[kMaxLines];
	int m_count;
	int m_tmCount;
	int m_cur;
	double m_frac; // 現在行の進捗 0..1
	int m_lineH;
	double m_scrollY;
	double m_targetY;
	CFont m_font;
	CFont m_fontHi;
	int m_fontPt;
	CString m_fontFace;
	UINT_PTR m_timer;
	BOOL m_overlay;

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
