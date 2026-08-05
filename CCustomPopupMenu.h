#pragma once
#include "CCustomControl.h"

// ============================================================================
// CCustomPopupMenu — 自前コンテキストメニュー（CCustom 系）
// ・DWM アクリル透過は載せない不透明ポップアップ
// ・描画は BufferedPaint+Opaque / 淫女オーバーレイ正式対応（ちらつき禁止）
// ・項目は固定配列。std::function / vector は使わない
// ・ルート先頭にフォント／アクリル（骨格注入）。フォントはホバーで即プレビュー
// ・チェック以外の CCustom* 内包は「ラベル + コントロール」のスライダー方式
//
// 【公開API（他アプリ向け）】
//   構築: Reset / AddCommand / AddCheck / AddSeparator / AddSubMenu
//         AddSlider / AddEdit / AddCombo / AddList
//         AddRangeSlider / AddProgress / AddButton
//   表示: Track(screenPt, owner) → 選択コマンド ID（骨格IDは 0）
//   照会: FindItemById / GetItemCount / GetItemKind / GetItemId
//         GetSliderPos / SetSliderPos / GetEditText / GetChoiceSel
//         GetRangeValues / SetProgressPos / GetProgressPos
//         GetSliderCtrl / GetEditCtrl / GetComboCtrl / GetListCtrl
//         GetRangeSliderCtrl / GetProgressCtrl / GetButtonCtrl
//   オプション: SetSkipChrome / SetStickyLeading / SetAeroMode
// ============================================================================

enum {
	CCUSTOM_POPUP_MAX_ITEMS = 110,
	CCUSTOM_POPUP_MAX_SUBS = 24,
	CCUSTOM_POPUP_MAX_SLIDERS = 16,
	CCUSTOM_POPUP_MAX_EDITS = 8,
	CCUSTOM_POPUP_MAX_COMBOS = 8,
	CCUSTOM_POPUP_MAX_LISTS = 4,
	CCUSTOM_POPUP_MAX_RANGES = 4,
	CCUSTOM_POPUP_MAX_PROGRESSES = 4,
	CCUSTOM_POPUP_MAX_BUTTONS = 8,
	CCUSTOM_POPUP_MAX_CHOICES = 24,
	CCUSTOM_POPUP_MAX_CHOICE_SETS = CCUSTOM_POPUP_MAX_COMBOS + CCUSTOM_POPUP_MAX_LISTS + CCUSTOM_POPUP_MAX_EDITS,
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
	CCUSTOM_POPUP_RANGE_H = 58,
	CCUSTOM_POPUP_PROGRESS_H = 44,
	CCUSTOM_POPUP_BUTTON_H = 40,
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
	CCUSTOM_POPUP_LIST = 7,
	CCUSTOM_POPUP_RANGE = 8,
	CCUSTOM_POPUP_PROGRESS = 9,
	CCUSTOM_POPUP_BUTTON = 10
};

