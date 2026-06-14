#include "stdafx.h"
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "CPianoRoll.h"
#include "resource.h"
#include "NoteFundamentalPick.h"
#include "PianoRollPick.h"
#include "PianoRollGoertzelAvx2.h"
#include <algorithm>

extern save savedata;
void COggDlg_SyncPianoRollFast();
IMPLEMENT_DYNAMIC(CPianoRoll, CCustomBlurDialogExBase)

namespace Cfg
{
    // piano roll3: 基音ピック + NormalizeDisplayPeak + 包絡ホールド
    static constexpr float IIR_ALPHA = 0.40f;
    static constexpr float IIR_ALPHA_BASS = 0.28f;
    static constexpr float SILENCE_ABS = 0.007f;
    static constexpr float BAND_SILENCE_BASS = 0.005f;
    static constexpr float BAND_SILENCE_MID = 0.004f;
    static constexpr float BAND_SILENCE_TRE = 0.004f;
    static constexpr int   ATTACK_FRAMES = 1;
    static constexpr int   RELEASE_FRAMES = 7;
    static constexpr int   VIS_GAP_FRAMES = 6;
    static constexpr float RETRIGGER_RATIO = 0.32f;
    static constexpr int   BAND_BASS_END = 25;
    static constexpr int   BAND_MID_END = 53;
    static constexpr float BASS_PICK_THRESH = 0.20f;
    static constexpr float MID_PICK_THRESH = 0.19f;
    static constexpr float TRE_PICK_THRESH = 0.16f;
    static constexpr float PRUNE_BAND_RATIO = 0.11f;
    static constexpr float PRUNE_TOP_RATIO = 0.17f;
    static constexpr float HOLD_ENV_BASS = 0.34f;
    static constexpr float HOLD_ENV_MID = 0.21f;
    static constexpr float HOLD_ENV_TRE = 0.19f;
    static constexpr float DISPLAY_PEAK_CAP = 5.0f;
    static constexpr int   ANALYZE_INTERVAL = 1024;
    static constexpr float WEAK_BASS_RATIO = 0.17f;
    static constexpr float WEAK_MID_RATIO = 0.15f;
    static constexpr float WEAK_TRE_RATIO = 0.14f;
    static constexpr int   ONSET_KEY_START = 41;
    static constexpr float ONSET_DELTA_THRESH = 0.045f;
    static constexpr float ONSET_MIN_STRENGTH = 0.08f;

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

    static void SuppressFalseSubharmonicPicks(const float* st, bool* picked,
        int bandStart, int bandEnd)
    {
        if (!st || !picked || bandStart >= bandEnd) return;
        static const int kUpSemi[] = { 12, 19, 24, 7 };
        for (int i = bandStart; i < bandEnd; ++i) {
            if (!picked[i]) continue;
            for (int up : kUpSemi) {
                const int hi = i + up;
                if (hi >= bandEnd) continue;
                if (!picked[hi] && st[hi] < st[i] * 0.20f) continue;
                if (st[hi] >= st[i] * 0.30f)
                    picked[i] = false;
            }
        }
    }

    static void SnapPicksToLocalMaxima(const float* st, bool* picked,
        int bandStart, int bandEnd, int radius)
    {
        if (!st || !picked || bandStart >= bandEnd || radius < 1) return;
        bool snapTo[128];
        memset(snapTo, 0, sizeof(snapTo));
        for (int i = bandStart; i < bandEnd; ++i) {
            if (!picked[i]) continue;
            int best = i;
            float bestS = st[i];
            const int jl = (i - radius < bandStart) ? bandStart : (i - radius);
            const int jh = (i + radius >= bandEnd) ? (bandEnd - 1) : (i + radius);
            for (int j = jl; j <= jh; ++j) {
                if (st[j] > bestS) {
                    bestS = st[j];
                    best = j;
                }
            }
            snapTo[best] = true;
        }
        for (int i = bandStart; i < bandEnd; ++i)
            picked[i] = snapTo[i];
    }

    static void StabilizeBassBandPicks(const float* st, bool* picked,
        int bandStart, int bandEnd)
    {
        if (!st || !picked || bandStart >= bandEnd) return;
        SuppressFalseSubharmonicPicks(st, picked, bandStart, bandEnd);
        SnapPicksToLocalMaxima(st, picked, bandStart, bandEnd, 2);
        CollapseNearbyPicks(st, picked, bandStart, bandEnd, 2, false);
    }

    static constexpr float BUFWAV3_TARGET_PEAK_DB = -11.0f;
    static constexpr float BUFWAV3_GAIN_DB_MAX = 32.0f;
    static constexpr float BUFWAV3_GAIN_ZERO_DB = -9.0f;
    static constexpr float BUFWAV3_PEAK_FLOOR_DB = -60.0f;

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

    static float QuieterHalfPeakDbFs(const double* samples, int n)
    {
        if (!samples || n < 512) return PeakDbFs(samples, n);
        const int half = n / 2;
        const float db0 = PeakDbFs(samples, half);
        const float db1 = PeakDbFs(samples + half, n - half);
        return (db0 < db1) ? db0 : db1;
    }

    static float Bufwav3LevelDbForDynamics(const double* winLow, int nLow,
        const double* winBass, int nBass)
    {
        const float peakFull = PeakDbFsWindows(winLow, nLow, winBass, nBass);
        float peakQuiet = QuieterHalfPeakDbFs(winLow, nLow);
        if (winBass && nBass > 0) {
            const float dbB = QuieterHalfPeakDbFs(winBass, nBass);
            if (dbB < peakQuiet) peakQuiet = dbB;
        }
        return (peakQuiet < peakFull) ? peakQuiet : peakFull;
    }

    static float MakeupGainDbForBufwav3(float peakDbFs)
    {
        if (peakDbFs >= BUFWAV3_GAIN_ZERO_DB) return 0.0f;
        float g = BUFWAV3_TARGET_PEAK_DB - peakDbFs;
        if (g > BUFWAV3_GAIN_DB_MAX) g = BUFWAV3_GAIN_DB_MAX;
        if (g < 0.0f) g = 0.0f;
        return g;
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
        if (!samples || n <= 0 || gainDb <= 0.001f) return;
        const double g = pow(10.0, (double)gainDb / 20.0);
        for (int i = 0; i < n; ++i) {
            double v = samples[i] * g;
            if (v > 1.0) v = 1.0;
            else if (v < -1.0) v = -1.0;
            samples[i] = v;
        }
    }

    static double ScaleGoertzelAmpD(double rawAmp, int keyIndex)
    {
        if (rawAmp <= 0.00005) return 0.0;
        double amp = rawAmp * (1.0 + ((double)keyIndex / 88.0) * 3.0);
        if (amp > 0.0001) {
            const double boost = amp * 50.0;
            amp = boost * boost * 0.002;
            if (amp > 10.0) amp = 10.0;
        }
        else {
            amp = 0.0;
        }
        return amp;
    }

    static float HoldEnvRatio(int keyIndex)
    {
        if (keyIndex < BAND_BASS_END) return HOLD_ENV_BASS;
        if (keyIndex < BAND_MID_END) return HOLD_ENV_MID;
        return HOLD_ENV_TRE;
    }

