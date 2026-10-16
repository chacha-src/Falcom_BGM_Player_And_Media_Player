#pragma once
// 108鍵ピアノロールの基音推定
//
// 入力は PianoRollPeakSpectrum が作る
//   - 鍵帯域の信号パワー blend[k]
//   - 鍵帯域の雑音床パワー floorPow[k]
//   - FFT 連続周波数での調波サリエンス fftSal[k] (任意)
//
// 反復は常に「低い候補への昇格」を含めて進む。高音に立ったピークは、それを倍音として
// 持つ低〜中音があればまず下の基音へ回収され、その倍音包絡が引かれる。だから
// 低〜中音が先に立ち、その倍音が高音へ化けない。
#include <cmath>
#include <cstring>
#include "PianoKeyTable.h"

namespace PianoRoll108
{
    static constexpr int COUNT = PianoKey::COUNT;

    static constexpr int WIN_LONG_END = 60;
    static constexpr int WIN_MID_END = 84;

    static constexpr int BASS_END = 48;   // C3
    static constexpr int MID_END = 72;    // C5
    static constexpr int C4_KEY = 60;
    static constexpr int O5_HI = 84;      // C6
    static constexpr int EDGE_HI = 96;    // C7
    static constexpr int MUSIC_LOW_FLOOR = 24; // C1

    inline int KeyBandIndex(int keyIndex)
    {
        if (keyIndex < BASS_END) return 0;
        if (keyIndex < MID_END) return 1;
        return 2;
    }

    inline float IirAlphaForKey(int keyIndex)
    {
        if (keyIndex < BASS_END) return 0.30f;
        if (keyIndex < MID_END) return 0.42f;
        return 0.48f;
    }

    inline void BuildDetectionSpectrum(const float* smoothed, const float* raw, float* out, int count)
    {
        if (!smoothed || !raw || !out || count <= 0) return;
        for (int i = 0; i < count; ++i)
            out[i] = smoothed[i] * 0.45f + raw[i] * 0.55f;
    }

    inline bool OnsetSupportsPick(const float* onset, const float* prevOnset,
        int keyIndex, float levelScale, float onsetDeltaScale = 1.0f)
    {
        if (!onset || !prevOnset || keyIndex < 0 || keyIndex >= COUNT) return false;
        float oMax = 0.0f;
        for (int i = 0; i < COUNT; ++i)
            if (onset[i] > oMax) oMax = onset[i];
        if (oMax < 0.004f) return false;
        float scale = levelScale;
        if (scale < 0.70f) scale = 0.70f;
        if (scale > 1.10f) scale = 1.10f;
        float od = onsetDeltaScale;
        if (od < 0.25f) od = 0.25f;
        if (od > 4.0f) od = 4.0f;
        const float delta = onset[keyIndex] - prevOnset[keyIndex];
        return onset[keyIndex] >= oMax * 0.20f * scale &&
            delta >= oMax * 0.12f * od;
    }

    inline bool IsBroadbandTransient(const float* onset, const float* prevOnset, float strict)
    {
        if (!onset || !prevOnset) return false;
        static constexpr int LO = 36;
        float oMax = 0.0f;
        for (int i = LO; i < COUNT; ++i)
            if (onset[i] > oMax) oMax = onset[i];
        if (oMax < 0.004f) return false;
        const float live = oMax * 0.01f;
        int rise = 0, kMin = COUNT, kMax = -1;
        for (int i = LO; i < COUNT; ++i) {
            if (onset[i] < live) continue;
            if (onset[i] <= prevOnset[i] * 2.0f + live * 0.5f) continue;
            ++rise;
            if (i < kMin) kMin = i;
            if (i > kMax) kMax = i;
        }
        float frac = 0.35f / (strict > 0.1f ? strict : 0.1f);
        if (frac < 0.10f) frac = 0.10f;
        if (frac > 0.90f) frac = 0.90f;
        return rise >= (int)((float)(COUNT - LO) * frac) && (kMax - kMin) >= 36;
    }

