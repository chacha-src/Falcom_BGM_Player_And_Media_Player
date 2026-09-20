#include "StdAfx.h"
#include "cemu_midi_live.h"
#include "cemu_types.h"
#include "cemu_modepref.h"
#include "cemu_mgr.h"
#include "cemu_zipfs.h"
#include "driver/cemu_driver.h"
#include "machine/cemu_hard.h"
#include "machine/cemu_hard_pcat.h"
#include "machine/cemu_hard_pc98.h"
#include "VstMidiEngine.h"
#include <string.h>
#include <stdlib.h>

enum {
	kLiveInjCap = 8192,
	/* PC98 の host-walk フォールバックは起動時に曲全体をキャプチャへ渡す。
	   最初の Pump が 512 フレーム窓を超えて全部 defer し、512 スロットだと
	   Note Off の大半が黙って落ちていた。 */
	kLiveHoldCap = 16384,
	kLiveRate = 44100,
	/* SMF div 480 @ tempo 500000us → 960 ticks/sec。
	   VstMidiEngine の ReadVar は MIDI 可変長 4 バイトまで (最大 0x0FFFFFFF)。
	   5 バイト delta だと LoadSmf が中断し length=2s パッドになる。 */
	kStubTicks = 0x0FFFFFFFu /* 約 3.23 日 @ 960 ticks/sec */
};

struct CEmuMidiLiveHold {
	DWORD msg;
	__int64 dueAbs; /* MIDI 時計の絶対サンプル (Pump ごとにリセットしない) */
};

struct CEmuMidiLive {
	CRITICAL_SECTION cs;
	int csReady;
	int active;
	CEmuZipFs* fs;
	CHard* hard;
	CDriver* drv;
	const CEmuGameEntry* ge;
	wchar_t midPath[MAX_PATH];
	int sampleRate;
	unsigned midiCursor;
	/* UART → ショートメッセージパーサ */
	uint8_t run;
	int need;
	int haveD0;
	uint8_t d0;
	uint32_t pendingTicks;
	uint32_t tickRem; /* ticks→samples の端数 */
	/* 時計は連続。Pump ごとに midiSample をリセットしない
	   (defer した NoteOff と新しい NoteOn の順が入れ替わり長さが崩れる)。 */
	__int64 midiSample;  /* UART delta タイムライン → ストリーム開始からのサンプル */
	__int64 audioSample; /* 既に Pump したサンプル (= Host64 チャンク基準) */
	int isMt32;
	int midiType; /* hoot midiout_type: 1/2 LA, 4/6 GS, 7 SC-88, 8 GM */
	int laBanksSent;
	int setupSent;
	int cc111StartSent;
	int sawNotes;
	int noteOns;
	uint8_t pc[16];
	uint8_t cc[16][128];
	uint8_t havePc[16];
	uint8_t haveCc[16][128];
	uint8_t pbL[16], pbM[16], havePb[16];
	int initPcBurst;
	int drumPcRetrig;
	__int64 holdNotesUntil;
	/* GM On / GS Reset / XG On のあと、プラグインが消化するまで後続を止める */
	__int64 resetHoldUntil;
	uint8_t sxHoldBuf[262144];
	int sxHoldOff[1024];
	int sxHoldLen[1024];
	int sxHoldN;
	int sxHoldUsed;
	wchar_t zipPath[CEMU_ZIP_PATH];
	/* 同一 zip SE overlay: 短い捕捉窓で NoteOn を印し、SE 終了時に
	   そのキーだけ Note Off (BGM を CC123 しない)。 */
	unsigned overlayCode;
	volatile long overlayPend;
	int ovlPhase; /* 0 待機, 1 捕捉, 2 SE 終了待ち */
	int ovlSeHeld;
	uint32_t seBits[16][4];
	__int64 ovlCapEnd;
	__int64 ovlMaxEnd;
	__int64 ovlHang;
	__int64 ovlLastSe;
	/* inject リング (このオーディオブロックで出す) */
	CEmuMidiLiveShort inj[kLiveInjCap];
	LONG injW;
	LONG injR;
	/* dueAbs がまだ audioSample+frames より先のイベント */
	CEmuMidiLiveHold hold[kLiveHoldCap];
	int holdN;
	int16_t* mixBuf;
	int mixCap;
	volatile LONG inPump;
	/* SysEx は Pump をまたいで F7 まで保持。途中で打ち切るとダンプが
	   running-status ノートになり、起動直後のゴミ音と鍵盤の往復になる。 */
	int inSysex;
	uint8_t sxBuf[4096];
	int sxN;
	int sxMt;
	int sxOverflow;
	/* リング圧。ここで黙って落とすと PC / Note Off が消える。 */
	unsigned injDropped;
	unsigned holdDropped;
	unsigned injPeak;
	unsigned holdPeak;
};

static CEmuMidiLive g_live;
static int g_liveBootAsSfx;

/* カタログ行が midiout 経路か */
static int LiveModeEntryIsMidi(const CEmuGameEntry* e)
{
	if (!e) return 0;
	for (int i = 0; i < e->optCount; i++) {
		if (_stricmp(e->opt[i].name, "midiout") == 0)
			return 1;
	}
	if (_stricmp(e->subtype, "midi") == 0) return 1;
	if (_stricmp(e->subtype, "midiout") == 0) return 1;
	if (_strnicmp(e->subtype, "midi", 4) == 0) return 1;
	return 0;
}

/* MIDI 可変長を track へ書く */
static void SmfPutVar(uint8_t* track, unsigned* tp, uint32_t v)
{
	uint8_t tmp[5];
	int n = 0;
	tmp[n++] = (uint8_t)(v & 0x7f);
	while (v >>= 7) {
		for (int i = n; i > 0; i--) tmp[i] = tmp[i - 1];
		tmp[0] = (uint8_t)(0x80 | (v & 0x7f));
		n++;
	}
	for (int i = 0; i < n; i++)
		track[(*tp)++] = tmp[i];
}

static void SmfPutSysex(uint8_t* track, unsigned* tp, const uint8_t* sx, int n)
{
	if (!track || !tp || !sx || n < 2) return;
	SmfPutVar(track, tp, 0);
	track[(*tp)++] = 0xf0;
	SmfPutVar(track, tp, (uint32_t)(n - 1));
	for (int i = 1; i < n; i++)
		track[(*tp)++] = sx[i];
}

/* Roland GS Reset / SC-88 System Mode / GM On。チェックサムは Roland DT1。 */
static const uint8_t kSxGsReset[] = {
	0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7
};
static const uint8_t kSxGsSysMode[] = {
	0xF0, 0x41, 0x10, 0x42, 0x12, 0x00, 0x00, 0x7F, 0x00, 0x01, 0xF7
};
static const uint8_t kSxGmOn[] = {
	0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7
};

/* 最小 Type-0 SMF: tempo, 名前, GS/SC-88/GM/LA 前設定, CC#111=0, 長い無音, EOT。
   VST はすぐ開き、リアルタイム音符は inject で来る。 */
