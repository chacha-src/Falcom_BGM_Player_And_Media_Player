#if !defined(AFX_DOUGA_H__27DB31FC_C694_448B_A81D_4F617F04BC9C__INCLUDED_)
#define AFX_DOUGA_H__27DB31FC_C694_448B_A81D_4F617F04BC9C__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// Douga.h : ヘッダー ファイル
//

#include "CCustomControl.h"

/////////////////////////////////////////////////////////////////////////////
// CDouga フレーム
struct StreamInfo
{
	DWORD streamIndex;
	GUID majorType;
	CString name;
	CString language;
};

class CDouga;

// EVR/VideoWindow の描画先。マウスは親(CDouga)へ転送して既存のドラッグ等を維持
class CDougaVideoSite : public CWnd
{
protected:
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
};

// 下部操作バー専用ホスト(動画HWNDと兄弟。重ね順競合を避ける)
class CDougaBarHost : public CWnd
{
public:
	CDougaBarHost();
	BOOL CreateBar(CDouga* owner);
	void LayoutBar();
	void ShowBar(BOOL show);
	void SyncSeekVol();
	void RefreshAero();
	void SetMediaInfoText(LPCWSTR text);
	int  BarHeight() const { return m_barH; }
	BOOL IsBarReady() const { return m_ready; }
	BOOL PtInBarClient(CPoint ptClientOfDouga) const;

	CCustomRangeSliderCtrl m_seek;
	CCustomSliderCtrl m_vol;
	CCustomStatic m_time, m_volL, m_volVal, m_info;
	CCustomStandardButton m_prev, m_rew, m_play, m_pause, m_stop, m_ff, m_next;
	CCustomStandardButton m_fade, m_mute, m_fs, m_sz1, m_sz15, m_sz2;
	CToolTipCtrl m_tip;

	void OnBnPrev();
	void OnBnRew();
	void OnBnPlay();
	void OnBnPause();
	void OnBnStop();
	void OnBnFf();
	void OnBnNext();
	void OnBnFade();
	void OnBnMute();
	void OnBnFs();
	void OnBnSz1();
	void OnBnSz15();
	void OnBnSz2();

protected:
	CDouga* m_owner;
	float m_dpi;
	int m_barH;
	int m_ready;
	int m_short;
	int m_laidShort; // 直前に適用した短縮ラベル状態(-1=未)
	int m_seekDrag;
	int m_muted;
	int m_mutePos;
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	DECLARE_MESSAGE_MAP()
};

class CDouga : public CFrameWnd
{
	DECLARE_DYNCREATE(CDouga)
protected:
	afx_msg void OnContextMenu(CWnd*, CPoint point);
public:
	CDouga();           // 動的生成に使用されるプロテクト コンストラクタ。
	BOOL Create(HWND);
	// 動画サイトとバーの配置。再入防止あり。描画系からは呼ばない。
	void ApplyVideoDest();
	void RefreshBarAero();
	void ToggleFullScreen();
	void RestoreDougaCursor();
	void RefreshBarMediaInfo();
	HWND GetVideoSiteHwnd() const { return m_videoSite.GetSafeHwnd(); }
	int  GetBarHeight() const;
	CDougaBarHost& GetBar() { return m_bar; }
	DWORD CntPin2(IAMStreamSelect* pFilter);
	BOOL SwitchStream(int streamType, int index);

