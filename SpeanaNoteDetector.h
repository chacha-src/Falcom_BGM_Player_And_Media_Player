#pragma once
// ============================================================================
// SpeanaNoteDetector
//   COggDlg スペアナ音階モード用の 88 鍵ノート検出エンジン。
//   CPianoRoll の検出ロジック（基音ピック + 倍音サリエンス + 包絡ホールド）を
//   独立・自己完結の形で複製したもの。ピアノロール本体には一切依存しない。
//   ピック等の重い処理は共有ヘッダ（NoteFundamentalPick.h / PianoRollPick.h /
//   PianoKeyTable.h / PianoRollGoertzelAvx2.h）を再利用する。
//
//   使い方:
//     SpeanaNoteDetector det;
//     det.Configure(sampleRate);
//     det.Process(monoTail, frameCount);   // frameCount >= 8192 推奨(16384で低音最良)
//     const bool*  act = det.Active();      // [88]
//     const float* st  = det.Strength();    // [88] (アクティブ鍵のみ非0)
// ============================================================================
#include <cmath>
#include <cstring>
#include <vector>
#include "NoteFundamentalPick.h"
#include "PianoRollPick.h"
#include "PianoKeyTable.h"
#include "PianoRollGoertzelAvx2.h"

namespace SpndCfg
{
    static constexpr double SPND_PI = 3.14159265358979323846;
    static constexpr int   KEY_COUNT      = 88;
    static constexpr int   MIDI_BASE      = 21;
    static constexpr int   KEY_OFFSET     = 9;
    static constexpr int   DETECT_KEYS    = 108;
    static constexpr int   WIN_LOW        = 8192;
    static constexpr int   WIN_BASS       = 16384;
    static constexpr int   WIN_HIGH       = 4096;
    static constexpr int   WIN_ONSET      = 1024;
    static constexpr int   LOW_KEY_SPLIT  = 51;  // C5: これ以上は高音窓(4096)
    // Task2: テンポ180の32分音符(約42ms)対応の多重解像度分割。
    //  [0,MID_WIN_SPLIT)   = 16384窓(371ms)  深い低音〜低中音は音程優先
    //  [MID_WIN_SPLIT,51)  = 8192窓(186ms)   A3〜B4の旋律域は時間分解能を上げる
    //  [51,88)             = 4096窓(93ms)    C5以上
    // 物理(不確定性): 半音分離はC3〜B3で長窓が必須なため、深い音ほど時間分解能は犠牲。
    static constexpr int   MID_WIN_SPLIT  = 36;  // A3(MIDI57)未満は長窓据え置き

    static constexpr float IIR_ALPHA = 0.40f;
    static constexpr float IIR_ALPHA_BASS = 0.28f;
    static constexpr float SILENCE_ABS = 0.007f;
    static constexpr float BAND_SILENCE_BASS = 0.005f;
    static constexpr float BAND_SILENCE_MID = 0.004f;
    static constexpr float BAND_SILENCE_TRE = 0.004f;
    static constexpr int   ATTACK_FRAMES = 1;
    // Task2方針転換: 速い音符は「オンセット即時ON + 窓短縮」で捕捉し、
    // ノートオフはピアノロール実績値を維持して持続音の途切れを防ぐ。
    // (スペアナは描画間隔ms2でしか呼ばれず、ピアノロールの固定23msより遅い/不規則なため、
    //  フレーム基準のホールドを短くすると実時間で持続が足りず途切れる。)
    static constexpr int   RELEASE_FRAMES = 7;
    static constexpr int   VIS_GAP_FRAMES = 6;
    static constexpr int   VIS_GAP_FRAMES_BASS = 2;
    static constexpr float RETRIGGER_RATIO = 0.32f;
    static constexpr int   BAND_BASS_END = 25;
    static constexpr int   BAND_MID_END = 53;
    static constexpr int   BAND_MID_LO_END = 45;
    static constexpr float BASS_PICK_THRESH = 0.20f;
    static constexpr float MID_PICK_THRESH = 0.19f;
    static constexpr float TRE_PICK_THRESH = 0.16f;
    static constexpr float PRUNE_BAND_RATIO = 0.11f;
    static constexpr float PRUNE_TOP_RATIO = 0.17f;
    static constexpr float HOLD_ENV_BASS = 0.34f;
    static constexpr float HOLD_ENV_MID = 0.21f;
    static constexpr float HOLD_ENV_TRE = 0.19f;
    static constexpr float DISPLAY_PEAK_CAP = 5.0f;
    static constexpr float WEAK_BASS_RATIO = 0.17f;
    static constexpr float WEAK_MID_RATIO = 0.15f;
    static constexpr float WEAK_TRE_RATIO = 0.14f;
    static constexpr int   ONSET_KEY_START = 41;
    static constexpr float ONSET_DELTA_THRESH = 0.045f;
    static constexpr float ONSET_MIN_STRENGTH = 0.08f;
    static constexpr float BASS_ONSET_DELTA_THRESH = 0.034f;
    static constexpr float BASS_ONSET_MIN_STRENGTH = 0.055f;
    static constexpr float MID_ONSET_DELTA_THRESH = 0.040f;
    static constexpr float MID_ONSET_MIN_STRENGTH = 0.065f;

