// CAnalyzerDlg.cpp : 簡易波形アナライザー(スクロールBB・多ch・周波数特性)
#include "stdafx.h"
#include "ogg.h"
#include "CAnalyzerDlg.h"
#include "ProAudio.h"
#include "oggDlg.h"
#include "CEqualizer.h"
#include <cmath>
#include <algorithm>

extern COggDlg* og;

IMPLEMENT_DYNAMIC(CAnalyzerDlg, CCustomBlurDialogExBase)

namespace
{
	static constexpr COLORREF ANALYZER_CHROMA_KEY = RGB(18, 20, 28);
	static constexpr COLORREF ANALYZER_BG = RGB(18, 20, 28);
	// アクリル時、ラベル下地を CHROMA にすると透過＋縁が消えて L/R が見えない
	static constexpr COLORREF ANALYZER_LABEL_PLATE = RGB(32, 36, 48);

	static const COLORREF kChColor[CAnalyzerDlg::CH_MAX] = {
		RGB(80, 200, 255), RGB(255, 140, 180), RGB(120, 230, 140), RGB(255, 200, 80),
		RGB(180, 140, 255), RGB(80, 220, 200), RGB(255, 160, 100), RGB(200, 200, 220)
	};

	inline float SampleToFloat(const void* pData, int bits, int frame, int ch, int channels)
	{
		if (!pData || channels <= 0) return 0.0f;
		if (bits == 16) {
			const short* s = (const short*)pData;
			return (float)s[frame * channels + ch] / 32768.0f;
		}
		if (bits == 24) {
			const unsigned char* b = (const unsigned char*)pData;
			const int idx = (frame * channels + ch) * 3;
			int v = b[idx] | (b[idx + 1] << 8) | (b[idx + 2] << 16);
			if (v & 0x800000) v |= ~0xFFFFFF;
			return (float)v / 8388608.0f;
		}
		if (bits == 32) {
			// DS/アップスケール出力は int32（IEEE float ではない）。Speana/ピアノロールと同じ正規化。
			const int* s = (const int*)pData;
			return (float)((double)s[frame * channels + ch] / 2147483648.0);
		}
		if (bits == 8) {
			const unsigned char* u = (const unsigned char*)pData;
			return ((float)u[frame * channels + ch] - 128.0f) / 128.0f;
		}
		return 0.0f;
	}

	inline float AmpToDb(float a)
	{
		const float x = fabsf(a);
		if (x < 1e-8f) return -96.0f;
		float db = 20.0f * log10f(x);
		if (db < -96.0f) db = -96.0f;
		if (db > 0.0f) db = 0.0f;
		return db;
	}

	void FftRadix2(float* re, float* im, int n)
	{
		int j = 0;
		for (int i = 1; i < n; ++i) {
			int bit = n >> 1;
			for (; j & bit; bit >>= 1) j ^= bit;
			j ^= bit;
			if (i < j) {
				std::swap(re[i], re[j]);
				std::swap(im[i], im[j]);
			}
		}
		for (int len = 2; len <= n; len <<= 1) {
			const float ang = -6.28318530718f / (float)len;
			const float wlenRe = cosf(ang);
			const float wlenIm = sinf(ang);
			for (int i = 0; i < n; i += len) {
				float wRe = 1.0f, wIm = 0.0f;
				const int half = len >> 1;
				for (int k = 0; k < half; ++k) {
					const float uRe = re[i + k];
					const float uIm = im[i + k];
					const float vRe = re[i + k + half] * wRe - im[i + k + half] * wIm;
					const float vIm = re[i + k + half] * wIm + im[i + k + half] * wRe;
					re[i + k] = uRe + vRe;
					im[i + k] = uIm + vIm;
					re[i + k + half] = uRe - vRe;
					im[i + k + half] = uIm - vIm;
					const float nWRe = wRe * wlenRe - wIm * wlenIm;
					wIm = wRe * wlenIm + wIm * wlenRe;
					wRe = nWRe;
				}
			}
		}
	}

	// 対数軸の帯域境界(edge=0..bins → 20Hz..nyquist)
	float SpecEdgeHz(int edge, int bins, float nyquist)
	{
		const float fMin = 20.0f;
		const float fMax = (std::max)(fMin * 1.01f, nyquist);
		if (bins <= 0) return fMin;
		const float t = (float)edge / (float)bins;
		return fMin * powf(fMax / fMin, t);
	}

	float SpecCenterHz(int bin, int bins, float nyquist)
	{
		return sqrtf(SpecEdgeHz(bin, bins, nyquist) * SpecEdgeHz(bin + 1, bins, nyquist));
	}

	bool CopyUnicodeToClipboard(HWND hwnd, const CString& text)
	{
		if (!hwnd || !::OpenClipboard(hwnd))
			return false;
		::EmptyClipboard();
		const SIZE_T bytes = (SIZE_T)(text.GetLength() + 1) * sizeof(WCHAR);
		HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
		if (!hMem) {
			::CloseClipboard();
			return false;
		}
		void* p = ::GlobalLock(hMem);
		if (!p) {
			::GlobalFree(hMem);
			::CloseClipboard();
			return false;
		}
		memcpy(p, (LPCWSTR)text, (size_t)bytes);
		::GlobalUnlock(hMem);
		if (!::SetClipboardData(CF_UNICODETEXT, hMem)) {
			::GlobalFree(hMem);
			::CloseClipboard();
			return false;
		}
		::CloseClipboard();
		return true;
	}

	// oggDlg_ds.cpp の EQ_FREQS と同じ帯域(オーバーレイ用ローカル複製)
	static const float kEqFreqs[CAnalyzerDlg::EQ_OVERLAY_BANDS] = {
		25.0f, 40.0f, 63.0f, 100.0f, 160.0f,
		250.0f, 400.0f, 630.0f, 1000.0f, 1600.0f,
		2500.0f, 4000.0f, 6300.0f, 10000.0f, 16000.0f
	};

	// savedata.eq[0..14]: 100=0dB、偏差*0.12 → ±12dB (equaliser と同じ)
	inline float EqSliderToDb(int slider)
	{
		return ((float)slider - 100.0f) * 0.12f;
	}

	inline int EqDbToSlider(float gainDb)
	{
		int v = (int)(gainDb / 0.12f + 100.0f + (gainDb >= 0.0f ? 0.5f : -0.5f));
		if (v < 0) v = 0;
		if (v > 200) v = 200;
		return v;
	}

	void GetZoomFreqRange(int zoom, float nyquist, float& fMin, float& fMax)
	{
		fMin = 20.0f;
		fMax = (std::max)(fMin * 1.01f, nyquist);
		if (zoom == CAnalyzerDlg::ZoomLow) {
			fMax = (std::min)(fMax, 250.0f);
		}
		else if (zoom == CAnalyzerDlg::ZoomMid) {
			fMin = 250.0f;
			fMax = (std::min)(fMax, 4000.0f);
			if (fMax <= fMin) fMax = fMin * 1.01f;
		}
		else if (zoom == CAnalyzerDlg::ZoomHigh) {
			fMin = 4000.0f;
			if (fMin >= fMax) fMin = fMax * 0.5f;
		}
	}

	inline float HzToPlotX(float hz, const CRect& plot, float fMin, float fMax)
	{
		float t = logf((std::max)(hz, fMin) / fMin) / logf(fMax / fMin);
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		return (float)(plot.left + (int)(t * plot.Width()));
	}

	inline float PlotXToHz(int x, const CRect& plot, float fMin, float fMax)
	{
		if (plot.Width() <= 0) return fMin;
		float t = (float)(x - plot.left) / (float)plot.Width();
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		return fMin * powf(fMax / fMin, t);
	}

	COLORREF WaterfallColor(float db)
	{
		// -96..0 → 青→緑→黄→白
		float t = (db + 96.0f) / 96.0f;
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		int r, g, b;
		if (t < 0.33f) {
			float u = t / 0.33f;
			r = (int)(20 + u * 20);
			g = (int)(30 + u * 120);
			b = (int)(60 + u * 160);
		}
		else if (t < 0.66f) {
			float u = (t - 0.33f) / 0.33f;
			r = (int)(40 + u * 180);
			g = (int)(150 + u * 80);
			b = (int)(220 - u * 180);
		}
		else {
			float u = (t - 0.66f) / 0.34f;
			r = (int)(220 + u * 35);
			g = (int)(230 + u * 25);
			b = (int)(40 + u * 200);
		}
		if (r > 255) r = 255;
		if (g > 255) g = 255;
		if (b > 255) b = 255;
		return RGB(r, g, b);
	}

	// TP/LUFS は簡易計測。ツールチップでその旨を明示する(誤解防止)。
	inline LPCTSTR AnalyzerTpLufsTipText()
	{
		return LL14(
			L"簡易計測の目安です。TP はサンプル間補間による概算ピーク、LUFS は K特性を省いた瞬時ラウドネスの近似で、放送規格の厳密な値ではありません。",
			L"Rough in-app estimate. TP is an approximate inter-sample peak and LUFS is a momentary loudness approximation without full K-weighting; not a broadcast-accurate measurement.",
			L"Estimation approximative. TP est un pic inter-echantillons approche et LUFS une approximation de la sonie momentanee sans ponderation K complete; ce ne sont pas des mesures normalisees.",
			L"Stima approssimativa. TP e un picco inter-campione approssimato e LUFS una loudness momentanea senza ponderazione K completa; non sono misure da broadcast.",
			L"Estimacion aproximada. TP es un pico entre muestras aproximado y LUFS una sonoridad momentanea sin ponderacion K completa; no son medidas de norma broadcast.",
			L"간이 측정 값입니다. TP는 샘플 간 보간에 의한 근사 피크, LUFS는 K 가중을 생략한 순시 라우드니스 근사이며 방송 규격의 정확한 값이 아닙니다.",
			L"仅为简易估算。TP 为采样间插值的近似峰值，LUFS 为省略 K 计权的瞬时响度近似，并非广播标准的精确值。",
			L"تقدير تقريبي فقط. TP ذروة تقريبية بين العينات، وLUFS تقريب للجهارة اللحظية بدون ترجيح K كامل، وليست قياسات معيارية للبث.",
			L"Приблизительная оценка. TP - примерный межсэмпловый пик, LUFS - приближение мгновенной громкости без полного K-взвешивания; это не вещательные измерения.",
			L"Grobe Schatzung. TP ist ein naherungsweiser Inter-Sample-Peak und LUFS eine momentane Lautheitsnaherung ohne vollstandige K-Bewertung; keine sendetauglichen Messwerte.",
			L"Estimativa aproximada. TP e um pico entre amostras aproximado e LUFS uma aproximacao de loudness momentanea sem ponderacao K completa; nao sao medidas de broadcast.",
			L"Ruwe schatting. TP is een benaderde inter-sample piek en LUFS een momentane luidheidsbenadering zonder volledige K-weging; geen broadcast-nauwkeurige meting.",
			L"Szacunkowy pomiar. TP to przyblizony szczyt miedzy probkami, a LUFS to przyblizenie chwilowej glosnosci bez pelnego wazenia K; to nie sa pomiary emisyjne.",
			L"Kaba bir tahmindir. TP ornekler arasi yaklasik tepe, LUFS ise tam K agirliklandirmasi olmayan anlik ses yuksekligi yaklasimidir; yayin standardi olcumu degildir.");
	}

	enum {
		IDM_SPEC_OVERLAY = 42001,
		IDM_SPEC_SPLIT_V = 42002,
		IDM_SPEC_SPLIT_H = 42003,
		IDM_SPEC_GRID4 = 42004,
		IDM_SPEC_GRID8 = 42005,
		IDM_STYLE_FILL = 42010,
		IDM_STYLE_LINE = 42011,
		IDM_STYLE_BARS = 42012,
		IDM_STYLE_CUBASE = 42013,
		IDM_STYLE_SPAN = 42014,
		IDM_STYLE_ABLETON = 42015,
		IDM_STYLE_FABFILTER = 42016,
		IDM_PEAK_HOLD = 42020,
		IDM_EQ_OVERLAY = 42021,
		IDM_FREEZE = 42022,
		IDM_RESET_PEAK = 42023,
		IDM_LEVEL_METER = 42024,
		IDM_ALWAYS_ON_TOP = 42025,
		IDM_CLEAR_DISPLAY = 42026,
		IDM_COPY_HOVER = 42027,
		IDM_COPY_PEAK = 42028,
		IDM_COPY_LEVELS = 42029,
		IDM_WAVE_SCROLL = 42030,
		IDM_WAVE_TRIGGER = 42031,
		IDM_LOWER_SPECTRUM = 42032,
		IDM_LOWER_WATERFALL = 42033,
		IDM_LOWER_PHASE = 42034,
		IDM_SPEC_DIFF = 42035,
		IDM_SPEC_SNAP = 42036,
		IDM_SPEC_SNAP_CLEAR = 42037,
		IDM_ZOOM_FULL = 42038,
		IDM_ZOOM_LOW = 42039,
		IDM_ZOOM_MID = 42040,
		IDM_ZOOM_HIGH = 42041,
		IDM_MARKER_ADD = 42042,
		IDM_MARKER_REMOVE = 42043,
		IDM_MARKER_CLEAR = 42044,
		IDM_CORR_METER = 42045,
		IDM_WAVE_SPEED_BASE = 42100, // +0..WAVE_SPEED_COUNT-1
		WM_ANALYZER_SPEC_DONE = WM_APP + 510,
		WM_ANALYZER_PRESENT = WM_APP + 511,
		WM_ANALYZER_SYNC = WM_APP + 512
	};
}

CAnalyzerDlg::CAnalyzerDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(IDD_ANALYZER, pParent)
{
	InitializeCriticalSection(&m_cs);
	for (int c = 0; c < CH_MAX; ++c) {
		m_ring[c].assign(RING_SAMPLES, 0.0f);
		m_ringSnap[c].assign(RING_SAMPLES, 0.0f);
		m_meterPeak[c] = 0.0f;
		m_meterHold[c] = 0.0f;
		m_meterRms[c] = 0.0f;
	}
	m_fftRe.assign(FFT_SIZE, 0.0f);
	m_fftIm.assign(FFT_SIZE, 0.0f);
	m_fftWindow.assign(FFT_SIZE, 0.0f);
	for (int i = 0; i < FFT_SIZE; ++i)
		m_fftWindow[i] = 0.5f * (1.0f - cosf(6.28318530718f * (float)i / (float)(FFT_SIZE - 1)));
	for (int c = 0; c < CH_MAX; ++c) {
		for (int b = 0; b < SPEC_BINS; ++b) {
			m_specDb[c][b] = -96.0f;
			m_specPeakDb[c][b] = -96.0f;
			m_specSnapDb[c][b] = -96.0f;
		}
		for (int r = 0; r < WF_ROWS; ++r) {
			for (int b = 0; b < SPEC_BINS; ++b)
				m_wfHist[c][r][b] = -96.0f;
		}
	}
	m_wfWrite = 0;
	m_wfFilled = 0;
	m_specSnapValid = false;
	m_tpLin = 0.0f;
	m_tpHold = 0.0f;
	m_lufsMom = -70.0f;
	m_lufsSum = 0.0;
	m_lufsCount = 0.0;
	m_eqDrag = false;
	m_eqDragBand = -1;
	m_eqDragPlot.SetRectEmpty();
	m_tpLufsTipRc.SetRectEmpty();
}

CAnalyzerDlg::~CAnalyzerDlg()
{
	StopSpecWorker();
	ReleaseBuffers();
	DeleteCriticalSection(&m_cs);
}

void CAnalyzerDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAnalyzerDlg, CCustomBlurDialogExBase)
	ON_WM_PAINT()
	ON_WM_TIMER()
	ON_WM_SIZE()
	ON_WM_CLOSE()
	ON_WM_SHOWWINDOW()
	ON_WM_ERASEBKGND()
	ON_WM_CONTEXTMENU()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_LBUTTONDBLCLK()
	ON_COMMAND(IDM_SPEC_OVERLAY, &CAnalyzerDlg::OnSpecLayoutOverlay)
	ON_COMMAND(IDM_SPEC_SPLIT_V, &CAnalyzerDlg::OnSpecLayoutSplitV)
	ON_COMMAND(IDM_SPEC_SPLIT_H, &CAnalyzerDlg::OnSpecLayoutSplitH)
	ON_COMMAND(IDM_SPEC_GRID4, &CAnalyzerDlg::OnSpecLayoutGrid4)
	ON_COMMAND(IDM_SPEC_GRID8, &CAnalyzerDlg::OnSpecLayoutGrid8)
	ON_COMMAND(IDM_STYLE_FILL, &CAnalyzerDlg::OnSpecStyleFill)
	ON_COMMAND(IDM_STYLE_LINE, &CAnalyzerDlg::OnSpecStyleLine)
	ON_COMMAND(IDM_STYLE_BARS, &CAnalyzerDlg::OnSpecStyleBars)
	ON_COMMAND(IDM_STYLE_CUBASE, &CAnalyzerDlg::OnSpecStyleCubase)
	ON_COMMAND(IDM_STYLE_SPAN, &CAnalyzerDlg::OnSpecStyleSpan)
	ON_COMMAND(IDM_STYLE_ABLETON, &CAnalyzerDlg::OnSpecStyleAbleton)
	ON_COMMAND(IDM_STYLE_FABFILTER, &CAnalyzerDlg::OnSpecStyleFabFilter)
	ON_COMMAND_RANGE(IDM_WAVE_SPEED_BASE, IDM_WAVE_SPEED_BASE + CAnalyzerDlg::WAVE_SPEED_COUNT - 1, &CAnalyzerDlg::OnWaveSpeedCmd)
	ON_COMMAND(IDM_PEAK_HOLD, &CAnalyzerDlg::OnTogglePeakHold)
	ON_COMMAND(IDM_EQ_OVERLAY, &CAnalyzerDlg::OnToggleEqOverlay)
	ON_COMMAND(IDM_FREEZE, &CAnalyzerDlg::OnToggleFreeze)
	ON_COMMAND(IDM_RESET_PEAK, &CAnalyzerDlg::OnResetPeakHold)
	ON_COMMAND(IDM_LEVEL_METER, &CAnalyzerDlg::OnToggleLevelMeter)
	ON_COMMAND(IDM_ALWAYS_ON_TOP, &CAnalyzerDlg::OnToggleAlwaysOnTop)
	ON_COMMAND(IDM_CLEAR_DISPLAY, &CAnalyzerDlg::OnClearDisplay)
	ON_COMMAND(IDM_COPY_HOVER, &CAnalyzerDlg::OnCopyHoverReadout)
	ON_COMMAND(IDM_COPY_PEAK, &CAnalyzerDlg::OnCopyPeakFreq)
	ON_COMMAND(IDM_COPY_LEVELS, &CAnalyzerDlg::OnCopyLevels)
	ON_COMMAND(IDM_WAVE_SCROLL, &CAnalyzerDlg::OnWaveModeScroll)
	ON_COMMAND(IDM_WAVE_TRIGGER, &CAnalyzerDlg::OnWaveModeTrigger)
	ON_COMMAND(IDM_LOWER_SPECTRUM, &CAnalyzerDlg::OnLowerModeSpectrum)
	ON_COMMAND(IDM_LOWER_WATERFALL, &CAnalyzerDlg::OnLowerModeWaterfall)
	ON_COMMAND(IDM_LOWER_PHASE, &CAnalyzerDlg::OnLowerModePhase)
	ON_COMMAND(IDM_SPEC_DIFF, &CAnalyzerDlg::OnToggleSpecDiff)
	ON_COMMAND(IDM_SPEC_SNAP, &CAnalyzerDlg::OnCaptureSpecSnap)
	ON_COMMAND(IDM_SPEC_SNAP_CLEAR, &CAnalyzerDlg::OnClearSpecSnap)
	ON_COMMAND(IDM_ZOOM_FULL, &CAnalyzerDlg::OnFreqZoomFull)
	ON_COMMAND(IDM_ZOOM_LOW, &CAnalyzerDlg::OnFreqZoomLow)
	ON_COMMAND(IDM_ZOOM_MID, &CAnalyzerDlg::OnFreqZoomMid)
	ON_COMMAND(IDM_ZOOM_HIGH, &CAnalyzerDlg::OnFreqZoomHigh)
	ON_COMMAND(IDM_MARKER_ADD, &CAnalyzerDlg::OnMarkerAdd)
	ON_COMMAND(IDM_MARKER_REMOVE, &CAnalyzerDlg::OnMarkerRemoveNearest)
	ON_COMMAND(IDM_MARKER_CLEAR, &CAnalyzerDlg::OnMarkerClearAll)
	ON_COMMAND(IDM_CORR_METER, &CAnalyzerDlg::OnToggleCorrMeter)
	ON_MESSAGE(WM_ANALYZER_SPEC_DONE, &CAnalyzerDlg::OnSpecAnalysisDone)
	ON_MESSAGE(WM_ANALYZER_PRESENT, &CAnalyzerDlg::OnPresentRequest)
	ON_MESSAGE(WM_ANALYZER_SYNC, &CAnalyzerDlg::OnSyncRequest)
END_MESSAGE_MAP()

