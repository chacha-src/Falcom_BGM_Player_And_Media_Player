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

// ---- BPM: 音声ピーク封筒の自己相関（再生中に自動で結果ダイアログ） ----
enum { kBpmEnvCap = 400 }; // ~8秒 @ 50Hz
static float g_bpmEnv[kBpmEnvCap];
static int g_bpmEnvN = 0;
static int g_bpmEnvPos = 0;
static DWORD g_bpmEnvLastMs = 0;
static float g_bpmEnvWinMax = 0.f;
static int g_bpmArmed = 0;
static int g_bpmHeldPcAudio = 0;
static DWORD g_bpmArmedSince = 0;
static int g_bpmResultShown = 0;
static int g_bpmLastEstimate = 0;

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
	g_pcAudioPrevOff = 0;
}

BOOL MpBpmIsMeasuring()
{
	return g_bpmArmed ? TRUE : FALSE;
}

static int MpBpmEstimateAutocorr();
static void MpBpmFinishAndShow(BOOL showFailIfNone);
static void MpBpmPushEnv(float pk);

void MpBpmNotifyPeak(float peak)
{
	if (!g_bpmArmed || g_bpmResultShown) return;
	if (peak < 0.f) peak = 0.f;
	if (peak > 1.f) peak = 1.f;
	const DWORD now = GetTickCount();
	if (g_bpmEnvLastMs == 0) {
		g_bpmEnvLastMs = now;
		g_bpmEnvWinMax = peak;
		return;
	}
	if (peak > g_bpmEnvWinMax) g_bpmEnvWinMax = peak;
	// 20ms ごとに封筒サンプルを1つ積む（音声スレッド高頻度→50Hz）
	if (now - g_bpmEnvLastMs < 20)
		return;
	g_bpmEnvLastMs = now;
	MpBpmPushEnv(g_bpmEnvWinMax);
	g_bpmEnvWinMax = peak;
}

static void MpBpmPushEnv(float pk)
{
	g_bpmEnv[g_bpmEnvPos] = pk;
	g_bpmEnvPos = (g_bpmEnvPos + 1) % kBpmEnvCap;
	if (g_bpmEnvN < kBpmEnvCap) g_bpmEnvN++;
}

void MpBpmOnTimerTick()
{
	if (!g_bpmArmed || g_bpmResultShown) return;
	// UI側フォールバック（音声フィードが無い経路向け）
	float pk = ProAudio_LivePeak();
	{
		extern int spelv[400];
		float e = 0.f;
		for (int i = 0; i < 48; ++i) {
			float v = (float)spelv[i];
			if (v > e) e = v;
		}
		e *= (1.f / 96.f);
		if (e > pk) pk = e;
	}
	MpBpmNotifyPeak(pk);

	if (g_bpmEnvN >= 100) { // ~2秒分
		const int bpm = MpBpmEstimateAutocorr();
		if (bpm > 0) {
			g_bpmLastEstimate = bpm;
			MpBpmFinishAndShow(FALSE);
			return;
		}
	}
	if (g_bpmArmedSince && (GetTickCount() - g_bpmArmedSince) > 20000)
		MpBpmFinishAndShow(TRUE);
}

static int MpBpmEstimateAutocorr()
{
	if (g_bpmEnvN < 80) return 0;
	float env[kBpmEnvCap];
	const int n = g_bpmEnvN;
	const int start = (g_bpmEnvPos - n + kBpmEnvCap * 2) % kBpmEnvCap;
	for (int i = 0; i < n; ++i)
		env[i] = g_bpmEnv[(start + i) % kBpmEnvCap];

	float mean = 0.f;
	for (int i = 0; i < n; ++i) mean += env[i];
	mean /= (float)n;
	for (int i = 0; i < n; ++i) env[i] -= mean;

	const float sr = 50.f; // 20ms
	// 旧 60..180 だと高速曲(約177–190)の真ピークが範囲外→半テンポ(≈90)を拾う
	const int lagMin = (int)(60.f * sr / 220.f + 0.5f); // ~14
	const int lagMax = (int)(60.f * sr / 70.f + 0.5f);  // ~43
	float best = -1.f;
	int bestLag = 0;
	for (int lag = lagMin; lag <= lagMax && lag < n / 2; ++lag) {
		float c = 0.f;
		for (int i = 0; i + lag < n; ++i)
			c += env[i] * env[i + lag];
		if (c > best) { best = c; bestLag = lag; }
	}
	if (bestLag <= 0 || best <= 0.f) return 0;

	auto scoreAtLag = [&](int lag) -> float {
		if (lag < lagMin || lag > lagMax || lag >= n / 2) return -1.f;
		float c = 0.f;
		for (int i = 0; i + lag < n; ++i)
			c += env[i] * env[i + lag];
		return c;
	};

	// オクターブ候補: lag / 2lag / lag/2 のスコアを比較し、近いときは速め(110–200)を優先
	int candLag[3] = { bestLag, bestLag * 2, (bestLag >= 2) ? (bestLag / 2) : 0 };
	float candSc[3];
	for (int i = 0; i < 3; ++i)
		candSc[i] = (candLag[i] > 0) ? scoreAtLag(candLag[i]) : -1.f;

	int pick = 0;
	float pickSc = candSc[0];
	for (int i = 1; i < 3; ++i) {
		if (candSc[i] < 0.f) continue;
		const int bpmI = (int)(60.f * sr / (float)candLag[i] + 0.5f);
		const int bpmP = (int)(60.f * sr / (float)candLag[pick] + 0.5f);
		const bool iInSweet = (bpmI >= 110 && bpmI <= 200);
		const bool pInSweet = (bpmP >= 110 && bpmP <= 200);
		if (candSc[i] > pickSc * 1.08f
			|| (candSc[i] > pickSc * 0.92f && iInSweet && !pInSweet)
			|| (candSc[i] > pickSc * 0.97f && iInSweet && pInSweet && bpmI > bpmP)) {
			pick = i;
			pickSc = candSc[i];
		}
	}

	int bpm = (int)(60.f * sr / (float)candLag[pick] + 0.5f);
	if (bpm < 70) bpm *= 2;
	if (bpm > 220 && (bpm / 2) >= 70) bpm /= 2;
	if (bpm < 70 || bpm > 220) return 0;
	return bpm;
}

