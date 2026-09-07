#pragma once
#include "cemu_hard.h"
#include "../chip/cemu_chip.h"
#include "../cemu_zipfs.h"
#include "cemu_dos98.h"

/* PC-98 hard: NP2 i286 + OPN(A) @ 0x188 + PIT/PIC + hoot EXT ports.
   Global NP2 core — only one instance active. */

enum { CEMU_PC98_MIDI_CAP = 256000 };

class CHardPc98 : public CHard {
public:
	CHardPc98();
	~CHardPc98() override;

	int Init(const CEmuGameEntry* ge, int sampleRate);
	void Shutdown();
	int SampleRate() const { return sampleRate_; }
	int LoadRoms(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode);
	int TriggerPlay(unsigned titleCode);
	int TriggerStop();
	/* Run CPU until interrupt returns to the boot idle loop (or budget). */
	void DrainInterrupt(uint64_t budgetCycles);
	/* DOS / general pump: IRQ delivery, trampoline DOS, idle HLT quantum. */
	void PumpCycles(uint64_t endCycle);
	/* Feed the OPN(A) its master clocks for a span of CPU cycles. Every path
	   that advances cpuCycles_ must call this or the chip timers run slow. */
	void AdvanceOpnClocks(uint64_t cpuCycles);

	Ay_Cpu* Cpu() override { return NULL; }
	uint8_t* Mem() override;
	CChip* SoundChip() override { return chip_; }

	uint8_t PortIn(uint16_t port) override;
	void PortOut(uint16_t port, uint8_t data) override;

	void AttachIoHooks();
	void DetachIoHooks();

	/* Advance PIT/VSYNC residuals; deliver IRQ0/VSYNC/OPN if due. */
	int DeliverIrqs();
	void TickSide(uint64_t cpuCycles);

	int Int60Hooked() const;
	CEmuDos98* Dos() { return isDos_ ? &dos_ : NULL; }

