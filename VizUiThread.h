#pragma once
#include <windows.h>
#include <functional>

class CWnd;

// Dedicated UI threads for the heavy visualizer windows (piano roll / analyzer).
// Their WM_PAINT / UiTickPump ticks run on their own thread, never on the main
// UI thread that drives timerp and the banner.
// Windows owned by a viz thread must be created, destroyed and have their
// timers / paint buffers touched only on that thread: use Invoke / MarshalToOwner.
enum VizUiSlot {
	VIZ_UI_PIANO = 0,
	VIZ_UI_ANALYZER = 1,
	VIZ_UI_SLOTS
};

namespace VizUi {
	// Main UI thread only, once at startup. Creates the sink used by PostToMainIfOff.
	void InitMainThread();
	// Runs fn on the slot's UI thread and waits (starts the thread on first use).
	// Falls back to running fn on the caller when the thread cannot be started.
	void Invoke(int slot, const std::function<void()>& fn);
	// Hidden popup on the slot thread. Use this as the dialog owner.
	// A main-thread HWND must not own a viz-thread window: activation and
	// song changes then SendMessage into the paint stack and crash.
	// Call only from that slot's thread (inside Invoke). NULL if unavailable.
	CWnd* ThreadOwner(int slot);
	// hwnd lives on a viz thread other than the caller: run fn there, wait, return true.
	// Otherwise returns false without running fn.
	bool MarshalToOwner(HWND hwnd, const std::function<void()>& fn);
	// Caller is not the main UI thread: queue fn there and return true.
	// On the main UI thread (or before InitMainThread) returns false without running fn.
	bool PostToMainIfOff(std::function<void()> fn);
	bool IsMainThread();
	// After the viz windows are destroyed: stop the threads.
	void Shutdown();
}
