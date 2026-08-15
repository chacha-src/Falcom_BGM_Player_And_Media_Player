#include "stdafx.h"
#include "AudioDevSync.h"
#include "oggDlg.h"
#include "CCustomControl.h"
#include "CCustomPopupMenu.h"
#include "resource.h"
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>

#pragma comment(lib, "Ole32.lib")

extern void MpPersistSavedataQuick();
extern void MpMicMixRestartIfRunning();

enum { kComboRegMax = 24, kNotifyHwndMax = 16 };

static TCHAR s_micIds[AUDIODEV_MAX][256];
static CString s_micNames[AUDIODEV_MAX];
static int s_micCnt = 0;
static int s_micReady = 0;

static TCHAR s_loopIds[AUDIODEV_MAX][256];
static CString s_loopNames[AUDIODEV_MAX];
static int s_loopCnt = 0;
static int s_loopReady = 0;

static CCustomComboBox* s_micCombos[kComboRegMax];
static int s_micComboN = 0;
static CCustomComboBox* s_loopCombos[kComboRegMax];
static int s_loopComboN = 0;

static HWND s_notifyHwnds[kNotifyHwndMax];
static int s_notifyHwndN = 0;

static int s_micGuard = 0;
static int s_loopGuard = 0;

static HWND s_watchHwnd = NULL;
static volatile LONG s_watchPosted = 0;
static IMMDeviceEnumerator* s_watchEnum = NULL;

class AudioDevEndpointNotify : public IMMNotificationClient
{
	LONG m_ref;
public:
	AudioDevEndpointNotify() : m_ref(1) {}
	virtual ~AudioDevEndpointNotify() {}

	STDMETHODIMP QueryInterface(REFIID riid, void** ppv)
	{
		if (!ppv) return E_POINTER;
		if (riid == IID_IUnknown || riid == __uuidof(IMMNotificationClient)) {
			*ppv = static_cast<IMMNotificationClient*>(this);
			AddRef();
			return S_OK;
		}
		*ppv = NULL;
		return E_NOINTERFACE;
	}
	STDMETHODIMP_(ULONG) AddRef() { return (ULONG)InterlockedIncrement(&m_ref); }
	STDMETHODIMP_(ULONG) Release()
	{
		const LONG c = InterlockedDecrement(&m_ref);
		if (c == 0) { delete this; return 0; }
		return (ULONG)c;
	}

	void PostChanged()
	{
		// デバウンス: 連続挿抜でも1回。watch が AudioDevRebuildAll → 他窓へ配送。
		HWND h = s_watchHwnd;
		if (!h || !::IsWindow(h)) {
			// watch 未準備でも登録 UI へ直接通知
			for (int i = 0; i < s_notifyHwndN; ++i) {
				HWND n = s_notifyHwnds[i];
				if (n && ::IsWindow(n))
					::PostMessage(n, WM_AUDIODEV_CHANGED, 0, 0);
			}
			return;
		}
		if (InterlockedCompareExchange(&s_watchPosted, 1, 0) == 0)
			::PostMessage(h, WM_AUDIODEV_CHANGED, 0, 0);
	}

	STDMETHODIMP OnDeviceStateChanged(LPCWSTR, DWORD) { PostChanged(); return S_OK; }
	STDMETHODIMP OnDeviceAdded(LPCWSTR) { PostChanged(); return S_OK; }
	STDMETHODIMP OnDeviceRemoved(LPCWSTR) { PostChanged(); return S_OK; }
	STDMETHODIMP OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) { PostChanged(); return S_OK; }
	STDMETHODIMP OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) { return S_OK; }
};

static AudioDevEndpointNotify* s_watchCb = NULL;

static CString AudioDevDefaultMicLabel()
{
	return LL14(L"(既定の録音デバイス)", L"(Default recording device)", L"(Périphérique d'enregistrement par défaut)", L"(Dispositivo di registrazione predefinito)", L"(Dispositivo de grabación predeterminado)", L"(기본 녹음 장치)", L"(默认录制设备)", L"(جهاز التسجيل الافتراضي)", L"(Устройство записи по умолчанию)", L"(Standardaufnahmegerät)", L"(Dispositivo de gravação padrão)", L"(Standaard opnameapparaat)", L"(Domyślne urządzenie nagrywania)", L"(Varsayılan kayıt aygıtı)");
}