    static constexpr float BUFWAV3_TARGET_PEAK_DB = -11.0f;
    static constexpr float BUFWAV3_GAIN_DB_MAX = 32.0f;
    static constexpr float BUFWAV3_GAIN_ZERO_DB = -9.0f;
    static constexpr float BUFWAV3_PEAK_FLOOR_DB = -60.0f;

    inline void NormalizeBandPeak(float* values, int lo, int hi, float cap)
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

    inline void SuppressFalseSubharmonicPicks(const float* st, bool* picked,
        int bandStart, int bandEnd, bool polyMix)
    {
        if (!st || !picked || bandStart >= bandEnd) return;
        const float upRatio = polyMix ? 0.50f : 0.30f;
        for (int i = bandStart; i < bandEnd; ++i) {
            if (!picked[i]) continue;
            for (int hi = i + 1; hi < bandEnd; ++hi) {
                if (!PianoKey::IsHarmonicPair(hi, i) && !PianoKey::IsOctaveRelated(hi, i))
                    continue;
                if (!picked[hi] && st[hi] < st[i] * 0.18f) continue;
                if (st[hi] >= st[i] * upRatio) {
                    picked[i] = false;
                    break;
                }
            }
        }
    }

    inline void SnapPicksToLocalMaxima(const float* st, bool* picked,
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

    inline void StabilizeBassBandPicks(const float* st, bool* picked,
        int bandStart, int bandEnd, bool polyMix)
    {
        if (!st || !picked || bandStart >= bandEnd) return;
        SuppressFalseSubharmonicPicks(st, picked, bandStart, bandEnd, polyMix);
        SnapPicksToLocalMaxima(st, picked, bandStart, bandEnd, 2);
        CollapseNearbyPicks(st, picked, bandStart, bandEnd, 2, false);
    }

    inline float PeakDbFs(const double* samples, int n)
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

    inline float PeakDbFsWindows(const double* winLow, int nLow,
        const double* winBass, int nBass)
    {
        float db = PeakDbFs(winLow, nLow);
        if (winBass && nBass > 0) {
            const float dbB = PeakDbFs(winBass, nBass);
            if (dbB > db) db = dbB;
        }
        return db;
    }

    inline float QuieterHalfPeakDbFs(const double* samples, int n)
    {
        if (!samples || n < 512) return PeakDbFs(samples, n);
        const int half = n / 2;
        const float db0 = PeakDbFs(samples, half);
        const float db1 = PeakDbFs(samples + half, n - half);
        return (db0 < db1) ? db0 : db1;
    }

    inline float Bufwav3LevelDbForDynamics(const double* winLow, int nLow,
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

    inline float MakeupGainDbForBufwav3(float peakDbFs)
    {
        if (peakDbFs >= BUFWAV3_GAIN_ZERO_DB) return 0.0f;
        float g = BUFWAV3_TARGET_PEAK_DB - peakDbFs;
        if (g > BUFWAV3_GAIN_DB_MAX) g = BUFWAV3_GAIN_DB_MAX;
        if (g < 0.0f) g = 0.0f;
        return g;
    }

    inline float PickThreshScaleFromLevelDb(float levelDb)
    {
        if (levelDb < -32.0f) return 0.58f;
        if (levelDb < -26.0f) return 0.68f;
        if (levelDb < -22.0f) return 0.79f;
        if (levelDb < -18.0f) return 0.89f;
        if (levelDb < -14.0f) return 0.97f;
        if (levelDb < -11.0f) return 1.00f;
        return 1.02f;
    }

    inline void ApplyGainDbInPlace(double* samples, int n, float gainDb)
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

    inline float ApplyDisplayScale(float rawAmp, int keyIndex)
    {
        return ScaleGoertzelAmp(rawAmp, keyIndex + KEY_OFFSET, DETECT_KEYS);
    }

    inline float HoldEnvRatio(int keyIndex)
    {
        if (keyIndex < BAND_BASS_END) return HOLD_ENV_BASS;
        if (keyIndex < BAND_MID_END) return HOLD_ENV_MID;
        return HOLD_ENV_TRE;
    }

    inline int TemporalFrames(int keyIndex, int baseFrames)
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
        else if (keyIndex >= BAND_BASS_END)
            scale = 1.50f - t * 0.55f;
        else
            scale = 1.90f - t * 1.45f;
        int f = (int)(baseFrames * scale + 0.5f);
        if (keyIndex >= BAND_MID_END) {
            if (f < 1) f = 1;
            if (f > 12) f = 12;
        }
        else {
            if (keyIndex < BAND_BASS_END) {
                if (f < 1) f = 1;
                if (f > 8) f = 8;
            }
            else {
                if (f < 2) f = 2;
                if (f > 20) f = 20;
            }
        }
        return f;
    }

    inline int VisGapFrames(int keyIndex)
    {
        const int base = (keyIndex < BAND_BASS_END) ? VIS_GAP_FRAMES_BASS : VIS_GAP_FRAMES;
        return TemporalFrames(keyIndex, base);
    }

    inline bool IsMelodicNeighbor(int a, int b)
    {
        if (a == b) return false;
        const int lo = (a < b) ? a : b;
        const int hi = (a < b) ? b : a;
        const int d = hi - lo;
        if (d < 1 || d > 11) return false;
        return !PianoKey::IsHarmonicPair(hi, lo) && !PianoKey::IsOctaveRelated(hi, lo);
    }

    inline int CountSpectralPeaksInBand(const float* st, int lo, int hi, float relToMax)
    {
        if (!st || lo >= hi) return 0;
        const float bandMax = BandMaxStrength(st, lo, hi);
        if (bandMax < 1e-6f) return 0;
        const float minV = bandMax * relToMax;
        int n = 0;
        for (int i = lo; i < hi; ++i) {
            if (st[i] < minV) continue;
            const int jl = (i - 1 < lo) ? lo : (i - 1);
            const int jh = (i + 1 >= hi) ? (hi - 1) : (i + 1);
            bool isPeak = true;
            for (int j = jl; j <= jh; ++j) {
                if (j == i) continue;
                if (st[j] >= st[i]) { isPeak = false; break; }
            }
            if (isPeak) ++n;
        }
        return n;
    }

    inline bool FrameLooksPolyphonic(const float* st)
    {
        if (!st) return false;
        const int bassPeaks = CountSpectralPeaksInBand(st, 0, BAND_BASS_END, 0.17f);
        const int midPeaks = CountSpectralPeaksInBand(st, BAND_BASS_END, BAND_MID_END, 0.14f);
        const int trePeaks = CountSpectralPeaksInBand(st, BAND_MID_END, 88, 0.14f);
        int liveBands = 0;
        if (bassPeaks >= 2) ++liveBands;
        if (midPeaks >= 4) ++liveBands;
        if (trePeaks >= 4) ++liveBands;
        return liveBands >= 2 || (midPeaks >= 5 && (bassPeaks + trePeaks) >= 3);
    }

    inline bool FrameLooksLikeSingleSource(const bool* picked, int lo, int hi)
    {
        if (!picked || lo >= hi) return true;
        int picks[32];
        int n = 0;
        for (int i = lo; i < hi; ++i) {
            if (!picked[i]) continue;
            if (n < 32) picks[n++] = i;
        }
        if (n <= 1) return true;
        int root = picks[0];
        for (int k = 1; k < n; ++k)
            if (picks[k] < root) root = picks[k];
        int harmonic = 0;
        for (int k = 0; k < n; ++k) {
            if (picks[k] == root) continue;
            if (PianoKey::IsHarmonicPair(picks[k], root) || PianoKey::IsHarmonicPair(root, picks[k]) ||
                PianoKey::IsOctaveRelated(picks[k], root))
                ++harmonic;
        }
        return harmonic + 1 >= n || (float)(harmonic + 1) / (float)n >= 0.62f;
    }

    inline bool FrameLooksSparseDominant(const float* st, const bool* picked, int lo, int hi)
    {
        if (!st || !picked || lo >= hi) return false;
        int picks[32];
        int n = 0;
        float sum = 0.0f;
        for (int i = lo; i < hi; ++i) {
            if (!picked[i]) continue;
            if (n < 32) picks[n++] = i;
            sum += st[i];
        }
        if (n <= 1 || n > 8 || sum < 1e-6f) return false;
        int best = picks[0];
        float bestS = st[best];
        for (int k = 1; k < n; ++k) {
            if (st[picks[k]] > bestS) {
                bestS = st[picks[k]];
                best = picks[k];
            }
        }
        for (int k = 0; k < n; ++k) {
            if (picks[k] == best) continue;
            if (!PianoKey::IsHarmonicPair(picks[k], best) && !PianoKey::IsHarmonicPair(best, picks[k]) &&
                !PianoKey::IsOctaveRelated(picks[k], best))
                return false;
        }
        return bestS >= sum * 0.38f;
    }

    inline void ApplyBandFundamentalPick(const float* st, bool* picked,
        int bandStart, int bandEnd, float relThresh, bool polyFrame)
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
            StabilizeBassBandPicks(st, bandPick, bandStart, bandEnd, polyFrame);
        PruneBandPicks(st, bandPick, bandStart, bandEnd,
            bandSpan, PRUNE_BAND_RATIO, PRUNE_TOP_RATIO);
        SuppressSubharmonicPicksInBand(st, bandPick, 88, bandStart, bandEnd);
        if (bandStart == 0)
            StabilizeBassBandPicks(st, bandPick, bandStart, bandEnd, polyFrame);

        int bandPickCount = 0;
        for (int i = bandStart; i < bandEnd; ++i)
            if (bandPick[i]) ++bandPickCount;

        if (polyFrame && bandStart >= BAND_BASS_END) {
            const int minPoly = (bandSpan >= 10) ? 3 : 2;
            if (bandPickCount < minPoly) {
                bool extra[128];
                memset(extra, 0, sizeof(extra));
                PickAllFundamentalsInBand(st, extra, 88, bandStart, bandEnd,
                    0.58f, relThresh * 0.72f);
                for (int i = bandStart; i < bandEnd; ++i)
                    if (extra[i]) bandPick[i] = true;
            }
        }

        for (int i = bandStart; i < bandEnd; ++i) {
            if (bandPick[i])
                picked[i] = true;
        }
    }

