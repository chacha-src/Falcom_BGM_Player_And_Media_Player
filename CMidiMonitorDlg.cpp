#include "stdafx.h"
#include "CMidiMonitorDlg.h"
#include "oggDlg.h"
#include "PlayList.h"
#include "CMediaPlayerDlg.h"
#include "CEqualizer.h"
#include "resource.h"
#include "VstMidiEngine.h"
#include "VstHostDlg.h"
#include "PluginKinds.h"
#include "SasamiToneNames.h"
#include "kb_sasami/source/sasami_midi.h"
#include <math.h>
#include <mmsystem.h>

class COggDlg;
extern COggDlg* og;
#include <algorithm>
#include <new>
/* クロスフェードで B(スロット1)が現行になった後も、UI から正しいエンジンを見る */
void MmBindVstActiveSlot();
extern save savedata;

/*
 * MIDI モニタの描画ペース
 *   タイマ1 (16ms): 位置保存 + PumpIdle（本体）
 *   タイマ2 (4ms): IdlePulse（キューが空で CPU に余裕があるときだけ追加同期）
 *   COggApp::OnIdle: 同じく IdlePulse。TRUE を返すと OnIdle が回り続けるので基底へ返す。
 *   timerp: PumpSyncNow は毎ティック。UpdateWindow だけ Ms2DrawDue。
 * QS_POSTMESSAGE で IdlePulse を止めると timerp の投稿でstarveするので見ない。
 */
extern CString filen;
extern int mode;
extern int tempo;
extern int pitch;
extern __int64 playb;
extern int playy;
extern int wavbit_sample_Hz;

namespace {

enum {
	IDM_MM_VIEW_2D = 42450,
	IDM_MM_VIEW_3D = 42451,
	IDM_MM_CAM_RESET = 42452,
	IDM_MM_FREEZE = 42453,
	IDM_MM_TOPMOST = 42454,
	IDM_MM_COPY = 42455,
	IDM_MM_RELOAD = 42456,
	IDM_MM_MAP_AUTO = 42457,
	IDM_MM_MAP_GS = 42458,
	IDM_MM_MAP_XG = 42459,
	IDM_MM_MAP_55 = 42460,
	IDM_MM_MAP_88 = 42461,
	IDM_MM_MAP_88P = 42462,
	IDM_MM_MAP_8820 = 42463,
	IDM_MM_MAP_GM = 42464,
	IDM_MM_MAP_SD = 42465,
	IDM_MM_MAP_LA = 42466,
	IDM_MM_MAP_GM2 = 42467,
	IDM_MM_MAP_NS = 42468,
	IDM_MM_MAP_KW = 42469,
	IDM_MM_MAP_SG = 42470,
	IDM_MM_MAP_KR = 42471,
	IDM_MM_MAP_PA = 42472,
	IDM_MM_MAP_CS = 42473,
	IDM_MM_MAP_GEM = 42474,
	IDM_MM_MAP_LK = 42475,
	IDM_MM_MAP_PV = 42476,
	IDM_MM_DRUM_PART = 42477,
	IDM_MM_DRUM_AB10 = 42478
};

HMIDIOUT s_kpiLiveOut = NULL;

// イベント時刻のサンプルレート。VST はエンジン、KPI MIDI は実際に開いたレート
// （savedata.samples だと 48k 設定＋44.1k KPI でモニタだけ走る）。
static int MmWantMonitorSampleRate()
{
	if (mode == MODE_VST_MIDI) {
		MmBindVstActiveSlot();
		const int r = VstMidiGetRate();
		if (r > 0) return r;
	} else if (mode == -3 && wavbit_sample_Hz >= 8000) {
		return wavbit_sample_Hz;
	}
	if (savedata.samples >= 8000)
		return savedata.samples;
	return 44100;
}

static void MmCloseKpiLiveOut()
{
	if (!s_kpiLiveOut) return;
	midiOutReset(s_kpiLiveOut);
	midiOutClose(s_kpiLiveOut);
	s_kpiLiveOut = NULL;
}

static int MmVstHostOpen()
{
	return (g_vstHostDlg && ::IsWindow(g_vstHostDlg->GetSafeHwnd())) ? 1 : 0;
}

// Host wiring: a multi on parts 1–16 covers only that block. Empty B rows
// must not follow A MIDI (and the other way around).
static int MmLiveHostPartOn(int part)
{
	if (part < 0 || part >= CMidiMonitorDlg::PART_MAX) return 0;
	if (!MmVstHostOpen()) return 1;
	wchar_t plug[40] = {};
	g_vstHostDlg->PartPluginName(part, plug, 40);
	return plug[0] ? 1 : 0;
}

// Host closed → 0. Do not reuse MmLiveHostPartOn (that returns 1 when closed).
static int MmHostSlotOccupied(int part)
{
	if (part < 0 || part >= CMidiMonitorDlg::PART_MAX) return 0;
	if (!MmVstHostOpen()) return 0;
	wchar_t plug[40] = {};
	g_vstHostDlg->PartPluginName(part, plug, 40);
	return plug[0] ? 1 : 0;
}

static constexpr COLORREF MM_BG = RGB(8, 8, 12);
static constexpr COLORREF MM_HEAD_BG = RGB(196, 196, 200);
static constexpr COLORREF MM_CHROMA = RGB(8, 8, 12);
static constexpr COLORREF MM_GRID = RGB(78, 82, 96);
static constexpr COLORREF MM_FG = RGB(230, 230, 236);
static constexpr COLORREF MM_HEAD_TX = RGB(28, 28, 36);
static constexpr COLORREF MM_ROW_A = RGB(16, 17, 22);
static constexpr COLORREF MM_ROW_B = RGB(11, 12, 16);
static constexpr COLORREF MM_ROW_U = RGB(30, 30, 34);
static constexpr COLORREF MM_ROW_D = RGB(96, 38, 22);
static constexpr COLORREF MM_TX_W = RGB(250, 250, 252);
static constexpr COLORREF MM_TX_D = RGB(214, 214, 220);
static constexpr COLORREF MM_TX_U = RGB(128, 128, 136);
static constexpr COLORREF MM_TX_DR = RGB(255, 216, 176);
static constexpr COLORREF MM_KEY_W = RGB(240, 240, 244);
static constexpr COLORREF MM_KEY_D = RGB(204, 206, 212);
static constexpr COLORREF MM_KEY_U = RGB(110, 110, 118);
static constexpr COLORREF MM_KEY_DR = RGB(255, 196, 148);
static constexpr COLORREF MM_KEYB = RGB(22, 22, 26);
static constexpr COLORREF MM_KEYB_U = RGB(52, 52, 58);
static constexpr COLORREF MM_KEYB_DR = RGB(56, 18, 12);

enum {
	MM_HIT_NONE = 0,
	MM_HIT_APPVOL,
	MM_HIT_VOL,
	MM_HIT_PAN,
	MM_HIT_EXP,
	MM_HIT_REV,
	MM_HIT_CRS,
	MM_HIT_VAR,
	MM_HIT_KEYS,
	MM_HIT_PC
};

enum {
	MM_LATCH_VOL = 1,
	MM_LATCH_PAN = 2,
	MM_LATCH_EXP = 4,
	MM_LATCH_REV = 8,
	MM_LATCH_CRS = 16,
	MM_LATCH_VAR = 32,
	MM_LATCH_PC = 64
};

static int MmCcForHit(int kind)
{
	if (kind == MM_HIT_VOL) return 7;
	if (kind == MM_HIT_PAN) return 10;
	if (kind == MM_HIT_EXP) return 11;
	if (kind == MM_HIT_REV) return 91;
	if (kind == MM_HIT_CRS) return 93;
	if (kind == MM_HIT_VAR) return 94;
	return -1;
}

static int MmDefaultForHit(int kind)
{
	if (kind == MM_HIT_VOL) return 100;
	if (kind == MM_HIT_PAN) return 64;
	if (kind == MM_HIT_EXP) return 127;
	if (kind == MM_HIT_REV) return 40;
	return 0;
}

static int MmIsBlackKey(int n)
{
	const int m = n % 12;
	return (m == 1 || m == 3 || m == 6 || m == 8 || m == 10) ? 1 : 0;
}

static COLORREF MmMix(COLORREF a, COLORREF b, int t)
{
	if (t <= 0) return a;
	if (t >= 256) return b;
	const int ar = GetRValue(a), ag = GetGValue(a), ab = GetBValue(a);
	const int br = GetRValue(b), bg = GetGValue(b), bb = GetBValue(b);
	return RGB(ar + (br - ar) * t / 256, ag + (bg - ag) * t / 256, ab + (bb - ab) * t / 256);
}

static void MmGlowTick(BYTE& g)
{
	if (!g) return;
	g = (BYTE)((int)g * 7 / 8);
	if (g < 6) g = 0;
}

/* 行の汚れ判定は「絵に出る量」で見る。lev と glow は毎フレーム減衰するので
   生の値で比べると全行が常に不一致になり、32行を毎回塗り直してしまう。 */
static int MmLevPix(float lev, int bh)
{
	int v = (int)(lev * 127.f);
	if (v < 0) v = 0;
	if (v > 127) v = 127;
	return bh * v / 127;
}

static int MmGlowSig(BYTE g)
{
	/* 16段階。セル背景のフェードが見えるだけの再描画に抑える */
	return (int)(g >> 4);
}

static ULONGLONG MmFileTimeU64(const FILETIME& ft)
{
	ULARGE_INTEGER u;
	u.LowPart = ft.dwLowDateTime;
	u.HighPart = ft.dwHighDateTime;
	return u.QuadPart;
}

/* GetSystemTimes の idle が全体の 8% 未満なら他アプリが忙しい → IdlePulse をスキップ。
   初回は差分が無いので slack あり扱い。 */
static int MmCpuHasSlack()
{
	FILETIME idle, ker, user;
	if (!GetSystemTimes(&idle, &ker, &user))
		return 1;
	static FILETIME sIdle, sKer, sUser;
	static int sInit = 0;
	if (!sInit) {
		sIdle = idle;
		sKer = ker;
		sUser = user;
		sInit = 1;
		return 1;
	}
	const ULONGLONG di = MmFileTimeU64(idle) - MmFileTimeU64(sIdle);
	const ULONGLONG dk = MmFileTimeU64(ker) - MmFileTimeU64(sKer);
	const ULONGLONG du = MmFileTimeU64(user) - MmFileTimeU64(sUser);
	sIdle = idle;
	sKer = ker;
	sUser = user;
	const ULONGLONG sys = dk + du;
	if (sys < 1)
		return 1;
	return (int)(di * 100ull / sys) >= 8;
}

/* QPC の freq と now。freq 未取得なら一度だけ QueryPerformanceFrequency。 */
static void MmQpcPair(LONGLONG& freq, LONGLONG& now)
{
	if (freq < 1) {
		LARGE_INTEGER f;
		QueryPerformanceFrequency(&f);
		freq = f.QuadPart;
		if (freq < 1) freq = 1;
	}
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	now = t.QuadPart;
}

static void MmBumpFade(BYTE& g, int /*burst*/)
{
	g = 255;
}

static COLORREF MmTint(COLORREF base, COLORREF hi, BYTE fade)
{
	if (!fade) return base;
	return MmMix(base, hi, fade);
}

static void MmFillFade(CDC& dc, int x, int y, int w, int h, COLORREF base, COLORREF hi, BYTE fade)
{
	if (w <= 0 || h <= 0 || !fade) return;
	dc.FillSolidRect(x, y, w, h, MmTint(base, hi, fade));
}

static const wchar_t* kGmName[128] = {
	L"Acoustic Grand", L"Bright Piano", L"Electric Grand", L"Honky-tonk",
	L"E.Piano 1", L"E.Piano 2", L"Harpsichord", L"Clavi",
	L"Celesta", L"Glockenspiel", L"Music Box", L"Vibraphone",
	L"Marimba", L"Xylophone", L"Tubular Bells", L"Dulcimer",
	L"Drawbar Organ", L"Percussive Organ", L"Rock Organ", L"Church Organ",
	L"Reed Organ", L"Accordion", L"Harmonica", L"Tango Accordion",
	L"Nylon Guitar", L"Steel Guitar", L"Jazz Guitar", L"Clean Guitar",
	L"Muted Guitar", L"Overdrive Gt", L"Distortion Gt", L"Gt Harmonics",
	L"Acoustic Bass", L"Finger Bass", L"Picked Bass", L"Fretless Bass",
	L"Slap Bass 1", L"Slap Bass 2", L"Synth Bass 1", L"Synth Bass 2",
	L"Violin", L"Viola", L"Cello", L"Contrabass",
	L"Tremolo Str", L"Pizzicato Str", L"Harp", L"Timpani",
	L"Strings", L"Slow Strings", L"Syn.Strings1", L"Syn.Strings2",
	L"Choir Aahs", L"Voice Oohs", L"SynVox", L"Orchestra Hit",
	L"Trumpet", L"Trombone", L"Tuba", L"Muted Trumpet",
	L"French Horns", L"Brass 1", L"Synth Brass1", L"Synth Brass2",
	L"Soprano Sax", L"Alto Sax", L"Tenor Sax", L"Baritone Sax",
	L"Oboe", L"EnglishHorn", L"Bassoon", L"Clarinet",
	L"Piccolo", L"Flute", L"Recorder", L"Pan Flute",
	L"Blown Bottle", L"Shakuhachi", L"Whistle", L"Ocarina",
	L"Square Lead", L"Saw Lead", L"Calliope", L"Chiff Lead",
	L"Charang", L"Voice Lead", L"Fifths Lead", L"Bass & Lead",
	L"New Age Pad", L"Warm Pad", L"Polysynth", L"Choir Pad",
	L"Bowed Glass", L"Metallic Pad", L"Halo Pad", L"Sweep Pad",
	L"Rain", L"Soundtrack", L"Crystal", L"Atmosphere",
	L"Brightness", L"Goblins", L"Echoes", L"Sci-Fi",
	L"Sitar", L"Banjo", L"Shamisen", L"Koto",
	L"Kalimba", L"Bagpipe", L"Fiddle", L"Shanai",
	L"Tinkle Bell", L"Agogo", L"Steel Drums", L"Woodblock",
	L"Taiko Drum", L"Melodic Tom", L"Synth Drum", L"Reverse Cymbal",
	L"Gt Fret Noise", L"Breath Noise", L"Seashore", L"Bird Tweet",
	L"Telephone", L"Helicopter", L"Applause", L"Gunshot"
};

static const wchar_t* kXgRev[] = {
	L"No Effect", L"Hall 1", L"Hall 2", L"Hall 3", L"Hall 4",
	L"Hall 5", L"Hall M", L"Hall L", L"Room 1", L"Room 2",
	L"Room 3", L"Room 4", L"Room 5", L"Room 6", L"Room 7",
	L"Stage 1", L"Stage 2", L"Plate 1", L"Plate 2", L"Delay",
	L"Panning Delay", L"White Room", L"Tunnel", L"Canyon", L"Basement"
};
static const wchar_t* kXgCho[] = {
	L"No Effect", L"Chorus 1", L"Chorus 2", L"Chorus 3", L"Chorus 4",
	L"FB Chorus", L"Flanger 1", L"Celeste 1", L"Celeste 2", L"Celeste 3",
	L"Flanger 2", L"Symphonic", L"Phaser"
};
static const wchar_t* kXgVar[] = {
	L"No Effect", L"Delay 1", L"Delay 2", L"Delay LCR", L"Echo",
	L"Cross Delay", L"ER 1", L"ER 2", L"Gate Reverb", L"Reverse Gate",
	L"Karaoke 1", L"Karaoke 2", L"Karaoke 3"
};
static const wchar_t* kGsRev[] = {
	L"Room 1", L"Room 2", L"Room 3", L"Hall 1", L"Hall 2",
	L"Plate", L"Delay", L"Panning Delay"
};
static const wchar_t* kGsCho[] = {
	L"Chorus 1", L"Chorus 2", L"Chorus 3", L"Chorus 4",
	L"Feedback Chorus", L"Flanger", L"Short Delay", L"Short Delay FB"
};
static const wchar_t* kGsDly[] = {
	L"Delay 1", L"Delay 2", L"Delay 3", L"Delay 4",
	L"Pan Delay 1", L"Pan Delay 2", L"Pan Delay 3", L"Pan Delay 4",
	L"Delay to Rev", L"Pan Repeat"
};
struct MmXgTn { BYTE msb; BYTE lsb; const wchar_t* n; };
static const MmXgTn kXgTypeNm[] = {
	{ 0x00, 0x00, L"Thru" },
	{ 0x01, 0x00, L"Hall 1" }, { 0x01, 0x01, L"Hall 2" },
	{ 0x02, 0x00, L"Room 1" }, { 0x02, 0x01, L"Room 2" }, { 0x02, 0x02, L"Room 3" },
	{ 0x03, 0x00, L"Stage 1" }, { 0x03, 0x01, L"Stage 2" },
	{ 0x04, 0x00, L"Plate" },
	{ 0x05, 0x00, L"Delay L,C,R" },
	{ 0x06, 0x00, L"Delay L,R" },
	{ 0x07, 0x00, L"Echo" },
	{ 0x08, 0x00, L"Cross Delay" },
	{ 0x09, 0x00, L"ER 1" }, { 0x09, 0x01, L"ER 2" },
	{ 0x0A, 0x00, L"Gate Reverb" },
	{ 0x0B, 0x00, L"Reverse Gate" },
	{ 0x10, 0x00, L"White Room" },
	{ 0x11, 0x00, L"Tunnel" },
	{ 0x12, 0x00, L"Canyon" },
	{ 0x13, 0x00, L"Basement" },
	{ 0x14, 0x00, L"Karaoke 1" }, { 0x14, 0x01, L"Karaoke 2" }, { 0x14, 0x02, L"Karaoke 3" },
	{ 0x41, 0x00, L"Chorus 1" }, { 0x41, 0x01, L"Chorus 2" }, { 0x41, 0x02, L"Chorus 3" }, { 0x41, 0x03, L"Chorus 4" },
	{ 0x42, 0x00, L"Celeste 1" }, { 0x42, 0x01, L"Celeste 2" }, { 0x42, 0x02, L"Celeste 3" }, { 0x42, 0x03, L"Celeste 4" },
	{ 0x43, 0x00, L"Flanger 1" }, { 0x43, 0x01, L"Flanger 2" }, { 0x43, 0x02, L"Flanger 3" },
	{ 0x44, 0x00, L"Symphonic" },
	{ 0x45, 0x00, L"Rotary Speaker" },
	{ 0x46, 0x00, L"Tremolo" },
	{ 0x47, 0x00, L"Auto Pan" },
	{ 0x48, 0x00, L"Phaser 1" }, { 0x48, 0x01, L"Phaser 2" },
	{ 0x49, 0x00, L"Distortion" }, { 0x49, 0x01, L"Comp+Distortion" },
	{ 0x4A, 0x00, L"Over Drive" },
	{ 0x4B, 0x00, L"Amp Simulator" },
	{ 0x4C, 0x00, L"3-Band EQ" },
	{ 0x4D, 0x00, L"2-Band EQ" },
	{ 0x4E, 0x00, L"Auto Wah" }, { 0x4E, 0x01, L"Auto Wah+Dist" }, { 0x4E, 0x02, L"Auto Wah+Od" },
	{ 0x50, 0x00, L"Pitch Change" }, { 0x50, 0x01, L"Pitch Change 2" },
	{ 0x51, 0x00, L"Aural Exciter" },
	{ 0x52, 0x00, L"Touch Wah 1" }, { 0x52, 0x01, L"Touch Wah+Dist" }, { 0x52, 0x02, L"Touch Wah+Od" }, { 0x52, 0x03, L"Touch Wah 2" },
	{ 0x53, 0x00, L"Compressor" },
	{ 0x54, 0x00, L"Noise Gate" },
	{ 0x55, 0x00, L"Voice Cancel" },
	{ 0x56, 0x00, L"2-Way Rotary" },
	{ 0x57, 0x00, L"Ensemble Detune" },
	{ 0x58, 0x00, L"Ambience" },
	{ 0x5D, 0x00, L"Talking Mod" },
	{ 0x5E, 0x00, L"Lo-Fi" },
	{ 0x5F, 0x00, L"Dist+Delay" }, { 0x5F, 0x01, L"Overdrive+Delay" },
	{ 0x60, 0x00, L"Comp+Dist+Delay" }, { 0x60, 0x01, L"Comp+Od+Delay" },
	{ 0x61, 0x00, L"Wah+Dist+Delay" }, { 0x61, 0x01, L"Wah+Od+Delay" },
};

static void MmCopyW(wchar_t* dst, int n, const wchar_t* src);
static BOOL MmLoadResDat(int id, BYTE** out, int* outN);
static BOOL MmLoadFileDat(const wchar_t* path, BYTE** out, int* outN);

static int MmXgUnpack(const BYTE* d, int n, int* ah, int* am, int* al, const BYTE** data, int* ndata)
{
	if (!d || n < 8 || d[0] != 0xf0)
		return 0;
	if (d[1] != 0x43 && d[1] != 0x42)
		return 0;
	if (d[3] != 0x4c)
		return 0;
	const int hasF7 = (n > 0 && d[n - 1] == 0xf7) ? 1 : 0;
	const int cmd = d[2] & 0xf0;
	/* Yamaha 43 1n 4C。Korg NS5R/NX5R の XG 互換は 42 3n 4C */
	if (cmd == 0x10 || (d[1] == 0x42 && cmd == 0x30)) {
		*ah = d[4] & 127;
		*am = d[5] & 127;
		*al = d[6] & 127;
		*data = d + 7;
		*ndata = n - 7 - hasF7;
		return *ndata > 0;
	}
	if (cmd == 0x00 && n >= 12) {
		*ah = d[6] & 127;
		*am = d[7] & 127;
		*al = d[8] & 127;
		*data = d + 9;
		int avail = n - 9 - hasF7 - 1;
		const int bc = ((d[4] & 127) << 7) | (d[5] & 127);
		if (bc > 0 && bc < avail) avail = bc;
		*ndata = avail;
		return *ndata > 0;
	}
	return 0;
}

static int MmInsNameFromDat(int fam, int msb, int lsb, wchar_t* out, int outN);

static void MmXgEffTypeName(int msb, int lsb, wchar_t* out, int outN)
{
	msb &= 127;
	lsb &= 127;
	if (MmInsNameFromDat('X', msb, lsb, out, outN)) return;
	const wchar_t* hit = NULL;
	const wchar_t* base = NULL;
	for (int i = 0; i < (int)(sizeof(kXgTypeNm) / sizeof(kXgTypeNm[0])); ++i) {
		if (kXgTypeNm[i].msb != (BYTE)msb) continue;
		if (kXgTypeNm[i].lsb == (BYTE)lsb) { hit = kXgTypeNm[i].n; break; }
		if (kXgTypeNm[i].lsb == 0) base = kXgTypeNm[i].n;
	}
	if (hit) { MmCopyW(out, outN, hit); return; }
	if (base) { MmCopyW(out, outN, base); return; }
	if (msb == 0 && lsb == 0) { MmCopyW(out, outN, L"Thru"); return; }
	_snwprintf_s(out, outN, _TRUNCATE, L"%d:%d", msb, lsb);
}

static void MmInsDisp(int mode, int slot, int insPacked, int varPacked, int varConn, wchar_t* out, int outN, int gsHasLsb)
{
	if (mode == 1) {
		if (slot != 0) { MmCopyW(out, outN, L"Thru"); return; }
		const int msb = (insPacked >> 8) & 127;
		const int lsb = insPacked & 127;
		if (gsHasLsb) {
			if (MmInsNameFromDat('G', msb, lsb, out, outN)) return;
			if (lsb == 0 && MmInsNameFromDat('8', msb, 0, out, outN)) return;
		} else {
			if (MmInsNameFromDat('8', msb, 0, out, outN)) return;
			if (MmInsNameFromDat('G', msb, lsb, out, outN)) return;
		}
		if (msb == 0 && lsb == 0) { MmCopyW(out, outN, L"Thru"); return; }
		MmCopyW(out, outN, L"EFX");
		return;
	}
	int packed = insPacked;
	if (slot == 0 && mode == 2 && packed == 0 && varConn == 0 && varPacked != 0)
		packed = varPacked;
	MmXgEffTypeName((packed >> 8) & 127, packed & 127, out, outN);
}

static BYTE* s_gsDat = NULL;
static int s_gsBytes = 0;
static BYTE* s_xgDat = NULL;
static int s_xgBytes = 0;
static BYTE* s_exDat = NULL;
static int s_exBytes = 0;
static int s_datTried = 0;
static wchar_t s_exLbl[32][12];
static int s_exLblReady = 0;

static void MmCopyW(wchar_t* dst, int n, const wchar_t* src)
{
	if (!dst || n <= 0) return;
	if (!src) src = L"";
	wcsncpy_s(dst, n, src, _TRUNCATE);
}

enum { INS_TMAX = 120, INS_PMAX = 16 };
struct MmInsP { BYTE n; BYTE kind; char name[16]; char extra[72]; };
struct MmInsT { BYTE fam; BYTE msb; BYTE lsb; BYTE np; char name[28]; MmInsP p[INS_PMAX]; };
static MmInsT s_insTab[INS_TMAX];
static int s_insTabN = 0;
static int s_insReady = 0;

static int MmInsHex2(const char* s)
{
	int v = 0;
	for (int i = 0; i < 2 && s[i]; ++i) {
		const char c = s[i];
		v <<= 4;
		if (c >= '0' && c <= '9') v += c - '0';
		else if (c >= 'A' && c <= 'F') v += c - 'A' + 10;
		else if (c >= 'a' && c <= 'f') v += c - 'a' + 10;
	}
	return v;
}

static void MmEnsureInsDat()
{
	if (s_insReady) return;
	s_insReady = 1;
	BYTE* raw = NULL;
	int n = 0;
	if (!MmLoadResDat(IDR_INSERTION_DAT, &raw, &n))
		MmLoadFileDat(L"res\\insertion.dat", &raw, &n);
	if (!raw || n <= 0) return;
	int i = 0;
	MmInsT* cur = NULL;
	while (i < n && s_insTabN < INS_TMAX) {
		char line[256];
		int L = 0;
		while (i < n && raw[i] != '\n' && L < 255) {
			if (raw[i] != '\r') line[L++] = (char)raw[i];
			++i;
		}
		if (i < n && raw[i] == '\n') ++i;
		line[L] = 0;
		char* p = line;
		while (*p == ' ' || *p == '\t') ++p;
		if (*p == 0 || *p == '#') continue;
		if (p[0] == 'T' && (p[1] == ' ' || p[1] == '\t')) {
			++p;
			while (*p == ' ' || *p == '\t') ++p;
			if (!*p) continue;
			const BYTE fam = (BYTE)*p++;
			while (*p == ' ' || *p == '\t') ++p;
			const int msb = MmInsHex2(p);
			while (*p && *p != ' ' && *p != '\t') ++p;
			while (*p == ' ' || *p == '\t') ++p;
			const int lsb = MmInsHex2(p);
			while (*p && *p != ' ' && *p != '\t') ++p;
			while (*p == ' ' || *p == '\t') ++p;
			cur = &s_insTab[s_insTabN++];
			memset(cur, 0, sizeof(*cur));
			cur->fam = fam;
			cur->msb = (BYTE)msb;
			cur->lsb = (BYTE)lsb;
			strncpy_s(cur->name, p, _TRUNCATE);
			continue;
		}
		if (p[0] == 'P' && (p[1] == ' ' || p[1] == '\t') && cur && cur->np < INS_PMAX) {
			++p;
			while (*p == ' ' || *p == '\t') ++p;
			int pn = 0;
			while (*p >= '0' && *p <= '9') { pn = pn * 10 + (*p - '0'); ++p; }
			while (*p == ' ' || *p == '\t') ++p;
			MmInsP& pp = cur->p[cur->np++];
			pp.n = (BYTE)pn;
			int ni = 0;
			while (*p && *p != ' ' && *p != '\t' && ni < 15) pp.name[ni++] = *p++;
			pp.name[ni] = 0;
			while (*p == ' ' || *p == '\t') ++p;
			pp.kind = (BYTE)(*p ? *p++ : 'u');
			while (*p == ' ' || *p == '\t') ++p;
			strncpy_s(pp.extra, p, _TRUNCATE);
		}
	}
	delete[] raw;
}

static const MmInsT* MmInsFind(int fam, int msb, int lsb)
{
	MmEnsureInsDat();
	msb &= 127;
	lsb &= 127;
	const MmInsT* base = NULL;
	for (int i = 0; i < s_insTabN; ++i) {
		if (s_insTab[i].fam != (BYTE)fam) continue;
		if (s_insTab[i].msb != (BYTE)msb) continue;
		if (s_insTab[i].lsb == (BYTE)lsb) return &s_insTab[i];
		if (s_insTab[i].lsb == 0) base = &s_insTab[i];
	}
	if (fam == 'G' && lsb == 0) {
		for (int i = 0; i < s_insTabN; ++i) {
			if (s_insTab[i].fam == '8' && s_insTab[i].msb == (BYTE)msb)
				return &s_insTab[i];
		}
	}
	return base;
}

static int MmInsNameFromDat(int fam, int msb, int lsb, wchar_t* out, int outN)
{
	const MmInsT* t = MmInsFind(fam, msb, lsb);
	if (!t || !t->name[0]) return 0;
	MultiByteToWideChar(CP_UTF8, 0, t->name, -1, out, outN);
	if (outN > 0) out[outN - 1] = 0;
	return out[0] != 0;
}

static int MmInsAppend(wchar_t* dst, int dstN, int used, const wchar_t* add)
{
	if (!add || !add[0] || used >= dstN - 2) return used;
	if (used > 0) {
		if (used < dstN - 1) dst[used++] = L' ';
		if (used < dstN - 1) dst[used++] = L' ';
	}
	const int left = dstN - used;
	if (left <= 1) return used;
	wcsncpy_s(dst + used, left, add, _TRUNCATE);
	return used + (int)wcslen(dst + used);
}

static int MmInsFmtP(const MmInsP& pp, int v, wchar_t* out, int outN)
{
	out[0] = 0;
	v &= 127;
	wchar_t nm[20];
	MultiByteToWideChar(CP_UTF8, 0, pp.name, -1, nm, 20);
	nm[19] = 0;
	const char k = (char)pp.kind;
	if (k == 'g') {
		if (v == 64) return 0;
		_snwprintf_s(out, outN, _TRUNCATE, L"%s %+dB", nm, v - 64);
		return 1;
	}
	if (k == 'p') {
		if (v == 64) return 0;
		if (v < 64) _snwprintf_s(out, outN, _TRUNCATE, L"%s L%d", nm, 64 - v);
		else _snwprintf_s(out, outN, _TRUNCATE, L"%s R%d", nm, v - 64);
		return 1;
	}
	if (k == 'b') {
		if (v == 0) return 0;
		_snwprintf_s(out, outN, _TRUNCATE, L"%s On", nm);
		return 1;
	}
	if (k == 'q') {
		static const wchar_t* qv[] = { L"0.5", L"1.0", L"2.0", L"4.0", L"9.0" };
		if (v <= 0) return 0;
		if (v > 4) v = 4;
		_snwprintf_s(out, outN, _TRUNCATE, L"%s Q%s", nm, qv[v]);
		return 1;
	}
	if (k == 'f') {
		static const wchar_t* hz[] = {
			L"200Hz", L"250Hz", L"315Hz", L"400Hz", L"500Hz", L"630Hz", L"800Hz",
			L"1.0k", L"1.3k", L"1.6k", L"2.0k", L"2.5k", L"3.2k", L"4.0k", L"5.0k", L"6.3k", L"8.0k"
		};
		if (v <= 0) return 0;
		if (v >= (int)(sizeof(hz) / sizeof(hz[0]))) v = (int)(sizeof(hz) / sizeof(hz[0])) - 1;
		_snwprintf_s(out, outN, _TRUNCATE, L"%s %s", nm, hz[v]);
		return 1;
	}
	if (k == 'e') {
		if (v == 0) return 0;
		const char* s = pp.extra;
		int idx = 0;
		while (*s && idx < v) {
			if (*s == ',') ++idx;
			++s;
		}
		if (!*s || idx != v) return 0;
		char tok[40];
		int t = 0;
		while (*s && *s != ',' && t < 39) tok[t++] = *s++;
		tok[t] = 0;
		wchar_t ev[40];
		MultiByteToWideChar(CP_UTF8, 0, tok, -1, ev, 40);
		ev[39] = 0;
		_snwprintf_s(out, outN, _TRUNCATE, L"%s %s", nm, ev);
		return 1;
	}
	int hide127 = (pp.extra[0] == '1');
	if (hide127) {
		if (v == 127) return 0;
	} else if (v == 0) return 0;
	_snwprintf_s(out, outN, _TRUNCATE, L"%s %d", nm, v);
	return 1;
}

static UINT MmReadBE(const BYTE* p, int n)
{
	UINT v = 0;
	for (int i = 0; i < n; ++i) v = (v << 8) | p[i];
	return v;
}

static BOOL MmReadVar(const BYTE*& q, const BYTE* end, unsigned& out)
{
	out = 0;
	for (int i = 0; i < 4; ++i) {
		if (q >= end) return FALSE;
		BYTE b = *q++;
		out = (out << 7) | (b & 0x7f);
		if (!(b & 0x80)) return TRUE;
	}
	return FALSE;
}

static bool MmCmpEvStable(const CMidiMonitorDlg::MmEv& a, const CMidiMonitorDlg::MmEv& b)
{
	if (a.tick != b.tick)
		return a.tick < b.tick;
	const int ca = (int)(a.msg & 15), cb = (int)(b.msg & 15);
	if (ca != cb)
		return ca < cb;
	return false;
}

static int MmGs1xToPart(int x)
{
	x &= 0x0f;
	if (x == 0) return 9;
	if (x <= 9) return x - 1;
	return x;
}

static int MmGsDt1Hdr(const BYTE* d, int n)
{
	if (!d || n < 10 || d[0] != 0xf0 || d[4] != 0x12) return 0;
	if (d[1] == 0x41 && d[3] == 0x42) return 1; // Roland GS / SC-8850 / SD
	if (d[1] == 0x42 && d[3] == 0x42) return 1; // Korg NS5R/NX5R GS 互換
	return 0;
}

static int MmXgBassHz(int v)
{
	static const int t[] = { 32, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000 };
	if (v < 0 || v > 15) return -1;
	return t[v];
}

static int MmXgTrebleHz(int v)
{
	static const int t[] = { 500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000 };
	if (v < 0 || v > 15) return -1;
	return t[v];
}

static BOOL MmLoadResDat(int id, BYTE** out, int* outN)
{
	*out = NULL;
	*outN = 0;
	HINSTANCE hi = AfxGetResourceHandle();
	HRSRC hr = FindResource(hi, MAKEINTRESOURCE(id), RT_RCDATA);
	if (!hr) return FALSE;
	HGLOBAL hg = LoadResource(hi, hr);
	if (!hg) return FALSE;
	DWORD sz = SizeofResource(hi, hr);
	const BYTE* p = (const BYTE*)LockResource(hg);
	if (!p || sz < 20) return FALSE;
	BYTE* buf = new BYTE[sz];
	memcpy(buf, p, sz);
	*out = buf;
	*outN = (int)sz;
	return TRUE;
}

static BOOL MmLoadFileDat(const wchar_t* path, BYTE** out, int* outN)
{
	*out = NULL;
	*outN = 0;
	HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE) return FALSE;
	DWORD sz = GetFileSize(f, NULL), got = 0;
	if (sz < 20 || sz > 8 * 1024 * 1024) { CloseHandle(f); return FALSE; }
	BYTE* buf = new BYTE[sz];
	if (!ReadFile(f, buf, sz, &got, NULL) || got != sz) {
		CloseHandle(f); delete[] buf; return FALSE;
	}
	CloseHandle(f);
	*out = buf;
	*outN = (int)sz;
	return TRUE;
}

static void MmEnsureDat()
{
	if (s_datTried) return;
	s_datTried = 1;
	if (!MmLoadResDat(IDR_SASAMI_GS, &s_gsDat, &s_gsBytes))
		MmLoadFileDat(L"C:\\Windows\\SASAMI_GS.DAT", &s_gsDat, &s_gsBytes);
	if (!MmLoadResDat(IDR_SASAMI_XG, &s_xgDat, &s_xgBytes))
		MmLoadFileDat(L"C:\\Windows\\SASAMI_XG.DAT", &s_xgDat, &s_xgBytes);
	if (!MmLoadResDat(IDR_SASAMI_EX, &s_exDat, &s_exBytes))
		MmLoadFileDat(L"C:\\Windows\\SASAMI_EX.DAT", &s_exDat, &s_exBytes);
	s_exLblReady = 0;
}

static void MmSjisToW(const char* src, int srcN, wchar_t* dst, int dstN)
{
	if (!dst || dstN <= 0) return;
	dst[0] = 0;
	if (!src || srcN <= 0) return;
	char tmp[32];
	int n = srcN;
	if (n > 31) n = 31;
	memcpy(tmp, src, n);
	tmp[n] = 0;
	MultiByteToWideChar(932, 0, tmp, -1, dst, dstN);
	dst[dstN - 1] = 0;
}

