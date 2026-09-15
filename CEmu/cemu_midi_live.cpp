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
#include <string.h>
#include <stdlib.h>

enum {
	kLiveInjCap = 512,
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
	int laBanksSent;
	int cc111StartSent;
	int sawNotes;
	int noteOns;
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

/* 最小 Type-0 SMF: tempo, 名前, 任意 LA バンク, CC#111=0, 長い無音, EOT。
   VST はすぐ開き、リアルタイム音符は inject で来る。 */
static int WriteLiveStubSmf(const wchar_t* path, const char* seqName, int laBanks)
{
	if (!path || !path[0]) return 0;
	uint8_t track[1024];
	unsigned tp = 0;

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

	SmfPutVar(track, &tp, 0);
	track[tp++] = 0xb0; track[tp++] = 111; track[tp++] = 0;

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

/* inject リングへ。満杯なら黙って落とす (PC/Off が消える) */
static void LivePushShort(DWORD msg, int sampleOfs)
{
	const LONG w = g_live.injW;
	const unsigned used = (unsigned)(w - g_live.injR);
	if (used > g_live.injPeak) g_live.injPeak = used;
	if (used >= (unsigned)(kLiveInjCap - 1)) {
		g_live.injDropped++;
		return;
	}
	const int i = (int)(w & (kLiveInjCap - 1));
	g_live.inj[i].msg = msg;
	g_live.inj[i].sampleOfs = sampleOfs;
	MemoryBarrier();
	g_live.injW = w + 1;
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

static void LiveFinishShort(DWORD msg, int frames)
{
	LiveEmitTimed(msg, frames);
	LiveTrackMsg(msg);
	if (((msg & 0xf0) == 0x90) && ((msg >> 16) & 0x7f) > 0) {
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

static void LiveConsumeUart(CHardPcat* hw, int frames)
{
	if (!hw) return;
	/* defer したイベントを先に — 新しい UART と同じ絶対時計。 */
	LiveFlushHolds(frames);

	const unsigned n = hw->MidiByteCount();
	while (g_live.midiCursor < n) {
		const unsigned i = g_live.midiCursor++;
		g_live.pendingTicks += hw->MidiDeltaAt(i);
		const uint8_t v = hw->MidiByteAt(i);
		if (v >= 0xf8) continue;

		if (v & 0x80) {
			g_live.haveD0 = 0;
			if (v == 0xf0) {
				int mt = 0;
				if (i + 3 < n && hw->MidiByteAt(i + 1) == 0x41
					&& hw->MidiByteAt(i + 3) == 0x16)
					mt = 1;
				for (; g_live.midiCursor < n; ) {
					const unsigned j = g_live.midiCursor;
					g_live.pendingTicks += hw->MidiDeltaAt(j);
					const uint8_t b = hw->MidiByteAt(j);
					g_live.midiCursor++;
					if (b == 0xf7) break;
					if (b >= 0xf8) continue;
					if (j > i + 1024) break;
				}
				if (mt) {
					g_live.isMt32 = 1;
					LiveEmitLaBanks(frames);
				} else {
					LiveAdvanceMidiClock();
				}
				g_live.run = 0;
				g_live.need = 0;
				continue;
			}
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
	}
}

static void LiveConsumeUartPc98(CHardPc98* hw, int frames)
{
	if (!hw) return;
	LiveFlushHolds(frames);

	const unsigned n = hw->MidiByteCount();
	while (g_live.midiCursor < n) {
		const unsigned i = g_live.midiCursor++;
		g_live.pendingTicks += hw->MidiDeltaAt(i);
		const uint8_t v = hw->MidiByteAt(i);
		if (v >= 0xf8) continue;

		if (v & 0x80) {
			g_live.haveD0 = 0;
			if (v == 0xf0) {
				for (; g_live.midiCursor < n; ) {
					const unsigned j = g_live.midiCursor;
					g_live.pendingTicks += hw->MidiDeltaAt(j);
					const uint8_t b = hw->MidiByteAt(j);
					g_live.midiCursor++;
					if (b == 0xf7) break;
					if (b >= 0xf8) continue;
					if (j > i + 1024) break;
				}
				LiveAdvanceMidiClock();
				g_live.run = 0;
				g_live.need = 0;
				continue;
			}
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
	}
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
	   入れ子にならないようにする。 */
	drv = g_live.drv; g_live.drv = NULL;
	hard = g_live.hard; g_live.hard = NULL;
	fs = g_live.fs; g_live.fs = NULL;
	mixBuf = g_live.mixBuf; g_live.mixBuf = NULL; g_live.mixCap = 0;
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
	g_live.laBanksSent = 0;
	g_live.cc111StartSent = 0;
	g_live.sawNotes = 0;
	g_live.noteOns = 0;
	g_live.zipPath[0] = 0;
	g_live.overlayCode = 0;
	g_live.ovlPhase = 0;
	g_live.ovlSeHeld = 0;
	g_live.injDropped = 0;
	g_live.holdDropped = 0;
	g_live.injPeak = 0;
	g_live.holdPeak = 0;
	memset(g_live.seBits, 0, sizeof(g_live.seBits));
	InterlockedExchange((LONG*)&g_live.overlayPend, 0);
	g_live.injR = g_live.injW;
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
	return (t == 1 || t == 2) ? 1 : 0;
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

	CEmuModePrefSet(zipPath, "MIDI");
	wchar_t zipOut[CEMU_ZIP_PATH];
	char dataDir[CEMU_DATA_DIR];
	CEmuMgr* mgr = CEmuMgrGet();
	const CEmuGameEntry* ge = CEmuMgrResolveZip(mgr, zipPath, zipOut,
		(int)_countof(zipOut), dataDir, (int)sizeof(dataDir));
	if (!ge || !LiveModeEntryIsMidi(ge)) {
		char stem[CEMU_ARCHIVE_NAME] = {};
		const wchar_t* openZip = (zipOut[0] ? zipOut : zipPath);
		if (CEmuArchiveStemFromPath(openZip, stem, (int)sizeof(stem))) {
			const CEmuGameEntry* midGe = CEmuCatalogFindArchiveForZipMode(
				&mgr->catalog, stem, NULL, NULL, "MIDI");
			if (midGe && LiveModeEntryIsMidi(midGe))
				ge = midGe;
		}
	}
	if (!ge || !LiveModeEntryIsMidi(ge))
		return 0;

	const wchar_t* openZip = zipOut[0] ? zipOut : zipPath;
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
	const int midiType = MidiOutTypeFromGe(ge);
	int laBanks = MidiOutTypeIsLa(midiType);
	if (!isPc98 && midiType == 0)
		laBanks = 1;
	const char* stubTag = laBanks ? "MT-32" : (midiType == 8 ? "GM" : "GS");
	const char* song = NULL;
	if (isPc98) {
		CHardPc98* hw = (CHardPc98*)hard;
		hw->MidiCaptureReset();
		hw->MidiForceUart(1);
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
	_snwprintf_s(midPath, _TRUNCATE, L"%scemu_mpu_%s_%08X.mid",
		tmpDir, laBanks ? L"mt32" : (isPc98 ? L"gs" : L"gm"), (unsigned)GetTickCount());

	if (!WriteLiveStubSmf(midPath, (song && song[0]) ? song : stubTag,
		laBanks)) {
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
	g_live.laBanksSent = 1; /* スタブに既に LA がある、または GS は不要 */
	g_live.cc111StartSent = 1;
	g_live.sawNotes = 0;
	g_live.noteOns = 0;
	g_live.overlayCode = 0;
	g_live.ovlPhase = 0;
	g_live.ovlSeHeld = 0;
	g_live.injDropped = 0;
	g_live.holdDropped = 0;
	g_live.injPeak = 0;
	g_live.holdPeak = 0;
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
	if (InterlockedExchange((LONG*)&g_live.overlayPend, 0)) {
		LiveArmSfxCapture();
		if (g_live.drv)
			g_live.drv->OverlayTitle(g_live.overlayCode);
	}
	g_live.drv->Render(g_live.mixBuf, frames);
	if (kind == CHard::KIND_PC98)
		LiveConsumeUartPc98((CHardPc98*)g_live.hard, frames);
	else
		LiveConsumeUart((CHardPcat*)g_live.hard, frames);
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
