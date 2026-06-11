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
#include <algorithm>

extern save savedata;
IMPLEMENT_DYNAMIC(CPianoRoll, CCustomBlurDialogExBase)

namespace Cfg
{
    // piano roll3: 基音ピック + NormalizeDisplayPeak + 包絡ホールド
    static constexpr float IIR_ALPHA = 0.40f;
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
    static constexpr float BASS_LATCH_RATIO = 0.21f;

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
        const float scale = 1.90f - t * 1.45f;
        int f = (int)(baseFrames * scale + 0.5f);
        if (f < 2) f = 2;
        if (f > 20) f = 20;
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
        RefineToLocalPeaksInBand(st, bandPick, 88, bandStart, bandEnd, 1);
        PruneBandPicks(st, bandPick, bandStart, bandEnd,
            bandSpan, PRUNE_BAND_RATIO, PRUNE_TOP_RATIO);
        SuppressSubharmonicPicksInBand(st, bandPick, 88, bandStart, bandEnd);

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

}

CPianoRoll::CPianoRoll(CWnd* pParent)
    : CCustomBlurDialogExBase(IDD_PIANOROLL, pParent)
{
    InitializeCriticalSection(&m_cs);
    m_ring.assign(RING_SIZE, 0.0);
    m_analysisBuf.assign(WIN_LOW, 0.0);
    m_bassAnalysisBuf.assign(WIN_BASS, 0.0);

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
    memset(m_prevRawStrengths, 0, sizeof(m_prevRawStrengths));
    memset(m_onsetStrengths, 0, sizeof(m_onsetStrengths));
    memset(m_prevOnsetStrengths, 0, sizeof(m_prevOnsetStrengths));
    for (int i = 0; i < PIANO_METER_CH_MAX; ++i) {
        m_chMeterDb[i] = -60.0f;
        m_chMeterFill[i] = 0.0f;
        m_chMeterAutoPeak[i] = 0.02f;
    }
    m_history.resize(MAX_HISTORY);
    for (auto& f : m_history) {
        memset(f.active, 0, sizeof(f.active));
        memset(f.strength, 0, sizeof(f.strength));
        memset(f.segment, 0, sizeof(f.segment));
        memset(f.bandMask, 0, sizeof(f.bandMask));
        memset(f.laneStrength, 0, sizeof(f.laneStrength));
    }
}

CPianoRoll::~CPianoRoll()
{
    m_feedEnabled = false;
    DeleteCriticalSection(&m_cs);
}

