#pragma once
// 基音検出コア（局所背景による白色化 + 倍音列の予測減算）
//
// なぜ作り直したか:
//   従来は「候補の強さ」を帯域最大値やベースのピーク値と直接比べていた。
//   検出用スペクトル blend は Goertzel 振幅の【2乗】(ApplyDetectScale =
//   amp^2 * 5) であり、さらに音楽のスペクトルは低域ほど桁違いに大きい。
//   結果 C2 のベースと C5 のメロディでは blend が 100〜1000 倍違い、
//   「親の 25%」「帯域最大の 60%」といった比率判定はベースが鳴った瞬間に
//   中高音を全滅させた。逆にベースが休むと基準が下がってノイズを拾う。
//   ゴースト抑制が 4 段重ねになっていたのも、どの段も同じ「絶対量の比較」を
//   別の係数で繰り返していただけだった。
//
// 方針:
//   1) 振幅に戻す(sqrt)。2乗ドメインで比率を語らない。
//   2) 局所背景(±1オクターブの低パーセンタイル)を基準に dB で測る。
//      「その音域の地の高さから何 dB 出ているか」なので低音も高音も同じ土俵。
//   3) 基音を1つ採るたびに、その音源の倍音減衰係数を【実測から推定】し、
//      予測ぶんを残差スペクトルから減算する。
//      - ゴースト倍音は予測どおりの大きさなので消える。
//      - 同じ位置に実音があれば予測を超えて残り、次の反復で採択される。
//      → 比率によるゴースト規則を一切持たなくてよくなる。
//   4) 減衰係数は「その基音固有の奇数次倍音(h3,h5,h7)」から推定する。
//      1オクターブ上の音は親の偶数次倍音としか重ならないため、奇数次は
//      親だけのものになる。ここから偶数次を予測すれば、
//      「オクターブ上に実音が居るのか、ただの倍音か」を原理的に切り分けられる。
#include <cmath>
#include <cstring>
#include "PianoKeyTable.h"

#ifdef PRSAL_TRACE
extern int g_prsalWatch;
#endif

namespace PianoRollSalience
{
    static constexpr int COUNT = PianoKey::COUNT;
    static constexpr int SAL_HARM = 8;      // サリエンスに使う次数 h1..h8
    // 減算する次数。実楽器の倍音列は 30 次前後まで続き、鍵盤 108 音の上端は
    // 低音の高次倍音の吹き溜まりになる。ここを短く切ると、切った先が丸ごと
    // ゴーストの巣になる（実測: 上端 B7 / A#6 が常時点灯）。
    static constexpr int SUB_HARM = 32;
    // 高次は外挿なので予測を控えめに…としていたが、控えた分がそのまま
    // 引き残りとして最上部に溜まり、地の低い高音域では -13dB 級の立派な
    // ピークに見えてゴーストになった（実測: ベース単独で B6/A#6 が点灯）。
    // パワードメイン減算では、そこに実音があれば sqrt(x^2 - pred^2) で
    // 生き残る。予測を削るより、丸ごと引いて実音の取り分だけ残すほうが安全。
    inline float SubConfidence(int)
    {
        return 1.0f;
    }
    static constexpr int BG_RADIUS = 10;    // 局所背景の窓（±10半音 ≒ ±1オクターブ）
    static constexpr float BG_PCT = 0.35f;  // 背景に使う順位（35%点）
    static constexpr float DB_CAP = 60.0f;  // 1部分音がサリエンスを独占しない上限
    // 反復中に局所背景を下げてよい下限（減算前の背景に対する比）。
    // 0 にすると説明済み領域の地がノイズ床まで落ちて引き残りが誇張される。
    static constexpr float BG_KEEP = 0.30f;
    // 予測減算のマージン。1.0 より大きくすると引きすぎ、つまり後続の音の
    // 倍音まで削ってしまい、その音の振幅推定(EstimatePartialBase)が過小になって
    // 今度は引き足りずゴーストが残る、という連鎖を起こす。推定した寄与を
    // そのまま引き、ゴーストの取りこぼしは unexplainedMin / explainedPenalty
    // 側で受け止める。
    static constexpr float SUB_SLACK = 1.00f;

