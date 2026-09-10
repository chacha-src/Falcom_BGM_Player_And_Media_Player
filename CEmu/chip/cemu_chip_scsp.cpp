#include "StdAfx.h"
#include "cemu_chip_scsp.h"
#include "cemu_chip.h"
#include "../fmmon/fmmon_shadow.h"
#include <string.h>
#include <stdlib.h>

/* Sega Model 2A/2B/2C/3 SCSP (YMF292-F): 32 slots of PCM/noise with envelope,
   LFO and DSP, wrapped around the standalone core vendored under
   CEmu/vendor/scsp (Audio Overload eng_ssf, the same core the SSF Saturn
   music format uses — Model 2/3 drive it identically over the MIDI port). */
extern "C" {
#include "../vendor/scsp/ao.h"
#include "../vendor/scsp/scsp.h"
#include "../vendor/scsp/sat_hw.h"
int CEmuScspSlotActive(int s);
double CEmuScspSlotRate(int s);
}

enum { kScspSlots = 32, kScspBlock = 512, kScspRamBytes = 512 * 1024 };

/* The core signals the sound 68000 through one context-free callback (it keeps
   a single instance in AllocedSCSP), so the pending level lives here. A
   positive argument asserts that IPL, a negative one clears it, 0 clears all. */
static int g_scspIrqLevel = 0;

/* The core keeps exactly one instance in its own AllocedSCSP global, so only
   the wrapper that started it may drive or tear it down. Without this guard a
   new chip created before the old one is destroyed makes the old destructor
   free the live instance. */
class CChipScsp;
static const CChipScsp* g_scspOwner = NULL;

static void CEmuScspIrqCb(int state)
{
	if (state > 0)
		g_scspIrqLevel = state;
	else if (state == 0 || -state == g_scspIrqLevel)
		g_scspIrqLevel = 0;
}

static int CEmuScspClamp16(int v)
{
	if (v > 32767) return 32767;
	if (v < -32768) return -32768;
	return v;
}

class CChipScsp : public CChip {
public:
	CChipScsp(uint32_t clockHz, int sampleRate)
		: clockHz_(clockHz ? clockHz : 22579200u)
		, sampleRate_(sampleRate > 0 ? sampleRate : 44100)
		, rom_(NULL)
		, romSize_(0)
		, started_(0)
		, clockAcc_(0)
	{
		memset(reg_, 0, sizeof(reg_));
		memset(monOn_, 0, sizeof(monOn_));
		memset(monMidi_, 0xff, sizeof(monMidi_));
		Start();
	}

	~CChipScsp() override
	{
		if (g_scspOwner == this) {
			scsp_term();
			g_scspOwner = NULL;
			g_scspIrqLevel = 0;
		}
	}

	void Reset() override
	{
		memset(reg_, 0, sizeof(reg_));
		memset(monOn_, 0, sizeof(monOn_));
		memset(monMidi_, 0xff, sizeof(monMidi_));
		Start();
	}

	void Write(uint32_t addr, uint32_t data) override
	{
		/* Register file is 0x1000 bytes; CEmu addresses it as 16-bit words. */
		const unsigned a = addr & 0x7ffu;
		const uint16_t v = (uint16_t)(data & 0xffffu);
		reg_[a] = v;
		if (!Live()) return;
		SCSP_0_w((offs_t)a, (data16_t)v, 0);
		UpdateMon();
	}

	void AdvanceClocks(uint64_t clocks) override
	{
		/* Model 2A/3 boot runs the 68000 for ~1.5 s before the first mix.
		   The firmware programs SCSP timers and then waits on them; a no-op
		   here left Timer A/B and MIDI SCIPD frozen, so every SCSP title
		   stayed silent even though MIDI was queued. 512 chip clocks = one
		   44.1 kHz sample (22.5792 MHz / 44100). */
		if (!Live() || clocks == 0) return;
		clockAcc_ += clocks;
		uint64_t samples = clockAcc_ / 512u;
		clockAcc_ %= 512u;
		while (samples) {
			int n = (samples > (uint64_t)kScspBlock) ? kScspBlock : (int)samples;
			memset(bufL_, 0, sizeof(bufL_));
			memset(bufR_, 0, sizeof(bufR_));
			INT16* buf[2] = { bufL_, bufR_ };
			SCSP_Update(NULL, NULL, buf, n);
			samples -= (uint64_t)n;
		}
	}

	void Render(int16_t* stereo, int frames) override
	{
		if (!stereo || frames <= 0) return;
		memset(stereo, 0, (size_t)frames * 2 * sizeof(int16_t));
		MixAdd(stereo, frames, 256);
	}

