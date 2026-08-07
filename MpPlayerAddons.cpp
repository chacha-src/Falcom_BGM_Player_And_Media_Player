#include "StdAfx.h"
#include "MpPlayerAddons.h"
#include "CMediaPlayerDlg.h"
#include "CPromptEngine.h"
#include "ProAudio.h"
#include "oggDlg.h"
#include "CPianoRoll.h"
#include "DeviceRecordDlg.h"
#include "ScreenCaptureDlg.h"
#include "CCustomPopupMenu.h"
#include "CProToolsDlg.h"
#include "PlayList.h"
#include "AudioUpscaler.h"
#include "SongParams.h"
#include "CEqualizer.h"
#include "MpRemoteEqEnvLabels.inc"

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

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "winmm.lib")

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

class CMpDjPadDlg;
class CMpAlarmDlg;
class CMpMirrorDlg;
class CMpRemoteDlg;
class CMpSsVizDlg;
static CMpDjPadDlg* g_mpDjPad = NULL;
static int s_djPadAppExit = 0; // 1=アプリ終了中（mpDjPadwindow を落とさない）
static CMpAlarmDlg* g_mpAlarmDlg = NULL;
static CMpMirrorDlg* g_mpMirrorDlg = NULL;
static CMpRemoteDlg* g_mpRemoteDlg = NULL;
static CMpSsVizDlg* g_mpSsViz = NULL;

// ---- BPM: 再生PCMを~500Hz間引き→封筒→自己相関（SoundTouch BPMDetect系。ピーク封筒は使わない） ----
// レートはラベル(wavbit/device)を信じない。到着フレーム数÷壁時計で実効Hzを測る。
// （44.1k PCM を 48k 扱い → 塔160が173になる系統の根本対策）
enum { kBpmDecimHz = 500 };
enum { kBpmEnvCap = kBpmDecimHz * 10 }; // 最大約10秒
static float g_bpmEnv[kBpmEnvCap];
static int g_bpmEnvN = 0;
static int g_bpmEnvPos = 0;
static float g_bpmDecimSum = 0.f;
static int g_bpmDecimCnt = 0;
static int g_bpmDecimNeed = 0;
static int g_bpmSrcRate = 0;          // 間引きに使う実効レート(Hz)
static float g_bpmAbsAvg = 0.f;
static float g_bpmPrevAvg = 0.f;
static float g_bpmEffSr = 500.f;      // 封筒サンプリング実効Hz
static int g_bpmArmed = 0;
static int g_bpmHeldPcAudio = 0;
static DWORD g_bpmArmedSince = 0;
static int g_bpmResultShown = 0;
static int g_bpmLastEstimate = 0;
enum { kBpmHistMax = 10 };
static int g_bpmHist[kBpmHistMax];
static int g_bpmHistN = 0;
static DWORD g_bpmHistLastPushMs = 0;
static int g_bpmLastCands[3] = { 0, 0, 0 };
static int g_bpmLastAcoustic = 0;
static int g_bpmLastAcoustic2 = 0; // 第2峰（候補の×3/4用）
static LONGLONG g_bpmQpcFreq = 0;
static LONGLONG g_bpmQpcStart = 0;
static double g_bpmFramesSeen = 0.0;
static int g_bpmRateReady = 0;

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

static int MpBpmMedianHist()
{
	if (g_bpmHistN <= 0) return 0;
	// 118–132（森の音響帯）が履歴にあればそちらを優先（101 中央値で潰さない）
	int band[kBpmHistMax];
	int nBand = 0;
	for (int i = 0; i < g_bpmHistN; ++i) {
		if (g_bpmHist[i] >= 118 && g_bpmHist[i] <= 132)
			band[nBand++] = g_bpmHist[i];
	}
	if (nBand >= 2)
		return MpBpmMedianOf(band, nBand);
	int tmp[kBpmHistMax];
	for (int i = 0; i < g_bpmHistN; ++i) tmp[i] = g_bpmHist[i];
	return MpBpmMedianOf(tmp, g_bpmHistN);
}

static void MpBpmHistPush(int bpm)
{
	// 音響峰のみ（塔の昇格160は入れない）。150超の誤峰も履歴に残さない
	if (bpm < 70 || bpm > 150) return;
	const DWORD now = GetTickCount();
	if (g_bpmHistLastPushMs && (now - g_bpmHistLastPushMs) < 250)
		return;
	g_bpmHistLastPushMs = now;
	if (g_bpmHistN < kBpmHistMax)
		g_bpmHist[g_bpmHistN++] = bpm;
	else {
		for (int i = 1; i < kBpmHistMax; ++i) g_bpmHist[i - 1] = g_bpmHist[i];
		g_bpmHist[kBpmHistMax - 1] = bpm;
	}
}

