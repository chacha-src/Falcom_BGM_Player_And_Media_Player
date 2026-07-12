// CAnalyzerDlg.cpp : 簡易波形アナライザー(スクロールBB・多ch・周波数特性)
#include "stdafx.h"
#include "ogg.h"
#include "CAnalyzerDlg.h"
#include <cmath>
#include <algorithm>

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

	enum {
		IDM_SPEC_OVERLAY = 42001,
		IDM_SPEC_SPLIT_V = 42002,
		IDM_SPEC_SPLIT_H = 42003,
		IDM_SPEC_GRID4 = 42004,
		IDM_SPEC_GRID8 = 42005,
		IDM_STYLE_FILL = 42010,
		IDM_STYLE_LINE = 42011,
		IDM_STYLE_BARS = 42012,
		IDM_PEAK_HOLD = 42020,
		IDM_EQ_OVERLAY = 42021,
		IDM_FREEZE = 42022,
		IDM_RESET_PEAK = 42023,
		WM_ANALYZER_SPEC_DONE = WM_APP + 510,
		WM_ANALYZER_PRESENT = WM_APP + 511
	};

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
}

CAnalyzerDlg::CAnalyzerDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(IDD_ANALYZER, pParent)
{
	InitializeCriticalSection(&m_cs);
	for (int c = 0; c < CH_MAX; ++c) {
		m_ring[c].assign(RING_SAMPLES, 0.0f);
		m_ringSnap[c].assign(RING_SAMPLES, 0.0f);
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
		}
	}
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
	ON_WM_LBUTTONDBLCLK()
	ON_COMMAND(IDM_SPEC_OVERLAY, &CAnalyzerDlg::OnSpecLayoutOverlay)
	ON_COMMAND(IDM_SPEC_SPLIT_V, &CAnalyzerDlg::OnSpecLayoutSplitV)
	ON_COMMAND(IDM_SPEC_SPLIT_H, &CAnalyzerDlg::OnSpecLayoutSplitH)
	ON_COMMAND(IDM_SPEC_GRID4, &CAnalyzerDlg::OnSpecLayoutGrid4)
	ON_COMMAND(IDM_SPEC_GRID8, &CAnalyzerDlg::OnSpecLayoutGrid8)
	ON_COMMAND(IDM_STYLE_FILL, &CAnalyzerDlg::OnSpecStyleFill)
	ON_COMMAND(IDM_STYLE_LINE, &CAnalyzerDlg::OnSpecStyleLine)
	ON_COMMAND(IDM_STYLE_BARS, &CAnalyzerDlg::OnSpecStyleBars)
	ON_COMMAND(IDM_PEAK_HOLD, &CAnalyzerDlg::OnTogglePeakHold)
	ON_COMMAND(IDM_EQ_OVERLAY, &CAnalyzerDlg::OnToggleEqOverlay)
	ON_COMMAND(IDM_FREEZE, &CAnalyzerDlg::OnToggleFreeze)
	ON_COMMAND(IDM_RESET_PEAK, &CAnalyzerDlg::OnResetPeakHold)
	ON_MESSAGE(WM_ANALYZER_SPEC_DONE, &CAnalyzerDlg::OnSpecAnalysisDone)
	ON_MESSAGE(WM_ANALYZER_PRESENT, &CAnalyzerDlg::OnPresentRequest)
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
#if CCUSTOM_AERO_SUPPORT
	if (!CCC_IsAeroEnabled())