    static constexpr int SAL_HARM = 8;
    static constexpr int SUB_HARM = 24;
    static constexpr int MAX_NOTES = 5;
    static constexpr float W_OFFSET_DB = 6.5f;
    static constexpr float W_CAP_DB = 40.0f;
    static constexpr float SAL_MIN = 10.5f;
    static constexpr float SAL_REL = 0.24f;
    static constexpr float PROMOTE_REL = 0.55f;
    static constexpr float EXPLAINED_FRAC = 0.18f;
    static constexpr float SEMI_SPILL = 0.55f;
    static constexpr float ENV_TAU = 0.40f;
    static constexpr float MASK_OFFSET_DB = 22.0f;
    static constexpr float MASK_UP_DB_PER_BARK = 13.0f;
    static constexpr float MASK_DOWN_DB_PER_BARK = 27.0f;

    struct HarmTable
    {
        short k[COUNT][SUB_HARM];
        float bark[COUNT];
        float availFrac[COUNT];
        float w[SAL_HARM];
        HarmTable()
        {
            for (int n = 1; n <= SAL_HARM; ++n)
                w[n - 1] = powf((float)n, -0.55f);
            float wAll = 0.0f;
            for (int n = 1; n <= 4; ++n) wAll += w[n - 1];
            for (int i = 0; i < COUNT; ++i) {
                k[i][0] = (short)i;
                for (int n = 2; n <= SUB_HARM; ++n)
                    k[i][n - 1] = (short)PianoKey::HarmonicKeyOnBoard(i, n);
                const float f = PianoKey::KeyHz(i);
                const float r = f / 7500.0f;
                bark[i] = 13.0f * atanf(0.00076f * f) + 3.5f * atanf(r * r);
                float wa = 0.0f;
                for (int n = 1; n <= 4; ++n)
                    if (k[i][n - 1] >= 0) wa += w[n - 1];
                availFrac[i] = wa / wAll;
            }
        }
    };

    inline const HarmTable& Harm()
    {
        static const HarmTable t;
        return t;
    }

    inline float BandStrict(int key, float pickBassRel, float pickLowMidRel,
        float pickMelodyRel, float pickTreRel)
    {
        float rel = 1.0f;
        if (key < BASS_END) rel = pickBassRel / 0.28f * 0.82f;
        else if (key < C4_KEY) rel = pickLowMidRel / 0.20f * 0.80f;
        else if (key < MID_END) rel = pickMelodyRel / 0.10f * 0.85f;
        else if (key < O5_HI) rel = pickMelodyRel / 0.10f * 1.00f;
        else rel = pickTreRel / 0.22f * 1.25f;
        if (rel < 0.35f) rel = 0.35f;
        if (rel > 3.5f) rel = 3.5f;
        return rel;
    }

    inline float AmpFloorForKey(int key, float powerFloor)
    {
        float rel = 0.20f;
        if (key < BASS_END) rel = 0.32f;
        else if (key < C4_KEY) rel = 0.22f;
        else if (key < O5_HI) rel = 0.14f;
        else if (key < EDGE_HI) rel = 0.10f;
        else rel = 0.08f;
        float p = powerFloor * rel;
        if (p < 1.0e-8f) p = 1.0e-8f;
        return sqrtf(p);
    }

    struct PickParams
    {
        float levelScale = 1.0f;
        float absNoiseFloor = 0.00055f;
        float pickBassRel = 0.28f;
        float pickLowMidRel = 0.20f;
        float pickMelodyRel = 0.10f;
        float pickTreRel = 0.22f;
        float ghostStrict = 1.0f;
        float envMargin = 1.10f;
        bool  verifyExplained = true;
        float percussive = 0.0f;
        const float* fftHarmSal = nullptr;
    };

