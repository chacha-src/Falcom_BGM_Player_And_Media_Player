#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "Sf2Vst2.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#pragma warning(push)
#pragma warning(disable : 4100 4244 4245 4456 4457 4701)
#define TSF_IMPLEMENTATION
#define TSF_NO_STDIO
#include "third_party/tinysoundfont/tsf.h"
#pragma warning(pop)

enum {
	SF2_INST_MAGIC = 0x32465301,
	SF2_VST2_UID = 0x54534632, /* 'TSF2' */
	SF2_MAX_VOICES = 256,
	SF2_MAX_EVENTS = 512,
	SF2_MAX_CACHE = 8,
	SF2_EDIT_W = 540,
	SF2_EDIT_H = 400,
	SF2_CH = 16
};

struct ERect {
	short top;
	short left;
	short bottom;
	short right;
};

struct Sf2MidiEv {
	int delta;
	unsigned char msg[4];
	unsigned char* syx;
	int syxN;
};

struct Sf2Inst;

struct Sf2CacheEnt {
	wchar_t path[MAX_PATH];
	tsf* master;
	int users;
};

struct Sf2Inst {
	unsigned magic;
	AEffect effect;
	audioMasterCallback host;
	tsf* font;
	CRITICAL_SECTION cs;
	wchar_t path[MAX_PATH];
	char nameA[64];
	char productA[64];
	int sampleRate;
	int blockSize;
	float* mix;
	int mixFrames;
	int program;
	int lastCh;
	unsigned char drum[SF2_CH];
	unsigned char prog[SF2_CH];
	Sf2MidiEv q[SF2_MAX_EVENTS];
	int qN;
	HWND edWnd;
	HWND edList;
	HWND edInfo;
	ERect edRect;
	unsigned char* chunk;
	int chunkN;
};

static CRITICAL_SECTION g_sf2CacheCs;
static LONG g_sf2CacheInit = 0;
static Sf2CacheEnt g_sf2Cache[SF2_MAX_CACHE];
static ATOM g_sf2EdAtom = 0;

static void Sf2EnsureCacheCs(void)
{
	if (InterlockedCompareExchange(&g_sf2CacheInit, 1, 0) == 0)
		InitializeCriticalSection(&g_sf2CacheCs);
}

int Sf2PathIsSoundFont(const wchar_t* path)
{
	if (!path || !path[0]) return 0;
	const wchar_t* dot = wcsrchr(path, L'.');
	const wchar_t* slash = wcsrchr(path, L'\\');
	const wchar_t* slash2 = wcsrchr(path, L'/');
	if (slash2 && (!slash || slash2 > slash)) slash = slash2;
	if (!dot || (slash && dot < slash)) return 0;
	return _wcsicmp(dot, L".sf2") == 0;
}

int Sf2ProbeHeader(const wchar_t* path)
{
	if (!Sf2PathIsSoundFont(path)) return 0;
	HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE) return 0;
	unsigned char hdr[12] = {};
	DWORD got = 0;
	const int ok = ReadFile(f, hdr, 12, &got, NULL) && got == 12 &&
		memcmp(hdr, "RIFF", 4) == 0 && memcmp(hdr + 8, "sfbk", 4) == 0;
	CloseHandle(f);
	return ok ? 1 : 0;
}

static tsf* Sf2LoadMaster(const wchar_t* path)
{
	HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE) return NULL;
	LARGE_INTEGER sz = {};
	if (!GetFileSizeEx(f, &sz) || sz.QuadPart < 16 || sz.QuadPart > 0x7fffffff) {
		CloseHandle(f);
		return NULL;
	}
	const int nbytes = (int)sz.QuadPart;
	tsf* font = NULL;
	HANDLE map = CreateFileMappingW(f, NULL, PAGE_READONLY, 0, 0, NULL);
	if (map) {
		void* view = MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0);
		if (view) {
			if (memcmp(view, "RIFF", 4) == 0 &&
				nbytes >= 12 && memcmp((char*)view + 8, "sfbk", 4) == 0)
				font = tsf_load_memory(view, nbytes);
			UnmapViewOfFile(view);
		}
		CloseHandle(map);
	}
	if (!font) {
		unsigned char* buf = (unsigned char*)malloc((size_t)nbytes);
		if (buf) {
			DWORD got = 0;
			SetFilePointer(f, 0, NULL, FILE_BEGIN);
			if (ReadFile(f, buf, (DWORD)nbytes, &got, NULL) && (int)got == nbytes &&
				memcmp(buf, "RIFF", 4) == 0 && memcmp(buf + 8, "sfbk", 4) == 0)
				font = tsf_load_memory(buf, nbytes);
			free(buf);
		}
	}
	CloseHandle(f);
	return font;
}

