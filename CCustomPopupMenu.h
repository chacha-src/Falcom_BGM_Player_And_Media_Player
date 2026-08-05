#pragma once
#include "CCustomControl.h"

// ============================================================================
// CCustomPopupMenu — 自前コンテキストメニュー（CCustom 系）
// ・DWM アクリル透過は載せない不透明ポップアップ
// ・描画は BufferedPaint+Opaque / 淫女オーバーレイ正式対応（ちらつき禁止）
// ・項目は固定配列。std::function / vector は使わない
// ・ルート先頭にフォント／アクリル（骨格注入）。フォントはホバーで即プレビュー
// ・スライダー / Edit / Combo / List 行は CCustom*。操作中はメニューを閉じずライブ通知可
// ============================================================================

enum {
	CCUSTOM_POPUP_MAX_ITEMS = 110,
	CCUSTOM_POPUP_MAX_SUBS = 24,
	CCUSTOM_POPUP_MAX_SLIDERS = 16,
	CCUSTOM_POPUP_MAX_EDITS = 8,
	CCUSTOM_POPUP_MAX_COMBOS = 8,
	CCUSTOM_POPUP_MAX_LISTS = 4,
	CCUSTOM_POPUP_MAX_CHOICES = 24,
	CCUSTOM_POPUP_MAX_FACES = 96,
	CCUSTOM_POPUP_CHOICE_LEN = 64,
	CCUSTOM_POPUP_TEXT_LEN = 128,
	CCUSTOM_POPUP_TIP_LEN = 256,
	CCUSTOM_POPUP_ITEM_H = 32,
	CCUSTOM_POPUP_SEP_H = 14,
	CCUSTOM_POPUP_SLIDER_H = 46,
	CCUSTOM_POPUP_EDIT_H = 48,
	CCUSTOM_POPUP_COMBO_H = 48,
	CCUSTOM_POPUP_LIST_H = 96,
	CCUSTOM_POPUP_PAD_X = 14,       // 左（リボン後）
	CCUSTOM_POPUP_PAD_RIGHT = 20,   // 右 — 余裕の余白
	CCUSTOM_POPUP_RIBBON_W = 9,
	CCUSTOM_POPUP_CHECK_W = 24,     // レ点列（ラベル無し行も同幅で揃える）
	CCUSTOM_POPUP_ARROW_W = 16,
	CCUSTOM_POPUP_MIN_W = 200,
	CCUSTOM_POPUP_ANIM_IN_MS = 140,
	CCUSTOM_POPUP_ANIM_OUT_MS = 100,
	CCUSTOM_POPUP_ANIM_SUB_MS = 120,
	CCUSTOM_POPUP_SCROLL_STEP = 36,
	// 骨格専用コマンド（呼び出し元へ返さない）
	CCUSTOM_POPUP_ID_ACRYLIC = 0x00E00100,
	CCUSTOM_POPUP_ID_FONT_BOLD = 0x00E00101,
	CCUSTOM_POPUP_ID_FONT_ITALIC = 0x00E00102,
	CCUSTOM_POPUP_ID_FONT_FACE = 0x00E00110
};

enum CCustomPopupItemKind {
	CCUSTOM_POPUP_CMD = 0,
	CCUSTOM_POPUP_CHECK = 1,
	CCUSTOM_POPUP_SEP = 2,
	CCUSTOM_POPUP_SUB = 3,
	CCUSTOM_POPUP_SLIDER = 4,
	CCUSTOM_POPUP_EDIT = 5,
	CCUSTOM_POPUP_COMBO = 6,
	CCUSTOM_POPUP_LIST = 7
};

typedef void (*CCustomPopupSliderCb)(void* ctx, int value);
typedef void (*CCustomPopupEditCb)(void* ctx, LPCTSTR text);
typedef void (*CCustomPopupChoiceCb)(void* ctx, int index, LPCTSTR text);

struct CCustomPopupItem {
	int kind;
	UINT id;
	wchar_t text[CCUSTOM_POPUP_TEXT_LEN];
	wchar_t tip[CCUSTOM_POPUP_TIP_LEN];
	BOOL enabled;
	BOOL checked;
	BOOL hasTip;
	int sliderMin;
	int sliderMax;
	int sliderPos;
	CCustomPopupSliderCb sliderCb;
	CCustomPopupEditCb editCb;
	CCustomPopupChoiceCb choiceCb;
	void* ctrlCtx;
	int subIndex;
	int sliderIndex;
	int editIndex;
	int comboIndex;
	int listIndex;
	int choiceSet;
	int choiceSel;
	CRect rc;
};

struct CCustomPopupChoiceSet {
	wchar_t items[CCUSTOM_POPUP_MAX_CHOICES][CCUSTOM_POPUP_CHOICE_LEN];
	int count;
};

class CCustomPopupMenu : public CWnd
{
	DECLARE_DYNAMIC(CCustomPopupMenu)
public:
	CCustomPopupMenu();
	virtual ~CCustomPopupMenu();