BOOL CAnalyzerDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	SetWindowText(LL14(
		L"アナライザー", L"Analyzer", L"Analyseur", L"Analizzatore",
		L"Analizador", L"분석기", L"分析器", L"المحلل",
		L"Анализатор", L"Analysator", L"Analisador", L"Analyser",
		L"Analizator", L"Analizor"));
	ModifyStyle(WS_MINIMIZEBOX, 0);
	SetIcon(nullptr, TRUE);
	SetIcon(nullptr, FALSE);
	// キャプションアイコンは付けない。Aero 有効時も WS_EX_DLGMODALFRAME を
	// 立てないと既定アイコンが残る（イコライザーは rc の DS_MODALFRAME で消えている）。
	ModifyStyleEx(0, WS_EX_DLGMODALFRAME, SWP_FRAMECHANGED);

	int ax = savedata.analyzerx;
	int ay = savedata.analyzery;
	int aw = savedata.analyzerw;
	int ah = savedata.analyzerh;
	if (ax == -1 || ay == -1 || aw < 200 || ah < 120 || aw > 10000 || ah > 10000) {
		ax = 140; ay = 180; aw = 720; ah = 420;
	}
	SetWindowPos(&CWnd::wndTop, ax, ay, aw, ah, SWP_NOZORDER | SWP_NOOWNERZORDER);

	m_font.CreateFont(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
		DEFAULT_PITCH | FF_SWISS, _T("Segoe UI"));

	m_specLayout = savedata.analyzerspeclayout;
	if (m_specLayout < SpecOverlay || m_specLayout > SpecGrid8)
		m_specLayout = SpecOverlay;
	m_specStyle = savedata.analyzerspecstyle;
	if (m_specStyle < StyleFill || m_specStyle > StyleFabFilter)
		m_specStyle = StyleFill;
	m_waveSpeedPct = savedata.analyzerwavespeed;
	if (m_waveSpeedPct < 25 || m_waveSpeedPct > 200)
		m_waveSpeedPct = 100;
	m_peakHold = (savedata.analyzerpeakhold != 0);
	m_eqOverlay = (savedata.analyzereqoverlay != 0);
	m_showLevelMeter = (savedata.analyzerlevelmeter != 0);
	m_alwaysOnTop = (savedata.analyzertopmost != 0);
	m_waveMode = savedata.analyzerwavemode;
	if (m_waveMode < WaveScroll || m_waveMode > WaveTrigger)
		m_waveMode = WaveScroll;
	m_lowerMode = savedata.analyzerlowermode;
	if (m_lowerMode < LowerSpectrum || m_lowerMode > LowerPhase)
		m_lowerMode = LowerSpectrum;
	m_specDiff = (savedata.analyzerspecdiff != 0);
	m_freqZoom = savedata.analyzerfreqzoom;
	if (m_freqZoom < ZoomFull || m_freqZoom > ZoomHigh)
		m_freqZoom = ZoomFull;
	m_frozen = false;
	m_hoverPlot.SetRectEmpty();
	m_hoverPlotCount = 0;

	// TP/LUFS 読取部だけツールチップを付ける(簡易計測である旨)。
	// 子コントロールを持たない描画専用ダイアログなので矩形ツール。矩形は描画時に更新。
	if (m_tooltip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX)) {
		CRect dummy(0, 0, 1, 1);
		m_tooltip.AddTool(this, AnalyzerTpLufsTipText(), &dummy, 1);
		m_tooltip.SetDelayTime(TTDT_INITIAL, 400);
		m_tooltip.SetDelayTime(TTDT_RESHOW, 120);
		m_tooltip.SetDelayTime(TTDT_AUTOPOP, 12000);
		m_tooltip.SendMessage(TTM_SETMAXTIPWIDTH, 0, 460);
		m_tooltip.Activate(TRUE);
	}
	EnableToolTips(TRUE);

	// 座標は先に載せ、最前面なら Z 順だけ差し替え(ピアノロールと同様)
	if (m_alwaysOnTop && ::IsWindow(m_hWnd)) {
		SetWindowPos(&CWnd::wndTopMost, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
	}

	m_feedEnabled = true;
	StartSpecWorker();
	// タイマーは座標保存のみ。描画は解析/音声完了の PostMessage で自由走行(ピアノロール方式)。
	SetTimer(1, 500, nullptr);
	EnableMainWindowLock(&savedata.analyzerMainLock, TRUE);
	return TRUE;
}

void CAnalyzerDlg::ResumePlaybackFeed() { m_feedEnabled = true; }
void CAnalyzerDlg::PauseFeed() { m_feedEnabled = false; }

void CAnalyzerDlg::ResetPlaybackState()
{
	EnterCriticalSection(&m_cs);
	for (int c = 0; c < CH_MAX; ++c)
		std::fill(m_ring[c].begin(), m_ring[c].end(), 0.0f);
	m_ringWrite = 0;
	m_ringFilled = 0;
	m_accSamples = 0;
	m_pendingScroll = 0;
	for (int c = 0; c < CH_MAX; ++c) {
		m_meterPeak[c] = 0.0f;
		m_meterHold[c] = 0.0f;
		m_meterRms[c] = 0.0f;
	}
	m_waveDispPeak = 0.25f;
	m_tpLin = 0.0f;
	m_tpHold = 0.0f;
	m_lufsMom = -70.0f;
	m_lufsSum = 0.0;
	m_lufsCount = 0.0;
	for (int c = 0; c < CH_MAX; ++c) {
		for (int b = 0; b < SPEC_BINS; ++b) {
			m_specDb[c][b] = -96.0f;
			m_specPeakDb[c][b] = -96.0f;
		}
		for (int r = 0; r < WF_ROWS; ++r) {
			for (int b = 0; b < SPEC_BINS; ++b)
				m_wfHist[c][r][b] = -96.0f;
		}
	}
	m_wfWrite = 0;
	m_wfFilled = 0;
	LeaveCriticalSection(&m_cs);
	m_waveReady = false;
	m_specReady = false;
	m_specDirty = true;
	if (::IsWindow(m_hWnd))
		Invalidate(FALSE);
}

void CAnalyzerDlg::DetachForDestroy()
{
	m_feedEnabled = false;
	KillTimer(1);
	StopSpecWorker();
	InterlockedExchange(&m_presentPosted, 0);
	InterlockedExchange(&m_syncPosted, 0);
	if (::IsWindow(m_hWnd)) {
		MSG msg;
		while (PeekMessage(&msg, m_hWnd, WM_ANALYZER_SPEC_DONE, WM_ANALYZER_SPEC_DONE, PM_REMOVE)) {}
		while (PeekMessage(&msg, m_hWnd, WM_ANALYZER_PRESENT, WM_ANALYZER_PRESENT, PM_REMOVE)) {}
		while (PeekMessage(&msg, m_hWnd, WM_ANALYZER_SYNC, WM_ANALYZER_SYNC, PM_REMOVE)) {}
	}
	ReleaseBuffers();
}

void CAnalyzerDlg::StartSpecWorker()
{
	if (m_hSpecThread) return;
	InterlockedExchange(&m_specStop, 0);
	InterlockedExchange(&m_specNeed, 0);
	m_hSpecWake = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (!m_hSpecWake) return;
	m_hSpecThread = CreateThread(NULL, 0, SpecWorkerEntry, this, 0, NULL);
	if (!m_hSpecThread) {
		CloseHandle(m_hSpecWake);
		m_hSpecWake = nullptr;
	}
}

void CAnalyzerDlg::StopSpecWorker()
{
	InterlockedExchange(&m_specStop, 1);
	if (m_hSpecWake) SetEvent(m_hSpecWake);
	if (m_hSpecThread) {
		WaitForSingleObject(m_hSpecThread, 2000);
		CloseHandle(m_hSpecThread);
		m_hSpecThread = nullptr;
	}
	if (m_hSpecWake) {
		CloseHandle(m_hSpecWake);
		m_hSpecWake = nullptr;
	}
}

void CAnalyzerDlg::RequestSpecAnalysis()
{
	if (m_frozen) return;
	InterlockedExchange(&m_specNeed, 1);
	if (m_hSpecWake) SetEvent(m_hSpecWake);
}

DWORD WINAPI CAnalyzerDlg::SpecWorkerEntry(LPVOID param)
{
	return static_cast<CAnalyzerDlg*>(param)->SpecWorkerLoop();
}

DWORD CAnalyzerDlg::SpecWorkerLoop()
{
	while (InterlockedCompareExchange(&m_specStop, 0, 0) == 0) {
		if (m_hSpecWake)
			WaitForSingleObject(m_hSpecWake, 20);
		if (InterlockedCompareExchange(&m_specStop, 0, 0) != 0)
			break;
		if (InterlockedExchange(&m_specNeed, 0) == 0)
			continue;
		UpdateSpectrumFromRing();
		// SPEC_DONE を無制限 Post するとポンプを埋め、ピアノ/EQ を飢餓させる。
		// 提示は KickUiPresent の合流・ms2 スロットルに任せる。
		if (::IsWindow(m_hWnd))
			KickUiPresent();
	}
	return 0;
}

LRESULT CAnalyzerDlg::OnSpecAnalysisDone(WPARAM, LPARAM)
{
	// ピアノロール同様: 解析ができたフレームからすぐ描く(タイマー待ちにしない)
	KickUiPresent();
	return 0;
}

static void AnalyzerPresentInvalidate(HWND hWnd)
{
	if (!::IsWindow(hWnd))
		return;
	CRect cr;
	::GetClientRect(hWnd, &cr);
	if (!cr.IsRectEmpty())
		CCC_InvalidateRectMinusOverlay(hWnd, cr);
}

LRESULT CAnalyzerDlg::OnPresentRequest(WPARAM, LPARAM)
{
	// presentPosted は OnPaint 完了まで保持。
	// UpdateWindow 定常化はピアノと同様に MP スクロールを飢餓させるため使わない。
	if (::IsWindow(m_hWnd) && IsWindowVisible() && !IsIconic()) {
		AnalyzerPresentInvalidate(m_hWnd);
	}
	else {
		InterlockedExchange(&m_presentPosted, 0);
	}
	return 0;
}

void COggDlg_SyncAnalyzerFast();

void CAnalyzerDlg::RequestSyncFromMainUi()
{
	if (!::IsWindow(m_hWnd)) return;
	// 可視化頻度は savedata.ms2（EQ 表示でも間引かない）
	int minMs = savedata.ms2;
	if (minMs < 16) minMs = 16;
	if (minMs > 960) minMs = 960;
	const DWORD now = GetTickCount();
	if (m_lastSyncPostTick != 0 && (now - m_lastSyncPostTick) < (DWORD)minMs)
		return;
	if (InterlockedCompareExchange(&m_syncPosted, 1, 0) != 0) return;
	m_lastSyncPostTick = now;
	PostMessage(WM_ANALYZER_SYNC, 0, 0);
}

LRESULT CAnalyzerDlg::OnSyncRequest(WPARAM, LPARAM)
{
	InterlockedExchange(&m_syncPosted, 0);
	if (!::IsWindow(m_hWnd) || !IsWindowVisible() || IsIconic())
		return 0;
	COggDlg_SyncAnalyzerFast();
	return 0;
}

void CAnalyzerDlg::SetSpecLayout(int layout)
{
	if (layout < SpecOverlay || layout > SpecGrid8) return;
	if (m_specLayout == layout) return;
	m_specLayout = layout;
	savedata.analyzerspeclayout = layout;
	m_specDirty = true;
	m_specReady = false;
	Invalidate(FALSE);
}

void CAnalyzerDlg::SetSpecStyle(int style)
{
	if (style < StyleFill || style > StyleFabFilter) return;
	if (m_specStyle == style) return;
	m_specStyle = style;
	savedata.analyzerspecstyle = style;
	m_specDirty = true;
	Invalidate(FALSE);
}

void CAnalyzerDlg::SetWaveSpeedPct(int pct)
{
	int nearest = 100;
	int best = 100000;
	for (int i = 0; i < WAVE_SPEED_COUNT; ++i) {
		const int d = abs(kWaveSpeedPct[i] - pct);
		if (d < best) { best = d; nearest = kWaveSpeedPct[i]; }
	}
	if (m_waveSpeedPct == nearest) return;
	m_waveSpeedPct = nearest;
	savedata.analyzerwavespeed = nearest;
	EnterCriticalSection(&m_cs);
	if (m_sampleRate > 0) {
		const int targetW = (m_waveW > 40) ? m_waveW : 640;
		const int baseSpc = (std::max)(4, m_sampleRate * 6 / 5 / targetW);
		m_samplesPerCol = (std::max)(2, (baseSpc * 100 + m_waveSpeedPct / 2) / m_waveSpeedPct);
	}
	LeaveCriticalSection(&m_cs);
}

void CAnalyzerDlg::SetWaveMode(int mode)
{
	if (mode < WaveScroll || mode > WaveTrigger) return;
	if (m_waveMode == mode) return;
	m_waveMode = mode;
	savedata.analyzerwavemode = mode;
	EnterCriticalSection(&m_cs);
	m_pendingScroll = 0;
	LeaveCriticalSection(&m_cs);
	m_waveReady = false;   // 描き方が変わるので波形BBは作り直し
	m_specDirty = true;
#if CCUSTOM_AERO_SUPPORT
	m_chromaReady = false; // スクロール差分キャッシュを捨てる
#endif
	Invalidate(FALSE);
	KickUiPresent();
}

void CAnalyzerDlg::SetLowerMode(int mode)
{
	if (mode < LowerSpectrum || mode > LowerPhase) return;
	if (m_lowerMode == mode) return;
	m_lowerMode = mode;
	savedata.analyzerlowermode = mode;
	m_hoverValid = false;
	m_hoverBin = -1;
	m_specDirty = true;
	m_specReady = false;
#if CCUSTOM_AERO_SUPPORT
	m_chromaReady = false;
#endif
	Invalidate(FALSE);
	KickUiPresent();
}

void CAnalyzerDlg::SetFreqZoom(int zoom)
{
	if (zoom < ZoomFull || zoom > ZoomHigh) return;
	if (m_freqZoom == zoom) return;
	m_freqZoom = zoom;
	savedata.analyzerfreqzoom = zoom;
	m_hoverValid = false;
	m_hoverBin = -1;
	m_specDirty = true;
	m_specReady = false;
#if CCUSTOM_AERO_SUPPORT
	m_chromaReady = false;
#endif
	Invalidate(FALSE);
	KickUiPresent();
}

void CAnalyzerDlg::PushWaterfallRow()
{
	// UpdateSpectrumFromRing 完了ごとに1行積む(解析スレッド)。描画側は CS で読む。
	EnterCriticalSection(&m_cs);
	const int row = m_wfWrite;
	for (int c = 0; c < CH_MAX; ++c)
		memcpy(m_wfHist[c][row], m_specDb[c], sizeof(float) * SPEC_BINS);
	m_wfWrite = (m_wfWrite + 1) % WF_ROWS;
	if (m_wfFilled < WF_ROWS) ++m_wfFilled;
	LeaveCriticalSection(&m_cs);
}

void CAnalyzerDlg::SyncEqUiFromSavedata()
{
	if (og && og->m_EqualizerDlg && ::IsWindow(og->m_EqualizerDlg->GetSafeHwnd()))
		og->m_EqualizerDlg->SyncSlidersFromSavedata();
}

int CAnalyzerDlg::WaveSpeedIndex() const
{
	for (int i = 0; i < WAVE_SPEED_COUNT; ++i)
		if (kWaveSpeedPct[i] == m_waveSpeedPct) return i;
	return 3; // x1.0
}

void CAnalyzerDlg::ResetPeakHold()
{
	EnterCriticalSection(&m_cs);
	for (int c = 0; c < CH_MAX; ++c) {
		for (int b = 0; b < SPEC_BINS; ++b)
			m_specPeakDb[c][b] = m_specDb[c][b];
		m_meterHold[c] = m_meterRms[c];
	}
	m_tpHold = m_tpLin;
	LeaveCriticalSection(&m_cs);
	m_specDirty = true;
	Invalidate(FALSE);
}

void CAnalyzerDlg::OnSpecLayoutOverlay() { SetSpecLayout(SpecOverlay); }
void CAnalyzerDlg::OnSpecLayoutSplitV() { SetSpecLayout(SpecSplitV); }
void CAnalyzerDlg::OnSpecLayoutSplitH() { SetSpecLayout(SpecSplitH); }
void CAnalyzerDlg::OnSpecLayoutGrid4() { SetSpecLayout(SpecGrid4); }
void CAnalyzerDlg::OnSpecLayoutGrid8() { SetSpecLayout(SpecGrid8); }
void CAnalyzerDlg::OnSpecStyleFill() { SetSpecStyle(StyleFill); }
void CAnalyzerDlg::OnSpecStyleLine() { SetSpecStyle(StyleLine); }
void CAnalyzerDlg::OnSpecStyleBars() { SetSpecStyle(StyleBars); }
void CAnalyzerDlg::OnSpecStyleCubase() { SetSpecStyle(StyleCubase); }
void CAnalyzerDlg::OnSpecStyleSpan() { SetSpecStyle(StyleSpan); }
void CAnalyzerDlg::OnSpecStyleAbleton() { SetSpecStyle(StyleAbleton); }
void CAnalyzerDlg::OnSpecStyleFabFilter() { SetSpecStyle(StyleFabFilter); }

void CAnalyzerDlg::OnWaveModeScroll() { SetWaveMode(WaveScroll); }
void CAnalyzerDlg::OnWaveModeTrigger() { SetWaveMode(WaveTrigger); }
void CAnalyzerDlg::OnLowerModeSpectrum() { SetLowerMode(LowerSpectrum); }
void CAnalyzerDlg::OnLowerModeWaterfall() { SetLowerMode(LowerWaterfall); }
void CAnalyzerDlg::OnLowerModePhase() { SetLowerMode(LowerPhase); }
void CAnalyzerDlg::OnFreqZoomFull() { SetFreqZoom(ZoomFull); }
void CAnalyzerDlg::OnFreqZoomLow() { SetFreqZoom(ZoomLow); }
void CAnalyzerDlg::OnFreqZoomMid() { SetFreqZoom(ZoomMid); }
void CAnalyzerDlg::OnFreqZoomHigh() { SetFreqZoom(ZoomHigh); }

void CAnalyzerDlg::OnToggleSpecDiff()
{
	m_specDiff = !m_specDiff;
	savedata.analyzerspecdiff = m_specDiff ? 1 : 0;
	m_specDirty = true;
	m_specReady = false;
#if CCUSTOM_AERO_SUPPORT
	m_chromaReady = false;
#endif
	Invalidate(FALSE);
	KickUiPresent();
}

void CAnalyzerDlg::OnCaptureSpecSnap()
{
	EnterCriticalSection(&m_cs);
	memcpy(m_specSnapDb, m_specDb, sizeof(m_specSnapDb));
	LeaveCriticalSection(&m_cs);
	m_specSnapValid = true;
	m_specDirty = true;
	Invalidate(FALSE);
	KickUiPresent();
}

void CAnalyzerDlg::OnClearSpecSnap()
{
	EnterCriticalSection(&m_cs);
	for (int c = 0; c < CH_MAX; ++c) {
		for (int b = 0; b < SPEC_BINS; ++b)
			m_specSnapDb[c][b] = -96.0f;
	}
	LeaveCriticalSection(&m_cs);
	m_specSnapValid = false;
	m_specDirty = true;
	Invalidate(FALSE);
	KickUiPresent();
}

void CAnalyzerDlg::OnMarkerAdd()
{
	if (!m_hoverValid) return;
	int hz = (int)(m_hoverHz + 0.5f);
	if (hz <= 0) return;
	if (hz > 96000) hz = 96000;
	int slot = -1;
	for (int i = 0; i < MARKER_MAX; ++i) {
		if (savedata.analyzermarkers[i] <= 0) { slot = i; break; }
	}
	if (slot < 0) {
		// 空きがなければ先頭を押し出す(FIFO)
		for (int i = 0; i < MARKER_MAX - 1; ++i)
			savedata.analyzermarkers[i] = savedata.analyzermarkers[i + 1];
		slot = MARKER_MAX - 1;
	}
	savedata.analyzermarkers[slot] = hz;
	m_specDirty = true;
	Invalidate(FALSE);
	KickUiPresent();
}

void CAnalyzerDlg::OnMarkerRemoveNearest()
{
	int best = -1;
	float bestD = 0.0f;
	for (int i = 0; i < MARKER_MAX; ++i) {
		if (savedata.analyzermarkers[i] <= 0) continue;
		float d = 0.0f;
		if (m_hoverValid) {
			d = fabsf(logf((float)savedata.analyzermarkers[i]
				/ (std::max)(m_hoverHz, 1.0f)));
		}
		if (best < 0 || d < bestD) { bestD = d; best = i; }
	}
	if (best < 0) return;
	savedata.analyzermarkers[best] = 0;
	m_specDirty = true;
	Invalidate(FALSE);
	KickUiPresent();
}

void CAnalyzerDlg::OnMarkerClearAll()
{
	bool any = false;
	for (int i = 0; i < MARKER_MAX; ++i) {
		if (savedata.analyzermarkers[i] != 0) any = true;
		savedata.analyzermarkers[i] = 0;
	}
	if (!any) return;
	m_specDirty = true;
	Invalidate(FALSE);
	KickUiPresent();
}

void CAnalyzerDlg::OnToggleCorrMeter()
{
	savedata.pro_corr_meter = savedata.pro_corr_meter ? 0 : 1;
#if CCUSTOM_AERO_SUPPORT
	m_chromaReady = false; // 相関帯の幅が変わるため残像を避ける
#endif
	Invalidate(FALSE);
	KickUiPresent();
}

void CAnalyzerDlg::OnWaveSpeedCmd(UINT nID)
{
	const int idx = (int)nID - IDM_WAVE_SPEED_BASE;
	if (idx < 0 || idx >= WAVE_SPEED_COUNT) return;
	SetWaveSpeedPct(kWaveSpeedPct[idx]);
}

void CAnalyzerDlg::OnTogglePeakHold()
{
	m_peakHold = !m_peakHold;
	savedata.analyzerpeakhold = m_peakHold ? 1 : 0;
	if (!m_peakHold)
		ResetPeakHold();
	else {
		m_specDirty = true;
		Invalidate(FALSE);
	}
}

