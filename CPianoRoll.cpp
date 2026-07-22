#include "stdafx.h"
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "CPianoRoll.h"

class COggDlg;
extern COggDlg* og;
#include "resource.h"
#include "NoteFundamentalPick.h"
#include "PianoRollPick.h"
#include "PianoRoll108Detect.h"
#include "PianoKeyTable.h"
#include "HarmonicProfile.h"
#include "PianoRollGoertzelAvx2.h"
#include <algorithm>

extern save savedata;
void COggDlg_SyncPianoRollFast();
void COggDlg_ShowPianoRollTune();

static int PrTuneClampPct(int v)
{
	if (v <= 0) return 100;
	if (v < 25) return 25;
	if (v > 400) return 400;
	return v;
}

static float PrTuneF(int pct, float defVal)
{
	return defVal * (float)PrTuneClampPct(pct) / 100.0f;
}

IMPLEMENT_DYNAMIC(CPianoRoll, CCustomBlurDialogExBase)

// pick fix 5 ベースを低音/中高音(一体)の2帯に整理。
// BAND_MID_END はレーン色用のみ。追加パッチ関数は置かない。
namespace Cfg
{
    // piano roll3: 基音ピック + NormalizeDisplayPeak + 包絡ホールド
    static constexpr float IIR_ALPHA = 0.40f;
    static constexpr float IIR_ALPHA_BASS = 0.28f;
    // 絶対無音ゲート。解析AGCを -3dBFS 目標にしたあとでも、
    // 弱い楽節(例: OST導入部)がここで全滅しないよう 0.002→0.0007 に下げた。
    static constexpr float SILENCE_ABS = 0.0007f;
    static constexpr float BAND_SILENCE_BASS = 0.00055f;
    static constexpr float BAND_SILENCE_MID = 0.00045f;
    static constexpr float BAND_SILENCE_TRE = 0.00045f;
    static constexpr int   ATTACK_FRAMES = 1;
    static constexpr int   ATTACK_FRAMES_EDGE = 2;
    // T130 の 16分(~115ms)/32分(~58ms)を分離できるようリリースを短く。
    // 旧5だと中域 TemporalFrames で最大20F(≈460ms)まで伸びて連打が1本に見える。
    static constexpr int   RELEASE_FRAMES = 3;
    static constexpr int   VIS_GAP_FRAMES = 2;
    static constexpr int   VIS_GAP_FRAMES_BASS = 3;
    static constexpr int   VIS_GAP_FRAMES_MID = 1;
    static constexpr int   VIS_GAP_FRAMES_TRE = 1;
    static constexpr int   VIS_GAP_SUSTAIN_BONUS = 1;
    static constexpr int   ATTACK_MISS_GRACE = 2;
    static constexpr float RETRIGGER_RATIO = 0.28f;
    static constexpr int   BAND_BASS_END = PianoRoll108::BASS_END;
    static constexpr int   BAND_MID_END = PianoRoll108::MID_END;
    static constexpr int   KEY_O2G = 43;   // G2
    static constexpr int   KEY_O5C = 72;   // C5
    static constexpr int   KEY_O7C = 96;   // C7
    // ノート有無: 調波サリエンスの局所ピークが領域ノイズ床×SNR を超えること。
    // 帯域max比は使わない（静音で高音が消え、派手な曲で砂になるため）。
    static constexpr float BASS_PICK_THRESH = 0.20f;
    static constexpr float UPPER_PICK_THRESH = 0.06f;
    static constexpr float PEAK_SNR = 3.0f;
    static constexpr float PEAK_REGION_REL = 0.055f;  // legacy pick helpers (unused path)
    // [検証のため元値へ復帰] 隣接ホッピング対策を単独の変数として切り分けて検証する。
    static constexpr float HOLD_ENV_BASS = 0.28f;
    static constexpr float HOLD_ENV_MID = 0.22f;  // 短音分離: 旧0.14は減衰残響で隙間を埋める
    static constexpr float HOLD_ENV_TRE = 0.24f;
    static constexpr float DISPLAY_PEAK_CAP = 5.0f;
    static constexpr int   ANALYZE_INTERVAL = 1024; // FeedPCM未使用。実ホップは SyncPianoRoll + ANALYZE_MIN_MS
    static constexpr int   ONSET_KEY_START = 62;  // D4（旧 index 41 + MIDI base 21）
    static constexpr float ONSET_DELTA_THRESH = 0.012f;
    static constexpr float ONSET_MIN_STRENGTH = 0.018f;
    static constexpr float BASS_ONSET_DELTA_THRESH = 0.034f;
    static constexpr float BASS_ONSET_MIN_STRENGTH = 0.055f;
    static constexpr float UPPER_ONSET_DELTA_THRESH = 0.040f;
    static constexpr float UPPER_ONSET_MIN_STRENGTH = 0.065f;

    // 解析専用の双方向AGC。再生用EQマスターとは独立に、窓ピークを目標へ揃える。
    // 旧: 上げのみ(+→-11dB、既に大きい入力は触らない) → 静かな曲は床に沈み、
    //     マスター上げで初めて載る／その分ゴーストが増える非対称が起きていた。
    // 新: 上げ下げとも -3dBFS 付近へ正規化し、音量スライダーに検出を依存させない。
    static constexpr float BUFWAV3_TARGET_PEAK_DB = -3.0f;
    static constexpr float BUFWAV3_GAIN_DB_MAX = 36.0f;   // 静かな曲への最大ブースト
    static constexpr float BUFWAV3_GAIN_DB_MIN = -24.0f;  // 大きい入力／マスター上げのカット
    static constexpr float BUFWAV3_PEAK_FLOOR_DB = -60.0f;
    // 絶対ノイズ床の基準(AGC後)。旧0.0015は -3dB 動作点では厳しすぎる。
    static constexpr float ABS_NOISE_FLOOR_BASE = 0.00055f;
    // スペクトル相対床: 下位パーセンタイル × 倍率 (マスター上げ時の砂粒ゴースト対策)
    static constexpr float SPECTRAL_NOISE_PERCENTILE = 0.35f;
    static constexpr float SPECTRAL_NOISE_FLOOR_MUL = 2.2f;

    static float PeakDbFs(const double* samples, int n)
    {
        if (!samples || n <= 0) return BUFWAV3_PEAK_FLOOR_DB;
        double peak = 0.0;
        double sumSq = 0.0;
        for (int i = 0; i < n; ++i) {
            const double a = fabs(samples[i]);
            if (a > peak) peak = a;
            sumSq += a * a;
        }
        const double rms = sqrt(sumSq / (double)n);
        double level = peak;
        if (rms * 4.0 > level) level = rms * 4.0;
        if (level < 1e-9) return BUFWAV3_PEAK_FLOOR_DB;
        return (float)(20.0 * log10(level));
    }

    static float PeakDbFsWindows(const double* winLow, int nLow,
        const double* winBass, int nBass)
    {
        float db = PeakDbFs(winLow, nLow);
        if (winBass && nBass > 0) {
            const float dbB = PeakDbFs(winBass, nBass);
            if (dbB > db) db = dbB;
        }
        return db;
    }

    // フル窓ピークのみ。静かな半分を使うと音頭前の無音でゲインが跳ね、ノイズ床がノートになる。
    static float LevelDbForDynamics(const double* winLow, int nLow,
        const double* winBass, int nBass)
    {
        return PeakDbFsWindows(winLow, nLow, winBass, nBass);
    }

    // 解析窓を目標ピークへ双方向正規化するゲイン(dB)。再生音量には影響しない。
    static float MakeupGainDb(float peakDbFs)
    {
        if (peakDbFs <= BUFWAV3_PEAK_FLOOR_DB + 0.5f)
            return 0.0f;
        float g = BUFWAV3_TARGET_PEAK_DB - peakDbFs;
        if (g > BUFWAV3_GAIN_DB_MAX) g = BUFWAV3_GAIN_DB_MAX;
        if (g < BUFWAV3_GAIN_DB_MIN) g = BUFWAV3_GAIN_DB_MIN;
        return g;
    }

    // blend スペクトルの下位パーセンタイルをノイズ床推定に使う。
    // ノートが多いフレームでは相対床が上がり、砂粒ゴーストを押し下げる。
    static float SpectralNoiseEstimate(const float* blend, int count)
    {
        if (!blend || count <= 0) return 0.0f;
        float tmp[128];
        int n = 0;
        const int cap = (count < 128) ? count : 128;
        for (int i = 0; i < count && n < cap; ++i) {
            if (blend[i] > 1e-8f)
                tmp[n++] = blend[i];
        }
        if (n < 8) return 0.0f;
        int k = (int)((float)n * SPECTRAL_NOISE_PERCENTILE);
        if (k < 0) k = 0;
        if (k >= n) k = n - 1;
        std::nth_element(tmp, tmp + k, tmp + n);
        return tmp[k];
    }

    static float PickThreshScaleFromLevelDb(float levelDb)
    {
        if (levelDb < -32.0f) return 0.58f;
        if (levelDb < -26.0f) return 0.68f;
        if (levelDb < -22.0f) return 0.79f;
        if (levelDb < -18.0f) return 0.89f;
        if (levelDb < -14.0f) return 0.97f;
        if (levelDb < -11.0f) return 1.00f;
        return 1.02f;
    }

    static void ApplyGainDbInPlace(double* samples, int n, float gainDb)
    {
        if (!samples || n <= 0) return;
        if (gainDb > -0.001f && gainDb < 0.001f) return;
        const double g = pow(10.0, (double)gainDb / 20.0);
        for (int i = 0; i < n; ++i) {
            double v = samples[i] * g;
            if (v > 1.0) v = 1.0;
            else if (v < -1.0) v = -1.0;
            samples[i] = v;
        }
    }

    // C4 = key 39。弦/鍵盤の減衰は中音域で長く、高音ほど短い。
    static float HoldEnvRatio(int keyIndex)
    {
        if (keyIndex < BAND_BASS_END) return HOLD_ENV_BASS;
        if (keyIndex < BAND_MID_END) return HOLD_ENV_MID;
        return HOLD_ENV_TRE;
    }

    static int TemporalFrames(int keyIndex, int baseFrames)
    {
        int lo = 0, hi = PianoKey::COUNT;
        if (keyIndex < BAND_BASS_END) { lo = 0; hi = BAND_BASS_END; }
        else if (keyIndex < BAND_MID_END) { lo = BAND_BASS_END; hi = BAND_MID_END; }
        else { lo = BAND_MID_END; hi = PianoKey::COUNT; }
        const int span = hi - lo;
        if (span <= 1) return baseFrames;
        const float t = (float)(keyIndex - lo) / (float)(span - 1);
        float scale;
        if (keyIndex >= BAND_MID_END)
            // 高音ほど短いが、極端に潰さない（検出後すぐ消えるのを防ぐ）
            scale = 1.35f - t * 0.70f;
        else if (keyIndex >= BAND_BASS_END)
            scale = 1.50f - t * 0.55f;
        else
            scale = 1.90f - t * 1.45f;
        // O4C 付近は物理減衰が長いので release/gap を最大 +2 フレーム
        const float dc = (float)(keyIndex - 60) / 12.0f;
        const float bell = expf(-dc * dc);
        int f = (int)(baseFrames * scale + bell * 2.0f + 0.5f);
        if (keyIndex >= BAND_MID_END) {
            if (f < 2) f = 2;
            if (f > 5) f = 5;   // 高音リリース上限: 旧12F→5F (≈115ms@1024hop ≈16分)
        }
        else {
            if (keyIndex < BAND_BASS_END) {
                if (f < 1) f = 1;
                if (f > 8) f = 8;
            }
            else {
                if (f < 2) f = 2;
                if (f > 6) f = 6; // 中音上限: 旧20F→6F (≈140ms、16分連打を分離)
            }
        }
        return f;
    }

    static int AttackFramesForKey(int keyIndex)
    {
        if (keyIndex >= PianoRoll108::BASS_END && keyIndex < PianoRoll108::EDGE_HI)
            return 1;
        if (keyIndex < 12 || keyIndex >= PianoRoll108::EDGE_HI)
            return TemporalFrames(keyIndex, ATTACK_FRAMES_EDGE);
        return 1;
    }

    static int VisGapFrames(int keyIndex)
    {
        if (keyIndex >= PianoRoll108::BASS_END && keyIndex < PianoRoll108::MID_END)
            return VIS_GAP_FRAMES_MID;
        const int base = (keyIndex < BAND_BASS_END) ? VIS_GAP_FRAMES_BASS
            : (keyIndex >= BAND_MID_END) ? VIS_GAP_FRAMES_TRE : VIS_GAP_FRAMES;
        return TemporalFrames(keyIndex, base);
    }

    static void NormalizeBandPeak(float* values, int lo, int hi, float cap)
    {
        if (!values || lo >= hi || cap <= 0.0f) return;
        float maxV = 0.0f;
        for (int i = lo; i < hi; ++i)
            if (values[i] > maxV) maxV = values[i];
        if (maxV <= cap) return;
        const float scale = cap / maxV;
        for (int i = lo; i < hi; ++i)
            values[i] *= scale;
    }


}

int CPianoRoll::ScaleWinSamples(int refSamples, int sampleRate, int capSamples)
{
    if (refSamples <= 0) return 0;
    if (sampleRate < 8000) sampleRate = REF_SAMPLE_RATE;
    int64_t n = ((int64_t)refSamples * (int64_t)sampleRate + REF_SAMPLE_RATE / 2) / REF_SAMPLE_RATE;
    if (n < 64) n = 64;
    static const int kAbsMax = 131072;
    if (n > kAbsMax) n = kAbsMax;
    if (capSamples > 0 && n > capSamples) n = capSamples;
    return (int)n;
}

int CPianoRoll::CaptureFrameCount(int sampleRate, int capSamples)
{
    return ScaleWinSamples(WIN_BASS_REF, sampleRate, capSamples);
}

int CPianoRoll::MinAnalyzeFrameCount(int sampleRate, int capSamples)
{
    return ScaleWinSamples(WIN_BASS_REF, sampleRate, capSamples);
}

CPianoRoll::CPianoRoll(CWnd* pParent)
    : CCustomBlurDialogExBase(IDD_PIANOROLL, pParent)
{
    InitializeCriticalSection(&m_cs);
    InitializeCriticalSection(&m_jobCs);
    m_ring.assign(RING_SIZE, 0.0);
    EnsureAnalysisTables(REF_SAMPLE_RATE);

    memset(m_activeKeys, 0, sizeof(m_activeKeys));
    memset(m_noteStrength, 0, sizeof(m_noteStrength));
    memset(m_rawStrengths, 0, sizeof(m_rawStrengths));
    memset(m_smoothedStrengths, 0, sizeof(m_smoothedStrengths));
    memset(m_displayStrengths, 0, sizeof(m_displayStrengths));
    memset(m_displaySmoothed, 0, sizeof(m_displaySmoothed));
    memset(m_consecActive, 0, sizeof(m_consecActive));
    memset(m_consecSilent, 0, sizeof(m_consecSilent));
    memset(m_segmentId, 0, sizeof(m_segmentId));
    memset(m_envPeak, 0, sizeof(m_envPeak));
    memset(m_unpickedFrames, 0, sizeof(m_unpickedFrames));
    memset(m_strengthDipFrames, 0, sizeof(m_strengthDipFrames));
    memset(m_transientHold, 0, sizeof(m_transientHold));
    memset(m_bandMask, 0, sizeof(m_bandMask));
    memset(m_laneStrength, 0, sizeof(m_laneStrength));
    memset(m_prevBandMask, 0, sizeof(m_prevBandMask));
    memset(m_prevRawStrengths, 0, sizeof(m_prevRawStrengths));
    memset(m_onsetStrengths, 0, sizeof(m_onsetStrengths));
    memset(m_prevOnsetStrengths, 0, sizeof(m_prevOnsetStrengths));
    memset(m_prevActiveKeys, 0, sizeof(m_prevActiveKeys));
    memset(m_prevNoteStrength, 0, sizeof(m_prevNoteStrength));
    memset(m_noteAgeFrames, 0, sizeof(m_noteAgeFrames));
    memset(m_scoopLatch, 0, sizeof(m_scoopLatch));
    memset(m_exprFlags, 0, sizeof(m_exprFlags));
    memset(m_vibHist, 0, sizeof(m_vibHist));
    memset(m_vibHistCount, 0, sizeof(m_vibHistCount));
    memset(m_keySnapActive, 0, sizeof(m_keySnapActive));
    memset(m_keySnapBand, 0, sizeof(m_keySnapBand));
    // 音色エンベロープモデルは各要素が既定コンストラクタで初期化済み(ResetOff相当の値)。
    // 念のため明示的にもオフ状態へ揃えておく。
    for (int i = 0; i < KEY_COUNT; ++i)
        m_envModel[i].ResetOff();
    memset(m_reattackMark, 0, sizeof(m_reattackMark));
    memset(m_onsetBoostThisFrame, 0, sizeof(m_onsetBoostThisFrame));
    memset(m_onsetBoostStreak, 0, sizeof(m_onsetBoostStreak));
    memset(m_harmonicGhostStreak, 0, sizeof(m_harmonicGhostStreak));
    for (int i = 0; i < PIANO_METER_CH_MAX; ++i) {
        m_chMeterDb[i] = -60.0f;
        m_chMeterFill[i] = 0.0f;
        m_chMeterAutoPeak[i] = 0.02f;
    }
    m_historyCount = 0;
    m_historyHead = 0;
    m_rollSpeedCredit = 0;
    for (int hi = 0; hi < (int)MAX_HISTORY; ++hi) {
        auto& f = m_historyRing[hi];
        memset(f.active, 0, sizeof(f.active));
        memset(f.strength, 0, sizeof(f.strength));
        memset(f.segment, 0, sizeof(f.segment));
        memset(f.bandMask, 0, sizeof(f.bandMask));
        memset(f.laneStrength, 0, sizeof(f.laneStrength));
        memset(f.expr, 0, sizeof(f.expr));
        memset(f.dynLevel, 0, sizeof(f.dynLevel));
        memset(f.reattack, 0, sizeof(f.reattack));
    }
}

CPianoRoll::~CPianoRoll()
{
    StopAnalysisWorker();
    m_feedEnabled = false;
    ReleasePaintBuffers();
    DeleteCriticalSection(&m_jobCs);
    DeleteCriticalSection(&m_cs);
}

// stop/曲切替の先頭。DoEvent 再入で旧形式バッファを解析しない。
void CPianoRoll::PauseAnalysis()
{
    m_feedEnabled = false;
    InterlockedIncrement(&m_analysisEpoch);
    InterlockedExchange(&m_jobPending, 0);
    // Peek で SYNC/ANALYSIS_DONE を捨てる前に落とす。残ると以降の Post が永久に no-op になる。
    InterlockedExchange(&m_syncPosted, 0);
    InterlockedExchange(&m_analysisDonePosted, 0);
    EnterCriticalSection(&m_jobCs);
    m_jobFrameCount = 0;
    LeaveCriticalSection(&m_jobCs);
    for (int i = 0; i < 1000; ++i) {
        if (InterlockedCompareExchange(&m_analysisBusy, 0, 0) == 0)
            break;
        Sleep(1);
    }
    InterlockedExchange(&m_jobPending, 0);
}

// flac⇔wav: 旧形式のワーカー/係数が残ると壊れる。
// 解析スレッドはここでは起動しない。ResumePlaybackFeed（DS 再生開始後）でのみ起動する。
void CPianoRoll::ResetPlaybackState()
{
    // Pause で busy=0 を待つので、表示状態のクリアはワーカー停止成否に依存しない。
    PauseAnalysis();

    if (::IsWindow(m_hWnd)) {
        MSG msg;
        while (PeekMessage(&msg, m_hWnd, WM_PIANOROLL_ANALYSIS_DONE, WM_PIANOROLL_ANALYSIS_DONE, PM_REMOVE)) {}
        while (PeekMessage(&msg, m_hWnd, WM_PIANOROLL_SYNC, WM_PIANOROLL_SYNC, PM_REMOVE)) {}
    }
    // Peek 後も必ずクリア（捨てたメッセージのフラグが残ると2曲目以降描画停止）
    InterlockedExchange(&m_syncPosted, 0);
    InterlockedExchange(&m_analysisDonePosted, 0);

    m_chMeterCount = 0;
    for (int i = 0; i < PIANO_METER_CH_MAX; ++i) {
        m_chMeterDb[i] = -60.0f;
        m_chMeterFill[i] = 0.0f;
        m_chMeterAutoPeak[i] = 0.02f;
    }
    m_meterDirty = true;

    if (TryEnterCriticalSection(&m_jobCs)) {
        m_jobMono.clear();
        m_jobFrameCount = 0;
        m_jobSampleRate = 44100;
        LeaveCriticalSection(&m_jobCs);
    }

    // 表示・ノート状態は必ず消す（曲切替で前曲のロールが残らないように）
    auto clearDisplayState = [this]() {
        memset(m_activeKeys, 0, sizeof(m_activeKeys));
        memset(m_noteStrength, 0, sizeof(m_noteStrength));
        memset(m_rawStrengths, 0, sizeof(m_rawStrengths));
        memset(m_prevRawStrengths, 0, sizeof(m_prevRawStrengths));
        memset(m_onsetStrengths, 0, sizeof(m_onsetStrengths));
        memset(m_prevOnsetStrengths, 0, sizeof(m_prevOnsetStrengths));
        memset(m_prevActiveKeys, 0, sizeof(m_prevActiveKeys));
        memset(m_prevNoteStrength, 0, sizeof(m_prevNoteStrength));
        memset(m_noteAgeFrames, 0, sizeof(m_noteAgeFrames));
        memset(m_scoopLatch, 0, sizeof(m_scoopLatch));
        memset(m_exprFlags, 0, sizeof(m_exprFlags));
        memset(m_vibHist, 0, sizeof(m_vibHist));
        memset(m_vibHistCount, 0, sizeof(m_vibHistCount));
        memset(m_smoothedStrengths, 0, sizeof(m_smoothedStrengths));
        memset(m_displayStrengths, 0, sizeof(m_displayStrengths));
        memset(m_displaySmoothed, 0, sizeof(m_displaySmoothed));
        memset(m_consecActive, 0, sizeof(m_consecActive));
        memset(m_consecSilent, 0, sizeof(m_consecSilent));
        memset(m_segmentId, 0, sizeof(m_segmentId));
        memset(m_envPeak, 0, sizeof(m_envPeak));
        memset(m_unpickedFrames, 0, sizeof(m_unpickedFrames));
        memset(m_strengthDipFrames, 0, sizeof(m_strengthDipFrames));
        memset(m_transientHold, 0, sizeof(m_transientHold));
        memset(m_bandMask, 0, sizeof(m_bandMask));
        memset(m_laneStrength, 0, sizeof(m_laneStrength));
        memset(m_prevBandMask, 0, sizeof(m_prevBandMask));
        memset(m_keySnapActive, 0, sizeof(m_keySnapActive));
        memset(m_keySnapBand, 0, sizeof(m_keySnapBand));
        // 曲切替時は音色エンベロープモデルも必ずリセット(前曲の減衰予測を持ち越さない)
        for (int i = 0; i < KEY_COUNT; ++i)
            m_envModel[i].ResetOff();
        memset(m_reattackMark, 0, sizeof(m_reattackMark));
        memset(m_onsetBoostStreak, 0, sizeof(m_onsetBoostStreak));
        memset(m_harmonicGhostStreak, 0, sizeof(m_harmonicGhostStreak));
        m_historyDirty = true;
        m_keyDirty = true;
        m_historyCount = 0;
        m_historyHead = 0;
        m_framesPending = 0;
        m_rollSpeedCredit = 0;
        m_rollScrollValid = false;
        m_rollReady = false;
        m_bufwav3LevelDb = -60.0f;
        for (int hi = 0; hi < (int)MAX_HISTORY; ++hi) {
            auto& f = m_historyRing[hi];
            memset(f.active, 0, sizeof(f.active));
            memset(f.strength, 0, sizeof(f.strength));
            memset(f.segment, 0, sizeof(f.segment));
            memset(f.bandMask, 0, sizeof(f.bandMask));
            memset(f.laneStrength, 0, sizeof(f.laneStrength));
            memset(f.expr, 0, sizeof(f.expr));
            memset(f.dynLevel, 0, sizeof(f.dynLevel));
            memset(f.reattack, 0, sizeof(f.reattack));
        }
        };

    bool gotCs = false;
    for (int attempt = 0; attempt < 50; ++attempt) {
        if (TryEnterCriticalSection(&m_cs)) {
            gotCs = true;
            break;
        }
        Sleep(1);
    }
    if (gotCs) {
        clearDisplayState();
        LeaveCriticalSection(&m_cs);
    }
    else {
        // CS が取れなくても表示用メンバは UI スレッド専用なので消す
        clearDisplayState();
    }

    StopAnalysisWorker();
    InterlockedExchange(&m_analysisBusy, 0);
    InterlockedExchange(&m_jobPending, 0);
    InterlockedExchange(&m_syncPosted, 0);

    // ワーカー完全停止後だけ解析テーブルを破棄（稼働中に触ると UAF）
    bool workerGone = (m_hAnalysisThread == NULL);
    if (!workerGone) {
        const DWORD wr = WaitForSingleObject(m_hAnalysisThread, 0);
        if (wr == WAIT_OBJECT_0) {
            CloseHandle(m_hAnalysisThread);
            m_hAnalysisThread = NULL;
            if (m_hAnalysisWake) {
                CloseHandle(m_hAnalysisWake);
                m_hAnalysisWake = NULL;
            }
            InterlockedExchange(&m_workerStop, 0);
            workerGone = true;
        }
    }

    if (workerGone && TryEnterCriticalSection(&m_cs)) {
        m_ringWrite = 0;
        m_ringCount = 0;
        m_samplesSinceAnalyze = 0;
        m_playbackDelaySamples = 0;
        m_lastAnalyzeTick = 0;
        m_inputSampleRate = 0;
        m_winLow = m_winBass = m_winHigh = m_winOnset = 0;
        m_goertzelCoeffs.clear();
        m_hannLow.clear();
        m_hannBass.clear();
        m_hannOnset.clear();
        m_blackmanHigh.clear();
        m_windowedLow.clear();
        m_windowedBass.clear();
        m_windowedHigh.clear();
        m_windowedOnset.clear();
        m_analysisBuf.clear();
        m_bassAnalysisBuf.clear();
        m_workerMonoScratch.clear();
        m_analysisHasBass = false;
        if (!m_ring.empty())
            std::fill(m_ring.begin(), m_ring.end(), 0.0);
        LeaveCriticalSection(&m_cs);
    }
    else {
        // ワーカー生存時もスロットルだけリセット（次曲の解析投入を止めない）
        m_lastAnalyzeTick = 0;
    }

    // 前曲の絵が残ったまま止まって見えないよう再描画を要求
    if (::IsWindow(m_hWnd))
        Invalidate(FALSE);
}