static int WriteLiveStubSmf(const wchar_t* path, const char* seqName, int midiType)
{
	if (!path || !path[0]) return 0;
	uint8_t track[2048];
	unsigned tp = 0;
	const int laBanks = (midiType == 1 || midiType == 2) ? 1 : 0;

	SmfPutVar(track, &tp, 0);
	track[tp++] = 0xff; track[tp++] = 0x51; track[tp++] = 0x03;
	track[tp++] = 0x07; track[tp++] = 0xa1; track[tp++] = 0x20;

	{
		char name[40];
		name[0] = 0;
		if (seqName && seqName[0])
			_snprintf_s(name, _TRUNCATE, "%s", seqName);
		else
			_snprintf_s(name, _TRUNCATE, "cemu-live");
		const int n = (int)strlen(name);
		SmfPutVar(track, &tp, 0);
		track[tp++] = 0xff; track[tp++] = 0x03;
		track[tp++] = (uint8_t)((n > 32) ? 32 : n);
		for (int i = 0; i < n && i < 32; i++)
			track[tp++] = (uint8_t)name[i];
	}

	if (laBanks) {
		for (int ch = 0; ch < 16; ch++) {
			if (ch == 9) continue;
			SmfPutVar(track, &tp, 0);
			track[tp++] = (uint8_t)(0xb0 | ch); track[tp++] = 0; track[tp++] = 127;
			SmfPutVar(track, &tp, 0);
			track[tp++] = (uint8_t)(0xb0 | ch); track[tp++] = 32; track[tp++] = 0;
		}
	}
	/* GS Reset / CC#111 はスタブに書かない。DispatchDueEvents はライブ注入の
	   あと SMF を歩くので、同じブロックで FMP の POWER/PC を消す。 */

	/* 数日分の本体。Host64/local の lengthSamples がライブ inject 用に開いたまま。
	   ここで CC#111 終端を書かない — 空の 4 日 SMF を VST がループしてしまう。 */
	SmfPutVar(track, &tp, (uint32_t)kStubTicks);
	track[tp++] = 0xb0; track[tp++] = 7; track[tp++] = 100;

	SmfPutVar(track, &tp, 0);
	track[tp++] = 0xff; track[tp++] = 0x2f; track[tp++] = 0x00;

	HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_TEMPORARY, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	const uint8_t hdr[14] = {
		'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0x01,0xe0
	};
	DWORD wr = 0;
	WriteFile(h, hdr, 14, &wr, NULL);
	uint8_t th[8] = { 'M','T','r','k', 0,0,0,0 };
	th[4] = (uint8_t)((tp >> 24) & 0xff);
	th[5] = (uint8_t)((tp >> 16) & 0xff);
	th[6] = (uint8_t)((tp >> 8) & 0xff);
	th[7] = (uint8_t)(tp & 0xff);
	WriteFile(h, th, 8, &wr, NULL);
	WriteFile(h, track, tp, &wr, NULL);
	CloseHandle(h);
	return (wr == tp) ? 1 : 0;
}

/* ライブ CS を一度だけ初期化 */
static void LiveEnsureCs(void)
{
	if (!g_live.csReady) {
		InitializeCriticalSection(&g_live.cs);
		g_live.csReady = 1;
	}
}

/* inject リングへ。満杯ならノートオンだけ捨て、並びは崩さない。 */
static void LivePushShort(DWORD msg, int sampleOfs)
{
	const LONG w = g_live.injW;
	unsigned used = (unsigned)(w - g_live.injR);
	if (used > g_live.injPeak) g_live.injPeak = used;
	if (used >= (unsigned)(kLiveInjCap - 1)) {
		const int st = (int)(msg & 0xf0);
		const int keep = (st == 0xb0 || st == 0xc0 || st == 0xe0
			|| st == 0x80 || (st == 0x90 && ((msg >> 16) & 0x7f) == 0));
		if (!keep) {
			g_live.injDropped++;
			return;
		}
		const LONG r = g_live.injR;
		const int i = (int)(r & (kLiveInjCap - 1));
		const DWORD old = g_live.inj[i].msg;
		const int ost = (int)(old & 0xf0);
		if (ost == 0x90 && ((old >> 16) & 0x7f) > 0) {
			g_live.injR = r + 1;
			g_live.injDropped++;
		} else {
			g_live.injDropped++;
			return;
		}
	}
	{
		const LONG nw = g_live.injW;
		const int i = (int)(nw & (kLiveInjCap - 1));
		g_live.inj[i].msg = msg;
		g_live.inj[i].sampleOfs = sampleOfs;
		MemoryBarrier();
		g_live.injW = nw + 1;
	}
}

/* まだこの Pump 窓に入らないイベントを hold へ */
static void LiveHoldPushAbs(DWORD msg, __int64 dueAbs)
{
	if (g_live.holdN > (int)g_live.holdPeak) g_live.holdPeak = (unsigned)g_live.holdN;
	if (g_live.holdN >= kLiveHoldCap) {
		g_live.holdDropped++;
		return;
	}
	g_live.hold[g_live.holdN].msg = msg;
	g_live.hold[g_live.holdN].dueAbs = dueAbs;
	g_live.holdN++;
}

static int LiveSeBitTest(int ch, int key)
{
	if ((unsigned)ch > 15u || (unsigned)key > 127u) return 0;
	return (g_live.seBits[ch][key >> 5] >> (key & 31)) & 1u;
}

static void LiveSeBitOn(int ch, int key)
{
	if ((unsigned)ch > 15u || (unsigned)key > 127u) return;
	const uint32_t m = 1u << (key & 31);
	uint32_t* w = &g_live.seBits[ch][key >> 5];
	if (!(*w & m)) {
		*w |= m;
		g_live.ovlSeHeld++;
	}
}

static void LiveSeBitOff(int ch, int key)
{
	if ((unsigned)ch > 15u || (unsigned)key > 127u) return;
	const uint32_t m = 1u << (key & 31);
	uint32_t* w = &g_live.seBits[ch][key >> 5];
	if (*w & m) {
		*w &= ~m;
		if (g_live.ovlSeHeld > 0)
			g_live.ovlSeHeld--;
	}
}

static void LiveKillSeNotes(void)
{
	if (g_live.ovlSeHeld > 0) {
		for (int ch = 0; ch < 16; ch++) {
			for (int wi = 0; wi < 4; wi++) {
				uint32_t bits = g_live.seBits[ch][wi];
				if (!bits) continue;
				for (int b = 0; b < 32; b++) {
					if (!(bits & (1u << b))) continue;
					const int key = wi * 32 + b;
					if (key > 127) continue;
					LivePushShort((DWORD)(0x80 | ch) | ((DWORD)key << 8), 0);
					LivePushShort((DWORD)(0x90 | ch) | ((DWORD)key << 8), 0);
				}
			}
		}
	}
	memset(g_live.seBits, 0, sizeof(g_live.seBits));
	g_live.ovlSeHeld = 0;
	g_live.ovlPhase = 0;
}

static void LiveArmSfxCapture(void)
{
	LiveKillSeNotes();
	g_live.ovlPhase = 1;
	const int rate = g_live.sampleRate > 0 ? g_live.sampleRate : kLiveRate;
	g_live.ovlCapEnd = g_live.audioSample + ((__int64)rate * 400) / 1000;
	g_live.ovlMaxEnd = g_live.audioSample + ((__int64)rate * 8000) / 1000;
	g_live.ovlHang = ((__int64)rate * 2500) / 1000;
	g_live.ovlLastSe = g_live.audioSample;
}

static void LiveTrackMsg(DWORD msg)
{
	if (g_live.ovlPhase <= 0) return;
	const int st = (int)(msg & 0xf0);
	const int ch = (int)(msg & 0x0f);
	const int d0 = (int)((msg >> 8) & 0x7f);
	const int d1 = (int)((msg >> 16) & 0x7f);
	if (st == 0x90) {
		if (d1 > 0) {
			if (g_live.ovlPhase == 1)
				LiveSeBitOn(ch, d0);
			if (LiveSeBitTest(ch, d0))
				g_live.ovlLastSe = g_live.audioSample;
		} else if (LiveSeBitTest(ch, d0)) {
			LiveSeBitOff(ch, d0);
			g_live.ovlLastSe = g_live.audioSample;
		}
	} else if (st == 0x80) {
		if (LiveSeBitTest(ch, d0)) {
			LiveSeBitOff(ch, d0);
			g_live.ovlLastSe = g_live.audioSample;
		}
	} else if (st == 0xb0 && (d0 == 120 || d0 == 121 || d0 == 123)) {
		for (int key = 0; key < 128; key++)
			LiveSeBitOff(ch, key);
		g_live.ovlLastSe = g_live.audioSample;
	}
}

