#include "stdafx.h"
#include "PcHwMidiIn.h"
#include "CCustomControl.h"
#include "VstMidiEngine.h"
#include <mmsystem.h>

extern save savedata;

enum {
	PCHW_PORT_N = 2,
	PCHW_SX_BUFS = 4,
	PCHW_SX_BYTES = 1024,
	PCHW_DEV_MAX = 32,
	PCHW_TAP_SHORT_N = 512,
	PCHW_TAP_SX_N = 32,
	PCHW_TAP_SX_B = 512,
	PCHW_REC_EV_MAX = 65536,
	PCHW_REC_SX_POOL = 256 * 1024,
	PCHW_SMF_TPQ = 480
};

struct PcSxBuf {
	MIDIHDR hdr;
	BYTE data[PCHW_SX_BYTES];
	HMIDIIN in;
	volatile LONG prepared;
	volatile LONG needAdd;
};

struct PcRecEv {
	DWORD ms;
	BYTE kind; /* 0=short 1=sysex */
	BYTE pad;
	WORD sxLen;
	DWORD msg;
	DWORD sxOff;
};

struct PcTapSx {
	BYTE port;
	unsigned short len;
	BYTE d[PCHW_TAP_SX_B];
};

struct PcTapShort {
	BYTE port;
	DWORD msg;
};

static CRITICAL_SECTION g_cs;
static LONG g_csReady = 0;
static volatile LONG g_stopRecycle = 0;

static HMIDIIN g_hIn[PCHW_PORT_N];
static int g_inDev[PCHW_PORT_N] = { -1, -1 };
static PcSxBuf g_sxBuf[PCHW_PORT_N][PCHW_SX_BUFS];

static HMIDIOUT g_hOut = NULL;
static int g_outMode = 0; /* 0=none 1=mapper 2=named */

static int g_recOn[PCHW_PORT_N];
static DWORD g_recOriginMs[PCHW_PORT_N];
static CString g_recPath[PCHW_PORT_N];
static PcRecEv g_recEv[PCHW_PORT_N][PCHW_REC_EV_MAX];
static int g_recN[PCHW_PORT_N];
static BYTE g_recSxPool[PCHW_PORT_N][PCHW_REC_SX_POOL];
static int g_recSxUsed[PCHW_PORT_N];
static BYTE g_held[PCHW_PORT_N][16][16]; /* 16ch × 128 notes / 8 */

static int g_inComboDev[PCHW_PORT_N][PCHW_DEV_MAX + 1];
static int g_outComboDev[PCHW_DEV_MAX + 2];

static PcTapSx g_outSx[8];
static volatile LONG g_outSxW = 0;
static volatile LONG g_outSxR = 0;

static PcTapShort g_tapShort[PCHW_TAP_SHORT_N];
static volatile LONG g_tapShortW = 0;
static volatile LONG g_tapShortR = 0;
static PcTapSx g_tapSx[PCHW_TAP_SX_N];
static volatile LONG g_tapSxW = 0;
static volatile LONG g_tapSxR = 0;

static void EnsureCs()
{
	if (InterlockedCompareExchange(&g_csReady, 1, 0) == 0)
		InitializeCriticalSection(&g_cs);
}

static TCHAR* InNameField(int port)
{
	return (port == 0) ? savedata.pcMidiIn1Name : savedata.pcMidiIn2Name;
}

static int FindMidiInByName(const TCHAR* name)
{
	if (!name || !name[0]) return -1;
	const UINT n = midiInGetNumDevs();
	for (UINT i = 0; i < n; ++i) {
		MIDIINCAPS c = {};
		if (midiInGetDevCaps(i, &c, sizeof(c)) != MMSYSERR_NOERROR) continue;
		if (_tcsicmp(c.szPname, name) == 0) return (int)i;
	}
	return -1;
}

static int FindMidiOutByName(const TCHAR* name)
{
	if (!name || !name[0]) return -1;
	const UINT n = midiOutGetNumDevs();
	for (UINT i = 0; i < n; ++i) {
		MIDIOUTCAPS c = {};
		if (midiOutGetDevCaps(i, &c, sizeof(c)) != MMSYSERR_NOERROR) continue;
		if (_tcsicmp(c.szPname, name) == 0) return (int)i;
	}
	return -1;
}

static void PersistInName(int port, int dev)
{
	TCHAR* dst = InNameField(port);
	if (dev < 0) {
		dst[0] = 0;
		return;
	}
	MIDIINCAPS c = {};
	if (midiInGetDevCaps((UINT)dev, &c, sizeof(c)) == MMSYSERR_NOERROR)
		_tcsncpy_s(dst, 32, c.szPname, _TRUNCATE);
	else
		dst[0] = 0;
}

static void TapPushShort(int port, DWORD msg)
{
	const LONG w = g_tapShortW;
	if ((LONG)(w - g_tapShortR) >= PCHW_TAP_SHORT_N - 1) return;
	const int i = (int)(w & (PCHW_TAP_SHORT_N - 1));
	g_tapShort[i].port = (BYTE)port;
	g_tapShort[i].msg = msg;
	MemoryBarrier();
	g_tapShortW = w + 1;
}

static void TapPushSysex(int port, const BYTE* d, int n)
{
	if (!d || n <= 0) return;
	if (n > PCHW_TAP_SX_B) n = PCHW_TAP_SX_B;
	const LONG w = g_tapSxW;
	if ((LONG)(w - g_tapSxR) >= PCHW_TAP_SX_N - 1) return;
	const int i = (int)(w & (PCHW_TAP_SX_N - 1));
	g_tapSx[i].port = (BYTE)port;
	g_tapSx[i].len = (unsigned short)n;
	memcpy(g_tapSx[i].d, d, (size_t)n);
	MemoryBarrier();
	g_tapSxW = w + 1;
}