static CString AudioDevDefaultLoopLabel()
{
	return LL14(L"(既定の再生デバイス)", L"(Default playback device)", L"(Périphérique de lecture par défaut)", L"(Dispositivo di riproduzione predefinito)", L"(Dispositivo de reproducción predeterminado)", L"(기본 재생 장치)", L"(默认播放设备)", L"(جهاز التشغيل الافتراضي)", L"(Устройство воспроизведения по умолчанию)", L"(Standardwiedergabegerät)", L"(Dispositivo de reprodução padrão)", L"(Standaard afspeelapparaat)", L"(Domyślne urządzenie odtwarzania)", L"(Varsayılan oynatma aygıtı)");
}

static void RegAdd(CCustomComboBox** arr, int* pn, CCustomComboBox* cb)
{
	if (!cb || !cb->GetSafeHwnd()) return;
	for (int i = 0; i < *pn; ++i) {
		if (arr[i] == cb) return;
	}
	if (*pn >= kComboRegMax) return;
	arr[(*pn)++] = cb;
}

static void RegDel(CCustomComboBox** arr, int* pn, CCustomComboBox* cb)
{
	if (!cb) return;
	for (int i = 0; i < *pn; ++i) {
		if (arr[i] != cb) continue;
		for (int j = i; j < *pn - 1; ++j) arr[j] = arr[j + 1];
		(*pn)--;
		return;
	}
}

static void NotifyHwndAdd(HWND h)
{
	if (!h || !::IsWindow(h)) return;
	for (int i = 0; i < s_notifyHwndN; ++i) {
		if (s_notifyHwnds[i] == h) return;
	}
	if (s_notifyHwndN >= kNotifyHwndMax) return;
	s_notifyHwnds[s_notifyHwndN++] = h;
}

static void NotifyHwndDel(HWND h)
{
	if (!h) return;
	for (int i = 0; i < s_notifyHwndN; ++i) {
		if (s_notifyHwnds[i] != h) continue;
		for (int j = i; j < s_notifyHwndN - 1; ++j)
			s_notifyHwnds[j] = s_notifyHwnds[j + 1];
		s_notifyHwndN--;
		return;
	}
}

static void EnumEndpoints(EDataFlow flow, TCHAR ids[][256], CString* names, int* pCnt)
{
	*pCnt = 0;
	ids[0][0] = 0;
	names[0] = (flow == eCapture) ? AudioDevDefaultMicLabel() : AudioDevDefaultLoopLabel();
	*pCnt = 1;

	IMMDeviceEnumerator* enumer = NULL;
	IMMDeviceCollection* coll = NULL;
	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator), (void**)&enumer);
	if (FAILED(hr) || !enumer) return;
	hr = enumer->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &coll);
	if (SUCCEEDED(hr) && coll) {
		UINT cnt = 0;
		coll->GetCount(&cnt);
		for (UINT i = 0; i < cnt && *pCnt < AUDIODEV_MAX; ++i) {
			IMMDevice* dev = NULL;
			if (FAILED(coll->Item(i, &dev)) || !dev) continue;
			LPWSTR id = NULL;
			if (FAILED(dev->GetId(&id)) || !id) { dev->Release(); continue; }
			IPropertyStore* props = NULL;
			CString name = id;
			if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props)) && props) {
				PROPVARIANT var;
				PropVariantInit(&var);
				if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &var)) && var.vt == VT_LPWSTR && var.pwszVal)
					name = var.pwszVal;
				PropVariantClear(&var);
				props->Release();
			}
			_tcsncpy(ids[*pCnt], id, 255);
			ids[*pCnt][255] = 0;
			names[*pCnt] = name;
			CoTaskMemFree(id);
			dev->Release();
			(*pCnt)++;
		}
		coll->Release();
	}
	enumer->Release();
}

static int FindSelById(const TCHAR ids[][256], int cnt, LPCTSTR wantId, int wantCur)
{
	int sel = 0;
	if (wantId && wantId[0]) {
		for (int i = 1; i < cnt; ++i) {
			if (_tcscmp(ids[i], wantId) == 0) { sel = i; break; }
		}
	} else if (wantCur > 0 && wantCur < cnt) {
		sel = wantCur;
	}
	return sel;
}

static void FillComboFrom(CCustomComboBox& cb, const CString* names, int cnt, int sel)
{
	if (!cb.GetSafeHwnd()) return;
	cb.ResetContent();
	for (int i = 0; i < cnt; ++i)
		cb.AddString(names[i]);
	if (sel < 0 || sel >= cnt) sel = 0;
	cb.SetCurSelPhysical(sel);
}

