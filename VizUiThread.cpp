#include "stdafx.h"
#include "VizUiThread.h"

namespace {

const UINT kInvokeMsg = WM_APP + 1;
const wchar_t kSinkClass[] = L"OggVizUiSink";

struct PostedFn {
	std::function<void()> fn;
};

LRESULT CALLBACK SinkProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	if (m == kInvokeMsg) {
		if (w) {
			PostedFn* box = reinterpret_cast<PostedFn*>(l);
			if (box) {
				if (box->fn)
					box->fn();
				delete box;
			}
		}
		else {
			const std::function<void()>* fn = reinterpret_cast<const std::function<void()>*>(l);
			if (fn && *fn)
				(*fn)();
		}
		return 0;
	}
	return ::DefWindowProcW(h, m, w, l);
}

HWND CreateSink()
{
	static volatile LONG s_registered = 0;
	if (InterlockedCompareExchange(&s_registered, 0, 0) == 0) {
		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof(wc);
		wc.lpfnWndProc = SinkProc;
		wc.hInstance = AfxGetInstanceHandle();
		wc.lpszClassName = kSinkClass;
		if (::RegisterClassExW(&wc) || ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
			InterlockedExchange(&s_registered, 1);
	}
	return ::CreateWindowExW(0, kSinkClass, L"", 0, 0, 0, 0, 0,
		HWND_MESSAGE, NULL, AfxGetInstanceHandle(), NULL);
}

// m_pMainWnd stays NULL: the thread must not route messages into COggDlg.
class CVizUiThread : public CWinThread
{
public:
	CVizUiThread()
	{
		m_bAutoDelete = FALSE;
		m_ready = ::CreateEventW(NULL, TRUE, FALSE, NULL);
	}
	virtual ~CVizUiThread()
	{
		if (m_ready)
			::CloseHandle(m_ready);
	}
	virtual BOOL InitInstance()
	{
		m_sink = CreateSink();
		if (m_ready)
			::SetEvent(m_ready);
		return TRUE;
	}
	virtual int ExitInstance()
	{
		if (m_sink && ::IsWindow(m_sink))
			::DestroyWindow(m_sink);
		m_sink = NULL;
		return CWinThread::ExitInstance();
	}

	HANDLE m_ready = NULL;
	HWND m_sink = NULL;
};

struct Slot {
	CVizUiThread* th = nullptr;
	DWORD tid = 0;
	HWND sink = NULL;
};

Slot s_slots[VIZ_UI_SLOTS];
CRITICAL_SECTION s_cs;
INIT_ONCE s_csOnce = INIT_ONCE_STATIC_INIT;
volatile LONG s_shutdown = 0;
DWORD s_mainTid = 0;
HWND s_mainSink = NULL;

BOOL CALLBACK InitCs(PINIT_ONCE, PVOID, PVOID*)
{
	InitializeCriticalSection(&s_cs);
	return TRUE;
}

void Lock()
{
	InitOnceExecuteOnce(&s_csOnce, InitCs, NULL, NULL);
	EnterCriticalSection(&s_cs);
}

void Unlock()
{
	LeaveCriticalSection(&s_cs);
}

bool ThreadAlive(const Slot& s)
{
	return s.th && s.th->m_hThread
		&& ::WaitForSingleObject(s.th->m_hThread, 0) == WAIT_TIMEOUT
		&& s.sink && ::IsWindow(s.sink);
}

HWND EnsureSlot(int slot, DWORD* outTid)
{
	if (slot < 0 || slot >= VIZ_UI_SLOTS)
		return NULL;
	Lock();
	Slot& s = s_slots[slot];
	if (s.th && !ThreadAlive(s)) {
		if (s.th->m_hThread && ::WaitForSingleObject(s.th->m_hThread, 0) == WAIT_OBJECT_0)
			delete s.th;
		s = Slot();
	}
	if (!s.th && InterlockedCompareExchange(&s_shutdown, 0, 0) == 0) {
		CVizUiThread* th = new CVizUiThread();
		if (th->CreateThread(CREATE_SUSPENDED, 4u * 1024u * 1024u)) {
			th->ResumeThread();
			if (th->m_ready)
				::WaitForSingleObject(th->m_ready, 5000);
			if (th->m_sink) {
				s.th = th;
				s.tid = th->m_nThreadID;
				s.sink = th->m_sink;
			}
			else {
				::PostThreadMessageW(th->m_nThreadID, WM_QUIT, 0, 0);
				if (::WaitForSingleObject(th->m_hThread, 2000) == WAIT_OBJECT_0)
					delete th;
			}
		}
		else {
			delete th;
		}
	}
	const HWND sink = s.sink;
	if (outTid)
		*outTid = s.tid;
	Unlock();
	return sink;
}

HWND FindSinkByTid(DWORD tid)
{
	HWND sink = NULL;
	Lock();
	for (int i = 0; i < VIZ_UI_SLOTS; ++i) {
		if (s_slots[i].th && s_slots[i].tid == tid) {
			sink = s_slots[i].sink;
			break;
		}
	}
	Unlock();
	return sink;
}

} // namespace

