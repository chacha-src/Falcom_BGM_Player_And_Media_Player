// ScreenCaptureDlg.cpp
// 画面キャプチャ → MP4 (H.264 + AAC)
// プライマリ / 全モニタ / ウィンドウ合成(配置・拡大縮小・Z順)

#include "stdafx.h"
#include "ogg.h"
#include "oggDlg.h"
#include "ScreenCaptureDlg.h"
#include "ScLiveSettingsDlg.h"
#include "AudioDevSync.h"
#include "ScWgcCapture.h"
#include "CMediaPlayerDlg.h"
#include <mmdeviceapi.h>
#include <Audioclient.h>
#include <FunctionDiscoveryKeys_devpkey.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <codecapi.h>
#include <wmcodecdsp.h>
#include <process.h>
#include <math.h>
#include <ShlObj.h>
#include <vector>
#include <mmsystem.h>

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "wmcodecdspuuid.lib")
#pragma comment(lib, "winmm.lib")

extern void MpPersistSavedataQuick();
extern volatile LONG g_interactiveTrackChange;

// YouTube go-live API は UI スレッドで同期するとプレビューが固まるので専用スレッドへ
struct ScYtGoLiveThunk {
	static UINT __stdcall Run(void* p)
	{
		CScreenCaptureDlg* self = (CScreenCaptureDlg*)p;
		if (self)
			self->TryYouTubeGoLiveTransition();
		return 0;
	}
};

#ifndef MF_E_TRANSFORM_NEED_MORE_INPUT
#define MF_E_TRANSFORM_NEED_MORE_INPUT ((HRESULT)0xC00D6D72L)
#endif
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#ifndef WDA_NONE
#define WDA_NONE 0x00000000
#endif
#ifndef MF_SINK_WRITER_DISABLE_THROTTLING
// Windows SDK によってヘッダ未定義のことがある
DEFINE_GUID(MF_SINK_WRITER_DISABLE_THROTTLING,
	0x08b845d8, 0x2b74, 0x4afe, 0x9e, 0x8e, 0x5a, 0x03, 0x9f, 0xb8, 0x6e, 0xbe);
#endif

namespace {

static const GUID s_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT =
{ 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID s_KSDATAFORMAT_SUBTYPE_PCM =
{ 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

static float ScClamp1(float v)
{
	if (v > 1.f) return 1.f;
	if (v < -1.f) return -1.f;
	return v;
}

static void ScSampleToFloat(const BYTE* frame, const WAVEFORMATEX* fmt, float& L, float& R)
{
	L = R = 0.f;
	if (!frame || !fmt) return;
	const WORD tag = fmt->wFormatTag;
	const WORD bits = fmt->wBitsPerSample;
	const int ch = (int)fmt->nChannels;
	const BOOL isFloat = (tag == WAVE_FORMAT_IEEE_FLOAT)
		|| (tag == WAVE_FORMAT_EXTENSIBLE && bits == 32
			&& ((const WAVEFORMATEXTENSIBLE*)fmt)->SubFormat == s_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
	const BOOL isPcmExt = (tag == WAVE_FORMAT_EXTENSIBLE
		&& ((const WAVEFORMATEXTENSIBLE*)fmt)->SubFormat == s_KSDATAFORMAT_SUBTYPE_PCM);
	if (isFloat) {
		const float* f = (const float*)frame;
		L = f[0];
		R = (ch >= 2) ? f[1] : L;
	} else if (bits == 16 || (isPcmExt && bits == 16) || (tag == WAVE_FORMAT_PCM && bits == 16)) {
		const short* s = (const short*)frame;
		L = (float)s[0] / 32768.f;
		R = (ch >= 2) ? ((float)s[1] / 32768.f) : L;
	} else if (bits == 24) {
		int v = (int)(frame[0] | (frame[1] << 8) | (frame[2] << 16));
		if (v & 0x800000) v |= ~0xFFFFFF;
		L = (float)v / 8388608.f;
		if (ch >= 2) {
			int v2 = (int)(frame[3] | (frame[4] << 8) | (frame[5] << 16));
			if (v2 & 0x800000) v2 |= ~0xFFFFFF;
			R = (float)v2 / 8388608.f;
		} else R = L;
	} else if (bits == 32) {
		const int* s = (const int*)frame;
		L = (float)s[0] / 2147483648.f;
		R = (ch >= 2) ? ((float)s[1] / 2147483648.f) : L;
	}
}

enum { SC_MIC_FRAMES = 48000, SC_MIC_CH = 2 };
static float s_micRing[SC_MIC_FRAMES * SC_MIC_CH];
static volatile LONG s_micW = 0;
static volatile LONG s_micR = 0;
static CRITICAL_SECTION s_micCs;
static volatile LONG s_micCsInit = 0;
static volatile LONG s_micCapRate = 0;

static void ScMicEnsureCs()
{
	if (InterlockedCompareExchange(&s_micCsInit, 1, 0) == 0)
		InitializeCriticalSection(&s_micCs);
}

static void ScMicRingWrite(const float* interleaved, int frames)
{
	if (!interleaved || frames <= 0) return;
	EnterCriticalSection(&s_micCs);
	LONG w = s_micW;
	for (int i = 0; i < frames; ++i) {
		const int wi = (int)(w % SC_MIC_FRAMES);
		s_micRing[wi * SC_MIC_CH + 0] = interleaved[i * 2 + 0];
		s_micRing[wi * SC_MIC_CH + 1] = interleaved[i * 2 + 1];
		w++;
	}
	s_micW = w;
	LONG r = s_micR;
	if ((LONG)(w - r) > (SC_MIC_FRAMES - 64))
		s_micR = w - (SC_MIC_FRAMES / 2);
	LeaveCriticalSection(&s_micCs);
}

static void ScMicIntoStereo(float* L, float* R, int frames, int outRate)
{
	if (!L || !R || frames <= 0 || outRate < 8000) return;
	const int capRate = (int)InterlockedCompareExchange(&s_micCapRate, 0, 0);
	if (capRate < 8000) return;
	float gain = (float)savedata.mic_mix_level / 100.f;
	if (gain < 0.f) gain = 0.f;
	if (gain > 2.f) gain = 2.f;
	EnterCriticalSection(&s_micCs);
	LONG r = s_micR;
	const LONG w = s_micW;
	const double step = (double)capRate / (double)outRate;
	double pos = 0.0;
	for (int i = 0; i < frames; ++i) {
		LONG ri = r + (LONG)pos;
		float mL = 0.f, mR = 0.f;
		if (ri < w) {
			const int idx = (int)(ri % SC_MIC_FRAMES);
			mL = s_micRing[idx * SC_MIC_CH + 0];
			mR = s_micRing[idx * SC_MIC_CH + 1];
		}
		L[i] = ScClamp1(L[i] + mL * gain);
		R[i] = ScClamp1(R[i] + mR * gain);
		pos += step;
	}
	s_micR = r + (LONG)pos;
	LeaveCriticalSection(&s_micCs);
}

static HRESULT ScActivateLoopClient(IMMDevice* renderDev, IAudioClient** outClient, WAVEFORMATEX** outFmt)
{
	if (!renderDev || !outClient || !outFmt) return E_POINTER;
	*outClient = NULL;
	*outFmt = NULL;
	IAudioClient* client = NULL;
	WAVEFORMATEX* fmt = NULL;
	HRESULT hr = renderDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&client);
	if (FAILED(hr) || !client) return FAILED(hr) ? hr : E_FAIL;
	hr = client->GetMixFormat(&fmt);
	if (FAILED(hr) || !fmt) {
		client->Release();
		return FAILED(hr) ? hr : E_FAIL;
	}
	*outClient = client;
	*outFmt = fmt;
	return S_OK;
}

static HRESULT ScInitLoopbackCapture(IMMDevice* renderDev,
	IAudioClient** outClient, IAudioCaptureClient** outCap, WAVEFORMATEX** outFmt, HANDLE* outEvent)
{
	*outClient = NULL;
	*outCap = NULL;
	*outFmt = NULL;
	*outEvent = NULL;

	IAudioClient* client = NULL;
	WAVEFORMATEX* fmt = NULL;
	HRESULT hr = ScActivateLoopClient(renderDev, &client, &fmt);
	if (FAILED(hr)) return hr;
	hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
		AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
		2000000, 0, fmt, NULL);
	if (FAILED(hr)) {
		client->Release();
		CoTaskMemFree(fmt);
		client = NULL; fmt = NULL;
		hr = ScActivateLoopClient(renderDev, &client, &fmt);
		if (FAILED(hr)) return hr;
		HANDLE ev = CreateEvent(NULL, FALSE, FALSE, NULL);
		if (!ev) {
			client->Release();
			CoTaskMemFree(fmt);
			return E_OUTOFMEMORY;
		}
		hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
			AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
			2000000, 0, fmt, NULL);
		if (SUCCEEDED(hr))
			hr = client->SetEventHandle(ev);
		if (FAILED(hr)) {
			CloseHandle(ev);
			client->Release();
			CoTaskMemFree(fmt);
			return hr;
		}
		IAudioCaptureClient* cap = NULL;
		hr = client->GetService(__uuidof(IAudioCaptureClient), (void**)&cap);
		if (FAILED(hr) || !cap) {
			CloseHandle(ev);
			client->Release();
			CoTaskMemFree(fmt);
			return FAILED(hr) ? hr : E_FAIL;
		}
		*outClient = client;
		*outCap = cap;
		*outFmt = fmt;
		*outEvent = ev;
		return S_OK;
	}

	IAudioCaptureClient* cap = NULL;
	hr = client->GetService(__uuidof(IAudioCaptureClient), (void**)&cap);
	if (FAILED(hr) || !cap) {
		client->Release();
		CoTaskMemFree(fmt);
		return FAILED(hr) ? hr : E_FAIL;
	}
	*outClient = client;
	*outCap = cap;
	*outFmt = fmt;
	*outEvent = NULL;
	return S_OK;
}

struct ScFrameBuf {
	BYTE* bits;
	int w, h, stride;
	HBITMAP bmp;
	HDC hdc;
	HGDIOBJ old;
};

static void ScFrameFree(ScFrameBuf& fb)
{
	if (fb.hdc) {
		if (fb.old) SelectObject(fb.hdc, fb.old);
		DeleteDC(fb.hdc);
	}
	if (fb.bmp) DeleteObject(fb.bmp);
	fb.bits = NULL;
	fb.bmp = NULL;
	fb.hdc = NULL;
	fb.old = NULL;
	fb.w = fb.h = fb.stride = 0;
}

static BOOL ScFrameAlloc(ScFrameBuf& fb, int w, int h)
{
	ScFrameFree(fb);
	if (w < 2 || h < 2) return FALSE;
	w &= ~1;
	h &= ~1;
	BITMAPINFO bmi = {};
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = w;
	bmi.bmiHeader.biHeight = -h;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;
	void* bits = NULL;
	HDC screen = GetDC(NULL);
	HBITMAP bmp = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
	ReleaseDC(NULL, screen);
	if (!bmp || !bits) {
		if (bmp) DeleteObject(bmp);
		return FALSE;
	}
	HDC hdc = CreateCompatibleDC(NULL);
	if (!hdc) {
		DeleteObject(bmp);
		return FALSE;
	}
	HGDIOBJ old = SelectObject(hdc, bmp);
	fb.bits = (BYTE*)bits;
	fb.w = w;
	fb.h = h;
	fb.stride = w * 4;
	fb.bmp = bmp;
	fb.hdc = hdc;
	fb.old = old;
	return TRUE;
}

static void ScFrameClear(ScFrameBuf& fb, COLORREF c)
{
	if (!fb.hdc) return;
	RECT r = { 0, 0, fb.w, fb.h };
	HBRUSH br = CreateSolidBrush(c);
	if (br) {
		FillRect(fb.hdc, &r, br);
		DeleteObject(br);
	}
}

// スレッドローカル再利用バッファ（4K PrintWindow の都度 alloc を避ける）
static thread_local ScFrameBuf s_scNativeBuf;
static thread_local ScFrameBuf s_scScaledBuf;

static BOOL ScIsExcludedHwnd(HWND hwnd, HWND excludeHwnd)
{
	if (!hwnd || !excludeHwnd) return FALSE;
	if (hwnd == excludeHwnd) return TRUE;
	if (::IsChild(excludeHwnd, hwnd)) return TRUE;
	HWND root = ::GetAncestor(hwnd, GA_ROOT);
	return (root == excludeHwnd) ? TRUE : FALSE;
}

// top-down DIB へ PrintWindow すると黒になる環境がある → 互換BMP経由で取る
static BOOL ScPrintWindowViaCompat(HWND hwnd, int ww, int wh, ScFrameBuf& outTopDown)
{
	if (!hwnd || ww < 2 || wh < 2) return FALSE;
	ww &= ~1; wh &= ~1;
	if (!outTopDown.bits || outTopDown.w != ww || outTopDown.h != wh) {
		if (!ScFrameAlloc(outTopDown, ww, wh))
			return FALSE;
	}

	HDC hdcScreen = ::GetDC(NULL);
	if (!hdcScreen) return FALSE;
	HDC hdcMem = ::CreateCompatibleDC(hdcScreen);
	HBITMAP hbmp = hdcMem ? ::CreateCompatibleBitmap(hdcScreen, ww, wh) : NULL;
	BOOL ok = FALSE;
	if (hdcMem && hbmp) {
		HGDIOBJ old = ::SelectObject(hdcMem, hbmp);
		::PatBlt(hdcMem, 0, 0, ww, wh, BLACKNESS);
		ok = ::PrintWindow(hwnd, hdcMem, PW_RENDERFULLCONTENT);
		if (!ok)
			ok = ::PrintWindow(hwnd, hdcMem, 0);
		if (ok)
			ok = ::BitBlt(outTopDown.hdc, 0, 0, ww, wh, hdcMem, 0, 0, SRCCOPY);
		::SelectObject(hdcMem, old);
	}
	if (hbmp) ::DeleteObject(hbmp);
	if (hdcMem) ::DeleteDC(hdcMem);
	::ReleaseDC(NULL, hdcScreen);
	return ok;
}

static BOOL ScBufferMostlyBlack(const ScFrameBuf& fb)
{
	if (!fb.bits || fb.w < 4 || fb.h < 4) return TRUE;
	const int pts[][2] = {
		{ fb.w / 2, fb.h / 2 },
		{ fb.w / 4, fb.h / 4 },
		{ (fb.w * 3) / 4, (fb.h * 3) / 4 },
		{ fb.w / 4, (fb.h * 3) / 4 },
		{ (fb.w * 3) / 4, fb.h / 4 },
	};
	int dark = 0;
	for (int i = 0; i < 5; ++i) {
		const int x = pts[i][0];
		const int y = pts[i][1];
		const BYTE* p = fb.bits + (size_t)y * (size_t)fb.stride + (size_t)x * 4;
		// BGRA
		if (p[0] < 12 && p[1] < 12 && p[2] < 12)
			dark++;
	}
	return dark >= 4;
}

// 他窓に隠れていなければ画面コピーで高速化できる（OBS等と同系統）
static BOOL ScWindowClearForScreenCap(HWND hwnd, HWND excludeHwnd)
{
	if (!hwnd || !::IsWindow(hwnd) || !::IsWindowVisible(hwnd) || ::IsIconic(hwnd))
		return FALSE;
	RECT wr = {};
	if (!::GetWindowRect(hwnd, &wr)) return FALSE;
	const int ww = wr.right - wr.left;
	const int wh = wr.bottom - wr.top;
	if (ww < 8 || wh < 8) return FALSE;
	const POINT pts[5] = {
		{ wr.left + ww / 2, wr.top + wh / 2 },
		{ wr.left + 8, wr.top + 8 },
		{ wr.right - 9, wr.top + 8 },
		{ wr.left + 8, wr.bottom - 9 },
		{ wr.right - 9, wr.bottom - 9 },
	};
	for (int i = 0; i < 5; ++i) {
		HWND hit = ::WindowFromPoint(pts[i]);
		if (!hit) return FALSE;
		HWND root = ::GetAncestor(hit, GA_ROOT);
		if (root == hwnd)
			continue;
		// キャプチャUI自身は WDA で穴になるので「見えている」扱いにしない
		if (excludeHwnd && (root == excludeHwnd || ::IsChild(excludeHwnd, hit)))
			return FALSE;
		return FALSE;
	}
	return TRUE;
}

// dst サイズへ直接画面ストレッチ（4K PrintWindow を避ける高速経路）
static BOOL ScCaptureWindowFromScreen(HWND hwnd, ScFrameBuf& fb, int dstW, int dstH)
{
	RECT wr = {};
	if (!::GetWindowRect(hwnd, &wr)) return FALSE;
	const int ww = wr.right - wr.left;
	const int wh = wr.bottom - wr.top;
	if (ww < 2 || wh < 2) return FALSE;
	HDC screen = ::GetDC(NULL);
	if (!screen) return FALSE;
	::SetStretchBltMode(fb.hdc, COLORONCOLOR);
	const BOOL ok = ::StretchBlt(fb.hdc, 0, 0, dstW, dstH, screen, wr.left, wr.top, ww, wh, SRCCOPY);
	::ReleaseDC(NULL, screen);
	if (!ok) return FALSE;
	if (ScBufferMostlyBlack(fb)) return FALSE;
	return TRUE;
}

// dst サイズへウィンドウキャプチャ
// 1) WGC（前面UI/遮蔽に依存しない・OBS同系統）
// 2) 画面 StretchBlt（完全に見えているとき）
// 3) PrintWindow（フォールバック・重い）
// dst サイズへウィンドウキャプチャ（任意でウィンドウ内切り出し）
// 1) Windows.Graphics.Capture（前面UI/遮蔽に依存しない・OBS同系統）
// 2) 画面 StretchBlt（完全に見えているとき）
// 3) PrintWindow（フォールバック・重い）
static BOOL ScCaptureWindowScaled(HWND hwnd, ScFrameBuf& fb, int dstW, int dstH, HWND excludeHwnd,
	int cropX, int cropY, int cropW, int cropH, BOOL forceGdi)
{
	if (!hwnd || !IsWindow(hwnd)) return FALSE;
	RECT wr = {};
	if (!::GetWindowRect(hwnd, &wr)) return FALSE;
	const int ww = wr.right - wr.left;
	const int wh = wr.bottom - wr.top;
	if (ww < 2 || wh < 2 || dstW < 2 || dstH < 2) return FALSE;
	dstW &= ~1; dstH &= ~1;
	if (!fb.bits || fb.w != dstW || fb.h != dstH) {
		if (!ScFrameAlloc(fb, dstW, dstH))
			return FALSE;
	}

	BOOL useCrop = (cropW > 1 && cropH > 1);
	if (useCrop) {
		if (cropX < 0) cropX = 0;
		if (cropY < 0) cropY = 0;
		if (cropX + cropW > ww) cropW = ww - cropX;
		if (cropY + cropH > wh) cropH = wh - cropY;
		if (cropW < 2 || cropH < 2) useCrop = FALSE;
	}

	::SetStretchBltMode(fb.hdc, COLORONCOLOR);

	// 1) Windows.Graphics.Capture（前面UIでも可）※録画中は固着するためスキップ
	if (!forceGdi) {
		if (useCrop) {
			if (ScWgcCaptureWindowBgraCrop(hwnd, fb.bits, dstW, dstH, fb.stride, cropX, cropY, cropW, cropH))
				return TRUE;
		} else if (ScWgcCaptureWindowBgra(hwnd, fb.bits, dstW, dstH, fb.stride)) {
			return TRUE;
		}
	}

	// 2) 高速経路: ターゲットが他窓に隠れていなければ画面から直接縮小コピー
	if (ScWindowClearForScreenCap(hwnd, excludeHwnd)) {
		HDC screen = ::GetDC(NULL);
		BOOL ok = FALSE;
		if (screen) {
			const int sx = wr.left + (useCrop ? cropX : 0);
			const int sy = wr.top + (useCrop ? cropY : 0);
			const int sw = useCrop ? cropW : ww;
			const int sh = useCrop ? cropH : wh;
			ok = ::StretchBlt(fb.hdc, 0, 0, dstW, dstH, screen, sx, sy, sw, sh, SRCCOPY);
			::ReleaseDC(NULL, screen);
			if (ok && ScBufferMostlyBlack(fb)) ok = FALSE;
		}
		if (ok) return TRUE;
	}

	// 3) 正確経路: PrintWindow（WGC不可・遮蔽時）
	// 曲切替中は stop/play の DoEvent へ WM_PRINT が再入してクラッシュするため禁止。
	BOOL gotNative = FALSE;
	if (InterlockedCompareExchange(&g_interactiveTrackChange, 0, 0) == 0)
		gotNative = ScPrintWindowViaCompat(hwnd, ww, wh, s_scNativeBuf);
	if (!gotNative) {
		HDC wdc = ::GetWindowDC(hwnd);
		BOOL ok = FALSE;
		if (wdc) {
			if (!s_scNativeBuf.bits || s_scNativeBuf.w != (ww & ~1) || s_scNativeBuf.h != (wh & ~1)) {
				if (!ScFrameAlloc(s_scNativeBuf, ww, wh)) {
					::ReleaseDC(hwnd, wdc);
					return FALSE;
				}
			}
			ok = ::BitBlt(s_scNativeBuf.hdc, 0, 0, s_scNativeBuf.w, s_scNativeBuf.h, wdc, 0, 0, SRCCOPY);
			::ReleaseDC(hwnd, wdc);
			if (ok && ScBufferMostlyBlack(s_scNativeBuf))
				ok = FALSE;
		}
		if (!ok)
			return FALSE;
	}

	if (useCrop) {
		return ::StretchBlt(fb.hdc, 0, 0, fb.w, fb.h,
			s_scNativeBuf.hdc, cropX, cropY, cropW, cropH, SRCCOPY);
	}
	if (fb.w == s_scNativeBuf.w && fb.h == s_scNativeBuf.h)
		return ::BitBlt(fb.hdc, 0, 0, fb.w, fb.h, s_scNativeBuf.hdc, 0, 0, SRCCOPY);
	return ::StretchBlt(fb.hdc, 0, 0, fb.w, fb.h,
		s_scNativeBuf.hdc, 0, 0, s_scNativeBuf.w, s_scNativeBuf.h, SRCCOPY);
}

// キャンバスへウィンドウを描画（画面合成ではなく HWND ターゲット）
static BOOL ScBlitWindowClipped(HWND hwnd, HDC dst, int canvasW, int canvasH,
	int dx, int dy, int dw, int dh, HWND excludeHwnd,
	int cropX, int cropY, int cropW, int cropH, BOOL forceGdi)
{
	if (!hwnd || !dst || !IsWindow(hwnd) || dw < 1 || dh < 1) return FALSE;
	if (ScIsExcludedHwnd(hwnd, excludeHwnd)) return FALSE;
	RECT wr = {};
	if (!::GetWindowRect(hwnd, &wr)) return FALSE;
	const int ww = wr.right - wr.left;
	const int wh = wr.bottom - wr.top;
	if (ww < 2 || wh < 2) return FALSE;

	int x0 = dx, y0 = dy, x1 = dx + dw, y1 = dy + dh;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > canvasW) x1 = canvasW;
	if (y1 > canvasH) y1 = canvasH;
	if (x1 <= x0 || y1 <= y0) return TRUE;

	const int outW = x1 - x0;
	const int outH = y1 - y0;
	if (outW < 1 || outH < 1) return TRUE;

	int capW = dw & ~1;
	int capH = dh & ~1;
	if (capW < 2) capW = 2;
	if (capH < 2) capH = 2;
	if (!ScCaptureWindowScaled(hwnd, s_scScaledBuf, capW, capH, excludeHwnd, cropX, cropY, cropW, cropH, forceGdi))
		return FALSE;

	const int sx = x0 - dx;
	const int sy = y0 - dy;
	::SetStretchBltMode(dst, COLORONCOLOR);
	if (s_scScaledBuf.w == capW && s_scScaledBuf.h == capH)
		return ::BitBlt(dst, x0, y0, outW, outH, s_scScaledBuf.hdc, sx, sy, SRCCOPY);
	return ::StretchBlt(dst, x0, y0, outW, outH,
		s_scScaledBuf.hdc, sx, sy, outW, outH, SRCCOPY);
}

static thread_local HDC s_gdiScreenDc = NULL;

static void ScGdiReleaseScreenDc()
{
	if (s_gdiScreenDc) {
		ReleaseDC(NULL, s_gdiScreenDc);
		s_gdiScreenDc = NULL;
	}
}

static HDC ScGdiScreenDc()
{
	if (!s_gdiScreenDc)
		s_gdiScreenDc = GetDC(NULL);
	return s_gdiScreenDc;
}

static BOOL ScCaptureMonitorRectGdi(const RECT& mr, ScFrameBuf& out)
{
	const int mw = mr.right - mr.left;
	const int mh = mr.bottom - mr.top;
	if (mw < 2 || mh < 2 || !out.bits || !out.hdc) return FALSE;
	HDC screen = ScGdiScreenDc();
	if (!screen) return FALSE;
	BOOL ok = FALSE;
	if (out.w == mw && out.h == mh) {
		// 等倍は BitBlt の方が速い（StretchBlt/CAPTUREBLT は 1080p で FPS 半減しやすい）
		ok = BitBlt(out.hdc, 0, 0, out.w, out.h, screen, mr.left, mr.top, SRCCOPY);
	} else {
		SetStretchBltMode(out.hdc, COLORONCOLOR);
		ok = StretchBlt(out.hdc, 0, 0, out.w, out.h, screen, mr.left, mr.top, mw, mh, SRCCOPY);
	}
	return ok;
}

static BOOL ScCaptureMonitorFast(HMONITOR mon, const RECT& mr, ScFrameBuf& out, BOOL forceGdi)
{
	if (out.w < 2 || out.h < 2) return FALSE;
	if (!forceGdi && mon && ScWgcCaptureMonitorBgra(mon, out.bits, out.w, out.h, out.stride))
		return TRUE;
	return ScCaptureMonitorRectGdi(mr, out);
}

// 画面座標→キャンバスへマウスカーソルを載せる（FX後に呼ぶ）
static void ScDrawCursorAt(HDC hdc, int dx, int dy, double sx, double sy, HCURSOR hCursor)
{
	if (!hdc || !hCursor) return;
	ICONINFO ii = {};
	int hotX = 0, hotY = 0;
	if (::GetIconInfo(hCursor, &ii)) {
		hotX = (int)ii.xHotspot;
		hotY = (int)ii.yHotspot;
		if (ii.hbmMask) ::DeleteObject(ii.hbmMask);
		if (ii.hbmColor) ::DeleteObject(ii.hbmColor);
	}
	const int iw = ::GetSystemMetrics(SM_CXCURSOR);
	const int ih = ::GetSystemMetrics(SM_CYCURSOR);
	int dw = (int)(iw * sx + 0.5);
	int dh = (int)(ih * sy + 0.5);
	if (dw < 8) dw = 8;
	if (dh < 8) dh = 8;
	const int ox = dx - (int)(hotX * sx + 0.5);
	const int oy = dy - (int)(hotY * sy + 0.5);
	::DrawIconEx(hdc, ox, oy, hCursor, dw, dh, 0, NULL, DI_NORMAL);
}

static void ScOverlayCursorOnFrame(HDC hdc, int canvasW, int canvasH,
	int srcL, int srcT, int srcR, int srcB)
{
	if (!hdc || canvasW < 2 || canvasH < 2 || srcR <= srcL || srcB <= srcT)
		return;
	CURSORINFO ci = {};
	ci.cbSize = sizeof(ci);
	if (!::GetCursorInfo(&ci) || !(ci.flags & CURSOR_SHOWING) || !ci.hCursor)
		return;
	const double sx = (double)canvasW / (double)(srcR - srcL);
	const double sy = (double)canvasH / (double)(srcB - srcT);
	const int cx = (int)((ci.ptScreenPos.x - srcL) * sx + 0.5);
	const int cy = (int)((ci.ptScreenPos.y - srcT) * sy + 0.5);
	if (cx < -64 || cy < -64 || cx > canvasW + 64 || cy > canvasH + 64)
		return;
	ScDrawCursorAt(hdc, cx, cy, sx, sy, ci.hCursor);
}

static BOOL ScTryMapCursorOntoLayer(HWND hwnd, int lx, int ly, int lw, int lh,
	int srcX, int srcY, int srcW, int srcH, POINT pt,
	int* outCx, int* outCy, double* outSx, double* outSy)
{
	if (!hwnd || !::IsWindow(hwnd) || lw < 1 || lh < 1 || !outCx || !outCy || !outSx || !outSy)
		return FALSE;
	RECT wr = {};
	if (!::GetWindowRect(hwnd, &wr))
		return FALSE;
	const int ww = wr.right - wr.left;
	const int wh = wr.bottom - wr.top;
	if (ww < 1 || wh < 1) return FALSE;
	const int cx0 = (srcW > 0) ? srcX : 0;
	const int cy0 = (srcH > 0) ? srcY : 0;
	const int cw = (srcW > 0) ? srcW : ww;
	const int ch = (srcH > 0) ? srcH : wh;
	if (cw < 1 || ch < 1) return FALSE;
	const int left = wr.left + cx0;
	const int top = wr.top + cy0;
	const int right = left + cw;
	const int bottom = top + ch;
	if (pt.x < left || pt.x >= right || pt.y < top || pt.y >= bottom)
		return FALSE;
	const double sx = (double)lw / (double)cw;
	const double sy = (double)lh / (double)ch;
	*outCx = lx + (int)((pt.x - left) * sx + 0.5);
	*outCy = ly + (int)((pt.y - top) * sy + 0.5);
	*outSx = sx;
	*outSy = sy;
	return TRUE;
}

static void ScOverlayCursorComposeWindows(HDC hdc, const CScreenCaptureDlg::ComposeSnap& snap)
{
	if (!hdc) return;
	CURSORINFO ci = {};
	ci.cbSize = sizeof(ci);
	if (!::GetCursorInfo(&ci) || !(ci.flags & CURSOR_SHOWING) || !ci.hCursor)
		return;
	int cx = 0, cy = 0;
	double sx = 1.0, sy = 1.0;
	BOOL hit = FALSE;
	// 手前(index 0)から探す
	for (int i = 0; i < snap.layerCnt; ++i) {
		const CScreenCaptureDlg::Layer& L = snap.layers[i];
		if (L.hidden) continue;
		if (ScIsExcludedHwnd(L.hwnd, snap.excludeHwnd)) continue;
		if (ScTryMapCursorOntoLayer(L.hwnd, L.x, L.y, L.w, L.h,
			L.srcX, L.srcY, L.srcW, L.srcH, ci.ptScreenPos, &cx, &cy, &sx, &sy)) {
			hit = TRUE;
			break;
		}
	}
	if (!hit && snap.includeMp && !snap.mpHidden) {
		hit = ScTryMapCursorOntoLayer(snap.mpHwnd, snap.mpX, snap.mpY, snap.mpW, snap.mpH,
			snap.mpSrcX, snap.mpSrcY, snap.mpSrcW, snap.mpSrcH,
			ci.ptScreenPos, &cx, &cy, &sx, &sy);
	}
	if (!hit) return;
	ScDrawCursorAt(hdc, cx, cy, sx, sy, ci.hCursor);
}

static BOOL ScComposeFrame(ScFrameBuf& out, const CScreenCaptureDlg::ComposeSnap& snap, BOOL forceGdi)
{
	int cw = snap.canvasW;
	int ch = snap.canvasH;
	cw &= ~1; ch &= ~1;
	if (cw < 2 || ch < 2) return FALSE;
	if (!out.bits || out.w != cw || out.h != ch) {
		if (!ScFrameAlloc(out, cw, ch))
			return FALSE;
	}

	if (snap.mode == CScreenCaptureDlg::SC_MODE_PRIMARY
		|| snap.mode == CScreenCaptureDlg::SC_MODE_MONITOR) {
		RECT mr = { snap.monL, snap.monT, snap.monR, snap.monB };
		if (mr.right <= mr.left || mr.bottom <= mr.top) {
			mr.left = 0;
			mr.top = 0;
			mr.right = GetSystemMetrics(SM_CXSCREEN);
			mr.bottom = GetSystemMetrics(SM_CYSCREEN);
		}
		HMONITOR mon = snap.monHandle;
		if (!mon)
			mon = MonitorFromRect(&mr, MONITOR_DEFAULTTONEAREST);
		// 全面キャプチャは上書きするので Clear 不要（1080p で数ms節約）
		BOOL ok = ScCaptureMonitorFast(mon, mr, out, forceGdi);
		if (ok && snap.fxN > 0 && out.bits)
			ScGpuApplyEffectChain(out.bits, out.w, out.h, out.stride, snap.fx, snap.fxN, snap.fxTime, snap.fxStr);
		if (ok && snap.showCursor && out.hdc)
			ScOverlayCursorOnFrame(out.hdc, out.w, out.h, mr.left, mr.top, mr.right, mr.bottom);
		return ok;
	}

	ScFrameClear(out, RGB(16, 16, 20));

	if (snap.mode == CScreenCaptureDlg::SC_MODE_VIRTUAL) {
		const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
		const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
		const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
		const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
		RECT vr = { vx, vy, vx + vw, vy + vh };
		// 仮想全体は複数GPUまたぎで WGC 1本では取れないことが多い → GDI
		BOOL ok = ScCaptureMonitorRectGdi(vr, out);
		if (ok && snap.fxN > 0 && out.bits)
			ScGpuApplyEffectChain(out.bits, out.w, out.h, out.stride, snap.fx, snap.fxN, snap.fxTime, snap.fxStr);
		if (ok && snap.showCursor && out.hdc)
			ScOverlayCursorOnFrame(out.hdc, out.w, out.h, vr.left, vr.top, vr.right, vr.bottom);
		return ok;
	}

	// ウィンドウ合成: PrintWindow で HWND 単位（前面に隠れてもターゲットを描く／キャプチャUIは除外）
	for (int i = snap.layerCnt - 1; i >= 0; --i) {
		const CScreenCaptureDlg::Layer& L = snap.layers[i];
		if (L.hidden) continue;
		if (!L.hwnd || !IsWindow(L.hwnd) || L.w < 1 || L.h < 1) continue;
		if (ScIsExcludedHwnd(L.hwnd, snap.excludeHwnd)) continue;
		ScBlitWindowClipped(L.hwnd, out.hdc, out.w, out.h, L.x, L.y, L.w, L.h, snap.excludeHwnd,
			L.srcX, L.srcY, L.srcW, L.srcH, forceGdi);
	}

	// MP画面を別途載せる (レイヤに無い場合 / Hide 時は載せない)
	if (snap.includeMp && !snap.mpHidden && snap.mpHwnd && ::IsWindow(snap.mpHwnd)
		&& snap.mpW > 1 && snap.mpH > 1
		&& !ScIsExcludedHwnd(snap.mpHwnd, snap.excludeHwnd)) {
		BOOL already = FALSE;
		for (int i = 0; i < snap.layerCnt; ++i) {
			if (snap.layers[i].hwnd == snap.mpHwnd) { already = TRUE; break; }
		}
		if (!already) {
			ScBlitWindowClipped(snap.mpHwnd, out.hdc, out.w, out.h,
				snap.mpX, snap.mpY, snap.mpW, snap.mpH, snap.excludeHwnd,
				snap.mpSrcX, snap.mpSrcY, snap.mpSrcW, snap.mpSrcH, forceGdi);
		}
	}
	if (snap.fxN > 0 && out.bits)
		ScGpuApplyEffectChain(out.bits, out.w, out.h, out.stride, snap.fx, snap.fxN, snap.fxTime, snap.fxStr);
	if (snap.showCursor && out.hdc)
		ScOverlayCursorComposeWindows(out.hdc, snap);
	return TRUE;
}

static void ScEnsureColorConvertMft()
{
	static LONG s_once = 0;
	if (InterlockedCompareExchange(&s_once, 1, 0) != 0)
		return;
	// SinkWriter が RGB→YUV / 形式変換 MFT を見つけられないと SetInput が 0xC00D36B4 になる
	MFTRegisterLocalByCLSID(
		__uuidof(CColorConvertDMO),
		MFT_CATEGORY_VIDEO_PROCESSOR,
		L"ColorConverter",
		MFT_ENUM_FLAG_SYNCMFT,
		0, NULL, 0, NULL);
#ifdef CLSID_VideoProcessorMFT
	MFTRegisterLocalByCLSID(
		CLSID_VideoProcessorMFT,
		MFT_CATEGORY_VIDEO_PROCESSOR,
		L"VideoProcessor",
		MFT_ENUM_FLAG_SYNCMFT,
		0, NULL, 0, NULL);
#endif
}

static HRESULT ScCreateSinkWriter(LPCWSTR path, BOOL enableHw, IMFSinkWriter** outWriter)
{
	if (!outWriter) return E_POINTER;
	*outWriter = NULL;
	ScEnsureColorConvertMft();

	auto tryAttrs = [&](BOOL hw) -> HRESULT {
		IMFAttributes* attrs = NULL;
		HRESULT h = E_FAIL;
		if (FAILED(MFCreateAttributes(&attrs, 3)) || !attrs)
			return E_FAIL;
#ifdef MF_TRANSCODE_CONTAINERTYPE
		attrs->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);
#endif
		if (hw)
			attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
		attrs->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
		h = MFCreateSinkWriterFromURL(path, NULL, attrs, outWriter);
		attrs->Release();
		return h;
	};

	HRESULT h = E_FAIL;
	if (enableHw) {
		h = tryAttrs(TRUE);
		if (SUCCEEDED(h) && *outWriter) return h;
	}
	// MSチュートリアル相当（属性なし）
	h = MFCreateSinkWriterFromURL(path, NULL, NULL, outWriter);
	if (SUCCEEDED(h) && *outWriter) return h;
	h = tryAttrs(FALSE);
	if (SUCCEEDED(h) && *outWriter) return h;
	if (!enableHw) {
		h = tryAttrs(TRUE);
		if (SUCCEEDED(h) && *outWriter) return h;
	}
	return FAILED(h) ? h : E_FAIL;
}

// attempt ごとに「1回だけ」AddStream→SetInput。失敗時は呼び出し側で writer を作り直すこと。
// RGB32 を優先（CPU の NV12 変換は 1080p で数ms〜十数ms食う）
// 0: H264+RGB32 公式
// 1: H264 Main+RGB32
// 2: H264 Base+RGB32
// 3: H264+NV12
// 4: H264 Base+NV12
// outStep: 1=AddStream 2=SetInput
static HRESULT ScAddVideoStream(IMFSinkWriter* writer, DWORD* outIdx, int w, int h, int fps,
	BOOL* outNv12, BOOL* outRgbBottomUp, int* outStep, int attempt)
{
	if (outNv12) *outNv12 = FALSE;
	if (outRgbBottomUp) *outRgbBottomUp = FALSE;
	if (outStep) *outStep = 0;
	if (!writer) return E_POINTER;
	w &= ~1; h &= ~1;
	if (w < 16) w = 16;
	if (h < 16) h = 16;
	if (w > 4096) w = 4096;
	if (h > 2304) h = 2304;
	if (fps < 5) fps = 5;
	if (fps > 120) fps = 120;
	if (attempt < 0) attempt = 0;

	// 画質寄りビットレート（1080p30≈15Mbps、1080p60≈31Mbps、1080p120≈50Mbps上限）
	UINT32 br = (UINT32)(((__int64)w * h * fps) / 4);
	if (br < 5000000u) br = 5000000u;
	if (br > 60000000u) br = 60000000u;

	UINT32 profile = 0;
	BOOL useNv12 = FALSE;
	BOOL useStride = FALSE;
	switch (attempt) {
	case 0: useNv12 = FALSE; useStride = FALSE; profile = (UINT32)eAVEncH264VProfile_High; break;
	case 1: useNv12 = FALSE; useStride = FALSE; profile = (UINT32)eAVEncH264VProfile_Main; break;
	case 2: useNv12 = FALSE; useStride = FALSE; profile = 0; break;
	case 3: useNv12 = FALSE; useStride = FALSE; profile = (UINT32)eAVEncH264VProfile_Base; break;
	case 4: useNv12 = TRUE;  useStride = TRUE;  profile = (UINT32)eAVEncH264VProfile_Main; break;
	default: useNv12 = FALSE; useStride = FALSE; profile = (UINT32)eAVEncH264VProfile_High; break;
	}

	IMFMediaType* outType = NULL;
	HRESULT hr = MFCreateMediaType(&outType);
	if (FAILED(hr)) return hr;
	hr = outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	if (SUCCEEDED(hr)) hr = outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
	if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AVG_BITRATE, br);
	if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	if (SUCCEEDED(hr)) hr = MFSetAttributeSize(outType, MF_MT_FRAME_SIZE, (UINT32)w, (UINT32)h);
	if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(outType, MF_MT_FRAME_RATE, (UINT32)fps, 1);
	if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(outType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
	if (SUCCEEDED(hr) && profile != 0)
		hr = outType->SetUINT32(MF_MT_MPEG2_PROFILE, profile);
	// 高FPS / 高解像度向けレベル
	if (SUCCEEDED(hr)) {
		UINT32 level = (UINT32)eAVEncH264VLevel4;
		if (fps > 60 || ((__int64)w * h > (__int64)1920 * 1080))
			level = (UINT32)eAVEncH264VLevel5_1;
		else if (fps > 30 || ((__int64)w * h > (__int64)1280 * 720))
			level = (UINT32)eAVEncH264VLevel4_2;
		hr = outType->SetUINT32(MF_MT_MPEG2_LEVEL, level);
	}
	DWORD idx = 0;
	if (SUCCEEDED(hr)) hr = writer->AddStream(outType, &idx);
	if (outType) outType->Release();
	if (FAILED(hr)) {
		if (outStep) *outStep = 1;
		return hr;
	}

	IMFMediaType* inType = NULL;
	hr = MFCreateMediaType(&inType);
	if (SUCCEEDED(hr)) hr = inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	if (SUCCEEDED(hr)) hr = inType->SetGUID(MF_MT_SUBTYPE, useNv12 ? MFVideoFormat_NV12 : MFVideoFormat_RGB32);
	if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	if (SUCCEEDED(hr)) hr = MFSetAttributeSize(inType, MF_MT_FRAME_SIZE, (UINT32)w, (UINT32)h);
	if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(inType, MF_MT_FRAME_RATE, (UINT32)fps, 1);
	if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(inType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
	if (SUCCEEDED(hr) && useStride)
		hr = inType->SetUINT32(MF_MT_DEFAULT_STRIDE, useNv12 ? (UINT32)w : (UINT32)(w * 4));

	IMFAttributes* encParams = NULL;
	if (SUCCEEDED(hr) && SUCCEEDED(MFCreateAttributes(&encParams, 4)) && encParams) {
		// ゲーム録画など高FPSは低遅延より画質優先
		encParams->SetUINT32(CODECAPI_AVLowLatencyMode, (fps >= 60) ? FALSE : TRUE);
		encParams->SetUINT32(CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_Quality);
		encParams->SetUINT32(CODECAPI_AVEncCommonQuality, (fps >= 60) ? 92 : ((fps >= 30) ? 90 : 88));
#ifdef CODECAPI_AVEncMPVGOPSize
		encParams->SetUINT32(CODECAPI_AVEncMPVGOPSize, (UINT32)(fps > 0 ? fps : 30));
#endif
	}
	if (SUCCEEDED(hr))
		hr = writer->SetInputMediaType(idx, inType, encParams);
	if (FAILED(hr) && encParams)
		hr = writer->SetInputMediaType(idx, inType, NULL);
	if (encParams) encParams->Release();
	if (inType) inType->Release();
	if (FAILED(hr)) {
		if (outStep) *outStep = 2;
		return hr;
	}

	if (outNv12) *outNv12 = useNv12 ? TRUE : FALSE;
	if (outRgbBottomUp) *outRgbBottomUp = FALSE;
	if (outIdx) *outIdx = idx;
	if (outStep) *outStep = 0;
	return S_OK;
}

static BOOL ScScaleFrameTo(ScFrameBuf& frame, int maxW, int maxH)
{
	maxW &= ~1; maxH &= ~1;
	if (maxW < 16) maxW = 16;
	if (maxH < 16) maxH = 16;
	if (frame.w <= maxW && frame.h <= maxH) return TRUE;
	int nw = frame.w, nh = frame.h;
	const double sx = (double)maxW / (double)((nw > 0) ? nw : 1);
	const double sy = (double)maxH / (double)((nh > 0) ? nh : 1);
	const double s = (sx < sy) ? sx : sy;
	nw = ((int)(frame.w * s)) & ~1;
	nh = ((int)(frame.h * s)) & ~1;
	if (nw < 16) nw = 16;
	if (nh < 16) nh = 16;
	ScFrameBuf scaled = {};
	if (!ScFrameAlloc(scaled, nw, nh)) return FALSE;
	SetStretchBltMode(scaled.hdc, COLORONCOLOR);
	StretchBlt(scaled.hdc, 0, 0, nw, nh, frame.hdc, 0, 0, frame.w, frame.h, SRCCOPY);
	ScFrameFree(frame);
	frame = scaled;
	return TRUE;
}

// BGRA top-down → NV12 (BT.709 limited)。1080p 向けに分岐少なめ。
static void ScBgraToNv12(const BYTE* bgra, int w, int h, int bgraStride, BYTE* nv12)
{
	BYTE* yPlane = nv12;
	BYTE* uvPlane = nv12 + (size_t)w * (size_t)h;
	for (int y = 0; y < h; ++y) {
		const BYTE* row = bgra + (size_t)y * (size_t)bgraStride;
		BYTE* yRow = yPlane + (size_t)y * (size_t)w;
		int x = 0;
		for (; x + 1 < w; x += 2) {
			const BYTE* p0 = row + (size_t)x * 4;
			const BYTE* p1 = p0 + 4;
			int Y0 = ((47 * p0[2] + 157 * p0[1] + 16 * p0[0] + 128) >> 8) + 16;
			int Y1 = ((47 * p1[2] + 157 * p1[1] + 16 * p1[0] + 128) >> 8) + 16;
			if ((unsigned)Y0 > 235u) Y0 = (Y0 < 16) ? 16 : 235;
			if ((unsigned)Y1 > 235u) Y1 = (Y1 < 16) ? 16 : 235;
			yRow[x] = (BYTE)Y0;
			yRow[x + 1] = (BYTE)Y1;
		}
		if (x < w) {
			const BYTE* p = row + (size_t)x * 4;
			int Y = ((47 * p[2] + 157 * p[1] + 16 * p[0] + 128) >> 8) + 16;
			if ((unsigned)Y > 235u) Y = (Y < 16) ? 16 : 235;
			yRow[x] = (BYTE)Y;
		}
	}
	for (int y = 0; y < h; y += 2) {
		const BYTE* row0 = bgra + (size_t)y * (size_t)bgraStride;
		const BYTE* row1 = bgra + (size_t)(y + 1) * (size_t)bgraStride;
		BYTE* uvRow = uvPlane + (size_t)(y / 2) * (size_t)w;
		for (int x = 0; x < w; x += 2) {
			const BYTE* p00 = row0 + (size_t)x * 4;
			const BYTE* p01 = p00 + 4;
			const BYTE* p10 = row1 + (size_t)x * 4;
			const BYTE* p11 = p10 + 4;
			const int B = (p00[0] + p01[0] + p10[0] + p11[0]) >> 2;
			const int G = (p00[1] + p01[1] + p10[1] + p11[1]) >> 2;
			const int R = (p00[2] + p01[2] + p10[2] + p11[2]) >> 2;
			int U = ((-26 * R - 87 * G + 112 * B + 128) >> 8) + 128;
			int V = ((112 * R - 102 * G - 10 * B + 128) >> 8) + 128;
			if ((unsigned)U > 240u) U = (U < 16) ? 16 : 240;
			if ((unsigned)V > 240u) V = (V < 16) ? 16 : 240;
			uvRow[x] = (BYTE)U;
			uvRow[x + 1] = (BYTE)V;
		}
	}
}

static thread_local std::vector<BYTE> s_scNv12Scratch;
static thread_local IMFMediaBuffer* s_scReuseBuf = NULL;
static thread_local DWORD s_scReuseBufCb = 0;

static void ScReleaseReuseBuf()
{
	if (s_scReuseBuf) {
		s_scReuseBuf->Release();
		s_scReuseBuf = NULL;
		s_scReuseBufCb = 0;
	}
}

static HRESULT ScWriteVideoSample(IMFSinkWriter* writer, DWORD stream, const ScFrameBuf& fb,
	LONGLONG rt, LONGLONG dur, BOOL asNv12, BOOL rgbBottomUp)
{
	DWORD cb = 0;
	const BYTE* srcPtr = NULL;
	std::vector<BYTE> flipScratch;
	if (asNv12) {
		cb = (DWORD)((size_t)fb.w * (size_t)fb.h * 3 / 2);
		s_scNv12Scratch.resize(cb);
		ScBgraToNv12(fb.bits, fb.w, fb.h, fb.stride, s_scNv12Scratch.data());
		srcPtr = s_scNv12Scratch.data();
	} else if (rgbBottomUp) {
		const int rowBytes = fb.w * 4;
		cb = (DWORD)((size_t)rowBytes * (size_t)fb.h);
		flipScratch.resize(cb);
		for (int y = 0; y < fb.h; ++y) {
			memcpy(flipScratch.data() + (size_t)(fb.h - 1 - y) * (size_t)rowBytes,
				fb.bits + (size_t)y * (size_t)fb.stride, (size_t)rowBytes);
		}
		srcPtr = flipScratch.data();
	} else {
		cb = (DWORD)(fb.stride * fb.h);
		srcPtr = fb.bits;
	}
	if (!srcPtr || cb == 0) return E_FAIL;

	IMFSample* sample = NULL;
	HRESULT hr = MFCreateSample(&sample);
	if (FAILED(hr)) return hr;

	IMFMediaBuffer* buffer = NULL;
	if (s_scReuseBuf && s_scReuseBufCb >= cb) {
		buffer = s_scReuseBuf;
		s_scReuseBuf = NULL;
		s_scReuseBufCb = 0;
	} else {
		ScReleaseReuseBuf();
		hr = MFCreateMemoryBuffer(cb, &buffer);
		if (FAILED(hr)) { sample->Release(); return hr; }
	}

	BYTE* p = NULL;
	hr = buffer->Lock(&p, NULL, NULL);
	if (SUCCEEDED(hr)) {
		memcpy(p, srcPtr, cb);
		buffer->Unlock();
		buffer->SetCurrentLength(cb);
		sample->AddBuffer(buffer);
		sample->SetSampleTime(rt);
		sample->SetSampleDuration(dur);
		hr = writer->WriteSample(stream, sample);
	}
	// バッファを再利用（次フレームの alloc を避ける）
	if (SUCCEEDED(hr) || hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
		ScReleaseReuseBuf();
		s_scReuseBuf = buffer;
		s_scReuseBufCb = cb;
		buffer = NULL;
	}
	if (buffer) buffer->Release();
	sample->Release();
	return hr;
}

static HRESULT ScRebuildVideoOnlyWriter(LPCWSTR path, int w, int h, int fps,
	IMFSinkWriter** writer, DWORD* videoIdx, BOOL* outNv12, BOOL* outRgbBottomUp,
	BOOL enableHw, int* outStep, int attempt)
{
	if (!writer) return E_POINTER;
	if (*writer) { (*writer)->Release(); *writer = NULL; }
	::DeleteFile(path);
	if (videoIdx) *videoIdx = 0;
	if (outNv12) *outNv12 = FALSE;
	if (outRgbBottomUp) *outRgbBottomUp = FALSE;
	HRESULT hh = ScCreateSinkWriter(path, enableHw, writer);
	if (FAILED(hh) || !*writer) return FAILED(hh) ? hh : E_FAIL;
	return ScAddVideoStream(*writer, videoIdx, w, h, fps, outNv12, outRgbBottomUp, outStep, attempt);
}

static HRESULT ScAddAudioStream(IMFSinkWriter* writer, DWORD* outIdx, UINT32 hz, UINT32 ch)
{
	if (!writer) return E_POINTER;
	if (hz != 44100 && hz != 48000) hz = 48000;
	if (ch < 1) ch = 1;
	if (ch > 2) ch = 2;

	IMFMediaType* outType = NULL;
	IMFMediaType* inType = NULL;
	HRESULT hr = MFCreateMediaType(&outType);
	if (FAILED(hr)) return hr;
	hr = outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	if (SUCCEEDED(hr)) hr = outType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
	if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
	if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, hz);
	if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, ch);
	// AAC: 192kbps (以前通っていた値)
	if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 192000 / 8);
	DWORD idx = 0;
	if (SUCCEEDED(hr)) hr = writer->AddStream(outType, &idx);
	if (FAILED(hr)) {
		if (outType) outType->Release();
		return hr;
	}
	hr = MFCreateMediaType(&inType);
	if (SUCCEEDED(hr)) hr = inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	if (SUCCEEDED(hr)) hr = inType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
	if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
	if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, hz);
	if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, ch);
	if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, ch * 2);
	if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, hz * ch * 2);
	// MF_MT_ALL_SAMPLES_INDEPENDENT は AAC 変換経路で SetInput 拒否の原因になることがあるので付けない
	if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(idx, inType, NULL);
	if (outType) outType->Release();
	if (inType) inType->Release();
	// AddStream 後に SetInput 失敗すると orphan stream が残る → 呼び出し側で writer 作り直しが必要
	if (SUCCEEDED(hr) && outIdx) *outIdx = idx;
	return hr;
}

static HRESULT ScWriteAudioSample(IMFSinkWriter* writer, DWORD stream, const short* pcm, int frames, int ch, int hz, LONGLONG rt)
{
	if (!pcm || frames <= 0) return S_OK;
	const DWORD cb = (DWORD)(frames * ch * 2);
	IMFSample* sample = NULL;
	IMFMediaBuffer* buffer = NULL;
	HRESULT hr = MFCreateSample(&sample);
	if (FAILED(hr)) return hr;
	hr = MFCreateMemoryBuffer(cb, &buffer);
	if (FAILED(hr)) { sample->Release(); return hr; }
	BYTE* p = NULL;
	hr = buffer->Lock(&p, NULL, NULL);
	if (SUCCEEDED(hr)) {
		memcpy(p, pcm, cb);
		buffer->Unlock();
		buffer->SetCurrentLength(cb);
		sample->AddBuffer(buffer);
		sample->SetSampleTime(rt);
		sample->SetSampleDuration((10000000LL * frames) / hz);
		hr = writer->WriteSample(stream, sample);
	}
	buffer->Release();
	sample->Release();
	return hr;
}

struct ScEnumCtx {
	HWND skip;
	HWND* hwnds;
	int* cnt;
	int maxCnt;
};

static BOOL CALLBACK ScEnumWindowsProc(HWND hwnd, LPARAM lp)
{
	ScEnumCtx* ctx = (ScEnumCtx*)lp;
	if (!ctx || !ctx->hwnds || !ctx->cnt) return FALSE;
	if (*ctx->cnt >= ctx->maxCnt) return FALSE;
	if (!IsWindowVisible(hwnd)) return TRUE;
	if (hwnd == ctx->skip) return TRUE;
	{
		const HWND liveHwnd = GetScLiveSettingsHwnd();
		if (liveHwnd && hwnd == liveHwnd)
			return TRUE;
	}
	if (GetWindow(hwnd, GW_OWNER)) return TRUE;
	const LONG_PTR ex = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
	if (ex & WS_EX_TOOLWINDOW) return TRUE;
	wchar_t title[256] = {};
	GetWindowTextW(hwnd, title, 255);
	if (!title[0]) return TRUE;
	ctx->hwnds[*ctx->cnt] = hwnd;
	(*ctx->cnt)++;
	return TRUE;
}

} // namespace

// ---- preview control (XSplit-like interact) ----

IMPLEMENT_DYNAMIC(CScPreviewCtrl, CStatic)

CScPreviewCtrl::CScPreviewCtrl()
	: m_owner(NULL)
	, m_bAeroMode(FALSE)
{
}

CScPreviewCtrl::~CScPreviewCtrl()
{
}

BEGIN_MESSAGE_MAP(CScPreviewCtrl, CStatic)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_RBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_SETCURSOR()
	ON_WM_CAPTURECHANGED()
END_MESSAGE_MAP()

static BOOL ScPreviewInteractive(CScreenCaptureDlg* o)
{
	if (!o || o->m_uiLocked || o->m_picking) return FALSE;
	if (o->IsWindowComposeMode()) return TRUE;
	if (o->m_includeMp.GetSafeHwnd() && o->m_includeMp.GetCheck() && o->m_layerCnt > 0) return TRUE;
	return FALSE;
}

void CScPreviewCtrl::PaintToDC(CDC& dc)
{
	CRect rc;
	GetClientRect(&rc);
	if (m_owner)
		m_owner->PaintPreview(dc, rc);
	else
		dc.FillSolidRect(&rc, RGB(20, 20, 24));
}

void CScPreviewCtrl::OnPaint()
{
	CPaintDC dc(this);
	// アクリル OpaqueFixer 配下ではここは来ないことが多い。念のため両対応。
	CRect r;
	GetClientRect(&r);
	if (r.Width() > 0 && r.Height() > 0) {
		BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
		params.dwFlags = BPPF_ERASE;
		HDC hdcBuf = NULL;
		HPAINTBUFFER hBP = ::BeginBufferedPaint(dc.GetSafeHdc(), &r, BPBF_TOPDOWNDIB, &params, &hdcBuf);
		if (hdcBuf && hBP) {
			CDC dcBuf;
			dcBuf.Attach(hdcBuf);
			PaintToDC(dcBuf);
			dcBuf.Detach();
			::BufferedPaintMakeOpaque(hBP, &r);
			::EndBufferedPaint(hBP, TRUE);
			return;
		}
	}
	PaintToDC(dc);
}

LRESULT CScPreviewCtrl::OnPrintClient(WPARAM wParam, LPARAM)
{
	// CCustomOpaqueFixer が WM_PAINT を奪い、ここ経由で描画する
	if (HDC hdc = (HDC)wParam) {
		CRect r;
		GetClientRect(&r);
		BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
		params.dwFlags = BPPF_ERASE;
		HDC hdcBuf = NULL;
		HPAINTBUFFER hBP = ::BeginBufferedPaint(hdc, &r, BPBF_TOPDOWNDIB, &params, &hdcBuf);
		if (hdcBuf && hBP) {
			CDC dcBuf;
			dcBuf.Attach(hdcBuf);
			PaintToDC(dcBuf);
			dcBuf.Detach();
			::BufferedPaintMakeOpaque(hBP, &r);
			::EndBufferedPaint(hBP, TRUE);
			return 0;
		}
		CDC dc;
		dc.Attach(hdc);
		PaintToDC(dc);
		dc.Detach();
	}
	return 0;
}

BOOL CScPreviewCtrl::OnEraseBkgnd(CDC*)
{
	return TRUE;
}

void CScPreviewCtrl::OnLButtonDown(UINT nFlags, CPoint point)
{
	if (m_owner && ScPreviewInteractive(m_owner)) {
		int handle = CScreenCaptureDlg::SC_HIT_NONE;
		const int layer = m_owner->HitTestPreview(point, &handle);
		if (layer >= 0) {
			m_owner->m_layer.SetCurSel(layer);
			m_owner->SyncGeoEditsFromSel();
			m_owner->BeginPreviewDrag(layer, handle, point);
			SetCapture();
			Invalidate(FALSE);
			return;
		}
	}
	CStatic::OnLButtonDown(nFlags, point);
}

void CScPreviewCtrl::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_owner && m_owner->m_dragging) {
		m_owner->EndPreviewDrag();
		if (GetCapture() == this)
			ReleaseCapture();
		Invalidate(FALSE);
		return;
	}
	CStatic::OnLButtonUp(nFlags, point);
}

void CScPreviewCtrl::OnRButtonUp(UINT nFlags, CPoint point)
{
	if (m_owner && !m_owner->m_uiLocked && !m_owner->m_picking && ScPreviewInteractive(m_owner)) {
		int handle = CScreenCaptureDlg::SC_HIT_NONE;
		int layer = m_owner->HitTestPreview(point, &handle);
		if (layer < 0) {
			const int sel = m_owner->m_layer.GetCurSel();
			if (sel >= 0 && sel < m_owner->m_layerCnt)
				layer = sel;
		}

		auto addLiveToggle = [&](CCustomPopupMenu& menu) {
			const BOOL liveOn = m_owner->m_live.GetSafeHwnd() && m_owner->m_live.GetCheck();
			menu.AddCheck(ID_SC_LIVE_TOGGLE,
				LL14(L"ライブ配信モード", L"Live stream mode", L"Mode diffusion live", L"Modalità diretta",
					L"Modo transmisión", L"라이브 방송 모드", L"直播模式", L"وضع البث المباشر",
					L"Режим эфира", L"Livestream-Modus", L"Modo ao vivo", L"Livestream-modus",
					L"Tryb transmisji", L"Canlı yayın modu"),
				liveOn,
				LL14(L"ONでRTMP配信（MP4なし）。ffmpeg.exe が必要です",
					L"ON = RTMP live (no MP4). Requires ffmpeg.exe",
					L"ON = live RTMP (pas de MP4). ffmpeg.exe requis",
					L"ON = live RTMP (niente MP4). Serve ffmpeg.exe",
					L"ON = vivo RTMP (sin MP4). Requiere ffmpeg.exe",
					L"ON이면 RTMP 라이브(MP4 없음). ffmpeg.exe 필요",
					L"开启后 RTMP 直播（不写 MP4）。需要 ffmpeg.exe",
					L"تشغيل = بث RTMP (بدون MP4). يلزم ffmpeg.exe",
					L"Вкл. = RTMP эфир (без MP4). Нужен ffmpeg.exe",
					L"AN = RTMP-Live (kein MP4). ffmpeg.exe nötig",
					L"ON = ao vivo RTMP (sem MP4). Requer ffmpeg.exe",
					L"AAN = RTMP-live (geen MP4). ffmpeg.exe vereist",
					L"WŁ = live RTMP (bez MP4). Wymaga ffmpeg.exe",
					L"AÇIK = RTMP canlı (MP4 yok). ffmpeg.exe gerekir"));
		};

		if (layer >= 0 && layer < m_owner->m_layerCnt) {
			m_owner->m_layer.SetCurSel(layer);
			m_owner->SyncGeoEditsFromSel();
			const BOOL hidden = m_owner->m_layers[layer].hidden;
			CCustomPopupMenu menu;
			addLiveToggle(menu);
			menu.AddSeparator();
			menu.AddCommand(ID_SC_LAYER_HIDE,
				hidden
				? LL14(L"表示する", L"Show", L"Afficher", L"Mostra", L"Mostrar", L"표시", L"显示", L"إظهار",
					L"Показать", L"Einblenden", L"Mostrar", L"Tonen", L"Pokaż", L"Göster")
				: LL14(L"非表示にする", L"Hide", L"Masquer", L"Nascondi", L"Ocultar", L"숨기기", L"隐藏", L"إخفاء",
					L"Скрыть", L"Ausblenden", L"Ocultar", L"Verbergen", L"Ukryj", L"Gizle"),
				hidden
				? LL14(L"このレイヤの映像を再表示（音はそのまま）", L"Show this layer's video again (audio unchanged)",
					L"Reafficher la video de ce calque (audio inchange)", L"Mostra di nuovo il video del livello (audio invariato)",
					L"Volver a mostrar el video de esta capa (audio igual)", L"이 레이어 영상을 다시 표시(소리는 그대로)",
					L"重新显示此层画面（音频不变）", L"إظهار فيديو هذه الطبقة مجدداً (الصوت كما هو)",
					L"Снова показать видео слоя (звук без изменений)", L"Video dieser Ebene wieder einblenden (Audio gleich)",
					L"Mostrar de novo o video desta camada (audio igual)", L"Video van deze laag weer tonen (audio ongewijzigd)",
					L"Pokaz ponownie wideo warstwy (audio bez zmian)", L"Bu katmanin videosunu tekrar goster (ses ayni)")
				: LL14(L"このレイヤの映像を隠す（音だけ載せたいときに）", L"Hide this layer's video (keep audio if needed)",
					L"Masquer la video de ce calque (garder l'audio)", L"Nascondi il video del livello (tieni l'audio)",
					L"Ocultar el video de esta capa (mantener audio)", L"이 레이어 영상을 숨김(소리만 남길 때)",
					L"隐藏此层画面（需要时可保留音频）", L"إخفاء فيديو هذه الطبقة (مع الإبقاء على الصوت)",
					L"Скрыть видео слоя (звук можно оставить)", L"Video dieser Ebene ausblenden (Audio behalten)",
					L"Ocultar o video desta camada (manter audio)", L"Video van deze laag verbergen (audio behouden)",
					L"Ukryj wideo warstwy (audio mozna zostawic)", L"Bu katmanin videosunu gizle (sesi tut)"));
			menu.AddSeparator();
			menu.AddCommand(ID_SC_LAYER_FIT,
				LL14(L"キャンバスにフィット", L"Fit to canvas", L"Ajuster au canevas", L"Adatta al canvas",
					L"Ajustar al lienzo", L"캔버스에 맞춤", L"铺满画布", L"ملاءمة اللوحة",
					L"Вписать в холст", L"In Fläche einpassen", L"Ajustar à tela", L"Passen op canvas",
					L"Dopasuj do płótna", L"Tuvale sığdır"),
				LL14(L"選択レイヤをキャンバス全体に合わせる", L"Fit selected layer to the whole canvas",
					L"Ajuster le calque au canevas", L"Adatta il livello al canvas", L"Ajustar capa al lienzo",
					L"선택 레이어를 캔버스에 맞춤", L"将所选层铺满画布", L"ملاءمة الطبقة للوحة",
					L"Вписать слой в холст", L"Ebene an Fläche anpassen", L"Ajustar camada à tela",
					L"Laag op canvas passen", L"Dopasuj warstwę do płótna", L"Katmanı tuvale sığdır"));
			menu.AddCommand(ID_SC_LAYER_SCALE50,
				LL14(L"50% サイズ", L"50% size", L"Taille 50%", L"Dimensione 50%",
					L"Tamaño 50%", L"50% 크기", L"50% 大小", L"حجم 50٪",
					L"Размер 50%", L"50% Größe", L"Tamanho 50%", L"50% grootte",
					L"Rozmiar 50%", L"%50 boyut"),
				LL14(L"選択レイヤをキャンバスの約50%サイズに", L"Scale selected layer to about 50% of the canvas",
					L"Mettre le calque a environ 50% du canevas", L"Scala il livello a circa il 50% del canvas",
					L"Escalar la capa a unos 50% del lienzo", L"선택 레이어를 캔버스 약 50% 크기로",
					L"将所选层缩放到画布约 50%", L"تغيير حجم الطبقة إلى نحو 50٪ من اللوحة",
					L"Масштаб слоя примерно 50% холста", L"Ebene auf ca. 50% der Fläche skalieren",
					L"Dimensionar a camada a cerca de 50% da tela", L"Laag schalen naar ongeveer 50% van het canvas",
					L"Skaluj warstwe do ok. 50% plotna", L"Secili katmani tuvalin yaklasik %50 boyutuna"));
			menu.AddCommand(ID_SC_LAYER_SCALE100,
				LL14(L"実寸 (100%)", L"Actual size (100%)", L"Taille réelle (100%)", L"Dimensione reale (100%)",
					L"Tamaño real (100%)", L"실측 (100%)", L"实际大小 (100%)", L"الحجم الفعلي (100٪)",
					L"Реальный размер (100%)", L"Originalgröße (100%)", L"Tamanho real (100%)", L"Ware grootte (100%)",
					L"Rzeczywisty rozmiar (100%)", L"Gerçek boyut (%100)"),
				LL14(L"選択レイヤを実寸（100%）に戻す", L"Reset selected layer to actual size (100%)",
					L"Remettre le calque a la taille reelle (100%)", L"Ripristina il livello a dimensione reale (100%)",
					L"Restablecer la capa a tamano real (100%)", L"선택 레이어를 실측(100%)으로",
					L"将所选层恢复为实际大小（100%）", L"إعادة الطبقة إلى الحجم الفعلي (100٪)",
					L"Вернуть слой к реальному размеру (100%)", L"Ebene auf Originalgroesse (100%) setzen",
					L"Redefinir a camada para tamanho real (100%)", L"Laag terugzetten naar ware grootte (100%)",
					L"Przywroc warstwe do rzeczywistego rozmiaru (100%)", L"Secili katmani gercek boyuta (%100) al"));
			menu.AddSeparator();
			menu.AddCommand(ID_SC_LAYER_ZUP,
				LL14(L"手前へ (Z+)", L"Bring forward (Z+)", L"Vers l'avant (Z+)", L"Porta avanti (Z+)",
					L"Traer al frente (Z+)", L"앞으로 (Z+)", L"前移 (Z+)", L"تقديم (Z+)",
					L"Вперёд (Z+)", L"Nach vorne (Z+)", L"Para frente (Z+)", L"Naar voren (Z+)",
					L"Do przodu (Z+)", L"Öne getir (Z+)"),
				LL14(L"重ね順を1つ手前へ（Zオーダー＋）", L"Bring the layer one step forward (Z-order +)",
					L"Avancer d'un cran dans l'ordre Z", L"Porta avanti di un passo nell'ordine Z",
					L"Traer un paso al frente (orden Z +)", L"겹침 순서를 한 단계 앞으로(Z+)",
					L"将叠放顺序前移一层（Z+）", L"تقديم الطبقة خطوة في ترتيب Z",
					L"Сдвинуть слой на один шаг вперёд (Z+)", L"Ebene eine Stufe nach vorne (Z+)",
					L"Trazer a camada um passo a frente (Z+)", L"Laag een stap naar voren (Z+)",
					L"Przesun warstwe o jeden w przod (Z+)", L"Katmani bir adim one getir (Z+)"),
				layer > 0);
			menu.AddCommand(ID_SC_LAYER_ZDOWN,
				LL14(L"奥へ (Z-)", L"Send back (Z-)", L"Vers l'arrière (Z-)", L"Porta indietro (Z-)",
					L"Enviar atrás (Z-)", L"뒤로 (Z-)", L"后移 (Z-)", L"تأخير (Z-)",
					L"Назад (Z-)", L"Nach hinten (Z-)", L"Para trás (Z-)", L"Naar achteren (Z-)",
					L"Do tyłu (Z-)", L"Geriye gönder (Z-)"),
				LL14(L"重ね順を1つ奥へ（Zオーダー−）", L"Send the layer one step back (Z-order −)",
					L"Reculer d'un cran dans l'ordre Z", L"Porta indietro di un passo nell'ordine Z",
					L"Enviar un paso atras (orden Z −)", L"겹침 순서를 한 단계 뒤로(Z−)",
					L"将叠放顺序后移一层（Z−）", L"تأخير الطبقة خطوة في ترتيب Z",
					L"Сдвинуть слой на один шаг назад (Z−)", L"Ebene eine Stufe nach hinten (Z−)",
					L"Enviar a camada um passo para tras (Z−)", L"Laag een stap naar achteren (Z−)",
					L"Przesun warstwe o jeden w tyl (Z−)", L"Katmani bir adim geriye gonder (Z−)"),
				layer < m_owner->m_layerCnt - 1);
			menu.AddSeparator();
			menu.AddCommand(ID_SC_LAYER_CROP_FULL,
				LL14(L"切出を解除", L"Clear crop", L"Annuler le rognage", L"Annulla ritaglio",
					L"Quitar recorte", L"잘라내기 해제", L"清除裁剪", L"إلغاء القص",
					L"Сбросить вырез", L"Ausschnitt aufheben", L"Limpar recorte", L"Uitsnede wissen",
					L"Wyczyść wycinek", L"Kırpmayı temizle"),
				LL14(L"選択レイヤの切出（クロップ）を解除し全体を表示", L"Clear crop on the selected layer and show the full window",
					L"Annuler le rognage et afficher toute la fenetre", L"Annulla il ritaglio e mostra tutta la finestra",
					L"Quitar el recorte y mostrar toda la ventana", L"선택 레이어의 잘라내기를 해제하고 전체 표시",
					L"清除所选层的裁剪并显示整个窗口", L"إلغاء القص وعرض النافذة كاملة",
					L"Сбросить вырез и показать всё окно", L"Ausschnitt aufheben und ganzes Fenster zeigen",
					L"Limpar o recorte e mostrar a janela inteira", L"Uitsnede wissen en hele venster tonen",
					L"Wyczysc wycinek i pokaz cale okno", L"Kirpmayi temizle ve tum pencereyi goster"));
			menu.AddSeparator();
			menu.AddCommand(ID_SC_LAYER_TILE,
				LL14(L"レイヤを整列", L"Tile layers", L"Aligner les calques", L"Allinea livelli",
					L"Alinear capas", L"레이어 정렬", L"排列图层", L"ترتيب الطبقات",
					L"Упорядочить слои", L"Ebenen anordnen", L"Alinhar camadas", L"Lagen tegelen",
					L"Uloz warstwy", L"Katmanlari diz"),
				LL14(L"全レイヤをキャンバス内に並べます。", L"Arrange all layers on the canvas.", L"Disposer tous les calques sur le canevas.", L"Disponi tutti i livelli sul canvas.", L"Disponer todas las capas en el lienzo.",
					L"모든 레이어를 캔버스에 배치합니다.", L"将所有层排列到画布上。", L"ترتيب كل الطبقات على اللوحة.", L"Разместить все слои на холсте.", L"Alle Ebenen auf der Flache anordnen.",
					L"Organizar todas as camadas na tela.", L"Alle lagen op het canvas schikken.", L"Uloz wszystkie warstwy na plotnie.", L"Tum katmanlari tuvalde diz."));
			menu.AddCommand(ID_SC_LAYER_REMOVE,
				LL14(L"レイヤを削除", L"Remove layer", L"Retirer le calque", L"Rimuovi livello",
					L"Quitar capa", L"레이어 삭제", L"删除层", L"إزالة الطبقة",
					L"Удалить слой", L"Ebene entfernen", L"Remover camada", L"Laag verwijderen",
					L"Usuń warstwę", L"Katmanı kaldır"),
				LL14(L"このレイヤをリストから削除します", L"Remove this layer from the list",
					L"Retirer ce calque de la liste", L"Rimuovi questo livello dall'elenco",
					L"Quitar esta capa de la lista", L"이 레이어를 목록에서 삭제",
					L"从此列表中删除该层", L"إزالة هذه الطبقة من القائمة",
					L"Удалить этот слой из списка", L"Diese Ebene aus der Liste entfernen",
					L"Remover esta camada da lista", L"Deze laag uit de lijst verwijderen",
					L"Usun te warstwe z listy", L"Bu katmani listeden kaldir"));
			CPoint sp = point;
			ClientToScreen(&sp);
			const UINT cmd = menu.Track(sp, m_owner);
			if (cmd == ID_SC_LIVE_TOGGLE) {
				if (m_owner->m_live.GetSafeHwnd()) {
					m_owner->m_live.SetCheck(m_owner->m_live.GetCheck() ? BST_UNCHECKED : BST_CHECKED);
					m_owner->OnBnClickedLive();
				}
			} else if (cmd == ID_SC_LAYER_HIDE) {
				m_owner->ToggleLayerHidden(layer);
			} else if (cmd == ID_SC_LAYER_FIT) {
				m_owner->m_layer.SetCurSel(layer);
				m_owner->FitSelected(0);
			} else if (cmd == ID_SC_LAYER_SCALE50) {
				m_owner->m_layer.SetCurSel(layer);
				m_owner->FitSelected(50);
			} else if (cmd == ID_SC_LAYER_SCALE100) {
				m_owner->m_layer.SetCurSel(layer);
				m_owner->FitSelected(100);
			} else if (cmd == ID_SC_LAYER_ZUP && layer > 0) {
				m_owner->m_layer.SetCurSel(layer);
				m_owner->OnBnClickedZUp();
			} else if (cmd == ID_SC_LAYER_ZDOWN && layer < m_owner->m_layerCnt - 1) {
				m_owner->m_layer.SetCurSel(layer);
				m_owner->OnBnClickedZDown();
			} else if (cmd == ID_SC_LAYER_CROP_FULL) {
				m_owner->m_layer.SetCurSel(layer);
				m_owner->OnBnClickedCropFull();
			} else if (cmd == ID_SC_LAYER_TILE) {
				m_owner->TileLayers();
			} else if (cmd == ID_SC_LAYER_REMOVE) {
				m_owner->m_layer.SetCurSel(layer);
				m_owner->OnBnClickedRemove();
			}
			Invalidate(FALSE);
			return;
		}

		// レイヤ無しでも Live 切替メニューを出す
		{
			CCustomPopupMenu menu;
			addLiveToggle(menu);
			CPoint sp = point;
			ClientToScreen(&sp);
			const UINT cmd = menu.Track(sp, m_owner);
			if (cmd == ID_SC_LIVE_TOGGLE && m_owner->m_live.GetSafeHwnd()) {
				m_owner->m_live.SetCheck(m_owner->m_live.GetCheck() ? BST_UNCHECKED : BST_CHECKED);
				m_owner->OnBnClickedLive();
			}
			Invalidate(FALSE);
			return;
		}
	}
	CStatic::OnRButtonUp(nFlags, point);
}

void CScPreviewCtrl::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_owner) {
		if (m_owner->m_dragging) {
			m_owner->UpdatePreviewDrag(point);
			Invalidate(FALSE);
			return;
		}
		if (ScPreviewInteractive(m_owner)) {
			int handle = CScreenCaptureDlg::SC_HIT_NONE;
			const int layer = m_owner->HitTestPreview(point, &handle);
			if (layer != m_owner->m_hoverLayer || handle != m_owner->m_hoverHandle) {
				m_owner->m_hoverLayer = layer;
				m_owner->m_hoverHandle = handle;
				Invalidate(FALSE);
			}
		}
	}
	CStatic::OnMouseMove(nFlags, point);
}

BOOL CScPreviewCtrl::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	if (m_owner && ScPreviewInteractive(m_owner) && nHitTest == HTCLIENT) {
		CPoint pt;
		GetCursorPos(&pt);
		ScreenToClient(&pt);
		int handle = CScreenCaptureDlg::SC_HIT_NONE;
		m_owner->HitTestPreview(pt, &handle);
		LPCWSTR idc = IDC_ARROW;
		if (handle == CScreenCaptureDlg::SC_HIT_TL || handle == CScreenCaptureDlg::SC_HIT_BR)
			idc = IDC_SIZENWSE;
		else if (handle == CScreenCaptureDlg::SC_HIT_TR || handle == CScreenCaptureDlg::SC_HIT_BL)
			idc = IDC_SIZENESW;
		else if (handle == CScreenCaptureDlg::SC_HIT_BODY)
			idc = IDC_SIZEALL;
		::SetCursor(AfxGetApp()->LoadStandardCursor(idc));
		return TRUE;
	}
	return CStatic::OnSetCursor(pWnd, nHitTest, message);
}

void CScPreviewCtrl::OnCaptureChanged(CWnd* pWnd)
{
	if (m_owner && m_owner->m_dragging && pWnd != this)
		m_owner->EndPreviewDrag();
	CStatic::OnCaptureChanged(pWnd);
}

// ---- FX wire graph (linear chain, GDI) ----

IMPLEMENT_DYNAMIC(CScFxWireCtrl, CStatic)

CScFxWireCtrl::CScFxWireCtrl()
	: m_owner(NULL)
	, m_bAeroMode(FALSE)
	, m_slotN(0)
	, m_dragging(FALSE)
	, m_dragFx(0)
	, m_dragFromSlot(-1)
	, m_hoverFx(0)
	, m_hoverSlot(-1)
	, m_trackLeave(FALSE)
{
	memset(m_slots, 0, sizeof(m_slots));
	memset(m_str, SC_FX_STR_DEF, sizeof(m_str));
	m_dragPt = CPoint(0, 0);
}

CScFxWireCtrl::~CScFxWireCtrl()
{
}

BEGIN_MESSAGE_MAP(CScFxWireCtrl, CStatic)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_RBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
END_MESSAGE_MAP()

void CScFxWireCtrl::SetChain(const int* fx, int n, const BYTE str[][8])
{
	memset(m_slots, 0, sizeof(m_slots));
	memset(m_str, SC_FX_STR_DEF, sizeof(m_str));
	m_slotN = 0;
	if (!fx || n <= 0) {
		Invalidate(FALSE);
		return;
	}
	if (n > SC_FX_CHAIN_MAX) n = SC_FX_CHAIN_MAX;
	for (int i = 0; i < n; ++i) {
		if (fx[i] > SC_FX_NONE && fx[i] < SC_FX_COUNT) {
			m_slots[m_slotN] = fx[i];
			for (int s = 0; s < SC_FX_STR_N; ++s) {
				BYTE v = str ? str[i][s] : (BYTE)SC_FX_STR_DEF;
				if (v > SC_FX_STR_MAX) v = (BYTE)SC_FX_STR_MAX;
				m_str[m_slotN][s] = v;
			}
			m_slotN++;
		}
	}
	Invalidate(FALSE);
}

void CScFxWireCtrl::GetChain(int* fxOut, int* nOut, BYTE strOut[][8]) const
{
	if (nOut) *nOut = m_slotN;
	if (!fxOut && !strOut) return;
	for (int i = 0; i < SC_FX_CHAIN_MAX; ++i) {
		if (fxOut)
			fxOut[i] = (i < m_slotN) ? m_slots[i] : 0;
		if (strOut) {
			for (int s = 0; s < SC_FX_STR_N; ++s)
				strOut[i][s] = (i < m_slotN) ? m_str[i][s] : (BYTE)SC_FX_STR_DEF;
		}
	}
}

void CScFxWireCtrl::NotifyChanged()
{
	if (m_owner)
		m_owner->OnFxWireChanged();
}

void CScFxWireCtrl::UpdateHover(CPoint pt)
{
	if (m_dragging) {
		// ドロップ中はドロップ先スロットだけハイライト
		const int slot = HitSlot(pt);
		const int fx = 0;
		if (slot == m_hoverSlot && fx == m_hoverFx)
			return;
		m_hoverSlot = slot;
		m_hoverFx = fx;
		Invalidate(FALSE);
		return;
	}
	const int fx = HitPalette(pt);
	const int slot = (fx > 0) ? -1 : HitSlot(pt);
	if (fx == m_hoverFx && slot == m_hoverSlot)
		return;
	m_hoverFx = fx;
	m_hoverSlot = slot;
	Invalidate(FALSE);
}

struct ScFxWireMetrics {
	int titleH;
	int railTop;
	int slotW, slotH, gap, inW, outW, x0;
	int palCols, palRows, pw, ph, palX0, palY0, palGapX, palGapY;
};

static ScFxWireMetrics ScFxMakeMetrics(const CRect& rc)
{
	ScFxWireMetrics m = {};
	// 説明ラベル用の帯を確保（潰さない）
	m.titleH = 18;
	m.railTop = m.titleH + 2;
	m.slotH = 24;
	m.gap = 3;
	m.inW = 26;
	m.outW = 36;
	const int margin = 4;
	const int usable = rc.Width() - margin * 2 - m.inW - m.gap - m.outW;
	m.slotW = (usable - SC_FX_CHAIN_MAX * m.gap) / SC_FX_CHAIN_MAX;
	if (m.slotW < 52) m.slotW = 52;
	if (m.slotW > 92) m.slotW = 92;
	const int total = m.inW + m.gap + SC_FX_CHAIN_MAX * (m.slotW + m.gap) + m.outW;
	m.x0 = (rc.Width() - total) / 2;
	if (m.x0 < margin) m.x0 = margin;

	m.palX0 = 4;
	m.palGapX = 3;
	m.palGapY = 2;
	m.palY0 = m.railTop + m.slotH + 5;
	int availH = rc.Height() - m.palY0 - 3;
	int availW = rc.Width() - m.palX0 * 2;
	if (availH < 40) availH = 40;
	if (availW < 80) availW = 80;
	const int nFx = SC_FX_COUNT - 1;

	// 横6列を基本（約12行で縦を満杯・文字を大きく）。狭いときだけ減列。
	int cols = 6;
	if (availW < 6 * 58) cols = 5;
	if (availW < 5 * 58) cols = 4;
	if (cols < 4) cols = 4;

	int rows = (nFx + cols - 1) / cols;
	if (rows < 1) rows = 1;

	m.pw = (availW - (cols - 1) * m.palGapX) / cols;
	if (m.pw < 44) m.pw = 44;
	// 縦は端数込みで availH を使い切る。セルが読めない高さは拒否して列を増やす。
	m.ph = (availH - (rows - 1) * m.palGapY) / rows;
	if (m.ph < 12) {
		// 行を減らすため列を増やして再計算（最大 10 列）
		while (m.ph < 12 && cols < 10) {
			cols++;
			rows = (nFx + cols - 1) / cols;
			if (rows < 1) rows = 1;
			m.pw = (availW - (cols - 1) * m.palGapX) / cols;
			if (m.pw < 44) m.pw = 44;
			m.ph = (availH - (rows - 1) * m.palGapY) / rows;
		}
		if (m.ph < 12) m.ph = 12;
	}
	m.palCols = cols;
	m.palRows = rows;
	const int used = rows * m.ph + (rows - 1) * m.palGapY;
	if (used < availH && rows > 0)
		m.ph += (availH - used) / rows;
	return m;
}

CRect CScFxWireCtrl::SlotRect(int i) const
{
	CRect rc;
	GetClientRect(&rc);
	const ScFxWireMetrics m = ScFxMakeMetrics(rc);
	const int x = m.x0 + m.inW + m.gap + i * (m.slotW + m.gap);
	return CRect(x, m.railTop, x + m.slotW, m.railTop + m.slotH);
}

CRect CScFxWireCtrl::PaletteRect(int fx) const
{
	CRect rc;
	GetClientRect(&rc);
	const ScFxWireMetrics m = ScFxMakeMetrics(rc);
	const int idx = fx - 1;
	const int cols = (m.palCols > 0) ? m.palCols : 6;
	// 行優先: 左→右、上→下（6列×約12行）
	const int row = idx / cols;
	const int col = idx % cols;
	const int x = m.palX0 + col * (m.pw + m.palGapX);
	const int y = m.palY0 + row * (m.ph + m.palGapY);
	return CRect(x, y, x + m.pw, y + m.ph);
}

int CScFxWireCtrl::HitPalette(CPoint pt) const
{
	CRect rc;
	GetClientRect(&rc);
	for (int fx = 1; fx < SC_FX_COUNT; ++fx) {
		CRect pr = PaletteRect(fx);
		if (pr.Height() < 8 || pr.Width() < 8)
			continue;
		if (pr.top >= rc.bottom - 1 || pr.left >= rc.right - 1)
			continue;
		if (pr.PtInRect(pt))
			return fx;
	}
	return 0;
}

int CScFxWireCtrl::HitSlot(CPoint pt) const
{
	for (int i = 0; i < SC_FX_CHAIN_MAX; ++i) {
		if (SlotRect(i).PtInRect(pt))
			return i;
	}
	return -1;
}

void CScFxWireCtrl::PaintToDC(CDC& dc)
{
	CRect rc;
	GetClientRect(&rc);
	dc.FillSolidRect(&rc, RGB(22, 24, 30));
	dc.SetBkMode(TRANSPARENT);
	CFont* oldFont = dc.SelectObject(GetFont());
	const ScFxWireMetrics m = ScFxMakeMetrics(rc);

	CFont titleFont;
	CFont smallFont;
	LOGFONT lfBase = {};
	if (CFont* base = GetFont())
		base->GetLogFont(&lfBase);
	else {
		lfBase.lfHeight = -12;
		lfBase.lfCharSet = DEFAULT_CHARSET;
		_tcsncpy(lfBase.lfFaceName, _T("MS Shell Dlg"), LF_FACESIZE - 1);
		lfBase.lfFaceName[LF_FACESIZE - 1] = 0;
	}
	LOGFONT lfTitle = lfBase;
	lfTitle.lfHeight = -13;
	lfTitle.lfWeight = FW_NORMAL;
	titleFont.CreateFontIndirect(&lfTitle);

	LOGFONT lfPal = lfBase;
	// セル高さに合わせて読みやすいサイズへ（下限11、上限15）
	int want = m.ph - 6;
	if (want < 11) want = 11;
	if (want > 15) want = 15;
	lfPal.lfHeight = -want;
	smallFont.CreateFontIndirect(&lfPal);

	dc.SelectObject(&titleFont);
	dc.SetTextColor(RGB(180, 195, 220));
	CRect title(6, 2, rc.right - 6, m.titleH);
	dc.DrawText(LL14(
		L"FX配線 (パレット→スロット / 同一効果の並列可 / 右クリック)",
		L"FX wiring (palette→slot / duplicates OK / right-click)",
		L"Câblage FX (palette→slot / doublons OK / clic droit)",
		L"Cablaggio FX (palette→slot / duplicati OK / destro)",
		L"Cableado FX (paleta→ranura / duplicados OK / clic der.)",
		L"FX 배선 (팔레트→슬롯 / 중복 가능 / 우클릭)",
		L"效果连线（拖到插槽 / 可重复 / 右键）",
		L"توصيل FX (اسحب إلى فتحة / تكرار مسموح / يمين)",
		L"Схема FX (на слот / дубликаты OK / ПКМ)",
		L"FX-Verdrahtung (Palette→Slot / Duplikate OK / Rechtsklick)",
		L"Ligação FX (paleta→slot / duplicatas OK / direito)",
		L"FX-bedrading (palet→slot / duplicaten OK / rechtsklik)",
		L"Okablowanie FX (przeciągnij→slot / duplikaty OK / PPM)",
		L"FX kablolama (paletten slota / tekrar OK / sağ tık)"),
		&title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

	dc.SelectObject(&smallFont);

	auto drawNode = [&](CRect r, COLORREF fill, COLORREF border, const CString& text) {
		dc.FillSolidRect(&r, fill);
		dc.Draw3dRect(&r, border, RGB(20, 20, 24));
		dc.SetTextColor(RGB(230, 235, 245));
		CRect tr = r;
		tr.DeflateRect(2, 1);
		dc.DrawText(text, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
	};

	CRect inR(m.x0, m.railTop, m.x0 + m.inW, m.railTop + m.slotH);
	drawNode(inR, RGB(40, 70, 50), RGB(100, 200, 140), L"IN");
	int prevCx = inR.right;
	int prevCy = (inR.top + inR.bottom) / 2;

	for (int i = 0; i < SC_FX_CHAIN_MAX; ++i) {
		CRect sr = SlotRect(i);
		const BOOL filled = (i < m_slotN && m_slots[i] > 0);
		CString lab;
		if (filled && m_owner)
			lab = m_owner->FxName(m_slots[i]);
		else if (filled)
			lab.Format(L"%d", m_slots[i]);
		else
			lab.Format(L"S%d", i + 1);
		const BOOL hover = (i == m_hoverSlot);
		drawNode(sr,
			hover ? (filled ? RGB(80, 65, 28) : RGB(48, 62, 88))
			      : (filled ? RGB(55, 45, 20) : RGB(35, 38, 48)),
			hover ? RGB(120, 210, 255)
			      : (filled ? RGB(255, 200, 80) : RGB(90, 100, 120)),
			lab);

		CPen pen(PS_SOLID, 2, RGB(90, 160, 255));
		CPen* old = dc.SelectObject(&pen);
		dc.MoveTo(prevCx, prevCy);
		dc.LineTo(sr.left, (sr.top + sr.bottom) / 2);
		dc.SelectObject(old);
		dc.FillSolidRect(sr.left - 3, (sr.top + sr.bottom) / 2 - 3, 6, 6, RGB(200, 220, 255));
		dc.FillSolidRect(sr.right - 3, (sr.top + sr.bottom) / 2 - 3, 6, 6, RGB(200, 220, 255));
		prevCx = sr.right;
		prevCy = (sr.top + sr.bottom) / 2;
	}

	CRect outR(prevCx + m.gap, m.railTop, prevCx + m.gap + m.outW, m.railTop + m.slotH);
	{
		CPen pen(PS_SOLID, 2, RGB(90, 160, 255));
		CPen* old = dc.SelectObject(&pen);
		dc.MoveTo(prevCx, prevCy);
		dc.LineTo(outR.left, (outR.top + outR.bottom) / 2);
		dc.SelectObject(old);
	}
	drawNode(outR, RGB(70, 40, 40), RGB(255, 120, 120), L"OUT");

	for (int fx = 1; fx < SC_FX_COUNT; ++fx) {
		CRect pr = PaletteRect(fx);
		if (pr.left >= rc.right - 1 || pr.top >= rc.bottom - 1)
			continue;
		if (pr.right > rc.right - 1) pr.right = rc.right - 1;
		if (pr.bottom > rc.bottom - 1) pr.bottom = rc.bottom - 1;
		if (pr.Width() < 8 || pr.Height() < 8)
			continue;
		CString name = m_owner ? m_owner->FxName(fx) : L"?";
		const BOOL hover = (fx == m_hoverFx) || (m_dragging && fx == m_dragFx && m_dragFromSlot < 0);
		dc.FillSolidRect(&pr, hover ? RGB(55, 85, 130) : RGB(32, 40, 58));
		dc.Draw3dRect(&pr,
			hover ? RGB(160, 220, 255) : RGB(100, 140, 200),
			hover ? RGB(40, 70, 110) : RGB(20, 24, 32));
		dc.SetTextColor(hover ? RGB(255, 250, 220) : RGB(220, 230, 245));
		CRect tr = pr;
		tr.DeflateRect(3, 1);
		dc.DrawText(name, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
	}

	if (m_dragging) {
		CString ghost = m_owner ? m_owner->FxName(m_dragFx) : L"";
		CRect gr(m_dragPt.x - 40, m_dragPt.y - 10, m_dragPt.x + 40, m_dragPt.y + 10);
		dc.FillSolidRect(&gr, RGB(80, 60, 20));
		dc.SetTextColor(RGB(255, 230, 150));
		dc.DrawText(ghost, &gr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
		if (m_dragFromSlot >= 0) {
			CPen pen(PS_DOT, 1, RGB(255, 200, 80));
			CPen* old = dc.SelectObject(&pen);
			CRect sr = SlotRect(m_dragFromSlot);
			dc.MoveTo(sr.right, (sr.top + sr.bottom) / 2);
			dc.LineTo(m_dragPt);
			dc.SelectObject(old);
		}
	}

	// システム WS_BORDER はアクリル下で欠け・ちらつきやすいので自前1px枠
	dc.Draw3dRect(&rc, RGB(70, 78, 96), RGB(18, 20, 26));

	dc.SelectObject(oldFont);
}

void CScFxWireCtrl::OnPaint()
{
	CPaintDC dc(this);
	CRect r;
	GetClientRect(&r);
	if (r.Width() > 0 && r.Height() > 0) {
		BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
		params.dwFlags = BPPF_ERASE;
		HDC hdcBuf = NULL;
		HPAINTBUFFER hBP = ::BeginBufferedPaint(dc.GetSafeHdc(), &r, BPBF_TOPDOWNDIB, &params, &hdcBuf);
		if (hdcBuf && hBP) {
			CDC dcBuf;
			dcBuf.Attach(hdcBuf);
			PaintToDC(dcBuf);
			dcBuf.Detach();
			::BufferedPaintMakeOpaque(hBP, &r);
			::EndBufferedPaint(hBP, TRUE);
			return;
		}
	}
	PaintToDC(dc);
}

BOOL CScFxWireCtrl::OnEraseBkgnd(CDC* /*pDC*/)
{
	return TRUE;
}

LRESULT CScFxWireCtrl::OnPrintClient(WPARAM wParam, LPARAM /*lParam*/)
{
	if (HDC hdc = (HDC)wParam) {
		CRect r;
		GetClientRect(&r);
		BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
		params.dwFlags = BPPF_ERASE;
		HDC hdcBuf = NULL;
		HPAINTBUFFER hBP = ::BeginBufferedPaint(hdc, &r, BPBF_TOPDOWNDIB, &params, &hdcBuf);
		if (hdcBuf && hBP) {
			CDC dcBuf;
			dcBuf.Attach(hdcBuf);
			PaintToDC(dcBuf);
			dcBuf.Detach();
			::BufferedPaintMakeOpaque(hBP, &r);
			::EndBufferedPaint(hBP, TRUE);
			return 0;
		}
		CDC dc;
		dc.Attach(hdc);
		PaintToDC(dc);
		dc.Detach();
	}
	return 0;
}

void CScFxWireCtrl::OnLButtonDown(UINT nFlags, CPoint point)
{
	if (m_owner && m_owner->m_uiLocked) return;
	const int pal = HitPalette(point);
	if (pal > 0) {
		m_dragging = TRUE;
		m_dragFx = pal;
		m_dragFromSlot = -1;
		m_dragPt = point;
		SetCapture();
		Invalidate(FALSE);
		return;
	}
	const int slot = HitSlot(point);
	if (slot >= 0 && slot < m_slotN && m_slots[slot] > 0) {
		m_dragging = TRUE;
		m_dragFx = m_slots[slot];
		m_dragFromSlot = slot;
		m_dragPt = point;
		SetCapture();
		Invalidate(FALSE);
		return;
	}
	CStatic::OnLButtonDown(nFlags, point);
}

void CScFxWireCtrl::OnMouseMove(UINT nFlags, CPoint point)
{
	if (!m_trackLeave) {
		TRACKMOUSEEVENT tme = { sizeof(tme) };
		tme.dwFlags = TME_LEAVE;
		tme.hwndTrack = m_hWnd;
		if (TrackMouseEvent(&tme))
			m_trackLeave = TRUE;
	}
	if (m_dragging) {
		m_dragPt = point;
		UpdateHover(point);
		Invalidate(FALSE); // ゴースト位置更新
		return;
	}
	UpdateHover(point);
	CStatic::OnMouseMove(nFlags, point);
}

void CScFxWireCtrl::OnMouseLeave()
{
	m_trackLeave = FALSE;
	if (m_hoverFx != 0 || m_hoverSlot >= 0) {
		m_hoverFx = 0;
		m_hoverSlot = -1;
		Invalidate(FALSE);
	}
}

void CScFxWireCtrl::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_dragging) {
		ReleaseCapture();
		m_dragging = FALSE;
		const int dst = HitSlot(point);
		if (dst >= 0 && m_dragFx > 0) {
			if (m_dragFromSlot >= 0) {
				if (dst != m_dragFromSlot) {
					if (dst < m_slotN) {
						const int t = m_slots[dst];
						m_slots[dst] = m_slots[m_dragFromSlot];
						m_slots[m_dragFromSlot] = t;
						BYTE ts[SC_FX_STR_N];
						memcpy(ts, m_str[dst], SC_FX_STR_N);
						memcpy(m_str[dst], m_str[m_dragFromSlot], SC_FX_STR_N);
						memcpy(m_str[m_dragFromSlot], ts, SC_FX_STR_N);
					} else {
						const int v = m_slots[m_dragFromSlot];
						BYTE vs[SC_FX_STR_N];
						memcpy(vs, m_str[m_dragFromSlot], SC_FX_STR_N);
						for (int i = m_dragFromSlot; i < m_slotN - 1; ++i) {
							m_slots[i] = m_slots[i + 1];
							memcpy(m_str[i], m_str[i + 1], SC_FX_STR_N);
						}
						m_slotN--;
						if (m_slotN < SC_FX_CHAIN_MAX) {
							m_slots[m_slotN] = v;
							memcpy(m_str[m_slotN], vs, SC_FX_STR_N);
							m_slotN++;
						}
					}
					NotifyChanged();
				}
			} else {
				auto initStr = [&](int idx) {
					for (int s = 0; s < SC_FX_STR_N; ++s)
						m_str[idx][s] = (BYTE)SC_FX_STR_DEF;
				};
				if (dst < m_slotN) {
					if (m_slotN < SC_FX_CHAIN_MAX) {
						for (int i = m_slotN; i > dst; --i) {
							m_slots[i] = m_slots[i - 1];
							memcpy(m_str[i], m_str[i - 1], SC_FX_STR_N);
						}
						m_slots[dst] = m_dragFx;
						initStr(dst);
						m_slotN++;
					} else {
						m_slots[dst] = m_dragFx;
						initStr(dst);
					}
				} else if (m_slotN < SC_FX_CHAIN_MAX) {
					m_slots[m_slotN] = m_dragFx;
					initStr(m_slotN);
					m_slotN++;
				} else {
					m_slots[SC_FX_CHAIN_MAX - 1] = m_dragFx;
					initStr(SC_FX_CHAIN_MAX - 1);
				}
				NotifyChanged();
			}
		}
		m_dragFx = 0;
		m_dragFromSlot = -1;
		UpdateHover(point);
		Invalidate(FALSE);
		return;
	}
	CStatic::OnLButtonUp(nFlags, point);
}

namespace {
struct ScFxStrCtx {
	CScFxWireCtrl* w;
	int slot;
	int si;
};
}

void CScFxWireCtrl::OnStrSlider(void* ctx, int v)
{
	ScFxStrCtx* c = (ScFxStrCtx*)ctx;
	if (!c || !c->w) return;
	if (c->slot < 0 || c->slot >= SC_FX_CHAIN_MAX) return;
	if (c->si < 0 || c->si >= SC_FX_STR_N) return;
	if (v < 0) v = 0;
	if (v > SC_FX_STR_MAX) v = SC_FX_STR_MAX;
	c->w->m_str[c->slot][c->si] = (BYTE)v;
	c->w->NotifyChanged();
	c->w->Invalidate(FALSE);
}

void CScFxWireCtrl::OnRButtonUp(UINT nFlags, CPoint point)
{
	if (m_owner && m_owner->m_uiLocked) return;
	const int slot = HitSlot(point);
	if (slot < 0 || slot >= m_slotN) {
		CStatic::OnRButtonUp(nFlags, point);
		return;
	}
	CCustomPopupMenu menu;
	menu.AddCommand(ID_SC_FX_CLEAR_SLOT,
		LL14(L"このスロットを解除", L"Clear this slot", L"Effacer ce slot", L"Azzera questo slot",
			L"Borrar esta ranura", L"이 슬롯 해제", L"清除此插槽", L"مسح هذه الفتحة",
			L"Очистить этот слот", L"Diesen Slot löschen", L"Limpar este slot", L"Deze slot wissen",
			L"Wyczyść ten slot", L"Bu slotu temizle"),
		LL14(L"このスロットの効果を外し、右側を詰めて並べ替え", L"Clear this slot's effect and shift the ones on the right leftward",
			L"Effacer l'effet de ce slot et decaler ceux de droite", L"Azzera l'effetto di questo slot e sposta quelli a destra",
			L"Borrar el efecto de este slot y desplazar los de la derecha", L"이 슬롯 효과를 제거하고 오른쪽을 왼쪽으로 채움",
			L"清除此插槽效果并将右侧效果左移", L"مسح تأثير هذه الفتحة وإزاحة اليمنى لليسار",
			L"Очистить эффект слота и сдвинуть правые влево", L"Effekt dieses Slots loeschen und rechte nach links ruecken",
			L"Limpar o efeito deste slot e deslocar os da direita", L"Effect van dit slot wissen en rechts naar links schuiven",
			L"Wyczysc efekt tego slotu i przesun prawe w lewo", L"Bu slotun efektini kaldir ve sagdakileri sola kaydir"));
	menu.AddCommand(ID_SC_FX_DUP_SLOT,
		LL14(L"この効果を右に複製", L"Duplicate effect to the right", L"Dupliquer à droite", L"Duplica a destra",
			L"Duplicar a la derecha", L"오른쪽으로 복제", L"向右复制此效果", L"تكرار التأثير يميناً",
			L"Дублировать вправо", L"Rechts duplizieren", L"Duplicar à direita", L"Rechts dupliceren",
			L"Duplikuj w prawo", L"Sağa çoğalt"),
		LL14(L"同じ効果を右隣スロットへコピー", L"Copy this effect into the next slot on the right",
			L"Copier l'effet dans le slot de droite", L"Copia l'effetto nello slot a destra",
			L"Copiar el efecto al slot de la derecha", L"이 효과를 오른쪽 슬롯에 복사",
			L"将此效果复制到右侧插槽", L"نسخ التأثير إلى الفتحة اليمنى",
			L"Скопировать эффект в слот справа", L"Effekt in den rechten Slot kopieren",
			L"Copiar o efeito para o slot à direita", L"Effect naar rechter slot kopiëren",
			L"Skopiuj efekt do slotu po prawej", L"Efekti sağdaki slota kopyala"),
		m_slotN < SC_FX_CHAIN_MAX);
	menu.AddCommand(ID_SC_FX_MOVE_LEFT,
		LL14(L"左へ移動", L"Move left", L"Déplacer à gauche", L"Sposta a sinistra",
			L"Mover a la izquierda", L"왼쪽으로", L"向左移动", L"تحريك لليسار",
			L"Влево", L"Nach links", L"Mover para a esquerda", L"Naar links",
			L"W lewo", L"Sola taşı"),
		LL14(L"この効果を1つ左のスロットへ入れ替え（適用順を前へ）", L"Swap this effect one slot left (earlier in the chain)",
			L"Echanger avec le slot de gauche (plus tot dans la chaine)", L"Scambia con lo slot a sinistra (prima nella catena)",
			L"Intercambiar con el slot de la izquierda (antes en la cadena)", L"이 효과를 왼쪽 슬롯과 교환(체인 앞쪽)",
			L"与左侧插槽对调（更早应用）", L"تبديل مع الفتحة اليسرى (أبكر في السلسلة)",
			L"Поменять со слотом слева (раньше в цепочке)", L"Mit linkem Slot tauschen (frueher in der Kette)",
			L"Trocar com o slot a esquerda (mais cedo na cadeia)", L"Wisselen met linker slot (eerder in de keten)",
			L"Zamien z slotem po lewej (wczesniej w lancuchu)", L"Soldaki slotla degistir (zincirde daha once)"),
		slot > 0);
	menu.AddCommand(ID_SC_FX_MOVE_RIGHT,
		LL14(L"右へ移動", L"Move right", L"Déplacer à droite", L"Sposta a destra",
			L"Mover a la derecha", L"오른쪽으로", L"向右移动", L"تحريك لليمين",
			L"Вправо", L"Nach rechts", L"Mover para a direita", L"Naar rechts",
			L"W prawo", L"Sağa taşı"),
		LL14(L"この効果を1つ右のスロットへ入れ替え（適用順を後へ）", L"Swap this effect one slot right (later in the chain)",
			L"Echanger avec le slot de droite (plus tard dans la chaine)", L"Scambia con lo slot a destra (dopo nella catena)",
			L"Intercambiar con el slot de la derecha (despues en la cadena)", L"이 효과를 오른쪽 슬롯과 교환(체인 뒤쪽)",
			L"与右侧插槽对调（更晚应用）", L"تبديل مع الفتحة اليمنى (أحدث في السلسلة)",
			L"Поменять со слотом справа (позже в цепочке)", L"Mit rechtem Slot tauschen (spaeter in der Kette)",
			L"Trocar com o slot a direita (mais tarde na cadeia)", L"Wisselen met rechter slot (later in de keten)",
			L"Zamien z slotem po prawej (pozniej w lancuchu)", L"Sagdaki slotla degistir (zincirde daha sonra)"),
		slot < m_slotN - 1);
	menu.AddSeparator();

	CCustomPopupMenu* strRoot = menu.AddSubMenu(
		LL14(L"強度設定", L"Strength", L"Intensité", L"Intensità",
			L"Intensidad", L"강도", L"强度", L"الشدة",
			L"Сила", L"Stärke", L"Intensidade", L"Sterkte",
			L"Siła", L"Yoğunluk"),
		LL14(L"各パラメータをスライダーで調整（ドラッグ中に即反映）",
			L"Adjust each parameter with a slider (live while dragging)",
			L"Régler chaque paramètre au curseur (temps réel)",
			L"Regola ogni parametro con lo slider (in tempo reale)",
			L"Ajustar cada parámetro con el deslizador (en vivo)",
			L"각 파라미터를 슬라이더로 조정(드래그 중 즉시 반영)",
			L"用滑块调整各参数（拖动时即时生效）",
			L"ضبط كل معلمة بالمنزلق (مباشر أثناء السحب)",
			L"Настройка параметров ползунком (сразу при перетаскивании)",
			L"Parameter per Schieberegler (live beim Ziehen)",
			L"Ajustar cada parâmetro com o controle (ao vivo)",
			L"Elke parameter met schuifregelaar (live tijdens slepen)",
			L"Reguluj parametry suwakiem (na żywo podczas przeciągania)",
			L"Her parametreyi kaydırıcıyla ayarla (sürüklerken anlık)"));
	const int fx = m_slots[slot];
	const int pn = m_owner ? m_owner->FxParamCount(fx) : 1;
	ScFxStrCtx lives[SC_FX_STR_N];
	ZeroMemory(lives, sizeof(lives));
	if (strRoot) {
		for (int si = 0; si < pn && si < SC_FX_STR_N; ++si) {
			CString pname = m_owner ? m_owner->FxParamName(fx, si) : L"";
			if (pname.IsEmpty())
				pname.Format(L"%d", si + 1);
			lives[si].w = this;
			lives[si].slot = slot;
			lives[si].si = si;
			strRoot->AddSlider(pname, 0, SC_FX_STR_MAX, (int)m_str[slot][si],
				&CScFxWireCtrl::OnStrSlider, &lives[si],
				LL14(L"0=弱 … 4=標準 … 8=強（ドラッグでプレビュー更新）",
					L"0=weak … 4=default … 8=strong (drag updates preview)",
					L"0=faible … 4=défaut … 8=fort (aperçu en direct)",
					L"0=debole … 4=predef. … 8=forte (anteprima live)",
					L"0=débil … 4=predet. … 8=fuerte (vista previa en vivo)",
					L"0=약 … 4=기본 … 8=강(드래그 시 미리보기 갱신)",
					L"0=弱 … 4=默认 … 8=强（拖动更新预览）",
					L"0=ضعيف … 4=افتراضي … 8=قوي (تحديث المعاينة)",
					L"0=слабо … 4=обычно … 8=сильно (превью сразу)",
					L"0=schwach … 4=Standard … 8=stark (Vorschau live)",
					L"0=fraco … 4=padrão … 8=forte (prévia ao vivo)",
					L"0=zwak … 4=standaard … 8=sterk (live voorvertoning)",
					L"0=słabo … 4=domyślnie … 8=mocno (podgląd na żywo)",
					L"0=zayıf … 4=varsayılan … 8=güçlü (sürüklerken önizleme)"));
		}
		strRoot->AddSeparator();
		strRoot->AddCommand(ID_SC_FX_STR_RESET,
			LL14(L"強度を標準に戻す", L"Reset strength to default", L"Réinit. intensité", L"Ripristina intensità",
				L"Restablecer intensidad", L"강도 기본값", L"重置强度", L"إعادة الشدة",
				L"Сброс силы", L"Stärke zurücksetzen", L"Repor intensidade", L"Sterkte resetten",
				L"Reset siły", L"Yoğunluğu sıfırla"));
	}

	menu.AddSeparator();
	menu.AddCommand(ID_SC_FX_CLEAR_AFTER,
		LL14(L"これより右を解除", L"Clear slots to the right", L"Effacer à droite", L"Azzera a destra",
			L"Borrar a la derecha", L"오른쪽 슬롯 해제", L"清除右侧插槽", L"مسح الفتحات إلى اليمين",
			L"Очистить справа", L"Rechts löschen", L"Limpar à direita", L"Rechts wissen",
			L"Wyczyść na prawo", L"Sağı temizle"),
		LL14(L"このスロットより右の効果をすべて解除", L"Clear all effects in slots to the right of this one",
			L"Effacer tous les effets a droite de ce slot", L"Azzera tutti gli effetti a destra di questo slot",
			L"Borrar todos los efectos a la derecha de este slot", L"이 슬롯보다 오른쪽 효과를 모두 해제",
			L"清除此插槽右侧的全部效果", L"مسح كل التأثيرات إلى يمين هذه الفتحة",
			L"Очистить все эффекты справа от этого слота", L"Alle Effekte rechts von diesem Slot loeschen",
			L"Limpar todos os efeitos a direita deste slot", L"Alle effecten rechts van dit slot wissen",
			L"Wyczysc wszystkie efekty na prawo od tego slotu", L"Bu slotun sagindaki tum efektleri temizle"),
		slot < m_slotN - 1);
	menu.AddCommand(ID_SC_FX_CLEAR_ALL,
		LL14(L"すべての配線を解除", L"Clear all wiring", L"Effacer tout le câblage", L"Azzera tutto",
			L"Borrar todo el cableado", L"모든 배선 해제", L"清除全部连线", L"مسح كل التوصيل",
			L"Очистить всю схему", L"Gesamte Verdrahtung löschen", L"Limpar toda a ligação", L"Alle bedrading wissen",
			L"Wyczyść całe okablowanie", L"Tüm kablolamayı temizle"),
		LL14(L"FXチェーン全体を空にし、配線をクリア", L"Empty the entire FX chain and clear all wiring",
			L"Vider toute la chaine FX et effacer le cablage", L"Svuota l'intera catena FX e azzera il cablaggio",
			L"Vaciar toda la cadena FX y borrar el cableado", L"FX 체인 전체를 비우고 배선을 클리어",
			L"清空整个 FX 链并清除连线", L"تفريغ سلسلة FX بالكامل ومسح التوصيل",
			L"Очистить всю FX-цепочку и схему", L"Gesamte FX-Kette leeren und Verdrahtung loeschen",
			L"Esvaziar toda a cadeia FX e limpar a ligacao", L"Hele FX-keten legen en bedrading wissen",
			L"Wyczysc caly lancuch FX i okablowanie", L"Tum FX zincirini bosalt ve kablolamayi temizle"));
	CPoint sp = point;
	ClientToScreen(&sp);
	CWnd* trackOwner = m_owner ? (CWnd*)m_owner : GetParent();
	if (!trackOwner) trackOwner = this;
	const UINT cmd = menu.Track(sp, trackOwner);
	if (cmd == ID_SC_FX_CLEAR_SLOT) {
		for (int i = slot; i < m_slotN - 1; ++i) {
			m_slots[i] = m_slots[i + 1];
			memcpy(m_str[i], m_str[i + 1], SC_FX_STR_N);
		}
		m_slotN--;
		m_slots[m_slotN] = 0;
		memset(m_str[m_slotN], SC_FX_STR_DEF, SC_FX_STR_N);
		NotifyChanged();
		Invalidate(FALSE);
	} else if (cmd == ID_SC_FX_DUP_SLOT && m_slotN < SC_FX_CHAIN_MAX) {
		const int v = m_slots[slot];
		BYTE vs[SC_FX_STR_N];
		memcpy(vs, m_str[slot], SC_FX_STR_N);
		for (int i = m_slotN; i > slot + 1; --i) {
			m_slots[i] = m_slots[i - 1];
			memcpy(m_str[i], m_str[i - 1], SC_FX_STR_N);
		}
		m_slots[slot + 1] = v;
		memcpy(m_str[slot + 1], vs, SC_FX_STR_N);
		m_slotN++;
		NotifyChanged();
		Invalidate(FALSE);
	} else if (cmd == ID_SC_FX_MOVE_LEFT && slot > 0) {
		const int t = m_slots[slot - 1];
		m_slots[slot - 1] = m_slots[slot];
		m_slots[slot] = t;
		BYTE ts[SC_FX_STR_N];
		memcpy(ts, m_str[slot - 1], SC_FX_STR_N);
		memcpy(m_str[slot - 1], m_str[slot], SC_FX_STR_N);
		memcpy(m_str[slot], ts, SC_FX_STR_N);
		NotifyChanged();
		Invalidate(FALSE);
	} else if (cmd == ID_SC_FX_MOVE_RIGHT && slot < m_slotN - 1) {
		const int t = m_slots[slot + 1];
		m_slots[slot + 1] = m_slots[slot];
		m_slots[slot] = t;
		BYTE ts[SC_FX_STR_N];
		memcpy(ts, m_str[slot + 1], SC_FX_STR_N);
		memcpy(m_str[slot + 1], m_str[slot], SC_FX_STR_N);
		memcpy(m_str[slot], ts, SC_FX_STR_N);
		NotifyChanged();
		Invalidate(FALSE);
	} else if (cmd == ID_SC_FX_CLEAR_AFTER && slot < m_slotN - 1) {
		for (int i = slot + 1; i < m_slotN; ++i) {
			m_slots[i] = 0;
			memset(m_str[i], SC_FX_STR_DEF, SC_FX_STR_N);
		}
		m_slotN = slot + 1;
		NotifyChanged();
		Invalidate(FALSE);
	} else if (cmd == ID_SC_FX_CLEAR_ALL) {
		memset(m_slots, 0, sizeof(m_slots));
		memset(m_str, SC_FX_STR_DEF, sizeof(m_str));
		m_slotN = 0;
		NotifyChanged();
		Invalidate(FALSE);
	} else if (cmd == ID_SC_FX_STR_RESET) {
		memset(m_str[slot], SC_FX_STR_DEF, SC_FX_STR_N);
		NotifyChanged();
		Invalidate(FALSE);
	}
}

namespace {

class CScHelpDlg : public CDialog
{
public:
	enum { IDD = IDD_SC_HELP };
	explicit CScHelpDlg(CScreenCaptureDlg* owner, CWnd* pParent = nullptr)
		: CDialog(IDD, pParent), m_owner(owner) {}
protected:
	CScreenCaptureDlg* m_owner;
	virtual BOOL OnInitDialog();
	virtual void PostNcDestroy();
	virtual void OnOK();
	virtual void OnCancel();
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnClose();
	DECLARE_MESSAGE_MAP()
};

static CScHelpDlg* g_scHelpDlg = nullptr;

BEGIN_MESSAGE_MAP(CScHelpDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CLOSE()
END_MESSAGE_MAP()

BOOL CScHelpDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	SetIcon(nullptr, TRUE);
	SetIcon(nullptr, FALSE);
	ModifyStyleEx(0, WS_EX_DLGMODALFRAME, SWP_FRAMECHANGED);
	SetWindowText(LL14(
		L"画面キャプチャ操作ガイド", L"Screen Capture Guide", L"Guide de capture", L"Guida cattura",
		L"Guía de captura", L"화면 캡처 가이드", L"屏幕捕获指南", L"دليل الالتقاط",
		L"Руководство захвата", L"Aufnahme-Anleitung", L"Guia de captura", L"Opnamegids",
		L"Przewodnik przechwytywania", L"Yakalama kılavuzu"));
	if (CWnd* pOk = GetDlgItem(IDOK))
		pOk->SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
			L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	return TRUE;
}

void CScHelpDlg::OnOK() { DestroyWindow(); }
void CScHelpDlg::OnCancel() { DestroyWindow(); }
void CScHelpDlg::OnClose() { DestroyWindow(); }

void CScHelpDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	if (g_scHelpDlg == this)
		g_scHelpDlg = nullptr;
	delete this;
}

BOOL CScHelpDlg::OnEraseBkgnd(CDC* pDC)
{
	CRect rc; GetClientRect(&rc);
	pDC->FillSolidRect(rc, RGB(248, 248, 252));
	return TRUE;
}

void CScHelpDlg::OnPaint()
{
	CPaintDC pdc(this);
	CCC_GdiHelpPaint hp;
	if (!CCC_GdiHelpBeginPaint(this, pdc, hp))
		return;
	CDC& dc = hp.mem;
	CRect rc = hp.rc;
	const int footerH = hp.footerH;
	dc.SetBkMode(TRANSPARENT);
	CFont* baseFont = GetFont();
	CFont boldFont;
	{
		LOGFONT lf = {};
		if (baseFont && baseFont->GetSafeHandle())
			baseFont->GetLogFont(&lf);
		else {
			NONCLIENTMETRICS ncm = {};
			ncm.cbSize = sizeof(ncm);
			::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
			lf = ncm.lfMessageFont;
		}
		lf.lfWeight = FW_BOLD;
		boldFont.CreateFontIndirect(&lf);
	}
	CFont* oldFont = dc.SelectObject(baseFont);

	TEXTMETRIC tm{};
	dc.GetTextMetrics(&tm);
	const int lh = max(14, tm.tmHeight + tm.tmExternalLeading + 1);
	const int titleLh = lh + 2;
	CBrush frameBrush(RGB(130, 130, 150));

	auto title = [&](int x, int y, LPCTSTR t) {
		CFont* prev = dc.SelectObject(&boldFont);
		dc.SetTextColor(RGB(72, 48, 120));
		dc.TextOut(x, y, t);
		dc.SelectObject(prev);
	};
	auto body = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(52, 52, 68));
		dc.TextOut(x, y, t);
	};
	auto muted = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(100, 100, 120));
		dc.TextOut(x, y, t);
	};

	int y = 6;
	const int L = 10;
	title(L, y, LL14(L"画面キャプチャ操作ガイド", L"Screen Capture — Guide", L"Guide capture", L"Guida cattura",
		L"Guía captura", L"화면 캡처 가이드", L"屏幕捕获指南", L"دليل الالتقاط",
		L"Руководство захвата", L"Aufnahme-Guide", L"Guia captura", L"Opnamegids",
		L"Przewodnik", L"Yakalama kılavuzu"));
	y += titleLh;
	muted(L, y, LL14(
		L"プレビューで構図を整え、FXを配線してから MP4 に録画します。",
		L"Compose in the preview, wire FX, then record to MP4.",
		L"Composez l'aperçu, câblez FX, enregistrez en MP4.",
		L"Componi anteprima, cablaggia FX, registra MP4.",
		L"Compose la vista previa, cable FX, graba MP4.",
		L"미리보기에서 구도를 잡고 FX를 배선한 뒤 MP4로 녹화합니다.",
		L"在预览中构图、连接效果，然后录制为 MP4。",
		L"رتّب المعاينة، وصّل FX، ثم سجّل إلى MP4.",
		L"Соберите превью, соедините FX, запишите MP4.",
		L"Vorschau komponieren, FX verdrahten, als MP4 aufnehmen.",
		L"Componha a prévia, ligue FX, grave em MP4.",
		L"Stel preview samen, bedraad FX, neem op naar MP4.",
		L"Ułóż podgląd, podłącz FX, nagraj do MP4.",
		L"Önizlemeyi düzenle, FX bağla, MP4 kaydet."));
	y += lh + 4;
	y = CCC_GdiHelpDrawSoftDemoPair(dc, L, y, rc.Width() - L * 2, min(140, max(112, rc.Height() / 5)),
		CCC_HELPDEMO_KCAPTURE);

	title(L, y, LL14(L"基本操作", L"Basics", L"Bases", L"Basi", L"Básicos", L"기본", L"基本", L"أساسيات",
		L"Основы", L"Grundlagen", L"Básicos", L"Basis", L"Podstawy", L"Temeller"));
	y += titleLh;
	body(L, y, LL14(L"・モード …… モニタ / 仮想画面 / ウィンドウ合成", L"· Mode …… monitor / virtual / window compose", L"· Mode …… moniteur / virtuel / fenêtres", L"· Modalità …… monitor / virtuale / finestre",
		L"· Modo …… monitor / virtual / ventanas", L"· 모드 …… 모니터 / 가상 / 창 합성", L"· 模式 …… 监视器/虚拟/窗口合成", L"· الوضع …… شاشة / افتراضي / نوافذ",
		L"· Режим …… монитор / вирт. / окна", L"· Modus …… Monitor / virtuell / Fenster", L"· Modo …… monitor / virtual / janelas", L"· Modus …… monitor / virtueel / vensters",
		L"· Tryb …… monitor / wirtualny / okna", L"· Mod …… monitör / sanal / pencere")); y += lh;
	body(L, y, LL14(L"・プレビュー …… ドラッグで移動、四隅でリサイズ、右クリックで表示切替", L"· Preview …… drag move, corners resize, right-click hide", L"· Aperçu …… glisser / coins / clic droit", L"· Anteprima …… trascina / angoli / destro",
		L"· Vista …… arrastrar / esquinas / clic der.", L"· 미리보기 …… 드래그·모서리·우클릭", L"· 预览 …… 拖动/四角缩放/右键显隐", L"· معاينة …… سحب / زوايا / يمين",
		L"· Превью …… перенос / углы / ПКМ", L"· Vorschau …… ziehen / Ecken / Rechtsklick", L"· Prévia …… arrastar / cantos / direito", L"· Preview …… slepen / hoeken / rechtsklik",
		L"· Podgląd …… przeciągnij / rogi / PPM", L"· Önizleme …… sürükle / köşe / sağ tık")); y += lh;
	body(L, y, LL14(L"・レイヤ …… 一覧から追加、Z+/Z- で前後、切出でクロップ", L"· Layers …… add from list, Z+/Z- order, crop fields", L"· Calques …… ajouter, Z+/Z-, rognage", L"· Livelli …… aggiungi, Z+/Z-, ritaglio",
		L"· Capas …… añadir, Z+/Z-, recorte", L"· 레이어 …… 추가, Z+/Z-, 크롭", L"· 图层 …… 添加、Z+/Z-、裁剪", L"· طبقات …… إضافة، Z+/Z-، قص",
		L"· Слои …… добавить, Z+/Z-, вырез", L"· Ebenen …… hinzufügen, Z+/Z-, Ausschnitt", L"· Camadas …… adicionar, Z+/Z-, recorte", L"· Lagen …… toevoegen, Z+/Z-, uitsnede",
		L"· Warstwy …… dodaj, Z+/Z-, wycinek", L"· Katman …… ekle, Z+/Z-, kırp")); y += lh;
	body(L, y, LL14(L"・音声 …… システム音 / マイク。右側の棒はピークメータ", L"· Audio …… system / mic. Right bars = peak meters", L"· Audio …… système / micro. Barres = crêtes", L"· Audio …… sistema / micro. Barre = picchi",
		L"· Audio …… sistema / mic. Barras = picos", L"· 오디오 …… 시스템/마이크. 막대=피크", L"· 音频 …… 系统/麦克风。右侧=峰值", L"· صوت …… نظام/ميك. الأشرطة=قمم",
		L"· Звук …… система / микрофон. Полосы = пики", L"· Audio …… System / Mikro. Balken = Pegel", L"· Áudio …… sistema / micro. Barras = picos", L"· Audio …… systeem / mic. Balken = pieken",
		L"· Dźwięk …… system / mik. Paski = szczyty", L"· Ses …… sistem / mik. Çubuklar = tepe")); y += lh;
	body(L, y, LL14(L"・マウスカーソル …… チェックで録画・プレビューに載せる／外す", L"· Mouse cursor …… checkbox to include/exclude in preview & recording", L"· Curseur …… case pour inclure/exclure aperçu et enregistrement", L"· Cursore …… casella per includere/escludere anteprima e registrazione",
		L"· Cursor …… casilla para incluir/excluir en vista y grabación", L"· 마우스 커서 …… 체크로 미리보기·녹화에 포함/제외", L"· 鼠标光标 …… 勾选以在预览/录制中包含或排除", L"· مؤشر الفأرة …… خانة لتضمين/استبعاد في المعاينة والتسجيل",
		L"· Курсор …… галочка — показать/скрыть в превью и записи", L"· Mauszeiger …… Haken = in Vorschau/Aufnahme ein-/ausblenden", L"· Cursor …… caixa para incluir/excluir na prévia e gravação", L"· Muiscursor …… vinkje om in voorbeeld/opname te tonen/verbergen",
		L"· Kursor …… zaznaczenie = pokaż/ukryj w podglądzie i nagraniu", L"· Fare imleci …… onay kutusu ile önizleme/kayıtta göster/gizle")); y += lh;
	body(L, y, LL14(L"・ライブ配信 …… YouTube認証→配信枠作成、または Nico/カスタムで URL+キー。ffmpeg.exe 必須", L"· Live …… YouTube Auth→Create, or Nico/Custom URL+key. ffmpeg.exe required", L"· Live …… Auth YouTube→Créer, ou Nico/Perso URL+clé. ffmpeg.exe requis", L"· Live …… Auth YouTube→Crea, o Nico/Custom URL+chiave. Serve ffmpeg.exe",
		L"· En vivo …… Auth YouTube→Crear, o Nico/Pers. URL+clave. Requiere ffmpeg.exe", L"· 라이브 …… YouTube 인증→방송 생성, 또는 Nico/사용자 URL+키. ffmpeg.exe 필수", L"· 直播 …… YouTube 认证→创建直播，或 Nico/自定义 URL+密钥。需要 ffmpeg.exe", L"· بث …… مصادقة YouTube→إنشاء، أو Nico/مخصص رابط+مفتاح. يلزم ffmpeg.exe",
		L"· Эфир …… Auth YouTube→Создать, или Nico/свой URL+ключ. Нужен ffmpeg.exe", L"· Live …… YouTube-Auth→Anlegen, oder Nico/Custom URL+Key. ffmpeg.exe nötig", L"· Ao vivo …… Auth YouTube→Criar, ou Nico/Pers. URL+chave. Requer ffmpeg.exe", L"· Live …… YouTube-auth→Maken, of Nico/Custom URL+key. ffmpeg.exe vereist",
		L"· Live …… Auth YouTube→Utwórz, lub Nico/Własne URL+klucz. Wymaga ffmpeg.exe", L"· Canlı …… YouTube Auth→Oluştur, veya Nico/Özel URL+anahtar. ffmpeg.exe gerekir")); y += lh + 4;

	title(L, y, LL14(L"ライブ配信", L"Live streaming", L"Diffusion live", L"Diretta", L"Transmisión", L"라이브", L"直播", L"البث المباشر",
		L"Прямой эфир", L"Livestream", L"Transmissão ao vivo", L"Livestream", L"Transmisja", L"Canlı yayın"));
	y += titleLh;
	body(L, y, LL14(L"・YouTube …… Googleでログイン → 配信枠作成 → 配信開始（MP4は書きません）", L"· YouTube …… Sign in with Google → Create → Go live (no MP4)", L"· YouTube …… Google → Créer → Diffuser (pas de MP4)", L"· YouTube …… Google → Crea → Diretta (niente MP4)",
		L"· YouTube …… Google → Crear → Emitir (sin MP4)", L"· YouTube …… Google 로그인 → 방송 생성 → 시작(MP4 없음)", L"· YouTube …… Google 登录 → 创建 → 开播（不写 MP4）", L"· YouTube …… تسجيل Google → إنشاء → بث (بدون MP4)",
		L"· YouTube …… Google → Создать → Эфир (без MP4)", L"· YouTube …… Google → Anlegen → Live (kein MP4)", L"· YouTube …… Google → Criar → Ao vivo (sem MP4)", L"· YouTube …… Google → Maken → Live (geen MP4)",
		L"· YouTube …… Google → Utwórz → Live (bez MP4)", L"· YouTube …… Google → Oluştur → Canlı (MP4 yok)")); y += lh;
	body(L, y, LL14(L"・Nico/カスタム …… 配信サイトの RTMP URL とキーを手入力して配信開始", L"· Nico/Custom …… enter the site's RTMP URL and key, then Go live", L"· Nico/Perso …… saisissez URL RTMP et clé, puis Diffuser", L"· Nico/Custom …… inserisci URL RTMP e chiave, poi Vai in diretta",
		L"· Nico/Pers. …… introduzca URL RTMP y clave, luego Emitir", L"· Nico/사용자 …… 사이트의 RTMP URL·키를 입력 후 시작", L"· Nico/自定义 …… 填写站点 RTMP URL 和密钥后开播", L"· Nico/مخصص …… أدخل رابط RTMP والمفتاح ثم ابدأ البث",
		L"· Nico/свой …… введите RTMP URL и ключ, затем В эфир", L"· Nico/Custom …… RTMP-URL und Key eingeben, dann Live starten", L"· Nico/Pers. …… introduza URL RTMP e chave e Entre ao vivo", L"· Nico/Custom …… vul RTMP-URL en key in, dan Live starten",
		L"· Nico/Własne …… wpisz URL RTMP i klucz, potem Rozpocznij", L"· Nico/Özel …… sitenin RTMP URL ve anahtarını girip Yayına başla")); y += lh + 4;

	// mini wiring diagram
	title(L, y, LL14(L"FX配線", L"FX wiring", L"Câblage FX", L"Cablaggio FX", L"Cableado FX", L"FX 배선", L"效果连线", L"توصيل FX",
		L"Схема FX", L"FX-Verdrahtung", L"Ligação FX", L"FX-bedrading", L"Okablowanie FX", L"FX kablolama"));
	y += titleLh;
	const int gx = L, gy = y, gw = min(280, rc.Width() / 2), gh = lh * 2 + 10;
	dc.FillSolidRect(gx, gy, gw, gh, RGB(245, 246, 250));
	dc.FillSolidRect(gx + 4, gy + 6, 28, gh - 12, RGB(70, 140, 90));
	dc.SetTextColor(RGB(255, 255, 255));
	dc.TextOut(gx + 8, gy + 8, L"IN");
	dc.FillSolidRect(gx + 44, gy + 6, 50, gh - 12, RGB(180, 140, 60));
	dc.FillSolidRect(gx + 104, gy + 6, 50, gh - 12, RGB(180, 140, 60));
	dc.FillSolidRect(gx + 164, gy + 6, 40, gh - 12, RGB(55, 60, 75));
	dc.FillSolidRect(gx + 214, gy + 6, 36, gh - 12, RGB(150, 70, 70));
	dc.SetTextColor(RGB(255, 255, 255));
	dc.TextOut(gx + 48, gy + 8, L"Blur");
	dc.TextOut(gx + 108, gy + 8, L"Neon");
	dc.TextOut(gx + 172, gy + 8, L"…");
	dc.TextOut(gx + 218, gy + 8, L"OUT");
	dc.FrameRect(CRect(gx, gy, gx + gw, gy + gh), &frameBrush);
	y = gy + gh + 4;
	muted(L, y, LL14(
		L"パレットからスロットへドラッグ。占有スロットへ落とすと挿入（同一効果の並列可）。右クリックで解除/複製/強度。最大8段・左→右に適用。上のプリセットで配線を保存/読込。",
		L"Drag palette→slot. Drop on filled slot inserts (duplicates OK). Right-click clears/duplicates/strength. Up to 8, left→right. Presets above save/load wiring.",
		L"Glisser palette→slot. Déposer sur rempli = insertion (doublons OK). Clic droit. Max 8, gauche→droite. Préréglages au-dessus.",
		L"Trascina palette→slot. Su pieno = inserimento (duplicati OK). Destro. Max 8, sx→dx. Preset sopra.",
		L"Arrastre paleta→ranura. Sobre ocupada = insertar (duplicados OK). Clic der. Máx. 8, izq→der. Ajustes arriba.",
		L"팔레트→슬롯 드래그. 채워진 슬롯에 놓으면 삽입(중복 가능). 우클릭 해제/복제/강도. 최대 8단, 좌→우. 위 프리셋으로 저장/불러오기.",
		L"从调色板拖到插槽。放到已占用插槽会插入（可重复）。右键清除/复制/强度。最多8段，左→右。上方预设可保存/读取连线。",
		L"اسحب إلى الفتحة. الإسقاط على ممتلئة يُدرج (تكرار مسموح). يمين. حتى 8، يسار→يمين. الإعدادات أعلاه.",
		L"Перетащите на слот. На занятый = вставка (дубликаты OK). ПКМ. До 8, слева→направо. Пресеты сверху.",
		L"Palette→Slot. Auf belegten = Einfügen (Duplikate OK). Rechtsklick. Max. 8, links→rechts. Presets oben.",
		L"Arraste paleta→slot. Em ocupado = inserir (duplicatas OK). Direito. Até 8, esq→dir. Predefinições acima.",
		L"Palet→slot. Op bezet = invoegen (duplicaten OK). Rechtsklik. Max 8, links→rechts. Presets hierboven.",
		L"Przeciągnij→slot. Na zajęty = wstaw (duplikaty OK). PPM. Max 8, lewo→prawo. Presety powyżej.",
		L"Paletten→slot. Dolu slota bırakınca ekler (tekrar OK). Sağ tık. En fazla 8, sol→sağ. Üstteki önayarlar."));
	y += lh + 6;

	title(L, y, LL14(L"エフェクト一覧", L"Effects", L"Effets", L"Effetti", L"Efectos", L"효과 목록", L"效果列表", L"التأثيرات",
		L"Эффекты", L"Effekte", L"Efeitos", L"Effecten", L"Efekty", L"Efektler"));
	y += titleLh;

	const int colW = (rc.Width() - L * 2 - 8) / 2;
	const int col2 = L + colW + 8;
	int y1 = y, y2 = y;
	for (int fx = 1; fx < SC_FX_COUNT; ++fx) {
		const BOOL left = ((fx - 1) % 2) == 0;
		int& yy = left ? y1 : y2;
		const int xx = left ? L : col2;
		CString line = m_owner ? (m_owner->FxName(fx) + L" — " + m_owner->FxDesc(fx))
			: CString(L"?");
		CRect tr(xx, yy, xx + colW - 2, yy + lh);
		dc.SetTextColor(RGB(55, 55, 70));
		dc.DrawText(line, &tr, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
		yy += lh;
	}
	y = max(y1, y2) + 4;
	muted(L, y, LL14(
		L"FPS は 120 まで。色味は軽め。中〜重（ぼかし極大/放射/油絵/ゴッドレイ等）は GPU 負荷大・実フレーム低下はユーザー判断。",
		L"FPS up to 120. Tints are light. Med–heavy (mega blur/radial/oil/godrays…) tax the GPU—FPS drop is your call.",
		L"FPS jusqu'à 120. Teintes légères. Moyen–lourd (flou max/radial/huile/rayons…) charge le GPU—baisse = votre choix.",
		L"FPS fino a 120. Tinte leggere. Medio–pesante (mega blur/radiale/olio/godrays…) carica la GPU—calo fps a vostra scelta.",
		L"FPS hasta 120. Tintes ligeros. Med–pesado (mega blur/radial/óleo/rayos…) carga la GPU—bajar fps es su decisión.",
		L"FPS는 120까지. 색조는 가벼움. 중~무거움(초대형 블러/방사/유화/갓레이 등)은 GPU 부하—프레임 저하는 사용자 판단.",
		L"FPS最高120。色调较轻。中~重（超模糊/径向/油画/丁达尔等）GPU负载大，掉帧由您决定。",
		L"حتى 120 إطارًا. الصبغات خفيفة. المتوسط–الثقيل يحمّل GPU—انخفاض الإطارات قرارك.",
		L"FPS до 120. Оттенки лёгкие. Сред–тяж. (мега-blur/radial/масло/лучи…) нагружают GPU—падение частоты на ваш выбор.",
		L"FPS bis 120. Töne leicht. Mittel–schwer (Mega-Blur/Radial/Öl/Godrays…) belasten die GPU—FPS-Abfall ist Ihre Entscheidung.",
		L"FPS até 120. Tons leves. Méd–pesado (mega blur/radial/óleo/raios…) carrega a GPU—queda de fps é sua escolha.",
		L"FPS tot 120. Tinten licht. Mid–zwaar (mega-blur/radiaal/olie/godrays…) belasten de GPU—fps-daling is jouw keuze.",
		L"FPS do 120. Odcienie lekkie. Śr.–ciężkie (mega blur/radial/olej/promienie…) obciążają GPU—spadek fps to Twoja decyzja.",
		L"FPS 120'ye kadar. Tonlar hafif. Orta–ağır (mega blur/radyal/yağlı/godrays…) GPU yükler—kare düşüşü sizin kararınız."));
	y += lh + 6;

	title(L, y, LL14(L"Soft3D（CCustom の飾り）", L"Soft 3D (CCustom accents)", L"Soft 3D (accents CCustom)", L"Soft 3D (accenti CCustom)",
		L"Soft 3D (acentos CCustom)", L"Soft3D (CCustom 장식)", L"Soft3D（CCustom 装饰）", L"Soft3D (زخارف CCustom)",
		L"Soft 3D (акценты CCustom)", L"Soft 3D (CCustom-Akzente)", L"Soft 3D (acentos CCustom)", L"Soft 3D (CCustom-accenten)",
		L"Soft 3D (akcenty CCustom)", L"Soft 3B (CCustom süs)"));
	y += titleLh;
	body(L, y, LL14(
		L"・この窓はボタン／チェック／グループ枠など CCustom コントロールに小さな Soft3D 飾りが入ります（CPU、OpenGL/D3D なし）",
		L"· This window’s CCustom buttons, checks and group boxes carry tiny Soft 3D accents (CPU; no OpenGL/D3D)",
		L"· Boutons/cases/cadres CCustom ont de petits accents Soft 3D (CPU, sans OpenGL/D3D)",
		L"· Pulsanti/check/group box CCustom hanno piccoli accenti Soft 3D (CPU, no OpenGL/D3D)",
		L"· Botones/casillas/marcos CCustom llevan Soft 3D pequeño (CPU, sin OpenGL/D3D)",
		L"· 이 창의 CCustom 버튼·체크·그룹박스에 작은 Soft3D 장식(CPU, OpenGL/D3D 없음)",
		L"· 本窗的 CCustom 按钮/复选/分组框有细小 Soft3D 装饰（CPU，无 OpenGL/D3D）",
		L"· أزرار/مربعات/أطر CCustom هنا لها زخارف Soft3D صغيرة (معالج فقط)",
		L"· Кнопки/флажки/group box CCustom — мелкие Soft 3D-акценты (только CPU)",
		L"· CCustom-Buttons/Checks/GroupBox mit kleinen Soft-3D-Akzenten (nur CPU)",
		L"· Botões/checks/group boxes CCustom têm Soft 3D pequeno (só CPU)",
		L"· CCustom-knoppen/checks/groupboxes hebben Soft 3D-accenten (alleen CPU)",
		L"· Przyciski/checkboxy/groupboxy CCustom mają Soft 3D (tylko CPU)",
		L"· CCustom düğme/onay/grup kutularında Soft 3B süs (yalnızca CPU)")); y += lh;
	muted(L, y, LL14(
		L"視点付きの全面 Soft3D は MPバナー／アナライザー／ピアノロール／コマンドロール側です。各窓の「?」ガイドも参照。",
		L"Full interactive Soft 3D views live on the MP banner, Analyzer, Piano Roll and Command Roll. See each window’s ? guide too.",
		L"Les vues Soft 3D interactives = bannière MP, analyseur, piano roll, command roll. Voir aussi « ? ».",
		L"Le viste Soft 3D interattive = banner MP, analizzatore, piano roll, command roll. Vedi anche « ? ».",
		L"Las vistas Soft 3D interactivas = banner MP, analizador, piano roll, command roll. Vea también « ? ».",
		L"시점 Soft3D는 MP 배너·애널라이저·피아노롤·커맨드롤. 각 창「?」가이드도 참고.",
		L"可操作 Soft3D 在 MP 横幅、分析器、钢琴卷帘、命令卷帘。也可看各窗「?」指南。",
		L"مشاهد Soft3D التفاعلية في بانر MP والمحلل والبيانو وCommand Roll. راجع أيضاً «؟».",
		L"Интерактивные Soft 3D — баннер MP, анализатор, piano roll, command roll. См. также «?».",
		L"Interaktive Soft-3D-Ansichten: MP-Banner, Analyzer, Piano Roll, Command Roll. Auch „?“.",
		L"Soft 3D interativo: banner MP, analisador, piano roll, command roll. Veja também «?».",
		L"Interactieve Soft 3D: MP-banner, analyser, pianorol, command roll. Zie ook «?».",
		L"Interaktywne Soft 3D: baner MP, analizator, piano roll, command roll. Zobacz też «?».",
		L"Etkileşimli Soft 3B: MP banner, analizör, piyano roll, komut roll. Ayrıca «?»."));

	dc.SelectObject(oldFont);
	CCC_GdiHelpEndPaint(hp);
}

} // namespace

CScreenCaptureDlg* g_screenCaptureDlg = NULL;

IMPLEMENT_DYNAMIC(CScreenCaptureDlg, CCustomBlurDialogBase)

CScreenCaptureDlg::CScreenCaptureDlg(CWnd* pParent)
	: CCustomBlurDialogBase(CScreenCaptureDlg::IDD, pParent)
	, m_availCnt(0)
	, m_layerCnt(0)
	, m_monCnt(0)
	, m_modeComboCnt(0)
	, m_snapCsInit(FALSE)
	, m_cacheBmp(NULL)
	, m_cacheW(0)
	, m_cacheH(0)
	, m_cacheDc(NULL)
	, m_cacheOld(NULL)
	, m_cacheBits(NULL)
	, m_dragHandle(SC_HIT_NONE)
	, m_dragLayer(-1)
	, m_dragging(FALSE)
	, m_dragStartCx(0)
	, m_dragStartCy(0)
	, m_dragOrigX(0)
	, m_dragOrigY(0)
	, m_dragOrigW(0)
	, m_dragOrigH(0)
	, m_hoverHandle(SC_HIT_NONE)
	, m_hoverLayer(-1)
	, m_stop(0)
	, m_run(0)
	, m_encodeGdi(0)
	, m_lastHr(S_OK)
	, m_lastStage(0)
	, m_frameCnt(0)
	, m_encFpsX10(0)
	, m_prevFpsX10(0)
	, m_prevFpsWinTick(0)
	, m_prevFpsWinCnt(0)
	, m_thread(NULL)
	, m_peakThread(NULL)
	, m_peakStop(0)
	, m_peakRun(0)
	, m_uiLocked(FALSE)
	, m_stopping(FALSE)
	, m_everStarted(FALSE)
	, m_picking(FALSE)
	, m_peakMic(0)
	, m_peakSys(0)
	, m_peakMix(0)
	, m_withAudio(TRUE)
	, m_withMic(FALSE)
	, m_liveMode(FALSE)
	, m_ytLiveTransitionDone(FALSE)
	, m_ytTestingRequested(FALSE)
	, m_ytLivePhase(0)
	, m_ytGoLiveRequest(0)
	, m_ytGoLiveLastTick(0)
	, m_ytGoLiveThread(NULL)
	, m_liveService(0)
	, m_fpsVal(15)
	, m_startTick(0)
{
	m_ytStreamStatus[0] = 0;
	memset(m_availHwnd, 0, sizeof(m_availHwnd));
	memset(m_layers, 0, sizeof(m_layers));
	memset(&m_recSnap, 0, sizeof(m_recSnap));
}

CScreenCaptureDlg::~CScreenCaptureDlg()
{
	m_stopping = TRUE;
	InterlockedExchange(&m_stop, 1);
	InterlockedExchange(&m_peakStop, 1);
	if (m_peakThread) {
		WaitForSingleObject(m_peakThread, 4000);
		CloseHandle(m_peakThread);
		m_peakThread = NULL;
	}
	if (m_thread) {
		WaitForSingleObject(m_thread, 8000);
		CloseHandle(m_thread);
		m_thread = NULL;
	}
	if (m_ytGoLiveThread) {
		WaitForSingleObject(m_ytGoLiveThread, 8000);
		CloseHandle(m_ytGoLiveThread);
		m_ytGoLiveThread = NULL;
	}
	if (m_cacheDc) {
		if (m_cacheOld) SelectObject(m_cacheDc, m_cacheOld);
		DeleteDC(m_cacheDc);
		m_cacheDc = NULL;
	}
	if (m_cacheBmp) {
		DeleteObject(m_cacheBmp);
		m_cacheBmp = NULL;
	}
	m_cacheBits = NULL;
	if (m_snapCsInit) {
		DeleteCriticalSection(&m_snapCs);
		m_snapCsInit = FALSE;
	}
	ScWgcShutdown();
}

void CScreenCaptureDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SC_PREVIEW, m_preview);
	DDX_Control(pDX, IDC_SC_FXGRAPH, m_fxWire);
	DDX_Control(pDX, IDC_SC_FXPRE_L, m_fxPreLabel);
	DDX_Control(pDX, IDC_SC_FXPRE, m_fxPre);
	DDX_Control(pDX, IDC_SC_FXPRE_LOAD, m_fxPreLoad);
	DDX_Control(pDX, IDC_SC_FXPRE_SAVE, m_fxPreSave);
	DDX_Control(pDX, IDC_SC_HELP, m_help);
	DDX_Control(pDX, IDC_SC_MODE_L, m_modeLabel);
	DDX_Control(pDX, IDC_SC_MODE, m_mode);
	DDX_Control(pDX, IDC_SC_CANVAS_L, m_canvasLabel);
	DDX_Control(pDX, IDC_SC_CANVAS, m_canvas);
	DDX_Control(pDX, IDC_SC_FPS_L, m_fpsLabel);
	DDX_Control(pDX, IDC_SC_FPS, m_fps);
	DDX_Control(pDX, IDC_SC_EFFECT_L, m_effectLabel);
	DDX_Control(pDX, IDC_SC_EFFECT, m_effect);
	DDX_Control(pDX, IDC_SC_AUDIO, m_audio);
	DDX_Control(pDX, IDC_SC_MIC, m_mic);
	DDX_Control(pDX, IDC_SC_MICDEV_L, m_micDevLabel);
	DDX_Control(pDX, IDC_SC_MICDEV, m_micDev);
	DDX_Control(pDX, IDC_SC_LOOPDEV_L, m_loopDevLabel);
	DDX_Control(pDX, IDC_SC_LOOPDEV, m_loopDev);
	DDX_Control(pDX, IDC_SC_METER_MIC_L, m_meterMicL);
	DDX_Control(pDX, IDC_SC_METER_SYS_L, m_meterSysL);
	DDX_Control(pDX, IDC_SC_METER_MIX_L, m_meterMixL);
	DDX_Control(pDX, IDC_SC_METER_MIC, m_meterMic);
	DDX_Control(pDX, IDC_SC_METER_SYS, m_meterSys);
	DDX_Control(pDX, IDC_SC_METER_MIX, m_meterMix);
	DDX_Control(pDX, IDC_SC_INCMP, m_includeMp);
	DDX_Control(pDX, IDC_SC_CURSOR, m_showCursor);
	DDX_Control(pDX, IDC_SC_LIVE, m_live);
	DDX_Control(pDX, IDC_SC_LIVE_CFG, m_liveCfg);
	DDX_Control(pDX, IDC_SC_PICK, m_pick);
	DDX_Control(pDX, IDC_SC_REFRESH, m_refresh);
	DDX_Control(pDX, IDC_SC_AVAIL_L, m_availLabel);
	DDX_Control(pDX, IDC_SC_AVAIL, m_avail);
	DDX_Control(pDX, IDC_SC_LAYER_L, m_layerLabel);
	DDX_Control(pDX, IDC_SC_LAYER, m_layer);
	DDX_Control(pDX, IDC_SC_ADD, m_add);
	DDX_Control(pDX, IDC_SC_REMOVE, m_remove);
	DDX_Control(pDX, IDC_SC_ZUP, m_zUp);
	DDX_Control(pDX, IDC_SC_ZDOWN, m_zDown);
	DDX_Control(pDX, IDC_SC_GEO_L, m_geoLabel);
	DDX_Control(pDX, IDC_SC_X, m_editX);
	DDX_Control(pDX, IDC_SC_Y, m_editY);
	DDX_Control(pDX, IDC_SC_W, m_editW);
	DDX_Control(pDX, IDC_SC_H, m_editH);
	DDX_Control(pDX, IDC_SC_CROP_L, m_cropLabel);
	DDX_Control(pDX, IDC_SC_SX, m_editSX);
	DDX_Control(pDX, IDC_SC_SY, m_editSY);
	DDX_Control(pDX, IDC_SC_SW, m_editSW);
	DDX_Control(pDX, IDC_SC_SH, m_editSH);
	DDX_Control(pDX, IDC_SC_CROP_FULL, m_cropFull);
	DDX_Control(pDX, IDC_SC_APPLYGEO, m_applyGeo);
	DDX_Control(pDX, IDC_SC_FIT, m_fit);
	DDX_Control(pDX, IDC_SC_SCALE50, m_scale50);
	DDX_Control(pDX, IDC_SC_SCALE100, m_scale100);
	DDX_Control(pDX, IDC_SC_TILE, m_tile);
	DDX_Control(pDX, IDC_SC_PATH_L, m_pathLabel);
	DDX_Control(pDX, IDC_SC_PATH, m_path);
	DDX_Control(pDX, IDC_SC_BROWSE, m_browse);
	DDX_Control(pDX, IDC_SC_START, m_start);
	DDX_Control(pDX, IDC_SC_CLOSE, m_close);
	DDX_Control(pDX, IDC_SC_STATUS, m_status);
	DDX_Control(pDX, IDC_SC_TIME, m_time);
}

BEGIN_MESSAGE_MAP(CScreenCaptureDlg, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_SC_BROWSE, &CScreenCaptureDlg::OnBnClickedBrowse)
	ON_BN_CLICKED(IDC_SC_START, &CScreenCaptureDlg::OnBnClickedStart)
	ON_BN_CLICKED(IDC_SC_CLOSE, &CScreenCaptureDlg::OnBnClickedClose)
	ON_BN_CLICKED(IDC_SC_HELP, &CScreenCaptureDlg::OnBnClickedHelp)
	ON_BN_CLICKED(IDC_SC_REFRESH, &CScreenCaptureDlg::OnBnClickedRefresh)
	ON_BN_CLICKED(IDC_SC_ADD, &CScreenCaptureDlg::OnBnClickedAdd)
	ON_BN_CLICKED(IDC_SC_REMOVE, &CScreenCaptureDlg::OnBnClickedRemove)
	ON_BN_CLICKED(IDC_SC_ZUP, &CScreenCaptureDlg::OnBnClickedZUp)
	ON_BN_CLICKED(IDC_SC_ZDOWN, &CScreenCaptureDlg::OnBnClickedZDown)
	ON_BN_CLICKED(IDC_SC_PICK, &CScreenCaptureDlg::OnBnClickedPick)
	ON_BN_CLICKED(IDC_SC_APPLYGEO, &CScreenCaptureDlg::OnBnClickedApplyGeo)
	ON_BN_CLICKED(IDC_SC_FIT, &CScreenCaptureDlg::OnBnClickedFit)
	ON_BN_CLICKED(IDC_SC_SCALE50, &CScreenCaptureDlg::OnBnClickedScale50)
	ON_BN_CLICKED(IDC_SC_SCALE100, &CScreenCaptureDlg::OnBnClickedScale100)
	ON_BN_CLICKED(IDC_SC_TILE, &CScreenCaptureDlg::OnBnClickedTile)
	ON_BN_CLICKED(IDC_SC_INCMP, &CScreenCaptureDlg::OnBnClickedIncludeMp)
	ON_BN_CLICKED(IDC_SC_CURSOR, &CScreenCaptureDlg::OnBnClickedShowCursor)
	ON_BN_CLICKED(IDC_SC_LIVE, &CScreenCaptureDlg::OnBnClickedLive)
	ON_BN_CLICKED(IDC_SC_LIVE_CFG, &CScreenCaptureDlg::OnBnClickedLiveCfg)
	ON_BN_CLICKED(IDC_SC_MIC, &CScreenCaptureDlg::OnBnClickedMic)
	ON_CBN_SELCHANGE(IDC_SC_MICDEV, &CScreenCaptureDlg::OnCbnSelchangeMicDev)
	ON_CBN_SELCHANGE(IDC_SC_LOOPDEV, &CScreenCaptureDlg::OnCbnSelchangeLoopDev)
	ON_CBN_SELCHANGE(IDC_SC_MODE, &CScreenCaptureDlg::OnCbnSelchangeMode)
	ON_CBN_SELCHANGE(IDC_SC_CANVAS, &CScreenCaptureDlg::OnCbnSelchangeCanvas)
	ON_CBN_SELCHANGE(IDC_SC_FPS, &CScreenCaptureDlg::OnCbnSelchangeFps)
	ON_CBN_SELCHANGE(IDC_SC_EFFECT, &CScreenCaptureDlg::OnCbnSelchangeEffect)
	ON_BN_CLICKED(IDC_SC_FXPRE_LOAD, &CScreenCaptureDlg::OnBnClickedFxPreLoad)
	ON_BN_CLICKED(IDC_SC_FXPRE_SAVE, &CScreenCaptureDlg::OnBnClickedFxPreSave)
	ON_CBN_SELCHANGE(IDC_SC_FXPRE, &CScreenCaptureDlg::OnCbnSelchangeFxPre)
	ON_BN_CLICKED(IDC_SC_CROP_FULL, &CScreenCaptureDlg::OnBnClickedCropFull)
	ON_LBN_SELCHANGE(IDC_SC_LAYER, &CScreenCaptureDlg::OnLbnSelchangeLayer)
	ON_WM_LBUTTONDOWN()
	ON_WM_TIMER()
	ON_WM_SIZE()
	ON_MESSAGE(WM_DPICHANGED, &CScreenCaptureDlg::OnDpiChanged)
	ON_WM_DESTROY()
END_MESSAGE_MAP()

void CScreenCaptureDlg::RefreshOpaqueUi()
{
	if (!GetSafeHwnd()) return;
	PostMessage(CCC_MSG_REAPPLY_OPAQUE_FIXERS, 0, 0);
	if (m_path.GetSafeHwnd()) m_path.RepaintClient();
	if (m_fps.GetSafeHwnd()) m_fps.Invalidate(FALSE);
	if (m_effect.GetSafeHwnd()) m_effect.Invalidate(FALSE);
	if (m_fxPre.GetSafeHwnd()) m_fxPre.Invalidate(FALSE);
	if (m_fxWire.GetSafeHwnd()) m_fxWire.Invalidate(FALSE);
	if (m_mode.GetSafeHwnd()) m_mode.Invalidate(FALSE);
	if (m_canvas.GetSafeHwnd()) m_canvas.Invalidate(FALSE);
}

CString CScreenCaptureDlg::NormalizeOutPath(const CString& pathIn) const
{
	CString p = pathIn;
	p.Trim();
	if (p.IsEmpty()) return p;
	const CString ext = L".mp4";
	const int dot = p.ReverseFind(L'.');
	const int slash = (p.ReverseFind(L'\\') > p.ReverseFind(L'/')) ? p.ReverseFind(L'\\') : p.ReverseFind(L'/');
	if (dot > slash)
		p = p.Left(dot) + ext;
	else
		p += ext;
	return p;
}

CString CScreenCaptureDlg::DefaultCaptureOutPath() const
{
	wchar_t docs[MAX_PATH] = {};
	CString dir;
	if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_MYVIDEO, NULL, 0, docs)))
		dir = docs;
	if (dir.IsEmpty()) {
		// 前回パスのフォルダを流用
		CString prev = savedata.cap_last_path;
		const int slash = (prev.ReverseFind(L'\\') > prev.ReverseFind(L'/')) ? prev.ReverseFind(L'\\') : prev.ReverseFind(L'/');
		if (slash > 0)
			dir = prev.Left(slash);
	}
	if (dir.IsEmpty())
		dir = L".";
	SYSTEMTIME st = {};
	GetLocalTime(&st);
	CString path;
	path.Format(L"%s\\capture_%04d%02d%02d_%02d%02d%02d.mp4",
		(LPCTSTR)dir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
	return path;
}

CString CScreenCaptureDlg::RefreshCaptureOutPathTimestamp(const CString& pathIn) const
{
	CString dir;
	CString p = pathIn;
	p.Trim();
	if (!p.IsEmpty()) {
		const int slash = (p.ReverseFind(L'\\') > p.ReverseFind(L'/')) ? p.ReverseFind(L'\\') : p.ReverseFind(L'/');
		if (slash > 0)
			dir = p.Left(slash);
	}
	if (dir.IsEmpty())
		return DefaultCaptureOutPath();
	SYSTEMTIME st = {};
	GetLocalTime(&st);
	CString path;
	path.Format(L"%s\\capture_%04d%02d%02d_%02d%02d%02d.mp4",
		(LPCTSTR)dir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
	return path;
}

namespace {
struct ScMonEnumCtx {
	HMONITOR mons[CScreenCaptureDlg::SC_MON_MAX];
	int cnt;
};
static BOOL CALLBACK ScEnumMonProc(HMONITOR hMon, HDC, LPRECT, LPARAM lp)
{
	ScMonEnumCtx* ctx = (ScMonEnumCtx*)lp;
	if (!ctx || ctx->cnt >= CScreenCaptureDlg::SC_MON_MAX) return FALSE;
	ctx->mons[ctx->cnt++] = hMon;
	return TRUE;
}
} // namespace

BOOL CScreenCaptureDlg::ResolveSelectedMonitorRect(int monIdx, RECT& outRc, HMONITOR* outMon) const
{
	memset(&outRc, 0, sizeof(outRc));
	if (outMon) *outMon = NULL;
	POINT ptZero = {};
	HMONITOR primary = MonitorFromPoint(ptZero, MONITOR_DEFAULTTOPRIMARY);
	if (monIdx < 0) {
		MONITORINFO mi = {};
		mi.cbSize = sizeof(mi);
		if (primary && GetMonitorInfo(primary, &mi)) {
			outRc = mi.rcMonitor;
			if (outMon) *outMon = primary;
			return TRUE;
		}
		outRc.left = 0;
		outRc.top = 0;
		outRc.right = GetSystemMetrics(SM_CXSCREEN);
		outRc.bottom = GetSystemMetrics(SM_CYSCREEN);
		if (outMon) *outMon = primary;
		return TRUE;
	}
	if (monIdx >= 0 && monIdx < m_monCnt) {
		HMONITOR mon = m_monHandles[monIdx];
		MONITORINFO mi = {};
		mi.cbSize = sizeof(mi);
		if (mon && GetMonitorInfo(mon, &mi)) {
			outRc = mi.rcMonitor;
			if (outMon) *outMon = mon;
			return TRUE;
		}
	}
	return FALSE;
}

void CScreenCaptureDlg::RefreshModeCombo()
{
	if (!m_mode.GetSafeHwnd()) return;
	const int keepMode = savedata.cap_mode;
	const int keepMon = savedata.cap_monitor_idx;

	ScMonEnumCtx ctx = {};
	EnumDisplayMonitors(NULL, NULL, ScEnumMonProc, (LPARAM)&ctx);
	m_monCnt = ctx.cnt;
	for (int i = 0; i < m_monCnt; ++i)
		m_monHandles[i] = ctx.mons[i];

	m_mode.ResetContent();
	m_modeComboCnt = 0;
	auto addEntry = [&](int savedMode, int monIdx, const CString& text) {
		if (m_modeComboCnt >= (int)_countof(m_modeComboMap)) return;
		m_mode.AddString(text);
		m_modeComboMap[m_modeComboCnt] = savedMode;
		m_modeComboMonIdx[m_modeComboCnt] = monIdx;
		m_modeComboCnt++;
	};

	addEntry(SC_MODE_PRIMARY, 0, LL14(L"プライマリ画面", L"Primary screen", L"Écran principal", L"Schermo principale",
		L"Pantalla principal", L"기본 화면", L"主屏幕", L"الشاشة الرئيسية",
		L"Основной экран", L"Primärer Bildschirm", L"Ecrã principal", L"Primair scherm",
		L"Ekran główny", L"Birincil ekran"));

	POINT ptZero = {};
	HMONITOR primary = MonitorFromPoint(ptZero, MONITOR_DEFAULTTOPRIMARY);
	int subNum = 2;
	for (int i = 0; i < m_monCnt; ++i) {
		if (m_monHandles[i] == primary) continue;
		MONITORINFOEX mi = {};
		mi.cbSize = sizeof(mi);
		if (!GetMonitorInfo(m_monHandles[i], &mi)) continue;
		const int mw = mi.rcMonitor.right - mi.rcMonitor.left;
		const int mh = mi.rcMonitor.bottom - mi.rcMonitor.top;
		CString label;
		label.Format(LL14(L"モニタ%d (%dx%d)", L"Monitor %d (%dx%d)", L"Moniteur %d (%dx%d)", L"Monitor %d (%dx%d)",
			L"Monitor %d (%dx%d)", L"모니터 %d (%dx%d)", L"显示器%d (%dx%d)", L"شاشة %d (%dx%d)",
			L"Монитор %d (%dx%d)", L"Monitor %d (%dx%d)", L"Monitor %d (%dx%d)", L"Monitor %d (%dx%d)",
			L"Monitor %d (%dx%d)", L"Monitör %d (%dx%d)"),
			subNum, mw, mh);
		addEntry(SC_MODE_MONITOR, i, label);
		subNum++;
	}

	addEntry(SC_MODE_VIRTUAL, 0, LL14(L"全モニタ (仮想)", L"All monitors (virtual)", L"Tous les moniteurs", L"Tutti i monitor",
		L"Todos los monitores", L"모든 모니터", L"全部显示器", L"كل الشاشات",
		L"Все мониторы", L"Alle Monitore", L"Todos os monitores", L"Alle monitoren",
		L"Wszystkie monitory", L"Tüm monitörler"));
	addEntry(SC_MODE_WINDOWS, 0, LL14(L"ウィンドウ合成", L"Window compose", L"Composition fenêtres", L"Composizione finestre",
		L"Composición de ventanas", L"창 합성", L"窗口合成", L"تركيب النوافذ",
		L"Композиция окон", L"Fenster-Komposition", L"Composição de janelas", L"Venstercompositie",
		L"Kompozycja okien", L"Pencere kompozisyonu"));

	m_mode.SetCurSel(SavedModeToComboSel(keepMode, keepMon));
}

int CScreenCaptureDlg::ModeComboToSavedMode(int comboSel, int& outMonIdx) const
{
	outMonIdx = 0;
	if (comboSel < 0 || comboSel >= m_modeComboCnt) return SC_MODE_PRIMARY;
	outMonIdx = m_modeComboMonIdx[comboSel];
	return m_modeComboMap[comboSel];
}

int CScreenCaptureDlg::SavedModeToComboSel(int mode, int monIdx) const
{
	for (int i = 0; i < m_modeComboCnt; ++i) {
		if (m_modeComboMap[i] != mode) continue;
		if (mode == SC_MODE_MONITOR && m_modeComboMonIdx[i] != monIdx) continue;
		return i;
	}
	return 0;
}

BOOL CScreenCaptureDlg::IsWindowComposeMode() const
{
	int monIdx = 0;
	return ModeComboToSavedMode(m_mode.GetCurSel(), monIdx) == SC_MODE_WINDOWS;
}

void CScreenCaptureDlg::ResolveCanvasSize(int& outW, int& outH) const
{
	const int preset = m_canvas.GetCurSel();
	if (preset == 1) { outW = 1280; outH = 720; return; }
	if (preset == 2) { outW = 1920; outH = 1080; return; }
	if (preset == 3) { outW = 1600; outH = 900; return; }
	if (preset == 4) { outW = 3840; outH = 2160; return; }

	// 0=自動: 選択中のキャプチャ対象の実解像度（4K も維持。旧:常に1920へ縮小）
	int monIdx = 0;
	const int mode = ModeComboToSavedMode(m_mode.GetCurSel(), monIdx);
	if (mode == SC_MODE_VIRTUAL) {
		outW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
		outH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
	} else if (mode == SC_MODE_MONITOR) {
		RECT rc = {};
		if (ResolveSelectedMonitorRect(monIdx, rc, NULL)) {
			outW = rc.right - rc.left;
			outH = rc.bottom - rc.top;
		} else {
			outW = GetSystemMetrics(SM_CXSCREEN);
			outH = GetSystemMetrics(SM_CYSCREEN);
		}
	} else {
		outW = GetSystemMetrics(SM_CXSCREEN);
		outH = GetSystemMetrics(SM_CYSCREEN);
	}
	outW &= ~1; outH &= ~1;
	if (outW < 160) outW = 160;
	if (outH < 120) outH = 120;
	if (outW > 7680) outW = 7680;
	if (outH > 4320) outH = 4320;
}

void CScreenCaptureDlg::FitLayerIntoCanvas(Layer& L, int cw, int ch) const
{
	if (cw < 2) cw = 2;
	if (ch < 2) ch = 2;
	int nw = L.w, nh = L.h;
	if (L.hwnd && ::IsWindow(L.hwnd)) {
		RECT wr = {};
		if (::GetWindowRect(L.hwnd, &wr)) {
			nw = wr.right - wr.left;
			nh = wr.bottom - wr.top;
		}
	}
	if (nw < 2) nw = 2;
	if (nh < 2) nh = 2;
	// キャンバスに収まるよう縮小（拡大はしない）
	if (nw > cw || nh > ch) {
		const double sx = (double)cw / (double)nw;
		const double sy = (double)ch / (double)nh;
		const double s = (sx < sy) ? sx : sy;
		L.w = (int)(nw * s);
		L.h = (int)(nh * s);
	} else {
		L.w = nw;
		L.h = nh;
	}
	if (L.w < 2) L.w = 2;
	if (L.h < 2) L.h = 2;
	if (L.w > cw) L.w = cw;
	if (L.h > ch) L.h = ch;
	L.x = (cw - L.w) / 2;
	L.y = (ch - L.h) / 2;
	if (L.x < 0) L.x = 0;
	if (L.y < 0) L.y = 0;
}

// ユーザ配置を維持したままキャンバス内へ収める（中央寄せしない）
void CScreenCaptureDlg::ClampLayerToCanvas(Layer& L, int cw, int ch) const
{
	if (cw < 2) cw = 2;
	if (ch < 2) ch = 2;
	if (L.w < 2) L.w = 2;
	if (L.h < 2) L.h = 2;
	if (L.w > cw) L.w = cw;
	if (L.h > ch) L.h = ch;
	if (L.x + L.w > cw) L.x = cw - L.w;
	if (L.y + L.h > ch) L.y = ch - L.h;
	if (L.x < 0) L.x = 0;
	if (L.y < 0) L.y = 0;
}

void CScreenCaptureDlg::FitAllLayersIntoCanvas()
{
	int cw = 0, ch = 0;
	ResolveCanvasSize(cw, ch);
	for (int i = 0; i < m_layerCnt; ++i) {
		if (m_layers[i].isMp)
			EnsureMpDefaultRect(m_layers[i]);
		else
			FitLayerIntoCanvas(m_layers[i], cw, ch);
	}
	RefreshLayerList();
	SyncGeoEditsFromSel();
	UpdatePreview(TRUE);
}

void CScreenCaptureDlg::ClampAllLayersToCanvas()
{
	int cw = 0, ch = 0;
	ResolveCanvasSize(cw, ch);
	for (int i = 0; i < m_layerCnt; ++i)
		ClampLayerToCanvas(m_layers[i], cw, ch);
}

void CScreenCaptureDlg::BuildComposeSnap(ComposeSnap& out) const
{
	memset(&out, 0, sizeof(out));
	int monIdx = 0;
	int mode = ModeComboToSavedMode(m_mode.GetCurSel(), monIdx);
	out.mode = mode;
	ResolveCanvasSize(out.canvasW, out.canvasH);
	out.layerCnt = 0;
	out.excludeHwnd = GetSafeHwnd();
	const BOOL wantMp = (m_includeMp.GetSafeHwnd()
		&& const_cast<CCustomCheckBox&>(m_includeMp).GetCheck());
	out.includeMp = wantMp;
	out.showCursor = (m_showCursor.GetSafeHwnd()
		&& const_cast<CCustomCheckBox&>(m_showCursor).GetCheck()) ? TRUE : FALSE;
	out.mpHidden = FALSE;
	out.mpHwnd = FindMediaPlayerHwnd();

	RECT monRc = {};
	HMONITOR mon = NULL;
	if (mode == SC_MODE_MONITOR)
		ResolveSelectedMonitorRect(monIdx, monRc, &mon);
	else if (mode == SC_MODE_PRIMARY)
		ResolveSelectedMonitorRect(-1, monRc, &mon);
	out.monL = monRc.left;
	out.monT = monRc.top;
	out.monR = monRc.right;
	out.monB = monRc.bottom;
	out.monHandle = mon;
	out.fxN = 0;
	out.fxTime = (float)(GetTickCount() % 600000) / 1000.f;
	memset(out.fx, 0, sizeof(out.fx));
	memset(out.fxStr, SC_FX_STR_DEF, sizeof(out.fxStr));
	{
		int chain[SC_FX_CHAIN_MAX] = {};
		BYTE str[SC_FX_CHAIN_MAX][SC_FX_STR_N] = {};
		int cn = 0;
		GetFxChain(chain, &cn, str);
		out.fxN = cn;
		for (int i = 0; i < cn; ++i) {
			out.fx[i] = chain[i];
			memcpy(out.fxStr[i], str[i], SC_FX_STR_N);
		}
	}

	if (mode == SC_MODE_WINDOWS) {
		for (int i = 0; i < m_layerCnt && out.layerCnt < SC_LAYER_MAX; ++i) {
			if (!m_layers[i].hwnd || !::IsWindow(m_layers[i].hwnd)) continue;
			if (ScIsExcludedHwnd(m_layers[i].hwnd, out.excludeHwnd)) continue;
			Layer L = m_layers[i];
			// キャンバス外にはみ出す配置は収まるよう縮小（録画見切れ防止）
			if (L.w > out.canvasW || L.h > out.canvasH
				|| L.x < 0 || L.y < 0
				|| L.x + L.w > out.canvasW || L.y + L.h > out.canvasH) {
				const int nw = L.w, nh = L.h;
				double sx = (double)out.canvasW / (double)((nw > 0) ? nw : 1);
				double sy = (double)out.canvasH / (double)((nh > 0) ? nh : 1);
				double s = (sx < sy) ? sx : sy;
				if (s > 1.0) s = 1.0;
				if (L.x < 0 || L.y < 0 || L.x + L.w > out.canvasW || L.y + L.h > out.canvasH
					|| L.w > out.canvasW || L.h > out.canvasH) {
					if (L.w > out.canvasW || L.h > out.canvasH) {
						L.w = (int)(nw * s);
						L.h = (int)(nh * s);
						if (L.w < 2) L.w = 2;
						if (L.h < 2) L.h = 2;
					}
					if (L.x + L.w > out.canvasW) L.x = out.canvasW - L.w;
					if (L.y + L.h > out.canvasH) L.y = out.canvasH - L.h;
					if (L.x < 0) L.x = 0;
					if (L.y < 0) L.y = 0;
				}
			}
			out.layers[out.layerCnt] = L;
			out.layerCnt++;
		}
	} else if (wantMp) {
		// 画面全体モードでも MP レイヤ配置を合成に載せる
		for (int i = 0; i < m_layerCnt; ++i) {
			if (!m_layers[i].isMp) continue;
			if (!m_layers[i].hwnd || !::IsWindow(m_layers[i].hwnd)) continue;
			out.mpHwnd = m_layers[i].hwnd;
			out.mpX = m_layers[i].x;
			out.mpY = m_layers[i].y;
			out.mpW = m_layers[i].w;
			out.mpH = m_layers[i].h;
			out.mpSrcX = m_layers[i].srcX;
			out.mpSrcY = m_layers[i].srcY;
			out.mpSrcW = m_layers[i].srcW;
			out.mpSrcH = m_layers[i].srcH;
			out.mpHidden = m_layers[i].hidden;
			break;
		}
		if (out.mpW < 2 && out.mpHwnd) {
			RECT wr = {};
			::GetWindowRect(out.mpHwnd, &wr);
			out.mpW = (wr.right - wr.left) / 2;
			out.mpH = (wr.bottom - wr.top) / 2;
			if (out.mpW < 160) out.mpW = 320;
			if (out.mpH < 120) out.mpH = 240;
			out.mpX = out.canvasW - out.mpW - 16;
			out.mpY = out.canvasH - out.mpH - 16;
			if (out.mpX < 0) out.mpX = 0;
			if (out.mpY < 0) out.mpY = 0;
		}
	}

	if (wantMp && out.mpHwnd && mode == SC_MODE_WINDOWS) {
		for (int i = 0; i < out.layerCnt; ++i) {
			if (out.layers[i].hwnd == out.mpHwnd || out.layers[i].isMp) {
				out.mpX = out.layers[i].x;
				out.mpY = out.layers[i].y;
				out.mpW = out.layers[i].w;
				out.mpH = out.layers[i].h;
				out.mpSrcX = out.layers[i].srcX;
				out.mpSrcY = out.layers[i].srcY;
				out.mpSrcW = out.layers[i].srcW;
				out.mpSrcH = out.layers[i].srcH;
				out.mpHidden = out.layers[i].hidden;
				break;
			}
		}
	}
}

void CScreenCaptureDlg::GetFxChain(int* fxOut, int* nOut, BYTE strOut[][8]) const
{
	if (m_fxWire.GetSafeHwnd()) {
		m_fxWire.GetChain(fxOut, nOut, strOut);
		return;
	}
	int n = savedata.cap_fx_n;
	if (n < 0) n = 0;
	if (n > SC_FX_CHAIN_MAX) n = SC_FX_CHAIN_MAX;
	const int src[SC_FX_CHAIN_MAX] = {
		savedata.cap_fx0, savedata.cap_fx1, savedata.cap_fx2, savedata.cap_fx3,
		savedata.cap_fx4, savedata.cap_fx5, savedata.cap_fx6, savedata.cap_fx7
	};
	int cn = 0;
	int tmp[SC_FX_CHAIN_MAX] = {};
	BYTE tmpStr[SC_FX_CHAIN_MAX][SC_FX_STR_N];
	memset(tmpStr, SC_FX_STR_DEF, sizeof(tmpStr));
	for (int i = 0; i < n; ++i) {
		if (src[i] > SC_FX_NONE && src[i] < SC_FX_COUNT) {
			tmp[cn] = src[i];
			memcpy(tmpStr[cn], savedata.cap_fx_str[i], SC_FX_STR_N);
			cn++;
		}
	}
	if (cn <= 0 && savedata.cap_effect > 0 && savedata.cap_effect < SC_FX_COUNT) {
		tmp[0] = savedata.cap_effect;
		cn = 1;
	}
	if (nOut) *nOut = cn;
	if (fxOut) {
		for (int i = 0; i < SC_FX_CHAIN_MAX; ++i)
			fxOut[i] = (i < cn) ? tmp[i] : 0;
	}
	if (strOut) {
		for (int i = 0; i < SC_FX_CHAIN_MAX; ++i)
			memcpy(strOut[i], tmpStr[i], SC_FX_STR_N);
	}
}

int CScreenCaptureDlg::FxParamCount(int fx) const
{
	switch (fx) {
	case SC_FX_BLUR_SOFT: case SC_FX_BLUR_STRONG: case SC_FX_BLUR_MEGA: return 1;
	case SC_FX_WAVE: case SC_FX_UNDERWATER: case SC_FX_HEAT_HAZE: case SC_FX_RIPPLE: return 3;
	case SC_FX_GLITCH: return 3;
	case SC_FX_PIXELATE: case SC_FX_NOISE: return 2;
	case SC_FX_VIGNETTE: case SC_FX_SPOTLIGHT: case SC_FX_FISHEYE: case SC_FX_SWIRL: case SC_FX_VORTEX: return 2;
	case SC_FX_BLOOM: case SC_FX_GODRAYS: case SC_FX_MOTION_BLUR: case SC_FX_RADIAL_BLUR: case SC_FX_ZOOM_BLUR: return 2;
	case SC_FX_OIL: return 5;
	case SC_FX_WATERCOLOR: return 4;
	case SC_FX_DREAM: return 4;
	case SC_FX_PENCIL: return 3;
	case SC_FX_KALEIDO: case SC_FX_CRT_CURVE: case SC_FX_CHROMA: case SC_FX_CHROMA_HEAVY: return 2;
	case SC_FX_FOG: case SC_FX_DISPLACE: case SC_FX_POSTER: case SC_FX_COMIC: return 2;
	case SC_FX_NONE: return 0;
	default: return 1; // 色調系など S1=効き
	}
}

CString CScreenCaptureDlg::FxParamName(int fx, int si) const
{
	auto L = [](const wchar_t* ja, const wchar_t* en) -> CString {
		return LL14(ja, en, en, en, en, en, en, en, en, en, en, en, en, en);
	};
	if (si < 0 || si >= SC_FX_STR_N) return L(L"?", L"?");
	switch (fx) {
	case SC_FX_WAVE: case SC_FX_UNDERWATER: case SC_FX_HEAT_HAZE: case SC_FX_RIPPLE:
		if (si == 0) return L(L"振幅", L"Amplitude");
		if (si == 1) return L(L"速度", L"Speed");
		if (si == 2) return L(L"周波数", L"Frequency");
		break;
	case SC_FX_BLUR_SOFT: case SC_FX_BLUR_STRONG: case SC_FX_BLUR_MEGA:
		if (si == 0) return L(L"強さ", L"Amount");
		break;
	case SC_FX_MOTION_BLUR: case SC_FX_RADIAL_BLUR: case SC_FX_ZOOM_BLUR:
		if (si == 0) return L(L"強さ", L"Amount");
		if (si == 1) return L(L"広がり", L"Spread");
		break;
	case SC_FX_PIXELATE:
		if (si == 0) return L(L"ブロック", L"Block size");
		if (si == 1) return L(L"効き", L"Amount");
		break;
	case SC_FX_NOISE:
		if (si == 0) return L(L"量", L"Amount");
		if (si == 1) return L(L"速度", L"Speed");
		break;
	case SC_FX_GLITCH:
		if (si == 0) return L(L"ずれ", L"Shift");
		if (si == 1) return L(L"速度", L"Speed");
		if (si == 2) return L(L"頻度", L"Frequency");
		break;
	case SC_FX_OIL:
		if (si == 0) return L(L"筆の長さ", L"Stroke length");
		if (si == 1) return L(L"筆の間隔", L"Stroke spacing");
		if (si == 2) return L(L"区画", L"Cell size");
		if (si == 3) return L(L"色面", L"Color steps");
		if (si == 4) return L(L"筆跡", L"Stroke mark");
		break;
	case SC_FX_WATERCOLOR:
		if (si == 0) return L(L"滲み", L"Bleed");
		if (si == 1) return L(L"洗い", L"Wash");
		if (si == 2) return L(L"紙目", L"Paper");
		if (si == 3) return L(L"縁取り", L"Edge");
		break;
	case SC_FX_DREAM:
		if (si == 0) return L(L"ブルーム", L"Bloom");
		if (si == 1) return L(L"広がり", L"Spread");
		if (si == 2) return L(L"半径", L"Radius");
		if (si == 3) return L(L"パステル", L"Pastel");
		break;
	case SC_FX_PENCIL:
		if (si == 0) return L(L"線の強さ", L"Line");
		if (si == 1) return L(L"ハッチ1", L"Hatch 1");
		if (si == 2) return L(L"ハッチ2", L"Hatch 2");
		break;
	case SC_FX_GODRAYS: case SC_FX_BLOOM:
		if (si == 0) return L(L"強さ", L"Amount");
		if (si == 1) return L(L"広がり", L"Spread");
		break;
	case SC_FX_VIGNETTE: case SC_FX_SPOTLIGHT: case SC_FX_FOG:
		if (si == 0) return L(L"強さ", L"Amount");
		if (si == 1) return L(L"範囲", L"Range");
		break;
	case SC_FX_FISHEYE:
		if (si == 0) return L(L"歪み", L"Distortion");
		if (si == 1) return L(L"カーブ", L"Curve");
		break;
	case SC_FX_SWIRL: case SC_FX_VORTEX:
		if (si == 0) return L(L"回転", L"Twist");
		if (si == 1) return L(L"範囲", L"Range");
		break;
	case SC_FX_KALEIDO:
		if (si == 0) return L(L"分割", L"Segments");
		if (si == 1) return L(L"効き", L"Amount");
		break;
	case SC_FX_CRT_CURVE:
		if (si == 0) return L(L"湾曲", L"Curve");
		if (si == 1) return L(L"走査線", L"Scanline");
		break;
	case SC_FX_CHROMA: case SC_FX_CHROMA_HEAVY:
		if (si == 0) return L(L"ずれ幅", L"Offset");
		if (si == 1) return L(L"効き", L"Amount");
		break;
	case SC_FX_POSTER:
		if (si == 0) return L(L"効き", L"Amount");
		if (si == 1) return L(L"階調", L"Levels");
		break;
	case SC_FX_COMIC:
		if (si == 0) return L(L"効き", L"Amount");
		if (si == 1) return L(L"線の強さ", L"Ink");
		break;
	case SC_FX_DISPLACE:
		if (si == 0) return L(L"変位量", L"Displace");
		if (si == 1) return L(L"速度", L"Speed");
		break;
	case SC_FX_INTERLACE:
		if (si == 0) return L(L"ずれ", L"Shift");
		break;
	case SC_FX_SHARPEN: case SC_FX_SHARPEN_HEAVY:
		if (si == 0) return L(L"強さ", L"Amount");
		break;
	default: break;
	}
	if (si == 0) return L(L"効き", L"Amount");
	if (si == 1) return L(L"補助1", L"Param 2");
	if (si == 2) return L(L"補助2", L"Param 3");
	if (si == 3) return L(L"補助3", L"Param 4");
	if (si == 4) return L(L"補助4", L"Param 5");
	if (si == 5) return L(L"補助5", L"Param 6");
	if (si == 6) return L(L"補助6", L"Param 7");
	return L(L"補助7", L"Param 8");
}

CString CScreenCaptureDlg::FxName(int fx) const
{
	switch (fx) {
	case SC_FX_BLUR_SOFT:
		return LL14(L"ぼかし弱", L"Soft blur", L"Flou léger", L"Sfocatura soft",
			L"Desenfoque suave", L"약한 흐림", L"轻模糊", L"ضباب خفيف",
			L"Лёгкое размытие", L"Weich (schwach)", L"Desfoque suave", L"Zachte blur",
			L"Lekkie rozmycie", L"Hafif bulanık");
	case SC_FX_BLUR_STRONG:
		return LL14(L"ぼかし強", L"Strong blur", L"Flou fort", L"Sfocatura forte",
			L"Desenfoque fuerte", L"강한 흐림", L"强模糊", L"ضباب قوي",
			L"Сильное размытие", L"Weich (stark)", L"Desfoque forte", L"Sterke blur",
			L"Silne rozmycie", L"Güçlü bulanık");
	case SC_FX_GRAY:
		return LL14(L"モノクロ", L"Monochrome", L"Monochrome", L"Monocromatico",
			L"Monocromo", L"모노크롬", L"单色", L"أحادي اللون",
			L"Монохром", L"Monochrom", L"Monocromático", L"Monochroom",
			L"Monochromia", L"Monokrom");
	case SC_FX_SEPIA:
		return LL14(L"セピア", L"Sepia", L"Sépia", L"Seppia",
			L"Sepia", L"세피아", L"棕褐", L"سيبيا",
			L"Сепия", L"Sepia", L"Sépia", L"Sepia",
			L"Sepia", L"Sepya");
	case SC_FX_VIGNETTE:
		return LL14(L"ビネット", L"Vignette", L"Vignette", L"Vignettatura",
			L"Viñeta", L"비네트", L"暗角", L"تظليل الحواف",
			L"Виньетка", L"Vignette", L"Vinheta", L"Vignet",
			L"Winieta", L"Vinyet");
	case SC_FX_SHARPEN:
		return LL14(L"シャープ", L"Sharpen", L"Netteté", L"Nitidezza",
			L"Nitidez", L"샤픈", L"锐化", L"حدة",
			L"Резкость", L"Schärfen", L"Nitidez", L"Verscherpen",
			L"Wyostrzenie", L"Keskinleştir");
	case SC_FX_MIRROR:
		return LL14(L"左右反転", L"Mirror", L"Miroir", L"Specchio",
			L"Espejo", L"좌우반전", L"左右镜像", L"مرآة",
			L"Зеркало", L"Spiegeln", L"Espelho", L"Spiegelen",
			L"Odbicie", L"Ayna");
	case SC_FX_WAVE:
		return LL14(L"波", L"Wave", L"Vague", L"Onda",
			L"Onda", L"파도", L"波浪", L"موجة",
			L"Волна", L"Welle", L"Onda", L"Golf",
			L"Fala", L"Dalga");
	case SC_FX_UNDERWATER:
		return LL14(L"水中", L"Underwater", L"Sous-marin", L"Sott'acqua",
			L"Bajo el agua", L"수중", L"水下", L"تحت الماء",
			L"Под водой", L"Unterwasser", L"Subaquático", L"Onderwater",
			L"Pod wodą", L"Su altı");
	case SC_FX_DUSK:
		return LL14(L"夕暮れ", L"Dusk", L"Crépuscule", L"Crepuscolo",
			L"Atardecer", L"황혼", L"黄昏", L"غروب",
			L"Сумерки", L"Dämmerung", L"Crepúsculo", L"Schemering",
			L"Zmierzch", L"Alacakaranlık");
	case SC_FX_COOL:
		return LL14(L"クール", L"Cool tone", L"Tons froids", L"Toni freddi",
			L"Tono frío", L"쿨톤", L"冷色", L"درجات باردة",
			L"Холодный тон", L"Kühle Töne", L"Tom frio", L"Koele tint",
			L"Zimny ton", L"Soğuk ton");
	case SC_FX_WARM:
		return LL14(L"ウォーム", L"Warm tone", L"Tons chauds", L"Toni caldi",
			L"Tono cálido", L"웜톤", L"暖色", L"درجات دافئة",
			L"Тёплый тон", L"Warme Töne", L"Tom quente", L"Warme tint",
			L"Ciepły ton", L"Sıcak ton");
	case SC_FX_POSTER:
		return LL14(L"ポスタライズ", L"Posterize", L"Postériser", L"Posterizza",
			L"Posterizar", L"포스터화", L"色调分离", L"ملصق",
			L"Постеризация", L"Posterisieren", L"Posterizar", L"Posteriseren",
			L"Posteryzacja", L"Posterize");
	case SC_FX_SCANLINE:
		return LL14(L"走査線", L"Scanlines", L"Lignes de balayage", L"Linee di scansione",
			L"Líneas de barrido", L"스캔라인", L"扫描线", L"خطوط المسح",
			L"Строки развёртки", L"Abtastzeilen", L"Linhas de varredura", L"Aftastlijnen",
			L"Linie skanowania", L"Tarama çizgileri");
	case SC_FX_EDGE:
		return LL14(L"エッジ", L"Edge detect", L"Contours", L"Bordi",
			L"Bordes", L"에지", L"边缘", L"حواف",
			L"Контуры", L"Kanten", L"Bordas", L"Randen",
			L"Krawędzie", L"Kenar");
	case SC_FX_INVERT:
		return LL14(L"ネガ", L"Invert", L"Négatif", L"Inverti",
			L"Invertir", L"반전", L"反色", L"عكس",
			L"Негатив", L"Negativ", L"Inverter", L"Inverteren",
			L"Negatyw", L"Negatif");
	case SC_FX_SOLARIZE:
		return LL14(L"ソラリゼ", L"Solarize", L"Solarisation", L"Solarizza",
			L"Solarizar", L"솔라라이즈", L"曝光过度", L"تشميس",
			L"Соляризация", L"Solarisieren", L"Solarizar", L"Solariseren",
			L"Solaryzacja", L"Solarize");
	case SC_FX_PIXELATE:
		return LL14(L"モザイク", L"Pixelate", L"Pixeliser", L"Pixel",
			L"Pixelar", L"모자이크", L"马赛克", L"بكسلة",
			L"Пиксель", L"Verpixeln", L"Pixelizar", L"Verpixelen",
			L"Pikselizacja", L"Piksel");
	case SC_FX_FLIP_V:
		return LL14(L"上下反転", L"Flip V", L"Retournement V", L"Capovolgi V",
			L"Voltear V", L"상하반전", L"上下翻转", L"قلب عمودي",
			L"Отразить В", L"Vertikal spiegeln", L"Inverter V", L"Verticaal",
			L"Odwróć V", L"Dikey çevir");
	case SC_FX_NOISE:
		return LL14(L"ノイズ", L"Noise", L"Bruit", L"Rumore",
			L"Ruido", L"노이즈", L"噪点", L"ضوضاء",
			L"Шум", L"Rauschen", L"Ruído", L"Ruis",
			L"Szum", L"Gürültü");
	case SC_FX_BLOOM:
		return LL14(L"ブルーム", L"Bloom", L"Bloom", L"Bloom",
			L"Bloom", L"블룸", L"辉光", L"توهج",
			L"Свечение", L"Bloom", L"Bloom", L"Bloom",
			L"Bloom", L"Bloom");
	case SC_FX_NEON:
		return LL14(L"ネオン", L"Neon", L"Néon", L"Neon",
			L"Neón", L"네온", L"霓虹", L"نيون",
			L"Неон", L"Neon", L"Neon", L"Neon",
			L"Neon", L"Neon");
	case SC_FX_NIGHTVISION:
		return LL14(L"ナイトビジョン", L"Night vision", L"Vision nocturne", L"Visione notturna",
			L"Visión nocturna", L"야간투시", L"夜视", L"رؤية ليلية",
			L"ПНВ", L"Nachtsicht", L"Visão noturna", L"Nachtzicht",
			L"Noktowizja", L"Gece görüş");
	case SC_FX_COMIC:
		return LL14(L"コミック", L"Comic", L"Comic", L"Fumetto",
			L"Cómic", L"코믹", L"漫画风", L"كوميك",
			L"Комикс", L"Comic", L"HQ", L"Strip",
			L"Komiks", L"Çizgi roman");
	case SC_FX_RETRO:
		return LL14(L"レトロ", L"Retro", L"Rétro", L"Retro",
			L"Retro", L"레트로", L"复古", L"ريترو",
			L"Ретро", L"Retro", L"Retrô", L"Retro",
			L"Retro", L"Retro");
	case SC_FX_FISHEYE:
		return LL14(L"魚眼", L"Fisheye", L"Fish-eye", L"Fish-eye",
			L"Ojo de pez", L"어안", L"鱼眼", L"عين السمكة",
			L"Рыбий глаз", L"Fischauge", L"Olho de peixe", L"Visoog",
			L"Rybie oko", L"Balık gözü");
	case SC_FX_HUE_SHIFT:
		return LL14(L"色相シフト", L"Hue shift", L"Teinte", L"Tonalità",
			L"Matiz", L"색조 이동", L"色相偏移", L"إزاحة اللون",
			L"Сдвиг оттенка", L"Farbton", L"Matiz", L"Tint",
			L"Odcień", L"Ton kaydır");
	case SC_FX_CONTRAST:
		return LL14(L"コントラスト", L"Contrast", L"Contraste", L"Contrasto",
			L"Contraste", L"대비", L"对比度", L"تباين",
			L"Контраст", L"Kontrast", L"Contraste", L"Contrast",
			L"Kontrast", L"Kontrast");
	case SC_FX_BRIGHTNESS:
		return LL14(L"明るさ", L"Brightness", L"Luminosité", L"Luminosità",
			L"Brillo", L"밝기", L"亮度", L"سطوع",
			L"Яркость", L"Helligkeit", L"Brilho", L"Helderheid",
			L"Jasność", L"Parlaklık");
	case SC_FX_SATURATE:
		return LL14(L"彩度アップ", L"Saturate", L"Saturation", L"Saturazione",
			L"Saturar", L"채도 업", L"饱和度", L"تشبع",
			L"Насыщенность", L"Sättigung", L"Saturação", L"Verzadiging",
			L"Nasycenie", L"Doygunluk");
	case SC_FX_DESAT:
		return LL14(L"彩度ダウン", L"Desaturate", L"Désaturer", L"Desatura",
			L"Desaturar", L"채도 다운", L"降饱和", L"إلغاء التشبع",
			L"Обесцветить", L"Entsättigen", L"Dessaturar", L"Desatureren",
			L"Desaturacja", L"Doygunluk düşür");
	case SC_FX_THRESHOLD:
		return LL14(L"二値化", L"Threshold", L"Seuil", L"Soglia",
			L"Umbral", L"이진화", L"二值化", L"عتبة",
			L"Порог", L"Schwellwert", L"Limiar", L"Drempel",
			L"Próg", L"Eşik");
	case SC_FX_RED_CAST:
		return LL14(L"赤キャスト", L"Red cast", L"Dominante rouge", L"Dominante rossa",
			L"Dominante roja", L"레드 캐스트", L"红色偏色", L"صبغة حمراء",
			L"Красный оттенок", L"Rotstich", L"Tom vermelho", L"Rode zweem",
			L"Czerwony odcień", L"Kırmızı ton");
	case SC_FX_GREEN_CAST:
		return LL14(L"緑キャスト", L"Green cast", L"Dominante verte", L"Dominante verde",
			L"Dominante verde", L"그린 캐스트", L"绿色偏色", L"صبغة خضراء",
			L"Зелёный оттенок", L"Grünstich", L"Tom verde", L"Groene zweem",
			L"Zielony odcień", L"Yeşil ton");
	case SC_FX_BLUE_CAST:
		return LL14(L"青キャスト", L"Blue cast", L"Dominante bleue", L"Dominante blu",
			L"Dominante azul", L"블루 캐스트", L"蓝色偏色", L"صبغة زرقاء",
			L"Синий оттенок", L"Blaustich", L"Tom azul", L"Blauwe zweem",
			L"Niebieski odcień", L"Mavi ton");
	case SC_FX_CYAN:
		return LL14(L"シアン", L"Cyan", L"Cyan", L"Ciano",
			L"Cian", L"시안", L"青色", L"سماوي",
			L"Голубой", L"Cyan", L"Ciano", L"Cyaan",
			L"Cyjan", L"Camgöbeği");
	case SC_FX_MAGENTA:
		return LL14(L"マゼンタ", L"Magenta", L"Magenta", L"Magenta",
			L"Magenta", L"마젠타", L"品红", L"أرجواني",
			L"Пурпурный", L"Magenta", L"Magenta", L"Magenta",
			L"Magenta", L"Macenta");
	case SC_FX_YELLOW:
		return LL14(L"イエロー", L"Yellow", L"Jaune", L"Giallo",
			L"Amarillo", L"옐로", L"黄色", L"أصفر",
			L"Жёлтый", L"Gelb", L"Amarelo", L"Geel",
			L"Żółty", L"Sarı");
	case SC_FX_TEAL_ORANGE:
		return LL14(L"ティールオレンジ", L"Teal & orange", L"Sarcelle/orange", L"Teal/arancio",
			L"Verdeazulado/naranja", L"틸 오렌지", L"青橙", L"أزرق مخضر/برتقالي",
			L"Бирюза/оранж", L"Teal & Orange", L"Verde-água/laranja", L"Teal & oranje",
			L"Morski/pomarańcz", L"Turkuaz turuncu");
	case SC_FX_FADE:
		return LL14(L"フェード", L"Fade", L"Fondu", L"Sbiadito",
			L"Desvanecer", L"페이드", L"淡化", L"تلاشي",
			L"Выцветание", L"Verblassen", L"Desbotar", L"Vervagen",
			L"Blaknięcie", L"Soluk");
	case SC_FX_SPOTLIGHT:
		return LL14(L"スポットライト", L"Spotlight", L"Spot", L"Riflettore",
			L"Foco", L"스포트라이트", L"聚光", L"ضوء موضعي",
			L"Прожектор", L"Spotlight", L"Holofote", L"Spotlight",
			L"Reflektor", L"Spot");
	case SC_FX_BARS_H:
		return LL14(L"横バー", L"H-bars", L"Barres H", L"Barre H",
			L"Barras H", L"가로 바", L"横条", L"أشرطة أفقية",
			L"Гор. полосы", L"H-Balken", L"Barras H", L"H-balken",
			L"Paski poz.", L"Yatay çubuk");
	case SC_FX_BARS_V:
		return LL14(L"縦バー", L"V-bars", L"Barres V", L"Barre V",
			L"Barras V", L"세로 바", L"竖条", L"أشرطة عمودية",
			L"Верт. полосы", L"V-Balken", L"Barras V", L"V-balken",
			L"Paski pion.", L"Dikey çubuk");
	case SC_FX_CHROMA:
		return LL14(L"色収差", L"Chroma shift", L"Aberration", L"Aberrazione",
			L"Aberración", L"색수차", L"色差", L"زيغ لوني",
			L"Хроматика", L"Chromatisch", L"Aberração", L"Chroma",
			L"Aberracja", L"Kromatik");
	case SC_FX_EMBOSS:
		return LL14(L"エンボス", L"Emboss", L"Relief", L"Rilievo",
			L"Relieve", L"엠보스", L"浮雕", L"نقش بارز",
			L"Тиснение", L"Prägung", L"Relevo", L"Reliëf",
			L"Płaskorzeźba", L"Kabartma");
	case SC_FX_LIFT:
		return LL14(L"リフト", L"Lift", L"Lift", L"Lift",
			L"Lift", L"리프트", L"提亮", L"رفع",
			L"Подъём", L"Anheben", L"Lift", L"Lift",
			L"Podniesienie", L"Kaldır");
	case SC_FX_MONO_BLUE:
		return LL14(L"青モノクロ", L"Blue mono", L"Mono bleu", L"Mono blu",
			L"Mono azul", L"블루 모노", L"蓝色单色", L"أحادي أزرق",
			L"Синий моно", L"Blau-Mono", L"Mono azul", L"Blauw mono",
			L"Nieb. mono", L"Mavi mono");
	case SC_FX_MONO_GREEN:
		return LL14(L"緑モノクロ", L"Green mono", L"Mono vert", L"Mono verde",
			L"Mono verde", L"그린 모노", L"绿色单色", L"أحادي أخضر",
			L"Зел. моно", L"Grün-Mono", L"Mono verde", L"Groen mono",
			L"Ziel. mono", L"Yeşil mono");
	case SC_FX_QUAD:
		return LL14(L"四分割ミラー", L"Quad mirror", L"Miroir 4", L"Specchio 4",
			L"Espejo 4", L"4분할 미러", L"四象限镜像", L"مرآة رباعية",
			L"4 зеркала", L"4-Spiegel", L"Espelho 4", L"4-spiegel",
			L"4 lustra", L"4 ayna");
	case SC_FX_DUO_PURPLE:
		return LL14(L"紫デュオトーン", L"Purple duo", L"Duo violet", L"Duo viola",
			L"Dúo púrpura", L"퍼플 듀오", L"紫色双色", L"ثنائي بنفسجي",
			L"Фиолет. дуо", L"Lila-Duo", L"Duo roxo", L"Paars duo",
			L"Fiolet duo", L"Mor ikili");
	case SC_FX_BLUR_MEGA:
		return LL14(L"ぼかし極大", L"Mega blur", L"Flou max", L"Sfocatura mega",
			L"Desenfoque mega", L"초대형 흐림", L"超强模糊", L"ضباب هائل",
			L"Мега-размытие", L"Mega-Blur", L"Desfoque mega", L"Mega-blur",
			L"Mega rozmycie", L"Mega bulanık");
	case SC_FX_MOTION_BLUR:
		return LL14(L"モーションブラー", L"Motion blur", L"Flou de mouvement", L"Motion blur",
			L"Motion blur", L"모션 블러", L"动态模糊", L"ضباب حركة",
			L"Размытие движения", L"Bewegungsunschärfe", L"Motion blur", L"Motion blur",
			L"Motion blur", L"Hareket bulanıklığı");
	case SC_FX_RADIAL_BLUR:
		return LL14(L"放射ブラー", L"Radial blur", L"Flou radial", L"Sfocatura radiale",
			L"Desenfoque radial", L"방사 블러", L"径向模糊", L"ضباب شعاعي",
			L"Радиальное размытие", L"Radialer Blur", L"Desfoque radial", L"Radiale blur",
			L"Rozmycie radialne", L"Radyal bulanık");
	case SC_FX_ZOOM_BLUR:
		return LL14(L"ズームブラー", L"Zoom blur", L"Flou zoom", L"Zoom blur",
			L"Zoom blur", L"줌 블러", L"变焦模糊", L"ضباب تكبير",
			L"Зум-размытие", L"Zoom-Blur", L"Zoom blur", L"Zoom-blur",
			L"Zoom blur", L"Yakınlaştırma blur");
	case SC_FX_SWIRL:
		return LL14(L"スワール", L"Swirl", L"Tourbillon", L"Vortice",
			L"Remolino", L"소용돌이", L"漩涡", L"دوامة",
			L"Свирл", L"Wirbel", L"Redemoinho", L"Werveling",
			L"Wir", L"Girdap");
	case SC_FX_RIPPLE:
		return LL14(L"リップル", L"Ripple", L"Ondulation", L"Increspatura",
			L"Ondulación", L"잔물결", L"波纹", L"تموج",
			L"Рябь", L"Kräuselung", L"Ondulação", L"Rimpel",
			L"Falowanie", L"Dalgalanma");
	case SC_FX_VORTEX:
		return LL14(L"ボルテックス", L"Vortex", L"Vortex", L"Vortex",
			L"Vórtice", L"볼텍스", L"涡流", L"دوامة قوية",
			L"Вихрь", L"Vortex", L"Vórtice", L"Vortex",
			L"Wir wodny", L"Vortex");
	case SC_FX_HEAT_HAZE:
		return LL14(L"陽炎", L"Heat haze", L"Mirage chaleur", L"Foschia calore",
			L"Calima", L"아지랑이", L"热浪", L"سراب حراري",
			L"Марево", L"Hitzeflimmern", L"Névoa de calor", L"Hittewaas",
			L"Miraż ciepła", L"Sıcaklık serabı");
	case SC_FX_GLITCH:
		return LL14(L"グリッチ", L"Glitch", L"Glitch", L"Glitch",
			L"Glitch", L"글리치", L"故障风", L"خلل",
			L"Глитч", L"Glitch", L"Glitch", L"Glitch",
			L"Glitch", L"Glitch");
	case SC_FX_CRT_CURVE:
		return LL14(L"CRT曲面", L"CRT curve", L"Courbe CRT", L"Curva CRT",
			L"Curva CRT", L"CRT 곡선", L"CRT曲面", L"منحنى CRT",
			L"Кривизна CRT", L"CRT-Kurve", L"Curva CRT", L"CRT-kromme",
			L"Krzywa CRT", L"CRT eğri");
	case SC_FX_KALEIDO:
		return LL14(L"万華鏡", L"Kaleidoscope", L"Kaléidoscope", L"Caleidoscopio",
			L"Caleidoscopio", L"만화경", L"万花筒", L"مشكال",
			L"Калейдоскоп", L"Kaleidoskop", L"Caleidoscópio", L"Caleidoscoop",
			L"Kalejdoskop", L"Kaleydoskop");
	case SC_FX_OIL:
		return LL14(L"油絵風", L"Oil paint", L"Peinture à l’huile", L"Olio",
			L"Óleo", L"유화풍", L"油画风", L"زيت",
			L"Масло", L"Ölmalerei", L"Óleo", L"Olieverf",
			L"Farba olejna", L"Yağlı boya");
	case SC_FX_WATERCOLOR:
		return LL14(L"水彩風", L"Watercolor", L"Aquarelle", L"Acquerello",
			L"Acuarela", L"수채화", L"水彩", L"ألوان مائية",
			L"Акварель", L"Aquarell", L"Aquarela", L"Aquarel",
			L"Akwarela", L"Sulu boya");
	case SC_FX_PENCIL:
		return LL14(L"鉛筆画", L"Pencil sketch", L"Crayon", L"Matita",
			L"Lápiz", L"연필화", L"铅笔素描", L"قلم رصاص",
			L"Карандаш", L"Bleistift", L"Lápis", L"Potlood",
			L"Ołówek", L"Kurşun kalem");
	case SC_FX_DREAM:
		return LL14(L"ドリーム", L"Dream", L"Rêve", L"Sogno",
			L"Sueño", L"드림", L"梦幻", L"حلم",
			L"Мечта", L"Traum", L"Sonho", L"Droom",
			L"Sen", L"Rüya");
	case SC_FX_GODRAYS:
		return LL14(L"ゴッドレイ", L"God rays", L"Rayons divins", L"God rays",
			L"Rayos de dios", L"갓레이", L"丁达尔光", L"أشعة إلهية",
			L"Божественные лучи", L"Godrays", L"Raios divinos", L"Godrays",
			L"Promienie boga", L"Tanrı ışınları");
	case SC_FX_DISPLACE:
		return LL14(L"ディスプレイス", L"Displace", L"Déplacement", L"Displace",
			L"Desplazar", L"디스플레이스", L"置换", L"إزاحة",
			L"Смещение", L"Displace", L"Deslocar", L"Displace",
			L"Przesunięcie", L"Kaydırma");
	case SC_FX_INTERLACE:
		return LL14(L"インターレース", L"Interlace", L"Entrelacement", L"Interlacciato",
			L"Entrelazado", L"인터레이스", L"隔行", L"تشابك",
			L"Чересстрочная", L"Zeilensprung", L"Entrelaçado", L"Interlace",
			L"Przeplot", L"Satır atlama");
	case SC_FX_CHROMA_HEAVY:
		return LL14(L"色収差強", L"Heavy chroma", L"Aberration forte", L"Aberrazione forte",
			L"Aberración fuerte", L"강한 색수차", L"强色差", L"زيغ لوني قوي",
			L"Сильная хроматика", L"Starke Chromatik", L"Aberração forte", L"Sterke chroma",
			L"Silna aberracja", L"Güçlü kromatik");
	case SC_FX_FOG:
		return LL14(L"フォグ", L"Fog", L"Brouillard", L"Nebbia",
			L"Niebla", L"안개", L"雾", L"ضباب",
			L"Туман", L"Nebel", L"Névoa", L"Mist",
			L"Mgła", L"Sis");
	case SC_FX_SHARPEN_HEAVY:
		return LL14(L"シャープ強", L"Heavy sharpen", L"Netteté forte", L"Nitidezza forte",
			L"Nitidez fuerte", L"강한 샤픈", L"强锐化", L"حدة قوية",
			L"Сильная резкость", L"Stark schärfen", L"Nitidez forte", L"Sterk verscherpen",
			L"Silne wyostrzenie", L"Güçlü keskinleştir");
	default:
		return LL14(L"なし", L"None", L"Aucun", L"Nessuno",
			L"Ninguno", L"없음", L"无", L"بدون",
			L"Нет", L"Kein", L"Nenhum", L"Geen",
			L"Brak", L"Yok");
	}
}

CString CScreenCaptureDlg::FxDesc(int fx) const
{
	switch (fx) {
	case SC_FX_BLUR_SOFT:
		return LL14(L"弱いぼかし", L"Light blur", L"Flou léger", L"Sfocatura leggera",
			L"Desenfoque suave", L"약한 흐림", L"轻模糊", L"ضباب خفيف",
			L"Лёгкое размытие", L"Leichte Unschärfe", L"Desfoque suave", L"Lichte blur",
			L"Lekkie rozmycie", L"Hafif bulanıklık");
	case SC_FX_BLUR_STRONG:
		return LL14(L"強いぼかし", L"Strong blur", L"Flou fort", L"Sfocatura forte",
			L"Desenfoque fuerte", L"강한 흐림", L"强模糊", L"ضباب قوي",
			L"Сильное размытие", L"Starke Unschärfe", L"Desfoque forte", L"Sterke blur",
			L"Silne rozmycie", L"Güçlü bulanıklık");
	case SC_FX_GRAY:
		return LL14(L"グレースケール", L"Grayscale", L"Niveaux de gris", L"Scala di grigi",
			L"Escala de grises", L"그레이스케일", L"灰度", L"تدرج رمادي",
			L"Оттенки серого", L"Graustufen", L"Escala de cinza", L"Grijstinten",
			L"Odcienie szarości", L"Gri ton");
	case SC_FX_SEPIA:
		return LL14(L"セピア調", L"Sepia tone", L"Ton sépia", L"Tono seppia",
			L"Tono sepia", L"세피아 톤", L"棕褐调", L"درجة سيبيا",
			L"Сепия", L"Sepiaton", L"Tom sépia", L"Sepiatint",
			L"Ton sepii", L"Sepya ton");
	case SC_FX_VIGNETTE:
		return LL14(L"周辺減光", L"Darken edges", L"Assombrir bords", L"Scurisce i bordi",
			L"Oscurece bordes", L"가장자리 어둡게", L"暗角", L"تظليل الحواف",
			L"Виньетка", L"Ränder abdunkeln", L"Escurece bordas", L"Randen donker",
			L"Przyciemnia brzegi", L"Kenarları karart");
	case SC_FX_SHARPEN:
		return LL14(L"輪郭を強調", L"Emphasize edges", L"Renforce contours", L"Enfatizza bordi",
			L"Resalta bordes", L"윤곽 강조", L"锐化轮廓", L"إبراز الحواف",
			L"Подчёркивает края", L"Kanten betonen", L"Realça bordas", L"Randen benadrukken",
			L"Wyostrza krawędzie", L"Kenarları belirginleştir");
	case SC_FX_MIRROR:
		return LL14(L"左右ミラー", L"Horizontal mirror", L"Miroir horizontal", L"Specchio orizz.",
			L"Espejo horizontal", L"좌우 미러", L"水平镜像", L"مرآة أفقية",
			L"Горизонтальное зеркало", L"Horizontal spiegeln", L"Espelho horizontal", L"Horizontaal spiegelen",
			L"Odbicie poziome", L"Yatay ayna");
	case SC_FX_WAVE:
		return LL14(L"波紋ゆらぎ", L"Wave distortion", L"Distorsion vague", L"Distorsione onda",
			L"Distorsión onda", L"물결 왜곡", L"波浪扭曲", L"تشويه موجي",
			L"Волновое искажение", L"Wellenverzerrung", L"Distorção de onda", L"Golfvervorming",
			L"Zniekształcenie fali", L"Dalga bozulması");
	case SC_FX_UNDERWATER:
		return LL14(L"水中＋青み", L"Underwater + cyan", L"Sous-marin + cyan", L"Sott'acqua + ciano",
			L"Bajo agua + cian", L"수중+시안", L"水下+青调", L"تحت الماء + سماوي",
			L"Под водой + циан", L"Unterwasser + Cyan", L"Subaquático + ciano", L"Onderwater + cyaan",
			L"Pod wodą + cyjan", L"Su altı + camgöbeği");
	case SC_FX_DUSK:
		return LL14(L"夕暮れの暖色", L"Warm dusk tint", L"Teinte crépuscule", L"Tinta crepuscolo",
			L"Tinte atardecer", L"황혼 웜톤", L"黄昏暖色", L"درجة غروب دافئة",
			L"Тёплые сумерки", L"Warme Dämmerung", L"Tom crepúsculo", L"Warme schemer",
			L"Ciepły zmierzch", L"Sıcak alacakaranlık");
	case SC_FX_COOL:
		return LL14(L"寒色寄り", L"Cool color cast", L"Dominante froide", L"Dominante fredda",
			L"Dominante fría", L"쿨톤 캐스트", L"冷色偏移", L"صبغة باردة",
			L"Холодный оттенок", L"Kühler Stich", L"Tom frio", L"Koele zweem",
			L"Zimny odcień", L"Soğuk renk");
	case SC_FX_WARM:
		return LL14(L"暖色寄り", L"Warm color cast", L"Dominante chaude", L"Dominante calda",
			L"Dominante cálida", L"웜톤 캐스트", L"暖色偏移", L"صبغة دافئة",
			L"Тёплый оттенок", L"Warmer Stich", L"Tom quente", L"Warme zweem",
			L"Ciepły odcień", L"Sıcak renk");
	case SC_FX_POSTER:
		return LL14(L"色数を減らす", L"Reduce color steps", L"Réduit les couleurs", L"Riduce i colori",
			L"Reduce colores", L"색 단계 감소", L"减少色阶", L"تقليل الألوان",
			L"Меньше оттенков", L"Weniger Farbstufen", L"Reduz cores", L"Minder kleuren",
			L"Mniej barw", L"Renk basamağını azalt");
	case SC_FX_SCANLINE:
		return LL14(L"CRT風の走査線", L"CRT-like scanlines", L"Lignes type CRT", L"Linee tipo CRT",
			L"Líneas tipo CRT", L"CRT 스캔라인", L"CRT扫描线", L"خطوط CRT",
			L"Строки как CRT", L"CRT-Abtastzeilen", L"Linhas tipo CRT", L"CRT-aftastlijnen",
			L"Linie jak CRT", L"CRT tarama çizgileri");
	case SC_FX_EDGE:
		return LL14(L"輪郭検出", L"Edge detection", L"Détection de contours", L"Rilevamento bordi",
			L"Detección de bordes", L"에지 검출", L"边缘检测", L"كشف الحواف",
			L"Выделение контуров", L"Kantenerkennung", L"Detecção de bordas", L"Randdetectie",
			L"Wykrywanie krawędzi", L"Kenar algılama");
	case SC_FX_INVERT:
		return LL14(L"色反転(ネガ)", L"Color invert (negative)", L"Inversion (négatif)", L"Inversione (negativo)",
			L"Invertir (negativo)", L"색 반전(네거)", L"反色(负片)", L"عكس الألوان",
			L"Инверсия (негатив)", L"Negativ", L"Inverter (negativo)", L"Inverteren (negatief)",
			L"Inwersja (negatyw)", L"Renk tersine (negatif)");
	case SC_FX_SOLARIZE:
		return LL14(L"明るい部分を反転", L"Invert bright areas", L"Inverse les zones claires", L"Inverte zone chiare",
			L"Invierte zonas claras", L"밝은 영역 반전", L"亮部反转", L"عكس المناطق الساطعة",
			L"Инверсия светлых", L"Helle Bereiche invertieren", L"Inverte áreas claras", L"Lichte delen inverteren",
			L"Odwraca jasne obszary", L"Parlak alanları ters çevir");
	case SC_FX_PIXELATE:
		return LL14(L"モザイク化", L"Blocky pixelate", L"Pixellisation", L"Effetto pixel",
			L"Pixelado", L"모자이크", L"马赛克化", L"بكسلة",
			L"Пикселизация", L"Verpixeln", L"Pixelizar", L"Verpixelen",
			L"Pikselizacja", L"Pikselleştir");
	case SC_FX_FLIP_V:
		return LL14(L"上下ミラー", L"Vertical flip", L"Retournement vertical", L"Capovolgi verticale",
			L"Voltear vertical", L"상하 반전", L"垂直翻转", L"قلب عمودي",
			L"Вертикальный переворот", L"Vertikal spiegeln", L"Inverter vertical", L"Verticaal spiegelen",
			L"Odwrócenie pionowe", L"Dikey çevir");
	case SC_FX_NOISE:
		return LL14(L"粒状ノイズ", L"Grain noise", L"Grain", L"Rumore granulosità",
			L"Grano", L"그레인 노이즈", L"颗粒噪点", L"ضوضاء حبيبية",
			L"Зернистый шум", L"Körniges Rauschen", L"Grão", L"Korrelruis",
			L"Ziarnisty szum", L"Gren gürültü");
	case SC_FX_BLOOM:
		return LL14(L"ハイライトの輝光", L"Highlight glow", L"Lueur des hautes lumières", L"Bagliore alte luci",
			L"Resplandor de luces", L"하이라이트 글로우", L"高光辉光", L"توهج الإبراز",
			L"Свечение бликов", L"Highlight-Glow", L"Brilho de realces", L"Highlight-gloed",
			L"Poświata świateł", L"Vurgu parıltısı");
	case SC_FX_NEON:
		return LL14(L"ネオン輪郭", L"Neon outlines", L"Contours néon", L"Contorni neon",
			L"Contornos neón", L"네온 윤곽", L"霓虹轮廓", L"حدود نيون",
			L"Неоновые контуры", L"Neon-Konturen", L"Contornos neon", L"Neonranden",
			L"Neonowe kontury", L"Neon kenarlar");
	case SC_FX_NIGHTVISION:
		return LL14(L"暗視風(緑)", L"Night-vision green", L"Vision nocturne verte", L"Visione notturna verde",
			L"Visión nocturna verde", L"야간투시 녹색", L"夜视绿", L"رؤية ليلية خضراء",
			L"ПНВ (зелёный)", L"Nachtsicht grün", L"Visão noturna verde", L"Nachtzicht groen",
			L"Noktowizja zielona", L"Gece görüş yeşil");
	case SC_FX_COMIC:
		return LL14(L"漫画風の塗り", L"Comic shading", L"Rendu comic", L"Resa fumetto",
			L"Sombreado cómic", L"코믹 음영", L"漫画着色", L"تظليل كوميك",
			L"Комиксная заливка", L"Comic-Schattierung", L"Sombreamento HQ", L"Strip-arcering",
			L"Cieniowanie komiksowe", L"Çizgi roman gölgeleme");
	case SC_FX_RETRO:
		return LL14(L"レトロ写真風", L"Retro photo look", L"Look photo rétro", L"Aspetto foto retro",
			L"Aspecto foto retro", L"레트로 사진", L"复古照片风", L"مظهر صورة ريترو",
			L"Ретро-фото", L"Retro-Fotooptik", L"Visual foto retrô", L"Retro-fotolook",
			L"Wygląd retro", L"Retro foto görünümü");
	case SC_FX_FISHEYE:
		return LL14(L"魚眼レンズ歪み", L"Fisheye lens warp", L"Distorsion fish-eye", L"Distorsione fish-eye",
			L"Distorsión ojo de pez", L"어안 왜곡", L"鱼眼畸变", L"تشويه عين السمكة",
			L"Искажение рыбий глаз", L"Fischaugenverzerrung", L"Distorção olho de peixe", L"Visoogvervorming",
			L"Zniekształcenie rybie oko", L"Balık gözü bozulması");
	case SC_FX_HUE_SHIFT:
		return LL14(L"色相を回転", L"Rotate hue", L"Fait tourner la teinte", L"Ruota la tonalità",
			L"Rota el matiz", L"색조 회전", L"旋转色相", L"تدوير اللون",
			L"Сдвиг оттенка", L"Farbton drehen", L"Rotaciona matiz", L"Tint draaien",
			L"Obraca odcień", L"Ton kaydır");
	case SC_FX_CONTRAST:
		return LL14(L"コントラスト強調", L"Boost contrast", L"Augmente le contraste", L"Aumenta contrasto",
			L"Aumenta contraste", L"대비 강화", L"提高对比", L"زيادة التباين",
			L"Усиление контраста", L"Kontrast erhöhen", L"Aumenta contraste", L"Contrast verhogen",
			L"Wzmacnia kontrast", L"Kontrast artır");
	case SC_FX_BRIGHTNESS:
		return LL14(L"全体を明るく", L"Brighten overall", L"Éclaircit globalement", L"Schiarisce tutto",
			L"Aclara todo", L"전체 밝게", L"整体提亮", L"إضاءة عامة",
			L"Общее осветление", L"Gesamt heller", L"Clareia tudo", L"Algeheel helderder",
			L"Rozjaśnia całość", L"Genel parlaklık");
	case SC_FX_SATURATE:
		return LL14(L"彩度を上げる", L"Increase saturation", L"Augmente la saturation", L"Aumenta saturazione",
			L"Aumenta saturación", L"채도 증가", L"提高饱和度", L"زيادة التشبع",
			L"Повышает насыщенность", L"Sättigung erhöhen", L"Aumenta saturação", L"Verzadiging verhogen",
			L"Zwiększa nasycenie", L"Doygunluk artır");
	case SC_FX_DESAT:
		return LL14(L"彩度を下げる", L"Reduce saturation", L"Réduit la saturation", L"Riduce saturazione",
			L"Reduce saturación", L"채도 감소", L"降低饱和度", L"تقليل التشبع",
			L"Снижает насыщенность", L"Sättigung senken", L"Reduz saturação", L"Verzadiging verlagen",
			L"Zmniejsza nasycenie", L"Doygunluk azalt");
	case SC_FX_THRESHOLD:
		return LL14(L"白黒の二値", L"Hard B&W threshold", L"Seuil N&B", L"Soglia B/N",
			L"Umbral B/N", L"흑백 이진화", L"黑白二值", L"عتبة أبيض/أسود",
			L"Жёсткий порог Ч/Б", L"Hartes S/W", L"Limiar P/B", L"Harde Z/W",
			L"Twardy próg C/B", L"Sert S/B eşik");
	case SC_FX_RED_CAST:
		return LL14(L"赤みを足す", L"Add red tint", L"Ajoute du rouge", L"Aggiunge rosso",
			L"Añade rojo", L"빨강 톤 추가", L"加红调", L"إضافة أحمر",
			L"Добавляет красный", L"Rot hinzufügen", L"Adiciona vermelho", L"Rood toevoegen",
			L"Dodaje czerwień", L"Kırmızı ekle");
	case SC_FX_GREEN_CAST:
		return LL14(L"緑みを足す", L"Add green tint", L"Ajoute du vert", L"Aggiunge verde",
			L"Añade verde", L"초록 톤 추가", L"加绿调", L"إضافة أخضر",
			L"Добавляет зелёный", L"Grün hinzufügen", L"Adiciona verde", L"Groen toevoegen",
			L"Dodaje zieleń", L"Yeşil ekle");
	case SC_FX_BLUE_CAST:
		return LL14(L"青みを足す", L"Add blue tint", L"Ajoute du bleu", L"Aggiunge blu",
			L"Añade azul", L"파랑 톤 추가", L"加蓝调", L"إضافة أزرق",
			L"Добавляет синий", L"Blau hinzufügen", L"Adiciona azul", L"Blauw toevoegen",
			L"Dodaje błękit", L"Mavi ekle");
	case SC_FX_CYAN:
		return LL14(L"シアン寄り", L"Cyan tint", L"Teinte cyan", L"Tinta ciano",
			L"Tinte cian", L"시안 톤", L"青色调", L"درجة سماوية",
			L"Голубой тон", L"Cyan-Ton", L"Tom ciano", L"Cyaantint",
			L"Ton cyjanu", L"Camgöbeği ton");
	case SC_FX_MAGENTA:
		return LL14(L"マゼンタ寄り", L"Magenta tint", L"Teinte magenta", L"Tinta magenta",
			L"Tinte magenta", L"마젠타 톤", L"品红调", L"درجة أرجوانية",
			L"Пурпурный тон", L"Magenta-Ton", L"Tom magenta", L"Magentatint",
			L"Ton magenty", L"Macenta ton");
	case SC_FX_YELLOW:
		return LL14(L"イエロー寄り", L"Yellow tint", L"Teinte jaune", L"Tinta gialla",
			L"Tinte amarillo", L"옐로 톤", L"黄色调", L"درجة صفراء",
			L"Жёлтый тон", L"Gelbton", L"Tom amarelo", L"Geeltint",
			L"Ton żółci", L"Sarı ton");
	case SC_FX_TEAL_ORANGE:
		return LL14(L"映画風の補色", L"Cinematic teal/orange", L"Complémentaire cinéma", L"Complementari cinema",
			L"Complementarios cine", L"시네마틱 보색", L"电影互补色", L"تكميلي سينمائي",
			L"Кино-комплемент", L"Kino-Komplementär", L"Complementar cinema", L"Cinema-complementair",
			L"Kinowe dopełnienie", L"Sinematik tamamlayıcı");
	case SC_FX_FADE:
		return LL14(L"洗いざらし風", L"Washed-out look", L"Aspect délavé", L"Aspetto sbiadito",
			L"Aspecto desgastado", L"바랜 느낌", L"褪色感", L"مظهر باهت",
			L"Выцветший вид", L"Verwaschener Look", L"Visual desbotado", L"Uitgewassen look",
			L"Wyblakły wygląd", L"Soluk görünüm");
	case SC_FX_SPOTLIGHT:
		return LL14(L"中央を明るく", L"Brighten center", L"Éclaircit le centre", L"Schiarisce il centro",
			L"Aclara el centro", L"중앙 밝게", L"中心提亮", L"إضاءة المركز",
			L"Осветляет центр", L"Mitte aufhellen", L"Clareia o centro", L"Midden helderder",
			L"Rozjaśnia środek", L"Merkezi parlat");
	case SC_FX_BARS_H:
		return LL14(L"細い横縞", L"Thin horizontal bars", L"Fines barres H", L"Sottili barre H",
			L"Barras H finas", L"가는 가로줄", L"细横条", L"أشرطة أفقية رفيعة",
			L"Тонкие гор. полосы", L"Dünne H-Balken", L"Barras H finas", L"Dunne H-balken",
			L"Cienkie paski poz.", L"İnce yatay çubuk");
	case SC_FX_BARS_V:
		return LL14(L"細い縦縞", L"Thin vertical bars", L"Fines barres V", L"Sottili barre V",
			L"Barras V finas", L"가는 세로줄", L"细竖条", L"أشرطة عمودية رفيعة",
			L"Тонкие верт. полосы", L"Dünne V-Balken", L"Barras V finas", L"Dunne V-balken",
			L"Cienkie paski pion.", L"İnce dikey çubuk");
	case SC_FX_CHROMA:
		return LL14(L"軽いRGBずれ", L"Light RGB split", L"Léger décalage RGB", L"Lieve split RGB",
			L"Ligera separación RGB", L"약한 RGB 분리", L"轻微RGB偏移", L"انزياح RGB خفيف",
			L"Лёгкий RGB-сдвиг", L"Leichte RGB-Trennung", L"Leve split RGB", L"Lichte RGB-split",
			L"Lekki rozdział RGB", L"Hafif RGB kayması");
	case SC_FX_EMBOSS:
		return LL14(L"浮き彫り風", L"Relief emboss", L"Relief léger", L"Rilievo leggero",
			L"Relieve ligero", L"얕은 엠보스", L"浅浮雕", L"نقش خفيف",
			L"Лёгкое тиснение", L"Leichte Prägung", L"Relevo leve", L"Licht reliëf",
			L"Lekka płaskorzeźba", L"Hafif kabartma");
	case SC_FX_LIFT:
		return LL14(L"中間調を持ち上げ", L"Lift midtones", L"Remonte les tons moyens", L"Alza i mezzitoni",
			L"Eleva medios tonos", L"중간톤 올림", L"提升中间调", L"رفع الدرجات الوسطى",
			L"Поднимает средние", L"Mitteltöne anheben", L"Eleva meios-tons", L"Middentonen omhoog",
			L"Podnosi półtony", L"Orta tonları kaldır");
	case SC_FX_MONO_BLUE:
		return LL14(L"青みのモノクロ", L"Cool blue mono", L"Mono bleu froid", L"Mono blu freddo",
			L"Mono azul frío", L"차가운 블루 모노", L"冷蓝单色", L"أحادي أزرق بارد",
			L"Холодный синий моно", L"Kühles Blau-Mono", L"Mono azul frio", L"Koel blauw mono",
			L"Zimny nieb. mono", L"Soğuk mavi mono");
	case SC_FX_MONO_GREEN:
		return LL14(L"緑みのモノクロ", L"Green mono tint", L"Mono teinté vert", L"Mono tinta verde",
			L"Mono tinte verde", L"그린 모노 톤", L"绿色单色调", L"أحادي أخضر ملوّن",
			L"Зелёный моно", L"Grün getöntes Mono", L"Mono tom verde", L"Groen getint mono",
			L"Zielone mono", L"Yeşil mono ton");
	case SC_FX_QUAD:
		return LL14(L"四隅にミラー", L"Mirror into 4 tiles", L"Miroir en 4", L"Specchio in 4",
			L"Espejo en 4", L"4칸 미러", L"四格镜像", L"مرآة إلى 4",
			L"Зеркало на 4", L"In 4 spiegeln", L"Espelho em 4", L"Spiegel in 4",
			L"Lustro na 4", L"4’e ayna");
	case SC_FX_DUO_PURPLE:
		return LL14(L"紫〜琥珀の二色", L"Purple–amber duo", L"Duo violet/ambre", L"Duo viola/ambra",
			L"Dúo púrpura/ámbar", L"퍼플-앰버 듀오", L"紫琥珀双色", L"ثنائي بنفسجي/كهرماني",
			L"Фиолет–янтарь", L"Lila–Bernstein", L"Duo roxo/âmbar", L"Paars–amber duo",
			L"Fiolet–bursztyn", L"Mor–kehribar");
	case SC_FX_BLUR_MEGA:
		return LL14(L"非常に強いぼかし（重い）", L"Very strong blur (heavy)", L"Flou très fort (lourd)", L"Sfocatura molto forte (pesante)",
			L"Desenfoque muy fuerte (pesado)", L"매우 강한 흐림(무거움)", L"超强模糊（重）", L"ضباب قوي جداً (ثقيل)",
			L"Очень сильное размытие (тяж.)", L"Sehr starker Blur (schwer)", L"Desfoque muito forte (pesado)", L"Zeer sterke blur (zwaar)",
			L"Bardzo silne rozmycie (ciężkie)", L"Çok güçlü blur (ağır)");
	case SC_FX_MOTION_BLUR:
		return LL14(L"横方向の残像（中〜重）", L"Horizontal smear (med–heavy)", L"Traînée H (moy–lourd)", L"Smear orizz. (medio–pesante)",
			L"Arrastre H (med–pesado)", L"가로 잔상(중~무거움)", L"水平拖影（中~重）", L"أثر أفقي (متوسط–ثقيل)",
			L"Гор. шлейф (сред–тяж.)", L"Horiz. Nachzieh (mittel–schwer)", L"Arrasto H (méd–pesado)", L"Horiz. sleep (mid–zwaar)",
			L"Poziomy smug (śr.–ciężki)", L"Yatay iz (orta–ağır)");
	case SC_FX_RADIAL_BLUR:
		return LL14(L"中心から外側へ放射ぼかし（中）", L"Outward radial blur (med)", L"Flou radial vers l’extérieur (moy)", L"Blur radiale verso fuori (medio)",
			L"Blur radial hacia afuera (med)", L"바깥 방사 블러(중)", L"向外径向模糊（中）", L"ضباب شعاعي للخارج (متوسط)",
			L"Радиальное наружу (сред.)", L"Radial nach außen (mittel)", L"Blur radial para fora (méd)", L"Radiaal naar buiten (mid)",
			L"Radialnie na zewnątrz (śr.)", L"Dışa radyal blur (orta)");
	case SC_FX_ZOOM_BLUR:
		return LL14(L"中心ズーム残像（中〜重）", L"Center zoom trails (med–heavy)", L"Traînées zoom centre (moy–lourd)", L"Code zoom centro (medio–pesante)",
			L"Estelas zoom centro (med–pesado)", L"중심 줌 잔상(중~무거움)", L"中心变焦拖影（中~重）", L"آثار تكبير المركز (متوسط–ثقيل)",
			L"Зум-шлейф центра (сред–тяж.)", L"Zoom-Spuren Mitte (mittel–schwer)", L"Rastros zoom centro (méd–pesado)", L"Zoom-sporen midden (mid–zwaar)",
			L"Ślady zoom środka (śr.–ciężki)", L"Merkez zoom izi (orta–ağır)");
	case SC_FX_SWIRL:
		return LL14(L"渦巻き歪み（中）", L"Swirl warp (med)", L"Distorsion tourbillon (moy)", L"Distorsione vortice (medio)",
			L"Distorsión remolino (med)", L"소용돌이 왜곡(중)", L"漩涡扭曲（中）", L"تشويه دوامة (متوسط)",
			L"Вихревое искажение (сред.)", L"Wirbelverzerrung (mittel)", L"Distorção redemoinho (méd)", L"Wervelvervorming (mid)",
			L"Zniekształcenie wiru (śr.)", L"Girdap bozulması (orta)");
	case SC_FX_RIPPLE:
		return LL14(L"同心円の波紋（中）", L"Concentric ripples (med)", L"Ondulations concentriques (moy)", L"Increspature concentriche (medio)",
			L"Ondas concéntricas (med)", L"동심원 잔물결(중)", L"同心波纹（中）", L"تموجات مركزية (متوسط)",
			L"Концентрическая рябь (сред.)", L"Konzentrische Wellen (mittel)", L"Ondulações concêntricas (méd)", L"Concentrische rimpels (mid)",
			L"Koncentryczne fale (śr.)", L"Eşmerkez dalga (orta)");
	case SC_FX_VORTEX:
		return LL14(L"強い渦巻き（中〜重）", L"Strong vortex (med–heavy)", L"Vortex fort (moy–lourd)", L"Vortex forte (medio–pesante)",
			L"Vórtice fuerte (med–pesado)", L"강한 볼텍스(중~무거움)", L"强涡流（中~重）", L"دوامة قوية (متوسط–ثقيل)",
			L"Сильный вихрь (сред–тяж.)", L"Starker Vortex (mittel–schwer)", L"Vórtice forte (méd–pesado)", L"Sterke vortex (mid–zwaar)",
			L"Silny wir (śr.–ciężki)", L"Güçlü vortex (orta–ağır)");
	case SC_FX_HEAT_HAZE:
		return LL14(L"熱気流のゆらぎ（中）", L"Heat shimmer (med)", L"Mirage de chaleur (moy)", L"Foschia di calore (medio)",
			L"Calima térmica (med)", L"열기 아지랑이(중)", L"热浪闪烁（中）", L"وميض حراري (متوسط)",
			L"Тепловое марево (сред.)", L"Hitze-Flimmern (mittel)", L"Cintilação de calor (méd)", L"Hitteflikkering (mid)",
			L"Migotanie ciepła (śr.)", L"Isı titreşimi (orta)");
	case SC_FX_GLITCH:
		return LL14(L"帯ずれ＋RGBずれ（中）", L"Band + RGB glitch (med)", L"Glitch bandes+RGB (moy)", L"Glitch bande+RGB (medio)",
			L"Glitch bandas+RGB (med)", L"밴드+RGB 글리치(중)", L"条带+RGB故障（中）", L"خلل شرائط+RGB (متوسط)",
			L"Глитч полос+RGB (сред.)", L"Band+RGB-Glitch (mittel)", L"Glitch faixas+RGB (méd)", L"Band+RGB-glitch (mid)",
			L"Glitch pasm+RGB (śr.)", L"Şerit+RGB glitch (orta)");
	case SC_FX_CRT_CURVE:
		return LL14(L"ブラウン管の湾曲＋走査線（中）", L"CRT bulge + scanlines (med)", L"Bombé CRT + lignes (moy)", L"Curva CRT + scanline (medio)",
			L"Curva CRT + líneas (med)", L"CRT 곡면+스캔라인(중)", L"CRT鼓面+扫描线（中）", L"تحدب CRT + خطوط (متوسط)",
			L"Выпуклость CRT + строки (сред.)", L"CRT-Wölbung + Zeilen (mittel)", L"Curva CRT + linhas (méd)", L"CRT-bol + scanlines (mid)",
			L"Wypukłość CRT + linie (śr.)", L"CRT şişkinlik + tarama (orta)");
	case SC_FX_KALEIDO:
		return LL14(L"万華鏡ミラー（中）", L"Kaleido mirror (med)", L"Miroir kaléido (moy)", L"Specchio caleido (medio)",
			L"Espejo caleido (med)", L"만화경 미러(중)", L"万花筒镜像（中）", L"مرآة مشكال (متوسط)",
			L"Калейдоскоп-зеркало (сред.)", L"Kaleido-Spiegel (mittel)", L"Espelho caleido (méd)", L"Caleido-spiegel (mid)",
			L"Lustro kalejdoskopu (śr.)", L"Kaleydoskop ayna (orta)");
	case SC_FX_OIL:
		return LL14(L"筆致方向の色面（油絵・重）", L"Directional brush oil (heavy)", L"Huile à coups de pinceau (lourd)", L"Olio a pennellate (pesante)",
			L"Óleo a pinceladas (pesado)", L"붓방향 유화(무거움)", L"笔触方向油画（重）", L"زيت بضربات فرشاة (ثقيل)",
			L"Масло мазками (тяж.)", L"Öl mit Pinselstrichen (schwer)", L"Óleo com pinceladas (pesado)", L"Olie met penseelstreken (zwaar)",
			L"Olej pociągnięciami (ciężkie)", L"Fırça darbeli yağlı (ağır)");
	case SC_FX_WATERCOLOR:
		return LL14(L"滲み＋紙目の水彩（中）", L"Bleed + paper grain wash (med)", L"Lavis + grain papier (moy)", L"Lavis + grana carta (medio)",
			L"Lavado + grano papel (med)", L"번짐+종이결 수채(중)", L"渗色+纸纹水彩（中）", L"غسل + حبيبات ورق (متوسط)",
			L"Размыв + фактура бумаги (сред.)", L"Ausbluten + Papierkorn (mittel)", L"Sangria + grão papel (méd)", L"Uitlopen + papierkorrel (mid)",
			L"Przeciekanie + ziarno papieru (śr.)", L"Sızma + kağıt dokusu (orta)");
	case SC_FX_PENCIL:
		return LL14(L"ハッチング鉛筆画（中）", L"Hatched pencil sketch (med)", L"Croquis hachuré (moy)", L"Schizzo tratteggiato (medio)",
			L"Boceto rayado (med)", L"해칭 연필화(중)", L"排线铅笔素描（中）", L"رسم بخطوط متقاطعة (متوسط)",
			L"Штриховой карандаш (сред.)", L"Schraffierte Bleistiftskizze (mittel)", L"Esboço hachurado (méd)", L"Gearceerde potloodschets (mid)",
			L"Szkic kreskowany (śr.)", L"Taralı kurşun kalem (orta)");
	case SC_FX_DREAM:
		return LL14(L"ハイライトブルーム夢幻（中）", L"Highlight bloom dream (med)", L"Rêve bloom highlights (moy)", L"Sogno bloom highlights (medio)",
			L"Sueño bloom de brillos (med)", L"하이라이트 블룸 드림(중)", L"高光绽放梦幻（中）", L"حلم بتوهج الإبراز (متوسط)",
			L"Мечта с bloom бликов (сред.)", L"Traum mit Highlight-Bloom (mittel)", L"Sonho com bloom de brilho (méd)", L"Droom met highlight-bloom (mid)",
			L"Sen z bloomem świateł (śr.)", L"Vurgu bloom rüya (orta)");
	case SC_FX_GODRAYS:
		return LL14(L"光条の放射（重）", L"Light shaft rays (heavy)", L"Rayons de lumière (lourd)", L"Raggi di luce (pesante)",
			L"Rayos de luz (pesado)", L"빛줄기 방사(무거움)", L"光束放射（重）", L"أشعة ضوئية (ثقيل)",
			L"Световые лучи (тяж.)", L"Lichtstrahlen (schwer)", L"Raios de luz (pesado)", L"Lichtstralen (zwaar)",
			L"Promienie światła (ciężkie)", L"Işık huzmeleri (ağır)");
	case SC_FX_DISPLACE:
		return LL14(L"ノイズ変位（中）", L"Noise displace (med)", L"Déplacement bruit (moy)", L"Displace rumore (medio)",
			L"Desplazamiento ruido (med)", L"노이즈 변위(중)", L"噪点置换（中）", L"إزاحة ضوضاء (متوسط)",
			L"Смещение шумом (сред.)", L"Rausch-Displace (mittel)", L"Deslocar com ruído (méd)", L"Ruis-displace (mid)",
			L"Przesunięcie szumem (śr.)", L"Gürültü kaydırma (orta)");
	case SC_FX_INTERLACE:
		return LL14(L"奇数行シフト（軽〜中）", L"Odd-line shift (light–med)", L"Décalage lignes impaires (lég–moy)", L"Shift righe dispari (leg–medio)",
			L"Desfase líneas impares (lig–med)", L"홀수 줄 시프트(가벼움~중)", L"奇数行偏移（轻~中）", L"إزاحة الأسطر الفردية (خفيف–متوسط)",
			L"Сдвиг нечётных строк (лёг–сред.)", L"Ungerade Zeilen verschieben (leicht–mittel)", L"Deslocar linhas ímpares (leve–méd)", L"Oneven regels verschuiven (licht–mid)",
			L"Przesunięcie nieparzystych (lek–śr.)", L"Tek satır kaydır (hafif–orta)");
	case SC_FX_CHROMA_HEAVY:
		return LL14(L"強いRGBずれ（中）", L"Strong RGB split (med)", L"Fort décalage RGB (moy)", L"Forte split RGB (medio)",
			L"Fuerte separación RGB (med)", L"강한 RGB 분리(중)", L"强RGB偏移（中）", L"انزياح RGB قوي (متوسط)",
			L"Сильный RGB-сдвиг (сред.)", L"Starke RGB-Trennung (mittel)", L"Forte split RGB (méd)", L"Sterke RGB-split (mid)",
			L"Silny rozdział RGB (śr.)", L"Güçlü RGB kayması (orta)");
	case SC_FX_FOG:
		return LL14(L"距離フォグ（白み・中）", L"Distance fog wash (med)", L"Brouillard de distance (moy)", L"Nebbia di distanza (medio)",
			L"Niebla por distancia (med)", L"거리 안개(중)", L"距离雾化（中）", L"ضباب مسافة (متوسط)",
			L"Дистанционный туман (сред.)", L"Distanznebel (mittel)", L"Névoa por distância (méd)", L"Afstandsmist (mid)",
			L"Mgła dystansowa (śr.)", L"Mesafe sisi (orta)");
	case SC_FX_SHARPEN_HEAVY:
		return LL14(L"強い輪郭強調（中）", L"Strong edge boost (med)", L"Netteté contours forte (moy)", L"Forte enfasi bordi (medio)",
			L"Fuerte realce de bordes (med)", L"강한 윤곽 강조(중)", L"强轮廓锐化（中）", L"تعزيز حواف قوي (متوسط)",
			L"Сильный акцент краёв (сред.)", L"Starke Kantenschärfung (mittel)", L"Forte realce de bordas (méd)", L"Sterke randboost (mid)",
			L"Silne wyostrzenie krawędzi (śr.)", L"Güçlü kenar vurgusu (orta)");
	default:
		return LL14(L"効果なし", L"No effect", L"Aucun effet", L"Nessun effetto",
			L"Sin efecto", L"효과 없음", L"无效果", L"بدون تأثير",
			L"Без эффекта", L"Kein Effekt", L"Sem efeito", L"Geen effect",
			L"Bez efektu", L"Efekt yok");
	}
}

void CScreenCaptureDlg::SyncFxComboFromChain()
{
	if (!m_effect.GetSafeHwnd()) return;
	int chain[SC_FX_CHAIN_MAX] = {};
	int cn = 0;
	GetFxChain(chain, &cn);
	if (cn <= 0) {
		m_effect.SetCurSel(0);
		return;
	}
	if (cn == 1) {
		m_effect.SetCurSel(chain[0]);
		return;
	}
	// 複数段: 「カスタム」行があればそこへ。なければ先頭を表示しつつ末尾カスタムを確保
	const int customIdx = m_effect.FindStringExact(-1, LL14(
		L"(カスタム配線)", L"(Custom wiring)", L"(Câblage perso.)", L"(Cablaggio pers.)",
		L"(Cableado pers.)", L"(사용자 배선)", L"(自定义连线)", L"(توصيل مخصص)",
		L"(Своя схема)", L"(Eigene Verdrahtung)", L"(Ligação pers.)", L"(Eigen bedrading)",
		L"(Własne okablowanie)", L"(Özel kablolama)"));
	if (customIdx >= 0)
		m_effect.SetCurSel(customIdx);
	else
		m_effect.SetCurSel(chain[0]);
}

void CScreenCaptureDlg::ApplyFxComboToChain()
{
	if (!m_effect.GetSafeHwnd()) return;
	const int sel = m_effect.GetCurSel();
	if (sel <= 0) {
		const int empty[SC_FX_CHAIN_MAX] = {};
		m_fxWire.SetChain(empty, 0);
		return;
	}
	if (sel > 0 && sel < SC_FX_COUNT) {
		const int one[1] = { sel };
		m_fxWire.SetChain(one, 1);
	}
}

void CScreenCaptureDlg::OnFxWireChanged()
{
	SyncFxComboFromChain();
	PersistUiToSavedata();
	UpdatePreview(TRUE);
}

CString CScreenCaptureDlg::FxPresetDefaultName(int idx) const
{
	CString s;
	s.Format(LL14(L"配線 %d", L"Wiring %d", L"Câblage %d", L"Cablaggio %d",
		L"Cableado %d", L"배선 %d", L"连线 %d", L"توصيل %d",
		L"Схема %d", L"Verdrahtung %d", L"Ligação %d", L"Bedrading %d",
		L"Okablowanie %d", L"Kablolama %d"), idx + 1);
	return s;
}

void CScreenCaptureDlg::FillFxPresetCombo()
{
	if (!m_fxPre.GetSafeHwnd()) return;
	m_fxPre.ResetContent();
	for (int i = 0; i < 16; ++i) {
		CString name = savedata.cap_fx_pre_name[i];
		name.Trim();
		if (name.IsEmpty())
			name = FxPresetDefaultName(i);
		m_fxPre.AddString(name);
	}
	int sel = savedata.cap_fx_pre_sel;
	if (sel < 0 || sel > 15) sel = 0;
	m_fxPre.SetCurSel(sel);
}

void CScreenCaptureDlg::PersistFxPresetsToSavedata()
{
	// 名前は保存時点でスロットへ書き込む。ここでは選択のみ。
	int sel = m_fxPre.GetSafeHwnd() ? m_fxPre.GetCurSel() : savedata.cap_fx_pre_sel;
	if (sel < 0 || sel > 15) sel = 0;
	savedata.cap_fx_pre_sel = sel;
	MpPersistSavedataQuick();
}

void CScreenCaptureDlg::ApplyFxPreset(int idx)
{
	if (idx < 0 || idx > 15) return;
	int chain[SC_FX_CHAIN_MAX] = {};
	BYTE str[SC_FX_CHAIN_MAX][SC_FX_STR_N];
	memset(str, SC_FX_STR_DEF, sizeof(str));
	int cn = savedata.cap_fx_pre_n[idx];
	if (cn < 0) cn = 0;
	if (cn > SC_FX_CHAIN_MAX) cn = SC_FX_CHAIN_MAX;
	int n = 0;
	for (int i = 0; i < cn; ++i) {
		const int fx = savedata.cap_fx_pre_fx[idx][i];
		if (fx > SC_FX_NONE && fx < SC_FX_COUNT) {
			chain[n] = fx;
			memcpy(str[n], savedata.cap_fx_pre_str[idx][i], SC_FX_STR_N);
			n++;
		}
	}
	m_fxWire.SetChain(chain, n, str);
	savedata.cap_fx_pre_sel = idx;
	SyncFxComboFromChain();
	PersistUiToSavedata();
	UpdatePreview(TRUE);
}

void CScreenCaptureDlg::SaveFxPreset(int idx)
{
	if (idx < 0 || idx > 15) return;
	int chain[SC_FX_CHAIN_MAX] = {};
	BYTE str[SC_FX_CHAIN_MAX][SC_FX_STR_N];
	memset(str, SC_FX_STR_DEF, sizeof(str));
	int cn = 0;
	GetFxChain(chain, &cn, str);
	savedata.cap_fx_pre_n[idx] = cn;
	memset(savedata.cap_fx_pre_fx[idx], 0, sizeof(savedata.cap_fx_pre_fx[idx]));
	memset(savedata.cap_fx_pre_str[idx], SC_FX_STR_DEF, sizeof(savedata.cap_fx_pre_str[idx]));
	for (int i = 0; i < cn && i < 8; ++i) {
		savedata.cap_fx_pre_fx[idx][i] = chain[i];
		memcpy(savedata.cap_fx_pre_str[idx][i], str[i], SC_FX_STR_N);
	}
	CString name;
	if (m_fxPre.GetSafeHwnd())
		m_fxPre.GetWindowText(name);
	name.Trim();
	if (name.IsEmpty())
		name = FxPresetDefaultName(idx);
	_tcsncpy(savedata.cap_fx_pre_name[idx], name, _countof(savedata.cap_fx_pre_name[idx]) - 1);
	savedata.cap_fx_pre_name[idx][_countof(savedata.cap_fx_pre_name[idx]) - 1] = 0;
	savedata.cap_fx_pre_sel = idx;
	if (m_fxPre.GetSafeHwnd()) {
		m_fxPre.DeleteString(idx);
		m_fxPre.InsertString(idx, name);
		m_fxPre.SetCurSel(idx);
	}
	MpPersistSavedataQuick();
}

void CScreenCaptureDlg::OnBnClickedFxPreLoad()
{
	if (m_uiLocked) return;
	int sel = m_fxPre.GetCurSel();
	if (sel < 0) sel = savedata.cap_fx_pre_sel;
	if (sel < 0 || sel > 15) sel = 0;
	ApplyFxPreset(sel);
}

void CScreenCaptureDlg::OnBnClickedFxPreSave()
{
	if (m_uiLocked) return;
	int sel = m_fxPre.GetCurSel();
	if (sel < 0) {
		// DROPDOWN 編集中は GetCurSel==-1 になり得る → テキスト一致 or 前回選択
		CString cur;
		m_fxPre.GetWindowText(cur);
		cur.Trim();
		sel = savedata.cap_fx_pre_sel;
		if (sel < 0 || sel > 15) sel = 0;
		for (int i = 0; i < 16; ++i) {
			CString n = savedata.cap_fx_pre_name[i];
			n.Trim();
			if (n.IsEmpty()) n = FxPresetDefaultName(i);
			if (n == cur) { sel = i; break; }
		}
	}
	SaveFxPreset(sel);
}

void CScreenCaptureDlg::OnCbnSelchangeFxPre()
{
	if (m_uiLocked) return;
	int sel = m_fxPre.GetCurSel();
	if (sel < 0 || sel > 15) return;
	savedata.cap_fx_pre_sel = sel;
	MpPersistSavedataQuick();
}

void CScreenCaptureDlg::PersistUiToSavedata()
{
	if (!GetSafeHwnd()) return;
	savedata.cap_with_audio = m_audio.GetCheck() ? 1 : 0;
	savedata.cap_with_mic = m_mic.GetCheck() ? 1 : 0;
	savedata.cap_include_mp = m_includeMp.GetCheck() ? 1 : 0;
	savedata.cap_show_cursor = m_showCursor.GetCheck() ? 1 : 0;
	savedata.cap_fps = CurrentPreviewFps();
	int monIdx = 0;
	int mode = ModeComboToSavedMode(m_mode.GetCurSel(), monIdx);
	savedata.cap_mode = mode;
	savedata.cap_monitor_idx = monIdx;
	int canvas = m_canvas.GetCurSel();
	if (canvas < 0 || canvas > 4) canvas = 2;
	savedata.cap_canvas_preset = canvas;
	int cw = 0, ch = 0;
	ResolveCanvasSize(cw, ch);
	savedata.cap_canvas_w = cw;
	savedata.cap_canvas_h = ch;
	{
		int chain[SC_FX_CHAIN_MAX] = {};
		BYTE str[SC_FX_CHAIN_MAX][SC_FX_STR_N];
		memset(str, SC_FX_STR_DEF, sizeof(str));
		int cn = 0;
		GetFxChain(chain, &cn, str);
		savedata.cap_fx_n = cn;
		savedata.cap_fx0 = (cn > 0) ? chain[0] : 0;
		savedata.cap_fx1 = (cn > 1) ? chain[1] : 0;
		savedata.cap_fx2 = (cn > 2) ? chain[2] : 0;
		savedata.cap_fx3 = (cn > 3) ? chain[3] : 0;
		savedata.cap_fx4 = (cn > 4) ? chain[4] : 0;
		savedata.cap_fx5 = (cn > 5) ? chain[5] : 0;
		savedata.cap_fx6 = (cn > 6) ? chain[6] : 0;
		savedata.cap_fx7 = (cn > 7) ? chain[7] : 0;
		savedata.cap_effect = savedata.cap_fx0;
		memset(savedata.cap_fx_str, SC_FX_STR_DEF, sizeof(savedata.cap_fx_str));
		for (int i = 0; i < cn && i < 8; ++i)
			memcpy(savedata.cap_fx_str[i], str[i], SC_FX_STR_N);
	}
	if (m_fxPre.GetSafeHwnd()) {
		int sel = m_fxPre.GetCurSel();
		if (sel < 0 || sel > 15) sel = savedata.cap_fx_pre_sel;
		if (sel < 0 || sel > 15) sel = 0;
		savedata.cap_fx_pre_sel = sel;
	}
	CString path;
	m_path.GetWindowText(path);
	path = NormalizeOutPath(path);
	_tcsncpy(savedata.cap_last_path, path, _countof(savedata.cap_last_path) - 1);
	savedata.cap_last_path[_countof(savedata.cap_last_path) - 1] = 0;
	PersistLiveFieldsFromUi();
	MpPersistSavedataQuick();
}

void CScreenCaptureDlg::EnableComposeUi(BOOL /*enable*/)
{
	// EnableWindow はアクリル透過を壊すので触らない。操作可否はハンドラ側で判定。
	RefreshOpaqueUi();
}

void CScreenCaptureDlg::SetRecordingUi(BOOL recording)
{
	m_uiLocked = recording;
	const BOOL liveUi = m_liveMode || (m_live.GetSafeHwnd() && m_live.GetCheck());
	if (recording) {
		m_start.SetWindowText(liveUi
			? LL14(L"配信停止", L"Stop live", L"Arrêter le live", L"Ferma diretta",
				L"Detener vivo", L"방송 중지", L"停止直播", L"إيقاف البث",
				L"Стоп эфир", L"Live stoppen", L"Parar ao vivo", L"Live stoppen",
				L"Zatrzymaj transmisję", L"Yayını durdur")
			: LL14(L"録画停止", L"Stop", L"Arrêter", L"Stop", L"Detener", L"중지", L"停止", L"إيقاف",
				L"Стоп", L"Stopp", L"Parar", L"Stop", L"Stop", L"Durdur"));
	} else {
		m_start.SetWindowText(liveUi
			? LL14(L"配信開始", L"Go live", L"Diffuser", L"Vai in diretta",
				L"Emitir", L"방송 시작", L"开始直播", L"بدء البث",
				L"В эфир", L"Live starten", L"Entrar ao vivo", L"Live starten",
				L"Rozpocznij transmisję", L"Yayına başla")
			: LL14(L"録画開始", L"Start", L"Démarrer", L"Avvia", L"Iniciar", L"시작", L"开始", L"بدء",
				L"Старт", L"Start", L"Iniciar", L"Start", L"Start", L"Başlat"));
	}
	const BOOL compose = IsWindowComposeMode();
	EnableComposeUi(compose);
	// EnableWindow は透過を壊すのでモード/キャンバスは PreTranslate でロック
	RefreshOpaqueUi();
	// 録画中はプレビュー描画を間引きすぎない（配信中の「停止」見えを防ぐ）
	if (recording) {
		KillTimer(SC_TIMER_PREV);
		SetTimer(SC_TIMER_PREV, m_liveMode ? 100 : 200, NULL);
	} else {
		ApplyPreviewTimer();
	}
}

void CScreenCaptureDlg::UpdateElapsedUi()
{
	if (!GetSafeHwnd() || !m_time.GetSafeHwnd()) return;
	const int setFps = CurrentPreviewFps();
	const double prevFps = InterlockedCompareExchange(&m_prevFpsX10, 0, 0) / 10.0;
	const double encFps = InterlockedCompareExchange(&m_encFpsX10, 0, 0) / 10.0;
	CString t;
	if (!InterlockedCompareExchange(&m_run, 0, 0)) {
		t.Format(L"Preview %.1f fps  (set %d)", prevFps, setFps);
		m_time.SetWindowText(t);
		return;
	}
	const DWORD ms = GetTickCount() - m_startTick;
	const int sec = (int)(ms / 1000);
	const LONG frames = InterlockedCompareExchange(&m_frameCnt, 0, 0);
	double avgEnc = 0.0;
	if (ms > 200 && frames > 0)
		avgEnc = (frames * 1000.0) / (double)ms;
	t.Format(L"%02d:%02d  Enc %.1f fps (avg %.1f)  Prev %.1f  set %d  %ldf",
		sec / 60, sec % 60, encFps, avgEnc, prevFps, setFps, (long)frames);
	if (m_liveMode && m_liveService == 0) {
		const LONG ph = InterlockedCompareExchange(&m_ytLivePhase, 0, 0);
		if (ph == 1) t += L"  [YT: 受信待ち]";
		else if (ph == 2) t += L"  [YT: 開始中]";
		else if (ph == 3) t += L"  [YT: ライブ]";
		else if (ph == 4) t += L"  [YT: 開始失敗]";
		else if (ph == 5) t += L"  [YT: 枠なし]";
		else if (ph == 6) t += L"  [YT: ffmpeg終了]";
	}
	m_time.SetWindowText(t);
}

void CScreenCaptureDlg::RefreshAvailList()
{
	m_avail.ResetContent();
	m_availCnt = 0;
	ScEnumCtx ctx = {};
	ctx.skip = GetSafeHwnd();
	ctx.hwnds = m_availHwnd;
	ctx.cnt = &m_availCnt;
	ctx.maxCnt = SC_AVAIL_MAX;
	EnumWindows(ScEnumWindowsProc, (LPARAM)&ctx);
	for (int i = 0; i < m_availCnt; ++i) {
		wchar_t title[256] = {};
		::GetWindowTextW(m_availHwnd[i], title, 255);
		m_avail.AddString(title);
	}
}

void CScreenCaptureDlg::RefreshLayerList()
{
	const int sel = m_layer.GetCurSel();
	m_layer.ResetContent();
	for (int i = 0; i < m_layerCnt; ++i) {
		CString s;
		s.Format(L"%s%s[Z%d] %s  (%d,%d %dx%d)%s",
			m_layers[i].hidden ? L"[Hide] " : L"",
			m_layers[i].isMp ? L"[MP] " : L"",
			i, m_layers[i].title, m_layers[i].x, m_layers[i].y, m_layers[i].w, m_layers[i].h,
			(m_layers[i].srcW > 1 && m_layers[i].srcH > 1) ? L" crop" : L"");
		m_layer.AddString(s);
	}
	if (sel >= 0 && sel < m_layerCnt)
		m_layer.SetCurSel(sel);
	else if (m_layerCnt > 0)
		m_layer.SetCurSel(0);
	SyncGeoEditsFromSel();
}

void CScreenCaptureDlg::SyncGeoEditsFromSel()
{
	const int sel = m_layer.GetCurSel();
	if (sel < 0 || sel >= m_layerCnt) {
		m_editX.SetWindowText(L"");
		m_editY.SetWindowText(L"");
		m_editW.SetWindowText(L"");
		m_editH.SetWindowText(L"");
		m_editSX.SetWindowText(L"");
		m_editSY.SetWindowText(L"");
		m_editSW.SetWindowText(L"");
		m_editSH.SetWindowText(L"");
		return;
	}
	CString s;
	s.Format(L"%d", m_layers[sel].x); m_editX.SetWindowText(s);
	s.Format(L"%d", m_layers[sel].y); m_editY.SetWindowText(s);
	s.Format(L"%d", m_layers[sel].w); m_editW.SetWindowText(s);
	s.Format(L"%d", m_layers[sel].h); m_editH.SetWindowText(s);
	s.Format(L"%d", m_layers[sel].srcX); m_editSX.SetWindowText(s);
	s.Format(L"%d", m_layers[sel].srcY); m_editSY.SetWindowText(s);
	s.Format(L"%d", m_layers[sel].srcW); m_editSW.SetWindowText(s);
	s.Format(L"%d", m_layers[sel].srcH); m_editSH.SetWindowText(s);
}

void CScreenCaptureDlg::ApplyGeoEditsToSel()
{
	const int sel = m_layer.GetCurSel();
	if (sel < 0 || sel >= m_layerCnt) return;
	CString sx, sy, sw, sh;
	m_editX.GetWindowText(sx); m_editY.GetWindowText(sy);
	m_editW.GetWindowText(sw); m_editH.GetWindowText(sh);
	m_layers[sel].x = _ttoi(sx);
	m_layers[sel].y = _ttoi(sy);
	m_layers[sel].w = _ttoi(sw);
	m_layers[sel].h = _ttoi(sh);
	if (m_layers[sel].w < 2) m_layers[sel].w = 2;
	if (m_layers[sel].h < 2) m_layers[sel].h = 2;
	CString csx, csy, csw, csh;
	m_editSX.GetWindowText(csx); m_editSY.GetWindowText(csy);
	m_editSW.GetWindowText(csw); m_editSH.GetWindowText(csh);
	m_layers[sel].srcX = _ttoi(csx);
	m_layers[sel].srcY = _ttoi(csy);
	m_layers[sel].srcW = _ttoi(csw);
	m_layers[sel].srcH = _ttoi(csh);
	if (m_layers[sel].srcX < 0) m_layers[sel].srcX = 0;
	if (m_layers[sel].srcY < 0) m_layers[sel].srcY = 0;
	if (m_layers[sel].srcW < 0) m_layers[sel].srcW = 0;
	if (m_layers[sel].srcH < 0) m_layers[sel].srcH = 0;
	RefreshLayerList();
	m_layer.SetCurSel(sel);
	UpdatePreview();
}

void CScreenCaptureDlg::AddLayerHwnd(HWND hwnd, BOOL isMp)
{
	if (!hwnd || !::IsWindow(hwnd)) return;
	hwnd = ::GetAncestor(hwnd, GA_ROOT);
	if (!hwnd || hwnd == GetSafeHwnd()) return;
	for (int i = 0; i < m_layerCnt; ++i) {
		if (m_layers[i].hwnd == hwnd) {
			if (isMp) m_layers[i].isMp = TRUE;
			m_layer.SetCurSel(i);
			SyncGeoEditsFromSel();
			UpdatePreview();
			return;
		}
	}
	if (m_layerCnt >= SC_LAYER_MAX) {
		m_status.SetWindowText(LL14(
			L"レイヤは最大16個までです。", L"Up to 16 layers.", L"16 calques max.", L"Max 16 livelli.",
			L"Máx. 16 capas.", L"레이어는 최대 16개입니다.", L"最多16层。", L"بحد أقصى 16 طبقة.",
			L"Максимум 16 слоёв.", L"Max. 16 Ebenen.", L"Máx. 16 camadas.", L"Max. 16 lagen.",
			L"Maks. 16 warstw.", L"En fazla 16 katman."));
		return;
	}
	Layer& L = m_layers[m_layerCnt];
	memset(&L, 0, sizeof(L));
	L.hwnd = hwnd;
	L.isMp = isMp;
	if (isMp)
		EnsureMpDefaultRect(L);
	else {
		RECT wr = {};
		::GetWindowRect(hwnd, &wr);
		L.x = 0;
		L.y = 0;
		L.w = wr.right - wr.left;
		L.h = wr.bottom - wr.top;
		if (L.w < 2) L.w = 2;
		if (L.h < 2) L.h = 2;
		int cw = 0, ch = 0;
		ResolveCanvasSize(cw, ch);
		FitLayerIntoCanvas(L, cw, ch);
	}
	::GetWindowText(hwnd, L.title, _countof(L.title) - 1);
	if (!L.title[0])
		_tcscpy_s(L.title, isMp ? L"Media Player" : L"(window)");
	m_layerCnt++;
	if (!isMp && !IsWindowComposeMode())
		m_mode.SetCurSel(SavedModeToComboSel(SC_MODE_WINDOWS, 0));
	EnableComposeUi(TRUE);
	// 追加レイヤ以外の配置は維持（全体再中央寄せしない）
	{
		int cw = 0, ch = 0;
		ResolveCanvasSize(cw, ch);
		for (int i = 0; i < m_layerCnt - 1; ++i)
			ClampLayerToCanvas(m_layers[i], cw, ch);
	}
	RefreshLayerList();
	m_layer.SetCurSel(m_layerCnt - 1);
	SyncGeoEditsFromSel();
	UpdatePreview(TRUE);
}

HWND CScreenCaptureDlg::FindMediaPlayerHwnd() const
{
	extern CMediaPlayerDlg* mp;
	if (mp && mp->GetSafeHwnd() && ::IsWindow(mp->GetSafeHwnd()))
		return mp->GetSafeHwnd();
	return NULL;
}

void CScreenCaptureDlg::EnsureMpDefaultRect(Layer& L) const
{
	int cw = 0, ch = 0;
	ResolveCanvasSize(cw, ch);
	RECT wr = {};
	int nw = 480, nh = 360;
	if (L.hwnd && ::IsWindow(L.hwnd) && ::GetWindowRect(L.hwnd, &wr)) {
		nw = wr.right - wr.left;
		nh = wr.bottom - wr.top;
	}
	// キャンバス右下に 45% 程度で配置
	L.w = (cw * 45) / 100;
	L.h = (int)(((__int64)L.w * nh) / (nw > 0 ? nw : 1));
	if (L.w < 160) L.w = 320;
	if (L.h < 120) L.h = 240;
	if (L.w > cw) L.w = cw;
	if (L.h > ch) L.h = ch;
	L.x = cw - L.w - 12;
	L.y = ch - L.h - 12;
	if (L.x < 0) L.x = 0;
	if (L.y < 0) L.y = 0;
}

void CScreenCaptureDlg::SyncMpLayerFromCheck()
{
	const BOOL want = m_includeMp.GetCheck() ? TRUE : FALSE;
	if (want) {
		HWND h = FindMediaPlayerHwnd();
		if (!h) {
			m_status.SetWindowText(LL14(
				L"MP画面が開いていません。先にメディアプレイヤーを開いてください。",
				L"MP is not open. Open the media player first.",
				L"MP n'est pas ouvert. Ouvrez d'abord le lecteur.",
				L"MP non aperto. Apri prima il media player.",
				L"MP no está abierto. Abra el reproductor primero.",
				L"MP가 열려 있지 않습니다. 먼저 미디어 플레이어를 여세요.",
				L"未打开MP。请先打开媒体播放器。",
				L"MP غير مفتوح. افتح المشغّل أولاً.",
				L"MP не открыт. Сначала откройте плеер.",
				L"MP ist nicht offen. Öffnen Sie zuerst den Player.",
				L"MP não está aberto. Abra o leitor primeiro.",
				L"MP is niet open. Open eerst de mediaplayer.",
				L"MP nie jest otwarty. Najpierw otwórz odtwarzacz.",
				L"MP açık değil. Önce medya oynatıcıyı açın."));
			m_includeMp.SetCheck(BST_UNCHECKED);
			return;
		}
		AddLayerHwnd(h, TRUE);
		m_status.SetWindowText(LL14(
			L"MPをレイヤに追加。配置はドラッグ、音だけなら右クリック→Hide。",
			L"MP added as layer. Drag to place; right-click Hide for audio-only.",
			L"MP ajouté. Glissez; clic droit Hide pour audio seul.",
			L"MP aggiunto. Trascina; destro Hide per solo audio.",
			L"MP añadido. Arrastre; clic der. Hide para solo audio.",
			L"MP를 레이어에 추가. 드래그로 배치, 오디오만이면 우클릭→Hide.",
			L"已将MP加入层。拖动放置；只要声音则右键→Hide。",
			L"أُضيف MP. اسحب؛ يمين Hide للصوت فقط.",
			L"MP добавлен. Перетаскивайте; ПКМ Hide — только звук.",
			L"MP hinzugefügt. Ziehen; Rechtsklick Hide für nur Audio.",
			L"MP adicionado. Arraste; direito Hide para só áudio.",
			L"MP toegevoegd. Sleep; rechtsklik Hide voor alleen audio.",
			L"Dodano MP. Przeciągaj; PPM Hide dla samego dźwięku.",
			L"MP eklendi. Sürükleyin; yalnızca ses için sağ tık→Hide."));
	} else {
		for (int i = m_layerCnt - 1; i >= 0; --i) {
			if (!m_layers[i].isMp) continue;
			for (int j = i; j < m_layerCnt - 1; ++j)
				m_layers[j] = m_layers[j + 1];
			m_layerCnt--;
		}
		RefreshLayerList();
		UpdatePreview();
	}
	PersistUiToSavedata();
}

void CScreenCaptureDlg::OnBnClickedIncludeMp()
{
	if (m_uiLocked) return;
	SyncMpLayerFromCheck();
}

void CScreenCaptureDlg::OnBnClickedShowCursor()
{
	if (m_uiLocked) return;
	PersistUiToSavedata();
	UpdatePreview(TRUE);
}

void CScreenCaptureDlg::OnCbnSelchangeMicDev()
{
	AudioMicDevApplyFromCombo(m_micDev);
}

void CScreenCaptureDlg::OnCbnSelchangeLoopDev()
{
	AudioLoopDevApplyFromCombo(m_loopDev);
}

void CScreenCaptureDlg::OnBnClickedMic()
{
	if (m_uiLocked) return;
	PersistUiToSavedata();
	// PeakMonitor は起動時に mic を開くか決める。チェック変更で取り直す。
	if (InterlockedCompareExchange(&m_run, 0, 0) == 0) {
		StopPeakMonitor();
		StartPeakMonitor();
	}
}

void CScreenCaptureDlg::ToggleLayerHidden(int layerIdx)
{
	if (layerIdx < 0 || layerIdx >= m_layerCnt) return;
	m_layers[layerIdx].hidden = !m_layers[layerIdx].hidden;
	const BOOL hid = m_layers[layerIdx].hidden;
	RefreshLayerList();
	m_layer.SetCurSel(layerIdx);
	SyncGeoEditsFromSel();
	UpdatePreview(TRUE);
	m_status.SetWindowText(hid
		? LL14(
			L"レイヤを非表示にしました（音だけ載せたいときに）。もう一度右クリックで表示。",
			L"Layer hidden (use for audio-only). Right-click again to show.",
			L"Calque masqué (audio seul). Clic droit pour réafficher.",
			L"Livello nascosto (solo audio). Clic destro per mostrare.",
			L"Capa oculta (solo audio). Clic derecho para mostrar.",
			L"레이어를 숨겼습니다(오디오만). 다시 우클릭하면 표시.",
			L"已隐藏层（适合只录声音）。再右键可显示。",
			L"تم إخفاء الطبقة (للصوت فقط). انقر باليمين لإظهارها.",
			L"Слой скрыт (только звук). ПКМ — показать снова.",
			L"Ebene ausgeblendet (nur Audio). Rechtsklick zum Einblenden.",
			L"Camada oculta (só áudio). Clique direito para mostrar.",
			L"Laag verborgen (alleen audio). Rechtsklik om te tonen.",
			L"Warstwa ukryta (tylko dźwięk). PPM — pokaż.",
			L"Katman gizlendi (yalnızca ses). Sağ tık ile göster.")
		: LL14(
			L"レイヤを再表示しました。",
			L"Layer shown again.",
			L"Calque réaffiché.",
			L"Livello mostrato di nuovo.",
			L"Capa visible de nuevo.",
			L"레이어를 다시 표시했습니다.",
			L"已重新显示层。",
			L"أُظهرَت الطبقة مجدداً.",
			L"Слой снова виден.",
			L"Ebene wieder sichtbar.",
			L"Camada visível novamente.",
			L"Laag weer zichtbaar.",
			L"Warstwa znów widoczna.",
			L"Katman tekrar görünür."));
}

void CScreenCaptureDlg::FitSelected(int scalePercent)
{
	const int sel = m_layer.GetCurSel();
	if (sel < 0 || sel >= m_layerCnt) return;
	Layer& L = m_layers[sel];
	if (!L.hwnd || !IsWindow(L.hwnd)) return;
	RECT wr = {};
	::GetWindowRect(L.hwnd, &wr);
	const int nw = wr.right - wr.left;
	const int nh = wr.bottom - wr.top;
	int cw = 0, ch = 0;
	ResolveCanvasSize(cw, ch);
	if (scalePercent <= 0) {
		// アスペクト維持でキャンバスにフィット
		if (nw < 1 || nh < 1) return;
		const double sx = (double)cw / (double)nw;
		const double sy = (double)ch / (double)nh;
		const double s = sx < sy ? sx : sy;
		L.w = (int)(nw * s);
		L.h = (int)(nh * s);
		if (L.w < 2) L.w = 2;
		if (L.h < 2) L.h = 2;
		L.x = (cw - L.w) / 2;
		L.y = (ch - L.h) / 2;
	} else {
		L.w = (nw * scalePercent) / 100;
		L.h = (nh * scalePercent) / 100;
		if (L.w < 2) L.w = 2;
		if (L.h < 2) L.h = 2;
	}
	RefreshLayerList();
	m_layer.SetCurSel(sel);
	UpdatePreview();
}

void CScreenCaptureDlg::TileLayers()
{
	if (m_layerCnt <= 0) return;
	int cw = 0, ch = 0;
	ResolveCanvasSize(cw, ch);
	const int cols = (int)ceil(sqrt((double)m_layerCnt));
	const int rows = (m_layerCnt + cols - 1) / cols;
	const int cellW = cw / cols;
	const int cellH = ch / rows;
	for (int i = 0; i < m_layerCnt; ++i) {
		const int col = i % cols;
		const int row = i / cols;
		Layer& L = m_layers[i];
		RECT wr = {};
		int nw = cellW, nh = cellH;
		if (L.hwnd && IsWindow(L.hwnd) && ::GetWindowRect(L.hwnd, &wr)) {
			nw = wr.right - wr.left;
			nh = wr.bottom - wr.top;
		}
		double sx = (double)cellW / (double)(nw > 0 ? nw : 1);
		double sy = (double)cellH / (double)(nh > 0 ? nh : 1);
		double s = sx < sy ? sx : sy;
		L.w = (int)(nw * s); if (L.w < 2) L.w = 2;
		L.h = (int)(nh * s); if (L.h < 2) L.h = 2;
		L.x = col * cellW + (cellW - L.w) / 2;
		L.y = row * cellH + (cellH - L.h) / 2;
	}
	RefreshLayerList();
	UpdatePreview();
}

void CScreenCaptureDlg::RefreshComposeCache()
{
	ComposeSnap snap;
	BuildComposeSnap(snap);
	ScFrameBuf fb = {};
	// プレビューは WGC 優先。録画スレッド側は別途 GDI フォールバック付き。
	if (!ScComposeFrame(fb, snap, FALSE)) {
		ScFrameFree(fb);
		return;
	}
	if (!m_cacheDc || !m_cacheBmp || m_cacheW != fb.w || m_cacheH != fb.h) {
		if (m_cacheDc) {
			if (m_cacheOld) SelectObject(m_cacheDc, m_cacheOld);
			DeleteDC(m_cacheDc);
			m_cacheDc = NULL;
			m_cacheOld = NULL;
		}
		if (m_cacheBmp) {
			DeleteObject(m_cacheBmp);
			m_cacheBmp = NULL;
		}
		m_cacheBits = NULL;
		BITMAPINFO bmi = {};
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = fb.w;
		bmi.bmiHeader.biHeight = -fb.h;
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;
		HDC screen = ::GetDC(NULL);
		void* bits = NULL;
		m_cacheBmp = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
		::ReleaseDC(NULL, screen);
		if (!m_cacheBmp || !bits) {
			ScFrameFree(fb);
			return;
		}
		m_cacheBits = (BYTE*)bits;
		m_cacheDc = CreateCompatibleDC(NULL);
		m_cacheOld = SelectObject(m_cacheDc, m_cacheBmp);
		m_cacheW = fb.w;
		m_cacheH = fb.h;
	}
	BitBlt(m_cacheDc, 0, 0, fb.w, fb.h, fb.hdc, 0, 0, SRCCOPY);
	ScFrameFree(fb);

	// プレビュー実FPS（約1秒窓）
	const DWORD now = GetTickCount();
	if (m_prevFpsWinTick == 0)
		m_prevFpsWinTick = now;
	m_prevFpsWinCnt++;
	const DWORD elapsed = now - m_prevFpsWinTick;
	if (elapsed >= 800) {
		const LONG x10 = (LONG)((m_prevFpsWinCnt * 10000.0) / (double)elapsed + 0.5);
		InterlockedExchange(&m_prevFpsX10, x10);
		m_prevFpsWinTick = now;
		m_prevFpsWinCnt = 0;
	}
}

BOOL CScreenCaptureDlg::GetPreviewMap(CRect& imageRect, float& scale, int& canvasW, int& canvasH) const
{
	if (!m_preview.GetSafeHwnd()) return FALSE;
	CRect client;
	m_preview.GetClientRect(&client);
	ResolveCanvasSize(canvasW, canvasH);
	if (m_cacheW > 0 && m_cacheH > 0) {
		canvasW = m_cacheW;
		canvasH = m_cacheH;
	}
	if (canvasW < 1 || canvasH < 1 || client.Width() < 1 || client.Height() < 1)
		return FALSE;
	const float sx = (float)client.Width() / (float)canvasW;
	const float sy = (float)client.Height() / (float)canvasH;
	scale = (sx < sy) ? sx : sy;
	const int dw = (int)(canvasW * scale);
	const int dh = (int)(canvasH * scale);
	imageRect.left = client.left + (client.Width() - dw) / 2;
	imageRect.top = client.top + (client.Height() - dh) / 2;
	imageRect.right = imageRect.left + dw;
	imageRect.bottom = imageRect.top + dh;
	return TRUE;
}

BOOL CScreenCaptureDlg::PreviewToCanvas(CPoint ptClient, int& cx, int& cy) const
{
	CRect imageRect;
	float scale = 1.f;
	int cw = 0, ch = 0;
	if (!GetPreviewMap(imageRect, scale, cw, ch) || scale < 0.0001f)
		return FALSE;
	cx = (int)((ptClient.x - imageRect.left) / scale);
	cy = (int)((ptClient.y - imageRect.top) / scale);
	return TRUE;
}

CRect CScreenCaptureDlg::CanvasToPreview(int x, int y, int w, int h) const
{
	CRect imageRect;
	float scale = 1.f;
	int cw = 0, ch = 0;
	CRect r(0, 0, 0, 0);
	if (!GetPreviewMap(imageRect, scale, cw, ch))
		return r;
	r.left = imageRect.left + (int)(x * scale);
	r.top = imageRect.top + (int)(y * scale);
	r.right = imageRect.left + (int)((x + w) * scale);
	r.bottom = imageRect.top + (int)((y + h) * scale);
	return r;
}

int CScreenCaptureDlg::HitTestPreview(CPoint ptClient, int* outHandle) const
{
	if (outHandle) *outHandle = SC_HIT_NONE;
	if (m_layerCnt <= 0) return -1;
	const BOOL winMode = IsWindowComposeMode();
	const BOOL mpOnly = (!winMode && const_cast<CCustomCheckBox&>(m_includeMp).GetCheck());
	if (!winMode && !mpOnly) return -1;

	const int sel = m_layer.GetCurSel();
	const int hs = 10;

	auto hitHandle = [&](const CRect& rr, CPoint pt) -> int {
		CRect tl(rr.left - 2, rr.top - 2, rr.left + hs, rr.top + hs);
		CRect tr(rr.right - hs, rr.top - 2, rr.right + 2, rr.top + hs);
		CRect bl(rr.left - 2, rr.bottom - hs, rr.left + hs, rr.bottom + 2);
		CRect br(rr.right - hs, rr.bottom - hs, rr.right + 2, rr.bottom + 2);
		if (tl.PtInRect(pt)) return SC_HIT_TL;
		if (tr.PtInRect(pt)) return SC_HIT_TR;
		if (bl.PtInRect(pt)) return SC_HIT_BL;
		if (br.PtInRect(pt)) return SC_HIT_BR;
		return SC_HIT_NONE;
	};

	auto layerOk = [&](int i) -> BOOL {
		if (i < 0 || i >= m_layerCnt) return FALSE;
		if (winMode) return TRUE;
		return m_layers[i].isMp;
	};

	if (layerOk(sel)) {
		CRect rr = CanvasToPreview(m_layers[sel].x, m_layers[sel].y, m_layers[sel].w, m_layers[sel].h);
		const int h = hitHandle(rr, ptClient);
		if (h != SC_HIT_NONE) {
			if (outHandle) *outHandle = h;
			return sel;
		}
	}

	for (int i = 0; i < m_layerCnt; ++i) {
		if (!layerOk(i)) continue;
		CRect rr = CanvasToPreview(m_layers[i].x, m_layers[i].y, m_layers[i].w, m_layers[i].h);
		rr.InflateRect(1, 1);
		if (rr.PtInRect(ptClient)) {
			if (outHandle) *outHandle = SC_HIT_BODY;
			return i;
		}
	}
	return -1;
}

void CScreenCaptureDlg::BeginPreviewDrag(int layer, int handle, CPoint ptClient)
{
	if (layer < 0 || layer >= m_layerCnt) return;
	int cx = 0, cy = 0;
	if (!PreviewToCanvas(ptClient, cx, cy)) return;
	m_dragging = TRUE;
	m_dragLayer = layer;
	m_dragHandle = (handle == SC_HIT_NONE) ? SC_HIT_BODY : handle;
	m_dragStartCx = cx;
	m_dragStartCy = cy;
	m_dragOrigX = m_layers[layer].x;
	m_dragOrigY = m_layers[layer].y;
	m_dragOrigW = m_layers[layer].w;
	m_dragOrigH = m_layers[layer].h;
}

void CScreenCaptureDlg::UpdatePreviewDrag(CPoint ptClient)
{
	if (!m_dragging || m_dragLayer < 0 || m_dragLayer >= m_layerCnt) return;
	int cx = 0, cy = 0;
	if (!PreviewToCanvas(ptClient, cx, cy)) return;
	const int dx = cx - m_dragStartCx;
	const int dy = cy - m_dragStartCy;
	Layer& L = m_layers[m_dragLayer];
	int x = m_dragOrigX, y = m_dragOrigY, w = m_dragOrigW, h = m_dragOrigH;
	const int minSz = 16;
	switch (m_dragHandle) {
	case SC_HIT_BODY:
		x = m_dragOrigX + dx;
		y = m_dragOrigY + dy;
		break;
	case SC_HIT_TL:
		x = m_dragOrigX + dx; y = m_dragOrigY + dy;
		w = m_dragOrigW - dx; h = m_dragOrigH - dy;
		break;
	case SC_HIT_TR:
		y = m_dragOrigY + dy;
		w = m_dragOrigW + dx; h = m_dragOrigH - dy;
		break;
	case SC_HIT_BL:
		x = m_dragOrigX + dx;
		w = m_dragOrigW - dx; h = m_dragOrigH + dy;
		break;
	case SC_HIT_BR:
		w = m_dragOrigW + dx; h = m_dragOrigH + dy;
		break;
	default:
		break;
	}
	if (w < minSz) {
		if (m_dragHandle == SC_HIT_TL || m_dragHandle == SC_HIT_BL)
			x -= (minSz - w);
		w = minSz;
	}
	if (h < minSz) {
		if (m_dragHandle == SC_HIT_TL || m_dragHandle == SC_HIT_TR)
			y -= (minSz - h);
		h = minSz;
	}
	L.x = x; L.y = y; L.w = w; L.h = h;
	{
		int cw = 0, ch = 0;
		ResolveCanvasSize(cw, ch);
		ClampLayerToCanvas(L, cw, ch);
	}
	SyncGeoEditsFromSel();
	// ドラッグ中も合成プレビューを追従（HUDだけ動いて中身が戻るのを防ぐ）
	if (!InterlockedCompareExchange(&m_run, 0, 0))
		RefreshComposeCache();
}

void CScreenCaptureDlg::EndPreviewDrag()
{
	if (!m_dragging) return;
	m_dragging = FALSE;
	const int layer = m_dragLayer;
	m_dragHandle = SC_HIT_NONE;
	m_dragLayer = -1;
	if (layer >= 0 && layer < m_layerCnt) {
		int cw = 0, ch = 0;
		ResolveCanvasSize(cw, ch);
		ClampLayerToCanvas(m_layers[layer], cw, ch);
	}
	RefreshLayerList();
	if (layer >= 0 && layer < m_layerCnt)
		m_layer.SetCurSel(layer);
	SyncGeoEditsFromSel();
	UpdatePreview(TRUE);
}

void CScreenCaptureDlg::DrawPreviewHud(CDC& dc, const CRect& imageRect, float scale, int canvasW, int canvasH)
{
	// MP4 に入る実フレーム枠（太枠 + コーナーマーク）。外側は PaintPreview で暗くしている
	{
		CPen penOuter(PS_SOLID, 3, RGB(255, 72, 72));
		CPen* oldP = dc.SelectObject(&penOuter);
		dc.SelectStockObject(NULL_BRUSH);
		CRect fr = imageRect;
		fr.InflateRect(1, 1);
		dc.Rectangle(&fr);
		dc.SelectObject(oldP);

		const int mark = (scale >= 1.f) ? 12 : ((int)(12.f * scale) < 10 ? 10 : (int)(12.f * scale));
		dc.FillSolidRect(imageRect.left, imageRect.top, mark, 3, RGB(255, 210, 60));
		dc.FillSolidRect(imageRect.left, imageRect.top, 3, mark, RGB(255, 210, 60));
		dc.FillSolidRect(imageRect.right - mark, imageRect.top, mark, 3, RGB(255, 210, 60));
		dc.FillSolidRect(imageRect.right - 3, imageRect.top, 3, mark, RGB(255, 210, 60));
		dc.FillSolidRect(imageRect.left, imageRect.bottom - 3, mark, 3, RGB(255, 210, 60));
		dc.FillSolidRect(imageRect.left, imageRect.bottom - mark, 3, mark, RGB(255, 210, 60));
		dc.FillSolidRect(imageRect.right - mark, imageRect.bottom - 3, mark, 3, RGB(255, 210, 60));
		dc.FillSolidRect(imageRect.right - 3, imageRect.bottom - mark, 3, mark, RGB(255, 210, 60));
	}

	int monIdxHud = 0;
	const int mode = ModeComboToSavedMode(m_mode.GetCurSel(), monIdxHud);
	const int sel = m_layer.GetCurSel();
	const int setFps = CurrentPreviewFps();
	const double prevFps = InterlockedCompareExchange(&m_prevFpsX10, 0, 0) / 10.0;
	const double encFps = InterlockedCompareExchange(&m_encFpsX10, 0, 0) / 10.0;
	int fxChain[SC_FX_CHAIN_MAX] = {};
	int fxN = 0;
	GetFxChain(fxChain, &fxN);

	// 上部情報バー（設定FPSと実測プレビュー/エンコードFPS）
	CString hud;
	CString modeName;
	if (mode == SC_MODE_PRIMARY) modeName = L"Primary";
	else if (mode == SC_MODE_VIRTUAL) modeName = L"All monitors";
	else if (mode == SC_MODE_MONITOR) modeName.Format(L"Monitor#%d", monIdxHud + 1);
	else modeName = L"Compose";
	CString fxName = L"";
	if (fxN > 0) {
		fxName = L"  FX:";
		for (int i = 0; i < fxN; ++i) {
			if (i) fxName += L"→";
			fxName += FxName(fxChain[i]);
		}
	}
	if (InterlockedCompareExchange(&m_run, 0, 0)) {
		hud.Format(L"MP4 %dx%d  %s  set %d  Prev %.1f  Enc %.1f  layers %d%s  ●REC %ldf",
			canvasW, canvasH, (LPCTSTR)modeName, setFps, prevFps, encFps, m_layerCnt, (LPCTSTR)fxName,
			(long)InterlockedCompareExchange(&m_frameCnt, 0, 0));
	} else {
		hud.Format(L"MP4 frame  %dx%d  %s  set %d  Prev %.1f fps  layers %d%s",
			canvasW, canvasH, (LPCTSTR)modeName, setFps, prevFps, m_layerCnt, (LPCTSTR)fxName);
	}
	CRect bar = imageRect;
	bar.bottom = bar.top + 18;
	dc.FillSolidRect(&bar, RGB(120, 20, 20));
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(RGB(255, 235, 220));
	CFont* oldFont = dc.SelectObject(GetFont());
	dc.DrawText(hud, &bar, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

	// 右下に「ここがMP4」ラベル
	{
		CString tag = LL14(L"← この赤枠 = MP4", L"← Red frame = MP4", L"← Cadre rouge = MP4", L"← Cornice rossa = MP4",
			L"← Marco rojo = MP4", L"← 빨간 틀 = MP4", L"← 红框 = MP4", L"← الإطار الأحمر = MP4",
			L"← Красная рамка = MP4", L"← Roter Rahmen = MP4", L"← Moldura vermelha = MP4", L"← Rood kader = MP4",
			L"← Czerwona ramka = MP4", L"← Kırmızı çerçeve = MP4");
		CSize ts = dc.GetTextExtent(tag);
		CRect tr(imageRect.right - ts.cx - 8, imageRect.bottom - 18, imageRect.right - 4, imageRect.bottom - 2);
		if (tr.left < imageRect.left + 4) tr.left = imageRect.left + 4;
		dc.FillSolidRect(&tr, RGB(90, 16, 16));
		dc.SetTextColor(RGB(255, 220, 180));
		dc.DrawText(tag, &tr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	}

	CPen penCanvas(PS_SOLID, 1, RGB(80, 90, 110));
	CPen* oldPen = dc.SelectObject(&penCanvas);

	if (mode == SC_MODE_WINDOWS || (m_includeMp.GetCheck() && m_layerCnt > 0)) {
		for (int i = m_layerCnt - 1; i >= 0; --i) {
			if (mode != SC_MODE_WINDOWS && !m_layers[i].isMp) continue;
			const Layer& L = m_layers[i];
			CRect rr = CanvasToPreview(L.x, L.y, L.w, L.h);
			const BOOL selected = (i == sel);
			const BOOL cropped = (L.srcW > 1 && L.srcH > 1);
			const COLORREF col = L.hidden
				? RGB(140, 140, 150)
				: (selected ? RGB(255, 200, 40) : (L.isMp ? RGB(120, 255, 160) : RGB(80, 200, 255)));
			CPen pen(L.hidden ? PS_DOT : PS_SOLID, selected ? 2 : 1, col);
			dc.SelectObject(&pen);
			dc.SelectStockObject(NULL_BRUSH);
			dc.Rectangle(&rr);

			CString label;
			if (cropped)
				label.Format(L"%s%sZ%d %dx%d crop(%d,%d %dx%d)  %s",
					L.hidden ? L"[Hide] " : L"",
					L.isMp ? L"[MP] " : L"", i, L.w, L.h, L.srcX, L.srcY, L.srcW, L.srcH, L.title);
			else
				label.Format(L"%s%sZ%d %dx%d  %s",
					L.hidden ? L"[Hide] " : L"",
					L.isMp ? L"[MP] " : L"", i, L.w, L.h, L.title);
			CRect lr = rr;
			lr.bottom = lr.top + 16;
			if (lr.bottom > rr.bottom) lr.bottom = rr.bottom;
			dc.FillSolidRect(&lr, L.hidden ? RGB(40, 40, 48) : (selected ? RGB(60, 45, 0) : RGB(0, 40, 60)));
			dc.SetTextColor(L.hidden ? RGB(200, 200, 210) : (selected ? RGB(255, 230, 120) : RGB(180, 230, 255)));
			dc.DrawText(label, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

			if (selected) {
				const int hs = 8;
				auto drawHandle = [&](int hx, int hy) {
					CRect hr(hx - hs / 2, hy - hs / 2, hx + hs / 2 + 1, hy + hs / 2 + 1);
					dc.FillSolidRect(&hr, RGB(255, 220, 60));
					dc.Draw3dRect(&hr, RGB(255, 255, 200), RGB(120, 90, 0));
				};
				drawHandle(rr.left, rr.top);
				drawHandle(rr.right, rr.top);
				drawHandle(rr.left, rr.bottom);
				drawHandle(rr.right, rr.bottom);
			}
		}
		if (m_layerCnt <= 0) {
			dc.SetTextColor(RGB(180, 180, 200));
			CRect tip = imageRect;
			tip.DeflateRect(8, 28, 8, 8);
			dc.DrawText(LL14(
				L"ウィンドウを追加し、枠をドラッグ / 四隅で拡大縮小。部分だけ載せるなら Crop sx/sy/sw/sh",
				L"Add windows, then drag / resize by corners. Use Crop sx/sy/sw/sh for a window region",
				L"Ajoutez des fenêtres, puis glissez / redimensionnez. Crop pour une région",
				L"Aggiungi finestre, poi trascina / ridimensiona. Crop per una regione",
				L"Añada ventanas y arrastre / redimensione. Crop para una región",
				L"창을 추가한 뒤 드래그 / 모서리로 크기 조절. 일부만 올릴 땐 Crop",
				L"添加窗口后拖动 / 用四角缩放。局部放入用 Crop",
				L"أضف نوافذ ثم اسحب / غيّر الحجم. Crop لمنطقة",
				L"Добавьте окна, затем перетаскивайте / углы. Crop для области",
				L"Fenster hinzufügen, dann ziehen / Ecken. Crop für Ausschnitt",
				L"Adicione janelas e arraste / redimensione. Crop para região",
				L"Vensters toevoegen, sleep / hoeken. Crop voor regio",
				L"Dodaj okna, przeciągaj / skaluj. Crop dla obszaru",
				L"Pencere ekleyin, sürükleyin / köşeler. Bölge için Crop"),
				&tip, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
		}
	} else {
		dc.SetTextColor(RGB(160, 170, 190));
		CRect tip = imageRect;
		tip.top += 22;
		tip.DeflateRect(6, 0, 6, 6);
		dc.DrawText(LL14(
			L"プレビュー (赤枠内だけがMP4。HUDは録画されません)",
			L"Preview (only inside red frame → MP4. HUD not recorded)",
			L"Aperçu (seul le cadre rouge → MP4. HUD non enregistré)",
			L"Anteprima (solo cornice rossa → MP4. HUD non registrato)",
			L"Vista previa (solo marco rojo → MP4. HUD no se graba)",
			L"미리보기 (빨간 틀 안만 MP4. HUD는 녹화 안 됨)",
			L"预览（红框内才是MP4。HUD不会录制）",
			L"معاينة (داخل الإطار الأحمر فقط → MP4. لا يُسجَّل HUD)",
			L"Превью (только в красной рамке → MP4. HUD не пишется)",
			L"Vorschau (nur im roten Rahmen → MP4. HUD nicht aufgenommen)",
			L"Prévia (só dentro da moldura vermelha → MP4. HUD não é gravado)",
			L"Voorbeeld (alleen in rood kader → MP4. HUD niet opgenomen)",
			L"Podgląd (tylko w czerwonej ramce → MP4. HUD nie jest nagrywany)",
			L"Onizleme (yalnızca kırmızı çerçeve → MP4. HUD kayda girmez)"),
			&tip, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
	}

	dc.SelectObject(oldFont);
	dc.SelectObject(oldPen);
	(void)scale;
}

void CScreenCaptureDlg::PaintPreview(CDC& dc, const CRect& client)
{
	dc.FillSolidRect(&client, RGB(12, 12, 16));
	CRect imageRect;
	float scale = 1.f;
	int cw = 0, ch = 0;
	if (!GetPreviewMap(imageRect, scale, cw, ch)) {
		dc.Draw3dRect(&client, RGB(90, 98, 118), RGB(16, 18, 22));
		return;
	}

	// キャンバス外（MP4に入らない余白）を暗くして、赤枠内が出力だと分かるようにする
	{
		CRgn rClient, rImage, rDim;
		rClient.CreateRectRgnIndirect(&client);
		rImage.CreateRectRgnIndirect(&imageRect);
		rDim.CreateRectRgn(0, 0, 0, 0);
		rDim.CombineRgn(&rClient, &rImage, RGN_DIFF);
		CBrush brush(RGB(8, 8, 10));
		dc.FillRgn(&rDim, &brush);
	}

	if (m_cacheDc && m_cacheBmp && m_cacheW > 0 && m_cacheH > 0) {
		// HALFTONE+長時間 CS 保持は配信スレッドの cache 更新を阻害しプレビューが凍る
		if (m_snapCsInit) EnterCriticalSection(&m_snapCs);
		SetStretchBltMode(dc.GetSafeHdc(), COLORONCOLOR);
		StretchBlt(dc.GetSafeHdc(), imageRect.left, imageRect.top, imageRect.Width(), imageRect.Height(),
			m_cacheDc, 0, 0, m_cacheW, m_cacheH, SRCCOPY);
		if (m_snapCsInit) LeaveCriticalSection(&m_snapCs);
	} else {
		dc.FillSolidRect(&imageRect, RGB(30, 30, 36));
	}
	DrawPreviewHud(dc, imageRect, scale, cw, ch);
	// システム WS_BORDER はプレビュー更新のたびちらつくので自前1px枠
	dc.Draw3dRect(&client, RGB(90, 98, 118), RGB(16, 18, 22));
}

void CScreenCaptureDlg::UpdatePreview(BOOL forceCompose)
{
	(void)forceCompose;
	if (!GetSafeHwnd() || !m_preview.GetSafeHwnd()) return;
	// 録画中は CaptureThread が m_cache を更新するので再合成しない（Enc と奪い合わない）
	const BOOL recording = InterlockedCompareExchange(&m_run, 0, 0) != 0;
	if (!recording)
		RefreshComposeCache();
	m_preview.Invalidate(FALSE);
	// 録画/配信中も UPDATENOW でプレビューを動かさないと「止まった」ように見える
	::RedrawWindow(m_preview.GetSafeHwnd(), NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
}

BOOL CScreenCaptureDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	CCC_BringDialogToForeground(this);
	// 待機中は Win+Shift+S 等でスクショできるよう affinity なし。
	// 録画開始時だけ WDA_EXCLUDEFROMCAPTURE（写り込み防止）。停止で戻す。
	::SetWindowDisplayAffinity(m_hWnd, WDA_NONE);
	// PrintWindow/WGC が他窓を前面化してもコンテキストメニューを即閉じしない
	::SetProp(m_hWnd, CCUSTOM_POPUP_RELAX_DISMISS_PROP, (HANDLE)1);
	if (!m_snapCsInit) {
		InitializeCriticalSection(&m_snapCs);
		m_snapCsInit = TRUE;
	}
	m_preview.SetOwner(this);
	m_fxWire.SetOwner(this);
	// プレビュー/配線は不透明オーナー描画（アクリル透過に乗せない）
	m_preview.SetAeroMode(FALSE);
	m_fxWire.SetAeroMode(FALSE);
	// アクリル下の WS_BORDER 欠け/ちらつき回避（枠は各 Paint で自前描画）
	if (m_preview.GetSafeHwnd())
		m_preview.ModifyStyle(WS_BORDER, 0, SWP_FRAMECHANGED);
	if (m_fxWire.GetSafeHwnd())
		m_fxWire.ModifyStyle(WS_BORDER, 0, SWP_FRAMECHANGED);
	m_help.SetWindowText(L"?");
	m_help.SetFlat(TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	LayoutHelpBtn();
	m_audio.SetAeroMode(FALSE);
	m_mic.SetAeroMode(FALSE);
	m_includeMp.SetAeroMode(FALSE);
	m_showCursor.SetAeroMode(FALSE);
	m_live.SetAeroMode(FALSE);
	if (m_liveCfg.GetSafeHwnd()) m_liveCfg.SetAeroMode(FALSE);
	m_fps.SetAeroMode(FALSE);
	m_effect.SetAeroMode(FALSE);
	m_fxPre.SetAeroMode(FALSE);
	m_mode.SetAeroMode(FALSE);
	m_canvas.SetAeroMode(FALSE);
	m_avail.SetAeroMode(FALSE);
	m_layer.SetAeroMode(FALSE);

	SetWindowText(LL14(
		L"画面キャプチャ", L"Screen capture", L"Capture d'écran", L"Cattura schermo",
		L"Captura de pantalla", L"화면 캡처", L"屏幕捕获", L"التقاط الشاشة",
		L"Захват экрана", L"Bildschirmaufnahme", L"Captura de ecrã", L"Schermopname",
		L"Przechwytywanie ekranu", L"Ekran yakalama"));

	m_modeLabel.SetWindowText(LL14(L"モード", L"Mode", L"Mode", L"Modalità", L"Modo", L"모드", L"模式", L"الوضع",
		L"Режим", L"Modus", L"Modo", L"Modus", L"Tryb", L"Mod"));
	m_canvasLabel.SetWindowText(LL14(L"解像度", L"Resolution", L"Résolution", L"Risoluzione", L"Resolución", L"해상도", L"分辨率", L"الدقة",
		L"Разрешение", L"Auflösung", L"Resolução", L"Resolutie", L"Rozdzielczość", L"Çözünürlük"));
	m_fpsLabel.SetWindowText(L"FPS");
	m_effectLabel.SetWindowText(LL14(L"効果", L"Effect", L"Effet", L"Effetto", L"Efecto", L"효과", L"效果", L"تأثير",
		L"Эффект", L"Effekt", L"Efeito", L"Effect", L"Efekt", L"Efekt"));
	m_fxPreLabel.SetWindowText(LL14(L"配線プリセット", L"Wiring preset", L"Préréglage câblage", L"Preset cablaggio",
		L"Ajuste cableado", L"배선 프리셋", L"连线预设", L"إعداد التوصيل",
		L"Пресет схемы", L"Verdrahtungs-Preset", L"Predefinição ligação", L"Bedradingspreset",
		L"Preset okablowania", L"Kablolama önayarı"));
	m_fxPreLoad.SetWindowText(LL14(L"読込", L"Load", L"Charger", L"Carica", L"Cargar", L"불러오기", L"读取", L"تحميل",
		L"Загрузить", L"Laden", L"Carregar", L"Laden", L"Wczytaj", L"Yükle"));
	m_fxPreSave.SetWindowText(LL14(L"保存", L"Save", L"Enreg.", L"Salva", L"Guardar", L"저장", L"保存", L"حفظ",
		L"Сохранить", L"Speichern", L"Guardar", L"Opslaan", L"Zapisz", L"Kaydet"));
	m_cropLabel.SetWindowText(LL14(L"切出 sx sy sw sh", L"Crop sx sy sw sh", L"Rogner sx sy sw sh", L"Ritaglio sx sy sw sh",
		L"Recorte sx sy sw sh", L"잘라내기 sx sy sw sh", L"裁剪 sx sy sw sh", L"قص sx sy sw sh",
		L"Вырез sx sy sw sh", L"Ausschnitt sx sy sw sh", L"Recorte sx sy sw sh", L"Uitsnede sx sy sw sh",
		L"Wycinek sx sy sw sh", L"Kırp sx sy sw sh"));
	m_cropFull.SetWindowText(LL14(L"切出解除", L"Full", L"Plein", L"Intero", L"Completo", L"전체", L"整窗", L"كامل",
		L"Весь", L"Ganz", L"Inteiro", L"Volledig", L"Całość", L"Tam"));
	m_pathLabel.SetWindowText(LL14(L"保存先", L"Save path", L"Chemin", L"Percorso", L"Ruta", L"저장 위치", L"保存路径", L"المسار",
		L"Путь", L"Pfad", L"Caminho", L"Pad", L"Ścieżka", L"Yol"));
	m_audio.SetWindowText(LL14(L"システム音", L"System audio", L"Son système", L"Audio sistema", L"Audio sistema", L"시스템 소리", L"系统声音", L"صوت النظام",
		L"Сист. звук", L"Systemton", L"Áudio sistema", L"Systeemaudio", L"Dźwięk systemu", L"Sistem sesi"));
	m_mic.SetWindowText(LL14(L"マイク", L"Mic", L"Micro", L"Microfono", L"Micrófono", L"마이크", L"麦克风", L"ميكروفون",
		L"Микрофон", L"Mikrofon", L"Microfone", L"Microfoon", L"Mikrofon", L"Mikrofon"));
	m_includeMp.SetWindowText(LL14(
		L"MPを載せる", L"Include MP", L"Inclure MP", L"Includi MP",
		L"Incluir MP", L"MP 포함", L"放入MP", L"تضمين MP",
		L"Включить MP", L"MP einbeziehen", L"Incluir MP", L"MP opnemen",
		L"Dołącz MP", L"MP ekle"));
	m_showCursor.SetWindowText(LL14(
		L"マウスカーソルを載せる", L"Include mouse cursor", L"Inclure le curseur", L"Includi cursore",
		L"Incluir cursor", L"마우스 커서 포함", L"包含鼠标光标", L"تضمين مؤشر الفأرة",
		L"Включить курсор", L"Mauszeiger einbeziehen", L"Incluir cursor", L"Muiscursor opnemen",
		L"Dołącz kursor myszy", L"Fare imlecini ekle"));
	m_availLabel.SetWindowText(LL14(L"ウィンドウ一覧", L"Windows", L"Fenêtres", L"Finestre", L"Ventanas", L"창 목록", L"窗口列表", L"النوافذ",
		L"Окна", L"Fenster", L"Janelas", L"Vensters", L"Okna", L"Pencereler"));
	m_layerLabel.SetWindowText(LL14(L"合成レイヤ (上が手前)", L"Layers (top = front)", L"Calques (haut = avant)", L"Livelli (alto = davanti)",
		L"Capas (arriba = frente)", L"합성 레이어 (위=앞)", L"合成层(上=前)", L"الطبقات (الأعلى=أمام)",
		L"Слои (верх = перед)", L"Ebenen (oben = vorne)", L"Camadas (cima = frente)", L"Lagen (boven = voor)",
		L"Warstwy (góra = przód)", L"Katmanlar (üst = ön)"));
	m_geoLabel.SetWindowText(LL14(L"配置 X Y W H", L"Layout X Y W H", L"Disposition X Y W H", L"Layout X Y W H",
		L"Diseño X Y W H", L"배치 X Y W H", L"布局 X Y W H", L"التخطيط X Y W H",
		L"Раскладка X Y W H", L"Layout X Y W H", L"Layout X Y W H", L"Layout X Y W H",
		L"Układ X Y W H", L"Yerleşim X Y W H"));
	m_pick.SetWindowText(LL14(L"画面で選択", L"Pick on screen", L"Choisir à l'écran", L"Scegli sullo schermo",
		L"Elegir en pantalla", L"화면에서 선택", L"在屏幕上选择", L"اختر على الشاشة",
		L"Выбрать на экране", L"Auf Bildschirm wählen", L"Escolher no ecrã", L"Kies op scherm",
		L"Wybierz na ekranie", L"Ekranda seç"));
	m_refresh.SetWindowText(LL14(L"更新", L"Refresh", L"Actualiser", L"Aggiorna", L"Actualizar", L"새로고침", L"刷新", L"تحديث",
		L"Обновить", L"Aktualisieren", L"Atualizar", L"Vernieuwen", L"Odśwież", L"Yenile"));
	m_add.SetWindowText(LL14(L"追加 >>", L"Add >>", L"Ajouter >>", L"Aggiungi >>", L"Añadir >>", L"추가 >>", L"添加 >>", L"إضافة >>",
		L"Добавить >>", L"Hinzufügen >>", L"Adicionar >>", L"Toevoegen >>", L"Dodaj >>", L"Ekle >>"));
	m_remove.SetWindowText(LL14(L"<< 削除", L"<< Remove", L"<< Retirer", L"<< Rimuovi", L"<< Quitar", L"<< 삭제", L"<< 删除", L"<< إزالة",
		L"<< Удалить", L"<< Entfernen", L"<< Remover", L"<< Verwijderen", L"<< Usuń", L"<< Kaldır"));
	m_applyGeo.SetWindowText(LL14(L"適用", L"Apply", L"Appliquer", L"Applica", L"Aplicar", L"적용", L"应用", L"تطبيق",
		L"Применить", L"Übernehmen", L"Aplicar", L"Toepassen", L"Zastosuj", L"Uygula"));
	m_fit.SetWindowText(LL14(L"全面", L"Fit", L"Ajuster", L"Adatta", L"Ajustar", L"맞춤", L"铺满", L"ملاءمة",
		L"Вписать", L"Einpassen", L"Ajustar", L"Passen", L"Dopasuj", L"Sığdır"));
	m_tile.SetWindowText(LL14(L"整列", L"Tile", L"Mosaïque", L"Affianca", L"Mosaico", L"정렬", L"平铺", L"تجانب",
		L"Плитка", L"Kacheln", L"Mosaico", L"Tegelen", L"Kafelki", L"Döşe"));
	m_start.SetWindowText(LL14(L"録画開始", L"Start", L"Démarrer", L"Avvia", L"Iniciar", L"시작", L"开始", L"بدء",
		L"Старт", L"Start", L"Iniciar", L"Start", L"Start", L"Başlat"));
	m_close.SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
		L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	m_status.SetWindowText(LL14(
		L"赤枠内がMP4。下の配線で効果を最大8段つなぎ可。スロット右クリックで強度。配線プリセットで保存/読込。Cropでウィンドウ一部。右クリックHideで映像オフ(音可)。HUDは録画されません。",
		L"Red frame = MP4. Wire up to 8 FX below. Right-click slot for strength. Save/load wiring presets. Crop for a window region. Right-click Hide (audio OK). HUD not recorded.",
		L"Cadre rouge = MP4. Chaînez jusqu'à 8 FX. Clic droit slot = intensité. Préréglages câblage. Crop pour une région. Clic droit Hide. HUD non enregistré.",
		L"Cornice rossa = MP4. Collega fino a 8 FX. Destro sullo slot = intensità. Preset cablaggio. Crop per regione. Destro Hide. HUD non registrato.",
		L"Marco rojo = MP4. Encadene hasta 8 FX. Clic der. en ranura = intensidad. Ajustes de cableado. Crop para región. Clic der. Hide. HUD no se graba.",
		L"빨간 틀 = MP4. 아래에서 FX 최대 8단 연결. 슬롯 우클릭으로 강도. 배선 프리셋 저장/불러오기. Crop으로 창 일부. 우클릭 Hide. HUD 미녹화.",
		L"红框=MP4。下方可串联最多8段效果。右键插槽调强度。连线预设可保存/读取。Crop放局部。右键Hide。HUD不录。",
		L"الإطار الأحمر = MP4. وصّل حتى 8 FX. يمين على الفتحة للشدة. إعدادات التوصيل. Crop لمنطقة. يمين Hide. لا يُسجَّل HUD.",
		L"Красная рамка = MP4. До 8 FX в цепочке. ПКМ по слоту — сила. Пресеты схемы. Crop для области. ПКМ Hide. HUD не пишется.",
		L"Roter Rahmen = MP4. Bis 8 FX verketten. Rechtsklick Slot = Stärke. Verdrahtungs-Presets. Crop für Ausschnitt. Rechtsklick Hide. HUD nicht aufgenommen.",
		L"Moldura vermelha = MP4. Encadeie até 8 FX. Direito no slot = intensidade. Predefinições de ligação. Crop para região. Direito Hide. HUD não é gravado.",
		L"Rood kader = MP4. Koppel tot 8 FX. Rechtsklik slot = sterkte. Bedradingspresets. Crop voor regio. Rechtsklik Hide. HUD niet opgenomen.",
		L"Czerwona ramka = MP4. Połącz do 8 FX. PPM na slocie = siła. Presety okablowania. Crop dla obszaru. PPM Hide. HUD nie jest nagrywany.",
		L"Kırmızı çerçeve = MP4. En fazla 8 FX bağlayın. Slota sağ tık = yoğunluk. Kablolama önayarları. Crop ile bölge. Sağ tık Hide. HUD kayda girmez."));

	RefreshModeCombo();

	m_canvas.AddString(LL14(L"自動 (ネイティブ)", L"Auto (native)", L"Auto (natif)", L"Auto (nativo)",
		L"Auto (nativo)", L"자동 (네이티브)", L"自动（原生）", L"تلقائي (أصلي)",
		L"Авто (натив)", L"Auto (nativ)", L"Auto (nativo)", L"Auto (native)",
		L"Auto (natywne)", L"Otomatik (yerel)"));
	m_canvas.AddString(L"1280 x 720");
	m_canvas.AddString(L"1920 x 1080");
	m_canvas.AddString(L"1600 x 900");
	m_canvas.AddString(L"3840 x 2160");
	int canvas = savedata.cap_canvas_preset;
	if (canvas < 0 || canvas > 4) canvas = 2;
	m_canvas.SetCurSel(canvas);

	m_audio.SetCheck(savedata.cap_with_audio ? BST_CHECKED : BST_UNCHECKED);
	m_mic.SetCheck(savedata.cap_with_mic ? BST_CHECKED : BST_UNCHECKED);
	AudioMicDevFillCombo(m_micDev);
	AudioLoopDevFillCombo(m_loopDev);
	m_micDevLabel.SetWindowText(LL14(L"マイク", L"Mic", L"Micro", L"Micro", L"Micro", L"마이크", L"麦克风", L"ميكروفون", L"Микрофон", L"Mikrofon", L"Microfone", L"Microfoon", L"Mikrofon", L"Mikrofon"));
	m_loopDevLabel.SetWindowText(LL14(L"システム", L"System", L"Système", L"Sistema", L"Sistema", L"시스템", L"系统", L"النظام", L"Система", L"System", L"Sistema", L"Systeem", L"System", L"Sistem"));
	m_includeMp.SetCheck(savedata.cap_include_mp ? BST_CHECKED : BST_UNCHECKED);
	m_showCursor.SetCheck(savedata.cap_show_cursor ? BST_CHECKED : BST_UNCHECKED);

	ApplyLiveFieldsToUi();
	SyncLiveUiEnable();
	if (savedata.cap_live_mode) {
		EnsureFfmpegAvailable(TRUE);
		OpenScLiveSettingsModeless(this);
	}

	static const int fpsTab[] = { 10, 15, 20, 24, 30, 60, 90, 120 };
	int fpsSel = 1;
	for (int i = 0; i < (int)_countof(fpsTab); ++i) {
		CString s; s.Format(L"%d", fpsTab[i]);
		m_fps.AddString(s);
		if (savedata.cap_fps == fpsTab[i]) fpsSel = i;
	}
	m_fps.SetCurSel(fpsSel);

	m_effect.ResetContent();
	for (int fx = 0; fx < SC_FX_COUNT; ++fx)
		m_effect.AddString(FxName(fx));
	m_effect.AddString(LL14(
		L"(カスタム配線)", L"(Custom wiring)", L"(Câblage perso.)", L"(Cablaggio pers.)",
		L"(Cableado pers.)", L"(사용자 배선)", L"(自定义连线)", L"(توصيل مخصص)",
		L"(Своя схема)", L"(Eigene Verdrahtung)", L"(Ligação pers.)", L"(Eigen bedrading)",
		L"(Własne okablowanie)", L"(Özel kablolama)"));
	{
		int chain[SC_FX_CHAIN_MAX] = {};
		BYTE str[SC_FX_CHAIN_MAX][SC_FX_STR_N];
		memset(str, SC_FX_STR_DEF, sizeof(str));
		int cn = savedata.cap_fx_n;
		if (cn < 0) cn = 0;
		if (cn > SC_FX_CHAIN_MAX) cn = SC_FX_CHAIN_MAX;
		const int src[SC_FX_CHAIN_MAX] = {
			savedata.cap_fx0, savedata.cap_fx1, savedata.cap_fx2, savedata.cap_fx3,
			savedata.cap_fx4, savedata.cap_fx5, savedata.cap_fx6, savedata.cap_fx7
		};
		int n = 0;
		for (int i = 0; i < cn; ++i) {
			if (src[i] > SC_FX_NONE && src[i] < SC_FX_COUNT) {
				chain[n] = src[i];
				memcpy(str[n], savedata.cap_fx_str[i], SC_FX_STR_N);
				n++;
			}
		}
		if (n <= 0 && savedata.cap_effect > 0 && savedata.cap_effect < SC_FX_COUNT) {
			chain[0] = savedata.cap_effect;
			n = 1;
		}
		m_fxWire.SetChain(chain, n, str);
		SyncFxComboFromChain();
	}
	FillFxPresetCombo();

	// 日付ファイル名は毎回更新（フォルダだけ前回を引き継ぐ）
	m_path.SetWindowText(NormalizeOutPath(RefreshCaptureOutPathTimestamp(savedata.cap_last_path)));

	RefreshAvailList();
	EnableComposeUi(IsWindowComposeMode());
	if (savedata.cap_include_mp)
		SyncMpLayerFromCheck();

	if (CCustomControlUtility::BeginDialogToolTip(m_tooltip, this)) {
		m_tooltip.AddTool(&m_mode, LL14(
			L"画面全体または複数ウィンドウの合成を選びます",
			L"Full screen or multi-window composition",
			L"Plein écran ou composition multi-fenêtres",
			L"Schermo intero o composizione multi-finestra",
			L"Pantalla completa o composición multi-ventana",
			L"전체 화면 또는 다중 창 합성",
			L"全屏或多窗口合成",
			L"شاشة كاملة أو تركيب نوافذ",
			L"Полный экран или композиция окон",
			L"Vollbild oder Mehrfenster-Komposition",
			L"Ecrã inteiro ou composição multi-janela",
			L"Volledig scherm of multi-venstercompositie",
			L"Pełny ekran lub kompozycja wielu okien",
			L"Tam ekran veya çoklu pencere kompozisyonu"));
		m_tooltip.AddTool(&m_canvas, LL14(
			L"出力解像度。自動は合成内容に合わせます",
			L"Output resolution. Auto follows composition",
			L"Résolution de sortie. Auto suit la composition",
			L"Risoluzione di uscita. Auto segue la composizione",
			L"Resolución de salida. Auto sigue la composición",
			L"출력 해상도. 자동은 합성에 맞춤",
			L"输出分辨率。自动随合成内容",
			L"دقة الإخراج. تلقائي يتبع التركيب",
			L"Разрешение выхода. Авто по композиции",
			L"Ausgabeauflösung. Auto folgt der Komposition",
			L"Resolução de saída. Auto segue a composição",
			L"Uitvoerresolutie. Auto volgt compositie",
			L"Rozdzielczość wyjścia. Auto wg kompozycji",
			L"Çıkış çözünürlüğü. Otomatik kompozisyona uyar"));
		m_tooltip.AddTool(&m_fps, LL14(
			L"録画＆プレビューFPS。90/120は限界計測用(PC次第で頭打ち)。高いほど負荷・画質ビットレート増",
			L"Record & preview FPS. 90/120 for limit testing (may cap). Higher = heavier, higher bitrate",
			L"FPS enregistrement/aperçu. 90/120 pour tester la limite (selon le PC)",
			L"FPS registrazione/anteprima. 90/120 per test limite (dipende dal PC)",
			L"FPS grabación/vista previa. 90/120 para probar límite (según PC)",
			L"녹화·미리보기 FPS. 90/120은 한계 측정용(PC에 따라 제한)",
			L"录制与预览FPS。90/120用于测极限（视电脑而定）",
			L"FPS التسجيل والمعاينة. 90/120 لاختبار الحد (حسب الجهاز)",
			L"FPS записи и превью. 90/120 для проверки предела (зависит от ПК)",
			L"Aufnahme-/Vorschau-FPS. 90/120 zum Limit-Test (PC-abhängig)",
			L"FPS gravação/prévia. 90/120 para testar limite (depende do PC)",
			L"Opname-/voorbeeld-FPS. 90/120 om limiet te testen (afhankelijk van PC)",
			L"FPS nagrywania/podglądu. 90/120 do testu limitu (zależnie od PC)",
			L"Kayıt/önizleme FPS. 90/120 limit testi için (PC’ye bağlı)"));
		m_tooltip.AddTool(&m_effect, LL14(
			L"クイック1段効果。複数つなぐ場合は下の配線パレットからドラッグ",
			L"Quick single FX. For a chain, drag from the wiring palette below",
			L"Effet simple. Pour une chaîne, glissez depuis la palette ci-dessous",
			L"Effetto singolo. Per una catena, trascina dalla palette sotto",
			L"Efecto simple. Para una cadena, arrastre desde la paleta abajo",
			L"빠른 1단 효과. 여러 개를 이으려면 아래 배선 팔레트에서 드래그",
			L"快捷单段效果。多段请从下方连线面板拖入",
			L"تأثير واحد سريع. للسلسلة اسحب من لوحة التوصيل أدناه",
			L"Быстрый один эффект. Для цепочки перетащите с панели ниже",
			L"Schneller Einzeleffekt. Für Kette von der Palette unten ziehen",
			L"Efeito único rápido. Para cadeia, arraste da paleta abaixo",
			L"Snelle enkele FX. Voor een keten sleep vanaf het palet hieronder",
			L"Szybki jeden efekt. Łańcuch: przeciągnij z palety poniżej",
			L"Hızlı tek efekt. Zincir için alttaki paletten sürükleyin"));
		m_tooltip.AddTool(&m_fxWire, LL14(
			L"パレットからスロットへドラッグで最大8段まで直列接続。同じ効果の並列配置可。右クリックで解除/複製/強度",
			L"Drag palette chips onto slots (max 8 in series; duplicates OK). Right-click clears/duplicates/strength",
			L"Glissez les puces vers les slots (max 8 en série; doublons OK). Clic droit efface/duplique/intensité",
			L"Trascina i chip sugli slot (max 8 in serie; duplicati OK). Destro azzera/duplica/intensità",
			L"Arrastre chips a las ranuras (máx. 8 en serie; duplicados OK). Clic der. borra/duplica/intensidad",
			L"팔레트 칩을 슬롯에 드래그(최대 8단 직렬·중복 가능). 우클릭으로 해제/복제/강도",
			L"将色块拖到插槽（最多串联8段，可重复）。右键清除/复制/强度",
			L"اسحب الشرائح إلى الفتحات (حتى 8 متسلسلة؛ التكرار مسموح). يمين للمسح/التكرار/الشدة",
			L"Перетащите чипы на слоты (макс. 8 подряд; дубликаты OK). ПКМ очищает/дублирует/сила",
			L"Chips auf Slots ziehen (max. 8 in Reihe; Duplikate OK). Rechtsklick löscht/dupliziert/Stärke",
			L"Arraste chips para slots (máx. 8 em série; duplicatas OK). Direito limpa/duplica/intensidade",
			L"Sleep chips naar slots (max 8 in serie; duplicaten OK). Rechtsklik wist/dupliceert/sterkte",
			L"Przeciągnij chipy na sloty (maks. 8 w szeregu; duplikaty OK). PPM czyści/duplikuje/siła",
			L"Chip’leri slotlara sürükleyin (en fazla 8 seri; tekrar OK). Sağ tık siler/çoğaltır/yoğunluk"));
		m_tooltip.AddTool(&m_fxPre, LL14(
			L"配線プリセット16枠。名前を編集して保存／読込",
			L"16 wiring presets. Edit name, then Save / Load",
			L"16 préréglages. Modifier le nom, puis Enreg. / Charger",
			L"16 preset. Modifica nome, poi Salva / Carica",
			L"16 ajustes. Edite el nombre y Guardar / Cargar",
			L"배선 프리셋 16칸. 이름 편집 후 저장/불러오기",
			L"16个连线预设。编辑名称后保存/读取",
			L"16 إعدادات توصيل. عدّل الاسم ثم حفظ/تحميل",
			L"16 пресетов схемы. Измените имя, затем Сохранить / Загрузить",
			L"16 Verdrahtungs-Presets. Name ändern, dann Speichern / Laden",
			L"16 predefinições. Edite o nome e Guarde / Carregue",
			L"16 bedradingspresets. Bewerk naam, dan Opslaan / Laden",
			L"16 presetów okablowania. Edytuj nazwę, potem Zapisz / Wczytaj",
			L"16 kablolama önayarı. Adı düzenleyip Kaydet / Yükle"));
		m_tooltip.AddTool(&m_cropFull, LL14(
			L"選択レイヤのウィンドウ内切り出しを解除（全体を載せる）",
			L"Clear crop on selected layer (use full window)",
			L"Efface le rognage de la couche (fenêtre entière)",
			L"Azzera il ritaglio del livello (finestra intera)",
			L"Quita el recorte de la capa (ventana completa)",
			L"선택 레이어 잘라내기 해제(창 전체)",
			L"清除所选层裁剪（整窗）",
			L"إلغاء القص للطبقة (النافذة كاملة)",
			L"Сбросить вырез слоя (всё окно)",
			L"Ausschnitt der Ebene aufheben (ganzes Fenster)",
			L"Limpa o recorte da camada (janela inteira)",
			L"Wis uitsnede van laag (heel venster)",
			L"Wyczyść wycinek warstwy (całe okno)",
			L"Katman kırpmasını kaldır (tüm pencere)"));
		m_tooltip.AddTool(&m_audio, LL14(
			L"PCの再生音(ループバック)を録画に載せます。MPの曲もここに含まれます",
			L"Include PC playback (loopback). MP audio is included here",
			L"Inclut la lecture PC (loopback). L'audio MP est inclus",
			L"Include audio PC (loopback). L'audio MP è incluso",
			L"Incluye audio del PC (loopback). El audio MP se incluye",
			L"PC 재생음(루프백)을 녹화에 포함. MP 곡도 여기 포함",
			L"录制PC播放音(环回)。MP曲目也在此",
			L"يشمل صوت التشغيل (loopback). صوت MP هنا أيضاً",
			L"Звук ПК (loopback). Трек MP тоже здесь",
			L"PC-Wiedergabe (Loopback). MP-Audio ist enthalten",
			L"Inclui áudio do PC (loopback). Áudio MP incluído",
			L"PC-weergave (loopback). MP-audio zit hierin",
			L"Dźwięk PC (loopback). Audio MP też tutaj",
			L"PC oynatma sesi (loopback). MP sesi de burada"));
		m_tooltip.AddTool(&m_mic, LL14(
			L"マイク入力を録画にミックスします",
			L"Mix microphone into the recording",
			L"Mixer le micro dans l'enregistrement",
			L"Mescola il microfono nella registrazione",
			L"Mezclar el micrófono en la grabación",
			L"마이크 입력을 녹화에 믹스",
			L"将麦克风混入录制",
			L"مزج الميكروفون في التسجيل",
			L"Микрофон в запись",
			L"Mikrofon in die Aufnahme mischen",
			L"Misturar microfone na gravação",
			L"Microfoon in de opname mixen",
			L"Mikrofon do nagrania",
			L"Mikrofonu kayda karıştır"));
		m_tooltip.AddTool(&m_includeMp, LL14(
			L"MP画面を合成に追加。音だけならプレビューで右クリック→Hide(チェックはONのまま)",
			L"Add MP window. For audio-only: right-click preview → Hide (keep checked)",
			L"Ajoute MP. Audio seul: clic droit aperçu → Hide (rester coché)",
			L"Aggiunge MP. Solo audio: destro anteprima → Hide (lascia selezionato)",
			L"Añade MP. Solo audio: clic der. vista → Hide (dejar marcado)",
			L"MP 창 추가. 오디오만: 미리보기 우클릭→Hide(체크 유지)",
			L"加入MP画面。只要声音：预览右键→Hide（保持勾选）",
			L"يضيف MP. للصوت فقط: يمين المعاينة → Hide (اتركه محدداً)",
			L"Добавляет окно MP. Только звук: ПКМ в превью → Hide (оставьте галочку)",
			L"MP-Fenster hinzufügen. Nur Audio: Rechtsklick Vorschau → Hide (anlassen)",
			L"Adiciona MP. Só áudio: direito na prévia → Hide (mantenha marcado)",
			L"Voegt MP toe. Alleen audio: rechtsklik voorbeeld → Hide (aan laten)",
			L"Dodaje MP. Tylko dźwięk: PPM podgląd → Hide (zostaw zaznaczone)",
			L"MP penceresi ekler. Yalnızca ses: sağ tık önizleme → Hide (işaretli kalsın)"));
		m_tooltip.AddTool(&m_showCursor, LL14(
			L"録画・プレビューにマウスカーソルを載せます（オフなら非表示）",
			L"Include the mouse cursor in preview/recording (off = hidden)",
			L"Inclut le curseur dans l'aperçu/l'enregistrement (off = masqué)",
			L"Include il cursore in anteprima/registrazione (off = nascosto)",
			L"Incluye el cursor en vista previa/grabación (off = oculto)",
			L"미리보기/녹화에 마우스 커서를 포함 (끄면 숨김)",
			L"在预览/录制中包含鼠标光标（关闭则隐藏）",
			L"يضمّن مؤشر الفأرة في المعاينة/التسجيل (إيقاف=إخفاء)",
			L"Показывать курсор в превью/записи (выкл = скрыт)",
			L"Mauszeiger in Vorschau/Aufnahme (aus = verborgen)",
			L"Inclui o cursor na prévia/gravação (off = oculto)",
			L"Muiscursor in voorbeeld/opname (uit = verborgen)",
			L"Kursor myszy w podglądzie/nagraniu (wył. = ukryty)",
			L"Önizleme/kayıtta fare imleci (kapalı = gizli)"));
		m_tooltip.AddTool(&m_live, LL14(
			L"ONでRTMPライブ配信（MP4なし）。ffmpeg.exe が必要。設定は専用窓で行います。",
			L"ON = RTMP live (no MP4). Requires ffmpeg.exe. Configure in the settings window.",
			L"ON = live RTMP (pas de MP4). ffmpeg.exe requis. Réglages dans la fenêtre dédiée.",
			L"ON = live RTMP (niente MP4). Serve ffmpeg.exe. Impostazioni nella finestra dedicata.",
			L"ON = vivo RTMP (sin MP4). Requiere ffmpeg.exe. Ajustes en la ventana dedicada.",
			L"ON이면 RTMP 라이브(MP4 없음). ffmpeg.exe 필요. 설정은 전용 창에서.",
			L"开启后 RTMP 直播（不写 MP4）。需要 ffmpeg.exe。在专用窗口中设置。",
			L"تشغيل = بث RTMP (بدون MP4). يلزم ffmpeg.exe. الإعدادات في النافذة المخصصة.",
			L"Вкл. = RTMP эфир (без MP4). Нужен ffmpeg.exe. Настройки в отдельном окне.",
			L"AN = RTMP-Live (kein MP4). ffmpeg.exe nötig. Einstellungen im eigenen Fenster.",
			L"ON = ao vivo RTMP (sem MP4). Requer ffmpeg.exe. Definições na janela dedicada.",
			L"AAN = RTMP-live (geen MP4). ffmpeg.exe vereist. Instellingen in apart venster.",
			L"WŁ = live RTMP (bez MP4). Wymaga ffmpeg.exe. Ustawienia w osobnym oknie.",
			L"AÇIK = RTMP canlı (MP4 yok). ffmpeg.exe gerekir. Ayarlar ayrı pencerede."));
		if (m_liveCfg.GetSafeHwnd()) {
			m_tooltip.AddTool(&m_liveCfg, LL14(
				L"ライブ配信の設定窓を開く（配信先・URL・認証など）",
				L"Open live stream settings (service, URL, auth, …)",
				L"Ouvrir les réglages live (service, URL, auth…)",
				L"Apri impostazioni diretta (servizio, URL, auth…)",
				L"Abrir ajustes de transmisión (servicio, URL, auth…)",
				L"라이브 설정 창 열기(서비스·URL·인증 등)",
				L"打开直播设置窗口（服务、URL、认证等）",
				L"فتح نافذة إعدادات البث (الخدمة، الرابط، المصادقة…)",
				L"Открыть настройки эфира (сервис, URL, вход…)",
				L"Livestream-Einstellungen öffnen (Dienst, URL, Auth…)",
				L"Abrir definições de transmissão (serviço, URL, auth…)",
				L"Livestream-instellingen openen (dienst, URL, auth…)",
				L"Otwórz ustawienia transmisji (usługa, URL, auth…)",
				L"Canlı yayın ayarlarını aç (servis, URL, auth…)"));
		}
		m_tooltip.AddTool(&m_pick, LL14(
			L"次にクリックしたウィンドウをレイヤに追加します",
			L"Next click adds that window as a layer",
			L"Le prochain clic ajoute la fenêtre",
			L"Il prossimo clic aggiunge la finestra",
			L"El siguiente clic añade la ventana",
			L"다음에 클릭한 창을 레이어에 추가",
			L"下一次点击的窗口将加入层",
			L"النقرة التالية تضيف النافذة",
			L"Следующий клик добавит окно",
			L"Nächster Klick fügt das Fenster hinzu",
			L"O próximo clique adiciona a janela",
			L"Volgende klik voegt het venster toe",
			L"Następne kliknięcie doda okno",
			L"Sonraki tıklama pencereyi ekler"));
		m_tooltip.AddTool(&m_refresh, LL14(
			L"ウィンドウ一覧を更新し、プレビューを描き直します",
			L"Refresh window list and redraw preview",
			L"Actualiser la liste et redessiner l'aperçu",
			L"Aggiorna elenco e ridisegna anteprima",
			L"Actualizar lista y redibujar vista previa",
			L"창 목록을 갱신하고 미리보기를 다시 그립니다",
			L"刷新窗口列表并重绘预览",
			L"تحديث قائمة النوافذ وإعادة رسم المعاينة",
			L"Обновить список окон и превью",
			L"Fensterliste aktualisieren und Vorschau neu zeichnen",
			L"Atualizar lista e redesenhar prévia",
			L"Vensterlijst vernieuwen en voorbeeld hertekenen",
			L"Odśwież listę okien i podgląd",
			L"Pencere listesini yenile ve önizlemeyi çiz"));
		m_tooltip.AddTool(&m_preview, LL14(
			L"合成プレビュー。左ドラッグで移動/四隅リサイズ。右クリックでHide/Show(全ウィンドウ)",
			L"Compose preview. L-drag move/resize. R-click Hide/Show (any window)",
			L"Aperçu. Glisser G pour bouger/taille. Clic D Hide/Show",
			L"Anteprima. Trascina S per muovere/ridimensionare. Destro Hide/Show",
			L"Vista previa. Arrastre izq. mover/tamaño. Der. Hide/Show",
			L"합성 미리보기. 좌드래그 이동/크기. 우클릭 Hide/Show",
			L"合成预览。左键拖动移动/缩放。右键Hide/Show",
			L"معاينة التركيب. سحب أيسر للنقل/الحجم. يمين Hide/Show",
			L"Превью. ЛКМ — двигать/размер. ПКМ Hide/Show",
			L"Vorschau. L-Ziehen verschieben/skalieren. R-Klick Hide/Show",
			L"Prévia. Arraste esq. mover/tamanho. Dir. Hide/Show",
			L"Voorbeeld. L-slepen verplaatsen/schalen. R-klik Hide/Show",
			L"Podgląd. LPM przeciągaj/skaluj. PPM Hide/Show",
			L"Önizleme. Sol sürükle taşı/ölçek. Sağ tık Hide/Show"));
		m_tooltip.AddTool(&m_avail, LL14(
			L"キャプチャ可能なトップレベルウィンドウ一覧",
			L"List of capturable top-level windows",
			L"Liste des fenêtres capturables",
			L"Elenco finestre catturabili",
			L"Lista de ventanas capturables",
			L"캡처 가능한 최상위 창 목록",
			L"可捕获的顶级窗口列表",
			L"قائمة النوافذ القابلة للالتقاط",
			L"Список захватываемых окон",
			L"Liste erfassbarer Fenster",
			L"Lista de janelas capturáveis",
			L"Lijst van te vangen vensters",
			L"Lista okien do przechwycenia",
			L"Yakalanabilir üst düzey pencereler"));
		m_tooltip.AddTool(&m_layer, LL14(
			L"合成レイヤ。上が手前。[Hide]は映像から除外(音はシステム音側)",
			L"Layers; top is front. [Hide] excludes video (audio via system sound)",
			L"Calques; haut = avant. [Hide] exclut la vidéo (audio système)",
			L"Livelli; alto = davanti. [Hide] esclude video (audio sistema)",
			L"Capas; arriba = frente. [Hide] excluye vídeo (audio sistema)",
			L"합성 레이어. 위=앞. [Hide]는 영상 제외(소리는 시스템음)",
			L"合成层；上为前。[Hide]排除画面（声音走系统音）",
			L"الطبقات؛ الأعلى أمام. [Hide] يستبعد الفيديو (الصوت عبر النظام)",
			L"Слои; верх = перед. [Hide] без видео (звук через системный)",
			L"Ebenen; oben = vorne. [Hide] ohne Video (Ton über System)",
			L"Camadas; cima = frente. [Hide] exclui vídeo (áudio do sistema)",
			L"Lagen; boven = voor. [Hide] zonder video (audio via systeem)",
			L"Warstwy; góra = przód. [Hide] bez wideo (dźwięk systemowy)",
			L"Katmanlar; üst = ön. [Hide] görüntüyü çıkarır (ses sistemden)"));
		m_tooltip.AddTool(&m_add, LL14(L"選択ウィンドウを合成レイヤへ追加", L"Add selected window to layers", L"Ajouter la fenêtre aux calques", L"Aggiungi finestra ai livelli", L"Añadir ventana a capas", L"선택 창을 레이어에 추가", L"将所选窗口加入层", L"إضافة النافذة إلى الطبقات", L"Добавить окно в слои", L"Fenster zu Ebenen hinzufügen", L"Adicionar janela às camadas", L"Venster aan lagen toevoegen", L"Dodaj okno do warstw", L"Seçili pencereyi katmanlara ekle"));
		m_tooltip.AddTool(&m_remove, LL14(L"選択レイヤを削除", L"Remove selected layer", L"Retirer le calque", L"Rimuovi livello", L"Quitar capa", L"선택 레이어 삭제", L"删除所选层", L"إزالة الطبقة", L"Удалить слой", L"Ebene entfernen", L"Remover camada", L"Laag verwijderen", L"Usuń warstwę", L"Seçili katmanı kaldır"));
		m_tooltip.AddTool(&m_zUp, LL14(L"レイヤを手前へ", L"Bring layer forward", L"Calque vers l'avant", L"Porta avanti", L"Traer al frente", L"레이어를 앞으로", L"将层前移", L"تقديم الطبقة", L"Слой вперёд", L"Ebene nach vorne", L"Trazer para frente", L"Laag naar voren", L"Warstwa do przodu", L"Katmanı öne getir"));
		m_tooltip.AddTool(&m_zDown, LL14(L"レイヤを奥へ", L"Send layer back", L"Calque vers l'arrière", L"Porta indietro", L"Enviar atrás", L"레이어를 뒤로", L"将层后移", L"تأخير الطبقة", L"Слой назад", L"Ebene nach hinten", L"Enviar para trás", L"Laag naar achteren", L"Warstwa do tyłu", L"Katmanı geriye gönder"));
		m_tooltip.AddTool(&m_applyGeo, LL14(L"X Y W H を選択レイヤに適用", L"Apply X Y W H to selected layer", L"Appliquer X Y W H", L"Applica X Y W H", L"Aplicar X Y W H", L"X Y W H를 선택 레이어에 적용", L"将 X Y W H 应用到所选层", L"تطبيق X Y W H", L"Применить X Y W H", L"X Y W H übernehmen", L"Aplicar X Y W H", L"X Y W H toepassen", L"Zastosuj X Y W H", L"X Y W H uygula"));
		m_tooltip.AddTool(&m_fit, LL14(L"キャンバスにフィット", L"Fit to canvas", L"Ajuster au canevas", L"Adatta al canvas", L"Ajustar al lienzo", L"캔버스에 맞춤", L"铺满画布", L"ملاءمة اللوحة", L"Вписать в холст", L"In die Fläche einpassen", L"Ajustar à tela", L"Passen op canvas", L"Dopasuj do płótna", L"Tuvale sığdır"));
		m_tooltip.AddTool(&m_scale50, LL14(L"元ウィンドウの50%サイズ", L"50% of source window size", L"50% de la taille source", L"50% della dimensione origine", L"50% del tamaño origen", L"원본 창의 50% 크기", L"源窗口 50% 大小", L"50٪ من حجم النافذة", L"50% размера окна", L"50% der Quellgröße", L"50% do tamanho da janela", L"50% van bronvenster", L"50% rozmiaru okna", L"Kaynak pencerenin %50'si"));
		m_tooltip.AddTool(&m_scale100, LL14(L"元ウィンドウの実寸", L"100% source window size", L"Taille source 100%", L"Dimensione origine 100%", L"Tamaño origen 100%", L"원본 창 실측", L"源窗口实际大小", L"حجم النافذة 100٪", L"100% размера окна", L"100% Quellgröße", L"100% do tamanho da janela", L"100% bronvenster", L"100% rozmiaru okna", L"Kaynak pencere %100"));
		m_tooltip.AddTool(&m_tile, LL14(L"レイヤをタイル状に整列", L"Tile layers", L"Mosaïque des calques", L"Affianca livelli", L"Mosaico de capas", L"레이어를 바둑판 정렬", L"平铺各层", L"تجانب الطبقات", L"Плиткой слои", L"Ebenen kacheln", L"Mosaico das camadas", L"Lagen tegelen", L"Ułóż warstwy", L"Katmanları döşe"));
		m_tooltip.AddTool(&m_path, LL14(L"MP4の保存先パス", L"MP4 save path", L"Chemin MP4", L"Percorso MP4", L"Ruta MP4", L"MP4 저장 경로", L"MP4 保存路径", L"مسار حفظ MP4", L"Путь MP4", L"MP4-Pfad", L"Caminho MP4", L"MP4-pad", L"Ścieżka MP4", L"MP4 kayıt yolu"));
		m_tooltip.AddTool(&m_browse, LL14(L"保存先を参照", L"Browse save location", L"Parcourir l'emplacement", L"Sfoglia destinazione", L"Examinar destino", L"저장 위치 찾아보기", L"浏览保存位置", L"استعراض موقع الحفظ", L"Обзор папки сохранения", L"Speicherort wählen", L"Procurar local", L"Bladeren naar locatie", L"Przeglądaj lokalizację", L"Kayıt konumuna göz at"));
		m_tooltip.AddTool(&m_start, LL14(L"録画開始/停止", L"Start/stop recording", L"Démarrer/arrêter", L"Avvia/ferma", L"Iniciar/detener", L"녹화 시작/중지", L"开始/停止录制", L"بدء/إيقاف التسجيل", L"Старт/стоп записи", L"Aufnahme starten/stoppen", L"Iniciar/parar gravação", L"Opname starten/stoppen", L"Start/stop nagrywania", L"Kaydı başlat/durdur"));
		m_tooltip.AddTool(&m_help, LL14(L"操作ガイドを表示", L"Show operation guide", L"Afficher le guide", L"Mostra guida", L"Mostrar guía", L"조작 가이드 표시", L"显示操作指南", L"إظهار الدليل", L"Показать руководство", L"Bedienungsanleitung", L"Mostrar guia", L"Handleiding tonen", L"Pokaż przewodnik", L"İşlem kılavuzunu göster"));
		m_tooltip.AddTool(&m_meterMic, LL14(L"マイク入力レベル(リアルタイム)", L"Mic level (live)", L"Niveau micro (live)", L"Livello microfono (live)", L"Nivel de micrófono (en vivo)", L"마이크 레벨(실시간)", L"麦克风电平(实时)", L"مستوى الميكروفون (مباشر)", L"Уровень микрофона (live)", L"Mikrofonpegel (live)", L"Nível do microfone (ao vivo)", L"Microfoonniveau (live)", L"Poziom mikrofonu (na żywo)", L"Mikrofon seviyesi (canlı)"));
		m_tooltip.AddTool(&m_meterSys, LL14(L"システム音レベル(ループバック=演奏込み)", L"System audio level (loopback incl. playback)", L"Niveau système (lecture incluse)", L"Livello sistema (include riproduzione)", L"Nivel del sistema (incluye reproducción)", L"시스템 소리 레벨(재생 포함)", L"系统声音电平(含播放)", L"مستوى صوت النظام (يشمل التشغيل)", L"Уровень системного звука (с воспроизведением)", L"Systemtonpegel inkl. Wiedergabe", L"Nível do sistema (inclui reprodução)", L"Systeemniveau (inclusief afspelen)", L"Poziom dźwięku systemu (z odtwarzaniem)", L"Sistem sesi seviyesi (oynatma dahil)"));
		m_tooltip.AddTool(&m_meterMix, LL14(L"プレビュー用ピーク(システム相当)", L"Preview peak (system-equivalent)", L"Pic d'aperçu (système)", L"Picco anteprima (sistema)", L"Pico de vista previa (sistema)", L"미리보기 피크(시스템 상당)", L"预览峰值(系统相当)", L"ذروة المعاينة (مكافئ النظام)", L"Пик превью (как система)", L"Vorschau-Peak (System)", L"Pico da prévia (sistema)", L"Voorbeeldpiek (systeem)", L"Szczyt podglądu (system)", L"Önizleme tepe (sistem)"));
		CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 420, 12000);
	}

	ApplyPreviewTimer(); // FPSコンボに連動(録画と同じ間隔でプレビュー)
	SetTimer(SC_TIMER_UI, 200, NULL);
	UpdatePreview();
	RefreshOpaqueUi();
	StartPeakMonitor();
	FitToWorkArea();
	// Fit が子を動かしたあと、Live フッターを Live 行基準で再確定
	CCC_CaptionLayout(m_hWnd); // 先に P/閉じる等を確定してから ? を左隣へ
	LayoutHelpBtn();
	return TRUE;
}

BOOL CScreenCaptureDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	if (m_picking && pMsg && pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_ESCAPE) {
		m_picking = FALSE;
		ReleaseCapture();
		m_status.SetWindowText(LL14(
			L"選択を取り消しました。", L"Pick cancelled.", L"Sélection annulée.", L"Selezione annullata.",
			L"Selección cancelada.", L"선택을 취소했습니다.", L"已取消选择。", L"تم إلغاء الاختيار.",
			L"Выбор отменён.", L"Auswahl abgebrochen.", L"Seleção cancelada.", L"Selectie geannuleerd.",
			L"Anulowano wybór.", L"Seçim iptal edildi."));
		return TRUE;
	}
	if (m_picking && pMsg && pMsg->message == WM_LBUTTONDOWN) {
		POINT pt = pMsg->pt;
		HWND h = ::WindowFromPoint(pt);
		if (h) AddLayerHwnd(h);
		m_picking = FALSE;
		ReleaseCapture();
		m_status.SetWindowText(LL14(
			L"ウィンドウを追加しました。", L"Window added.", L"Fenêtre ajoutée.", L"Finestra aggiunta.",
			L"Ventana añadida.", L"창을 추가했습니다.", L"已添加窗口。", L"تمت إضافة النافذة.",
			L"Окно добавлено.", L"Fenster hinzugefügt.", L"Janela adicionada.", L"Venster toegevoegd.",
			L"Dodano okno.", L"Pencere eklendi."));
		return TRUE;
	}
	if (m_uiLocked && pMsg) {
		const UINT m = pMsg->message;
		if (m >= WM_KEYFIRST && m <= WM_KEYLAST) {
			if (pMsg->hwnd == m_start.GetSafeHwnd() || pMsg->hwnd == m_close.GetSafeHwnd())
				return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
			if (m == WM_KEYDOWN && pMsg->wParam == VK_ESCAPE)
				return TRUE;
		}
	}
	return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
}

void CScreenCaptureDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
	if (m_picking) {
		ClientToScreen(&point);
		HWND h = ::WindowFromPoint(point);
		if (h) AddLayerHwnd(h);
		m_picking = FALSE;
		ReleaseCapture();
		return;
	}
	CCustomBlurDialogBase::OnLButtonDown(nFlags, point);
}

void CScreenCaptureDlg::OnCbnSelchangeMode()
{
	EnableComposeUi(IsWindowComposeMode());
	if (m_layerCnt > 0)
		FitAllLayersIntoCanvas();
	else
		UpdatePreview();
	PersistUiToSavedata();
}

void CScreenCaptureDlg::OnCbnSelchangeCanvas()
{
	if (m_layerCnt > 0)
		FitAllLayersIntoCanvas();
	else
		UpdatePreview(TRUE);
	PersistUiToSavedata();
}

int CScreenCaptureDlg::CurrentPreviewFps() const
{
	static const int fpsTab[] = { 10, 15, 20, 24, 30, 60, 90, 120 };
	int sel = m_fps.GetSafeHwnd() ? m_fps.GetCurSel() : -1;
	if (sel < 0 || sel >= (int)_countof(fpsTab)) {
		int f = savedata.cap_fps;
		if (f < 5) f = 15;
		if (f > 120) f = 120;
		return f;
	}
	return fpsTab[sel];
}

void CScreenCaptureDlg::ApplyPreviewTimer()
{
	if (!GetSafeHwnd()) return;
	int fps = CurrentPreviewFps();
	if (fps < 5) fps = 5;
	if (fps > 120) fps = 120;
	// 配信中はプレビューを軽く（Enc 優先）
	if (InterlockedCompareExchange(&m_run, 0, 0) && m_liveMode && fps > 10)
		fps = 10;
	UINT ms = (UINT)(1000 / fps);
	if (ms < 8) ms = 8; // ~120fps
	// エフェクトチェーンON時はプレビューだけ軽く間引き（録画FPSは別経路）
	{
		int cn = 0;
		GetFxChain(NULL, &cn);
		if (cn > 0 && ms < 16) ms = 16; // ~60fps
		if (cn > 2 && ms < 20) ms = 20; // ~50fps
	}
	KillTimer(SC_TIMER_PREV);
	SetTimer(SC_TIMER_PREV, ms, NULL);
}

void CScreenCaptureDlg::OnCbnSelchangeFps()
{
	if (m_uiLocked) return;
	PersistUiToSavedata();
	ApplyPreviewTimer();
	if (m_preview.GetSafeHwnd())
		m_preview.Invalidate(FALSE);
}

void CScreenCaptureDlg::OnCbnSelchangeEffect()
{
	if (m_uiLocked) return;
	const int sel = m_effect.GetCurSel();
	// カスタム行は配線側が本体なので触らない
	if (sel >= SC_FX_COUNT)
		return;
	ApplyFxComboToChain();
	PersistUiToSavedata();
	UpdatePreview(TRUE);
}

void CScreenCaptureDlg::OnBnClickedCropFull()
{
	if (m_uiLocked) return;
	const int sel = m_layer.GetCurSel();
	if (sel < 0 || sel >= m_layerCnt) return;
	m_layers[sel].srcX = 0;
	m_layers[sel].srcY = 0;
	m_layers[sel].srcW = 0;
	m_layers[sel].srcH = 0;
	SyncGeoEditsFromSel();
	RefreshLayerList();
	m_layer.SetCurSel(sel);
	UpdatePreview(TRUE);
}

void CScreenCaptureDlg::OnLbnSelchangeLayer()
{
	SyncGeoEditsFromSel();
	if (m_preview.GetSafeHwnd())
		m_preview.Invalidate(FALSE);
}

void CScreenCaptureDlg::OnBnClickedRefresh()
{
	if (m_uiLocked) return;
	RefreshAvailList();
	RefreshOpaqueUi();
}

void CScreenCaptureDlg::OnBnClickedAdd()
{
	if (m_uiLocked) return;
	const int sel = m_avail.GetCurSel();
	if (sel < 0 || sel >= m_availCnt) return;
	AddLayerHwnd(m_availHwnd[sel]);
}

void CScreenCaptureDlg::OnBnClickedRemove()
{
	if (m_uiLocked) return;
	const int sel = m_layer.GetCurSel();
	if (sel < 0 || sel >= m_layerCnt) return;
	for (int i = sel; i < m_layerCnt - 1; ++i)
		m_layers[i] = m_layers[i + 1];
	m_layerCnt--;
	RefreshLayerList();
	UpdatePreview();
}

void CScreenCaptureDlg::OnBnClickedZUp()
{
	if (m_uiLocked) return;
	const int sel = m_layer.GetCurSel();
	if (sel <= 0 || sel >= m_layerCnt) return;
	Layer tmp = m_layers[sel - 1];
	m_layers[sel - 1] = m_layers[sel];
	m_layers[sel] = tmp;
	RefreshLayerList();
	m_layer.SetCurSel(sel - 1);
	UpdatePreview();
}

void CScreenCaptureDlg::OnBnClickedZDown()
{
	if (m_uiLocked) return;
	const int sel = m_layer.GetCurSel();
	if (sel < 0 || sel >= m_layerCnt - 1) return;
	Layer tmp = m_layers[sel + 1];
	m_layers[sel + 1] = m_layers[sel];
	m_layers[sel] = tmp;
	RefreshLayerList();
	m_layer.SetCurSel(sel + 1);
	UpdatePreview();
}

void CScreenCaptureDlg::OnBnClickedPick()
{
	if (m_uiLocked) return;
	m_picking = TRUE;
	SetCapture();
	m_status.SetWindowText(LL14(
		L"対象ウィンドウをクリックしてください (Escで取消)",
		L"Click the target window (Esc to cancel)",
		L"Cliquez la fenêtre cible (Échap pour annuler)",
		L"Clicca la finestra (Esc per annullare)",
		L"Haga clic en la ventana (Esc para cancelar)",
		L"대상 창을 클릭하세요 (Esc 취소)",
		L"请点击目标窗口（Esc 取消）",
		L"انقر النافذة المستهدفة (Esc للإلغاء)",
		L"Кликните целевое окно (Esc — отмена)",
		L"Ziel-Fenster anklicken (Esc abbricht)",
		L"Clique na janela alvo (Esc cancela)",
		L"Klik het doelvenster (Esc annuleert)",
		L"Kliknij okno docelowe (Esc anuluje)",
		L"Hedef pencereye tıklayın (Esc iptal)"));
}

void CScreenCaptureDlg::OnBnClickedApplyGeo()
{
	if (m_uiLocked) return;
	ApplyGeoEditsToSel();
}

void CScreenCaptureDlg::OnBnClickedFit()
{
	if (m_uiLocked) return;
	FitSelected(0);
}

void CScreenCaptureDlg::OnBnClickedScale50()
{
	if (m_uiLocked) return;
	FitSelected(50);
}

void CScreenCaptureDlg::OnBnClickedScale100()
{
	if (m_uiLocked) return;
	FitSelected(100);
}

void CScreenCaptureDlg::OnBnClickedTile()
{
	if (m_uiLocked) return;
	TileLayers();
}

void CScreenCaptureDlg::OnBnClickedBrowse()
{
	if (m_uiLocked) return;
	CString cur;
	m_path.GetWindowText(cur);
	cur = NormalizeOutPath(cur);
	CFileDialog dlg(FALSE, L"mp4", cur.IsEmpty() ? L"capture.mp4" : cur,
		OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
		LL14(L"MP4 (*.mp4)|*.mp4|すべてのファイル (*.*)|*.*||",
			L"MP4 (*.mp4)|*.mp4|All Files (*.*)|*.*||",
			L"MP4 (*.mp4)|*.mp4|Tous les fichiers (*.*)|*.*||",
			L"MP4 (*.mp4)|*.mp4|Tutti i file (*.*)|*.*||",
			L"MP4 (*.mp4)|*.mp4|Todos los archivos (*.*)|*.*||",
			L"MP4 (*.mp4)|*.mp4|모든 파일 (*.*)|*.*||",
			L"MP4 (*.mp4)|*.mp4|所有文件 (*.*)|*.*||",
			L"MP4 (*.mp4)|*.mp4|All Files (*.*)|*.*||",
			L"MP4 (*.mp4)|*.mp4|Все файлы (*.*)|*.*||",
			L"MP4 (*.mp4)|*.mp4|Alle Dateien (*.*)|*.*||",
			L"MP4 (*.mp4)|*.mp4|Todos os ficheiros (*.*)|*.*||",
			L"MP4 (*.mp4)|*.mp4|Alle bestanden (*.*)|*.*||",
			L"MP4 (*.mp4)|*.mp4|Wszystkie pliki (*.*)|*.*||",
			L"MP4 (*.mp4)|*.mp4|Tum dosyalar (*.*)|*.*||"), this);
	if (dlg.DoModal() == IDOK) {
		m_path.SetWindowText(NormalizeOutPath(dlg.GetPathName()));
		PersistUiToSavedata();
		RefreshOpaqueUi();
	}
}

BOOL CScreenCaptureDlg::StartRecording()
{
	if (InterlockedCompareExchange(&m_run, 0, 0)) return FALSE;

	// レイヤがあるのに画面モードだとキャプチャUIや前面窓が写る → ウィンドウ合成へ強制
	if (m_layerCnt > 0 && !IsWindowComposeMode()) {
		m_mode.SetCurSel(SavedModeToComboSel(SC_MODE_WINDOWS, 0));
		EnableComposeUi(TRUE);
	}
	// 録画直前は配置を維持したままキャンバス内へクランプ（中央寄せで戻さない）
	if (m_layerCnt > 0) {
		ClampAllLayersToCanvas();
		RefreshLayerList();
		SyncGeoEditsFromSel();
	}
	::SetWindowDisplayAffinity(m_hWnd, WDA_EXCLUDEFROMCAPTURE);

	// ファイル名の日時を録画開始時点で更新（上書き連発を防ぐ）
	{
		CString cur;
		m_path.GetWindowText(cur);
		m_path.SetWindowText(NormalizeOutPath(RefreshCaptureOutPathTimestamp(cur)));
	}
	PersistUiToSavedata();

	ComposeSnap snap;
	BuildComposeSnap(snap);
	// 録画スレッドがプレビューキャッシュへ書き込めるよう先に確保
	RefreshComposeCache();
	if (snap.mode == SC_MODE_WINDOWS && snap.layerCnt <= 0) {
		m_status.SetWindowText(LL14(
			L"合成レイヤにウィンドウを追加してください。",
			L"Add at least one window to the layers.",
			L"Ajoutez au moins une fenêtre aux calques.",
			L"Aggiungi almeno una finestra ai livelli.",
			L"Añada al menos una ventana a las capas.",
			L"합성 레이어에 창을 추가하세요.",
			L"请至少向合成层添加一个窗口。",
			L"أضف نافذة واحدة واحدةً إلى الطبقات.",
			L"Добавьте хотя бы одно окно в слои.",
			L"Fügen Sie mindestens ein Fenster hinzu.",
			L"Adicione pelo menos uma janela às camadas.",
			L"Voeg minstens één venster toe.",
			L"Dodaj co najmniej jedno okno do warstw.",
			L"Katmanlara en az bir pencere ekleyin."));
		return FALSE;
	}

	CString path;
	m_path.GetWindowText(path);
	path = NormalizeOutPath(path);

	const BOOL liveChecked = m_live.GetSafeHwnd() && m_live.GetCheck();
	int liveSvc = savedata.cap_live_service;
	if (liveSvc < 0 || liveSvc > 2) liveSvc = 0;

	if (liveChecked) {
		PersistLiveFieldsFromUi();
		TCHAR ffCheck[MAX_PATH] = {};
		if (!EnsureFfmpegAvailable(TRUE)) {
			if (!ResolveFfmpegPath(ffCheck, MAX_PATH)) {
				m_status.SetWindowText(LL14(
					L"ffmpeg.exe が見つかりません。ライブ配信UIから公式ビルドを取得できます。",
					L"ffmpeg.exe not found. You can download the official build from the live settings UI.",
					L"ffmpeg.exe introuvable. Telechargez la build officielle depuis l'UI live.",
					L"ffmpeg.exe non trovato. Scarica la build ufficiale dall'UI live.",
					L"No se encontro ffmpeg.exe. Descargue la build oficial desde la UI en vivo.",
					L"ffmpeg.exe 없음. 라이브 UI에서 공식 빌드를 받을 수 있습니다.",
					L"找不到 ffmpeg.exe。可从直播设置界面获取官方构建。",
					L"لم يُعثر على ffmpeg.exe. يمكن تنزيل البناء الرسمي من واجهة البث.",
					L"ffmpeg.exe не найден. Официальную сборку можно скачать из UI эфира.",
					L"ffmpeg.exe nicht gefunden. Offizielle Build uber die Live-UI holen.",
					L"ffmpeg.exe nao encontrado. Baixe a build oficial pela UI ao vivo.",
					L"ffmpeg.exe niet gevonden. Download de officiele build via de live-UI.",
					L"Nie znaleziono ffmpeg.exe. Oficjalny build mozna pobrac z UI live.",
					L"ffmpeg.exe bulunamadi. Resmi surumu canli arayuzden indirebilirsiniz."));
			}
			return FALSE;
		}
		if (liveSvc == 0) {
			// 既に「配信枠作成」済みなら再利用（毎回新規枠を作らない）
			CString urlHave = savedata.cap_live_url;
			CString keyHave = savedata.cap_live_key;
			urlHave.Trim();
			keyHave.Trim();
			const BOOL haveReady = !urlHave.IsEmpty() && !keyHave.IsEmpty()
				&& savedata.yt_broadcast_id[0] != 0
				&& savedata.yt_stream_id[0] != 0;
			if (!haveReady) {
				CString ytErr;
				if (!PrepareYouTubeLiveBeforeStart(ytErr)) {
					m_status.SetWindowText(ytErr.IsEmpty()
						? LL14(L"YouTube 配信の準備に失敗しました。", L"YouTube live prepare failed.",
							L"Préparation YouTube échouée.", L"Preparazione YouTube non riuscita.",
							L"Falló la preparación de YouTube.", L"YouTube 방송 준비 실패.",
							L"YouTube 直播准备失败。", L"فشل تجهيز بث YouTube.",
							L"Не удалось подготовить эфир YouTube.", L"YouTube-Live-Vorbereitung fehlgeschlagen.",
							L"Falha ao preparar YouTube ao vivo.", L"Voorbereiding YouTube-live mislukt.",
							L"Przygotowanie YouTube nie powiodło się.", L"YouTube canlı hazırlığı başarısız.")
						: ytErr);
					return FALSE;
				}
			}
		} else {
			CString url = savedata.cap_live_url;
			CString key = savedata.cap_live_key;
			url.Trim();
			key.Trim();
			if (url.IsEmpty() || key.IsEmpty()) {
				m_status.SetWindowText(LL14(
					L"RTMP URL とストリームキーを入力してください（設定…）。",
					L"Enter RTMP URL and stream key (Settings…).",
					L"Saisissez l'URL RTMP et la clé (Réglages…).",
					L"Inserisci URL RTMP e chiave stream (Impostazioni…).",
					L"Introduzca URL RTMP y clave de stream (Ajustes…).",
					L"RTMP URL과 스트림 키를 입력하세요(설정…).",
					L"请输入 RTMP URL 和串流密钥（设置…）。",
					L"أدخل رابط RTMP ومفتاح البث (إعدادات…).",
					L"Введите RTMP URL и ключ потока (Настройки…).",
					L"RTMP-URL und Stream-Key eingeben (Einstellungen…).",
					L"Introduza URL RTMP e chave de stream (Definições…).",
					L"Voer RTMP-URL en streamkey in (Instellingen…).",
					L"Wprowadź URL RTMP i klucz streamu (Ustawienia…).",
					L"RTMP URL ve yayın anahtarını girin (Ayarlar…)."));
				OpenScLiveSettingsModeless(this);
				return FALSE;
			}
		}
		m_liveMode = TRUE;
		m_liveService = liveSvc;
		m_ytLiveTransitionDone = FALSE;
		m_ytTestingRequested = FALSE;
		InterlockedExchange(&m_ytLivePhase, 1);
		m_ytStreamStatus[0] = 0;
		m_outPath.Empty();
		ScLiveSettingsSetStreamingUi(TRUE);
	} else {
		m_liveMode = FALSE;
		m_liveService = 0;
		if (path.IsEmpty()) {
			m_status.SetWindowText(LL14(
				L"保存先を指定してください。", L"Please specify a save path.", L"Indiquez un chemin.",
				L"Specificare un percorso.", L"Especifique una ruta.", L"저장 위치를 지정하세요.",
				L"请指定保存路径。", L"حدد المسار.", L"Укажите путь.", L"Bitte Pfad angeben.",
				L"Indique um caminho.", L"Geef een pad op.", L"Podaj ścieżkę.", L"Yol belirtin."));
			return FALSE;
		}
		{
			const int slash = (path.ReverseFind(L'\\') > path.ReverseFind(L'/')) ? path.ReverseFind(L'\\') : path.ReverseFind(L'/');
			if (slash > 0)
				CreateDirectory(path.Left(slash), NULL);
		}
		::DeleteFile(path);
		m_outPath = path;
		m_path.SetWindowText(path);
	}

	m_fpsVal = CurrentPreviewFps();
	if (m_fpsVal < 5) m_fpsVal = 15;
	if (m_fpsVal > 120) m_fpsVal = 120;
	m_withAudio = m_audio.GetCheck() ? TRUE : FALSE;
	m_withMic = m_mic.GetCheck() ? TRUE : FALSE;
	// ライブはシステム音声を常に送る
	if (m_liveMode) {
		m_withAudio = TRUE;
		if (m_audio.GetSafeHwnd()) m_audio.SetCheck(BST_CHECKED);
	}
	InterlockedExchange(&m_ytGoLiveRequest, 0);
	m_ytGoLiveLastTick = 0;
	if (m_ytGoLiveThread) {
		WaitForSingleObject(m_ytGoLiveThread, 100);
		CloseHandle(m_ytGoLiveThread);
		m_ytGoLiveThread = NULL;
	}

	// 固着 WGC を破棄して新規セッションで録る（WGC=高速、失敗時のみ GDI）。
	InterlockedExchange(&m_encodeGdi, 0);
	ScWgcReleaseSessions();

	EnterCriticalSection(&m_snapCs);
	m_recSnap = snap;
	LeaveCriticalSection(&m_snapCs);

	// 録画スレッドが m_cache を更新するため、先にキャンバスサイズで確保
	RefreshComposeCache();

	// 録画スレッドがループバックを独占するのでプレビュー監視を止める
	StopPeakMonitor();

	InterlockedExchange(&m_stop, 0);
	InterlockedExchange(&m_lastHr, S_OK);
	InterlockedExchange(&m_lastStage, 0);
	InterlockedExchange(&m_frameCnt, 0);
	InterlockedExchange(&m_encFpsX10, 0);
	m_startTick = GetTickCount();
	m_everStarted = TRUE;

	uintptr_t th = _beginthreadex(NULL, 0, CaptureThread, this, 0, NULL);
	if (!th) {
		InterlockedExchange(&m_encodeGdi, 0);
		StartPeakMonitor();
		m_status.SetWindowText(LL14(
			L"スレッドを開始できませんでした。", L"Could not start thread.", L"Impossible de démarrer le thread.",
			L"Impossibile avviare il thread.", L"No se pudo iniciar el hilo.", L"스레드를 시작할 수 없습니다.",
			L"无法启动线程。", L"تعذر بدء الخيط.", L"Не удалось запустить поток.", L"Thread startete nicht.",
			L"Não foi possível iniciar a thread.", L"Kon thread niet starten.", L"Nie można uruchomić wątku.", L"İş parçacığı başlatılamadı."));
		return FALSE;
	}
	m_thread = (HANDLE)th;
	InterlockedExchange(&m_run, 1);
	SetRecordingUi(TRUE);
	ApplyPreviewTimer();
	m_status.SetWindowText(m_liveMode
		? LL14(
			L"配信送信中…（Studioが「公開予約」のままなら RTMP 未到達。ffmpeg/URLを確認）",
			L"Sending stream… (If Studio stays Scheduled, RTMP is not reaching YouTube)",
			L"Envoi… (Si Studio reste planifie, RTMP n'arrive pas)",
			L"Invio… (Se Studio resta programmato, RTMP non arriva)",
			L"Enviando… (Si Studio sigue programado, RTMP no llega)",
			L"송출 중…(Studio가 예약이면 RTMP 미도달)",
			L"推流中…（若 Studio 仍为预约，则 RTMP 未到达）",
			L"جارٍ الإرسال… (إذا بقي مجدولًا فـ RTMP لا يصل)",
			L"Отправка… (если всё ещё запланировано — RTMP не доходит)",
			L"Sende… (Bleibt Studio geplant, kommt RTMP nicht an)",
			L"A enviar… (Se continuar agendado, RTMP nao chega)",
			L"Verzenden… (Blijft Studio gepland, dan komt RTMP niet aan)",
			L"Wysylanie… (Jesli nadal zaplanowane, RTMP nie dociera)",
			L"Gonderiliyor… (Studio planli kalirsa RTMP ulasmıyor)")
		: LL14(
			L"録画中…",
			L"Recording…",
			L"Enregistrement…",
			L"Registrazione…",
			L"Grabando…",
			L"녹화 중…",
			L"录制中…",
			L"جاري التسجيل…",
			L"Запись…",
			L"Aufnahme…",
			L"A gravar…",
			L"Opnemen…",
			L"Nagrywanie…",
			L"Kaydediliyor…"));
	return TRUE;
}

void CScreenCaptureDlg::StopRecording()
{
	if (m_stopping) return;
	m_stopping = TRUE;
	InterlockedExchange(&m_stop, 1);
	if (m_thread) {
		WaitForSingleObject(m_thread, 15000);
		CloseHandle(m_thread);
		m_thread = NULL;
	}
	if (m_ytGoLiveThread) {
		WaitForSingleObject(m_ytGoLiveThread, 60000);
		CloseHandle(m_ytGoLiveThread);
		m_ytGoLiveThread = NULL;
	}
	InterlockedExchange(&m_run, 0);
	InterlockedExchange(&m_encodeGdi, 0);
	const BOOL wasLive = m_liveMode;
	const int wasLiveSvc = m_liveService;
	if (wasLive && wasLiveSvc == 0)
		FinishYouTubeLiveAfterStop();
	m_liveMode = FALSE;
	m_ytLiveTransitionDone = FALSE;
	m_ytTestingRequested = FALSE;
	InterlockedExchange(&m_ytLivePhase, 0);
	InterlockedExchange(&m_ytGoLiveRequest, 0);
	m_ytGoLiveLastTick = 0;
	if (wasLive)
		ScLiveSettingsSetStreamingUi(FALSE);
	const BOOL uiAlive = (GetSafeHwnd() != NULL);
	if (uiAlive && IsIconic())
		ShowWindow(SW_RESTORE);
	const HRESULT capHr = (HRESULT)InterlockedCompareExchange(&m_lastHr, 0, 0);
	const LONG frames = InterlockedCompareExchange(&m_frameCnt, 0, 0);

	if (uiAlive) {
		SetRecordingUi(FALSE);
		ApplyPreviewTimer();
		if (SUCCEEDED(capHr) && frames > 0) {
			if (wasLive) {
				m_status.SetWindowText(LL14(
					L"配信を終了しました。", L"Live stream ended.", L"Diffusion terminée.", L"Diretta terminata.",
					L"Transmisión finalizada.", L"방송을 종료했습니다.", L"直播已结束。", L"انتهى البث.",
					L"Эфир завершён.", L"Livestream beendet.", L"Transmissão encerrada.", L"Livestream beëindigd.",
					L"Transmisja zakończona.", L"Canlı yayın bitti."));
			} else {
				CString msg;
				msg.Format(LL14(
					L"保存しました:\n%s", L"Saved:\n%s", L"Enregistré:\n%s", L"Salvato:\n%s",
					L"Guardado:\n%s", L"저장됨:\n%s", L"已保存:\n%s", L"تم الحفظ:\n%s",
					L"Сохранено:\n%s", L"Gespeichert:\n%s", L"Guardado:\n%s", L"Opgeslagen:\n%s",
					L"Zapisano:\n%s", L"Kaydedildi:\n%s"), (LPCTSTR)m_outPath);
				m_status.SetWindowText(msg);
			}
		} else if (FAILED(capHr)) {
			const LONG stage = InterlockedCompareExchange(&m_lastStage, 0, 0);
			CString msg;
			msg.Format(wasLive
				? LL14(
					L"配信に失敗しました (HRESULT=0x%08X, stage=%ld)。ffmpeg / URL / 認証を確認。",
					L"Live failed (HRESULT=0x%08X, stage=%ld). Check ffmpeg / URL / auth.",
					L"Échec du live (HRESULT=0x%08X, stage=%ld). Vérifiez ffmpeg / URL / auth.",
					L"Diretta non riuscita (HRESULT=0x%08X, stage=%ld). Controlla ffmpeg / URL / auth.",
					L"Falló el vivo (HRESULT=0x%08X, stage=%ld). Revise ffmpeg / URL / auth.",
					L"방송 실패 (HRESULT=0x%08X, stage=%ld). ffmpeg / URL / 인증을 확인.",
					L"直播失败 (HRESULT=0x%08X, stage=%ld)。请检查 ffmpeg / URL / 认证。",
					L"فشل البث (HRESULT=0x%08X, stage=%ld). تحقق من ffmpeg / الرابط / المصادقة.",
					L"Сбой эфира (HRESULT=0x%08X, stage=%ld). Проверьте ffmpeg / URL / auth.",
					L"Live fehlgeschlagen (HRESULT=0x%08X, stage=%ld). ffmpeg / URL / Auth prüfen.",
					L"Falha no ao vivo (HRESULT=0x%08X, stage=%ld). Verifique ffmpeg / URL / auth.",
					L"Live mislukt (HRESULT=0x%08X, stage=%ld). Controleer ffmpeg / URL / auth.",
					L"Transmisja nie powiodła się (HRESULT=0x%08X, stage=%ld). Sprawdź ffmpeg / URL / auth.",
					L"Canlı başarısız (HRESULT=0x%08X, stage=%ld). ffmpeg / URL / auth kontrol edin.")
				: LL14(
					L"録画に失敗しました (HRESULT=0x%08X, stage=%ld)。音声オフで再試行も可。",
					L"Capture failed (HRESULT=0x%08X, stage=%ld). Try with audio off.",
					L"Échec de la capture (HRESULT=0x%08X, stage=%ld). Essayez sans audio.",
					L"Cattura non riuscita (HRESULT=0x%08X, stage=%ld). Prova senza audio.",
					L"Error de captura (HRESULT=0x%08X, stage=%ld). Pruebe sin audio.",
					L"캡처 실패 (HRESULT=0x%08X, stage=%ld). 오디오 끄고 재시도.",
					L"捕获失败 (HRESULT=0x%08X, stage=%ld)。可尝试关闭音频。",
					L"فشل الالتقاط (HRESULT=0x%08X, stage=%ld). جرّب بدون صوت.",
					L"Ошибка захвата (HRESULT=0x%08X, stage=%ld). Попробуйте без звука.",
					L"Aufnahme fehlgeschlagen (HRESULT=0x%08X, stage=%ld). Ohne Audio versuchen.",
					L"Falha na captura (HRESULT=0x%08X, stage=%ld). Tente sem áudio.",
					L"Opname mislukt (HRESULT=0x%08X, stage=%ld). Probeer zonder audio.",
					L"Przechwytywanie nie powiodło się (HRESULT=0x%08X, stage=%ld). Spróbuj bez dźwięku.",
					L"Yakalama başarısız (HRESULT=0x%08X, stage=%ld). Ses kapalı deneyin."),
				(unsigned)capHr, (long)stage);
			m_status.SetWindowText(msg);
		} else {
			m_status.SetWindowText(LL14(
				L"フレームがありませんでした。", L"No frames were captured.", L"Aucune image capturée.",
				L"Nessun fotogramma.", L"No se capturaron fotogramas.", L"프레임이 없습니다.",
				L"没有捕获到帧。", L"لا إطارات.", L"Нет кадров.", L"Keine Frames.",
				L"Sem fotogramas.", L"Geen frames.", L"Brak klatek.", L"Kare yok."));
		}
		UpdateElapsedUi();
	}
	m_stopping = FALSE;
	if (uiAlive) {
		// 録画終了後は再び Win+Shift+S でこの画面を撮れるようにする
		::SetWindowDisplayAffinity(m_hWnd, WDA_NONE);
		StartPeakMonitor();
		SyncLiveUiEnable();
	}
}

void CScreenCaptureDlg::StartPeakMonitor()
{
	if (m_peakThread || InterlockedCompareExchange(&m_peakRun, 0, 0) != 0)
		return;
	if (InterlockedCompareExchange(&m_run, 0, 0) != 0)
		return; // 録画中は CaptureThread がピーク更新
	InterlockedExchange(&m_peakStop, 0);
	InterlockedExchange(&m_peakRun, 0);
	uintptr_t th = _beginthreadex(NULL, 0, PeakMonitorThread, this, 0, NULL);
	if (!th) return;
	m_peakThread = (HANDLE)th;
}

void CScreenCaptureDlg::StopPeakMonitor()
{
	InterlockedExchange(&m_peakStop, 1);
	if (m_peakThread) {
		WaitForSingleObject(m_peakThread, 4000);
		CloseHandle(m_peakThread);
		m_peakThread = NULL;
	}
	InterlockedExchange(&m_peakRun, 0);
	InterlockedExchange(&m_peakStop, 0);
}

UINT __stdcall CScreenCaptureDlg::PeakMonitorThread(void* p)
{
	CScreenCaptureDlg* self = (CScreenCaptureDlg*)p;
	HRESULT hrCo = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	InterlockedExchange(&self->m_peakRun, 1);

	IMMDeviceEnumerator* enumer = NULL;
	IMMDevice* renderDev = NULL;
	IAudioClient* loopClient = NULL;
	IAudioCaptureClient* loopCap = NULL;
	WAVEFORMATEX* mixFmt = NULL;
	HANDLE hEvent = NULL;
	IMMDevice* micDev = NULL;
	IAudioClient* micClient = NULL;
	IAudioCaptureClient* micCap = NULL;
	WAVEFORMATEX* micFmt = NULL;
	HANDLE hMicEvent = NULL;

	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator), (void**)&enumer);
	if (FAILED(hr) || !enumer) goto peak_done;

	// プレビュー中: ループバックは常時。マイクは「マイク」チェックONのときだけ開く
	// (常時 eCapture はマイク付きPCでUI全体を重くする)
	if (savedata.loop_device[0]) {
		hr = enumer->GetDevice(savedata.loop_device, &renderDev);
		if (FAILED(hr) || !renderDev) {
			if (renderDev) { renderDev->Release(); renderDev = NULL; }
			hr = enumer->GetDefaultAudioEndpoint(eRender, eConsole, &renderDev);
		}
	} else {
		hr = enumer->GetDefaultAudioEndpoint(eRender, eConsole, &renderDev);
	}
	if (SUCCEEDED(hr) && renderDev)
		ScInitLoopbackCapture(renderDev, &loopClient, &loopCap, &mixFmt, &hEvent);

	if (savedata.cap_with_mic) {
		HRESULT hm = S_OK;
		if (savedata.mic_device[0]) {
			hm = enumer->GetDevice(savedata.mic_device, &micDev);
			if (FAILED(hm) || !micDev) {
				if (micDev) { micDev->Release(); micDev = NULL; }
				hm = enumer->GetDefaultAudioEndpoint(eCapture, eConsole, &micDev);
			}
		} else {
			hm = enumer->GetDefaultAudioEndpoint(eCapture, eConsole, &micDev);
		}
		if (SUCCEEDED(hm) && micDev) {
			hm = micDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&micClient);
			if (SUCCEEDED(hm) && micClient) {
				hm = micClient->GetMixFormat(&micFmt);
				if (SUCCEEDED(hm) && micFmt) {
					hMicEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
					if (hMicEvent) {
						hm = micClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
							AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
							2000000, 0, micFmt, NULL);
						if (SUCCEEDED(hm))
							hm = micClient->SetEventHandle(hMicEvent);
						if (SUCCEEDED(hm))
							hm = micClient->GetService(__uuidof(IAudioCaptureClient), (void**)&micCap);
						if (FAILED(hm) || !micCap) {
							if (micCap) { micCap->Release(); micCap = NULL; }
							micClient->Release(); micClient = NULL;
							if (micFmt) { CoTaskMemFree(micFmt); micFmt = NULL; }
							CloseHandle(hMicEvent); hMicEvent = NULL;
						}
					}
				}
			}
		}
	}

	if (loopClient) loopClient->Start();
	if (micClient) micClient->Start();

	while (InterlockedCompareExchange(&self->m_peakStop, 0, 0) == 0) {
		HANDLE waits[2];
		int nw = 0;
		if (hEvent) waits[nw++] = hEvent;
		if (hMicEvent) waits[nw++] = hMicEvent;
		if (nw > 0)
			WaitForMultipleObjects(nw, waits, FALSE, 50);
		else
			Sleep(30);

		if (micCap && micFmt) {
			UINT32 packet = 0;
			HRESULT hm = micCap->GetNextPacketSize(&packet);
			while (SUCCEEDED(hm) && packet > 0 && InterlockedCompareExchange(&self->m_peakStop, 0, 0) == 0) {
				BYTE* data = NULL;
				UINT32 frames = 0;
				DWORD flags = 0;
				hm = micCap->GetBuffer(&data, &frames, &flags, NULL, NULL);
				if (FAILED(hm)) break;
				if (frames > 0 && data && !(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
					float pk = 0.f;
					for (UINT32 i = 0; i < frames; ++i) {
						float L = 0.f, R = 0.f;
						ScSampleToFloat(data + i * micFmt->nBlockAlign, micFmt, L, R);
						const float aL = (L < 0.f) ? -L : L;
						const float aR = (R < 0.f) ? -R : R;
						if (aL > pk) pk = aL;
						if (aR > pk) pk = aR;
					}
					const LONG v = (LONG)(pk * 1000.f);
					LONG cur = InterlockedCompareExchange(&self->m_peakMic, 0, 0);
					if (v > cur) InterlockedExchange(&self->m_peakMic, v > 1000 ? 1000 : v);
				}
				micCap->ReleaseBuffer(frames);
				hm = micCap->GetNextPacketSize(&packet);
			}
		}

		if (loopCap && mixFmt) {
			UINT32 packet = 0;
			hr = loopCap->GetNextPacketSize(&packet);
			while (SUCCEEDED(hr) && packet > 0 && InterlockedCompareExchange(&self->m_peakStop, 0, 0) == 0) {
				BYTE* data = NULL;
				UINT32 frames = 0;
				DWORD flags = 0;
				hr = loopCap->GetBuffer(&data, &frames, &flags, NULL, NULL);
				if (FAILED(hr)) break;
				if (frames > 0) {
					float pk = 0.f;
					for (UINT32 i = 0; i < frames; ++i) {
						float L = 0.f, R = 0.f;
						if (data && !(flags & AUDCLNT_BUFFERFLAGS_SILENT))
							ScSampleToFloat(data + i * mixFmt->nBlockAlign, mixFmt, L, R);
						const float aL = (L < 0.f) ? -L : L;
						const float aR = (R < 0.f) ? -R : R;
						if (aL > pk) pk = aL;
						if (aR > pk) pk = aR;
					}
					const LONG v = (LONG)(pk * 1000.f);
					LONG cur = InterlockedCompareExchange(&self->m_peakSys, 0, 0);
					if (v > cur) InterlockedExchange(&self->m_peakSys, v > 1000 ? 1000 : v);
					LONG curP = InterlockedCompareExchange(&self->m_peakMix, 0, 0);
					if (v > curP) InterlockedExchange(&self->m_peakMix, v > 1000 ? 1000 : v);
				}
				loopCap->ReleaseBuffer(frames);
				hr = loopCap->GetNextPacketSize(&packet);
			}
		}
	}

	if (loopClient) loopClient->Stop();
	if (micClient) micClient->Stop();

peak_done:
	InterlockedExchange(&self->m_peakRun, 0);
	if (loopCap) loopCap->Release();
	if (loopClient) loopClient->Release();
	if (mixFmt) CoTaskMemFree(mixFmt);
	if (renderDev) renderDev->Release();
	if (micCap) micCap->Release();
	if (micClient) micClient->Release();
	if (micFmt) CoTaskMemFree(micFmt);
	if (micDev) micDev->Release();
	if (enumer) enumer->Release();
	if (hEvent) CloseHandle(hEvent);
	if (hMicEvent) CloseHandle(hMicEvent);
	if (SUCCEEDED(hrCo) || hrCo == S_FALSE) CoUninitialize();
	return 0;
}

UINT __stdcall CScreenCaptureDlg::CaptureThread(void* p)
{
	CScreenCaptureDlg* self = (CScreenCaptureDlg*)p;
	HRESULT hrCo = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
	timeBeginPeriod(1);

	IMFSinkWriter* writer = NULL;
	ScFrameBuf frame = {};
	IMMDeviceEnumerator* enumer = NULL;
	IMMDevice* renderDev = NULL;
	IAudioClient* loopClient = NULL;
	IAudioCaptureClient* loopCap = NULL;
	WAVEFORMATEX* mixFmt = NULL;
	HANDLE hEvent = NULL;
	IMMDevice* micDev = NULL;
	IAudioClient* micClient = NULL;
	IAudioCaptureClient* micCap = NULL;
	WAVEFORMATEX* micFmt = NULL;
	HANDLE hMicEvent = NULL;

	ComposeSnap snap = {};
	EnterCriticalSection(&self->m_snapCs);
	snap = self->m_recSnap;
	LeaveCriticalSection(&self->m_snapCs);

	const int fps = self->m_fpsVal > 0 ? self->m_fpsVal : 15;
	const LONGLONG frameDur = 10000000LL / fps;
	const BOOL wantAudio = self->m_withAudio;
	const BOOL wantMic = self->m_withMic;
	const DWORD outHz = 48000;
	const WORD outCh = 2;

	DWORD videoIdx = 0;
	DWORD audioIdx = 0;
	BOOL haveAudio = FALSE;
	BOOL videoNv12 = FALSE;
	BOOL videoRgbBottomUp = FALSE;
	LONGLONG videoRt = 0;
	LONGLONG audioRt = 0;
	HRESULT hr = S_OK;

	// ===== ライブ配信 (ffmpeg → RTMP)。MP4 / IMFSinkWriter は使わない =====
	if (self->m_liveMode) {
		CString rtmpUrl;
		CString rtmpKey;
		CString cmd;
		STARTUPINFO si = {};
		PROCESS_INFORMATION pi = {};
		HANDLE hVidPipe = INVALID_HANDLE_VALUE;
		HANDLE hAudPipe = INVALID_HANDLE_VALUE;
		HANDLE thAudFeed = NULL;
		BOOL haveFfmpegProc = FALSE;
		ScFrameBuf livePipe = {};
		int liveOutW = 1920, liveOutH = 1080;
		BOOL needLiveScale = FALSE;
		auto writeVid = [&](HANDLE h, const void* data, DWORD len) -> BOOL {
			const BYTE* p = (const BYTE*)data;
			DWORD left = len;
			DWORD spins = 0;
			while (left > 0) {
				DWORD wr = 0;
				if (!WriteFile(h, p, left, &wr, NULL)) {
					const DWORD err = GetLastError();
					if (err == ERROR_NO_DATA || err == ERROR_IO_PENDING) {
						if (haveFfmpegProc) {
							DWORD ffCode = STILL_ACTIVE;
							if (GetExitCodeProcess(pi.hProcess, &ffCode) && ffCode != STILL_ACTIVE) {
								InterlockedExchange(&self->m_ytLivePhase, 6);
								SetLastError(ffCode ? ffCode : ERROR_BROKEN_PIPE);
								return FALSE;
							}
						}
						Sleep(1);
						if (++spins > 2500) { // ~2.5s: 開始時 RTMP 待ちを超えたら失敗
							SetLastError(ERROR_TIMEOUT);
							return FALSE;
						}
						continue;
					}
					return FALSE;
				}
				if (wr == 0) {
					Sleep(1);
					if (++spins > 2500) {
						SetLastError(ERROR_TIMEOUT);
						return FALSE;
					}
					continue;
				}
				p += wr;
				left -= wr;
				spins = 0;
			}
			return TRUE;
		};
		TCHAR ffmpegPath[MAX_PATH] = {};
		if (!self->ResolveFfmpegPath(ffmpegPath, MAX_PATH)) {
			InterlockedExchange(&self->m_lastStage, 100);
			InterlockedExchange(&self->m_lastHr, HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
			goto done_live;
		}

		rtmpUrl = savedata.cap_live_url;
		rtmpKey = savedata.cap_live_key;
		rtmpUrl.Trim();
		rtmpKey.Trim();
		if (rtmpUrl.IsEmpty()) {
			InterlockedExchange(&self->m_lastStage, 104);
			InterlockedExchange(&self->m_lastHr, E_INVALIDARG);
			goto done_live;
		}
		if (!rtmpKey.IsEmpty() && rtmpUrl.Find(rtmpKey) < 0) {
			if (rtmpUrl.Right(1) != L"/")
				rtmpUrl += L"/";
			rtmpUrl += rtmpKey;
		}

		if (!ScComposeFrame(frame, snap, FALSE)) {
			if (!ScComposeFrame(frame, snap, TRUE)) {
				InterlockedExchange(&self->m_lastStage, 2);
				InterlockedExchange(&self->m_lastHr, E_FAIL);
				goto done_live;
			}
		}
		{
			int ew = frame.w & ~1, eh = frame.h & ~1;
			if (ew < 16) ew = 16;
			if (eh < 16) eh = 16;
			if (ew > 4096) ew = 4096;
			if (eh > 2304) eh = 2304;
			if (ew != frame.w || eh != frame.h) {
				ScFrameBuf scaled = {};
				if (ScFrameAlloc(scaled, ew, eh)) {
					SetStretchBltMode(scaled.hdc, COLORONCOLOR);
					StretchBlt(scaled.hdc, 0, 0, ew, eh, frame.hdc, 0, 0, frame.w, frame.h, SRCCOPY);
					ScFrameFree(frame);
					frame = scaled;
				} else {
					frame.w = ew;
					frame.h = eh;
				}
			}
		}

		// ライブ送出サイズ（キャンバス解像度、上限 4K。1080p30 以上も可）
		liveOutW = frame.w & ~1;
		liveOutH = frame.h & ~1;
		int vBitrate = 6000;
		{
			const int maxW = 3840, maxH = 2160;
			if (liveOutW > maxW || liveOutH > maxH) {
				int nw = liveOutW, nh = liveOutH;
				if (nw > maxW) { nh = (nh * maxW) / nw; nw = maxW; }
				if (nh > maxH) { nw = (nw * maxH) / nh; nh = maxH; }
				nw &= ~1; nh &= ~1;
				if (nw < 16) nw = 16;
				if (nh < 16) nh = 16;
				liveOutW = nw;
				liveOutH = nh;
			}
		}
		{
			const int pixels = liveOutW * liveOutH;
			int base = 2500;
			if (pixels >= 3840 * 2160) base = 20000;
			else if (pixels >= 2560 * 1440) base = 10000;
			else if (pixels >= 1920 * 1080) base = 6000;
			else if (pixels >= 1280 * 720) base = 4500;
			else base = 2500;
			if (fps > 30)
				vBitrate = (int)(((__int64)base * fps) / 30);
			else
				vBitrate = base;
			if (vBitrate < 1500) vBitrate = 1500;
			if (vBitrate > 51000) vBitrate = 51000;
		}
		needLiveScale = (liveOutW != frame.w || liveOutH != frame.h);
		if (needLiveScale) {
			if (!ScFrameAlloc(livePipe, liveOutW, liveOutH)) {
				InterlockedExchange(&self->m_lastStage, 2);
				InterlockedExchange(&self->m_lastHr, E_OUTOFMEMORY);
				goto done_live;
			}
		}
		const int gop = (fps > 0 ? fps : 30) * 2;

		TCHAR pipeVid[128] = {}, pipeAud[128] = {};
		const DWORD pipeTag = GetTickCount();
		_sntprintf_s(pipeVid, _TRUNCATE, L"\\\\.\\pipe\\oggscvid%lu%lu",
			(unsigned long)GetCurrentProcessId(), (unsigned long)pipeTag);
		_sntprintf_s(pipeAud, _TRUNCATE, L"\\\\.\\pipe\\oggscaud%lu%lu",
			(unsigned long)GetCurrentProcessId(), (unsigned long)pipeTag);

		// 1080p BGRA ≈8MB/frame。1MB だと1フレーム書き込み中に ffmpeg が止まると毎回ブロックする。
		// 2フレーム分を確保し、満杯時はフレームごとスキップして時計を止めない。
		DWORD vidPipeBytes = (DWORD)((__int64)liveOutW * (size_t)liveOutH * 4u * 2u);
		if (vidPipeBytes < (2u << 20)) vidPipeBytes = (2u << 20);
		if (vidPipeBytes > (48u << 20)) vidPipeBytes = (48u << 20);

		hVidPipe = CreateNamedPipe(pipeVid, PIPE_ACCESS_OUTBOUND,
			PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, vidPipeBytes, vidPipeBytes, 0, NULL);
		hAudPipe = CreateNamedPipe(pipeAud, PIPE_ACCESS_OUTBOUND,
			PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 1 << 18, 1 << 18, 0, NULL);
		if (hVidPipe == INVALID_HANDLE_VALUE || hAudPipe == INVALID_HANDLE_VALUE) {
			if (hVidPipe != INVALID_HANDLE_VALUE) { CloseHandle(hVidPipe); hVidPipe = INVALID_HANDLE_VALUE; }
			if (hAudPipe != INVALID_HANDLE_VALUE) { CloseHandle(hAudPipe); hAudPipe = INVALID_HANDLE_VALUE; }
			InterlockedExchange(&self->m_lastStage, 101);
			InterlockedExchange(&self->m_lastHr, HRESULT_FROM_WIN32(GetLastError()));
			goto done_live;
		}

		struct PipeConn { HANDLE h; BOOL ok; };
		PipeConn connV = { hVidPipe, FALSE };
		PipeConn connA = { hAudPipe, FALSE };
		struct PipeConnThunk {
			static DWORD WINAPI Run(LPVOID p) {
				PipeConn* c = (PipeConn*)p;
				c->ok = ConnectNamedPipe(c->h, NULL) || (GetLastError() == ERROR_PIPE_CONNECTED);
				return 0;
			}
		};
		HANDLE thV = CreateThread(NULL, 0, &PipeConnThunk::Run, &connV, 0, NULL);
		HANDLE thA = CreateThread(NULL, 0, &PipeConnThunk::Run, &connA, 0, NULL);
		Sleep(40);

		cmd.Format(
			L"\"%s\" -hide_banner -loglevel info -y "
			L"-fflags +nobuffer+genpts -flags low_delay "
			L"-thread_queue_size 512 -probesize 32 -analyzeduration 0 "
			L"-f rawvideo -pix_fmt bgra -s %dx%d -r %d -i \"%s\" "
			L"-thread_queue_size 512 -probesize 32 -analyzeduration 0 "
			L"-f s16le -ar %u -ac %u -i \"%s\" "
			L"-c:v libx264 -pix_fmt yuv420p -preset ultrafast -tune zerolatency -threads 0 "
			L"-bf 0 -g %d -keyint_min %d -sc_threshold 0 "
			L"-b:v %dk -maxrate %dk -bufsize %dk "
			L"-c:a aac -ar %u -b:a 128k -ac 2 "
			L"-f flv -flvflags no_duration_filesize \"%s\"",
			ffmpegPath, liveOutW, liveOutH, fps, pipeVid,
			(unsigned)outHz, (unsigned)outCh, pipeAud,
			gop, gop,
			vBitrate, vBitrate, vBitrate, // bufsize=1s 相当（*2 だと開始時 VBV で映像が詰まりやすい）
			(unsigned)outHz, (LPCTSTR)rtmpUrl);

		// exe 隣に残る ffmpeg-*.log（旧 -report）を掃除。詳細ログは %TEMP% のみ。
		{
			TCHAR mod[MAX_PATH] = {};
			if (GetModuleFileName(NULL, mod, MAX_PATH)) {
				TCHAR* slash = _tcsrchr(mod, L'\\');
				if (slash) {
					slash[1] = 0;
					TCHAR pattern[MAX_PATH] = {};
					_sntprintf_s(pattern, _TRUNCATE, L"%sffmpeg-*.log", mod);
					WIN32_FIND_DATA fd = {};
					HANDLE hFind = FindFirstFile(pattern, &fd);
					if (hFind != INVALID_HANDLE_VALUE) {
						do {
							if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
								TCHAR delPath[MAX_PATH] = {};
								_sntprintf_s(delPath, _TRUNCATE, L"%s%s", mod, fd.cFileName);
								DeleteFile(delPath);
							}
						} while (FindNextFile(hFind, &fd));
						FindClose(hFind);
					}
				}
			}
		}

		// ffmpeg ログ（接続失敗の切り分け用・%TEMP% のみ）
		TCHAR ffLog[MAX_PATH] = {};
		{
			TCHAR tmp[MAX_PATH] = {};
			GetTempPath(MAX_PATH, tmp);
			_sntprintf_s(ffLog, _TRUNCATE, L"%sogg_sc_ffmpeg.log", tmp);
			DeleteFile(ffLog);
		}
		SECURITY_ATTRIBUTES sa = {};
		sa.nLength = sizeof(sa);
		sa.bInheritHandle = TRUE;
		HANDLE hLog = CreateFile(ffLog, GENERIC_WRITE, FILE_SHARE_READ, &sa,
			CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_HIDE;
		BOOL procOk = FALSE;
		if (hLog != INVALID_HANDLE_VALUE) {
			HANDLE hNul = CreateFile(L"NUL", GENERIC_READ, FILE_SHARE_READ, &sa, OPEN_EXISTING, 0, NULL);
			si.dwFlags |= STARTF_USESTDHANDLES;
			si.hStdInput = (hNul != INVALID_HANDLE_VALUE) ? hNul : NULL;
			si.hStdOutput = hLog;
			si.hStdError = hLog;
			wchar_t* cmdBuf2 = _tcsdup(cmd);
			if (cmdBuf2) {
				procOk = CreateProcess(NULL, cmdBuf2, NULL, NULL, TRUE,
					CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
				free(cmdBuf2);
			}
			if (hNul != INVALID_HANDLE_VALUE) CloseHandle(hNul);
			CloseHandle(hLog);
			hLog = INVALID_HANDLE_VALUE;
		} else {
			wchar_t* cmdBuf = _tcsdup(cmd);
			if (cmdBuf) {
				procOk = CreateProcess(NULL, cmdBuf, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
				free(cmdBuf);
			}
		}
		if (!procOk) {
			if (thV) { WaitForSingleObject(thV, 100); CloseHandle(thV); thV = NULL; }
			if (thA) { WaitForSingleObject(thA, 100); CloseHandle(thA); thA = NULL; }
			CloseHandle(hVidPipe); hVidPipe = INVALID_HANDLE_VALUE;
			CloseHandle(hAudPipe); hAudPipe = INVALID_HANDLE_VALUE;
			InterlockedExchange(&self->m_lastStage, 102);
			InterlockedExchange(&self->m_lastHr, HRESULT_FROM_WIN32(GetLastError()));
			goto done_live;
		}
		haveFfmpegProc = TRUE;

		// ffmpeg は入力を順に open する。映像パイプ接続後すぐに映像を流し、
		// 音声 Connect は音声スレッド側で待つ（両方待ちはデッドロック→Enc0）。
		{
			TCHAR liveLog[MAX_PATH] = {};
			TCHAR tmpDir[MAX_PATH] = {};
			GetTempPath(MAX_PATH, tmpDir);
			_sntprintf_s(liveLog, _TRUNCATE, L"%sogg_sc_live.log", tmpDir);
			HANDLE hLiveLog = CreateFile(liveLog, GENERIC_WRITE, FILE_SHARE_READ, NULL,
				CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			auto liveLogLine = [&](const char* msg) {
				if (hLiveLog == INVALID_HANDLE_VALUE || !msg) return;
				DWORD wr = 0;
				WriteFile(hLiveLog, msg, (DWORD)strlen(msg), &wr, NULL);
				WriteFile(hLiveLog, "\r\n", 2, &wr, NULL);
				FlushFileBuffers(hLiveLog);
			};
			liveLogLine("wait video ConnectNamedPipe");
			DWORD wrV = thV ? WaitForSingleObject(thV, 15000) : WAIT_FAILED;
			if (thV) { CloseHandle(thV); thV = NULL; }
			if (wrV != WAIT_OBJECT_0 || !connV.ok) {
				liveLogLine("FAIL video connect");
				if (hLiveLog != INVALID_HANDLE_VALUE) { CloseHandle(hLiveLog); hLiveLog = INVALID_HANDLE_VALUE; }
				if (thA) { WaitForSingleObject(thA, 100); CloseHandle(thA); thA = NULL; }
				InterlockedExchange(&self->m_lastStage, 101);
				InterlockedExchange(&self->m_lastHr, HRESULT_FROM_WIN32(ERROR_PIPE_NOT_CONNECTED));
				TerminateProcess(pi.hProcess, 1);
				CloseHandle(pi.hThread);
				CloseHandle(pi.hProcess);
				haveFfmpegProc = FALSE;
				CloseHandle(hVidPipe); hVidPipe = INVALID_HANDLE_VALUE;
				CloseHandle(hAudPipe); hAudPipe = INVALID_HANDLE_VALUE;
				goto done_live;
			}
			liveLogLine("video connected OK; start audio thread (audio connect deferred)");
			if (hLiveLog != INVALID_HANDLE_VALUE) { CloseHandle(hLiveLog); hLiveLog = INVALID_HANDLE_VALUE; }
		}


		// 音声は専用スレッドでパイプへ供給（映像スレッドと分離し ffmpeg 二重入力デッドロックを防ぐ）
		struct LiveAudFeed {
			CScreenCaptureDlg* self;
			HANDLE hPipe;
			HANDLE hFfmpeg;
			HANDLE thConn; // audio ConnectNamedPipe thread
			PipeConn* conn; // audio connect result
			DWORD outHz;
			WORD outCh;
			BOOL wantLoop;
			volatile LONG fail;
			static DWORD WINAPI Run(LPVOID p) {
				LiveAudFeed* c = (LiveAudFeed*)p;
				if (!c || c->hPipe == INVALID_HANDLE_VALUE) return 0;
				// 映像が流れ始めたあと ffmpeg が音声パイプを開くまで待つ
				if (c->thConn) {
					const DWORD wr = WaitForSingleObject(c->thConn, 20000);
					CloseHandle(c->thConn);
					c->thConn = NULL;
					if (wr != WAIT_OBJECT_0 || !c->conn || !c->conn->ok) {
						InterlockedExchange(&c->fail, 1);
						return 0;
					}
				}
				HRESULT hrCo = CoInitializeEx(NULL, COINIT_MULTITHREADED);
				IMMDeviceEnumerator* enumer = NULL;
				IMMDevice* renderDev = NULL;
				IAudioClient* loopClient = NULL;
				IAudioCaptureClient* loopCap = NULL;
				WAVEFORMATEX* mixFmt = NULL;
				HANDLE hEvent = NULL;
				BOOL haveLoop = FALSE;
				if (c->wantLoop) {
					HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
						__uuidof(IMMDeviceEnumerator), (void**)&enumer);
					if (SUCCEEDED(hr) && enumer) {
						if (savedata.loop_device[0]) {
							hr = enumer->GetDevice(savedata.loop_device, &renderDev);
							if (FAILED(hr) || !renderDev) {
								if (renderDev) { renderDev->Release(); renderDev = NULL; }
								hr = enumer->GetDefaultAudioEndpoint(eRender, eConsole, &renderDev);
							}
						} else {
							hr = enumer->GetDefaultAudioEndpoint(eRender, eConsole, &renderDev);
						}
						if (SUCCEEDED(hr) && renderDev) {
							hr = ScInitLoopbackCapture(renderDev, &loopClient, &loopCap, &mixFmt, &hEvent);
							if (SUCCEEDED(hr) && loopClient && loopCap && mixFmt)
								haveLoop = TRUE;
						}
					}
					if (haveLoop && loopClient)
						loopClient->Start();
				}
				const DWORD srcHz = (mixFmt && mixFmt->nSamplesPerSec) ? mixFmt->nSamplesPerSec : c->outHz;
				LARGE_INTEGER qpf, qpc0;
				QueryPerformanceFrequency(&qpf);
				QueryPerformanceCounter(&qpc0);
				const LONGLONG qpf64 = (qpf.QuadPart > 0) ? qpf.QuadPart : 1;
				LONGLONG written = 0;
				short silence[4800 * 2];
				memset(silence, 0, sizeof(silence));
				auto writeBlk = [&](const void* data, DWORD len) -> BOOL {
					const BYTE* pb = (const BYTE*)data;
					DWORD left = len;
					while (left > 0) {
						if (InterlockedCompareExchange(&c->self->m_stop, 0, 0) != 0)
							return FALSE;
						DWORD wr = 0;
						if (!WriteFile(c->hPipe, pb, left, &wr, NULL)) {
							const DWORD err = GetLastError();
							if (c->hFfmpeg) {
								DWORD ffCode = STILL_ACTIVE;
								if (GetExitCodeProcess(c->hFfmpeg, &ffCode) && ffCode != STILL_ACTIVE) {
									InterlockedExchange(&c->self->m_ytLivePhase, 6);
									InterlockedExchange(&c->fail, 1);
									return FALSE;
								}
							}
							if (err == ERROR_BROKEN_PIPE || err == ERROR_NO_DATA) {
								InterlockedExchange(&c->fail, 1);
								return FALSE;
							}
							Sleep(1);
							continue;
						}
						if (wr == 0) { Sleep(1); continue; }
						pb += wr;
						left -= wr;
					}
					return TRUE;
				};
				auto pushSilence = [&](int nFrames) -> BOOL {
					while (nFrames > 0) {
						int n = nFrames;
						if (n > 4800) n = 4800;
						if (!writeBlk(silence, (DWORD)(n * c->outCh * sizeof(short))))
							return FALSE;
						written += n;
						nFrames -= n;
					}
					return TRUE;
				};
				while (InterlockedCompareExchange(&c->self->m_stop, 0, 0) == 0
					&& InterlockedCompareExchange(&c->fail, 0, 0) == 0) {
					LARGE_INTEGER qpc;
					QueryPerformanceCounter(&qpc);
					const LONGLONG elapsedQ = qpc.QuadPart - qpc0.QuadPart;
					const LONGLONG target = (elapsedQ * (LONGLONG)c->outHz) / qpf64;
					if (written >= target + (LONGLONG)c->outHz / 5) {
						if (hEvent)
							WaitForSingleObject(hEvent, 5);
						else
							Sleep(1);
						continue;
					}
					BOOL wroteReal = FALSE;
					if (haveLoop && loopCap && mixFmt) {
						UINT32 packet = 0;
						HRESULT ha = loopCap->GetNextPacketSize(&packet);
						while (SUCCEEDED(ha) && packet > 0
							&& InterlockedCompareExchange(&c->self->m_stop, 0, 0) == 0
							&& written < target + (LONGLONG)c->outHz / 5) {
							BYTE* data = NULL;
							UINT32 frames = 0;
							DWORD flags = 0;
							ha = loopCap->GetBuffer(&data, &frames, &flags, NULL, NULL);
							if (FAILED(ha)) break;
							if (frames > 0) {
								float fL[4096], fR[4096];
								short pcm[8192 * 2];
								UINT32 done = 0;
								float pk = 0.f;
								while (done < frames) {
									UINT32 n = frames - done;
									if (n > 4096) n = 4096;
									for (UINT32 ii = 0; ii < n; ++ii) {
										float L = 0.f, R = 0.f;
										if (data && !(flags & AUDCLNT_BUFFERFLAGS_SILENT))
											ScSampleToFloat(data + (done + ii) * mixFmt->nBlockAlign, mixFmt, L, R);
										fL[ii] = L; fR[ii] = R;
										const float aL = (L < 0.f) ? -L : L;
										const float aR = (R < 0.f) ? -R : R;
										if (aL > pk) pk = aL;
										if (aR > pk) pk = aR;
									}
									int outFrames = (int)(((__int64)n * (int)c->outHz) / (srcHz ? (int)srcHz : (int)c->outHz));
									if (outFrames < 1 && n > 0) outFrames = 1;
									if (outFrames > 8192) outFrames = 8192;
									if (srcHz == c->outHz) {
										for (int o = 0; o < outFrames; ++o) {
											float L = ScClamp1(fL[o]);
											float R = ScClamp1(fR[o]);
											pcm[o * 2 + 0] = (short)(L * 32767.f);
											pcm[o * 2 + 1] = (short)(R * 32767.f);
										}
									} else {
										for (int o = 0; o < outFrames; ++o) {
											double srcPos = ((double)o * (double)srcHz / (double)c->outHz);
											int i0 = (int)srcPos;
											int i1 = i0 + 1;
											if (i0 < 0) i0 = 0;
											if (i0 >= (int)n) i0 = (int)n - 1;
											if (i1 >= (int)n) i1 = (int)n - 1;
											const float frac = (float)(srcPos - (double)i0);
											float L = fL[i0] + (fL[i1] - fL[i0]) * frac;
											float R = fR[i0] + (fR[i1] - fR[i0]) * frac;
											L = ScClamp1(L); R = ScClamp1(R);
											pcm[o * 2 + 0] = (short)(L * 32767.f);
											pcm[o * 2 + 1] = (short)(R * 32767.f);
										}
									}
									if (!writeBlk(pcm, (DWORD)(outFrames * c->outCh * sizeof(short)))) {
										loopCap->ReleaseBuffer(frames);
										goto aud_done;
									}
									written += outFrames;
									wroteReal = TRUE;
									done += n;
								}
								if (pk > 0.f) {
									const LONG v = (LONG)(pk * 1000.f);
									LONG cur = InterlockedCompareExchange(&c->self->m_peakSys, 0, 0);
									if (v > cur) InterlockedExchange(&c->self->m_peakSys, v > 1000 ? 1000 : v);
									LONG curP = InterlockedCompareExchange(&c->self->m_peakMix, 0, 0);
									if (v > curP) InterlockedExchange(&c->self->m_peakMix, v > 1000 ? 1000 : v);
								}
							}
							loopCap->ReleaseBuffer(frames);
							ha = loopCap->GetNextPacketSize(&packet);
						}
					}
					if (!wroteReal && written < target) {
						const int need = (int)(target - written);
						if (need > 0 && !pushSilence(need))
							break;
					}
					if (hEvent)
						WaitForSingleObject(hEvent, 2);
					else
						Sleep(1);
				}
			aud_done:
				if (loopClient) loopClient->Stop();
				if (loopCap) loopCap->Release();
				if (loopClient) loopClient->Release();
				if (mixFmt) CoTaskMemFree(mixFmt);
				if (renderDev) renderDev->Release();
				if (enumer) enumer->Release();
				if (hEvent) CloseHandle(hEvent);
				if (SUCCEEDED(hrCo) || hrCo == S_FALSE) CoUninitialize();
				return 0;
			}
		};

		LiveAudFeed audFeed = {};
		audFeed.self = self;
		audFeed.hPipe = hAudPipe;
		audFeed.hFfmpeg = pi.hProcess;
		audFeed.thConn = thA; // ownership moves to audio thread
		thA = NULL;
		audFeed.conn = &connA;
		audFeed.outHz = outHz;
		audFeed.outCh = outCh;
		audFeed.wantLoop = wantAudio ? TRUE : FALSE;
		audFeed.fail = 0;
		thAudFeed = CreateThread(NULL, 0, &LiveAudFeed::Run, &audFeed, 0, NULL);
		if (!thAudFeed) {
			if (audFeed.thConn) { WaitForSingleObject(audFeed.thConn, 100); CloseHandle(audFeed.thConn); audFeed.thConn = NULL; }
			InterlockedExchange(&self->m_lastStage, 103);
			InterlockedExchange(&self->m_lastHr, HRESULT_FROM_WIN32(GetLastError()));
			goto done_live;
		}
		// 映像は待たず即ループへ（ffmpeg の video probe を先に満たす）

		{
			LARGE_INTEGER qpf, qpc0;
			QueryPerformanceFrequency(&qpf);
			QueryPerformanceCounter(&qpc0);
			LONGLONG nextFrameQpc = 0;
			BOOL writeFail = FALSE;
			const LONGLONG qpf64 = (qpf.QuadPart > 0) ? qpf.QuadPart : 1;
			DWORD encWinTick = GetTickCount();
			int encWinCnt = 0;
			LONGLONG periodQ = qpf64 / (fps > 0 ? fps : 30);
			if (periodQ < 1) periodQ = 1;
			DWORD lastYtKickTick = 0;

			while (InterlockedCompareExchange(&self->m_stop, 0, 0) == 0 && !writeFail
				&& InterlockedCompareExchange(&audFeed.fail, 0, 0) == 0) {
				LARGE_INTEGER qpc;
				QueryPerformanceCounter(&qpc);
				const LONGLONG elapsedQ = qpc.QuadPart - qpc0.QuadPart;
				if (elapsedQ < nextFrameQpc) {
					Sleep(1);
					continue;
				}
				if (nextFrameQpc + periodQ < elapsedQ)
					nextFrameQpc = elapsedQ;
				snap.fxTime = (float)((elapsedQ * 1000.0) / (double)qpf64) * 0.001f;
				BOOL haveFrame = ScComposeFrame(frame, snap, FALSE);
				if (!haveFrame)
					haveFrame = ScComposeFrame(frame, snap, TRUE);
				if (!haveFrame) {
					if (ScFrameAlloc(frame, snap.canvasW, snap.canvasH)) {
						ScFrameClear(frame, RGB(16, 16, 20));
						if (snap.fxN > 0 && frame.bits)
							ScGpuApplyEffectChain(frame.bits, frame.w, frame.h, frame.stride,
								snap.fx, snap.fxN, snap.fxTime, snap.fxStr);
						haveFrame = TRUE;
					}
				}
				if (haveFrame && frame.bits) {
					const BYTE* writeBits = frame.bits;
					int writeW = frame.w;
					int writeH = frame.h;
					int writeStride = frame.stride > 0 ? frame.stride : (frame.w * 4);
					if (needLiveScale && livePipe.bits && livePipe.hdc && frame.hdc) {
						SetStretchBltMode(livePipe.hdc, COLORONCOLOR);
						StretchBlt(livePipe.hdc, 0, 0, liveOutW, liveOutH,
							frame.hdc, 0, 0, frame.w, frame.h, SRCCOPY);
						writeBits = livePipe.bits;
						writeW = liveOutW;
						writeH = liveOutH;
						writeStride = liveOutW * 4;
					}
					// パイプ書き込みより先にプレビュー更新（RTMP/エンコ待ちで WriteFile が
					// 止まってもローカル映像は進む）
					const LONG fc = InterlockedIncrement(&self->m_frameCnt);
					if ((fc % 2) == 0 && self->m_snapCsInit) {
						EnterCriticalSection(&self->m_snapCs);
						if (self->m_cacheDc && frame.hdc && self->m_cacheW > 0 && self->m_cacheH > 0) {
							SetStretchBltMode(self->m_cacheDc, COLORONCOLOR);
							StretchBlt(self->m_cacheDc, 0, 0, self->m_cacheW, self->m_cacheH,
								frame.hdc, 0, 0, frame.w, frame.h, SRCCOPY);
						}
						LeaveCriticalSection(&self->m_snapCs);
					}
					// パイプに1フレーム以上残っている＝ffmpeg が追いついていない → 丸ごとスキップ
					// （途中放棄は rawvideo 破壊になるので書込前判定のみ）
					BOOL skipVid = FALSE;
					{
						DWORD pipeBytes = 0;
						const DWORD frameBytes = (DWORD)((__int64)writeW * (size_t)writeH * 4u);
						if (frameBytes > 0 && PeekNamedPipe(hVidPipe, NULL, 0, NULL, &pipeBytes, NULL)
							&& pipeBytes >= frameBytes)
							skipVid = TRUE;
					}
					if (!skipVid) {
						if (writeStride == writeW * 4) {
							if (!writeVid(hVidPipe, writeBits, (DWORD)(writeStride * writeH))) {
								InterlockedExchange(&self->m_lastStage, 103);
								InterlockedExchange(&self->m_lastHr, HRESULT_FROM_WIN32(GetLastError()));
								writeFail = TRUE;
								break;
							}
						} else {
							for (int y = 0; y < writeH && !writeFail; ++y) {
								if (!writeVid(hVidPipe, writeBits + (size_t)y * (size_t)writeStride, (DWORD)(writeW * 4))) {
									InterlockedExchange(&self->m_lastStage, 103);
									InterlockedExchange(&self->m_lastHr, HRESULT_FROM_WIN32(GetLastError()));
									writeFail = TRUE;
								}
							}
							if (writeFail) break;
						}
					}

					if (haveFfmpegProc && (fc % (fps > 0 ? fps : 30)) == 0) {
						DWORD ffCode = STILL_ACTIVE;
						if (GetExitCodeProcess(pi.hProcess, &ffCode) && ffCode != STILL_ACTIVE) {
							InterlockedExchange(&self->m_lastStage, 105);
							InterlockedExchange(&self->m_lastHr, HRESULT_FROM_WIN32(ffCode ? ffCode : ERROR_BROKEN_PIPE));
							InterlockedExchange(&self->m_ytLivePhase, 6);
							writeFail = TRUE;
							break;
						}
					}
					if (self->m_liveService == 0 && !self->m_ytLiveTransitionDone
						&& fc >= (LONG)(fps > 0 ? fps : 30)) {
						const DWORD nowKick = GetTickCount();
						if (lastYtKickTick == 0 || (nowKick - lastYtKickTick) >= 2000) {
							lastYtKickTick = nowKick;
							InterlockedExchange(&self->m_ytGoLiveRequest, 1);
						}
					}
					{
						encWinCnt++;
						const DWORD nowEnc = GetTickCount();
						const DWORD elEnc = nowEnc - encWinTick;
						if (elEnc >= 800) {
							const LONG x10 = (LONG)((encWinCnt * 10000.0) / (double)elEnc + 0.5);
							InterlockedExchange(&self->m_encFpsX10, x10);
							encWinTick = nowEnc;
							encWinCnt = 0;
						}
					}
				}
				nextFrameQpc += periodQ;
			}
			if (InterlockedCompareExchange(&audFeed.fail, 0, 0) != 0 && !writeFail) {
				InterlockedExchange(&self->m_lastStage, 103);
				InterlockedExchange(&self->m_lastHr, HRESULT_FROM_WIN32(ERROR_BROKEN_PIPE));
			}
		}

		InterlockedExchange(&self->m_stop, 1);

		if (hVidPipe != INVALID_HANDLE_VALUE) {
			FlushFileBuffers(hVidPipe);
			DisconnectNamedPipe(hVidPipe);
			CloseHandle(hVidPipe);
			hVidPipe = INVALID_HANDLE_VALUE;
		}
		if (hAudPipe != INVALID_HANDLE_VALUE) {
			FlushFileBuffers(hAudPipe);
			DisconnectNamedPipe(hAudPipe);
			CloseHandle(hAudPipe);
			hAudPipe = INVALID_HANDLE_VALUE;
		}
		if (haveFfmpegProc) {
			WaitForSingleObject(pi.hProcess, 8000);
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
			haveFfmpegProc = FALSE;
		}

	done_live:
		if (thAudFeed) {
			InterlockedExchange(&self->m_stop, 1);
			WaitForSingleObject(thAudFeed, 5000);
			CloseHandle(thAudFeed);
			thAudFeed = NULL;
		}
		ScFrameFree(livePipe);
		if (hVidPipe != INVALID_HANDLE_VALUE) { CloseHandle(hVidPipe); hVidPipe = INVALID_HANDLE_VALUE; }
		if (hAudPipe != INVALID_HANDLE_VALUE) { CloseHandle(hAudPipe); hAudPipe = INVALID_HANDLE_VALUE; }
		if (haveFfmpegProc) {
			TerminateProcess(pi.hProcess, 1);
			WaitForSingleObject(pi.hProcess, 3000);
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
			haveFfmpegProc = FALSE;
		}
		if (micClient) micClient->Stop();
		if (loopClient) loopClient->Stop();
		if (micCap) micCap->Release();
		if (micClient) micClient->Release();
		if (micFmt) CoTaskMemFree(micFmt);
		if (micDev) micDev->Release();
		if (hMicEvent) CloseHandle(hMicEvent);
		if (loopCap) loopCap->Release();
		if (loopClient) loopClient->Release();
		if (mixFmt) CoTaskMemFree(mixFmt);
		if (renderDev) renderDev->Release();
		if (enumer) enumer->Release();
		if (hEvent) CloseHandle(hEvent);
		ScFrameFree(frame);
		ScReleaseReuseBuf();
		ScGdiReleaseScreenDc();
		timeEndPeriod(1);
		if (SUCCEEDED(hrCo)) CoUninitialize();
		InterlockedExchange(&self->m_encodeGdi, 0);
		InterlockedExchange(&self->m_run, 0);
		return 0;
	}

	hr = MFStartup(MF_VERSION);
	if (FAILED(hr)) {
		InterlockedExchange(&self->m_lastStage, 1);
		InterlockedExchange(&self->m_lastHr, hr);
		goto done;
	}

	if (!ScComposeFrame(frame, snap, FALSE)) {
		if (!ScComposeFrame(frame, snap, TRUE)) {
			InterlockedExchange(&self->m_lastStage, 2);
			InterlockedExchange(&self->m_lastHr, E_FAIL);
			goto done;
		}
	}
	// エンコードに渡す解像度を偶数＆上限で正規化（H.264制約）
	{
		int ew = frame.w & ~1, eh = frame.h & ~1;
		if (ew < 16) ew = 16;
		if (eh < 16) eh = 16;
		if (ew > 4096) ew = 4096;
		if (eh > 2304) eh = 2304;
		if (ew != frame.w || eh != frame.h) {
			ScFrameBuf scaled = {};
			if (ScFrameAlloc(scaled, ew, eh)) {
				SetStretchBltMode(scaled.hdc, COLORONCOLOR);
				StretchBlt(scaled.hdc, 0, 0, ew, eh, frame.hdc, 0, 0, frame.w, frame.h, SRCCOPY);
				ScFrameFree(frame);
				frame = scaled;
			} else {
				frame.w = ew;
				frame.h = eh;
			}
		}
	}

	{
		int vstep = 0;
		int okAttempt = -1;
		HRESULT lastVideoHr = E_FAIL;
		// HW エンコード優先（SW は 1080p30 で足りないことが多い）
		for (int hwPass = 0; okAttempt < 0 && hwPass < 2; ++hwPass) {
			const BOOL useHw = (hwPass == 0) ? TRUE : FALSE;
			for (int attempt = 0; attempt < 5; ++attempt) {
				haveAudio = FALSE;
				hr = ScRebuildVideoOnlyWriter(self->m_outPath, frame.w, frame.h, fps,
					&writer, &videoIdx, &videoNv12, &videoRgbBottomUp, useHw, &vstep, attempt);
				if (SUCCEEDED(hr)) { okAttempt = attempt; break; }
				lastVideoHr = hr;
			}
		}
		// 高解像度拒否向け縮小リトライ（attempt0=公式RGB32 を優先）
		const int kMaxList[][2] = { {1280, 720}, {854, 480} };
		for (int i = 0; okAttempt < 0 && i < 2; ++i) {
			if (frame.w <= kMaxList[i][0] && frame.h <= kMaxList[i][1])
				continue;
			if (!ScScaleFrameTo(frame, kMaxList[i][0], kMaxList[i][1]))
				break;
			for (int attempt = 0; attempt < 5; ++attempt) {
				haveAudio = FALSE;
				hr = ScRebuildVideoOnlyWriter(self->m_outPath, frame.w, frame.h, fps,
					&writer, &videoIdx, &videoNv12, &videoRgbBottomUp, FALSE, &vstep, attempt);
				if (SUCCEEDED(hr)) { okAttempt = attempt; break; }
				lastVideoHr = hr;
			}
		}
		if (okAttempt < 0) {
			InterlockedExchange(&self->m_lastStage, (vstep == 1) ? 40 : (vstep == 2) ? 41 : 4);
			InterlockedExchange(&self->m_lastHr, lastVideoHr);
			goto done;
		}
		hr = S_OK;
	}

	if (wantAudio) {
		hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
			__uuidof(IMMDeviceEnumerator), (void**)&enumer);
		if (SUCCEEDED(hr) && enumer) {
			if (savedata.loop_device[0]) {
				hr = enumer->GetDevice(savedata.loop_device, &renderDev);
				if (FAILED(hr) || !renderDev) {
					if (renderDev) { renderDev->Release(); renderDev = NULL; }
					hr = enumer->GetDefaultAudioEndpoint(eRender, eConsole, &renderDev);
				}
			} else {
				hr = enumer->GetDefaultAudioEndpoint(eRender, eConsole, &renderDev);
			}
			if (SUCCEEDED(hr) && renderDev) {
				hr = ScInitLoopbackCapture(renderDev, &loopClient, &loopCap, &mixFmt, &hEvent);
				if (SUCCEEDED(hr) && loopClient && loopCap && mixFmt) {
					hr = ScAddAudioStream(writer, &audioIdx, outHz, outCh);
					if (SUCCEEDED(hr)) {
						haveAudio = TRUE;
					} else {
						// AddStream 後の失敗で orphan AAC が残ると BeginWriting が 0xC00D36B4 になる
						HRESULT hv = ScRebuildVideoOnlyWriter(self->m_outPath, frame.w, frame.h, fps,
							&writer, &videoIdx, &videoNv12, &videoRgbBottomUp, FALSE, NULL, 0);
						haveAudio = FALSE;
						audioIdx = 0;
						if (FAILED(hv)) {
							InterlockedExchange(&self->m_lastStage, 5);
							InterlockedExchange(&self->m_lastHr, hr);
							goto done;
						}
						hr = S_OK;
					}
				}
			}
		}
	}

	hr = writer->BeginWriting();
	if (FAILED(hr) && haveAudio) {
		// 映像+AAC の組み合わせ拒否 → 映像のみで再構築
		const HRESULT beginHr = hr;
		if (loopClient) { loopClient->Stop(); }
		if (loopCap) { loopCap->Release(); loopCap = NULL; }
		if (loopClient) { loopClient->Release(); loopClient = NULL; }
		if (mixFmt) { CoTaskMemFree(mixFmt); mixFmt = NULL; }
		if (hEvent) { CloseHandle(hEvent); hEvent = NULL; }
		haveAudio = FALSE;
		audioIdx = 0;
		hr = ScRebuildVideoOnlyWriter(self->m_outPath, frame.w, frame.h, fps,
			&writer, &videoIdx, &videoNv12, &videoRgbBottomUp, FALSE, NULL, 0);
		if (SUCCEEDED(hr))
			hr = writer->BeginWriting();
		if (FAILED(hr))
			hr = beginHr;
	}
	if (FAILED(hr)) {
		InterlockedExchange(&self->m_lastStage, 6);
		InterlockedExchange(&self->m_lastHr, hr);
		goto done;
	}

	if (haveAudio && loopClient)
		loopClient->Start();

	if (haveAudio && wantMic && enumer) {
		ScMicEnsureCs();
		InterlockedExchange(&s_micW, 0);
		InterlockedExchange(&s_micR, 0);
		HRESULT hm = S_OK;
		if (savedata.mic_device[0]) {
			hm = enumer->GetDevice(savedata.mic_device, &micDev);
			if (FAILED(hm) || !micDev) {
				if (micDev) { micDev->Release(); micDev = NULL; }
				hm = enumer->GetDefaultAudioEndpoint(eCapture, eConsole, &micDev);
			}
		} else {
			hm = enumer->GetDefaultAudioEndpoint(eCapture, eConsole, &micDev);
		}
		if (SUCCEEDED(hm) && micDev) {
			hm = micDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&micClient);
			if (SUCCEEDED(hm) && micClient) {
				hm = micClient->GetMixFormat(&micFmt);
				if (SUCCEEDED(hm) && micFmt) {
					BOOL micOk = FALSE;
					hMicEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
					if (hMicEvent) {
						hm = micClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
							AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
							2000000, 0, micFmt, NULL);
						if (SUCCEEDED(hm))
							hm = micClient->SetEventHandle(hMicEvent);
						if (SUCCEEDED(hm)) {
							hm = micClient->GetService(__uuidof(IAudioCaptureClient), (void**)&micCap);
							if (SUCCEEDED(hm) && micCap) micOk = TRUE;
						}
					}
					if (!micOk) {
						if (hMicEvent) { CloseHandle(hMicEvent); hMicEvent = NULL; }
						if (micCap) { micCap->Release(); micCap = NULL; }
						if (micClient) { micClient->Release(); micClient = NULL; }
						if (micFmt) { CoTaskMemFree(micFmt); micFmt = NULL; }
						hm = micDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&micClient);
						if (SUCCEEDED(hm) && micClient) {
							hm = micClient->GetMixFormat(&micFmt);
							if (SUCCEEDED(hm) && micFmt) {
								hm = micClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
									AUDCLNT_STREAMFLAGS_NOPERSIST,
									2000000, 0, micFmt, NULL);
								if (SUCCEEDED(hm)) {
									hm = micClient->GetService(__uuidof(IAudioCaptureClient), (void**)&micCap);
									if (SUCCEEDED(hm) && micCap) micOk = TRUE;
								}
							}
						}
					}
					if (micOk && micCap && micFmt) {
						InterlockedExchange(&s_micCapRate, (LONG)micFmt->nSamplesPerSec);
						micClient->Start();
					} else {
						if (micCap) { micCap->Release(); micCap = NULL; }
						if (micClient) { micClient->Release(); micClient = NULL; }
						if (micFmt) { CoTaskMemFree(micFmt); micFmt = NULL; }
						if (hMicEvent) { CloseHandle(hMicEvent); hMicEvent = NULL; }
					}
				}
			}
		}
	}

	{
		const DWORD srcHz = (mixFmt && mixFmt->nSamplesPerSec) ? mixFmt->nSamplesPerSec : outHz;
		LARGE_INTEGER qpf, qpc0;
		QueryPerformanceFrequency(&qpf);
		QueryPerformanceCounter(&qpc0);
		LONGLONG nextFrameQpc = 0;
		BOOL writeFail = FALSE;
		const LONGLONG qpf64 = (qpf.QuadPart > 0) ? qpf.QuadPart : 1;

		// 映像合成の前後で必ず音声を排出（4Kで合成が重いと音が欠ける主因）
		auto drainAudio = [&]() {
			if (!haveAudio || !loopCap || !mixFmt || writeFail) return;
			if (micCap && micFmt) {
				UINT32 micPkt = 0;
				HRESULT hm = micCap->GetNextPacketSize(&micPkt);
				while (SUCCEEDED(hm) && micPkt > 0 && InterlockedCompareExchange(&self->m_stop, 0, 0) == 0) {
					BYTE* data = NULL;
					UINT32 frames = 0;
					DWORD flags = 0;
					hm = micCap->GetBuffer(&data, &frames, &flags, NULL, NULL);
					if (FAILED(hm)) break;
					if (frames > 0 && data && !(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
						float conv[4096 * 2];
						UINT32 done = 0;
						while (done < frames) {
							UINT32 n = frames - done;
							if (n > 4096) n = 4096;
							float pk = 0.f;
							for (UINT32 i = 0; i < n; ++i) {
								float L, R;
								ScSampleToFloat(data + (done + i) * micFmt->nBlockAlign, micFmt, L, R);
								conv[i * 2 + 0] = L;
								conv[i * 2 + 1] = R;
								const float aL = (L < 0.f) ? -L : L;
								const float aR = (R < 0.f) ? -R : R;
								if (aL > pk) pk = aL;
								if (aR > pk) pk = aR;
							}
							{
								const LONG v = (LONG)(pk * 1000.f);
								LONG cur = InterlockedCompareExchange(&self->m_peakMic, 0, 0);
								if (v > cur) InterlockedExchange(&self->m_peakMic, v > 1000 ? 1000 : v);
							}
							ScMicRingWrite(conv, (int)n);
							done += n;
						}
					}
					micCap->ReleaseBuffer(frames);
					hm = micCap->GetNextPacketSize(&micPkt);
				}
			}

			UINT32 packet = 0;
			hr = loopCap->GetNextPacketSize(&packet);
			while (SUCCEEDED(hr) && packet > 0 && InterlockedCompareExchange(&self->m_stop, 0, 0) == 0) {
				BYTE* data = NULL;
				UINT32 frames = 0;
				DWORD flags = 0;
				hr = loopCap->GetBuffer(&data, &frames, &flags, NULL, NULL);
				if (FAILED(hr)) break;
				if (frames > 0) {
					float fL[4096], fR[4096];
					short pcm[8192 * 2];
					UINT32 done = 0;
					while (done < frames) {
						UINT32 n = frames - done;
						if (n > 4096) n = 4096;
						float pkSys = 0.f;
						for (UINT32 i = 0; i < n; ++i) {
							float L = 0.f, R = 0.f;
							if (data && !(flags & AUDCLNT_BUFFERFLAGS_SILENT))
								ScSampleToFloat(data + (done + i) * mixFmt->nBlockAlign, mixFmt, L, R);
							fL[i] = L;
							fR[i] = R;
							const float aL = (L < 0.f) ? -L : L;
							const float aR = (R < 0.f) ? -R : R;
							if (aL > pkSys) pkSys = aL;
							if (aR > pkSys) pkSys = aR;
						}
						{
							const LONG v = (LONG)(pkSys * 1000.f);
							LONG cur = InterlockedCompareExchange(&self->m_peakSys, 0, 0);
							if (v > cur) InterlockedExchange(&self->m_peakSys, v > 1000 ? 1000 : v);
							LONG curP = InterlockedCompareExchange(&self->m_peakMix, 0, 0);
							if (v > curP) InterlockedExchange(&self->m_peakMix, v > 1000 ? 1000 : v);
						}
						if (wantMic)
							ScMicIntoStereo(fL, fR, (int)n, (int)srcHz);

						int outFrames = (int)(((__int64)n * (int)outHz) / (srcHz ? (int)srcHz : (int)outHz));
						if (outFrames < 1 && n > 0) outFrames = 1;
						if (outFrames > 8192) outFrames = 8192;
						for (int o = 0; o < outFrames; ++o) {
							double srcPos = (srcHz == outHz) ? (double)o : ((double)o * (double)srcHz / (double)outHz);
							int i0 = (int)srcPos;
							int i1 = i0 + 1;
							if (i0 < 0) i0 = 0;
							if (i0 >= (int)n) i0 = (int)n - 1;
							if (i1 >= (int)n) i1 = (int)n - 1;
							const float frac = (float)(srcPos - (double)i0);
							float L = fL[i0] + (fL[i1] - fL[i0]) * frac;
							float R = fR[i0] + (fR[i1] - fR[i0]) * frac;
							L = ScClamp1(L);
							R = ScClamp1(R);
							pcm[o * 2 + 0] = (short)(L * 32767.f);
							pcm[o * 2 + 1] = (short)(R * 32767.f);
						}
						hr = ScWriteAudioSample(writer, audioIdx, pcm, outFrames, outCh, (int)outHz, audioRt);
						if (FAILED(hr) && hr != MF_E_TRANSFORM_NEED_MORE_INPUT) {
							InterlockedExchange(&self->m_lastStage, 7);
							InterlockedExchange(&self->m_lastHr, hr);
							writeFail = TRUE;
							break;
						}
						audioRt += (10000000LL * outFrames) / outHz;
						done += n;
					}
				}
				loopCap->ReleaseBuffer(frames);
				if (writeFail) break;
				hr = loopCap->GetNextPacketSize(&packet);
			}
		};

		DWORD encWinTick = GetTickCount();
		int encWinCnt = 0;
		LONGLONG periodQ = qpf64 / (fps > 0 ? fps : 30);
		if (periodQ < 1) periodQ = 1;

		while (InterlockedCompareExchange(&self->m_stop, 0, 0) == 0 && !writeFail) {
			// 合成前に溜まったループバックを先に書く
			drainAudio();
			if (writeFail) break;

			LARGE_INTEGER qpc;
			QueryPerformanceCounter(&qpc);
			const LONGLONG elapsedQ = qpc.QuadPart - qpc0.QuadPart;
			BOOL didVideo = FALSE;
			if (elapsedQ >= nextFrameQpc) {
				// 遅れ分はバースト投入せず間引き（Enc が 7↔60 に振れる主因）
				if (nextFrameQpc + periodQ < elapsedQ) {
					nextFrameQpc = elapsedQ;
					videoRt = (elapsedQ * 10000000LL) / qpf64;
				}
				const LONGLONG dur = frameDur;
				snap.fxTime = (float)((elapsedQ * 1000.0) / (double)qpf64) * 0.001f;
				// WGC 優先（開始時にセッション再生成）。失敗フレームのみ GDI。
				BOOL haveFrame = ScComposeFrame(frame, snap, FALSE);
				if (!haveFrame)
					haveFrame = ScComposeFrame(frame, snap, TRUE);
				if (!haveFrame) {
					if (ScFrameAlloc(frame, snap.canvasW, snap.canvasH)) {
						ScFrameClear(frame, RGB(16, 16, 20));
						if (snap.fxN > 0 && frame.bits)
							ScGpuApplyEffectChain(frame.bits, frame.w, frame.h, frame.stride,
								snap.fx, snap.fxN, snap.fxTime, snap.fxStr);
						haveFrame = TRUE;
					}
				}
				if (haveFrame) {
					hr = ScWriteVideoSample(writer, videoIdx, frame, videoRt, dur, videoNv12, videoRgbBottomUp);
					if (FAILED(hr) && hr != MF_E_TRANSFORM_NEED_MORE_INPUT) {
						InterlockedExchange(&self->m_lastStage, 7);
						InterlockedExchange(&self->m_lastHr, hr);
						writeFail = TRUE;
						break;
					}
					videoRt += dur;
					const LONG fc = InterlockedIncrement(&self->m_frameCnt);
					// プレビューは 3 フレームに 1 回（Enc を優先）
					if ((fc % 3) == 0 && self->m_snapCsInit && TryEnterCriticalSection(&self->m_snapCs)) {
						if (self->m_cacheBits && frame.bits) {
							if (self->m_cacheW == frame.w && self->m_cacheH == frame.h
								&& frame.stride == frame.w * 4) {
								const size_t nbytes = (size_t)frame.stride * (size_t)frame.h;
								memcpy(self->m_cacheBits, frame.bits, nbytes);
							} else if (self->m_cacheDc && frame.hdc && self->m_cacheW > 0 && self->m_cacheH > 0) {
								SetStretchBltMode(self->m_cacheDc, COLORONCOLOR);
								StretchBlt(self->m_cacheDc, 0, 0, self->m_cacheW, self->m_cacheH,
									frame.hdc, 0, 0, frame.w, frame.h, SRCCOPY);
							}
						}
						LeaveCriticalSection(&self->m_snapCs);
					}
					// 直近エンコード投入 FPS（約0.8秒窓）= 実ウォールクロック
					{
						encWinCnt++;
						const DWORD nowEnc = GetTickCount();
						const DWORD elEnc = nowEnc - encWinTick;
						if (elEnc >= 800) {
							const LONG x10 = (LONG)((encWinCnt * 10000.0) / (double)elEnc + 0.5);
							InterlockedExchange(&self->m_encFpsX10, x10);
							encWinTick = nowEnc;
							encWinCnt = 0;
						}
					}
				}
				nextFrameQpc += periodQ;
				didVideo = TRUE;
				// 合成中に溜まった音声をすぐ排出
				drainAudio();
			}

			if (writeFail) break;
			if (haveAudio && loopCap && mixFmt) {
				if (!didVideo) {
					if (hEvent) {
						HANDLE waits[2];
						DWORD nWait = 1;
						waits[0] = hEvent;
						if (hMicEvent) { waits[1] = hMicEvent; nWait = 2; }
						WaitForMultipleObjects(nWait, waits, FALSE, 5);
					} else {
						if (hMicEvent)
							WaitForSingleObject(hMicEvent, 2);
						else
							Sleep(1);
					}
					drainAudio();
				}
			} else if (!didVideo) {
				Sleep(1);
			}
		}
	}

	if (writer) {
		HRESULT hf = writer->Finalize();
		if (FAILED(hf) && SUCCEEDED((HRESULT)InterlockedCompareExchange(&self->m_lastHr, 0, 0)))
			InterlockedExchange(&self->m_lastHr, hf);
	}

done:
	if (micClient) micClient->Stop();
	if (loopClient) loopClient->Stop();
	if (micCap) micCap->Release();
	if (micClient) micClient->Release();
	if (micFmt) CoTaskMemFree(micFmt);
	if (micDev) micDev->Release();
	if (hMicEvent) CloseHandle(hMicEvent);
	if (loopCap) loopCap->Release();
	if (loopClient) loopClient->Release();
	if (mixFmt) CoTaskMemFree(mixFmt);
	if (renderDev) renderDev->Release();
	if (enumer) enumer->Release();
	if (hEvent) CloseHandle(hEvent);
	if (writer) writer->Release();
	ScFrameFree(frame);
	ScReleaseReuseBuf();
	ScGdiReleaseScreenDc();
	MFShutdown();
	timeEndPeriod(1);
	if (SUCCEEDED(hrCo)) CoUninitialize();
	InterlockedExchange(&self->m_encodeGdi, 0);
	InterlockedExchange(&self->m_run, 0);
	return 0;
}

void CScreenCaptureDlg::OnBnClickedStart()
{
	if (InterlockedCompareExchange(&m_run, 0, 0))
		StopRecording();
	else
		StartRecording();
}

static int ScMeterUiLevel(LONG peak)
{
	// 線形ピークは小さく見えやすいので平方根カーブ+ゲインで視認性を上げる
	if (peak <= 0) return 0;
	if (peak > 1000) peak = 1000;
	const double n = (double)peak / 1000.0;
	int ui = (int)(sqrt(n) * 1000.0 * 1.15);
	if (ui < 1) ui = 1;
	if (ui > 1000) ui = 1000;
	return ui;
}

void CScreenCaptureDlg::PaintMetersFromPeaks()
{
	LONG mic = InterlockedCompareExchange(&m_peakMic, 0, 0);
	LONG sys = InterlockedCompareExchange(&m_peakSys, 0, 0);
	LONG mix = InterlockedCompareExchange(&m_peakMix, 0, 0);
	// 減衰を緩やかにしてリアルタイム感を維持
	InterlockedExchange(&m_peakMic, mic * 88 / 100);
	InterlockedExchange(&m_peakSys, sys * 88 / 100);
	InterlockedExchange(&m_peakMix, mix * 88 / 100);
	if (m_meterMic.GetSafeHwnd()) m_meterMic.SetLevel(ScMeterUiLevel(mic));
	if (m_meterSys.GetSafeHwnd()) m_meterSys.SetLevel(ScMeterUiLevel(sys));
	if (m_meterMix.GetSafeHwnd()) m_meterMix.SetLevel(ScMeterUiLevel(mix));
}

void CScreenCaptureDlg::CloseModeless()
{
	if (InterlockedCompareExchange(&m_run, 0, 0))
		StopRecording();
	StopPeakMonitor();
	PersistUiToSavedata();
	CloseScLiveSettingsFromOwner();
	if (GetSafeHwnd())
		DestroyWindow();
}

void CScreenCaptureDlg::ShiftChildrenBelow(CWnd* dlg, int yThresholdClient, int dy)
{
	if (!dlg || !dlg->GetSafeHwnd() || dy == 0) return;
	for (CWnd* p = dlg->GetWindow(GW_CHILD); p; p = p->GetWindow(GW_HWNDNEXT)) {
		CRect r;
		p->GetWindowRect(&r);
		dlg->ScreenToClient(&r);
		if (r.top >= yThresholdClient)
			p->SetWindowPos(NULL, r.left, r.top + dy, 0, 0,
				SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}
}

void CScreenCaptureDlg::FitToWorkArea()
{
	if (!GetSafeHwnd()) return;

	HMONITOR mon = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi = { sizeof(mi) };
	if (!GetMonitorInfo(mon, &mi)) return;
	const int workW = mi.rcWork.right - mi.rcWork.left;
	const int workH = mi.rcWork.bottom - mi.rcWork.top;
	if (workW < 200 || workH < 200) return;

	CRect wr;
	GetWindowRect(&wr);
	int overflow = wr.Height() - workH;
	const int margin = 8;

	auto shrinkCtrlH = [](CWnd& w, int shrink) {
		if (!w.GetSafeHwnd() || shrink <= 0) return;
		CRect r;
		w.GetWindowRect(&r);
		w.SetWindowPos(NULL, 0, 0, r.Width(), r.Height() - shrink,
			SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	};

	// 配線パネルは操作の中心なので最後まで潰さない。
	// 1) プレビュー → 2) リスト → 3) 配線（最低高さ高め）

	// 1) プレビュー高さを縮める
	if (overflow > margin && m_preview.GetSafeHwnd()) {
		CRect pv;
		m_preview.GetWindowRect(&pv);
		ScreenToClient(&pv);
		const int minPvH = 110;
		int can = pv.Height() - minPvH;
		if (can < 0) can = 0;
		int take = overflow - margin;
		if (take > can) take = can;
		if (take > 0) {
			shrinkCtrlH(m_preview, take);
			ShiftChildrenBelow(this, pv.bottom, -take);
			SetWindowPos(NULL, 0, 0, wr.Width(), wr.Height() - take,
				SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			GetWindowRect(&wr);
			overflow = wr.Height() - workH;
			m_preview.Invalidate(FALSE);
		}
	}

	// 2) リスト高さを縮める
	if (overflow > margin && m_avail.GetSafeHwnd() && m_layer.GetSafeHwnd()) {
		CRect a;
		m_avail.GetWindowRect(&a);
		ScreenToClient(&a);
		const int minListH = 36;
		int can = a.Height() - minListH;
		if (can < 0) can = 0;
		int take = overflow - margin;
		if (take > can) take = can;
		if (take > 0) {
			shrinkCtrlH(m_avail, take);
			shrinkCtrlH(m_layer, take);
			ShiftChildrenBelow(this, a.bottom, -take);
			SetWindowPos(NULL, 0, 0, wr.Width(), wr.Height() - take,
				SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			GetWindowRect(&wr);
			overflow = wr.Height() - workH;
		}
	}

	// 3) FX配線は最後の手段（パレットが読める最低高を維持）
	if (overflow > margin && m_fxWire.GetSafeHwnd()) {
		CRect fx;
		m_fxWire.GetWindowRect(&fx);
		ScreenToClient(&fx);
		const int minFxH = 200;
		int can = fx.Height() - minFxH;
		if (can < 0) can = 0;
		int take = overflow - margin;
		if (take > can) take = can;
		if (take > 0) {
			shrinkCtrlH(m_fxWire, take);
			ShiftChildrenBelow(this, fx.bottom, -take);
			SetWindowPos(NULL, 0, 0, wr.Width(), wr.Height() - take,
				SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			GetWindowRect(&wr);
			overflow = wr.Height() - workH;
			m_fxWire.Invalidate(FALSE);
		}
	}

	// 横は通常余裕。万一は作業領域内へクランプのみ
	GetWindowRect(&wr);
	int x = wr.left, y = wr.top, w = wr.Width(), h = wr.Height();
	if (w > workW) w = workW;
	if (h > workH) h = workH;
	if (x < mi.rcWork.left) x = mi.rcWork.left;
	if (y < mi.rcWork.top) y = mi.rcWork.top;
	if (x + w > mi.rcWork.right) x = mi.rcWork.right - w;
	if (y + h > mi.rcWork.bottom) y = mi.rcWork.bottom - h;
	// 余白があれば中央寄せ
	if (w < workW && h <= workH) {
		x = mi.rcWork.left + (workW - w) / 2;
		y = mi.rcWork.top + (workH - h) / 2;
	}
	SetWindowPos(NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

void CScreenCaptureDlg::LayoutHelpBtn()
{
	CCC_CaptionPlaceHelpBtn(m_hWnd, &m_help);
}

void CScreenCaptureDlg::ShowHelpSheet()
{
	if (g_scHelpDlg && ::IsWindow(g_scHelpDlg->GetSafeHwnd())) {
		CCC_PresentOwnedHelp(g_scHelpDlg, this);
		return;
	}
	if (g_scHelpDlg && !::IsWindow(g_scHelpDlg->GetSafeHwnd()))
		g_scHelpDlg = nullptr;
	CScHelpDlg* dlg = new CScHelpDlg(this, this);
	if (!dlg->Create(IDD_SC_HELP, this)) {
		delete dlg;
		return;
	}
	g_scHelpDlg = dlg;
	CCC_PresentOwnedHelp(dlg, this);
}

void CScreenCaptureDlg::OnBnClickedHelp()
{
	ShowHelpSheet();
}

void CScreenCaptureDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogBase::OnSize(nType, cx, cy);
	if (nType != SIZE_MINIMIZED) {
		CCC_CaptionLayout(m_hWnd);
		LayoutHelpBtn();
	}
}

LRESULT CScreenCaptureDlg::OnDpiChanged(WPARAM /*wParam*/, LPARAM lParam)
{
	const RECT* prc = reinterpret_cast<const RECT*>(lParam);
	if (prc) {
		SetWindowPos(NULL, prc->left, prc->top,
			prc->right - prc->left, prc->bottom - prc->top,
			SWP_NOZORDER | SWP_NOACTIVATE);
	}
	CCC_CaptionRefreshDpi(m_hWnd);
	FitToWorkArea();
	LayoutHelpBtn();
	// 配線メトリクスはクライアント矩形基準なので再描画のみ（ちらつき抑制で erase なし）
	if (m_fxWire.GetSafeHwnd())
		m_fxWire.Invalidate(FALSE);
	if (m_preview.GetSafeHwnd())
		m_preview.Invalidate(FALSE);
	RefreshOpaqueUi();
	return 0;
}

void CScreenCaptureDlg::OnBnClickedClose()
{
	CloseModeless();
}

void CScreenCaptureDlg::PostNcDestroy()
{
	CCustomBlurDialogBase::PostNcDestroy();
	if (g_screenCaptureDlg == this)
		g_screenCaptureDlg = NULL;
	delete this;
}

void OpenScreenCaptureModeless(CWnd* parent)
{
	if (g_screenCaptureDlg && ::IsWindow(g_screenCaptureDlg->GetSafeHwnd())) {
		g_screenCaptureDlg->ShowWindow(SW_SHOW);
		g_screenCaptureDlg->SetForegroundWindow();
		return;
	}
	g_screenCaptureDlg = new CScreenCaptureDlg(parent);
	if (!g_screenCaptureDlg->Create(IDD_SCREENCAPTURE, parent)) {
		delete g_screenCaptureDlg;
		g_screenCaptureDlg = NULL;
		return;
	}
	g_screenCaptureDlg->ShowWindow(SW_SHOW);
	g_screenCaptureDlg->SetForegroundWindow();
}

void ScreenCaptureApplySavedataToUi(BOOL gameGuide)
{
	if (g_screenCaptureDlg && ::IsWindow(g_screenCaptureDlg->GetSafeHwnd()))
		g_screenCaptureDlg->ApplySavedataToUi(gameGuide);
}

void CScreenCaptureDlg::ApplySavedataToUi(BOOL gameGuide)
{
	if (!GetSafeHwnd() || m_uiLocked || InterlockedCompareExchange(&m_run, 0, 0) != 0)
		return;

	RefreshModeCombo();

	int canvas = savedata.cap_canvas_preset;
	if (canvas < 0 || canvas > 4) canvas = 2;
	if (m_canvas.GetSafeHwnd())
		m_canvas.SetCurSel(canvas);

	if (m_audio.GetSafeHwnd())
		m_audio.SetCheck(savedata.cap_with_audio ? BST_CHECKED : BST_UNCHECKED);
	if (m_mic.GetSafeHwnd())
		m_mic.SetCheck(savedata.cap_with_mic ? BST_CHECKED : BST_UNCHECKED);
		AudioMicDevSyncComboSel(m_micDev);
		AudioLoopDevSyncComboSel(m_loopDev);
	if (m_includeMp.GetSafeHwnd())
		m_includeMp.SetCheck(savedata.cap_include_mp ? BST_CHECKED : BST_UNCHECKED);
	if (m_showCursor.GetSafeHwnd())
		m_showCursor.SetCheck(savedata.cap_show_cursor ? BST_CHECKED : BST_UNCHECKED);
	ApplyLiveFieldsToUi();
	SyncLiveUiEnable();

	static const int fpsTab[] = { 10, 15, 20, 24, 30, 60, 90, 120 };
	int fpsSel = 1;
	for (int i = 0; i < (int)_countof(fpsTab); ++i) {
		if (savedata.cap_fps == fpsTab[i]) fpsSel = i;
	}
	if (m_fps.GetSafeHwnd())
		m_fps.SetCurSel(fpsSel);

	{
		int chain[SC_FX_CHAIN_MAX] = {};
		BYTE str[SC_FX_CHAIN_MAX][SC_FX_STR_N];
		memset(str, SC_FX_STR_DEF, sizeof(str));
		int cn = savedata.cap_fx_n;
		if (cn < 0) cn = 0;
		if (cn > SC_FX_CHAIN_MAX) cn = SC_FX_CHAIN_MAX;
		const int src[SC_FX_CHAIN_MAX] = {
			savedata.cap_fx0, savedata.cap_fx1, savedata.cap_fx2, savedata.cap_fx3,
			savedata.cap_fx4, savedata.cap_fx5, savedata.cap_fx6, savedata.cap_fx7
		};
		int n = 0;
		for (int i = 0; i < cn; ++i) {
			if (src[i] > SC_FX_NONE && src[i] < SC_FX_COUNT) {
				chain[n] = src[i];
				memcpy(str[n], savedata.cap_fx_str[i], SC_FX_STR_N);
				n++;
			}
		}
		if (n <= 0 && savedata.cap_effect > 0 && savedata.cap_effect < SC_FX_COUNT) {
			chain[0] = savedata.cap_effect;
			n = 1;
		}
		m_fxWire.SetChain(chain, n, str);
		SyncFxComboFromChain();
	}

	EnableComposeUi(IsWindowComposeMode());
	if (savedata.cap_include_mp)
		SyncMpLayerFromCheck();
	ApplyPreviewTimer();
	UpdatePreview(TRUE);
	RefreshOpaqueUi();

	if (gameGuide && m_status.GetSafeHwnd()) {
		m_status.SetWindowText(LL14(
			L"ゲーム録画プリセット適用済み。①モードで録画したい画面/窓を選ぶ ②必要ならCrop ③赤枠を確認して「開始」。効果配線はクリア済み（高画質優先）。",
			L"Game capture preset applied. ① Pick screen/window in Mode ② Crop if needed ③ Check red frame → Start. FX cleared for quality.",
			L"Preset jeu applique. ① Mode ecran/fenetre ② Crop si besoin ③ Cadre rouge → Demarrer. FX effaces (qualite).",
			L"Preset gioco applicato. ① Modalita schermo/finestra ② Crop se serve ③ Cornice rossa → Avvia. FX azzerati (qualita).",
			L"Preset juego aplicado. ① Modo pantalla/ventana ② Crop si hace falta ③ Marco rojo → Iniciar. FX limpios (calidad).",
			L"게임 캡처 프리셋 적용. ①모드에서 화면/창 선택 ②필요시 Crop ③빨간 틀 확인 후 시작. FX 제거(고화질).",
			L"已应用游戏录制预设。①在模式中选画面/窗口 ②需要时Crop ③确认红框后开始。已清效果（偏画质）。",
			L"تم تطبيق إعداد اللعبة. ① اختر الشاشة/النافذة ② Crop إن لزم ③ الإطار الأحمر ثم ابدأ. أُزيلت التأثيرات للجودة.",
			L"Пресет игры применён. ① Режим экран/окно ② Crop при необходимости ③ Красная рамка → Старт. FX очищены (качество).",
			L"Game-Preset angewendet. ① Modus Bildschirm/Fenster ② ggf. Crop ③ Roten Rahmen prüfen → Start. FX geleert (Qualität).",
			L"Preset de jogo aplicado. ① Modo tela/janela ② Crop se preciso ③ Moldura vermelha → Iniciar. FX limpos (qualidade).",
			L"Game-preset toegepast. ① Modus scherm/venster ② Crop indien nodig ③ Rood kader → Start. FX gewist (kwaliteit).",
			L"Preset gry zastosowany. ① Tryb ekran/okno ② Crop w razie potrzeby ③ Czerwona ramka → Start. FX wyczyszczone (jakosc).",
			L"Oyun on ayari uygulandi. ① Modda ekran/pencere sec ② Gerekirse Crop ③ Kirmizi cerceve → Baslat. FX temiz (kalite)."));
	}
}

void CloseScreenCaptureIfOpen()
{
	if (g_screenCaptureDlg && ::IsWindow(g_screenCaptureDlg->GetSafeHwnd()))
		g_screenCaptureDlg->DestroyWindow();
}

void CScreenCaptureDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == SC_TIMER_PREV) {
		if (!m_dragging)
			UpdatePreview(); // 録画中もライブプレビューを継続
		PaintMetersFromPeaks(); // プレビューと同周期でメーター更新(二重減衰を避ける)
	} else if (nIDEvent == SC_TIMER_UI) {
		UpdateElapsedUi();
		if (m_thread) {
			DWORD code = 0;
			if (GetExitCodeThread(m_thread, &code) && code != STILL_ACTIVE)
				StopRecording();
		}
		// YouTube ライブ遷移: 同期 HTTPS を UI でやるとプレビューが 0.3s 単位で止まる。
		// キックだけ UI、実APIは専用スレッド（Enc とも分離）。
		if (m_liveMode && m_liveService == 0 && !m_ytLiveTransitionDone) {
			if (m_ytGoLiveThread) {
				DWORD code = 0;
				if (GetExitCodeThread(m_ytGoLiveThread, &code) && code != STILL_ACTIVE) {
					CloseHandle(m_ytGoLiveThread);
					m_ytGoLiveThread = NULL;
				}
			}
			const BOOL kick = (InterlockedCompareExchange(&m_ytGoLiveRequest, 0, 1) == 1);
			const DWORD now = GetTickCount();
			const BOOL due = (m_ytGoLiveLastTick == 0 || (now - m_ytGoLiveLastTick) >= 2000);
			if (!m_ytGoLiveThread && (kick || due)) {
				m_ytGoLiveLastTick = now;
				uintptr_t th = _beginthreadex(NULL, 0, &ScYtGoLiveThunk::Run, this, 0, NULL);
				m_ytGoLiveThread = (th ? (HANDLE)th : NULL);
			} else if (kick && m_ytGoLiveThread) {
				InterlockedExchange(&m_ytGoLiveRequest, 1); // 実行中なら次ティックへ持ち越し
			}
		}
		if (m_liveMode && m_liveService == 0) {
			const LONG ph = InterlockedCompareExchange(&m_ytLivePhase, 0, 0);
			CString st;
			if (ph == 3) {
				st = LL14(
					L"YouTube ライブ中。",
					L"YouTube is live.",
					L"YouTube en direct.",
					L"YouTube in diretta.",
					L"YouTube en vivo.",
					L"YouTube 라이브 중.",
					L"YouTube 直播中。",
					L"YouTube مباشر.",
					L"YouTube в эфире.",
					L"YouTube live.",
					L"YouTube ao vivo.",
					L"YouTube live.",
					L"YouTube na zywo.",
					L"YouTube canlida.");
			} else if (ph == 4) {
				CString detail = m_ytStreamStatus[0] ? m_ytStreamStatus : L"";
				if (!detail.IsEmpty() && detail.Find(L'/') >= 0
					&& detail.Find(L"fail") < 0 && detail.Find(L"err") < 0)
					detail.Empty();
				st = LL14(
					L"RTMP到達済。ライブ開始に失敗。Studioで手動開始できます。",
					L"RTMP connected. Go-live failed. Start manually in Studio.",
					L"RTMP connecte. Echec du live. Demarrez dans Studio.",
					L"RTMP connesso. Avvio live non riuscito. Avvia in Studio.",
					L"RTMP conectado. Fallo al ir en vivo. Inicie en Studio.",
					L"RTMP 연결됨. 라이브 시작 실패. Studio에서 수동 시작.",
					L"RTMP 已连接。开播失败。可在 Studio 手动开始。",
					L"RTMP متصل. فشل بدء البث. ابدأ يدويًا في Studio.",
					L"RTMP подключен. Старт эфира не удался. Запустите в Studio.",
					L"RTMP verbunden. Live-Start fehlgeschlagen. In Studio starten.",
					L"RTMP ligado. Falha ao ir ao vivo. Inicie no Studio.",
					L"RTMP verbonden. Live-start mislukt. Start in Studio.",
					L"RTMP polaczony. Start live nieudany. Uruchom w Studio.",
					L"RTMP baglandi. Canli baslatma basarisiz. Studio'dan baslatin.");
				if (!detail.IsEmpty()) {
					st += L" ";
					st += detail;
				}
			} else if (ph == 5) {
				st = LL14(
					L"配信枠IDがありません。「配信枠作成」を先に実行してください。",
					L"No broadcast id. Click Create broadcast first.",
					L"Pas d'id de diffusion. Cliquez Creer d'abord.",
					L"Manca id diretta. Premi Crea diretta prima.",
					L"No hay id de emision. Pulse Crear emision primero.",
					L"방송 ID 없음. 먼저 방송 생성을 실행하세요.",
					L"没有直播框 ID。请先点击「创建直播」。",
					L"لا يوجد معرف بث. نفّذ إنشاء البث أولاً.",
					L"Нет id эфира. Сначала нажмите «Создать эфир».",
					L"Keine Broadcast-ID. Zuerst Broadcast anlegen.",
					L"Sem id de transmissao. Clique Criar primeiro.",
					L"Geen broadcast-id. Klik eerst Broadcast maken.",
					L"Brak id transmisji. Najpierw kliknij Utworz.",
					L"Yayin id yok. Once Yayin olustur'a basin.");
			} else if (ph == 1) {
				st = LL14(
					L"RTMP送信中。YouTubeの受信を待っています。",
					L"Sending RTMP. Waiting for YouTube ingest.",
					L"Envoi RTMP. Attente de la reception YouTube.",
					L"Invio RTMP. In attesa della ricezione YouTube.",
					L"Enviando RTMP. Esperando recepcion de YouTube.",
					L"RTMP 송출 중. YouTube 수신 대기.",
					L"正在推送 RTMP。等待 YouTube 接收。",
					L"جارٍ إرسال RTMP. بانتظار استقبال YouTube.",
					L"Отправка RTMP. Ожидание приёма YouTube.",
					L"RTMP senden. Warte auf YouTube-Empfang.",
					L"A enviar RTMP. A aguardar rececao do YouTube.",
					L"RTMP verzenden. Wachten op YouTube-ontvangst.",
					L"Wysylanie RTMP. Czekam na odbior YouTube.",
					L"RTMP gonderiliyor. YouTube alimi bekleniyor.");
			} else if (ph == 2) {
				st = LL14(
					L"RTMP到達済。ライブ配信を開始しています。",
					L"RTMP connected. Starting the live broadcast.",
					L"RTMP connecte. Demarrage de la diffusion.",
					L"RTMP connesso. Avvio della diretta.",
					L"RTMP conectado. Iniciando la emision en vivo.",
					L"RTMP 연결됨. 라이브 방송 시작 중.",
					L"RTMP 已连接。正在开始直播。",
					L"RTMP متصل. جارٍ بدء البث المباشر.",
					L"RTMP подключен. Запуск эфира.",
					L"RTMP verbunden. Live-Broadcast wird gestartet.",
					L"RTMP ligado. A iniciar a transmissao ao vivo.",
					L"RTMP verbonden. Live-uitzending starten.",
					L"RTMP polaczony. Uruchamiam transmisje na zywo.",
					L"RTMP baglandi. Canli yayin baslatiliyor.");
			} else if (ph == 6) {
				st = LL14(
					L"ffmpeg が終了しました。",
					L"ffmpeg exited.",
					L"ffmpeg s'est arrete.",
					L"ffmpeg terminato.",
					L"ffmpeg termino.",
					L"ffmpeg 종료됨.",
					L"ffmpeg 已退出。",
					L"توقف ffmpeg.",
					L"ffmpeg завершился.",
					L"ffmpeg beendet.",
					L"ffmpeg saiu.",
					L"ffmpeg gestopt.",
					L"ffmpeg zakonczyl.",
					L"ffmpeg cikti.");
			}
			if (!st.IsEmpty()) {
				if (m_status.GetSafeHwnd())
					m_status.SetWindowText(st);
				ScLiveSettingsSetStatusText(st);
			}
		}
		if (m_path.GetSafeHwnd() && ::GetFocus() == m_path.GetSafeHwnd())
			m_path.RepaintClient();
	}
	CCustomBlurDialogBase::OnTimer(nIDEvent);
}

void CScreenCaptureDlg::OnDestroy()
{
	AudioMicDevUnregisterCombo(&m_micDev);
	AudioLoopDevUnregisterCombo(&m_loopDev);
	::RemoveProp(m_hWnd, CCUSTOM_POPUP_RELAX_DISMISS_PROP);
	KillTimer(SC_TIMER_PREV);
	KillTimer(SC_TIMER_UI);
	StopPeakMonitor();
	if (g_scHelpDlg && ::IsWindow(g_scHelpDlg->GetSafeHwnd()))
		g_scHelpDlg->DestroyWindow();
	if (m_picking) {
		m_picking = FALSE;
		ReleaseCapture();
	}
	if (InterlockedCompareExchange(&m_run, 0, 0))
		StopRecording();
	m_preview.SetOwner(NULL);
	if (m_cacheDc) {
		if (m_cacheOld) SelectObject(m_cacheDc, m_cacheOld);
		DeleteDC(m_cacheDc);
		m_cacheDc = NULL;
		m_cacheOld = NULL;
	}
	if (m_cacheBmp) {
		DeleteObject(m_cacheBmp);
		m_cacheBmp = NULL;
	}
	m_cacheBits = NULL;
	CCustomBlurDialogBase::OnDestroy();
}

void CScreenCaptureDlg::OnCancel()
{
	if (m_picking) {
		m_picking = FALSE;
		ReleaseCapture();
		m_status.SetWindowText(LL14(
			L"選択を取り消しました。", L"Pick cancelled.", L"Sélection annulée.", L"Selezione annullata.",
			L"Selección cancelada.", L"선택을 취소했습니다.", L"已取消选择。", L"تم إلغاء الاختيار.",
			L"Выбор отменён.", L"Auswahl abgebrochen.", L"Seleção cancelada.", L"Selectie geannuleerd.",
			L"Anulowano wybór.", L"Seçim iptal edildi."));
		return;
	}
	CloseModeless();
}

void CScreenCaptureDlg::OnOK()
{
}