    // h1..h16 の鍵インデックス表。HarmonicUpKeyAny は NearestKeyIndex
    // (O(108) の線形探索 + powf) を毎回回すため、貪欲ループの内側で呼ぶと重い。
    struct HarmTable
    {
        short k[COUNT][SUB_HARM];
        HarmTable()
        {
            // PianoKey::NearestKeyIndex は範囲外の周波数を最上鍵へ丸めて返す。
            // そのまま使うと、上端の鍵では h2 以降が全部自分自身になり
            // 「自分の倍音が自分を支持する」自己参照が成立してしまう。
            // 実測ではこれで最上鍵 B7 が常時点灯していた。鍵盤上端を超える
            // 倍音は存在しないものとして扱う。
            const float topHz = PianoKey::KeyHz(COUNT - 1) * 1.03f;
            for (int i = 0; i < COUNT; ++i) {
                k[i][0] = (short)i;
                const float f0 = PianoKey::KeyHz(i);
                for (int n = 2; n <= SUB_HARM; ++n) {
                    if (f0 * (float)n > topHz) { k[i][n - 1] = -1; continue; }
                    const int h = PianoKey::HarmonicUpKeyAny(i, n);
                    k[i][n - 1] = (short)((h > i && h < COUNT) ? h : -1);
                }
            }
        }
    };

    inline const HarmTable& Harm()
    {
        static const HarmTable t;
        return t;
    }

    // 部分音 n の重み。実楽器の倍音は概ね 1/n 前後で減衰するが、重みを
    // 落としすぎると高次しか出ない音(矩形波等)を取りこぼすので n^-0.75。
    inline float HarmWeight(int n)
    {
        static const float w[SAL_HARM] = {
            1.000f, 0.595f, 0.439f, 0.354f, 0.299f, 0.261f, 0.233f, 0.210f
        };
        return w[n - 1];
    }

    // 局所背景: 各鍵の周囲 ±BG_RADIUS 半音の振幅を並べた低パーセンタイル値。
    // 中央値ではなく 35% 点なのは、和音で窓内の半分が音で埋まっても
    // 「地の高さ」側を拾い続けるため。
    inline void BuildLocalBackground(const float* amp, float* bg, int count, float floorAmp)
    {
        float buf[BG_RADIUS * 2 + 1];
        for (int i = 0; i < count; ++i) {
            int lo = i - BG_RADIUS; if (lo < 0) lo = 0;
            int hi = i + BG_RADIUS; if (hi > count - 1) hi = count - 1;
            int n = 0;
            for (int j = lo; j <= hi; ++j) {
                const float v = amp[j];
                int p = n++;
                while (p > 0 && buf[p - 1] > v) { buf[p] = buf[p - 1]; --p; }
                buf[p] = v;
            }
            int idx = (int)(BG_PCT * (float)(n - 1) + 0.5f);
            if (idx < 0) idx = 0;
            if (idx >= n) idx = n - 1;
            float b = buf[idx];
            if (b < floorAmp) b = floorAmp;
            bg[i] = b;
        }
    }

    // 局所背景から何 dB 出ているか。背景以下は 0。
    inline void BuildDb(const float* r, const float* bg, float* db, int count)
    {
        for (int i = 0; i < count; ++i) {
            const float b = bg[i];
            if (r[i] <= b) { db[i] = 0.0f; continue; }
            float v = 20.0f * log10f(r[i] / b);
            if (v > DB_CAP) v = DB_CAP;
            db[i] = v;
        }
    }