void CAnalyzerDlg::OnToggleEqOverlay()
{
	m_eqOverlay = !m_eqOverlay;
	savedata.analyzereqoverlay = m_eqOverlay ? 1 : 0;
	m_specDirty = true;
	Invalidate(FALSE);
}

void CAnalyzerDlg::OnToggleFreeze()
{
	m_frozen = !m_frozen;
	Invalidate(FALSE);
}

void CAnalyzerDlg::OnResetPeakHold()
{
	ResetPeakHold();
}

void CAnalyzerDlg::OnToggleLevelMeter()
{
	m_showLevelMeter = !m_showLevelMeter;
	savedata.analyzerlevelmeter = m_showLevelMeter ? 1 : 0;
#if CCUSTOM_AERO_SUPPORT
	m_chromaReady = false; // 右端ストリップ残像を避ける
#endif
	Invalidate(FALSE);
}

void CAnalyzerDlg::OnToggleAlwaysOnTop()
{
	m_alwaysOnTop = !m_alwaysOnTop;
	savedata.analyzertopmost = m_alwaysOnTop ? 1 : 0;
	if (::IsWindow(m_hWnd)) {
		SetWindowPos(m_alwaysOnTop ? &CWnd::wndTopMost : &CWnd::wndNoTopMost,
			0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	}
}

void CAnalyzerDlg::OnClearDisplay()
{
	// 波形/周波数/スペクトログラム履歴と TP・LUFS のホールドを消す。
	// 差分用スナップショットは「スナップショットを消去」でのみ消す。
	ResetPlaybackState();
#if CCUSTOM_AERO_SUPPORT
	m_chromaReady = false;
#endif
}

void CAnalyzerDlg::OnCopyHoverReadout()
{
	if (!m_hoverValid)
		return;
	const int channels = (std::max)(1, (std::min)(m_channels, CH_MAX));
	CString s;
	if (m_hoverHz >= 1000.0f)
		s.Format(_T("%s\t%.3f kHz\t%.1f dB"), ChannelLabel(m_hoverCh, channels), m_hoverHz / 1000.0f, m_hoverDb);
	else
		s.Format(_T("%s\t%.1f Hz\t%.1f dB"), ChannelLabel(m_hoverCh, channels), m_hoverHz, m_hoverDb);
	CopyUnicodeToClipboard(m_hWnd, s);
}

void CAnalyzerDlg::OnCopyPeakFreq()
{
	float bestDb = -96.0f;
	int bestCh = 0;
	int bestBin = 0;
	int channels = 2;
	int sr = 44100;
	EnterCriticalSection(&m_cs);
	channels = (std::max)(1, (std::min)(m_channels, CH_MAX));
	sr = (m_sampleRate > 0) ? m_sampleRate : 44100;
	const float (*bins)[SPEC_BINS] = m_peakHold ? m_specPeakDb : m_specDb;
	for (int c = 0; c < channels; ++c) {
		for (int b = 0; b < SPEC_BINS; ++b) {
			if (bins[c][b] > bestDb) {
				bestDb = bins[c][b];
				bestCh = c;
				bestBin = b;
			}
		}
	}
	LeaveCriticalSection(&m_cs);

	const float nyquist = (float)sr * 0.5f;
	const float hz = SpecCenterHz(bestBin, SPEC_BINS, nyquist);
	CString s;
	if (hz >= 1000.0f)
		s.Format(_T("%s\t%.3f kHz\t%.1f dB\tSR %d"), ChannelLabel(bestCh, channels), hz / 1000.0f, bestDb, sr);
	else
		s.Format(_T("%s\t%.1f Hz\t%.1f dB\tSR %d"), ChannelLabel(bestCh, channels), hz, bestDb, sr);
	CopyUnicodeToClipboard(m_hWnd, s);
}

void CAnalyzerDlg::OnCopyLevels()
{
	float hold[CH_MAX] = {};
	float rms[CH_MAX] = {};
	float tp = 0.0f, lufs = -70.0f;
	int channels = 2;
	EnterCriticalSection(&m_cs);
	for (int c = 0; c < CH_MAX; ++c) {
		hold[c] = m_meterHold[c];
		rms[c] = m_meterRms[c];
	}
	channels = (std::max)(1, (std::min)(m_channels, CH_MAX));
	tp = (std::max)(m_tpHold, m_tpLin);
	lufs = m_lufsMom;
	LeaveCriticalSection(&m_cs);

	CString s;
	for (int c = 0; c < channels; ++c) {
		CString one;
		one.Format(_T("%s\t%.1f dB\thold %.1f dB"),
			ChannelLabel(c, channels), AmpToDb(rms[c]), AmpToDb(hold[c]));
		if (c > 0) s += _T("\t");
		s += one;
	}
	float tpDb = -99.9f;
	if (tp > 1e-6f) tpDb = 20.0f * log10f(tp);
	CString extra;
	extra.Format(_T("\tTP %.1f dBTP\tLUFS %.1f"), tpDb, lufs);
	s += extra;
	CopyUnicodeToClipboard(m_hWnd, s);
}

void CAnalyzerDlg::OnContextMenu(CWnd* /*pWnd*/, CPoint point)
{
	CMenu menu;
	menu.CreatePopupMenu();

	CMenu subWave;
	subWave.CreatePopupMenu();
	subWave.AppendMenu(MF_STRING | (m_waveMode == WaveScroll ? MF_CHECKED : 0),
		IDM_WAVE_SCROLL, LL14(L"スクロール波形", L"Scrolling wave", L"Onde defilante", L"Onda scorrevole", L"Onda desplazable", L"스크롤 파형", L"滚动波形", L"موجة متمررة", L"Прокрутка волны", L"Scrollende Welle", L"Onda rolante", L"Scrollende golf", L"Przewijana fala", L"Kayan dalga"));
	subWave.AppendMenu(MF_STRING | (m_waveMode == WaveTrigger ? MF_CHECKED : 0),
		IDM_WAVE_TRIGGER, LL14(L"トリガー式オシロ", L"Triggered scope", L"Oscillo declenche", L"Oscilloscopio con trigger", L"Osciloscopio con disparo", L"트리거 오실로스코프", L"触发示波器", L"راسم ذبذبات محفز", L"Синхр. осциллограф", L"Getriggertes Oszilloskop", L"Osciloscopio disparado", L"Getriggerde scoop", L"Wyzwalany oscyloskop", L"Tetiklemeli osiloskop"));
	menu.AppendMenu(MF_POPUP, (UINT_PTR)subWave.Detach(),
		LL14(L"上部の表示", L"Upper display", L"Affichage superieur", L"Display superiore", L"Pantalla superior", L"상단 표시", L"上部显示", L"العرض العلوي", L"Верхняя область", L"Obere Anzeige", L"Exibicao superior", L"Bovenste weergave", L"Gorny widok", L"Ust gosterim"));

	CMenu subLower;
	subLower.CreatePopupMenu();
	subLower.AppendMenu(MF_STRING | (m_lowerMode == LowerSpectrum ? MF_CHECKED : 0),
		IDM_LOWER_SPECTRUM, LL14(L"周波数特性", L"Spectrum", L"Spectre", L"Spettro", L"Espectro", L"스펙트럼", L"频谱", L"الطيف", L"Спектр", L"Spektrum", L"Espectro", L"Spectrum", L"Widmo", L"Spektrum"));
	subLower.AppendMenu(MF_STRING | (m_lowerMode == LowerWaterfall ? MF_CHECKED : 0),
		IDM_LOWER_WATERFALL, LL14(L"スペクトログラム", L"Spectrogram", L"Spectrogramme", L"Spettrogramma", L"Espectrograma", L"스펙트로그램", L"频谱图", L"مخطط طيفي", L"Спектрограмма", L"Spektrogramm", L"Espectrograma", L"Spectrogram", L"Spektrogram", L"Spektrogram"));
	subLower.AppendMenu(MF_STRING | (m_lowerMode == LowerPhase ? MF_CHECKED : 0),
		IDM_LOWER_PHASE, LL14(L"位相スコープ", L"Phase scope", L"Scope de phase", L"Scope di fase", L"Osciloscopio de fase", L"위상 스코프", L"相位示波器", L"راسم الطور", L"Фазовый скоп", L"Phasenskop", L"Escopo de fase", L"Fasescoop", L"Skop fazy", L"Faz skopu"));
	menu.AppendMenu(MF_POPUP, (UINT_PTR)subLower.Detach(),
		LL14(L"下部の表示", L"Lower display", L"Affichage inferieur", L"Display inferiore", L"Pantalla inferior", L"하단 표시", L"下部显示", L"العرض السفلي", L"Нижняя область", L"Untere Anzeige", L"Exibicao inferior", L"Onderste weergave", L"Dolny widok", L"Alt gosterim"));

	CMenu subDiff;
	subDiff.CreatePopupMenu();
	subDiff.AppendMenu(MF_STRING | (m_specDiff ? MF_CHECKED : 0),
		IDM_SPEC_DIFF, LL14(L"差分を表示", L"Show difference", L"Afficher la difference", L"Mostra differenza", L"Mostrar diferencia", L"차분 표시", L"显示差分", L"إظهار الفرق", L"Показать разницу", L"Differenz anzeigen", L"Mostrar diferenca", L"Verschil tonen", L"Pokaz roznice", L"Farki goster"));
	subDiff.AppendMenu(MF_STRING, IDM_SPEC_SNAP,
		LL14(L"現在をスナップショット", L"Capture snapshot", L"Capturer un instantane", L"Cattura istantanea", L"Capturar instantanea", L"현재를 스냅샷", L"捕获快照", L"التقاط لقطة", L"Сделать снимок", L"Momentaufnahme erstellen", L"Capturar instantaneo", L"Momentopname maken", L"Zrob migawke", L"Anlik goruntu al"));
	subDiff.AppendMenu(MF_STRING | (m_specSnapValid ? 0 : MF_GRAYED), IDM_SPEC_SNAP_CLEAR,
		LL14(L"スナップショットを消去", L"Clear snapshot", L"Effacer l'instantane", L"Cancella istantanea", L"Borrar instantanea", L"스냅샷 지우기", L"清除快照", L"مسح اللقطة", L"Удалить снимок", L"Momentaufnahme loschen", L"Limpar instantaneo", L"Momentopname wissen", L"Wyczysc migawke", L"Anlik goruntuyu sil"));
	menu.AppendMenu(MF_POPUP, (UINT_PTR)subDiff.Detach(),
		LL14(L"スペクトラム差分", L"Spectrum difference", L"Difference de spectre", L"Differenza di spettro", L"Diferencia de espectro", L"스펙트럼 차분", L"频谱差分", L"فرق الطيف", L"Разница спектра", L"Spektrumdifferenz", L"Diferenca de espectro", L"Spectrumverschil", L"Roznica widma", L"Spektrum farki"));

	CMenu subZoom;
	subZoom.CreatePopupMenu();
	subZoom.AppendMenu(MF_STRING | (m_freqZoom == ZoomFull ? MF_CHECKED : 0),
		IDM_ZOOM_FULL, LL14(L"全帯域", L"Full range", L"Bande complete", L"Banda intera", L"Banda completa", L"전대역", L"全频段", L"النطاق الكامل", L"Весь диапазон", L"Voller Bereich", L"Faixa completa", L"Volledig bereik", L"Pelny zakres", L"Tam bant"));
	subZoom.AppendMenu(MF_STRING | (m_freqZoom == ZoomLow ? MF_CHECKED : 0),
		IDM_ZOOM_LOW, LL14(L"低域 (20-250Hz)", L"Low (20-250 Hz)", L"Graves (20-250 Hz)", L"Bassi (20-250 Hz)", L"Graves (20-250 Hz)", L"저역 (20-250Hz)", L"低频 (20-250Hz)", L"منخفض (20-250 هرتز)", L"Низкие (20-250 Гц)", L"Tiefen (20-250 Hz)", L"Graves (20-250 Hz)", L"Laag (20-250 Hz)", L"Niskie (20-250 Hz)", L"Bas (20-250 Hz)"));
	subZoom.AppendMenu(MF_STRING | (m_freqZoom == ZoomMid ? MF_CHECKED : 0),
		IDM_ZOOM_MID, LL14(L"中域 (250Hz-4kHz)", L"Mid (250 Hz - 4 kHz)", L"Mediums (250 Hz - 4 kHz)", L"Medi (250 Hz - 4 kHz)", L"Medios (250 Hz - 4 kHz)", L"중역 (250Hz-4kHz)", L"中频 (250Hz-4kHz)", L"متوسط (250 هرتز - 4 كيلوهرتز)", L"Средние (250 Гц - 4 кГц)", L"Mitten (250 Hz - 4 kHz)", L"Medios (250 Hz - 4 kHz)", L"Midden (250 Hz - 4 kHz)", L"Srednie (250 Hz - 4 kHz)", L"Orta (250 Hz - 4 kHz)"));
	subZoom.AppendMenu(MF_STRING | (m_freqZoom == ZoomHigh ? MF_CHECKED : 0),
		IDM_ZOOM_HIGH, LL14(L"高域 (4kHz以上)", L"High (4 kHz and up)", L"Aigus (4 kHz et plus)", L"Alti (da 4 kHz)", L"Agudos (desde 4 kHz)", L"고역 (4kHz 이상)", L"高频 (4kHz 以上)", L"عالي (4 كيلوهرتز فأكثر)", L"Высокие (от 4 кГц)", L"Hohen (ab 4 kHz)", L"Agudos (a partir de 4 kHz)", L"Hoog (vanaf 4 kHz)", L"Wysokie (od 4 kHz)", L"Tiz (4 kHz ve ustu)"));
	menu.AppendMenu(MF_POPUP, (UINT_PTR)subZoom.Detach(),
		LL14(L"周波数の範囲", L"Frequency range", L"Plage de frequences", L"Intervallo di frequenza", L"Rango de frecuencias", L"주파수 범위", L"频率范围", L"نطاق التردد", L"Диапазон частот", L"Frequenzbereich", L"Faixa de frequencia", L"Frequentiebereik", L"Zakres czestotliwosci", L"Frekans araligi"));

	CMenu subMarker;
	subMarker.CreatePopupMenu();
	subMarker.AppendMenu(MF_STRING | (m_hoverValid ? 0 : MF_GRAYED), IDM_MARKER_ADD,
		LL14(L"ホバー位置に追加", L"Add at hover position", L"Ajouter a la position du survol", L"Aggiungi alla posizione del cursore", L"Anadir en la posicion del cursor", L"호버 위치에 추가", L"在悬停位置添加", L"إضافة عند موضع المؤشر", L"Добавить в позиции курсора", L"An Hover-Position hinzufugen", L"Adicionar na posicao do cursor", L"Toevoegen op hoverpositie", L"Dodaj w miejscu kursora", L"Imlec konumuna ekle"));
	subMarker.AppendMenu(MF_STRING, IDM_MARKER_REMOVE,
		LL14(L"最も近いものを削除", L"Remove nearest", L"Supprimer le plus proche", L"Rimuovi il piu vicino", L"Eliminar el mas cercano", L"가장 가까운 것 삭제", L"删除最近的", L"إزالة الأقرب", L"Удалить ближайший", L"Nachsten entfernen", L"Remover o mais proximo", L"Dichtstbijzijnde verwijderen", L"Usun najblizszy", L"En yakini kaldir"));
	subMarker.AppendMenu(MF_STRING, IDM_MARKER_CLEAR,
		LL14(L"すべて削除", L"Clear all", L"Tout effacer", L"Cancella tutti", L"Borrar todos", L"모두 삭제", L"全部清除", L"مسح الكل", L"Удалить все", L"Alle loschen", L"Limpar todos", L"Alles wissen", L"Usun wszystkie", L"Tumunu temizle"));
	menu.AppendMenu(MF_POPUP, (UINT_PTR)subMarker.Detach(),
		LL14(L"周波数マーカー", L"Frequency markers", L"Marqueurs de frequence", L"Marcatori di frequenza", L"Marcadores de frecuencia", L"주파수 마커", L"频率标记", L"علامات التردد", L"Частотные маркеры", L"Frequenzmarker", L"Marcadores de frequencia", L"Frequentiemarkeringen", L"Znaczniki czestotliwosci", L"Frekans isaretleri"));

	menu.AppendMenu(MF_STRING | (savedata.pro_corr_meter ? MF_CHECKED : 0),
		IDM_CORR_METER, LL14(L"位相相関メーター", L"Correlation meter", L"Correlometre", L"Misuratore di correlazione", L"Medidor de correlacion", L"위상 상관 미터", L"相位相关表", L"مقياس الترابط الطوري", L"Измеритель корреляции", L"Korrelationsanzeige", L"Medidor de correlacao", L"Correlatiemeter", L"Miernik korelacji", L"Faz korelasyon olceri"));
	menu.AppendMenu(MF_SEPARATOR);

	CMenu subLayout;
	subLayout.CreatePopupMenu();
	subLayout.AppendMenu(MF_STRING | (m_specLayout == SpecOverlay ? MF_CHECKED : 0),
		IDM_SPEC_OVERLAY, LL14(L"重ね描き", L"Overlay", L"Superpose", L"Sovrapposto", L"Superpuesto", L"겹침", L"叠加", L"تراكب", L"Наложение", L"Uberlagert", L"Sobreposto", L"Overlay", L"Nakladanie", L"Ustuste"));
	subLayout.AppendMenu(MF_STRING | (m_specLayout == SpecSplitV ? MF_CHECKED : 0),
		IDM_SPEC_SPLIT_V, LL14(L"上下分割", L"Split vertical", L"Division verticale", L"Divisione verticale", L"Division vertical", L"상하 분할", L"上下分割", L"تقسيم رأسي", L"Вертикально", L"Vertikal teilen", L"Dividir vertical", L"Verticaal", L"Pionowo", L"Dikey bol"));
	subLayout.AppendMenu(MF_STRING | (m_specLayout == SpecSplitH ? MF_CHECKED : 0),
		IDM_SPEC_SPLIT_H, LL14(L"左右分割", L"Split horizontal", L"Division horizontale", L"Divisione orizzontale", L"Division horizontal", L"좌우 분할", L"左右分割", L"تقسيم أفقي", L"Горизонтально", L"Horizontal teilen", L"Dividir horizontal", L"Horizontaal", L"Poziomo", L"Yatay bol"));
	subLayout.AppendMenu(MF_STRING | (m_specLayout == SpecGrid4 ? MF_CHECKED : 0)
		| (m_channels < 4 ? MF_GRAYED : 0),
		IDM_SPEC_GRID4, LL14(L"4分割 (2x2)", L"4-way (2x2)", L"4 voies (2x2)", L"4 vie (2x2)", L"4 vias (2x2)", L"4분할 (2x2)", L"四分割(2x2)", L"4 اتجاهات", L"4 панели", L"4-fach (2x2)", L"4 vias (2x2)", L"4-weg (2x2)", L"4 panele (2x2)", L"4 bolum (2x2)"));
	subLayout.AppendMenu(MF_STRING | (m_specLayout == SpecGrid8 ? MF_CHECKED : 0)
		| (m_channels < 5 ? MF_GRAYED : 0),
		IDM_SPEC_GRID8, LL14(L"8分割 (2x4)", L"8-way (2x4)", L"8 voies (2x4)", L"8 vie (2x4)", L"8 vias (2x4)", L"8분할 (2x4)", L"八分割(2x4)", L"8 اتجاهات", L"8 панелей", L"8-fach (2x4)", L"8 vias (2x4)", L"8-weg (2x4)", L"8 paneli (2x4)", L"8 bolum (2x4)"));
	menu.AppendMenu(MF_POPUP, (UINT_PTR)subLayout.Detach(),
		LL14(L"周波数特性の表示", L"Frequency display", L"Affichage frequence", L"Visualizzazione frequenza", L"Vista de frecuencia", L"주파수 표시", L"频率显示", L"عرض التردد", L"Отображение АЧХ", L"Frequenzanzeige", L"Exibicao de frequencia", L"Frequentieweergave", L"Wyswietlanie czest.", L"Frekans gorunumu"));

	CMenu subStyle;
	subStyle.CreatePopupMenu();
	subStyle.AppendMenu(MF_STRING | (m_specStyle == StyleFill ? MF_CHECKED : 0),
		IDM_STYLE_FILL, LL14(L"Ozone 風 (塗+線)", L"Ozone (fill+line)", L"Ozone (rempl.+ligne)", L"Ozone (riemp.+linea)", L"Ozone (relleno+linea)", L"Ozone (채움+선)", L"Ozone(填充+线)", L"Ozone (تعبئة+خط)", L"Ozone (заливка+линия)", L"Ozone (Fullung+Linie)", L"Ozone (preench.+linha)", L"Ozone (vulling+lijn)", L"Ozone (wypeln.+linia)", L"Ozone (dolgu+cizgi)"));
	subStyle.AppendMenu(MF_STRING | (m_specStyle == StyleCubase ? MF_CHECKED : 0),
		IDM_STYLE_CUBASE, LL14(L"Cubase Frequency 風", L"Cubase Frequency style", L"Style Cubase Frequency", L"Stile Cubase Frequency", L"Estilo Cubase Frequency", L"Cubase Frequency 스타일", L"Cubase Frequency 风格", L"نمط Cubase Frequency", L"Стиль Cubase Frequency", L"Cubase Frequency-Stil", L"Estilo Cubase Frequency", L"Cubase Frequency-stijl", L"Styl Cubase Frequency", L"Cubase Frequency tarzı"));
	subStyle.AppendMenu(MF_STRING | (m_specStyle == StyleSpan ? MF_CHECKED : 0),
		IDM_STYLE_SPAN, LL14(L"Voxengo SPAN 風", L"Voxengo SPAN style", L"Style Voxengo SPAN", L"Stile Voxengo SPAN", L"Estilo Voxengo SPAN", L"Voxengo SPAN 스타일", L"Voxengo SPAN 风格", L"نمط Voxengo SPAN", L"Стиль Voxengo SPAN", L"Voxengo SPAN-Stil", L"Estilo Voxengo SPAN", L"Voxengo SPAN-stijl", L"Styl Voxengo SPAN", L"Voxengo SPAN tarzı"));
	subStyle.AppendMenu(MF_STRING | (m_specStyle == StyleAbleton ? MF_CHECKED : 0),
		IDM_STYLE_ABLETON, LL14(L"Ableton Spectrum 風", L"Ableton Spectrum style", L"Style Ableton Spectrum", L"Stile Ableton Spectrum", L"Estilo Ableton Spectrum", L"Ableton Spectrum 스타일", L"Ableton Spectrum 风格", L"نمط Ableton Spectrum", L"Стиль Ableton Spectrum", L"Ableton Spectrum-Stil", L"Estilo Ableton Spectrum", L"Ableton Spectrum-stijl", L"Styl Ableton Spectrum", L"Ableton Spectrum tarzı"));
	subStyle.AppendMenu(MF_STRING | (m_specStyle == StyleFabFilter ? MF_CHECKED : 0),
		IDM_STYLE_FABFILTER, LL14(L"FabFilter Pro-Q 風", L"FabFilter Pro-Q style", L"Style FabFilter Pro-Q", L"Stile FabFilter Pro-Q", L"Estilo FabFilter Pro-Q", L"FabFilter Pro-Q 스타일", L"FabFilter Pro-Q 风格", L"نمط FabFilter Pro-Q", L"Стиль FabFilter Pro-Q", L"FabFilter Pro-Q-Stil", L"Estilo FabFilter Pro-Q", L"FabFilter Pro-Q-stijl", L"Styl FabFilter Pro-Q", L"FabFilter Pro-Q tarzı"));
	subStyle.AppendMenu(MF_STRING | (m_specStyle == StyleBars ? MF_CHECKED : 0),
		IDM_STYLE_BARS, LL14(L"バー (汎用)", L"Bars (generic)", L"Barres", L"Barre", L"Barras", L"막대", L"柱状", L"أشرطة", L"Столбцы", L"Balken", L"Barras", L"Balken", L"Slupki", L"Cubuk"));
	subStyle.AppendMenu(MF_STRING | (m_specStyle == StyleLine ? MF_CHECKED : 0),
		IDM_STYLE_LINE, LL14(L"線のみ", L"Line only", L"Ligne seule", L"Solo linea", L"Solo linea", L"선만", L"仅线", L"خط فقط", L"Только линия", L"Nur Linie", L"Somente linha", L"Alleen lijn", L"Tylko linia", L"Sadece cizgi"));
	menu.AppendMenu(MF_POPUP, (UINT_PTR)subStyle.Detach(),
		LL14(L"周波数の表示モード", L"Frequency display mode", L"Mode d'affichage frequence", L"Modalita visualizzazione", L"Modo de visualizacion", L"주파수 표시 모드", L"频率显示模式", L"وضع عرض التردد", L"Режим АЧХ", L"Frequenz-Anzeigemodus", L"Modo de frequencia", L"Frequentieweergavemodus", L"Tryb wyswietlania czest.", L"Frekans gorunum modu"));

	CMenu subSpeed;
	subSpeed.CreatePopupMenu();
	const int speedIdx = WaveSpeedIndex();
	for (int i = 0; i < WAVE_SPEED_COUNT; ++i) {
		CString lab;
		lab.Format(_T("x%.2f"), (double)kWaveSpeedPct[i] / 100.0);
		subSpeed.AppendMenu(MF_STRING | (i == speedIdx ? MF_CHECKED : 0),
			IDM_WAVE_SPEED_BASE + i, lab);
	}
	menu.AppendMenu(MF_POPUP, (UINT_PTR)subSpeed.Detach(),
		LL14(L"波形の流れる速度", L"Wave scroll speed", L"Vitesse de defilement", L"Velocita scorrimento", L"Velocidad de desplazamiento", L"파형 스크롤 속도", L"波形滚动速度", L"سرعة تمرير الموجة", L"Скорость прокрутки волны", L"Wellen-Scrollgeschwindigkeit", L"Velocidade de rolagem", L"Golf-scrolsnelheid", L"Predkosc przewijania fali", L"Dalga kaydirma hizi"));

	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING | (m_peakHold ? MF_CHECKED : 0),
		IDM_PEAK_HOLD, LL14(L"ピークホールド", L"Peak hold", L"Maintien de crete", L"Picco trattenuto", L"Retencion de pico", L"피크 홀드", L"峰值保持", L"الاحتفاظ بالذروة", L"Удержание пика", L"Peak Hold", L"Retencao de pico", L"Piekvasthouden", L"Przytrzymanie szczytu", L"Tepe tutma"));
	menu.AppendMenu(MF_STRING | (m_eqOverlay ? MF_CHECKED : 0),
		IDM_EQ_OVERLAY, LL14(L"EQオーバーレイ", L"EQ overlay", L"Superposition EQ", L"Sovrapposizione EQ", L"Superposicion EQ", L"EQ 오버레이", L"EQ叠加", L"تراكب EQ", L"Оверлей EQ", L"EQ-Overlay", L"Sobreposicao EQ", L"EQ-overlay", L"Nakladka EQ", L"EQ kaplama"));
	menu.AppendMenu(MF_STRING | (m_showLevelMeter ? MF_CHECKED : 0),
		IDM_LEVEL_METER, LL14(L"レベルメーター", L"Level meter", L"Indicateur de niveau", L"Misuratore di livello", L"Medidor de nivel", L"레벨 미터", L"电平表", L"مقياس المستوى", L"Уровень сигнала", L"Pegelanzeige", L"Medidor de nivel", L"Niveaumeter", L"Miernik poziomu", L"Seviye olcer"));
	menu.AppendMenu(MF_STRING | (m_frozen ? MF_CHECKED : 0),
		IDM_FREEZE, LL14(L"フリーズ", L"Freeze", L"Gel", L"Congela", L"Congelar", L"정지", L"冻结", L"تجميد", L"Заморозка", L"Einfrieren", L"Congelar", L"Bevriezen", L"Zamroz", L"Dondur"));
	menu.AppendMenu(MF_STRING, IDM_RESET_PEAK,
		LL14(L"ピークをリセット (ダブルクリック)", L"Reset peaks (double-click)", L"Reinit. cretes (double-clic)", L"Reset picchi (doppio clic)", L"Restablecer picos (doble clic)", L"피크 리셋(더블클릭)", L"重置峰值(双击)", L"إعادة الذروة (نقر مزدوج)", L"Сброс пиков (двойной клик)", L"Peaks zurucksetzen (Doppelklick)", L"Redefinir picos (duplo clique)", L"Piekreset (dubbelklik)", L"Reset szczytow (dwuklik)", L"Tepe sifirla (cift tik)"));

	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING | (m_hoverValid ? 0 : MF_GRAYED), IDM_COPY_HOVER,
		LL14(L"ホバー値をコピー", L"Copy hover readout", L"Copier la lecture au survol", L"Copia lettura hover", L"Copiar lectura al pasar", L"호버 값 복사", L"复制悬停读数", L"نسخ قراءة التمرير", L"Копировать наведение", L"Hover-Wert kopieren", L"Copiar leitura ao pairar", L"Hoverwaarde kopieren", L"Kopiuj odczyt hover", L"Hover degerini kopyala"));
	menu.AppendMenu(MF_STRING, IDM_COPY_PEAK,
		LL14(L"最大ピークをコピー", L"Copy loudest peak", L"Copier le pic max", L"Copia picco massimo", L"Copiar pico maximo", L"최대 피크 복사", L"复制最大峰值", L"نسخ أعلى قمة", L"Копировать макс. пик", L"Lautesten Peak kopieren", L"Copiar pico mais alto", L"Luidste piek kopieren", L"Kopiuj najglosniejszy szczyt", L"En yuksek tepeyi kopyala"));
	menu.AppendMenu(MF_STRING, IDM_COPY_LEVELS,
		LL14(L"レベルをコピー", L"Copy levels", L"Copier les niveaux", L"Copia livelli", L"Copiar niveles", L"레벨 복사", L"复制电平", L"نسخ المستويات", L"Копировать уровни", L"Pegel kopieren", L"Copiar niveis", L"Niveaus kopieren", L"Kopiuj poziomy", L"Seviyeleri kopyala"));

	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, IDM_CLEAR_DISPLAY,
		LL14(L"表示をクリア", L"Clear display", L"Effacer l'affichage", L"Cancella visualizzazione", L"Borrar pantalla", L"표시 지우기", L"清除显示", L"مسح العرض", L"Очистить экран", L"Anzeige leeren", L"Limpar exibicao", L"Weergave wissen", L"Wyczysc wyswietlacz", L"Goruntuyu temizle"));
	menu.AppendMenu(MF_STRING | (m_alwaysOnTop ? MF_CHECKED : 0),
		IDM_ALWAYS_ON_TOP, LL14(L"常に手前に表示", L"Always on top", L"Toujours au premier plan", L"Sempre in primo piano", L"Siempre visible", L"항상 위에 표시", L"始终置顶", L"دائما في المقدمة", L"Поверх всех окон", L"Immer im Vordergrund", L"Sempre no topo", L"Altijd op voorgrond", L"Zawsze na wierzchu", L"Her zaman ustte"));

	if (point.x == -1 && point.y == -1) {
		CRect rc; GetClientRect(&rc); ClientToScreen(&rc);
		point = CPoint(rc.left + 8, rc.top + 8);
	}
	menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
}