#endif
		ModifyStyleEx(0, WS_EX_DLGMODALFRAME);

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
	if (m_specStyle < StyleFill || m_specStyle > StyleBars)
		m_specStyle = StyleFill;
	m_peakHold = (savedata.analyzerpeakhold != 0);
	m_eqOverlay = (savedata.analyzereqoverlay != 0);
	m_frozen = false;
	m_hoverPlot.SetRectEmpty();
	m_hoverPlotCount = 0;

	m_feedEnabled = true;
	StartSpecWorker();
	// タイマーは座標保存のみ。描画は解析/音声完了の PostMessage で自由走行(ピアノロール方式)。
	SetTimer(1, 500, nullptr);
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
	m_meterPeak[0] = m_meterPeak[1] = 0.0f;
	m_meterHold[0] = m_meterHold[1] = 0.0f;
	m_meterRms[0] = m_meterRms[1] = 0.0f;
	m_waveDispPeak = 0.25f;
	for (int c = 0; c < CH_MAX; ++c) {
		for (int b = 0; b < SPEC_BINS; ++b) {
			m_specDb[c][b] = -96.0f;
			m_specPeakDb[c][b] = -96.0f;
		}
	}
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
	if (::IsWindow(m_hWnd)) {
		MSG msg;
		while (PeekMessage(&msg, m_hWnd, WM_ANALYZER_SPEC_DONE, WM_ANALYZER_SPEC_DONE, PM_REMOVE)) {}
		while (PeekMessage(&msg, m_hWnd, WM_ANALYZER_PRESENT, WM_ANALYZER_PRESENT, PM_REMOVE)) {}
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
		if (::IsWindow(m_hWnd))
			PostMessage(WM_ANALYZER_SPEC_DONE, 0, 0);
	}
	return 0;
}

LRESULT CAnalyzerDlg::OnSpecAnalysisDone(WPARAM, LPARAM)
{
	// ピアノロール同様: 解析ができたフレームからすぐ描く(タイマー待ちにしない)
	KickUiPresent();
	return 0;
}

LRESULT CAnalyzerDlg::OnPresentRequest(WPARAM, LPARAM)
{
	InterlockedExchange(&m_presentPosted, 0);
	if (::IsWindow(m_hWnd) && IsWindowVisible() && !IsIconic())
		Invalidate(FALSE);
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
	if (style < StyleFill || style > StyleBars) return;
	if (m_specStyle == style) return;
	m_specStyle = style;
	savedata.analyzerspecstyle = style;
	m_specDirty = true;
	Invalidate(FALSE);
}