static void LiveOvlTick(void)
{
	if (g_live.ovlPhase <= 0) return;
	const __int64 now = g_live.audioSample;
	if (g_live.ovlPhase == 1 && now >= g_live.ovlCapEnd)
		g_live.ovlPhase = 2;
	if (g_live.ovlSeHeld <= 0 && g_live.ovlPhase >= 2) {
		g_live.ovlPhase = 0;
		return;
	}
	if (now >= g_live.ovlMaxEnd) {
		LiveKillSeNotes();
		return;
	}
	if (g_live.ovlPhase == 2 && g_live.ovlSeHeld > 0
		&& (now - g_live.ovlLastSe) >= g_live.ovlHang)
		LiveKillSeNotes();
}

static void LiveAdvanceMidiClock(void)
{
	const int rate = g_live.sampleRate > 0 ? g_live.sampleRate : kLiveRate;
	if (!g_live.pendingTicks) return;
	const unsigned __int64 num =
		(unsigned __int64)g_live.pendingTicks * (unsigned __int64)rate
		+ (unsigned __int64)g_live.tickRem;
	g_live.tickRem = (uint32_t)(num % 960ull);
	g_live.midiSample += (__int64)(num / 960ull);
	g_live.pendingTicks = 0;
}

/* msg を絶対 midiSample に置く。ofs はこの Pump の audioSample 相対。 */
static void LiveEmitTimed(DWORD msg, int frames)
{
	LiveAdvanceMidiClock();
	const __int64 ofs64 = g_live.midiSample - g_live.audioSample;
	if (frames <= 0) {
		LiveHoldPushAbs(msg, g_live.midiSample);
		return;
	}
	if (ofs64 < 0) {
		/* MIDI 時計がオーディオより遅れている (ギャップクランプ / 起動)。前へスナップ。 */
		g_live.midiSample = g_live.audioSample;
		LivePushShort(msg, 0);
		return;
	}
	if (ofs64 < (__int64)frames) {
		LivePushShort(msg, (int)ofs64);
	} else {
		LiveHoldPushAbs(msg, g_live.midiSample);
	}
}

static void LiveLatchShort(DWORD msg)
{
	const int ch = (int)(msg & 15);
	const int hi = (int)(msg & 0xf0);
	if (hi == 0xc0) {
		g_live.pc[ch] = (uint8_t)((msg >> 8) & 0x7f);
		g_live.havePc[ch] = 1;
	} else if (hi == 0xb0) {
		const int cc = (int)((msg >> 8) & 0x7f);
		const int vv = (int)((msg >> 16) & 0x7f);
		g_live.cc[ch][cc] = (uint8_t)vv;
		g_live.haveCc[ch][cc] = 1;
	} else if (hi == 0xe0) {
		g_live.pbL[ch] = (uint8_t)((msg >> 8) & 0x7f);
		g_live.pbM[ch] = (uint8_t)((msg >> 16) & 0x7f);
		g_live.havePb[ch] = 1;
	}
}

static int LiveHasProgramLatch(void)
{
	for (int i = 0; i < 16; i++) {
		if (g_live.havePc[i] || g_live.havePb[i])
			return 1;
		for (int c = 0; c < 128; c++) {
			if (g_live.haveCc[i][c])
				return 1;
		}
	}
	return 0;
}

static void LivePushProgramSnapshot(int ofs)
{
	for (int ch = 0; ch < 16; ch++) {
		int any = g_live.havePc[ch] || g_live.havePb[ch];
		for (int c = 0; !any && c < 128; c++)
			if (g_live.haveCc[ch][c]) any = 1;
		if (!any) continue;
		if (g_live.haveCc[ch][0])
			LivePushShort((DWORD)(0xb0 | ch) | (0u << 8)
				| ((DWORD)g_live.cc[ch][0] << 16), ofs);
		if (g_live.haveCc[ch][32])
			LivePushShort((DWORD)(0xb0 | ch) | (32u << 8)
				| ((DWORD)g_live.cc[ch][32] << 16), ofs);
		for (int c = 1; c < 128; c++) {
			if (c == 32 || c == 120 || c == 121 || c == 123)
				continue;
			if (!g_live.haveCc[ch][c]) continue;
			LivePushShort((DWORD)(0xb0 | ch) | ((DWORD)c << 8)
				| ((DWORD)g_live.cc[ch][c] << 16), ofs);
		}
		if (g_live.havePb[ch])
			LivePushShort((DWORD)(0xe0 | ch)
				| ((DWORD)g_live.pbL[ch] << 8)
				| ((DWORD)g_live.pbM[ch] << 16), ofs);
		if (g_live.havePc[ch])
			LivePushShort((DWORD)(0xc0 | ch) | ((DWORD)g_live.pc[ch] << 8), ofs);
	}
}

static int LiveResetHolding(void)
{
	return (g_live.resetHoldUntil
		&& g_live.audioSample < g_live.resetHoldUntil) ? 1 : 0;
}

static void LiveQueueSysex(const uint8_t* d, int n)
{
	if (!d || n < 2) return;
	if (g_live.sxHoldN >= 1024) return;
	if (g_live.sxHoldUsed + n > (int)sizeof(g_live.sxHoldBuf)) return;
	g_live.sxHoldOff[g_live.sxHoldN] = g_live.sxHoldUsed;
	g_live.sxHoldLen[g_live.sxHoldN] = n;
	memcpy(g_live.sxHoldBuf + g_live.sxHoldUsed, d, (size_t)n);
	g_live.sxHoldUsed += n;
	g_live.sxHoldN++;
}

static int LiveSysexIsHardReset(const uint8_t* d, int n)
{
	return VstMidiSysexIsGmOn(d, n)
		|| VstMidiSysexIsGsReset(d, n)
		|| VstMidiSysexIsXgOn(d, n);
}

static int LiveSysexIsModeChange(const uint8_t* d, int n)
{
	return LiveSysexIsHardReset(d, n)
		|| VstMidiSysexIsGsSysMode(d, n);
}

/* GS 40/50/60 1x 15 = USE FOR RHYTHM。SC-VA は MAP2 を書くと
   既存キット（A10 POWER など）を STANDARD に戻す。 */
static int LiveSysexIsRhythmUse(const uint8_t* d, int n)
{
	if (!d || n < 11) return 0;
	if (d[0] != 0xf0 || d[1] != 0x41 || d[3] != 0x42 || d[4] != 0x12)
		return 0;
	const uint8_t aa = d[5];
	if ((aa & 0xf0) != 0x40 && (aa & 0xf0) != 0x50 && (aa & 0xf0) != 0x60)
		return 0;
	if (d[6] < 0x10 || d[6] > 0x1f) return 0;
	return (d[7] == 0x15) ? 1 : 0;
}

static __int64 LiveNoteGate(void)
{
	__int64 gate = g_live.resetHoldUntil;
	if (g_live.holdNotesUntil > gate)
		gate = g_live.holdNotesUntil;
	return gate;
}

/* PC/GS ゲートより前のノートだけ後ろへずらす。間隔は潰さない。
   一点に揃えると TriggerPlay が先に進めたイントロが先頭 1–2s で走る。 */
static void LivePostponeHoldsToNotes(void)
{
	const __int64 gate = g_live.holdNotesUntil;
	if (gate <= 0 || g_live.holdN <= 0) return;
	__int64 minDue = g_live.hold[0].dueAbs;
	for (int i = 1; i < g_live.holdN; i++) {
		if (g_live.hold[i].dueAbs < minDue)
			minDue = g_live.hold[i].dueAbs;
	}
	if (minDue >= gate) return;
	const __int64 shift = gate - minDue;
	for (int i = 0; i < g_live.holdN; i++)
		g_live.hold[i].dueAbs += shift;
}

static void LiveHoldNoteTimed(DWORD msg, int isOn)
{
	LiveAdvanceMidiClock();
	/* ゲートへ一点クランプすると TriggerPlay が先に進めたイントロが
	   先頭 1–2s に畳まれる。間隔は Postpone でまとめて後ろへずらす。 */
	LiveHoldPushAbs(msg, g_live.midiSample);
	LivePostponeHoldsToNotes();
	LiveTrackMsg(msg);
	if (isOn) {
		g_live.noteOns++;
		g_live.sawNotes = 1;
	}
}