void CPianoRoll::ResumePlaybackFeed()
{
    if (!::IsWindow(m_hWnd) || m_paintDisabled)
        return;
    if (!EnsureAnalysisWorkerAlive())
        return;
    m_feedEnabled = true;
}

void CPianoRoll::DoDataExchange(CDataExchange* pDX)
{
    CCustomBlurDialogExBase::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CPianoRoll, CCustomBlurDialogExBase)
    ON_WM_PAINT()
    ON_WM_TIMER()
    ON_WM_SIZE()
    ON_WM_MOVE()
    ON_WM_SHOWWINDOW()
    ON_WM_CLOSE()
    ON_WM_CONTEXTMENU()
    ON_COMMAND_RANGE(IDM_ROLL_SPEED_BASE, IDM_ROLL_SPEED_BASE + ROLL_SPEED_COUNT - 1, &CPianoRoll::OnRollSpeedCmd)
    ON_COMMAND(IDM_ROLL_FREEZE, &CPianoRoll::OnToggleFreeze)
    ON_COMMAND(IDM_ROLL_CLEAR, &CPianoRoll::OnClearDisplay)
    ON_COMMAND(IDM_ROLL_LEGEND, &CPianoRoll::OnToggleExprLegend)
    ON_COMMAND(IDM_ROLL_EXPR, &CPianoRoll::OnToggleExprMarks)
    ON_COMMAND(IDM_ROLL_METER, &CPianoRoll::OnToggleLevelMeter)
    ON_COMMAND(IDM_ROLL_TOPMOST, &CPianoRoll::OnToggleAlwaysOnTop)
    ON_COMMAND(IDM_ROLL_REATTACK, &CPianoRoll::OnToggleReattackDetect)
    ON_COMMAND(IDM_ROLL_IMPULSE, &CPianoRoll::OnToggleImpulsiveGhost)
    ON_COMMAND(IDM_ROLL_HARM_GHOST, &CPianoRoll::OnToggleHarmonicGhost)
    ON_COMMAND(IDM_ROLL_HARM_PROF, &CPianoRoll::OnToggleHarmonicProfile)
    ON_COMMAND(IDM_ROLL_TUNE, &CPianoRoll::OnOpenTuneDialog)
    ON_MESSAGE(WM_PIANOROLL_SYNC, &CPianoRoll::OnSyncRequest)
    ON_MESSAGE(WM_PIANOROLL_ANALYSIS_DONE, &CPianoRoll::OnAnalysisDone)
END_MESSAGE_MAP()

BOOL CPianoRoll::OnInitDialog()
{
    CCustomBlurDialogExBase::OnInitDialog();
    SetWindowText(LL14(
        L"簡易ピアノロール", L"Simple Piano Roll", L"Rouleau piano simple", L"Rotolo pianoforte semplice",
        L"Rollo de piano simple", L"간이 피아노 롤", L"简易钢琴卷帘", L"لوحة بيانو بسيطة",
        L"Простой пианоролл", L"Einfache Klavierrolle", L"Rolo de piano simples", L"Eenvoudige pianorol",
        L"Prosta rolka pianina", L"Basit piyano rulosu"));
    // [ビルド確認用タグ] この文字列がタイトルバーに出ていれば、この CPianoRoll.cpp が
    // 実際にビルド・実行されている証拠になる。出ていなければ、差し替え忘れ/
    // 別コピーのビルド/キャッシュ等、ファイルが反映されていない問題を疑うこと。
    // 動作確認が済んだら削除して構わない。
#if 0
    {
        CString curTitle;
        GetWindowText(curTitle);
        curTitle += L" [PR-DBG-v7]";
        SetWindowText(curTitle);
    }
#endif
    ModifyStyle(WS_MINIMIZEBOX, 0);
    SetIcon(nullptr, TRUE);
    SetIcon(nullptr, FALSE);
    // キャプションアイコンは付けない。WM_SETICON(NULL) だけでは DWM が
    // 既定アイコンへフォールバックするため、Aero 有効時も常に
    // WS_EX_DLGMODALFRAME を立ててフレーム再計算する（イコライザーと同じ見た目）。
    ModifyStyleEx(0, WS_EX_DLGMODALFRAME, SWP_FRAMECHANGED);

    {
        int sp = savedata.pianorollscrollspeed;
        if (sp < 25 || sp > 200) sp = 100;
        m_rollSpeedPct = sp;
        m_rollSpeedCredit = 0;
    }
    m_showExprLegend = (savedata.pianorollexprlegend != 0);
    m_showExprMarks = (savedata.pianorollexprmarks != 0);
    m_showLevelMeter = (savedata.pianorolllevelmeter != 0);
    m_alwaysOnTop = (savedata.pianorolltopmost != 0);
    m_reattackDetectEnabled = (savedata.pianorollreattack != 0);
    m_impulsiveGhostSuppressEnabled = (savedata.pianorollimpulse != 0);
    m_harmonicGhostGuardEnabled = (savedata.pianorollharmghost != 0);
    m_harmonicProfileGuardEnabled = (savedata.pianorollharmprof != 0);
    m_frozen = false;

    if (savedata.pianorollx != -1)
        SetWindowPos(m_alwaysOnTop ? &CWnd::wndTopMost : &CWnd::wndTop,
            savedata.pianorollx, savedata.pianorolly,
            savedata.pianorollw, savedata.pianorollh,
            SWP_NOOWNERZORDER | (m_alwaysOnTop ? 0 : SWP_NOZORDER));
    else
        SetWindowPos(m_alwaysOnTop ? &CWnd::wndTopMost : &CWnd::wndTop,
            100, 150, 800, 450,
            SWP_NOOWNERZORDER | (m_alwaysOnTop ? 0 : SWP_NOZORDER));

    EnsureAnalysisTables(m_inputSampleRate);
    StartAnalysisWorker();
    UpdatePianoRollTimer();
    m_feedEnabled = true;
    m_paintDisabled = false;
    m_historyDirty = true;
    EnableMainWindowLock(&savedata.pianorollMainLock, TRUE);
    return TRUE;
}

float CPianoRoll::MidiToFreq(int midi)
{
    return 440.0f * powf(2.0f, (midi - 69) / 12.0f);
}

int CPianoRoll::KeyBandIndex(int keyIndex)
{
    return PianoRoll108::KeyBandIndex(keyIndex);
}

double CPianoRoll::ReadMonoSample(const uint8_t* sp, int bits)
{
    switch (bits)
    {
    case 8:
        return (double(*sp) - 128.0) / 128.0;
    case 16: {
        int16_t s16;
        memcpy(&s16, sp, 2);
        return s16 / 32768.0;
    }
    case 24: {
        int32_t s24 = (int32_t(sp[2]) << 16) | (int32_t(sp[1]) << 8) | sp[0];
        if (s24 & 0x800000) s24 |= 0xFF000000;
        return s24 / 8388608.0;
    }
    case 32: {
        int32_t s32;
        memcpy(&s32, sp, 4);
        return s32 / 2147483648.0;
    }
    default:
        return 0.0;
    }
}

// Goertzel 係数と各窓関数をサンプルレートに合わせて計算/再計算する。
// サンプルレートが変化しなければキャッシュを流用するため低コスト。
// FeedPCM の EnterCriticalSection 内から呼ばれる。
void CPianoRoll::EnsureAnalysisTables(int sampleRate, int capCaptureFrames)
{
    if (sampleRate < 8000) sampleRate = 44100;
    const int cap = (capCaptureFrames > 0) ? capCaptureFrames : 0;
    const int winLow = ScaleWinSamples(WIN_LOW_REF, sampleRate, cap);
    int winBass = ScaleWinSamples(WIN_BASS_REF, sampleRate, cap);
    int winHigh = ScaleWinSamples(WIN_HIGH_REF, sampleRate, cap);
    int winOnset = ScaleWinSamples(WIN_ONSET_REF, sampleRate, cap);
    if (winBass < winLow) winBass = winLow;
    if (winHigh > winLow) winHigh = winLow;
    if (winOnset > winLow) winOnset = winLow;
    if (sampleRate == m_inputSampleRate &&
        winLow == m_winLow && winBass == m_winBass &&
        winHigh == m_winHigh && winOnset == m_winOnset &&
        !m_goertzelCoeffs.empty())
        return;

    m_inputSampleRate = sampleRate;
    m_winLow = winLow;
    m_winBass = winBass;
    m_winHigh = winHigh;
    m_winOnset = winOnset;
    m_goertzelCoeffs.resize(KEY_COUNT);
    for (int i = 0; i < KEY_COUNT; ++i) {
        const int midi = MIDI_BASE + i;
        const double freq = MidiToFreq(midi);
        m_goertzelCoeffs[i] = 2.0 * cos(2.0 * M_PI * freq / sampleRate);
    }

    m_hannLow.resize(m_winLow);
    for (int n = 0; n < m_winLow; ++n) {
        const double denom = (m_winLow > 1) ? (double)(m_winLow - 1) : 1.0;
        m_hannLow[n] = 0.5 - 0.5 * cos(2.0 * M_PI * n / denom);
    }

    m_hannOnset.resize(m_winOnset);
    for (int n = 0; n < m_winOnset; ++n) {
        const double denom = (m_winOnset > 1) ? (double)(m_winOnset - 1) : 1.0;
        m_hannOnset[n] = 0.5 - 0.5 * cos(2.0 * M_PI * n / denom);
    }

    m_hannBass.resize(m_winBass);
    for (int n = 0; n < m_winBass; ++n) {
        const double denom = (m_winBass > 1) ? (double)(m_winBass - 1) : 1.0;
        m_hannBass[n] = 0.5 - 0.5 * cos(2.0 * M_PI * n / denom);
    }

    m_blackmanHigh.resize(m_winHigh);
    for (int n = 0; n < m_winHigh; ++n) {
        const double denom = (m_winHigh > 1) ? (double)(m_winHigh - 1) : 1.0;
        m_blackmanHigh[n] = 0.42 - 0.5 * cos(2.0 * M_PI * n / denom)
            + 0.08 * cos(4.0 * M_PI * n / denom);
    }

    m_windowedLow.assign(m_winLow, 0.0);
    m_windowedBass.assign(m_winBass, 0.0);
    m_windowedHigh.assign(m_winHigh, 0.0);
    m_windowedOnset.assign(m_winOnset, 0.0);
    m_analysisBuf.assign(m_winLow, 0.0);
    m_bassAnalysisBuf.assign(m_winBass, 0.0);
}

// Goertzel アルゴリズムで単一周波数の振幅(magnitude)を計算する。
// 係数 coefficient = 2*cos(2π*f/sr) は EnsureAnalysisTables で事前計算済み。
// window が非 null なら掛け算でサイドローブを抑制する(Hann, Blackman 等)。
// 戻り値は numSamples で正規化した振幅(0.0〜)。FFT 全帯域ではなく対象周波数だけ
// 計算するため 88 鍵 × O(N) の計算量で済む(FFT の O(N log N) より有利な用途)。
double CPianoRoll::GoertzelMagnitude(const double* samples, int numSamples,
    double coefficient, const double* window)
{
    double s_prev = 0.0, s_prev2 = 0.0;
    for (int n = 0; n < numSamples; ++n) {
        const double x = window ? (samples[n] * window[n]) : samples[n];
        const double s = x + coefficient * s_prev - s_prev2;
        s_prev2 = s_prev;
        s_prev = s;
    }
    const double power = s_prev2 * s_prev2 + s_prev * s_prev - coefficient * s_prev * s_prev2;
    return sqrt(power > 0.0 ? power : 0.0) * 2.5 / numSamples;
}

float CPianoRoll::ApplyDisplayScale(float rawAmp, int keyIndex, int winSamples, int refWinSamples)
{
    if (rawAmp <= 1e-10f) return 0.0f;
    if (winSamples <= 0) winSamples = refWinSamples;
    if (refWinSamples <= 0) refWinSamples = WIN_LOW_REF;

    float amp = (float)rawAmp * ((float)winSamples / (float)refWinSamples);

    const float hz = PianoKey::KeyHz(keyIndex);
    float eq = sqrtf(440.0f / (hz > 20.0f ? hz : 20.0f));
    float eqMin = 0.60f;
    float eqMax = 1.8f;
    if (keyIndex < PianoRoll108::BASS_END)
        eqMax = 1.05f;
    else if (keyIndex < 12)
        eqMax = 1.6f;
    else if (keyIndex >= 84)
        eqMin = 0.72f;
    else if (keyIndex >= 72)
        eqMin = 0.65f;
    if (eq < eqMin) eq = eqMin;
    if (eq > eqMax) eq = eqMax;
    amp *= eq;

    const double x = (double)amp * 80.0;
    double out = x * x * 0.003;
    if (out > 10.0) out = 10.0;
    return (float)out;
}

float CPianoRoll::ApplyDetectScale(float rawAmp, int winSamples, int refWinSamples)
{
    if (rawAmp <= 1e-10f) return 0.0f;
    if (winSamples <= 0) winSamples = refWinSamples;
    if (refWinSamples <= 0) refWinSamples = WIN_LOW_REF;
    const float amp = (float)rawAmp * ((float)winSamples / (float)refWinSamples);
    return ScaleGoertzelAmpFlat(amp);
}




// 再生スレッドからデコード済み PCM を受け取り、モノラル double に変換して
// リングバッファへ書き込む。m_cs で保護されているためスレッドセーフ。
// ResetPlaybackState 後は m_feedEnabled=true に戻すまで書き込まれない。
void CPianoRoll::FeedPCM(const void* pData, int frames,
    int sampleRate, int bits, int channels, int playbackDelaySamples)
{
    // SyncPianoRollFromPlayCursor → AnalyzePlayCursorMono が解析の正本。
    // リングは誰も読まないのに再生スレッドが毎サンプル m_cs で書いており、
    // 解析/UI と争奪して滞留の一因になるため書き込みを停止する。
    (void)pData; (void)frames; (void)sampleRate; (void)bits;
    (void)channels; (void)playbackDelaySamples;
}

// bufwav3 の再生バッファ直後から呼ばれる。mono は既にモノラル変換済み。
// ANALYZE_MIN_MS(4ms)のスロットリングでワーカーを過負荷から守る。
// ジョブバッファ(m_jobMono)へコピーして SetEvent でワーカーを起こす。
// 前のジョブが完了していない場合も InterlockedExchange で上書きする
// (古い分析より最新フレームを優先する)。
void CPianoRoll::AnalyzePlayCursorMono(const double* mono, int frameCount, int sampleRate)
{
    if (!m_feedEnabled) return;
    if (!mono || frameCount < MinAnalyzeFrameCount(sampleRate, frameCount) || sampleRate < 8000) return;
    // 形式切替後にワーカーが死んだまま/stop のまま残ると解析が永久に止まる
    if (!EnsureAnalysisWorkerAlive()) return;
    if (!m_hAnalysisWake) return;

    const DWORD now = GetTickCount();
    if (m_lastAnalyzeTick != 0 && (now - m_lastAnalyzeTick) < ANALYZE_MIN_MS)
        return;
    m_lastAnalyzeTick = now;

    EnterCriticalSection(&m_jobCs);
    const int copyFrames = frameCount;
    if ((int)m_jobMono.size() < copyFrames)
        m_jobMono.resize((size_t)copyFrames);
    memcpy(m_jobMono.data(), mono, (size_t)copyFrames * sizeof(double));
    m_jobFrameCount = copyFrames;
    m_jobSampleRate = sampleRate;
    InterlockedExchange(&m_jobPending, 1);
    LeaveCriticalSection(&m_jobCs);
    SetEvent(m_hAnalysisWake);
}

void CPianoRoll::SetChannelMeterDb(const float* dbPerChannel, int channelCount)
{
    // UI スレッド専用。m_cs(Goertzel)を取ると解析中にメーターが遅延する。
    static constexpr float kPeakDecay = 0.994f;
    static constexpr float kFillAttack = 0.55f;
    static constexpr float kFillRelease = 0.18f;

    m_chMeterCount = channelCount;
    if (m_chMeterCount < 0) m_chMeterCount = 0;
    if (m_chMeterCount > PIANO_METER_CH_MAX) m_chMeterCount = PIANO_METER_CH_MAX;
    bool meterChanged = false;
    for (int i = 0; i < PIANO_METER_CH_MAX; ++i) {
        if (i < m_chMeterCount && dbPerChannel) {
            const float in = dbPerChannel[i];
            m_chMeterDb[i] = in;
            float lin = (in <= -59.0f) ? 0.0f : powf(10.0f, in / 20.0f);

            float& peak = m_chMeterAutoPeak[i];
            if (lin > peak)
                peak = lin;
            else
                peak = peak * kPeakDecay + lin * (1.0f - kPeakDecay);
            if (peak < 0.002f) peak = 0.002f;

            float norm = lin / peak;
            if (norm < 0.0f) norm = 0.0f;
            if (norm > 1.0f) norm = 1.0f;

            float& fill = m_chMeterFill[i];
            const float prevFill = fill;
            const float rate = (norm >= fill) ? kFillAttack : kFillRelease;
            fill += (norm - fill) * rate;
            if (!meterChanged && fabsf(fill - prevFill) > 0.02f)
                meterChanged = true;
        }
        else {
            const float prevFill = m_chMeterFill[i];
            m_chMeterDb[i] = -60.0f;
            m_chMeterFill[i] *= 0.85f;
            m_chMeterAutoPeak[i] = 0.02f;
            if (!meterChanged && m_chMeterFill[i] > 0.01f && prevFill > 0.01f)
                meterChanged = true;
        }
    }
    if (meterChanged)
        m_meterDirty = true;
}

// 窓掛け済みバッファから Goertzel 解析を実行し m_rawStrengths を更新する。
// 108鍵: 低〜中は 8192、高は 4096（低音だけ長窓にすると時間軸がずれるため統一）
void CPianoRoll::RunGoertzelFromBuffer(const double* winLow,
    const double* winBass, int bassWinLen)
{
    if (!winLow) return;

    for (int i = 0; i < m_winLow; ++i)
        m_analysisBuf[i] = winLow[i];

    const bool hasBass = (winBass && bassWinLen >= m_winBass);
    m_analysisHasBass = hasBass;
    if (hasBass) {
        for (int i = 0; i < m_winBass; ++i)
            m_bassAnalysisBuf[i] = winBass[i];
    }

    const float levelDb = Cfg::LevelDbForDynamics(
        m_analysisBuf.data(), m_winLow,
        hasBass ? m_bassAnalysisBuf.data() : nullptr,
        hasBass ? m_winBass : 0);

    const float gainDb = Cfg::MakeupGainDb(levelDb);
    // 絶対値ノイズフロア／ピック閾値は AGC「後」の動作点を基準にする。
    // 旧: 入力生レベルをそのまま使う → 静かな曲だけ相対閾値が緩み、
    //     マスター上げ後は床が下がってゴーストが増える非対称が残っていた。
    m_lastGainDb = gainDb;
    m_bufwav3LevelDb = levelDb + gainDb;
    Cfg::ApplyGainDbInPlace(m_analysisBuf.data(), m_winLow, gainDb);
    if (hasBass)
        Cfg::ApplyGainDbInPlace(m_bassAnalysisBuf.data(), m_winBass, gainDb);

    // 真の無音のみ早期リターン。AGC可能な静かな曲は通す。
    if (levelDb < -58.0f) {
        for (int i = 0; i < KEY_COUNT; ++i) {
            m_noteStrength[i] = 0.0f;
            m_rawStrengths[i] = 0.0f;
            m_displayStrengths[i] = 0.0f;
            m_onsetStrengths[i] = 0.0f;
            m_smoothedStrengths[i] *= 0.5f;
            m_displaySmoothed[i] *= 0.5f;
            m_consecActive[i] = 0;
            m_consecSilent[i] = 0;
            m_unpickedFrames[i] = 0;
            m_strengthDipFrames[i] = 0;
            m_transientHold[i] = 0;
            m_envPeak[i] = 0.0f;
            // 無音区間は音色エンベロープモデルもオフへ戻す(次の音を新規オンセットとして扱う)
            m_envModel[i].ResetOff();
            m_reattackMark[i] = false;
            m_onsetBoostStreak[i] = 0;
            m_harmonicGhostStreak[i] = 0;
        }
        memcpy(m_prevOnsetStrengths, m_onsetStrengths, sizeof(m_onsetStrengths));
        m_detectSilent = true;
        return;
    }

    m_detectSilent = false;

    for (int i = 0; i < m_winLow; ++i)
        m_windowedLow[i] = m_analysisBuf[i] * m_hannLow[i];
    for (int i = 0; i < m_winHigh; ++i)
        m_windowedHigh[i] = m_analysisBuf[i + (m_winLow - m_winHigh)] * m_blackmanHigh[i];
    const double* onsetSrc = m_analysisBuf.data() + (m_winLow - m_winOnset);
    for (int i = 0; i < m_winOnset; ++i)
        m_windowedOnset[i] = onsetSrc[i] * m_hannOnset[i];

    const int splitLo = PianoRoll108::WIN_LONG_END;
    const int splitHi = PianoRoll108::WIN_MID_END;

    auto storeKey = [this](int i, float goertzel, int winSamples, int refWin) {
        m_rawStrengths[i] = ApplyDetectScale(goertzel, winSamples, refWin);
        m_displayStrengths[i] = ApplyDisplayScale(goertzel, i, winSamples, refWin);
    };

    if (hasBass) {
        for (int i = 0; i < m_winBass; ++i)
            m_windowedBass[i] = m_bassAnalysisBuf[i] * m_hannBass[i];
        PianoRollGoertzelBatchAvx2(
            m_windowedBass.data(), m_winBass, m_goertzelCoeffs.data(),
            0, splitLo, m_goertzelRawScratch);
        for (int i = 0; i < splitLo; ++i)
            storeKey(i, (float)m_goertzelRawScratch[i], m_winBass, WIN_BASS_REF);
    }
    else {
        PianoRollGoertzelBatchAvx2(
            m_windowedLow.data(), m_winLow, m_goertzelCoeffs.data(),
            0, splitLo, m_goertzelRawScratch);
        for (int i = 0; i < splitLo; ++i)
            storeKey(i, (float)m_goertzelRawScratch[i], m_winLow, WIN_BASS_REF);
    }

    PianoRollGoertzelBatchAvx2(
        m_windowedLow.data(), m_winLow, m_goertzelCoeffs.data(),
        splitLo, splitHi, m_goertzelRawScratch);
    for (int i = splitLo; i < splitHi; ++i)
        storeKey(i, (float)m_goertzelRawScratch[i - splitLo], m_winLow, WIN_LOW_REF);

    PianoRollGoertzelBatchAvx2(
        m_windowedHigh.data(), m_winHigh, m_goertzelCoeffs.data(),
        splitHi, KEY_COUNT, m_goertzelRawScratch);
    for (int i = splitHi; i < KEY_COUNT; ++i)
        storeKey(i, (float)m_goertzelRawScratch[i - splitHi], m_winHigh, WIN_HIGH_REF);

    PianoRollGoertzelBatchAvx2(
        m_windowedOnset.data(), m_winOnset, m_goertzelCoeffs.data(),
        0, KEY_COUNT, m_goertzelRawScratch);
    for (int i = 0; i < KEY_COUNT; ++i)
        m_onsetStrengths[i] = ApplyDetectScale(
            (float)m_goertzelRawScratch[i], m_winOnset, WIN_ONSET_REF);

    for (int i = 0; i < KEY_COUNT; ++i) {
        const float alpha = PianoRoll108::IirAlphaForKey(i);
        m_smoothedStrengths[i] =
            m_smoothedStrengths[i] * (1.0f - alpha) + m_rawStrengths[i] * alpha;
        m_displaySmoothed[i] =
            m_displaySmoothed[i] * (1.0f - alpha) + m_displayStrengths[i] * alpha;
    }
    // UpdateNoteStates / PushDisplayFrames は m_cs 下の PublishDetectResults で行う
}

