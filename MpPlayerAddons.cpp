#include "StdAfx.h"
#include "MpPlayerAddons.h"
#include "CMediaPlayerDlg.h"
#include "CPromptEngine.h"
#include "ProAudio.h"
#include "oggDlg.h"
#include "CPianoRoll.h"
#include "DeviceRecordDlg.h"
#include "ScreenCaptureDlg.h"
#include "PlayList.h"

#include <mmdeviceapi.h>
#include <audioclient.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mmsystem.h>
#include <propsys.h>
#include <functiondiscoverykeys_devpkey.h>

#pragma comment(lib, "ws2_32.lib")

extern save savedata;
extern COggDlg* og;
extern CPlayList* pl;
extern void MpPersistSavedataQuick();
extern int spelv[400];
extern int tempo;
extern int pitch;
extern int ps;
extern int g_ds_pcm_rate;
extern int g_ds_pcm_ch;
extern int g_ds_pcm_bits;
extern int g_outBytesPerFrame;

class CMpDjPadDlg;
class CMpAlarmDlg;
class CMpMirrorDlg;
class CMpRemoteDlg;
class CMpSsVizDlg;
static CMpDjPadDlg* g_mpDjPad = NULL;
static CMpAlarmDlg* g_mpAlarmDlg = NULL;
static CMpMirrorDlg* g_mpMirrorDlg = NULL;
static CMpRemoteDlg* g_mpRemoteDlg = NULL;
static CMpSsVizDlg* g_mpSsViz = NULL;

// ---- BPM onset timing (crude): チェックONのあいだ蓄積し、OFFで推定 ----
static DWORD g_bpmPeakTimes[32];
static int g_bpmPeakN = 0;
static int g_bpmWasHigh = 0;
static int g_bpmArmed = 0;
static int g_bpmHeldPcAudio = 0;

// PC音ループバックの参照カウント(MIDI録り・BPM計測などが共有)
static int g_pcAudioRetain = 0;
static int g_pcAudioPrevOff = 0;

void MpPcAudioRetain()
{
	if (g_pcAudioRetain == 0)
		g_pcAudioPrevOff = savedata.mpLoopbackScore ? 0 : 1;
	if (!savedata.mpLoopbackScore)
		savedata.mpLoopbackScore = 1;
	g_pcAudioRetain++;
	CWnd* parent = NULL;
	if (og) parent = og;
	else if (mp) parent = mp;
	EnsureDeviceRecordLoopbackFeed(parent);
	if (og && og->m_PianoRollDlg && ::IsWindow(og->m_PianoRollDlg->GetSafeHwnd()))
		og->m_PianoRollDlg->ResumePlaybackFeed();
}

void MpPcAudioRelease()
{
	if (g_pcAudioRetain <= 0) return;
	g_pcAudioRetain--;
	if (g_pcAudioRetain > 0) return;
	if (!g_pcAudioPrevOff) return;
	g_pcAudioPrevOff = 0;
	savedata.mpLoopbackScore = 0;
	StopDeviceRecordLoopbackFeed();
	extern int playf;
	if (!playf && og && og->m_PianoRollDlg && ::IsWindow(og->m_PianoRollDlg->GetSafeHwnd()))
		og->m_PianoRollDlg->PauseAnalysis();
}

void MpPcAudioMarkUserOwned()
{
	// 明示ONされたPC音は、ツール側 Release で勝手に落とさない
	g_pcAudioPrevOff = 0;
}

BOOL MpBpmIsMeasuring()
{
	return g_bpmArmed ? TRUE : FALSE;
}

void MpBpmOnTimerTick()
{
	if (!g_bpmArmed) return;
	const float pk = ProAudio_LivePeak();
	const int high = (pk >= 0.45f) ? 1 : 0;
	if (high && !g_bpmWasHigh) {
		const DWORD t = GetTickCount();
		if (g_bpmPeakN >= 32) {
			for (int i = 0; i < 31; ++i) g_bpmPeakTimes[i] = g_bpmPeakTimes[i + 1];
			g_bpmPeakTimes[31] = t;
		}
		else {
			g_bpmPeakTimes[g_bpmPeakN++] = t;
		}
	}
	g_bpmWasHigh = high;
}

static int MpBpmEstimateFromPeaks()
{
	if (g_bpmPeakN < 4) return 0;
	int hist[120];
	for (int i = 0; i < 120; ++i) hist[i] = 0;
	for (int i = 1; i < g_bpmPeakN; ++i) {
		const int dt = (int)(g_bpmPeakTimes[i] - g_bpmPeakTimes[i - 1]);
		if (dt < 250 || dt > 2000) continue;
		const int bpm = (int)(60000.0 / (double)dt + 0.5);
		if (bpm >= 60 && bpm <= 180)
			hist[bpm - 60]++;
	}
	int best = 0, bestBpm = 0;
	for (int b = 60; b <= 180; ++b) {
		if (hist[b - 60] > best) { best = hist[b - 60]; bestBpm = b; }
	}
	return bestBpm;
}

void MpBpmDetectFromPeaks()
{
	const int bpm = MpBpmEstimateFromPeaks();
	if (bpm <= 0) return;
	savedata.mpDetectedBpm = bpm;
	MpPersistSavedataQuick();
	if (mp && mp->m_seek.GetSafeHwnd())
		mp->m_seek.SetBeatGrid((float)bpm, savedata.mpBeatGrid ? TRUE : FALSE);
	if (savedata.wav_export_xfade) {
		int sec = (int)(240000.0 / (double)bpm + 0.5);
		if (sec < 1) sec = 1;
		if (sec > 30) sec = 30;
		savedata.wav_export_xfade_sec = sec;
		MpPersistSavedataQuick();
	}
}

void MpOnBpmDetect(CMediaPlayerDlg* /*mpDlg*/)
{
	extern int playf;
	if (!g_bpmArmed) {
		g_bpmArmed = 1;
		g_bpmPeakN = 0;
		g_bpmWasHigh = 0;
		// 再生していなければ PC 音ピークを確保
		if (!playf && !g_bpmHeldPcAudio) {
			MpPcAudioRetain();
			g_bpmHeldPcAudio = 1;
		}
		return;
	}
	g_bpmArmed = 0;
	MpBpmDetectFromPeaks();
	if (g_bpmHeldPcAudio) {
		MpPcAudioRelease();
		g_bpmHeldPcAudio = 0;
	}
	if (savedata.mpDetectedBpm <= 0 && mp) {
		mp->MessageBox(
			LL14(L"BPM を推定できませんでした。再生またはPC音を鳴らしながら数秒計測してからオフにしてください。", L"Could not estimate BPM. Measure for a few seconds while playing (or PC audio), then uncheck.", L"BPM non estime. Mesurez quelques secondes (lecture ou audio PC) puis decochez.", L"BPM non stimato. Misura alcuni secondi (riproduzione o audio PC) poi togli il segno.", L"No se pudo estimar BPM. Mida unos segundos (reproduccion o audio PC) y quite la marca.",
				L"BPM 추정 실패. 재생 또는 PC 소리를 내며 몇 초 측정 후 체크 해제.", L"无法估计 BPM。请在播放或PC声音中测量数秒后再取消勾选。", L"تعذر تقدير BPM. قِس ثوانٍ أثناء التشغيل أو صوت الجهاز ثم ألغِ التحديد.", L"Не удалось оценить BPM. Измерьте несколько секунд при воспроизведении или звуке ПК и снимите галочку.", L"BPM nicht geschaetzt. Einige Sekunden bei Wiedergabe/PC-Audio messen, dann abwaehlen.",
				L"Nao foi possivel estimar BPM. Meca alguns segundos (reproducao ou audio do PC) e desmarque.", L"BPM niet geschat. Meet enkele seconden (afspelen of pc-audio) en vink uit.", L"Nie udalo sie oszacowac BPM. Mierz kilka sekund (odtwarzanie lub dzwiek PC) i odznacz.", L"BPM tahmin edilemedi. Calarken veya PC sesinde birkaç saniye ölçüp işareti kaldırın."),
			_T("BPM"), MB_OK | MB_ICONINFORMATION);
	}
}

