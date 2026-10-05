#include "stdafx.h"
#include "UiTickPump.h"
#include <dxgi.h>
#include <d3d11.h>
#include <dwmapi.h>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

#ifndef DXGI_ADAPTER_FLAG_SOFTWARE
#define DXGI_ADAPTER_FLAG_SOFTWARE 2
#endif

extern volatile LONG g_appExiting;

enum { kHubMax = 24 };

static CRITICAL_SECTION s_cs;
static LONG s_csState = 0; // 0 none, 1 initing, 2 ready
static UiTickPump* s_pumps[kHubMax];
static int s_nPumps = 0;
static HANDLE s_hubThread = NULL;
static HANDLE s_hubStop = NULL;
static HANDLE s_frameEvent = NULL; // auto-reset, TheadLoop waiter
static HANDLE s_waitTimer = NULL;
static IDXGIOutput* s_output = NULL;
static ID3D11Device* s_d3d = NULL;
static ID3D11DeviceContext* s_d3dCtx = NULL;
static volatile LONG s_hubRun = 0;
static volatile LONG s_dxgiOk = 0;
static HWND s_bindHwnd = NULL;
static HMONITOR s_boundMon = NULL;
static LARGE_INTEGER s_qpcFreq = {};
static LONGLONG s_period = 0;
static int s_dxgiInstant = 0;
static int s_rebindCtr = 0;

static void HubCsEnter()
{
	if (InterlockedCompareExchange(&s_csState, 1, 0) == 0) {
		InitializeCriticalSection(&s_cs);
		InterlockedExchange(&s_csState, 2);
	}
	else {
		while (InterlockedCompareExchange(&s_csState, 2, 2) != 2)
			Sleep(0);
	}
	EnterCriticalSection(&s_cs);
}

static void HubCsLeave()
{
	LeaveCriticalSection(&s_cs);
}

static void HubReleaseDxgi()
{
	InterlockedExchange(&s_dxgiOk, 0);
	if (s_output) {
		s_output->Release();
		s_output = NULL;
	}
	if (s_d3dCtx) {
		s_d3dCtx->Release();
		s_d3dCtx = NULL;
	}
	if (s_d3d) {
		s_d3d->Release();
		s_d3d = NULL;
	}
	s_boundMon = NULL;
}

static HMONITOR HubWantMon()
{
	HWND h = s_bindHwnd;
	if (h && ::IsWindow(h))
		return MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
	return MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
}