static int RecNoteIsHeld(int port, int ch, int note)
{
	if (ch < 0 || ch > 15 || note < 0 || note > 127) return 0;
	return (g_held[port][ch][note >> 3] & (BYTE)(1u << (note & 7))) ? 1 : 0;
}

static void RecNoteHold(int port, DWORD msg)
{
	const int st = (int)(msg & 0xf0);
	const int ch = (int)(msg & 0x0f);
	const int note = (int)((msg >> 8) & 0x7f);
	const int vel = (int)((msg >> 16) & 0x7f);
	if (ch < 0 || ch > 15) return;
	BYTE* bits = g_held[port][ch];
	if (st == 0x90 && vel > 0) {
		if (note < 0 || note > 127) return;
		bits[note >> 3] = (BYTE)(bits[note >> 3] | (BYTE)(1u << (note & 7)));
	}
	else if (st == 0x80 || (st == 0x90 && vel == 0)) {
		if (note < 0 || note > 127) return;
		bits[note >> 3] = (BYTE)(bits[note >> 3] & (BYTE)~(1u << (note & 7)));
	}
	/* CC 120/123 は RecFlushChHeld が個別 Off を書いてからビットを落とす。
	   ここで memset すると、バッファ満杯で Off を書けなかった音が SMF から消える。 */
}

static DWORD RecMakeNoteOff(int ch, int note, int vel)
{
	if (ch < 0) ch = 0;
	if (ch > 15) ch = 15;
	if (note < 0) note = 0;
	if (note > 127) note = 127;
	if (vel <= 0) vel = 0x40;
	if (vel > 127) vel = 127;
	return (DWORD)(0x80 | ch) | ((DWORD)note << 8) | ((DWORD)vel << 16);
}

static int RecAppendShort(int port, DWORD msg, DWORD ms)
{
	if (g_recN[port] >= PCHW_REC_EV_MAX) return 0;
	PcRecEv& e = g_recEv[port][g_recN[port]++];
	e.ms = ms;
	e.kind = 0;
	e.sxLen = 0;
	e.msg = msg;
	e.sxOff = 0;
	return 1;
}

/* 鳴っている音を 8n で全部閉じる。CC All Notes/Sound Off や保存終了で使う。 */
static void RecFlushChHeld(int port, int ch, DWORD ms)
{
	if (ch < 0 || ch > 15) return;
	BYTE* bits = g_held[port][ch];
	for (int note = 0; note < 128; ++note) {
		if ((bits[note >> 3] & (BYTE)(1u << (note & 7))) == 0) continue;
		if (!RecAppendShort(port, RecMakeNoteOff(ch, note, 0x40), ms))
			break;
		bits[note >> 3] = (BYTE)(bits[note >> 3] & (BYTE)~(1u << (note & 7)));
	}
}

static int RecShortDataLen(DWORD msg)
{
	const int st = (int)(msg & 0xff);
	if (st >= 0xF8) return 0; /* Clock / Active Sensing 等。SMF には書かない */
	if (st == 0xF1 || st == 0xF3) return 1;
	if (st == 0xF2) return 2;
	if (st == 0xF6) return 0;
	const int cmd = st & 0xf0;
	if (cmd == 0xC0 || cmd == 0xD0) return 1;
	if (cmd >= 0x80 && cmd <= 0xE0) return 2;
	return -1;
}

static void RecPushShort(int port, DWORD msg)
{
	if (!g_recOn[port]) return;
	const int st8 = (int)(msg & 0xff);
	/* リアルタイム（F8..FF）を 3 バイトとして書くと、後続の Note Off が
	   パーサに食われて「Off が漏れた」ように聞こえる。 */
	if (st8 >= 0xF8) return;
	if (st8 >= 0xF0) return;

	const int st = st8 & 0xf0;
	const int ch = st8 & 0x0f;
	const int d1 = (int)((msg >> 8) & 0x7f);
	const int vel = (int)((msg >> 16) & 0x7f);
	const DWORD now = timeGetTime();

	/* 9n vel=0 は Off。vel0 の On を無視する再生系があるので 8n に揃える。 */
	if (st == 0x90 && vel == 0)
		msg = RecMakeNoteOff(ch, d1, 0x40);

	/* 同じ音の On が Off 無しで続く（連打・レガート）。先に Off を挟まないと
	   SMF では最初の On が鳴りっぱなしになる。 */
	if (st == 0x90 && vel > 0 && RecNoteIsHeld(port, ch, d1)) {
		const DWORD off = RecMakeNoteOff(ch, d1, 0x40);
		if (RecAppendShort(port, off, now))
			RecNoteHold(port, off);
	}

	/* All Sound Off / All Notes Off は CC だけだと SMF 再生で Off 扱いにならないことが多い。 */
	if (st == 0xb0 && (d1 == 120 || d1 == 123))
		RecFlushChHeld(port, ch, now);

	if (!RecAppendShort(port, msg, now))
		return;
	RecNoteHold(port, msg);
}

static void RecPushSysex(int port, const BYTE* d, int n)
{
	if (!g_recOn[port] || !d || n <= 0) return;
	if (g_recN[port] >= PCHW_REC_EV_MAX) return;
	if (g_recSxUsed[port] + n > PCHW_REC_SX_POOL) return;
	const int off = g_recSxUsed[port];
	memcpy(g_recSxPool[port] + off, d, (size_t)n);
	g_recSxUsed[port] += n;
	PcRecEv& e = g_recEv[port][g_recN[port]++];
	e.ms = timeGetTime();
	e.kind = 1;
	e.sxLen = (WORD)n;
	e.msg = 0;
	e.sxOff = (DWORD)off;
}

static void MidiOutShort(DWORD msg)
{
	if (g_hOut)
		midiOutShortMsg(g_hOut, msg);
}