// ---- Mirror output ----
static IAudioClient* g_mpMirrorClient = NULL;
static IAudioRenderClient* g_mpMirrorRender = NULL;
static UINT32 g_mpMirrorBufSize = 0;
static int g_mpMirrorFailed = 0;
static int g_mpMirrorRate = 0;
static int g_mpMirrorCh = 0;
static int g_mpMirrorBits = 0;
static int g_mpMirrorBpf = 0;

static void MpMirrorRelease()
{
	if (g_mpMirrorRender) { g_mpMirrorRender->Release(); g_mpMirrorRender = NULL; }
	if (g_mpMirrorClient) {
		g_mpMirrorClient->Stop();
		g_mpMirrorClient->Release();
		g_mpMirrorClient = NULL;
	}
	g_mpMirrorBufSize = 0;
	g_mpMirrorRate = 0;
	g_mpMirrorCh = 0;
	g_mpMirrorBits = 0;
	g_mpMirrorBpf = 0;
}

void MpMirrorShutdown()
{
	MpMirrorRelease();
	g_mpMirrorFailed = 0;
}

static BOOL MpMirrorEnsureInit()
{
	if (g_mpMirrorFailed || !savedata.mpMirrorOut) return FALSE;
	if (g_mpMirrorClient && g_mpMirrorRender) return TRUE;

	MpMirrorRelease();
	IMMDeviceEnumerator* en = NULL;
	IMMDevice* dev = NULL;
	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator), (void**)&en);
	if (FAILED(hr) || !en) { g_mpMirrorFailed = 1; return FALSE; }

	if (savedata.mpMirrorDevice[0]) {
		hr = en->GetDevice(savedata.mpMirrorDevice, &dev);
		if (FAILED(hr) || !dev) {
			CString id = savedata.mpMirrorDevice;
			IMMDeviceCollection* col = NULL;
			if (SUCCEEDED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col)) && col) {
				UINT n = 0;
				col->GetCount(&n);
				for (UINT i = 0; i < n && !dev; ++i) {
					IMMDevice* d = NULL;
					if (FAILED(col->Item(i, &d)) || !d) continue;
					LPWSTR pid = NULL;
					if (SUCCEEDED(d->GetId(&pid)) && pid) {
						if (id.CompareNoCase(pid) == 0)
							dev = d;
						else
							d->Release();
						CoTaskMemFree(pid);
					}
					else d->Release();
				}
				col->Release();
			}
		}
	}
	if (!dev) {
		hr = en->GetDefaultAudioEndpoint(eRender, eCommunications, &dev);
		if (FAILED(hr) || !dev)
			hr = en->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
	}
	en->Release();
	if (FAILED(hr) || !dev) { g_mpMirrorFailed = 1; return FALSE; }

	hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&g_mpMirrorClient);
	dev->Release();
	if (FAILED(hr) || !g_mpMirrorClient) { g_mpMirrorFailed = 1; return FALSE; }

	const int rate = (g_ds_pcm_rate >= 8000) ? g_ds_pcm_rate : 44100;
	const int ch = (g_ds_pcm_ch >= 1) ? g_ds_pcm_ch : 2;
	int bits = g_ds_pcm_bits;
	if (bits != 16 && bits != 24 && bits != 32) bits = 16;

	WAVEFORMATEX wfx = {};
	wfx.wFormatTag = WAVE_FORMAT_PCM;
	wfx.nChannels = (WORD)ch;
	wfx.nSamplesPerSec = (DWORD)rate;
	wfx.wBitsPerSample = (WORD)bits;
	wfx.nBlockAlign = (WORD)(ch * bits / 8);
	wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

	const REFERENCE_TIME bufPeriod = 40 * 10000;
	const REFERENCE_TIME bufDur = bufPeriod * 4;
	hr = g_mpMirrorClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, bufDur, 0, &wfx, NULL);
	if (FAILED(hr)) {
		MpMirrorRelease();
		g_mpMirrorFailed = 1;
		return FALSE;
	}
	g_mpMirrorClient->GetBufferSize(&g_mpMirrorBufSize);
	hr = g_mpMirrorClient->GetService(__uuidof(IAudioRenderClient), (void**)&g_mpMirrorRender);
	if (FAILED(hr) || !g_mpMirrorRender) {
		MpMirrorRelease();
		g_mpMirrorFailed = 1;
		return FALSE;
	}
	g_mpMirrorRate = rate;
	g_mpMirrorCh = ch;
	g_mpMirrorBits = bits;
	g_mpMirrorBpf = (int)wfx.nBlockAlign;
	g_mpMirrorClient->Start();
	return TRUE;
}

void MpMirrorWritePcm(const BYTE* pcm, int bytes)
{
	if (!pcm || bytes <= 0 || !savedata.mpMirrorOut || g_mpMirrorFailed) return;
	if (!MpMirrorEnsureInit()) return;

	const int vol = savedata.mpMirrorVol;
	if (vol <= 0) return;

	static BYTE scaled[65536];
	BYTE* dst = (BYTE*)pcm;
	int useLen = bytes;
	const int bits = g_mpMirrorBits;
	if (vol < 100 && bits == 16 && bytes <= (int)sizeof(scaled)) {
		const int samples = bytes / 2;
		const short* src = (const short*)pcm;
		short* out = (short*)scaled;
		for (int i = 0; i < samples; ++i) {
			int v = (int)src[i] * vol / 100;
			if (v > 32767) v = 32767;
			if (v < -32768) v = -32768;
			out[i] = (short)v;
		}
		dst = scaled;
	}
	else if (vol < 100 && bits == 32 && bytes <= (int)sizeof(scaled)) {
		const int samples = bytes / 4;
		const int* src = (const int*)pcm;
		int* out = (int*)scaled;
		for (int i = 0; i < samples; ++i) {
			long long v = ((long long)src[i] * vol) / 100;
			if (v > 2147483647LL) v = 2147483647LL;
			if (v < -2147483648LL) v = -2147483648LL;
			out[i] = (int)v;
		}
		dst = scaled;
	}
	else if (vol < 100 && bits == 24 && bytes <= (int)sizeof(scaled)) {
		const int samples = bytes / 3;
		for (int i = 0; i < samples; ++i) {
			int v = (int)pcm[i * 3] | ((int)pcm[i * 3 + 1] << 8) | ((int)((signed char)pcm[i * 3 + 2]) << 16);
			v = v * vol / 100;
			if (v > 8388607) v = 8388607;
			if (v < -8388608) v = -8388608;
			scaled[i * 3] = (BYTE)(v & 0xff);
			scaled[i * 3 + 1] = (BYTE)((v >> 8) & 0xff);
			scaled[i * 3 + 2] = (BYTE)((v >> 16) & 0xff);
		}
		dst = scaled;
	}
	// vol<100 でバッファ不足時はフル音量のまま通す(無音よりマシ)

	UINT32 pad = 0;
	if (FAILED(g_mpMirrorClient->GetCurrentPadding(&pad))) {
		g_mpMirrorFailed = 1;
		MpMirrorRelease();
		return;
	}
	const UINT32 room = (g_mpMirrorBufSize > pad) ? (g_mpMirrorBufSize - pad) : 0;
	const int bpf = (g_mpMirrorBpf > 0) ? g_mpMirrorBpf : ((g_outBytesPerFrame > 0) ? g_outBytesPerFrame : 4);
	const UINT32 framesNeed = (UINT32)(useLen / bpf);
	if (framesNeed == 0 || framesNeed > room) return;

	BYTE* pData = NULL;
	if (FAILED(g_mpMirrorRender->GetBuffer(framesNeed, &pData))) {
		g_mpMirrorFailed = 1;
		MpMirrorRelease();
		return;
	}
	memcpy(pData, dst, (size_t)useLen);
	g_mpMirrorRender->ReleaseBuffer(framesNeed, 0);
}