typedef void (*CCustomPopupSliderCb)(void* ctx, int value);
typedef void (*CCustomPopupEditCb)(void* ctx, LPCTSTR text);
typedef void (*CCustomPopupChoiceCb)(void* ctx, int index, LPCTSTR text);
// pos / loop選択 / A-B（未設定は -1 のまま）/ nSBCode / dragTarget(HitTest)
typedef void (*CCustomPopupRangeCb)(void* ctx, int pos, int selMin, int selMax, int abA, int abB, UINT nSBCode, int dragTarget);
typedef void (*CCustomPopupButtonCb)(void* ctx, UINT id);

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
	int rangeSelMin;
	int rangeSelMax;
	int rangeAbA;
	int rangeAbB;
	BOOL progressShowPct;
	BOOL buttonCloseOnClick;
	CCustomPopupSliderCb sliderCb;
	CCustomPopupEditCb editCb;
	CCustomPopupChoiceCb choiceCb;
	CCustomPopupRangeCb rangeCb;
	CCustomPopupButtonCb buttonCb;
	void* ctrlCtx;
	int subIndex;
	int sliderIndex;
	int editIndex;
	int comboIndex;
	int listIndex;
	int rangeIndex;
	int progressIndex;
	int buttonIndex;
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

	// --- 基本項目 ---
	BOOL AddCommand(UINT id, LPCTSTR text, LPCTSTR tip = NULL, BOOL enabled = TRUE);
	BOOL AddCheck(UINT id, LPCTSTR text, BOOL checked, LPCTSTR tip = NULL, BOOL enabled = TRUE);
	BOOL AddSeparator();
	CCustomPopupMenu* AddSubMenu(LPCTSTR text, LPCTSTR tip = NULL);

	// --- CCustom* 内包（チェック以外はスライダー方式: ラベル行 + コントロール）---
	// id は FindItemById / Get* 用。0 でも可。
	BOOL AddSlider(LPCTSTR label, int vmin, int vmax, int vpos,
		CCustomPopupSliderCb cb, void* ctx, LPCTSTR tip = NULL, UINT id = 0);
	BOOL AddEdit(LPCTSTR label, LPCTSTR initial,
		CCustomPopupEditCb cb, void* ctx, LPCTSTR tip = NULL, UINT id = 0);
	BOOL AddCombo(LPCTSTR label, const LPCTSTR* items, int count, int curSel,
		CCustomPopupChoiceCb cb, void* ctx, LPCTSTR tip = NULL, UINT id = 0);
	BOOL AddList(LPCTSTR label, const LPCTSTR* items, int count, int curSel,
		CCustomPopupChoiceCb cb, void* ctx, LPCTSTR tip = NULL, UINT id = 0);
	BOOL AddRangeSlider(LPCTSTR label, int vmin, int vmax, int vpos,
		int selMin, int selMax, int abA, int abB,
		CCustomPopupRangeCb cb, void* ctx, LPCTSTR tip = NULL, UINT id = 0);
	BOOL AddProgress(LPCTSTR label, int vmin, int vmax, int vpos,
		BOOL showPercent = TRUE, LPCTSTR tip = NULL, UINT id = 0);
	// closeOnClick=TRUE ならクリックでメニューを閉じ Track が id を返す。cb は閉じる前に呼ばれる。
	BOOL AddButton(UINT id, LPCTSTR text,
		CCustomPopupButtonCb cb = NULL, void* ctx = NULL, LPCTSTR tip = NULL, BOOL closeOnClick = TRUE);

	UINT Track(CPoint screenPt, CWnd* pOwner);

	void SetAeroMode(BOOL b) { m_bAeroMode = b; if (GetSafeHwnd()) Invalidate(FALSE); }
	void SetSkipChrome(BOOL b) { m_skipChrome = b; }
	void SetStickyLeading(int n) { m_stickyCount = (n < 0) ? 0 : n; }
	void PersistPopupFont();
	void RefreshFontChain();
	// フォント変更後に開いているメニューの寸法・子コントロールを張り直す
	void RelayoutOpenChain();

	// --- 照会 / 実行中更新（Track 中も可。id==0 の項目は先頭一致）---
	int GetItemCount() const { return m_itemCount; }
	int FindItemById(UINT id) const;
	int GetItemKind(int idx) const;
	UINT GetItemId(int idx) const;

	BOOL GetSliderPos(UINT id, int* outPos) const;
	BOOL SetSliderPos(UINT id, int pos);
	BOOL GetEditText(UINT id, wchar_t* buf, int bufCch) const;
	BOOL GetChoiceSel(UINT id, int* outSel) const;
	BOOL GetRangeValues(UINT id, int* pos, int* selMin, int* selMax, int* abA, int* abB) const;
	// Track 中の再生追従。ドラッグ中は無視。id==0 なら先頭の Range。
	BOOL LiveMirrorRange(UINT id, int pos, int selMin, int selMax, int mn, int mx, int abA, int abB);
	BOOL GetProgressPos(UINT id, int* outPos) const;
	BOOL SetProgressPos(UINT id, int pos);

	static CCustomPopupMenu* GetTrackingRoot();

	CCustomSliderCtrl* GetSliderCtrl(UINT id);
	CCustomEdit* GetEditCtrl(UINT id);
	CCustomComboBox* GetComboCtrl(UINT id);
	CCustomListBox* GetListCtrl(UINT id);
	CCustomRangeSliderCtrl* GetRangeSliderCtrl(UINT id);
	CCustomProgressCtrl* GetProgressCtrl(UINT id);
	CCustomStandardButton* GetButtonCtrl(UINT id);

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
	CCustomRangeSliderCtrl m_ranges[CCUSTOM_POPUP_MAX_RANGES];
	int m_rangeCount;
	CCustomProgressCtrl m_progresses[CCUSTOM_POPUP_MAX_PROGRESSES];
	int m_progressCount;
	CCustomStandardButton m_buttons[CCUSTOM_POPUP_MAX_BUTTONS];
	int m_buttonCount;
	CCustomPopupChoiceSet m_choiceSets[CCUSTOM_POPUP_MAX_CHOICE_SETS];
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
	static CCustomPopupMenu* s_trackingRoot;
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
	void RefreshEmbeddedChildren(); // 親描画で子が塗り潰された場合の再描画
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
	void NotifyRangeFromHwnd(HWND hwnd, UINT nSBCode);
	void NotifyButtonFromHwnd(HWND hwnd);
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
	void InvalidateBgOnly(); // 背景アニメ用：子コントロールを巻き込まない
	int FindItemIndexById(UINT id) const;

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
