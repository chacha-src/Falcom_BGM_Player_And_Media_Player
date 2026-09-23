#include "sasami_misao.h"
#include "sasami_misao_internal.h"

#include "midisynth.hpp"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <stdio.h>
#include <math.h>
#include <vector>
#include <string>

#ifdef _MSC_VER
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#endif

namespace {

struct PcmSample {
	std::vector<int16_t> pcm;
	uint32_t rate;
	PcmSample() : rate(0) {}
};

struct PcmVoice {
	int ch;
	int slot;
	double pos;
	double step;
	double gainL;
	double gainR;
	PcmVoice() : ch(0), slot(0), pos(0), step(1), gainL(0.35), gainR(0.35) {}
};

static int MisaoReadLineInts(const char** pp, int* dst, int maxn)
{
	const char* p = *pp;
	int n = 0;
	while (n < maxn) {
		while (*p == ' ' || *p == '\t' || *p == '\r') p++;
		if (*p == 0 || *p == '\n' || *p == ';' || *p == '@' || *p == '*') break;
		char* end = NULL;
		long v = strtol(p, &end, 10);
		if (end == p) break;
		dst[n++] = (int)v;
		p = end;
	}
	while (*p && *p != '\n') p++;
	if (*p == '\n') p++;
	*pp = p;
	return n;
}

static int MisaoOpInRange(const int* op)
{
	return op[0] >= 0 && op[0] <= 31 && op[1] >= 0 && op[1] <= 31
		&& op[2] >= 0 && op[2] <= 31 && op[3] >= 0 && op[3] <= 15
		&& op[4] >= 0 && op[4] <= 15 && op[5] >= 0 && op[5] <= 127
		&& op[6] >= 0 && op[6] <= 3 && op[7] >= 0 && op[7] <= 15
		&& op[8] >= 0 && op[8] <= 7 && op[9] >= 0 && op[9] <= 3;
}

static void MisaoFillOps(FMPARAMETER* p, const int ops[4][10])
{
	p->op1.AR = ops[0][0]; p->op1.DR = ops[0][1]; p->op1.SR = ops[0][2]; p->op1.RR = ops[0][3];
	p->op1.SL = ops[0][4]; p->op1.TL = ops[0][5]; p->op1.KS = ops[0][6]; p->op1.ML = ops[0][7];
	p->op1.DT = ops[0][8]; p->op1.AMS = ops[0][9];
	p->op2.AR = ops[1][0]; p->op2.DR = ops[1][1]; p->op2.SR = ops[1][2]; p->op2.RR = ops[1][3];
	p->op2.SL = ops[1][4]; p->op2.TL = ops[1][5]; p->op2.KS = ops[1][6]; p->op2.ML = ops[1][7];
	p->op2.DT = ops[1][8]; p->op2.AMS = ops[1][9];
	p->op3.AR = ops[2][0]; p->op3.DR = ops[2][1]; p->op3.SR = ops[2][2]; p->op3.RR = ops[2][3];
	p->op3.SL = ops[2][4]; p->op3.TL = ops[2][5]; p->op3.KS = ops[2][6]; p->op3.ML = ops[2][7];
	p->op3.DT = ops[2][8]; p->op3.AMS = ops[2][9];
	p->op4.AR = ops[3][0]; p->op4.DR = ops[3][1]; p->op4.SR = ops[3][2]; p->op4.RR = ops[3][3];
	p->op4.SL = ops[3][4]; p->op4.TL = ops[3][5]; p->op4.KS = ops[3][6]; p->op4.ML = ops[3][7];
	p->op4.DT = ops[3][8]; p->op4.AMS = ops[3][9];
}

static void LoadProgramsFile(fm_note_factory& factory, const wchar_t* path)
{
	FILE* fp = NULL;
	_wfopen_s(&fp, path, L"rb");
	if (!fp) return;
	if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return; }
	long sz = ftell(fp);
	if (sz <= 0 || sz > 16 * 1024 * 1024) { fclose(fp); return; }
	if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return; }
	std::string raw((size_t)sz, '\0');
	if (fread(&raw[0], 1, (size_t)sz, fp) != (size_t)sz) { fclose(fp); return; }
	fclose(fp);
	const unsigned char* b = (const unsigned char*)raw.data();
	size_t n = raw.size();
	size_t i0 = 0;
	int utf16 = 0;
	if (n >= 2 && b[0] == 0xFF && b[1] == 0xFE) { utf16 = 1; i0 = 2; }
	else if (n >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) i0 = 3;
	else {
		int pairs = 0, zeros = 0;
		for (size_t i = 1; i < 80 && i < n; i += 2) { pairs++; if (b[i] == 0) zeros++; }
		if (pairs > 8 && zeros * 4 > pairs * 3) utf16 = 1;
	}
	std::string text;
	text.reserve(n);
	if (utf16) {
		for (size_t i = i0; i + 1 < n; i += 2) {
			unsigned c = (unsigned)b[i] | ((unsigned)b[i + 1] << 8);
			text.push_back(c < 128 ? (char)c : ' ');
		}
	} else {
		for (size_t i = i0; i < n; i++) {
			unsigned char c = b[i];
			if (c < 128) text.push_back((char)c);
			else {
				text.push_back(' ');
				if ((c & 0xE0) == 0xC0 && i + 1 < n) i += 1;
				else if ((c & 0xF0) == 0xE0 && i + 2 < n) i += 2;
				else if ((c & 0xF8) == 0xF0 && i + 3 < n) i += 3;
			}
		}
	}
	const char* p = text.c_str();
	while (*p) {
		while (*p == ' ' || *p == '\t' || *p == '\r') p++;
		if (*p == '\n') { p++; continue; }
		if (*p != '@' && *p != '*') {
			while (*p && *p != '\n') p++;
			continue;
		}
		const int drum = (*p == '*');
		p++;
		int prog = 0;
		if (MisaoReadLineInts(&p, &prog, 1) != 1) continue;
		int ops[4][10];
		int ok = 1;
		if (drum) {
			int hdr[6];
			if (MisaoReadLineInts(&p, hdr, 6) != 6) continue;
			if (hdr[0] < 0 || hdr[0] > 7 || hdr[1] < 0 || hdr[1] > 7 || hdr[2] < 0 || hdr[2] > 7) continue;
			if (hdr[3] < 0 || hdr[3] > 127 || hdr[4] < 0 || hdr[4] > 16383 || hdr[5] < 0 || hdr[5] > 127) continue;
			for (int k = 0; k < 4; k++) {
				if (MisaoReadLineInts(&p, ops[k], 10) != 10 || !MisaoOpInRange(ops[k])) { ok = 0; break; }
			}
			if (!ok) continue;
			DRUMPARAMETER d;
			memset(&d, 0, sizeof(d));
			d.ALG = hdr[0]; d.FB = hdr[1]; d.LFO = hdr[2];
			d.key = hdr[3]; d.panpot = hdr[4]; d.assign = hdr[5];
			MisaoFillOps(&d, ops);
			factory.set_drum_program(prog, d);
		} else {
			int hdr[4];
			int hn = MisaoReadLineInts(&p, hdr, 4);
			if (hn < 3) continue;
			if (hdr[0] < 0 || hdr[0] > 7 || hdr[1] < 0 || hdr[1] > 7 || hdr[2] < 0 || hdr[2] > 7) continue;
			int tr = (hn >= 4) ? hdr[3] : 0;
			if (tr < -48) tr = -48;
			if (tr > 48) tr = 48;
			for (int k = 0; k < 4; k++) {
				if (MisaoReadLineInts(&p, ops[k], 10) != 10 || !MisaoOpInRange(ops[k])) { ok = 0; break; }
			}
			if (!ok) continue;
			FMPARAMETER fm;
			memset(&fm, 0, sizeof(fm));
			fm.ALG = hdr[0]; fm.FB = hdr[1]; fm.LFO = hdr[2]; fm.transpose = tr;
			MisaoFillOps(&fm, ops);
			factory.set_program(prog, fm);
		}
	}
}

