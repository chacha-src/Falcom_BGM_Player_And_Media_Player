#pragma once
// CAnalyzerDlg.h : 簡易波形アナライザー
// 上部: PCM 波形(横スクロール・バックバッファ) / 下部: 周波数特性
// ステレオ〜7.1ch(最大8)対応。高さに余裕がある分だけ ch を並べる。
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
	static constexpr int RING_SAMPLES = 8192;  // FFT/波形用
	static constexpr int FFT_SIZE = 2048;      // 低域分解能(≈21Hz@44.1k)
	static constexpr int SPEC_BINS = 120;      // 対数軸の表示点数

	void FeedPCM(const void* pData, int frames, int sampleRate, int bits, int channels);
	void ResumePlaybackFeed();
	void PauseFeed();
	void ResetPlaybackState();
	void DetachForDestroy();

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
	virtual BOOL PreTranslateMessage(MSG* pMsg);

private:
	void UpdateSpectrumFromRing();
	void FullRedrawWave(COLORREF bg);
	// 戻り値: 横スクロールした px。全面再描画時は -1、何もしなければ 0
	int ScrollWaveAndDrawNew(COLORREF bg);
	void RedrawSpectrum(COLORREF bg);
	void Present(CDC& dc, const CRect& rc, BOOL bAero);
	void ReleaseBuffers();
	bool EnsureWaveBuffer(CDC& refDC, int w, int h);
	bool EnsureSpecBuffer(CDC& refDC, int w, int h);
	bool SnapshotRing(int& outWrite, int& outFilled, int& outChannels);
	int VisibleChannelCount(int waveH) const;
	static LPCTSTR ChannelLabel(int ch, int channels);

	CRITICAL_SECTION m_cs;
	bool m_feedEnabled = false;
	int m_sampleRate = 44100;
	int m_channels = 2;

	// チャンネル別リング(最新 RING_SAMPLES)
	std::vector<float> m_ring[CH_MAX];
	std::vector<float> m_ringSnap[CH_MAX]; // 描画用スナップショット(ロック外で描く)
	int m_ringWrite = 0;
	int m_ringFilled = 0;
	int m_accSamples = 0;          // 波形1px 分の蓄積
	int m_samplesPerCol = 64;      // 1px あたりサンプル数
	int m_pendingScroll = 0;       // 未描画の横スクロール量(px)

	// 波形バックバッファ(幅=表示幅、1px=1カラム)
	CDC m_waveDC;
	CBitmap m_waveBmp;
	CBitmap* m_waveOld = nullptr;
	CDC m_waveScratchDC;
	CBitmap m_waveScratchBmp;
	CBitmap* m_waveScratchOld = nullptr;
	int m_waveW = 0, m_waveH = 0;
	bool m_waveReady = false;
	int m_waveLayoutCh = 0;        // バッファ作成時の表示 ch 数

	// スペクトル用
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

	CFont m_font;

#if CCUSTOM_AERO_SUPPORT
	// ピアノロールと同様: クロマ→α 変換結果を保持し、差分更新する
	CCC_ChromaBlitCache m_chromaCache;
	bool m_chromaReady = false;
	int m_chromaW = 0, m_chromaH = 0;
	int m_lastWaveScroll = 0;
#endif
};