    inline int PickNotes(const float* blend, const float* floorPow, const bool* prevActive,
        bool* outPicked, float* outOwnPow, const PickParams& p)
    {
        if (!blend || !floorPow || !outPicked || !outOwnPow) return 0;
        memset(outPicked, 0, sizeof(bool) * COUNT);
        const HarmTable& ht = Harm();

        float scale = p.levelScale;
        if (scale < 0.55f) scale = 0.55f;
        if (scale > 1.25f) scale = 1.25f;

        float amp[COUNT], r[COUNT], nf[COUNT], db0[COUNT], thr[COUNT];
        for (int i = 0; i < COUNT; ++i) {
            amp[i] = (blend[i] > 0.0f) ? sqrtf(blend[i]) : 0.0f;
            r[i] = amp[i];
            float n0 = (floorPow[i] > 0.0f) ? sqrtf(floorPow[i]) : 0.0f;
            const float nAbs = AmpFloorForKey(i, p.absNoiseFloor) * 0.72f;
            const float nCap = nAbs * 2.40f;
            if (n0 < nAbs) n0 = nAbs;
            if (n0 > nCap) n0 = nCap;
            nf[i] = n0;
            db0[i] = (amp[i] > 0.0f) ? 20.0f * log10f(amp[i]) : -300.0f;
            float t = SAL_MIN * BandStrict(i, p.pickBassRel, p.pickLowMidRel,
                p.pickMelodyRel, p.pickTreRel) / scale * (0.45f + 0.55f * ht.availFrac[i]);
            // 前フレームで鳴っていたノートは持続ヒステリシス(0.68倍)を掛け、
            // 2分音符・全音符が途中で途切れて細切れになるのを防ぐ。
            if (prevActive && prevActive[i]) t *= 0.68f;
            thr[i] = t;
        }

        // 近傍(±4 半音以内)の強い音によるスカート漏れ・うなりをマスクする。
        float maskDb[COUNT];
        for (int i = 0; i < COUNT; ++i) {
            float mk = -300.0f;
            if (amp[i] > 0.0f) {
                for (int j = i - 4; j <= i + 4; ++j) {
                    if (j < 0 || j >= COUNT || j == i || amp[j] <= 0.0f) continue;
                    const float dSemi = (float)((i > j) ? (i - j) : (j - i));
                    const float v = db0[j] - 12.0f - dSemi * 4.8f;
                    if (v > mk) mk = v;
                }
            }
            maskDb[i] = mk;
        }

        float explained[COUNT];
        float explainedAmp[COUNT];
        bool rejected[COUNT];
        memset(explained, 0, sizeof(explained));
        memset(explainedAmp, 0, sizeof(explainedAmp));
        memset(rejected, 0, sizeof(rejected));

        int nPicked = 0;
        float firstSal = 0.0f;
        float W[COUNT], sal[COUNT], fundPow[COUNT];
        memset(fundPow, 0, sizeof(fundPow));

        // 基音自体が雑音床の下でも、1 本の減衰した倍音列が揃っていれば基音として起こす。
        auto buriedSeries = [&](int key) -> bool {
            if (key < MUSIC_LOW_FLOOR || key >= MID_END) return false;
            int supp = 0, lowN = 0, peaks = 0;
            float mx = 0.0f, a2 = 0.0f, a3 = 0.0f;
            for (int n = 2; n <= 8; ++n) {
                const int hk = ht.k[key][n - 1];
                if (hk < 0 || W[hk] <= 0.0f || r[hk] <= 0.0f) continue;
                ++supp;
                if (n <= 4) ++lowN;
                if (n == 2) a2 = r[hk];
                if (n == 3) a3 = r[hk];
                if (n <= 6 && r[hk] > mx) mx = r[hk];
            }
            if (lowN < 3 || supp < 5 || mx <= 0.0f) return false;
            for (int n = 2; n <= 6; ++n) {
                const int hk = ht.k[key][n - 1];
                if (hk >= 0 && r[hk] > mx * 0.70f) ++peaks;
            }
            if (peaks > 2) return false;
            if (!(a2 >= mx * 0.99f || a3 >= mx * 0.99f)) return false;
            const int k4 = ht.k[key][3];
            const float a4 = (k4 >= 0) ? r[k4] : 0.0f;
            float top = a2;
            if (a3 > top) top = a3;
            if (a4 > top) top = a4;
            if (a2 < top * 0.35f || a3 < top * 0.28f || a4 < top * 0.20f) return false;
            const int kLo = ht.k[key][1];
            const int kHi = ht.k[key][5];
            if (kLo >= 0 && kHi > kLo) {
                const float gate = top * 0.45f;
                for (int j = kLo + 1; j < kHi; ++j) {
                    if (r[j] <= gate || W[j] <= 0.0f) continue;
                    bool slot = false;
                    for (int n = 2; n <= 6 && !slot; ++n)
                        if (ht.k[key][n - 1] == j) slot = true;
                    if (!slot) return false;
                }
            }
            return true;
        };

        for (int phase = 0; phase < 2; ++phase)
        for (int iter = 0; iter < COUNT && nPicked < MAX_NOTES; ++iter) {
            for (int k = 0; k < COUNT; ++k) {
                float v = 0.0f;
                if (r[k] > 0.0f) {
                    v = 20.0f * log10f(r[k] / nf[k]) - W_OFFSET_DB;
                    if (v < 0.0f) v = 0.0f;
                    if (v > W_CAP_DB) v = W_CAP_DB;
                }
                W[k] = v;
            }

            int best = -1;
            float bestSal = 0.0f;
            for (int k = 0; k < COUNT; ++k) {
                sal[k] = 0.0f;
                if (k < MUSIC_LOW_FLOOR || outPicked[k] || rejected[k]) continue;
                float s = 0.0f;
                int supp = 0;
                for (int n = 1; n <= SAL_HARM; ++n) {
                    const int hk = ht.k[k][n - 1];
                    if (hk < 0 || W[hk] <= 0.0f) continue;
                    s += ht.w[n - 1] * W[hk];
                    ++supp;
                }
                // FFT 連続周波数の調波サリエンス。まだどの倍音も引かれていない鍵で、
                // 鍵中心からわずかに外れた倍音がある場合にのみ微調整として使う。
                if (p.fftHarmSal && k < C4_KEY && explained[k] <= 0.0f && supp >= 2) {
                    const float fs = p.fftHarmSal[k];
                    if (fs > s && fs < s * 1.35f) s = fs;
                }
                sal[k] = s;
                const bool buried = (W[k] <= 0.0f);
                if (phase == 0) {
                    if (buried) {
                        if (!buriedSeries(k)) continue;
                    }
                    else if (supp < 2 && (k < 48 || W[k] < 12.0f)) {
                        // phase 0 では倍音を持つ音、または中〜高域で明確に強い基音を先に拾う。
                        continue;
                    }
                    if (!buried && db0[k] < maskDb[k]) continue;
                }
                else {
                    if (buried || db0[k] < maskDb[k]) continue;
                    // 低音(C3=key48未満)の単発ピーク(supp<2)はバスドラムのドン鳴りなので弾く。
                    if (supp < 2 && (k < BASS_END || W[k] < 13.0f)) continue;
                }
                if (s < thr[k]) continue;
                const float relNeed = (prevActive && prevActive[k]) ? (SAL_REL * 0.70f) : SAL_REL;
                if (nPicked > 0 && s < firstSal * relNeed) continue;
                // 前フレームから持続中の鍵はサリエンスに持続ボーナスを与え、別鍵へのランダムな飛び移りを抑える
                const float score = (prevActive && prevActive[k]) ? (s * 1.18f) : s;
                if (score > bestSal) {
                    bestSal = score;
                    best = k;
                }
            }
            if (best < 0) break;

            // 高次倍音(2〜6 次)が先に最大になった場合、その真の基音が低〜中音にあれば昇格する。
            for (bool moved = true; moved; ) {
                moved = false;
                for (int m = 2; m <= 6 && !moved; ++m) {
                    const int lo = PianoKey::HarmonicDownKeyAny(best, m);
                    if (lo < MUSIC_LOW_FLOOR || lo >= best || ht.k[lo][m - 1] != best) continue;
                    if (outPicked[lo] || rejected[lo]) continue;
                    if (W[lo] <= 0.0f && !buriedSeries(lo)) continue;
                    if (sal[lo] < thr[lo] || sal[lo] < bestSal * PROMOTE_REL) continue;
                    int ind = 0;
                    for (int n = 2; n <= SAL_HARM; ++n) {
                        if (n % m == 0) continue;
                        const int hk = ht.k[lo][n - 1];
                        if (hk >= 0 && W[hk] > 0.0f) ++ind;
                    }
                    if (ind < 1) continue;
                    best = lo;
                    bestSal = sal[lo];
                    moved = true;
                }
            }

            bool ok = true;
            const float a0 = amp[best];
            if (p.verifyExplained && explained[best] > EXPLAINED_FRAC * a0 * a0) {
                float keep = 0.48f * p.ghostStrict;
                if (keep < 0.25f) keep = 0.25f;
                if (keep > 0.85f) keep = 0.85f;
                if (r[best] < a0 * keep) {
                    ok = false;
                }
                else {
                    int avail = 0, own = 0;
                    for (int n = 2; n <= 4; ++n) {
                        const int hk = ht.k[best][n - 1];
                        if (hk < 0) continue;
                        ++avail;
                        if (W[hk] > 0.0f && r[hk] >= amp[hk] * (keep * 0.75f)) ++own;
                    }
                    if (avail > 0 ? own < 1 : W[best] < 14.0f * p.ghostStrict) ok = false;
                }
            }
            // 高音域(G5 以上): 下にまだ選んでいない基音候補があり、その倍音列の 1 本として
            // 自然な大きさなら、実音ではなく倍音とみなして見送る。
            if (ok && best >= 79) {
                for (int j = MUSIC_LOW_FLOOR; j < best && ok; ++j) {
                    if (amp[j] <= a0 * 0.8f && !outPicked[j]) continue;
                    for (int n = 2; n <= SUB_HARM; ++n) {
                        if (ht.k[j][n - 1] != best) continue;
                        int sib = 0;
                        for (int m = 2; m <= 8; ++m) {
                            if (m == n) continue;
                            const int sk = ht.k[j][m - 1];
                            if (sk >= 0 && amp[sk] > nf[sk] * 2.0f) ++sib;
                        }
                        if (sib >= 2 && a0 <= amp[j] / powf((float)n, 0.7f) * 1.6f)
                            ok = false;
                        break;
                    }
                }
            }
            // ドラム過渡ガードは高音域(ハイハット・シンバル帯域)のみに適用し、
            // 拍頭で同時に鳴る低音(ベース)や中音(コード・メロディ)を巻き添えで消さない。
            if (ok && best >= MID_END && p.percussive > 0.3f && !(prevActive && prevActive[best])) {
                int avail = 0, sup = 0;
                for (int n = 2; n <= 5; ++n) {
                    const int hk = ht.k[best][n - 1];
                    if (hk < 0) continue;
                    ++avail;
                    if (W[hk] > 0.0f) ++sup;
                }
                if (avail > 0 ? sup < 1 : W[best] < 15.0f) ok = false;
            }
            // ベース音域(C3=48未満)は原則モノフォニック(単音、またはオクターブ重ねのみ)。
            // 既に低音域で別の音を選んでいるフレームで、オクターブ関係(±12)でない2本目の低音候補が
            // 来た場合は、キックの被りや倍音混濁によるランダム飛びなので棄却する。
            if (ok && best < BASS_END) {
                for (int j = MUSIC_LOW_FLOOR; j < BASS_END; ++j) {
                    if (!outPicked[j]) continue;
                    const int diff = (best > j) ? (best - j) : (j - best);
                    if (diff != 12) {
                        ok = false;
                        break;
                    }
                }
            }
            if (ok) {
                for (int d = -1; d <= 1; d += 2) {
                    const int a = best + d;
                    if (a >= 0 && a < COUNT && outPicked[a] && a0 < amp[a] * SEMI_SPILL) ok = false;
                }
            }
            if (!ok) {
                rejected[best] = true;
                continue;
            }

            outPicked[best] = true;
            if (nPicked == 0) firstSal = sal[best];
            ++nPicked;

            // 倍音包絡フィッティング:
            // 単一楽器の第2・第3倍音(基音の0.7〜1.0倍程度)はしっかり引き切る一方、
            // 上のオクターブや12度で「別の強いメロディ/和音」が同時に鳴っているときに
            // その1本の大振幅に引っ張られて logA が跳ね上がりメロディまで消してしまうのを防ぐ。
            float refAmp = r[best];
            {
                float hTop1 = 0.0f, hTop2 = 0.0f;
                for (int n = 2; n <= 4; ++n) {
                    const int hk = ht.k[best][n - 1];
                    if (hk < 0 || W[hk] <= 0.0f || r[hk] <= 0.0f) continue;
                    if (r[hk] > hTop1) { hTop2 = hTop1; hTop1 = r[hk]; }
                    else if (r[hk] > hTop2) { hTop2 = r[hk]; }
                }
                if (refAmp <= 0.0f) {
                    refAmp = (hTop2 > 0.0f) ? hTop2 : hTop1;
                }
                else if (hTop2 > refAmp) {
                    const float capRef = refAmp * 1.25f;
                    refAmp = (hTop2 < capRef) ? hTop2 : capRef;
                }
            }

            float la[SAL_HARM], ln[SAL_HARM];
            int cnt = 0;
            for (int n = 1; n <= SAL_HARM; ++n) {
                const int hk = ht.k[best][n - 1];
                if (hk < 0 || W[hk] <= 0.0f || r[hk] <= 0.0f) continue;
                float aUse = r[hk];
                if (n >= 2 && refAmp > 0.0f) {
                    const float capH = refAmp / powf((float)n, 0.35f);
                    if (aUse > capH) aUse = capH;
                }
                la[cnt] = logf(aUse);
                ln[cnt] = logf((float)n);
                ++cnt;
            }
            float beta = 1.05f;
            float logA = (r[best] > 0.0f) ? logf(r[best]) : -30.0f;
            if (cnt >= 2) {
                float bestErr = 1.0e30f;
                for (int g = 0; g < 11; ++g) {
                    const float bt = 0.5f + 0.18f * (float)g;
                    float v[SAL_HARM];
                    for (int i = 0; i < cnt; ++i) {
                        const float x = la[i] + bt * ln[i];
                        int j = i;
                        while (j > 0 && v[j - 1] > x) { v[j] = v[j - 1]; --j; }
                        v[j] = x;
                    }
                    const float lA = v[(int)(ENV_TAU * (float)(cnt - 1))];
                    float err = 0.0f;
                    for (int i = 0; i < cnt; ++i) {
                        const float e = la[i] - (lA - bt * ln[i]);
                        err += (e >= 0.0f) ? ENV_TAU * e : (ENV_TAU - 1.0f) * e;
                    }
                    if (err < bestErr) {
                        bestErr = err;
                        beta = bt;
                        logA = lA;
                    }
                }
            }

            float predPow[COUNT], predAmp[COUNT];
            memset(predPow, 0, sizeof(predPow));
            memset(predAmp, 0, sizeof(predAmp));
            for (int n = 2; n <= SUB_HARM; ++n) {
                const int hk = ht.k[best][n - 1];
                if (hk < 0 || hk == best) continue;
                float bUse = beta;
                if (n > 4 && bUse > 1.0f) bUse = 1.0f;
                const float pa = expf(logA - bUse * logf((float)n)) * p.envMargin;
                predPow[hk] += pa * pa;
                if (n <= 6 && pa > predAmp[hk]) predAmp[hk] = pa;
            }
            for (int k = 0; k < COUNT; ++k) {
                if (predPow[k] <= 0.0f || r[k] <= 0.0f) continue;
                const bool multi = (explainedAmp[k] > 0.0f && predAmp[k] > 0.0f);
                explained[k] += predPow[k];
                explainedAmp[k] += predAmp[k];
                // 複数の既選択音の低次倍音(2〜4次)が同じ鍵に重なる場合(例: C3の3倍音とG3の2倍音がG4で重なる)、
                // 同相干渉で振幅が足し算されるため 0.78*(Σpa)^2 まで説明する。
                float totExp = explained[k];
                if (multi) {
                    const float coh = 0.78f * explainedAmp[k] * explainedAmp[k];
                    if (coh > totExp) totExp = coh;
                }
                const float p0 = amp[k] * amp[k];
                r[k] = (totExp < p0) ? sqrtf(p0 - totExp) : 0.0f;
                explained[k] = (totExp < p0) ? totExp : p0;
            }
            explained[best] += r[best] * r[best];
            if (amp[best] * 2.0f < expf(logA)) {
                float partial = 0.0f;
                for (int n = 2; n <= 6; ++n) {
                    const int hk = ht.k[best][n - 1];
                    if (hk >= 0 && blend[hk] > partial) partial = blend[hk];
                }
                fundPow[best] = partial * 0.5f;
            }
            r[best] = 0.0f;
        }

        for (int i = 0; i < COUNT; ++i) {
            if (outPicked[i]) {
                outOwnPow[i] = blend[i];
                if (fundPow[i] > outOwnPow[i]) outOwnPow[i] = fundPow[i];
            }
            else {
                outOwnPow[i] = r[i] * r[i];
            }
        }
        return nPicked;
    }
}