static void QueueOutSysex(const BYTE* d, int n)
{
	if (!g_hOut || !d || n <= 0) return;
	if (n > PCHW_TAP_SX_B) n = PCHW_TAP_SX_B;
	const LONG w = g_outSxW;
	if ((LONG)(w - g_outSxR) >= 7) return;
	const int i = (int)(w & 7);
	g_outSx[i].len = (unsigned short)n;
	memcpy(g_outSx[i].d, d, (size_t)n);
	MemoryBarrier();
	g_outSxW = w + 1;
}

static void FlushOutSysex()
{
	if (!g_hOut) return;
	for (;;) {
		const LONG r = g_outSxR;
		if (r == g_outSxW) break;
		const int i = (int)(r & 7);
		MIDIHDR hdr = {};
		hdr.lpData = (LPSTR)g_outSx[i].d;
		hdr.dwBufferLength = (DWORD)g_outSx[i].len;
		if (midiOutPrepareHeader(g_hOut, &hdr, sizeof(hdr)) == MMSYSERR_NOERROR) {
			midiOutLongMsg(g_hOut, &hdr, sizeof(hdr));
			for (int w = 0; w < 40 && !(hdr.dwFlags & MHDR_DONE); ++w)
				Sleep(1);
			midiOutUnprepareHeader(g_hOut, &hdr, sizeof(hdr));
		}
		MemoryBarrier();
		g_outSxR = r + 1;
	}
}

static void DispatchShort(int port, DWORD msg)
{
	TapPushShort(port, msg);
	VstLiveMidiShort(port, msg);
	MidiOutShort(msg);
	EnterCriticalSection(&g_cs);
	RecPushShort(port, msg);
	LeaveCriticalSection(&g_cs);
}

static void DispatchSysex(int port, const BYTE* d, int n)
{
	TapPushSysex(port, d, n);
	VstLiveMidiSysex(port, d, n);
	QueueOutSysex(d, n);
	EnterCriticalSection(&g_cs);
	RecPushSysex(port, d, n);
	LeaveCriticalSection(&g_cs);
}

static void CALLBACK MidiInProc(HMIDIIN hmi, UINT msg, DWORD_PTR /*inst*/, DWORD_PTR p1, DWORD_PTR)
{
	if (!hmi) return;
	int port = -1;
	for (int i = 0; i < PCHW_PORT_N; ++i)
		if (g_hIn[i] == hmi) { port = i; break; }
	if (port < 0) return;
	/* MIDI_IO_STATUS 時は処理が追いつかないと MIM_MOREDATA になる。
	   捨てると Note Off が抜けて SMF で音が残る。DATA と同じく録る。 */
	if (msg == MIM_DATA || msg == MIM_MOREDATA) {
		DispatchShort(port, (DWORD)p1);
		return;
	}
	if (msg == MIM_LONGDATA) {
		MIDIHDR* hdr = (MIDIHDR*)p1;
		if (!hdr) return;
		const int bytes = (int)hdr->dwBytesRecorded;
		if (bytes > 0)
			DispatchSysex(port, (const BYTE*)hdr->lpData, bytes);
		if (bytes > 0 && InterlockedCompareExchange(&g_stopRecycle, 0, 0) == 0)
			midiInAddBuffer(hmi, hdr, sizeof(MIDIHDR));
	}
}

static void CloseInPort(int port)
{
	if (port < 0 || port >= PCHW_PORT_N) return;
	HMIDIIN h = g_hIn[port];
	g_hIn[port] = NULL;
	g_inDev[port] = -1;
	if (!h) return;
	InterlockedExchange(&g_stopRecycle, 1);
	midiInStop(h);
	midiInReset(h);
	for (int b = 0; b < PCHW_SX_BUFS; ++b) {
		PcSxBuf& s = g_sxBuf[port][b];
		InterlockedExchange(&s.needAdd, 0);
		if (InterlockedExchange(&s.prepared, 0) == 1)
			midiInUnprepareHeader(h, &s.hdr, sizeof(MIDIHDR));
		s.in = NULL;
	}
	midiInClose(h);
	InterlockedExchange(&g_stopRecycle, 0);
}

static void OpenInPort(int port, int dev)
{
	EnsureCs();
	if (port < 0 || port >= PCHW_PORT_N) return;
	if (g_hIn[port] && g_inDev[port] == dev) {
		PersistInName(port, dev);
		return;
	}
	CloseInPort(port);
	if (dev < 0) {
		PersistInName(port, -1);
		return;
	}
	HMIDIIN h = NULL;
	if (midiInOpen(&h, (UINT)dev, (DWORD_PTR)MidiInProc, 0,
		CALLBACK_FUNCTION | MIDI_IO_STATUS) != MMSYSERR_NOERROR) {
		PersistInName(port, dev);
		return;
	}
	g_hIn[port] = h;
	g_inDev[port] = dev;
	PersistInName(port, dev);
	InterlockedExchange(&g_stopRecycle, 0);
	for (int b = 0; b < PCHW_SX_BUFS; ++b) {
		PcSxBuf& s = g_sxBuf[port][b];
		ZeroMemory(&s.hdr, sizeof(s.hdr));
		s.hdr.lpData = (LPSTR)s.data;
		s.hdr.dwBufferLength = PCHW_SX_BYTES;
		s.in = h;
		InterlockedExchange(&s.needAdd, 0);
		if (midiInPrepareHeader(h, &s.hdr, sizeof(MIDIHDR)) != MMSYSERR_NOERROR)
			continue;
		InterlockedExchange(&s.prepared, 1);
		midiInAddBuffer(h, &s.hdr, sizeof(MIDIHDR));
	}
	midiInStart(h);
}

static void CloseOut()
{
	if (!g_hOut) return;
	midiOutReset(g_hOut);
	midiOutClose(g_hOut);
	g_hOut = NULL;
}