// ---- Remote HTTP (127.0.0.1) ----
static SOCKET g_mpRemoteListen = INVALID_SOCKET;
static HANDLE g_mpRemoteThread = NULL;
static volatile LONG g_mpRemoteStop = 0;
static HWND g_mpRemoteHwnd = NULL;
static int g_mpRemoteWsa = 0;

static void MpRemoteSendCmd(int cmd)
{
	if (g_mpRemoteHwnd && ::IsWindow(g_mpRemoteHwnd))
		::PostMessage(g_mpRemoteHwnd, WM_MP_TRANSPORT_CMD, (WPARAM)cmd, 0);
}

static void MpRemoteHandleRequest(SOCKET s)
{
	char buf[2048];
	int n = recv(s, buf, (int)sizeof(buf) - 1, 0);
	if (n <= 0) return;
	buf[n] = 0;
	char* line = buf;
	char* nl = strchr(line, '\n');
	if (nl) *nl = 0;

	const char* body =
		"HTTP/1.0 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n"
		"<html><body><h3>MP Remote</h3>"
		"<p><a href=\"/cmd?c=play\">Play</a> | "
		"<a href=\"/cmd?c=pause\">Pause</a> | "
		"<a href=\"/cmd?c=next\">Next</a> | "
		"<a href=\"/cmd?c=volup\">Vol+</a> | "
		"<a href=\"/cmd?c=voldn\">Vol-</a></p></body></html>";

	if (strstr(line, "GET /cmd")) {
		if (strstr(line, "c=play")) MpRemoteSendCmd(0);
		else if (strstr(line, "c=pause")) MpRemoteSendCmd(1);
		else if (strstr(line, "c=next")) MpRemoteSendCmd(2);
		else if (strstr(line, "c=volup")) MpRemoteSendCmd(3);
		else if (strstr(line, "c=voldn")) MpRemoteSendCmd(4);
		const char* ok = "HTTP/1.0 204 No Content\r\nConnection: close\r\n\r\n";
		send(s, ok, (int)strlen(ok), 0);
		return;
	}
	send(s, body, (int)strlen(body), 0);
}

static UINT MpRemoteThreadProc(LPVOID)
{
	while (InterlockedCompareExchange(&g_mpRemoteStop, 0, 0) == 0) {
		fd_set rf;
		FD_ZERO(&rf);
		FD_SET(g_mpRemoteListen, &rf);
		timeval tv = { 1, 0 };
		if (select(0, &rf, NULL, NULL, &tv) <= 0) continue;
		SOCKET c = accept(g_mpRemoteListen, NULL, NULL);
		if (c == INVALID_SOCKET) continue;
		MpRemoteHandleRequest(c);
		closesocket(c);
	}
	return 0;
}

void MpRemoteStop()
{
	InterlockedExchange(&g_mpRemoteStop, 1);
	if (g_mpRemoteListen != INVALID_SOCKET) {
		closesocket(g_mpRemoteListen);
		g_mpRemoteListen = INVALID_SOCKET;
	}
	if (g_mpRemoteThread) {
		WaitForSingleObject(g_mpRemoteThread, 3000);
		CloseHandle(g_mpRemoteThread);
		g_mpRemoteThread = NULL;
	}
	if (g_mpRemoteWsa) {
		WSACleanup();
		g_mpRemoteWsa = 0;
	}
}

void MpRemoteEnsureRunning(HWND notifyHwnd)
{
	g_mpRemoteHwnd = notifyHwnd;
	if (!savedata.mpRemoteOn) {
		MpRemoteStop();
		return;
	}
	if (g_mpRemoteThread && g_mpRemoteListen != INVALID_SOCKET) return;

	MpRemoteStop();
	InterlockedExchange(&g_mpRemoteStop, 0);
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;
	g_mpRemoteWsa = 1;

	g_mpRemoteListen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (g_mpRemoteListen == INVALID_SOCKET) {
		MpRemoteStop();
		return;
	}

	BOOL yes = 1;
	setsockopt(g_mpRemoteListen, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));

	sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	const int port = (savedata.mpRemotePort >= 1024 && savedata.mpRemotePort <= 65535)
		? savedata.mpRemotePort : 8765;
	addr.sin_port = htons((u_short)port);
	if (bind(g_mpRemoteListen, (sockaddr*)&addr, sizeof(addr)) != 0) {
		MpRemoteStop();
		return;
	}
	listen(g_mpRemoteListen, 4);
	g_mpRemoteThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)MpRemoteThreadProc, NULL, 0, NULL);
	if (!g_mpRemoteThread)
		MpRemoteStop();
}

// ---- MIDI In ----
static HMIDIIN g_mpMidiIn = NULL;
static HWND g_mpMidiHwnd = NULL;

static void CALLBACK MpMidiInCallback(HMIDIIN, UINT msg, DWORD_PTR, DWORD_PTR dw1, DWORD_PTR)
{
	if (msg != MIM_DATA || !g_mpMidiHwnd) return;
	const DWORD pack = (DWORD)dw1;
	const BYTE st = (BYTE)(pack & 0xFF);
	const BYTE d1 = (BYTE)((pack >> 8) & 0xFF);
	const BYTE d2 = (BYTE)((pack >> 16) & 0xFF);
	int cmd = -1;
	if ((st & 0xF0) == 0x90 && d2 > 0) {
		if (d1 == 60) cmd = 0;
		else if (d1 == 61) cmd = 1;
		else if (d1 == 62) cmd = 2;
	}
	else if ((st & 0xF0) == 0xB0) {
		if (d1 == 7) {
			cmd = (d2 >= 64) ? 3 : 4;
		}
	}
	if (cmd >= 0)
		::PostMessage(g_mpMidiHwnd, WM_MP_TRANSPORT_CMD, (WPARAM)cmd, 1);
}

void MpMidiInShutdown()
{
	if (g_mpMidiIn) {
		midiInStop(g_mpMidiIn);
		midiInClose(g_mpMidiIn);
		g_mpMidiIn = NULL;
	}
}

BOOL MpMidiInIsActive()
{
	return g_mpMidiIn != NULL;
}

void MpMidiInSetActive(BOOL on, HWND notifyHwnd)
{
	g_mpMidiHwnd = notifyHwnd;
	if (!on) {
		MpMidiInShutdown();
		return;
	}
	if (g_mpMidiIn) return;
	MMRESULT r = midiInOpen(&g_mpMidiIn, MIDI_MAPPER, (DWORD_PTR)MpMidiInCallback, 0, CALLBACK_FUNCTION);
	if (r != MMSYSERR_NOERROR) {
		g_mpMidiIn = NULL;
		return;
	}
	midiInStart(g_mpMidiIn);
}

LRESULT MpAddonsOnTransportCmd(CMediaPlayerDlg* mpDlg, WPARAM wParam, LPARAM)
{
	if (!mpDlg) return 0;
	switch ((int)wParam) {
	case 0: mpDlg->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_MP_PLAY, BN_CLICKED), 0); break;
	case 1: mpDlg->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_MP_PAUSE, BN_CLICKED), 0); break;
	case 2: mpDlg->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_MP_NEXT, BN_CLICKED), 0); break;
	case 3:
	case 4:
		if (og && og->m_sl.GetSafeHwnd()) {
			int v = og->m_sl.GetPos() / 1000;
			v += (wParam == 3) ? 5 : -5;
			if (v < 0) v = 0;
			if (v > 100) v = 100;
			og->m_sl.SetPos(v * 1000);
			og->PostMessage(WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, v * 1000), (LPARAM)og->m_sl.GetSafeHwnd());
		}
		break;
	default: break;
	}
	return 0;
}