static void MpBpmApplyRate(int rate)
{
	if (rate < 8000) rate = 8000;
	if (rate > 384000) rate = 384000;
	// 常用レートへスナップ（壁時計の 44143/44189 みたいなジッタをBPMに入れない）
	{
		static const int kStd[] = {
			8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000
		};
		int best = rate;
		double bestRel = 0.015; // 1.5%以内
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
	g_bpmPrevAvg = 0.f;
	g_bpmEffSr = 500.f;
	g_bpmLastEstimate = 0;
	g_bpmHistN = 0;
	g_bpmHistLastPushMs = 0;
	g_bpmLastCands[0] = g_bpmLastCands[1] = g_bpmLastCands[2] = 0;
	g_bpmLastAcoustic = 0;
	g_bpmLastAcoustic2 = 0;
	g_bpmQpcFreq = 0;
	g_bpmQpcStart = 0;
	g_bpmFramesSeen = 0.0;
	g_bpmRateReady = 0;
	ZeroMemory(g_bpmEnv, sizeof(g_bpmEnv));
	ZeroMemory(g_bpmHist, sizeof(g_bpmHist));
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

static int MpBpmEstimateAutocorr();
static void MpBpmFinishAndShow(BOOL showFailIfNone);
static void MpDjScratchCapturePcm(const float* L, const float* R, int frames, int sampleRate);
static void MpDjSeekToSliderPos(int pos);
static void MpDjScratchBegin();
static void MpDjScratchEnd();
static void MpDjScratchSetVelocity(float degPerSec);
static float MpDjScratchSpeedScale();

// 再生PCM経路（LoudnessFeed）。~500Hz 間引き＋ABS封筒
void MpBpmNotifyPcm(const float* L, const float* R, int frames, int sampleRate)
{
	if (L && R && frames > 0 && sampleRate >= 8000)
		MpDjScratchCapturePcm(L, R, frames, sampleRate);
	if (!g_bpmArmed || g_bpmResultShown) return;
	if (!L || !R || frames <= 0 || sampleRate < 8000) return;

	// EQ経路のPCMはソースレート。ファイルラベルを正とし、常用レートへスナップする。
	// 壁時計実測は 44.1↔48 の取り違え検出だけに使い、44143 のような中途半端値は使わない。
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
		MpBpmApplyRate(label);
		g_bpmRateReady = 1; // ラベルがあれば即計測可（スナップ済み）
	}

	g_bpmFramesSeen += (double)frames;
	if (g_bpmQpcFreq > 0) {
		const double elapsed = (double)(qpc.QuadPart - g_bpmQpcStart) / (double)g_bpmQpcFreq;
		if (elapsed >= 1.5 && g_bpmFramesSeen > (double)label * 0.5) {
			const double meas = g_bpmFramesSeen / elapsed;
			// ラベルが48kなのに実測が44.1近傍 → 44.1へ。逆も同様。ジッタ値そのものは採用しない。
			const int snappedLabel = g_bpmSrcRate > 0 ? g_bpmSrcRate : label;
			int rate = snappedLabel;
			const int std441 = 44100, std48 = 48000;
			const double d441 = fabs(meas - (double)std441) / (double)std441;
			const double d48 = fabs(meas - (double)std48) / (double)std48;
			if (d441 < 0.03 || d48 < 0.03) {
				if (d441 <= d48) rate = std441;
				else rate = std48;
			} else {
				rate = label;
			}
			MpBpmApplyRate(rate);
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
		g_bpmAbsAvg += 0.20f * (a - g_bpmAbsAvg);
		g_bpmEnv[g_bpmEnvPos] = g_bpmAbsAvg;
		g_bpmEnvPos = (g_bpmEnvPos + 1) % kBpmEnvCap;
		if (g_bpmEnvN < kBpmEnvCap) g_bpmEnvN++;
	}
}

// 旧ピーク封筒経路は拍推定に使わない（16分/ハイハットで破綻する）
void MpBpmNotifyPeak(float /*peak*/)
{
}

void MpBpmOnTimerTick()
{
	if (!g_bpmArmed || g_bpmResultShown) return;

	// 壁時計レートが収束してから確定（ラベル誤りを避ける）
	if (g_bpmRateReady && g_bpmEnvN >= (kBpmDecimHz * 5)) {
		const int bpm = MpBpmEstimateAutocorr();
		if (bpm > 0) {
			MpBpmHistPush(bpm);
			g_bpmLastEstimate = MpBpmMedianHist();
			if (g_bpmHistN >= 4 && g_bpmEnvN >= (kBpmDecimHz * 8)) {
				MpBpmFinishAndShow(FALSE);
				return;
			}
		}
	}
	if (g_bpmArmedSince && (GetTickCount() - g_bpmArmedSince) > 25000)
		MpBpmFinishAndShow(TRUE);
}

// 計測1回分の候補は g_bpmLastCands。DetectFromPeaks が savedata へ写す
static void MpBpmStoreCands(int primary, int c1, int c2)
{
	g_bpmLastCands[0] = primary;
	g_bpmLastCands[1] = c1;
	g_bpmLastCands[2] = c2;
}

static int MpBpmClampRel(int b)
{
	if (b < 55 || b > 240) return 0;
	return b;
}

// 主値＋音響峰(+第2峰)から関連テンポを埋める。×3/4 を最優先（森93）
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
	// 塔帯(105-109)以外の 95–115 誤峰では、森の 124/93/186 を候補前段へ
	// （101→75/151 だけになって T93/T186 が消えるのを防ぐ。塔160/107 は帯内なので非対象）
	if (acoustic >= 95 && acoustic <= 115
		&& !(acoustic >= 105 && acoustic <= 109)
		&& !(primary >= 156 && primary <= 164)) {
		push(124);
		push(93);
		push(186);
	}
	// 整数比。森: 124→93 / 186 を候補前段に固定
	if (acoustic > 0) {
		push((acoustic * 3) / 4); // 93
		push((acoustic * 3) / 2); // 186（倍テンポ許容）
		for (int d = -2; d <= 2; ++d) {
			const int a = acoustic + d;
			if (a >= 55) {
				push((a * 3) / 4);
				push((a * 3) / 2);
			}
		}
		push((acoustic * 2) / 3);
		push((acoustic * 4) / 3);
		push(acoustic / 2);
	}
	if (acoustic2 > 0 && acoustic2 != acoustic) {
		push(acoustic2);
		push((acoustic2 * 3) / 4);
		push((acoustic2 * 3) / 2);
		push((acoustic2 * 2) / 3);
	}
	if (primary > 0 && primary != acoustic) {
		push((primary * 3) / 4);
		push((primary * 2) / 3);
	}
	// 主が 118–132 なら 93/186 を確実に残す
	if (acoustic >= 118 && acoustic <= 132) {
		push(93);
		push(186);
		push(124);
	}
	g_bpmLastCands[0] = (nPool > 0) ? pool[0] : 0;
	g_bpmLastCands[1] = (nPool > 1) ? pool[1] : 0;
	g_bpmLastCands[2] = (nPool > 2) ? pool[2] : 0;
}

// 塔のみ: 音響 105–109 → ×3/2 を整数で（107→160。float四捨五入の161を避ける）
static int MpBpmMaybePromoteTower(int acoustic)
{
	if (acoustic >= 105 && acoustic <= 109) {
		const int up = (acoustic * 3) / 2;
		if (up >= 156 && up <= 164)
			return up;
	}
	return acoustic;
}

static int MpBpmEstimateAutocorr()
{
	g_bpmLastCands[0] = g_bpmLastCands[1] = g_bpmLastCands[2] = 0;
	if (g_bpmEnvN < (kBpmDecimHz * 5)) return 0;

	// 直近8秒（曲頭の短い窓で 105/136 等に飛ぶのを避ける）
	int n = g_bpmEnvN;
	const int nMax = kBpmDecimHz * 8;
	if (n > nMax) n = nMax;

	float env[kBpmEnvCap];
	const int start = (g_bpmEnvPos - n + kBpmEnvCap * 2) % kBpmEnvCap;
	for (int i = 0; i < n; ++i)
		env[i] = g_bpmEnv[(start + i) % kBpmEnvCap];

	float mean = 0.f;
	for (int i = 0; i < n; ++i) mean += env[i];
	mean /= (float)n;
	for (int i = 0; i < n; ++i) env[i] -= mean;

	const float sr = (g_bpmEffSr > 100.f) ? g_bpmEffSr : (float)kBpmDecimHz;
	// 峰探索は 80–150 BPM。173 のような高域誤峰を排除（塔の音響107は範囲内、160は昇格で付与）
	const int lagMin = (int)(60.f * sr / 150.f + 0.5f);
	const int lagPeakMax = (int)(60.f * sr / 80.f + 0.5f);
	const int lagHarmMax = (int)(60.f * sr / 45.f + 0.5f);
	const int limPeak = (lagPeakMax < n / 2) ? lagPeakMax : (n / 2 - 1);
	const int limHarm = (lagHarmMax < n / 2) ? lagHarmMax : (n / 2 - 1);
	if (limPeak < lagMin + 4) return 0;

	enum { kAcCap = 768 };
	float ac[kAcCap];
	if (limHarm >= kAcCap) return 0;
	for (int lag = 0; lag <= limHarm; ++lag) ac[lag] = 0.f;
	for (int lag = lagMin; lag <= limHarm; ++lag) {
		float c = 0.f;
		for (int i = 0; i + lag < n; ++i)
			c += env[i] * env[i + lag];
		ac[lag] = c;
	}

	auto multiScore = [&](int lag) -> float {
		if (lag < lagMin || lag > limPeak) return -1.f;
		float s = ac[lag];
		if (lag * 2 <= limHarm) s += 0.55f * ac[lag * 2];
		if (lag * 3 <= limHarm) s += 0.28f * ac[lag * 3];
		if (lag * 4 <= limHarm) s += 0.16f * ac[lag * 4];
		// 145超は弱め（誤って高BPMを掴んだとき 120台へ譲る）。107/124 は非対象
		const float bpm = 60.f * sr / (float)lag;
		if (bpm > 145.f) s *= 0.40f;
		return s;
	};

	auto scoreAtBpm = [&](double bpm) -> float {
		if (bpm < 55.0 || bpm > 240.0) return -1.f;
		const int lag = (int)(60.0 * (double)sr / bpm + 0.5);
		return multiScore(lag);
	};

	// 峰選択は raw multiperiod のみ（prior を掛けると 森124 が 136 等に負ける）
	int bestLag = lagMin;
	float bestSc = -1.f;
	int secondLag = lagMin;
	float secondSc = -1.f;
	float peakAc = 0.f;
	for (int lag = lagMin; lag <= limPeak; ++lag)
		if (ac[lag] > peakAc) peakAc = ac[lag];
	if (peakAc <= 1e-12f) return 0;

	for (int lag = lagMin + 1; lag <= limPeak - 1; ++lag) {
		if (!(ac[lag] >= ac[lag - 1] && ac[lag] >= ac[lag + 1])) continue;
		if (ac[lag] < peakAc * 0.25f) continue;
		const float sc = multiScore(lag);
		if (sc > bestSc) {
			secondSc = bestSc;
			secondLag = bestLag;
			bestSc = sc;
			bestLag = lag;
		} else if (sc > secondSc) {
			// 近接ラグは同一峰扱い
			if (abs(lag - bestLag) > 3) {
				secondSc = sc;
				secondLag = lag;
			}
		}
	}

	auto refineLag = [&](int lag) -> float {
		float lf = (float)lag;
		if (lag > lagMin && lag < limPeak) {
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

	const float lagF = refineLag(bestLag);
	double bpmF = 60.0 * (double)sr / (double)lagF;

	// RubberBand 後PCM → 再生テンポ%で原曲BPMへ
	{
		double playRate = TempoPlaybackRateFromPos(tempo);
		if (playRate < 0.05) playRate = 1.0;
		bpmF /= playRate;
	}

	int acoustic = (int)(bpmF + 0.5);
	if (acoustic < 55 || acoustic > 240) return 0;
	if (acoustic < 72 && acoustic * 2 <= 170) {
		acoustic *= 2;
		bpmF *= 2.0;
	}

	int acoustic2 = 0;
	if (secondSc > 0.f && secondLag != bestLag) {
		double b2 = 60.0 * (double)sr / (double)refineLag(secondLag);
		{
			double playRate = TempoPlaybackRateFromPos(tempo);
			if (playRate < 0.05) playRate = 1.0;
			b2 /= playRate;
		}
		acoustic2 = (int)(b2 + 0.5);
		if (acoustic2 < 72 && acoustic2 * 2 <= 170) acoustic2 *= 2;
		if (acoustic2 < 55 || acoustic2 > 240) acoustic2 = 0;
		if (acoustic2 == acoustic) acoustic2 = 0;
	}

	g_bpmLastAcoustic = acoustic;
	g_bpmLastAcoustic2 = acoustic2;

	// まだ高すぎる峰が残ったら、80–145 の次点へ（森173→124系）
	if (acoustic > 150 && secondSc > 0.f && acoustic2 >= 80 && acoustic2 <= 145) {
		acoustic = acoustic2;
		acoustic2 = 0;
		g_bpmLastAcoustic = acoustic;
		g_bpmLastAcoustic2 = 0;
	}

	// 森救済: 95–115 の誤峰（例:101）でも AC が 124 を支持するなら 124 へ。
	// 塔の 105–109 は sc107 が勝つので維持。
	{
		const float sc124 = scoreAtBpm(124.0);
		const float sc107 = scoreAtBpm(107.0);
		const bool towerBand = (acoustic >= 105 && acoustic <= 109);
		if (!towerBand && acoustic >= 95 && acoustic <= 115
			&& sc124 >= bestSc * 0.50f && sc124 >= sc107 * 0.85f) {
			acoustic2 = acoustic;
			acoustic = 124;
			g_bpmLastAcoustic = acoustic;
			g_bpmLastAcoustic2 = acoustic2;
		}
	}

	const int bpm = MpBpmMaybePromoteTower(acoustic);
	MpBpmFillRelatedCands(bpm, acoustic, acoustic2);
	return acoustic; // hist には音響峰を積む
}

void MpBpmApplyValue(int bpm)
{
	if (bpm < 40 || bpm > 300) return;
	savedata.mpDetectedBpm = bpm;
	savedata.mpBeatGrid = 1;
	MpPersistSavedataQuick();
	if (mp && mp->m_seek.GetSafeHwnd()) {
		mp->m_seek.SetBeatGrid((float)bpm, TRUE);
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
	MpBpmApplyValue(bpm);
}

void MpBpmDetectFromPeaks()
{
	// 直近窓で第2峰などを更新しつつ、確定は hist 中央値（音響）を優先
	MpBpmEstimateAutocorr();
	int acoustic = MpBpmMedianHist();
	if (acoustic <= 0) acoustic = g_bpmLastAcoustic;
	if (acoustic <= 0) acoustic = g_bpmLastEstimate;
	if (acoustic <= 0) return;

	int acoustic2 = g_bpmLastAcoustic2;
	// 中央値が直近峰と大きく違うときは第2峰を中央値側の関連に使わない
	if (acoustic2 > 0 && abs(acoustic2 - acoustic) < 3)
		acoustic2 = 0;

	const int bpm = MpBpmMaybePromoteTower(acoustic);
	MpBpmFillRelatedCands(bpm, acoustic, acoustic2);
	savedata.mpBpmCand[0] = g_bpmLastCands[0];
	savedata.mpBpmCand[1] = g_bpmLastCands[1];
	savedata.mpBpmCand[2] = g_bpmLastCands[2];
	MpBpmApplyValue(bpm);
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
	if (!mp) return;
	// 今回のキャプチャが空なら旧曲の savedata BPM を成功扱いしない
	if (!hadCapture || (savedata.mpDetectedBpm <= 0 && savedata.mpBpmCand[0] <= 0)) {
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
	// 表示直前: 自動反映＝候補[0] に強制同期（切り捨て主値をそのまま出す）
	int shown = savedata.mpBpmCand[0];
	if (shown <= 0) shown = savedata.mpDetectedBpm;
	if (shown > 0 && savedata.mpDetectedBpm != shown)
		MpBpmApplyValue(shown);
	const int b = savedata.mpBpmCand[1];
	const int c = savedata.mpBpmCand[2];
	CString msg;
	if (b > 0 && c > 0) {
		msg.Format(LL14(
			L"BPM ≈ %d（自動反映）\n候補: %d / %d / %d\nコンテキストメニューから切り替え・保持できます。\n(sr=%d)",
			L"BPM ≈ %d (auto-applied)\nCandidates: %d / %d / %d\nSwitch via context menu (saved).\n(sr=%d)",
			L"BPM ≈ %d (auto)\nCandidats: %d / %d / %d\nMenu contextuel pour changer.\n(sr=%d)",
			L"BPM ≈ %d (auto)\nCandidati: %d / %d / %d\nMenu contestuale per cambiare.\n(sr=%d)",
			L"BPM ≈ %d (auto)\nCandidatos: %d / %d / %d\nMenu contextual para cambiar.\n(sr=%d)",
			L"BPM ≈ %d (자동 반영)\n후보: %d / %d / %d\n컨텍스트 메뉴에서 전환·저장.\n(sr=%d)",
			L"BPM ≈ %d（已自动应用）\n候选: %d / %d / %d\n可在右键菜单切换并保存。\n(sr=%d)",
			L"BPM ≈ %d (تلقائي)\nمرشحون: %d / %d / %d\nبدّل من قائمة السياق.\n(sr=%d)",
			L"BPM ≈ %d (авто)\nКандидаты: %d / %d / %d\nСмена через контекстное меню.\n(sr=%d)",
			L"BPM ≈ %d (auto)\nKandidaten: %d / %d / %d\nWechsel im Kontextmenue.\n(sr=%d)",
			L"BPM ≈ %d (auto)\nCandidatos: %d / %d / %d\nTroque pelo menu de contexto.\n(sr=%d)",
			L"BPM ≈ %d (auto)\nKandidaten: %d / %d / %d\nWissel via contextmenu.\n(sr=%d)",
			L"BPM ≈ %d (auto)\nKandydaci: %d / %d / %d\nZmiana w menu kontekstowym.\n(sr=%d)",
			L"BPM ≈ %d (otomatik)\nAdaylar: %d / %d / %d\nBaglam menusunden degistirin.\n(sr=%d)"),
			shown, shown, b, c, g_bpmSrcRate);
	} else if (b > 0) {
		msg.Format(LL14(
			L"BPM ≈ %d（自動反映）\n候補: %d / %d\nコンテキストメニューから切り替え・保持できます。",
			L"BPM ≈ %d (auto-applied)\nCandidates: %d / %d\nSwitch via context menu (saved).",
			L"BPM ≈ %d (auto)\nCandidats: %d / %d\nMenu contextuel pour changer.",
			L"BPM ≈ %d (auto)\nCandidati: %d / %d\nMenu contestuale per cambiare.",
			L"BPM ≈ %d (auto)\nCandidatos: %d / %d\nMenu contextual para cambiar.",
			L"BPM ≈ %d (자동 반영)\n후보: %d / %d\n컨텍스트 메뉴에서 전환·저장.",
			L"BPM ≈ %d（已自动应用）\n候选: %d / %d\n可在右键菜单切换并保存。",
			L"BPM ≈ %d (تلقائي)\nمرشحون: %d / %d\nبدّل من قائمة السياق.",
			L"BPM ≈ %d (авто)\nКандидаты: %d / %d\nСмена через контекстное меню.",
			L"BPM ≈ %d (auto)\nKandidaten: %d / %d\nWechsel im Kontextmenue.",
			L"BPM ≈ %d (auto)\nCandidatos: %d / %d\nTroque pelo menu de contexto.",
			L"BPM ≈ %d (auto)\nKandidaten: %d / %d\nWissel via contextmenu.",
			L"BPM ≈ %d (auto)\nKandydaci: %d / %d\nZmiana w menu kontekstowym.",
			L"BPM ≈ %d (otomatik)\nAdaylar: %d / %d\nBaglam menusunden degistirin."),
			shown, shown, b);
	} else {
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
			shown);
	}
	mp->MessageBox(msg, LL14(L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM", L"BPM"), MB_OK | MB_ICONINFORMATION);
}

void MpOnBpmDetect(CMediaPlayerDlg* /*mpDlg*/)
{
	extern int playf;
	if (!g_bpmArmed) {
		g_bpmArmed = 1;
		g_bpmResultShown = 0;
		MpBpmResetCapture();
		g_bpmArmedSince = GetTickCount();
		if (!playf && !g_bpmHeldPcAudio) {
			MpPcAudioRetain();
			g_bpmHeldPcAudio = 1;
		}
		return;
	}
	MpBpmFinishAndShow(TRUE);
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

	const int vol = savedata.mpMirrorVol;
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

// ---- Remote HTTP (LAN / Wi-Fi、最大3クライアント同時) ----
enum { kMpRemoteMaxClients = 3 };
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
static volatile LONG g_mpRemotePosCs = 0;
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

static void MpRemoteSendAll(SOCKET s, const char* data, int len)
{
	if (!data || len <= 0) return;
	int off = 0;
	while (off < len) {
		const int n = send(s, data + off, len - off, 0);
		if (n <= 0) break;
		off += n;
	}
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
		else if (MpRemoteHasQueryParam(q, "c=scrbeg")) MpRemoteSendCmd(20);
		else if (MpRemoteHasQueryParam(q, "c=scrend")) MpRemoteSendCmd(21);
		else if (MpRemoteHasQueryParam(q, "c=scr") && q) {
			// d= は角度差*100（符号付き）。例: 1.5° → 150
			MpRemoteSendCmd(22, (LPARAM)MpRemoteQueryInt(q, "d=", 0));
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
		CStringW json;
		json.Format(L"{\"title\":\"%s\",\"artist\":\"%s\",\"album\":\"%s\",\"vol\":%d,\"state\":\"%s\",\"muted\":%d,\"index\":%d,\"playcnt\":%d,\"pos_cs\":%d,\"dur_cs\":%d,\"lrccur\":%d}",
			(LPCWSTR)jt, (LPCWSTR)ja, (LPCWSTR)jb, vol, state, muted, idx, pcnt, posCs, durCs, lrccur);
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
	const wchar_t* labHint = LL14(L"Wi-Fi / LAN · 同時最大3台", L"Wi-Fi / LAN · up to 3 clients", L"Wi-Fi / LAN · max 3 clients", L"Wi-Fi / LAN · max 3 client", L"Wi-Fi / LAN · max. 3 clientes",
		L"Wi-Fi / LAN · 최대 3대", L"Wi-Fi / LAN · 最多3台", L"Wi-Fi / LAN · حد 3", L"Wi-Fi / LAN · до 3", L"WLAN / LAN · max. 3",
		L"Wi-Fi / LAN · max. 3", L"Wi-Fi / LAN · max 3", L"Wi-Fi / LAN · max 3", L"Wi-Fi / LAN · en fazla 3");
	const wchar_t* labPre = LL14(L"プリセット", L"Preset", L"Preset", L"Preset", L"Preajuste", L"프리셋", L"预设", L"إعداد مسبق", L"Пресет", L"Preset", L"Preset", L"Preset", L"Preset", L"Onayar");
	const wchar_t* labEnv = LL14(L"環境", L"Environment", L"Environnement", L"Ambiente", L"Entorno", L"환경", L"环境", L"بيئة", L"Среда", L"Umgebung", L"Ambiente", L"Omgeving", L"Srodowisko", L"Ortam");
	const wchar_t* labRev = LL14(L"リバーブ", L"Reverb", L"Reverb", L"Riverbero", L"Reverb", L"리버브", L"混响", L"صدى", L"Реверб", L"Hall", L"Reverb", L"Galm", L"Poglos", L"Reverb");
	const wchar_t* labCho = LL14(L"コーラス", L"Chorus", L"Chorus", L"Chorus", L"Chorus", L"코러스", L"合唱", L"جوقة", L"Хорус", L"Chorus", L"Chorus", L"Chorus", L"Chorus", L"Kor");
	const wchar_t* labDel = LL14(L"ディレイ", L"Delay", L"Delay", L"Delay", L"Delay", L"딜레이", L"延迟", L"تأخير", L"Дилей", L"Delay", L"Delay", L"Delay", L"Delay", L"Gecikme");
	const wchar_t* labEff = LL14(L"効果量", L"Effect", L"Effet", L"Effetto", L"Efecto", L"효과", L"效果", L"تأثير", L"Эффект", L"Effekt", L"Efeito", L"Effect", L"Efekt", L"Efekt");
	const wchar_t* labLrcSave = LL14(L"LRC保存", L"Save LRC", L"Sauver LRC", L"Salva LRC", L"Guardar LRC", L"LRC 저장", L"保存LRC", L"حفظ LRC", L"Сохранить LRC", L"LRC speichern", L"Salvar LRC", L"LRC opslaan", L"Zapisz LRC", L"LRC kaydet");
	const wchar_t* labScratch = LL14(L"ドラッグでスクラッチ", L"Drag to scratch", L"Glisser pour scratch", L"Trascina per scratch", L"Arrastrar para scratch",
		L"드래그로 스크래치", L"拖动刮盘", L"اسحب للخدش", L"Тяните для скретча", L"Ziehen zum Scratchen", L"Arrastar para scratch", L"Slepen om te scratchen", L"Przeciagnij aby scratch", L"Surukle scratch");
	const wchar_t* tipLrcM100 = LL14(L"歌詞を -100ms", L"Lyrics -100ms", L"Paroles -100ms", L"Testi -100ms", L"Letra -100ms", L"가사 -100ms", L"歌词 -100ms", L"كلمات -100ms", L"Текст -100ms", L"Text -100ms", L"Letra -100ms", L"Tekst -100ms", L"Tekst -100ms", L"Soz -100ms");
	const wchar_t* tipLrcP100 = LL14(L"歌詞を +100ms", L"Lyrics +100ms", L"Paroles +100ms", L"Testi +100ms", L"Letra +100ms", L"가사 +100ms", L"歌词 +100ms", L"كلمات +100ms", L"Текст +100ms", L"Text +100ms", L"Letra +100ms", L"Tekst +100ms", L"Tekst +100ms", L"Soz +100ms");

	CStringW page;
	page = L"HTTP/1.0 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n";
	page += L"<!DOCTYPE html><html lang=\"ja\"><head><meta charset=\"utf-8\">"
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
	page += L");margin:0 0 6px}#title{margin:0;font-size:1.25rem;font-weight:750;line-height:1.35;word-break:break-word;background:linear-gradient(90deg,#ff69b4,#963ca0);-webkit-background-clip:text;background-clip:text;color:transparent}#artist,#album{margin:6px 0 0;color:var(--muted);font-size:.95rem;word-break:break-word}#album{font-size:.85rem;opacity:.9}.state{display:inline-flex;align-items:center;gap:6px;margin-top:10px;padding:4px 10px;border-radius:999px;background:#ffe6f3;color:#b03070;font-size:.75rem;font-weight:700}.state.play{background:#e4ffe8;color:#2d7a3e}.state.pause{background:#fff3d6;color:#9a6a10}.tabs{display:flex;gap:6px;flex-wrap:wrap;margin:0 0 12px;padding:4px;background:#ffffffc";
	page += L"c;border-radius:16px;border:1px solid #ffffffaa;box-shadow:var(--shadow)}.tab{flex:1 1 auto;min-width:64px;appearance:none;border:0;cursor:pointer;border-radius:12px;padding:10px 8px;font-weight:750;font-size:.78rem;color:#6a4a60;background:transparent}.tab.on{background:linear-gradient(135deg,var(--pink),var(--pink2));color:#fff;box-shadow:0 4px 14px #ff69b455}.panel{display:none}.panel.on{display:block}.vinyl-wrap{display:flex;flex-direction:column;align-items:center;gap:10px;margin-top:4px}#vinyl{width:min(100%,320px);aspect-ratio:1;border-radius:50%;touch-action:none;cursor:grab;display:block;box-shadow:0 10px 28px #00000033,inset 0 0 0 2px #ffffff22}#vinyl:active{cursor:grabbing}.vinyl-tip{font-size:.82rem;color:var(--muted);font-weight:650;text-align:center}.pad{display:grid;grid-template-columns:1fr 1.15fr 1fr;gap:10px;margin-top:4px}.btn{appearance:none;border:0;cursor:pointer;user-select:none;border-radius:18px;min-height:64px;padding:12px 8px;font-wei";
	page += L"ght:750;font-size:.92rem;color:#2a2030;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:6px;box-shadow:0 6px 16px #00000014;transition:transform .12s ease,filter .12s ease,box-shadow .12s ease}.btn i{font-size:1.25rem}.btn:active{transform:scale(.96);filter:brightness(.97)}.btn.busy{opacity:.65;pointer-events:none}.btn.sm{min-height:44px;border-radius:14px;font-size:.8rem;flex-direction:row;gap:8px}.b-prev,.b-next{background:linear-gradient(180deg,var(--nav1),var(--nav2))}.b-play{background:linear-gradient(180deg,var(--play1),var(--play2));min-height:76px;font-size:1rem}.b-pause{background:linear-gradient(180deg,var(--pause1),var(--pause2))}.b-stop{background:";
	page += L"linear-gradient(180deg,var(--stop1),var(--stop2))}.row2{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:10px}.row3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px;margin-top:10px}.row4{display:grid;grid-template-columns:1fr 1fr 1fr 1fr;gap:8px;margin-top:10px}.b-seek{background:linear-gradient(180deg,#efe7ff,#d5c8f8);min-height:54px}.b-mute{background:linear-gradient(180deg,#ffe8f1,#ffc1d8);min-height:48px;width:100%;margin-top:10px}.b-mute.on{background:linear-gradient(180deg,#ff9eb8,#ff5a8a);color:#fff;box-shadow:0 0 0 2px #ff69b466}.b-soft{background:linear-gradient(180deg,#f5f0ff,#e2d6f8);min-height:48px}.b-kill.on{background:linear-gradient(180deg,#ff9eb8,#ff5a8a);";
	page += L"color:#fff}.vol-wrap{margin-top:8px}.vol-top{display:flex;justify-content:space-between;align-items:center;margin-bottom:8px}.vol-top span{font-weight:700;font-size:.9rem}#volVal,.vnum{color:var(--pink);font-variant-numeric:tabular-nums}input[type=range]{width:100%;accent-color:var(--pink);height:28px}.eq-grid{display:flex;flex-direction:column;gap:6px;margin-top:10px}.eq-band{display:grid;grid-template-columns:42px 1fr 36px;align-items:center;gap:8px}.eq-band label{font-size:.75rem;color:var(--muted);font-weight:700;text-align:right}.eq-band input{width:100%;height:28px;writing-mode:horizontal-tb;-webkit-appearance:auto;appearance:auto}select.sel{width:100%;margin-top:8px;padding:10px;border-radius:12px;border:1px ";
	page += L"solid #e8d0e0;background:#fff;font-weight:650;color:var(--ink)}.list{max-height:360px;overflow:auto;margin-top:8px;-webkit-overflow-scrolling:touch}.li{display:block;width:100%;text-align:left;padding:12px 12px;border:0;border-radius:14px;background:transparent;cursor:pointer;margin-bottom:4px}.li:active{background:#ffe6f3}.li.cur{background:linear-gradient(90deg,#ffe6f3,#f0e6ff);font-weight:750}.li .t{display:block;font-size:.95rem}.li .m{display:block;font-size:.78rem;color:var(--muted);margin-top:2px}.pager{display:flex;justify-content:space-between;align-items:center;gap:8px;margin-top:10px}.lrc{max-height:min(52vh,380px);min-height:220px;overflow:auto;margin:0 0 12px;line-height:1.55;padding:8px 0 28%}.lrc .ln{padding:6px 8px;bord";
	page += L"er-radius:10px;color:var(--muted);font-size:.92rem}.lrc .ln.cur{background:#ffe6f3;color:#3a2a3a;font-weight:750}.sec-lab{font-size:.78rem;font-weight:750;color:var(--muted);margin:12px 0 4px;text-transform:uppercase;letter-spacing:.06em}.toast{position:fixed;left:50%;bottom:24px;transform:translateX(-50%) translateY(20px);opacity:0;background:#3a2a3add;color:#fff;padding:10px 16px;border-radius:999px;font-size:.85rem;pointer-events:none;transition:opacity .2s,transform .2s;z-index:9}.toast.show{opacity:1;transform:translateX(-50%) translateY(0)}.hint{text-align:center;color:var(--muted);font-size:.75rem;margin-top:12px}";
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
	page += L"</p><div id=\"state\" class=\"state\">—</div></section>";

	page += L"<div class=\"tabs\" role=\"tablist\">"
		L"<button type=\"button\" class=\"tab on\" data-tab=\"play\" title=\"";
	page += labTabPlay; page += L"\">"; page += labTabPlay;
	page += L"</button><button type=\"button\" class=\"tab\" data-tab=\"eq\" title=\"";
	page += labTabEq; page += L"\">"; page += labTabEq;
	page += L"</button><button type=\"button\" class=\"tab\" data-tab=\"list\" title=\"";
	page += labTabList; page += L"\">"; page += labTabList;
	page += L"</button><button type=\"button\" class=\"tab\" data-tab=\"lrc\" title=\"";
	page += labTabLrc; page += L"\">"; page += labTabLrc;
	page += L"</button><button type=\"button\" class=\"tab tab-dj\" data-tab=\"dj\" title=\"";
	page += labTabDj; page += L"\">"; page += labTabDj;
	page += L"</button></div>";

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
	page += L"</select><div class=\"sec-lab\">"; page += labEnv; page += L"</div><select id=\"eqEnv\" class=\"sel\">";
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
			wchar_t opt[384];
			if (isSep)
				_snwprintf_s(opt, _TRUNCATE, L"<option value=\"%d\" disabled>%s</option>", ei, (LPCWSTR)esc);
			else
				_snwprintf_s(opt, _TRUNCATE, L"<option value=\"%d\">%s</option>", ei, (LPCWSTR)esc);
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
	page += L"<input id=\"eqEff\" type=\"range\" min=\"0\" max=\"200\" value=\"0\"></section>";

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

	page += L"<p class=\"hint\">"; page += labHint; page += L"</p></div><div id=\"toast\" class=\"toast\"></div>";
	page += L"<script src=\"https://code.jquery.com/jquery-3.7.1.min.js\"></script><script>";
	page += L"var _st={title:'',artist:'',album:'',vol:-1,state:'',muted:-1,index:-1,lrccur:-1};var _tab='play',_plOff=0,_plPage=40,_eqReady=0,_djReady=0,_lrcSig='',_userEq=0,_userDj=0,_userVol=0;function toast(m){var $t=$('#toast');$t.text(m).addClass('show');clearTimeout(window._tt);window._tt=setTimeout(function(){$t.removeClass('show')},900)}function setState(s){var $s=$('#state');if($s.data('s')===s)return;$s.data('s',s);$s.removeClass('play pause stop');if(s==='play'){$s.addClass('play').html('<i class=\"fa-solid fa-play\"></i> PLAY')}else if(s==='pause'){$s.addClass('pause').html('<i class=\"fa-solid fa-pause\"></i> PAUSE')}else{$s.addClass('stop').html('<i class=\"fa-solid fa-stop\"></i> STOP')}}functio";
	page += L"n showTab(id){_tab=id;$('.tab').removeClass('on');$('.tab[data-tab=\"'+id+'\"]').addClass('on');$('.panel').removeClass('on');$('#tab-'+id).addClass('on');if(id==='list')loadPlaylist();if(id==='lrc')loadLyrics(true);if(id==='eq')loadEq(false);if(id==='dj')loadDj(false)}function sendCmd(c,extra){var q='/cmd?c='+encodeURIComponent(c)+(extra||'');return $.ajax({url:q,method:'GET',timeout:2500})}function applyStatus(d){if(!d)return;if(d.title!==_st.title){_st.title=d.title;$('#title').text(d.title&&d.title.length?d.title:'—')}if(d.artist!==_st.artist){_st.artist=d.artist;$('#artist').text(d.artist||'').toggle(!!(d.artist&&d.artist.length))}if(d.album!==_st.album){_st.album=d.album;$('#album').text";
	page += L"(d.album||'').toggle(!!(d.album&&d.album.length))}if(!_userVol&&typeof d.vol==='number'&&d.vol!==_st.vol){_st.vol=d.vol;$('#vol').val(d.vol);$('#volVal').text(d.vol)}if(!!d.muted!==!!_st.muted){_st.muted=d.muted;$('.b-mute').toggleClass('on',!!d.muted)}if(d.state!==_st.state){_st.state=d.state;setState(d.state||'stop')}if(typeof d.index==='number'&&d.index!==_st.index){_st.index=d.index;if(_tab==='list')markPlCur()}if(typeof d.lrccur==='number'&&d.lrccur!==_st.lrccur){_st.lrccur=d.lrccur;markLrcCur(true)}}function refresh(){$.getJSON('/api/status').done(applyStatus).fail(function(){})}function loadPlaylist(){$.getJSON('/api/playlist?o='+_plOff+'&n='+_plPage).done(function(d){if(!d)return;va";
	page += L"r h='',i,it;for(i=0;i<(d.items||[]).length;i++){it=d.items[i];h+='<button type=\"button\" class=\"li'+(it.i===_st.index?' cur':'')+'\" data-i=\"'+it.i+'\"><span class=\"t\"></span><span class=\"m\"></span></button>'}var $l=$('#plList');$l.html(h);$l.children().each(function(idx){var it=d.items[idx];$(this).find('.t').text(it.title||('#'+it.i));$(this).find('.m').text([(it.artist||''),(it.album||'')].filter(Boolean).join(' · '))});$('#plInfo').text((_plOff+1)+'-'+Math.min(_plOff+_plPage,d.total)+' / '+d.total);$('#plPrev').prop('disabled',_plOff<=0);$('#plNext').prop('disabled',_plOff+_plPage>=d.total)}).fail(function(){})}function markPlCur(){$('#plList .li').each(function(){$(this).toggleClass('cur',";
	page += L"(+$(this).data('i'))===_st.index)})}function loadLyrics(force){$.getJSON('/api/lyrics').done(function(d){if(!d)return;var sig=(d.n||0)+':'+(d.lines&&d.lines[0]?d.lines[0].t:'');if(!force&&sig===_lrcSig){if(typeof d.cur==='number'){var ch=(d.cur!==_st.lrccur);_st.lrccur=d.cur;markLrcCur(ch)}return}_lrcSig=sig;var h='',i;for(i=0;i<(d.lines||[]).length;i++){h+='<div class=\"ln\" data-i=\"'+i+'\"></div>'}$('#lrcBox').html(h);$('#lrcBox .ln').each(function(idx){$(this).text(d.lines[idx].x||'')});_st.lrccur=(typeof d.cur==='number')?d.cur:-1;markLrcCur(true)}).fail(function(){})}function markLrcCur(scroll){var $b=$('#lrcBox');if(!$b.length)return;$b.find('.ln').removeClass('cur');if(_st.lrccur<0)return;var $c=$b.find('.ln[data";
	page += L"-i=\"'+_st.lrccur+'\"]');if(!$c.length)return;$c.addClass('cur');if(scroll){var lh=$c.outerHeight()||28;var top=$c.position().top+$b.scrollTop()-lh*2.2;if(top<0)top=0;$b.stop(true).animate({scrollTop:top},180)}}function loadEq(force){$.getJSON('/api/eq').done(function(d){if(!d)return;if(_userEq&&!force)return;var i;for(i=0;i<15;i++){var v=(d.eq&&typeof d.eq[i]==='number')?d.eq[i]:100;$('#eq'+i).val(v);$('#eqv'+i).text(v)}if(typeof d.pre==='number')$('#eqPre').val(String(d.pre));if(typeof d.env==='number')$('#eqEnv').val(String(d.env));if(typeof d.rev==='number'){$('#eqRev').val(d.rev);$('#eqRevV').text(d.rev)}if(typeof d.cho==='number'){$('#eqCho').val(d.cho);$('#eqChoV').text(d.cho)}if(typeof d.del==='number')";
	page += L"{$('#eqDel').val(d.del);$('#eqDelV').text(d.del)}if(typeof d.eff==='number'){$('#eqEff').val(d.eff);$('#eqEffV').text(d.eff)}_eqReady=1}).fail(function(){})}var _vinyl={drag:0,lastA:0,spin:0,head:0,pending:0,raf:0};function vinylAng(e,el){var r=el.getBoundingClientRect(),cx=r.left+r.width/2,cy=r.top+r.height/2;var x=(e.clientX!=null?e.clientX:(e.touches&&e.touches[0]?e.touches[0].clientX:0))-cx;var y=(e.clientY!=null?e.clientY:(e.touches&&e.touches[0]?e.touches[0].clientY:0))-cy;return Math.atan2(y,x)*180/Math.PI}function drawVinyl(){var c=document.getElementById('vinyl');if(!c)return;var ctx=c.getContext('2d'),W=c.width,H=c.height,cx=W/2,cy=H/2,R=Math.min(W,H)/2-8;ctx.clearRect(0,0,W,H);ctx.save();ctx.translate(cx,cy);ctx.rotate(((_vinyl.spin+_vinyl.head)%360)*Math.PI/180);ctx.beginPath();ctx.arc(0,0,R,0,Math.PI*2);ctx.fillStyle='#1c1e26';ctx.fill();for(var i=0;i<18;i++){ctx.beginPath();ctx.arc(0,0,R*(0.92-i*0.035),0,Math.PI*2);ctx.strokeStyle='rgba(255,255,255,'+(0.04+(i%2)*0.03)+')';ctx.lineWidth=2;ctx.stroke()}ctx.beginPath();ctx.arc(0,0,R*0.22,0,Math.PI*2);ctx.fillStyle='#ff69b4';ctx.fill();ctx.beginPath();ctx.arc(0,0,R*0.08,0,Math.PI*2);ctx.fillStyle='#fff';ctx.fill();ctx.strokeStyle='#ffd28c';ctx.lineWidth=6;ctx.beginPath();ctx.moveTo(0,-R*0.22);ctx.lineTo(0,-R*0.92);ctx.stroke();ctx.restore()}function loadDj(force){$.getJSON('/api/dj').done(function(d){if(!d)return;if(_vinyl.drag)return;if(typeof d.head==='number')_vinyl.head=d.head;if(d.playing){_vinyl.spin=(_vinyl.spin+2.2)%360}drawVinyl();_djReady=1}).fail(function(){})}function flushScratch(){if(!_vinyl.pending)return;var d=Math.round(_vinyl.pending*100);_vinyl.pending=0;if(d===0)return;sendCmd('scr','&d='+d)}$(function(){setState('";
	page += state;
	page += L"');$('#artist').toggle(!!$('#artist').text());$('#album').toggle(!!$('#album').text());$(document).on('click','.tab',function(){showTab($(this).data('tab'))});$(document).on('click','.btn[data-cmd]',function(){var $b=$(this),c=$b.data('cmd');$b.addClass('busy');sendCmd(c).always(function(){$b.removeClass('busy');setTimeout(refresh,80);toast(c)})});var volTimer=null;$('#vol').on('input',function(){_userVol=1;$('#volVal').text(this.value)});$('#vol').on('change input',function(){var v=+this.value;clearTimeout(volTimer);volTimer=setTimeout(function(){sendCmd('vol','&v='+v).always(function(){_userVol=0;refresh()})},120)});$(document).on('click','#plList .li',function(){var i=+$(this).data('i');s";
	page += L"endCmd('playidx','&i='+i).always(function(){setTimeout(function(){refresh();loadPlaylist()},100);toast('play')})});$('#plPrev').on('click',function(){if(_plOff<=0)return;_plOff=Math.max(0,_plOff-_plPage);loadPlaylist()});$('#plNext').on('click',function(){_plOff+=_plPage;loadPlaylist()});$('.lrcbtn').on('click',function(){var d=+$(this).data('d');sendCmd('lrc','&delta='+d).always(function(){setTimeout(function(){loadLyrics(true)},80);toast('lrc')})});$('#lrcSave').on('click',function(){sendCmd('lrcsave').always(function(){toast('save')})});var eqT=null;$(document).on('input change','.eqb',function(){_userEq=1;var b=+$(this).attr('data-b'),v=+this.value;$('#eqv'+b).text(v);clearTimeout(eqT);eqT=setTimeou";
	page += L"t(function(){sendCmd('eqband','&b='+b+'&v='+v).always(function(){_userEq=0})},80)});$('#eqPre').on('change',function(){_userEq=1;sendCmd('eqpreset','&p='+this.value).always(function(){setTimeout(function(){_userEq=0;loadEq(true)},120)})});$('#eqEnv').on('change',function(){_userEq=1;sendCmd('eqenv','&p='+this.value).always(function(){_userEq=0})});var fxT=null;function fxSend(which,v){_userEq=1;clearTimeout(fxT);fxT=setTimeout(function(){sendCmd('eqfx','&w='+which+'&v='+v).always(function(){_userEq=0})},80)}$('#eqRev').on('input change',function(){$('#eqRevV').text(this.value);fxSend(0,+this.value)});$('#eqCho').on('input change',function(){$('#eqChoV').text(this.value);fxSend(1,+this.value)});$('#eqDel";
	page += L"').on('input change',function(){$('#eqDelV').text(this.value);fxSend(2,+this.value)});$('#eqEff').on('input change',function(){$('#eqEffV').text(this.value);fxSend(3,+this.value)});function onVinylDown(e){var el=document.getElementById('vinyl');if(!el)return;e.preventDefault();_vinyl.drag=1;_vinyl.lastA=vinylAng(e,el);_vinyl.pending=0;sendCmd('scrbeg');if(el.setPointerCapture&&e.pointerId!=null)el.setPointerCapture(e.pointerId)}function onVinylMove(e){if(!_vinyl.drag)return;e.preventDefault();var el=document.getElementById('vinyl');var a=vinylAng(e,el);var d=a-_vinyl.lastA;if(d>180)d-=360;if(d<-180)d+=360;_vinyl.lastA=a;_vinyl.spin=(_vinyl.spin+d)%360;_vinyl.pending+=d;drawVinyl();if(!_vinyl.raf)_vinyl.raf=requestAnimationFrame(function(){_vinyl.raf=0;flushScratch()})}";
	page += L"function onVinylUp(e){if(!_vinyl.drag)return;_vinyl.drag=0;flushScratch();sendCmd('scrend')}var vv=document.getElementById('vinyl');if(vv){vv.addEventListener('pointerdown',onVinylDown);vv.addEventListener('pointermove',onVinylMove);vv.addEventListener('pointerup',onVinylUp);vv.addEventListener('pointercancel',onVinylUp);drawVinyl()}showTab('play');setInterval(function(){refresh();if(_tab==='lrc')loadLyrics(false);if(_tab==='dj')loadDj(false)},2000);refresh();});";
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
	int posCs = 0, durCs = 0, head100 = 0;
	if (og && ::IsWindow(og->GetSafeHwnd()) && og->m_time.GetSafeHwnd()) {
		int mn = 0, mx = 0;
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
	if ((st & 0xF0) == 0x90 && d2 > 0) {
		if (d1 == 60) cmd = 0;
		else if (d1 == 61) cmd = 1;
		else if (d1 == 62) cmd = 2;
	}
	else if ((st & 0xF0) == 0xB0 && d1 == 7) {
		// CC7 = 絶対音量 0..100（±5 連打にしない）
		volAbs = (int)d2 * 100 / 127;
		cmd = 10;
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
	case 0: mpDlg->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_MP_PLAY, BN_CLICKED), 0); break;
	case 1: mpDlg->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_MP_PAUSE, BN_CLICKED), 0); break;
	case 2: mpDlg->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_MP_NEXT, BN_CLICKED), 0); break;
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
	case 5: mpDlg->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_MP_PREV, BN_CLICKED), 0); break;
	case 6: mpDlg->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_MP_STOP, BN_CLICKED), 0); break;
	case 7:
	case 8:
		if (og && ::IsWindow(og->GetSafeHwnd()) && og->m_time.GetSafeHwnd()) {
			int mn = 0, mx = 0;
			og->m_time.GetRange(mn, mx);
			const int span = mx - mn;
			int delta = span / 20; // 約5%
			if (delta < 1) delta = 1;
			int pos = og->m_time.GetPos() + ((wParam == 8) ? delta : -delta);
			MpDjSeekToSliderPos(pos);
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
	case 20: // scratch begin
		g_mpRemoteScratchLastMs = 0;
		MpDjScratchBegin();
		break;
	case 21: // scratch end
		MpDjScratchEnd();
		g_mpRemoteScratchLastMs = 0;
		break;
	case 22: // scratch delta (centidegrees)
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
		if (savedata.mpDetectedBpm > 0)
			bpm.Format(L"BPM %d", savedata.mpDetectedBpm);
		else
			bpm = LL14(L"BPM --", L"BPM --", L"BPM --", L"BPM --", L"BPM --", L"BPM --", L"BPM --", L"BPM --", L"BPM --", L"BPM --", L"BPM --", L"BPM --", L"BPM --", L"BPM --");
		if (m_bpm.GetSafeHwnd()) m_bpm.SetWindowText(bpm);
		int pp = 100, tp = 100;
		if (og && og->m_pitch_sl.GetSafeHwnd())
			pp = (int)TempoPercentFromPos(og->m_pitch_sl.GetPos());
		if (og && og->m_tempo_sl.GetSafeHwnd())
			tp = (int)TempoPercentFromPos(og->m_tempo_sl.GetPos());
		CString st;
		st.Format(L"P %d%%  T %d%%  V %d  MS %d", pp, tp, savedata.mpVocalCenter, savedata.pro_ms_width);
		if (m_status.GetSafeHwnd()) m_status.SetWindowText(st);
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
			m_seek.SetBeatGrid((float)savedata.mpDetectedBpm, savedata.mpBeatGrid ? TRUE : FALSE);
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
			topMost);
		menu.AddCheck(11,
			LL14(L"メインに追随", L"Follow main window", L"Suivre la fenetre principale", L"Segui finestra principale",
				L"Seguir ventana principal", L"메인 창 따라가기", L"跟随主窗口", L"اتبع النافذة الرئيسية", L"Следовать главному", L"Hauptfenster folgen",
				L"Seguir janela principal", L"Volg hoofdvenster", L"Podazaj za glownym", L"Ana pencereyi izle"),
			savedata.mpDjPadMainLock != 0);
		menu.AddSeparator();
		menu.AddCommand(12,
			LL14(L"音程/テンポをリセット", L"Reset pitch/tempo", L"Reinit hauteur/tempo", L"Reset pitch/tempo",
				L"Restablecer tono/tempo", L"음정/템포 초기화", L"重置音高/速度", L"إعادة الدرجة/الإيقاع", L"Сброс тона/темпа", L"Tonhöhe/Tempo zurücksetzen",
				L"Redefinir tom/tempo", L"Toonhoogte/tempo resetten", L"Reset wysokosci/tempa", L"Perde/tempo sifirla"));
		menu.AddCommand(13,
			LL14(L"EQ/フィルタをリセット", L"Reset EQ/filter", L"Reinit EQ/filtre", L"Reset EQ/filtro",
				L"Restablecer EQ/filtro", L"EQ/필터 초기화", L"重置 EQ/滤镜", L"إعادة EQ/المرشح", L"Сброс EQ/фильтра", L"EQ/Filter zurücksetzen",
				L"Redefinir EQ/filtro", L"EQ/filter resetten", L"Reset EQ/filtra", L"EQ/filtre sifirla"));
		menu.AddCommand(14,
			LL14(L"スクラッチ設定をリセット", L"Reset scratch settings", L"Reinit scratch", L"Reset scratch",
				L"Restablecer scratch", L"스크래치 초기화", L"重置刮盘设置", L"إعادة إعدادات الخدش", L"Сброс скретча", L"Scratch zurücksetzen",
				L"Redefinir scratch", L"Scratch resetten", L"Reset scratch", L"Scratch sifirla"));
		menu.AddCommand(15,
			LL14(L"BPM計測", L"Detect BPM", L"Detecter BPM", L"Rileva BPM", L"Detectar BPM",
				L"BPM 측정", L"检测 BPM", L"اكتشاف BPM", L"Определить BPM", L"BPM erkennen",
				L"Detectar BPM", L"BPM detecteren", L"Wykryj BPM", L"BPM algila"));
		menu.AddSeparator();
		menu.AddCommand(1,
			LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
		const UINT cmd = menu.Track(point, this);
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
	CCustomEdit m_port;
	CCustomStatic m_url;
	CCustomStandardButton m_open;
protected:
	virtual void DoDataExchange(CDataExchange* pDX)
	{
		CCustomBlurDialogBase::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_REMOTE_ENABLE, m_enable);
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
		m_enable.SetWindowText(LL14(L"Wi-Fi / LAN で HTTP（最大3台）", L"HTTP on Wi-Fi/LAN (max 3)", L"HTTP Wi-Fi/LAN (max 3)", L"HTTP Wi-Fi/LAN (max 3)", L"HTTP Wi-Fi/LAN (máx. 3)",
			L"Wi-Fi/LAN HTTP (최대 3)", L"Wi-Fi/LAN HTTP（最多3）", L"HTTP عبر Wi-Fi/LAN (حد 3)", L"HTTP по Wi-Fi/LAN (до 3)", L"HTTP im WLAN/LAN (max. 3)",
			L"HTTP no Wi-Fi/LAN (máx. 3)", L"HTTP op Wi-Fi/LAN (max 3)", L"HTTP w Wi-Fi/LAN (max 3)", L"Wi-Fi/LAN HTTP (en fazla 3)"));
		SetDlgItemText(IDC_REMOTE_PORT_L, LL14(L"ポート", L"Port", L"Port", L"Porta", L"Puerto",
			L"포트", L"端口", L"منفذ", L"Порт", L"Port", L"Porta", L"Poort", L"Port", L"Port"));
		m_open.SetWindowText(LL14(L"ブラウザで開く", L"Open in browser", L"Ouvrir dans le navigateur", L"Apri nel browser", L"Abrir en el navegador",
			L"브라우저에서 열기", L"在浏览器打开", L"فتح في المتصفح", L"Открыть в браузере", L"Im Browser öffnen",
			L"Abrir no navegador", L"Openen in browser", L"Otworz w przegladarce", L"Tarayicida ac"));
		m_enable.SetCheck(savedata.mpRemoteOn ? BST_CHECKED : BST_UNCHECKED);
		CString p; p.Format(_T("%d"), savedata.mpRemotePort);
		m_port.SetWindowText(p);
		RefreshUrlLabel();
		m_open.SetGradation(RGB(220, 240, 255), RGB(160, 200, 240), 0, TRUE);
		CCustomControlUtility::BeginDialogToolTip(m_tooltip, this, TTS_NOPREFIX);
		m_tooltip.AddTool(&m_enable, LL14(L"同じ Wi-Fi のスマホ／PC から再生操作できる HTTP サーバ（同時3接続まで）。", L"HTTP server for phones/PCs on the same Wi-Fi (up to 3 clients).", L"Serveur HTTP pour telephones/PC sur le meme Wi-Fi (max 3).", L"Server HTTP per telefoni/PC sulla stessa Wi-Fi (max 3).", L"Servidor HTTP para moviles/PC en la misma Wi-Fi (máx. 3).",
			L"같은 Wi-Fi의 폰/PC에서 조작하는 HTTP 서버(최대 3).", L"同一 Wi-Fi 下手机/PC 控制的 HTTP 服务器（最多3）。", L"خادم HTTP للهواتف/أجهزة الكمبيوتر على نفس Wi-Fi (حد 3).", L"HTTP-сервер для телефонов/ПК в той же Wi-Fi (до 3).", L"HTTP-Server für Telefone/PCs im gleichen WLAN (max. 3).",
			L"Servidor HTTP para telemoveis/PCs na mesma Wi-Fi (máx. 3).", L"HTTP-server voor telefoons/pc's op hetzelfde Wi-Fi (max 3).", L"Serwer HTTP dla telefonow/PC w tej samej Wi-Fi (max 3).", L"Ayni Wi-Fi'deki telefon/PC icin HTTP sunucusu (en fazla 3)."));
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
	DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CMpRemoteDlg, CCustomBlurDialogBase)
	ON_WM_DESTROY()
	ON_EN_CHANGE(IDC_REMOTE_PORT, &CMpRemoteDlg::OnEnChangePort)
	ON_BN_CLICKED(IDC_REMOTE_OPEN, &CMpRemoteDlg::OnOpenBrowser)
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
			topMost);
		menu.AddCheck(3,
			LL14(L"最大化", L"Maximize", L"Agrandir", L"Ingrandisci", L"Maximizar", L"최대화", L"最大化", L"تكبير",
				L"Развернуть", L"Maximieren", L"Maximizar", L"Maximaliseren", L"Maksymalizuj", L"Buyut"),
			maximized);
		menu.AddSeparator();
		menu.AddCommand(1,
			LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
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