static void LoadProgramsTxt(fm_note_factory& factory, const wchar_t* dir)
{
	if (!dir || !dir[0]) return;
	wchar_t path[MAX_PATH];
	_snwprintf_s(path, _TRUNCATE, L"%s\\programs.txt", dir);
	LoadProgramsFile(factory, path);
}

static uint16_t Rd16(const uint8_t* p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t Rd32(const uint8_t* p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool LoadWavMono16(const wchar_t* path, PcmSample& out)
{
	FILE* fp = NULL;
	_wfopen_s(&fp, path, L"rb");
	if (!fp) return false;
	if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return false; }
	long sz = ftell(fp);
	if (sz <= 44 || sz > 128 * 1024 * 1024) { fclose(fp); return false; }
	fseek(fp, 0, SEEK_SET);
	std::vector<uint8_t> bytes((size_t)sz);
	if (fread(bytes.data(), 1, bytes.size(), fp) != bytes.size()) { fclose(fp); return false; }
	fclose(fp);
	if (memcmp(bytes.data(), "RIFF", 4) != 0 || memcmp(bytes.data() + 8, "WAVE", 4) != 0) return false;
	uint16_t fmtTag = 0, channels = 0, bits = 0;
	uint32_t rate = 0, dataOff = 0, dataSize = 0;
	for (uint32_t off = 12; off + 8 <= bytes.size(); ) {
		uint32_t ckSize = Rd32(bytes.data() + off + 4);
		uint32_t body = off + 8;
		if (body + ckSize > bytes.size()) break;
		if (memcmp(bytes.data() + off, "fmt ", 4) == 0 && ckSize >= 16) {
			fmtTag = Rd16(bytes.data() + body);
			channels = Rd16(bytes.data() + body + 2);
			rate = Rd32(bytes.data() + body + 4);
			bits = Rd16(bytes.data() + body + 14);
		} else if (memcmp(bytes.data() + off, "data", 4) == 0) {
			dataOff = body;
			dataSize = ckSize;
		}
		off = body + ckSize + (ckSize & 1);
	}
	if (fmtTag != 1 || (channels != 1 && channels != 2) || bits != 16 || rate < 8000 || dataOff == 0 || dataSize < channels * 2)
		return false;
	const uint32_t frames = dataSize / (uint32_t)(channels * 2);
	out.pcm.resize(frames);
	const int16_t* src = (const int16_t*)(bytes.data() + dataOff);
	for (uint32_t i = 0; i < frames; i++) {
		if (channels == 1) out.pcm[i] = src[i];
		else {
			int v = ((int)src[i * 2] + (int)src[i * 2 + 1]) / 2;
			if (v < -32768) v = -32768;
			if (v > 32767) v = 32767;
			out.pcm[i] = (int16_t)v;
		}
	}
	out.rate = rate;
	return !out.pcm.empty();
}

static bool LoadMfMono16(const wchar_t* path, PcmSample& out)
{
	HRESULT hrCo = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	const bool coOk = SUCCEEDED(hrCo);
	if (FAILED(hrCo) && hrCo != RPC_E_CHANGED_MODE) return false;
	if (FAILED(MFStartup(MF_VERSION))) {
		if (coOk) CoUninitialize();
		return false;
	}
	IMFSourceReader* reader = NULL;
	IMFMediaType* type = NULL;
	bool ok = false;
	if (SUCCEEDED(MFCreateSourceReaderFromURL(path, NULL, &reader)) &&
		SUCCEEDED(MFCreateMediaType(&type))) {
		type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
		type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
		type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
		if (SUCCEEDED(reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, type))) {
			IMFMediaType* cur = NULL;
			UINT32 channels = 0, rate = 0;
			if (SUCCEEDED(reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &cur))) {
				cur->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
				cur->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
				cur->Release();
			}
			if (channels >= 1 && channels <= 8 && rate >= 8000) {
				std::vector<int16_t> pcm;
				for (;;) {
					DWORD flags = 0;
					IMFSample* smp = NULL;
					HRESULT hr = reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, NULL, &flags, NULL, &smp);
					if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) break;
					if (!smp) continue;
					IMFMediaBuffer* buf = NULL;
					if (SUCCEEDED(smp->ConvertToContiguousBuffer(&buf))) {
						BYTE* p = NULL;
						DWORD maxLen = 0, curLen = 0;
						if (SUCCEEDED(buf->Lock(&p, &maxLen, &curLen))) {
							const int16_t* src = (const int16_t*)p;
							DWORD samples = curLen / 2;
							DWORD frames = samples / channels;
							for (DWORD i = 0; i < frames; i++) {
								int acc = 0;
								for (UINT32 c = 0; c < channels; c++) acc += src[i * channels + c];
								pcm.push_back((int16_t)(acc / (int)channels));
							}
							buf->Unlock();
						}
						buf->Release();
					}
					smp->Release();
					if (pcm.size() > 48000u * 60u * 10u) break;
				}
				if (!pcm.empty()) {
					out.pcm.swap(pcm);
					out.rate = rate;
					ok = true;
				}
			}
		}
	}
	if (type) type->Release();
	if (reader) reader->Release();
	MFShutdown();
	if (coOk) CoUninitialize();
	return ok;
}