namespace
{
    // 倍音ゴースト抑制用: candidate が「他の音の倍音として際どく通過した」かどうかを判定する。
    // PianoKeyTable.h の PassesFundamentalTest/PassesFundamentalTestSustain と同じ
    // 判定式(下側の潜在的な基音候補との比率が 0.78 を超えたら本来は倍音として棄却)
    // を土台にしているが、目的が異なるため単純化して再現している:
    //   - あちらは「棄却するかどうか」の最終判定(1回だけ実行)
    //   - こちらは「棄却ラインのすぐ近く(僅差)で生き残ったか」だけを見る予備検知
    // 明確に独立している音(下の潜在基音がほとんど鳴っていない)は対象外となり、
    // 実際にゴーストが観測された「閾値をまたいで一瞬だけ通過する」ケースだけを狙う。
    bool IsMarginalFund(const float* blend, int candidate, int count, float marginRatio, float rejectRatio)
    {
        if (!blend || candidate < 0 || candidate >= count) return false;
        const float sc = blend[candidate];
        if (sc <= 0.0f) return false;
        for (int n = PianoKey::HARMONIC_N_MIN; n <= PianoKey::HARMONIC_N_MAX; ++n) {
            const int lo = PianoKey::HarmonicDownKey(candidate, n);
            if (lo < 0 || lo >= count || lo >= candidate) continue;
            const float rejectAt = sc * rejectRatio;
            const float marginAt = rejectAt * marginRatio;
            if (blend[lo] >= marginAt && blend[lo] < rejectAt)
                return true;
        }
        return false;
    }
}

namespace
{
    // 隣接半音間のピック“ホッピング”対策。
    // 実音の周波数が2鍵のちょうど中間に近いと、IsLocalPeakInBand/SnapToLocalMaximum
    // (NoteFundamentalPick.h)の判定がフレームごとに僅差で入れ替わり、同じ1音が
    // 隣接する2鍵の間を毎フレーム飛び移ってしまう。m_activeKeys は鍵ごとに独立して
    // いるため、これがそのまま「隣接する2本のバーが交互にチラつく」症状になる。
    // 前フレームで鳴っていた鍵(prevActive)と、今フレーム僅差で勝った隣の鍵の値が
    // 近い(marginRatio以内)場合は、前フレームの鍵を優先して復活させ、
    // 隣の鍵は降ろすことでホッピングを抑える。
    // NoteFundamentalPick.h 側の判定式自体には一切手を入れない、後付けの安定化。
    void StabilizeBinHop(const float* blend, bool* picked,
        const bool* prevActive, int count, float marginRatio)
    {
        if (!blend || !picked || !prevActive || count <= 0) return;
        for (int i = 0; i < count; ++i) {
            if (!prevActive[i] || picked[i]) continue;
            const float bi = blend[i];
            if (bi <= 0.0f) continue;
            for (int d = -1; d <= 1; d += 2) {
                const int j = i + d;
                if (j < 0 || j >= count || !picked[j]) continue;
                const float bj = blend[j];
                if (bj <= 0.0f) continue;
                if (bi >= bj * marginRatio) {
                    picked[i] = true;
                    picked[j] = false;
                    break;
                }
            }
        }
    }
}

void CPianoRoll::UpdateNoteStates()
{
    using namespace Cfg;

    const float pickScale = PickThreshScaleFromLevelDb(m_bufwav3LevelDb);
    const float silenceAbs = PrTuneF(savedata.prTuneSilencePct, SILENCE_ABS);
    const float bandSilBass = PrTuneF(savedata.prTuneBandSilBassPct, BAND_SILENCE_BASS);
    const float bandSilMid = PrTuneF(savedata.prTuneBandSilMidPct, BAND_SILENCE_MID);
    const float bandSilTre = PrTuneF(savedata.prTuneBandSilTrePct, BAND_SILENCE_TRE);
    const float retriggerRatio = PrTuneF(savedata.prTuneRetrigPct, RETRIGGER_RATIO);
    const float harmGhostMargin = PrTuneF(savedata.prTuneHarmGhostPct, kHarmonicGhostMarginRatio);
    const float harmRejectRatio = PrTuneF(savedata.prTuneHarmRejectPct, 0.78f);
    const float harmProfMin = PrTuneF(savedata.prTuneHarmProfPct, kHarmonicProfileNoiseMinConfidence);
    const float pickBassRel = PrTuneF(savedata.prTunePickBassPct, 0.28f);
    const float pickLowMidRel = PrTuneF(savedata.prTunePickLowMidPct, 0.20f);
    const float pickMelodyRel = PrTuneF(savedata.prTunePickMelodyPct, 0.10f);
    const float pickTreRel = PrTuneF(savedata.prTunePickTrePct, 0.22f);
    const float onsetDeltaScale = PrTuneF(savedata.prTuneOnsetDeltaPct, 1.0f);

    float maxS = 0.0f;
    for (int i = 0; i < KEY_COUNT; ++i)
        if (m_smoothedStrengths[i] > maxS) maxS = m_smoothedStrengths[i];

    float blend[KEY_COUNT];
    PianoRoll108::BuildDetectionSpectrum(m_smoothedStrengths, m_rawStrengths, blend, KEY_COUNT);

    const float bassMax = BandMaxStrength(blend, 0, PianoRoll108::BASS_END);
    const float midMax = BandMaxStrength(blend, PianoRoll108::BASS_END, PianoRoll108::MID_END);
    const float treMax = BandMaxStrength(blend, PianoRoll108::MID_END, KEY_COUNT);
    const bool anyBandLive =
        bassMax >= bandSilBass ||
        midMax >= bandSilMid ||
        treMax >= bandSilTre;

    if (!anyBandLive || maxS < silenceAbs) {
        for (int i = 0; i < KEY_COUNT; ++i) {
            m_activeKeys[i] = false;
            m_noteStrength[i] = 0.0f;
            m_consecActive[i] = 0;
            m_consecSilent[i] = 0;
            m_unpickedFrames[i] = 0;
            m_strengthDipFrames[i] = 0;
            m_transientHold[i] = 0;
            m_envPeak[i] = 0.0f;
            m_bandMask[i] = 0;
            memset(m_laneStrength[i], 0, sizeof(m_laneStrength[i]));
            m_smoothedStrengths[i] *= 0.4f;
            m_displaySmoothed[i] *= 0.4f;
            // 無音区間は音色エンベロープモデルもオフへ戻す
            m_envModel[i].ResetOff();
            m_reattackMark[i] = false;
            m_onsetBoostStreak[i] = 0;
            m_harmonicGhostStreak[i] = 0;
        }
        memcpy(m_prevOnsetStrengths, m_onsetStrengths, sizeof(m_onsetStrengths));
        return;
    }

    // ノイズ床: AGC後の固定基準 + スペクトル相対床。
    // 旧: absFloor = base * gainLinear → ブーストで床が上がり静かな曲が載らず、
    //     マスター上げ(gain≈0)で床が最低になりゴーストが増える。
    // 新: 双方向AGCで動作点を揃えたうえで、相対床で砂粒を抑える。
    // 大きなブースト時のみソースSNR悪化分を控えめに持ち上げる。
    const float baseFloor = PrTuneF(savedata.prTuneAbsFloorPct, ABS_NOISE_FLOOR_BASE);
    float absFloor = baseFloor;
    const float noiseEst = SpectralNoiseEstimate(blend, KEY_COUNT);
    if (noiseEst > 0.0f) {
        const float relFloor = noiseEst * SPECTRAL_NOISE_FLOOR_MUL;
        if (relFloor > absFloor) absFloor = relFloor;
    }
    if (m_lastGainDb > 8.0f) {
        const float extra = powf(10.0f, (m_lastGainDb - 8.0f) * 0.25f / 20.0f);
        const float boostFloor = baseFloor * extra;
        if (boostFloor > absFloor) absFloor = boostFloor;
    }

    bool picked[KEY_COUNT];
    PianoRoll108::BuildFramePicks(blend, picked, KEY_COUNT, pickScale, absFloor,
        m_onsetStrengths, m_prevOnsetStrengths,
        pickBassRel, pickLowMidRel, pickMelodyRel, pickTreRel, onsetDeltaScale);

#ifdef _DEBUG
    // [診断用] BuildFramePicks直後、picked[]そのものが絶対値フロアで
    // 本当に弾かれているかを確認する。ここで picked[i]==false なのに
    // 後段で active=1 になっているなら、原因はホールド系ロジック側にある。
    {
        static DWORD s_dbgPickLogTick = 0;
        const DWORD dbgNow2 = GetTickCount();
        if (dbgNow2 - s_dbgPickLogTick >= 500) {
            for (int i = 0; i < KEY_COUNT; ++i) {
                if (picked[i] && blend[i] < absFloor * 0.5f) {
                    char buf[160];
                    sprintf_s(buf, "[PianoRollDbg][FLOOR-LEAK] key=%d picked=1 blend=%.5f floor=%.5f\n",
                        i, blend[i], absFloor);
                    OutputDebugStringA(buf);
                }
            }
            s_dbgPickLogTick = dbgNow2;
        }
    }
#endif

    // 隣接半音ホッピング対策(実験的、既定で常時有効)。
    // 前フレームでアクティブだった鍵(m_activeKeys、この時点ではまだ前フレームの値)を
    // 基準に、僅差で入れ替わっただけの隣接ピックを元に戻す。
    static constexpr float kAdjacentHoppingMarginRatio = 0.85f;
    StabilizeBinHop(blend, picked, m_activeKeys, KEY_COUNT, kAdjacentHoppingMarginRatio);

    for (int i = 0; i < KEY_COUNT; ++i) {
        const float sigStrength = blend[i];
        bool effective = picked[i];

        if (!effective && m_activeKeys[i]) {
            float holdRatio;
            if (i < BAND_BASS_END)
                holdRatio = PrTuneF(savedata.prTuneHoldBassPct, HOLD_ENV_BASS);
            else if (i < BAND_MID_END)
                holdRatio = PrTuneF(savedata.prTuneHoldMidPct, HOLD_ENV_MID);
            else
                holdRatio = PrTuneF(savedata.prTuneHoldTrePct, HOLD_ENV_TRE);
            if (holdRatio > 0.0f && m_envPeak[i] > 0.001f) {
                if (sigStrength >= m_envPeak[i] * holdRatio)
                    effective = true;
                else if (m_smoothedStrengths[i] >= m_envPeak[i] * holdRatio * 0.82f)
                    effective = true;
            }
        }

        // ホールド延長でもパッシブ倍音を残さない。
        // ただし C4〜C6 のメロディー帯は、ピック側の FinishPicks に任せ、
        // ここでの二次剪定でピアノを再キルしない（パッチ連鎖の主因だった）。
        if (effective &&
            (i < PianoRoll108::C4_KEY || i >= PianoRoll108::O5_HI) &&
            PianoKey::IsHarmonicGhostPartial(blend, i, KEY_COUNT, PianoRoll108::BASS_END)) {
            effective = false;
        }

        if (effective) {
            ++m_consecActive[i];
            m_consecSilent[i] = 0;
            m_unpickedFrames[i] = 0;
            if (sigStrength > m_envPeak[i])
                m_envPeak[i] = sigStrength;
            else
                m_envPeak[i] = m_envPeak[i] * 0.92f + sigStrength * 0.08f;
            m_strengthDipFrames[i] = 0;
        }
        else {
            ++m_consecSilent[i];
            if (!m_activeKeys[i]) {
                if (m_consecSilent[i] >= ATTACK_MISS_GRACE)
                    m_consecActive[i] = 0;
            }
            if (m_activeKeys[i]) {
                ++m_unpickedFrames[i];
                if (m_envPeak[i] > 0.001f &&
                    sigStrength < m_envPeak[i] * retriggerRatio)
                    ++m_strengthDipFrames[i];
                else
                    m_strengthDipFrames[i] = 0;
            }
        }

        const bool onsetBoost = picked[i] &&
            PianoRoll108::OnsetSupportsPick(m_onsetStrengths, m_prevOnsetStrengths, i, pickScale, onsetDeltaScale);
        // 再アタック判定(UpdateEnvelope)は本関数の後段で呼ばれるため、
        // ここで計算済みのオンセット判定結果を保存しておいて使い回す。
        m_onsetBoostThisFrame[i] = onsetBoost;
        if (onsetBoost) {
            if (m_onsetBoostStreak[i] < 255) ++m_onsetBoostStreak[i];
        }
        else {
            m_onsetBoostStreak[i] = 0;
        }
        const int attackNeed = onsetBoost ? 1 : AttackFramesForKey(i);

        bool cur = m_activeKeys[i];
        if (!cur) {
            if (effective && m_consecActive[i] >= attackNeed) {
                bool allowOn = true;
                if (m_harmonicGhostGuardEnabled) {
                    const bool suspect = IsMarginalFund(
                        blend, i, KEY_COUNT, harmGhostMargin, harmRejectRatio);
                    if (suspect) {
                        // [特性ベース判定] 振幅のしきい値では「本物の小さい音」と
                        // 「ゴースト(親音への追従に過ぎない漏れ込み)」は区別できない。
                        // 違いは振る舞いの「形」にある: ゴーストは親音の減衰に
                        // ただ追従するだけで、それ自体のアタック(短窓オンセットの
                        // 立ち上がり)を持たない。本物の音は音量が小さくても、
                        // 自分自身のアタック transient を持つはずである。
                        // そこで振幅の持続フレーム数ではなく、m_onsetBoostStreak
                        // (実測済みの短窓オンセット信号が連続で立っているか)を
                        // 直接の合否基準にする。
                        m_harmonicGhostStreak[i] = m_onsetBoostStreak[i];
                        allowOn = (m_onsetBoostStreak[i] >= kHarmonicGhostConfirmFrames);
                    }
                    else {
                        m_harmonicGhostStreak[i] = 0;
                    }
                }
                if (allowOn && m_harmonicProfileGuardEnabled) {
                    // メロディー帯(C4-C6)はプロファイル追加判定を掛けない。
                    // 未較正テンプレがピアノをノイズ/部分音と誤認し、拾えない主因になっていた。
                    const bool melodyBand =
                        (i >= PianoRoll108::C4_KEY && i < PianoRoll108::O5_HI);
                    if (!melodyBand) {
                        if (HarmonicProfile::LooksLikeNoiseProfile(
                            blend, i, KEY_COUNT, harmProfMin)) {
                            allowOn = false;
                        }
                        else if (HarmonicProfile::LooksLikePartialGhost(blend, i, KEY_COUNT)) {
                            allowOn = false;
                        }
                    }
                }
                if (allowOn) {
                    cur = true;
                    m_consecSilent[i] = 0;
                    ++m_segmentId[i];
                    m_envPeak[i] = sigStrength;
                    m_unpickedFrames[i] = 0;
                    m_strengthDipFrames[i] = 0;
                    m_harmonicGhostStreak[i] = 0;
                }
            }
            else {
                m_harmonicGhostStreak[i] = 0;
            }
        }
        else {
            if (effective) {
                m_unpickedFrames[i] = 0;
                m_strengthDipFrames[i] = 0;
            }
            int gapLimit = VisGapFrames(i);
            if (m_envPeak[i] > 0.12f)
                gapLimit += VIS_GAP_SUSTAIN_BONUS;
            const int releaseLimit = TemporalFrames(i, RELEASE_FRAMES);
            const bool gapDetected =
                m_unpickedFrames[i] >= gapLimit ||
                m_strengthDipFrames[i] >= gapLimit;
            if (gapDetected || m_consecSilent[i] >= releaseLimit) {
                cur = false;
                m_consecActive[i] = 0;
                m_envPeak[i] = 0.0f;
                m_unpickedFrames[i] = 0;
                m_strengthDipFrames[i] = 0;
            }
        }
        m_activeKeys[i] = cur;
    }

#ifdef _DEBUG
    // [診断用] 表示(スクロール描画)ではなく、検出(m_activeKeys)そのものが
    // 実際にどれだけ高頻度で点滅しているかを直接確認するためのログ。
    // 0.5秒間に3回以上オン/オフが切り替わった鍵だけを出力する。
    // DebugView やVisual Studioの出力ウィンドウで確認できる。
    {
        static bool s_dbgPrevActive[KEY_COUNT] = {};
        static int  s_dbgTransitions[KEY_COUNT] = {};
        static DWORD s_dbgLastLogTick = 0;
        for (int i = 0; i < KEY_COUNT; ++i) {
            if (m_activeKeys[i] != s_dbgPrevActive[i]) {
                ++s_dbgTransitions[i];
                s_dbgPrevActive[i] = m_activeKeys[i];
            }
        }
        const DWORD dbgNow = GetTickCount();
        if (dbgNow - s_dbgLastLogTick >= 500) {
            for (int i = 0; i < KEY_COUNT; ++i) {
                if (s_dbgTransitions[i] >= 3) {
                    char buf[160];
                    sprintf_s(buf, "[PianoRollDbg] key=%d transitions/0.5s=%d blend=%.4f envPeak=%.4f active=%d\n",
                        i, s_dbgTransitions[i], blend[i], m_envPeak[i], (int)m_activeKeys[i]);
                    OutputDebugStringA(buf);
                }
                s_dbgTransitions[i] = 0;
            }
            s_dbgLastLogTick = dbgNow;
        }
    }
#endif

    for (int i = 0; i < KEY_COUNT; ++i) {
        if (!m_activeKeys[i]) {
            m_noteStrength[i] = 0.0f;
            continue;
        }
        // 描画強度は display 経路。検出 envPeak はホールド判定専用。
        float disp = m_displaySmoothed[i];
        if (disp <= 0.0f) disp = m_displayStrengths[i];
        if (disp <= 0.0f) disp = m_smoothedStrengths[i];
        if (disp <= 0.0f) disp = m_rawStrengths[i];
        if (disp > 10.0f) disp = 10.0f;
        m_noteStrength[i] = disp;
    }

    for (int i = 0; i < KEY_COUNT; ++i) {
        m_bandMask[i] = 0;
        memset(m_laneStrength[i], 0, sizeof(m_laneStrength[i]));
        if (!m_activeKeys[i]) continue;
        const int band = KeyBandIndex(i);
        m_bandMask[i] = (uint8_t)(1u << band);
        m_laneStrength[i][0] = m_noteStrength[i];
    }

    DetectExpressions();

    // 音色エンベロープ更新 + 再アタック(ゲート連結中の同鍵連打)検出。
    // DetectExpressions() の後で呼ぶこと(exprFlags をリセットせず ACCENT を追加するため)。
    UpdateEnvelope();

    memcpy(m_prevOnsetStrengths, m_onsetStrengths, sizeof(m_onsetStrengths));
}

// 音色エンベロープモデル(NoteEnvelopeModel.h)の更新。
// 各アクティブ鍵につき:
//   1) NoteEnvelope::Update() で「直近の谷からの実測リバウンド量」と
//      「このフレームの短窓オンセット判定(m_onsetBoostThisFrame)」の
//      両方が同時に成立した場合のみ再アタックと判定し、セグメントID
//      (見た目上のノート境界)を進めて ACCENT を立てる。
//      片方だけでは発火しないため、持続音の自然な揺らぎだけでは暴走しない。
//   2) m_impulsiveGhostSuppressEnabled が true の場合のみ、
//      打撃/ノイズ的な減衰形状(LooksImpulsive)と判定された鍵をオフに戻す。
//      既定は無効(opt-in)。誤検出で弱いスタッカートまで消す可能性があるため、
//      効果を確認しながら有効化すること。
void CPianoRoll::UpdateEnvelope()
{
    const float nowMs = (float)GetTickCount();

    for (int i = 0; i < KEY_COUNT; ++i) {
        if (!m_activeKeys[i]) {
            m_envModel[i].ResetOff();
            m_reattackMark[i] = false;
            continue;
        }

        if (!m_reattackDetectEnabled) {
            // 機能自体を無効化している場合でも、状態は前回オフのまま維持し
            // 従来通りの見た目(タイ検出なし)にする。
            m_reattackMark[i] = false;
            continue;
        }

        // 単発フレームのオンセット支持(picked[]のチラつき起因を含む)を除外するため、
        // 連続 kOnsetConfirmFrames フレーム以上支持が続いた場合のみ「本物の攻撃」として扱う。
        const bool confirmedOnset = m_onsetBoostStreak[i] >= kOnsetConfirmFrames;
        const bool reattack = NoteEnvelope::Update(m_envModel[i], nowMs, m_noteStrength[i],
            confirmedOnset);
        m_reattackMark[i] = reattack;
        if (reattack) {
            ++m_segmentId[i];
            m_exprFlags[i] |= NoteExpr::ACCENT;
        }

        if (m_impulsiveGhostSuppressEnabled &&
            NoteEnvelope::LooksImpulsive(m_envModel[i])) {
            // 打撃/ノイズ的な鍵だけをオフへ戻す。他の鍵の状態には触れない。
            m_activeKeys[i] = false;
            m_noteStrength[i] = 0.0f;
            m_envPeak[i] = 0.0f;
            m_bandMask[i] = 0;
            memset(m_laneStrength[i], 0, sizeof(m_laneStrength[i]));
            m_envModel[i].ResetOff();
            m_reattackMark[i] = false;
        }
    }
}



namespace
{
    static bool HistDetectVibrato(const float* hist, int count, float envPeak)
    {
        // 判定が甘いと装飾音やトレモロ・わずかな揺れまでビブラート扱いになるため、
        // 「十分長く・はっきりした周期的振動」のみを通すよう厳しめにする。
        if (count < 12 || envPeak < 0.06f) return false;
        float mean = 0.0f;
        for (int k = 0; k < count; ++k) mean += hist[k];
        mean /= (float)count;
        float minV = mean, maxV = mean;
        int reversals = 0;
        int prevSign = 0;
        // ノイズ床: 平均からの偏差がこの値未満は「揺れていない」とみなす。
        const float dead = 0.05f * envPeak;
        for (int k = 0; k < count; ++k) {
            const float v = hist[k];
            if (v < minV) minV = v;
            if (v > maxV) maxV = v;
            const float det = v - mean;
            const int sign = (det > dead) ? 1 : ((det < -dead) ? -1 : 0);
            if (sign != 0 && prevSign != 0 && sign != prevSign)
                ++reversals;
            if (sign != 0) prevSign = sign;
        }
        const float swing = maxV - minV;
        // 反転4回以上(=おおむね2周期以上)かつ十分な振幅のみビブラートと判定。
        return reversals >= 4 && swing >= envPeak * 0.18f;
    }
}