static BOOL MmLookupGs(int mapId, int bank, int pc, wchar_t* out, int outN)
{
	if (!s_gsDat || s_gsBytes < 20) return FALSE;
	const int rec = s_gsBytes / 20;
	const int npass = (mapId == 0 || mapId == 6) ? 1 : 3;
	for (int pass = 0; pass < npass; ++pass) {
		int wantMap = mapId, wantBank = bank, wantPc = pc;
		if (pass == 1) { wantMap = 4; wantBank = bank; }
		if (pass == 2) { wantMap = 4; wantBank = 0; }
		for (int i = 0; i < rec; ++i) {
			const BYTE* r = s_gsDat + i * 20;
			if (r[0] == (BYTE)wantMap && r[1] == (BYTE)wantBank && r[2] == (BYTE)wantPc) {
				MmSjisToW((const char*)(r + 3), 17, out, outN);
				return out[0] != 0;
			}
		}
	}
	return FALSE;
}

static BOOL MmLookupEx(int mapId, int bank, int pc, wchar_t* out, int outN)
{
	if (!s_exDat || s_exBytes < 20) return FALSE;
	const int rec = s_exBytes / 20;
	for (int i = 0; i < rec; ++i) {
		const BYTE* r = s_exDat + i * 20;
		if (r[0] == (BYTE)mapId && r[1] == (BYTE)bank && r[2] == (BYTE)pc) {
			MmSjisToW((const char*)(r + 3), 17, out, outN);
			return out[0] != 0;
		}
	}
	return FALSE;
}

static int MmExBank(int mapId, int msb, int lsb, int isDrum)
{
	if (mapId == 9 || mapId == 14) {
		if (isDrum || msb == 120) return 120;
		return lsb;
	}
	if (mapId == 11) {
		if (isDrum || msb == 122) return 122;
		return lsb;
	}
	if (mapId == 13) return lsb;
	return msb;
}

// XG ドラム: リズムバンク MSB 127、または Multi Part の Part Mode≠Normal。
// ch10 固定にしない（mildgrov の A11=#Apogee Kit など追加キット用）。
static int MmXgPartIsDrum(int bankMsb, int partMode)
{
	return (bankMsb == 127 || partMode != 0) ? 1 : 0;
}

static int MmMsbLooksKorg(int m)
{
	return m == 56 || m == 61 || m == 62 || m == 82 || m == 83 ||
		(m >= 88 && m <= 91);
}

static const wchar_t* MmExMapLabel(int mapId)
{
	if (mapId < 0 || mapId >= 32) return L"ETCmap";
	if (!s_exLblReady) {
		s_exLblReady = 1;
		memset(s_exLbl, 0, sizeof(s_exLbl));
		if (s_exDat && s_exBytes >= 20) {
			const int rec = s_exBytes / 20;
			for (int i = 0; i < rec; ++i) {
				const BYTE* r = s_exDat + i * 20;
				if (r[0] == 255 && r[1] < 32)
					MmSjisToW((const char*)(r + 3), 17, s_exLbl[r[1]], 12);
			}
		}
	}
	if (s_exLbl[mapId][0]) return s_exLbl[mapId];
	return L"ETCmap";
}

static BOOL MmLookupXgOk(int msb, int lsb, int pc, wchar_t* out, int outN)
{
	if (!s_xgDat || s_xgBytes < 20) return FALSE;
	const int rec = s_xgBytes / 20;
	for (int pass = 0; pass < 2; ++pass) {
		const BYTE wantM = (BYTE)((pass == 0) ? msb : 0);
		const BYTE wantL = (BYTE)((pass == 0) ? lsb : 0);
		const BYTE wantP = (BYTE)pc;
		for (int i = 0; i < rec; ++i) {
			const BYTE* r = s_xgDat + i * 20;
			if (r[0] == wantM && r[1] == wantL && r[2] == wantP) {
				MmSjisToW((const char*)(r + 3), 17, out, outN);
				return out[0] != 0;
			}
		}
	}
	return FALSE;
}

static const wchar_t* MmEffName(int mode, int kind, int type)
{
	if (mode == 2) {
		if (kind == 0) {
			if (type >= 0 && type < (int)(sizeof(kXgRev) / sizeof(kXgRev[0]))) return kXgRev[type];
			return L"Reverb";
		}
		if (kind == 1) {
			if (type >= 0 && type < (int)(sizeof(kXgCho) / sizeof(kXgCho[0]))) return kXgCho[type];
			return L"Chorus";
		}
		if (type >= 0 && type < (int)(sizeof(kXgVar) / sizeof(kXgVar[0]))) return kXgVar[type];
		return L"Variation";
	}
	if (kind == 0) {
		if (type >= 0 && type < (int)(sizeof(kGsRev) / sizeof(kGsRev[0]))) return kGsRev[type];
		return L"Room 1";
	}
	if (kind == 1) {
		if (type >= 0 && type < (int)(sizeof(kGsCho) / sizeof(kGsCho[0]))) return kGsCho[type];
		return L"Chorus 1";
	}
	if (kind == 2) {
		if (type >= 0 && type < (int)(sizeof(kGsDly) / sizeof(kGsDly[0]))) return kGsDly[type];
		return L"Delay 1";
	}
	return L"OFF";
}

static int MmForceMapId(int mapForce)
{
	if (mapForce >= 10 && mapForce <= 19)
		return mapForce - 1;
	switch (mapForce) {
	case 3: return 1;
	case 4: return 2;
	case 5: return 3;
	case 6: return 4;
	case 7: return 5;
	case 8: return 6;
	case 9: return 8;
	default: return -1;
	}
}

static void MmResolveLookup(int mapForce, int sysMode, int partMap, int partMsb, int* isXg, int* mapId, int* bankMsb)
{
	int xg = (sysMode == 2);
	int mid = partMap;
	int msb = partMsb;
	if (mapForce >= 10 && mapForce <= 19) {
		xg = 0;
		mid = mapForce - 1;
	} else switch (mapForce) {
	case 1: xg = 0; break;
	case 2: xg = 1; break;
	case 3: xg = 0; mid = 1; break;
	case 4: xg = 0; mid = 2; break;
	case 5: xg = 0; mid = 3; break;
	case 6: xg = 0; mid = 4; break;
	case 7: xg = 0; mid = 5; break;
	case 8: xg = 0; mid = 6; break;
	case 9: xg = 0; mid = 8; msb = 127; break;
	default:
		if (mid == 8) msb = 127;
		break;
	}
	if (isXg) *isXg = xg;
	if (mapId) *mapId = mid;
	if (bankMsb) *bankMsb = msb;
}

static const wchar_t* MmMapLabel(int sysMode, int mapId, int bankMsb, int bankLsb)
{
	if (sysMode == 2) return L"XGmap";
	if (mapId >= 9) return MmExMapLabel(mapId);
	if (sysMode != 2 && bankMsb == 127) return L"LAmap";
	if (sysMode != 2 && bankMsb == 126) return L"CMmap";
	if (mapId == 8) return L"LAmap";
	if (sysMode == 0 || mapId == 5) return L"GMmap";
	if (mapId == 6) return L"SDmap";
	if (bankLsb == 1 || mapId == 1) return L"55map";
	if (bankLsb == 3 || mapId == 3) return L"88Promap";
	if (bankLsb == 4 || mapId == 4) return L"8820map";
	if (bankLsb == 2 || mapId == 2) return L"88map";
	if (mapId == 0) return L"GSmap";
	return L"88map";
}

static void MmKeyName(int sf, int mi, wchar_t* out, int n)
{
	static const wchar_t* maj[] = { L"Cb", L"Gb", L"Db", L"Ab", L"Eb", L"Bb", L"F", L"C", L"G", L"D", L"A", L"E", L"B", L"F#", L"C#" };
	static const wchar_t* minn[] = { L"Ab", L"Eb", L"Bb", L"F", L"C", L"G", L"D", L"A", L"E", L"B", L"F#", L"C#", L"G#", L"D#", L"A#" };
	int i = sf + 7;
	if (i < 0) i = 0;
	if (i > 14) i = 14;
	if (mi)
		_snwprintf_s(out, n, _TRUNCATE, L"%s minor", minn[i]);
	else
		_snwprintf_s(out, n, _TRUNCATE, L"%s Major", maj[i]);
}

class CMmHelpDlg : public CDialog
{
public:
	enum { IDD = IDD_MM_HELP };
	explicit CMmHelpDlg(CWnd* pParent = nullptr) : CDialog(IDD, pParent) {}
protected:
	virtual BOOL OnInitDialog();
	virtual void PostNcDestroy();
	virtual void OnOK();
	virtual void OnCancel();
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnClose();
	DECLARE_MESSAGE_MAP()
};

static CMmHelpDlg* g_mmHelpDlg = nullptr;

BEGIN_MESSAGE_MAP(CMmHelpDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CLOSE()
END_MESSAGE_MAP()

BOOL CMmHelpDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	CCC_ApplyWindowIconFromTemplate(this, IDD);
	ModifyStyleEx(0, WS_EX_DLGMODALFRAME, SWP_FRAMECHANGED);
	SetWindowText(LL14(
		L"MIDIモニタ操作ガイド", L"MIDI Monitor Guide", L"Guide moniteur MIDI", L"Guida monitor MIDI",
		L"Guia de monitor MIDI", L"MIDI 모니터 가이드", L"MIDI监视器指南", L"دليل مراقب MIDI",
		L"Руководство MIDI-монитора", L"MIDI-Monitor-Anleitung", L"Guia do monitor MIDI", L"MIDI-monitorhandleiding",
		L"Przewodnik monitora MIDI", L"MIDI izleyici kilavuzu"));
	if (CWnd* pOk = GetDlgItem(IDOK))
		pOk->SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
			L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	return TRUE;
}
void CMmHelpDlg::OnOK() { DestroyWindow(); }
void CMmHelpDlg::OnCancel() { DestroyWindow(); }
void CMmHelpDlg::OnClose() { DestroyWindow(); }
void CMmHelpDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	if (g_mmHelpDlg == this) g_mmHelpDlg = nullptr;
	delete this;
}
BOOL CMmHelpDlg::OnEraseBkgnd(CDC* pDC)
{
	CRect rc; GetClientRect(&rc);
	pDC->FillSolidRect(rc, RGB(248, 248, 252));
	return TRUE;
}
void CMmHelpDlg::OnPaint()
{
	CPaintDC pdc(this);
	CCC_GdiHelpPaint hp;
	if (!CCC_GdiHelpBeginPaint(this, pdc, hp))
		return;
	CDC& dc = hp.mem;
	CRect rc = hp.rc;
	dc.SetBkMode(TRANSPARENT);
	CFont* baseFont = GetFont();
	CFont boldFont;
	{
		LOGFONT lf = {};
		if (baseFont && baseFont->GetSafeHandle())
			baseFont->GetLogFont(&lf);
		else {
			NONCLIENTMETRICS ncm = {};
			ncm.cbSize = sizeof(ncm);
			::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
			lf = ncm.lfMessageFont;
		}
		lf.lfWeight = FW_BOLD;
		boldFont.CreateFontIndirect(&lf);
	}
	CFont* oldFont = dc.SelectObject(baseFont);
	TEXTMETRIC tm{};
	dc.GetTextMetrics(&tm);
	const int lh = max(14, tm.tmHeight + tm.tmExternalLeading + 1);
	const int titleLh = lh + 2;
	auto title = [&](int x, int y, LPCTSTR t) {
		CFont* prev = dc.SelectObject(&boldFont);
		dc.SetTextColor(RGB(72, 48, 120));
		dc.TextOut(x, y, t);
		dc.SelectObject(prev);
	};
	auto body = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(52, 52, 68));
		dc.TextOut(x, y, t);
	};
	int y = 6;
	const int L = 10;
	title(L, y, LL14(L"MIDIモニタ操作ガイド", L"MIDI Monitor — Guide", L"Moniteur MIDI — Guide", L"Monitor MIDI — Guida",
		L"Monitor MIDI — Guía", L"MIDI 모니터 — 가이드", L"MIDI监视器 — 指南", L"مراقب MIDI — دليل",
		L"MIDI-монитор — руководство", L"MIDI-Monitor — Anleitung", L"Monitor MIDI — Guia", L"MIDI-monitor — Gids",
		L"Monitor MIDI — przewodnik", L"MIDI izleyici — kilavuz"));
	y += titleLh;
	body(L, y, LL14(
		L"再生中の SMF(.mid/.midi/.kar/.rmi) を32パート(A01-A16 / B01-B16)で監視します。音声推定ではなくファイルのMIDI事象そのものです。",
		L"Watches the playing SMF (.mid/.midi/.kar/.rmi) as 32 parts (A01-A16 / B01-B16). This is the file's MIDI events, not audio pitch detection.",
		L"Surveille le SMF en lecture (32 parties A01-A16 / B01-B16). Ce sont les evenements MIDI du fichier, pas une detection audio.",
		L"Controlla lo SMF in riproduzione (32 parti A01-A16 / B01-B16). Sono gli eventi MIDI del file, non il rilevamento audio.",
		L"Vigila el SMF en reproduccion (32 partes A01-A16 / B01-B16). Son los eventos MIDI del archivo, no deteccion de audio.",
		L"재생 중인 SMF를 32파트(A01-A16 / B01-B16)로 감시합니다. 오디오 추정가 아니라 파일의 MIDI 이벤트입니다.",
		L"监视正在播放的 SMF，共 32 声部（A01-A16 / B01-B16）。这是文件里的 MIDI 事件，不是音频音高检测。",
		L"يراقب ملف SMF أثناء التشغيل كـ 32 جزءاً. هذه أحداث MIDI من الملف وليست كشفاً صوتياً.",
		L"Следит за SMF во время воспроизведения (32 партии A01-A16 / B01-B16). Это MIDI-события файла, не оценка по аудио.",
		L"Ueberwacht das spielende SMF in 32 Parts (A01-A16 / B01-B16). Das sind MIDI-Ereignisse der Datei, keine Audioerkennung.",
		L"Monitora o SMF em reproducao em 32 partes (A01-A16 / B01-B16). Sao os eventos MIDI do arquivo, nao deteccao de audio.",
		L"Bewaakt het spelende SMF in 32 partijen (A01-A16 / B01-B16). Dit zijn MIDI-events uit het bestand, geen audiodetectie.",
		L"Nadzoruje odtwarzane SMF w 32 partiach (A01-A16 / B01-B16). To zdarzenia MIDI z pliku, nie detekcja audio.",
		L"Calan SMF'yi 32 part (A01-A16 / B01-B16) olarak izler. Ses tahmini degil, dosyanin MIDI olaylaridir."));
	y += lh + 4;
	title(L, y, LL14(L"表示", L"View", L"Affichage", L"Vista", L"Vista", L"표시", L"显示", L"العرض", L"Вид", L"Ansicht", L"Vista", L"Weergave", L"Widok", L"Gorunum"));
	y += titleLh;
	body(L, y, LL14(
		L"・通常(2D) …… ヘッダ(BPM/拍子/小節・拍・tick/リバーブ等)と32行のチャンネル表。パートの間に薄い横線。右端はミニ鍵盤。",
		L"· Normal (2D) …… Header (BPM/meter/bar-beat-tick/reverb…) and a 32-row channel table. Thin lines between parts. Mini keyboard on the right.",
		L"· Normal (2D) …… En-tete (BPM/mesure/mesure-temps-tick/reverb…) et tableau 32 canaux. Traits fins entre les parties. Mini clavier a droite.",
		L"· Normale (2D) …… Intestazione (BPM/misura/battuta-tick/reverb…) e tabella 32 canali. Linee sottili tra le parti. Mini tastiera a destra.",
		L"· Normal (2D) …… Cabecera (BPM/compas/compas-pulso-tick/reverb…) y tabla de 32 canales. Lineas finas entre partes. Mini teclado a la derecha.",
		L"· 일반(2D) …… 헤더(BPM/박자/마디·박·tick/리버브 등)와 32행 채널 표. 파트 사이에 얇은 가로선. 오른쪽은 미니 건반.",
		L"· 普通(2D) …… 页眉（BPM/拍号/小节·拍·tick/混响等）和 32 行通道表。声部之间有细横线。右侧是迷你键盘。",
		L"· عادي (2D) …… رأس (BPM/ميزان/ مازورة-نبض-tick/صدى…) وجدول 32 قناة. خطوط رفيعة بين الأجزاء. لوحة مفاتيح صغيرة يميناً.",
		L"· Обычный (2D) …… Заголовок (BPM/размер/такт-доля-tick/реверб…) и таблица 32 каналов. Тонкие линии между партиями. Мини-клавиатура справа.",
		L"· Normal (2D) …… Kopf (BPM/Takt/Takt-Schlag-Tick/Reverb…) und 32-Zeilen-Kanaltabelle. Duenne Linien zwischen den Parts. Mini-Tastatur rechts.",
		L"· Normal (2D) …… Cabecalho (BPM/compasso/compasso-pulso-tick/reverb…) e tabela de 32 canais. Linhas finas entre as partes. Mini teclado a direita.",
		L"· Normaal (2D) …… Kop (BPM/maatsoort/maat-tel-tick/reverb…) en 32-rijige kanaaltabel. Dunne lijnen tussen partijen. Mini-toetsenbord rechts.",
		L"· Zwykly (2D) …… Naglowek (BPM/metrum/takt-uderzenie-tick/poglos…) i tabela 32 kanalow. Cienkie linie miedzy partiami. Mini klawiatura po prawej.",
		L"· Normal (2D) …… Baslik (BPM/olcu/olcu-vurus-tick/reverb…) ve 32 satirlik kanal tablosu. Partlar arasinda ince cizgiler. Sagda mini klavye."));
	y += lh;
	y = CCC_GdiHelpDrawSoftDemoPair(dc, L, y, rc.Width() - L * 2, min(140, max(112, rc.Height() / 5)),
		CCC_HELPDEMO_KMIDIMON);
	title(L, y, LL14(L"簡易3D(Soft3D)", L"Soft 3D", L"3D simplifie", L"3D semplificato", L"3D simple", L"간이 3D", L"简易3D", L"Soft 3D",
		L"Простой 3D", L"Soft 3D", L"Soft 3D", L"Eenvoudig 3D", L"Soft 3D", L"Soft 3B"));
	y += titleLh;
	body(L, y, LL14(
		L"・右クリック「表示モード」→ 簡易3D。32パートのレベルを箱として CPU 描画(OpenGL/Direct3D 不使用)。",
		L"· Right-click View → Soft 3D. 32 part levels as boxes, CPU-only (no OpenGL/Direct3D).",
		L"· Clic droit Affichage → Soft 3D. 32 niveaux en boites, CPU seul (pas OpenGL/Direct3D).",
		L"· Destro Vista → Soft 3D. 32 livelli a scatole, solo CPU (niente OpenGL/Direct3D).",
		L"· Clic der. Vista → Soft 3D. 32 niveles en cajas, solo CPU (sin OpenGL/Direct3D).",
		L"· 우클릭 「표시 모드」→ 간이 3D. 32파트 레벨을 상자로 CPU만 그립니다.",
		L"· 右键「显示模式」→ 简易3D。用盒子表示 32 声部电平，仅 CPU（无 OpenGL/Direct3D）。",
		L"· يمين ← العرض ← Soft 3D. مستويات 32 جزءاً كصناديق، معالج فقط.",
		L"· ПКМ «Вид» → Soft 3D. 32 уровня партий ящиками, только CPU.",
		L"· Rechtsklick Ansicht → Soft 3D. 32 Part-Pegel als Boxen, nur CPU.",
		L"· Direito Exibir → Soft 3D. 32 niveis em caixas, so CPU.",
		L"· Rechtsklik Weergave → Soft 3D. 32 partijniveaus als dozen, alleen CPU.",
		L"· PPM Widok → Soft 3D. 32 poziomy partii jako pudelka, tylko CPU.",
		L"· Sag tik Gorunum → Soft 3B. 32 part seviyesi kutu olarak, yalnizca CPU."));
	y += lh;
	body(L, y, LL14(
		L"・ドラッグで回転、ホイールでズーム、0 で視点リセット。重いときは 2D に戻せます。",
		L"· Drag to orbit, wheel to zoom, 0 to reset view. Switch back to 2D if it feels heavy.",
		L"· Glisser = orbite, molette = zoom, 0 = reset. Revenez en 2D si c'est lourd.",
		L"· Trascina = orbita, rotella = zoom, 0 = reset. Torna al 2D se e pesante.",
		L"· Arrastrar = orbita, rueda = zoom, 0 = restablecer. Vuelva a 2D si va lento.",
		L"· 드래그=회전, 휠=줌, 0=리셋. 무거우면 2D로 되돌리세요.",
		L"· 拖动旋转、滚轮缩放、0 重置。觉得卡就切回 2D。",
		L"· سحب للدوران، عجلة للتكبير، 0 لإعادة الضبط. عد إلى 2D إذا ثقل.",
		L"· Перетаскивание — поворот, колесо — масштаб, 0 — сброс. Вернитесь в 2D, если тяжело.",
		L"· Ziehen = drehen, Rad = Zoom, 0 = Reset. Zurueck zu 2D, wenn es zäh ist.",
		L"· Arrastar = orbitar, roda = zoom, 0 = redefinir. Volte ao 2D se pesar.",
		L"· Sleepen = draaien, wiel = zoom, 0 = reset. Terug naar 2D als het zwaar is.",
		L"· Przeciagnij = obrot, kolo = zoom, 0 = reset. Wroc do 2D, gdy ciezko.",
		L"· Surukle = don, tekerlek = zoom, 0 = sifirla. Agirsa 2B'ye donun."));
	y += lh + 4;
	title(L, y, LL14(L"列の見方", L"Columns", L"Colonnes", L"Colonne", L"Columnas", L"열", L"列", L"الأعمدة", L"Столбцы", L"Spalten", L"Colunas", L"Kolommen", L"Kolumny", L"Sutunlar"));
	y += titleLh;
	body(L, y, LL14(
		L"・PC# BNK Map …… プログラム、バンク、GS/XG/ETC マップ。Instrument は DAT(SASAMI_GS/XG/EX)。LA は bank127=LAmap、GM2 は GM2map。列名は下の数値・バーと同じ横位置です。Lev〜Var は Vibrato の Rat/Dpt と同じく2行にして、1つおきに上下へ書きます。",
		L"· PC# BNK Map …… Program, bank, GS/XG/ETC map. Names from SASAMI_GS/XG/EX DAT. LA=bank127 LAmap, GM2=GM2map. Column titles sit on the data. Lev-Var use two rows like Vibrato Rat/Dpt, alternating up and down.",
		L"· PC# BNK Map …… Programme, banque, carte GS/XG/ETC. Noms SASAMI_GS/XG/EX. Titres sur les donnees. Lev-Var sur deux lignes en alternance, comme Rat/Dpt de Vibrato.",
		L"· PC# BNK Map …… Programma, bank, mappa GS/XG/ETC. Nomi SASAMI_GS/XG/EX. Titoli sui dati. Lev-Var su due righe alternate, come Rat/Dpt di Vibrato.",
		L"· PC# BNK Map …… Programa, banco, mapa GS/XG/ETC. Nombres SASAMI_GS/XG/EX. Titulos sobre los datos. Lev-Var en dos filas alternas, como Rat/Dpt de Vibrato.",
		L"· PC# BNK Map …… 프로그램, 뱅크, GS/XG/ETC 맵. 이름은 SASAMI_GS/XG/EX. 열 제목은 데이터와 같은 가로 위치. Lev-Var는 Vibrato의 Rat/Dpt처럼 두 줄로 위아래 교차합니다.",
		L"· PC# BNK Map …… 音色号、库、GS/XG/ETC。名称来自 SASAMI_GS/XG/EX。列名与下方数据对齐。Lev-Var 像 Vibrato 的 Rat/Dpt 一样分两行上下交错。",
		L"· PC# BNK Map …… برنامج، بنك، خريطة GS/XG/ETC. العناوين فوق البيانات؛ Lev-Var على سطرين بالتناوب مثل Rat/Dpt في Vibrato.",
		L"· PC# BNK Map …… Программа, банк, карта GS/XG/ETC. Заголовки над данными; Lev-Var в две строки через одну, как Rat/Dpt у Vibrato.",
		L"· PC# BNK Map …… Programm, Bank, GS/XG/ETC-Map. Titel sitzen auf den Daten; Lev-Var zweizeilig im Wechsel, wie Rat/Dpt bei Vibrato.",
		L"· PC# BNK Map …… Programa, banco, mapa GS/XG/ETC. Titulos sobre os dados; Lev-Var em duas linhas alternadas, como Rat/Dpt do Vibrato.",
		L"· PC# BNK Map …… Programma, bank, GS/XG/ETC-map. Titels op de data; Lev-Var op twee regels om-en-om, zoals Rat/Dpt bij Vibrato.",
		L"· PC# BNK Map …… Program, bank, mapa GS/XG/ETC. Tytuly nad danymi; Lev-Var w dwoch wierszach na przemian, jak Rat/Dpt w Vibrato.",
		L"· PC# BNK Map …… Program, banka, GS/XG/ETC haritasi. Basliklar verinin ustunde; Lev-Var Vibrato Rat/Dpt gibi iki satira nobetlese yazilir."));
	y += lh;
	body(L, y, LL14(
		L"・Lev は発音。Vol/Pan/Exp 等は動いているとき明るく、初期値のままなら暗くなります。ヘッダ DS は DirectSound 音量（ドラッグで MP の DS 音量と同期。主音量とは別）。Notes は実発音数、暗いバーが MAX（少しして減衰）。DRUM はドラムパートのヒット。その右はいまの小節/総小節（001/051）、拍/1小節の拍（1/4→2/4…）、小節内tick/1小節のtick（0000/1920）で、再生位置に合わせて動きます。",
		L"· Lev is level. Vol/Pan/Exp light up while moving, stay dim at defaults. Header DS is DirectSound volume (drag syncs with the MP DS slider, not master volume). Notes is held polyphony; the dim bar is MAX (holds, then decays). DRUM lights on drum hits. To its right: bar/total (001/051), beat/beats-in-bar (1/4→2/4…), tick-in-bar/ticks-per-bar (0000/1920), live with playback.",
		L"· Lev = niveau. Vol/Pan/Exp s'allument en mouvement. DS d'en-tete = volume DirectSound (glisser = sync MP DS, pas le volume maitre). Notes = polyphonie reelle; barre sombre = MAX (puis baisse). DRUM = hits batterie. A droite : mesure/total, temps/mesure, tick/base.",
		L"· Lev e il livello. Vol/Pan/Exp si illuminano se si muovono. DS in testa = volume DirectSound (trascina = sync MP DS, non il master). Notes = polifonia reale; barra scura = MAX (poi scende). DRUM = colpi batteria. A destra: battuta/totale, tempo/misura, tick/base.",
		L"· Lev es el nivel. Vol/Pan/Exp se iluminan al moverse. DS de cabecera = volumen DirectSound (arrastrar = sync MP DS, no el master). Notes = polifonia real; barra oscura = MAX (luego baja). DRUM = golpes de bateria. A la derecha: compas/total, pulso/compas, tick/base.",
		L"· Lev는 레벨. Vol/Pan/Exp는 움직일 때 밝고, 기본값이면 어둡습니다. 헤더 DS는 DirectSound 음량(드래그하면 MP DS와 동기, 주음량과 별개). Notes는 실제 발음 수, 어두운 바는 MAX(잠시 후 감쇠). DRUM은 드럼 타격. 오른쪽은 마디/전체·박/박자·tick/분해능.",
		L"· Lev 是电平。Vol/Pan/Exp 在变化时发亮。页眉 DS 是 DirectSound 音量（拖动与 MP 的 DS 同步，与主音量分开）。Notes 是实际发音数，暗条是 MAX（稍后衰减）。DRUM 表示鼓组敲击。右侧是小节/总数、拍/拍号、tick/时基。",
		L"· Lev هو المستوى. DS في الرأس = مستوى DirectSound (يسحب مع DS في MP لا الصوت الرئيسي). Notes = تعدد الأصوات الفعلي؛ الشريط الداكن = MAX ثم ينخفض. DRUM لضربات الطبل. يمينه: مازورة/المجموع، نبض/ميزان، tick/الأساس.",
		L"· Lev — уровень. DS в шапке — громкость DirectSound (синхрон с DS на MP, не мастер). Notes — реальная полифония; тёмная полоса — MAX (потом спадает). DRUM — удары барабанов. Справа: такт/всего, доля/размер, tick/база.",
		L"· Lev ist Pegel. Kopf-DS ist DirectSound-Lautstaerke (sync mit MP-DS, nicht Master). Notes ist echte Polyphonie; die dunkle Leiste ist MAX (dann Abfall). DRUM = Drum-Hits. Rechts: Takt/gesamt, Schlag/Taktart, Tick/Timebase.",
		L"· Lev e o nivel. DS do cabecalho e o volume DirectSound (sync com DS do MP, nao o master). Notes e a polifonia real; a barra escura e MAX (depois cai). DRUM = batidas de bateria. A direita: compasso/total, pulso/compasso, tick/base.",
		L"· Lev is niveau. Kop-DS is DirectSound-volume (sync met MP-DS, niet master). Notes is echte polyfonie; de donkere balk is MAX (daarna verval). DRUM = drumhits. Rechts: maat/totaal, tel/maatsoort, tick/timebase.",
		L"· Lev to poziom. DS w naglowku to glosnosc DirectSound (sync z DS w MP, nie master). Notes to rzeczywista polifonia; ciemny pasek to MAX (potem opada). DRUM = uderzenia perkusji. Po prawej: takt/razem, uderzenie/metrum, tick/baza.",
		L"· Lev seviyedir. Baslik DS DirectSound sesidir (MP DS ile eslenir, ana ses degil). Notes gercek polifonidir; koyu cubuk MAX'tir (sonra duser). DRUM davul vurusudur. Saginda: olcu/toplam, vurus/olcu, tick/zaman tabani."));
	y += lh;
	body(L, y, LL14(
		L"・INSERTION は ON/OFF ではなく、曲の SysEx から引いた正式名称です（Thru、Distortion、Overdrive、Stereo-EQ など）。XG は Insertion 1–4（03 00–03。MU100 の 03 10 は INS2）。GS は 88Pro EFX（40 03）。Variation をインサーション接続したときも出します。かかっているパートはインスト名の左に橙の◆を出します。B16 の下に接続パートと効いているパラメータ（Drive、EQ ゲイン、Rev/Cho 送り）を文字で出します。INS3/4 がある曲は4行になります。",
		L"· INSERTION shows official names from the song SysEx (Thru, Distortion, Overdrive, Stereo-EQ…), not ON/OFF. XG: Insertion 1–4 (03 00–03; MU100 03 10 = INS2). GS: 88Pro EFX (40 03). Variation used as insertion is included. Parts using it get an orange ◆ left of the instrument name. Rows under B16 list connected parts and active parameters. Songs with INS3/4 get four rows.",
		L"· INSERTION affiche le nom officiel (Thru, Distortion…), pas ON/OFF. XG Insertion 1–4, Variation en insertion, ou EFX GS.",
		L"· INSERTION mostra il nome ufficiale (Thru, Distortion…), non ON/OFF. XG Insertion 1–4, Variation in insertion, o EFX GS.",
		L"· INSERTION muestra el nombre oficial (Thru, Distortion…), no ON/OFF. Insercion XG 1–4, Variation como insercion, o EFX GS.",
		L"· INSERTION은 ON/OFF가 아니라 효과의 정식 이름입니다. XG 인서션 1–4, 인서션 연결된 Variation, 또는 GS EFX.",
		L"· INSERTION 显示正式效果名，不是 ON/OFF。XG 插入 1–4、作插入连接的 Variation，或 GS EFX。",
		L"· INSERTION يعرض اسم المؤثر الرسمي وليس ON/OFF. XG 1–4 أو Variation أو GS EFX.",
		L"· INSERTION показывает официальное имя эффекта, не ON/OFF. XG 1–4, Variation как insertion или GS EFX.",
		L"· INSERTION zeigt den offiziellen Effektnamen, nicht ON/OFF. XG 1–4, Variation als Insertion oder GS-EFX.",
		L"· INSERTION mostra o nome oficial, nao ON/OFF. Insercao XG 1–4, Variation como insercao ou EFX GS.",
		L"· INSERTION toont de officiële effectnaam, niet ON/OFF. XG 1–4, Variation als insertion of GS-EFX.",
		L"· INSERTION pokazuje oficjalna nazwe efektu, nie ON/OFF. XG 1–4, Variation jako insercja lub GS EFX.",
		L"· INSERTION resmi efekt adini gosterir, ON/OFF degil. XG 1–4, insertion bagli Variation veya GS EFX."));
	y += lh;
	body(L, y, LL14(
		L"・SysEx …… GS は Roland DT1（F0 41 dv 42 12）と Korg 互換（F0 42 3n 42 12）を同じに扱います。パート 1x はバンク/PC/受信ch/リズム切替に加え、キーシフト・レベル・パン・Cho/Rev/Delay 送り。2x はビブラート・TVF・TVA。4x 22 は EFX ON。システム 40 01 は Rev/Cho に加え Delay（50）。XG は Multi Part 08（A01–16）と 09（B01–16）のバンク/PC/Part Mode/音量/パン/送り/TVF/EG、EQ 周波数は 0–15 の表のときだけ Hz にします。SD-90 の GS ダンプも同じ DT1。音色名は SASAMI_GS/XG/EX（SD ネイティブバンク MSB、Korg は EX）。",
		L"· SysEx: GS DT1 is Roland F0 41 dv 42 12 and Korg-compatible F0 42 3n 42 12. Part 1x: bank/PC/Rx/rhythm plus key shift, level, pan, Cho/Rev/Delay send. 2x: vibrato, TVF, TVA. 4x 22: EFX ON. System 40 01 includes Delay (50) as well as Rev/Cho. XG Multi Part 08 (A01–16) and 09 (B01–16): bank/PC/Part Mode/volume/pan/sends/TVF/EG. EQ frequency becomes Hz only for index 0–15. SD-90 GS dumps use the same DT1. Names from SASAMI_GS/XG/EX (SD native MSB, Korg EX).",
		L"· SysEx GS DT1 (Roland / Korg 42 42 12). Mixer, TVF/TVA, Delay, EFX. XG 08/09 et Insertion 1–4. SD-90 en GS = meme DT1.",
		L"· SysEx GS DT1 (Roland / Korg). Mixer, TVF/TVA, Delay, EFX. XG 08/09 e Insertion 1–4. SD-90 GS = stesso DT1.",
		L"· SysEx GS DT1 (Roland / Korg). Mixer, TVF/TVA, Delay, EFX. XG 08/09 e Insertion 1–4. SD-90 GS = el mismo DT1.",
		L"· SysEx GS DT1(Roland/Korg). 믹서·TVF/TVA·Delay·EFX. XG 08/09와 Insertion 1–4. SD-90 GS는 같은 DT1.",
		L"· SysEx：GS DT1（Roland／Korg 兼容）。混音、TVF/TVA、Delay、EFX。XG 08/09 与 Insertion 1–4。SD-90 的 GS 转储同 DT1。",
		L"· SysEx GS DT1 (Roland / Korg). Mixer و TVF و Delay و EFX. XG 08/09 و Insertion 1–4.",
		L"· SysEx GS DT1 (Roland / Korg). Микшер, TVF/TVA, Delay, EFX. XG 08/09 и Insertion 1–4. SD-90 GS — тот же DT1.",
		L"· SysEx GS-DT1 (Roland / Korg). Mixer, TVF/TVA, Delay, EFX. XG 08/09 und Insertion 1–4. SD-90 GS = dasselbe DT1.",
		L"· SysEx GS DT1 (Roland / Korg). Mixer, TVF/TVA, Delay, EFX. XG 08/09 e Insertion 1–4. SD-90 GS = o mesmo DT1.",
		L"· SysEx GS DT1 (Roland / Korg). Mixer, TVF/TVA, Delay, EFX. XG 08/09 en Insertion 1–4. SD-90 GS = dezelfde DT1.",
		L"· SysEx GS DT1 (Roland / Korg). Mixer, TVF/TVA, Delay, EFX. XG 08/09 i Insertion 1–4. SD-90 GS = to samo DT1.",
		L"· SysEx GS DT1 (Roland / Korg). Mixer, TVF/TVA, Delay, EFX. XG 08/09 ve Insertion 1–4. SD-90 GS ayni DT1."));
	y += lh;
	body(L, y, LL14(
		L"・VST 再生では GS リセットのあと A10/B10 へ USE FOR RHYTHM を送り、XG では ch10 にドラムバンク(MSB127)を送ります。曲の SysEx が後から上書きします。右クリック「操作」→「ドラムモード」でも再送できます。88map の ORCHESTRA はキット名のことがあります。",
		L"· For VST playback, GS Reset is followed by USE FOR RHYTHM on A10/B10. XG sends drum bank MSB 127 on ch10. Later song SysEx can override. Right-click Actions → Drum mode to resend. ORCHESTRA on 88map can be a kit name.",
		L"· En VST, apres GS Reset, USE FOR RHYTHM part sur A10/B10. XG envoie la banque batterie MSB 127 sur ch10. Le SysEx du morceau peut ecraser. Clic droit Actions → Mode batterie. ORCHESTRA en 88map peut etre un kit.",
		L"· In VST, dopo GS Reset si invia USE FOR RHYTHM su A10/B10. XG manda banco batteria MSB 127 su ch10. Il SysEx del brano puo sovrascrivere. Tasto destro Azioni → Modo batteria. ORCHESTRA in 88map puo essere un kit.",
		L"· En VST, tras GS Reset se envia USE FOR RHYTHM a A10/B10. XG manda banco de bateria MSB 127 en ch10. El SysEx de la pieza puede pisarlo. Clic derecho Acciones → Modo bateria. ORCHESTRA en 88map puede ser un kit.",
		L"· VST 재생에서는 GS 리셋 뒤 A10/B10에 USE FOR RHYTHM을 보내고, XG는 ch10에 드럼 뱅크(MSB127)를 보냅니다. 곡 SysEx가 나중에 덮어씁니다. 오른쪽 클릭 조작 → 드럼 모드. 88map의 ORCHESTRA는 키트 이름일 수 있습니다.",
		L"· VST 播放时，GS 复位后会向 A10/B10 发送 USE FOR RHYTHM；XG 则向 ch10 发送鼓组库 MSB127。乐曲 SysEx 可覆盖。右键「操作」→「鼓组模式」。88map 的 ORCHESTRA 可能是鼓组名。",
		L"· في VST بعد GS Reset يُرسل USE FOR RHYTHM إلى A10/B10. XG يرسل بنك الطبل MSB 127 على القناة 10. يمكن لـ SysEx المقطوعة أن يستبدل ذلك. زر أيمن إجراءات → وضع الطبل.",
		L"· В VST после GS Reset на A10/B10 уходит USE FOR RHYTHM. В XG на ch10 — банк ударных MSB 127. SysEx пьесы может переписать. ПКМ Действия → Режим ударных. ORCHESTRA в 88map может быть именем набора.",
		L"· Bei VST folgt auf GS Reset USE FOR RHYTHM auf A10/B10. XG sendet Drum-Bank MSB 127 auf Kanal 10. Song-SysEx kann das ueberschreiben. Rechtsklick Aktionen → Drum-Modus. ORCHESTRA in 88map kann ein Kitname sein.",
		L"· No VST, apos GS Reset envia-se USE FOR RHYTHM em A10/B10. XG envia banco de bateria MSB 127 no ch10. O SysEx da peca pode sobrescrever. Clique direito Acoes → Modo bateria. ORCHESTRA no 88map pode ser nome de kit.",
		L"· Bij VST volgt na GS Reset USE FOR RHYTHM op A10/B10. XG stuurt drumbank MSB 127 op ch10. Song-SysEx kan dit overschrijven. Rechtsklik Acties → Drummodus. ORCHESTRA op 88map kan een kitnaam zijn.",
		L"· W VST po GS Reset na A10/B10 idzie USE FOR RHYTHM. XG wysyla bank perkusji MSB 127 na ch10. SysEx utworu moze to nadpisac. PPM Akcje → Tryb perkusji. ORCHESTRA na 88map moze byc nazwa zestawu.",
		L"· VST'de GS Reset sonrasi A10/B10'a USE FOR RHYTHM gider. XG ch10'a davul bankasi MSB 127 gonderir. Parca SysEx ezebilir. Sag tik Islemler → Davul modu. 88map'te ORCHESTRA kit adi olabilir."));
	y += lh;
	body(L, y, LL14(
		L"・行の色 …… 使っているパートは白と少し暗い白を交互。ドラムパートは行全体を暖色にします（XG はバンク MSB 127 や Part Mode。ch10 以外の追加キットも含む）。ノート/CC/SysEx が一度も無いチャンネルは灰色。ミニ鍵盤をクリックして鳴らすと交互色（ドラムなら暖色）に戻ります。",
		L"· Row colors: used parts alternate white / off-white. Drum parts tint the whole row warm (XG: bank MSB 127 / Part Mode, not only ch10). Channels with no note/CC/SysEx stay gray. Click a mini-keyboard to play and the row returns to the stripe (or drum tint).",
		L"· Couleurs : parties utilisees en blanc / blanc casse alterne. Batterie = ligne chaude. Sans note/CC/SysEx = gris. Clic sur le mini clavier : la bande (ou teinte batterie) revient.",
		L"· Colori: parti usate in bianco / bianco sporco alternato. Batteria = riga calda. Senza nota/CC/SysEx = grigio. Clic sulla mini tastiera: torna la riga (o tinta batteria).",
		L"· Colores: partes usadas en blanco / blanco roto. Bateria = fila calida. Sin nota/CC/SysEx = gris. Clic en el mini teclado: vuelve la franja (o tinte de bateria).",
		L"· 행 색: 쓰는 파트는 흰색/조금 어두운 흰색 교차. 드럼은 행 전체 난색. 노트/CC/SysEx가 없는 채널은 회색. 미니 건반을 클릭해 치면 교차색(드럼이면 난색)으로 돌아갑니다.",
		L"· 行色：有数据的声部白/略暗白交替。鼓组整行暖色。从未有音符/CC/SysEx 的通道为灰。点击迷你键盘发音后恢复条纹（鼓组则为暖色）。",
		L"· الألوان: الأجزاء المستخدمة أبيض/أبيض باهت بالتناوب. الطبل يلوّن الصف بالكامل. بلا نغمة/CC/SysEx = رمادي. النقر على اللوحة يعيد الشريط.",
		L"· Цвета: занятые партии — белый / чуть серый через ряд. Ударные — вся строка тёплая. Без нот/CC/SysEx — серый. Клик по мини-клавиатуре возвращает полосу (или тёплый тон).",
		L"· Zeilen: genutzte Parts weiss / leicht abgesetzt. Drums faerben die ganze Zeile warm. Ohne Note/CC/SysEx grau. Klick auf die Mini-Tastatur stellt Streifen (oder Drum-Ton) wieder her.",
		L"· Cores: partes usadas em branco / branco sujo. Bateria = linha quente. Sem nota/CC/SysEx = cinza. Clique no mini teclado devolve a faixa (ou tom de bateria).",
		L"· Rijen: gebruikte partijen wit / iets donkerder wit. Drums kleuren de hele rij warm. Geen noot/CC/SysEx = grijs. Klik op het minitoetsenbord herstelt de streep (of drumtint).",
		L"· Kolory: uzywane partie bialy / lekko przygaszony. Perkusja barwi caly wiersz. Bez nuty/CC/SysEx = szary. Klik w mini klawiature wraca pas (lub cieply odcien).",
		L"· Satir renkleri: kullanilan partlar beyaz / biraz koyu beyaz. Davul tum satiri sicak boyar. Nota/CC/SysEx yoksa gri. Mini klavyeye tiklayinca serit (davulsa sicak ton) doner."));
	y += lh;
	body(L, y, LL14(
		L"・変化の光 …… A01–B16 は発音中だけ緑に点き、離すと消えます。PC# / BNK / Map は行の交互色のままです。音色が変わるとインスト欄が琥珀に点きます。EFX/インサーションが乗っているパートはインスト名の左が橙の◆です。Lev/Vol などのバーはフェードしません。",
		L"· Change glow: A01–B16 light green while sounding, then fade. PC# / BNK / Map keep the row stripe. An instrument change ambers that cell. Parts with EFX/insertion show an orange ◆ left of the name. Meter bars do not fade.",
		L"· Lueur : A01–B16 vert pendant le son, puis fondu. PC# / BNK / Map gardent le zebrage. Changement de timbre = ambre. Les barres ne fondent pas.",
		L"· Bagliore: A01–B16 verde mentre suona, poi sfuma. PC# / BNK / Map restano a strisce. Cambio timbro = ambra. Le barre non sfumano.",
		L"· Brillo: A01–B16 verde al sonar, luego se apaga. PC# / BNK / Map siguen la raya. Cambio de timbre = ambar. Las barras no se desvanecen.",
		L"· 변화 빛: A01–B16은 발음 중 초록으로 켜졌다가 꺼집니다. PC# / BNK / Map는 교차색 그대로. 음색이 바뀌면 인스트 칸이 호박색. Lev/Vol 등 바는 페이드하지 않습니다.",
		L"· 变化光：A01–B16 发音时亮绿，松开后淡出。PC# / BNK / Map 保持行条纹。音色变化时乐器栏呈琥珀。Lev/Vol 等条不淡出。",
		L"· توهج: A01–B16 أخضر أثناء العزف ثم يخفت. PC# / BNK / Map تبقى مخططة. تغيير الآلة يُamber الخلية. الأشرطة لا تتلاشى.",
		L"· Подсветка: A01–B16 зелёные, пока звучат, потом гаснут. PC# / BNK / Map остаются полосатыми. Смена тембра — янтарь. Полоски не затухают.",
		L"· Leuchten: A01–B16 gruen beim Klingen, dann aus. PC# / BNK / Map bleiben gestreift. Klangwechsel faerbt die Instrumentzelle amber. Balken blenden nicht aus.",
		L"· Brilho: A01–B16 verde ao soar, depois some. PC# / BNK / Map mantem a faixa. Troca de timbre = ambar. As barras nao esmaecem.",
		L"· Gloed: A01–B16 groen tijdens klank, daarna uit. PC# / BNK / Map blijven gestreept. Klankwissel maakt het instrumentvak amber. Balken faden niet.",
		L"· Swiatlo: A01–B16 zielone gdy graja, potem gasna. PC# / BNK / Map zostaja w pasach. Zmiana barwy = bursztyn. Paski nie wygasaja.",
		L"· Pariltı: A01–B16 seslenirken yesil, sonra solar. PC# / BNK / Map seritte kalir. Timbir degisince enstruman hucresi kehribar. Cubuklar solmaz."));
	y += lh;
	body(L, y, LL14(
		L"・先頭のノートが乗るまでは SysEx/CC は遅れません（頭のダンプを表示遅延に引きずらない）。一度ノートが乗ったら、SysEx/CC も聞こえる位置に合わせて遅らせます。追いつき中は同じアドレスの SysEx / 同じ CC は最後の値だけ適用します。",
		L"· Until a note appears, SysEx/CC are not delayed (opening dumps are not dragged by display lag). After the first note, SysEx/CC lag with the audible position. While catching up, only the last SysEx per address and last CC of each type are applied.",
		L"· Tant qu'aucune note n'apparait, SysEx/CC ne sont pas retardees. Apres la premiere note, elles suivent la position audible. En rattrapage, seul le dernier SysEx par adresse et le dernier CC de chaque type s'appliquent.",
		L"· Finche non compare una nota, SysEx/CC non sono ritardati. Dopo la prima nota seguono la posizione udibile. In recupero si applica solo l'ultimo SysEx per indirizzo e l'ultimo CC di ciascun tipo.",
		L"· Hasta que aparezca una nota, SysEx/CC no se retrasan. Tras la primera nota siguen la posicion audible. Al ponerse al dia solo se aplica el ultimo SysEx por direccion y el ultimo CC de cada tipo.",
		L"· 노트가 올라오기 전에는 SysEx/CC를 늦추지 않습니다. 첫 노트가 올라온 뒤에는 들리는 위치에 맞춰 늦춥니다. 따라잡을 때는 주소마다 마지막 SysEx, CC 종류마다 마지막 값만 적용합니다.",
		L"· 在出现音符之前，SysEx/CC 不延迟（开头的转储不被显示延迟拖住）。出现第一个音符后，SysEx/CC 再按可听位置延迟。追赶时每个地址只保留最后一条 SysEx、每种 CC 只保留最后一次。",
		L"· إلى أن تظهر نغمة لا تُؤخَّر SysEx/CC. بعد أول نغمة تُؤخَّر مع الموضع المسموع. عند اللحاق يُطبَّق آخر SysEx لكل عنوان وآخر CC لكل نوع.",
		L"· Пока нет ноты, SysEx/CC не задерживаются. После первой ноты они следуют слышимой позиции. При догоне — только последний SysEx на адрес и последний CC каждого типа.",
		L"· Bis eine Note erscheint, werden SysEx/CC nicht verzoegert. Nach der ersten Note folgen sie der hoerbaren Position. Beim Aufholen nur das letzte SysEx pro Adresse und das letzte CC je Typ.",
		L"· Ate aparecer uma nota, SysEx/CC nao atrasam. Depois da primeira nota seguem a posicao audivel. No alcance so vale o ultimo SysEx por endereco e o ultimo CC de cada tipo.",
		L"· Tot er een noot verschijnt, worden SysEx/CC niet vertraagd. Na de eerste noot volgen ze de hoorbare positie. Bij inhalen alleen de laatste SysEx per adres en de laatste CC per type.",
		L"· Dopoki nie pojawi sie nuta, SysEx/CC nie sa opozniane. Po pierwszej nucie ida za slyszalna pozycja. Przy doganianiu tylko ostatni SysEx na adres i ostatni CC danego typu.",
		L"· Nota gorene kadar SysEx/CC gecikmez. Ilk nota geldikten sonra duyulan konuma gore gecikir. Yetisirken her adresin son SysEx'i ve her CC turunun son degeri uygulanir."));
	y += lh;
	body(L, y, LL14(
		L"・描画は 16ms ごとを基本に、絵が変わった行だけ塗り直します。キューが空いているときはノートと tick を追加で追従するので、短いドラムも点灯し、小節・拍・tick が滑らかです（VSTホストの鍵盤も同じ）。ほかのアプリが忙しいときは譲ります。バーは1ピクセル動いたときだけ、ミニ鍵盤は押している音が変わったときだけです。32パートが同時に鳴っても全面は描き直しません。",
		L"· Drawing is every 16ms at base; only rows whose picture changed are repainted. When the queue is idle, notes and ticks are synced extra so short drums still light and bar/beat/tick stay smooth (same for the VST host keyboard). Extra work yields if other apps are busy. A bar when it moves by a pixel, the mini keyboard when held notes change. Even with all 32 parts sounding, the table is not fully repainted.",
		L"· Dessin toutes les 16 ms de base : seules les lignes changeantes. File d'attente libre = notes et ticks en plus (batterie courte, curseur fluide, clavier hote VST). On cede si d'autres applis sont occupees.",
		L"· Disegno ogni 16 ms di base: solo le righe cambiate. Coda libera = note e tick extra (batteria breve, cursore fluido, tastiera host VST). Cede se altre app sono occupate.",
		L"· Dibujo cada 16 ms de base: solo filas que cambian. Cola libre = notas y ticks extra (caja corta, cursor fluido, teclado host VST). Cede si otras apps estan ocupadas.",
		L"· 기본은 16ms마다 바뀐 행만 그립니다. 큐가 비면 노트와 tick을 추가로 맞춰 짧은 드럼도 켜지고 소절/박/tick이 부드럽습니다(VST 호스트 건반도 같음). 다른 앱이 바쁘면 양보합니다.",
		L"· 绘制以 16ms 为基准，只重绘变化的行。队列空闲时额外同步音符和 tick，短鼓也能点亮，小节/拍/tick 更顺（VST 宿主键盘同样）。其他程序忙时让出。",
		L"· الرسم كل 16ms أساساً: الصفوف المتغيرة فقط. عند فراغ الطابور تُزامَن النغمات والـ tick إضافياً. إن انشغلت برامج أخرى نترك المعالج.",
		L"· Рисование каждые 16 мс: только изменившиеся строки. При простое очереди дополнительно синхронизируются ноты и tick (короткие ударные, плавный курсор, клавиатура VST-хоста). Если другие программы заняты — уступаем.",
		L"· Zeichnen alle 16 ms: nur geaenderte Zeilen. Ist die Queue leer, werden Noten und Ticks extra nachgezogen (kurze Drums, fluessiger Cursor, VST-Host-Tastatur). Andere Apps beschaeftigt: nachgeben.",
		L"· Desenho a cada 16 ms: so linhas que mudam. Fila ociosa = notas e ticks extra (bateria curta, cursor suave, teclado do host VST). Outros apps ocupados: cede.",
		L"· Tekenen elke 16 ms: alleen gewijzigde rijen. Lege wachtrij = extra noten en ticks (korte drums, soepele cursor, VST-hosttoetsen). Andere apps druk: wijken.",
		L"· Rysowanie co 16 ms: tylko zmienione wiersze. Pusta kolejka = dodatkowe nuty i ticki (krotkie bębny, plynny kursor, klawiatura hosta VST). Inne aplikacje zajete: ustepujemy.",
		L"· Temel 16ms: sadece degisen satirlar. Kuyruk bossa nota ve tick ek senkron (kisa davul, akici imlec, VST host klavye). Diger uygulamalar mesgulse yol verir."));
	y += lh;
	body(L, y, LL14(
		L"・Vol/Pan/Exp/Rev/Crs/Var はドラッグまたはホイールで送出（ダブルクリックで初期値）。PC# はホイールでプログラム変更。右端のミニ鍵盤はクリックで発音。曲の CC より約2.5秒優先します。",
		L"· Drag or wheel Vol/Pan/Exp/Rev/Crs/Var to send CC (double-click resets). Wheel PC# for program change. Click the mini keyboard to play. Your edits hold about 2.5s over the song CC.",
		L"· Glisser / molette Vol/Pan/Exp/Rev/Crs/Var envoie le CC (double-clic = defaut). Molette sur PC# = programme. Mini clavier cliquable. Vos reglages priment ~2,5 s sur le SMF.",
		L"· Trascina o rotella su Vol/Pan/Exp/Rev/Crs/Var per inviare CC (doppio clic = default). Rotella su PC# = programma. Mini tastiera cliccabile. Le modifiche restano ~2,5 s sul SMF.",
		L"· Arrastre o rueda Vol/Pan/Exp/Rev/Crs/Var para enviar CC (doble clic = valor por defecto). Rueda en PC# = programa. Mini teclado clicable. Sus ediciones pesan ~2,5 s sobre el SMF.",
		L"· Vol/Pan/Exp/Rev/Crs/Var는 드래그 또는 휠로 전송(더블클릭=초기값). PC#는 휠로 프로그램 변경. 미니 건반 클릭으로 연주. 곡 CC보다 약 2.5초 우선.",
		L"· 拖动或滚轮 Vol/Pan/Exp/Rev/Crs/Var 发送 CC（双击恢复默认）。滚轮 PC# 改音色。点击迷你键盘发音。约 2.5 秒内优先于乐曲 CC。",
		L"· اسحب أو عجلة Vol/Pan/Exp لإرسال CC. عجلة PC# لتغيير البرنامج. انقر على لوحة المفاتيح الصغيرة للعزف. تعديلاتك تسبق ملف SMF نحو 2.5 ث.",
		L"· Перетаскивание/колесо Vol/Pan/Exp/Rev/Crs/Var шлёт CC (двойной щелчок — сброс). Колесо на PC# — программа. Мини-клавиатура играет. Правки держатся ~2,5 с над CC файла.",
		L"· Ziehen oder Rad auf Vol/Pan/Exp/Rev/Crs/Var sendet CC (Doppelklick = Default). Rad auf PC# = Programm. Mini-Tastatur ist spielbar. Edits gelten ~2,5 s vor Song-CC.",
		L"· Arrastar ou roda em Vol/Pan/Exp/Rev/Crs/Var envia CC (duplo clique = padrao). Roda em PC# = programa. Mini teclado toca. Edicoes valem ~2,5 s sobre o CC do SMF.",
		L"· Slepen of wiel op Vol/Pan/Exp/Rev/Crs/Var stuurt CC (dubbelklik = default). Wiel op PC# = programma. Mini-toetsenbord speelt. Edits gaan ~2,5 s voor SMF-CC.",
		L"· Przeciagnij lub kolo na Vol/Pan/Exp/Rev/Crs/Var wysyla CC (dwuklik = domyslne). Kolo na PC# = program. Mini klawiatura gra. Edycje trzymaja ~2,5 s nad CC z SMF.",
		L"· Vol/Pan/Exp/Rev/Crs/Var surukleme veya tekerlekle CC gonderir (cift tik = varsayilan). PC# tekerlegi program degistirir. Mini klavye calinir. Duzenlemeler SMF CC'den ~2,5 sn once gelir."));
	y += lh;
	body(L, y, LL14(
		L"・VST MIDI 再生中は、いま鳴っている VST（または MIDI マッパー）と同じ経路で鍵盤・CC が出ます。KPI プラグイン再生中はプラグインへ差し込めないため、同じ「MIDI出力」端末（未指定なら MIDI マッパー）から出します。",
		L"· During VST MIDI playback, keys and CC go through the same VST (or MIDI Mapper) as the song. During KPI plugin playback the plugin has no MIDI in, so notes go to the MIDI-out device (Mapper if none is set).",
		L"· En lecture VST MIDI, clavier et CC passent par le meme VST (ou MIDI Mapper) que le morceau. En lecture KPI, le plugin n'a pas d'entree MIDI : les notes vont vers la sortie MIDI (Mapper si aucune).",
		L"· In riproduzione VST MIDI, tasti e CC usano lo stesso VST (o MIDI Mapper) del brano. Con plugin KPI non c'e ingresso MIDI: le note vanno al dispositivo MIDI out (Mapper se non impostato).",
		L"· En VST MIDI, teclas y CC van por el mismo VST (o MIDI Mapper) que la cancion. Con KPI el plugin no tiene entrada MIDI; las notas van al dispositivo MIDI out (Mapper si no hay uno).",
		L"· VST MIDI 재생 중에는 곡과 같은 VST(또는 MIDI 매퍼)로 건반·CC가 나갑니다. KPI 플러그인 재생 중에는 플러그인에 MIDI 입력이 없어, MIDI 출력 장치(미지정 시 매퍼)로 나갑니다.",
		L"· VST MIDI 播放时，琴键和 CC 走与乐曲相同的 VST（或 MIDI 映射器）。KPI 插件播放时插件没有 MIDI 输入，音符从 MIDI 输出设备发出（未指定则为映射器）。",
		L"· أثناء تشغيل VST MIDI تمر المفاتيح وCC عبر نفس الـ VST (أو MIDI Mapper). أثناء KPI لا يملك المكون إضافة مدخل MIDI فتخرج النغمات إلى جهاز MIDI out (Mapper إن لم يُحدد).",
		L"· При VST MIDI клавиши и CC идут в тот же VST (или MIDI Mapper), что и пьеса. У KPI-плагина нет MIDI-входа, поэтому ноты идут на устройство MIDI out (Mapper, если не задано).",
		L"· Bei VST-MIDI gehen Tasten und CC denselben Weg wie das Stueck (VST oder MIDI-Mapper). KPI-Plugins haben keinen MIDI-Eingang; Noten gehen an das MIDI-Out-Geraet (Mapper, wenn keins gesetzt).",
		L"· Em VST MIDI, teclas e CC seguem o mesmo VST (ou MIDI Mapper) da musica. No KPI o plugin nao tem entrada MIDI; as notas vao para o dispositivo MIDI out (Mapper se nenhum).",
		L"· Bij VST MIDI gaan toetsen en CC via dezelfde VST (of MIDI Mapper) als het nummer. KPI-plugins hebben geen MIDI-in; noten gaan naar het MIDI-uit-apparaat (Mapper indien leeg).",
		L"· Przy VST MIDI klawisze i CC ida ta sama droga co utwor (VST lub MIDI Mapper). Wtyczka KPI nie ma wejscia MIDI; nuty ida na urzadzenie MIDI out (Mapper, jesli puste).",
		L"· VST MIDI sirasinda tuslar ve CC sarkiyle ayni VST (veya MIDI Mapper) yolundan cikar. KPI eklentisinin MIDI girisi yoktur; notalar MIDI cikis aygitina gider (aygit yoksa Mapper)."));
	y += lh;
	body(L, y, LL14(
		L"・VSTホストを開いているときは、その鍵盤・MIDI入力がこのモニタに出ます。ミニ鍵盤やスライダーはホストのプラグインへ送られます。右クリック「開く」からホストを出せます。",
		L"· With the VST host open, its keyboard and MIDI in appear here. Mini keys and sliders go to the host's plug-ins. Right-click Open to show the host.",
		L"· Hote VST ouvert : son clavier et MIDI in s'affichent ici. Mini clavier et curseurs vont aux plug-ins. Clic droit Ouvrir pour l'hote.",
		L"· Con host VST aperto, tastiera e MIDI in compaiono qui. Mini tasti e slider vanno ai plug-in. Tasto destro Apri per l'host.",
		L"· Con el host VST abierto, su teclado y MIDI in salen aqui. Mini teclas y deslizadores van a los plug-ins. Clic derecho Abrir para el host.",
		L"· VST 호스트가 열려 있으면 그 건반·MIDI 입력이 여기 나옵니다. 미니 건반과 슬라이더는 호스트 플러그인으로 갑니다. 우클릭 열기로 호스트를 엽니다.",
		L"· 打开 VST 主机时，其键盘和 MIDI 输入会显示在此。迷你键盘和滑块送到主机插件。右键「打开」可打开主机。",
		L"· مع مضيف VST مفتوح تظهر لوحتُه وMIDI هنا. المفاتيح الصغيرة والمنزلقات إلى إضافات المضيف. يمين ← فتح للمضيف.",
		L"· Если хост VST открыт, его клавиатура и MIDI in видны здесь. Мини-клавиши и ползунки идут в плагины хоста. ПКМ «Открыть» — хост.",
		L"· Ist der VST-Host offen, erscheinen Tastatur und MIDI-In hier. Mini-Tasten und Schieber gehen an die Plug-ins. Rechtsklick Oeffnen fuer den Host.",
		L"· Com o host VST aberto, teclado e MIDI in aparecem aqui. Mini teclas e controlos vao aos plug-ins. Clique direito Abrir para o host.",
		L"· Met VST-host open verschijnen toetsenbord en MIDI-in hier. Mini-toetsen en schuiven gaan naar de plug-ins. Rechtsklik Openen voor de host.",
		L"· Gdy host VST jest otwarty, klawiatura i MIDI in sa tu. Mini klawisze i suwaki ida do wtyczek. PPM Otworz dla hosta.",
		L"· VST host aciksa klavyesi ve MIDI girisi burada gorunur. Mini tuslar ve kaydiricilar host eklentilerine gider. Sag tik Ac ile hostu acin."));
	y += lh + 2;
	title(L, y, LL14(L"右クリック", L"Right-click", L"Clic droit", L"Tasto destro", L"Clic derecho", L"우클릭", L"右键", L"زر أيمن", L"ПКМ", L"Rechtsklick", L"Botao direito", L"Rechtsklik", L"PPM", L"Sag tik"));
	y += titleLh;
	body(L, y, LL14(
		L"・表示モード / フリーズ / 常に手前 / 状態をコピー / マップ指定（55/88/LA など） / 他ウィンドウ / このガイド。",
		L"· View mode / Freeze / Always on top / Copy state / Map (55/88/LA…) / Other windows / This guide.",
		L"· Mode d'affichage / Gel / Premier plan / Copier / Carte / Autres fenetres / Ce guide.",
		L"· Modalita / Congela / In primo piano / Copia / Mappa / Altre finestre / Questa guida.",
		L"· Modo / Congelar / Siempre visible / Copiar / Mapa / Otras ventanas / Esta guia.",
		L"· 표시 모드 / 프리즈 / 항상 위 / 상태 복사 / 맵 / 다른 창 / 이 가이드.",
		L"· 显示模式 / 冻结 / 置顶 / 复制状态 / 映射 / 其他窗口 / 本指南。",
		L"· وضع العرض / تجميد / دائماً أعلى / نسخ / خريطة / نوافذ أخرى / هذا الدليل.",
		L"· Режим / Заморозка / Поверх всех / Копия / Карта / Другие окна / Это руководство.",
		L"· Ansicht / Einfrieren / Immer oben / Kopieren / Map / Andere Fenster / Diese Anleitung.",
		L"· Modo / Congelar / Sempre no topo / Copiar / Mapa / Outras janelas / Este guia.",
		L"· Weergave / Bevriezen / Altijd boven / Kopieren / Map / Andere vensters / Deze gids.",
		L"· Widok / Zamroz / Zawsze na wierzchu / Kopiuj / Mapa / Inne okna / Ten przewodnik.",
		L"· Gorunum / Dondur / Her zaman ustte / Kopyala / Harita / Diger pencereler / Bu kilavuz."));
	dc.SelectObject(oldFont);
	CCC_GdiHelpEndPaint(hp);
}

} // namespace