LPCTSTR CAnalyzerDlg::ChannelLabel(int ch, int channels)
{
	static const TCHAR* stereo[] = { _T("L"), _T("R") };
	static const TCHAR* ch51[] = { _T("L"), _T("R"), _T("C"), _T("LFE"), _T("SL"), _T("SR") };
	static const TCHAR* ch71[] = { _T("L"), _T("R"), _T("C"), _T("LFE"), _T("SL"), _T("SR"), _T("BL"), _T("BR") };
	static TCHAR generic[8][8];
	if (channels == 2 && ch < 2) return stereo[ch];
	if (channels == 6 && ch < 6) return ch51[ch];
	if (channels >= 8 && ch < 8) return ch71[ch];
	_stprintf_s(generic[ch], _T("Ch%d"), ch + 1);
	return generic[ch];
}

int CAnalyzerDlg::VisibleChannelCount(int waveH) const
{
	const int ch = (std::max)(1, (std::min)(m_channels, CH_MAX));
	const int minBand = 22;
	const int fit = (std::max)(1, waveH / minBand);
	return (std::min)(ch, fit);
}

void CAnalyzerDlg::FeedPCM(const void* pData, int frames, int sampleRate, int bits, int channels)
{
	if (!m_feedEnabled || m_frozen || !pData || frames <= 0) return;
	if (channels < 1) channels = 1;
	if (channels > CH_MAX) channels = CH_MAX;
	if (sampleRate > 0) {
		m_sampleRate = sampleRate;
		// ~1.2 秒分が見える密度 × 速度倍率。細かすぎると負荷、粗すぎるとカクつく
		const int targetW = (m_waveW > 40) ? m_waveW : 640;
		const int baseSpc = (std::max)(4, sampleRate * 6 / 5 / targetW);
		const int speedPct = (std::max)(25, (std::min)(200, m_waveSpeedPct));
		m_samplesPerCol = (std::max)(2, (baseSpc * 100 + speedPct / 2) / speedPct);
	}

	float framePeak[CH_MAX] = {};
	double frameSumSq[CH_MAX] = {};
	int frameN[CH_MAX] = {};
	// 簡易 TP: 直前2サンプルから 3点ラグランジュで半サンプル位置を推定(オーバーシュートを拾う)
	float tpPrev1[2] = { 0.0f, 0.0f };
	float tpPrev2[2] = { 0.0f, 0.0f };
	float framePeakTp = 0.0f;
	EnterCriticalSection(&m_cs);
	if (m_channels != channels) {
		m_channels = channels;
		m_waveReady = false;
		m_specDirty = true;
	}
	{
		const int i1 = (m_ringWrite - 1 + RING_SAMPLES) % RING_SAMPLES;
		const int i2 = (m_ringWrite - 2 + RING_SAMPLES) % RING_SAMPLES;
		for (int c = 0; c < 2; ++c) {
			tpPrev1[c] = m_ring[c][i1];
			tpPrev2[c] = m_ring[c][i2];
		}
	}

	const int tpCh = (channels >= 2) ? 2 : 1;
	for (int i = 0; i < frames; ++i) {
		for (int c = 0; c < channels; ++c) {
			const float v = SampleToFloat(pData, bits, i, c, channels);
			m_ring[c][m_ringWrite] = v;
			const float a = fabsf(v);
			if (a > framePeak[c]) framePeak[c] = a;
			frameSumSq[c] += (double)a * (double)a;
			++frameN[c];
			if (c < tpCh) {
				if (a > framePeakTp) framePeakTp = a;
				const float mid = 0.375f * tpPrev2[c] + 0.75f * tpPrev1[c] - 0.125f * v;
				const float am = fabsf(mid);
				if (am > framePeakTp) framePeakTp = am;
				tpPrev2[c] = tpPrev1[c];
				tpPrev1[c] = v;
			}
		}
		for (int c = channels; c < CH_MAX; ++c)
			m_ring[c][m_ringWrite] = 0.0f;
		m_ringWrite = (m_ringWrite + 1) % RING_SAMPLES;
		if (m_ringFilled < RING_SAMPLES) ++m_ringFilled;
	}
	m_accSamples += frames;

	// 簡易 LUFS(モーメンタリ近似): ProAudio と同じ 10*log10(mean)-0.691。K特性は省略。
	{
		double meanSq = 0.0;
		int used = 0;
		for (int c = 0; c < tpCh; ++c) {
			if (frameN[c] > 0) {
				meanSq += frameSumSq[c] / (double)frameN[c];
				++used;
			}
		}
		if (used > 0) {
			meanSq /= (double)used;
			m_lufsSum += meanSq * (double)frames;
			m_lufsCount += (double)frames;
			if (meanSq < 1e-12) meanSq = 1e-12;
			const float block = 10.0f * (float)log10(meanSq) - 0.691f;
			if (block > m_lufsMom)
				m_lufsMom = m_lufsMom * 0.60f + block * 0.40f;
			else
				m_lufsMom = m_lufsMom * 0.92f + block * 0.08f;
			if (m_lufsMom < -70.0f) m_lufsMom = -70.0f;
		}
		m_tpLin = framePeakTp;
		if (framePeakTp > m_tpHold)
			m_tpHold = framePeakTp;
		else
			m_tpHold *= 0.9995f;
	}

	float maxPk = 0.0f;
	const int meterCh = (std::min)(channels, CH_MAX);
	for (int c = 0; c < meterCh; ++c) {
		float rms = 0.0f;
		if (frameN[c] > 0)
			rms = (float)sqrt(frameSumSq[c] / (double)frameN[c]);
		if (rms > m_meterRms[c])
			m_meterRms[c] = m_meterRms[c] * 0.55f + rms * 0.45f;
		else
			m_meterRms[c] = m_meterRms[c] * 0.92f + rms * 0.08f;

		// ピークホールドも RMS の最大値を保持(サンプルピークではない)
		if (m_meterRms[c] >= m_meterHold[c])
			m_meterHold[c] = m_meterRms[c];
		else
			m_meterHold[c] = m_meterHold[c] * 0.997f;

		// 参考用にサンプルピークも残す(描画では使わない)
		if (framePeak[c] > m_meterPeak[c])
			m_meterPeak[c] = framePeak[c];
		else
			m_meterPeak[c] = m_meterPeak[c] * 0.92f + framePeak[c] * 0.08f;

		// 波形オートレンジは従来通り L/R 基準(LFE等で縮小しないように)
		if (c < 2 && framePeak[c] > maxPk) maxPk = framePeak[c];
	}
	// 波形オートレンジはサンプルピーク基準(min/max描画用)
	if (maxPk > m_waveDispPeak)
		m_waveDispPeak = m_waveDispPeak * 0.70f + maxPk * 0.30f;
	else
		m_waveDispPeak = m_waveDispPeak * 0.997f + maxPk * 0.003f;
	if (m_waveDispPeak < 0.06f) m_waveDispPeak = 0.06f;
	if (m_waveDispPeak > 1.0f) m_waveDispPeak = 1.0f;

	const int spc = (std::max)(4, m_samplesPerCol);
	int pushed = 0;
	const int pushCap = (m_waveW > 0) ? m_waveW : 120;
	while (m_accSamples >= spc && pushed < pushCap) {
		m_accSamples -= spc;
		++m_pendingScroll;
		++pushed;
	}
	const int cap = (m_waveW > 0) ? m_waveW : 640;
	if (m_pendingScroll > cap)
		m_pendingScroll = cap;
	LeaveCriticalSection(&m_cs);
	RequestSpecAnalysis();
	KickUiPresent();
}

bool CAnalyzerDlg::SnapshotRing(int& outWrite, int& outFilled, int& outChannels)
{
	EnterCriticalSection(&m_cs);
	outChannels = (std::max)(1, (std::min)(m_channels, CH_MAX));
	outWrite = m_ringWrite;
	outFilled = m_ringFilled;
	if (outFilled <= 0) {
		LeaveCriticalSection(&m_cs);
		return false;
	}
	for (int c = 0; c < outChannels; ++c)
		memcpy(m_ringSnap[c].data(), m_ring[c].data(), RING_SAMPLES * sizeof(float));
	LeaveCriticalSection(&m_cs);
	return true;
}

void CAnalyzerDlg::UpdateSpectrumFromRing()
{
	// 波形と同じタイマー周期で更新(バックバッファへ描いてから Present)。間引きしない。
	int channels = 0, sr = 44100, filled = 0, write = 0;
	if (!SnapshotRing(write, filled, channels))
		return;
	EnterCriticalSection(&m_cs);
	sr = m_sampleRate > 0 ? m_sampleRate : 44100;
	LeaveCriticalSection(&m_cs);
	if (filled < 64) return;

	const int use = (std::min)(filled, FFT_SIZE);
	int start = write - use;
	while (start < 0) start += RING_SAMPLES;

	const float nyquist = (float)sr * 0.5f;
	// |X|*2/N。Hann 時は正弦≈-6dBFS 読み(窓補正なし)。過大表示を避けるため窓補正はしない。
	const float norm = 2.0f / (float)FFT_SIZE;
	const int kNyq = FFT_SIZE / 2;
	float magLin[FFT_SIZE / 2 + 1];
	float instant[CH_MAX][SPEC_BINS];
	float smoothed[SPEC_BINS];

	auto magAt = [&](float kFrac) -> float {
		if (kFrac < 1.0f) kFrac = 1.0f;
		if (kFrac > (float)(kNyq - 1)) kFrac = (float)(kNyq - 1);
		const int k0 = (int)kFrac;
		const float t = kFrac - (float)k0;
		return magLin[k0] * (1.0f - t) + magLin[k0 + 1] * t;
	};

	for (int c = 0; c < channels; ++c) {
		for (int i = 0; i < FFT_SIZE; ++i) {
			float v = 0.0f;
			if (i >= FFT_SIZE - use) {
				const int src = (start + (i - (FFT_SIZE - use))) % RING_SAMPLES;
				v = m_ringSnap[c][src];
			}
			m_fftRe[i] = v * m_fftWindow[i];
			m_fftIm[i] = 0.0f;
		}
		FftRadix2(m_fftRe.data(), m_fftIm.data(), FFT_SIZE);

		magLin[0] = 0.0f;
		for (int k = 1; k <= kNyq; ++k)
			magLin[k] = sqrtf(m_fftRe[k] * m_fftRe[k] + m_fftIm[k] * m_fftIm[k]) * norm;

		// 対数帯域ごとにエネルギー平均。帯域幅 < 1 FFT bin なら線形補間
		// (低域の階段化を防ぎ、高域のピーク固まりも抑える)
		for (int b = 0; b < SPEC_BINS; ++b) {
			const float fLo = SpecEdgeHz(b, SPEC_BINS, nyquist);
			const float fHi = SpecEdgeHz(b + 1, SPEC_BINS, nyquist);
			float kLo = fLo * (float)FFT_SIZE / (float)sr;
			float kHi = fHi * (float)FFT_SIZE / (float)sr;
			if (kLo < 1.0f) kLo = 1.0f;
			if (kHi > (float)kNyq) kHi = (float)kNyq;
			if (kHi <= kLo) kHi = kLo + 0.05f;

			float energy = 0.0f;
			float weight = 0.0f;
			if (kHi - kLo < 1.0f) {
				const float m = magAt(0.5f * (kLo + kHi));
				energy = m * m;
				weight = 1.0f;
			}
			else {
				const int i0 = (int)kLo;
				const int i1 = (int)kHi;
				for (int k = i0; k <= i1; ++k) {
					if (k < 1 || k > kNyq) continue;
					const float a = (std::max)(kLo, (float)k);
					const float bb = (std::min)(kHi, (float)(k + 1));
					const float w = bb - a;
					if (w <= 0.0f) continue;
					const float m = magLin[k];
					energy += m * m * w;
					weight += w;
				}
			}
			const float rms = (weight > 1e-12f) ? sqrtf(energy / weight) : 0.0f;
			instant[c][b] = AmpToDb(rms);
		}

		// 周波数方向の軽い平滑(ギザギザ／固まり感を減らす)
		for (int b = 0; b < SPEC_BINS; ++b) {
			const float l = instant[c][(std::max)(0, b - 1)];
			const float m = instant[c][b];
			const float r = instant[c][(std::min)(SPEC_BINS - 1, b + 1)];
			smoothed[b] = l * 0.18f + m * 0.64f + r * 0.18f;
		}
		for (int b = 0; b < SPEC_BINS; ++b)
			instant[c][b] = smoothed[b];
	}

	EnterCriticalSection(&m_cs);
	const bool peakHold = m_peakHold;
	for (int c = 0; c < channels; ++c) {
		for (int b = 0; b < SPEC_BINS; ++b) {
			if (instant[c][b] > m_specDb[c][b])
				m_specDb[c][b] = instant[c][b];
			else
				m_specDb[c][b] = m_specDb[c][b] * 0.78f + instant[c][b] * 0.22f;
			if (peakHold) {
				if (instant[c][b] >= m_specPeakDb[c][b])
					m_specPeakDb[c][b] = instant[c][b];
				else
					m_specPeakDb[c][b] = m_specPeakDb[c][b] * 0.985f + instant[c][b] * 0.015f;
			}
			else {
				m_specPeakDb[c][b] = m_specDb[c][b];
			}
		}
	}
	LeaveCriticalSection(&m_cs);
	PushWaterfallRow();
	m_specDirty = true;
}