namespace PianoExpr {
    static constexpr uint8_t ACCENT = 0x01;
    static constexpr uint8_t SCOOP = 0x02;
    static constexpr uint8_t VIBRATO = 0x04;
    static constexpr uint8_t SLIDE = 0x08;
    static constexpr uint8_t FALL = 0x10;
    static constexpr uint8_t SUSTAIN = 0x20;
    static constexpr uint8_t CRESC = 0x40;   // クレッシェンド(持続音が膨らむ)
    static constexpr uint8_t DECRESC = 0x80;   // デクレッシェンド(持続音がしぼむ)
    static constexpr uint8_t ALL_MASK = ACCENT | SCOOP | VIBRATO | SLIDE | FALL | SUSTAIN | CRESC | DECRESC;
}

// UpdateNoteStates の直後に呼ばれ、各アクティブノートへ表現記号フラグを付与する。
// 検出ロジック概要:
//   SCOOP   … 直前フレームで隣のキーがアクティブだった(音程が下から上がってきた)
//   SLIDE   … 上下隣キーからの遷移
//   FALL    … 上隣キーからの遷移(下降消音)
//   ACCENT  … ノートオン直後(age <= 3)に強度が急上昇、または本モジュールの再アタック検出
//   SUSTAIN … 12フレーム以上継続(持続音・ストリングス)
//   VIBRATO … 強度の周期的変動を VIB_HIST_LEN 分の自己相関で検出
void CPianoRoll::DetectExpressions()
{
    using namespace Cfg;
    // C6 以上はスキャッターが多く、遷移記号を付けるとノイズになるので抑える。
    static constexpr int kExprHiStart = PianoRoll108::O5_HI; // 84 = C6

    for (int i = 1; i < KEY_COUNT; ++i) {
        if (m_scoopLatch[i] > 0) --m_scoopLatch[i];
        if (m_activeKeys[i - 1] && i < kExprHiStart)
            m_scoopLatch[i] = 5;
    }

    for (int i = 0; i < KEY_COUNT; ++i) {
        m_exprFlags[i] = 0;
        const bool wasActive = m_prevActiveKeys[i];
        const bool nowActive = m_activeKeys[i];
        const bool hiScatter = (i >= kExprHiStart);

        if (!nowActive) {
            m_noteAgeFrames[i] = 0;
            m_vibHistCount[i] = 0;
            continue;
        }

        if (!wasActive)
            m_noteAgeFrames[i] = 0;
        else if (m_noteAgeFrames[i] < 255)
            ++m_noteAgeFrames[i];

        if (!wasActive && !hiScatter) {
            if (i > 0 && m_scoopLatch[i] >= 2)
                m_exprFlags[i] |= PianoExpr::SCOOP;
            if (i > 0 && m_prevActiveKeys[i - 1])
                m_exprFlags[i] |= PianoExpr::SLIDE;
            if (i + 1 < KEY_COUNT && m_prevActiveKeys[i + 1]) {
                m_exprFlags[i] |= PianoExpr::SLIDE;
                m_exprFlags[i] |= PianoExpr::FALL;
            }
        }

        if (nowActive && m_noteAgeFrames[i] >= 12)
            m_exprFlags[i] |= PianoExpr::SUSTAIN;

        if (!hiScatter && m_noteAgeFrames[i] <= 3) {
            const float prev = m_prevNoteStrength[i];
            const float cur = m_noteStrength[i];
            if (cur - prev > 0.18f || (prev > 0.03f && (cur - prev) / prev > 0.28f))
                m_exprFlags[i] |= PianoExpr::ACCENT;
        }

        if (m_vibHistCount[i] < VIB_HIST_LEN)
            ++m_vibHistCount[i];
        for (int k = 0; k < VIB_HIST_LEN - 1; ++k)
            m_vibHist[i][k] = m_vibHist[i][k + 1];
        m_vibHist[i][VIB_HIST_LEN - 1] = m_noteStrength[i];

        if (m_noteAgeFrames[i] >= 10 && m_vibHistCount[i] >= 12) {
            const int n = min((int)m_vibHistCount[i], VIB_HIST_LEN);
            if (HistDetectVibrato(m_vibHist[i] + (VIB_HIST_LEN - n), n,
                m_envPeak[i] > 0.01f ? m_envPeak[i] : 1.0f))
                m_exprFlags[i] |= PianoExpr::VIBRATO;
        }

        // クレッシェンド/デクレッシェンド: 持続(SUSTAIN)かつビブラートでないとき、
        // 強度履歴の前半と後半の平均差から緩やかな増減を判定する。
        if ((m_exprFlags[i] & PianoExpr::SUSTAIN) && !(m_exprFlags[i] & PianoExpr::VIBRATO)
            && m_noteAgeFrames[i] >= 16 && m_vibHistCount[i] >= VIB_HIST_LEN) {
            const float* h = m_vibHist[i];
            const int half = VIB_HIST_LEN / 2;
            float a = 0.0f, b = 0.0f;
            for (int k = 0; k < half; ++k) a += h[k];
            for (int k = half; k < VIB_HIST_LEN; ++k) b += h[k];
            a /= (float)half; b /= (float)(VIB_HIST_LEN - half);
            const float pk = m_envPeak[i] > 0.01f ? m_envPeak[i] : 1.0f;
            const float diff = b - a;
            if (diff > pk * 0.28f) m_exprFlags[i] |= PianoExpr::CRESC;
            else if (diff < -pk * 0.28f && b > pk * 0.18f) m_exprFlags[i] |= PianoExpr::DECRESC;
        }
    }

    memcpy(m_prevActiveKeys, m_activeKeys, sizeof(m_activeKeys));
    memcpy(m_prevBandMask, m_bandMask, sizeof(m_bandMask));
    for (int i = 0; i < KEY_COUNT; ++i)
        m_prevNoteStrength[i] = m_noteStrength[i];
}

namespace {
    static constexpr COLORREF PIANO_CHROMA_KEY = RGB(20, 20, 20);
}

void CPianoRoll::MarkKeyVisualDirty()
{
    m_keyDirty = true;
    memcpy(m_keySnapActive, m_activeKeys, sizeof(m_keySnapActive));
    memcpy(m_keySnapBand, m_bandMask, sizeof(m_keySnapBand));
    memcpy(m_keySnapExpr, m_exprFlags, sizeof(m_keySnapExpr));
}

void CPianoRoll::InvalidateRegions(bool roll, bool key)
{
    if (m_paintDisabled || !::IsWindow(m_hWnd)) return;
    CRect cr;
    GetClientRect(&cr);
    const int w = cr.Width();
    const int h = cr.Height();
    if (w <= 0 || h <= 0) return;

    int keyH = h * 20 / 100;
    if (keyH < 50) keyH = 50;
    if (keyH > 100) keyH = 100;
    const int rollH = h - keyH;
    if (rollH <= 0) return;

    if (roll && key) {
        CCC_InvalidateRectMinusOverlay(m_hWnd, cr);
        return;
    }
    if (roll)
        CCC_InvalidateRectMinusOverlay(m_hWnd, CRect(0, 0, w, rollH));
    if (key)
        InvalidateRect(CRect(0, rollH, w, h), FALSE);
}

void CPianoRoll::BuildLiveNoteFrame(NoteFrame& frame) const
{
    // 太さは帯域内の表示強度比（低音支配時に中高音バーが消えないよう帯域別に正規化）
    // m_rawStrengths は Goertzel(ロック外)が書くため、ここでは m_cs 下で更新される
    // m_noteStrength を使う（UI と publish のレースを避ける）。
    float bandRawMax[3] = { 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < KEY_COUNT; ++i) {
        if (!m_activeKeys[i]) continue;
        const int band = PianoRoll108::KeyBandIndex(i);
        if (m_noteStrength[i] > bandRawMax[band])
            bandRawMax[band] = m_noteStrength[i];
    }

    for (int i = 0; i < KEY_COUNT; ++i) {
        frame.active[i] = m_activeKeys[i];
        frame.strength[i] = m_noteStrength[i];
        frame.segment[i] = m_segmentId[i];
        frame.bandMask[i] = m_bandMask[i];
        frame.expr[i] = m_exprFlags[i];
        memcpy(frame.laneStrength[i], m_laneStrength[i], sizeof(frame.laneStrength[i]));
        frame.reattack[i] = m_activeKeys[i] && m_reattackMark[i];
        if (m_activeKeys[i]) {
            const int band = PianoRoll108::KeyBandIndex(i);
            const float ref = bandRawMax[band];
            float dyn = (ref > 1e-6f) ? (m_noteStrength[i] / ref) : 0.5f;
            if (dyn < 0.06f) dyn = 0.0f;
            if (dyn > 1.0f) dyn = 1.0f;
            frame.dynLevel[i] = dyn;
        }
        else {
            frame.dynLevel[i] = 0.0f;
        }
    }
}

void CPianoRoll::PushFrame(bool requestUiInvalidate)
{
    if (!m_feedEnabled || m_paintDisabled || m_frozen) return;
    m_historyHead = (m_historyHead + (int)MAX_HISTORY - 1) % (int)MAX_HISTORY;
    BuildLiveNoteFrame(m_historyRing[m_historyHead]);
    if (m_historyCount < (int)MAX_HISTORY)
        ++m_historyCount;
    // 保留フレームは上限を設ける。描画が解析に追いつかない(特にアクリル時)と
    // 無制限に溜まり、OnPaint の追い付き再描画ループが UI スレッドを占有して
    // 他処理(モード切替など)が数十秒固まる原因になる。上限で頭打ちにして
    // 追いつけない分は間引く(可視化なので体感への影響は小さい)。
    if (m_framesPending < 3)
        ++m_framesPending;

    for (int i = 0; i < KEY_COUNT; ++i) {
        if (m_activeKeys[i] != m_keySnapActive[i] ||
            m_exprFlags[i] != m_keySnapExpr[i]) {
            MarkKeyVisualDirty();
            break;
        }
    }

    if (requestUiInvalidate)
        InvalidateRegions(true, false);
}

void CPianoRoll::PushDisplayFrames()
{
    // フリーズ中は履歴スクロールを止め、鍵盤/ライブ行だけ最新化する。
    if (m_frozen) {
        for (int i = 0; i < KEY_COUNT; ++i) {
            if (m_activeKeys[i] != m_keySnapActive[i] ||
                m_bandMask[i] != m_keySnapBand[i] ||
                m_exprFlags[i] != m_keySnapExpr[i]) {
                MarkKeyVisualDirty();
                break;
            }
        }
        return;
    }
    // 解析は毎ホップ実行済み。表示速度だけ変える:
    //   100% → 1行/解析、200% → 2行、50% → 2解析に1行。
    int pct = m_rollSpeedPct;
    if (pct < 25) pct = 25;
    if (pct > 200) pct = 200;
    m_rollSpeedCredit += pct;
    int pushed = 0;
    while (m_rollSpeedCredit >= 100 && pushed < 4) {
        m_rollSpeedCredit -= 100;
        PushFrame(false);
        ++pushed;
    }
}

void CPianoRoll::SetRollSpeedPct(int pct)
{
    int nearest = 100;
    int best = 100000;
    for (int i = 0; i < ROLL_SPEED_COUNT; ++i) {
        const int d = abs(kRollSpeedPct[i] - pct);
        if (d < best) { best = d; nearest = kRollSpeedPct[i]; }
    }
    if (m_rollSpeedPct == nearest) return;
    m_rollSpeedPct = nearest;
    savedata.pianorollscrollspeed = nearest;
    m_rollSpeedCredit = 0;
}

int CPianoRoll::RollSpeedIndex() const
{
    for (int i = 0; i < ROLL_SPEED_COUNT; ++i) {
        if (kRollSpeedPct[i] == m_rollSpeedPct) return i;
    }
    return 3; // 100%
}

void CPianoRoll::OnRollSpeedCmd(UINT nID)
{
    const int idx = (int)nID - (int)IDM_ROLL_SPEED_BASE;
    if (idx < 0 || idx >= ROLL_SPEED_COUNT) return;
    SetRollSpeedPct(kRollSpeedPct[idx]);
}

void CPianoRoll::RequestFullRollRedraw()
{
    m_historyDirty = true;
    m_rollScrollValid = false;
    m_framesPending = 0;
    m_keyDirty = true;
    if (::IsWindow(m_hWnd))
        InvalidateRegions(true, true);
}

void CPianoRoll::ClearRollHistory()
{
    m_historyCount = 0;
    m_historyHead = 0;
    m_rollSpeedCredit = 0;
    m_framesPending = 0;
    for (int hi = 0; hi < (int)MAX_HISTORY; ++hi) {
        auto& f = m_historyRing[hi];
        memset(f.active, 0, sizeof(f.active));
        memset(f.strength, 0, sizeof(f.strength));
        memset(f.segment, 0, sizeof(f.segment));
        memset(f.bandMask, 0, sizeof(f.bandMask));
        memset(f.laneStrength, 0, sizeof(f.laneStrength));
        memset(f.expr, 0, sizeof(f.expr));
        memset(f.dynLevel, 0, sizeof(f.dynLevel));
        memset(f.reattack, 0, sizeof(f.reattack));
    }
    RequestFullRollRedraw();
}

void CPianoRoll::OnToggleFreeze()
{
    m_frozen = !m_frozen;
    if (::IsWindow(m_hWnd))
        Invalidate(FALSE);
}

void CPianoRoll::OnClearDisplay()
{
    ClearRollHistory();
}

void CPianoRoll::OnToggleExprLegend()
{
    m_showExprLegend = !m_showExprLegend;
    savedata.pianorollexprlegend = m_showExprLegend ? 1 : 0;
    if (::IsWindow(m_hWnd))
        Invalidate(FALSE);
}

void CPianoRoll::OnToggleExprMarks()
{
    m_showExprMarks = !m_showExprMarks;
    savedata.pianorollexprmarks = m_showExprMarks ? 1 : 0;
    RequestFullRollRedraw();
}

void CPianoRoll::OnToggleLevelMeter()
{
    m_showLevelMeter = !m_showLevelMeter;
    savedata.pianorolllevelmeter = m_showLevelMeter ? 1 : 0;
    m_keyDirty = true;
    if (::IsWindow(m_hWnd))
        Invalidate(FALSE);
}