static void SyncComboSelOnly(CCustomComboBox& cb, int sel)
{
	if (!cb.GetSafeHwnd()) return;
	if (cb.GetCount() <= 0) return;
	if (sel < 0) sel = 0;
	if (sel >= cb.GetCount()) sel = 0;
	if (cb.GetCurSelPhysical() != sel)
		cb.SetCurSelPhysical(sel);
}

void AudioDevWatchEnsure(HWND hwndUi)
{
	if (hwndUi && ::IsWindow(hwndUi))
		s_watchHwnd = hwndUi;
	if (s_watchCb && s_watchEnum)
		return;
	IMMDeviceEnumerator* enumer = NULL;
	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator), (void**)&enumer);
	if (FAILED(hr) || !enumer) return;
	AudioDevEndpointNotify* cb = new AudioDevEndpointNotify();
	hr = enumer->RegisterEndpointNotificationCallback(cb);
	if (FAILED(hr)) {
		cb->Release();
		enumer->Release();
		return;
	}
	s_watchEnum = enumer;
	s_watchCb = cb;
}

void AudioDevWatchShutdown()
{
	if (s_watchEnum && s_watchCb) {
		s_watchEnum->UnregisterEndpointNotificationCallback(s_watchCb);
	}
	if (s_watchCb) {
		s_watchCb->Release();
		s_watchCb = NULL;
	}
	if (s_watchEnum) {
		s_watchEnum->Release();
		s_watchEnum = NULL;
	}
	s_watchHwnd = NULL;
	InterlockedExchange(&s_watchPosted, 0);
}

void AudioDevRegisterNotifyHwnd(HWND h) { NotifyHwndAdd(h); }
void AudioDevUnregisterNotifyHwnd(HWND h) { NotifyHwndDel(h); }

CString AudioDevRescanButtonLabel()
{
	return LL14(L"再検出", L"Rescan", L"Rescan", L"Rileva", L"Redetectar",
		L"재검색", L"重新检测", L"إعادة الفحص", L"Обновить", L"Neu erkennen",
		L"Redetectar", L"Opnieuw", L"Wykryj ponownie", L"Yeniden tara");
}

void AudioDevApplyRescanButton(CWnd* btn)
{
	if (!btn || !btn->GetSafeHwnd()) return;
	btn->SetWindowText(AudioDevRescanButtonLabel());
}

static void RebuildMicCombos()
{
	const int sel = AudioMicDevCurSel();
	for (int i = 0; i < s_micComboN; ++i) {
		CCustomComboBox* p = s_micCombos[i];
		if (!p || !p->GetSafeHwnd()) continue;
		FillComboFrom(*p, s_micNames, s_micCnt, sel);
	}
}

static void RebuildLoopCombos()
{
	const int sel = AudioLoopDevCurSel();
	for (int i = 0; i < s_loopComboN; ++i) {
		CCustomComboBox* p = s_loopCombos[i];
		if (!p || !p->GetSafeHwnd()) continue;
		FillComboFrom(*p, s_loopNames, s_loopCnt, sel);
	}
}

void AudioDevRebuildAll()
{
	InterlockedExchange(&s_watchPosted, 0);

	TCHAR prevMic[256];
	_tcsncpy(prevMic, savedata.mic_device, 255);
	prevMic[255] = 0;

	AudioMicDevRefresh();
	AudioLoopDevRefresh();

	s_micGuard = 1;
	s_loopGuard = 1;
	RebuildMicCombos();
	RebuildLoopCombos();
	s_micGuard = 0;
	s_loopGuard = 0;

	if (_tcscmp(prevMic, savedata.mic_device) != 0)
		MpMicMixRestartIfRunning();

	HWND watch = s_watchHwnd;
	for (int i = 0; i < s_notifyHwndN; ++i) {
		HWND h = s_notifyHwnds[i];
		if (!h || !::IsWindow(h) || h == watch) continue;
		::PostMessage(h, WM_AUDIODEV_CHANGED, 0, 0);
	}
}

// ---- Mic ----