void CAnalyzerDlg::ReleaseBuffers()
{
	if (m_waveScratchDC.GetSafeHdc()) {
		if (m_waveScratchOld) m_waveScratchDC.SelectObject(m_waveScratchOld);
		m_waveScratchOld = nullptr;
		if (m_waveScratchBmp.GetSafeHandle()) m_waveScratchBmp.DeleteObject();
		m_waveScratchDC.DeleteDC();
	}
	if (m_waveDC.GetSafeHdc()) {
		if (m_waveOld) m_waveDC.SelectObject(m_waveOld);
		m_waveOld = nullptr;
		if (m_waveBmp.GetSafeHandle()) m_waveBmp.DeleteObject();
		m_waveDC.DeleteDC();
	}
	if (m_specDC.GetSafeHdc()) {
		if (m_specOld) m_specDC.SelectObject(m_specOld);
		m_specOld = nullptr;
		if (m_specBmp.GetSafeHandle()) m_specBmp.DeleteObject();
		m_specDC.DeleteDC();
	}
	if (m_frameDC.GetSafeHdc()) {
		if (m_frameOld) m_frameDC.SelectObject(m_frameOld);
		m_frameOld = nullptr;
		if (m_frameBmp.GetSafeHandle()) m_frameBmp.DeleteObject();
		m_frameDC.DeleteDC();
	}
	m_waveW = m_waveH = 0;
	m_specW = m_specH = 0;
	m_frameW = m_frameH = 0;
	m_waveReady = m_specReady = false;
#if CCUSTOM_AERO_SUPPORT
	m_chromaCache.Release();
	m_chromaReady = false;
	m_chromaW = m_chromaH = 0;
	m_lastWaveScroll = 0;
#endif
}

bool CAnalyzerDlg::EnsureWaveBuffer(CDC& refDC, int w, int h)
{
	if (w <= 0 || h <= 0) return false;
	if (m_waveDC.GetSafeHdc() && m_waveW == w && m_waveH == h && m_waveScratchDC.GetSafeHdc())
		return true;

	if (m_waveScratchDC.GetSafeHdc()) {
		if (m_waveScratchOld) m_waveScratchDC.SelectObject(m_waveScratchOld);
		m_waveScratchOld = nullptr;
		if (m_waveScratchBmp.GetSafeHandle()) m_waveScratchBmp.DeleteObject();
		m_waveScratchDC.DeleteDC();
	}
	if (m_waveDC.GetSafeHdc()) {
		if (m_waveOld) m_waveDC.SelectObject(m_waveOld);
		m_waveOld = nullptr;
		if (m_waveBmp.GetSafeHandle()) m_waveBmp.DeleteObject();
		m_waveDC.DeleteDC();
	}

	if (!m_waveDC.CreateCompatibleDC(&refDC)) return false;
	if (!m_waveBmp.CreateCompatibleBitmap(&refDC, w, h)) { m_waveDC.DeleteDC(); return false; }
	m_waveOld = m_waveDC.SelectObject(&m_waveBmp);

	if (!m_waveScratchDC.CreateCompatibleDC(&refDC)) return false;
	if (!m_waveScratchBmp.CreateCompatibleBitmap(&refDC, w, h)) return false;
	m_waveScratchOld = m_waveScratchDC.SelectObject(&m_waveScratchBmp);

	m_waveW = w;
	m_waveH = h;
	m_waveReady = false;
	return true;
}

bool CAnalyzerDlg::EnsureSpecBuffer(CDC& refDC, int w, int h)
{
	if (w <= 0 || h <= 0) return false;
	if (m_specDC.GetSafeHdc() && m_specW == w && m_specH == h)
		return true;
	if (m_specDC.GetSafeHdc()) {
		if (m_specOld) m_specDC.SelectObject(m_specOld);
		m_specOld = nullptr;
		if (m_specBmp.GetSafeHandle()) m_specBmp.DeleteObject();
		m_specDC.DeleteDC();
	}
	if (!m_specDC.CreateCompatibleDC(&refDC)) return false;
	if (!m_specBmp.CreateCompatibleBitmap(&refDC, w, h)) { m_specDC.DeleteDC(); return false; }
	m_specOld = m_specDC.SelectObject(&m_specBmp);
	m_specW = w;
	m_specH = h;
	m_specReady = false;
	m_specDirty = true;
	return true;
}

bool CAnalyzerDlg::EnsureFrameBuffer(CDC& refDC, int w, int h)
{
	if (w <= 0 || h <= 0) return false;
	if (m_frameDC.GetSafeHdc() && m_frameW == w && m_frameH == h)
		return true;
	if (m_frameDC.GetSafeHdc()) {
		if (m_frameOld) m_frameDC.SelectObject(m_frameOld);
		m_frameOld = nullptr;
		if (m_frameBmp.GetSafeHandle()) m_frameBmp.DeleteObject();
		m_frameDC.DeleteDC();
	}
	if (!m_frameDC.CreateCompatibleDC(&refDC)) return false;
	if (!m_frameBmp.CreateCompatibleBitmap(&refDC, w, h)) { m_frameDC.DeleteDC(); return false; }
	m_frameOld = m_frameDC.SelectObject(&m_frameBmp);
	m_frameW = w;
	m_frameH = h;
	return true;
}

void CAnalyzerDlg::KickUiPresent()
{
	// 多重 Post を防ぐ。表示キックは ms2（ピアノ/EQ/MP と帯域を分け合う）
	if (!::IsWindow(m_hWnd)) return;
	if (InterlockedCompareExchange(&m_fullRedrawBusy, 0, 0) != 0) {
		InterlockedExchange(&m_presentDeferred, 1);
		return;
	}
	const DWORD now = GetTickCount();
	if (InterlockedCompareExchange(&m_presentPosted, 0, 0) != 0) {
		// Invalidate 待ち固着時のみ強制描画して開放
		if (m_lastPresentKickTick != 0 && (now - m_lastPresentKickTick) >= 150u) {
			MSG msg;
			while (::PeekMessage(&msg, m_hWnd, WM_ANALYZER_PRESENT, WM_ANALYZER_PRESENT, PM_REMOVE)) {}
			AnalyzerPresentInvalidate(m_hWnd);
			UpdateWindow();
			InterlockedExchange(&m_presentPosted, 0);
		}
		else {
			return;
		}
	}
	int minMs = savedata.ms2;
	if (minMs < 16) minMs = 16;
	if (minMs > 960) minMs = 960;
	if (m_lastPresentKickTick != 0 && (now - m_lastPresentKickTick) < (DWORD)minMs)
		return;
	if (InterlockedCompareExchange(&m_presentPosted, 1, 0) != 0) return;
	m_lastPresentKickTick = now;
	if (!PostMessage(WM_ANALYZER_PRESENT, 0, 0))
		InterlockedExchange(&m_presentPosted, 0);
}

void CAnalyzerDlg::FullRedrawWave(COLORREF bg)
{
	if (!m_waveDC.GetSafeHdc() || m_waveW <= 0 || m_waveH <= 0) return;
	// 全再描画なので溜まった scroll は無効（完了後の catch-up 嵐を防ぐ）
	EnterCriticalSection(&m_cs);
	m_pendingScroll = 0;
	LeaveCriticalSection(&m_cs);
	m_waveDC.FillSolidRect(0, 0, m_waveW, m_waveH, bg);

	int channels = 0, filled = 0, write = 0;
	if (!SnapshotRing(write, filled, channels)) {
		m_waveReady = true;
		return;
	}
	int spc = 64;
	EnterCriticalSection(&m_cs);
	spc = (std::max)(4, m_samplesPerCol);
	LeaveCriticalSection(&m_cs);

	const int vis = VisibleChannelCount(m_waveH);
	m_waveLayoutCh = vis;
	if (filled < 8) {
		m_waveReady = true;
		return;
	}

	const int bandH = m_waveH / vis;
	CFont* oldFont = m_waveDC.SelectObject(&m_font);
	m_waveDC.SetBkMode(TRANSPARENT);
	float dispPeak = 0.25f;
	EnterCriticalSection(&m_cs);
	dispPeak = m_waveDispPeak;
	LeaveCriticalSection(&m_cs);
	// 直近ピークを帯の ~72% に合わせる(常時フル振りを避ける)。静音は拡大しすぎない。
	float waveGain = 0.70f / (std::max)(0.08f, dispPeak);
	if (waveGain > 2.2f) waveGain = 2.2f;
	if (waveGain < 0.55f) waveGain = 0.55f;

	// トリガー式オシロ: リングの直近ウィンドウを立ち上がりゼロクロスに合わせて静止表示。
	// 表示窓はリング内に必ず収める(スクロール用の spc だと窓長がリングを超える)。
	const bool trigger = (m_waveMode == WaveTrigger);
	int trigSpc = spc;
	int trigStart = 0;
	if (trigger) {
		const int budget = (std::min)(filled, RING_SAMPLES / 2);
		trigSpc = (std::max)(1, budget / (std::max)(1, m_waveW));
		const int window = trigSpc * m_waveW;
		int base = write - window;
		int searchMax = (std::min)(RING_SAMPLES / 4, filled - window);
		if (searchMax < 0) searchMax = 0;
		trigStart = base;
		for (int k = 0; k < searchMax; ++k) {
			const int cur = ((base - k) % RING_SAMPLES + RING_SAMPLES) % RING_SAMPLES;
			const int prv = ((base - k - 1) % RING_SAMPLES + RING_SAMPLES) % RING_SAMPLES;
			if (m_ringSnap[0][prv] <= 0.0f && m_ringSnap[0][cur] > 0.0f) {
				trigStart = base - k;
				break;
			}
		}
	}

	for (int c = 0; c < vis; ++c) {
		const int y0 = c * bandH;
		const int y1 = (c == vis - 1) ? m_waveH : (c + 1) * bandH;
		const int mid = (y0 + y1) / 2;
		const int amp = (std::max)(2, (y1 - y0) / 2 - 2);

		HGDIOBJ oldPen = m_waveDC.SelectObject(::GetStockObject(DC_PEN));
		::SetDCPenColor(m_waveDC.GetSafeHdc(), RGB(40, 44, 58));
		m_waveDC.MoveTo(0, mid);
		m_waveDC.LineTo(m_waveW, mid);

		::SetDCPenColor(m_waveDC.GetSafeHdc(), kChColor[c % CH_MAX]);

		for (int x = 0; x < m_waveW; ++x) {
			const int ageCols = m_waveW - 1 - x;
			const int sampleBack = trigger
				? (write - (trigStart + x * trigSpc))
				: (ageCols * spc + spc);
			const int useSpc = trigger ? trigSpc : spc;
			float mn = 1.0f, mx = -1.0f;
			bool any = false;
			for (int s = 0; s < useSpc; ++s) {
				int idx = write - sampleBack + s;
				while (idx < 0) idx += RING_SAMPLES;
				idx %= RING_SAMPLES;
				if (filled < RING_SAMPLES && idx >= filled) continue;
				float v = m_ringSnap[c][idx];
				if (v > 1.0f) v = 1.0f;
				if (v < -1.0f) v = -1.0f;
				if (!any || v < mn) mn = v;
				if (!any || v > mx) mx = v;
				any = true;
			}
			if (!any) continue;
			mn *= waveGain; mx *= waveGain;
			if (mn < -1.0f) mn = -1.0f;
			if (mx > 1.0f) mx = 1.0f;
			m_waveDC.MoveTo(x, mid - (int)(mx * amp));
			m_waveDC.LineTo(x, mid - (int)(mn * amp));
		}

		m_waveDC.FillSolidRect(0, y0, 28, (std::min)(14, y1 - y0), ANALYZER_LABEL_PLATE);
		m_waveDC.SetTextColor(kChColor[c % CH_MAX]);
		m_waveDC.TextOut(4, y0 + 2, ChannelLabel(c, channels));
		m_waveDC.SelectObject(oldPen);
	}
	m_waveDC.SelectObject(oldFont);
	m_waveReady = true;
	EnterCriticalSection(&m_cs);
	m_pendingScroll = 0;
	LeaveCriticalSection(&m_cs);
}

int CAnalyzerDlg::ScrollWaveAndDrawNew(COLORREF bg, int maxScroll)
{
	if (!m_waveDC.GetSafeHdc() || !m_waveScratchDC.GetSafeHdc()) return 0;
	if (m_waveMode == WaveTrigger) {
		// トリガー表示は毎回全描画(横スクロールしない)
		FullRedrawWave(bg);
		return -1;
	}

	int scroll = 0, channels = 0, write = 0, filled = 0, spc = 64;
	EnterCriticalSection(&m_cs);
	scroll = m_pendingScroll;
	if (maxScroll > 0 && scroll > maxScroll)
		scroll = maxScroll;
	m_pendingScroll -= scroll;
	spc = (std::max)(4, m_samplesPerCol);
	LeaveCriticalSection(&m_cs);

	const int vis = VisibleChannelCount(m_waveH);
	if (vis != m_waveLayoutCh || !m_waveReady) {
		FullRedrawWave(bg);
		return -1;
	}
	if (scroll <= 0) return 0;
	if (scroll >= m_waveW) {
		FullRedrawWave(bg);
		return -1;
	}
	if (!SnapshotRing(write, filled, channels))
		return 0;

	const int keep = m_waveW - scroll;
	m_waveScratchDC.BitBlt(0, 0, keep, m_waveH, &m_waveDC, scroll, 0, SRCCOPY);
	m_waveScratchDC.FillSolidRect(keep, 0, scroll, m_waveH, bg);
	m_waveDC.BitBlt(0, 0, m_waveW, m_waveH, &m_waveScratchDC, 0, 0, SRCCOPY);

	const int bandH = m_waveH / vis;
	CFont* oldFont = m_waveDC.SelectObject(&m_font);
	m_waveDC.SetBkMode(TRANSPARENT);
	float dispPeak = 0.25f;
	EnterCriticalSection(&m_cs);
	dispPeak = m_waveDispPeak;
	LeaveCriticalSection(&m_cs);
	float waveGain = 0.70f / (std::max)(0.08f, dispPeak);
	if (waveGain > 2.2f) waveGain = 2.2f;
	if (waveGain < 0.55f) waveGain = 0.55f;

	for (int c = 0; c < vis; ++c) {
		const int y0 = c * bandH;
		const int y1 = (c == vis - 1) ? m_waveH : (c + 1) * bandH;
		const int mid = (y0 + y1) / 2;
		const int amp = (std::max)(2, (y1 - y0) / 2 - 2);

		HGDIOBJ oldPen = m_waveDC.SelectObject(::GetStockObject(DC_PEN));
		::SetDCPenColor(m_waveDC.GetSafeHdc(), RGB(40, 44, 58));
		m_waveDC.MoveTo(keep, mid);
		m_waveDC.LineTo(m_waveW, mid);

		::SetDCPenColor(m_waveDC.GetSafeHdc(), kChColor[c % CH_MAX]);

		for (int x = keep; x < m_waveW; ++x) {
			const int ageCols = m_waveW - 1 - x;
			const int sampleBack = ageCols * spc + spc;
			float mn = 1.0f, mx = -1.0f;
			bool any = false;
			for (int s = 0; s < spc; ++s) {
				int idx = write - sampleBack + s;
				while (idx < 0) idx += RING_SAMPLES;
				idx %= RING_SAMPLES;
				if (filled < RING_SAMPLES && idx >= filled) continue;
				float v = m_ringSnap[c][idx];
				if (v > 1.0f) v = 1.0f;
				if (v < -1.0f) v = -1.0f;
				if (!any || v < mn) mn = v;
				if (!any || v > mx) mx = v;
				any = true;
			}
			if (!any) continue;
			mn *= waveGain; mx *= waveGain;
			if (mn < -1.0f) mn = -1.0f;
			if (mx > 1.0f) mx = 1.0f;
			m_waveDC.MoveTo(x, mid - (int)(mx * amp));
			m_waveDC.LineTo(x, mid - (int)(mn * amp));
		}

		m_waveDC.FillSolidRect(0, y0, 28, (std::min)(14, y1 - y0), ANALYZER_LABEL_PLATE);
		m_waveDC.SetTextColor(kChColor[c % CH_MAX]);
		m_waveDC.TextOut(4, y0 + 2, ChannelLabel(c, channels));
		m_waveDC.SelectObject(oldPen);
	}
	m_waveDC.SelectObject(oldFont);
	m_waveReady = true;
	return scroll;
}

void CAnalyzerDlg::DrawEqOverlay(CDC& dc, const CRect& plot, float nyquist)
{
	if (plot.Width() < 8 || plot.Height() < 8) return;

	float fMinZ = 20.0f, fMaxZ = nyquist;
	GetZoomFreqRange(m_freqZoom, nyquist, fMinZ, fMaxZ);
	auto hzToX = [&](float hz) -> int {
		return (int)HzToPlotX(hz, plot, fMinZ, fMaxZ);
	};
	// EQ ゲイン軸: 上=+12dB / 中=0 / 下=-12dB (equaliser と同じスケール)
	auto gainToY = [&](float gainDb) -> int {
		float t = (gainDb + 12.0f) / 24.0f;
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		return plot.bottom - (int)(t * plot.Height());
	};

	// ズーム時は範囲外の帯域を端に潰さない(掴める位置とカーブを一致させる)
	auto bandVisible = [&](int b) -> bool {
		const float f = kEqFreqs[b];
		return f < nyquist && f >= fMinZ * 0.999f && f <= fMaxZ * 1.001f;
	};

	CPen bandPen(PS_DOT, 1, RGB(90, 110, 70));
	CPen* oldPen = dc.SelectObject(&bandPen);
	for (int b = 0; b < EQ_OVERLAY_BANDS; ++b) {
		if (!bandVisible(b)) continue;
		const int x = hzToX(kEqFreqs[b]);
		dc.MoveTo(x, plot.top);
		dc.LineTo(x, plot.bottom);
	}

	const int y0 = gainToY(0.0f);
	CPen zeroPen(PS_SOLID, 1, RGB(120, 160, 90));
	CPen* prev = dc.SelectObject(&zeroPen);
	dc.MoveTo(plot.left, y0);
	dc.LineTo(plot.right, y0);

	POINT pts[EQ_OVERLAY_BANDS];
	int nPts = 0;
	for (int b = 0; b < EQ_OVERLAY_BANDS; ++b) {
		if (!bandVisible(b)) continue;
		int eqv = savedata.eq[b];
		if (eqv < 0) eqv = 0;
		if (eqv > 200) eqv = 200;
		pts[nPts].x = hzToX(kEqFreqs[b]);
		pts[nPts].y = gainToY(EqSliderToDb(eqv));
		++nPts;
	}
	if (nPts >= 2) {
		CPen eqPen(PS_SOLID, 2, RGB(200, 255, 120));
		CPen* prev2 = dc.SelectObject(&eqPen);
		dc.Polyline(pts, nPts);
		for (int i = 0; i < nPts; ++i) {
			dc.FillSolidRect(pts[i].x - 2, pts[i].y - 2, 5, 5, RGB(220, 255, 160));
		}
		dc.SelectObject(prev2 ? prev2 : prev);
	}
	dc.SetTextColor(RGB(180, 220, 120));
	dc.TextOut(plot.left + 4, plot.top + 2, _T("EQ"));
	dc.SelectObject(oldPen);
}