static void OpenOutFromMode()
{
	EnsureCs();
	HMIDIOUT old = g_hOut;
	g_hOut = NULL;
	if (old) {
		midiOutReset(old);
		midiOutClose(old);
	}
	if (g_outMode == 0) {
		savedata.pcMidiOutMode = 0;
		savedata.pcMidiOutName[0] = 0;
		return;
	}
	UINT id = MIDI_MAPPER;
	if (g_outMode == 2) {
		const int found = FindMidiOutByName(savedata.pcMidiOutName);
		if (found >= 0) id = (UINT)found;
		else {
			g_outMode = 0;
			savedata.pcMidiOutMode = 0;
			return;
		}
	} else {
		savedata.pcMidiOutName[0] = 0;
	}
	savedata.pcMidiOutMode = g_outMode;
	HMIDIOUT h = NULL;
	if (midiOutOpen(&h, id, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
		if (id != MIDI_MAPPER) {
			if (midiOutOpen(&h, MIDI_MAPPER, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
				h = NULL;
		} else
			h = NULL;
	}
	g_hOut = h;
}

static void RecycleSysexUi()
{
	FlushOutSysex();
	if (InterlockedCompareExchange(&g_stopRecycle, 0, 0) != 0) return;
	for (int p = 0; p < PCHW_PORT_N; ++p) {
		HMIDIIN h = g_hIn[p];
		if (!h) continue;
		for (int b = 0; b < PCHW_SX_BUFS; ++b) {
			PcSxBuf& s = g_sxBuf[p][b];
			if (InterlockedCompareExchange(&s.needAdd, 0, 1) != 1) continue;
			if (InterlockedCompareExchange(&s.prepared, 0, 0) != 1) continue;
			s.hdr.dwBytesRecorded = 0;
			midiInAddBuffer(h, &s.hdr, sizeof(MIDIHDR));
		}
	}
}

static int PickSmfPath(int port, CWnd* owner, CString& outPath)
{
	CCC_ModalUiGuard modal;
	const wchar_t* defName = (port == 0) ? L"midiin1.mid" : L"midiin2.mid";
	CWnd* parent = CCC_GetActiveMainWindow();
	if (!parent) parent = owner;
	CFileDialog dlg(FALSE, _T("mid"), defName,
		OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT | OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_NOTESTFILECREATE,
		LL14(L"MIDI (*.mid)|*.mid|すべて (*.*)|*.*||", L"MIDI (*.mid)|*.mid|All (*.*)|*.*||",
			L"MIDI (*.mid)|*.mid|Tous (*.*)|*.*||", L"MIDI (*.mid)|*.mid|Tutti (*.*)|*.*||",
			L"MIDI (*.mid)|*.mid|Todos (*.*)|*.*||", L"MIDI (*.mid)|*.mid|모두 (*.*)|*.*||",
			L"MIDI (*.mid)|*.mid|全部 (*.*)|*.*||", L"MIDI (*.mid)|*.mid|الكل (*.*)|*.*||",
			L"MIDI (*.mid)|*.mid|Все (*.*)|*.*||", L"MIDI (*.mid)|*.mid|Alle (*.*)|*.*||",
			L"MIDI (*.mid)|*.mid|Todos (*.*)|*.*||", L"MIDI (*.mid)|*.mid|Alles (*.*)|*.*||",
			L"MIDI (*.mid)|*.mid|Wszystkie (*.*)|*.*||", L"MIDI (*.mid)|*.mid|Tumu (*.*)|*.*||"),
		parent);
	if (dlg.DoModal() != IDOK) return 0;
	outPath = dlg.GetPathName();
	return outPath.IsEmpty() ? 0 : 1;
}

static void StartRec(int port)
{
	CString path;
	if (!PickSmfPath(port, NULL, path))
		return;
	EnsureCs();
	EnterCriticalSection(&g_cs);
	g_recOn[port] = 1;
	g_recN[port] = 0;
	g_recSxUsed[port] = 0;
	g_recOriginMs[port] = timeGetTime();
	g_recPath[port] = path;
	memset(g_held[port], 0, sizeof(g_held[port]));
	LeaveCriticalSection(&g_cs);
}

static void PutVlq(BYTE* dst, int& len, int cap, int v)
{
	if (v < 0) v = 0;
	BYTE stack[5];
	int n = 0;
	stack[n++] = (BYTE)(v & 0x7f);
	while ((v >>= 7) > 0 && n < 5) {
		stack[n] = (BYTE)((v & 0x7f) | 0x80);
		n++;
	}
	for (int i = n - 1; i >= 0 && len < cap; --i)
		dst[len++] = stack[i];
}

static int MsToTicks(DWORD ms, DWORD origin)
{
	const DWORD d = ms - origin;
	/* 480 PPQN @ 120 BPM → 960 ticks/sec */
	return (int)(((__int64)d * 960) / 1000);
}

static int WriteSmf(int port, CWnd* owner)
{
	EnsureCs();
	PcRecEv ev[PCHW_REC_EV_MAX];
	int evN = 0;
	BYTE sxCopy[PCHW_REC_SX_POOL];
	int sxUsed = 0;
	DWORD origin = 0;
	BYTE held[16][16];
	EnterCriticalSection(&g_cs);
	g_recOn[port] = 0;
	evN = g_recN[port];
	if (evN > PCHW_REC_EV_MAX) evN = PCHW_REC_EV_MAX;
	if (evN > 0)
		memcpy(ev, g_recEv[port], (size_t)evN * sizeof(PcRecEv));
	sxUsed = g_recSxUsed[port];
	if (sxUsed > PCHW_REC_SX_POOL) sxUsed = PCHW_REC_SX_POOL;
	if (sxUsed > 0)
		memcpy(sxCopy, g_recSxPool[port], (size_t)sxUsed);
	origin = g_recOriginMs[port];
	memcpy(held, g_held[port], sizeof(held));
	g_recN[port] = 0;
	g_recSxUsed[port] = 0;
	memset(g_held[port], 0, sizeof(g_held[port]));
	LeaveCriticalSection(&g_cs);

	const DWORD now = timeGetTime();
	for (int ch = 0; ch < 16; ++ch) {
		for (int note = 0; note < 128; ++note) {
			if ((held[ch][note >> 3] & (BYTE)(1u << (note & 7))) == 0) continue;
			if (evN >= PCHW_REC_EV_MAX) break;
			PcRecEv& e = ev[evN++];
			e.ms = now;
			e.kind = 0;
			e.sxLen = 0;
			e.msg = (DWORD)(0x80 | ch) | ((DWORD)note << 8) | (0x40u << 16);
			e.sxOff = 0;
		}
	}

	if (evN < 1) {
		if (owner) {
			CCC_ModalUiGuard modal;
			owner->MessageBox(
				LL14(L"録った MIDI がありません。デバイスを選んでから SMF 保存を開始してください。",
					L"No MIDI captured. Choose a device, then start SMF save.",
					L"Aucun MIDI. Choisissez un peripherique puis demarrez.",
					L"Nessun MIDI. Scegli un dispositivo e avvia.",
					L"Sin MIDI. Elija un dispositivo y luego inicie.",
					L"녹음된 MIDI가 없습니다. 장치를 고른 뒤 SMF 저장을 시작하세요.",
					L"没有录到 MIDI。请先选择设备再开始 SMF 保存。",
					L"لا MIDI. اختر جهازاً ثم ابدأ الحفظ.",
					L"Нет MIDI. Выберите устройство и начните запись.",
					L"Kein MIDI. Geraet waehlen, dann SMF starten.",
					L"Sem MIDI. Escolha um dispositivo e inicie.",
					L"Geen MIDI. Kies een apparaat en start.",
					L"Brak MIDI. Wybierz urzadzenie i zacznij.",
					L"MIDI yok. Aygit secip SMF kaydini baslatin."),
				LL14(L"SMF保存", L"SMF save", L"Enregistrement SMF", L"Salvataggio SMF", L"Guardar SMF",
					L"SMF 저장", L"SMF保存", L"حفظ SMF", L"Сохранение SMF", L"SMF speichern",
					L"Gravar SMF", L"SMF opslaan", L"Zapis SMF", L"SMF kaydet"),
				MB_OK | MB_ICONINFORMATION);
		}
		return 0;
	}

	CString path = g_recPath[port];
	g_recPath[port].Empty();
	if (path.IsEmpty()) {
		if (!PickSmfPath(port, owner, path))
			return 0;
	}

	const int cap = 1 << 20;
	BYTE* track = (BYTE*)malloc((size_t)cap);
	if (!track) return 0;
	int trackLen = 0;
	PutVlq(track, trackLen, cap, 0);
	if (trackLen + 6 < cap) {
		track[trackLen++] = 0xFF;
		track[trackLen++] = 0x51;
		track[trackLen++] = 0x03;
		track[trackLen++] = 0x07;
		track[trackLen++] = 0xA1;
		track[trackLen++] = 0x20;
	}
	PutVlq(track, trackLen, cap, 0);
	const char* tname = (port == 0) ? "MIDI In 1" : "MIDI In 2";
	const int tn = (int)strlen(tname);
	if (trackLen + 3 + tn < cap) {
		track[trackLen++] = 0xFF;
		track[trackLen++] = 0x03;
		track[trackLen++] = (BYTE)tn;
		memcpy(track + trackLen, tname, (size_t)tn);
		trackLen += tn;
	}

	int lastTick = 0;
	for (int i = 0; i < evN; ++i) {
		int tick = MsToTicks(ev[i].ms, origin);
		if (tick < lastTick) tick = lastTick;
		if (ev[i].kind == 0) {
			DWORD m = ev[i].msg;
			const int st8 = (int)(m & 0xff);
			if (st8 >= 0xF8) continue; /* リアルタイムはファイルに残さない */
			if ((m & 0xf0) == 0x90 && ((m >> 16) & 0x7f) == 0)
				m = RecMakeNoteOff((int)(m & 0x0f), (int)((m >> 8) & 0x7f), 0x40);
			const int dataN = RecShortDataLen(m);
			if (dataN < 0) continue;
			PutVlq(track, trackLen, cap, tick - lastTick);
			lastTick = tick;
			if (trackLen + 1 + dataN >= cap) break;
			track[trackLen++] = (BYTE)(m & 0xff);
			if (dataN >= 1)
				track[trackLen++] = (BYTE)((m >> 8) & 0xff);
			if (dataN >= 2)
				track[trackLen++] = (BYTE)((m >> 16) & 0xff);
		} else {
			const int n = (int)ev[i].sxLen;
			const BYTE* d = sxCopy + (int)ev[i].sxOff;
			if (n <= 0 || (int)ev[i].sxOff + n > sxUsed) continue;
			int payload = n;
			if (d[0] == 0xF0) {
				payload = n - 1;
				d += 1;
			}
			PutVlq(track, trackLen, cap, tick - lastTick);
			lastTick = tick;
			if (trackLen + 1 >= cap) break;
			track[trackLen++] = 0xF0;
			PutVlq(track, trackLen, cap, payload);
			if (trackLen + payload >= cap) break;
			memcpy(track + trackLen, d, (size_t)payload);
			trackLen += payload;
		}
	}
	PutVlq(track, trackLen, cap, 0);
	if (trackLen + 3 < cap) {
		track[trackLen++] = 0xFF;
		track[trackLen++] = 0x2F;
		track[trackLen++] = 0x00;
	}

	CFile f;
	int ok = 0;
	if (f.Open(path, CFile::modeCreate | CFile::modeWrite | CFile::typeBinary)) {
		BYTE hdr[14] = {
			'M','T','h','d', 0,0,0,6, 0,0, 0,1,
			(BYTE)(PCHW_SMF_TPQ >> 8), (BYTE)(PCHW_SMF_TPQ & 0xff)
		};
		f.Write(hdr, 14);
		BYTE th[8] = { 'M','T','r','k', 0,0,0,0 };
		th[4] = (BYTE)((trackLen >> 24) & 0xff);
		th[5] = (BYTE)((trackLen >> 16) & 0xff);
		th[6] = (BYTE)((trackLen >> 8) & 0xff);
		th[7] = (BYTE)(trackLen & 0xff);
		f.Write(th, 8);
		f.Write(track, trackLen);
		f.Close();
		ok = 1;
	}
	free(track);
	return ok;
}

static void InComboCb(void* ctx, int index, LPCTSTR)
{
	const int port = (int)(INT_PTR)ctx;
	if (port < 0 || port >= PCHW_PORT_N) return;
	if (index < 0) index = 0;
	if (index > PCHW_DEV_MAX) index = PCHW_DEV_MAX;
	OpenInPort(port, g_inComboDev[port][index]);
}

static void OutComboCb(void*, int index, LPCTSTR)
{
	EnsureCs();
	if (index < 0) index = 0;
	if (index > PCHW_DEV_MAX + 1) index = 0;
	const int data = g_outComboDev[index];
	if (data == -1) {
		g_outMode = 0;
		savedata.pcMidiOutMode = 0;
		savedata.pcMidiOutName[0] = 0;
		CloseOut();
		return;
	}
	if (data == -2) {
		g_outMode = 1;
		savedata.pcMidiOutMode = 1;
		savedata.pcMidiOutName[0] = 0;
		OpenOutFromMode();
		return;
	}
	g_outMode = 2;
	savedata.pcMidiOutMode = 2;
	MIDIOUTCAPS c = {};
	if (midiOutGetDevCaps((UINT)data, &c, sizeof(c)) == MMSYSERR_NOERROR)
		_tcsncpy_s(savedata.pcMidiOutName, c.szPname, _TRUNCATE);
	else
		savedata.pcMidiOutName[0] = 0;
	OpenOutFromMode();
}

static void AddPortSub(CCustomPopupMenu* parent, int port)
{
	if (!parent || port < 0 || port >= PCHW_PORT_N) return;
	CCustomPopupMenu* sub = parent->AddSubMenu(
		port == 0
			? CString(LL14(L"MIDI In 1", L"MIDI In 1", L"MIDI In 1", L"MIDI In 1", L"MIDI In 1",
				L"MIDI In 1", L"MIDI In 1", L"MIDI In 1", L"MIDI In 1", L"MIDI In 1",
				L"MIDI In 1", L"MIDI In 1", L"MIDI In 1", L"MIDI In 1"))
			: CString(LL14(L"MIDI In 2", L"MIDI In 2", L"MIDI In 2", L"MIDI In 2", L"MIDI In 2",
				L"MIDI In 2", L"MIDI In 2", L"MIDI In 2", L"MIDI In 2", L"MIDI In 2",
				L"MIDI In 2", L"MIDI In 2", L"MIDI In 2", L"MIDI In 2")),
		port == 0
			? CString(LL14(L"入力デバイスを選び、モニタ（パートA）へ送り SMF／MIDI Out します。",
				L"Pick an input device; send to the monitor (part A), SMF, and MIDI Out.",
				L"Choisir l'entree. Envoyer au moniteur (partie A), SMF et MIDI Out.",
				L"Scegli l'ingresso. Invia al monitor (parte A), SMF e MIDI Out.",
				L"Elegir entrada. Enviar al monitor (parte A), SMF y MIDI Out.",
				L"입력 장치를 골라 모니터(파트 A)·SMF·MIDI Out로 보냅니다.",
				L"选择输入设备，送到监视器（声部A）、SMF 与 MIDI Out。",
				L"اختر جهاز الدخل. يُرسل للمراقب (الجزء A) وSMF وMIDI Out.",
				L"Выбрать вход. На монитор (партия A), SMF и MIDI Out.",
				L"Eingabe waehlen. An Monitor (Part A), SMF und MIDI Out.",
				L"Escolher entrada. Enviar ao monitor (parte A), SMF e MIDI Out.",
				L"Kies invoer. Naar monitor (partij A), SMF en MIDI Out.",
				L"Wybierz wejscie. Na monitor (partia A), SMF i MIDI Out.",
				L"Giris aygitini sec. Izleyici (parti A), SMF ve MIDI Out."))
			: CString(LL14(L"入力デバイスを選び、モニタ（パートB）へ送り SMF／MIDI Out します。",
				L"Pick an input device; send to the monitor (part B), SMF, and MIDI Out.",
				L"Choisir l'entree. Envoyer au moniteur (partie B), SMF et MIDI Out.",
				L"Scegli l'ingresso. Invia al monitor (parte B), SMF e MIDI Out.",
				L"Elegir entrada. Enviar al monitor (parte B), SMF y MIDI Out.",
				L"입력 장치를 골라 모니터(파트 B)·SMF·MIDI Out로 보냅니다.",
				L"选择输入设备，送到监视器（声部B）、SMF 与 MIDI Out。",
				L"اختر جهاز الدخل. يُرسل للمراقب (الجزء B) وSMF وMIDI Out.",
				L"Выбрать вход. На монитор (партия B), SMF и MIDI Out.",
				L"Eingabe waehlen. An Monitor (Part B), SMF und MIDI Out.",
				L"Escolher entrada. Enviar ao monitor (parte B), SMF e MIDI Out.",
				L"Kies invoer. Naar monitor (partij B), SMF en MIDI Out.",
				L"Wybierz wejscie. Na monitor (partia B), SMF i MIDI Out.",
				L"Giris aygitini sec. Izleyici (parti B), SMF ve MIDI Out.")));
	if (!sub) return;

	RecycleSysexUi();

	wchar_t inStore[PCHW_DEV_MAX + 1][64];
	LPCTSTR inItems[PCHW_DEV_MAX + 1];
	int inCount = 0;
	g_inComboDev[port][0] = -1;
	inItems[inCount] = LL14(L"(なし)", L"(None)", L"(Aucun)", L"(Nessuno)", L"(Ninguno)",
		L"(없음)", L"(无)", L"(بلا)", L"(Нет)", L"(Keine)", L"(Nenhum)", L"(Geen)", L"(Brak)", L"(Yok)");
	inCount++;
	UINT nIn = midiInGetNumDevs();
	if (nIn > (UINT)PCHW_DEV_MAX) nIn = (UINT)PCHW_DEV_MAX;
	int inSel = 0;
	const int wantIn = (g_inDev[port] >= 0) ? g_inDev[port] : FindMidiInByName(InNameField(port));
	for (UINT i = 0; i < nIn && inCount < PCHW_DEV_MAX + 1; ++i) {
		MIDIINCAPS c = {};
		if (midiInGetDevCaps(i, &c, sizeof(c)) != MMSYSERR_NOERROR) continue;
		g_inComboDev[port][inCount] = (int)i;
		wcsncpy_s(inStore[inCount], c.szPname, _TRUNCATE);
		inItems[inCount] = inStore[inCount];
		if ((int)i == wantIn) inSel = inCount;
		inCount++;
	}
	sub->AddCombo(
		LL14(L"入力デバイス", L"Input device", L"Peripherique d'entree", L"Dispositivo di ingresso",
			L"Dispositivo de entrada", L"입력 장치", L"输入设备", L"جهاز الإدخال", L"Устройство ввода",
			L"Eingabegeraet", L"Dispositivo de entrada", L"Invoerapparaat", L"Urzadzenie wejsciowe", L"Giris aygiti"),
		inItems, inCount, inSel, InComboCb, (void*)(INT_PTR)port,
		LL14(L"この口の MIDI In。選択ですぐモニタへ反映します。",
			L"MIDI In for this port. Applies to the monitor immediately.",
			L"MIDI In de ce port. Applique au moniteur tout de suite.",
			L"MIDI In di questa porta. Subito sul monitor.",
			L"MIDI In de este puerto. Al monitor al instante.",
			L"이 포트의 MIDI In. 선택 즉시 모니터에 반영.",
			L"此口的 MIDI In。选择后立即反映到监视器。",
			L"MIDI In لهذا المنفذ. يطبق على المراقب فوراً.",
			L"MIDI In этого порта. Сразу на монитор.",
			L"MIDI In dieses Ports. Sofort am Monitor.",
			L"MIDI In desta porta. Imediato no monitor.",
			L"MIDI In van deze poort. Meteen op de monitor.",
			L"MIDI In tego portu. Od razu na monitorze.",
			L"Bu portun MIDI In. Izleyiciye hemen yansir."));

	wchar_t outStore[PCHW_DEV_MAX + 2][64];
	LPCTSTR outItems[PCHW_DEV_MAX + 2];
	int outCount = 0;
	g_outComboDev[0] = -1;
	outItems[outCount++] = LL14(L"(なし)", L"(None)", L"(Aucun)", L"(Nessuno)", L"(Ninguno)",
		L"(없음)", L"(无)", L"(بلا)", L"(Нет)", L"(Keine)", L"(Nenhum)", L"(Geen)", L"(Brak)", L"(Yok)");
	g_outComboDev[1] = -2;
	outItems[outCount++] = L"MIDI Mapper";
	UINT nOut = midiOutGetNumDevs();
	if (nOut > (UINT)PCHW_DEV_MAX) nOut = (UINT)PCHW_DEV_MAX;
	int outSel = 0;
	if (savedata.pcMidiOutMode == 1 || g_outMode == 1) outSel = 1;
	for (UINT i = 0; i < nOut && outCount < PCHW_DEV_MAX + 2; ++i) {
		MIDIOUTCAPS c = {};
		if (midiOutGetDevCaps(i, &c, sizeof(c)) != MMSYSERR_NOERROR) continue;
		g_outComboDev[outCount] = (int)i;
		wcsncpy_s(outStore[outCount], c.szPname, _TRUNCATE);
		outItems[outCount] = outStore[outCount];
		if ((savedata.pcMidiOutMode == 2 || g_outMode == 2)
			&& _tcsicmp(c.szPname, savedata.pcMidiOutName) == 0)
			outSel = outCount;
		outCount++;
	}
	sub->AddCombo(
		LL14(L"MIDI 出力", L"MIDI Out", L"Sortie MIDI", L"Uscita MIDI", L"Salida MIDI",
			L"MIDI 출력", L"MIDI 输出", L"خرج MIDI", L"MIDI-выход", L"MIDI-Ausgang",
			L"Saida MIDI", L"MIDI-uitgang", L"Wyjscie MIDI", L"MIDI cikis"),
		outItems, outCount, outSel, OutComboCb, NULL,
		LL14(L"In 1/2 のリアルタイム MIDI スルー先（なし＝出力しない）。",
			L"Realtime MIDI thru target for In 1/2 (None = no output).",
			L"Cible thru MIDI temps reel pour In 1/2 (Aucun = pas de sortie).",
			L"Destinazione thru MIDI in tempo reale per In 1/2 (Nessuno = niente).",
			L"Destino thru MIDI en tiempo real de In 1/2 (Ninguno = sin salida).",
			L"In 1/2 실시간 MIDI 스루 대상(없음=출력 안 함).",
			L"In 1/2 的实时 MIDI 直通目标（无=不输出）。",
			L"هدف تمرير MIDI الفوري لـ In 1/2 (بلا = لا خرج).",
			L"Цель MIDI-thru для In 1/2 (Нет = без выхода).",
			L"Echtzeit-MIDI-Thru fuer In 1/2 (Keine = keine Ausgabe).",
			L"Destino thru MIDI em tempo real de In 1/2 (Nenhum = sem saida).",
			L"Realtime MIDI-thru voor In 1/2 (Geen = geen uitvoer).",
			L"Cel MIDI thru In 1/2 w czasie rzeczywistym (Brak = bez wyjscia).",
			L"In 1/2 gercek zamanli MIDI thru hedefi (Yok = cikis yok)."));

	sub->AddCheck(
		(port == 0) ? IDM_PCHW_IN1_SMF : IDM_PCHW_IN2_SMF,
		g_recOn[port]
			? CString(LL14(L"SMF保存を終了…", L"Stop SMF save…", L"Arreter SMF…", L"Termina SMF…", L"Detener SMF…",
				L"SMF 저장 끝내기…", L"结束 SMF 保存…", L"إيقاف حفظ SMF…", L"Закончить SMF…", L"SMF beenden…",
				L"Parar SMF…", L"SMF stoppen…", L"Zakoncz SMF…", L"SMF kaydini bitir…"))
			: CString(LL14(L"SMF保存を開始", L"Start SMF save", L"Demarrer SMF", L"Avvia SMF", L"Iniciar SMF",
				L"SMF 저장 시작", L"开始 SMF 保存", L"بدء حفظ SMF", L"Начать SMF", L"SMF starten",
				L"Iniciar SMF", L"SMF starten", L"Rozpocznij SMF", L"SMF kaydini baslat")),
		g_recOn[port] != 0,
		LL14(L"この口の MIDI In を SMF (.mid) として録ります。もう一度で保存終了。",
			L"Record this port's MIDI In to SMF (.mid). Click again to finish and save.",
			L"Enregistrer le MIDI In de ce port en SMF. Recliquer pour sauver.",
			L"Registra il MIDI In di questa porta in SMF. Clic di nuovo per salvare.",
			L"Grabar el MIDI In de este puerto a SMF. Clic otra vez para guardar.",
			L"이 포트 MIDI In을 SMF로 녹음. 다시 누르면 저장 종료.",
			L"将此口 MIDI In 录成 SMF。再点一次结束并保存。",
			L"تسجيل MIDI In لهذا المنفذ كـ SMF. انقر مجدداً للحفظ.",
			L"Писать MIDI In этого порта в SMF. Повторный клик — сохранить.",
			L"MIDI In dieses Ports als SMF aufnehmen. Nochmal klicken zum Speichern.",
			L"Gravar o MIDI In desta porta em SMF. Clique de novo para guardar.",
			L"MIDI In van deze poort als SMF opnemen. Opnieuw klikken om op te slaan.",
			L"Nagrywaj MIDI In tego portu do SMF. Kliknij ponownie, aby zapisac.",
			L"Bu portun MIDI In kaydini SMF olarak al. Bitirmek icin tekrar tikla."));
}

void PcHwMidiInRestoreFromSave()
{
	EnsureCs();
	g_inDev[0] = g_inDev[1] = -1;
	g_outMode = savedata.pcMidiOutMode;
	if (g_outMode != 1 && g_outMode != 2) g_outMode = 0;
	const int d0 = FindMidiInByName(savedata.pcMidiIn1Name);
	if (d0 >= 0) OpenInPort(0, d0);
	const int d1 = FindMidiInByName(savedata.pcMidiIn2Name);
	if (d1 >= 0) OpenInPort(1, d1);
	if (g_outMode != 0)
		OpenOutFromMode();
}

void PcHwMidiInShutdown()
{
	EnsureCs();
	EnterCriticalSection(&g_cs);
	g_recOn[0] = g_recOn[1] = 0;
	LeaveCriticalSection(&g_cs);
	CloseInPort(0);
	CloseInPort(1);
	CloseOut();
}

void PcHwMidiInAppendToMenu(CCustomPopupMenu* parent)
{
	if (!parent) return;
	AddPortSub(parent, 0);
	AddPortSub(parent, 1);
}

int PcHwMidiInHandleCmd(UINT cmd, CWnd* owner)
{
	if (cmd == IDM_PCHW_IN1_SMF || cmd == IDM_PCHW_IN2_SMF) {
		const int port = (cmd == IDM_PCHW_IN1_SMF) ? 0 : 1;
		if (g_recOn[port])
			WriteSmf(port, owner);
		else
			StartRec(port);
		return 1;
	}
	return 0;
}

int PcHwMidiInStealShorts(BYTE* ports, DWORD* msgs, int maxN)
{
	FlushOutSysex();
	RecycleSysexUi();
	if (!ports || !msgs || maxN <= 0) return 0;
	int n = 0;
	while (n < maxN) {
		const LONG r = g_tapShortR;
		if (r == g_tapShortW) break;
		const int i = (int)(r & (PCHW_TAP_SHORT_N - 1));
		ports[n] = g_tapShort[i].port;
		msgs[n] = g_tapShort[i].msg;
		MemoryBarrier();
		g_tapShortR = r + 1;
		++n;
	}
	return n;
}

int PcHwMidiInStealSysex(int* port, BYTE* dst, int dstMax)
{
	if (!port || !dst || dstMax <= 0) return 0;
	const LONG r = g_tapSxR;
	if (r == g_tapSxW) return 0;
	const int i = (int)(r & (PCHW_TAP_SX_N - 1));
	int n = (int)g_tapSx[i].len;
	if (n > dstMax) n = dstMax;
	*port = (int)g_tapSx[i].port;
	if (n > 0) memcpy(dst, g_tapSx[i].d, (size_t)n);
	MemoryBarrier();
	g_tapSxR = r + 1;
	return n;
}
