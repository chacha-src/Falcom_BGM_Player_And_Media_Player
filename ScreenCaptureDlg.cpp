// ScreenCaptureDlg.cpp
// 画面キャプチャ → MP4 (H.264 + AAC)
// プライマリ / 全モニタ / ウィンドウ合成(配置・拡大縮小・Z順)

#include "stdafx.h"
#include "ogg.h"
#include "oggDlg.h"
#include "ScreenCaptureDlg.h"
#include "ScWgcCapture.h"
#include "CMediaPlayerDlg.h"
#include <mmdeviceapi.h>
#include <Audioclient.h>
#include <FunctionDiscoveryKeys_devpkey.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <process.h>
#include <math.h>
#include <ShlObj.h>

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

extern void MpPersistSavedataQuick();

#ifndef MF_E_TRANSFORM_NEED_MORE_INPUT
#define MF_E_TRANSFORM_NEED_MORE_INPUT ((HRESULT)0xC00D6D72L)
#endif
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
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
static BOOL ScCaptureWindowScaled(HWND hwnd, ScFrameBuf& fb, int dstW, int dstH, HWND excludeHwnd)
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

	::SetStretchBltMode(fb.hdc, COLORONCOLOR);

	// 1) Windows.Graphics.Capture（前面UIでも可）
	// 暗いアプリ画面を ScBufferMostlyBlack で落とすと PrintWindow(~11fps) に落ちるので WGC 成功時は信頼する
	if (ScWgcCaptureWindowBgra(hwnd, fb.bits, dstW, dstH, fb.stride))
		return TRUE;

	// 2) 高速経路: ターゲットが他窓に隠れていなければ画面から直接縮小コピー
	if (ScWindowClearForScreenCap(hwnd, excludeHwnd)) {
		if (ScCaptureWindowFromScreen(hwnd, fb, dstW, dstH))
			return TRUE;
	}

	// 3) 正確経路: PrintWindow（WGC不可・遮蔽時）
	if (!ScPrintWindowViaCompat(hwnd, ww, wh, s_scNativeBuf)) {
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

	if (fb.w == s_scNativeBuf.w && fb.h == s_scNativeBuf.h)
		return ::BitBlt(fb.hdc, 0, 0, fb.w, fb.h, s_scNativeBuf.hdc, 0, 0, SRCCOPY);
	return ::StretchBlt(fb.hdc, 0, 0, fb.w, fb.h,
		s_scNativeBuf.hdc, 0, 0, s_scNativeBuf.w, s_scNativeBuf.h, SRCCOPY);
}

// キャンバスへウィンドウを描画（画面合成ではなく HWND ターゲット）
static BOOL ScBlitWindowClipped(HWND hwnd, HDC dst, int canvasW, int canvasH,
	int dx, int dy, int dw, int dh, HWND excludeHwnd)
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

	// レイヤ配置サイズへ HWND をキャプチャ（前面=画面コピー高速 / 遮蔽=PrintWindow）
	int capW = dw & ~1;
	int capH = dh & ~1;
	if (capW < 2) capW = 2;
	if (capH < 2) capH = 2;
	if (!ScCaptureWindowScaled(hwnd, s_scScaledBuf, capW, capH, excludeHwnd))
		return FALSE;

	const int sx = x0 - dx;
	const int sy = y0 - dy;
	::SetStretchBltMode(dst, COLORONCOLOR);
	if (s_scScaledBuf.w == capW && s_scScaledBuf.h == capH)
		return ::BitBlt(dst, x0, y0, outW, outH, s_scScaledBuf.hdc, sx, sy, SRCCOPY);
	return ::StretchBlt(dst, x0, y0, outW, outH,
		s_scScaledBuf.hdc, sx, sy, outW, outH, SRCCOPY);
}

static BOOL ScComposeFrame(ScFrameBuf& out, const CScreenCaptureDlg::ComposeSnap& snap)
{
	int cw = snap.canvasW;
	int ch = snap.canvasH;
	cw &= ~1; ch &= ~1;
	if (cw < 2 || ch < 2) return FALSE;
	if (!out.bits || out.w != cw || out.h != ch) {
		if (!ScFrameAlloc(out, cw, ch))
			return FALSE;
	}
	ScFrameClear(out, RGB(16, 16, 20));

	if (snap.mode == CScreenCaptureDlg::SC_MODE_PRIMARY) {
		const int sw = GetSystemMetrics(SM_CXSCREEN);
		const int sh = GetSystemMetrics(SM_CYSCREEN);
		HDC screen = GetDC(NULL);
		if (!screen) return FALSE;
		SetStretchBltMode(out.hdc, COLORONCOLOR);
		BOOL ok = StretchBlt(out.hdc, 0, 0, out.w, out.h, screen, 0, 0, sw, sh, SRCCOPY);
		ReleaseDC(NULL, screen);
		return ok;
	}
	if (snap.mode == CScreenCaptureDlg::SC_MODE_VIRTUAL) {
		const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
		const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
		const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
		const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
		HDC screen = GetDC(NULL);
		if (!screen) return FALSE;
		SetStretchBltMode(out.hdc, COLORONCOLOR);
		BOOL ok = StretchBlt(out.hdc, 0, 0, out.w, out.h, screen, vx, vy, vw, vh, SRCCOPY);
		ReleaseDC(NULL, screen);
		return ok;
	}

	// ウィンドウ合成: PrintWindow で HWND 単位（前面に隠れてもターゲットを描く／キャプチャUIは除外）
	for (int i = snap.layerCnt - 1; i >= 0; --i) {
		const CScreenCaptureDlg::Layer& L = snap.layers[i];
		if (L.hidden) continue;
		if (!L.hwnd || !IsWindow(L.hwnd) || L.w < 1 || L.h < 1) continue;
		if (ScIsExcludedHwnd(L.hwnd, snap.excludeHwnd)) continue;
		ScBlitWindowClipped(L.hwnd, out.hdc, out.w, out.h, L.x, L.y, L.w, L.h, snap.excludeHwnd);
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
				snap.mpX, snap.mpY, snap.mpW, snap.mpH, snap.excludeHwnd);
		}
	}
	return TRUE;
}