static bool ExtEq(const wchar_t* path, const wchar_t* ext)
{
	const wchar_t* dot = wcsrchr(path, L'.');
	return dot && _wcsicmp(dot, ext) == 0;
}

} // namespace

struct SasamiMisaoSynth::Impl {
	SasamiSong song;
	fm_note_factory factory;
	synthesizer synth;
	MisaoChState ch[SASAMI_MISAO_MAX_CH];
	uint8_t gate[SASAMI_MISAO_MAX_CH];
	uint8_t slotSel[SASAMI_MISAO_MAX_CH];
	uint8_t pan[SASAMI_MISAO_MAX_CH];
	PcmSample sample[128];
	std::vector<PcmVoice> voices;
	wchar_t baseDir[MAX_PATH];
	unsigned T;
	unsigned* sharedT;
	uint32_t rate;
	int ended;
	int chCount;

	Impl() : synth(&factory), sharedT(NULL), rate(44100), ended(0), chCount(0), T(kMisaoDefaultT) {
		memset(ch, 0, sizeof(ch));
		memset(gate, 0, sizeof(gate));
		memset(slotSel, 0, sizeof(slotSel));
		for (int i = 0; i < SASAMI_MISAO_MAX_CH; i++) pan[i] = 63;
		baseDir[0] = 0;
	}

