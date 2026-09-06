#include "StdAfx.h"
#include "cemu_midi_live.h"
#include "cemu_modepref.h"
#include "cemu_mgr.h"
#include "cemu_zipfs.h"
#include "driver/cemu_driver.h"
#include "machine/cemu_hard.h"
#include "machine/cemu_hard_pcat.h"
#include "machine/cemu_hard_pc98.h"
#include <string.h>

enum {
	kLiveInjCap = 512,
	kLiveHoldCap = 512,
	kLiveRate = 44100,
	/* SMF div 480 @ tempo 500000µs → 960 ticks/sec.
	   ReadVar in VstMidiEngine accepts at most 4 MIDI varlen bytes
	   (max 0x0FFFFFFF). 5-byte deltas make LoadSmf abort → length=2s pad. */
	kStubTicks = 0x0FFFFFFFu /* ~3.23 days @ 960 ticks/sec */
};

struct CEmuMidiLiveHold {
	DWORD msg;
	__int64 dueAbs; /* absolute MIDI-clock sample (never reset per pump) */
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
	/* UART → short msg parser */
	uint8_t run;
	int need;
	int haveD0;
	uint8_t d0;
	uint32_t pendingTicks;
	uint32_t tickRem; /* ticks→samples fractional remainder */
	/* Continuous clocks — do NOT reset midiSample each pump (that reordered
	 * deferred note-offs vs new note-ons and scrambled durations). */
	__int64 midiSample;  /* UART delta timeline → samples from stream start */
	__int64 audioSample; /* samples already pumped (= Host64 chunk base) */
	int isMt32;
	int laBanksSent;
	int cc111StartSent;
	int sawNotes;
	int noteOns;
	/* inject ring (ready for this audio block) */
	CEmuMidiLiveShort inj[kLiveInjCap];
	LONG injW;
	LONG injR;
	/* events with dueAbs still ahead of audioSample+frames */
	CEmuMidiLiveHold hold[kLiveHoldCap];
	int holdN;
	int16_t* mixBuf;
	int mixCap;
};

static CEmuMidiLive g_live;

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

/* Minimal Type-0 SMF: tempo, name, optional LA banks, CC#111=0, huge silence, EOT.
   VST opens immediately; realtime notes arrive via inject. */
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

	/* Multi-day body so Host64/local lengthSamples stays open for live inject.
	   No CC#111 end here — that would make VST loop an empty 4-day SMF. */
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

static void LiveEnsureCs(void)
{
	if (!g_live.csReady) {
		InitializeCriticalSection(&g_live.cs);
		g_live.csReady = 1;
	}
}

static void LivePushShort(DWORD msg, int sampleOfs)
{
	const LONG w = g_live.injW;
	if ((w - g_live.injR) >= (kLiveInjCap - 1)) return;
	const int i = (int)(w & (kLiveInjCap - 1));
	g_live.inj[i].msg = msg;
	g_live.inj[i].sampleOfs = sampleOfs;
	MemoryBarrier();
	g_live.injW = w + 1;
}

static void LiveHoldPushAbs(DWORD msg, __int64 dueAbs)
{
	if (g_live.holdN >= kLiveHoldCap) return;
	g_live.hold[g_live.holdN].msg = msg;
	g_live.hold[g_live.holdN].dueAbs = dueAbs;
	g_live.holdN++;
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

/* Place msg at absolute midiSample. ofs is relative to this pump's audioSample. */
static void LiveEmitTimed(DWORD msg, int frames)
{
	LiveAdvanceMidiClock();
	const __int64 ofs64 = g_live.midiSample - g_live.audioSample;
	if (frames <= 0) {
		LiveHoldPushAbs(msg, g_live.midiSample);
		return;
	}
	if (ofs64 < 0) {
		/* MIDI clock lagged audio (clamped gaps / boot). Snap forward. */
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
	/* Deferred events first — same absolute clock as new UART traffic. */
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
		LiveEmitTimed(msg, frames);

		if (hi == 0x90 && nData == 2 && data[1] > 0) {
			g_live.noteOns++;
			g_live.sawNotes = 1;
		}
	}

	/* Do NOT advance pendingTicks here — incomplete messages must keep their
	 * gap until the message completes (else note lengths double-count). */

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
		LiveEmitTimed(msg, frames);

		if (hi == 0x90 && nData == 2 && data[1] > 0) {
			g_live.noteOns++;
			g_live.sawNotes = 1;
		}
	}

	if (n >= (unsigned)CEMU_PC98_MIDI_CAP - 64) {
		hw->MidiCaptureReset();
		g_live.midiCursor = 0;
		g_live.pendingTicks = 0;
	}
}

int CEmuMidiLiveActive(void)
{
	return g_live.active ? 1 : 0;
}

int CEmuMidiLiveHasNotes(void)
{
	return g_live.sawNotes ? 1 : 0;
}

void CEmuMidiLiveStop(void)
{
	LiveEnsureCs();
	CDriver* drv = NULL;
	CHard* hard = NULL;
	CEmuZipFs* fs = NULL;
	int16_t* mixBuf = NULL;
	EnterCriticalSection(&g_live.cs);
	/* Detach under CS then destroy outside — HardDestroy/Render must not
	   run while another thread waits on this CS (Pump) or while we hold it. */
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

	/* PCAT MT-32 → LA banks in stub; PC98 SC-55/SC-88 → GS (no LA). */
	const int isPc98 = (hard->hardKind == CHard::KIND_PC98) ? 1 : 0;
	const int laBanks = isPc98 ? 0 : 1;
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
		tmpDir, isPc98 ? L"gs" : L"mt32", (unsigned)GetTickCount());

	if (!WriteLiveStubSmf(midPath, (song && song[0]) ? song : (isPc98 ? "GS" : "MT-32"),
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
	g_live.laBanksSent = 1; /* stub already has LA banks, or GS needs none */
	g_live.cc111StartSent = 1;
	g_live.sawNotes = 0;
	g_live.noteOns = 0;
	g_live.injR = g_live.injW = 0;
	g_live.active = 1;
	LeaveCriticalSection(&g_live.cs);

	wcsncpy_s(outMidPath, (size_t)outCap, midPath, _TRUNCATE);
	return 1;
}

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
	g_live.drv->Render(g_live.mixBuf, frames);
	if (kind == CHard::KIND_PC98)
		LiveConsumeUartPc98((CHardPc98*)g_live.hard, frames);
	else
		LiveConsumeUart((CHardPcat*)g_live.hard, frames);
	g_live.audioSample += (__int64)frames;
	LeaveCriticalSection(&g_live.cs);
	return 1;
}

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
	/* Host64/VST place by sampleOfs — keep chronological order. */
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
