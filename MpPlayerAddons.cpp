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
#include "PlayList.h"
#include "AudioUpscaler.h"
#include "SongParams.h"

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
extern int plf;
extern CMediaPlayerDlg* mp;
extern int wavbit_sample_Hz;
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

// 再生PCM経路（LoudnessFeed）。~500Hz 間引き＋ABS封筒
void MpBpmNotifyPcm(const float* L, const float* R, int frames, int sampleRate)
{
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
		const char* q = strchr(line, '?');
		if (MpRemoteHasQueryParam(q, "c=play")) MpRemoteSendCmd(0);
		else if (MpRemoteHasQueryParam(q, "c=pause")) MpRemoteSendCmd(1);
		else if (MpRemoteHasQueryParam(q, "c=next")) MpRemoteSendCmd(2);
		else if (MpRemoteHasQueryParam(q, "c=volup")) MpRemoteSendCmd(3);
		else if (MpRemoteHasQueryParam(q, "c=voldn")) MpRemoteSendCmd(4);
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
			if (mpDlg->m_vol.GetSafeHwnd()) {
				mpDlg->m_vol.SetPos(v);
				mpDlg->SendMessage(WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, v), (LPARAM)mpDlg->m_vol.GetSafeHwnd());
			}
			if (og && og->m_sl.GetSafeHwnd()) {
				og->m_sl.SetPos(v * 1000);
				og->PostMessage(WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, v * 1000), (LPARAM)og->m_sl.GetSafeHwnd());
			}
		}
		break;
	case 10:
		{
			int v = (int)lParam;
			if (v < 0) v = 0;
			if (v > 100) v = 100;
			if (mpDlg->m_vol.GetSafeHwnd()) {
				mpDlg->m_vol.SetPos(v);
				mpDlg->SendMessage(WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, v), (LPARAM)mpDlg->m_vol.GetSafeHwnd());
			}
			if (og && og->m_sl.GetSafeHwnd()) {
				og->m_sl.SetPos(v * 1000);
				og->PostMessage(WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, v * 1000), (LPARAM)og->m_sl.GetSafeHwnd());
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