void AudioMicDevRefresh()
{
	EnumEndpoints(eCapture, s_micIds, s_micNames, &s_micCnt);
	s_micReady = 1;
	int sel = FindSelById(s_micIds, s_micCnt, savedata.mic_device, savedata.mic_device_cur);
	savedata.mic_device_cur = sel;
	_tcsncpy(savedata.mic_device, s_micIds[sel], _countof(savedata.mic_device) - 1);
	savedata.mic_device[_countof(savedata.mic_device) - 1] = 0;
}

int AudioMicDevCount()
{
	if (!s_micReady) AudioMicDevRefresh();
	return s_micCnt;
}

LPCTSTR AudioMicDevId(int i)
{
	if (!s_micReady) AudioMicDevRefresh();
	if (i < 0 || i >= s_micCnt) return L"";
	return s_micIds[i];
}

CString AudioMicDevName(int i)
{
	if (!s_micReady) AudioMicDevRefresh();
	if (i < 0 || i >= s_micCnt) return CString();
	return s_micNames[i];
}

int AudioMicDevCurSel()
{
	if (!s_micReady) AudioMicDevRefresh();
	return FindSelById(s_micIds, s_micCnt, savedata.mic_device, savedata.mic_device_cur);
}

void AudioMicDevRegisterCombo(CCustomComboBox* cb) { RegAdd(s_micCombos, &s_micComboN, cb); }
void AudioMicDevUnregisterCombo(CCustomComboBox* cb) { RegDel(s_micCombos, &s_micComboN, cb); }

void AudioMicDevFillCombo(CCustomComboBox& cb)
{
	if (!s_micReady) AudioMicDevRefresh();
	FillComboFrom(cb, s_micNames, s_micCnt, AudioMicDevCurSel());
	AudioMicDevRegisterCombo(&cb);
}

void AudioMicDevSyncComboSel(CCustomComboBox& cb)
{
	SyncComboSelOnly(cb, AudioMicDevCurSel());
}

void AudioMicDevSyncAllUi()
{
	const int sel = AudioMicDevCurSel();
	for (int i = 0; i < s_micComboN; ++i) {
		CCustomComboBox* p = s_micCombos[i];
		if (!p || !p->GetSafeHwnd()) continue;
		if (p->GetCount() != s_micCnt)
			FillComboFrom(*p, s_micNames, s_micCnt, sel);
		else
			SyncComboSelOnly(*p, sel);
	}
}

void AudioMicDevApplySel(int sel)
{
	if (s_micGuard) return;
	if (!s_micReady) AudioMicDevRefresh();
	if (sel < 0 || sel >= s_micCnt) sel = 0;
	savedata.mic_device_cur = sel;
	_tcsncpy(savedata.mic_device, s_micIds[sel], _countof(savedata.mic_device) - 1);
	savedata.mic_device[_countof(savedata.mic_device) - 1] = 0;
	{
		TCHAR cur[256];
		_tcsncpy(cur, savedata.mic_device, 255); cur[255] = 0;
		for (int i = 0; i < 2; ++i) {
			if (_tcsicmp(savedata.mpMicMru[i], cur) == 0) {
				for (int j = i; j < 2; ++j)
					_tcscpy_s(savedata.mpMicMru[j], savedata.mpMicMru[j + 1]);
				savedata.mpMicMru[2][0] = 0;
				break;
			}
		}
		for (int i = 2; i > 0; --i)
			_tcscpy_s(savedata.mpMicMru[i], savedata.mpMicMru[i - 1]);
		_tcscpy_s(savedata.mpMicMru[0], cur);
	}
	MpMicMixRestartIfRunning();
	MpPersistSavedataQuick();
	s_micGuard = 1;
	AudioMicDevSyncAllUi();
	s_micGuard = 0;
}

void AudioMicDevApplyFromCombo(CCustomComboBox& cb)
{
	if (s_micGuard) return;
	if (!cb.GetSafeHwnd()) return;
	int sel = cb.GetCurSelPhysical();
	AudioMicDevApplySel(sel);
}

