#pragma once
#include "cemu_hard.h"
#include "../chip/cemu_chip.h"
#include "../cemu_zipfs.h"
#include "cemu_dos98.h"
#include "cemu_np2ctx.h"

/* PC-98 ハード: NP2 i286 + OPN(A) @ 0x188 + PIT/PIC + hoot EXT。
   ライブ NP2 コアはプロセス全体で 1 組。BindNp2 がこのインスタンスの RAM/CPU を入れ替える。 */

enum { CEMU_PC98_MIDI_CAP = 256000 };

class CHardPc98 : public CHard {
public:
	CHardPc98();
	~CHardPc98() override;

	int Init(const CEmuGameEntry* ge, int sampleRate);
	void Shutdown();
	int EnsureNp2Ram();
	void BindNp2();
	int SampleRate() const { return sampleRate_; }
	int PmdOpnIrq() const { return pmdOpnIrq_; }
	int PmdPlayArmed() const { return pmdPlayArmed_; }
	int LoadRoms(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode);
	int TriggerPlay(unsigned titleCode);
	int TriggerStop();
	/* 割り込みがブートアイドルへ戻るまで（または予算まで）CPU を回す */
	void DrainInterrupt(uint64_t budgetCycles);
	/* DOS／汎用ポンプ: IRQ 配送、トランポリン DOS、アイドル HLT 量子 */
	void PumpCycles(uint64_t endCycle);
	/* この CPU サイクル分のマスタクロックを OPN(A) へ。cpuCycles_ を進める経路は必ず呼ぶ。呼ばないとチップタイマが遅れる。 */
	void AdvanceOpnClocks(uint64_t cpuCycles);

	Ay_Cpu* Cpu() override { return NULL; }
	uint8_t* Mem() override;
	CChip* SoundChip() override { return chip_; }
	/* SOUND ORCHESTRA の第 2 FM。他ボードでは NULL */
	CChip* OplChip() const { return opl_; }
	int SorchMode() const { return modeSorch_; }
	/* PIT ch1 + PPI スピーカ（PC-98 beep / 1bit DAC）。常に混成。スピーカも鳴らす OPN リップが無音にならないようにする。 */
	void MixBeep(int16_t* stereo, int frames);
	unsigned BeepActivity() const { return beepEventCount_; }
	int ModeBeep() const { return modeBeep_; }
	/* PumpCycles 外のランループ用 CEMU_PC98_IPPROF 採取（非 DOS pc98vx はドライバから np2_step） */
	void ProfSample();
	/* ブート診断: 一度も走らなかったゲストと、走ったが FM を組まなかったゲストを分ける */
	uint64_t CpuCycles() const { return cpuCycles_; }
	unsigned OpnWriteCount() const { return opnWriteCount_; }

	uint8_t PortIn(uint16_t port) override;
	void PortOut(uint16_t port, uint8_t data) override;

	void AttachIoHooks();
	void DetachIoHooks();

