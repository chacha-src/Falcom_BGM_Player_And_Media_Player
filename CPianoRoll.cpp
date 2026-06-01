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

IMPLEMENT_DYNAMIC(CPianoRoll, CCustomDialogEx)

namespace Cfg
{
    static constexpr float IIR_ALPHA            = 0.40f;
    static constexpr float SILENCE_ABS          = 0.007f;
    static constexpr float BAND_SILENCE_BASS    = 0.005f;
    static constexpr float BAND_SILENCE_MID     = 0.004f;
    static constexpr float BAND_SILENCE_TRE     = 0.004f;
    static constexpr int   ATTACK_FRAMES        = 1;
    static constexpr int   RELEASE_FRAMES       = 7;
    static constexpr int   VIS_GAP_FRAMES       = 6;
    static constexpr float RETRIGGER_RATIO      = 0.32f;
    static constexpr int   BAND_BASS_END        = 25;
    static constexpr int   BAND_MID_END         = 53;
    static constexpr float BASS_PICK_THRESH     = 0.18f;
    static constexpr float MID_PICK_THRESH      = 0.17f;
    static constexpr float TRE_PICK_THRESH      = 0.15f;
    static constexpr int   BASS_PICK_POOL        = 6;
    static constexpr int   PICK_POOL_MAX        = 16;
    static constexpr float PRUNE_BAND_RATIO     = 0.08f;
    static constexpr float PRUNE_TOP_RATIO      = 0.13f;
    static constexpr float HOLD_ENV_BASS        = 0.36f;
    static constexpr float HOLD_ENV_MID         = 0.22f;
    static constexpr float HOLD_ENV_TRE         = 0.20f;
    static constexpr float DISPLAY_PEAK_CAP     = 5.0f;
    static constexpr int   ANALYZE_INTERVAL     = 1024;

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

    // 低い鍵ほど長い時間単位（release/gap/attack スケール）
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

    static void ApplyBandFundamentalPick(const float* st, bool* picked,
        int bandStart, int bandEnd, float relThresh, int maxNotes)
    {
        if (!st || !picked || bandStart >= bandEnd || maxNotes <= 0) return;

        bool bandPick[128];
        memset(bandPick, 0, sizeof(bandPick));
        PickFundamentalNotesToBand(st, bandPick, 88,
            bandStart, bandEnd, maxNotes, relThresh);
        SuppressSubharmonicPicksInBand(st, bandPick, 88, bandStart, bandEnd);
        RefineToLocalPeaksInBand(st, bandPick, 88, bandStart, bandEnd, 1);
        PruneBandPicks(st, bandPick, bandStart, bandEnd,
            maxNotes, PRUNE_BAND_RATIO, PRUNE_TOP_RATIO);

        for (int i = bandStart; i < bandEnd; ++i) {
            if (bandPick[i])
                picked[i] = true;
        }
    }

}

CPianoRoll::CPianoRoll(CWnd* pParent)
    : CCustomDialogEx(IDD_PIANOROLL, pParent)
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
    m_historyDirty = true;
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
    CCustomDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CPianoRoll, CCustomDialogEx)
    ON_WM_PAINT()
    ON_WM_TIMER()
    ON_WM_SIZE()
    ON_WM_MOVE()
    ON_WM_CLOSE()
END_MESSAGE_MAP()