	void ApplyTempo(unsigned t)
	{
		if (t == 0) return;
		T = t;
		if (sharedT) *sharedT = t;
	}

	void EmitMidi(int chIdx, uint32_t msg)
	{
		synth.midi_event(msg | (uint32_t)(chIdx & 0x0F));
	}

	void ResetState()
	{
		ended = 0;
		T = sharedT ? *sharedT : kMisaoDefaultT;
		if (T == 0) T = kMisaoDefaultT;
		synth.reset();
		MisaoInitChState(ch, song);
		memset(gate, 0, sizeof(gate));
		memset(slotSel, 0, sizeof(slotSel));
		for (int i = 0; i < SASAMI_MISAO_MAX_CH; i++) pan[i] = 63;
		voices.clear();
		chCount = MisaoEffectiveChCount(song);
		for (int i = 0; i < chCount; i++) {
			EmitMidi(i, 0xB0 | (7u << 8) | (100u << 16));
			EmitMidi(i, 0xB0 | (10u << 8) | ((uint8_t)MisaoPanCc(63) << 16));
		}
	}

	bool ResolvePath(const wchar_t* rel, wchar_t* out, int outCch)
	{
		if (!rel || !rel[0] || !out || outCch <= 0) return false;
		if ((rel[0] && rel[1] == L':') || rel[0] == L'\\' || rel[0] == L'/') {
			wcsncpy_s(out, outCch, rel, _TRUNCATE);
			return true;
		}
		if (baseDir[0])
			_snwprintf_s(out, outCch, _TRUNCATE, L"%s\\%s", baseDir, rel);
		else
			wcsncpy_s(out, outCch, rel, _TRUNCATE);
		return true;
	}