    // サリエンス[dB] = 基音自身の突出 6 割 + 上倍音列の加重平均 4 割。
    //
    // 全次数の単純加重平均にすると、上倍音は必ず基音より弱いぶん平均が下がり、
    // 「基音は十分出ているのにスコアが閾値に届かない」実音を大量に取りこぼす
    // （A4 が db=12.2 なのに平均 7.5 で落選する等）。基音の突出を主、倍音列を
    // 従にすることで、実音は通しつつ「倍音列を伴わない単発ピーク＝ノイズ」は
    // 従の項が効いて落ちる。
    // 範囲外へ出た部分音は分母からも外す（高音が「倍音を数えられないぶん低
    // スコア」になって取りこぼされるのを防ぐ）。
    inline float Salience(const float* db, int key, int count)
    {
        const short* hk = Harm().k[key];
        float acc = 0.0f, wsum = 0.0f;
        for (int n = 2; n <= SAL_HARM; ++n) {
            const int k = hk[n - 1];
            if (k < 0 || k >= count) continue;
            const float w = HarmWeight(n);
            acc += w * db[k];
            wsum += w;
        }
        const float upper = (wsum > 1e-6f) ? (acc / wsum) : 0.0f;
        
        // 基音の dB をそのままベースにし、倍音があればボーナスを加算する。
        // 以前は加重平均だったため、倍音が他音の減算で消えたり元々無い
        // （サイン波に近い）音色だと、基音が十分強くてもスコアが下がって
        // 落選していた。加算方式なら、単独のピークでも dB が閾値を超えれば通る。
        // 倍音ボーナス(0.30)は、基音より第2倍音が強い音色で倍音側が先に
        // 誤採択されるのを防ぐ（親にボーナスが乗って親が勝つ）ためのもの。
        return db[key] + upper * 0.30f;
    }


    // 倍音列モデル: n 次部分音の振幅を base / n^e とみなす。
    // base(n=1 換算の水準)と減衰指数 e の両方を実測から推定する。
    struct HarmModel { float base; float e; };

    // 推定に使う参照点は基音だけが持つ奇数次(h3,h5,h7,h9)。
    //  - 奇数次を使う理由: 1オクターブ上の音は親の偶数次としか重ならない。
    //    奇数次を見れば「オクターブ上に実音が居るのか、ただの倍音か」を
    //    原理的に切り分けられる。
    //  - base を【最小値】で採る理由: 別の実音が倍音位置に重なると観測値は
    //    必ず増える方向にしか汚れない。つまり一番小さい推定値がもっとも
    //    汚染されていない。平均・中央値は密なアンサンブルで base を過大評価し、
    //    引き算がその実音自身を消してしまう（実測: A#1 のベース上で F3/D4 が
    //    完全消滅）。
    //  - e を固定 1.0 にしない理由: 実際の減衰は音色ごとに違う。1/n 決め打ちで
    //    高次まで外挿すると、実際にはそこまで倍音を持たない音源で高次を引き
    //    すぎ、そこにあった別の音の参照点を削ってしまう。すると今度はその音の
    //    base が過小になり、引き足りずに自分の h2 がゴースト化する
    //    （実測: C3+C4 のオクターブ和音で C5 が残った）。
    //
    // 基音スロット r[key] そのものではなく倍音列から求めるのは、親音に削られて
    // 基音スロットが目減りしている場合でも本来の音量を復元するため。
    inline HarmModel EstimateHarmModel(const float* r, const float* amp_orig, int key, float amp0, int count, const bool* explained)
    {
        HarmModel m;
        m.base = amp0;
        
        // Estimate e from odd harmonics using the ORIGINAL spectrum to avoid
        // corruption from previous subtractions' skirts.
        float max_e = 0.0f;
        for (int h = 3; h <= 9; h += 2) {
            int hk = Harm().k[key][h - 1];
            if (hk < 0 || hk >= count) break;
            if (explained && explained[hk]) continue;
            
            // Use original amplitude for harmonics to avoid corruption from previous subtractions.
            // But use current residual amp0 for the fundamental, because the original
            // fundamental might have been inflated by another note's harmonics (which
            // have now been subtracted).
            float v = amp_orig[hk];
            float ref_amp = amp0;
            
            if (v > 1e-4f) {
                float e_est = logf(ref_amp / v) / logf((float)h);
                if (e_est > max_e) max_e = e_est;
            }
        }
        
        if (max_e > 0.0f) {
            m.e = max_e;
            if (m.e < 0.90f) m.e = 0.90f;
            if (m.e > 1.30f) m.e = 1.30f;
        } else {
            m.e = 1.15f;
        }

        return m;
    }