IMPLEMENT_DYNAMIC(CMidiMonitorDlg, CCustomBlurDialogExBase)

CMidiMonitorDlg::CMidiMonitorDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(IDD_MIDIMONITOR, pParent)
	, m_frameOld(nullptr), m_frameW(0), m_frameH(0)
#if CCUSTOM_AERO_SUPPORT
	, m_chromaW(0), m_chromaH(0), m_chromaReady(false)
#endif
	, m_fontDpi(0), m_fontH(0)
	, m_ev(NULL), m_evCount(0), m_evPos(0), m_hadNote(0), m_hearPlayb(-1), m_sx(NULL), m_sxBytes(0)
	, m_division(480), m_sampleRate(44100), m_loopStartSample(0), m_loopEndSample(0), m_lastPlayb(-1)
	, m_usecQn(500000), m_tsNum(4), m_tsDen(4), m_keySf(0), m_keyMin(0), m_transpose(0)
	, m_sysMode(0), m_revType(4), m_choType(2), m_varType(0), m_revPacked(0), m_choPacked(0), m_varPacked(0), m_varConn(1), m_ins1(0), m_ins2(0), m_ins3(0), m_ins4(0), m_dlyType(0)
	, m_noteCount(0), m_masterVol(100)
	, m_notesPeak(0), m_notesPeakHold(0), m_layW(0)
	, m_dragKind(0), m_dragPart(-1), m_playPart(-1), m_playNote(-1)
	, m_viewMode(0), m_mapForce(0), m_gsMapKind(0), m_fileHasXg(0), m_fileHasGm(0), m_fileHasSd(0), m_gs32(0), m_mirrorToB(0)
	, m_tsEvN(0), m_maxTick(0)
	, m_posBar(1), m_posBars(1), m_posBeat(1), m_posTick(0), m_posTpm(1920), m_posNum(4)
	, m_frozen(false), m_alwaysOnTop(false), m_paintDisabled(false)
	, m_rotDragging(false), m_rotDragYaw0(0), m_rotDragPitch0(0), m_soft3dTourUntil(0)
	, m_hoverCol(-1), m_hoverPart(-1)
	, m_layHeadH(0), m_layRowH(0), m_layFootH(0), m_layExtra(0), m_persistAge(0)
	, m_visAcc(0), m_visLastMs(0), m_pbAnchor(0), m_pbQpc(0), m_pbFreq(0), m_idleLastQpc(0)
	, m_drumGlow(0), m_dispBpm(-1)
	, m_dirtyRows(0xFFFFFFFFu), m_rowLive(0), m_nameNeed(0), m_burstApply(0)
	, m_dirtyHead(true), m_fullDraw(true), m_volDragging(false)
{
	m_loadedPath[0] = 0;
	m_sourcePath[0] = 0;
	m_titleBuf[0] = 0;
	m_hoverTip[0] = 0;
	m_volBarRc.SetRectEmpty();
	m_notesBarRc.SetRectEmpty();
	memset(m_part, 0, sizeof(m_part));
	memset(m_show, 0, sizeof(m_show));
	memset(m_plugShown, 0, sizeof(m_plugShown));
	memset(m_gsEfx, 0, sizeof(m_gsEfx));
	m_gsEfxHasLsb = 0;
	m_gsEfxMask = 0;
	memset(m_insBlk, 0, sizeof(m_insBlk));
	memset(m_varBlk, 0, sizeof(m_varBlk));
	m_insLine[0][0] = 0;
	m_insLine[1][0] = 0;
	m_insLine[2][0] = 0;
	m_insLine[3][0] = 0;
	m_showInsLine[0][0] = 0;
	m_showInsLine[1][0] = 0;
	m_showInsLine[2][0] = 0;
	m_showInsLine[3][0] = 0;
	m_showBpm = -1;
	m_showVarPacked = -1;
	m_showVarConn = -1;
	m_showIns3 = -1;
	m_showIns4 = -1;
	m_showDly = -1;
	m_showBar = m_showBars = m_showBeat = m_showTick = m_showTpm = m_showNum = -1;
	m_showTitle[0] = 0;
	memset(m_tsEv, 0, sizeof(m_tsEv));
	memset(m_latchUntil, 0, sizeof(m_latchUntil));
	memset(m_latchMask, 0, sizeof(m_latchMask));
}

CMidiMonitorDlg::~CMidiMonitorDlg()
{
	UnloadMidi();
	ReleasePaintBuffers();
}

void CMidiMonitorDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_MM_HELP, m_help);
}

BEGIN_MESSAGE_MAP(CMidiMonitorDlg, CCustomBlurDialogExBase)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_TIMER()
	ON_WM_SIZE()
	ON_WM_SYSCOMMAND()
	ON_WM_MOVE()
	ON_WM_SHOWWINDOW()
	ON_WM_CLOSE()
	ON_WM_DESTROY()
	ON_WM_CONTEXTMENU()
	ON_BN_CLICKED(IDC_MM_HELP, &CMidiMonitorDlg::OnBnClickedHelp)
	ON_COMMAND(ID_HELP_SHOWSHEET, &CMidiMonitorDlg::OnBnClickedHelp)
	ON_WM_LBUTTONDOWN()
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONUP()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_MOUSEWHEEL()
	ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTW, 0, 0xFFFF, &CMidiMonitorDlg::OnTtnNeedText)
	ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTA, 0, 0xFFFF, &CMidiMonitorDlg::OnTtnNeedText)
END_MESSAGE_MAP()

UINT CMidiMonitorDlg::WindowDpi() const
{
	HWND h = GetSafeHwnd();
	if (!h) return 96;
	typedef UINT(WINAPI* PFN)(HWND);
	static PFN s_fn = NULL;
	static BOOL s_got = FALSE;
	if (!s_got) {
		HMODULE u = ::GetModuleHandleW(L"user32.dll");
		if (u) s_fn = (PFN)::GetProcAddress(u, "GetDpiForWindow");
		s_got = TRUE;
	}
	if (s_fn) {
		UINT d = s_fn(h);
		if (d > 0) return d;
	}
	HDC hdc = ::GetDC(h);
	UINT d = hdc ? (UINT)GetDeviceCaps(hdc, LOGPIXELSX) : 96;
	if (hdc) ::ReleaseDC(h, hdc);
	return d ? d : 96;
}

void CMidiMonitorDlg::ReleasePaintBuffers()
{
	if (m_frameOld && m_frameDC.GetSafeHdc()) {
		m_frameDC.SelectObject(m_frameOld);
		m_frameOld = nullptr;
	}
	if (m_frameBmp.GetSafeHandle())
		m_frameBmp.DeleteObject();
	if (m_frameDC.GetSafeHdc())
		m_frameDC.DeleteDC();
	m_frameW = m_frameH = 0;
	m_fullDraw = true;
#if CCUSTOM_AERO_SUPPORT
	m_chromaCache.Release();
	m_chromaReady = false;
	m_chromaW = m_chromaH = 0;
#endif
}

bool CMidiMonitorDlg::EnsureFrameBuffer(CDC& refDC, int w, int h)
{
	if (w <= 0 || h <= 0) return false;
	if (m_frameDC.GetSafeHdc() && m_frameW == w && m_frameH == h && m_frameBmp.GetSafeHandle())
		return true;
	ReleasePaintBuffers();
	if (!m_frameDC.CreateCompatibleDC(&refDC))
		return false;
	if (!m_frameBmp.CreateCompatibleBitmap(&refDC, w, h)) {
		m_frameDC.DeleteDC();
		return false;
	}
	m_frameOld = m_frameDC.SelectObject(&m_frameBmp);
	m_frameW = w;
	m_frameH = h;
	m_fullDraw = true;
	return true;
}

void CMidiMonitorDlg::InitPartDefaults(int i, BYTE heard)
{
	if (i < 0 || i >= PART_MAX) return;
	Part& p = m_part[i];
	memset(&p, 0, sizeof(p));
	m_plugShown[i][0] = 0;
	p.pc = 0;
	p.vol = 100;
	p.exp = 127;
	p.pan = 64;
	p.rev = 40;
	p.eqLow = 80;
	p.eqHigh = 10000;
	p.mapId = 4;
	p.bankLsb = 0;
	{
		const int fmid = MmForceMapId(m_mapForce);
		if (fmid >= 0) {
			p.mapId = fmid;
			if (m_mapForce == 3) p.bankLsb = 1;
			else if (m_mapForce == 4) p.bankLsb = 2;
			else if (m_mapForce == 5) p.bankLsb = 3;
			else if (m_mapForce == 6) p.bankLsb = 4;
			else if (m_mapForce == 9) { p.bankMsb = 127; p.bankLsb = 0; }
		} else if (!m_fileHasXg && !m_fileHasGm && m_gsMapKind >= 1 && m_gsMapKind <= 4 && m_mapForce != 2) {
			p.bankLsb = m_gsMapKind;
			if (m_gsMapKind == 1) p.mapId = 1;
			else if (m_gsMapKind == 2) p.mapId = 2;
			else if (m_gsMapKind == 3) p.mapId = 3;
			else p.mapId = 4;
		} else if (m_fileHasGm || m_gsMapKind == 5) {
			p.mapId = 5;
			p.bankLsb = 0;
		} else if (m_fileHasSd || m_gsMapKind == 6) {
			p.mapId = 6;
			p.bankLsb = 0;
		} else if (m_gsMapKind == 8) {
			p.mapId = 8;
			p.bankMsb = 127;
			p.bankLsb = 0;
		} else if (m_gsMapKind >= 9) {
			p.mapId = m_gsMapKind;
			p.bankLsb = 0;
		}
	}
	p.xgPartMode = 0;
	p.isDrum = ((i % 16) == 9) ? 1 : 0;
	if (m_sysMode == 2) {
		if ((i % 16) == 9) {
			p.xgPartMode = 1;
			if (p.bankMsb == 0)
				p.bankMsb = 127;
			p.isDrum = 1;
		} else {
			p.isDrum = MmXgPartIsDrum(p.bankMsb, p.xgPartMode);
		}
	}
	p.heard = heard;
	p.rxCh = i % 16;
	p.rxPort = (i >= 16) ? 1 : 0;
	p.lastNote = -1;
	RefreshPartName(p);
}

void CMidiMonitorDlg::ResetPartsBank(int port)
{
	if (port < 0 || port > 1) return;
	const int b0 = port * 16;
	for (int i = b0; i < b0 + 16 && i < PART_MAX; ++i)
		InitPartDefaults(i, m_part[i].heard);
	m_dirtyRows |= (port ? 0xFFFF0000u : 0x0000FFFFu);
	m_fullDraw = true;
	m_nameNeed = 0;
}