static HRESULT ScAddVideoStream(IMFSinkWriter* writer, DWORD* outIdx, int w, int h, int fps)
{
	IMFMediaType* outType = NULL;
	IMFMediaType* inType = NULL;
	HRESULT hr = MFCreateMediaType(&outType);
	if (FAILED(hr)) return hr;
	hr = outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	if (SUCCEEDED(hr)) hr = outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
	// 30fps 向け: 過大ビットレートはエンコード遅延の原因になるので抑える
	{
		UINT32 br = (UINT32)(((__int64)w * h * fps) / 6);
		if (br < 2500000u) br = 2500000u;
		if (br > 12000000u) br = 12000000u;
		if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AVG_BITRATE, br);
	}
	if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	if (SUCCEEDED(hr)) hr = MFSetAttributeSize(outType, MF_MT_FRAME_SIZE, (UINT32)w, (UINT32)h);
	if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(outType, MF_MT_FRAME_RATE, (UINT32)fps, 1);
	if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(outType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
	DWORD idx = 0;
	if (SUCCEEDED(hr)) hr = writer->AddStream(outType, &idx);
	if (SUCCEEDED(hr)) hr = MFCreateMediaType(&inType);
	if (SUCCEEDED(hr)) hr = inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	if (SUCCEEDED(hr)) hr = inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
	if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	if (SUCCEEDED(hr)) hr = MFSetAttributeSize(inType, MF_MT_FRAME_SIZE, (UINT32)w, (UINT32)h);
	if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(inType, MF_MT_FRAME_RATE, (UINT32)fps, 1);
	if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(inType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
	if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32)(w * 4));
	if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(idx, inType, NULL);
	if (outType) outType->Release();
	if (inType) inType->Release();
	if (SUCCEEDED(hr) && outIdx) *outIdx = idx;
	return hr;
}

static HRESULT ScAddAudioStream(IMFSinkWriter* writer, DWORD* outIdx, UINT32 hz, UINT32 ch)
{
	IMFMediaType* outType = NULL;
	IMFMediaType* inType = NULL;
	HRESULT hr = MFCreateMediaType(&outType);
	if (FAILED(hr)) return hr;
	hr = outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	if (SUCCEEDED(hr)) hr = outType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
	if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
	if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, hz);
	if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, ch);
	if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 192000 / 8);
	DWORD idx = 0;
	if (SUCCEEDED(hr)) hr = writer->AddStream(outType, &idx);
	if (SUCCEEDED(hr)) hr = MFCreateMediaType(&inType);
	if (SUCCEEDED(hr)) hr = inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	if (SUCCEEDED(hr)) hr = inType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
	if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
	if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, hz);
	if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, ch);
	if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, ch * 2);
	if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, hz * ch * 2);
	if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
	if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(idx, inType, NULL);
	if (outType) outType->Release();
	if (inType) inType->Release();
	if (SUCCEEDED(hr) && outIdx) *outIdx = idx;
	return hr;
}