    static int TemporalFrames(int keyIndex, int baseFrames)
    {
        int lo = 0, hi = 88;
        if (keyIndex < BAND_BASS_END) { lo = 0; hi = BAND_BASS_END; }
        else if (keyIndex < BAND_MID_END) { lo = BAND_BASS_END; hi = BAND_MID_END; }
        else { lo = BAND_MID_END; hi = 88; }
        const int span = hi - lo;
        if (span <= 1) return baseFrames;
        const float t = (float)(keyIndex - lo) / (float)(span - 1);
        float scale;
        if (keyIndex >= BAND_MID_END)
            scale = 1.35f - t * 1.00f;
        else
            scale = 1.90f - t * 1.45f;
        int f = (int)(baseFrames * scale + 0.5f);
        if (keyIndex >= BAND_MID_END) {
            if (f < 1) f = 1;
            if (f > 12) f = 12;
        }
        else {
            if (f < 2) f = 2;
            if (f > 20) f = 20;
        }
        return f;
    }

    // 同時音数上限なし: 帯域内の鍵数だけをループ上限に使う
    static void ApplyBandFundamentalPick(const float* st, bool* picked,
        int bandStart, int bandEnd, float relThresh)
    {
        if (!st || !picked || bandStart >= bandEnd) return;
        const int bandSpan = bandEnd - bandStart;

        bool bandPick[128];
        memset(bandPick, 0, sizeof(bandPick));
        PickFundamentalNotesToBand(st, bandPick, 88,
            bandStart, bandEnd, bandSpan, relThresh);
        SuppressSubharmonicPicksInBand(st, bandPick, 88, bandStart, bandEnd);
        const int peakRadius = (bandStart == 0) ? 2 : 1;
        RefineToLocalPeaksInBand(st, bandPick, 88, bandStart, bandEnd, peakRadius);
        if (bandStart == 0)
            StabilizeBassBandPicks(st, bandPick, bandStart, bandEnd);
        PruneBandPicks(st, bandPick, bandStart, bandEnd,
            bandSpan, PRUNE_BAND_RATIO, PRUNE_TOP_RATIO);
        SuppressSubharmonicPicksInBand(st, bandPick, 88, bandStart, bandEnd);
        if (bandStart == 0)
            StabilizeBassBandPicks(st, bandPick, bandStart, bandEnd);

        for (int i = bandStart; i < bandEnd; ++i) {
            if (bandPick[i])
                picked[i] = true;
        }
    }

    static void SuppressWeakBandPicks(const float* strengths,
        bool* bassPick, bool* midPick, bool* treblePick)
    {
        if (!strengths || !bassPick || !midPick || !treblePick) return;

        const float bassMax = BandMaxStrength(strengths, 0, BAND_BASS_END);
        const float midMax = BandMaxStrength(strengths, BAND_BASS_END, BAND_MID_END);
        const float treMax = BandMaxStrength(strengths, BAND_MID_END, 88);

        if (bassMax > 1e-6f) {
            const float minB = bassMax * WEAK_BASS_RATIO;
            for (int i = 0; i < BAND_BASS_END; ++i)
                if (bassPick[i] && strengths[i] < minB) bassPick[i] = false;
        }
        if (midMax > 1e-6f) {
            const float minM = midMax * WEAK_MID_RATIO;
            for (int i = BAND_BASS_END; i < BAND_MID_END; ++i)
                if (midPick[i] && strengths[i] < minM) midPick[i] = false;
        }
        if (treMax > 1e-6f) {
            const float minT = treMax * WEAK_TRE_RATIO;
            for (int i = BAND_MID_END; i < 88; ++i)
                if (treblePick[i] && strengths[i] < minT) treblePick[i] = false;
        }
    }

    // 低音帯: スペアナ mode0 に近い「生強度の局所ピーク」ピック（基音推定の誤オクターブを避ける）
    static void ApplyBassBandSpeanaPick(const float* st, bool* picked,
        int bandStart, int bandEnd, float relThresh)
    {
        if (!st || !picked || bandStart >= bandEnd) return;
        const float bandMax = BandMaxStrength(st, bandStart, bandEnd);
        if (bandMax < 1e-6f) return;
        const float minS = bandMax * relThresh;

        bool cand[128];
        memset(cand, 0, sizeof(cand));
        for (int i = bandStart; i < bandEnd; ++i) {
            if (st[i] < minS) continue;
            int best = i;
            float bestS = st[i];
            const int jl = (i - 2 < bandStart) ? bandStart : (i - 2);
            const int jh = (i + 2 >= bandEnd) ? (bandEnd - 1) : (i + 2);
            for (int j = jl; j <= jh; ++j) {
                if (st[j] > bestS) {
                    bestS = st[j];
                    best = j;
                }
            }
            cand[best] = true;
        }

        static const int kUpSemi[] = { 12, 19, 24, 7, 5 };
        for (int i = bandStart; i < bandEnd; ++i) {
            if (!cand[i]) continue;
            for (int up : kUpSemi) {
                const int hi = i + up;
                if (hi >= bandEnd) continue;
                if (st[hi] >= st[i] * 0.32f) {
                    cand[i] = false;
                    break;
                }
            }
        }

        CollapseNearbyPicks(st, cand, bandStart, bandEnd, 2, false);
        for (int i = bandStart; i < bandEnd; ++i) {
            if (cand[i])
                picked[i] = true;
        }
    }

}

