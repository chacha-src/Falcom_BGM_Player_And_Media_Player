// \tgEFAMIDIVZTCUB
// Copyright(c)2003-2004 yuno
#ifndef midisynth_hpp
#define midisynth_hpp

#include <map>
#include <memory>
#include <vector>
#include <stdint.h>
#include <stddef.h>

    typedef double sample_t;//added by Kobarin
    class channel;

    // VXe[hñ^B
    enum system_mode_t{ system_mode_default, system_mode_gm, system_mode_gm2, system_mode_gs, system_mode_xg };

    // Rs[sÂÌî{NXB
/*  í by Kobarin
    class uncopyable{
    public:
        uncopyable(){}
    private:
        uncopyable(const uncopyable&){}
        void operator=(const uncopyable&){}
    };
*/
    // m[gB­¹Ì¹B
    struct tone_color;

    // m[gB­¹Ì¹B
    class note{//:uncopyable{//C³ by Kobarin
    public:
        note(int assign_, int panpot_):assign(assign_), panpot(panpot_){}
        virtual ~note(){}
        int get_assign()const{ return assign; }
        int get_panpot()const{ return panpot; }
        virtual bool synthesize(sample_t* buf, std::size_t samples, double rate, sample_t left, sample_t right) = 0;
        virtual void note_off(int velocity) = 0;
        virtual void sound_off() = 0;
        virtual void set_frequency_multiplier(double value) = 0;
        virtual void set_tremolo(int depth, double freq) = 0;
        virtual void set_vibrato(double depth, double freq) = 0;
        virtual void set_damper(int value) = 0;
        virtual void set_sostenute(int value) = 0;
        virtual void set_freeze(int value) = 0;
        virtual void apply_tone(const tone_color&) {}
    private:
        int assign;
        int panpot;
    };

    // m[gt@NgB
    // m[gIbZ[WÉÎµÄKØÈm[gðìèo·B
    struct tone_color{
        int revSend, choSend, dlySend;
        int revMode, choMode, dlyMode, insMode, ins2;
        int cutoff, reso, hpf, attack, decay, release;
        int vibRate, vibDepth, vibDelay;
        int eqLo, eqHi;
        int insFam;
        int insPacked;
        int insDrive;
        int insLo, insHi;
        int mapLsb, varMsb, pc, sysMode, bankExact;
        /* SysEx の生値。64 付近が「そのタイプの標準」。spatXg=1 のときタイプは MSB/LSB。 */
        int revTime, revChar, revLpf, revFb, revPre;
        int choRate, choDepth, choFb, choDly, choLpf, choToRev, choToDly;
        int dlyTime, dlyFb, dlyLpf, dlyToRev, dlyTrim;
        int revMsb, revLsb, choMsb, choLsb, dlyMsb, dlyLsb, spatXg;
        int spatPass; /* 0=通常 1=ドライ（尾は別チャンネル） */
        tone_color():
            revSend(0), choSend(0), dlySend(0),
            revMode(0), choMode(0), dlyMode(0), insMode(0), ins2(0),
            cutoff(64), reso(64), hpf(64), attack(64), decay(64), release(64),
            vibRate(64), vibDepth(64), vibDelay(64),
            eqLo(64), eqHi(64),
            insFam(0), insPacked(0), insDrive(-1), insLo(64), insHi(64),
            mapLsb(0), varMsb(0), pc(0), sysMode(0), bankExact(0),
            revTime(64), revChar(4), revLpf(0), revFb(0), revPre(0),
            choRate(3), choDepth(19), choFb(8), choDly(80), choLpf(0), choToRev(0), choToDly(0),
            dlyTime(0x61), dlyFb(80), dlyLpf(0), dlyToRev(0), dlyTrim(127),
            revMsb(0), revLsb(0), choMsb(0), choLsb(0), dlyMsb(0), dlyLsb(0), spatXg(0),
            spatPass(0) {}
    };

    /* GS/XG: 64 = no change. Time (atk/dec/rel) は大きいほど遅く、YM の AR/DR/RR は逆。 */
    struct tone_paint {
        int arD, drD, rrD, fbDelta, carTl, modTl, amsOn;
        int dtD, mulD, slD;
    };
    inline int tone_clmpi(int v, int lo, int hi)
    {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }
    /* 1 EQ 2 OD 3 Dist 4 Amp 5 Chorus 6 Trem 7 Phaser 8 Rotary 9 Flanger
       10 Delay 11 Reverb 12 Pitch 13 Comp 16 Enhancer 14 Wah 15 LoFi 19 Reverse.
       Delay/reverb は 1 スロットぶん。既にシステム側で尾が付いていれば足さない。 */
    inline void paint_ins_kind(tone_paint& p, int kind, int drv)
    {
        if (drv < 0) drv = 64;
        if (drv > 127) drv = 127;
        const int k = (drv >= 96) ? 2 : 1;
        if (kind == 2) {
            p.fbDelta += 1;
            p.carTl -= k;
            p.modTl += 1;
            p.slD -= 1;
        } else if (kind == 3) {
            p.fbDelta += 1;
            p.carTl -= k + 1;
            p.modTl += k;
            if (drv >= 80) p.slD -= 1;
            if (drv >= 96) p.arD += 1;
        } else if (kind == 4) {
            p.fbDelta += 1 + (drv >= 100 ? 1 : 0);
            p.carTl -= k + 1;
            p.modTl += k;
            p.slD -= 1;
        } else if (kind == 5) {
            p.amsOn = 1;
            p.dtD += 1;
        } else if (kind == 6) {
            p.amsOn = 1;
        } else if (kind == 7 || kind == 9) {
            p.amsOn = 1;
            p.fbDelta += 1;
            p.dtD += 1;
        } else if (kind == 8) {
            p.amsOn = 1;
            p.dtD += 1;
        } else if (kind == 10) {
            if (p.rrD >= 0) p.rrD -= 1;
            else if (p.rrD > -2 && drv >= 110) p.rrD -= 1;
        } else if (kind == 11) {
            if (p.rrD >= 0) {
                p.rrD -= 1;
                if (p.drD > -1) p.drD -= 1;
            } else if (p.rrD > -2 && drv >= 110) {
                p.rrD -= 1;
            }
        } else if (kind == 12) {
            p.dtD += 1;
            if (drv >= 80) p.mulD += 1;
        } else if (kind == 13) {
            p.carTl += 1;
            p.arD += 1;
        } else if (kind == 14) {
            p.modTl += (drv >= 96) ? 2 : 1;
            p.fbDelta += 1;
        } else if (kind == 15) {
            p.carTl += 1;
            p.fbDelta += 1;
            if (p.arD > -2) p.arD -= 1;
        } else if (kind == 16) {
            p.modTl += 1;
            p.fbDelta += 1;
        } else if (kind == 19) {
            if (p.arD > -2) p.arD -= 1;
        }
    }
    inline int gs88_ins_kind(int msb)
    {
        switch (msb & 0x7F) {
        case 0x01: case 0x05: return 1;
        case 0x02: return 2;
        case 0x03: return 3;
        case 0x04: return 7;
        case 0x06: return 16;
        case 0x07: return 14;
        case 0x08: return 8;
        case 0x09: case 0x0A: return 13;
        case 0x0B: case 0x0C: case 0x0D: case 0x0E: return 5;
        case 0x0F: case 0x10: return 9;
        case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: return 10;
        case 0x16: case 0x18: case 0x1B: return 11;
        case 0x19: case 0x1A: return 12;
        case 0x1C: return 10;
        case 0x1D: return 4;
        default: return 0;
        }
    }
    inline int gspro_ins_kind(int msb, int lsb)
    {
        if (msb == 0x01) {
            if (lsb <= 0x01) return 1;
            if (lsb == 0x02 || lsb == 0x03) return 16;
            if (lsb == 0x10) return 2;
            if (lsb == 0x11) return 3;
            if (lsb == 0x20) return 7;
            if (lsb == 0x21) return 14;
            if (lsb == 0x22) return 8;
            if (lsb == 0x23 || lsb == 0x24) return 9;
            if (lsb == 0x25 || lsb == 0x26) return 6;
            if (lsb == 0x30 || lsb == 0x31) return 13;
            if (lsb >= 0x40 && lsb <= 0x44) return 5;
            if (lsb == 0x55) return 11;
            if (lsb == 0x56) return 0;
            if (lsb >= 0x50 && lsb <= 0x57) return 10;
            if (lsb == 0x60 || lsb == 0x61) return 12;
            return 0;
        }
        if (msb == 0x03) return 8;
        if (msb == 0x04) return 4;
        if (msb == 0x05) return 5;
        if (msb == 0x11) {
            if (lsb == 0x00) return 5;
            if (lsb == 0x01 || lsb == 0x02) return 9;
            if (lsb == 0x04) return 8;
            if (lsb == 0x06 || lsb == 0x08) return 14;
            if (lsb == 0x07) return 7;
            return 2;
        }
        return 0;
    }
    inline int xg_ins_kind(int msb, int lsb)
    {
        if (msb == 0) return 0;
        if ((msb >= 0x01 && msb <= 0x04) || (msb >= 0x10 && msb <= 0x13) || msb == 0x58) return 11;
        if ((msb >= 0x05 && msb <= 0x09) || msb == 0x14) return 10;
        if (msb == 0x0A) return 0;
        if (msb == 0x0B) return 19;
        if (msb == 0x41 || msb == 0x42 || msb == 0x44 || msb == 0x57) return 5;
        if (msb == 0x43) return 9;
        if (msb == 0x48) return 7;
        if (msb == 0x45 || msb == 0x56) return 8;
        if (msb == 0x46 || msb == 0x47) return 6;
        if (msb == 0x49) return 3;
        if (msb == 0x4A) return 2;
        if (msb == 0x4B) return 4;
        if (msb == 0x4C || msb == 0x4D) return 1;
        if (msb == 0x4E || msb == 0x52 || msb == 0x5D) return 14;
        if (msb == 0x50) return 12;
        if (msb == 0x51) return 16;
        if (msb == 0x53 || msb == 0x54) return 13;
        if (msb == 0x5E) return 15;
        if (msb == 0x5F) return (lsb == 1) ? 2 : 3;
        if (msb == 0x60) return 3;
        if (msb == 0x61) return 14;
        return 0;
    }
    inline void apply_insertion_paint(tone_paint& p, const tone_color& c)
    {
        if (c.insFam <= 0) return;
        const int packed = c.insPacked ? c.insPacked : (c.insMode << 8);
        if (packed == 0) return;
        const int msb = (packed >> 8) & 0x7F;
        const int lsb = packed & 0x7F;
        if (c.insLo != 64) p.carTl -= (c.insLo - 64) * 3 / 63;
        if (c.insHi != 64) p.modTl -= (c.insHi - 64) * 2 / 63;
        int kind = 0;
        int kind2 = 0;
        if (c.insFam == 1) {
            if (msb == 0x1C) { kind = 5; kind2 = 10; }
            else kind = gs88_ins_kind(msb);
        } else if (c.insFam == 2) {
            if (msb == 0x02) {
                switch (lsb) {
                case 0: kind = 2; kind2 = 5; break;
                case 1: kind = 2; kind2 = 9; break;
                case 2: kind = 2; kind2 = 10; break;
                case 3: kind = 3; kind2 = 5; break;
                case 4: kind = 3; kind2 = 9; break;
                case 5: kind = 3; kind2 = 10; break;
                case 6: kind = 16; kind2 = 5; break;
                case 7: kind = 16; kind2 = 9; break;
                case 8: kind = 16; kind2 = 10; break;
                case 9: kind = 5; kind2 = 10; break;
                case 10: kind = 9; kind2 = 10; break;
                case 11: kind = 5; kind2 = 9; break;
                default: kind = 2; break;
                }
            } else {
                kind = gspro_ins_kind(msb, lsb);
            }
        } else if (c.insFam == 3) {
            kind = xg_ins_kind(msb, lsb);
            if (msb == 0x5F || msb == 0x60 || msb == 0x61) kind2 = 10;
        }
        int drv = c.insDrive;
        if (drv < 0) drv = (kind == 2 || kind == 3 || kind == 4) ? 72 : 64;
        paint_ins_kind(p, kind, drv);
        if (kind2) paint_ins_kind(p, kind2, drv > 40 ? drv - 24 : drv);
    }
    inline void apply_bank_fallback_paint(tone_paint& p, const tone_color& c)
    {
        if (c.bankExact) return;
        const int lsb = c.mapLsb & 0x7F;
        const int msb = c.varMsb & 0x7F;
        const int gs = (c.sysMode == system_mode_gs)
            || (c.sysMode == system_mode_default && lsb <= 4 && msb < 64);
        if (gs) {
            if (lsb == 1) { p.carTl += 2; p.modTl -= 1; p.fbDelta -= 1; p.arD -= 1; }
            else if (lsb == 2) { p.carTl -= 2; p.modTl += 1; p.fbDelta += 1; }
            else if (lsb == 3) { p.carTl -= 1; p.modTl += 2; p.fbDelta += 1; p.amsOn = 1; p.dtD += 1; }
            else if (lsb == 4) { p.carTl -= 2; p.modTl += 1; p.dtD += 1; p.fbDelta += 1; }
            if (msb > 0 && msb < 64) {
                const int g = (msb / 8) & 7;
                static const int car[8] = { 0, -2, -1, 2, -3, 1, 0, -2 };
                static const int mod[8] = { 0, 2, 1, -1, 2, 0, 3, 1 };
                static const int fb[8] = { 0, 1, 0, -1, 1, 2, 0, 1 };
                p.carTl += car[g]; p.modTl += mod[g]; p.fbDelta += fb[g];
                if (g == 4 || g == 6) p.dtD += 1;
            }
        }
        if (c.sysMode == system_mode_xg || c.sysMode == system_mode_gm2) {
            if (lsb != 0) {
                const int v = (lsb + (c.pc & 7)) & 7;
                static const int car[8] = { -2, 2, -1, 1, -3, 0, 2, -1 };
                static const int mod[8] = { 1, -1, 2, 0, 2, 1, -2, 3 };
                static const int fb[8] = { 1, -1, 1, 0, 2, -1, 1, 0 };
                p.carTl += car[v]; p.modTl += mod[v]; p.fbDelta += fb[v];
                if (v & 1) p.dtD += 1;
                if (v == 5) p.arD += 1;
                else if (v == 6) p.arD -= 1;
            }
            if (msb == 64) { p.fbDelta += 1; p.modTl += 2; p.arD += 1; }
        }
    }
    inline int spat_send(int v)
    {
        if (v < 0) return 0;
        if (v > 127) return 127;
        return v;
    }
    /* SC-88 delay time 01h–73h。0x61 が約 340ms。短いほどエコー感を出さない。 */
    inline int dly_time_mul(int t)
    {
        if (t <= 0) t = 0x61;
        if (t < 0x37) return 28;
        if (t < 0x50) return 58;
        if (t < 0x5A) return 82;
        if (t < 0x69) return 100;
        return 112;
    }
    /* リバーブ枠がディレイ型ならホールは足さない。ディレイ枠と同時でも尾は最大 2 段。 */
    inline double gs_delay_ms(int t)
    {
        if (t < 1) t = 1;
        if (t > 0x73) t = 0x73;
        struct Seg { int a, b; double ms0, ms1; };
        static const Seg seg[9] = {
            { 0x01, 0x14, 0.1, 2.0 },
            { 0x14, 0x23, 2.0, 5.0 },
            { 0x23, 0x2D, 5.0, 10.0 },
            { 0x2D, 0x37, 10.0, 20.0 },
            { 0x37, 0x46, 20.0, 50.0 },
            { 0x46, 0x50, 50.0, 100.0 },
            { 0x50, 0x5A, 100.0, 200.0 },
            { 0x5A, 0x69, 200.0, 500.0 },
            { 0x69, 0x73, 500.0, 1000.0 }
        };
        for (int i = 0; i < 9; i++) {
            if (t > seg[i].b && i != 8) continue;
            int span = seg[i].b - seg[i].a;
            double u = (span > 0) ? (double)(t - seg[i].a) / (double)span : 1.0;
            if (u < 0) u = 0;
            if (u > 1) u = 1;
            return seg[i].ms0 + (seg[i].ms1 - seg[i].ms0) * u;
        }
        return 340.0;
    }
    /* mode 1=ディレイ（遅れたゲート） 2=リバーブ（短いプリディレイの別音） */
    struct echo_plan { int mode; int ms; float gain; };
    inline echo_plan plan_echo(const tone_color& c)
    {
        echo_plan e;
        e.mode = 0;
        e.ms = 0;
        e.gain = 0;
        int revDelay = 0;
        int hall = 0;
        if (!c.spatXg) {
            int m = c.revMode & 7;
            if (m >= 6) revDelay = 1;
            else if (m >= 3) hall = 1;
        } else {
            int m = c.revMsb & 0x7F;
            if (m >= 0x05 && m <= 0x08) revDelay = 1;
            else if (m == 1 || m == 0x11 || m == 0x12 || m == 0x13) hall = 1;
        }
        int dlyOn = 0;
        if (!c.spatXg)
            dlyOn = c.dlySend > 28;
        else if ((c.dlyMsb >= 5 && c.dlyMsb <= 8) || c.dlyMsb == 0x14)
            dlyOn = c.dlySend > 28;
        if (dlyOn || (revDelay && c.revSend > 28)) {
            int ms = dlyOn ? (int)(gs_delay_ms(c.dlyTime) + 0.5) : (50 + c.revTime * 5);
            if (ms < 35) return e;
            if (ms > 1000) ms = 1000;
            int send = dlyOn ? c.dlySend : c.revSend;
            float g = (send / 127.0f) * 0.28f;
            if (dlyOn && c.dlyFb > 64) g += (c.dlyFb - 64) / 640.0f;
            if (g > 0.32f) g = 0.32f;
            if (g < 0.06f) return e;
            e.mode = 1;
            e.ms = ms;
            e.gain = g;
            return e;
        }
        int varHall = c.spatXg && c.dlySend > 24 && c.dlyMsb >= 1 && c.dlyMsb <= 4;
        if ((c.revSend > 20 && !revDelay) || varHall) {
            /* 40 01 37 はすでに ms。0 を 18ms にするとスラップエコーになる。 */
            int pre = c.revPre;
            if (pre <= 0) pre = hall ? 5 : 3;
            if (pre > 40) pre = 40;
            int send = varHall && c.revSend < c.dlySend ? c.dlySend : c.revSend;
            float g = (send / 127.0f) * (hall ? 0.20f : 0.16f);
            if (g > 0.22f) g = 0.22f;
            if (g < 0.05f) return e;
            e.mode = 2;
            e.ms = pre;
            e.gain = g;
            return e;
        }
        if (!c.spatXg && (c.choMode & 7) >= 6 && c.choSend > 48) {
            int ms = 45 + (c.choDly * 70) / 127;
            float g = (c.choSend / 127.0f) * 0.26f;
            if (g > 0.28f) g = 0.28f;
            e.mode = 1;
            e.ms = ms;
            e.gain = g;
        }
        return e;
    }
    inline void spat_tail(const tone_color& c, int& tail, int& fb, int& bright, int& dark)
    {
        tail = 0;
        fb = 0;
        bright = 0;
        dark = 0;
        if (c.spatPass == 1) return;
        const int xg = c.spatXg;
        int revSend = c.revSend;
        if (c.choToRev > 0 && c.choSend > 0)
            revSend = spat_send(revSend + (c.choSend * c.choToRev) / 254);
        int revClass = 0;
        int revW = 0;
        if (!xg) {
            switch (c.revMode & 7) {
            case 0: revClass = 1; revW = 40; break;
            case 1: revClass = 1; revW = 50; break;
            case 2: revClass = 1; revW = 60; break;
            case 3: revClass = 2; revW = 82; break;
            case 4: revClass = 2; revW = 100; break;
            case 5: revClass = 3; revW = 70; break;
            case 6: revClass = 4; revW = 68; break;
            default: revClass = 4; revW = 78; break;
            }
        } else {
            const int m = c.revMsb & 0x7F;
            const int l = c.revLsb & 0x7F;
            if (m == 1) { revClass = 2; revW = (l == 0) ? 84 : 100; }
            else if (m == 2) { revClass = 1; revW = (l <= 0) ? 40 : (l == 1) ? 50 : 60; }
            else if (m == 3) { revClass = 1; revW = 66; }
            else if (m == 4 || m == 0x10) { revClass = 3; revW = (m == 4) ? 70 : 54; }
            else if (m == 0x11 || m == 0x12) { revClass = 2; revW = 100; }
            else if (m == 0x13) { revClass = 2; revW = 88; dark = 1; }
            else if (m >= 0x05 && m <= 0x08) { revClass = 4; revW = 72; }
            else if (m == 0x09 || m == 0x14) { revClass = 1; revW = 48; }
        }
        int timeScale = c.revTime;
        if (timeScale < 8) timeScale = 8;
        int tMul = 72 + (timeScale * 56) / 127;
        if (c.revChar != 4) {
            int dch = c.revChar - 4;
            if (dch > 3) dch = 3;
            if (dch < -3) dch = -3;
            tMul += dch * 5;
        }
        if (c.revPre > 48) tMul += (c.revPre - 48) / 16;
        if (tMul < 48) tMul = 48;
        if (tMul > 136) tMul = 136;
        int revStep = 0;
        int revFb = 0;
        if (revClass >= 1 && revClass <= 3 && revSend > 16) {
            int amt = (revSend * revW / 127) * tMul / 100;
            if (amt >= 60) revStep = 2;
            else if (amt >= 24) revStep = 1;
            if (revClass == 3 && revStep > 0) bright = 1;
            if (c.revLpf >= 4 && revStep > 0) dark = 1;
        } else if (revClass == 4 && revSend > 24) {
            int amt = (revSend * revW / 127) * tMul / 100;
            if (amt >= 32) revStep = 1;
            if (c.revFb >= 72 && revSend > 40) revFb = 1;
            if (c.revLpf >= 5) dark = 1;
        }
        int dlySend = c.dlySend;
        if (c.dlyTrim >= 0 && c.dlyTrim < 127)
            dlySend = dlySend * c.dlyTrim / 127;
        if (c.choToDly > 0 && c.choSend > 0)
            dlySend = spat_send(dlySend + (c.choSend * c.choToDly) / 254);
        int dlyStep = 0;
        int dlyFbAdd = 0;
        int dlySeries = 0;
        int varRev = 0;
        if (dlySend > 16) {
            int w = 0;
            if (!xg) {
                switch (c.dlyMode) {
                case 0: w = 52; break;
                case 1: w = 66; break;
                case 2: w = 80; break;
                case 3: w = 96; break;
                case 4: w = 58; break;
                case 5: w = 70; break;
                case 6: w = 82; break;
                case 7: w = 94; break;
                case 8: w = 68; dlySeries = 1; break;
                default: w = 86; break;
                }
            } else {
                const int m = c.dlyMsb & 0x7F;
                if (m >= 1 && m <= 4) { w = 78; varRev = 1; }
                else if (m == 5 || m == 6) w = 68;
                else if (m == 7 || m == 8) w = 88;
                else if (m == 9) w = 36;
                else if (m >= 0x10 && m <= 0x13) { w = 80; varRev = 1; }
                else if (m == 0x14) { w = 72; dlySeries = 1; }
            }
            if (w > 0) {
                int amt = (dlySend * w / 127) * dly_time_mul(c.dlyTime) / 100;
                if (c.dlyFb > 88) amt += (c.dlyFb - 88) / 10;
                if (amt >= 34) dlyStep = 1;
                if ((c.dlyFb >= 104 && amt >= 40) || (!xg && c.dlyMode >= 9 && amt >= 36))
                    dlyFbAdd = 1;
                if (c.dlyLpf >= 5) dark = 1;
            }
        }
        if (revClass == 4 || varRev) {
            tail = (revStep > dlyStep) ? revStep : dlyStep;
            if (revClass == 4 && tail > 1) tail = 1;
            if (tail > 2) tail = 2;
            fb = (revFb || dlyFbAdd) ? 1 : 0;
        } else {
            tail = revStep;
            if (revStep == 0)
                tail = dlyStep;
            else if (dlyStep && tail < 2 && (dlySeries || dlySend >= 64))
                tail += 1;
            fb = dlyFbAdd;
        }
        int choShort = 0;
        if (!xg && (c.choMode & 7) >= 6) choShort = 1;
        if (choShort && c.choSend > 70 && tail == 0) tail = 1;
        if (choShort && !xg && (c.choMode & 7) == 7 && c.choFb > 48 && c.choSend > 56 && !fb)
            fb = 1;
        if (tail > 2) tail = 2;
    }
    inline tone_paint make_tone_paint(const tone_color& c)
    {
        tone_paint p = {};
        p.arD = (64 - c.attack) * 4 / 63;
        p.drD = (64 - c.decay) * 3 / 63;
        const int envRr = (64 - c.release) * 3 / 63;
        p.rrD = envRr;
        int tail = 0, spatFb = 0, bright = 0, dark = 0;
        spat_tail(c, tail, spatFb, bright, dark);
        p.rrD -= tail;
        /* 64 = その音色のまま。LPF を開いても変調を足さない（開く＝パッチどおり、
           閉じる＝モジュレータを沈める）。開いたときに modTl を足すと側波が増えてジーになる。 */
        p.fbDelta = spatFb;
        {
            const int rsn = c.reso - 64;
            if (rsn > 48)
                p.fbDelta += 2;
            else if (rsn > 24)
                p.fbDelta += 1;
            else if (rsn < -24)
                p.fbDelta -= 1;
        }
        if (c.choSend > 28 && spatFb == 0) {
            int addFb = 0;
            if (!c.spatXg) {
                const int m = c.choMode & 7;
                if ((m == 4 || m == 5) && c.choFb >= 28) addFb = 1;
            } else if ((c.choMsb == 0x43 || c.choMsb == 0x48) && c.choFb >= 48) {
                addFb = 1;
            }
            if (addFb) p.fbDelta += 1;
        }
        const int hpf = c.hpf - 64;
        if (hpf > 0)
            p.fbDelta -= hpf * 2 / 63;
        const int eqL = (c.eqLo - 64) * 4 / 63;
        const int eqH = (c.eqHi - 64) * 4 / 63;
        const int cutOff = c.cutoff - 64;
        p.carTl = -eqL;
        if (hpf > 0)
            p.carTl += hpf * 3 / 63;
        p.modTl = -eqH / 2;
        if (cutOff < 0) {
            p.modTl += cutOff * 6 / 63;
            if (cutOff <= -24)
                p.fbDelta -= 1;
        } else if (cutOff > 16) {
            p.carTl -= (cutOff - 16) * 3 / 63;
        }
        if (dark) p.modTl -= 1;
        else if (bright) p.modTl += 1;
        p.amsOn = 0;
        if (c.spatXg && c.choSend > 24) {
            const int cm = c.choMsb & 0x7F;
            if (cm == 0x45 || cm == 0x46 || cm == 0x47 || cm == 0x56) p.amsOn = 1;
        }
        apply_insertion_paint(p, c);
        apply_bank_fallback_paint(p, c);
        if (p.rrD < envRr - 2) p.rrD = envRr - 2;
        p.rrD = tone_clmpi(p.rrD, -4, 4);
        p.fbDelta = tone_clmpi(p.fbDelta, -2, 3);
        p.carTl = tone_clmpi(p.carTl, -16, 12);
        p.modTl = tone_clmpi(p.modTl, -16, 16);
        p.dtD = tone_clmpi(p.dtD, -3, 3);
        p.mulD = tone_clmpi(p.mulD, -2, 2);
        p.slD = tone_clmpi(p.slD, -4, 4);
        return p;
    }
    inline double tone_vib_delay_sec(int vibDelay)
    {
        int dly = vibDelay - 64;
        if (dly < 0) dly = 0;
        return dly * (1.5 / 63.0);
    }

    class note_factory{//:uncopyable{//C³ by Kobarin
    public:
        virtual note* note_on(int_least32_t program, int note, int velocity, double frequency_multiplier)=0;
        virtual void set_tone_color(const tone_color&) {}
    protected:
        ~note_factory(){}
    };

    // MIDI`lB
    class channel{//:uncopyable{//C³ by Kobarin
        enum{ NUM_NOTES = 128 };
    public:
        channel(note_factory* factory, int bank, int chIndex = 0);
        ~channel();

        int synthesize(sample_t* out, std::size_t samples, double rate, int_least32_t master_volume, int master_balance);
        void reset_all_parameters();
        void reset_all_controller();
        void all_note_off();
        void all_sound_off();
        void all_sound_off_immediately();

        void note_off(int note, int velocity);
        void note_on(int note, int velocity);
        void polyphonic_key_pressure(int note, int value);
        void program_change(int value){ set_program(128 * bank + value); }
        void channel_pressure(int value);
        void pitch_bend_change(int value){ pitch_bend = value; update_frequency_multiplier(); }
        void control_change(int control, int value);
        void bank_select(int value);

        void set_bank(int value){ bank = value; }
        void set_rhythm_part(int on){
            if(on){
                int kit = ((bank & 0x3F80) == 0x3C00) ? (bank & 0x7F) : 0;
                default_bank = 0x3C00;
                bank = 0x3C00 | kit;
            }else{
                default_bank = 0x3C80;
                bank = 0x3C80;
            }
            program = bank * 128 + (program & 0x7F);
        }
        void set_program(int value){ program = value; }
        void set_panpot(int value){ panpot = value; }
        void set_volume(int value){ volume = value; }
        void set_expression(int value){ expression = value; }
        void set_pitch_bend_sensitivity(int value){ pitch_bend_sensitivity = value; update_frequency_multiplier(); }
        void set_modulation_depth(int value){ modulation_depth = value; update_modulation(); }
        void set_modulation_depth_range(int value){ modulation_depth_range = value; update_modulation(); }
        void set_damper(int value);
        void set_sostenute(int value);
        void set_freeze(int value);
        void set_fine_tuning(int value){ fine_tuning = value; update_frequency_multiplier(); }
        void set_coarse_tuning(int value){ coarse_tuning = value; update_frequency_multiplier(); }
        void set_RPN(int value){ RPN = value; NRPN = 0x3FFF; }
        void set_NRPN(int value){ NRPN = value; RPN = 0x3FFF; }
        void set_tremolo_frequency(double value){ tremolo_frequency = value; }
        void set_vibrato_frequency(double value){ vibrato_frequency = value; }
        void set_master_frequency_multiplier(double value){ master_frequency_multiplier = value; update_frequency_multiplier(); }
        void set_mute(bool mute_){ mute = mute_; }
        void set_system_mode(system_mode_t mode);
        void set_effect_mode(int kind, int value);
        void set_sys_fx_level(int kind, int value);
        void set_efx_on(int slot, int on);
        void apply_xg_part(int addr, int value);
        void apply_gs_tone(int addr, int value);
        void apply_gs_part_mix(int addr, int value);
        void apply_drum_param(int note, int param, int value);
        void apply_gs_sysfx_byte(int addr, int val);
        void set_eq_band(int hi, int val);
        void apply_gs_efx_byte(int addr, int val);
        void apply_xg_ins_byte(int slot, int addr, int val);
        void apply_xg_sysfx_byte(int addr, int val);
        tone_color effect_color() const;
        void mono_mode_on(){ all_note_off(); mono = true; }
        void poly_mode_on(){ all_note_off(); mono = false; }

        int get_program()const{ return program; }
        int get_bank()const{ return bank; }
        int get_panpot()const{ return panpot; }
        int get_volume()const{ return volume; }
        int get_expression()const{ return expression; }
        int get_channel_pressure()const{ return pressure; }
        int get_pitch_bend()const{ return pitch_bend; }
        int get_pitch_bend_sensitivity()const{ return pitch_bend_sensitivity; }
        int get_modulation_depth()const{ return modulation_depth; }
        int get_modulation_depth_range()const{ return modulation_depth_range; }
        int get_damper()const{ return damper; }
        int get_sostenute()const{ return sostenute; }
        int get_freeze()const{ return freeze; }
        int get_fine_tuning()const{ return fine_tuning; }
        int get_coarse_tuning()const{ return coarse_tuning; }
        int get_RPN()const{ return RPN; }
        int get_NRPN()const{ return NRPN; }
        double get_tremolo_frequency()const{ return tremolo_frequency; }
        double get_vibrato_frequency()const{ return vibrato_frequency; }
        bool get_mute()const{ return mute; }
        bool get_mono_mode()const{ return mono; }

    private:
        struct NOTE{
            class note* note;
            int key;
            enum STATUS{
                NOTEON, NOTEOFF, SOUNDOFF
            }status;
            NOTE(class note* p, int key_):note(p),key(key_),status(NOTEON){}
        };
        std::vector<NOTE> notes;
        note_factory* factory;
        int default_bank;
        int program;
        int bank;
        int panpot;
        int volume;
        int expression;
        int pressure;
        int pitch_bend;
        int pitch_bend_sensitivity;
        int modulation_depth;
        int modulation_depth_range;
        int damper;
        int sostenute;
        int freeze;
        int fine_tuning;
        int coarse_tuning;
        int RPN;
        int NRPN;
        bool mono;
        bool mute;
        double tremolo_frequency;
        double vibrato_frequency;
        double frequency_multiplier;
        double master_frequency_multiplier;
        system_mode_t system_mode;
        int fxRevSend, fxChoSend, fxDlySend;
        int fxRevMode, fxChoMode, fxDlyMode, fxInsMode, fxIns2;
        int fxInsOn, fxInsOn2, fxInsSys1, fxInsSys2;
        int sysRevLevel, sysChoLevel, sysDlyLevel;
        int nrpnCutoff, nrpnReso, nrpnHpf, nrpnAtk, nrpnDec, nrpnRel;
        int nrpnVibRate, nrpnVibDepth, nrpnVibDelay;
        int eqLoGain, eqHiGain;
        int chIndex;
        int fxInsFam, fxInsPacked, fxIns2Packed;
        int fxInsDrive, fxInsLo, fxInsHi;
        int fxInsP[32];
        int fxVarPacked, fxVarConn, fxVarPart;
        int fxVarP[10];
        int fxRevTime, fxRevChar, fxRevLpf, fxRevFb, fxRevPre;
        int fxRevMsb, fxRevLsb;
        int fxChoRate, fxChoDepth, fxChoFb, fxChoDly, fxChoLpf, fxChoToRev, fxChoToDly;
        int fxChoMsb, fxChoLsb;
        int fxDlyTime, fxDlyFb, fxDlyLpf, fxDlyToRev;
        int fxDlyLvlC, fxDlyLvlL, fxDlyLvlR;
        int fxDlyMsb, fxDlyLsb;
        int keyShift, fineCents;
        int keyLo, keyHi;
        int velSens, velOff;
        int softPedal, breath, foot, balance, phaser, tremDepth;
        int portaOn, portaTime, portaFrom, lastKey, portaKey;
        int portaMs, portaSpan, portaLeft;
        int dataMsb;
        int drumKey[128], drumLv[128], drumPan[128], drumRev[128], drumCho[128], drumDly[128];
        int ccMem[128];

        int get_registered_parameter();
        void set_registered_parameter(int value);
        void update_frequency_multiplier();
        void update_modulation();
        void update_fx_vibrato();
        void touch_tone();
        void apply_nrpn_data(int value);
    };

    // MIDIVZTCUB
    class synthesizer{//:uncopyable{//C³ by Kobarin
        enum{ NUM_CHANNELS = 16 };
    public:
        synthesizer(note_factory* factory);

        channel* get_channel(int ch);

        int synthesize(sample_t* output, std::size_t samples, double rate);
        int synthesize_mixing(sample_t* output, std::size_t samples, double rate);
        void reset();
        void reset_all_parameters();
        void reset_all_controller();
        void all_note_off();
        void all_sound_off();
        void all_sound_off_immediately();

        void note_on(int channel, int note, int velocity){ get_channel(channel)->note_on(note, velocity); }
        void note_off(int channel, int note, int velocity){ get_channel(channel)->note_off(note, velocity); }
        void polyphonic_key_pressure(int channel, int note, int value){ get_channel(channel)->polyphonic_key_pressure(note, value); }
        void control_change(int channel, int control, int value){ get_channel(channel)->control_change(control, value); }
        void program_change(int channel, int program){ get_channel(channel)->program_change(program); }
        void channel_pressure(int channel, int value){ get_channel(channel)->channel_pressure(value); }
        void pitch_bend_change(int channel, int value){ get_channel(channel)->pitch_bend_change(value); }
        void sysex_message(const void* data, std::size_t size);
        void midi_event(int command, int param1, int param2);
        void midi_event(uint_least32_t message){ midi_event(message & 0xFF, (message >> 8) & 0x7F, (message >> 16) & 0x7F); }

        void set_main_volume(int value){ main_volume = value; }
        void set_master_volume(int value){ master_volume = value; }
        void set_master_balance(int value){ master_balance = value; }
        void set_master_fine_tuning(int value){ master_fine_tuning = value; update_master_frequency_multiplier(); }
        void set_master_coarse_tuning(int value){ master_coarse_tuning = value; update_master_frequency_multiplier(); }
        void set_system_mode(system_mode_t mode);

        int get_main_volume()const{ return main_volume; }
        int get_master_volume()const{ return master_volume; }
        int get_master_balance()const{ return master_balance; }
        int get_master_fine_tuning()const{ return master_fine_tuning; }
        int get_master_coarse_tuning()const{ return master_coarse_tuning; }
        system_mode_t get_system_mode()const{ return system_mode; }

    private:
        std::auto_ptr<channel> channels[NUM_CHANNELS];
        double active_sensing;
        int main_volume;
        int master_volume;
        int master_balance;
        int master_fine_tuning;
        int master_coarse_tuning;
        double master_frequency_multiplier;
        system_mode_t system_mode;
        void update_master_frequency_multiplier();
    };

    // ³·g¶¬íB
    // U 32768 (-32767`32767) Ì³·gð¶¬·éB
    class sine_wave_generator{
    public:
        sine_wave_generator();
        sine_wave_generator(double cycle);
        void set_cycle(double cycle);
        void add_modulation(int_least32_t x);
        int get_next();
        int get_next(int_least32_t modulation);
    private:
        uint_least32_t position;
        uint_least32_t step;
    };

    // Gx[v¶¬íB
    // TL=0 ÌÆ« 0`32767 Ìlð¶¬·éB
    class envelope_generator{
    public:
        envelope_generator(int AR, int DR, int SR, int RR, int SL, int TL);
        void set_rate(double rate);
        void set_hold(double value);
        void set_freeze(double value);
        void key_off();
        void sound_off();
        bool is_finished()const{ return state == FINISHED; }
        int get_next();
    private:
        enum{ ATTACK, ATTACK_RELEASE, DECAY, DECAY_RELEASE, SASTAIN, RELEASE, SOUNDOFF, FINISHED }state;
        int AR, DR, SR, RR, TL;
        uint_least32_t fAR, fDR, fSR, fRR, fSL, fTL, fOR, fSS, fDRR, fDSS;
        uint_least32_t current;
        double rate;
        double hold;
        double freeze;
        void update_parameters();
    };

    // FMIy[^ (W[^¨æÑLA)B
    class fm_operator{
    public:
        fm_operator(int AR, int DR, int SR, int RR, int SL, int TL, int KS, int ML, int DT, int AMS, int key);
        void set_freq_rate(double freq, double rate);
        void set_hold(double value){ eg.set_hold(value); }
        void set_freeze(double value){ eg.set_freeze(value); }
        void add_modulation(int_least32_t x){ swg.add_modulation(x); }
        void key_off(){ eg.key_off(); }
        void sound_off(){ eg.sound_off(); }
        bool is_finished()const{ return eg.is_finished(); }
        int get_next();
        int get_next(int modulate);
        int get_next(int lfo, int modulate);
        inline int operator()(){ return get_next(); }
        inline int operator()(int m){ return get_next(m); }
        inline int operator()(int lfo, int m){ return get_next(lfo, m); }
    private:
        sine_wave_generator swg;
        envelope_generator eg;
        double ML;
        double DT;
        int_least32_t ams_factor;
        int_least32_t ams_bias;
    };

    // FM¹¹p[^B
    struct FMPARAMETER{
        int ALG, FB, LFO;
        struct{
            int AR, DR, SR, RR, SL, TL, KS, ML, DT, AMS;
        }op1, op2, op3, op4;
        int transpose;
    };
    // hp[^B
    struct DRUMPARAMETER:FMPARAMETER{
        int key, panpot, assign;
    };

    // FMTEhWFl[^B
    class fm_sound_generator{
    public:
        fm_sound_generator(const FMPARAMETER& params, int note, double frequency_multiplier);
        void set_rate(double rate);
        void set_frequency_multiplier(double value);
        void set_damper(int damper);
        void set_sostenute(int sostenute);
        void set_freeze(int freeze);
        void set_tremolo(int depth, double frequency);
        void set_vibrato(double depth, double frequency);
        void key_off();
        void sound_off();
        bool is_finished()const;
        int get_next();
    private:
        fm_operator op1;
        fm_operator op2;
        fm_operator op3;
        fm_operator op4;
        sine_wave_generator ams_lfo;
        sine_wave_generator vibrato_lfo;
        sine_wave_generator tremolo_lfo;
        int ALG;
        int FB;
        double freq;
        double freq_mul;
        double ams_freq;
        bool ams_enable;
        int tremolo_depth;
        double tremolo_freq;
        int vibrato_depth;
        double vibrato_freq;
        double rate;
        int feedback;
        int damper;
        int sostenute;
    };

    // FM¹¹m[gB
    class fm_note:public note{
    public:
        fm_note(const FMPARAMETER& params, int note, int velocity, int panpot, int assign, double frequency_multiplier);
        virtual void release(){ delete this; }
        virtual bool synthesize(sample_t* buf, std::size_t samples, double rate, sample_t left, sample_t right);
        virtual void note_off(int velocity);
        virtual void sound_off();
        virtual void set_frequency_multiplier(double value);
        virtual void set_tremolo(int depth, double freq);
        virtual void set_vibrato(double depth, double freq);
        virtual void set_damper(int value);
        virtual void set_sostenute(int value);
        virtual void set_freeze(int value);
    public:
        fm_sound_generator fm;
        int velocity;
    };

    // FM¹¹m[gt@NgB
    class Ym2612Pool;
    class fm_note_factory:public note_factory{
    public:
        fm_note_factory();
        ~fm_note_factory();
        void clear();
        bool load_wopn(const wchar_t* path, int family = 0, int append = 0);
        bool load_wopn_mem(const void* data, size_t size, int family = 0, int append = 0);
        void get_program(int number, FMPARAMETER& p);
        bool set_program(int number, const FMPARAMETER& p);
        bool set_drum_program(int number, const DRUMPARAMETER& p);
        virtual note* note_on(int_least32_t program, int note, int velocity, double frequency_multiplier);
        void set_tone_color(const tone_color& c) { color = c; }
        void set_raira(int raira);
        void reset_pool_frame();
        void begin_pool_frame(std::size_t samples, double rate);
        void end_pool_frame();
    private:
        tone_color color;
        std::map<int, FMPARAMETER> programs;
        std::map<int, DRUMPARAMETER> drums;
        Ym2612Pool* ym;
    };
//}

#endif