void MpOnGameCapturePreset(CMediaPlayerDlg* mpDlg)
{
	if (!mpDlg || !::IsWindow(mpDlg->GetSafeHwnd()))
		return;

	CPoint pt;
	::GetCursorPos(&pt);

	enum {
		ID_GCP_720_60 = 42001,
		ID_GCP_1080_60 = 42002,
		ID_GCP_1080_120 = 42003,
		ID_GCP_4K_60 = 42004,
		ID_GCP_PLUS_WAV = 42005
	};

	CCustomPopupMenu menu;
	menu.SetAeroMode(FALSE);
	menu.AddCommand(ID_GCP_720_60,
		LL14(L"720p / 60fps（軽量）", L"720p / 60fps (light)", L"720p / 60fps (leger)", L"720p / 60fps (leggero)", L"720p / 60fps (ligero)",
			L"720p / 60fps (가벼움)", L"720p / 60fps（轻量）", L"720p / 60fps (خفيف)", L"720p / 60fps (лёгкий)", L"720p / 60fps (leicht)",
			L"720p / 60fps (leve)", L"720p / 60fps (licht)", L"720p / 60fps (lekki)", L"720p / 60fps (hafif)"),
		LL14(L"負荷を抑えたゲーム録画。まずはこれで試せます。", L"Lower-load game capture. Good starting point.", L"Capture jeu legere. Bon point de depart.", L"Cattura gioco leggera. Buon inizio.", L"Captura ligera. Buen punto de partida.",
			L"부하를 낮춘 게임 녹화. 먼저 이것부터.", L"低负载游戏录制。可先试这个。", L"تسجيل لعبة خفيف. نقطة بداية جيدة.", L"Лёгкая запись игры. Хороший старт.", L"Leichte Game-Aufnahme. Guter Start.",
			L"Captura leve. Bom ponto de partida.", L"Lichte game-opname. Goed startpunt.", L"Lekkie nagrywanie gry. Dobry start.", L"Hafif oyun kaydi. Iyi baslangic."));
	menu.AddCommand(ID_GCP_1080_60,
		LL14(L"1080p / 60fps（推奨・高品位）", L"1080p / 60fps (recommended)", L"1080p / 60fps (recommande)", L"1080p / 60fps (consigliato)", L"1080p / 60fps (recomendado)",
			L"1080p / 60fps (권장·고화질)", L"1080p / 60fps（推荐·高画质）", L"1080p / 60fps (موصى به)", L"1080p / 60fps (рекомендуется)", L"1080p / 60fps (empfohlen)",
			L"1080p / 60fps (recomendado)", L"1080p / 60fps (aanbevolen)", L"1080p / 60fps (zalecane)", L"1080p / 60fps (onerilen)"),
		LL14(L"フルHD・60fps・高ビットレート。多くのゲーム向けの標準高品位。", L"Full HD 60fps high bitrate. Standard high quality for most games.", L"Plein HD 60fps debit eleve. Qualite standard pour la plupart des jeux.", L"Full HD 60fps bitrate alto. Qualita standard per molti giochi.", L"Full HD 60fps alto bitrate. Calidad estandar para la mayoria.",
			L"풀HD 60fps 고비트레이트. 대부분 게임에 맞는 표준 고화질.", L"全高清60fps高码率。多数游戏的标准高画质。", L"Full HD 60 إطار بمعدل بت عالٍ. جودة قياسية لمعظم الألعاب.", L"Full HD 60fps высокий битрейт. Стандарт для большинства игр.", L"Full HD 60fps hohe Bitrate. Standard-Qualität für die meisten Spiele.",
			L"Full HD 60fps alto bitrate. Qualidade padrao para a maioria.", L"Full HD 60fps hoge bitrate. Standaardkwaliteit voor de meeste games.", L"Full HD 60fps wysoki bitrate. Standard dla wiekszosci gier.", L"Full HD 60fps yuksek bitrate. Cogu oyun icin standart kalite."));
	menu.AddCommand(ID_GCP_1080_120,
		LL14(L"1080p / 120fps（滑らか・高負荷）", L"1080p / 120fps (smooth·heavy)", L"1080p / 120fps (fluide·lourd)", L"1080p / 120fps (fluido·pesante)", L"1080p / 120fps (suave·pesado)",
			L"1080p / 120fps (부드러움·고부하)", L"1080p / 120fps（流畅·高负载）", L"1080p / 120fps (سلس·ثقيل)", L"1080p / 120fps (плавно·тяжело)", L"1080p / 120fps (flüssig·schwer)",
			L"1080p / 120fps (suave·pesado)", L"1080p / 120fps (soepel·zwaar)", L"1080p / 120fps (plynnie·ciezkie)", L"1080p / 120fps (akici·agir)"),
		LL14(L"高リフレッシュ向け。PC性能が必要です。", L"For high-refresh games. Needs a strong PC.", L"Pour ecrans haute frequence. PC puissant requis.", L"Per alti Hz. Serve un PC potente.", L"Para alto refresco. Requiere PC potente.",
			L"고주사율용. 강한 PC 필요.", L"适合高刷新。需要较强电脑。", L"لشاشات عالية التردد. يحتاج جهاز قوي.", L"Для высокого Гц. Нужен мощный ПК.", L"Für hohe Hz. Starker PC nötig.",
			L"Para alto refresh. Precisa de PC forte.", L"Voor hoge Hz. Sterke PC nodig.", L"Dla wysokiego Hz. Potrzebny mocny PC.", L"Yuksek Hz icin. Guclu PC gerekir."));
	menu.AddCommand(ID_GCP_4K_60,
		LL14(L"4K / 60fps（最高画質）", L"4K / 60fps (max quality)", L"4K / 60fps (qualite max)", L"4K / 60fps (qualita max)", L"4K / 60fps (max calidad)",
			L"4K / 60fps (최고화질)", L"4K / 60fps（最高画质）", L"4K / 60fps (أقصى جودة)", L"4K / 60fps (макс. качество)", L"4K / 60fps (max. Qualität)",
			L"4K / 60fps (qualidade max)", L"4K / 60fps (max kwaliteit)", L"4K / 60fps (max jakosc)", L"4K / 60fps (en yuksek kalite)"),
		LL14(L"3840×2160。容量と負荷が最大。強力なGPU向け。", L"3840×2160. Largest size/load. For strong GPUs.", L"3840×2160. Taille/charge max. GPU puissant.", L"3840×2160. Dimensione/carico max. GPU potente.", L"3840×2160. Tamano/carga max. GPU potente.",
			L"3840×2160. 용량·부하 최대. 강한 GPU용.", L"3840×2160。体积与负载最大。需强GPU。", L"3840×2160. أكبر حجم/حمل. لوحدة GPU قوية.", L"3840×2160. Макс. размер/нагрузка. Для мощных GPU.", L"3840×2160. Max. Größe/Last. Für starke GPUs.",
			L"3840×2160. Tamanho/carga max. Para GPU forte.", L"3840×2160. Max. formaat/belasting. Voor sterke GPU.", L"3840×2160. Max. rozmiar/obciazenie. Dla mocnego GPU.", L"3840×2160. En buyuk boyut/yuk. Guclu GPU icin."));
	menu.AddSeparator();
	menu.AddCommand(ID_GCP_PLUS_WAV,
		LL14(L"1080p60 + 高音質WAV録音も開く", L"1080p60 + open HQ WAV recorder", L"1080p60 + ouvrir enregistreur WAV HQ", L"1080p60 + apri registratore WAV HQ", L"1080p60 + abrir grabador WAV HQ",
			L"1080p60 + 고음질 WAV 녹음도 열기", L"1080p60 + 同时打开高音质WAV录音", L"1080p60 + فتح مسجل WAV عالي الجودة", L"1080p60 + открыть WAV-запись HQ", L"1080p60 + WAV-Rekorder HQ öffnen",
			L"1080p60 + abrir gravador WAV HQ", L"1080p60 + WAV-recorder HQ openen", L"1080p60 + otworz rejestrator WAV HQ", L"1080p60 + yuksek kaliteli WAV ac"),
		LL14(L"画面キャプチャに加え、システム音を別途WAVで残します。", L"Screen capture plus a separate WAV of system audio.", L"Capture ecran plus WAV separe du son systeme.", L"Cattura piu WAV separato dell'audio di sistema.", L"Captura mas WAV aparte del audio del sistema.",
			L"화면 캡처와 함께 시스템 음을 별도 WAV로 남김.", L"画面捕获并另存系统声为WAV。", L"التقاط الشاشة مع WAV منفصل لصوت النظام.", L"Захват экрана плюс отдельный WAV системного звука.", L"Bildschirmaufnahme plus separates System-WAV.",
			L"Captura mais WAV separado do audio do sistema.", L"Schermopname plus apart systeemaudio-WAV.", L"Nagranie ekranu plus osobny WAV dzwieku systemu.", L"Ekran yakalama artı ayri sistem sesi WAV."));

	const UINT cmd = menu.Track(pt, mpDlg);
	if (cmd == 0)
		return;

	int canvas = 2; // 1080
	int fps = 60;
	BOOL openWav = FALSE;
	if (cmd == ID_GCP_720_60) { canvas = 1; fps = 60; }
	else if (cmd == ID_GCP_1080_60) { canvas = 2; fps = 60; }
	else if (cmd == ID_GCP_1080_120) { canvas = 2; fps = 120; }
	else if (cmd == ID_GCP_4K_60) { canvas = 4; fps = 60; }
	else if (cmd == ID_GCP_PLUS_WAV) { canvas = 2; fps = 60; openWav = TRUE; }
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
	virtual void PostNcDestroy()
	{
		g_mpDjPad = NULL;
		if (savedata.mpDjPadwindow) {
			savedata.mpDjPadwindow = 0;
			MpPersistSavedataQuick();
		}
		CCustomBlurDialogBase::PostNcDestroy();
		delete this;
	}
	afx_msg void OnPitchUp() { MpDjApplyPitchTempoDelta(TRUE, +3); }
	afx_msg void OnPitchDn() { MpDjApplyPitchTempoDelta(TRUE, -3); }
	afx_msg void OnTempoUp() { MpDjApplyPitchTempoDelta(FALSE, +3); }
	afx_msg void OnTempoDn() { MpDjApplyPitchTempoDelta(FALSE, -3); }
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
	if (savedata.mpDjPadwindow) {
		savedata.mpDjPadwindow = 0;
		MpPersistSavedataQuick();
	}
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
