#pragma once
// CAnalyzerDlg.h : 簡易波形アナライザー
// 上部: PCM 波形(横スクロール・バックバッファ) / 下部: 周波数特性
// ステレオ〜7.1ch(最大8)対応。周波数特性は右クリックで分割レイアウト切替。
// Ozone 風: 塗/線/バー、ピークホールド、EQオーバーレイ、ホバー読取、レベルメーター、フリーズ。
#include "afxdialogex.h"
#include "CCustomControl.h"
#include <vector>

class CAnalyzerDlg : public CCustomBlurDialogExBase
{
	DECLARE_DYNAMIC(CAnalyzerDlg)

public:
	CAnalyzerDlg(CWnd* pParent = nullptr);
	virtual ~CAnalyzerDlg();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_ANALYZER };
#endif

	static constexpr int CH_MAX = 8;
	static constexpr int RING_SAMPLES = 8192;
	static constexpr int FFT_SIZE = 2048;
	static constexpr int SPEC_BINS = 120;
	static constexpr int EQ_OVERLAY_BANDS = 15;

	enum SpecLayout {
		SpecOverlay = 0, // 全ch重ね描き
		SpecSplitV = 1,  // 上下分割
		SpecSplitH = 2,  // 左右分割
		SpecGrid4 = 3,   // 2x2(4ch+)
		SpecGrid8 = 4    // 2x4(8ch)
	};

	enum SpecStyle {
		StyleFill = 0, // 塗+線(Ozone風)
		StyleLine = 1, // 線のみ
		StyleBars = 2  // バー
	};

	void FeedPCM(const void* pData, int frames, int sampleRate, int bits, int channels);
	void ResumePlaybackFeed();
	void PauseFeed();
	void ResetPlaybackState();
	void DetachForDestroy();
	void RequestSyncFromMainUi();

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	DECLARE_MESSAGE_MAP()

	afx_msg void OnPaint();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnClose();
	afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnMouseLeave();
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnSpecLayoutOverlay();
	afx_msg void OnSpecLayoutSplitV();
	afx_msg void OnSpecLayoutSplitH();
	afx_msg void OnSpecLayoutGrid4();
	afx_msg void OnSpecLayoutGrid8();
	afx_msg void OnSpecStyleFill();
	afx_msg void OnSpecStyleLine();
	afx_msg void OnSpecStyleBars();
	afx_msg void OnTogglePeakHold();
	afx_msg void OnToggleEqOverlay();
	afx_msg void OnToggleFreeze();
	afx_msg void OnResetPeakHold();
	afx_msg LRESULT OnSpecAnalysisDone(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnPresentRequest(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnSyncRequest(WPARAM wParam, LPARAM lParam);
	virtual BOOL PreTranslateMessage(MSG* pMsg);

private:
	void UpdateSpectrumFromRing();
	void FullRedrawWave(COLORREF bg);
	int ScrollWaveAndDrawNew(COLORREF bg, int maxScroll = 0);
	void RedrawSpectrum(COLORREF bg);
	void DrawSpecPanel(CDC& dc, const CRect& plot, int chBegin, int chCount,
		float spec[][SPEC_BINS], float peak[][SPEC_BINS], int channels, int sr, bool drawTitle);
	void DrawEqOverlay(CDC& dc, const CRect& plot, float nyquist);
	void DrawLevelMeters(CDC& dc, const CRect& waveRc, COLORREF bg);
	void DrawHoverReadout(CDC& dc, const CRect& clientRc);
	void Present(CDC& dc, const CRect& rc, BOOL bAero);
	void ReleaseBuffers();
	bool EnsureWaveBuffer(CDC& refDC, int w, int h);
	bool EnsureSpecBuffer(CDC& refDC, int w, int h);
	bool EnsureFrameBuffer(CDC& refDC, int w, int h);
	void KickUiPresent();
	bool SnapshotRing(int& outWrite, int& outFilled, int& outChannels);
	int VisibleChannelCount(int waveH) const;
	static LPCTSTR ChannelLabel(int ch, int channels);
	void SetSpecLayout(int layout);
	void SetSpecStyle(int style);
	void ResetPeakHold();
	bool UpdateHoverFromPoint(CPoint ptClient); // true=表示内容が変わった
	void StartSpecWorker();
	void StopSpecWorker();
	void RequestSpecAnalysis();
	static DWORD WINAPI SpecWorkerEntry(LPVOID param);
	DWORD SpecWorkerLoop();

	CRITICAL_SECTION m_cs;
	bool m_feedEnabled = false;
	int m_sampleRate = 44100;
	int m_channels = 2;
	int m_specLayout = SpecOverlay;
	int m_specStyle = StyleFill;
	bool m_peakHold = true;
	bool m_eqOverlay = true;
	bool m_frozen = false;

	std::vector<float> m_ring[CH_MAX];
	std::vector<float> m_ringSnap[CH_MAX];
	int m_ringWrite = 0;
	int m_ringFilled = 0;
	int m_accSamples = 0;
	int m_samplesPerCol = 64;
	int m_pendingScroll = 0;

	CDC m_waveDC;
	CBitmap m_waveBmp;
	CBitmap* m_waveOld = nullptr;
	CDC m_waveScratchDC;
	CBitmap m_waveScratchBmp;
	CBitmap* m_waveScratchOld = nullptr;
	int m_waveW = 0, m_waveH = 0;
	bool m_waveReady = false;
	int m_waveLayoutCh = 0;

	float m_specDb[CH_MAX][SPEC_BINS];
	float m_specPeakDb[CH_MAX][SPEC_BINS];
	std::vector<float> m_fftRe;
	std::vector<float> m_fftIm;
	std::vector<float> m_fftWindow;
	bool m_specDirty = true;

	CDC m_specDC;
	CBitmap m_specBmp;
	CBitmap* m_specOld = nullptr;
	int m_specW = 0, m_specH = 0;
	bool m_specReady = false;

	// 最終合成バッファ(1回 BitBlt で点滅抑制)
	CDC m_frameDC;
	CBitmap m_frameBmp;
	CBitmap* m_frameOld = nullptr;
	int m_frameW = 0, m_frameH = 0;

	// ホバー読取用(スペクトラム各パネル。BB座標 → クライアントは split 加算)
	static constexpr int HOVER_PLOT_MAX = CH_MAX;
	CRect m_hoverPlots[HOVER_PLOT_MAX];
	int m_hoverPlotCh[HOVER_PLOT_MAX] = {};
	int m_hoverPlotCount = 0;
	CRect m_hoverPlot; // 現在ホバー中のパネル(描画用)
	int m_hoverSplitY = 0;
	bool m_hoverValid = false;
	bool m_hoverChanged = false;
	bool m_trackingMouse = false;
	float m_hoverHz = 0.0f;
	float m_hoverDb = -96.0f;
	int m_hoverCh = 0;
	int m_hoverBin = -1;

	// レベルメーター: すべて RMS 基準(バー=現在 / 白線=RMSピークホールド)
	float m_meterPeak[2] = { 0.0f, 0.0f }; // 参考用サンプルピーク(描画未使用)
	float m_meterHold[2] = { 0.0f, 0.0f };
	float m_meterRms[2] = { 0.0f, 0.0f };
	float m_waveDispPeak = 0.25f; // 波形表示用の追従ピーク(小さいほど拡大)

	// UI 提示要求(音声/ワーカーは Invalidate せず PostMessage 合流 — ピアノロールと同じ自由走行)
	volatile LONG m_presentPosted = 0;
	volatile LONG m_syncPosted = 0;
	DWORD m_lastSyncPostTick = 0;

	CFont m_font;

	HANDLE m_hSpecThread = nullptr;
	HANDLE m_hSpecWake = nullptr;
	volatile LONG m_specStop = 0;
	volatile LONG m_specNeed = 0;

#if CCUSTOM_AERO_SUPPORT
	CCC_ChromaBlitCache m_chromaCache;
	bool m_chromaReady = false;
	int m_chromaW = 0, m_chromaH = 0;
	int m_lastWaveScroll = 0;
#endif
};
