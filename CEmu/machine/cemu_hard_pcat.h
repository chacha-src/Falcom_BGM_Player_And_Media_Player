#pragma once
#include "cemu_hard.h"
#include "../chip/cemu_chip.h"
#include "../cemu_zipfs.h"
#include "cemu_dos98.h"

/* PC/AT hard: NP2 i286 + DOS + AdLib/SB OPL @0x388 + CMS SAA @0x220 +
   PC speaker (PIT2+0x61) + MPU-401 UART @0x330 + hoot EXT.
   Subtypes: adlib/opl/sb16, gameblaster/cms, beep, midiout (MPU capture). */

enum { CEMU_PCAT_MIDI_CAP = 256000 };

class CHardPcat : public CHard {
public:
	CHardPcat();
	~CHardPcat() override;

	int Init(const CEmuGameEntry* ge, int sampleRate);
	void Shutdown();
	int SampleRate() const { return sampleRate_; }
	int LoadRoms(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode);
	int TriggerPlay(unsigned titleCode);
	void PumpCycles(uint64_t endCycle);
	void DrainInterrupt(uint64_t budgetCycles);

	Ay_Cpu* Cpu() override { return NULL; }
	uint8_t* Mem() override;
	CChip* SoundChip() override { return chip_; }

	uint8_t PortIn(uint16_t port) override;
	void PortOut(uint16_t port, uint8_t data) override;

	void AttachIoHooks();
	void DetachIoHooks();
	int DeliverIrqs();
	void TickSide(uint64_t cpuCycles);

	/* Mix speaker + CMS into OPL (or empty) stereo buffer. */
	void MixExtra(int16_t* stereo, int frames);
	/* Key-off OPL / mute SAA / gate speaker — end-of-song hang fix. */
	void MuteAllSound();
	/* Export captured MPU UART bytes as Type-0 SMF. Returns 1 if enough events. */
	int ExportCapturedSmf(const wchar_t* path) const;
	unsigned MidiByteCount() const { return midiCount_; }
	unsigned MidiNoteOnCount() const;
	const char* DosSongName() const { return dosSong_; }
	uint8_t MidiByteAt(unsigned i) const {
		return (midiBytes_ && i < midiCount_) ? midiBytes_[i] : (uint8_t)0;
	}
	uint32_t MidiDeltaAt(unsigned i) const {
		return (midiDelta_ && i < midiCount_) ? midiDelta_[i] : 0u;
	}
	int MidiUartMode() const { return mpuUart_; }
	void MidiForceUart(int on) { mpuUart_ = on ? 1 : 0; }
	unsigned SilpSongBytes() const { return silpSongBytes_; }
	void MidiCaptureReset();

	CEmuDos98* Dos() { return &dos_; }

	int cpuHz_;
	int bootClockMul_; /* catalog clockmul — applied only while booting DOS */
	int oplHz_;
	int funcVect_;
	uint64_t cpuCycles_;
	unsigned oplWriteCount_;
	unsigned oplKeyOnCount_;
	unsigned saaWriteCount_;
	unsigned saaToneOnCount_;
	unsigned speakerToneCount_;
	unsigned midiCount_;
	unsigned irq0Count_;
	int dosStubReady_;
	uint8_t stubState_;
	uint8_t extCmd_;
	uint16_t extSong_;
	uint16_t extParam_;
	int modeCms_;
	int modeBeep_;
	int modeMidi_;
	char hootAdvName_[16]; /* e.g. ADLIB.ADV / SBP2FM.ADV */
	uint16_t hootAdvSeg_; /* resident AIL .ADV image for HOOT register_driver */
	unsigned hootAdvSize_;
	uint16_t hootAdvQuantumOff_; /* XMIDI quantum (ADLIB=232D, SBP2FM=295B) */
	uint16_t hootAdvIoOff_; /* CS offset of runtime OPL base port word */

private:
	void MaterializeDosFiles(CEmuZipFs* fs, const CEmuGameEntry* ge);
	void BindDosRomHandles(const CEmuGameEntry* ge);
	const char* SelectedDosSong(const CEmuGameEntry* ge, unsigned titleCode) const;
	void BindDosTriggerSong(const CEmuGameEntry* ge, unsigned titleCode);
	int RunDosCommand(const char* cmdline, uint64_t budgetCycles, int stopWhenReady = 1);
	int BootDos(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode);
	void PitOut(uint16_t port, uint8_t data);
	uint8_t PitIn(uint16_t port);
	void PitTick(uint64_t cpuCycles);
	int IvtHooked(uint8_t vec) const;
	void MidiDataOut(uint8_t data);
	void MidiCmdOut(uint8_t data);
	uint8_t MidiStatusIn();
	uint8_t MidiDataIn();
	void MidiPushAck(uint8_t v);
	void MidiCaptureByte(uint8_t v);
	void RepairSilpDriverFar();
	void PreloadSilpSong(unsigned titleCode);
	void PrepHootAilState();
	void FixHootAilTimer();
	void InstallHootAilTimbres();
	void RestoreHootIdleTrampoline(uint8_t* mem);
	int FarCallAil(uint16_t api, uint16_t* stackWords, int nWords, uint64_t budget);