void AudioMicDevAppendMenu(CCustomPopupMenu& menu)
{
	if (!s_micReady) AudioMicDevRefresh();
	CCustomPopupMenu* sub = menu.AddSubMenu(
		LL14(L"マイク端末", L"Microphone", L"Microphone", L"Microfono", L"Micrófono",
			L"마이크", L"麦克风", L"الميكروفون", L"Микрофон", L"Mikrofon",
			L"Microfone", L"Microfoon", L"Mikrofon", L"Mikrofon"),
		LL14(L"マイク入力に使う録音デバイスを選びます。ミックス録音やマイクメーターに反映。", L"Choose the recording device for mic input. Affects mix recording and the mic meter.", L"Choisir le peripherique d'entree micro. Affecte l'enregistrement mix et le vu-metre.", L"Scegli il dispositivo di registrazione micro. Influisce su mix e misuratore.", L"Elige el dispositivo de grabacion del micro. Afecta la mezcla y el medidor.",
			L"마이크 입력에 쓸 녹음 장치를 고릅니다. 믹스 녹음·마이크 미터에 반영.", L"选择麦克风输入的录音设备。影响混音录音和麦克风电平表。", L"اختر جهاز التسجيل لإدخال الميكروفون. يؤثر على تسجيل المزج ومقياس الميكروفون.", L"Выберите устройство записи микрофона. Влияет на микс и индикатор.", L"Aufnahmegerät für Mikrofoneingang wählen. Betrifft Mix-Aufnahme und Mikrofonpegel.",
			L"Escolha o dispositivo de gravacao do microfone. Afeta a gravacao de mix e o medidor.", L"Kies het opnameapparaat voor microfooninvoer. Beinvloedt mix-opname en mic-meter.", L"Wybierz urzadzenie nagrywania mikrofonu. Wplywa na mix i miernik.", L"Mikrofon girisi icin kayit aygitini sec. Mix kaydi ve mikrofon metresine yansir."));
	if (!sub) return;
	const int cur = AudioMicDevCurSel();
	for (int i = 0; i < s_micCnt; ++i) {
		sub->AddCheck((UINT)(ID_AUDIO_MIC_BASE + i), s_micNames[i], i == cur);
	}
}

BOOL AudioMicDevHandleMenuCmd(UINT cmd)
{
	if (cmd < ID_AUDIO_MIC_BASE || cmd > (ID_AUDIO_MIC_BASE + AUDIODEV_MAX - 1)) return FALSE;
	AudioMicDevApplySel((int)(cmd - ID_AUDIO_MIC_BASE));
	return TRUE;
}

// ---- Loop ----

void AudioLoopDevRefresh()
{
	EnumEndpoints(eRender, s_loopIds, s_loopNames, &s_loopCnt);
	s_loopReady = 1;
	int sel = FindSelById(s_loopIds, s_loopCnt, savedata.loop_device, savedata.loop_device_cur);
	savedata.loop_device_cur = sel;
	_tcsncpy(savedata.loop_device, s_loopIds[sel], _countof(savedata.loop_device) - 1);
	savedata.loop_device[_countof(savedata.loop_device) - 1] = 0;
}

int AudioLoopDevCount()
{
	if (!s_loopReady) AudioLoopDevRefresh();
	return s_loopCnt;
}

LPCTSTR AudioLoopDevId(int i)
{
	if (!s_loopReady) AudioLoopDevRefresh();
	if (i < 0 || i >= s_loopCnt) return L"";
	return s_loopIds[i];
}

CString AudioLoopDevName(int i)
{
	if (!s_loopReady) AudioLoopDevRefresh();
	if (i < 0 || i >= s_loopCnt) return CString();
	return s_loopNames[i];
}

int AudioLoopDevCurSel()
{
	if (!s_loopReady) AudioLoopDevRefresh();
	return FindSelById(s_loopIds, s_loopCnt, savedata.loop_device, savedata.loop_device_cur);
}

void AudioLoopDevRegisterCombo(CCustomComboBox* cb) { RegAdd(s_loopCombos, &s_loopComboN, cb); }
void AudioLoopDevUnregisterCombo(CCustomComboBox* cb) { RegDel(s_loopCombos, &s_loopComboN, cb); }

void AudioLoopDevFillCombo(CCustomComboBox& cb)
{
	if (!s_loopReady) AudioLoopDevRefresh();
	FillComboFrom(cb, s_loopNames, s_loopCnt, AudioLoopDevCurSel());
	AudioLoopDevRegisterCombo(&cb);
}

void AudioLoopDevSyncComboSel(CCustomComboBox& cb)
{
	SyncComboSelOnly(cb, AudioLoopDevCurSel());
}

void AudioLoopDevSyncAllUi()
{
	const int sel = AudioLoopDevCurSel();
	for (int i = 0; i < s_loopComboN; ++i) {
		CCustomComboBox* p = s_loopCombos[i];
		if (!p || !p->GetSafeHwnd()) continue;
		if (p->GetCount() != s_loopCnt)
			FillComboFrom(*p, s_loopNames, s_loopCnt, sel);
		else
			SyncComboSelOnly(*p, sel);
	}
}