    inline void SuppressWeakBandPicks(const float* strengths,
        bool* bassPick, bool* midPick, bool* treblePick, bool polyFrame)
    {
        if (!strengths || !bassPick || !midPick || !treblePick) return;

        const float bassR = WEAK_BASS_RATIO * (polyFrame ? 0.82f : 1.0f);
        const float midR = WEAK_MID_RATIO * (polyFrame ? 0.90f : 1.0f);
        const float treR = WEAK_TRE_RATIO * (polyFrame ? 0.92f : 1.0f);

        const float bassMax = BandMaxStrength(strengths, 0, BAND_BASS_END);
        const float midMax = BandMaxStrength(strengths, BAND_BASS_END, BAND_MID_END);
        const float treMax = BandMaxStrength(strengths, BAND_MID_END, 88);

        if (bassMax > 1e-6f) {
            const float minB = bassMax * bassR;
            for (int i = 0; i < BAND_BASS_END; ++i)
                if (bassPick[i] && strengths[i] < minB) bassPick[i] = false;
        }
        if (midMax > 1e-6f) {
            const float minM = midMax * midR;
            for (int i = BAND_BASS_END; i < BAND_MID_END; ++i)
                if (midPick[i] && strengths[i] < minM) midPick[i] = false;
        }
        if (treMax > 1e-6f) {
            const float minT = treMax * treR;
            for (int i = BAND_MID_END; i < 88; ++i)
                if (treblePick[i] && strengths[i] < minT) treblePick[i] = false;
        }
    }