void CAnalyzerDlg::ResetPeakHold()
{
	EnterCriticalSection(&m_cs);
	for (int c = 0; c < CH_MAX; ++c) {
		for (int b = 0; b < SPEC_BINS; ++b)
			m_specPeakDb[c][b] = m_specDb[c][b];
		if (c < 2)
			m_meterHold[c] = m_meterRms[c];
	}
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

void CAnalyzerDlg::OnContextMenu(CWnd* /*pWnd*/, CPoint point)
{
	CMenu menu;
	menu.CreatePopupMenu();

	CMenu subLayout;
	subLayout.CreatePopupMenu();
	subLayout.AppendMenu(MF_STRING | (m_specLayout == SpecOverlay ? MF_CHECKED : 0),
		IDM_SPEC_OVERLAY, LL14(L"重ね描き", L"Overlay", L"Superpose", L"Sovrapposto", L"Superpuesto", L"겹침", L"叠加", L"تراكب", L"Наложение", L"Uberlagert", L"Sobreposto", L"Overlay", L"Nakladanie", L"Ustuste"));
	subLayout.AppendMenu(MF_STRING | (m_specLayout == SpecSplitV ? MF_CHECKED : 0),
		IDM_SPEC_SPLIT_V, LL14(L"上下分割", L"Split vertical", L"Split vertical", L"Divisione verticale", L"Division vertical", L"상하 분할", L"上下分割", L"تقسيم رأسي", L"Вертикально", L"Vertikal teilen", L"Dividir vertical", L"Verticaal", L"Pionowo", L"Dikey bol"));
	subLayout.AppendMenu(MF_STRING | (m_specLayout == SpecSplitH ? MF_CHECKED : 0),
		IDM_SPEC_SPLIT_H, LL14(L"左右分割", L"Split horizontal", L"Split horizontal", L"Divisione orizzontale", L"Division horizontal", L"좌우 분할", L"左右分割", L"تقسيم أفقي", L"Горизонтально", L"Horizontal teilen", L"Dividir horizontal", L"Horizontaal", L"Poziomo", L"Yatay bol"));
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
		IDM_STYLE_FILL, LL14(L"塗+線 (Ozone風)", L"Fill + line (Ozone)", L"Remplissage + ligne", L"Riempimento + linea", L"Relleno + linea", L"채움+선", L"填充+线", L"تعبئة+خط", L"Заливка+линия", L"Fullung + Linie", L"Preenchimento + linha", L"Vulling + lijn", L"Wypelnienie + linia", L"Dolgu + cizgi"));
	subStyle.AppendMenu(MF_STRING | (m_specStyle == StyleLine ? MF_CHECKED : 0),
		IDM_STYLE_LINE, LL14(L"線のみ", L"Line only", L"Ligne seule", L"Solo linea", L"Solo linea", L"선만", L"仅线", L"خط فقط", L"Только линия", L"Nur Linie", L"Somente linha", L"Alleen lijn", L"Tylko linia", L"Sadece cizgi"));
	subStyle.AppendMenu(MF_STRING | (m_specStyle == StyleBars ? MF_CHECKED : 0),
		IDM_STYLE_BARS, LL14(L"バー", L"Bars", L"Barres", L"Barre", L"Barras", L"막대", L"柱状", L"أشرطة", L"Столбцы", L"Balken", L"Barras", L"Balken", L"Slupki", L"Cubuk"));
	menu.AppendMenu(MF_POPUP, (UINT_PTR)subStyle.Detach(),
		LL14(L"描画スタイル", L"Draw style", L"Style de dessin", L"Stile disegno", L"Estilo de dibujo", L"그리기 스타일", L"绘制样式", L"نمط الرسم", L"Стиль отрисовки", L"Zeichenstil", L"Estilo de desenho", L"Tekenstijl", L"Styl rysowania", L"Cizim stili"));

	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING | (m_peakHold ? MF_CHECKED : 0),
		IDM_PEAK_HOLD, LL14(L"ピークホールド", L"Peak hold", L"Maintien de crete", L"Picco trattenuto", L"Retencion de pico", L"피크 홀드", L"峰值保持", L"الاحتفاظ بالذروة", L"Удержание пика", L"Peak Hold", L"Retencao de pico", L"Piekvasthouden", L"Przytrzymanie szczytu", L"Tepe tutma"));
	menu.AppendMenu(MF_STRING | (m_eqOverlay ? MF_CHECKED : 0),
		IDM_EQ_OVERLAY, LL14(L"EQオーバーレイ", L"EQ overlay", L"Superposition EQ", L"Sovrapposizione EQ", L"Superposicion EQ", L"EQ 오버레이", L"EQ叠加", L"تراكب EQ", L"Оверлей EQ", L"EQ-Overlay", L"Sobreposicao EQ", L"EQ-overlay", L"Nakladka EQ", L"EQ kaplama"));
	menu.AppendMenu(MF_STRING | (m_frozen ? MF_CHECKED : 0),
		IDM_FREEZE, LL14(L"フリーズ", L"Freeze", L"Gel", L"Congela", L"Congelar", L"정지", L"冻结", L"تجميد", L"Заморозка", L"Einfrieren", L"Congelar", L"Bevriezen", L"Zamroz", L"Dondur"));
	menu.AppendMenu(MF_STRING, IDM_RESET_PEAK,
		LL14(L"ピークをリセット (ダブルクリック)", L"Reset peaks (double-click)", L"Reinit. cretes (double-clic)", L"Reset picchi (doppio clic)", L"Restablecer picos (doble clic)", L"피크 리셋(더블클릭)", L"重置峰值(双击)", L"إعادة الذروة (نقر مزدوج)", L"Сброс пиков (двойной клик)", L"Peaks zurucksetzen (Doppelklick)", L"Redefinir picos (duplo clique)", L"Piekreset (dubbelklik)", L"Reset szczytow (dwuklik)", L"Tepe sifirla (cift tik)"));

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
		// ~1.2 秒分が見える密度。細かすぎると負荷、粗すぎるとカクつく
		const int targetW = (m_waveW > 40) ? m_waveW : 640;
		m_samplesPerCol = (std::max)(4, sampleRate * 6 / 5 / targetW);
	}

	float framePeak[2] = { 0.0f, 0.0f };
	double frameSumSq[2] = { 0.0, 0.0 };
	int frameN[2] = { 0, 0 };
	EnterCriticalSection(&m_cs);
	if (m_channels != channels) {
		m_channels = channels;
		m_waveReady = false;
		m_specDirty = true;
	}

	for (int i = 0; i < frames; ++i) {
		for (int c = 0; c < channels; ++c) {
			const float v = SampleToFloat(pData, bits, i, c, channels);
			m_ring[c][m_ringWrite] = v;
			if (c < 2) {
				const float a = fabsf(v);
				if (a > framePeak[c]) framePeak[c] = a;
				frameSumSq[c] += (double)a * (double)a;
				++frameN[c];
			}
		}
		for (int c = channels; c < CH_MAX; ++c)
			m_ring[c][m_ringWrite] = 0.0f;
		m_ringWrite = (m_ringWrite + 1) % RING_SAMPLES;
		if (m_ringFilled < RING_SAMPLES) ++m_ringFilled;
	}
	m_accSamples += frames;

	float maxPk = 0.0f;
	for (int c = 0; c < 2; ++c) {
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

		if (framePeak[c] > maxPk) maxPk = framePeak[c];
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
	// 多重 Post を防ぐ(ピアノロール RequestSyncFromMainUi と同じ)
	if (!::IsWindow(m_hWnd)) return;
	if (InterlockedCompareExchange(&m_presentPosted, 1, 0) != 0) return;
	PostMessage(WM_ANALYZER_PRESENT, 0, 0);
}