	void play(int,CString str = L"");
	void plays(TCHAR* s);
	void plays2();
	void stops();
	void stop();
	void pause(int a);
	void seek(LONGLONG l);
	int SeekPoint(int file_bytes, float percent);
	void Filtersdown(IGraphBuilder *pGraph,WCHAR *filter=NULL);
	void Filtervideooff(IGraphBuilder *pGraph,WCHAR *filter=NULL);
	void Filtervideooff2(IGraphBuilder *pGraph, WCHAR *filter = NULL);
	void Filtervideooff3(IGraphBuilder *pGraph, WCHAR *filter = NULL);
	void Filtervideooff4(IGraphBuilder *pGraph);
	HRESULT EnumFilters (IGraphBuilder *pGraph,int no) ;
	double GetFrameRate(IGraphBuilder* pGraph);
	BOOL EnumeratePinsForStreams(IGraphBuilder* pGraph,
		std::vector<StreamInfo>& audioStreams,
		std::vector<StreamInfo>& videoStreams,
		std::vector<StreamInfo>& subtitleStreams);
	BOOL GetStreamInfo(IGraphBuilder* pGraph, std::vector<StreamInfo>& audioStreams,
		std::vector<StreamInfo>& videoStreams, std::vector<StreamInfo>& subtitleStreams);
	void ConnectSubtitlePins(IGraphBuilder* pGraph);
	void ConnectSubtitleToVSFilter(IGraphBuilder* pGraph, IBaseFilter* pVSFilter);
	void ReplaceSourceWithLAV(IGraphBuilder* pGraph, LPCWSTR filename);
	void ConnectSubtitleWithDirectVobSub(IGraphBuilder* pGraph);

	struct StreamMapping {
		int videoStart;
		int videoCount;
		int audioStart;
		int audioCount;
		int subtitleStart;
		int subtitleCount;
	} streamMap;
	std::vector<StreamInfo> audioStreams, videoStreams, subtitleStreams;


	void DeleteAudioMenuItems(CMenu& menu);
	void DeleteEmptyMenuItems(CMenu& menu, CString* streamNames, int maxCount, UINT baseID);
	void UpdateStreamMenu(CMenu* pMenu, CString* streamNames, int maxCount, LPCWSTR prefix);
	void LocalizeDougaMenu(CMenu* pPopup);
	void LocalizeDougaMenu1(CMenu* pPopup);
	void ShowDougaContextMenu(CPoint point);
	int  FindDougaStreamSubMenu(CMenu* pPopup, UINT firstItemId);

	void DumpFilterGraph();

	BYTE toc[100];
	int filesize;
	double aa;
	CDC *cdc0,dc;
	CBitmap bmp;
	int pcnt;

	CDougaVideoSite m_videoSite; // DirectShow/EVR の描画先(バーと兄弟)
	CDougaBarHost m_bar;
	int m_applyBusy;       // ApplyVideoDest 再入防止
	int m_inSizeMove;      // リサイズドラッグ中(バー子の再配置を遅延)

// オーバーライド
	// ClassWizard は仮想関数のオーバーライドを生成します。
	//{{AFX_VIRTUAL(CDouga)
	public:
	virtual BOOL DestroyWindow();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	protected:
	virtual void PostNcDestroy();
	virtual LRESULT DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	//}}AFX_VIRTUAL

// インプリメンテーション
public:
	virtual ~CDouga();