static bool HubInitDxgi()
{
#ifdef KBSASAMI_HOST_BUILD
	/* 本家ホストは WaitForVBlank 中に窓を壊すと DXGI で戻らない */
	return false;
#else
	HubReleaseDxgi();
	HMODULE dxgi = GetModuleHandleW(L"dxgi.dll");
	if (!dxgi)
		dxgi = LoadLibraryW(L"dxgi.dll");
	if (!dxgi)
		return false;
	typedef HRESULT(WINAPI* PFN_CreateFactory1)(REFIID, void**);
	PFN_CreateFactory1 create1 = (PFN_CreateFactory1)GetProcAddress(dxgi, "CreateDXGIFactory1");
	if (!create1)
		return false;
	IDXGIFactory1* fac = NULL;
	if (FAILED(create1(__uuidof(IDXGIFactory1), (void**)&fac)) || !fac)
		return false;

	const HMONITOR want = HubWantMon();
	IDXGIAdapter1* pickAd = NULL;
	IDXGIOutput* pickOut = NULL;
	IDXGIAdapter1* fallbackAd = NULL;
	IDXGIOutput* fallbackOut = NULL;

	for (UINT a = 0; a < 8; ++a) {
		IDXGIAdapter1* ad = NULL;
		const HRESULT er = fac->EnumAdapters1(a, &ad);
		if (er == DXGI_ERROR_NOT_FOUND)
			break;
		if (FAILED(er) || !ad)
			continue;
		DXGI_ADAPTER_DESC1 adesc;
		ZeroMemory(&adesc, sizeof(adesc));
		if (SUCCEEDED(ad->GetDesc1(&adesc)) && (adesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
			ad->Release();
			continue;
		}
		for (UINT o = 0; o < 8 && !pickOut; ++o) {
			IDXGIOutput* out = NULL;
			const HRESULT eo = ad->EnumOutputs(o, &out);
			if (eo == DXGI_ERROR_NOT_FOUND)
				break;
			if (FAILED(eo) || !out)
				continue;
			DXGI_OUTPUT_DESC od;
			ZeroMemory(&od, sizeof(od));
			if (FAILED(out->GetDesc(&od)) || !od.AttachedToDesktop) {
				out->Release();
				continue;
			}
			if (!fallbackOut) {
				fallbackAd = ad;
				fallbackAd->AddRef();
				fallbackOut = out;
				fallbackOut->AddRef();
			}
			if (!want || od.Monitor == want) {
				pickAd = ad;
				pickAd->AddRef();
				pickOut = out;
				pickOut->AddRef();
			}
			out->Release();
		}
		ad->Release();
		if (pickOut)
			break;
	}
	fac->Release();

	if (!pickOut) {
		pickAd = fallbackAd;
		pickOut = fallbackOut;
		fallbackAd = NULL;
		fallbackOut = NULL;
	}
	if (fallbackAd) fallbackAd->Release();
	if (fallbackOut) fallbackOut->Release();

	if (!pickOut) {
		if (pickAd) pickAd->Release();
		return false;
	}

	s_output = pickOut;
	s_boundMon = want;

	HMODULE d3d11 = GetModuleHandleW(L"d3d11.dll");
	if (!d3d11)
		d3d11 = LoadLibraryW(L"d3d11.dll");
	if (d3d11 && pickAd) {
		typedef HRESULT(WINAPI* PFN_D3D11CreateDevice)(
			IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
			const D3D_FEATURE_LEVEL*, UINT, UINT,
			ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
		PFN_D3D11CreateDevice createDev =
			(PFN_D3D11CreateDevice)GetProcAddress(d3d11, "D3D11CreateDevice");
		if (createDev) {
			D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
			createDev(pickAd, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0,
				NULL, 0, D3D11_SDK_VERSION, &s_d3d, &fl, &s_d3dCtx);
		}
	}
	if (pickAd)
		pickAd->Release();

	s_dxgiInstant = 0;
	InterlockedExchange(&s_dxgiOk, s_output ? 1 : 0);
	return s_output != NULL;
#endif
}

static bool HubQueryDwm(LONGLONG* lastVblank, LONGLONG* period)
{
	if (!lastVblank || !period)
		return false;
	HMODULE dwm = GetModuleHandleW(L"dwmapi.dll");
	if (!dwm)
		dwm = LoadLibraryW(L"dwmapi.dll");
	if (!dwm)
		return false;
	typedef HRESULT(WINAPI* PFN_GetTi)(HWND, DWM_TIMING_INFO*);
	typedef HRESULT(WINAPI* PFN_IsComp)(BOOL*);
	PFN_GetTi getTi = (PFN_GetTi)GetProcAddress(dwm, "DwmGetCompositionTimingInfo");
	PFN_IsComp isComp = (PFN_IsComp)GetProcAddress(dwm, "DwmIsCompositionEnabled");
	BOOL comp = FALSE;
	if (!isComp || !getTi || FAILED(isComp(&comp)) || !comp)
		return false;
	DWM_TIMING_INFO ti;
	ZeroMemory(&ti, sizeof(ti));
	ti.cbSize = sizeof(ti);
	if (FAILED(getTi(NULL, &ti)))
		return false;
	if (ti.qpcRefreshPeriod < 1000 || ti.qpcVBlank == 0)
		return false;
	*lastVblank = (LONGLONG)ti.qpcVBlank;
	*period = (LONGLONG)ti.qpcRefreshPeriod;
	return true;
}

static LONGLONG HubDefaultPeriod()
{
	if (s_qpcFreq.QuadPart < 1)
		QueryPerformanceFrequency(&s_qpcFreq);
	LONGLONG period = (s_qpcFreq.QuadPart > 0) ? (s_qpcFreq.QuadPart / 60) : 1;
	LONGLONG vb = 0, p = 0;
	if (HubQueryDwm(&vb, &p) && p > 1000)
		period = p;
	if (period < 1)
		period = 1;
	return period;
}

static void HubWaitTimer(HANDLE stopEvent, LONGLONG waitTicks)
{
	if (waitTicks < 1)
		return;
	if (s_qpcFreq.QuadPart < 1)
		QueryPerformanceFrequency(&s_qpcFreq);
	if (s_period < 1)
		s_period = HubDefaultPeriod();
	if (s_period > 0 && waitTicks > s_period * 2)
		waitTicks = s_period * 2;

	if (s_waitTimer && s_qpcFreq.QuadPart > 0) {
		LARGE_INTEGER due;
		due.QuadPart = -(LONGLONG)((waitTicks * 10000000ULL + (ULONGLONG)s_qpcFreq.QuadPart / 2)
			/ (ULONGLONG)s_qpcFreq.QuadPart);
		if (due.QuadPart >= 0)
			due.QuadPart = -10000; // 1ms
		if (SetWaitableTimer(s_waitTimer, &due, 0, NULL, NULL, FALSE)) {
			HANDLE w[2];
			DWORD n = 1;
			w[0] = s_waitTimer;
			if (stopEvent) {
				w[1] = stopEvent;
				n = 2;
			}
			const DWORD capMs = (s_period > 0 && s_qpcFreq.QuadPart > 0)
				? (DWORD)((s_period * 2 * 1000) / s_qpcFreq.QuadPart) + 4
				: 36;
			WaitForMultipleObjects(n, w, FALSE, capMs);
			return;
		}
	}
	DWORD ms = 1;
	if (s_qpcFreq.QuadPart > 0)
		ms = (DWORD)((waitTicks * 1000 + s_qpcFreq.QuadPart / 2) / s_qpcFreq.QuadPart);
	if (ms < 1) ms = 1;
	if (ms > 32) ms = 32;
	if (stopEvent)
		WaitForSingleObject(stopEvent, ms);
	else
		Sleep(ms);
}

static void HubWaitDwmPhase(HANDLE stopEvent)
{
	if (s_qpcFreq.QuadPart < 1)
		QueryPerformanceFrequency(&s_qpcFreq);
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);

	LONGLONG lastVb = 0, period = 0;
	if (!HubQueryDwm(&lastVb, &period)) {
		period = HubDefaultPeriod();
		lastVb = now.QuadPart - (now.QuadPart % period);
	}
	s_period = period;
	if (s_period < 1)
		s_period = 1;

	LONGLONG next = lastVb + s_period;
	while (next <= now.QuadPart)
		next += s_period;
	const LONGLONG slop = (s_qpcFreq.QuadPart > 0) ? (s_qpcFreq.QuadPart / 2500) : 1; // ~0.4ms
	if (next - now.QuadPart < slop)
		next += s_period;
	HubWaitTimer(stopEvent, next - now.QuadPart);
}

static void HubWaitVblank(HANDLE stopEvent)
{
	if (stopEvent && WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0)
		return;

	if (++s_rebindCtr >= 90) {
		s_rebindCtr = 0;
		const HMONITOR want = HubWantMon();
		if (want != s_boundMon)
			HubInitDxgi();
	}

	if (InterlockedCompareExchange(&s_dxgiOk, 0, 0) != 0 && s_output) {
		LARGE_INTEGER t0, t1;
		QueryPerformanceCounter(&t0);
		const HRESULT hr = s_output->WaitForVBlank();
		QueryPerformanceCounter(&t1);
		if (SUCCEEDED(hr)) {
			const LONGLONG elapsed = t1.QuadPart - t0.QuadPart;
			const LONGLONG inst = (s_qpcFreq.QuadPart > 0) ? (s_qpcFreq.QuadPart / 5000) : 2000; // 0.2ms
			if (elapsed >= inst) {
				s_dxgiInstant = 0;
				return;
			}
			// 即戻りが続くのは WaitForVBlank が待っていないドライバ。DWM 位相待ちへ。
			if (++s_dxgiInstant >= 3)
				HubReleaseDxgi();
		}
		else {
			HubReleaseDxgi();
		}
	}

	HubWaitDwmPhase(stopEvent);
}

static void HubPostPumps()
{
	UiTickPump* snap[kHubMax];
	int n = 0;
	HubCsEnter();
	n = s_nPumps;
	if (n > kHubMax)
		n = kHubMax;
	for (int i = 0; i < n; ++i)
		snap[i] = s_pumps[i];
	HubCsLeave();
	for (int i = 0; i < n; ++i) {
		if (snap[i])
			snap[i]->TryPostTick();
	}
}

static DWORD WINAPI HubThreadProc(LPVOID)
{
	CoInitializeEx(NULL, COINIT_MULTITHREADED);
	QueryPerformanceFrequency(&s_qpcFreq);
	s_period = HubDefaultPeriod();
	HubInitDxgi();
	s_waitTimer = CreateWaitableTimerExW(NULL, NULL,
		CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	if (!s_waitTimer)
		s_waitTimer = CreateWaitableTimerW(NULL, TRUE, NULL);

	InterlockedExchange(&s_hubRun, 1);
	for (;;) {
		if (s_hubStop && WaitForSingleObject(s_hubStop, 0) == WAIT_OBJECT_0)
			break;
		if (InterlockedCompareExchange(&g_appExiting, 0, 0) != 0)
			break;
		HubWaitVblank(s_hubStop);
		if (s_hubStop && WaitForSingleObject(s_hubStop, 0) == WAIT_OBJECT_0)
			break;
		if (s_frameEvent)
			SetEvent(s_frameEvent);
		HubPostPumps();
	}

	if (s_waitTimer) {
		CloseHandle(s_waitTimer);
		s_waitTimer = NULL;
	}
	HubReleaseDxgi();
	InterlockedExchange(&s_hubRun, 0);
	CoUninitialize();
	return 0;
}

void UiTickPump::EnsureHub()
{
	HubCsEnter();
	if (!s_frameEvent)
		s_frameEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
	if (!s_hubStop)
		s_hubStop = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (!s_hubThread && s_hubStop) {
		ResetEvent(s_hubStop);
		s_hubThread = CreateThread(NULL, 0, HubThreadProc, NULL, 0, NULL);
		if (s_hubThread)
			SetThreadPriority(s_hubThread, THREAD_PRIORITY_ABOVE_NORMAL);
	}
	HubCsLeave();
}

void UiTickPump::BindWindow(HWND hwnd)
{
	s_bindHwnd = hwnd;
}

UiTickPump::UiTickPump()
	: m_hwnd(NULL)
	, m_msg(0)
	, m_posted(0)
	, m_registered(0)
{
}

UiTickPump::~UiTickPump()
{
	Stop();
}

bool UiTickPump::IsRunning()
{
	return InterlockedCompareExchange(&m_registered, 0, 0) != 0
		&& InterlockedCompareExchange(&s_hubRun, 0, 0) != 0;
}

void UiTickPump::Ack()
{
	InterlockedExchange(&m_posted, 0);
}

bool UiTickPump::TryPostTick()
{
	const HWND hwnd = m_hwnd;
	const UINT msg = m_msg;
	if (!hwnd || !msg || !::IsWindow(hwnd) || !::IsWindowVisible(hwnd) || ::IsIconic(hwnd))
		return false;
	if (InterlockedCompareExchange(&m_posted, 1, 0) != 0)
		return false;
	if (!::PostMessage(hwnd, msg, 0, 0)) {
		InterlockedExchange(&m_posted, 0);
		return false;
	}
	return true;
}

void UiTickPump::Start(HWND hwnd, UINT msg)
{
	Stop();
	if (!hwnd || !msg || !::IsWindow(hwnd))
		return;
	m_hwnd = hwnd;
	m_msg = msg;
	InterlockedExchange(&m_posted, 0);
	BindWindow(hwnd);
	EnsureHub();
	HubCsEnter();
	bool have = false;
	for (int i = 0; i < s_nPumps; ++i) {
		if (s_pumps[i] == this) {
			have = true;
			break;
		}
	}
	if (!have && s_nPumps < kHubMax) {
		s_pumps[s_nPumps++] = this;
		InterlockedExchange(&m_registered, 1);
	}
	HubCsLeave();
}

void UiTickPump::Stop()
{
	const HWND hwnd = m_hwnd;
	const UINT msg = m_msg;
	HubCsEnter();
	for (int i = 0; i < s_nPumps; ++i) {
		if (s_pumps[i] == this) {
			s_pumps[i] = s_pumps[s_nPumps - 1];
			s_pumps[s_nPumps - 1] = NULL;
			--s_nPumps;
			break;
		}
	}
	HubCsLeave();
	InterlockedExchange(&m_registered, 0);
	InterlockedExchange(&m_posted, 0);
	if (hwnd && msg && ::IsWindow(hwnd)) {
		MSG drain;
		while (::PeekMessage(&drain, hwnd, msg, msg, PM_REMOVE)) {}
	}
	m_hwnd = NULL;
	m_msg = 0;
}

void UiTickPump::ShutdownHub()
{
	InterlockedExchange(&g_appExiting, 1);
	HubCsEnter();
	HANDLE th = s_hubThread;
	HANDLE stop = s_hubStop;
	s_hubThread = NULL;
	HubCsLeave();
	if (stop)
		SetEvent(stop);
	if (th) {
		WaitForSingleObject(th, 400);
		CloseHandle(th);
	}
}

void UiTickPump::WaitVblank(HANDLE stopEvent)
{
	EnsureHub();
	HANDLE ev = s_frameEvent;
	if (!ev) {
		HubWaitDwmPhase(stopEvent);
		return;
	}
	DWORD to = 36;
	if (s_period > 0 && s_qpcFreq.QuadPart > 0) {
		to = (DWORD)((s_period * 2 * 1000) / s_qpcFreq.QuadPart) + 8;
		if (to < 20) to = 20;
		if (to > 48) to = 48;
	}
	if (stopEvent) {
		HANDLE w[2] = { ev, stopEvent };
		WaitForMultipleObjects(2, w, FALSE, to);
	}
	else {
		WaitForSingleObject(ev, to);
	}
}
