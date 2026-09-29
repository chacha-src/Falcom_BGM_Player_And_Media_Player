#pragma once
#include <windows.h>

// Shared vsync hub. One wait thread for the process.
// WaitForVBlank (DXGI) when possible; otherwise QPC + high-res waitable timer.
// Per-window Start() only registers — does not create extra wait threads.
// Does not paint. Ack() after handling so the next Post can fire.
#ifndef WM_UITICK_VSYNC
#define WM_UITICK_VSYNC (WM_APP + 548)
#endif

class UiTickPump {
public:
	UiTickPump();
	~UiTickPump();
	UiTickPump(const UiTickPump&) = delete;
	UiTickPump& operator=(const UiTickPump&) = delete;

	void Start(HWND hwnd, UINT msg = WM_UITICK_VSYNC);
	void Stop();
	void Ack();
	bool IsRunning();
	bool TryPostTick();

	// TheadLoop: wait for the shared hub pulse (not DwmGetCompositionTimingInfo+Sleep).
	static void WaitVblank(HANDLE stopEvent);
	static void EnsureHub();
	static void BindWindow(HWND hwnd);

private:
	HWND m_hwnd;
	UINT m_msg;
	volatile LONG m_posted;
	volatile LONG m_registered;
};