static tsf* Sf2AcquireFont(const wchar_t* path)
{
	Sf2EnsureCacheCs();
	EnterCriticalSection(&g_sf2CacheCs);
	int freeSlot = -1;
	for (int i = 0; i < SF2_MAX_CACHE; ++i) {
		if (!g_sf2Cache[i].master) {
			if (freeSlot < 0) freeSlot = i;
			continue;
		}
		if (_wcsicmp(g_sf2Cache[i].path, path) == 0) {
			tsf* copy = tsf_copy(g_sf2Cache[i].master);
			if (copy) g_sf2Cache[i].users++;
			LeaveCriticalSection(&g_sf2CacheCs);
			return copy;
		}
	}
	if (freeSlot < 0) {
		LeaveCriticalSection(&g_sf2CacheCs);
		tsf* one = Sf2LoadMaster(path);
		return one;
	}
	tsf* master = Sf2LoadMaster(path);
	if (!master) {
		LeaveCriticalSection(&g_sf2CacheCs);
		return NULL;
	}
	wcsncpy_s(g_sf2Cache[freeSlot].path, path, _TRUNCATE);
	g_sf2Cache[freeSlot].master = master;
	g_sf2Cache[freeSlot].users = 1;
	tsf* copy = tsf_copy(master);
	if (!copy) {
		g_sf2Cache[freeSlot].master = NULL;
		g_sf2Cache[freeSlot].path[0] = 0;
		g_sf2Cache[freeSlot].users = 0;
		tsf_close(master);
	}
	LeaveCriticalSection(&g_sf2CacheCs);
	return copy;
}

static void Sf2ReleaseFont(const wchar_t* path, tsf* inst)
{
	if (inst) tsf_close(inst);
	if (!path || !path[0]) return;
	Sf2EnsureCacheCs();
	EnterCriticalSection(&g_sf2CacheCs);
	for (int i = 0; i < SF2_MAX_CACHE; ++i) {
		if (!g_sf2Cache[i].master || _wcsicmp(g_sf2Cache[i].path, path) != 0)
			continue;
		if (--g_sf2Cache[i].users <= 0) {
			tsf_close(g_sf2Cache[i].master);
			g_sf2Cache[i].master = NULL;
			g_sf2Cache[i].path[0] = 0;
			g_sf2Cache[i].users = 0;
		}
		break;
	}
	LeaveCriticalSection(&g_sf2CacheCs);
}

static Sf2Inst* Sf2FromEffect(AEffect* e)
{
	if (!e || !e->object) return NULL;
	Sf2Inst* s = (Sf2Inst*)e->object;
	return (s->magic == SF2_INST_MAGIC) ? s : NULL;
}

int Sf2Vst2IsInstance(const AEffect* e)
{
	if (!e || e->uniqueID != SF2_VST2_UID || !e->object) return 0;
	const Sf2Inst* s = (const Sf2Inst*)e->object;
	return s->magic == SF2_INST_MAGIC ? 1 : 0;
}

static void Sf2ApplyProgram(Sf2Inst* s, int ch, int pc)
{
	if (!s->font) return;
	ch &= 15;
	pc &= 127;
	s->prog[ch] = (unsigned char)pc;
	tsf_channel_set_presetnumber(s->font, ch, pc, s->drum[ch] ? 1 : 0);
}

static void Sf2GmReset(Sf2Inst* s)
{
	if (!s->font) return;
	tsf_reset(s->font);
	tsf_note_off_all(s->font);
	for (int ch = 0; ch < SF2_CH; ++ch) {
		s->drum[ch] = (ch == 9) ? 1 : 0;
		s->prog[ch] = 0;
		tsf_channel_set_bank(s->font, ch, 0);
		tsf_channel_midi_control(s->font, ch, 121, 0);
		tsf_channel_midi_control(s->font, ch, 7, 100);
		tsf_channel_midi_control(s->font, ch, 11, 127);
		tsf_channel_midi_control(s->font, ch, 10, 64);
		Sf2ApplyProgram(s, ch, 0);
	}
	s->program = 0;
}