	/* PIT/VSYNC 端数を進め、期限なら IRQ0/VSYNC/OPN を届ける */
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
	/* ゲストが HostService 0x10 で dataAddr_ を渡したときセット */
	int dataAddrHost_;
	int fileSize_;
	int data2Addr_;
	int file2Size_;
	int addressing_; /* 0=平坦リニア、1=seg:off ワード */
	int isDos_;
	int nopnDrv_; /* 1 なら NOPNDRV.COM を載せた（曲ポインタは DS:19F4） */
	int dofmd_; /* 1 なら DOFMD/BRANM 糊を載せた（host 0x11 → リアルモード曲ポインタ） */
	int fmd98_; /* 1 なら FMD98.BIN/DRV を載せた（Falcom TotalSound） */
	int fmdSongOff_; /* CS 相対の曲バッファ（例 0x22E0）。未知なら 0 */
	int rx98_; /* 1 なら RX.BIN 糊を載せた（Falcom Ys/Brandish 期 OPN ドライバ） */
	int rxSongOff_; /* CS 相対の曲バッファ（AH=0 mov si）。未知なら 0 */
	int prog98_; /* 1 なら Falcom PROG.BIN 糊（Alm/LM）: INT7F cmd1 が DS:SI 曲を読む */
	int progSongAddr_; /* 平坦曲プリロード（例 2000:6000 → 0x26000）。未知なら 0 */
	int bst398_; /* 1 なら BirdySoft BST3（0FC00 ドライバ。cmd0 AH!=0 でロード選択） */
	int koei98_; /* 1 なら koei98.bin 糊を載せた（INT 40h、パック seg:off ROM） */
	int cal98_; /* 1 なら BirdySoft CAL/PAL INT60 ドライバ（OPN ISR の IVT 植込が要る） */
	int madp98_; /* 1 なら QueenSoft MADP_98（OPN ISR を INT40 に植える。INT0B ミラーが要る） */
	int n3golf98_; /* 1 なら n3golf98.bin 糊（INT D2。OPN ISR は INT14 に置く） */
	int dks98_; /* 1 なら KSK DKS/FQ BGMDRV 系（INT69 AH=0。ホスト seg:off 曲バンク） */
	int mdplay98_; /* 1 なら Glodia MDPLAY.BIN（非 D）— INT08 タイマ ISR が要る */
	int musicComKeepalive_; /* 1: fakecall/music/46 — 再生中 MUSIC.COM [0294]=0 を維持 */
	int synthIfKeepalive_; /* 1: SYNTH_98/S20 は INT60 後 IF=0。ホスト STI で OPN IRQ を通す */
	int modeMidi_; /* カタログ midiout — MPU-401 UART @ E0D0/E0D2（FMP -m 等） */
	int midiCapArmed_; /* BootDos シェル中は 0。以降 1 — 0x00 洪水を避ける */
	uint8_t sound86Mask_; /* A460 下位: bit0=OPNA 拡張、bit1=OPNA マスク（MAME/NP2） */
	uint8_t sound86FifoCtl_; /* A468 */
	uint8_t sound86DacCtl_; /* A46A */
	uint8_t sound86Mute_; /* A66E */
	int wolfteam98_; /* 1 なら 000_BOOT + F000 糊（INT 4A 再生、曲 @ dataaddr） */
	int wolfMiSeg_; /* 載せた MF 音色のリアルモード seg（無ければ 0） */
	int wolfSyncRun_; /* 0=ブート時オープンバス FF（4713）。1=実行時 not-busy */
	/* 再配置 BSS（d_98 既定。000_BOOT オペコード文脈からスキャン） */
	uint16_t wolfGateStop_; /* 再生停止ゲート（d_98: 5B48） */
	uint16_t wolfGatePlay_; /* 再生実行ゲート（d_98: 5B5A） */
	uint16_t wolfSongPtr_; /* 曲への far ポインタ（d_98: 5B5D） */
	uint16_t wolfSongBuf_; /* CS 相対の曲シャドウ（d_98: 7E5E） */
	uint16_t wolfTitleWord_; /* 曲コード語（d_98: 643A） */
	uint16_t wolfFlagA_; /* INT4C 再生武装バイト（d_98: 0662。gou: 062F） */
	int wstimer_; /* カタログ wstimer>0: SORC98 風 INT D2 再生はコマンド順の組み直しが要る */
	int dummySndRom_; /* カタログ dummysndrom: BIOS 音源ありビットを植える */
	int pc88VaIo_; /* 1: PC-88VA / vados — OPN ポート族が 2 系統 */
	int sorcGlue_; /* 1: Falcom SORCERIAN bootcs 糊（data @3000 または VA @11800） */
	uint16_t olteusMapSeg_; /* olteus_va: タイマ ISR 用 MAP.EXE ロード seg（CS） */
	uint16_t olteusDataSeg_; /* MAP DS（CS+0x0F86）— [003C]/[CC4D]/[5Bxx] はここ */
	int olteusTimerOn_; /* VA ポート 134/136/10A 武装（OUT 10A,0022） */
	int olteusTrampOk_; /* INT08 トランポリンを植えた（任意） */
	int olteusIrqPulse_; /* TickSide が武装。PumpCycles が MAP:09BC をソフトコール */
	int olteusInTick_; /* MAP tick へのソフトコールが生きている間 1 */
	uint64_t olteusTimerResidual_;
	uint64_t olteusTickGuard_; /* ソフトコール以降 MAP に費やしたサイクル */
	unsigned vaPc88PortHits_; /* VA 上の 44h/A8h OUT */
	unsigned vaPc98PortHits_; /* VA 上の 188h OPN OUT */
	uint8_t vaPc88LatchedAddr_; /* PC-88 OPN ポートの載せ先 */
	uint8_t vaPc88LatchedAddrHi_; /* PC-88 OPNA ハイバンクの載せ先 */
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
	unsigned opnKeyOnCount_; /* スロットビット付き reg 0x28 */
	unsigned opnTlLiveCount_; /* 初期化後 data < 0x7F の TL/KS レジスタ */
	unsigned opnFnumCount_; /* A0-A6 / 1A0-1A6 F-number 書込 */
	unsigned opnTimerCount_; /* 24/25/27 タイマレジスタ */
	unsigned opnIrqDeliverCount_; /* 受けた OPN IRQ（INT 0Bh） */
	/* reg 0x28 チャネル欄ごとのキーオン。OPNA だと思ったドライバは ch 4-6 を叩くが YM2203 では鳴らない。
	   合計キーオン数だけでは本物の演奏と区別できない。 */
	unsigned opnKeyOnCh_[8];
	unsigned pitTickCount_;  /* PIT ch0 ラップ（IRQ0 が撃たれたはず） */
	unsigned timerIrqCount_; /* INT 08 配送回数 — ISR が走らないドライバと、走って何もしないドライバを分ける */
	int lastSongLoadOk_;
	int lastSongLoadBytes_;
	/* デバッグ: 直近 OPN addr/data 対（addr ポートのあと data ポート） */
	uint16_t opnLogAddr_[64];
	uint8_t opnLogData_[64];
	unsigned opnLogCount_;
	/* 先頭 64 書込は常にリセット／初期化の定型。音が出ないときの書込は末尾なので、直近のリングも残す */
	uint16_t opnTailAddr_[64];
	uint8_t opnTailData_[64];
	unsigned opnTailCount_;
	uint8_t opnLatchedAddr_;
	uint8_t ssgPortAJumper_; /* ソフト SSG I/O A。bit7 セットで PortIn 上書きを許可 */
	uint8_t ssgEcho_[16]; /* 直近 SSG 00-0F DATA0 書込。IN 比較を検出 */
	uint8_t opnLatchedAddrHi_;
	/* MPU-401 UART キャプチャ（midiout / FMP3 -m → VST ライブ注入） */
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
	/* Wolfteam E0D0 コマンドストリームキャプチャ（E0D0 へ書いた生バイト） */
	uint8_t wolfCmdLog_[2048];
	unsigned wolfCmdLogCount_;
	unsigned wolfCmdWriteCount_; /* E0D0 書込総数（ログ上限を超え得る） */
	/* E0D0 MIDI ストリームパーサ状態 + ブリッジ統計 */
	int wolfBridgeEnable_;
	unsigned wolfNoteOnCount_;
	unsigned wolfNoteOffCount_;
	unsigned wolfCtrlCount_;
	uint8_t wolfRunStatus_;
	uint8_t wolfData_[2];
	int wolfDataIdx_;
	int wolfDataNeed_;
	int wolfInSysex_;
	/* MIDI→FM ブリッジ用 OPN FM ボイス割当 */
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
	/* Wolfteam E0D0 MUSDRV コマンドストリーム → OPN ソフトブリッジ */
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
	/* MPU-401 intelligent: PIT ラップで clock-to-host / play ステップ */
	void MpuClockTick();
	void MpuFinishReset();