// ---- Alarm ----
static int g_mpAlarmLastFireKey = -1;

void MpAlarmEnsureTimer(CMediaPlayerDlg* mpDlg)
{
	if (!mpDlg || !::IsWindow(mpDlg->GetSafeHwnd())) return;
	if (savedata.mpAlarmHour < 0 || savedata.mpAlarmHour > 23) {
		mpDlg->KillTimer(7);
		return;
	}
	mpDlg->SetTimer(7, 15000, NULL);
}

void MpAlarmTick(CMediaPlayerDlg* mpDlg)
{
	if (!mpDlg || savedata.mpAlarmHour < 0) return;
	SYSTEMTIME st;
	GetLocalTime(&st);
	if (st.wHour != (WORD)savedata.mpAlarmHour || st.wMinute != (WORD)savedata.mpAlarmMin)
		return;
	const int key = st.wHour * 100 + st.wMinute;
	if (key == g_mpAlarmLastFireKey) return;
	g_mpAlarmLastFireKey = key;
	if (ps == 0)
		mpDlg->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_MP_PLAY, BN_CLICKED), 0);
}

static BOOL MpPathLooksLikeVideo(LPCTSTR path)
{
	if (!path || !path[0]) return FALSE;
	LPCTSTR dot = _tcsrchr(path, _T('.'));
	if (!dot) return FALSE;
	if (_tcsicmp(dot, _T(".mp4")) == 0 || _tcsicmp(dot, _T(".mkv")) == 0 || _tcsicmp(dot, _T(".avi")) == 0
		|| _tcsicmp(dot, _T(".webm")) == 0 || _tcsicmp(dot, _T(".mov")) == 0 || _tcsicmp(dot, _T(".wmv")) == 0)
		return TRUE;
	return FALSE;
}

void MpOnVideoExtract(CMediaPlayerDlg* mpDlg)
{
	if (!mpDlg || !pl || !og) return;
	const int pc = mpDlg->GetSelectedPcIndex();
	if (pc < 0 || pc >= pl->playcnt) return;
	playlistdata0& item = pl->pc[pc];
	if (!MpPathLooksLikeVideo(item.fol)) {
		mpDlg->MessageBox(
			LL14(L"動画ファイルを選択してください。", L"Select a video file.", L"Selectionnez une video.", L"Seleziona un video.", L"Seleccione un video.",
				L"동영상 파일을 선택하세요.", L"请选择视频文件。", L"اختر ملف فيديو.", L"Выберите видеофайл.", L"Video waehlen.",
				L"Selecione um video.", L"Selecteer video.", L"Wybierz plik wideo.", L"Video dosyasi secin."),
			_T("Extract"), MB_OK);
		return;
	}
	CString out = item.fol;
	const int dot = out.ReverseFind(_T('.'));
	if (dot > 0) out = out.Left(dot);
	out += _T(".wav");
	if (GetFileAttributes(out) != INVALID_FILE_ATTRIBUTES) {
		if (mpDlg->MessageBox(
			LL14(L"WAV が既にあります。上書きしますか？", L"WAV exists. Overwrite?", L"WAV existe. Ecraser?", L"WAV esiste. Sovrascrivere?", L"WAV existe. ¿Sobrescribir?",
				L"WAV가 있습니다. 덮어쓸까요?", L"WAV 已存在。覆盖？", L"WAV موجود. استبدال؟", L"WAV есть. Перезаписать?", L"WAV vorhanden. Ueberschreiben?",
				L"WAV existe. Substituir?", L"WAV bestaat. Overschrijven?", L"WAV istnieje. Nadpisac?", L"WAV var. Uzerine yazilsin mi?"),
			_T("Extract"), MB_YESNO | MB_ICONQUESTION) != IDYES)
			return;
	}
	if (!og->ExportToWav(&item, out, 1, NULL, true)) {
		mpDlg->MessageBox(
			LL14(L"抽出に失敗しました。", L"Extract failed.", L"Echec extraction.", L"Estrazione fallita.", L"Extraccion fallida.",
				L"추출 실패.", L"提取失败。", L"فشل الاستخراج.", L"Ошибка извлечения.", L"Extraktion fehlgeschlagen.",
				L"Falha na extracao.", L"Extractie mislukt.", L"Ekstrakcja nie powiodla sie.", L"Cikarma basarisiz."),
			_T("Extract"), MB_OK | MB_ICONWARNING);
	}
}


// ---- 動画の音声差し替え: 映像は MF SourceReader、音声は外部 WAV → MP4(H264+AAC) ----
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

static BOOL MpReadWavPcm16(LPCTSTR path, short** outPcm, int* outFrames, int* outCh, int* outHz, BYTE** outOwned)
{
	*outPcm = NULL; *outFrames = 0; *outCh = 0; *outHz = 0; *outOwned = NULL;
	HANDLE h = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return FALSE;
	LARGE_INTEGER li = {};
	if (!GetFileSizeEx(h, &li) || li.QuadPart < 44 || li.QuadPart > (LONGLONG)2000 * 1024 * 1024) {
		CloseHandle(h); return FALSE;
	}
	const DWORD sz = (DWORD)li.QuadPart;
	BYTE* buf = (BYTE*)malloc(sz);
	if (!buf) { CloseHandle(h); return FALSE; }
	DWORD rd = 0;
	if (!ReadFile(h, buf, sz, &rd, NULL) || rd != sz) { free(buf); CloseHandle(h); return FALSE; }
	CloseHandle(h);
	if (memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) { free(buf); return FALSE; }
	DWORD pos = 12;
	WORD ch = 0, bps = 0; DWORD hz = 0; DWORD dataOff = 0, dataBytes = 0;
	while (pos + 8 <= sz) {
		DWORD id = *(DWORD*)(buf + pos);
		DWORD csz = *(DWORD*)(buf + pos + 4);
		pos += 8;
		if (pos + csz > sz) break;
		if (id == 0x20746d66) { // fmt
			if (csz >= 16) {
				WORD fmt = *(WORD*)(buf + pos);
				ch = *(WORD*)(buf + pos + 2);
				hz = *(DWORD*)(buf + pos + 4);
				bps = *(WORD*)(buf + pos + 14);
				if (fmt != 1 || (bps != 16 && bps != 24 && bps != 32)) { free(buf); return FALSE; }
			}
		} else if (id == 0x61746164) { // data
			dataOff = pos; dataBytes = csz; break;
		}
		pos += csz + (csz & 1);
	}
	if (!dataOff || !ch || !hz || bps != 16) { free(buf); return FALSE; }
	const int frames = (int)(dataBytes / (ch * 2));
	if (frames <= 0) { free(buf); return FALSE; }
	*outPcm = (short*)(buf + dataOff);
	*outFrames = frames;
	*outCh = (int)ch;
	*outHz = (int)hz;
	*outOwned = buf;
	return TRUE;
}