static int Sf2SysexIs(const unsigned char* d, int n, const unsigned char* pat, int patN)
{
	if (!d || !pat || n < patN) return 0;
	return memcmp(d, pat, (size_t)patN) == 0;
}

static void Sf2ApplySysex(Sf2Inst* s, const unsigned char* d, int n)
{
	if (!d || n < 6 || d[0] != 0xf0) return;
	/* GM / GM2 System On */
	if (n >= 6 && d[1] == 0x7e && d[3] == 0x09 && (d[4] == 0x01 || d[4] == 0x03)) {
		Sf2GmReset(s);
		return;
	}
	/* GS DT1 */
	if (n >= 11 && d[1] == 0x41 && d[3] == 0x42 && d[4] == 0x12) {
		const unsigned char aa = d[5], bb = d[6], cc = d[7];
		if (aa == 0x40 && bb == 0x00 && cc == 0x7f) {
			Sf2GmReset(s);
			return;
		}
		if (aa == 0x00 && bb == 0x00 && cc == 0x7f) {
			Sf2GmReset(s);
			return;
		}
		/* 40 1p 15 vv : USE FOR RHYTHM PART */
		if (aa == 0x40 && (bb & 0xf0) == 0x10 && cc == 0x15 && n >= 9) {
			const int ch = bb & 0x0f;
			s->drum[ch] = d[8] ? 1 : 0;
			Sf2ApplyProgram(s, ch, s->prog[ch]);
			return;
		}
		return;
	}
	/* XG */
	if (n >= 8 && d[1] == 0x43 && d[3] == 0x4c) {
		if (d[4] == 0x00 && d[5] == 0x00 && d[6] == 0x7e) {
			Sf2GmReset(s);
			return;
		}
		/* 08 pp 07 vv : part mode (00 normal, 01/02 drum) */
		if (d[4] == 0x08 && d[6] == 0x07 && n >= 9) {
			const int ch = d[5] & 0x0f;
			s->drum[ch] = d[7] ? 1 : 0;
			Sf2ApplyProgram(s, ch, s->prog[ch]);
		}
	}
}

static void Sf2ApplyShort(Sf2Inst* s, const unsigned char* m)
{
	if (!s->font || !m) return;
	const int st = m[0];
	if (st == 0xf0) return;
	const int cmd = st & 0xf0;
	const int ch = st & 0x0f;
	s->lastCh = ch;
	switch (cmd) {
	case 0x80:
		tsf_channel_note_off(s->font, ch, m[1] & 127);
		break;
	case 0x90:
		if ((m[2] & 127) == 0)
			tsf_channel_note_off(s->font, ch, m[1] & 127);
		else
			tsf_channel_note_on(s->font, ch, m[1] & 127, (m[2] & 127) / 127.0f);
		break;
	case 0xb0: {
		const int cc = m[1] & 127;
		const int vv = m[2] & 127;
		if (cc == 0) {
			if (vv == 120 || vv == 127) s->drum[ch] = 1;
			else if (ch != 9) s->drum[ch] = 0;
		}
		tsf_channel_midi_control(s->font, ch, cc, vv);
		if (cc == 0 || cc == 32)
			Sf2ApplyProgram(s, ch, s->prog[ch]);
		break;
	}
	case 0xc0:
		Sf2ApplyProgram(s, ch, m[1] & 127);
		s->program = tsf_channel_get_preset_index(s->font, ch);
		break;
	case 0xe0:
		tsf_channel_set_pitchwheel(s->font, ch, (m[1] & 127) | ((m[2] & 127) << 7));
		break;
	default:
		break;
	}
}

static void Sf2ApplyEvent(Sf2Inst* s, const Sf2MidiEv* e)
{
	if (!e) return;
	if (e->syx && e->syxN > 0)
		Sf2ApplySysex(s, e->syx, e->syxN);
	else
		Sf2ApplyShort(s, e->msg);
}

static void Sf2ClearQueue(Sf2Inst* s)
{
	for (int i = 0; i < s->qN; ++i) {
		if (s->q[i].syx) free(s->q[i].syx);
		s->q[i].syx = NULL;
	}
	s->qN = 0;
}