BOOL CPianoRoll::OnInitDialog()
{
    CCustomDialogEx::OnInitDialog();
    SetWindowText(LL14(
        L"ピアノロール", L"Piano Roll", L"Rouleau piano", L"Rotolo pianoforte",
        L"Rollo de piano", L"피아노 롤", L"钢琴卷帘", L"لوحة البيانو",
        L"Пианоролл", L"Klavierrolle", L"Rolo de piano", L"Pianorol",
        L"Rolka pianina", L"Piyano rulosu"));

    ModifyStyle(WS_MINIMIZEBOX | WS_MAXIMIZEBOX, 0);

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
    case 0: r = 48;  g = 130; b = 200; break;  // 低音=青 (ベース)
    case 1: r = 42;  g = 168; b = 88;  break;  // 中音=緑 (ピアノ)
    default: r = 210; g = 175; b = 48;  break; // 高音=金 (ギター/メロディ)
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
    if (!m_feedEnabled || !mono || frameCount < WIN_LOW || sampleRate < 8000) return;

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

void CPianoRoll::RunGoertzelFromBuffer(const double* winLow8192,
    const double* winBass, int bassWinLen)
{
    if (!winLow8192) return;

    for (int i = 0; i < WIN_LOW; ++i)
        m_analysisBuf[i] = winLow8192[i];

    if (winBass && bassWinLen >= WIN_BASS) {
        for (int i = 0; i < WIN_BASS; ++i)
            m_bassAnalysisBuf[i] = winBass[i];
    }

    for (int i = 0; i < KEY_COUNT; ++i)
    {
        double raw = 0.0;
        if (i < Cfg::BAND_BASS_END && winBass && bassWinLen >= WIN_BASS) {
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
        BASS_PICK_THRESH, BASS_PICK_POOL);
    ApplyBandFundamentalPick(pickStrength, picked, BAND_BASS_END, BAND_MID_END,
        MID_PICK_THRESH, PICK_POOL_MAX);
    ApplyBandFundamentalPick(pickStrength, picked, BAND_MID_END, KEY_COUNT,
        TRE_PICK_THRESH, PICK_POOL_MAX);

    for (int i = 0; i < KEY_COUNT; ++i) {
        bassPick[i] = midPick[i] = treblePick[i] = false;
        if (!picked[i]) continue;
        if (i < BAND_BASS_END) bassPick[i] = true;
        else if (i < BAND_MID_END) midPick[i] = true;
        else treblePick[i] = true;
    }

    for (int i = 0; i < KEY_COUNT; ++i)
    {
        const float sigStrength = pickStrength[i];
        bool effectivePicked = picked[i];

        if (!effectivePicked && m_activeKeys[i]) {
            const float holdRatio = HoldEnvRatio(i);
            if (holdRatio > 0.0f && m_envPeak[i] > 0.001f &&
                trackStrength[i] >= m_envPeak[i] * holdRatio)
                effectivePicked = true;
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
    if (width <= 0) {
        xL = xR = 0;
        return;
    }
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
    if (width <= 0 || IsBlackKey(midi)) {
        xL = xR = 0;
        return;
    }
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

    static int MidiOctaveNumber(int midi)
    {
        return (midi / 12) - 1;
    }

    static void DrawBevelKey(CDC& dc, CRect rc, COLORREF fill, bool pressed)
    {
        if (rc.Width() <= 1 || rc.Height() <= 1) return;
        if (pressed)
            rc.OffsetRect(0, min(2, rc.Height() / 5));

        dc.FillSolidRect(&rc, fill);

        const COLORREF topLeft = pressed ? RGB(45, 45, 50) : RGB(255, 255, 255);
        const COLORREF botRight = pressed ? RGB(190, 190, 195) : RGB(110, 110, 115);
        CPen penTL(PS_SOLID, 1, topLeft);
        CPen penBR(PS_SOLID, 1, botRight);
        CPen* pOld = dc.SelectObject(&penTL);
        dc.MoveTo(rc.left, rc.bottom - 1);
        dc.LineTo(rc.left, rc.top);
        dc.LineTo(rc.right - 1, rc.top);
        dc.SelectObject(&penBR);
        dc.MoveTo(rc.left, rc.bottom - 1);
        dc.LineTo(rc.right - 1, rc.bottom - 1);
        dc.LineTo(rc.right - 1, rc.top);
        dc.SelectObject(pOld);
    }

    static COLORREF LocalBandColor(int bandId, float strength, bool blackKey)
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

    static void DrawLaneFill(CDC& dc, CRect rc, uint8_t bandMask, const float* laneStr,
        int fallbackBand, float fallbackStrength, bool blackKey)
    {
        if (rc.Width() <= 0 || rc.Height() <= 0) return;

        int laneCount = 0;
        for (int b = 0; b < 3; ++b)
            if (bandMask & (1u << b)) ++laneCount;

        if (laneCount <= 0) {
            dc.FillSolidRect(&rc, LocalBandColor(fallbackBand, fallbackStrength, blackKey));
            return;
        }
        if (laneCount == 1) {
            int bandId = 0;
            for (int b = 0; b < 3; ++b) {
                if (bandMask & (1u << b)) { bandId = b; break; }
            }
            const float st = laneStr && laneStr[0] > 0.0f ? laneStr[0] : fallbackStrength;
            dc.FillSolidRect(&rc, LocalBandColor(bandId, st, blackKey));
            return;
        }

        int slot = 0;
        for (int b = 0; b < 3; ++b) {
            if (!(bandMask & (1u << b))) continue;
            CRect sub = rc;
            const int w = rc.Width();
            sub.left = rc.left + (w * slot) / laneCount;
            sub.right = rc.left + (w * (slot + 1)) / laneCount;
            if (sub.right <= sub.left) sub.right = sub.left + 1;
            const float st = (laneStr && laneStr[slot] > 0.0f) ? laneStr[slot] : fallbackStrength;
            dc.FillSolidRect(&sub, LocalBandColor(b, st, blackKey));
            ++slot;
        }
    }

    static void DrawLaneKey(CDC& dc, CRect rc, uint8_t bandMask, const float* laneStr,
        int fallbackBand, float fallbackStrength, bool blackKey, bool pressed)
    {
        if (rc.Width() <= 1 || rc.Height() <= 1) return;
        if (pressed)
            rc.OffsetRect(0, min(2, rc.Height() / 5));

        int laneCount = 0;
        for (int b = 0; b < 3; ++b)
            if (bandMask & (1u << b)) ++laneCount;

        if (laneCount <= 1) {
            int bandId = fallbackBand;
            for (int b = 0; b < 3; ++b) {
                if (bandMask & (1u << b)) { bandId = b; break; }
            }
            const float st = laneStr && laneStr[0] > 0.0f ? laneStr[0] : fallbackStrength;
            DrawBevelKey(dc, rc, LocalBandColor(bandId, st, blackKey), pressed);
            return;
        }

        int slot = 0;
        for (int b = 0; b < 3; ++b) {
            if (!(bandMask & (1u << b))) continue;
            CRect sub = rc;
            sub.top = rc.top + (rc.Height() * slot) / laneCount;
            sub.bottom = rc.top + (rc.Height() * (slot + 1)) / laneCount;
            if (sub.bottom <= sub.top) sub.bottom = sub.top + 1;
            const float st = (laneStr && laneStr[slot] > 0.0f) ? laneStr[slot] : fallbackStrength;
            DrawBevelKey(dc, sub, LocalBandColor(b, st, blackKey), pressed);
            ++slot;
        }
    }
}

void CPianoRoll::OnPaint()
{
    CPaintDC dc(this);
    CRect rect;
    GetClientRect(&rect);
    if (rect.Width() <= 0 || rect.Height() <= 0) return;

    CDC memDC;
    memDC.CreateCompatibleDC(&dc);
    CBitmap memBmp;
    memBmp.CreateCompatibleBitmap(&dc, rect.Width(), rect.Height());
    CBitmap* pOld = memDC.SelectObject(&memBmp);

    memDC.FillSolidRect(&rect, RGB(20, 20, 20));

    int keyH = rect.Height() * 20 / 100;
    if (keyH < 50) keyH = 50;
    if (keyH > 100) keyH = 100;
    const int rollH = rect.Height() - keyH;

    std::vector<NoteFrame> histCopy;
    bool activesCopy[KEY_COUNT];
    uint8_t bandMaskCopy[KEY_COUNT];
    float laneStrengthCopy[KEY_COUNT][3];
    {
        EnterCriticalSection(&m_cs);
        histCopy = m_history;
        memcpy(activesCopy, m_activeKeys, sizeof(m_activeKeys));
        memcpy(bandMaskCopy, m_bandMask, sizeof(m_bandMask));
        memcpy(laneStrengthCopy, m_laneStrength, sizeof(m_laneStrength));
        LeaveCriticalSection(&m_cs);
    }

    // ロール部: 88鍵均等幅の縦グリッド
    {
        CPen gridPen(PS_SOLID, 1, RGB(34, 34, 34));
        CPen* pOldPen = memDC.SelectObject(&gridPen);
        for (int i = 1; i < KEY_COUNT; ++i) {
            int xL, xR;
            GetChromaticKeyRect(i, rect.Width(), xL, xR);
            memDC.MoveTo(xL, 0);
            memDC.LineTo(xL, rollH);
        }
        memDC.SelectObject(pOldPen);
    }

    // スクロール履歴（半音均等幅）
    for (size_t r = 0; r < histCopy.size(); ++r) {
        for (int i = 0; i < KEY_COUNT; ++i) {
            if (!histCopy[r].active[i]) continue;

            const int midi = MIDI_BASE + i;
            int xL, xR;
            GetChromaticKeyRect(i, rect.Width(), xL, xR);

            const int yTop = (int)((MAX_HISTORY - 1 - r) * (float)rollH / MAX_HISTORY);
            const int yBot = (int)((MAX_HISTORY - r) * (float)rollH / MAX_HISTORY);

            const uint8_t bMask = histCopy[r].bandMask[i] ? histCopy[r].bandMask[i]
                : (uint8_t)(1u << KeyBandIndex(i));
            DrawLaneFill(memDC,
                CRect(xL + 1, yTop, xR - 1, yBot),
                bMask, histCopy[r].laneStrength[i],
                KeyBandIndex(i), histCopy[r].strength[i], IsBlackKey(midi));
        }
    }

    // 鍵盤: 上側=88鍵均等幅（ロールと揃える） / 下側=52白鍵ピアノ配列
    const int bkH = keyH * 52 / 100;
    const int labelH = min(16, keyH / 4);
    const int keyTop = rollH + labelH + 2;
    const int splitY = keyTop + bkH;
    const COLORREF whiteFace = RGB(238, 238, 238);
    const COLORREF blackFace = RGB(28, 28, 34);

    memDC.FillSolidRect(CRect(0, rollH, rect.Width(), rect.Height()), RGB(150, 150, 155));

    // 下側: 白鍵のみ（C#列は描かず C/D がはみ出す）
    for (int i = 0; i < KEY_COUNT; ++i) {
        const int midi = MIDI_BASE + i;
        if (IsBlackKey(midi)) continue;

        int xL, xR;
        GetWhiteKeyRect52(midi, rect.Width(), xL, xR);
        CRect kc(xL, splitY, xR, rect.Height());
        const bool on = activesCopy[i];
        if (on) {
            const uint8_t bMask = bandMaskCopy[i] ? bandMaskCopy[i] : (uint8_t)(1u << KeyBandIndex(i));
            DrawLaneKey(memDC, CRect(kc.left + 1, kc.top, kc.right - 1, kc.bottom),
                bMask, laneStrengthCopy[i], KeyBandIndex(i), 2.5f, false, on);
        }
        else {
            DrawBevelKey(memDC, CRect(kc.left + 1, kc.top, kc.right - 1, kc.bottom), whiteFace, false);
        }
    }

    // 上側: 白鍵（均等幅の細い部分）
    for (int i = 0; i < KEY_COUNT; ++i) {
        const int midi = MIDI_BASE + i;
        if (IsBlackKey(midi)) continue;

        int xL, xR;
        GetChromaticKeyRect(i, rect.Width(), xL, xR);
        CRect kc(xL + 1, keyTop, xR - 1, splitY);
        const bool on = activesCopy[i];
        if (on) {
            const uint8_t bMask = bandMaskCopy[i] ? bandMaskCopy[i] : (uint8_t)(1u << KeyBandIndex(i));
            DrawLaneKey(memDC, kc, bMask, laneStrengthCopy[i], KeyBandIndex(i), 2.5f, false, on);
        }
        else {
            DrawBevelKey(memDC, kc, whiteFace, false);
        }
    }

    // 上側: 黒鍵（均等幅・上半分のみ）
    for (int i = 0; i < KEY_COUNT; ++i) {
        const int midi = MIDI_BASE + i;
        if (!IsBlackKey(midi)) continue;

        int xL, xR;
        GetChromaticKeyRect(i, rect.Width(), xL, xR);
        CRect kc(xL + 1, keyTop, xR - 1, splitY);
        const bool on = activesCopy[i];
        if (on) {
            const uint8_t bMask = bandMaskCopy[i] ? bandMaskCopy[i] : (uint8_t)(1u << KeyBandIndex(i));
            DrawLaneKey(memDC, kc, bMask, laneStrengthCopy[i], KeyBandIndex(i), 2.5f, true, on);
        }
        else {
            DrawBevelKey(memDC, kc, blackFace, false);
        }
    }

    {
        CPen sepPen(PS_SOLID, 1, RGB(90, 90, 95));
        CPen* pOldPen = memDC.SelectObject(&sepPen);
        memDC.MoveTo(0, splitY);
        memDC.LineTo(rect.Width(), splitY);
        memDC.MoveTo(0, rollH);
        memDC.LineTo(rect.Width(), rollH);
        memDC.SelectObject(pOldPen);
    }

    // 音名 (C～B) とオクターブ番号
    {
        CFont noteFont;
        noteFont.CreateFont(
            -max(9, min(14, rect.Width() / 52)), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        CFont octFont;
        octFont.CreateFont(
            -max(8, min(12, rect.Width() / 52)), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        memDC.SetBkMode(TRANSPARENT);

        CFont* pOldFont = memDC.SelectObject(&noteFont);
        memDC.SetTextColor(RGB(70, 70, 75));
        for (int i = 0; i < KEY_COUNT; ++i) {
            const int midi = MIDI_BASE + i;
            const wchar_t* name = WhiteKeyLabel(midi);
            if (!name) continue;

            int xL, xR;
            GetWhiteKeyRect52(midi, rect.Width(), xL, xR);
            CRect tr(xL + 2, rect.Height() - labelH - 2, xR - 2, rect.Height() - 2);
            memDC.DrawText(name, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        memDC.SelectObject(&octFont);
        memDC.SetTextColor(RGB(100, 100, 110));
        for (int i = 0; i < KEY_COUNT; ++i) {
            const int midi = MIDI_BASE + i;
            if (midi % 12 != 0) continue;

            int xL, xR;
            GetWhiteKeyRect52(midi, rect.Width(), xL, xR);
            CString oct;
            oct.Format(L"%d", MidiOctaveNumber(midi));
            CRect tr(xL + 2, rollH + 1, xR - 2, rollH + labelH + 1);
            memDC.DrawText(oct, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        memDC.SelectObject(pOldFont);
    }

    dc.BitBlt(0, 0, rect.Width(), rect.Height(), &memDC, 0, 0, SRCCOPY);
    memDC.SelectObject(pOld);
    m_historyDirty = false;
}

void CPianoRoll::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == 1) {
        if (m_historyDirty)
            Invalidate(FALSE);
        CRect rc;
        GetWindowRect(&rc);
        if (!IsIconic()) {
            savedata.pianorollx = rc.left;
            savedata.pianorolly = rc.top;
            savedata.pianorollw = rc.Width();
            savedata.pianorollh = rc.Height();
        }
    }
    CCustomDialogEx::OnTimer(nIDEvent);
}

void CPianoRoll::OnSize(UINT nType, int cx, int cy)
{
    CCustomDialogEx::OnSize(nType, cx, cy);
    Invalidate(FALSE);
}

void CPianoRoll::OnMove(int x, int y)
{
    CCustomDialogEx::OnMove(x, y);
}

void CPianoRoll::OnClose()
{
    m_feedEnabled = false;
    savedata.pianorollwindow = 0;
    DestroyWindow();
}

BOOL CPianoRoll::PreTranslateMessage(MSG* pMsg)
{
    return CCustomDialogEx::PreTranslateMessage(pMsg);
}