    inline void ApplyBassHarmonicPick(const float* norm, const float* raw, bool* picked,
        int bandStart, int bandEnd, float relThresh)
    {
        if (!norm || !raw || picked == nullptr || bandStart >= bandEnd) return;

        const float rawBandMax = BandMaxStrength(raw, bandStart, bandEnd);
        if (rawBandMax < 0.0025f) return;

        float sal[128];
        memset(sal, 0, sizeof(sal));
        float salMax = 0.0f;
        for (int i = bandStart; i < bandEnd; ++i) {
            const float f = raw[i];
            if (f <= 0.0f) continue;
            const float h2 = (i + 12 < 88) ? raw[i + 12] : 0.0f;
            const float h3 = (i + 19 < 88) ? raw[i + 19] : 0.0f;
            const float h4 = (i + 24 < 88) ? raw[i + 24] : 0.0f;
            // 先頭にfを掛けて基音の存在を必須化（加算式だと上の倍音だけで低音ゴーストが出る）
            sal[i] = f * (f + h2 * 0.5f + h3 * 0.33f + h4 * 0.25f);
            if (sal[i] > salMax) salMax = sal[i];
        }
        if (salMax < 1e-9f) return;

        const float minSal = salMax * relThresh;
        for (int i = bandStart; i < bandEnd; ++i) {
            if (sal[i] < minSal) continue;
            const int jl = (i - 1 < bandStart) ? bandStart : (i - 1);
            const int jh = (i + 1 >= bandEnd) ? (bandEnd - 1) : (i + 1);
            bool isPeak = true;
            for (int j = jl; j <= jh; ++j) {
                if (j != i && sal[j] > sal[i]) { isPeak = false; break; }
            }
            if (isPeak)
                picked[i] = true;
        }
        CollapseNearbyPicks(norm, picked, bandStart, bandEnd, 2, false);
    }