void AudioLoopDevApplySel(int sel)
{
	if (s_loopGuard) return;
	if (!s_loopReady) AudioLoopDevRefresh();
	if (sel < 0 || sel >= s_loopCnt) sel = 0;
	savedata.loop_device_cur = sel;
	_tcsncpy(savedata.loop_device, s_loopIds[sel], _countof(savedata.loop_device) - 1);
	savedata.loop_device[_countof(savedata.loop_device) - 1] = 0;
	{
		TCHAR cur[256];
		_tcsncpy(cur, savedata.loop_device, 255); cur[255] = 0;
		for (int i = 0; i < 2; ++i) {
			if (_tcsicmp(savedata.mpLoopMru[i], cur) == 0) {
				for (int j = i; j < 2; ++j)
					_tcscpy_s(savedata.mpLoopMru[j], savedata.mpLoopMru[j + 1]);
				savedata.mpLoopMru[2][0] = 0;
				break;
			}
		}
		for (int i = 2; i > 0; --i)
			_tcscpy_s(savedata.mpLoopMru[i], savedata.mpLoopMru[i - 1]);
		_tcscpy_s(savedata.mpLoopMru[0], cur);
	}
	MpPersistSavedataQuick();
	s_loopGuard = 1;
	AudioLoopDevSyncAllUi();
	s_loopGuard = 0;
}

void AudioLoopDevApplyFromCombo(CCustomComboBox& cb)
{
	if (s_loopGuard) return;
	if (!cb.GetSafeHwnd()) return;
	AudioLoopDevApplySel(cb.GetCurSelPhysical());
}

void AudioLoopDevAppendMenu(CCustomPopupMenu& menu)
{
	if (!s_loopReady) AudioLoopDevRefresh();
	CCustomPopupMenu* sub = menu.AddSubMenu(
		LL14(L"システム音端末", L"System audio device", L"Périphérique son système", L"Dispositivo audio sistema", L"Dispositivo audio sistema",
			L"시스템 소리 장치", L"系统声音设备", L"جهاز صوت النظام", L"Устройство системного звука", L"Systemton-Gerät",
			L"Dispositivo de áudio do sistema", L"Systeemaudio-apparaat", L"Urządzenie dźwięku systemu", L"Sistem sesi aygıtı"),
		LL14(L"システム音(ループバック)を取り込む再生デバイスを選びます。PC音の録音/取り込み用。", L"Choose the playback device used for system-audio (loopback) capture.", L"Choisir le peripherique de lecture pour capturer le son systeme (loopback).", L"Scegli il dispositivo di riproduzione per catturare l'audio di sistema (loopback).", L"Elige el dispositivo de reproduccion para capturar el audio del sistema (loopback).",
			L"시스템 소리(루프백)를 가져올 재생 장치를 고릅니다. PC 음 녹음/캡처용.", L"选择用于抓取系统声音（环回）的播放设备。用于录制/采集 PC 音。", L"اختر جهاز التشغيل لالتقاط صوت النظام (loopback) لتسجيل صوت الجهاز.", L"Выберите устройство воспроизведения для захвата системного звука (loopback).", L"Wiedergabegerät für Systemton-Capture (Loopback) wählen.",
			L"Escolha o dispositivo de reproducao para capturar o audio do sistema (loopback).", L"Kies het afspeelapparaat voor systeemaudio-capture (loopback).", L"Wybierz urzadzenie odtwarzania do przechwytywania dzwieku systemu (loopback).", L"Sistem sesi (loopback) yakalamak icin oynatma aygitini sec."));
	if (!sub) return;
	const int cur = AudioLoopDevCurSel();
	for (int i = 0; i < s_loopCnt; ++i) {
		sub->AddCheck((UINT)(ID_AUDIO_LOOP_BASE + i), s_loopNames[i], i == cur);
	}
}

BOOL AudioLoopDevHandleMenuCmd(UINT cmd)
{
	if (cmd < ID_AUDIO_LOOP_BASE || cmd > ID_AUDIO_LOOP_LAST) return FALSE;
	AudioLoopDevApplySel((int)(cmd - ID_AUDIO_LOOP_BASE));
	return TRUE;
}