    // 解析窓の漏れ幅を実測する。Goertzel は半音ごとの点推定なので、実際の
    // 部分音は 1 本の鍵に収まらず両隣にも漏れる。漏れ量は窓長と音域で変わる
    // ため固定値では合わない。確定した基音の両隣が基音に対して持っている比を
    // そのまま「その音源・その音域での漏れ率」として使う。
    // 両隣の小さいほうを採るのは、片側に別の実音が乗っていても引きずられない
    // ようにするため（重なりは必ず増やす方向にしか効かない）。
    // ※注意: 実測には残差 r ではなく元の amp_orig を使うこと。
    inline float MeasureSkirt(const float* amp_orig, int key, int count)
    {
        const float amp0 = amp_orig[key];
        if (amp0 <= 1e-9f) return 0.0f;
        const float lo = (key - 1 >= 0) ? amp_orig[key - 1] : 0.0f;
        const float hi = (key + 1 < count) ? amp_orig[key + 1] : 0.0f;
        float s = (lo < hi ? lo : hi) / amp0;
        if (s < 0.0f) s = 0.0f;
        if (s > 0.85f) s = 0.85f;
        return s;
    }

    // 部分音 1 本ぶんを、漏れを含めてパワードメインで引く。
    // 中心だけ引いて隣を残すと山の肩がそのまま残り、次の反復でそこが
    // 「新しい局所ピーク」に見えてゴーストになる。漏れは中心から離れるほど
    // 幾何級数的に減るものとして skirt^|d| を掛ける。
    inline void SubtractPartial(float* r, const float* amp_orig, int key, float pred, float skirt, int count, int parent)
    {
        if (key < 0 || key >= count || pred <= 0.0f) return;
        
        // 漏れの上限は、その場所に実際に存在する振幅（amp_orig）で頭打ちにする。
        // これにより、片側に実音が重なっていても過大評価せず、
        // かつチューニングのズレで片側の漏れが大きい場合でも正しく引ける。
        const float lo = (key - 1 >= 0) ? amp_orig[key - 1] : 0.0f;
        const float hi = (key + 1 < count) ? amp_orig[key + 1] : 0.0f;

        float s1 = pred * skirt;
        float s2 = s1 * skirt;

#ifdef PRSAL_TRACE
        if (key == ::g_prsalWatch) {
            printf("      Sub parent=%d key=%d pred=%.4f r_before=%.4f ", parent, key, pred, r[key]);
        }
#endif

        for (int d = -2; d <= 2; ++d) {
            const int k = key + d;
            if (k < 0 || k >= count) continue;
            
            float q = (d == 0) ? pred : ((d == -1 || d == 1) ? s1 : s2);
            
            // 実際の漏れの上限でキャップする（d=0 の中心は除く）
            // ※注意: 実音が隣接している場合、lo/hi は非常に大きくなるため、
            // ここでのキャップは「漏れが極端に少ない場合」の保護としてのみ働く。
            if (d == -1 && q > lo) q = lo;
            if (d == 1 && q > hi) q = hi;
            
            // 過剰減算を防ぐため、現在の残差 r を上限とする。
            // amp_orig を上限にすると、他の音の引き算ですでに減っている場合に
            // r[k]^2 - q^2 が負になり、過剰減算（ゼロクラッシュ）を引き起こす。
            if (q > r[k]) q = r[k];
            
            const float rest = r[k] * r[k] - q * q;
            r[k] = (rest > 0.0f) ? sqrtf(rest) : 0.0f;
        }

#ifdef PRSAL_TRACE
        if (key == ::g_prsalWatch) {
            printf("r_after=%.4f\n", r[key]);
        }
#endif
    }

    struct PickParams
    {
        int   pickLo;           // 採択範囲（下端）
        int   pickHi;           // 採択範囲（上端, exclusive）
        int   bassEnd;          // 低音帯の終端（隣接単一化の対象）
        int   maxNotes;         // 反復回数の上限（安全弁）
        float salThresh[COUNT]; // 鍵ごとのサリエンスしきい値[dB]
        float fundMinDb;        // 基音自身に要求する最低突出[dB]
        float explainedPenalty; // 既採択音の倍音位置に課す追加しきい値[dB]
        float unexplainedMin[COUNT]; // 既採択音の倍音位置で、残っていることを要求する振幅比
        int   verifyHi;         // これ以上の鍵は倍音列が範囲外で裏取りできない
    };