	CChip* chip_;
	CChip* saa1_;
	CChip* saa2_;
	int sampleRate_;
	int active_;
	CEmuDos98 dos_;
	const CEmuGameEntry* dosGe_;
	char dosSong_[CEMU_ROM_NAME];

	uint32_t pitClockHz_;
	/* Channel 0 = IRQ0 timer */
	uint16_t pit0Reload_;
	uint32_t pit0Counter_;
	uint64_t pit0Residual_;
	int pit0IrqPending_;
	int pit0WriteHi_;
	int pit0ReadHi_;
	int pit0Running_;
	/* Channel 2 = PC speaker */
	uint16_t pit2Reload_;
	uint32_t pit2Counter_;
	uint64_t pit2Residual_;
	int pit2WriteHi_;
	int pit2ReadHi_;
	int pit2Out_;
	int pit2Running_;
	uint8_t pitCtrlLatch_; /* last control for channel select */
	uint8_t port61_;
	uint64_t spkPhase_;
	uint64_t spkPhaseInc_;

	uint8_t picMask_;
	int picMasterIcw_;
	uint8_t picMasterIcw1_;
	uint64_t oplPumpResidual_;

	/* MPU-401 UART @ 0x330/0x331 */
	int mpuUart_;
	uint8_t mpuRx_;
	int mpuRxFull_;
	uint8_t mpuAckQ_[8];
	int mpuAckR_, mpuAckW_;

	uint8_t* midiBytes_;   /* heap CEMU_PCAT_MIDI_CAP */
	uint32_t* midiDelta_;  /* heap CEMU_PCAT_MIDI_CAP */
	uint64_t midiLastCycle_;
	uint16_t silpDrvSeg_; /* Sierra silp_at.com: preserve ADL/CMS/MT32 load seg */
	uint16_t silpSongSeg_; /* preserve song buffer seg at CS:0275 */
	unsigned silpSongBytes_; /* SCI/song bytes; IRQ stack sits above this */
	int silpScanDone_;    /* one-shot full-mem DRV scan */
	int hootTimerFixed_;
	uint16_t hootAilCs_; /* AIL code segment once API_timer is known */

	/* Minimal Sound Blaster DSP detect (0x226/22A/22C/22E) — music is still OPL. */
	int sbDspResetting_;
	uint8_t sbDspReadData_;
	int sbDspReadAvail_;
	uint8_t sbDspQueue_[4];
	int sbDspQueueR_, sbDspQueueW_;
	uint8_t saaSel_[2];
	uint8_t saaAmp_[2][6];
	uint8_t saaFreq_[2][6];
	uint8_t saaOct_[2][6];
	uint8_t saaEn_[2];
	void SbDspPush(uint8_t v);
	void CmsTrackSaa(int chip, uint8_t data);
};

void CEmuHardPcatSetActive(CHardPcat* hw);