void CMidiMonitorDlg::ResetParts()
{
	BYTE heard[PART_MAX];
	for (int i = 0; i < PART_MAX; ++i)
		heard[i] = m_part[i].heard;
	memset(m_part, 0, sizeof(m_part));
	memset(m_plugShown, 0, sizeof(m_plugShown));
	for (int i = 0; i < PART_MAX; ++i)
		InitPartDefaults(i, heard[i]);
	m_usecQn = 500000;
	m_tsNum = 4;
	m_tsDen = 4;
	m_keySf = 0;
	m_keyMin = 0;
	m_transpose = 0;
	m_sysMode = m_fileHasXg ? 2 : ((m_fileHasGm || m_gsMapKind == 5 || m_gsMapKind == 9) ? 0 : 1);
	memset(m_gsEfx, 0, sizeof(m_gsEfx));
	m_gsEfxHasLsb = 0;
	m_gsEfxMask = 0;
	memset(m_insBlk, 0, sizeof(m_insBlk));
	memset(m_varBlk, 0, sizeof(m_varBlk));
	m_ins1 = 0;
	m_ins2 = 0;
	m_ins3 = 0;
	m_ins4 = 0;
	m_dlyType = 0;
	if (m_sysMode == 2) {
		m_revType = 1;
		m_choType = 0x41;
		m_varType = 5;
		m_revPacked = 0x0100;
		m_choPacked = 0x4100;
		m_varPacked = 0x0500;
		m_varConn = 0;
	} else {
		m_revType = 4;
		m_choType = 2;
		m_varType = 0;
		m_revPacked = 0;
		m_choPacked = 0;
		m_varPacked = 0;
		m_varConn = 1;
	}
	m_noteCount = 0;
	m_notesPeak = 0;
	m_notesPeakHold = 0;
	m_dirtyRows = 0xFFFFFFFFu;
	m_dirtyHead = true;
	m_fullDraw = true;
	m_nameNeed = 0;
	m_rowLive = 0;
	m_drumGlow = 0;
	memset(m_latchUntil, 0, sizeof(m_latchUntil));
	memset(m_latchMask, 0, sizeof(m_latchMask));
}

void CMidiMonitorDlg::UnloadMidi()
{
	MmCloseKpiLiveOut();
	if (m_ev) { delete[] m_ev; m_ev = NULL; }
	if (m_sx) { delete[] m_sx; m_sx = NULL; }
	m_evCount = m_evPos = 0;
	m_hadNote = 0;
	m_sxBytes = 0;
	m_loopStartSample = 0;
	m_loopEndSample = 0;
	m_loadedPath[0] = 0;
	m_titleBuf[0] = 0;
	m_gsMapKind = 0;
	m_fileHasXg = 0;
	m_fileHasGm = 0;
	m_fileHasSd = 0;
	m_gs32 = 0;
	m_mirrorToB = 0;
	m_tsEvN = 0;
	m_maxTick = 0;
	UpdatePlayPos();
	for (int i = 0; i < PART_MAX; ++i)
		m_part[i].heard = 0;
}

void CMidiMonitorDlg::LookupToneName(int isXg, int mapId, int bankMsb, int bankLsb, int pc, int isDrum, wchar_t* out, int outN)
{
	SasamiToneLookup(isXg, mapId, bankMsb, bankLsb, pc, isDrum, out, outN);
}

void CMidiMonitorDlg::RefreshPartName(Part& p)
{
	int isXg = 0, mapId = 0, bankMsb = 0;
	MmResolveLookup(m_mapForce, m_sysMode, p.mapId, p.bankMsb, &isXg, &mapId, &bankMsb);
	LookupToneName(isXg, mapId, bankMsb, p.bankLsb, p.pc, p.isDrum, p.name, NAME_CHARS);
}

void CMidiMonitorDlg::ApplyMapForce(int force)
{
	m_mapForce = force;
	const int mid = MmForceMapId(force);
	for (int i = 0; i < PART_MAX; ++i) {
		Part& p = m_part[i];
		if (mid >= 0) {
			p.mapId = mid;
			if (force == 3) p.bankLsb = 1;
			else if (force == 4) p.bankLsb = 2;
			else if (force == 5) p.bankLsb = 3;
			else if (force == 6) p.bankLsb = 4;
			else if (force == 9) { p.bankMsb = 127; p.bankLsb = 0; }
		}
		RefreshPartName(p);
	}
	m_dirtyRows = 0xFFFFFFFFu;
	m_dirtyHead = true;
	m_fullDraw = true;
}

void CMidiMonitorDlg::ReloadCurrentMidi()
{
	m_loadedPath[0] = 0;
	LoadCurrentMidi();
}

void CMidiMonitorDlg::ApplyNrpn(Part& p)
{
	const int d = p.dataMsb;
	if (p.nrpnMsb == 1) {
		if (p.nrpnLsb == 8) p.vibRat = d - 64;
		else if (p.nrpnLsb == 9) p.vibDpt = d - 64;
		else if (p.nrpnLsb == 10) p.vibDly = d - 64;
		else if (p.nrpnLsb == 0x20) p.lpf = d - 64;
		else if (p.nrpnLsb == 0x21) p.rsn = d - 64;
		else if (p.nrpnLsb == 0x24) p.hpf = d - 64;
		else if (p.nrpnLsb == 0x63) p.atk = d - 64;
		else if (p.nrpnLsb == 0x64) p.dcy = d - 64;
		else if (p.nrpnLsb == 0x66) p.rls = d - 64;
	}
}

void CMidiMonitorDlg::ApplyXgPartByte(int part, int a, BYTE v)
{
	if (part < 0 || part >= PART_MAX) return;
	Part& p = m_part[part];
	const int vv = v & 127;
	int chg = 0, inst = 0, vib = 0, filt = 0, env = 0, eq = 0;
	if (a == 0x00) {
		p.bankMsb = vv; chg = 1; inst = 1;
		if (m_sysMode == 2) p.isDrum = MmXgPartIsDrum(p.bankMsb, p.xgPartMode);
	} else if (a == 0x01) {
		p.bankLsb = vv; chg = 1; inst = 1;
	} else if (a == 0x02) {
		p.pc = vv; chg = 1; inst = 1;
	} else if (a == 0x03) {
		p.rxCh = (vv < 16) ? vv : 16; chg = 1; inst = 1;
	} else if (a == 0x06) {
		p.xgPartMode = (BYTE)((vv != 0) ? 1 : 0); chg = 1;
		if (m_sysMode == 2) p.isDrum = MmXgPartIsDrum(p.bankMsb, p.xgPartMode);
	} else if (a == 0x07) {
		p.dt = vv - 0x40; chg = 1;
	} else if (a == 0x09) {
		p.vol = vv; chg = 1;
	} else if (a == 0x0C) {
		p.pan = vv; chg = 1;
	} else if (a == 0x10) {
		p.crs = vv; chg = 1;
	} else if (a == 0x11) {
		p.rev = vv; chg = 1;
	} else if (a == 0x12) {
		p.var = vv; chg = 1;
	} else if (a == 0x13) {
		p.vibRat = vv - 64; chg = 1; vib = 1;
	} else if (a == 0x14) {
		p.vibDpt = vv - 64; chg = 1; vib = 1;
	} else if (a == 0x15) {
		p.vibDly = vv - 64; chg = 1; vib = 1;
	} else if (a == 0x16) {
		p.lpf = vv - 64; chg = 1; filt = 1;
	} else if (a == 0x17) {
		p.rsn = vv - 64; chg = 1; filt = 1;
	} else if (a == 0x18) {
		p.atk = vv - 64; chg = 1; env = 1;
	} else if (a == 0x19) {
		p.dcy = vv - 64; chg = 1; env = 1;
	} else if (a == 0x1A) {
		p.rls = vv - 64; chg = 1; env = 1;
	} else if (a == 0x70 || a == 0x71) {
		chg = 1; eq = 1;
	} else if (a == 0x72 || a == 0x74) {
		const int hz = MmXgBassHz(vv);
		if (hz > 0) p.eqLow = hz;
		chg = 1; eq = 1;
	} else if (a == 0x73 || a == 0x75) {
		const int hz = MmXgTrebleHz(vv);
		if (hz > 0) p.eqHigh = hz;
		chg = 1; eq = 1;
	} else if (a >= 0x70 && a <= 0x77) {
		chg = 1; eq = 1;
	}
	if (!chg) return;
	p.heard = 1;
	if (inst) {
		MmBumpFade(p.fadeInst, m_burstApply);
		m_nameNeed |= (1u << part);
	}
	if (vib) MmBumpFade(p.fadeVib, m_burstApply);
	if (filt) MmBumpFade(p.fadeFilt, m_burstApply);
	if (env) MmBumpFade(p.fadeEnv, m_burstApply);
	if (eq) MmBumpFade(p.fadeEq, m_burstApply);
	m_dirtyRows |= (1u << part);
}

void CMidiMonitorDlg::ApplyGsPartByte(Part& p, int kind, int a, BYTE v, int* changed, int* mark)
{
	const int vv = v & 127;
	if (kind == 1) {
		if (a == 0x00) { p.bankMsb = vv; *changed = 1; *mark = 1; }
		else if (a == 0x01) { p.pc = vv; *changed = 1; *mark = 1; }
		else if (a == 0x02) {
			p.rxCh = (vv >= 0x10) ? 16 : vv;
			*changed = 1; *mark = 1;
		} else if (a == 0x15) {
			p.isDrum = (vv != 0) ? 1 : 0;
			*changed = 1;
			*mark = 1;
		} else if (a == 0x16) {
			p.dt = vv - 0x40; *changed = 1;
		} else if (a == 0x19) {
			p.vol = vv; *changed = 1;
		} 		else if (a == 0x1B) {
			p.pan = vv; *changed = 1;
		} else if (a == 0x22) {
			p.crs = vv; *changed = 1;
		} else if (a == 0x23) {
			p.rev = vv; *changed = 1;
		} else if (a == 0x24 || a == 0x2A) {
			p.var = vv; *changed = 1;
		}
	} else if (kind == 2) {
		const int s = vv - 64;
		if (a == 0x00) { p.vibRat = s; *changed = 1; MmBumpFade(p.fadeVib, m_burstApply); }
		else if (a == 0x01) { p.vibDpt = s; *changed = 1; MmBumpFade(p.fadeVib, m_burstApply); }
		else if (a == 0x02) { p.vibDly = s; *changed = 1; MmBumpFade(p.fadeVib, m_burstApply); }
		else if (a == 0x03) { p.lpf = s; *changed = 1; MmBumpFade(p.fadeFilt, m_burstApply); }
		else if (a == 0x04) { p.rsn = s; *changed = 1; MmBumpFade(p.fadeFilt, m_burstApply); }
		else if (a == 0x05) { p.atk = s; *changed = 1; MmBumpFade(p.fadeEnv, m_burstApply); }
		else if (a == 0x06) { p.dcy = s; *changed = 1; MmBumpFade(p.fadeEnv, m_burstApply); }
		else if (a == 0x07) { p.rls = s; *changed = 1; MmBumpFade(p.fadeEnv, m_burstApply); }
	} else if (kind == 4) {
		if (a == 0x22) {
			p.efxOn = (vv != 0) ? 1 : 0;
			*changed = 1;
			m_dirtyHead = true;
		}
	}
}

void CMidiMonitorDlg::ApplyShort(int port, DWORD msg, BOOL fromUser, BOOL liveExact)
{
	const int st = msg & 0xf0;
	const int ch = msg & 0x0f;
	int part0 = (port * 16) + ch;
	if (part0 < 0) part0 = 0;
	if (part0 >= PART_MAX) part0 = PART_MAX - 1;
	const int scan = (!fromUser && !liveExact && m_gs32) ? 1 : 0; // GS 32ch は Rx Channel で振り分け
	const int iBegin = scan ? 0 : part0;
	const int iEnd = scan ? PART_MAX : (part0 + 1);
	const int d1 = (int)((msg >> 8) & 0xff);
	const int d2 = (int)((msg >> 16) & 0xff);
	for (int part = iBegin; part < iEnd; ++part) {
		if (scan) {
			if (m_part[part].rxCh != ch) continue;
			if (m_part[part].rxCh > 15) continue;
			const int rp = m_part[part].rxPort;
			if (!m_mirrorToB && rp != 2 && rp != port) continue;
		}
		Part& p = m_part[part];
	if (st >= 0x80 && st <= 0xe0) {
		// ホストの XG ドラムバンク注入（ch10 の CC0=127 / CC32=0）は「パート使用」にしない。
		const int xgInj = (!fromUser && st == 0xb0 && (part % 16) == 9
			&& ((d1 == 0 && d2 == 127) || (d1 == 32 && d2 == 0))) ? 1 : 0;
		if (!xgInj && !p.heard) {
			p.heard = 1;
			m_dirtyRows |= (1u << part);
		}
	}
	if (st == 0x90) {
		if (d2 > 0) {
			p.noteOn[d1] = (BYTE)d2;
			p.noteFlash[d1] = 48; // ~48ms。Note Off がすぐ来ても鍵盤が点く
			p.lastNote = d1;
			p.lastVel = d2;
			p.held++;
			if (p.held < 1) p.held = 1;
			const float lv = (float)d2 / 127.f;
			if (lv > p.lev) p.lev = lv;
			MmBumpFade(p.fadeCh, m_burstApply);
			m_dirtyRows |= (1u << part);
			if (p.isDrum) m_drumGlow = 255;
		} else {
			p.noteOn[d1] = 0;
			if (p.held > 0) p.held--;
			m_dirtyRows |= (1u << part);
		}
	} else if (st == 0x80) {
		p.noteOn[d1] = 0;
		if (p.held > 0) p.held--;
		m_dirtyRows |= (1u << part);
	} else if (st == 0xc0) {
		if (fromUser || !IsLatched(part, MM_LATCH_PC)) {
			p.pc = d1 & 127;
			MmBumpFade(p.fadeInst, m_burstApply);
			m_nameNeed |= (1u << part);
			m_dirtyRows |= (1u << part);
		}
	} else if (st == 0xb0) {
		if (d1 == 0) {
			p.bankMsb = d2;
			if (m_sysMode != 2 && m_mapForce != 2 && m_mapForce < 3) {
				if (d2 == 121) p.mapId = 9;
				else if (p.mapId < 9 && VstMidiBankMsbIsSdNative(d2)) p.mapId = 6;
				else if (p.mapId < 9 && MmMsbLooksKorg(d2)) p.mapId = 10;
			}
			if (m_sysMode == 2)
				p.isDrum = MmXgPartIsDrum(p.bankMsb, p.xgPartMode);
			else if (p.bankMsb == 120 && (m_gsMapKind == 9 || p.mapId == 9 || p.mapId == 14))
				p.isDrum = 1;
			else if ((p.mapId == 11 || m_gsMapKind == 11) && p.bankMsb == 122)
				p.isDrum = 1;
			MmBumpFade(p.fadeInst, m_burstApply);
			m_nameNeed |= (1u << part);
			m_dirtyRows |= (1u << part);
		}
		else if (d1 == 32) {
			p.bankLsb = d2;
			if (m_sysMode != 2 && m_mapForce != 2 && m_mapForce < 3 && p.mapId < 9) {
				if (VstMidiBankMsbIsSdNative(p.bankMsb)) p.mapId = 6;
				else if (d2 == 1) p.mapId = 1;
				else if (d2 == 2) p.mapId = 2;
				else if (d2 == 3) p.mapId = 3;
				else if (d2 == 4) p.mapId = 4;
			}
			MmBumpFade(p.fadeInst, m_burstApply);
			m_nameNeed |= (1u << part);
			m_dirtyRows |= (1u << part);
		}
		else if (d1 == 7) { if (fromUser || !IsLatched(part, MM_LATCH_VOL)) { if (p.vol != d2) { p.vol = d2; m_dirtyRows |= (1u << part); } } }
		else if (d1 == 11) { if (fromUser || !IsLatched(part, MM_LATCH_EXP)) { if (p.exp != d2) { p.exp = d2; m_dirtyRows |= (1u << part); } } }
		else if (d1 == 10) { if (fromUser || !IsLatched(part, MM_LATCH_PAN)) { if (p.pan != d2) { p.pan = d2; m_dirtyRows |= (1u << part); } } }
		else if (d1 == 91) { if (fromUser || !IsLatched(part, MM_LATCH_REV)) { if (p.rev != d2) { p.rev = d2; m_dirtyRows |= (1u << part); } } }
		else if (d1 == 93) { if (fromUser || !IsLatched(part, MM_LATCH_CRS)) { if (p.crs != d2) { p.crs = d2; m_dirtyRows |= (1u << part); } } }
		else if (d1 == 94) { if (fromUser || !IsLatched(part, MM_LATCH_VAR)) { if (p.var != d2) { p.var = d2; m_dirtyRows |= (1u << part); } } }
		else if (d1 == 71) { p.rsn = d2 - 64; MmBumpFade(p.fadeFilt, m_burstApply); m_dirtyRows |= (1u << part); }
		else if (d1 == 74) { p.lpf = d2 - 64; MmBumpFade(p.fadeFilt, m_burstApply); m_dirtyRows |= (1u << part); }
		else if (d1 == 72) { p.rls = d2 - 64; MmBumpFade(p.fadeEnv, m_burstApply); m_dirtyRows |= (1u << part); }
		else if (d1 == 73) { p.atk = d2 - 64; MmBumpFade(p.fadeEnv, m_burstApply); m_dirtyRows |= (1u << part); }
		else if (d1 == 75) { p.dcy = d2 - 64; MmBumpFade(p.fadeEnv, m_burstApply); m_dirtyRows |= (1u << part); }
		else if (d1 == 76) { p.vibRat = d2 - 64; MmBumpFade(p.fadeVib, m_burstApply); m_dirtyRows |= (1u << part); }
		else if (d1 == 77) { p.vibDpt = d2 - 64; MmBumpFade(p.fadeVib, m_burstApply); m_dirtyRows |= (1u << part); }
		else if (d1 == 78) { p.vibDly = d2 - 64; MmBumpFade(p.fadeVib, m_burstApply); m_dirtyRows |= (1u << part); }
		else if (d1 == 98) { p.nrpnLsb = d2; MmBumpFade(p.fadeNrpn, m_burstApply); m_dirtyRows |= (1u << part); }
		else if (d1 == 99) { p.nrpnMsb = d2; MmBumpFade(p.fadeNrpn, m_burstApply); m_dirtyRows |= (1u << part); }
		else if (d1 == 100) p.rpnLsb = d2;
		else if (d1 == 101) p.rpnMsb = d2;
		else if (d1 == 6) {
			p.dataMsb = d2;
			ApplyNrpn(p);
			if (p.nrpnMsb == 1) {
				if (p.nrpnLsb == 8 || p.nrpnLsb == 9 || p.nrpnLsb == 10)
					p.fadeVib = 255;
				else if (p.nrpnLsb == 0x20 || p.nrpnLsb == 0x21 || p.nrpnLsb == 0x24)
					p.fadeFilt = 255;
				else if (p.nrpnLsb == 0x63 || p.nrpnLsb == 0x64 || p.nrpnLsb == 0x66)
					p.fadeEnv = 255;
			}
			MmBumpFade(p.fadeNrpn, m_burstApply);
			m_dirtyRows |= (1u << part);
		}
		else if (d1 == 121) {
			p.exp = 127; p.pan = 64; p.rev = 40; p.crs = 0; p.var = 0;
			m_dirtyRows |= (1u << part);
		}
		else if (d1 == 120 || d1 == 123) {
			memset(p.noteOn, 0, sizeof(p.noteOn));
			p.held = 0;
		}
	} else if (st == 0xe0) {
		const int bend = ((d2 << 7) | d1) - 8192;
		p.dt = bend / 64;
	}
	}
	if (!m_burstApply && m_nameNeed) {
		for (int i = 0; i < PART_MAX; ++i) {
			if (m_nameNeed & (1u << i)) {
				RefreshPartName(m_part[i]);
				m_dirtyRows |= (1u << i);
			}
		}
		m_nameNeed = 0;
	}
}

void CMidiMonitorDlg::ApplySysex(const BYTE* d, int n, int livePort)
{
	if (!d || n < 6) return;
	if (d[0] != 0xf0) return;
	if (n >= 6 && VstMidiSysexIsGmOn(d, n)) {
		const int gm2 = (n >= 5 && d[4] == 0x03);
		m_sysMode = 0;
		m_varConn = 1;
		if (gm2) m_gsMapKind = 9;
		if (livePort >= 0 && livePort <= 1) {
			ResetPartsBank(livePort);
			m_sysMode = 0;
			m_varConn = 1;
			return;
		}
		ResetParts();
		m_sysMode = 0;
		m_varConn = 1;
		return;
	}
	if (n >= 11 && VstMidiSysexIsGsReset(d, n)) {
		m_sysMode = 1;
		m_varConn = 1;
		if (livePort >= 0 && livePort <= 1) {
			ResetPartsBank(livePort);
			m_sysMode = 1;
			m_varConn = 1;
			return;
		}
		ResetParts();
		m_sysMode = 1;
		m_varConn = 1;
		return;
	}
	if (n >= 11 && d[1] == 0x41 && d[3] == 0x42 && d[4] == 0x12 &&
		(d[5] == 0x50 || d[5] == 0x60) && d[6] == 0x00 && d[7] == 0x7f) {
		m_sysMode = 1;
		if (livePort >= 0) {
			const int bank = livePort + 1;
			if (bank <= 1) ResetPartsBank(bank);
			return;
		}
		for (int i = 16; i < PART_MAX; ++i) {
			Part& p = m_part[i];
			p.pc = 0;
			p.bankMsb = 0;
			p.vol = 100;
			p.exp = 127;
			p.pan = 64;
			p.rev = 40;
			p.isDrum = ((i % 16) == 9) ? 1 : 0;
			p.rxCh = i % 16;
			p.rxPort = 1;
			memset(p.noteOn, 0, sizeof(p.noteOn));
			p.held = 0;
			if (!m_burstApply)
				RefreshPartName(p);
			else
				m_nameNeed |= (1u << i);
		}
		m_dirtyRows = 0xFFFFFFFFu;
		m_dirtyHead = true;
		return;
	}
	if (n >= 9 && VstMidiSysexIsXgOn(d, n)) {
		m_sysMode = 2;
		m_varConn = 0;
		if (livePort >= 0 && livePort <= 1) {
			ResetPartsBank(livePort);
			m_sysMode = 2;
			m_varConn = 0;
			return;
		}
		ResetParts();
		m_sysMode = 2;
		m_varConn = 0;
		m_revType = 1;
		m_choType = 0x41;
		m_varType = 5;
		m_revPacked = 0x0100;
		m_choPacked = 0x4100;
		m_varPacked = 0x0500;
		return;
	}
	{
		int ah = 0, am = 0, al = 0, nd = 0;
		const BYTE* data = NULL;
		const int footBefore = InsFootCount();
		if (MmXgUnpack(d, n, &ah, &am, &al, &data, &nd)) {
			int eff = 0;
			for (int i = 0; i < nd; ++i) {
				const int a = al + i;
				if (a > 127) break;
				const BYTE v = data[i] & 127;
				if (ah == 0x02 && am == 0x01) {
					if (a == 0x00) { m_revType = v; m_revPacked = (v << 8) | (m_revPacked & 0x7f); eff = 1; }
					else if (a == 0x01) { m_revPacked = (m_revPacked & 0x7f00) | v; eff = 1; }
					else if (a == 0x20) { m_choType = v; m_choPacked = (v << 8) | (m_choPacked & 0x7f); eff = 1; }
					else if (a == 0x21) { m_choPacked = (m_choPacked & 0x7f00) | v; eff = 1; }
					else if (a == 0x40) {
						m_varType = v;
						m_varPacked = (v << 8) | (m_varPacked & 0x7f);
						eff = 1;
					} else if (a == 0x41) {
						m_varPacked = (m_varPacked & 0x7f00) | v;
						eff = 1;
					} else if (a == 0x5A) {
						m_varConn = v ? 1 : 0;
						eff = 1;
					}
					if (a >= 0x40 && a < 0x40 + 32) {
						m_varBlk[a - 0x40] = v;
						eff = 1;
					}
				} else if (ah == 0x03 && (am == 0x00 || am == 0x01 || am == 0x02 || am == 0x03 || am == 0x10)) {
					int slot = 1;
					if (am == 0x00) slot = 0;
					else if (am == 0x02) slot = 2;
					else if (am == 0x03) slot = 3;
					else slot = 1;
					int* dst = &m_ins1;
					if (slot == 1) dst = &m_ins2;
					else if (slot == 2) dst = &m_ins3;
					else if (slot == 3) dst = &m_ins4;
					if (a == 0x00) { *dst = (v << 8) | (*dst & 0x7f); eff = 1; }
					else if (a == 0x01) { *dst = (*dst & 0x7f00) | v; eff = 1; }
					if (a < 48) {
						m_insBlk[slot][a] = v;
						eff = 1;
					}
				} else if ((ah == 0x08 || ah == 0x09) && am >= 0 && am < 16) {
					const int part = (ah == 0x09) ? (16 + am) : am;
					if (ah == 0x09)
						m_gs32 = 1;
					ApplyXgPartByte(part, a, v);
				}
			}
			if (eff) {
				m_dirtyHead = true;
				if (InsFootCount() != footBefore)
					m_fullDraw = true;
				SyncXgInsParts();
			}
		}
	}
	if (MmGsDt1Hdr(d, n)) {
		if (m_sysMode != 2)
			m_sysMode = 1;
		const int hasF7g = (n > 0 && d[n - 1] == 0xf7) ? 1 : 0;
		const int aa = d[5], bb = d[6], cc = d[7];
		int nval = n - 8 - hasF7g - 1;
		if (nval < 0) nval = 0;
		int blk = 0;
		if (aa == 0x50) blk = 1;
		else if (aa == 0x60) blk = 2;
		if (livePort >= 0) blk += livePort;

		if ((aa == 0x40 || aa == 0x50) && bb == 0x01) {
			for (int i = 0; i < nval; ++i) {
				const int a = cc + i;
				const BYTE vv = d[8 + i] & 127;
				if (a == 0x30) m_revType = vv;
				else if (a == 0x38) m_choType = vv;
				else if (a == 0x50) m_dlyType = vv;
			}
			m_dirtyHead = true;
		}
		if (aa == 0x40 && bb == 0x03) {
			for (int i = 0; i < nval; ++i) {
				const int a = cc + i;
				if (a < 0 || a >= 32) continue;
				m_gsEfx[a] = d[8 + i] & 127;
				m_gsEfxMask |= (1u << a);
				if (a == 0x01) m_gsEfxHasLsb = 1;
				m_ins1 = (m_gsEfx[0] << 8) | m_gsEfx[1];
				m_dirtyHead = true;
			}
		}
		if ((aa == 0x40 || aa == 0x50) && blk <= 1 &&
			((bb >= 0x10 && bb <= 0x1f) || (bb >= 0x20 && bb <= 0x2f) || (bb >= 0x40 && bb <= 0x4f))) {
			if (aa == 0x50)
				m_gs32 = 1;
			const int part = blk * 16 + MmGs1xToPart(bb);
			if (part >= 0 && part < PART_MAX) {
				Part& p = m_part[part];
				const int kind = (bb >> 4) & 0x0f;
				int changed = 0, mark = 0;
				for (int i = 0; i < nval; ++i) {
					const int a = cc + i;
					ApplyGsPartByte(p, kind, a, d[8 + i], &changed, &mark);
				}
				if (changed) {
					p.heard = 1;
					if (mark) {
						MmBumpFade(p.fadeInst, m_burstApply);
						m_nameNeed |= (1u << part);
					}
					m_dirtyRows |= (1u << part);
				}
			}
		}
	}
	if (!m_burstApply && m_nameNeed) {
		for (int i = 0; i < PART_MAX; ++i) {
			if (m_nameNeed & (1u << i)) {
				RefreshPartName(m_part[i]);
				m_dirtyRows |= (1u << i);
			}
		}
		m_nameNeed = 0;
	}
}

void CMidiMonitorDlg::ApplyEvent(const MmEv& e)
{
	if (e.msg == 0xff) {
		if (e.aux >= 10000) m_usecQn = (int)e.aux;
		return;
	}
	if (e.msg == 0xfe) {
		m_tsNum = (int)(e.aux & 0xff);
		m_tsDen = (int)((e.aux >> 8) & 0xff);
		if (m_tsNum < 1) m_tsNum = 4;
		if (m_tsDen < 1) m_tsDen = 4;
		return;
	}
	if (e.msg == 0xfd) {
		m_keySf = (int)(signed char)(e.aux & 0xff);
		m_keyMin = (int)((e.aux >> 8) & 1);
		return;
	}
	if (e.msg == 0xf0) {
		if (e.sysexOff >= 0 && e.sysexOff + (int)e.aux <= m_sxBytes)
			ApplySysex(m_sx + e.sysexOff, (int)e.aux);
		return;
	}
	ApplyShort(e.port, e.msg);
}

