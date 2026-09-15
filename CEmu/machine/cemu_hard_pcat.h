#pragma once
#include "cemu_hard.h"
#include "../chip/cemu_chip.h"
#include "../cemu_zipfs.h"
#include "cemu_dos98.h"
#include "cemu_np2ctx.h"

/* PC/AT ハード: NP2 i286 + DOS + AdLib/SB OPL @0x388 + CMS SAA @0x220 +
   PC スピーカ（PIT2+0x61）+ MPU-401 UART @0x330 + hoot EXT。
   subtype: adlib/opl/sb16、gameblaster/cms、beep、tandy（SN76496 @0xC0）、
   ps1（IBM PS/1 Audio SN @0x200）、midiout（MPU キャプチャ）。 */

enum { CEMU_PCAT_MIDI_CAP = 256000 };

class CHardPcat : public CHard {
public:
	CHardPcat();
	~CHardPcat() override;

	int Init(const CEmuGameEntry* ge, int sampleRate);
	void Shutdown();
	int EnsureNp2Ram();
	void BindNp2();
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

	/* スピーカと CMS を OPL（または空）ステレオへ混成 */
	void MixExtra(int16_t* stereo, int frames);
	/* OPL キーオフ／SAA mute／スピーカゲート — 曲終了のハング対策 */
	void MuteAllSound();
	/* キャプチャした MPU UART を Type-0 SMF で出す。イベントが足りれば 1 */
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
	int bootClockMul_; /* カタログ clockmul — DOS ブート中だけ適用 */
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
	int modeSb_;
	int modeMidi_;
	/* Sierra silp_at.com 糊。silp 修復は CS:0265..027B を poke し CS:0275 から曲バッファ
	   セグメントを読む。他の INT 7Fh 糊（CODE.COM、HOOT.EXE、PMDL_AT）では無意味なオフセットで、
	   無関係な糊データと、曲 memcpy 経由でロード済みドライバを壊す。 */
	int modeSilp_;
	int modePs1_; /* IBM PS/1 Audio Card（IBMCARD.DRV、ポート 0x200-0x206） */
	char hootAdvName_[16]; /* 例: ADLIB.ADV / SBP2FM.ADV */
	uint16_t hootAdvSeg_; /* HOOT register_driver 用の常駐 AIL .ADV イメージ */
	unsigned hootAdvSize_;
	uint16_t hootAdvQuantumOff_; /* XMIDI クォンタム（ADLIB=232D、SBP2FM=295B） */
	uint16_t hootAdvIoOff_; /* 実行時 OPL ベースポート語の CS オフセット */

private:
	void MaterializeDosFiles(CEmuZipFs* fs, const CEmuGameEntry* ge);
	void BindDosRomHandles(const CEmuGameEntry* ge);
	const char* SelectedDosSong(const CEmuGameEntry* ge, unsigned titleCode) const;
	void BindDosTriggerSong(const CEmuGameEntry* ge, unsigned titleCode);
	int RunDosCommand(const char* cmdline, uint64_t budgetCycles, int stopWhenReady = 1);
	void HootSubstArgv(char* tail, int tailCap);
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
	int HootAilPossible() const;
	void FixHootAilTimer();
	void FixHootMidiInt8();
	void StartHootMidiSequence();
	void RepairMokMidiPlay();
	void RepairMokIntelMpu(int armCth);
	void InstallHootAilTimbres();
	void RestoreHootIdleTrampoline(uint8_t* mem);
	int FarCallAil(uint16_t api, uint16_t* stackWords, int nWords, uint64_t budget);

	CChip* chip_;
	CChip* saa1_;
	CChip* saa2_;
	CChip* sn764_;
	int sampleRate_;
	int active_;
	CEmuDos98 dos_;
	const CEmuGameEntry* dosGe_;
	char dosSong_[CEMU_ROM_NAME];

	uint32_t pitClockHz_;
	/* チャネル 0 = IRQ0 タイマ */
	uint16_t pit0Reload_;
	uint32_t pit0Counter_;
	uint64_t pit0Residual_;
	int pit0IrqPending_;
	int pit0WriteHi_;
	int pit0ReadHi_;
	int pit0Running_;
	/* チャネル 2 = PC スピーカ */
	uint16_t pit2Reload_;
	uint32_t pit2Counter_;
	uint64_t pit2Residual_;
	int pit2WriteHi_;
	int pit2ReadHi_;
	int pit2Out_;
	int pit2Running_;
	uint8_t pitCtrlLatch_; /* チャネル選択用の直前コントロール */
	uint8_t port61_;
	uint64_t spkPhase_;
	uint64_t spkPhaseInc_;

	uint8_t picMask_;
	int pic0Isr_; /* 未使用: 既存 obj とクラス配置を合わせるため残す */
	int picMasterIcw_;
	uint8_t picMasterIcw1_;
	uint64_t oplPumpResidual_;

	/* MPU-401 UART ポート（0x330/0x331） */
	int mpuUart_;
	uint8_t mpuRx_;
	int mpuRxFull_;
	uint8_t mpuAckQ_[8];
	int mpuAckR_, mpuAckW_;
	uint8_t mpuCmdByte_;

	uint8_t* midiBytes_;   /* ヒープ CEMU_PCAT_MIDI_CAP */
	uint32_t* midiDelta_;  /* ヒープ CEMU_PCAT_MIDI_CAP */
	uint64_t midiLastCycle_;
	uint16_t silpDrvSeg_; /* Sierra silp_at.com: ADL/CMS/MT32 ロード seg を保持 */
	uint16_t silpSongSeg_; /* CS:0275 の曲バッファ seg を保持 */
	unsigned silpSongBytes_; /* SCI/曲バイト。IRQ スタックはこの上 */
	int silpScanDone_;    /* フルメモリ DRV スキャンは一度だけ */
	uint16_t mokDrvSeg_; /* Mok MID.DRV CS — IVT 破壊後に INT8 を再植 */
	int hootTimerFixed_;
	uint16_t hootAilCs_; /* API_timer 確定後の AIL コードセグメント */

	/* 最小 Sound Blaster DSP 検出（0x226/22A/22C/22E）。音楽は依然 OPL */
	int sbDspResetting_;
	uint8_t sbDspReadData_;
	int sbDspReadAvail_;
	uint8_t sbDspQueue_[4];
	int sbDspQueueR_, sbDspQueueW_;
	uint8_t sbMixerIdx_;
	uint8_t sbMixer_[256];
	uint8_t saaSel_[2];
	uint8_t saaAmp_[2][6];
	uint8_t saaFreq_[2][6];
	uint8_t saaOct_[2][6];
	uint8_t saaEn_[2];
	uint8_t* np2Ram_;
	uint8_t np2Cpu_[CEMU_NP2_CPU_SIZE];
	int np2HaveCpu_;
	void SbDspPush(uint8_t v);
	void SbMixerReset();
	void CmsTrackSaa(int chip, uint8_t data);
};

void CEmuHardPcatSetActive(CHardPcat* hw);