    // 貪欲反復: 最強の基音を採る → その倍音列の予測ぶんを引く → 繰り返す。
    //
    // 局所背景は【毎回、残差から作り直す】。
    // 背景を最初のスペクトルに固定すると、ベースの倍音で埋まった中高域では
    // 背景そのものが持ち上がっており、ベースを引いた後も「地が高いまま」に
    // なって残った実音が背景以下に見える。実測では F#5 が引き算後に
    // -0.6dB となり消えていた。ベースを説明し終えたら地も下がるのが正しい。
    inline int PickIterative(const float* amp, const float* absFloor,
        bool* outPicked, int count, const PickParams& p)
    {
        memset(outPicked, 0, (size_t)count * sizeof(bool));

        // 恒久的に候補から外す鍵（低域の隣接単一化で潰したもの）。
        // 局所ピーク判定そのものは毎回【残差から】やり直す。元スペクトルで
        // 一度だけ判定すると、隣の鍵に落ちたベース倍音に覆われた実音が
        // 「ピークでない」と確定してしまい、ベースを引いた後も永久に候補に
        // 戻れない（実測: ベース h9/h10 に挟まれた D#5 が消えていた）。
        bool blocked[COUNT];
        memset(blocked, 0, (size_t)count * sizeof(bool));

        float r[COUNT], db[COUNT], bg[COUNT], bg0[COUNT];
        memcpy(r, amp, (size_t)count * sizeof(float));

        // 減算前の局所背景。以降の背景はこれを下限として持つ（BG_KEEP 参照）。
        BuildLocalBackground(amp, bg0, count, 1e-7f);
        for (int i = 0; i < count; ++i) {
            const float lim = absFloor[i] * 0.5f;
            if (bg0[i] < lim) bg0[i] = lim;
        }

        // 既採択音の倍音位置。予測減算しても残差はゼロにならないので、
        // 「すでに説明済みの場所」には追加のしきい値を課す。ハード除去では
        // ないため、残差が十分大きい＝実音が重なっている場合は通る。
        bool explained[COUNT];
        memset(explained, 0, (size_t)count * sizeof(bool));

        int picked = 0;
        for (int round = 0; round < p.maxNotes; ++round) {
            BuildLocalBackground(r, bg, count, 1e-7f);
            for (int i = 0; i < count; ++i) {
                // 無音〜極小信号で dB が発散しないよう、背景の下限は絶対床に合わせる。
                const float lim = absFloor[i] * 0.5f;
                if (bg[i] < lim) bg[i] = lim;
                // さらに減算前の背景の一定割合を下回らせない。引いた後の背景を
                // そのまま使うと、倍音を説明し終えた領域の地がノイズ床まで落ち、
                // わずかな引き残りが 30dB 級に見えてゴーストが大量発生する。
                // BG_KEEP を 0.35f から 0.05f に下げて、より小さな音も拾えるようにする。
                const float keep = bg0[i] * 0.05f;
                if (bg[i] < keep) bg[i] = keep;
            }
            BuildDb(r, bg, db, count);

            bool cand[COUNT];
            for (int i = 0; i < count; ++i) {
                cand[i] = false;
                if (blocked[i] || i < p.pickLo || i >= p.pickHi) continue;
                if (r[i] < absFloor[i]) continue;
                // 基音は残差スペクトル上の真の局所ピークであること。倍音の裾や、
                // DC 方向へ単調増加する低域漏れの途中は基音になれない。
                if (i > 0 && r[i - 1] >= r[i]) continue;
                if (i + 1 < count && r[i + 1] > r[i]) continue;
                cand[i] = true;
            }

            float sal[COUNT];
            int best = -1;
            float bestSal = 0.0f;
            for (int i = p.pickLo; i < p.pickHi; ++i) {
                sal[i] = 0.0f;
                if (!cand[i] || outPicked[i]) continue;
                if (db[i] < p.fundMinDb) continue;
                // 既採択音の倍音位置では「元の振幅のうち説明しきれず残った割合」
                // も見る。残差が十分に大きい＝そこに別の実音が重なっている、と
                // 言えるだけの取り分が無ければ採らない。
                // 説明済みスロットでは局所背景も一緒に下がるため、dB だけでは
                // わずかな引き残りが目立って見えてしまう。
                if (explained[i]) {
                    // 倍音列が鍵盤範囲外に出る最上部は、倍音和で「実音である」
                    // 裏を取れない一方、下の音の高次倍音の吹き溜まりになる。
                    // 説明済みスロットなら基音として採らない。
                    if (i >= p.verifyHi) continue;
                    if (r[i] < amp[i] * p.unexplainedMin[i]) continue;
                }
                const float s = Salience(db, i, count);
                sal[i] = s;
                const float th = p.salThresh[i] +
                    (explained[i] ? p.explainedPenalty : 0.0f);
                if (s < th) continue;
                if (s > bestSal) { bestSal = s; best = i; }
            }
#ifdef PRSAL_TRACE
            {
                const int w = ::g_prsalWatch;
                if (w >= 0 && w < count)
                    printf("   [rd%d] best=%d sal=%.1f | watch%d r=%.4f bg=%.4f db=%.1f "
                        "sal=%.1f cand=%d exp=%d unexp=%.2f\n",
                        round, best, bestSal, w, r[w], bg[w], db[w], Salience(db, w, count),
                        cand[w] ? 1 : 0, explained[w] ? 1 : 0,
                        (amp[w] > 0.0f) ? r[w] / amp[w] : 0.0f);
            }
#endif
            if (best < 0) break;

            // 基音優先。最高スコアが下位候補の整数倍位置にあり、その下位候補も
            // 十分なスコアを持つなら、先に下位（＝基音）を採る。
            // 局所背景は音域ごとに違うので、倍音のほうが「空いた場所に出ている」
            // ぶんスコアが高くなることがある。そのまま採ると倍音が先に確定し、
            // 基音側は自分の倍音を消されてスコアを失い、二度と採られない。
            for (bool moved = true; moved; ) {
                moved = false;
                for (int n = 2; n <= 6; ++n) {
                    const int lo = PianoKey::HarmonicDownKeyAny(best, n);
                    if (lo < p.pickLo || lo >= best) continue;
                    if (!cand[lo] || outPicked[lo]) continue;
                    if (db[lo] < p.fundMinDb) continue;
                    if (explained[lo] &&
                        (lo >= p.verifyHi || r[lo] < amp[lo] * p.unexplainedMin[lo])) continue;
                    if (sal[lo] < p.salThresh[lo] +
                        (explained[lo] ? p.explainedPenalty : 0.0f)) continue;
                    if (sal[lo] < bestSal * 0.40f) continue;
#ifdef PRSAL_TRACE
                    if (::g_prsalWatch >= 0)
                        printf("   [rd%d] promote %d(sal=%.1f) -> %d(sal=%.1f)\n", round, best, bestSal, lo, sal[lo]);
#endif
                    best = lo;
                    bestSal = sal[lo];
                    moved = true;   // さらに下の親が居ないか見る
                    break;
                }
            }

            outPicked[best] = true;
            ++picked;

            // 減算はパワードメインで行う。無相関な部分音が同じ鍵に重なったとき、
            // 観測振幅は sqrt(a^2+b^2) であって a+b ではない。振幅のまま引くと
            // 引きすぎになり、しかも残差から次の基音の振幅 a0 を取るため、
            // 後続の音の倍音減算まで過小になって倍音が総ゴースト化する
            // （C3+C4 のオクターブ和音で C5/G5/C6… が総点灯した）。
            const float a0 = r[best];
            const HarmModel hm = EstimateHarmModel(r, amp, best, a0, count, explained);
            const float skirt = MeasureSkirt(amp, best, count);
            const short* hk = Harm().k[best];
            for (int n = 2; n <= SUB_HARM; ++n) {
                const int k = hk[n - 1];
                if (k < 0 || k >= count) continue;
                const float pred =
                    hm.base / powf((float)n, hm.e) * SUB_SLACK * SubConfidence(n);
                SubtractPartial(r, amp, k, pred, skirt, count, best);
                explained[k] = true;
            }
            // 基音自身の裾も引く。これをやらないと、強い音の両隣が常に
            // 「説明されていない山」として残る。
            SubtractPartial(r, amp, best, a0, skirt, count, best);
            r[best] = 0.0f;

            // 半音分解能の足りない低域では、採った鍵の両隣は同じ音の裾。
            // 残差は SubtractPartial が均して落としているので、ここでは
            // 「基音として二度と採らない」印だけ付ける。
            if (best < p.bassEnd) {
                if (best - 1 >= 0) blocked[best - 1] = true;
                if (best + 1 < count) blocked[best + 1] = true;
            }
        }
        return picked;
    }
}