/* 1x 15 の次の process() まで PC を出さない（同一バッファだとキットが消える）。 */
static void LiveArmRhythmPcWait(int frames)
{
	const int cur = frames > 0 ? frames : 512;
	g_live.resetHoldUntil = g_live.audioSample + (__int64)cur;
	const __int64 notes = g_live.resetHoldUntil + 512;
	if (g_live.holdNotesUntil < notes)
		g_live.holdNotesUntil = notes;
	LivePostponeHoldsToNotes();
}

static void LiveCompactSysexHold(int drop)
{
	if (drop <= 0) return;
	if (drop >= g_live.sxHoldN) {
		g_live.sxHoldN = 0;
		g_live.sxHoldUsed = 0;
		return;
	}
	int used = 0;
	int n = 0;
	for (int i = drop; i < g_live.sxHoldN; i++) {
		const int ln = g_live.sxHoldLen[i];
		if (g_live.sxHoldOff[i] != used)
			memmove(g_live.sxHoldBuf + used,
				g_live.sxHoldBuf + g_live.sxHoldOff[i], (size_t)ln);
		g_live.sxHoldOff[n] = used;
		g_live.sxHoldLen[n] = ln;
		used += ln;
		n++;
	}
	g_live.sxHoldN = n;
	g_live.sxHoldUsed = used;
}

/* VST BLOCK_FRAMES=512。Open 時の Reset 消化と同じ 12 ブロック無音。 */
static void LiveArmResetHold(int frames)
{
	const int silent = 12 * 512;
	const int cur = frames > 0 ? frames : 512;
	g_live.resetHoldUntil = g_live.audioSample
		+ (__int64)cur + (__int64)silent;
	g_live.holdNotesUntil = g_live.resetHoldUntil + 512;
	g_live.initPcBurst = 0;
	g_live.drumPcRetrig = 0;
	LivePostponeHoldsToNotes();
}

static void LiveFlushResetGate(int frames)
{
	if (!g_live.resetHoldUntil) return;
	if (g_live.audioSample < g_live.resetHoldUntil) return;
	g_live.resetHoldUntil = 0;
	/* キュー先頭のモード変更（GS Reset のあとの System Mode など）は
	   PC と同じブロックに載せない。88map 切替が PC を Piano に戻す。 */
	int drop = 0;
	int rearm = 0;
	int sawRhythm = 0;
	for (int i = 0; i < g_live.sxHoldN; i++) {
		const uint8_t* d = g_live.sxHoldBuf + g_live.sxHoldOff[i];
		const int n = g_live.sxHoldLen[i];
		const int mode = LiveSysexIsModeChange(d, n);
		if (LiveSysexIsRhythmUse(d, n))
			sawRhythm = 1;
		VstMidiInjectSysex(0, d, n);
		VstLiveTapPushSysexAt(0, d, n, g_live.midiSample);
		drop = i + 1;
		if (mode) {
			rearm = 1;
			break;
		}
	}
	LiveCompactSysexHold(drop);
	if (rearm) {
		LiveArmResetHold(frames);
		LivePostponeHoldsToNotes();
		return;
	}
	/* A11 MAP2 など 1x 15 の直後に POWER PC を載せない。 */
	if (sawRhythm) {
		if (g_live.initPcBurst)
			g_live.drumPcRetrig = 1;
		LiveArmRhythmPcWait(frames);
	}
}

static void LiveFlushProgramGate(int frames)
{
	if (LiveResetHolding()) return;
	if (!LiveHasProgramLatch()) return;
	if (g_live.initPcBurst && !g_live.drumPcRetrig) return;
	LivePushProgramSnapshot(0);
	g_live.initPcBurst = 1;
	g_live.drumPcRetrig = 0;
	const __int64 next = g_live.audioSample
		+ (frames > 0 ? frames : 512);
	if (g_live.holdNotesUntil < next)
		g_live.holdNotesUntil = next;
	LivePostponeHoldsToNotes();
}

static void LiveFinishShort(DWORD msg, int frames)
{
	LiveLatchShort(msg);
	const uint8_t hi = (uint8_t)(msg & 0xf0);
	const int isNote = (hi == 0x80 || hi == 0x90) ? 1 : 0;
	const int isOn = (hi == 0x90 && ((msg >> 16) & 0x7f) > 0) ? 1 : 0;
	/* GM/GS/XG の消化中は後続ショートを出さない。実機も Reset 完了まで無視する。
	   due は MIDI 時計を保つ。ゲート一点へ揃えるとイントロが走る。 */
	if (LiveResetHolding()) {
		if (isNote)
			LiveHoldNoteTimed(msg, isOn);
		else {
			LiveAdvanceMidiClock();
			LiveTrackMsg(msg);
		}
		return;
	}
	/* Reset が無い曲用。PC スナップショットは Consume 末尾。ノートはゲート後。 */
	if (isNote && LiveHasProgramLatch() && !g_live.initPcBurst) {
		LiveHoldNoteTimed(msg, isOn);
		return;
	}
	if (isNote && g_live.holdNotesUntil
		&& g_live.audioSample < g_live.holdNotesUntil) {
		LiveHoldNoteTimed(msg, isOn);
		return;
	}
	if (!g_live.sawNotes && (hi == 0xc0 || hi == 0xb0 || hi == 0xe0)) {
		LiveAdvanceMidiClock();
		/* 初回スナップショット前の PC は UART 途中で出さない。
		   後続の 1x 15 と同じ Pump に載ると POWER が消える。 */
		if (g_live.initPcBurst)
			LivePushShort(msg, 0);
		LiveTrackMsg(msg);
		return;
	}
	LiveEmitTimed(msg, frames);
	LiveTrackMsg(msg);
	if (isOn) {
		g_live.noteOns++;
		g_live.sawNotes = 1;
	}
}

static void LiveFlushHolds(int frames)
{
	if (frames <= 0 || g_live.holdN <= 0) return;
	const __int64 audioEnd = g_live.audioSample + (__int64)frames;
	int w = 0;
	for (int i = 0; i < g_live.holdN; i++) {
		const __int64 due = g_live.hold[i].dueAbs;
		if (due < audioEnd) {
			__int64 ofs64 = due - g_live.audioSample;
			if (ofs64 < 0) ofs64 = 0;
			if (ofs64 >= (__int64)frames) ofs64 = (__int64)frames - 1;
			LivePushShort(g_live.hold[i].msg, (int)ofs64);
		} else {
			g_live.hold[w++] = g_live.hold[i];
		}
	}
	g_live.holdN = w;
}

static void LiveEmitSysex(const uint8_t* d, int n, int frames)
{
	if (!d || n < 2) return;
	if (LiveSysexIsModeChange(d, n)) {
		if (LiveResetHolding()) {
			/* GS Reset は先に出した DT1 を無効化する。System Mode は残す。 */
			if (LiveSysexIsHardReset(d, n)) {
				g_live.sxHoldN = 0;
				g_live.sxHoldUsed = 0;
			}
			LiveQueueSysex(d, n);
			return;
		}
		VstMidiInjectSysex(0, d, n);
		VstLiveTapPushSysexAt(0, d, n, g_live.midiSample);
		LiveArmResetHold(frames);
		return;
	}
	if (LiveResetHolding()) {
		LiveQueueSysex(d, n);
		return;
	}
	if (LiveSysexIsRhythmUse(d, n)) {
		/* A11 MAP2 が POWER PC と同じ process() に入るとキットが STANDARD に戻る。 */
		VstMidiInjectSysex(0, d, n);
		VstLiveTapPushSysexAt(0, d, n, g_live.midiSample);
		if (g_live.initPcBurst)
			g_live.drumPcRetrig = 1;
		LiveArmRhythmPcWait(frames);
		return;
	}
	VstMidiInjectSysex(0, d, n);
	VstLiveTapPushSysexAt(0, d, n, g_live.midiSample);
}