void CAnalyzerDlg::DrawSpecPanel(CDC& dc, const CRect& plot, int chBegin, int chCount,
	float spec[][SPEC_BINS], float peak[][SPEC_BINS], int channels, int sr, bool drawTitle)
{
	if (plot.Width() < 8 || plot.Height() < 8 || chCount <= 0) return;
	const float nyquist = (float)sr * 0.5f;
	const int style = m_specStyle;
	const bool drawPeak = m_peakHold;

	CPen frame(PS_SOLID, 1, RGB(55, 60, 78));
	CPen* oldPen = dc.SelectObject(&frame);
	dc.SelectStockObject(NULL_BRUSH);
	dc.Rectangle(plot);
	dc.SelectObject(oldPen); // frame 破棄前に復元

	if (drawTitle) {
		dc.SetTextColor(RGB(180, 190, 210));
		dc.TextOut(plot.left + 2, (int)(std::max)(0L, plot.top - 13),
			LL14(L"周波数特性", L"Frequency response", L"Reponse en frequence", L"Risposta in frequenza",
				L"Respuesta en frecuencia", L"주파수 특성", L"频率特性", L"الاستجابة الترددية",
				L"АЧХ", L"Frequenzgang", L"Resposta em frequencia", L"Frequentierespons",
				L"Charakterystyka", L"Frekans yaniti"));
	}

	// 表示レンジ: 通常 -96..0dB / 差分 -48..+48dB(0dB が中央)
	const bool diff = (m_specDiff && m_specSnapValid);
	float fMinZ = 20.0f, fMaxZ = nyquist;
	GetZoomFreqRange(m_freqZoom, nyquist, fMinZ, fMaxZ);

	auto dbToY = [&](float db) -> int {
		float t = diff ? ((db + 48.0f) / 96.0f) : ((db + 96.0f) / 96.0f);
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		return plot.bottom - (int)(t * plot.Height());
	};
	auto hzToX = [&](float hz) -> int {
		return (int)HzToPlotX(hz, plot, fMinZ, fMaxZ);
	};
	// 塗り/バーの基準線。差分表示では 0dB(中央)から上下に伸ばす。
	const int base0 = diff ? dbToY(0.0f) : (plot.bottom - 1);

	CPen grid(PS_DOT, 1, RGB(45, 50, 65));
	oldPen = dc.SelectObject(&grid);
	dc.SetTextColor(RGB(120, 130, 150));
	for (int db = diff ? 40 : 0; db >= (diff ? -40 : -80); db -= 20) {
		const int y = dbToY((float)db);
		if (y <= plot.top || y >= plot.bottom) continue;
		dc.MoveTo(plot.left, y);
		dc.LineTo(plot.right, y);
		if (drawTitle && plot.Width() > 60) {
			CString lab;
			lab.Format(_T("%d"), db);
			dc.TextOut(plot.left - 26, y - 6, lab);
		}
	}

	const int markHz[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
	for (int mi = 0; mi < 10; ++mi) {
		const float f = (float)markHz[mi];
		if (f >= nyquist) continue;
		if (f < fMinZ || f > fMaxZ) continue; // ズーム範囲外の目盛りは出さない
		const int x = hzToX(f);
		dc.MoveTo(x, plot.top);
		dc.LineTo(x, plot.bottom);
		if (drawTitle && plot.Height() > 40) {
			CString lab;
			if (markHz[mi] >= 1000) lab.Format(_T("%dk"), markHz[mi] / 1000);
			else lab.Format(_T("%d"), markHz[mi]);
			dc.TextOut(x - 6, plot.bottom - 12, lab);
		}
	}

	// ズーム範囲に入るビンだけを使う(範囲外を端に潰すと縦線状の塊になる)
	int binLo = 0, binHi = SPEC_BINS - 1;
	if (m_freqZoom != ZoomFull) {
		binLo = SPEC_BINS; binHi = -1;
		for (int b = 0; b < SPEC_BINS; ++b) {
			const float cHz = SpecCenterHz(b, SPEC_BINS, nyquist);
			if (cHz < fMinZ || cHz > fMaxZ) continue;
			if (b < binLo) binLo = b;
			if (b > binHi) binHi = b;
		}
		if (binHi < binLo) { binLo = 0; binHi = SPEC_BINS - 1; }
		else {
			if (binLo > 0) --binLo;             // 端が切れて見えないように1つ外側まで
			if (binHi < SPEC_BINS - 1) ++binHi;
		}
	}
	const int nPts = binHi - binLo + 1;

	for (int i = 0; i < chCount; ++i) {
		const int c = chBegin + i;
		if (c < 0 || c >= channels || c >= CH_MAX) continue;

		POINT fillPts[SPEC_BINS + 2];
		POINT peakPts[SPEC_BINS];
		POINT linePts[SPEC_BINS];
		for (int b = binLo; b <= binHi; ++b) {
			const int k = b - binLo;
			const int x = hzToX(SpecCenterHz(b, SPEC_BINS, nyquist));
			linePts[k].x = x;
			linePts[k].y = dbToY(diff ? (spec[c][b] - m_specSnapDb[c][b]) : spec[c][b]);
			peakPts[k].x = x;
			peakPts[k].y = dbToY(diff ? (peak[c][b] - m_specSnapDb[c][b]) : peak[c][b]);
			fillPts[k] = linePts[k];
		}
		fillPts[nPts].x = linePts[nPts - 1].x;
		fillPts[nPts].y = base0;
		fillPts[nPts + 1].x = linePts[0].x;
		fillPts[nPts + 1].y = base0;

		const COLORREF col = kChColor[c];
		const COLORREF fill = RGB(
			(GetRValue(col) * 2 + 18) / 5,
			(GetGValue(col) * 2 + 20) / 5,
			(GetBValue(col) * 2 + 28) / 5);
		const COLORREF fillSoft = RGB(
			(GetRValue(col) + 18 * 3) / 4,
			(GetGValue(col) + 20 * 3) / 4,
			(GetBValue(col) + 28 * 3) / 4);
		const COLORREF fillCubase = RGB(
			(GetRValue(col) + 40) / 3,
			(GetGValue(col) + 90) / 3,
			(GetBValue(col) + 110) / 3);

		const bool isBars = (style == StyleBars || style == StyleSpan || style == StyleAbleton);
		const bool isFill = (style == StyleFill || style == StyleCubase || style == StyleFabFilter);
		const bool isLine = (style == StyleLine || style == StyleFill || style == StyleCubase
			|| style == StyleFabFilter);

		if (isBars) {
			int barW;
			if (style == StyleSpan)
				barW = (std::max)(1, plot.Width() / nPts); // 密着
			else if (style == StyleAbleton)
				barW = (std::max)(2, plot.Width() / (std::max)(1, nPts / 2) - 2); // やや太い
			else
				barW = (std::max)(1, plot.Width() / nPts - 1);

			for (int b = binLo; b <= binHi; ++b) {
				if (style == StyleAbleton && (b & 1)) continue; // 間引いてAbleton風の区切り
				const int k = b - binLo;
				const int x = linePts[k].x;
				const int y = linePts[k].y;
				const int hw = barW / 2;
				CRect bar(x - hw, y, x - hw + barW, base0);
				if (bar.top > bar.bottom) { const int t = bar.top; bar.top = bar.bottom; bar.bottom = t; }
				if (bar.Width() < 1) bar.right = bar.left + 1;
				dc.FillSolidRect(bar, (style == StyleSpan) ? fill : fillSoft);
				dc.FillSolidRect(x - hw, y, barW, (style == StyleSpan) ? 1 : 2, col);
				if (drawPeak && style == StyleSpan) {
					dc.FillSolidRect(peakPts[k].x - 1, peakPts[k].y, 3, 2,
						RGB((GetRValue(col) + 255) / 2, (GetGValue(col) + 255) / 2, (GetBValue(col) + 255) / 2));
				}
			}
			if (drawPeak && style == StyleAbleton) {
				for (int b = binLo; b <= binHi; b += 2) {
					const int k = b - binLo;
					dc.FillSolidRect(peakPts[k].x - 1, peakPts[k].y, 3, 2,
						RGB((GetRValue(col) + 255) / 2, (GetGValue(col) + 255) / 2, (GetBValue(col) + 255) / 2));
				}
			}
			else if (drawPeak && style == StyleBars) {
				for (int k = 0; k < nPts; ++k) {
					dc.FillSolidRect(peakPts[k].x - 1, peakPts[k].y, 3, 2,
						RGB((GetRValue(col) + 255) / 2, (GetGValue(col) + 255) / 2, (GetBValue(col) + 255) / 2));
				}
			}
		}
		else if (isFill) {
			CBrush br((style == StyleCubase) ? fillCubase
				: (style == StyleFabFilter) ? fillSoft : fill);
			CBrush* oldBr = dc.SelectObject(&br);
			dc.SelectStockObject(NULL_PEN);
			dc.SetPolyFillMode(WINDING);
			dc.Polygon(fillPts, nPts + 2);
			dc.SelectObject(oldBr);
		}

		if (drawPeak && !isBars) {
			CPen peakPen(PS_SOLID, 1, RGB(
				(GetRValue(col) * 2 + 255) / 3,
				(GetGValue(col) * 2 + 255) / 3,
				(GetBValue(col) * 2 + 255) / 3));
			CPen* prev = dc.SelectObject(&peakPen);
			dc.Polyline(peakPts, nPts);
			dc.SelectObject(prev ? prev : oldPen);
		}

		if (isLine) {
			const int penW = (style == StyleFabFilter) ? 1
				: (style == StyleCubase) ? 2 : 2;
			CPen curve(PS_SOLID, penW, col);
			CPen* prev = dc.SelectObject(&curve);
			dc.Polyline(linePts, nPts);
			dc.SelectObject(prev ? prev : oldPen);
		}

		dc.SetTextColor(col);
		dc.TextOut(plot.right - 28, plot.top + 2 + i * 12, ChannelLabel(c, channels));
	}

	if (diff) {
		// 0dB 基準線(差分表示であることを明示)
		dc.FillSolidRect(plot.left, base0, plot.Width(), 1, RGB(120, 130, 160));
		if (drawTitle) {
			dc.SetTextColor(RGB(200, 190, 140));
			dc.TextOut(plot.left + 34, plot.top + 2,
				LL14(L"差分", L"Diff", L"Diff.", L"Diff.", L"Dif.", L"차분", L"差分", L"فرق",
					L"Разн.", L"Diff.", L"Dif.", L"Versch.", L"Roznica", L"Fark"));
		}
	}

	if (m_eqOverlay)
		DrawEqOverlay(dc, plot, nyquist);
	DrawFreqMarkers(dc, plot, nyquist);

	dc.SelectObject(oldPen);
}

void CAnalyzerDlg::DrawFreqMarkers(CDC& dc, const CRect& plot, float nyquist)
{
	if (plot.Width() < 8 || plot.Height() < 8) return;
	float fMinZ = 20.0f, fMaxZ = nyquist;
	GetZoomFreqRange(m_freqZoom, nyquist, fMinZ, fMaxZ);

	CPen mk(PS_DOT, 1, RGB(255, 170, 90));
	CPen* oldPen = dc.SelectObject(&mk);
	int shown = 0;
	for (int i = 0; i < MARKER_MAX; ++i) {
		const int hz = savedata.analyzermarkers[i];
		if (hz <= 0) continue;
		const float f = (float)hz;
		if (f < fMinZ || f > fMaxZ || f >= nyquist) continue;
		const int x = (int)HzToPlotX(f, plot, fMinZ, fMaxZ);
		dc.MoveTo(x, plot.top);
		dc.LineTo(x, plot.bottom);
		if (plot.Height() > 40) {
			CString lab;
			if (hz >= 1000) lab.Format(_T("%.2fk"), (double)hz / 1000.0);
			else lab.Format(_T("%d"), hz);
			dc.SetTextColor(RGB(255, 190, 120));
			dc.TextOut((std::min)((long)x + 2, plot.right - 34),
				plot.top + 14 + (shown % 3) * 12, lab);
		}
		++shown;
	}
	dc.SelectObject(oldPen);
}

void CAnalyzerDlg::RedrawSpectrum(COLORREF bg)
{
	if (!m_specDC.GetSafeHdc() || m_specW <= 0 || m_specH <= 0) return;
	m_specDC.FillSolidRect(0, 0, m_specW, m_specH, bg);

	int channels = 2, sr = 44100, layout = SpecOverlay;
	float spec[CH_MAX][SPEC_BINS], peak[CH_MAX][SPEC_BINS];
	EnterCriticalSection(&m_cs);
	channels = (std::max)(1, (std::min)(m_channels, CH_MAX));
	sr = m_sampleRate > 0 ? m_sampleRate : 44100;
	layout = m_specLayout;
	memcpy(spec, m_specDb, sizeof(spec));
	memcpy(peak, m_specPeakDb, sizeof(peak));
	LeaveCriticalSection(&m_cs);

	CFont* oldFont = m_specDC.SelectObject(&m_font);
	m_specDC.SetBkMode(TRANSPARENT);

	const int gap = 4;
	const int padL = 4, padR = 4, padT = 16, padB = 4;
	CRect area(padL, padT, m_specW - padR, m_specH - padB);

	m_hoverPlotCount = 0;
	auto addHoverPlot = [&](const CRect& plot, int ch) {
		if (m_hoverPlotCount >= HOVER_PLOT_MAX) return;
		m_hoverPlots[m_hoverPlotCount] = plot;
		m_hoverPlotCh[m_hoverPlotCount] = ch;
		++m_hoverPlotCount;
	};

	const float nyquist = (float)sr * 0.5f;

	// --- 位相スコープ(リサージュ) ---
	if (m_lowerMode == LowerPhase) {
		CRect plot(area.left + 4, area.top, area.right - 4, area.bottom - 4);
		// ホバー表は空にしない(他処理の前提)。位相中はホバー読取自体を抑止する。
		addHoverPlot(plot, 0);
		m_specDC.SetTextColor(RGB(180, 190, 210));
		m_specDC.TextOut(plot.left + 2, (int)(std::max)(0L, plot.top - 13),
			LL14(L"位相スコープ (L/R)", L"Phase scope (L/R)", L"Scope de phase (L/R)", L"Scope di fase (L/R)",
				L"Osciloscopio de fase (L/R)", L"위상 스코프 (L/R)", L"相位示波器 (L/R)", L"راسم الطور (L/R)",
				L"Фазовый скоп (L/R)", L"Phasenskop (L/R)", L"Escopo de fase (L/R)", L"Fasescoop (L/R)",
				L"Skop fazy (L/R)", L"Faz skopu (L/R)"));

		const int side = (std::min)(plot.Width(), plot.Height());
		CRect box(plot.left + (plot.Width() - side) / 2, plot.top + (plot.Height() - side) / 2,
			plot.left + (plot.Width() - side) / 2 + side, plot.top + (plot.Height() - side) / 2 + side);
		CPen frame(PS_SOLID, 1, RGB(55, 60, 78));
		CPen* oldPen = m_specDC.SelectObject(&frame);
		m_specDC.SelectStockObject(NULL_BRUSH);
		m_specDC.Rectangle(box);
		m_specDC.SelectObject(oldPen);

		if (channels < 2 || side < 24) {
			m_specDC.SetTextColor(RGB(200, 180, 140));
			m_specDC.TextOut(plot.left + 8, plot.top + 8,
				LL14(L"ステレオ音源でのみ表示されます。", L"Shown for stereo sources only.",
					L"Affiche uniquement pour les sources stereo.", L"Mostrato solo per sorgenti stereo.",
					L"Solo se muestra con fuentes estereo.", L"스테레오 음원에서만 표시됩니다.",
					L"仅在立体声音源时显示。", L"يظهر لمصادر ستيريو فقط.",
					L"Показывается только для стерео.", L"Nur bei Stereoquellen sichtbar.",
					L"Exibido apenas para fontes estereo.", L"Alleen bij stereobronnen.",
					L"Widoczne tylko dla zrodel stereo.", L"Sadece stereo kaynaklarda gosterilir."));
		}
		else {
			int wr = 0, wf = 0, wch = 0;
			if (SnapshotRing(wr, wf, wch) && wf >= 64) {
				const int cx = (box.left + box.right) / 2;
				const int cy = (box.top + box.bottom) / 2;
				const int r = side / 2 - 2;
				// 目安の十字(上=同相 / 横=逆相方向)
				CPen ax(PS_DOT, 1, RGB(50, 56, 72));
				CPen* prevAx = m_specDC.SelectObject(&ax);
				m_specDC.MoveTo(cx, box.top); m_specDC.LineTo(cx, box.bottom);
				m_specDC.MoveTo(box.left, cy); m_specDC.LineTo(box.right, cy);
				m_specDC.SelectObject(prevAx);

				const int useN = (std::min)(wf, 2048);
				const int step = (std::max)(1, useN / 512);
				POINT pts[513];
				int n = 0;
				for (int i = 0; i < useN && n < 512; i += step) {
					int idx = wr - useN + i;
					while (idx < 0) idx += RING_SAMPLES;
					idx %= RING_SAMPLES;
					float l = m_ringSnap[0][idx];
					float rr = m_ringSnap[1][idx];
					if (l > 1.0f) l = 1.0f; if (l < -1.0f) l = -1.0f;
					if (rr > 1.0f) rr = 1.0f; if (rr < -1.0f) rr = -1.0f;
					// 45度回転: 上=モノラル(同相)、左右=広がり
					const float mx = (l + rr) * 0.7071f;
					const float my = (l - rr) * 0.7071f;
					pts[n].x = cx + (int)(my * r);
					pts[n].y = cy - (int)(mx * r);
					++n;
				}
				if (n >= 2) {
					CPen liss(PS_SOLID, 1, RGB(120, 230, 170));
					CPen* prev = m_specDC.SelectObject(&liss);
					m_specDC.Polyline(pts, n);
					m_specDC.SelectObject(prev);
				}
			}
		}
		m_specDC.SelectObject(oldFont);
		m_specReady = true;
		m_specDirty = false;
		return;
	}

	// --- スペクトログラム(ウォーターフォール) ---
	if (m_lowerMode == LowerWaterfall) {
		int wfWrite = 0, wfFilled = 0;
		EnterCriticalSection(&m_cs);
		wfWrite = m_wfWrite;
		wfFilled = m_wfFilled;
		LeaveCriticalSection(&m_cs);

		float fMinZ = 20.0f, fMaxZ = nyquist;
		GetZoomFreqRange(m_freqZoom, nyquist, fMinZ, fMaxZ);
		int binLo = 0, binHi = SPEC_BINS - 1;
		if (m_freqZoom != ZoomFull) {
			binLo = SPEC_BINS; binHi = -1;
			for (int b = 0; b < SPEC_BINS; ++b) {
				const float cHz = SpecCenterHz(b, SPEC_BINS, nyquist);
				if (cHz < fMinZ || cHz > fMaxZ) continue;
				if (b < binLo) binLo = b;
				if (b > binHi) binHi = b;
			}
			if (binHi < binLo) { binLo = 0; binHi = SPEC_BINS - 1; }
		}
		const int imgW = binHi - binLo + 1;

		// 小さな画像(bins x WF_ROWS)を作って一括 StretchDIBits(1ピクセルずつ塗ると重い)。
		// 行は imgW 個ずつ詰める(DIB の行ストライド = imgW*4)。
		static DWORD img[WF_ROWS * SPEC_BINS];
		BITMAPINFO bi = {};
		bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bi.bmiHeader.biWidth = imgW;
		bi.bmiHeader.biHeight = -WF_ROWS; // トップダウン(先頭行=最新)
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;

		auto drawWf = [&](const CRect& plot, int ch) {
			if (plot.Width() < 8 || plot.Height() < 8) return;
			if (ch < 0) ch = 0;
			if (ch >= CH_MAX) ch = CH_MAX - 1;
			EnterCriticalSection(&m_cs);
			for (int r = 0; r < WF_ROWS; ++r) {
				const int src = ((wfWrite - 1 - r) % WF_ROWS + WF_ROWS) % WF_ROWS;
				const bool valid = (r < wfFilled);
				for (int b = binLo; b <= binHi; ++b) {
					const float db = valid ? m_wfHist[ch][src][b] : -96.0f;
					const COLORREF c = WaterfallColor(db);
					// DIB は BGRX 順 → little endian の DWORD では 0x00RRGGBB
					img[r * imgW + (b - binLo)] = ((DWORD)GetRValue(c) << 16)
						| ((DWORD)GetGValue(c) << 8) | (DWORD)GetBValue(c);
				}
			}
			LeaveCriticalSection(&m_cs);
			const int oldMode = m_specDC.SetStretchBltMode(COLORONCOLOR);
			::StretchDIBits(m_specDC.GetSafeHdc(),
				plot.left, plot.top, plot.Width(), plot.Height(),
				0, 0, imgW, WF_ROWS, img, &bi, DIB_RGB_COLORS, SRCCOPY);
			m_specDC.SetStretchBltMode(oldMode);

			CPen frame(PS_SOLID, 1, RGB(55, 60, 78));
			CPen* oldPen = m_specDC.SelectObject(&frame);
			m_specDC.SelectStockObject(NULL_BRUSH);
			m_specDC.Rectangle(plot);
			m_specDC.SelectObject(oldPen);
			if (m_eqOverlay)
				DrawEqOverlay(m_specDC, plot, nyquist);
			DrawFreqMarkers(m_specDC, plot, nyquist);
			m_specDC.SetTextColor(kChColor[ch]);
			m_specDC.TextOut(plot.right - 28, plot.top + 2, ChannelLabel(ch, channels));
		};

		m_specDC.SetTextColor(RGB(180, 190, 210));
		m_specDC.TextOut(area.left + 2, (int)(std::max)(0L, area.top - 13),
			LL14(L"スペクトログラム", L"Spectrogram", L"Spectrogramme", L"Spettrogramma",
				L"Espectrograma", L"스펙트로그램", L"频谱图", L"مخطط طيفي",
				L"Спектрограмма", L"Spektrogramm", L"Espectrograma", L"Spectrogram",
				L"Spektrogram", L"Spektrogram"));

		if (layout == SpecOverlay || channels <= 1) {
			// 重ね描きできないので先頭chのみ
			CRect plot(area.left + 4, area.top, area.right, area.bottom - 4);
			addHoverPlot(plot, 0);
			drawWf(plot, 0);
		}
		else if (layout == SpecSplitV) {
			const int n = channels;
			const int cellH = (area.Height() - gap * (n - 1)) / n;
			for (int i = 0; i < n; ++i) {
				CRect plot(area.left + 4, area.top + i * (cellH + gap),
					area.right, area.top + i * (cellH + gap) + cellH);
				addHoverPlot(plot, i);
				drawWf(plot, i);
			}
		}
		else if (layout == SpecSplitH) {
			const int n = channels;
			const int cellW = (area.Width() - gap * (n - 1)) / n;
			for (int i = 0; i < n; ++i) {
				CRect plot(area.left + i * (cellW + gap), area.top,
					area.left + i * (cellW + gap) + cellW, area.bottom - 4);
				addHoverPlot(plot, i);
				drawWf(plot, i);
			}
		}
		else {
			const int cols = (layout == SpecGrid4) ? 2 : 4;
			const int maxCell = (layout == SpecGrid4) ? 4 : 8;
			const int cellW = (area.Width() - gap * (cols - 1)) / cols;
			const int cellH = (area.Height() - gap) / 2;
			for (int i = 0; i < maxCell; ++i) {
				if (i >= channels) break;
				const int r = i / cols, c = i % cols;
				CRect plot(area.left + c * (cellW + gap), area.top + r * (cellH + gap),
					area.left + c * (cellW + gap) + cellW, area.top + r * (cellH + gap) + cellH);
				addHoverPlot(plot, i);
				drawWf(plot, i);
			}
		}

		m_specDC.SelectObject(oldFont);
		m_specReady = true;
		m_specDirty = false;
		return;
	}

	if (layout == SpecOverlay || channels <= 1) {
		CRect plot(area.left + 28, area.top, area.right, area.bottom - 12);
		// 重ね描き: ホバーは全chから最大dBのチャンネルを選ぶ(UpdateHover側)
		addHoverPlot(plot, -1);
		DrawSpecPanel(m_specDC, plot, 0, channels, spec, peak, channels, sr, true);
	}
	else if (layout == SpecSplitV) {
		const int n = channels;
		const int cellH = (area.Height() - gap * (n - 1)) / n;
		for (int i = 0; i < n; ++i) {
			CRect plot(area.left + 20, area.top + i * (cellH + gap),
				area.right, area.top + i * (cellH + gap) + cellH);
			addHoverPlot(plot, i);
			DrawSpecPanel(m_specDC, plot, i, 1, spec, peak, channels, sr, i == 0);
		}
	}
	else if (layout == SpecSplitH) {
		const int n = channels;
		const int cellW = (area.Width() - gap * (n - 1)) / n;
		for (int i = 0; i < n; ++i) {
			CRect plot(area.left + i * (cellW + gap), area.top,
				area.left + i * (cellW + gap) + cellW, area.bottom - 4);
			addHoverPlot(plot, i);
			DrawSpecPanel(m_specDC, plot, i, 1, spec, peak, channels, sr, i == 0);
		}
	}
	else if (layout == SpecGrid4) {
		const int cols = 2, rows = 2;
		const int cellW = (area.Width() - gap) / cols;
		const int cellH = (area.Height() - gap) / rows;
		for (int i = 0; i < 4; ++i) {
			if (i >= channels) break;
			const int r = i / cols, c = i % cols;
			CRect plot(area.left + c * (cellW + gap), area.top + r * (cellH + gap),
				area.left + c * (cellW + gap) + cellW, area.top + r * (cellH + gap) + cellH);
			addHoverPlot(plot, i);
			DrawSpecPanel(m_specDC, plot, i, 1, spec, peak, channels, sr, i == 0);
		}
	}
	else { // SpecGrid8
		const int cols = 4, rows = 2;
		const int cellW = (area.Width() - gap * (cols - 1)) / cols;
		const int cellH = (area.Height() - gap) / rows;
		for (int i = 0; i < 8; ++i) {
			if (i >= channels) break;
			const int r = i / cols, c = i % cols;
			CRect plot(area.left + c * (cellW + gap), area.top + r * (cellH + gap),
				area.left + c * (cellW + gap) + cellW, area.top + r * (cellH + gap) + cellH);
			addHoverPlot(plot, i);
			DrawSpecPanel(m_specDC, plot, i, 1, spec, peak, channels, sr, i == 0);
		}
	}

	// アクティブな m_hoverPlot は UpdateHoverFromPoint がカーソルで決める(ここで [0]=L に戻さない)

	m_specDC.SelectObject(oldFont);
	m_specReady = true;
	m_specDirty = false;
}

// メーター帯の寸法。描画とクロマ更新幅で同じ値を使う。
static void AnalyzerMeterGeom(int channels, int& n, int& meterW, int& gap, int& totalW)
{
	n = channels;
	if (n < 1) n = 1;
	if (n > CAnalyzerDlg::CH_MAX) n = CAnalyzerDlg::CH_MAX;
	// ch 数が増えるほど細く詰める(右端ストリップが波形を潰さない範囲に収める)
	meterW = (n <= 2) ? 10 : (n <= 4 ? 8 : 6);
	gap = (n <= 4) ? 3 : 2;
	totalW = n * meterW + (n - 1) * gap + 8;
}

void CAnalyzerDlg::DrawLevelMeters(CDC& dc, const CRect& waveRc, COLORREF bg)
{
	// bg: アクリル時は CHROMA 以外の不透明色で塗る(キー色だと透過して消える)
	const COLORREF panelBg = (bg == ANALYZER_CHROMA_KEY) ? RGB(22, 26, 36) : RGB(12, 14, 20);
	// アクリル用ストリップは ~39px。40 未満で return するとバーごと消える。
	if (waveRc.Width() < 28 || waveRc.Height() < 40) return;

	float hold[CH_MAX] = {}, rms[CH_MAX] = {};
	int channels = 2;
	EnterCriticalSection(&m_cs);
	for (int c = 0; c < CH_MAX; ++c) {
		hold[c] = m_meterHold[c];
		rms[c] = m_meterRms[c];
	}
	channels = m_channels;
	LeaveCriticalSection(&m_cs);

	int n = 1, meterW = 10, gap = 3, totalW = 0;
	AnalyzerMeterGeom(channels, n, meterW, gap, totalW);
	CRect area(waveRc.right - totalW - 4, waveRc.top + 8, waveRc.right - 4, waveRc.bottom - 8);
	dc.FillSolidRect(area, panelBg);

	auto ampToY = [&](float a) -> int {
		float db = AmpToDb(a);
		float t = (db + 72.0f) / 72.0f;
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		return area.bottom - (int)(t * area.Height());
	};

	// すべて RMS 基準: バー=現在RMS、白線=RMSピークホールド、黄/赤=RMS閾値
	const float ampYellow = powf(10.0f, -9.0f / 20.0f);
	const float ampRed = powf(10.0f, -3.0f / 20.0f);
	const int yYellow = ampToY(ampYellow);
	const int yRed = ampToY(ampRed);

	for (int c = 0; c < n; ++c) {
		const int x0 = area.left + 4 + c * (meterW + gap);
		CRect track(x0, area.top + 2, x0 + meterW, area.bottom - 2);
		dc.FillSolidRect(track, RGB(36, 42, 56));

		const float aRms = (std::min)(1.0f, (std::max)(0.0f, rms[c]));
		const float aHold = (std::min)(1.0f, (std::max)(0.0f, hold[c]));
		const int yFill = ampToY(aRms);
		const int yHold = ampToY(aHold);

		if (yFill < track.bottom) {
			const int ySafeTop = (std::max)(yFill, yYellow);
			if (ySafeTop < track.bottom)
				dc.FillSolidRect(CRect(track.left, ySafeTop, track.right, track.bottom), kChColor[c]);
			if (yFill < yYellow) {
				const int yY0 = (std::max)(yFill, yRed);
				if (yY0 < yYellow)
					dc.FillSolidRect(CRect(track.left, yY0, track.right, yYellow), RGB(255, 200, 80));
			}
			if (yFill < yRed)
				dc.FillSolidRect(CRect(track.left, yFill, track.right, yRed), RGB(255, 80, 80));
		}
		if (yHold >= track.top && yHold < track.bottom)
			dc.FillSolidRect(track.left, yHold, track.Width(), 2, RGB(220, 230, 245));

		dc.SetTextColor(kChColor[c]);
		dc.SetBkMode(TRANSPARENT);
		CFont* of = dc.SelectObject(&m_font);
		dc.TextOut(x0, area.top - 2, ChannelLabel(c, channels));
		dc.SelectObject(of);
	}

	// 位相相関メーター(-1..+1)と L/R バランス
	// メータ帯の左隣に固定幅で描く(波形領域が狭くても strip 更新で見える)
	if (savedata.pro_corr_meter && n >= 2) {
		const float corr = ProAudio_CorrValue();
		const float bal = ProAudio_CorrBalance();
		const int corrW = 22;
		CRect corrRc(area.left - corrW - 4, area.top + 2, area.left - 4, area.bottom - 2);
		if (corrRc.left < waveRc.left + 2)
			corrRc.OffsetRect(waveRc.left + 2 - corrRc.left, 0);
		if (corrRc.Width() >= 12 && corrRc.right <= waveRc.right) {
			dc.FillSolidRect(corrRc, RGB(28, 32, 44));
			dc.Draw3dRect(corrRc, RGB(70, 80, 100), RGB(40, 45, 60));
			const int midY = (corrRc.top + corrRc.bottom) / 2;
			dc.FillSolidRect(corrRc.left + 2, midY, corrRc.Width() - 4, 1, RGB(90, 100, 120));
			// 相関: 上=+1(同相) 下=-1(逆相)。緑バー
			int y = midY - (int)(corr * ((corrRc.Height() / 2) - 5));
			if (y < corrRc.top + 3) y = corrRc.top + 3;
			if (y > corrRc.bottom - 4) y = corrRc.bottom - 4;
			dc.FillSolidRect(corrRc.left + 3, y - 2, corrRc.Width() - 6, 5, RGB(100, 230, 150));
			// バランス: 下端の横位置
			int bx = corrRc.left + corrRc.Width() / 2 + (int)(bal * (corrRc.Width() / 2 - 3));
			if (bx < corrRc.left + 2) bx = corrRc.left + 2;
			if (bx > corrRc.right - 3) bx = corrRc.right - 3;
			dc.FillSolidRect(bx - 1, corrRc.bottom - 5, 3, 4, RGB(255, 180, 80));
			dc.SetTextColor(RGB(170, 200, 190));
			dc.SetBkMode(TRANSPARENT);
			CFont* of2 = dc.SelectObject(&m_font);
			dc.TextOut(corrRc.left + 2, corrRc.top, _T("φ"));
			dc.SelectObject(of2);
		}
	}
}

// 波形右端のレベルメーター帯幅(クロマ更新用)。DrawLevelMeters の早期 return 下限以上にすること。
static int AnalyzerMeterStripWidth(int channels)
{
	int n = 1, meterW = 10, gap = 3, totalW = 0;
	AnalyzerMeterGeom(channels, n, meterW, gap, totalW);
	int w = totalW + 16; // 余白多め(>=44)
	if (savedata.pro_corr_meter && n >= 2)
		w += 28; // φ 相関帯
	return w;
}

void CAnalyzerDlg::DrawTpLufsReadout(CDC& dc, const CRect& waveRc)
{
	if (waveRc.Width() < 120 || waveRc.Height() < 28) return;

	float tp = 0.0f, lufs = -70.0f;
	EnterCriticalSection(&m_cs);
	tp = (std::max)(m_tpHold, m_tpLin);
	lufs = m_lufsMom;
	LeaveCriticalSection(&m_cs);

	float tpDb = -99.9f;
	if (tp > 1e-6f) tpDb = 20.0f * log10f(tp);
	if (tpDb < -99.9f) tpDb = -99.9f;

	CString s;
	s.Format(_T("TP %.1f dBTP   LUFS %.1f  [%s]"), tpDb, lufs,
		LL14(L"簡易", L"approx", L"approx", L"approx", L"aprox", L"간이", L"简易", L"تقريبي",
			L"прибл.", L"ca.", L"aprox", L"ca.", L"ok.", L"yakl."));

	CFont* of = dc.SelectObject(&m_font);
	dc.SetBkMode(TRANSPARENT);
	const CSize sz = dc.GetTextExtent(s);
	// 左端は ch ラベル板、フリーズ表示があるときはさらに右へ寄せる
	const int x0 = waveRc.left + (m_frozen ? 96 : 34);
	CRect box(x0, waveRc.top + 2, x0 + sz.cx + 10, waveRc.top + 6 + sz.cy);
	if (box.right > waveRc.right - 4)
		box.right = waveRc.right - 4;
	if (box.Width() > 20) {
		// アクリルでも読めるよう不透明プレート(CHROMA キーでは塗らない)
		dc.FillSolidRect(box, ANALYZER_LABEL_PLATE);
		dc.SetTextColor(tpDb > -0.1f ? RGB(255, 150, 130) : RGB(200, 215, 235));
		dc.TextOut(box.left + 5, box.top + 1, s);
	}
	else {
		box.SetRectEmpty();
	}
	dc.SelectObject(of);

	// ツールチップ矩形を実描画位置へ合わせる(簡易計測である旨を出す)。
	// 毎フレーム送らないよう、変化したときだけ更新する。
	if (box != m_tpLufsTipRc) {
		m_tpLufsTipRc = box;
		if (m_tooltip.GetSafeHwnd() && !m_tpLufsTipRc.IsRectEmpty())
			m_tooltip.SetToolRect(this, 1, &m_tpLufsTipRc);
	}
}

void CAnalyzerDlg::DrawHoverReadout(CDC& dc, const CRect& clientRc)
{
	UNREFERENCED_PARAMETER(clientRc);
	if (!m_hoverValid || m_hoverPlot.IsRectEmpty()) return;

	CRect plot = m_hoverPlot;
	plot.OffsetRect(0, m_hoverSplitY);
	if (plot.Width() < 8 || plot.Height() < 8) return;

	int sr = m_sampleRate > 0 ? m_sampleRate : 44100;
	const float nyquist = (float)sr * 0.5f;
	float fMin = 20.0f, fMax = (std::max)(20.2f, nyquist);
	GetZoomFreqRange(m_freqZoom, nyquist, fMin, fMax);
	float tx = logf((std::max)(m_hoverHz, fMin) / fMin) / logf(fMax / fMin);
	if (tx < 0.0f) tx = 0.0f;
	if (tx > 1.0f) tx = 1.0f;
	// 差分表示中は縦軸が ±48dB(0dB=中央)。読取点も同じ軸に載せる。
	float ty;
	if (m_specDiff && m_specSnapValid && m_hoverBin >= 0 && m_hoverCh >= 0 && m_hoverCh < CH_MAX)
		ty = ((m_hoverDb - m_specSnapDb[m_hoverCh][m_hoverBin]) + 48.0f) / 96.0f;
	else
		ty = (m_hoverDb + 96.0f) / 96.0f;
	if (ty < 0.0f) ty = 0.0f;
	if (ty > 1.0f) ty = 1.0f;
	const int cx = plot.left + (int)(tx * plot.Width());
	const int cy = plot.bottom - (int)(ty * plot.Height());

	CPen cross(PS_DOT, 1, RGB(180, 190, 210));
	CPen* old = dc.SelectObject(&cross);
	dc.MoveTo(cx, plot.top);
	dc.LineTo(cx, plot.bottom);
	dc.MoveTo(plot.left, cy);
	dc.LineTo(plot.right, cy);
	dc.SelectObject(old);
	dc.FillSolidRect(cx - 2, cy - 2, 5, 5, RGB(255, 230, 120));

	CString s;
	const int channels = (std::max)(1, (std::min)(m_channels, CH_MAX));
	if (m_hoverHz >= 1000.0f)
		s.Format(_T("%s  %.2f kHz  %.1f dB"), ChannelLabel(m_hoverCh, channels), m_hoverHz / 1000.0f, m_hoverDb);
	else
		s.Format(_T("%s  %.0f Hz  %.1f dB"), ChannelLabel(m_hoverCh, channels), m_hoverHz, m_hoverDb);
	if (m_frozen)
		s += _T("  [FREEZE]");

	CSize sz = dc.GetTextExtent(s);
	CRect box(plot.left + 8, plot.top + 8, plot.left + 16 + sz.cx, plot.top + 16 + sz.cy);
	if (box.right > plot.right - 4)
		box.OffsetRect(plot.right - 4 - box.right, 0);
	dc.FillSolidRect(box, RGB(20, 24, 36));
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(RGB(230, 235, 245));
	CFont* of = dc.SelectObject(&m_font);
	dc.TextOut(box.left + 4, box.top + 2, s);
	dc.SelectObject(of);
}

bool CAnalyzerDlg::UpdateHoverFromPoint(CPoint ptClient)
{
	const bool wasValid = m_hoverValid;
	const int oldBin = m_hoverBin;
	const float oldDb = m_hoverDb;
	const int oldCh = m_hoverCh;

	m_hoverValid = false;
	m_hoverBin = -1;
	if (m_hoverPlotCount <= 0 || m_hoverSplitY <= 0)
		return wasValid;
	// 位相スコープは周波数軸ではないので読取を出さない
	if (m_lowerMode == LowerPhase)
		return wasValid;

	// カーソル下のパネルを探す(分割時は各ch、重ね時は1枚で ch=-1)
	int hit = -1;
	CRect hitPlot;
	for (int i = 0; i < m_hoverPlotCount; ++i) {
		CRect plot = m_hoverPlots[i];
		plot.OffsetRect(0, m_hoverSplitY);
		if (plot.PtInRect(ptClient) && plot.Width() >= 8 && plot.Height() >= 8) {
			hit = i;
			hitPlot = plot;
			break;
		}
	}
	if (hit < 0)
		return wasValid;

	int sr = 44100, channels = 2;
	float localBins[CH_MAX][SPEC_BINS];
	EnterCriticalSection(&m_cs);
	sr = m_sampleRate > 0 ? m_sampleRate : 44100;
	channels = (std::max)(1, (std::min)(m_channels, CH_MAX));
	memcpy(localBins, m_specDb, sizeof(localBins));
	LeaveCriticalSection(&m_cs);

	const float nyquist = (float)sr * 0.5f;
	float fMin = 20.0f, fMax = (std::max)(20.2f, nyquist);
	GetZoomFreqRange(m_freqZoom, nyquist, fMin, fMax);
	const float hz = PlotXToHz(ptClient.x, hitPlot, fMin, fMax);

	int bestB = -1;
	float bestDist = 1e9f;
	for (int b = 0; b < SPEC_BINS; ++b) {
		const float cHz = SpecCenterHz(b, SPEC_BINS, nyquist);
		if (m_freqZoom != ZoomFull && (cHz < fMin || cHz > fMax)) continue;
		const float d = fabsf(logf((std::max)(cHz, 1.0f) / (std::max)(hz, 1.0f)));
		if (bestB < 0 || d < bestDist) { bestDist = d; bestB = b; }
	}
	if (bestB < 0) bestB = 0;

	int ch = m_hoverPlotCh[hit];
	if (ch < 0) {
		// 重ね描き: そのビンで最も大きい ch を表示
		ch = 0;
		float bestDb = localBins[0][bestB];
		for (int c = 1; c < channels; ++c) {
			if (localBins[c][bestB] > bestDb) {
				bestDb = localBins[c][bestB];
				ch = c;
			}
		}
	}
	if (ch < 0) ch = 0;
	if (ch >= channels) ch = channels - 1;

	m_hoverHz = SpecCenterHz(bestB, SPEC_BINS, nyquist);
	m_hoverDb = localBins[ch][bestB];
	m_hoverCh = ch;
	m_hoverBin = bestB;
	m_hoverPlot = m_hoverPlots[hit];
	m_hoverValid = true;

	return !wasValid || oldBin != m_hoverBin || oldCh != m_hoverCh
		|| fabsf(oldDb - m_hoverDb) >= 0.5f;
}

bool CAnalyzerDlg::HitEqBand(CPoint ptClient, int& outBand, CRect& outPlot)
{
	outBand = -1;
	outPlot.SetRectEmpty();
	// EQ カーブが出ている表示(周波数特性/スペクトログラム)でのみ掴める
	if (!m_eqOverlay || m_lowerMode == LowerPhase) return false;
	if (m_hoverPlotCount <= 0 || m_hoverSplitY <= 0) return false;

	int sr = 44100;
	EnterCriticalSection(&m_cs);
	sr = m_sampleRate > 0 ? m_sampleRate : 44100;
	LeaveCriticalSection(&m_cs);
	const float nyquist = (float)sr * 0.5f;
	float fMin = 20.0f, fMax = (std::max)(20.2f, nyquist);
	GetZoomFreqRange(m_freqZoom, nyquist, fMin, fMax);

	for (int i = 0; i < m_hoverPlotCount; ++i) {
		CRect plot = m_hoverPlots[i];
		plot.OffsetRect(0, m_hoverSplitY);
		if (plot.Width() < 8 || plot.Height() < 8) continue;
		if (!plot.PtInRect(ptClient)) continue;

		int best = -1, bestD = 0;
		for (int b = 0; b < EQ_OVERLAY_BANDS; ++b) {
			const float f = kEqFreqs[b];
			if (f >= nyquist || f < fMin * 0.999f || f > fMax * 1.001f) continue;
			int eqv = savedata.eq[b];
			if (eqv < 0) eqv = 0;
			if (eqv > 200) eqv = 200;
			float t = (EqSliderToDb(eqv) + 12.0f) / 24.0f;
			if (t < 0.0f) t = 0.0f;
			if (t > 1.0f) t = 1.0f;
			const int x = (int)HzToPlotX(f, plot, fMin, fMax);
			const int y = plot.bottom - (int)(t * plot.Height());
			const int dx = abs(x - ptClient.x);
			const int dy = abs(y - ptClient.y);
			if (dx > 10 || dy > 10) continue;
			const int d = dx * dx + dy * dy;
			if (best < 0 || d < bestD) { best = b; bestD = d; }
		}
		if (best < 0) return false;
		outBand = best;
		outPlot = plot;
		return true;
	}
	return false;
}

void CAnalyzerDlg::ApplyEqBandFromY(int band, const CRect& plot, int yClient)
{
	if (band < 0 || band >= EQ_OVERLAY_BANDS || plot.Height() < 8) return;
	float t = (float)(plot.bottom - yClient) / (float)plot.Height();
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;
	const int slider = EqDbToSlider(t * 24.0f - 12.0f);
	if (savedata.eq[band] == slider) return;
	savedata.eq[band] = slider;
	SyncEqUiFromSavedata();
	m_specDirty = true;
	KickUiPresent();
}

void CAnalyzerDlg::Present(CDC& dc, const CRect& rc, BOOL bAero)
{
	UNREFERENCED_PARAMETER(bAero);
	const int split = rc.top + (int)(rc.Height() * 0.65);
	m_hoverSplitY = split;
	const int waveH = split - rc.top;
	const int specH = rc.bottom - split;
	const int clientW = rc.Width();
	const int clientH = rc.Height();

	CDC* pDst = &dc;
	if (EnsureFrameBuffer(dc, clientW, clientH) && m_frameDC.GetSafeHdc())
		pDst = &m_frameDC;

	if (m_waveReady && m_waveDC.GetSafeHdc())
		pDst->BitBlt(0, 0, m_waveW, waveH, &m_waveDC, 0, 0, SRCCOPY);
	else
		pDst->FillSolidRect(0, 0, clientW, waveH, ANALYZER_BG);

	DrawTpLufsReadout(*pDst, CRect(0, 0, m_waveW > 0 ? m_waveW : clientW, waveH));

	if (m_showLevelMeter)
		DrawLevelMeters(*pDst, CRect(0, 0, m_waveW > 0 ? m_waveW : clientW, waveH), ANALYZER_BG);

	if (m_specReady && m_specDC.GetSafeHdc())
		pDst->BitBlt(0, split, m_specW, specH, &m_specDC, 0, 0, SRCCOPY);
	else
		pDst->FillSolidRect(0, split, clientW, specH, ANALYZER_BG);

	pDst->FillSolidRect(0, split - 1, clientW, 2, RGB(60, 65, 80));
	if (m_hoverValid)
		DrawHoverReadout(*pDst, rc);
	if (m_frozen) {
		pDst->SetBkMode(TRANSPARENT);
		pDst->SetTextColor(RGB(255, 180, 80));
		CFont* of = pDst->SelectObject(&m_font);
		pDst->TextOut(8, 4, LL14(L"フリーズ中", L"Frozen", L"Gele", L"Congelato", L"Congelado", L"정지됨", L"已冻结", L"مجمد", L"Заморожено", L"Eingefroren", L"Congelado", L"Bevroren", L"Zamrozone", L"Donduruldu"));
		pDst->SelectObject(of);
	}

	if (pDst != &dc)
		dc.BitBlt(0, 0, clientW, clientH, pDst, 0, 0, SRCCOPY);
}

void CAnalyzerDlg::OnPaint()
{
	CPaintDC dc(this);
	CRect rc;
	GetClientRect(&rc);
	if (rc.IsRectEmpty()) {
		InterlockedExchange(&m_presentPosted, 0);
		return;
	}

	const int split = rc.top + (int)(rc.Height() * 0.65);
	const int waveW = rc.Width();
	const int waveH = split - rc.top;
	const int specW = rc.Width();
	const int specH = rc.bottom - split;
	const int clientW = rc.Width();
	const int clientH = rc.Height();

#if CCUSTOM_AERO_SUPPORT
	const BOOL bAero = (savedata.aero == 1 && CCC_IsAeroEnabled() && CCC_IsWin11());
	const COLORREF bg = bAero ? ANALYZER_CHROMA_KEY : ANALYZER_BG;
#else
	const BOOL bAero = FALSE;
	const COLORREF bg = ANALYZER_BG;
#endif

	if (!EnsureWaveBuffer(dc, waveW, waveH) || !EnsureSpecBuffer(dc, specW, specH)) {
		dc.FillSolidRect(rc, ANALYZER_BG);
		InterlockedExchange(&m_presentPosted, 0);
		return;
	}

	int pending = 0;
	EnterCriticalSection(&m_cs);
	pending = m_pendingScroll;
	LeaveCriticalSection(&m_cs);

	// 1フレームで大きく飛ぶとカクつくが、追いつき不足も精度を落とす。
	const int scrollCap = (std::max)(8, (std::min)(48, m_waveW > 0 ? m_waveW / 20 : 24));

	bool didWaveFull = false;
	bool didWaveScroll = false;
	bool needDeferredKick = false;
#if CCUSTOM_AERO_SUPPORT
	m_lastWaveScroll = 0;
#endif

	// トリガー表示は毎回全描画(スクロール差分を使わない)
	if (!m_waveReady || m_waveMode == WaveTrigger) {
		InterlockedExchange(&m_fullRedrawBusy, 1);
		FullRedrawWave(bg);
		didWaveFull = true;
		InterlockedExchange(&m_fullRedrawBusy, 0);
		if (InterlockedExchange(&m_presentDeferred, 0) != 0)
			needDeferredKick = true;
	}
	else if (pending > 0) {
		const int scrolled = ScrollWaveAndDrawNew(bg, scrollCap);
		if (scrolled < 0) {
			didWaveFull = true;
		}
		else if (scrolled > 0) {
			didWaveScroll = true;
#if CCUSTOM_AERO_SUPPORT
			m_lastWaveScroll = scrolled;
#endif
		}
	}

	// FFT キックは FeedPCM 側のみ。ここで再要求するとフォーカス時に描画ループ化する。
	const bool didSpec = (!m_specReady || m_specDirty);
	if (didSpec)
		RedrawSpectrum(bg);

	m_hoverSplitY = split;
	// マウス停止中も解析描画でパネル表が更新されるため、カーソル位置からホバーを再同期
	if (m_trackingMouse) {
		CPoint pt;
		GetCursorPos(&pt);
		ScreenToClient(&pt);
		UpdateHoverFromPoint(pt);
	}
	m_hoverChanged = false;

	// 未消化スクロールの追い付き Kick は提示後に行う（presentPosted 解放後）
	EnterCriticalSection(&m_cs);
	const bool moreScroll = (m_pendingScroll > 0);
	LeaveCriticalSection(&m_cs);

#if CCUSTOM_AERO_SUPPORT
	if (bAero) {
		if (m_chromaW != clientW || m_chromaH != clientH) {
			m_chromaCache.Release();
			m_chromaReady = false;
			m_chromaW = clientW;
			m_chromaH = clientH;
		}
		if (m_chromaCache.Ensure(dc.GetSafeHdc(), clientW, clientH)) {
			const COLORREF key = ANALYZER_CHROMA_KEY;
			if (m_waveReady && m_waveDC.GetSafeHdc()) {
				const int stripW = m_showLevelMeter
					? (std::min)(waveW, AnalyzerMeterStripWidth(m_channels))
					: 0;
				const int stripX = (std::max)(0, waveW - stripW);

				if (didWaveScroll && m_lastWaveScroll > 0 && m_chromaReady) {
					// 前回メーター帯を波形で戻してからスクロール(バーが波形に流れ込まない)
					if (stripW > 0)
						m_chromaCache.UpdateRect(m_waveDC.GetSafeHdc(), stripX, 0, stripX, 0, stripW, waveH, key);
					m_chromaCache.ScrollCols(0, 0, waveW, waveH, m_lastWaveScroll);
					const int keep = waveW - m_lastWaveScroll;
					m_chromaCache.UpdateRect(m_waveDC.GetSafeHdc(), keep, 0,
						keep, 0, m_lastWaveScroll, waveH, key);
					m_chromaCache.UpdateRect(m_waveDC.GetSafeHdc(), 0, 0, 0, 0,
						(std::min)(32, waveW), waveH, key);
					// TP/LUFS プレートが横に流れないよう、その行だけ波形BBから戻す
					if (!m_tpLufsTipRc.IsRectEmpty()) {
						const int rw = (std::min)((long)waveW, m_tpLufsTipRc.right + 8);
						const int rh = (std::min)((long)waveH, m_tpLufsTipRc.bottom + 4);
						if (rw > 0 && rh > 0)
							m_chromaCache.UpdateRect(m_waveDC.GetSafeHdc(), 0, 0, 0, 0, rw, rh, key);
					}
				}
				else if (didWaveFull || !m_chromaReady) {
					m_chromaCache.UpdateRect(m_waveDC.GetSafeHdc(), 0, 0, 0, 0, waveW, waveH, key);
				}

				// メーターは独立ストリップ → クロマ右端へ(波形BBには焼かない)
				if (stripW > 0 && m_waveScratchDC.GetSafeHdc()) {
					m_waveScratchDC.FillSolidRect(0, 0, stripW, waveH, key);
					DrawLevelMeters(m_waveScratchDC, CRect(0, 0, stripW, waveH), key);
					m_chromaCache.UpdateRect(m_waveScratchDC.GetSafeHdc(),
						0, 0, stripX, 0, stripW, waveH, key);
				}

				// TP/LUFS も独立プレート(不透明)としてクロマへ焼く
				if (m_waveScratchDC.GetSafeHdc()) {
					const int bandH = (std::min)(waveH, 28);
					m_waveScratchDC.FillSolidRect(0, 0, waveW, bandH, key);
					DrawTpLufsReadout(m_waveScratchDC, CRect(0, 0, waveW, waveH));
					if (!m_tpLufsTipRc.IsRectEmpty() && m_tpLufsTipRc.bottom <= bandH) {
						m_chromaCache.UpdateRect(m_waveScratchDC.GetSafeHdc(),
							m_tpLufsTipRc.left, m_tpLufsTipRc.top,
							m_tpLufsTipRc.left, m_tpLufsTipRc.top,
							m_tpLufsTipRc.Width(), m_tpLufsTipRc.Height(), key);
					}
				}
			}
			if (m_specReady && m_specDC.GetSafeHdc() && (didSpec || !m_chromaReady)) {
				m_chromaCache.UpdateRect(m_specDC.GetSafeHdc(), 0, 0, 0, split, specW, specH, key);
			}
			m_chromaCache.FillOpaqueRect(0, split - 1, clientW, 2, RGB(60, 65, 80), key);

			if (m_hoverValid) {
				CRect plot = m_hoverPlot;
				plot.OffsetRect(0, split);
				CString s;
				const int channels = (std::max)(1, (std::min)(m_channels, CH_MAX));
				if (m_hoverHz >= 1000.0f)
					s.Format(_T("%s  %.2f kHz  %.1f dB"), ChannelLabel(m_hoverCh, channels), m_hoverHz / 1000.0f, m_hoverDb);
				else
					s.Format(_T("%s  %.0f Hz  %.1f dB"), ChannelLabel(m_hoverCh, channels), m_hoverHz, m_hoverDb);
				CDC* pTmp = &m_waveScratchDC;
				if (pTmp->GetSafeHdc()) {
					CFont* of = pTmp->SelectObject(&m_font);
					CSize sz = pTmp->GetTextExtent(s);
					pTmp->SelectObject(of);
					CRect box(plot.left + 8, plot.top + 8, plot.left + 16 + sz.cx, plot.top + 16 + sz.cy);
					if (box.right > plot.right - 4)
						box.OffsetRect(plot.right - 4 - box.right, 0);
					const int bw = box.Width(), bh = box.Height();
					if (bw > 0 && bh > 0 && bw < waveW && bh < waveH) {
						pTmp->FillSolidRect(0, 0, bw, bh, RGB(20, 24, 36));
						pTmp->SetBkMode(TRANSPARENT);
						pTmp->SetTextColor(RGB(230, 235, 245));
						of = pTmp->SelectObject(&m_font);
						pTmp->TextOut(4, 2, s);
						pTmp->SelectObject(of);
						m_chromaCache.UpdateRect(pTmp->GetSafeHdc(), 0, 0, box.left, box.top, bw, bh, key);
					}
				}
			}
			if (m_frozen) {
				CString fr = LL14(L"フリーズ中", L"Frozen", L"Gele", L"Congelato", L"Congelado", L"정지됨", L"已冻结", L"مجمد", L"Заморожено", L"Eingefroren", L"Congelado", L"Bevroren", L"Zamrozone", L"Donduruldu");
				if (m_waveScratchDC.GetSafeHdc()) {
					CFont* of = m_waveScratchDC.SelectObject(&m_font);
					CSize sz = m_waveScratchDC.GetTextExtent(fr);
					const int bw = sz.cx + 12, bh = sz.cy + 8;
					m_waveScratchDC.FillSolidRect(0, 0, bw, bh, RGB(40, 32, 16));
					m_waveScratchDC.SetBkMode(TRANSPARENT);
					m_waveScratchDC.SetTextColor(RGB(255, 180, 80));
					m_waveScratchDC.TextOut(6, 4, fr);
					m_waveScratchDC.SelectObject(of);
					m_chromaCache.UpdateRect(m_waveScratchDC.GetSafeHdc(), 0, 0, 4, 2, bw, bh, key);
				}
			}

			// 「メインに追従」をクロマバッファへ焼いてから1回だけ BlitFull。
			// 画面 DC への content → overlay の2段合成はアクリルでちらつく。
			// ScrollCols で前フレームのロック表示が左へ流れるため、現在の矩形だけでなく
			// ヘッダー行全幅を下地へ戻してから焼き直す。
			{
				CRect lockRc;
				CCC_MainLockGetOverlayRect(m_hWnd, lockRc);
				if (!lockRc.IsRectEmpty() && m_chromaCache.hdcDib) {
					CRect headerRow(0, lockRc.top, clientW, lockRc.bottom);
					if (headerRow.top < 0)
						headerRow.top = 0;
					if (headerRow.bottom > clientH)
						headerRow.bottom = clientH;

					CRect wavePart = headerRow;
					if (wavePart.bottom > split)
						wavePart.bottom = split;
					if (wavePart.top < wavePart.bottom && m_waveReady && m_waveDC.GetSafeHdc()) {
						m_chromaCache.UpdateRect(m_waveDC.GetSafeHdc(),
							wavePart.left, wavePart.top, wavePart.left, wavePart.top,
							wavePart.Width(), wavePart.Height(), key);
					}
					CRect specPart = headerRow;
					if (specPart.top < split)
						specPart.top = split;
					if (specPart.top < specPart.bottom && m_specReady && m_specDC.GetSafeHdc()) {
						m_chromaCache.UpdateRect(m_specDC.GetSafeHdc(),
							specPart.left, specPart.top - split, specPart.left, specPart.top,
							specPart.Width(), specPart.Height(), key);
					}
					CDC dcCache;
					dcCache.Attach(m_chromaCache.hdcDib);
					CCC_MainLockPaintClient(dcCache, m_hWnd);
					dcCache.Detach();
					m_chromaCache.MakeRectOpaque(lockRc.left, lockRc.top, lockRc.Width(), lockRc.Height());
				}
			}

			m_chromaReady = true;
			m_chromaCache.BlitFull(dc.GetSafeHdc(), 0, 0, clientW, clientH);
		}
		else {
			Present(dc, rc, FALSE);
			CCC_MainLockPaintClient(dc, m_hWnd);
		}
	}
	else
#endif
	{
		Present(dc, rc, FALSE);
		CCC_MainLockPaintClient(dc, m_hWnd);
	}

	// 描画完了後に提示フラグを開放し、未消化スクロール/遅延 Kick があれば1回だけ
	InterlockedExchange(&m_presentPosted, 0);
	if ((moreScroll && !didWaveFull) || needDeferredKick)
		KickUiPresent();
}

BOOL CAnalyzerDlg::OnEraseBkgnd(CDC* pDC)
{
	UNREFERENCED_PARAMETER(pDC);
	return TRUE;
}

void CAnalyzerDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == 1) {
		// 描画は PostMessage 自由走行。タイマーは座標保存のみ。
		CRect rc;
		GetWindowRect(&rc);
		if (!IsIconic()) {
			savedata.analyzerx = rc.left;
			savedata.analyzery = rc.top;
			savedata.analyzerw = rc.Width();
			savedata.analyzerh = rc.Height();
		}
	}
	CCustomBlurDialogExBase::OnTimer(nIDEvent);
}

void CAnalyzerDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	ReleaseBuffers();
#if CCUSTOM_AERO_SUPPORT
	// Finalize の再実行はしない。DWM 属性の軽い再適用のみ。
	if (nType != SIZE_MINIMIZED && CCC_IsAeroEnabled())
		CCC_RefreshDwmBlur(m_hWnd);
#endif
	if (::IsWindow(m_hWnd)) {
		CRect cr;
		GetClientRect(&cr);
		if (!cr.IsRectEmpty())
			CCC_InvalidateRectMinusOverlay(m_hWnd, cr);
	}
}

void CAnalyzerDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CCustomBlurDialogExBase::OnShowWindow(bShow, nStatus);
#if CCUSTOM_AERO_SUPPORT
	// 基底側で Apply/Refresh 済み。ここでは内容再描画のみ。
	if (bShow && CCC_IsAeroEnabled())
		Invalidate(FALSE);
#endif
}

void CAnalyzerDlg::OnClose()
{
	savedata.analyzerwindow = 0;
	DetachForDestroy();
	DestroyWindow();
}

void CAnalyzerDlg::OnMouseMove(UINT nFlags, CPoint point)
{
	UNREFERENCED_PARAMETER(nFlags);
	if (m_eqDrag) {
		if (!(nFlags & MK_LBUTTON)) {
			// キャプチャを取り逃した場合の保険
			m_eqDrag = false;
			m_eqDragBand = -1;
			if (::GetCapture() == m_hWnd) ::ReleaseCapture();
		}
		else {
			ApplyEqBandFromY(m_eqDragBand, m_eqDragPlot, point.y);
			if (m_eqDragBand >= 0 && m_eqDragBand < EQ_OVERLAY_BANDS) {
				// ドラッグ中は読取ボックスに帯域の Hz とゲインを出す
				m_hoverHz = kEqFreqs[m_eqDragBand];
				m_hoverDb = EqSliderToDb((std::min)(200, (std::max)(0, savedata.eq[m_eqDragBand])));
				m_hoverBin = -1;
				m_hoverPlot = m_eqDragPlot;
				m_hoverPlot.OffsetRect(0, -m_hoverSplitY);
				m_hoverValid = true;
				m_hoverChanged = true;
			}
			KickUiPresent();
			return;
		}
	}
	if (!m_trackingMouse) {
		TRACKMOUSEEVENT tme = { sizeof(tme) };
		tme.dwFlags = TME_LEAVE;
		tme.hwndTrack = m_hWnd;
		TrackMouseEvent(&tme);
		m_trackingMouse = true;
	}
	CRect rc; GetClientRect(&rc);
	m_hoverSplitY = rc.top + (int)(rc.Height() * 0.65);
	// フォーカス時のマウス洪水で全再描画しない。変化時のみ提示要求を合流。
	if (UpdateHoverFromPoint(point)) {
		m_hoverChanged = true;
		KickUiPresent();
	}
	CCustomBlurDialogExBase::OnMouseMove(nFlags, point);
}