static HRESULT MpVrAddVideoH264(IMFSinkWriter* writer, DWORD* idx, UINT32 w, UINT32 h, UINT32 fpsNum, UINT32 fpsDen)
{
	IMFMediaType* outType = NULL;
	IMFMediaType* inType = NULL;
	HRESULT hr = MFCreateMediaType(&outType);
	if (SUCCEEDED(hr)) hr = outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	if (SUCCEEDED(hr)) hr = outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
	if (SUCCEEDED(hr)) hr = MFSetAttributeSize(outType, MF_MT_FRAME_SIZE, w, h);
	if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(outType, MF_MT_FRAME_RATE, fpsNum, fpsDen);
	if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AVG_BITRATE, 4000000);
	if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	DWORD vidx = 0;
	if (SUCCEEDED(hr)) hr = writer->AddStream(outType, &vidx);
	if (SUCCEEDED(hr)) hr = MFCreateMediaType(&inType);
	if (SUCCEEDED(hr)) hr = inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	if (SUCCEEDED(hr)) hr = inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
	if (SUCCEEDED(hr)) hr = MFSetAttributeSize(inType, MF_MT_FRAME_SIZE, w, h);
	if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(inType, MF_MT_FRAME_RATE, fpsNum, fpsDen);
	if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32)(w * 4));
	if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(vidx, inType, NULL);
	if (outType) outType->Release();
	if (inType) inType->Release();
	if (SUCCEEDED(hr) && idx) *idx = vidx;
	return hr;
}

static HRESULT MpVrAddAudioAac(IMFSinkWriter* writer, DWORD* idx, UINT32 hz, UINT32 ch)
{
	if (hz != 44100 && hz != 48000) hz = 48000;
	if (ch < 1) ch = 1;
	if (ch > 2) ch = 2;
	IMFMediaType* outType = NULL;
	IMFMediaType* inType = NULL;
	HRESULT hr = MFCreateMediaType(&outType);
	if (SUCCEEDED(hr)) hr = outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	if (SUCCEEDED(hr)) hr = outType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
	if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
	if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, hz);
	if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, ch);
	if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 192000 / 8);
	DWORD aidx = 0;
	if (SUCCEEDED(hr)) hr = writer->AddStream(outType, &aidx);
	if (SUCCEEDED(hr)) hr = MFCreateMediaType(&inType);
	if (SUCCEEDED(hr)) hr = inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	if (SUCCEEDED(hr)) hr = inType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
	if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
	if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, hz);
	if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, ch);
	if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, ch * 2);
	if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, hz * ch * 2);
	if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(aidx, inType, NULL);
	if (outType) outType->Release();
	if (inType) inType->Release();
	if (SUCCEEDED(hr) && idx) *idx = aidx;
	return hr;
}

static HRESULT MpVrWritePcm(IMFSinkWriter* writer, DWORD stream, const short* pcm, int frames, int ch, int hz, LONGLONG rt)
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

void MpOnVideoReplaceAudio(CMediaPlayerDlg* mpDlg)
{
	if (!mpDlg || !pl || !og) return;
	const int pc = mpDlg->GetSelectedPcIndex();
	if (pc < 0 || pc >= pl->playcnt) return;
	playlistdata0& item = pl->pc[pc];
	if (!MpPathLooksLikeVideo(item.fol)) {
		mpDlg->MessageBox(
			LL14(L"動画ファイルを選択してください。", L"Select a video file.", L"Selectionnez une video.", L"Seleziona un video.", L"Seleccione un video.",
				L"동영상 파일을 선택하세요.", L"请选择视频文件。", L"اختر ملف فيديو.", L"Выберите видеофайл.", L"Video waehlen.",
				L"Selecione um video.", L"Selecteer video.", L"Wybierz plik wideo.", L"Video dosyasi secin."),
			_T("Replace"), MB_OK);
		return;
	}
	CFileDialog wavDlg(TRUE, _T("wav"), NULL, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_ENABLESIZING,
		_T("WAV (*.wav)|*.wav|All (*.*)|*.*||"), mpDlg);
	if (wavDlg.DoModal() != IDOK) return;
	CString out = item.fol;
	const int dot = out.ReverseFind(_T('.'));
	if (dot > 0) out = out.Left(dot);
	out += _T("_replaced.mp4");
	if (GetFileAttributes(out) != INVALID_FILE_ATTRIBUTES) {
		if (mpDlg->MessageBox(
			LL14(L"出力 MP4 が既にあります。上書きしますか？", L"Output MP4 exists. Overwrite?", L"MP4 existe. Ecraser?", L"MP4 esiste. Sovrascrivere?", L"MP4 existe. ¿Sobrescribir?",
				L"출력 MP4가 있습니다. 덮어쓸까요?", L"输出 MP4 已存在。覆盖？", L"MP4 موجود. استبدال؟", L"MP4 есть. Перезаписать?", L"MP4 vorhanden. Ueberschreiben?",
				L"MP4 existe. Substituir?", L"MP4 bestaat. Overschrijven?", L"MP4 istnieje. Nadpisac?", L"MP4 var. Uzerine yazilsin mi?"),
			_T("Replace"), MB_YESNO | MB_ICONQUESTION) != IDYES)
			return;
	}

	short* pcm = NULL; int frames = 0, ch = 0, hz = 0; BYTE* owned = NULL;
	if (!MpReadWavPcm16(wavDlg.GetPathName(), &pcm, &frames, &ch, &hz, &owned)) {
		mpDlg->MessageBox(
			LL14(L"WAV を読めません（16bit PCM のみ）。", L"Cannot read WAV (16-bit PCM only).", L"Lecture WAV impossible (PCM 16 bits).", L"Impossibile leggere WAV (solo PCM 16 bit).", L"No se puede leer WAV (solo PCM 16 bit).",
				L"WAV를 읽을 수 없습니다(16bit PCM만).", L"无法读取 WAV（仅 16bit PCM）。", L"تعذر قراءة WAV (PCM 16 بت فقط).", L"Не удалось прочитать WAV (только PCM 16 бит).", L"WAV nicht lesbar (nur 16-bit PCM).",
				L"Nao foi possivel ler WAV (somente PCM 16 bits).", L"Kan WAV niet lezen (alleen 16-bit PCM).", L"Nie mozna odczytac WAV (tylko PCM 16-bit).", L"WAV okunamadi (yalniz 16-bit PCM)."),
			_T("Replace"), MB_OK | MB_ICONWARNING);
		return;
	}
	if (ch > 2) ch = 2;

	HRESULT hrCo = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	HRESULT hrMf = MFStartup(MF_VERSION);
	BOOL ok = FALSE;
	if (SUCCEEDED(hrMf)) {
		IMFSourceReader* reader = NULL;
		IMFSinkWriter* writer = NULL;
		hrMf = MFCreateSourceReaderFromURL(item.fol, NULL, &reader);
		if (FAILED(hrMf) || !reader) {
			CString url; url.Format(_T("file:///%s"), (LPCTSTR)item.fol); url.Replace(_T('\\'), _T('/'));
			hrMf = MFCreateSourceReaderFromURL(url, NULL, &reader);
		}
		if (SUCCEEDED(hrMf) && reader) {
			IMFMediaType* typ = NULL;
			if (SUCCEEDED(MFCreateMediaType(&typ)) && typ) {
				typ->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
				typ->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
				hrMf = reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, typ);
				typ->Release();
			}
			UINT32 vw = 0, vh = 0, fpsN = 30, fpsD = 1;
			IMFMediaType* cur = NULL;
			if (SUCCEEDED(reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &cur)) && cur) {
				MFGetAttributeSize(cur, MF_MT_FRAME_SIZE, &vw, &vh);
				MFGetAttributeRatio(cur, MF_MT_FRAME_RATE, &fpsN, &fpsD);
				cur->Release();
			}
			if (fpsN == 0 || fpsD == 0) { fpsN = 30; fpsD = 1; }
			if (vw >= 16 && vh >= 16) {
				hrMf = MFCreateSinkWriterFromURL(out, NULL, NULL, &writer);
				DWORD vIdx = 0, aIdx = 0;
				if (SUCCEEDED(hrMf) && writer) {
					hrMf = MpVrAddVideoH264(writer, &vIdx, vw, vh, fpsN, fpsD);
					if (SUCCEEDED(hrMf)) hrMf = MpVrAddAudioAac(writer, &aIdx, (UINT32)hz, (UINT32)ch);
					if (SUCCEEDED(hrMf)) hrMf = writer->BeginWriting();
				}
				if (SUCCEEDED(hrMf) && writer) {
					LONGLONG audioRt = 0;
					const int block = hz / 10; // 100ms
					int pcmPos = 0;
					ok = TRUE;
					for (;;) {
						DWORD streamIndex = 0, flags = 0; LONGLONG ts = 0;
						IMFSample* sample = NULL;
						HRESULT hr = reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
							0, &streamIndex, &flags, &ts, &sample);
						if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) break;
						if (!sample) continue;
						hr = writer->WriteSample(vIdx, sample);
						sample->Release();
						if (FAILED(hr)) { ok = FALSE; break; }
						while (pcmPos < frames && audioRt <= ts + 2000000LL) {
							int n = block;
							if (pcmPos + n > frames) n = frames - pcmPos;
							if (FAILED(MpVrWritePcm(writer, aIdx, pcm + pcmPos * ch, n, ch, hz, audioRt))) {
								ok = FALSE; break;
							}
							pcmPos += n;
							audioRt += (10000000LL * n) / hz;
						}
						if (!ok) break;
					}
					while (ok && pcmPos < frames) {
						int n = block;
						if (pcmPos + n > frames) n = frames - pcmPos;
						if (FAILED(MpVrWritePcm(writer, aIdx, pcm + pcmPos * ch, n, ch, hz, audioRt))) {
							ok = FALSE; break;
						}
						pcmPos += n;
						audioRt += (10000000LL * n) / hz;
					}
					if (FAILED(writer->Finalize())) ok = FALSE;
				}
			}
		}
		if (writer) writer->Release();
		if (reader) reader->Release();
		MFShutdown();
	}
	if (SUCCEEDED(hrCo) || hrCo == S_FALSE) CoUninitialize();
	if (owned) free(owned);

	if (!ok) {
		mpDlg->MessageBox(
			LL14(L"差し替えに失敗しました。", L"Replace failed.", L"Echec du remplacement.", L"Sostituzione fallita.", L"Reemplazo fallido.",
				L"교체 실패.", L"替换失败。", L"فشل الاستبدال.", L"Замена не удалась.", L"Ersetzen fehlgeschlagen.",
				L"Falha na substituicao.", L"Vervangen mislukt.", L"Zastepowanie nie powiodlo sie.", L"Degistirme basarisiz."),
			_T("Replace"), MB_OK | MB_ICONWARNING);
		return;
	}
	CString msg;
	msg.Format(LL14(L"書き出しました:\n%s", L"Wrote:\n%s", L"Ecrit:\n%s", L"Scritto:\n%s", L"Escrito:\n%s",
		L"저장됨:\n%s", L"已写出:\n%s", L"كُتب:\n%s", L"Записано:\n%s", L"Geschrieben:\n%s",
		L"Gravado:\n%s", L"Geschreven:\n%s", L"Zapisano:\n%s", L"Yazildi:\n%s"), (LPCTSTR)out);
	mpDlg->MessageBox(msg, _T("Replace"), MB_OK | MB_ICONINFORMATION);
}

