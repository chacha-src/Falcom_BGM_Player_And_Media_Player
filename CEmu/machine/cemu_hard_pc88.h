#pragma once

#include "cemu_hard.h"

#include "../chip/cemu_chip.h"

#include "../cemu_zipfs.h"



class CHardPc88 : public CHard {

public:

	CHardPc88();

	~CHardPc88() override;



	int Init(const CEmuGameEntry* ge, int sampleRate);

	void Shutdown();

	int SampleRate() const { return sampleRate_; }

	int LoadRoms(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode);

	/* 選んだ bgm/voice を mdata/vdata へ載せる（再呼出可）。
	   titleCode: 下位 8=バンク添字、bit 8..23=そのバンク内バイトオフセット
	   （KOEI パック MMLDATA / mfile_size）。コピーは 64K で切る。 */
	void LoadSongData(unsigned titleCode);

	/* 直前 LoadRoms の完全 title code（曲の再ロード用） */
	unsigned titleCode_;

	/* mdata が PATCH/スタック（SP=0x100）と重なると play 時の再載せは危険 */
	int ShouldRestageSong() const;

	/* KOEI FMDRV.SYS 系: パック CIM は既定 0x4000、再生は E=0 */
	int PackedKoei() const { return packedKoei_; }

	/* 現 titleCode_ のポート 80/01 再生添字（herzog サブ曲など） */
	uint8_t PlaySongIndex() const;

	/* ポート 01 パラメータ: 通常は PlaySongIndex()。song<<8 の PATCH は title 下位バイトを残す
	   （duel は param で CP 30、port80 がページ）。 */
	uint8_t PlayParamIndex() const;

	/* PATCH ブート後: I!=0 ページが空なら欠けた FE19 IM2 ベクタを埋める */
	void FixupIm2AfterBoot();

	/* カタログが RTC+VRTC を出す: IM2 表が本当に使うものだけ残す */
	void PruneDeadTickSources();

	/* gineiden: play は音源 ISR を植えるが RTC 枠は空 — ミラーする */
	void FixupIm2AfterPlay();

	/* gineiden: PATCH 4E2F が OPN Timer B をクリア。AMAIN 4E00 同様に再武装 */
	void ArmGineidenOpnTimer();
	/* lizard88: play CALL 9F0F のあと Timer B 再武装＋マスク解除 */
	void ArmLizardOpnTimer();
	void ArmFallbackOpnTimer();
	void ArmPwmajan2();
	/* yaksa PATCH2: ISR@086A は (37D1)!=0 でなければ CALL 3556 RET Z。play の
	   CALL 0774/07A7 はそのゲートを武装しないので、3CA4 オープナのあと BGM が死ぬ。 */
	void ArmYaksaPlay();
	/* navitune 系: title bit 8..23 が code@mdata 内の曲を選ぶ。
	   cmd07 の PATCH LD BC,mdata を書き換えてから play を付け替える（ホスト stub なし）。 */
	void ApplyNavituneTitleSong();
	/* navitune: コードロード後 PATCH が DI→EI を一度（hoot patch は EI しない） */
	void PrepareNavitunePatch();
	/* PATCH の LD A,07 / LD BC,… / CALL 4D00 の PC（無ければ 0） */
	unsigned NavituneRetargetPc() const;
	/* navitune: PATCH play 後 — 音源 IRQ マスク解除 / EI のみ */
	void FinishNavitunePlay();
	/* yakyufan: mute@0C5D のあと再生許可フラグを再アサート */
	int NeedsYakyufanArm() const;
	void ArmYakyufanPlay();
	int NeedsGineidenArm() const { return armGineidenTimer_; }
	int NeedsLizardArm() const { return armLizardTimer_; }
	int NeedsLongPlayDrain() const { return longPlayDrain_; }
	int NeedsNavituneArm() const { return armNavituneTimer_; }
	int SkipUnwedge() const;
	void GuardHardrankPc();
	int NeedsDeferredRtc() const { return deferRtcAfterPlay_; }
	void EnableDeferredRtc()
	{
		if (!deferRtcAfterPlay_) return;
		ArmN88RtcPlayer();
		useRtc = 1;
		deferRtcAfterPlay_ = 0;
	}
	/* N88+DEMOM/MUSIC: IM2 RTC ベクタを既知プレーヤ ISR へ強制 */
	void ArmN88RtcPlayer();
	int NeedsN88RtcGuard() const { return n88RtcIsr_ != 0; }
	void GuardN88RtcVector()
	{
		if (!n88RtcIsr_ || !mem_ || !cpu_ || cpu_->r.i != 0xF3)
			return;
		const unsigned cur = (unsigned)mem_[0xF304] | ((unsigned)mem_[0xF305] << 8);
		if (cur != n88RtcIsr_ && mem_[n88RtcIsr_] == 0xF5) {
			mem_[0xF304] = (uint8_t)(n88RtcIsr_ & 0xff);
			mem_[0xF305] = (uint8_t)(n88RtcIsr_ >> 8);
		}
		if (n88RtcThrottleAddr_ && n88RtcThrottleAddr_ < 0x10000
			&& mem_[n88RtcThrottleAddr_] == 0)
			mem_[n88RtcThrottleAddr_] = 0x01;
		if (cpu_->r.sp >= 0xF000 || cpu_->r.sp < 0x0100)
			cpu_->r.sp = 0x0200;
	}

