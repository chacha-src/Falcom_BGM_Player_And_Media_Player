#pragma once

// カラオケ風: 先頭は上から、途中は現在行を縦中央に追従、末尾は最終行を下端へ（MP LRC GDI と同系）。
// 途中オープン時は頭から該当行へ高速 catch-up（timerp 相当の ~60fps）。
// 長い行は描画幅に収まるようフォントを縮める。CreatePointFont は窓の DPI を使う。
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
	int GetFontPt() const { return m_fontPt; }
	// デスクトップ常時前面向け: 暗背景・高コントラスト文字
	void SetOverlayStyle(BOOL on);
	// 途中オープン用: スクロールを先頭に戻し、該当行まで高速追従を開始
	void BeginCatchFromTop();
	BOOL IsFastCatching() const { return m_fastCatch; }

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
	double m_scrollVel; // px/sec（臨界減衰風）
	BOOL m_fastCatch;   // 大距離の頭→該当行 chase
	ULONGLONG m_lastAnimQpc;
	ULONGLONG m_qpcFreq;
	CFont m_font;
	CFont m_fontHi;
	int m_fontPt;
	UINT m_dpi;
	CString m_fontFace;
	UINT_PTR m_timer;
	BOOL m_overlay;
	volatile LONG m_animPosted; // timerp 型 oneshot Post 合流

	// 毎フレ CreateCompatibleBitmap しない
	CDC m_memDC;
	CBitmap m_memBmp;
	CBitmap* m_oldBmp;
	int m_memW;
	int m_memH;

	UINT GetViewDpi() const;
	void RecalcTarget();
	void StartAnim();
	void StopAnim();
	void StepScroll(double dtSec);
	void EnsureMemDC(int w, int h);
	void RequestAnimTick();

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	afx_msg LRESULT OnAnimTick(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()
};