void CMidiMonitorDlg::LoadCurrentMidi()
{
	const wchar_t* src = filen;
	const int wantSr = MmWantMonitorSampleRate();
	if (src && src[0] && m_sourcePath[0] && _wcsicmp(src, m_sourcePath) == 0
		&& m_loadedPath[0] && m_ev && m_evCount > 0 && m_sampleRate == wantSr)
		return;

	wchar_t mid[520];
	mid[0] = 0;
	wchar_t hints[8][128];
	int hc = 0;
	if (src && src[0])
		VstResolvePlayPath(src, mid, 520, hints, 8, &hc);
	if (!mid[0] && src)
		MmCopyW(mid, 520, src);
	if (!mid[0]) {
		if (m_loadedPath[0]) {
			UnloadMidi();
			ResetParts();
		}
		m_sourcePath[0] = 0;
		return;
	}
	if (src && src[0])
		MmCopyW(m_sourcePath, 520, src);
	else
		m_sourcePath[0] = 0;
	if (_wcsicmp(m_loadedPath, mid) == 0 && m_sampleRate == MmWantMonitorSampleRate())
		return;

	UnloadMidi();
	ResetParts();
	// SMF でなくてもパスを覚える。空のままだと次のタイマーで Unload が heard を消し、
	// ライブ行がノート中だけ点いて note-off で灰色に戻る。
	MmCopyW(m_loadedPath, 520, mid);
	HANDLE f = CreateFileW(mid, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (f == INVALID_HANDLE_VALUE) return;
	DWORD size = GetFileSize(f, NULL), got = 0;
	if (size < 14 || size > 64 * 1024 * 1024) { CloseHandle(f); return; }
	BYTE* data = new (std::nothrow) BYTE[size]; // 巨大 SMF で new 失敗しても落ちない
	if (!data) { CloseHandle(f); return; }
	if (!ReadFile(f, data, size, &got, NULL) || got != size) {
		CloseHandle(f); delete[] data; return;
	}
	CloseHandle(f);
	const BYTE* smf = data;
	DWORD smfSize = size;
	if (size >= 20 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "RMID", 4) == 0) {
		DWORD off = 12;
		while (off + 8 <= size) {
			const DWORD cksz = (DWORD)data[off + 4] | ((DWORD)data[off + 5] << 8)
				| ((DWORD)data[off + 6] << 16) | ((DWORD)data[off + 7] << 24);
			if (memcmp(data + off, "data", 4) == 0) {
				if (off + 8 > size) break;
				smf = data + off + 8;
				smfSize = cksz;
				if (smf + smfSize > data + size)
					smfSize = (DWORD)(data + size - smf);
				break;
			}
			const DWORD step = 8 + ((cksz + 1) & ~1u);
			if (step < 8 || off + step < off) break;
			off += step;
		}
	}
	if (smfSize < 14 || memcmp(smf, "MThd", 4) || MmReadBE(smf + 4, 4) < 6) {
		delete[] data; return;
	}
	const int tracks = (int)MmReadBE(smf + 10, 2);
	const int division = (int)MmReadBE(smf + 12, 2);
	if (division <= 0 || (division & 0x8000)) { delete[] data; return; }
	m_division = division;
	MmEv* ev = new (std::nothrow) MmEv[EV_MAX];
	BYTE* sxData = new (std::nothrow) BYTE[(size_t)size + 8 + 128];
	if (!ev || !sxData) {
		delete[] ev;
		delete[] sxData;
		delete[] data;
		return;
	}
	int sxUsed = 0;
	const int sxCap = (int)size + 8 + 128;
	int count = 0;
	int hasXg = 0;
	int mapHint = 0;
	int sawFf21 = 0;
	int gs32 = 0;
	int maxPort = 0;
	m_titleBuf[0] = 0;
	const BYTE* p = smf + 8 + MmReadBE(smf + 4, 4);
	const BYTE* fileEnd = smf + smfSize;
	for (int tr = 0; tr < tracks && p + 8 <= fileEnd; ++tr) {
		if (memcmp(p, "MTrk", 4)) break;
		DWORD len = MmReadBE(p + 4, 4);
		const BYTE* q = p + 8;
		const BYTE* end = (q + len <= fileEnd) ? q + len : fileEnd;
		unsigned __int64 tick = 0;
		BYTE running = 0;
		int curPort = 0;
		while (q < end && count < EV_MAX) {
			unsigned delta = 0;
			if (!MmReadVar(q, end, delta)) break;
			tick += delta;
			if (q >= end) break;
			BYTE st = *q;
			if (st & 0x80) { ++q; if (st < 0xf0) running = st; }
			else if (running) st = running;
			else break;
			if (st == 0xff) {
				if (q >= end) break;
				BYTE type = *q++;
				unsigned ml = 0;
				if (!MmReadVar(q, end, ml) || q + ml > end) break;
				if (type == 0x51 && ml == 3) {
					ev[count].tick = tick; ev[count].sample = 0;
					ev[count].msg = 0xff; ev[count].aux = MmReadBE(q, 3);
					ev[count].port = curPort; ev[count].sysexOff = -1;
					++count;
				} else if (type == 0x58 && ml >= 2) {
					ev[count].tick = tick; ev[count].sample = 0;
					ev[count].msg = 0xfe;
					int den = 1 << (q[1] & 7);
					ev[count].aux = (DWORD)q[0] | ((DWORD)den << 8);
					ev[count].port = curPort; ev[count].sysexOff = -1;
					++count;
				} else if (type == 0x59 && ml >= 2) {
					ev[count].tick = tick; ev[count].sample = 0;
					ev[count].msg = 0xfd;
					ev[count].aux = (DWORD)(BYTE)q[0] | ((DWORD)(q[1] & 1) << 8);
					ev[count].port = curPort; ev[count].sysexOff = -1;
					++count;
				} else if (type == 0x21 && ml >= 1) {
					curPort = (int)q[0];
					if (curPort < 0) curPort = 0;
					if (curPort > 1) curPort = 1;
					sawFf21 = 1;
					if (curPort > maxPort) maxPort = curPort;
				} else if ((type == 0x01 || type == 0x02 || type == 0x03) && ml > 0) {
					char tmp[256];
					unsigned n = ml;
					if (n > 255) n = 255;
					memcpy(tmp, q, n);
					tmp[n] = 0;
					wchar_t w[256];
					w[0] = 0;
					if (!MultiByteToWideChar(932, 0, tmp, -1, w, 256))
						MultiByteToWideChar(CP_ACP, 0, tmp, -1, w, 256);
					w[255] = 0;
					if (w[0]) {
						mapHint = VstMidiFoldGsMapHint(mapHint, VstMidiGuessGsMapKind(w, NULL));
						int junk = 1;
						for (const wchar_t* p = w; *p; ++p) {
							const wchar_t c = *p;
							if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') continue;
							if (c != L'?' && c != L'*' && c != L'.' && c != L'-' && c != L'_' && c != L'!') {
								junk = 0;
								break;
							}
						}
						if (!junk && (wcsstr(w, L"GM版") || wcscmp(w, L"GM曲") == 0))
							junk = 1;
						if (!junk && (type == 0x03 || !m_titleBuf[0]))
							MmCopyW(m_titleBuf, 280, w);
					}
				}
				q += ml;
			} else if (st == 0xf0 || st == 0xf7) {
				unsigned sl = 0;
				if (!MmReadVar(q, end, sl) || q + sl > end) break;
				const int need = (st == 0xf0) ? (1 + (int)sl) : (int)sl;
				if (need > 0 && sxUsed + need <= sxCap) {
					const int off = sxUsed;
					if (st == 0xf0) sxData[sxUsed++] = 0xf0;
					memcpy(sxData + sxUsed, q, sl);
					sxUsed += (int)sl;
					ev[count].tick = tick; ev[count].sample = 0;
					ev[count].msg = 0xf0; ev[count].aux = (DWORD)need;
					ev[count].port = curPort; ev[count].sysexOff = off;
					++count;
					if (need >= 6) {
						if (VstMidiSysexIsXgOn(sxData + off, need))
							hasXg = 1;
						if (VstMidiSysexMarksGs32(sxData + off, need))
							gs32 = 1;
					}
				}
				q += sl;
			} else {
				const int kind = st & 0xf0;
				const int need = (kind == 0xc0 || kind == 0xd0) ? 1 : 2;
				if (q + need > end) break;
				BYTE d1 = q[0], d2 = (need == 2) ? q[1] : 0;
				q += need;
				if (kind >= 0x80 && kind <= 0xe0) {
					ev[count].tick = tick; ev[count].sample = 0;
					ev[count].msg = st | ((DWORD)d1 << 8) | ((DWORD)d2 << 16);
					ev[count].aux = 0;
					ev[count].port = curPort; ev[count].sysexOff = -1;
					++count;
				}
			}
		}
		p = end;
	}
	if (!count) {
		delete[] ev; delete[] data; delete[] sxData; return;
	}
	std::stable_sort(ev, ev + count, MmCmpEvStable);
	const int sr = MmWantMonitorSampleRate();
	m_sampleRate = sr;
	unsigned __int64 lastTick = 0;
	unsigned tempoU = 500000;
	__int64 sample = 0;
	unsigned __int64 rem = 0;
	const unsigned __int64 den = (unsigned __int64)division * 1000000ULL;
	for (int i = 0; i < count; ++i) {
		const unsigned __int64 dt = ev[i].tick - lastTick;
		rem += dt * (unsigned __int64)tempoU * (unsigned __int64)sr;
		sample += (__int64)(rem / den);
		rem %= den;
		ev[i].sample = sample;
		lastTick = ev[i].tick;
		if (ev[i].msg == 0xff && ev[i].aux)
			tempoU = (unsigned)ev[i].aux;
	}
	m_ev = ev;
	m_evCount = count;
	m_sx = sxData;
	m_sxBytes = sxUsed;
	MmCopyW(m_loadedPath, 520, mid);
	if (!mapHint)
		mapHint = VstMidiGuessGsMapKind(m_titleBuf, mid);
	else
		mapHint = VstMidiFoldGsMapHint(mapHint, VstMidiGuessGsMapKind(NULL, mid));
	if (mapHint == 7) hasXg = 1;
	{
		BYTE msb[32];
		BYTE have[2048];
		unsigned short pairs[256];
		int nPairs = 0, hasGm = 0, hasGs = 0, hasSd = 0, hasGm2 = 0, cc32Max = 0;
		memset(msb, 0, sizeof(msb));
		memset(have, 0, sizeof(have));
		for (int i = 0; i < count; ++i) {
			if (ev[i].msg == 0xf0 && ev[i].sysexOff >= 0) {
				const int n = (int)ev[i].aux;
				if (ev[i].sysexOff + n <= sxUsed) {
					const BYTE* d = sxData + ev[i].sysexOff;
					if (VstMidiSysexIsXgOn(d, n)) hasXg = 1;
					if (VstMidiSysexIsGmOn(d, n)) {
						hasGm = 1;
						if (n >= 5 && d[4] == 0x03) hasGm2 = 1;
					}
					if (VstMidiSysexIsGsReset(d, n)) hasGs = 1;
				}
				continue;
			}
			const int st = (int)(ev[i].msg & 0xf0);
			const int ch = (int)(ev[i].msg & 0x0f);
			int idx = ev[i].port * 16 + ch;
			if (idx < 0) idx = ch;
			if (idx > 31) idx = 31;
			const int d1 = (int)((ev[i].msg >> 8) & 0x7f);
			const int d2 = (int)((ev[i].msg >> 16) & 0x7f);
			const int drum = (ch == 9) ? 1 : 0;
			if (st == 0xb0 && d1 == 0) {
				msb[idx] = (BYTE)d2;
				if (VstMidiBankMsbIsSdNative(d2)) hasSd = 1;
				if (d2 == 121) hasGm2 = 1;
			} else if (st == 0xb0 && d1 == 32) {
				if (!drum && d2 >= 1 && d2 <= 4 && d2 > cc32Max) cc32Max = d2;
			} else if (st == 0xc0 && !drum) {
				const int bank = (int)msb[idx];
				const int bit = bank * 128 + d1;
				if (bit >= 0 && bit < 16384) {
					const int bi = bit >> 3;
					const BYTE mask = (BYTE)(1 << (bit & 7));
					if (!(have[bi] & mask)) {
						have[bi] = (BYTE)(have[bi] | mask);
						if (nPairs < 256)
							pairs[nPairs++] = (unsigned short)((bank << 8) | d1);
					}
				}
			}
		}
		int resolved = 0;
		if (hasXg) resolved = 0;
		else if (mapHint == 8) resolved = 8;
		else if (mapHint >= 9 && mapHint <= 18) resolved = mapHint;
		else if (mapHint >= 1 && mapHint <= 4) resolved = mapHint;
		else if (hasGm2 && !hasGs) resolved = 9;
		else if ((mapHint == 5 || hasGm) && !hasGs) resolved = 5;
		else if (mapHint == 6 || hasSd) resolved = 6;
		else if (cc32Max >= 1 && cc32Max <= 4) resolved = cc32Max;
		else resolved = VstMidiGsMapDropFromUsed(pairs, nPairs);
		m_fileHasXg = hasXg;
		m_fileHasGm = (resolved == 5) ? 1 : 0;
		m_fileHasSd = (resolved == 6) ? 1 : 0;
		m_gsMapKind = (resolved == 8 || (resolved >= 1 && resolved <= 6) ||
			(resolved >= 9 && resolved <= 18)) ? resolved : 0;
	}
	BYTE chUse[PART_MAX];
	memset(chUse, 0, sizeof(chUse));
	for (int i = 0; i < count; ++i) {
		const DWORD msg = ev[i].msg;
		if (msg == 0xff || msg == 0xfe || msg == 0xfd || msg == 0xf0) {
			if (msg != 0xf0 || ev[i].sysexOff < 0) continue;
			const int n = (int)ev[i].aux;
			if (ev[i].sysexOff + n > sxUsed || n < 8) continue;
			const BYTE* d = sxData + ev[i].sysexOff;
			int ah = 0, am = 0, al = 0, nd = 0;
			const BYTE* xd = NULL;
			if (MmXgUnpack(d, n, &ah, &am, &al, &xd, &nd) && (ah == 0x08 || ah == 0x09) && am >= 0 && am < 16) {
				const int part = (ah == 0x09) ? (16 + am) : am;
				if (part >= 0 && part < PART_MAX) chUse[part] = 1;
				if (ah == 0x09) gs32 = 1;
				continue;
			}
			if (!MmGsDt1Hdr(d, n)) continue;
			if (!((d[6] >= 0x10 && d[6] <= 0x1f) || (d[6] >= 0x20 && d[6] <= 0x2f) || (d[6] >= 0x40 && d[6] <= 0x4f)))
				continue;
			if (d[5] != 0x40 && d[5] != 0x50) continue;
			if (d[7] == 0x15 && n <= 12) continue;
			const int blk = (d[5] == 0x50) ? 1 : 0;
			const int part = blk * 16 + MmGs1xToPart(d[6]);
			if (part >= 0 && part < PART_MAX) chUse[part] = 1;
			continue;
		}
		const int st = (int)(msg & 0xf0);
		if (st < 0x80 || st > 0xe0) continue;
		int idx = ev[i].port * 16 + (int)(msg & 0x0f);
		if (idx < 0) idx = 0;
		if (idx >= PART_MAX) idx = PART_MAX - 1;
		chUse[idx] = 1;
	}
	m_gs32 = gs32;
	m_mirrorToB = (gs32 && !sawFf21) ? 1 : 0;
	const int wantB = (gs32 || maxPort >= 1) ? 1 : 0;
	if (!hasXg && m_fileHasGm == 0 && count + 8 < EV_MAX && sxUsed + 44 <= sxCap) {
		const BYTE rhyA[11] = { 0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x10, 0x15, 0x01, 0x1a, 0xf7 };
		const BYTE rhyB[11] = { 0xf0, 0x41, 0x10, 0x42, 0x12, 0x50, 0x10, 0x15, 0x01, 0x0a, 0xf7 };
		auto isRst = [&](const MmEv& e) -> int {
			if (e.msg != 0xf0 || e.sysexOff < 0) return 0;
			const int n = (int)e.aux;
			if (e.sysexOff + n > sxUsed || n < 11) return 0;
			const BYTE* d = sxData + e.sysexOff;
			if (VstMidiSysexIsGsReset(d, n)) return 1;
			if (d[0] == 0xf0 && d[1] == 0x41 && d[3] == 0x42 && d[4] == 0x12 &&
				d[6] == 0x00 && d[7] == 0x7f && (d[5] == 0x50 || d[5] == 0x60))
				return (d[5] == 0x50) ? 2 : 3;
			return 0;
		};
		MmEv* tmp = new (std::nothrow) MmEv[EV_MAX];
		if (tmp) {
		int w = 0;
		int any = 0;
		for (int i = 0; i < count; ++i)
			if (isRst(ev[i])) { any = 1; break; }
		auto put = [&](int which, unsigned __int64 tick, __int64 samp) {
			if (w >= EV_MAX || sxUsed + 11 > sxCap) return;
			const BYTE* src = (which == 2) ? rhyB : rhyA;
			const int off = sxUsed;
			memcpy(sxData + sxUsed, src, 11);
			sxUsed += 11;
			tmp[w].tick = tick; tmp[w].sample = samp; tmp[w].msg = 0xf0;
			tmp[w].aux = 11; tmp[w].port = (which == 2) ? 1 : 0;
			tmp[w].sysexOff = off;
			++w;
		};
		if (!any) { put(1, 0, 0); if (wantB) put(2, 0, 0); }
		for (int i = 0; i < count && w < EV_MAX; ++i) {
			tmp[w++] = ev[i];
			const int k = isRst(ev[i]);
			if (k == 1) { put(1, ev[i].tick, ev[i].sample); if (wantB) put(2, ev[i].tick, ev[i].sample); }
			else if (k == 2) put(2, ev[i].tick, ev[i].sample);
			else if (k == 3) put(1, ev[i].tick, ev[i].sample);
		}
		delete[] ev;
		ev = tmp;
		count = w;
		m_ev = ev;
		m_evCount = count;
		}
	}
	if (hasXg && count + 8 < EV_MAX) {
		auto isXg = [&](const MmEv& e) -> int {
			if (e.msg != 0xf0 || e.sysexOff < 0) return 0;
			const int n = (int)e.aux;
			if (e.sysexOff + n > sxUsed) return 0;
			return VstMidiSysexIsXgOn(sxData + e.sysexOff, n);
		};
		MmEv* tmp = new (std::nothrow) MmEv[EV_MAX];
		if (tmp) {
		int w = 0;
		int any = 0;
		for (int i = 0; i < count; ++i)
			if (isXg(ev[i])) { any = 1; break; }
		auto put = [&](unsigned __int64 tick, __int64 samp, int port) {
			if (w + 2 > EV_MAX) return;
			tmp[w].tick = tick; tmp[w].sample = samp;
			tmp[w].msg = 0xb9 | (0u << 8) | (127u << 16);
			tmp[w].aux = 0; tmp[w].port = port; tmp[w].sysexOff = -1;
			++w;
			tmp[w].tick = tick; tmp[w].sample = samp;
			tmp[w].msg = 0xb9 | (32u << 8);
			tmp[w].aux = 0; tmp[w].port = port; tmp[w].sysexOff = -1;
			++w;
		};
		if (!any) { put(0, 0, 0); if (wantB) put(0, 0, 1); }
		for (int i = 0; i < count && w < EV_MAX; ++i) {
			tmp[w++] = ev[i];
			if (isXg(ev[i])) {
				put(ev[i].tick, ev[i].sample, ev[i].port);
				if (wantB && ev[i].port == 0) put(ev[i].tick, ev[i].sample, 1);
			}
		}
		delete[] ev;
		ev = tmp;
		count = w;
		}
	}
	m_ev = ev;
	m_evCount = count;
	m_sx = sxData;
	m_sxBytes = sxUsed;
	m_tsEvN = 0;
	m_maxTick = 0;
	for (int i = 0; i < count; ++i) {
		if (ev[i].tick > m_maxTick)
			m_maxTick = ev[i].tick;
		if (ev[i].msg == 0xfe && m_tsEvN < 64) {
			m_tsEv[m_tsEvN].tick = ev[i].tick;
			m_tsEv[m_tsEvN].num = (int)(ev[i].aux & 0xff);
			m_tsEv[m_tsEvN].den = (int)((ev[i].aux >> 8) & 0xff);
			if (m_tsEv[m_tsEvN].num < 1) m_tsEv[m_tsEvN].num = 4;
			if (m_tsEv[m_tsEvN].den < 1) m_tsEv[m_tsEvN].den = 4;
			++m_tsEvN;
		}
	}
	ResetParts();
	for (int i = 0; i < PART_MAX; ++i)
		m_part[i].heard = chUse[i];
	MarkHostOccupiedParts();
	delete[] data;
	m_loopStartSample = 0;
	m_loopEndSample = 0;
	{
		__int64 ccS = 0, ccE = 0;
		for (int i = 0; i < count; ++i) {
			if ((ev[i].msg & 0xf0) != 0xb0) continue;
			if (((ev[i].msg >> 8) & 0x7f) != 111) continue;
			const int v = (int)((ev[i].msg >> 16) & 0x7f);
			if (v == 0) ccS = ev[i].sample;
			else ccE = ev[i].sample;
		}
		if (ccE > ccS) {
			m_loopStartSample = ccS;
			m_loopEndSample = ccE;
		}
	}
	m_lastPlayb = -1;
	m_evPos = 0;
	m_hadNote = 0;
	{
		int force = 0;
		const wchar_t* mapPath = m_sourcePath[0] ? m_sourcePath : m_loadedPath;
		if (PlMidDiskGet(mapPath, NULL, NULL, NULL, &force) < 0)
			force = 0;
		ApplyMapForce(force);
	}
	UpdatePlayPos();
}

static int MmSxIsModeReset(const BYTE* d, int n)
{
	if (!d || n < 6) return 0;
	if (VstMidiSysexIsGmOn(d, n) || VstMidiSysexIsGsReset(d, n) || VstMidiSysexIsXgOn(d, n))
		return 1;
	return 0;
}

static DWORD MmSxParamKey(int port, const BYTE* d, int n)
{
	DWORD k = (DWORD)(port & 3) << 30;
	if (!d || n < 5) return k | 1u;
	if (d[1] == 0x41 && n >= 8 && d[4] == 0x12)
		return k | 0x10000000u | ((DWORD)d[5] << 16) | ((DWORD)d[6] << 8) | (DWORD)d[7];
	if (d[1] == 0x43 && n >= 7 && d[3] == 0x4c)
		return k | 0x20000000u | ((DWORD)d[4] << 16)
			| ((DWORD)(n >= 8 ? d[5] : 0) << 8) | (DWORD)(n >= 9 ? d[6] : 0);
	return k | ((DWORD)d[1] << 16) | ((DWORD)d[2] << 8) | (DWORD)d[3];
}

static int MmEvIsModeReset(const CMidiMonitorDlg::MmEv& e, const BYTE* sx, int sxBytes)
{
	if (e.msg != 0xf0 || e.sysexOff < 0) return 0;
	const int n = (int)e.aux;
	if (n < 6 || e.sysexOff + n > sxBytes) return 0;
	return MmSxIsModeReset(sx + e.sysexOff, n);
}

static int MmEvIsNoteOn(const CMidiMonitorDlg::MmEv& e)
{
	if ((e.msg & 0xf0) != 0x90) return 0;
	if (((e.msg >> 16) & 0x7f) == 0) return 0;
	return 1;
}

// playb までのイベントを適用。大量シーク時は最後の GM/GS/XG リセットから圧縮して再適用。
void CMidiMonitorDlg::ApplyDueEvents(int lastDue)
{
	if (lastDue < m_evPos) return;
	const int nDue = lastDue - m_evPos + 1;
	const int savedBurst = m_burstApply;
	if (nDue > 32)
		m_burstApply = 1;
	if (nDue <= 32) {
		for (int i = m_evPos; i <= lastDue; ++i) {
			ApplyEvent(m_ev[i]);
			if (MmEvIsNoteOn(m_ev[i]))
				m_hadNote = 1;
		}
	} 	else {
		int start = m_evPos;
		for (int k = lastDue; k >= m_evPos; --k) {
			if (MmEvIsModeReset(m_ev[k], m_sx, m_sxBytes)) {
				start = k; // 途中のリセットより前は捨ててよい
				break;
			}
		}
		int lastCc[2][16][128];
		int lastPc[2][16];
		int lastPb[2][16];
		int lastAt[2][16];
		int lastT = -1, lastS = -1, lastK = -1;
		memset(lastCc, 0xff, sizeof(lastCc));
		memset(lastPc, 0xff, sizeof(lastPc));
		memset(lastPb, 0xff, sizeof(lastPb));
		memset(lastAt, 0xff, sizeof(lastAt));
		DWORD sxKey[2048];
		int sxLast[2048];
		memset(sxLast, 0xff, sizeof(sxLast));
		int sxFail = 0;
		for (int k = start; k <= lastDue; ++k) {
			const MmEv& e = m_ev[k];
			if (e.msg == 0xff) { lastT = k; continue; }
			if (e.msg == 0xfe) { lastS = k; continue; }
			if (e.msg == 0xfd) { lastK = k; continue; }
			if (e.msg == 0xf0) {
				if (MmEvIsModeReset(e, m_sx, m_sxBytes))
					continue;
				if (e.sysexOff < 0 || e.sysexOff + (int)e.aux > m_sxBytes)
					continue;
				const DWORD key = MmSxParamKey(e.port, m_sx + e.sysexOff, (int)e.aux);
				unsigned s = key & 2047u;
				int put = 0;
				for (int p = 0; p < 16; ++p) {
					const unsigned t = (s + (unsigned)p) & 2047u;
					if (sxLast[t] < 0 || sxKey[t] == key) {
						sxKey[t] = key;
						sxLast[t] = k;
						put = 1;
						break;
					}
				}
				if (!put)
					sxFail = 1;
				continue;
			}
			int port = e.port;
			if (port < 0) port = 0;
			if (port > 1) port = 1;
			const int st = (int)(e.msg & 0xf0);
			const int ch = (int)(e.msg & 0x0f);
			const int d1 = (int)((e.msg >> 8) & 0x7f);
			if (st == 0xb0)
				lastCc[port][ch][d1] = k;
			else if (st == 0xc0)
				lastPc[port][ch] = k;
			else if (st == 0xe0)
				lastPb[port][ch] = k;
			else if (st == 0xd0)
				lastAt[port][ch] = k;
		}
		for (int k = start; k <= lastDue; ++k) {
			const MmEv& e = m_ev[k];
			int keep = 1;
			if (e.msg == 0xff) keep = (k == lastT);
			else if (e.msg == 0xfe) keep = (k == lastS);
			else if (e.msg == 0xfd) keep = (k == lastK);
			else if (e.msg == 0xf0) {
				if (MmEvIsModeReset(e, m_sx, m_sxBytes))
					keep = (k == start);
				else if (!sxFail && e.sysexOff >= 0 && e.sysexOff + (int)e.aux <= m_sxBytes) {
					const DWORD key = MmSxParamKey(e.port, m_sx + e.sysexOff, (int)e.aux);
					unsigned s = key & 2047u;
					int last = -2;
					for (int p = 0; p < 16; ++p) {
						const unsigned t = (s + (unsigned)p) & 2047u;
						if (sxLast[t] < 0) { last = -1; break; }
						if (sxKey[t] == key) { last = sxLast[t]; break; }
					}
					keep = (last == k || last < 0);
				}
			} else {
				int port = e.port;
				if (port < 0) port = 0;
				if (port > 1) port = 1;
				const int st = (int)(e.msg & 0xf0);
				const int ch = (int)(e.msg & 0x0f);
				const int d1 = (int)((e.msg >> 8) & 0x7f);
				if (st == 0xb0) {
					if (d1 != 120 && d1 != 121 && d1 != 123 && d1 != 6 && d1 != 38
						&& (d1 < 96 || d1 > 101))
						keep = (k == lastCc[port][ch][d1]);
				} else if (st == 0xc0)
					keep = (k == lastPc[port][ch]);
				else if (st == 0xe0)
					keep = (k == lastPb[port][ch]);
				else if (st == 0xd0)
					keep = (k == lastAt[port][ch]);
			}
			if (keep) {
				ApplyEvent(e);
				if (MmEvIsNoteOn(e))
					m_hadNote = 1;
			}
		}
	}
	m_evPos = lastDue + 1;
	m_burstApply = savedBurst;
}

// playb を鍵盤・CC 表示へ進める。VST/KPI は DS カーソルより先なので 700ms 引く。
// ExtrapolateHeard でサンプルが止まっている間も QPC で少し先読みする。
void CMidiMonitorDlg::SyncFromPlayback()
{
	if (m_frozen) return;
	LoadCurrentMidi();
	if (!m_ev || m_evCount <= 0) {
		UpdatePlayPos();
		return;
	}
	__int64 pbRaw = playb;
	if (pbRaw < 0) pbRaw = 0;
	__int64 pbHeard = pbRaw;
	if (mode == MODE_VST_MIDI) {
		const int vstPcm = (savedata.vstMultiDll[0] || savedata.vstExtraPath[0]);
		if (vstPcm) {
			// 時間表示と同じ再生カーソル（エンジン側）。ノート表示だけ
			// プラグイン遅延＋DS がアナログより先に進む 700ms を引く。
			const int sr = (m_sampleRate > 0) ? m_sampleRate : 44100;
			const double sec = OggGetGdiPlaybackTimeSec();
			pbRaw = (__int64)(sec * (double)sr + 0.5);
			MmBindVstActiveSlot();
			pbHeard = pbRaw - VstMidiGetLatencySamples() - (__int64)sr * 700 / 1000;
			if (pbRaw < 0) pbRaw = 0;
			if (pbHeard < 0) pbHeard = 0;
		}
	} else if (mode == -3) {
		// mid(kpi): playb はデコード書き込み位置で、DS 再生カーソルより先。
		// イベント時刻は KPI の実レート。VST プラグイン遅延は無い。
		const int sr = (m_sampleRate > 0) ? m_sampleRate : 44100;
		const double sec = OggGetGdiPlaybackTimeSec();
		pbRaw = (__int64)(sec * (double)sr + 0.5);
		pbHeard = pbRaw - (__int64)sr * 700 / 1000;
		if (pbRaw < 0) pbRaw = 0;
		if (pbHeard < 0) pbHeard = 0;
	}
	if (m_loopEndSample > m_loopStartSample) {
		const __int64 ls = m_loopStartSample;
		const __int64 le = m_loopEndSample;
		const __int64 span = le - ls;
		auto wrapFwd = [&](__int64 s) -> __int64 {
			if (s <= le) return s;
			if (span <= 0) return ls;
			return ls + ((s - le - 1) % span);
		};
		pbRaw = wrapFwd(pbRaw);
		pbHeard = wrapFwd(pbHeard);
		if (pbRaw >= ls && pbHeard < ls) {
			const __int64 behind = ls - pbHeard;
			pbHeard = le - ((behind - 1) % span);
			if (pbHeard < ls) pbHeard = ls;
		}
	}
	ExtrapolateHeard(pbHeard);
	if (m_loopEndSample > m_loopStartSample && pbHeard > m_loopEndSample) {
		const __int64 span = m_loopEndSample - m_loopStartSample;
		if (span > 0)
			pbHeard = m_loopStartSample + ((pbHeard - m_loopEndSample - 1) % span);
	}
	if (pbRaw < m_lastPlayb || (m_hearPlayb >= 0 && pbHeard < m_hearPlayb)) {
		ResetParts();
		m_evPos = 0;
		m_hadNote = 0;
		m_pbAnchor = 0;
		m_pbQpc = 0;
	}
	m_lastPlayb = pbRaw;
	m_hearPlayb = pbHeard;

	m_burstApply = 0;
	if (!m_hadNote) {
		int lastDump = m_evPos - 1;
		for (int i = m_evPos; i < m_evCount && m_ev[i].sample <= pbRaw; ++i) {
			if (MmEvIsNoteOn(m_ev[i]))
				break;
			lastDump = i;
		}
		if (lastDump >= m_evPos) {
			m_burstApply = 1;
			ApplyDueEvents(lastDump);
			m_burstApply = 0;
		}
		int lastHeard = m_evPos - 1;
		for (int i = m_evPos; i < m_evCount && m_ev[i].sample <= pbHeard; ++i)
			lastHeard = i;
		if (lastHeard >= m_evPos)
			ApplyDueEvents(lastHeard);
	} else {
		int lastDue = m_evPos - 1;
		for (int i = m_evPos; i < m_evCount && m_ev[i].sample <= pbHeard; ++i)
			lastDue = i;
		if (lastDue >= m_evPos)
			ApplyDueEvents(lastDue);
	}
	if (m_nameNeed) {
		for (int i = 0; i < PART_MAX; ++i) {
			if (m_nameNeed & (1u << i))
				RefreshPartName(m_part[i]);
		}
		m_nameNeed = 0;
	}
	UpdateNoteMeter();
	UpdatePlayPos();
}

void CMidiMonitorDlg::UpdatePlayPos()
{
	unsigned __int64 tick = 0;
	if (m_ev && m_evCount > 0) {
		__int64 ds = 0;
		if (m_evPos > 0) {
			const int i = m_evPos - 1;
			tick = m_ev[i].tick;
			ds = m_hearPlayb - m_ev[i].sample;
		} else {
			ds = m_hearPlayb;
		}
		if (ds < 0) ds = 0;
		if (ds > 0 && m_usecQn > 0 && m_division > 0 && m_sampleRate > 0) {
			tick += (unsigned __int64)ds * (unsigned __int64)m_division * 1000000ULL
				/ ((unsigned __int64)m_usecQn * (unsigned __int64)m_sampleRate);
			if (m_evPos >= 0 && m_evPos < m_evCount && tick > m_ev[m_evPos].tick)
				tick = m_ev[m_evPos].tick;
		}
		if (tick > m_maxTick) tick = m_maxTick;
	}

	auto fold = [&](unsigned __int64 target, int* bar, int* beat, int* tickIn, int* tpbOut, int* numOut) {
		int num = 4, den = 4;
		unsigned __int64 last = 0;
		unsigned __int64 acc = 0;
		int ei = 0;
		auto tpbOf = [&]() -> unsigned __int64 {
			int t = (m_division > 0 ? m_division : 480) * 4 / den;
			if (t < 1) t = 1;
			return (unsigned __int64)t;
		};
		auto tpmOf = [&]() -> unsigned __int64 {
			unsigned __int64 tpm = tpbOf() * (unsigned __int64)num;
			if (tpm < 1) tpm = 1;
			return tpm;
		};
		while (ei < m_tsEvN && m_tsEv[ei].tick <= target) {
			acc += (m_tsEv[ei].tick - last) / tpmOf();
			last = m_tsEv[ei].tick;
			num = m_tsEv[ei].num;
			den = m_tsEv[ei].den;
			if (num < 1) num = 4;
			if (den < 1) den = 4;
			++ei;
		}
		const unsigned __int64 tpb = tpbOf();
		const unsigned __int64 tpm = tpmOf();
		const unsigned __int64 span = (target >= last) ? (target - last) : 0;
		if (bar) *bar = (int)(acc + span / tpm) + 1;
		if (beat) *beat = (int)((span % tpm) / tpb) + 1;
		if (tickIn) *tickIn = (int)(span % tpm);
		if (tpbOut) *tpbOut = (int)tpm;
		if (numOut) *numOut = num;
	};

	int bar = 1, beat = 1, tickIn = 0, tpm = 1920, num = 4;
	int bars = 1;
	fold(tick, &bar, &beat, &tickIn, &tpm, &num);
	fold(m_maxTick, &bars, NULL, NULL, NULL, NULL);
	if (bar < 1) bar = 1;
	if (bars < 1) bars = 1;
	if (beat < 1) beat = 1;
	if (num < 1) num = 4;
	if (tpm < 1) tpm = 1920;
	m_posBar = bar;
	m_posBars = bars;
	m_posBeat = beat;
	m_posTick = tickIn;
	m_posTpm = tpm;
	m_posNum = num;
}

void CMidiMonitorDlg::DrawVBar(CDC& dc, int x, int y, int bw, int bh, int v0, int vmax, COLORREF col, int glow, int idle)
{
	if (bw < 2 || bh < 2) return;
	dc.FillSolidRect(x, y, bw, bh, RGB(16, 16, 20));
	int v = v0;
	if (v < 0) v = 0;
	if (vmax < 1) vmax = 1;
	if (v > vmax) v = vmax;
	int h = (bh * v) / vmax;
	if (h <= 0) return;
	COLORREF c = col;
	if (idle)
		c = MmMix(RGB(38, 40, 46), col, 40);
	else if (glow > 0)
		c = MmMix(col, RGB(255, 255, 220), glow);
	else
		c = MmMix(RGB(48, 50, 56), col, 150); // glow=0 固定。ノート中もバーをフェードさせない
	dc.FillSolidRect(x, y + bh - h, bw, h, c);
	if (glow > 40 && h > 2)
		dc.FillSolidRect(x, y + bh - h, bw, 1, MmMix(c, RGB(255, 255, 255), 120));
}

void CMidiMonitorDlg::DrawPanBar(CDC& dc, int x, int y, int bw, int bh, int pan, int glow, int idle)
{
	if (bw < 3 || bh < 2) return;
	dc.FillSolidRect(x, y, bw, bh, RGB(16, 16, 20));
	int mid = x + bw / 2;
	dc.FillSolidRect(mid, y, 1, bh, RGB(70, 70, 36));
	int p = pan;
	if (p < 0) p = 0;
	if (p > 127) p = 127;
	int px = x + (bw - 2) * p / 127;
	COLORREF c = RGB(230, 210, 40);
	if (idle)
		c = RGB(90, 88, 50);
	else if (glow > 0)
		c = MmMix(c, RGB(255, 255, 200), glow);
	dc.FillSolidRect(px, y + 1, 2, bh - 2, c);
}

// A0..C8。noteOn か noteFlash なら点灯（短い音でも flash で見える）。
void CMidiMonitorDlg::DrawMiniKeys(CDC& dc, const CRect& rc, const Part& p, COLORREF keyW, COLORREF keyB)
{
	if (rc.Width() < 20 || rc.Height() < 6) return;
	const int k0 = 21, k1 = 108;
	int whites = 0;
	for (int n = k0; n <= k1; ++n) {
		const int m = n % 12;
		if (m != 1 && m != 3 && m != 6 && m != 8 && m != 10) whites++;
	}
	if (whites < 1) return;
	const int ww = rc.Width();
	const int hh = rc.Height();
	int wi = 0;
	for (int n = k0; n <= k1; ++n) {
		const int m = n % 12;
		if (m == 1 || m == 3 || m == 6 || m == 8 || m == 10) continue;
		int x0 = rc.left + wi * ww / whites;
		int x1 = rc.left + (wi + 1) * ww / whites;
		COLORREF c = keyW;
		if (p.noteOn[n] || p.noteFlash[n]) c = RGB(220, 40, 40); // 短い音は flash だけでも点灯
		dc.FillSolidRect(x0, rc.top, max(1, x1 - x0 - 1), hh, c);
		wi++;
	}
	wi = 0;
	for (int n = k0; n <= k1; ++n) {
		const int m = n % 12;
		if (m != 1 && m != 3 && m != 6 && m != 8 && m != 10) { wi++; continue; }
		int xw = rc.left + (wi * ww / whites);
		int bw = max(2, ww / whites * 6 / 10);
		int x0 = xw - bw / 2;
		COLORREF c = keyB;
		if (p.noteOn[n] || p.noteFlash[n]) c = RGB(255, 70, 70); // 黒鍵も flash で点灯
		dc.FillSolidRect(x0, rc.top, bw, hh * 6 / 10, c);
	}
}