void MpOnGameCapturePreset(CMediaPlayerDlg* mpDlg)
{
	savedata.cap_include_mp = 0;
	savedata.cap_with_audio = 1;
	MpPersistSavedataQuick();
	OpenScreenCaptureModeless(mpDlg);
	OpenDeviceRecordModeless(mpDlg);
}

void MpOnMidiInToggle(CMediaPlayerDlg* mpDlg)
{
	const BOOL on = (g_mpMidiIn == NULL);
	MpMidiInSetActive(on, mpDlg ? mpDlg->GetSafeHwnd() : NULL);
}

// ---- DJ Pad dialog ----
class CMpDjPadDlg : public CCustomBlurDialogBase
{
public:
	enum { IDD = IDD_MP_DJPAD };
	CMpDjPadDlg(CWnd* p = NULL) : CCustomBlurDialogBase(IDD, p) {}
	CCustomStandardButton m_pitchUp, m_pitchDn, m_tempoUp, m_tempoDn, m_vocal, m_msNarrow, m_msWide;
protected:
	virtual void DoDataExchange(CDataExchange* pDX)
	{
		CCustomBlurDialogBase::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_DJPAD_PITCH_UP, m_pitchUp);
		DDX_Control(pDX, IDC_DJPAD_PITCH_DN, m_pitchDn);
		DDX_Control(pDX, IDC_DJPAD_TEMPO_UP, m_tempoUp);
		DDX_Control(pDX, IDC_DJPAD_TEMPO_DN, m_tempoDn);
		DDX_Control(pDX, IDC_DJPAD_VOCAL, m_vocal);
		DDX_Control(pDX, IDC_DJPAD_MS_NARROW, m_msNarrow);
		DDX_Control(pDX, IDC_DJPAD_MS_WIDE, m_msWide);
	}
	virtual void PostNcDestroy() { g_mpDjPad = NULL; delete this; CCustomBlurDialogBase::PostNcDestroy(); }
	afx_msg void OnPitchUp() { CString e; MpPromptExecute(_T("pitch +3"), &e); }
	afx_msg void OnPitchDn() { CString e; MpPromptExecute(_T("pitch -3"), &e); }
	afx_msg void OnTempoUp() { CString e; MpPromptExecute(_T("tempo +3"), &e); }
	afx_msg void OnTempoDn() { CString e; MpPromptExecute(_T("tempo -3"), &e); }
	afx_msg void OnVocal() {
		savedata.mpVocalCenter = min(200, savedata.mpVocalCenter + 10);
		MpPersistSavedataQuick();
	}
	afx_msg void OnMsNarrow() {
		savedata.pro_ms_width = max(0, savedata.pro_ms_width - 10);
		MpPersistSavedataQuick();
	}
	afx_msg void OnMsWide() {
		savedata.pro_ms_width = min(200, savedata.pro_ms_width + 10);
		MpPersistSavedataQuick();
	}
	DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CMpDjPadDlg, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_DJPAD_PITCH_UP, &CMpDjPadDlg::OnPitchUp)
	ON_BN_CLICKED(IDC_DJPAD_PITCH_DN, &CMpDjPadDlg::OnPitchDn)
	ON_BN_CLICKED(IDC_DJPAD_TEMPO_UP, &CMpDjPadDlg::OnTempoUp)
	ON_BN_CLICKED(IDC_DJPAD_TEMPO_DN, &CMpDjPadDlg::OnTempoDn)
	ON_BN_CLICKED(IDC_DJPAD_VOCAL, &CMpDjPadDlg::OnVocal)
	ON_BN_CLICKED(IDC_DJPAD_MS_NARROW, &CMpDjPadDlg::OnMsNarrow)
	ON_BN_CLICKED(IDC_DJPAD_MS_WIDE, &CMpDjPadDlg::OnMsWide)
END_MESSAGE_MAP()

void CloseMpDjPadIfOpen()
{
	if (g_mpDjPad && ::IsWindow(g_mpDjPad->GetSafeHwnd()))
		g_mpDjPad->DestroyWindow();
	g_mpDjPad = NULL;
}