static HRESULT ScWriteVideoSample(IMFSinkWriter* writer, DWORD stream, const ScFrameBuf& fb, LONGLONG rt, LONGLONG dur)
{
	const DWORD cb = (DWORD)(fb.stride * fb.h);
	IMFSample* sample = NULL;
	IMFMediaBuffer* buffer = NULL;
	HRESULT hr = MFCreateSample(&sample);
	if (FAILED(hr)) return hr;
	hr = MFCreateMemoryBuffer(cb, &buffer);
	if (FAILED(hr)) { sample->Release(); return hr; }
	BYTE* p = NULL;
	hr = buffer->Lock(&p, NULL, NULL);
	if (SUCCEEDED(hr)) {
		memcpy(p, fb.bits, cb);
		buffer->Unlock();
		buffer->SetCurrentLength(cb);
		sample->AddBuffer(buffer);
		sample->SetSampleTime(rt);
		sample->SetSampleDuration(dur);
		hr = writer->WriteSample(stream, sample);
	}
	buffer->Release();
	sample->Release();
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
	if (o->m_mode.GetCurSel() == CScreenCaptureDlg::SC_MODE_WINDOWS) return TRUE;
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
		if (layer >= 0 && layer < m_owner->m_layerCnt) {
			m_owner->m_layer.SetCurSel(layer);
			m_owner->SyncGeoEditsFromSel();
			const BOOL hidden = m_owner->m_layers[layer].hidden;
			CMenu menu;
			if (menu.CreatePopupMenu()) {
				menu.AppendMenu(MF_STRING, ID_SC_LAYER_HIDE,
					hidden
					? LL14(L"Show(表示)", L"Show", L"Afficher", L"Mostra", L"Mostrar", L"표시", L"显示", L"إظهار",
						L"Показать", L"Einblenden", L"Mostrar", L"Tonen", L"Pokaż", L"Göster")
					: LL14(L"Hide(非表示)", L"Hide", L"Masquer", L"Nascondi", L"Ocultar", L"숨기기", L"隐藏", L"إخفاء",
						L"Скрыть", L"Ausblenden", L"Ocultar", L"Verbergen", L"Ukryj", L"Gizle"));
				CPoint sp = point;
				ClientToScreen(&sp);
				const UINT cmd = menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
					sp.x, sp.y, m_owner);
				if (cmd == ID_SC_LAYER_HIDE)
					m_owner->ToggleLayerHidden(layer);
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

static CScreenCaptureDlg* g_screenCaptureDlg = NULL;

IMPLEMENT_DYNAMIC(CScreenCaptureDlg, CCustomBlurDialogBase)

CScreenCaptureDlg::CScreenCaptureDlg(CWnd* pParent)
	: CCustomBlurDialogBase(CScreenCaptureDlg::IDD, pParent)
	, m_availCnt(0)
	, m_layerCnt(0)
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
	, m_lastHr(S_OK)
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
	, m_fpsVal(15)
	, m_startTick(0)
{
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
	DDX_Control(pDX, IDC_SC_MODE_L, m_modeLabel);
	DDX_Control(pDX, IDC_SC_MODE, m_mode);
	DDX_Control(pDX, IDC_SC_CANVAS_L, m_canvasLabel);
	DDX_Control(pDX, IDC_SC_CANVAS, m_canvas);
	DDX_Control(pDX, IDC_SC_FPS_L, m_fpsLabel);
	DDX_Control(pDX, IDC_SC_FPS, m_fps);
	DDX_Control(pDX, IDC_SC_AUDIO, m_audio);
	DDX_Control(pDX, IDC_SC_MIC, m_mic);
	DDX_Control(pDX, IDC_SC_METER_MIC_L, m_meterMicL);
	DDX_Control(pDX, IDC_SC_METER_SYS_L, m_meterSysL);
	DDX_Control(pDX, IDC_SC_METER_MIX_L, m_meterMixL);
	DDX_Control(pDX, IDC_SC_METER_MIC, m_meterMic);
	DDX_Control(pDX, IDC_SC_METER_SYS, m_meterSys);
	DDX_Control(pDX, IDC_SC_METER_MIX, m_meterMix);
	DDX_Control(pDX, IDC_SC_INCMP, m_includeMp);
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
	ON_CBN_SELCHANGE(IDC_SC_MODE, &CScreenCaptureDlg::OnCbnSelchangeMode)
	ON_CBN_SELCHANGE(IDC_SC_CANVAS, &CScreenCaptureDlg::OnCbnSelchangeCanvas)
	ON_CBN_SELCHANGE(IDC_SC_FPS, &CScreenCaptureDlg::OnCbnSelchangeFps)
	ON_LBN_SELCHANGE(IDC_SC_LAYER, &CScreenCaptureDlg::OnLbnSelchangeLayer)
	ON_WM_LBUTTONDOWN()
	ON_WM_TIMER()
	ON_WM_DESTROY()
END_MESSAGE_MAP()

void CScreenCaptureDlg::RefreshOpaqueUi()
{
	if (!GetSafeHwnd()) return;
	PostMessage(CCC_MSG_REAPPLY_OPAQUE_FIXERS, 0, 0);
	if (m_path.GetSafeHwnd()) m_path.RepaintClient();
	if (m_fps.GetSafeHwnd()) m_fps.Invalidate(FALSE);
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

void CScreenCaptureDlg::ResolveCanvasSize(int& outW, int& outH) const
{
	const int preset = m_canvas.GetCurSel();
	if (preset == 1) { outW = 1280; outH = 720; return; }
	if (preset == 2) { outW = 1920; outH = 1080; return; }
	if (preset == 3) { outW = 1600; outH = 900; return; }
	// 0=自動: レイヤbboxではなく画面基準（レイヤはキャンバスへ縮小フィット）
	const int mode = m_mode.GetCurSel();
	if (mode == SC_MODE_VIRTUAL) {
		outW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
		outH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
	} else {
		outW = GetSystemMetrics(SM_CXSCREEN);
		outH = GetSystemMetrics(SM_CYSCREEN);
	}
	if (outW > 1920) {
		outH = (int)(((__int64)outH * 1920) / outW);
		outW = 1920;
	}
	outW &= ~1; outH &= ~1;
	if (outW < 160) outW = 160;
	if (outH < 120) outH = 120;
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

void CScreenCaptureDlg::BuildComposeSnap(ComposeSnap& out) const
{
	memset(&out, 0, sizeof(out));
	int mode = m_mode.GetCurSel();
	if (mode < 0 || mode > 2) mode = 0;
	out.mode = mode;
	ResolveCanvasSize(out.canvasW, out.canvasH);
	out.layerCnt = 0;
	out.excludeHwnd = GetSafeHwnd();
	const BOOL wantMp = (m_includeMp.GetSafeHwnd()
		&& const_cast<CCustomCheckBox&>(m_includeMp).GetCheck());
	out.includeMp = wantMp;
	out.mpHidden = FALSE;
	out.mpHwnd = FindMediaPlayerHwnd();

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
				out.mpHidden = out.layers[i].hidden;
				break;
			}
		}
	}
}

void CScreenCaptureDlg::PersistUiToSavedata()
{
	if (!GetSafeHwnd()) return;
	savedata.cap_with_audio = m_audio.GetCheck() ? 1 : 0;
	savedata.cap_with_mic = m_mic.GetCheck() ? 1 : 0;
	savedata.cap_include_mp = m_includeMp.GetCheck() ? 1 : 0;
	savedata.cap_fps = CurrentPreviewFps();
	int mode = m_mode.GetCurSel();
	if (mode < 0 || mode > 2) mode = 0;
	savedata.cap_mode = mode;
	int canvas = m_canvas.GetCurSel();
	if (canvas < 0 || canvas > 3) canvas = 2;
	savedata.cap_canvas_preset = canvas;
	int cw = 0, ch = 0;
	ResolveCanvasSize(cw, ch);
	savedata.cap_canvas_w = cw;
	savedata.cap_canvas_h = ch;
	CString path;
	m_path.GetWindowText(path);
	path = NormalizeOutPath(path);
	_tcsncpy(savedata.cap_last_path, path, _countof(savedata.cap_last_path) - 1);
	savedata.cap_last_path[_countof(savedata.cap_last_path) - 1] = 0;
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
	m_start.SetWindowText(recording
		? LL14(L"録画停止", L"Stop", L"Arrêter", L"Stop", L"Detener", L"중지", L"停止", L"إيقاف",
			L"Стоп", L"Stopp", L"Parar", L"Stop", L"Stop", L"Durdur")
		: LL14(L"録画開始", L"Start", L"Démarrer", L"Avvia", L"Iniciar", L"시작", L"开始", L"بدء",
			L"Старт", L"Start", L"Iniciar", L"Start", L"Start", L"Başlat"));
	const BOOL compose = (m_mode.GetCurSel() == SC_MODE_WINDOWS);
	EnableComposeUi(compose);
	// EnableWindow は透過を壊すのでモード/キャンバスは PreTranslate でロック
	RefreshOpaqueUi();
	// 録画中はプレビュー描画を間引き（Enc の周期ドロップ軽減）
	if (recording) {
		KillTimer(SC_TIMER_PREV);
		SetTimer(SC_TIMER_PREV, 100, NULL);
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
		s.Format(L"%s%s[Z%d] %s  (%d,%d %dx%d)",
			m_layers[i].hidden ? L"[Hide] " : L"",
			m_layers[i].isMp ? L"[MP] " : L"",
			i, m_layers[i].title, m_layers[i].x, m_layers[i].y, m_layers[i].w, m_layers[i].h);
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
		return;
	}
	CString s;
	s.Format(L"%d", m_layers[sel].x); m_editX.SetWindowText(s);
	s.Format(L"%d", m_layers[sel].y); m_editY.SetWindowText(s);
	s.Format(L"%d", m_layers[sel].w); m_editW.SetWindowText(s);
	s.Format(L"%d", m_layers[sel].h); m_editH.SetWindowText(s);
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
	if (!isMp && m_mode.GetCurSel() != SC_MODE_WINDOWS)
		m_mode.SetCurSel(SC_MODE_WINDOWS);
	EnableComposeUi(TRUE);
	// 追加後に全レイヤをキャンバス内へ再フィット（見切れ防止）
	FitAllLayersIntoCanvas();
	m_layer.SetCurSel(m_layerCnt - 1);
	SyncGeoEditsFromSel();
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
	if (!ScComposeFrame(fb, snap)) {
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
	const BOOL winMode = (m_mode.GetCurSel() == SC_MODE_WINDOWS);
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
	SyncGeoEditsFromSel();
}

void CScreenCaptureDlg::EndPreviewDrag()
{
	if (!m_dragging) return;
	m_dragging = FALSE;
	const int layer = m_dragLayer;
	m_dragHandle = SC_HIT_NONE;
	m_dragLayer = -1;
	RefreshLayerList();
	if (layer >= 0 && layer < m_layerCnt)
		m_layer.SetCurSel(layer);
	SyncGeoEditsFromSel();
	UpdatePreview(TRUE);
}

void CScreenCaptureDlg::DrawPreviewHud(CDC& dc, const CRect& imageRect, float scale, int canvasW, int canvasH)
{
	// キャンバス枠
	CPen penCanvas(PS_SOLID, 1, RGB(80, 90, 110));
	CPen* oldPen = dc.SelectObject(&penCanvas);
	dc.SelectStockObject(NULL_BRUSH);
	dc.Rectangle(imageRect);

	const int mode = m_mode.GetCurSel();
	const int sel = m_layer.GetCurSel();
	const int setFps = CurrentPreviewFps();
	const double prevFps = InterlockedCompareExchange(&m_prevFpsX10, 0, 0) / 10.0;
	const double encFps = InterlockedCompareExchange(&m_encFpsX10, 0, 0) / 10.0;

	// 上部情報バー（設定FPSと実測プレビュー/エンコードFPS）
	CString hud;
	CString modeName;
	if (mode == SC_MODE_PRIMARY) modeName = L"Primary";
	else if (mode == SC_MODE_VIRTUAL) modeName = L"All monitors";
	else modeName = L"Compose";
	if (InterlockedCompareExchange(&m_run, 0, 0)) {
		hud.Format(L"%s  %dx%d  set %d  Prev %.1f  Enc %.1f  layers %d  ●REC %ldf",
			(LPCTSTR)modeName, canvasW, canvasH, setFps, prevFps, encFps, m_layerCnt,
			(long)InterlockedCompareExchange(&m_frameCnt, 0, 0));
	} else {
		hud.Format(L"%s  %dx%d  set %d  Prev %.1f fps  layers %d",
			(LPCTSTR)modeName, canvasW, canvasH, setFps, prevFps, m_layerCnt);
	}
	CRect bar = imageRect;
	bar.bottom = bar.top + 18;
	dc.FillSolidRect(&bar, RGB(0, 0, 0));
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(RGB(220, 230, 255));
	CFont* oldFont = dc.SelectObject(GetFont());
	dc.DrawText(hud, &bar, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

	if (mode == SC_MODE_WINDOWS || (m_includeMp.GetCheck() && m_layerCnt > 0)) {
		for (int i = m_layerCnt - 1; i >= 0; --i) {
			if (mode != SC_MODE_WINDOWS && !m_layers[i].isMp) continue;
			const Layer& L = m_layers[i];
			CRect rr = CanvasToPreview(L.x, L.y, L.w, L.h);
			const BOOL selected = (i == sel);
			const COLORREF col = L.hidden
				? RGB(140, 140, 150)
				: (selected ? RGB(255, 200, 40) : (L.isMp ? RGB(120, 255, 160) : RGB(80, 200, 255)));
			CPen pen(L.hidden ? PS_DOT : PS_SOLID, selected ? 2 : 1, col);
			dc.SelectObject(&pen);
			dc.SelectStockObject(NULL_BRUSH);
			dc.Rectangle(&rr);

			CString label;
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
				L"ウィンドウを追加し、枠をドラッグ / 四隅で拡大縮小",
				L"Add windows, then drag / resize by corners",
				L"Ajoutez des fenêtres, puis glissez / redimensionnez",
				L"Aggiungi finestre, poi trascina / ridimensiona",
				L"Añada ventanas y arrastre / redimensione",
				L"창을 추가한 뒤 드래그 / 모서리로 크기 조절",
				L"添加窗口后拖动 / 用四角缩放",
				L"أضف نوافذ ثم اسحب / غيّر الحجم من الزوايا",
				L"Добавьте окна, затем перетаскивайте / углы",
				L"Fenster hinzufügen, dann ziehen / Ecken skalieren",
				L"Adicione janelas e arraste / redimensione",
				L"Vensters toevoegen, sleep / hoeken schalen",
				L"Dodaj okna, przeciągaj / skaluj rogami",
				L"Pencere ekleyin, sürükleyin / köşelerden ölçekleyin"),
				&tip, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
		}
	} else {
		dc.SetTextColor(RGB(160, 170, 190));
		CRect tip = imageRect;
		tip.top += 22;
		tip.DeflateRect(6, 0, 6, 6);
		dc.DrawText(LL14(
			L"プレビュー (録画にHUDは含まれません)",
			L"Preview (HUD is not recorded)",
			L"Aperçu (HUD non enregistré)",
			L"Anteprima (HUD non registrato)",
			L"Vista previa (HUD no se graba)",
			L"미리보기 (HUD는 녹화되지 않음)",
			L"预览（HUD不会录制）",
			L"معاينة (لا يُسجَّل HUD)",
			L"Превью (HUD не записывается)",
			L"Vorschau (HUD wird nicht aufgenommen)",
			L"Prévia (HUD não é gravado)",
			L"Voorbeeld (HUD wordt niet opgenomen)",
			L"Podgląd (HUD nie jest nagrywany)",
			L"Onizleme (HUD kayda girmez)"),
			&tip, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
	}

	dc.SelectObject(oldFont);
	dc.SelectObject(oldPen);
	(void)scale;
}

void CScreenCaptureDlg::PaintPreview(CDC& dc, const CRect& client)
{
	dc.FillSolidRect(&client, RGB(18, 18, 22));
	CRect imageRect;
	float scale = 1.f;
	int cw = 0, ch = 0;
	if (!GetPreviewMap(imageRect, scale, cw, ch))
		return;

	if (m_cacheDc && m_cacheBmp && m_cacheW > 0 && m_cacheH > 0) {
		if (m_snapCsInit) EnterCriticalSection(&m_snapCs);
		SetStretchBltMode(dc.GetSafeHdc(), HALFTONE);
		StretchBlt(dc.GetSafeHdc(), imageRect.left, imageRect.top, imageRect.Width(), imageRect.Height(),
			m_cacheDc, 0, 0, m_cacheW, m_cacheH, SRCCOPY);
		if (m_snapCsInit) LeaveCriticalSection(&m_snapCs);
	} else {
		dc.FillSolidRect(&imageRect, RGB(30, 30, 36));
	}
	DrawPreviewHud(dc, imageRect, scale, cw, ch);
}

void CScreenCaptureDlg::UpdatePreview(BOOL forceCompose)
{
	if (!GetSafeHwnd() || !m_preview.GetSafeHwnd()) return;
	// 録画中は CaptureThread が m_cache を更新するので再合成しない（Enc と奪い合わない）
	const BOOL recording = InterlockedCompareExchange(&m_run, 0, 0) != 0;
	if (!recording && (forceCompose || !m_dragging))
		RefreshComposeCache();
	m_preview.Invalidate(FALSE);
	if (!recording)
		m_preview.UpdateWindow();
	::RedrawWindow(m_preview.GetSafeHwnd(), NULL, NULL,
		recording
		? (RDW_INVALIDATE | RDW_FRAME)
		: (RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME));
}

BOOL CScreenCaptureDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	CCC_BringDialogToForeground(this);
	// 画面キャプチャAPIからこのダイアログを除外（写り込み防止。ウィンドウ合成は PrintWindow 側でも除外）
	::SetWindowDisplayAffinity(m_hWnd, WDA_EXCLUDEFROMCAPTURE);
	if (!m_snapCsInit) {
		InitializeCriticalSection(&m_snapCs);
		m_snapCsInit = TRUE;
	}
	m_preview.SetOwner(this);
	m_audio.SetAeroMode(FALSE);
	m_mic.SetAeroMode(FALSE);
	m_includeMp.SetAeroMode(FALSE);
	m_fps.SetAeroMode(FALSE);
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
		L"プレビュー: ドラッグで配置／四隅で拡大縮小／右クリックでHide(非表示・音だけ可)。HUDは録画されません。",
		L"Preview: drag to place / corners to resize / right-click Hide (audio-only OK). HUD not recorded.",
		L"Aperçu: glisser / coins pour taille / clic droit Hide (audio seul OK). HUD non enregistré.",
		L"Anteprima: trascina / angoli per dimensione / destro Hide (solo audio OK). HUD non registrato.",
		L"Vista: arrastre / esquinas para tamaño / clic der. Hide (solo audio OK). HUD no se graba.",
		L"미리보기: 드래그 배치/모서리 크기/우클릭 Hide(오디오만 가능). HUD는 녹화 안 됨.",
		L"预览：拖动放置/四角缩放/右键Hide（可只录声音）。HUD不会录制。",
		L"معاينة: اسحب / زوايا للحجم / يمين Hide (صوت فقط OK). لا يُسجَّل HUD.",
		L"Превью: перетаскивание / углы / ПКМ Hide (можно только звук). HUD не пишется.",
		L"Vorschau: ziehen / Ecken skalieren / Rechtsklick Hide (nur Audio OK). HUD nicht aufgenommen.",
		L"Prévia: arraste / cantos / direito Hide (só áudio OK). HUD não é gravado.",
		L"Voorbeeld: slepen / hoeken / rechtsklik Hide (alleen audio OK). HUD niet opgenomen.",
		L"Podgląd: przeciągaj / rogi / PPM Hide (tylko dźwięk OK). HUD nie jest nagrywany.",
		L"Önizleme: sürükle / köşe ölçek / sağ tık Hide (yalnızca ses OK). HUD kayda girmez."));

	m_mode.AddString(LL14(L"プライマリ画面", L"Primary screen", L"Écran principal", L"Schermo principale",
		L"Pantalla principal", L"기본 화면", L"主屏幕", L"الشاشة الرئيسية",
		L"Основной экран", L"Primärer Bildschirm", L"Ecrã principal", L"Primair scherm",
		L"Ekran główny", L"Birincil ekran"));
	m_mode.AddString(LL14(L"全モニタ (仮想)", L"All monitors (virtual)", L"Tous les moniteurs", L"Tutti i monitor",
		L"Todos los monitores", L"모든 모니터", L"全部显示器", L"كل الشاشات",
		L"Все мониторы", L"Alle Monitore", L"Todos os monitores", L"Alle monitoren",
		L"Wszystkie monitory", L"Tüm monitörler"));
	m_mode.AddString(LL14(L"ウィンドウ合成", L"Window compose", L"Composition fenêtres", L"Composizione finestre",
		L"Composición de ventanas", L"창 합성", L"窗口合成", L"تركيب النوافذ",
		L"Композиция окон", L"Fenster-Komposition", L"Composição de janelas", L"Venstercompositie",
		L"Kompozycja okien", L"Pencere kompozisyonu"));
	int mode = savedata.cap_mode;
	if (mode < 0 || mode > 2) mode = 0;
	m_mode.SetCurSel(mode);

	m_canvas.AddString(LL14(L"自動", L"Auto", L"Auto", L"Auto", L"Auto", L"자동", L"自动", L"تلقائي",
		L"Авто", L"Auto", L"Auto", L"Auto", L"Auto", L"Otomatik"));
	m_canvas.AddString(L"1280 x 720");
	m_canvas.AddString(L"1920 x 1080");
	m_canvas.AddString(L"1600 x 900");
	int canvas = savedata.cap_canvas_preset;
	if (canvas < 0 || canvas > 3) canvas = 2;
	m_canvas.SetCurSel(canvas);

	m_audio.SetCheck(savedata.cap_with_audio ? BST_CHECKED : BST_UNCHECKED);
	m_mic.SetCheck(savedata.cap_with_mic ? BST_CHECKED : BST_UNCHECKED);
	m_includeMp.SetCheck(savedata.cap_include_mp ? BST_CHECKED : BST_UNCHECKED);

	static const int fpsTab[] = { 10, 15, 20, 24, 30, 60 };
	int fpsSel = 1;
	for (int i = 0; i < 6; ++i) {
		CString s; s.Format(L"%d", fpsTab[i]);
		m_fps.AddString(s);
		if (savedata.cap_fps == fpsTab[i]) fpsSel = i;
	}
	m_fps.SetCurSel(fpsSel);

	// 日付ファイル名は毎回更新（フォルダだけ前回を引き継ぐ）
	m_path.SetWindowText(NormalizeOutPath(RefreshCaptureOutPathTimestamp(savedata.cap_last_path)));

	RefreshAvailList();
	EnableComposeUi(mode == SC_MODE_WINDOWS);
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
			L"録画＆プレビュー表示間隔(FPS)。PCスペックに合わせて選択(高いほど滑らか・負荷増)",
			L"Record & preview FPS. Pick for your PC (higher = smoother, heavier)",
			L"IPS enregistrement et aperçu. Selon le PC (plus élevé = plus fluide)",
			L"FPS registrazione e anteprima. In base al PC (più alto = più fluido)",
			L"FPS de grabación y vista previa. Según el PC (más alto = más fluido)",
			L"녹화·미리보기 FPS. PC 사양에 맞게 선택(높을수록 부드럽고 부하↑)",
			L"录制与预览帧率。按电脑性能选择（越高越流畅、更重）",
			L"معدل التسجيل والمعاينة. اختر حسب الجهاز (الأعلى أنعم وأثقل)",
			L"FPS записи и превью. Выберите по ПК (выше — плавнее, тяжелее)",
			L"Aufnahme- und Vorschau-FPS. Nach PC wählen (höher = flüssiger)",
			L"FPS de gravação e prévia. Escolha pelo PC (mais alto = mais suave)",
			L"Opname- en voorbeeld-FPS. Kies naar PC (hoger = vloeiender)",
			L"FPS nagrywania i podglądu. Dobierz do PC (wyższe = płynniej)",
			L"Kayıt ve önizleme FPS. PC’ye göre seçin (yüksek = akıcı, ağır)"));
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
	EnableComposeUi(m_mode.GetCurSel() == SC_MODE_WINDOWS);
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
	static const int fpsTab[] = { 10, 15, 20, 24, 30, 60 };
	int sel = m_fps.GetSafeHwnd() ? m_fps.GetCurSel() : -1;
	if (sel < 0 || sel > 5) {
		int f = savedata.cap_fps;
		if (f < 5) f = 15;
		if (f > 60) f = 60;
		return f;
	}
	return fpsTab[sel];
}