	void LoadPcmFromBytes(uint8_t slot, const uint8_t* bytes, uint8_t len)
	{
		if (slot >= 128 || !bytes || len == 0) return;
		char mb[256];
		memcpy(mb, bytes, len);
		mb[len] = 0;
		wchar_t rel[260];
		int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, mb, len, rel, 259);
		if (n <= 0)
			n = MultiByteToWideChar(932, 0, mb, len, rel, 259);
		if (n <= 0) return;
		rel[n] = 0;
		wchar_t full[MAX_PATH];
		if (!ResolvePath(rel, full, MAX_PATH)) return;
		PcmSample tmp;
		if (ExtEq(full, L".wav")) {
			if (!LoadWavMono16(full, tmp))
				return;
		} else if (!LoadMfMono16(full, tmp)) {
			return;
		}
		sample[slot].pcm.swap(tmp.pcm);
		sample[slot].rate = tmp.rate;
	}

	bool HavePcmSlot(int chIdx) const
	{
		if (chIdx < 0 || chIdx >= SASAMI_MISAO_MAX_CH) return false;
		uint8_t slot = slotSel[chIdx];
		return slot < 128 && !sample[slot].pcm.empty() && sample[slot].rate >= 8000;
	}

	void TriggerPcm(int chIdx, int note)
	{
		if (!HavePcmSlot(chIdx)) return;
		uint8_t slot = slotSel[chIdx];
		PcmVoice v;
		v.ch = chIdx;
		v.slot = slot;
		v.pos = 0.0;
		v.step = ((double)sample[slot].rate / (double)(rate ? rate : 44100)) * pow(2.0, ((double)note - 60.0) / 12.0);
		if (v.step <= 0.0) v.step = 1.0;
		double p = (double)MisaoPanCc(pan[chIdx]) / 127.0;
		v.gainL = 0.45 * (1.0 - p);
		v.gainR = 0.45 * p;
		voices.push_back(v);
	}

	void TickOnceInternal()
	{
		int any = 0;
		for (int i = 0; i < chCount; i++) {
			if (!ch[i].alive) continue;
			any = 1;
			if (ch[i].wait >= 2) {
				ch[i].wait--;
				continue;
			}
			int guard = 0;
			while (ch[i].alive && ch[i].wait < 2 && guard++ < 4096) {
				uint32_t addr = ch[i].pc;
				if (!SasamiOffOk(song, addr, 1) || addr == 0xF0) {
					ch[i].alive = 0;
					gate[i] = 0;
					break;
				}
				const int cmd = SasamiGet(song, addr);
				const uint8_t b1 = SasamiGet(song, addr + 1);
				const uint8_t b2 = SasamiGet(song, addr + 2);
				const uint16_t w1 = SasamiGet16(song, addr + 1);
				switch (cmd) {
				case 0:
					EmitMidi(i, 0x80 | (ch[i].lastNote << 8));
					ch[i].lastNote = (uint8_t)MisaoNoteKey(b1);
					if (HavePcmSlot(i))
						TriggerPcm(i, ch[i].lastNote);
					else
						EmitMidi(i, 0x90 | (ch[i].lastNote << 8) | (100u << 16));
					gate[i] = 1;
					ch[i].wait = b2;
					ch[i].pc = addr + 3;
					break;
				case 1:
					EmitMidi(i, 0x80 | (ch[i].lastNote << 8));
					gate[i] = 0;
					ch[i].wait = b2;
					ch[i].pc = addr + 3;
					break;
				case 2:
					slotSel[i] = b1;
					EmitMidi(i, 0xC0 | (b1 << 8));
					ch[i].pc = addr + 3;
					break;
				case 3: {
					uint32_t dest = w1;
					if (dest >= 0x1000) dest -= 0x1000;
					if (dest == 0xF0) { ch[i].alive = 0; gate[i] = 0; }
					else {
						if (dest < addr) {
							ch[i].backJumps++;
							if (ch[i].backJumps >= 2) { ch[i].alive = 0; gate[i] = 0; }
							else ch[i].pc = dest;
						} else
							ch[i].pc = dest;
					}
					break;
				}
				case 9:
					if (w1) ApplyTempo(w1);
					ch[i].pc = addr + 3;
					break;
				case 10:
					ch[i].wait = b2;
					ch[i].pc = addr + 3;
					break;
				case 11:
					EmitMidi(i, 0xB0 | (b1 << 8) | (7u << 16));
					ch[i].pc = addr + 3;
					break;
				case 12: {
					ch[i].pitchM = w1;
					const int pb = MisaoCombinedBend(ch[i]);
					EmitMidi(i, 0xE0 | ((pb & 0x7F) << 8) | (((pb >> 7) & 0x7F) << 16));
					ch[i].pc = addr + 3;
					break;
				}
				case 13:
					if (ch[i].loopSp < MisaoChState::MISAO_LOOP_NEST)
						ch[i].loopCnt[ch[i].loopSp++] = b1;
					else
						ch[i].loopCnt[MisaoChState::MISAO_LOOP_NEST - 1] = b1;
					ch[i].pc = addr + 3;
					break;
				case 14: {
					if (ch[i].loopSp == 0) {
						ch[i].pc = addr + 3;
						break;
					}
					uint8_t* cp = &ch[i].loopCnt[ch[i].loopSp - 1];
					uint8_t c = *cp;
					if (c) c--;
					if (c == 0) {
						ch[i].loopSp--;
						ch[i].pc = addr + 3;
					} else {
						*cp = c;
						uint32_t dest = w1;
						if (dest >= 0x1000) dest -= 0x1000;
						ch[i].pc = dest;
					}
					break;
				}
				case 18: {
					ch[i].detune = w1;
					const int pb = MisaoCombinedBend(ch[i]);
					EmitMidi(i, 0xE0 | ((pb & 0x7F) << 8) | (((pb >> 7) & 0x7F) << 16));
					ch[i].pc = addr + 3;
					break;
				}
				case 24:
					ch[i].lastNote = (uint8_t)MisaoNoteKey(b1);
					if (HavePcmSlot(i))
						TriggerPcm(i, ch[i].lastNote);
					else
						EmitMidi(i, 0x90 | (ch[i].lastNote << 8) | (100u << 16));
					ch[i].wait = b2;
					ch[i].pc = addr + 3;
					break;
				case 25:
					pan[i] = b1;
					EmitMidi(i, 0xB0 | ((uint8_t)MisaoPanCc(b1) << 8) | (10u << 16));
					ch[i].pc = addr + 3;
					break;
				case 26:
					if (SasamiOffOk(song, addr, (uint32_t)3 + b2))
						LoadPcmFromBytes(b1, song.data + addr + 3, b2);
					ch[i].pc = SasamiOffOk(song, addr, (uint32_t)3 + b2) ? addr + 3 + b2 : addr + 3;
					break;
				default:
					ch[i].pc = addr + 3;
					break;
				}
				if (ch[i].wait >= 2) break;
				if (cmd == 0 || cmd == 1 || cmd == 10 || cmd == 24) break;
			}
		}
		if (!any) ended = 1;
	}
};