void OpenMpDjPadModeless(CWnd* parent)
{
	if (g_mpDjPad && ::IsWindow(g_mpDjPad->GetSafeHwnd())) {
		g_mpDjPad->ShowWindow(SW_SHOW);
		g_mpDjPad->SetForegroundWindow();
		return;
	}
	g_mpDjPad = new CMpDjPadDlg(parent);
	if (!g_mpDjPad->Create(IDD_MP_DJPAD, parent)) {
		delete g_mpDjPad;
		g_mpDjPad = NULL;
		return;
	}
	savedata.mpDjPadwindow = 1;
	MpPersistSavedataQuick();
	g_mpDjPad->ShowWindow(SW_SHOW);
}

// ---- Alarm settings dialog ----
class CMpAlarmDlg : public CCustomBlurDialogBase
{
public:
	enum { IDD = IDD_MP_ALARM };
	CMpAlarmDlg(CWnd* p = NULL) : CCustomBlurDialogBase(IDD, p) {}
	CCustomComboBox m_hour, m_min;
	CCustomCheckBox m_enable;
protected:
	virtual void DoDataExchange(CDataExchange* pDX)
	{
		CCustomBlurDialogBase::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_ALARM_HOUR, m_hour);
		DDX_Control(pDX, IDC_ALARM_MIN, m_min);
		DDX_Control(pDX, IDC_ALARM_ENABLE, m_enable);
	}
	virtual BOOL OnInitDialog()
	{
		CCustomBlurDialogBase::OnInitDialog();
		for (int h = 0; h < 24; ++h) {
			CString s; s.Format(_T("%02d"), h);
			m_hour.AddString(s);
		}
		for (int m = 0; m < 60; ++m) {
			CString s; s.Format(_T("%02d"), m);
			m_min.AddString(s);
		}
		if (savedata.mpAlarmHour >= 0) {
			m_enable.SetCheck(BST_CHECKED);
			m_hour.SetCurSel(savedata.mpAlarmHour);
			m_min.SetCurSel(savedata.mpAlarmMin);
		}
		return TRUE;
	}
	virtual void PostNcDestroy() { g_mpAlarmDlg = NULL; delete this; CCustomBlurDialogBase::PostNcDestroy(); }
	afx_msg void OnDestroy()
	{
		if (m_enable.GetCheck()) {
			savedata.mpAlarmHour = m_hour.GetCurSel();
			savedata.mpAlarmMin = m_min.GetCurSel();
		}
		else {
			savedata.mpAlarmHour = -1;
			savedata.mpAlarmMin = 0;
		}
		MpPersistSavedataQuick();
		if (mp) MpAlarmEnsureTimer(mp);
		CCustomBlurDialogBase::OnDestroy();
	}
	DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CMpAlarmDlg, CCustomBlurDialogBase)
	ON_WM_DESTROY()
END_MESSAGE_MAP()

void CloseMpAlarmDlgIfOpen()
{
	if (g_mpAlarmDlg && ::IsWindow(g_mpAlarmDlg->GetSafeHwnd()))
		g_mpAlarmDlg->DestroyWindow();
	g_mpAlarmDlg = NULL;
}

void OpenMpAlarmDlgModeless(CWnd* parent)
{
	if (g_mpAlarmDlg && ::IsWindow(g_mpAlarmDlg->GetSafeHwnd())) {
		g_mpAlarmDlg->ShowWindow(SW_SHOW);
		return;
	}
	g_mpAlarmDlg = new CMpAlarmDlg(parent);
	if (!g_mpAlarmDlg->Create(IDD_MP_ALARM, parent)) {
		delete g_mpAlarmDlg;
		g_mpAlarmDlg = NULL;
		return;
	}
	g_mpAlarmDlg->ShowWindow(SW_SHOW);
}

// ---- Mirror settings ----
class CMpMirrorDlg : public CCustomBlurDialogBase
{
public:
	enum { IDD = IDD_MP_MIRROR };
	CMpMirrorDlg(CWnd* p = NULL) : CCustomBlurDialogBase(IDD, p) {}
	CCustomCheckBox m_enable;
	CCustomSliderCtrl m_vol;
	CCustomComboBox m_dev;
protected:
	virtual void DoDataExchange(CDataExchange* pDX)
	{
		CCustomBlurDialogBase::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_MIRROR_ENABLE, m_enable);
		DDX_Control(pDX, IDC_MIRROR_VOL, m_vol);
		DDX_Control(pDX, IDC_MIRROR_DEV, m_dev);
	}
	virtual BOOL OnInitDialog()
	{
		CCustomBlurDialogBase::OnInitDialog();
		m_enable.SetCheck(savedata.mpMirrorOut ? BST_CHECKED : BST_UNCHECKED);
		m_vol.SetRange(0, 100);
		m_vol.SetPos(savedata.mpMirrorVol);
		m_dev.AddString(LL14(L"(既定)", L"(Default)", L"(Defaut)", L"(Predefinito)", L"(Predeterminado)",
			L"(기본)", L"(默认)", L"(افتراضي)", L"(По умолчанию)", L"(Standard)", L"(Padrao)", L"(Standaard)", L"(Domyslnie)", L"(Varsayilan)"));
		IMMDeviceEnumerator* en = NULL;
		if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
			__uuidof(IMMDeviceEnumerator), (void**)&en)) && en) {
			IMMDeviceCollection* col = NULL;
			if (SUCCEEDED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col)) && col) {
				UINT n = 0;
				col->GetCount(&n);
				for (UINT i = 0; i < n; ++i) {
					IMMDevice* d = NULL;
					if (FAILED(col->Item(i, &d)) || !d) continue;
					IPropertyStore* ps = NULL;
					LPWSTR pid = NULL;
					d->GetId(&pid);
					if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &ps)) && ps) {
						PROPVARIANT v;
						PropVariantInit(&v);
						if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) {
							const int idx = m_dev.AddString(v.pwszVal);
							if (pid && idx >= 0)
								m_dev.SetItemData(idx, (DWORD_PTR)_wcsdup(pid));
						}
						PropVariantClear(&v);
						ps->Release();
					}
					if (pid) CoTaskMemFree(pid);
					d->Release();
				}
				col->Release();
			}
			en->Release();
		}
		return TRUE;
	}
	virtual void PostNcDestroy() { g_mpMirrorDlg = NULL; delete this; CCustomBlurDialogBase::PostNcDestroy(); }
	afx_msg void OnDestroy()
	{
		savedata.mpMirrorOut = m_enable.GetCheck() ? 1 : 0;
		savedata.mpMirrorVol = m_vol.GetPos();
		const int sel = m_dev.GetCurSel();
		if (sel <= 0)
			savedata.mpMirrorDevice[0] = 0;
		else {
			const DWORD_PTR p = m_dev.GetItemData(sel);
			if (p) {
#ifdef UNICODE
				wcsncpy_s(savedata.mpMirrorDevice, (LPCWSTR)p, _TRUNCATE);
#else
				WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)p, -1, savedata.mpMirrorDevice, 256, NULL, NULL);
#endif
			}
		}
		const int n = m_dev.GetCount();
		for (int i = 0; i < n; ++i) {
			const DWORD_PTR p = m_dev.GetItemData(i);
			if (p) free((void*)p);
			m_dev.SetItemData(i, 0);
		}
		MpPersistSavedataQuick();
		MpMirrorShutdown();
		CCustomBlurDialogBase::OnDestroy();
	}
	DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CMpMirrorDlg, CCustomBlurDialogBase)
	ON_WM_DESTROY()
END_MESSAGE_MAP()

void CloseMpMirrorDlgIfOpen()
{
	if (g_mpMirrorDlg && ::IsWindow(g_mpMirrorDlg->GetSafeHwnd()))
		g_mpMirrorDlg->DestroyWindow();
	g_mpMirrorDlg = NULL;
}