    inline void SupplementPolyBassPeak(const float* st, bool* picked, int bandStart, int bandEnd)
    {
        if (!st || !picked || bandStart >= bandEnd) return;
        for (int i = bandStart; i < bandEnd; ++i)
            if (picked[i]) return;

        const float bandMax = BandMaxStrength(st, bandStart, bandEnd);
        if (bandMax < 1e-6f) return;
        const float minS = bandMax * 0.24f;

        int best = -1;
        int second = -1;
        float bestS = minS;
        float secondS = minS;
        for (int i = bandStart; i < bandEnd; ++i) {
            if (st[i] < minS) continue;
            const int jl = (i - 1 < bandStart) ? bandStart : (i - 1);
            const int jh = (i + 1 >= bandEnd) ? (bandEnd - 1) : (i + 1);
            bool isPeak = true;
            for (int j = jl; j <= jh; ++j) {
                if (j != i && st[j] >= st[i]) { isPeak = false; break; }
            }
            if (!isPeak) continue;
            if (st[i] > bestS) {
                secondS = bestS;
                second = best;
                bestS = st[i];
                best = i;
            }
            else if (st[i] > secondS) {
                secondS = st[i];
                second = i;
            }
        }
        if (best < 0) return;
        if (second >= 0 && bestS < secondS * 1.18f) return;
        picked[best] = true;
    }
}

class SpeanaNoteDetector
{
public:
    void Configure(double sampleRate)
    {
        if (sampleRate < 8000.0) sampleRate = 44100.0;
        if (m_sampleRate == sampleRate && !m_coeffs.empty()) return;
        m_sampleRate = sampleRate;
        using namespace SpndCfg;
        const double PI = SpndCfg::SPND_PI;
        m_coeffs.resize(KEY_COUNT);
        for (int i = 0; i < KEY_COUNT; ++i) {
            const int midi = MIDI_BASE + i;
            const double freq = 440.0 * pow(2.0, (midi - 69) / 12.0);
            m_coeffs[i] = 2.0 * cos(2.0 * PI * freq / sampleRate);
        }
        m_hannLow.resize(WIN_LOW);
        for (int n = 0; n < WIN_LOW; ++n)
            m_hannLow[n] = 0.5 - 0.5 * cos(2.0 * PI * n / (WIN_LOW - 1));
        m_hannBass.resize(WIN_BASS);
        for (int n = 0; n < WIN_BASS; ++n)
            m_hannBass[n] = 0.5 - 0.5 * cos(2.0 * PI * n / (WIN_BASS - 1));
        m_hannOnset.resize(WIN_ONSET);
        for (int n = 0; n < WIN_ONSET; ++n)
            m_hannOnset[n] = 0.5 - 0.5 * cos(2.0 * PI * n / (WIN_ONSET - 1));
        m_blackmanHigh.resize(WIN_HIGH);
        for (int n = 0; n < WIN_HIGH; ++n)
            m_blackmanHigh[n] = 0.42 - 0.5 * cos(2.0 * PI * n / (WIN_HIGH - 1))
                + 0.08 * cos(4.0 * PI * n / (WIN_HIGH - 1));
        m_low.assign(WIN_LOW, 0.0);
        m_bass.assign(WIN_BASS, 0.0);
        m_wLow.assign(WIN_LOW, 0.0);
        m_wBass.assign(WIN_BASS, 0.0);
        m_wHigh.assign(WIN_HIGH, 0.0);
        m_wOnset.assign(WIN_ONSET, 0.0);
        m_scratch.assign(KEY_COUNT, 0.0);
    }

    void Reset()
    {
        using namespace SpndCfg;
        memset(m_activeKeys, 0, sizeof(m_activeKeys));
        memset(m_noteStrength, 0, sizeof(m_noteStrength));
        memset(m_rawStrengths, 0, sizeof(m_rawStrengths));
        memset(m_smoothedStrengths, 0, sizeof(m_smoothedStrengths));
        memset(m_onsetStrengths, 0, sizeof(m_onsetStrengths));
        memset(m_prevOnsetStrengths, 0, sizeof(m_prevOnsetStrengths));
        memset(m_consecActive, 0, sizeof(m_consecActive));
        memset(m_consecSilent, 0, sizeof(m_consecSilent));
        memset(m_envPeak, 0, sizeof(m_envPeak));
        memset(m_unpickedFrames, 0, sizeof(m_unpickedFrames));
        memset(m_strengthDipFrames, 0, sizeof(m_strengthDipFrames));
    }