void CMidiMonitorDlg::DrawHeader(CDC& dc, int w, int headH, UINT dpi)
{
	dc.FillSolidRect(0, 0, w, headH, MM_HEAD_BG);
	dc.SetBkMode(TRANSPARENT);
	CFont* oldF = dc.SelectObject(&m_fontHead);
	dc.SetTextColor(MM_HEAD_TX);

	int bpm = 0;
	if (m_usecQn > 0)
		bpm = (int)((60000000.0 / (double)m_usecQn) + 0.5);
	if (bpm < 1) bpm = savedata.mpDetectedBpm;
	m_dispBpm = bpm;
	int tpc = tempo;
	if (tpc < 1) tpc = 100;
	m_transpose = (pitch != 0) ? (int)((double)pitch / 100.0 + (pitch > 0 ? 0.5 : -0.5)) : 0;

	wchar_t keyBuf[40];
	MmKeyName(m_keySf, m_keyMin, keyBuf, 40);
	const wchar_t* fn = m_loadedPath[0] ? m_loadedPath : (LPCWSTR)filen;
	const wchar_t* slash = fn ? wcsrchr(fn, L'\\') : NULL;
	const wchar_t* base = slash ? slash + 1 : (fn ? fn : L"");
	/* ファイル名の横にシーケンス名/曲名メタ(FF 03)も出す */
	wchar_t nameBuf[400];
	if (m_titleBuf[0] && _wcsicmp(m_titleBuf, base) != 0)
		_snwprintf_s(nameBuf, _TRUNCATE, L"%s  /  %s", base, m_titleBuf);
	else
		MmCopyW(nameBuf, 400, base);
	wchar_t line1[560];
	_snwprintf_s(line1, _TRUNCATE, L"BPM %3d    %3d%%    %s    TB %d    %d/%d    Transpose %d    %s",
		bpm, tpc, keyBuf, m_division, m_tsNum, m_tsDen, m_transpose, nameBuf);
	dc.TextOut(Scale(8, dpi), Scale(4, dpi), line1);

	const int volBarW = max(40, w / 3);
	const int vx = Scale(8, dpi);
	const int vy = Scale(22, dpi);
	const int vh = Scale(10, dpi);
	m_volBarRc.SetRect(vx, vy, vx + volBarW, vy + vh);
	dc.FillSolidRect(vx, vy, volBarW, vh, RGB(22, 28, 22));
	int pct = m_masterVol;
	if (pct < 0) pct = 0;
	if (pct > 100) pct = 100;
	const int fillW = volBarW * pct / 100;
	if (fillW > 0)
		dc.FillSolidRect(vx, vy, fillW, vh, RGB(48, 170, 70));
	dc.FillSolidRect(vx, vy, volBarW, 1, RGB(70, 100, 70));
	dc.FillSolidRect(vx, vy + vh - 1, volBarW, 1, RGB(20, 40, 20));
	for (int k = 1; k < 4; ++k) {
		const int tx = vx + volBarW * k / 4;
		dc.FillSolidRect(tx, vy + 1, 1, vh - 2, RGB(36, 52, 36));
	}
	wchar_t volT[32];
	_snwprintf_s(volT, _TRUNCATE, L"DS %d%%", pct);
	dc.SetTextColor(RGB(245, 255, 245));
	dc.TextOut(vx + Scale(4, dpi), Scale(21, dpi), volT);

	const int nx = vx + volBarW + Scale(6, dpi);
	const int notesW = Scale(140, dpi);
	m_notesBarRc.SetRect(nx, vy, nx + notesW, vy + vh);
	dc.FillSolidRect(nx, vy, notesW, vh, RGB(14, 22, 32));
	int nScale = 32;
	const int pk = (int)(m_notesPeak + 0.5f);
	if (pk > nScale) nScale = pk;
	if (m_noteCount > nScale) nScale = m_noteCount;
	if (nScale < 1) nScale = 1;
	const int maxW = notesW * pk / nScale;
	const int nFill = notesW * m_noteCount / nScale;
	if (maxW > 0)
		dc.FillSolidRect(nx, vy, maxW, vh, RGB(36, 72, 104));
	if (nFill > 0)
		dc.FillSolidRect(nx, vy, nFill, vh, RGB(90, 170, 220));
	if (pk > 0 && maxW > 1)
		dc.FillSolidRect(nx + maxW - 2, vy, 2, vh, RGB(220, 240, 255));
	dc.FillSolidRect(nx, vy, notesW, 1, RGB(50, 80, 110));
	dc.FillSolidRect(nx, vy + vh - 1, notesW, 1, RGB(16, 24, 36));
	wchar_t nt[40];
	_snwprintf_s(nt, _TRUNCATE, L"Notes %03d  MAX %03d", m_noteCount, pk);
	dc.SetTextColor(RGB(255, 255, 255));
	dc.TextOut(nx + Scale(4, dpi), Scale(21, dpi), nt);

	const int dx = nx + notesW + Scale(14, dpi);
	const int dy = Scale(20, dpi);
	const int ds = Scale(12, dpi);
	const int dg = m_drumGlow;
	COLORREF dcol = (dg > 0)
		? MmMix(RGB(70, 16, 16), RGB(255, 50, 40), dg)
		: RGB(52, 20, 20);
	dc.FillSolidRect(dx, dy, ds, ds, dcol);
	dc.FillSolidRect(dx + 1, dy + 1, ds - 2, 1, MmMix(dcol, RGB(255, 180, 160), dg > 0 ? 80 : 20));
	dc.SetTextColor(dg > 40 ? RGB(255, 230, 230) : RGB(140, 90, 90));
	dc.SelectObject(&m_fontTiny);
	dc.TextOut(dx + ds + Scale(3, dpi), dy + 1, L"DRUM");
	{
		const CSize drumSz = dc.GetTextExtent(L"DRUM");
		dc.SelectObject(&m_fontHead);
		wchar_t sBar[20], sBeat[16], sTick[24];
		_snwprintf_s(sBar, _TRUNCATE, L"%03d/%03d", m_posBar, m_posBars);
		_snwprintf_s(sBeat, _TRUNCATE, L"%d/%d", m_posBeat, m_posNum);
		_snwprintf_s(sTick, _TRUNCATE, L"%04d/%04d", m_posTick, m_posTpm);
		const int pad = Scale(5, dpi);
		const int gap = Scale(5, dpi);
		int px = dx + ds + Scale(3, dpi) + drumSz.cx + Scale(10, dpi);
		const CSize szBar = dc.GetTextExtent(sBar);
		const CSize szBeat = dc.GetTextExtent(sBeat);
		const CSize szTick = dc.GetTextExtent(sTick);
		const int bwBar = szBar.cx + pad * 2;
		const int bwBeat = szBeat.cx + pad * 2;
		const int bwTick = szTick.cx + pad * 2;
		dc.FillSolidRect(px, vy, bwBar, vh, RGB(16, 28, 16));
		dc.SetTextColor(RGB(210, 255, 210));
		dc.TextOut(px + pad, Scale(21, dpi), sBar);
		px += bwBar + gap;
		dc.FillSolidRect(px, vy, bwBeat, vh, RGB(28, 24, 12));
		dc.SetTextColor(RGB(255, 230, 150));
		dc.TextOut(px + pad, Scale(21, dpi), sBeat);
		px += bwBeat + gap;
		dc.FillSolidRect(px, vy, bwTick, vh, RGB(12, 22, 36));
		dc.SetTextColor(RGB(170, 220, 255));
		dc.TextOut(px + pad, Scale(21, dpi), sTick);
	}

	dc.SelectObject(&m_fontHead);
	dc.SetTextColor(MM_HEAD_TX);
	const wchar_t* sysN = (m_sysMode == 2) ? L"XG" : (m_sysMode == 1) ? L"GS" : L"GM";
	wchar_t ins1n[48], ins2n[48], ins3n[48], ins4n[48], revN[48], choN[48], varN[48];
	MmInsDisp(m_sysMode, 0, m_ins1, m_varPacked, m_varConn, ins1n, 48, m_gsEfxHasLsb);
	MmInsDisp(m_sysMode, 1, m_ins2, 0, 1, ins2n, 48, m_gsEfxHasLsb);
	MmInsDisp(m_sysMode, 2, m_ins3, 0, 1, ins3n, 48, m_gsEfxHasLsb);
	MmInsDisp(m_sysMode, 3, m_ins4, 0, 1, ins4n, 48, m_gsEfxHasLsb);
	if (m_sysMode == 2) {
		MmXgEffTypeName((m_revPacked >> 8) & 127, m_revPacked & 127, revN, 48);
		MmXgEffTypeName((m_choPacked >> 8) & 127, m_choPacked & 127, choN, 48);
		MmXgEffTypeName((m_varPacked >> 8) & 127, m_varPacked & 127, varN, 48);
	} else {
		wcsncpy_s(revN, MmEffName(m_sysMode, 0, m_revType), _TRUNCATE);
		wcsncpy_s(choN, MmEffName(m_sysMode, 1, m_choType), _TRUNCATE);
		if (m_sysMode == 1)
			wcsncpy_s(varN, MmEffName(1, 2, m_dlyType), _TRUNCATE);
		else
			wcsncpy_s(varN, L"—", _TRUNCATE);
	}
	wchar_t line3[480];
	if (m_sysMode == 2 && (m_ins3 != 0 || m_ins4 != 0)) {
		_snwprintf_s(line3, _TRUNCATE, L"Reverb  %s     Chorus  %s     Variation  %s     SYS  %s     INSERTION 1-4  %s / %s / %s / %s",
			revN, choN, varN, sysN, ins1n, ins2n, ins3n, ins4n);
	} else {
		_snwprintf_s(line3, _TRUNCATE, L"Reverb  %s     Chorus  %s     Variation  %s     SYS  %s     INSERTION 1/2  %s / %s",
			revN, choN, varN, sysN, ins1n, ins2n);
	}
	dc.TextOut(Scale(8, dpi), Scale(36, dpi), line3);

	dc.SelectObject(&m_fontTiny);
	dc.SetTextColor(MM_HEAD_TX);
	const int yCol = Scale(54, dpi);
	const int ySub = Scale(66, dpi);
	const int meterX = Scale(280, dpi);
	const int textRX = Scale(360, dpi);
	const int keysX = Scale(672, dpi);
	const int bw = Scale(6, dpi);
	dc.TextOut(Scale(4, dpi), yCol, L"CH#");
	{
		const int xPc = Scale(40, dpi);
		const CSize s3 = dc.GetTextExtent(L"000 ");
		dc.TextOut(xPc, yCol, L"PC#");
		dc.TextOut(xPc + s3.cx, yCol, L"BNK");
		dc.TextOut(xPc + s3.cx * 2, yCol, L"Map");
	}
	dc.TextOut(Scale(148, dpi), yCol, L"Instrument");

	/* Lev〜Var はバーが狭いので、Vibrato の Rat/Dpt と同じく2行にして交互に書く */
	{
		const int panW = Scale(8, dpi);
		const wchar_t* cap[7] = { L"Lev", L"Vol", L"Pan", L"Exp", L"Rev", L"Crs", L"Var" };
		const int xs[7] = {
			meterX, meterX + Scale(10, dpi), meterX + Scale(18, dpi),
			meterX + Scale(28, dpi), meterX + Scale(36, dpi),
			meterX + Scale(44, dpi), meterX + Scale(52, dpi)
		};
		const int ws[7] = { bw, bw, panW, bw, bw, bw, bw };
		for (int k = 0; k < 7; ++k) {
			const CSize z = dc.GetTextExtent(cap[k]);
			const int y = (k & 1) ? ySub : yCol;
			dc.TextOut(xs[k] + ws[k] / 2 - z.cx / 2, y, cap[k]);
		}
	}

	const CSize n3g = dc.GetTextExtent(L"+00 +00 +00   ");
	dc.TextOut(textRX, yCol, L"Vibrato");
	dc.TextOut(textRX + n3g.cx, yCol, L"Filter");
	dc.TextOut(textRX + n3g.cx * 2, yCol, L"Envelope");
	dc.TextOut(textRX + n3g.cx * 3, yCol, L"EQ");
	dc.TextOut(Scale(640, dpi), yCol, L"NRPN");
	dc.TextOut(keysX, yCol, L"Keyboard");

	dc.SetTextColor(RGB(70, 70, 80));
	{
		const CSize n4 = dc.GetTextExtent(L"+00 ");
		int x = textRX;
		dc.TextOut(x, ySub, L"Rat"); x += n4.cx;
		dc.TextOut(x, ySub, L"Dpt"); x += n4.cx;
		dc.TextOut(x, ySub, L"Dly");
		x = textRX + n3g.cx;
		dc.TextOut(x, ySub, L"LPF"); x += n4.cx;
		dc.TextOut(x, ySub, L"Rsn"); x += n4.cx;
		dc.TextOut(x, ySub, L"HPF");
		x = textRX + n3g.cx * 2;
		dc.TextOut(x, ySub, L"Atk"); x += n4.cx;
		dc.TextOut(x, ySub, L"Dcy"); x += n4.cx;
		dc.TextOut(x, ySub, L"Rls");
		x = textRX + n3g.cx * 3;
		dc.TextOut(x, ySub, L"Low");
		x += dc.GetTextExtent(L"0 ").cx;
		dc.TextOut(x, ySub, L"High");
	}
	if (m_frozen) {
		dc.SelectObject(&m_fontHead);
		dc.SetTextColor(RGB(255, 180, 80));
		dc.TextOut(Scale(8, dpi), Scale(4, dpi) + Scale(14, dpi),
			LL14(L"フリーズ中", L"Frozen", L"Gele", L"Congelato", L"Congelado", L"정지됨", L"已冻结", L"مجمد",
				L"Заморожено", L"Eingefroren", L"Congelado", L"Bevroren", L"Zamrozone", L"Donduruldu"));
	}
	m_showBpm = bpm;
	m_showTpc = tpc;
	m_showNotes = m_noteCount;
	m_showPeak = pk;
	m_showVol = m_masterVol;
	m_showSys = m_sysMode;
	m_showRev = m_revType;
	m_showCho = m_choType;
	m_showVar = m_varType;
	m_showRevPacked = m_revPacked;
	m_showChoPacked = m_choPacked;
	m_showVarPacked = m_varPacked;
	m_showVarConn = m_varConn;
	m_showIns1 = m_ins1;
	m_showIns2 = m_ins2;
	m_showIns3 = m_ins3;
	m_showIns4 = m_ins4;
	m_showDly = m_dlyType;
	m_showDrum = m_drumGlow;
	m_showDiv = m_division;
	m_showTsN = m_tsNum;
	m_showTsD = m_tsDen;
	m_showTransp = m_transpose;
	m_showKeySf = m_keySf;
	m_showKeyMin = m_keyMin;
	m_showBar = m_posBar;
	m_showBars = m_posBars;
	m_showBeat = m_posBeat;
	m_showTick = m_posTick;
	m_showTpm = m_posTpm;
	m_showNum = m_posNum;
	m_showFrozen = m_frozen ? 1 : 0;
	wcsncpy_s(m_showTitle, m_titleBuf, _TRUNCATE);
	dc.SelectObject(oldF);
}

void CMidiMonitorDlg::DrawPartRow(CDC& dc, int i, int y, int rowH, int w, UINT dpi, int forceKeys)
{
	const Part& p = m_part[i];
	const int meterX = Scale(280, dpi);
	const int textRX = Scale(360, dpi);
	const int keysX = Scale(672, dpi);
	
	const Part& s = m_show[i];
	const int bh = rowH - Scale(4, dpi);
	int textLDirty = (forceKeys || p.heard != s.heard || p.isDrum != s.isDrum || (p.held > 0) != (s.held > 0)
		|| p.pc != s.pc || p.bankMsb != s.bankMsb || p.bankLsb != s.bankLsb || p.mapId != s.mapId
		|| p.efxOn != s.efxOn || PartHasInsertion(i) != (int)s.insMark
		|| wcscmp(p.name, s.name) != 0
		|| MmGlowSig(p.fadeCh) != MmGlowSig(s.fadeCh)
		|| MmGlowSig(p.fadeInst) != MmGlowSig(s.fadeInst)) ? 1 : 0;
	wchar_t shownName[48];
	wcsncpy_s(shownName, p.name, _TRUNCATE);
	wchar_t plug[40] = {};
	if (g_vstHostDlg && ::IsWindow(g_vstHostDlg->GetSafeHwnd()))
		g_vstHostDlg->PartPluginName(i, plug, 40);
	if (plug[0]) {
		if (shownName[0]) {
			wchar_t tmp[48];
			_snwprintf_s(tmp, _TRUNCATE, L"%s [%s]", p.name, plug);
			wcsncpy_s(shownName, tmp, _TRUNCATE);
		} else
			wcsncpy_s(shownName, plug, _TRUNCATE);
	}
	if (wcscmp(shownName, m_plugShown[i]) != 0)
		textLDirty = 1;
		
	int metersDirty = (forceKeys || p.heard != s.heard || p.isDrum != s.isDrum || (p.held > 0) != (s.held > 0)
		|| MmLevPix(p.lev, bh) != MmLevPix(s.lev, bh)
		|| p.vol != s.vol || p.exp != s.exp || p.pan != s.pan || p.rev != s.rev || p.crs != s.crs || p.var != s.var
		|| MmGlowSig(p.glowVol) != MmGlowSig(s.glowVol) || MmGlowSig(p.glowExp) != MmGlowSig(s.glowExp)
		|| MmGlowSig(p.glowPan) != MmGlowSig(s.glowPan) || MmGlowSig(p.glowRev) != MmGlowSig(s.glowRev)
		|| MmGlowSig(p.glowCrs) != MmGlowSig(s.glowCrs) || MmGlowSig(p.glowVar) != MmGlowSig(s.glowVar)) ? 1 : 0;

	int textRDirty = (forceKeys || p.heard != s.heard || p.isDrum != s.isDrum || (p.held > 0) != (s.held > 0)
		|| p.vibRat != s.vibRat || p.vibDpt != s.vibDpt || p.vibDly != s.vibDly
		|| p.lpf != s.lpf || p.rsn != s.rsn || p.hpf != s.hpf
		|| p.atk != s.atk || p.dcy != s.dcy || p.rls != s.rls
		|| p.eqLow != s.eqLow || p.eqHigh != s.eqHigh
		|| p.nrpnMsb != s.nrpnMsb || p.nrpnLsb != s.nrpnLsb
		|| MmGlowSig(p.fadeVib) != MmGlowSig(s.fadeVib)
		|| MmGlowSig(p.fadeFilt) != MmGlowSig(s.fadeFilt)
		|| MmGlowSig(p.fadeEnv) != MmGlowSig(s.fadeEnv)
		|| MmGlowSig(p.fadeEq) != MmGlowSig(s.fadeEq)
		|| MmGlowSig(p.fadeNrpn) != MmGlowSig(s.fadeNrpn)) ? 1 : 0;

	int keysDirty = (forceKeys || keysX >= w || p.heard != s.heard || p.isDrum != s.isDrum
		|| memcmp(p.noteOn, s.noteOn, sizeof(p.noteOn)) != 0
		|| memcmp(p.noteFlash, s.noteFlash, sizeof(p.noteFlash)) != 0) ? 1 : 0;

	dc.SetBkMode(TRANSPARENT);
	CFont* oldF = dc.SelectObject(&m_fontTiny);
	const int unused = (p.heard || plug[0]) ? 0 : 1;
	const int drum = (!unused && p.isDrum) ? 1 : 0;
	const int dim = (i & 1);
	COLORREF rowBg = unused ? MM_ROW_U : (drum ? MM_ROW_D : (dim ? MM_ROW_B : MM_ROW_A));
	COLORREF tx = unused ? MM_TX_U : (drum ? MM_TX_DR : (dim ? MM_TX_D : MM_TX_W));
	COLORREF keyW = unused ? MM_KEY_U : (drum ? MM_KEY_DR : (dim ? MM_KEY_D : MM_KEY_W));
	COLORREF keyB = unused ? MM_KEYB_U : (drum ? MM_KEYB_DR : MM_KEYB);
	
	if (textLDirty) dc.FillSolidRect(0, y, meterX, rowH, rowBg);
	if (metersDirty) dc.FillSolidRect(meterX, y, textRX - meterX, rowH, rowBg);
	if (textRDirty) dc.FillSolidRect(textRX, y, keysX - textRX, rowH, rowBg);
	if (keysDirty) dc.FillSolidRect(keysX, y, w - keysX, rowH, rowBg);
	const int fadeH = rowH - 1;
	if (textLDirty) {
		MmFillFade(dc, 0, y, Scale(38, dpi), fadeH, rowBg, RGB(40, 210, 120), p.fadeCh);
		MmFillFade(dc, Scale(40, dpi), y, meterX - Scale(40, dpi), fadeH, rowBg, RGB(240, 190, 50), p.fadeInst);
	}
	if (textRDirty) {
		const int filtX = Scale(448, dpi);
		const int envX = Scale(536, dpi);
		const int eqX = Scale(624, dpi);
		const int nrpnX = Scale(640, dpi);
		MmFillFade(dc, textRX, y, filtX - textRX, fadeH, rowBg, RGB(200, 140, 255), p.fadeVib);
		MmFillFade(dc, filtX, y, envX - filtX, fadeH, rowBg, RGB(255, 160, 80), p.fadeFilt);
		MmFillFade(dc, envX, y, eqX - envX, fadeH, rowBg, RGB(180, 200, 90), p.fadeEnv);
		MmFillFade(dc, eqX, y, nrpnX - eqX, fadeH, rowBg, RGB(160, 160, 200), p.fadeEq);
		MmFillFade(dc, nrpnX, y, keysX - nrpnX, fadeH, rowBg, RGB(140, 150, 170), p.fadeNrpn);
	}

	if (textLDirty) {
		if (p.held > 0 && !unused)
			dc.SetTextColor(drum ? RGB(255, 220, 180) : RGB(255, 255, 255));
		else
			dc.SetTextColor(tx);
		wchar_t chs[8];
		_snwprintf_s(chs, _TRUNCATE, L"%c%02d", (i < 16) ? L'A' : L'B', (i % 16) + 1);
		dc.TextOut(Scale(4, dpi), y + 1, chs);
		dc.SetTextColor(tx);
		wchar_t pcb[48];
		int isXg = 0, mapId = 0, bankMsb = 0;
		MmResolveLookup(m_mapForce, m_sysMode, p.mapId, p.bankMsb, &isXg, &mapId, &bankMsb);
		const int sys = isXg ? 2 : ((mapId == 5) ? 0 : 1);
		_snwprintf_s(pcb, _TRUNCATE, L"%03d %03d %s", p.pc + 1, p.bankMsb, MmMapLabel(sys, mapId, bankMsb, p.bankLsb));
		dc.TextOut(Scale(40, dpi), y + 1, pcb);
		int nameX = Scale(148, dpi);
		if (PartHasInsertion(i)) {
			dc.SetTextColor(RGB(255, 112, 48));
			dc.TextOut(nameX, y + 1, L"◆");
			nameX += Scale(12, dpi);
		}
		dc.SetTextColor(tx);
		dc.TextOut(nameX, y + 1, shownName);
		wcsncpy_s(m_plugShown[i], shownName, _TRUNCATE);
	}

	const int by = y + Scale(2, dpi);
	const int bw = Scale(6, dpi);
	// glow 引数は常に 0。ノート中も VOL/EXP/PAN バーをフェードさせない。
	if (metersDirty) {
		DrawVBar(dc, meterX, by, bw, bh, (int)(p.lev * 127.f), 127, RGB(70, 255, 90), 0, unused);
		DrawVBar(dc, meterX + Scale(10, dpi), by, bw, bh, p.vol, 127, RGB(50, 200, 70), 0, unused || (p.vol == 100) ? 1 : 0);
		DrawPanBar(dc, meterX + Scale(18, dpi), by, Scale(8, dpi), bh, p.pan, 0, unused || (p.pan == 64) ? 1 : 0);
		DrawVBar(dc, meterX + Scale(28, dpi), by, bw, bh, p.exp, 127, RGB(50, 200, 70), 0, unused || (p.exp == 127) ? 1 : 0);
		DrawVBar(dc, meterX + Scale(36, dpi), by, bw, bh, p.rev, 127, RGB(210, 50, 50), 0, unused || (p.rev == 40) ? 1 : 0);
		DrawVBar(dc, meterX + Scale(44, dpi), by, bw, bh, p.crs, 127, RGB(80, 200, 230), 0, unused || (p.crs == 0) ? 1 : 0);
		DrawVBar(dc, meterX + Scale(52, dpi), by, bw, bh, p.var, 127, RGB(50, 80, 200), 0, unused || (p.var == 0) ? 1 : 0);
	}

	if (textRDirty) {
		const int numsLive = (p.vibRat | p.vibDpt | p.vibDly | p.lpf | p.rsn | p.hpf | p.atk | p.dcy | p.rls);
		if (unused)
			dc.SetTextColor(MM_TX_U);
		else if (drum)
			dc.SetTextColor(numsLive ? RGB(255, 200, 150) : RGB(196, 120, 88));
		else
			dc.SetTextColor(numsLive ? tx : (dim ? RGB(118, 118, 126) : RGB(140, 140, 148)));
		wchar_t num[96];
		_snwprintf_s(num, _TRUNCATE, L"%+03d %+03d %+03d   %+03d %+03d %+03d   %+03d %+03d %+03d   %d %s",
			p.vibRat, p.vibDpt, p.vibDly, p.lpf, p.rsn, p.hpf, p.atk, p.dcy, p.rls,
			p.eqLow, (p.eqHigh >= 1000) ? L"10k" : L"Hz");
		dc.TextOut(textRX, y + 1, num);
		if (unused)
			dc.SetTextColor(MM_TX_U);
		else
			dc.SetTextColor((p.nrpnMsb | p.nrpnLsb) ? tx : (drum ? RGB(168, 96, 72) : RGB(70, 72, 80)));
		wchar_t nr[16];
		_snwprintf_s(nr, _TRUNCATE, L"%02X\n%02X", p.nrpnMsb & 127, p.nrpnLsb & 127);
		CRect nrRc(Scale(640, dpi), y, keysX - Scale(4, dpi), y + rowH);
		dc.DrawText(nr, &nrRc, DT_CENTER | DT_WORDBREAK);
	}

	if (keysDirty) {
		CRect krc(keysX, y + 1, w - Scale(4, dpi), y + rowH - 2);
		DrawMiniKeys(dc, krc, p, keyW, keyB);
	}
	
	if (textLDirty || metersDirty || textRDirty || keysDirty) {
		dc.FillSolidRect(0, y + rowH - 1, w, 1, drum ? RGB(160, 72, 48) : MM_GRID);
	}
	dc.SelectObject(oldF);
	m_show[i] = m_part[i];
	m_show[i].insMark = (BYTE)(PartHasInsertion(i) ? 1 : 0);
}

int CMidiMonitorDlg::PartHasInsertion(int i) const
{
	if (i < 0 || i >= PART_MAX) return 0;
	if (!m_part[i].efxOn) return 0;
	if (m_sysMode == 1 && m_ins1 == 0) return 0;
	return 1;
}

int CMidiMonitorDlg::InsFootCount() const
{
	if (m_sysMode == 2 && (m_ins3 != 0 || m_ins4 != 0))
		return 4;
	return 2;
}

int CMidiMonitorDlg::InsPacked(int slot) const
{
	if (slot == 1) return m_ins2;
	if (slot == 2) return m_ins3;
	if (slot == 3) return m_ins4;
	return m_ins1;
}

void CMidiMonitorDlg::SyncXgInsParts()
{
	if (m_sysMode != 2) return;
	BYTE on[PART_MAX];
	memset(on, 0, sizeof(on));
	for (int s = 0; s < 4; ++s) {
		if (InsPacked(s) == 0) continue;
		const int pv = m_insBlk[s][0x0C];
		if (pv >= 0 && pv < PART_MAX) on[pv] = 1;
	}
	if (m_ins1 == 0 && m_varConn == 0 && m_varPacked != 0) {
		const int pv = m_varBlk[0x1B];
		if (pv >= 0 && pv < PART_MAX) on[pv] = 1;
	}
	for (int i = 0; i < PART_MAX; ++i) {
		if (m_part[i].efxOn == on[i]) continue;
		m_part[i].efxOn = on[i];
		m_dirtyRows |= (1u << i);
	}
}

void CMidiMonitorDlg::BuildInsLine(int slot, wchar_t* out, int outN)
{
	if (!out || outN <= 0) return;
	out[0] = 0;
	MmEnsureInsDat();
	wchar_t name[48];
	MmInsDisp(m_sysMode, slot, InsPacked(slot), m_varPacked, m_varConn, name, 48, m_gsEfxHasLsb);
	int used = 0;
	wchar_t head[80];
	_snwprintf_s(head, _TRUNCATE, L"INS%d %s", slot + 1, name);
	used = MmInsAppend(out, outN, used, head);

	int packed = InsPacked(slot);
	int fromVar = 0;
	if (slot == 0 && m_sysMode == 2 && packed == 0 && m_varConn == 0 && m_varPacked != 0) {
		packed = m_varPacked;
		fromVar = 1;
	}
	const int msb = (packed >> 8) & 127;
	const int lsb = packed & 127;
	const MmInsT* t = NULL;
	if (m_sysMode == 1) {
		if (m_gsEfxHasLsb) {
			t = MmInsFind('G', msb, lsb);
			if (!t && lsb == 0) t = MmInsFind('8', msb, 0);
		} else {
			t = MmInsFind('8', msb, 0);
			if (!t) t = MmInsFind('G', msb, lsb);
		}
	} else
		t = MmInsFind('X', msb, lsb);

	if (m_sysMode == 1 && slot == 0) {
		wchar_t parts[96];
		parts[0] = 0;
		int pu = 0;
		for (int i = 0; i < PART_MAX; ++i) {
			if (!m_part[i].efxOn) continue;
			wchar_t tag[8];
			_snwprintf_s(tag, _TRUNCATE, L"%c%02d", (i < 16) ? L'A' : L'B', (i % 16) + 1);
			if (pu > 0 && pu < 90) { parts[pu++] = L'+'; parts[pu] = 0; }
			wcsncat_s(parts, tag, _TRUNCATE);
			pu = (int)wcslen(parts);
		}
		if (parts[0])
			used = MmInsAppend(out, outN, used, parts);
		else if (packed != 0)
			used = MmInsAppend(out, outN, used, L"Part off");
		if (m_gsEfx[0x17] > 0) {
			wchar_t s[24];
			_snwprintf_s(s, _TRUNCATE, L"to Rev %d", m_gsEfx[0x17]);
			used = MmInsAppend(out, outN, used, s);
		}
		if (m_gsEfx[0x18] > 0) {
			wchar_t s[24];
			_snwprintf_s(s, _TRUNCATE, L"to Cho %d", m_gsEfx[0x18]);
			used = MmInsAppend(out, outN, used, s);
		}
		if (m_gsEfx[0x19] > 0) {
			wchar_t s[24];
			_snwprintf_s(s, _TRUNCATE, L"to Dly %d", m_gsEfx[0x19]);
			used = MmInsAppend(out, outN, used, s);
		}
		if ((m_gsEfxMask & (1u << 0x1F)) && m_gsEfx[0x1F] == 0 && packed != 0)
			used = MmInsAppend(out, outN, used, L"EQ off");
		if ((m_gsEfxMask & (1u << 0x1B)) && m_gsEfx[0x1B] > 0 && m_gsEfx[0x1B] <= 95) {
			wchar_t s[24];
			_snwprintf_s(s, _TRUNCATE, L"Ctrl CC%d", m_gsEfx[0x1B]);
			used = MmInsAppend(out, outN, used, s);
		}
		if (t) {
			for (int i = 0; i < t->np; ++i) {
				const int addr = 2 + t->p[i].n;
				if (addr < 0 || addr >= 32) continue;
				wchar_t one[48];
				if (!MmInsFmtP(t->p[i], m_gsEfx[addr], one, 48)) continue;
				used = MmInsAppend(out, outN, used, one);
			}
		}
		(void)used;
		return;
	}

	if (m_sysMode == 2) {
		int partV = 127;
		const BYTE* blk = NULL;
		if (fromVar) {
			used = MmInsAppend(out, outN, used, L"via Variation");
			partV = m_varBlk[0x1B];
		} else {
			blk = m_insBlk[slot];
			partV = blk[0x0C];
		}
		if (partV == 127)
			used = MmInsAppend(out, outN, used, L"Part off");
		else if (partV >= 0 && partV < 32) {
			wchar_t tag[16];
			_snwprintf_s(tag, _TRUNCATE, L"%c%02d", (partV < 16) ? L'A' : L'B', (partV % 16) + 1);
			used = MmInsAppend(out, outN, used, tag);
		} else if (partV < 127) {
			wchar_t tag[16];
			_snwprintf_s(tag, _TRUNCATE, L"Part %d", partV + 1);
			used = MmInsAppend(out, outN, used, tag);
		}
		if (t && blk) {
			for (int i = 0; i < t->np; ++i) {
				const int addr = 1 + t->p[i].n;
				if (addr < 0 || addr >= 48) continue;
				wchar_t one[48];
				if (!MmInsFmtP(t->p[i], blk[addr], one, 48)) continue;
				used = MmInsAppend(out, outN, used, one);
			}
		}
		(void)used;
	}
}

void CMidiMonitorDlg::DrawInsFoot(CDC& dc, int y, int w, int footH, UINT dpi)
{
	const int nLine = InsFootCount();
	for (int s = 0; s < nLine; ++s)
		BuildInsLine(s, m_insLine[s], 220);
	int lineH = (nLine > 0) ? (footH / nLine) : footH;
	if (lineH < 1) lineH = 1;
	CFont* oldF = dc.SelectObject(&m_fontTiny);
	for (int s = 0; s < nLine; ++s) {
		const int yy = y + s * lineH;
		int rh = lineH;
		if (s == nLine - 1) rh = footH - lineH * (nLine - 1);
		if (rh < 1) rh = 1;
		dc.FillSolidRect(0, yy, w, rh, (s & 1) ? RGB(14, 16, 22) : RGB(18, 20, 28));
		dc.SetBkMode(TRANSPARENT);
		dc.SetTextColor((m_insLine[s][4] && wcsstr(m_insLine[s], L"Thru") == m_insLine[s] + 5)
			? RGB(120, 124, 136) : RGB(220, 214, 190));
		dc.TextOut(Scale(8, dpi), yy + Scale(1, dpi), m_insLine[s]);
		wcsncpy_s(m_showInsLine[s], m_insLine[s], _TRUNCATE);
	}
	dc.FillSolidRect(0, y, w, 1, MM_GRID);
	dc.SelectObject(oldF);
}

int CMidiMonitorDlg::LayPartY(int i) const
{
	if (i < 0) i = 0;
	if (i > PART_MAX) i = PART_MAX;
	const int extra = (m_layExtra < 0) ? 0 : m_layExtra;
	return m_layHeadH + i * m_layRowH + ((i < extra) ? i : extra);
}

int CMidiMonitorDlg::LayPartH(int i) const
{
	if (i < 0 || i >= PART_MAX) return m_layRowH;
	return m_layRowH + ((i < m_layExtra) ? 1 : 0);
}

void CMidiMonitorDlg::DrawMonitor2D(CDC& dc, int w, int h, UINT dpi)
{
	const int headH = Scale(78, dpi);
	const int footH = Scale(14 * InsFootCount(), dpi);
	dc.FillSolidRect(0, headH, w, h - headH, MM_BG);
	DrawHeader(dc, w, headH, dpi);
	int bodyH = h - headH - footH;
	if (bodyH < PART_MAX) bodyH = PART_MAX;
	int rowH = bodyH / PART_MAX;
	if (rowH < Scale(10, dpi)) rowH = Scale(10, dpi);
	int extra = bodyH - rowH * PART_MAX;
	if (extra < 0) extra = 0;
	if (extra > PART_MAX) extra = PART_MAX;
	m_layHeadH = headH;
	m_layRowH = rowH;
	m_layFootH = footH;
	m_layExtra = extra;
	m_layW = w;
	for (int i = 0; i < PART_MAX; ++i)
		DrawPartRow(dc, i, LayPartY(i), LayPartH(i), w, dpi, 1);
	DrawInsFoot(dc, LayFootY(), w, footH, dpi);
}

// noteFlash は実時間 dt で減らす。lev/glow は 16ms ステップ（追いつき最大 4）。
void CMidiMonitorDlg::TickVisuals()
{
	const DWORD now = GetTickCount();
	int dt = 16;
	if (m_visLastMs != 0) {
		dt = (int)(now - m_visLastMs);
		if (dt < 0) dt = 0;
		if (dt > 80) dt = 80;
	}
	m_visLastMs = now;
	m_visAcc += dt;

	for (int i = 0; i < PART_MAX; ++i) {
		Part& p = m_part[i];
		int flashCh = 0;
		for (int n = 0; n < NOTE_MAX; ++n) {
			if (!p.noteFlash[n]) continue;
			int nf = (int)p.noteFlash[n] - dt;
			if (nf < 0) nf = 0;
			if ((BYTE)nf != p.noteFlash[n]) {
				p.noteFlash[n] = (BYTE)nf;
				flashCh = 1;
			}
		}
		if (flashCh)
			m_dirtyRows |= (1u << i);
	}

	int steps = 0;
	while (m_visAcc >= 16 && steps < 4) {
		m_visAcc -= 16;
		++steps;
	}
	if (steps <= 0)
		return;

	DWORD live = 0;
	int drumHit = 0;
	for (int s = 0; s < steps; ++s) {
		live = 0;
		drumHit = 0;
		for (int i = 0; i < PART_MAX; ++i) {
			Part& p = m_part[i];
			if (p.held <= 0) {
				p.lev *= 0.90f;
				if (p.lev < 0.002f) p.lev = 0;
			}
			MmGlowTick(p.glowVol);
			MmGlowTick(p.glowExp);
			MmGlowTick(p.glowPan);
			MmGlowTick(p.glowRev);
			MmGlowTick(p.glowCrs);
			MmGlowTick(p.glowVar);
			MmGlowTick(p.fadeCh);
			MmGlowTick(p.fadeInst);
			MmGlowTick(p.fadeVib);
			MmGlowTick(p.fadeFilt);
			MmGlowTick(p.fadeEnv);
			MmGlowTick(p.fadeEq);
			MmGlowTick(p.fadeNrpn);
			if (p.held > 0 && p.fadeCh < 96)
				p.fadeCh = 96;
			int flashOn = 0;
			for (int n = 0; n < NOTE_MAX; ++n) {
				if (p.noteFlash[n]) { flashOn = 1; break; }
			}
			const int busy = (p.held > 0 || flashOn || p.lev > 0.002f
				|| p.glowVol || p.glowExp || p.glowPan || p.glowRev || p.glowCrs || p.glowVar
				|| p.fadeCh || p.fadeInst || p.fadeVib || p.fadeFilt || p.fadeEnv || p.fadeEq || p.fadeNrpn);
			if (busy) live |= (1u << i);
			if (p.isDrum && (p.held > 0 || flashOn))
				drumHit = 1;
		}
		TickNotePeak();
		if (drumHit) m_drumGlow = 255;
		else if (m_drumGlow) {
			m_drumGlow = m_drumGlow * 5 / 6;
			if (m_drumGlow < 8) m_drumGlow = 0;
		}
	}
	m_rowLive = live;
}

int CMidiMonitorDlg::AppVolPercent() const
{
	/* MP の DirectSound 表示と同じ: (pos + 499) * 2 / 10 ＝ 0.2% 刻み */
	int pos = savedata.dsvol;
	if (og && og->m_dsval.GetSafeHwnd())
		pos = og->m_dsval.GetPos();
	int pct = (pos + 499) / 5;
	if (pct < 0) pct = 0;
	if (pct > 100) pct = 100;
	return pct;
}

void CMidiMonitorDlg::SetAppVolPercent(int pct)
{
	if (pct < 0) pct = 0;
	if (pct > 100) pct = 100;
	int pos = pct * 5 - 499;
	if (pos < -498) pos = -498;
	if (pos > 1) pos = 1;
	if (pos == 0) pos = 1;
	if (og && og->m_dsval.GetSafeHwnd())
		og->m_dsval.SetPos(pos);
	savedata.dsvol = pos;
	m_masterVol = pct;
	m_dirtyHead = true;
}

void CMidiMonitorDlg::PollAppVolume()
{
	const int pct = AppVolPercent();
	if (pct != m_masterVol) {
		m_masterVol = pct;
		m_dirtyHead = true;
	}
}

// ヘッダの NOTES 本数と MAX ピーク。新しいピークは hold=90（~1.4s @16ms）。
void CMidiMonitorDlg::UpdateNoteMeter()
{
	int notes = 0;
	for (int i = 0; i < PART_MAX; ++i)
		notes += m_part[i].held;
	if (notes < 0) notes = 0;
	if (notes != m_noteCount) {
		m_noteCount = notes;
		m_dirtyHead = true;
	}
	if ((float)m_noteCount > m_notesPeak) {
		m_notesPeak = (float)m_noteCount;
		m_notesPeakHold = 90;
		m_dirtyHead = true;
	}
}

// hold 中は下げない。その後 0.07/tick。速すぎると MAX がすぐ消えて「止まった」ように見える。
void CMidiMonitorDlg::TickNotePeak()
{
	UpdateNoteMeter();
	if ((float)m_noteCount > m_notesPeak) {
		m_notesPeak = (float)m_noteCount;
		m_notesPeakHold = 90;
		m_dirtyHead = true;
		return;
	}
	if (m_notesPeakHold > 0) {
		m_notesPeakHold--;
		return;
	}
	if (m_notesPeak > (float)m_noteCount + 0.02f) {
		m_notesPeak -= 0.07f;
		if (m_notesPeak < (float)m_noteCount)
			m_notesPeak = (float)m_noteCount;
		m_dirtyHead = true;
	} else {
		m_notesPeak = (float)m_noteCount;
	}
}

void CMidiMonitorDlg::LatchPart(int part, BYTE bit)
{
	if (part < 0 || part >= PART_MAX || !bit) return;
	m_latchMask[part] |= bit;
	m_latchUntil[part] = GetTickCount() + 2500;
}

bool CMidiMonitorDlg::IsLatched(int part, BYTE bit) const
{
	if (part < 0 || part >= PART_MAX || !bit) return false;
	if ((m_latchMask[part] & bit) == 0) return false;
	if ((int)(GetTickCount() - m_latchUntil[part]) >= 0) return false;
	return true;
}

// VST ホストが実際に送った鍵盤／ハード MIDI。SMF thru ではない。
void CMidiMonitorDlg::DrainLiveTap()
{
	BYTE ports[64];
	DWORD msgs[64];
	int applied = 0;
	for (;;) {
		const int n = VstLiveTapStealShorts(ports, msgs, 64);
		if (n <= 0) break;
		if (!m_frozen) {
			for (int i = 0; i < n; ++i) {
				const int part = (int)ports[i] * 16 + (int)(msgs[i] & 15);
				if (!MmLiveHostPartOn(part)) continue;
				ApplyShort((int)ports[i], msgs[i], FALSE, TRUE);
				++applied;
			}
		}
		if (n < 64) break;
	}
	for (int k = 0; k < 32; ++k) {
		int port = 0;
		BYTE sx[1024];
		const int n = VstLiveTapStealSysex(&port, sx, (int)sizeof(sx));
		if (n <= 0) break;
		if (!m_frozen) {
			ApplySysex(sx, n, port);
			++applied;
		}
	}
	if (applied && !m_frozen)
		UpdateNoteMeter();
}

void CMidiMonitorDlg::SnapshotLiveNotes()
{
	if (playb > 0 && m_ev && m_evCount > 0) return;
	int any = 0;
	for (int i = 0; i < PART_MAX; ++i) {
		if (!MmLiveHostPartOn(i)) continue;
		VstLiveActInfo a = {};
		if (!VstLiveActivity(i + 1, &a) || a.held <= 0) continue;
		const int port = i / 16;
		const int ch = i % 16;
		const int vel = a.vel > 0 ? a.vel : 64;
		for (int b = 0; b < 4; ++b) {
			unsigned m = a.mask[b];
			for (int bit = 0; bit < 32; ++bit) {
				if (!(m & (1u << bit))) continue;
				const int note = b * 32 + bit;
				ApplyShort(port, (DWORD)(0x90 | ch) | ((DWORD)note << 8) | ((DWORD)vel << 16), FALSE, TRUE);
				++any;
			}
		}
	}
	if (any)
		UpdateNoteMeter();
	MarkHostOccupiedParts();
}

