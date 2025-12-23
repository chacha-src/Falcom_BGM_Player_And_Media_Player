// LyricsProgressWnd.h
#pragma once

// =========================================================
// リソース不要のシンプル進捗表示ウィンドウ
// =========================================================
class CLyricsProgressWnd : public CWnd
{
public:
	CLyricsProgressWnd();
	virtual ~CLyricsProgressWnd();

	// ウィンドウ作成
	BOOL Create(CWnd* pParent = NULL);

	// 表示テキスト設定
	void SetText(const CString& text);

	// 表示/非表示
	void Show();
	void Hide();

protected:
	CString m_strText;
	CFont m_font;

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);

	DECLARE_MESSAGE_MAP()
};