SasamiMisaoSynth::SasamiMisaoSynth() : m(NULL) {}
SasamiMisaoSynth::~SasamiMisaoSynth() { Close(); }

bool SasamiMisaoSynth::Open(const SasamiSong& song, uint32_t sampleRate, const wchar_t* programsTxtDir, unsigned* sharedTempoT, const wchar_t* sampleBaseDir)
{
	Close();
	if (!SasamiMisaoActive(song)) return false;
	m = new Impl();
	m->song = song;
	m->sharedT = sharedTempoT;
	m->rate = sampleRate < 8000 ? 44100 : sampleRate;
	if (sampleBaseDir && sampleBaseDir[0])
		wcsncpy_s(m->baseDir, sampleBaseDir, _TRUNCATE);
	else if (programsTxtDir && programsTxtDir[0])
		wcsncpy_s(m->baseDir, programsTxtDir, _TRUNCATE);
	LoadProgramsTxt(m->factory, programsTxtDir);
	m->ResetState();
	return true;
}

void SasamiMisaoSynth::Close()
{
	delete m;
	m = NULL;
}

void SasamiMisaoSynth::Reset()
{
	if (m) m->ResetState();
}

void SasamiMisaoSynth::SetTempoT(unsigned t)
{
	if (m && t) m->ApplyTempo(t);
}