	/* schwarz PATCH は play 周りを DI したまま EI しない — コマンド後も IRQ を生かす */
	int NeedsFe19PlayEi() const;

	/* Falcom E000 / FE19: IM2 音源ベクタを植えた状態で play 周りを DI */
	int NeedsPlayEi() const;

	/* PATCH IM2 + CALL init が DI 下（hadou/gra88）: ブート中に EI が要る */
	int NeedsBootEiPulse() const;

	/* BOTHTEC The Scheme OPNA（MUS2+ADR_+INT2）: PATCH は 0x9000 */
	int IsSchemeOpna() const { return schemeMode_; }

	/* hoot scheme Play(): フラグ 0x9010/11/13（title 下位 + バンク上位） */
	void SchemePlayTrigger(unsigned titleCode);

	/* Falcom は type=prog へ JP する前後でポート 32 音源 IRQ をマスク。prog は OPN タイマ IRQ が無いと PATCH のマスク解除に戻れない */
	int IgnoreSoundIrqMask() const;

	/* hoot oldfalcom Play(): type=prog をコピーし E00E..E014 と RAM フラグを植える */
	void ApplyFalcomPlay();

	/* PATCH コマンド待ち PC（page0 stub または Falcom E027）。無ければ -1 */
	int CmdPollPc() const;

	/* Game Arts / castle: ポートコマンドは IRQ 駆動再生を武装するが、ISR は RTC ベクタ 04
	   （castle は PROG2 入口）。ホストが init を CALL してから base。 */
	unsigned PlayKickBase() const { return playKickBase_; }
	unsigned PlayKickInitOff() const { return playKickInitOff_; }
	int PlayKickEi() const { return playKickEi_; }
	void SetPlayKick(unsigned base, unsigned initOff, int ei)
	{
		playKickBase_ = base;
		playKickInitOff_ = initOff;
		playKickEi_ = ei ? 1 : 0;
	}
	void SetForcePlayEi(int v) { forcePlayEi_ = v ? 1 : 0; }
	void DirectPlayKick(unsigned addr, int ei);

	Ay_Cpu* Cpu() override { return cpu_; }

	uint8_t* Mem() override { return mem_; }

	CChip* SoundChip() override { return chip_; }



	uint8_t PortIn(uint16_t port) override;

	void PortOut(uint16_t port, uint8_t data) override;



	uint64_t CpuCycles() const { return cpuCycles_; }

	void AddCpuCycles(uint64_t n) { cpuCycles_ += n; }



	uint8_t cmd;

	uint8_t param;

	uint8_t song;

	int soundIrqMasked;

	int useRtc;

	int useVrtc;

	int opnaMode;

	/* 実効 Z80 Hz: 4000000 * max(1, clockmul) */
	int cpuHz_;



private:

	void FreeBanks();

	void StageBanks(CEmuZipFs* fs, const CEmuGameEntry* ge);

	void DeriveMicrocabinVdata(const CEmuGameEntry* ge);

	void BankCopyBgm(uint8_t songIndex);

	void SetSoundIrqPort(uint8_t data);

	/* PC-8801 テキスト窓（I/O 70h / 78h）: 8000-83FF の 1KB ビューを動かす */
	void SetTextWindow(uint8_t hi);



	uint8_t mem_[0x10000];

	Ay_Cpu* cpu_;

	CChip* chip_;

	int sampleRate_;

	uint64_t cpuCycles_;

	/* hoot 風 ioport ラッチ（既定 0）。未知ポートはここで RMW。
	   IN のオープンバス 0xFF は 0x32 の bit7 を永久に立て arcus2 が無音になる。
	   ポート 0x32 と 0xAA は ioPorts_[0x32] を共有。 */
	uint8_t ioPorts_[256];

	/* テキスト窓のベースページ（0x80=閉じ=素のメイン RAM）と、開いている間隠す 8000-83FF 実バイト */
	uint8_t textWinHi_;
	uint8_t textWinShadow_[0x400];

	int mdataAddr_;

	int mdataSize_;

	int mfileSize_;

	int vdataAddr_;

	int vfileSize_;

	int wolfteamMode_;

	/* ポート 0 バンクコピーは mucom88 のみ。KOEI PATCH は 0x5C/0x5D に IRQ オペコード
	   （POP AF / EI）を置く。dst ポインタと扱うと RAM が壊れる。 */
	int mucomBankCopy_;

	int mdataAddrDefaulted_;

	int packedKoei_;

	int initPc_;

	/* f_crisis 系: PATCH は EI しない。LoadRoms で一度武装 */
	int forcePlayEi_;

	/* gineiden: play 後、空 RTC 枠へ IM2 音源ベクタをミラー */
	int mirrorSoundToRtc_;

	/* gineiden: PATCH がクリアした OPN Timer B をホストが再武装 */
	int armGineidenTimer_;
	/* lizard88: play 後にホストが OPN Timer B を再武装 */
	int armLizardTimer_;
	/* 1942_88: ADEE LDIR は cmd=1 のドレインを長くする */
	int longPlayDrain_;
	/* yaksa PATCH2: カタログ ROM 名 — 再生添字は非 0 のまま */
	int yaksaPatch2_;
	/* navitune 系: code@mdata と bgm は同一イメージ。title fileOff → 曲 */
	int armNavituneTimer_;
	uint16_t naviSongAddr_; /* 絶対曲ヘッダ（mdata+fileOff） */

	/* rogueal: play 後にだけ RTC を許可（ブート LDIR と RTC の競合） */
	int deferRtcAfterPlay_;

	/* N88 thexder/bokosuka: DEMOM/MUSIC が LD (F304),HL する RTC ISR 番地 */
	unsigned n88RtcIsr_;
	unsigned n88RtcThrottleAddr_;

	/* hardrank SMD-88.sb2: I=$91 / vec04@9104 をプレーヤ ISR に保つ */
	int hardrankSb2_;

	/* Scheme OPNA 専用（PATCH@9000、BGM は port0 → C000） */
	int schemeMode_;

	/* hoot OldFalcomDriver: 0 なし、1 XANADU、2 XANADU2、3 ASTEKA2 */
	int falcomType_;

	/* 直接 CALL 再生: 基点（PLAY88/C000/PROG2）、任意の +init オフセット（Game Arts +6）、
	   基点 CALL 後に EI するか */
	unsigned playKickBase_;
	unsigned playKickInitOff_;
	int playKickEi_;

	unsigned char* bgmBank_[256];

	unsigned bgmBankSize_[256];

	/* Falcom type=prog（bgm とは別。xana2 は両方使う） */
	unsigned char* progBank_[256];

	unsigned progBankSize_[256];

	unsigned char* voiceBank_[256];

	unsigned voiceBankSize_[256];

};



void CEmuHardPc88SetActive(CHardPc88* hw);