static void LiveEmitLaBanks(int frames)
{
	if (g_live.laBanksSent) return;
	for (int ch = 0; ch < 16; ch++) {
		if (ch == 9) continue;
		LiveEmitTimed((DWORD)(0xb0 | ch) | (0u << 8) | (127u << 16), frames);
		LiveEmitTimed((DWORD)(0xb0 | ch) | (32u << 8) | (0u << 16), frames);
	}
	g_live.laBanksSent = 1;
}

/* LA だけライブ注入する。GS/SC-88/GM の Reset はスタブ SMF 側。
   ここで二度目の GS Reset / 全ch CC32 を出すと、同じブロックの FMP PC が
   無効化され、POWER キット等が未設定のままになる。 */
static void LiveEmitMapSetup(int frames)
{
	if (g_live.setupSent) return;
	g_live.setupSent = 1;
	const int t = g_live.midiType;
	if (t == 1 || t == 2)
		LiveEmitLaBanks(frames);
}

static void LiveSysexAbort(void)
{
	g_live.inSysex = 0;
	g_live.sxN = 0;
	g_live.sxMt = 0;
	g_live.sxOverflow = 0;
}

/* 1=このバイトは SysEx。0=未完ダンプを捨てたので status として再処理。 */
static int LiveSysexFeed(uint8_t b, int frames)
{
	enum { kCap = (int)sizeof(g_live.sxBuf) };
	if (!g_live.inSysex) {
		if (b != 0xf0) return 0;
		g_live.inSysex = 1;
		g_live.sxN = 1;
		g_live.sxBuf[0] = 0xf0;
		g_live.sxMt = 0;
		g_live.sxOverflow = 0;
		return 1;
	}
	if (b >= 0xf8 && b != 0xf7)
		return 1;
	if (b == 0xf7) {
		if (!g_live.sxOverflow && g_live.sxN < kCap)
			g_live.sxBuf[g_live.sxN++] = 0xf7;
		if (!g_live.sxOverflow && g_live.sxN >= 2
			&& g_live.sxBuf[g_live.sxN - 1] == 0xf7) {
			LiveAdvanceMidiClock();
			LiveEmitSysex(g_live.sxBuf, g_live.sxN, frames);
		}
		if (g_live.sxMt) {
			g_live.isMt32 = 1;
			LiveEmitLaBanks(frames);
		}
		LiveSysexAbort();
		return 1;
	}
	if (b & 0x80) {
		LiveSysexAbort();
		return 0;
	}
	if (g_live.sxN < kCap) {
		g_live.sxBuf[g_live.sxN++] = b;
		if (g_live.sxN == 4 && g_live.sxBuf[1] == 0x41 && g_live.sxBuf[3] == 0x16)
			g_live.sxMt = 1;
	} else {
		g_live.sxOverflow = 1;
	}
	return 1;
}

static void LiveConsumeUart(CHardPcat* hw, int frames)
{
	if (!hw) return;
	/* Reset 消化後の DT1 をノートより先に出す。PC は UART の 1x 15 を見てから。 */
	LiveFlushResetGate(frames);
	LiveEmitMapSetup(frames);

	const unsigned n = hw->MidiByteCount();
	while (g_live.midiCursor < n) {
		const unsigned i = g_live.midiCursor++;
		g_live.pendingTicks += hw->MidiDeltaAt(i);
		const uint8_t v = hw->MidiByteAt(i);
		if (v >= 0xf8) continue;

		if (g_live.inSysex || v == 0xf0) {
			if (LiveSysexFeed(v, frames)) {
				g_live.run = 0;
				g_live.need = 0;
				g_live.haveD0 = 0;
				continue;
			}
		}

		if (v & 0x80) {
			g_live.haveD0 = 0;
			if ((v & 0xf0) == 0xf0) {
				g_live.run = 0;
				g_live.need = 0;
				continue;
			}
			g_live.run = v;
			g_live.need = ((v & 0xf0) == 0xc0 || (v & 0xf0) == 0xd0) ? 1 : 2;
			continue;
		}

		if (!g_live.run || g_live.need <= 0) continue;

		uint8_t data[2];
		int nData = 0;
		if (g_live.need == 1) {
			data[0] = (uint8_t)(v & 0x7f);
			nData = 1;
		} else if (!g_live.haveD0) {
			g_live.d0 = (uint8_t)(v & 0x7f);
			g_live.haveD0 = 1;
			continue;
		} else {
			data[0] = g_live.d0;
			data[1] = (uint8_t)(v & 0x7f);
			nData = 2;
			g_live.haveD0 = 0;
		}

		const uint8_t hi = (uint8_t)(g_live.run & 0xf0);
		const int ch = (int)(g_live.run & 0x0f);

		if (g_live.isMt32 && !g_live.laBanksSent)
			LiveEmitLaBanks(frames);

		if (!g_live.cc111StartSent && (hi == 0x90 || hi == 0x80)) {
			LiveEmitTimed((DWORD)0xb0 | (111u << 8) | (0u << 16), frames);
			g_live.cc111StartSent = 1;
		}

		if (g_live.isMt32 && hi == 0xc0 && ch != 9) {
			LiveEmitTimed((DWORD)(0xb0 | ch) | (0u << 8) | (127u << 16), frames);
			LiveEmitTimed((DWORD)(0xb0 | ch) | (32u << 8) | (0u << 16), frames);
		}

		DWORD msg = (DWORD)g_live.run | ((DWORD)data[0] << 8);
		if (nData > 1)
			msg |= ((DWORD)data[1] << 16);
		LiveFinishShort(msg, frames);
	}

	/* ここで pendingTicks を進めない — 未完メッセージは完了までギャップを保持
	   (進めないと音符長が二重計上になる)。 */

	if (n >= (unsigned)CEMU_PCAT_MIDI_CAP - 64) {
		hw->MidiCaptureReset();
		g_live.midiCursor = 0;
		g_live.pendingTicks = 0;
		LiveSysexAbort();
	}
	LiveFlushProgramGate(frames);
	LiveFlushHolds(frames);
}

static void LiveConsumeUartPc98(CHardPc98* hw, int frames)
{
	if (!hw) return;
	LiveFlushResetGate(frames);
	LiveEmitMapSetup(frames);

	const unsigned n = hw->MidiByteCount();
	while (g_live.midiCursor < n) {
		const unsigned i = g_live.midiCursor++;
		g_live.pendingTicks += hw->MidiDeltaAt(i);
		const uint8_t v = hw->MidiByteAt(i);
		if (v >= 0xf8) continue;

		if (g_live.inSysex || v == 0xf0) {
			if (LiveSysexFeed(v, frames)) {
				g_live.run = 0;
				g_live.need = 0;
				g_live.haveD0 = 0;
				continue;
			}
		}

		if (v & 0x80) {
			g_live.haveD0 = 0;
			if ((v & 0xf0) == 0xf0) {
				g_live.run = 0;
				g_live.need = 0;
				continue;
			}
			g_live.run = v;
			g_live.need = ((v & 0xf0) == 0xc0 || (v & 0xf0) == 0xd0) ? 1 : 2;
			continue;
		}

		if (!g_live.run || g_live.need <= 0) continue;

		uint8_t data[2];
		int nData = 0;
		if (g_live.need == 1) {
			data[0] = (uint8_t)(v & 0x7f);
			nData = 1;
		} else if (!g_live.haveD0) {
			g_live.d0 = (uint8_t)(v & 0x7f);
			g_live.haveD0 = 1;
			continue;
		} else {
			data[0] = g_live.d0;
			data[1] = (uint8_t)(v & 0x7f);
			nData = 2;
			g_live.haveD0 = 0;
		}

		const uint8_t hi = (uint8_t)(g_live.run & 0xf0);
		if (!g_live.cc111StartSent && (hi == 0x90 || hi == 0x80)) {
			LiveEmitTimed((DWORD)0xb0 | (111u << 8) | (0u << 16), frames);
			g_live.cc111StartSent = 1;
		}

		DWORD msg = (DWORD)g_live.run | ((DWORD)data[0] << 8);
		if (nData > 1)
			msg |= ((DWORD)data[1] << 16);
		LiveFinishShort(msg, frames);
	}

	if (n >= (unsigned)CEMU_PC98_MIDI_CAP - 64) {
		hw->MidiCaptureReset();
		g_live.midiCursor = 0;
		g_live.pendingTicks = 0;
		LiveSysexAbort();
	}
	LiveFlushProgramGate(frames);
	LiveFlushHolds(frames);
}

