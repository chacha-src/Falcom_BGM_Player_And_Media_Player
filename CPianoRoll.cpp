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
#include "PianoKeyTable.h"
#include "PianoRollGoertzelAvx2.h"
#include <algorithm>

extern save savedata;
void COggDlg_SyncPianoRollFast();
IMPLEMENT_DYNAMIC(CPianoRoll, CCustomBlurDialogExBase)

// 分析パラメータ。値は実音への応答とゴースト抑制のバランスで調整済み。
// 基音ピック + NormalizeBandPeak + 包絡ホールドの3本柱で動作する。
namespace Cfg
{
    static constexpr float IIR_ALPHA = 0.46f;       // 中高音 IIR（弱いアタックレス音の立ち上がりを速く）
    static constexpr float IIR_ALPHA_BASS = 0.28f;  // 低音はゆっくり追従(倍音影響を抑える)
    static constexpr float SILENCE_ABS = 0.007f;    // 全帯域の絶対無音閾値
    static constexpr float BAND_SILENCE_BASS = 0.005f;
    static constexpr float BAND_SILENCE_MID = 0.004f;
    static constexpr float BAND_SILENCE_TRE = 0.004f;
    static constexpr int   ATTACK_FRAMES = 1;        // ノートオンに必要な連続アクティブフレーム数
    static constexpr int   RELEASE_FRAMES = 5;       // サステイン維持と高速音のバランス
    static constexpr int   VIS_GAP_FRAMES = 4;       // 中高音の再トリガーギャップ
    static constexpr int   VIS_GAP_FRAMES_BASS = 2;  // 低音の再トリガーギャップ(低音は長いノートが多い)
    static constexpr float RETRIGGER_RATIO = 0.28f;  // サステイン中の一瞬の落ち込みを許容
    static constexpr int   BAND_BASS_END = 25;        // 低音帯の上限インデックス(B2相当)
    static constexpr int   BAND_MID_END = 53;
    static constexpr int   BAND_MID_LO_END = 45;     // O3帯 / O4上端で正規化分割(A4付近)
    static constexpr int   BAND_TRE_HI_START = 63;   // O6/C6: 木琴・超高音は別正規化/閾値
    static constexpr float BASS_PICK_THRESH = 0.20f;
    // 弱い持続声部(フルート)・短命な打音(ベル)向けに mid/treble 閾値を下げる。
    static constexpr float MID_PICK_THRESH = 0.100f;
    static constexpr float TRE_PICK_THRESH = 0.085f;
    static constexpr float TRE_HI_PICK_THRESH = 0.058f;
    static constexpr float PRUNE_BAND_RATIO = 0.08f;
    static constexpr float PRUNE_TOP_RATIO = 0.12f;
    static constexpr float HOLD_ENV_BASS = 0.34f;
    static constexpr float HOLD_ENV_MID = 0.21f;
    static constexpr float HOLD_ENV_TRE = 0.19f;
    static constexpr float HOLD_ENV_TRE_HI = 0.11f;  // O6+ベル: 減衰が速いのでホールドを緩める
    static constexpr float DISPLAY_PEAK_CAP = 5.0f;
    static constexpr int   ANALYZE_INTERVAL = 1024;
    static constexpr float WEAK_BASS_RATIO = 0.13f;
    static constexpr float WEAK_MID_RATIO = 0.10f;
    static constexpr float WEAK_TRE_RATIO = 0.08f;
    static constexpr float WEAK_TRE_HI_RATIO = 0.055f;
    static constexpr float ONSET_DELTA_THRESH = 0.036f;
    static constexpr float ONSET_MIN_STRENGTH = 0.065f;
    static constexpr float BASS_ONSET_DELTA_THRESH = 0.034f;
    static constexpr float BASS_ONSET_MIN_STRENGTH = 0.055f;
    static constexpr float MID_ONSET_DELTA_THRESH = 0.030f;
    static constexpr int   ONSET_KEY_START = 41;
    static constexpr float MID_ONSET_MIN_STRENGTH = 0.050f;

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
        int bandStart, int bandEnd, bool polyMix)
    {
        if (!st || !picked || bandStart >= bandEnd) return;
        SuppressFalseSubharmonicPicks(st, picked, bandStart, bandEnd, polyMix);
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
        if (keyIndex >= BAND_TRE_HI_START) return HOLD_ENV_TRE_HI;
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
        else if (keyIndex >= BAND_BASS_END)
            scale = 1.50f - t * 0.55f; // O3〜O4: フレーム数の帯域内ばらつきを抑える
        else
            scale = 1.90f - t * 1.45f;
        int f = (int)(baseFrames * scale + 0.5f);
        if (keyIndex >= BAND_MID_END) {
            if (f < 1) f = 1;
            if (f > 7) f = 7;
        }
        else {
            if (keyIndex < BAND_BASS_END) {
                if (f < 1) f = 1;
                if (f > 8) f = 8;
            }
            else {
                if (f < 2) f = 2;
                if (f > 12) f = 12;
            }
        }
        return f;
    }

    static int VisGapFrames(int keyIndex)
    {
        if (keyIndex >= BAND_TRE_HI_START)
            return TemporalFrames(keyIndex, VIS_GAP_FRAMES + 4);
        const int base = (keyIndex < BAND_BASS_END) ? VIS_GAP_FRAMES_BASS : VIS_GAP_FRAMES;
        return TemporalFrames(keyIndex, base);
    }

    // 96kHz等でも主ピック閾値は据え置き（上げると埋もれた旋律が拾えない）
    static float TrebleThreshScaleFromSampleRate(int sampleRate)
    {
        (void)sampleRate;
        return 1.0f;
    }

    static bool IsMelodicNeighbor(int a, int b)
    {
        if (a == b) return false;
        const int lo = (a < b) ? a : b;
        const int hi = (a < b) ? b : a;
        const int d = hi - lo;
        if (d < 1 || d > 11) return false;
        return !PianoKey::IsHarmonicPair(hi, lo) && !PianoKey::IsOctaveRelated(hi, lo);
    }

    static int CountSpectralPeaksInBand(const float* st, int lo, int hi, float relToMax)
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
                if (st[j] >= st[i]) {
                    isPeak = false;
                    break;
                }
            }
            if (isPeak) ++n;
        }
        return n;
    }

    // 正規化後スペクトルから複音を推定（ピック数ではなくピーク数）
    static bool FrameLooksPolyphonic(const float* st)
    {
        if (!st) return false;
        const int bassPeaks = CountSpectralPeaksInBand(st, 0, BAND_BASS_END, 0.17f);
        const int midPeaks = CountSpectralPeaksInBand(st, BAND_BASS_END, BAND_MID_END, 0.14f);
        const int trePeaks = CountSpectralPeaksInBand(st, BAND_MID_END, 88, 0.14f);
        int liveBands = 0;
        if (bassPeaks >= 2) ++liveBands;
        if (midPeaks >= 4) ++liveBands;
        if (trePeaks >= 5) ++liveBands;
        return liveBands >= 2 || (midPeaks >= 5 && (bassPeaks + trePeaks) >= 4);
    }

    // ピック結果が単一音の倍音列か（ハープ等）
    static bool FrameLooksLikeSingleSource(const bool* picked, int lo, int hi)
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

    static bool FrameLooksSparseDominant(const float* st, const bool* picked, int lo, int hi)
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

    // 同時音数上限なし: 帯域内の鍵数だけをループ上限に使う
    static void ApplyBandFundamentalPick(const float* st, bool* picked,
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

        // 複音補完の緩い全ピックは中音域のみ。高音域で行うとHFノイズが一斉点灯する。
        if (polyFrame && bandStart >= BAND_BASS_END && bandStart < BAND_MID_END) {
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

    static void SuppressWeakBandPicks(const float* strengths,
        bool* bassPick, bool* midPick, bool* treblePick, bool polyFrame)
    {
        if (!strengths || !bassPick || !midPick || !treblePick) return;

        const float bassR = WEAK_BASS_RATIO * (polyFrame ? 0.82f : 1.0f);
        const float midR = WEAK_MID_RATIO * (polyFrame ? 0.90f : 1.0f);
        const float treR = WEAK_TRE_RATIO * (polyFrame ? 0.92f : 1.0f);

        const float bassMax = BandMaxStrength(strengths, 0, BAND_BASS_END);
        const float midMax = BandMaxStrength(strengths, BAND_BASS_END, BAND_MID_END);
        const float treLoMax = BandMaxStrength(strengths, BAND_MID_END, BAND_TRE_HI_START);
        const float treHiMax = BandMaxStrength(strengths, BAND_TRE_HI_START, 88);

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
        if (treLoMax > 1e-6f || treHiMax > 1e-6f) {
            const float minLo = (treLoMax > 1e-6f) ? treLoMax * treR : 0.0f;
            const float minHi = (treHiMax > 1e-6f) ? treHiMax * WEAK_TRE_HI_RATIO * (polyFrame ? 0.92f : 1.0f) : 0.0f;
            for (int i = BAND_MID_END; i < BAND_TRE_HI_START; ++i)
                if (treblePick[i] && strengths[i] < minLo) treblePick[i] = false;
            for (int i = BAND_TRE_HI_START; i < 88; ++i)
                if (treblePick[i] && strengths[i] < minHi) treblePick[i] = false;
        }
    }

    // 低音帯: フルスペクトル倍音サリエンスによる基音ピック
    //   raw  = 帯域正規化前の生強度（倍音 i+12/i+19/i+24 が C3以上にあっても参照できる）
    //   norm = 帯域正規化後の強度（局所ピーク判定・闾値に使用）
    // 合成音検証済み: 基音が弱く2倍音が最強でも、サリエンスは正しい基音に当たる。
    static void ApplyBassHarmonicPick(const float* norm, const float* raw, bool* picked,
        int bandStart, int bandEnd, float relThresh, bool supplementalFundamentalOnly = false)
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
            sal[i] = f * (f + h2 * 0.5f + h3 * 0.33f + h4 * 0.25f);
            if (sal[i] > salMax) salMax = sal[i];
        }
        if (salMax < 1e-9f) return;

        const float minSal = salMax * relThresh;
        for (int i = bandStart; i < bandEnd; ++i) {
            if (supplementalFundamentalOnly && picked[i]) continue;
            if (sal[i] < minSal) continue;
            if (supplementalFundamentalOnly &&
                !PianoKey::SalienceLooksLikeFundamental(raw, i, 88))
                continue;
            if (supplementalFundamentalOnly &&
                PianoKey::IsHarmonicOfAnyActive(raw, i, picked, bandStart, bandEnd, 88, 0.70f))
                continue;
            // 厳格な局所ピーク（±1）: 滲みで隣接鍵が同時点灯するのを防ぐ
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

    // 低音帯: スペアナ mode0 に近い「生強度の局所ピーク」ピック（基音推定の誤オクターブを避ける）
    static void ApplyBassBandSpeanaPick(const float* st, bool* picked,
        int bandStart, int bandEnd, float relThresh, bool polyMix)
    {
        if (!st || !picked || bandStart >= bandEnd) return;
        const float bandMax = BandMaxStrength(st, bandStart, bandEnd);
        if (bandMax < 0.0025f) return; // 非常に低い物理的なノイズフロア（無音時の砂嵐ノイズを完全にカット）
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

        // C3+の別声部に引きずられて低音が死なないよう、倍音照合は帯域内に限定
        const float ghostRatio = polyMix ? 0.54f : 0.32f;
        for (int i = bandStart; i < bandEnd; ++i) {
            if (!cand[i]) continue;
            for (int hi = i + 1; hi < bandEnd; ++hi) {
                if (!PianoKey::IsHarmonicPair(hi, i) && !PianoKey::IsOctaveRelated(hi, i))
                    continue;
                if (st[hi] >= st[i] * ghostRatio) {
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

    // 複音: スラップ等でスペアナピックが全滅したとき帯域最強ピークを1つ補う
    static void SupplementPolyBassPeak(const float* st, bool* picked, int bandStart, int bandEnd)
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
                if (j != i && st[j] >= st[i]) {
                    isPeak = false;
                    break;
                }
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

    // 複音BGMの旋律: 基音グリーディが伴奏倍音を拾い、倍音整理で本当の旋律が落ちる。
    // 帯域内の局所ピークを足す。Salience+倍音チェックでゴーストを抑える。
    static void SupplementPolyMelodyPeaks(const float* norm, const float* raw, bool* picked,
        int bandStart, int bandEnd, float relOfBandMax)
    {
        if (!norm || !raw || !picked || bandStart >= bandEnd) return;
        const float bandMax = BandMaxStrength(norm, bandStart, bandEnd);
        if (bandMax < 1e-6f) return;
        const float minS = bandMax * relOfBandMax;
        for (int i = bandStart; i < bandEnd; ++i) {
            if (picked[i]) continue;
            if (norm[i] < minS) continue;
            if (!IsLocalPeakInBand(norm, i, bandStart, bandEnd)) continue;
            if (!PianoKey::SalienceLooksLikeFundamental(raw, i, 88)) continue;
            if (PianoKey::IsHarmonicOfAnyActive(norm, i, picked, bandStart, bandEnd, 88, 0.68f))
                continue;
            picked[i] = true;
        }
    }

    // 複音旋律: 下の音もピック済みで明らかに強いときだけ上の倍音を落とす
    static void PrunePolyMelodyHarmonicDupes(const float* st, bool* picked, int lo, int hi)
    {
        if (!st || !picked || lo >= hi) return;
        for (int i = hi - 1; i >= lo; --i) {
            if (!picked[i]) continue;
            for (int j = lo; j < i; ++j) {
                if (!picked[j] || !PianoKey::IsHarmonicPair(i, j)) continue;
                if (st[j] >= st[i] * 0.72f) {
                    picked[i] = false;
                    break;
                }
            }
        }
    }

    static int RequiredAttackFrames(int keyIndex, float strength = 0.0f, float bandMax = 0.0f)
    {
        const int base = TemporalFrames(keyIndex, ATTACK_FRAMES);
        if (keyIndex >= BAND_TRE_HI_START) {
            if (bandMax > 1e-6f && strength >= bandMax * 0.46f) return 1;
            const int hiMin = 2;
            return base < hiMin ? hiMin : base;
        }
        return base;
    }

    static void CapBandPicksPreferActive(const float* norm, bool* picked, int lo, int hi,
        int maxKeep, const bool* activeKeys)
    {
        if (!norm || !picked || lo >= hi || maxKeep <= 0) return;
        struct Item { int idx; float val; bool active; } items[32];
        int n = 0;
        for (int i = lo; i < hi; ++i) {
            if (!picked[i]) continue;
            if (n < 32) {
                items[n].idx = i;
                items[n].val = norm[i];
                items[n].active = activeKeys && activeKeys[i];
                ++n;
            }
        }
        if (n <= maxKeep) return;
        for (int a = 0; a < n - 1; ++a) {
            for (int b = a + 1; b < n; ++b) {
                bool swap = false;
                if (items[b].active && !items[a].active) swap = true;
                else if (items[b].active == items[a].active && items[b].val > items[a].val)
                    swap = true;
                if (swap) {
                    const Item t = items[a];
                    items[a] = items[b];
                    items[b] = t;
                }
            }
        }
        for (int k = maxKeep; k < n; ++k)
            picked[items[k].idx] = false;
    }

    static void CapBandPicksByStrength(const float* norm, bool* picked, int lo, int hi, int maxKeep)
    {
        if (!norm || !picked || lo >= hi || maxKeep <= 0) return;
        struct Item { int idx; float val; } items[32];
        int n = 0;
        for (int i = lo; i < hi; ++i) {
            if (!picked[i]) continue;
            if (n < 32) {
                items[n].idx = i;
                items[n].val = norm[i];
                ++n;
            }
        }
        if (n <= maxKeep) return;
        for (int a = 0; a < n - 1; ++a) {
            for (int b = a + 1; b < n; ++b) {
                if (items[b].val > items[a].val) {
                    const Item t = items[a];
                    items[a] = items[b];
                    items[b] = t;
                }
            }
        }
        for (int k = maxKeep; k < n; ++k)
            picked[items[k].idx] = false;
    }

    // O6+: スペクトルピークが多いのに鍵がコロコロ変わる＝HFノイズ状クラスター
    static void PruneHighFrequencyClutter(const float* norm, bool* picked, const bool* activeKeys)
    {
        if (!norm || !picked) return;
        const int lo = BAND_TRE_HI_START;
        const int hi = 88;
        const int peaks = CountSpectralPeaksInBand(norm, lo, hi, 0.09f);
        const float bandMax = BandMaxStrength(norm, lo, hi);
        if (bandMax < 1e-6f) return;

        const float minRelNew = (peaks >= 6) ? 0.50f : (peaks >= 4) ? 0.42f : 0.34f;
        const float minRelActive = 0.22f;
        for (int i = lo; i < hi; ++i) {
            if (!picked[i]) continue;
            const float rel = (activeKeys && activeKeys[i]) ? minRelActive : minRelNew;
            if (norm[i] < bandMax * rel)
                picked[i] = false;
        }
        FilterWeakIsolatedOutliers(norm, picked, lo, hi, (peaks >= 5) ? 0.36f : 0.30f);
        if (peaks >= 4)
            CapBandPicksPreferActive(norm, picked, lo, hi, 2, activeKeys);
        else if (peaks >= 2)
            CapBandPicksPreferActive(norm, picked, lo, hi, 3, activeKeys);

        // 新規の弱い鍵だけ落とす（鳴り続けているベルは上で緩め済み）
        if (peaks >= 4) {
            for (int i = lo; i < hi; ++i) {
                if (!picked[i]) continue;
                if (activeKeys && activeKeys[i]) continue;
                if (norm[i] < bandMax * 0.46f)
                    picked[i] = false;
            }
        }
    }

    // 中音基音の倍音が帯域別正規化で高音に独立音として漏れるのを抑える
    static bool TrebleLooksIndependentOfMid(const float* norm, const float* raw, int keyIndex)
    {
        if (!norm || !raw || keyIndex < BAND_MID_END) return true;
        const int treHi = 88;
        if (keyIndex >= BAND_TRE_HI_START) {
            const float hiMax = BandMaxStrength(norm, BAND_TRE_HI_START, treHi);
            return PianoKey::SalienceLooksLikeFundamental(raw, keyIndex, treHi) &&
                norm[keyIndex] >= hiMax * 0.56f;
        }
        const float loMax = BandMaxStrength(norm, BAND_MID_END, BAND_TRE_HI_START);
        return PianoKey::SalienceLooksLikeFundamental(raw, keyIndex, treHi) &&
            norm[keyIndex] >= loMax * 0.60f;
    }

    static bool IsMidHarmonicLeak(const float* norm, const float* raw, int treKey,
        const bool* picked, const bool* activeKeys)
    {
        if (!norm || !raw || treKey < BAND_MID_END) return false;
        for (int j = BAND_BASS_END; j < BAND_MID_END; ++j) {
            if (!picked[j] && !(activeKeys && activeKeys[j])) continue;
            if (!PianoKey::IsHarmonicPair(treKey, j) && !PianoKey::IsOctaveRelated(treKey, j))
                continue;
            if (raw[j] >= raw[treKey] * 0.20f)
                return !TrebleLooksIndependentOfMid(norm, raw, treKey);
        }
        return false;
    }

    static void SuppressCrossBandHarmonicLeaks(const float* norm, const float* raw,
        bool* picked, const bool* activeKeys)
    {
        if (!norm || !raw || !picked) return;
        for (int i = BAND_MID_END; i < 88; ++i) {
            if (!picked[i]) continue;
            if (IsMidHarmonicLeak(norm, raw, i, picked, activeKeys))
                picked[i] = false;
        }
    }

    // 剪定後も鳴り続けている O6+ ベルを拾い直す（中音倍音は復活させない）
    static void ReinstateActiveHighBells(const float* norm, const float* raw,
        bool* picked, const bool* activeKeys)
    {
        if (!norm || !raw || !picked || !activeKeys) return;
        const int lo = BAND_TRE_HI_START;
        const int hi = 88;
        const float bandMax = BandMaxStrength(norm, lo, hi);
        if (bandMax < 1e-6f) return;
        for (int i = lo; i < hi; ++i) {
            if (!activeKeys[i]) continue;
            if (IsMidHarmonicLeak(norm, raw, i, picked, activeKeys)) continue;
            if (norm[i] >= bandMax * 0.13f)
                picked[i] = true;
        }
    }

    // O6+ 打楽器: 倍音が乏しい純音。Salience なしで局所ピークを足す。
    static void SupplementHighBellPeaks(const float* norm, const float* raw, bool* picked,
        int bandStart, int bandEnd, float relOfBandMax)
    {
        if (!norm || !raw || !picked || bandStart >= bandEnd) return;
        const int peaks = CountSpectralPeaksInBand(norm, bandStart, bandEnd, 0.09f);
        if (peaks >= 8) return;
        const float bandMax = BandMaxStrength(norm, bandStart, bandEnd);
        if (bandMax < 1e-6f) return;
        const float minS = bandMax * relOfBandMax;
        for (int i = bandStart; i < bandEnd; ++i) {
            if (picked[i]) continue;
            if (norm[i] < minS) continue;
            if (!IsLocalPeakInBand(norm, i, bandStart, bandEnd)) continue;
            if (norm[i] < bandMax * 0.32f) continue;
            if (IsMidHarmonicLeak(norm, raw, i, picked, nullptr)) continue;
            if (PianoKey::IsHarmonicOfAnyActive(norm, i, picked, bandStart, bandEnd, 88, 0.74f))
                continue;
            picked[i] = true;
        }
    }

    static bool LooksLikeTonalFundamental(const float* raw, int i)
    {
        return PianoKey::SalienceLooksLikeFundamental(raw, i, 88) ||
            PianoKey::PassesFundamentalTestSustain(raw, i, 88);
    }

    // ドラム: 帯域横断の多数ピーク＋同時オンセット＝ノイズ状バー。基音構造のないピックを落とす。
    static bool FrameLooksPercussiveBurst(const float* st,
        const float* onset, const float* prevOnset, const bool* picked)
    {
        if (!st || !onset || !prevOnset) return false;

        const int bassP = CountSpectralPeaksInBand(st, 0, BAND_BASS_END, 0.15f);
        const int midP = CountSpectralPeaksInBand(st, BAND_BASS_END, BAND_MID_END, 0.12f);
        const int treP = CountSpectralPeaksInBand(st, BAND_MID_END, 88, 0.11f);
        const int totalP = bassP + midP + treP;

        int pickN = 0;
        if (picked) {
            for (int i = 0; i < 88; ++i)
                if (picked[i]) ++pickN;
        }

        int onsetBurst = 0;
        for (int i = 0; i < 88; ++i) {
            const float d = onset[i] - prevOnset[i];
            if (d >= ONSET_DELTA_THRESH * 0.85f)
                ++onsetBurst;
        }

        if (totalP >= 15 && bassP >= 2 && treP >= 5 && pickN >= 6) return true;
        if (treP >= 10 && (pickN >= 5 || midP >= 6)) return true;
        if (onsetBurst >= 10 && totalP >= 12) return true;
        if (pickN >= 12) return true;
        return false;
    }

    static void SuppressPercussivePicks(const float* norm, const float* raw, bool* picked)
    {
        if (!norm || !raw || !picked) return;

        for (int i = BAND_BASS_END; i < 88; ++i) {
            if (!picked[i]) continue;
            if (!LooksLikeTonalFundamental(raw, i))
                picked[i] = false;
        }
        for (int i = 0; i < BAND_BASS_END; ++i) {
            if (!picked[i]) continue;
            if (!PianoKey::PassesFundamentalTestSustain(raw, i, 88))
                picked[i] = false;
        }

        FilterWeakIsolatedOutliers(norm, picked, BAND_BASS_END, 88, 0.26f);
        CapBandPicksByStrength(norm, picked, BAND_MID_END, 88, 4);
        CapBandPicksByStrength(norm, picked, 0, BAND_BASS_END, 2);
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
    return ScaleWinSamples(WIN_LOW_REF, sampleRate, capSamples);
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

// 再生停止・曲切替時に呼ぶ。リングバッファ・ノート状態・履歴を全クリアする。
// m_cs を TryEnterCriticalSection でポーリング取得しているのは、
// 再生スレッドが FeedPCM で長時間 m_cs を保持している間もデッドロックしないため。
// 最大200ms(200*1ms)待ってロックを諦める。
void CPianoRoll::ResetPlaybackState()
{
    InterlockedExchange(&m_jobPending, 0);
    EnterCriticalSection(&m_jobCs);
    m_jobFrameCount = 0;
    LeaveCriticalSection(&m_jobCs);

    for (int attempt = 0; attempt < 200; ++attempt) {
        if (!TryEnterCriticalSection(&m_cs)) {
            Sleep(1);
            continue;
        }
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
    return;
    }
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

float CPianoRoll::ApplyDisplayScale(float rawAmp, int keyIndex)
{
    return ScaleGoertzelAmp(rawAmp, keyIndex + KEY_OFFSET, DETECT_KEYS);
}

// 再生スレッドからデコード済み PCM を受け取り、モノラル double に変換して
// リングバッファへ書き込む。m_cs で保護されているためスレッドセーフ。
// ResetPlaybackState 後は m_feedEnabled=true に戻すまで書き込まれない。
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

// bufwav3 の再生バッファ直後から呼ばれる。mono は既にモノラル変換済み。
// ANALYZE_MIN_MS(4ms)のスロットリングでワーカーを過負荷から守る。
// ジョブバッファ(m_jobMono)へコピーして SetEvent でワーカーを起こす。
// 前のジョブが完了していない場合も InterlockedExchange で上書きする
// (古い分析より最新フレームを優先する)。
void CPianoRoll::AnalyzePlayCursorMono(const double* mono, int frameCount, int sampleRate)
{
    if (!mono || frameCount < MinAnalyzeFrameCount(sampleRate, frameCount) || sampleRate < 8000) return;
    if (!m_hAnalysisWake || m_workerStop) return;

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

// 窓掛け済みバッファから Goertzel 解析を実行し m_rawStrengths を更新する。
// 低音(C0〜B3): WIN_LOW(8192) 窓 + Hann、高音(C4〜): WIN_HIGH(4096) 窓 + Blackman、
// オンセット: WIN_ONSET(1024) 窓 + Hann で独立解析する。
// AVX2 バッチ実装(PianoRollGoertzelBatchAvx2)を使用。
// 最後に IIR 平滑を適用して m_smoothedStrengths を更新し UpdateNoteStates を呼ぶ。
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

    const float levelDb = Cfg::Bufwav3LevelDbForDynamics(
        m_analysisBuf.data(), m_winLow,
        hasBass ? m_bassAnalysisBuf.data() : nullptr,
        hasBass ? m_winBass : 0);
    m_bufwav3LevelDb = levelDb;
    const float gainDb = Cfg::MakeupGainDbForBufwav3(levelDb);
    Cfg::ApplyGainDbInPlace(m_analysisBuf.data(), m_winLow, gainDb);
    if (hasBass)
        Cfg::ApplyGainDbInPlace(m_bassAnalysisBuf.data(), m_winBass, gainDb);

    for (int i = 0; i < m_winLow; ++i)
        m_windowedLow[i] = m_analysisBuf[i] * m_hannLow[i];
    for (int i = 0; i < m_winHigh; ++i)
        m_windowedHigh[i] = m_analysisBuf[i + (m_winLow - m_winHigh)] * m_blackmanHigh[i];
    const double* onsetSrc = m_analysisBuf.data() + (m_winLow - m_winOnset);
    for (int i = 0; i < m_winOnset; ++i)
        m_windowedOnset[i] = onsetSrc[i] * m_hannOnset[i];

    if (hasBass) {
        for (int i = 0; i < m_winBass; ++i)
            m_windowedBass[i] = m_bassAnalysisBuf[i] * m_hannBass[i];
        PianoRollGoertzelBatchAvx2(
            m_windowedBass.data(), m_winBass, m_goertzelCoeffs.data(),
            0, LOW_KEY_SPLIT, m_goertzelRawScratch);
    }
    else {
        PianoRollGoertzelBatchAvx2(
            m_windowedLow.data(), m_winLow, m_goertzelCoeffs.data(),
            0, LOW_KEY_SPLIT, m_goertzelRawScratch);
    }
    for (int i = 0; i < LOW_KEY_SPLIT; ++i)
        m_rawStrengths[i] = ApplyDisplayScale((float)m_goertzelRawScratch[i], i);

    PianoRollGoertzelBatchAvx2(
        m_windowedHigh.data(), m_winHigh, m_goertzelCoeffs.data(),
        LOW_KEY_SPLIT, KEY_COUNT, m_goertzelRawScratch);
    for (int i = LOW_KEY_SPLIT; i < KEY_COUNT; ++i)
        m_rawStrengths[i] = ApplyDisplayScale(
            (float)m_goertzelRawScratch[i - LOW_KEY_SPLIT], i);

    PianoRollGoertzelBatchAvx2(
        m_windowedOnset.data(), m_winOnset, m_goertzelCoeffs.data(),
        0, KEY_COUNT, m_goertzelRawScratch);
    for (int i = 0; i < KEY_COUNT; ++i) {
        m_onsetStrengths[i] = ApplyDisplayScale((float)m_goertzelRawScratch[i], i);
    }

    for (int i = 0; i < KEY_COUNT; ++i) {
        float alpha;
        if (i < Cfg::BAND_BASS_END)
            alpha = Cfg::IIR_ALPHA_BASS;
        else
            alpha = Cfg::IIR_ALPHA;
        m_smoothedStrengths[i] =
            m_smoothedStrengths[i] * (1.0f - alpha) + m_rawStrengths[i] * alpha;
    }

    UpdateNoteStates();
    PushFrame(false);
}

// 最新の Goertzel 振幅値(m_rawStrengths / m_smoothedStrengths)をもとに
// 88 鍵それぞれのノートオン/オフ・強度・セグメント ID を更新する。
//
// 処理の流れ:
//   1. 各帯域を NormalizeBandPeak で正規化(帯域内最大を DISPLAY_PEAK_CAP でキャップ)
//   2. バス/ミッド/トレブル別のファンダメンタルピック → SuppressFalseSubharmonicPicks
//   3. ストリングス補完ピック(平滑強度の局所最大値)
//   4. RETRIGGER_RATIO 未満への落ち込みで再トリガー → segmentId をインクリメント
//   5. ATTACK_FRAMES / RELEASE_FRAMES で包絡的な ON/OFF 判定
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
    NormalizeBandPeak(pickStrength, BAND_BASS_END, BAND_MID_LO_END, DISPLAY_PEAK_CAP);
    NormalizeBandPeak(pickStrength, BAND_MID_LO_END, BAND_MID_END, DISPLAY_PEAK_CAP);
    NormalizeBandPeak(pickStrength, BAND_MID_END, BAND_TRE_HI_START, DISPLAY_PEAK_CAP);
    NormalizeBandPeak(pickStrength, BAND_TRE_HI_START, KEY_COUNT, DISPLAY_PEAK_CAP);
    NormalizeBandPeak(trackStrength, 0, BAND_BASS_END, DISPLAY_PEAK_CAP);
    NormalizeBandPeak(trackStrength, BAND_BASS_END, BAND_MID_LO_END, DISPLAY_PEAK_CAP);
    NormalizeBandPeak(trackStrength, BAND_MID_LO_END, BAND_MID_END, DISPLAY_PEAK_CAP);
    NormalizeBandPeak(trackStrength, BAND_MID_END, BAND_TRE_HI_START, DISPLAY_PEAK_CAP);
    NormalizeBandPeak(trackStrength, BAND_TRE_HI_START, KEY_COUNT, DISPLAY_PEAK_CAP);

    const float pickScale = PickThreshScaleFromLevelDb(m_bufwav3LevelDb);
    const float treSrScale = TrebleThreshScaleFromSampleRate(m_inputSampleRate);
    const bool polyFrame = FrameLooksPolyphonic(pickStrength);

    float maxS = 0.0f;
    for (int i = 0; i < KEY_COUNT; ++i)
        if (pickStrength[i] > maxS) maxS = pickStrength[i];

    const float bassMax = BandMaxStrength(pickStrength, 0, BAND_BASS_END);
    const float midMax = BandMaxStrength(pickStrength, BAND_BASS_END, BAND_MID_END);
    const float treMax = max(BandMaxStrength(pickStrength, BAND_MID_END, BAND_TRE_HI_START),
        BandMaxStrength(pickStrength, BAND_TRE_HI_START, KEY_COUNT));
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
    ApplyBandFundamentalPick(pickStrength, picked, BAND_MID_END, BAND_TRE_HI_START,
        TRE_PICK_THRESH * pickScale * treSrScale, polyFrame);
    ApplyBandFundamentalPick(pickStrength, picked, BAND_TRE_HI_START, KEY_COUNT,
        TRE_HI_PICK_THRESH * pickScale * treSrScale, polyFrame);

    {
        auto SustainStringPick = [&](int lo, int hi, float rel) {
            const float trkMax = BandMaxStrength(trackStrength, lo, hi);
            if (trkMax < 1e-6f) return;
            const float minT = trkMax * rel;
            for (int i = lo; i < hi; ++i) {
                if (picked[i]) continue;
                if (trackStrength[i] < minT) continue;
                const float l = (i > lo) ? trackStrength[i - 1] : 0.0f;
                const float r = (i + 1 < hi) ? trackStrength[i + 1] : 0.0f;
                if (trackStrength[i] >= l && trackStrength[i] >= r)
                    picked[i] = true;
            }
        };
        SustainStringPick(BAND_BASS_END, BAND_MID_END, 0.24f);
        SustainStringPick(BAND_MID_END, BAND_TRE_HI_START, 0.30f);
        SustainStringPick(BAND_TRE_HI_START, KEY_COUNT, 0.16f);
    }

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
    // O5以上(>=LOW_KEY_SPLIT)はクロス帯域の倍音整理から除外する。
    // 高音の実音(旋律)はベース/中音のオクターブ・5度上に当たりやすく、
    // 通しの倍音整理だと「下の音の倍音」とみなされ誤除去され「鳴ってるのに出ない」。
    // 高音帯は ApplyBandFundamentalPick 内で帯域内の倍音抑制・局所ピーク整理済み。
    if (singleSource) {
        ResolveHarmonicPicks(pickStrength, picked, BAND_BASS_END, LOW_KEY_SPLIT);
        FilterWeakIsolatedOutliers(pickStrength, picked, BAND_BASS_END, LOW_KEY_SPLIT, 0.21f);
        ResolveHarmonicPicksLight(pickStrength, picked, 0, BAND_BASS_END);
    }
    else {
        // 複音時は中高音旋律帯の倍音整理をスキップ（伴奏の下音に飲まれる）
        ResolveHarmonicPicksLight(pickStrength, picked, 0, BAND_BASS_END);
    }

    for (int i = LOW_KEY_SPLIT; i < KEY_COUNT; ++i) {
        if (!picked[i]) continue;
        if (polyFrame && i >= BAND_TRE_HI_START) continue;
        for (int j = 0; j < i; ++j) {
            if (!PianoKey::IsHarmonicPair(i, j)) continue;
            if (polyFrame && !picked[j] && !m_activeKeys[j]) continue;
            const bool crossBand = (j < BAND_MID_END && i >= BAND_MID_END);
            if (crossBand) {
                if (m_rawStrengths[j] >= m_rawStrengths[i] * 0.24f &&
                    !TrebleLooksIndependentOfMid(pickStrength, m_rawStrengths, i)) {
                    picked[i] = false;
                    break;
                }
            }
            else {
                const float stripRatio = polyFrame ? 0.60f : 0.48f;
                if (pickStrength[j] >= pickStrength[i] * stripRatio) {
                    picked[i] = false;
                    break;
                }
            }
        }
    }

    // 低音は8192窓の解像度不足で1音が±2〜3半音に漏れ、隣接団子(ゴースト)になる。
    // 周波数が低いほどスナップ半径・統合ギャップを広げ、漏れを1音へ収束させる。
    // 深低音(C2=key15未満)は最も漏れが広いので最大、O3付近は控えめ。
    SnapPicksToLocalMaxima(pickStrength, picked, 0, 15, 5);
    SnapPicksToLocalMaxima(pickStrength, picked, 15, BAND_BASS_END, 4);
    SnapPicksToLocalMaxima(pickStrength, picked, BAND_BASS_END, BAND_MID_LO_END, 2);
    // 複音時は高音旋律を倍音整理で落としやすいのでスキップ（単音時のみ整理）
    if (!polyFrame) {
        ResolveHarmonicPicksLight(pickStrength, picked, BAND_MID_END, BAND_TRE_HI_START);
        ResolveHarmonicPicksLight(pickStrength, picked, BAND_TRE_HI_START, KEY_COUNT);
    }
    SnapPicksToLocalMaxima(pickStrength, picked, BAND_MID_LO_END, BAND_MID_END, 1);
    SnapPicksToLocalMaxima(pickStrength, picked, BAND_MID_END, BAND_TRE_HI_START, 1);
    SnapPicksToLocalMaxima(pickStrength, picked, BAND_TRE_HI_START, KEY_COUNT, 1);
    CollapseNearbyPicks(pickStrength, picked, 0, 15, 5, false);
    CollapseNearbyPicks(pickStrength, picked, 0, BAND_BASS_END, 4, false);
    CollapseNearbyPicks(pickStrength, picked, BAND_BASS_END, BAND_MID_LO_END, 2, false);
    CollapseNearbyPicks(pickStrength, picked, BAND_MID_LO_END, BAND_MID_END, 2, false);
    CollapseNearbyPicks(pickStrength, picked, BAND_MID_END, BAND_TRE_HI_START, 2, false);
    CollapseNearbyPicks(pickStrength, picked, BAND_TRE_HI_START, KEY_COUNT, 3, false);
    FilterWeakIsolatedOutliers(pickStrength, picked, BAND_MID_END, BAND_TRE_HI_START, 0.21f);
    FilterWeakIsolatedOutliers(pickStrength, picked, BAND_TRE_HI_START, KEY_COUNT, 0.30f);
    SupplementHighBellPeaks(pickStrength, m_rawStrengths, picked, BAND_TRE_HI_START, KEY_COUNT, 0.085f);
    PruneHighFrequencyClutter(pickStrength, picked, m_activeKeys);
    ReinstateActiveHighBells(pickStrength, m_rawStrengths, picked, m_activeKeys);
    SuppressCrossBandHarmonicLeaks(pickStrength, m_rawStrengths, picked, m_activeKeys);
    if (polyFrame) {
        SupplementPolyMelodyPeaks(pickStrength, m_rawStrengths, picked,
            BAND_MID_LO_END, BAND_TRE_HI_START, 0.12f);
        PrunePolyMelodyHarmonicDupes(pickStrength, picked, BAND_MID_LO_END, BAND_TRE_HI_START);
        FilterWeakIsolatedOutliers(pickStrength, picked, BAND_MID_LO_END, BAND_TRE_HI_START, 0.23f);
    }

    const bool percussiveFrame = FrameLooksPercussiveBurst(
        pickStrength, m_onsetStrengths, m_prevOnsetStrengths, picked);
    if (percussiveFrame)
        SuppressPercussivePicks(pickStrength, m_rawStrengths, picked);

    const float treHiBandMax = BandMaxStrength(pickStrength, BAND_TRE_HI_START, KEY_COUNT);

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

        if (effectivePicked && i >= BAND_MID_END &&
            IsMidHarmonicLeak(pickStrength, m_rawStrengths, i, picked, m_activeKeys))
            effectivePicked = false;

        if (!effectivePicked && i < BAND_BASS_END) {
            const float onsetDelta = m_onsetStrengths[i] - m_prevOnsetStrengths[i];
            if (onsetDelta >= BASS_ONSET_DELTA_THRESH &&
                m_onsetStrengths[i] >= BASS_ONSET_MIN_STRENGTH &&
                pickStrength[i] >= BASS_PICK_THRESH * pickScale * 0.40f) {
                effectivePicked = true;
            }
        }

        if (!effectivePicked && polyFrame && i >= BAND_BASS_END && i < BAND_MID_END) {
            const float onsetDelta = m_onsetStrengths[i] - m_prevOnsetStrengths[i];
            if (onsetDelta >= MID_ONSET_DELTA_THRESH &&
                m_onsetStrengths[i] >= MID_ONSET_MIN_STRENGTH &&
                pickStrength[i] >= MID_PICK_THRESH * pickScale * 0.40f &&
                !PianoKey::IsHarmonicOfAnyActive(pickStrength, i, picked, 0, KEY_COUNT, KEY_COUNT, 0.80f))
                effectivePicked = true;
        }

        if (!effectivePicked && i >= BAND_TRE_HI_START) {
            const float onsetDelta = m_onsetStrengths[i] - m_prevOnsetStrengths[i];
            const bool strongBell = (treHiBandMax > 1e-6f &&
                pickStrength[i] >= treHiBandMax * 0.44f);
            const bool continuing = m_activeKeys[i] && pickStrength[i] >= treHiBandMax * 0.12f &&
                !IsMidHarmonicLeak(pickStrength, m_rawStrengths, i, picked, m_activeKeys);
            if (continuing) {
                effectivePicked = true;
            }
            else if (onsetDelta >= ONSET_DELTA_THRESH * 0.78f * treSrScale &&
                m_onsetStrengths[i] >= ONSET_MIN_STRENGTH * 0.72f * treSrScale &&
                pickStrength[i] >= TRE_HI_PICK_THRESH * pickScale * 0.48f * treSrScale &&
                IsLocalPeakInBand(pickStrength, i, BAND_TRE_HI_START, KEY_COUNT) &&
                (strongBell || LooksLikeTonalFundamental(m_rawStrengths, i)) &&
                !PianoKey::IsHarmonicOfAnyActive(pickStrength, i, picked, 0, KEY_COUNT, KEY_COUNT, 0.82f))
                effectivePicked = true;
        }

        if (!effectivePicked && i >= ONSET_KEY_START && i < BAND_TRE_HI_START) {
            const float onsetDelta = m_onsetStrengths[i] - m_prevOnsetStrengths[i];
            if (onsetDelta >= ONSET_DELTA_THRESH * treSrScale &&
                m_onsetStrengths[i] >= ONSET_MIN_STRENGTH * treSrScale &&
                pickStrength[i] >= TRE_PICK_THRESH * pickScale * 0.55f * treSrScale &&
                IsLocalPeakInBand(pickStrength, i, BAND_MID_END, KEY_COUNT) &&
                !PianoKey::IsHarmonicOfAnyActive(pickStrength, i, picked, 0, KEY_COUNT, KEY_COUNT))
                effectivePicked = true;
        }

        // ハープ等: 上の音が残っていても、下が今フレームでピックされていれば下降音を採用
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

        if (percussiveFrame && !m_activeKeys[i] && effectivePicked) {
            if (i >= BAND_BASS_END) {
                if (!LooksLikeTonalFundamental(m_rawStrengths, i))
                    effectivePicked = false;
            }
            else if (!PianoKey::PassesFundamentalTestSustain(m_rawStrengths, i, KEY_COUNT)) {
                effectivePicked = false;
            }
        }

        m_noteStrength[i] = effectivePicked ? m_rawStrengths[i] : 0.0f;

        if (effectivePicked) {
            ++m_consecActive[i];
            m_consecSilent[i] = 0;
            m_unpickedFrames[i] = 0;
            float envSample = sigStrength;
            if (i >= BAND_TRE_HI_START) {
                if (pickStrength[i] > envSample) envSample = pickStrength[i];
                if (trackStrength[i] > envSample) envSample = trackStrength[i];
            }
            if (envSample > m_envPeak[i])
                m_envPeak[i] = envSample;
            else
                m_envPeak[i] = m_envPeak[i] * 0.92f + envSample * 0.08f;
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
            if (effectivePicked &&
                m_consecActive[i] >= RequiredAttackFrames(i, pickStrength[i], treHiBandMax)) {
                cur = true;
                m_consecSilent[i] = 0;
                ++m_segmentId[i];
                float envSample = sigStrength;
                if (i >= BAND_TRE_HI_START) {
                    if (pickStrength[i] > envSample) envSample = pickStrength[i];
                    if (trackStrength[i] > envSample) envSample = trackStrength[i];
                }
                m_envPeak[i] = envSample;
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
            const int releaseBase = (i >= BAND_TRE_HI_START) ? (RELEASE_FRAMES + 3) : RELEASE_FRAMES;
            const int releaseLimit = TemporalFrames(i, releaseBase);
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
        // 表示バー高は包絡ピーク(帯域正規化済み)を使う。生強度だとサステイン中に
        // 細い線になり、オンセットだけ太いゴーストと混在して見える。
        float disp = m_envPeak[i];
        if (i >= BAND_TRE_HI_START) {
            if (pickStrength[i] > disp) disp = pickStrength[i];
            if (trackStrength[i] > disp) disp = trackStrength[i];
        }
        if (disp <= 0.0f) disp = pickStrength[i];
        if (disp <= 0.0f) disp = trackStrength[i];
        if (disp <= 0.0f) disp = m_rawStrengths[i];
        m_noteStrength[i] = disp;
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
    static constexpr uint8_t ACCENT  = 0x01;
    static constexpr uint8_t SCOOP   = 0x02;
    static constexpr uint8_t VIBRATO = 0x04;
    static constexpr uint8_t SLIDE   = 0x08;
    static constexpr uint8_t FALL    = 0x10;
    static constexpr uint8_t SUSTAIN = 0x20;
    static constexpr uint8_t CRESC   = 0x40;   // クレッシェンド(持続音が膨らむ)
    static constexpr uint8_t DECRESC = 0x80;   // デクレッシェンド(持続音がしぼむ)
    static constexpr uint8_t ALL_MASK = ACCENT | SCOOP | VIBRATO | SLIDE | FALL | SUSTAIN | CRESC | DECRESC;
}

// UpdateNoteStates の直後に呼ばれ、各アクティブノートへ表現記号フラグを付与する。
// 検出ロジック概要:
//   SCOOP   … 直前フレームで隣のキーがアクティブだった(音程が下から上がってきた)
//   SLIDE   … 上下隣キーからの遷移
//   FALL    … 上隣キーからの遷移(下降消音)
//   ACCENT  … ノートオン直後(age <= 3)に強度が急上昇
//   SUSTAIN … 12フレーム以上継続(持続音・ストリングス)
//   VIBRATO … 強度の周期的変動を VIB_HIST_LEN 分の自己相関で検出
void CPianoRoll::DetectExpressions()
{
    using namespace Cfg;

    for (int i = 1; i < KEY_COUNT; ++i) {
        if (m_scoopLatch[i] > 0) --m_scoopLatch[i];
        if (m_activeKeys[i - 1] && i < BAND_TRE_HI_START)
            m_scoopLatch[i] = 5;
    }

    for (int i = 0; i < KEY_COUNT; ++i) {
        m_exprFlags[i] = 0;
        const bool wasActive = m_prevActiveKeys[i];
        const bool nowActive = m_activeKeys[i];
        const bool hiScatter = (i >= BAND_TRE_HI_START);

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

        // クレッシェンド/デクレッシェンド: 持続音(SUSTAIN)で、強度が一方向へ
        // はっきり推移しているときだけ付与する。ビブラート(揺れ)とは排他。
        // 自然減衰の撥弦音を拾わないよう、持続が十分長く振幅も保っている音に限定。
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
        CPen pen(PS_SOLID, 1, col);
        CPen* pOld = dc.SelectObject(&pen);
        dc.Polyline(pts, nPts);
        dc.SelectObject(pOld);
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

    ReleaseExprLegendCache();

    if (!m_legendDC.CreateCompatibleDC(&refDC)) return false;
    if (!m_legendBmp.CreateCompatibleBitmap(&refDC, pw, ph)) {
        m_legendDC.DeleteDC();
        return false;
    }
    m_legendOldBmp = m_legendDC.SelectObject(&m_legendBmp);

    DrawExprLegendContent(m_legendDC, rollW, rollH, CRect(0, 0, pw, ph));

    m_legendW = pw;
    m_legendH = ph;
    m_legendCacheRollW = rollW;
    m_legendCacheRollH = rollH;
    m_legendReady = true;
    return true;
}

// 凡例パネル背景を半透明で塗る(下のバーを透かす)。1x1のソースを引き伸ばして
// AlphaBlend する軽量実装(msimg32 の AlphaBlend を CDC 経由で使用)。
static void PianoFillRectAlpha(CDC& dc, const CRect& rc, COLORREF clr, BYTE alpha)
{
    if (rc.Width() <= 0 || rc.Height() <= 0) return;
    CDC mem;
    if (!mem.CreateCompatibleDC(&dc)) return;
    CBitmap bmp;
    if (!bmp.CreateCompatibleBitmap(&dc, 1, 1)) { mem.DeleteDC(); return; }
    CBitmap* ob = mem.SelectObject(&bmp);
    mem.SetPixelV(0, 0, clr);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, alpha, 0 };
    dc.AlphaBlend(rc.left, rc.top, rc.Width(), rc.Height(), &mem, 0, 0, 1, 1, bf);
    mem.SelectObject(ob);
    mem.DeleteDC();
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

void CPianoRoll::DrawExprLegendContent(CDC& dc, int rollW, int rollH, const CRect& panel) const
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
    PianoFillRectAlpha(dc, panel, RGB(14, 14, 20), 170);
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
            CBrush br(col);
            CBrush* pOld = dc.SelectObject(&br);
            CPen pen(PS_SOLID, 1, col);
            CPen* pOldPen = dc.SelectObject(&pen);
            POINT pts[4] = {
                { xLs + 1, midOld }, { xRs - 1, midOld },
                { xRd - 1, midNew }, { xLd + 1, midNew }
            };
            dc.Polygon(pts, 4);
            dc.SelectObject(pOld);
            dc.SelectObject(pOldPen);
        }
    }
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

// 1フレーム分だけロールバッファをスクロールアップして最新行を下端に描く。
// スクロール処理: ロールバッファ全体を scrollPx 分 BitBlt で上へずらし、
// 空いた下端に DrawPlayheadRow で最新フレームを描画する。
// ComposeRollBuffer(全再描画)と異なり毎フレームの差分更新のみで済む。
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
    // 凡例はここでは焼き込まない(OnPaint で半透明オーバーレイ描画)。
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
    // V-Sync には縛らず、解析が出来たフレームからどんどん描画する(自由走行)。
    // V-Sync 同期にするとかえってカクついたため、解析完了ごとに再描画して
    // 流れるようにスクロールさせる。
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
    m_hAnalysisWake = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!m_hAnalysisWake) return;
    m_hAnalysisThread = CreateThread(
        NULL, 0, AnalysisWorkerThreadEntry, this, 0, NULL);
    if (!m_hAnalysisThread) {
        CloseHandle(m_hAnalysisWake);
        m_hAnalysisWake = NULL;
    }
}

// m_workerStop フラグを立てて SetEvent でワーカーを起こし、終了を最大10秒待つ。
// デストラクタと DetachForDestroy から呼ばれる。
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

// ワーカースレッドのメインループ。イベント待ちで眠り、起こされたら
// m_jobPending を CAS で取得して ProcessAnalysisJob を実行する。
// 解析完了後、::IsWindow チェックを挟んでから PostMessage するのは
// ウィンドウが既に破棄されている場合の HWND 再利用バグを防ぐため。
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

// ジョブバッファをローカルにコピーしてから m_jobCs を解放し、
// 長い Goertzel 演算中はジョブバッファを解放しておく(再生スレッドが
// 次のジョブを書き込める状態を保つ)。
// 結果は m_goertzelRawScratch へ書き込み、OnAnalysisDone で参照される。
bool CPianoRoll::ProcessAnalysisJob()
{
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

    if (frameCount < MinAnalyzeFrameCount(sampleRate, frameCount) || sampleRate < 8000)
        return false;

    const double* mono = m_workerMonoScratch.data();

    EnterCriticalSection(&m_cs);
    EnsureAnalysisTables(sampleRate, frameCount);
    if (frameCount < m_winLow) {
        LeaveCriticalSection(&m_cs);
        return false;
    }
    const double* lowWin = mono + (frameCount - m_winLow);
    const int bassLen = (frameCount >= m_winBass) ? m_winBass : m_winLow;
    const double* bassWin = (frameCount >= m_winBass)
        ? mono + (frameCount - m_winBass)
        : lowWin;

    RunGoertzelFromBuffer(lowWin, bassWin, bassLen);
    LeaveCriticalSection(&m_cs);
    return true;
}

void CPianoRoll::DrawKeyboardToBuffer(CDC& memDC, int width, int keySectionH, int keyH,
    const bool* activesCopy, const uint8_t* bandMaskCopy, const float laneStrengthCopy[KEY_COUNT][3],
    const float* chFillCopy, int chCountCopy, const uint8_t* exprCopy) const
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

    // アクティブキーに表現記号を重ねる（履歴バーと同じグリフをキー側にも表示）。
    // クロマチックキー上部(keyTop付近)に主要フラグのグリフを1つ描く。
    if (exprCopy && m_paintFontsReady && bkH >= 8) {
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
    uint8_t exprCopy[KEY_COUNT];
    int chCountCopy = 0;
    EnterCriticalSection(&m_cs);
    BuildLiveNoteFrame(liveSnap);
    memcpy(activesCopy, m_activeKeys, sizeof(m_activeKeys));
    memcpy(bandMaskCopy, m_bandMask, sizeof(m_bandMask));
    memcpy(laneStrengthCopy, m_laneStrength, sizeof(m_laneStrength));
    memcpy(exprCopy, m_exprFlags, sizeof(m_exprFlags));
    chCountCopy = m_chMeterCount;
    memcpy(chFillCopy, m_chMeterFill, sizeof(chFillCopy));
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

    // スクロール経路は履歴フレームを使わない(TryAdvanceRollBufferはBitBltのみ)。
    // 全描画(ComposeRollBuffer)が必要なときだけ履歴120フレーム(約253KB)をコピーする。
    if (pending > 0 && m_rollReady
        && TryAdvanceRollBuffer(w, rollH, 0, nullptr, pending, liveSnap)) {
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
    const bool haveLegend = m_rollReady && !lgPanel.IsRectEmpty() && m_rollDC.GetSafeHdc();
    CDC   lgBgDC;
    CBitmap lgBgBmp;
    CBitmap* lgBgOld = nullptr;
    bool legendBaked = false;
    if (haveLegend) {
        const int pw = lgPanel.Width(), ph = lgPanel.Height();
        if (lgBgDC.CreateCompatibleDC(&dc) && lgBgBmp.CreateCompatibleBitmap(&dc, pw, ph)) {
            lgBgOld = lgBgDC.SelectObject(&lgBgBmp);
            lgBgDC.BitBlt(0, 0, pw, ph, &m_rollDC, lgPanel.left, lgPanel.top, SRCCOPY); // 下地退避
            DrawExprLegendContent(m_rollDC, w, rollH, lgPanel);                          // 焼き込み(α合成)
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
                if (didRollScroll && m_lastScrollPx > 0 && m_chromaReady) {
                    // スクロール時: キャッシュDIBを memmove で繰り上げ、
                    // チャネルキー→アルファ変換は変化領域(下端の新ライブ行＋凡例)のみ。
                    // ロール全域(O(w×rollH))の再変換を避けてアクリル時を大幅軽量化。
                    m_chromaCache.ScrollRows(0, rollH, m_lastScrollPx);
                    const int rowPitch = HistoryRowPitch(rollH);
                    int bandTop = rollH - m_lastScrollPx - rowPitch - 2;
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
	if (legendBaked) {
		m_rollDC.BitBlt(lgPanel.left, lgPanel.top, lgPanel.Width(), lgPanel.Height(),
			&lgBgDC, 0, 0, SRCCOPY);
		if (lgBgOld) lgBgDC.SelectObject(lgBgOld);
	}

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