	int opnaMode;
	int cpuHz_;
	int opnHz_;
	int bootCs_;
	int bootIp_;
	int funcVect_;
	int dataAddr_;
	int fileSize_;
	int data2Addr_;
	int file2Size_;
	int addressing_; /* 0=flat linear, 1=seg:off words */
	int isDos_;
	int nopnDrv_; /* 1 if NOPNDRV.COM staged (song ptr at DS:19F4) */
	int dofmd_; /* 1 if DOFMD/BRANM glue staged (host 0x11 → real-mode song ptr) */
	int fmd98_; /* 1 if FMD98.BIN/DRV staged (Falcom TotalSound) */
	int fmdSongOff_; /* CS-relative song buffer (e.g. 0x22E0); 0 if unknown */
	int rx98_; /* 1 if RX.BIN glue staged (Falcom Ys/Brandish-era OPN driver) */
	int rxSongOff_; /* CS-relative song buffer (AH=0 mov si); 0 if unknown */
	int prog98_; /* 1 if Falcom PROG.BIN glue (Alm/LM): INT7F cmd1 reads DS:SI song */
	int progSongAddr_; /* flat song preload (e.g. 2000:6000 → 0x26000); 0 if unknown */
	int bst398_; /* 1 if BirdySoft BST3 (0FC00 driver; cmd0 AH!=0 selects load) */
	int koei98_; /* 1 if koei98.bin glue staged (INT 40h, packed seg:off ROMs) */
	int cal98_; /* 1 if BirdySoft CAL/PAL INT60 driver (needs OPN ISR IVT install) */
	int madp98_; /* 1 if QueenSoft MADP_98 (OPN ISR planted on INT40, needs INT0B mirror) */
	int n3golf98_; /* 1 if n3golf98.bin glue (INT D2; OPN ISR parked on INT14) */
	int dks98_; /* 1 if KSK DKS/FQ BGMDRV family (INT69 AH=0; host seg:off song bank) */
	int mdplay98_; /* 1 if Glodia MDPLAY.BIN (non-D) — needs INT08 timer ISR) */
	int musicComKeepalive_; /* 1: fakecall/music/46 — hold MUSIC.COM [0294]=0 while playing */
	int modeMidi_; /* catalog midiout — MPU-401 UART @ E0D0/E0D2 (FMP -m etc.) */
	int midiCapArmed_; /* 0 during BootDos shells; 1 after — avoid 0x00 flood */
	uint8_t sound86Mask_; /* A460 low bits: bit0=OPNA enhance, bit1=OPNA mask (MAME/NP2) */
	uint8_t sound86FifoCtl_; /* A468 */
	uint8_t sound86DacCtl_; /* A46A */
	uint8_t sound86Mute_; /* A66E */
	int wolfteam98_; /* 1 if 000_BOOT + F000 glue (INT 4A play, song @ dataaddr) */
	int wolfMiSeg_; /* real-mode seg of staged MF instrument (0 if none) */
	int wolfSyncRun_; /* 0=boot open-bus FF (4713); 1=runtime not-busy */
	/* Relocated BSS (d_98 defaults; scanned from 000_BOOT opcode context). */
	uint16_t wolfGateStop_; /* play-stop gate byte (d_98: 5B48) */
	uint16_t wolfGatePlay_; /* play-run gate byte (d_98: 5B5A) */
	uint16_t wolfSongPtr_; /* far ptr to song (d_98: 5B5D) */
	uint16_t wolfSongBuf_; /* CS-relative song shadow (d_98: 7E5E) */
	uint16_t wolfTitleWord_; /* title code word (d_98: 643A) */
	uint16_t wolfFlagA_; /* INT4C play-armed byte (d_98: 0662; gou: 062F) */
	int wstimer_; /* catalog wstimer>0: SORC98-style INT D2 play needs cmd re-order */
	int dummySndRom_; /* catalog dummysndrom: plant BIOS sound-present bits */
	int pc88VaIo_; /* 1: PC-88VA / vados — dual OPN port families */
	int sorcGlue_; /* 1: Falcom SORCERIAN bootcs glue (data @3000 or VA @11800) */
	uint16_t olteusMapSeg_; /* olteus_va: MAP.EXE load seg (CS) for timer ISR */
	uint16_t olteusDataSeg_; /* MAP DS (CS+0x0F86) — [003C]/[CC4D]/[5Bxx] live here */
	int olteusTimerOn_; /* VA ports 134/136/10A armed (OUT 10A,0022) */
	int olteusTrampOk_; /* INT08 trampoline planted (optional) */
	int olteusIrqPulse_; /* TickSide arms; PumpCycles soft-calls MAP:09BC */
	int olteusInTick_; /* 1 while soft-call into MAP tick is live */
	uint64_t olteusTimerResidual_;
	uint64_t olteusTickGuard_; /* cycles spent in MAP since soft-call */
	unsigned vaPc88PortHits_; /* 44h/A8h OUTs on VA */
	unsigned vaPc98PortHits_; /* 188h OPN OUTs on VA */
	uint8_t vaPc88LatchedAddr_; /* staged addr for PC-88 OPN ports */
	uint8_t vaPc88LatchedAddrHi_; /* staged hi-bank addr for PC-88 OPNA */
	uint64_t cpuCycles_;
	uint8_t extCmd_;
	uint16_t extSong_;
	uint16_t extParam_;
	uint8_t stubState_;
	uint8_t picMask_;
	uint8_t slavePicMask_;
	int opnInService_;
	int irqEdgeSeen_;
	int irqEdgeConsumed_;
	unsigned opnWriteCount_;
	unsigned opnKeyOnCount_; /* reg 0x28 with slot bits */
	unsigned opnTlLiveCount_; /* TL/KS regs with data < 0x7F after init */
	unsigned opnFnumCount_; /* A0-A6 / 1A0-1A6 f-number writes */
	unsigned opnTimerCount_; /* 24/25/27 timer regs */
	unsigned opnIrqDeliverCount_; /* accepted OPN IRQs (INT 0Bh) */
	int lastSongLoadOk_;
	int lastSongLoadBytes_;
	/* Debug: last OPN addr/data pairs (addr port then data port). */
	uint16_t opnLogAddr_[64];
	uint8_t opnLogData_[64];
	unsigned opnLogCount_;
	uint8_t opnLatchedAddr_;
	uint8_t ssgPortAJumper_; /* soft SSG I/O A; bit7 set enables PortIn override */
	uint8_t opnLatchedAddrHi_;
	/* MPU-401 UART capture (midiout / FMP3 -m → VST live inject). */
	unsigned MidiByteCount() const { return midiCount_; }
	uint8_t MidiByteAt(unsigned i) const {
		return (midiBytes_ && i < midiCount_) ? midiBytes_[i] : (uint8_t)0;
	}
	uint32_t MidiDeltaAt(unsigned i) const {
		return (midiDelta_ && i < midiCount_) ? midiDelta_[i] : 0u;
	}
	unsigned MidiNoteOnCount() const { return midiNoteOnCount_; }
	unsigned MidiPortOutCount() const { return midiPortOutCount_; }
	const char* DosSongName() const { return dosSong_; }
	void MidiForceUart(int on) { mpuUart_ = on ? 1 : 0; }
	void MidiCaptureReset();
	/* Wolfteam E0D0 command-stream capture (raw bytes written to E0D0). */
	uint8_t wolfCmdLog_[2048];
	unsigned wolfCmdLogCount_;
	unsigned wolfCmdWriteCount_; /* total E0D0 writes (may exceed log cap) */
	/* E0D0 MIDI stream parser state + bridge stats. */
	int wolfBridgeEnable_;
	unsigned wolfNoteOnCount_;
	unsigned wolfNoteOffCount_;
	unsigned wolfCtrlCount_;
	uint8_t wolfRunStatus_;
	uint8_t wolfData_[2];
	int wolfDataIdx_;
	int wolfDataNeed_;
	int wolfInSysex_;
	/* OPN FM voice allocation for the MIDI→FM bridge. */
	int wolfVoiceCount_;
	int wolfVoiceActive_[6];
	int wolfVoiceMidiCh_[6];
	int wolfVoiceNote_[6];
	uint64_t wolfVoiceAge_[6];
	uint64_t wolfVoiceClock_;
	uint8_t wolfChVol_[16];
	uint8_t wolfChExpr_[16];

private:
	void FreeBanks();
	void StageBanks(CEmuZipFs* fs, const CEmuGameEntry* ge);
	int LoadSongToAddr(unsigned songNum, int destAddr, int maxSize, int isSecondary);
	void HostService(uint8_t func);
	void PitOut(uint16_t port, uint8_t data);
	uint8_t PitIn(uint16_t port);
	void PitTick(uint64_t cpuCycles);
	/* Wolfteam E0D0 MUSDRV command-stream → OPN soft bridge. */
	void WolfCmdByte(uint8_t data);
	void WolfBridgeReset();
	void WolfMidiDispatch(uint8_t status, uint8_t d0, uint8_t d1);
	void WolfNoteOn(int midiCh, int note, int vel);
	void WolfNoteOff(int midiCh, int note);
	void WolfAllNotesOff();
	void WolfProgramVoice(int v, int vel, int midiCh);
	void WolfOpnW(int bank, uint8_t reg, uint8_t val);
	void ArmOlteusVaTimer(uint16_t mapSeg);
	void MidiDataOut(uint8_t data);
	void MidiCmdOut(uint8_t data);
	uint8_t MidiStatusIn();
	uint8_t MidiDataIn();
	void MidiPushAck(uint8_t v);
	void MidiCaptureByte(uint8_t v);