void CScreenCaptureDlg::ApplyPreviewTimer()
{
	if (!GetSafeHwnd()) return;
	int fps = CurrentPreviewFps();
	if (fps < 5) fps = 5;
	if (fps > 60) fps = 60;
	UINT ms = (UINT)(1000 / fps);
	if (ms < 16) ms = 16; // ~60fps上限でも過負荷にならない程度
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
	if (m_layerCnt > 0 && m_mode.GetCurSel() != SC_MODE_WINDOWS) {
		m_mode.SetCurSel(SC_MODE_WINDOWS);
		EnableComposeUi(TRUE);
	}
	// 録画直前にキャンバス内へ再フィット（見切れ防止）
	if (m_layerCnt > 0)
		FitAllLayersIntoCanvas();
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

	m_fpsVal = CurrentPreviewFps();
	if (m_fpsVal < 5) m_fpsVal = 15;
	if (m_fpsVal > 60) m_fpsVal = 60;
	m_withAudio = m_audio.GetCheck() ? TRUE : FALSE;
	m_withMic = m_mic.GetCheck() ? TRUE : FALSE;
	m_outPath = path;
	m_path.SetWindowText(path);

	EnterCriticalSection(&m_snapCs);
	m_recSnap = snap;
	LeaveCriticalSection(&m_snapCs);

	// 録画スレッドがループバックを独占するのでプレビュー監視を止める
	StopPeakMonitor();

	InterlockedExchange(&m_stop, 0);
	InterlockedExchange(&m_lastHr, S_OK);
	InterlockedExchange(&m_frameCnt, 0);
	InterlockedExchange(&m_encFpsX10, 0);
	m_startTick = GetTickCount();
	m_everStarted = TRUE;

	uintptr_t th = _beginthreadex(NULL, 0, CaptureThread, this, 0, NULL);
	if (!th) {
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
	m_status.SetWindowText(LL14(
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
	InterlockedExchange(&m_run, 0);
	const BOOL uiAlive = (GetSafeHwnd() != NULL);
	if (uiAlive && IsIconic())
		ShowWindow(SW_RESTORE);
	const HRESULT capHr = (HRESULT)InterlockedCompareExchange(&m_lastHr, 0, 0);
	const LONG frames = InterlockedCompareExchange(&m_frameCnt, 0, 0);

	if (uiAlive) {
		SetRecordingUi(FALSE);
		if (SUCCEEDED(capHr) && frames > 0) {
			CString msg;
			msg.Format(LL14(
				L"保存しました:\n%s", L"Saved:\n%s", L"Enregistré:\n%s", L"Salvato:\n%s",
				L"Guardado:\n%s", L"저장됨:\n%s", L"已保存:\n%s", L"تم الحفظ:\n%s",
				L"Сохранено:\n%s", L"Gespeichert:\n%s", L"Guardado:\n%s", L"Opgeslagen:\n%s",
				L"Zapisano:\n%s", L"Kaydedildi:\n%s"), (LPCTSTR)m_outPath);
			m_status.SetWindowText(msg);
		} else if (FAILED(capHr)) {
			CString msg;
			msg.Format(LL14(
				L"録画に失敗しました (HRESULT=0x%08X)。",
				L"Capture failed (HRESULT=0x%08X).",
				L"Échec de la capture (HRESULT=0x%08X).",
				L"Cattura non riuscita (HRESULT=0x%08X).",
				L"Error de captura (HRESULT=0x%08X).",
				L"캡처 실패 (HRESULT=0x%08X).",
				L"捕获失败 (HRESULT=0x%08X)。",
				L"فشل الالتقاط (HRESULT=0x%08X).",
				L"Ошибка захвата (HRESULT=0x%08X).",
				L"Aufnahme fehlgeschlagen (HRESULT=0x%08X).",
				L"Falha na captura (HRESULT=0x%08X).",
				L"Opname mislukt (HRESULT=0x%08X).",
				L"Przechwytywanie nie powiodło się (HRESULT=0x%08X).",
				L"Yakalama başarısız (HRESULT=0x%08X)."), (unsigned)capHr);
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
	if (uiAlive)
		StartPeakMonitor();
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

	// プレビュー中は常にシステム(ループバック=演奏込み)とマイクを監視
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

	{
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
	LONGLONG videoRt = 0;
	LONGLONG audioRt = 0;

	HRESULT hr = MFStartup(MF_VERSION);
	if (FAILED(hr)) { InterlockedExchange(&self->m_lastHr, hr); goto done; }

	if (!ScComposeFrame(frame, snap)) {
		InterlockedExchange(&self->m_lastHr, E_FAIL);
		goto done;
	}

	{
		IMFAttributes* attrs = NULL;
		if (SUCCEEDED(MFCreateAttributes(&attrs, 2)) && attrs) {
			attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
			attrs->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
			hr = MFCreateSinkWriterFromURL(self->m_outPath, NULL, attrs, &writer);
			attrs->Release();
		} else {
			hr = MFCreateSinkWriterFromURL(self->m_outPath, NULL, NULL, &writer);
		}
	}
	if (FAILED(hr) || !writer) {
		InterlockedExchange(&self->m_lastHr, FAILED(hr) ? hr : E_FAIL);
		goto done;
	}

	hr = ScAddVideoStream(writer, &videoIdx, frame.w, frame.h, fps);
	if (FAILED(hr)) {
		InterlockedExchange(&self->m_lastHr, hr);
		goto done;
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
					if (SUCCEEDED(hr))
						haveAudio = TRUE;
				}
			}
		}
	}

	hr = writer->BeginWriting();
	if (FAILED(hr)) {
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
				BOOL haveFrame = ScComposeFrame(frame, snap);
				if (!haveFrame && frame.bits)
					haveFrame = TRUE;
				if (!haveFrame) {
					if (ScFrameAlloc(frame, snap.canvasW, snap.canvasH)) {
						ScFrameClear(frame, RGB(16, 16, 20));
						haveFrame = TRUE;
					}
				}
				if (haveFrame) {
					hr = ScWriteVideoSample(writer, videoIdx, frame, videoRt, dur);
					if (FAILED(hr) && hr != MF_E_TRANSFORM_NEED_MORE_INPUT) {
						InterlockedExchange(&self->m_lastHr, hr);
						writeFail = TRUE;
						break;
					}
					videoRt += dur;
					InterlockedIncrement(&self->m_frameCnt);
					// プレビュー用キャッシュへ共有（UI 側の二重 WGC を避ける）
					if (self->m_snapCsInit) {
						EnterCriticalSection(&self->m_snapCs);
						if (self->m_cacheBits && self->m_cacheW == frame.w && self->m_cacheH == frame.h
							&& frame.bits && frame.stride == frame.w * 4) {
							const size_t nbytes = (size_t)frame.stride * (size_t)frame.h;
							memcpy(self->m_cacheBits, frame.bits, nbytes);
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
	MFShutdown();
	if (SUCCEEDED(hrCo)) CoUninitialize();
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
	if (GetSafeHwnd())
		DestroyWindow();
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
		if (m_path.GetSafeHwnd() && ::GetFocus() == m_path.GetSafeHwnd())
			m_path.RepaintClient();
	}
	CCustomBlurDialogBase::OnTimer(nIDEvent);
}

void CScreenCaptureDlg::OnDestroy()
{
	KillTimer(SC_TIMER_PREV);
	KillTimer(SC_TIMER_UI);
	StopPeakMonitor();
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