/* ライブ UART セッションが走っているか */
int CEmuMidiLiveActive(void)
{
	return g_live.active ? 1 : 0;
}

/* 最初の NoteOn を見たか (プレイリスト time=-1 / ループヒント) */
int CEmuMidiLiveHasNotes(void)
{
	return g_live.sawNotes ? 1 : 0;
}

/* キャプチャバッファを UART ストリームとして辿る (ランニングステータス、SysEx は飛ばす)。 */
template <class T>
static void LiveScanCapture(const T* hw, CEmuMidiLiveDiag* d)
{
	const unsigned n = hw->MidiByteCount();
	d->hwBytes = n;
	uint8_t run = 0;
	int need = 0, haveD0 = 0;
	uint8_t d0 = 0;
	int inSysex = 0;
	for (unsigned i = 0; i < n; i++) {
		const uint8_t v = hw->MidiByteAt(i);
		if (v >= 0xf8) continue;
		if (v == 0xf0) { inSysex = 1; run = 0; need = 0; haveD0 = 0; continue; }
		if (v == 0xf7) { inSysex = 0; continue; }
		if (v & 0x80) {
			inSysex = 0;
			haveD0 = 0;
			if ((v & 0xf0) == 0xf0) { run = 0; need = 0; continue; }
			run = v;
			need = ((v & 0xf0) == 0xc0 || (v & 0xf0) == 0xd0) ? 1 : 2;
			continue;
		}
		if (inSysex || !run || need <= 0) continue;
		if (need == 2 && !haveD0) { d0 = v; haveD0 = 1; continue; }
		haveD0 = 0;
		const uint8_t hi = (uint8_t)(run & 0xf0);
		if (hi == 0x90)
			(v > 0) ? d->hwNoteOn++ : d->hwNoteOff++;
		else if (hi == 0x80)
			d->hwNoteOff++;
		else if (hi == 0xc0)
			d->hwProgram++;
		else if (hi == 0xb0)
			d->hwControl++;
		(void)d0;
	}
}

/* UART 捕捉 vs inject/hold リングの差。ドライバが出さなかったのかチェーンが落としたのか */
int CEmuMidiLiveGetDiag(CEmuMidiLiveDiag* out)
{
	if (!out) return 0;
	memset(out, 0, sizeof(*out));
	LiveEnsureCs();
	EnterCriticalSection(&g_live.cs);
	const int ok = (g_live.active && g_live.hard) ? 1 : 0;
	if (ok) {
		out->injDropped = g_live.injDropped;
		out->holdDropped = g_live.holdDropped;
		out->injPeak = g_live.injPeak;
		out->holdPeak = g_live.holdPeak;
		out->midiSample = g_live.midiSample;
		out->audioSample = g_live.audioSample;
		if (g_live.hard->hardKind == CHard::KIND_PC98)
			LiveScanCapture((const CHardPc98*)g_live.hard, out);
		else if (g_live.hard->hardKind == CHard::KIND_PCAT)
			LiveScanCapture((const CHardPcat*)g_live.hard, out);
	}
	LeaveCriticalSection(&g_live.cs);
	return ok;
}

/* 走っているライブがこの zip か */
int CEmuMidiLiveSameZip(const wchar_t* zipPath)
{
	if (!zipPath || !zipPath[0]) return 0;
	LiveEnsureCs();
	EnterCriticalSection(&g_live.cs);
	const int ok = (g_live.active && g_live.zipPath[0]
		&& _wcsicmp(g_live.zipPath, zipPath) == 0) ? 1 : 0;
	LeaveCriticalSection(&g_live.cs);
	return ok;
}

/* 同一 zip SE: 曲を差し替えず title を注入 */
int CEmuMidiLiveOverlayTitle(unsigned titleCode)
{
	LiveEnsureCs();
	EnterCriticalSection(&g_live.cs);
	if (!g_live.active || !g_live.drv) {
		LeaveCriticalSection(&g_live.cs);
		return 0;
	}
	g_live.overlayCode = titleCode;
	LeaveCriticalSection(&g_live.cs);
	InterlockedExchange((LONG*)&g_live.overlayPend, 1);
	return 1;
}

/* ライブセッションを止め、hard/drv を CS 外で破棄 */
void CEmuMidiLiveStop(void)
{
	LiveEnsureCs();
	CDriver* drv = NULL;
	CHard* hard = NULL;
	CEmuZipFs* fs = NULL;
	int16_t* mixBuf = NULL;
	EnterCriticalSection(&g_live.cs);
	/* CS 内で切り離し、外で破棄 — HardDestroy/Render が Pump 待ちの CS と
	   入れ子にならないようにする。Render 中は inPump を待ってから mixBuf
	   と drv を捨てる（SC-55→GS の再起動が UI×prefetch で固まらない）。 */
	drv = g_live.drv; g_live.drv = NULL;
	hard = g_live.hard; g_live.hard = NULL;
	fs = g_live.fs; g_live.fs = NULL;
	if (g_live.midPath[0])
		g_live.midPath[0] = 0;
	g_live.ge = NULL;
	g_live.active = 0;
	g_live.midiCursor = 0;
	g_live.run = 0;
	g_live.need = 0;
	g_live.haveD0 = 0;
	g_live.pendingTicks = 0;
	g_live.tickRem = 0;
	g_live.midiSample = 0;
	g_live.audioSample = 0;
	g_live.holdN = 0;
	g_live.isMt32 = 0;
	g_live.midiType = 0;
	g_live.laBanksSent = 0;
	g_live.setupSent = 0;
	g_live.cc111StartSent = 0;
	g_live.sawNotes = 0;
	g_live.noteOns = 0;
	g_live.initPcBurst = 0;
	g_live.drumPcRetrig = 0;
	g_live.holdNotesUntil = 0;
	g_live.resetHoldUntil = 0;
	g_live.sxHoldN = 0;
	g_live.sxHoldUsed = 0;
	memset(g_live.pc, 0, sizeof(g_live.pc));
	memset(g_live.cc, 0, sizeof(g_live.cc));
	memset(g_live.havePc, 0, sizeof(g_live.havePc));
	memset(g_live.haveCc, 0, sizeof(g_live.haveCc));
	memset(g_live.pbL, 0, sizeof(g_live.pbL));
	memset(g_live.pbM, 0, sizeof(g_live.pbM));
	memset(g_live.havePb, 0, sizeof(g_live.havePb));
	g_live.zipPath[0] = 0;
	g_live.overlayCode = 0;
	g_live.ovlPhase = 0;
	g_live.ovlSeHeld = 0;
	g_live.injDropped = 0;
	g_live.holdDropped = 0;
	g_live.injPeak = 0;
	g_live.holdPeak = 0;
	LiveSysexAbort();
	memset(g_live.seBits, 0, sizeof(g_live.seBits));
	InterlockedExchange((LONG*)&g_live.overlayPend, 0);
	g_live.injR = g_live.injW;
	LeaveCriticalSection(&g_live.cs);
	while (InterlockedCompareExchange(&g_live.inPump, 0, 0) != 0)
		Sleep(1);
	EnterCriticalSection(&g_live.cs);
	mixBuf = g_live.mixBuf;
	g_live.mixBuf = NULL;
	g_live.mixCap = 0;
	LeaveCriticalSection(&g_live.cs);
	if (drv) {
		drv->Close();
		CEmuDriverDestroy(drv);
	}
	if (hard)
		CEmuHardDestroy(hard);
	if (fs) {
		CEmuZipFsClose(fs);
		free(fs);
	}
	if (mixBuf)
		free(mixBuf);
}

