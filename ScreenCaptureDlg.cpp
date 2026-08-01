// ScreenCaptureDlg.cpp
// 画面キャプチャ → MP4 (H.264 + AAC)
// プライマリ / 全モニタ / ウィンドウ合成(配置・拡大縮小・Z順)

#include "stdafx.h"
#include "ogg.h"
#include "oggDlg.h"
#include "ScreenCaptureDlg.h"
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

static BOOL ScCaptureWindowToBuf(HWND hwnd, ScFrameBuf& fb)
{
	if (!hwnd || !IsWindow(hwnd)) return FALSE;
	RECT wr = {};
	if (!::GetWindowRect(hwnd, &wr)) return FALSE;
	int ww = wr.right - wr.left;
	int wh = wr.bottom - wr.top;
	if (ww < 2 || wh < 2) return FALSE;
	if (!fb.bits || fb.w != (ww & ~1) || fb.h != (wh & ~1)) {
		if (!ScFrameAlloc(fb, ww, wh))
			return FALSE;
	}
	ScFrameClear(fb, RGB(0, 0, 0));
	// PrintWindow が失敗したら BitBlt へ
	BOOL ok = PrintWindow(hwnd, fb.hdc, PW_RENDERFULLCONTENT);
	if (!ok)
		ok = PrintWindow(hwnd, fb.hdc, 0);
	if (!ok) {
		HDC wdc = GetWindowDC(hwnd);
		if (wdc) {
			ok = BitBlt(fb.hdc, 0, 0, fb.w, fb.h, wdc, 0, 0, SRCCOPY);
			ReleaseDC(hwnd, wdc);
		}
	}
	if (!ok) {
		// 最後の手段: 画面上の矩形をコピー (前面にある場合)
		HDC screen = ::GetDC(NULL);
		if (screen) {
			ok = ::BitBlt(fb.hdc, 0, 0, fb.w, fb.h, screen, wr.left, wr.top, SRCCOPY);
			::ReleaseDC(NULL, screen);
		}
	}
	return ok;
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
		SetStretchBltMode(out.hdc, HALFTONE);
		StretchBlt(out.hdc, 0, 0, out.w, out.h, screen, 0, 0, sw, sh, SRCCOPY);
		ReleaseDC(NULL, screen);
		return TRUE;
	}
	if (snap.mode == CScreenCaptureDlg::SC_MODE_VIRTUAL) {
		const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
		const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
		const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
		const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
		HDC screen = GetDC(NULL);
		if (!screen) return FALSE;
		SetStretchBltMode(out.hdc, HALFTONE);
		StretchBlt(out.hdc, 0, 0, out.w, out.h, screen, vx, vy, vw, vh, SRCCOPY);
		ReleaseDC(NULL, screen);
		return TRUE;
	}

	// ウィンドウ合成: 末尾(奥) → 先頭(手前)
	ScFrameBuf winBuf = {};
	for (int i = snap.layerCnt - 1; i >= 0; --i) {
		const CScreenCaptureDlg::Layer& L = snap.layers[i];
		if (!L.hwnd || !IsWindow(L.hwnd) || L.w < 1 || L.h < 1) continue;
		if (!ScCaptureWindowToBuf(L.hwnd, winBuf)) continue;
		SetStretchBltMode(out.hdc, HALFTONE);
		StretchBlt(out.hdc, L.x, L.y, L.w, L.h, winBuf.hdc, 0, 0, winBuf.w, winBuf.h, SRCCOPY);
	}
	ScFrameFree(winBuf);

	// MP画面を別途載せる (レイヤに無い場合)
	if (snap.includeMp && snap.mpHwnd && ::IsWindow(snap.mpHwnd) && snap.mpW > 1 && snap.mpH > 1) {
		BOOL already = FALSE;
		for (int i = 0; i < snap.layerCnt; ++i) {
			if (snap.layers[i].hwnd == snap.mpHwnd) { already = TRUE; break; }
		}
		if (!already) {
			ScFrameBuf mpBuf = {};
			if (ScCaptureWindowToBuf(snap.mpHwnd, mpBuf)) {
				SetStretchBltMode(out.hdc, HALFTONE);
				StretchBlt(out.hdc, snap.mpX, snap.mpY, snap.mpW, snap.mpH,
					mpBuf.hdc, 0, 0, mpBuf.w, mpBuf.h, SRCCOPY);
			}
			ScFrameFree(mpBuf);
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
	if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AVG_BITRATE, (UINT32)(w * h * fps / 4));
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
	, m_thread(NULL)
	, m_uiLocked(FALSE)
	, m_stopping(FALSE)
	, m_everStarted(FALSE)
	, m_picking(FALSE)
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

void CScreenCaptureDlg::ResolveCanvasSize(int& outW, int& outH) const
{
	const int preset = m_canvas.GetCurSel();
	if (preset == 1) { outW = 1280; outH = 720; return; }
	if (preset == 2) { outW = 1920; outH = 1080; return; }
	if (preset == 3) { outW = 1600; outH = 900; return; }
	// 0=自動
	const int mode = m_mode.GetCurSel();
	if (mode == SC_MODE_VIRTUAL) {
		outW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
		outH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
	} else if (mode == SC_MODE_WINDOWS && m_layerCnt > 0) {
		int maxR = 0, maxB = 0;
		for (int i = 0; i < m_layerCnt; ++i) {
			const int r = m_layers[i].x + m_layers[i].w;
			const int b = m_layers[i].y + m_layers[i].h;
			if (r > maxR) maxR = r;
			if (b > maxB) maxB = b;
		}
		outW = maxR > 0 ? maxR : 1280;
		outH = maxB > 0 ? maxB : 720;
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

void CScreenCaptureDlg::BuildComposeSnap(ComposeSnap& out) const
{
	memset(&out, 0, sizeof(out));
	int mode = m_mode.GetCurSel();
	if (mode < 0 || mode > 2) mode = 0;
	out.mode = mode;
	ResolveCanvasSize(out.canvasW, out.canvasH);
	out.layerCnt = 0;
	const BOOL wantMp = (m_includeMp.GetSafeHwnd()
		&& const_cast<CCustomCheckBox&>(m_includeMp).GetCheck());
	out.includeMp = wantMp;
	out.mpHwnd = FindMediaPlayerHwnd();

	if (mode == SC_MODE_WINDOWS) {
		for (int i = 0; i < m_layerCnt && out.layerCnt < SC_LAYER_MAX; ++i) {
			if (!m_layers[i].hwnd || !::IsWindow(m_layers[i].hwnd)) continue;
			out.layers[out.layerCnt] = m_layers[i];
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
	static const int fpsTab[] = { 10, 15, 20, 24, 30 };
	int sel = m_fps.GetCurSel();
	if (sel < 0 || sel > 4) sel = 1;
	savedata.cap_fps = fpsTab[sel];
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
}

void CScreenCaptureDlg::UpdateElapsedUi()
{
	if (!GetSafeHwnd() || !m_time.GetSafeHwnd()) return;
	if (!InterlockedCompareExchange(&m_run, 0, 0)) {
		m_time.SetWindowText(L"");
		return;
	}
	const DWORD ms = GetTickCount() - m_startTick;
	const int sec = (int)(ms / 1000);
	CString t;
	t.Format(L"%02d:%02d  (%ld f)", sec / 60, sec % 60, (long)InterlockedCompareExchange(&m_frameCnt, 0, 0));
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
		s.Format(L"%s[Z%d] %s  (%d,%d %dx%d)",
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
	}
	::GetWindowText(hwnd, L.title, _countof(L.title) - 1);
	if (!L.title[0])
		_tcscpy_s(L.title, isMp ? L"Media Player" : L"(window)");
	m_layerCnt++;
	if (!isMp && m_mode.GetCurSel() != SC_MODE_WINDOWS)
		m_mode.SetCurSel(SC_MODE_WINDOWS);
	EnableComposeUi(TRUE);
	RefreshLayerList();
	m_layer.SetCurSel(m_layerCnt - 1);
	SyncGeoEditsFromSel();
	UpdatePreview();
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
			L"MP画面をレイヤに追加しました。プレビューで配置・拡大縮小できます。",
			L"Added MP as a layer. Drag/resize on the preview.",
			L"MP ajouté. Glissez/redimensionnez dans l'aperçu.",
			L"MP aggiunto. Trascina/ridimensiona nell'anteprima.",
			L"MP añadido. Arrastre/redimensione en la vista previa.",
			L"MP를 레이어에 추가했습니다. 미리보기에서 배치/크기 조절하세요.",
			L"已将MP加入层。可在预览中拖动/缩放。",
			L"تمت إضافة MP. اسحب/غيّر الحجم في المعاينة.",
			L"MP добавлен. Перетаскивайте/масштабируйте в превью.",
			L"MP hinzugefügt. Ziehen/skalieren in der Vorschau.",
			L"MP adicionado. Arraste/redimensione na prévia.",
			L"MP toegevoegd. Sleep/schaal in het voorbeeld.",
			L"Dodano MP. Przeciągaj/skaluj w podglądzie.",
			L"MP eklendi. Önizlemede sürükleyin/ölçekleyin."));
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
	static const int fpsTab[] = { 10, 15, 20, 24, 30 };
	int fpsSel = m_fps.GetCurSel();
	if (fpsSel < 0 || fpsSel > 4) fpsSel = 1;
	const int showFps = fpsTab[fpsSel];

	// 上部情報バー
	CString hud;
	CString modeName;
	if (mode == SC_MODE_PRIMARY) modeName = L"Primary";
	else if (mode == SC_MODE_VIRTUAL) modeName = L"All monitors";
	else modeName = L"Compose";
	hud.Format(L"%s  %dx%d  FPS %d  layers %d",
		(LPCTSTR)modeName, canvasW, canvasH, showFps, m_layerCnt);
	if (InterlockedCompareExchange(&m_run, 0, 0)) {
		CString rec;
		rec.Format(L"  ●REC %ldf", (long)InterlockedCompareExchange(&m_frameCnt, 0, 0));
		hud += rec;
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
			CPen pen(PS_SOLID, selected ? 2 : 1, selected ? RGB(255, 200, 40) : (L.isMp ? RGB(120, 255, 160) : RGB(80, 200, 255)));
			dc.SelectObject(&pen);
			dc.SelectStockObject(NULL_BRUSH);
			dc.Rectangle(&rr);

			CString label;
			label.Format(L"%sZ%d %dx%d  %s", L.isMp ? L"[MP] " : L"", i, L.w, L.h, L.title);
			CRect lr = rr;
			lr.bottom = lr.top + 16;
			if (lr.bottom > rr.bottom) lr.bottom = rr.bottom;
			dc.FillSolidRect(&lr, selected ? RGB(60, 45, 0) : RGB(0, 40, 60));
			dc.SetTextColor(selected ? RGB(255, 230, 120) : RGB(180, 230, 255));
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
		SetStretchBltMode(dc.GetSafeHdc(), HALFTONE);
		StretchBlt(dc.GetSafeHdc(), imageRect.left, imageRect.top, imageRect.Width(), imageRect.Height(),
			m_cacheDc, 0, 0, m_cacheW, m_cacheH, SRCCOPY);
	} else {
		dc.FillSolidRect(&imageRect, RGB(30, 30, 36));
	}
	DrawPreviewHud(dc, imageRect, scale, cw, ch);
}

void CScreenCaptureDlg::UpdatePreview(BOOL forceCompose)
{
	if (!GetSafeHwnd() || !m_preview.GetSafeHwnd()) return;
	if (forceCompose || !m_dragging)
		RefreshComposeCache();
	m_preview.Invalidate(FALSE);
	m_preview.UpdateWindow();
	// OpaqueFixer 経由の再描画を確実に
	::RedrawWindow(m_preview.GetSafeHwnd(), NULL, NULL,
		RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME);
}

BOOL CScreenCaptureDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	CCC_BringDialogToForeground(this);
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
		L"MPの曲を載せる", L"Include MP song", L"Inclure morceau MP", L"Includi brano MP",
		L"Incluir canción MP", L"MP 곡 포함", L"放入MP曲目", L"تضمين أغنية MP",
		L"Включить трек MP", L"MP-Titel einbeziehen", L"Incluir faixa MP", L"MP-nummer opnemen",
		L"Dołącz utwór MP", L"MP parçasını ekle"));
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
		L"プレビュー上で枠をドラッグ／四隅で拡大縮小できます（HUDは録画されません）。",
		L"Drag frames on preview / resize by corners (HUD is not recorded).",
		L"Glissez les cadres / redimensionnez aux coins (HUD non enregistré).",
		L"Trascina i riquadri / ridimensiona agli angoli (HUD non registrato).",
		L"Arrastre marcos / redimensione en esquinas (HUD no se graba).",
		L"미리보기에서 프레임 드래그/모서리 크기조절 (HUD는 녹화 안 됨).",
		L"可在预览中拖动边框/用四角缩放（HUD不会录制）。",
		L"اسحب الإطارات / غيّر الحجم من الزوايا (لا يُسجَّل HUD).",
		L"Перетаскивайте рамки / углы (HUD не записывается).",
		L"Rahmen ziehen / an Ecken skalieren (HUD wird nicht aufgenommen).",
		L"Arraste molduras / redimensione nos cantos (HUD não é gravado).",
		L"Sleep kaders / schaal via hoeken (HUD wordt niet opgenomen).",
		L"Przeciągaj ramki / skaluj rogami (HUD nie jest nagrywany).",
		L"Çerçeveleri sürükleyin / köşelerden ölçekleyin (HUD kayda girmez)."));

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

	static const int fpsTab[] = { 10, 15, 20, 24, 30 };
	int fpsSel = 1;
	for (int i = 0; i < 5; ++i) {
		CString s; s.Format(L"%d", fpsTab[i]);
		m_fps.AddString(s);
		if (savedata.cap_fps == fpsTab[i]) fpsSel = i;
	}
	m_fps.SetCurSel(fpsSel);

	CString path = savedata.cap_last_path;
	if (path.IsEmpty()) {
		wchar_t docs[MAX_PATH] = {};
		if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_MYVIDEO, NULL, 0, docs))) {
			SYSTEMTIME st; GetLocalTime(&st);
			path.Format(L"%s\\capture_%04d%02d%02d_%02d%02d%02d.mp4",
				docs, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
		} else {
			path = L"capture.mp4";
		}
	}
	m_path.SetWindowText(NormalizeOutPath(path));

	RefreshAvailList();
	EnableComposeUi(mode == SC_MODE_WINDOWS);
	if (savedata.cap_include_mp)
		SyncMpLayerFromCheck();

	if (m_tooltip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX)) {
		m_tooltip.Activate(TRUE);
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
	}

	SetTimer(SC_TIMER_PREV, 300, NULL);
	SetTimer(SC_TIMER_UI, 500, NULL);
	UpdatePreview();
	RefreshOpaqueUi();
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
	UpdatePreview();
	PersistUiToSavedata();
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
	PersistUiToSavedata();

	ComposeSnap snap;
	BuildComposeSnap(snap);
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

	static const int fpsTab[] = { 10, 15, 20, 24, 30 };
	int sel = m_fps.GetCurSel();
	if (sel < 0 || sel > 4) sel = 1;
	m_fpsVal = fpsTab[sel];
	m_withAudio = m_audio.GetCheck() ? TRUE : FALSE;
	m_withMic = m_mic.GetCheck() ? TRUE : FALSE;
	m_outPath = path;
	m_path.SetWindowText(path);

	EnterCriticalSection(&m_snapCs);
	m_recSnap = snap;
	LeaveCriticalSection(&m_snapCs);

	InterlockedExchange(&m_stop, 0);
	InterlockedExchange(&m_lastHr, S_OK);
	InterlockedExchange(&m_frameCnt, 0);
	m_startTick = GetTickCount();
	m_everStarted = TRUE;

	uintptr_t th = _beginthreadex(NULL, 0, CaptureThread, this, 0, NULL);
	if (!th) {
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
		L"録画中…", L"Recording…", L"Enregistrement…", L"Registrazione…",
		L"Grabando…", L"녹화 중…", L"录制中…", L"جاري التسجيل…",
		L"Запись…", L"Aufnahme…", L"A gravar…", L"Opnemen…",
		L"Nagrywanie…", L"Kaydediliyor…"));
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

	hr = MFCreateSinkWriterFromURL(self->m_outPath, NULL, NULL, &writer);
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

		while (InterlockedCompareExchange(&self->m_stop, 0, 0) == 0 && !writeFail) {
			LARGE_INTEGER qpc;
			QueryPerformanceCounter(&qpc);
			const LONGLONG elapsedQ = qpc.QuadPart - qpc0.QuadPart;
			if (elapsedQ >= nextFrameQpc) {
				if (ScComposeFrame(frame, snap)) {
					hr = ScWriteVideoSample(writer, videoIdx, frame, videoRt, frameDur);
					if (FAILED(hr) && hr != MF_E_TRANSFORM_NEED_MORE_INPUT) {
						InterlockedExchange(&self->m_lastHr, hr);
						writeFail = TRUE;
						break;
					}
					videoRt += frameDur;
					InterlockedIncrement(&self->m_frameCnt);
				}
				nextFrameQpc += (qpf.QuadPart / fps);
				if (nextFrameQpc < elapsedQ - qpf.QuadPart)
					nextFrameQpc = elapsedQ;
			}

			if (haveAudio && loopCap && mixFmt) {
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
						Sleep(2);
				}

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
								for (UINT32 i = 0; i < n; ++i) {
									float L, R;
									ScSampleToFloat(data + (done + i) * micFmt->nBlockAlign, micFmt, L, R);
									conv[i * 2 + 0] = L;
									conv[i * 2 + 1] = R;
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
							for (UINT32 i = 0; i < n; ++i) {
								float L = 0.f, R = 0.f;
								if (data && !(flags & AUDCLNT_BUFFERFLAGS_SILENT))
									ScSampleToFloat(data + (done + i) * mixFmt->nBlockAlign, mixFmt, L, R);
								fL[i] = L;
								fR[i] = R;
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
			} else {
				Sleep(5);
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

void CScreenCaptureDlg::OnBnClickedClose()
{
	if (InterlockedCompareExchange(&m_run, 0, 0))
		StopRecording();
	PersistUiToSavedata();
	EndDialog(IDCANCEL);
}

void CScreenCaptureDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == SC_TIMER_PREV) {
		if (!m_dragging)
			UpdatePreview(); // 録画中もライブプレビューを継続
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
	OnBnClickedClose();
}

void CScreenCaptureDlg::OnOK()
{
}