CPianoRoll::CPianoRoll(CWnd* pParent)
    : CCustomBlurDialogExBase(IDD_PIANOROLL, pParent)
{
    InitializeCriticalSection(&m_cs);
    InitializeCriticalSection(&m_jobCs);
    m_ring.assign(RING_SIZE, 0.0);
    m_analysisBuf.assign(WIN_LOW, 0.0);
    m_bassAnalysisBuf.assign(WIN_BASS, 0.0);
    m_windowedLow.assign(WIN_LOW, 0.0);
    m_windowedHigh.assign(WIN_HIGH, 0.0);
    m_windowedOnset.assign(WIN_ONSET, 0.0);

    memset(m_activeKeys, 0, sizeof(m_activeKeys));
    memset(m_noteStrength, 0, sizeof(m_noteStrength));
    memset(m_rawStrengths, 0, sizeof(m_rawStrengths));
    memset(m_smoothedStrengths, 0, sizeof(m_smoothedStrengths));
    memset(m_consecActive, 0, sizeof(m_consecActive));
    memset(m_consecSilent, 0, sizeof(m_consecSilent));
    memset(m_segmentId, 0, sizeof(m_segmentId));
    memset(m_envPeak, 0, sizeof(m_envPeak));
    memset(m_unpickedFrames, 0, sizeof(m_unpickedFrames));
    memset(m_strengthDipFrames, 0, sizeof(m_strengthDipFrames));
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
    for (int i = 0; i < PIANO_METER_CH_MAX; ++i) {
        m_chMeterDb[i] = -60.0f;
        m_chMeterFill[i] = 0.0f;
        m_chMeterAutoPeak[i] = 0.02f;
    }
    m_historyCount = 0;
    m_historyHead = 0;
    for (int hi = 0; hi < (int)MAX_HISTORY; ++hi) {
        auto& f = m_historyRing[hi];
        memset(f.active, 0, sizeof(f.active));
        memset(f.strength, 0, sizeof(f.strength));
        memset(f.segment, 0, sizeof(f.segment));
        memset(f.bandMask, 0, sizeof(f.bandMask));
        memset(f.laneStrength, 0, sizeof(f.laneStrength));
        memset(f.expr, 0, sizeof(f.expr));
        memset(f.dynLevel, 0, sizeof(f.dynLevel));
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

void CPianoRoll::ResetPlaybackState()
{
    InterlockedExchange(&m_jobPending, 0);
    EnterCriticalSection(&m_jobCs);
    m_jobFrameCount = 0;
    LeaveCriticalSection(&m_jobCs);

    EnterCriticalSection(&m_cs);
    m_ringWrite = 0;
    m_ringCount = 0;
    m_samplesSinceAnalyze = 0;
    m_playbackDelaySamples = 0;
    m_lastAnalyzeTick = 0;
    m_bufwav3LevelDb = -60.0f;
    m_chMeterCount = 0;
    for (int i = 0; i < PIANO_METER_CH_MAX; ++i) {
        m_chMeterDb[i] = -60.0f;
        m_chMeterFill[i] = 0.0f;
        m_chMeterAutoPeak[i] = 0.02f;
    }
    m_historyDirty = true;
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
    memset(m_consecActive, 0, sizeof(m_consecActive));
    memset(m_consecSilent, 0, sizeof(m_consecSilent));
    memset(m_segmentId, 0, sizeof(m_segmentId));
    memset(m_envPeak, 0, sizeof(m_envPeak));
    memset(m_unpickedFrames, 0, sizeof(m_unpickedFrames));
    memset(m_strengthDipFrames, 0, sizeof(m_strengthDipFrames));
    memset(m_bandMask, 0, sizeof(m_bandMask));
    memset(m_laneStrength, 0, sizeof(m_laneStrength));
    memset(m_prevBandMask, 0, sizeof(m_prevBandMask));
    memset(m_keySnapActive, 0, sizeof(m_keySnapActive));
    memset(m_keySnapBand, 0, sizeof(m_keySnapBand));
    m_analysisHasBass = false;
    ReleasePaintBuffers();
    m_historyDirty = true;
    m_keyDirty = true;
    m_historyCount = 0;
    m_historyHead = 0;
    if (!m_ring.empty())
        std::fill(m_ring.begin(), m_ring.end(), 0.0);
    for (int hi = 0; hi < (int)MAX_HISTORY; ++hi) {
        auto& f = m_historyRing[hi];
        memset(f.active, 0, sizeof(f.active));
        memset(f.strength, 0, sizeof(f.strength));
        memset(f.segment, 0, sizeof(f.segment));
        memset(f.bandMask, 0, sizeof(f.bandMask));
        memset(f.laneStrength, 0, sizeof(f.laneStrength));
        memset(f.expr, 0, sizeof(f.expr));
        memset(f.dynLevel, 0, sizeof(f.dynLevel));
    }
    LeaveCriticalSection(&m_cs);
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
    ON_MESSAGE(WM_PIANOROLL_SYNC, &CPianoRoll::OnSyncRequest)
    ON_MESSAGE(WM_PIANOROLL_ANALYSIS_DONE, &CPianoRoll::OnAnalysisDone)
END_MESSAGE_MAP()

BOOL CPianoRoll::OnInitDialog()
{
    CCustomBlurDialogExBase::OnInitDialog();
    SetWindowText(LL14(
        L"ピアノロール", L"Piano Roll", L"Rouleau piano", L"Rotolo pianoforte",
        L"Rollo de piano", L"피아노 롤", L"钢琴卷帘", L"لوحة البيانو",
        L"Пианоролл", L"Klavierrolle", L"Rolo de piano", L"Pianorol",
        L"Rolka pianina", L"Piyano rulosu"));

    ModifyStyle(WS_MINIMIZEBOX, 0);
    SetIcon(nullptr, TRUE);
    SetIcon(nullptr, FALSE);
#if CCUSTOM_AERO_SUPPORT
    if (!CCC_IsAeroEnabled())
#endif
        ModifyStyleEx(0, WS_EX_DLGMODALFRAME);

    if (savedata.pianorollx != -1)
        SetWindowPos(&CWnd::wndTop,
            savedata.pianorollx, savedata.pianorolly,
            savedata.pianorollw, savedata.pianorollh,
            SWP_NOZORDER | SWP_NOOWNERZORDER);
    else
        SetWindowPos(&CWnd::wndTop, 100, 150, 800, 450,
            SWP_NOZORDER | SWP_NOOWNERZORDER);

    EnsureAnalysisTables(m_inputSampleRate);
    StartAnalysisWorker();
    UpdatePianoRollTimer();
    m_feedEnabled = true;
    m_paintDisabled = false;
    m_historyDirty = true;
#if CCUSTOM_AERO_SUPPORT
    if (CCC_IsAeroEnabled())
        ApplyDwmBlur();
#endif
    return TRUE;
}

float CPianoRoll::MidiToFreq(int midi)
{
    return 440.0f * powf(2.0f, (midi - 69) / 12.0f);
}

int CPianoRoll::KeyBandIndex(int keyIndex)
{
    if (keyIndex < Cfg::BAND_BASS_END) return 0;
    if (keyIndex < Cfg::BAND_MID_END) return 1;
    return 2;
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

void CPianoRoll::EnsureAnalysisTables(int sampleRate)
{
    if (sampleRate < 8000) sampleRate = 44100;
    if (sampleRate == m_inputSampleRate && !m_goertzelCoeffs.empty())
        return;

    m_inputSampleRate = sampleRate;
    m_goertzelCoeffs.resize(KEY_COUNT);
    for (int i = 0; i < KEY_COUNT; ++i) {
        const int midi = MIDI_BASE + i;
        const double freq = MidiToFreq(midi);
        m_goertzelCoeffs[i] = 2.0 * cos(2.0 * M_PI * freq / sampleRate);
    }

    m_hannLow.resize(WIN_LOW);
    for (int n = 0; n < WIN_LOW; ++n)
        m_hannLow[n] = 0.5 - 0.5 * cos(2.0 * M_PI * n / (WIN_LOW - 1));

    m_hannOnset.resize(WIN_ONSET);
    for (int n = 0; n < WIN_ONSET; ++n)
        m_hannOnset[n] = 0.5 - 0.5 * cos(2.0 * M_PI * n / (WIN_ONSET - 1));

    m_hannBass.resize(WIN_BASS);
    for (int n = 0; n < WIN_BASS; ++n)
        m_hannBass[n] = 0.5 - 0.5 * cos(2.0 * M_PI * n / (WIN_BASS - 1));

    m_blackmanHigh.resize(WIN_HIGH);
    for (int n = 0; n < WIN_HIGH; ++n) {
        m_blackmanHigh[n] = 0.42 - 0.5 * cos(2.0 * M_PI * n / (WIN_HIGH - 1))
            + 0.08 * cos(4.0 * M_PI * n / (WIN_HIGH - 1));
    }

    m_windowedLow.assign(WIN_LOW, 0.0);
    m_windowedHigh.assign(WIN_HIGH, 0.0);
    m_windowedOnset.assign(WIN_ONSET, 0.0);
    m_analysisBuf.assign(WIN_LOW, 0.0);
    m_bassAnalysisBuf.assign(WIN_BASS, 0.0);
}

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

float CPianoRoll::ApplyDisplayScale(float rawAmp, int keyIndex)
{
    return ScaleGoertzelAmp(rawAmp, keyIndex + KEY_OFFSET, DETECT_KEYS);
}

void CPianoRoll::FeedPCM(const void* pData, int frames,
    int sampleRate, int bits, int channels, int playbackDelaySamples)
{
    if (!m_feedEnabled || !pData || frames <= 0 || channels <= 0 || bits < 8) return;

    EnterCriticalSection(&m_cs);
    EnsureAnalysisTables(sampleRate);
    if (playbackDelaySamples > 0) {
        m_playbackDelaySamples = (m_playbackDelaySamples * 3 + playbackDelaySamples) / 4;
    }

    const uint8_t* p = static_cast<const uint8_t*>(pData);
    const int bytesPerSample = bits / 8;
    const int frameBytes = bytesPerSample * channels;

    for (int f = 0; f < frames; ++f)
    {
        double mono = 0.0;
        for (int ch = 0; ch < channels; ++ch) {
            const uint8_t* sp = p + f * frameBytes + ch * bytesPerSample;
            mono += ReadMonoSample(sp, bits);
        }
        mono /= channels;

        m_ring[m_ringWrite] = mono;
        m_ringWrite = (m_ringWrite + 1) % RING_SIZE;
        if (m_ringCount < RING_SIZE) ++m_ringCount;
        ++m_samplesSinceAnalyze;
    }

    LeaveCriticalSection(&m_cs);
}

void CPianoRoll::AnalyzePlayCursorMono(const double* mono, int frameCount, int sampleRate)
{
    if (!mono || frameCount < WIN_LOW || sampleRate < 8000) return;
    if (!m_hAnalysisWake || m_workerStop) return;

    const DWORD now = GetTickCount();
    if (m_lastAnalyzeTick != 0 && (now - m_lastAnalyzeTick) < ANALYZE_MIN_MS)
        return;
    m_lastAnalyzeTick = now;

    EnterCriticalSection(&m_jobCs);
    const int copyFrames = (frameCount > PIANO_BASS_FRAMES) ? PIANO_BASS_FRAMES : frameCount;
    memcpy(m_jobMono, mono, (size_t)copyFrames * sizeof(double));
    m_jobFrameCount = copyFrames;
    m_jobSampleRate = sampleRate;
    InterlockedExchange(&m_jobPending, 1);
    LeaveCriticalSection(&m_jobCs);
    SetEvent(m_hAnalysisWake);
}

void CPianoRoll::SetChannelMeterDb(const float* dbPerChannel, int channelCount)
{
    static constexpr float kPeakDecay = 0.994f;
    static constexpr float kFillAttack = 0.55f;
    static constexpr float kFillRelease = 0.18f;

    EnterCriticalSection(&m_cs);
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
    LeaveCriticalSection(&m_cs);
    if (meterChanged)
        m_meterDirty = true;
}

void CPianoRoll::RunGoertzelFromBuffer(const double* winLow8192,
    const double* winBass, int bassWinLen)
{
    if (!winLow8192) return;

    for (int i = 0; i < WIN_LOW; ++i)
        m_analysisBuf[i] = winLow8192[i];

    const bool hasBass = (winBass && bassWinLen >= WIN_BASS);
    m_analysisHasBass = hasBass;
    if (hasBass) {
        for (int i = 0; i < WIN_BASS; ++i)
            m_bassAnalysisBuf[i] = winBass[i];
    }

    const float levelDb = Cfg::Bufwav3LevelDbForDynamics(
        m_analysisBuf.data(), WIN_LOW,
        hasBass ? m_bassAnalysisBuf.data() : nullptr,
        hasBass ? WIN_BASS : 0);
    m_bufwav3LevelDb = levelDb;
    const float gainDb = Cfg::MakeupGainDbForBufwav3(levelDb);
    Cfg::ApplyGainDbInPlace(m_analysisBuf.data(), WIN_LOW, gainDb);
    if (hasBass)
        Cfg::ApplyGainDbInPlace(m_bassAnalysisBuf.data(), WIN_BASS, gainDb);

    for (int i = 0; i < WIN_LOW; ++i)
        m_windowedLow[i] = m_analysisBuf[i] * m_hannLow[i];
    for (int i = 0; i < WIN_HIGH; ++i)
        m_windowedHigh[i] = m_analysisBuf[i + (WIN_LOW - WIN_HIGH)] * m_blackmanHigh[i];
    const double* onsetSrc = m_analysisBuf.data() + (WIN_LOW - WIN_ONSET);
    for (int i = 0; i < WIN_ONSET; ++i)
        m_windowedOnset[i] = onsetSrc[i] * m_hannOnset[i];

    for (int i = 0; i < WIN_ONSET; ++i)
        m_windowedOnset[i] = onsetSrc[i] * m_hannOnset[i];

    PianoRollGoertzelBatchAvx2(
        m_windowedLow.data(), WIN_LOW, m_goertzelCoeffs.data(),
        0, LOW_KEY_SPLIT, m_goertzelRawScratch);
    for (int i = 0; i < LOW_KEY_SPLIT; ++i)
        m_rawStrengths[i] = ApplyDisplayScale((float)m_goertzelRawScratch[i], i);

    PianoRollGoertzelBatchAvx2(
        m_windowedHigh.data(), WIN_HIGH, m_goertzelCoeffs.data(),
        LOW_KEY_SPLIT, KEY_COUNT, m_goertzelRawScratch);
    for (int i = LOW_KEY_SPLIT; i < KEY_COUNT; ++i)
        m_rawStrengths[i] = ApplyDisplayScale((float)m_goertzelRawScratch[i - LOW_KEY_SPLIT], i);

    PianoRollGoertzelBatchAvx2(
        m_windowedOnset.data(), WIN_ONSET, m_goertzelCoeffs.data(),
        Cfg::ONSET_KEY_START, KEY_COUNT, m_goertzelRawScratch);
    for (int i = 0; i < Cfg::ONSET_KEY_START; ++i)
        m_onsetStrengths[i] = 0.0f;
    for (int i = Cfg::ONSET_KEY_START; i < KEY_COUNT; ++i) {
        m_onsetStrengths[i] = ApplyDisplayScale(
            (float)m_goertzelRawScratch[i - Cfg::ONSET_KEY_START], i);
    }

    for (int i = 0; i < KEY_COUNT; ++i) {
        const float alpha = (i < Cfg::BAND_BASS_END) ? Cfg::IIR_ALPHA_BASS : Cfg::IIR_ALPHA;
        m_smoothedStrengths[i] =
            m_smoothedStrengths[i] * (1.0f - alpha) + m_rawStrengths[i] * alpha;
    }

    UpdateNoteStates();
    PushFrame(false);
}

void CPianoRoll::UpdateNoteStates()
{
    using namespace Cfg;

    float pickStrength[KEY_COUNT];
    float trackStrength[KEY_COUNT];
    for (int i = 0; i < KEY_COUNT; ++i) {
        pickStrength[i] = m_rawStrengths[i];
        trackStrength[i] = m_smoothedStrengths[i];
    }

    NormalizeBandPeak(pickStrength, 0, BAND_BASS_END, DISPLAY_PEAK_CAP);
    NormalizeBandPeak(pickStrength, BAND_BASS_END, BAND_MID_END, DISPLAY_PEAK_CAP);
    NormalizeBandPeak(pickStrength, BAND_MID_END, KEY_COUNT, DISPLAY_PEAK_CAP);
    NormalizeBandPeak(trackStrength, 0, BAND_BASS_END, DISPLAY_PEAK_CAP);
    NormalizeBandPeak(trackStrength, BAND_BASS_END, BAND_MID_END, DISPLAY_PEAK_CAP);
    NormalizeBandPeak(trackStrength, BAND_MID_END, KEY_COUNT, DISPLAY_PEAK_CAP);

    const float pickScale = PickThreshScaleFromLevelDb(m_bufwav3LevelDb);

    float maxS = 0.0f;
    for (int i = 0; i < KEY_COUNT; ++i)
        if (pickStrength[i] > maxS) maxS = pickStrength[i];

    const float bassMax = BandMaxStrength(pickStrength, 0, BAND_BASS_END);
    const float midMax = BandMaxStrength(pickStrength, BAND_BASS_END, BAND_MID_END);
    const float treMax = BandMaxStrength(pickStrength, BAND_MID_END, KEY_COUNT);
    const bool anyBandLive =
        bassMax >= BAND_SILENCE_BASS ||
        midMax >= BAND_SILENCE_MID ||
        treMax >= BAND_SILENCE_TRE;

    if (!anyBandLive || maxS < SILENCE_ABS) {
        for (int i = 0; i < KEY_COUNT; ++i) {
            m_activeKeys[i] = false;
            m_noteStrength[i] = 0.0f;
            m_consecActive[i] = 0;
            m_consecSilent[i] = 0;
            m_unpickedFrames[i] = 0;
            m_strengthDipFrames[i] = 0;
            m_envPeak[i] = 0.0f;
            m_bandMask[i] = 0;
            memset(m_laneStrength[i], 0, sizeof(m_laneStrength[i]));
            m_smoothedStrengths[i] *= 0.4f;
        }
        return;
    }

    bool bassPick[KEY_COUNT];
    bool midPick[KEY_COUNT];
    bool treblePick[KEY_COUNT];
    bool picked[KEY_COUNT];
    memset(bassPick, 0, sizeof(bassPick));
    memset(midPick, 0, sizeof(midPick));
    memset(treblePick, 0, sizeof(treblePick));
    memset(picked, 0, sizeof(picked));

    ApplyBassBandSpeanaPick(pickStrength, picked, 0, BAND_BASS_END,
        BASS_PICK_THRESH * pickScale);
    ApplyBandFundamentalPick(pickStrength, picked, BAND_BASS_END, BAND_MID_END,
        MID_PICK_THRESH * pickScale);
    ApplyBandFundamentalPick(pickStrength, picked, BAND_MID_END, KEY_COUNT,
        TRE_PICK_THRESH * pickScale);

    for (int i = 0; i < KEY_COUNT; ++i) {
        bassPick[i] = midPick[i] = treblePick[i] = false;
        if (!picked[i]) continue;
        if (i < BAND_BASS_END) bassPick[i] = true;
        else if (i < BAND_MID_END) midPick[i] = true;
        else treblePick[i] = true;
    }

    SuppressWeakBandPicks(pickStrength, bassPick, midPick, treblePick);

    for (int i = 0; i < KEY_COUNT; ++i)
        picked[i] = bassPick[i] || midPick[i] || treblePick[i];

    for (int i = 0; i < KEY_COUNT; ++i)
    {
        const float sigStrength = pickStrength[i];
        bool effectivePicked = picked[i];

        if (!effectivePicked && i >= ONSET_KEY_START) {
            const float onsetDelta = m_onsetStrengths[i] - m_prevOnsetStrengths[i];
            if (onsetDelta >= ONSET_DELTA_THRESH &&
                m_onsetStrengths[i] >= ONSET_MIN_STRENGTH &&
                pickStrength[i] >= TRE_PICK_THRESH * pickScale * 0.55f)
                effectivePicked = true;
        }

        if (!effectivePicked && m_activeKeys[i]) {
            if (i < BAND_BASS_END) {
                // 低音はラッチしない（誤った音程への固定を防ぐ）
            }
            else {
                const float holdRatio = HoldEnvRatio(i);
                if (holdRatio > 0.0f && m_envPeak[i] > 0.001f &&
                    trackStrength[i] >= m_envPeak[i] * holdRatio)
                    effectivePicked = true;
            }
        }

        m_noteStrength[i] = effectivePicked ? m_rawStrengths[i] : 0.0f;

        if (effectivePicked) {
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
            m_consecActive[i] = 0;
            if (m_activeKeys[i]) {
                ++m_unpickedFrames[i];
                if (m_envPeak[i] > 0.001f &&
                    sigStrength < m_envPeak[i] * RETRIGGER_RATIO)
                    ++m_strengthDipFrames[i];
                else
                    m_strengthDipFrames[i] = 0;
            }
        }

        bool cur = m_activeKeys[i];
        if (!cur) {
            if (effectivePicked && m_consecActive[i] >= TemporalFrames(i, ATTACK_FRAMES)) {
                cur = true;
                m_consecSilent[i] = 0;
                ++m_segmentId[i];
                m_envPeak[i] = sigStrength;
                m_unpickedFrames[i] = 0;
                m_strengthDipFrames[i] = 0;
            }
        }
        else {
            if (effectivePicked) {
                m_unpickedFrames[i] = 0;
                m_strengthDipFrames[i] = 0;
            }
            const int gapLimit = TemporalFrames(i, VIS_GAP_FRAMES);
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

    for (int i = 0; i < KEY_COUNT; ++i) {
        if (!m_activeKeys[i]) {
            m_noteStrength[i] = 0.0f;
            continue;
        }
        if (m_noteStrength[i] <= 0.0f)
            m_noteStrength[i] = m_rawStrengths[i];
    }

    for (int i = 0; i < KEY_COUNT; ++i) {
        m_bandMask[i] = 0;
        memset(m_laneStrength[i], 0, sizeof(m_laneStrength[i]));
        if (!m_activeKeys[i]) continue;

        float bandStr[3] = { 0.0f, 0.0f, 0.0f };
        if (bassPick[i]) bandStr[0] = pickStrength[i];
        else if (midPick[i]) bandStr[1] = pickStrength[i];
        else if (treblePick[i]) bandStr[2] = pickStrength[i];
        else bandStr[KeyBandIndex(i)] = pickStrength[i];

        int lane = 0;
        for (int b = 0; b < 3; ++b) {
            if (bandStr[b] <= 0.0f) continue;
            m_bandMask[i] |= (uint8_t)(1u << b);
            m_laneStrength[i][lane++] = bandStr[b];
        }
    }

    DetectExpressions();

    memcpy(m_prevOnsetStrengths, m_onsetStrengths, sizeof(m_onsetStrengths));
}

namespace
{
    static bool HistDetectVibrato(const float* hist, int count, float envPeak)
    {
        if (count < 8 || envPeak < 0.04f) return false;
        float mean = 0.0f;
        for (int k = 0; k < count; ++k) mean += hist[k];
        mean /= (float)count;
        float minV = mean, maxV = mean;
        int reversals = 0;
        float prevDet = 0.0f;
        int prevSign = 0;
        for (int k = 0; k < count; ++k) {
            const float v = hist[k];
            if (v < minV) minV = v;
            if (v > maxV) maxV = v;
            const float det = v - mean;
            const int sign = (det > 0.015f * envPeak) ? 1 : ((det < -0.015f * envPeak) ? -1 : 0);
            if (sign != 0 && prevSign != 0 && sign != prevSign)
                ++reversals;
            if (sign != 0) prevSign = sign;
            prevDet = det;
        }
        const float swing = maxV - minV;
        return reversals >= 2 && swing >= envPeak * 0.08f;
    }
}

namespace PianoExpr {
    static constexpr uint8_t ACCENT  = 0x01;
    static constexpr uint8_t SCOOP   = 0x02;
    static constexpr uint8_t VIBRATO = 0x04;
    static constexpr uint8_t SLIDE   = 0x08;
    static constexpr uint8_t FALL    = 0x10;
    static constexpr uint8_t SUSTAIN = 0x20;
    static constexpr uint8_t ALL_MASK = ACCENT | SCOOP | VIBRATO | SLIDE | FALL | SUSTAIN;
}

void CPianoRoll::DetectExpressions()
{
    for (int i = 1; i < KEY_COUNT; ++i) {
        if (m_scoopLatch[i] > 0) --m_scoopLatch[i];
        if (m_activeKeys[i - 1])
            m_scoopLatch[i] = 5;
    }

    for (int i = 0; i < KEY_COUNT; ++i) {
        m_exprFlags[i] = 0;
        const bool wasActive = m_prevActiveKeys[i];
        const bool nowActive = m_activeKeys[i];

        if (!nowActive) {
            m_noteAgeFrames[i] = 0;
            m_vibHistCount[i] = 0;
            continue;
        }

        if (!wasActive)
            m_noteAgeFrames[i] = 0;
        else if (m_noteAgeFrames[i] < 255)
            ++m_noteAgeFrames[i];

        if (!wasActive) {
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

        if (m_noteAgeFrames[i] <= 3) {
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

        if (m_noteAgeFrames[i] >= 5 && m_vibHistCount[i] >= 8) {
            const int n = min((int)m_vibHistCount[i], VIB_HIST_LEN);
            if (HistDetectVibrato(m_vibHist[i] + (VIB_HIST_LEN - n), n,
                m_envPeak[i] > 0.01f ? m_envPeak[i] : 1.0f))
                m_exprFlags[i] |= PianoExpr::VIBRATO;
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
}

void CPianoRoll::InvalidatePianoRollRegions(bool roll, bool key)
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
        InvalidateRect(&cr, FALSE);
        return;
    }
    if (roll)
        InvalidateRect(CRect(0, 0, w, rollH), FALSE);
    if (key)
        InvalidateRect(CRect(0, rollH, w, h), FALSE);
}

void CPianoRoll::BuildLiveNoteFrame(NoteFrame& frame) const
{
    for (int i = 0; i < KEY_COUNT; ++i) {
        frame.active[i] = m_activeKeys[i];
        frame.strength[i] = m_noteStrength[i];
        frame.segment[i] = m_segmentId[i];
        frame.bandMask[i] = m_bandMask[i];
        frame.expr[i] = m_exprFlags[i];
        memcpy(frame.laneStrength[i], m_laneStrength[i], sizeof(frame.laneStrength[i]));
        if (m_activeKeys[i]) {
            const float pk = m_envPeak[i] > 0.02f ? m_envPeak[i] : 1.0f;
            float dyn = m_noteStrength[i] / pk;
            if (dyn < 0.12f) dyn = 0.12f;
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
    if (!m_feedEnabled || m_paintDisabled) return;
    m_historyHead = (m_historyHead + (int)MAX_HISTORY - 1) % (int)MAX_HISTORY;
    BuildLiveNoteFrame(m_historyRing[m_historyHead]);
    if (m_historyCount < (int)MAX_HISTORY)
        ++m_historyCount;
    ++m_framesPending;

    for (int i = 0; i < KEY_COUNT; ++i) {
        if (m_activeKeys[i] != m_keySnapActive[i]) {
            MarkKeyVisualDirty();
            break;
        }
    }

    if (requestUiInvalidate)
        InvalidatePianoRollRegions(true, false);
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
        static constexpr int kKeys = 88;
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
        CPen penTL(PS_SOLID, 1, topLeft);
        CPen penBR(PS_SOLID, 1, botRight);
        CPen* pOld = dc.SelectObject(&penTL);
        dc.MoveTo(rc.left, rc.bottom - 1); dc.LineTo(rc.left, rc.top); dc.LineTo(rc.right - 1, rc.top);
        dc.SelectObject(&penBR);
        dc.MoveTo(rc.left, rc.bottom - 1); dc.LineTo(rc.right - 1, rc.bottom - 1); dc.LineTo(rc.right - 1, rc.top);
        dc.SelectObject(pOld);
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
        default:                 return L"?";
        }
    }

    static int CountExprFlags(uint8_t expr)
    {
        int n = 0;
        for (uint8_t f = 1; f <= PianoExpr::SUSTAIN; f <<= 1)
            if (expr & f) ++n;
        return n;
    }

    static void DrawExprBadgePanel(CDC& dc, CRect rc, uint8_t flag, CFont* pFont)
    {
        if (rc.Width() < 4 || rc.Height() < 4 || !pFont) return;
        const COLORREF fg = ExprColorForFlag(flag);
        dc.FillSolidRect(&rc, RGB(14, 14, 20));
        CPen border(PS_SOLID, 1, fg);
        CPen* pOldPen = dc.SelectObject(&border);
        dc.MoveTo(rc.left, rc.bottom - 1); dc.LineTo(rc.left, rc.top); dc.LineTo(rc.right - 1, rc.top);
        dc.LineTo(rc.right - 1, rc.bottom - 1); dc.LineTo(rc.left, rc.bottom - 1);
        dc.SelectObject(pOldPen);

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
            PianoExpr::SLIDE, PianoExpr::VIBRATO, PianoExpr::SUSTAIN
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

        int symH = vertical
            ? max(nFlags * 5, min(spaceAbove, nFlags * 7 + 2))
            : max(8, min(spaceAbove, max(10, rowPitch + 4)));
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
        for (int y = rc.top; y < rc.bottom; ++y) {
            const int off = (int)(sin((y - rc.top) * 0.75) * amp);
            const int x = xBase + off;
            if (x >= rc.left && x < rc.right)
                dc.SetPixel(x, y, col);
        }
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
        if (labelW > 0 && row.Height() >= 4) {
            CFont f; f.CreateFont(-max(7, min(11, row.Height() - 1)), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            CFont* pOld = dc.SelectObject(&f);
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
    const int symPx = max(10, min(20, max(clientW / 38, rowPitch + 6)));
    const int symCompactPx = max(6, min(10, lanePx + 1));
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
}

void CPianoRoll::GetExprLegendPanelRect(int rollW, int rollH, CRect& panel) const
{
    panel.SetRectEmpty();
    if (rollW < 72 || rollH < 48) return;

    static const int kItemCount = 6;
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

void CPianoRoll::DrawExprLegend(CDC& dc, int rollW, int rollH) const
{
    using namespace PianoDraw;
    CRect panel;
    GetExprLegendPanelRect(rollW, rollH, panel);
    if (panel.IsRectEmpty()) return;
    if (!m_paintFontsReady || !m_fontExprLegend.GetSafeHandle() || !m_fontExprSymbol.GetSafeHandle())
        return;

    static const uint8_t kFlags[] = {
        PianoExpr::ACCENT, PianoExpr::SCOOP, PianoExpr::FALL,
        PianoExpr::SLIDE, PianoExpr::VIBRATO, PianoExpr::SUSTAIN
    };
    static const wchar_t* kLabels[] = {
        LL14(L"アクセント", L"Accent", L"Accent", L"Accento", L"Acento", L"액센트", L"重音", L"لهجة", L"Акцент", L"Akzent", L"Acento", L"Accent", L"Akcent", L"Aksan"),
        LL14(L"スクープ", L"Scoop", L"Scoop", L"Scoop", L"Scoop", L"스쿱", L"滑音(上)", L"Scoop", L"Скуп", L"Scoop", L"Scoop", L"Scoop", L"Scoop", L"Scoop"),
        LL14(L"フォール", L"Fall", L"Chute", L"Fall", L"Caída", L"하강", L"滑音(下)", L"Fall", L"Падение", L"Fall", L"Queda", L"Fall", L"Spadek", L"Düşüş"),
        LL14(L"スライド", L"Slide", L"Glissé", L"Slide", L"Desliz", L"슬라이드", L"滑音", L"Slide", L"Слайд", L"Slide", L"Slide", L"Slide", L"Slide", L"Slide"),
        LL14(L"ビブラート", L"Vibrato", L"Vibrato", L"Vibrato", L"Vibrato", L"비브라토", L"颤音", L"Vibrato", L"Вибрато", L"Vibrato", L"Vibrato", L"Vibrato", L"Wibrato", L"Vibrato"),
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

    dc.FillSolidRect(&panel, RGB(14, 14, 20));
    CPen border(PS_SOLID, 1, RGB(70, 70, 82));
    CPen* pOldPen = dc.SelectObject(&border);
    dc.MoveTo(panel.left, panel.bottom - 1); dc.LineTo(panel.left, panel.top);
    dc.LineTo(panel.right - 1, panel.top); dc.LineTo(panel.right - 1, panel.bottom - 1);
    dc.LineTo(panel.left, panel.bottom - 1);
    dc.SelectObject(pOldPen);

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
    CPen gridPen(PS_SOLID, 1, RGB(34, 34, 34));
    CPen* pOldPen = dc.SelectObject(&gridPen);
    for (int i = 1; i < KEY_COUNT; ++i) {
        int xL, xR; GetChromaticKeyRect(i, width, xL, xR);
        dc.MoveTo(xL, yFrom);
        dc.LineTo(xL, yTo);
    }
    dc.SelectObject(pOldPen);
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
}

void CPianoRoll::ComposeRollBuffer(CDC& dc, int width, int rollH,
    int histCount, const NoteFrame* hist, const NoteFrame& live) const
{
    DrawHistoryArea(dc, width, rollH, histCount, hist);
    DrawPlayheadRow(dc, width, rollH, live);
    DrawExprLegend(dc, width, rollH);
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

bool CPianoRoll::TryAdvanceRollBuffer(int width, int rollH, int histCount, const NoteFrame* hist,
    int pendingCount, const NoteFrame& live)
{
    (void)pendingCount;
    (void)histCount;
    (void)hist;
    m_lastScrollPx = 0;
    m_lastScrollHealTop = 0;
    if (!m_rollReady || rollH <= 0)
        return false;
    if (!m_rollDC.GetSafeHdc() || !m_rollScratchDC.GetSafeHdc())
        return false;

    const int scrollPx = HistoryScrollPx(rollH, 1);
    const int preserveH = rollH - scrollPx;
    if (scrollPx <= 0 || preserveH <= 0)
        return false;

    int yTop0, yBot0;
    GetHistoryRowBounds(rollH, 0, yTop0, yBot0);
    if (yBot0 <= yTop0)
        return false;

    // --- 差分スクロール ---
    // A) 履歴ピクセル [0,preserveH) を scrollPx 分繰り上げ（グリッド・バーごと保持）
    m_rollScratchDC.BitBlt(0, 0, width, preserveH, &m_rollDC, 0, scrollPx, SRCCOPY);

    // B) 空いた下端 [yTop0,rollH) のみクリア → live 行
    m_rollScratchDC.FillSolidRect(0, yTop0, width, rollH - yTop0, RGB(20, 20, 20));
    DrawHistoryGrid(m_rollScratchDC, width, yTop0, rollH);
    DrawPlayheadRow(m_rollScratchDC, width, rollH, live);

    m_rollDC.BitBlt(0, 0, width, rollH, &m_rollScratchDC, 0, 0, SRCCOPY);
    DrawExprLegend(m_rollDC, width, rollH);
    m_lastScrollPx = scrollPx;
    m_lastScrollHealTop = 0;
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
    if (InterlockedCompareExchange(&m_syncPosted, 1, 0) != 0) return;
    PostMessage(WM_PIANOROLL_SYNC, 0, 0);
}

void CPianoRoll::ApplySyncInvalidate()
{
    if (m_paintDisabled || !::IsWindow(m_hWnd)) return;
    if (m_meterDirty)
        m_keyDirty = true;
    Invalidate(FALSE);
}

LRESULT CPianoRoll::OnSyncRequest(WPARAM, LPARAM)
{
    InterlockedExchange(&m_syncPosted, 0);
    if (m_paintDisabled || !::IsWindow(m_hWnd)) return 0;
    COggDlg_SyncPianoRollFast();
    ApplySyncInvalidate();
    return 0;
}

LRESULT CPianoRoll::OnAnalysisDone(WPARAM, LPARAM)
{
    if (m_paintDisabled || !::IsWindow(m_hWnd)) return 0;
    ApplySyncInvalidate();
    return 0;
}

DWORD WINAPI CPianoRoll::AnalysisWorkerThreadEntry(LPVOID param)
{
    return static_cast<CPianoRoll*>(param)->AnalysisWorkerLoop();
}

void CPianoRoll::StartAnalysisWorker()
{
    if (m_hAnalysisThread) return;
    InterlockedExchange(&m_workerStop, 0);
    InterlockedExchange(&m_jobPending, 0);
    m_hAnalysisWake = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!m_hAnalysisWake) return;
    m_hAnalysisThread = CreateThread(
        NULL, 0, AnalysisWorkerThreadEntry, this, 0, NULL);
    if (!m_hAnalysisThread) {
        CloseHandle(m_hAnalysisWake);
        m_hAnalysisWake = NULL;
    }
}

void CPianoRoll::StopAnalysisWorker()
{
    if (!m_hAnalysisThread && !m_hAnalysisWake) return;
    InterlockedExchange(&m_workerStop, 1);
    if (m_hAnalysisWake)
        SetEvent(m_hAnalysisWake);
    if (m_hAnalysisThread) {
        WaitForSingleObject(m_hAnalysisThread, 10000);
        CloseHandle(m_hAnalysisThread);
        m_hAnalysisThread = NULL;
    }
    if (m_hAnalysisWake) {
        CloseHandle(m_hAnalysisWake);
        m_hAnalysisWake = NULL;
    }
}

DWORD CPianoRoll::AnalysisWorkerLoop()
{
    for (;;) {
        const DWORD wait = WaitForSingleObject(m_hAnalysisWake, INFINITE);
        if (wait != WAIT_OBJECT_0)
            continue;
        if (InterlockedCompareExchange(&m_workerStop, 0, 0) != 0)
            break;

        bool didWork = false;
        while (InterlockedCompareExchange(&m_jobPending, 0, 1) == 1) {
            if (ProcessAnalysisJob())
                didWork = true;
        }

        if (didWork && ::IsWindow(m_hWnd))
            PostMessage(WM_PIANOROLL_ANALYSIS_DONE, 0, 0);
    }
    return 0;
}

bool CPianoRoll::ProcessAnalysisJob()
{
    double localMono[PIANO_BASS_FRAMES];
    int frameCount = 0;
    int sampleRate = 44100;

    EnterCriticalSection(&m_jobCs);
    frameCount = m_jobFrameCount;
    sampleRate = m_jobSampleRate;
    if (frameCount > PIANO_BASS_FRAMES) frameCount = PIANO_BASS_FRAMES;
    if (frameCount > 0)
        memcpy(localMono, m_jobMono, (size_t)frameCount * sizeof(double));
    LeaveCriticalSection(&m_jobCs);

    if (frameCount < WIN_LOW || sampleRate < 8000)
        return false;

    const double* lowWin = localMono + (frameCount - WIN_LOW);
    const int bassLen = (frameCount >= WIN_BASS) ? WIN_BASS : WIN_LOW;
    const double* bassWin = (frameCount >= WIN_BASS)
        ? localMono + (frameCount - WIN_BASS)
        : lowWin;

    EnterCriticalSection(&m_cs);
    EnsureAnalysisTables(sampleRate);
    RunGoertzelFromBuffer(lowWin, bassWin, bassLen);
    LeaveCriticalSection(&m_cs);
    return true;
}

void CPianoRoll::DrawKeyboardToBuffer(CDC& memDC, int width, int keySectionH, int keyH,
    const bool* activesCopy, const uint8_t* bandMaskCopy, const float laneStrengthCopy[KEY_COUNT][3],
    const float* chFillCopy, int chCountCopy) const
{
    using namespace PianoDraw;

    const int bkH = keyH * 52 / 100;
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
        CPen sepPen(PS_SOLID, 1, RGB(90, 90, 95));
        CPen* pOldPen = memDC.SelectObject(&sepPen);
        memDC.MoveTo(0, splitY); memDC.LineTo(width, splitY);
        memDC.MoveTo(0, 0); memDC.LineTo(width, 0);
        memDC.SelectObject(pOldPen);
    }

    if (chCountCopy > 0 && labelH >= 4) {
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

void CPianoRoll::OnPaint()
{
    CPaintDC dc(this);
    if (m_paintDisabled) return;
    CRect rect;
    GetClientRect(&rect);
    const int w = rect.Width();
    const int h = rect.Height();
    if (w <= 0 || h <= 0) return;

    int keyH = h * 20 / 100;
    if (keyH < 50) keyH = 50; if (keyH > 100) keyH = 100;
    const int rollH = h - keyH;
    const int keySectionH = h - rollH;
    if (rollH <= 0 || keySectionH <= 0) return;

    CRect clip;
    dc.GetClipBox(&clip);
    const bool clipRoll = clip.top < rollH;
    const bool clipKey = clip.bottom > rollH;

    EnsurePaintFonts(w, keyH, rollH);
    if (!EnsureRollBuffer(dc, w, rollH) || !EnsureKeyBuffer(dc, w, keySectionH))
        return;

    NoteFrame liveSnap;
    bool activesCopy[KEY_COUNT];
    uint8_t bandMaskCopy[KEY_COUNT];
    float laneStrengthCopy[KEY_COUNT][3];
    float chFillCopy[PIANO_METER_CH_MAX];
    int chCountCopy = 0;
    EnterCriticalSection(&m_cs);
    BuildLiveNoteFrame(liveSnap);
    memcpy(activesCopy, m_activeKeys, sizeof(m_activeKeys));
    memcpy(bandMaskCopy, m_bandMask, sizeof(m_bandMask));
    memcpy(laneStrengthCopy, m_laneStrength, sizeof(m_laneStrength));
    chCountCopy = m_chMeterCount;
    memcpy(chFillCopy, m_chMeterFill, sizeof(chFillCopy));
    LeaveCriticalSection(&m_cs);

    if (m_meterDirty)
        m_keyDirty = true;

    int pending = 0;
    NoteFrame histSnap[MAX_HISTORY];
    int histCount = 0;
    EnterCriticalSection(&m_cs);
    pending = m_framesPending;
    CopyHistorySnapshot(histSnap, MAX_HISTORY, histCount);
    LeaveCriticalSection(&m_cs);

    const bool rollDirty = m_historyDirty;
    const bool needKeyDraw = m_keyDirty || !m_keyBufReady;
    bool didRollUpdate = false;
    bool didRollScroll = false;
    bool needAnotherRollFrame = false;

    if (pending > 0 && m_rollReady
        && TryAdvanceRollBuffer(w, rollH, histCount, histSnap, pending, liveSnap)) {
        EnterCriticalSection(&m_cs);
        if (m_framesPending > 0) --m_framesPending;
        needAnotherRollFrame = (m_framesPending > 0);
        LeaveCriticalSection(&m_cs);
        m_rollScrollValid = true;
        m_rollReady = true;
        didRollUpdate = true;
        didRollScroll = true;
    }
    else if (pending > 0 || rollDirty || !m_rollReady) {
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
        DrawKeyboardToBuffer(m_keyDC, w, keySectionH, keyH, activesCopy, bandMaskCopy, laneStrengthCopy, chFillCopy, chCountCopy);
        m_keyBufReady = true;
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
            if (m_rollReady && didRollUpdate)
                m_chromaCache.UpdateRect(m_rollDC.GetSafeHdc(), 0, 0, 0, 0, w, rollH, PIANO_CHROMA_KEY);
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

    if (didRollUpdate)
        m_historyDirty = false;
    if (clipKey || needKeyDraw) {
        m_keyDirty = false;
        m_meterDirty = false;
    }
    if (needAnotherRollFrame)
        Invalidate(FALSE);
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
    if (nType != SIZE_MINIMIZED && CCC_IsAeroEnabled())
        ApplyDwmBlur();
#endif
    Invalidate(FALSE);
}

void CPianoRoll::OnMove(int x, int y)
{
    CCustomBlurDialogExBase::OnMove(x, y);
    // ピアノロールは高頻度更新のため Move 毎の DWM 再合成は行わない（重い）
}

void CPianoRoll::OnShowWindow(BOOL bShow, UINT nStatus)
{
    CCustomBlurDialogExBase::OnShowWindow(bShow, nStatus);
#if CCUSTOM_AERO_SUPPORT
    if (bShow && CCC_IsAeroEnabled())
    {
        ApplyDwmBlur();
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