static int MidiOutTypeFromGe(const CEmuGameEntry* e)
{
	if (!e) return 0;
	for (int i = 0; i < e->optCount; i++) {
		if (_stricmp(e->opt[i].name, "midiout_type") == 0)
			return (int)strtoul(e->opt[i].value, NULL, 0);
	}
	return 0;
}

/* hoot midiout_type: 1/2 = MT-32 (LA), 4/6 = GS, 8 = GM。LA バンク (CC0=127)
   で MIDI モニタが LAmap を出す。GM タイトルには付けない。 */
static int MidiOutTypeIsLa(int t)
{
	/* 1/2 MT-32(LA), 3 CM-64（LA + PCM）。GS/SC-88/GM には LA バンクを付けない。 */
	return (t == 1 || t == 2 || t == 3) ? 1 : 0;
}

/* 既存 SMF BGM を置き換えずライブ MPU で SE を注入 */
int CEmuMidiLiveStartOverlayPcat(const wchar_t* zipPath, unsigned titleCode)
{
	g_liveBootAsSfx = 1;
	wchar_t dummy[MAX_PATH] = {};
	const int ok = CEmuMidiLiveStartPcat(zipPath, titleCode, dummy, MAX_PATH);
	if (!ok)
		g_liveBootAsSfx = 0;
	return (ok && dummy[0]) ? 1 : 0;
}

/* PCAT/PC98 midiout を起動しスタブ SMF を書く。リアルタイム音符は inject */
int CEmuMidiLiveStartPcat(const wchar_t* zipPath, unsigned titleCode,
	wchar_t* outMidPath, int outCap)
{
	if (!zipPath || !outMidPath || outCap <= 0) return 0;
	outMidPath[0] = 0;
	LiveEnsureCs();
	CEmuMidiLiveStop();

	/* 既に GS/LA/SC-88 が選ばれていればそれを残す。汎用 "MIDI" に潰すと
	   vg2_98 の SC-88 行が SC-55 に戻る。未選択なら XML の midiout_type へ。 */
	wchar_t zipOut[CEMU_ZIP_PATH];
	char dataDir[CEMU_DATA_DIR];
	CEmuMgr* mgr = CEmuMgrGet();
	const CEmuGameEntry* ge = CEmuMgrResolveZip(mgr, zipPath, zipOut,
		(int)_countof(zipOut), dataDir, (int)sizeof(dataDir));
	if (!ge || !LiveModeEntryIsMidi(ge)) {
		char stem[CEMU_ARCHIVE_NAME] = {};
		const wchar_t* openZip = (zipOut[0] ? zipOut : zipPath);
		if (CEmuArchiveStemFromPath(openZip, stem, (int)sizeof(stem))) {
			char prefer[CEMU_MODE_TAG] = {};
			CEmuModePrefGet(openZip, prefer, (int)sizeof(prefer));
			CEmuArchiveMode modes[CEMU_MODE_MAX];
			const int nm = CEmuCatalogListArchiveModes(&mgr->catalog, stem,
				NULL, NULL, modes, CEMU_MODE_MAX);
			const CEmuGameEntry* midGe = NULL;
			for (int pass = 0; pass < 2 && !midGe; pass++) {
				for (int i = 0; i < nm; i++) {
					if (!modes[i].isMidi) continue;
					if (pass == 0 && prefer[0]
						&& _stricmp(modes[i].tag, prefer) != 0)
						continue;
					if (modes[i].entryIndex < 0
						|| modes[i].entryIndex >= mgr->catalog.count)
						continue;
					const CEmuGameEntry* cand = mgr->catalog.entry[modes[i].entryIndex];
					if (cand && LiveModeEntryIsMidi(cand)) {
						midGe = cand;
						break;
					}
				}
			}
			if (midGe)
				ge = midGe;
		}
	}
	if (!ge || !LiveModeEntryIsMidi(ge))
		return 0;

	const wchar_t* openZip = zipOut[0] ? zipOut : zipPath;
	{
		char curTag[CEMU_MODE_TAG] = {};
		char fromGe[CEMU_MODE_TAG] = {};
		CEmuModeTagFromEntry(ge, fromGe, (int)sizeof(fromGe));
		if (!CEmuModePrefGet(openZip, curTag, (int)sizeof(curTag))
			|| !CEmuModeIsMidiTag(curTag)) {
			if (fromGe[0] && CEmuModeIsMidiTag(fromGe))
				CEmuModePrefSet(openZip, fromGe);
		}
	}
	CEmuZipFs* fs = (CEmuZipFs*)calloc(1, sizeof(CEmuZipFs));
	if (!fs) return 0;
	if (!CEmuZipFsOpen(fs, openZip)) { free(fs); return 0; }

	const int rate = kLiveRate;
	CHard* hard = CEmuHardCreate(ge, rate);
	CDriver* drv = CEmuDriverCreate(ge);
	const int okKind = hard && (hard->hardKind == CHard::KIND_PCAT
		|| hard->hardKind == CHard::KIND_PC98);
	if (!hard || !drv || !okKind
		|| !drv->Open(hard, ge, fs, titleCode ? titleCode : 0x10)) {
		if (drv) { drv->Close(); CEmuDriverDestroy(drv); }
		if (hard) CEmuHardDestroy(hard);
		CEmuZipFsClose(fs);
		free(fs);
		return 0;
	}

	/* MT-32 (type 1/2) → LA バンク。モニタは LAmap でパート名を付け、
	   プラグインはプログラム番号を MT-32 音色として読む。GM (8) / GS (4,6)
	   は GMmap/GSmap のまま。ラベル無し行は PCAT では MT-32、PC98 では
	   SC-55 (当時のドライバが相手にしていた音源)。 */
	const int isPc98 = (hard->hardKind == CHard::KIND_PC98) ? 1 : 0;
	int midiType = MidiOutTypeFromGe(ge);
	{
		char geTag[CEMU_MODE_TAG] = {};
		CEmuModeTagFromEntry(ge, geTag, (int)sizeof(geTag));
		if (!midiType) {
			if (!_stricmp(geTag, "LA") || !_stricmp(geTag, "MT-32")
				|| !_stricmp(geTag, "CM-64") || !_stricmp(geTag, "CM64"))
				midiType = 1;
			else if (!_stricmp(geTag, "GS") || !_stricmp(geTag, "SC-55"))
				midiType = 4;
			else if (!_stricmp(geTag, "SC-88") || !_stricmp(geTag, "SC88"))
				midiType = 7;
			else if (!_stricmp(geTag, "GM"))
				midiType = 8;
		}
	}
	int laBanks = MidiOutTypeIsLa(midiType);
	if (!isPc98 && midiType == 0)
		laBanks = 1;
	int stubType = midiType;
	if (laBanks && stubType != 1 && stubType != 2)
		stubType = 1;
	if (!stubType && isPc98)
		stubType = 4; /* PC98 無印は SC-55/GS */
	const char* stubTag = laBanks ? "MT-32"
		: (stubType == 8 ? "GM" : (stubType == 7 ? "SC-88" : "GS"));
	const char* song = NULL;
	if (isPc98) {
		CHardPc98* hw = (CHardPc98*)hard;
		/* FMP3 -m は UART。Falcom FMD / MMD インテリジェントは 3Fh を送らない。
		   Open 後に UART を強制すると ACK/CTH が壊れ MIDI が無音になる。 */
		if (hw->MidiIsUart())
			hw->MidiCaptureReset();
		else
			hw->MidiArmCapture();
		song = hw->DosSongName();
	} else {
		CHardPcat* hw = (CHardPcat*)hard;
		hw->MidiCaptureReset();
		hw->MidiForceUart(1);
		song = hw->DosSongName();
	}

	wchar_t tmpDir[MAX_PATH] = {};
	GetTempPathW(MAX_PATH, tmpDir);
	wchar_t midPath[MAX_PATH] = {};
	const wchar_t* tagW = L"gs";
	if (laBanks) tagW = L"mt32";
	else if (stubType == 8) tagW = L"gm";
	else if (stubType == 7) tagW = L"sc88";
	_snwprintf_s(midPath, _TRUNCATE, L"%scemu_mpu_%s_%08X.mid",
		tmpDir, tagW, (unsigned)GetTickCount());

	if (!WriteLiveStubSmf(midPath, (song && song[0]) ? song : stubTag,
		stubType)) {
		drv->Close();
		CEmuDriverDestroy(drv);
		CEmuHardDestroy(hard);
		CEmuZipFsClose(fs);
		free(fs);
		DeleteFileW(midPath);
		return 0;
	}

	EnterCriticalSection(&g_live.cs);
	g_live.fs = fs;
	g_live.hard = hard;
	g_live.drv = drv;
	g_live.ge = ge;
	wcsncpy_s(g_live.midPath, midPath, _TRUNCATE);
	wcsncpy_s(g_live.zipPath, openZip, _TRUNCATE);
	g_live.sampleRate = rate;
	g_live.midiCursor = 0;
	g_live.run = 0;
	g_live.need = 0;
	g_live.haveD0 = 0;
	g_live.pendingTicks = 0;
	g_live.tickRem = 0;
	g_live.midiSample = 0;
	g_live.audioSample = 0;
	g_live.holdN = 0;
	g_live.isMt32 = laBanks ? 1 : 0;
	g_live.midiType = stubType;
	g_live.laBanksSent = laBanks ? 1 : 0;
	g_live.setupSent = 0;
	g_live.cc111StartSent = 1;
	g_live.sawNotes = 0;
	g_live.noteOns = 0;
	g_live.initPcBurst = 0;
	g_live.drumPcRetrig = 0;
	g_live.holdNotesUntil = 0;
	g_live.resetHoldUntil = 0;
	g_live.sxHoldN = 0;
	g_live.sxHoldUsed = 0;
	memset(g_live.pc, 0, sizeof(g_live.pc));
	memset(g_live.cc, 0, sizeof(g_live.cc));
	memset(g_live.havePc, 0, sizeof(g_live.havePc));
	memset(g_live.haveCc, 0, sizeof(g_live.haveCc));
	memset(g_live.pbL, 0, sizeof(g_live.pbL));
	memset(g_live.pbM, 0, sizeof(g_live.pbM));
	memset(g_live.havePb, 0, sizeof(g_live.havePb));
	g_live.overlayCode = 0;
	g_live.ovlPhase = 0;
	g_live.ovlSeHeld = 0;
	g_live.injDropped = 0;
	g_live.holdDropped = 0;
	g_live.injPeak = 0;
	g_live.holdPeak = 0;
	LiveSysexAbort();
	memset(g_live.seBits, 0, sizeof(g_live.seBits));
	if (g_liveBootAsSfx) {
		g_live.overlayCode = titleCode;
		InterlockedExchange((LONG*)&g_live.overlayPend, 1);
		g_liveBootAsSfx = 0;
	} else {
		InterlockedExchange((LONG*)&g_live.overlayPend, 0);
	}
	g_live.injR = g_live.injW = 0;
	g_live.active = 1;
	LeaveCriticalSection(&g_live.cs);

	wcsncpy_s(outMidPath, (size_t)outCap, midPath, _TRUNCATE);
	return 1;
}