    // monoTail: 最新サンプル列。frameCount は末尾が再生同期点。16384 以上で低音最良。
    void Process(const double* monoTail, int frameCount)
    {
        using namespace SpndCfg;
        if (m_coeffs.empty()) Configure(m_sampleRate);
        if (!monoTail || frameCount < WIN_LOW) { Reset(); return; }

        const bool hasBass = (frameCount >= WIN_BASS);
        const double* lowSrc = monoTail + (frameCount - WIN_LOW);
        for (int i = 0; i < WIN_LOW; ++i) m_low[i] = lowSrc[i];
        if (hasBass) {
            const double* bassSrc = monoTail + (frameCount - WIN_BASS);
            for (int i = 0; i < WIN_BASS; ++i) m_bass[i] = bassSrc[i];
        }

        const float levelDb = Bufwav3LevelDbForDynamics(
            m_low.data(), WIN_LOW, hasBass ? m_bass.data() : nullptr, hasBass ? WIN_BASS : 0);
        m_levelDb = levelDb;
        const float gainDb = MakeupGainDbForBufwav3(levelDb);
        ApplyGainDbInPlace(m_low.data(), WIN_LOW, gainDb);
        if (hasBass) ApplyGainDbInPlace(m_bass.data(), WIN_BASS, gainDb);

        for (int i = 0; i < WIN_LOW; ++i) m_wLow[i] = m_low[i] * m_hannLow[i];
        for (int i = 0; i < WIN_HIGH; ++i)
            m_wHigh[i] = m_low[i + (WIN_LOW - WIN_HIGH)] * m_blackmanHigh[i];
        for (int i = 0; i < WIN_ONSET; ++i)
            m_wOnset[i] = m_low[(WIN_LOW - WIN_ONSET) + i] * m_hannOnset[i];

        if (hasBass) {
            for (int i = 0; i < WIN_BASS; ++i) m_wBass[i] = m_bass[i] * m_hannBass[i];
            PianoRollGoertzelBatchAvx2(m_wBass.data(), WIN_BASS, m_coeffs.data(),
                0, LOW_KEY_SPLIT, m_scratch.data());
            for (int i = 0; i < LOW_KEY_SPLIT; ++i)
                m_rawStrengths[i] = ApplyDisplayScale((float)m_scratch[i], i);
        }
        else {
            PianoRollGoertzelBatchAvx2(m_wLow.data(), WIN_LOW, m_coeffs.data(),
                0, LOW_KEY_SPLIT, m_scratch.data());
            for (int i = 0; i < LOW_KEY_SPLIT; ++i)
                m_rawStrengths[i] = ApplyDisplayScale((float)m_scratch[i], i);
        }

        PianoRollGoertzelBatchAvx2(m_wHigh.data(), WIN_HIGH, m_coeffs.data(),
            LOW_KEY_SPLIT, KEY_COUNT, m_scratch.data());
        for (int i = LOW_KEY_SPLIT; i < KEY_COUNT; ++i)
            m_rawStrengths[i] = ApplyDisplayScale((float)m_scratch[i - LOW_KEY_SPLIT], i);

        PianoRollGoertzelBatchAvx2(m_wOnset.data(), WIN_ONSET, m_coeffs.data(),
            0, KEY_COUNT, m_scratch.data());
        for (int i = 0; i < KEY_COUNT; ++i)
            m_onsetStrengths[i] = ApplyDisplayScale((float)m_scratch[i], i);

        for (int i = 0; i < KEY_COUNT; ++i) {
            const float alpha = (i < BAND_BASS_END) ? IIR_ALPHA_BASS : IIR_ALPHA;
            m_smoothedStrengths[i] =
                m_smoothedStrengths[i] * (1.0f - alpha) + m_rawStrengths[i] * alpha;
        }

        UpdateNoteStates();
    }

    const bool*  Active() const { return m_activeKeys; }
    const float* Strength() const { return m_noteStrength; }

private:
    void UpdateNoteStates()
    {
        using namespace SpndCfg;

        float pickStrength[KEY_COUNT];
        float trackStrength[KEY_COUNT];
        for (int i = 0; i < KEY_COUNT; ++i) {
            pickStrength[i] = m_rawStrengths[i];
            trackStrength[i] = m_smoothedStrengths[i];
        }

        NormalizeBandPeak(pickStrength, 0, BAND_BASS_END, DISPLAY_PEAK_CAP);
        NormalizeBandPeak(pickStrength, BAND_BASS_END, BAND_MID_LO_END, DISPLAY_PEAK_CAP);
        NormalizeBandPeak(pickStrength, BAND_MID_LO_END, BAND_MID_END, DISPLAY_PEAK_CAP);
        NormalizeBandPeak(pickStrength, BAND_MID_END, KEY_COUNT, DISPLAY_PEAK_CAP);
        NormalizeBandPeak(trackStrength, 0, BAND_BASS_END, DISPLAY_PEAK_CAP);
        NormalizeBandPeak(trackStrength, BAND_BASS_END, BAND_MID_LO_END, DISPLAY_PEAK_CAP);
        NormalizeBandPeak(trackStrength, BAND_MID_LO_END, BAND_MID_END, DISPLAY_PEAK_CAP);
        NormalizeBandPeak(trackStrength, BAND_MID_END, KEY_COUNT, DISPLAY_PEAK_CAP);

        const float pickScale = PickThreshScaleFromLevelDb(m_levelDb);
        const bool polyFrame = FrameLooksPolyphonic(pickStrength);

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
                m_smoothedStrengths[i] *= 0.4f;
            }
            memcpy(m_prevOnsetStrengths, m_onsetStrengths, sizeof(m_onsetStrengths));
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