	CChip* chip_;
	/* SNE SOUND ORCHESTRA: 26K 互換 YM2203 に加え 0x18C/0x18E の第 2 FM。他 PC-98 では OPNA ハイバンク。
	   非 0 ならその 2 ポートを opl_ へ。1=plain/L（YM3812）、2=V/VS/LS（Y8950。当面 FM 部のみ）。 */
	int modeSorch_;
	CChip* opl_;
	/* OPL 半分のシャドウ。9ch を OPN の横のモニタ行に出せるようにする */
	void SorchTrackOplWrite(uint8_t reg, uint8_t data);
	uint8_t sorchOplRegs_[256];
	uint8_t sorchOplOn_[9];
	int sampleRate_;
	int active_;
	uint8_t* midiBytes_;
	uint32_t* midiDelta_;
	unsigned midiCount_;
	unsigned midiNoteOnCount_;
	unsigned midiPortOutCount_; /* キャプチャ非武装でも E0D0 OUT */
	uint64_t midiLastCycle_;
	int mpuUart_;
	uint8_t mpuAckQ_[32];
	unsigned mpuAckR_;
	unsigned mpuAckW_;
	uint8_t mpuRx_;
	int mpuRxFull_;
	/* Intelligent モードファーム（FMD / MPU-401。UART 3Fh ではない） */
	uint8_t mpuCmdByte_;
	uint8_t mpuTempo_;
	uint8_t mpuTimebase_;
	uint8_t mpuCthRate_;
	int mpuClockToHost_;
	int mpuWsdChan_;
	uint64_t mpuCthResidual_;
	int mpuResetBusy_;
	uint64_t mpuResetUntil_;