void CMidiMonitorDlg::MarkHostOccupiedParts()
{
	if (m_frozen) return;
	if (!MmVstHostOpen()) return;
	for (int i = 0; i < PART_MAX; ++i) {
		if (m_part[i].heard) continue;
		if (!MmHostSlotOccupied(i)) continue;
		m_part[i].heard = 1;
		m_dirtyRows |= (1u << i);
	}
}

void CMidiMonitorDlg::InjectShort(int part, DWORD msg)
{
	if (part < 0 || part >= PART_MAX) return;
	const int port = part / 16;
	const int ch = part % 16;
	msg = (msg & ~(DWORD)0x0f) | (DWORD)ch;
	MmBindVstActiveSlot();
	if (MmVstHostOpen()) {
		MmCloseKpiLiveOut();
		VstLiveMidiShort(port, msg);
	} else if (mode == MODE_VST_MIDI) {
		MmCloseKpiLiveOut();
		VstMidiInjectShort(port, msg, -1);
	} else {
		if (!s_kpiLiveOut) {
			UINT id = MIDI_MAPPER;
			if (savedata.midiOutName[0]) {
				const UINT nd = midiOutGetNumDevs();
				for (UINT i = 0; i < nd; ++i) {
					MIDIOUTCAPS c = {};
					if (midiOutGetDevCaps(i, &c, sizeof(c)) != MMSYSERR_NOERROR) continue;
					if (_wcsicmp(c.szPname, savedata.midiOutName) == 0) { id = i; break; }
				}
			}
			if (midiOutOpen(&s_kpiLiveOut, id, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
				s_kpiLiveOut = NULL;
				if (id != MIDI_MAPPER) {
					if (midiOutOpen(&s_kpiLiveOut, MIDI_MAPPER, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
						s_kpiLiveOut = NULL;
				}
			}
			if (s_kpiLiveOut) {
				BYTE gmOn[] = { 0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7 };
				MIDIHDR hdr = {};
				hdr.lpData = (LPSTR)gmOn;
				hdr.dwBufferLength = sizeof(gmOn);
				if (midiOutPrepareHeader(s_kpiLiveOut, &hdr, sizeof(hdr)) == MMSYSERR_NOERROR) {
					midiOutLongMsg(s_kpiLiveOut, &hdr, sizeof(hdr));
					for (int w = 0; w < 20 && !(hdr.dwFlags & MHDR_DONE); ++w)
						Sleep(1);
					midiOutUnprepareHeader(s_kpiLiveOut, &hdr, sizeof(hdr));
				}
			}
		}
		if (s_kpiLiveOut)
			midiOutShortMsg(s_kpiLiveOut, msg);
	}
	ApplyShort(port, msg, TRUE);
	const int st = (int)(msg & 0xf0);
	if (st == 0xb0) {
		const int cc = (int)((msg >> 8) & 0x7f);
		BYTE bit = 0;
		if (cc == 7) bit = MM_LATCH_VOL;
		else if (cc == 10) bit = MM_LATCH_PAN;
		else if (cc == 11) bit = MM_LATCH_EXP;
		else if (cc == 91) bit = MM_LATCH_REV;
		else if (cc == 93) bit = MM_LATCH_CRS;
		else if (cc == 94) bit = MM_LATCH_VAR;
		LatchPart(part, bit);
	} else if (st == 0xc0) {
		LatchPart(part, MM_LATCH_PC);
	}
	UpdateNoteMeter();
}

void CMidiMonitorDlg::ReleasePlayNote()
{
	if (m_playPart < 0 || m_playNote < 0) return;
	const int ch = m_playPart % 16;
	const DWORD msg = (DWORD)(0x80 | ch) | ((DWORD)(m_playNote & 127) << 8);
	InjectShort(m_playPart, msg);
	m_playPart = -1;
	m_playNote = -1;
}

int CMidiMonitorDlg::KeyAt(const CRect& rc, CPoint pt) const
{
	if (!rc.PtInRect(pt) || rc.Width() < 20 || rc.Height() < 6) return -1;
	const int k0 = 21, k1 = 108;
	int whites = 0;
	for (int n = k0; n <= k1; ++n) {
		if (!MmIsBlackKey(n)) whites++;
	}
	if (whites < 1) return -1;
	const int ww = rc.Width();
	const int hh = rc.Height();
	if (pt.y < rc.top + hh * 6 / 10) {
		int wi = 0;
		for (int n = k0; n <= k1; ++n) {
			if (!MmIsBlackKey(n)) { wi++; continue; }
			int xw = rc.left + (wi * ww / whites);
			int bw = max(2, ww / whites * 6 / 10);
			int x0 = xw - bw / 2;
			if (pt.x >= x0 && pt.x < x0 + bw) return n;
		}
	}
	int wi = 0;
	for (int n = k0; n <= k1; ++n) {
		if (MmIsBlackKey(n)) continue;
		int x0 = rc.left + wi * ww / whites;
		int x1 = rc.left + (wi + 1) * ww / whites;
		if (pt.x >= x0 && pt.x < x1) return n;
		wi++;
	}
	return -1;
}

int CMidiMonitorDlg::HitMonitor(CPoint clientPt, int& part, CRect& cell) const
{
	part = -1;
	cell.SetRectEmpty();
	if (IsView3D() || m_layHeadH <= 0 || m_layRowH <= 0) return MM_HIT_NONE;
	CPoint f = clientPt;
	f.y -= CCC_GetCustomCaptionHeight(m_hWnd);
	if (!m_volBarRc.IsRectEmpty() && m_volBarRc.PtInRect(f))
		return MM_HIT_APPVOL;
	if (f.y < m_layHeadH) return MM_HIT_NONE;
	const int footY = LayFootY();
	if (f.y >= footY) return MM_HIT_NONE;
	int row = 0;
	const int y0 = f.y - m_layHeadH;
	if (m_layExtra > 0 && m_layRowH + 1 > 0 && y0 < m_layExtra * (m_layRowH + 1))
		row = y0 / (m_layRowH + 1);
	else if (m_layRowH > 0)
		row = m_layExtra + (y0 - m_layExtra * (m_layRowH + 1)) / m_layRowH;
	if (row < 0 || row >= PART_MAX) return MM_HIT_NONE;
	part = row;
	const UINT dpi = WindowDpi();
	const int y = LayPartY(row);
	const int rowH = LayPartH(row);
	const int w = m_layW;
	const int meterX = Scale(280, dpi);
	const int bh = rowH - Scale(4, dpi);
	const int by = y + Scale(2, dpi);
	const int bw = Scale(6, dpi);
	auto hitBar = [&](int x, int bwBar, CRect& out) -> bool {
		out.SetRect(x, by, x + bwBar, by + bh);
		return f.x >= x && f.x < x + bwBar && f.y >= by && f.y < by + bh;
	};
	if (hitBar(meterX + Scale(10, dpi), bw, cell)) return MM_HIT_VOL;
	if (hitBar(meterX + Scale(18, dpi), Scale(8, dpi), cell)) return MM_HIT_PAN;
	if (hitBar(meterX + Scale(28, dpi), bw, cell)) return MM_HIT_EXP;
	if (hitBar(meterX + Scale(36, dpi), bw, cell)) return MM_HIT_REV;
	if (hitBar(meterX + Scale(44, dpi), bw, cell)) return MM_HIT_CRS;
	if (hitBar(meterX + Scale(52, dpi), bw, cell)) return MM_HIT_VAR;
	CRect krc(Scale(672, dpi), y + 1, w - Scale(4, dpi), y + rowH - 2);
	if (krc.PtInRect(f)) { cell = krc; return MM_HIT_KEYS; }
	CRect pcRc(Scale(40, dpi), y, Scale(140, dpi), y + rowH);
	if (pcRc.PtInRect(f)) { cell = pcRc; return MM_HIT_PC; }
	return MM_HIT_NONE;
}

void CMidiMonitorDlg::ApplyDragValue(CPoint clientPt)
{
	if (m_dragPart < 0 || m_dragPart >= PART_MAX) return;
	const int cc = MmCcForHit(m_dragKind);
	if (cc < 0) return;
	CPoint f = clientPt;
	f.y -= CCC_GetCustomCaptionHeight(m_hWnd);
	const UINT dpi = WindowDpi();
	CRect cell;
	int part = m_dragPart;
	HitMonitor(clientPt, part, cell);
	part = m_dragPart;
	const int y = LayPartY(part);
	const int rowH = LayPartH(part);
	const int bh = rowH - Scale(4, dpi);
	const int by = y + Scale(2, dpi);
	int v = 0;
	if (m_dragKind == MM_HIT_PAN) {
		const int meterX = Scale(280, dpi);
		const int x0 = meterX + Scale(18, dpi);
		const int bw = Scale(8, dpi);
		if (bw > 0) v = (f.x - x0) * 127 / bw;
	} else {
		if (bh > 0) v = (by + bh - f.y) * 127 / bh;
	}
	if (v < 0) v = 0;
	if (v > 127) v = 127;
	const int ch = part % 16;
	const DWORD msg = (DWORD)(0xb0 | ch) | ((DWORD)cc << 8) | ((DWORD)v << 16);
	InjectShort(part, msg);
	InvalidateDirty();
}

bool CMidiMonitorDlg::HitVolBar(CPoint clientPt) const
{
	if (IsView3D() || m_volBarRc.IsRectEmpty()) return false;
	CPoint f = clientPt;
	f.y -= CCC_GetCustomCaptionHeight(m_hWnd);
	return m_volBarRc.PtInRect(f) ? true : false;
}

// 変化した行だけ Invalidate。lev/glow はピクセル量子化して比べる（生値だと毎フレーム全行）。
void CMidiMonitorDlg::InvalidateDirty()
{
	if (!::IsWindow(m_hWnd)) return;
	if (!m_fullDraw && !IsView3D()) {
		int bh = m_layRowH - 4;
		if (bh < 2) bh = 2;
		DWORD diff = 0;
		for (int i = 0; i < PART_MAX; ++i) {
			const Part& a = m_part[i];
			const Part& b = m_show[i];
			/* dt(ピッチベンド)は表に出さないので比較から外す */
			if (memcmp(&a, &b, offsetof(Part, dt)) != 0
				|| memcmp(&a.vibRat, &b.vibRat, offsetof(Part, lev) - offsetof(Part, vibRat)) != 0
				|| a.heard != b.heard
				|| MmLevPix(a.lev, bh) != MmLevPix(b.lev, bh)
				|| MmGlowSig(a.glowVol) != MmGlowSig(b.glowVol)
				|| MmGlowSig(a.glowExp) != MmGlowSig(b.glowExp)
				|| MmGlowSig(a.glowPan) != MmGlowSig(b.glowPan)
				|| MmGlowSig(a.glowRev) != MmGlowSig(b.glowRev)
				|| MmGlowSig(a.glowCrs) != MmGlowSig(b.glowCrs)
				|| MmGlowSig(a.glowVar) != MmGlowSig(b.glowVar)
				|| MmGlowSig(a.fadeCh) != MmGlowSig(b.fadeCh)
				|| MmGlowSig(a.fadeInst) != MmGlowSig(b.fadeInst)
				|| MmGlowSig(a.fadeVib) != MmGlowSig(b.fadeVib)
				|| MmGlowSig(a.fadeFilt) != MmGlowSig(b.fadeFilt)
				|| MmGlowSig(a.fadeEnv) != MmGlowSig(b.fadeEnv)
				|| MmGlowSig(a.fadeEq) != MmGlowSig(b.fadeEq)
				|| MmGlowSig(a.fadeNrpn) != MmGlowSig(b.fadeNrpn)
				|| a.efxOn != b.efxOn
				|| PartHasInsertion(i) != (int)b.insMark
				|| wcscmp(a.name, b.name) != 0
				|| memcmp(a.noteOn, b.noteOn, sizeof(a.noteOn)) != 0
				|| memcmp(a.noteFlash, b.noteFlash, sizeof(a.noteFlash)) != 0)
				diff |= (1u << i);
		}
		m_dirtyRows = diff;
		int bpm = 0;
		if (m_usecQn > 0)
			bpm = (int)((60000000.0 / (double)m_usecQn) + 0.5);
		if (bpm < 1) bpm = savedata.mpDetectedBpm;
		int tpc = tempo;
		if (tpc < 1) tpc = 100;
		int transp = (pitch != 0) ? (int)((double)pitch / 100.0 + (pitch > 0 ? 0.5 : -0.5)) : 0;
		const int pk = (int)(m_notesPeak + 0.5f);
		m_dirtyHead = (bpm != m_showBpm || tpc != m_showTpc || m_noteCount != m_showNotes
			|| pk != m_showPeak || m_masterVol != m_showVol || m_sysMode != m_showSys
			|| m_revType != m_showRev || m_choType != m_showCho || m_varType != m_showVar
			|| m_revPacked != m_showRevPacked || m_choPacked != m_showChoPacked
			|| m_varPacked != m_showVarPacked || m_varConn != m_showVarConn
			|| m_ins1 != m_showIns1 || m_ins2 != m_showIns2 || m_ins3 != m_showIns3 || m_ins4 != m_showIns4
			|| m_dlyType != m_showDly || m_drumGlow != m_showDrum
			|| m_division != m_showDiv || m_tsNum != m_showTsN || m_tsDen != m_showTsD
			|| transp != m_showTransp || m_keySf != m_showKeySf || m_keyMin != m_showKeyMin
			|| (m_frozen ? 1 : 0) != m_showFrozen
			|| m_posBar != m_showBar || m_posBars != m_showBars || m_posBeat != m_showBeat
			|| m_posTick != m_showTick || m_posTpm != m_showTpm || m_posNum != m_showNum
			|| wcscmp(m_titleBuf, m_showTitle) != 0);
		if (!m_dirtyHead) {
			const int nLine = InsFootCount();
			for (int s = 0; s < nLine; ++s) {
				wchar_t ln[220];
				BuildInsLine(s, ln, 220);
				if (wcscmp(ln, m_showInsLine[s]) != 0) {
					m_dirtyHead = true;
					break;
				}
			}
		}
	}
	if (m_fullDraw || IsView3D()) {
		Invalidate(FALSE);
		return;
	}
	if (!m_dirtyHead && m_dirtyRows == 0)
		return;
	CRect rc;
	GetClientRect(&rc);
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	const int w = rc.Width();
	CRect acc;
	int any = 0;
	if (m_dirtyHead && m_layHeadH > 0) {
		acc.SetRect(0, capH, w, capH + m_layHeadH);
		any = 1;
		if (m_layFootH > 0 && m_layRowH > 0) {
			const int fy = capH + LayFootY();
			CRect fr(0, fy, w, fy + m_layFootH);
			acc.UnionRect(&acc, &fr);
		}
	}
	if (m_layRowH > 0 && m_layHeadH > 0) {
		for (int i = 0; i < PART_MAX; ++i) {
			if ((m_dirtyRows & (1u << i)) == 0) continue;
			const int y = capH + LayPartY(i);
			CRect r(0, y, w, y + LayPartH(i));
			if (!any) { acc = r; any = 1; }
			else acc.UnionRect(&acc, &r);
		}
	} else {
		Invalidate(FALSE);
		return;
	}
	if (any)
		InvalidateRect(&acc, FALSE);
}

void CMidiMonitorDlg::DrawMonitor3D(CDC& dc, int w, int h)
{
	dc.FillSolidRect(0, 0, w, h, MM_BG);
	GdiSoft3D::View v;
	const float boxes[1][6] = { { -1.25f, 1.25f, -0.05f, 0.85f, -0.05f, 1.15f } };
	GdiSoft3D::BuildView(w, h, m_cam, boxes, 1, v);
	GdiSoft3D::Context ctx;
	if (!ctx.Create(w, h)) return;
	ctx.view = v;
	ctx.SetFog(GdiSoft3D::FogLinear, MM_BG, 0.05f, 1.2f, 0.7f);
	ctx.BeginFrame(MM_BG);
	ctx.DrawGrid(-1.1f, 1.1f, 0.0f, 1.05f, 0.0f, 6, RGB(40, 44, 58));
	for (int i = 0; i < PART_MAX; ++i) {
		const int bank = i / 16;
		const int col = i % 16;
		const float x0 = -1.00f + col * (2.00f / 16.f);
		const float x1 = x0 + (2.00f / 16.f) * 0.72f;
		const float z0 = (bank == 0) ? 0.18f : 0.58f;
		const float z1 = z0 + 0.28f;
		float lv = m_part[i].lev;
		if (lv < 0.04f) lv = 0.04f;
		const float y = 0.08f + lv * 0.70f;
		COLORREF c;
		if (!m_part[i].heard && !MmHostSlotOccupied(i))
			c = RGB(72, 72, 78);
		else if (m_part[i].isDrum)
			c = RGB(255, 140, 64);
		else if (i & 1)
			c = RGB(196, 198, 206);
		else
			c = RGB(236, 238, 244);
		if (m_part[i].held > 0) c = RGB(255, 80, 80);
		ctx.DrawBox(x0, x1, y, z0, z1, c, 0.f);
	}
	ctx.EndFrame();
	ctx.Present(dc, 0, 0);
	if (m_soft3dTourUntil != 0 && GetTickCount() < m_soft3dTourUntil) {
		dc.SetBkMode(TRANSPARENT);
		dc.SetTextColor(RGB(240, 245, 255));
		dc.TextOut(8, 8, LL14(
			L"ドラッグ=回転 ホイール=ズーム 0=リセット",
			L"Drag=rotate  Wheel=zoom  0=reset",
			L"Glisser=rotation  Molette=zoom  0=reinit.",
			L"Trascina=ruota  Rotella=zoom  0=reset",
			L"Arrastrar=rotar  Rueda=zoom  0=restablecer",
			L"드래그=회전  휠=줌  0=리셋",
			L"拖动=旋转  滚轮=缩放  0=重置",
			L"سحب=دوران  عجلة=تكبير  0=إعادة",
			L"Перетащ.=поворот  Колесо=масштаб  0=сброс",
			L"Ziehen=drehen  Rad=Zoom  0=Reset",
			L"Arrastar=girar  Roda=zoom  0=redefinir",
			L"Sleep=draaien  Wiel=zoom  0=reset",
			L"Przeciagnij=obrot  Kolo=zoom  0=reset",
			L"Surukle=don  Tekerlek=zoom  0=sifirla"));
	}
	if ((savedata.soft3dPerfHintDismiss & 16) == 0) {
		static DWORD s_t0 = 0;
		static int s_slow = 0;
		static DWORD s_hint = 0;
		const DWORD now = GetTickCount();
		if (s_t0 != 0) {
			if (now - s_t0 >= 32) { if (++s_slow >= 40) { s_slow = 0; s_hint = now + 4000; } }
			else if (s_slow > 0) --s_slow;
		}
		s_t0 = now;
		if (now < s_hint) {
			dc.SetBkMode(TRANSPARENT);
			dc.SetTextColor(RGB(255, 190, 160));
			dc.TextOut(8, 26, LL14(
				L"重いときは右クリックから 2D に戻せます",
				L"Feeling heavy? Switch back to 2D from the context menu",
				L"Trop lourd ? Revenez en 2D via le menu contextuel",
				L"Troppo pesante? Torna al 2D dal menu contestuale",
				L"¿Va lento? Vuelva a 2D desde el menú contextual",
				L"무거우면 우클릭에서 2D로 돌릴 수 있습니다",
				L"若觉得卡，可从右键菜单切回 2D",
				L"ثقيل؟ عد إلى 2D من قائمة السياق",
				L"Тяжело? Вернитесь в 2D через контекстное меню",
				L"Zu zäh? Über das Kontextmenü zurück zu 2D",
				L"Pesado? Volte ao 2D pelo menu de contexto",
				L"Te zwaar? Ga terug naar 2D via het contextmenu",
				L"Za ciężko? Wróć do 2D z menu kontekstowego",
				L"Ağır mı? Bağlam menüsünden 2B'ye dönün"));
		}
	}
}

BOOL CMidiMonitorDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	SetWindowText(LL14(
		L"MIDIモニタ", L"MIDI Monitor", L"Moniteur MIDI", L"Monitor MIDI", L"Monitor MIDI",
		L"MIDI 모니터", L"MIDI监视器", L"مراقب MIDI", L"MIDI-монитор", L"MIDI-Monitor",
		L"Monitor MIDI", L"MIDI-monitor", L"Monitor MIDI", L"MIDI izleyici"));
	ModifyStyle(WS_MINIMIZEBOX, 0);
	ModifyStyleEx(0, WS_EX_DLGMODALFRAME, SWP_FRAMECHANGED);

	m_viewMode = (savedata.midimonviewmode == 1) ? 1 : 0;
	m_alwaysOnTop = (savedata.midimontopmost != 0);
	SyncSoft3DFromSave();
	ResetParts();
	MmEnsureDat();
	PollAppVolume();

	const UINT dpi = WindowDpi();
	int dw = Scale(1180, dpi), dh = Scale(720, dpi);
	if (savedata.midimonx != -1 && savedata.midimonw > 200 && savedata.midimonh > 160)
		SetWindowPos(m_alwaysOnTop ? &CWnd::wndTopMost : &CWnd::wndTop,
			savedata.midimonx, savedata.midimony, savedata.midimonw, savedata.midimonh,
			SWP_NOOWNERZORDER | (m_alwaysOnTop ? 0 : SWP_NOZORDER));
	else
		SetWindowPos(m_alwaysOnTop ? &CWnd::wndTopMost : &CWnd::wndTop,
			80, 80, dw, dh,
			SWP_NOOWNERZORDER | (m_alwaysOnTop ? 0 : SWP_NOZORDER));

	m_help.SetWindowText(L"?");
	m_help.SetFlat(TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	LayoutHelpBtn();
	if (m_tooltip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX)) {
		if (m_help.GetSafeHwnd())
			m_tooltip.AddTool(&m_help, LL14(L"操作ガイドを表示", L"Show operation guide", L"Afficher le guide", L"Mostra guida", L"Mostrar guía", L"조작 가이드 표시", L"显示操作指南", L"إظهار الدليل", L"Показать руководство", L"Bedienungsanleitung", L"Mostrar guia", L"Handleiding tonen", L"Pokaż przewodnik", L"İşlem kılavuzunu göster"));
		m_tooltip.SetDelayTime(TTDT_INITIAL, 400);
		m_tooltip.SetDelayTime(TTDT_RESHOW, 120);
		m_tooltip.SetDelayTime(TTDT_AUTOPOP, 12000);
		m_tooltip.SendMessage(TTM_SETMAXTIPWIDTH, 0, 460);
		m_tooltip.Activate(TRUE);
	}
	EnableToolTips(TRUE);
	EnableMainWindowLock(&savedata.midimonMainLock, TRUE);
	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();
	SetTimer(1, 16, nullptr); // 本体。PersistPos もここ
	SetTimer(2, 4, nullptr);  // IdlePulse。OnIdle と同じ入口
	LoadCurrentMidi();
	return TRUE;
}

void CMidiMonitorDlg::OnPaint()
{
	CPaintDC dc(this);
	if (m_paintDisabled) return;
	CRect rect;
	GetClientRect(&rect);
	const int w = rect.Width();
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	const int h = rect.Height() - capH;
	if (w <= 0 || h <= 0) {
		CCC_CaptionPaintGdi(dc, m_hWnd);
		return;
	}

	const UINT dpi = WindowDpi();
	if (m_fontDpi != (int)dpi || !m_fontTiny.GetSafeHandle()) {
		if (m_fontHead.GetSafeHandle()) m_fontHead.DeleteObject();
		if (m_fontCell.GetSafeHandle()) m_fontCell.DeleteObject();
		if (m_fontTiny.GetSafeHandle()) m_fontTiny.DeleteObject();
		LOGFONT lf = {};
		lf.lfHeight = -Scale(12, dpi);
		lf.lfWeight = FW_BOLD;
		wcscpy_s(lf.lfFaceName, L"MS Gothic");
		m_fontHead.CreateFontIndirect(&lf);
		lf.lfHeight = -Scale(11, dpi);
		lf.lfWeight = FW_NORMAL;
		m_fontCell.CreateFontIndirect(&lf);
		lf.lfHeight = -Scale(10, dpi);
		m_fontTiny.CreateFontIndirect(&lf);
		m_fontDpi = (int)dpi;
		m_fullDraw = true;
	}

	if (!EnsureFrameBuffer(dc, w, h) || !m_frameDC.GetSafeHdc()) {
		dc.FillSolidRect(0, capH, w, h, MM_BG);
		CCC_CaptionPaintGdi(dc, m_hWnd);
		return;
	}

	const bool full = m_fullDraw || IsView3D();
	if (full) {
		if (IsView3D()) {
			DrawMonitor3D(m_frameDC, w, h);
			for (int i = 0; i < PART_MAX; ++i)
				m_show[i] = m_part[i];
		} else
			DrawMonitor2D(m_frameDC, w, h, dpi);
		m_fullDraw = false;
		m_dirtyRows = 0;
		m_dirtyHead = false;
	} else {
		if (m_layHeadH <= 0 || m_layRowH <= 0)
			DrawMonitor2D(m_frameDC, w, h, dpi);
		else {
			if (m_dirtyHead) {
				DrawHeader(m_frameDC, w, m_layHeadH, dpi);
				if (m_layFootH > 0)
					DrawInsFoot(m_frameDC, LayFootY(), w, m_layFootH, dpi);
			}
			for (int i = 0; i < PART_MAX; ++i) {
				if (m_dirtyRows & (1u << i))
					DrawPartRow(m_frameDC, i, LayPartY(i), LayPartH(i), w, dpi, 0);
			}
		}
		m_dirtyRows = 0;
		m_dirtyHead = false;
	}

	CRect pr = dc.m_ps.rcPaint;
	if (pr.IsRectEmpty()) {
		pr.SetRect(0, capH, w, capH + h);
	}
	const int paintCap = (pr.top < capH) ? 1 : 0;
	int sx = pr.left;
	int sy = pr.top - capH;
	int sw = pr.Width();
	int sh = pr.Height();
	if (sy < 0) { sh += sy; sy = 0; }
	if (sx < 0) { sw += sx; sx = 0; }
	if (sx + sw > w) sw = w - sx;
	if (sy + sh > h) sh = h - sy;
	if (sw <= 0 || sh <= 0) {
		if (paintCap)
			CCC_CaptionPaintGdi(dc, m_hWnd);
		return;
	}

#if CCUSTOM_AERO_SUPPORT
	const bool bodyAero = (savedata.aero == 1 && CCC_IsWin11());
	const bool capGlassBody = (!bodyAero && CCC_AcrylicCaption(m_hWnd) && CCC_IsWin11());
	if (bodyAero || capGlassBody) {
		if (m_chromaW != w || m_chromaH != h) {
			m_chromaCache.Release();
			m_chromaReady = false;
			m_chromaW = w;
			m_chromaH = h;
		}
		if (m_chromaCache.Ensure(dc.GetSafeHdc(), w, h)) {
			if (bodyAero)
				m_chromaCache.UpdateRect(m_frameDC.GetSafeHdc(), 0, 0, 0, 0, w, h, MM_CHROMA);
			else
				m_chromaCache.UpdateOpaqueRect(m_frameDC.GetSafeHdc(), 0, 0, 0, 0, w, h);
			m_chromaReady = true;
			m_chromaCache.BlitFull(dc.GetSafeHdc(), 0, capH, w, h);
			if (paintCap)
				CCC_CaptionPaintGdi(dc, m_hWnd);
			return;
		}
	}
	if (!CCC_IsAeroEnabled() && CCC_AcrylicCaption(m_hWnd) && CCC_IsWin11()) {
		CCC_BlitStretchOpaque(dc.GetSafeHdc(), 0, capH, w, h,
			m_frameDC.GetSafeHdc(), 0, 0, w, h);
		if (paintCap)
			CCC_CaptionPaintGdi(dc, m_hWnd);
		return;
	}
#endif
	dc.BitBlt(sx, capH + sy, sw, sh, &m_frameDC, sx, sy, SRCCOPY);
	if (paintCap)
		CCC_CaptionPaintGdi(dc, m_hWnd);
}

BOOL CMidiMonitorDlg::OnEraseBkgnd(CDC* pDC)
{
	UNREFERENCED_PARAMETER(pDC);
	return TRUE;
}

// 1=16ms 本体。2=4ms 追加パルス（OnIdle と同じ IdlePulse）。
void CMidiMonitorDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == 1) {
		if (++m_persistAge >= 32) {
			PersistPos();
			m_persistAge = 0;
		}
		PumpIdle();
	} else if (nIDEvent == 2) {
		IdlePulse();
	}
	CCustomBlurDialogExBase::OnTimer(nIDEvent);
}

void CMidiMonitorDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	if (nType == SIZE_MINIMIZED) {
		ShowWindow(SW_HIDE);
		return;
	}
	ReleasePaintBuffers();
#if CCUSTOM_AERO_SUPPORT
	if (CCC_IsAeroEnabled())
		CCC_RefreshDwmBlur(m_hWnd);
#endif
	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();
	Invalidate(FALSE);
}

// タスクバー／システムメニューの最小化はアイコン化せず隠す（所有ポップアップの残骸防止）。
void CMidiMonitorDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == SC_MINIMIZE) {
		ShowWindow(SW_HIDE);
		return;
	}
	CDialogEx::OnSysCommand(nID, lParam);
}

void CMidiMonitorDlg::OnMove(int x, int y)
{
	CCustomBlurDialogExBase::OnMove(x, y);
}

void CMidiMonitorDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CCustomBlurDialogExBase::OnShowWindow(bShow, nStatus);
	if (bShow) {
		LoadCurrentMidi();
		SnapshotLiveNotes();
		MarkHostOccupiedParts();
		PollAppVolume();
		Invalidate(FALSE);
	}
}

void CMidiMonitorDlg::OnClose()
{
	DetachForDestroy();
	savedata.midimonwindow = 0;
	DestroyWindow();
	extern CMediaPlayerDlg* mp;
	if (mp && ::IsWindow(mp->GetSafeHwnd()))
		mp->SyncPushToggleButtons();
}

void CMidiMonitorDlg::OnDestroy()
{
	KillTimer(1);
	KillTimer(2);
	ReleasePlayNote();
	PersistPos();
	CCC_CaptionUnregister(m_hWnd);
	CCustomBlurDialogExBase::OnDestroy();
}

void CMidiMonitorDlg::DetachForDestroy()
{
	m_paintDisabled = true;
	KillTimer(1);
	KillTimer(2);
	PersistPos();
	ReleasePaintBuffers();
}

void CMidiMonitorDlg::ResetPlaybackState()
{
	ResetParts();
	m_lastPlayb = -1;
	m_hearPlayb = -1;
	m_pbAnchor = 0;
	m_pbQpc = 0;
	m_visAcc = 0;
	m_visLastMs = 0;
	m_evPos = 0;
	m_hadNote = 0;
	m_loadedPath[0] = 0;
	if (::IsWindow(m_hWnd))
		Invalidate(FALSE);
}

// キュー消化。描画は InvalidateDirty まで。3D の追加パルスは IdlePulse 側で弾く。
void CMidiMonitorDlg::PumpIdle()
{
	if (!::IsWindow(m_hWnd) || m_paintDisabled) return;
	if (IsIconic() || !IsWindowVisible()) return;
	if (m_playNote >= 0 && ::GetCapture() != m_hWnd)
		ReleasePlayNote();
	if (!m_frozen) {
		SyncFromPlayback();
		DrainLiveTap();
		MarkHostOccupiedParts();
		TickVisuals();
	} else {
		DrainLiveTap();
	}
	if (!m_volDragging)
		PollAppVolume();
	InvalidateDirty();
}

// pbHeard が前回と同じ（サンプルが進んでいない）ときだけ QPC で先読み。
// 一時停止 (playy==0) と巻き戻しは補間しない。行き過ぎは sr/40 で切る。
void CMidiMonitorDlg::ExtrapolateHeard(__int64& pbHeard)
{
	LONGLONG now = 0;
	MmQpcPair(m_pbFreq, now);
	if (playy == 0) {
		m_pbAnchor = pbHeard;
		m_pbQpc = now;
		return;
	}
	if (pbHeard < m_pbAnchor) {
		m_pbAnchor = pbHeard;
		m_pbQpc = now;
		return;
	}
	if (pbHeard > m_pbAnchor) {
		m_pbAnchor = pbHeard;
		m_pbQpc = now;
		return;
	}
	const __int64 sr = (m_sampleRate > 0) ? m_sampleRate : 44100;
	__int64 cap = sr / 40;
	if (cap < 1) cap = 1;
	LONGLONG dq = now - m_pbQpc;
	if (dq < 0) dq = 0;
	__int64 extra = dq * sr / m_pbFreq;
	if (extra > cap) extra = cap;
	pbHeard += extra;
}

// キー/マウスボタン待ちがあるときは触らない。QS_POSTMESSAGE は見ない（timerp でstarve）。
// 前面 ~4ms、裏 ~16ms。idle<8% ならスキップ。UpdateWindow は dirty のときだけ。
void CMidiMonitorDlg::IdlePulse()
{
	if (!::IsWindow(m_hWnd) || m_paintDisabled) return;
	if (IsIconic() || !IsWindowVisible()) return;
	if (IsView3D()) return;
	if (::GetQueueStatus(QS_KEY | QS_MOUSEBUTTON | QS_HOTKEY))
		return;

	HWND fg = ::GetForegroundWindow();
	DWORD fgPid = 0;
	if (fg)
		::GetWindowThreadProcessId(fg, &fgPid);
	const int ours = (fgPid == GetCurrentProcessId()) ? 1 : 0;
	const int minMs = ours ? 4 : 16;

	LONGLONG now = 0;
	MmQpcPair(m_pbFreq, now);
	if (m_idleLastQpc != 0) {
		const LONGLONG elapsedMs = (now - m_idleLastQpc) * 1000 / m_pbFreq;
		if (elapsedMs < minMs)
			return;
	}
	if (!MmCpuHasSlack())
		return;

	m_idleLastQpc = now;
	PumpIdle();
	CRect ur;
	if (GetUpdateRect(&ur, FALSE))
		UpdateWindow();
	SwitchToThread();
}

// timerp 用。同期は毎ティック。描画の間引きは呼び出し側の Ms2DrawDue。
void CMidiMonitorDlg::PumpSyncNow()
{
	PumpIdle();
}

void CMidiMonitorDlg::PersistPos()
{
	if (!::IsWindow(m_hWnd) || IsIconic()) return;
	CRect rc; GetWindowRect(&rc);
	savedata.midimonx = rc.left;
	savedata.midimony = rc.top;
	savedata.midimonw = rc.Width();
	savedata.midimonh = rc.Height();
}

void CMidiMonitorDlg::SyncSoft3DFromSave()
{
	GdiSoft3D::CamFromSaved(m_cam, savedata.midimon3dyaw, savedata.midimon3dpitch, savedata.midimon3dzoom);
}

void CMidiMonitorDlg::PersistSoft3D()
{
	GdiSoft3D::CamToSaved(m_cam, savedata.midimon3dyaw, savedata.midimon3dpitch, savedata.midimon3dzoom);
}

void CMidiMonitorDlg::PaletteApplySoft3D()
{
	m_viewMode = (savedata.midimonviewmode == 1) ? 1 : 0;
	SyncSoft3DFromSave();
	if (::IsWindow(m_hWnd))
		Invalidate(FALSE);
}

void CMidiMonitorDlg::LayoutHelpBtn()
{
	CCC_CaptionPlaceHelpBtn(m_hWnd, &m_help);
}

void CMidiMonitorDlg::ShowHelpSheet()
{
	if (g_mmHelpDlg && ::IsWindow(g_mmHelpDlg->GetSafeHwnd())) {
		CCC_PresentOwnedHelp(g_mmHelpDlg, this);
		return;
	}
	if (g_mmHelpDlg && !::IsWindow(g_mmHelpDlg->GetSafeHwnd()))
		g_mmHelpDlg = nullptr;
	CMmHelpDlg* dlg = new CMmHelpDlg(this);
	if (!dlg->Create(IDD_MM_HELP, this)) {
		delete dlg;
		return;
	}
	g_mmHelpDlg = dlg;
	CCC_PresentOwnedHelp(dlg, this);
}

void CMidiMonitorDlg::OnBnClickedHelp()
{
	ShowHelpSheet();
}