        bool bassOnsetFrame = false;
        for (int i = 0; i < BAND_BASS_END; ++i) {
            const float onsetDelta = m_onsetStrengths[i] - m_prevOnsetStrengths[i];
            if (onsetDelta >= BASS_ONSET_DELTA_THRESH &&
                m_onsetStrengths[i] >= BASS_ONSET_MIN_STRENGTH &&
                pickStrength[i] >= BASS_PICK_THRESH * pickScale * 0.35f) {
                bassOnsetFrame = true;
                break;
            }
        }

        ApplyBassHarmonicPick(pickStrength, m_rawStrengths, picked, 0, BAND_BASS_END,
            BASS_PICK_THRESH * pickScale * (polyFrame ? 0.82f : 1.0f));
        const bool bassDominantFrame =
            bassMax >= BAND_SILENCE_BASS &&
            bassMax >= midMax * 0.72f;
        if (polyFrame && (bassOnsetFrame || bassDominantFrame))
            SupplementPolyBassPeak(pickStrength, picked, 0, BAND_BASS_END);
        ApplyBandFundamentalPick(pickStrength, picked, BAND_BASS_END, BAND_MID_END,
            MID_PICK_THRESH * pickScale * (polyFrame ? 0.93f : 1.0f), polyFrame);
        ApplyBandFundamentalPick(pickStrength, picked, BAND_MID_END, KEY_COUNT,
            TRE_PICK_THRESH * pickScale, polyFrame);

        for (int i = 0; i < KEY_COUNT; ++i) {
            bassPick[i] = midPick[i] = treblePick[i] = false;
            if (!picked[i]) continue;
            if (i < BAND_BASS_END) bassPick[i] = true;
            else if (i < BAND_MID_END) midPick[i] = true;
            else treblePick[i] = true;
        }

        SuppressWeakBandPicks(pickStrength, bassPick, midPick, treblePick, polyFrame);

        for (int i = 0; i < KEY_COUNT; ++i)
            picked[i] = bassPick[i] || midPick[i] || treblePick[i];

        const bool singleSource = !polyFrame && (
            FrameLooksLikeSingleSource(picked, 0, KEY_COUNT)
            || FrameLooksSparseDominant(pickStrength, picked, 0, KEY_COUNT));
        if (singleSource) {
            ResolveHarmonicPicks(pickStrength, picked, BAND_BASS_END, KEY_COUNT);
            FilterWeakIsolatedOutliers(pickStrength, picked, BAND_BASS_END, KEY_COUNT, 0.21f);
            ResolveHarmonicPicksLight(pickStrength, picked, 0, BAND_BASS_END);
        }
        else {
            ResolveHarmonicPicksLight(pickStrength, picked, BAND_BASS_END, KEY_COUNT);
            ResolveHarmonicPicksLight(pickStrength, picked, 0, BAND_BASS_END);
        }

        SnapPicksToLocalMaxima(pickStrength, picked, 0, BAND_BASS_END, 2);
        SnapPicksToLocalMaxima(pickStrength, picked, BAND_BASS_END, BAND_MID_LO_END, 2);
        CollapseNearbyPicks(pickStrength, picked, 0, BAND_BASS_END, 2, false);
        CollapseNearbyPicks(pickStrength, picked, BAND_BASS_END, BAND_MID_LO_END, 2, false);