	/* PIT チャネル 0 */
	uint32_t pitClockHz_;
	uint16_t pitReload_;
	uint32_t pitCounter_;
	uint64_t pitResidual_;
	int pitIrqPending_;
	int pitWriteHi_;
	int pitReadHi_;
	int pitRunning_;
	/* カウンタラッチコマンド状態。読込は実際の位置を返す必要がある。ドライバが CPU をこれで較正する。 */
	uint16_t pitLatch_;
	int pitLatched_;

	/* PIT ch1 = スピーカ矩形（ポート 0x73 / コントロール 0x77 ch=1） */
	uint16_t pit1Reload_;
	uint32_t pit1Counter_;
	int pit1WriteHi_;
	int pit1ReadHi_;
	int pit1Access_; /* 8253 RW: 1=下位、2=上位、3=下位/上位 */
	int pit1Running_;
	uint64_t pit1Phase_;
	uint64_t pit1PhaseInc_;
	void BeepCommitPit1();
	/* PPI ポート C @ 0x35: bit3 クリアでスピーカゲート ON（MAME/QEMU） */
	uint8_t ppiC_;
	int modeBeep_;
	unsigned beepEventCount_;
	int beepMonOn_;
	int beepMonMidi_;
	void BeepMonUpdate();
	void BeepSetGateFromPpi();

	/* VSYNC 約 60Hz */
	uint64_t vsyncResidual_;
	int vsyncPending_;
	uint8_t gdcA0Poll_;
	/* PumpCycles ホストサンプルを跨ぐ OPN クロック端数（DOS 経路） */
	uint64_t opnPumpResidual_;

	/* ホストサービスラッチ（0x7D0 族） */
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

	uint8_t* np2Ram_;
	uint8_t np2Cpu_[CEMU_NP2_CPU_SIZE];
	int np2HaveCpu_;
	int pmdOpnIrq_; /* PMD*: 音楽クロックは OPN Timer B — INT08 を届けない */
	int pmdPlayArmed_; /* TriggerPlay 後 1 — BootDos はまだ INT08 が要る */
};

void CEmuHardPc98SetActive(CHardPc98* hw);
CHardPc98* CEmuHardPc98GetActive();
