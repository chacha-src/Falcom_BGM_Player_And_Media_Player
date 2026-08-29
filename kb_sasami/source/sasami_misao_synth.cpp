#include "sasami_misao.h"
#include "sasami_misao_internal.h"

#include "midisynth.hpp"

#include <windows.h>
#include <stdio.h>

namespace {

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

} // namespace

struct SasamiMisaoSynth::Impl {
	SasamiSong song;
	fm_note_factory factory;
	synthesizer synth;
	MisaoChState ch[SASAMI_MISAO_MAX_CH];
	uint8_t gate[SASAMI_MISAO_MAX_CH];
	unsigned T;
	unsigned* sharedT;
	uint32_t rate;
	int ended;
	int chCount;

	Impl() : synth(&factory), sharedT(NULL), rate(44100), ended(0), chCount(0), T(kMisaoDefaultT) {
		memset(ch, 0, sizeof(ch));
		memset(gate, 0, sizeof(gate));
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
		chCount = MisaoEffectiveChCount(song);
		for (int i = 0; i < chCount; i++) {
			EmitMidi(i, 0xB0 | (7u << 8) | (100u << 16));
			EmitMidi(i, 0xB0 | (10u << 8) | ((uint8_t)MisaoPanCc(63) << 16));
		}
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
					ch[i].loopCnt = b1;
					ch[i].pc = addr + 3;
					break;
				case 14: {
					uint8_t c = ch[i].loopCnt;
					if (c) c--;
					if (c == 0) ch[i].pc = addr + 3;
					else {
						ch[i].loopCnt = c;
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
					EmitMidi(i, 0x90 | (ch[i].lastNote << 8) | (100u << 16));
					ch[i].wait = b2;
					ch[i].pc = addr + 3;
					break;
				case 25:
					EmitMidi(i, 0xB0 | ((uint8_t)MisaoPanCc(b1) << 8) | (10u << 16));
					ch[i].pc = addr + 3;
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

bool SasamiMisaoSynth::Open(const SasamiSong& song, uint32_t sampleRate, const wchar_t* programsTxtDir, unsigned* sharedTempoT)
{
	Close();
	if (!SasamiMisaoActive(song)) return false;
	m = new Impl();
	m->song = song;
	m->sharedT = sharedTempoT;
	m->rate = sampleRate < 8000 ? 44100 : sampleRate;
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