        for (int i = 0; i < KEY_COUNT; ++i)
        {
            const float sigStrength = pickStrength[i];
            bool effectivePicked = picked[i];
            bool bassOnsetHit = false;

            if (!effectivePicked && i < BAND_BASS_END) {
                const float onsetDelta = m_onsetStrengths[i] - m_prevOnsetStrengths[i];
                if (onsetDelta >= BASS_ONSET_DELTA_THRESH &&
                    m_onsetStrengths[i] >= BASS_ONSET_MIN_STRENGTH &&
                    pickStrength[i] >= BASS_PICK_THRESH * pickScale * 0.40f) {
                    effectivePicked = true;
                    bassOnsetHit = true;
                }
            }

            if (!effectivePicked && polyFrame && i >= BAND_BASS_END && i < BAND_MID_END) {
                const float onsetDelta = m_onsetStrengths[i] - m_prevOnsetStrengths[i];
                if (onsetDelta >= MID_ONSET_DELTA_THRESH &&
                    m_onsetStrengths[i] >= MID_ONSET_MIN_STRENGTH &&
                    pickStrength[i] >= MID_PICK_THRESH * pickScale * 0.48f &&
                    !PianoKey::IsHarmonicOfAnyActive(pickStrength, i, picked, 0, KEY_COUNT, KEY_COUNT, 0.80f))
                    effectivePicked = true;
            }

            if (!effectivePicked && i >= ONSET_KEY_START) {
                const float onsetDelta = m_onsetStrengths[i] - m_prevOnsetStrengths[i];
                if (onsetDelta >= ONSET_DELTA_THRESH &&
                    m_onsetStrengths[i] >= ONSET_MIN_STRENGTH &&
                    pickStrength[i] >= TRE_PICK_THRESH * pickScale * 0.55f &&
                    !PianoKey::IsHarmonicOfAnyActive(pickStrength, i, picked, 0, KEY_COUNT, KEY_COUNT))
                    effectivePicked = true;
            }

            if (!effectivePicked && picked[i] && i >= BAND_BASS_END) {
                for (int j = i + 1; j < i + 13 && j < KEY_COUNT; ++j) {
                    if (!m_activeKeys[j]) continue;
                    if (!IsMelodicNeighbor(i, j)) continue;
                    if (pickStrength[i] >= pickStrength[j] * 0.42f) {
                        effectivePicked = true;
                        break;
                    }
                }
            }

            if (!effectivePicked && m_activeKeys[i]) {
                if (i < BAND_BASS_END) {
                    const float holdRatio = HOLD_ENV_BASS;
                    if (holdRatio > 0.0f && m_envPeak[i] > 0.001f &&
                        trackStrength[i] >= m_envPeak[i] * holdRatio &&
                        !PianoKey::IsHarmonicOfAnyActive(pickStrength, i, picked, 0, BAND_BASS_END, KEY_COUNT, 0.88f))
                        effectivePicked = true;
                }
                else {
                    const float holdRatio = HoldEnvRatio(i);
                    if (holdRatio > 0.0f && m_envPeak[i] > 0.001f &&
                        trackStrength[i] >= m_envPeak[i] * holdRatio &&
                        !PianoKey::IsHarmonicOfAnyActive(pickStrength, i, picked, 0, KEY_COUNT, KEY_COUNT, 0.78f))
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
                const bool fastAttack = bassOnsetHit ||
                    (effectivePicked && i < BAND_BASS_END && m_onsetStrengths[i] >= BASS_ONSET_MIN_STRENGTH);
                if (effectivePicked &&
                    (fastAttack || m_consecActive[i] >= TemporalFrames(i, ATTACK_FRAMES))) {
                    cur = true;
                    m_consecSilent[i] = 0;
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
                const int gapLimit = VisGapFrames(i);
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

        // 表示用バー高: アクティブな音は包絡ピーク(m_envPeak)を用いる。
        // 瞬間生強度(m_rawStrengths)はピアノ減衰で急落し、サステイン中にバーが
        // 消えて「途切れ」て見えるため。envPeakはピーク保持+緩やか減衰で安定。
        // 全鍵が同一(帯域正規化)スケールなので相対バー高も一貫する。
        for (int i = 0; i < KEY_COUNT; ++i) {
            if (!m_activeKeys[i]) { m_noteStrength[i] = 0.0f; continue; }
            float disp = m_envPeak[i];
            if (disp <= 0.0f) disp = pickStrength[i];
            if (disp <= 0.0f) disp = m_rawStrengths[i];
            m_noteStrength[i] = disp;
        }

        memcpy(m_prevOnsetStrengths, m_onsetStrengths, sizeof(m_onsetStrengths));
    }

    double m_sampleRate = 44100.0;
    std::vector<double> m_coeffs;
    std::vector<double> m_hannLow, m_hannBass, m_hannOnset, m_blackmanHigh;
    std::vector<double> m_low, m_bass, m_wLow, m_wBass, m_wHigh, m_wOnset;
    std::vector<double> m_scratch;
    float m_levelDb = -60.0f;

    bool  m_activeKeys[SpndCfg::KEY_COUNT] = {};
    float m_noteStrength[SpndCfg::KEY_COUNT] = {};
    float m_rawStrengths[SpndCfg::KEY_COUNT] = {};
    float m_smoothedStrengths[SpndCfg::KEY_COUNT] = {};
    float m_onsetStrengths[SpndCfg::KEY_COUNT] = {};
    float m_prevOnsetStrengths[SpndCfg::KEY_COUNT] = {};
    int   m_consecActive[SpndCfg::KEY_COUNT] = {};
    int   m_consecSilent[SpndCfg::KEY_COUNT] = {};
    float m_envPeak[SpndCfg::KEY_COUNT] = {};
    int   m_unpickedFrames[SpndCfg::KEY_COUNT] = {};
    int   m_strengthDipFrames[SpndCfg::KEY_COUNT] = {};
};