void CPianoRoll::OnToggleAlwaysOnTop()
{
    m_alwaysOnTop = !m_alwaysOnTop;
    savedata.pianorolltopmost = m_alwaysOnTop ? 1 : 0;
    if (::IsWindow(m_hWnd)) {
        SetWindowPos(m_alwaysOnTop ? &CWnd::wndTopMost : &CWnd::wndNoTopMost,
            0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void CPianoRoll::OnToggleReattackDetect()
{
    m_reattackDetectEnabled = !m_reattackDetectEnabled;
    savedata.pianorollreattack = m_reattackDetectEnabled ? 1 : 0;
}

void CPianoRoll::OnToggleImpulsiveGhost()
{
    m_impulsiveGhostSuppressEnabled = !m_impulsiveGhostSuppressEnabled;
    savedata.pianorollimpulse = m_impulsiveGhostSuppressEnabled ? 1 : 0;
}

void CPianoRoll::OnToggleHarmonicGhost()
{
    m_harmonicGhostGuardEnabled = !m_harmonicGhostGuardEnabled;
    savedata.pianorollharmghost = m_harmonicGhostGuardEnabled ? 1 : 0;
}

void CPianoRoll::OnToggleHarmonicProfile()
{
    m_harmonicProfileGuardEnabled = !m_harmonicProfileGuardEnabled;
    savedata.pianorollharmprof = m_harmonicProfileGuardEnabled ? 1 : 0;
}

void CPianoRoll::OnOpenTuneDialog()
{
    COggDlg_ShowPianoRollTune();
}

void CPianoRoll::OnContextMenu(CWnd* /*pWnd*/, CPoint point)
{
    CMenu menu;
    menu.CreatePopupMenu();

    CMenu subSpeed;
    subSpeed.CreatePopupMenu();
    const int speedIdx = RollSpeedIndex();
    for (int i = 0; i < ROLL_SPEED_COUNT; ++i) {
        CString lab;
        lab.Format(_T("x%.2f"), (double)kRollSpeedPct[i] / 100.0);
        subSpeed.AppendMenu(MF_STRING | (i == speedIdx ? MF_CHECKED : 0),
            IDM_ROLL_SPEED_BASE + i, lab);
    }
    menu.AppendMenu(MF_POPUP, (UINT_PTR)subSpeed.Detach(),
        LL14(L"表示の流れる速度", L"Display scroll speed", L"Vitesse de defilement", L"Velocita scorrimento", L"Velocidad de desplazamiento", L"표시 스크롤 속도", L"显示滚动速度", L"سرعة التمرير", L"Скорость прокрутки", L"Anzeigegeschwindigkeit", L"Velocidade de rolagem", L"Weergavesnelheid", L"Predkosc przewijania", L"Goruntuleme hizi"));

    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING | (m_showExprLegend ? MF_CHECKED : 0), IDM_ROLL_LEGEND,
        LL14(L"記号の凡例", L"Symbol legend", L"Legende des symboles", L"Legenda simboli", L"Leyenda de simbolos", L"기호 범례", L"符号图例", L"دليل الرموز", L"Легенда символов", L"Symbollegende", L"Legenda de simbolos", L"Symbollegenda", L"Legenda symboli", L"Sembol aciklamasi"));
    menu.AppendMenu(MF_STRING | (m_showExprMarks ? MF_CHECKED : 0), IDM_ROLL_EXPR,
        LL14(L"表現記号を表示", L"Show expression marks", L"Afficher les symboles d'expression", L"Mostra simboli espressivi", L"Mostrar simbolos de expresion", L"표현 기호 표시", L"显示奏法记号", L"إظهار رموز التعبير", L"Показывать знаки экспрессии", L"Ausdruckszeichen anzeigen", L"Mostrar simbolos de expressao", L"Expressietekens tonen", L"Pokazuj znaki ekspresji", L"Ifade isaretlerini goster"));
    menu.AppendMenu(MF_STRING | (m_showLevelMeter ? MF_CHECKED : 0), IDM_ROLL_METER,
        LL14(L"レベルメーター", L"Level meter", L"Indicateur de niveau", L"Misuratore di livello", L"Medidor de nivel", L"레벨 미터", L"电平表", L"مقياس المستوى", L"Уровень сигнала", L"Pegelanzeige", L"Medidor de nivel", L"Niveaumeter", L"Miernik poziomu", L"Seviye olcer"));
    menu.AppendMenu(MF_STRING | (m_alwaysOnTop ? MF_CHECKED : 0), IDM_ROLL_TOPMOST,
        LL14(L"常に手前に表示", L"Always on top", L"Toujours au premier plan", L"Sempre in primo piano", L"Siempre visible", L"항상 위에 표시", L"始终置顶", L"دائما في المقدمة", L"Поверх всех окон", L"Immer im Vordergrund", L"Sempre no topo", L"Altijd op voorgrond", L"Zawsze na wierzchu", L"Her zaman ustte"));

    CMenu subDetect;
    subDetect.CreatePopupMenu();
    subDetect.AppendMenu(MF_STRING | (m_reattackDetectEnabled ? MF_CHECKED : 0), IDM_ROLL_REATTACK,
        LL14(L"再アタック検出", L"Re-attack detect", L"Detection de reattaque", L"Rilevamento riattacco", L"Deteccion de reataque", L"리어택 검출", L"再起音检测", L"كشف الهجوم المتكرر", L"Обнаружение реатаки", L"Re-Attack-Erkennung", L"Detectar reataque", L"Her-aanval detectie", L"Wykrywanie reataku", L"Yeniden saldiri algilama"));
    subDetect.AppendMenu(MF_STRING | (m_impulsiveGhostSuppressEnabled ? MF_CHECKED : 0), IDM_ROLL_IMPULSE,
        LL14(L"打撃音ゴースト抑制", L"Impulsive ghost suppress", L"Suppression fantomes impulsifs", L"Soppressione fantasmi impulsivi", L"Supresion de fantasmas impulsivos", L"타격음 고스트 억제", L"打击音幽灵抑制", L"كبح أشباح الإيقاع", L"Подавление импульсных призраков", L"Impulsiv-Geister unterdrucken", L"Suprimir fantasmas impulsivos", L"Impulsieve spoken dempen", L"Tlumienie duchow impulsywnych", L"Vurus hayaletini bastir"));
    subDetect.AppendMenu(MF_STRING | (m_harmonicGhostGuardEnabled ? MF_CHECKED : 0), IDM_ROLL_HARM_GHOST,
        LL14(L"倍音ゴースト抑制", L"Harmonic ghost suppress", L"Suppression fantomes harmoniques", L"Soppressione fantasmi armonici", L"Supresion de fantasmas armonicos", L"배음 고스트 억제", L"泛音幽灵抑制", L"كبح أشباح التوافقيات", L"Подавление гармонических призраков", L"Oberton-Geister unterdrucken", L"Suprimir fantasmas harmonicos", L"Harmonische spoken dempen", L"Tlumienie duchow harmonicznych", L"Armonik hayaletini bastir"));
    subDetect.AppendMenu(MF_STRING | (m_harmonicProfileGuardEnabled ? MF_CHECKED : 0), IDM_ROLL_HARM_PROF,
        LL14(L"音色プロファイル判定", L"Timbre profile guard", L"Garde profil de timbre", L"Protezione profilo timbrico", L"Guardia de perfil timbrico", L"음색 프로파일 판정", L"音色轮廓判定", L"حارس ملف الطابع", L"Профиль тембра", L"Klangprofil-Schutz", L"Guarda de perfil timbrico", L"Timbreprofiel-bewaking", L"Ochrona profilu barwy", L"Timbre profil korumasi"));
    menu.AppendMenu(MF_POPUP, (UINT_PTR)subDetect.Detach(),
        LL14(L"検出オプション", L"Detection options", L"Options de detection", L"Opzioni di rilevamento", L"Opciones de deteccion", L"검출 옵션", L"检测选项", L"خيارات الكشف", L"Параметры обнаружения", L"Erkennungsoptionen", L"Opcoes de deteccao", L"Detectie-opties", L"Opcje wykrywania", L"Algilama secenekleri"));

    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING, IDM_ROLL_TUNE,
        LL14(L"検出パラメータ調整...", L"Detection parameter tuning...", L"Regler les parametres...",
            L"Regola parametri rilevamento...", L"Ajustar parametros de deteccion...", L"검출 파라미터 조정...",
            L"检测参数调整...", L"ضبط معلمات الكشف...", L"Настройка параметров...", L"Erkennungsparameter...",
            L"Ajustar parametros...", L"Detectieparameters...", L"Dostosuj parametry...", L"Algilama parametreleri..."));
    menu.AppendMenu(MF_STRING | (m_frozen ? MF_CHECKED : 0), IDM_ROLL_FREEZE,
        LL14(L"フリーズ", L"Freeze", L"Gel", L"Congela", L"Congelar", L"정지", L"冻结", L"تجميد", L"Заморозка", L"Einfrieren", L"Congelar", L"Bevriezen", L"Zamroz", L"Dondur"));
    menu.AppendMenu(MF_STRING, IDM_ROLL_CLEAR,
        LL14(L"表示をクリア", L"Clear display", L"Effacer l'affichage", L"Cancella visualizzazione", L"Borrar pantalla", L"표시 지우기", L"清除显示", L"مسح العرض", L"Очистить экран", L"Anzeige leeren", L"Limpar exibicao", L"Weergave wissen", L"Wyczysc wyswietlacz", L"Goruntuyu temizle"));

    if (point.x == -1 && point.y == -1) {
        CRect rc; GetClientRect(&rc); ClientToScreen(&rc);
        point = CPoint(rc.left + 8, rc.top + 8);
    }
    menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
}

int CPianoRoll::HistoryCountLocked() const
{
    return m_historyCount;
}

const CPianoRoll::NoteFrame& CPianoRoll::HistoryAt(int indexFromNewest) const
{
    static NoteFrame s_empty;
    static bool s_emptyInit = false;
    if (!s_emptyInit) {
        memset(&s_empty, 0, sizeof(s_empty));
        s_emptyInit = true;
    }
    if (indexFromNewest < 0 || indexFromNewest >= m_historyCount)
        return s_empty;
    const int idx = (m_historyHead + indexFromNewest) % (int)MAX_HISTORY;
    return m_historyRing[idx];
}

void CPianoRoll::CopyHistorySnapshot(NoteFrame* out, int maxOut, int& outCount) const
{
    outCount = 0;
    if (!out || maxOut <= 0 || m_historyCount <= 0) return;
    const int n = (m_historyCount < maxOut) ? m_historyCount : maxOut;
    for (int i = 0; i < n; ++i)
        out[i] = HistoryAt(i);
    outCount = n;
}

bool CPianoRoll::IsBlackKey(int midiNote) const
{
    const int r = midiNote % 12;
    return (r == 1 || r == 3 || r == 6 || r == 8 || r == 10);
}

void CPianoRoll::GetChromaticKeyRect(int keyIndex, int width, int& xL, int& xR) const
{
    if (keyIndex < 0) keyIndex = 0;
    if (keyIndex >= KEY_COUNT) keyIndex = KEY_COUNT - 1;
    if (width <= 0) { xL = xR = 0; return; }
    xL = (int)((keyIndex * (float)width) / (float)KEY_COUNT);
    xR = (int)(((keyIndex + 1) * (float)width) / (float)KEY_COUNT);
    if (xR <= xL) xR = xL + 1;
}

int CPianoRoll::GetWhiteKeyIndex(int midiNote) const
{
    int w = 0;
    for (int m = MIDI_BASE; m < midiNote; ++m)
        if (!IsBlackKey(m)) ++w;
    return w;
}

void CPianoRoll::GetWhiteKeyRect52(int midi, int width, int& xL, int& xR) const
{
    if (width <= 0 || IsBlackKey(midi)) { xL = xR = 0; return; }
    const int w = GetWhiteKeyIndex(midi);
    xL = (int)(w * (float)width / (float)WHITE_KEY_COUNT);
    xR = (int)((w + 1) * (float)width / (float)WHITE_KEY_COUNT);
    if (xR <= xL) xR = xL + 1;
}

namespace PianoDraw
{
    static const wchar_t* WhiteKeyLabel(int midi)
    {
        switch (midi % 12) {
        case 0:  return L"C";
        case 2:  return L"D";
        case 4:  return L"E";
        case 5:  return L"F";
        case 7:  return L"G";
        case 9:  return L"A";
        case 11: return L"B";
        default: return nullptr;
        }
    }

    static int MidiOctaveNumber(int midi) { return (midi / 12) - 1; }

    static void LerpRgb(float t, int r0, int g0, int b0, int r1, int g1, int b1, int& r, int& g, int& b)
    {
        r = (int)(r0 + t * (r1 - r0));
        g = (int)(g0 + t * (g1 - g0));
        b = (int)(b0 + t * (b1 - b0));
    }

    static void SampleKeyGradient(float t, int& r, int& g, int& b)
    {
        static const struct { float pos; int r, g, b; } stops[] = {
            { 0.00f,  50, 110, 225 }, // 低音: 青
            { 0.20f,  30, 185, 215 }, // シアン
            { 0.40f,  55, 200,  80 }, // 緑
            { 0.60f, 215, 210,  45 }, // 黄
            { 0.80f, 235, 140,  40 }, // オレンジ
            { 1.00f, 225,  50,  50 }, // 高音: 赤
        };
        if (t <= stops[0].pos) { r = stops[0].r; g = stops[0].g; b = stops[0].b; return; }
        for (int i = 1; i < (int)(sizeof(stops) / sizeof(stops[0])); ++i) {
            if (t > stops[i].pos) continue;
            const float seg = (t - stops[i - 1].pos) / (stops[i].pos - stops[i - 1].pos);
            LerpRgb(seg, stops[i - 1].r, stops[i - 1].g, stops[i - 1].b,
                stops[i].r, stops[i].g, stops[i].b, r, g, b);
            return;
        }
        const int n = (int)(sizeof(stops) / sizeof(stops[0])) - 1;
        r = stops[n].r; g = stops[n].g; b = stops[n].b;
    }

    static COLORREF KeyNoteColorImpl(int keyIndex, float strength, bool blackKey)
    {
        static constexpr int kKeys = PianoKey::COUNT;
        if (keyIndex < 0) keyIndex = 0;
        if (keyIndex >= kKeys) keyIndex = kKeys - 1;
        const float t = (float)keyIndex / (float)(kKeys - 1);
        const float st = min(strength / 3.0f, 1.0f);
        int r, g, b;
        SampleKeyGradient(t, r, g, b);
        if (blackKey) {
            r = min(255, r + 25);
            g = max(0, g - 15);
            b = max(0, b - 10);
        }
        const int dim = (int)((1.0f - st * 0.65f) * 80.0f);
        r = max(0, min(255, r - dim));
        g = max(0, min(255, g - dim));
        b = max(0, min(255, b - dim));
        return RGB(r, g, b);
    }

    static void DrawBevelKey(CDC& dc, CRect rc, COLORREF fill, bool pressed)
    {
        if (rc.Width() <= 1 || rc.Height() <= 1) return;
        if (pressed) rc.OffsetRect(0, min(2, rc.Height() / 5));
        dc.FillSolidRect(&rc, fill);
        const COLORREF topLeft = pressed ? RGB(45, 45, 50) : RGB(255, 255, 255);
        const COLORREF botRight = pressed ? RGB(190, 190, 195) : RGB(110, 110, 115);
        HGDIOBJ oldPen = dc.SelectObject(::GetStockObject(DC_PEN));
        ::SetDCPenColor(dc.GetSafeHdc(), topLeft);
        dc.MoveTo(rc.left, rc.bottom - 1); dc.LineTo(rc.left, rc.top); dc.LineTo(rc.right - 1, rc.top);
        ::SetDCPenColor(dc.GetSafeHdc(), botRight);
        dc.MoveTo(rc.left, rc.bottom - 1); dc.LineTo(rc.right - 1, rc.bottom - 1); dc.LineTo(rc.right - 1, rc.top);
        dc.SelectObject(oldPen);
    }

    static COLORREF LocalKeyColor(int keyIndex, float strength, bool blackKey)
    {
        return KeyNoteColorImpl(keyIndex, strength, blackKey);
    }

    static void DrawLaneFill(CDC& dc, CRect rc, uint8_t bandMask, const float* laneStr,
        int keyIndex, float fallbackStrength, bool blackKey)
    {
        if (rc.Width() <= 0 || rc.Height() <= 0) return;
        int laneCount = 0;
        for (int b = 0; b < 3; ++b) if (bandMask & (1u << b)) ++laneCount;
        if (laneCount <= 0) { dc.FillSolidRect(&rc, LocalKeyColor(keyIndex, fallbackStrength, blackKey)); return; }
        if (laneCount == 1) {
            const float st = laneStr && laneStr[0] > 0.0f ? laneStr[0] : fallbackStrength;
            dc.FillSolidRect(&rc, LocalKeyColor(keyIndex, st, blackKey)); return;
        }
        int slot = 0;
        for (int b = 0; b < 3; ++b) {
            if (!(bandMask & (1u << b))) continue;
            CRect sub = rc;
            const int w = rc.Width();
            sub.left = rc.left + (w * slot) / laneCount; sub.right = rc.left + (w * (slot + 1)) / laneCount;
            if (sub.right <= sub.left) sub.right = sub.left + 1;
            const float st = (laneStr && laneStr[slot] > 0.0f) ? laneStr[slot] : fallbackStrength;
            dc.FillSolidRect(&sub, LocalKeyColor(keyIndex, st, blackKey)); ++slot;
        }
    }

    static void DrawLaneKey(CDC& dc, CRect rc, uint8_t bandMask, const float* laneStr,
        int keyIndex, float fallbackStrength, bool blackKey, bool pressed)
    {
        if (rc.Width() <= 1 || rc.Height() <= 1) return;
        if (pressed) rc.OffsetRect(0, min(2, rc.Height() / 5));
        int laneCount = 0;
        for (int b = 0; b < 3; ++b) if (bandMask & (1u << b)) ++laneCount;
        if (laneCount <= 1) {
            const float st = laneStr && laneStr[0] > 0.0f ? laneStr[0] : fallbackStrength;
            DrawBevelKey(dc, rc, LocalKeyColor(keyIndex, st, blackKey), pressed); return;
        }
        int slot = 0;
        for (int b = 0; b < 3; ++b) {
            if (!(bandMask & (1u << b))) continue;
            CRect sub = rc;
            sub.top = rc.top + (rc.Height() * slot) / laneCount; sub.bottom = rc.top + (rc.Height() * (slot + 1)) / laneCount;
            if (sub.bottom <= sub.top) sub.bottom = sub.top + 1;
            const float st = (laneStr && laneStr[slot] > 0.0f) ? laneStr[slot] : fallbackStrength;
            DrawBevelKey(dc, sub, LocalKeyColor(keyIndex, st, blackKey), pressed); ++slot;
        }
    }

    static CRect DynInsetRect(CRect rc, float dynLevel)
    {
        if (rc.Width() <= 1) return rc;
        if (dynLevel < 0.0f) dynLevel = 0.0f;
        if (dynLevel > 1.0f) dynLevel = 1.0f;
        const int w = rc.Width();
        int shrink = (int)((1.0f - dynLevel) * w * 0.44f);
        if (shrink > w / 2 - 1) shrink = w / 2 - 1;
        if (shrink < 0) shrink = 0;
        rc.left += shrink;
        rc.right -= shrink;
        if (rc.right <= rc.left) rc.right = rc.left + 1;
        return rc;
    }

    static COLORREF ExprColorForFlag(uint8_t flag)
    {
        switch (flag) {
        case PianoExpr::SCOOP:   return RGB(255, 220, 80);
        case PianoExpr::ACCENT:  return RGB(255, 100, 100);
        case PianoExpr::VIBRATO: return RGB(120, 255, 180);
        case PianoExpr::SLIDE:   return RGB(120, 160, 255);
        case PianoExpr::FALL:    return RGB(255, 170, 90);
        case PianoExpr::SUSTAIN: return RGB(190, 190, 210);
        case PianoExpr::CRESC:   return RGB(120, 230, 255);
        case PianoExpr::DECRESC: return RGB(200, 150, 255);
        default:                 return RGB(255, 255, 255);
        }
    }

    static COLORREF ExprPrimaryColor(uint8_t expr)
    {
        if (expr & PianoExpr::SCOOP) return ExprColorForFlag(PianoExpr::SCOOP);
        if (expr & PianoExpr::ACCENT) return ExprColorForFlag(PianoExpr::ACCENT);
        if (expr & PianoExpr::VIBRATO) return ExprColorForFlag(PianoExpr::VIBRATO);
        if (expr & PianoExpr::SLIDE) return ExprColorForFlag(PianoExpr::SLIDE);
        if (expr & PianoExpr::FALL) return ExprColorForFlag(PianoExpr::FALL);
        if (expr & PianoExpr::CRESC) return ExprColorForFlag(PianoExpr::CRESC);
        if (expr & PianoExpr::DECRESC) return ExprColorForFlag(PianoExpr::DECRESC);
        if (expr & PianoExpr::SUSTAIN) return ExprColorForFlag(PianoExpr::SUSTAIN);
        return RGB(255, 255, 255);
    }

    static float ExprDynLevel(uint8_t expr, float dyn)
    {
        if (dyn < 0.0f) dyn = 0.0f;
        if (dyn > 1.0f) dyn = 1.0f;
        if (expr & PianoExpr::ACCENT) dyn = max(dyn, 0.95f);
        if (expr & PianoExpr::VIBRATO) dyn = max(dyn, min(1.0f, dyn + 0.24f));
        if (expr & PianoExpr::SCOOP) dyn = max(dyn, min(1.0f, dyn + 0.14f));
        if (expr & PianoExpr::SLIDE) dyn = max(dyn, min(1.0f, dyn + 0.20f));
        if (expr & PianoExpr::FALL) dyn = max(dyn, min(1.0f, dyn + 0.12f));
        if (expr & PianoExpr::CRESC) dyn = max(dyn, min(1.0f, dyn + 0.16f));
        if (expr & PianoExpr::DECRESC) dyn = max(dyn, min(1.0f, dyn + 0.10f));
        if (expr & PianoExpr::SUSTAIN) dyn = max(dyn, min(1.0f, dyn + 0.08f));
        return dyn;
    }

    static const wchar_t* ExprGlyphForFlag(uint8_t flag)
    {
        switch (flag) {
        case PianoExpr::SCOOP:   return L"\x2197";
        case PianoExpr::ACCENT:  return L"\x25B8";
        case PianoExpr::VIBRATO: return L"~";
        case PianoExpr::SLIDE:   return L"\x2192";
        case PianoExpr::FALL:    return L"\x2198";
        case PianoExpr::SUSTAIN: return L"\x2015";
        case PianoExpr::CRESC:   return L"\x003C";   // '<' クレッシェンド
        case PianoExpr::DECRESC: return L"\x003E";   // '>' デクレッシェンド
        default:                 return L"?";
        }
    }

    static int CountExprFlags(uint8_t expr)
    {
        int n = 0;
        for (int f = 1; f <= (int)PianoExpr::DECRESC; f <<= 1)
            if (expr & (uint8_t)f) ++n;
        return n;
    }

    static void DrawExprBadgePanel(CDC& dc, CRect rc, uint8_t flag, CFont* pFont)
    {
        if (rc.Width() < 4 || rc.Height() < 4 || !pFont) return;
        const COLORREF fg = ExprColorForFlag(flag);
        dc.FillSolidRect(&rc, RGB(14, 14, 20));
        HGDIOBJ oldPen = dc.SelectObject(::GetStockObject(DC_PEN));
        ::SetDCPenColor(dc.GetSafeHdc(), fg);
        dc.MoveTo(rc.left, rc.bottom - 1); dc.LineTo(rc.left, rc.top); dc.LineTo(rc.right - 1, rc.top);
        dc.LineTo(rc.right - 1, rc.bottom - 1); dc.LineTo(rc.left, rc.bottom - 1);
        dc.SelectObject(oldPen);

        CFont* pOld = dc.SelectObject(pFont);
        dc.SetBkMode(TRANSPARENT);
        dc.SetTextColor(fg);
        dc.DrawText(ExprGlyphForFlag(flag), rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        dc.SelectObject(pOld);
    }

    static void DrawExprGlyphOnNote(CDC& dc, CRect rc, uint8_t flag, CFont* pFont)
    {
        if (rc.Width() < 3 || rc.Height() < 3 || !pFont) return;
        const COLORREF fg = ExprColorForFlag(flag);
        CFont* pOld = dc.SelectObject(pFont);
        dc.SetBkMode(TRANSPARENT);
        dc.SetTextColor(RGB(0, 0, 0));
        CRect sh = rc; sh.OffsetRect(1, 1);
        dc.DrawText(ExprGlyphForFlag(flag), sh, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        dc.SetTextColor(fg);
        dc.DrawText(ExprGlyphForFlag(flag), rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        dc.SelectObject(pOld);
    }

    static void DrawExprGlyphsAboveCell(CDC& dc, CRect cell, uint8_t expr,
        int clipTop, int clipBottom, CFont* pSymFont, int rowPitch)
    {
        if (!expr || cell.Width() < 2 || !pSymFont) return;

        const int flagCount = CountExprFlags(expr);
        if (flagCount <= 0) return;

        const int spaceAbove = cell.top - clipTop;
        if (spaceAbove < 5) return;

        const int laneW = cell.Width();
        static const uint8_t kOrder[] = {
            PianoExpr::ACCENT, PianoExpr::SCOOP, PianoExpr::FALL,
            PianoExpr::SLIDE, PianoExpr::VIBRATO, PianoExpr::CRESC,
            PianoExpr::DECRESC, PianoExpr::SUSTAIN
        };

        uint8_t flags[8];
        int nFlags = 0;
        for (uint8_t flag : kOrder) {
            if (expr & flag)
                flags[nFlags++] = flag;
        }
        if (nFlags <= 0) return;

        const int gap = 1;
        const int needW = nFlags * 5 + gap * (nFlags - 1);
        const bool vertical = (nFlags > 1 && laneW < needW);

        // 記号高さもレーン幅・行高に比例して拡大（リサイズで見やすく）。
        int symH = vertical
            ? max(nFlags * 6, min(spaceAbove, nFlags * 9 + 2))
            : max(10, min(spaceAbove, max(laneW + 2, rowPitch + 4)));
        symH = min(symH, spaceAbove);

        CRect sym(cell.left, cell.top - symH, cell.right, cell.top);
        if (sym.bottom > cell.top) sym.bottom = cell.top;
        if (sym.bottom <= sym.top) return;

        if (vertical) {
            const int rowH = max(4, sym.Height() / nFlags);
            int y = sym.top;
            for (int i = 0; i < nFlags; ++i) {
                CRect badge(sym.left, y, sym.right, min(sym.bottom, y + rowH));
                if (badge.bottom > badge.top)
                    DrawExprGlyphOnNote(dc, badge, flags[i], pSymFont);
                y += rowH;
            }
        }
        else {
            int badgeW = (laneW - gap * (nFlags - 1)) / nFlags;
            if (badgeW < 3) badgeW = 3;
            int totalW = badgeW * nFlags + gap * (nFlags - 1);
            if (totalW > laneW) {
                badgeW = max(3, (laneW - gap * (nFlags - 1)) / nFlags);
                totalW = badgeW * nFlags + gap * (nFlags - 1);
            }
            int x0 = sym.left + (laneW - totalW) / 2;
            for (int i = 0; i < nFlags; ++i) {
                CRect badge(x0 + i * (badgeW + gap), sym.top,
                    x0 + i * (badgeW + gap) + badgeW, sym.bottom);
                DrawExprGlyphOnNote(dc, badge, flags[i], pSymFont);
            }
        }
    }

    static void DrawExprSymbolTop(CDC& dc, CRect cell, uint8_t expr,
        int clipTop, int clipBottom, CFont* pSymFont, int rowPitch)
    {
        DrawExprGlyphsAboveCell(dc, cell, expr, clipTop, clipBottom, pSymFont, rowPitch);
    }

    static void DrawVibratoWobble(CDC& dc, CRect rc, COLORREF col)
    {
        if (rc.Height() < 1 || rc.Width() < 2) return;
        const int amp = min(2, max(1, rc.Width() / 3));
        const int xBase = rc.right - 1;
        // SetPixel(1px毎=最も遅いGDI)を廃し、Polylineで一括描画する。
        // 縦に長い場合のスタック確保を避けるため上限を設ける。
        static const int MAXPTS = 4096;
        POINT pts[MAXPTS];
        int nPts = 0;
        for (int y = rc.top; y < rc.bottom && nPts < MAXPTS; ++y) {
            const int off = (int)(sin((y - rc.top) * 0.75) * amp);
            int x = xBase + off;
            if (x < rc.left) x = rc.left;
            if (x > rc.right - 1) x = rc.right - 1;
            pts[nPts].x = x;
            pts[nPts].y = y;
            ++nPts;
        }
        if (nPts < 2) {
            if (nPts == 1) dc.SetPixel(pts[0].x, pts[0].y, col);
            return;
        }
        HGDIOBJ oldPen = dc.SelectObject(::GetStockObject(DC_PEN));
        ::SetDCPenColor(dc.GetSafeHdc(), col);
        dc.Polyline(pts, nPts);
        dc.SelectObject(oldPen);
    }

    static void DrawHistoryNote(CDC& dc, CRect rc, uint8_t bandMask, const float* laneStr,
        int keyIndex, float strength, float dynLevel, uint8_t expr, bool blackKey)
    {
        if (rc.Width() <= 0 || rc.Height() <= 0) return;
        float dyn = dynLevel > 0.0f ? dynLevel : 0.5f;
        dyn = ExprDynLevel(expr, dyn);

        CRect bar = DynInsetRect(rc, dyn);
        if ((expr & PianoExpr::SLIDE) && bar.right < rc.right)
            bar.right = min(rc.right, bar.right + max(1, rc.Width() / 5));

        DrawLaneFill(dc, bar, bandMask, laneStr, keyIndex, strength, blackKey);

        if (expr & PianoExpr::VIBRATO)
            DrawVibratoWobble(dc, bar, ExprPrimaryColor(PianoExpr::VIBRATO));
        if ((expr & PianoExpr::SCOOP) && bar.Width() >= 2)
            dc.FillSolidRect(CRect(bar.left, bar.top, bar.left + max(1, bar.Width() / 3), bar.bottom),
                ExprPrimaryColor(PianoExpr::SCOOP));
        if ((expr & PianoExpr::ACCENT) && bar.Height() >= 1)
            dc.FillSolidRect(CRect(bar.left, bar.top, bar.right, bar.top + 1),
                ExprColorForFlag(PianoExpr::ACCENT));
        if (expr & PianoExpr::SLIDE)
            dc.FillSolidRect(CRect(bar.right - 1, bar.top, bar.right, bar.bottom),
                ExprColorForFlag(PianoExpr::SLIDE));
        if ((expr & PianoExpr::FALL) && bar.Width() >= 2)
            dc.FillSolidRect(CRect(bar.right - max(1, bar.Width() / 3), bar.top, bar.right, bar.bottom),
                ExprColorForFlag(PianoExpr::FALL));
        if ((expr & PianoExpr::SUSTAIN) && bar.Height() >= 3) {
            const int mid = bar.top + bar.Height() / 2;
            dc.FillSolidRect(CRect(bar.left, mid, bar.right, mid + 1),
                ExprColorForFlag(PianoExpr::SUSTAIN));
        }
    }
}

void CPianoRoll::DrawChannelDbBars(CDC& dc, const CRect& rc, const float* chFill, int chCount) const
{
    if (!chFill || chCount <= 0 || rc.Width() < 4 || rc.Height() < 2) return;
    const int n = (chCount > PIANO_METER_CH_MAX) ? PIANO_METER_CH_MAX : chCount;
    CRect inner(rc.left + 2, rc.top + 1, rc.right - 2, rc.bottom - 1);
    if (inner.Width() < 4 || inner.Height() < 2) return;
    dc.FillSolidRect(inner, RGB(100, 100, 106));
    const int labelW = (n <= 2) ? min(14, inner.Width() / 4) : 0;
    const int barLeft = inner.left + labelW;
    const int barWMax = inner.right - barLeft;
    if (barWMax < 2) return;
    for (int c = 0; c < n; ++c) {
        CRect row(inner.left, inner.top, inner.right, inner.bottom);
        row.top = inner.top + (inner.Height() * c) / n;
        row.bottom = inner.top + (inner.Height() * (c + 1)) / n;
        if (c > 0) row.top += 1; if (c + 1 < n) row.bottom -= 1;
        if (row.bottom <= row.top) row.bottom = row.top + 1;
        CRect track(barLeft, row.top, inner.right, row.bottom);
        dc.FillSolidRect(track, RGB(62, 62, 68));
        float fill = chFill[c]; if (fill < 0.0f)fill = 0.0f; if (fill > 1.0f)fill = 1.0f;
        int barW = (int)(barWMax * fill + 0.5f);
        if (fill > 0.02f && barW < 2) barW = 2;
        if (barW > 0) {
            COLORREF col = RGB(70, 175, 95);
            if (n == 2 && c == 1) col = RGB(175, 120, 70);
            else if (n > 2) col = RGB(90 + c * 12, 140, 180 - c * 8);
            dc.FillSolidRect(CRect(track.left, track.top, track.left + barW, track.bottom), col);
        }
        if (labelW > 0 && row.Height() >= 4 && m_fontMeterTag.GetSafeHandle()) {
            CFont* pOld = dc.SelectObject(const_cast<CFont*>(&m_fontMeterTag));
            dc.SetBkMode(TRANSPARENT); dc.SetTextColor(RGB(230, 230, 235));
            CRect lr(row.left, row.top, barLeft, row.bottom);
            const wchar_t* tag = (n == 1) ? L"M" : ((c == 0) ? L"L" : L"R");
            dc.DrawText(tag, -1, lr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            dc.SelectObject(pOld);
        }
    }
}

void CPianoRoll::DetachForDestroy()
{
    KillTimer(1);
    m_feedEnabled = false;
    m_paintDisabled = true;
    InterlockedExchange(&m_syncPosted, 0);
    InterlockedExchange(&m_analysisDonePosted, 0);
    StopAnalysisWorker();
    EnterCriticalSection(&m_cs);
    m_framesPending = 0;
    LeaveCriticalSection(&m_cs);
    ReleasePaintBuffers();
}

void CPianoRoll::ReleasePaintBuffers()
{
    if (m_rollScratchDC.GetSafeHdc()) {
        if (m_rollScratchOldBmp) m_rollScratchDC.SelectObject(m_rollScratchOldBmp);
        m_rollScratchDC.DeleteDC();
    }
    m_rollScratchBmp.DeleteObject();
    m_rollScratchOldBmp = nullptr;

    if (m_rollDC.GetSafeHdc()) {
        if (m_rollOldBmp) m_rollDC.SelectObject(m_rollOldBmp);
        m_rollDC.DeleteDC();
    }
    m_rollBmp.DeleteObject();
    m_rollOldBmp = nullptr;
    m_rollW = 0;
    m_rollH = 0;
    m_rollReady = false;
    m_rollScrollValid = false;

    if (m_keyDC.GetSafeHdc()) {
        if (m_keyOldBmp) m_keyDC.SelectObject(m_keyOldBmp);
        m_keyDC.DeleteDC();
    }
    m_keyBmp.DeleteObject();
    m_keyOldBmp = nullptr;
    m_keyW = 0;
    m_keyH = 0;
    m_keyBufReady = false;

#if CCUSTOM_AERO_SUPPORT
    m_chromaCache.Release();
    m_chromaReady = false;
    m_chromaW = 0;
    m_chromaH = 0;
#endif

    if (m_fontKeyNote.GetSafeHandle()) m_fontKeyNote.DeleteObject();
    if (m_fontKeyOct.GetSafeHandle()) m_fontKeyOct.DeleteObject();
    if (m_fontMeterTag.GetSafeHandle()) m_fontMeterTag.DeleteObject();
    if (m_fontExprSymbol.GetSafeHandle()) m_fontExprSymbol.DeleteObject();
    if (m_fontExprSymbolCompact.GetSafeHandle()) m_fontExprSymbolCompact.DeleteObject();
    if (m_fontExprLegend.GetSafeHandle()) m_fontExprLegend.DeleteObject();
    m_paintFontsReady = false;
    m_fontCacheClientW = 0;
    m_fontCacheKeyH = 0;
    m_fontCacheRollH = 0;

    ReleaseExprLegendCache();
}

bool CPianoRoll::EnsureRollBuffer(CDC& refDC, int width, int rollH)
{
    if (width <= 0 || rollH <= 0) return false;
    if (m_rollW == width && m_rollH == rollH && m_rollDC.GetSafeHdc())
        return true;

    if (m_rollDC.GetSafeHdc()) {
        if (m_rollOldBmp) m_rollDC.SelectObject(m_rollOldBmp);
        m_rollDC.DeleteDC();
    }
    m_rollBmp.DeleteObject();
    m_rollOldBmp = nullptr;

    if (m_rollScratchDC.GetSafeHdc()) {
        if (m_rollScratchOldBmp) m_rollScratchDC.SelectObject(m_rollScratchOldBmp);
        m_rollScratchDC.DeleteDC();
    }
    m_rollScratchBmp.DeleteObject();
    m_rollScratchOldBmp = nullptr;

    if (!m_rollDC.CreateCompatibleDC(&refDC)) return false;
    if (!m_rollScratchDC.CreateCompatibleDC(&refDC)) {
        m_rollDC.DeleteDC();
        return false;
    }
    if (!m_rollBmp.CreateCompatibleBitmap(&refDC, width, rollH)) {
        m_rollScratchDC.DeleteDC();
        m_rollDC.DeleteDC();
        return false;
    }
    if (!m_rollScratchBmp.CreateCompatibleBitmap(&refDC, width, rollH)) {
        m_rollBmp.DeleteObject();
        m_rollScratchDC.DeleteDC();
        m_rollDC.DeleteDC();
        return false;
    }
    m_rollOldBmp = m_rollDC.SelectObject(&m_rollBmp);
    m_rollScratchOldBmp = m_rollScratchDC.SelectObject(&m_rollScratchBmp);
    m_rollW = width;
    m_rollH = rollH;
    m_rollReady = false;
    m_rollScrollValid = false;
    return true;
}

bool CPianoRoll::EnsureKeyBuffer(CDC& refDC, int width, int keySectionH)
{
    if (width <= 0 || keySectionH <= 0) return false;
    if (m_keyW == width && m_keyH == keySectionH && m_keyDC.GetSafeHdc())
        return true;

    if (m_keyDC.GetSafeHdc()) {
        if (m_keyOldBmp) m_keyDC.SelectObject(m_keyOldBmp);
        m_keyDC.DeleteDC();
    }
    m_keyBmp.DeleteObject();
    m_keyOldBmp = nullptr;

    if (!m_keyDC.CreateCompatibleDC(&refDC)) return false;
    if (!m_keyBmp.CreateCompatibleBitmap(&refDC, width, keySectionH)) {
        m_keyDC.DeleteDC();
        return false;
    }
    m_keyOldBmp = m_keyDC.SelectObject(&m_keyBmp);
    m_keyW = width;
    m_keyH = keySectionH;
    m_keyBufReady = false;
    return true;
}

void CPianoRoll::EnsurePaintFonts(int clientW, int keyH, int rollH)
{
    if (m_paintFontsReady &&
        m_fontCacheClientW == clientW &&
        m_fontCacheKeyH == keyH &&
        m_fontCacheRollH == rollH)
        return;

    const int rowPitch = HistoryRowPitch(rollH);
    const int lanePx = max(5, clientW / 88);
    const int notePx = max(9, min(14, clientW / 52));
    const int octPx = max(8, min(12, clientW / 52));
    const int tagPx = max(7, min(11, keyH / 4));
    // 表現記号: ウィンドウを広げたらレーン幅(lanePx)・行高(rowPitch)に比例して拡大。
    // 以前は上限20/10で頭打ちだったので引き上げ、リサイズで見やすくなるようにする。
    const int symPx = max(11, min(30, max(lanePx * 2, rowPitch + 6)));
    const int symCompactPx = max(7, min(18, lanePx + 2));
    const int legPx = max(7, min(11, min(clientW / 58, rollH / 15)));

    if (m_fontKeyNote.GetSafeHandle()) m_fontKeyNote.DeleteObject();
    if (m_fontKeyOct.GetSafeHandle()) m_fontKeyOct.DeleteObject();
    if (m_fontMeterTag.GetSafeHandle()) m_fontMeterTag.DeleteObject();
    if (m_fontExprSymbol.GetSafeHandle()) m_fontExprSymbol.DeleteObject();
    if (m_fontExprSymbolCompact.GetSafeHandle()) m_fontExprSymbolCompact.DeleteObject();
    if (m_fontExprLegend.GetSafeHandle()) m_fontExprLegend.DeleteObject();

    m_fontKeyNote.CreateFont(-notePx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    m_fontKeyOct.CreateFont(-octPx, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    m_fontMeterTag.CreateFont(-tagPx, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    m_fontExprSymbol.CreateFont(-symPx, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Symbol");
    m_fontExprSymbolCompact.CreateFont(-symCompactPx, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Symbol");
    m_fontExprLegend.CreateFont(-legPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    m_fontCacheClientW = clientW;
    m_fontCacheKeyH = keyH;
    m_fontCacheRollH = rollH;
    m_paintFontsReady = true;
    // フォント差し替え後は凡例キャッシュを作り直す
    ReleaseExprLegendCache();
}

void CPianoRoll::GetExprLegendPanelRect(int rollW, int rollH, CRect& panel) const
{
    panel.SetRectEmpty();
    if (rollW < 72 || rollH < 48) return;

    static const int kItemCount = 8;
    const int pad = max(3, min(6, rollW / 90));
    int lineH = max(9, min(16, rollH / 14));
    int titleH = max(10, min(14, lineH + 1));
    int badgeW = max(10, min(15, rollW / 28));

    int cols = 1;
    const int fullH = pad * 2 + titleH + kItemCount * lineH;
    if (rollW >= 210 && rollH < fullH - 8)
        cols = 2;

    const int rows = (kItemCount + cols - 1) / cols;
    int panelW = min(rollW - 8, max(88, rollW * 55 / 100));
    if (cols == 2)
        panelW = min(panelW, min(240, rollW - 8));
    else
        panelW = min(panelW, min(200, rollW - 8));

    int panelH = pad * 2 + titleH + rows * lineH;
    const int maxH = rollH * 42 / 100;
    if (panelH > maxH) {
        lineH = max(8, (maxH - pad * 2 - titleH) / rows);
        panelH = pad * 2 + titleH + rows * lineH;
    }
    if (rollH < 100) {
        titleH = 0;
        lineH = max(8, min(12, rollH / 8));
        badgeW = max(8, min(12, rollW / 40));
        panelH = pad * 2 + lineH;
        panelW = min(rollW - 8, pad * 2 + kItemCount * (badgeW + 2));
    }

    panel.SetRect(4, 4, 4 + panelW, 4 + panelH);
}

void CPianoRoll::ReleaseExprLegendCache() const
{
    if (m_legendDC.GetSafeHdc()) {
        if (m_legendOldBmp) m_legendDC.SelectObject(m_legendOldBmp);
        m_legendDC.DeleteDC();
    }
    m_legendBmp.DeleteObject();
    m_legendOldBmp = nullptr;
    m_legendW = m_legendH = 0;
    m_legendReady = false;
    m_legendCacheRollW = m_legendCacheRollH = -1;

    if (m_legendBgDC.GetSafeHdc()) {
        if (m_legendBgOldBmp) m_legendBgDC.SelectObject(m_legendBgOldBmp);
        m_legendBgDC.DeleteDC();
    }
    m_legendBgBmp.DeleteObject();
    m_legendBgOldBmp = nullptr;
    m_legendBgW = m_legendBgH = 0;
}

bool CPianoRoll::EnsureExprLegendCache(CDC& refDC, int rollW, int rollH) const
{
    CRect panel;
    GetExprLegendPanelRect(rollW, rollH, panel);
    if (panel.IsRectEmpty()) return false;
    if (!m_paintFontsReady || !m_fontExprLegend.GetSafeHandle() || !m_fontExprSymbol.GetSafeHandle())
        return false;

    const int pw = panel.Width();
    const int ph = panel.Height();
    if (pw <= 0 || ph <= 0) return false;

    // サイズ(=フォントサイズ依存)が変わったら作り直す。内容は静的なので一度だけ描画。
    if (m_legendReady && m_legendCacheRollW == rollW && m_legendCacheRollH == rollH
        && m_legendW == pw && m_legendH == ph && m_legendDC.GetSafeHdc())
        return true;

    // legendBg（焼き込み退避用）は消さない。オーバーレイキャッシュだけ作り直す。
    if (m_legendDC.GetSafeHdc()) {
        if (m_legendOldBmp) m_legendDC.SelectObject(m_legendOldBmp);
        m_legendDC.DeleteDC();
    }
    m_legendBmp.DeleteObject();
    m_legendOldBmp = nullptr;
    m_legendW = m_legendH = 0;
    m_legendReady = false;
    m_legendCacheRollW = m_legendCacheRollH = -1;

    if (!m_legendDC.CreateCompatibleDC(&refDC)) return false;
    if (!m_legendBmp.CreateCompatibleBitmap(&refDC, pw, ph)) {
        m_legendDC.DeleteDC();
        return false;
    }
    m_legendOldBmp = m_legendDC.SelectObject(&m_legendBmp);
    // 不透明ステッカーとして一回だけ描く（TransparentBlt+マゼンタはピンク文字を抜いてしまう）
    m_legendDC.FillSolidRect(0, 0, pw, ph, RGB(14, 14, 20));
    DrawExprLegendContent(m_legendDC, rollW, rollH, CRect(0, 0, pw, ph), false);

    m_legendW = pw;
    m_legendH = ph;
    m_legendCacheRollW = rollW;
    m_legendCacheRollH = rollH;
    m_legendReady = true;
    return true;
}

// 凡例パネル背景を半透明で塗る(下のバーを透かす)。1x1のソースを引き伸ばして
// AlphaBlend する軽量実装。DC/ビットマップは再利用して分単位の GDI 断片化を防ぐ。
static void PianoFillRectAlpha(CDC& dc, const CRect& rc, COLORREF clr, BYTE alpha)
{
    if (rc.Width() <= 0 || rc.Height() <= 0) return;
    static CDC s_mem;
    static CBitmap s_bmp;
    static CBitmap* s_old = nullptr;
    static bool s_ready = false;
    static COLORREF s_clr = (COLORREF)-1;
    if (!s_ready) {
        if (!s_mem.CreateCompatibleDC(&dc)) return;
        if (!s_bmp.CreateCompatibleBitmap(&dc, 1, 1)) { s_mem.DeleteDC(); return; }
        s_old = s_mem.SelectObject(&s_bmp);
        s_ready = true;
        s_clr = (COLORREF)-1;
    }
    if (s_clr != clr) {
        s_mem.SetPixelV(0, 0, clr);
        s_clr = clr;
    }
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, alpha, 0 };
    dc.AlphaBlend(rc.left, rc.top, rc.Width(), rc.Height(), &s_mem, 0, 0, 1, 1, bf);
}

void CPianoRoll::DrawExprLegend(CDC& dc, int rollW, int rollH) const
{
    CRect panel;
    GetExprLegendPanelRect(rollW, rollH, panel);
    if (panel.IsRectEmpty()) return;
    const int pw = panel.Width(), ph = panel.Height();
    if (pw <= 0 || ph <= 0) return;

    // ちらつき(点滅)対策: 画面 dc へ「バー描画 → α重ね」を直接2段で行うと、
    // GDI は可視サーフェスへ直接描くため一瞬バーが見えて点滅する。
    // パネルサイズのオフスクリーン DC で合成し、1回の BitBlt で提示する。
    // 下地のバーは凡例を焼き込んでいない m_rollDC の該当領域から取得するため、
    // フレーム蓄積(α重ねの濃化)も起きない。
    CDC mem;
    if (!mem.CreateCompatibleDC(&dc)) { DrawExprLegendContent(dc, rollW, rollH, panel); return; }
    CBitmap bmp;
    if (!bmp.CreateCompatibleBitmap(&dc, pw, ph)) { mem.DeleteDC(); DrawExprLegendContent(dc, rollW, rollH, panel); return; }
    CBitmap* ob = mem.SelectObject(&bmp);

    // 下地(流れるバー)を m_rollDC から取り込む。未準備なら背景色で埋める。
    if (m_rollDC.GetSafeHdc())
        mem.BitBlt(0, 0, pw, ph, const_cast<CDC*>(&m_rollDC), panel.left, panel.top, SRCCOPY);
    else
        mem.FillSolidRect(0, 0, pw, ph, RGB(20, 20, 20));

    // 凡例本体を (0,0) 原点で合成(背景はα、その上に枠・バッジ・文字)。
    DrawExprLegendContent(mem, rollW, rollH, CRect(0, 0, pw, ph));

    dc.BitBlt(panel.left, panel.top, pw, ph, &mem, 0, 0, SRCCOPY);
    mem.SelectObject(ob);
    mem.DeleteDC();
}

void CPianoRoll::DrawExprLegendContent(CDC& dc, int rollW, int rollH, const CRect& panel, bool fillPanelBg) const
{
    using namespace PianoDraw;
    if (panel.IsRectEmpty()) return;
    if (!m_paintFontsReady || !m_fontExprLegend.GetSafeHandle() || !m_fontExprSymbol.GetSafeHandle())
        return;

    static const uint8_t kFlags[] = {
        PianoExpr::ACCENT, PianoExpr::SCOOP, PianoExpr::FALL,
        PianoExpr::SLIDE, PianoExpr::VIBRATO, PianoExpr::CRESC,
        PianoExpr::DECRESC, PianoExpr::SUSTAIN
    };
    static const wchar_t* kLabels[] = {
        LL14(L"アクセント", L"Accent", L"Accent", L"Accento", L"Acento", L"액센트", L"重音", L"لهجة", L"Акцент", L"Akzent", L"Acento", L"Accent", L"Akcent", L"Aksan"),
        LL14(L"スクープ", L"Scoop", L"Scoop", L"Scoop", L"Scoop", L"스쿱", L"滑音(上)", L"Scoop", L"Скуп", L"Scoop", L"Scoop", L"Scoop", L"Scoop", L"Scoop"),
        LL14(L"フォール", L"Fall", L"Chute", L"Fall", L"Caída", L"하강", L"滑音(下)", L"Fall", L"Падение", L"Fall", L"Queda", L"Fall", L"Spadek", L"Düşüş"),
        LL14(L"スライド", L"Slide", L"Glissé", L"Slide", L"Desliz", L"슬라이드", L"滑音", L"Slide", L"Слайд", L"Slide", L"Slide", L"Slide", L"Slide", L"Slide"),
        LL14(L"ビブラート", L"Vibrato", L"Vibrato", L"Vibrato", L"Vibrato", L"비브라토", L"颤音", L"Vibrato", L"Вибрато", L"Vibrato", L"Vibrato", L"Vibrato", L"Wibrato", L"Vibrato"),
        LL14(L"クレッシェンド", L"Cresc.", L"Cresc.", L"Cresc.", L"Cresc.", L"크레셴도", L"渐强", L"Cresc.", L"Крещ.", L"Cresc.", L"Cresc.", L"Cresc.", L"Cresc.", L"Cresc."),
        LL14(L"デクレッシェンド", L"Decresc.", L"Decresc.", L"Decresc.", L"Decresc.", L"데크레셴도", L"渐弱", L"Decresc.", L"Дим.", L"Decresc.", L"Decresc.", L"Decresc.", L"Decresc.", L"Decresc."),
        LL14(L"サステイン", L"Sustain", L"Sustain", L"Sustain", L"Sustain", L"서스테인", L"延音", L"Sustain", L"Длит.", L"Sustain", L"Sustain", L"Sustain", L"Sustain", L"Sustain")
    };
    const int n = (int)(sizeof(kFlags) / sizeof(kFlags[0]));
    const int pad = max(3, min(6, rollW / 90));
    const int lineH = max(8, (panel.Height() - pad * 2) / (rollH < 100 ? 1 : (n + 1)));
    const int titleH = (rollH < 100) ? 0 : max(10, min(14, lineH + 1));
    const int badgeW = max(8, min(15, rollW / 28));
    const bool iconsOnly = (rollH < 100);
    int cols = 1;
    if (!iconsOnly && rollW >= 210 && panel.Height() < pad * 2 + titleH + n * lineH - 4)
        cols = 2;
    const int rows = iconsOnly ? 1 : (n + cols - 1) / cols;

    // 背景は半透明で塗り、下を流れるバーがうっすら透ける(可読性は保つ濃さ)。
    if (fillPanelBg)
        PianoFillRectAlpha(dc, panel, RGB(14, 14, 20), 170);
    HGDIOBJ oldPen = dc.SelectObject(::GetStockObject(DC_PEN));
    ::SetDCPenColor(dc.GetSafeHdc(), RGB(70, 70, 82));
    dc.MoveTo(panel.left, panel.bottom - 1); dc.LineTo(panel.left, panel.top);
    dc.LineTo(panel.right - 1, panel.top); dc.LineTo(panel.right - 1, panel.bottom - 1);
    dc.LineTo(panel.left, panel.bottom - 1);
    dc.SelectObject(oldPen);

    CFont* pLeg = CFont::FromHandle((HFONT)m_fontExprLegend.GetSafeHandle());
    CFont* pSym = CFont::FromHandle((HFONT)m_fontExprSymbol.GetSafeHandle());
    CFont* pOldF = dc.SelectObject(pLeg);
    dc.SetBkMode(TRANSPARENT);

    int y = panel.top + pad;
    if (!iconsOnly) {
        dc.SetTextColor(RGB(210, 210, 220));
        CRect titleR(panel.left + pad, y, panel.right - pad, y + titleH);
        dc.DrawText(LL14(L"記号の意味", L"Symbol legend", L"Légende", L"Legenda simboli", L"Leyenda", L"기호 설명", L"符号说明", L"دليل الرموز", L"Обозначения", L"Symbollegende", L"Legenda", L"Symbolen", L"Legenda symboli", L"Semboller"),
            titleR, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        y += titleH;
    }

    if (iconsOnly) {
        const int gap = 2;
        int bw = (panel.Width() - pad * 2 - gap * (n - 1)) / n;
        if (bw < 8) bw = 8;
        int x = panel.left + pad;
        for (int i = 0; i < n; ++i) {
            CRect badgeR(x, y, min(panel.right - pad, x + bw), y + lineH);
            DrawExprBadgePanel(dc, badgeR, kFlags[i], pSym);
            x += bw + gap;
        }
    }
    else if (cols == 2) {
        const int colW = (panel.Width() - pad * 2) / 2;
        for (int i = 0; i < n; ++i) {
            const int col = i / rows;
            const int row = i % rows;
            const int x0 = panel.left + pad + col * colW;
            const int y0 = y + row * lineH;
            CRect badgeR(x0, y0 + 1, x0 + badgeW, y0 + lineH - 1);
            DrawExprBadgePanel(dc, badgeR, kFlags[i], pSym);
            CRect labelR(badgeR.right + 4, y0, x0 + colW - 2, y0 + lineH);
            dc.SetTextColor(ExprColorForFlag(kFlags[i]));
            dc.DrawText(kLabels[i], labelR, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
    }
    else {
        for (int i = 0; i < n; ++i) {
            CRect badgeR(panel.left + pad, y + 1, panel.left + pad + badgeW, y + lineH - 1);
            DrawExprBadgePanel(dc, badgeR, kFlags[i], pSym);
            CRect labelR(badgeR.right + 4, y, panel.right - pad, y + lineH);
            dc.SetTextColor(ExprColorForFlag(kFlags[i]));
            dc.DrawText(kLabels[i], labelR, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            y += lineH;
        }
    }
    dc.SelectObject(pOldF);
}

void CPianoRoll::DrawHistoryGrid(CDC& dc, int width, int yFrom, int yTo) const
{
    if (yFrom < 0) yFrom = 0;
    if (yFrom >= yTo) return;
    // 毎フレーム CreatePen すると長時間で GDI が断片化し EQ 描画まで重くなる
    HGDIOBJ oldPen = dc.SelectObject(::GetStockObject(DC_PEN));
    ::SetDCPenColor(dc.GetSafeHdc(), RGB(34, 34, 34));
    for (int i = 1; i < KEY_COUNT; ++i) {
        int xL, xR; GetChromaticKeyRect(i, width, xL, xR);
        dc.MoveTo(xL, yFrom);
        dc.LineTo(xL, yTo);
    }
    dc.SelectObject(oldPen);
}

int CPianoRoll::HistoryRowPitch(int rollH) const
{
    int yTop, yBot;
    GetHistoryRowBounds(rollH, 0, yTop, yBot);
    return max(1, yBot - yTop);
}

int CPianoRoll::HistoryScrollPx(int rollH, int rowsToScroll) const
{
    if (rowsToScroll <= 0 || rollH <= 0) return 0;
    const int maxRows = (int)MAX_HISTORY;
    if (rowsToScroll >= maxRows) return rollH;
    int yTop0, yBot0, yTopN, yBotN;
    GetHistoryRowBounds(rollH, 0, yTop0, yBot0);
    GetHistoryRowBounds(rollH, rowsToScroll, yTopN, yBotN);
    return yTop0 - yTopN;
}

void CPianoRoll::GetHistoryRowBounds(int rollH, int rowFromBottom, int& yTop, int& yBot) const
{
    if (rowFromBottom < 0) rowFromBottom = 0;
    if (rowFromBottom >= (int)MAX_HISTORY || rollH <= 0) {
        yTop = yBot = 0;
        return;
    }
    const int maxRows = (int)MAX_HISTORY;
    const int s = maxRows - 1 - rowFromBottom;
    yTop = s * rollH / maxRows;
    yBot = (s + 1) * rollH / maxRows;
    if (yTop < 0) yTop = 0;
    if (yBot > rollH) yBot = rollH;
    if (yBot <= yTop) yBot = yTop + 1;
}

void CPianoRoll::DrawHistoryRowAt(CDC& dc, int width, int yTop, int yBot, const NoteFrame& frame) const
{
    using namespace PianoDraw;
    if (yBot <= yTop) return;
    const int rowPitch = yBot - yTop;

    for (int i = 0; i < KEY_COUNT; ++i) {
        if (!frame.active[i]) continue;
        const int midi = MIDI_BASE + i;
        int xL, xR; GetChromaticKeyRect(i, width, xL, xR);
        const uint8_t bMask = frame.bandMask[i] ? frame.bandMask[i] : (uint8_t)(1u << KeyBandIndex(i));
        DrawHistoryNote(dc, CRect(xL + 1, yTop, xR - 1, yBot), bMask, frame.laneStrength[i],
            i, frame.strength[i], frame.dynLevel[i], frame.expr[i], IsBlackKey(midi));
    }

    // 再アタック(タイ連結中の同鍵連打)が起きた鍵は、バーの左端に細い白線を
    // 一本引いて「ここでノートが切り替わった」ことを見た目でも分かるようにする。
    // ゲート100で沈黙区間がなくても、この線でO6L4CCCCCCのような連打を区別できる。
    HGDIOBJ oldPen = dc.SelectObject(::GetStockObject(DC_PEN));
    ::SetDCPenColor(dc.GetSafeHdc(), RGB(255, 255, 255));
    for (int i = 0; i < KEY_COUNT; ++i) {
        if (!frame.active[i] || !frame.reattack[i]) continue;
        int xL, xR; GetChromaticKeyRect(i, width, xL, xR);
        if (xR - xL < 2) continue;
        dc.MoveTo(xL + 1, yTop);
        dc.LineTo(xL + 1, yBot);
    }
    dc.SelectObject(oldPen);

    if (!m_paintFontsReady || !m_fontExprSymbol.GetSafeHandle()) return;
    CFont* pSymFont = CFont::FromHandle((HFONT)m_fontExprSymbol.GetSafeHandle());
    for (int i = 0; i < KEY_COUNT; ++i) {
        if (!frame.active[i] || !frame.expr[i]) continue;
        int xL, xR; GetChromaticKeyRect(i, width, xL, xR);
        const int laneW = xR - xL - 2;
        if (laneW < 14 && m_fontExprSymbolCompact.GetSafeHandle())
            pSymFont = CFont::FromHandle((HFONT)m_fontExprSymbolCompact.GetSafeHandle());
        else
            pSymFont = CFont::FromHandle((HFONT)m_fontExprSymbol.GetSafeHandle());
        DrawExprSymbolTop(dc, CRect(xL + 1, yTop, xR - 1, yBot), frame.expr[i],
            0, yBot, pSymFont, rowPitch);
    }
}

void CPianoRoll::DrawHistoryRow(CDC& dc, int width, int rollH, size_t rowIndex, const NoteFrame& frame) const
{
    int yTop, yBot;
    GetHistoryRowBounds(rollH, (int)rowIndex, yTop, yBot);
    DrawHistoryRowAt(dc, width, yTop, yBot, frame);
}

void CPianoRoll::DrawHistoryArea(CDC& dc, int width, int rollH, int histCount, const NoteFrame* hist) const
{
    dc.FillSolidRect(0, 0, width, rollH, RGB(20, 20, 20));
    DrawHistoryGrid(dc, width, 0, rollH);
    // row0=live（DrawPlayheadRow）。row r>=1 には hist[r-1]（PushFrame で row0 に入った直前フレーム）
    if (!hist) return;
    for (int r = 1; r < histCount && r < (int)MAX_HISTORY; ++r)
        DrawHistoryRow(dc, width, rollH, r, hist[r - 1]);

    DrawPitchTransitions(dc, width, rollH, histCount, hist);
}

// 音階移行(スライド/フォール/スクープ)の斜め描画:
// 隣接フレーム間で音が隣接音階へ移った箇所を、行境界をまたいで斜めの帯で繋ぎ、
// 縦バーの段差ではなく滑らかな移行に見せる（既存の縦バーはそのまま＝互換性重視）。全音域。
void CPianoRoll::DrawPitchTransitions(CDC& dc, int width, int rollH, int histCount, const NoteFrame* hist) const
{
    if (!hist || histCount < 3) return;
    const int maxR = (histCount < (int)MAX_HISTORY) ? histCount : (int)MAX_HISTORY;
    const uint8_t kTransMask = PianoExpr::SLIDE | PianoExpr::FALL | PianoExpr::SCOOP;

    CGdiObject* pOldBrush = dc.SelectStockObject(DC_BRUSH);
    CGdiObject* pOldPen = dc.SelectStockObject(DC_PEN);
    for (int r = 1; r + 1 < maxR; ++r) {
        const NoteFrame& fNew = hist[r - 1]; // 下(新しい)行 = row r
        const NoteFrame& fOld = hist[r];     // 上(古い)行 = row r+1
        int yTopNew, yBotNew, yTopOld, yBotOld;
        GetHistoryRowBounds(rollH, r, yTopNew, yBotNew);
        GetHistoryRowBounds(rollH, r + 1, yTopOld, yBotOld);
        const int midNew = (yTopNew + yBotNew) / 2;
        const int midOld = (yTopOld + yBotOld) / 2;

        for (int j = 0; j < KEY_COUNT; ++j) {
            if (!fNew.active[j] || !(fNew.expr[j] & kTransMask)) continue;
            // 移行元(古い行)で隣接(±1〜±2半音)に鳴っていた音を探す
            int src = -1;
            for (int d = 1; d <= 2 && src < 0; ++d) {
                if (j - d >= 0 && fOld.active[j - d]) src = j - d;
                else if (j + d < KEY_COUNT && fOld.active[j + d]) src = j + d;
            }
            if (src < 0 || src == j) continue;

            int xLs, xRs, xLd, xRd;
            GetChromaticKeyRect(src, width, xLs, xRs);
            GetChromaticKeyRect(j, width, xLd, xRd);
            if (xRs <= xLs || xRd <= xLd) continue;

            const COLORREF col = PianoDraw::LocalKeyColor(j, fNew.strength[j], false);
            // DC_PEN/DC_BRUSH で色だけ差し替え(ループ内 CreatePen/Brush を避ける)
            ::SetDCBrushColor(dc.GetSafeHdc(), col);
            ::SetDCPenColor(dc.GetSafeHdc(), col);
            POINT pts[4] = {
                { xLs + 1, midOld }, { xRs - 1, midOld },
                { xRd - 1, midNew }, { xLd + 1, midNew }
            };
            dc.Polygon(pts, 4);
        }
    }
    if (pOldBrush) dc.SelectObject(pOldBrush);
    if (pOldPen) dc.SelectObject(pOldPen);
}

void CPianoRoll::ComposeRollBuffer(CDC& dc, int width, int rollH,
    int histCount, const NoteFrame* hist, const NoteFrame& live) const
{
    DrawHistoryArea(dc, width, rollH, histCount, hist);
    DrawPlayheadRow(dc, width, rollH, live);
    // 凡例(記号の意味)はロールバッファへ焼き込まず、OnPaint で最終画面へ
    // 半透明オーバーレイとして重ねる(背景の黒をアルファ化して下のバーを透かす)。
}

void CPianoRoll::DrawPlayheadRow(CDC& dc, int width, int rollH, const NoteFrame& live) const
{
    int yTop, yBot;
    GetHistoryRowBounds(rollH, 0, yTop, yBot);
    if (yBot <= yTop) return;

    dc.FillSolidRect(0, yTop, width, yBot - yTop, RGB(20, 20, 20));
    DrawHistoryGrid(dc, width, yTop, yBot);
    DrawHistoryRowAt(dc, width, yTop, yBot, live);
}

// pendingCount 行分を1回の BitBlt でスクロールし、空いた帯に履歴+live を描く。
// 旧: pending 回フルバッファ転送 → 遅延時に O(n) で重くなり EQ を圧迫した。
bool CPianoRoll::TryAdvanceRollBuffer(int width, int rollH, int histCount, const NoteFrame* hist,
    int pendingCount, const NoteFrame& live)
{
    m_lastScrollPx = 0;
    m_lastScrollHealTop = 0;
    if (!m_rollReady || rollH <= 0)
        return false;
    if (!m_rollDC.GetSafeHdc() || !m_rollScratchDC.GetSafeHdc())
        return false;

    int n = pendingCount;
    if (n < 1) n = 1;
    if (n > 3) n = 3;

    const int scrollPx = HistoryScrollPx(rollH, n);
    const int preserveH = rollH - scrollPx;
    if (scrollPx <= 0 || preserveH <= 0)
        return false;

    int yBandTop = 0, yBandBot = 0;
    GetHistoryRowBounds(rollH, n - 1, yBandTop, yBandBot);
    if (yBandTop < 0) yBandTop = 0;
    if (rollH - yBandTop <= 0)
        return false;

    // A) 履歴ピクセルを scrollPx 分まとめて繰り上げ（n 回分を1回の BitBlt）
    m_rollScratchDC.BitBlt(0, 0, width, preserveH, &m_rollDC, 0, scrollPx, SRCCOPY);

    // B) 空いた帯にグリッド + 各行
    m_rollScratchDC.FillSolidRect(0, yBandTop, width, rollH - yBandTop, RGB(20, 20, 20));
    DrawHistoryGrid(m_rollScratchDC, width, yBandTop, rollH);

    // 連続 TryAdvance と同じ並び: row r(r>=1) ← hist[r], row0 ← live
    for (int r = n - 1; r >= 1; --r) {
        const NoteFrame& fr = (hist && histCount > r) ? hist[r] : live;
        int yTop, yBot;
        GetHistoryRowBounds(rollH, r, yTop, yBot);
        if (yBot > yTop)
            DrawHistoryRowAt(m_rollScratchDC, width, yTop, yBot, fr);
    }
    DrawPlayheadRow(m_rollScratchDC, width, rollH, live);

    m_rollDC.BitBlt(0, 0, width, rollH, &m_rollScratchDC, 0, 0, SRCCOPY);
    m_lastScrollPx = scrollPx;
    m_lastScrollHealTop = yBandTop;
    return true;
}

void CPianoRoll::UpdatePianoRollTimer()
{
    KillTimer(1);
    int ms = savedata.ms2;
    if (ms < 16) ms = 16;
    if (ms > 960) ms = 960;
    SetTimer(1, (UINT)ms, nullptr);
}

void CPianoRoll::RequestSyncFromMainUi()
{
    if (!::IsWindow(m_hWnd)) return;
    // 描画待ち中は追加 SYNC しない（OnSyncRequest の PCM コピー連鎖で時間とともに悪化する）
    if (InterlockedCompareExchange(&m_analysisDonePosted, 0, 0) != 0) return;
    if (InterlockedCompareExchange(&m_syncPosted, 0, 0) != 0) return;
    // 実時間スロットル（paint 遅延で ms2 が伸びても 60Hz 同期にしない）
    int minMs = savedata.ms2;
    if (minMs < 16) minMs = 16;
    if (minMs > 960) minMs = 960;
    const DWORD now = GetTickCount();
    if (m_lastSyncPostTick != 0 && (now - m_lastSyncPostTick) < (DWORD)minMs)
        return;
    if (InterlockedCompareExchange(&m_syncPosted, 1, 0) != 0) return;
    m_lastSyncPostTick = now;
    if (!PostMessage(WM_PIANOROLL_SYNC, 0, 0))
        InterlockedExchange(&m_syncPosted, 0);
}

void CPianoRoll::ApplySyncInvalidate()
{
    if (m_paintDisabled || !::IsWindow(m_hWnd)) return;
    if (m_meterDirty)
        m_keyDirty = true;
    // 0.9a と同じ全域無効化。部分無効化はロック矩形の描画漏れ等の
    // デグレ源になったため戻した。
    Invalidate(FALSE);
}

LRESULT CPianoRoll::OnSyncRequest(WPARAM, LPARAM)
{
    // syncPosted は OnPaint 完了まで保持（ANALYSIS_DONE と同じ背圧）。
    // ここで降ろすと SyncPianoRollFast がキューに積まれ UI が時間とともに死ぬ。
    if (m_paintDisabled || !::IsWindow(m_hWnd)) {
        InterlockedExchange(&m_syncPosted, 0);
        return 0;
    }
    COggDlg_SyncPianoRollFast();
    ApplySyncInvalidate();
    return 0;
}

LRESULT CPianoRoll::OnAnalysisDone(WPARAM, LPARAM)
{
    // posted フラグは OnPaint 完了まで保持（描画背圧）。
    // 先に降ろすと重い OnPaint 中に再 Post され、EQ コード更新が飢える。
    if (m_paintDisabled || !::IsWindow(m_hWnd)) {
        InterlockedExchange(&m_analysisDonePosted, 0);
        return 0;
    }
    ApplySyncInvalidate();
    return 0;
}

DWORD WINAPI CPianoRoll::AnalysisWorkerThreadEntry(LPVOID param)
{
    return static_cast<CPianoRoll*>(param)->AnalysisWorkerLoop();
}

// 分析ワーカースレッドを起動する。OnInitDialog から呼ばれる。
// イベント(m_hAnalysisWake)で眠り、AnalyzePlayCursorMono が SetEvent で起こす。
void CPianoRoll::StartAnalysisWorker()
{
    if (m_hAnalysisThread) return;
    InterlockedExchange(&m_workerStop, 0);
    InterlockedExchange(&m_jobPending, 0);
    InterlockedExchange(&m_analysisBusy, 0);
    if (!m_hAnalysisWake) {
        m_hAnalysisWake = CreateEvent(NULL, FALSE, FALSE, NULL);
        if (!m_hAnalysisWake) return;
    }
    m_hAnalysisThread = CreateThread(
        NULL, 0, AnalysisWorkerThreadEntry, this, 0, NULL);
    if (!m_hAnalysisThread) {
        CloseHandle(m_hAnalysisWake);
        m_hAnalysisWake = NULL;
    }
}

// 死亡スレッドの回収、stop フラグの解除、必要なら再起動。
// 形式切替後に解析が永久停止する主因をここで潰す。
// 再生スレッドと UI から同時に呼ばれ得るため m_jobCs で直列化する。
bool CPianoRoll::EnsureAnalysisWorkerAlive()
{
    EnterCriticalSection(&m_jobCs);

    if (m_hAnalysisThread) {
        const DWORD wr = WaitForSingleObject(m_hAnalysisThread, 0);
        if (wr == WAIT_OBJECT_0) {
            CloseHandle(m_hAnalysisThread);
            m_hAnalysisThread = NULL;
            if (m_hAnalysisWake) {
                CloseHandle(m_hAnalysisWake);
                m_hAnalysisWake = NULL;
            }
            InterlockedExchange(&m_workerStop, 0);
            InterlockedExchange(&m_analysisBusy, 0);
            InterlockedExchange(&m_jobPending, 0);
        }
        else if (wr == WAIT_TIMEOUT) {
            // 生きているが stop=1 のまま残っているとジョブを受け付けない
            if (InterlockedCompareExchange(&m_workerStop, 0, 0) != 0) {
                InterlockedExchange(&m_workerStop, 0);
                InterlockedExchange(&m_jobPending, 0);
                InterlockedExchange(&m_analysisBusy, 0);
                if (m_hAnalysisWake)
                    SetEvent(m_hAnalysisWake);
            }
            const bool alive = (m_hAnalysisWake != NULL);
            LeaveCriticalSection(&m_jobCs);
            return alive;
        }
        else {
            // WAIT_FAILED 等: ハンドルが壊れているので捨てて作り直す
            CloseHandle(m_hAnalysisThread);
            m_hAnalysisThread = NULL;
            if (m_hAnalysisWake) {
                CloseHandle(m_hAnalysisWake);
                m_hAnalysisWake = NULL;
            }
            InterlockedExchange(&m_workerStop, 0);
            InterlockedExchange(&m_analysisBusy, 0);
            InterlockedExchange(&m_jobPending, 0);
        }
    }

    if (!m_hAnalysisThread)
        StartAnalysisWorker();
    const bool ok = (m_hAnalysisThread != NULL && m_hAnalysisWake != NULL);
    LeaveCriticalSection(&m_jobCs);
    return ok;
}

// m_workerStop フラグを立てて SetEvent でワーカーを起こし、終了を待つ。
// デストラクタと DetachForDestroy から呼ばれる。
void CPianoRoll::StopAnalysisWorker()
{
    if (!m_hAnalysisThread && !m_hAnalysisWake) return;
    InterlockedExchange(&m_workerStop, 1);
    InterlockedExchange(&m_jobPending, 0);
    if (m_hAnalysisWake)
        SetEvent(m_hAnalysisWake);
    if (m_hAnalysisThread) {
        // タイムアウト時にハンドルだけ閉じるとスレッドが生きたまま UAF する
        const DWORD wr = WaitForSingleObject(m_hAnalysisThread, 15000);
        if (wr == WAIT_OBJECT_0) {
            CloseHandle(m_hAnalysisThread);
            m_hAnalysisThread = NULL;
        }
        else if (wr == WAIT_TIMEOUT) {
            // 既に死んでいるのにシグナルされないケースは上で拾えないので再確認
            if (WaitForSingleObject(m_hAnalysisThread, 0) == WAIT_OBJECT_0) {
                CloseHandle(m_hAnalysisThread);
                m_hAnalysisThread = NULL;
            }
        }
    }
    if (m_hAnalysisThread == NULL && m_hAnalysisWake) {
        CloseHandle(m_hAnalysisWake);
        m_hAnalysisWake = NULL;
    }
    if (m_hAnalysisThread == NULL)
        InterlockedExchange(&m_workerStop, 0);
    InterlockedExchange(&m_analysisBusy, 0);
}

// ワーカースレッドのメインループ。イベント待ちで眠り、起こされたら
// m_jobPending を CAS で取得して ProcessAnalysisJob を実行する。
// 解析完了後、::IsWindow チェックを挟んでから PostMessage するのは
// ウィンドウが既に破棄されている場合の HWND 再利用バグを防ぐため。
DWORD CPianoRoll::AnalysisWorkerLoop()
{
    for (;;) {
        HANDLE wake = m_hAnalysisWake;
        if (!wake)
            break;
        const DWORD wait = WaitForSingleObject(wake, INFINITE);
        if (wait != WAIT_OBJECT_0)
            continue;
        if (InterlockedCompareExchange(&m_workerStop, 0, 0) != 0)
            break;

        bool didWork = false;
        while (InterlockedCompareExchange(&m_jobPending, 0, 1) == 1) {
            if (InterlockedCompareExchange(&m_workerStop, 0, 0) != 0)
                break;
            if (ProcessAnalysisJob())
                didWork = true;
        }

        // 多重 Post するとキューが空にならず WM_PAINT が飢餓する。
        // coalesce + 最短 8ms（アナライザー KickUiPresent と同じ。ms2 にすると体感が重い）。
        if (didWork && ::IsWindow(m_hWnd)) {
            const DWORD now = GetTickCount();
            if (m_lastAnalysisDonePostTick == 0 || (now - m_lastAnalysisDonePostTick) >= 8u) {
                if (InterlockedCompareExchange(&m_analysisDonePosted, 1, 0) == 0) {
                    m_lastAnalysisDonePostTick = now;
                    if (!PostMessage(WM_PIANOROLL_ANALYSIS_DONE, 0, 0))
                        InterlockedExchange(&m_analysisDonePosted, 0);
                }
            }
        }
    }
    return 0;
}

// ジョブバッファをローカルにコピーしてから m_jobCs を解放し、
// 長い Goertzel 演算中はジョブバッファを解放しておく(再生スレッドが
// 次のジョブを書き込める状態を保つ)。
// Goertzel は m_cs 外。UI OnPaint が結果スナップショットだけ短時間待つようにする。
// SEH で CS / busy を必ず解放し、例外でワーカーが死んでもロックを残さない。
bool CPianoRoll::ProcessAnalysisJob()
{
    InterlockedExchange(&m_analysisBusy, 1);
    const LONG epochAtStart = InterlockedCompareExchange(&m_analysisEpoch, 0, 0);

    int frameCount = 0;
    int sampleRate = 44100;

    EnterCriticalSection(&m_jobCs);
    frameCount = m_jobFrameCount;
    sampleRate = m_jobSampleRate;
    if (frameCount > 0) {
        if ((int)m_workerMonoScratch.size() < frameCount)
            m_workerMonoScratch.resize((size_t)frameCount);
        memcpy(m_workerMonoScratch.data(), m_jobMono.data(), (size_t)frameCount * sizeof(double));
    }
    LeaveCriticalSection(&m_jobCs);

    bool ok = false;
    if (m_feedEnabled &&
        InterlockedCompareExchange(&m_analysisEpoch, 0, 0) == epochAtStart &&
        frameCount >= MinAnalyzeFrameCount(sampleRate, frameCount) &&
        sampleRate >= 8000) {
        const double* mono = m_workerMonoScratch.data();
        __try {
            ok = RunAnalysisJob(mono, frameCount, sampleRate, epochAtStart);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            ok = false;
        }
        if (ok &&
            m_feedEnabled &&
            InterlockedCompareExchange(&m_analysisEpoch, 0, 0) == epochAtStart) {
            EnterCriticalSection(&m_cs);
            __try {
                PublishDetectResults();
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                ok = false;
            }
            LeaveCriticalSection(&m_cs);
        }
    }

    InterlockedExchange(&m_analysisBusy, 0);
    return ok;
}

void CPianoRoll::PublishDetectResults()
{
    if (m_detectSilent) {
        for (int i = 0; i < KEY_COUNT; ++i) {
            m_activeKeys[i] = false;
            m_bandMask[i] = 0;
            memset(m_laneStrength[i], 0, sizeof(m_laneStrength[i]));
            m_exprFlags[i] = 0;
        }
        PushFrame(false);
        return;
    }
    UpdateNoteStates();
    PushDisplayFrames();
}

bool CPianoRoll::RunAnalysisJob(const double* mono, int frameCount, int sampleRate, LONG epochAtStart)
{
    if (!mono) return false;
    if (!m_feedEnabled ||
        InterlockedCompareExchange(&m_analysisEpoch, 0, 0) != epochAtStart)
        return false;

    try {
        EnsureAnalysisTables(sampleRate);
        if (frameCount < m_winLow || m_winLow <= 0 ||
            (int)m_goertzelCoeffs.size() < KEY_COUNT ||
            (int)m_windowedLow.size() < m_winLow)
            return false;
        const double* lowWin = mono + (frameCount - m_winLow);
        const int bassLen = (frameCount >= m_winBass) ? m_winBass : m_winLow;
        const double* bassWin = (frameCount >= m_winBass)
            ? mono + (frameCount - m_winBass)
            : lowWin;

        RunGoertzelFromBuffer(lowWin, bassWin, bassLen);
        return true;
    }
    catch (...) {
        return false;
    }
}

void CPianoRoll::DrawKeyboardToBuffer(CDC& memDC, int width, int keySectionH, int keyH,
    const bool* activesCopy, const uint8_t* bandMaskCopy, const float laneStrengthCopy[KEY_COUNT][3],
    const float* chFillCopy, int chCountCopy, const uint8_t* exprCopy) const
{
    using namespace PianoDraw;

    const int bkH = keyH * WHITE_KEY_COUNT / 100;
    const int labelH = min(16, keyH / 4);
    const int keyTop = labelH + 2;
    const int splitY = keyTop + bkH;
    const COLORREF whiteFace = RGB(238, 238, 238);
    const COLORREF blackFace = RGB(28, 28, 34);
    memDC.FillSolidRect(0, 0, width, keySectionH, RGB(150, 150, 155));

    for (int i = 0; i < KEY_COUNT; ++i) {
        const int midi = MIDI_BASE + i; if (IsBlackKey(midi)) continue;
        int xL, xR; GetWhiteKeyRect52(midi, width, xL, xR);
        CRect kc(xL, splitY, xR, keySectionH);
        const bool on = activesCopy[i];
        if (on) {
            const uint8_t bMask = bandMaskCopy[i] ? bandMaskCopy[i] : (uint8_t)(1u << KeyBandIndex(i));
            DrawLaneKey(memDC, CRect(kc.left + 1, kc.top, kc.right - 1, kc.bottom), bMask, laneStrengthCopy[i], i, 2.5f, false, on);
        }
        else { DrawBevelKey(memDC, CRect(kc.left + 1, kc.top, kc.right - 1, kc.bottom), whiteFace, false); }
    }
    for (int i = 0; i < KEY_COUNT; ++i) {
        const int midi = MIDI_BASE + i; if (IsBlackKey(midi)) continue;
        int xL, xR; GetChromaticKeyRect(i, width, xL, xR);
        CRect kc(xL + 1, keyTop, xR - 1, splitY);
        const bool on = activesCopy[i];
        if (on) {
            const uint8_t bMask = bandMaskCopy[i] ? bandMaskCopy[i] : (uint8_t)(1u << KeyBandIndex(i));
            DrawLaneKey(memDC, kc, bMask, laneStrengthCopy[i], i, 2.5f, false, on);
        }
        else { DrawBevelKey(memDC, kc, whiteFace, false); }
    }
    for (int i = 0; i < KEY_COUNT; ++i) {
        const int midi = MIDI_BASE + i; if (!IsBlackKey(midi)) continue;
        int xL, xR; GetChromaticKeyRect(i, width, xL, xR);
        CRect kc(xL + 1, keyTop, xR - 1, splitY);
        const bool on = activesCopy[i];
        if (on) {
            const uint8_t bMask = bandMaskCopy[i] ? bandMaskCopy[i] : (uint8_t)(1u << KeyBandIndex(i));
            DrawLaneKey(memDC, kc, bMask, laneStrengthCopy[i], i, 2.5f, true, on);
        }
        else { DrawBevelKey(memDC, kc, blackFace, false); }
    }
    for (int i = 0; i < KEY_COUNT; ++i) {
        const int midi = MIDI_BASE + i;
        if (!IsBlackKey(midi) || !activesCopy[i]) continue;
        const int parentMidi = midi - 1;
        int xL, xR; GetWhiteKeyRect52(parentMidi, width, xL, xR);
        if (xR <= xL) continue;
        CRect kc(xL + 1, keySectionH - labelH - 2, xR - 1, keySectionH - 2);
        if (kc.Height() < 2) continue;
        const uint8_t bMask = bandMaskCopy[i] ? bandMaskCopy[i] : (uint8_t)(1u << KeyBandIndex(i));
        DrawLaneKey(memDC, kc, bMask, laneStrengthCopy[i], i, 2.5f, false, true);
    }

    {
        HGDIOBJ oldPen = memDC.SelectObject(::GetStockObject(DC_PEN));
        ::SetDCPenColor(memDC.GetSafeHdc(), RGB(90, 90, 95));
        memDC.MoveTo(0, splitY); memDC.LineTo(width, splitY);
        memDC.MoveTo(0, 0); memDC.LineTo(width, 0);
        memDC.SelectObject(oldPen);
    }

    // アクティブキーに表現記号を重ねる（履歴バーと同じグリフをキー側にも表示）。
    // クロマチックキー上部(keyTop付近)に主要フラグのグリフを1つ描く。
    if (m_showExprMarks && exprCopy && m_paintFontsReady && bkH >= 8) {
        CFont* pSym = nullptr;
        if (m_fontExprSymbolCompact.GetSafeHandle())
            pSym = CFont::FromHandle((HFONT)m_fontExprSymbolCompact.GetSafeHandle());
        else if (m_fontExprSymbol.GetSafeHandle())
            pSym = CFont::FromHandle((HFONT)m_fontExprSymbol.GetSafeHandle());
        if (pSym) {
            // 主要フラグの優先順位（履歴の ExprPrimaryColor と同順）
            static const uint8_t kPri[] = {
                PianoExpr::ACCENT, PianoExpr::SCOOP, PianoExpr::VIBRATO,
                PianoExpr::SLIDE, PianoExpr::FALL, PianoExpr::SUSTAIN
            };
            const int glyphH = min(bkH, 22);
            for (int i = 0; i < KEY_COUNT; ++i) {
                if (!activesCopy[i] || !exprCopy[i]) continue;
                uint8_t flag = 0;
                for (uint8_t f : kPri) { if (exprCopy[i] & f) { flag = f; break; } }
                if (!flag) continue;
                int xL, xR; GetChromaticKeyRect(i, width, xL, xR);
                if (xR - xL < 3) continue;
                CRect gr(xL, keyTop + 1, xR, keyTop + 1 + glyphH);
                DrawExprGlyphOnNote(memDC, gr, flag, pSym);
            }
        }
    }

    if (m_showLevelMeter && chCountCopy > 0 && labelH >= 4) {
        CRect meterStrip(2, 1, width - 2, labelH + 1);
        DrawChannelDbBars(memDC, meterStrip, chFillCopy, chCountCopy);
    }

    if (m_paintFontsReady) {
        memDC.SetBkMode(TRANSPARENT);
        CFont* pOldFont = memDC.SelectObject(CFont::FromHandle((HFONT)m_fontKeyNote.GetSafeHandle()));
        memDC.SetTextColor(RGB(70, 70, 75));
        for (int i = 0; i < KEY_COUNT; ++i) {
            const int midi = MIDI_BASE + i;
            const wchar_t* name = WhiteKeyLabel(midi); if (!name) continue;
            int xL, xR; GetWhiteKeyRect52(midi, width, xL, xR);
            CRect tr(xL + 2, keySectionH - labelH - 2, xR - 2, keySectionH - 2);
            memDC.DrawText(name, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        memDC.SelectObject(CFont::FromHandle((HFONT)m_fontKeyOct.GetSafeHandle()));
        memDC.SetTextColor(RGB(100, 100, 110));
        for (int i = 0; i < KEY_COUNT; ++i) {
            const int midi = MIDI_BASE + i; if (midi % 12 != 0) continue;
            int xL, xR; GetWhiteKeyRect52(midi, width, xL, xR);
            CString oct; oct.Format(L"%d", MidiOctaveNumber(midi));
            CRect tr(xL + 2, 1, xR - 2, labelH + 1);
            memDC.DrawText(oct, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        memDC.SelectObject(pOldFont);
    }
}

void CPianoRoll::PresentClientFromBuffers(CPaintDC& dc, int w, int h, int rollH, int keySectionH)
{
    UNREFERENCED_PARAMETER(h);
#if CCUSTOM_AERO_SUPPORT
    if (savedata.aero == 1 && CCC_IsWin11()) {
        if (m_chromaReady) {
            if (m_rollReady)
                m_chromaCache.BlitRect(dc.GetSafeHdc(), 0, 0, w, rollH);
            if (m_keyBufReady)
                m_chromaCache.BlitRect(dc.GetSafeHdc(), 0, rollH, w, keySectionH);
        }
        else {
            if (m_rollReady)
                dc.BitBlt(0, 0, w, rollH, &m_rollDC, 0, 0, SRCCOPY);
            if (m_keyBufReady)
                dc.BitBlt(0, rollH, w, keySectionH, &m_keyDC, 0, 0, SRCCOPY);
        }
    }
    else
#endif
    {
        if (m_rollReady)
            dc.BitBlt(0, 0, w, rollH, &m_rollDC, 0, 0, SRCCOPY);
        if (m_keyBufReady)
            dc.BitBlt(0, rollH, w, keySectionH, &m_keyDC, 0, 0, SRCCOPY);
    }

    if (m_frozen) {
        dc.SetBkMode(TRANSPARENT);
        dc.SetTextColor(RGB(255, 180, 80));
        CFont* of = nullptr;
        if (m_fontMeterTag.GetSafeHandle())
            of = dc.SelectObject(&m_fontMeterTag);
        dc.TextOut(8, 4, LL14(L"フリーズ中", L"Frozen", L"Gele", L"Congelato", L"Congelado", L"정지됨", L"已冻结", L"مجمد", L"Заморожено", L"Eingefroren", L"Congelado", L"Bevroren", L"Zamrozone", L"Donduruldu"));
        if (of) dc.SelectObject(of);
    }

    CCC_MainLockPaintClient(dc, m_hWnd);
    InterlockedExchange(&m_analysisDonePosted, 0);
    InterlockedExchange(&m_syncPosted, 0);
}

void CPianoRoll::OnPaint()
{
    CPaintDC dc(this);
    if (m_paintDisabled) {
        InterlockedExchange(&m_analysisDonePosted, 0);
        InterlockedExchange(&m_syncPosted, 0);
        return;
    }
    CRect rect;
    GetClientRect(&rect);
    const int w = rect.Width();
    const int h = rect.Height();
    if (w <= 0 || h <= 0) {
        InterlockedExchange(&m_analysisDonePosted, 0);
        InterlockedExchange(&m_syncPosted, 0);
        return;
    }

    int keyH = h * 20 / 100;
    if (keyH < 50) keyH = 50; if (keyH > 100) keyH = 100;
    const int rollH = h - keyH;
    const int keySectionH = h - rollH;
    if (rollH <= 0 || keySectionH <= 0) {
        InterlockedExchange(&m_analysisDonePosted, 0);
        InterlockedExchange(&m_syncPosted, 0);
        return;
    }

    if (CCC_MainLockPreferQuickPresent() && m_rollReady && m_keyBufReady
        && m_rollW == w && m_rollH == rollH && m_keyW == w && m_keyH == keySectionH) {
        PresentClientFromBuffers(dc, w, h, rollH, keySectionH);
        return;
    }

    CRect clip;
    dc.GetClipBox(&clip);
    const bool clipRoll = clip.top < rollH;
    const bool clipKey = clip.bottom > rollH;

    EnsurePaintFonts(w, keyH, rollH);
    if (!EnsureRollBuffer(dc, w, rollH) || !EnsureKeyBuffer(dc, w, keySectionH)) {
        InterlockedExchange(&m_analysisDonePosted, 0);
        InterlockedExchange(&m_syncPosted, 0);
        return;
    }

    NoteFrame liveSnap;
    bool activesCopy[KEY_COUNT];
    uint8_t bandMaskCopy[KEY_COUNT];
    float laneStrengthCopy[KEY_COUNT][3];
    float chFillCopy[PIANO_METER_CH_MAX];
    uint8_t exprCopy[KEY_COUNT];
    int chCountCopy = 0;
    chCountCopy = m_chMeterCount;
    memcpy(chFillCopy, m_chMeterFill, sizeof(chFillCopy));
    EnterCriticalSection(&m_cs);
    BuildLiveNoteFrame(liveSnap);
    memcpy(activesCopy, m_activeKeys, sizeof(m_activeKeys));
    memcpy(bandMaskCopy, m_bandMask, sizeof(m_bandMask));
    memcpy(laneStrengthCopy, m_laneStrength, sizeof(m_laneStrength));
    memcpy(exprCopy, m_exprFlags, sizeof(m_exprFlags));
    LeaveCriticalSection(&m_cs);

    if (m_meterDirty)
        m_keyDirty = true;

    int pending = 0;
    EnterCriticalSection(&m_cs);
    pending = m_framesPending;
    LeaveCriticalSection(&m_cs);

    const bool rollDirty = m_historyDirty;
    const bool needKeyDraw = m_keyDirty || !m_keyBufReady;
    bool didRollUpdate = false;
    bool didRollScroll = false;
    bool needAnotherRollFrame = false;

    // pending 分は1回の BitBlt スクロールで消化（n 回フル転送しない）。
    if (pending > 0 && m_rollReady) {
        int n = pending;
        if (n > 3) n = 3;
        NoteFrame histSnap[3];
        int histCount = 0;
        if (n > 1) {
            EnterCriticalSection(&m_cs);
            const int avail = (m_historyCount < n) ? m_historyCount : n;
            for (int i = 0; i < avail; ++i)
                histSnap[i] = HistoryAt(i);
            histCount = avail;
            LeaveCriticalSection(&m_cs);
        }
        if (TryAdvanceRollBuffer(w, rollH, histCount, histSnap, n, liveSnap)) {
            EnterCriticalSection(&m_cs);
            m_framesPending -= n;
            if (m_framesPending < 0) m_framesPending = 0;
            needAnotherRollFrame = (m_framesPending > 0);
            LeaveCriticalSection(&m_cs);
            m_rollScrollValid = true;
            m_rollReady = true;
            didRollUpdate = true;
            didRollScroll = true;
        }
        else {
            NoteFrame histFull[MAX_HISTORY];
            int histFullCount = 0;
            EnterCriticalSection(&m_cs);
            CopyHistorySnapshot(histFull, MAX_HISTORY, histFullCount);
            LeaveCriticalSection(&m_cs);
            ComposeRollBuffer(m_rollDC, w, rollH, histFullCount, histFull, liveSnap);
            EnterCriticalSection(&m_cs);
            m_framesPending = 0;
            LeaveCriticalSection(&m_cs);
            m_rollScrollValid = true;
            m_rollReady = true;
            didRollUpdate = true;
            didRollScroll = false;
        }
    }
    else if (rollDirty || !m_rollReady) {
        NoteFrame histSnap[MAX_HISTORY];
        int histCount = 0;
        EnterCriticalSection(&m_cs);
        CopyHistorySnapshot(histSnap, MAX_HISTORY, histCount);
        LeaveCriticalSection(&m_cs);
        ComposeRollBuffer(m_rollDC, w, rollH, histCount, histSnap, liveSnap);
        EnterCriticalSection(&m_cs);
        m_framesPending = 0;
        LeaveCriticalSection(&m_cs);
        m_rollScrollValid = true;
        m_rollReady = true;
        didRollUpdate = true;
        didRollScroll = false;
    }
    else if (m_rollReady) {
        DrawPlayheadRow(m_rollDC, w, rollH, liveSnap);
        didRollUpdate = true;
    }

    if (needKeyDraw) {
        DrawKeyboardToBuffer(m_keyDC, w, keySectionH, keyH, activesCopy, bandMaskCopy, laneStrengthCopy, chFillCopy, chCountCopy, exprCopy);
        m_keyBufReady = true;
    }

    // 凡例(記号の意味)は m_rollDC に「半透明で焼き込み」してから提示する。
    // アクリル時の最終面はアルファ前提(クロマキャッシュ)で、GDI で直接重ねると
    // アルファ0=完全透過になり文字すら出なくなる。そこで一旦 m_rollDC に焼き込み、
    // 通常Blit/クロマ変換の両方で正しく不透明に提示されるようにする。
    // 焼き込み前の下地バーを退避し、提示後に書き戻すことで、次のスクロールに
    // 凡例が混入(α重ねの蓄積)するのを防ぐ。下地バーは毎フレーム新鮮なので
    // バーが透けて見える表現は維持される。
    CRect lgPanel;
    GetExprLegendPanelRect(w, rollH, lgPanel);
    const bool haveLegend = m_showExprLegend && m_rollReady && !lgPanel.IsRectEmpty() && m_rollDC.GetSafeHdc();
    bool legendBaked = false;
    if (haveLegend) {
        const int pw = lgPanel.Width(), ph = lgPanel.Height();
        bool bgOk = (m_legendBgDC.GetSafeHdc() && m_legendBgW == pw && m_legendBgH == ph);
        if (!bgOk) {
            if (m_legendBgDC.GetSafeHdc()) {
                if (m_legendBgOldBmp) m_legendBgDC.SelectObject(m_legendBgOldBmp);
                m_legendBgDC.DeleteDC();
            }
            m_legendBgBmp.DeleteObject();
            m_legendBgOldBmp = nullptr;
            m_legendBgW = m_legendBgH = 0;
            if (m_legendBgDC.CreateCompatibleDC(&dc) && m_legendBgBmp.CreateCompatibleBitmap(&dc, pw, ph)) {
                m_legendBgOldBmp = m_legendBgDC.SelectObject(&m_legendBgBmp);
                m_legendBgW = pw;
                m_legendBgH = ph;
                bgOk = true;
            }
        }
        if (bgOk) {
            // 下地バーを退避 → 事前描画した凡例ステッカーを貼る → 提示後に下地へ戻す
            m_legendBgDC.BitBlt(0, 0, pw, ph, &m_rollDC, lgPanel.left, lgPanel.top, SRCCOPY);
            if (EnsureExprLegendCache(dc, w, rollH) && m_legendDC.GetSafeHdc()) {
                m_rollDC.BitBlt(lgPanel.left, lgPanel.top, pw, ph,
                    const_cast<CDC*>(&m_legendDC), 0, 0, SRCCOPY);
            }
            else {
                m_rollDC.FillSolidRect(&lgPanel, RGB(14, 14, 20));
                DrawExprLegendContent(m_rollDC, w, rollH, lgPanel, false);
            }
            legendBaked = true;
        }
    }

#if CCUSTOM_AERO_SUPPORT
    if (savedata.aero == 1 && CCC_IsWin11()) {
        if (m_chromaW != w || m_chromaH != h) {
            m_chromaCache.Release();
            m_chromaReady = false;
            m_chromaW = w;
            m_chromaH = h;
        }
        if (m_chromaCache.Ensure(dc.GetSafeHdc(), w, h)) {
            if (m_rollReady && didRollUpdate) {
                if (didRollScroll && m_lastScrollPx > 0 && m_chromaReady
                    && m_lastScrollPx < rollH) {
                    // スクロール時: キャッシュDIBを memmove で繰り上げ、
                    // 変化帯のみ再変換。scrollPx>=rollH のときは下の else で全域更新。
                    m_chromaCache.ScrollRows(0, rollH, m_lastScrollPx);
                    int bandTop = m_lastScrollHealTop;
                    if (bandTop <= 0) {
                        const int rowPitch = HistoryRowPitch(rollH);
                        bandTop = rollH - m_lastScrollPx - rowPitch - 2;
                    }
                    if (bandTop < 0) bandTop = 0;
                    const int bandH = rollH - bandTop;
                    if (bandH > 0)
                        m_chromaCache.UpdateRect(m_rollDC.GetSafeHdc(), 0, bandTop, 0, bandTop, w, bandH, PIANO_CHROMA_KEY);
                    CRect lg; GetExprLegendPanelRect(w, rollH, lg);
                    if (lg.Width() > 0 && lg.Height() > 0)
                        m_chromaCache.UpdateRect(m_rollDC.GetSafeHdc(), lg.left, lg.top, lg.left, lg.top, lg.Width(), lg.Height(), PIANO_CHROMA_KEY);
                }
                else {
                    m_chromaCache.UpdateRect(m_rollDC.GetSafeHdc(), 0, 0, 0, 0, w, rollH, PIANO_CHROMA_KEY);
                }
            }
            if (needKeyDraw || !m_chromaReady)
                m_chromaCache.UpdateRect(m_keyDC.GetSafeHdc(), 0, 0, 0, rollH, w, keySectionH, PIANO_CHROMA_KEY);
            m_chromaReady = true;
            if (m_rollReady)
                m_chromaCache.BlitRect(dc.GetSafeHdc(), 0, 0, w, rollH);
            if (m_keyBufReady)
                m_chromaCache.BlitRect(dc.GetSafeHdc(), 0, rollH, w, keySectionH);
        }
        else {
            if (m_rollReady)
                dc.BitBlt(0, 0, w, rollH, &m_rollDC, 0, 0, SRCCOPY);
            if (m_keyBufReady)
                dc.BitBlt(0, rollH, w, keySectionH, &m_keyDC, 0, 0, SRCCOPY);
        }
    }
    else
#endif
    {
        if (m_rollReady)
            dc.BitBlt(0, 0, w, rollH, &m_rollDC, 0, 0, SRCCOPY);
        if (m_keyBufReady)
            dc.BitBlt(0, rollH, w, keySectionH, &m_keyDC, 0, 0, SRCCOPY);
    }

    // 提示が終わったら m_rollDC の凡例領域を下地バーへ戻す(次スクロールへの混入防止)。
    if (legendBaked && m_legendBgDC.GetSafeHdc()) {
        m_rollDC.BitBlt(lgPanel.left, lgPanel.top, lgPanel.Width(), lgPanel.Height(),
            &m_legendBgDC, 0, 0, SRCCOPY);
    }

    if (m_frozen) {
        dc.SetBkMode(TRANSPARENT);
        dc.SetTextColor(RGB(255, 180, 80));
        CFont* of = nullptr;
        if (m_fontMeterTag.GetSafeHandle())
            of = dc.SelectObject(&m_fontMeterTag);
        dc.TextOut(8, 4, LL14(L"フリーズ中", L"Frozen", L"Gele", L"Congelato", L"Congelado", L"정지됨", L"已冻结", L"مجمد", L"Заморожено", L"Eingefroren", L"Congelado", L"Bevroren", L"Zamrozone", L"Donduruldu"));
        if (of) dc.SelectObject(of);
    }

    CCC_MainLockPaintClient(dc, m_hWnd);

    if (didRollUpdate)
        m_historyDirty = false;
    if (clipKey || needKeyDraw) {
        m_keyDirty = false;
        m_meterDirty = false;
    }
    // 追い付き用の即時自己再描画はしない。ここで Invalidate すると WM_PAINT が
    // 連鎖し、アクリル時の重いペイントで UI スレッドを占有してしまう。
    // 残りの保留フレームは次の解析完了(OnAnalysisDone)/同期(OnSyncRequest)時に描く。
    (void)needAnotherRollFrame;

    // 描画完了後に ANALYSIS_DONE / SYNC を開放（背圧）。
    // 閉じると軽くなる現象の主因だった「描画中の再 Post 連鎖」を断つ。
    InterlockedExchange(&m_analysisDonePosted, 0);
    InterlockedExchange(&m_syncPosted, 0);

    // [デバッグ用] どの機能が有効な状態でビルド・実行されているかを画面に直接表示する。
    // これが表示されなければ、この CPianoRoll.cpp が実際には動いていない証拠になる。
    // 動作確認が済んだら、このブロックごと削除して構わない。
#if 0
    {
        CString dbg;
        dbg.Format(L"PR-DBG-v7  reattack=%s  ghostGuard=%s  impulsive=%s",
            m_reattackDetectEnabled ? L"ON" : L"off",
            m_harmonicGhostGuardEnabled ? L"ON" : L"off",
            m_impulsiveGhostSuppressEnabled ? L"ON" : L"off");
        CFont dbgFont;
        dbgFont.CreateFont(-14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        CFont* pOldDbg = dc.SelectObject(&dbgFont);
        dc.SetBkMode(OPAQUE);
        dc.SetBkColor(RGB(0, 0, 0));
        dc.SetTextColor(RGB(255, 255, 0));
        CRect dbgRect(4, 4, w - 4, 24);
        dc.DrawText(dbg, dbgRect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        dc.SelectObject(pOldDbg);
    }
#endif
}

void CPianoRoll::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == 1) {
        CRect rc; GetWindowRect(&rc);
        if (!IsIconic()) {
            savedata.pianorollx = rc.left; savedata.pianorolly = rc.top;
            savedata.pianorollw = rc.Width(); savedata.pianorollh = rc.Height();
        }
    }
    CCustomBlurDialogExBase::OnTimer(nIDEvent);
}

void CPianoRoll::OnSize(UINT nType, int cx, int cy)
{
    CCustomBlurDialogExBase::OnSize(nType, cx, cy);
    ReleasePaintBuffers();
    m_historyDirty = true;
    m_keyDirty = true;
    m_framesPending = 0;
#if CCUSTOM_AERO_SUPPORT
    // Finalize の再実行はしない。DWM 属性の軽い再適用のみ。
    if (nType != SIZE_MINIMIZED && CCC_IsAeroEnabled())
        CCC_RefreshDwmBlur(m_hWnd);
#endif
    InvalidateRegions(true, true);
}

void CPianoRoll::OnMove(int x, int y)
{
    CCustomBlurDialogExBase::OnMove(x, y);
    // 簡易ピアノロールは高頻度更新のため Move 毎の DWM 再合成は行わない（重い）
}

void CPianoRoll::OnShowWindow(BOOL bShow, UINT nStatus)
{
    CCustomBlurDialogExBase::OnShowWindow(bShow, nStatus);
#if CCUSTOM_AERO_SUPPORT
    // 基底側で Apply/Refresh 済み。内容の再同期のみ行う。
    if (bShow && CCC_IsAeroEnabled())
    {
        m_keyDirty = true;
        m_rollScrollValid = false;
        RequestSyncFromMainUi();
        Invalidate(FALSE);
    }
#endif
    if (bShow) {
        m_rollReady = false;
        m_rollScrollValid = false;
        m_historyDirty = true;
    }
}

void CPianoRoll::OnClose()
{
    DetachForDestroy();
    savedata.pianorollwindow = 0;
    DestroyWindow();
}

BOOL CPianoRoll::PreTranslateMessage(MSG* pMsg)
{
    return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}