	void MixAdd(int16_t* stereo, int frames, int gain) override
	{
		if (!stereo || frames <= 0 || !Live()) return;
		int done = 0;
		while (done < frames) {
			int n = frames - done;
			if (n > kScspBlock) n = kScspBlock;
			memset(bufL_, 0, sizeof(bufL_));
			memset(bufR_, 0, sizeof(bufR_));
			INT16* buf[2] = { bufL_, bufR_ };
			SCSP_Update(NULL, NULL, buf, n);
			for (int i = 0; i < n; i++) {
				int16_t* p = stereo + (size_t)(done + i) * 2;
				p[0] = (int16_t)CEmuScspClamp16((int)p[0] + (int)bufL_[i] * gain / 256);
				p[1] = (int16_t)CEmuScspClamp16((int)p[1] + (int)bufR_[i] * gain / 256);
			}
			done += n;
		}
		UpdateMon();
	}

	/* The wave ROM is not wired to the SCSP on Model 2/3 — the sound 68000
	   copies samples from its ROM windows into the shared 512KB RAM. Keep the
	   pointer only so the host bus can serve those windows. */
	void SetPcmRom(const uint8_t* data, unsigned size) override
	{
		rom_ = data;
		romSize_ = size;
	}

	unsigned ReadReg(unsigned wordIndex)
	{
		if (!Live()) return 0;
		return (unsigned)(uint16_t)SCSP_0_r((offs_t)(wordIndex & 0x7ffu), 0);
	}

	void MidiIn(uint8_t data)
	{
		if (!Live()) return;
		SCSP_MidiIn(0, (data16_t)data, 0);
	}

	unsigned GetRegSnapshot(uint8_t* buf, unsigned cap) const override
	{
		if (!buf || cap == 0) return 0;
		const unsigned n = cap < sizeof(reg_) ? cap : (unsigned)sizeof(reg_);
		memcpy(buf, reg_, n);
		return n;
	}

	bool Irq() const override { return g_scspIrqLevel > 0; }
	void AckIrq() override {}
	uint8_t ReadStatus() override { return 0; }
	uint8_t ReadData() override { return 0; }
	uint8_t ReadStatusHi() override { return 0; }
	uint8_t ReadDataHi() override { return 0; }

private:
	void Start()
	{
		SCSPinterface intf;
		memset(&intf, 0, sizeof(intf));
		intf.num = 1;
		intf.region[0] = sat_ram;
		intf.mixing_level[0] = 0;
		intf.irq_callback[0] = CEmuScspIrqCb;
		g_scspIrqLevel = 0;
		clockAcc_ = 0;
		sat_hw_init();
		started_ = scsp_start(&intf) ? 1 : 0;
		g_scspOwner = started_ ? this : NULL;
	}

	int Live() const { return started_ && g_scspOwner == this; }

	void UpdateMon()
	{
		for (int s = 0; s < kScspSlots; s++) {
			const int on = CEmuScspSlotActive(s);
			int midi = 60;
			if (on) {
				const double rate = CEmuScspSlotRate(s);
				if (rate > 0.0) {
					/* Sample root pitch is unknown, so report the rate
					   relative to unity playback with C4 as the origin. */
					midi = FmMonShadowHzToMidi(261.6255653 * rate);
					if (midi < 0) midi = 60;
				}
			}
			if (monOn_[s] == (uint8_t)on
				&& (!on || monMidi_[s] == (uint8_t)midi))
				continue;
			monOn_[s] = (uint8_t)on;
			monMidi_[s] = (uint8_t)midi;
			FmMonShadowPcmNote(s, midi, on);
		}
	}

	uint32_t clockHz_;
	int sampleRate_;
	const uint8_t* rom_;
	unsigned romSize_;
	int started_;
	uint64_t clockAcc_;
	uint16_t reg_[0x800];
	uint8_t monOn_[kScspSlots];
	uint8_t monMidi_[kScspSlots];
	INT16 bufL_[kScspBlock];
	INT16 bufR_[kScspBlock];
};

CChip* CEmuChipScspCreate(uint32_t clockHz, int sampleRate)
{
	return new CChipScsp(clockHz, sampleRate);
}
void CEmuChipScspDestroy(CChip* c) { delete c; }

uint8_t* CEmuChipScspRam() { return sat_ram; }
unsigned CEmuChipScspRamSize() { return kScspRamBytes; }
int CEmuChipScspIrqLevel() { return g_scspIrqLevel; }

unsigned CEmuChipScspReadReg(CChip* c, unsigned wordIndex)
{
	return c ? static_cast<CChipScsp*>(c)->ReadReg(wordIndex) : 0;
}

void CEmuChipScspMidiIn(CChip* c, uint8_t data)
{
	if (c) static_cast<CChipScsp*>(c)->MidiIn(data);
}