static int Sf2CmpEv(const void* a, const void* b)
{
	const Sf2MidiEv* x = (const Sf2MidiEv*)a;
	const Sf2MidiEv* y = (const Sf2MidiEv*)b;
	return x->delta - y->delta;
}

static void Sf2EnsureMix(Sf2Inst* s, int frames)
{
	if (frames <= s->mixFrames && s->mix) return;
	float* p = (float*)realloc(s->mix, (size_t)frames * 2u * sizeof(float));
	if (!p) return;
	s->mix = p;
	s->mixFrames = frames;
}

static void Sf2RenderSpan(Sf2Inst* s, float* l, float* r, int frames)
{
	if (frames <= 0) return;
	if (!s->font || !l) {
		if (l) memset(l, 0, (size_t)frames * sizeof(float));
		if (r && r != l) memset(r, 0, (size_t)frames * sizeof(float));
		return;
	}
	Sf2EnsureMix(s, frames);
	if (!s->mix) {
		memset(l, 0, (size_t)frames * sizeof(float));
		if (r && r != l) memset(r, 0, (size_t)frames * sizeof(float));
		return;
	}
	tsf_render_float(s->font, s->mix, frames, 0);
	memcpy(l, s->mix, (size_t)frames * sizeof(float));
	if (r && r != l)
		memcpy(r, s->mix + frames, (size_t)frames * sizeof(float));
}

static void VSTCALLBACK Sf2ProcessReplacing(AEffect* e, float** inputs, float** outputs, VstInt32 frames)
{
	(void)inputs;
	Sf2Inst* s = Sf2FromEffect(e);
	if (!s || !outputs || frames <= 0) return;
	float* l = outputs[0];
	float* r = (e->numOutputs > 1 && outputs[1]) ? outputs[1] : l;
	EnterCriticalSection(&s->cs);
	if (s->qN > 1)
		qsort(s->q, (size_t)s->qN, sizeof(s->q[0]), Sf2CmpEv);
	int pos = 0;
	for (int i = 0; i < s->qN; ++i) {
		int d = s->q[i].delta;
		if (d < pos) d = pos;
		if (d > frames) d = frames;
		if (d > pos) {
			Sf2RenderSpan(s, l + pos, r + pos, d - pos);
			pos = d;
		}
		Sf2ApplyEvent(s, &s->q[i]);
	}
	Sf2ClearQueue(s);
	if (pos < frames)
		Sf2RenderSpan(s, l + pos, r + pos, frames - pos);
	LeaveCriticalSection(&s->cs);
}

static void VSTCALLBACK Sf2Process(AEffect* e, float** inputs, float** outputs, VstInt32 frames)
{
	Sf2ProcessReplacing(e, inputs, outputs, frames);
}

static void VSTCALLBACK Sf2SetParameter(AEffect*, VstInt32, float) {}
static float VSTCALLBACK Sf2GetParameter(AEffect*, VstInt32) { return 0.f; }

static void Sf2CopyCStr(char* dst, int dstN, const char* src)
{
	if (!dst || dstN <= 0) return;
	if (!src) src = "";
	strncpy_s(dst, (size_t)dstN, src, _TRUNCATE);
}

static void Sf2FillPresetName(Sf2Inst* s, int index, char* dst, int dstN)
{
	if (!dst || dstN <= 0) return;
	dst[0] = 0;
	if (!s->font) return;
	const int n = tsf_get_presetcount(s->font);
	if (index < 0 || index >= n) return;
	const char* nm = tsf_get_presetname(s->font, index);
	Sf2CopyCStr(dst, dstN, nm ? nm : "");
}

static void Sf2QueueShort(Sf2Inst* s, int delta, const char midiData[4])
{
	if (s->qN >= SF2_MAX_EVENTS) {
		Sf2MidiEv tmp = {};
		memcpy(tmp.msg, midiData, 4);
		Sf2ApplyEvent(s, &tmp);
		return;
	}
	Sf2MidiEv& ev = s->q[s->qN++];
	memset(&ev, 0, sizeof(ev));
	ev.delta = delta;
	memcpy(ev.msg, midiData, 4);
}

