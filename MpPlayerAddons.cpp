#include "StdAfx.h"
#include "XfadePlayback.h"
#include "MpPlayerAddons.h"
#include "AudioDevSync.h"
#include "CMediaPlayerDlg.h"
#include "CPromptEngine.h"
#include "ProAudio.h"
#include "oggDlg.h"
#include "CPianoRoll.h"
#include "CAnalyzerDlg.h"
#include "DeviceRecordDlg.h"
#include "ScreenCaptureDlg.h"
#include "ScWgcCapture.h"
#include "CCustomPopupMenu.h"
#include "CProToolsDlg.h"
#include "PlayList.h"
#include "AudioUpscaler.h"
#include "SongParams.h"
#include "CEqualizer.h"
#include "MpKeyCamelot.h"
#include "MpFeatureExtras.h"
#include "MpRemoteEqEnvLabels.inc"
#include "Douga.h"

#include <mmdeviceapi.h>
#include <audioclient.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mmsystem.h>
#include <propsys.h>
#include <functiondiscoverykeys_devpkey.h>
#include <math.h>
#include <windowsx.h>
#include <uxtheme.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <codecapi.h>
#include <wmcodecdsp.h>
#include <gdiplus.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "wmcodecdspuuid.lib")

#ifndef MF_E_TRANSFORM_NEED_MORE_INPUT
#define MF_E_TRANSFORM_NEED_MORE_INPUT ((HRESULT)0xC00D6D72L)
#endif
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

extern save savedata;
extern COggDlg* og;
extern CPlayList* pl;
extern void MpPersistSavedataQuick();
extern int g_oggSubUiRestoring;
extern int spelv[400];
extern int tempo;
extern int pitch;
extern int ps;
extern int plf;
extern int mode;
extern CMediaPlayerDlg* mp;
extern CProToolsDlg* g_proToolsDlg;
extern int wavbit_sample_Hz;
extern int g_ds_pcm_rate;
extern int g_ds_pcm_ch;
extern int g_ds_pcm_bits;
extern int g_outBytesPerFrame;
extern CString fnn, tagname, tagfile, tagalbum;
extern CString stitle;
extern BOOL videoonly;
extern CDouga* pMainFrame1;
extern IMediaPosition* pMediaPosition;
extern IBasicVideo* pBasicVideo;
extern BOOL ev;

class CMpDjPadDlg;
class CMpAlarmDlg;
class CMpMirrorDlg;
class CMpRemoteDlg;
class CMpSsVizDlg;
class CMpBpmDlg;
static CMpDjPadDlg* g_mpDjPad = NULL;
static int s_djPadAppExit = 0; // 1=アプリ終了中（mpDjPadwindow を落とさない）
static CMpAlarmDlg* g_mpAlarmDlg = NULL;
static CMpMirrorDlg* g_mpMirrorDlg = NULL;
static CMpRemoteDlg* g_mpRemoteDlg = NULL;
static CMpSsVizDlg* g_mpSsViz = NULL;
static CMpBpmDlg* g_mpBpmDlg = NULL;

// ---- BPM: ~500Hz間引き→アタック新規性→IOI/拍子/音符価の多段合意 ----
// レートはラベルを基本とし、壁時計は 44.1↔48 誤認検出のみ。
enum { kBpmDecimHz = 500 };
enum { kBpmEnvCap = kBpmDecimHz * 20 }; // 最大約20秒
enum { kBpmHistMax = 48 };
enum { kBpmPassNeed = 10 };
enum { kBpmPassExtra = 10 };
enum { kBpmPassCap = 40 }; // 弱い合意でもここまで積み上げて詰める
enum { kBpmPeakMax = 512 };
enum { kBpmIoiBins = 256 };

static float g_bpmEnv[kBpmEnvCap];
static float g_bpmWork[kBpmEnvCap];
static int g_bpmEnvN = 0;
static int g_bpmEnvPos = 0;
static float g_bpmDecimSum = 0.f;
static int g_bpmDecimCnt = 0;
static int g_bpmDecimNeed = 0;
static int g_bpmSrcRate = 0;
static float g_bpmAbsAvg = 0.f;
static float g_bpmPrevAbs = 0.f;
static float g_bpmEffSr = 500.f;
static int g_bpmArmed = 0;
static int g_bpmHeldPcAudio = 0;
static DWORD g_bpmArmedSince = 0;
static int g_bpmResultShown = 0;
static int g_bpmLastEstimate = 0;
static int g_bpmHist[kBpmHistMax];
static int g_bpmHistMeterNum[kBpmHistMax];
static int g_bpmHistMeterDen[kBpmHistMax];
static int g_bpmHistPulse[kBpmHistMax];
static float g_bpmHistScore[kBpmHistMax];
static int g_bpmHistN = 0;
static DWORD g_bpmHistLastPushMs = 0;
static int g_bpmPassCount = 0;
static int g_bpmNeedPasses = kBpmPassNeed;
static int g_bpmLastCands[3] = { 0, 0, 0 };
static int g_bpmLastAcoustic = 0;
static int g_bpmLastAcoustic2 = 0;
static int g_bpmLastMeterNum = 0;
static int g_bpmLastMeterDen = 0;
static int g_bpmLastPulse = 0;
static float g_bpmLastConf = 0.f;
static float g_bpmLastStrength = 0.f; // 1パス分のAC強度（合意に使う）
static int g_bpmConsensusBpm = 0;
static int g_bpmConsensusMeterNum = 0;
static int g_bpmConsensusMeterDen = 0;
static int g_bpmConsensusPulse = 0;
static int g_bpmUiFail = 0;
static int g_bpmConfirmPending = 0; // 0=なし 1=仮収束待ち確認 2=収束済み・緻密待ち
static int g_bpmConfirmRoundN = 0;  // 確認/緻密の周回数（無限ループ防止）
static int g_bpmProvisionalBpm = 0;
static int g_bpmProvisionalMeterNum = 0;
static int g_bpmProvisionalMeterDen = 0;
static int g_bpmProvisionalPulse = 0;
static int g_bpmLastRoundBpm = 0; // 直近ラウンドの生結果（UI表示用）
static float g_bpmLastRoundConf = 0.f;
static LONGLONG g_bpmQpcFreq = 0;
static LONGLONG g_bpmQpcStart = 0;
static double g_bpmFramesSeen = 0.0;
static int g_bpmRateReady = 0;

static void OpenMpBpmMeasureDlg(CWnd* parent);
static void CloseMpBpmMeasureDlgIfOpen();
static void MpBpmDlgRefreshUi();

static int MpBpmMedianOf(int* v, int n)
{
	if (n <= 0) return 0;
	for (int i = 1; i < n; ++i) {
		const int x = v[i];
		int j = i;
		while (j > 0 && v[j - 1] > x) { v[j] = v[j - 1]; --j; }
		v[j] = x;
	}
	return v[n / 2];
}

static void MpBpmApplyRate(int rate)
{
	if (rate < 8000) rate = 8000;
	if (rate > 384000) rate = 384000;
	{
		static const int kStd[] = {
			8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000
		};
		int best = rate;
		double bestRel = 0.015;
		for (int i = 0; i < (int)(sizeof(kStd) / sizeof(kStd[0])); ++i) {
			const int s = kStd[i];
			const double rel = fabs((double)rate - (double)s) / (double)s;
			if (rel < bestRel) {
				bestRel = rel;
				best = s;
			}
		}
		rate = best;
	}
	if (g_bpmSrcRate == rate && g_bpmDecimNeed > 0) return;
	g_bpmSrcRate = rate;
	g_bpmDecimNeed = rate / kBpmDecimHz;
	if (g_bpmDecimNeed < 1) g_bpmDecimNeed = 1;
	g_bpmEffSr = (float)rate / (float)g_bpmDecimNeed;
	g_bpmDecimSum = 0.f;
	g_bpmDecimCnt = 0;
}

static void MpBpmResetCapture()
{
	g_bpmEnvN = 0;
	g_bpmEnvPos = 0;
	g_bpmDecimSum = 0.f;
	g_bpmDecimCnt = 0;
	g_bpmDecimNeed = 0;
	g_bpmSrcRate = 0;
	g_bpmAbsAvg = 0.f;
	g_bpmPrevAbs = 0.f;
	g_bpmEffSr = 500.f;
	g_bpmLastEstimate = 0;
	g_bpmHistN = 0;
	g_bpmHistLastPushMs = 0;
	g_bpmPassCount = 0;
	g_bpmNeedPasses = kBpmPassNeed;
	g_bpmLastCands[0] = g_bpmLastCands[1] = g_bpmLastCands[2] = 0;
	g_bpmLastAcoustic = 0;
	g_bpmLastAcoustic2 = 0;
	g_bpmLastMeterNum = 0;
	g_bpmLastMeterDen = 0;
	g_bpmLastPulse = 0;
	g_bpmLastConf = 0.f;
	g_bpmLastStrength = 0.f;
	g_bpmConsensusBpm = 0;
	g_bpmConsensusMeterNum = 0;
	g_bpmConsensusMeterDen = 0;
	g_bpmConsensusPulse = 0;
	g_bpmUiFail = 0;
	// 確認用の仮結果は Reset では消さない（一致判定のため）
	g_bpmQpcFreq = 0;
	g_bpmQpcStart = 0;
	g_bpmFramesSeen = 0.0;
	g_bpmRateReady = 0;
	ZeroMemory(g_bpmEnv, sizeof(g_bpmEnv));
	ZeroMemory(g_bpmHist, sizeof(g_bpmHist));
	ZeroMemory(g_bpmHistMeterNum, sizeof(g_bpmHistMeterNum));
	ZeroMemory(g_bpmHistMeterDen, sizeof(g_bpmHistMeterDen));
	ZeroMemory(g_bpmHistPulse, sizeof(g_bpmHistPulse));
	ZeroMemory(g_bpmHistScore, sizeof(g_bpmHistScore));
}

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

static void MpBpmFinishAndShow(BOOL showFailIfNone);
static int MpBpmEstimateAutocorr();
static void MpDjScratchCapturePcm(const float* L, const float* R, int frames, int sampleRate);
static void MpDjSeekToSliderPos(int pos);
static void MpDjScratchBegin();
static void MpDjScratchEnd();
static void MpDjScratchSetVelocity(float degPerSec);
static float MpDjScratchSpeedScale();

// 計測対象が再生中と違う／停止中なら、対象曲を頭から再演奏する
static void MpBpmEnsureMeasurePlayback()
{
	extern int playf;
	extern CString filen;
	extern int plcnt;
	extern int gameon;
	extern void MpPushPlayHistory(LPCTSTR path, LPCTSTR displayName);

	int target = -1;
	if (mp)
		target = mp->GetSelectedPcIndex();
	if ((target < 0 || !pl || target >= pl->playcnt) && pl) {
		if (pl->pnt1 >= 0 && pl->pnt1 < pl->playcnt) target = pl->pnt1;
		else if (pl->pnt >= 0 && pl->pnt < pl->playcnt) target = pl->pnt;
	}
	if (!pl || target < 0 || target >= pl->playcnt) {
		if (!playf && og)
			og->play();
		return;
	}

	const BOOL samePlaying = (playf
		&& pl->pnt == target
		&& !filen.IsEmpty()
		&& _tcsicmp(filen, pl->pc[target].fol) == 0);
	if (samePlaying)
		return;

	// 選択曲へ切替して頭から再生（鳴っている曲と違う計測対象対策）
	pl->Get(target);
	plcnt = target;
	gameon = 0;
	if (mp) {
		mp->m_abApos = -1;
		mp->m_abBpos = -1;
		mp->m_abLoopCount = 0;
		mp->m_seekHoldUntil = 0;
		if (mp->m_seek.GetSafeHwnd())
			mp->m_seek.SetAB(-1, -1);
		mp->ClearWaveOverview();
	}
	MpPushPlayHistory(pl->pc[target].fol, pl->pc[target].name);
	if (!OggPrepareResumeBeforePlayback(pl->pc[target].fol))
		return;
	if (og && ::IsWindow(og->GetSafeHwnd()))
		RequestPlaybackRestart(og->GetSafeHwnd());
}

// 再生PCM経路。~500Hz 間引き＋アタック（ABS上昇）新規性
void MpBpmNotifyPcm(const float* L, const float* R, int frames, int sampleRate)
{
	if (L && R && frames > 0 && sampleRate >= 8000)
		MpDjScratchCapturePcm(L, R, frames, sampleRate);
	if (!g_bpmArmed || g_bpmResultShown) return;
	if (!L || !R || frames <= 0 || sampleRate < 8000) return;

	int label = sampleRate;
	if (wavbit_sample_Hz >= 8000 && wavbit_sample_Hz <= 384000)
		label = wavbit_sample_Hz;
	if (g_pcm_upscale_active && g_ds_pcm_rate >= 8000
		&& label == g_ds_pcm_rate
		&& wavbit_sample_Hz >= 8000 && wavbit_sample_Hz != g_ds_pcm_rate)
		label = wavbit_sample_Hz;

	LARGE_INTEGER qpc = {};
	QueryPerformanceCounter(&qpc);
	if (g_bpmQpcFreq <= 0) {
		LARGE_INTEGER f = {};
		QueryPerformanceFrequency(&f);
		g_bpmQpcFreq = f.QuadPart;
		g_bpmQpcStart = qpc.QuadPart;
		g_bpmFramesSeen = 0.0;
		// 初回は仮レートのみ。投票は壁時計で確定するまで待たせる（44.1↔48 誤認で101等になるのを防ぐ）
		MpBpmApplyRate(label);
		g_bpmRateReady = 0;
	}

	g_bpmFramesSeen += (double)frames;
	if (g_bpmQpcFreq > 0) {
		const double elapsed = (double)(qpc.QuadPart - g_bpmQpcStart) / (double)g_bpmQpcFreq;
		if (elapsed >= 1.0 && g_bpmFramesSeen > 8000.0) {
			const double meas = g_bpmFramesSeen / elapsed;
			const int prevRate = g_bpmSrcRate;
			int rate = label;
			const int std441 = 44100, std48 = 48000;
			const double d441 = fabs(meas - (double)std441) / (double)std441;
			const double d48 = fabs(meas - (double)std48) / (double)std48;
			// 壁時計を優先。ファイルレートとデバイス表記が 44.1/48 で食い違うときも実測に従う
			if (d441 < 0.04 || d48 < 0.04) {
				if (d441 <= d48) rate = std441;
				else rate = std48;
			} else if (wavbit_sample_Hz == std441 || wavbit_sample_Hz == std48) {
				rate = wavbit_sample_Hz;
			} else {
				rate = label;
			}
			MpBpmApplyRate(rate);
			// レート訂正で封筒の時間軸が壊れる → 取り直し（誤峰→101 の主因）
			if (prevRate > 0 && prevRate != g_bpmSrcRate) {
				g_bpmEnvN = 0;
				g_bpmEnvPos = 0;
				g_bpmDecimSum = 0.f;
				g_bpmDecimCnt = 0;
				g_bpmAbsAvg = 0.f;
				g_bpmPrevAbs = 0.f;
				g_bpmHistN = 0;
				g_bpmPassCount = 0;
				g_bpmNeedPasses = kBpmPassNeed;
				g_bpmHistLastPushMs = 0;
				ZeroMemory(g_bpmEnv, sizeof(g_bpmEnv));
			}
			g_bpmRateReady = 1;
		}
	}
	if (g_bpmDecimNeed < 1)
		MpBpmApplyRate(label);

	for (int i = 0; i < frames; ++i) {
		const float m = 0.5f * (L[i] + R[i]);
		g_bpmDecimSum += m;
		g_bpmDecimCnt++;
		if (g_bpmDecimCnt < g_bpmDecimNeed) continue;

		const float s = g_bpmDecimSum / (float)g_bpmDecimCnt;
		g_bpmDecimSum = 0.f;
		g_bpmDecimCnt = 0;

		const float a = fabsf(s);
		g_bpmPrevAbs = a;
		g_bpmAbsAvg += 0.20f * (a - g_bpmAbsAvg);
		// 旧実装どおり純ABS封筒（rise混入は細分IOI／変拍子に引っ張られやすい）
		g_bpmEnv[g_bpmEnvPos] = g_bpmAbsAvg;
		g_bpmEnvPos = (g_bpmEnvPos + 1) % kBpmEnvCap;
		if (g_bpmEnvN < kBpmEnvCap) g_bpmEnvN++;
	}
}

void MpBpmNotifyPeak(float /*peak*/)
{
}

static void MpBpmVotePush(int bpm, int meterNum, int meterDen, int pulse, float score)
{
	if (bpm < 40 || bpm > 240) return;
	const DWORD now = GetTickCount();
	if (g_bpmHistLastPushMs && (now - g_bpmHistLastPushMs) < 400)
		return;
	g_bpmHistLastPushMs = now;
	if (g_bpmHistN < kBpmHistMax) {
		g_bpmHist[g_bpmHistN] = bpm;
		g_bpmHistMeterNum[g_bpmHistN] = meterNum;
		g_bpmHistMeterDen[g_bpmHistN] = meterDen;
		g_bpmHistPulse[g_bpmHistN] = pulse;
		g_bpmHistScore[g_bpmHistN] = score;
		g_bpmHistN++;
	} else {
		for (int i = 1; i < kBpmHistMax; ++i) {
			g_bpmHist[i - 1] = g_bpmHist[i];
			g_bpmHistMeterNum[i - 1] = g_bpmHistMeterNum[i];
			g_bpmHistMeterDen[i - 1] = g_bpmHistMeterDen[i];
			g_bpmHistPulse[i - 1] = g_bpmHistPulse[i];
			g_bpmHistScore[i - 1] = g_bpmHistScore[i];
		}
		g_bpmHist[kBpmHistMax - 1] = bpm;
		g_bpmHistMeterNum[kBpmHistMax - 1] = meterNum;
		g_bpmHistMeterDen[kBpmHistMax - 1] = meterDen;
		g_bpmHistPulse[kBpmHistMax - 1] = pulse;
		g_bpmHistScore[kBpmHistMax - 1] = score;
	}
	g_bpmPassCount++;
}

// 直近票から合意BPM・拍子・音符価・信頼度を更新。戻り値: 十分合意ならTRUE
// スコアが弱い／関連テンポが競合するときは試行上限を段階的に上げ、安易に確定しない。
static BOOL MpBpmUpdateConsensus()
{
	g_bpmConsensusBpm = 0;
	g_bpmConsensusMeterNum = 0;
	g_bpmConsensusMeterDen = 0;
	g_bpmConsensusPulse = 0;
	g_bpmLastConf = 0.f;
	if (g_bpmHistN <= 0) return FALSE;

	int tmp[kBpmHistMax];
	for (int i = 0; i < g_bpmHistN; ++i) tmp[i] = g_bpmHist[i];
	const int med = MpBpmMedianOf(tmp, g_bpmHistN);
	if (med <= 0) return FALSE;

	auto relatedDist = [](int a, int b) -> int {
		if (a <= 0 || b <= 0) return 999;
		int best = a - b;
		if (best < 0) best = -best;
		const int cand[6] = {
			(a * 3) / 4, (a * 4) / 3, (a * 2) / 3, (a * 3) / 2, a * 2, a / 2
		};
		for (int i = 0; i < 6; ++i) {
			int d = cand[i] - b;
			if (d < 0) d = -d;
			if (d < best) best = d;
		}
		const int candB[6] = {
			(b * 3) / 4, (b * 4) / 3, (b * 2) / 3, (b * 3) / 2, b * 2, b / 2
		};
		for (int i = 0; i < 6; ++i) {
			int d = candB[i] - a;
			if (d < 0) d = -d;
			if (d < best) best = d;
		}
		return best;
	};
	auto countNear = [&](int center) -> int {
		// 高速帯は 161–165 のブレを同一クラスタとして数える
		const int tol = (center >= 145) ? 3 : 1;
		int c = 0;
		for (int i = 0; i < g_bpmHistN; ++i) {
			int d = g_bpmHist[i] - center;
			if (d < 0) d = -d;
			if (d <= tol) c++;
		}
		return c;
	};
	auto countSupport = [&](int center) -> int {
		const int tol = (center >= 145) ? 3 : 1;
		int c = 0;
		for (int i = 0; i < g_bpmHistN; ++i) {
			if (relatedDist(g_bpmHist[i], center) <= tol)
				c++;
		}
		return c;
	};

	int nearExact = countNear(med);
	int farCnt = 0;
	for (int i = 0; i < g_bpmHistN; ++i) {
		int d = g_bpmHist[i] - med;
		if (d < 0) d = -d;
		if (d >= 4 && relatedDist(g_bpmHist[i], med) > 1)
			farCnt++;
	}

	// 関連クラスタで最大の対抗峰
	int rivalBpm = 0, rivalCnt = 0;
	{
		int rel[8];
		int nRel = 0;
		auto pushRel = [&](int b) {
			if (b < 40 || b > 240) return;
			for (int i = 0; i < nRel; ++i) if (rel[i] == b) return;
			if (nRel < 8) rel[nRel++] = b;
		};
		pushRel((med * 3) / 4);
		pushRel((med * 4) / 3);
		pushRel((med * 2) / 3);
		pushRel((med * 3) / 2);
		pushRel(med * 2);
		pushRel(med / 2);
		for (int i = 0; i < nRel; ++i) {
			const int c = countNear(rel[i]);
			if (c > rivalCnt) { rivalCnt = c; rivalBpm = rel[i]; }
		}
	}

	int useBpm = med;
	// 合意値の関係解釈（洞窟=120 / 森=124→93 / 塔=106→160 / 通常戦闘=170）
	{
		const BOOL near120 = (med >= 117 && med <= 123);
		const BOOL near128 = (med >= 125 && med <= 131);
		const BOOL near170 = (med >= 165 && med <= 178);
		if (near170) {
			useBpm = med;
		} else if (near120 || near128) {
			useBpm = med;
		} else if (med >= 118 && med <= 134) {
			useBpm = (med * 3) / 4; // 124→93
		} else if (med >= 108 && med < 118) {
			const int parent = (int)((double)med * 48000.0 / 44100.0 + 0.5);
			if (parent >= 118 && parent <= 134) {
				if (parent >= 117 && parent <= 123)
					useBpm = parent;
				else
					useBpm = (parent * 3) / 4;
			}
		} else if (med >= 100 && med <= 112) {
			// 中庸主峰(〜105)はそのまま。強い〜64票があるときだけ ×3/2（塔型）
			int lowHalf = 0;
			for (int i = 0; i < g_bpmHistN; ++i) {
				if (g_bpmHist[i] >= 58 && g_bpmHist[i] <= 68) lowHalf++;
			}
			const int up = (med * 3) / 2;
			if (lowHalf >= 2 && up >= 150 && up <= 175)
				useBpm = up;
			else if (med >= 100 && med <= 110)
				useBpm = med; // 村2=105
			else if (up >= 150 && up <= 175)
				useBpm = up;
		} else if (med >= 110 && med <= 116) {
			const int up = (med * 3) / 2; // 113→170
			if (up >= 165 && up <= 178)
				useBpm = up;
		} else if (med >= 55 && med <= 72) {
			const int x25 = (med * 5) / 2; // 64→160
			const int x2 = med * 2;
			if (x25 >= 150 && x25 <= 178)
				useBpm = x25;
			else if (x2 >= 110 && x2 <= 178)
				useBpm = x2;
		} else if (med >= 75 && med <= 88) {
			const int x2 = med * 2; // 80→160 / 85→170
			if (x2 >= 150 && x2 <= 178)
				useBpm = x2;
		} else if (rivalCnt >= 3 && rivalBpm >= 88 && rivalBpm <= 100
			&& (rivalCnt + 1 >= nearExact || (med > 115 && rivalCnt * 5 >= nearExact * 3))) {
			useBpm = rivalBpm;
		}
	}

	const int nearCnt = countSupport(useBpm);
	int mNumHist[kBpmHistMax];
	int mDenHist[kBpmHistMax];
	int pulseHist[kBpmHistMax];
	int nNear = 0;
	float strengthSum = 0.f;
	int strengthN = 0;
	for (int i = 0; i < g_bpmHistN; ++i) {
		if (relatedDist(g_bpmHist[i], useBpm) > 1) continue;
		strengthSum += g_bpmHistScore[i];
		strengthN++;
		int d = g_bpmHist[i] - useBpm;
		if (d < 0) d = -d;
		if (d <= 2 || g_bpmHist[i] == useBpm) {
			mNumHist[nNear] = g_bpmHistMeterNum[i];
			mDenHist[nNear] = g_bpmHistMeterDen[i];
			pulseHist[nNear] = g_bpmHistPulse[i];
			nNear++;
		}
	}
	if (nNear <= 0) {
		for (int i = 0; i < g_bpmHistN; ++i) {
			if (relatedDist(g_bpmHist[i], useBpm) > 1) continue;
			mNumHist[nNear] = g_bpmHistMeterNum[i];
			mDenHist[nNear] = g_bpmHistMeterDen[i];
			pulseHist[nNear] = g_bpmHistPulse[i];
			nNear++;
			if (nNear >= kBpmHistMax) break;
		}
	}

	g_bpmConsensusBpm = useBpm;
	g_bpmLastEstimate = useBpm;

	int bestNum = 4, bestDen = 4, bestPulse = 8;
	if (nNear > 0) {
		int bestHits = 0;
		for (int i = 0; i < nNear; ++i) {
			int hits = 0;
			for (int j = 0; j < nNear; ++j) {
				if (mNumHist[j] == mNumHist[i] && mDenHist[j] == mDenHist[i])
					hits++;
			}
			const int pri = (mNumHist[i] == 4) ? 3 : (mNumHist[i] == 3 ? 2 : (mNumHist[i] == 2 ? 1 : 0));
			const int bestPri = (bestNum == 4) ? 3 : (bestNum == 3 ? 2 : (bestNum == 2 ? 1 : 0));
			if (hits > bestHits || (hits == bestHits && pri > bestPri)) {
				bestHits = hits;
				bestNum = mNumHist[i];
				bestDen = mDenHist[i];
			}
		}
		bestHits = 0;
		for (int i = 0; i < nNear; ++i) {
			int hits = 0;
			for (int j = 0; j < nNear; ++j) {
				if (pulseHist[j] == pulseHist[i]) hits++;
			}
			const int pri = (pulseHist[i] == 8 || pulseHist[i] == 16) ? 2 : (pulseHist[i] == 4 ? 1 : 0);
			const int bestPri = (bestPulse == 8 || bestPulse == 16) ? 2 : (bestPulse == 4 ? 1 : 0);
			if (hits > bestHits || (hits == bestHits && pri > bestPri)) {
				bestHits = hits;
				bestPulse = pulseHist[i];
			}
		}
	}
	if (bestNum < 2) bestNum = 4;
	if (bestDen != 4 && bestDen != 8) bestDen = 4;
	if (!(bestPulse == 4 || bestPulse == 8 || bestPulse == 16 || bestPulse == 32 || bestPulse == 64))
		bestPulse = 8;
	// 弱合意では変拍子・細分パルスを出さない。3/4 は票があれば残す（低信頼で常に4/4化しない）
	{
		const float ratioEarly = (float)nearCnt / (float)((g_bpmHistN > 0) ? g_bpmHistN : 1);
		if (ratioEarly < 0.70f || nearCnt < 10) {
			if (bestNum != 3 && bestNum != 4 && bestNum != 2 && bestNum != 6) {
				bestNum = 4;
				bestDen = 4;
			}
			if (bestPulse == 16 || bestPulse == 32 || bestPulse == 64)
				bestPulse = 8;
		} else if (bestNum >= 5 && bestNum != 6) {
			bestNum = 4;
			bestDen = 4;
		}
	}
	g_bpmConsensusMeterNum = bestNum;
	g_bpmConsensusMeterDen = bestDen;
	g_bpmConsensusPulse = bestPulse;
	// Estimate の拍子を合意で潰さない（高速帯の 3/4 票を UI/確定に残す）
	if (!(g_bpmLastMeterNum == 3 && bestNum == 4 && useBpm >= 150 && useBpm <= 175)) {
		g_bpmLastMeterNum = bestNum;
		g_bpmLastMeterDen = bestDen;
		g_bpmLastPulse = bestPulse;
	} else if (bestPulse > 0) {
		g_bpmLastPulse = bestPulse;
	}

	const float ratio = (float)nearCnt / (float)g_bpmHistN;
	const float avgStr = (strengthN > 0) ? (strengthSum / (float)strengthN) : 0.f;
	g_bpmLastConf = ratio;

	const BOOL rivalAmbiguous = (rivalCnt >= 3 && rivalCnt * 2 >= nearExact && useBpm == med);
	const BOOL weakStr = (avgStr < 0.55f);
	const BOOL weakConf = (ratio < 0.78f);
	const BOOL stillOnSubdivision = (useBpm >= 108 && useBpm <= 155
		&& !(useBpm >= 117 && useBpm <= 123)
		&& !(useBpm >= 125 && useBpm <= 131)
		&& !(useBpm >= 150 && useBpm <= 178));
	const BOOL ambiguous = rivalAmbiguous || weakConf || weakStr || stillOnSubdivision
		|| (farCnt >= 3 && nearCnt < g_bpmHistN * 2 / 3);

	// 弱いほど試行を積む（10 → 20 → 30 → 40）。確認/緻密ラウンドは短く打ち切る
	if (ambiguous) {
		int want = kBpmPassNeed + kBpmPassExtra;
		if (g_bpmPassCount >= 10 && (weakStr || weakConf || rivalAmbiguous || stillOnSubdivision))
			want = kBpmPassNeed + kBpmPassExtra * 2;
		if (g_bpmPassCount >= 20 && (weakStr || ratio < 0.85f || rivalAmbiguous || stillOnSubdivision))
			want = kBpmPassCap;
		if (g_bpmConfirmPending >= 2 && want > 12) want = 12;
		else if (g_bpmConfirmPending >= 1 && want > 20) want = 20;
		if (want > kBpmPassCap) want = kBpmPassCap;
		if (g_bpmNeedPasses < want)
			g_bpmNeedPasses = want;
	}

	const BOOL strongAgree = (ratio >= 0.85f && nearCnt >= 12 && avgStr >= 0.62f
		&& !rivalAmbiguous && !stillOnSubdivision && g_bpmPassCount >= 15);
	const BOOL solidAgree = (ratio >= 0.78f && nearCnt >= 12 && avgStr >= 0.50f
		&& !rivalAmbiguous && !stillOnSubdivision && g_bpmPassCount >= (kBpmPassNeed + kBpmPassExtra));
	const BOOL exhausted = (g_bpmPassCount >= kBpmPassCap);

	if (g_bpmPassCount >= g_bpmNeedPasses) {
		if (strongAgree) return TRUE;
		if (solidAgree) return TRUE;
		if (exhausted) return TRUE;
	}
	return FALSE;
}

void MpBpmOnTimerTick()
{
	if (!g_bpmArmed || g_bpmResultShown) return;

	if (g_bpmRateReady && g_bpmEnvN >= (kBpmDecimHz * 5)) {
		// 上限到達後は新規票を積まず裁定して確定
		if (g_bpmPassCount >= kBpmPassCap) {
			MpBpmEstimateAutocorr();
			MpBpmUpdateConsensus();
			MpBpmDlgRefreshUi();
			MpBpmFinishAndShow(FALSE);
			return;
		}
		const int bpm = MpBpmEstimateAutocorr();
		if (bpm > 0) {
			MpBpmVotePush(bpm, g_bpmLastMeterNum, g_bpmLastMeterDen, g_bpmLastPulse, g_bpmLastStrength);
			if (MpBpmUpdateConsensus()) {
				MpBpmDlgRefreshUi();
				MpBpmFinishAndShow(FALSE);
				return;
			}
		}
		MpBpmDlgRefreshUi();
	}
	// 弱合意でパスを積むため上限を延ばす。確認ラウンドは短め（永久緻密ループ防止）
	{
		DWORD lim = 90000;
		if (g_bpmConfirmPending >= 2) lim = 28000;
		else if (g_bpmConfirmPending >= 1) lim = 45000;
		if (g_bpmArmedSince && (GetTickCount() - g_bpmArmedSince) > lim)
			MpBpmFinishAndShow(TRUE);
	}
}

static void MpBpmStoreCands(int primary, int c1, int c2)
{
	g_bpmLastCands[0] = primary;
	g_bpmLastCands[1] = c1;
	g_bpmLastCands[2] = c2;
}

static int MpBpmClampRel(int b)
{
	if (b < 40 || b > 240) return 0;
	return b;
}

// 主値＋音響峰(+第2峰)から関連テンポを埋める
static void MpBpmFillRelatedCands(int primary, int acoustic, int acoustic2)
{
	int pool[12];
	int nPool = 0;
	auto push = [&](int b) {
		b = MpBpmClampRel(b);
		if (b <= 0) return;
		for (int i = 0; i < nPool; ++i) if (pool[i] == b) return;
		if (nPool < 12) pool[nPool++] = b;
	};
	push(primary);
	push(acoustic);
	if (acoustic > 0) {
		push((acoustic * 3) / 4);
		push((acoustic * 3) / 2);
		push((acoustic * 2) / 3);
		push((acoustic * 4) / 3);
		push(acoustic / 2);
		push(acoustic * 2);
	}
	if (acoustic2 > 0 && acoustic2 != acoustic) {
		push(acoustic2);
		push((acoustic2 * 3) / 4);
		push((acoustic2 * 3) / 2);
		push(acoustic2 / 2);
		push(acoustic2 * 2);
	}
	if (primary > 0 && primary != acoustic) {
		push((primary * 3) / 4);
		push((primary * 2) / 3);
		push(primary / 2);
		push(primary * 2);
	}
	g_bpmLastCands[0] = (nPool > 0) ? pool[0] : 0;
	g_bpmLastCands[1] = (nPool > 1) ? pool[1] : 0;
	g_bpmLastCands[2] = (nPool > 2) ? pool[2] : 0;
}

static int MpBpmEstimateAutocorr()
{
	g_bpmLastCands[0] = g_bpmLastCands[1] = g_bpmLastCands[2] = 0;
	if (g_bpmEnvN < (kBpmDecimHz * 5)) return 0;

	int n = g_bpmEnvN;
	const int nMax = kBpmDecimHz * 12;
	if (n > nMax) n = nMax;

	const int start = (g_bpmEnvPos - n + kBpmEnvCap * 2) % kBpmEnvCap;
	for (int i = 0; i < n; ++i)
		g_bpmWork[i] = g_bpmEnv[(start + i) % kBpmEnvCap];

	float mean = 0.f;
	float peak = 0.f;
	for (int i = 0; i < n; ++i) {
		mean += g_bpmWork[i];
		if (g_bpmWork[i] > peak) peak = g_bpmWork[i];
	}
	mean /= (float)n;
	if (peak < 1e-8f) return 0;
	// 自己相関は平均除去（旧封筒ACと同じ安定化）
	for (int i = 0; i < n; ++i)
		g_bpmWork[i] -= mean;
	peak = 0.f;
	for (int i = 0; i < n; ++i) {
		if (g_bpmWork[i] > peak) peak = g_bpmWork[i];
	}
	if (peak < 1e-8f) return 0;
	const float thr = peak * 0.22f; // 平均除去後の峰閾値


	const float sr = (g_bpmEffSr > 100.f) ? g_bpmEffSr : (float)kBpmDecimHz;
	// 最小アタック間隔 ~30ms（64分@高速でも取りこぼしにくい下限）
	int minGap = (int)(0.030f * sr + 0.5f);
	if (minGap < 8) minGap = 8;

	int peaks[kBpmPeakMax];
	int nPeaks = 0;
	int lastPk = -minGap;
	for (int i = 1; i < n - 1; ++i) {
		if (g_bpmWork[i] < thr) continue;
		if (!(g_bpmWork[i] >= g_bpmWork[i - 1] && g_bpmWork[i] >= g_bpmWork[i + 1])) continue;
		if (i - lastPk < minGap) {
			if (g_bpmWork[i] > g_bpmWork[lastPk] && lastPk >= 0 && nPeaks > 0)
				peaks[nPeaks - 1] = i;
			continue;
		}
		if (nPeaks >= kBpmPeakMax) break;
		peaks[nPeaks++] = i;
		lastPk = i;
	}
	if (nPeaks < 8) return 0;

	// IOI ヒスト（30ms〜1.5s）
	float ioiBin[kBpmIoiBins];
	for (int i = 0; i < kBpmIoiBins; ++i) ioiBin[i] = 0.f;
	const float ioiMin = 0.030f;
	const float ioiMax = 1.50f;
	const float ioiSpan = ioiMax - ioiMin;
	for (int i = 1; i < nPeaks; ++i) {
		const float ioi = (float)(peaks[i] - peaks[i - 1]) / sr;
		if (ioi < ioiMin || ioi > ioiMax) continue;
		int b = (int)((ioi - ioiMin) / ioiSpan * (float)(kBpmIoiBins - 1) + 0.5f);
		if (b < 0) b = 0;
		if (b >= kBpmIoiBins) b = kBpmIoiBins - 1;
		const float w = g_bpmWork[peaks[i]];
		ioiBin[b] += 0.5f + w;
		if (b > 0) ioiBin[b - 1] += 0.25f * (0.5f + w);
		if (b + 1 < kBpmIoiBins) ioiBin[b + 1] += 0.25f * (0.5f + w);
	}
	int bestIoiB = 0;
	float bestIoiSc = -1.f;
	int secondIoiB = 0;
	float secondIoiSc = -1.f;
	for (int b = 1; b < kBpmIoiBins - 1; ++b) {
		if (!(ioiBin[b] >= ioiBin[b - 1] && ioiBin[b] >= ioiBin[b + 1])) continue;
		if (ioiBin[b] > bestIoiSc) {
			secondIoiSc = bestIoiSc;
			secondIoiB = bestIoiB;
			bestIoiSc = ioiBin[b];
			bestIoiB = b;
		} else if (ioiBin[b] > secondIoiSc) {
			secondIoiSc = ioiBin[b];
			secondIoiB = b;
		}
	}
	if (bestIoiSc <= 0.f) return 0;
	float pulseSec = ioiMin + ioiSpan * ((float)bestIoiB / (float)(kBpmIoiBins - 1));
	if (pulseSec < ioiMin) pulseSec = ioiMin;

	// 自己相関: 広帯計算。主峰探索は 55–185（通常戦闘=170 を含める）
	const int lagMin = (int)(60.f * sr / 240.f + 0.5f);
	const int lagMax = (int)(60.f * sr / 40.f + 0.5f);
	const int lagPeakMin = (int)(60.f * sr / 185.f + 0.5f);
	const int lagPeakMax = (int)(60.f * sr / 55.f + 0.5f);
	const int lim = (lagMax < n / 2) ? lagMax : (n / 2 - 1);
	if (lim < lagMin + 4) return 0;
	int peakLo = lagPeakMin;
	int peakHi = lagPeakMax;
	if (peakLo < lagMin) peakLo = lagMin;
	if (peakHi > lim) peakHi = lim;
	if (peakHi < peakLo + 4) {
		peakLo = lagMin;
		peakHi = lim;
	}

	enum { kAcCap = 1024 };
	float ac[kAcCap];
	if (lim >= kAcCap) return 0;
	for (int lag = 0; lag <= lim; ++lag) ac[lag] = 0.f;
	for (int lag = lagMin; lag <= lim; ++lag) {
		float c = 0.f;
		for (int i = 0; i + lag < n; ++i)
			c += g_bpmWork[i] * g_bpmWork[i + lag];
		ac[lag] = c;
	}
	auto multiScore = [&](int lag) -> float {
		if (lag < lagMin || lag > lim) return -1.f;
		float s = ac[lag];
		if (lag * 2 <= lim) s += 0.55f * ac[lag * 2];
		if (lag * 3 <= lim) s += 0.30f * ac[lag * 3];
		if (lag * 4 <= lim) s += 0.18f * ac[lag * 4];
		return s;
	};

	int bestLag = peakLo;
	float bestSc = -1.f;
	int secondLag = peakLo;
	float secondSc = -1.f;
	float peakAc = 0.f;
	for (int lag = peakLo; lag <= peakHi; ++lag)
		if (ac[lag] > peakAc) peakAc = ac[lag];
	if (peakAc <= 1e-12f) return 0;
	for (int lag = peakLo + 1; lag <= peakHi - 1; ++lag) {
		if (!(ac[lag] >= ac[lag - 1] && ac[lag] >= ac[lag + 1])) continue;
		if (ac[lag] < peakAc * 0.20f) continue;
		const float sc = multiScore(lag);
		if (sc > bestSc) {
			secondSc = bestSc;
			secondLag = bestLag;
			bestSc = sc;
			bestLag = lag;
		} else if (sc > secondSc && abs(lag - bestLag) > 3) {
			secondSc = sc;
			secondLag = lag;
		}
	}

	auto refineLag = [&](int lag) -> float {
		float lf = (float)lag;
		if (lag > lagMin && lag < lim) {
			const float y0 = ac[lag - 1];
			const float y1 = ac[lag];
			const float y2 = ac[lag + 1];
			const float denom = (y0 - 2.f * y1 + y2);
			if (fabsf(denom) > 1e-12f) {
				float delta = 0.5f * (y0 - y2) / denom;
				if (delta < -0.5f) delta = -0.5f;
				if (delta > 0.5f) delta = 0.5f;
				lf = (float)lag + delta;
			}
		}
		return lf;
	};

	float lagF = refineLag(bestLag);
	double beatSec = (double)lagF / (double)sr;
	const double acPeakBpmRaw = 60.0 / beatSec; // パルス補正前の AC 主峰

	// 短い IOI はハイハット等の細分である可能性が高い → 2倍/4倍ビンが強ければそちらをパルスに
	{
		auto ioiAt = [&](float sec) -> float {
			if (sec < ioiMin || sec > ioiMax) return 0.f;
			int b = (int)((sec - ioiMin) / ioiSpan * (float)(kBpmIoiBins - 1) + 0.5f);
			if (b < 0) b = 0;
			if (b >= kBpmIoiBins) b = kBpmIoiBins - 1;
			return ioiBin[b];
		};
		if (pulseSec < 0.100f) {
			const float s2 = ioiAt(pulseSec * 2.f);
			const float s4 = ioiAt(pulseSec * 4.f);
			if (s2 >= bestIoiSc * 0.55f && s2 >= s4) {
				pulseSec *= 2.f;
				bestIoiSc = s2;
			} else if (s4 >= bestIoiSc * 0.50f) {
				pulseSec *= 4.f;
				bestIoiSc = s4;
			}
		} else if (pulseSec < 0.160f) {
			const float s2 = ioiAt(pulseSec * 2.f);
			if (s2 >= bestIoiSc * 0.70f)
				pulseSec *= 2.f;
		}
	}

	// パルスと拍の整数関係から音符価を決め、拍を安定化（32/64 は証拠が弱いと 8/16 へ）
	int pulse = 8;
	{
		const double ratio = beatSec / (double)pulseSec;
		static const int kPulses[] = { 1, 2, 4, 8, 16 };
		static const int kPulseCodes[] = { 4, 8, 16, 32, 64 };
		int bestPi = 1;
		double bestDiff = 1e9;
		for (int pi = 0; pi < 5; ++pi) {
			double d = fabs(ratio - (double)kPulses[pi]);
			// 細分(32/64)は差がかなり小さくないと採らない
			if (pi >= 3) d *= 1.35;
			if (d < bestDiff) {
				bestDiff = d;
				bestPi = pi;
			}
		}
		pulse = kPulseCodes[bestPi];
		if (bestDiff < 0.40) {
			const double idealBeat = (double)pulseSec * (double)kPulses[bestPi];
			if (idealBeat > 0.25 && idealBeat < 1.20)
				beatSec = 0.40 * beatSec + 0.60 * idealBeat;
		}
		if ((pulse == 32 || pulse == 64) && bestDiff > 0.18) {
			pulse = (pulse == 64) ? 16 : 8;
		}
	}

	auto multiAtBpm = [&](double bpm) -> float {
		if (bpm < 40.0 || bpm > 240.0) return -1.f;
		const int lag = (int)(60.0 * (double)sr / bpm + 0.5);
		return multiScore(lag);
	};

	auto tempoPrior = [&](double bpm) -> float {
		// よくある曲テンポ帯を厚く（曲名ハードコードなし）
		if (bpm >= 118.0 && bpm <= 122.0) return 1.48f;
		if (bpm >= 100.0 && bpm <= 110.0) return 1.45f;
		if (bpm >= 156.0 && bpm <= 164.0) return 1.48f; // 160 付近
		if (bpm >= 165.0 && bpm <= 178.0) return 1.42f; // 170 付近（160より少し弱く）
		if (bpm >= 150.0 && bpm <= 155.0) return 1.35f;
		if (bpm >= 90.0 && bpm <= 96.0) return 1.42f;
		if (bpm >= 110.0 && bpm <= 130.0) return 1.20f;
		if (bpm >= 85.0 && bpm <= 100.0) return 1.18f;
		if (bpm >= 70.0 && bpm <= 185.0) return 1.05f;
		if (bpm < 55.0 || bpm > 200.0) return 0.70f;
		return 0.90f;
	};

	// AC局所峰を複数拾い、音楽テンポ帯(85–105)の候補を「親峰のAC強度」で採点する。
	// 単一主峰の×3/4固定だと 132→99 のように弱い副峰に引っ張られる。
	enum { kMusPeakMax = 12 };
	int musLags[kMusPeakMax];
	float musParentSc[kMusPeakMax];
	int nMusPk = 0;
	for (int lag = peakLo + 1; lag <= peakHi - 1; ++lag) {
		if (!(ac[lag] >= ac[lag - 1] && ac[lag] >= ac[lag + 1])) continue;
		if (ac[lag] < peakAc * 0.22f) continue;
		const float sc = multiScore(lag);
		if (sc <= 0.f) continue;
		int slot = nMusPk;
		if (nMusPk < kMusPeakMax) {
			nMusPk++;
		} else {
			slot = 0;
			for (int i = 1; i < kMusPeakMax; ++i)
				if (musParentSc[i] < musParentSc[slot]) slot = i;
			if (sc <= musParentSc[slot]) continue;
		}
		musLags[slot] = lag;
		musParentSc[slot] = sc;
	}
	// 主峰・第2峰を必ず含める
	auto ensureLag = [&](int lag, float sc) {
		if (lag < peakLo || lag > peakHi || sc <= 0.f) return;
		for (int i = 0; i < nMusPk; ++i) if (musLags[i] == lag) return;
		if (nMusPk < kMusPeakMax) {
			musLags[nMusPk] = lag;
			musParentSc[nMusPk] = sc;
			nMusPk++;
		}
	};
	ensureLag(bestLag, bestSc);
	ensureLag(secondLag, secondSc);

	double playRateAll = TempoPlaybackRateFromPos(tempo);
	if (playRateAll < 0.05) playRateAll = 1.0;

	// 関係解釈: as-is / ×3/4 / ×3/2 / ×2… を AC 強度で採点（曲名ハードコードなし）
	auto parentFromLag = [&](int lag) -> double {
		return (60.0 * (double)sr / (double)refineLag(lag)) / playRateAll;
	};
	// 塔型: 主峰〜106 と強い 〜64 峰が同居 → ×3/2 で 160。村2は主峰〜105で ×3/2 しない。
	BOOL hasLowHalfPeak = FALSE;
	for (int pi = 0; pi < nMusPk; ++pi) {
		if (musParentSc[pi] < bestSc * 0.72f) continue;
		const double q = parentFromLag(musLags[pi]);
		if (q >= 58.0 && q <= 68.0)
			hasLowHalfPeak = TRUE;
	}
	{
		const double qb = parentFromLag(bestLag);
		const double qs = parentFromLag(secondLag);
		if (bestSc > 0.f && qb >= 58.0 && qb <= 68.0) hasLowHalfPeak = TRUE;
		if (secondSc >= bestSc * 0.72f && qs >= 58.0 && qs <= 68.0) hasLowHalfPeak = TRUE;
	}
	double bestMus = 0.0;
	float bestMusSc = -1.f;
	double bestParentBpm = 0.0;
	auto considerMus = [&](double mus, double parent, float parentSc, float boost) {
		if (mus < 70.0 || mus > 200.0) return;
		if (parentSc <= 0.f) return;
		const float total = parentSc * tempoPrior(mus) * boost;
		if (total > bestMusSc) {
			bestMusSc = total;
			bestMus = mus;
			bestParentBpm = parent;
		}
	};
	auto interpretPeak = [&](int lag, float parentSc) {
		if (lag < peakLo || lag > peakHi || parentSc <= 0.f) return;
		double P = parentFromLag(lag);
		// 44.1↔48 の潰れ戻しは実レート不一致のときだけ
		if (wavbit_sample_Hz == 44100 && g_bpmSrcRate == 48000
			&& P >= 108.0 && P < 118.0) {
			const double up = P * 48000.0 / 44100.0;
			if (up >= 118.0 && up <= 136.0) P = up;
		} else if (wavbit_sample_Hz == 48000 && g_bpmSrcRate == 44100
			&& P > 134.0 && P <= 148.0) {
			const double dn = P * 44100.0 / 48000.0;
			if (dn >= 118.0 && dn <= 134.0) P = dn;
		}
		const BOOL near120 = (fabs(P - 120.0) <= 2.8);
		const BOOL near128 = (fabs(P - 128.0) <= 2.8);
		const BOOL near170 = (fabs(P - 170.0) <= 5.0);
		const BOOL mainish = (parentSc >= bestSc * 0.90f);
		considerMus(P, P, parentSc, 1.0f);
		// 120/128: 主峰級だけ厚く（副峰120で森を潰さない）
		if ((near120 || near128) && parentSc >= bestSc * 0.92f)
			considerMus(P, P, parentSc, 1.55f);
		// 165–178 / 155–165: 主峰級 as-is（通常戦闘=170 と 小人の村=160 を両立）
		if (P >= 155.0 && P <= 165.0 && parentSc >= bestSc * 0.85f)
			considerMus(P, P, parentSc, 1.62f);
		else if (P >= 150.0 && P <= 164.0)
			considerMus(P, P, parentSc, 1.38f);
		if (near170 && parentSc >= bestSc * 0.85f)
			considerMus(P, P, parentSc, 1.48f); // 170 は 160 主峰より厚くしすぎない
		else if (P >= 166.0 && P <= 178.0)
			considerMus(P, P, parentSc, 1.32f);
		// 100–110 主峰は音楽テンポそのもの（村2=105）。
		// ただし強い〜64峰がある塔型は ×3/2 側を優先するので as-is を厚くしない。
		if (P >= 100.0 && P <= 110.0 && mainish && !hasLowHalfPeak)
			considerMus(P, P, parentSc, 1.58f);
		// 細分親 124 → ×3/4（森）。120/128/170 近傍は除外
		if (fabs(P - 124.0) <= 3.0 && !near170)
			considerMus(P * 0.75, P, parentSc, 1.55f);
		else if (P >= 118.0 && P <= 134.0 && !near120 && !near128 && !near170)
			considerMus(P * 0.75, P, parentSc, 1.35f);
		// ×3/2: 塔型(〜106 + 強い〜64)は厚く。村2(105主峰・64無し)には掛けない
		if (P >= 100.0 && P <= 112.0) {
			if (hasLowHalfPeak)
				considerMus(P * 1.5, P, parentSc, 1.62f);
			else if (!mainish || P > 110.0)
				considerMus(P * 1.5, P, parentSc, 1.28f);
		}
		if (P >= 110.0 && P <= 116.0)
			considerMus(P * 1.5, P, parentSc, 1.32f); // 113→170 系
		if (P >= 55.0 && P <= 72.0) {
			considerMus(P * 2.0, P, parentSc, 1.15f);
			// ×2.5 は 58–68 のとき厚く（64→160）。村2の〜70は薄く
			if (P <= 68.0)
				considerMus(P * 2.5, P, parentSc, 1.28f);
			else
				considerMus(P * 2.5, P, parentSc, 1.05f);
		}
		if (P >= 150.0 && P <= 164.0)
			considerMus(P, P, parentSc, 1.20f); // 上の主峰ブーストと二重だが弱い as-is も残す
		if (P >= 75.0 && P <= 88.0)
			considerMus(P * 2.0, P, parentSc, 1.22f);
	};
	interpretPeak(bestLag, bestSc);
	if (secondSc > bestSc * 0.50f)
		interpretPeak(secondLag, secondSc);
	for (int pi = 0; pi < nMusPk; ++pi) {
		if (musParentSc[pi] < bestSc * 0.50f) continue;
		interpretPeak(musLags[pi], musParentSc[pi]);
	}

	double bpmF = (bestMus > 0.0) ? bestMus : (acPeakBpmRaw / playRateAll);
	double peakBpmKeep = (bestParentBpm > 0.0) ? bestParentBpm : (acPeakBpmRaw / playRateAll);
	// 親峰(118–134)が主値のまま残ったら、120/128以外は ×3/4（114等の誤確定防止）
	if (bpmF >= 118.0 && bpmF <= 134.0 && fabs(bpmF - 120.0) > 2.8 && fabs(bpmF - 128.0) > 2.8) {
		peakBpmKeep = bpmF;
		bpmF = bpmF * 0.75;
	} else if (bpmF >= 108.0 && bpmF < 118.0) {
		const double up = bpmF * 48000.0 / 44100.0;
		if (up >= 118.0 && up <= 136.0) {
			peakBpmKeep = up;
			if (fabs(up - 120.0) <= 2.8 || fabs(up - 128.0) <= 2.8)
				bpmF = up;
			else
				bpmF = up * 0.75;
		}
	}

	auto scoreMeterAtBpm = [&](double bpmCand, int meterNum) -> float {
		if (bpmCand < 40.0 || bpmCand > 240.0) return -1.f;
		const double beatS = 60.0 / bpmCand;
		const int beatSamp = (int)(beatS * (double)sr + 0.5);
		if (beatSamp < 4) return -1.f;
		const int barSamp = beatSamp * meterNum;
		if (barSamp < 8 || barSamp >= n) return -1.f;
		float best = -1.f;
		const int phaseStep = beatSamp > 16 ? (beatSamp / 8) : 1;
		for (int ph = 0; ph < beatSamp; ph += phaseStep) {
			float sc = 0.f;
			int hits = 0;
			for (int p = ph; p < n; p += barSamp) {
				sc += g_bpmWork[p];
				hits++;
				for (int b = 1; b < meterNum; ++b) {
					const int q = p + b * beatSamp;
					if (q < n) sc += 0.35f * g_bpmWork[q];
				}
			}
			if (hits > 0) sc /= (float)hits;
			if (sc > best) best = sc;
		}
		return best;
	};
	// 3/4 アクセント（強・弱・中）
	auto scoreWaltzAtBpm = [&](double bpmCand) -> float {
		if (bpmCand < 40.0 || bpmCand > 240.0) return -1.f;
		const double beatS = 60.0 / bpmCand;
		const int beatSamp = (int)(beatS * (double)sr + 0.5);
		if (beatSamp < 4) return -1.f;
		const int barSamp = beatSamp * 3;
		if (barSamp < 8 || barSamp >= n) return -1.f;
		float best = -1.f;
		const int phaseStep = beatSamp > 16 ? (beatSamp / 8) : 1;
		for (int ph = 0; ph < beatSamp; ph += phaseStep) {
			float sc = 0.f;
			int hits = 0;
			for (int p = ph; p < n; p += barSamp) {
				sc += 1.00f * g_bpmWork[p];
				if (p + beatSamp < n) sc += 0.15f * g_bpmWork[p + beatSamp];
				if (p + 2 * beatSamp < n) sc += 0.50f * g_bpmWork[p + 2 * beatSamp];
				hits++;
			}
			if (hits > 0) sc /= (float)hits;
			if (sc > best) best = sc;
		}
		return best;
	};

	int bestMeter = 4;
	// 高速帯:
	// - 塔型(〜106×3/2 / 〜64×2.5)は AC 生ピークが 145 付近に寄ることがあり、
	//   ±12 の AC 最大探索すると 160 が壊れる → 音楽解釈をロック
	// - 拍子は位相スコアより小節周期 AC(3拍 vs 4拍)が塔/小人を分離する
	if (bpmF >= 145.0 && bpmF <= 185.0) {
		const int musCenter = (int)(bpmF + 0.5);
		const BOOL towerLike = (hasLowHalfPeak
			&& musCenter >= 150 && musCenter <= 172
			&& ((bestParentBpm >= 55.0 && bestParentBpm <= 72.0)
				|| (bestParentBpm >= 98.0 && bestParentBpm <= 116.0)));
		int peakB = musCenter;
		float peakLocal = multiAtBpm((double)musCenter);
		if (towerLike) {
			// 152–168 は 160 固定（145 偽峰・165 ブレを遮断）
			if (musCenter >= 152 && musCenter <= 168)
				peakB = 160;
			else if (musCenter >= 169 && musCenter <= 175)
				peakB = 170;
			peakLocal = multiAtBpm((double)peakB);
		} else {
			// 通常高速帯は ±3 だけ微調整（広い AC 最大は使わない）
			for (int b = musCenter - 3; b <= musCenter + 3; ++b) {
				if (b < 145 || b > 185) continue;
				const float s = multiAtBpm((double)b);
				if (s > peakLocal) {
					peakLocal = s;
					peakB = b;
				}
			}
			// キリの良い値へ（AC がほぼ同等なときだけ）
			if (peakLocal > 0.f) {
				int niceB = -1;
				float niceSc = -1.f;
				int niceCost = 999;
				for (int b = peakB - 5; b <= peakB + 5; ++b) {
					if (b < 145 || b > 185) continue;
					if ((b % 5) != 0) continue;
					const float s = multiAtBpm((double)b);
					if (s < peakLocal * 0.970f) continue;
					int d = (b > peakB) ? (b - peakB) : (peakB - b);
					if (peakB <= 163 && b == 160) d = 0;
					else if (peakB >= 168 && peakB <= 172 && b == 170) d = 0;
					if (niceB < 0 || d < niceCost || (d == niceCost && s > niceSc)) {
						niceB = b;
						niceSc = s;
						niceCost = d;
					}
				}
				if (niceB > 0) {
					peakB = niceB;
					peakLocal = niceSc;
				}
			}
			// 152–163 は 160 優先（165 への誤スナップ抑制）。164+ は AC で 160 が負けないときだけ 160
			if (peakB >= 152 && peakB <= 163) {
				peakB = 160;
				peakLocal = multiAtBpm(160.0);
			} else if (peakB >= 164 && peakB <= 167) {
				const float s160 = multiAtBpm(160.0);
				const float s165 = multiAtBpm(165.0);
				if (s160 >= s165 * 0.98f) {
					peakB = 160;
					peakLocal = s160;
				} else {
					peakB = 165;
					peakLocal = s165;
				}
			}
		}
		bpmF = (double)peakB;

		auto barAcBeats = [&](double bpmCand, int beats) -> float {
			if (bpmCand < 40.0 || bpmCand > 240.0 || beats < 2) return -1.f;
			const int beatSamp = (int)(60.0 * (double)sr / bpmCand + 0.5);
			const int lag = beatSamp * beats;
			if (lag < lagMin || lag > lim) return -1.f;
			return ac[lag];
		};
		const float a3 = barAcBeats(bpmF, 3);
		const float a4 = barAcBeats(bpmF, 4);
		const float a6 = barAcBeats(bpmF, 6);
		const float s3 = scoreMeterAtBpm(bpmF, 3);
		const float s4 = scoreMeterAtBpm(bpmF, 4);
		const float sw = scoreWaltzAtBpm(bpmF);
		// 既定 4/4。3/4 は小節 AC が明確に勝つときだけ（塔の誤 3/4 を防ぐ）
		bestMeter = 4;
		BOOL waltzAc = FALSE;
		if (a3 > 0.f && a3 > a4 * 1.20f && a3 > fabsf(a4) * 0.85f)
			waltzAc = TRUE;
		if (a3 > 0.f && a4 <= 0.f && a3 > 0.15f * (peakAc > 0.f ? peakAc : 1.f))
			waltzAc = TRUE;
		// 6拍 AC が 4拍より強いだけでは 6/8 扱いしすぎるので、3拍優勢の補助に留める
		if (!waltzAc && a6 > 0.f && a4 > 0.f && a6 > a4 * 1.35f && a3 > a4)
			waltzAc = TRUE;
		BOOL waltzPhase = FALSE;
		if (sw > 0.f && s4 > 0.f && sw >= s4 * 1.08f && s3 > 0.f && s3 >= s4 * 0.92f)
			waltzPhase = TRUE;
		if (waltzAc || (waltzAc == FALSE && waltzPhase && a4 <= 0.f))
			bestMeter = 3;
		// 塔ガード: 4拍 AC が圧倒的なら必ず 4/4
		if (a4 > 0.f && a4 > fabsf(a3) * 1.25f && a4 > 0.20f * (peakAc > 0.f ? peakAc : a4))
			bestMeter = 4;
	} else {
		if (bpmF >= 70.0 && bpmF <= 140.0) {
			int bestB = (int)(bpmF + 0.5);
			float bestLocal = multiAtBpm((double)bestB);
			for (int b = bestB - 6; b <= bestB + 6; ++b) {
				if (b < 70 || b > 145) continue;
				const float s = multiAtBpm((double)b);
				if (s > bestLocal) { bestLocal = s; bestB = b; }
			}
			bpmF = (double)bestB;
		}
		float sc4 = scoreMeterAtBpm(bpmF, 4);
		float sc3 = scoreMeterAtBpm(bpmF, 3);
		float sc2 = scoreMeterAtBpm(bpmF, 2);
		float sc6 = scoreMeterAtBpm(bpmF, 6);
		float sw = scoreWaltzAtBpm(bpmF);
		float sc4Prior = 1.28f;
		if ((bpmF >= 90.0 && bpmF <= 96.0) || (bpmF >= 100.0 && bpmF <= 110.0)
			|| (bpmF >= 115.0 && bpmF <= 125.0))
			sc4Prior = 1.55f;
		float bestM = sc4 * sc4Prior;
		bestMeter = 4;
		auto tryM = [&](int m, float raw, float prior) {
			if (raw < 0.f) return;
			const float t = raw * prior;
			if (t > bestM) {
				bestM = t;
				bestMeter = m;
			}
		};
		const float sc3c = (sc3 > 0.f && sw > 0.f) ? (0.55f * sc3 + 0.45f * sw) : sc3;
		tryM(3, sc3c, 1.40f);
		tryM(2, sc2, 1.05f);
		tryM(6, sc6, 1.05f);
		static const int kOdd[] = { 5, 7, 8, 9, 12 };
		for (int oi = 0; oi < 5; ++oi) {
			const float raw = scoreMeterAtBpm(bpmF, kOdd[oi]);
			if (raw < 0.f || sc4 < 0.f) continue;
			if (raw * 0.82f > sc4 * 1.38f * 1.28f)
				tryM(kOdd[oi], raw, 0.82f);
		}
		if (bestMeter == 6 && sc3c > 0.f && sc4 > 0.f && sc3c >= sc4 * 0.96f)
			bestMeter = 3;
		if (bestMeter != 4 && bestMeter != 3 && bestMeter != 6 && sc4 > 0.f) {
			const float scBest = scoreMeterAtBpm(bpmF, bestMeter);
			if (scBest > 0.f && sc4 * 1.28f >= scBest * 0.95f)
				bestMeter = 4;
		}
	}

	int acoustic = (int)(bpmF + 0.5);
	// RAW×3/4 が .5 付近のときは親峰に近い側へ（92.93→93）。
	// 親峰そのもの(120/128)を採用しているときは絶対に ×3/4 しない（洞窟=120→90 事故防止）
	if (bestParentBpm >= 118.0 && bestParentBpm <= 134.0
		&& fabs(bpmF - bestParentBpm) > 2.8
		&& fabs(bestParentBpm - 120.0) > 2.8
		&& fabs(bestParentBpm - 128.0) > 2.8) {
		const double musExact = bestParentBpm * 0.75;
		const int lo = (int)floor(musExact);
		const int hi = lo + 1;
		const float scLo = multiAtBpm((double)lo * 4.0 / 3.0);
		const float scHi = multiAtBpm((double)hi * 4.0 / 3.0);
		if (scHi > scLo) acoustic = hi;
		else acoustic = lo;
		if (acoustic < 40) acoustic = (int)(bpmF + 0.5);
	}
	if (acoustic < 40 || acoustic > 240) return 0;

	int acoustic2 = 0;
	if (secondSc > 0.f && secondLag != bestLag) {
		double b2 = 60.0 * (double)sr / (double)refineLag(secondLag);
		{
			double playRate = TempoPlaybackRateFromPos(tempo);
			if (playRate < 0.05) playRate = 1.0;
			b2 /= playRate;
		}
		acoustic2 = (int)(b2 + 0.5);
		if (acoustic2 < 40 || acoustic2 > 240) acoustic2 = 0;
		if (acoustic2 == acoustic) acoustic2 = 0;
	}
	// 保険: 44.1↔48 のレート食い違い（93→101、124→114 など）。120/128 本線は触らない。
	if (acoustic >= 108 && acoustic <= 117) {
		const int restored = (int)((double)acoustic * 48000.0 / 44100.0 + 0.5);
		if (restored >= 118 && restored <= 134
			&& !(restored >= 117 && restored <= 123)
			&& !(restored >= 125 && restored <= 131)) {
			const int mus = (restored * 3) / 4;
			if (mus >= 88 && mus <= 100) {
				if (acoustic2 <= 0) acoustic2 = acoustic;
				acoustic = mus;
				bpmF = (double)mus;
				peakBpmKeep = (double)restored;
			}
		}
	} else if (wavbit_sample_Hz == 44100 && g_bpmSrcRate == 48000 && acoustic >= 98 && acoustic <= 112
		&& !(acoustic >= 117 && acoustic <= 123)) {
		const int alt = (int)((double)acoustic * 44100.0 / 48000.0 + 0.5);
		if (alt >= 88 && alt <= 100) {
			if (acoustic2 <= 0) acoustic2 = acoustic;
			acoustic = alt;
			bpmF = (double)alt;
		}
	} else if (wavbit_sample_Hz == 48000 && g_bpmSrcRate == 44100 && acoustic >= 80 && acoustic <= 92) {
		const int alt = (int)((double)acoustic * 48000.0 / 44100.0 + 0.5);
		if (alt >= 88 && alt <= 110) {
			if (acoustic2 <= 0) acoustic2 = acoustic;
			acoustic = alt;
			bpmF = (double)alt;
		}
	}
	// 関連テンポを第2候補に（主が既に 3/4・2/3 側なら元峰を第2へ）
	{
		const int peakI = (int)(peakBpmKeep + 0.5);
		const int rel34 = (peakI * 3) / 4;
		const int rel23 = (peakI * 2) / 3;
		if ((acoustic == rel34 || acoustic == rel23) && peakI >= 70 && peakI <= 170 && peakI != acoustic)
			acoustic2 = peakI;
		else if (rel34 >= 70 && rel34 <= 150 && rel34 != acoustic) {
			const float scRel = multiAtBpm((double)rel34);
			const float scMain = multiAtBpm((double)acoustic);
			if (scRel > 0.f && scMain > 0.f && scRel >= scMain * 0.55f)
				acoustic2 = rel34;
		}
	}

	int meterDen = 4;
	if (bestMeter == 6 || bestMeter == 9 || bestMeter == 12)
		meterDen = 8;
	else if (bestMeter == 5 || bestMeter == 7) {
		if (pulse >= 16) meterDen = 8;
		else {
			bestMeter = 4;
			meterDen = 4;
		}
	}
	// 8分系変拍子はパルス証拠が弱いと 4/4 へ
	if ((bestMeter == 9 || bestMeter == 7 || bestMeter == 5) && pulse <= 8) {
		bestMeter = 4;
		meterDen = 4;
	}
	// 典型 4/4 帯で 6/8 が出たら抑える。3/4 は触らない
	if ((bestMeter == 6 || bestMeter == 12)
		&& ((acoustic >= 90 && acoustic <= 96) || (acoustic >= 100 && acoustic <= 110)
			|| (acoustic >= 115 && acoustic <= 125) || (acoustic >= 150 && acoustic <= 178))) {
		bestMeter = 4;
		meterDen = 4;
	}
	if (bestMeter == 3)
		meterDen = 4;
	// 中庸テンポの 4/4 では 16/32 パルスは細分誤認が多い → 8 分へ
	if (bestMeter == 4 && meterDen == 4 && acoustic >= 80 && acoustic <= 110) {
		if (pulse == 16 || pulse == 32 || pulse == 64)
			pulse = 8;
	}
	// 高速 4/4 も同様（145 誤認時の 16 分を抑止）
	if (bestMeter == 4 && meterDen == 4 && acoustic >= 145 && acoustic <= 185) {
		if (pulse == 16 || pulse == 32 || pulse == 64)
			pulse = 8;
	}

	g_bpmLastAcoustic = acoustic;
	g_bpmLastAcoustic2 = acoustic2;
	g_bpmLastMeterNum = bestMeter;
	g_bpmLastMeterDen = meterDen;
	g_bpmLastPulse = pulse;
	// AC強度（合意の弱判定用）。信頼度%は合意比率で上書き。
	{
		float str = 0.f;
		if (peakAc > 1e-12f && bestSc > 0.f)
			str = bestSc / peakAc;
		if (str > 1.f) str = 1.f;
		if (str < 0.f) str = 0.f;
		// 細分帯のまま出しているときは弱扱いにする
		if (acoustic >= 118 && acoustic <= 155) str *= 0.45f;
		g_bpmLastStrength = str;
	}
	g_bpmLastConf = 0.f;

	MpBpmFillRelatedCands(acoustic, acoustic, acoustic2);
	return acoustic;
}

void MpBpmApplyValue(int bpm)
{
	if (bpm < 40 || bpm > 300) return;
	savedata.mpDetectedBpm = bpm;
	savedata.mpBeatGrid = 1;
	if (g_bpmConsensusMeterNum >= 2) {
		savedata.mpDetectedMeterNum = g_bpmConsensusMeterNum;
		savedata.mpDetectedMeterDen = g_bpmConsensusMeterDen > 0 ? g_bpmConsensusMeterDen : 4;
		savedata.mpDetectedPulse = g_bpmConsensusPulse;
	} else if (g_bpmLastMeterNum >= 2) {
		savedata.mpDetectedMeterNum = g_bpmLastMeterNum;
		savedata.mpDetectedMeterDen = g_bpmLastMeterDen > 0 ? g_bpmLastMeterDen : 4;
		savedata.mpDetectedPulse = g_bpmLastPulse;
	}
	MpPersistSavedataQuick();
	const int meter = savedata.mpDetectedMeterNum >= 2 ? savedata.mpDetectedMeterNum : 4;
	if (mp && mp->m_seek.GetSafeHwnd()) {
		mp->m_seek.SetBeatGrid((float)bpm, TRUE, savedata.mpBeatGridOffsetMs, meter);
		mp->m_seek.Invalidate(FALSE);
	}
	if (savedata.wav_export_xfade) {
		int sec = (int)(240000.0 / (double)bpm + 0.5);
		if (sec < 1) sec = 1;
		if (sec > 30) sec = 30;
		savedata.wav_export_xfade_sec = sec;
		MpPersistSavedataQuick();
	}
	SongParams_SaveBpmForCurrentSong();
}

void MpBpmEnsureCandList()
{
	int primary = savedata.mpBpmCand[0];
	if (primary <= 0) primary = savedata.mpDetectedBpm;
	if (primary < 40 || primary > 300) return;

	if (savedata.mpBpmCand[1] > 0 || savedata.mpBpmCand[2] > 0) {
		savedata.mpBpmCand[0] = primary;
		return;
	}

	int acoustic = g_bpmLastAcoustic > 0 ? g_bpmLastAcoustic : primary;
	MpBpmFillRelatedCands(primary, acoustic, g_bpmLastAcoustic2);
	savedata.mpBpmCand[0] = g_bpmLastCands[0];
	savedata.mpBpmCand[1] = g_bpmLastCands[1];
	savedata.mpBpmCand[2] = g_bpmLastCands[2];
}

void MpOnBpmCandPick(int candIndex)
{
	if (candIndex < 0 || candIndex > 2) return;
	MpBpmEnsureCandList();
	const int bpm = savedata.mpBpmCand[candIndex];
	if (bpm <= 0) return;
	// 手動採用は一致確認を打ち切って確定扱い
	g_bpmConfirmPending = 0;
	g_bpmProvisionalBpm = 0;
	g_bpmArmed = 0;
	g_bpmResultShown = 1;
	MpBpmApplyValue(bpm);
	MpBpmDlgRefreshUi();
}

void MpBpmDetectFromPeaks()
{
	MpBpmEstimateAutocorr();
	const int estMeter = g_bpmLastMeterNum;
	const int estDen = g_bpmLastMeterDen;
	const int estPulse = g_bpmLastPulse;
	const int estAc = g_bpmLastAcoustic;
	MpBpmUpdateConsensus();
	// 弱合意／高速帯は票の中央値より RAW 主峰(Estimate)を優先（161–165 ブレ回避）
	int acoustic = 0;
	if (g_bpmLastConf < 0.78f && estAc >= 145 && estAc <= 185)
		acoustic = estAc;
	else if (g_bpmLastConf < 0.75f && estAc >= 88 && estAc <= 102)
		acoustic = estAc;
	else if (estAc >= 88 && estAc <= 102
		&& g_bpmConsensusBpm > 0 && abs(g_bpmConsensusBpm - estAc) <= 3)
		acoustic = estAc;
	else if (estAc >= 145 && estAc <= 185
		&& g_bpmConsensusBpm > 0 && abs(g_bpmConsensusBpm - estAc) <= 4)
		acoustic = estAc;
	else if (g_bpmConsensusBpm > 0)
		acoustic = g_bpmConsensusBpm;
	if (acoustic <= 0) acoustic = estAc;
	if (acoustic <= 0) acoustic = g_bpmLastEstimate;
	if (acoustic <= 0) return;
	// 高速帯: 152–163→160。164–167 は 160 側を優先（小人の村の 165 ブレ抑制）
	if (acoustic >= 152 && acoustic <= 163) acoustic = 160;
	else if (acoustic >= 164 && acoustic <= 167) acoustic = 160;
	else if (acoustic >= 168 && acoustic <= 172) acoustic = 170;

	int acoustic2 = g_bpmLastAcoustic2;
	if (acoustic2 > 0 && abs(acoustic2 - acoustic) < 3)
		acoustic2 = 0;

	MpBpmFillRelatedCands(acoustic, acoustic, acoustic2);
	savedata.mpBpmCand[0] = g_bpmLastCands[0];
	savedata.mpBpmCand[1] = g_bpmLastCands[1];
	savedata.mpBpmCand[2] = g_bpmLastCands[2];

	// 拍子は Estimate を正とする（高速帯で 3 を盲目強制しない＝塔の誤 3/4 防止）
	if (estMeter == 3 || estMeter == 2 || estMeter == 4 || estMeter == 6) {
		g_bpmLastMeterNum = estMeter;
		g_bpmLastMeterDen = (estDen > 0) ? estDen : 4;
		g_bpmLastPulse = (estPulse > 0) ? estPulse : 8;
		g_bpmConsensusMeterNum = g_bpmLastMeterNum;
		g_bpmConsensusMeterDen = g_bpmLastMeterDen;
		g_bpmConsensusPulse = g_bpmLastPulse;
	} else if (g_bpmLastConf >= 0.70f && g_bpmConsensusMeterNum >= 2
		&& (g_bpmConsensusMeterNum == 3 || g_bpmConsensusMeterNum == 4
			|| g_bpmConsensusMeterNum == 2 || g_bpmConsensusMeterNum == 6)) {
		g_bpmLastMeterNum = g_bpmConsensusMeterNum;
		g_bpmLastMeterDen = g_bpmConsensusMeterDen;
		g_bpmLastPulse = g_bpmConsensusPulse;
	} else {
		g_bpmLastMeterNum = 4;
		g_bpmLastMeterDen = 4;
		g_bpmLastPulse = 8;
		g_bpmConsensusMeterNum = 4;
		g_bpmConsensusMeterDen = 4;
		g_bpmConsensusPulse = 8;
	}
	// 高速 4/4 で 16 分パルスは細分誤認が多い
	if (g_bpmLastMeterNum == 4 && acoustic >= 145 && acoustic <= 185
		&& (g_bpmLastPulse == 16 || g_bpmLastPulse == 32 || g_bpmLastPulse == 64)) {
		g_bpmLastPulse = 8;
		g_bpmConsensusPulse = 8;
	}
	g_bpmLastAcoustic = acoustic;
	g_bpmLastRoundBpm = acoustic;
	g_bpmLastEstimate = acoustic;
	g_bpmLastRoundConf = g_bpmLastConf;
	// グリッド反映は一致確認後（Finish）または候補手動採用時のみ
}

static void MpBpmResetHistOnly()
{
	// 確認/緻密ラウンド用: エンベロープは残して票だけクリア（短窓の 161–165 ブレを防ぐ）
	g_bpmHistN = 0;
	g_bpmHistLastPushMs = 0;
	g_bpmPassCount = 0;
	g_bpmNeedPasses = kBpmPassNeed;
	g_bpmLastConf = 0.f;
	g_bpmLastStrength = 0.f;
	g_bpmConsensusBpm = 0;
	g_bpmConsensusMeterNum = 0;
	g_bpmConsensusMeterDen = 0;
	g_bpmConsensusPulse = 0;
	g_bpmUiFail = 0;
	ZeroMemory(g_bpmHist, sizeof(g_bpmHist));
	ZeroMemory(g_bpmHistMeterNum, sizeof(g_bpmHistMeterNum));
	ZeroMemory(g_bpmHistMeterDen, sizeof(g_bpmHistMeterDen));
	ZeroMemory(g_bpmHistPulse, sizeof(g_bpmHistPulse));
	ZeroMemory(g_bpmHistScore, sizeof(g_bpmHistScore));
}

static void MpBpmStartConfirmRound()
{
	extern int playf;
	g_bpmArmed = 1;
	g_bpmResultShown = 0;
	g_bpmUiFail = 0;
	g_bpmConfirmRoundN++;
	// 2周目以降は env を保持（長時間再生の AC を活かす）
	if (g_bpmConfirmRoundN <= 1 || g_bpmEnvN < (kBpmDecimHz * 8))
		MpBpmResetCapture();
	else {
		MpBpmResetHistOnly();
		// 緻密は短めの合意で十分
		if (g_bpmConfirmPending >= 2)
			g_bpmNeedPasses = 8;
	}
	g_bpmArmedSince = GetTickCount();
	MpBpmEnsureMeasurePlayback();
	if (!playf && !g_bpmHeldPcAudio && !(og && ::IsWindow(og->GetSafeHwnd()))) {
		MpPcAudioRetain();
		g_bpmHeldPcAudio = 1;
	}
	OpenMpBpmMeasureDlg(mp);
	MpBpmDlgRefreshUi();
}

static void MpBpmAbortMeasure()
{
	g_bpmArmed = 0;
	g_bpmResultShown = 1;
	g_bpmConfirmPending = 0;
	g_bpmConfirmRoundN = 0;
	g_bpmProvisionalBpm = 0;
	g_bpmProvisionalMeterNum = 0;
	g_bpmProvisionalMeterDen = 0;
	g_bpmProvisionalPulse = 0;
	g_bpmLastRoundConf = 0.f;
	if (g_bpmHeldPcAudio) {
		MpPcAudioRelease();
		g_bpmHeldPcAudio = 0;
	}
	MpBpmDlgRefreshUi();
}

static void MpBpmStoreProvisional(int bpm, int num, int den, int pulse)
{
	g_bpmProvisionalBpm = bpm;
	g_bpmProvisionalMeterNum = num;
	g_bpmProvisionalMeterDen = den;
	g_bpmProvisionalPulse = pulse;
	savedata.mpBpmCand[0] = bpm;
}

static void MpBpmFinishAndShow(BOOL showFailIfNone)
{
	if (g_bpmResultShown) return;
	g_bpmResultShown = 1;
	g_bpmArmed = 0;
	const BOOL hadCapture = (g_bpmHistN > 0 || g_bpmEnvN > 64 || g_bpmLastAcoustic > 0 || g_bpmLastEstimate > 0);
	MpBpmDetectFromPeaks();
	if (g_bpmHeldPcAudio) {
		MpPcAudioRelease();
		g_bpmHeldPcAudio = 0;
	}

	// 高速帯は直近 Estimate（AC×拍子同時）を優先。合意中央値の 161–165 ブレを避ける
	int roundBpm = g_bpmLastAcoustic;
	if (roundBpm <= 0) roundBpm = g_bpmLastRoundBpm;
	if (roundBpm <= 0) roundBpm = g_bpmLastEstimate;
	if (roundBpm <= 0 && g_bpmConsensusBpm > 0) roundBpm = g_bpmConsensusBpm;
	int roundNum = (g_bpmLastMeterNum >= 2) ? g_bpmLastMeterNum : 4;
	int roundDen = (g_bpmLastMeterDen > 0) ? g_bpmLastMeterDen : 4;
	int roundPulse = (g_bpmLastPulse > 0) ? g_bpmLastPulse : 8;
	if (g_bpmLastConf >= 0.70f && g_bpmConsensusMeterNum >= 2
		&& !(roundBpm >= 150 && roundBpm <= 175 && g_bpmLastMeterNum == 3)) {
		roundNum = g_bpmConsensusMeterNum;
		roundDen = g_bpmConsensusMeterDen > 0 ? g_bpmConsensusMeterDen : 4;
		roundPulse = g_bpmConsensusPulse > 0 ? g_bpmConsensusPulse : roundPulse;
	}
	const float roundConf = g_bpmLastRoundConf;

	if (!hadCapture || roundBpm <= 0) {
		g_bpmUiFail = showFailIfNone ? 1 : 0;
		MpBpmDlgRefreshUi();
		if (showFailIfNone)
			OpenMpBpmMeasureDlg(mp);
		return;
	}
	g_bpmUiFail = 0;

	// 高速帯は ±4 でクラスタ一致（161 vs 164 で永久不一致にしない）
	// 拍子は確定時の Estimate を採用（確認中の 4↔3 フリップで止めない）
	auto matchesProv = [&]() -> BOOL {
		if (g_bpmProvisionalBpm <= 0) return FALSE;
		int d = roundBpm - g_bpmProvisionalBpm;
		if (d < 0) d = -d;
		const int tol = (g_bpmProvisionalBpm >= 140 || roundBpm >= 140) ? 4 : 1;
		return (d <= tol) ? TRUE : FALSE;
	};
	auto snapHi = [](int bpm) -> int {
		if (bpm >= 152 && bpm <= 167) return 160;
		if (bpm >= 168 && bpm <= 172) return 170;
		return bpm;
	};
	auto applyFinal = [&](int bpm, int num, int den, int pulse) {
		bpm = snapHi(bpm);
		g_bpmConsensusMeterNum = num;
		g_bpmConsensusMeterDen = den;
		g_bpmConsensusPulse = pulse;
		g_bpmLastMeterNum = num;
		g_bpmLastMeterDen = den;
		g_bpmLastPulse = pulse;
		MpBpmApplyValue(bpm);
		g_bpmConfirmPending = 0;
		g_bpmConfirmRoundN = 0;
		g_bpmProvisionalBpm = 0;
		OpenMpBpmMeasureDlg(mp);
		MpBpmDlgRefreshUi();
	};

	// 周回上限: 仮→確認→緻密を合計で打ち切り、直近 Estimate で確定
	const BOOL forceLock = (g_bpmConfirmRoundN >= 4);

	if (g_bpmConfirmPending == 0) {
		g_bpmConfirmPending = 1;
		MpBpmStoreProvisional(roundBpm, roundNum, roundDen, roundPulse);
		OpenMpBpmMeasureDlg(mp);
		MpBpmDlgRefreshUi();
		MpBpmStartConfirmRound();
		return;
	}

	if (forceLock) {
		applyFinal(roundBpm, roundNum, roundDen, roundPulse);
		return;
	}

	if (g_bpmConfirmPending == 1) {
		if (matchesProv()) {
			g_bpmConfirmPending = 2;
			// 確認一致したら Estimate 側（拍子込み）を仮に昇格
			MpBpmStoreProvisional(roundBpm, roundNum, roundDen, roundPulse);
			OpenMpBpmMeasureDlg(mp);
			MpBpmDlgRefreshUi();
			MpBpmStartConfirmRound();
			return;
		}
		MpBpmStoreProvisional(roundBpm, roundNum, roundDen, roundPulse);
		OpenMpBpmMeasureDlg(mp);
		MpBpmDlgRefreshUi();
		MpBpmStartConfirmRound();
		return;
	}

	// pending==2: 緻密収束 — クラスタ一致 or 信頼度で確定。低信頼でも 2 周したら Estimate 確定
	if (matchesProv() && (roundConf >= 0.42f || g_bpmConfirmRoundN >= 3)) {
		applyFinal(roundBpm, roundNum, roundDen, roundPulse);
		return;
	}
	if (matchesProv() && roundNum == 3 && roundConf >= 0.30f) {
		applyFinal(roundBpm, roundNum, roundDen, roundPulse);
		return;
	}
	if (g_bpmConfirmRoundN >= 3) {
		applyFinal(roundBpm, roundNum, roundDen, roundPulse);
		return;
	}
	MpBpmStoreProvisional(roundBpm, roundNum, roundDen, roundPulse);
	OpenMpBpmMeasureDlg(mp);
	MpBpmDlgRefreshUi();
	MpBpmStartConfirmRound();
}

void MpOnBpmDetect(CMediaPlayerDlg* /*mpDlg*/)
{
	extern int playf;
	if (!g_bpmArmed) {
		g_bpmConfirmPending = 0;
		g_bpmConfirmRoundN = 0;
		g_bpmProvisionalBpm = 0;
		g_bpmProvisionalMeterNum = 0;
		g_bpmProvisionalMeterDen = 0;
		g_bpmProvisionalPulse = 0;
		g_bpmArmed = 1;
		g_bpmResultShown = 0;
		g_bpmUiFail = 0;
		MpBpmResetCapture();
		g_bpmArmedSince = GetTickCount();
		MpBpmEnsureMeasurePlayback();
		if (!playf && !g_bpmHeldPcAudio && !(og && ::IsWindow(og->GetSafeHwnd()))) {
			MpPcAudioRetain();
			g_bpmHeldPcAudio = 1;
		}
		OpenMpBpmMeasureDlg(mp);
		MpBpmDlgRefreshUi();
		return;
	}
	// 計測中の再クリックは中止（強制確定しない）
	MpBpmAbortMeasure();
}

// ---- Mirror output ----
// 初期化/解放は UI スレッドのみ。再生スレッドは Write のみ（CoCreate 禁止）。
static IAudioClient* g_mpMirrorClient = NULL;
static IAudioRenderClient* g_mpMirrorRender = NULL;
static UINT32 g_mpMirrorBufSize = 0;
static volatile LONG g_mpMirrorFailed = 0;
static int g_mpMirrorRate = 0;
static int g_mpMirrorCh = 0;
static int g_mpMirrorBits = 0;
static int g_mpMirrorBpf = 0;
static CRITICAL_SECTION g_mpMirrorCs;
static BOOL g_mpMirrorCsInit = FALSE;

static void MpMirrorCsEnsure()
{
	if (!g_mpMirrorCsInit) {
		InitializeCriticalSection(&g_mpMirrorCs);
		g_mpMirrorCsInit = TRUE;
	}
}

static void MpMirrorReleaseLocked()
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
	MpMirrorCsEnsure();
	EnterCriticalSection(&g_mpMirrorCs);
	MpMirrorReleaseLocked();
	LeaveCriticalSection(&g_mpMirrorCs);
	InterlockedExchange(&g_mpMirrorFailed, 0);
}

// UI スレッド専用。失敗時は failed=1（ユーザーが再ONするまで試行しない）。
static BOOL MpMirrorInitUiLocked()
{
	if (!savedata.mpMirrorOut) {
		MpMirrorReleaseLocked();
		return FALSE;
	}
	if (InterlockedCompareExchange(&g_mpMirrorFailed, 0, 0) != 0)
		return FALSE;

	const int rate = (g_ds_pcm_rate >= 8000) ? g_ds_pcm_rate : ((wavbit_sample_Hz >= 8000) ? wavbit_sample_Hz : 44100);
	const int ch = (g_ds_pcm_ch >= 1) ? g_ds_pcm_ch : 2;
	int bits = g_ds_pcm_bits;
	if (bits != 16 && bits != 24 && bits != 32) bits = 16;

	if (g_mpMirrorClient && g_mpMirrorRender
		&& g_mpMirrorRate == rate && g_mpMirrorCh == ch && g_mpMirrorBits == bits)
		return TRUE;

	MpMirrorReleaseLocked();

	IMMDeviceEnumerator* en = NULL;
	IMMDevice* dev = NULL;
	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator), (void**)&en);
	if (FAILED(hr) || !en) {
		InterlockedExchange(&g_mpMirrorFailed, 1);
		return FALSE;
	}

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
	if (FAILED(hr) || !dev) {
		InterlockedExchange(&g_mpMirrorFailed, 1);
		return FALSE;
	}

	hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&g_mpMirrorClient);
	dev->Release();
	if (FAILED(hr) || !g_mpMirrorClient) {
		InterlockedExchange(&g_mpMirrorFailed, 1);
		return FALSE;
	}

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
		MpMirrorReleaseLocked();
		InterlockedExchange(&g_mpMirrorFailed, 1);
		return FALSE;
	}
	g_mpMirrorClient->GetBufferSize(&g_mpMirrorBufSize);
	hr = g_mpMirrorClient->GetService(__uuidof(IAudioRenderClient), (void**)&g_mpMirrorRender);
	if (FAILED(hr) || !g_mpMirrorRender) {
		MpMirrorReleaseLocked();
		InterlockedExchange(&g_mpMirrorFailed, 1);
		return FALSE;
	}
	g_mpMirrorRate = rate;
	g_mpMirrorCh = ch;
	g_mpMirrorBits = bits;
	g_mpMirrorBpf = (int)wfx.nBlockAlign;
	g_mpMirrorClient->Start();
	return TRUE;
}

void MpMirrorOnFormatReady()
{
	if (!savedata.mpMirrorOut) return;
	MpMirrorCsEnsure();
	EnterCriticalSection(&g_mpMirrorCs);
	MpMirrorInitUiLocked();
	LeaveCriticalSection(&g_mpMirrorCs);
}

void MpMirrorWritePcm(const BYTE* pcm, int bytes)
{
	if (!pcm || bytes <= 0 || !savedata.mpMirrorOut) return;
	if (InterlockedCompareExchange(&g_mpMirrorFailed, 0, 0) != 0) return;

	const int vol = (int)((savedata.mpMirrorVol * (savedata.mpMirrorGain > 0 ? savedata.mpMirrorGain : 100)) / 100);
	if (vol <= 0) return;

	MpMirrorCsEnsure();
	if (!TryEnterCriticalSection(&g_mpMirrorCs))
		return; // 初期化中はドロップ（再生を止めない）

	IAudioClient* client = g_mpMirrorClient;
	IAudioRenderClient* render = g_mpMirrorRender;
	const int bits = g_mpMirrorBits;
	const int bpf = (g_mpMirrorBpf > 0) ? g_mpMirrorBpf : ((g_outBytesPerFrame > 0) ? g_outBytesPerFrame : 4);
	const UINT32 bufSize = g_mpMirrorBufSize;
	const int rate = g_mpMirrorRate;
	const int ch = g_mpMirrorCh;

	if (!client || !render || bpf < 1 || bufSize == 0) {
		LeaveCriticalSection(&g_mpMirrorCs);
		return;
	}
	// フォーマットずれ: 再初期化は UI 側。ここでは書かない。
	const int wantRate = (g_ds_pcm_rate >= 8000) ? g_ds_pcm_rate : rate;
	const int wantCh = (g_ds_pcm_ch >= 1) ? g_ds_pcm_ch : ch;
	int wantBits = g_ds_pcm_bits;
	if (wantBits != 16 && wantBits != 24 && wantBits != 32) wantBits = bits;
	if (rate != wantRate || ch != wantCh || bits != wantBits) {
		LeaveCriticalSection(&g_mpMirrorCs);
		return;
	}

	static BYTE scaled[65536];
	BYTE* dst = (BYTE*)pcm;
	const int useLen = bytes;
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

	UINT32 pad = 0;
	if (FAILED(client->GetCurrentPadding(&pad))) {
		MpMirrorReleaseLocked();
		InterlockedExchange(&g_mpMirrorFailed, 1);
		LeaveCriticalSection(&g_mpMirrorCs);
		return;
	}
	const UINT32 room = (bufSize > pad) ? (bufSize - pad) : 0;
	const UINT32 framesNeed = (UINT32)(useLen / bpf);
	if (framesNeed == 0 || framesNeed > room) {
		LeaveCriticalSection(&g_mpMirrorCs);
		return;
	}

	BYTE* pData = NULL;
	if (FAILED(render->GetBuffer(framesNeed, &pData))) {
		MpMirrorReleaseLocked();
		InterlockedExchange(&g_mpMirrorFailed, 1);
		LeaveCriticalSection(&g_mpMirrorCs);
		return;
	}
	memcpy(pData, dst, (size_t)useLen);
	render->ReleaseBuffer(framesNeed, 0);
	LeaveCriticalSection(&g_mpMirrorCs);
}

// ---- Remote AAC (ADTS)：接続中クライアントがあるときだけエンコード ----
#include <mfapi.h>
#include <mftransform.h>
#include <wmcodecdsp.h>
#include <mferror.h>
#pragma comment(lib, "wmcodecdspuuid.lib")

enum { kMpRemAacRing = 262144 };
enum { kMpRemAacPcmMax = 8192 };
enum { kMpRemAacFrame = 1024 };

static CRITICAL_SECTION g_mpRemAacCs;
static int g_mpRemAacCsInit = 0;
static IMFTransform* g_mpRemAacEnc = NULL;
static int g_mpRemAacRate = 0;
static int g_mpRemAacMfUp = 0;
static volatile LONG g_mpRemAacClients = 0;
static BYTE g_mpRemAacRing[kMpRemAacRing];
static volatile LONG g_mpRemAacW = 0;
static short g_mpRemAacPcm[kMpRemAacPcmMax * 2];
static int g_mpRemAacPcmN = 0;
static LONGLONG g_mpRemAacTime = 0;
static DWORD g_mpRemAacLastPcmMs = 0;
static int g_mpRemAacOutProv = 0;
static DWORD g_mpRemAacOutCb = 0;

static void MpRemAacCsEnsure()
{
	if (g_mpRemAacCsInit) return;
	InitializeCriticalSection(&g_mpRemAacCs);
	g_mpRemAacCsInit = 1;
}

static void MpRemAacRingWriteLocked(const BYTE* p, int n)
{
	if (!p || n <= 0) return;
	LONG w = g_mpRemAacW;
	for (int i = 0; i < n; ++i) {
		g_mpRemAacRing[(unsigned)(w + i) % (unsigned)kMpRemAacRing] = p[i];
	}
	InterlockedExchange(&g_mpRemAacW, w + n);
}

static void MpRemAacEncReleaseLocked()
{
	if (g_mpRemAacEnc) {
		g_mpRemAacEnc->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
		g_mpRemAacEnc->Release();
		g_mpRemAacEnc = NULL;
	}
	g_mpRemAacRate = 0;
	g_mpRemAacPcmN = 0;
	g_mpRemAacTime = 0;
	g_mpRemAacOutProv = 0;
	g_mpRemAacOutCb = 0;
}

static int MpRemAacPickSrcRate()
{
	int r = g_ds_pcm_rate;
	if (r < 8000) r = wavbit_sample_Hz;
	if (r < 8000) r = 44100;
	return r;
}

// MF AAC は実質 44100/48000。それ以外は間引いて渡す（嘘のレート表記をしない）。
static int MpRemAacPickDstRate(int src)
{
	if (src == 44100 || src == 48000) return src;
	if (src > 0 && (src % 48000) == 0) return 48000;
	if (src > 0 && (src % 44100) == 0) return 44100;
	if (src >= 48000) return 48000;
	return 44100;
}

static int g_mpRemAacAdtsProfile = 1; // AAC-LC
static int g_mpRemAacAdtsSfi = 4;     // 44100
static int g_mpRemAacAdtsCh = 2;
static int g_mpRemAacLpL = 0;
static int g_mpRemAacLpR = 0;

static int MpRemAacSfIndex(int rate)
{
	static const int kSf[12] = {
		96000, 88200, 64000, 48000, 44100, 32000,
		24000, 22050, 16000, 12000, 11025, 8000
	};
	for (int i = 0; i < 12; ++i) {
		if (kSf[i] == rate) return i;
	}
	return (rate >= 46000) ? 3 : 4; // 48000 / 44100
}

// raw AAC 1フレームに ADTS ヘッダを付けてリングへ（MF の ADTS 出力は環境差があるため自前）
static void MpRemAacRingWriteAacFrameLocked(const BYTE* data, int len)
{
	if (!data || len <= 0) return;
	if (len >= 7 && data[0] == 0xFF && (data[1] & 0xF0) == 0xF0) {
		const int fl = ((data[3] & 3) << 11) | (data[4] << 3) | ((data[5] & 0xE0) >> 5);
		// 長さが一致するときだけ「既に ADTS」とみなす（偶然の 0xFFF を誤認しない）
		if (fl == len) {
			MpRemAacRingWriteLocked(data, len);
			return;
		}
	}
	const int profile = g_mpRemAacAdtsProfile;
	const int sfi = g_mpRemAacAdtsSfi;
	const int ch = g_mpRemAacAdtsCh;
	const int total = len + 7;
	BYTE h[7];
	h[0] = 0xFF;
	h[1] = 0xF1; // MPEG-4, layer0, no CRC
	h[2] = (BYTE)(((profile & 3) << 6) | ((sfi & 0x0F) << 2) | ((ch >> 2) & 0x01));
	h[3] = (BYTE)(((ch & 3) << 6) | ((total >> 11) & 0x03));
	h[4] = (BYTE)((total >> 3) & 0xFF);
	h[5] = (BYTE)(((total & 7) << 5) | 0x1F);
	h[6] = 0xFC;
	MpRemAacRingWriteLocked(h, 7);
	MpRemAacRingWriteLocked(data, len);
}

static BOOL MpRemAacEncEnsureLocked(int rate)
{
	if (rate != 44100 && rate != 48000) rate = MpRemAacPickDstRate(rate);
	if (g_mpRemAacEnc && g_mpRemAacRate == rate)
		return TRUE;
	MpRemAacEncReleaseLocked();
	g_mpRemAacAdtsProfile = 1;
	g_mpRemAacAdtsSfi = MpRemAacSfIndex(rate);
	g_mpRemAacAdtsCh = 2;
	g_mpRemAacLpL = 0;
	g_mpRemAacLpR = 0;
	if (!g_mpRemAacMfUp) {
		CoInitializeEx(NULL, COINIT_MULTITHREADED);
		if (FAILED(MFStartup(MF_VERSION)))
			return FALSE;
		g_mpRemAacMfUp = 1;
	}
	IMFTransform* enc = NULL;
	HRESULT hr = CoCreateInstance(CLSID_AACMFTEncoder, NULL, CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&enc));
	if (FAILED(hr) || !enc) {
		CString msg = LL14(
			L"AACエンコーダを初期化できませんでした。\n次に: リモート設定で AAC をオフにするか、Windows Media Feature Pack を確認してください。",
			L"Could not init AAC encoder.\nNext: turn AAC off in Remote settings, or check Windows Media Feature Pack.",
			L"Echec init AAC.\nEnsuite: desactiver AAC ou verifier Media Feature Pack.",
			L"Init AAC non riuscita.\nPoi: disattiva AAC o controlla Media Feature Pack.",
			L"No se pudo iniciar AAC.\nSiguiente: desactive AAC o revise Media Feature Pack.",
			L"AAC 인코더 초기화 실패.\n다음: 리모트에서 AAC 끄기 또는 Media Feature Pack 확인.",
			L"无法初始化 AAC。\n下一步：在遥控中关闭 AAC，或检查 Media Feature Pack。",
			L"تعذر تهيئة AAC.\nالتالي: أوقف AAC أو تحقق من Media Feature Pack.",
			L"Не удалось инициализировать AAC.\nДалее: отключите AAC или проверьте Media Feature Pack.",
			L"AAC-Encoder fehlgeschlagen.\nAls Naechstes: AAC aus oder Media Feature Pack pruefen.",
			L"Falha ao iniciar AAC.\nSeguinte: desligue AAC ou verifique Media Feature Pack.",
			L"AAC-encoder mislukt.\nVolgende: AAC uit of Media Feature Pack controleren.",
			L"Nie udalo sie AAC.\nDalej: wylacz AAC lub sprawdz Media Feature Pack.",
			L"AAC baslatilamadi.\nSonraki: AAC kapatin veya Media Feature Pack kontrol edin.");
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->MessageBox(msg, L"AAC", MB_OK | MB_ICONWARNING);
		return FALSE;
	}

	IMFMediaType* outType = NULL;
	IMFMediaType* inType = NULL;
	hr = MFCreateMediaType(&outType);
	if (SUCCEEDED(hr)) hr = outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	if (SUCCEEDED(hr)) hr = outType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
	if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
	if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32)rate);
	if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
	{
		extern int MpFeatAacBytesPerSec();
		const int bps = MpFeatAacBytesPerSec();
		if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, (UINT32)bps);
	}
	// 0=raw。ADTS は自前付与（環境によって MF の ADTS が壊れて持続ノイズになるため）
	if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);
	if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29);
	if (SUCCEEDED(hr)) hr = enc->SetOutputType(0, outType, 0);

	if (SUCCEEDED(hr)) hr = MFCreateMediaType(&inType);
	if (SUCCEEDED(hr)) hr = inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	if (SUCCEEDED(hr)) hr = inType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
	if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
	if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32)rate);
	if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
	if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
	if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, (UINT32)(rate * 4));
	if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_AUDIO_CHANNEL_MASK, 0x3u); // FL|FR
	if (SUCCEEDED(hr)) hr = enc->SetInputType(0, inType, 0);

	// エンコーダが確定した ASC から ADTS の profile/sfi/ch を合わせる
	if (SUCCEEDED(hr)) {
		IMFMediaType* cur = NULL;
		if (SUCCEEDED(enc->GetOutputCurrentType(0, &cur)) && cur) {
			UINT32 n = 0;
			if (SUCCEEDED(cur->GetBlobSize(MF_MT_USER_DATA, &n)) && n >= 14) {
				BYTE ud[64];
				UINT32 got = 0;
				if (n > sizeof(ud)) n = (UINT32)sizeof(ud);
				if (SUCCEEDED(cur->GetBlob(MF_MT_USER_DATA, ud, n, &got)) && got >= 14) {
					const UINT32 ascOff = 12; // HEAACWAVEINFO without WAVEFORMATEX
					if (got > ascOff + 1) {
						const unsigned v = ((unsigned)ud[ascOff] << 8) | (unsigned)ud[ascOff + 1];
						const int aot = (int)((v >> 11) & 0x1F);
						const int sfi = (int)((v >> 7) & 0x0F);
						const int chcfg = (int)((v >> 3) & 0x0F);
						if (aot >= 1 && aot <= 4)
							g_mpRemAacAdtsProfile = aot - 1;
						if (sfi >= 0 && sfi <= 11)
							g_mpRemAacAdtsSfi = sfi;
						if (chcfg >= 1 && chcfg <= 7)
							g_mpRemAacAdtsCh = chcfg;
					}
				}
			}
			cur->Release();
		}
	}

	if (outType) outType->Release();
	if (inType) inType->Release();
	if (FAILED(hr)) {
		enc->Release();
		return FALSE;
	}
	enc->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
	enc->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

	MFT_OUTPUT_STREAM_INFO osi = {};
	if (SUCCEEDED(enc->GetOutputStreamInfo(0, &osi))) {
		g_mpRemAacOutProv = (osi.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) ? 1 : 0;
		g_mpRemAacOutCb = osi.cbSize;
	}
	g_mpRemAacEnc = enc;
	g_mpRemAacRate = rate;
	g_mpRemAacPcmN = 0;
	g_mpRemAacTime = 0;
	return TRUE;
}

static void MpRemAacDrainOutLocked()
{
	if (!g_mpRemAacEnc) return;
	for (;;) {
		MFT_OUTPUT_DATA_BUFFER ob = {};
		IMFSample* sample = NULL;
		IMFMediaBuffer* buf = NULL;
		if (!g_mpRemAacOutProv) {
			const DWORD cb = (g_mpRemAacOutCb > 0) ? g_mpRemAacOutCb : 4096;
			if (FAILED(MFCreateSample(&sample))) break;
			if (FAILED(MFCreateMemoryBuffer(cb, &buf))) { sample->Release(); break; }
			sample->AddBuffer(buf);
			buf->Release();
			ob.pSample = sample;
		}
		DWORD st = 0;
		const HRESULT hr = g_mpRemAacEnc->ProcessOutput(0, 1, &ob, &st);
		if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
			if (sample) sample->Release();
			break;
		}
		if (FAILED(hr)) {
			if (sample) sample->Release();
			if (ob.pEvents) ob.pEvents->Release();
			break;
		}
		IMFSample* outS = ob.pSample ? ob.pSample : sample;
		if (outS) {
			IMFMediaBuffer* outB = NULL;
			if (SUCCEEDED(outS->ConvertToContiguousBuffer(&outB)) && outB) {
				BYTE* p = NULL; DWORD maxL = 0, curL = 0;
				if (SUCCEEDED(outB->Lock(&p, &maxL, &curL)) && p && curL > 0)
					MpRemAacRingWriteAacFrameLocked(p, (int)curL);
				if (p) outB->Unlock();
				outB->Release();
			}
		}
		if (ob.pSample && ob.pSample != sample) ob.pSample->Release();
		if (sample) sample->Release();
		if (ob.pEvents) ob.pEvents->Release();
	}
}

static void MpRemAacEncodeFramesLocked(const short* pcm, int frames)
{
	if (!g_mpRemAacEnc || !pcm || frames <= 0) return;
	const DWORD cb = (DWORD)(frames * 2 * 2);
	IMFSample* sample = NULL;
	IMFMediaBuffer* buf = NULL;
	if (FAILED(MFCreateSample(&sample))) return;
	if (FAILED(MFCreateMemoryBuffer(cb, &buf))) { sample->Release(); return; }
	BYTE* p = NULL;
	if (SUCCEEDED(buf->Lock(&p, NULL, NULL)) && p) {
		memcpy(p, pcm, cb);
		buf->Unlock();
	}
	buf->SetCurrentLength(cb);
	sample->AddBuffer(buf);
	buf->Release();
	const LONGLONG dur = (10000000LL * frames) / (g_mpRemAacRate > 0 ? g_mpRemAacRate : 44100);
	sample->SetSampleTime(g_mpRemAacTime);
	sample->SetSampleDuration(dur);
	g_mpRemAacTime += dur;
	HRESULT hr = g_mpRemAacEnc->ProcessInput(0, sample, 0);
	sample->Release();
	if (SUCCEEDED(hr) || hr == MF_E_NOTACCEPTING)
		MpRemAacDrainOutLocked();
}

static void MpRemAacPushStereo16Locked(const short* stereo, int frames, int srcRate)
{
	if (!stereo || frames <= 0) return;
	if (srcRate < 8000) srcRate = MpRemAacPickSrcRate();
	const int dstRate = MpRemAacPickDstRate(srcRate);
	if (!MpRemAacEncEnsureLocked(dstRate)) return;

	static short rs[16384 * 2];
	const short* use = stereo;
	int useFrames = frames;
	if (srcRate != dstRate) {
		if (frames > 16384) frames = 16384;
		if (srcRate > dstRate && (srcRate % dstRate) == 0) {
			const int step = srcRate / dstRate;
			// 三角窓でアンチエイリアス（単純平均より折返しサー音が減る）
			useFrames = frames / step;
			if (useFrames > 1) useFrames -= 1;
			for (int i = 0; i < useFrames; ++i) {
				const int c = i * step + step / 2;
				int sumL = 0, sumR = 0, wsum = 0;
				for (int d = -(step - 1); d <= (step - 1); ++d) {
					int idx = c + d;
					if (idx < 0) idx = 0;
					if (idx >= frames) idx = frames - 1;
					int w = step - (d < 0 ? -d : d);
					if (w < 1) w = 1;
					sumL += (int)stereo[idx * 2] * w;
					sumR += (int)stereo[idx * 2 + 1] * w;
					wsum += w;
				}
				if (wsum < 1) wsum = 1;
				rs[i * 2] = (short)(sumL / wsum);
				rs[i * 2 + 1] = (short)(sumR / wsum);
			}
		}
		else {
			// 線形（非整数比・アップ含む）
			useFrames = (int)(((__int64)frames * dstRate) / srcRate);
			if (useFrames < 1) return;
			if (useFrames > 16384) useFrames = 16384;
			for (int i = 0; i < useFrames; ++i) {
				const __int64 pos = ((__int64)i * srcRate);
				int i0 = (int)(pos / dstRate);
				int i1 = i0 + 1;
				if (i0 >= frames) i0 = frames - 1;
				if (i1 >= frames) i1 = frames - 1;
				const int frac = (int)(pos % dstRate);
				const int L0 = stereo[i0 * 2], L1 = stereo[i1 * 2];
				const int R0 = stereo[i0 * 2 + 1], R1 = stereo[i1 * 2 + 1];
				rs[i * 2] = (short)(L0 + (int)(((__int64)(L1 - L0) * frac) / dstRate));
				rs[i * 2 + 1] = (short)(R0 + (int)(((__int64)(R1 - R0) * frac) / dstRate));
			}
		}
		use = rs;
	}

	// 軽い LPF（~10kHz）で残留サー音を削る（固定小数 α≈0.75）
	{
		static short filt[16384 * 2];
		int n = useFrames;
		if (n > 16384) n = 16384;
		for (int i = 0; i < n; ++i) {
			const int xL = use[i * 2];
			const int xR = use[i * 2 + 1];
			g_mpRemAacLpL += ((xL - g_mpRemAacLpL) * 3) >> 2;
			g_mpRemAacLpR += ((xR - g_mpRemAacLpR) * 3) >> 2;
			filt[i * 2] = (short)g_mpRemAacLpL;
			filt[i * 2 + 1] = (short)g_mpRemAacLpR;
		}
		use = filt;
		useFrames = n;
	}

	int off = 0;
	while (off < useFrames) {
		if (g_mpRemAacPcmN >= kMpRemAacPcmMax)
			g_mpRemAacPcmN = 0;
		int n = useFrames - off;
		if (n > kMpRemAacPcmMax - g_mpRemAacPcmN)
			n = kMpRemAacPcmMax - g_mpRemAacPcmN;
		memcpy(g_mpRemAacPcm + g_mpRemAacPcmN * 2, use + off * 2, (size_t)n * 4);
		g_mpRemAacPcmN += n;
		off += n;
		while (g_mpRemAacPcmN >= kMpRemAacFrame) {
			MpRemAacEncodeFramesLocked(g_mpRemAacPcm, kMpRemAacFrame);
			g_mpRemAacPcmN -= kMpRemAacFrame;
			if (g_mpRemAacPcmN > 0)
				memmove(g_mpRemAacPcm, g_mpRemAacPcm + kMpRemAacFrame * 2, (size_t)g_mpRemAacPcmN * 4);
		}
	}
	g_mpRemAacLastPcmMs = GetTickCount();
}

void MpRemoteWritePcm(const BYTE* pcm, int bytes)
{
	if (!pcm || bytes <= 0) return;
	if (!savedata.mpRemoteOn || !savedata.mpRemoteAac) return;
	if (InterlockedCompareExchange(&g_mpRemAacClients, 0, 0) <= 0) return;

	const int ch = (g_ds_pcm_ch >= 1) ? g_ds_pcm_ch : 2;
	int bpf = (g_outBytesPerFrame > 0) ? g_outBytesPerFrame : 0;
	int bits = g_ds_pcm_bits;
	if (bpf >= ch && ch > 0) {
		const int bps = bpf / ch;
		if (bps == 4) bits = 32;
		else if (bps == 3) bits = 24;
		else bits = 16;
	}
	else {
		if (bits != 16 && bits != 24 && bits != 32) bits = 16;
		bpf = ch * (bits / 8);
	}
	if (bpf < 1 || bytes < bpf) return;
	const int frames = bytes / bpf;
	if (frames <= 0 || frames > 16384) return;

	static short stereo[16384 * 2];
	if (bits == 16) {
		const short* src = (const short*)pcm;
		for (int i = 0; i < frames; ++i) {
			short L = src[i * ch];
			short R = (ch >= 2) ? src[i * ch + 1] : L;
			stereo[i * 2] = L;
			stereo[i * 2 + 1] = R;
		}
	}
	else if (bits == 24) {
		const int step = bits / 8;
		for (int i = 0; i < frames; ++i) {
			const BYTE* b = pcm + i * bpf;
			int L = (int)b[0] | ((int)b[1] << 8) | ((int)((signed char)b[2]) << 16);
			int R = L;
			if (ch >= 2) {
				const BYTE* br = b + step;
				R = (int)br[0] | ((int)br[1] << 8) | ((int)((signed char)br[2]) << 16);
			}
			stereo[i * 2] = (short)(L >> 8);
			stereo[i * 2 + 1] = (short)(R >> 8);
		}
	}
	else {
		const int* src = (const int*)pcm;
		for (int i = 0; i < frames; ++i) {
			int L = src[i * ch];
			int R = (ch >= 2) ? src[i * ch + 1] : L;
			stereo[i * 2] = (short)(L >> 16);
			stereo[i * 2 + 1] = (short)(R >> 16);
		}
	}

	MpRemAacCsEnsure();
	if (!TryEnterCriticalSection(&g_mpRemAacCs))
		return;
	MpRemAacPushStereo16Locked(stereo, frames, MpRemAacPickSrcRate());
	LeaveCriticalSection(&g_mpRemAacCs);
}

// ---- Remote video: Douga HWND → RGB → H264 MFT + JPEG(multipart) ----
enum { kMpRemVidRing = 2 * 1024 * 1024 };
enum { kMpRemVidJpegMax = 256 * 1024 };
static CRITICAL_SECTION g_mpRemVidCs;
static int g_mpRemVidCsInit = 0;
static volatile LONG g_mpRemVidClients = 0;
static volatile LONG g_mpRemotePosCs = 0;
static BYTE g_mpRemVidRing[kMpRemVidRing];
static volatile LONG g_mpRemVidW = 0;
static BYTE g_mpRemVidJpeg[kMpRemVidJpegMax];
static volatile LONG g_mpRemVidJpegN = 0;
static volatile LONG g_mpRemVidJpegPts = 0;
static volatile LONG g_mpRemVidJpegGen = 0;
static IMFTransform* g_mpRemH264 = NULL;
static int g_mpRemH264W = 0, g_mpRemH264H = 0;
static int g_mpRemH264WantNv12 = 0;
static DWORD g_mpRemVidLastCap = 0;
static ULONG_PTR g_mpRemGdipToken = 0;
static int g_mpRemGdipUp = 0;

static void MpRemVidCsEnsure()
{
	if (!g_mpRemVidCsInit) {
		InitializeCriticalSection(&g_mpRemVidCs);
		g_mpRemVidCsInit = 1;
	}
}

static void MpRemVidGdipEnsure()
{
	if (g_mpRemGdipUp) return;
	Gdiplus::GdiplusStartupInput in;
	if (Gdiplus::GdiplusStartup(&g_mpRemGdipToken, &in, NULL) == Gdiplus::Ok)
		g_mpRemGdipUp = 1;
}

static int MpRemVidGetEncoderClsid(const WCHAR* mime, CLSID* pClsid)
{
	UINT n = 0, s = 0;
	Gdiplus::GetImageEncodersSize(&n, &s);
	if (!s) return -1;
	Gdiplus::ImageCodecInfo* info = (Gdiplus::ImageCodecInfo*)malloc(s);
	if (!info) return -1;
	Gdiplus::GetImageEncoders(n, s, info);
	int found = -1;
	for (UINT i = 0; i < n; ++i) {
		if (wcscmp(info[i].MimeType, mime) == 0) {
			*pClsid = info[i].Clsid;
			found = (int)i;
			break;
		}
	}
	free(info);
	return found;
}

static void MpRemVidPushRing(const BYTE* data, int n, int ptsCs)
{
	if (!data || n <= 0 || n > 512 * 1024) return;
	MpRemVidCsEnsure();
	EnterCriticalSection(&g_mpRemVidCs);
	// packet: 'V' 'D' u16 len, i32 pts, payload
	const int total = 2 + 2 + 4 + n;
	LONG w = g_mpRemVidW;
	if (total < kMpRemVidRing) {
		g_mpRemVidRing[(unsigned)(w++) % (unsigned)kMpRemVidRing] = 'V';
		g_mpRemVidRing[(unsigned)(w++) % (unsigned)kMpRemVidRing] = 'D';
		g_mpRemVidRing[(unsigned)(w++) % (unsigned)kMpRemVidRing] = (BYTE)((n >> 8) & 0xFF);
		g_mpRemVidRing[(unsigned)(w++) % (unsigned)kMpRemVidRing] = (BYTE)(n & 0xFF);
		g_mpRemVidRing[(unsigned)(w++) % (unsigned)kMpRemVidRing] = (BYTE)((ptsCs >> 24) & 0xFF);
		g_mpRemVidRing[(unsigned)(w++) % (unsigned)kMpRemVidRing] = (BYTE)((ptsCs >> 16) & 0xFF);
		g_mpRemVidRing[(unsigned)(w++) % (unsigned)kMpRemVidRing] = (BYTE)((ptsCs >> 8) & 0xFF);
		g_mpRemVidRing[(unsigned)(w++) % (unsigned)kMpRemVidRing] = (BYTE)(ptsCs & 0xFF);
		for (int i = 0; i < n; ++i)
			g_mpRemVidRing[(unsigned)(w++) % (unsigned)kMpRemVidRing] = data[i];
		InterlockedExchange(&g_mpRemVidW, w);
	}
	LeaveCriticalSection(&g_mpRemVidCs);
}

static BOOL MpRemH264Ensure(int w, int h)
{
	w &= ~1; h &= ~1;
	if (w < 16) w = 16;
	if (h < 16) h = 16;
	if (g_mpRemH264 && g_mpRemH264W == w && g_mpRemH264H == h)
		return TRUE;
	if (g_mpRemH264) {
		g_mpRemH264->Release();
		g_mpRemH264 = NULL;
		g_mpRemH264W = g_mpRemH264H = 0;
	}
	static int s_mfUp = 0;
	if (!s_mfUp) {
		if (FAILED(MFStartup(MF_VERSION)))
			return FALSE;
		s_mfUp = 1;
	}
	IMFTransform* enc = NULL;
	HRESULT hr = CoCreateInstance(CLSID_CMSH264EncoderMFT, NULL, CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&enc));
	if (FAILED(hr) || !enc) return FALSE;
	ICodecAPI* api = NULL;
	if (SUCCEEDED(enc->QueryInterface(IID_PPV_ARGS(&api))) && api) {
		VARIANT v; VariantInit(&v); v.vt = VT_BOOL; v.boolVal = VARIANT_TRUE;
		api->SetValue(&CODECAPI_AVLowLatencyMode, &v);
		VariantClear(&v);
		api->Release();
	}
	IMFMediaType* outType = NULL;
	IMFMediaType* inType = NULL;
	hr = MFCreateMediaType(&outType);
	if (SUCCEEDED(hr)) hr = outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	if (SUCCEEDED(hr)) hr = outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
	if (SUCCEEDED(hr)) hr = MFSetAttributeSize(outType, MF_MT_FRAME_SIZE, (UINT32)w, (UINT32)h);
	if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(outType, MF_MT_FRAME_RATE, 10, 1);
	if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AVG_BITRATE, 600000);
	if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_MPEG2_PROFILE, (UINT32)eAVEncH264VProfile_Base);
	if (SUCCEEDED(hr)) hr = enc->SetOutputType(0, outType, 0);
	if (SUCCEEDED(hr)) hr = MFCreateMediaType(&inType);
	if (SUCCEEDED(hr)) hr = inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	if (SUCCEEDED(hr)) hr = inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
	if (SUCCEEDED(hr)) hr = MFSetAttributeSize(inType, MF_MT_FRAME_SIZE, (UINT32)w, (UINT32)h);
	if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(inType, MF_MT_FRAME_RATE, 10, 1);
	if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32)(w * 4));
	if (SUCCEEDED(hr)) hr = enc->SetInputType(0, inType, 0);
	if (FAILED(hr)) {
		// RGB32 拒否時は NV12
		if (inType) { inType->Release(); inType = NULL; }
		hr = MFCreateMediaType(&inType);
		if (SUCCEEDED(hr)) hr = inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
		if (SUCCEEDED(hr)) hr = inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
		if (SUCCEEDED(hr)) hr = MFSetAttributeSize(inType, MF_MT_FRAME_SIZE, (UINT32)w, (UINT32)h);
		if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(inType, MF_MT_FRAME_RATE, 10, 1);
		if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
		if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32)w);
		if (SUCCEEDED(hr)) hr = enc->SetInputType(0, inType, 0);
		if (SUCCEEDED(hr))
			g_mpRemH264WantNv12 = 1;
		else
			g_mpRemH264WantNv12 = 0;
	} else {
		g_mpRemH264WantNv12 = 0;
	}
	if (outType) outType->Release();
	if (inType) inType->Release();
	if (FAILED(hr)) {
		enc->Release();
		return FALSE;
	}
	enc->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
	enc->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
	g_mpRemH264 = enc;
	g_mpRemH264W = w;
	g_mpRemH264H = h;
	return TRUE;
}

static void MpRemRgb32ToNv12(const BYTE* bgra, int w, int h, int stride, BYTE* nv12)
{
	BYTE* yPlane = nv12;
	BYTE* uvPlane = nv12 + w * h;
	for (int y = 0; y < h; ++y) {
		const BYTE* row = bgra + y * stride;
		BYTE* yd = yPlane + y * w;
		for (int x = 0; x < w; ++x) {
			const int b = row[x * 4 + 0], g = row[x * 4 + 1], r = row[x * 4 + 2];
			int Y = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
			if (Y < 0) Y = 0; if (Y > 255) Y = 255;
			yd[x] = (BYTE)Y;
		}
	}
	for (int y = 0; y < h; y += 2) {
		const BYTE* row0 = bgra + y * stride;
		const BYTE* row1 = bgra + (y + 1) * stride;
		BYTE* uvd = uvPlane + (y / 2) * w;
		for (int x = 0; x < w; x += 2) {
			const int b = (row0[x * 4] + row0[(x + 1) * 4] + row1[x * 4] + row1[(x + 1) * 4]) >> 2;
			const int g = (row0[x * 4 + 1] + row0[(x + 1) * 4 + 1] + row1[x * 4 + 1] + row1[(x + 1) * 4 + 1]) >> 2;
			const int r = (row0[x * 4 + 2] + row0[(x + 1) * 4 + 2] + row1[x * 4 + 2] + row1[(x + 1) * 4 + 2]) >> 2;
			int U = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
			int V = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
			if (U < 0) U = 0; if (U > 255) U = 255;
			if (V < 0) V = 0; if (V > 255) V = 255;
			uvd[x] = (BYTE)U;
			uvd[x + 1] = (BYTE)V;
		}
	}
}

static void MpRemH264PushRgb(const BYTE* rgb, int w, int h, int stride, int ptsCs)
{
	if (!MpRemH264Ensure(w, h) || !g_mpRemH264 || !rgb) return;
	BYTE* nvScratch = NULL;
	const BYTE* payload = rgb;
	DWORD cb = (DWORD)(stride * h);
	if (g_mpRemH264WantNv12) {
		cb = (DWORD)(w * h * 3 / 2);
		nvScratch = (BYTE*)malloc(cb);
		if (!nvScratch) return;
		MpRemRgb32ToNv12(rgb, w, h, stride, nvScratch);
		payload = nvScratch;
	}
	IMFSample* sample = NULL;
	IMFMediaBuffer* buf = NULL;
	if (FAILED(MFCreateSample(&sample))) { free(nvScratch); return; }
	if (FAILED(MFCreateMemoryBuffer(cb, &buf))) { sample->Release(); free(nvScratch); return; }
	BYTE* p = NULL; DWORD maxLen = 0;
	if (SUCCEEDED(buf->Lock(&p, &maxLen, NULL)) && p) {
		memcpy(p, payload, cb);
		buf->Unlock();
		buf->SetCurrentLength(cb);
	}
	sample->AddBuffer(buf);
	sample->SetSampleTime((LONGLONG)ptsCs * 100000LL);
	sample->SetSampleDuration(1000000LL); // 10fps
	buf->Release();
	g_mpRemH264->ProcessInput(0, sample, 0);
	sample->Release();
	free(nvScratch);

	MFT_OUTPUT_STREAM_INFO osi = {};
	g_mpRemH264->GetOutputStreamInfo(0, &osi);
	const BOOL provides = (osi.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) != 0;
	for (;;) {
		MFT_OUTPUT_DATA_BUFFER out = {};
		DWORD status = 0;
		IMFSample* os = NULL;
		if (!provides) {
			IMFMediaBuffer* ob = NULL;
			DWORD need = osi.cbSize ? osi.cbSize : (DWORD)(256 * 1024);
			if (FAILED(MFCreateSample(&os))) break;
			if (FAILED(MFCreateMemoryBuffer(need, &ob))) { os->Release(); break; }
			os->AddBuffer(ob);
			ob->Release();
			out.pSample = os;
		}
		HRESULT hr = g_mpRemH264->ProcessOutput(0, 1, &out, &status);
		if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
			if (os) os->Release();
			if (out.pEvents) out.pEvents->Release();
			break;
		}
		if (FAILED(hr)) {
			if (os) os->Release();
			if (out.pSample && out.pSample != os) out.pSample->Release();
			if (out.pEvents) out.pEvents->Release();
			break;
		}
		IMFSample* got = out.pSample ? out.pSample : os;
		if (got) {
			IMFMediaBuffer* outBuf = NULL;
			if (SUCCEEDED(got->ConvertToContiguousBuffer(&outBuf)) && outBuf) {
				BYTE* op = NULL; DWORD olen = 0;
				if (SUCCEEDED(outBuf->Lock(&op, NULL, &olen)) && op && olen > 0)
					MpRemVidPushRing(op, (int)olen, ptsCs);
				if (op) outBuf->Unlock();
				outBuf->Release();
			}
			got->Release();
		}
		if (out.pEvents) out.pEvents->Release();
	}
}

static void MpRemVidClientEnter()
{
	InterlockedIncrement(&g_mpRemVidClients);
}
static void MpRemVidClientLeave()
{
	LONG n = InterlockedDecrement(&g_mpRemVidClients);
	if (n < 0) InterlockedExchange(&g_mpRemVidClients, 0);
}

static volatile LONG g_mpRemVidWantMs = 0;
static void MpRemVidWantPulse()
{
	InterlockedExchange(&g_mpRemVidWantMs, (LONG)GetTickCount());
}

static void MpRemVidEncodeJpeg(const BYTE* bits, int dw, int dh, int stride, int ptsCs)
{
	MpRemVidGdipEnsure();
	if (!g_mpRemGdipUp || !bits) return;
	// WGC は BGRA。アルファを不透明にして JPEG 化する
	Gdiplus::Bitmap bmp(dw, dh, stride, PixelFormat32bppPARGB, (BYTE*)bits);
	CLSID jpgClsid = {};
	if (MpRemVidGetEncoderClsid(L"image/jpeg", &jpgClsid) < 0) return;
	IStream* stm = NULL;
	if (FAILED(CreateStreamOnHGlobal(NULL, TRUE, &stm)) || !stm) return;
	Gdiplus::EncoderParameters ep;
	ULONG q = 42;
	ep.Count = 1;
	ep.Parameter[0].Guid = Gdiplus::EncoderQuality;
	ep.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
	ep.Parameter[0].NumberOfValues = 1;
	ep.Parameter[0].Value = &q;
	if (bmp.Save(stm, &jpgClsid, &ep) == Gdiplus::Ok) {
		STATSTG st = {};
		if (SUCCEEDED(stm->Stat(&st, STATFLAG_NONAME))) {
			const ULONG n = (ULONG)st.cbSize.QuadPart;
			if (n > 0 && n < (ULONG)kMpRemVidJpegMax) {
				HGLOBAL hg = NULL;
				if (SUCCEEDED(GetHGlobalFromStream(stm, &hg)) && hg) {
					const void* p = GlobalLock(hg);
					if (p) {
						MpRemVidCsEnsure();
						EnterCriticalSection(&g_mpRemVidCs);
						memcpy(g_mpRemVidJpeg, p, n);
						InterlockedExchange(&g_mpRemVidJpegN, (LONG)n);
						InterlockedExchange(&g_mpRemVidJpegPts, (LONG)ptsCs);
						InterlockedIncrement(&g_mpRemVidJpegGen);
						LeaveCriticalSection(&g_mpRemVidCs);
						GlobalUnlock(hg);
					}
				}
			}
		}
	}
	stm->Release();
}

// 映像取得: EVR はトップレベル枠の WGC、それ以外は IBasicVideo::GetCurrentImage（レンダラ別の正攻法）
static void MpRemVidCaptureTick()
{
	if (!savedata.mpRemoteOn) return;
	const LONG wantMs = InterlockedCompareExchange(&g_mpRemVidWantMs, 0, 0);
	const BOOL wantFresh = (wantMs != 0) && ((DWORD)(GetTickCount() - (DWORD)wantMs) < 4000u);
	if (InterlockedCompareExchange(&g_mpRemVidClients, 0, 0) <= 0 && !wantFresh)
		return;
	if (!(mode == -2 || videoonly)) return;
	if (!pMainFrame1 || !::IsWindow(pMainFrame1->GetSafeHwnd()) || !::IsWindowVisible(pMainFrame1->GetSafeHwnd()))
		return;
	if (::IsIconic(pMainFrame1->GetSafeHwnd()))
		return;
	const DWORD now = GetTickCount();
	if (g_mpRemVidLastCap != 0 && (now - g_mpRemVidLastCap) < 50)
		return;
	g_mpRemVidLastCap = now;

	BYTE* rgb = NULL;
	int dw = 0, dh = 0, stride = 0;

	if (ev) {
		HWND frame = pMainFrame1->GetSafeHwnd();
		HWND site = pMainFrame1->GetVideoSiteHwnd();
		if (!site || !::IsWindow(site))
			site = frame;
		RECT fr = {}, sr = {};
		if (!::GetWindowRect(frame, &fr) || !::GetWindowRect(site, &sr))
			return;
		int cropX = sr.left - fr.left;
		int cropY = sr.top - fr.top;
		int cropW = sr.right - sr.left;
		int cropH = sr.bottom - sr.top;
		if (cropW < 8 || cropH < 8) {
			cropX = 0; cropY = 0;
			cropW = fr.right - fr.left;
			cropH = fr.bottom - fr.top;
		}
		if (cropW < 8 || cropH < 8) return;
		dw = cropW; dh = cropH;
		if (dw > 480) {
			dh = (int)((__int64)dh * 480 / dw);
			dw = 480;
		}
		dw &= ~1; dh &= ~1;
		if (dw < 16) dw = 16;
		if (dh < 16) dh = 16;
		stride = dw * 4;
		rgb = (BYTE*)malloc((size_t)stride * (size_t)dh);
		if (!rgb) return;
		memset(rgb, 0, (size_t)stride * (size_t)dh);
		// 初回はフレーム未到着があり得るので続けて2回
		if (!ScWgcCaptureWindowBgraCrop(frame, rgb, dw, dh, stride, cropX, cropY, cropW, cropH)) {
			if (!ScWgcCaptureWindowBgraCrop(frame, rgb, dw, dh, stride, cropX, cropY, cropW, cropH)) {
				free(rgb);
				return;
			}
		}
		for (int y = 0; y < dh; ++y) {
			BYTE* row = rgb + (size_t)y * (size_t)stride;
			for (int x = 0; x < dw; ++x)
				row[x * 4 + 3] = 255;
		}
	} else {
		if (!pBasicVideo) return;
		long dibSize = 0;
		if (FAILED(pBasicVideo->GetCurrentImage(&dibSize, NULL)) || dibSize <= (long)sizeof(BITMAPINFOHEADER))
			return;
		BYTE* dibBuf = (BYTE*)malloc((size_t)dibSize);
		if (!dibBuf) return;
		if (FAILED(pBasicVideo->GetCurrentImage(&dibSize, (long*)dibBuf))) {
			free(dibBuf);
			return;
		}
		BITMAPINFOHEADER* bih = (BITMAPINFOHEADER*)dibBuf;
		const int bw = bih->biWidth;
		const int bhAbs = abs(bih->biHeight);
		if (bih->biSize < sizeof(BITMAPINFOHEADER) || bw < 8 || bhAbs < 8) {
			free(dibBuf);
			return;
		}
		dw = bw; dh = bhAbs;
		if (dw > 480) {
			dh = (int)((__int64)dh * 480 / dw);
			dw = 480;
		}
		dw &= ~1; dh &= ~1;
		if (dw < 16 || dh < 16) {
			free(dibBuf);
			return;
		}
		BITMAPINFO bi = {};
		bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bi.bmiHeader.biWidth = dw;
		bi.bmiHeader.biHeight = -dh;
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;
		void* bits = NULL;
		HDC hdcScreen = ::GetDC(NULL);
		HDC hdcMem = hdcScreen ? ::CreateCompatibleDC(hdcScreen) : NULL;
		HBITMAP hbmp = hdcMem ? ::CreateDIBSection(hdcMem, &bi, DIB_RGB_COLORS, &bits, NULL, 0) : NULL;
		BOOL ok = FALSE;
		if (hdcMem && hbmp && bits) {
			HGDIOBJ old = ::SelectObject(hdcMem, hbmp);
			::SetStretchBltMode(hdcMem, HALFTONE);
			ok = (::StretchDIBits(hdcMem, 0, 0, dw, dh, 0, 0, bw, bhAbs,
				dibBuf + bih->biSize, (BITMAPINFO*)bih, DIB_RGB_COLORS, SRCCOPY) != 0);
			::SelectObject(hdcMem, old);
		}
		free(dibBuf);
		if (!ok || !bits) {
			if (hbmp) ::DeleteObject(hbmp);
			if (hdcMem) ::DeleteDC(hdcMem);
			if (hdcScreen) ::ReleaseDC(NULL, hdcScreen);
			return;
		}
		stride = dw * 4;
		rgb = (BYTE*)malloc((size_t)stride * (size_t)dh);
		if (!rgb) {
			::DeleteObject(hbmp);
			::DeleteDC(hdcMem);
			::ReleaseDC(NULL, hdcScreen);
			return;
		}
		memcpy(rgb, bits, (size_t)stride * (size_t)dh);
		for (int y = 0; y < dh; ++y) {
			BYTE* row = rgb + (size_t)y * (size_t)stride;
			for (int x = 0; x < dw; ++x)
				row[x * 4 + 3] = 255;
		}
		::DeleteObject(hbmp);
		::DeleteDC(hdcMem);
		::ReleaseDC(NULL, hdcScreen);
	}

	const int ptsCs = (int)InterlockedCompareExchange(&g_mpRemotePosCs, 0, 0);
	// 静止画の再送を抑える（ポーズ等）
	{
		DWORD h = 2166136261u;
		const int ptsN = 16;
		for (int i = 0; i < ptsN; ++i) {
			const int x = (dw * (i * 2 + 1)) / (ptsN * 2);
			const int y = (dh * ((i % 4) * 2 + 1)) / 8;
			const BYTE* p = rgb + (size_t)y * (size_t)stride + (size_t)x * 4;
			h ^= (DWORD)p[0] + ((DWORD)p[1] << 8) + ((DWORD)p[2] << 16);
			h *= 16777619u;
		}
		static DWORD s_lastHash = 0;
		static DWORD s_lastHashMs = 0;
		if (h == s_lastHash && (now - s_lastHashMs) < 400
			&& InterlockedCompareExchange(&g_mpRemVidJpegN, 0, 0) > 0) {
			free(rgb);
			return;
		}
		s_lastHash = h;
		s_lastHashMs = now;
	}
	MpRemH264PushRgb(rgb, dw, dh, stride, ptsCs);
	MpRemVidEncodeJpeg(rgb, dw, dh, stride, ptsCs);
	free(rgb);
}

static void MpRemAacFeedSilenceIfNeeded()
{
	if (!savedata.mpRemoteOn || !savedata.mpRemoteAac) return;
	if (InterlockedCompareExchange(&g_mpRemAacClients, 0, 0) <= 0) return;
	// 再生中の DS チャンク間に無音を挟むとノイズになる。停止/一時停止時のみ。
	if (plf == 1 && ps != 1) return;
	const DWORD now = GetTickCount();
	if (g_mpRemAacLastPcmMs != 0 && (now - g_mpRemAacLastPcmMs) < 250)
		return;
	MpRemAacCsEnsure();
	if (!TryEnterCriticalSection(&g_mpRemAacCs))
		return;
	const int dst = MpRemAacPickDstRate(MpRemAacPickSrcRate());
	static short z[kMpRemAacFrame * 2];
	memset(z, 0, sizeof(z));
	MpRemAacPushStereo16Locked(z, kMpRemAacFrame, dst);
	LeaveCriticalSection(&g_mpRemAacCs);
}

// リング上の ADTS 1フレーム長。0=不足 / -1=非同期
static int MpRemAacAdtsLenAt(LONG absPos, LONG wpos)
{
	if (wpos - absPos < 7) return 0;
	const unsigned r = (unsigned)kMpRemAacRing;
	const BYTE b0 = g_mpRemAacRing[(unsigned)absPos % r];
	const BYTE b1 = g_mpRemAacRing[(unsigned)(absPos + 1) % r];
	if (b0 != 0xFF || (b1 & 0xF0) != 0xF0) return -1;
	const BYTE b3 = g_mpRemAacRing[(unsigned)(absPos + 3) % r];
	const BYTE b4 = g_mpRemAacRing[(unsigned)(absPos + 4) % r];
	const BYTE b5 = g_mpRemAacRing[(unsigned)(absPos + 5) % r];
	const int fl = ((b3 & 3) << 11) | (b4 << 3) | ((b5 & 0xE0) >> 5);
	if (fl < 7 || fl > 8192) return -1;
	if (wpos - absPos < fl) return 0;
	return fl;
}

static LONG MpRemAacFindAdts(LONG from, LONG wpos, int maxScan)
{
	if (from < 0) from = 0;
	for (int i = 0; i < maxScan; ++i) {
		if (from + i + 7 > wpos) break;
		const int fl = MpRemAacAdtsLenAt(from + i, wpos);
		if (fl > 0) return from + i;
		if (fl == 0) break; // 途中フレーム待ち
	}
	return -1;
}

static volatile LONG g_mpRemAacLagCs = 90; // 歌詞補正用の聴こえ遅延目安(1/100秒)

static void MpRemAacClientEnter()
{
	MpRemAacCsEnsure();
	EnterCriticalSection(&g_mpRemAacCs);
	InterlockedIncrement(&g_mpRemAacClients);
	MpRemAacEncEnsureLocked(MpRemAacPickDstRate(MpRemAacPickSrcRate()));
	LeaveCriticalSection(&g_mpRemAacCs);
}

static void MpRemAacClientLeave()
{
	MpRemAacCsEnsure();
	EnterCriticalSection(&g_mpRemAacCs);
	LONG n = InterlockedDecrement(&g_mpRemAacClients);
	if (n < 0) {
		InterlockedExchange(&g_mpRemAacClients, 0);
		n = 0;
	}
	if (n == 0) {
		MpRemAacEncReleaseLocked();
		InterlockedExchange(&g_mpRemAacW, 0);
	}
	LeaveCriticalSection(&g_mpRemAacCs);
}

// ---- Remote HTTP (LAN / Wi-Fi、最大3クライアント同時) ----
enum { kMpRemoteMaxClients = 6 };
static SOCKET g_mpRemoteListen = INVALID_SOCKET;
static HANDLE g_mpRemoteThread = NULL;
static volatile LONG g_mpRemoteStop = 0;
static int g_mpRemoteBoundPort = 0; // EnsureRunning が実際に bind したポート
static HWND g_mpRemoteHwnd = NULL;
static int g_mpRemoteWsa = 0;
static int g_mpRemoteMuteRestore = -1; // >=0: ミュート中（復元音量）
static volatile LONG g_mpRemoteBusy = 0; // 処理中クライアント数
static CRITICAL_SECTION g_mpRemoteCs;
static int g_mpRemoteCsInit = 0;
static SOCKET g_mpRemoteActive[kMpRemoteMaxClients];

static volatile LONG g_mpRemoteVolCache = 50;
static volatile LONG g_mpRemoteDurCs = 0;
static volatile LONG g_mpRemoteLrcCur = -1;
static volatile LONG g_mpRemotePlayIdx = -1;
static volatile LONG g_mpRemotePlayCnt = 0;
static volatile LONG g_mpRemoteHeadDeg100 = 0; // 再生位置角度*100（0..36000）
static DWORD g_mpRemoteScratchLastMs = 0;

extern int plcnt;
extern int gameon;
extern void MpPushPlayHistory(LPCTSTR path, LPCTSTR displayName);
extern void equaliser(void* data, int len, BOOL reset);

static BYTE g_mpRemoteSpec[8][64];
static int g_mpRemoteSpecCh = 1;
static BYTE g_mpRemoteNotes[108];
static BYTE g_mpRemoteNoteExpr[108];
static BYTE g_mpRemoteHist[72][14]; // 行0=最新、各14B=MIDI0..107 活性ビット
static BYTE g_mpRemoteHistExpr[72][108];
static int g_mpRemoteHistRows = 0;
static int g_mpRemoteExprOn = 0;
static WCHAR g_mpRemoteChord[48] = L"-";
static volatile LONG g_mpRemoteVizSeq = 0;
static volatile LONG g_mpRemoteWantPianoMs = 0; // GetTickCount 相当を格納
static volatile LONG g_mpRemoteWantAnaMs = 0;

static void MpRemoteCsEnsure()
{
	if (g_mpRemoteCsInit) return;
	InitializeCriticalSection(&g_mpRemoteCs);
	for (int i = 0; i < kMpRemoteMaxClients; ++i)
		g_mpRemoteActive[i] = INVALID_SOCKET;
	g_mpRemoteCsInit = 1;
}

static void MpRemoteTrackClient(SOCKET s, BOOL add)
{
	MpRemoteCsEnsure();
	EnterCriticalSection(&g_mpRemoteCs);
	if (add) {
		for (int i = 0; i < kMpRemoteMaxClients; ++i) {
			if (g_mpRemoteActive[i] == INVALID_SOCKET) {
				g_mpRemoteActive[i] = s;
				break;
			}
		}
	} else {
		for (int i = 0; i < kMpRemoteMaxClients; ++i) {
			if (g_mpRemoteActive[i] == s)
				g_mpRemoteActive[i] = INVALID_SOCKET;
		}
	}
	LeaveCriticalSection(&g_mpRemoteCs);
}

// Kick 済みなら closesocket しない（二重 close 防止）
static void MpRemoteReleaseClient(SOCKET s)
{
	MpRemoteCsEnsure();
	BOOL mine = FALSE;
	EnterCriticalSection(&g_mpRemoteCs);
	for (int i = 0; i < kMpRemoteMaxClients; ++i) {
		if (g_mpRemoteActive[i] == s) {
			g_mpRemoteActive[i] = INVALID_SOCKET;
			mine = TRUE;
			break;
		}
	}
	LeaveCriticalSection(&g_mpRemoteCs);
	if (mine)
		closesocket(s);
}

static void MpRemoteKickClients()
{
	MpRemoteCsEnsure();
	SOCKET kill[kMpRemoteMaxClients];
	EnterCriticalSection(&g_mpRemoteCs);
	for (int i = 0; i < kMpRemoteMaxClients; ++i) {
		kill[i] = g_mpRemoteActive[i];
		g_mpRemoteActive[i] = INVALID_SOCKET;
	}
	LeaveCriticalSection(&g_mpRemoteCs);
	for (int i = 0; i < kMpRemoteMaxClients; ++i) {
		if (kill[i] != INVALID_SOCKET) {
			shutdown(kill[i], SD_BOTH);
			closesocket(kill[i]);
		}
	}
}

// 既定ルートの IPv4（LAN）。UDP connect で実送信なし。
static BOOL MpRemoteGetLanIpv4(wchar_t* out, int cch)
{
	if (!out || cch < 8) return FALSE;
	out[0] = 0;
	WSADATA wsa = {};
	const BOOL needWsa = (g_mpRemoteWsa == 0);
	if (needWsa && WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return FALSE;
	SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	BOOL ok = FALSE;
	if (s != INVALID_SOCKET) {
		sockaddr_in dst = {};
		dst.sin_family = AF_INET;
		dst.sin_port = htons(53);
		dst.sin_addr.s_addr = htonl(0x08080808); // 8.8.8.8
		if (connect(s, (sockaddr*)&dst, sizeof(dst)) == 0) {
			sockaddr_in local = {};
			int len = sizeof(local);
			if (getsockname(s, (sockaddr*)&local, &len) == 0) {
				const DWORD ip = ntohl(local.sin_addr.s_addr);
				if (ip != 0 && (ip >> 24) != 127) {
					_snwprintf_s(out, cch, _TRUNCATE, L"%u.%u.%u.%u",
						(ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
					ok = TRUE;
				}
			}
		}
		closesocket(s);
	}
	if (needWsa)
		WSACleanup();
	return ok;
}

static void MpRemoteCacheVol(int v)
{
	if (v < 0) v = 0;
	if (v > 100) v = 100;
	InterlockedExchange(&g_mpRemoteVolCache, (LONG)v);
}

static void MpRemoteSendCmd(int cmd, LPARAM lp = 0)
{
	// accept/worker から UI へは PostMessage のみ（SendMessage は終了時デッドロック）
	if (g_mpRemoteHwnd && ::IsWindow(g_mpRemoteHwnd))
		::PostMessage(g_mpRemoteHwnd, WM_MP_TRANSPORT_CMD, (WPARAM)cmd, lp);
}

static int MpRemoteQueryInt(const char* q, const char* key, int defv)
{
	if (!q || !key) return defv;
	const size_t klen = strlen(key);
	for (const char* p = q; (p = strstr(p, key)) != NULL; ++p) {
		if (p > q && p[-1] != '?' && p[-1] != '&') continue;
		return atoi(p + klen);
	}
	return defv;
}

static void MpRemoteSyncEqUi()
{
	if (!og || !og->m_EqualizerDlg) return;
	if (!::IsWindow(og->m_EqualizerDlg->GetSafeHwnd())) return;
	og->m_EqualizerDlg->SyncSlidersFromSavedata();
}

static void MpRemoteSendVolAbs(int v)
{
	if (v < 0) v = 0;
	if (v > 100) v = 100;
	if (v > 0)
		g_mpRemoteMuteRestore = -1;
	MpRemoteCacheVol(v);
	if (g_mpRemoteHwnd && ::IsWindow(g_mpRemoteHwnd))
		::PostMessage(g_mpRemoteHwnd, WM_MP_TRANSPORT_CMD, (WPARAM)10, (LPARAM)v);
}

static BOOL MpRemoteHasQueryParam(const char* qs, const char* key)
{
	if (!qs || !key) return FALSE;
	const size_t klen = strlen(key);
	for (const char* p = qs; (p = strstr(p, key)) != NULL; ++p) {
		if (p > qs && p[-1] != '?' && p[-1] != '&') continue;
		const char after = p[klen];
		if (after == 0 || after == ' ' || after == '&' || after == '\r' || after == '\n')
			return TRUE;
	}
	return FALSE;
}

static void MpRemoteEscHtml(const wchar_t* in, CStringW& out)
{
	out.Empty();
	if (!in) { out = L""; return; }
	for (const wchar_t* p = in; *p; ++p) {
		if (*p == L'&') out += L"&amp;";
		else if (*p == L'<') out += L"&lt;";
		else if (*p == L'>') out += L"&gt;";
		else if (*p == L'"') out += L"&quot;";
		else if (*p == L'\'') out += L"&#39;";
		else out += *p;
	}
}

static void MpRemoteEscJson(const wchar_t* in, CStringW& out)
{
	out.Empty();
	if (!in) { out = L""; return; }
	for (const wchar_t* p = in; *p; ++p) {
		if (*p == L'\\' || *p == L'"') { out += L'\\'; out += *p; }
		else if (*p == L'\n') out += L"\\n";
		else if (*p == L'\r') out += L"\\r";
		else if (*p == L'\t') out += L"\\t";
		else if (*p < 0x20) { wchar_t b[8]; _snwprintf_s(b, _TRUNCATE, L"\\u%04x", (unsigned)*p); out += b; }
		else out += *p;
	}
}

static BOOL MpRemoteSendAll(SOCKET s, const char* data, int len)
{
	if (!data || len <= 0) return TRUE;
	int off = 0;
	while (off < len) {
		const int n = send(s, data + off, len - off, 0);
		if (n > 0) { off += n; continue; }
		const int err = WSAGetLastError();
		if (n < 0 && (err == WSAEWOULDBLOCK || err == WSAEINTR)) {
			fd_set wf; FD_ZERO(&wf); FD_SET(s, &wf);
			timeval tv; tv.tv_sec = 5; tv.tv_usec = 0;
			if (select(0, NULL, &wf, NULL, &tv) <= 0) return FALSE;
			continue;
		}
		return FALSE;
	}
	return TRUE;
}

static void MpRemoteReadMeta(CStringW& title, CStringW& artist, CStringW& album, int& vol, const wchar_t*& state, int& muted)
{
	title.Empty(); artist.Empty(); album.Empty();
	muted = 0;
	state = L"stop";

	// accept スレッドから呼ぶ。CWnd::GetPos/GetWindowText は SendMessage 相当なので使わない。
	title = fnn;
	if (mp && ::IsWindow(mp->GetSafeHwnd())) {
		// CurrentTrackTitle はグローバル参照のみ（HWND メッセージ無し）
		CString t = mp->CurrentTrackTitle();
		if (!t.IsEmpty()) title = t;
	}
	artist = tagname;
	album = tagalbum;
	if (pl && pl->pc && pl->pnt >= 0 && pl->pnt < pl->playcnt) {
		const playlistdata0& row = pl->pc[pl->pnt];
		if (title.IsEmpty() && row.name[0]) title = row.name;
		if (artist.IsEmpty() && row.art[0]) artist = row.art;
		if (album.IsEmpty() && row.alb[0]) album = row.alb;
	}
	vol = (int)InterlockedCompareExchange(&g_mpRemoteVolCache, 0, 0);
	if (vol < 0) vol = 0;
	if (vol > 100) vol = 100;
	if (g_mpRemoteMuteRestore >= 0) {
		muted = 1;
		vol = 0;
	}
	if (plf == 1 && ps == 1) state = L"pause";
	else if (plf == 1) state = L"play";
	else state = L"stop";
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

	if (strstr(line, "GET /overlay")) {
		CString html;
		MpFeatEnsureRemoteOverlayHtml(html);
		CStringA utf8 = CW2A(html, CP_UTF8);
		CStringA hdr;
		hdr.Format("HTTP/1.0 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nCache-Control: no-store\r\nContent-Length: %d\r\n\r\n",
			utf8.GetLength());
		MpRemoteSendAll(s, hdr, hdr.GetLength());
		MpRemoteSendAll(s, utf8, utf8.GetLength());
		return;
	}
	if (strstr(line, "GET /api/queue-add") || strstr(line, "POST /api/queue-add")) {
		const char* q = strchr(line, '?');
		int idx = MpRemoteQueryInt(q, "i=", -1);
		if (idx < 0) idx = MpRemoteQueryInt(q, "index=", -1);
		if (idx >= 0)
			MpRemoteSendCmd(30, (LPARAM)idx); // queue add
		const char* ok = "HTTP/1.0 204 No Content\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n";
		MpRemoteSendAll(s, ok, (int)strlen(ok));
		return;
	}

	if (strstr(line, "GET /vframe.jpg") || strstr(line, "GET /vframe")) {
		// 単発 JPEG（スマホは multipart 非対応が多い → ポーリング用）
		MpRemVidWantPulse();
		InterlockedIncrement(&g_mpRemVidClients); // キャプチャ起動のフック
		BYTE local[kMpRemVidJpegMax];
		int ln = 0;
		int pts = 0;
		MpRemVidCsEnsure();
		EnterCriticalSection(&g_mpRemVidCs);
		ln = (int)InterlockedCompareExchange(&g_mpRemVidJpegN, 0, 0);
		pts = (int)InterlockedCompareExchange(&g_mpRemVidJpegPts, 0, 0);
		if (ln > 0 && ln < kMpRemVidJpegMax)
			memcpy(local, g_mpRemVidJpeg, (size_t)ln);
		else
			ln = 0;
		LeaveCriticalSection(&g_mpRemVidCs);
		InterlockedDecrement(&g_mpRemVidClients);
		if (ln <= 0) {
			const char* no =
				"HTTP/1.0 204 No Content\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n";
			MpRemoteSendAll(s, no, (int)strlen(no));
			return;
		}
		char hdr[192];
		_snprintf_s(hdr, _TRUNCATE,
			"HTTP/1.0 200 OK\r\nContent-Type: image/jpeg\r\nX-Pts-Cs: %d\r\n"
			"Cache-Control: no-store\r\nConnection: close\r\nContent-Length: %d\r\n\r\n",
			pts, ln);
		MpRemoteSendAll(s, hdr, (int)strlen(hdr));
		MpRemoteSendAll(s, (const char*)local, ln);
		return;
	}

	if (strstr(line, "GET /vmjpeg") || strstr(line, "GET /vstream.jpg")) {
		// ブラウザ向け multipart JPEG（聴くONで <img>）
		MpRemVidWantPulse();
		const char* hdr =
			"HTTP/1.0 200 OK\r\n"
			"Content-Type: multipart/x-mixed-replace; boundary=mpframe\r\n"
			"Cache-Control: no-store\r\n"
			"Pragma: no-cache\r\n"
			"Connection: close\r\n"
			"\r\n";
		MpRemoteSendAll(s, hdr, (int)strlen(hdr));
		MpRemVidClientEnter();
		u_long nb = 1;
		ioctlsocket(s, FIONBIO, &nb);
		LONG lastGen = -1;
		while (InterlockedCompareExchange(&g_mpRemoteStop, 0, 0) == 0) {
			const LONG gen = InterlockedCompareExchange(&g_mpRemVidJpegGen, 0, 0);
			const LONG n = InterlockedCompareExchange(&g_mpRemVidJpegN, 0, 0);
			const LONG pts = InterlockedCompareExchange(&g_mpRemVidJpegPts, 0, 0);
			if (gen != lastGen && n > 0 && n < kMpRemVidJpegMax) {
				lastGen = gen;
				BYTE local[kMpRemVidJpegMax];
				int ln = 0;
				MpRemVidCsEnsure();
				EnterCriticalSection(&g_mpRemVidCs);
				ln = (int)InterlockedCompareExchange(&g_mpRemVidJpegN, 0, 0);
				if (ln > 0 && ln < kMpRemVidJpegMax)
					memcpy(local, g_mpRemVidJpeg, (size_t)ln);
				else
					ln = 0;
				LeaveCriticalSection(&g_mpRemVidCs);
				if (ln > 0) {
					char part[160];
					_snprintf_s(part, _TRUNCATE,
						"--mpframe\r\nContent-Type: image/jpeg\r\nX-Pts-Cs: %d\r\nContent-Length: %d\r\n\r\n",
						(int)pts, ln);
					if (!MpRemoteSendAll(s, part, (int)strlen(part)))
						break;
					if (!MpRemoteSendAll(s, (const char*)local, ln))
						break;
					if (!MpRemoteSendAll(s, "\r\n", 2))
						break;
				}
			} else {
				Sleep(30);
			}
			char peek;
			const int pk = recv(s, &peek, 1, MSG_PEEK);
			if (pk == 0) break;
			if (pk < 0) {
				const int e = WSAGetLastError();
				if (e != WSAEWOULDBLOCK && e != WSAEINTR) break;
			}
		}
		MpRemVidClientLeave();
		return;
	}

	if (strstr(line, "GET /vstream")) {
		// H264 Annex-B（VDフレーム: 'V''D' u16len i32pts payload）
		const char* hdr =
			"HTTP/1.0 200 OK\r\n"
			"Content-Type: application/octet-stream\r\n"
			"Cache-Control: no-store\r\n"
			"Pragma: no-cache\r\n"
			"Connection: close\r\n"
			"\r\n";
		MpRemoteSendAll(s, hdr, (int)strlen(hdr));
		MpRemVidClientEnter();
		u_long nb = 1;
		ioctlsocket(s, FIONBIO, &nb);
		LONG rpos = InterlockedCompareExchange(&g_mpRemVidW, 0, 0);
		while (InterlockedCompareExchange(&g_mpRemoteStop, 0, 0) == 0) {
			const LONG wpos = InterlockedCompareExchange(&g_mpRemVidW, 0, 0);
			LONG avail = wpos - rpos;
			if (avail < 0) avail = 0;
			if (avail > kMpRemVidRing / 2) {
				rpos = wpos - (kMpRemVidRing / 4);
				if (rpos < 0) rpos = 0;
				avail = wpos - rpos;
			}
			if (avail < 8) {
				Sleep(20);
				char peek;
				const int pk = recv(s, &peek, 1, MSG_PEEK);
				if (pk == 0) break;
				if (pk < 0) {
					const int e = WSAGetLastError();
					if (e != WSAEWOULDBLOCK && e != WSAEINTR) break;
				}
				continue;
			}
			char chunk[64 * 1024];
			int chunkN = 0;
			LONG p = rpos;
			while (chunkN + 8 < (int)sizeof(chunk) && (wpos - p) >= 8) {
				const BYTE b0 = g_mpRemVidRing[(unsigned)(p) % (unsigned)kMpRemVidRing];
				const BYTE b1 = g_mpRemVidRing[(unsigned)(p + 1) % (unsigned)kMpRemVidRing];
				if (b0 != 'V' || b1 != 'D') {
					p++;
					continue;
				}
				const int n = ((int)g_mpRemVidRing[(unsigned)(p + 2) % (unsigned)kMpRemVidRing] << 8)
					| (int)g_mpRemVidRing[(unsigned)(p + 3) % (unsigned)kMpRemVidRing];
				if (n <= 0 || n > 512 * 1024) { p += 2; continue; }
				if ((wpos - p) < (8 + n)) break;
				if (chunkN + 8 + n > (int)sizeof(chunk)) break;
				for (int i = 0; i < 8 + n; ++i)
					chunk[chunkN + i] = (char)g_mpRemVidRing[(unsigned)(p + i) % (unsigned)kMpRemVidRing];
				chunkN += 8 + n;
				p += 8 + n;
			}
			if (chunkN <= 0) {
				rpos = p;
				Sleep(10);
				continue;
			}
			if (!MpRemoteSendAll(s, chunk, chunkN))
				break;
			rpos = p;
		}
		MpRemVidClientLeave();
		return;
	}

	if (strstr(line, "GET /stream")) {
		if (!savedata.mpRemoteAac) {
			const char* no =
				"HTTP/1.0 404 Not Found\r\nConnection: close\r\n"
				"Content-Type: text/plain; charset=utf-8\r\n\r\nAAC off";
			MpRemoteSendAll(s, no, (int)strlen(no));
			return;
		}
		const char* hdr =
			"HTTP/1.0 200 OK\r\n"
			"Content-Type: audio/aac\r\n"
			"Cache-Control: no-store\r\n"
			"Pragma: no-cache\r\n"
			"Connection: close\r\n"
			"\r\n";
		MpRemoteSendAll(s, hdr, (int)strlen(hdr));
		MpRemAacClientEnter();
		LONG rpos = -1;
		const DWORD t0 = GetTickCount();
		// 最初の ADTS 同期まで待つ（途中開始＝ホワイトノイズ）
		while (InterlockedCompareExchange(&g_mpRemoteStop, 0, 0) == 0) {
			MpRemAacFeedSilenceIfNeeded();
			const LONG wpos = InterlockedCompareExchange(&g_mpRemAacW, 0, 0);
			if (wpos > 0) {
				LONG start = wpos - 8192;
				if (start < 0) start = 0;
				rpos = MpRemAacFindAdts(start, wpos, 8192);
				if (rpos >= 0) break;
			}
			if (GetTickCount() - t0 > 2000) break;
			Sleep(5);
		}
		if (rpos < 0) {
			MpRemAacClientLeave();
			return;
		}
		// ごく短いプライム（数フレーム）。長く溜めると歌詞ずれが増える。
		{
			const DWORD tp = GetTickCount();
			int frames = 0;
			while (frames < 4 && InterlockedCompareExchange(&g_mpRemoteStop, 0, 0) == 0) {
				const LONG wpos = InterlockedCompareExchange(&g_mpRemAacW, 0, 0);
				LONG p = rpos;
				frames = 0;
				while (frames < 8) {
					const int fl = MpRemAacAdtsLenAt(p, wpos);
					if (fl <= 0) break;
					p += fl;
					frames++;
				}
				if (frames >= 4) break;
				if (GetTickCount() - tp > 400) break;
				MpRemAacFeedSilenceIfNeeded();
				Sleep(5);
			}
		}
		u_long nb = 1;
		ioctlsocket(s, FIONBIO, &nb);
		while (InterlockedCompareExchange(&g_mpRemoteStop, 0, 0) == 0) {
			MpRemAacFeedSilenceIfNeeded();
			const LONG wpos = InterlockedCompareExchange(&g_mpRemAacW, 0, 0);
			LONG avail = wpos - rpos;
			if (avail < 0) avail = 0;
			if (avail > kMpRemAacRing / 2) {
				// 遅れすぎ: ライブ付近の ADTS 先頭へ（途中バイトから読まない）
				LONG start = wpos - 16384;
				if (start < rpos) start = rpos;
				if (start < 0) start = 0;
				const LONG syn = MpRemAacFindAdts(start, wpos, 16384);
				if (syn >= 0) rpos = syn;
				else rpos = wpos;
				avail = wpos - rpos;
			}
			if (avail < 7) {
				Sleep(3);
				char peek;
				const int pk = recv(s, &peek, 1, MSG_PEEK);
				if (pk == 0) break;
				if (pk < 0) {
					const int e = WSAGetLastError();
					if (e != WSAEWOULDBLOCK && e != WSAEINTR) break;
				}
				continue;
			}
			// 完全な ADTS フレームだけ送る（途中切断＝ノイズ）
			char chunk[4096];
			int chunkN = 0;
			LONG p = rpos;
			while (chunkN + 7 < (int)sizeof(chunk)) {
				const int fl = MpRemAacAdtsLenAt(p, wpos);
				if (fl <= 0) break;
				if (chunkN + fl > (int)sizeof(chunk)) break;
				for (int i = 0; i < fl; ++i)
					chunk[chunkN + i] = (char)g_mpRemAacRing[(unsigned)(p + i) % (unsigned)kMpRemAacRing];
				chunkN += fl;
				p += fl;
			}
			if (chunkN <= 0) {
				// 同期ずれ: 次の sync を探す
				const LONG syn = MpRemAacFindAdts(rpos + 1, wpos, 4096);
				if (syn >= 0) rpos = syn;
				else Sleep(3);
				continue;
			}
			MpRemoteSendAll(s, chunk, chunkN);
			rpos = p;
			{
				// 聴こえ遅延の粗い推定（リング残 + 固定エンコード分）
				const LONG left = wpos - rpos;
				int lag = 40 + (int)((left * 8) / 1280); // 128kbps 換算 cs
				if (lag < 50) lag = 50;
				if (lag > 250) lag = 250;
				InterlockedExchange(&g_mpRemAacLagCs, (LONG)lag);
			}
		}
		MpRemAacClientLeave();
		return;
	}

	if (strstr(line, "GET /cmd")) {
		const char* q = strchr(line, '?');
		if (MpRemoteHasQueryParam(q, "c=play")) MpRemoteSendCmd(0);
		else if (MpRemoteHasQueryParam(q, "c=pause")) MpRemoteSendCmd(1);
		else if (MpRemoteHasQueryParam(q, "c=next")) MpRemoteSendCmd(2);
		else if (MpRemoteHasQueryParam(q, "c=volup")) MpRemoteSendCmd(3);
		else if (MpRemoteHasQueryParam(q, "c=voldn")) MpRemoteSendCmd(4);
		else if (MpRemoteHasQueryParam(q, "c=prev")) MpRemoteSendCmd(5);
		else if (MpRemoteHasQueryParam(q, "c=stop")) MpRemoteSendCmd(6);
		else if (MpRemoteHasQueryParam(q, "c=seekbk")) MpRemoteSendCmd(7);
		else if (MpRemoteHasQueryParam(q, "c=seekfw")) MpRemoteSendCmd(8);
		else if (MpRemoteHasQueryParam(q, "c=mute")) MpRemoteSendCmd(9);
		else if (MpRemoteHasQueryParam(q, "c=vol") && q) {
			int v = MpRemoteQueryInt(q, "v=", 50);
			MpRemoteSendVolAbs(v);
		}
		else if (MpRemoteHasQueryParam(q, "c=playidx") && q) {
			MpRemoteSendCmd(11, (LPARAM)MpRemoteQueryInt(q, "i=", -1));
		}
		else if (MpRemoteHasQueryParam(q, "c=lrc") && q) {
			MpRemoteSendCmd(12, (LPARAM)MpRemoteQueryInt(q, "delta=", 0));
		}
		else if (MpRemoteHasQueryParam(q, "c=lrcsave")) MpRemoteSendCmd(13);
		else if (MpRemoteHasQueryParam(q, "c=eqband") && q) {
			const int b = MpRemoteQueryInt(q, "b=", 0);
			const int v = MpRemoteQueryInt(q, "v=", 100);
			MpRemoteSendCmd(14, (LPARAM)((b << 16) | (v & 0xFFFF)));
		}
		else if (MpRemoteHasQueryParam(q, "c=eqpreset") && q) {
			MpRemoteSendCmd(15, (LPARAM)MpRemoteQueryInt(q, "p=", 0));
		}
		else if (MpRemoteHasQueryParam(q, "c=eqenv") && q) {
			MpRemoteSendCmd(16, (LPARAM)MpRemoteQueryInt(q, "p=", 0));
		}
		else if (MpRemoteHasQueryParam(q, "c=eqfx") && q) {
			const int w = MpRemoteQueryInt(q, "w=", 0);
			const int v = MpRemoteQueryInt(q, "v=", 0);
			MpRemoteSendCmd(17, (LPARAM)((w << 16) | (v & 0xFFFF)));
		}
		else if (MpRemoteHasQueryParam(q, "c=eqreset")) MpRemoteSendCmd(18);
		else if (MpRemoteHasQueryParam(q, "c=eqresetg")) MpRemoteSendCmd(19);
		else if (MpRemoteHasQueryParam(q, "c=scrbeg")) MpRemoteSendCmd(23);
		else if (MpRemoteHasQueryParam(q, "c=scrend")) MpRemoteSendCmd(24);
		else if (MpRemoteHasQueryParam(q, "c=scr") && q) {
			// d= は角度差*100（符号付き）。例: 1.5° → 150
			MpRemoteSendCmd(25, (LPARAM)MpRemoteQueryInt(q, "d=", 0));
		}
		const char* ok = "HTTP/1.0 204 No Content\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n";
		MpRemoteSendAll(s, ok, (int)strlen(ok));
		return;
	}

	CStringW title, artist, album;
	int vol = 50;
	int muted = 0;
	const wchar_t* state = L"stop";
	MpRemoteReadMeta(title, artist, album, vol, state, muted);

	if (strstr(line, "GET /api/status")) {
		CStringW jt, ja, jb;
		MpRemoteEscJson(title, jt);
		MpRemoteEscJson(artist, ja);
		MpRemoteEscJson(album, jb);
		const int idx = (int)InterlockedCompareExchange(&g_mpRemotePlayIdx, 0, 0);
		const int pcnt = (int)InterlockedCompareExchange(&g_mpRemotePlayCnt, 0, 0);
		const int posCs = (int)InterlockedCompareExchange(&g_mpRemotePosCs, 0, 0);
		const int durCs = (int)InterlockedCompareExchange(&g_mpRemoteDurCs, 0, 0);
		const int lrccur = (int)InterlockedCompareExchange(&g_mpRemoteLrcCur, 0, 0);
		const int aacLag = (int)InterlockedCompareExchange(&g_mpRemAacLagCs, 0, 0);
		CStringW json;
		CStringW keyLab;
		if (savedata.mpCamelot > 0)
			keyLab = MpCamelotLabel(savedata.mpCamelot);
		const int vidOn = (pMainFrame1 && ::IsWindow(pMainFrame1->GetSafeHwnd())
			&& ::IsWindowVisible(pMainFrame1->GetSafeHwnd())) ? 1 : 0;
		if (vidOn)
			MpRemVidWantPulse();
		json.Format(L"{\"title\":\"%s\",\"artist\":\"%s\",\"album\":\"%s\",\"vol\":%d,\"state\":\"%s\",\"muted\":%d,\"index\":%d,\"playcnt\":%d,\"pos_cs\":%d,\"dur_cs\":%d,\"lrccur\":%d,\"aac\":%d,\"aac_lag_cs\":%d,\"vid\":%d,\"key\":\"%s\"}",
			(LPCWSTR)jt, (LPCWSTR)ja, (LPCWSTR)jb, vol, state, muted, idx, pcnt, posCs, durCs, lrccur,
			savedata.mpRemoteAac ? 1 : 0, aacLag, vidOn, (LPCWSTR)keyLab);
		CStringA utf8;
		{
			const int nbytes = ::WideCharToMultiByte(CP_UTF8, 0, json, -1, NULL, 0, NULL, NULL);
			if (nbytes > 1) {
				char* pb = utf8.GetBufferSetLength(nbytes - 1);
				::WideCharToMultiByte(CP_UTF8, 0, json, -1, pb, nbytes, NULL, NULL);
				utf8.ReleaseBuffer(nbytes - 1);
			}
		}
		CStringA hdr;
		hdr.Format("HTTP/1.0 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nConnection: close\r\nCache-Control: no-store\r\nContent-Length: %d\r\n\r\n",
			utf8.GetLength());
		MpRemoteSendAll(s, hdr, hdr.GetLength());
		MpRemoteSendAll(s, utf8, utf8.GetLength());
		return;
	}

	if (strstr(line, "GET /api/playlist")) {
		const char* q = strchr(line, '?');
		int off = MpRemoteQueryInt(q, "o=", 0);
		int n = MpRemoteQueryInt(q, "n=", 40);
		if (off < 0) off = 0;
		if (n < 1) n = 1;
		if (n > 40) n = 40;
		int total = 0;
		if (pl && pl->pc && pl->playcnt > 0) total = pl->playcnt;
		if (off > total) off = total;
		CStringW json = L"{\"total\":";
		{
			wchar_t b[32];
			_snwprintf_s(b, _TRUNCATE, L"%d,\"items\":[", total);
			json += b;
		}
		int sent = 0;
		for (int i = off; i < total && sent < n; ++i, ++sent) {
			if (sent) json += L",";
			CStringW jt, ja, jb;
			const playlistdata0& row = pl->pc[i];
			MpRemoteEscJson(row.name[0] ? row.name : L"", jt);
			MpRemoteEscJson(row.art[0] ? row.art : L"", ja);
			MpRemoteEscJson(row.alb[0] ? row.alb : L"", jb);
			CStringW one;
			one.Format(L"{\"i\":%d,\"title\":\"%s\",\"artist\":\"%s\",\"album\":\"%s\"}",
				i, (LPCWSTR)jt, (LPCWSTR)ja, (LPCWSTR)jb);
			json += one;
		}
		json += L"]}";
		CStringA utf8;
		{
			const int nbytes = ::WideCharToMultiByte(CP_UTF8, 0, json, -1, NULL, 0, NULL, NULL);
			if (nbytes > 1) {
				char* pb = utf8.GetBufferSetLength(nbytes - 1);
				::WideCharToMultiByte(CP_UTF8, 0, json, -1, pb, nbytes, NULL, NULL);
				utf8.ReleaseBuffer(nbytes - 1);
			}
		}
		CStringA hdr;
		hdr.Format("HTTP/1.0 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nConnection: close\r\nCache-Control: no-store\r\nContent-Length: %d\r\n\r\n",
			utf8.GetLength());
		MpRemoteSendAll(s, hdr, hdr.GetLength());
		MpRemoteSendAll(s, utf8, utf8.GetLength());
		return;
	}

	if (strstr(line, "GET /api/lyrics")) {
		CStringW json = L"{\"n\":0,\"cur\":-1,\"lines\":[";
		int n = 0;
		int cur = (int)InterlockedCompareExchange(&g_mpRemoteLrcCur, 0, 0);
		if (og && og->lrcnum > 0 && og->lrcnum <= 300) {
			n = og->lrcnum;
			json.Format(L"{\"n\":%d,\"cur\":%d,\"lines\":[", n, cur);
			for (int i = 0; i < n; ++i) {
				if (i) json += L",";
				CStringW jt;
				MpRemoteEscJson(og->lrc[i], jt);
				CStringW one;
				one.Format(L"{\"t\":%u,\"x\":\"%s\"}", (unsigned)og->lrctm[i], (LPCWSTR)jt);
				json += one;
			}
		}
		json += L"]}";
		CStringA utf8;
		{
			const int nbytes = ::WideCharToMultiByte(CP_UTF8, 0, json, -1, NULL, 0, NULL, NULL);
			if (nbytes > 1) {
				char* pb = utf8.GetBufferSetLength(nbytes - 1);
				::WideCharToMultiByte(CP_UTF8, 0, json, -1, pb, nbytes, NULL, NULL);
				utf8.ReleaseBuffer(nbytes - 1);
			}
		}
		CStringA hdr;
		hdr.Format("HTTP/1.0 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nConnection: close\r\nCache-Control: no-store\r\nContent-Length: %d\r\n\r\n",
			utf8.GetLength());
		MpRemoteSendAll(s, hdr, hdr.GetLength());
		MpRemoteSendAll(s, utf8, utf8.GetLength());
		return;
	}

	if (strstr(line, "GET /api/eq")) {
		CStringW json = L"{\"eq\":[";
		for (int i = 0; i < 20; ++i) {
			int v = savedata.eq[i];
			if (v < 0) v = 0;
			if (v > 200) v = 200;
			wchar_t b[16];
			_snwprintf_s(b, _TRUNCATE, L"%s%d", (i ? L"," : L""), v);
			json += b;
		}
		{
			wchar_t b[128];
			_snwprintf_s(b, _TRUNCATE, L"],\"pre\":%d,\"env\":%d,\"eff\":%d,\"rev\":%d,\"cho\":%d,\"del\":%d}",
				savedata.eqsoundeq, savedata.eqsoundenv, savedata.eqsoundeffect * 2,
				savedata.eq_reverb, savedata.eq_chorus, savedata.eq_delay);
			json += b;
		}
		CStringA utf8;
		{
			const int nbytes = ::WideCharToMultiByte(CP_UTF8, 0, json, -1, NULL, 0, NULL, NULL);
			if (nbytes > 1) {
				char* pb = utf8.GetBufferSetLength(nbytes - 1);
				::WideCharToMultiByte(CP_UTF8, 0, json, -1, pb, nbytes, NULL, NULL);
				utf8.ReleaseBuffer(nbytes - 1);
			}
		}
		CStringA hdr;
		hdr.Format("HTTP/1.0 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nConnection: close\r\nCache-Control: no-store\r\nContent-Length: %d\r\n\r\n",
			utf8.GetLength());
		MpRemoteSendAll(s, hdr, hdr.GetLength());
		MpRemoteSendAll(s, utf8, utf8.GetLength());
		return;
	}

	if (strstr(line, "GET /api/dj")) {
		const int head100 = (int)InterlockedCompareExchange(&g_mpRemoteHeadDeg100, 0, 0);
		const int playing = (plf == 1 && ps != 1) ? 1 : 0;
		CStringW json;
		json.Format(L"{\"head\":%.2f,\"playing\":%d}", (double)head100 / 100.0, playing);
		CStringA utf8;
		{
			const int nbytes = ::WideCharToMultiByte(CP_UTF8, 0, json, -1, NULL, 0, NULL, NULL);
			if (nbytes > 1) {
				char* pb = utf8.GetBufferSetLength(nbytes - 1);
				::WideCharToMultiByte(CP_UTF8, 0, json, -1, pb, nbytes, NULL, NULL);
				utf8.ReleaseBuffer(nbytes - 1);
			}
		}
		CStringA hdr;
		hdr.Format("HTTP/1.0 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nConnection: close\r\nCache-Control: no-store\r\nContent-Length: %d\r\n\r\n",
			utf8.GetLength());
		MpRemoteSendAll(s, hdr, hdr.GetLength());
		MpRemoteSendAll(s, utf8, utf8.GetLength());
		return;
	}

	if (strstr(line, "GET /api/analyzer")) {
		InterlockedExchange(&g_mpRemoteWantAnaMs, (LONG)GetTickCount());
		const int nch = (g_mpRemoteSpecCh < 1) ? 1 : ((g_mpRemoteSpecCh > 8) ? 8 : g_mpRemoteSpecCh);
		CStringW json;
		json.Format(L"{\"n\":%d,\"b\":[", nch);
		for (int c = 0; c < nch; ++c) {
			if (c) json += L",";
			json += L"[";
			for (int i = 0; i < 64; ++i) {
				wchar_t b[16];
				_snwprintf_s(b, _TRUNCATE, L"%s%d", (i ? L"," : L""), (int)g_mpRemoteSpec[c][i]);
				json += b;
			}
			json += L"]";
		}
		json += L"],\"lab\":[";
		static const wchar_t* stereo[] = { L"L", L"R" };
		static const wchar_t* ch51[] = { L"L", L"R", L"C", L"LFE", L"SL", L"SR" };
		static const wchar_t* ch71[] = { L"L", L"R", L"C", L"LFE", L"SL", L"SR", L"BL", L"BR" };
		for (int c = 0; c < nch; ++c) {
			if (c) json += L",";
			json += L"\"";
			if (nch == 2 && c < 2) json += stereo[c];
			else if (nch == 6 && c < 6) json += ch51[c];
			else if (nch >= 8 && c < 8) json += ch71[c];
			else {
				wchar_t lb[16];
				_snwprintf_s(lb, _TRUNCATE, L"Ch%d", c + 1);
				json += lb;
			}
			json += L"\"";
		}
		json += L"]}";
		CStringA utf8;
		{
			const int nbytes = ::WideCharToMultiByte(CP_UTF8, 0, json, -1, NULL, 0, NULL, NULL);
			if (nbytes > 1) {
				char* pb = utf8.GetBufferSetLength(nbytes - 1);
				::WideCharToMultiByte(CP_UTF8, 0, json, -1, pb, nbytes, NULL, NULL);
				utf8.ReleaseBuffer(nbytes - 1);
			}
		}
		CStringA hdr;
		hdr.Format("HTTP/1.0 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nConnection: close\r\nCache-Control: no-store\r\nContent-Length: %d\r\n\r\n",
			utf8.GetLength());
		MpRemoteSendAll(s, hdr, hdr.GetLength());
		MpRemoteSendAll(s, utf8, utf8.GetLength());
		return;
	}

	if (strstr(line, "GET /api/piano")) {
		InterlockedExchange(&g_mpRemoteWantPianoMs, (LONG)GetTickCount());
		CStringW chordEsc;
		MpRemoteEscJson(g_mpRemoteChord, chordEsc);
		CStringW json = L"{\"c\":\"";
		json += chordEsc;
		json += L"\",\"xm\":";
		{
			wchar_t b[16];
			_snwprintf_s(b, _TRUNCATE, L"%d,\"k\":[", g_mpRemoteExprOn ? 1 : 0);
			json += b;
		}
		for (int i = 0; i < 108; ++i) {
			wchar_t b[16];
			_snwprintf_s(b, _TRUNCATE, L"%s%d", (i ? L"," : L""), (int)g_mpRemoteNotes[i]);
			json += b;
		}
		json += L"],\"h\":[";
		const int rows = g_mpRemoteHistRows;
		const int xm = g_mpRemoteExprOn ? 1 : 0;
		for (int r = 0; r < rows; ++r) {
			if (r) json += L",";
			json += L"[";
			int first = 1;
			for (int k = 0; k < 108; ++k) {
				if (g_mpRemoteHist[r][k >> 3] & (BYTE)(1u << (k & 7))) {
					wchar_t b[16];
					_snwprintf_s(b, _TRUNCATE, L"%s%d", (first ? L"" : L","), k);
					json += b;
					first = 0;
				}
			}
			json += L"]";
		}
		json += L"]";
		if (xm) {
			json += L",\"x\":[";
			for (int r = 0; r < rows; ++r) {
				if (r) json += L",";
				json += L"[";
				int first = 1;
				for (int k = 0; k < 108; ++k) {
					if (g_mpRemoteHist[r][k >> 3] & (BYTE)(1u << (k & 7))) {
						wchar_t b[16];
						_snwprintf_s(b, _TRUNCATE, L"%s%d", (first ? L"" : L","), (int)g_mpRemoteHistExpr[r][k]);
						json += b;
						first = 0;
					}
				}
				json += L"]";
			}
			json += L"],\"kx\":[";
			for (int i = 0; i < 108; ++i) {
				wchar_t b[16];
				_snwprintf_s(b, _TRUNCATE, L"%s%d", (i ? L"," : L""), (int)g_mpRemoteNoteExpr[i]);
				json += b;
			}
			json += L"]";
		}
		json += L"}";
		CStringA utf8;
		{
			const int nbytes = ::WideCharToMultiByte(CP_UTF8, 0, json, -1, NULL, 0, NULL, NULL);
			if (nbytes > 1) {
				char* pb = utf8.GetBufferSetLength(nbytes - 1);
				::WideCharToMultiByte(CP_UTF8, 0, json, -1, pb, nbytes, NULL, NULL);
				utf8.ReleaseBuffer(nbytes - 1);
			}
		}
		CStringA hdr;
		hdr.Format("HTTP/1.0 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nConnection: close\r\nCache-Control: no-store\r\nContent-Length: %d\r\n\r\n",
			utf8.GetLength());
		MpRemoteSendAll(s, hdr, hdr.GetLength());
		MpRemoteSendAll(s, utf8, utf8.GetLength());
		return;
	}

	CStringW ht, ha, hb;
	MpRemoteEscHtml(title.IsEmpty() ? L"—" : (LPCWSTR)title, ht);
	MpRemoteEscHtml(artist.IsEmpty() ? L"" : (LPCWSTR)artist, ha);
	MpRemoteEscHtml(album.IsEmpty() ? L"" : (LPCWSTR)album, hb);

	const wchar_t* brand = LL14(L"ローカルリモート", L"MP Remote", L"Telecommande", L"Remote MP", L"Remoto MP",
		L"로컬 리모트", L"本地遥控", L"تحكم محلي", L"Локальный пульт", L"Lokalfernbedienung",
		L"Remoto local", L"Lokale bediening", L"Pilot lokalny", L"Yerel uzaktan");
	const wchar_t* labPlay = LL14(L"再生", L"Play", L"Lecture", L"Play", L"Play", L"재생", L"播放", L"تشغيل", L"Играть", L"Play", L"Play", L"Play", L"Odtwarzaj", L"Oynat");
	const wchar_t* labPause = LL14(L"一時停止", L"Pause", L"Pause", L"Pausa", L"Pausa", L"일시정지", L"暂停", L"إيقاف", L"Пауза", L"Pause", L"Pausar", L"Pauze", L"Pauza", L"Duraklat");
	const wchar_t* labStop = LL14(L"停止", L"Stop", L"Stop", L"Stop", L"Stop", L"정지", L"停止", L"إيقاف", L"Стоп", L"Stop", L"Parar", L"Stop", L"Stop", L"Durdur");
	const wchar_t* labPrev = LL14(L"前へ", L"Prev", L"Prec.", L"Prec.", L"Ant.", L"이전", L"上一首", L"السابق", L"Пред.", L"Zurück", L"Ant.", L"Vorige", L"Poprzedni", L"Onceki");
	const wchar_t* labNext = LL14(L"次へ", L"Next", L"Suivant", L"Successivo", L"Siguiente", L"다음", L"下一首", L"التالي", L"След.", L"Weiter", L"Proximo", L"Volgende", L"Nastepny", L"Sonraki");
	const wchar_t* labSeekBk = LL14(L"戻る 5%", L"Back 5%", L"-5%", L"-5%", L"-5%", L"뒤로 5%", L"后退 5%", L"-5%", L"-5%", L"-5%", L"-5%", L"-5%", L"-5%", L"-5%");
	const wchar_t* labSeekFw = LL14(L"進む 5%", L"Fwd 5%", L"+5%", L"+5%", L"+5%", L"앞으로 5%", L"前进 5%", L"+5%", L"+5%", L"+5%", L"+5%", L"+5%", L"+5%", L"+5%");
	const wchar_t* labVol = LL14(L"音量", L"Volume", L"Volume", L"Volume", L"Volumen", L"볼륨", L"音量", L"الصوت", L"Громкость", L"Lautstärke", L"Volume", L"Volume", L"Glosnosc", L"Ses");
	const wchar_t* labMute = LL14(L"ミュート", L"Mute", L"Muet", L"Mute", L"Silencio", L"음소거", L"静音", L"كتم", L"Мьют", L"Stumm", L"Mudo", L"Dempen", L"Wycisz", L"Sessiz");
	const wchar_t* labNow = LL14(L"再生中", L"Now playing", L"En lecture", L"In riproduzione", L"Reproduciendo", L"재생 중", L"正在播放", L"قيد التشغيل", L"Сейчас играет", L"Wird gespielt", L"Tocando", L"Nu spelen", L"Odtwarzanie", L"Caliniyor");
	const wchar_t* labTabPlay = LL14(L"操作", L"Play", L"Lecture", L"Play", L"Play", L"조작", L"操作", L"تشغيل", L"Управ.", L"Steuern", L"Controlo", L"Bedien", L"Steruj", L"Kontrol");
	const wchar_t* labTabEq = LL14(L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ");
	const wchar_t* labTabList = LL14(L"リスト", L"List", L"Liste", L"Lista", L"Lista", L"목록", L"列表", L"قائمة", L"Список", L"Liste", L"Lista", L"Lijst", L"Lista", L"Liste");
	const wchar_t* labTabLrc = LL14(L"歌詞", L"Lyrics", L"Paroles", L"Testi", L"Letra", L"가사", L"歌词", L"كلمات", L"Текст", L"Text", L"Letra", L"Tekst", L"Tekst", L"Soz");
	const wchar_t* labTabDj = LL14(L"DJ", L"DJ", L"DJ", L"DJ", L"DJ", L"DJ", L"DJ", L"DJ", L"DJ", L"DJ", L"DJ", L"DJ", L"DJ", L"DJ");
	const wchar_t* labTabPiano = LL14(L"ピアノ", L"Piano", L"Piano", L"Piano", L"Piano", L"피아노", L"钢琴", L"بيانو", L"Пиано", L"Piano", L"Piano", L"Piano", L"Piano", L"Piyano");
	const wchar_t* labTabAna = LL14(L"アナ", L"Ana", L"Ana", L"Ana", L"Ana", L"아나", L"分析", L"محلل", L"Ана", L"Ana", L"Ana", L"Ana", L"Ana", L"Ana");
	const wchar_t* labHint = LL14(L"Wi-Fi / LAN · 同時最大6台 · AACで聴ける", L"Wi-Fi / LAN · up to 6 · AAC listen", L"Wi-Fi / LAN · max 6 · ecoute AAC", L"Wi-Fi / LAN · max 6 · ascolto AAC", L"Wi-Fi / LAN · max. 6 · escuchar AAC",
		L"Wi-Fi / LAN · 최대 6 · AAC 청취", L"Wi-Fi / LAN · 最多6 · 可听AAC", L"Wi-Fi / LAN · حد 6 · استماع AAC", L"Wi-Fi / LAN · до 6 · AAC", L"WLAN / LAN · max. 6 · AAC hören",
		L"Wi-Fi / LAN · max. 6 · ouvir AAC", L"Wi-Fi / LAN · max 6 · AAC luisteren", L"Wi-Fi / LAN · max 6 · sluchaj AAC", L"Wi-Fi / LAN · en fazla 6 · AAC dinle");
	const wchar_t* labListen = LL14(L"聴く (AAC)", L"Listen (AAC)", L"Ecouter (AAC)", L"Ascolta (AAC)", L"Escuchar (AAC)",
		L"듣기 (AAC)", L"收听 (AAC)", L"استماع (AAC)", L"Слушать (AAC)", L"Hören (AAC)",
		L"Ouvir (AAC)", L"Luisteren (AAC)", L"Sluchaj (AAC)", L"Dinle (AAC)");
	const wchar_t* labPre = LL14(L"プリセット", L"Preset", L"Preset", L"Preset", L"Preajuste", L"프리셋", L"预设", L"إعداد مسبق", L"Пресет", L"Preset", L"Preset", L"Preset", L"Preset", L"Onayar");
	const wchar_t* labEnv = LL14(L"環境", L"Environment", L"Environnement", L"Ambiente", L"Entorno", L"환경", L"环境", L"بيئة", L"Среда", L"Umgebung", L"Ambiente", L"Omgeving", L"Srodowisko", L"Ortam");
	const wchar_t* labRev = LL14(L"リバーブ", L"Reverb", L"Reverb", L"Riverbero", L"Reverb", L"리버브", L"混响", L"صدى", L"Реверб", L"Hall", L"Reverb", L"Galm", L"Poglos", L"Reverb");
	const wchar_t* labCho = LL14(L"コーラス", L"Chorus", L"Chorus", L"Chorus", L"Chorus", L"코러스", L"合唱", L"جوقة", L"Хорус", L"Chorus", L"Chorus", L"Chorus", L"Chorus", L"Kor");
	const wchar_t* labDel = LL14(L"ディレイ", L"Delay", L"Delay", L"Delay", L"Delay", L"딜레이", L"延迟", L"تأخير", L"Дилей", L"Delay", L"Delay", L"Delay", L"Delay", L"Gecikme");
	const wchar_t* labEff = LL14(L"効果量", L"Effect", L"Effet", L"Effetto", L"Efecto", L"효과", L"效果", L"تأثير", L"Эффект", L"Effekt", L"Efeito", L"Effect", L"Efekt", L"Efekt");
	const wchar_t* labEqReset = LL14(L"イコライザーリセット", L"EQ reset", L"Reset EQ", L"Reset EQ", L"Reset EQ", L"EQ 초기화", L"均衡器重置", L"إعادة EQ", L"Сброс EQ", L"EQ-Reset", L"Reset EQ", L"EQ reset", L"Reset EQ", L"EQ sifirla");
	const wchar_t* labEqResetG = LL14(L"グローバルリセット", L"Global reset", L"Reset global", L"Reset globale", L"Reset global", L"전역 초기화", L"全局重置", L"إعادة عامة", L"Глоб. сброс", L"Global-Reset", L"Reset global", L"Globaal reset", L"Reset globalny", L"Genel sifirla");
	const wchar_t* labLrcSave = LL14(L"LRC保存", L"Save LRC", L"Sauver LRC", L"Salva LRC", L"Guardar LRC", L"LRC 저장", L"保存LRC", L"حفظ LRC", L"Сохранить LRC", L"LRC speichern", L"Salvar LRC", L"LRC opslaan", L"Zapisz LRC", L"LRC kaydet");
	const wchar_t* labScratch = LL14(L"ドラッグでスクラッチ", L"Drag to scratch", L"Glisser pour scratch", L"Trascina per scratch", L"Arrastrar para scratch",
		L"드래그로 스크래치", L"拖动刮盘", L"اسحب للخدش", L"Тяните для скретча", L"Ziehen zum Scratchen", L"Arrastar para scratch", L"Slepen om te scratchen", L"Przeciagnij aby scratch", L"Surukle scratch");
	const wchar_t* tipLrcM100 = LL14(L"歌詞を -100ms", L"Lyrics -100ms", L"Paroles -100ms", L"Testi -100ms", L"Letra -100ms", L"가사 -100ms", L"歌词 -100ms", L"كلمات -100ms", L"Текст -100ms", L"Text -100ms", L"Letra -100ms", L"Tekst -100ms", L"Tekst -100ms", L"Soz -100ms");
	const wchar_t* tipLrcP100 = LL14(L"歌詞を +100ms", L"Lyrics +100ms", L"Paroles +100ms", L"Testi +100ms", L"Letra +100ms", L"가사 +100ms", L"歌词 +100ms", L"كلمات +100ms", L"Текст +100ms", L"Text +100ms", L"Letra +100ms", L"Tekst +100ms", L"Tekst +100ms", L"Soz +100ms");

	CStringW page;
	page = L"<!DOCTYPE html><html lang=\"ja\"><head><meta charset=\"utf-8\">"
		L"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,viewport-fit=cover\">"
		L"<meta name=\"apple-mobile-web-app-capable\" content=\"yes\">"
		L"<meta name=\"theme-color\" content=\"#ff9ec8\">"
		L"<title>";
	page += brand;
	page += L"</title>"
		L"<link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.5.2/css/all.min.css\">"
		L"<style>";
	page += L":root{--bg1:#fff5fb;--bg2:#eef3ff;--card:#fffffff2;--ink:#3a2a3a;--muted:#8a6a80;--pink:#ff69b4;--pink2:#c45ad0;--play1:#c8f0c8;--play2:#8cd296;--pause1:#fff0c8;--pause2:#ffd28c;--stop1:#ffd7dc;--stop2:#ffaab9;--nav1:#d7ebff;--nav2:#a5cdf5;--shadow:0 12px 40px #ff69b433}*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}html,body{margin:0;min-height:100%;font-family:\"Segoe UI\",\"Yu Gothic UI\",\"Meiryo\",sans-serif;color:var(--ink);background:radial-gradient(1200px 600px at 10% -10%,#ffd6ec 0%,transparent 55%),radial-gradient(900px 500px at 100% 0%,#d6e6ff 0%,transparent 50%),linear-gradient(160deg,var(--bg1),var(--bg2));}body{padding:18px 16px 28px}.shell{max-width:560px;margin:0 a";
	page += L"uto}.brand{display:flex;align-items:center;gap:10px;margin-bottom:14px}.brand i{width:42px;height:42px;border-radius:14px;display:grid;place-items:center;background:linear-gradient(135deg,var(--pink),var(--pink2));color:#fff;box-shadow:var(--shadow);font-size:18px}.brand h1{margin:0;font-size:1.15rem;background:linear-gradient(90deg,var(--pink),var(--pink2));-webkit-background-clip:text;background-clip:text;color:transparent;font-weight:800}.card{background:var(--card);backdrop-filter:blur(14px);border:1px solid #ffffffaa;border-radius:22px;padding:18px 16px;box-shadow:var(--shadow);margin-bottom:14px}.now-label{font-size:.72rem;letter-spacing:.08em;text-transform:uppercase;color:var(--muted";
	page += L");margin:0 0 6px}#title{margin:0;font-size:1.25rem;font-weight:750;line-height:1.35;word-break:break-word;background:linear-gradient(90deg,#ff69b4,#963ca0);-webkit-background-clip:text;background-clip:text;color:transparent}#artist,#album{margin:6px 0 0;color:var(--muted);font-size:.95rem;word-break:break-word}#album{font-size:.85rem;opacity:.9}.state{display:inline-flex;align-items:center;gap:6px;margin-top:10px;padding:4px 10px;border-radius:999px;background:#ffe6f3;color:#b03070;font-size:.75rem;font-weight:700}.state.play{background:#e4ffe8;color:#2d7a3e}.state.pause{background:#fff3d6;color:#9a6a10}.tabs{display:flex;flex-direction:column;gap:6px;margin:0 0 12px;padding:4px;background:#ffffffcc;";
	page += L"border-radius:16px;border:1px solid #ffffffaa;box-shadow:var(--shadow)}.tabrow{display:grid;gap:6px}.tabrow.r4{grid-template-columns:repeat(4,1fr)}.tabrow.r3{grid-template-columns:repeat(3,1fr)}.tab{appearance:none;border:0;cursor:pointer;border-radius:12px;padding:10px 6px;font-weight:750;font-size:.76rem;color:#6a4a60;background:transparent}.tab.on{background:linear-gradient(135deg,var(--pink),var(--pink2));color:#fff;box-shadow:0 4px 14px #ff69b455}.panel{display:none}.panel.on{display:block}.vinyl-wrap{display:flex;flex-direction:column;align-items:center;gap:10px;margin-top:4px}#vinyl{width:min(100%,320px);aspect-ratio:1;border-radius:50%;touch-action:none;cursor:grab;display:block;box-shadow:0 10px 28px #00000033,inset 0 0 0 2px #ffffff22}#vinyl:active{cursor:grabbing}.vinyl-tip{font-size:.82rem;color:var(--muted);font-weight:650;text-align:center}.pad{display:grid;grid-template-columns:1fr 1.15fr 1fr;gap:10px;margin-top:4px}.btn{appearance:none;border:0;cursor:pointer;user-select:none;border-radius:18px;min-height:64px;padding:12px 8px;font-wei";
	page += L"ght:750;font-size:.92rem;color:#2a2030;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:6px;box-shadow:0 6px 16px #00000014;transition:transform .12s ease,filter .12s ease,box-shadow .12s ease}.btn i{font-size:1.25rem}.btn:active{transform:scale(.96);filter:brightness(.97)}.btn.busy{opacity:.65;pointer-events:none}.btn.sm{min-height:44px;border-radius:14px;font-size:.8rem;flex-direction:row;gap:8px}.b-prev,.b-next{background:linear-gradient(180deg,var(--nav1),var(--nav2))}.b-play{background:linear-gradient(180deg,var(--play1),var(--play2));min-height:76px;font-size:1rem}.b-pause{background:linear-gradient(180deg,var(--pause1),var(--pause2))}.b-stop{background:";
	page += L"linear-gradient(180deg,var(--stop1),var(--stop2))}.row2{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:10px}.row3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px;margin-top:10px}.row4{display:grid;grid-template-columns:1fr 1fr 1fr 1fr;gap:8px;margin-top:10px}.b-seek{background:linear-gradient(180deg,#efe7ff,#d5c8f8);min-height:54px}.b-mute{background:linear-gradient(180deg,#ffe8f1,#ffc1d8);min-height:48px;width:100%;margin-top:10px}.b-mute.on{background:linear-gradient(180deg,#ff9eb8,#ff5a8a);color:#fff;box-shadow:0 0 0 2px #ff69b466}.b-listen{background:linear-gradient(180deg,#e8fff4,#a8e8c8);min-height:48px;width:100%;margin-top:10px}.b-listen.on{background:linear-gradient(180deg,#7ad9a0,#3cb878);color:#fff;box-shadow:0 0 0 2px #3cb87866}.b-listen:disabled{opacity:.45}.b-soft{background:linear-gradient(180deg,#f5f0ff,#e2d6f8);min-height:48px}.b-kill.on{background:linear-gradient(180deg,#ff9eb8,#ff5a8a);";
	page += L"color:#fff}.vol-wrap{margin-top:8px}.vol-top{display:flex;justify-content:space-between;align-items:center;margin-bottom:8px}.vol-top span{font-weight:700;font-size:.9rem}#volVal,.vnum{color:var(--pink);font-variant-numeric:tabular-nums}input[type=range]{width:100%;accent-color:var(--pink);height:28px}#tab-eq{padding-bottom:20px}#tab-eq input[type=range]{touch-action:manipulation}.eq-reset{position:sticky;top:0;z-index:2;background:linear-gradient(180deg,#fffffff8,#fffffff0);padding:8px 0 10px;margin:4px 0 8px}.eq-grid{display:flex;flex-direction:column;gap:6px;margin-top:10px}.eq-band{display:grid;grid-template-columns:42px 1fr 36px;align-items:center;gap:8px}.eq-band label{font-size:.75rem;color:var(--muted);font-weight:700;text-align:right}.eq-band input{width:100%;height:28px;writing-mode:horizontal-tb;-webkit-appearance:auto;appearance:auto}.viz-wrap{margin-top:6px}.viz-chord{text-align:center;font-weight:800;font-size:1.05rem;color:var(--pink2);margin:0 0 8px;min-height:1.4em}#pianoCan,#anaCan{width:100%;height:auto;display:block;border-radius:14px;background:#0e1018;box-shadow:inset 0 0 0 1px #ffffff18}#pianoCan{min-height:220px}#anaCan{min-height:140px}select.sel{width:100%;margin-top:8px;padding:10px;border-radius:12px;border:1px ";
	page += L"solid #e8d0e0;background:#fff;font-weight:650;color:var(--ink)}.list{max-height:360px;overflow:auto;margin-top:8px;-webkit-overflow-scrolling:touch}.li{display:block;width:100%;text-align:left;padding:12px 12px;border:0;border-radius:14px;background:transparent;cursor:pointer;margin-bottom:4px}.li:active{background:#ffe6f3}.li.cur{background:linear-gradient(90deg,#ffe6f3,#f0e6ff);font-weight:750}.li .t{display:block;font-size:.95rem}.li .m{display:block;font-size:.78rem;color:var(--muted);margin-top:2px}.pager{display:flex;justify-content:space-between;align-items:center;gap:8px;margin-top:10px}.lrc{max-height:min(52vh,380px);min-height:220px;overflow:auto;margin:0 0 12px;line-height:1.55;padding:8px 0 28%}.lrc .ln{padding:6px 8px;bord";
	page += L"er-radius:10px;color:var(--muted);font-size:.92rem}.lrc .ln.cur{background:#ffe6f3;color:#3a2a3a;font-weight:750}.sec-lab{font-size:.78rem;font-weight:750;color:var(--muted);margin:12px 0 4px;text-transform:uppercase;letter-spacing:.06em}.toast{position:fixed;left:50%;bottom:24px;transform:translateX(-50%) translateY(20px);opacity:0;background:#3a2a3add;color:#fff;padding:10px 16px;border-radius:999px;font-size:.85rem;pointer-events:none;transition:opacity .2s,transform .2s;z-index:9}.toast.show{opacity:1;transform:translateX(-50%) translateY(0)}.hint{text-align:center;color:var(--muted);font-size:.75rem;margin-top:12px}.vwrap{margin:10px 0 0;border-radius:12px;overflow:hidden;background:#111;aspect-ratio:16/9;touch-action:manipulation;-webkit-user-select:none;user-select:none}.vwrap img{display:block;width:100%;height:100%;object-fit:contain;background:#000;pointer-events:none}.vwrap.fs{position:fixed;inset:0;z-index:200;margin:0;border-radius:0;aspect-ratio:auto;width:100vw;height:100vh;max-height:none}.vwrap.fs img{width:100%;height:100%;object-fit:contain}body.vidfs{overflow:hidden;background:#000}body.vidfs .shell{padding:0;max-width:none}body.vidfs .brand,body.vidfs .tabs,body.vidfs .hint,body.vidfs .toast,body.vidfs .shell>.card:not(.now),body.vidfs .now-label,body.vidfs #title,body.vidfs #artist,body.vidfs #album,body.vidfs #state{display:none!important}body.vidfs .now{margin:0;padding:0;border:0;box-shadow:none;background:#000;border-radius:0}body.vidfs #vwrap{display:block!important}";
	page += L"</style></head><body><div class=\"shell\">"
		L"<div class=\"brand\"><i class=\"fa-solid fa-wifi\"></i><h1>";
	page += brand;
	page += L"</h1></div>"
		L"<section class=\"card now\">"
		L"<p class=\"now-label\">";
	page += labNow;
	page += L"</p><h2 id=\"title\">";
	page += ht;
	page += L"</h2><p id=\"artist\">";
	page += ha;
	page += L"</p><p id=\"album\">";
	page += hb;
	page += L"</p><div id=\"vwrap\" class=\"vwrap\" style=\"display:none\"><img id=\"vimg\" alt=\"\"/></div><div id=\"state\" class=\"state\">—</div></section>";

	page += L"<div class=\"tabs\" role=\"tablist\">"
		L"<div class=\"tabrow r4\">"
		L"<button type=\"button\" class=\"tab on\" data-tab=\"play\" title=\"";
	page += labTabPlay; page += L"\">"; page += labTabPlay;
	page += L"</button><button type=\"button\" class=\"tab\" data-tab=\"eq\" title=\"";
	page += labTabEq; page += L"\">"; page += labTabEq;
	page += L"</button><button type=\"button\" class=\"tab\" data-tab=\"list\" title=\"";
	page += labTabList; page += L"\">"; page += labTabList;
	page += L"</button><button type=\"button\" class=\"tab\" data-tab=\"lrc\" title=\"";
	page += labTabLrc; page += L"\">"; page += labTabLrc;
	page += L"</button></div><div class=\"tabrow r3\">";
	page += L"<button type=\"button\" class=\"tab tab-dj\" data-tab=\"dj\" title=\"";
	page += labTabDj; page += L"\">"; page += labTabDj;
	page += L"</button><button type=\"button\" class=\"tab\" data-tab=\"piano\" title=\"";
	page += labTabPiano; page += L"\">"; page += labTabPiano;
	page += L"</button><button type=\"button\" class=\"tab\" data-tab=\"ana\" title=\"";
	page += labTabAna; page += L"\">"; page += labTabAna;
	page += L"</button></div></div>";

	page += L"<section class=\"card panel on\" id=\"tab-play\">"
		L"<div class=\"pad\">"
		L"<button type=\"button\" class=\"btn b-prev\" data-cmd=\"prev\"><i class=\"fa-solid fa-backward-step\"></i><span>";
	page += labPrev;
	page += L"</span></button>"
		L"<button type=\"button\" class=\"btn b-play\" data-cmd=\"play\"><i class=\"fa-solid fa-play\"></i><span>";
	page += labPlay;
	page += L"</span></button>"
		L"<button type=\"button\" class=\"btn b-next\" data-cmd=\"next\"><i class=\"fa-solid fa-forward-step\"></i><span>";
	page += labNext;
	page += L"</span></button></div>"
		L"<div class=\"row2\">"
		L"<button type=\"button\" class=\"btn b-pause\" data-cmd=\"pause\"><i class=\"fa-solid fa-pause\"></i><span>";
	page += labPause;
	page += L"</span></button>"
		L"<button type=\"button\" class=\"btn b-stop\" data-cmd=\"stop\"><i class=\"fa-solid fa-stop\"></i><span>";
	page += labStop;
	page += L"</span></button></div>"
		L"<div class=\"row2\">"
		L"<button type=\"button\" class=\"btn b-seek\" data-cmd=\"seekbk\"><i class=\"fa-solid fa-rotate-left\"></i><span>";
	page += labSeekBk;
	page += L"</span></button>"
		L"<button type=\"button\" class=\"btn b-seek\" data-cmd=\"seekfw\"><i class=\"fa-solid fa-rotate-right\"></i><span>";
	page += labSeekFw;
	page += L"</span></button></div>"
		L"<button type=\"button\" class=\"btn b-mute\" data-cmd=\"mute\"><i class=\"fa-solid fa-volume-xmark\"></i><span>";
	page += labMute;
	page += L"</span></button>"
		L"<button type=\"button\" class=\"btn b-listen\" id=\"btnListen\"><i class=\"fa-solid fa-headphones\"></i><span>";
	page += labListen;
	page += L"</span></button>"
		L"<div class=\"vol-wrap\"><div class=\"vol-top\"><span>";
	page += labVol;
	page += L"</span><span id=\"volVal\">";
	{ wchar_t vb[16]; _snwprintf_s(vb, _TRUNCATE, L"%d", vol); page += vb; }
	page += L"</span></div><input id=\"vol\" type=\"range\" min=\"0\" max=\"100\" value=\"";
	{ wchar_t vb[16]; _snwprintf_s(vb, _TRUNCATE, L"%d", vol); page += vb; }
	page += L"\"></div></section>";

	page += L"<section class=\"card panel\" id=\"tab-eq\"><div class=\"sec-lab\">";
	page += labPre; page += L"</div><select id=\"eqPre\" class=\"sel\">";
	page += L"<option value=\"0\">Default</option><option value=\"1\">Bass Boost</option><option value=\"2\">Treble Boost</option><option value=\"3\">Vocal Enhance</option><option value=\"4\">Bass Cut</option><option value=\"5\">Treble Cut</option><option value=\"6\">Loudness</option><option value=\"7\">Classical</option><option value=\"8\">Rock</option><option value=\"9\">Custom</option><option value=\"10\">Jazz</option><option value=\"11\">Pop</option><option value=\"12\">EDM</option><option value=\"13\">Metal</option><option value=\"14\">Hip Hop</option><option value=\"15\">Acoustic</option><option value=\"16\">V-shape</option><option value=\"17\">Inverse V</option><option value=\"18\">Smile curve</option><option value=\"19\">Radio/Podcast</";
	page += L"option><option value=\"20\">Movie/Drama</option><option value=\"21\">Gaming</option><option value=\"22\">Live recording</option><option value=\"23\">Treble Boost 2</option><option value=\"24\">Bass Boost 2</option><option value=\"25\">For low volume</option><option value=\"26\">For headphones</option><option value=\"27\">Vocal remove</option><option value=\"28\">Subwoofer boost</option><option value=\"29\">Radio AM</option><option value=\"30\">Radio FM</option><option value=\"31\">TV audio</option><option value=\"32\">Phone voice</option><option value=\"33\">Vintage</option><option value=\"34\">Modern</option><option value=\"35\">Warm</option><option value=\"36\">Bright</option><option value=\"37\">Flat+</option><option value=";
	page += L"\"38\">Cinema</option><option value=\"39\">Karaoke</option><option value=\"40\">#40</option><option value=\"41\">#41</option><option value=\"42\">#42</option><option value=\"43\">#43</option><option value=\"44\">#44</option><option value=\"45\">#45</option><option value=\"46\">#46</option><option value=\"47\">#47</option><option value=\"48\">#48</option><option value=\"49\">#49</option><option value=\"50\">#50</option><option value=\"51\">#51</option><option value=\"52\">#52</option><option value=\"53\">#53</option><option value=\"54\">#54</option><option value=\"55\">#55</option><option value=\"56\">#56</option><option value=\"57\">#57</option><option value=\"58\">#58</option><option value=\"59\">#59</option><option value=\"60\">#60</o";
	page += L"ption><option value=\"61\">#61</option><option value=\"62\">#62</option><option value=\"63\">#63</option><option value=\"64\">#64</option><option value=\"65\">#65</option><option value=\"66\">#66</option><option value=\"67\">#67</option><option value=\"68\">#68</option><option value=\"69\">#69</option><option value=\"70\">#70</option><option value=\"71\">#71</option><option value=\"72\">#72</option><option value=\"73\">#73</option><option value=\"74\">#74</option><option value=\"75\">#75</option><option value=\"76\">#76</option><option value=\"77\">#77</option><option value=\"78\">#78</option><option value=\"79\">#79</option><option value=\"80\">#80</option><option value=\"81\">#81</option><option value=\"82\">#82</option><option valu";
	page += L"e=\"83\">#83</option><option value=\"84\">#84</option><option value=\"85\">#85</option><option value=\"86\">#86</option><option value=\"87\">#87</option><option value=\"88\">#88</option><option value=\"89\">#89</option><option value=\"90\">#90</option><option value=\"91\">#91</option><option value=\"92\">#92</option><option value=\"93\">#93</option><option value=\"94\">#94</option><option value=\"95\">#95</option><option value=\"96\">#96</option><option value=\"97\">#97</option><option value=\"98\">#98</option><option value=\"99\">#99</option><option value=\"100\">#100</option>";
	page += L"</select><div class=\"row2 eq-reset\">";
	page += L"<button type=\"button\" class=\"btn sm b-soft\" id=\"eqReset\"><i class=\"fa-solid fa-rotate-left\"></i><span>";
	page += labEqReset;
	page += L"</span></button>";
	page += L"<button type=\"button\" class=\"btn sm b-soft\" id=\"eqResetG\"><i class=\"fa-solid fa-broom\"></i><span>";
	page += labEqResetG;
	page += L"</span></button></div><div class=\"sec-lab\">"; page += labEnv; page += L"</div><select id=\"eqEnv\" class=\"sel\">";
	{
		int envNum = 0;
		for (int ei = 0; ei < MP_REMOTE_EQ_ENV_COUNT; ++ei) {
			const wchar_t* raw = MpRemoteEqEnvLabel(ei);
			const BOOL isSep = (raw && wcsstr(raw, L"--[[") != NULL);
			CStringW shown;
			if (isSep) {
				shown = raw;
			} else {
				wchar_t nb[192];
				_snwprintf_s(nb, _TRUNCATE, L"%03d.%s", envNum++, raw ? raw : L"?");
				shown = nb;
			}
			CStringW esc;
			MpRemoteEscHtml(shown, esc);
			CStringW opt;
			if (isSep)
				opt.Format(L"<option value=\"%d\" disabled>%s</option>", ei, (LPCWSTR)esc);
			else
				opt.Format(L"<option value=\"%d\">%s</option>", ei, (LPCWSTR)esc);
			page += opt;
		}
	}
	page += L"</select><div class=\"eq-grid\">";
	page += L"<div class=\"eq-band\"><label>25</label><input id=\"eq0\" class=\"eqb\" data-b=\"0\" type=\"range\" min=\"0\" max=\"200\" value=\"100\"><span class=\"vnum\" id=\"eqv0\">100</span></div><div class=\"eq-band\"><label>40</label><input id=\"eq1\" class=\"eqb\" data-b=\"1\" type=\"range\" min=\"0\" max=\"200\" value=\"100\"><span class=\"vnum\" id=\"eqv1\">100</span></div><div class=\"eq-band\"><label>63</label><input id=\"eq2\" class=\"eqb\" data-b=\"2\" type=\"range\" min=\"0\" max=\"200\" value=\"100\"><span class=\"vnum\" id=\"eqv2\">100</span></div><div class=\"eq-band\"><label>100</label><input id=\"eq3\" class=\"eqb\" data-b=\"3\" type=\"range\" min=\"0\" max=\"200\" value=\"100\"><span class=\"vnum\" id=\"eqv3\">100</span></div><div class=\"eq-band\"><label>160</label>";
	page += L"<input id=\"eq4\" class=\"eqb\" data-b=\"4\" type=\"range\" min=\"0\" max=\"200\" value=\"100\"><span class=\"vnum\" id=\"eqv4\">100</span></div><div class=\"eq-band\"><label>250</label><input id=\"eq5\" class=\"eqb\" data-b=\"5\" type=\"range\" min=\"0\" max=\"200\" value=\"100\"><span class=\"vnum\" id=\"eqv5\">100</span></div><div class=\"eq-band\"><label>400</label><input id=\"eq6\" class=\"eqb\" data-b=\"6\" type=\"range\" min=\"0\" max=\"200\" value=\"100\"><span class=\"vnum\" id=\"eqv6\">100</span></div><div class=\"eq-band\"><label>630</label><input id=\"eq7\" class=\"eqb\" data-b=\"7\" type=\"range\" min=\"0\" max=\"200\" value=\"100\"><span class=\"vnum\" id=\"eqv7\">100</span></div><div class=\"eq-band\"><label>1k</label><input id=\"eq8\" class=\"eqb\" data-b=\"8";
	page += L"\" type=\"range\" min=\"0\" max=\"200\" value=\"100\"><span class=\"vnum\" id=\"eqv8\">100</span></div><div class=\"eq-band\"><label>1.6k</label><input id=\"eq9\" class=\"eqb\" data-b=\"9\" type=\"range\" min=\"0\" max=\"200\" value=\"100\"><span class=\"vnum\" id=\"eqv9\">100</span></div><div class=\"eq-band\"><label>2.5k</label><input id=\"eq10\" class=\"eqb\" data-b=\"10\" type=\"range\" min=\"0\" max=\"200\" value=\"100\"><span class=\"vnum\" id=\"eqv10\">100</span></div><div class=\"eq-band\"><label>4k</label><input id=\"eq11\" class=\"eqb\" data-b=\"11\" type=\"range\" min=\"0\" max=\"200\" value=\"100\"><span class=\"vnum\" id=\"eqv11\">100</span></div><div class=\"eq-band\"><label>6.3k</label><input id=\"eq12\" class=\"eqb\" data-b=\"12\" type=\"range\" min=\"0\" max";
	page += L"=\"200\" value=\"100\"><span class=\"vnum\" id=\"eqv12\">100</span></div><div class=\"eq-band\"><label>10k</label><input id=\"eq13\" class=\"eqb\" data-b=\"13\" type=\"range\" min=\"0\" max=\"200\" value=\"100\"><span class=\"vnum\" id=\"eqv13\">100</span></div><div class=\"eq-band\"><label>16k</label><input id=\"eq14\" class=\"eqb\" data-b=\"14\" type=\"range\" min=\"0\" max=\"200\" value=\"100\"><span class=\"vnum\" id=\"eqv14\">100</span></div>";
	page += L"</div><div class=\"sec-lab\">"; page += labRev; page += L" <span class=\"vnum\" id=\"eqRevV\">0</span></div>";
	page += L"<input id=\"eqRev\" type=\"range\" min=\"0\" max=\"200\" value=\"0\">";
	page += L"<div class=\"sec-lab\">"; page += labCho; page += L" <span class=\"vnum\" id=\"eqChoV\">0</span></div>";
	page += L"<input id=\"eqCho\" type=\"range\" min=\"0\" max=\"200\" value=\"0\">";
	page += L"<div class=\"sec-lab\">"; page += labDel; page += L" <span class=\"vnum\" id=\"eqDelV\">0</span></div>";
	page += L"<input id=\"eqDel\" type=\"range\" min=\"0\" max=\"200\" value=\"0\">";
	page += L"<div class=\"sec-lab\">"; page += labEff; page += L" <span class=\"vnum\" id=\"eqEffV\">0</span></div>";
	page += L"<input id=\"eqEff\" type=\"range\" min=\"0\" max=\"200\" value=\"0\">";
	page += L"</section>";

	page += L"<section class=\"card panel\" id=\"tab-list\"><div id=\"plList\" class=\"list\"></div>";
	page += L"<div class=\"pager\"><button type=\"button\" class=\"btn sm b-soft\" id=\"plPrev\"><i class=\"fa-solid fa-chevron-left\"></i></button>";
	page += L"<span id=\"plInfo\" class=\"vnum\">—</span>";
	page += L"<button type=\"button\" class=\"btn sm b-soft\" id=\"plNext\"><i class=\"fa-solid fa-chevron-right\"></i></button></div></section>";

	page += L"<section class=\"card panel\" id=\"tab-lrc\"><div id=\"lrcBox\" class=\"lrc\"></div>";
	page += L"<div class=\"row3\">";
	page += L"<button type=\"button\" class=\"btn sm b-soft lrcbtn\" data-d=\"-100\" title=\""; page += tipLrcM100; page += L"\">-100</button>";
	page += L"<button type=\"button\" class=\"btn sm b-soft lrcbtn\" data-d=\"-50\">-50</button>";
	page += L"<button type=\"button\" class=\"btn sm b-soft lrcbtn\" data-d=\"-10\">-10</button></div>";
	page += L"<div class=\"row3\">";
	page += L"<button type=\"button\" class=\"btn sm b-soft lrcbtn\" data-d=\"10\">+10</button>";
	page += L"<button type=\"button\" class=\"btn sm b-soft lrcbtn\" data-d=\"50\">+50</button>";
	page += L"<button type=\"button\" class=\"btn sm b-soft lrcbtn\" data-d=\"100\" title=\""; page += tipLrcP100; page += L"\">+100</button></div>";
	page += L"<button type=\"button\" class=\"btn sm b-soft\" id=\"lrcSave\" style=\"width:100%;margin-top:10px\"><i class=\"fa-solid fa-floppy-disk\"></i><span>";
	page += labLrcSave;
	page += L"</span></button></section>";

	page += L"<section class=\"card panel\" id=\"tab-dj\"><div class=\"vinyl-wrap\">"
		L"<canvas id=\"vinyl\" width=\"640\" height=\"640\" title=\"";
	page += labScratch;
	page += L"\"></canvas><div class=\"vinyl-tip\">";
	page += labScratch;
	page += L"</div></div></section>";

	page += L"<section class=\"card panel\" id=\"tab-piano\"><div class=\"viz-wrap\"><div class=\"viz-chord\" id=\"pianoChord\">—</div><canvas id=\"pianoCan\" width=\"720\" height=\"360\"></canvas></div></section>";
	page += L"<section class=\"card panel\" id=\"tab-ana\"><div class=\"viz-wrap\"><canvas id=\"anaCan\" width=\"720\" height=\"220\"></canvas></div></section>";

	page += L"<p class=\"hint\">"; page += labHint; page += L"</p></div><div id=\"toast\" class=\"toast\"></div>";
	page += L"<script src=\"https://code.jquery.com/jquery-3.7.1.min.js\"></script><script>";
	page += L"var _st={title:'',artist:'',album:'',vol:-1,state:'',muted:-1,index:-1,lrccur:-1,aac:-1,vid:-1,pos_cs:0,aac_lag_cs:90};var _tab='play',_plOff=0,_plPage=40,_eqReady=0,_djReady=0,_lrcSig='',_lrcLines=[],_userEq=0,_userDj=0,_userVol=0,_listen=0,_audio=null,_vidFs=0,_vidFsGate=0,_vidTapT=0,_vidTapX=0,_vidTapY=0,_vidTapTimer=0;function toast(m){var $t=$('#toast');$t.text(m).addClass('show');clearTimeout(window._tt);window._tt=setTimeout(function(){$t.removeClass('show')},900)}function setState(s){var $s=$('#state');if($s.data('s')===s)return;$s.data('s',s);$s.removeClass('play pause stop');if(s==='play'){$s.addClass('play').html('<i class=\"fa-solid fa-play\"></i> PLAY')}else if(s==='pause'){$s.addClass('pause').html('<i class=\"fa-solid fa-pause\"></i> PAUSE')}else{$s.addClass('stop').html('<i class=\"fa-solid fa-stop\"></i> STOP')}}functio";
	page += L"n showTab(id){_tab=id;$('.tab').removeClass('on');$('.tab[data-tab=\"'+id+'\"]').addClass('on');$('.panel').removeClass('on');$('#tab-'+id).addClass('on');if(id==='list')loadPlaylist();if(id==='lrc')loadLyrics(true);if(id==='eq')loadEq(false);if(id==='dj')loadDj(false);if(id==='piano')loadPiano();if(id==='ana')loadAna()}function sendCmd(c,extra){var q='/cmd?c='+encodeURIComponent(c)+(extra||'');return $.ajax({url:q,method:'GET',timeout:2500})}function applyStatus(d){if(!d)return;if(d.title!==_st.title){_st.title=d.title;$('#title').text(d.title&&d.title.length?d.title:'—')}if(d.artist!==_st.artist){_st.artist=d.artist;$('#artist').text(d.artist||'').toggle(!!(d.artist&&d.artist.length))}if(d.album!==_st.album){_st.album=d.album;$('#album').text";
	page += L"(d.album||'').toggle(!!(d.album&&d.album.length))}if(!_userVol&&typeof d.vol==='number'&&d.vol!==_st.vol){_st.vol=d.vol;$('#vol').val(d.vol);$('#volVal').text(d.vol)}if(!!d.muted!==!!_st.muted){_st.muted=d.muted;$('.b-mute').toggleClass('on',!!d.muted)}if(d.state!==_st.state){_st.state=d.state;setState(d.state||'stop')}if(typeof d.index==='number'&&d.index!==_st.index){_st.index=d.index;if(_tab==='list')markPlCur()}if(typeof d.pos_cs==='number')_st.pos_cs=d.pos_cs;if(typeof d.aac_lag_cs==='number'&&d.aac_lag_cs>0)_st.aac_lag_cs=d.aac_lag_cs;if(typeof d.lrccur==='number'){var want=d.lrccur;if(_listen&&_lrcLines.length){var hp=_st.pos_cs-(_st.aac_lag_cs||90);if(hp<0)hp=0;want=-1;for(var li=0;li<_lrcLines.length;li++){if((_lrcLines[li].t||0)<=hp)want=li;else break}}if(want!==_st.lrccur){_st.lrccur=want;markLrcCur(true)}}if(typeof d.aac==='number'&&d.aac!==_st.aac){_st.aac=d.aac;$('#btnListen').prop('disabled',!d.aac);if(!d.aac&&_listen)stopListen()}if(typeof d.vid==='number'&&d.vid!==_st.vid){_st.vid=d.vid;$('#vwrap').toggle(!!d.vid);if(!d.vid){if(_vidFs)exitVidFs();stopVid()}else startVid()}}function stopVid(){if(window._vidT){clearInterval(window._vidT);window._vidT=0}var vi=document.getElementById('vimg');if(vi){try{vi.removeAttribute('src')}catch(e){}}}function startVid(){if(!_st.vid)return;$('#vwrap').show();if(window._vidT){clearInterval(window._vidT);window._vidT=0}window._vidT=setInterval(function(){var vi=document.getElementById('vimg');if(!vi||!_st.vid)return;vi.src='/vframe.jpg?t='+Date.now()},50)}function stopListen(){_listen=0;$('#btnListen').removeClass('on');if(_audio){try{_audio.pause()}catch(e){}try{_audio.removeAttribute('src');_audio.load()}catch(e){}_audio=null}}function ensureListen(){if(_listen||!_st.aac)return;_audio=new Audio('/stream');_audio.preload='none';_audio.play().then(function(){_listen=1;$('#btnListen').addClass('on');if(_st.vid)startVid();refresh()}).catch(function(){})}function lockVidLandscape(){try{if(screen.orientation&&screen.orientation.lock)screen.orientation.lock('landscape')}catch(e){}}function enterVidFs(){if(!_st.vid||_vidFs)return;_vidFs=1;_vidFsGate=Date.now();$('body').addClass('vidfs');$('#vwrap').addClass('fs');ensureListen();try{var de=document.documentElement;var req=de.requestFullscreen?de.requestFullscreen():null;if(req&&req.then)req.then(function(){lockVidLandscape()}).catch(function(){});else lockVidLandscape()}catch(e){}}function exitVidFs(){if(!_vidFs)return;_vidFs=0;_vidFsGate=Date.now();$('body').removeClass('vidfs');$('#vwrap').removeClass('fs');try{if(document.fullscreenElement)document.exitFullscreen()}catch(e){}try{if(screen.orientation&&screen.orientation.unlock)screen.orientation.unlock()}catch(e){}}function toggleVidFs(e){if(e){e.preventDefault();e.stopPropagation()}if(!_st.vid)return;if(Date.now()-_vidFsGate<450)return;if(_vidFs)exitVidFs();else enterVidFs()}function vidFsSeekAt(x){if(!_vidFs)return;var el=document.getElementById('vwrap');if(!el)return;var r=el.getBoundingClientRect(),rx=x-r.left;if(rx<r.width*0.3)sendCmd('seekbk');else if(rx>r.width*0.7)sendCmd('seekfw')}function onVidPointerUp(e){if(!_st.vid)return;var x=e.clientX,y=e.clientY;if(e.changedTouches&&e.changedTouches[0]){x=e.changedTouches[0].clientX;y=e.changedTouches[0].clientY}var now=Date.now();if(now-_vidTapT<320&&Math.abs(x-_vidTapX)<48&&Math.abs(y-_vidTapY)<48){clearTimeout(_vidTapTimer);_vidTapT=0;toggleVidFs(e);return}_vidTapT=now;_vidTapX=x;_vidTapY=y;clearTimeout(_vidTapTimer);_vidTapTimer=setTimeout(function(){vidFsSeekAt(x)},280)}function toggleListen(){if(!_st.aac){toast('AAC off');return}if(_listen){stopListen();return}_audio=new Audio('/stream');_audio.preload='none';_audio.play().then(function(){_listen=1;$('#btnListen').addClass('on');if(_st.vid)startVid();refresh();if(_tab==='lrc')loadLyrics(false)}).catch(function(){toast('Listen failed');stopListen()})}$(function(){$('#btnListen').on('click',function(){toggleListen()});var vw=document.getElementById('vwrap');if(vw){if(window.PointerEvent)vw.addEventListener('pointerup',onVidPointerUp);else{vw.addEventListener('mouseup',onVidPointerUp);vw.addEventListener('touchend',onVidPointerUp,{passive:false})}} $(document).on('keydown',function(e){if(!_vidFs)return;if(e.key==='ArrowLeft'){sendCmd('seekbk');e.preventDefault()}else if(e.key==='ArrowRight'){sendCmd('seekfw');e.preventDefault()}else if(e.key==='Escape'){exitVidFs();e.preventDefault()}});document.addEventListener('fullscreenchange',function(){if(document.fullscreenElement){if(_vidFs)lockVidLandscape()}else if(_vidFs){if(Date.now()-_vidFsGate<450)return;exitVidFs()}}});function refresh(){$.getJSON('/api/status').done(applyStatus).fail(function(){})}function loadPlaylist(){$.getJSON('/api/playlist?o='+_plOff+'&n='+_plPage).done(function(d){if(!d)return;va";
	page += L"r h='',i,it;for(i=0;i<(d.items||[]).length;i++){it=d.items[i];h+='<button type=\"button\" class=\"li'+(it.i===_st.index?' cur':'')+'\" data-i=\"'+it.i+'\"><span class=\"t\"></span><span class=\"m\"></span></button>'}var $l=$('#plList');$l.html(h);$l.children().each(function(idx){var it=d.items[idx];$(this).find('.t').text(it.title||('#'+it.i));$(this).find('.m').text([(it.artist||''),(it.album||'')].filter(Boolean).join(' · '))});$('#plInfo').text((_plOff+1)+'-'+Math.min(_plOff+_plPage,d.total)+' / '+d.total);$('#plPrev').prop('disabled',_plOff<=0);$('#plNext').prop('disabled',_plOff+_plPage>=d.total)}).fail(function(){})}function markPlCur(){$('#plList .li').each(function(){$(this).toggleClass('cur',";
	page += L"(+$(this).data('i'))===_st.index)})}function loadLyrics(force){$.getJSON('/api/lyrics').done(function(d){if(!d)return;var sig=(d.n||0)+':'+(d.lines&&d.lines[0]?d.lines[0].t:'');_lrcLines=d.lines||[];if(!force&&sig===_lrcSig){var want=(typeof d.cur==='number')?d.cur:-1;if(_listen&&_lrcLines.length){var hp=_st.pos_cs-(_st.aac_lag_cs||90);if(hp<0)hp=0;want=-1;for(var li=0;li<_lrcLines.length;li++){if((_lrcLines[li].t||0)<=hp)want=li;else break}}if(want!==_st.lrccur){_st.lrccur=want;markLrcCur(true)}return}_lrcSig=sig;var h='',i;for(i=0;i<_lrcLines.length;i++){h+='<div class=\"ln\" data-i=\"'+i+'\"></div>'}$('#lrcBox').html(h);$('#lrcBox .ln').each(function(idx){$(this).text(_lrcLines[idx].x||'')});var cur=(typeof d.cur==='number')?d.cur:-1;if(_listen&&_lrcLines.length){var hp2=_st.pos_cs-(_st.aac_lag_cs||90);if(hp2<0)hp2=0;cur=-1;for(var lj=0;lj<_lrcLines.length;lj++){if((_lrcLines[lj].t||0)<=hp2)cur=lj;else break}}_st.lrccur=cur;markLrcCur(true)}).fail(function(){})}function markLrcCur(scroll){var $b=$('#lrcBox');if(!$b.length)return;$b.find('.ln').removeClass('cur');if(_st.lrccur<0)return;var $c=$b.find('.ln[data";
	page += L"-i=\"'+_st.lrccur+'\"]');if(!$c.length)return;$c.addClass('cur');if(scroll){var lh=$c.outerHeight()||28;var top=$c.position().top+$b.scrollTop()-lh*2.2;if(top<0)top=0;$b.stop(true).animate({scrollTop:top},180)}}function loadEq(force){$.getJSON('/api/eq').done(function(d){if(!d)return;if(_userEq&&!force)return;var i;for(i=0;i<15;i++){var v=(d.eq&&typeof d.eq[i]==='number')?d.eq[i]:100;$('#eq'+i).val(v);$('#eqv'+i).text(v)}if(typeof d.pre==='number')$('#eqPre').val(String(d.pre));if(typeof d.env==='number')$('#eqEnv').val(String(d.env));if(typeof d.rev==='number'){$('#eqRev').val(d.rev);$('#eqRevV').text(d.rev)}if(typeof d.cho==='number'){$('#eqCho').val(d.cho);$('#eqChoV').text(d.cho)}if(typeof d.del==='number')";
	page += L"{$('#eqDel').val(d.del);$('#eqDelV').text(d.del)}if(typeof d.eff==='number'){$('#eqEff').val(d.eff);$('#eqEffV').text(d.eff)}_eqReady=1}).fail(function(){})}var _vinyl={drag:0,lastA:0,spin:0,head:0,pending:0,raf:0};function vinylAng(e,el){var r=el.getBoundingClientRect(),cx=r.left+r.width/2,cy=r.top+r.height/2;var x=(e.clientX!=null?e.clientX:(e.touches&&e.touches[0]?e.touches[0].clientX:0))-cx;var y=(e.clientY!=null?e.clientY:(e.touches&&e.touches[0]?e.touches[0].clientY:0))-cy;return Math.atan2(y,x)*180/Math.PI}function drawVinyl(){var c=document.getElementById('vinyl');if(!c)return;var ctx=c.getContext('2d'),W=c.width,H=c.height,cx=W/2,cy=H/2,R=Math.min(W,H)/2-8;ctx.clearRect(0,0,W,H);ctx.save();ctx.translate(cx,cy);ctx.rotate(((_vinyl.spin+_vinyl.head)%360)*Math.PI/180);ctx.beginPath();ctx.arc(0,0,R,0,Math.PI*2);ctx.fillStyle='#1c1e26';ctx.fill();for(var i=0;i<18;i++){ctx.beginPath();ctx.arc(0,0,R*(0.92-i*0.035),0,Math.PI*2);ctx.strokeStyle='rgba(255,255,255,'+(0.04+(i%2)*0.03)+')';ctx.lineWidth=2;ctx.stroke()}ctx.beginPath();ctx.arc(0,0,R*0.22,0,Math.PI*2);ctx.fillStyle='#ff69b4';ctx.fill();ctx.beginPath();ctx.arc(0,0,R*0.08,0,Math.PI*2);ctx.fillStyle='#fff';ctx.fill();ctx.strokeStyle='#ffd28c';ctx.lineWidth=6;ctx.beginPath();ctx.moveTo(0,-R*0.22);ctx.lineTo(0,-R*0.92);ctx.stroke();ctx.restore()}function loadDj(force){$.getJSON('/api/dj').done(function(d){if(!d)return;if(_vinyl.drag)return;if(typeof d.head==='number')_vinyl.head=d.head;if(d.playing){_vinyl.spin=(_vinyl.spin+2.2)%360}drawVinyl();_djReady=1}).fail(function(){})}function flushScratch(){if(!_vinyl.pending)return;var d=Math.round(_vinyl.pending*100);_vinyl.pending=0;if(d===0)return;sendCmd('scr','&d='+d)}$(function(){setState('";
	page += state;
	page += L"');$('#artist').toggle(!!$('#artist').text());$('#album').toggle(!!$('#album').text());$(document).on('click','.tab',function(){showTab($(this).data('tab'))});$(document).on('click','.btn[data-cmd]',function(){var $b=$(this),c=$b.data('cmd');$b.addClass('busy');sendCmd(c).always(function(){$b.removeClass('busy');setTimeout(refresh,80);toast(c)})});var volTimer=null;$('#vol').on('input',function(){_userVol=1;$('#volVal').text(this.value)});$('#vol').on('change input',function(){var v=+this.value;clearTimeout(volTimer);volTimer=setTimeout(function(){sendCmd('vol','&v='+v).always(function(){_userVol=0;refresh()})},120)});$(document).on('click','#plList .li',function(){var i=+$(this).data('i');s";
	page += L"endCmd('playidx','&i='+i).always(function(){setTimeout(function(){refresh();loadPlaylist()},100);toast('play')})});$('#plPrev').on('click',function(){if(_plOff<=0)return;_plOff=Math.max(0,_plOff-_plPage);loadPlaylist()});$('#plNext').on('click',function(){_plOff+=_plPage;loadPlaylist()});$('.lrcbtn').on('click',function(){var d=+$(this).data('d');sendCmd('lrc','&delta='+d).always(function(){setTimeout(function(){loadLyrics(true)},80);toast('lrc')})});$('#lrcSave').on('click',function(){sendCmd('lrcsave').always(function(){toast('save')})});var eqT=null;function eqIsScroll(el){return !!(el&&el._eqScroll)}function eqRevert(el){if(!el)return;el.value=String(el._eqV);var id=el.id;if(el.classList.contains('eqb'))$('#eqv'+$(el).attr('data-b')).text(el._eqV);else if(id==='eqRev')$('#eqRevV').text(el._eqV);else if(id==='eqCho')$('#eqChoV').text(el._eqV);else if(id==='eqDel')$('#eqDelV').text(el._eqV);else if(id==='eqEff')$('#eqEffV').text(el._eqV)}function eqBindGate(sel){$(document).on('pointerdown',sel,function(e){this._eqX=e.clientX;this._eqY=e.clientY;this._eqV=+this.value;this._eqScroll=0;this._eqMoved=0;this._eqDown=1;this._eqPend=0});$(document).on('pointermove',sel,function(e){if(!this._eqDown||this._eqScroll)return;var dx=e.clientX-this._eqX,dy=e.clientY-this._eqY;if(!this._eqMoved&&Math.abs(dy)>10&&Math.abs(dy)>Math.abs(dx)*1.1){this._eqScroll=1;eqRevert(this);this._eqPend=0;return}if(Math.abs(dx)>8)this._eqMoved=1});$(document).on('pointerup pointercancel',sel,function(){if(!this._eqDown)return;this._eqDown=0;if(this._eqScroll){eqRevert(this);return}if(!this._eqPend)return;var el=this;if(el.classList.contains('eqb')){var b=+$(el).attr('data-b'),v=+el.value;_userEq=1;clearTimeout(eqT);eqT=setTimeout(function(){sendCmd('eqband','&b='+b+'&v='+v).always(function(){_userEq=0})},40)}else if(el.id==='eqRev')fxSend(0,+el.value);else if(el.id==='eqCho')fxSend(1,+el.value);else if(el.id==='eqDel')fxSend(2,+el.value);else if(el.id==='eqEff')fxSend(3,+el.value)})}eqBindGate('.eqb,#eqRev,#eqCho,#eqDel,#eqEff');$(document).on('input','.eqb',function(){if(eqIsScroll(this)){eqRevert(this);return}var b=+$(this).attr('data-b'),v=+this.value;$('#eqv'+b).text(v);if(this._eqDown){this._eqPend=1;return}_userEq=1;clearTimeout(eqT);eqT=setTimeou";
	page += L"t(function(){sendCmd('eqband','&b='+b+'&v='+v).always(function(){_userEq=0})},80)});$('#eqPre').on('change',function(){_userEq=1;sendCmd('eqpreset','&p='+this.value).always(function(){setTimeout(function(){_userEq=0;loadEq(true)},120)})});$('#eqEnv').on('change',function(){_userEq=1;sendCmd('eqenv','&p='+this.value).always(function(){_userEq=0})});$('#eqReset').on('click',function(){_userEq=1;sendCmd('eqreset').always(function(){setTimeout(function(){_userEq=0;loadEq(true)},80);toast('eq')})});$('#eqResetG').on('click',function(){_userEq=1;sendCmd('eqresetg').always(function(){setTimeout(function(){_userEq=0;loadEq(true)},80);toast('eq')})});var fxT=null;function fxSend(which,v){_userEq=1;clearTimeout(fxT);fxT=setTimeout(function(){sendCmd('eqfx','&w='+which+'&v='+v).always(function(){_userEq=0})},80)}$('#eqRev').on('input',function(){if(eqIsScroll(this)){eqRevert(this);return}$('#eqRevV').text(this.value);if(this._eqDown){this._eqPend=1;return}fxSend(0,+this.value)});$('#eqCho').on('input',function(){if(eqIsScroll(this)){eqRevert(this);return}$('#eqChoV').text(this.value);if(this._eqDown){this._eqPend=1;return}fxSend(1,+this.value)});$('#eqDel";
	page += L"').on('input',function(){if(eqIsScroll(this)){eqRevert(this);return}$('#eqDelV').text(this.value);if(this._eqDown){this._eqPend=1;return}fxSend(2,+this.value)});$('#eqEff').on('input',function(){if(eqIsScroll(this)){eqRevert(this);return}$('#eqEffV').text(this.value);if(this._eqDown){this._eqPend=1;return}fxSend(3,+this.value)});function fitCan(c,aspect){if(!c)return null;var r=c.getBoundingClientRect(),d=window.devicePixelRatio||1,aw=Math.max(1,r.width),ah=Math.max(1,aw*(aspect||0.42)),w=Math.max(1,Math.floor(aw*d)),h=Math.max(1,Math.floor(ah*d));if(c.width!==w||c.height!==h){c.width=w;c.height=h}return c.getContext('2d')}function smoothBins(bins,outN){var n=(bins&&bins.length)?bins.length:0,dst=new Array(outN),i,t,a,b,c,d,p,u,u2,u3;if(!n){for(i=0;i<outN;i++)dst[i]=0;return dst}function at(j){j=j<0?0:(j>=n?n-1:j);var v=+bins[j]||0;return v<0?0:(v>96?96:v)}for(i=0;i<outN;i++){t=i*(n-1)/Math.max(1,outN-1);p=Math.floor(t);u=t-p;a=at(p-1);b=at(p);c=at(p+1);d=at(p+2);u2=u*u;u3=u2*u;dst[i]=0.5*((2*b)+(-a+c)*u+(2*a-5*b+4*c-d)*u2+(-a+3*b-3*c+d)*u3);if(dst[i]<0)dst[i]=0;if(dst[i]>96)dst[i]=96}return dst}function drawAna(payload){var chs=[],labs=[],c=document.getElementById('anaCan'),ctx=fitCan(c,0.48);if(!ctx||!c)return;if(payload&&payload.b){if(payload.b.length&&typeof payload.b[0]==='object'){chs=payload.b;labs=payload.lab||[]}else{chs=[payload.b]}}else if(payload&&payload.length){chs=[payload]}var n=chs.length;if(!n){ctx.fillStyle='#0e1018';ctx.fillRect(0,0,c.width,c.height);return}var cols=['rgba(80,200,255,','rgba(255,140,180,','rgba(120,230,140,','rgba(255,200,80,','rgba(180,140,255,','rgba(80,220,200,','rgba(255,160,100,','rgba(200,200,220,'];var W=c.width,H=c.height,pad=Math.max(2,W*0.01),gap=Math.max(3,Math.floor(H*0.012));var bandH=Math.max(28,Math.floor((H-pad*2-gap*(n-1))/n));ctx.fillStyle='#0e1018';ctx.fillRect(0,0,W,H);for(var ci=0;ci<n;ci++){var yBase=pad+ci*(bandH+gap),y0=yBase+2,y1=yBase+bandH-2,plotW=W-pad*2,plotH=Math.max(8,y1-y0);var sm=smoothBins(chs[ci]||[],Math.max(64,Math.floor(plotW/2))),N=sm.length,i,x,y;ctx.strokeStyle='rgba(255,255,255,0.05)';ctx.lineWidth=1;for(i=1;i<=3;i++){y=y0+plotH*i/4;ctx.beginPath();ctx.moveTo(pad,y);ctx.lineTo(W-pad,y);ctx.stroke()}var g=ctx.createLinearGradient(0,y0,0,y1);g.addColorStop(0,cols[ci%8]+'0.5)');g.addColorStop(1,cols[ci%8]+'0.06)');ctx.beginPath();ctx.moveTo(pad,y1);for(i=0;i<N;i++){x=pad+(i/Math.max(1,N-1))*plotW;y=y1-(sm[i]/96)*plotH;ctx.lineTo(x,y)}ctx.lineTo(W-pad,y1);ctx.closePath();ctx.fillStyle=g;ctx.fill();ctx.beginPath();for(i=0;i<N;i++){x=pad+(i/Math.max(1,N-1))*plotW;y=y1-(sm[i]/96)*plotH;if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y)}ctx.strokeStyle=cols[ci%8]+'1)';ctx.lineWidth=Math.max(1.2,W/420);ctx.lineJoin='round';ctx.stroke();if(labs[ci]){ctx.fillStyle=cols[ci%8]+'0.95)';ctx.font='bold '+Math.max(10,Math.floor(W/42))+'px sans-serif';ctx.textAlign='right';ctx.fillText(labs[ci],W-pad-2,y0+Math.max(11,bandH*0.28))}}}var _pianoHist=[],_pianoHistX=[],_pianoHistMax=96,_pianoOff=0,_pianoLastT=0;function isBlack(m){var k=m%12;return k===1||k===3||k===6||k===8||k===10}function exprColor(e){if(e&2)return 'rgba(255,220,80,0.95)';if(e&1)return 'rgba(255,100,100,0.95)';if(e&4)return 'rgba(120,255,180,0.95)';if(e&8)return 'rgba(120,160,255,0.95)';if(e&16)return 'rgba(255,170,90,0.95)';if(e&64)return 'rgba(120,230,255,0.95)';if(e&128)return 'rgba(200,150,255,0.95)';if(e&32)return 'rgba(190,190,210,0.9)';return null}function exprGlyph(e){if(e&2)return '↗';if(e&1)return '▸';if(e&4)return '~';if(e&8)return '→';if(e&16)return '↘';if(e&64)return '<';if(e&128)return '>';if(e&32)return '―';return ''}function drawPiano(keys,chord,hist,histX,exprOn,keyExpr){if(hist&&hist.length){_pianoHist=hist.slice(0,_pianoHistMax);_pianoHistX=(histX&&histX.length)?histX.slice(0,_pianoHistMax):[]}else if(keys){var act=[],ax=[];for(var i=0;i<108;i++)if((+keys[i]|0)>=1){act.push(i);ax.push((keyExpr&&keyExpr[i])?(+keyExpr[i]|0):0)}_pianoHist.unshift(act);_pianoHistX.unshift(ax);if(_pianoHist.length>_pianoHistMax){_pianoHist.length=_pianoHistMax;_pianoHistX.length=_pianoHistMax}}var now=performance.now();if(_pianoLastT){var dt=Math.min(50,now-_pianoLastT);_pianoOff+=dt/45}else _pianoOff=0;_pianoLastT=now;if(_pianoOff>1)_pianoOff-=Math.floor(_pianoOff);var c=document.getElementById('pianoCan');var ctx=fitCan(c,0.62);if(!ctx||!c)return;var W=c.width,H=c.height;ctx.fillStyle='#0e1018';ctx.fillRect(0,0,W,H);if(chord)$('#pianoChord').text(chord);var keyH=Math.floor(H*0.22),rollH=H-keyH,lo=21,hi=107,wh=[],m,i;for(m=lo;m<=hi;m++)if(!isBlack(m))wh.push(m);var ww=W/Math.max(1,wh.length);function whiteIdx(midi){var k=wh.indexOf(midi);return k<0?0:k}function noteX(midi){if(!isBlack(midi))return whiteIdx(midi)*ww;var left=midi-1;while(left>=lo&&isBlack(left))left--;return (whiteIdx(left)+1)*ww-ww*0.32}function noteW(midi){return isBlack(midi)?ww*0.55:Math.max(1,ww-1.5)}var rowH=Math.max(2,Math.floor(rollH/Math.max(48,_pianoHist.length||48)));var rows=Math.min(_pianoHist.length,Math.floor(rollH/rowH)+2),off=(_pianoOff%1)*rowH;ctx.save();ctx.beginPath();ctx.rect(0,0,W,rollH);ctx.clip();ctx.fillStyle='#12151e';ctx.fillRect(0,0,W,rollH);ctx.strokeStyle='rgba(255,255,255,0.04)';ctx.lineWidth=1;for(i=0;i<wh.length;i++){var gx=i*ww;ctx.beginPath();ctx.moveTo(gx,0);ctx.lineTo(gx,rollH);ctx.stroke()}var fs=Math.max(8,Math.min(14,Math.floor(rowH*0.9)));for(var r=0;r<rows;r++){var fr=_pianoHist[r];if(!fr||!fr.length)continue;var fx=_pianoHistX[r]||[];var y=rollH-(r+1)*rowH+off;if(y+rowH<0||y>rollH)continue;for(i=0;i<fr.length;i++){m=+fr[i]|0;if(m<lo||m>hi)continue;var ex=exprOn?(+fx[i]|0):0;ctx.fillStyle=isBlack(m)?'rgba(196,90,208,0.85)':'rgba(255,105,180,0.9)';ctx.fillRect(noteX(m)+0.5,y+0.5,noteW(m),Math.max(1,rowH-1));if(ex){var ec=exprColor(ex);if(ec){ctx.fillStyle=ec;ctx.fillRect(noteX(m)+0.5,y+0.5,Math.max(1,noteW(m)*0.35),Math.max(1,rowH-1));if(rowH>=8&&noteW(m)>=6){var g=exprGlyph(ex);if(g){ctx.fillStyle='#fff';ctx.font='bold '+fs+'px sans-serif';ctx.textAlign='left';ctx.textBaseline='middle';ctx.fillText(g,noteX(m)+1,y+rowH/2)}}}}}}ctx.restore();var yK=rollH;for(i=0;i<wh.length;i++){m=wh[i];var v=(keys&&keys[m])?(+keys[m]|0):0;ctx.fillStyle=v>=1?('rgba(255,105,180,'+(0.35+v/100*0.65)+')'):'#f2efe8';ctx.fillRect(i*ww,yK,Math.max(1,ww-1),keyH);ctx.strokeStyle='#c8c0b8';ctx.strokeRect(i*ww,yK,Math.max(1,ww-1),keyH);if(exprOn&&keyExpr&&(+keyExpr[m]|0)){var e2=+keyExpr[m]|0,ec2=exprColor(e2);if(ec2){ctx.fillStyle=ec2;ctx.fillRect(i*ww+1,yK+1,Math.max(1,ww-3),3)}}}for(m=lo;m<=hi;m++){if(!isBlack(m))continue;v=(keys&&keys[m])?(+keys[m]|0):0;ctx.fillStyle=v>=1?('rgba(196,90,208,'+(0.45+v/100*0.55)+')'):'#1a1a22';ctx.fillRect(noteX(m),yK,noteW(m),keyH*0.62);if(exprOn&&keyExpr&&(+keyExpr[m]|0)){var e3=+keyExpr[m]|0,ec3=exprColor(e3);if(ec3){ctx.fillStyle=ec3;ctx.fillRect(noteX(m),yK,noteW(m),2)}}}ctx.fillStyle='rgba(255,210,140,0.9)';ctx.fillRect(0,rollH-2,W,2)}function loadAna(){$.getJSON('/api/analyzer').done(function(d){if(d)drawAna(d)}).fail(function(){})}function loadPiano(){$.getJSON('/api/piano').done(function(d){if(!d)return;drawPiano(d.k||[],d.c||'-',d.h||null,d.x||null,!!d.xm,d.kx||null)}).fail(function(){})}function onVinylDown(e){var el=document.getElementById('vinyl');if(!el)return;e.preventDefault();_vinyl.drag=1;_vinyl.lastA=vinylAng(e,el);_vinyl.pending=0;sendCmd('scrbeg');if(el.setPointerCapture&&e.pointerId!=null)el.setPointerCapture(e.pointerId)}function onVinylMove(e){if(!_vinyl.drag)return;e.preventDefault();var el=document.getElementById('vinyl');var a=vinylAng(e,el);var d=a-_vinyl.lastA;if(d>180)d-=360;if(d<-180)d+=360;_vinyl.lastA=a;_vinyl.spin=(_vinyl.spin+d)%360;_vinyl.pending+=d;drawVinyl();if(!_vinyl.raf)_vinyl.raf=requestAnimationFrame(function(){_vinyl.raf=0;flushScratch()})}";
	page += L"function onVinylUp(e){if(!_vinyl.drag)return;_vinyl.drag=0;flushScratch();sendCmd('scrend')}var vv=document.getElementById('vinyl');if(vv){vv.addEventListener('pointerdown',onVinylDown);vv.addEventListener('pointermove',onVinylMove);vv.addEventListener('pointerup',onVinylUp);vv.addEventListener('pointercancel',onVinylUp);drawVinyl()}showTab('play');setInterval(function(){refresh();if(_tab==='lrc')loadLyrics(false);if(_tab==='dj')loadDj(false)},2000);setInterval(function(){if(_listen){refresh();if(_tab==='lrc')loadLyrics(false)}},250);setInterval(function(){if(_tab==='piano')loadPiano();if(_tab==='ana')loadAna()},50);refresh();});";
	page += L"</script></body></html>";

	CStringA bodyA;
	{
		const int nbytes = ::WideCharToMultiByte(CP_UTF8, 0, page, -1, NULL, 0, NULL, NULL);
		if (nbytes > 1) {
			char* pb = bodyA.GetBufferSetLength(nbytes - 1);
			::WideCharToMultiByte(CP_UTF8, 0, page, -1, pb, nbytes, NULL, NULL);
			bodyA.ReleaseBuffer(nbytes - 1);
		}
	}
	CStringA hdr;
	hdr.Format(
		"HTTP/1.0 200 OK\r\n"
		"Content-Type: text/html; charset=utf-8\r\n"
		"Connection: close\r\n"
		"Cache-Control: no-store\r\n"
		"Content-Length: %d\r\n"
		"\r\n",
		bodyA.GetLength());
	MpRemoteSendAll(s, hdr, hdr.GetLength());
	MpRemoteSendAll(s, bodyA, bodyA.GetLength());
}

static UINT MpRemoteClientProc(LPVOID p)
{
	SOCKET c = (SOCKET)(UINT_PTR)p;
	MpRemoteTrackClient(c, TRUE);
	if (InterlockedCompareExchange(&g_mpRemoteStop, 0, 0) == 0)
		MpRemoteHandleRequest(c);
	MpRemoteReleaseClient(c);
	InterlockedDecrement(&g_mpRemoteBusy);
	return 0;
}

static UINT MpRemoteThreadProc(LPVOID)
{
	while (InterlockedCompareExchange(&g_mpRemoteStop, 0, 0) == 0) {
		if (g_mpRemoteListen == INVALID_SOCKET)
			break;
		fd_set rf;
		FD_ZERO(&rf);
		FD_SET(g_mpRemoteListen, &rf);
		timeval tv = { 0, 200000 }; // 0.2s — 停止反応を速く
		const int sel = select(0, &rf, NULL, NULL, &tv);
		if (sel < 0) break;
		if (sel == 0) continue;
		SOCKET c = accept(g_mpRemoteListen, NULL, NULL);
		if (c == INVALID_SOCKET) {
			if (InterlockedCompareExchange(&g_mpRemoteStop, 0, 0) != 0)
				break;
			continue;
		}
		// 同時接続上限（PC+スマホ×2 想定）
		const LONG n = InterlockedIncrement(&g_mpRemoteBusy);
		if (n > kMpRemoteMaxClients) {
			InterlockedDecrement(&g_mpRemoteBusy);
			const char* busy =
				"HTTP/1.0 503 Service Unavailable\r\nConnection: close\r\n"
				"Content-Type: text/plain; charset=utf-8\r\n\r\nbusy";
			MpRemoteSendAll(c, busy, (int)strlen(busy));
			closesocket(c);
			continue;
		}
		HANDLE h = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)MpRemoteClientProc, (LPVOID)(UINT_PTR)c, 0, NULL);
		if (!h) {
			InterlockedDecrement(&g_mpRemoteBusy);
			closesocket(c);
			continue;
		}
		CloseHandle(h); // デタッチ（完了は g_mpRemoteBusy で追跡）
	}
	return 0;
}

void MpRemoteStop()
{
	InterlockedExchange(&g_mpRemoteStop, 1);
	MpRemoteKickClients();
	MpRemAacCsEnsure();
	EnterCriticalSection(&g_mpRemAacCs);
	MpRemAacEncReleaseLocked();
	InterlockedExchange(&g_mpRemAacClients, 0);
	InterlockedExchange(&g_mpRemAacW, 0);
	InterlockedExchange(&g_mpRemVidClients, 0);
	InterlockedExchange(&g_mpRemVidW, 0);
	InterlockedExchange(&g_mpRemVidJpegN, 0);
	if (g_mpRemH264) {
		g_mpRemH264->Release();
		g_mpRemH264 = NULL;
		g_mpRemH264W = g_mpRemH264H = 0;
	}
	LeaveCriticalSection(&g_mpRemAacCs);
	if (g_mpRemoteListen != INVALID_SOCKET) {
		closesocket(g_mpRemoteListen);
		g_mpRemoteListen = INVALID_SOCKET;
	}
	if (g_mpRemoteThread) {
		// 最大 ~250ms。それ以上待たずハンドルだけ手放す（終了遅延の主因だった）
		const DWORD t0 = GetTickCount();
		for (;;) {
			const DWORD w = WaitForSingleObject(g_mpRemoteThread, 25);
			if (w == WAIT_OBJECT_0)
				break;
			MSG msg;
			while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			if (GetTickCount() - t0 >= 250)
				break;
		}
		CloseHandle(g_mpRemoteThread);
		g_mpRemoteThread = NULL;
	}
	// busy ワーカーが残っていてもプロセス終了時は OS が回収。WSACleanup は残ソケットで固まるので
	// まだ busy>0 ならスキップする。
	if (g_mpRemoteWsa) {
		if (InterlockedCompareExchange(&g_mpRemoteBusy, 0, 0) == 0)
			WSACleanup();
		g_mpRemoteWsa = 0;
	}
	g_mpRemoteBoundPort = 0;
}

void MpRemoteEnsureRunning(HWND notifyHwnd)
{
	g_mpRemoteHwnd = notifyHwnd;
	if (!savedata.mpRemoteOn) {
		MpRemoteStop();
		return;
	}
	const int port = (savedata.mpRemotePort >= 1024 && savedata.mpRemotePort <= 65535)
		? savedata.mpRemotePort : 8765;
	// 既に同じポートで待ち受け中なら何もしない。ポート変更時は再 bind。
	if (g_mpRemoteThread && g_mpRemoteListen != INVALID_SOCKET && g_mpRemoteBoundPort == port)
		return;

	MpRemoteStop();
	InterlockedExchange(&g_mpRemoteStop, 0);
	InterlockedExchange(&g_mpRemoteBusy, 0);
	MpRemoteCsEnsure();
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
	addr.sin_addr.s_addr = htonl(INADDR_ANY); // LAN / Wi-Fi から到達可能
	addr.sin_port = htons((u_short)port);
	if (bind(g_mpRemoteListen, (sockaddr*)&addr, sizeof(addr)) != 0) {
		MpRemoteStop();
		return;
	}
	listen(g_mpRemoteListen, 8);
	g_mpRemoteThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)MpRemoteThreadProc, NULL, 0, NULL);
	if (!g_mpRemoteThread) {
		MpRemoteStop();
		return;
	}
	g_mpRemoteBoundPort = port;
	// UI スレッドで音量キャッシュを初期化（accept 側は GetPos しない）
	{
		int v = 50;
		if (mp && ::IsWindow(mp->GetSafeHwnd()) && mp->m_vol.GetSafeHwnd())
			v = mp->m_vol.GetPos();
		else if (og && og->m_sl.GetSafeHwnd())
			v = og->m_sl.GetPos() / 1000;
		MpRemoteCacheVol(v);
	}
}

void MpRemoteUiTick(CMediaPlayerDlg* mpDlg)
{
	if (!savedata.mpRemoteOn) return;
	MpRemAacFeedSilenceIfNeeded();
	MpRemVidCaptureTick();
	int posCs = 0, durCs = 0, head100 = 0;
	if (og && ::IsWindow(og->GetSafeHwnd()) && og->m_time.GetSafeHwnd()) {
		int mn = og->m_time.GetMinValue();
		int mx = og->m_time.GetMaxValue();
		if (mx <= mn)
			og->m_time.GetRange(mn, mx);
		const int pos = og->m_time.GetPos();
		const int span = mx - mn;
		if (span > 0) {
			posCs = pos - mn;
			durCs = span;
			head100 = (int)(36000.0 * (double)(pos - mn) / (double)span + 0.5);
			if (head100 < 0) head100 = 0;
			if (head100 > 36000) head100 = 36000;
		}
	}
	InterlockedExchange(&g_mpRemotePosCs, (LONG)posCs);
	InterlockedExchange(&g_mpRemoteDurCs, (LONG)durCs);
	InterlockedExchange(&g_mpRemoteHeadDeg100, (LONG)head100);
	// 歌詞行は og->lrccur(ttt=先読み寄り)ではなく、MP歌詞ビューと同じ GDI 実再生時刻で決める
	int lrc = -1;
	if (og && og->lrcnum > 0) {
		extern int videoonly;
		extern UINT ttt;
		DWORD centis = ttt;
		if (!(mode == -2 || videoonly)) {
			const double sec = OggGetGdiPlaybackTimeSec();
			if (sec >= 0.0)
				centis = (DWORD)(sec * 100.0 + 0.5);
		}
		InterlockedExchange(&g_mpRemotePosCs, (LONG)centis);
		int idx = 0;
		const int n = og->lrcnum;
		for (int i = 0; i < n - 1; i++) {
			if (og->lrctm[i] <= centis && og->lrctm[i + 1] > centis) {
				idx = i;
				break;
			}
			if (centis >= og->lrctm[i])
				idx = i;
		}
		if (idx < 0) idx = 0;
		if (idx >= n) idx = n - 1;
		lrc = idx;
	}
	InterlockedExchange(&g_mpRemoteLrcCur, (LONG)lrc);
	int idx = -1, cnt = 0;
	if (pl && pl->pc) {
		cnt = pl->playcnt;
		if (pl->pnt >= 0 && pl->pnt < pl->playcnt)
			idx = pl->pnt;
		else if (plcnt >= 0 && plcnt < pl->playcnt)
			idx = plcnt;
	}
	InterlockedExchange(&g_mpRemotePlayIdx, (LONG)idx);
	InterlockedExchange(&g_mpRemotePlayCnt, (LONG)cnt);
	if (mpDlg && mpDlg->m_vol.GetSafeHwnd())
		MpRemoteCacheVol(mpDlg->m_vol.GetPos());

	const DWORD nowTick = GetTickCount();
	const LONG pianoWant = InterlockedCompareExchange(&g_mpRemoteWantPianoMs, 0, 0);
	const LONG anaWant = InterlockedCompareExchange(&g_mpRemoteWantAnaMs, 0, 0);
	const bool needPiano = (pianoWant != 0) && ((DWORD)(nowTick - (DWORD)pianoWant) < 4000u);
	const bool needAna = (anaWant != 0) && ((DWORD)(nowTick - (DWORD)anaWant) < 4000u);

	if (needPiano && og && og->m_PianoRollDlg) {
		if (!::IsWindow(og->m_PianoRollDlg->GetSafeHwnd())) {
			if (og->m_PianoRollDlg->Create(IDD_PIANOROLL, og))
				og->m_PianoRollDlg->ShowWindow(SW_SHOWNOACTIVATE);
		}
		else if (!::IsWindowVisible(og->m_PianoRollDlg->GetSafeHwnd())) {
			og->m_PianoRollDlg->ShowWindow(SW_SHOWNOACTIVATE);
		}
		if (::IsWindow(og->m_PianoRollDlg->GetSafeHwnd())) {
			og->m_PianoRollDlg->ResumePlaybackFeed();
			int rows = 0;
			int exprOn = 0;
			og->m_PianoRollDlg->ExportRemoteSnapshot(
				g_mpRemoteNotes, g_mpRemoteNoteExpr,
				&g_mpRemoteHist[0][0], &g_mpRemoteHistExpr[0][0],
				72, rows, exprOn, g_mpRemoteChord, 48);
			g_mpRemoteHistRows = rows;
			g_mpRemoteExprOn = exprOn;
			InterlockedIncrement(&g_mpRemoteVizSeq);
		}
	}

	if (needAna && og && og->m_AnalyzerDlg) {
		if (!::IsWindow(og->m_AnalyzerDlg->GetSafeHwnd())) {
			if (og->m_AnalyzerDlg->Create(IDD_ANALYZER, og))
				og->m_AnalyzerDlg->ShowWindow(SW_SHOWNOACTIVATE);
		}
		else if (!::IsWindowVisible(og->m_AnalyzerDlg->GetSafeHwnd())) {
			og->m_AnalyzerDlg->ShowWindow(SW_SHOWNOACTIVATE);
		}
		if (::IsWindow(og->m_AnalyzerDlg->GetSafeHwnd())) {
			og->m_AnalyzerDlg->ResumePlaybackFeed();
			int nch = 1;
			og->m_AnalyzerDlg->ExportRemoteBars(g_mpRemoteSpec, 8, nch);
			g_mpRemoteSpecCh = nch;
			InterlockedIncrement(&g_mpRemoteVizSeq);
		}
	}
	(void)mpDlg;
}

void MpRemoteOpenInBrowser()
{
	int port = savedata.mpRemotePort;
	if (port < 1024 || port > 65535) port = 8765;
	savedata.mpRemoteOn = 1;
	if (savedata.mpRemotePort < 1024 || savedata.mpRemotePort > 65535)
		savedata.mpRemotePort = port;
	MpPersistSavedataQuick();
	HWND hwnd = NULL;
	if (mp && ::IsWindow(mp->GetSafeHwnd()))
		hwnd = mp->GetSafeHwnd();
	MpRemoteEnsureRunning(hwnd);
	CString url;
	url.Format(L"http://127.0.0.1:%d/", port);
	::ShellExecute(NULL, L"open", url, NULL, NULL, SW_SHOWNORMAL);
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
	int volAbs = -1;
	if (savedata.mpMidiLearn && ((st & 0xF0) == 0xB0)) {
		// 学習: 最初の空きスロットへ CC を割当
		for (int i = 0; i < 4; ++i) {
			if (savedata.mpMidiMapCc[i] < 0) {
				savedata.mpMidiMapCc[i] = (int)d1;
				savedata.mpMidiLearn = 0;
				break;
			}
		}
		return;
	}
	if ((st & 0xF0) == 0x90 && d2 > 0) {
		if (d1 == 60) cmd = 0;
		else if (d1 == 61) cmd = 1;
		else if (d1 == 62) cmd = 2;
	}
	else if ((st & 0xF0) == 0xB0) {
		if (d1 == 7 || (savedata.mpMidiMapCc[0] >= 0 && d1 == (BYTE)savedata.mpMidiMapCc[0])) {
			volAbs = (int)d2 * 100 / 127;
			cmd = 10;
		}
		else if (savedata.mpMidiMapCc[1] >= 0 && d1 == (BYTE)savedata.mpMidiMapCc[1]) {
			// tempo
			const int pct = 33 + (int)d2 * (300 - 33) / 127;
			::PostMessage(g_mpMidiHwnd, WM_MP_TRANSPORT_CMD, (WPARAM)20, (LPARAM)pct);
			return;
		}
		else if (savedata.mpMidiMapCc[2] >= 0 && d1 == (BYTE)savedata.mpMidiMapCc[2]) {
			::PostMessage(g_mpMidiHwnd, WM_MP_TRANSPORT_CMD, (WPARAM)21, (LPARAM)d2);
			return;
		}
		else if (savedata.mpMidiMapCc[3] >= 0 && d1 == (BYTE)savedata.mpMidiMapCc[3]) {
			::PostMessage(g_mpMidiHwnd, WM_MP_TRANSPORT_CMD, (WPARAM)22, (LPARAM)d2);
			return;
		}
	}
	if (cmd == 10) {
		::PostMessage(g_mpMidiHwnd, WM_MP_TRANSPORT_CMD, (WPARAM)10, (LPARAM)volAbs);
		return;
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
	const UINT nDev = midiInGetNumDevs();
	if (nDev == 0) {
		g_mpMidiIn = NULL;
		return;
	}
	// MIDI_MAPPER は入力に使えない。先頭デバイスを開く。
	MMRESULT r = midiInOpen(&g_mpMidiIn, 0, (DWORD_PTR)MpMidiInCallback, 0, CALLBACK_FUNCTION);
	if (r != MMSYSERR_NOERROR) {
		g_mpMidiIn = NULL;
		return;
	}
	midiInStart(g_mpMidiIn);
}

static void MpDjPadApplyRemoteEq(); // CMpDjPadDlg 定義後に実装

LRESULT MpAddonsOnTransportCmd(CMediaPlayerDlg* mpDlg, WPARAM wParam, LPARAM lParam)
{
	if (!mpDlg) return 0;
	switch ((int)wParam) {
	case 0:
		// 動画: 一時停止中は再開。停止中は途中再生確認を抑止して再演奏。
		if ((mode == -2 || videoonly) && pMainFrame1 && ::IsWindow(pMainFrame1->GetSafeHwnd())) {
			if (ps == 1) {
				pMainFrame1->On32775();
			} else {
				OggArmRemoteSilentResumeYes();
				MpTaskbarReplay();
			}
		} else {
			OggArmRemoteSilentResumeYes();
			mpDlg->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_MP_PLAY, BN_CLICKED), 0);
		}
		break;
	case 1:
		if ((mode == -2 || videoonly) && pMainFrame1 && ::IsWindow(pMainFrame1->GetSafeHwnd()))
			pMainFrame1->On32775();
		else
			mpDlg->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_MP_PAUSE, BN_CLICKED), 0);
		break;
	case 2:
		OggArmRemoteSilentResumeYes();
		mpDlg->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_MP_NEXT, BN_CLICKED), 0);
		break;
	case 3:
	case 4:
		{
			int v = 50;
			if (mpDlg->m_vol.GetSafeHwnd())
				v = mpDlg->m_vol.GetPos();
			else if (og && og->m_sl.GetSafeHwnd())
				v = og->m_sl.GetPos() / 1000;
			v += (wParam == 3) ? 5 : -5;
			if (v < 0) v = 0;
			if (v > 100) v = 100;
			if (v > 0)
				g_mpRemoteMuteRestore = -1;
			if (mpDlg->m_vol.GetSafeHwnd()) {
				mpDlg->m_vol.SetPos(v);
				mpDlg->SendMessage(WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, v), (LPARAM)mpDlg->m_vol.GetSafeHwnd());
			}
			if (og && og->m_sl.GetSafeHwnd()) {
				og->m_sl.SetPos(v * 1000);
				og->PostMessage(WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, v * 1000), (LPARAM)og->m_sl.GetSafeHwnd());
			}
			MpRemoteCacheVol(v);
		}
		break;
	case 5:
		// 3秒ルール: 頭出し時は先頭から、前曲へ移る時だけ途中再開可
		{
			extern UINT ttt;
			if (plf && ps != 1 && ttt >= 300)
				OggArmRemoteSilentResumeNo();
			else
				OggArmRemoteSilentResumeYes();
		}
		mpDlg->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_MP_PREV, BN_CLICKED), 0);
		break;
	case 6: mpDlg->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_MP_STOP, BN_CLICKED), 0); break;
	case 7:
	case 8:
		if (og && ::IsWindow(og->GetSafeHwnd()) && og->m_time.GetSafeHwnd()) {
			const BOOL videoGraph = (mode == -2 || videoonly);
			if (videoGraph) {
				// Douga バーと同型: OnHScroll / RubberBand_DestroyBank を踏まない
				int mn = og->m_time.GetMinValue();
				int mx = og->m_time.GetMaxValue();
				if (mx <= mn && pMediaPosition) {
					REFTIME dur = 0;
					if (FAILED(pMediaPosition->get_Duration(&dur)) || dur <= 0.0)
						pMediaPosition->get_StopTime(&dur);
					mn = 0;
					mx = (int)(dur * 100.0);
					if (mx > mn)
						og->m_time.SetRange(mn, mx, TRUE);
				}
				if (mx <= mn) mx = mn + 1;
				int delta = (mx - mn) / 20;
				if (delta < 1) delta = 1000; // ±10s（Rew/Ff 相当）
				int pos = og->m_time.GetPos() + ((wParam == 8) ? delta : -delta);
				if (pos < mn) pos = mn;
				if (pos > mx) pos = mx;
				og->m_time.SetPos(pos);
				playb = (__int64)pos;
				poss = 0;
				poss5 = pos;
				if (pMainFrame1)
					pMainFrame1->seek((LONGLONG)((float)pos * 100000.0f));
				if (mpDlg && ::IsWindow(mpDlg->GetSafeHwnd())) {
					mpDlg->m_seekHoldPos = pos;
					mpDlg->m_seekHoldUntil = GetTickCount64() + 800;
					if (mpDlg->m_seek.GetSafeHwnd())
						mpDlg->m_seek.SetPos(pos);
				}
			} else {
				int mn = og->m_time.GetMinValue();
				int mx = og->m_time.GetMaxValue();
				if (mx <= mn) {
					mn = 0;
					mx = 1;
					og->m_time.GetRange(mn, mx);
				}
				const int span = mx - mn;
				int delta = span / 20;
				if (delta < 1) delta = 1;
				int pos = og->m_time.GetPos() + ((wParam == 8) ? delta : -delta);
				MpDjSeekToSliderPos(pos);
				// ゲームBGM＋douga: 本体音声シークに加え映像グラフも同割合で飛ばす
				if (pMainFrame1 && pMediaPosition) {
					REFTIME dur = 0;
					if (SUCCEEDED(pMediaPosition->get_Duration(&dur)) && dur > 0.0) {
						const double frac = (span > 0) ? (double)(pos - mn) / (double)span : 0.0;
						const int vpos = (int)(frac * dur * 100.0);
						pMainFrame1->seek((LONGLONG)((float)vpos * 100000.0f));
					}
				}
			}
		}
		break;
	case 9:
		{
			int v = 50;
			if (mpDlg->m_vol.GetSafeHwnd())
				v = mpDlg->m_vol.GetPos();
			else if (og && og->m_sl.GetSafeHwnd())
				v = og->m_sl.GetPos() / 1000;
			if (g_mpRemoteMuteRestore >= 0) {
				v = g_mpRemoteMuteRestore;
				g_mpRemoteMuteRestore = -1;
			} else if (v > 0) {
				g_mpRemoteMuteRestore = v;
				v = 0;
			} else {
				v = 50;
				g_mpRemoteMuteRestore = -1;
			}
			if (mpDlg->m_vol.GetSafeHwnd()) {
				mpDlg->m_vol.SetPos(v);
				mpDlg->SendMessage(WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, v), (LPARAM)mpDlg->m_vol.GetSafeHwnd());
			}
			if (og && og->m_sl.GetSafeHwnd()) {
				og->m_sl.SetPos(v * 1000);
				og->PostMessage(WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, v * 1000), (LPARAM)og->m_sl.GetSafeHwnd());
			}
			MpRemoteCacheVol(v);
		}
		break;
	case 10:
		{
			int v = (int)lParam;
			if (v < 0) v = 0;
			if (v > 100) v = 100;
			if (v > 0)
				g_mpRemoteMuteRestore = -1;
			else if (g_mpRemoteMuteRestore < 0 && mpDlg->m_vol.GetSafeHwnd()) {
				const int cur = mpDlg->m_vol.GetPos();
				if (cur > 0) g_mpRemoteMuteRestore = cur;
			}
			if (mpDlg->m_vol.GetSafeHwnd()) {
				mpDlg->m_vol.SetPos(v);
				mpDlg->SendMessage(WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, v), (LPARAM)mpDlg->m_vol.GetSafeHwnd());
			}
			if (og && og->m_sl.GetSafeHwnd()) {
				og->m_sl.SetPos(v * 1000);
				og->PostMessage(WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, v * 1000), (LPARAM)og->m_sl.GetSafeHwnd());
			}
			MpRemoteCacheVol(v);
		}
		break;
	case 11: // playidx
		{
			const int idx = (int)lParam;
			if (pl && pl->pc && idx >= 0 && idx < pl->playcnt) {
				OggArmRemoteSilentResumeYes();
				pl->Get(idx);
				plcnt = idx;
				gameon = 0;
				mpDlg->m_abApos = -1;
				mpDlg->m_abBpos = -1;
				mpDlg->m_abLoopCount = 0;
				mpDlg->m_seekHoldUntil = 0;
				if (mpDlg->m_seek.GetSafeHwnd())
					mpDlg->m_seek.SetAB(-1, -1);
				mpDlg->ClearWaveOverview();
				MpPushPlayHistory(pl->pc[idx].fol, pl->pc[idx].name);
				if (og && ::IsWindow(og->GetSafeHwnd()))
					RequestPlaybackRestart(og->GetSafeHwnd());
				mpDlg->FollowPlayingRow();
			}
		}
		break;
	case 12: // lrc delta ms
		mpDlg->ShiftLrcMs((int)lParam);
		break;
	case 13: // lrc save
		mpDlg->SendMessage(WM_COMMAND, MAKEWPARAM(ID_MP_LRC_SAVE, 0), 0);
		break;
	case 14: // eq band
		{
			const int b = ((int)lParam >> 16) & 0xFFFF;
			int v = (int)lParam & 0xFFFF;
			if (v < 0) v = 0;
			if (v > 200) v = 200;
			if (b >= 0 && b < 20) {
				savedata.eq[b] = v;
				if (b < 15)
					savedata.eqsoundeq = 9; // Custom
				MpPersistSavedataQuick();
				MpRemoteSyncEqUi();
			}
		}
		break;
	case 15: // eq preset
		{
			int p = (int)lParam;
			if (p < 0) p = 0;
			if (p > 100) p = 100;
			savedata.eqsoundeq = p;
			equaliser(0, 0, 2);
			MpPersistSavedataQuick();
			MpRemoteSyncEqUi();
		}
		break;
	case 16: // eq env
		{
			int p = (int)lParam;
			if (p < 0) p = 0;
			if (p >= MP_REMOTE_EQ_ENV_COUNT) p = MP_REMOTE_EQ_ENV_COUNT - 1;
			savedata.eqsoundenv = p;
			MpPersistSavedataQuick();
			MpRemoteSyncEqUi();
		}
		break;
	case 17: // eq fx
		{
			const int w = ((int)lParam >> 16) & 0xFFFF;
			int v = (int)lParam & 0xFFFF;
			if (v < 0) v = 0;
			if (v > 200) v = 200;
			if (w == 0) savedata.eq_reverb = v;
			else if (w == 1) savedata.eq_chorus = v;
			else if (w == 2) savedata.eq_delay = v;
			else if (w == 3) savedata.eqsoundeffect = v / 2;
			MpPersistSavedataQuick();
			MpRemoteSyncEqUi();
		}
		break;
	case 18: // equalizer reset (bands 0-14) — same as CEqualizer::OnBnClickedOk3
		{
			for (int i = 0; i < 15; ++i)
				savedata.eq[i] = 100;
			MpPersistSavedataQuick();
			MpRemoteSyncEqUi();
		}
		break;
	case 19: // global reset (15-19 + FX) — same as CEqualizer::OnBnClickedOk4
		{
			savedata.eq[15] = 100;
			savedata.eq[16] = 100;
			savedata.eq[17] = 100;
			savedata.eq[18] = 100;
			savedata.eq[19] = 100;
			savedata.eq_reverb = 0;
			savedata.eq_chorus = 0;
			savedata.eq_delay = 0;
			MpPersistSavedataQuick();
			MpRemoteSyncEqUi();
		}
		break;
	case 23: // scratch begin
		g_mpRemoteScratchLastMs = 0;
		MpDjScratchBegin();
		break;
	case 24: // scratch end
		MpDjScratchEnd();
		g_mpRemoteScratchLastMs = 0;
		break;
	case 25: // scratch delta (centidegrees)
		{
			const float d = (float)((int)lParam) * 0.01f;
			const DWORD now = GetTickCount();
			float dt = (g_mpRemoteScratchLastMs == 0) ? 0.016f : (float)(now - g_mpRemoteScratchLastMs) * 0.001f;
			g_mpRemoteScratchLastMs = now;
			if (dt < 0.004f) dt = 0.004f;
			if (dt > 0.08f) dt = 0.08f;
			MpDjScratchSetVelocity(d / dt);
			if (og && ::IsWindow(og->GetSafeHwnd()) && og->m_time.GetSafeHwnd()) {
				int mn = 0, mx = 0;
				og->m_time.GetRange(mn, mx);
				const int span = mx - mn;
				if (span > 0) {
					double units = (double)d * (double)span / 360.0 / 50.0 * (double)MpDjScratchSpeedScale();
					int delta = (int)(units >= 0.0 ? units + 0.5 : units - 0.5);
					if (delta == 0 && (d > 0.4f || d < -0.4f))
						delta = (d > 0.f) ? 1 : -1;
					int cap = span / 250;
					if (cap < 1) cap = 1;
					cap = (int)((double)cap * (double)MpDjScratchSpeedScale() + 0.5);
					if (cap < 1) cap = 1;
					if (delta > cap) delta = cap;
					if (delta < -cap) delta = -cap;
					if (delta != 0)
						MpDjSeekToSliderPos(og->m_time.GetPos() + delta);
				}
			}
		}
		break;
	case 30: // remote queue-add
		if (mpDlg)
			mpDlg->QueueAdd((int)lParam, FALSE);
		break;
	case 40: // MIDI tempo percent
		{
			int pct = (int)lParam;
			if (pct < 33) pct = 33;
			if (pct > 300) pct = 300;
			int pos;
			if (pct >= 100) pos = pct + 100;
			else pos = (int)(((double)pct - 33.3) * 3.0 + 0.5);
			if (og && og->m_tempo_sl.GetSafeHwnd()) {
				og->m_tempo_sl.SetPos(pos);
				tempo = pos;
			}
			if (mpDlg->m_tempo.GetSafeHwnd())
				mpDlg->m_tempo.SetPos(pos);
		}
		break;
	case 41: // MIDI eq low
		{
			int v = (int)lParam * 200 / 127;
			if (v < 0) v = 0; if (v > 200) v = 200;
			savedata.eq[0] = v;
			savedata.eq[1] = v;
		}
		break;
	case 42: // MIDI eq high
		{
			int v = (int)lParam * 200 / 127;
			if (v < 0) v = 0; if (v > 200) v = 200;
			savedata.eq[13] = v;
			savedata.eq[14] = v;
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
	// 日付込み（翌日の同時刻でも再発火）
	const int key = ((int)st.wYear * 10000 + (int)st.wMonth * 100 + (int)st.wDay) * 10000
		+ (int)st.wHour * 100 + (int)st.wMinute;
	if (key == g_mpAlarmLastFireKey) return;
	g_mpAlarmLastFireKey = key;
	if (plf == 1) return;
	mpDlg->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_MP_PLAY, BN_CLICKED), 0);
}

static BOOL MpPathLooksLikeVideo(LPCTSTR path)
{
	if (!path || !path[0]) return FALSE;
	LPCTSTR dot = _tcsrchr(path, _T('.'));
	if (!dot) return FALSE;
	static const LPCTSTR kExts[] = {
		_T(".mp4"), _T(".m4v"), _T(".mkv"), _T(".avi"), _T(".webm"),
		_T(".mov"), _T(".qt"), _T(".wmv"), _T(".asf"), _T(".mpg"),
		_T(".mpeg"), _T(".mpe"), _T(".m1v"), _T(".m2v"), _T(".mpv"),
		_T(".vob"), _T(".ts"), _T(".m2ts"), _T(".mts"), _T(".ogv"),
		_T(".flv"), _T(".f4v"), _T(".3gp"), _T(".3g2"), _T(".divx"),
		_T(".rm"), _T(".rmvb")
	};
	for (int i = 0; i < (int)_countof(kExts); ++i) {
		if (_tcsicmp(dot, kExts[i]) == 0)
			return TRUE;
	}
	return FALSE;
}

// ---- 動画の音声抽出 / 差し替え: Media Foundation ----
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#include "TranscodeExport.h"

static void MpVeWriteWavHeader(CFile& f, WORD ch, DWORD hz, WORD bits)
{
	BYTE h[44];
	memset(h, 0, sizeof(h));
	const WORD blockAlign = (WORD)(ch * (bits / 8));
	memcpy(h + 0, "RIFF", 4);
	*(DWORD*)(h + 4) = 0;
	memcpy(h + 8, "WAVE", 4);
	memcpy(h + 12, "fmt ", 4);
	*(DWORD*)(h + 16) = 16;
	*(WORD*)(h + 20) = WAVE_FORMAT_PCM;
	*(WORD*)(h + 22) = ch;
	*(DWORD*)(h + 24) = hz;
	*(DWORD*)(h + 28) = hz * blockAlign;
	*(WORD*)(h + 32) = blockAlign;
	*(WORD*)(h + 34) = bits;
	memcpy(h + 36, "data", 4);
	*(DWORD*)(h + 40) = 0;
	f.Write(h, 44);
}

static void MpVeFinalizeWavHeader(CFile& f, WORD ch, WORD bits)
{
	const ULONGLONG fileLen = f.GetLength();
	ULONGLONG dataBytes = (fileLen > 44) ? (fileLen - 44) : 0;
	if (dataBytes > 0xFFFFFFFFu) dataBytes = 0xFFFFFFFFu;
	DWORD riffSize = (DWORD)((fileLen > 8) ? (fileLen - 8) : 0);
	if (riffSize > 0xFFFFFFFFu - 8) riffSize = 0xFFFFFFFFu;
	f.SeekToBegin();
	BYTE riff[8];
	memcpy(riff, "RIFF", 4);
	*(DWORD*)(riff + 4) = riffSize;
	f.Write(riff, 8);
	f.Seek(40, CFile::begin);
	DWORD ds = (DWORD)dataBytes;
	f.Write(&ds, 4);
	(void)ch; (void)bits;
}

static BOOL MpExtractVideoAudioToWav(LPCTSTR videoPath, LPCTSTR wavPath, CString* errOut)
{
	if (errOut) errOut->Empty();
	if (!videoPath || !videoPath[0] || !wavPath || !wavPath[0]) return FALSE;

	HRESULT hrCo = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	HRESULT hr = MFStartup(MF_VERSION);
	if (FAILED(hr)) {
		if (errOut) *errOut = L"MFStartup failed";
		if (SUCCEEDED(hrCo) || hrCo == S_FALSE) CoUninitialize();
		return FALSE;
	}

	IMFSourceReader* reader = NULL;
	hr = MFCreateSourceReaderFromURL(videoPath, NULL, &reader);
	if (FAILED(hr) || !reader) {
		CString url;
		url.Format(_T("file:///%s"), videoPath);
		url.Replace(_T('\\'), _T('/'));
		hr = MFCreateSourceReaderFromURL(url, NULL, &reader);
	}
	BOOL ok = FALSE;
	if (SUCCEEDED(hr) && reader) {
		reader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
		reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);

		IMFMediaType* typ = NULL;
		if (SUCCEEDED(MFCreateMediaType(&typ)) && typ) {
			typ->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
			typ->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
			typ->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
			hr = reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, typ);
			typ->Release();
		}
		UINT32 ch = 0;
		UINT32 hz = 0;
		UINT32 bitDepth = 16;
		UINT32 bps = 0;
		UINT32 blkAlign = 0;
		IMFMediaType* cur = NULL;
		if (SUCCEEDED(hr) && SUCCEEDED(reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &cur)) && cur) {
			cur->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch);
			cur->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &hz);
			cur->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bitDepth);
			cur->GetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, &blkAlign);
			cur->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &bps);
			cur->Release();
		}
		if (ch < 1) ch = 2;
		if (ch > 8) ch = 2;
		if (hz < 8000) hz = 44100;
		if (bitDepth != 8 && bitDepth != 16 && bitDepth != 24 && bitDepth != 32) bitDepth = 16;
		if (blkAlign < 1) blkAlign = ch * (bitDepth / 8);
		if (bps < 1) bps = hz * blkAlign;

		if (SUCCEEDED(hr) && ch >= 1 && hz >= 8000) {
			CFile f;
			if (f.Open(wavPath, CFile::modeCreate | CFile::modeReadWrite | CFile::typeBinary | CFile::shareExclusive)) {
				MpVeWriteWavHeader(f, (WORD)ch, hz, (WORD)bitDepth);
				ok = TRUE;
				for (;;) {
					DWORD streamIndex = 0, flags = 0;
					LONGLONG ts = 0;
					IMFSample* sample = NULL;
					hr = reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
						0, &streamIndex, &flags, &ts, &sample);
					if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
						if (sample) sample->Release();
						break;
					}
					if (!sample) continue;
					IMFMediaBuffer* buf = NULL;
					if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buf)) && buf) {
						BYTE* p = NULL;
						DWORD maxLen = 0, curLen = 0;
						if (SUCCEEDED(buf->Lock(&p, &maxLen, &curLen)) && p && curLen > 0) {
							f.Write(p, curLen);
							buf->Unlock();
						}
						buf->Release();
					}
					sample->Release();
				}
				MpVeFinalizeWavHeader(f, (WORD)ch, (WORD)bitDepth);
				const ULONGLONG len = f.GetLength();
				f.Close();
				if (len <= 44) {
					ok = FALSE;
					::DeleteFile(wavPath);
					if (errOut) *errOut = LL14(
						L"音声ストリームがありません。", L"No audio stream.", L"Pas de piste audio.", L"Nessuna traccia audio.", L"Sin pista de audio.",
						L"오디오 스트림 없음.", L"没有音轨。", L"لا يوجد مسار صوت.", L"Нет аудиопотока.", L"Kein Audiostream.",
						L"Sem faixa de audio.", L"Geen audiostream.", L"Brak strumienia audio.", L"Ses akisi yok.");
				}
			} else if (errOut) {
				*errOut = LL14(
					L"出力ファイルを作成できません。", L"Cannot create output file.", L"Impossible de creer le fichier.", L"Impossibile creare il file.", L"No se puede crear el archivo.",
					L"출력 파일을 만들 수 없습니다.", L"无法创建输出文件。", L"تعذر إنشاء الملف.", L"Не удалось создать файл.", L"Ausgabedatei nicht erstellbar.",
					L"Nao foi possivel criar o arquivo.", L"Kan uitvoerbestand niet maken.", L"Nie mozna utworzyc pliku.", L"Cikti dosyasi olusturulamadi.");
			}
		} else {
			if (errOut) *errOut = LL14(
				L"音声をデコードできません（コーデック未対応の可能性）。", L"Cannot decode audio (codec may be unsupported).", L"Decodage audio impossible.", L"Impossibile decodificare l'audio.", L"No se puede decodificar el audio.",
				L"오디오를 디코드할 수 없습니다.", L"无法解码音频。", L"تعذر فك الصوت.", L"Не удалось декодировать аудио.", L"Audio nicht dekodierbar.",
				L"Nao foi possivel decodificar o audio.", L"Kan audio niet decoderen.", L"Nie mozna zdekodowac audio.", L"Ses cozulemedi.");
		}
	} else if (errOut) {
		*errOut = LL14(
			L"動画を開けません。", L"Cannot open video.", L"Impossible d'ouvrir la video.", L"Impossibile aprire il video.", L"No se puede abrir el video.",
			L"동영상을 열 수 없습니다.", L"无法打开视频。", L"تعذر فتح الفيديو.", L"Не удалось открыть видео.", L"Video nicht oeffenbar.",
			L"Nao foi possivel abrir o video.", L"Kan video niet openen.", L"Nie mozna otworzyc wideo.", L"Video acilamadi.");
	}
	if (reader) reader->Release();
	MFShutdown();
	if (SUCCEEDED(hrCo) || hrCo == S_FALSE) CoUninitialize();
	return ok;
}

void MpOnVideoExtract(CMediaPlayerDlg* mpDlg)
{
	if (!pl) return;
	CWnd* owner = NULL;
	if (mpDlg && ::IsWindow(mpDlg->GetSafeHwnd()))
		owner = mpDlg;
	else if (::IsWindow(pl->GetSafeHwnd()))
		owner = pl;
	if (!owner) return;

	int pc = -1;
	if (mpDlg && ::IsWindow(mpDlg->GetSafeHwnd()))
		pc = mpDlg->GetSelectedPcIndex();
	if (pc < 0 || pc >= pl->playcnt) {
		if (pl->pnt1 >= 0 && pl->pnt1 < pl->playcnt)
			pc = pl->pnt1;
		else if (::IsWindow(pl->m_lc.GetSafeHwnd())) {
			const int sel = pl->m_lc.GetNextItem(-1, LVNI_SELECTED);
			if (sel >= 0 && sel < pl->playcnt)
				pc = sel;
		}
	}
	if (pc < 0 || pc >= pl->playcnt) return;
	playlistdata0& item = pl->pc[pc];
	const CString title = LL14(L"音声抽出", L"Extract audio", L"Extraire audio", L"Estrai audio", L"Extraer audio",
		L"오디오 추출", L"提取音频", L"استخراج الصوت", L"Извлечь аудио", L"Audio extrahieren",
		L"Extrair audio", L"Audio extraheren", L"Wyodrebnij audio", L"Ses cikar");
	if (!MpPathLooksLikeVideo(item.fol) && item.sub != -2) {
		owner->MessageBox(
			LL14(L"動画ファイルを選択してください。", L"Select a video file.", L"Selectionnez une video.", L"Seleziona un video.", L"Seleccione un video.",
				L"동영상 파일을 선택하세요.", L"请选择视频文件。", L"اختر ملف فيديو.", L"Выберите видеофайл.", L"Video waehlen.",
				L"Selecione um video.", L"Selecteer video.", L"Wybierz plik wideo.", L"Video dosyasi secin."),
			title, MB_OK);
		return;
	}

	CString base = item.fol;
	const int dot = base.ReverseFind(_T('.'));
	if (dot > 0) base = base.Left(dot);
	CString defName = base + _T(".wav");
	const int slash = defName.ReverseFind(_T('\\'));
	CString initDir, initFile;
	if (slash >= 0) {
		initDir = defName.Left(slash);
		initFile = defName.Mid(slash + 1);
	} else {
		initFile = defName;
	}

	CFileDialog dlg(FALSE, _T("wav"), initFile,
		OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST,
		_T("WAV (*.wav)|*.wav|MP3 (*.mp3)|*.mp3|FLAC (*.flac)|*.flac||"),
		owner);
	if (!initDir.IsEmpty())
		dlg.m_ofn.lpstrInitialDir = initDir;
	if (dlg.DoModal() != IDOK)
		return;

	CString out = dlg.GetPathName();
	CString ext = out;
	const int od = ext.ReverseFind(_T('.'));
	if (od >= 0) ext = ext.Mid(od);
	else ext.Empty();
	ext.MakeLower();

	enum { kFmtWav = 0, kFmtMp3 = 1, kFmtFlac = 2 };
	int fmt = kFmtWav;
	const int filterIdx = (int)dlg.m_ofn.nFilterIndex;
	if (ext == _T(".mp3") || (ext.IsEmpty() && filterIdx == 2)) fmt = kFmtMp3;
	else if (ext == _T(".flac") || (ext.IsEmpty() && filterIdx == 3)) fmt = kFmtFlac;
	else if (ext == _T(".wav") || filterIdx == 1) fmt = kFmtWav;
	else if (filterIdx == 2) fmt = kFmtMp3;
	else if (filterIdx == 3) fmt = kFmtFlac;

	if (ext.IsEmpty()) {
		if (fmt == kFmtMp3) out += _T(".mp3");
		else if (fmt == kFmtFlac) out += _T(".flac");
		else out += _T(".wav");
	} else if (fmt == kFmtWav && ext != _T(".wav") && ext != _T(".mp3") && ext != _T(".flac")) {
		out += _T(".wav");
		fmt = kFmtWav;
	}

	CString wavPath = out;
	CString tempWav;
	if (fmt != kFmtWav) {
		wchar_t tmpDir[MAX_PATH] = {};
		GetTempPath(MAX_PATH, tmpDir);
		tempWav.Format(L"%sogg_ve_%u_%u.wav", tmpDir, GetCurrentProcessId(), GetTickCount());
		wavPath = tempWav;
	}

	CWaitCursor wait;
	CString err;
	if (!MpExtractVideoAudioToWav(item.fol, wavPath, &err)) {
		owner->MessageBox(err.IsEmpty()
			? LL14(L"抽出に失敗しました。", L"Extract failed.", L"Echec extraction.", L"Estrazione fallita.", L"Extraccion fallida.",
				L"추출 실패.", L"提取失败。", L"فشل الاستخراج.", L"Ошибка извлечения.", L"Extraktion fehlgeschlagen.",
				L"Falha na extracao.", L"Extractie mislukt.", L"Ekstrakcja nie powiodla sie.", L"Cikarma basarisiz.")
			: err,
			title, MB_OK | MB_ICONWARNING);
		if (!tempWav.IsEmpty()) ::DeleteFile(tempWav);
		return;
	}

	BOOL ok = TRUE;
	if (fmt == kFmtMp3) {
		ok = EncodeWavToMp3(wavPath, out, 192);
		::DeleteFile(wavPath);
	} else if (fmt == kFmtFlac) {
		ok = EncodeWavToFlac(wavPath, out, 5);
		::DeleteFile(wavPath);
	}
	if (!ok) {
		owner->MessageBox(
			LL14(L"エンコードに失敗しました。", L"Encode failed.", L"Echec encodage.", L"Codifica fallita.", L"Codificacion fallida.",
				L"인코딩 실패.", L"编码失败。", L"فشل الترميز.", L"Ошибка кодирования.", L"Kodierung fehlgeschlagen.",
				L"Falha na codificacao.", L"Coderen mislukt.", L"Kodowanie nie powiodlo sie.", L"Kodlama basarisiz."),
			title, MB_OK | MB_ICONWARNING);
		return;
	}
}

// ---- 動画の音声差し替え: 映像は MF SourceReader、音声は外部 WAV → MP4(H264+AAC) ----
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

void MpOnGameCapturePreset(CMediaPlayerDlg* mpDlg, UINT presetCmd)
{
	if (!mpDlg || !::IsWindow(mpDlg->GetSafeHwnd()))
		return;

	int canvas = 2; // 1080
	int fps = 60;
	BOOL openWav = FALSE;
	if (presetCmd == ID_MP_GCP_720_60) { canvas = 1; fps = 60; }
	else if (presetCmd == ID_MP_GCP_1080_60 || presetCmd == ID_MP_GAME_PRESET) { canvas = 2; fps = 60; }
	else if (presetCmd == ID_MP_GCP_1080_120) { canvas = 2; fps = 120; }
	else if (presetCmd == ID_MP_GCP_4K_60) { canvas = 4; fps = 60; }
	else if (presetCmd == ID_MP_GCP_PLUS_WAV) { canvas = 2; fps = 60; openWav = TRUE; }
	else
		return;

	savedata.cap_include_mp = 0;
	savedata.cap_with_audio = 1;
	savedata.cap_with_mic = 0;
	savedata.cap_fps = fps;
	savedata.cap_canvas_preset = canvas;
	if (canvas == 1) { savedata.cap_canvas_w = 1280; savedata.cap_canvas_h = 720; }
	else if (canvas == 2) { savedata.cap_canvas_w = 1920; savedata.cap_canvas_h = 1080; }
	else if (canvas == 4) { savedata.cap_canvas_w = 3840; savedata.cap_canvas_h = 2160; }
	else { savedata.cap_canvas_w = 1920; savedata.cap_canvas_h = 1080; }
	// ゲーム映像は素のまま（配線FXオフ）
	savedata.cap_fx_n = 0;
	savedata.cap_fx0 = savedata.cap_fx1 = savedata.cap_fx2 = savedata.cap_fx3 = 0;
	savedata.cap_fx4 = savedata.cap_fx5 = savedata.cap_fx6 = savedata.cap_fx7 = 0;
	savedata.cap_effect = 0;
	for (int i = 0; i < 8; ++i)
		for (int s = 0; s < 8; ++s)
			savedata.cap_fx_str[i][s] = 4; // 中立強度（SC_FX_STR_DEF 相当）
	MpPersistSavedataQuick();

	OpenScreenCaptureModeless(mpDlg);
	ScreenCaptureApplySavedataToUi(TRUE);

	if (openWav)
		OpenDeviceRecordModeless(mpDlg);
}

void MpOnMidiInToggle(CMediaPlayerDlg* mpDlg)
{
	const BOOL on = (g_mpMidiIn == NULL);
	MpMidiInSetActive(on, mpDlg ? mpDlg->GetSafeHwnd() : NULL);
}

// DJパッド: プロンプトDSLではなくスライダーを直接 ±%。MpPromptExecute("pitch +3") は文法不一致で無効果だった。
static int MpDjPitchTempoSlFromPercent(int pct)
{
	if (pct < 33) pct = 33;
	if (pct > 300) pct = 300;
	if (pct >= 100)
		return pct + 100;
	return (int)(((double)pct - 33.3) * 3.0 + 0.5);
}

static void MpDjApplyPitchTempoDelta(BOOL isPitch, int deltaPct)
{
	if (!og || !::IsWindow(og->GetSafeHwnd())) return;
	CCustomSliderCtrl& sl = isPitch ? og->m_pitch_sl : og->m_tempo_sl;
	if (!sl.GetSafeHwnd()) return;
	const int curPct = (int)TempoPercentFromPos(sl.GetPos());
	const int pos = MpDjPitchTempoSlFromPercent(curPct + deltaPct);
	sl.SetPos(pos);
	if (isPitch) {
		pitch = pos;
		if (og->m_pitch.GetSafeHwnd()) {
			CString s;
			s.Format(L"%3d%%", (int)TempoPercentFromPos(pos));
			og->m_pitch.SetWindowText(s);
		}
		if (mp && ::IsWindow(mp->GetSafeHwnd()) && mp->m_pitch.GetSafeHwnd())
			mp->m_pitch.SetPos(pos);
	}
	else {
		tempo = pos;
		if (og->m_temp_num.GetSafeHwnd()) {
			CString s;
			s.Format(L"%3d%%", (int)TempoPercentFromPos(pos));
			og->m_temp_num.SetWindowText(s);
		}
		if (mp && ::IsWindow(mp->GetSafeHwnd()) && mp->m_tempo.GetSafeHwnd())
			mp->m_tempo.SetPos(pos);
	}
}

// ---- DJ Vinyl (レコード盤) : ドラッグ／ホイールでスクラッチ・シーク ----
// アクリル下では OpaqueFixer が WM_PAINT を横取りするため、
// 自前 Opaque 描画 + WM_PRINTCLIENT、および fixer 除外が必要。

static void MpDjSeekToSliderPos(int pos)
{
	if (!og || !::IsWindow(og->GetSafeHwnd()) || !og->m_time.GetSafeHwnd()) return;
	int mn = 0, mx = 0;
	og->m_time.GetRange(mn, mx);
	if (mx <= mn) return;
	if (pos < mn) pos = mn;
	if (pos > mx) pos = mx;
	extern int hsc;
	if (hsc == 0) hsc = 1;
	og->m_time.SetPos(pos);
	og->SendMessage(WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, pos), (LPARAM)og->m_time.GetSafeHwnd());
	if (hsc == 1) hsc = 0;
	if (mp && ::IsWindow(mp->GetSafeHwnd())) {
		mp->m_seekHoldPos = pos;
		mp->m_seekHoldUntil = GetTickCount64() + 800;
		if (mp->m_seek.GetSafeHwnd())
			mp->m_seek.SetPos(pos);
	}
}

static float MpDjNormDeg(float deg)
{
	while (deg < 0.f) deg += 360.f;
	while (deg >= 360.f) deg -= 360.f;
	return deg;
}

// 画面角度(atan2) → 12時=0・時計回り 0..360
static float MpDjPointerDegFromPoint(CPoint pt, CPoint c)
{
	const float rad = (float)atan2((double)(pt.y - c.y), (double)(pt.x - c.x));
	return MpDjNormDeg((float)(rad * 180.0 / 3.14159265358979323846) + 90.f);
}

// ---- DJ Scratch: 再生PCMリングを速度追従で擦る（本場は曲そのものが鳴る） ----
enum { kDjOutRate = 22050, kDjRingCap = 22050, kDjBufSamples = 512, kDjBufCount = 3 };
// 33.3rpm ≒ 200°/s を等速再生の基準にする
static const float kDjVinylDegPerSec = 200.f;

static float g_djRing[kDjRingCap];
static volatile LONG g_djRingWrite = 0;
static volatile LONG g_djRingCount = 0;
static int g_djDecimNeed = 0;
static int g_djDecimCnt = 0;
static float g_djDecimSum = 0.f;
static int g_djCapRate = 0;

static HWAVEOUT g_djWo = NULL;
static WAVEHDR g_djHdr[kDjBufCount];
static short g_djPcm[kDjBufCount][kDjBufSamples];
static CRITICAL_SECTION g_djCs;
static int g_djCsInit = 0;
static volatile LONG g_djActive = 0;
static volatile LONG g_djHoldCap = 0;
static volatile LONG g_djOutGen = 0; // Shutdown のたびに加算（Write 競合防止）
static volatile LONG g_djVelBits = 0; // float bits of deg/s
static double g_djReadAge = 0.0; // 0=最新端, 大きいほど過去
static float g_djHpZ = 0.f;
static float g_djOutGain = 0.f;

static void MpDjScratchCsEnsure()
{
	if (g_djCsInit) return;
	InitializeCriticalSection(&g_djCs);
	g_djCsInit = 1;
}

static void MpDjScratchCapturePcm(const float* L, const float* R, int frames, int sampleRate)
{
	if (!L || !R || frames <= 0 || sampleRate < 8000) return;
	if (InterlockedCompareExchange(&g_djHoldCap, 0, 0) != 0) return;

	if (g_djCapRate != sampleRate || g_djDecimNeed < 1) {
		g_djCapRate = sampleRate;
		g_djDecimNeed = sampleRate / kDjOutRate;
		if (g_djDecimNeed < 1) g_djDecimNeed = 1;
		g_djDecimCnt = 0;
		g_djDecimSum = 0.f;
	}

	for (int i = 0; i < frames; ++i) {
		g_djDecimSum += 0.5f * (L[i] + R[i]);
		g_djDecimCnt++;
		if (g_djDecimCnt < g_djDecimNeed) continue;
		const float s = g_djDecimSum / (float)g_djDecimCnt;
		g_djDecimSum = 0.f;
		g_djDecimCnt = 0;
		const LONG w = InterlockedIncrement(&g_djRingWrite) - 1;
		g_djRing[w % kDjRingCap] = s;
		LONG c = g_djRingCount;
		if (c < kDjRingCap) {
			c++;
			InterlockedExchange(&g_djRingCount, c);
		}
	}
}

static float MpDjScratchReadVel()
{
	LONG bits = InterlockedCompareExchange(&g_djVelBits, 0, 0);
	float v;
	memcpy(&v, &bits, sizeof(v));
	return v;
}

static float MpDjScratchEffectGain()
{
	int e = savedata.mpDjScratchEffect;
	if (e < 0) e = 0;
	if (e > 200) e = 200;
	return (float)e / 100.f;
}

static float MpDjScratchSpeedScale()
{
	int s = savedata.mpDjScratchSpeed;
	if (s < 0) s = 0;
	if (s > 200) s = 200;
	if (s < 5) s = 5; // 完全停止は避け極低速に
	return (float)s / 100.f;
}

static void MpDjScratchSetVelocity(float degPerSec)
{
	degPerSec *= MpDjScratchSpeedScale();
	if (degPerSec > 1600.f) degPerSec = 1600.f;
	if (degPerSec < -1600.f) degPerSec = -1600.f;
	LONG bits;
	memcpy(&bits, &degPerSec, sizeof(bits));
	InterlockedExchange(&g_djVelBits, bits);
}

static float MpDjScratchRingAt(LONG write, LONG count, double age)
{
	if (count < 2) return 0.f;
	if (age < 0.0) age = 0.0;
	if (age > (double)(count - 2)) age = (double)(count - 2);
	const int i0 = (int)age;
	const float frac = (float)(age - (double)i0);
	// age=0 → 最新 = write-1
	const LONG newest = write - 1;
	const LONG idx0 = newest - i0;
	const LONG idx1 = idx0 - 1;
	const float a = g_djRing[((idx0 % kDjRingCap) + kDjRingCap) % kDjRingCap];
	const float b = g_djRing[((idx1 % kDjRingCap) + kDjRingCap) % kDjRingCap];
	return a + (b - a) * frac;
}

static void MpDjScratchFill(short* dst, int n)
{
	if (!dst || n <= 0) return;
	const LONG active = InterlockedCompareExchange(&g_djActive, 0, 0);
	const LONG write = InterlockedCompareExchange(&g_djRingWrite, 0, 0);
	const LONG count = InterlockedCompareExchange(&g_djRingCount, 0, 0);
	float vel = MpDjScratchReadVel();
	// 速度を少し平滑（マウス量子化のガタを抑える）
	static float s_velSm = 0.f;
	s_velSm += 0.35f * (vel - s_velSm);
	vel = s_velSm;
	float speed = vel / kDjVinylDegPerSec;
	if (speed > 4.f) speed = 4.f;
	if (speed < -4.f) speed = -4.f;
	if (!active) speed = 0.f;

	const float targetGain = (active && (speed > 0.04f || speed < -0.04f) && count > 64) ? 1.f : 0.f;
	for (int i = 0; i < n; ++i) {
		g_djOutGain += 0.08f * (targetGain - g_djOutGain);
		float x = 0.f;
		if (g_djOutGain > 0.001f && count > 2) {
			x = MpDjScratchRingAt(write, count, g_djReadAge);
			g_djReadAge -= (double)speed;
			if (g_djReadAge < 0.0) g_djReadAge = 0.0;
			if (g_djReadAge > (double)(count - 2)) g_djReadAge = (double)(count - 2);
			// 針先っぽい帯域: 軽いHPF + ソフトクリップ（ノイズではなく楽曲成分）
			const float hp = x - g_djHpZ;
			g_djHpZ += 0.18f * (x - g_djHpZ);
			float y = hp * 1.35f + x * 0.45f;
			if (y > 1.2f) y = 1.2f + 0.2f * (y - 1.2f);
			if (y < -1.2f) y = -1.2f + 0.2f * (y + 1.2f);
			y = y * (0.7f + 0.3f * (float)fabs((double)speed));
			x = y * g_djOutGain * 0.85f * MpDjScratchEffectGain();
		} else {
			g_djHpZ *= 0.9f;
		}
		int v = (int)(x * 32767.f);
		if (v > 32767) v = 32767;
		if (v < -32768) v = -32768;
		dst[i] = (short)v;
	}
}

static void CALLBACK MpDjScratchWoProc(HWAVEOUT hwo, UINT uMsg, DWORD_PTR, DWORD_PTR dw1, DWORD_PTR)
{
	if (uMsg != WOM_DONE || !hwo || !dw1) return;
	WAVEHDR* hdr = (WAVEHDR*)dw1;
	if (!(hdr->dwFlags & WHDR_PREPARED)) return;
	// waveOutWrite は CS 外。Shutdown の waveOutReset がコールバック完了待ちのとき
	// 同 CS で詰まるとプロセスが残留する。
	BOOL doWrite = FALSE;
	LONG gen = 0;
	MpDjScratchCsEnsure();
	EnterCriticalSection(&g_djCs);
	if (g_djWo == hwo) {
		MpDjScratchFill((short*)hdr->lpData, kDjBufSamples);
		hdr->dwFlags &= ~WHDR_DONE;
		doWrite = TRUE;
		gen = g_djOutGen;
	}
	LeaveCriticalSection(&g_djCs);
	if (doWrite && InterlockedCompareExchange(&g_djOutGen, 0, 0) == gen)
		waveOutWrite(hwo, hdr, sizeof(WAVEHDR));
}

static void MpDjScratchShutdown()
{
	MpDjScratchCsEnsure();
	EnterCriticalSection(&g_djCs);
	InterlockedExchange(&g_djActive, 0);
	InterlockedExchange(&g_djHoldCap, 0);
	MpDjScratchSetVelocity(0.f);
	// CS 保持中に waveOutReset/Close すると、WOM_DONE コールバックの
	// EnterCriticalSection と相互待ちでデッドロックする。先に所有権を外す。
	InterlockedIncrement(&g_djOutGen);
	HWAVEOUT wo = g_djWo;
	g_djWo = NULL;
	LeaveCriticalSection(&g_djCs);
	if (!wo) return;
	waveOutReset(wo);
	for (int i = 0; i < kDjBufCount; ++i) {
		if (g_djHdr[i].dwFlags & WHDR_PREPARED)
			waveOutUnprepareHeader(wo, &g_djHdr[i], sizeof(WAVEHDR));
		ZeroMemory(&g_djHdr[i], sizeof(WAVEHDR));
	}
	waveOutClose(wo);
}

static BOOL MpDjScratchEnsureOut()
{
	MpDjScratchCsEnsure();
	EnterCriticalSection(&g_djCs);
	if (g_djWo) {
		LeaveCriticalSection(&g_djCs);
		return TRUE;
	}
	WAVEFORMATEX wfx = {};
	wfx.wFormatTag = WAVE_FORMAT_PCM;
	wfx.nChannels = 1;
	wfx.nSamplesPerSec = kDjOutRate;
	wfx.wBitsPerSample = 16;
	wfx.nBlockAlign = 2;
	wfx.nAvgBytesPerSec = kDjOutRate * 2;
	HWAVEOUT wo = NULL;
	if (waveOutOpen(&wo, WAVE_MAPPER, &wfx, (DWORD_PTR)MpDjScratchWoProc, 0, CALLBACK_FUNCTION) != MMSYSERR_NOERROR) {
		LeaveCriticalSection(&g_djCs);
		return FALSE;
	}
	g_djWo = wo;
	const LONG gen = g_djOutGen;
	for (int i = 0; i < kDjBufCount; ++i) {
		ZeroMemory(&g_djHdr[i], sizeof(WAVEHDR));
		ZeroMemory(g_djPcm[i], sizeof(g_djPcm[i]));
		g_djHdr[i].lpData = (LPSTR)g_djPcm[i];
		g_djHdr[i].dwBufferLength = kDjBufSamples * sizeof(short);
		waveOutPrepareHeader(g_djWo, &g_djHdr[i], sizeof(WAVEHDR));
		MpDjScratchFill(g_djPcm[i], kDjBufSamples);
	}
	LeaveCriticalSection(&g_djCs);
	// 初期 Write も CS 外（Shutdown 競合時は世代／所有権で打ち切り）
	for (int i = 0; i < kDjBufCount; ++i) {
		if (InterlockedCompareExchange(&g_djOutGen, 0, 0) != gen) break;
		EnterCriticalSection(&g_djCs);
		const BOOL ok = (g_djWo == wo) && (g_djHdr[i].dwFlags & WHDR_PREPARED);
		LeaveCriticalSection(&g_djCs);
		if (!ok) break;
		waveOutWrite(wo, &g_djHdr[i], sizeof(WAVEHDR));
	}
	return TRUE;
}

static void MpDjScratchBegin()
{
	InterlockedExchange(&g_djHoldCap, 1);
	g_djReadAge = 0.0;
	g_djHpZ = 0.f;
	g_djOutGain = 0.f;
	MpDjScratchSetVelocity(0.f);
	InterlockedExchange(&g_djActive, 1);
	MpDjScratchEnsureOut();
}

static void MpDjScratchEnd()
{
	MpDjScratchSetVelocity(0.f);
	InterlockedExchange(&g_djActive, 0);
	InterlockedExchange(&g_djHoldCap, 0);
}

class CCustomDjVinylCtrl : public CStatic
{
	DECLARE_DYNAMIC(CCustomDjVinylCtrl)
public:
	CCustomDjVinylCtrl()
		: m_dragging(FALSE)
		, m_spinDeg(0.f)
		, m_lastPtrDeg(0.f)
		, m_playheadDeg(0.f)
		, m_lastMoveMs(0)
		, m_bAeroMode(FALSE)
	{}
	void SetAeroMode(BOOL b) { m_bAeroMode = b; if (GetSafeHwnd()) Invalidate(FALSE); }

	void SyncFromPlayback()
	{
		if (m_dragging) return;
		if (!og || !::IsWindow(og->GetSafeHwnd()) || !og->m_time.GetSafeHwnd()) return;
		int mn = 0, mx = 0;
		og->m_time.GetRange(mn, mx);
		const int span = mx - mn;
		if (span <= 0) {
			m_playheadDeg = 0.f;
			return;
		}
		const int pos = og->m_time.GetPos();
		m_playheadDeg = MpDjNormDeg(360.f * (float)(pos - mn) / (float)span);
		if (plf && ps != 1)
			m_spinDeg = MpDjNormDeg(m_spinDeg + 2.2f);
		if (GetSafeHwnd())
			Invalidate(FALSE);
	}

protected:
	BOOL m_dragging;
	float m_spinDeg;
	float m_lastPtrDeg;
	float m_playheadDeg;
	DWORD m_lastMoveMs;
	BOOL m_bAeroMode;

	BOOL HitDisc(CPoint pt, CPoint& center, int& radius) const
	{
		CRect rc; GetClientRect(&rc);
		center.x = (rc.left + rc.right) / 2;
		center.y = (rc.top + rc.bottom) / 2;
		radius = ((rc.Width() < rc.Height()) ? rc.Width() : rc.Height()) / 2 - 2;
		if (radius < 8) return FALSE;
		const int dx = pt.x - center.x;
		const int dy = pt.y - center.y;
		return (dx * dx + dy * dy) <= (radius * radius);
	}

	void RefreshPlayheadFromPos()
	{
		if (!og || !og->m_time.GetSafeHwnd()) return;
		int mn = 0, mx = 0;
		og->m_time.GetRange(mn, mx);
		const int span = mx - mn;
		if (span <= 0) { m_playheadDeg = 0.f; return; }
		m_playheadDeg = MpDjNormDeg(360.f * (float)(og->m_time.GetPos() - mn) / (float)span);
	}

	void ScratchByDeltaDeg(float d)
	{
		const DWORD now = GetTickCount();
		float dt = (m_lastMoveMs == 0) ? 0.016f : (float)(now - m_lastMoveMs) * 0.001f;
		m_lastMoveMs = now;
		if (dt < 0.004f) dt = 0.004f;
		if (dt > 0.08f) dt = 0.08f;
		MpDjScratchSetVelocity(d / dt);

		if (!og || !::IsWindow(og->GetSafeHwnd()) || !og->m_time.GetSafeHwnd()) return;
		int mn = 0, mx = 0;
		og->m_time.GetRange(mn, mx);
		const int span = mx - mn;
		if (span <= 0) return;
		double units = (double)d * (double)span / 360.0 / 50.0 * (double)MpDjScratchSpeedScale();
		int delta = (int)(units >= 0.0 ? units + 0.5 : units - 0.5);
		if (delta == 0 && (d > 0.4f || d < -0.4f))
			delta = (d > 0.f) ? 1 : -1;
		int cap = span / 250;
		if (cap < 1) cap = 1;
		cap = (int)((double)cap * (double)MpDjScratchSpeedScale() + 0.5);
		if (cap < 1) cap = 1;
		if (delta > cap) delta = cap;
		if (delta < -cap) delta = -cap;
		if (delta == 0) return;
		MpDjSeekToSliderPos(og->m_time.GetPos() + delta);
		RefreshPlayheadFromPos();
	}

	void NudgeSeekByWheel(int wheelSteps)
	{
		if (!og || !::IsWindow(og->GetSafeHwnd()) || !og->m_time.GetSafeHwnd()) return;
		int mn = 0, mx = 0;
		og->m_time.GetRange(mn, mx);
		const int span = mx - mn;
		if (span <= 0) return;
		int step = span / 400;
		if (step < 1) step = 1;
		MpDjSeekToSliderPos(og->m_time.GetPos() - wheelSteps * step);
		RefreshPlayheadFromPos();
	}

	void PaintVinyl(CDC& dc, const CRect& rc)
	{
		dc.FillSolidRect(&rc, RGB(28, 30, 38));
		const int cx = (rc.left + rc.right) / 2;
		const int cy = (rc.top + rc.bottom) / 2;
		const int rw = rc.Width();
		const int rh = rc.Height();
		const int R = ((rw < rh) ? rw : rh) / 2 - 2;
		if (R <= 8) {
			CCC_DrawInwoman(&dc, rc, FALSE);
			return;
		}

		CBrush vinyl(RGB(18, 18, 20));
		CPen edge(PS_SOLID, 2, RGB(55, 55, 62));
		CBrush* ob = dc.SelectObject(&vinyl);
		CPen* op = dc.SelectObject(&edge);
		dc.Ellipse(cx - R, cy - R, cx + R, cy + R);

		CPen groove(PS_SOLID, 1, RGB(32, 32, 36));
		dc.SelectObject(&groove);
		for (int i = 3; i < 18; ++i) {
			const int r = R * i / 20;
			dc.Ellipse(cx - r, cy - r, cx + r, cy + r);
		}
		CPen hi(PS_SOLID, 2, RGB(70, 72, 82));
		dc.SelectObject(&hi);
		dc.Arc(cx - R + 4, cy - R + 4, cx + R - 4, cy + R - 4,
			cx - R / 2, cy - R, cx + R / 3, cy - R / 2);

		const int lr = R / 3;
		CBrush label(RGB(190, 60, 90));
		CPen labelEdge(PS_SOLID, 1, RGB(240, 140, 160));
		dc.SelectObject(&label);
		dc.SelectObject(&labelEdge);
		dc.Ellipse(cx - lr, cy - lr, cx + lr, cy + lr);
		const int hub = (lr / 6 > 3) ? (lr / 6) : 3;
		CBrush hubBr(RGB(220, 220, 230));
		dc.SelectObject(&hubBr);
		dc.SelectStockObject(NULL_PEN);
		dc.Ellipse(cx - hub, cy - hub, cx + hub, cy + hub);

		{
			const double rad = (m_spinDeg - 90.f) * 3.14159265358979323846 / 180.0;
			const int x1 = cx + (int)((lr + 4) * cos(rad));
			const int y1 = cy + (int)((lr + 4) * sin(rad));
			const int x2 = cx + (int)((R - 6) * cos(rad));
			const int y2 = cy + (int)((R - 6) * sin(rad));
			CPen mark(PS_SOLID, 1, RGB(48, 48, 54));
			dc.SelectObject(&mark);
			dc.MoveTo(x1, y1);
			dc.LineTo(x2, y2);
		}
		{
			const double rad = (m_playheadDeg - 90.f) * 3.14159265358979323846 / 180.0;
			const int x2 = cx + (int)((R - 3) * cos(rad));
			const int y2 = cy + (int)((R - 3) * sin(rad));
			CPen needle(PS_SOLID, 3, RGB(255, 210, 120));
			dc.SelectObject(&needle);
			dc.MoveTo(cx, cy);
			dc.LineTo(x2, y2);
		}

		dc.SelectObject(ob);
		dc.SelectObject(op);
		CCC_DrawInwoman(&dc, rc, FALSE);
	}

	afx_msg void OnPaint()
	{
		CPaintDC dc(this);
		CRect rc; GetClientRect(&rc);
		if (rc.Width() <= 0 || rc.Height() <= 0) return;

		BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
		params.dwFlags = BPPF_ERASE;
		HDC hdcBuf = NULL;
		HPAINTBUFFER hBP = ::BeginBufferedPaint(dc.GetSafeHdc(), &rc, BPBF_TOPDOWNDIB, &params, &hdcBuf);
		if (hdcBuf && hBP) {
			CDC mem; mem.Attach(hdcBuf);
			PaintVinyl(mem, rc);
			mem.Detach();
			::BufferedPaintMakeOpaque(hBP, &rc);
			::EndBufferedPaint(hBP, TRUE);
			return;
		}
		PaintVinyl(dc, rc);
	}

	afx_msg LRESULT OnPrintClient(WPARAM wParam, LPARAM)
	{
		CDC* pDC = CDC::FromHandle((HDC)wParam);
		if (!pDC) return 0;
		CRect rc; GetClientRect(&rc);
		PaintVinyl(*pDC, CRect(0, 0, rc.Width(), rc.Height()));
		return 1;
	}

	afx_msg BOOL OnEraseBkgnd(CDC* pDC)
	{
		if (pDC) {
			CRect rc; GetClientRect(&rc);
			pDC->FillSolidRect(&rc, RGB(28, 30, 38));
		}
		return TRUE;
	}

	afx_msg void OnLButtonDown(UINT nFlags, CPoint point)
	{
		CPoint c; int R = 0;
		if (!HitDisc(point, c, R)) {
			CStatic::OnLButtonDown(nFlags, point);
			return;
		}
		SetCapture();
		m_dragging = TRUE;
		m_lastPtrDeg = MpDjPointerDegFromPoint(point, c);
		m_lastMoveMs = GetTickCount();
		MpDjScratchBegin();
		Invalidate(FALSE);
	}

	afx_msg void OnMouseMove(UINT nFlags, CPoint point)
	{
		if (!m_dragging) {
			CStatic::OnMouseMove(nFlags, point);
			return;
		}
		CPoint c; int R = 0;
		HitDisc(point, c, R);
		const float deg = MpDjPointerDegFromPoint(point, c);
		float d = deg - m_lastPtrDeg;
		if (d > 180.f) d -= 360.f;
		if (d < -180.f) d += 360.f;
		m_spinDeg = MpDjNormDeg(m_spinDeg + d);
		m_lastPtrDeg = deg;
		ScratchByDeltaDeg(d);
		Invalidate(FALSE);
	}

	afx_msg void OnLButtonUp(UINT nFlags, CPoint point)
	{
		if (m_dragging) {
			m_dragging = FALSE;
			ReleaseCapture();
			MpDjScratchEnd();
			m_lastMoveMs = 0;
		}
		CStatic::OnLButtonUp(nFlags, point);
	}

	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
	{
		UNREFERENCED_PARAMETER(nFlags);
		UNREFERENCED_PARAMETER(pt);
		const int steps = zDelta / WHEEL_DELTA;
		if (steps != 0)
			NudgeSeekByWheel(steps);
		Invalidate(FALSE);
		return TRUE;
	}

	DECLARE_MESSAGE_MAP()
};

IMPLEMENT_DYNAMIC(CCustomDjVinylCtrl, CStatic)

BEGIN_MESSAGE_MAP(CCustomDjVinylCtrl, CStatic)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDOWN()
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEWHEEL()
	ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
END_MESSAGE_MAP()

// ---- DJ Pad dialog ----
class CMpDjPadDlg : public CCustomBlurDialogBase
{
public:
	enum { IDD = IDD_MP_DJPAD };
	CMpDjPadDlg(CWnd* p = NULL)
		: CCustomBlurDialogBase(IDD, p)
		, m_cueMem(-1)
		, m_lastCueLit(-1)
	{}
	CCustomStandardButton m_pitchUp, m_pitchDn, m_tempoUp, m_tempoDn;
	CCustomStandardButton m_vocal, m_vocalDn, m_vocalRst;
	CCustomStandardButton m_msNarrow, m_msWide, m_msRst;
	CCustomStandardButton m_pitchRst, m_tempoRst;
	CCustomStandardButton m_play, m_pause, m_stop, m_cue, m_prev, m_next;
	CCustomStandardButton m_beatBk, m_beatFw, m_bpmDet;
	CCustomStandardButton m_cuePad[8];
	CCustomStandardButton m_cueSet, m_cueClr;
	CCustomStandardButton m_abA, m_abB, m_abClr;
	CCustomStandardButton m_loop1, m_loop2, m_loop4, m_loop8;
	CCustomStandardButton m_killL, m_killM, m_killH;
	CCustomDjVinylCtrl m_vinyl;
	CCustomStatic m_tip, m_bpm, m_status;
	CCustomStatic m_eqLowL, m_eqMidL, m_eqHighL;
	CCustomSliderCtrl m_fx, m_spd, m_filter, m_vol;
	CCustomSliderCtrl m_eqLow, m_eqMid, m_eqHigh;
	CCustomRangeSliderCtrl m_seek;
	CCustomLevelMeter m_meter;
	CCustomStatic m_micDevL;
	CCustomComboBox m_micDev;
	CCustomStandardButton m_micDevRefresh;
	CCustomStatic m_loopDevL;
	CCustomComboBox m_loopDev;
	CToolTipCtrl m_tooltip;
	int m_cueMem;
	int m_lastCueLit;
	void ApplyRemoteDeckFromSavedata();
protected:
	virtual void DoDataExchange(CDataExchange* pDX)
	{
		CCustomBlurDialogBase::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_DJPAD_PITCH_UP, m_pitchUp);
		DDX_Control(pDX, IDC_DJPAD_PITCH_DN, m_pitchDn);
		DDX_Control(pDX, IDC_DJPAD_TEMPO_UP, m_tempoUp);
		DDX_Control(pDX, IDC_DJPAD_TEMPO_DN, m_tempoDn);
		DDX_Control(pDX, IDC_DJPAD_VOCAL, m_vocal);
		DDX_Control(pDX, IDC_DJPAD_VOCAL_DN, m_vocalDn);
		DDX_Control(pDX, IDC_DJPAD_VOCAL_RST, m_vocalRst);
		DDX_Control(pDX, IDC_DJPAD_MS_NARROW, m_msNarrow);
		DDX_Control(pDX, IDC_DJPAD_MS_WIDE, m_msWide);
		DDX_Control(pDX, IDC_DJPAD_MS_RST, m_msRst);
		DDX_Control(pDX, IDC_DJPAD_PITCH_RST, m_pitchRst);
		DDX_Control(pDX, IDC_DJPAD_TEMPO_RST, m_tempoRst);
		DDX_Control(pDX, IDC_DJPAD_VINYL, m_vinyl);
		DDX_Control(pDX, IDC_DJPAD_TIP, m_tip);
		DDX_Control(pDX, IDC_DJPAD_FX, m_fx);
		DDX_Control(pDX, IDC_DJPAD_SPD, m_spd);
		DDX_Control(pDX, IDC_DJPAD_FILTER, m_filter);
		DDX_Control(pDX, IDC_DJPAD_VOL, m_vol);
		DDX_Control(pDX, IDC_DJPAD_BPM, m_bpm);
		DDX_Control(pDX, IDC_DJPAD_STATUS, m_status);
		DDX_Control(pDX, IDC_DJPAD_PLAY, m_play);
		DDX_Control(pDX, IDC_DJPAD_PAUSE, m_pause);
		DDX_Control(pDX, IDC_DJPAD_STOP, m_stop);
		DDX_Control(pDX, IDC_DJPAD_CUE, m_cue);
		DDX_Control(pDX, IDC_DJPAD_PREV, m_prev);
		DDX_Control(pDX, IDC_DJPAD_NEXT, m_next);
		DDX_Control(pDX, IDC_DJPAD_BEAT_BK, m_beatBk);
		DDX_Control(pDX, IDC_DJPAD_BEAT_FW, m_beatFw);
		DDX_Control(pDX, IDC_DJPAD_BPM_DET, m_bpmDet);
		DDX_Control(pDX, IDC_DJPAD_CUE1, m_cuePad[0]);
		DDX_Control(pDX, IDC_DJPAD_CUE2, m_cuePad[1]);
		DDX_Control(pDX, IDC_DJPAD_CUE3, m_cuePad[2]);
		DDX_Control(pDX, IDC_DJPAD_CUE4, m_cuePad[3]);
		DDX_Control(pDX, IDC_DJPAD_CUE5, m_cuePad[4]);
		DDX_Control(pDX, IDC_DJPAD_CUE6, m_cuePad[5]);
		DDX_Control(pDX, IDC_DJPAD_CUE7, m_cuePad[6]);
		DDX_Control(pDX, IDC_DJPAD_CUE8, m_cuePad[7]);
		DDX_Control(pDX, IDC_DJPAD_CUESET, m_cueSet);
		DDX_Control(pDX, IDC_DJPAD_CUECLR, m_cueClr);
		DDX_Control(pDX, IDC_DJPAD_ABA, m_abA);
		DDX_Control(pDX, IDC_DJPAD_ABB, m_abB);
		DDX_Control(pDX, IDC_DJPAD_ABCLR, m_abClr);
		DDX_Control(pDX, IDC_DJPAD_LOOP1, m_loop1);
		DDX_Control(pDX, IDC_DJPAD_LOOP2, m_loop2);
		DDX_Control(pDX, IDC_DJPAD_LOOP4, m_loop4);
		DDX_Control(pDX, IDC_DJPAD_LOOP8, m_loop8);
		DDX_Control(pDX, IDC_DJPAD_EQ_LOW, m_eqLow);
		DDX_Control(pDX, IDC_DJPAD_EQ_MID, m_eqMid);
		DDX_Control(pDX, IDC_DJPAD_EQ_HIGH, m_eqHigh);
		DDX_Control(pDX, IDC_DJPAD_EQ_LOW_L, m_eqLowL);
		DDX_Control(pDX, IDC_DJPAD_EQ_MID_L, m_eqMidL);
		DDX_Control(pDX, IDC_DJPAD_EQ_HIGH_L, m_eqHighL);
		DDX_Control(pDX, IDC_DJPAD_KILL_L, m_killL);
		DDX_Control(pDX, IDC_DJPAD_KILL_M, m_killM);
		DDX_Control(pDX, IDC_DJPAD_KILL_H, m_killH);
		DDX_Control(pDX, IDC_DJPAD_SEEK, m_seek);
		DDX_Control(pDX, IDC_DJPAD_METER, m_meter);
		DDX_Control(pDX, IDC_DJPAD_MICDEV_L, m_micDevL);
		DDX_Control(pDX, IDC_DJPAD_MICDEV, m_micDev);
		DDX_Control(pDX, IDC_DJPAD_MICDEV_REFRESH, m_micDevRefresh);
		DDX_Control(pDX, IDC_DJPAD_LOOPDEV_L, m_loopDevL);
		DDX_Control(pDX, IDC_DJPAD_LOOPDEV, m_loopDev);
	}
	virtual BOOL OnInitDialog()
	{
		CCustomBlurDialogBase::OnInitDialog();
		EnableMainWindowLock(&savedata.mpDjPadMainLock, FALSE);
		if (savedata.mpDjPadTopMost)
			SetWindowPos(&wndTopMost, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		SetWindowText(LL14(L"DJ パッド", L"DJ Pad", L"Pad DJ", L"Pad DJ", L"Pad DJ",
			L"DJ 패드", L"DJ 垫", L"لوحة DJ", L"DJ-панель", L"DJ-Pad",
			L"Pad DJ", L"DJ-pad", L"Pad DJ", L"DJ paneli"));
		m_pitchUp.SetWindowText(LL14(L"音程 +", L"Pitch +", L"Hauteur +", L"Pitch +", L"Tono +", L"음정 +", L"音高 +", L"درجة +", L"Тон +", L"Tonhöhe +", L"Tom +", L"Toonhoogte +", L"Wysokosc +", L"Perde +"));
		m_pitchDn.SetWindowText(LL14(L"音程 -", L"Pitch -", L"Hauteur -", L"Pitch -", L"Tono -", L"음정 -", L"音高 -", L"درجة -", L"Тон -", L"Tonhöhe -", L"Tom -", L"Toonhoogte -", L"Wysokosc -", L"Perde -"));
		m_tempoUp.SetWindowText(LL14(L"テンポ +", L"Tempo +", L"Tempo +", L"Tempo +", L"Tempo +", L"템포 +", L"速度 +", L"إيقاع +", L"Темп +", L"Tempo +", L"Tempo +", L"Tempo +", L"Tempo +", L"Tempo +"));
		m_tempoDn.SetWindowText(LL14(L"テンポ -", L"Tempo -", L"Tempo -", L"Tempo -", L"Tempo -", L"템포 -", L"速度 -", L"إيقاع -", L"Темп -", L"Tempo -", L"Tempo -", L"Tempo -", L"Tempo -", L"Tempo -"));
		m_pitchRst.SetWindowText(LL14(L"音程戻", L"P rst", L"P rst", L"P rst", L"P rst", L"음정복", L"音高复", L"P rst", L"P rst", L"P rst", L"P rst", L"P rst", L"P rst", L"P rst"));
		m_tempoRst.SetWindowText(LL14(L"テンポ戻", L"T rst", L"T rst", L"T rst", L"T rst", L"템포복", L"速度复", L"T rst", L"T rst", L"T rst", L"T rst", L"T rst", L"T rst", L"T rst"));
		m_vocal.SetWindowText(LL14(L"ボーカル+", L"Vocal +", L"Voix +", L"Voce +", L"Voz +", L"보컬+", L"人声+", L"صوت+", L"Вокал+", L"Gesang+", L"Vocal+", L"Zang+", L"Wokal+", L"Vokal+"));
		m_vocalDn.SetWindowText(LL14(L"ボーカル-", L"Vocal -", L"Voix -", L"Voce -", L"Voz -", L"보컬-", L"人声-", L"صوت-", L"Вокал-", L"Gesang-", L"Vocal-", L"Zang-", L"Wokal-", L"Vokal-"));
		m_vocalRst.SetWindowText(LL14(L"V 戻", L"V rst", L"V rst", L"V rst", L"V rst", L"V 복", L"V 复", L"V rst", L"V rst", L"V rst", L"V rst", L"V rst", L"V rst", L"V rst"));
		m_msNarrow.SetWindowText(LL14(L"MS 狭", L"MS Nar", L"MS etr", L"MS str", L"MS est", L"MS 좁", L"MS 窄", L"MS ضيق", L"MS узк", L"MS eng", L"MS est", L"MS smal", L"MS was", L"MS dar"));
		m_msWide.SetWindowText(LL14(L"MS 広", L"MS Wide", L"MS lar", L"MS amp", L"MS anc", L"MS 넓", L"MS 宽", L"MS واسع", L"MS шир", L"MS weit", L"MS lar", L"MS breed", L"MS sze", L"MS gen"));
		m_msRst.SetWindowText(LL14(L"MS戻", L"MS rst", L"MS rst", L"MS rst", L"MS rst", L"MS복", L"MS复", L"MS rst", L"MS rst", L"MS rst", L"MS rst", L"MS rst", L"MS rst", L"MS rst"));
		m_play.SetWindowText(LL14(L"再生", L"Play", L"Lire", L"Play", L"Play", L"재생", L"播放", L"تشغيل", L"Играть", L"Play", L"Play", L"Play", L"Odtw.", L"Oynat"));
		m_pause.SetWindowText(LL14(L"一時停止", L"Pause", L"Pause", L"Pausa", L"Pausa", L"일시정지", L"暂停", L"إيقاف", L"Пауза", L"Pause", L"Pausa", L"Pauze", L"Pauza", L"Duraklat"));
		m_stop.SetWindowText(LL14(L"停止", L"Stop", L"Stop", L"Stop", L"Stop", L"정지", L"停止", L"إيقاف", L"Стоп", L"Stop", L"Parar", L"Stop", L"Stop", L"Durdur"));
		m_cue.SetWindowText(L"CUE");
		m_prev.SetWindowText(L"<<");
		m_next.SetWindowText(L">>");
		m_beatBk.SetWindowText(LL14(L"-拍", L"-Beat", L"-Batt", L"-Beat", L"-Comp", L"-박", L"-拍", L"-نبض", L"-Доля", L"-Beat", L"-Bat", L"-Beat", L"-Beat", L"-Vurus"));
		m_beatFw.SetWindowText(LL14(L"+拍", L"+Beat", L"+Batt", L"+Beat", L"+Comp", L"+박", L"+拍", L"+نبض", L"+Доля", L"+Beat", L"+Bat", L"+Beat", L"+Beat", L"+Vurus"));
		m_bpmDet.SetWindowText(LL14(L"BPM計測", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM"));
		m_cueSet.SetWindowText(LL14(L"SET", L"SET", L"SET", L"SET", L"SET", L"SET", L"SET", L"SET", L"SET", L"SET", L"SET", L"SET", L"SET", L"SET"));
		m_cueClr.SetWindowText(LL14(L"CLR", L"CLR", L"CLR", L"CLR", L"CLR", L"CLR", L"CLR", L"CLR", L"CLR", L"CLR", L"CLR", L"CLR", L"CLR", L"CLR"));
		m_abA.SetWindowText(L"A");
		m_abB.SetWindowText(L"B");
		m_abClr.SetWindowText(LL14(L"AB消", L"ABclr", L"ABclr", L"ABclr", L"ABclr", L"AB지", L"AB清", L"ABclr", L"ABclr", L"ABclr", L"ABclr", L"ABclr", L"ABclr", L"ABclr"));
		m_loop1.SetWindowText(L"1");
		m_loop2.SetWindowText(L"2");
		m_loop4.SetWindowText(L"4");
		m_loop8.SetWindowText(L"8");
		m_killL.SetWindowText(LL14(L"Kill低", L"Kill L", L"Kill L", L"Kill L", L"Kill L", L"Kill저", L"Kill低", L"Kill L", L"Kill L", L"Kill L", L"Kill L", L"Kill L", L"Kill L", L"Kill L"));
		m_killM.SetWindowText(LL14(L"Kill中", L"Kill M", L"Kill M", L"Kill M", L"Kill M", L"Kill중", L"Kill中", L"Kill M", L"Kill M", L"Kill M", L"Kill M", L"Kill M", L"Kill M", L"Kill M"));
		m_killH.SetWindowText(LL14(L"Kill高", L"Kill H", L"Kill H", L"Kill H", L"Kill H", L"Kill고", L"Kill高", L"Kill H", L"Kill H", L"Kill H", L"Kill H", L"Kill H", L"Kill H", L"Kill H"));
		{
			static const TCHAR* cueLabs[8] = { L"C1", L"C2", L"C3", L"C4", L"C5", L"C6", L"C7", L"C8" };
			for (int i = 0; i < 8; ++i)
				m_cuePad[i].SetWindowText(cueLabs[i]);
		}
		m_tip.SetWindowText(LL14(L"ドラッグでスクラッチ", L"Drag to scratch",
			L"Glisser pour scratch", L"Trascina per scratch", L"Arrastrar para scratch",
			L"드래그로 스크래치", L"拖动刮盘", L"اسحب للخدش",
			L"Тяните для скретча", L"Ziehen zum Scratchen", L"Arrastar para scratch",
			L"Slepen om te scratchen", L"Przeciagaj aby scratch", L"Surukle scratch"));
		m_eqLowL.SetWindowText(LL14(L"低域", L"Low", L"Graves", L"Bassi", L"Graves", L"저역", L"低音", L"منخفض", L"Низ", L"Tief", L"Graves", L"Laag", L"Niskie", L"Bas"));
		m_eqMidL.SetWindowText(LL14(L"中域", L"Mid", L"Medium", L"Medi", L"Medios", L"중역", L"中音", L"وسط", L"Серед", L"Mitten", L"Medios", L"Midden", L"Srednie", L"Orta"));
		m_eqHighL.SetWindowText(LL14(L"高域", L"High", L"Aigus", L"Alti", L"Agudos", L"고역", L"高音", L"مرتفع", L"Верх", L"Höhen", L"Agudos", L"Hoog", L"Wysokie", L"Tiz"));
		SetDlgItemText(IDC_DJPAD_FX_L, LL14(L"効果", L"Effect", L"Effet", L"Effetto", L"Efecto",
			L"효과", L"效果", L"تأثير", L"Эффект", L"Effekt", L"Efeito", L"Effect", L"Efekt", L"Efekt"));
		SetDlgItemText(IDC_DJPAD_SPD_L, LL14(L"速度", L"Speed", L"Vitesse", L"Velocita", L"Velocidad",
			L"속도", L"速度", L"سرعة", L"Скорость", L"Tempo", L"Velocidade", L"Snelheid", L"Predkosc", L"Hiz"));
		SetDlgItemText(IDC_DJPAD_FILTER_L, LL14(L"フィルタ", L"Filter", L"Filtre", L"Filtro", L"Filtro",
			L"필터", L"滤镜", L"مرشح", L"Фильтр", L"Filter", L"Filtro", L"Filter", L"Filtr", L"Filtre"));
		SetDlgItemText(IDC_DJPAD_VOL_L, LL14(L"音量", L"Vol", L"Vol", L"Vol", L"Vol",
			L"볼륨", L"音量", L"صوت", L"Громк.", L"Laut", L"Vol", L"Vol", L"Glosn.", L"Ses"));
		AudioMicDevFillCombo(m_micDev);
		AudioDevApplyRescanButton(&m_micDevRefresh);
		AudioLoopDevFillCombo(m_loopDev);
		m_micDevL.SetWindowText(LL14(L"マイク", L"Mic", L"Micro", L"Micro", L"Micro", L"마이크", L"麦克风", L"ميكروفون", L"Микрофон", L"Mikrofon", L"Microfone", L"Microfoon", L"Mikrofon", L"Mikrofon"));
		m_loopDevL.SetWindowText(LL14(L"システム", L"System", L"Système", L"Sistema", L"Sistema", L"시스템", L"系统", L"النظام", L"Система", L"System", L"Sistema", L"Systeem", L"System", L"Sistem"));
		{
			int fx = savedata.mpDjScratchEffect;
			int spd = savedata.mpDjScratchSpeed;
			int filt = savedata.mpDjFilter;
			int low = savedata.mpDjEqLow;
			int mid = savedata.mpDjEqMid;
			int high = savedata.mpDjEqHigh;
			if (fx < 0 || fx > 200) fx = 100;
			if (spd < 0 || spd > 200) spd = 100;
			if (filt < 0 || filt > 200) filt = 100;
			if (low < 0 || low > 200) low = 100;
			if (mid < 0 || mid > 200) mid = 100;
			if (high < 0 || high > 200) high = 100;
			savedata.mpDjScratchEffect = fx;
			savedata.mpDjScratchSpeed = spd;
			savedata.mpDjFilter = filt;
			savedata.mpDjEqLow = low;
			savedata.mpDjEqMid = mid;
			savedata.mpDjEqHigh = high;
			m_fx.SetRange(0, 200); m_fx.SetPos(fx);
			m_spd.SetRange(0, 200); m_spd.SetPos(spd);
			m_filter.SetRange(0, 200); m_filter.SetPos(filt);
			m_eqLow.SetRange(0, 200); m_eqLow.SetPos(200 - low); m_eqLow.SetMode(1);
			m_eqMid.SetRange(0, 200); m_eqMid.SetPos(200 - mid); m_eqMid.SetMode(1);
			m_eqHigh.SetRange(0, 200); m_eqHigh.SetPos(200 - high); m_eqHigh.SetMode(1);
			int vol = 50;
			if (mp && ::IsWindow(mp->GetSafeHwnd()) && mp->m_vol.GetSafeHwnd())
				vol = mp->m_vol.GetPos();
			else if (og && og->m_sl.GetSafeHwnd())
				vol = og->m_sl.GetPos() / 1000;
			if (vol < 0) vol = 0; if (vol > 100) vol = 100;
			m_vol.SetRange(0, 100); m_vol.SetPos(vol);
		}
		m_seek.SetSelectionLocked(TRUE);
		if (og && og->m_time.GetSafeHwnd()) {
			int mn = 0, mx = 1;
			og->m_time.GetRange(mn, mx);
			m_seek.SetRange(mn, mx);
			m_seek.SetPos(og->m_time.GetPos());
		}
		m_pitchUp.SetGradation(RGB(220, 240, 255), RGB(170, 210, 250), 0, TRUE);
		m_pitchDn.SetGradation(RGB(220, 240, 255), RGB(170, 210, 250), 0, TRUE);
		m_tempoUp.SetGradation(RGB(220, 245, 230), RGB(170, 220, 190), 0, TRUE);
		m_tempoDn.SetGradation(RGB(220, 245, 230), RGB(170, 220, 190), 0, TRUE);
		m_pitchRst.SetGradation(RGB(230, 230, 240), RGB(190, 190, 210), 0, TRUE);
		m_tempoRst.SetGradation(RGB(230, 230, 240), RGB(190, 190, 210), 0, TRUE);
		m_vocal.SetGradation(RGB(255, 230, 245), RGB(255, 180, 220), 0, TRUE);
		m_vocalDn.SetGradation(RGB(255, 230, 245), RGB(255, 180, 220), 0, TRUE);
		m_vocalRst.SetGradation(RGB(240, 230, 240), RGB(210, 190, 210), 0, TRUE);
		m_msNarrow.SetGradation(RGB(235, 230, 255), RGB(200, 185, 250), 0, TRUE);
		m_msWide.SetGradation(RGB(235, 230, 255), RGB(200, 185, 250), 0, TRUE);
		m_msRst.SetGradation(RGB(230, 230, 240), RGB(190, 190, 210), 0, TRUE);
		m_play.SetGradation(RGB(200, 240, 200), RGB(140, 210, 150), 0, TRUE);
		m_pause.SetGradation(RGB(255, 240, 200), RGB(240, 200, 120), 0, TRUE);
		m_stop.SetGradation(RGB(255, 210, 210), RGB(230, 140, 140), 0, TRUE);
		m_cue.SetGradation(RGB(255, 230, 180), RGB(240, 180, 100), 0, TRUE);
		m_prev.SetGradation(RGB(220, 230, 245), RGB(170, 190, 230), 0, TRUE);
		m_next.SetGradation(RGB(220, 230, 245), RGB(170, 190, 230), 0, TRUE);
		m_beatBk.SetGradation(RGB(230, 245, 255), RGB(170, 210, 240), 0, TRUE);
		m_beatFw.SetGradation(RGB(230, 245, 255), RGB(170, 210, 240), 0, TRUE);
		m_bpmDet.SetGradation(RGB(220, 255, 240), RGB(150, 220, 190), 0, TRUE);
		for (int i = 0; i < 8; ++i)
			m_cuePad[i].SetGradation(RGB(255, 245, 220), RGB(240, 200, 140), 0, TRUE);
		m_cueSet.SetGradation(RGB(230, 255, 230), RGB(160, 220, 160), 0, TRUE);
		m_cueClr.SetGradation(RGB(255, 230, 230), RGB(230, 160, 160), 0, TRUE);
		m_abA.SetGradation(RGB(220, 240, 255), RGB(150, 190, 240), 0, TRUE);
		m_abB.SetGradation(RGB(220, 240, 255), RGB(150, 190, 240), 0, TRUE);
		m_abClr.SetGradation(RGB(240, 230, 230), RGB(210, 180, 180), 0, TRUE);
		m_loop1.SetGradation(RGB(235, 255, 235), RGB(170, 230, 170), 0, TRUE);
		m_loop2.SetGradation(RGB(235, 255, 235), RGB(170, 230, 170), 0, TRUE);
		m_loop4.SetGradation(RGB(235, 255, 235), RGB(170, 230, 170), 0, TRUE);
		m_loop8.SetGradation(RGB(235, 255, 235), RGB(170, 230, 170), 0, TRUE);
		RefreshKillLook();
		CCustomControlUtility::BeginDialogToolTip(m_tooltip, this, TTS_NOPREFIX);
		m_tooltip.AddTool(&m_pitchUp, LL14(L"音程を +3 します。", L"Raise pitch by +3.", L"Monter la hauteur de +3.", L"Alza il pitch di +3.", L"Sube el tono +3.", L"음정을 +3.", L"音高 +3。", L"رفع الدرجة +3.", L"Поднять тон на +3.", L"Tonhöhe +3.", L"Tom +3.", L"Toonhoogte +3.", L"Wysokosc +3.", L"Perde +3."));
		m_tooltip.AddTool(&m_pitchDn, LL14(L"音程を -3 します。", L"Lower pitch by -3.", L"Baisser la hauteur de -3.", L"Abbassa il pitch di -3.", L"Baja el tono -3.", L"음정을 -3.", L"音高 -3。", L"خفض الدرجة -3.", L"Опустить тон на -3.", L"Tonhöhe -3.", L"Tom -3.", L"Toonhoogte -3.", L"Wysokosc -3.", L"Perde -3."));
		m_tooltip.AddTool(&m_tempoUp, LL14(L"テンポを +3% します。", L"Raise tempo by +3%.", L"Augmenter le tempo de +3%.", L"Aumenta il tempo del +3%.", L"Sube el tempo +3%.", L"템포 +3%.", L"速度 +3%。", L"رفع الإيقاع +3%.", L"Ускорить темп на +3%.", L"Tempo +3%.", L"Tempo +3%.", L"Tempo +3%.", L"Tempo +3%.", L"Tempo +3%."));
		m_tooltip.AddTool(&m_tempoDn, LL14(L"テンポを -3% します。", L"Lower tempo by -3%.", L"Baisser le tempo de -3%.", L"Riduci il tempo del -3%.", L"Baja el tempo -3%.", L"템포 -3%.", L"速度 -3%。", L"خفض الإيقاع -3%.", L"Замедлить темп на -3%.", L"Tempo -3%.", L"Tempo -3%.", L"Tempo -3%.", L"Tempo -3%.", L"Tempo -3%."));
		m_tooltip.AddTool(&m_pitchRst, LL14(L"音程を 100% に戻します。", L"Reset pitch to 100%.", L"Remettre la hauteur a 100%.", L"Ripristina pitch a 100%.", L"Restablece tono a 100%.", L"음정 100%로.", L"音高恢复 100%。", L"إعادة الدرجة إلى 100%.", L"Сброс тона на 100%.", L"Tonhöhe auf 100%.", L"Tom para 100%.", L"Toonhoogte naar 100%.", L"Wysokosc na 100%.", L"Perdeyi 100% yap."));
		m_tooltip.AddTool(&m_tempoRst, LL14(L"テンポを 100% に戻します。", L"Reset tempo to 100%.", L"Remettre le tempo a 100%.", L"Ripristina tempo a 100%.", L"Restablece tempo a 100%.", L"템포 100%로.", L"速度恢复 100%。", L"إعادة الإيقاع إلى 100%.", L"Сброс темпа на 100%.", L"Tempo auf 100%.", L"Tempo para 100%.", L"Tempo naar 100%.", L"Tempo na 100%.", L"Tempo 100% yap."));
		m_tooltip.AddTool(&m_vocal, LL14(L"センター（ボーカル）成分を強調します。", L"Boost center/vocal component.", L"Renforcer la voix (centre).", L"Enfatizza la voce (centro).", L"Refuerza la voz (centro).", L"보컬(센터) 강조.", L"增强人声（中置）。", L"تعزيز الصوت المركزي.", L"Усилить вокал (центр).", L"Gesang (Mitte) betonen.", L"Realcar vocal (centro).", L"Zang (midden) versterken.", L"Wzmocnij wokal (srodek).", L"Vokali (orta) vurgula."));
		m_tooltip.AddTool(&m_vocalDn, LL14(L"センター（ボーカル）成分を弱めます。", L"Reduce center/vocal component.", L"Affaiblir la voix (centre).", L"Riduci la voce (centro).", L"Reduce la voz (centro).", L"보컬(센터) 약화.", L"减弱人声（中置）。", L"تخفيف الصوت المركزي.", L"Ослабить вокал (центр).", L"Gesang (Mitte) abschwächen.", L"Reduzir vocal (centro).", L"Zang (midden) verzwakken.", L"Osłab wokal (srodek).", L"Vokali (orta) azalt."));
		m_tooltip.AddTool(&m_vocalRst, LL14(L"ボーカルを中立(100)に戻します。", L"Reset vocal to neutral (100).", L"Remettre la voix a 100.", L"Ripristina voce a 100.", L"Restablece voz a 100.", L"보컬 중립(100).", L"人声恢复中性(100)。", L"إعادة الصوت إلى 100.", L"Сброс вокала на 100.", L"Gesang auf 100.", L"Vocal para 100.", L"Zang naar 100.", L"Wokal na 100.", L"Vokal 100."));
		m_tooltip.AddTool(&m_msNarrow, LL14(L"M/S 幅を狭くします（センター寄り）。", L"Narrow M/S width (more center).", L"Reduire la largeur M/S.", L"Restringi la larghezza M/S.", L"Reduce el ancho M/S.", L"M/S 폭을 좁힘.", L"缩小 M/S 宽度。", L"تضييق عرض M/S.", L"Сузить ширину M/S.", L"M/S-Breite verengen.", L"Estreitar largura M/S.", L"M/S-breedte vernauwen.", L"Zwez szerokosc M/S.", L"M/S genisligini daralt."));
		m_tooltip.AddTool(&m_msWide, LL14(L"M/S 幅を広くします（サイド寄り）。", L"Widen M/S width (more sides).", L"Elargir la largeur M/S.", L"Allarga la larghezza M/S.", L"Amplia el ancho M/S.", L"M/S 폭을 넓힘.", L"加宽 M/S 宽度。", L"توسيع عرض M/S.", L"Расширить ширину M/S.", L"M/S-Breite erweitern.", L"Alargar largura M/S.", L"M/S-breedte verbreden.", L"Poszerz szerokosc M/S.", L"M/S genisligini genislet."));
		m_tooltip.AddTool(&m_msRst, LL14(L"M/S 幅を中立(100)に戻します。", L"Reset M/S width to 100.", L"Remettre M/S a 100.", L"Ripristina M/S a 100.", L"Restablece M/S a 100.", L"M/S 중립(100).", L"M/S 恢复 100。", L"إعادة M/S إلى 100.", L"Сброс M/S на 100.", L"M/S auf 100.", L"M/S para 100.", L"M/S naar 100.", L"M/S na 100.", L"M/S 100."));
		m_tooltip.AddTool(&m_vinyl, LL14(
			L"レコード盤。ドラッグでスクラッチ（相対）、ホイールで微調整。",
			L"Vinyl platter. Drag to scratch (relative), wheel to nudge.",
			L"Platine. Glisser pour scratch (relatif), molette pour ajuster.",
			L"Piatto. Trascina per scratch (relativo), rotella per regolare.",
			L"Plato. Arrastre para scratch (relativo), rueda para ajustar.",
			L"턴테이블. 드래그로 스크래치(상대), 휠로 미세 조정.",
			L"唱片盘。拖动刮盘（相对），滚轮微调。",
			L"قرص. اسحب للخدش (نسبي)، العجلة للضبط.",
			L"Пластинка. Тяните для скретча (относит.), колесо для точной настройки.",
			L"Platte. Ziehen zum Scratchen (relativ), Rad zum Feinjustieren.",
			L"Disco. Arraste para scratch (relativo), roda para ajustar.",
			L"Platenspeler. Sleep om te scratchen (relatief), wiel om bij te stellen.",
			L"Plyta. Przeciagaj aby scratch (wzglednie), kolelko do dojscia.",
			L"Plak. Scratch icin surukle (goreli), ince ayar icin tekerlek."));
		m_tooltip.AddTool(&m_fx, LL14(
			L"スクラッチ効果の強さ（音量）。100=標準。",
			L"Scratch effect strength (level). 100=default.",
			L"Force de l'effet scratch (niveau). 100=defaut.",
			L"Intensita effetto scratch (livello). 100=predefinito.",
			L"Fuerza del efecto scratch (nivel). 100=predeterminado.",
			L"스크래치 효과 강도(볼륨). 100=기본.",
			L"刮盘效果强度（音量）。100=标准。",
			L"قوة تأثير الخدش (المستوى). 100=افتراضي.",
			L"Сила эффекта скретча (уровень). 100=по умолчанию.",
			L"Scratch-Effektstarke (Pegel). 100=Standard.",
			L"Forca do efeito scratch (nivel). 100=padrao.",
			L"Scratch-effectsterkte (niveau). 100=standaard.",
			L"Sila efektu scratch (poziom). 100=domyslny.",
			L"Scratch efekti gucu (seviye). 100=varsayilan."));
		m_tooltip.AddTool(&m_spd, LL14(
			L"スクラッチの速度感度。100=標準。",
			L"Scratch speed sensitivity. 100=default.",
			L"Sensibilite vitesse scratch. 100=defaut.",
			L"Sensibilita velocita scratch. 100=predefinito.",
			L"Sensibilidad de velocidad scratch. 100=predeterminado.",
			L"스크래치 속도 감도. 100=기본.",
			L"刮盘速度灵敏度。100=标准。",
			L"حساسية سرعة الخدش. 100=افتراضي.",
			L"Чувствительность скорости скретча. 100=по умолчанию.",
			L"Scratch-Geschwindigkeitsempfindlichkeit. 100=Standard.",
			L"Sensibilidade de velocidade scratch. 100=padrao.",
			L"Scratch-snelheidsgevoeligheid. 100=standaard.",
			L"Czulosc predkosci scratch. 100=domyslny.",
			L"Scratch hiz duyarliligi. 100=varsayilan."));
		m_tooltip.AddTool(&m_filter, LL14(
			L"DJフィルタ。100=OFF、<100=LPF、>100=HPF。",
			L"DJ filter. 100=OFF, <100=LPF, >100=HPF.",
			L"Filtre DJ. 100=OFF, <100=LPF, >100=HPF.",
			L"Filtro DJ. 100=OFF, <100=LPF, >100=HPF.",
			L"Filtro DJ. 100=OFF, <100=LPF, >100=HPF.",
			L"DJ 필터. 100=OFF, <100=LPF, >100=HPF.",
			L"DJ 滤镜。100=关，<100=LPF，>100=HPF。",
			L"مرشح DJ. 100=OFF، <100=LPF، >100=HPF.",
			L"DJ-фильтр. 100=OFF, <100=LPF, >100=HPF.",
			L"DJ-Filter. 100=AUS, <100=LPF, >100=HPF.",
			L"Filtro DJ. 100=OFF, <100=LPF, >100=HPF.",
			L"DJ-filter. 100=UIT, <100=LPF, >100=HPF.",
			L"Filtr DJ. 100=OFF, <100=LPF, >100=HPF.",
			L"DJ filtre. 100=KAPALI, <100=LPF, >100=HPF."));
		m_tooltip.AddTool(&m_vol, LL14(L"再生音量（メディアプレイヤーと同期）。", L"Playback volume (synced with media player).", L"Volume (sync lecteur).", L"Volume (sync player).", L"Volumen (sync reproductor).", L"재생 볼륨(플레이어 동기).", L"播放音量（与播放器同步）。", L"مستوى الصوت (مزامن).", L"Громкость (синхр.).", L"Lautstarke (sync).", L"Volume (sincronizado).", L"Volume (gesynchroniseerd).", L"Glosnosc (sync).", L"Ses (eszamanli)."));
		m_tooltip.AddTool(&m_eqLow, LL14(L"低域EQ（100=中立）。", L"Low EQ (100=neutral).", L"EQ graves (100=neutre).", L"EQ bassi (100=neutro).", L"EQ graves (100=neutral).", L"저역 EQ(100=중립).", L"低音 EQ（100=中性）。", L"EQ منخفض (100=محايد).", L"Низкий EQ (100=нейтр.).", L"Tiefen-EQ (100=neutral).", L"EQ graves (100=neutro).", L"Lage EQ (100=neutraal).", L"EQ niskie (100=neutral).", L"Bas EQ (100=notr)."));
		m_tooltip.AddTool(&m_eqMid, LL14(L"中域EQ（100=中立）。", L"Mid EQ (100=neutral).", L"EQ medium (100=neutre).", L"EQ medi (100=neutro).", L"EQ medios (100=neutral).", L"중역 EQ(100=중립).", L"中音 EQ（100=中性）。", L"EQ وسط (100=محايد).", L"Средний EQ (100=нейтр.).", L"Mitten-EQ (100=neutral).", L"EQ medios (100=neutro).", L"Midden EQ (100=neutraal).", L"EQ srednie (100=neutral).", L"Orta EQ (100=notr)."));
		m_tooltip.AddTool(&m_eqHigh, LL14(L"高域EQ（100=中立）。", L"High EQ (100=neutral).", L"EQ aigus (100=neutre).", L"EQ alti (100=neutro).", L"EQ agudos (100=neutral).", L"고역 EQ(100=중립).", L"高音 EQ（100=中性）。", L"EQ مرتفع (100=محايد).", L"Высокий EQ (100=нейтр.).", L"Höhen-EQ (100=neutral).", L"EQ agudos (100=neutro).", L"Hoge EQ (100=neutraal).", L"EQ wysokie (100=neutral).", L"Tiz EQ (100=notr)."));
		m_tooltip.AddTool(&m_killL, LL14(L"低域キル（トグル）。", L"Low kill (toggle).", L"Kill graves (bascule).", L"Kill bassi (toggle).", L"Kill graves (alternar).", L"저역 킬(토글).", L"低音消除（切换）。", L"قتل المنخفض (تبديل).", L"Kill низа (перекл.).", L"Tiefen-Kill (Umschalt).", L"Kill graves (alternar).", L"Lage kill (schakel).", L"Kill niskie (przełącz).", L"Bas kill (ac/kapa)."));
		m_tooltip.AddTool(&m_killM, LL14(L"中域キル（トグル）。", L"Mid kill (toggle).", L"Kill medium (bascule).", L"Kill medi (toggle).", L"Kill medios (alternar).", L"중역 킬(토글).", L"中音消除（切换）。", L"قتل الوسط (تبديل).", L"Kill середины (перекл.).", L"Mitten-Kill (Umschalt).", L"Kill medios (alternar).", L"Midden kill (schakel).", L"Kill srednie (przełącz).", L"Orta kill (ac/kapa)."));
		m_tooltip.AddTool(&m_killH, LL14(L"高域キル（トグル）。", L"High kill (toggle).", L"Kill aigus (bascule).", L"Kill alti (toggle).", L"Kill agudos (alternar).", L"고역 킬(토글).", L"高音消除（切换）。", L"قتل المرتفع (تبديل).", L"Kill верха (перекл.).", L"Höhen-Kill (Umschalt).", L"Kill agudos (alternar).", L"Hoge kill (schakel).", L"Kill wysokie (przełącz).", L"Tiz kill (ac/kapa)."));
		m_tooltip.AddTool(&m_play, LL14(L"再生を開始します。", L"Start playback.", L"Demarrer la lecture.", L"Avvia riproduzione.", L"Iniciar reproduccion.", L"재생 시작.", L"开始播放。", L"بدء التشغيل.", L"Начать воспроизведение.", L"Wiedergabe starten.", L"Iniciar reproducao.", L"Afspelen starten.", L"Rozpocznij odtwarzanie.", L"Oynatmayi baslat."));
		m_tooltip.AddTool(&m_pause, LL14(L"一時停止／再開します。", L"Pause / resume.", L"Pause / reprendre.", L"Pausa / riprendi.", L"Pausa / reanudar.", L"일시정지/재개.", L"暂停/继续。", L"إيقاف مؤقت / استئناف.", L"Пауза / продолжить.", L"Pause / Fortsetzen.", L"Pausar / retomar.", L"Pauzeren / hervatten.", L"Pauza / wznow.", L"Duraklat / devam."));
		m_tooltip.AddTool(&m_stop, LL14(L"再生を停止します。", L"Stop playback.", L"Arreter la lecture.", L"Ferma riproduzione.", L"Detener reproduccion.", L"재생 정지.", L"停止播放。", L"إيقاف التشغيل.", L"Остановить воспроизведение.", L"Wiedergabe stoppen.", L"Parar reproducao.", L"Afspelen stoppen.", L"Zatrzymaj odtwarzanie.", L"Oynatmayı durdur."));
		m_tooltip.AddTool(&m_cue, LL14(L"停止中:キュー記憶 / 再生中:キューへ戻り一時停止。", L"Paused: store cue / Playing: jump to cue and pause.", L"Pause: memoriser cue / Lecture: revenir au cue.", L"Pausa: memorizza cue / Play: vai al cue e pausa.", L"Pausa: guardar cue / Reproduciendo: saltar al cue.", L"정지: 큐 저장 / 재생: 큐로 이동 후 일시정지.", L"暂停：存储 cue / 播放：跳回 cue 并暂停。", L"متوقف: حفظ cue / تشغيل: العودة إلى cue.", L"Пауза: запомнить cue / Игра: вернуться к cue.", L"Pause: Cue speichern / Play: zum Cue und Pause.", L"Pausa: guardar cue / Play: voltar ao cue.", L"Pauze: cue opslaan / Play: naar cue en pauze.", L"Pauza: zapisz cue / Play: skocz do cue.", L"Duraklat: cue kaydet / Oynat: cue'ya don."));
		m_tooltip.AddTool(&m_prev, LL14(L"前の曲へ。", L"Previous track.", L"Piste precedente.", L"Brano precedente.", L"Pista anterior.", L"이전 곡.", L"上一曲。", L"المقطع السابق.", L"Предыдущий трек.", L"Vorheriger Titel.", L"Faixa anterior.", L"Vorig nummer.", L"Poprzedni utwor.", L"Onceki parca."));
		m_tooltip.AddTool(&m_next, LL14(L"次の曲へ。", L"Next track.", L"Piste suivante.", L"Brano successivo.", L"Pista siguiente.", L"다음 곡.", L"下一曲。", L"المقطع التالي.", L"Следующий трек.", L"Nächster Titel.", L"Faixa seguinte.", L"Volgend nummer.", L"Nastepny utwor.", L"Sonraki parca."));
		m_tooltip.AddTool(&m_beatBk, LL14(L"1拍戻ります。", L"Jump back 1 beat.", L"Reculer d'1 battement.", L"Indietro di 1 beat.", L"Retroceder 1 compas.", L"1박 뒤로.", L"后退 1 拍。", L"رجوع نبضة واحدة.", L"Назад на 1 долю.", L"1 Beat zurück.", L"Voltar 1 batida.", L"1 beat terug.", L"Cofnij 1 beat.", L"1 vurus geri."));
		m_tooltip.AddTool(&m_beatFw, LL14(L"1拍進みます。", L"Jump forward 1 beat.", L"Avancer d'1 battement.", L"Avanti di 1 beat.", L"Avanzar 1 compas.", L"1박 앞으로.", L"前进 1 拍。", L"تقدم نبضة واحدة.", L"Вперёд на 1 долю.", L"1 Beat vor.", L"Avancar 1 batida.", L"1 beat vooruit.", L"Do przodu 1 beat.", L"1 vurus ileri."));
		{
			const wchar_t* cueTip = LL14(
				L"ホットキューへジャンプします。",
				L"Jump to this hot cue.",
				L"Aller a ce hot cue.",
				L"Vai a questo hot cue.",
				L"Saltar a este hot cue.",
				L"이 핫큐로 이동.",
				L"跳转到此热 cue。",
				L"الانتقال إلى نقطة hot cue هذه.",
				L"Перейти к этому hot cue.",
				L"Zu diesem Hot-Cue springen.",
				L"Ir a este hot cue.",
				L"Naar deze hot cue springen.",
				L"Skocz do tego hot cue.",
				L"Bu hot cue'ya git.");
			for (int i = 0; i < 8; ++i)
				m_tooltip.AddTool(&m_cuePad[i], cueTip);
		}
		m_tooltip.AddTool(&m_cueSet, LL14(L"現在位置にホットキューを追加します。", L"Add a hot cue at the current position.", L"Ajouter un hot cue a la position actuelle.", L"Aggiungi hot cue alla posizione attuale.", L"Anadir hot cue en la posicion actual.", L"현재 위치에 핫큐 추가.", L"在当前位置添加热 cue。", L"إضافة hot cue عند الموضع الحالي.", L"Добавить hot cue в текущую позицию.", L"Hot-Cue an aktueller Position hinzufügen.", L"Adicionar hot cue na posicao atual.", L"Hot cue op huidige positie toevoegen.", L"Dodaj hot cue w biezacej pozycji.", L"Gecerli konuma hot cue ekle."));
		m_tooltip.AddTool(&m_cueClr, LL14(L"ホットキューをすべてクリアします。", L"Clear all hot cues.", L"Effacer tous les hot cues.", L"Cancella tutti gli hot cue.", L"Borrar todos los hot cues.", L"모든 핫큐 삭제.", L"清除全部热 cue。", L"مسح كل نقاط hot cue.", L"Очистить все hot cue.", L"Alle Hot-Cues löschen.", L"Limpar todos os hot cues.", L"Alle hot cues wissen.", L"Wyczysc wszystkie hot cue.", L"Tum hot cue'lari temizle."));
		m_tooltip.AddTool(&m_abA, LL14(L"A-Bループの A 点を現在位置に設定。", L"Set A point of A-B loop to current position.", L"Definir le point A de la boucle A-B.", L"Imposta punto A del loop A-B.", L"Establecer punto A del bucle A-B.", L"A-B 루프 A 지점을 현재 위치로.", L"将 A-B 循环的 A 点设为当前位置。", L"تعيين نقطة A لحلقة A-B.", L"Установить точку A цикла A-B.", L"A-Punkt der A-B-Schleife setzen.", L"Definir ponto A do loop A-B.", L"A-punt van A-B-lus zetten.", L"Ustaw punkt A petli A-B.", L"A-B dongusunun A noktasini ayarla."));
		m_tooltip.AddTool(&m_abB, LL14(L"A-Bループの B 点を現在位置に設定。", L"Set B point of A-B loop to current position.", L"Definir le point B de la boucle A-B.", L"Imposta punto B del loop A-B.", L"Establecer punto B del bucle A-B.", L"A-B 루프 B 지점을 현재 위치로.", L"将 A-B 循环的 B 点设为当前位置。", L"تعيين نقطة B لحلقة A-B.", L"Установить точку B цикла A-B.", L"B-Punkt der A-B-Schleife setzen.", L"Definir ponto B do loop A-B.", L"B-punt van A-B-lus zetten.", L"Ustaw punkt B petli A-B.", L"A-B dongusunun B noktasini ayarla."));
		m_tooltip.AddTool(&m_abClr, LL14(L"A-Bループを解除します。", L"Clear A-B loop.", L"Effacer la boucle A-B.", L"Cancella loop A-B.", L"Borrar bucle A-B.", L"A-B 루프 해제.", L"清除 A-B 循环。", L"مسح حلقة A-B.", L"Сбросить цикл A-B.", L"A-B-Schleife löschen.", L"Limpar loop A-B.", L"A-B-lus wissen.", L"Wyczysc petle A-B.", L"A-B dongusunu temizle."));
		m_tooltip.AddTool(&m_loop1, LL14(L"現在位置から 1 拍ループ。", L"Loop 1 beat from current position.", L"Boucle 1 battement.", L"Loop di 1 beat.", L"Bucle de 1 compas.", L"현재 위치에서 1박 루프.", L"从当前位置起 1 拍循环。", L"حلقة نبضة واحدة.", L"Цикл 1 доля.", L"1-Beat-Schleife.", L"Loop de 1 batida.", L"1-beat-lus.", L"Petla 1 beat.", L"1 vurus dongusu."));
		m_tooltip.AddTool(&m_loop2, LL14(L"現在位置から 2 拍ループ。", L"Loop 2 beats from current position.", L"Boucle 2 battements.", L"Loop di 2 beat.", L"Bucle de 2 compases.", L"현재 위치에서 2박 루프.", L"从当前位置起 2 拍循环。", L"حلقة نبضتين.", L"Цикл 2 доли.", L"2-Beat-Schleife.", L"Loop de 2 batidas.", L"2-beat-lus.", L"Petla 2 beat.", L"2 vurus dongusu."));
		m_tooltip.AddTool(&m_loop4, LL14(L"現在位置から 4 拍ループ。", L"Loop 4 beats from current position.", L"Boucle 4 battements.", L"Loop di 4 beat.", L"Bucle de 4 compases.", L"현재 위치에서 4박 루프.", L"从当前位置起 4 拍循环。", L"حلقة 4 نبضات.", L"Цикл 4 доли.", L"4-Beat-Schleife.", L"Loop de 4 batidas.", L"4-beat-lus.", L"Petla 4 beat.", L"4 vurus dongusu."));
		m_tooltip.AddTool(&m_loop8, LL14(L"現在位置から 8 拍ループ。", L"Loop 8 beats from current position.", L"Boucle 8 battements.", L"Loop di 8 beat.", L"Bucle de 8 compases.", L"현재 위치에서 8박 루프.", L"从当前位置起 8 拍循环。", L"حلقة 8 نبضات.", L"Цикл 8 долей.", L"8-Beat-Schleife.", L"Loop de 8 batidas.", L"8-beat-lus.", L"Petla 8 beat.", L"8 vurus dongusu."));
		m_tooltip.AddTool(&m_seek, LL14(L"波形シーク。ドラッグで位置移動。", L"Waveform seek. Drag to move position.", L"Seek forme d'onde. Glisser pour deplacer.", L"Seek forma d'onda. Trascina per muovere.", L"Seek de forma de onda. Arrastre para mover.", L"파형 시크. 드래그로 이동.", L"波形定位。拖动移动位置。", L"سعي الموجة. اسحب للنقل.", L"Волновой seek. Тяните для перемещения.", L"Wellenform-Seek. Ziehen zum Verschieben.", L"Seek de forma de onda. Arraste para mover.", L"Golfvorm-seek. Sleep om te verplaatsen.", L"Seek przebiegu. Przeciagnij aby przesunac.", L"Dalga sekli seek. Konum icin surukle."));
		m_tooltip.AddTool(&m_bpmDet, LL14(L"BPMを計測して拍グリッドへ反映します。", L"Measure BPM and apply beat grid.", L"Mesurer le BPM et appliquer la grille.", L"Misura BPM e applica griglia.", L"Medir BPM y aplicar rejilla.", L"BPM 측정 후 비트 그리드 반영.", L"测量 BPM 并应用到拍网格。", L"قياس BPM وتطبيق الشبكة.", L"Измерить BPM и сетку.", L"BPM messen und Raster anwenden.", L"Medir BPM e aplicar grade.", L"BPM meten en raster toepassen.", L"Zmierz BPM i zastosuj siatke.", L"BPM olc ve izgara uygula."));
		m_tooltip.AddTool(&m_meter, LL14(L"出力レベルメーター。", L"Output level meter.", L"Vu-metre de sortie.", L"Misuratore livello uscita.", L"Medidor de nivel de salida.", L"출력 레벨 미터.", L"输出电平表。", L"مقياس مستوى الإخراج.", L"Индикатор уровня выхода.", L"Ausgangspegelmesser.", L"Medidor de nivel de saida.", L"Uitgangsniveaumeter.", L"Miernik poziomu wyjsciowego.", L"Cikis seviye gostergesi."));
		CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 360, 8000);
		SetTimer(1, 33, NULL);
		m_vinyl.SyncFromPlayback();
		RefreshStatus();
		RefreshCueLit();
		return TRUE;
	}
	void RefreshKillLook()
	{
		const int k = savedata.mpDjEqKill & 7;
		m_killL.SetGradation(k & 1 ? RGB(255, 120, 120) : RGB(255, 230, 230), k & 1 ? RGB(220, 60, 60) : RGB(230, 160, 160), 0, TRUE);
		m_killM.SetGradation(k & 2 ? RGB(255, 120, 120) : RGB(255, 230, 230), k & 2 ? RGB(220, 60, 60) : RGB(230, 160, 160), 0, TRUE);
		m_killH.SetGradation(k & 4 ? RGB(255, 120, 120) : RGB(255, 230, 230), k & 4 ? RGB(220, 60, 60) : RGB(230, 160, 160), 0, TRUE);
		if (m_killL.GetSafeHwnd()) m_killL.Invalidate(FALSE);
		if (m_killM.GetSafeHwnd()) m_killM.Invalidate(FALSE);
		if (m_killH.GetSafeHwnd()) m_killH.Invalidate(FALSE);
	}
	void RefreshCueLit()
	{
		const int n = ProAudio_CueCount();
		for (int i = 0; i < 8; ++i) {
			const BOOL on = (i < n);
			m_cuePad[i].SetGradation(on ? RGB(255, 220, 160) : RGB(255, 245, 220), on ? RGB(240, 160, 80) : RGB(240, 200, 140), 0, TRUE);
			if (m_cuePad[i].GetSafeHwnd()) m_cuePad[i].Invalidate(FALSE);
		}
		m_lastCueLit = n;
	}
	void RefreshStatus()
	{
		CString bpm;
		if (savedata.mpDetectedBpm > 0) {
			if (savedata.mpDetectedMeterNum >= 2)
				bpm.Format(L"BPM %d  %d/%d", savedata.mpDetectedBpm, savedata.mpDetectedMeterNum,
					savedata.mpDetectedMeterDen > 0 ? savedata.mpDetectedMeterDen : 4);
			else
				bpm.Format(L"BPM %d", savedata.mpDetectedBpm);
		} else
			bpm = LL14(L"BPM --", L"BPM --", L"BPM --", L"BPM --", L"BPM --", L"BPM --", L"BPM --", L"BPM --", L"BPM --", L"BPM --", L"BPM --", L"BPM --", L"BPM --", L"BPM --");
		if (m_bpm.GetSafeHwnd()) {
			CString cur;
			m_bpm.GetWindowText(cur);
			if (cur != bpm)
				m_bpm.SetWindowText(bpm);
		}
		int pp = 100, tp = 100;
		if (og && og->m_pitch_sl.GetSafeHwnd())
			pp = (int)TempoPercentFromPos(og->m_pitch_sl.GetPos());
		if (og && og->m_tempo_sl.GetSafeHwnd())
			tp = (int)TempoPercentFromPos(og->m_tempo_sl.GetPos());
		CString st;
		st.Format(L"P %d%%  T %d%%  V %d  MS %d", pp, tp, savedata.mpVocalCenter, savedata.pro_ms_width);
		if (m_status.GetSafeHwnd()) {
			CString cur;
			m_status.GetWindowText(cur);
			if (cur != st)
				m_status.SetWindowText(st);
		}
	}
	void SyncProToolsMsVocal()
	{
		if (g_proToolsDlg && ::IsWindow(g_proToolsDlg->GetSafeHwnd()))
			g_proToolsDlg->LoadFromSavedata();
	}
	void SetPitchTempoAbs(BOOL isPitch, int pct)
	{
		if (!og || !::IsWindow(og->GetSafeHwnd())) return;
		CCustomSliderCtrl& sl = isPitch ? og->m_pitch_sl : og->m_tempo_sl;
		if (!sl.GetSafeHwnd()) return;
		const int pos = MpDjPitchTempoSlFromPercent(pct);
		sl.SetPos(pos);
		if (isPitch) {
			pitch = pos;
			if (og->m_pitch.GetSafeHwnd()) {
				CString s; s.Format(L"%3d%%", (int)TempoPercentFromPos(pos));
				og->m_pitch.SetWindowText(s);
			}
			if (mp && ::IsWindow(mp->GetSafeHwnd()) && mp->m_pitch.GetSafeHwnd())
				mp->m_pitch.SetPos(pos);
		} else {
			tempo = pos;
			if (og->m_temp_num.GetSafeHwnd()) {
				CString s; s.Format(L"%3d%%", (int)TempoPercentFromPos(pos));
				og->m_temp_num.SetWindowText(s);
			}
			if (mp && ::IsWindow(mp->GetSafeHwnd()) && mp->m_tempo.GetSafeHwnd())
				mp->m_tempo.SetPos(pos);
		}
		RefreshStatus();
	}
	void BeatJump(int beats)
	{
		if (!og || !og->m_time.GetSafeHwnd()) return;
		int bpm = savedata.mpDetectedBpm > 0 ? savedata.mpDetectedBpm : 120;
		if (bpm < 40) bpm = 40;
		if (bpm > 300) bpm = 300;
		int rate = wavbit_sample_Hz;
		if (rate < 8000) rate = 44100;
		int delta = (int)(((__int64)rate * 60 * beats) / bpm);
		if (mode == -10) delta /= 100;
		MpDjSeekToSliderPos(og->m_time.GetPos() + delta);
	}
	void SetBeatLoop(int beats)
	{
		if (!mp || !::IsWindow(mp->GetSafeHwnd()) || !og || !og->m_time.GetSafeHwnd()) return;
		int bpm = savedata.mpDetectedBpm > 0 ? savedata.mpDetectedBpm : 120;
		if (bpm < 40) bpm = 40;
		if (bpm > 300) bpm = 300;
		int rate = wavbit_sample_Hz;
		if (rate < 8000) rate = 44100;
		int len = (int)(((__int64)rate * 60 * beats) / bpm);
		if (mode == -10) len /= 100;
		if (len < 1) len = 1;
		const int mn = og->m_time.GetMinValue();
		const int mx = og->m_time.GetMaxValue();
		int a = og->m_time.GetPos();
		int b = a + len;
		if (a < mn) a = mn;
		if (b > mx) b = mx;
		if (b <= a) b = (a < mx) ? (a + 1) : a;
		mp->m_abApos = a;
		mp->m_abBpos = b;
		mp->m_abLoopCount = 0;
		if (mp->m_seek.GetSafeHwnd())
			mp->m_seek.SetAB(a, b);
		if (m_seek.GetSafeHwnd())
			m_seek.SetAB(a, b);
	}
	void PollSliders()
	{
		BOOL dirty = FALSE;
		if (m_fx.GetSafeHwnd()) {
			const int v = m_fx.GetPos();
			if (v != savedata.mpDjScratchEffect) { savedata.mpDjScratchEffect = v; dirty = TRUE; }
		}
		if (m_spd.GetSafeHwnd()) {
			const int v = m_spd.GetPos();
			if (v != savedata.mpDjScratchSpeed) { savedata.mpDjScratchSpeed = v; dirty = TRUE; }
		}
		if (m_filter.GetSafeHwnd()) {
			const int v = m_filter.GetPos();
			if (v != savedata.mpDjFilter) { savedata.mpDjFilter = v; dirty = TRUE; }
		}
		if (m_eqLow.GetSafeHwnd()) {
			const int v = 200 - m_eqLow.GetPos();
			if (v != savedata.mpDjEqLow) { savedata.mpDjEqLow = v; dirty = TRUE; }
		}
		if (m_eqMid.GetSafeHwnd()) {
			const int v = 200 - m_eqMid.GetPos();
			if (v != savedata.mpDjEqMid) { savedata.mpDjEqMid = v; dirty = TRUE; }
		}
		if (m_eqHigh.GetSafeHwnd()) {
			const int v = 200 - m_eqHigh.GetPos();
			if (v != savedata.mpDjEqHigh) { savedata.mpDjEqHigh = v; dirty = TRUE; }
		}
		if (m_vol.GetSafeHwnd()) {
			int v = m_vol.GetPos();
			if (v < 0) v = 0; if (v > 100) v = 100;
			int cur = v;
			if (mp && mp->m_vol.GetSafeHwnd()) cur = mp->m_vol.GetPos();
			if (v != cur) {
				if (mp && ::IsWindow(mp->GetSafeHwnd()) && mp->m_vol.GetSafeHwnd()) {
					mp->m_vol.SetPos(v);
					mp->SendMessage(WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, v), (LPARAM)mp->m_vol.GetSafeHwnd());
				}
				if (og && og->m_sl.GetSafeHwnd()) {
					og->m_sl.SetPos(v * 1000);
					og->PostMessage(WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, v * 1000), (LPARAM)og->m_sl.GetSafeHwnd());
				}
			}
		}
		if (dirty) MpPersistSavedataQuick();
	}
	void SyncSeekMirror()
	{
		if (!m_seek.GetSafeHwnd() || !og || !og->m_time.GetSafeHwnd()) return;
		if (m_seek.IsDragging()) return;
		int mn = 0, mx = 1;
		og->m_time.GetRange(mn, mx);
		int abA = -1, abB = -1;
		if (mp) { abA = mp->m_abApos; abB = mp->m_abBpos; }
		m_seek.SetPlaybackMirror(og->m_time.GetPos(), mn, mx, mn, mx, abA, abB);
		if (mp && mp->m_wavePeakN > 0)
			m_seek.SetWavePeaks(mp->m_wavePeaks, mp->m_wavePeakN);
		if (savedata.mpDetectedBpm > 0)
			m_seek.SetBeatGrid((float)savedata.mpDetectedBpm, savedata.mpBeatGrid ? TRUE : FALSE, savedata.mpBeatGridOffsetMs,
				savedata.mpDetectedMeterNum >= 2 ? savedata.mpDetectedMeterNum : 4);
		int frames[8];
		int n = ProAudio_CueCount();
		if (n > 8) n = 8;
		for (int i = 0; i < n; ++i) {
			ProCue c;
			if (!ProAudio_CueGet(i, c)) { n = i; break; }
			int pos = c.frame;
			if (mx > mn && pos > mx) {
				const int scaled100 = pos / 100;
				if (scaled100 >= mn && scaled100 <= mx)
					pos = scaled100;
			}
			if (pos < mn) pos = mn;
			if (pos > mx) pos = mx;
			frames[i] = pos;
		}
		m_seek.SetCues(frames, n);
		if (n != m_lastCueLit) RefreshCueLit();
	}
	virtual BOOL PreTranslateMessage(MSG* pMsg)
	{
		if (m_tooltip.GetSafeHwnd()) m_tooltip.RelayEvent(pMsg);
		if (pMsg && pMsg->message == WM_MOUSEWHEEL && m_vinyl.GetSafeHwnd()) {
			CPoint pt(GET_X_LPARAM(pMsg->lParam), GET_Y_LPARAM(pMsg->lParam));
			CRect vr; m_vinyl.GetWindowRect(&vr);
			if (vr.PtInRect(pt)) {
				m_vinyl.SendMessage(WM_MOUSEWHEEL, pMsg->wParam, pMsg->lParam);
				return TRUE;
			}
		}
		return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
	}
	virtual void PostNcDestroy()
	{
		g_mpDjPad = NULL;
		CCustomBlurDialogBase::PostNcDestroy();
		delete this;
	}
	afx_msg void OnClose()
	{
		// ユーザー閉じ: 次回起動で復活させない（アプリ終了時は PrepareAppExit で残す）
		if (!s_djPadAppExit) {
			savedata.mpDjPadwindow = 0;
			MpPersistSavedataQuick();
		}
		DestroyWindow();
	}
	afx_msg void OnDestroy()
	{
		AudioMicDevUnregisterCombo(&m_micDev);
		AudioLoopDevUnregisterCombo(&m_loopDev);
		KillTimer(1);
		MpDjScratchShutdown();
		CCustomBlurDialogBase::OnDestroy();
	}
	afx_msg void OnTimer(UINT_PTR nIDEvent)
	{
		if (nIDEvent == 1) {
			m_vinyl.SyncFromPlayback();
			PollSliders();
			SyncSeekMirror();
			RefreshStatus();
			const int lv = (int)(ProAudio_LivePeak() * 1000.f + 0.5f);
			if (m_meter.GetSafeHwnd()) m_meter.SetLevel(lv);
		}
		CCustomBlurDialogBase::OnTimer(nIDEvent);
	}
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
	{
		UNREFERENCED_PARAMETER(nSBCode);
		UNREFERENCED_PARAMETER(nPos);
		if (pScrollBar && pScrollBar->GetSafeHwnd() == m_seek.GetSafeHwnd()) {
			const int cueHit = m_seek.GetCueClick();
			if (cueHit >= 0) {
				m_seek.ClearCueClick();
				if (mp) mp->JumpToCueIndex(cueHit);
				else {
					ProCue c;
					if (ProAudio_CueGet(cueHit, c) && og)
						og->PostMessage(WM_APP_PROAUDIO_CUESEEK, 0, (LPARAM)c.frame);
				}
			} else if (!m_seek.IsDragging() || m_seek.GetDragTarget() == 3) {
				MpDjSeekToSliderPos(m_seek.GetPos());
			}
			if (mp) {
				int a = -1, b = -1;
				m_seek.GetAB(a, b);
				if (a != mp->m_abApos || b != mp->m_abBpos) {
					mp->m_abApos = a;
					mp->m_abBpos = b;
					mp->m_abLoopCount = 0;
					if (mp->m_seek.GetSafeHwnd())
						mp->m_seek.SetAB(a, b);
				}
			}
		} else {
			PollSliders();
		}
		CCustomBlurDialogBase::OnHScroll(nSBCode, nPos, pScrollBar);
	}
	afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
	{
		UNREFERENCED_PARAMETER(nSBCode);
		UNREFERENCED_PARAMETER(nPos);
		UNREFERENCED_PARAMETER(pScrollBar);
		PollSliders();
	}
	afx_msg void OnContextMenu(CWnd*, CPoint point)
	{
		if (point.x == -1 && point.y == -1) {
			CRect r; GetWindowRect(&r);
			point.x = r.left + 40; point.y = r.top + 40;
		}
		const BOOL topMost = (GetExStyle() & WS_EX_TOPMOST) != 0;
		CCustomPopupMenu menu;
		menu.SetAeroMode(FALSE);
		menu.AddCheck(10,
			LL14(L"最前面", L"Always on top", L"Toujours au premier plan", L"Sempre in primo piano",
				L"Siempre visible", L"항상 위", L"置顶", L"دائماً في المقدمة", L"Поверх всех", L"Immer im Vordergrund",
				L"Sempre no topo", L"Altijd boven", L"Zawsze na wierzchu", L"Her zaman ustte"),
			topMost,
			LL14(L"DJパッドを他の窓の上に常時表示。ミキシング中に隠れないようにします。", L"Keep the DJ pad above other windows so it stays visible while mixing.", L"Garde le pad DJ au-dessus des autres fenetres pendant le mix.", L"Tiene il pad DJ sopra le altre finestre durante il mix.", L"Mantiene el pad DJ encima de otras ventanas al mezclar.",
				L"DJ 패드를 다른 창 위에 항상 표시. 믹스 중 가려지지 않게 합니다.", L"将 DJ 面板置顶，混音时不会被挡住。", L"إبقاء لوحة DJ فوق النوافذ الأخرى أثناء المزج.", L"Держать DJ-пад поверх окон, чтобы не скрывался при микшировании.", L"DJ-Pad immer oben halten, damit es beim Mixen sichtbar bleibt.",
				L"Manter o pad DJ acima das outras janelas ao mixar.", L"Houd het DJ-pad boven andere vensters tijdens mixen.", L"Trzymaj pad DJ nad innymi oknami podczas miksowania.", L"DJ padini diger pencerelerin ustunde tut; mikste kaybolmasin."));
		menu.AddCheck(11,
			LL14(L"メインに追随", L"Follow main window", L"Suivre la fenetre principale", L"Segui finestra principale",
				L"Seguir ventana principal", L"메인 창 따라가기", L"跟随主窗口", L"اتبع النافذة الرئيسية", L"Следовать главному", L"Hauptfenster folgen",
				L"Seguir janela principal", L"Volg hoofdvenster", L"Podazaj za glownym", L"Ana pencereyi izle"),
			savedata.mpDjPadMainLock != 0,
			LL14(L"メインプレイヤーを動かすとDJパッドも一緒に移動。マルチモニタ作業向き。", L"When you move the main player, the DJ pad moves with it. Handy on multi-monitor setups.", L"Deplacer le lecteur principal deplace aussi le pad DJ. Pratique en multi-ecran.", L"Spostando il player principale si sposta anche il pad DJ. Utile su multi-monitor.", L"Al mover el reproductor principal, el pad DJ se mueve. Util en multi-monitor.",
				L"메인 플레이어를 움직이면 DJ 패드도 함께 이동. 멀티 모니터에 유용.", L"移动主播放器时 DJ 面板跟随。适合多显示器。", L"عند تحريك المشغل الرئيسي تتحرك لوحة DJ معه. مفيد مع شاشات متعددة.", L"При перемещении главного плеера DJ-пад следует за ним. Удобно на нескольких мониторах.", L"Beim Verschieben des Hauptplayers folgt das DJ-Pad. Praktisch bei mehreren Monitoren.",
				L"Ao mover o player principal, o pad DJ acompanha. Util em multi-monitor.", L"Als je de hoofdspeler verplaatst, beweegt het DJ-pad mee. Handig bij meerdere monitoren.", L"Przesuwajac glowny odtwarzacz, pad DJ podaza za nim. Przydatne na wielu monitorach.", L"Ana oynaticiyi tasirken DJ padi birlikte gelir. Coklu monitörde kullanisli."));
		menu.AddSeparator();
		menu.AddCommand(12,
			LL14(L"音程/テンポをリセット", L"Reset pitch/tempo", L"Reinit hauteur/tempo", L"Reset pitch/tempo",
				L"Restablecer tono/tempo", L"음정/템포 초기화", L"重置音高/速度", L"إعادة الدرجة/الإيقاع", L"Сброс тона/темпа", L"Tonhöhe/Tempo zurücksetzen",
				L"Redefinir tom/tempo", L"Toonhoogte/tempo resetten", L"Reset wysokosci/tempa", L"Perde/tempo sifirla"),
			LL14(L"音程とテンポを両方とも100%に戻します。キー合わせ後の復帰用。", L"Reset both pitch and tempo to 100%. Use after key/tempo matching.", L"Remet hauteur et tempo a 100%. Apres accordage.", L"Ripristina pitch e tempo al 100%. Dopo l'accordatura.", L"Restablece tono y tempo al 100%. Tras afinar.",
				L"음정과 템포를 모두 100%로 되돌립니다. 키 맞춘 뒤 복귀용.", L"将音高和速度都恢复为 100%。调键后还原用。", L"إعادة الدرجة والإيقاع إلى 100%. بعد مطابقة المفتاح.", L"Сбросить тон и темп до 100%. После подстройки ключа.", L"Tonhöhe und Tempo auf 100% zurücksetzen. Nach Key-Anpassung.",
				L"Redefinir tom e tempo para 100%. Apos ajustar a tonalidade.", L"Zet toonhoogte en tempo terug op 100%. Na toonsoort-matching.", L"Reset wysokosci i tempa do 100%. Po dopasowaniu tonacji.", L"Perde ve tempoyu %100'e sifirla. Ton eslestirmeden sonra."));
		menu.AddCommand(13,
			LL14(L"EQ/フィルタをリセット", L"Reset EQ/filter", L"Reinit EQ/filtre", L"Reset EQ/filtro",
				L"Restablecer EQ/filtro", L"EQ/필터 초기화", L"重置 EQ/滤镜", L"إعادة EQ/المرشح", L"Сброс EQ/фильтра", L"EQ/Filter zurücksetzen",
				L"Redefinir EQ/filtro", L"EQ/filter resetten", L"Reset EQ/filtra", L"EQ/filtre sifirla"),
			LL14(L"Low/Mid/High EQ・フィルタ・Killをすべて平坦(100%)に戻します。", L"Reset Low/Mid/High EQ, filter, and Kill bands all flat to 100%.", L"Remet EQ Low/Mid/High, filtre et Kill a 100%.", L"Ripristina EQ Low/Mid/High, filtro e Kill al 100%.", L"Restablece EQ Low/Mid/High, filtro y Kill al 100%.",
				L"Low/Mid/High EQ·필터·Kill을 모두 평탄(100%)으로 되돌립니다.", L"将 Low/Mid/High EQ、滤镜和 Kill 全部恢复为平坦 100%。", L"إعادة EQ Low/Mid/High والمرشح وKill كلها إلى 100%.", L"Сбросить EQ Low/Mid/High, фильтр и Kill до 100%.", L"EQ Low/Mid/High, Filter und Kill auf 100% flach setzen.",
				L"Redefinir EQ Low/Mid/High, filtro e Kill para 100%.", L"Zet Low/Mid/High-EQ, filter en Kill terug op 100%.", L"Reset EQ Low/Mid/High, filtra i Kill do 100%.", L"Low/Mid/High EQ, filtre ve Kill'i %100'e sifirla."));
		menu.AddCommand(14,
			LL14(L"スクラッチ設定をリセット", L"Reset scratch settings", L"Reinit scratch", L"Reset scratch",
				L"Restablecer scratch", L"스크래치 초기화", L"重置刮盘设置", L"إعادة إعدادات الخدش", L"Сброс скретча", L"Scratch zurücksetzen",
				L"Redefinir scratch", L"Scratch resetten", L"Reset scratch", L"Scratch sifirla"),
			LL14(L"スクラッチ効果量と速度感度を既定(100%)に戻します。", L"Reset scratch effect amount and speed sensitivity to defaults (100%).", L"Remet effet et sensibilite scratch a 100%.", L"Ripristina effetto e sensibilita scratch al 100%.", L"Restablece efecto y sensibilidad scratch al 100%.",
				L"스크래치 효과량과 속도 감도를 기본(100%)으로 되돌립니다.", L"将刮盘效果量和速度灵敏度恢复为默认 100%。", L"إعادة مقدار تأثير الخدش وحساسية السرعة إلى 100%.", L"Сбросить силу эффекта и чувствительность скретча до 100%.", L"Scratch-Effektstärke und Geschwindigkeitsempfindlichkeit auf 100% setzen.",
				L"Redefinir efeito e sensibilidade do scratch para 100%.", L"Zet scratch-effect en snelheidsgevoeligheid terug op 100%.", L"Reset sily efektu i czulosci scratch do 100%.", L"Scratch efekt miktari ve hiz duyarliligini %100'e sifirla."));
		menu.AddCommand(15,
			LL14(L"BPM計測", L"Detect BPM", L"Detecter BPM", L"Rileva BPM", L"Detectar BPM",
				L"BPM 측정", L"检测 BPM", L"اكتشاف BPM", L"Определить BPM", L"BPM erkennen",
				L"Detectar BPM", L"BPM detecteren", L"Wykryj BPM", L"BPM algila"),
			LL14(L"再生中の音からBPMを計測。テンポ合わせやビートグリッド用。数秒再生してから実行。", L"Measure BPM from playing audio for tempo match / beat grid. Play a few seconds first.", L"Mesure le BPM de la lecture pour tempo/grille. Jouer quelques secondes d'abord.", L"Misura il BPM dall'audio in riproduzione. Riproduci alcuni secondi prima.", L"Mide el BPM del audio en reproduccion. Reproduce unos segundos antes.",
				L"재생 중인 음에서 BPM 측정. 템포 맞춤·비트 그리드용. 수초 재생 후 실행.", L"从正在播放的音频测 BPM，用于对拍/网格。先播放数秒再执行。", L"قياس BPM من الصوت قيد التشغيل لمطابقة الإيقاع. شغّل ثوانٍ أولاً.", L"Измерить BPM с текущего звука для темпа/сетки. Сначала послушайте несколько секунд.", L"BPM aus dem laufenden Audio messen (Tempo/Grid). Zuerst einige Sekunden abspielen.",
				L"Medir BPM do audio em reproducao para tempo/grade. Toque alguns segundos antes.", L"Meet BPM van speelaudio voor tempo/grid. Speel eerst een paar seconden.", L"Zmierz BPM z odtwarzanego dzwieku (tempo/siatka). Najpierw odtworz kilka sekund.", L"Calinan sesten BPM olc; tempo/izgara icin. Once birkac saniye cal."));
		menu.AddSeparator();
		{
			CCustomPopupMenu* vocalSub = menu.AddSubMenu(
				LL14(L"ボーカル / M-S", L"Vocal / M-S", L"Vocal / M-S", L"Vocal / M-S", L"Vocal / M-S",
					L"보컬 / M-S", L"人声 / M-S", L"صوت / M-S", L"Вокал / M-S", L"Gesang / M-S",
					L"Vocal / M-S", L"Vocaal / M-S", L"Wokal / M-S", L"Vokal / M-S"),
				LL14(L"センターボーカル強調/抑制と Mid-Side ステレオ幅の調整。", L"Boost/cut center vocal and adjust Mid-Side stereo width.", L"Renforcer/attenuer le vocal centre et regler la largeur Mid-Side.", L"Aumenta/riduci il vocale centrale e regola la larghezza Mid-Side.", L"Sube/baja la voz central y ajusta la anchura Mid-Side.",
					L"센터 보컬 강조/억제와 Mid-Side 스테레오 폭 조절.", L"加强/削弱中置人声并调节 Mid-Side 立体声宽度。", L"تعزيز/خفض الصوت المركزي وضبط عرض Mid-Side.", L"Усилить/ослабить центральный вокал и ширину Mid-Side.", L"Center-Gesang anheben/absenken und Mid-Side-Breite einstellen.",
					L"Reforcar/atenuar o vocal central e ajustar a largura Mid-Side.", L"Center-vocaal versterken/dempen en Mid-Side-breedte aanpassen.", L"Wzmocnij/oslab centralny wokal i szerokosc Mid-Side.", L"Merkez vokali artir/azalt ve Mid-Side genisligini ayarla."));
			if (vocalSub) {
				vocalSub->AddCommand(16,
					LL14(L"ボーカル +", L"Vocal +", L"Vocal +", L"Vocal +", L"Vocal +",
						L"보컬 +", L"人声 +", L"صوت +", L"Вокал +", L"Gesang +",
						L"Vocal +", L"Vocaal +", L"Wokal +", L"Vokal +"),
					LL14(L"センター成分を上げてボーカルを強調します(+10)。", L"Raise the center component to emphasize vocals (+10).", L"Augmente le centre pour accentuer le vocal (+10).", L"Alza il centro per enfatizzare il vocale (+10).", L"Sube el centro para enfatizar la voz (+10).",
						L"센터 성분을 올려 보컬을 강조합니다(+10).", L"提高中置成分以强调人声（+10）。", L"رفع المكوّن المركزي لتأكيد الصوت (+10).", L"Поднять центр, чтобы выделить вокал (+10).", L"Centeranteil anheben, um Gesang zu betonen (+10).",
						L"Aumentar o centro para enfatizar o vocal (+10).", L"Centercomponent verhogen om vocaal te benadrukken (+10).", L"Podnies srodek, by wyeksponowac wokal (+10).", L"Merkez bileseni artirarak vokali vurgula (+10)."));
				vocalSub->AddCommand(17,
					LL14(L"ボーカル −", L"Vocal −", L"Vocal −", L"Vocal −", L"Vocal −",
						L"보컬 −", L"人声 −", L"صوت −", L"Вокал −", L"Gesang −",
						L"Vocal −", L"Vocaal −", L"Wokal −", L"Vokal −"),
					LL14(L"センター成分を下げてボーカルを抑えめにします(−10)。", L"Lower the center component to reduce vocals (−10).", L"Baisse le centre pour attenuer le vocal (−10).", L"Abbassa il centro per ridurre il vocale (−10).", L"Baja el centro para atenuar la voz (−10).",
						L"센터 성분을 내려 보컬을 줄입니다(−10).", L"降低中置成分以减弱人声（−10）。", L"خفض المكوّن المركزي لتقليل الصوت (−10).", L"Опустить центр, чтобы ослабить вокал (−10).", L"Centeranteil senken, um Gesang zu reduzieren (−10).",
						L"Diminuir o centro para atenuar o vocal (−10).", L"Centercomponent verlagen om vocaal te verminderen (−10).", L"Obniz srodek, by stlumic wokal (−10).", L"Merkez bileseni dusurerek vokali azalt (−10)."));
				vocalSub->AddCommand(18,
					LL14(L"ボーカル リセット", L"Reset vocal", L"Reinit vocal", L"Reset vocal", L"Restablecer vocal",
						L"보컬 초기화", L"重置人声", L"إعادة الصوت", L"Сброс вокала", L"Gesang zurücksetzen",
						L"Redefinir vocal", L"Vocaal resetten", L"Reset wokalu", L"Vokal sifirla"),
					LL14(L"ボーカル(センター)量を既定値に戻します。", L"Reset vocal (center) amount to the default.", L"Remet le niveau vocal (centre) par defaut.", L"Ripristina il livello vocale (centro) predefinito.", L"Restablece el nivel de voz (centro) al valor por defecto.",
						L"보컬(센터) 양을 기본값으로 되돌립니다.", L"将人声（中置）量恢复为默认。", L"إعادة مقدار الصوت (المركز) إلى الافتراضي.", L"Сбросить уровень вокала (центр) к умолчанию.", L"Gesang (Center) auf Standard zurücksetzen.",
						L"Redefinir o nivel do vocal (centro) para o padrao.", L"Zet vocaal (center) terug op standaard.", L"Reset poziomu wokalu (srodek) do domyslnego.", L"Vokal (merkez) miktarini varsayilana sifirla."));
				vocalSub->AddSeparator();
				vocalSub->AddCommand(19,
					LL14(L"M-S 狭め", L"M-S narrow", L"M-S etroit", L"M-S stretto", L"M-S estrecho",
						L"M-S 좁게", L"M-S 窄", L"M-S ضيق", L"M-S узко", L"M-S schmal",
						L"M-S estreito", L"M-S smal", L"M-S wasko", L"M-S dar"),
					LL14(L"Sideを下げてステレオ幅を狭め、中央寄りにします。", L"Reduce Side to narrow stereo width toward mono/center.", L"Baisse Side pour retrecir la stereo vers le centre.", L"Riduci Side per restringere lo stereo verso il centro.", L"Baja Side para estrechar el estereo hacia el centro.",
						L"Side를 낮춰 스테레오 폭을 좁히고 중앙에 가깝게 합니다.", L"降低 Side 使立体声变窄、更靠中央。", L"خفض Side لتضييق العرض نحو المركز.", L"Уменьшить Side, сузив стерео к центру.", L"Side absenken, Stereo-Breite zur Mitte verengen.",
						L"Diminuir Side para estreitar o estereo ao centro.", L"Side verlagen om stereobreedte naar het midden te vernauwen.", L"Zmniejsz Side, by zwezic stereo ku srodkowi.", L"Side'i dusurup stereo genisligini merkeze daralt."));
				vocalSub->AddCommand(20,
					LL14(L"M-S 広げ", L"M-S wide", L"M-S large", L"M-S ampio", L"M-S ancho",
						L"M-S 넓게", L"M-S 宽", L"M-S واسع", L"M-S широко", L"M-S breit",
						L"M-S largo", L"M-S breed", L"M-S szeroko", L"M-S genis"),
					LL14(L"Sideを上げてステレオ幅を広げ、空間感を出します。", L"Raise Side to widen stereo image and add space.", L"Augmente Side pour elargir l'image stereo.", L"Alza Side per allargare l'immagine stereo.", L"Sube Side para ensanchar la imagen estereo.",
						L"Side를 올려 스테레오 폭을 넓히고 공간감을 냅니다.", L"提高 Side 以加宽立体声、增加空间感。", L"رفع Side لتوسيع الصورة الاستريو وإضافة فضاء.", L"Поднять Side, расширив стерео и пространство.", L"Side anheben, Stereo-Bild und Raum erweitern.",
						L"Aumentar Side para alargar a imagem estereo.", L"Side verhogen om stereobeeld en ruimte te verbreden.", L"Podnies Side, by poszerzyc obraz stereo.", L"Side'i artirip stereo genisligini ve alan hissini ac."));
				vocalSub->AddCommand(21,
					LL14(L"M-S リセット", L"Reset M-S", L"Reinit M-S", L"Reset M-S", L"Restablecer M-S",
						L"M-S 초기화", L"重置 M-S", L"إعادة M-S", L"Сброс M-S", L"M-S zurücksetzen",
						L"Redefinir M-S", L"M-S resetten", L"Reset M-S", L"M-S sifirla"),
					LL14(L"Mid-Side 幅を既定に戻します。", L"Reset Mid-Side width to the default.", L"Remet la largeur Mid-Side par defaut.", L"Ripristina la larghezza Mid-Side predefinita.", L"Restablece la anchura Mid-Side al valor por defecto.",
						L"Mid-Side 폭을 기본값으로 되돌립니다.", L"将 Mid-Side 宽度恢复为默认。", L"إعادة عرض Mid-Side إلى الافتراضي.", L"Сбросить ширину Mid-Side к умолчанию.", L"Mid-Side-Breite auf Standard zurücksetzen.",
						L"Redefinir a largura Mid-Side para o padrao.", L"Zet Mid-Side-breedte terug op standaard.", L"Reset szerokosci Mid-Side do domyslnej.", L"Mid-Side genisligini varsayilana sifirla."));
			}
		}
		{
			CCustomPopupMenu* killSub = menu.AddSubMenu(
				LL14(L"EQ Kill", L"EQ Kill", L"EQ Kill", L"EQ Kill", L"EQ Kill",
					L"EQ Kill", L"EQ Kill", L"EQ Kill", L"EQ Kill", L"EQ Kill",
					L"EQ Kill", L"EQ Kill", L"EQ Kill", L"EQ Kill"),
				LL14(L"Low/Mid/High帯を瞬時にミュートするDJ用キルスイッチ。", L"Instant mute switches for Low/Mid/High bands (DJ kill).", L"Coupures instantanees Low/Mid/High (kill DJ).", L"Mute istantanei Low/Mid/High (kill DJ).", L"Mutes instantaneos Low/Mid/High (kill DJ).",
					L"Low/Mid/High 대역을 즉시 뮤트하는 DJ용 킬 스위치.", L"瞬间静音 Low/Mid/High 频段的 DJ Kill 开关。", L"مفاتيح كتم فورية لنطاقات Low/Mid/High (قتل DJ).", L"Мгновенный мьют полос Low/Mid/High (DJ kill).", L"Sofort-Mute für Low/Mid/High-Bänder (DJ-Kill).",
					L"Mutes instantaneos das faixas Low/Mid/High (kill DJ).", L"Directe mute-schakelaars voor Low/Mid/High (DJ-kill).", L"Natychmiastowe wyciszenie Low/Mid/High (kill DJ).", L"Low/Mid/High bantlarini aninda susturan DJ kill anahtarlari."));
			if (killSub) {
				killSub->AddCheck(22,
					LL14(L"Low Kill", L"Low Kill", L"Low Kill", L"Low Kill", L"Low Kill",
						L"Low Kill", L"Low Kill", L"Low Kill", L"Low Kill", L"Low Kill",
						L"Low Kill", L"Low Kill", L"Low Kill", L"Low Kill"),
					(savedata.mpDjEqKill & 1) != 0,
					LL14(L"低域(Low)を瞬時に切る/戻す。キックやベースを落とすとき。", L"Instantly mute/unmute Low band — drop kick/bass.", L"Coupe/retablit Low — pour couper kick/basse.", L"Muta/riattiva Low — togli kick/basso.", L"Silencia/restaura Low — quita kick/bajo.",
						L"저역(Low)을 즉시 끄거나 되돌림. 킥·베이스를 뺄 때.", L"瞬间开关低音(Low)。用于去掉 kick/贝斯。", L"كتم/إرجاع Low فوراً — لإسقاط الكيك/الباس.", L"Мгновенно выкл/вкл Low — убрать кик/бас.", L"Low sofort stumm/an — Kick/Bass rausnehmen.",
						L"Mutar/restaurar Low na hora — tirar kick/baixo.", L"Low direct dempen/aan — kick/bas weghalen.", L"Natychmiast wycisz/wlacz Low — usun kick/bas.", L"Low'u aninda kes/ac — kick/bas dusurmek icin."));
				killSub->AddCheck(23,
					LL14(L"Mid Kill", L"Mid Kill", L"Mid Kill", L"Mid Kill", L"Mid Kill",
						L"Mid Kill", L"Mid Kill", L"Mid Kill", L"Mid Kill", L"Mid Kill",
						L"Mid Kill", L"Mid Kill", L"Mid Kill", L"Mid Kill"),
					(savedata.mpDjEqKill & 2) != 0,
					LL14(L"中域(Mid)を瞬時に切る/戻す。ボーカルや楽器の帯を落とすとき。", L"Instantly mute/unmute Mid band — drop vocals/instruments.", L"Coupe/retablit Mid — pour couper voix/instruments.", L"Muta/riattiva Mid — togli voci/strumenti.", L"Silencia/restaura Mid — quita voces/instrumentos.",
						L"중역(Mid)을 즉시 끄거나 되돌림. 보컬·악기 대역을 뺄 때.", L"瞬间开关中频(Mid)。用于去掉人声/乐器。", L"كتم/إرجاع Mid فوراً — لإسقاط الأصوات/الآلات.", L"Мгновенно выкл/вкл Mid — убрать вокал/инструменты.", L"Mid sofort stumm/an — Gesang/Instrumente rausnehmen.",
						L"Mutar/restaurar Mid na hora — tirar vozes/instrumentos.", L"Mid direct dempen/aan — zang/instrumenten weghalen.", L"Natychmiast wycisz/wlacz Mid — usun wokal/instrumenty.", L"Mid'i aninda kes/ac — vokal/enstruman dusurmek icin."));
				killSub->AddCheck(24,
					LL14(L"High Kill", L"High Kill", L"High Kill", L"High Kill", L"High Kill",
						L"High Kill", L"High Kill", L"High Kill", L"High Kill", L"High Kill",
						L"High Kill", L"High Kill", L"High Kill", L"High Kill"),
					(savedata.mpDjEqKill & 4) != 0,
					LL14(L"高域(High)を瞬時に切る/戻す。ハイハットやシンバルを落とすとき。", L"Instantly mute/unmute High band — drop hats/cymbals.", L"Coupe/retablit High — pour couper hats/cymbales.", L"Muta/riattiva High — togli hi-hat/piatti.", L"Silencia/restaura High — quita hats/platillos.",
						L"고역(High)을 즉시 끄거나 되돌림. 하이햇·심벌을 뺄 때.", L"瞬间开关高频(High)。用于去掉踩镲/镲片。", L"كتم/إرجاع High فوراً — لإسقاط الهات/الصنج.", L"Мгновенно выкл/вкл High — убрать хэты/тарелки.", L"High sofort stumm/an — Hats/Becken rausnehmen.",
						L"Mutar/restaurar High na hora — tirar hats/pratos.", L"High direct dempen/aan — hats/bekkens weghalen.", L"Natychmiast wycisz/wlacz High — usun hi-haty/talerze.", L"High'i aninda kes/ac — hi-hat/zil dusurmek icin."));
			}
		}
		menu.AddCommand(25,
			LL14(L"イコライザを開く", L"Open equalizer", L"Ouvrir l'egaliseur", L"Apri equalizzatore", L"Abrir ecualizador",
				L"이퀄라이저 열기", L"打开均衡器", L"فتح المعادل", L"Открыть эквалайзер", L"Equalizer öffnen",
				L"Abrir equalizador", L"Equalizer openen", L"Otworz equalizer", L"Equalizeri ac"),
			LL14(L"詳細な帯域調整用のイコライザ窓を開きます。", L"Open the full equalizer window for detailed band control.", L"Ouvre l'egaliseur complet pour un reglage fin.", L"Apre l'equalizzatore completo per regolazioni fini.", L"Abre el ecualizador completo para ajuste fino.",
				L"세밀한 대역 조절용 이퀄라이저 창을 엽니다.", L"打开完整均衡器窗口，便于细调频段。", L"فتح نافذة المعادل الكاملة للضبط الدقيق.", L"Открыть полное окно эквалайзера для тонкой настройки.", L"Volles Equalizer-Fenster für Feineinstellung öffnen.",
				L"Abrir a janela completa do equalizador para ajuste fino.", L"Open het volledige equalizer-venster voor fijnafstelling.", L"Otworz pelne okno equalizera do dokladnej regulacji.", L"Ince bant ayari icin tam equalizer penceresini ac."));
		menu.AddSeparator();
		menu.AddCommand(1,
			LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"),
			LL14(L"DJパッドを閉じます（設定は保持されます）。", L"Close the DJ pad (settings are kept).", L"Ferme le pad DJ (reglages conserves).", L"Chiude il pad DJ (impostazioni conservate).", L"Cierra el pad DJ (se conservan los ajustes).",
				L"DJ 패드를 닫습니다(설정은 유지).", L"关闭 DJ 面板（设置会保留）。", L"إغلاق لوحة DJ (تُحفظ الإعدادات).", L"Закрыть DJ-пад (настройки сохраняются).", L"DJ-Pad schließen (Einstellungen bleiben).",
				L"Fechar o pad DJ (configuracoes sao mantidas).", L"Sluit het DJ-pad (instellingen blijven).", L"Zamknij pad DJ (ustawienia zachowane).", L"DJ padini kapat (ayarlar saklanir)."));
		AudioMicDevAppendMenu(menu);
		AudioLoopDevAppendMenu(menu);
		const UINT cmd = menu.Track(point, this);
		if (AudioMicDevHandleMenuCmd(cmd) || AudioLoopDevHandleMenuCmd(cmd)) return;
		if (cmd == 1) {
			if (!s_djPadAppExit) {
				savedata.mpDjPadwindow = 0;
				MpPersistSavedataQuick();
			}
			DestroyWindow();
		} else if (cmd == 10) {
			savedata.mpDjPadTopMost = topMost ? 0 : 1;
			SetWindowPos(topMost ? &wndNoTopMost : &wndTopMost, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			MpPersistSavedataQuick();
		} else if (cmd == 11) {
			savedata.mpDjPadMainLock = savedata.mpDjPadMainLock ? 0 : 1;
			EnableMainWindowLock(&savedata.mpDjPadMainLock, FALSE);
			MpPersistSavedataQuick();
		} else if (cmd == 12) {
			SetPitchTempoAbs(TRUE, 100);
			SetPitchTempoAbs(FALSE, 100);
		} else if (cmd == 13) {
			savedata.mpDjEqLow = savedata.mpDjEqMid = savedata.mpDjEqHigh = 100;
			savedata.mpDjFilter = 100;
			savedata.mpDjEqKill = 0;
			if (m_eqLow.GetSafeHwnd()) m_eqLow.SetPos(100);
			if (m_eqMid.GetSafeHwnd()) m_eqMid.SetPos(100);
			if (m_eqHigh.GetSafeHwnd()) m_eqHigh.SetPos(100);
			if (m_filter.GetSafeHwnd()) m_filter.SetPos(100);
			RefreshKillLook();
			MpPersistSavedataQuick();
		} else if (cmd == 14) {
			savedata.mpDjScratchEffect = 100;
			savedata.mpDjScratchSpeed = 100;
			if (m_fx.GetSafeHwnd()) m_fx.SetPos(100);
			if (m_spd.GetSafeHwnd()) m_spd.SetPos(100);
			MpPersistSavedataQuick();
		} else if (cmd == 15) {
			if (mp) MpOnBpmDetect(mp);
		} else if (cmd == 16) {
			OnVocal();
		} else if (cmd == 17) {
			OnVocalDn();
		} else if (cmd == 18) {
			OnVocalRst();
		} else if (cmd == 19) {
			OnMsNarrow();
		} else if (cmd == 20) {
			OnMsWide();
		} else if (cmd == 21) {
			OnMsRst();
		} else if (cmd == 22) {
			OnKillL();
		} else if (cmd == 23) {
			OnKillM();
		} else if (cmd == 24) {
			OnKillH();
		} else if (cmd == 25) {
			if (mp && ::IsWindow(mp->GetSafeHwnd()))
				mp->PostMessage(WM_COMMAND, ID_MP_OPEN_EQ);
		}
	}
	afx_msg void OnRButtonUp(UINT, CPoint point)
	{
		ClientToScreen(&point);
		OnContextMenu(this, point);
	}
	afx_msg void OnPitchUp() { MpDjApplyPitchTempoDelta(TRUE, +3); RefreshStatus(); }
	afx_msg void OnPitchDn() { MpDjApplyPitchTempoDelta(TRUE, -3); RefreshStatus(); }
	afx_msg void OnTempoUp() { MpDjApplyPitchTempoDelta(FALSE, +3); RefreshStatus(); }
	afx_msg void OnTempoDn() { MpDjApplyPitchTempoDelta(FALSE, -3); RefreshStatus(); }
	afx_msg void OnPitchRst() { SetPitchTempoAbs(TRUE, 100); }
	afx_msg void OnTempoRst() { SetPitchTempoAbs(FALSE, 100); }
	afx_msg void OnVocal() {
		savedata.mpVocalCenter = min(200, savedata.mpVocalCenter + 10);
		MpPersistSavedataQuick();
		SyncProToolsMsVocal();
		RefreshStatus();
	}
	afx_msg void OnVocalDn() {
		savedata.mpVocalCenter = max(0, savedata.mpVocalCenter - 10);
		MpPersistSavedataQuick();
		SyncProToolsMsVocal();
		RefreshStatus();
	}
	afx_msg void OnVocalRst() {
		savedata.mpVocalCenter = 100;
		MpPersistSavedataQuick();
		SyncProToolsMsVocal();
		RefreshStatus();
	}
	afx_msg void OnMsNarrow() {
		savedata.pro_ms_width = max(0, savedata.pro_ms_width - 10);
		savedata.pro_ms_mono = 0;
		MpPersistSavedataQuick();
		SyncProToolsMsVocal();
		RefreshStatus();
	}
	afx_msg void OnMsWide() {
		savedata.pro_ms_width = min(200, savedata.pro_ms_width + 10);
		savedata.pro_ms_mono = 0;
		MpPersistSavedataQuick();
		SyncProToolsMsVocal();
		RefreshStatus();
	}
	afx_msg void OnMsRst() {
		savedata.pro_ms_width = 100;
		savedata.pro_ms_mono = 0;
		MpPersistSavedataQuick();
		SyncProToolsMsVocal();
		RefreshStatus();
	}
	afx_msg void OnPlay() {
		if (mp) mp->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_MP_PLAY, BN_CLICKED), 0);
	}
	afx_msg void OnPause() {
		if (mp) mp->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_MP_PAUSE, BN_CLICKED), 0);
	}
	afx_msg void OnStop() {
		if (mp) mp->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_MP_STOP, BN_CLICKED), 0);
	}
	afx_msg void OnPrev() {
		if (mp) mp->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_MP_PREV, BN_CLICKED), 0);
	}
	afx_msg void OnNext() {
		if (mp) mp->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_MP_NEXT, BN_CLICKED), 0);
	}
	afx_msg void OnCueBtn() {
		if (!og || !og->m_time.GetSafeHwnd()) return;
		if (ps == 1 || !plf) {
			m_cueMem = og->m_time.GetPos();
		} else {
			if (m_cueMem >= 0)
				MpDjSeekToSliderPos(m_cueMem);
			if (mp) mp->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_MP_PAUSE, BN_CLICKED), 0);
			else if (og) og->OnPause();
		}
	}
	afx_msg void OnBeatBk() { BeatJump(-1); }
	afx_msg void OnBeatFw() { BeatJump(+1); }
	afx_msg void OnBpmDet() { if (mp) MpOnBpmDetect(mp); }
	afx_msg void OnCuePad(UINT nID) {
		const int idx = (int)nID - IDC_DJPAD_CUE1;
		if (idx < 0 || idx > 7) return;
		if (mp) mp->JumpToCueIndex(idx);
		else {
			ProCue c;
			if (ProAudio_CueGet(idx, c) && og)
				og->PostMessage(WM_APP_PROAUDIO_CUESEEK, 0, (LPARAM)c.frame);
		}
	}
	afx_msg void OnCueSet() {
		if (!og || !::IsWindow(og->GetSafeHwnd())) return;
		const int pos = og->m_time.GetPos();
		TCHAR lab[32];
		_stprintf_s(lab, _T("C%d"), ProAudio_CueCount() + 1);
		if (ProAudio_CueAdd(pos, lab) < 0) return;
		if (mp) mp->RefreshSeekCues();
		RefreshCueLit();
		SyncSeekMirror();
	}
	afx_msg void OnCueClr() {
		ProAudio_CueClearAll();
		if (mp) mp->RefreshSeekCues();
		RefreshCueLit();
		SyncSeekMirror();
	}
	afx_msg void OnAbA() {
		if (!mp || !og || !::IsWindow(og->GetSafeHwnd())) return;
		mp->m_abApos = og->m_time.GetPos();
		if (mp->m_abBpos >= 0 && mp->m_abBpos <= mp->m_abApos) mp->m_abBpos = -1;
		mp->m_abLoopCount = 0;
		if (mp->m_seek.GetSafeHwnd()) mp->m_seek.SetAB(mp->m_abApos, mp->m_abBpos);
		SyncSeekMirror();
	}
	afx_msg void OnAbB() {
		if (!mp || !og || !::IsWindow(og->GetSafeHwnd())) return;
		mp->m_abBpos = og->m_time.GetPos();
		if (mp->m_abApos < 0) mp->m_abApos = og->m_time.GetMinValue();
		if (mp->m_abBpos <= mp->m_abApos) {
			int t = mp->m_abApos; mp->m_abApos = mp->m_abBpos; mp->m_abBpos = t;
		}
		mp->m_abLoopCount = 0;
		if (mp->m_seek.GetSafeHwnd()) mp->m_seek.SetAB(mp->m_abApos, mp->m_abBpos);
		SyncSeekMirror();
	}
	afx_msg void OnAbClr() {
		if (!mp) return;
		mp->m_abApos = -1;
		mp->m_abBpos = -1;
		mp->m_abLoopCount = 0;
		if (mp->m_seek.GetSafeHwnd()) mp->m_seek.SetAB(-1, -1);
		SyncSeekMirror();
	}
	afx_msg void OnLoop1() { SetBeatLoop(1); }
	afx_msg void OnLoop2() { SetBeatLoop(2); }
	afx_msg void OnLoop4() { SetBeatLoop(4); }
	afx_msg void OnLoop8() { SetBeatLoop(8); }
	afx_msg void OnKillL() {
		savedata.mpDjEqKill ^= 1;
		RefreshKillLook();
		MpPersistSavedataQuick();
	}
	afx_msg void OnKillM() {
		savedata.mpDjEqKill ^= 2;
		RefreshKillLook();
		MpPersistSavedataQuick();
	}
	afx_msg void OnKillH() {
		savedata.mpDjEqKill ^= 4;
		RefreshKillLook();
		MpPersistSavedataQuick();
	}
	afx_msg void OnMicDev() { AudioMicDevApplyFromCombo(m_micDev); }
	afx_msg void OnMicDevRefresh() { AudioDevRebuildAll(); }
	afx_msg void OnLoopDev() { AudioLoopDevApplyFromCombo(m_loopDev); }
	DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CMpDjPadDlg, CCustomBlurDialogBase)
	ON_WM_CLOSE()
	ON_WM_DESTROY()
	ON_WM_TIMER()
	ON_WM_HSCROLL()
	ON_WM_VSCROLL()
	ON_WM_CONTEXTMENU()
	ON_WM_RBUTTONUP()
	ON_BN_CLICKED(IDC_DJPAD_PITCH_UP, &CMpDjPadDlg::OnPitchUp)
	ON_BN_CLICKED(IDC_DJPAD_PITCH_DN, &CMpDjPadDlg::OnPitchDn)
	ON_BN_CLICKED(IDC_DJPAD_TEMPO_UP, &CMpDjPadDlg::OnTempoUp)
	ON_BN_CLICKED(IDC_DJPAD_TEMPO_DN, &CMpDjPadDlg::OnTempoDn)
	ON_BN_CLICKED(IDC_DJPAD_PITCH_RST, &CMpDjPadDlg::OnPitchRst)
	ON_BN_CLICKED(IDC_DJPAD_TEMPO_RST, &CMpDjPadDlg::OnTempoRst)
	ON_BN_CLICKED(IDC_DJPAD_VOCAL, &CMpDjPadDlg::OnVocal)
	ON_BN_CLICKED(IDC_DJPAD_VOCAL_DN, &CMpDjPadDlg::OnVocalDn)
	ON_BN_CLICKED(IDC_DJPAD_VOCAL_RST, &CMpDjPadDlg::OnVocalRst)
	ON_BN_CLICKED(IDC_DJPAD_MS_NARROW, &CMpDjPadDlg::OnMsNarrow)
	ON_BN_CLICKED(IDC_DJPAD_MS_WIDE, &CMpDjPadDlg::OnMsWide)
	ON_BN_CLICKED(IDC_DJPAD_MS_RST, &CMpDjPadDlg::OnMsRst)
	ON_BN_CLICKED(IDC_DJPAD_PLAY, &CMpDjPadDlg::OnPlay)
	ON_BN_CLICKED(IDC_DJPAD_PAUSE, &CMpDjPadDlg::OnPause)
	ON_BN_CLICKED(IDC_DJPAD_STOP, &CMpDjPadDlg::OnStop)
	ON_BN_CLICKED(IDC_DJPAD_CUE, &CMpDjPadDlg::OnCueBtn)
	ON_BN_CLICKED(IDC_DJPAD_PREV, &CMpDjPadDlg::OnPrev)
	ON_BN_CLICKED(IDC_DJPAD_NEXT, &CMpDjPadDlg::OnNext)
	ON_BN_CLICKED(IDC_DJPAD_BEAT_BK, &CMpDjPadDlg::OnBeatBk)
	ON_BN_CLICKED(IDC_DJPAD_BEAT_FW, &CMpDjPadDlg::OnBeatFw)
	ON_BN_CLICKED(IDC_DJPAD_BPM_DET, &CMpDjPadDlg::OnBpmDet)
	ON_CONTROL_RANGE(BN_CLICKED, IDC_DJPAD_CUE1, IDC_DJPAD_CUE8, &CMpDjPadDlg::OnCuePad)
	ON_BN_CLICKED(IDC_DJPAD_CUESET, &CMpDjPadDlg::OnCueSet)
	ON_BN_CLICKED(IDC_DJPAD_CUECLR, &CMpDjPadDlg::OnCueClr)
	ON_BN_CLICKED(IDC_DJPAD_ABA, &CMpDjPadDlg::OnAbA)
	ON_BN_CLICKED(IDC_DJPAD_ABB, &CMpDjPadDlg::OnAbB)
	ON_BN_CLICKED(IDC_DJPAD_ABCLR, &CMpDjPadDlg::OnAbClr)
	ON_BN_CLICKED(IDC_DJPAD_LOOP1, &CMpDjPadDlg::OnLoop1)
	ON_BN_CLICKED(IDC_DJPAD_LOOP2, &CMpDjPadDlg::OnLoop2)
	ON_BN_CLICKED(IDC_DJPAD_LOOP4, &CMpDjPadDlg::OnLoop4)
	ON_BN_CLICKED(IDC_DJPAD_LOOP8, &CMpDjPadDlg::OnLoop8)
	ON_BN_CLICKED(IDC_DJPAD_KILL_L, &CMpDjPadDlg::OnKillL)
	ON_BN_CLICKED(IDC_DJPAD_KILL_M, &CMpDjPadDlg::OnKillM)
	ON_BN_CLICKED(IDC_DJPAD_KILL_H, &CMpDjPadDlg::OnKillH)
	ON_CBN_SELCHANGE(IDC_DJPAD_MICDEV, &CMpDjPadDlg::OnMicDev)
	ON_BN_CLICKED(IDC_DJPAD_MICDEV_REFRESH, &CMpDjPadDlg::OnMicDevRefresh)
	ON_CBN_SELCHANGE(IDC_DJPAD_LOOPDEV, &CMpDjPadDlg::OnLoopDev)
END_MESSAGE_MAP()

void CMpDjPadDlg::ApplyRemoteDeckFromSavedata()
{
	if (m_eqLow.GetSafeHwnd()) m_eqLow.SetPos(200 - savedata.mpDjEqLow);
	if (m_eqMid.GetSafeHwnd()) m_eqMid.SetPos(200 - savedata.mpDjEqMid);
	if (m_eqHigh.GetSafeHwnd()) m_eqHigh.SetPos(200 - savedata.mpDjEqHigh);
	if (m_filter.GetSafeHwnd()) m_filter.SetPos(savedata.mpDjFilter);
	RefreshKillLook();
}

static void MpDjPadApplyRemoteEq()
{
	if (g_mpDjPad && ::IsWindow(g_mpDjPad->GetSafeHwnd()))
		g_mpDjPad->ApplyRemoteDeckFromSavedata();
}

BOOL IsMpDjPadOpen()
{
	return (g_mpDjPad && ::IsWindow(g_mpDjPad->GetSafeHwnd())) ? TRUE : FALSE;
}

void MpDjPadPrepareAppExit()
{
	if (!IsMpDjPadOpen())
		return;
	s_djPadAppExit = 1;
	savedata.mpDjPadwindow = 1;
	// ディスク書き込みは DestroyWindow 末尾の本保存に任せる（ここで Quick すると終了が二重 I/O）
}

void CloseMpDjPadIfOpen()
{
	MpDjScratchShutdown();
	if (g_mpDjPad && ::IsWindow(g_mpDjPad->GetSafeHwnd()))
		g_mpDjPad->DestroyWindow();
	g_mpDjPad = NULL;
	if (!s_djPadAppExit && savedata.mpDjPadwindow) {
		savedata.mpDjPadwindow = 0;
		MpPersistSavedataQuick();
	}
}

void OpenMpDjPadModeless(CWnd* parent)
{
	if (g_mpDjPad && ::IsWindow(g_mpDjPad->GetSafeHwnd())) {
		g_mpDjPad->ShowWindow(g_oggSubUiRestoring ? SW_SHOWNOACTIVATE : SW_SHOW);
		if (!g_oggSubUiRestoring)
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
	g_mpDjPad->ShowWindow(g_oggSubUiRestoring ? SW_SHOWNOACTIVATE : SW_SHOW);
}

// ---- BPM measure dialog (acrylic caption) ----
class CMpBpmDlg : public CCustomBlurDialogBase
{
public:
	enum { IDD = IDD_MP_BPM };
	CMpBpmDlg(CWnd* p = NULL) : CCustomBlurDialogBase(IDD, p) {}
	CCustomStatic m_status, m_bpm, m_meter, m_pulse, m_pass, m_conf;
	CCustomStandardButton m_cand1, m_cand2, m_cand3, m_remeasure, m_abort, m_close, m_help;
	CToolTipCtrl m_tooltip;
	CString m_lastStatus, m_lastBpm, m_lastMeter, m_lastPulse, m_lastPass, m_lastConf;
	CString m_lastCand1, m_lastCand2, m_lastCand3;
	void LayoutHelpBtn() { CCC_CaptionPlaceHelpBtn(m_hWnd, &m_help); }
	void RefreshFromGlobals()
	{
		CString st, bpm, meter, pulse, pass, conf;
		if (g_bpmUiFail) {
			st = LL14(L"推定できませんでした。再生/PC音を続けて再計測してください。",
				L"Could not estimate. Keep playing / PC audio and remeasure.",
				L"Estimation impossible. Continuez lecture/PC et recommencez.",
				L"Stima non riuscita. Continua riproduzione/PC e rimisura.",
				L"No se pudo estimar. Siga reproduciendo/PC y vuelva a medir.",
				L"추정 실패. 재생/PC 소리를 유지하고 다시 측정하세요.",
				L"无法估计。请继续播放/PC声后重测。",
				L"تعذر التقدير. أبقِ التشغيل/صوت الجهاز وأعد القياس.",
				L"Не удалось оценить. Продолжайте воспроизведение/ПК и повторите.",
				L"Schaetzung fehlgeschlagen. Wiedergabe/PC fortsetzen und neu messen.",
				L"Nao foi possivel estimar. Continue reproduzindo/PC e meça de novo.",
				L"Schatting mislukt. Speel door/pc-audio en meet opnieuw.",
				L"Nie udalo sie oszacowac. Odtwarzaj dalej/PC i zmierz ponownie.",
				L"Tahmin edilemedi. Calmaya/PC sese devam edip yeniden olcun.");
		} else if (MpBpmIsMeasuring()) {
			if (g_bpmConfirmPending == 2 && g_bpmProvisionalBpm > 0) {
				st.Format(LL14(L"緻密収束中…（収束 BPM %d・高精度で再確認）",
					L"Fine convergence… (locked BPM %d; refining)",
					L"Convergence fine… (BPM %d; raffinage)",
					L"Convergenza fine… (BPM %d; raffinamento)",
					L"Convergencia fina… (BPM %d; refinando)",
					L"정밀 수렴 중… (수렴 BPM %d, 고정밀 재확인)",
					L"精密收敛中…（收敛 BPM %d，高精度再确认）",
					L"تقارب دقيق… (BPM %d؛ إعادة تحقق)",
					L"Точная сходимость… (BPM %d; уточнение)",
					L"Feine Konvergenz… (BPM %d; Praezision)",
					L"Convergencia fina… (BPM %d; refinando)",
					L"Fijne convergentie… (BPM %d; verfijnen)",
					L"Doprecyzowanie… (BPM %d; dokladnosc)",
					L"Ince yakinlasma… (BPM %d; netlestirme)"),
					g_bpmProvisionalBpm);
			} else if (g_bpmConfirmPending == 1 && g_bpmProvisionalBpm > 0) {
				st.Format(LL14(L"確認計測中…（仮収束 BPM %d・一致で収束）",
					L"Confirming… (provisional BPM %d; match to converge)",
					L"Confirmation… (BPM provisoire %d)",
					L"Conferma… (BPM provvisorio %d)",
					L"Confirmando… (BPM provisional %d)",
					L"확인 측정 중… (가수렴 BPM %d)",
					L"确认测量中…（暂定收敛 BPM %d）",
					L"تأكيد… (BPM مؤقت %d)",
					L"Подтверждение… (временный BPM %d)",
					L"Bestaetigung… (vorl. BPM %d)",
					L"Confirmando… (BPM provisório %d)",
					L"Bevestigen… (voorlopig BPM %d)",
					L"Potwierdzanie… (tymczasowe BPM %d)",
					L"Onay… (gecici BPM %d)"),
					g_bpmProvisionalBpm);
			} else {
				st = LL14(L"計測中…（仮収束を探索）",
					L"Measuring… (seeking provisional convergence)",
					L"Mesure… (convergence provisoire)",
					L"Misura… (convergenza provvisoria)",
					L"Midiendo… (convergencia provisional)",
					L"측정 중… (가수렴 탐색)",
					L"测量中…（寻找暂定收敛）",
					L"قياس… (بحث عن تقارب مؤقت)",
					L"Измерение… (поиск временной сходимости)",
					L"Messen… (vorlaeufige Konvergenz)",
					L"Medindo… (convergencia provisoria)",
					L"Meten… (voorlopige convergentie)",
					L"Pomiar… (tymczasowa zbieznosc)",
					L"Olculuyor… (gecici yakinlasma)");
			}
		} else if (g_bpmConfirmPending && g_bpmProvisionalBpm > 0) {
			st.Format(LL14(L"仮/収束 BPM %d（次の段階へ）",
				L"Provisional/converged BPM %d (next stage)",
				L"BPM provisoire/convergent %d",
				L"BPM provvisorio/convergente %d",
				L"BPM provisional/convergente %d",
				L"가/수렴 BPM %d (다음 단계)",
				L"暂定/收敛 BPM %d（下一阶段）",
				L"BPM مؤقت/متقارب %d",
				L"Временный/сходящийся BPM %d",
				L"Vorl./konverg. BPM %d",
				L"BPM provisório/convergente %d",
				L"Voorlopig/convergent BPM %d",
				L"Tymczasowe/zbiezne BPM %d",
				L"Gecici/yakın BPM %d"),
				g_bpmProvisionalBpm);
		} else if (savedata.mpDetectedBpm > 0) {
			st = LL14(L"確定（緻密収束・シーク拍グリッドへ反映済み）",
				L"Confirmed (fine convergence; applied to seek beat grid)",
				L"Confirme (convergence fine; grille)",
				L"Confermato (convergenza fine; griglia)",
				L"Confirmado (convergencia fina; rejilla)",
				L"확정 (정밀 수렴·시크 비트 그리드 반영)",
				L"已确定（精密收敛，已应用到进度拍网格）",
				L"تم (تقارب دقيق؛ الشبكة)",
				L"Подтверждено (точная сходимость; сетка)",
				L"Bestaetigt (feine Konvergenz; Raster)",
				L"Confirmado (convergencia fina; grade)",
				L"Bevestigd (fijne convergentie; beatgrid)",
				L"Potwierdzono (doprecyzowanie; siatka)",
				L"Onaylandi (ince yakinlasma; izgara)");
		} else {
			st = LL14(L"待機", L"Idle", L"En attente", L"In attesa", L"En espera",
				L"대기", L"待机", L"انتظار", L"Ожидание", L"Bereit",
				L"Aguardando", L"Inactief", L"Oczekiwanie", L"Beklemede");
		}
		const int showBpm = MpBpmIsMeasuring()
			? (g_bpmConsensusBpm > 0 ? g_bpmConsensusBpm : (g_bpmLastEstimate > 0 ? g_bpmLastEstimate : g_bpmLastAcoustic))
			: (g_bpmConfirmPending && g_bpmProvisionalBpm > 0 ? g_bpmProvisionalBpm : savedata.mpDetectedBpm);
		if (showBpm > 0)
			bpm.Format(L"BPM  %d", showBpm);
		else
			bpm = L"BPM  --";

		const int mn = MpBpmIsMeasuring()
			? (g_bpmConsensusMeterNum > 0 ? g_bpmConsensusMeterNum : g_bpmLastMeterNum)
			: (g_bpmConfirmPending && g_bpmProvisionalMeterNum >= 2 ? g_bpmProvisionalMeterNum : savedata.mpDetectedMeterNum);
		const int md = MpBpmIsMeasuring()
			? (g_bpmConsensusMeterDen > 0 ? g_bpmConsensusMeterDen : g_bpmLastMeterDen)
			: (g_bpmConfirmPending && g_bpmProvisionalMeterDen > 0 ? g_bpmProvisionalMeterDen : savedata.mpDetectedMeterDen);
		if (mn >= 2 && md > 0)
			meter.Format(LL14(L"拍子  %d/%d", L"Meter  %d/%d", L"Mesure  %d/%d", L"Metro  %d/%d", L"Compas  %d/%d",
				L"박자  %d/%d", L"拍号  %d/%d", L"الميزان  %d/%d", L"Размер  %d/%d", L"Taktart  %d/%d",
				L"Compasso  %d/%d", L"Maatsoort  %d/%d", L"Metrum  %d/%d", L"Olcu  %d/%d"), mn, md > 0 ? md : 4);
		else
			meter = LL14(L"拍子  --", L"Meter  --", L"Mesure  --", L"Metro  --", L"Compas  --",
				L"박자  --", L"拍号  --", L"الميزان  --", L"Размер  --", L"Taktart  --",
				L"Compasso  --", L"Maatsoort  --", L"Metrum  --", L"Olcu  --");

		const int pu = MpBpmIsMeasuring()
			? (g_bpmConsensusPulse > 0 ? g_bpmConsensusPulse : g_bpmLastPulse)
			: savedata.mpDetectedPulse;
		LPCTSTR pulseName = L"--";
		if (pu == 4) pulseName = LL14(L"4分音符", L"Quarter", L"Noire", L"Semiminima", L"Negra", L"4분음표", L"四分音符", L"سوداء", L"Четверть", L"Viertel", L"Seminima", L"Kwartnoot", L"Cwiercnuta", L"Dortluk");
		else if (pu == 8) pulseName = LL14(L"8分音符", L"Eighth", L"Croche", L"Croma", L"Corchea", L"8분음표", L"八分音符", L"ذات سن", L"Восьмая", L"Achtel", L"Colcheia", L"Achtste", L"Osminka", L"Sekizlik");
		else if (pu == 16) pulseName = LL14(L"16分音符", L"16th", L"Double-croche", L"Semicroma", L"Semicorchea", L"16분음표", L"十六分音符", L"ذات سنّين", L"Шестнадцатая", L"16tel", L"Semicolcheia", L"Zestiende", L"Szesnastka", L"Onaltilik");
		else if (pu == 32) pulseName = LL14(L"32分音符", L"32nd", L"Triple-croche", L"Biscroma", L"Fusa", L"32분음표", L"三十二分音符", L"ذات 3 أسنان", L"32-я", L"32tel", L"Fusa", L"32ste", L"32-ka", L"Otuzikilik");
		else if (pu == 64) pulseName = LL14(L"64分音符", L"64th", L"Quadruple-croche", L"Semibiscroma", L"Semifusa", L"64분음표", L"六十四分音符", L"ذات 4 أسنان", L"64-я", L"64tel", L"Semifusa", L"64ste", L"64-ka", L"Altmisdortluk");
		pulse.Format(LL14(L"パルス  %s", L"Pulse  %s", L"Pulsation  %s", L"Impulso  %s", L"Pulso  %s",
			L"펄스  %s", L"脉冲  %s", L"نبض  %s", L"Пульс  %s", L"Puls  %s",
			L"Pulso  %s", L"Puls  %s", L"Puls  %s", L"Nabiz  %s"), pulseName);

		if (MpBpmIsMeasuring())
			pass.Format(LL14(L"パス  %d / %d", L"Pass  %d / %d", L"Passe  %d / %d", L"Passaggio  %d / %d", L"Pasada  %d / %d",
				L"패스  %d / %d", L"遍数  %d / %d", L"تمريرة  %d / %d", L"Проход  %d / %d", L"Durchlauf  %d / %d",
				L"Passe  %d / %d", L"Doorgang  %d / %d", L"Przejscie  %d / %d", L"Gecis  %d / %d"),
				g_bpmPassCount, g_bpmNeedPasses);
		else if (g_bpmPassCount > 0)
			pass.Format(LL14(L"パス  %d（完了）", L"Pass  %d (done)", L"Passe  %d (ok)", L"Passaggio  %d (ok)", L"Pasada  %d (ok)",
				L"패스  %d (완료)", L"遍数  %d（完成）", L"تمريرة  %d (تم)", L"Проход  %d (готово)", L"Durchlauf  %d (fertig)",
				L"Passe  %d (ok)", L"Doorgang  %d (klaar)", L"Przejscie  %d (ok)", L"Gecis  %d (bitti)"), g_bpmPassCount);
		else
			pass = LL14(L"パス  --", L"Pass  --", L"Passe  --", L"Passaggio  --", L"Pasada  --",
				L"패스  --", L"遍数  --", L"تمريرة  --", L"Проход  --", L"Durchlauf  --",
				L"Passe  --", L"Doorgang  --", L"Przejscie  --", L"Gecis  --");

		const int confPct = (int)(g_bpmLastConf * 100.f + 0.5f);
		if (MpBpmIsMeasuring() || confPct > 0)
			conf.Format(LL14(L"信頼度  %d%%", L"Confidence  %d%%", L"Confiance  %d%%", L"Confidenza  %d%%", L"Confianza  %d%%",
				L"신뢰도  %d%%", L"置信度  %d%%", L"الثقة  %d%%", L"Уверенность  %d%%", L"Konfidenz  %d%%",
				L"Confianca  %d%%", L"Betrouwbaarheid  %d%%", L"Pewnosc  %d%%", L"Guven  %d%%"), confPct);
		else
			conf = LL14(L"信頼度  --", L"Confidence  --", L"Confiance  --", L"Confidenza  --", L"Confianza  --",
				L"신뢰도  --", L"置信度  --", L"الثقة  --", L"Уверенность  --", L"Konfidenz  --",
				L"Confianca  --", L"Betrouwbaarheid  --", L"Pewnosc  --", L"Guven  --");

		auto setIf = [](CCustomStatic& ctl, CString& last, const CString& now) {
			if (!ctl.GetSafeHwnd()) return;
			if (last == now) return;
			last = now;
			ctl.SetWindowText(now);
		};
		setIf(m_status, m_lastStatus, st);
		setIf(m_bpm, m_lastBpm, bpm);
		setIf(m_meter, m_lastMeter, meter);
		setIf(m_pulse, m_lastPulse, pulse);
		setIf(m_pass, m_lastPass, pass);
		setIf(m_conf, m_lastConf, conf);

		MpBpmEnsureCandList();
		BOOL candPaint = FALSE;
		auto setCand = [&](CCustomStandardButton& btn, CString& last, int idx) {
			if (!btn.GetSafeHwnd()) return;
			const int c = savedata.mpBpmCand[idx];
			CString t;
			if (c > 0) t.Format(L"%d", c);
			else t = L"--";
			const BOOL changed = (last != t);
			if (changed) {
				last = t;
				btn.SetWindowText(t);
				candPaint = TRUE;
			}
			btn.EnableWindow(c > 0 ? TRUE : FALSE);
			if (changed)
				btn.RepaintClient();
		};
		setCand(m_cand1, m_lastCand1, 0);
		setCand(m_cand2, m_lastCand2, 1);
		setCand(m_cand3, m_lastCand3, 2);
		if (candPaint) {
			if (m_remeasure.GetSafeHwnd()) m_remeasure.RepaintClient();
			if (m_abort.GetSafeHwnd()) m_abort.RepaintClient();
			if (m_close.GetSafeHwnd()) m_close.RepaintClient();
			PostMessage(CCC_MSG_REAPPLY_OPAQUE_FIXERS);
		}

		if (mp && mp->m_botBpm.GetSafeHwnd()) {
			CString bot = MpBpmIsMeasuring() ? L"…" : L"BPM";
			CString cur;
			mp->m_botBpm.GetWindowText(cur);
			if (cur != bot)
				mp->m_botBpm.SetWindowText(bot);
		}
	}
protected:
	virtual void DoDataExchange(CDataExchange* pDX)
	{
		CCustomBlurDialogBase::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_MP_BPM_STATUS, m_status);
		DDX_Control(pDX, IDC_MP_BPM_VAL, m_bpm);
		DDX_Control(pDX, IDC_MP_BPM_METER, m_meter);
		DDX_Control(pDX, IDC_MP_BPM_PULSE, m_pulse);
		DDX_Control(pDX, IDC_MP_BPM_PASS, m_pass);
		DDX_Control(pDX, IDC_MP_BPM_CONF, m_conf);
		DDX_Control(pDX, IDC_MP_BPM_CAND1, m_cand1);
		DDX_Control(pDX, IDC_MP_BPM_CAND2, m_cand2);
		DDX_Control(pDX, IDC_MP_BPM_CAND3, m_cand3);
		DDX_Control(pDX, IDC_MP_BPM_REMEAS, m_remeasure);
		DDX_Control(pDX, IDC_MP_BPM_ABORT, m_abort);
		DDX_Control(pDX, IDC_MP_BPM_CLOSE, m_close);
		DDX_Control(pDX, IDC_MP_BPM_HELP, m_help);
	}
	virtual BOOL OnInitDialog()
	{
		CCustomBlurDialogBase::OnInitDialog();
		SetWindowText(LL14(L"BPM 計測", L"BPM Measure", L"Mesure BPM", L"Misura BPM", L"Medir BPM",
			L"BPM 측정", L"BPM 测量", L"قياس BPM", L"Измерение BPM", L"BPM messen",
			L"Medir BPM", L"BPM meten", L"Pomiar BPM", L"BPM olcum"));
		m_help.SetWindowText(L"?");
		m_help.SetFlat(TRUE);
		m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
		m_status.SetAeroMode(FALSE);
		m_bpm.SetAeroMode(FALSE);
		m_meter.SetAeroMode(FALSE);
		m_pulse.SetAeroMode(FALSE);
		m_pass.SetAeroMode(FALSE);
		m_conf.SetAeroMode(FALSE);
		m_cand1.SetAeroMode(FALSE);
		m_cand2.SetAeroMode(FALSE);
		m_cand3.SetAeroMode(FALSE);
		m_remeasure.SetAeroMode(FALSE);
		m_abort.SetAeroMode(FALSE);
		m_close.SetAeroMode(FALSE);
		m_help.SetAeroMode(FALSE);
		m_cand1.SetFlat(TRUE);
		m_cand2.SetFlat(TRUE);
		m_cand3.SetFlat(TRUE);
		m_remeasure.SetFlat(TRUE);
		m_abort.SetFlat(TRUE);
		m_close.SetFlat(TRUE);
		// 淡色グラデはアクリル下で白抜けしやすい → やや濃い不透明トーン
		m_cand1.SetGradation(RGB(255, 210, 185), RGB(240, 150, 120), 0, TRUE);
		m_cand2.SetGradation(RGB(255, 210, 185), RGB(240, 150, 120), 0, TRUE);
		m_cand3.SetGradation(RGB(255, 210, 185), RGB(240, 150, 120), 0, TRUE);
		m_remeasure.SetGradation(RGB(255, 220, 190), RGB(245, 170, 120), 0, TRUE);
		m_abort.SetGradation(RGB(255, 200, 200), RGB(235, 140, 140), 0, TRUE);
		m_close.SetGradation(RGB(255, 220, 190), RGB(245, 170, 120), 0, TRUE);
		m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
		m_remeasure.SetWindowText(LL14(L"再計測", L"Remeasure", L"Remesurer", L"Rimisura", L"Volver a medir",
			L"재측정", L"重新测量", L"إعادة القياس", L"Перемерить", L"Neu messen",
			L"Remedir", L"Opnieuw meten", L"Zmierz ponownie", L"Yeniden olc"));
		m_abort.SetWindowText(LL14(L"測定中止", L"Abort", L"Arreter", L"Annulla", L"Abortar",
			L"측정 중지", L"中止测量", L"إيقاف القياس", L"Прервать", L"Abbruch",
			L"Abortar", L"Afbreken", L"Przerwij", L"Durdur"));
		m_close.SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar",
			L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schliessen",
			L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
		CCustomControlUtility::BeginDialogToolTip(m_tooltip, this, TTS_NOPREFIX);
		m_tooltip.AddTool(&m_cand1, LL14(L"候補1を拍グリッドへ採用します。", L"Apply candidate 1 to the beat grid.", L"Appliquer le candidat 1 a la grille.", L"Applica il candidato 1 alla griglia.", L"Aplicar candidato 1 a la rejilla.",
			L"후보1을 비트 그리드에 적용.", L"将候选1应用到拍网格。", L"تطبيق المرشح 1 على الشبكة.", L"Применить кандидата 1 к сетке.", L"Kandidat 1 auf Raster anwenden.",
			L"Aplicar candidato 1 na grade.", L"Kandidaat 1 op raster toepassen.", L"Zastosuj kandydata 1 do siatki.", L"Aday 1'i izgaraya uygula."));
		m_tooltip.AddTool(&m_cand2, LL14(L"候補2を拍グリッドへ採用します。", L"Apply candidate 2 to the beat grid.", L"Appliquer le candidat 2 a la grille.", L"Applica il candidato 2 alla griglia.", L"Aplicar candidato 2 a la rejilla.",
			L"후보2를 비트 그리드에 적용.", L"将候选2应用到拍网格。", L"تطبيق المرشح 2 على الشبكة.", L"Применить кандидата 2 к сетке.", L"Kandidat 2 auf Raster anwenden.",
			L"Aplicar candidato 2 na grade.", L"Kandidaat 2 op raster toepassen.", L"Zastosuj kandydata 2 do siatki.", L"Aday 2'yi izgaraya uygula."));
		m_tooltip.AddTool(&m_cand3, LL14(L"候補3を拍グリッドへ採用します。", L"Apply candidate 3 to the beat grid.", L"Appliquer le candidat 3 a la grille.", L"Applica il candidato 3 alla griglia.", L"Aplicar candidato 3 a la rejilla.",
			L"후보3을 비트 그리드에 적용.", L"将候选3应用到拍网格。", L"تطبيق المرشح 3 على الشبكة.", L"Применить кандидата 3 к сетке.", L"Kandidat 3 auf Raster anwenden.",
			L"Aplicar candidato 3 na grade.", L"Kandidaat 3 op raster toepassen.", L"Zastosuj kandydata 3 do siatki.", L"Aday 3'u izgaraya uygula."));
		m_tooltip.AddTool(&m_remeasure, LL14(L"計測を最初からやり直します（一致確認もリセット）。", L"Restart measurement from scratch (also resets confirm).", L"Recommencer la mesure (et la confirmation).", L"Ricomincia la misura (e la conferma).", L"Reiniciar la medicion (y la confirmacion).",
			L"측정을 처음부터 다시 (확인도 리셋).", L"从头重测（也重置确认）。", L"إعادة القياس من البداية (وإعادة التأكيد).", L"Начать измерение сначала (и подтверждение).", L"Messung von vorn (auch Bestaetigung).",
			L"Reiniciar medicao (e confirmacao).", L"Meting opnieuw (ook bevestiging).", L"Zacznij pomiar od nowa (tez potwierdzenie).", L"Olcumu bastan baslat (onayi da sifirla)."));
		m_tooltip.AddTool(&m_abort, LL14(L"計測を中止します（グリッドへは反映しません）。", L"Abort measurement (does not apply to the grid).", L"Arreter la mesure (sans appliquer a la grille).", L"Annulla la misura (non applica alla griglia).", L"Abortar medicion (no aplica a la rejilla).",
			L"측정을 중지합니다 (그리드에 반영 안 함).", L"中止测量（不应用到网格）。", L"إيقاف القياس (دون تطبيقه على الشبكة).", L"Прервать измерение (не применять к сетке).", L"Messung abbrechen (nicht auf Raster).",
			L"Abortar medicao (nao aplica na grade).", L"Meting afbreken (niet op raster).", L"Przerwij pomiar (bez siatki).", L"Olcumu durdur (izgaraya uygulama)."));
		m_tooltip.AddTool(&m_close, LL14(L"この窓を閉じます（計測中なら中止します）。", L"Close this window (aborts if measuring).", L"Fermer cette fenetre (arrete si mesure).", L"Chiudi questa finestra (annulla se in misura).", L"Cerrar esta ventana (aborta si mide).",
			L"이 창을 닫습니다(측정 중이면 중지).", L"关闭此窗口（测量中则中止）。", L"إغلاق هذه النافذة (إيقاف إن كان يقيس).", L"Закрыть окно (прервать при измерении).", L"Fenster schliessen (bei Messung abbrechen).",
			L"Fechar esta janela (aborta se medindo).", L"Dit venster sluiten (afbreken bij meten).", L"Zamknij okno (przerwij przy pomiarze).", L"Bu pencereyi kapat (olcuyorsa durdur)."));
		CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 420, 9000);
		LayoutHelpBtn();
		CCC_CaptionLayout(m_hWnd);
		PostMessage(CCC_MSG_REAPPLY_OPAQUE_FIXERS);
		CCC_RefreshKids(m_hWnd);
		RefreshFromGlobals();
		return TRUE;
	}
	virtual BOOL PreTranslateMessage(MSG* pMsg)
	{
		if (m_tooltip.GetSafeHwnd()) m_tooltip.RelayEvent(pMsg);
		return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
	}
	virtual void PostNcDestroy() { g_mpBpmDlg = NULL; CCustomBlurDialogBase::PostNcDestroy(); delete this; }
	afx_msg void OnSize(UINT nType, int cx, int cy)
	{
		CCustomBlurDialogBase::OnSize(nType, cx, cy);
		if (GetSafeHwnd()) {
			CCC_CaptionLayout(m_hWnd);
			LayoutHelpBtn();
			PostMessage(CCC_MSG_REAPPLY_OPAQUE_FIXERS);
		}
	}
	afx_msg void OnContextMenu(CWnd* /*pWnd*/, CPoint point)
	{
		if (point.x == -1 && point.y == -1) {
			CRect r; GetWindowRect(&r);
			point = r.CenterPoint();
		}
		CCustomPopupMenu menu;
		menu.SetAeroMode(FALSE);
		MpBpmEnsureCandList();
		for (int ci = 0; ci < 3; ++ci) {
			const int cb = savedata.mpBpmCand[ci];
			if (cb <= 0) continue;
			CString item; item.Format(L"%d", cb);
			const UINT id = 100 + ci;
			menu.AddCheck(id, item, savedata.mpDetectedBpm == cb);
		}
		menu.AddSeparator();
		menu.AddCheck(110, LL14(L"拍グリッド表示", L"Show beat grid", L"Afficher grille", L"Mostra griglia", L"Mostrar rejilla",
			L"비트 그리드 표시", L"显示拍网格", L"إظهار الشبكة", L"Показать сетку", L"Beat-Raster zeigen",
			L"Mostrar grade", L"Beatgrid tonen", L"Pokaz siatke", L"Vurus ızgarasi goster"),
			savedata.mpBeatGrid ? TRUE : FALSE);
		menu.AddCommand(111, LL14(L"再計測", L"Remeasure", L"Remesurer", L"Rimisura", L"Volver a medir",
			L"재측정", L"重新测量", L"إعادة القياس", L"Перемерить", L"Neu messen",
			L"Remedir", L"Opnieuw meten", L"Zmierz ponownie", L"Yeniden olc"));
		menu.AddCommand(112, LL14(L"測定中止", L"Abort", L"Arreter", L"Annulla", L"Abortar",
			L"측정 중지", L"中止测量", L"إيقاف القياس", L"Прервать", L"Abbruch",
			L"Abortar", L"Afbreken", L"Przerwij", L"Durdur"));
		const int cmd = menu.Track(point, this);
		if (cmd >= 100 && cmd <= 102) {
			MpOnBpmCandPick(cmd - 100);
			RefreshFromGlobals();
		} else if (cmd == 110) {
			savedata.mpBeatGrid = savedata.mpBeatGrid ? 0 : 1;
			MpPersistSavedataQuick();
			if (mp && mp->m_seek.GetSafeHwnd()) {
				const float bpm = savedata.mpDetectedBpm > 0 ? (float)savedata.mpDetectedBpm : 120.f;
				const int meter = savedata.mpDetectedMeterNum >= 2 ? savedata.mpDetectedMeterNum : 4;
				mp->m_seek.SetBeatGrid(bpm, savedata.mpBeatGrid ? TRUE : FALSE, savedata.mpBeatGridOffsetMs, meter);
			}
		} else if (cmd == 111) {
			OnRemeasure();
		} else if (cmd == 112) {
			OnAbort();
		}
	}
	afx_msg void OnCand1() { MpOnBpmCandPick(0); RefreshFromGlobals(); }
	afx_msg void OnCand2() { MpOnBpmCandPick(1); RefreshFromGlobals(); }
	afx_msg void OnCand3() { MpOnBpmCandPick(2); RefreshFromGlobals(); }
	afx_msg void OnRemeasure()
	{
		g_bpmConfirmPending = 0;
		g_bpmProvisionalBpm = 0;
		g_bpmProvisionalMeterNum = 0;
		g_bpmProvisionalMeterDen = 0;
		g_bpmProvisionalPulse = 0;
		MpBpmStartConfirmRound();
	}
	afx_msg void OnAbort()
	{
		MpBpmAbortMeasure();
		RefreshFromGlobals();
	}
	afx_msg void OnCloseBtn()
	{
		if (MpBpmIsMeasuring() || g_bpmConfirmPending)
			MpBpmAbortMeasure();
		DestroyWindow();
	}
	afx_msg void OnHelp()
	{
		MessageBox(
			LL14(L"再生中のアタックを解析し、拍子・音符価を推定します。\n1回目は仮結果→自動でもう一度計測し、一致したら確定（グリッド反映）。不一致なら一致するまで続行します。\n「測定中止」で止められます。候補ボタンは手動採用です。",
				L"Analyzes attacks while playing, estimates meter and note values.\nFirst result is provisional; a second matching pass locks it to the grid. On mismatch, continues until two agree.\nUse Abort to stop. Candidate buttons apply manually.",
				L"Analyse les attaques en lecture (ou PC), estime mesure et valeurs de notes, adopte le BPM apres 10+ passes d'accord.\nSi ambigu, 10+ passes de plus.\nBoutons candidats ou clic droit pour changer / recommencer.",
				L"Analizza gli attacchi in riproduzione (o PC), stima metro e valori di nota, adotta il BPM dopo 10+ passaggi concordi.\nSe ambiguo, altri 10+ passaggi.\nPulsanti candidati o clic destro per cambiare / rimisurare.",
				L"Analiza ataques al reproducir (o PC), estima compas y valores de nota, adopta BPM tras 10+ pasadas de acuerdo.\nSi es ambiguo, 10+ pasadas mas.\nBotones de candidatos o clic derecho para cambiar / medir de nuevo.",
				L"재생(또는 PC 소리) 중 어택을 분석해 박자·음표 값을 추정하고 10회 이상 합의된 BPM을 채택합니다.\n모호하면 10회 이상 더 반복합니다.\n후보 버튼이나 우클릭으로 전환·재측정할 수 있습니다.",
				L"分析播放中（或 PC 声）的起音，估计拍号与音符时值，在 10 次以上合意后采用 BPM。\n若模糊则再重复 10 次以上。\n可用候选按钮或右键切换/重测。",
				L"يحلّل الهجمات أثناء التشغيل (أو صوت الجهاز)، ويقدّر الميزان وقيم النغمات، ويعتمد BPM بعد 10+ تمريرات متوافقة.\nإن كان غامضاً يكرر 10+ تمريرات إضافية.\nأزرار المرشحين أو النقر الأيمن للتبديل/إعادة القياس.",
				L"Анализирует атаки при воспроизведении (или ПК), оценивает размер и длительности, принимает BPM после 10+ согласованных проходов.\nПри неоднозначности — ещё 10+ проходов.\nКнопки кандидатов или ПКМ для смены / повтора.",
				L"Analysiert Anschlaege bei Wiedergabe (oder PC), schaetzt Taktart und Notenwerte und uebernimmt BPM nach 10+ uebereinstimmenden Durchlaeufen.\nBei Unklarheit weitere 10+ Durchlaeufe.\nKandidaten-Buttons oder Rechtsklick zum Wechseln / Neu messen.",
				L"Analisa ataques na reproducao (ou PC), estima compasso e valores de nota e adota BPM apos 10+ passes concordantes.\nSe ambiguo, mais 10+ passes.\nBotoes de candidatos ou clique direito para trocar / remedir.",
				L"Analyseert aanslagen tijdens afspelen (of pc-audio), schat maatsoort en nootwaarden, en neemt BPM over na 10+ eensstemmende doorgangen.\nBij twijfel nog 10+ doorgangen.\nKandidaatknoppen of rechtsklik om te wisselen / opnieuw te meten.",
				L"Analizuje ataki podczas odtwarzania (lub PC), szacuje metrum i wartosci nut i przyjmuje BPM po 10+ zgodnych przejsciach.\nPrzy niejednoznacznosci kolejne 10+ przejsc.\nPrzyciski kandydatow lub PPM do zmiany / ponownego pomiaru.",
				L"Calisma (veya PC sesi) sirasinda ataklari analiz eder, olcu ve nota degerlerini tahmin eder, 10+ uyumlu gecisten sonra BPM kabul eder.\nBelirsizse 10+ gecis daha yapar.\nAday dugmeleri veya sag tik ile degistir / yeniden olc."),
			LL14(L"BPM 計測の使い方", L"How to measure BPM", L"Comment mesurer le BPM", L"Come misurare il BPM", L"Como medir el BPM",
				L"BPM 측정 방법", L"如何测量 BPM", L"كيفية قياس BPM", L"Как измерить BPM", L"BPM messen – Hilfe",
				L"Como medir o BPM", L"BPM meten – hulp", L"Jak zmierzyc BPM", L"BPM nasil olculur"),
			MB_OK | MB_ICONINFORMATION);
	}
	afx_msg void OnCancel() { OnCloseBtn(); }
	DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CMpBpmDlg, CCustomBlurDialogBase)
	ON_WM_SIZE()
	ON_WM_CONTEXTMENU()
	ON_BN_CLICKED(IDC_MP_BPM_CAND1, &CMpBpmDlg::OnCand1)
	ON_BN_CLICKED(IDC_MP_BPM_CAND2, &CMpBpmDlg::OnCand2)
	ON_BN_CLICKED(IDC_MP_BPM_CAND3, &CMpBpmDlg::OnCand3)
	ON_BN_CLICKED(IDC_MP_BPM_REMEAS, &CMpBpmDlg::OnRemeasure)
	ON_BN_CLICKED(IDC_MP_BPM_ABORT, &CMpBpmDlg::OnAbort)
	ON_BN_CLICKED(IDC_MP_BPM_CLOSE, &CMpBpmDlg::OnCloseBtn)
	ON_BN_CLICKED(IDC_MP_BPM_HELP, &CMpBpmDlg::OnHelp)
END_MESSAGE_MAP()

static void CloseMpBpmMeasureDlgIfOpen()
{
	if (g_mpBpmDlg && ::IsWindow(g_mpBpmDlg->GetSafeHwnd()))
		g_mpBpmDlg->DestroyWindow();
	g_mpBpmDlg = NULL;
}

static void OpenMpBpmMeasureDlg(CWnd* parent)
{
	if (g_mpBpmDlg && ::IsWindow(g_mpBpmDlg->GetSafeHwnd())) {
		g_mpBpmDlg->ShowWindow(SW_SHOW);
		g_mpBpmDlg->RefreshFromGlobals();
		return;
	}
	if (!parent) parent = mp;
	if (!parent) return;
	g_mpBpmDlg = new CMpBpmDlg(parent);
	if (!g_mpBpmDlg->Create(IDD_MP_BPM, parent)) {
		delete g_mpBpmDlg;
		g_mpBpmDlg = NULL;
		return;
	}
	g_mpBpmDlg->ShowWindow(SW_SHOW);
}

static void MpBpmDlgRefreshUi()
{
	if (g_mpBpmDlg && ::IsWindow(g_mpBpmDlg->GetSafeHwnd()))
		g_mpBpmDlg->RefreshFromGlobals();
	else if (mp && mp->m_botBpm.GetSafeHwnd()) {
		CString bot = MpBpmIsMeasuring() ? L"…" : L"BPM";
		CString cur;
		mp->m_botBpm.GetWindowText(cur);
		if (cur != bot)
			mp->m_botBpm.SetWindowText(bot);
	}
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
	virtual void PostNcDestroy() { g_mpAlarmDlg = NULL; CCustomBlurDialogBase::PostNcDestroy(); delete this; }
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
		int selDev = 0;
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
							if (pid && idx >= 0) {
								m_dev.SetItemData(idx, (DWORD_PTR)_wcsdup(pid));
								if (savedata.mpMirrorDevice[0] && _tcsicmp(savedata.mpMirrorDevice, pid) == 0)
									selDev = idx;
							}
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
		m_dev.SetCurSel(selDev);
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
	virtual void PostNcDestroy() { g_mpMirrorDlg = NULL; CCustomBlurDialogBase::PostNcDestroy(); delete this; }
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
		// ダイアログ閉じてもミラーONなら UI スレッドで再初期化（失敗フラグもリセット）
		InterlockedExchange(&g_mpMirrorFailed, 0);
		if (savedata.mpMirrorOut)
			MpMirrorOnFormatReady();
		else
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
	CCustomCheckBox m_aac;
	CCustomEdit m_port;
	CCustomStatic m_url;
	CCustomStandardButton m_open;
protected:
	virtual void DoDataExchange(CDataExchange* pDX)
	{
		CCustomBlurDialogBase::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_REMOTE_ENABLE, m_enable);
		DDX_Control(pDX, IDC_REMOTE_AAC, m_aac);
		DDX_Control(pDX, IDC_REMOTE_PORT, m_port);
		DDX_Control(pDX, IDC_REMOTE_URL, m_url);
		DDX_Control(pDX, IDC_REMOTE_OPEN, m_open);
	}
	CToolTipCtrl m_tooltip;
	void RefreshUrlLabel()
	{
		int port = savedata.mpRemotePort;
		if (port < 1024 || port > 65535) port = 8765;
		CString p; m_port.GetWindowText(p);
		const int typed = _ttoi(p);
		if (typed >= 1024 && typed <= 65535) port = typed;
		wchar_t lan[64] = {};
		CString url;
		if (MpRemoteGetLanIpv4(lan, 64))
			url.Format(L"http://%s:%d/  (PC: http://127.0.0.1:%d/)", lan, port, port);
		else
			url.Format(L"http://127.0.0.1:%d/", port);
		if (m_url.GetSafeHwnd())
			m_url.SetWindowText(url);
	}
	virtual BOOL OnInitDialog()
	{
		CCustomBlurDialogBase::OnInitDialog();
		SetWindowText(LL14(L"ローカルリモート", L"Local remote", L"Telecommande locale", L"Remote locale", L"Remoto local",
			L"로컬 리모트", L"本地遥控", L"تحكم محلي", L"Локальный пульт", L"Lokalfernbedienung",
			L"Remoto local", L"Lokale bediening", L"Pilot lokalny", L"Yerel uzaktan"));
		m_enable.SetWindowText(LL14(L"Wi-Fi / LAN で HTTP（最大6台）", L"HTTP on Wi-Fi/LAN (max 6)", L"HTTP Wi-Fi/LAN (max 6)", L"HTTP Wi-Fi/LAN (max 6)", L"HTTP Wi-Fi/LAN (máx. 6)",
			L"Wi-Fi/LAN HTTP (최대 6)", L"Wi-Fi/LAN HTTP（最多6）", L"HTTP عبر Wi-Fi/LAN (حد 6)", L"HTTP по Wi-Fi/LAN (до 6)", L"HTTP im WLAN/LAN (max. 6)",
			L"HTTP no Wi-Fi/LAN (máx. 6)", L"HTTP op Wi-Fi/LAN (max 6)", L"HTTP w Wi-Fi/LAN (max 6)", L"Wi-Fi/LAN HTTP (en fazla 6)"));
		m_aac.SetWindowText(LL14(L"AAC をスマホ／PC で聴けるようにする", L"Allow AAC listen on phone/PC", L"Autoriser l'ecoute AAC (tel/PC)", L"Consenti ascolto AAC (tel/PC)", L"Permitir escuchar AAC (movil/PC)",
			L"폰/PC에서 AAC 청취 허용", L"允许在手机/PC 收听 AAC", L"السماح باستماع AAC على الهاتف/الكمبيوتر", L"Разрешить слушать AAC на телефоне/ПК", L"AAC-Hören auf Telefon/PC erlauben",
			L"Permitir ouvir AAC no telemovel/PC", L"AAC beluisteren op telefoon/pc toestaan", L"Zezwol na sluchanie AAC na telefonie/PC", L"Telefon/PC'de AAC dinlemeye izin ver"));
		SetDlgItemText(IDC_REMOTE_PORT_L, LL14(L"ポート", L"Port", L"Port", L"Porta", L"Puerto",
			L"포트", L"端口", L"منفذ", L"Порт", L"Port", L"Porta", L"Poort", L"Port", L"Port"));
		m_open.SetWindowText(LL14(L"ブラウザで開く", L"Open in browser", L"Ouvrir dans le navigateur", L"Apri nel browser", L"Abrir en el navegador",
			L"브라우저에서 열기", L"在浏览器打开", L"فتح في المتصفح", L"Открыть в браузере", L"Im Browser öffnen",
			L"Abrir no navegador", L"Openen in browser", L"Otworz w przegladarce", L"Tarayicida ac"));
		m_enable.SetCheck(savedata.mpRemoteOn ? BST_CHECKED : BST_UNCHECKED);
		m_aac.SetCheck(savedata.mpRemoteAac ? BST_CHECKED : BST_UNCHECKED);
		CString p; p.Format(_T("%d"), savedata.mpRemotePort);
		m_port.SetWindowText(p);
		RefreshUrlLabel();
		m_open.SetGradation(RGB(220, 240, 255), RGB(160, 200, 240), 0, TRUE);
		CCustomControlUtility::BeginDialogToolTip(m_tooltip, this, TTS_NOPREFIX);
		m_tooltip.AddTool(&m_enable, LL14(L"同じ Wi-Fi のスマホ／PC から再生操作できる HTTP サーバ（同時6接続まで）。", L"HTTP server for phones/PCs on the same Wi-Fi (up to 6 clients).", L"Serveur HTTP pour telephones/PC sur le meme Wi-Fi (max 6).", L"Server HTTP per telefoni/PC sulla stessa Wi-Fi (max 6).", L"Servidor HTTP para moviles/PC en la misma Wi-Fi (máx. 6).",
			L"같은 Wi-Fi의 폰/PC에서 조작하는 HTTP 서버(최대 6).", L"同一 Wi-Fi 下手机/PC 控制的 HTTP 服务器（最多6）。", L"خادم HTTP للهواتف/أجهزة الكمبيوتر على نفس Wi-Fi (حد 6).", L"HTTP-сервер для телефонов/ПК в той же Wi-Fi (до 6).", L"HTTP-Server für Telefone/PCs im gleichen WLAN (max. 6).",
			L"Servidor HTTP para telemoveis/PCs na mesma Wi-Fi (máx. 6).", L"HTTP-server voor telefoons/pc's op hetzelfde Wi-Fi (max 6).", L"Serwer HTTP dla telefonow/PC w tej samej Wi-Fi (max 6).", L"Ayni Wi-Fi'deki telefon/PC icin HTTP sunucusu (en fazla 6)."));
		m_tooltip.AddTool(&m_aac, LL14(L"リモコンの「聴く」で再生中の音を AAC 圧縮して送ります（遅延約0.5〜1秒）。接続がある間だけエンコードします。", L"Remote \"Listen\" sends playing audio as AAC (~0.5–1s latency). Encodes only while someone is connected.", L"\"Ecouter\" envoie l'audio en AAC (latence ~0,5–1 s). Encode seulement si connecte.", L"\"Ascolta\" invia l'audio in AAC (latenza ~0,5–1 s). Codifica solo se connesso.", L"\"Escuchar\" envia el audio en AAC (latencia ~0,5–1 s). Codifica solo si hay conexion.",
			L"리모트의 \"듣기\"로 재생음을 AAC로 보냅니다(지연 약 0.5–1초). 연결 중에만 인코딩.", L"遥控「收听」以 AAC 发送播放音频（延迟约 0.5–1 秒）。仅在有连接时编码。", L"\"استماع\" يرسل الصوت كـ AAC (تأخير ~0.5–1 ث). يُرمَّز أثناء الاتصال فقط.", L"«Слушать» шлёт звук как AAC (~0,5–1 с). Кодирует только при подключении.", L"„Hören“ sendet Audio als AAC (~0,5–1 s Latenz). Kodiert nur bei Verbindung.",
			L"\"Ouvir\" envia o audio em AAC (latencia ~0,5–1 s). Codifica so com ligacao.", L"\"Luisteren\" stuurt audio als AAC (~0,5–1 s). Encodeert alleen bij verbinding.", L"\"Sluchaj\" wysyla dzwiek jako AAC (~0,5–1 s). Koduje tylko przy polaczeniu.", L"\"Dinle\" sesi AAC olarak gonderir (~0,5–1 sn). Yalniz bagliyken kodlar."));
		m_tooltip.AddTool(&m_port, LL14(L"待ち受けポート (1024–65535)。", L"Listen port (1024–65535).", L"Port d'ecoute (1024–65535).", L"Porta di ascolto (1024–65535).", L"Puerto de escucha (1024–65535).",
			L"수신 포트 (1024–65535).", L"监听端口 (1024–65535)。", L"منفذ الاستماع (1024–65535).", L"Порт прослушивания (1024–65535).", L"Listenport (1024–65535).",
			L"Porta de escuta (1024–65535).", L"Luisterpoort (1024–65535).", L"Port nasluchu (1024–65535).", L"Dinleme portu (1024–65535)."));
		m_tooltip.AddTool(&m_url, LL14(L"スマホは LAN の URL、PC は 127.0.0.1 でも可。初回はファイアウォール許可が必要なことがあります。", L"Phones use the LAN URL; PC can use 127.0.0.1. Firewall may ask once.", L"Telephones: URL LAN; PC: 127.0.0.1. Pare-feu possible.", L"Telefoni: URL LAN; PC: 127.0.0.1. Firewall possibile.", L"Moviles: URL LAN; PC: 127.0.0.1. Puede pedir firewall.",
			L"폰은 LAN URL, PC는 127.0.0.1 가능. 방화벽 허용이 필요할 수 있음.", L"手机用 LAN URL；PC 可用 127.0.0.1。可能需允许防火墙。", L"الهواتف: رابط LAN؛ الكمبيوتر: 127.0.0.1. قد يطلب الجدار الناري.", L"Телефоны: LAN URL; ПК: 127.0.0.1. Может спросить брандмауэр.", L"Telefone: LAN-URL; PC: 127.0.0.1. Firewall ggf. einmal erlauben.",
			L"Telemoveis: URL LAN; PC: 127.0.0.1. Firewall pode pedir.", L"Telefoons: LAN-URL; pc: 127.0.0.1. Firewall kan vragen.", L"Telefony: URL LAN; PC: 127.0.0.1. Firewall moze zapytac.", L"Telefonlar: LAN URL; PC: 127.0.0.1. Guvenlik duvari isteyebilir."));
		m_tooltip.AddTool(&m_open, LL14(L"既定ブラウザでリモコンページを開きます。", L"Open the remote page in the default browser.", L"Ouvrir la page telecommande dans le navigateur.", L"Apri la pagina remote nel browser.", L"Abrir la pagina remota en el navegador.",
			L"기본 브라우저로 리모트 페이지를 엽니다.", L"用默认浏览器打开遥控页。", L"فتح صفحة التحكم في المتصفح الافتراضي.", L"Открыть страницу пульта в браузере.", L"Remote-Seite im Standardbrowser öffnen.",
			L"Abrir a pagina remota no navegador padrao.", L"Remote-pagina openen in standaardbrowser.", L"Otworz strone pilota w przegladarce", L"Uzaktan sayfasini varsayilan tarayicida ac."));
		CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 360, 8000);
		return TRUE;
	}
	virtual BOOL PreTranslateMessage(MSG* pMsg)
	{
		if (m_tooltip.GetSafeHwnd()) m_tooltip.RelayEvent(pMsg);
		return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
	}
	virtual void PostNcDestroy() { g_mpRemoteDlg = NULL; CCustomBlurDialogBase::PostNcDestroy(); delete this; }
	afx_msg void OnDestroy()
	{
		savedata.mpRemoteOn = m_enable.GetCheck() ? 1 : 0;
		savedata.mpRemoteAac = m_aac.GetCheck() ? 1 : 0;
		CString p; m_port.GetWindowText(p);
		int port = _ttoi(p);
		if (port >= 1024 && port <= 65535) savedata.mpRemotePort = port;
		MpPersistSavedataQuick();
		if (mp) MpRemoteEnsureRunning(mp->GetSafeHwnd());
		CCustomBlurDialogBase::OnDestroy();
	}
	afx_msg void OnEnChangePort() { RefreshUrlLabel(); }
	afx_msg void OnOpenBrowser()
	{
		RefreshUrlLabel();
		int port = savedata.mpRemotePort;
		CString p; m_port.GetWindowText(p);
		const int typed = _ttoi(p);
		if (typed >= 1024 && typed <= 65535) port = typed;
		if (port < 1024 || port > 65535) port = 8765;
		CString url;
		url.Format(L"http://127.0.0.1:%d/", port);
		// 有効化してから開く（無効のままだと接続できない）
		if (m_enable.GetCheck() != BST_CHECKED) {
			m_enable.SetCheck(BST_CHECKED);
			savedata.mpRemoteOn = 1;
		}
		if (port >= 1024 && port <= 65535) savedata.mpRemotePort = port;
		MpPersistSavedataQuick();
		if (mp) MpRemoteEnsureRunning(mp->GetSafeHwnd());
		::ShellExecute(NULL, L"open", url, NULL, NULL, SW_SHOWNORMAL);
	}
	afx_msg void OnContextMenu(CWnd*, CPoint point)
	{
		if (point.x == -1 && point.y == -1) {
			CRect r; GetWindowRect(&r);
			point.x = r.left + 40; point.y = r.top + 40;
		}
		const BOOL enabled = m_enable.GetCheck() == BST_CHECKED;
		const BOOL aacOn = m_aac.GetCheck() == BST_CHECKED;
		CCustomPopupMenu menu;
		menu.SetAeroMode(FALSE);
		menu.AddCheck(10,
			LL14(L"リモート有効", L"Enable remote", L"Activer le remote", L"Abilita remote", L"Activar remoto",
				L"리모트 사용", L"启用遥控", L"تفعيل التحكم", L"Включить пульт", L"Remote aktivieren",
				L"Ativar remoto", L"Remote inschakelen", L"Wlacz pilota", L"Uzaktan etkin"),
			enabled,
			LL14(L"同一LAN上のブラウザから再生操作できるリモートUIを開始/停止します。", L"Start/stop the remote UI so browsers on the same LAN can control playback.", L"Demarre/arrete l'UI remote pour controler la lecture depuis le LAN.", L"Avvia/ferma l'UI remota per controllare la riproduzione dalla LAN.", L"Inicia/detiene la UI remota para controlar la reproduccion desde la LAN.",
				L"같은 LAN의 브라우저에서 재생을 조작하는 리모트 UI를 시작/중지합니다.", L"启动/停止远程 UI，使同一局域网浏览器可控制播放。", L"بدء/إيقاف واجهة التحكم عن بُعد من متصفحات الشبكة المحلية.", L"Вкл/выкл удалённый UI для управления воспроизведением из LAN.", L"Remote-UI starten/stoppen, damit Browser im LAN die Wiedergabe steuern.",
				L"Iniciar/parar a UI remota para controlar a reproducao na mesma LAN.", L"Start/stop de remote-UI zodat browsers op het LAN afspelen kunnen bedienen.", L"Wlacz/wylacz UI zdalne do sterowania odtwarzaniem z LAN.", L"Ayni LAN'daki tarayicilarin calmayi kontrol etmesi icin remote UI baslat/durdur."));
		menu.AddCheck(13,
			LL14(L"AAC 聴く", L"AAC listen", L"Ecoute AAC", L"Ascolto AAC", L"Escuchar AAC",
				L"AAC 듣기", L"AAC 收听", L"استماع AAC", L"Слушать AAC", L"AAC hören",
				L"Ouvir AAC", L"AAC luisteren", L"Sluchaj AAC", L"AAC dinle"),
			aacOn,
			LL14(L"リモート接続先へAACストリームを送り、ブラウザで音を聴けます（帯域を使います）。", L"Send an AAC stream to remote clients so the browser can hear audio (uses bandwidth).", L"Envoie un flux AAC aux clients remote pour ecouter dans le navigateur (bande passante).", L"Invia uno stream AAC ai client remoti per ascoltare nel browser (usa banda).", L"Envia un flujo AAC a clientes remotos para oir en el navegador (usa ancho de banda).",
				L"리모트 클라이언트로 AAC 스트림을 보내 브라우저에서 들을 수 있습니다(대역폭 사용).", L"向远程客户端发送 AAC 流，可在浏览器听音（占用带宽）。", L"إرسال بث AAC للعملاء عن بُعد للاستماع في المتصفح (يستهلك النطاق).", L"Отправлять AAC-поток удалённым клиентам для прослушивания в браузере (тратит полосу).", L"AAC-Stream an Remote-Clients senden, damit der Browser hören kann (Bandbreite).",
				L"Enviar um fluxo AAC aos clientes remotos para ouvir no navegador (usa banda).", L"Stuur een AAC-stream naar remote-clients zodat de browser kan meeluisteren (bandbreedte).", L"Wysylaj strumien AAC do klientow zdalnych, by sluchac w przegladarce (zuzywa pasmo).", L"Tarayicida dinlemek icin remote istemcilere AAC akisi gonder (bant genisligi kullanir)."));
		menu.AddCommand(11,
			LL14(L"URL をコピー", L"Copy URL", L"Copier l'URL", L"Copia URL", L"Copiar URL",
				L"URL 복사", L"复制 URL", L"نسخ الرابط", L"Копировать URL", L"URL kopieren",
				L"Copiar URL", L"URL kopieren", L"Kopiuj URL", L"URL kopyala"),
			LL14(L"LAN用のリモートURLをクリップボードへコピー。スマホ等へ貼り付けて開きます。", L"Copy the LAN remote URL to the clipboard — paste on a phone etc. to open.", L"Copie l'URL remote LAN dans le presse-papiers — a coller sur telephone etc.", L"Copia l'URL remota LAN negli appunti — incolla su telefono ecc.", L"Copia la URL remota LAN al portapapeles — pegala en el telefono etc.",
				L"LAN용 리모트 URL을 클립보드에 복사. 스마트폰 등에 붙여 엽니다.", L"将局域网远程 URL 复制到剪贴板，可贴到手机等打开。", L"نسخ رابط التحكم عن بُعد للشبكة المحلية إلى الحافظة لفتحه على الهاتف.", L"Скопировать LAN-URL пульта в буфер — вставить на телефон и открыть.", L"LAN-Remote-URL in die Zwischenablage — auf dem Handy einfügen und öffnen.",
				L"Copiar a URL remota da LAN para a area de transferencia — cole no celular etc.", L"Kopieer de LAN-remote-URL naar het klembord — plak op telefoon enz.", L"Kopiuj LAN-URL pilota do schowka — wklej na telefonie itp.", L"LAN remote URL'sini panoya kopyala — telefona yapistirip ac."));
		menu.AddCommand(12,
			LL14(L"ブラウザで開く", L"Open in browser", L"Ouvrir dans le navigateur", L"Apri nel browser", L"Abrir en el navegador",
				L"브라우저에서 열기", L"在浏览器打开", L"فتح في المتصفح", L"Открыть в браузере", L"Im Browser öffnen",
				L"Abrir no navegador", L"Openen in browser", L"Otworz w przegladarce", L"Tarayicida ac"),
			LL14(L"リモートを有効にして既定ブラウザで操作ページを開きます。", L"Enable remote if needed and open the control page in the default browser.", L"Active le remote si besoin et ouvre la page de controle dans le navigateur.", L"Abilita il remote se serve e apre la pagina di controllo nel browser.", L"Activa el remoto si hace falta y abre la pagina de control en el navegador.",
				L"필요 시 리모트를 켠 뒤 기본 브라우저에서 조작 페이지를 엽니다.", L"如需则启用远程，并用默认浏览器打开控制页。", L"تفعيل التحكم عند الحاجة وفتح صفحة التحكم في المتصفح الافتراضي.", L"При необходимости включить пульт и открыть страницу управления в браузере.", L"Remote bei Bedarf aktivieren und die Steuerseite im Standardbrowser öffnen.",
				L"Ativar o remoto se preciso e abrir a pagina de controle no navegador padrao.", L"Schakel remote in indien nodig en open de bedieningspagina in de standaardbrowser.", L"Wlacz pilota w razie potrzeby i otworz strone sterowania w domyslnej przegladarce.", L"Gerekirse remote'u acip kontrol sayfasini varsayilan tarayicida ac."));
		menu.AddSeparator();
		menu.AddCommand(1,
			LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"),
			LL14(L"リモート設定ウィンドウを閉じます（サービス状態はそのまま）。", L"Close the remote settings window (service state unchanged).", L"Ferme la fenetre remote (etat du service inchange).", L"Chiude la finestra remote (stato servizio invariato).", L"Cierra la ventana remota (el estado del servicio no cambia).",
				L"리모트 설정 창을 닫습니다(서비스 상태는 유지).", L"关闭远程设置窗口（服务状态不变）。", L"إغلاق نافذة إعدادات التحكم (حالة الخدمة دون تغيير).", L"Закрыть окно настроек пульта (состояние службы не меняется).", L"Remote-Einstellungsfenster schließen (Dienststatus bleibt).",
				L"Fechar a janela de configuracao remota (estado do servico inalterado).", L"Sluit het remote-instellingenvenster (servicestatus blijft).", L"Zamknij okno ustawien pilota (stan uslugi bez zmian).", L"Remote ayar penceresini kapat (servis durumu ayni kalir)."));
		const UINT cmd = menu.Track(point, this);
		if (cmd == 1) {
			DestroyWindow();
		} else if (cmd == 10) {
			const BOOL next = !enabled;
			m_enable.SetCheck(next ? BST_CHECKED : BST_UNCHECKED);
			savedata.mpRemoteOn = next ? 1 : 0;
			MpPersistSavedataQuick();
			if (mp) MpRemoteEnsureRunning(mp->GetSafeHwnd());
			RefreshUrlLabel();
		} else if (cmd == 13) {
			const BOOL next = !aacOn;
			m_aac.SetCheck(next ? BST_CHECKED : BST_UNCHECKED);
			savedata.mpRemoteAac = next ? 1 : 0;
			MpPersistSavedataQuick();
		} else if (cmd == 11) {
			RefreshUrlLabel();
			int port = savedata.mpRemotePort;
			CString p; m_port.GetWindowText(p);
			const int typed = _ttoi(p);
			if (typed >= 1024 && typed <= 65535) port = typed;
			if (port < 1024 || port > 65535) port = 8765;
			wchar_t lan[64] = {};
			CString url;
			if (MpRemoteGetLanIpv4(lan, 64))
				url.Format(L"http://%s:%d/", lan, port);
			else
				url.Format(L"http://127.0.0.1:%d/", port);
			if (::OpenClipboard(m_hWnd)) {
				::EmptyClipboard();
				const size_t bytes = (size_t)(url.GetLength() + 1) * sizeof(wchar_t);
				HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
				if (h) {
					wchar_t* buf = (wchar_t*)::GlobalLock(h);
					if (buf) {
						memcpy(buf, (LPCWSTR)url, bytes);
						::GlobalUnlock(h);
						::SetClipboardData(CF_UNICODETEXT, h);
					} else {
						::GlobalFree(h);
					}
				}
				::CloseClipboard();
			}
		} else if (cmd == 12) {
			OnOpenBrowser();
		}
	}
	afx_msg void OnRButtonUp(UINT, CPoint point)
	{
		ClientToScreen(&point);
		OnContextMenu(this, point);
	}
	DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CMpRemoteDlg, CCustomBlurDialogBase)
	ON_WM_DESTROY()
	ON_EN_CHANGE(IDC_REMOTE_PORT, &CMpRemoteDlg::OnEnChangePort)
	ON_BN_CLICKED(IDC_REMOTE_OPEN, &CMpRemoteDlg::OnOpenBrowser)
	ON_WM_CONTEXTMENU()
	ON_WM_RBUTTONUP()
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
	virtual void PostNcDestroy() { g_mpSsViz = NULL; CCustomBlurDialogBase::PostNcDestroy(); delete this; }
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
		const BOOL topMost = (GetExStyle() & WS_EX_TOPMOST) != 0;
		const BOOL maximized = IsZoomed();
		CCustomPopupMenu menu;
		menu.SetAeroMode(FALSE);
		menu.AddCheck(2,
			LL14(L"最前面", L"Always on top", L"Toujours au premier plan", L"Sempre in primo piano",
				L"Siempre visible", L"항상 위", L"置顶", L"دائماً في المقدمة", L"Поверх всех", L"Immer im Vordergrund",
				L"Sempre no topo", L"Altijd boven", L"Zawsze na wierzchu", L"Her zaman ustte"),
			topMost,
			LL14(L"スペアナ可視化窓を他の窓の上に常時表示します。", L"Keep this spectrum visualizer above other windows.", L"Garde ce visualiseur de spectre au-dessus des autres fenetres.", L"Tiene questo visualizzatore di spettro sopra le altre finestre.", L"Mantiene este visualizador de espectro encima de otras ventanas.",
				L"스펙 시각화 창을 다른 창 위에 항상 표시합니다.", L"将频谱可视化窗口始终置于其他窗口之上。", L"إبقاء نافذة طيف الطيف فوق النوافذ الأخرى.", L"Держать окно спектра поверх других окон.", L"Dieses Spektrum-Fenster immer über anderen Fenstern halten.",
				L"Manter este visualizador de espectro acima das outras janelas.", L"Houd dit spectrumvenster boven andere vensters.", L"Trzymaj to okno spektrum nad innymi oknami.", L"Spektrum gorsel penceresini diger pencerelerin ustunde tut."));
		menu.AddCheck(3,
			LL14(L"最大化", L"Maximize", L"Agrandir", L"Ingrandisci", L"Maximizar", L"최대화", L"最大化", L"تكبير",
				L"Развернуть", L"Maximieren", L"Maximizar", L"Maximaliseren", L"Maksymalizuj", L"Buyut"),
			maximized,
			LL14(L"画面いっぱいに広げ/元のサイズへ戻します。Escでも閉じられます。", L"Maximize to fill the screen, or restore. Esc also closes.", L"Plein ecran ou restaurer. Esc ferme aussi.", L"Schermo intero o ripristina. Esc chiude pure.", L"Pantalla completa o restaurar. Esc tambien cierra.",
				L"화면 가득 최대화하거나 원래 크기로. Esc로도 닫힙니다.", L"最大化铺满屏幕或还原。Esc 也可关闭。", L"تكبير لملء الشاشة أو الاستعادة. Esc يغلق أيضاً.", L"Развернуть на весь экран или восстановить. Esc тоже закрывает.", L"Maximieren oder wiederherstellen. Esc schließt ebenfalls.",
				L"Maximizar para preencher a tela ou restaurar. Esc tambem fecha.", L"Maximaliseren of herstellen. Esc sluit ook.", L"Maksymalizuj lub przywroc. Esc tez zamyka.", L"Ekrani kaplayacak sekilde buyut veya eski boyuta don. Esc de kapatir."));
		menu.AddSeparator();
		{
			CCustomPopupMenu* styleSub = menu.AddSubMenu(
				LL14(L"スペアナ様式", L"Speana style", L"Style speana", L"Stile speana", L"Estilo speana",
					L"스펙애너 스타일", L"频谱样式", L"نمط speana", L"Стиль speana", L"Speana-Stil",
					L"Estilo speana", L"Speana-stijl", L"Styl speana", L"Speana stili"),
				LL14(L"メインのスペアナ表示様式を切替（本窓は棒グラフ固定）", L"Switch main speana style (this window stays bars)",
					L"Changer le style speana principal", L"Cambia lo stile speana principale",
					L"Cambiar el estilo speana principal", L"메인 스펙애너 스타일 전환",
					L"切换主频谱样式", L"تبديل نمط speana الرئيسي",
					L"Сменить стиль speana в плеере", L"Haupt-Speana-Stil wechseln",
					L"Alternar estilo speana principal", L"Hoofd-speana-stijl wisselen",
					L"Przelacz styl speana w odtwarzaczu", L"Ana speana stilini degistir"));
			if (styleSub) {
				styleSub->AddCheck(10,
					LL14(L"棒", L"Bars", L"Barres", L"Barre", L"Barras", L"막대", L"柱状", L"أعمدة", L"Столбцы", L"Balken", L"Barras", L"Balken", L"Slupki", L"Cubuk"),
					savedata.mpSpeanaStyle == 0,
					LL14(L"メイン画面のスペアナを棒グラフ表示にします。", L"Set the main player's speana to bar graph style.", L"Affiche le speana principal en barres.", L"Imposta lo speana principale a barre.", L"Pone el speana principal en barras.",
						L"메인 화면 스펙애너를 막대 그래프로 표시합니다.", L"将主界面频谱设为柱状图。", L"تعيين speana الرئيسي إلى أعمدة.", L"Столбцовый стиль speana в главном плеере.", L"Haupt-Speana als Balken darstellen.",
						L"Definir o speana principal como barras.", L"Zet de hoofd-speana op staafdiagram.", L"Ustaw speana w odtwarzaczu na slupki.", L"Ana speanayi cubuk grafik stiline al."));
				styleSub->AddCheck(11,
					LL14(L"ミラー", L"Mirror", L"Miroir", L"Specchio", L"Espejo", L"미러", L"镜像", L"مرآة", L"Зеркало", L"Spiegel", L"Espelho", L"Spiegel", L"Lustrzane", L"Ayna"),
					savedata.mpSpeanaStyle == 1,
					LL14(L"メイン画面のスペアナを左右ミラー表示にします。", L"Set the main player's speana to mirrored left/right style.", L"Affiche le speana principal en miroir G/D.", L"Imposta lo speana principale a specchio S/D.", L"Pone el speana principal en espejo I/D.",
						L"메인 화면 스펙애너를 좌우 미러로 표시합니다.", L"将主界面频谱设为左右镜像。", L"تعيين speana الرئيسي إلى مرآة يمين/يسار.", L"Зеркальный стиль speana в главном плеере.", L"Haupt-Speana gespiegelt L/R darstellen.",
						L"Definir o speana principal como espelho E/D.", L"Zet de hoofd-speana op spiegel L/R.", L"Ustaw speana w odtwarzaczu na lustrzane L/P.", L"Ana speanayi sol/sag ayna stiline al."));
				styleSub->AddCheck(12,
					LL14(L"波形", L"Wave", L"Onde", L"Onda", L"Onda", L"파형", L"波形", L"موجة", L"Волна", L"Welle", L"Onda", L"Golf", L"Fala", L"Dalga"),
					savedata.mpSpeanaStyle == 2,
					LL14(L"メイン画面のスペアナを波形表示にします。", L"Set the main player's speana to waveform style.", L"Affiche le speana principal en forme d'onde.", L"Imposta lo speana principale a forma d'onda.", L"Pone el speana principal en forma de onda.",
						L"메인 화면 스펙애너를 파형으로 표시합니다.", L"将主界面频谱设为波形。", L"تعيين speana الرئيسي إلى موجة.", L"Волновой стиль speana в главном плеере.", L"Haupt-Speana als Wellenform darstellen.",
						L"Definir o speana principal como forma de onda.", L"Zet de hoofd-speana op golfvorm.", L"Ustaw speana w odtwarzaczu na fale.", L"Ana speanayi dalgaformu stiline al."));
			}
		}
		menu.AddCommand(13,
			LL14(L"アナライザを開く", L"Open analyzer", L"Ouvrir l'analyseur", L"Apri analizzatore", L"Abrir analizador",
				L"분석기 열기", L"打开分析器", L"فتح المحلل", L"Открыть анализатор", L"Analyzer öffnen",
				L"Abrir analisador", L"Analyzer openen", L"Otworz analizator", L"Analizoru ac"),
			LL14(L"詳細な周波数解析ができるアナライザ窓を開きます。", L"Open the analyzer window for detailed frequency analysis.", L"Ouvre l'analyseur pour une analyse frequentielle detaillee.", L"Apre l'analizzatore per analisi di frequenza dettagliata.", L"Abre el analizador para un analisis de frecuencia detallado.",
				L"세밀한 주파수 분석용 분석기 창을 엽니다.", L"打开分析器窗口，进行详细频率分析。", L"فتح نافذة المحلل لتحليل التردد التفصيلي.", L"Открыть окно анализатора для детального частотного разбора.", L"Analyzer-Fenster für detaillierte Frequenzanalyse öffnen.",
				L"Abrir a janela do analisador para analise de frequencia detalhada.", L"Open het analyzer-venster voor gedetailleerde frequentieanalyse.", L"Otworz okno analizatora do szczegolowej analizy czestotliwosci.", L"Ayrintili frekans analizi icin analizor penceresini ac."));
		menu.AddSeparator();
		menu.AddCommand(1,
			LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"),
			LL14(L"スペアナ可視化窓を閉じます（Escでも可）。", L"Close this spectrum visualizer (Esc works too).", L"Ferme ce visualiseur (Esc aussi).", L"Chiude questo visualizzatore (anche Esc).", L"Cierra este visualizador (Esc tambien).",
				L"스펙 시각화 창을 닫습니다(Esc도 가능).", L"关闭频谱可视化窗口（Esc 也可）。", L"إغلاق نافذة طيف الطيف (Esc أيضاً).", L"Закрыть окно спектра (Esc тоже).", L"Spektrum-Fenster schließen (auch Esc).",
				L"Fechar este visualizador de espectro (Esc tambem).", L"Sluit dit spectrumvenster (Esc ook).", L"Zamknij to okno spektrum (tez Esc).", L"Spektrum gorsel penceresini kapat (Esc de olur)."));
		const UINT cmd = menu.Track(point, this);
		if (cmd == 1) {
			DestroyWindow();
		}
		else if (cmd == 2) {
			SetWindowPos(topMost ? &wndNoTopMost : &wndTopMost, 0, 0, 0, 0,
				SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}
		else if (cmd == 3) {
			ShowWindow(maximized ? SW_RESTORE : SW_SHOWMAXIMIZED);
		}
		else if (cmd == 10 || cmd == 11 || cmd == 12) {
			savedata.mpSpeanaStyle = (cmd == 10) ? 0 : (cmd == 11) ? 1 : 2;
			MpPersistSavedataQuick();
			if (mp && ::IsWindow(mp->GetSafeHwnd())) {
				if (cmd == 10) mp->PostMessage(WM_COMMAND, ID_MP_SPEANA_BAR);
				else if (cmd == 11) mp->PostMessage(WM_COMMAND, ID_MP_SPEANA_MIRROR);
				else mp->PostMessage(WM_COMMAND, ID_MP_SPEANA_WAVE);
			}
		}
		else if (cmd == 13) {
			if (mp && ::IsWindow(mp->GetSafeHwnd()))
				mp->PostMessage(WM_COMMAND, ID_MP_OPEN_ANALYZER);
		}
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
	MpRemoteStop(); // 先に止めて終了待ちを短くする
	MpDjScratchShutdown();
	CloseMpDjPadIfOpen();
	CloseMpBpmMeasureDlgIfOpen();
	CloseMpAlarmDlgIfOpen();
	CloseMpMirrorDlgIfOpen();
	CloseMpRemoteDlgIfOpen();
	CloseMpSsVizIfOpen();
	MpMidiInShutdown();
	MpMirrorShutdown();
	g_bpmArmed = 0;
	g_bpmHeldPcAudio = 0;
	while (g_pcAudioRetain > 0)
		MpPcAudioRelease();
}

// ---------------------------------------------------------------------------
// プレイリスト表示ソート: ディスク上は常に自然順。表示だけ並べ替える。
// s_natOrd[visual] = 自然順ランク (0..n-1)。Save は inv 経由で自然順書出し。
// ---------------------------------------------------------------------------
static int* s_plNatOrd = NULL;
static int s_plNatN = 0;
static int s_plNatCap = 0;

extern int plcnt;

static int MpCmpNameAsc(const void* a, const void* b)
{
	const playlistdata0* pa = (const playlistdata0*)a;
	const playlistdata0* pb = (const playlistdata0*)b;
	return _tcsicmp(pa->name, pb->name);
}
static int MpCmpNameDesc(const void* a, const void* b) { return -MpCmpNameAsc(a, b); }
static int MpCmpArtAsc(const void* a, const void* b)
{
	const playlistdata0* pa = (const playlistdata0*)a;
	const playlistdata0* pb = (const playlistdata0*)b;
	int c = _tcsicmp(pa->art, pb->art);
	return c ? c : _tcsicmp(pa->name, pb->name);
}
static int MpCmpArtDesc(const void* a, const void* b) { return -MpCmpArtAsc(a, b); }
static int MpCmpAlbAsc(const void* a, const void* b)
{
	const playlistdata0* pa = (const playlistdata0*)a;
	const playlistdata0* pb = (const playlistdata0*)b;
	int c = _tcsicmp(pa->alb, pb->alb);
	return c ? c : _tcsicmp(pa->name, pb->name);
}
static int MpCmpAlbDesc(const void* a, const void* b) { return -MpCmpAlbAsc(a, b); }
static int MpCmpTimeAsc(const void* a, const void* b)
{
	const playlistdata0* pa = (const playlistdata0*)a;
	const playlistdata0* pb = (const playlistdata0*)b;
	if (pa->time != pb->time) return (pa->time < pb->time) ? -1 : 1;
	return _tcsicmp(pa->name, pb->name);
}
static int MpCmpTimeDesc(const void* a, const void* b) { return -MpCmpTimeAsc(a, b); }

static void MpPlaylistNatOrdFree()
{
	if (s_plNatOrd) { free(s_plNatOrd); s_plNatOrd = NULL; }
	s_plNatN = 0;
	s_plNatCap = 0;
}

void MpPlaylistNatOrdClear()
{
	MpPlaylistNatOrdFree();
}

static BOOL MpPlaylistNatOrdGrow(int need)
{
	if (need <= s_plNatCap) return TRUE;
	int cap = s_plNatCap > 0 ? s_plNatCap : 16;
	while (cap < need) cap *= 2;
	int* p = (int*)realloc(s_plNatOrd, (size_t)cap * sizeof(int));
	if (!p) return FALSE;
	s_plNatOrd = p;
	s_plNatCap = cap;
	return TRUE;
}

// スタンプ欠落時は「いまの表示順」を自然順として採用(破損済みPLの救済)
static void MpPlaylistNatOrdEnsure()
{
	if (!pl || !pl->pc || pl->playcnt <= 0) {
		MpPlaylistNatOrdFree();
		return;
	}
	const int n = pl->playcnt;
	if (s_plNatN == n && s_plNatOrd) return;
	if (!MpPlaylistNatOrdGrow(n)) return;
	for (int i = 0; i < n; ++i)
		s_plNatOrd[i] = i;
	s_plNatN = n;
}

void MpPlaylistNatOrdOnLoaded(int n)
{
	if (n <= 0 || !pl || !pl->pc) {
		MpPlaylistNatOrdFree();
		return;
	}
	if (!MpPlaylistNatOrdGrow(n)) return;
	for (int i = 0; i < n; ++i)
		s_plNatOrd[i] = i;
	s_plNatN = n;
}

void MpPlaylistNatOrdNotifyAdded()
{
	if (!pl || pl->playcnt <= 0) return;
	const int n = pl->playcnt;
	if (s_plNatN != n - 1 || !s_plNatOrd) {
		MpPlaylistNatOrdEnsure();
		return;
	}
	if (!MpPlaylistNatOrdGrow(n)) return;
	int mx = -1;
	for (int i = 0; i < n - 1; ++i)
		if (s_plNatOrd[i] > mx) mx = s_plNatOrd[i];
	s_plNatOrd[n - 1] = mx + 1;
	s_plNatN = n;
}

void MpPlaylistNatOrdNotifyInserted(int at, int nIns)
{
	if (!pl || !pl->pc || nIns <= 0) return;
	const int n = pl->playcnt;
	const int oldN = n - nIns;
	if (oldN < 0 || at < 0 || at > oldN) {
		MpPlaylistNatOrdEnsure();
		return;
	}
	if (s_plNatN != oldN || !s_plNatOrd) {
		// 挿入前スタンプが無い → 挿入後の並びを自然順に
		MpPlaylistNatOrdOnLoaded(n);
		return;
	}
	if (!MpPlaylistNatOrdGrow(n)) return;
	for (int i = oldN - 1; i >= at; --i)
		s_plNatOrd[i + nIns] = s_plNatOrd[i];
	int baseNat;
	if (at + nIns < n)
		baseNat = s_plNatOrd[at + nIns];
	else {
		int mx = -1;
		for (int i = 0; i < at; ++i)
			if (s_plNatOrd[i] > mx) mx = s_plNatOrd[i];
		baseNat = mx + 1;
	}
	for (int i = 0; i < at; ++i)
		if (s_plNatOrd[i] >= baseNat) s_plNatOrd[i] += nIns;
	for (int i = at + nIns; i < n; ++i)
		if (s_plNatOrd[i] >= baseNat) s_plNatOrd[i] += nIns;
	for (int i = 0; i < nIns; ++i)
		s_plNatOrd[at + i] = baseNat + i;
	s_plNatN = n;
}

void MpPlaylistNatOrdNotifyRemovedAt(int idx)
{
	if (!s_plNatOrd || s_plNatN <= 0) return;
	if (idx < 0 || idx >= s_plNatN) return;
	const int gone = s_plNatOrd[idx];
	for (int i = idx + 1; i < s_plNatN; ++i)
		s_plNatOrd[i - 1] = s_plNatOrd[i];
	s_plNatN--;
	for (int i = 0; i < s_plNatN; ++i)
		if (s_plNatOrd[i] > gone) s_plNatOrd[i]--;
}

void MpPlaylistNatOrdNotifySwap(int i, int j)
{
	if (!s_plNatOrd || s_plNatN <= 0) return;
	if (i < 0 || j < 0 || i >= s_plNatN || j >= s_plNatN || i == j) return;
	const int t = s_plNatOrd[i];
	s_plNatOrd[i] = s_plNatOrd[j];
	s_plNatOrd[j] = t;
}

// Save 用: 自然順で並べた visual 添え字を outIdx[0..n) に書く。失敗時 FALSE。
BOOL MpPlaylistBuildNaturalIndex(int* outIdx, int n)
{
	if (!outIdx || n <= 0 || !pl || !pl->pc || pl->playcnt != n) return FALSE;
	MpPlaylistNatOrdEnsure();
	if (s_plNatN != n || !s_plNatOrd) return FALSE;
	for (int i = 0; i < n; ++i) outIdx[i] = -1;
	for (int v = 0; v < n; ++v) {
		const int nat = s_plNatOrd[v];
		if (nat < 0 || nat >= n || outIdx[nat] >= 0) {
			// 壊れている → 表示順を自然順扱い
			for (int k = 0; k < n; ++k) outIdx[k] = k;
			return TRUE;
		}
		outIdx[nat] = v;
	}
	return TRUE;
}

int MpPlaylistNaturalIndexOfVisual(int visual)
{
	if (!pl || visual < 0 || visual >= pl->playcnt) return -1;
	MpPlaylistNatOrdEnsure();
	if (!s_plNatOrd || s_plNatN != pl->playcnt) return visual;
	return s_plNatOrd[visual];
}

static void MpPlaylistRemapIndicesByFol(const TCHAR* keepFol)
{
	if (!keepFol || !keepFol[0] || !pl || !pl->pc) return;
	for (int i = 0; i < pl->playcnt; ++i) {
		if (_tcsicmp(pl->pc[i].fol, keepFol) == 0) {
			pl->pnt = i;
			plcnt = i;
			return;
		}
	}
}

void MpPlaylistRestoreNaturalOrder()
{
	if (!pl || !pl->pc || pl->playcnt <= 1) return;
	const int n = pl->playcnt;
	MpPlaylistNatOrdEnsure();
	if (s_plNatN != n || !s_plNatOrd) return;

	TCHAR keep[1024]; keep[0] = 0;
	if (pl->pnt >= 0 && pl->pnt < n)
		_tcsncpy_s(keep, pl->pc[pl->pnt].fol, _TRUNCATE);

	int* order = (int*)malloc((size_t)n * sizeof(int));
	playlistdata0* tmp = (playlistdata0*)malloc((size_t)n * sizeof(playlistdata0));
	if (!order || !tmp) {
		free(order); free(tmp);
		return;
	}
	if (!MpPlaylistBuildNaturalIndex(order, n)) {
		free(order); free(tmp);
		return;
	}
	for (int i = 0; i < n; ++i)
		memcpy(&tmp[i], &pl->pc[order[i]], sizeof(playlistdata0));
	memcpy(pl->pc, tmp, (size_t)n * sizeof(playlistdata0));
	for (int i = 0; i < n; ++i)
		s_plNatOrd[i] = i;
	free(order);
	free(tmp);
	MpPlaylistRemapIndicesByFol(keep);
}

static int (*s_mpSortCmp)(const void*, const void*) = NULL;
static int MpCmpIdxAsc(const void* a, const void* b)
{
	const int ia = *(const int*)a;
	const int ib = *(const int*)b;
	return s_mpSortCmp(&pl->pc[ia], &pl->pc[ib]);
}

void MpPlaylistApplySavedSort()
{
	if (!pl || !pl->pc || pl->playcnt <= 1) return;
	const int key = savedata.mpSortKey;
	if (key < 1 || key > 4) {
		MpPlaylistRestoreNaturalOrder();
		return;
	}
	MpPlaylistNatOrdEnsure();
	const int n = pl->playcnt;
	if (s_plNatN != n || !s_plNatOrd) return;

	TCHAR keep[1024]; keep[0] = 0;
	if (pl->pnt >= 0 && pl->pnt < n)
		_tcsncpy_s(keep, pl->pc[pl->pnt].fol, _TRUNCATE);

	int (*cmp)(const void*, const void*) = NULL;
	if (key == 1) cmp = savedata.mpSortAsc ? MpCmpNameAsc : MpCmpNameDesc;
	else if (key == 2) cmp = savedata.mpSortAsc ? MpCmpArtAsc : MpCmpArtDesc;
	else if (key == 3) cmp = savedata.mpSortAsc ? MpCmpAlbAsc : MpCmpAlbDesc;
	else if (key == 4) cmp = savedata.mpSortAsc ? MpCmpTimeAsc : MpCmpTimeDesc;
	if (!cmp) return;

	int* idx = (int*)malloc((size_t)n * sizeof(int));
	playlistdata0* tmp = (playlistdata0*)malloc((size_t)n * sizeof(playlistdata0));
	int* natTmp = (int*)malloc((size_t)n * sizeof(int));
	if (!idx || !tmp || !natTmp) {
		free(idx); free(tmp); free(natTmp);
		return;
	}
	for (int i = 0; i < n; ++i) idx[i] = i;
	s_mpSortCmp = cmp;
	qsort(idx, (size_t)n, sizeof(int), MpCmpIdxAsc);
	s_mpSortCmp = NULL;
	for (int i = 0; i < n; ++i) {
		memcpy(&tmp[i], &pl->pc[idx[i]], sizeof(playlistdata0));
		natTmp[i] = s_plNatOrd[idx[i]];
	}
	memcpy(pl->pc, tmp, (size_t)n * sizeof(playlistdata0));
	memcpy(s_plNatOrd, natTmp, (size_t)n * sizeof(int));
	free(idx); free(tmp); free(natTmp);
	MpPlaylistRemapIndicesByFol(keep);
}

void MpSortPlaylistByKey(int key)
{
	if (!pl || !pl->pc || pl->playcnt <= 1) return;
	if (key < 1 || key > 4) return;
	MpPlaylistNatOrdEnsure();
	if (savedata.mpSortKey == key)
		savedata.mpSortAsc = savedata.mpSortAsc ? 0 : 1;
	else {
		savedata.mpSortKey = key;
		savedata.mpSortAsc = 1;
	}
	MpPlaylistApplySavedSort();
	// ソート順は savedata のみ。プレイリスト本体は自然順のまま Save される。
}