void CMidiMonitorDlg::OnContextMenu(CWnd* /*pWnd*/, CPoint point)
{
	int ctxPart = m_hoverPart;
	{
		CPoint cl = point;
		if (cl.x != -1 || cl.y != -1) {
			ScreenToClient(&cl);
			int hp = -1;
			CRect cell;
			HitMonitor(cl, hp, cell);
			if (hp >= 0) ctxPart = hp;
		}
	}
	CCustomPopupMenu menu;
	menu.SetAeroMode(FALSE);
	CCustomPopupMenu* view = menu.AddSubMenu(
		LL14(L"表示モード", L"View mode", L"Mode d'affichage", L"Modalita", L"Modo", L"표시 모드", L"显示模式", L"وضع العرض", L"Режим", L"Ansicht", L"Modo", L"Weergave", L"Tryb", L"Gorunum"),
		LL14(L"通常2Dと簡易3Dを切り替えます", L"Switch between normal 2D and Soft 3D", L"Basculer 2D / Soft 3D", L"Passa 2D / Soft 3D", L"Cambiar 2D / Soft 3D", L"일반 2D와 간이 3D 전환", L"在普通2D与简易3D间切换", L"التبديل بين 2D و Soft 3D", L"Переключение 2D / Soft 3D", L"Zwischen 2D und Soft 3D wechseln", L"Alternar 2D / Soft 3D", L"Wisselen 2D / Soft 3D", L"Przelacz 2D / Soft 3D", L"2D / Soft 3B degistir"));
	if (view) {
		view->AddCheck(IDM_MM_VIEW_2D, LL14(L"通常 (2D)", L"Normal (2D)", L"Normal (2D)", L"Normale (2D)", L"Normal (2D)", L"일반 (2D)", L"普通 (2D)", L"عادي (2D)", L"Обычный (2D)", L"Normal (2D)", L"Normal (2D)", L"Normaal (2D)", L"Zwykly (2D)", L"Normal (2D)"), !IsView3D());
		view->AddCheck(IDM_MM_VIEW_3D, LL14(L"簡易3D", L"Soft 3D", L"3D simplifie", L"3D semplificato", L"3D simple", L"간이 3D", L"简易3D", L"Soft 3D", L"Простой 3D", L"Einfaches 3D", L"3D simples", L"Eenvoudig 3D", L"Uproszczone 3D", L"Basit 3B"), IsView3D());
		if (IsView3D()) {
			int yaw10 = (int)(m_cam.yawDeg * 10.f);
			int pit10 = (int)(m_cam.pitchDeg * 10.f);
			int zoomPct = (int)(m_cam.zoom * 100.f + 0.5f);
			view->AddSeparator();
			view->AddSlider(LL14(L"Yaw (0.1°)", L"Yaw (0.1°)", L"Lacet (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"偏航 (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)"),
				-1800, 1800, yaw10, [](void* ctx, int v) {
					auto* self = (CMidiMonitorDlg*)ctx;
					self->m_cam.yawDeg = (float)v / 10.f;
					GdiSoft3D::ClampCam(self->m_cam);
					self->PersistSoft3D();
					self->Invalidate(FALSE);
				}, this);
			view->AddSlider(LL14(L"Pitch (0.1°)", L"Pitch (0.1°)", L"Tangage (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"俯仰 (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)"),
				-850, 850, pit10, [](void* ctx, int v) {
					auto* self = (CMidiMonitorDlg*)ctx;
					self->m_cam.pitchDeg = (float)v / 10.f;
					GdiSoft3D::ClampCam(self->m_cam);
					self->PersistSoft3D();
					self->Invalidate(FALSE);
				}, this);
			view->AddSlider(LL14(L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"缩放 (%)", L"تكبير (%)", L"Масштаб (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)"),
				35, 400, zoomPct, [](void* ctx, int v) {
					auto* self = (CMidiMonitorDlg*)ctx;
					self->m_cam.zoom = (float)v / 100.f;
					GdiSoft3D::ClampCam(self->m_cam);
					self->PersistSoft3D();
					self->Invalidate(FALSE);
				}, this);
			view->AddCommand(IDM_MM_CAM_RESET,
				LL14(L"視点をリセット", L"Reset view", L"Reinitialiser la vue", L"Reimposta vista", L"Restablecer vista", L"시점 재설정", L"重置视角", L"إعادة ضبط العرض", L"Сбросить вид", L"Ansicht zuruecksetzen", L"Redefinir vista", L"Weergave resetten", L"Resetuj widok", L"Gorunumu sifirla"));
		}
	}
	CCustomPopupMenu* ops = menu.AddSubMenu(
		LL14(L"操作", L"Actions", L"Actions", L"Azioni", L"Acciones", L"조작", L"操作", L"إجراءات", L"Действия", L"Aktionen", L"Acoes", L"Acties", L"Akcje", L"Islemler"),
		LL14(L"フリーズ、最前面、状態コピー、再読込", L"Freeze, always on top, copy state, reload", L"Gel, premier plan, copier, recharger", L"Congela, primo piano, copia, ricarica", L"Congelar, siempre visible, copiar, recargar", L"프리즈, 항상 위, 상태 복사, 다시 읽기", L"冻结、置顶、复制状态、重新读取", L"تجميد، أعلى، نسخ، إعادة تحميل", L"Заморозка, поверх, копия, перечитать", L"Einfrieren, immer oben, kopieren, neu laden", L"Congelar, topo, copiar, recarregar", L"Bevriezen, bovenop, kopieren, herladen", L"Zamroz, na wierzchu, kopiuj, wczytaj ponownie", L"Dondur, ustte, kopyala, yeniden yukle"));
	if (ops) {
		ops->AddCheck(IDM_MM_FREEZE, LL14(L"フリーズ", L"Freeze", L"Gel", L"Congela", L"Congelar", L"정지", L"冻结", L"تجميد", L"Заморозка", L"Einfrieren", L"Congelar", L"Bevriezen", L"Zamroz", L"Dondur"), m_frozen);
		ops->AddCheck(IDM_MM_TOPMOST, LL14(L"常に手前に表示", L"Always on top", L"Toujours au premier plan", L"Sempre in primo piano", L"Siempre visible", L"항상 위", L"始终置顶", L"دائماً أعلى", L"Поверх всех окон", L"Immer im Vordergrund", L"Sempre no topo", L"Altijd boven", L"Zawsze na wierzchu", L"Her zaman ustte"), m_alwaysOnTop);
		ops->AddCommand(IDM_MM_COPY, LL14(L"状態をコピー", L"Copy state", L"Copier l'etat", L"Copia stato", L"Copiar estado", L"상태 복사", L"复制状态", L"نسخ الحالة", L"Копировать состояние", L"Zustand kopieren", L"Copiar estado", L"Status kopieren", L"Kopiuj stan", L"Durumu kopyala"));
		ops->AddCommand(IDM_MM_RELOAD, LL14(L"MIDIを再読込", L"Reload MIDI", L"Recharger MIDI", L"Ricarica MIDI", L"Recargar MIDI", L"MIDI 다시 읽기", L"重新读取 MIDI", L"إعادة تحميل MIDI", L"Перечитать MIDI", L"MIDI neu laden", L"Recarregar MIDI", L"MIDI herladen", L"Wczytaj MIDI ponownie", L"MIDI'yi yeniden yukle"));
		CCustomPopupMenu* drum = ops->AddSubMenu(
			LL14(L"ドラムモード", L"Drum mode", L"Mode batterie", L"Modo batteria", L"Modo bateria", L"드럼 모드", L"鼓组模式", L"وضع الطبل", L"Режим ударных", L"Drum-Modus", L"Modo bateria", L"Drummodus", L"Tryb perkusji", L"Davul modu"),
			LL14(L"GS USE FOR RHYTHM / XG ドラムバンクを送る", L"Send GS USE FOR RHYTHM / XG drum bank", L"Envoyer GS USE FOR RHYTHM / banque XG", L"Invia GS USE FOR RHYTHM / banco XG", L"Enviar GS USE FOR RHYTHM / banco XG", L"GS USE FOR RHYTHM / XG 드럼 뱅크 전송", L"发送 GS USE FOR RHYTHM / XG 鼓组库", L"إرسال GS USE FOR RHYTHM / بنك XG", L"Послать GS USE FOR RHYTHM / банк XG", L"GS USE FOR RHYTHM / XG-Drum-Bank senden", L"Enviar GS USE FOR RHYTHM / banco XG", L"GS USE FOR RHYTHM / XG-drumbank sturen", L"Wyslij GS USE FOR RHYTHM / bank XG", L"GS USE FOR RHYTHM / XG davul bankasi gonder"));
		if (drum) {
			drum->AddCommand(IDM_MM_DRUM_PART, LL14(L"このパートをドラムにする", L"Make this part drums", L"Mettre cette partie en batterie", L"Rendi questa parte batteria", L"Poner esta parte en bateria", L"이 파트를 드럼으로", L"将此声部设为鼓组", L"اجعل هذا الجزء طبلاً", L"Сделать эту партию ударными", L"Diesen Part zu Drums machen", L"Tornar esta parte bateria", L"Dit deel drums maken", L"Zrob ta partie perkusja", L"Bu parti davul yap"));
			drum->AddCommand(IDM_MM_DRUM_AB10, LL14(L"A10 と B10 をドラムにする", L"Make A10 and B10 drums", L"A10 et B10 en batterie", L"A10 e B10 batteria", L"A10 y B10 en bateria", L"A10과 B10을 드럼으로", L"将 A10 和 B10 设为鼓组", L"اجعل A10 و B10 طبلاً", L"A10 и B10 — ударные", L"A10 und B10 zu Drums", L"A10 e B10 em bateria", L"A10 en B10 drums maken", L"A10 i B10 jako perkusja", L"A10 ve B10 davul yap"));
		}
	}
	CCustomPopupMenu* map = menu.AddSubMenu(
		(m_sourcePath[0] && SasamiPathIsMidi(m_sourcePath))
			? LL14(L"ささみ☆ﾐ 音源モード", L"Sasami MIDI map", L"Carte Sasami MIDI", L"Mappa Sasami MIDI", L"Mapa Sasami MIDI", L"사사미 MIDI 맵", L"ささみ☆ﾐ 音源模式", L"خريطة Sasami MIDI", L"Карта Sasami MIDI", L"Sasami-Klangkarte", L"Mapa Sasami MIDI", L"Sasami MIDI-kaart", L"Mapa Sasami MIDI", L"Sasami MIDI haritasi")
			: LL14(L"音色マップ", L"Tone map", L"Carte de timbres", L"Mappa timbri", L"Mapa de timbres", L"음색 맵", L"音色映射", L"خريطة الأصوات", L"Карта тембров", L"Klangkarte", L"Mapa de timbres", L"Klankkaart", L"Mapa barw", L"Timbir haritasi"),
		(m_sourcePath[0] && SasamiPathIsMidi(m_sourcePath))
			? LL14(L".mpy/.mpw2 の SMF 変換音源。55map / 88map / XG 等", L"Sound source for .mpy/.mpw2 SMF conversion (55map, 88map, XG…)", L"Source sonore pour conversion SMF .mpy/.mpw2", L"Sorgente per conversione SMF .mpy/.mpw2", L"Fuente sonora para conversion SMF .mpy/.mpw2", L".mpy/.mpw2 SMF 변환 음원", L".mpy/.mpw2 的 SMF 转换音源", L"مصدر صوت لتحويل SMF لـ .mpy/.mpw2", L"Источник звука для SMF из .mpy/.mpw2", L"Klangquelle fur .mpy/.mpw2-SMF", L"Fonte sonora para conversao SMF .mpy/.mpw2", L"Geluidbron voor .mpy/.mpw2 SMF", L"Zrodlo dzwieku konwersji SMF .mpy/.mpw2", L".mpy/.mpw2 SMF donusum ses kaynagi")
			: LL14(L"名前引きに使う音色マップ（自動／GS／XG／55／88／LA など）", L"Tone map for names (Auto / GS / XG / 55 / 88 / LA…)", L"Carte de timbres pour les noms (Auto / GS / XG / 55 / 88 / LA…)", L"Mappa timbri per i nomi (Auto / GS / XG / 55 / 88 / LA…)", L"Mapa de timbres para nombres (Auto / GS / XG / 55 / 88 / LA…)", L"이름에 쓸 음색 맵 (자동 / GS / XG / 55 / 88 / LA…)", L"用于查名的音色映射（自动／GS／XG／55／88／LA 等）", L"خريطة الأصوات للأسماء (Auto / GS / XG / 55 / 88 / LA…)", L"Карта тембров для имён (Auto / GS / XG / 55 / 88 / LA…)", L"Klangkarte fuer Namen (Auto / GS / XG / 55 / 88 / LA…)", L"Mapa de timbres para nomes (Auto / GS / XG / 55 / 88 / LA…)", L"Klankkaart voor namen (Auto / GS / XG / 55 / 88 / LA…)", L"Mapa barw do nazw (Auto / GS / XG / 55 / 88 / LA…)", L"Isimler icin timbir haritasi (Auto / GS / XG / 55 / 88 / LA…)"));
	if (map) {
		map->AddCheck(IDM_MM_MAP_AUTO, LL14(L"自動 (SysEx / 曲名)", L"Auto (SysEx / title)", L"Auto (SysEx / titre)", L"Auto (SysEx / titolo)", L"Auto (SysEx / titulo)", L"자동 (SysEx / 제목)", L"自动 (SysEx / 曲名)", L"تلقائي (SysEx / عنوان)", L"Авто (SysEx / название)", L"Auto (SysEx / Titel)", L"Auto (SysEx / titulo)", L"Auto (SysEx / titel)", L"Auto (SysEx / tytul)", L"Otomatik (SysEx / baslik)"), m_mapForce == 0);
		map->AddCheck(IDM_MM_MAP_GS, L"GS", m_mapForce == 1);
		map->AddCheck(IDM_MM_MAP_XG, L"XG", m_mapForce == 2);
		map->AddSeparator();
		map->AddCheck(IDM_MM_MAP_55, L"55map", m_mapForce == 3);
		map->AddCheck(IDM_MM_MAP_88, L"88map", m_mapForce == 4);
		map->AddCheck(IDM_MM_MAP_88P, L"88Promap", m_mapForce == 5);
		map->AddCheck(IDM_MM_MAP_8820, L"8820map", m_mapForce == 6);
		map->AddCheck(IDM_MM_MAP_GM, L"GMmap", m_mapForce == 7);
		map->AddCheck(IDM_MM_MAP_SD, L"SDmap", m_mapForce == 8);
		map->AddCheck(IDM_MM_MAP_LA, L"LAmap", m_mapForce == 9);
		CCustomPopupMenu* etc = map->AddSubMenu(
			LL14(L"ETC (EX)", L"ETC (EX)", L"ETC (EX)", L"ETC (EX)", L"ETC (EX)", L"ETC (EX)", L"ETC (EX)", L"ETC (EX)", L"ETC (EX)", L"ETC (EX)", L"ETC (EX)", L"ETC (EX)", L"ETC (EX)", L"ETC (EX)"),
			LL14(L"GS/XG 以外（SASAMI_EX.DAT）", L"Non-GS/XG (SASAMI_EX.DAT)", L"Hors GS/XG (SASAMI_EX.DAT)", L"Non GS/XG (SASAMI_EX.DAT)", L"Fuera de GS/XG (SASAMI_EX.DAT)", L"GS/XG 이외 (SASAMI_EX.DAT)", L"GS/XG 以外（SASAMI_EX.DAT）", L"غير GS/XG (SASAMI_EX.DAT)", L"Не GS/XG (SASAMI_EX.DAT)", L"Nicht GS/XG (SASAMI_EX.DAT)", L"Fora GS/XG (SASAMI_EX.DAT)", L"Geen GS/XG (SASAMI_EX.DAT)", L"Poza GS/XG (SASAMI_EX.DAT)", L"GS/XG disi (SASAMI_EX.DAT)"));
		if (etc) {
			etc->AddCheck(IDM_MM_MAP_GM2, L"GM2map", m_mapForce == 10);
			etc->AddCheck(IDM_MM_MAP_NS, L"NSmap", m_mapForce == 11);
			etc->AddCheck(IDM_MM_MAP_KW, L"KWmap", m_mapForce == 12);
			etc->AddCheck(IDM_MM_MAP_SG, L"SGmap", m_mapForce == 13);
			etc->AddCheck(IDM_MM_MAP_KR, L"KRmap", m_mapForce == 14);
			etc->AddCheck(IDM_MM_MAP_PA, L"PAmap", m_mapForce == 15);
			etc->AddCheck(IDM_MM_MAP_CS, L"CSmap", m_mapForce == 16);
			etc->AddCheck(IDM_MM_MAP_GEM, L"GEMmap", m_mapForce == 17);
			etc->AddCheck(IDM_MM_MAP_LK, L"LKmap", m_mapForce == 18);
			etc->AddCheck(IDM_MM_MAP_PV, L"PVmap", m_mapForce == 19);
		}
	}
	CCustomPopupMenu* openSub = menu.AddSubMenu(
		LL14(L"開く", L"Open", L"Ouvrir", L"Apri", L"Abrir", L"열기", L"打开", L"فتح", L"Открыть", L"Offnen", L"Abrir", L"Openen", L"Otworz", L"Ac"),
		LL14(L"イコライザ／ピアノロール／アナライザ／VSTホスト／操作ガイド", L"Equalizer / piano roll / analyzer / VST host / guide", L"Egaliseur / piano roll / analyseur / hote VST / guide", L"Equalizzatore / piano roll / analizzatore / host VST / guida", L"Ecualizador / piano roll / analizador / host VST / guia", L"이퀄라이저/피아노 롤/분석기/VST 호스트/가이드", L"均衡器/钢琴卷帘/分析器/VST主机/指南", L"المعادل / البيانو / المحلل / مضيف VST / الدليل", L"Эквалайзер / пианоролл / анализатор / хост VST / руководство", L"Equalizer / Piano-Roll / Analyzer / VST-Host / Anleitung", L"Equalizador / piano roll / analisador / host VST / guia", L"Equalizer / piano-roll / analyser / VST-host / gids", L"Equalizer / piano roll / analizator / host VST / przewodnik", L"Equalizer / piano roll / analizor / VST host / kilavuz"));
	if (openSub) {
		openSub->AddCommand(ID_MP_OPEN_EQ, LL14(L"イコライザを開く", L"Open equalizer", L"Ouvrir l'egaliseur", L"Apri equalizzatore", L"Abrir ecualizador", L"이퀄라이저 열기", L"打开均衡器", L"فتح المعادل", L"Открыть эквалайзер", L"Equalizer öffnen", L"Abrir equalizador", L"Equalizer openen", L"Otworz equalizer", L"Equalizeri ac"));
		openSub->AddCommand(ID_MP_OPEN_PIANOROLL, LL14(L"ピアノロールを開く", L"Open piano roll", L"Ouvrir le piano roll", L"Apri piano roll", L"Abrir piano roll", L"피아노 롤 열기", L"打开钢琴卷帘", L"فتح لفافة البيانو", L"Открыть пианоролл", L"Piano-Roll öffnen", L"Abrir piano roll", L"Piano-roll openen", L"Otworz piano roll", L"Piyano rolunu ac"));
		openSub->AddCommand(ID_MP_OPEN_ANALYZER, LL14(L"アナライザを開く", L"Open analyzer", L"Ouvrir l'analyseur", L"Apri analizzatore", L"Abrir analizador", L"분석기 열기", L"打开分析器", L"فتح المحلل", L"Открыть анализатор", L"Analyzer öffnen", L"Abrir analisador", L"Analyzer openen", L"Otworz analizator", L"Analizoru ac"));
		openSub->AddCommand(ID_MP_OPEN_VSTHOST, LL14(L"VSTホストを開く", L"Open VST host", L"Ouvrir l'hote VST", L"Apri host VST", L"Abrir host VST", L"VST 호스트 열기", L"打开 VST 主机", L"فتح مضيف VST", L"Открыть хост VST", L"VST-Host öffnen", L"Abrir host VST", L"VST-host openen", L"Otworz host VST", L"VST hostu ac"));
		openSub->AddCommand(ID_HELP_SHOWSHEET, LL14(L"操作ガイド", L"Operation guide", L"Guide d'utilisation", L"Guida operativa", L"Guía de operación", L"조작 가이드", L"操作指南", L"دليل التشغيل", L"Руководство", L"Bedienungsanleitung", L"Guia de operação", L"Handleiding", L"Przewodnik", L"İşlem kılavuzu"));
	}

	if (point.x == -1 && point.y == -1) {
		CRect rc; GetClientRect(&rc); ClientToScreen(&rc);
		point = CPoint(rc.left + 8, rc.top + 8);
	}
	const UINT cmd = menu.Track(point, this);
	if (cmd == IDM_MM_VIEW_2D) {
		m_viewMode = 0; savedata.midimonviewmode = 0; m_fullDraw = true; Invalidate(FALSE);
	} else if (cmd == IDM_MM_VIEW_3D) {
		m_viewMode = 1; savedata.midimonviewmode = 1;
		m_fullDraw = true;
		if (!(savedata.soft3dTourSeen & 16)) {
			savedata.soft3dTourSeen |= 16;
			m_soft3dTourUntil = GetTickCount() + 3000;
		}
		Invalidate(FALSE);
	} else if (cmd == IDM_MM_CAM_RESET) {
		savedata.midimon3dyaw = -220; savedata.midimon3dpitch = 260; savedata.midimon3dzoom = 100;
		SyncSoft3DFromSave(); Invalidate(FALSE);
	} else if (cmd == IDM_MM_FREEZE) {
		m_frozen = !m_frozen;
	} else if (cmd == IDM_MM_TOPMOST) {
		m_alwaysOnTop = !m_alwaysOnTop;
		savedata.midimontopmost = m_alwaysOnTop ? 1 : 0;
		SetWindowPos(m_alwaysOnTop ? &CWnd::wndTopMost : &CWnd::wndNoTopMost, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	} else if (cmd == IDM_MM_COPY) {
		wchar_t buf[4096];
		buf[0] = 0;
		int n = 0;
		n += _snwprintf_s(buf + n, _countof(buf) - n, _TRUNCATE, L"CH\tPC\tBNK\tName\tVol\tExp\tPan\tRev\tCrs\r\n");
		for (int i = 0; i < PART_MAX && n < 4000; ++i) {
			n += _snwprintf_s(buf + n, _countof(buf) - n, _TRUNCATE, L"%c%02d\t%03d\t%03d\t%s\t%d\t%d\t%d\t%d\t%d\r\n",
				(i < 16) ? L'A' : L'B', (i % 16) + 1, m_part[i].pc + 1, m_part[i].bankMsb, m_part[i].name,
				m_part[i].vol, m_part[i].exp, m_part[i].pan, m_part[i].rev, m_part[i].crs);
		}
		BuildInsLine(0, m_insLine[0], 220);
		BuildInsLine(1, m_insLine[1], 220);
		n += _snwprintf_s(buf + n, _countof(buf) - n, _TRUNCATE, L"%s\r\n%s\r\n", m_insLine[0], m_insLine[1]);
		if (InsFootCount() > 2) {
			BuildInsLine(2, m_insLine[2], 220);
			BuildInsLine(3, m_insLine[3], 220);
			n += _snwprintf_s(buf + n, _countof(buf) - n, _TRUNCATE, L"%s\r\n%s\r\n", m_insLine[2], m_insLine[3]);
		}
		if (OpenClipboard()) {
			EmptyClipboard();
			const size_t bytes = (wcslen(buf) + 1) * sizeof(wchar_t);
			HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
			if (h) {
				void* p = GlobalLock(h);
				if (p) { memcpy(p, buf, bytes); GlobalUnlock(h); SetClipboardData(CF_UNICODETEXT, h); }
				else GlobalFree(h);
			}
			CloseClipboard();
		}
	} else if (cmd == IDM_MM_RELOAD) {
		m_loadedPath[0] = 0;
		LoadCurrentMidi();
		MarkHostOccupiedParts();
		Invalidate(FALSE);
	} else if (cmd == IDM_MM_DRUM_PART || cmd == IDM_MM_DRUM_AB10) {
		int parts[2];
		int n = 0;
		if (cmd == IDM_MM_DRUM_AB10) {
			parts[n++] = 9;
			parts[n++] = 25;
		} else {
			int p = ctxPart;
			if (p < 0) p = 9;
			if (p >= PART_MAX) p = PART_MAX - 1;
			parts[n++] = p;
		}
		for (int i = 0; i < n; ++i) {
			const int part = parts[i];
			const int ch = part % 16;
			const int port = part / 16;
			BYTE bb = 0x10;
			if (ch == 9) bb = 0x10;
			else if (ch < 9) bb = (BYTE)(0x11 + ch);
			else bb = (BYTE)(0x10 + ch);
			BYTE d[11];
			d[0] = 0xf0; d[1] = 0x41; d[2] = 0x10; d[3] = 0x42; d[4] = 0x12;
			d[5] = (BYTE)(port ? 0x50 : 0x40);
			d[6] = bb; d[7] = 0x15; d[8] = 0x01; d[9] = 0; d[10] = 0xf7;
			unsigned sum = 0;
			for (int k = 5; k <= 8; ++k) sum += d[k];
			d[9] = (BYTE)((0x80 - (sum & 0x7f)) & 0x7f);
			ApplySysex(d, 11);
			m_part[part].isDrum = 1;
			if (m_sysMode == 2)
				m_part[part].xgPartMode = 1;
			RefreshPartName(m_part[part]);
			m_dirtyRows |= (1u << part);
			if (m_sysMode == 2) {
				InjectShort(part, 0xb0 | (0u << 8) | (127u << 16));
				InjectShort(part, 0xb0 | (32u << 8));
			} else {
				MmBindVstActiveSlot();
				if (MmVstHostOpen())
					VstLiveMidiSysex(port, d, 11);
				else if (mode == MODE_VST_MIDI)
					VstMidiInjectSysex(port, d, 11);
			}
		}
		m_fullDraw = true;
		Invalidate(FALSE);
	} else if (cmd == IDM_MM_MAP_AUTO || cmd == IDM_MM_MAP_GS || cmd == IDM_MM_MAP_XG
		|| cmd == IDM_MM_MAP_55 || cmd == IDM_MM_MAP_88 || cmd == IDM_MM_MAP_88P
		|| cmd == IDM_MM_MAP_8820 || cmd == IDM_MM_MAP_GM || cmd == IDM_MM_MAP_SD
		|| cmd == IDM_MM_MAP_LA || (cmd >= IDM_MM_MAP_GM2 && cmd <= IDM_MM_MAP_PV)) {
		int force = 0;
		if (cmd == IDM_MM_MAP_AUTO) force = 0;
		else if (cmd == IDM_MM_MAP_GS) force = 1;
		else if (cmd == IDM_MM_MAP_XG) force = 2;
		else if (cmd == IDM_MM_MAP_55) force = 3;
		else if (cmd == IDM_MM_MAP_88) force = 4;
		else if (cmd == IDM_MM_MAP_88P) force = 5;
		else if (cmd == IDM_MM_MAP_8820) force = 6;
		else if (cmd == IDM_MM_MAP_GM) force = 7;
		else if (cmd == IDM_MM_MAP_SD) force = 8;
		else if (cmd == IDM_MM_MAP_LA) force = 9;
		else force = 10 + (int)(cmd - IDM_MM_MAP_GM2);
		ApplyMapForce(force);
		const wchar_t* flagPath = m_sourcePath[0] ? m_sourcePath : m_loadedPath;
		if (flagPath[0]) {
#ifndef _UNICODE
			PlMidForceSet(CString(flagPath), force);
#else
			PlMidForceSet(flagPath, force);
#endif
			if (m_sourcePath[0] && SasamiPathIsMidi(m_sourcePath)) {
				SasamiInvalidateTempMidi(m_sourcePath);
				ReloadCurrentMidi();
			} else {
				PlMidNotifyMarkViews();
			}
		}
		Invalidate(FALSE);
	} else if (cmd == ID_MP_OPEN_EQ || cmd == ID_MP_OPEN_PIANOROLL || cmd == ID_MP_OPEN_ANALYZER) {
		extern CMediaPlayerDlg* mp;
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->PostMessage(WM_COMMAND, cmd);
		else if (og && ::IsWindow(og->GetSafeHwnd())) {
			if (cmd == ID_MP_OPEN_EQ)
				og->PostMessage(WM_COMMAND, MAKEWPARAM(IDC_BUTTON59, BN_CLICKED), 0);
			else if (cmd == ID_MP_OPEN_PIANOROLL)
				og->PostMessage(WM_OGG_TOGGLE_SUBUI, 1, 0);
			else
				og->PostMessage(WM_OGG_TOGGLE_SUBUI, 2, 0);
		}
	} else if (cmd == ID_MP_OPEN_VSTHOST) {
		extern CMediaPlayerDlg* mp;
		CWnd* parent = (mp && ::IsWindow(mp->GetSafeHwnd())) ? (CWnd*)mp : this;
		OpenVstHostModeless(parent);
	} else if (cmd == ID_HELP_SHOWSHEET) {
		ShowHelpSheet();
	}
}

void CMidiMonitorDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
	if (CCC_MainLockOverlayHitTest(m_hWnd, point)) {
		CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
		return;
	}
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	if (capH > 0 && point.y >= 0 && point.y < capH) {
		CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
		return;
	}
	if (IsView3D()) {
		m_rotDragging = true;
		m_rotDragOrigin = point;
		m_rotDragYaw0 = m_cam.yawDeg;
		m_rotDragPitch0 = m_cam.pitchDeg;
		SetCapture();
		return;
	}
	int part = -1;
	CRect cell;
	const int hit = HitMonitor(point, part, cell);
	if (hit == MM_HIT_APPVOL) {
		m_volDragging = true;
		CPoint f = point;
		f.y -= CCC_GetCustomCaptionHeight(m_hWnd);
		int pct = 0;
		if (m_volBarRc.Width() > 0)
			pct = (f.x - m_volBarRc.left) * 100 / m_volBarRc.Width();
		SetAppVolPercent(pct);
		SetCapture();
		InvalidateDirty();
		return;
	}
	if (hit == MM_HIT_VOL || hit == MM_HIT_PAN || hit == MM_HIT_EXP
		|| hit == MM_HIT_REV || hit == MM_HIT_CRS || hit == MM_HIT_VAR) {
		m_dragKind = hit;
		m_dragPart = part;
		SetCapture();
		ApplyDragValue(point);
		return;
	}
	if (hit == MM_HIT_KEYS && part >= 0) {
		CPoint f = point;
		f.y -= CCC_GetCustomCaptionHeight(m_hWnd);
		const int note = KeyAt(cell, f);
		if (note >= 0) {
			ReleasePlayNote();
			m_playPart = part;
			m_playNote = note;
			const int ch = part % 16;
			const DWORD msg = (DWORD)(0x90 | ch) | ((DWORD)note << 8) | (100u << 16);
			InjectShort(part, msg);
			InvalidateDirty();
			SetCapture();
		}
		return;
	}
	CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
}

void CMidiMonitorDlg::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_volDragging) {
		if (!(nFlags & MK_LBUTTON)) {
			m_volDragging = false;
			if (::GetCapture() == m_hWnd) ::ReleaseCapture();
			return;
		}
		CPoint f = point;
		f.y -= CCC_GetCustomCaptionHeight(m_hWnd);
		int pct = 0;
		if (m_volBarRc.Width() > 0)
			pct = (f.x - m_volBarRc.left) * 100 / m_volBarRc.Width();
		SetAppVolPercent(pct);
		InvalidateDirty();
		return;
	}
	if (m_dragKind == MM_HIT_VOL || m_dragKind == MM_HIT_PAN || m_dragKind == MM_HIT_EXP
		|| m_dragKind == MM_HIT_REV || m_dragKind == MM_HIT_CRS || m_dragKind == MM_HIT_VAR) {
		if (!(nFlags & MK_LBUTTON)) {
			m_dragKind = MM_HIT_NONE;
			m_dragPart = -1;
			if (::GetCapture() == m_hWnd) ::ReleaseCapture();
			return;
		}
		ApplyDragValue(point);
		return;
	}
	if (m_playNote >= 0) {
		if (!(nFlags & MK_LBUTTON)) {
			ReleasePlayNote();
			if (::GetCapture() == m_hWnd) ::ReleaseCapture();
			InvalidateDirty();
			return;
		}
		int part = -1;
		CRect cell;
		if (HitMonitor(point, part, cell) == MM_HIT_KEYS && part == m_playPart) {
			CPoint f = point;
			f.y -= CCC_GetCustomCaptionHeight(m_hWnd);
			const int note = KeyAt(cell, f);
			if (note >= 0 && note != m_playNote) {
				ReleasePlayNote();
				m_playPart = part;
				m_playNote = note;
				const int ch = part % 16;
				const DWORD msg = (DWORD)(0x90 | ch) | ((DWORD)note << 8) | (100u << 16);
				InjectShort(part, msg);
				InvalidateDirty();
			}
		}
		return;
	}
	if (!IsView3D()) {
		int part = -1;
		CRect cell;
		const int hit = HitMonitor(point, part, cell);
		if (hit == MM_HIT_APPVOL || hit == MM_HIT_PAN)
			::SetCursor(::LoadCursor(NULL, IDC_SIZEWE));
		else if (hit == MM_HIT_VOL || hit == MM_HIT_EXP || hit == MM_HIT_REV
			|| hit == MM_HIT_CRS || hit == MM_HIT_VAR)
			::SetCursor(::LoadCursor(NULL, IDC_SIZENS));
		else if (hit == MM_HIT_KEYS)
			::SetCursor(::LoadCursor(NULL, IDC_HAND));
	}
	if (m_rotDragging) {
		if (!(nFlags & MK_LBUTTON)) {
			m_rotDragging = false;
			if (::GetCapture() == m_hWnd) ::ReleaseCapture();
			PersistSoft3D();
			return;
		}
		GdiSoft3D::OrbitDrag(m_cam, m_rotDragYaw0, m_rotDragPitch0, m_rotDragOrigin, point);
		Invalidate(FALSE);
		return;
	}
	CCustomBlurDialogExBase::OnMouseMove(nFlags, point);
}

void CMidiMonitorDlg::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_volDragging) {
		m_volDragging = false;
		if (::GetCapture() == m_hWnd) ::ReleaseCapture();
		return;
	}
	if (m_dragKind != MM_HIT_NONE) {
		m_dragKind = MM_HIT_NONE;
		m_dragPart = -1;
		if (::GetCapture() == m_hWnd) ::ReleaseCapture();
		return;
	}
	if (m_playNote >= 0) {
		ReleasePlayNote();
		if (::GetCapture() == m_hWnd) ::ReleaseCapture();
		InvalidateDirty();
		return;
	}
	if (m_rotDragging) {
		m_rotDragging = false;
		if (::GetCapture() == m_hWnd) ::ReleaseCapture();
		PersistSoft3D();
		return;
	}
	CCustomBlurDialogExBase::OnLButtonUp(nFlags, point);
}

void CMidiMonitorDlg::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	if (IsView3D() || CCC_MainLockOverlayHitTest(m_hWnd, point)) {
		CCustomBlurDialogExBase::OnLButtonDblClk(nFlags, point);
		return;
	}
	int part = -1;
	CRect cell;
	const int hit = HitMonitor(point, part, cell);
	const int cc = MmCcForHit(hit);
	if (cc >= 0 && part >= 0) {
		const int v = MmDefaultForHit(hit);
		const int ch = part % 16;
		const DWORD msg = (DWORD)(0xb0 | ch) | ((DWORD)cc << 8) | ((DWORD)v << 16);
		InjectShort(part, msg);
		InvalidateDirty();
		return;
	}
	CCustomBlurDialogExBase::OnLButtonDblClk(nFlags, point);
}

BOOL CMidiMonitorDlg::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	UNREFERENCED_PARAMETER(nFlags);
	if (IsView3D()) {
		GdiSoft3D::WheelZoom(m_cam, zDelta);
		PersistSoft3D();
		Invalidate(FALSE);
		return TRUE;
	}
	CPoint client = pt;
	ScreenToClient(&client);
	int part = -1;
	CRect cell;
	const int hit = HitMonitor(client, part, cell);
	if (part >= 0 && (hit == MM_HIT_VOL || hit == MM_HIT_PAN || hit == MM_HIT_EXP
		|| hit == MM_HIT_REV || hit == MM_HIT_CRS || hit == MM_HIT_VAR)) {
		const int cc = MmCcForHit(hit);
		int* pv = nullptr;
		Part& p = m_part[part];
		if (hit == MM_HIT_VOL) pv = &p.vol;
		else if (hit == MM_HIT_PAN) pv = &p.pan;
		else if (hit == MM_HIT_EXP) pv = &p.exp;
		else if (hit == MM_HIT_REV) pv = &p.rev;
		else if (hit == MM_HIT_CRS) pv = &p.crs;
		else pv = &p.var;
		int v = *pv + ((zDelta > 0) ? 2 : -2);
		if (v < 0) v = 0;
		if (v > 127) v = 127;
		const int ch = part % 16;
		const DWORD msg = (DWORD)(0xb0 | ch) | ((DWORD)cc << 8) | ((DWORD)v << 16);
		InjectShort(part, msg);
		InvalidateDirty();
		return TRUE;
	}
	if (part >= 0 && hit == MM_HIT_PC) {
		int pc = m_part[part].pc + ((zDelta > 0) ? 1 : -1);
		if (pc < 0) pc = 0;
		if (pc > 127) pc = 127;
		const int ch = part % 16;
		const DWORD msg = (DWORD)(0xc0 | ch) | ((DWORD)pc << 8);
		InjectShort(part, msg);
		InvalidateDirty();
		return TRUE;
	}
	return CCustomBlurDialogExBase::OnMouseWheel(nFlags, zDelta, pt);
}

BOOL CMidiMonitorDlg::OnTtnNeedText(UINT id, NMHDR* pNMHDR, LRESULT* pResult)
{
	UNREFERENCED_PARAMETER(id);
	*pResult = 0;
	NMTTDISPINFO* pdi = (NMTTDISPINFO*)pNMHDR;
	pdi->lpszText = m_hoverTip;
	m_hoverTip[0] = 0;
	return FALSE;
}

BOOL CMidiMonitorDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	if (pMsg->message == WM_KEYDOWN && pMsg->wParam == '0' && IsView3D()) {
		savedata.midimon3dyaw = -220;
		savedata.midimon3dpitch = 260;
		savedata.midimon3dzoom = 100;
		SyncSoft3DFromSave();
		Invalidate(FALSE);
		return TRUE;
	}
	return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}