void CAnalyzerDlg::FullRedrawWave(COLORREF bg)
{
	if (!m_waveDC.GetSafeHdc() || m_waveW <= 0 || m_waveH <= 0) return;
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

	for (int c = 0; c < vis; ++c) {
		const int y0 = c * bandH;
		const int y1 = (c == vis - 1) ? m_waveH : (c + 1) * bandH;
		const int mid = (y0 + y1) / 2;
		const int amp = (std::max)(2, (y1 - y0) / 2 - 2);

		CPen grid(PS_SOLID, 1, RGB(40, 44, 58));
		CPen* op = m_waveDC.SelectObject(&grid);
		m_waveDC.MoveTo(0, mid);
		m_waveDC.LineTo(m_waveW, mid);

		CPen pen(PS_SOLID, 1, kChColor[c % CH_MAX]);
		m_waveDC.SelectObject(&pen);

		for (int x = 0; x < m_waveW; ++x) {
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
		m_waveDC.SelectObject(op);
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

		CPen grid(PS_SOLID, 1, RGB(40, 44, 58));
		CPen* op = m_waveDC.SelectObject(&grid);
		m_waveDC.MoveTo(keep, mid);
		m_waveDC.LineTo(m_waveW, mid);

		CPen pen(PS_SOLID, 1, kChColor[c % CH_MAX]);
		m_waveDC.SelectObject(&pen);

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
		m_waveDC.SelectObject(op);
	}
	m_waveDC.SelectObject(oldFont);
	m_waveReady = true;
	return scroll;
}

void CAnalyzerDlg::DrawEqOverlay(CDC& dc, const CRect& plot, float nyquist)
{
	if (plot.Width() < 8 || plot.Height() < 8) return;

	auto hzToX = [&](float hz) -> int {
		const float fMin = 20.0f;
		const float fMax = (std::max)(fMin * 1.01f, nyquist);
		float t = logf((std::max)(hz, fMin) / fMin) / logf(fMax / fMin);
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		return plot.left + (int)(t * plot.Width());
	};
	// EQ ゲイン軸: 上=+12dB / 中=0 / 下=-12dB (equaliser と同じスケール)
	auto gainToY = [&](float gainDb) -> int {
		float t = (gainDb + 12.0f) / 24.0f;
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		return plot.bottom - (int)(t * plot.Height());
	};

	CPen bandPen(PS_DOT, 1, RGB(90, 110, 70));
	CPen* oldPen = dc.SelectObject(&bandPen);
	for (int b = 0; b < EQ_OVERLAY_BANDS; ++b) {
		if (kEqFreqs[b] >= nyquist) continue;
		const int x = hzToX(kEqFreqs[b]);
		dc.MoveTo(x, plot.top);
		dc.LineTo(x, plot.bottom);
	}

	const int y0 = gainToY(0.0f);
	CPen zeroPen(PS_SOLID, 1, RGB(120, 160, 90));
	dc.SelectObject(&zeroPen);
	dc.MoveTo(plot.left, y0);
	dc.LineTo(plot.right, y0);

	POINT pts[EQ_OVERLAY_BANDS];
	int nPts = 0;
	for (int b = 0; b < EQ_OVERLAY_BANDS; ++b) {
		if (kEqFreqs[b] >= nyquist) continue;
		int eqv = savedata.eq[b];
		if (eqv < 0) eqv = 0;
		if (eqv > 200) eqv = 200;
		pts[nPts].x = hzToX(kEqFreqs[b]);
		pts[nPts].y = gainToY(EqSliderToDb(eqv));
		++nPts;
	}
	if (nPts >= 2) {
		CPen eqPen(PS_SOLID, 2, RGB(200, 255, 120));
		dc.SelectObject(&eqPen);
		dc.Polyline(pts, nPts);
		for (int i = 0; i < nPts; ++i) {
			dc.FillSolidRect(pts[i].x - 2, pts[i].y - 2, 5, 5, RGB(220, 255, 160));
		}
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

	if (drawTitle) {
		dc.SetTextColor(RGB(180, 190, 210));
		dc.TextOut(plot.left + 2, (int)(std::max)(0L, plot.top - 13),
			LL14(L"周波数特性", L"Frequency response", L"Reponse en frequence", L"Risposta in frequenza",
				L"Respuesta en frecuencia", L"주파수 특성", L"频率特性", L"الاستجابة الترددية",
				L"АЧХ", L"Frequenzgang", L"Resposta em frequencia", L"Frequentierespons",
				L"Charakterystyka", L"Frekans yaniti"));
	}

	CPen grid(PS_DOT, 1, RGB(45, 50, 65));
	dc.SelectObject(&grid);
	dc.SetTextColor(RGB(120, 130, 150));
	for (int db = 0; db >= -80; db -= 20) {
		const float t = (0.0f - (float)db) / 96.0f;
		const int y = plot.bottom - (int)(t * plot.Height());
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
		const float t = logf(f / 20.0f) / logf(nyquist / 20.0f);
		if (t < 0.0f || t > 1.0f) continue;
		const int x = plot.left + (int)(t * plot.Width());
		dc.MoveTo(x, plot.top);
		dc.LineTo(x, plot.bottom);
		if (drawTitle && plot.Height() > 40) {
			CString lab;
			if (markHz[mi] >= 1000) lab.Format(_T("%dk"), markHz[mi] / 1000);
			else lab.Format(_T("%d"), markHz[mi]);
			dc.TextOut(x - 6, plot.bottom - 12, lab);
		}
	}

	auto dbToY = [&](float db) -> int {
		float t = (db + 96.0f) / 96.0f;
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		return plot.bottom - (int)(t * plot.Height());
	};
	auto hzToX = [&](float hz) -> int {
		const float fMin = 20.0f;
		const float fMax = (std::max)(fMin * 1.01f, nyquist);
		float t = logf((std::max)(hz, fMin) / fMin) / logf(fMax / fMin);
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		return plot.left + (int)(t * plot.Width());
	};

	for (int i = 0; i < chCount; ++i) {
		const int c = chBegin + i;
		if (c < 0 || c >= channels || c >= CH_MAX) continue;

		POINT fillPts[SPEC_BINS + 2];
		POINT peakPts[SPEC_BINS];
		POINT linePts[SPEC_BINS];
		for (int b = 0; b < SPEC_BINS; ++b) {
			const int x = hzToX(SpecCenterHz(b, SPEC_BINS, nyquist));
			linePts[b].x = x;
			linePts[b].y = dbToY(spec[c][b]);
			peakPts[b].x = x;
			peakPts[b].y = dbToY(peak[c][b]);
			fillPts[b] = linePts[b];
		}
		fillPts[SPEC_BINS].x = linePts[SPEC_BINS - 1].x;
		fillPts[SPEC_BINS].y = plot.bottom - 1;
		fillPts[SPEC_BINS + 1].x = linePts[0].x;
		fillPts[SPEC_BINS + 1].y = plot.bottom - 1;

		const COLORREF col = kChColor[c];
		const COLORREF fill = RGB(
			(GetRValue(col) * 2 + 18) / 5,
			(GetGValue(col) * 2 + 20) / 5,
			(GetBValue(col) * 2 + 28) / 5);

		if (style == StyleBars) {
			const int barW = (std::max)(1, plot.Width() / SPEC_BINS - 1);
			for (int b = 0; b < SPEC_BINS; ++b) {
				const int x = linePts[b].x;
				const int y = linePts[b].y;
				CRect bar(x - barW / 2, y, x - barW / 2 + barW, plot.bottom - 1);
				dc.FillSolidRect(bar, fill);
				dc.FillSolidRect(x - barW / 2, y, barW, 2, col);
			}
		}
		else if (style == StyleFill) {
			CBrush br(fill);
			CBrush* oldBr = dc.SelectObject(&br);
			dc.SelectStockObject(NULL_PEN);
			dc.SetPolyFillMode(WINDING);
			dc.Polygon(fillPts, SPEC_BINS + 2);
			dc.SelectObject(oldBr);
		}

		if (drawPeak && style != StyleBars) {
			CPen peakPen(PS_SOLID, 1, RGB(
				(GetRValue(col) * 2 + 255) / 3,
				(GetGValue(col) * 2 + 255) / 3,
				(GetBValue(col) * 2 + 255) / 3));
			dc.SelectObject(&peakPen);
			dc.Polyline(peakPts, SPEC_BINS);
		}
		else if (drawPeak && style == StyleBars) {
			for (int b = 0; b < SPEC_BINS; ++b) {
				dc.FillSolidRect(peakPts[b].x - 1, peakPts[b].y, 3, 2,
					RGB((GetRValue(col) + 255) / 2, (GetGValue(col) + 255) / 2, (GetBValue(col) + 255) / 2));
			}
		}

		if (style != StyleBars) {
			CPen curve(PS_SOLID, 2, col);
			dc.SelectObject(&curve);
			dc.Polyline(linePts, SPEC_BINS);
		}

		dc.SetTextColor(col);
		dc.TextOut(plot.right - 28, plot.top + 2 + i * 12, ChannelLabel(c, channels));
	}

	if (m_eqOverlay && chBegin == 0)
		DrawEqOverlay(dc, plot, nyquist);

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

void CAnalyzerDlg::DrawLevelMeters(CDC& dc, const CRect& waveRc, COLORREF bg)
{
	// bg: アクリル時は CHROMA 以外の不透明色で塗る(キー色だと透過して消える)
	const COLORREF panelBg = (bg == ANALYZER_CHROMA_KEY) ? RGB(22, 26, 36) : RGB(12, 14, 20);
	// アクリル用ストリップは ~39px。40 未満で return するとバーごと消える。
	if (waveRc.Width() < 28 || waveRc.Height() < 40) return;

	float hold[2] = { 0, 0 }, rms[2] = { 0, 0 };
	int channels = 2;
	EnterCriticalSection(&m_cs);
	hold[0] = m_meterHold[0]; hold[1] = m_meterHold[1];
	rms[0] = m_meterRms[0]; rms[1] = m_meterRms[1];
	channels = m_channels;
	LeaveCriticalSection(&m_cs);

	const int meterW = 10;
	const int gap = 3;
	const int n = (channels >= 2) ? 2 : 1;
	const int totalW = n * meterW + (n - 1) * gap + 8;
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
}

// 波形右端のレベルメーター帯幅(クロマ更新用)。DrawLevelMeters の早期 return 下限以上にすること。
static int AnalyzerMeterStripWidth(int channels)
{
	const int n = (channels >= 2) ? 2 : 1;
	return n * 10 + (n - 1) * 3 + 8 + 16; // 余白多め(>=44)
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
	const float fMin = 20.0f;
	const float fMax = (std::max)(fMin * 1.01f, nyquist);
	float tx = logf((std::max)(m_hoverHz, fMin) / fMin) / logf(fMax / fMin);
	if (tx < 0.0f) tx = 0.0f;
	if (tx > 1.0f) tx = 1.0f;
	float ty = (m_hoverDb + 96.0f) / 96.0f;
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
	const float fMin = 20.0f;
	const float fMax = (std::max)(fMin * 1.01f, nyquist);
	const float tx = (float)(ptClient.x - hitPlot.left) / (float)hitPlot.Width();
	const float hz = fMin * powf(fMax / fMin, (std::max)(0.0f, (std::min)(1.0f, tx)));

	int bestB = 0;
	float bestDist = 1e9f;
	for (int b = 0; b < SPEC_BINS; ++b) {
		const float cHz = SpecCenterHz(b, SPEC_BINS, nyquist);
		const float d = fabsf(logf((std::max)(cHz, 1.0f) / (std::max)(hz, 1.0f)));
		if (d < bestDist) { bestDist = d; bestB = b; }
	}

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
	if (rc.IsRectEmpty()) return;

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
		return;
	}

	int pending = 0;
	EnterCriticalSection(&m_cs);
	pending = m_pendingScroll;
	LeaveCriticalSection(&m_cs);

	// 1フレームで大きく飛ぶとカクつく。ピアノロールの framesPending 上限に相当。
	const int scrollCap = (std::max)(2, (std::min)(8, m_waveW > 0 ? m_waveW / 64 : 8));

	bool didWaveFull = false;
	bool didWaveScroll = false;
#if CCUSTOM_AERO_SUPPORT
	m_lastWaveScroll = 0;
#endif

	if (!m_waveReady) {
		FullRedrawWave(bg);
		didWaveFull = true;
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

	// 未消化の波形スクロールがあれば次フレームへ(追い付き用の自己 Invalidate は
	// ピアノロール同様 PostMessage 合流で UI 占有を避ける)
	EnterCriticalSection(&m_cs);
	const bool moreScroll = (m_pendingScroll > 0);
	LeaveCriticalSection(&m_cs);
	if (moreScroll)
		KickUiPresent();

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
				const int stripW = (std::min)(waveW, AnalyzerMeterStripWidth(m_channels));
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

			m_chromaReady = true;
			m_chromaCache.BlitFull(dc.GetSafeHdc(), 0, 0, clientW, clientH);
		}
		else {
			Present(dc, rc, FALSE);
		}
	}
	else
#endif
	{
		Present(dc, rc, FALSE);
	}
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
		CCC_RefreshDialogDwmBlur(m_hWnd);
#endif
	Invalidate(FALSE);
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

void CAnalyzerDlg::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	UNREFERENCED_PARAMETER(nFlags);
	UNREFERENCED_PARAMETER(point);
	ResetPeakHold();
	CCustomBlurDialogExBase::OnLButtonDblClk(nFlags, point);
}

BOOL CAnalyzerDlg::PreTranslateMessage(MSG* pMsg)
{
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
		if (pMsg->wParam == VK_SPACE) {
			ResetPeakHold();
			return TRUE;
		}
	}
	return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}