namespace VizUi {

void InitMainThread()
{
	s_mainTid = ::GetCurrentThreadId();
	if (!s_mainSink || !::IsWindow(s_mainSink))
		s_mainSink = CreateSink();
}

bool IsMainThread()
{
	return s_mainTid == 0 || ::GetCurrentThreadId() == s_mainTid;
}

void Invoke(int slot, const std::function<void()>& fn)
{
	if (!fn)
		return;
	DWORD tid = 0;
	const HWND sink = EnsureSlot(slot, &tid);
	if (!sink || tid == ::GetCurrentThreadId()) {
		fn();
		return;
	}
	::SendMessageW(sink, kInvokeMsg, 0, reinterpret_cast<LPARAM>(&fn));
}

bool MarshalToOwner(HWND hwnd, const std::function<void()>& fn)
{
	if (!hwnd || !fn || !::IsWindow(hwnd))
		return false;
	const DWORD tid = ::GetWindowThreadProcessId(hwnd, NULL);
	if (tid == 0 || tid == ::GetCurrentThreadId())
		return false;
	const HWND sink = FindSinkByTid(tid);
	if (!sink)
		return false;
	::SendMessageW(sink, kInvokeMsg, 0, reinterpret_cast<LPARAM>(&fn));
	return true;
}

bool PostToMainIfOff(std::function<void()> fn)
{
	if (!fn || s_mainTid == 0 || ::GetCurrentThreadId() == s_mainTid)
		return false;
	if (!s_mainSink || !::IsWindow(s_mainSink))
		return false;
	PostedFn* box = new PostedFn{ std::move(fn) };
	if (!::PostMessageW(s_mainSink, kInvokeMsg, 1, reinterpret_cast<LPARAM>(box))) {
		fn = std::move(box->fn);
		delete box;
		return false;
	}
	return true;
}

void Shutdown()
{
	InterlockedExchange(&s_shutdown, 1);
	Slot snap[VIZ_UI_SLOTS];
	Lock();
	for (int i = 0; i < VIZ_UI_SLOTS; ++i) {
		snap[i] = s_slots[i];
		s_slots[i] = Slot();
	}
	Unlock();
	for (int i = 0; i < VIZ_UI_SLOTS; ++i) {
		CVizUiThread* th = snap[i].th;
		if (!th)
			continue;
		// static: a timed-out send is still delivered later.
		static const std::function<void()> s_quit = []() { ::PostQuitMessage(0); };
		if (snap[i].sink && ::IsWindow(snap[i].sink))
			::SendMessageTimeoutW(snap[i].sink, kInvokeMsg, 0, reinterpret_cast<LPARAM>(&s_quit),
				SMTO_NORMAL, 1000, NULL);
		else
			::PostThreadMessageW(snap[i].tid, WM_QUIT, 0, 0);
		if (th->m_hThread && ::WaitForSingleObject(th->m_hThread, 2000) == WAIT_OBJECT_0)
			delete th;
	}
}

} // namespace VizUi