	CChip* chip_;
	int sampleRate_;
	int active_;
	uint8_t* midiBytes_;
	uint32_t* midiDelta_;
	unsigned midiCount_;
	unsigned midiNoteOnCount_;
	unsigned midiPortOutCount_; /* E0D0 OUTs even when capture disarmed */
	uint64_t midiLastCycle_;
	int mpuUart_;
	uint8_t mpuAckQ_[8];
	unsigned mpuAckR_;
	unsigned mpuAckW_;
	uint8_t mpuRx_;
	int mpuRxFull_;

	/* PIT ch0 */
	uint32_t pitClockHz_;
	uint16_t pitReload_;
	uint32_t pitCounter_;
	uint64_t pitResidual_;
	int pitIrqPending_;
	int pitWriteHi_;
	int pitReadHi_;
	int pitRunning_;

	/* VSYNC ~60 Hz */
	uint64_t vsyncResidual_;
	int vsyncPending_;
	/* OPN clock residual across PumpCycles host samples (DOS path) */
	uint64_t opnPumpResidual_;

	/* Host service latch (0x7D0 family) */
	uint8_t hostFunc_;
	uint16_t hostParam1_;
	uint16_t hostParam2_;
	uint16_t hostParam3_;
	uint8_t hostStatus_;

	unsigned char* bgmBank_[256];
	unsigned bgmBankSize_[256];
	unsigned char* bgm2Bank_[256];
	unsigned bgm2BankSize_[256];

	CEmuDos98 dos_;
	int dosStubReady_;
	int picMasterIcw_;
	int picSlaveIcw_;
	uint8_t picMasterIcw1_;
	uint8_t picSlaveIcw1_;
	char dosSong_[DOS98_NAME];
	const CEmuGameEntry* dosGe_;

	int BootDos(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode);
	int RunDosCommand(const char* cmdline, uint64_t budgetCycles);
	void MaterializeDosFiles(CEmuZipFs* fs, const CEmuGameEntry* ge);
	void BindDosRomHandles(const CEmuGameEntry* ge);
	void BindDosTriggerSong(const CEmuGameEntry* ge, unsigned titleCode);
	const char* SelectedDosSong(const CEmuGameEntry* ge, unsigned titleCode) const;
	int RunDosDevices(const CEmuGameEntry* ge, uint64_t budgetCycles);
};

void CEmuHardPc98SetActive(CHardPc98* hw);
CHardPc98* CEmuHardPc98GetActive();