void MpBpmDetectFromPeaks()
{
	int bpm = g_bpmLastEstimate;
	if (bpm <= 0) bpm = MpBpmEstimateAutocorr();
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

static void MpBpmFinishAndShow(BOOL showFailIfNone)
{
	if (g_bpmResultShown) return;
	g_bpmResultShown = 1;
	g_bpmArmed = 0;
	MpBpmDetectFromPeaks();
	if (g_bpmHeldPcAudio) {
		MpPcAudioRelease();
		g_bpmHeldPcAudio = 0;
	}
	if (!mp) return;
	if (savedata.mpDetectedBpm <= 0) {
		if (showFailIfNone) {
			mp->MessageBox(
				LL14(L"BPM を推定できませんでした。\n曲を再生したまま数秒待ち、もう一度「BPM 計測」を試してください。",
					L"Could not estimate BPM.\nKeep playing for a few seconds, then try Measure BPM again.",
					L"BPM non estime. Lisez quelques secondes puis reessayez.",
					L"BPM non stimato. Riproduci alcuni secondi e riprova.",
					L"No se pudo estimar BPM. Reproduzca unos segundos y reintente.",
					L"BPM 추정 실패. 재생을 유지한 채 몇 초 후 다시 시도하세요.",
					L"无法估计 BPM。请保持播放数秒后重试。",
					L"تعذر تقدير BPM. أبقِ التشغيل ثوانٍ ثم أعد المحاولة.",
					L"Не удалось оценить BPM. Продолжайте воспроизведение и повторите.",
					L"BPM nicht geschaetzt. Wiedergabe fortsetzen und erneut versuchen.",
					L"Nao foi possivel estimar BPM. Continue reproduzindo e tente de novo.",
					L"BPM niet geschat. Speel door en probeer opnieuw.",
					L"Nie udalo sie oszacowac BPM. Odtwarzaj dalej i sprobuj ponownie.",
					L"BPM tahmin edilemedi. Calmaya devam edip tekrar deneyin."),
				LL14(L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM"),
				MB_OK | MB_ICONINFORMATION);
		}
		return;
	}
	savedata.mpBeatGrid = 1;
	if (mp->m_seek.GetSafeHwnd()) {
		mp->m_seek.SetBeatGrid((float)savedata.mpDetectedBpm, TRUE);
		mp->m_seek.Invalidate(FALSE);
	}
	MpPersistSavedataQuick();
	CString msg;
	msg.Format(LL14(
		L"BPM ≈ %d\nシークの拍グリッドに反映しました。",
		L"BPM ≈ %d\nApplied to the seek beat grid.",
		L"BPM ≈ %d\nApplique a la grille.",
		L"BPM ≈ %d\nApplicato alla griglia.",
		L"BPM ≈ %d\nAplicado a la rejilla.",
		L"BPM ≈ %d\n시크 비트 그리드에 반영했습니다.",
		L"BPM ≈ %d\n已应用到进度条拍网格。",
		L"BPM ≈ %d\nطُبّق على شبكة الإيقاع.",
		L"BPM ≈ %d\nПрименено к сетке долей.",
		L"BPM ≈ %d\nAuf Beat-Raster angewendet.",
		L"BPM ≈ %d\nAplicado na grade.",
		L"BPM ≈ %d\nToegepast op beatgrid.",
		L"BPM ≈ %d\nZastosowano do siatki.",
		L"BPM ≈ %d\nVurus izgarasina uygulandi."),
		savedata.mpDetectedBpm);
	mp->MessageBox(msg, LL14(L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM"), MB_OK | MB_ICONINFORMATION);
}

void MpOnBpmDetect(CMediaPlayerDlg* /*mpDlg*/)
{
	extern int playf;
	if (!g_bpmArmed) {
		g_bpmArmed = 1;
		g_bpmResultShown = 0;
		g_bpmEnvN = 0;
		g_bpmEnvPos = 0;
		g_bpmEnvLastMs = 0;
		g_bpmEnvWinMax = 0.f;
		g_bpmLastEstimate = 0;
		g_bpmArmedSince = GetTickCount();
		ZeroMemory(g_bpmEnv, sizeof(g_bpmEnv));
		if (!playf && !g_bpmHeldPcAudio) {
			MpPcAudioRetain();
			g_bpmHeldPcAudio = 1;
		}
		return;
	}
	MpBpmFinishAndShow(TRUE);
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

	CStringW page;
	page.Format(L"HTTP/1.0 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n"
		L"<html><body><h3>%s</h3><p>"
		L"<a href=\"/cmd?c=play\">%s</a> | "
		L"<a href=\"/cmd?c=pause\">%s</a> | "
		L"<a href=\"/cmd?c=next\">%s</a> | "
		L"<a href=\"/cmd?c=volup\">%s</a> | "
		L"<a href=\"/cmd?c=voldn\">%s</a></p></body></html>",
		(LPCWSTR)LL14(L"ローカルリモート", L"MP Remote", L"Telecommande", L"Remote MP", L"Remoto MP",
			L"로컬 리모트", L"本地遥控", L"تحكم محلي", L"Локальный пульт", L"Lokalfernbedienung",
			L"Remoto local", L"Lokale bediening", L"Pilot lokalny", L"Yerel uzaktan"),
		(LPCWSTR)LL14(L"再生", L"Play", L"Lecture", L"Play", L"Play", L"재생", L"播放", L"تشغيل", L"Играть", L"Play", L"Play", L"Play", L"Odtwarzaj", L"Oynat"),
		(LPCWSTR)LL14(L"一時停止", L"Pause", L"Pause", L"Pausa", L"Pausa", L"일시정지", L"暂停", L"إيقاف", L"Пауза", L"Pause", L"Pausar", L"Pauze", L"Pauza", L"Duraklat"),
		(LPCWSTR)LL14(L"次へ", L"Next", L"Suivant", L"Successivo", L"Siguiente", L"다음", L"下一首", L"التالي", L"След.", L"Weiter", L"Proximo", L"Volgende", L"Nastepny", L"Sonraki"),
		(LPCWSTR)LL14(L"音量+", L"Vol+", L"Vol+", L"Vol+", L"Vol+", L"볼륨+", L"音量+", L"صوت+", L"Громк.+", L"Laut+", L"Vol+", L"Vol+", L"Glosn.+", L"Ses+"),
		(LPCWSTR)LL14(L"音量-", L"Vol-", L"Vol-", L"Vol-", L"Vol-", L"볼륨-", L"音量-", L"صوت-", L"Громк.-", L"Laut-", L"Vol-", L"Vol-", L"Glosn.-", L"Ses-"));
	CStringA bodyA;
	{
		const int nbytes = ::WideCharToMultiByte(CP_UTF8, 0, page, -1, NULL, 0, NULL, NULL);
		if (nbytes > 1) {
			char* pb = bodyA.GetBufferSetLength(nbytes - 1);
			::WideCharToMultiByte(CP_UTF8, 0, page, -1, pb, nbytes, NULL, NULL);
			bodyA.ReleaseBuffer(nbytes - 1);
		}
	}
	const char* body = (LPCSTR)bodyA;

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
			LL14(L"抽出", L"Extract", L"Extraction", L"Estrazione", L"Extraccion", L"추출", L"提取", L"استخراج", L"Извлечение", L"Extraktion", L"Extracao", L"Extractie", L"Ekstrakcja", L"Cikarma"), MB_OK);
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
			LL14(L"抽出", L"Extract", L"Extraction", L"Estrazione", L"Extraccion", L"추출", L"提取", L"استخراج", L"Извлечение", L"Extraktion", L"Extracao", L"Extractie", L"Ekstrakcja", L"Cikarma"), MB_YESNO | MB_ICONQUESTION) != IDYES)
			return;
	}
	if (!og->ExportToWav(&item, out, 1, NULL, true)) {
		mpDlg->MessageBox(
			LL14(L"抽出に失敗しました。", L"Extract failed.", L"Echec extraction.", L"Estrazione fallita.", L"Extraccion fallida.",
				L"추출 실패.", L"提取失败。", L"فشل الاستخراج.", L"Ошибка извлечения.", L"Extraktion fehlgeschlagen.",
				L"Falha na extracao.", L"Extractie mislukt.", L"Ekstrakcja nie powiodla sie.", L"Cikarma basarisiz."),
			LL14(L"抽出", L"Extract", L"Extraction", L"Estrazione", L"Extraccion", L"추출", L"提取", L"استخراج", L"Извлечение", L"Extraktion", L"Extracao", L"Extractie", L"Ekstrakcja", L"Cikarma"), MB_OK | MB_ICONWARNING);
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
			LL14(L"差し替え", L"Replace", L"Remplacer", L"Sostituisci", L"Reemplazar", L"교체", L"替换", L"استبدال", L"Замена", L"Ersetzen", L"Substituir", L"Vervangen", L"Zamien", L"Degistir"), MB_OK);
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
			LL14(L"差し替え", L"Replace", L"Remplacer", L"Sostituisci", L"Reemplazar", L"교체", L"替换", L"استبدال", L"Замена", L"Ersetzen", L"Substituir", L"Vervangen", L"Zamien", L"Degistir"), MB_YESNO | MB_ICONQUESTION) != IDYES)
			return;
	}

	short* pcm = NULL; int frames = 0, ch = 0, hz = 0; BYTE* owned = NULL;
	if (!MpReadWavPcm16(wavDlg.GetPathName(), &pcm, &frames, &ch, &hz, &owned)) {
		mpDlg->MessageBox(
			LL14(L"WAV を読めません（16bit PCM のみ）。", L"Cannot read WAV (16-bit PCM only).", L"Lecture WAV impossible (PCM 16 bits).", L"Impossibile leggere WAV (solo PCM 16 bit).", L"No se puede leer WAV (solo PCM 16 bit).",
				L"WAV를 읽을 수 없습니다(16bit PCM만).", L"无法读取 WAV（仅 16bit PCM）。", L"تعذر قراءة WAV (PCM 16 بت فقط).", L"Не удалось прочитать WAV (только PCM 16 бит).", L"WAV nicht lesbar (nur 16-bit PCM).",
				L"Nao foi possivel ler WAV (somente PCM 16 bits).", L"Kan WAV niet lezen (alleen 16-bit PCM).", L"Nie mozna odczytac WAV (tylko PCM 16-bit).", L"WAV okunamadi (yalniz 16-bit PCM)."),
			LL14(L"差し替え", L"Replace", L"Remplacer", L"Sostituisci", L"Reemplazar", L"교체", L"替换", L"استبدال", L"Замена", L"Ersetzen", L"Substituir", L"Vervangen", L"Zamien", L"Degistir"), MB_OK | MB_ICONWARNING);
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
			LL14(L"差し替え", L"Replace", L"Remplacer", L"Sostituisci", L"Reemplazar", L"교체", L"替换", L"استبدال", L"Замена", L"Ersetzen", L"Substituir", L"Vervangen", L"Zamien", L"Degistir"), MB_OK | MB_ICONWARNING);
		return;
	}
	CString msg;
	msg.Format(LL14(L"書き出しました:\n%s", L"Wrote:\n%s", L"Ecrit:\n%s", L"Scritto:\n%s", L"Escrito:\n%s",
		L"저장됨:\n%s", L"已写出:\n%s", L"كُتب:\n%s", L"Записано:\n%s", L"Geschrieben:\n%s",
		L"Gravado:\n%s", L"Geschreven:\n%s", L"Zapisano:\n%s", L"Yazildi:\n%s"), (LPCTSTR)out);
	mpDlg->MessageBox(msg, LL14(L"差し替え", L"Replace", L"Remplacer", L"Sostituisci", L"Reemplazar", L"교체", L"替换", L"استبدال", L"Замена", L"Ersetzen", L"Substituir", L"Vervangen", L"Zamien", L"Degistir"), MB_OK | MB_ICONINFORMATION);
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
	CToolTipCtrl m_tooltip;
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
	virtual BOOL OnInitDialog()
	{
		CCustomBlurDialogBase::OnInitDialog();
		SetWindowText(LL14(L"DJ パッド", L"DJ Pad", L"Pad DJ", L"Pad DJ", L"Pad DJ",
			L"DJ 패드", L"DJ 垫", L"لوحة DJ", L"DJ-панель", L"DJ-Pad",
			L"Pad DJ", L"DJ-pad", L"Pad DJ", L"DJ paneli"));
		m_pitchUp.SetWindowText(LL14(L"音程 +", L"Pitch +", L"Hauteur +", L"Pitch +", L"Tono +", L"음정 +", L"音高 +", L"درجة +", L"Тон +", L"Tonhöhe +", L"Tom +", L"Toonhoogte +", L"Wysokosc +", L"Perde +"));
		m_pitchDn.SetWindowText(LL14(L"音程 -", L"Pitch -", L"Hauteur -", L"Pitch -", L"Tono -", L"음정 -", L"音高 -", L"درجة -", L"Тон -", L"Tonhöhe -", L"Tom -", L"Toonhoogte -", L"Wysokosc -", L"Perde -"));
		m_tempoUp.SetWindowText(LL14(L"テンポ +", L"Tempo +", L"Tempo +", L"Tempo +", L"Tempo +", L"템포 +", L"速度 +", L"إيقاع +", L"Темп +", L"Tempo +", L"Tempo +", L"Tempo +", L"Tempo +", L"Tempo +"));
		m_tempoDn.SetWindowText(LL14(L"テンポ -", L"Tempo -", L"Tempo -", L"Tempo -", L"Tempo -", L"템포 -", L"速度 -", L"إيقاع -", L"Темп -", L"Tempo -", L"Tempo -", L"Tempo -", L"Tempo -", L"Tempo -"));
		m_vocal.SetWindowText(LL14(L"ボーカル強調", L"Vocal +", L"Voix +", L"Voce +", L"Voz +", L"보컬 강조", L"人声增强", L"صوت +", L"Вокал +", L"Gesang +", L"Vocal +", L"Zang +", L"Wokal +", L"Vokal +"));
		m_msNarrow.SetWindowText(LL14(L"MS 狭く", L"MS Narrow", L"MS etroit", L"MS stretto", L"MS estrecho", L"MS 좁게", L"MS 变窄", L"MS ضيق", L"MS узко", L"MS eng", L"MS estreito", L"MS smal", L"MS wasko", L"MS dar"));
		m_msWide.SetWindowText(LL14(L"MS 広く", L"MS Wide", L"MS large", L"MS ampio", L"MS ancho", L"MS 넓게", L"MS 变宽", L"MS واسع", L"MS широко", L"MS weit", L"MS largo", L"MS breed", L"MS szeroko", L"MS genis"));
		m_pitchUp.SetGradation(RGB(220, 240, 255), RGB(170, 210, 250), 0, TRUE);
		m_pitchDn.SetGradation(RGB(220, 240, 255), RGB(170, 210, 250), 0, TRUE);
		m_tempoUp.SetGradation(RGB(220, 245, 230), RGB(170, 220, 190), 0, TRUE);
		m_tempoDn.SetGradation(RGB(220, 245, 230), RGB(170, 220, 190), 0, TRUE);
		m_vocal.SetGradation(RGB(255, 230, 245), RGB(255, 180, 220), 0, TRUE);
		m_msNarrow.SetGradation(RGB(235, 230, 255), RGB(200, 185, 250), 0, TRUE);
		m_msWide.SetGradation(RGB(235, 230, 255), RGB(200, 185, 250), 0, TRUE);
		CCustomControlUtility::BeginDialogToolTip(m_tooltip, this, TTS_NOPREFIX);
		m_tooltip.AddTool(&m_pitchUp, LL14(L"音程を +3 します。", L"Raise pitch by +3.", L"Monter la hauteur de +3.", L"Alza il pitch di +3.", L"Sube el tono +3.", L"음정을 +3.", L"音高 +3。", L"رفع الدرجة +3.", L"Поднять тон на +3.", L"Tonhöhe +3.", L"Tom +3.", L"Toonhoogte +3.", L"Wysokosc +3.", L"Perde +3."));
		m_tooltip.AddTool(&m_pitchDn, LL14(L"音程を -3 します。", L"Lower pitch by -3.", L"Baisser la hauteur de -3.", L"Abbassa il pitch di -3.", L"Baja el tono -3.", L"음정을 -3.", L"音高 -3。", L"خفض الدرجة -3.", L"Опустить тон на -3.", L"Tonhöhe -3.", L"Tom -3.", L"Toonhoogte -3.", L"Wysokosc -3.", L"Perde -3."));
		m_tooltip.AddTool(&m_tempoUp, LL14(L"テンポを +3% します。", L"Raise tempo by +3%.", L"Augmenter le tempo de +3%.", L"Aumenta il tempo del +3%.", L"Sube el tempo +3%.", L"템포 +3%.", L"速度 +3%。", L"رفع الإيقاع +3%.", L"Ускорить темп на +3%.", L"Tempo +3%.", L"Tempo +3%.", L"Tempo +3%.", L"Tempo +3%.", L"Tempo +3%."));
		m_tooltip.AddTool(&m_tempoDn, LL14(L"テンポを -3% します。", L"Lower tempo by -3%.", L"Baisser le tempo de -3%.", L"Riduci il tempo del -3%.", L"Baja el tempo -3%.", L"템포 -3%.", L"速度 -3%。", L"خفض الإيقاع -3%.", L"Замедлить темп на -3%.", L"Tempo -3%.", L"Tempo -3%.", L"Tempo -3%.", L"Tempo -3%.", L"Tempo -3%."));
		m_tooltip.AddTool(&m_vocal, LL14(L"センター（ボーカル）成分を強調します。", L"Boost center/vocal component.", L"Renforcer la voix (centre).", L"Enfatizza la voce (centro).", L"Refuerza la voz (centro).", L"보컬(센터) 강조.", L"增强人声（中置）。", L"تعزيز الصوت المركزي.", L"Усилить вокал (центр).", L"Gesang (Mitte) betonen.", L"Realcar vocal (centro).", L"Zang (midden) versterken.", L"Wzmocnij wokal (srodek).", L"Vokali (orta) vurgula."));
		m_tooltip.AddTool(&m_msNarrow, LL14(L"M/S 幅を狭くします（センター寄り）。", L"Narrow M/S width (more center).", L"Reduire la largeur M/S.", L"Restringi la larghezza M/S.", L"Reduce el ancho M/S.", L"M/S 폭을 좁힘.", L"缩小 M/S 宽度。", L"تضييق عرض M/S.", L"Сузить ширину M/S.", L"M/S-Breite verengen.", L"Estreitar largura M/S.", L"M/S-breedte vernauwen.", L"Zwez szerokosc M/S.", L"M/S genisligini daralt."));
		m_tooltip.AddTool(&m_msWide, LL14(L"M/S 幅を広くします（サイド寄り）。", L"Widen M/S width (more sides).", L"Elargir la largeur M/S.", L"Allarga la larghezza M/S.", L"Amplia el ancho M/S.", L"M/S 폭을 넓힘.", L"加宽 M/S 宽度。", L"توسيع عرض M/S.", L"Расширить ширину M/S.", L"M/S-Breite erweitern.", L"Alargar largura M/S.", L"M/S-breedte verbreden.", L"Poszerz szerokosc M/S.", L"M/S genisligini genislet."));
		CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 360, 8000);
		return TRUE;
	}
	virtual BOOL PreTranslateMessage(MSG* pMsg)
	{
		if (m_tooltip.GetSafeHwnd()) m_tooltip.RelayEvent(pMsg);
		return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
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
	CToolTipCtrl m_tooltip;
	virtual BOOL OnInitDialog()
	{
		CCustomBlurDialogBase::OnInitDialog();
		SetWindowText(LL14(L"アラーム", L"Alarm", L"Alarme", L"Sveglia", L"Alarma",
			L"알람", L"闹钟", L"منبه", L"Будильник", L"Wecker",
			L"Alarme", L"Wekker", L"Budzik", L"Alarm"));
		m_enable.SetWindowText(LL14(L"有効", L"Enable", L"Activer", L"Abilita", L"Activar",
			L"사용", L"启用", L"تفعيل", L"Включить", L"Aktiv",
			L"Ativar", L"Inschakelen", L"Wlacz", L"Etkin"));
		SetDlgItemText(IDC_ALARM_HOUR_L, LL14(L"時", L"Hour", L"Heure", L"Ora", L"Hora",
			L"시", L"时", L"ساعة", L"Час", L"Stunde", L"Hora", L"Uur", L"Godz.", L"Saat"));
		SetDlgItemText(IDC_ALARM_MIN_L, LL14(L"分", L"Min", L"Min", L"Min", L"Min",
			L"분", L"分", L"دقيقة", L"Мин", L"Min", L"Min", L"Min", L"Min", L"Dk"));
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
		} else {
			SYSTEMTIME st = {};
			::GetLocalTime(&st);
			m_hour.SetCurSel(st.wHour);
			m_min.SetCurSel(st.wMinute);
		}
		CCustomControlUtility::BeginDialogToolTip(m_tooltip, this, TTS_NOPREFIX);
		m_tooltip.AddTool(&m_enable, LL14(L"指定時刻に再生を開始します（一致する分に一度）。", L"Start playback at the set time (once per matching minute).", L"Demarre la lecture a l'heure (une fois).", L"Avvia la riproduzione all'ora (una volta).", L"Inicia la reproduccion a la hora (una vez).",
			L"지정 시각에 재생 시작(일치 분에 1회).", L"在指定时刻开始播放（匹配分钟一次）。", L"بدء التشغيل في الوقت المحدد.", L"Запуск в заданное время (раз в минуту).", L"Wiedergabe zur Zeit starten (einmal).",
			L"Inicia a reproducao no horario (uma vez).", L"Start afspelen op tijd (eenmaal).", L"Uruchom o zadanej godzinie (raz).", L"Belirlenen saatte calmayi baslat (bir kez)."));
		m_tooltip.AddTool(&m_hour, LL14(L"アラームの時 (0–23)。", L"Alarm hour (0–23).", L"Heure (0–23).", L"Ora (0–23).", L"Hora (0–23).", L"시 (0–23).", L"时 (0–23).", L"ساعة (0–23).", L"Час (0–23).", L"Stunde (0–23).", L"Hora (0–23).", L"Uur (0–23).", L"Godzina (0–23).", L"Saat (0–23)."));
		m_tooltip.AddTool(&m_min, LL14(L"アラームの分 (0–59)。", L"Alarm minute (0–59).", L"Minute (0–59).", L"Minuto (0–59).", L"Minuto (0–59).", L"분 (0–59).", L"分 (0–59).", L"دقيقة (0–59).", L"Минута (0–59).", L"Minute (0–59).", L"Minuto (0–59).", L"Minuut (0–59).", L"Minuta (0–59).", L"Dakika (0–59)."));
		CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 360, 8000);
		return TRUE;
	}
	virtual BOOL PreTranslateMessage(MSG* pMsg)
	{
		if (m_tooltip.GetSafeHwnd()) m_tooltip.RelayEvent(pMsg);
		return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
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
	CToolTipCtrl m_tooltip;
	virtual BOOL OnInitDialog()
	{
		CCustomBlurDialogBase::OnInitDialog();
		SetWindowText(LL14(L"ミラー出力", L"Mirror output", L"Sortie miroir", L"Uscita mirror", L"Salida espejo",
			L"미러 출력", L"镜像输出", L"خرج مرآة", L"Зеркальный выход", L"Spiegelausgabe",
			L"Saida espelho", L"Spiegelaudio", L"Wyjscie lustrzane", L"Ayna cikis"));
		m_enable.SetWindowText(LL14(L"ミラー ON", L"Mirror on", L"Miroir ON", L"Mirror ON", L"Espejo ON",
			L"미러 ON", L"镜像开", L"المرآة تشغيل", L"Зеркало ВКЛ", L"Spiegel AN",
			L"Espelho ON", L"Spiegel AAN", L"Lustro ON", L"Ayna AC"));
		SetDlgItemText(IDC_MIRROR_VOL_L, LL14(L"音量", L"Vol", L"Vol", L"Vol", L"Vol",
			L"볼륨", L"音量", L"صوت", L"Громк.", L"Laut", L"Vol", L"Vol", L"Glosn.", L"Ses"));
		SetDlgItemText(IDC_MIRROR_DEV_L, LL14(L"デバイス", L"Device", L"Peripherique", L"Dispositivo", L"Dispositivo",
			L"장치", L"设备", L"جهاز", L"Устройство", L"Gerät", L"Dispositivo", L"Apparaat", L"Urzadzenie", L"Aygit"));
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
		CCustomControlUtility::BeginDialogToolTip(m_tooltip, this, TTS_NOPREFIX);
		m_tooltip.AddTool(&m_enable, LL14(L"別の再生デバイスへ同時出力します。", L"Also output to another playback device.", L"Sortir aussi vers un autre peripherique.", L"Invia anche a un altro dispositivo.", L"Salida tambien a otro dispositivo.",
			L"다른 재생 장치로도 출력.", L"同时输出到另一播放设备。", L"الإخراج أيضاً لجهاز آخر.", L"Также выводить на другое устройство.", L"Zusätzlich auf anderes Gerät ausgeben.",
			L"Tambem sair para outro dispositivo.", L"Ook uitvoeren naar ander apparaat.", L"Takze wyprowadzaj na inne urzadzenie.", L"Baska bir aygita da cik."));
		m_tooltip.AddTool(&m_vol, LL14(L"ミラー側の音量 (0–100)。", L"Mirror volume (0–100).", L"Volume miroir (0–100).", L"Volume mirror (0–100).", L"Volumen espejo (0–100).",
			L"미러 볼륨 (0–100).", L"镜像音量 (0–100)。", L"مستوى المرآة (0–100).", L"Громкость зеркала (0–100).", L"Spiegel-Lautstärke (0–100).",
			L"Volume do espelho (0–100).", L"Spiegelvolume (0–100).", L"Glosnosc lustra (0–100).", L"Ayna sesi (0–100)."));
		m_tooltip.AddTool(&m_dev, LL14(L"ミラー出力先デバイス。", L"Target device for mirror output.", L"Peripherique de sortie miroir.", L"Dispositivo di destinazione.", L"Dispositivo de destino.",
			L"미러 출력 장치.", L"镜像输出设备。", L"جهاز خرج المرآة.", L"Устройство зеркального выхода.", L"Zielgerät für Spiegelausgabe.",
			L"Dispositivo de saida espelho.", L"Doelapparaat voor spiegel.", L"Docelowe urzadzenie lustra.", L"Ayna cikis aygiti."));
		CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 360, 8000);
		return TRUE;
	}
	virtual BOOL PreTranslateMessage(MSG* pMsg)
	{
		if (m_tooltip.GetSafeHwnd()) m_tooltip.RelayEvent(pMsg);
		return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
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
	CToolTipCtrl m_tooltip;
	virtual BOOL OnInitDialog()
	{
		CCustomBlurDialogBase::OnInitDialog();
		SetWindowText(LL14(L"ローカルリモート", L"Local remote", L"Telecommande locale", L"Remote locale", L"Remoto local",
			L"로컬 리모트", L"本地遥控", L"تحكم محلي", L"Локальный пульт", L"Lokalfernbedienung",
			L"Remoto local", L"Lokale bediening", L"Pilot lokalny", L"Yerel uzaktan"));
		m_enable.SetWindowText(LL14(L"localhost で HTTP", L"HTTP on localhost", L"HTTP sur localhost", L"HTTP su localhost", L"HTTP en localhost",
			L"localhost HTTP", L"本机 HTTP", L"HTTP على localhost", L"HTTP на localhost", L"HTTP auf localhost",
			L"HTTP em localhost", L"HTTP op localhost", L"HTTP na localhost", L"localhost HTTP"));
		SetDlgItemText(IDC_REMOTE_PORT_L, LL14(L"ポート", L"Port", L"Port", L"Porta", L"Puerto",
			L"포트", L"端口", L"منفذ", L"Порт", L"Port", L"Porta", L"Poort", L"Port", L"Port"));
		m_enable.SetCheck(savedata.mpRemoteOn ? BST_CHECKED : BST_UNCHECKED);
		CString p; p.Format(_T("%d"), savedata.mpRemotePort);
		m_port.SetWindowText(p);
		CCustomControlUtility::BeginDialogToolTip(m_tooltip, this, TTS_NOPREFIX);
		m_tooltip.AddTool(&m_enable, LL14(L"ブラウザから再生操作できる簡易 HTTP サーバを起動します。", L"Start a simple HTTP server for browser transport control.", L"Demarrer un serveur HTTP simple pour controler la lecture.", L"Avvia un semplice server HTTP per il controllo.", L"Inicia un servidor HTTP simple para control.",
			L"브라우저로 조작하는 간이 HTTP 서버를 켭니다.", L"启动简易 HTTP 服务器以便浏览器控制播放。", L"تشغيل خادم HTTP بسيط للتحكم.", L"Запустить простой HTTP-сервер для управления.", L"Einfachen HTTP-Server für Steuerung starten.",
			L"Inicia um servidor HTTP simples para controle.", L"Start een eenvoudige HTTP-server voor bediening.", L"Uruchom prosty serwer HTTP do sterowania.", L"Tarayicidan kontrol icin basit HTTP sunucusu baslat."));
		m_tooltip.AddTool(&m_port, LL14(L"待ち受けポート (1024–65535)。", L"Listen port (1024–65535).", L"Port d'ecoute (1024–65535).", L"Porta di ascolto (1024–65535).", L"Puerto de escucha (1024–65535).",
			L"수신 포트 (1024–65535).", L"监听端口 (1024–65535)。", L"منفذ الاستماع (1024–65535).", L"Порт прослушивания (1024–65535).", L"Listenport (1024–65535).",
			L"Porta de escuta (1024–65535).", L"Luisterpoort (1024–65535).", L"Port nasluchu (1024–65535).", L"Dinleme portu (1024–65535)."));
		CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 360, 8000);
		return TRUE;
	}
	virtual BOOL PreTranslateMessage(MSG* pMsg)
	{
		if (m_tooltip.GetSafeHwnd()) m_tooltip.RelayEvent(pMsg);
		return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
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
		SetWindowText(LL14(L"SS ビジュアライザ (Escで閉じる)", L"SS visualizer (Esc to close)", L"Visualiseur SS (Esc)", L"Visualizzatore SS (Esc)", L"Visualizador SS (Esc)",
			L"SS 비주얼 (Esc로 닫기)", L"SS 可视化（Esc关闭）", L"عارض SS (Esc للإغلاق)", L"SS-визуализатор (Esc)", L"SS-Visualizer (Esc)",
			L"Visual SS (Esc)", L"SS-visualizer (Esc)", L"Wizual SS (Esc)", L"SS gorsel (Esc)"));
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
	afx_msg void OnContextMenu(CWnd*, CPoint point)
	{
		if (point.x == -1 && point.y == -1) {
			CRect r; GetWindowRect(&r);
			point.x = r.left + 40; point.y = r.top + 40;
		}
		CMenu menu;
		menu.CreatePopupMenu();
		menu.AppendMenu(MF_STRING, 1,
			LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
		const int cmd = (int)menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, this);
		if (cmd == 1)
			DestroyWindow();
	}
	afx_msg void OnRButtonUp(UINT, CPoint point)
	{
		ClientToScreen(&point);
		OnContextMenu(this, point);
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
			// モノ=0.. / ステレオL=100.. / R=200..。STオン時は 0台が空のままになり真っ黒になる
			int v = spelv[i];
			if (spelv[100 + i] > v) v = spelv[100 + i];
			if (spelv[200 + i] > v) v = spelv[200 + i];
			if (v < 0) v = 0;
			if (v > 96) v = 96;
			const int bh = max(2, (v + 1) * h / 100);
			const int x = gap + i * (bw + gap);
			const COLORREF c = (v > 70) ? RGB(80, 255, 160) : RGB(40, 180, 120);
			m_mem.FillSolidRect(x, h - bh, bw, bh, c);
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
	ON_WM_CONTEXTMENU()
	ON_WM_RBUTTONUP()
END_MESSAGE_MAP()

void CloseMpSsVizIfOpen()
{
	if (g_mpSsViz && ::IsWindow(g_mpSsViz->GetSafeHwnd()))
		g_mpSsViz->DestroyWindow();
	g_mpSsViz = NULL;
}

BOOL MpSsVizIsOpen()
{
	return (g_mpSsViz && ::IsWindow(g_mpSsViz->GetSafeHwnd())) ? TRUE : FALSE;
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
