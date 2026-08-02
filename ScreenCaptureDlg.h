#pragma once
#include "afxwin.h"
#include "CCustomControl.h"
#include "resource.h"

class CScreenCaptureDlg;

// プレビュー上で選択・移動・四隅リサイズ (XSplit風簡易)
// DDX で直接アタッチする (二重 Subclass 禁止 — 起動クラッシュ防止)
class CScPreviewCtrl : public CStatic
{
	DECLARE_DYNAMIC(CScPreviewCtrl)
public:
	CScPreviewCtrl();
	virtual ~CScPreviewCtrl();
	void SetOwner(CScreenCaptureDlg* owner) { m_owner = owner; }
	void PaintToDC(CDC& dc);

protected:
	CScreenCaptureDlg* m_owner;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg LRESULT OnPrintClient(WPARAM wParam, LPARAM lParam);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	afx_msg void OnCaptureChanged(CWnd* pWnd);
};

// 画面キャプチャ → MP4 (H.264 + AAC)
class CScreenCaptureDlg : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CScreenCaptureDlg)
	friend class CScPreviewCtrl;

public:
	CScreenCaptureDlg(CWnd* pParent = NULL);
	virtual ~CScreenCaptureDlg();
	enum { IDD = IDD_SCREENCAPTURE };

	enum { SC_LAYER_MAX = 16, SC_AVAIL_MAX = 128 };
	enum { SC_MODE_PRIMARY = 0, SC_MODE_VIRTUAL = 1, SC_MODE_WINDOWS = 2 };
	enum {
		SC_HIT_NONE = 0, SC_HIT_BODY = 1,
		SC_HIT_TL = 2, SC_HIT_TR = 3, SC_HIT_BL = 4, SC_HIT_BR = 5
	};

	struct Layer {
		HWND hwnd;
		int x, y, w, h;
		TCHAR title[128];
		BOOL isMp;   // MP画面レイヤ
		BOOL hidden; // プレビュー/録画の映像から除外(音はシステム音側で可)
	};

	struct ComposeSnap {
		int mode;
		int canvasW;
		int canvasH;
		int layerCnt;
		Layer layers[SC_LAYER_MAX];
		BOOL includeMp;
		BOOL mpHidden;
		HWND mpHwnd;
		int mpX, mpY, mpW, mpH;
		HWND excludeHwnd; // キャプチャダイアログ自身（合成から除外）
	};

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual void PostNcDestroy();
	void CloseModeless();
	DECLARE_MESSAGE_MAP()

	void PersistUiToSavedata();
	void RefreshOpaqueUi();
	CString NormalizeOutPath(const CString& pathIn) const;
	void SetRecordingUi(BOOL recording);
	void UpdateElapsedUi();
	void PaintMetersFromPeaks();
	void StartPeakMonitor();
	void StopPeakMonitor();
	BOOL StartRecording();
	void StopRecording();
	static UINT __stdcall PeakMonitorThread(void* p);
	void UpdatePreview(BOOL forceCompose = FALSE);
	void PaintPreview(CDC& dc, const CRect& client);
	void RefreshComposeCache();
	BOOL GetPreviewMap(CRect& imageRect, float& scale, int& canvasW, int& canvasH) const;
	BOOL PreviewToCanvas(CPoint ptClient, int& cx, int& cy) const;
	CRect CanvasToPreview(int x, int y, int w, int h) const;
	int HitTestPreview(CPoint ptClient, int* outHandle) const;
	void BeginPreviewDrag(int layer, int handle, CPoint ptClient);
	void UpdatePreviewDrag(CPoint ptClient);
	void EndPreviewDrag();
	void DrawPreviewHud(CDC& dc, const CRect& imageRect, float scale, int canvasW, int canvasH);
	void RefreshAvailList();
	void RefreshLayerList();
	void SyncGeoEditsFromSel();
	void ApplyGeoEditsToSel();
	void EnableComposeUi(BOOL enable);
	void ResolveCanvasSize(int& outW, int& outH) const;
	void FitLayerIntoCanvas(Layer& L, int cw, int ch) const;
	void FitAllLayersIntoCanvas();
	void BuildComposeSnap(ComposeSnap& out) const;
	void AddLayerHwnd(HWND hwnd, BOOL isMp = FALSE);
	void SyncMpLayerFromCheck();
	void EnsureMpDefaultRect(Layer& L) const;
	HWND FindMediaPlayerHwnd() const;
	CString DefaultCaptureOutPath() const;
	CString RefreshCaptureOutPathTimestamp(const CString& pathIn) const;
	void TileLayers();
	void FitSelected(int scalePercent);
	void ToggleLayerHidden(int layerIdx);
	void ApplyPreviewTimer();
	int CurrentPreviewFps() const;
	static UINT __stdcall CaptureThread(void* p);