void CPianoRoll::ResetPlaybackState()
{
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
    memset(m_smoothedStrengths, 0, sizeof(m_smoothedStrengths));
    memset(m_consecActive, 0, sizeof(m_consecActive));
    memset(m_consecSilent, 0, sizeof(m_consecSilent));
    memset(m_segmentId, 0, sizeof(m_segmentId));
    memset(m_envPeak, 0, sizeof(m_envPeak));
    memset(m_unpickedFrames, 0, sizeof(m_unpickedFrames));
    memset(m_strengthDipFrames, 0, sizeof(m_strengthDipFrames));
    memset(m_bandMask, 0, sizeof(m_bandMask));
    memset(m_laneStrength, 0, sizeof(m_laneStrength));
    m_analysisHasBass = false;
    m_historyDirty = true;
    if (!m_ring.empty())
        std::fill(m_ring.begin(), m_ring.end(), 0.0);
    for (auto& f : m_history) {
        memset(f.active, 0, sizeof(f.active));
        memset(f.strength, 0, sizeof(f.strength));
        memset(f.segment, 0, sizeof(f.segment));
        memset(f.bandMask, 0, sizeof(f.bandMask));
        memset(f.laneStrength, 0, sizeof(f.laneStrength));
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
END_MESSAGE_MAP()

BOOL CPianoRoll::OnInitDialog()
{
    CCustomBlurDialogExBase::OnInitDialog();
    SetWindowText(LL14(
        L"ピアノロール", L"Piano Roll", L"Rouleau piano", L"Rotolo pianoforte",
        L"Rollo de piano", L"피아노 롤", L"钢琴卷帘", L"لوحة البيانو",
        L"Пианоролл", L"Klavierrolle", L"Rolo de piano", L"Pianorol",
        L"Rolka pianina", L"Piyano rulosu"));

    ModifyStyle(WS_MINIMIZEBOX | WS_MAXIMIZEBOX, 0);
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
    SetTimer(1, 50, nullptr);
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

COLORREF CPianoRoll::BandNoteColor(int bandId, float strength, bool blackKey)
{
    const float st = min(strength / 3.0f, 1.0f);
    int r = 0, g = 0, b = 0;
    switch (bandId) {
    case 0: r = 48;  g = 130; b = 200; break;
    case 1: r = 42;  g = 168; b = 88;  break;
    default: r = 210; g = 175; b = 48;  break;
    }
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

    const DWORD now = GetTickCount();
    if (m_lastAnalyzeTick != 0 && (now - m_lastAnalyzeTick) < ANALYZE_MIN_MS)
        return;
    m_lastAnalyzeTick = now;

    EnterCriticalSection(&m_cs);
    EnsureAnalysisTables(sampleRate);
    const double* lowWin = mono + (frameCount - WIN_LOW);
    const int bassLen = (frameCount >= WIN_BASS) ? WIN_BASS : WIN_LOW;
    const double* bassWin = (frameCount >= WIN_BASS)
        ? mono + (frameCount - WIN_BASS)
        : lowWin;
    RunGoertzelFromBuffer(lowWin, bassWin, bassLen);
    LeaveCriticalSection(&m_cs);
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
            const float rate = (norm >= fill) ? kFillAttack : kFillRelease;
            fill += (norm - fill) * rate;
        }
        else {
            m_chMeterDb[i] = -60.0f;
            m_chMeterFill[i] *= 0.85f;
            m_chMeterAutoPeak[i] = 0.02f;
        }
    }
    LeaveCriticalSection(&m_cs);
    m_historyDirty = true;
    if (GetSafeHwnd())
        Invalidate(FALSE);
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

    for (int i = 0; i < KEY_COUNT; ++i)
    {
        double raw = 0.0;
        if (i < Cfg::BAND_BASS_END && hasBass) {
            raw = GoertzelMagnitude(
                m_bassAnalysisBuf.data(), WIN_BASS, m_goertzelCoeffs[i], m_hannBass.data());
        }
        else if (i < LOW_KEY_SPLIT) {
            raw = GoertzelMagnitude(
                m_analysisBuf.data(), WIN_LOW, m_goertzelCoeffs[i], m_hannLow.data());
        }
        else {
            raw = GoertzelMagnitude(
                m_analysisBuf.data() + (WIN_LOW - WIN_HIGH), WIN_HIGH,
                m_goertzelCoeffs[i], m_blackmanHigh.data());
        }
        m_rawStrengths[i] = (i < Cfg::BAND_BASS_END)
            ? (float)Cfg::ScaleGoertzelAmpD(raw, i)
            : ApplyDisplayScale((float)raw, i);
    }

    for (int i = 0; i < KEY_COUNT; ++i) {
        m_smoothedStrengths[i] =
            m_smoothedStrengths[i] * (1.0f - Cfg::IIR_ALPHA) + m_rawStrengths[i] * Cfg::IIR_ALPHA;
    }

    UpdateNoteStates();
    PushFrame();
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

    NormalizeDisplayPeak(pickStrength, KEY_COUNT, DISPLAY_PEAK_CAP);
    NormalizeDisplayPeak(trackStrength, KEY_COUNT, DISPLAY_PEAK_CAP);

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

    ApplyBandFundamentalPick(pickStrength, picked, 0, BAND_BASS_END,
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

        if (!effectivePicked && m_activeKeys[i]) {
            if (i < BAND_BASS_END && bassMax > 1e-6f &&
                trackStrength[i] >= bassMax * BASS_LATCH_RATIO)
                effectivePicked = true;
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
}

void CPianoRoll::PushFrame()
{
    NoteFrame frame;
    for (int i = 0; i < KEY_COUNT; ++i) {
        frame.active[i] = m_activeKeys[i];
        frame.strength[i] = m_noteStrength[i];
        frame.segment[i] = m_segmentId[i];
        frame.bandMask[i] = m_bandMask[i];
        memcpy(frame.laneStrength[i], m_laneStrength[i], sizeof(frame.laneStrength[i]));
    }
    m_history.insert(m_history.begin(), frame);
    if (m_history.size() > MAX_HISTORY) m_history.pop_back();
    m_historyDirty = true;
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

namespace
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

    static COLORREF LocalBandColor(int bandId, float strength, bool blackKey)
    {
        const float st = min(strength / 3.0f, 1.0f);
        int r = 0, g = 0, b = 0;
        switch (bandId) {
        case 0: r = 48;  g = 130; b = 200; break;
        case 1: r = 42;  g = 168; b = 88;  break;
        default: r = 210; g = 175; b = 48; break;
        }
        if (blackKey) { r = min(255, r + 25); g = max(0, g - 15); b = max(0, b - 10); }
        const int dim = (int)((1.0f - st * 0.65f) * 80.0f);
        r = max(0, min(255, r - dim)); g = max(0, min(255, g - dim)); b = max(0, min(255, b - dim));
        return RGB(r, g, b);
    }

    static void DrawLaneFill(CDC& dc, CRect rc, uint8_t bandMask, const float* laneStr,
        int fallbackBand, float fallbackStrength, bool blackKey)
    {
        if (rc.Width() <= 0 || rc.Height() <= 0) return;
        int laneCount = 0;
        for (int b = 0; b < 3; ++b) if (bandMask & (1u << b)) ++laneCount;
        if (laneCount <= 0) { dc.FillSolidRect(&rc, LocalBandColor(fallbackBand, fallbackStrength, blackKey)); return; }
        if (laneCount == 1) {
            int bandId = 0;
            for (int b = 0; b < 3; ++b) if (bandMask & (1u << b)) { bandId = b; break; }
            const float st = laneStr && laneStr[0] > 0.0f ? laneStr[0] : fallbackStrength;
            dc.FillSolidRect(&rc, LocalBandColor(bandId, st, blackKey)); return;
        }
        int slot = 0;
        for (int b = 0; b < 3; ++b) {
            if (!(bandMask & (1u << b))) continue;
            CRect sub = rc;
            const int w = rc.Width();
            sub.left = rc.left + (w * slot) / laneCount; sub.right = rc.left + (w * (slot + 1)) / laneCount;
            if (sub.right <= sub.left) sub.right = sub.left + 1;
            const float st = (laneStr && laneStr[slot] > 0.0f) ? laneStr[slot] : fallbackStrength;
            dc.FillSolidRect(&sub, LocalBandColor(b, st, blackKey)); ++slot;
        }
    }

    static void DrawLaneKey(CDC& dc, CRect rc, uint8_t bandMask, const float* laneStr,
        int fallbackBand, float fallbackStrength, bool blackKey, bool pressed)
    {
        if (rc.Width() <= 1 || rc.Height() <= 1) return;
        if (pressed) rc.OffsetRect(0, min(2, rc.Height() / 5));
        int laneCount = 0;
        for (int b = 0; b < 3; ++b) if (bandMask & (1u << b)) ++laneCount;
        if (laneCount <= 1) {
            int bandId = fallbackBand;
            for (int b = 0; b < 3; ++b) if (bandMask & (1u << b)) { bandId = b; break; }
            const float st = laneStr && laneStr[0] > 0.0f ? laneStr[0] : fallbackStrength;
            DrawBevelKey(dc, rc, LocalBandColor(bandId, st, blackKey), pressed); return;
        }
        int slot = 0;
        for (int b = 0; b < 3; ++b) {
            if (!(bandMask & (1u << b))) continue;
            CRect sub = rc;
            sub.top = rc.top + (rc.Height() * slot) / laneCount; sub.bottom = rc.top + (rc.Height() * (slot + 1)) / laneCount;
            if (sub.bottom <= sub.top) sub.bottom = sub.top + 1;
            const float st = (laneStr && laneStr[slot] > 0.0f) ? laneStr[slot] : fallbackStrength;
            DrawBevelKey(dc, sub, LocalBandColor(b, st, blackKey), pressed); ++slot;
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

void CPianoRoll::OnPaint()
{
    CPaintDC dc(this);
    CRect rect;
    GetClientRect(&rect);
    if (rect.Width() <= 0 || rect.Height() <= 0) return;

    CDC memDC; memDC.CreateCompatibleDC(&dc);
    CBitmap memBmp; memBmp.CreateCompatibleBitmap(&dc, rect.Width(), rect.Height());
    CBitmap* pOld = memDC.SelectObject(&memBmp);
    memDC.FillSolidRect(&rect, RGB(20, 20, 20));

    int keyH = rect.Height() * 20 / 100;
    if (keyH < 50)keyH = 50; if (keyH > 100)keyH = 100;
    const int rollH = rect.Height() - keyH;

    std::vector<NoteFrame> histCopy;
    bool activesCopy[KEY_COUNT];
    uint8_t bandMaskCopy[KEY_COUNT];
    float laneStrengthCopy[KEY_COUNT][3];
    float chFillCopy[PIANO_METER_CH_MAX];
    int chCountCopy = 0;
    {
        EnterCriticalSection(&m_cs);
        histCopy = m_history;
        memcpy(activesCopy, m_activeKeys, sizeof(m_activeKeys));
        memcpy(bandMaskCopy, m_bandMask, sizeof(m_bandMask));
        memcpy(laneStrengthCopy, m_laneStrength, sizeof(m_laneStrength));
        chCountCopy = m_chMeterCount;
        memcpy(chFillCopy, m_chMeterFill, sizeof(chFillCopy));
        LeaveCriticalSection(&m_cs);
    }

    {
        CPen gridPen(PS_SOLID, 1, RGB(34, 34, 34));
        CPen* pOldPen = memDC.SelectObject(&gridPen);
        for (int i = 1; i < KEY_COUNT; ++i) {
            int xL, xR; GetChromaticKeyRect(i, rect.Width(), xL, xR);
            memDC.MoveTo(xL, 0); memDC.LineTo(xL, rollH);
        }
        memDC.SelectObject(pOldPen);
    }

    for (size_t r = 0; r < histCopy.size(); ++r) {
        for (int i = 0; i < KEY_COUNT; ++i) {
            if (!histCopy[r].active[i]) continue;
            const int midi = MIDI_BASE + i;
            int xL, xR; GetChromaticKeyRect(i, rect.Width(), xL, xR);
            const int yTop = (int)((MAX_HISTORY - 1 - r) * (float)rollH / MAX_HISTORY);
            const int yBot = (int)((MAX_HISTORY - r) * (float)rollH / MAX_HISTORY);
            const uint8_t bMask = histCopy[r].bandMask[i] ? histCopy[r].bandMask[i] : (uint8_t)(1u << KeyBandIndex(i));
            DrawLaneFill(memDC, CRect(xL + 1, yTop, xR - 1, yBot), bMask, histCopy[r].laneStrength[i],
                KeyBandIndex(i), histCopy[r].strength[i], IsBlackKey(midi));
        }
    }

    const int bkH = keyH * 52 / 100;
    const int labelH = min(16, keyH / 4);
    const int keyTop = rollH + labelH + 2;
    const int splitY = keyTop + bkH;
    const COLORREF whiteFace = RGB(238, 238, 238);
    const COLORREF blackFace = RGB(28, 28, 34);
    memDC.FillSolidRect(CRect(0, rollH, rect.Width(), rect.Height()), RGB(150, 150, 155));

    for (int i = 0; i < KEY_COUNT; ++i) {
        const int midi = MIDI_BASE + i; if (IsBlackKey(midi)) continue;
        int xL, xR; GetWhiteKeyRect52(midi, rect.Width(), xL, xR);
        CRect kc(xL, splitY, xR, rect.Height());
        const bool on = activesCopy[i];
        if (on) {
            const uint8_t bMask = bandMaskCopy[i] ? bandMaskCopy[i] : (uint8_t)(1u << KeyBandIndex(i));
            DrawLaneKey(memDC, CRect(kc.left + 1, kc.top, kc.right - 1, kc.bottom), bMask, laneStrengthCopy[i], KeyBandIndex(i), 2.5f, false, on);
        }
        else { DrawBevelKey(memDC, CRect(kc.left + 1, kc.top, kc.right - 1, kc.bottom), whiteFace, false); }
    }
    for (int i = 0; i < KEY_COUNT; ++i) {
        const int midi = MIDI_BASE + i; if (IsBlackKey(midi)) continue;
        int xL, xR; GetChromaticKeyRect(i, rect.Width(), xL, xR);
        CRect kc(xL + 1, keyTop, xR - 1, splitY);
        const bool on = activesCopy[i];
        if (on) {
            const uint8_t bMask = bandMaskCopy[i] ? bandMaskCopy[i] : (uint8_t)(1u << KeyBandIndex(i));
            DrawLaneKey(memDC, kc, bMask, laneStrengthCopy[i], KeyBandIndex(i), 2.5f, false, on);
        }
        else { DrawBevelKey(memDC, kc, whiteFace, false); }
    }
    for (int i = 0; i < KEY_COUNT; ++i) {
        const int midi = MIDI_BASE + i; if (!IsBlackKey(midi)) continue;
        int xL, xR; GetChromaticKeyRect(i, rect.Width(), xL, xR);
        CRect kc(xL + 1, keyTop, xR - 1, splitY);
        const bool on = activesCopy[i];
        if (on) {
            const uint8_t bMask = bandMaskCopy[i] ? bandMaskCopy[i] : (uint8_t)(1u << KeyBandIndex(i));
            DrawLaneKey(memDC, kc, bMask, laneStrengthCopy[i], KeyBandIndex(i), 2.5f, true, on);
        }
        else { DrawBevelKey(memDC, kc, blackFace, false); }
    }

    {
        CPen sepPen(PS_SOLID, 1, RGB(90, 90, 95));
        CPen* pOldPen = memDC.SelectObject(&sepPen);
        memDC.MoveTo(0, splitY); memDC.LineTo(rect.Width(), splitY);
        memDC.MoveTo(0, rollH);  memDC.LineTo(rect.Width(), rollH);
        memDC.SelectObject(pOldPen);
    }

    if (chCountCopy > 0 && labelH >= 4) {
        CRect meterStrip(2, rollH + 1, rect.Width() - 2, rollH + labelH + 1);
        DrawChannelDbBars(memDC, meterStrip, chFillCopy, chCountCopy);
    }

    {
        CFont noteFont, octFont;
        noteFont.CreateFont(-max(9, min(14, rect.Width() / 52)), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        octFont.CreateFont(-max(8, min(12, rect.Width() / 52)), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        memDC.SetBkMode(TRANSPARENT);
        CFont* pOldFont = memDC.SelectObject(&noteFont);
        memDC.SetTextColor(RGB(70, 70, 75));
        for (int i = 0; i < KEY_COUNT; ++i) {
            const int midi = MIDI_BASE + i;
            const wchar_t* name = WhiteKeyLabel(midi); if (!name) continue;
            int xL, xR; GetWhiteKeyRect52(midi, rect.Width(), xL, xR);
            CRect tr(xL + 2, rect.Height() - labelH - 2, xR - 2, rect.Height() - 2);
            memDC.DrawText(name, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        memDC.SelectObject(&octFont);
        memDC.SetTextColor(RGB(100, 100, 110));
        for (int i = 0; i < KEY_COUNT; ++i) {
            const int midi = MIDI_BASE + i; if (midi % 12 != 0) continue;
            int xL, xR; GetWhiteKeyRect52(midi, rect.Width(), xL, xR);
            CString oct; oct.Format(L"%d", MidiOctaveNumber(midi));
            CRect tr(xL + 2, rollH + 1, xR - 2, rollH + labelH + 1);
            memDC.DrawText(oct, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        memDC.SelectObject(pOldFont);
    }

#if CCUSTOM_AERO_SUPPORT
    if (savedata.aero == 1 && CCC_IsWin11())
        CCC_BlitChromaNoFlicker(dc.GetSafeHdc(), 0, 0, rect.Width(), rect.Height(),
            memDC.GetSafeHdc(), 0, 0, RGB(20, 20, 20));
    else
#endif
        dc.BitBlt(0, 0, rect.Width(), rect.Height(), &memDC, 0, 0, SRCCOPY);
    memDC.SelectObject(pOld);
    m_historyDirty = false;
}

void CPianoRoll::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == 1) {
        if (m_historyDirty) Invalidate(FALSE);
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
#if CCUSTOM_AERO_SUPPORT
    if (nType != SIZE_MINIMIZED && CCC_IsAeroEnabled())
        ApplyDwmBlur();
#endif
    Invalidate(FALSE);
}

void CPianoRoll::OnMove(int x, int y)
{
    CCustomBlurDialogExBase::OnMove(x, y);
#if CCUSTOM_AERO_SUPPORT
    if (CCC_IsAeroEnabled())
        CCC_RefreshDialogDwmBlur(m_hWnd);
#endif
}

void CPianoRoll::OnShowWindow(BOOL bShow, UINT nStatus)
{
    CCustomBlurDialogExBase::OnShowWindow(bShow, nStatus);
#if CCUSTOM_AERO_SUPPORT
    if (bShow && CCC_IsAeroEnabled())
    {
        ApplyDwmBlur();
        Invalidate(FALSE);
    }
#endif
}

void CPianoRoll::OnClose()
{
    m_feedEnabled = false;
    savedata.pianorollwindow = 0;
    DestroyWindow();
}

BOOL CPianoRoll::PreTranslateMessage(MSG* pMsg)
{
    return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}
