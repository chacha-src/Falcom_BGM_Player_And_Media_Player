// CMpHelpDlg.h : メディアプレイヤー操作ガイド(アクリルキャプション + Soft3D 実演)
//
// 旧 CMpCheatSheetDlg(素の CDialog + CCC_GdiHelpBeginPaint)の置き換え。
// キャプション帯はアクリルのまま残し、本文だけを不透明メモリ面で描く。
// CCC_GdiHelpBeginPaint は窓全体を縮小フィットしてしまいアクリル帯と衝突するため使わない。
//
#pragma once

#include "CCustomControl.h"
#include "GdiSoft2D.h"
#include "GdiSoft3D.h"
#include "resource.h"

// ガイドのミニマップクリック → 本体側の該当パーツを一時ハイライト。
// wParam: 0=Lib 1=Banner 2=Play 3=Sound 4=List 5=Lyrics 6=Tools
#ifndef WM_MP_HELP_HIGHLIGHT
#define WM_MP_HELP_HIGHLIGHT (WM_APP + 780)
#endif

class CMpHelpDlg : public CCustomBlurDialogExBase
{
	DECLARE_DYNAMIC(CMpHelpDlg)
public:
	explicit CMpHelpDlg(CWnd* pParent = nullptr);
	virtual ~CMpHelpDlg();
	enum { IDD = IDD_MP_CHEATSHEET };

	// モードレス単一インスタンス(親の OnDestroy から閉じるために公開)
	static CMpHelpDlg* Instance() { return s_inst; }

protected:
	enum {
		kChapterN = 5,     // 概要 / 再生 / リスト / Soft3D / ショートカット
		kMapRegionN = 7,   // ミニマップのクリック領域
		kStoryFrameN = 5,  // Soft2D ストーリーボードのコマ数
		kAnimTimerId = 1
	};

	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual void PostNcDestroy();
	virtual void OnOK();
	virtual void OnCancel();

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnClose();
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnTabSelChange(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnCopy();
	DECLARE_MESSAGE_MAP()

	// タブ・本文・Copy ボタンの配置(OnInitDialog / OnSize から)
	void LayoutChrome();

	CCustomTabCtrl m_tabs;
	CCustomStandardButton m_copy;
	CStatic m_body;            // 当たり判定用の場所取り(描画はダイアログ側)

	CRect m_bodyRc;            // 本文の描画矩形(クライアント座標)
	CRect m_mapRc[kMapRegionN];// 概要章のミニマップ領域(クライアント座標)

	// 本文用の永続メモリ面(33ms タイマーで毎フレーム作り直さない)
	CDC m_mem;
	CBitmap m_memBmp;
	CBitmap* m_memOldBmp;
	int m_memW;
	int m_memH;

	GdiSoft3D::Context m_demo3d;
	GdiSoft3D::Cam m_demoCam;
	GdiSoft2D::Context m_demo2d;
	DWORD m_animTick;
	UINT_PTR m_timer;

	int m_chapter;             // 0..kChapterN-1
	int m_contentBottom;       // 章本文の実描画下端(クライアント本文座標)
	int m_fittedChapter;       // 最後に窓高さを合わせた章(-1=未)
	CToolTipCtrl m_tooltip;
	CWnd* m_ownerMp;

	void FitWindowToContent();

	static CMpHelpDlg* s_inst;
};
