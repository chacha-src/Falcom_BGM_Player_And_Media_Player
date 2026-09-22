#include "stdafx.h"
#include "ogg.h"
#include "oggDlg.h"
#include "Soft3DLoop.h"
#include "Soft3DMazeDlg.h"
#include "Soft3DRaceDlg.h"
#include <mmsystem.h>

extern COggDlg* og;
extern int tempo;
extern int pitch;
extern int playf;
extern int plf;

static CRITICAL_SECTION s_dx;
static volatile LONG s_dxState = 0; // 0=none 1=initing 2=ready
static volatile LONG s_run = 0;
static volatile LONG s_dxReinit = 0;
static volatile LONG s_deferPresent = 0;
static CWinThread* s_thr = NULL;

static void Soft3DDxEnsureCs()
{
	for (;;) {
		const LONG st = InterlockedCompareExchange(&s_dxState, 1, 0);
		if (st == 0) {
			InitializeCriticalSection(&s_dx);
			InterlockedExchange(&s_dxState, 2);
			return;
		}
		if (st == 2)
			return;
		Sleep(0);
	}
}

void Soft3DDxLock()
{
	Soft3DDxEnsureCs();
	EnterCriticalSection(&s_dx);
}

void Soft3DDxUnlock()
{
	if (InterlockedCompareExchange(&s_dxState, 2, 2) == 2)
		LeaveCriticalSection(&s_dx);
}

void Soft3DDeferPresent(int on)
{
	InterlockedExchange(&s_deferPresent, on ? 1 : 0);
}

int Soft3DPresentShouldSkip()
{
	return InterlockedCompareExchange(&s_deferPresent, 0, 0) != 0 ? 1 : 0;
}

int Soft3DLoopStopping()
{
	return InterlockedCompareExchange(&s_run, 0, 0) == 0 ? 1 : 0;
}

void Soft3DPostPlayback(int op, int arg)
{
	if (op == S3PB_TEMPO) {
		int pct = arg;
		if (pct < 25) pct = 25;
		if (pct > 200) pct = 200;
		tempo = pct * 2;
		arg = pct;
	} else if (op == S3PB_PITCH) {
		if (arg < 0) arg = 0;
		if (arg > 400) arg = 400;
		pitch = arg;
	}
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->PostMessage(WM_OGG_S3_PLAYBACK, (WPARAM)op, (LPARAM)arg);
}

void Soft3DPostPlayIfStopped()
{
	if (playf == 0 && plf == 0)
		Soft3DPostPlayback(S3PB_PLAY, 0);
}

void Soft3DPostDxReinit(HWND hwnd)
{
	if (!hwnd || !::IsWindow(hwnd))
		return;
	if (InterlockedCompareExchange(&s_dxReinit, 1, 0) != 0)
		return;
	if (!::PostMessage(hwnd, WM_S3_DX_REINIT, 0, 0))
		InterlockedExchange(&s_dxReinit, 0);
}

void Soft3DDxReinitAck()
{
	InterlockedExchange(&s_dxReinit, 0);
}

void Soft3DPostStatusText(HWND hwnd, volatile LONG* posted, LPCTSTR text)
{
	if (!hwnd || !::IsWindow(hwnd) || !posted || !text)
		return;
	const size_t n = _tcslen(text) + 1;
	TCHAR* p = (TCHAR*)malloc(n * sizeof(TCHAR));
	if (!p)
		return;
	_tcscpy_s(p, n, text);
	if (InterlockedCompareExchange(posted, 1, 0) != 0) {
		free(p);
		return;
	}
	if (!::PostMessage(hwnd, WM_S3_STATUS_TEXT, 0, (LPARAM)p)) {
		InterlockedExchange(posted, 0);
		free(p);
	}
}

static UINT Soft3DLoopProc(LPVOID)
{
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
	while (InterlockedCompareExchange(&s_run, 1, 1) == 1) {
		const DWORD t0 = timeGetTime();
		Soft3DMazeOnTimerp();
		Soft3DRaceOnTimerp();
		if (InterlockedCompareExchange(&s_run, 1, 1) != 1)
			break;
		const DWORD dt = timeGetTime() - t0;
		if (dt < 15u)
			Sleep(15u - dt);
		else
			Sleep(1);
	}
	return 0;
}

void Soft3DLoopEnsure()
{
	Soft3DDxEnsureCs();
	if (s_thr && InterlockedCompareExchange(&s_run, 1, 1) == 1)
		return;
	if (s_thr) {
		InterlockedExchange(&s_run, 0);
		if (WaitForSingleObject(s_thr->m_hThread, 1200) != WAIT_OBJECT_0)
			return;
		delete s_thr;
		s_thr = NULL;
	}
	InterlockedExchange(&s_run, 1);
	/* PE 既定スタックは 70MB 予約。追加スレッドがそれを踏むと x86 VA が尽き、
	   CEmu(PC98/88/Neo) と DXGI モニタが同時に死ぬ。ゲームループは 1MB で足りる。 */
	s_thr = AfxBeginThread((AFX_THREADPROC)Soft3DLoopProc, NULL,
		THREAD_PRIORITY_NORMAL, 1024u * 1024u, CREATE_SUSPENDED);
	if (!s_thr) {
		InterlockedExchange(&s_run, 0);
		return;
	}
	s_thr->m_bAutoDelete = FALSE;
	s_thr->ResumeThread();
}

void Soft3DLoopRequestStop()
{
	InterlockedExchange(&s_run, 0);
}

void Soft3DLoopStopJoin()
{
	InterlockedExchange(&s_run, 0);
	Soft3DDeferPresent(1);
	if (!s_thr) {
		Soft3DDeferPresent(0);
		return;
	}
	// PAINT/TIMER を汲むと DXGI Present 中のスワップチェーンへ WM_PAINT が入り
	// UI とループが Soft3DDxLock で突き当たる。描画ポンプ無しで待つ。
	const DWORD w = WaitForSingleObject(s_thr->m_hThread, 1200);
	if (w != WAIT_OBJECT_0)
		return;
	delete s_thr;
	s_thr = NULL;
	Soft3DDeferPresent(0);
}
