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
					LL14(L"音声ミキサーでミュートになっています。\nこのままでは音が出ません。解除しますか？", L"Muted in volume mixer.\nNo sound will play. Unmute?", L"Muet dans le mixeur.\nAucun son. Réactiver ?", L"Disattivato nel mixer.\nNessun suono. Riattivare?", L"Silenciado en el mezclador.\nNo hay sonido. ¿Activar?", L"볼륨 믹서에서 음소거되어 있습니다.\n소리가 나지 않습니다. 해제할까요?", L"音量混合器已静音。\n将无声音。要取消静音吗？", L"كتم في خلاط الصوت.\nلن يخرج صوت. إلغاء الكتم؟", L"Выкл. в микшере.\nЗвука не будет. Включить?", L"Im Mixer stummgeschaltet.\nKein Ton. Aufheben?", L"Mudo no mixer.\nSem som. Reativar?", L"Gedempt in volumemixer.\nGeen geluid. Dempen opheffen?", L"Wyciszony w mikserze.\nBrak dźwięku. Przywrócić?", L"Ses karıştırıcıda sessiz.\nSes çıkmaz. Kaldırılsın mı?"),
					LL14(L"確認", L"Confirm", L"Confirmer", L"Conferma", L"Confirmar", L"확인", L"确认", L"تأكيد", L"Подтвердить", L"Bestätigen", L"Confirmar", L"Bevestigen", L"Potwierdź", L"Onayla"),
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
					LL14(L"音声ミキサーでミュートになっています。\nこのままでは音が出ません。解除しますか？", L"Muted in volume mixer.\nNo sound will play. Unmute?", L"Muet dans le mixeur.\nAucun son. Réactiver ?", L"Disattivato nel mixer.\nNessun suono. Riattivare?", L"Silenciado en el mezclador.\nNo hay sonido. ¿Activar?", L"볼륨 믹서에서 음소거되어 있습니다.\n소리가 나지 않습니다. 해제할까요?", L"音量混合器已静音。\n将无声音。要取消静音吗？", L"كتم في خلاط الصوت.\nلن يخرج صوت. إلغاء الكتم؟", L"Выкл. в микшере.\nЗвука не будет. Включить?", L"Im Mixer stummgeschaltet.\nKein Ton. Aufheben?", L"Mudo no mixer.\nSem som. Reativar?", L"Gedempt in volumemixer.\nGeen geluid. Dempen opheffen?", L"Wyciszony w mikserze.\nBrak dźwięku. Przywrócić?", L"Ses karıştırıcıda sessiz.\nSes çıkmaz. Kaldırılsın mı?"),
					LL14(L"確認", L"Confirm", L"Confirmer", L"Conferma", L"Confirmar", L"확인", L"确认", L"تأكيد", L"Подтвердить", L"Bestätigen", L"Confirmar", L"Bevestigen", L"Potwierdź", L"Onayla"),
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
				LL14(L"全体がミュート設定になっています。解除しますか？", L"Master volume is muted. Unmute?", L"Volume maître muet. Réactiver ?", L"Volume principale disattivato. Riattivare?", L"Volumen maestro silenciado. ¿Activar?", L"마스터 볼륨이 음소거되어 있습니다. 해제할까요?", L"主音量已静音。要取消静音吗？", L"المستوى الرئيسي مكتوم. إلغاء الكتم؟", L"Главная громкость выкл. Включить?", L"Hauptlautstärke stumm. Aufheben?", L"Volume mestre mudo. Reativar?", L"Hoofdvolume gedempt. Opheffen?", L"Głośność główna wyciszona. Przywrócić?", L"Ana ses sessiz. Kaldırılsın mı?"),
				LL14(L"確認", L"Confirm", L"Confirmer", L"Conferma", L"Confirmar", L"확인", L"确认", L"تأكيد", L"Подтвердить", L"Bestätigen", L"Confirmar", L"Bevestigen", L"Potwierdź", L"Onayla"),
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
					LL14(L"音声ミキサーで当アプリがミュートになっています。\n解除しますか？", L"This app is muted in volume mixer.\nUnmute?", L"Cette app est muette dans le mixeur.\nRéactiver ?", L"Questa app è disattivata nel mixer.\nRiattivare?", L"Esta app está silenciada en el mezclador.\n¿Activar?", L"이 앱이 볼륨 믹서에서 음소거되어 있습니다.\n해제할까요?", L"此应用在音量混合器中已静音。\n要取消静音吗？", L"هذا التطبيق مكتوم في خلاط الصوت.\nإلغاء الكتم؟", L"Приложение выкл. в микшере.\nВключить?", L"Diese App ist im Mixer stumm.\nAufheben?", L"Este app está mudo no mixer.\nReativar?", L"Deze app is gedempt in volumemixer.\nOpheffen?", L"Ta aplikacja wyciszona w mikserze.\nPrzywrócić?", L"Bu uygulama ses karıştırıcıda sessiz.\nKaldırılsın mı?"),
					LL14(L"確認", L"Confirm", L"Confirmer", L"Conferma", L"Confirmar", L"확인", L"确认", L"تأكيد", L"Подтвердить", L"Bestätigen", L"Confirmar", L"Bevestigen", L"Potwierdź", L"Onayla"),
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