void SasamiMisaoSynth::TickOnce()
{
	if (m) m->TickOnceInternal();
}

void SasamiMisaoSynth::SynthesizeMix(double* stereoInterleaved, uint32_t frames)
{
	if (!m || !stereoInterleaved || frames == 0) return;
	m->synth.synthesize_mixing(stereoInterleaved, frames, (double)m->rate);
	for (uint32_t f = 0; f < frames; f++) {
		for (size_t vi = 0; vi < m->voices.size(); ) {
			PcmVoice& v = m->voices[vi];
			if (v.slot >= 128 || m->sample[v.slot].pcm.empty() || v.pos >= (double)m->sample[v.slot].pcm.size()) {
				m->voices.erase(m->voices.begin() + vi);
				continue;
			}
			const std::vector<int16_t>& pcm = m->sample[v.slot].pcm;
			uint32_t p0 = (uint32_t)v.pos;
			uint32_t p1 = p0 + 1 < pcm.size() ? p0 + 1 : p0;
			double frac = v.pos - (double)p0;
			double smp = ((double)pcm[p0] * (1.0 - frac) + (double)pcm[p1] * frac) / 32768.0;
			stereoInterleaved[f * 2] += smp * v.gainL;
			stereoInterleaved[f * 2 + 1] += smp * v.gainR;
			v.pos += v.step;
			++vi;
		}
	}
}

uint32_t SasamiMisaoSynth::SampleRate() const
{
	return m ? m->rate : 0;
}

int SasamiMisaoSynth::Ended() const
{
	return m ? m->ended : 1;
}

void SasamiMisaoSynth::FillMonitor(uint8_t* onOut, uint8_t* noteOut, int maxCh, int* outCount) const
{
	if (outCount) *outCount = 0;
	if (!onOut || !noteOut || maxCh <= 0) return;
	memset(onOut, 0, (size_t)maxCh);
	memset(noteOut, 0, (size_t)maxCh);
	if (!m) return;
	int n = m->chCount;
	if (n > maxCh) n = maxCh;
	if (n > SASAMI_MISAO_MAX_CH) n = SASAMI_MISAO_MAX_CH;
	for (int i = 0; i < n; i++) {
		onOut[i] = m->gate[i] ? 1 : 0;
		noteOut[i] = m->ch[i].lastNote;
	}
	if (outCount) *outCount = n;
}