/* セッションレートで frames 進め、Steal 用ショートをキュー */
int CEmuMidiLivePump(int frames)
{
	if (frames <= 0) return 0;
	LiveEnsureCs();
	EnterCriticalSection(&g_live.cs);
	while (InterlockedCompareExchange(&g_live.inPump, 0, 0) != 0) {
		LeaveCriticalSection(&g_live.cs);
		Sleep(1);
		EnterCriticalSection(&g_live.cs);
	}
	if (!g_live.active || !g_live.drv || !g_live.hard) {
		LeaveCriticalSection(&g_live.cs);
		return 0;
	}
	const int kind = g_live.hard->hardKind;
	if (kind != CHard::KIND_PCAT && kind != CHard::KIND_PC98) {
		LeaveCriticalSection(&g_live.cs);
		return 0;
	}
	if (!g_live.mixBuf || g_live.mixCap < frames) {
		free(g_live.mixBuf);
		g_live.mixBuf = (int16_t*)malloc((size_t)frames * 4);
		g_live.mixCap = g_live.mixBuf ? frames : 0;
	}
	if (!g_live.mixBuf) {
		LeaveCriticalSection(&g_live.cs);
		return 0;
	}
	CDriver* drv = g_live.drv;
	CHard* hard = g_live.hard;
	int16_t* mix = g_live.mixBuf;
	int doOvl = 0;
	unsigned ovlCode = 0;
	if (InterlockedExchange((LONG*)&g_live.overlayPend, 0)) {
		LiveArmSfxCapture();
		doOvl = 1;
		ovlCode = g_live.overlayCode;
	}
	InterlockedExchange(&g_live.inPump, 1);
	LeaveCriticalSection(&g_live.cs);

	if (doOvl && drv)
		drv->OverlayTitle(ovlCode);
	drv->Render(mix, frames);

	EnterCriticalSection(&g_live.cs);
	InterlockedExchange(&g_live.inPump, 0);
	if (!g_live.active || g_live.drv != drv || g_live.hard != hard) {
		LeaveCriticalSection(&g_live.cs);
		return 1;
	}
	if (kind == CHard::KIND_PC98)
		LiveConsumeUartPc98((CHardPc98*)hard, frames);
	else
		LiveConsumeUart((CHardPcat*)hard, frames);
	g_live.audioSample += (__int64)frames;
	LiveOvlTick();
	LeaveCriticalSection(&g_live.cs);
	return 1;
}

/* Pump 累積フレーム。steal した short の sampleOfs の基準 */
__int64 CEmuMidiLiveAudioFrames(void)
{
	LiveEnsureCs();
	EnterCriticalSection(&g_live.cs);
	const __int64 n = g_live.active ? g_live.audioSample : 0;
	LeaveCriticalSection(&g_live.cs);
	return n;
}

int CEmuMidiLiveSampleRate(void)
{
	LiveEnsureCs();
	EnterCriticalSection(&g_live.cs);
	const int rate = g_live.active
		? (g_live.sampleRate > 0 ? g_live.sampleRate : kLiveRate)
		: kLiveRate;
	LeaveCriticalSection(&g_live.cs);
	return rate;
}

/* inject リングからショートを取り出す */
int CEmuMidiLiveStealShorts(CEmuMidiLiveShort* out, int maxCount)
{
	if (!out || maxCount < 1) return 0;
	LiveEnsureCs();
	int n = 0;
	EnterCriticalSection(&g_live.cs);
	LONG r = g_live.injR;
	const LONG w = g_live.injW;
	while (n < maxCount && r != w) {
		const int i = (int)(r & (kLiveInjCap - 1));
		out[n++] = g_live.inj[i];
		++r;
	}
	g_live.injR = r;
	LeaveCriticalSection(&g_live.cs);
	/* Host64/VST は sampleOfs で置く — 時系列を崩さない。 */
	for (int i = 1; i < n; i++) {
		CEmuMidiLiveShort t = out[i];
		int j = i;
		while (j > 0 && out[j - 1].sampleOfs > t.sampleOfs) {
			out[j] = out[j - 1];
			j--;
		}
		out[j] = t;
	}
	return n;
}

CHard* CEmuMidiLiveHard(void)
{
	return g_live.active ? g_live.hard : NULL;
}