public:
	virtual BOOL OnInitDialog();
	afx_msg void OnBnClickedBrowse();
	afx_msg void OnBnClickedStart();
	afx_msg void OnBnClickedClose();
	afx_msg void OnBnClickedRefresh();
	afx_msg void OnBnClickedAdd();
	afx_msg void OnBnClickedRemove();
	afx_msg void OnBnClickedZUp();
	afx_msg void OnBnClickedZDown();
	afx_msg void OnBnClickedPick();
	afx_msg void OnBnClickedApplyGeo();
	afx_msg void OnBnClickedFit();
	afx_msg void OnBnClickedScale50();
	afx_msg void OnBnClickedScale100();
	afx_msg void OnBnClickedTile();
	afx_msg void OnBnClickedIncludeMp();
	afx_msg void OnCbnSelchangeMode();
	afx_msg void OnCbnSelchangeCanvas();
	afx_msg void OnCbnSelchangeFps();
	afx_msg void OnLbnSelchangeLayer();
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnDestroy();
	virtual void OnCancel();
	virtual void OnOK();

	CScPreviewCtrl m_preview;
	CCustomStatic m_modeLabel;
	CCustomComboBox m_mode;
	CCustomStatic m_canvasLabel;
	CCustomComboBox m_canvas;
	CCustomStatic m_fpsLabel;
	CCustomComboBox m_fps;
	CCustomCheckBox m_audio;
	CCustomCheckBox m_mic;
	CCustomCheckBox m_includeMp;
	CCustomStandardButton m_pick;
	CCustomStandardButton m_refresh;
	CCustomStatic m_availLabel;
	CCustomListBox m_avail;
	CCustomStatic m_layerLabel;
	CCustomListBox m_layer;
	CCustomStandardButton m_add;
	CCustomStandardButton m_remove;
	CCustomStandardButton m_zUp;
	CCustomStandardButton m_zDown;
	CCustomStatic m_geoLabel;
	CCustomEdit m_editX;
	CCustomEdit m_editY;
	CCustomEdit m_editW;
	CCustomEdit m_editH;
	CCustomStandardButton m_applyGeo;
	CCustomStandardButton m_fit;
	CCustomStandardButton m_scale50;
	CCustomStandardButton m_scale100;
	CCustomStandardButton m_tile;
	CCustomStatic m_pathLabel;
	CCustomEdit m_path;
	CCustomStandardButton m_browse;
	CCustomStandardButton m_start;
	CCustomStandardButton m_close;
	CCustomStatic m_status;
	CCustomStatic m_time;
	CToolTipCtrl m_tooltip;

	enum { SC_TIMER_UI = 81, SC_TIMER_PREV = 82 };

	HWND m_availHwnd[SC_AVAIL_MAX];
	int m_availCnt;
	Layer m_layers[SC_LAYER_MAX];
	int m_layerCnt;

	ComposeSnap m_recSnap;
	CRITICAL_SECTION m_snapCs;
	BOOL m_snapCsInit;

	HBITMAP m_cacheBmp;
	int m_cacheW;
	int m_cacheH;
	HDC m_cacheDc;
	HGDIOBJ m_cacheOld;
	BYTE* m_cacheBits;

	int m_dragHandle;
	int m_dragLayer;
	BOOL m_dragging;
	int m_dragStartCx, m_dragStartCy;
	int m_dragOrigX, m_dragOrigY, m_dragOrigW, m_dragOrigH;
	int m_hoverHandle;
	int m_hoverLayer;

	volatile LONG m_stop;
	volatile LONG m_run;
	volatile LONG m_lastHr;
	volatile LONG m_frameCnt;
	volatile LONG m_encFpsX10;   // 直近の録画(エンコード投入) FPS ×10
	volatile LONG m_prevFpsX10;  // 直近のプレビュー合成 FPS ×10
	DWORD m_prevFpsWinTick;
	int m_prevFpsWinCnt;
	HANDLE m_thread;
	HANDLE m_peakThread;
	volatile LONG m_peakStop;
	volatile LONG m_peakRun;
	BOOL m_uiLocked;
	BOOL m_stopping;
	BOOL m_everStarted;
	BOOL m_picking;
	CString m_outPath;
	BOOL m_withAudio;
	BOOL m_withMic;
	int m_fpsVal;
	DWORD m_startTick;

	volatile LONG m_peakMic;
	volatile LONG m_peakSys;
	volatile LONG m_peakMix;
	CCustomStatic m_meterMicL;
	CCustomStatic m_meterSysL;
	CCustomStatic m_meterMixL;
	CCustomLevelMeter m_meterMic;
	CCustomLevelMeter m_meterSys;
	CCustomLevelMeter m_meterMix;
};

void OpenScreenCaptureModeless(CWnd* parent);
void CloseScreenCaptureIfOpen();