void OpenMpMirrorDlgModeless(CWnd* parent)
{
	if (g_mpMirrorDlg && ::IsWindow(g_mpMirrorDlg->GetSafeHwnd())) {
		g_mpMirrorDlg->ShowWindow(SW_SHOW);
		return;
	}
	g_mpMirrorDlg = new CMpMirrorDlg(parent);
	if (!g_mpMirrorDlg->Create(IDD_MP_MIRROR, parent)) {
		delete g_mpMirrorDlg;
		g_mpMirrorDlg = NULL;
		return;
	}
	g_mpMirrorDlg->ShowWindow(SW_SHOW);
}

// ---- Remote settings ----
class CMpRemoteDlg : public CCustomBlurDialogBase
{
public:
	enum { IDD = IDD_MP_REMOTE };
	CMpRemoteDlg(CWnd* p = NULL) : CCustomBlurDialogBase(IDD, p) {}
	CCustomCheckBox m_enable;
	CCustomEdit m_port;
protected:
	virtual void DoDataExchange(CDataExchange* pDX)
	{
		CCustomBlurDialogBase::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_REMOTE_ENABLE, m_enable);
		DDX_Control(pDX, IDC_REMOTE_PORT, m_port);
	}
	virtual BOOL OnInitDialog()
	{
		CCustomBlurDialogBase::OnInitDialog();
		m_enable.SetCheck(savedata.mpRemoteOn ? BST_CHECKED : BST_UNCHECKED);
		CString p; p.Format(_T("%d"), savedata.mpRemotePort);
		m_port.SetWindowText(p);
		return TRUE;
	}
	virtual void PostNcDestroy() { g_mpRemoteDlg = NULL; delete this; CCustomBlurDialogBase::PostNcDestroy(); }
	afx_msg void OnDestroy()
	{
		savedata.mpRemoteOn = m_enable.GetCheck() ? 1 : 0;
		CString p; m_port.GetWindowText(p);
		int port = _ttoi(p);
		if (port >= 1024 && port <= 65535) savedata.mpRemotePort = port;
		MpPersistSavedataQuick();
		if (mp) MpRemoteEnsureRunning(mp->GetSafeHwnd());
		CCustomBlurDialogBase::OnDestroy();
	}
	DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CMpRemoteDlg, CCustomBlurDialogBase)
	ON_WM_DESTROY()
END_MESSAGE_MAP()

void CloseMpRemoteDlgIfOpen()
{
	if (g_mpRemoteDlg && ::IsWindow(g_mpRemoteDlg->GetSafeHwnd()))
		g_mpRemoteDlg->DestroyWindow();
	g_mpRemoteDlg = NULL;
}

void OpenMpRemoteDlgModeless(CWnd* parent)
{
	if (g_mpRemoteDlg && ::IsWindow(g_mpRemoteDlg->GetSafeHwnd())) {
		g_mpRemoteDlg->ShowWindow(SW_SHOW);
		return;
	}
	g_mpRemoteDlg = new CMpRemoteDlg(parent);
	if (!g_mpRemoteDlg->Create(IDD_MP_REMOTE, parent)) {
		delete g_mpRemoteDlg;
		g_mpRemoteDlg = NULL;
		return;
	}
	g_mpRemoteDlg->ShowWindow(SW_SHOW);
}

// ---- SS Visualizer ----
class CMpSsVizDlg : public CCustomBlurDialogBase
{
public:
	enum { IDD = IDD_MP_SSVIZ };
	CMpSsVizDlg(CWnd* p = NULL) : CCustomBlurDialogBase(IDD, p), m_memOld(NULL), m_tw(0), m_th(0) {}
	CDC m_mem;
	CBitmap m_bmp;
	CBitmap* m_memOld;
	int m_tw, m_th;
protected:
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs)
	{
		cs.dwExStyle |= WS_EX_TOPMOST;
		cs.style &= ~WS_CAPTION;
		cs.style |= WS_POPUP;
		return CCustomBlurDialogBase::PreCreateWindow(cs);
	}
	virtual BOOL OnInitDialog()
	{
		CCustomBlurDialogBase::OnInitDialog();
		ShowWindow(SW_SHOWMAXIMIZED);
		SetTimer(1, 33, NULL);
		return TRUE;
	}
	virtual void PostNcDestroy() { g_mpSsViz = NULL; delete this; CCustomBlurDialogBase::PostNcDestroy(); }
	virtual BOOL PreTranslateMessage(MSG* pMsg)
	{
		if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_ESCAPE) {
			DestroyWindow();
			return TRUE;
		}
		return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
	}
	afx_msg void OnTimer(UINT_PTR nIDEvent)
	{
		if (nIDEvent == 1) Invalidate(FALSE);
		CCustomBlurDialogBase::OnTimer(nIDEvent);
	}
	afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
	afx_msg void OnPaint()
	{
		CPaintDC dc(this);
		CRect rc; GetClientRect(&rc);
		const int w = rc.Width(), h = rc.Height();
		if (w <= 0 || h <= 0) return;
		if (w != m_tw || h != m_th) {
			if (m_mem.GetSafeHdc()) {
				m_mem.SelectObject(m_memOld);
				m_bmp.DeleteObject();
				m_mem.DeleteDC();
			}
			m_mem.CreateCompatibleDC(&dc);
			m_bmp.CreateCompatibleBitmap(&dc, w, h);
			m_memOld = m_mem.SelectObject(&m_bmp);
			m_tw = w; m_th = h;
		}
		m_mem.FillSolidRect(0, 0, w, h, RGB(8, 8, 16));
		const int bars = 64;
		const int gap = 2;
		const int bw = max(2, (w - gap * (bars + 1)) / bars);
		for (int i = 0; i < bars; ++i) {
			int v = spelv[i];
			if (v < 0) v = 0;
			if (v > 96) v = 96;
			const int bh = (v + 1) * h / 100;
			const int x = gap + i * (bw + gap);
			m_mem.FillSolidRect(x, h - bh, bw, bh, RGB(40, 180, 120));
		}
		dc.BitBlt(0, 0, w, h, &m_mem, 0, 0, SRCCOPY);
	}
	afx_msg void OnDestroy()
	{
		KillTimer(1);
		if (m_mem.GetSafeHdc()) {
			m_mem.SelectObject(m_memOld);
			m_bmp.DeleteObject();
			m_mem.DeleteDC();
		}
		CCustomBlurDialogBase::OnDestroy();
	}
	DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CMpSsVizDlg, CCustomBlurDialogBase)
	ON_WM_TIMER()
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_DESTROY()
END_MESSAGE_MAP()

void CloseMpSsVizIfOpen()
{
	if (g_mpSsViz && ::IsWindow(g_mpSsViz->GetSafeHwnd()))
		g_mpSsViz->DestroyWindow();
	g_mpSsViz = NULL;
}

void OpenMpSsVizModeless(CWnd* parent)
{
	if (g_mpSsViz && ::IsWindow(g_mpSsViz->GetSafeHwnd())) {
		g_mpSsViz->ShowWindow(SW_SHOWMAXIMIZED);
		return;
	}
	g_mpSsViz = new CMpSsVizDlg(parent);
	if (!g_mpSsViz->Create(IDD_MP_SSVIZ, parent)) {
		delete g_mpSsViz;
		g_mpSsViz = NULL;
		return;
	}
}

void MpAddonsShutdownAll()
{
	CloseMpDjPadIfOpen();
	CloseMpAlarmDlgIfOpen();
	CloseMpMirrorDlgIfOpen();
	CloseMpRemoteDlgIfOpen();
	CloseMpSsVizIfOpen();
	MpRemoteStop();
	MpMidiInShutdown();
	MpMirrorShutdown();
	g_bpmArmed = 0;
	g_bpmHeldPcAudio = 0;
	while (g_pcAudioRetain > 0)
		MpPcAudioRelease();
}
