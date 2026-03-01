#include "stdafx.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>

static ISimpleAudioVolume* FindCurrentProcessSimpleVolume()
{
	IMMDeviceEnumerator* enumerator = NULL;
	IMMDevice* device = NULL;
	IAudioSessionManager2* sessionManager = NULL;
	IAudioSessionEnumerator* sessions = NULL;
	ISimpleAudioVolume* result = NULL;

	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator), (void**)&enumerator);
	if (SUCCEEDED(hr)) hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
	if (SUCCEEDED(hr)) hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, NULL, (void**)&sessionManager);
	if (SUCCEEDED(hr)) hr = sessionManager->GetSessionEnumerator(&sessions);
	if (SUCCEEDED(hr) && sessions) {
		int count = 0;
		sessions->GetCount(&count);
		DWORD currentPid = GetCurrentProcessId();
		for (int i = 0; i < count; ++i) {
			IAudioSessionControl* control = NULL;
			if (FAILED(sessions->GetSession(i, &control)) || !control) continue;
			IAudioSessionControl2* control2 = NULL;
			if (SUCCEEDED(control->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&control2)) && control2) {
				DWORD pid = 0;
				if (SUCCEEDED(control2->GetProcessId(&pid)) && pid == currentPid) {
					control2->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&result);
				}
				control2->Release();
			}
			control->Release();
			if (result) break;
		}
	}

	if (sessions) sessions->Release();
	if (sessionManager) sessionManager->Release();
	if (device) device->Release();
	if (enumerator) enumerator->Release();

	return result;
}

static DWORD WINAPI MixerMuteCheckThread(LPVOID)
{
	HRESULT hr = CoInitialize(NULL);
	if (FAILED(hr)) return 0;

	const int maxAttempts = 40;
	for (int attempt = 0; attempt < maxAttempts; ++attempt) {
		ISimpleAudioVolume* simple = FindCurrentProcessSimpleVolume();
		if (simple) {
			BOOL muted = FALSE;
			if (SUCCEEDED(simple->GetMute(&muted)) && muted) {
				int result = MessageBoxW(NULL,
					LL2(L"音声ミキサーでミュートになっています。\nこのままでは音が出ません。解除しますか？", L"Muted in volume mixer.\nNo sound will play. Unmute?"),
					LL2(L"確認", L"Confirm"),
					MB_OKCANCEL | MB_ICONWARNING | MB_SYSTEMMODAL | MB_TOPMOST);
				if (result == IDOK) {
					simple->SetMute(FALSE, NULL);
				}
			}
			simple->Release();
			break;
		}
		Sleep(250);
	}

	CoUninitialize();
	return 0;
}

void CheckCurrentAppMixerMuteModal()
{
	HRESULT hr = CoInitialize(NULL);
	if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return;

	const int maxAttempts = 40;
	for (int attempt = 0; attempt < maxAttempts; ++attempt) {
		ISimpleAudioVolume* simple = FindCurrentProcessSimpleVolume();
		if (simple) {
			BOOL muted = FALSE;
			if (SUCCEEDED(simple->GetMute(&muted)) && muted) {
				int result = MessageBoxW(NULL,
					LL2(L"音声ミキサーでミュートになっています。\nこのままでは音が出ません。解除しますか？", L"Muted in volume mixer.\nNo sound will play. Unmute?"),
					LL2(L"確認", L"Confirm"),
					MB_OKCANCEL | MB_ICONWARNING | MB_SYSTEMMODAL | MB_TOPMOST);
				if (result == IDOK) {
					simple->SetMute(FALSE, NULL);
				}
			}
			simple->Release();
			break;
		}
		Sleep(250);
	}

	if (hr == S_OK) CoUninitialize();
}

static bool GetMasterMuteState(BOOL* muted)
{
	if (!muted) return false;
	*muted = FALSE;

	IMMDeviceEnumerator* enumerator = NULL;
	IMMDevice* device = NULL;
	IAudioEndpointVolume* endpoint = NULL;
	bool ok = false;

	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator), (void**)&enumerator);
	if (SUCCEEDED(hr)) hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
	if (SUCCEEDED(hr)) hr = device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL, (void**)&endpoint);
	if (SUCCEEDED(hr) && endpoint) {
		ok = SUCCEEDED(endpoint->GetMute(muted)) ? true : false;
	}

	if (endpoint) endpoint->Release();
	if (device) device->Release();
	if (enumerator) enumerator->Release();

	return ok;
}

bool CheckMixerMuteOnPlayModal()
{
	HRESULT hr = CoInitialize(NULL);
	if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return true;

	BOOL masterMuted = FALSE;
	if (GetMasterMuteState(&masterMuted) && masterMuted) {
			int result = MessageBoxW(NULL,
				LL2(L"全体がミュート設定になっています。解除しますか？", L"Master volume is muted. Unmute?"),
				LL2(L"確認", L"Confirm"),
			MB_OKCANCEL | MB_ICONWARNING | MB_SYSTEMMODAL | MB_TOPMOST);
		if (result == IDOK) {
			IMMDeviceEnumerator* enumerator = NULL;
			IMMDevice* device = NULL;
			IAudioEndpointVolume* endpoint = NULL;
			HRESULT hr2 = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
				__uuidof(IMMDeviceEnumerator), (void**)&enumerator);
			if (SUCCEEDED(hr2)) hr2 = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
			if (SUCCEEDED(hr2)) hr2 = device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL, (void**)&endpoint);
			if (SUCCEEDED(hr2) && endpoint) {
				endpoint->SetMute(FALSE, NULL);
			}
			if (endpoint) endpoint->Release();
			if (device) device->Release();
			if (enumerator) enumerator->Release();
		}
	}

	const int maxAttempts = 10;
	for (int attempt = 0; attempt < maxAttempts; ++attempt) {
		ISimpleAudioVolume* simple = FindCurrentProcessSimpleVolume();
		if (simple) {
			BOOL muted = FALSE;
			if (SUCCEEDED(simple->GetMute(&muted)) && muted) {
				int result = MessageBoxW(NULL,
					LL2(L"音声ミキサーで当アプリがミュートになっています。\n解除しますか？", L"This app is muted in volume mixer.\nUnmute?"),
					LL2(L"確認", L"Confirm"),
					MB_OKCANCEL | MB_ICONWARNING | MB_SYSTEMMODAL | MB_TOPMOST);
				if (result == IDOK) {
					simple->SetMute(FALSE, NULL);
				}
			}
			simple->Release();
			break;
		}
		Sleep(200);
	}

	if (hr == S_OK) CoUninitialize();
	return true;
}

class MixerMuteCheckLauncher
{
public:
	MixerMuteCheckLauncher()
	{
		HANDLE thread = CreateThread(NULL, 0, MixerMuteCheckThread, NULL, 0, NULL);
		if (thread) CloseHandle(thread);
	}
};

static MixerMuteCheckLauncher g_mixerMuteCheckLauncher;