	void Reset();
	BOOL AddCommand(UINT id, LPCTSTR text, LPCTSTR tip = NULL, BOOL enabled = TRUE);
	BOOL AddCheck(UINT id, LPCTSTR text, BOOL checked, LPCTSTR tip = NULL, BOOL enabled = TRUE);
	BOOL AddSeparator();
	CCustomPopupMenu* AddSubMenu(LPCTSTR text, LPCTSTR tip = NULL);
	BOOL AddSlider(LPCTSTR label, int vmin, int vmax, int vpos,
		CCustomPopupSliderCb cb, void* ctx, LPCTSTR tip = NULL);
	BOOL AddEdit(LPCTSTR label, LPCTSTR initial,
		CCustomPopupEditCb cb, void* ctx, LPCTSTR tip = NULL);
	BOOL AddCombo(LPCTSTR label, const LPCTSTR* items, int count, int curSel,
		CCustomPopupChoiceCb cb, void* ctx, LPCTSTR tip = NULL);
	BOOL AddList(LPCTSTR label, const LPCTSTR* items, int count, int curSel,
		CCustomPopupChoiceCb cb, void* ctx, LPCTSTR tip = NULL);

	UINT Track(CPoint screenPt, CWnd* pOwner);

	void SetAeroMode(BOOL b) { m_bAeroMode = b; if (GetSafeHwnd()) Invalidate(FALSE); }
	void SetSkipChrome(BOOL b) { m_skipChrome = b; }
	void SetStickyLeading(int n) { m_stickyCount = (n < 0) ? 0 : n; }
	void PersistPopupFont();
	void RefreshFontChain();

protected:
	CCustomPopupItem m_items[CCUSTOM_POPUP_MAX_ITEMS];
	int m_itemCount;
	CCustomPopupMenu* m_subs[CCUSTOM_POPUP_MAX_SUBS];
	int m_subCount;
	CCustomSliderCtrl m_sliders[CCUSTOM_POPUP_MAX_SLIDERS];
	int m_sliderCount;
	CCustomEdit m_edits[CCUSTOM_POPUP_MAX_EDITS];
	int m_editCount;
	CCustomComboBox m_combos[CCUSTOM_POPUP_MAX_COMBOS];
	int m_comboCount;
	CCustomListBox m_lists[CCUSTOM_POPUP_MAX_LISTS];
	int m_listCount;
	CCustomPopupChoiceSet m_choiceSets[CCUSTOM_POPUP_MAX_COMBOS + CCUSTOM_POPUP_MAX_LISTS];
	int m_choiceSetCount;

	BOOL m_bAeroMode;
	BOOL m_tracking;
	BOOL m_done;
	UINT m_result;
	int m_hot;
	int m_openSub;
	CWnd* m_owner;
	CCustomPopupMenu* m_parentMenu;
	CCustomPopupMenu* m_root;
	CToolTipCtrl m_tip;
	int m_tipHot;
	CBitmap m_memBmp;
	int m_memW;
	int m_memH;
	HFONT m_font;
	HFONT m_fontOwned;
	int m_menuW;
	int m_menuH;       // 表示ウィンドウ高さ（クランプ後）
	int m_contentH;    // 全項目のコンテンツ高さ
	int m_scrollY;     // 縦スクロール（固定ヘッダより下のみ）
	int m_scrollMax;
	int m_stickyCount; // 先頭から固定する項目数（フォントのサイズ/太字等）
	int m_stickyH;     // 固定ヘッダの高さ
	BOOL m_asSubmenu;
	int m_animTick;
	BOOL m_skipChrome;
	BOOL m_chromeInjected;
	wchar_t m_previewFace[32];
	BOOL m_previewing;
	int m_bounceIdx;   // レ点バウンス中の項目（-1=なし）
	int m_nBounce;     // CCustomCheckBox と同じ 8→0

	void CopyText(wchar_t* dst, int dstN, LPCTSTR src);
	BOOL AddItemBase(int kind, UINT id, LPCTSTR text, LPCTSTR tip, BOOL enabled, BOOL checked);
	int AllocChoiceSet(const LPCTSTR* items, int count);
	void MeasureLayout();
	BOOL CreatePopupAt(CPoint screenPt, CCustomPopupMenu* parentMenu, CCustomPopupMenu* root);
	void DestroyPopupTree();
	void CloseChain(UINT result);
	void PaintToDC(CDC& dc);
	CRect ItemViewRect(int idx) const;
	int HitTest(CPoint pt) const;
	void SetHot(int idx);
	void OpenSubAt(int idx);
	void CloseOpenSub();
	void SyncEmbeddedChildren();
	void ShowEmbedded(BOOL show);
	void UpdateTip();
	BOOL IsPointInChain(CPoint screenPt) const;
	BOOL IsHwndRelated(HWND h) const;
	BOOL IsForegroundOurs() const;
	BOOL IsInteractiveKind(int kind) const;
	BOOL IsChromeCommand(UINT id) const;
	CCustomPopupMenu* RootMenu();
	void RunModalLoop();
	void AnimateIn();
	void AnimateOut();
	void NotifyEditFromHwnd(HWND hwnd);
	void NotifyChoiceFromHwnd(HWND hwnd, BOOL fromList);
	void ApplyPreviewFace(LPCTSTR face);
	void ClearPreviewFace();
	void CommitFace(LPCTSTR face);
	void EnsureChromePrefix();
	void RebuildMenuFont();
	BOOL HandleChromeClick(int idx);
	void StartCheckBounce(int idx);
	void SetScrollY(int y);
	BOOL OnWheelDelta(int delta);
	BOOL HandleWheelInChain(CPoint screenPt, int delta);

	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg LRESULT OnPrintClient(WPARAM wParam, LPARAM lParam);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnMouseLeave();
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnKillFocus(CWnd* pNewWnd);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg BOOL OnTtnNeedText(UINT id, NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	DECLARE_MESSAGE_MAP()
};