void CAnalyzerDlg::OnMouseLeave()
{
	m_trackingMouse = false;
	if (m_hoverValid) {
		m_hoverValid = false;
		m_hoverBin = -1;
		m_hoverChanged = true;
		KickUiPresent();
	}
	CCustomBlurDialogExBase::OnMouseLeave();
}

void CAnalyzerDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
	int band = -1;
	CRect plot;
	if (HitEqBand(point, band, plot)) {
		m_eqDrag = true;
		m_eqDragBand = band;
		m_eqDragPlot = plot;
		SetCapture();
		ApplyEqBandFromY(band, plot, point.y);
		return; // 基底のウィンドウドラッグへ渡さない
	}
	CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
}

void CAnalyzerDlg::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_eqDrag) {
		m_eqDrag = false;
		m_eqDragBand = -1;
		m_eqDragPlot.SetRectEmpty();
		if (::GetCapture() == m_hWnd) ::ReleaseCapture();
		m_specDirty = true;
		KickUiPresent();
		return;
	}
	CCustomBlurDialogExBase::OnLButtonUp(nFlags, point);
}

void CAnalyzerDlg::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	UNREFERENCED_PARAMETER(nFlags);
	UNREFERENCED_PARAMETER(point);
	ResetPeakHold();
	CCustomBlurDialogExBase::OnLButtonDblClk(nFlags, point);
}

BOOL CAnalyzerDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	if (pMsg->message == WM_KEYDOWN) {
		if (pMsg->wParam == VK_ESCAPE) {
			PostMessage(WM_CLOSE);
			return TRUE;
		}
		if (pMsg->wParam == 'F' || pMsg->wParam == 'f') {
			OnToggleFreeze();
			return TRUE;
		}
		if (pMsg->wParam == 'P' || pMsg->wParam == 'p') {
			OnTogglePeakHold();
			return TRUE;
		}
		if (pMsg->wParam == 'E' || pMsg->wParam == 'e') {
			OnToggleEqOverlay();
			return TRUE;
		}
		if (pMsg->wParam == 'M' || pMsg->wParam == 'm') {
			OnToggleLevelMeter();
			return TRUE;
		}
		if (pMsg->wParam == 'T' || pMsg->wParam == 't') {
			OnToggleAlwaysOnTop();
			return TRUE;
		}
		if (pMsg->wParam == 'W' || pMsg->wParam == 'w') {
			SetWaveMode(m_waveMode == WaveScroll ? WaveTrigger : WaveScroll);
			return TRUE;
		}
		if (pMsg->wParam == 'S' || pMsg->wParam == 's') {
			SetLowerMode((m_lowerMode + 1) % 3);
			return TRUE;
		}
		if (pMsg->wParam == 'D' || pMsg->wParam == 'd') {
			OnToggleSpecDiff();
			return TRUE;
		}
		if (pMsg->wParam == 'Z' || pMsg->wParam == 'z') {
			SetFreqZoom((m_freqZoom + 1) % 4);
			return TRUE;
		}
		if (pMsg->wParam == 'C' || pMsg->wParam == 'c') {
			if (::GetKeyState(VK_CONTROL) & 0x8000) {
				if (m_hoverValid)
					OnCopyHoverReadout();
				else
					OnCopyPeakFreq();
				return TRUE;
			}
			OnClearDisplay();
			return TRUE;
		}
		if (pMsg->wParam == VK_SPACE) {
			ResetPeakHold();
			return TRUE;
		}
	}
	return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}