static void Sf2QueueSysex(Sf2Inst* s, int delta, const char* dump, int bytes)
{
	if (!dump || bytes <= 0) return;
	unsigned char* copy = (unsigned char*)malloc((size_t)bytes);
	if (!copy) return;
	memcpy(copy, dump, (size_t)bytes);
	if (s->qN >= SF2_MAX_EVENTS) {
		Sf2MidiEv tmp = {};
		tmp.syx = copy;
		tmp.syxN = bytes;
		Sf2ApplyEvent(s, &tmp);
		free(copy);
		return;
	}
	Sf2MidiEv& ev = s->q[s->qN++];
	memset(&ev, 0, sizeof(ev));
	ev.delta = delta;
	ev.syx = copy;
	ev.syxN = bytes;
}

static void Sf2UpdateEditorInfo(Sf2Inst* s)
{
	if (!s->edInfo || !IsWindow(s->edInfo)) return;
	wchar_t buf[1024];
	wchar_t path[MAX_PATH];
	wcsncpy_s(path, s->path, _TRUNCATE);
	int voices = 0;
	unsigned char drum[SF2_CH], prog[SF2_CH];
	int banks[SF2_CH];
	EnterCriticalSection(&s->cs);
	voices = s->font ? tsf_active_voice_count(s->font) : 0;
	memcpy(drum, s->drum, sizeof(drum));
	memcpy(prog, s->prog, sizeof(prog));
	for (int ch = 0; ch < SF2_CH; ++ch)
		banks[ch] = s->font ? tsf_channel_get_preset_bank(s->font, ch) : 0;
	LeaveCriticalSection(&s->cs);
	wchar_t lines[16][40];
	for (int ch = 0; ch < SF2_CH; ++ch)
		swprintf_s(lines[ch], L"%02d %s b%03d p%03d", ch + 1,
			drum[ch] ? L"DR" : L"ML", banks[ch] & 0x7fff, prog[ch]);
	swprintf_s(buf,
		L"%s\r\nSR %d  block %d  voices %d / %d\r\n"
		L"%s  %s  %s  %s\r\n%s  %s  %s  %s\r\n"
		L"%s  %s  %s  %s\r\n%s  %s  %s  %s",
		path, s->sampleRate, s->blockSize, voices, SF2_MAX_VOICES,
		lines[0], lines[1], lines[2], lines[3],
		lines[4], lines[5], lines[6], lines[7],
		lines[8], lines[9], lines[10], lines[11],
		lines[12], lines[13], lines[14], lines[15]);
	SetWindowTextW(s->edInfo, buf);
}

static LRESULT CALLBACK Sf2EdWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
	Sf2Inst* s = (Sf2Inst*)GetWindowLongPtrW(w, GWLP_USERDATA);
	switch (msg) {
	case WM_CREATE: {
		CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
		s = (Sf2Inst*)cs->lpCreateParams;
		SetWindowLongPtrW(w, GWLP_USERDATA, (LONG_PTR)s);
		HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
		s->edList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
			WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
			8, 8, 260, SF2_EDIT_H - 16, w, (HMENU)1, NULL, NULL);
		s->edInfo = CreateWindowExW(0, L"STATIC", L"",
			WS_CHILD | WS_VISIBLE | SS_LEFT,
			276, 8, SF2_EDIT_W - 284, SF2_EDIT_H - 16, w, (HMENU)2, NULL, NULL);
		SendMessageW(s->edList, WM_SETFONT, (WPARAM)font, TRUE);
		SendMessageW(s->edInfo, WM_SETFONT, (WPARAM)font, TRUE);
		EnterCriticalSection(&s->cs);
		const int n = s->font ? tsf_get_presetcount(s->font) : 0;
		for (int i = 0; i < n; ++i) {
			const char* nm = s->font ? tsf_get_presetname(s->font, i) : NULL;
			char line[160];
			sprintf_s(line, "%03d  %s", i, nm ? nm : "");
			wchar_t wline[160];
			MultiByteToWideChar(CP_ACP, 0, line, -1, wline, 160);
			SendMessageW(s->edList, LB_ADDSTRING, 0, (LPARAM)wline);
		}
		if (s->program >= 0)
			SendMessageW(s->edList, LB_SETCURSEL, (WPARAM)s->program, 0);
		LeaveCriticalSection(&s->cs);
		SetTimer(w, 1, 200, NULL);
		Sf2UpdateEditorInfo(s);
		return 0;
	}
	case WM_COMMAND:
		if (LOWORD(wp) == 1 && HIWORD(wp) == LBN_SELCHANGE && s) {
			const int idx = (int)SendMessageW(s->edList, LB_GETCURSEL, 0, 0);
			if (idx >= 0) {
				EnterCriticalSection(&s->cs);
				s->program = idx;
				if (s->font) {
					const int ch = s->lastCh & 15;
					tsf_channel_set_presetindex(s->font, ch, idx);
					s->prog[ch] = (unsigned char)tsf_channel_get_preset_number(s->font, ch);
				}
				LeaveCriticalSection(&s->cs);
			}
		}
		return 0;
	case WM_TIMER:
		if (s) Sf2UpdateEditorInfo(s);
		return 0;
	case WM_DESTROY:
		if (s) {
			KillTimer(w, 1);
			s->edWnd = NULL;
			s->edList = NULL;
			s->edInfo = NULL;
		}
		return 0;
	default:
		break;
	}
	return DefWindowProcW(w, msg, wp, lp);
}