	// 生成されたメッセージ マップ関数
	//{{AFX_MSG(CDouga)
	afx_msg void OnSizing(UINT fwSide, LPRECT pRect);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnEnterSizeMove();
	afx_msg void OnExitSizeMove();
	afx_msg void OnClose();
	afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
	afx_msg void OnMenuitem32771();
	afx_msg void OnMenuitem32772();
	afx_msg void OnMenuitem32773();
	afx_msg void OnMenuitem32774();
	afx_msg void OnST1();
	afx_msg void OnST2();
	afx_msg void OnST3();
	afx_msg void OnST4();
	afx_msg void OnST5();
	afx_msg void OnST6();
	afx_msg void OnST7();
	afx_msg void OnST8();
	afx_msg void OnST9();
	afx_msg void OnST10();
	afx_msg void OnST11();
	afx_msg void OnST12();
	afx_msg void OnST13();
	afx_msg void OnST14();
	afx_msg void OnST15();
	afx_msg void OnST16();
	afx_msg void OnST17();
	afx_msg void OnST18();
	afx_msg void OnST19();
	afx_msg void OnST20();
	afx_msg void OnST21();
	afx_msg void OnST22();
	afx_msg void OnST23();
	afx_msg void OnST24();
	afx_msg void OnST25();
	afx_msg void OnST26();
	afx_msg void OnST27();
	afx_msg void OnST28();
	afx_msg void OnST29();
	afx_msg void OnST30();
	afx_msg void OnST31();
	afx_msg void OnST32();
	afx_msg void OnST33();
	afx_msg void OnST34();
	afx_msg void OnST35();
	afx_msg void OnST36();
	afx_msg void OnST37();
	afx_msg void OnST38();
	afx_msg void OnST39();
	afx_msg void OnST40();
	afx_msg	void OnMV1();
	afx_msg void OnMV2();
	afx_msg void OnMV3();
	afx_msg void OnMV4();
	afx_msg void OnMV5();
	afx_msg void OnMV6();
	afx_msg void OnMV7();
	afx_msg void OnMV8();
	afx_msg void OnMV9();
	afx_msg void OnMV10();
	afx_msg void OnETC1();
	afx_msg void OnETC2();
	afx_msg void OnETC3();
	afx_msg void OnETC4();
	afx_msg void OnETC5();
	afx_msg void OnETC6();
	afx_msg void OnETC7();
	afx_msg void OnETC8();
	afx_msg void OnETC9();
	afx_msg void OnETC10();
	afx_msg void OnETC11();
	afx_msg void OnETC12();
	afx_msg void OnETC13();
	afx_msg void OnETC14();
	afx_msg void OnETC15();
	afx_msg void OnETC16();
	afx_msg void OnETC17();
	afx_msg void OnETC18();
	afx_msg void OnETC19();
	afx_msg void OnETC20();
	afx_msg void OnETC21();
	afx_msg void OnETC22();
	afx_msg void OnETC23();
	afx_msg void OnETC24();
	afx_msg void OnETC25();
	afx_msg void OnETC26();
	afx_msg void OnETC27();
	afx_msg void OnETC28();
	afx_msg void OnETC29();
	afx_msg void OnETC30();
	afx_msg void OnETC31();
	afx_msg void OnETC32();
	afx_msg void OnETC33();
	afx_msg void OnETC34();
	afx_msg void OnETC35();
	afx_msg void OnETC36();
	afx_msg void OnETC37();
	afx_msg void OnETC38();
	afx_msg void OnETC39();
	afx_msg void OnETC40();
	afx_msg void OnPaint();
	afx_msg LRESULT OnNcHitTest(CPoint point);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO FAR* lpMMI);
	afx_msg int OnMouseActivate(CWnd* pDesktopWnd, UINT nHitTest, UINT message);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
#if WIN64
	afx_msg void OnTimer(UINT_PTR nIDEvent);
#else
	afx_msg void OnTimer(UINT nIDEvent);
#endif
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
//	afx_msg void OnRButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
protected:
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
public:
	afx_msg void OnWindowPosChanging(WINDOWPOS* lpwndpos);
	afx_msg void OnWindowPosChanged(WINDOWPOS* lpwndpos);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnRButtonDown(UINT nFlags, CPoint point);
//	afx_msg void OnNcLButtonDown(UINT nHitTest, CPoint point);
	afx_msg void OnNcRButtonDown(UINT nHitTest, CPoint point);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnNcLButtonDblClk(UINT nHitTest, CPoint point);
	afx_msg void On32775();
	afx_msg void OnDropFiles(HDROP hDropInfo);
	afx_msg void OnNcMouseMove(UINT nHitTest, CPoint point);
	afx_msg void OnNcDestroy();
	afx_msg void OnNcRButtonUp(UINT nHitTest, CPoint point);
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnDougaMenuPlay();
	afx_msg void OnDougaMenuStop();
	afx_msg void OnDougaMenuPrev();
	afx_msg void OnDougaMenuNext();
	afx_msg void OnDougaMenuRew();
	afx_msg void OnDougaMenuFf();
	afx_msg void OnDougaMenuMute();
	afx_msg void OnDougaMenuFs();
	afx_msg void OnDougaMenuFade();
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ は前行の直前に追加の宣言を挿入します。

#endif // !defined(AFX_DOUGA_H__27DB31FC_C694_448B_A81D_4F617F04BC9C__INCLUDED_)
