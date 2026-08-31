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

static void LoadProgramsTxt(fm_note_factory& factory, const wchar_t* dir)
{
	if (!dir || !dir[0]) return;
	wchar_t path[MAX_PATH];
	_snwprintf_s(path, _TRUNCATE, L"%s\\programs.txt", dir);
	FILE* fp = NULL;
	_wfopen_s(&fp, path, L"rt");
	if (!fp) return;
	while (!feof(fp)) {
		int c = getc(fp);
		if (c == '@') {
			int prog = 0;
			FMPARAMETER p;
			if (fscanf_s(fp, "%d%d%d%d", &prog, &p.ALG, &p.FB, &p.LFO) == 4
				&& fscanf_s(fp, "%d%d%d%d%d%d%d%d%d%d", &p.op1.AR, &p.op1.DR, &p.op1.SR, &p.op1.RR, &p.op1.SL, &p.op1.TL, &p.op1.KS, &p.op1.ML, &p.op1.DT, &p.op1.AMS) == 10
				&& fscanf_s(fp, "%d%d%d%d%d%d%d%d%d%d", &p.op2.AR, &p.op2.DR, &p.op2.SR, &p.op2.RR, &p.op2.SL, &p.op2.TL, &p.op2.KS, &p.op2.ML, &p.op2.DT, &p.op2.AMS) == 10
				&& fscanf_s(fp, "%d%d%d%d%d%d%d%d%d%d", &p.op3.AR, &p.op3.DR, &p.op3.SR, &p.op3.RR, &p.op3.SL, &p.op3.TL, &p.op3.KS, &p.op3.ML, &p.op3.DT, &p.op3.AMS) == 10
				&& fscanf_s(fp, "%d%d%d%d%d%d%d%d%d%d", &p.op4.AR, &p.op4.DR, &p.op4.SR, &p.op4.RR, &p.op4.SL, &p.op4.TL, &p.op4.KS, &p.op4.ML, &p.op4.DT, &p.op4.AMS) == 10) {
				factory.set_program(prog, p);
			}
		} else if (c == '*') {
			int prog = 0;
			DRUMPARAMETER p;
			if (fscanf_s(fp, "%d%d%d%d%d%d%d", &prog, &p.ALG, &p.FB, &p.LFO, &p.key, &p.panpot, &p.assign) == 7
				&& fscanf_s(fp, "%d%d%d%d%d%d%d%d%d%d", &p.op1.AR, &p.op1.DR, &p.op1.SR, &p.op1.RR, &p.op1.SL, &p.op1.TL, &p.op1.KS, &p.op1.ML, &p.op1.DT, &p.op1.AMS) == 10
				&& fscanf_s(fp, "%d%d%d%d%d%d%d%d%d%d", &p.op2.AR, &p.op2.DR, &p.op2.SR, &p.op2.RR, &p.op2.SL, &p.op2.TL, &p.op2.KS, &p.op2.ML, &p.op2.DT, &p.op2.AMS) == 10
				&& fscanf_s(fp, "%d%d%d%d%d%d%d%d%d%d", &p.op3.AR, &p.op3.DR, &p.op3.SR, &p.op3.RR, &p.op3.SL, &p.op3.TL, &p.op3.KS, &p.op3.ML, &p.op3.DT, &p.op3.AMS) == 10
				&& fscanf_s(fp, "%d%d%d%d%d%d%d%d%d%d", &p.op4.AR, &p.op4.DR, &p.op4.SR, &p.op4.RR, &p.op4.SL, &p.op4.TL, &p.op4.KS, &p.op4.ML, &p.op4.DT, &p.op4.AMS) == 10) {
				factory.set_drum_program(prog, p);
			}
		}
	}
	fclose(fp);
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