static int Sf2RegisterEditor(void)
{
	if (g_sf2EdAtom) return 1;
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = Sf2EdWndProc;
	wc.hInstance = GetModuleHandleW(NULL);
	wc.lpszClassName = L"RairaSf2VstEditor";
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	g_sf2EdAtom = RegisterClassExW(&wc);
	if (!g_sf2EdAtom && GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
		return 1;
	return g_sf2EdAtom ? 1 : 0;
}

static int Sf2ProcessEvents(Sf2Inst* s, VstEvents* ev)
{
	if (!ev) return 0;
	EnterCriticalSection(&s->cs);
	for (int i = 0; i < ev->numEvents; ++i) {
		VstEvent* e = ev->events[i];
		if (!e) continue;
		if (e->type == kVstMidiType) {
			VstMidiEvent* m = (VstMidiEvent*)e;
			Sf2QueueShort(s, m->deltaFrames, m->midiData);
		} else if (e->type == kVstSysExType) {
			VstMidiSysexEvent* sx = (VstMidiSysexEvent*)e;
			Sf2QueueSysex(s, sx->deltaFrames, sx->sysexDump, sx->dumpBytes);
		}
	}
	LeaveCriticalSection(&s->cs);
	return 1;
}

struct Sf2Chunk {
	char magic[8];
	unsigned ver;
	unsigned pathBytes;
	unsigned short chBank[16];
	unsigned char chProg[16];
	unsigned char chDrum[16];
};

static VstIntPtr Sf2GetChunk(Sf2Inst* s, void** ptr, int isPreset)
{
	(void)isPreset;
	if (!ptr) return 0;
	char pathUtf8[MAX_PATH * 3];
	pathUtf8[0] = 0;
	WideCharToMultiByte(CP_UTF8, 0, s->path, -1, pathUtf8, (int)sizeof(pathUtf8), NULL, NULL);
	const int pathN = (int)strlen(pathUtf8);
	const int total = (int)sizeof(Sf2Chunk) + pathN;
	unsigned char* blob = (unsigned char*)realloc(s->chunk, (size_t)total);
	if (!blob) return 0;
	s->chunk = blob;
	s->chunkN = total;
	Sf2Chunk ch = {};
	memcpy(ch.magic, "RairaSF2", 8);
	ch.ver = 1;
	ch.pathBytes = (unsigned)pathN;
	EnterCriticalSection(&s->cs);
	for (int i = 0; i < 16; ++i) {
		ch.chBank[i] = s->font ? (unsigned short)tsf_channel_get_preset_bank(s->font, i) : 0;
		ch.chProg[i] = s->prog[i];
		ch.chDrum[i] = s->drum[i];
	}
	LeaveCriticalSection(&s->cs);
	memcpy(blob, &ch, sizeof(ch));
	if (pathN) memcpy(blob + sizeof(ch), pathUtf8, (size_t)pathN);
	*ptr = blob;
	return total;
}

static VstIntPtr Sf2SetChunk(Sf2Inst* s, void* ptr, VstIntPtr size, int isPreset)
{
	(void)isPreset;
	if (!ptr || size < (VstIntPtr)sizeof(Sf2Chunk)) return 0;
	const Sf2Chunk* ch = (const Sf2Chunk*)ptr;
	if (memcmp(ch->magic, "RairaSF2", 8) != 0 || ch->ver != 1) return 0;
	EnterCriticalSection(&s->cs);
	for (int i = 0; i < 16; ++i) {
		s->drum[i] = ch->chDrum[i] ? 1 : 0;
		if (s->font) tsf_channel_set_bank(s->font, i, ch->chBank[i]);
		Sf2ApplyProgram(s, i, ch->chProg[i]);
	}
	LeaveCriticalSection(&s->cs);
	return 1;
}

static VstIntPtr VSTCALLBACK Sf2Dispatcher(AEffect* e, VstInt32 opcode, VstInt32 index,
	VstIntPtr value, void* ptr, float opt)
{
	Sf2Inst* s = Sf2FromEffect(e);
	if (!s) return 0;
	switch (opcode) {
	case effOpen:
		EnterCriticalSection(&s->cs);
		Sf2GmReset(s);
		LeaveCriticalSection(&s->cs);
		return 1;
	case effClose:
		if (s->edWnd && IsWindow(s->edWnd))
			DestroyWindow(s->edWnd);
		EnterCriticalSection(&s->cs);
		Sf2ClearQueue(s);
		free(s->mix);
		s->mix = NULL;
		free(s->chunk);
		s->chunk = NULL;
		s->chunkN = 0;
		LeaveCriticalSection(&s->cs);
		Sf2ReleaseFont(s->path, s->font);
		s->font = NULL;
		DeleteCriticalSection(&s->cs);
		s->magic = 0;
		free(s);
		return 1;
	case effSetProgram:
		EnterCriticalSection(&s->cs);
		if (s->font && value >= 0 && value < tsf_get_presetcount(s->font)) {
			s->program = (int)value;
			tsf_channel_set_presetindex(s->font, s->lastCh & 15, (int)value);
			s->prog[s->lastCh & 15] =
				(unsigned char)tsf_channel_get_preset_number(s->font, s->lastCh & 15);
		}
		LeaveCriticalSection(&s->cs);
		return 1;
	case effGetProgram:
		return s->program;
	case effGetProgramName:
	case effGetProgramNameIndexed: {
		const int pi = (opcode == effGetProgramName) ? s->program : index;
		char tmp[64] = {};
		EnterCriticalSection(&s->cs);
		Sf2FillPresetName(s, pi, tmp, 64);
		LeaveCriticalSection(&s->cs);
		if (ptr) Sf2CopyCStr((char*)ptr, 24, tmp);
		return tmp[0] ? 1 : 0;
	}
	case effSetSampleRate:
		s->sampleRate = (int)opt;
		if (s->sampleRate < 8000) s->sampleRate = 8000;
		EnterCriticalSection(&s->cs);
		if (s->font)
			tsf_set_output(s->font, TSF_STEREO_UNWEAVED, s->sampleRate, 0.f);
		LeaveCriticalSection(&s->cs);
		return 1;
	case effSetBlockSize:
		s->blockSize = (int)value;
		if (s->blockSize < 16) s->blockSize = 16;
		EnterCriticalSection(&s->cs);
		Sf2EnsureMix(s, s->blockSize);
		LeaveCriticalSection(&s->cs);
		return 1;
	case effMainsChanged:
		if (value == 0) {
			EnterCriticalSection(&s->cs);
			if (s->font) tsf_note_off_all(s->font);
			LeaveCriticalSection(&s->cs);
		}
		return 1;
	case effEditGetRect:
		if (ptr) {
			s->edRect.top = 0;
			s->edRect.left = 0;
			s->edRect.bottom = (short)SF2_EDIT_H;
			s->edRect.right = (short)SF2_EDIT_W;
			*(ERect**)ptr = &s->edRect;
		}
		return 1;
	case effEditOpen: {
		HWND parent = (HWND)ptr;
		if (!parent) return 0;
		if (s->edWnd && IsWindow(s->edWnd)) {
			SetParent(s->edWnd, parent);
			ShowWindow(s->edWnd, SW_SHOW);
			return 1;
		}
		if (!Sf2RegisterEditor()) return 0;
		s->edWnd = CreateWindowExW(0, L"RairaSf2VstEditor", L"SoundFont",
			WS_CHILD | WS_VISIBLE, 0, 0, SF2_EDIT_W, SF2_EDIT_H,
			parent, NULL, GetModuleHandleW(NULL), s);
		return s->edWnd ? 1 : 0;
	}
	case effEditClose:
		if (s->edWnd && IsWindow(s->edWnd))
			DestroyWindow(s->edWnd);
		s->edWnd = NULL;
		return 1;
	case effEditIdle:
		if (s->edWnd && IsWindow(s->edWnd))
			Sf2UpdateEditorInfo(s);
		return 1;
	case effGetChunk:
		return Sf2GetChunk(s, (void**)ptr, index);
	case effSetChunk:
		return Sf2SetChunk(s, ptr, value, index);
	case effProcessEvents:
		return Sf2ProcessEvents(s, (VstEvents*)ptr);
	case effGetPlugCategory:
		return kPlugCategSynth;
	case effGetEffectName:
		if (ptr) Sf2CopyCStr((char*)ptr, 32, s->nameA);
		return 1;
	case effGetVendorString:
		if (ptr) Sf2CopyCStr((char*)ptr, 64, "oggYSED / TinySoundFont");
		return 1;
	case effGetProductString:
		if (ptr) Sf2CopyCStr((char*)ptr, 64, s->productA);
		return 1;
	case effGetVendorVersion:
		return 0x00010000;
	case effCanDo:
		if (!ptr) return 0;
		if (!strcmp((char*)ptr, "receiveVstEvents") ||
			!strcmp((char*)ptr, "receiveVstMidiEvent") ||
			!strcmp((char*)ptr, "receiveVstSysexEvent") ||
			!strcmp((char*)ptr, "midiProgramNames"))
			return 1;
		return 0;
	case effGetVstVersion:
		return 2400;
	case effGetNumMidiInputChannels:
		return 16;
	case effGetNumMidiOutputChannels:
		return 0;
	case effConnectInput:
	case effConnectOutput:
	case effStartProcess:
	case effStopProcess:
		return 1;
	default:
		(void)opt;
		return 0;
	}
}

AEffect* Sf2Vst2Open(const wchar_t* path, audioMasterCallback host)
{
	if (!Sf2PathIsSoundFont(path) || !Sf2ProbeHeader(path))
		return NULL;
	tsf* font = Sf2AcquireFont(path);
	if (!font) return NULL;
	if (tsf_get_presetcount(font) <= 0) {
		Sf2ReleaseFont(path, font);
		return NULL;
	}
	if (!tsf_set_max_voices(font, SF2_MAX_VOICES)) {
		Sf2ReleaseFont(path, font);
		return NULL;
	}
	Sf2Inst* s = (Sf2Inst*)calloc(1, sizeof(Sf2Inst));
	if (!s) {
		Sf2ReleaseFont(path, font);
		return NULL;
	}
	s->magic = SF2_INST_MAGIC;
	s->host = host;
	s->font = font;
	s->sampleRate = 44100;
	s->blockSize = 512;
	s->lastCh = 0;
	InitializeCriticalSection(&s->cs);
	wcsncpy_s(s->path, path, _TRUNCATE);
	const wchar_t* leaf = wcsrchr(path, L'\\');
	leaf = leaf ? leaf + 1 : path;
	char leafA[64] = {};
	WideCharToMultiByte(CP_ACP, 0, leaf, -1, leafA, 64, NULL, NULL);
	char* dot = strrchr(leafA, '.');
	if (dot) *dot = 0;
	Sf2CopyCStr(s->nameA, 64, leafA[0] ? leafA : "SoundFont");
	Sf2CopyCStr(s->productA, 64, s->nameA);
	tsf_set_output(font, TSF_STEREO_UNWEAVED, s->sampleRate, 0.f);
	Sf2GmReset(s);

	AEffect* e = &s->effect;
	memset(e, 0, sizeof(*e));
	e->magic = kEffectMagic;
	e->dispatcher = Sf2Dispatcher;
	e->process = Sf2Process;
	e->setParameter = Sf2SetParameter;
	e->getParameter = Sf2GetParameter;
	e->numPrograms = tsf_get_presetcount(font);
	if (e->numPrograms < 1) e->numPrograms = 1;
	e->numParams = 0;
	e->numInputs = 0;
	e->numOutputs = 2;
	e->flags = effFlagsHasEditor | effFlagsCanReplacing | effFlagsIsSynth |
		effFlagsNoSoundInStop | effFlagsProgramChunks;
	e->initialDelay = 0;
	e->object = s;
	e->user = s;
	e->uniqueID = SF2_VST2_UID;
	e->version = 0x00010000;
	e->processReplacing = Sf2ProcessReplacing;
	if (host)
		host(e, audioMasterWantMidi, 0, 1, NULL, 0.f);
	return e;
}
