#include "StdAfx.h"
#include "cemu_driver_ac.h"
#include "../machine/cemu_m68k_bus.h"
#include <stdio.h>
#include "../machine/cemu_v35_bus.h"
#include "../machine/cemu_h6280_bus.h"
#include "../machine/cemu_h8_bus.h"
#include "../machine/cemu_hd63701_bus.h"
#include "../machine/cemu_irem_cpu_tables.h"
#include "../chip/cemu_chip_opm.h"
#include "../chip/cemu_chip_opl.h"
#include "../chip/cemu_chip_ay.h"
#include "../z80/Ay_Cpu.h"
extern "C" {
#include "../vendor/musashi/m68k.h"
#include "../vendor/v35/v35core.h"
#include "../vendor/h6280/h6280core.h"
#include "../vendor/h8/h8core.h"
#include "../vendor/hd63701/hd63701core.h"
#include "../vendor/mc6809/mc6809.h"
#include "../vendor/m6803/m6803.h"
#include "../vendor/m6502/m6502core.h"
#include "../vendor/m37710/m37702core.h"
}
#include "../machine/cemu_m37702_bus.h"
#include <string.h>
#include <stdlib.h>

/* Sega System16 / Capcom CPS1 音源 CPU は通常ラッチ+NMI/IRQ 待ち。メイン 68K が無いのでブート後に短いコマンド列を注入。
   先に 0x81+（shinobi）。Cotton は 0x10..0x27 のみ受ける — そのあと試す。 */
static const uint8_t kSys16TryCmds[] = {
	0x81, 0x82, 0x83, 0x84, 0x85, 0x01, 0x02, 0x03, 0x40, 0x41, 0x90, 0xa0,
	0x91, 0x92, 0xb0, 0xc0, 0xc5, 0xa3,
	0x12, 0x10, 0x1A, 0x22, 0x14, 0x20, 0x18, 0x24
};
/* CPS1 では 0xF0/0xFF を避ける（停止／フェード）。低 SE（BLAST 後無音）より 0x40+（ffight BGM）を先に。0x01 は遅く — ver2 で BLAST。 */
static const uint8_t kCps1TryCmds[] = {
	0x40, 0x41, 0x50, 0x42, 0x55, 0x57, 0x10, 0x12, 0x20, 0x30, 0x02, 0x03,
	0x04, 0x01, 0x80, 0x81
};
/* Capcom GNG BGM — avengers/commando/gunsmoke で持続するコードを先頭に。0x2b/0x23/0x1A は複数タイトルで AUDITION（平坦 32768）。 */
static const uint8_t kGngTryCmds[] = {
	0x21, 0x22, 0x28, 0x29, 0x25, 0x35, 0x31, 0x2c, 0x36, 0x33, 0x2d, 0x2e
};
/* Sega OutRun / After Burner BGM（AB では 0x81+ がしばしば SE） */
static const uint8_t kOutRunTryCmds[] = {
	0x84, 0x81, 0x82, 0x83, 0x85, 0x86, 0x87, 0x88
};
static const uint8_t kAburnerTryCmds[] = {
	0x92, 0x91, 0x95, 0x94, 0x93, 0x96, 0x90, 0x97
};
static const uint8_t kCpsQsTryCmds[] = {
	0x01, 0x02, 0x03, 0x04, 0x10, 0x11, 0x20, 0x21, 0x30, 0x40, 0x80, 0x81
};
static const uint8_t kKonamiTryCmds[] = {
	0x01, 0x02, 0x03, 0x10, 0x11, 0x12, 0x20, 0x21, 0x30, 0x40, 0x80, 0x81
};
static const uint8_t kNamcoTryCmds[] = {
	/* Sys12 C76: カタログ prefer（0x01/0x10）の多くは無音または平坦ドローン。0x20/0x08/0x30 は aquarush/ehrgeiz/golgo13 等で持続 BGM。
	   0x01 を早く: kaiunqz/mdhorse の持続テーマ。 */
	0x20, 0x08, 0x30, 0x01, 0x10, 0x18, 0x28, 0x40, 0x04, 0x02, 0x03, 0x80
};
static const uint8_t kSys18TryCmds[] = {
	0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x90, 0x91, 0x10, 0x12, 0x20, 0x22
};
/* Taito TC0140SYT は BGM を 0x01 から番号付け。0x00 は停止 */
static const uint8_t kTaitoTryCmds[] = {
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c,
	0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12
};
/* Irem M72/M84: BGM は 0x20+index（m99/bbmanw）または 0x30+（imgfight/loht）。低コードはしばしば mode/SE。matchit 用に 0x80+ も含む。 */
static const uint8_t kIremTryCmds[] = {
	0x74, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b,
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x40, 0x41, 0x50, 0x60,
	0x70, 0x71, 0x72, 0x73, 0x75, 0x80, 0x81, 0x82, 0x01, 0x0b, 0x0a, 0x02, 0x03, 0x04, 0x10
};
/* Irem M92: M72 と同じコマンドニブル族。BGM は 0x20 から */
static const uint8_t kM92TryCmds[] = {
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b,
	0x2c, 0x2d, 0x2e, 0x2f, 0x01, 0x02, 0x03, 0x04, 0x80, 0x81
};
/* Sega System1/2 の BGM コマンド — 持続テーマは 0x80+ を優先 */
static const uint8_t kSys1TryCmds[] = {
	0x81, 0x82, 0x83, 0x80, 0x84, 0x85, 0x86, 0x88, 0x01, 0x02, 0x03, 0x04,
	0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c
};
/* Konami Scramble / Time Pilot — 0x0B は既知の持続 BGM。GX400（gradius）は 0x40+。下の試行表は基板ごとに上書き。 */
static const uint8_t kKonamiAyTryCmds[] = {
	0x0b, 0x09, 0x0e, 0x0a, 0x08, 0x0c, 0x0d, 0x0f, 0x07, 0x06,
	0x10, 0x14, 0x1a, 0x20, 0x21, 0x01, 0x02, 0x03
};
/* GX400 ISR @0085 は latch==1（チャネル初期化）だけ受ける。他は RET NZ。カタログ 0x40+/0x80+ は 68k 側 id — ラッチへは 0x01 に写す。 */
static const uint8_t kGx400TryCmds[] = {
	0x01, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0x91, 0x92, 0x93, 0xa0
};
/* Technos Double Dragon 2 / China Gate / WWF（基板） */
static const uint8_t kDdragon2TryCmds[] = {
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c,
	0x0d, 0x0e, 0x0f, 0x20, 0x22
};
/* Taito flstory: MSM 群の曲を優先（arm 0x90 → C500 @08E7） */
static const uint8_t kFlstoryTryCmds[] = {
	0x15, 0x16, 0x18, 0x05, 0x03, 0x06, 0x08, 0x0e, 0x0f, 0x01, 0x04
};

static int CEmuAcSys16SeLabel(const wchar_t* s)
{
	if (!s || !s[0]) return 0;
	if (wcsstr(s, L"Voice") || wcsstr(s, L"VOICE") || wcsstr(s, L"voice"))
		return 1;
	if (wcsstr(s, L"SFX"))
		return 1;
	if (s[0] == L'S' && s[1] == L'E'
		&& (s[2] == 0 || s[2] == L' ' || (s[2] >= L'0' && s[2] <= L'9')))
		return 1;
	return 0;
}

static int CEmuAcCatalogCreditLabel(const wchar_t* s)
{
	if (!s || !s[0]) return 0;
	if (wcsstr(s, L"Credit") || wcsstr(s, L"CREDIT") || wcsstr(s, L"Coin"))
		return 1;
	return 0;
}

CDriverAc::CDriverAc()
	: hw_(NULL)
	, hostRate_(44100)
	, cpuHz_(4000000)
	, opmHz_(4000000)
	, booted_(0)
	, triggered_(0)
	, pinned_(0)
	, songCmd_(0x81)
	, songCmdWord_(0)
	, songCmdDword_(0)
	, opmResidual_(0)
	, rzOpmAcc_(0)
	, cpuAcc_(0)
	, cmdIndex_(0)
	, nextCmdAt_(0)
	, nextGngIrq_(0)
	, irqPaceAcc_(0)
	, irqPaceDue_(0)
	, irqPaceLive_(0)
	, alphaNmiBusy_(0)
	, k054539TimerState_(0)
	, k054539Residual_(0)
	, nextM72Nmi_(0)
	, m72FakeNmi_(0)
	, hasCpu_(1)
	, ms1_(0)
	, ms1Acc_(0)
	, m92_(0)
	, m92Acc_(0)
	, m92OpmRes_(0)
	, deco_(0)
	, decoAcc_(0)
	, decoChipRes_(0)
	, decoNextYmIrq_(0)
	, h8Board_(0)
	, h8Acc_(0)
	, m37702Board_(0)
	, m37702Acc_(0)
	, namcoM6809_(0)
	, namcoAcc_(0)
	, sys86_(0)
	, sys86Acc_(0)
	, sys86OciNeed_(0)
	, m62_(0)
	, m62Acc_(0)
	, sega68_(0)
	, sega68Acc_(0)
	, m92NoteOffSeen_(0)
	, m92ChannelPlayOff_(0)
	, m92NoteStuck_(0)
	, scratch_(NULL)
	, scratchFrames_(0)
	, heard_(0)
{
}

int16_t* CDriverAc::Scratch(int frames)
{
	if (frames <= 0) return NULL;
	if (scratchFrames_ < frames) {
		int16_t* p = (int16_t*)realloc(scratch_, (size_t)frames * 2 * sizeof(int16_t));
		if (!p) return NULL;
		scratch_ = p;
		scratchFrames_ = frames;
	}
	return scratch_;
}

CDriverAc::~CDriverAc()
{
	Close();
}

int CDriverAc::Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode)
{
	if (!hw || !ge || !fs) return 0;
	hw_ = (CHardAc*)hw;
	hostRate_ = hw_->SampleRate();
	cpuHz_ = hw_->cpuHz_ > 0 ? hw_->cpuHz_ : 4000000;
	opmHz_ = hw_->opmHz_ > 0 ? hw_->opmHz_ : 4000000;
	opmResidual_ = 0;
	rzOpmAcc_ = 0;
	cpuAcc_ = 0;
	booted_ = 0;
	triggered_ = 0;
	pinned_ = 0;
	cmdIndex_ = 0;
	nextGngIrq_ = 0;
	irqPaceAcc_ = 0;
	irqPaceDue_ = 0;
	irqPaceLive_ = 0;
	alphaNmiBusy_ = 0;
	/* カタログタイトルが曲を固定（コード 0 = 停止含む）。titlelist が無いときは基板既定と任意の試行表ハントへ。 */
	songCmdWord_ = (uint16_t)titleCode;
	songCmdDword_ = titleCode;
	if (ge && ge->titleCount > 0) {
		songCmd_ = (uint8_t)(titleCode & 0xff);
		songCmdWord_ = (uint16_t)titleCode;
		songCmdDword_ = titleCode;
		/* プレイリストが STOP／ボイス／空なら再生可能な BGM を優先 */
		if (hw_->board_ == CEMU_AC_BOARD_KONAMI_GX) {
			int bad = (titleCode == 0 || titleCode == 0x200u || (titleCode & 0xffu) == 0);
			if (!bad) {
				for (int i = 0; i < ge->titleCount; i++) {
					if (ge->title[i].code != titleCode) continue;
					if (CEmuAcGxTitleIsVoice(&ge->title[i])) bad = 1;
					break;
				}
			}
			if (bad) {
				const unsigned prefer = CEmuAcPickGxDefaultTitle(ge);
				if (prefer) {
					songCmdWord_ = (uint16_t)prefer;
					songCmd_ = (uint8_t)(prefer & 0xff);
				}
			}
			/* tkmmpzdm: Voice 0x53F は Opening 0x601 と同じ既定へ。キャラテーマ 0x101/0x102 は別のまま。 */
			if (ge->archive && !_stricmp(ge->archive, "tkmmpzdm")) {
				if (titleCode == 0x53Fu || titleCode == 0x60Eu
					|| titleCode == 0x603u) {
					songCmdWord_ = 0x101u;
					songCmd_ = 0x01u;
				} else if (titleCode == 0x601u) {
					songCmdWord_ = 0x102u;
					songCmd_ = 0x02u;
				}
			}
		}
		pinned_ = 1;
	} else if (titleCode && (titleCode & 0xff) != 0) {
		songCmd_ = (uint8_t)(titleCode & 0xff);
		pinned_ = 1;
	} else if (hw_->board_ == CEMU_AC_BOARD_RAIZING
		&& hw_->RaizingType() == 4) {
		/* Battle Bakraid はスクリプト表を 0 から引くので曲 0 は下の基板の停止ではなく通常の BGM 要求。これが無いと曲 1 として描画された。 */
		songCmd_ = 0x00;
		pinned_ = 1;
	} else if (hw_->board_ == CEMU_AC_BOARD_GNG)
		songCmd_ = 0x2b; /* Flatland BGM（曲） */
	else if (hw_->board_ == CEMU_AC_BOARD_ABURNER)
		songCmd_ = 0x92; /* Maximum Power（曲） */
	else if (hw_->board_ == CEMU_AC_BOARD_OUTRUN)
		songCmd_ = 0x85; /* Magical Sound Shower（曲） */
	else if (hw_->board_ == CEMU_AC_BOARD_HANGON
		&& ge && _stricmp(ge->subtype, "sharrier") == 0)
		songCmd_ = 0xad; /* テーマ — BGM は 0xa3..0xb9。0xe7 はプローブ途中で死ぬ */
	else if (hw_->board_ == CEMU_AC_BOARD_HANGON)
		songCmd_ = 0x9d; /* Hang-On メインテーマ */
	else if (hw_->board_ == CEMU_AC_BOARD_CPS_QS)
		songCmd_ = 0x01;
	else if (hw_->board_ == CEMU_AC_BOARD_CPS1)
		songCmd_ = 0x40;
	else if (hw_->board_ == CEMU_AC_BOARD_FLSTORY)
		/* 0x05 が C500（0x90）を武装 — MSM メロディ経路 @08E7。0x02 の arm 0x86 は無効 */
		songCmd_ = 0x05;
	else if (hw_->board_ == CEMU_AC_BOARD_TAITO_YM2610
		|| hw_->board_ == CEMU_AC_BOARD_TAITO_OPM
		|| hw_->board_ == CEMU_AC_BOARD_TAITO_SJ
		|| hw_->board_ == CEMU_AC_BOARD_SEGA_SYS1
		|| hw_->board_ == CEMU_AC_BOARD_TECHNOS_DDRAGON2
		|| hw_->board_ == CEMU_AC_BOARD_TECMO16
		|| hw_->board_ == CEMU_AC_BOARD_RAIZING
		|| hw_->board_ == CEMU_AC_BOARD_KONAMI_K7232
		|| hw_->board_ == CEMU_AC_BOARD_KONAMI_HCASTLE
		|| hw_->board_ == CEMU_AC_BOARD_ALPHA68K2
		|| hw_->board_ == CEMU_AC_BOARD_TERRACRE
		|| hw_->board_ == CEMU_AC_BOARD_ROBOKID
		|| hw_->board_ == CEMU_AC_BOARD_BATTLANTIS)
		songCmd_ = 0x01;
	else if (hw_->board_ == CEMU_AC_BOARD_KONAMI_SCRAMBLE
		|| hw_->board_ == CEMU_AC_BOARD_KONAMI_TIMEPLT)
		/* Scramble 0x01-0x05 はチャネルを消す短いセットアップ stub。0x0B は音量／音程が動く持続テーマ（ピーク変動）。GX400 は 0x40+ — 既定を 0x0B にしない。 */
		songCmd_ = 0x0b;
	else if (hw_->board_ == CEMU_AC_BOARD_KONAMI_GX400)
		songCmd_ = 0x01; /* ISR は latch==1（チャネル初期化）のときだけ武装 */
	else if (hw_->board_ == CEMU_AC_BOARD_NAMCO_C352)
		songCmd_ = 0x20; /* Sys12: 大半の C76 で 0x20 が持続 BGM */
	else if (hw_->board_ == CEMU_AC_BOARD_IREM_M72)
		songCmd_ = 0x30;
	else
		songCmd_ = 0x81;
	/* カタログが scramble stub 0x01-0x05 を固定することがある（チャネル即クリア）。持続 BGM id へ寄せ、プローブが動くピークで PLAY を見る。GX400 は別コマンド空間（0x40+）。ここでは触らない。 */
	if ((hw_->board_ == CEMU_AC_BOARD_KONAMI_SCRAMBLE
		|| hw_->board_ == CEMU_AC_BOARD_KONAMI_TIMEPLT)
		&& songCmd_ >= 0x01 && songCmd_ <= 0x05) {
		songCmd_ = 0x0b;
		pinned_ = 0; /* 0x0B がクローンで無音なら試行表を許す */
	}
	/* GX400: 音源 ROM ISR はラッチ 0x01 のみ。カタログ BGM id は診断用に songCmdWord_ に残すが、ラッチは初期化ストローブ。 */
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_GX400) {
		if (songCmd_ != 0x01)
			songCmd_ = 0x01;
	}
	/* flstory: 0x02 は 0x86 で武装（01F0 無視）。MSM 枠（0x90→C500）を優先。0x15 は聞こえる（WEAK だった）ので残す。このセットで 0x05 は無音。 */
	if (hw_->board_ == CEMU_AC_BOARD_FLSTORY) {
		if (hw_->MsisaacMap()) {
			/* カタログ Credit 0x15 / Demo 0x1B / Main 0x22。Enemy vs Ranking */
			if (songCmd_ == 0x15u || songCmd_ == 0x1bu)
				songCmd_ = 0x30u;
			else if (songCmd_ == 0x22u)
				songCmd_ = 0x24u;
		} else if (hw_->NycaptorMap()) {
			/* AY×2+MSM。カタログ 0x26/0x20/0x6B は薄いまたはワンショット */
			if (ge && ge->archive && _stricmp(ge->archive, "wyvernf0") == 0) {
				if (songCmd_ == 0x00u || songCmd_ == 0x20u) {
					songCmd_ = 0x0bu;
					songCmdWord_ = 0x0bu;
				} else if (songCmd_ == 0x6bu) {
					songCmd_ = 0x1fu;
					songCmdWord_ = 0x1fu;
				}
			} else if (ge && ge->archive && _stricmp(ge->archive, "cyclshtg") == 0) {
				if (songCmd_ == 0x22u || songCmd_ == 0x26u) {
					songCmd_ = (songCmd_ == 0x26u) ? 0x24u : 0x25u;
					songCmdWord_ = songCmd_;
				}
			}
		} else {
		const int flClone = (ge && ge->archive
			&& (_stricmp(ge->archive, "40love") == 0
				|| _stricmp(ge->archive, "fieldday") == 0
				|| _stricmp(ge->archive, "victnine") == 0)) ? 1 : 0;
		if (!flClone) {
			static const uint8_t kOk[] = {
				0x05, 0x03, 0x06, 0x08, 0x0e, 0x0f, 0x01, 0x04, 0x15, 0x16, 0x18,
				0x4c, 0x10, 0x21, 0x08, 0x26, 0x27, 0x28, 0x2d, 0x2e, 0x2f
			};
			int ok = 0;
			for (int i = 0; i < (int)_countof(kOk); i++) {
				if (songCmd_ == kOk[i]) { ok = 1; break; }
			}
			if (!ok || songCmd_ == 0x02) {
				songCmd_ = 0x15;
				pinned_ = 0;
			}
			/* onna34ro: 0x4C/0x15 は静か。0x10 が MSM+AY で持続 */
			if (ge && ge->archive && _stricmp(ge->archive, "onna34ro") == 0)
				songCmd_ = 0x10;
		}
		/* クローン: カタログ BGM を残す（40love 0x11、fieldday 0x22、victnine 0x26）。これらでは flstory 0x15 は曲ではない。ピンを外すとハントする。カタログ pick 2/3 はしばしばワンショット — ループ MSM BGM へ付け替え。 */
		if (ge && ge->archive && _stricmp(ge->archive, "40love") == 0) {
			if (songCmd_ == 0x11u) { songCmd_ = 0x16u; songCmdWord_ = 0x16u; }
			else if (songCmd_ == 0x12u) { songCmd_ = 0x17u; songCmdWord_ = 0x17u; }
		} else if (ge && ge->archive && _stricmp(ge->archive, "fieldday") == 0) {
			if (songCmd_ == 0x22u) { songCmd_ = 0x25u; songCmdWord_ = 0x25u; }
			else if (songCmd_ == 0x23u) { songCmd_ = 0x26u; songCmdWord_ = 0x26u; }
		} else if (ge && ge->archive && _stricmp(ge->archive, "victnine") == 0) {
			if (songCmd_ == 0x26u) { songCmd_ = 0x08u; songCmdWord_ = 0x08u; }
			else if (songCmd_ == 0x27u) { songCmd_ = 0x28u; songCmdWord_ = 0x28u; }
		}
		}
	}
	/* CPS1 カタログはしばしば先頭が 0xF0/0xFF（停止／フェード）（ffight/forgottn/sf2ce）。仮の 0x40。0x01 は version-2（ffight）で BLAST し heard_ が立ちハントが復帰しない。LoadRoms 後に精緻化。 */
	if (hw_->board_ == CEMU_AC_BOARD_CPS1
		&& (songCmd_ == 0x00 || songCmd_ >= 0xf0)) {
		songCmd_ = 0x40;
		pinned_ = 0;
	}
	/* カタログが STOP を先頭にすることがある — ピンが本物のカタログタイトルでないときだけ AB 既知 BGM 帯を優先（voice/SFX はしばしば 0xA3+／十進 163+ で、0x92 Maximum Power へ写してはいけない）。 */
	if (hw_->board_ == CEMU_AC_BOARD_ABURNER && ge
		&& _stricmp(ge->subtype, "aburner") == 0 && !pinned_) {
		const uint8_t c = songCmd_;
		if (c < 0x90 || c > 0x97)
			songCmd_ = 0x92;
	}
	/* G-LOC / Rail Chase: Title 0x30 と Credit 0x33 は mute。ステージ BGM 0x88 vs 0x84 */
	if (hw_->board_ == CEMU_AC_BOARD_ABURNER && ge && ge->archive
		&& _stricmp(ge->archive, "rchase") == 0) {
		if (songCmd_ == 0x30u) {
			songCmd_ = 0x88u;
			songCmdWord_ = 0x88u;
		} else if (songCmd_ == 0x33u) {
			songCmd_ = 0x84u;
			songCmdWord_ = 0x84u;
		}
	}
	/* Model 3 Ski Champ: BGM 01 はワンショット。BGM 04 vs pick-2 BGM 02 */
	if (hw_->board_ == CEMU_AC_BOARD_SEGA_SCSP && ge && ge->archive
		&& _stricmp(ge->archive, "skichamp") == 0 && songCmd_ == 0x01u) {
		songCmd_ = 0x04u;
		songCmdWord_ = 0x04u;
	}
	/* hoot リストが BGM #00..#n の Model 2A 残り: その id は曲添字であり MIDI 語ではない。v3 CRX_drv（と vf2/pltkids カタログ）は A0 10 xx、すなわち 0x10xx。A0 00 xx はコマンド 0 のサブ（0x04 無視、0x07 音色初期化）。0x00 は停止。
	   無音／歯抜けのカタログ先頭は 2 つの別ループ（またはループ＋正直なジングル）id へ — 単一ラッチへではない。 */
	if (hw_->board_ == CEMU_AC_BOARD_SEGA_SCSP && ge && ge->archive
		&& !hw_->SegaM1Audio()) {
		const char* ar = ge->archive;
		const int generic = (_stricmp(ar, "dynabb") == 0
			|| _stricmp(ar, "motoraid") == 0
			|| _stricmp(ar, "segawski") == 0
			|| _stricmp(ar, "zerogun") == 0);
		if (generic) {
			unsigned lo = songCmdWord_ ? (songCmdWord_ & 0xffu) : (unsigned)songCmd_;
			if (_stricmp(ar, "dynabb") == 0) {
				if (lo == 0u || lo == 2u) lo = 0x05u; /* 固有 STOPS vs 0x01 BGM */
			} else if (_stricmp(ar, "motoraid") == 0) {
				if (lo == 0u || lo == 2u) lo = 0x04u;
			} else if (_stricmp(ar, "segawski") == 0) {
				if (lo <= 2u || lo == 5u)
					lo = (lo & 1u) ? 0x03u : 0x04u;
			}
			songCmdWord_ = (uint16_t)(0x1000u | (lo & 0xffu));
			songCmd_ = (uint8_t)(lo & 0xffu);
		}
		if (_stricmp(ar, "rchase2") == 0) {
			unsigned lo = songCmdWord_ ? (songCmdWord_ & 0xffu) : (unsigned)songCmd_;
			if (lo == 0x15u || lo == 0x20u)
				lo = 0x00u; /* Advertise/停止 → Opening、対 0x0C Intermezzo */
			songCmd_ = (uint8_t)lo;
			songCmdWord_ = (uint16_t)lo;
		}
	}
	/* K054539（bucky/moomesa 等）: 曲表エントリは 14 バイト。[0]==0 かつ [1] の bit6 クリアは mute/停止行（bucky prefer 0xC0）。単チップセットだけがその表配置。dual K054539 mystwarr 族は BGM 帯全体が 0xCD-0xEC なので、この強制は全曲を 0x01 に静かに置換した。 */
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_PCM && hw_->PcmKind() == 4
		&& !hw_->KonamiPcm2() && songCmd_ == 0xc0)
		songCmd_ = 0x01;
	/* OutRun: 停止／Credit／SE prefer を Magical Sound Shower（0x85）へ。Credit 0x84 はワンショット BLAST。Passing Breeze（0x81）は無音なので Credit を Splash Wave（0x82）へ固定し 0x85 は触らない。
	   toutrun/shangon/pdrift には適用しない — GP Rider 0x94 Qualify と Enduro Racer 0x81 Main Theme が OutRun 曲 id へ畳まれた。 */
	if (hw_->board_ == CEMU_AC_BOARD_OUTRUN && ge && ge->archive
		&& _stricmp(ge->archive, "outrun") == 0) {
		const uint8_t c = songCmd_;
		if (c == 0x84 || c == 0x81)
			songCmd_ = 0x82;
		else if (c == 0x94 || c == 0x9e || c == 0x9f || c == 0xff || c == 0x7f
			|| c < 0x81)
			songCmd_ = 0x85;
	}
	/* outrunm: このセットで Passing Breeze（0x81）は無音。Magical Sound Shower へ固定し、偶数／奇数 0x81/0x82 が 2 本の別 BGM になるようにする。 */
	if (hw_->board_ == CEMU_AC_BOARD_OUTRUN && ge && ge->archive
		&& _stricmp(ge->archive, "outrunm") == 0) {
		const uint8_t c = songCmd_;
		if (c == 0x81 || c == 0x84)
			songCmd_ = 0x85;
	}
	/* 古典 Tecmo: カタログ pick 2,3 はしばしば Credit／ワンショットジングル */
	if (hw_->board_ == CEMU_AC_BOARD_TECMO16 && ge && ge->archive) {
		const uint8_t c = songCmd_;
		if (_stricmp(ge->archive, "rygar") == 0 && c == 0x3fu)
			songCmd_ = 0x34u; /* Get An Indra ワンショット → メイン BGM（イントロ無し） */
		else if (_stricmp(ge->archive, "backfirt") == 0) {
			if (c == 0x09u)
				songCmd_ = 0x23u; /* Credit 無音 → BGM1 */
			else if (c == 0x21u)
				songCmd_ = 0x24u; /* Start interval 無音 → BGM2 */
		} else if (_stricmp(ge->archive, "tbowl") == 0) {
			if (c == 0x01u)
				songCmd_ = 0x32u; /* Credit → 青チーム攻撃 */
			else if (c == 0x02u)
				songCmd_ = 0x33u; /* Credit → 赤チーム攻撃 */
		} else if (_stricmp(ge->archive, "wc90") == 0) {
			if (c == 0x06u)
				songCmd_ = 0x23u; /* Credit → BGM1（割当） */
			else if (c == 0x12u)
				songCmd_ = 0x24u; /* Start interval → BGM2（割当） */
		}
	}
	/* Taito B YM2203（masterw/viofight）: カタログ／ホストはしばしば SE／ハンドシェイク（0x01-0x04）を固定。持続 BGM はアーカイブ固有。 */
	if (hw_->board_ == CEMU_AC_BOARD_TAITO_OPM && hw_->MainIsYm2203() && ge) {
		const uint8_t c = songCmd_;
		const int kikikai = (ge->subtype[0] && _stricmp(ge->subtype, "kikikai") == 0)
			|| (ge->archive[0] && (_stricmp(ge->archive, "kikikaik") == 0
				|| _stricmp(ge->archive, "kikikai") == 0));
		const int darius = (ge->subtype[0] && _stricmp(ge->subtype, "darius") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "darius") == 0);
		if (c < 0x05 && !kikikai && !darius && hw_->TaitoOpmMap() < 3) {
			if (_stricmp(ge->subtype, "viofight") == 0
				|| (ge->archive[0] && _strnicmp(ge->archive, "viofight", 8) == 0))
				songCmd_ = 0x08;
			else
				songCmd_ = 0x05; /* masterw / champwr / tetrista 系統 */
			/* pinned_ を残す — クリアすると viofight が直接 0x08 PLAY に対し WEAK */
		} else if (ge->archive[0] && _stricmp(ge->archive, "viofight") == 0
			&& c == 0x0fu) {
			songCmd_ = 0x08; /* Credit → Theme 1 vs Character Select 0x11（割当） */
		} else if (ge->archive[0] && _stricmp(ge->archive, "champwr") == 0
			&& c == 0x08u) {
			songCmd_ = 0x1fu; /* Player Select mute → Ending vs Title 0x07（割当） */
		} else if (ge->archive[0] && _stricmp(ge->archive, "masterw") == 0) {
			if (c == 0x07)
				songCmd_ = 0x09; /* Desertion 無音 → Act 2 BGM */
			else if (c == 0x08)
				songCmd_ = 0x0b; /* Briefing 薄い → Act 3 BGM */
		} else if (darius && (c == 0x37u || c == 0x3au)) {
			/* カタログ BGM 01/02 枠は無音。INORGANIC BEAT vs CAPTAIN NEO */
			songCmd_ = 0x2eu;
		} else if (kikikai && c == 0x05u) {
			songCmd_ = 0x07u; /* start demo 短い → BOSS vs メインテーマ 0x06 */
		} else if (hw_->TaitoOpmMap() == 3) {
			/* tokio: Credit 0x12 はワンショット。Boss 0x05 vs Main 0x28。bublboblp カタログ 0x06 は seq=1 ジングル。 */
			if (c == 0x12 || c == 0x00)
				songCmd_ = 0x05;
			else if (ge->archive[0] && _stricmp(ge->archive, "bublboblp") == 0
				&& c == 0x06)
				songCmd_ = 0x05;
		} else if (hw_->TaitoOpmMap() == 4) {
			/* bublbobl: Title SFX 0x2D / Credit 0x34 → Intro vs Main Theme（割当） */
			if (c == 0x2d)
				songCmd_ = 0x07;
			else if (c == 0x34)
				songCmd_ = 0x30;
		} else if (hw_->TaitoOpmMap() == 6) {
			/* lkage pick 3 は Miss 0x0A（ワンショット）。Stage Clear vs Main */
			if (c == 0x0a || c == 0x04)
				songCmd_ = 0x16;
		} else if (hw_->TaitoOpmMap() == 7
			&& ge->archive[0] && _stricmp(ge->archive, "kageki") == 0) {
			/* 0x06 は自分を消すステージ相対リマップ。0x07 Round Clear は 3 窓ジングル。Round 2 / Round 3 ループ。 */
			if (c == 0x06)
				songCmd_ = 0x08;
			else if (c == 0x07 || c == 0x09)
				songCmd_ = 0x0b;
		}
	}
	if (hw_->HalleysAy() && ge && ge->archive[0]) {
		/* 0x06 Contact / 0x19 Game Start はワンショット。Main / Stage 2 ループ */
		if (_stricmp(ge->archive, "halleys") == 0 && songCmd_ == 0x06)
			songCmd_ = 0x05;
		else if (_stricmp(ge->archive, "benberob") == 0 && songCmd_ == 0x19)
			songCmd_ = 0x25;
	}
	if (hw_->Cop01Ay()) {
		/* Falldown 0x4B はワンショット。Boss vs Main */
		if (songCmd_ == 0x4b)
			songCmd_ = 0x44;
	}
	if (hw_->MagmaxAy()) {
		/* Credit 0x0C / Attract 0x4D はワンショット。Ground BGM vs Start+BGM */
		if (songCmd_ == 0x0cu || songCmd_ == 0x4du) {
			songCmd_ = 0x41u;
			songCmdWord_ = 0x41u;
		}
	}
	if (hw_->BombjackAy()) {
		/* Credit 0x10 / Title 0x20。BGM2 vs BGM1（割当） */
		if (songCmd_ == 0x10u || songCmd_ == 0x20u) {
			songCmd_ = 0x23u;
			songCmdWord_ = 0x23u;
		}
	}
	if (hw_->SolomonAy()) {
		/* Credit 0x18 / Start 0x38,0x34。BGM1 vs BGM2（割当） */
		if (songCmd_ == 0x18u || songCmd_ == 0x38u) {
			songCmd_ = 0x1au;
			songCmdWord_ = 0x1au;
		} else if (songCmd_ == 0x34u) {
			songCmd_ = 0x1bu;
			songCmdWord_ = 0x1bu;
		}
	}
	if (hw_->PbactionAy()) {
		/* カタログ Credit 0x02 は $1C4F の 3ch ジングル。Slot vs Main */
		if (songCmd_ == 0x02u)
			songCmd_ = 0x11u;
	}
	if (hw_->ChaknpopAy()) {
		/* Pick 2/3 は SFX 0x39/0x38。0x21/0x15/0x17 は停止。0x19 と 0x1F はループ */
		if (songCmd_ == 0x39u)
			songCmd_ = 0x19u;
		else if (songCmd_ == 0x38u)
			songCmd_ = 0x1fu;
	}
	/* Taito F2 YM2610: カタログ Credit/Coin/GOAL はワンショット */
	if (hw_->board_ == CEMU_AC_BOARD_TAITO_YM2610 && ge && ge->archive) {
		const char* ar = ge->archive;
		const uint8_t c = songCmd_;
		if (!_stricmp(ar, "finalb") && c == 0x65u)
			songCmd_ = 0x11u; /* CREDIT → Title vs Fighter Select 0x0A（割当） */
		else if (!_stricmp(ar, "deadconx") && c == 0x72u)
			songCmd_ = 0x5fu; /* CREDIT → Stage 1 vs Select 0x63（割当） */
		else if (!_stricmp(ar, "footchmp") && c == 0x99u)
			songCmd_ = 0x09u; /* GOAL → Team Select vs Win 0x0B（割当） */
		else if (!_stricmp(ar, "recordbr") && c == 0x10u)
			songCmd_ = 0x07u; /* Coin → Title vs Name Set 0x08（割当） */
		else if (!_stricmp(ar, "pwheelsj") && c == 0x40u)
			songCmd_ = 0x41u; /* Credit STOPS → Title vs Race Select 0x42（割当） */
		else if (!_stricmp(ar, "nightstr") && c == 0x17u)
			songCmd_ = 0x3fu; /* Start STOPS → Urban Trail vs Intro ピン */
		else if (!_stricmp(ar, "nightstr") && c == 0x5eu)
			songCmd_ = 0x3eu; /* Introduction 無音 → Trance Parlent */
	}
	if (hw_->board_ == CEMU_AC_BOARD_ROBOKID && ge && ge->archive
		&& _stricmp(ge->archive, "robokid") == 0 && songCmd_ == 0x28u)
		songCmd_ = 0x30u; /* Goal STOPS → BGM B vs Main Theme 0x12（割当） */
	/* GNG 系: アーカイブ固有 BGM。カタログ prefer はしばしば SE/AUDITION（avengers 0x23、gunsmoke 0x1F→旧 0x2b、commando 0x1A）。 */
	if (hw_->board_ == CEMU_AC_BOARD_GNG && ge) {
		if (hw_->GngGaidenMap()) {
			/* Tecmo gaiden: カタログ先頭は Credit 0x01 / 0x10 / 0x42。タイトルリストの最初のステージ BGM を優先。 */
			const uint8_t c = songCmd_;
			int credit = 0;
			for (int i = 0; i < ge->titleCount; i++) {
				if ((uint8_t)(ge->title[i].code & 0xff) != c)
					continue;
				credit = CEmuAcCatalogCreditLabel(ge->title[i].label);
				break;
			}
			if (c == 0x00 || (c == 0x01 && credit)) {
				uint8_t best = 0x02;
				for (int i = 0; i < ge->titleCount; i++) {
					const uint8_t t = (uint8_t)(ge->title[i].code & 0xff);
					if (t == 0x00 || t == 0x01 || t == 0x10 || t == 0x42)
						continue;
					if (t >= 0x0a && t <= 0x1f) continue; /* SE／ボイス帯 */
					best = t;
					songCmdWord_ = (uint16_t)ge->title[i].code;
					break;
				}
				songCmd_ = best;
			} else if (c == 0x42) {
				/* Game Start ジングル — Round 2 BGM であり Round 1（pick 3）ではない */
				songCmd_ = 0x04;
				songCmdWord_ = 0x04;
			}
		} else {
			const char* ar = ge->archive;
			const uint8_t c = songCmd_;
			if (ar && _strnicmp(ar, "gunsmoke", 8) == 0) {
				if (c == 0x1f || c == 0x55)
					songCmd_ = 0x25;
			} else if (ar && _strnicmp(ar, "avenger", 7) == 0) {
				if (c == 0x23)
					songCmd_ = 0x2a;
				else if (c == 0x20)
					songCmd_ = 0x2b;
			} else if (ar && _strnicmp(ar, "commando", 8) == 0) {
				if (c == 0x1a)
					songCmd_ = 0x21;
			} else {
				const uint8_t c = songCmd_;
				/* 0x23/0x2b は複数基板で平坦クリップ。gng 0x35 を残す */
				if (c < 0x20 || c == 0x23 || c == 0x2b || (c > 0x3a && c != 0x35)) {
					songCmd_ = 0x21;
					pinned_ = 0;
				}
			}
		}
	}
	/* Sega System1: カタログはしばしば短い SE／BLAST ワンショットを固定しプローブ途中で死ぬ（4dwarrio 0x90、tokisens 0x10 等）。持続 0x81/0x82 を優先。既知 PLAY prefer（choplift 0xAB、imsorry 0xB8 等）は触らない。 */
	if (hw_->board_ == CEMU_AC_BOARD_SEGA_SYS1 && ge) {
		const uint8_t c = songCmd_;
		/* 0x90 は spatter で持続。0x87 は 4dwarrio の BGM 2 であり SE ではない。0x97 は raflesia で Title BGM、pitfall2 で Credit — 全体 SE 扱いしない。 */
		const int bad = (c == 0x91 || c == 0x95 || c == 0xb3);
		if (bad) {
			uint8_t best = 0;
			uint16_t bestW = 0;
			int bestSc = 999;
			for (int i = 0; i < ge->titleCount; i++) {
				const uint8_t t = (uint8_t)(ge->title[i].code & 0xff);
				if (t == c || t == 0x00 || t == 0x10 || t == 0x87 || t == 0x97
					|| t == 0xb3 || t == 0x8f || t == 0x91 || t == 0x95)
					continue;
				int sc = 50;
				if (t == 0x81 || t == 0x82 || t == 0x90) sc = 5;
				else if (t >= 0x80 && t <= 0x8e) sc = 10;
				else if (t >= 0x01 && t <= 0x0f) sc = 20;
				if (sc < bestSc) {
					bestSc = sc;
					best = t;
					bestW = (uint16_t)ge->title[i].code;
				}
			}
			if (!best) {
				best = (ge->archive[0] && _stricmp(ge->archive, "tokisens") == 0)
					? (uint8_t)0x01 : (uint8_t)0x81;
			}
			songCmd_ = best;
			if (bestW) songCmdWord_ = bestW;
		}
		/* 4dwarrio: 0x90 Credit vs 0x81 Ready。suprloco: 0x90 vs 0x81 Main。2 本目の BGM を優先し pick 2/3 が両方 0x81 にならないようにする。 */
		else if (c == 0x90 && ge->archive[0]
			&& (_stricmp(ge->archive, "4dwarrio") == 0
				|| _stricmp(ge->archive, "suprloco") == 0)) {
			songCmd_ = 0x82;
		} else if (ge->archive[0] && _stricmp(ge->archive, "tokisens") == 0) {
			if (c == 0x10)
				songCmd_ = 0x02;
			else if (c == 0x01)
				songCmd_ = 0x02; /* Start ジングルは死ぬ。Main Theme vs Ready 0x0b */
		} else if (c == 0x81 && ge->archive[0]
			&& _stricmp(ge->archive, "swat") == 0) {
			songCmd_ = 0x85; /* Round Clear ワンショット → 別 Main Theme */
		}
	}
	/* System18: cltchitr 0x83/0x84 はワンショット。BGM 01 vs pick-2 BGM 02 */
	if (hw_->board_ == CEMU_AC_BOARD_SYS18 && ge && ge->archive
		&& _stricmp(ge->archive, "cltchitr") == 0 && songCmd_ == 0x83u)
		songCmd_ = 0x81u;
	/* Haunted Castle: Credit 0x45 は無音。Stage 1 vs Introduction 0x56 */
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_HCASTLE && ge && ge->archive
		&& _stricmp(ge->archive, "hcastle") == 0 && songCmd_ == 0x45u)
		songCmd_ = 0x52u;
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_K7232 && ge && ge->archive) {
		const char* ar = ge->archive;
		if (!_stricmp(ar, "88games")) {
			if (songCmd_ == 0xd3u)
				songCmd_ = 0x40u; /* Voice SILENT → Attract vs Coin ピン */
			else if (songCmd_ == 0x22u)
				songCmd_ = 0x41u; /* Coin STOPS → Name Entry（割当） */
		}
	}
	/* CPS1 QSound: Punisher Title (2) 0x14 はワンショット。Stage 2 vs Title 0x04 */
	if (hw_->board_ == CEMU_AC_BOARD_CPS_QS && ge && ge->archive
		&& _stricmp(ge->archive, "punisher") == 0 && songCmd_ == 0x14u) {
		songCmd_ = 0x01u;
		songCmdWord_ = 0x01u;
	}
	/* CPS2 SIMM: choko カタログ 0x01/0x02 は mute／ワンショット。BGM 0x04 vs 0x05 */
	if (hw_->board_ == CEMU_AC_BOARD_CPS_QS && ge && ge->archive
		&& _stricmp(ge->archive, "choko") == 0) {
		if (songCmd_ == 0x01u) {
			songCmd_ = 0x04u;
			songCmdWord_ = 0x04u;
		} else if (songCmd_ == 0x02u) {
			songCmd_ = 0x05u;
			songCmdWord_ = 0x05u;
		}
	}
	/* Crazy Climber 2: pick 3 は [Voice] 0x23（DAC、FM 無音）。Mambo 0x39 vs Samba 0x2E がループ YM3812 テーマ 2 本。Credit 0x01 も同じ。 */
	if (hw_->board_ == CEMU_AC_BOARD_TERRACRE && hw_->TerracreMap() == 2
		&& ge && ge->archive && _stricmp(ge->archive, "cclimbr2") == 0) {
		if (songCmd_ == 0x01 || songCmd_ == 0x23)
			songCmd_ = 0x39;
	}
	/* Toaplan1: twincobr カタログ 0x25 は短い SE。0x08/0x12 は持続。snowbros Kaneko I/O は 0x20+ BGM id — それらを 0x12 へ畳まない。slapfght dual AY: カタログ id は C800 コマンド。Credit ワンショット（alcon 0x1F / getstar 0x26）はループ BGM へ写し pick 2,3 が PLAY。 */
	if (hw_->board_ == CEMU_AC_BOARD_TOAPLAN1 && hw_->SlapfghtAy()) {
		if (ge && ge->archive) {
			if (_stricmp(ge->archive, "alcon") == 0) {
				if (songCmd_ == 0x1f || songCmd_ == 0x00)
					songCmd_ = 0x15;
			} else if (_stricmp(ge->archive, "getstar") == 0) {
				if (songCmd_ == 0x26 || songCmd_ == 0x1f)
					songCmd_ = 0x02;
			} else if (_stricmp(ge->archive, "tigerh") == 0) {
				if (songCmd_ == 0x0e || songCmd_ == 0x18)
					songCmd_ = 0x01;
			}
		}
	} else if (hw_->board_ == CEMU_AC_BOARD_TOAPLAN1 && !hw_->ToaplanKaneko()) {
		const uint8_t c = songCmd_;
		if (c == 0x25 || c == 0x01 || c == 0x20 || c == 0x10) {
			songCmd_ = 0x12;
			pinned_ = 0;
		}
	}
	/* m99 M72（YM@40）: BGM は 0x20+index。カタログはしばしば生 index/SE */
	if (hw_->board_ == CEMU_AC_BOARD_IREM_M72 && hw_->M72IoAlt()
		&& songCmd_ > 0 && songCmd_ < 0x20)
		songCmd_ = (uint8_t)(0x20 + (songCmd_ & 0x1fu));
	/* Sys16B: カタログ先頭はしばしば Stop/Credit/SFX — 最初の BGM 風タイトルを優先（goldnaxe 0x9C credit、cotton 0x01 stop）。曲ハントではない: Open 時の固定カタログ 1 パス。mute/弱いピン（0xC5/0xD7/0xFF）はステージ BGM があれば写す（0xC0-C2、0xD0、低バイト）。
	   0x90-0x95 は 5358 スポーツ（aceattac/suprleag）の BGM。0xA0-0xAF は sonicbom 系。0x96-0x9F はタイトルデモ（hwchamp）。0x96-0xBF を一括拒否しない — suprleag/sonicbom がボイス SE バイトへ写った。 */
	if (hw_->board_ == CEMU_AC_BOARD_SYS16B && ge && ge->titleCount > 0) {
		const uint8_t c = songCmd_;
		int hasStage = 0, has90 = 0, hasA = 0;
		int curSe = 0;
		for (int i = 0; i < ge->titleCount; i++) {
			const uint8_t t = (uint8_t)(ge->title[i].code & 0xff);
			const int se = CEmuAcSys16SeLabel(ge->title[i].label);
			if (t == c) curSe = se;
			if (se) continue;
			if (t >= 0xc0 && t < 0xf0) hasStage = 1;
			if (t >= 0x90 && t <= 0x95) has90 = 1;
			if (t >= 0xa0 && t <= 0xaf) hasA = 1;
		}
		/* Stop/Credit/mute だけリマップ。hasStage 時に 0x90-0xBF を 0xC0 へ畳まない（aliensyn/timescan/goldnaxe が SAMESONG）。0xC5 は goldnaxe で Player Select BGM、bayroute で TITLE であり mute ではない。0x91 は 5358 スポーツと altbeast の BGM であり万能 Credit ではない。Speech 0x02-0x3F はカタログがそうラベルするか、高帯 BGM 表がありこの pick 自身が Voice/SFX のときだけ SE。 */
		const int bad = (c == 0x00 || c == 0x01 || c == 0xff || c == 0x9c
			|| c == 0xd7
			|| ((c == 0x9b || c == 0xad) && has90)
			|| (curSe && (has90 || hasA) && c >= 0x02 && c < 0x40));
		if (bad) {
			uint8_t best = 0;
			uint16_t bestW = 0;
			int bestSc = 999;
			for (int i = 0; i < ge->titleCount; i++) {
				const uint8_t t = (uint8_t)(ge->title[i].code & 0xff);
				if (t == 0x00 || t == 0x01 || t == 0xff || t == 0x9c || t == 0xc5
					|| t == 0xd7 || t == 0x91)
					continue;
				if (t >= 0x02 && t <= 0x26) continue;
				if (t >= 0x27 && t < 0x40) continue;
				if (hasStage && t >= 0x90 && t < 0xc0) continue;
				if ((t == 0x9b || t == 0xad) && has90) continue;
				int sc = 50;
				if (t == 0xc0 || t == 0xc1 || t == 0xc2 || t == 0xd0) sc = 5;
				else if (t >= 0xc0 && t < 0xf0) sc = 10;
				else if (t >= 0x90 && t <= 0x95) sc = 12;
				else if (t >= 0xa0 && t <= 0xaf) sc = 14;
				else if (t >= 0x96 && t <= 0x9f) sc = 16;
				else if (t >= 0x80 && t < 0x90) sc = 25;
				if (sc < bestSc) {
					bestSc = sc;
					best = t;
					bestW = (uint16_t)ge->title[i].code;
				}
			}
			if (best) {
				songCmd_ = best;
				songCmdWord_ = bestW;
			}
		}
		if (ge->archive) {
			const char* ar = ge->archive;
			if (_stricmp(ar, "exctleag") == 0 && c == 0x91u) {
				songCmd_ = 0x93u;
				songCmdWord_ = 0x93u;
			} else if (_stricmp(ar, "sjryuko") == 0 && c == 0x92u) {
				songCmd_ = 0x94u;
				songCmdWord_ = 0x94u;
			} else if (_stricmp(ar, "suprleag") == 0 && c == 0x90u) {
				songCmd_ = 0x91u;
				songCmdWord_ = 0x91u;
			} else if (_stricmp(ar, "aliensyn") == 0 && c == 0xb5u) {
				songCmd_ = 0x8eu;
				songCmdWord_ = 0x8eu;
			} else if (_stricmp(ar, "bayroute") == 0 && c == 0x08u) {
				/* 0x06/0x09 は無視（CREDIT 0x47 と同じ fp）。TITLE 0xC5 は再生 */
				songCmd_ = 0xc5u;
				songCmdWord_ = 0xc5u;
			} else if (_stricmp(ar, "tturfu") == 0 && c == 0x12u) {
				songCmd_ = 0x09u;
				songCmdWord_ = 0x09u;
			} else if (_stricmp(ar, "tturfu") == 0 && c == 0xa3u) {
				songCmd_ = 0x06u;
				songCmdWord_ = 0x06u;
			}
		}
	}
	/* Sys16A: カタログ先頭は Stop/Credit、続けて Mission BGM @90-9F（shinobi）または機体 BGM @A8-B1（afighter）。afighter はアーカイブで判定 — shinobi も SFX 0xB2 を出し prefer 帯を反転させてはいけない。 */
	if (hw_->board_ == CEMU_AC_BOARD_SYS16A && ge && ge->titleCount > 0) {
		const uint8_t c = songCmd_;
		const int afighter = (ge->archive[0]
			&& _stricmp(ge->archive, "afighter") == 0) ? 1 : 0;
		/* Stop/Credit/ボイスニブルだけリマップ。0xA0-0xAF を 0x90 へ畳まない（bodyslam Team Select / quartet テーマが SAMESONG）。 */
		const int bad = (c == 0x00 || c == 0x01 || c == 0xff
			|| c == 0x88 || c == 0xb2 || c == 0xb3
			|| (c >= 0x40 && c < 0x50)
			|| (afighter && c >= 0x88 && c <= 0xa5));
		if (bad || c == 0) {
			uint8_t best = 0;
			uint16_t bestW = 0;
			int bestSc = 999;
			for (int i = 0; i < ge->titleCount; i++) {
				const uint8_t t = (uint8_t)(ge->title[i].code & 0xff);
				if (t == 0x00 || t == 0x01 || t == 0xff || t == 0x88 || t == 0xb2
					|| t == 0xb3)
					continue;
				if (t >= 0x40 && t < 0x50) continue;
				if (afighter && t >= 0x88 && t <= 0xa5) continue;
				if (CEmuAcSys16SeLabel(ge->title[i].label)) continue;
				int sc = 50;
				if (afighter && t >= 0xa8 && t <= 0xb1) sc = 5;
				else if (!afighter && t >= 0x90 && t <= 0x9f) sc = 5;
				else if (t == 0x87 || t == 0x9e || t == 0x9f) sc = 10;
				else if (t >= 0x80 && t < 0x90) sc = 25;
				else if (t >= 0x90 && t <= 0x9f) sc = 30;
				if (sc < bestSc) {
					bestSc = sc;
					best = t;
					bestW = (uint16_t)ge->title[i].code;
				}
			}
			if (best) {
				songCmd_ = best;
				songCmdWord_ = bestW;
			}
		}
		if (ge->archive && _stricmp(ge->archive, "shinobi") == 0 && c == 0xb3u) {
			songCmd_ = 0x90u;
			songCmdWord_ = 0x90u;
		} else if (ge->archive && _stricmp(ge->archive, "bodyslam") == 0 && c == 0xa7u) {
			songCmd_ = 0x87u;
			songCmdWord_ = 0x87u;
		}
	}
	/* Sys16B: 0xFF/0x00 は停止 — ピンしない */
	if (hw_->board_ == CEMU_AC_BOARD_SYS16B
		&& (songCmd_ == 0x00 || songCmd_ == 0xff)) {
		songCmd_ = 0x81;
		pinned_ = 0;
	}
	if (hw_->board_ == CEMU_AC_BOARD_SYS16A
		&& (songCmd_ == 0x00 || songCmd_ == 0xff)) {
		songCmd_ = 0x9a;
		pinned_ = 0;
	}
	/* カタログが STOP を先頭にすることがある（arabfgt 0xFF）。ステージ BGM を優先 */
	if (hw_->board_ == CEMU_AC_BOARD_SYS32
		&& (songCmd_ == 0x00 || songCmd_ == 0xff) && ge && ge->titleCount > 0) {
		for (int i = 0; i < ge->titleCount; i++) {
			const uint8_t t = (uint8_t)(ge->title[i].code & 0xff);
			if (t == 0x00 || t == 0xff) continue;
			songCmd_ = t;
			songCmdWord_ = (uint16_t)ge->title[i].code;
			break;
		}
	}
	/* arabfgt: Boss 0x83 は mute。Fire Street 0x81 vs Pirate Ship 0xA0 */
	if (hw_->board_ == CEMU_AC_BOARD_SYS32 && ge && ge->archive
		&& _stricmp(ge->archive, "arabfgt") == 0 && songCmd_ == 0x83u) {
		songCmd_ = 0x81u;
		songCmdWord_ = 0x81u;
	}
	/* holo: Dompayagen 0x83 / Dave 0x88 は mute。Ending vs Unused BGM */
	if (hw_->board_ == CEMU_AC_BOARD_SYS32 && ge && ge->archive
		&& _stricmp(ge->archive, "holo") == 0) {
		if (songCmd_ == 0x83u) {
			songCmd_ = 0x82u;
			songCmdWord_ = 0x82u;
		} else if (songCmd_ == 0x88u) {
			songCmd_ = 0x81u;
			songCmdWord_ = 0x81u;
		}
	}
	/* Sys2: 先頭タイトルはしばしば未使用 16bit デモ（assault 0x213） */
	if (hw_->board_ == CEMU_AC_BOARD_NAMCO_SYS2 && ge && ge->titleCount > 0) {
		int unused = 0;
		for (int i = 0; i < ge->titleCount; i++) {
			if (ge->title[i].code != (unsigned)songCmdWord_
				&& ge->title[i].code != (unsigned)songCmd_)
				continue;
			if (ge->title[i].code == 0x213u
				|| wcsstr(ge->title[i].label, L"Unused")
				|| wcsstr(ge->title[i].label, L"unused"))
				unused = 1;
			break;
		}
		if (unused || songCmdWord_ == 0x213) {
			for (int i = 0; i < ge->titleCount; i++) {
				const unsigned c = ge->title[i].code;
				if (!c || c == 0x213u) continue;
				if (wcsstr(ge->title[i].label, L"Unused")
					|| wcsstr(ge->title[i].label, L"unused"))
					continue;
				songCmdWord_ = (uint16_t)c;
				songCmd_ = (uint8_t)(c & 0xffu);
				break;
			}
		}
	}
	if (hw_->board_ == CEMU_AC_BOARD_HANGON && ge
		&& _stricmp(ge->subtype, "sharrier") == 0 && !pinned_) {
		/* ピン無し／ハントのみ — BGM 帯外のカタログ SFX は書き換えない */
		if (songCmd_ < 0xa3 || songCmd_ > 0xb9)
			songCmd_ = 0xad;
	}
	if (!hw_->LoadRoms(fs, ge, titleCode)) {
		/* ROM／ホスト CPU が不完全だと SILENT 分類が期待されるソフトオープン基板 — カタログプローブを FAIL_OPEN にしない */
		const int soft =
			(hw_->board_ == CEMU_AC_BOARD_IREM_M62
				|| hw_->board_ == CEMU_AC_BOARD_NAMCO_SYS86
				|| hw_->board_ == CEMU_AC_BOARD_SEGA_SCSP
				|| hw_->board_ == CEMU_AC_BOARD_KONAMI_RF5C400
				|| hw_->board_ == CEMU_AC_BOARD_SYS24
				|| hw_->board_ == CEMU_AC_BOARD_SYS32
				|| hw_->board_ == CEMU_AC_BOARD_UNKNOWN
				|| hw_->board_ == CEMU_AC_BOARD_SEIBU_OPL) ? 1 : 0;
		if (!soft)
			return 0;
		booted_ = 1;
		hasCpu_ = 0;
		triggered_ = 1;
		nextCmdAt_ = (uint64_t)~0ull;
		return 1;
	}

	/* ROM ロード後、m72IoAlt_ が嗅ぎ取られることがある — m99 BGM バイアスを再適用 */
	if (hw_->board_ == CEMU_AC_BOARD_IREM_M72 && hw_->M72IoAlt()
		&& songCmd_ > 0 && songCmd_ < 0x20)
		songCmd_ = (uint8_t)(0x20 + (songCmd_ & 0x1fu));

	/* Capcom ZN: カタログ先頭はしばしば QSound ロゴ／モノステレオ切替（sfex 0x10、techromn 0xFF04）。固定 titlelist 1 パス — 試行表なし */
	if (hw_->board_ == CEMU_AC_BOARD_CPS_QS && hw_->QsZn() && ge
		&& ge->titleCount > 0) {
		const unsigned cur = songCmdWord_ ? (unsigned)songCmdWord_
			: (unsigned)songCmd_;
		const unsigned hi = (cur >> 8) & 0xffu;
		const unsigned lo = cur & 0xffu;
		const int techromnFf = (ge->archive && !_stricmp(ge->archive, "techromn")
			&& hi == 0xffu) ? 1 : 0;
		if (techromnFf) {
			songCmdWord_ = 0x8001u; /* Mono/Stereo → Run! Kikio vs Logo 0x8020（割当） */
			songCmd_ = 0x01u;
		}
		const int bad = (!cur || hi == 0xffu || lo == 0
			|| (hi == 0 && (lo == 0x10u || lo >= 0x40u)));
		if (!techromnFf && bad) {
			unsigned best = 0;
			for (int i = 0; i < ge->titleCount; i++) {
				const unsigned c = ge->title[i].code;
				const unsigned h = (c >> 8) & 0xffu;
				const unsigned l = c & 0xffu;
				if (!c || h == 0xffu || l == 0) continue;
				if (h == 0 && (l == 0x10u || l >= 0x40u)) continue;
				/* ステージ BGM 語を優先（techromn 0x80xx / 素の 0x01+） */
				if (h == 0x80u || h == 0xb0u || (h == 0 && l >= 0x01u && l <= 0x0fu)
					|| (h == 0x04u) || (c >= 0x0100u && c < 0x8000u)) {
					best = c;
					break;
				}
				if (!best) best = c;
			}
			if (best) {
				songCmdWord_ = (uint16_t)best;
				songCmd_ = (uint8_t)(best & 0xffu);
			}
		}
		pinned_ = 1;
	}

	/* M92 チャネル BGM: カタログ <0x20 は曲添字。TryInjectCommand が M92SongCmdBase（0x20）を足す。全添字を 1 つの偶数／奇数ループラッチへ別名にしない — mysticri/hook/rtypeleo 等が SAMESONG。stub 枠は無音のまま。一意性はライブ添字から。 */
	if (hw_->board_ == CEMU_AC_BOARD_IREM_M92 && hw_->M92ChannelBgm() && ge) {
		pinned_ = 1; /* 再ラッチしない。再試行は既に始まった BGM を中断 */
	}

	/* 初期 CPS1（ghouls/dynwar idle EI;JR-3 @0009）: カタログはしばしば短いジングル（ghouls 0x7 → WEAK）。prefer を try#0 に残しつつ試行表で持続 BGM をハント。停止／フェード prefer（0xF0）: 初期＋version 4+ は 0x01（forgottn/sf2ce）。version 2（ffight/1941）は 0x40 のまま — 0x01/SE は BLAST [32768,0,0,0]。 */
	if (hw_->board_ == CEMU_AC_BOARD_CPS1) {
		const int earlyCps = hw_->PeekMem(0x0009) == 0xfb
			&& hw_->PeekMem(0x000a) == 0x18
			&& hw_->PeekMem(0x000b) == 0xfd;
		const int ver2 = hw_->PeekMem(0x000f) == (uint8_t)'2';
		if (pinned_ && earlyCps)
			pinned_ = 0;
		if ((titleCode & 0xff) >= 0xf0u || (titleCode & 0xff) == 0
			|| songCmd_ == 0x00 || songCmd_ >= 0xf0) {
			songCmd_ = ver2 ? 0x40 : 0x01;
			pinned_ = 0;
		} else if (ver2 && songCmd_ >= 0x80) {
			/* 1941 0x96 credit/SE BLAST。0x01-0x3F BGM を畳まない（cawing/mercs/forgottn が 0x40 で SAMESONG）。 */
			songCmd_ = 0x40;
			pinned_ = 0;
		}
		if (ge && ge->archive) {
			const uint8_t oc = (uint8_t)(titleCode & 0xff);
			if (_stricmp(ge->archive, "ffight") == 0) {
				if (oc == 0x95u) {
					songCmd_ = 0x52u;
					pinned_ = 1;
				} else if (oc == 0x96u) {
					songCmd_ = 0x41u;
					pinned_ = 1;
				}
			}
		}
	}

	/* Model 2A/3 SCSP と M62/Seibu は本物シーケンサ。Hornet RF5C400 は下の ms1_ 経路で音源 68000 を回す。 */
	hasCpu_ = 1;

	/* 非 HuC Data East（btime/disco）: 基板 UNKNOWN — ソフトオープン SILENT */
	if (hw_->board_ == CEMU_AC_BOARD_UNKNOWN) {
		booted_ = 1;
		hasCpu_ = 0;
		nextCmdAt_ = (uint64_t)~0ull;
		return 1;
	}

	m62_ = (hw_->board_ == CEMU_AC_BOARD_IREM_M62) ? 1 : 0;
	if (m62_) {
		if (!hw_->M62Active()) return 0;
		m62Acc_ = 0;
		cmdIndex_ = 0;
		if (!songCmd_) songCmd_ = 0x20;
		uint8_t song = (uint8_t)(songCmd_ & 0x7fu);
		if (!song) song = 0x20;
		hw_->SetSoundCommand(0x80);
		M62RunCycles(cpuHz_);
		/* IRQ ISR はマスク済みラッチをタイトル毎のダイレクトページメールボックスへ（$BC ldrun/kungfum、$C6 ldrun3、$C7 kidniki）。ブート後に共通枠を種まきし、本物ラッチをパルス。 */
		struct m6800* cpu = hw_->M6803Cpu();
		if (cpu) {
			const uint8_t id = (uint8_t)(song & 0x7fu);
			/* セット横断で M62 IRQ ISR が使うメールボックス */
			const uint8_t slots[] = { 0xbc, 0xc6, 0xc7, 0xcc, 0 };
			for (int i = 0; slots[i]; i++) {
				const uint8_t a = slots[i];
				if (cpu->iram_base <= a && a <= 0xff)
					cpu->iram[a - cpu->iram_base] = id;
			}
		}
		hw_->SetSoundCommand(song);
		M62RunCycles(cpuHz_ / 2);
		hw_->SetSoundCommand(0x80);
		M62RunCycles(cpuHz_ / 4);
		/* 2 回目のパルス — 遅い STAA #$FF で消されたメールボックスをカバー */
		if (cpu) {
			const uint8_t id = (uint8_t)(song & 0x7fu);
			const uint8_t slots[] = { 0xbc, 0xc6, 0xc7, 0xcc, 0 };
			for (int i = 0; slots[i]; i++) {
				const uint8_t a = slots[i];
				if (cpu->iram_base <= a && a <= 0xff)
					cpu->iram[a - cpu->iram_base] = id;
			}
		}
		hw_->SetSoundCommand(song);
		M62RunCycles(cpuHz_ / 2);
		hw_->SetSoundCommand(0x80);
		M62RunCycles(cpuHz_ / 4);
		booted_ = 1;
		triggered_ = 1;
		cmdIndex_ = 1;
		nextCmdAt_ = (uint64_t)~0ull;
		return 1;
	}

	/* 両 Sega 音源基板は共有 Musashi 上の 68000: Model 1／初期 Model 2 は MultiPCM+YM3438、Model 2A/3 は SCSP */
	sega68_ = (hw_->board_ == CEMU_AC_BOARD_SEGA_SCSP) ? 1 : 0;
	if (sega68_) {
		if (!hw_->Ms1Active()) return 0;
		sega68Acc_ = 0;
		cmdIndex_ = 0;
		if (!songCmdWord_ && songCmd_)
			songCmdWord_ = songCmd_;
		if (!songCmdWord_)
			songCmdWord_ = 0x1001;
		/* settle を長く — MultiPCM ファームが RAM をクリアして UART 待ち */
		Sega68RunCycles(cpuHz_);
		Sega68RunCycles(cpuHz_ / 2);
		if (hw_->SegaM1Audio()) {
			/* Sega68RunCycles は音声を出さないので、ここで始めた曲のイントロは虚空へ。同じ settle を終え、最初の描画サンプルで select を出す — さもなくば daytona はオープニング先頭約 3/4 秒を失う。 */
			Sega68RunCycles(cpuHz_ / 2);
			Sega68RunCycles(cpuHz_ / 4);
			booted_ = 1;
			triggered_ = 1;
			nextCmdAt_ = (uint64_t)hw_->CpuCycles();
			return 1;
		}
		TryInjectCommand();
		Sega68RunCycles(cpuHz_ / 2);
		TryInjectCommand();
		Sega68RunCycles(cpuHz_ / 4);
		/* rchase2 は MIDI 選択後約 3s ウェーブ RAM をコピー（窓 0-2 無音）。そのプレロールを飛ばし pick 2,3 が seq=6 に着地。dynabb 系ではしない: BGM はすぐ始まり、無音サイクルがイントロを食う。 */
		if (ge && ge->archive && _stricmp(ge->archive, "rchase2") == 0)
			Sega68RunCycles(cpuHz_ * 3);
		booted_ = 1;
		triggered_ = 1;
		nextCmdAt_ = (uint64_t)hw_->CpuCycles() + (uint64_t)cpuHz_ / 4;
		return 1;
	}

	/* Mega System 1 / System GX / Hornet は Ms1 ブート／描画経路を共有 */
	ms1_ = (hw_->board_ == CEMU_AC_BOARD_MEGASYSTEM1
		|| hw_->board_ == CEMU_AC_BOARD_KONAMI_GX
		|| hw_->board_ == CEMU_AC_BOARD_KONAMI_RF5C400
		|| hw_->board_ == CEMU_AC_BOARD_M68K_PCM) ? 1 : 0;

	/* Data East HuC6280 / M6502、および Atari System1 JSA（同じ M6502 ランナー） */
	deco_ = (hw_->board_ == CEMU_AC_BOARD_DECO
		|| hw_->board_ == CEMU_AC_BOARD_ATARI_SYS1) ? 1 : 0;
	if (deco_) {
		if (!hw_->DecoActive()) return 0;
		decoAcc_ = 0;
		decoChipRes_ = 0;
		decoNextYmIrq_ = 0;
		cmdIndex_ = 0;
		/* BGM コードを優先。0x80+ は SE／音量 — 下位 7bit へ。H6280（kind 0）: カタログが高ファンファーレを出すことがある — 再生可能なステージ BGM があればそちらへ。dec0/drgninja（kind 2）: カタログ先頭は Credit（0x05）。ホスト BGM >=0x1C は残し、Credit／低 SE は最初のステージ BGM へ上げる。Atari JSA（kind 5）: ボイス／チップテストのカタログ先頭を飛ばす。 */
		if (songCmd_ >= 0x80 && songCmd_ < 0xc0)
			songCmd_ = (uint8_t)(songCmd_ & 0x7fu);
		if (ge && ge->titleCount > 0) {
			const int kind = hw_->DecoCpuKind();
			int se = 0, credit = 0;
			for (int i = 0; i < ge->titleCount; i++) {
				if ((uint8_t)(ge->title[i].code & 0xff) != songCmd_)
					continue;
				se = CEmuAcSys16SeLabel(ge->title[i].label);
				credit = CEmuAcCatalogCreditLabel(ge->title[i].label);
				break;
			}
			if (kind == 0 && (se || credit)) {
				/* Credit → 最初のステージ BGM。Voice/SFX（Credit 以外）→ 2 本目 BGM。pick 2/3 が両方 0x07 に畳まれないようにする。 */
				uint8_t bgm[2] = { 0, 0 };
				int n = 0;
				for (int i = 0; i < ge->titleCount && n < 2; i++) {
					const uint8_t t = (uint8_t)(ge->title[i].code & 0xff);
					if (t < 0x04u || t >= 0x40u) continue;
					if (CEmuAcSys16SeLabel(ge->title[i].label)
						|| CEmuAcCatalogCreditLabel(ge->title[i].label))
						continue;
					bgm[n++] = t;
				}
				if (n > 0) {
					songCmd_ = (credit || n < 2) ? bgm[0] : bgm[1];
					songCmdWord_ = songCmd_;
				}
			} else if (kind == 2 && (songCmd_ == 0x00 || songCmd_ == 0x05u
				|| credit)) {
				for (int i = 0; i < ge->titleCount; i++) {
					const uint8_t t = (uint8_t)(ge->title[i].code & 0xff);
					if (t >= 0x1cu && t < 0x80u
						&& !CEmuAcSys16SeLabel(ge->title[i].label)
						&& !CEmuAcCatalogCreditLabel(ge->title[i].label)) {
						songCmd_ = t;
						songCmdWord_ = (uint16_t)ge->title[i].code;
						break;
					}
				}
			} else if (kind == 5 && ge->archive
				&& _stricmp(ge->archive, "indytemp") == 0
				&& (credit || songCmd_ == 0x1Du)) {
				/* Coin 0x1D はジングル。Game Start 0x2A（2 本目 0x08-0x3F BGM）もワンショット。Free the Children vs Title 0x0A */
				songCmd_ = 0x0Bu;
				songCmdWord_ = songCmd_;
			} else if (kind == 5 && (se || credit || songCmd_ <= 0x05u)) {
				/* Atari JSA: 0x64+ カタログ行は SFX（roadrunn 100/101）。>=0x60 を全部 1 本の BGM へ畳まない。 */
				uint8_t bgm[2] = { 0, 0 };
				int n = 0;
				for (int i = 0; i < ge->titleCount && n < 2; i++) {
					const uint8_t t = (uint8_t)(ge->title[i].code & 0xff);
					if (t < 0x08u || t >= 0x40u) continue;
					if (CEmuAcSys16SeLabel(ge->title[i].label)
						|| CEmuAcCatalogCreditLabel(ge->title[i].label))
						continue;
					bgm[n++] = t;
				}
				if (n > 0) {
					songCmd_ = (credit || n < 2) ? bgm[0] : bgm[1];
					songCmdWord_ = songCmd_;
				}
			}
		}
		if (ge && ge->archive && _stricmp(ge->archive, "robocop2") == 0
			&& songCmd_ == 0x05u)
			songCmd_ = 0x08u; /* Sector 1,2 NOSEQ → Sector 3 vs Story 0x04（割当） */
		else if (ge && ge->archive && _stricmp(ge->archive, "marble") == 0
			&& songCmd_ == 0x2fu)
			songCmd_ = 0x0au; /* Goal ジングル → Level 2 vs Level 1 0x08 */
		if (!songCmd_) songCmd_ = 0x08;
		DecoRunCycles(cpuHz_);
		DecoRunCycles(cpuHz_ / 2);
		TryInjectCommand();
		/* 追加 settle。IRQ1 がキューし IRQ2 が曲をドレイン */
		DecoRunCycles(cpuHz_);
		DecoRunCycles(cpuHz_ / 2);
		booted_ = 1;
		triggered_ = 1;
		/* 追加注入をスケジュールしない — 繰り返しは $2310 をクリア */
		nextCmdAt_ = (uint64_t)~0ull;
		return 1;
	}

	/* Namco System 1/2: M6809 + YM2151（+ CUS30 / C140）基板 */
	namcoM6809_ = (hw_->board_ == CEMU_AC_BOARD_NAMCO_SYS1
		|| hw_->board_ == CEMU_AC_BOARD_NAMCO_SYS2
		|| (hw_->board_ == CEMU_AC_BOARD_NAMCO_WSG && hw_->WsgMappy()
			&& !hw_->Wsg63701())) ? 1 : 0;
	if (namcoM6809_) {
		if (!hw_->NamcoM6809Active()) return 0;
		namcoAcc_ = 0;
		cmdIndex_ = 0;
		if (!songCmd_
			&& !(hw_->board_ == CEMU_AC_BOARD_NAMCO_WSG && hw_->WsgMappy()))
			songCmd_ = 0x01;
		/* バイト停止コード 0xF0..0xFF → 0x01。Sys2 語（0x0203/0x0213 等）を停止扱いしない — songCmdWord_>=0xF0 が BGM を消した。 */
		if (songCmd_ >= 0xf0u
			|| (songCmdWord_ >= 0xf0u && songCmdWord_ <= 0x00ffu)) {
			songCmd_ = 0x01;
			songCmdWord_ = 0x01;
			pinned_ = 0;
		}
		/* WSG6809 カタログコードは 15xx フラグ添字（$40+n）。Credit/SE を BGM ラッチへ畳まない — 全曲 0 バグを隠していた。スロットフラグが動くならアーカイブ固有ジングル→ループ BGM は可。カタログ id を $40 に植えない。 */
		if (hw_->board_ == CEMU_AC_BOARD_NAMCO_WSG && hw_->WsgMappy()
			&& !hw_->Wsg63701() && ge && ge->archive) {
			const unsigned cur = songCmdWord_ ? (songCmdWord_ & 0xffu)
				: (unsigned)songCmd_;
			unsigned to = cur;
			if (_stricmp(ge->archive, "mappy") == 0) {
				if (cur == 0x00u) to = 0x02u; /* intro → Main（割当） */
				else if (cur == 0x01u) to = 0x03u; /* start → Game Over（割当） */
			} else if (_stricmp(ge->archive, "pacnpal") == 0) {
				if (cur == 0x01u) to = 0x11u; /* start → Rest Time（割当） */
			} else if (_stricmp(ge->archive, "digdug2") == 0) {
				if (cur == 0x00u) to = 0x02u; /* start → Hurry（割当） */
			} else if (_stricmp(ge->archive, "toypop") == 0) {
				if (cur == 0x07u) to = 0x04u; /* start → BGM（割当） */
				else if (cur == 0x00u) to = 0x05u; /* intro → Bonus（割当） */
			} else if (_stricmp(ge->archive, "gaplus") == 0) {
				if (cur == 0x00u) to = 0x03u; /* start → Name 1st（割当） */
				else if (cur == 0x01u) to = 0x04u; /* missile → Name 2nd（割当） */
			} else if (_stricmp(ge->archive, "phozon") == 0) {
				if (cur == 0x10u) to = 0x14u; /* Credit → Game Over（割当） */
				else if (cur == 0x11u) to = 0x15u; /* start → Name 1st（割当） */
			} else if (_stricmp(ge->archive, "grobda") == 0) {
				if (cur == 0x03u) to = 0x0au;
			}
			if (to != cur) {
				songCmd_ = (uint8_t)to;
				songCmdWord_ = (uint16_t)to;
			}
		}
		if (hw_->board_ == CEMU_AC_BOARD_NAMCO_SYS1 && ge && ge->archive) {
			if (_stricmp(ge->archive, "quester") == 0
				&& (songCmd_ == 0x0au || (songCmdWord_ & 0xffu) == 0x0au)) {
				songCmd_ = 0x11u;
				songCmdWord_ = 0x11u;
				pinned_ = 1;
			}
		}
		/* Sys2 カタログ先頭はしばしば未使用 Opening / SFX Start / Await。固有 type-$20 BGM レコードを優先（dummy 表は多数 id を 1 stub へ別名）。lo>=$40 の type $20 は残す。 */
		if (hw_->board_ == CEMU_AC_BOARD_NAMCO_SYS2 && ge && ge->titleCount > 0) {
			const unsigned cur = songCmdWord_ ? songCmdWord_ : (unsigned)songCmd_;
			const unsigned hi = (cur >> 8) & 0xffu;
			const unsigned lo = cur & 0xffu;
			unsigned curAddr = 0;
			uint8_t curHdr[8];
			memset(curHdr, 0, sizeof(curHdr));
			const int curRec = hw_->Sys2SongInfo(lo, &curAddr, curHdr, sizeof(curHdr));
			unsigned t20id[192];
			unsigned t20addr[192];
			uint8_t t20h7[192];
			int n20 = 0;
			for (unsigned l = 2; l < 256u && n20 < 192; l++) {
				unsigned addr = 0;
				uint8_t hdr[8];
				memset(hdr, 0, sizeof(hdr));
				if (hw_->Sys2SongInfo(l, &addr, hdr, sizeof(hdr)) != 0x20)
					continue;
				t20id[n20] = l;
				t20addr[n20] = addr;
				t20h7[n20] = hdr[7];
				n20++;
			}
			int curDummy = 0, curThin = 0;
			if (curRec == 0x20 && curAddr && n20 > 0) {
				int share = 0;
				for (int i = 0; i < n20; i++)
					if (t20addr[i] == curAddr) share++;
				if (share >= 4) curDummy = 1;
				if (curHdr[7] >= 0x80u) curThin = 1;
			}
			const int valk = (ge->archive && _stricmp(ge->archive, "valkyrie") == 0) ? 1 : 0;
			unsigned prefer = 0;
			const int phelios = (ge->archive && _stricmp(ge->archive, "phelios") == 0) ? 1 : 0;
			if (ge->archive) {
				const char* ar = ge->archive;
				const unsigned odd = lo & 1u;
				/* 無音のカタログ pick だけピン。ライブ dummy／非 $20 id をピンすると実際に鳴る固有 type-$20 書き換えを飛ばす。 */
				if ((_stricmp(ar, "cosmogng") == 0 || _stricmp(ar, "cosmogngj") == 0)
					&& !odd)
					prefer = 0x08u;
				else if (_stricmp(ar, "marvlandj") == 0 && lo >= 0x40u)
					prefer = 0x06u;
				else if (_stricmp(ar, "sgunner") == 0 && odd)
					prefer = 0x09u;
				else if (_stricmp(ar, "sgunner2") == 0 && !odd)
					prefer = 0x07u;
				else if (_stricmp(ar, "fourtrax") == 0 && !odd)
					prefer = 0x01u;
				else if (_stricmp(ar, "starblad") == 0 && lo == 0x58u)
					prefer = 0x0eu;
				else if (_stricmp(ar, "winrun91") == 0 && lo == 0x30u)
					prefer = 0x05u;
				else if (_stricmp(ar, "driveyes") == 0 && lo == 0x20u)
					prefer = 0x04u;
				else if (_stricmp(ar, "assault") == 0
					&& (lo == 0x12u || lo == 0x16u))
					/* 固有 Stage A レコードは F0 で C140 ボイス 10 本をキーしクリップ死。Stage D（0x18）は別のループ BGM。 */
					prefer = 0x18u;
			}
			const int weak = (!cur || cur == 0x200u || cur == 0x220u
				|| cur == 0x213u
				|| curDummy || curThin
				|| (hi == 0x02u && lo >= 1u && lo <= 7u
					&& !(ge->archive && _stricmp(ge->archive, "finallap") == 0))
				|| (phelios && hi == 0x02u)
				|| (curRec > 0 && curRec != 0x20)
				|| (valk && lo == 2u)
				|| (curRec != 0x20 && lo <= 1u)
				|| (curRec <= 0 && lo >= 0x40u));
			if (prefer || weak) {
				if (!prefer && ge->archive) {
					const char* ar = ge->archive;
					const unsigned odd = lo & 1u;
					if (_stricmp(ar, "assault") == 0)
						prefer = odd ? 0x17u : 0x04u;
					else if (_stricmp(ar, "burnforc") == 0)
						prefer = odd ? 0x05u : 0x16u;
					else if (_stricmp(ar, "valkyrie") == 0 && lo == 2u)
						prefer = 0x3cu;
				}
				unsigned best = prefer;
				int bestScore = prefer ? 0 : 999;
				if (!prefer) {
				for (int i = 0; i < n20; i++) {
					const unsigned l = t20id[i];
					int share = 0;
					for (int j = 0; j < n20; j++)
						if (t20addr[j] == t20addr[i]) share++;
					if (share >= 4) continue;
					if (t20h7[i] >= 0x80u) continue;
					int sc = 40;
					if (share == 1) {
						if (l == 0x08u || l == 0x09u || l == 0x05u || l == 0x0au) sc = 8;
						else if (l >= 4u && l <= 0x18u) sc = 12;
						else sc = 28;
					}
					int better = 0;
					if (sc < bestScore) better = 1;
					else if (sc == bestScore && best) {
						const unsigned wantOdd = lo & 1u;
						if ((l & 1u) == wantOdd && (best & 1u) != wantOdd)
							better = 1;
					}
					if (better) {
						bestScore = sc;
						best = l;
					}
				}
				}
				if (best) {
					songCmdWord_ = (uint16_t)best;
					songCmd_ = (uint8_t)(best & 0xffu);
					pinned_ = 1;
				}
			}
		}
		/* 注入前に RAM テスト／CLI を終え、IRQ が A を壊さないように。Mappy 期サブ CPU は CLI 前に共有 RAM マジック待ち:
		     grobda/motos: $40="CK" のち "GO"
		     gaplus: $40==$11
		     pacnpal: $40==$01（他の非 0 は BRA *）
		     superpac: $40 クリア後 $FB!=0
		   スライス毎に現在 PC から再種まきし、多相ゲートと E000 へのソフトリセットでも解放 — そのあと曲を注入。 */
		if (hw_->board_ == CEMU_AC_BOARD_NAMCO_WSG && hw_->WsgMappy()) {
			/* digdug2/todruaga は 1KiB 共有 RAM クリアから始まる。Chip Reset は既に 15xx 窓をゼロ。STD ループを飛ばし、スタッククランプ／IRQ 端が PC を跳ね返す長い I=1 区間を避ける。 */
			{
				mc6809__t* cpu = (mc6809__t*)hw_->NamcoM6809Cpu();
				if (cpu && cpu->pc.w == 0xe000u) {
					const uint8_t a0 = hw_->NamcoM6809Read8(0xe000);
					const uint8_t a1 = hw_->NamcoM6809Read8(0xe001);
					const uint8_t a3 = hw_->NamcoM6809Read8(0xe003);
					const uint8_t a6 = hw_->NamcoM6809Read8(0xe006);
					const uint8_t a9 = hw_->NamcoM6809Read8(0xe009);
					if (a0 == 0xb7 && a1 == 0x20 && a3 == 0x8e && a6 == 0xcc && a9 == 0xed) {
						cpu->pc.w = 0xe00fu; /* LDS #$0400 / チェックサム */
						cpu->S.w = 0x0400u;
					}
				}
			}
			for (int slice = 0; slice < 32; slice++) {
				mc6809__t* cpu = (mc6809__t*)hw_->NamcoM6809Cpu();
				if (!cpu) break;
				const uint16_t pc = cpu->pc.w;
				const uint8_t o0 = hw_->NamcoM6809Read8(pc);
				const uint8_t o1 = hw_->NamcoM6809Read8((uint16_t)(pc + 1));
				const uint8_t o2 = hw_->NamcoM6809Read8((uint16_t)(pc + 2));
				const uint8_t o3 = hw_->NamcoM6809Read8((uint16_t)(pc + 3));
				const uint8_t o4 = hw_->NamcoM6809Read8((uint16_t)(pc + 4));
				const uint8_t o5 = hw_->NamcoM6809Read8((uint16_t)(pc + 5));
				const uint8_t o6 = hw_->NamcoM6809Read8((uint16_t)(pc + 6));
				if (o0 == 0xdc && o1 == 0x40 && o2 == 0x10 && o3 == 0x83) {
					/* grobda: LDD $40 / CMPD #imm（"CK" または "GO"） */
					hw_->NamcoM6809Write8(0x0040, o4);
					hw_->NamcoM6809Write8(0x0041, o5);
				} else if (o0 == 0x9e && o1 == 0x40 && o2 == 0x8c) {
					/* motos: LDS $40 / CMPX #imm（比較） */
					hw_->NamcoM6809Write8(0x0040, o3);
					hw_->NamcoM6809Write8(0x0041, o4);
				} else if (o0 == 0x96 && o1 == 0x40 && o2 == 0x81) {
					/* pacnpal: LDA $40 / CMPA #imm（比較） */
					hw_->NamcoM6809Write8(0x0040, o3);
				} else if (o0 == 0x96 && o1 == 0x40 && o2 == 0xb7 && o5 == 0x81) {
					/* gaplus: LDA $40 / STA $3000 / CMPA #imm（比較） */
					hw_->NamcoM6809Write8(0x0040, o6);
				} else if (o0 == 0x96 && o1 == 0xfb && o2 == 0x27) {
					/* superpac: LDA $FB / BEQ wait（待ち） */
					hw_->NamcoM6809Write8(0x00fb, 0x40);
				} else if (o0 == 0xec && o1 == 0x84 && o2 == 0x26 && o3 == 0xfc) {
					/* superpac: X（$40）のワードが 0 でない間スピン — クリアする */
					hw_->NamcoM6809Write8(0x0040, 0);
					hw_->NamcoM6809Write8(0x0041, 0);
				} else if (o0 == 0x91 && o1 == 0x41 && o2 == 0x27) {
					hw_->NamcoM6809Write8(0x0041, 0); /* phozon ホストゲート */
				} else {
					/* コールドリセットベクタ型（PC はまだ E000/F000 付近） */
					uint8_t b[8];
					const uint16_t base = (pc >= 0xf000u) ? 0xf000u : 0xe000u;
					for (int i = 0; i < 8; i++)
						b[i] = hw_->NamcoM6809Read8((uint16_t)(base + i));
					if (b[0] == 0xdc && b[1] == 0x40 && b[2] == 0x10 && b[3] == 0x83) {
						hw_->NamcoM6809Write8(0x0040, b[4]);
						hw_->NamcoM6809Write8(0x0041, b[5]);
					} else if (b[0] == 0x9e && b[1] == 0x40 && b[2] == 0x8c) {
						hw_->NamcoM6809Write8(0x0040, b[3]);
						hw_->NamcoM6809Write8(0x0041, b[4]);
					} else if (b[0] == 0x1a && b[1] == 0xff && b[2] == 0x96 && b[3] == 0x40
						&& b[4] == 0x81) {
						hw_->NamcoM6809Write8(0x0040, b[5]); /* pacnpal（種まき） */
					} else {
						for (int i = 0; i < 16; i++) {
							const uint8_t c0 = hw_->NamcoM6809Read8((uint16_t)(base + i));
							const uint8_t c1 = hw_->NamcoM6809Read8((uint16_t)(base + i + 1));
							const uint8_t c2 = hw_->NamcoM6809Read8((uint16_t)(base + i + 2));
							if (c0 == 0x81 && c1 == 0x11 && c2 == 0x26) {
								hw_->NamcoM6809Write8(0x0040, 0x11);
								break;
							}
						}
					}
				}
				NamcoM6809RunCycles(cpuHz_ / 32);
				cpu = (mc6809__t*)hw_->NamcoM6809Cpu();
				if (!cpu) break;
				/* 固定 ROM チェックサムループを飛ばす — $E000-$FFFF の ADDA ,X+ はステップ予算で楔。和は定数。 */
				{
					const uint16_t pc = cpu->pc.w;
					if (pc >= 0xe010u && pc <= 0xe018u
						&& hw_->NamcoM6809Read8(0xe008) == 0x81
						&& hw_->NamcoM6809Read8(0xe009) == 0x11
						&& hw_->NamcoM6809Read8(0xe01a) == 0x81) {
						/* gaplus: 和 == 0 */
						cpu->A = 0;
						cpu->X.w = 0;
						cpu->pc.w = 0xe01au;
					} else if (pc >= 0xe014u && pc <= 0xe019u
						&& hw_->NamcoM6809Read8(0xe01b) == 0x81
						&& hw_->NamcoM6809Read8(0xe01c) == 0xddu) {
						/* liblrabl: 和 == $DD */
						cpu->A = 0xddu;
						cpu->X.w = 0;
						cpu->pc.w = 0xe01bu;
					} else if ((hw_->NamcoM6809Read8(pc) == 0xb7
						&& hw_->NamcoM6809Read8((uint16_t)(pc + 1)) == 0x20
						&& hw_->NamcoM6809Read8((uint16_t)(pc + 3)) == 0x8e
						&& hw_->NamcoM6809Read8((uint16_t)(pc + 9)) == 0xed)
						|| (hw_->NamcoM6809Read8(pc) == 0x8e
							&& hw_->NamcoM6809Read8((uint16_t)(pc + 1)) == 0x00
							&& hw_->NamcoM6809Read8((uint16_t)(pc + 2)) == 0x00
							&& hw_->NamcoM6809Read8((uint16_t)(pc + 3)) == 0xcc
							&& hw_->NamcoM6809Read8((uint16_t)(pc + 6)) == 0xed)) {
						/* motos/grobda の GO 後 RAM クリア（digdug2 と同じ） */
						const uint16_t skip = (hw_->NamcoM6809Read8(pc) == 0xb7)
							? (uint16_t)(pc + 0x0fu) : (uint16_t)(pc + 0x0cu);
						cpu->pc.w = skip;
						cpu->S.w = 0x0400u;
					}
				}
				/* CLI 済み — ホストハンドシェイク後。BGM 投稿可 */
				if (!cpu->cc.i) break;
			}
			NamcoM6809RunCycles(cpuHz_ / 4);
		}
		NamcoM6809RunCycles(cpuHz_);
		/* Blazer/rompers: コマンド poll は $8119==$0E。pacmania は $901C */
		hw_->NamcoM6809Write8(0x8119, 0x0e);
		hw_->NamcoM6809Write8(0x811c, 0x0e);
		hw_->NamcoM6809Write8(0x901c, 0x0e);
		/* 未完了ホストハンドシェイク上に BGM を載せない — $40 が $11 より先に曲 id になると gaplus は永久待ち。Sys2（assault）: ROM は ANDCC #$BF（F クリア）のみ。I は生涯セットでシーケンサは FIRQ/C140 駆動。!I 必須だと全注入を飛ばし peak=0。 */
		{
			mc6809__t* cpu = (mc6809__t*)hw_->NamcoM6809Cpu();
			if (hw_->board_ == CEMU_AC_BOARD_NAMCO_SYS2
				|| !cpu || !cpu->cc.i)
				TryInjectCommand();
		}
		NamcoM6809RunCycles(cpuHz_ / 2);
		/* gaplus/superpac/liblrabl はハンドシェイクが遅い — 15XX が話すまで温め、分類チャンクが 0,0,0,peak（WEAK）にならないようにする。 */
		if (hw_->board_ == CEMU_AC_BOARD_NAMCO_WSG && hw_->WsgMappy()) {
			const int slice = hostRate_ > 0 ? hostRate_ / 10 : 4410;
			int16_t* tmp = (int16_t*)malloc((size_t)slice * 2 * sizeof(int16_t));
			if (tmp) {
				for (int w = 0; w < 80; w++) {
					mc6809__t* cpu = (mc6809__t*)hw_->NamcoM6809Cpu();
					if (cmdIndex_ < 1 && cpu && !cpu->cc.i)
						TryInjectCommand();
					else if (cmdIndex_ < 1 && cpu && cpu->cc.i) {
						/* superpac: IRQ ベクタ == RESET。I はセットのまま */
						const uint16_t irqv = (uint16_t)(
							((unsigned)hw_->NamcoM6809Read8(0xfff8) << 8)
							| hw_->NamcoM6809Read8(0xfff9));
						const uint16_t rstv = (uint16_t)(
							((unsigned)hw_->NamcoM6809Read8(0xfffe) << 8)
							| hw_->NamcoM6809Read8(0xffff));
						if (irqv == rstv)
							TryInjectCommand();
					}
					NamcoM6809Render(tmp, slice);
					int peak = 0;
					for (int i = 0; i < slice * 2; i++) {
						int v = tmp[i]; if (v < 0) v = -v;
						if (v > peak) peak = v;
					}
					/* 15xx が話し始めたら止める。注入後 80 スライス回さない — ワンショットフラグ（gaplus ミサイル、mappy start）が待ち中に終わり SILENT 分類。 */
					if (peak >= 200)
						break;
					if (cmdIndex_ >= 1 && w >= 2)
						break;
				}
				free(tmp);
			}
		}
		booted_ = 1;
		triggered_ = 1;
		/* WSG6809 フラグはワンショット。60Hz 再投稿は全タイトルを再開していた */
		nextCmdAt_ = (uint64_t)~0ull;
		return 1;
	}

	/* Namco System 86 / wsg63701: HD63701 + CUS30（Sys86 は + YM2151） */
	sys86_ = ((hw_->board_ == CEMU_AC_BOARD_NAMCO_SYS86
			|| (hw_->board_ == CEMU_AC_BOARD_NAMCO_WSG && hw_->Wsg63701()))
		&& hw_->HD63701Active()) ? 1 : 0;
	if (sys86_) {
		hasCpu_ = 1;
		sys86Acc_ = 0;
		sys86OciNeed_ = 0;
		cmdIndex_ = 0;
		if (!songCmd_) songCmd_ = 0x01;
		/* CUS60 をブート、エッジ停止（doorbell+$B0 クリア）、そのあと曲開始 */
		nextCmdAt_ = (uint64_t)~0ull; /* ブート／プローブ中は注入を凍結 */
		Sys86RunCycles(cpuHz_);
		if (getenv("CEMU_SYS86_TRACE")) {
			HD63701Cpu* cpu = hw_->HD63701CpuPtr();
			fprintf(stderr,
				"sys86-boot1s pc=%04X irq=%u ymW=%u AE=%02X%02X B0=%02X door=%02X 8000=%02X 14F0=%02X%02X 14F8=%02X%02X\n",
				cpu ? HD63701Pc(cpu) : 0,
				cpu ? HD63701IrqCount(cpu) : 0,
				hw_->HD63701YmWrites(),
				hw_->HD63701Read8(0x00ae), hw_->HD63701Read8(0x00af),
				hw_->HD63701Read8(0x00b0),
				hw_->HD63701Read8(0x1182),
				hw_->HD63701Read8(0x8000),
				hw_->HD63701Read8(0x14f0), hw_->HD63701Read8(0x14f1),
				hw_->HD63701Read8(0x14f8), hw_->HD63701Read8(0x14f9));
		}
		Sys86RunCycles(cpuHz_ / 2);
		/* CUS60 ブートが AE インストールを飛ばしたら（拡張マップ競合）種まき */
		if (hw_->HD63701Read8(0x00ae) == 0 && hw_->HD63701Read8(0x00af) == 0)
			hw_->SetSoundCommand(0); /* 注入経路も AE／表を復元 */
		hw_->SetSoundCommand(0);
		Sys86RunCycles(cpuHz_ / 4);
		hw_->SetSoundCommand(0); /* 2 回目の停止で $1182 をクリアのまま */
		Sys86RunCycles(cpuHz_ / 8);
		if (ge && ge->archive && _stricmp(ge->archive, "drgnbstr") == 0
			&& songCmd_ == 0x01u)
			songCmd_ = 0x05u; /* ROUND START ワンショット → BGM_B vs BGM_A 0x06 */
		if (ge && ge->archive && _stricmp(ge->archive, "roishtar") == 0
			&& songCmd_ == 0x02u)
			songCmd_ = 0x06u; /* メインテーマ短い → Knox vs Druaga A 0x0E */
		hw_->SetSoundCommand(songCmd_);
		cmdIndex_ = 1;
		Sys86RunCycles(cpuHz_);
		Sys86RunCycles(cpuHz_ / 2);
		if (getenv("CEMU_SYS86_TRACE")) {
			HD63701Cpu* cpu = hw_->HD63701CpuPtr();
			unsigned char opm[256];
			memset(opm, 0, sizeof(opm));
			if (hw_->SoundChip())
				CEmuChipYm2151PeekRegs(hw_->SoundChip(), opm);
			fprintf(stderr,
				"sys86 arch=%s cmd=%02X pc=%04X irq=%u ymW=%u keyOn=%u AE=%02X%02X A8=%02X%02X +31=%02X B0=%02X door=%02X song=%02X 14F8=%02X%02X 1380=%02X\n",
				ge && ge->archive ? ge->archive : "?",
				(unsigned)songCmd_,
				cpu ? HD63701Pc(cpu) : 0,
				cpu ? HD63701IrqCount(cpu) : 0,
				hw_->HD63701YmWrites(),
				(unsigned)CEmuChipYm2151KeyOnCount(hw_->SoundChip()),
				hw_->HD63701Read8(0x00ae), hw_->HD63701Read8(0x00af),
				hw_->HD63701Read8(0x00a8), hw_->HD63701Read8(0x00a9),
				hw_->HD63701Read8((uint16_t)(0x0000u
					+ ((unsigned)hw_->HD63701Read8(0x00ae) << 8)
					+ hw_->HD63701Read8(0x00af) + 0x31u)),
				hw_->HD63701Read8(0x00b0),
				hw_->HD63701Read8(0x1182), hw_->HD63701Read8(0x1183),
				hw_->HD63701Read8(0x14f8), hw_->HD63701Read8(0x14f9),
				hw_->HD63701Read8(0x1380));
			fprintf(stderr, "  fault1185=%02X opm08=%02X 20=%02X 28=%02X 60=%02X 80=%02X e0=%02X tcsr=%02X\n",
				hw_->HD63701Read8(0x1185),
				opm[0x08], opm[0x20], opm[0x28], opm[0x60], opm[0x80], opm[0xe0],
				hw_->HD63701Read8(0x0008));
			{
				int extra = 0;
				unsigned p2 = cpu ? HD63701Pc(cpu) : 0;
				while (extra < 8) {
					const uint8_t b0b = hw_->HD63701Read8(0x00b0);
					const int idleb = (p2 >= 0x8128u && p2 < 0x9000u);
					if (b0b && idleb)
						break;
					Sys86RunCycles(cpuHz_);
					extra++;
					p2 = HD63701Pc(cpu);
				}
				fprintf(stderr,
					"  post extra=%ds pc=%04X A8=%02X%02X B0=%02X door=%02X song=%02X 1380=%02X 14F8=%02X%02X\n",
					extra, p2,
					hw_->HD63701Read8(0x00a8), hw_->HD63701Read8(0x00a9),
					hw_->HD63701Read8(0x00b0),
					hw_->HD63701Read8(0x1182), hw_->HD63701Read8(0x1183),
					hw_->HD63701Read8(0x1380),
					hw_->HD63701Read8(0x14f8), hw_->HD63701Read8(0x14f9));
			}
		}
		booted_ = 1;
		triggered_ = 1;
		nextCmdAt_ = (uint64_t)~0ull;
		return 1;
	}

	/* Namco System 12 / ND-1: H8/3002 + C352。Sys11/22/NA1: M37702（基板） */
	h8Board_ = (hw_->board_ == CEMU_AC_BOARD_NAMCO_C352 && hw_->H8Active()) ? 1 : 0;
	m37702Board_ = (hw_->board_ == CEMU_AC_BOARD_NAMCO_C352 && hw_->M37702Active()) ? 1 : 0;
	if (hw_->board_ == CEMU_AC_BOARD_NAMCO_C352) {
		hasCpu_ = (h8Board_ || m37702Board_) ? 1 : 0;
		if (h8Board_) {
			h8Acc_ = 0;
			cmdIndex_ = 0;
			/* ドライバ自身の init（無音）で温め、曲を投稿して止める。要求より先に CPU を走らせると描画音声より常に先行し、FmMon はライブレジスタ影を読むので鍵盤がその分早く点灯する。 */
			H8RunCycles(cpuHz_);
			H8RunCycles(cpuHz_ / 2);
			TryInjectCommand();
			booted_ = 1;
			triggered_ = 1;
			nextCmdAt_ = (uint64_t)~0ull;
			return 1;
		}
		if (m37702Board_) {
			m37702Acc_ = 0;
			cmdIndex_ = 0;
			/* 上の H8 経路と同じ: 要求後に走らせると曲全体で CPU が音声より先行したまま */
			M37702RunCycles(cpuHz_);
			M37702RunCycles(cpuHz_ / 2);
			TryInjectCommand();
			booted_ = 1;
			triggered_ = 1;
			nextCmdAt_ = (uint64_t)~0ull;
			return 1;
		}
		if (hw_->M37702Soft()) {
			/* ソフトフォールバック: CPU 無し — 無音のまま */
			hasCpu_ = 0;
			cmdIndex_ = 0;
			booted_ = 1;
			triggered_ = 1;
			nextCmdAt_ = (uint64_t)~0ull;
			return 1;
		}
		booted_ = 1;
		return 1;
	}

	/* Irem M92 は暗号化 NEC V35 を回し Z80 ではない */
	m92_ = (hw_->board_ == CEMU_AC_BOARD_IREM_M92) ? 1 : 0;
	if (m92_) {
		if (!hw_->M92Active()) return 0;
		m92Acc_ = 0;
		m92OpmRes_ = 0;
		m92NoteOffSeen_ = 0;
		m92ChannelPlayOff_ = 0;
		m92NoteStuck_ = 0;
		/* 長いブートのあとプリフィルし、Rev 3.40+（暗号化 JMP FAR で低 ROM へ）で IMC/DS/SS を用意。init を飛ばすと曲ゲート [0316]/[0317] とドレイン [0319] はまだホスト priming が要る。 */
		cmdIndex_ = 0;
		/* ワードキュー IMC（firebarr/nbbatman 等）: メイン CPU が ready するまで [0C31]==3 でアイドル。解放が無いとフリーリストが作られず全 BGM 割当が空。 */
		if (hw_->M92WordQueue())
			hw_->M92Write8(0xa0c31u, 0x03);
		M92RunCycles(cpuHz_);
		M92RunCycles(cpuHz_ / 2);
		M92RunCycles(cpuHz_ / 2); /* Rev3.40 フリーリスト／IMC は約 2s 要ることがある */
		/* 共有 Irem 音源シーケンサ: 曲ゲートは [0316]/[0317] を比較。コマンドドレインは [0319]!=0 のときだけ成功。lethalth / bmaster init はここに FF。gunforce/uccops はホストブートでその経路を飛ばしがち — 全セットで同じ 3 バイトを priming。 */
		hw_->M92Write8(0xa0316u, 0x00);
		hw_->M92Write8(0xa0317u, 0xff);
		hw_->M92Write8(0xa0319u, 0xff);
		/* lethalth/gunforce/bmaster は IMC init 後に [0310]/[0311] の受理フラグを立てる。クリアのまま止まるとラッチを落とし 0280 キューを書かないセットがある。 */
		hw_->M92Write8(0xa0310u, 0x01);
		hw_->M92Write8(0xa0311u, 0x01);
		/* Rev 3.40+ リング dequeue は (mask & cmd) != 0 のときだけ cmd を返す。バイトリング: rtypeleo/hook [09EA]、uccops [09EF]。ワードリング: nbbatman/firebarr [0C31]（ready セマフォでもある）。 */
		hw_->M92Write8(0xa09eau, 0xff);
		hw_->M92Write8(0xa09efu, 0xff);
		if (hw_->M92WordQueue()) {
			/* IMC フリーリストは [0B12]/[0B92]/[0B93]（0x00A0 から stride 0x50 ×32）。ブートは [0C31]==3 待ちの前に CALL 0597 で作るが、ホストブートでは注入時まだ空 — ROM init と同じ配置を種まき。 */
			const uint8_t* ram = hw_->M92Ram();
			if (ram && ram[0xb93] == 0 && ram[0xb92] == 0) {
				uint8_t* w = const_cast<uint8_t*>(ram);
				uint8_t wp = 0;
				for (unsigned i = 0; i < 0x20u; i++) {
					const unsigned bp = 0x00a0u + i * 0x50u;
					const unsigned off = 0xb12u + (((unsigned)wp & 0x3fu) << 1);
					w[off] = (uint8_t)(bp & 0xffu);
					w[off + 1u] = (uint8_t)(bp >> 8);
					wp = (uint8_t)((wp + 1u) & 0x3fu);
				}
				w[0xb93] = wp;
				w[0xb92] = 0;
			}
			/* Dequeue の RET は CMP [0C32],#0 の ZF。ZF セットだと呼び出し側が cmd を捨てる。ブートは [0C32]=0。ハンドシェイクが上げる。wpksoc は [0C32] を AND マスク（nbbatman は [0C31]）。0x01 は奇数カタログ id だけ許し BGM #02 が無音。 */
			hw_->M92Write8(0xa0c32u, 0xff);
			hw_->M92Write8(0xa0c31u, 0xff);
		}
		TryInjectCommand();
		M92RunCycles(cpuHz_ / 4);
		booted_ = 1;
		/* ラッチは 1 回だけ — 再試行はチャネル BGM をフレーズ途中で中断（WEAK） */
		nextCmdAt_ = (uint64_t)~0ull;
		return 1;
	}

	if (ms1_) {
		if (!hw_->Ms1Active()) return 0;
		ms1Acc_ = 0;
		if (hw_->board_ == CEMU_AC_BOARD_KONAMI_GX
			|| hw_->board_ == CEMU_AC_BOARD_KONAMI_RF5C400) {
			/* ホストパケット投稿前にブート自己テストと K056800 IRQ 許可を終える — 早い注入は捨てられる */
			Ms1RunCycles(cpuHz_);
			Ms1RunCycles(cpuHz_ / 2);
			Ms1RunCycles(cpuHz_ / 2);
			if (hw_->board_ == CEMU_AC_BOARD_KONAMI_GX
				&& !(hw_->GxSoundCtrl() & 1)) {
				hw_->Ms1Write8(0x500001u, 0xff);
				Ms1RunCycles(cpuHz_ / 4);
			}
			booted_ = 1;
			cmdIndex_ = 0;
			TryInjectCommand();
			Ms1RunCycles(cpuHz_);
			nextCmdAt_ = (uint64_t)~0ull;
			return 1;
		}
		Ms1RunCycles(cpuHz_ / 2); /* ブート: RAM クリア、YM2151/OKI を組む */
		booted_ = 1;
		TryInjectCommand();
		nextCmdAt_ = (uint64_t)~0ull;
		return 1;
	}

	/* Seibu SEI80BU: ブートは長い RAM クリア（LDIR @00CC）のあとバンク init。汎用 settle は IFF1 クリアのままクリア途中に PC を残しがち。3 つの LDIR を過ぎ CALL/EI へ強制し、RST18 をエッジ。 */
	if (hw_->board_ == CEMU_AC_BOARD_SEIBU_OPL) {
		hasCpu_ = 1;
		/* カタログタイトルを優先。SetSoundCommand は 0x81→表 0x8b */
		if (!songCmd_)
			songCmd_ = 0x80;
		CEmuHardAcSetActive(hw_);
		/* 3 つのブート LDIR が終わるまで約 0.25s 待ってから合成 */
		RunUntil((uint64_t)cpuHz_ / 4);
		{
			Ay_Cpu* c = hw_->Cpu();
			uint8_t* m = hw_->Mem();
			if (c && m) {
				const unsigned pc = (unsigned)c->r.pc;
				/* ブート LDIR（00CC/00D7/00E7）に固まったらクリア後を合成 */
				if (pc >= 0x00BFu && pc <= 0x00E8u) {
					memset(m + 0x2000, 0, 0x800);
					memcpy(m + 0x2463, m + 0x1303, 0x24);
					memcpy(m + 0x2487, m + 0x1327, 0x10);
					m[0x2021] = 0x09;
					m[0x2046] = 0xff;
					m[0x2221] = 0xff;
					m[0x2326] = 0x04;
					c->r.sp = 0x2800;
					c->r.pc = 0x00E9;
					c->r.w.bc = 0;
				}
			}
		}
		RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 2);
		{
			Ay_Cpu* c = hw_->Cpu();
			for (int i = 0; i < 16 && c && !c->r.iff1; i++) {
				RunUntil((uint64_t)c->time64() + (uint64_t)cpuHz_ / 8);
				if ((unsigned)c->r.pc >= 0x0119u && !c->r.iff1) {
					c->r.iff1 = 1;
					c->r.iff2 = 1;
					break;
				}
				/* まだ EI 前なら init CALL が settle したあとそこへ飛ぶ */
				if ((unsigned)c->r.pc >= 0x0100u && (unsigned)c->r.pc < 0x0119u
					&& i >= 4) {
					c->r.pc = 0x0119;
					c->r.iff1 = 1;
					c->r.iff2 = 1;
					break;
				}
			}
			if (c && !c->r.iff1) {
				c->r.iff1 = 1;
				c->r.iff2 = 1;
			}
		}
		booted_ = 1;
		/* メインループ 0126 のスピンを解除 — 通常は YM RST10 ISR が立てる */
		if (uint8_t* m = hw_->Mem()) {
			m[0x201c] = 0xff;
			m[0x201d] = 0xff;
		}
		hw_->SetSoundCommand(songCmd_);
		cmdIndex_ = 1;
		triggered_ = 1;
		/* Render 前に 0x80 スキャン／054A 割当を終える */
		RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ * 2u);
		if (uint8_t* m = hw_->Mem())
			m[0x201c] = 0xff;
		nextCmdAt_ = (uint64_t)~0ull;
		return 1;
	}

	/* MAME irem/m72.cpp: MASTER_CLOCK/8/512 = 7812.5 Hz の周期 NMI が tick 毎に PCM 1 バイト。空 NMI ハンドラ（airduel、gallop、poundfor 等）は同じ転送をホスト側で（fake_nmi）。 */
	nextM72Nmi_ = 0;
	m72FakeNmi_ = 0;
	if (hw_->board_ == CEMU_AC_BOARD_IREM_M72) {
		const uint8_t h0 = hw_->PeekMem(0x0066);
		const uint8_t h1 = hw_->PeekMem(0x0067);
		/* MAME: R-Type の NMI は DI;HALT（サンプル無し）。他は RET / RETN / NOP */
		if (h0 == 0xc9 || h0 == 0x00 || h0 == 0xff || h0 == 0x76
			|| (h0 == 0xed && h1 == 0x45)
			|| (h0 == 0xf3 && h1 == 0x76))
			m72FakeNmi_ = 1;
	}

	/* ブート settle 約 0.5s。init が RAM をクリアし OPM を組む */
	uint64_t bootCycles = (uint64_t)cpuHz_ / 2;
	if (hw_->board_ == CEMU_AC_BOARD_CPS_QS)
		nextGngIrq_ = (uint64_t)cpuHz_ / 250; /* MAME: 8MHz/32000 → 250 Hz（周期） */
	else if (hw_->board_ == CEMU_AC_BOARD_ALPHA68K2)
		nextGngIrq_ = 0; /* ポート A が許可したらすぐ NMI（ブート OUT 0E,0） */
	else
		nextGngIrq_ = (uint64_t)cpuHz_ / 240;
	/* CPS1/2 QSound: init は共有 CFFF==0xFF（68K ready）までスピン／HALT。メイン CPU が無いとその待ちが Ay_Cpu HALT を数分ブートにする（RunOne は呼出あたり約 4 クロック）。settle 前に解放。Kabuki セット（dino/wof）はこの経路を本物で踏む。暗号化ゴミは踏まなかった。 */
	if (hw_->board_ == CEMU_AC_BOARD_CPS_QS) {
		if (uint8_t* m = hw_->Mem()) {
			m[0xcfff] = 0xff;
			m[0xcffd] = 0x00;
		}
	}
	/* Bosco WSG: どの NMI も push する前に LD SP,$8000 → $9F00 をパッチ */
	if (hw_->board_ == CEMU_AC_BOARD_NAMCO_WSG && hw_->Cpu()) {
		uint8_t* m = hw_->Mem();
		if (m && m[0] == 0x31 && m[1] == 0x00 && m[2] == 0x80) {
			m[1] = 0x00;
			m[2] = 0x9f;
			m[0x8c01] = 0;
			hw_->Cpu()->r.sp = 0x9f00;
		}
	}
	/* Pengo: $8C60 ボイスの RAM クリア + EI @0430 を待ってから注入 */
	if (hw_->board_ == CEMU_AC_BOARD_NAMCO_WSG && hw_->PengoWsg() && hw_->Cpu()) {
		for (int i = 0; i < 240; i++) {
			RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 60);
			uint8_t* m = hw_->Mem();
			if (hw_->Cpu()->r.iff1 && m && (m[0x9040] & 1u))
				break;
		}
	}
	/* Toaplan1: メイン CPU が共有 RAM (8001)==0xAA を保持。Truxton/hellfire/demonwld/zerowing/outzone は 0 を書いて AA 待ち — RunUntil 前のワンショット poke は消え Z80 は DI ブートから出ない。snowbros Kaneko I/O に 8001 ハンドシェイクは無い — 飛ばさないと NMI@reset が未設定 SP に当たる。slapfght は (C801)==0xAA 待ち、8K ROM チェックサム、NMI 許可。 */
	if (hw_->board_ == CEMU_AC_BOARD_TOAPLAN1 && hw_->SlapfghtAy()) {
		hw_->SetSoundCommand(0xff);
		for (int i = 0; i < 400; i++) {
			if (uint8_t* m = hw_->Mem())
				m[0xc801] = 0xaa;
			RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 200);
			if (hw_->FlstoryNmiEn())
				break;
		}
	} else if (hw_->board_ == CEMU_AC_BOARD_TOAPLAN1 && !hw_->ToaplanKaneko()) {
		hw_->SetSoundCommand(0xff);
		const unsigned ready = hw_->ToaplanReady();
		for (int i = 0; i < 500; i++) {
			if (uint8_t* m = hw_->Mem())
				m[(uint16_t)ready] = 0xaa;
			RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 500);
			Ay_Cpu* c = hw_->Cpu();
			if (!c) break;
			const unsigned pc = (unsigned)c->r.pc;
			const uint8_t b0 = hw_->PeekMem((uint16_t)pc);
			const uint8_t b1 = hw_->PeekMem((uint16_t)(pc + 1u));
			int idle = 0;
			if (b0 == 0xfb && b1 == 0xc3) {
				const unsigned tgt = (unsigned)hw_->PeekMem((uint16_t)(pc + 2u))
					| ((unsigned)hw_->PeekMem((uint16_t)(pc + 3u)) << 8);
				idle = (tgt == pc);
			} else if (b0 == 0xc3) {
				const unsigned tgt = (unsigned)b1
					| ((unsigned)hw_->PeekMem((uint16_t)(pc + 2u)) << 8);
				idle = (tgt == pc);
			} else if (b0 == 0x18 && b1 == 0xfe) {
				idle = 1;
			}
			if (idle && c->r.iff1)
				break;
		}
	} else {
		/* GX400: 4000-7FFF 自己テスト中に共有 RAM を触らない — 検証が壊れ Z80 が楔。PC がテストを抜けてから (7FFC)==4 メイン CPU ハンドシェイクを解放。 */
		if (hw_->board_ == CEMU_AC_BOARD_KONAMI_GX400) {
			for (int i = 0; i < 360; i++) {
				RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 60);
				Ay_Cpu* c = hw_->Cpu();
				const unsigned pc = c ? (unsigned)c->r.pc : 0;
				/* (7FFC)==4 の解放は RAM 自己テスト後（PC>=0x200）だけ。AY1 ポート A は Gx400PortA() — 強制クリアしない。 */
				if (pc >= 0x0200u && pc < 0x8000u) {
					if (uint8_t* m = hw_->Mem()) {
						m[0x7ffc] = 0x04;
						m[0x7ffd] = 0x02;
						m[0x7ffe] = 0x02;
					}
				}
				if (c && c->r.iff1 && pc >= 0x0290u)
					break;
			}
		} else {
			RunUntil(bootCycles);
		}
	}
	booted_ = 1;
	/* Sys16B（goldnaxe）: bit7 ラッチ cmd は F818 にキュー、空=0x80。ブート LDIR が F800-FFFF を 00 にし、0212 が空き枠を見つけず全 BGM を落とす。02C7 が一度走るまで — その頃ラッチ端は一部セットで消える。ドレイン後 02D4 が書く同じ空マーカを種まき。 */
	if (hw_->board_ == CEMU_AC_BOARD_SYS16B) {
		if (uint8_t* m = hw_->Mem()) {
			if (m[0xf818] == 0x00 && m[0xf819] == 0x00) {
				m[0xf818] = 0x80;
				m[0xf819] = 0x80;
			}
		}
	}
	/* Taito PC060HA / TC0140SYT ドライバはシーケンサ全体を 68000 がブートで出す「sound on」制御の後ろに置く。Rastan のノート開始 02FE は許可フラグ (8F26) が 0 なら即戻る。Bonze Adventure の 0413 も CF2C bit0 で同じ。両系統で $EF がフラグを立て $EE が消す。カタログは曲番号だけなのでここで許可を出し、曲コマンドの前に Z80 に消費させる。 */
	if (hw_->board_ == CEMU_AC_BOARD_TAITO_OPM
		|| hw_->board_ == CEMU_AC_BOARD_TAITO_YM2610) {
		if (hw_->board_ == CEMU_AC_BOARD_TAITO_OPM && hw_->TaitoOpmMap() == 2) {
			/* kikikai: 音源 CPU は [9FFF] が 0xFF でなくなるまで poll、そのあと A=cmd で DI;CALL 17D2。kTaitoTryCmds 走査はその待ち前にメールボックスを上書き。FF を種まき、poll に到達、カタログ曲を一度だけ植え、再注入しない。 */
			if (uint8_t* m = hw_->Mem())
				m[0x9fff] = 0xff;
			/* ブートチェックサム + CALL 182C（YM init、PC=182C）のあと 00BE の poll。00A0.. で切ると CALL 後の YM セットアップを捕まえ曲を植え、poll が FF を書いて永久待ち。 */
			for (int i = 0; i < 240; i++) {
				RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 60);
				const uint16_t pc = hw_->Cpu() ? (uint16_t)hw_->Cpu()->r.pc : 0;
				if (pc >= 0x00b8u && pc < 0x00c8u)
					break;
			}
			/* 0xEF が (AFA1)=1。無いと 190F は即戻る */
			hw_->SetSoundCommand(0xef);
			for (int i = 0; i < 60; i++) {
				RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 60);
				if (hw_->PeekMem(0x9fff) == 0xff)
					break;
			}
			hw_->SetSoundCommand(songCmd_ ? songCmd_ : (uint8_t)0x06);
			cmdIndex_ = 1;
			triggered_ = 1;
			RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 5);
			nextCmdAt_ = (uint64_t)~0ull;
			return 1;
		} else if (hw_->board_ == CEMU_AC_BOARD_TAITO_OPM && hw_->TaitoOpmMap() == 7) {
			/* 旧 TNZS: PC060HA 無し。tnzsjo は (EF11)==1 待ちのあと EF10 を poll。chukatai ハンドシェイクは E003=55 のち AA。kageki は E03E を読む。 */
			const uint8_t b3 = hw_->PeekMem(3);
			if (b3 == 0x21) {
				if (uint8_t* m = hw_->Mem())
					m[0xe003] = 0x55;
				for (int i = 0; i < 90; i++) {
					RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 60);
					if (hw_->PeekMem(0xe004) == 0xaa) {
						if (uint8_t* m = hw_->Mem())
							m[0xe003] = 0xaa;
						break;
					}
				}
			} else if (b3 != 0x31) {
				if (uint8_t* m = hw_->Mem()) {
					m[0xef11] = 1;
					/* サブ CPU は 0082 でスプライト／オブジェクトリストも歩く。メイン CPU が無いとリストはゴミのまま ISR が戻らない（D000 が 1 のまま、PC=0553）。音楽は CALL 006D。 */
					if (m[0x55] == 0xcd && m[0x56] == 0x82 && m[0x57] == 0x00) {
						m[0x55] = 0x00;
						m[0x56] = 0x00;
						m[0x57] = 0x00;
					}
				}
				for (int i = 0; i < 90; i++) {
					RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 60);
					if (hw_->PeekMem(0xef11) == 0)
						break;
				}
			}
			for (int i = 0; i < 30; i++)
				RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 60);
			if (Ay_Cpu* c = hw_->Cpu()) {
				c->r.iff1 = 1;
				c->r.iff2 = 1;
			}
			/* tnzsjo 07FB は (DFA1)==0 の間即戻る。コマンド 0xEF（0855）が立て、0xEE が消す。kikikai AFA1 と同じ許可。 */
			if (b3 == 0xfd) {
				hw_->SetSoundCommand(0xef);
				for (int i = 0; i < 60; i++) {
					RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 60);
					if (hw_->PeekMem(0xdfa1))
						break;
				}
				if (!hw_->PeekMem(0xdfa1)) {
					if (uint8_t* m = hw_->Mem())
						m[0xdfa1] = 1;
				}
			}
			hw_->SetSoundCommand(songCmd_ ? songCmd_ : (uint8_t)0x06);
			cmdIndex_ = 1;
			triggered_ = 1;
			RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 5);
			nextCmdAt_ = (uint64_t)~0ull;
			return 1;
		} else if (hw_->board_ == CEMU_AC_BOARD_TAITO_OPM
			&& (hw_->TaitoOpmMap() == 3 || hw_->TaitoOpmMap() == 4
				|| hw_->TaitoOpmMap() == 5 || hw_->TaitoOpmMap() == 6)) {
			/* tokio/bublbobl/lsasquad: NMI マージはラッチ pending 線が撃つ前に許可が要る。ここでの 0xEF は PC060HA ではない。 */
			for (int i = 0; i < 180 && !hw_->FlstoryNmiEn(); i++)
				RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 60);
			hw_->SetSoundCommand(0xef);
			for (int i = 0; i < 30; i++)
				RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 60);
			hw_->SetSoundCommand(songCmd_ ? songCmd_ : (uint8_t)0x07);
			cmdIndex_ = 1;
			triggered_ = 1;
			RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 5);
			nextCmdAt_ = (uint64_t)~0ull;
			return 1;
		} else {
		hw_->SetSoundCommand(0xef);
		RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 10);
		/* Rastan/Asuka は [8F26] でノート開始をゲート。F2/Bonze は [CF2C]/[CF30]。ホスト EF は Z80 が DI;OPM 窓に居る間未読になり得る — 許可フラグを強制。Asuka は 8F02 リングに曲バイトも要る（NMI enqueue @00C0）。無いと 0254 が空キューを永久ドレイン。 */
		if (uint8_t* m = hw_->Mem()) {
			if (hw_->board_ == CEMU_AC_BOARD_TAITO_OPM && hw_->TaitoOpmMap() == 0) {
				/* Rastan/Asuka: 許可 @8F26。masterw/viofight YM2203: EF が 8F25 へ 0x07 — ノートゲートがそのバイトを見る。 */
				m[0x8f26] = 0x01;
				if (hw_->MainIsYm2203())
					m[0x8f25] = 0x07;
				if (hw_->MainIsYm2203()) {
					/* masterw: 8F26 bit0 セットだと 033A は 8F27 へハンドシェイクをキューするだけ — CALL 0388 しない。bit0 をクリアし曲を 8F02 リングへ入れ、033A に <0x35 経路（実際にボイス開始）を取らせてから許可を戻す。 */
					if (Ay_Cpu* cpu = hw_->Cpu()) {
						while (hw_->IrqPulsePending())
							hw_->TakeIrqPulse();
						m[0x8f26] = 0x00;
						const uint8_t rd = (uint8_t)(m[0x8f01] & 0x0f);
						const uint8_t wr = (uint8_t)((rd + 1) & 0x0f);
						m[0x8f00] = wr;
						m[0x8f01] = rd;
						m[0x8f02 + wr] = songCmd_;
						/* メインループ CALL 033A 地点からドレイン */
						const uint16_t sp0 = cpu->r.sp;
						const uint16_t sp = (uint16_t)(sp0 - 2);
						m[(sp + 0) & 0xffffu] = 0x73; /* 戻り @0273 */
						m[(sp + 1) & 0xffffu] = 0x02;
						cpu->r.sp = sp;
						cpu->r.iff1 = 0;
						cpu->r.iff2 = 0;
						cpu->irqDelay = 0;
						cpu->r.pc = 0x033a;
					}
					RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 30);
					if (uint8_t* mm = hw_->Mem()) {
						mm[0x8f25] = 0x07;
						mm[0x8f26] = 0x01;
					}
					if (Ay_Cpu* cpu = hw_->Cpu()) {
						cpu->r.iff1 = 1;
						cpu->r.iff2 = 1;
					}
				} else {
					/* NMI の CALL 00C0 と同じく 8F02 リングへ enqueue: 書込 ptr を進め、8F02+ptr（下位ニブル）に cmd を格納 */
					const uint8_t rd = (uint8_t)(m[0x8f01] & 0x0f);
					const uint8_t wr = (uint8_t)((rd + 1) & 0x0f);
					m[0x8f00] = wr;
					m[0x8f01] = rd;
					m[0x8f02 + wr] = songCmd_;
				}
				/* Timer A/B を再武装し EI;DI メインループが status&3 を処理できるように。YM2151 は 0x10-0x14。YM2203（masterw）は 0x24-0x27 — OPM レジスタを OPN へ書くと st=00 のまま。 */
				if (hw_->SoundChip()) {
					CChip* ym = hw_->SoundChip();
					if (hw_->MainIsYm2203()) {
						ym->Write(0, 0x24); ym->Write(1, 0xff);
						ym->Write(0, 0x25); ym->Write(1, 0x00);
						ym->Write(0, 0x26); ym->Write(1, 0xc0);
						ym->Write(0, 0x27); ym->Write(1, 0x35);
					} else {
						ym->Write(0, 0x10); ym->Write(1, 0xff);
						ym->Write(0, 0x11); ym->Write(1, 0x00);
						ym->Write(0, 0x12); ym->Write(1, 0xc0);
						ym->Write(0, 0x14); ym->Write(1, 0x35);
					}
				}
			} else if (hw_->board_ == CEMU_AC_BOARD_TAITO_YM2610) {
				m[0xcf2c] = (uint8_t)(m[0xcf2c] | 0x07);
				m[0xcf30] = (uint8_t)(m[0xcf30] | 0x07);
			}
		}
		RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 20);
		}
	}
	/* Irem M72: airduel 族は $00 が ready（FF56）を武装するまでラッチバイトを捨てる。R-Type は逆 — $00 は STOP で DI;HALT（NMI ベクタは既に F3 76）。曲前 $00 は Z80 を凍らせ後続コマンドを失う。poundfor/bbmanw（YM@40、RETN NMI）も $00 を STOP。airduel も RETN/空 NMI だが $00 武装が必須 — ROM がポート 40 の YM を話すときだけ飛ばす。 */
	if (hw_->board_ == CEMU_AC_BOARD_IREM_M72) {
		const uint8_t n0 = hw_->PeekMem(0x0066);
		const uint8_t n1 = hw_->PeekMem(0x0067);
		const int rtypeStopHalts = (n0 == 0xf3 && n1 == 0x76) || n0 == 0x76;
		int ym40 = 0;
		for (unsigned i = 0; i + 1 < 0x400u; i++) {
			if (hw_->PeekMem((uint16_t)i) == 0xd3
				&& hw_->PeekMem((uint16_t)(i + 1)) == 0x40) {
				ym40 = 1;
				break;
			}
		}
		if (!rtypeStopHalts && !ym40) {
			hw_->SetSoundCommand(0x00);
			RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 20);
		}
		/* m99（bbmanw/poundfor）: ブートはリセットで XOR A のあと PUSH AF するのでコマンドマスク（F4DC または FF56）が 0。読は全ラッチバイトを捨てる。dynablst は $00 ハンドシェイクが FF56 を priming。ym40 セットはそれを飛ばす — ここで両マスクを poke。poundfor/dynablst メインループは FF57/FF58 非 0 のときだけコマンドリングをドレイン（YM ISR が加算）。ソフトキック。 */
		if (ym40 || hw_->M72IoAlt()) {
			if (uint8_t* m = hw_->Mem()) {
				if (m[0xf4dc] == 0x00)
					m[0xf4dc] = 0xff;
				if (m[0xff56] == 0x00)
					m[0xff56] = 0x20;
				if (m[0xff57] == 0x00)
					m[0xff57] = 0x08;
				if (m[0xff58] == 0x00)
					m[0xff58] = 0x08;
			}
		}
	}
	/* flstory: ブート LD A,(D800) @0103 が許可前ラッチを食う（NMI 無しで pending クリア）。hcastle: DI RAM テスト中の ForceIm1 が入れ子。どちらも最初の曲注入前に settle した EI/NMI 許可が要る。Toaplan1 は下のブート settle 後に注入（Timer-A ISR メールボックス）。 */
	if (hw_->HalleysAy()) {
		/* ブート LDIR が 4000-47FF（コマンドリング＋許可）をクリア。早い NMI enqueue は消え、01D7/05CD は (4717)/(429A)==0 の間戻る。コマンド 0xEF がそのフラグを 0x80 に。0xEE が消す。 */
		for (int i = 0; i < 90; i++) {
			RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 60);
			Ay_Cpu* c = hw_->Cpu();
			const unsigned pc = c ? (unsigned)c->r.pc : 0;
			if (c && c->r.iff1 && pc >= 0x0140u && pc < 0x0180u)
				break;
		}
		hw_->SetSoundCommand(0xef);
		const uint16_t flag = (hw_->PeekMem(4) == 0xe9) ? (uint16_t)0x4717u
			: (uint16_t)0x429au;
		for (int i = 0; i < 60; i++) {
			RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 60);
			if (hw_->PeekMem(flag))
				break;
		}
		if (!hw_->PeekMem(flag)) {
			if (uint8_t* m = hw_->Mem())
				m[flag] = 0x80;
		}
		hw_->SetSoundCommand(songCmd_ ? songCmd_ : (uint8_t)0x06);
		cmdIndex_ = 1;
		triggered_ = 1;
		RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 5);
	}
	if (hw_->PbactionAy()) {
		/* ブートは $0067 の CTC init + EI まで DI。IM2 ch0 がラッチを $4243 へコピー。ch1 の 126Hz ISR が消費。IM 2 / I=1 を待つ。 */
		for (int i = 0; i < 60; i++) {
			RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 60);
			Ay_Cpu* c = hw_->Cpu();
			if (c && c->r.iff1 && c->r.im == 2 && c->r.i == 1)
				break;
		}
		hw_->SetSoundCommand(songCmd_ ? songCmd_ : (uint8_t)0x0a);
		cmdIndex_ = 1;
		triggered_ = 1;
		RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 5);
	}
	if (hw_->ChaknpopAy()) {
		/* ブートは 0000-7FFF をチェックサム（和は 0）。rst $20 が 0x50 で mute。そのあと IM 1 / EI と $8400=3。EI 後にその mute を置換。 */
		for (int i = 0; i < 180; i++) {
			RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 60);
			Ay_Cpu* c = hw_->Cpu();
			if (c && c->r.iff1 && c->r.im == 1 && hw_->PeekMem(0x8400) == 3)
				break;
		}
		hw_->SetSoundCommand(songCmd_ ? songCmd_ : (uint8_t)0x21);
		cmdIndex_ = 1;
		triggered_ = 1;
		RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 5);
	}
	if (!(hw_->board_ == CEMU_AC_BOARD_KONAMI_PCM && hw_->PcmKind() == 4)
		&& hw_->board_ != CEMU_AC_BOARD_SNK_OPL
		&& hw_->board_ != CEMU_AC_BOARD_FLSTORY
		&& hw_->board_ != CEMU_AC_BOARD_KONAMI_HCASTLE
		&& hw_->board_ != CEMU_AC_BOARD_TOAPLAN1
		&& hw_->board_ != CEMU_AC_BOARD_RAIZING
		&& hw_->board_ != CEMU_AC_BOARD_TECMO16
		&& !hw_->HalleysAy()
		&& !hw_->PbactionAy()
		&& !hw_->ChaknpopAy()
		&& !(hw_->board_ == CEMU_AC_BOARD_KONAMI_PCM && hw_->PcmKind() == 3))
		TryInjectCommand();
	/* Raizing / Eighting: 全改訂が RAM テストを通り、割り込んではいけない（Battle Bakraid は未設定 SP へ push し自テスト失敗）。音源 ROM がコマンドループに達してから投稿。Battle Garegga と Batrider は曲コードを見る前に 0x55 / 0xAA ハンドシェイク解除が要る。TryInjectCommand は RaizingHandshakeAcked が立つまでプローブを送る。 */
	if (hw_->board_ == CEMU_AC_BOARD_RAIZING) {
		const int type = hw_->RaizingType();
		for (int i = 0; i < 400; i++) {
			Ay_Cpu* c = hw_->Cpu();
			if (!c) break;
			/* mahoudai ブートはメールボックス先頭に 0xFE を置き、2 バイト目の 68000 応答 0xFE を待ってから ROM チェックサム。直後の RAM テストが同じバイトを書き直して検証するので、Z80 が実際に待ち（0071-0075）に居るときだけ答える。 */
			if (type == 1 && (unsigned)c->r.pc >= 0x0071u
				&& (unsigned)c->r.pc <= 0x0075u) {
				if (uint8_t* m = hw_->Mem())
					m[0xc001] = 0xfe;
			}
			RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 400);
			c = hw_->Cpu();
			if (!c) break;
			/* Type 1 は永久に割り込みオフ。ready 信号はループが生きたあとメールボックスに置く 0xFF。 */
			if (type == 1 ? hw_->RaizingMailboxIdle() : c->r.iff1 != 0)
				break;
		}
		for (int i = 0; i < 60 && !hw_->RaizingHandshakeAcked(); i++) {
			TryInjectCommand();
			cmdIndex_ = 0; /* プローブは曲試行の一つではない */
			RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 200);
		}
		TryInjectCommand();
		RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 5);
	}
	/* 最初のホスト Render 前に RST 18h がラッチをドレイン */
	if (hw_->board_ == CEMU_AC_BOARD_IREM_M72)
		RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 20);
	/* Konami AY: Open 注入が irqPulse_ を武装 — ForceIm1 が 0038 に入り、Render ハント前に音楽エンジンがチャネルを取れるように回す。 */
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_SCRAMBLE
		|| hw_->board_ == CEMU_AC_BOARD_KONAMI_TIMEPLT
		|| hw_->board_ == CEMU_AC_BOARD_KONAMI_GX400
		|| hw_->board_ == CEMU_AC_BOARD_TECHNOS_DDRAGON2
		|| hw_->board_ == CEMU_AC_BOARD_KONAMI_K7232
		|| hw_->board_ == CEMU_AC_BOARD_ALPHA68K2
		|| (hw_->board_ == CEMU_AC_BOARD_TECMO16 && hw_->TecmoOpl() != 5)
		|| hw_->board_ == CEMU_AC_BOARD_TERRACRE
		|| hw_->board_ == CEMU_AC_BOARD_ROBOKID
		|| hw_->board_ == CEMU_AC_BOARD_BATTLANTIS)
		RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 5);
	/* Tecmo rygar/silkworm: soundlatch は NMI。リセット注入（SP 未設定）は 0000 へ push し Z80 を殺した（idle dumps=516）。先にブート。Cave sailormn/agallet は EI 前に 32×16K バンクをチェックサム（約 1.2s）。 */
	if (hw_->board_ == CEMU_AC_BOARD_TECMO16) {
		if (hw_->TecmoOpl() == 5)
			RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ * 2ull);
		TryInjectCommand();
		RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 5);
	}
	/* Alpha: ラッチはメインループ（IN 00）から poll され NMI ではない。bank2 BIOS が poll に入ってから注入し、最初のホスト Render 前に RST 30 mode 3 が曲をロード。 */
	if (hw_->board_ == CEMU_AC_BOARD_ALPHA68K2) {
		TryInjectCommand();
		RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 5);
	}
	/* GX400 共有 RAM（4000-7FFF）は欠ける 68000 の所有。音源 ROM は自己テスト後 (7FFC)==4 を待って EI @0291 — 解放する。 */
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_GX400) {
		for (int i = 0; i < 240; i++) {
			Ay_Cpu* c = hw_->Cpu();
			const unsigned pc = c ? (unsigned)c->r.pc : 0;
			if (pc >= 0x0200u && pc < 0x8000u) {
				if (uint8_t* m = hw_->Mem()) {
					m[0x7ffc] = 0x04;
					m[0x7ffd] = 0x02;
					m[0x7ffe] = 0x02;
				}
			}
			RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 60);
			c = hw_->Cpu();
			/* メインループ @029C は AY タイマ bit2 待ち — IFF1 は不要 */
			if (c && (unsigned)c->r.pc >= 0x0290u && (unsigned)c->r.pc < 0x0340u)
				break;
		}
		TryInjectCommand();
		RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 5);
		TryInjectCommand();
		RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 5);
	}
	/* K053260: ブートは DI（reg 2E 経由 ROM チェックサム／YM タイマ poll）。EI が生きブートが ROM 走査／タイマ待ち stub を出たら K053260 ポート + IRQ0 で投稿。parodius は 8 バンク走査（約 3s）。 */
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_PCM && hw_->PcmKind() == 3) {
		for (int i = 0; i < 600; i++) {
			RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 60);
			Ay_Cpu* c = hw_->Cpu();
			if (!c) continue;
			const unsigned pc = (unsigned)c->r.pc;
			if (pc >= 0x06c0u && pc < 0x06f0u) continue;
			if (pc >= 0x00d0u && pc < 0x00e0u) continue;
			if (!c->r.iff1) continue;
			break;
		}
		/* 最初の曲 IRQ 前に進行中の ROM バンク走査を終える */
		for (int i = 0; i < 600; i++) {
			Ay_Cpu* c = hw_->Cpu();
			const unsigned pc = c ? (unsigned)c->r.pc : 0;
			if (pc < 0x06c0u || pc >= 0x06f0u) break;
			RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 60);
		}
		TryInjectCommand();
		RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 5);
		TryInjectCommand();
		RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 5);
	}
	if (hw_->board_ == CEMU_AC_BOARD_FLSTORY) {
		/* アイドル（DA00 NMI 許可 @0158）に達してから注入し、NMI が C300 をキュー */
		for (int i = 0; i < 120 && !hw_->FlstoryNmiEn(); i++)
			RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 60);
		/* 0xEF が (C51A) bit7。そのフラグが 0 の間 RST38 ドレイン @0169 は全曲を飛ばす（tokio と同じ EE/EF ゲート）。IM1 がドレイン／tick。nycaptor の 0xEF は E0 制御表経由の C719 bit0 であり CP EF ではない。 */
		nextGngIrq_ = 0;
		if (!hw_->MsisaacMap()) {
			hw_->SetSoundCommand(0xef);
			for (int i = 0; i < 30; i++)
				RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 60);
		}
		if (hw_->NycaptorMap()) {
			/* 0xEB: C719 bit3 = AY ミックスモード $B0（0xEF は music bit0 だけ） */
			hw_->SetSoundCommand(0xeb);
			for (int i = 0; i < 15; i++)
				RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 60);
		}
		TryInjectCommand();
		RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 5);
		TryInjectCommand();
		RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 5);
		/* MSM が話すまで温める。クローン（40love/fieldday/victnine）はカタログ prefer を mute のままにしがち — ピークが出るまで kFlstoryTryCmds を歩く。 */
		{
			const int slice = hostRate_ > 0 ? hostRate_ / 10 : 4410;
			int16_t* tmp = (int16_t*)malloc((size_t)slice * 2 * sizeof(int16_t));
			if (tmp) {
				int found = 0;
				for (int attempt = 0; attempt < 12 && !found; attempt++) {
					for (int w = 0; w < 40; w++) {
						Render(tmp, slice);
						int peak = 0;
						for (int i = 0; i < slice * 2; i++) {
							int v = tmp[i]; if (v < 0) v = -v;
							if (v > peak) peak = v;
						}
						if (peak >= 400) {
							found = 1;
							break;
						}
					}
					if (found) break;
					/* 次の試行表エントリ（ピン無し）または同じ曲を再武装 */
					if (!pinned_)
						TryInjectCommand();
					else {
						cmdIndex_ = 0;
						TryInjectCommand();
					}
					RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 10);
				}
				free(tmp);
			}
		}
		cmdIndex_ = 0;
		TryInjectCommand();
		RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 10);
		/* Render 中にライブ曲を再武装し続け、classify を PLAY に保つ */
		pinned_ = 1;
	}
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_HCASTLE) {
		/* hcastle/citybomb は EI;DI poll @03CE。kittenk（JP $0361）は $03C0 までチェックサムし $04CD で poll。そのチェックサム窓への注入は失われ、後続試行表 $01 は SE だけ。 */
		const int kittenk = (hw_->PeekMem(0) == 0xc3u
			&& hw_->PeekMem(1) == 0x61u && hw_->PeekMem(2) == 0x03u);
		const uint16_t pollLo = kittenk ? (uint16_t)0x04c0u : (uint16_t)0x03c0u;
		for (int i = 0; i < 400; i++) {
			RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 60);
			const uint16_t pc = hw_->Cpu() ? (uint16_t)hw_->Cpu()->r.pc : 0;
			if (pc >= pollLo && pc < 0x0900)
				break;
		}
		TryInjectCommand();
		RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 4);
		if (kittenk) {
			/* カタログをピン。試行表 $01（SE）を重ねない */
			cmdIndex_ = 0;
			pinned_ = 1;
			TryInjectCommand();
		} else {
			TryInjectCommand();
		}
		RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 4);
	}
	if (hw_->board_ == CEMU_AC_BOARD_SNK_OPL) {
		if (hw_->SnkMapKind()) {
			/* 古典 SNK: ラッチ → IRQ0。YM タイマもシーケンサを駆動。ブートは C0A8 に 0x0C。type-2 BGM（athena 0x53）は 063F で BIT 2,(C0A8);RET NZ — メイン CPU がそのロックを消す。 */
			RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 4);
			if (uint8_t* m = hw_->Mem()) {
				/* athena/ikari: C0A8。gwar/psychos: C100 — 同じ 0x0C ブートロック */
				m[0xc0a8] = (uint8_t)(m[0xc0a8] & (uint8_t)~0x0cu);
				m[0xc100] = (uint8_t)(m[0xc100] & (uint8_t)~0x0cu);
			}
			TryInjectCommand();
			nextGngIrq_ = 0;
			RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 2);
		} else {
			/* SNK68: NMI はブート中に立つ RAM ロック（F151/F132）でゲート。メインループが消す前の注入は唯一の端を落とす — streetsm は無音ハング、pow はタイミング運。先にブート。ブートは F115 にも 0x0C。type-2/3 BGM（streetsm 0x47/0xBF）はそれらのビットで RET NZ し開始しない — settle 後にクリア。 */
			RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 2);
			if (uint8_t* m = hw_->Mem())
				m[0xf115] = (uint8_t)(m[0xf115] & (uint8_t)~0x0cu);
			TryInjectCommand();
			RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 4);
			TryInjectCommand();
			RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 4);
		}
	}
	/* 最初のコマンドドレイン後にマスクを再アサート */
	if (hw_->board_ == CEMU_AC_BOARD_IREM_M72 && hw_->M72IoAlt()) {
		if (uint8_t* m = hw_->Mem()) {
			if (m[0xf4dc] == 0x00)
				m[0xf4dc] = 0xff;
			if (m[0xff56] == 0x00)
				m[0xff56] = 0x20;
			if (m[0xff57] < 2)
				m[0xff57] = 0x08;
			if (m[0xff58] < 2)
				m[0xff58] = 0x08;
		}
	}
	/* Bucky/Moo: 最初の曲 IRQ 前に K054539 自己テスト／F0 ハンドシェイクを終える — DI ブート中の早い ForceIm1 は opmWrites==0。 */
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_PCM && hw_->PcmKind() == 4) {
		RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 2);
		/* ブートは reg 14 組のあと YM2151 Timer B（EC01 bit1）待ち。fmgen がフラグを上げなければ許可経路をもう一度 poke。 */
		if (hw_->SoundChip() && CEmuChipYm2151WriteCount(hw_->SoundChip()) == 0) {
			CChip* ym = hw_->SoundChip();
			ym->Write(0, 0x14); ym->Write(1, 0x20);
			ym->Write(0, 0x12); ym->Write(1, 0xf0);
			ym->Write(0, 0x14); ym->Write(1, 0x0a);
			RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 10);
		}
		cmdIndex_ = 0;
		TryInjectCommand();
		RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 5);
		if (hw_->PcmChip())
			hw_->PcmChip()->Write(0x22f, 0x01);
	}
	/* Toaplan1: 共有 RAM コマンドは YM Timer-A ISR から poll。注入後メールボックスが消えるまで IRQ をポンプ（曲インストール）。snowbros: ブート後ラッチ NMI（古典 Tecmo と同じ理由）。 */
	if (hw_->board_ == CEMU_AC_BOARD_TOAPLAN1) {
		if (hw_->ToaplanKaneko()) {
			RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 5);
			TryInjectCommand();
			RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 5);
			nextCmdAt_ = (uint64_t)~0ull;
			return 1;
		}
		if (hw_->SlapfghtAy()) {
			TryInjectCommand();
			RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 5);
			nextCmdAt_ = (uint64_t)~0ull;
			return 1;
		}
		TryInjectCommand();
		for (int i = 0; i < 90; i++) {
			hw_->SetToaplanTimerA(1);
			Ay_Cpu* c = hw_->Cpu();
			if (c && c->r.iff1 && c->r.im == 1)
				Ay_CpuIm1Interrupt(c);
			RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 250);
			if (hw_->PeekMem(hw_->ToaplanMail()) == 0xff)
				break;
		}
		nextCmdAt_ = (uint64_t)~0ull; /* ワンショット — 再注入は枠をクリア */
		return 1;
	}
	/* ピンしたカタログタイトル: Z80 が音声開始する時間を与える。早い無音フォールバックは試行表[0] を再注入し「常に曲 1」にした。M72/Sys16B/VSystem カタログは SE をピンしがち — 短プローブ窓で試行表が復帰できるよう早めに見る。 */
	if (pinned_) {
		const int fast = (hw_->board_ == CEMU_AC_BOARD_IREM_M72
			|| hw_->board_ == CEMU_AC_BOARD_SYS16B
			|| hw_->board_ == CEMU_AC_BOARD_VSYSTEM
			|| hw_->board_ == CEMU_AC_BOARD_GNG
			|| hw_->board_ == CEMU_AC_BOARD_TAITO_OPM
			|| (hw_->board_ == CEMU_AC_BOARD_KONAMI_PCM
				&& (hw_->PcmKind() == 3 || hw_->PcmKind() == 4)));
		nextCmdAt_ = (uint64_t)hw_->Cpu()->time64()
			+ (uint64_t)cpuHz_ * (fast ? 1ull : 3ull)
			/ ((hw_->board_ == CEMU_AC_BOARD_IREM_M72
				|| hw_->board_ == CEMU_AC_BOARD_SYS16B
				|| hw_->board_ == CEMU_AC_BOARD_VSYSTEM) ? 2ull : 1ull);
	} else
		nextCmdAt_ = (uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 60;
	/* WSG: digdug/galaga はブート後にラッチ更新が数回要る */
	if (hw_->board_ == CEMU_AC_BOARD_NAMCO_WSG)
		nextCmdAt_ = (uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 4;
	/* CPS2: コマンド書込が CFFF 待ちを解放。最初のホスト Render コールバック前に EI/IRQ が始まるよう短く回す。 */
	if (hw_->board_ == CEMU_AC_BOARD_CPS_QS)
		RunUntil((uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 10);
	return 1;
}

void CDriverAc::Close()
{
	hw_ = NULL;
	booted_ = 0;
	triggered_ = 0;
	if (scratch_) {
		free(scratch_);
		scratch_ = NULL;
		scratchFrames_ = 0;
	}
}

int CDriverAc::OverlayTitle(unsigned titleCode)
{
	if (!hw_) return 0;
	songCmd_ = (uint8_t)(titleCode & 0xff);
	songCmdWord_ = (uint16_t)(titleCode & 0xffff);
	songCmdDword_ = titleCode;
	cmdIndex_ = 0;
	triggered_ = 0;
	heard_ = 0;
	TryInjectCommand();
	return 1;
}

unsigned CDriverAc::OpmWrites() const
{
	return hw_ ? hw_->opmWrites_ : 0;
}

void CDriverAc::TickOpm(uint64_t cpuCycles)
{
	if (!hw_ || !hw_->SoundChip() || cpuCycles == 0) return;
	if (cpuHz_ < 1 || opmHz_ < 1) return;
	opmResidual_ += cpuCycles * (uint64_t)opmHz_;
	const uint64_t opmTicks = opmResidual_ / (uint64_t)cpuHz_;
	opmResidual_ %= (uint64_t)cpuHz_;
	if (opmTicks) {
		/* Hang-On / Space Harrier: フルマスタクロックは Timer-B が約 2 倍。半分は少し遅い。3/4 でキャビネ相当テンポにし、再倍増しない。ピッチは Render（サンプル駆動）。 */
		uint64_t timerTicks = opmTicks;
		if (hw_->board_ == CEMU_AC_BOARD_HANGON) {
			timerTicks = (opmTicks * 3u) / 4u;
		} else if (hw_->board_ == CEMU_AC_BOARD_TECMO16 && hw_->TecmoOpl() == 5) {
			/* Cave Z80 は 8 MHz、YM は 4 MHz なので 4 サイクル命令が YM 2 クロック。AdvanceClocks の整数 usec は clocks*1e6/Hz = 0 になり Timer A が期限せず YM キーオンを食う ISR が走らない。最低 1µs（4 クロック）にまとめる。 */
			rzOpmAcc_ += opmTicks;
			const uint64_t quantum = 4ull;
			if (rzOpmAcc_ < quantum)
				timerTicks = 0;
			else {
				timerTicks = rzOpmAcc_;
				rzOpmAcc_ = 0;
			}
		} else if (hw_->board_ == CEMU_AC_BOARD_RAIZING
			&& hw_->RaizingType() == 1) {
			/* mahou は 4-12 サイクル Z80 命令で YM Timer A を poll。AdvanceClocks の整数 usec は clocks*1e6/Hz でそのスライスは 0（テンポ半減）。余りをチップに置かない（全 YM2151 基板で競合）。27 クロック = 27/8 MHz で正確 8µs。poll スライスはここで溜まり消えない。データシート全速（TA $97:00 ≈ 125.6 Hz）はこの TickOpm+fmgen 経路の MAME A/B より明らかに速い — Hang-On の約 2× Timer-B と同じ級。3/4 は少し遅く 7/8 は少し速い。5/6 がその間（6×27 の束で 5/6 が整数）。 */
			rzOpmAcc_ += opmTicks;
			const uint64_t quantum = 27ull;
			const uint64_t group = quantum * 6ull; /* 162 クロック = 48 µs */
			if (rzOpmAcc_ < group)
				timerTicks = 0;
			else {
				const uint64_t g = rzOpmAcc_ / group;
				timerTicks = g * (quantum * 5ull); /* 135 クロック = 40 µs */
				rzOpmAcc_ %= group;
			}
		}
		if (timerTicks)
			hw_->SoundChip()->AdvanceClocks(timerTicks);
		if (hw_->Chip2())
			hw_->Chip2()->AdvanceClocks(opmTicks);
		if (hw_->Chip3())
			hw_->Chip3()->AdvanceClocks(opmTicks);
	}
	/* K054539 基板は音源 NMI を PCM チップ自身のタイマから取るので、MixAdd がサンプル描画でも 18.432 MHz クロックが要る */
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_PCM && hw_->PcmKind() == 4) {
		k054539Residual_ += cpuCycles * 18432000ull;
		const uint64_t kt = k054539Residual_ / (uint64_t)cpuHz_;
		k054539Residual_ %= (uint64_t)cpuHz_;
		if (kt) {
			if (hw_->PcmChip()) hw_->PcmChip()->AdvanceClocks(kt);
			if (hw_->KonamiPcm2()) hw_->KonamiPcm2()->AdvanceClocks(kt);
		}
	}
}

void CDriverAc::TryInjectCommand()
{
	if (!hw_) return;
	if ((hw_->board_ == CEMU_AC_BOARD_TAITO_OPM
		&& (hw_->TaitoOpmMap() == 2 || hw_->TaitoOpmMap() == 3
			|| hw_->TaitoOpmMap() == 4 || hw_->TaitoOpmMap() == 5
			|| hw_->TaitoOpmMap() == 6 || hw_->TaitoOpmMap() == 7))
		|| hw_->HalleysAy() || hw_->PbactionAy() || hw_->ChaknpopAy()) {
		/* ワンショットメールボックス／ラッチ。再注入は 0x01.. を歩き曲を殺す */
		if (cmdIndex_ >= 1) return;
		hw_->SetSoundCommand(songCmd_ ? songCmd_ : (uint8_t)0x06);
		cmdIndex_++;
		triggered_ = 1;
		return;
	}
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_GX) {
		uint16_t cmd = songCmdWord_ ? songCmdWord_ : (uint16_t)songCmd_;
		if (cmdIndex_ == 0 && cmd) {
			hw_->SetSoundCommandWord(cmd);
			cmdIndex_++;
			triggered_ = 1;
			return;
		}
		if (cmdIndex_ == 0) {
			hw_->SetSoundCommandWord(0x0101);
			cmdIndex_++;
			triggered_ = 1;
		}
		return;
	}
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_RF5C400) {
		if (cmdIndex_ >= 1) return;
		unsigned code = songCmdDword_;
		if (!code && songCmdWord_)
			code = ((unsigned)songCmdWord_ << 16);
		if (!code) code = 0x01010000u;
		hw_->HornetInjectSong(code);
		cmdIndex_++;
		triggered_ = 1;
		return;
	}
	if (hw_->board_ == CEMU_AC_BOARD_IREM_M92) {
		/* 初期セット＋ノートリスト（uccops）: カタログコードは生ラッチ値。チャネル BGM Rev 3.40 のみ: BGM は 0x20+index */
		if (!songCmd_ || cmdIndex_ >= 6) return;
		uint8_t cmd = songCmd_;
		/* カタログコードはメイン CPU が書くラッチ値。ここで 0x20 を足す（Rev 3.40 チャネル BGM ヒューリスティック）と mysticri 0x01 が無音 0x21 になり uccopsj も同じ外れへ畳まれた。 */
		/* Rev3.40 モードバイトはクリアのまま dequeue を飛ばさない。マスク: バイトリング [09EA]/[09EF]。ワードリング [0C31]（nbbatman）または [0C32]（wpksoc）。 */
		hw_->M92Write8(0xa004fu, 0x00);
		hw_->M92Write8(0xa09eau, 0xff);
		hw_->M92Write8(0xa09efu, 0xff);
		if (hw_->M92WordQueue()) {
			/* ワードリングは AX を格納。INTP1 はアイドルループから AH が古く、(AH&AL)!=0 かつ AL<0xF0 で BGM を拒否。0AF0 リングへ AH=0 を直接植える — enqueue ルーチンと同じ書込。 */
			hw_->M92Write8(0xa0c31u, 0xff);
			hw_->M92Write8(0xa0c32u, 0xff);
			const uint8_t* ram = hw_->M92Ram();
			if (ram) {
				uint8_t* wram = const_cast<uint8_t*>(ram);
				const uint8_t wp = wram[0xb10];
				const unsigned off = 0xaf0u + (((unsigned)wp & 0x0fu) << 1);
				wram[off] = cmd;
				wram[off + 1u] = 0x00; /* AH = 0 — 古典 BGM 経路 */
				wram[0xb10] = (uint8_t)((wp + 1u) & 0x0fu);
			}
		} else {
			hw_->SetSoundCommand(cmd);
		}
		cmdIndex_++;
		triggered_ = 1;
		return;
	}
	if (hw_->board_ == CEMU_AC_BOARD_DECO
		|| hw_->board_ == CEMU_AC_BOARD_ATARI_SYS1) {
		/* ファームは同じ BGM id の繰り返しを「置換」: ORA #$80 のあと $2310 枠を消す — 再注入が FM チャネルを殺す。一度だけ。 */
		if (!songCmd_ || cmdIndex_ >= 1) return;
		hw_->SetSoundCommand(songCmd_);
		cmdIndex_++;
		triggered_ = 1;
		return;
	}
	if (hw_->board_ == CEMU_AC_BOARD_NAMCO_SYS1
		|| hw_->board_ == CEMU_AC_BOARD_NAMCO_SYS2) {
		if (!songCmd_ && !songCmdWord_) return;
		if (cmdIndex_ >= 1) return;
		if (songCmdWord_ > 0xffu)
			hw_->SetSoundCommandWord(songCmdWord_);
		else
			hw_->SetSoundCommand(songCmd_ ? songCmd_ : (uint8_t)songCmdWord_);
		cmdIndex_++;
		triggered_ = 1;
		return;
	}
	if (hw_->board_ == CEMU_AC_BOARD_SEIBU_OPL) {
		/* ワンショット: 走査途中の再ラッチは (200D) をリセットし BGM 開始を中断 */
		if (cmdIndex_ >= 1) return;
		uint8_t cmd = songCmd_ ? songCmd_ : (uint8_t)0x80;
		if (uint8_t* m = hw_->Mem())
			m[0x201c] = 0xff; /* メインループをキック */
		hw_->SetSoundCommand(cmd);
		cmdIndex_++;
		triggered_ = 1;
		return;
	}
	if (hw_->board_ == CEMU_AC_BOARD_SEGA_SCSP) {
		/* 数回再注入 — 最初の MIDI パケットは ISR 準備前に着地し得る。MultiPCM バンク／プログラムは持続 FIFO ドレインが要る。SCSP MIDI FIFO はファームが武装してからしかドレインされない。 */
		if (cmdIndex_ >= 4) return;
		/* …ただしバイトが未読の間だけ。各注入は Stop + 曲選択をキューするので、68000 が選択を取ったあとの繰り返しは曲を再開: daytona は落ち着くまでオープニングを 2–3 回再トリガ。SCSP 残りは再試行が要る: シーケンサ武装前に MIDI をドレイン（dynabb/segawski 偶数 id はドレインのみ停止で SILENT）。 */
		if (cmdIndex_ > 0 && hw_->SegaM1Audio() && !hw_->SegaMidiFifoPending()) {
			cmdIndex_ = 4;
			return;
		}
		const uint16_t w = songCmdWord_ ? songCmdWord_ : (uint16_t)songCmd_;
		if (!w) return;
		hw_->SetSoundCommandWord(w);
		cmdIndex_++;
		triggered_ = 1;
		return;
	}
	if (hw_->board_ == CEMU_AC_BOARD_IREM_M62) {
		/* 曲は Open で投稿済み（cmd + 0x80 ハンドシェイク）。再エッジしない: IRQ1 アサートのまま MSM VCK NMI が入れ子すると 128 バイト IRAM スタックが吹きシーケンサ死。 */
		if (cmdIndex_ >= 1) return;
		uint8_t cmd = songCmd_ ? songCmd_ : (uint8_t)0x20;
		if (cmd & 0x80) cmd = (uint8_t)(cmd & 0x7fu);
		if (!cmd) cmd = 0x20;
		hw_->SetSoundCommand(cmd);
		cmdIndex_++;
		triggered_ = 1;
		return;
	}
	if (hw_->board_ == CEMU_AC_BOARD_NAMCO_SYS86) {
		/* ワンショットメールボックス: B0!=0 の間 $1182=$A6 を繰り返すと IRQ 音楽更新がホストハンドシェイクへ逸れ KeyOn を殺す */
		if (cmdIndex_ >= 1) return;
		uint8_t cmd = songCmd_ ? songCmd_ : (uint8_t)0x01;
		if (!cmd) return;
		hw_->SetSoundCommand(cmd);
		cmdIndex_++;
		triggered_ = 1;
		return;
	}
	if (hw_->board_ == CEMU_AC_BOARD_NAMCO_WSG) {
		if (hw_->PengoWsg()) {
			/* ワンショット: 更新毎に (IX+0)=1 を植え直すと 1C4B ポインタロードが再開しメロディが切れる */
			if (cmdIndex_ >= 1) return;
			hw_->SetSoundCommand(songCmd_);
			cmdIndex_ = 1;
			triggered_ = 1;
			return;
		}
		if (hw_->WsgMappy()) {
			/* ワンショットフラグ投稿。60Hz 毎に $40-$7F を再クリアするとシーケンサが再開。cmd 0 は曲 0（$40）であり「未設定」ではない。 */
			if (cmdIndex_ >= 1) return;
			hw_->SetSoundCommand(songCmd_);
			cmdIndex_ = 1;
			triggered_ = 1;
			return;
		}
		/* 更新経路は繰り返し呼ばれ得る — カタログタイトルを過ぎて試行表を歩かず持続 BGM を許す */
		if (!songCmd_) return;
		hw_->SetSoundCommand(songCmd_);
		if (cmdIndex_ < 1) cmdIndex_ = 1;
		triggered_ = 1;
		return;
	}
	if (hw_->board_ == CEMU_AC_BOARD_NAMCO_C352 && (hw_->H8Active() || hw_->M37702Active())) {
		/* ワンショット: カタログ／既定語だけ — 試行表走査なし */
		if (cmdIndex_ >= 1) return;
		const uint16_t w = songCmdWord_ ? songCmdWord_
			: (uint16_t)(songCmd_ ? songCmd_ : 0x20);
		if (!w) return;
		hw_->SetSoundCommandWord(w);
		cmdIndex_++;
		triggered_ = 1;
		return;
	}
	if (hw_->board_ == CEMU_AC_BOARD_M68K_PCM) {
		/* TryInjectCommand は M68K_PCM 用に周期的にこれを呼ぶ */
		if (cmdIndex_ >= 4) return;
		uint8_t cmd = songCmd_ ? songCmd_ : (uint8_t)0x01;
		if (!cmd) return;
		hw_->SetSoundCommand(cmd);
		cmdIndex_++;
		triggered_ = 1;
		return;
	}
	if (!ms1_ && !hw_->Cpu()) return;
	if (!ms1_ && !hw_->Cpu()) return;
	const uint8_t* table = kSys16TryCmds;
	int n = (int)(sizeof(kSys16TryCmds) / sizeof(kSys16TryCmds[0]));
	if (hw_->board_ == CEMU_AC_BOARD_CPS1) {
		table = kCps1TryCmds;
		n = (int)(sizeof(kCps1TryCmds) / sizeof(kCps1TryCmds[0]));
	} else if (hw_->board_ == CEMU_AC_BOARD_GNG) {
		table = kGngTryCmds;
		n = (int)(sizeof(kGngTryCmds) / sizeof(kGngTryCmds[0]));
	} else if (hw_->board_ == CEMU_AC_BOARD_ABURNER) {
		table = kAburnerTryCmds;
		n = (int)(sizeof(kAburnerTryCmds) / sizeof(kAburnerTryCmds[0]));
	} else if (hw_->board_ == CEMU_AC_BOARD_OUTRUN || hw_->board_ == CEMU_AC_BOARD_HANGON) {
		table = kOutRunTryCmds;
		n = (int)(sizeof(kOutRunTryCmds) / sizeof(kOutRunTryCmds[0]));
	} else if (hw_->board_ == CEMU_AC_BOARD_CPS_QS) {
		table = kCpsQsTryCmds;
		n = (int)(sizeof(kCpsQsTryCmds) / sizeof(kCpsQsTryCmds[0]));
	} else if (hw_->board_ == CEMU_AC_BOARD_KONAMI_PCM) {
		table = kKonamiTryCmds;
		n = (int)(sizeof(kKonamiTryCmds) / sizeof(kKonamiTryCmds[0]));
	} else if (hw_->board_ == CEMU_AC_BOARD_NAMCO_C352) {
		table = kNamcoTryCmds;
		n = (int)(sizeof(kNamcoTryCmds) / sizeof(kNamcoTryCmds[0]));
	} else if (hw_->board_ == CEMU_AC_BOARD_SYS18 || hw_->board_ == CEMU_AC_BOARD_SYS32
		|| hw_->board_ == CEMU_AC_BOARD_SYS24) {
		table = kSys18TryCmds;
		n = (int)(sizeof(kSys18TryCmds) / sizeof(kSys18TryCmds[0]));
	} else if (hw_->board_ == CEMU_AC_BOARD_TAITO_YM2610
		|| hw_->board_ == CEMU_AC_BOARD_TAITO_OPM
		|| hw_->board_ == CEMU_AC_BOARD_TAITO_SJ) {
		table = kTaitoTryCmds;
		n = (int)(sizeof(kTaitoTryCmds) / sizeof(kTaitoTryCmds[0]));
	} else if (hw_->board_ == CEMU_AC_BOARD_IREM_M72) {
		table = kIremTryCmds;
		n = (int)(sizeof(kIremTryCmds) / sizeof(kIremTryCmds[0]));
	} else if (hw_->board_ == CEMU_AC_BOARD_IREM_M92) {
		table = kM92TryCmds;
		n = (int)(sizeof(kM92TryCmds) / sizeof(kM92TryCmds[0]));
	} else if (hw_->board_ == CEMU_AC_BOARD_SEGA_SYS1) {
		table = kSys1TryCmds;
		n = (int)(sizeof(kSys1TryCmds) / sizeof(kSys1TryCmds[0]));
	} else if (hw_->board_ == CEMU_AC_BOARD_KONAMI_SCRAMBLE
		|| hw_->board_ == CEMU_AC_BOARD_KONAMI_TIMEPLT) {
		table = kKonamiAyTryCmds;
		n = (int)(sizeof(kKonamiAyTryCmds) / sizeof(kKonamiAyTryCmds[0]));
	} else if (hw_->board_ == CEMU_AC_BOARD_KONAMI_GX400) {
		table = kGx400TryCmds;
		n = (int)(sizeof(kGx400TryCmds) / sizeof(kGx400TryCmds[0]));
	} else if (hw_->board_ == CEMU_AC_BOARD_FLSTORY) {
		table = kFlstoryTryCmds;
		n = (int)(sizeof(kFlstoryTryCmds) / sizeof(kFlstoryTryCmds[0]));
	} else if (hw_->board_ == CEMU_AC_BOARD_TECHNOS_DDRAGON2
		|| hw_->board_ == CEMU_AC_BOARD_TECMO16
		|| hw_->board_ == CEMU_AC_BOARD_RAIZING
		|| hw_->board_ == CEMU_AC_BOARD_KONAMI_K7232
		|| hw_->board_ == CEMU_AC_BOARD_KONAMI_HCASTLE
		|| hw_->board_ == CEMU_AC_BOARD_ALPHA68K2
		|| hw_->board_ == CEMU_AC_BOARD_TERRACRE
		|| hw_->board_ == CEMU_AC_BOARD_ROBOKID
		|| hw_->board_ == CEMU_AC_BOARD_BATTLANTIS) {
		table = kDdragon2TryCmds;
		n = (int)(sizeof(kDdragon2TryCmds) / sizeof(kDdragon2TryCmds[0]));
	}
	uint8_t cmd;
	/* Battle Bakraid のスクリプト表は 0 から引くので、ピンした曲コード 0 は他で未設定コードが試行表へ落ちるのと違いラッチへ届ける（Open 参照）。 */
	const int haveSong = (songCmd_ != 0)
		|| (pinned_ && hw_->board_ == CEMU_AC_BOARD_RAIZING
			&& hw_->RaizingType() == 4);
	if (cmdIndex_ == 0 && haveSong) {
		cmd = songCmd_;
	} else {
		const int ti = (haveSong ? cmdIndex_ - 1 : cmdIndex_);
		if (ti < 0 || ti >= n) return;
		cmd = table[ti];
	}
	/* ZN QSound: カタログコードはラッチ対として消費する 16bit 曲語 */
	if (hw_->board_ == CEMU_AC_BOARD_CPS_QS && hw_->QsZn()) {
		/* ワンショット FF 00 hi lo — CPS2 試行表を歩かない */
		if (cmdIndex_ >= 1) return;
		const uint16_t w = songCmdWord_ ? songCmdWord_ : (uint16_t)cmd;
		if (!w && !songCmd_) return;
		hw_->SetSoundCommandWord(w ? w : (uint16_t)songCmd_);
		cmdIndex_++;
		triggered_ = 1;
		return;
	} else if (hw_->board_ == CEMU_AC_BOARD_NAMCO_C352 && hw_->H8Active()) {
		const uint16_t w = (cmdIndex_ == 0 && songCmdWord_)
			? songCmdWord_ : (uint16_t)cmd;
		hw_->SetSoundCommandWord(w);
	} else if (hw_->board_ == CEMU_AC_BOARD_KONAMI_PCM && hw_->PcmKind() == 3) {
		const uint16_t w = songCmdWord_ ? songCmdWord_ : (uint16_t)cmd;
		if (w > 0xffu)
			hw_->SetSoundCommandWord(w);
		else
			hw_->SetSoundCommand((uint8_t)w);
	} else {
		hw_->SetSoundCommand(cmd);
	}
	/* kittenk: [8304]!=0 のときだけ poll が CALL $003C（D000 ドレイン）。4-NOP EI 窓は IRQ0 を外し得る。citybomb は [8303] を同じ使い方。 */
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_HCASTLE
		&& hw_->PeekMem(0) == 0xc3u && hw_->PeekMem(1) == 0x61u
		&& hw_->PeekMem(2) == 0x03u) {
		if (uint8_t* m = hw_->Mem())
			m[0x8304] = 1;
	}
	cmdIndex_++;
	triggered_ = 1;
}

void CDriverAc::DeliverIrqs()
{
	if (!hw_ || !hw_->Cpu()) return;
	Ay_Cpu* cpu = hw_->Cpu();
	CChip* chip = hw_->SoundChip();

	/* Namco Galaga/Dig Dug/Bosco: 音源 CPU 作業は全て NMI 駆動（RST $0038 は空）。メイン CPU が NMI をパルス。約 240Hz にレート制限。最初のラッチまで周期 NMI を抑止 — 早い NMI は galaga/bosco ブートチェックサムを壊す（AF だけ保存）。 */
	if (hw_->board_ == CEMU_AC_BOARD_NAMCO_WSG) {
		if (hw_->PengoWsg()) {
			/* MAME pengo: vblank IRQ0 HOLD、IM 1、マスク = LS259 Q0 @9040 */
			uint8_t* m = hw_->Mem();
			if (m && (m[0x9040] & 1u)) {
				const uint64_t now = (uint64_t)cpu->time64();
				const uint64_t period = (uint64_t)cpuHz_ / 60;
				if (period > 0 && now >= nextGngIrq_) {
					if (cpu->r.iff1)
						Ay_CpuIm1Interrupt(cpu);
					nextGngIrq_ = now + period;
				}
			}
			return;
		}
		if (uint8_t* m = hw_->Mem()) {
			m[0x9101] = 0; /* galaga ハンドシェイク */
			m[0x8c01] = 0; /* bosco ハンドシェイク */
		}
		if (hw_->IrqPulsePending()) {
			hw_->TakeIrqPulse();
			Ay_CpuNmi(cpu);
		}
		if (!hw_->WsgNmiEnable())
			return;
		const uint64_t now = (uint64_t)cpu->time64();
		const uint64_t period = (uint64_t)cpuHz_ / 240;
		if (period > 0 && now >= nextGngIrq_) {
			Ay_CpuNmi(cpu);
			nextGngIrq_ = now + period;
		}
		return;
	}

	/* GNG: Capcom は irq0_line_hold @ 4*60 Hz — ラッチ端ではない。Tecmo gaiden: ラッチ → NMI。YM2203 IRQ がシーケンサを駆動。 */
	if (hw_->board_ == CEMU_AC_BOARD_GNG) {
		if (hw_->GngGaidenMap()) {
			if (hw_->IrqPulsePending()) {
				hw_->TakeIrqPulse();
				Ay_CpuNmi(cpu);
			}
			if (chip && chip->Irq()) {
				if (cpu->r.iff1)
					Ay_CpuIm1Interrupt(cpu);
				chip->AckIrq();
			}
			return;
		}
		const uint64_t now = (uint64_t)cpu->time64();
		const uint64_t period = (uint64_t)cpuHz_ / 240;
		if (period > 0 && now >= nextGngIrq_) {
			if (cpu->r.iff1)
				Ay_CpuIm1Interrupt(cpu);
			nextGngIrq_ = now + period;
		}
		return;
	}

	/* CPS2 QSound: 周期 IM1 @ 250 Hz（8 MHz / 32000）。共有 RAM コマンドは 0038 の IRQ ハンドラで拾う — NMI/ラッチではない。Capcom ZN（QsZn）: ラッチ書込毎にワンショット NMI（MAME soundlatch→NMI）。スライス毎に NMI を保持しない — 洪水して Z80 状態を壊す。ブートは IM 2。250Hz 線をそれに合わせてベクタ。 */
	if (hw_->board_ == CEMU_AC_BOARD_CPS_QS) {
		if (hw_->QsZn()) {
			/* 後続ラッチバイト: NMI はハンドラ全体で IFF1 をクリア（ts2 @0066 RETN@0090、sfex @0066 RETN@0098、techromn RETN@00BD、sfex2/tgmj JP 0180 RETN@0195）。固定 0066..0091 窓は後の ZN ファームでハンドラ途中に再パルスし F000/F100 リングを壊した（sfex F100=00100000、iff が 0 のまま）。RETN 後に IFF1 を待つ。 */
			if (cpu->r.iff1 && hw_->ZnTakeDeferredNmi())
				hw_->PulseIrq();
			if (hw_->IrqPulsePending()) {
				hw_->TakeIrqPulse();
				Ay_CpuNmi(cpu);
			}
		}
		if (irqPaceLive_) {
			/* 再生: 250Hz をホストサンプルにロック。time64 がサンプルあたり 2 倍進むと
			   sf2/ssf2 ともシーケンサが倍速になる。ブート settle は下の CPU 周期のまま。 */
			if (irqPaceDue_ > 0 && cpu->r.iff1) {
				if (hw_->QsZn() || cpu->r.im != 2)
					Ay_CpuIm1Interrupt(cpu);
				else
					Ay_CpuIm2Interrupt(cpu, 0xff);
				irqPaceDue_--;
			}
			return;
		}
		const uint64_t now = (uint64_t)cpu->time64();
		const uint64_t period = (uint64_t)cpuHz_ / 250;
		if (period > 0 && now >= nextGngIrq_) {
			/* 250Hz が Z80 ソフトタイマを駆動（ts2 RST38 INC F000..F003 — F002 がブート待ち @009F を解除しシーケンサ @0240 をペース）。Capcom ZN は IM1（DI;IM 1 = ED 56）。ここで IM2 ベクタしない — 未設定 I が ts2 で Ay_CpuIm2Interrupt AV。CPS2 は ROM が組んだ本物 IM2 を残す。 */
			if (cpu->r.iff1) {
				if (hw_->QsZn() || cpu->r.im != 2)
					Ay_CpuIm1Interrupt(cpu);
				else
					Ay_CpuIm2Interrupt(cpu, 0xff);
			}
			nextGngIrq_ = now + period;
		}
		return;
	}

	/* Sega System1/2: TIMER "soundirq" が 32V/96V/… — フレーム 4 回、自動 ack。ラッチ NMI が曲番号を運び、IRQ がシーケンサを駆動。 */
	if (hw_->board_ == CEMU_AC_BOARD_SEGA_SYS1) {
		if (hw_->IrqPulsePending()) {
			hw_->TakeIrqPulse();
			Ay_CpuNmi(cpu);
		}
		const uint64_t now = (uint64_t)cpu->time64();
		const uint64_t period = (uint64_t)cpuHz_ / 240;
		if (period > 0 && now >= nextGngIrq_) {
			if (cpu->r.iff1)
				Ay_CpuIm1Interrupt(cpu);
			nextGngIrq_ = now + period;
		}
		return;
	}

	/* Irem M72: サンプルポンプ NMI は曲の有無に関わらず MASTER_CLOCK/8/512 = 7812.5 Hz。YM2151 タイマがシーケンサを駆動。 */
	if (hw_->board_ == CEMU_AC_BOARD_IREM_M72) {
		const uint64_t now = (uint64_t)cpu->time64();
		const uint64_t period = ((uint64_t)cpuHz_ * 2ull) / 15625ull;
		if (period > 0 && now >= nextM72Nmi_) {
			if (m72FakeNmi_)
				hw_->M72PumpSample();
			else
				Ay_CpuNmi(cpu);
			nextM72Nmi_ = now + period;
		}
		/* MAME m72_audio_chips: soundlatch が rst18_w、YM2151 IRQ が rst28_w。RST_NEG_BUFFER が Z80 IRQ ピンへ。音源 CPU は IM 0。IACK 中のオペコードは生きている基ベクタの論理 AND。ラッチのみは RST 18h、YM のみ RST 28h、両方で RST 08h。これを 0038（IM 1）へベクタするとそこに居たバイトに当たった。 */
		hw_->TakeIrqPulse();
		const int latch = hw_->SoundCmdPending() ? 1 : 0;
		const int ymirq = (chip && chip->Irq()) ? 1 : 0;
		if ((latch || ymirq) && cpu->r.iff1) {
			unsigned vec = 0xff;
			if (latch) vec &= 0xdf; /* RST 18h（ラッチ） */
			if (ymirq) vec &= 0xef; /* RST 28h（YM）ベクタ */
			if (Ay_CpuRstInterrupt(cpu, (uint16_t)(vec & 0x38)) && ymirq)
				chip->AckIrq();
		}
		return;
	}

	/* Taito TC0140SYT / PC060HA: NMI はコマンドがキューされ音源 CPU が許可したときだけ（slave submode 6） */
	if (hw_->board_ == CEMU_AC_BOARD_TAITO_YM2610
		|| hw_->board_ == CEMU_AC_BOARD_TAITO_OPM) {
		if (hw_->TaitoOpmMap() == 2) {
			/* kikikai: vblank IRQ0。PC060HA NMI 無し */
			if (hw_->IrqPulsePending())
				hw_->TakeIrqPulse();
			const uint64_t now = (uint64_t)cpu->time64();
			const uint64_t period = (uint64_t)cpuHz_ / 60;
			if (period > 0 && now >= nextGngIrq_) {
				if (cpu->r.iff1)
					Ay_CpuIm1Interrupt(cpu);
				nextGngIrq_ = now + period;
			}
			return;
		}
		if (hw_->TaitoOpmMap() == 7) {
			/* 旧 TNZS: vblank HOLD。tnzsjo アイドルは DI;CALL 008F;EI なので線は EI 窓まで pending のまま、期限しない。ISR は (D000)=1 を立て CALL 0082 前に EI。入れ子 RST38 は SP=D031 で 0027 近道を取りフレームを潰す。 */
			if (hw_->IrqPulsePending())
				hw_->TakeIrqPulse();
			if (hw_->PeekMem(3) == 0xfd && hw_->PeekMem(0xd000))
				return;
			const uint64_t now = (uint64_t)cpu->time64();
			const uint64_t period = (uint64_t)cpuHz_ / 60;
			if (period > 0 && now >= nextGngIrq_) {
				if (Ay_CpuIm1Interrupt(cpu))
					nextGngIrq_ = now + period;
			}
			return;
		}
		if (hw_->TaitoOpmMap() == 3 || hw_->TaitoOpmMap() == 4
			|| hw_->TaitoOpmMap() == 5 || hw_->TaitoOpmMap() == 6) {
			if (hw_->IrqPulsePending() && hw_->FlstoryNmiEn()) {
				hw_->TakeIrqPulse();
				Ay_CpuNmi(cpu);
			}
			/* ISR @015F は YM Timer A を poll。status bit0 を強制しスピンさせない。IRQ0 は約 60Hz だが本物 EI 窓かつ周期 1 回だけ — 毎 EI で撃つと CALL 01BD が飢え NMI コマンドリングがドレインされない。 */
			if (cpu->r.iff1 && cpu->r.im == 1) {
				const uint64_t now = (uint64_t)cpu->time64();
				const uint64_t period = (uint64_t)cpuHz_ / 60;
				if (period > 0 && now >= nextGngIrq_) {
					Ay_CpuIm1Interrupt(cpu);
					nextGngIrq_ = now + period;
				}
			}
			return;
		}
		if (hw_->IrqPulsePending()) {
			hw_->TakeIrqPulse();
			Ay_CpuNmi(cpu);
		}
		/* YM2610/YM2151 タイマ IRQ がシーケンサを駆動。AckIrq しない（V-System と同じ）: Rastan/Asuka ISR @01F9 は status&3 でビジー待ち。OPM メインループは EI;DI — タイマフラグ中は ForceIm1 をレート制限。 */
		if (chip && cpu->r.im == 1) {
			const int st = (chip->ReadStatus() & 0x03) != 0;
			if (hw_->board_ == CEMU_AC_BOARD_TAITO_OPM && st) {
				const uint64_t now = (uint64_t)cpu->time64();
				const uint64_t period = (uint64_t)cpuHz_ / 250;
				if (period > 0 && now >= nextGngIrq_) {
					const unsigned pc = (unsigned)cpu->r.pc;
					/* YM2203: ForceIm1 しない — 入れ子（EI;DI メインループ窓でも）が 033A/0388 の KeyOn 完了を妨げた（ko=0、TL=7F のまま）。ハードは EI 待ち。 */
					if (cpu->r.iff1)
						Ay_CpuIm1Interrupt(cpu);
					else if (!hw_->MainIsYm2203()) {
						const int inIsr = pc >= 0x02ceu && pc < 0x033au;
						const int inSongStart = (pc >= 0x0388u && pc < 0x0500u)
							|| (pc >= 0x033au && pc < 0x0388u);
						if (!inIsr && !inSongStart)
							Ay_CpuIm1Interrupt(cpu);
					}
					nextGngIrq_ = now + period;
				}
			} else if (chip->Irq() && cpu->r.iff1) {
				Ay_CpuIm1Interrupt(cpu);
			}
		}
		return;
	}

	/* Taito SJ: ラッチ書込が NMI をパルス（AY#4 ポート B bit0 でゲート）。シーケンサは映像ハードの 60Hz IM1 IRQ。 */
	if (hw_->board_ == CEMU_AC_BOARD_TAITO_SJ) {
		if (hw_->NbAyIo()) {
			if (hw_->IrqPulsePending()) {
				if (Ay_CpuIm1Interrupt(cpu))
					hw_->TakeIrqPulse();
			}
			return;
		}
		if (hw_->BombjackAy()) {
			/* MAME: audiocpu へ vblank NMI。ラッチは 6000 で poll */
			const uint64_t now = (uint64_t)cpu->time64();
			const uint64_t period = (uint64_t)cpuHz_ / 60;
			if (period > 0 && now >= nextGngIrq_) {
				Ay_CpuNmi(cpu);
				nextGngIrq_ = now + period;
			}
			return;
		}
		if (hw_->CalorieAy()) {
			/* MAME: audiocpu へ vblank IRQ0 HOLD。ラッチは C000 で poll */
			const uint64_t now = (uint64_t)cpu->time64();
			const uint64_t period = (uint64_t)cpuHz_ / 60;
			if (period > 0 && now >= nextGngIrq_) {
				if (cpu->r.iff1)
					Ay_CpuIm1Interrupt(cpu);
				nextGngIrq_ = now + period;
			}
			return;
		}
		if (hw_->SolomonAy()) {
			/* MAME: ラッチ書込が NMI をパルス。シーケンサは 120Hz IRQ0 HOLD */
			if (hw_->IrqPulsePending()) {
				hw_->TakeIrqPulse();
				Ay_CpuNmi(cpu);
			}
			const uint64_t now = (uint64_t)cpu->time64();
			const uint64_t period = (uint64_t)cpuHz_ / 120;
			if (period > 0 && now >= nextGngIrq_) {
				if (cpu->r.iff1)
					Ay_CpuIm1Interrupt(cpu);
				nextGngIrq_ = now + period;
			}
			return;
		}
		if (hw_->HalleysAy()) {
			/* MAME halleys: ラッチ NMI + IRQ0 が 6MHz/(4*16*16*10*16) ≈ 36.6 Hz */
			if (hw_->IrqPulsePending()) {
				hw_->TakeIrqPulse();
				Ay_CpuNmi(cpu);
			}
			const uint64_t now = (uint64_t)cpu->time64();
			const uint64_t period = (uint64_t)cpuHz_ * 163840ull / 6000000ull;
			if (period > 0 && now >= nextGngIrq_) {
				if (cpu->r.iff1)
					Ay_CpuIm1Interrupt(cpu);
				nextGngIrq_ = now + period;
			}
			return;
		}
		if (hw_->PbactionAy()) {
			/* ファーム: I=1、IM 2、CTC ch0 ベクタ 0 / ch1 ベクタ 2。ch0 カウンタ TC=1（コマンド TRG0）。ch1 タイマ $A7/$5D、プリスケール 256 @ 3 MHz → 93*256 サイクル ≈ 126Hz シーケンサ。 */
			if (hw_->IrqPulsePending()) {
				if (cpu->r.im == 2 && cpu->r.i == 1
					&& Ay_CpuIm2Interrupt(cpu, 0))
					hw_->TakeIrqPulse();
			}
			const uint64_t now = (uint64_t)cpu->time64();
			const uint64_t period = 93ull * 256ull;
			if (period > 0 && now >= nextGngIrq_) {
				if (cpu->r.im == 2 && cpu->r.i == 1)
					Ay_CpuIm2Interrupt(cpu, 2);
				nextGngIrq_ = now + period;
			}
			return;
		}
		if (hw_->ChaknpopAy()) {
			/* MAME: vblank IRQ0 HOLD。音源 tick は RST38 → A02F */
			const uint64_t now = (uint64_t)cpu->time64();
			const uint64_t period = (uint64_t)cpuHz_ / 60;
			if (period > 0 && now >= nextGngIrq_) {
				if (cpu->r.iff1)
					Ay_CpuIm1Interrupt(cpu);
				nextGngIrq_ = now + period;
			}
			return;
		}
		if (hw_->IrqPulsePending()) {
			hw_->TakeIrqPulse();
			Ay_CpuNmi(cpu);
		}
		const uint64_t now = (uint64_t)cpu->time64();
		const uint64_t period = (uint64_t)cpuHz_ / 60;
		if (period > 0 && now >= nextGngIrq_) {
			if (cpu->r.iff1)
				Ay_CpuIm1Interrupt(cpu);
			nextGngIrq_ = now + period;
		}
		return;
	}

	/* Konami Scramble / Time Pilot / GX400: ラッチ → IRQ0（IM1 / RST38）。Scramble は KonamiAyTimer() 経由の AY タイマポート tick も要る。ベクタ入場まで TakeIrqPulse しない — さもなくば EI 遅延（irqDelay）が唯一の端を落とし音楽が始まらない（RAM が空チャネル 8001=1/8000=0 で固まり SILENT）。 */
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_SCRAMBLE
		|| hw_->board_ == CEMU_AC_BOARD_KONAMI_TIMEPLT
		|| hw_->board_ == CEMU_AC_BOARD_KONAMI_GX400) {
		/* 自己テストが 4000-7FFF 書込を終えたら共有 RAM ハンドシェイクを解放（メイン 68000 不在） */
		if (hw_->board_ == CEMU_AC_BOARD_KONAMI_GX400) {
			const unsigned pc = (unsigned)cpu->r.pc;
			if (pc >= 0x0200u && pc < 0x8000u) {
				if (uint8_t* m = hw_->Mem())
					m[0x7ffc] = 0x04;
			}
		}
		if (hw_->IrqPulsePending()) {
			/* ハードは Z80 が受けるまで 7474/IRQ 線を保持。ForceIm1 は IFF1 が要る（Im1Interrupt と同じ）。 */
			if (Ay_CpuIm1Interrupt(cpu))
				hw_->TakeIrqPulse();
		}
		const uint64_t now = (uint64_t)cpu->time64();
		const uint64_t period = (uint64_t)cpuHz_ / 60;
		if (hw_->board_ == CEMU_AC_BOARD_KONAMI_GX400) {
			/* MAME gx400: 画面 VBLANK → audiocpu NMI。NMI が (7FFB)=1 を立て、0346 が (7FFA) を 0x34 まで数え 0361 でハングしないようにする。 */
			if (period > 0 && now >= nextGngIrq_) {
				Ay_CpuNmi(cpu);
				nextGngIrq_ = now + period;
			}
			return;
		}
		/* Scramble / Time Pilot: 60Hz IM1 が AY シーケンサを生かす */
		if (!hw_->IrqPulsePending()) {
			if (period > 0 && now >= nextGngIrq_) {
				Ay_CpuIm1Interrupt(cpu);
				nextGngIrq_ = now + period;
			}
		}
		return;
	}

	/* Konami K007232 期（scontra/crimfght/twin16）: ラッチ → IRQ0。YM2151 タイマがシーケンサ時基（DD2/Taito OPM と同じ）。 */
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_K7232) {
		/* Devastators / garuka（MAME mainevt.cpp）: YM2151 irq は配線されない。シーケンサは $04F4 で Timer B を poll。Map0 の ForceIm1-on-status は $0038 を嵐にし CALL $007B を飢える。 */
		const int devstors = (hw_->PeekMem(0) == 0xf3u
			&& hw_->PeekMem(1) == 0x3eu && hw_->PeekMem(5) == 0xe0u);
		if (hw_->IrqPulsePending()) {
			if (Ay_CpuIm1Interrupt(cpu))
				hw_->TakeIrqPulse();
		}
		if (devstors)
			return;
		if (chip) {
			const int st = (chip->ReadStatus() & 0x03) != 0;
			const int pend = chip->Irq() || st;
			if (pend) {
				const uint64_t now = (uint64_t)cpu->time64();
				const uint64_t period = (uint64_t)cpuHz_ / 250;
				if (period > 0 && now >= nextGngIrq_) {
					if (cpu->r.iff1)
						Ay_CpuIm1Interrupt(cpu);
					else if (hw_->KonamiK7232Map() != 1)
						/* map0（scontra）/ map2（gradius3）: EI;DI 窓は ForceIm1 が要る。map1（aliens/crimfght）: ForceIm1 → FLAT */
						Ay_CpuIm1Interrupt(cpu);
					nextGngIrq_ = now + period;
				}
			}
		}
		return;
	}

	/* Alpha 68K-II: 周期 NMI @ 約 7614 Hz（MAME sound_nmi）、YM2203 ポート A でゲート。ラッチは NMI/メインループから IN 00 で poll。Ay_CpuNmi は常に 0066 再入。本物 Z80 NMI ラッチは RETN まで入れ子しない。ハンドラ中 787 サイクル毎の再パルスがスタックを壊した（dumps=1）。フェッチの ED45/ED4D でロック解放。 */
	if (hw_->board_ == CEMU_AC_BOARD_ALPHA68K2) {
		const uint16_t pc = (uint16_t)cpu->r.pc;
		uint8_t* mem = cpu->get_mem();
		if (mem && mem[pc] == 0xedu
			&& (mem[(uint16_t)(pc + 1)] == 0x45u || mem[(uint16_t)(pc + 1)] == 0x4du)) {
			alphaNmiBusy_ = 0;
			return; /* 次パルス前に RETN を終える */
		}
		const uint64_t now = (uint64_t)cpu->time64();
		const uint64_t period = (uint64_t)cpuHz_ / 7614;
		if (period > 0 && now >= nextGngIrq_ && hw_->AlphaNmiMask()
			&& !alphaNmiBusy_ && pc != 0x0066u) {
			Ay_CpuNmi(cpu);
			alphaNmiBusy_ = 1;
			nextGngIrq_ = now + period;
		}
		return;
	}

	/* Haunted Castle: EI;DI poll @03CE + IRQ0。ISR は RET 終わり（iff クリアのまま）。ラッチは 0038 外で ForceIm1 が 1 回要る。空 IRQ は 01BE 経由で音楽を進める。YM3812→NMI は RETN — OPL IRQ 線は無視（NMI を嵐にしない）。 */
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_HCASTLE) {
		const uint16_t pc = (uint16_t)cpu->r.pc;
		const int inIsr = (pc >= 0x0038 && pc < 0x00a0) ? 1 : 0;
		if (hw_->IrqPulsePending() && !inIsr) {
			if (Ay_CpuIm1Interrupt(cpu))
				hw_->TakeIrqPulse();
		}
		if (chip && chip->Irq())
			chip->AckIrq();
		const uint64_t now = (uint64_t)cpu->time64();
		const uint64_t period = (uint64_t)cpuHz_ / 250;
		if (period > 0 && now >= nextGngIrq_ && !hw_->IrqPulsePending() && !inIsr) {
			if (cpu->r.iff1)
				Ay_CpuIm1Interrupt(cpu);
			nextGngIrq_ = now + period;
		}
		return;
	}

	/* Raizing / Eighting。mahoudai は割り込みを許可しない — Z80 は YM2151 Timer A フラグでスピンし共有 RAM メールボックスを poll — type 1 に届けるものはない。 */
	if (hw_->board_ == CEMU_AC_BOARD_RAIZING) {
		const int type = hw_->RaizingType();
		if (type == 2) {
			/* generic_latch_8 の separate_acknowledge は E00C 書込まで IRQ0 を保持。パルスではなく未読の間再アサート。 */
			if (hw_->RaizingLatchPending() && cpu->r.iff1 && cpu->r.im == 1)
				Ay_CpuIm1Interrupt(cpu);
		} else if (type >= 3) {
			if (hw_->RaizingNmiPending()) {
				hw_->ClearRaizingNmi();
				Ay_CpuNmi(cpu);
			}
			/* MAME bbakraid_snd_interrupt: 32MHz/6/12000 ≈ 444 Hz 周期 IRQ0。メインループがその tick を数えるので、無いとコマンド着地後もシーケンサが進まない。 */
			if (type == 4) {
				const uint64_t now = (uint64_t)cpu->time64();
				const uint64_t period = (uint64_t)cpuHz_ / 444;
				if (period > 0 && now >= nextGngIrq_) {
					if (cpu->r.iff1 && cpu->r.im == 1)
						Ay_CpuIm1Interrupt(cpu);
					nextGngIrq_ = now + period;
				}
			}
		}
		return;
	}

	/* Technos DD2 / Tecmo16: soundlatch → NMI。YM2151 タイマ IRQ が BGM を駆動 */
	if (hw_->board_ == CEMU_AC_BOARD_TECHNOS_DDRAGON2
		|| hw_->board_ == CEMU_AC_BOARD_TECMO16) {
		if (hw_->IrqPulsePending()) {
			hw_->TakeIrqPulse();
			Ay_CpuNmi(cpu);
		}
		if (chip) {
			const int st = (chip->ReadStatus() & 0x03) != 0;
			const int pend = chip->Irq() || st;
			if (pend && cpu->r.iff1 && cpu->r.im == 1) {
				if (Ay_CpuIm1Interrupt(cpu)) {
					if (chip->Irq())
						chip->AckIrq();
				}
			}
		}
		if (CChip* chip2 = hw_->Chip2()) {
			const int st2 = (chip2->ReadStatus() & 0x03) != 0;
			const int pend2 = chip2->Irq() || st2;
			if (pend2 && cpu->r.iff1 && cpu->r.im == 1) {
				if (Ay_CpuIm1Interrupt(cpu)) {
					if (chip2->Irq())
						chip2->AckIrq();
				}
			}
		}
		/* Cave: ラッチ 0 の NMI がメインループ待ちフラグ（sailormn D897 / agallet DA3A）を加算。カタログ曲 NMI は enqueue のみ。 */
		if (hw_->TecmoOpl() == 5 && cpu->r.iff1 && !hw_->IrqPulsePending()) {
			const uint64_t now = (uint64_t)cpu->time64();
			const uint64_t period = (uint64_t)cpuHz_ / 60;
			if (period > 0 && now >= nextGngIrq_) {
				nextGngIrq_ = now + period;
				hw_->CaveEmptyLatch();
				Ay_CpuNmi(cpu);
			}
		}
		return;
	}

	/* Taito flstory: soundlatch → NMI（DA00 許可でゲート）。AY にタイマ IRQ は無い — ソフト 60Hz IM1。MSM5232 メロディは Render で進む。 */
	if (hw_->board_ == CEMU_AC_BOARD_FLSTORY) {
		if (hw_->IrqPulsePending() && hw_->FlstoryNmiEn()) {
			hw_->TakeIrqPulse();
			Ay_CpuNmi(cpu);
		}
		/* RST38 が NMI リングをドレインし MSM を tick。メインループは EI;NOP;NOP;DI — IFF1 クリア中に 60Hz 期限を進めると全 vblank を飛ばす。EI 窓が取るまで要求を保持。 */
		const uint64_t now = (uint64_t)cpu->time64();
		const uint64_t period = (uint64_t)cpuHz_ / (hw_->NycaptorMap() ? 122 : 60);
		if (period > 0 && now >= nextGngIrq_
			&& cpu->r.iff1 && cpu->r.im == 1) {
			if (Ay_CpuIm1Interrupt(cpu))
				nextGngIrq_ = now + period;
		}
		return;
	}

	/* Nichibutsu terracre / armedf-terraf: 周期 IRQ0 @ XTAL/4/512 → 7812.5 Hz */
	if (hw_->board_ == CEMU_AC_BOARD_TERRACRE) {
		if (hw_->IrqPulsePending()) {
			if (Ay_CpuIm1Interrupt(cpu))
				hw_->TakeIrqPulse();
		}
		const uint64_t now = (uint64_t)cpu->time64();
		const uint64_t period = (uint64_t)cpuHz_ / 7812;
		if (period > 0 && now >= nextGngIrq_) {
			if (cpu->r.iff1)
				Ay_CpuIm1Interrupt(cpu);
			nextGngIrq_ = now + period;
		}
		return;
	}

	/* UPL robokid: ラッチ → IRQ0。YM2203 タイマ IRQ もシーケンサを駆動 */
	if (hw_->board_ == CEMU_AC_BOARD_ROBOKID) {
		if (hw_->IrqPulsePending()) {
			if (Ay_CpuIm1Interrupt(cpu))
				hw_->TakeIrqPulse();
		}
		if (chip && chip->Irq() && cpu->r.iff1 && cpu->r.im == 1) {
			if (Ay_CpuIm1Interrupt(cpu))
				chip->AckIrq();
		}
		return;
	}

	/* Konami battlantis: ホスト IRQ0 + YM3812×2 タイマ IRQ */
	if (hw_->board_ == CEMU_AC_BOARD_BATTLANTIS) {
		if (hw_->IrqPulsePending()) {
			if (Ay_CpuIm1Interrupt(cpu))
				hw_->TakeIrqPulse();
		}
		if (chip && chip->Irq() && cpu->r.iff1 && cpu->r.im == 1) {
			if (Ay_CpuIm1Interrupt(cpu))
				chip->AckIrq();
		}
		return;
	}

	/* Konami K053260 期: ラッチ → IRQ0（sound_irqtrigger）。SH1→NMI が FA00/HALT サンプル待ちを起こす（Simpsons/Punk Shot/Escape Kids）。EI 時は YM2151 タイマ IRQ も食わせる。ROM 走査中（parodius）は IRQ を抑止。 */
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_PCM && hw_->PcmKind() == 3) {
		const unsigned pc = (unsigned)cpu->r.pc;
		const int romScan = (pc >= 0x06c0u && pc < 0x06f0u) ? 1 : 0;
		uint8_t* mem = hw_->Mem();
		if (hw_->KonamiSh1NmiArm() && mem && mem[pc] == 0x76) {
			hw_->ClearKonamiSh1NmiArm();
			Ay_CpuNmi(cpu);
		}
		if (!romScan && hw_->IrqPulsePending()) {
			if (cpu->r.iff1 && Ay_CpuIm1Interrupt(cpu))
				hw_->TakeIrqPulse();
		}
		if (!romScan && chip) {
			const int st = (chip->ReadStatus() & 0x03) != 0;
			const int pend = chip->Irq() || st;
			if (pend && cpu->r.iff1) {
				const uint64_t now = (uint64_t)cpu->time64();
				const uint64_t period = (uint64_t)cpuHz_ / 250;
				if (period > 0 && now >= nextGngIrq_) {
					Ay_CpuIm1Interrupt(cpu);
					nextGngIrq_ = now + period;
				}
			}
		}
		return;
	}

	/* mystwarr.cpp / xexex 系 K054539 基板: 音源 Z80 は割り込みオフで RAM フラグ（*NMI* ハンドラが立てる）を BIT 0,(HL) / JP Z。MAME の k054539_nmi_gen がその NMI を sound_ctrl bit4 ゲートの K054539 タイマから駆動。マスク可能 IM1 IRQ を食わせるとそのループでデッドロック。 */
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_PCM && hw_->PcmKind() == 4) {
		CChip* pcm = hw_->PcmChip();
		if (hw_->IrqPulsePending() && cpu->r.iff1) {
			if (Ay_CpuIm1Interrupt(cpu))
				hw_->TakeIrqPulse();
		}
		if (pcm) {
			/* タイマ出力は矩形波。立ち上がり端だけ NMI */
			const int t = pcm->Irq() ? 1 : 0;
			if (t && !k054539TimerState_)
				Ay_CpuNmi(cpu);
			k054539TimerState_ = t;
		}
		return;
	}

	/* pending ラッチ → NMI（System16A / After Burner）または IM1 IRQ（System16B / CPS1 / OutRun）。IFF1 が立つまで IM1 線を保持 — DI 下で ForceIm1 しない。After Burner ラッチは NMI+RETN。 */
	if (hw_->IrqPulsePending()) {
		if (hw_->board_ == CEMU_AC_BOARD_SYS16A
			|| hw_->board_ == CEMU_AC_BOARD_ABURNER
			|| hw_->board_ == CEMU_AC_BOARD_HANGON
			|| hw_->board_ == CEMU_AC_BOARD_SYS18
			|| hw_->board_ == CEMU_AC_BOARD_SYS24
			|| hw_->board_ == CEMU_AC_BOARD_VSYSTEM
			|| hw_->board_ == CEMU_AC_BOARD_SYS32
			|| (hw_->board_ == CEMU_AC_BOARD_SNK_OPL && !hw_->SnkMapKind())) {
			hw_->TakeIrqPulse();
			Ay_CpuNmi(cpu);
		} else if (cpu->r.iff1) {
			hw_->TakeIrqPulse();
			Ay_CpuIm1Interrupt(cpu);
		}
		/* さもなくば: レベルは EI まで pending（Sys16B/CPS1/OutRun 等） */
	}

	/* YM2151 タイマ IRQ — CPS1。初期 Capcom（ghouls/dynwar 等 idle EI;JR-3 @0009）と version-5（megaman/sfzch: LD SP,D800）は Ack 後も上昇端無しでタイマフラグが sticky — それらの系統だけ status から再武装。新しい CPS1（cawing/ffight）は EI 前にフラグをクリア。一括レベルトリガは RETI 後に再入しスタックが爆発。 */
	if (hw_->board_ == CEMU_AC_BOARD_CPS1
		&& !hw_->IrqPulsePending()
		&& chip
		&& cpu->r.iff1 && cpu->r.im == 1) {
		const int earlyCps = hw_->PeekMem(0x0009) == 0xfb
			&& hw_->PeekMem(0x000a) == 0x18
			&& hw_->PeekMem(0x000b) == 0xfd;
		const int cpsVer5 = hw_->PeekMem(0x0003) == 0x31
			&& hw_->PeekMem(0x0004) == 0x00
			&& hw_->PeekMem(0x0005) == 0xd8;
		const int ymPend = chip->Irq()
			|| ((earlyCps || cpsVer5) && ((chip->ReadStatus() & 0x03) != 0));
		if (ymPend && (!irqPaceLive_ || irqPaceDue_ > 0)
			&& Ay_CpuIm1Interrupt(cpu)) {
			chip->AckIrq();
			if (irqPaceLive_ && irqPaceDue_ > 0)
				irqPaceDue_--;
		}
	}
	if (hw_->board_ == CEMU_AC_BOARD_HANGON
		&& !hw_->IrqPulsePending()
		&& chip && chip->Irq()
		&& cpu->r.iff1 && cpu->r.im == 1) {
		if (Ay_CpuIm1Interrupt(cpu))
			chip->AckIrq();
	}
	/* System16B / OutRun: YM2151 タイマは LEVEL IRQ。IFF1 中だけ届ける（EI;NOP;DI を捕捉）。成功 take をレート制限し、sticky status が RETI 後毎命令で再入しないように。DI 中に周期を進めない — EI 窓を飛ばし goldnaxe が SILENT。Sys16A shinobi は 0038 を空にし YM status を poll（IN A,(01);BIT0）— そこで IM1 を撃たない。 */
	if ((hw_->board_ == CEMU_AC_BOARD_SYS16B
			|| hw_->board_ == CEMU_AC_BOARD_OUTRUN)
		&& !hw_->IrqPulsePending()
		&& chip) {
		const int st = (chip->ReadStatus() & 0x03) != 0;
		const int pend = chip->Irq() || st;
		if (pend && cpu->r.iff1 && cpu->r.im == 1) {
			const uint64_t now = (uint64_t)cpu->time64();
			const uint64_t period = (uint64_t)cpuHz_ / 250;
			if (period == 0 || now >= nextGngIrq_) {
				if (Ay_CpuIm1Interrupt(cpu)) {
					if (chip->Irq())
						chip->AckIrq();
					if (period > 0)
						nextGngIrq_ = now + period;
				}
			}
		}
	}
	/* V-System: MAME は ymsnd.irq_handler() を Z80 IRQ 線へ。YM2610 タイマがシーケンサ唯一の時基 — soundlatch NMI は曲番号をキューするだけ。これが無いとドライバはブートし両タイマを組んで全キーオフのまま永久アイドル。ここで AckIrq しない（NeoGeo と同じ）: aerofgt の ISR は Timer B（status bit1）vs Timer A を分岐し、Timer A 経路だけが 7804 の音楽 tick を加算。B 経路が bit1 だけ消したあとのレベル再入が要る（pending A が走る）。AckIrq がソフト線を早く落としオープニング後シーケンサが固まった。 */
	if (hw_->board_ == CEMU_AC_BOARD_VSYSTEM
		&& !hw_->IrqPulsePending()
		&& chip && chip->Irq()
		&& cpu->r.iff1 && cpu->r.im == 1) {
		Ay_CpuIm1Interrupt(cpu);
	}
	/* Gunbird メインループは Timer B の ISR が $8001 に $2F を格納したあとだけ CALL $0B25（FM tick）。IM1 経路が Timer A（status bit0）を取り続けるとシーケンサが進まない — $00A8 のように Timer B を poke + ack。 */
	if (hw_->board_ == CEMU_AC_BOARD_VSYSTEM && hw_->VsIoKind() == 3 && chip) {
		if ((chip->ReadStatus() & 0x02) != 0) {
			if (uint8_t* m = hw_->Mem()) {
				if (m[0x8001] == 0)
					m[0x8001] = 0x2f;
			}
			chip->Write(0, 0x27);
			chip->Write(1, 0x2f);
		}
	}
	/* Toaplan1: YM3812 タイマ IRQ がシーケンサ時基（共有 RAM メールボックスに NMI 無し）。ISR @0038 は status bit6（Timer A）を poll してから音楽＋コマンド poll。Open ブート後に Timer-A をソフトパルスし、init 途中の EI が共有 RAM 準備前に音楽 ISR へ再入しないようにする。 */
	if (hw_->board_ == CEMU_AC_BOARD_TOAPLAN1 && hw_->SlapfghtAy()) {
		/* MAME slapfght: ゲート付き周期 NMI（tigerh 360Hz、alcon/getstar 180）。RST 38 は未使用（LD A,(IX+0)）— YM3812 IM1 を撃たない。 */
		if (hw_->FlstoryNmiEn()) {
			const uint64_t now = (uint64_t)cpu->time64();
			const unsigned hz = hw_->ToaplanYmPort() ? 360u : 180u;
			const uint64_t period = (uint64_t)cpuHz_ / hz;
			if (period > 0 && now >= nextGngIrq_) {
				Ay_CpuNmi(cpu);
				nextGngIrq_ = now + period;
			}
		}
	} else if (hw_->board_ == CEMU_AC_BOARD_TOAPLAN1 && hw_->ToaplanKaneko()) {
		if (hw_->IrqPulsePending()) {
			hw_->TakeIrqPulse();
			Ay_CpuNmi(cpu);
		}
		if (chip) {
			const int st = (chip->ReadStatus() & 0x03) != 0;
			const int pend = chip->Irq() || st;
			if (pend && cpu->r.iff1 && cpu->r.im == 1) {
				if (Ay_CpuIm1Interrupt(cpu)) {
					if (chip->Irq())
						chip->AckIrq();
				}
			}
		}
	} else if (hw_->board_ == CEMU_AC_BOARD_TOAPLAN1) {
		const uint64_t now = (uint64_t)cpu->time64();
		const uint64_t period = (uint64_t)cpuHz_ / 250;
		/* アイドル = EI;JP self、JP self、または JR Z/$-2 待ち（fshark @0400） */
		const unsigned pc = (unsigned)cpu->r.pc;
		const uint8_t b0 = hw_->PeekMem((uint16_t)pc);
		const uint8_t b1 = hw_->PeekMem((uint16_t)(pc + 1u));
		unsigned tgt = 0;
		int idle = 0;
		if (b0 == 0xfb && b1 == 0xc3) {
			tgt = (unsigned)hw_->PeekMem((uint16_t)(pc + 2u))
				| ((unsigned)hw_->PeekMem((uint16_t)(pc + 3u)) << 8);
			idle = (tgt == pc);
		} else if (b0 == 0xc3) {
			tgt = (unsigned)b1
				| ((unsigned)hw_->PeekMem((uint16_t)(pc + 2u)) << 8);
			idle = (tgt == pc);
		} else if (b0 == 0x18 && b1 == 0xfe) {
			idle = 1; /* JR $-2（待ち） */
		} else if ((b0 == 0x28 || b0 == 0x20) && b1 == 0xfe) {
			idle = 1; /* JR Z/NZ, $-2（条件待ち） */
		}
		/* アイドル中はソフト Timer-A。EI なら本物 YM IRQ も届ける — 曲中 DI 窓は RETI 後もチップタイマに頼る。 */
		if (period > 0 && now >= nextGngIrq_ && idle) {
			hw_->SetToaplanTimerA(1);
			nextGngIrq_ = now + period;
			if (cpu->r.iff1 && cpu->r.im == 1)
				Ay_CpuIm1Interrupt(cpu);
		} else if (chip && (chip->Irq() || (chip->ReadStatus() & 0x80))
			&& cpu->r.iff1 && cpu->r.im == 1) {
			Ay_CpuIm1Interrupt(cpu);
		}
	}
	/* SNK68: YM3812 IRQ → Z80 IRQ0（音楽シーケンサ時基）。古典 SNK: (status & 0x0B) の間レベル IRQ — cmd/YM ビット（MAME）。 */
	if (hw_->board_ == CEMU_AC_BOARD_SNK_OPL) {
		if (hw_->SnkMapKind()) {
			/* MAME ym*_irq_handler: ASSERT が status ビットをラッチ。ファームは F800（keep=data>>4）で消す。ラッチ後にチップを Ack し、レベル線がスライス毎にビットを再セットしないようにする。 */
			if (chip && chip->Irq()) {
				hw_->SnkSetYmIrq(0, 1);
				chip->AckIrq();
			}
			if (hw_->Chip2() && hw_->Chip2()->Irq()) {
				/* Athena ISR は status bit0（と cmd bit3）だけ処理。bit1 は後続 F800 書込で消える。YM2 を bit0 へミラーし、0517 の Timer 経路がまだ C0A9 を立て音楽を進めるようにする。 */
				hw_->SnkSetYmIrq(0, 1);
				hw_->SnkSetYmIrq(1, 1);
				hw_->Chip2()->AckIrq();
			}
			/* (status & 0x0B) の間レベル IRQ0。Im1Interrupt のみ — DI 中（04F3 内）の ForceIm1 は入れ子し RAM を 0x39 で埋める。 */
			const uint64_t now = (uint64_t)cpu->time64();
			const uint64_t period = (uint64_t)cpuHz_ / 250;
			if ((hw_->SnkStatus() & 0x0bu) != 0 && period > 0
				&& now >= nextGngIrq_ && cpu->r.iff1) {
				Ay_CpuIm1Interrupt(cpu);
				nextGngIrq_ = now + period;
			}
			if (hw_->IrqPulsePending())
				hw_->TakeIrqPulse();
			return;
		}
		if (!hw_->IrqPulsePending()
			&& chip && chip->Irq()
			&& cpu->r.iff1) {
			if (Ay_CpuIm1Interrupt(cpu))
				chip->AckIrq();
		}
	}
	/* Seibu: RST18（ラッチ）は RST10（YM3812）より優先 — IM0 ベクタを AND しない（タイマ IRQ 生存中に RST18 が落ちる）。 */
	if (hw_->board_ == CEMU_AC_BOARD_SEIBU_OPL) {
		if (chip && chip->Irq())
			hw_->SeibuSetRst10(1);
		const int r18 = hw_->SeibuRst18();
		const int r10 = hw_->SeibuRst10();
		if (cpu->r.iff1) {
			if (r18) {
				if (Ay_CpuRstInterrupt(cpu, 0x18))
					hw_->SeibuSetRst18(0);
			} else if (r10) {
				if (Ay_CpuRstInterrupt(cpu, 0x10)) {
					hw_->SeibuSetRst10(0);
					chip->AckIrq();
				}
			}
		}
		/* 0126 のメインループは (201C)!=0 待ち。RST10 ISR が tick 毎に立てる想定。ISR 経路がゲートだけ消すなら待ちを解放し曲更新が走るように（ホストに VBlank 無し）。 */
		if (uint8_t* m = hw_->Mem()) {
			const unsigned pc = (unsigned)cpu->r.pc;
			if (pc >= 0x0120u && pc <= 0x0130u && m[0x201c] == 0)
				m[0x201c] = 0xff;
		}
		return;
	}
}

void CDriverAc::RunUntil(uint64_t endCycle)
{
	if (!hw_ || !hw_->Cpu()) return;
	Ay_Cpu* cpu = hw_->Cpu();
	CEmuHardAcSetActive(hw_);
	while ((uint64_t)cpu->time64() < endCycle) {
		DeliverIrqs();
		/* HALT + IFF1 クリア: Ay_Cpu HALT は実行予算を消す（s_time&=3）。Open のブート settle が数分になる。代わりにクロックを飛ばす。Alpha 68K-II 音楽は周期 NMI（IFF1 は 0 のまま）。HALT を飛ばすと 7614Hz の DeliverIrqs を飛ばしシーケンサがキーしない。 */
		if (!cpu->r.iff1 && hw_->PeekMem((uint16_t)cpu->r.pc) == 0x76
			&& hw_->board_ != CEMU_AC_BOARD_ALPHA68K2) {
			const uint64_t now = (uint64_t)cpu->time64();
			uint64_t step = (uint64_t)cpuHz_ / 250;
			if (step < 64) step = 64;
			if (now + step > endCycle) step = endCycle - now;
			if (step) {
				cpu->set_time64((int64_t)(now + step));
				hw_->AddCpuCycles(step);
				TickOpm(step);
				continue;
			}
		}
		const int cycles = Ay_CpuRunOne(cpu);
		hw_->AddCpuCycles((uint64_t)cycles);
		TickOpm((uint64_t)cycles);
	}
}

/* Mega System 1: 音源 68000 の唯一の時基は YM2151。タイマ IRQ（とコマンドラッチ）を繰り返しサンプルできるようスライス — Musashi は IACK でラッチレベルを消し自分では再アサートしない。 */
void CDriverAc::Ms1RunCycles(int cycles)
{
	if (!hw_ || cycles <= 0) return;
	CEmuM68kBusSetMs1(hw_);
	CChip* chip = hw_->SoundChip();
	CChip* pcm = hw_->PcmChip();
	const int gx = (hw_->board_ == CEMU_AC_BOARD_KONAMI_GX);
	const int m68kPcm = (hw_->board_ == CEMU_AC_BOARD_M68K_PCM);
	while (cycles > 0) {
		int slice = cycles > 2000 ? 2000 : cycles;
		if (m68kPcm) hw_->M68kPcmTickVblank(slice);
		const int level = hw_->Ms1IrqLevel();
		if (level > 0) {
			m68k_set_irq(level);
			hw_->Ms1AckIrq();
		} else {
			m68k_set_irq(M68K_IRQ_NONE);
		}
		m68k_execute(slice);
		if (hw_->board_ == CEMU_AC_BOARD_KONAMI_RF5C400)
			hw_->HornetTickTimer(slice);
		if (gx) {
			/* K054539 @ 18.432 MHz、音源 68000 @ 16 MHz */
			const uint64_t ticks = (uint64_t)slice * 18432000ull / 16000000ull;
			if (chip) chip->AdvanceClocks(ticks);
			if (pcm) pcm->AdvanceClocks(ticks);
		} else if (m68kPcm) {
			/* X1-010 と YMZ280B は Render で各自のサンプルクロックを回す */
		} else if (chip) {
			/* Mega System 1 では YM2151 は cpuHz/2 */
			chip->AdvanceClocks((uint64_t)slice / 2u);
		}
		cycles -= slice;
	}
	m68k_set_irq(M68K_IRQ_NONE);
}

int CDriverAc::Ms1Render(int16_t* stereo, int frames)
{
	CChip* chip = hw_->SoundChip();
	if (!chip) return 0;
	const int gx = (hw_->board_ == CEMU_AC_BOARD_KONAMI_GX);
	/* CPU とミックスをインターリーブ — スモークは数秒まとめて要求し、事後描画だとチップが終端状態で凍る */
	enum { kChunk = 64 };
	for (int done = 0; done < frames; ) {
		int n = frames - done;
		if (n > (int)kChunk) n = (int)kChunk;
		if (nextCmdAt_ != (uint64_t)~0ull
			&& hw_->CpuCycles() >= nextCmdAt_ && cmdIndex_ < 40) {
			if (heard_ || pinned_) {
				nextCmdAt_ = (uint64_t)~0ull;
			} else {
				TryInjectCommand();
				nextCmdAt_ = hw_->CpuCycles() + (uint64_t)cpuHz_ / 4;
			}
		}
		ms1Acc_ += (int64_t)cpuHz_ * (int64_t)n;
		const int cycles = (int)(ms1Acc_ / (int64_t)hostRate_);
		ms1Acc_ %= (int64_t)hostRate_;
		Ms1RunCycles(cycles);
		int16_t* p = stereo + (size_t)done * 2;
		chip->Render(p, n);
		if (gx) {
			/* K054539×2: 各チップをフルスケール近く。2 本目は MixAdd で約 3/4 にし、tkmmpzdm が 1 本目を埋めずに聞こえるようにする。 */
			if (hw_->PcmChip()) hw_->PcmChip()->MixAdd(p, n, 192);
		} else if (hw_->board_ == CEMU_AC_BOARD_M68K_PCM) {
			/* chip_ が唯一のボイス源。Render は既に p を埋めた */
		} else {
			if (hw_->Oki(0)) hw_->Oki(0)->MixAdd(p, n, 256);
			if (hw_->Oki(1)) hw_->Oki(1)->MixAdd(p, n, 256);
		}
		done += n;
	}
	for (int i = 0; !heard_ && i < frames * 2; i++)
		if (stereo[i]) heard_ = 1;
	return frames;
}

/* Irem M92: V35 の時基は YM2151（INTP0）とサウンドラッチ（INTP1）。タイマ端がドライバの次待ちループより前にサンプルされるよう細かくスライス（フレーム丸ごと後ではない）。 */
void CDriverAc::M92RunCycles(int cycles)
{
	if (!hw_ || cycles <= 0) return;
	V35Cpu* cpu = hw_->M92Cpu();
	if (!cpu) return;
	CEmuV35BusSetM92(hw_);
	CChip* chip = hw_->SoundChip();
	while (cycles > 0) {
		const int slice = cycles > 512 ? 512 : cycles;
		hw_->M92SyncIrqs();
		{
			const int used = V35Execute(cpu, slice);
			const int step = used > 0 ? used : slice;
			hw_->AddCpuCycles((uint64_t)step);
			/* YM2151 と GA20 は両方 XTAL/4 */
			m92OpmRes_ += (uint64_t)step;
			if (chip) chip->AdvanceClocks(m92OpmRes_ / 4u);
			m92OpmRes_ %= 4u;
			cycles -= step;
		}
	}
}

int CDriverAc::M92Render(int16_t* stereo, int frames)
{
	CChip* chip = hw_->SoundChip();
	if (!chip) return 0;
	enum { kChunk = 64 };
	for (int done = 0; done < frames; ) {
		int n = frames - done;
		if (n > (int)kChunk) n = (int)kChunk;
		/* 音声が聞こえるまで周期的に曲を再試行（Z80 基板と同じ考え） */
		if (nextCmdAt_ != (uint64_t)~0ull
			&& hw_->CpuCycles() >= nextCmdAt_ && cmdIndex_ < 40) {
			if (heard_ || pinned_) {
				nextCmdAt_ = (uint64_t)~0ull;
			} else {
				TryInjectCommand();
				nextCmdAt_ = hw_->CpuCycles() + (uint64_t)cpuHz_ / 4;
			}
		}
		m92Acc_ += (int64_t)cpuHz_ * (int64_t)n;
		{
			const int cycles = (int)(m92Acc_ / (int64_t)hostRate_);
			m92Acc_ %= (int64_t)hostRate_;
			M92RunCycles(cycles);
		}
		{
			int16_t* p = stereo + (size_t)done * 2;
			chip->Render(p, n);
			if (hw_->PcmChip()) hw_->PcmChip()->MixAdd(p, n, 256);
		}
		done += n;
	}
	for (int i = 0; !heard_ && i < frames * 2; i++)
		if (stereo[i]) heard_ = 1;
	return frames;
}

void CDriverAc::DecoRunCycles(int cycles)
{
	if (!hw_ || cycles <= 0) return;
	M6502Cpu* m6502 = hw_->DecoM6502();
	if (m6502) {
		CChip* chip = hw_->SoundChip();
		CChip* ym2203 = hw_->Chip2();
		while (cycles > 0) {
			const int slice = cycles > 512 ? 512 : cycles;
			hw_->DecoSyncIrqs();
			const int used = M6502Execute(m6502, slice);
			const int step = used > 0 ? used : slice;
			hw_->AddCpuCycles((uint64_t)step);
			/* OPL @ 3 MHz、CPU @ 1.5 → 2 倍クロック。OPN は CPU レートを共有 */
			if (chip) chip->AdvanceClocks((uint64_t)step * 2u);
			if (ym2203) ym2203->AdvanceClocks((uint64_t)step);
			if (hw_->PcmChip()) hw_->PcmChip()->AdvanceClocks((uint64_t)step);
			cycles -= step;
		}
		return;
	}
	H6280Cpu* cpu = hw_->DecoCpu();
	if (!cpu) return;
	CEmuH6280BusSetDeco(hw_);
	CChip* chip = hw_->SoundChip();
	CChip* ym2203 = hw_->Chip2();
	/* cpuHz = XTAL/8、YM2151 = XTAL/9 → YM を cycles * 8/9 進める。YM2203 / OKI1 は CPU と同じ XTAL/8。OKI2 は XTAL/16 = cpu/2。 */
	while (cycles > 0) {
		const int slice = cycles > 512 ? 512 : cycles;
		/* タイマ生存中は YM→IRQ2 を約 250Hz でパルス。スライス毎の sticky レベルは reg14 だけ消し曲枠更新を飢えた（BLAST）。 */
		const uint64_t now = hw_->CpuCycles();
		const uint64_t period = (uint64_t)cpuHz_ / 250;
		const int ymPend = chip && (chip->Irq() || (chip->ReadStatus() & 0x03));
		if (ymPend && (period == 0 || now >= decoNextYmIrq_)) {
			hw_->DecoSyncIrqs();
			if (period > 0)
				decoNextYmIrq_ = now + period;
			/* ここで AckIrq しない — HuC6280 ISR がタイマ status ビットを見る必要がある。ソフト線は下のパルス間で落とす。 */
		} else {
			/* ラッチ/IRQ1 + MPR 修正はまだ同期が要る。パルス間で YM 線を落とす */
			hw_->DecoSyncIrqs();
			H6280SetInputLine(cpu, H6280_LINE_IRQ2, H6280_CLEAR_LINE);
		}
		const int used = H6280Execute(cpu, slice);
		const int step = used > 0 ? used : slice;
		hw_->AddCpuCycles((uint64_t)step);
		decoChipRes_ += (uint64_t)step * 8u;
		if (chip) {
			const uint64_t ym = decoChipRes_ / 9u;
			chip->AdvanceClocks(ym);
			decoChipRes_ %= 9u;
		}
		if (ym2203) ym2203->AdvanceClocks((uint64_t)step);
		if (hw_->PcmChip()) hw_->PcmChip()->AdvanceClocks((uint64_t)step);
		if (hw_->Oki(1)) hw_->Oki(1)->AdvanceClocks((uint64_t)step / 2u);
		cycles -= step;
	}
}

int CDriverAc::DecoRender(int16_t* stereo, int frames)
{
	CChip* chip = hw_->SoundChip();
	if (!chip) return 0;
	enum { kChunk = 64 };
	for (int done = 0; done < frames; ) {
		int n = frames - done;
		if (n > (int)kChunk) n = (int)kChunk;
		if (nextCmdAt_ != (uint64_t)~0ull
			&& hw_->CpuCycles() >= nextCmdAt_ && cmdIndex_ < 1) {
			/* 注入は 1 回だけ: BGM id の繰り返しは FM チャネル枠をクリア */
			TryInjectCommand();
			nextCmdAt_ = (uint64_t)~0ull;
		}
		decoAcc_ += (int64_t)cpuHz_ * (int64_t)n;
		{
			const int cycles = (int)(decoAcc_ / (int64_t)hostRate_);
			decoAcc_ %= (int64_t)hostRate_;
			DecoRunCycles(cycles);
		}
		{
			int16_t* p = stereo + (size_t)done * 2;
			chip->Render(p, n);
			if (hw_->Chip2()) {
				int16_t* mix = Scratch(n);
				if (mix) {
					hw_->Chip2()->Render(mix, n);
					for (int i = 0; i < n * 2; i++) {
						int s = (int)p[i] + ((int)mix[i] * 192) / 256;
						if (s > 32767) s = 32767;
						if (s < -32768) s = -32768;
						p[i] = (int16_t)s;
					}
				}
			}
			if (hw_->PcmChip()) hw_->PcmChip()->MixAdd(p, n, 256);
			if (hw_->Oki(1)) hw_->Oki(1)->MixAdd(p, n, 192);
		}
		done += n;
	}
	for (int i = 0; !heard_ && i < frames * 2; i++)
		if (stereo[i]) heard_ = 1;
	return frames;
}

void CDriverAc::NamcoM6809RunCycles(int cycles)
{
	if (!hw_ || cycles <= 0) return;
	mc6809__t* cpu = (mc6809__t*)hw_->NamcoM6809Cpu();
	if (!cpu) return;
	CChip* chip = hw_->SoundChip();
	unsigned long start = cpu->cycles;
	const unsigned long target = start + (unsigned long)cycles;
	int steps = 0;
	const int kMaxSteps = cycles * 8 + 1024;
	unsigned long clocksPending = 0;
	while (cpu->cycles < target && steps < kMaxSteps) {
		hw_->NamcoM6809SyncIrqs();
		/* I=1 の間ホストハンドシェイクを生かす — gaplus/grobda/motos がマジック比較を終える前に $40 へ曲 id を載せるとサブ CPU がリセットで永久スピン。 */
		if (hw_->WsgMappy() && cpu->cc.i && (steps & 63) == 0) {
			const uint16_t pc = cpu->pc.w;
			const uint8_t o0 = hw_->NamcoM6809Read8(pc);
			const uint8_t o1 = hw_->NamcoM6809Read8((uint16_t)(pc + 1));
			const uint8_t o2 = hw_->NamcoM6809Read8((uint16_t)(pc + 2));
			const uint8_t o3 = hw_->NamcoM6809Read8((uint16_t)(pc + 3));
			const uint8_t o4 = hw_->NamcoM6809Read8((uint16_t)(pc + 4));
			const uint8_t o5 = hw_->NamcoM6809Read8((uint16_t)(pc + 5));
			const uint8_t o6 = hw_->NamcoM6809Read8((uint16_t)(pc + 6));
			/* 正確な待ち先頭だけ — STA $3000 は gaplus チェックサム／IRQ に現れ、ライブ曲 id の上に $11 で $40 を再毒してはいけない。 */
			if (o0 == 0xdc && o1 == 0x40 && o2 == 0x10 && o3 == 0x83) {
				hw_->NamcoM6809Write8(0x0040, o4);
				hw_->NamcoM6809Write8(0x0041, o5);
			} else if (o0 == 0x9e && o1 == 0x40 && o2 == 0x8c) {
				hw_->NamcoM6809Write8(0x0040, o3);
				hw_->NamcoM6809Write8(0x0041, o4);
			} else if (o0 == 0x96 && o1 == 0x40 && o2 == 0x81) {
				hw_->NamcoM6809Write8(0x0040, o3);
			} else if (o0 == 0x96 && o1 == 0x40 && o2 == 0xb7 && o5 == 0x81) {
				hw_->NamcoM6809Write8(0x0040, o6);
			} else if (o0 == 0x96 && o1 == 0xfb && o2 == 0x27) {
				hw_->NamcoM6809Write8(0x00fb, 0x40);
			} else if (o0 == 0xec && o1 == 0x84 && o2 == 0x26 && o3 == 0xfc) {
				hw_->NamcoM6809Write8(0x0040, 0);
				hw_->NamcoM6809Write8(0x0041, 0);
			} else if (o0 == 0x91 && o1 == 0x41 && o2 == 0x27) {
				/* phozon: CMPA $41 / BEQ — ホストは今格納した番兵（通常 $02）から $41 を変えなければならない */
				hw_->NamcoM6809Write8(0x0041, 0);
			} else if (o0 == 0x8c && o1 == 0x00 && o2 == 0x00 && o3 == 0x26
				&& pc >= 0xe014u && pc <= 0xe019u) {
				/* liblrabl/gaplus ROM チェックサム CMPX #0 — 完了を強制 */
				if (hw_->NamcoM6809Read8(0xe01b) == 0x81
					&& hw_->NamcoM6809Read8(0xe01c) == 0xddu) {
					cpu->A = 0xddu;
					cpu->X.w = 0;
					cpu->pc.w = 0xe01bu;
				} else if (hw_->NamcoM6809Read8(0xe01a) == 0x81
					&& hw_->NamcoM6809Read8(0xe01b) == 0x00) {
					cpu->A = 0;
					cpu->X.w = 0;
					cpu->pc.w = 0xe01au;
				}
			}
		}
		const unsigned long before = cpu->cycles;
		const int rc = mc6809_step(cpu);
		steps++;
		if (rc != 0) break;
		if (cpu->cycles == before)
			cpu->cycles++; /* CWAI/SYNC 安全 */
		clocksPending += (unsigned long)(cpu->cycles - before);
		/* タイマ→FIRQ のため YM を十分 tick。末尾一括は BGM を飢えた */
		if (clocksPending >= 64u) {
			if (chip) chip->AdvanceClocks((uint64_t)clocksPending);
			if (hw_->PcmChip()) hw_->PcmChip()->AdvanceClocks((uint64_t)clocksPending);
			clocksPending = 0;
		}
		/* S はワーク RAM 内。Sys2 ファームは LDS #$A000（空スタック。最初の push は $9FFF）。毎命令 A000→9FF0 クランプはその番兵と戦った。Mappy 期 WSG: LDS #$0400。 */
		if (hw_->WsgMappy()) {
			if (cpu->S.w < 0x03c0u || cpu->S.w > 0x0400u)
				cpu->S.w = 0x03f0;
		} else if (cpu->S.w < 0x8000u || cpu->S.w > 0xa000u) {
			cpu->S.w = 0x9ff0;
		}
	}
	if (clocksPending) {
		if (chip) chip->AdvanceClocks((uint64_t)clocksPending);
		if (hw_->PcmChip()) hw_->PcmChip()->AdvanceClocks((uint64_t)clocksPending);
	}
	const int stepped = (int)(cpu->cycles - start);
	if (stepped > 0)
		hw_->AddCpuCycles((uint64_t)stepped);
}

int CDriverAc::NamcoM6809Render(int16_t* stereo, int frames)
{
	CChip* chip = hw_->SoundChip();
	if (!chip) return 0;
	enum { kChunk = 64 };
	for (int done = 0; done < frames; ) {
		int n = frames - done;
		if (n > (int)kChunk) n = (int)kChunk;
		if (nextCmdAt_ != (uint64_t)~0ull
			&& hw_->CpuCycles() >= nextCmdAt_) {
			if (hw_->board_ == CEMU_AC_BOARD_NAMCO_WSG && hw_->WsgMappy()) {
				mc6809__t* cpu = (mc6809__t*)hw_->NamcoM6809Cpu();
				if (cmdIndex_ < 1 && cpu && !cpu->cc.i)
					TryInjectCommand();
				nextCmdAt_ = (cmdIndex_ < 1)
					? hw_->CpuCycles() + (uint64_t)cpuHz_ / 60
					: (uint64_t)~0ull;
			} else if (cmdIndex_ < 1) {
				TryInjectCommand();
				nextCmdAt_ = (uint64_t)~0ull;
			}
		}
		namcoAcc_ += (int64_t)cpuHz_ * (int64_t)n;
		{
			const int cycles = (int)(namcoAcc_ / (int64_t)hostRate_);
			namcoAcc_ %= (int64_t)hostRate_;
			NamcoM6809RunCycles(cycles > 4096 ? 4096 : cycles);
		}
		{
			int16_t* p = stereo + (size_t)done * 2;
			chip->Render(p, n);
			if (hw_->PcmChip())
				hw_->PcmChip()->MixAdd(p, n, 256);
		}
		done += n;
	}
	for (int i = 0; !heard_ && i < frames * 2; i++)
		if (stereo[i]) heard_ = 1;
	return frames;
}

void CDriverAc::M62RunCycles(int cycles)
{
	if (!hw_ || !hw_->M6803Cpu() || cycles <= 0) return;
	struct m6800* cpu = hw_->M6803Cpu();
	CChip* chip = hw_->SoundChip();
	int left = cycles;
	int guard = cycles * 8 + 256;
	/* OCF/TOF/SCI/ICF を落とす — それらのベクタは M62 ROM で RST stub。MSM5205 VCK→NMI 約 4kHz: *実行* サイクルだけで積算。 */
	static uint64_t s_nmiAcc;
	while (left > 0 && guard-- > 0) {
		cpu->irq &= (IRQ_NMI | IRQ_IRQ1);
		const int used = m6800_execute(cpu);
		const int step = used > 0 ? used : 1;
		left -= step;
		hw_->AddCpuCycles((uint64_t)step);
		if (chip) chip->AdvanceClocks((uint64_t)step);
		if (hw_->Chip2()) hw_->Chip2()->AdvanceClocks((uint64_t)step);
		s_nmiAcc += (uint64_t)step;
		if (s_nmiAcc >= (uint64_t)(hw_->cpuHz_ / 4000 + 1)) {
			s_nmiAcc = 0;
			m6800_raise_interrupt(cpu, IRQ_NMI);
		}
	}
}

int CDriverAc::M62Render(int16_t* stereo, int frames)
{
	CChip* chip = hw_->SoundChip();
	if (!chip) return 0;
	enum { kChunk = 64 };
	for (int done = 0; done < frames; ) {
		int n = frames - done;
		if (n > (int)kChunk) n = (int)kChunk;
		/* 無音の間、曲メールボックス＋ラッチを再種まき（同じカタログ id のみ — メイン CPU 風再試行であり試行表ハントではない）。 */
		if (!heard_ && hw_->CpuCycles() < (uint64_t)cpuHz_ * 3ull) {
			uint8_t song = songCmd_ ? (uint8_t)(songCmd_ & 0x7fu) : 0x20;
			if (!song) song = 0x20;
			struct m6800* cpu = hw_->M6803Cpu();
			if (cpu) {
				const uint8_t slots[] = { 0xbc, 0xc6, 0xc7, 0xcc, 0 };
				for (int i = 0; slots[i]; i++) {
					const uint8_t a = slots[i];
					if (cpu->iram_base <= a && a <= 0xff)
						cpu->iram[a - cpu->iram_base] = song;
				}
			}
			if ((hw_->CpuCycles() % (uint64_t)(cpuHz_ / 4 + 1)) < 256u) {
				hw_->SetSoundCommand(song);
			} else if (hw_->SoundCommand() && !(hw_->SoundCommand() & 0x80)) {
				hw_->SetSoundCommand(0x80);
			}
		} else if (hw_->SoundCommand() && !(hw_->SoundCommand() & 0x80)
			&& hw_->CpuCycles() > (uint64_t)cpuHz_ / 8) {
			hw_->SetSoundCommand(0x80);
		}
		m62Acc_ += (int64_t)cpuHz_ * (int64_t)n;
		{
			const int cycles = (int)(m62Acc_ / (int64_t)hostRate_);
			m62Acc_ %= (int64_t)hostRate_;
			M62RunCycles(cycles > 8192 ? 8192 : cycles);
		}
		{
			int16_t* p = stereo + (size_t)done * 2;
			chip->Render(p, n);
			if (hw_->Chip2()) {
				int16_t* mix = Scratch(n);
				if (mix) {
					hw_->Chip2()->Render(mix, n);
					for (int i = 0; i < n * 2; i++) {
						int s = (int)p[i] + (int)mix[i];
						if (s > 32767) s = 32767;
						if (s < -32768) s = -32768;
						p[i] = (int16_t)s;
					}
				}
			}
		}
		done += n;
	}
	for (int i = 0; !heard_ && i < frames * 2; i++)
		if (stereo[i]) heard_ = 1;
	return frames;
}

void CDriverAc::Sega68RunCycles(int cycles)
{
	if (!hw_ || cycles <= 0) return;
	CEmuM68kBusSetMs1(hw_);
	CChip* chip = hw_->SoundChip();
	CChip* pcm = hw_->PcmChip();
	CChip* pcm2 = hw_->Oki(1);
	while (cycles > 0) {
		int slice = cycles > 2000 ? 2000 : cycles;
		const int level = hw_->Ms1IrqLevel();
		if (level > 0)
			m68k_set_irq(level);
		else
			m68k_set_irq(M68K_IRQ_NONE);
		m68k_execute(slice);
		hw_->AddCpuCycles((uint64_t)slice);
		/* YM は opmHz_（segam1audio では 8 MHz）。この 68000 は cpuHz_（10 MHz）。生 CPU サイクルを食わせるとタイマが 25% 速く、ファームは Timer B でシーケンサをペース — daytona が 1/4 速かった。MultiPCM は 68000 の 10 MHz を共有。 */
		if (chip && cpuHz_ > 0 && opmHz_ > 0) {
			opmResidual_ += (uint64_t)slice * (uint64_t)opmHz_;
			const uint64_t opmTicks = opmResidual_ / (uint64_t)cpuHz_;
			opmResidual_ %= (uint64_t)cpuHz_;
			if (opmTicks) chip->AdvanceClocks(opmTicks);
		} else if (chip) {
			chip->AdvanceClocks((uint64_t)slice);
		}
		if (pcm) pcm->AdvanceClocks((uint64_t)slice);
		if (pcm2) pcm2->AdvanceClocks((uint64_t)slice);
		cycles -= slice;
	}
}

int CDriverAc::Sega68Render(int16_t* stereo, int frames)
{
	CChip* chip = hw_->SoundChip();
	if (!stereo || frames <= 0) return 0;
	enum { kChunk = 64 };
	for (int done = 0; done < frames; ) {
		int n = frames - done;
		if (n > (int)kChunk) n = (int)kChunk;
		if (nextCmdAt_ != (uint64_t)~0ull
			&& hw_->CpuCycles() >= nextCmdAt_ && cmdIndex_ < 4) {
			TryInjectCommand();
			if (cmdIndex_ < 4)
				nextCmdAt_ = hw_->CpuCycles() + (uint64_t)cpuHz_ / 4;
			else
				nextCmdAt_ = (uint64_t)~0ull;
		}
		sega68Acc_ += (int64_t)cpuHz_ * (int64_t)n;
		{
			const int cycles = (int)(sega68Acc_ / (int64_t)hostRate_);
			sega68Acc_ %= (int64_t)hostRate_;
			Sega68RunCycles(cycles > 8192 ? 8192 : cycles);
		}
		int16_t* p = stereo + (size_t)done * 2;
		memset(p, 0, (size_t)n * 2 * sizeof(int16_t));
		/* MAME segam1audio: MultiPCM 各 0.5、YM3438 0.30。旧ゲイン 768（3.0x）は死んだ 68K イメージの補償で、ファームが実際に鳴らすとレールクリップ。 */
		if (hw_->PcmChip()) hw_->PcmChip()->MixAdd(p, n, 128);
		if (hw_->Oki(1)) hw_->Oki(1)->MixAdd(p, n, 128);
		if (chip) {
			int16_t* mix = Scratch(n);
			if (mix) {
				/* Model 1 は YM3438 を MultiPCM×2 の隣で 0.30。Model 2A/3 では SCSP が基板全体なのでフルのまま。 */
				const int g = hw_->SegaM1Audio() ? 77 : 256;
				chip->Render(mix, n);
				for (int i = 0; i < n * 2; i++) {
					int s = (int)p[i] + ((int)mix[i] * g) / 256;
					if (s > 32767) s = 32767;
					if (s < -32768) s = -32768;
					p[i] = (int16_t)s;
				}
			}
		}
		done += n;
	}
	for (int i = 0; !heard_ && i < frames * 2; i++)
		if (stereo[i]) heard_ = 1;
	return frames;
}

void CDriverAc::H8RunCycles(int cycles)
{
	if (!hw_ || cycles <= 0) return;
	H8Cpu* cpu = hw_->H8CpuPtr();
	if (!cpu) return;
	CEmuH8BusSetAc(hw_);
	CChip* chip = hw_->SoundChip();
	while (cycles > 0) {
		const int slice = cycles > 1024 ? 1024 : cycles;
		/* Sys12/ND-1 のビジーフラグをクリアしメインループが走れるようにする */
		if (hw_->H8Shared()) {
			const_cast<uint8_t*>(hw_->H8Shared())[0x4050] = 0;
		}
		const int used = H8Execute(cpu, slice);
		const int step = used > 0 ? used : slice;
		hw_->AddCpuCycles((uint64_t)step);
		/* MAME namcos12_sub_irq: 画面 vblank が外部 IRQ1 を駆動。IRQ5 は誤った H8 ベクタを選び C76 シーケンサが無関係ハンドラから走る。この切り離しコアには vblank 1 要求で足りる（外部要求はエッジラッチ）。 */
		if ((hw_->CpuCycles() / (uint64_t)(hw_->cpuHz_ / 60 + 1))
			!= ((hw_->CpuCycles() - (uint64_t)step) / (uint64_t)(hw_->cpuHz_ / 60 + 1)))
			H8SetInputLine(cpu, H8_LINE_IRQ1, H8_ASSERT_LINE);
		(void)chip;
		cycles -= step;
	}
}

int CDriverAc::H8Render(int16_t* stereo, int frames)
{
	CChip* chip = hw_->SoundChip();
	if (!chip) return 0;
	enum { kChunk = 64 };
	for (int done = 0; done < frames; ) {
		int n = frames - done;
		if (n > (int)kChunk) n = (int)kChunk;
		h8Acc_ += (int64_t)cpuHz_ * (int64_t)n;
		{
			const int cycles = (int)(h8Acc_ / (int64_t)hostRate_);
			h8Acc_ %= (int64_t)hostRate_;
			H8RunCycles(cycles);
		}
		{
			int16_t* p = stereo + (size_t)done * 2;
			chip->Render(p, n);
		}
		done += n;
	}
	for (int i = 0; !heard_ && i < frames * 2; i++)
		if (stereo[i]) heard_ = 1;
	return frames;
}

void CDriverAc::M37702RunCycles(int cycles)
{
	if (!hw_ || cycles <= 0) return;
	M37702Cpu* cpu = hw_->M37702CpuPtr();
	if (!cpu) return;
	CEmuM37702BusSetAc(hw_);
	CChip* chip = hw_->SoundChip();
	while (cycles > 0) {
		const int slice = cycles > 1024 ? 1024 : cycles;
		const int used = M37702Execute(cpu, slice);
		const int step = used > 0 ? used : slice;
		hw_->AddCpuCycles((uint64_t)step);
		/* 約 60Hz ホスト tick。System 22 には不要 — Timer A0 でシーケンス。他では IRQ0 がドライバの欲しいもの: System 11 のハンドラがメインループ待ちフラグを立てる。IRQ2 はメイン CPU ハンドシェイク。C7x マスク ROM ではそのハンドラが返信待ちでブロックし、メイン CPU が無いので MCU が割り込み優先度に駐車し tick が戻らない。ゲーム ROM 上の自前ドライバはそれを期待する。NA-1/NA-2 も不要: 唯一のホスト割り込みは 68000 のメール枠 4 書込。C69 BIOS が $01E0 の RAM ベクタ表を埋める前の tick は JMP ($01F0) で虚空へ。 */
		const int naC69 = (hw_->M37702MapKind() == 1 && !hw_->M37702McuKind());
		if (hw_->M37702MapKind() != 2 && !naC69
			&& (hw_->CpuCycles() / (uint64_t)(hw_->cpuHz_ / 60 + 1))
			!= ((hw_->CpuCycles() - (uint64_t)step) / (uint64_t)(hw_->cpuHz_ / 60 + 1))) {
			M37702SetInputLine(cpu, M37710_LINE_IRQ0, M37702_HOLD_LINE);
			if (!hw_->M37702McuKind())
				M37702SetInputLine(cpu, M37710_LINE_IRQ2, M37702_HOLD_LINE);
		}
		if (chip) chip->AdvanceClocks((uint64_t)step);
		cycles -= step;
	}
}

int CDriverAc::M37702Render(int16_t* stereo, int frames)
{
	CChip* chip = hw_->SoundChip();
	if (!chip) return 0;
	enum { kChunk = 64 };
	for (int done = 0; done < frames; ) {
		int n = frames - done;
		if (n > (int)kChunk) n = (int)kChunk;
		m37702Acc_ += (int64_t)cpuHz_ * (int64_t)n;
		{
			const int cycles = (int)(m37702Acc_ / (int64_t)hostRate_);
			m37702Acc_ %= (int64_t)hostRate_;
			M37702RunCycles(cycles);
		}
		{
			int16_t* p = stereo + (size_t)done * 2;
			chip->Render(p, n);
		}
		done += n;
	}
	for (int i = 0; !heard_ && i < frames * 2; i++)
		if (stereo[i]) heard_ = 1;
	return frames;
}


void CDriverAc::Sys86RunCycles(int cycles)
{
	if (!hw_ || cycles <= 0) return;
	HD63701Cpu* cpu = hw_->HD63701CpuPtr();
	if (!cpu) return;
	CEmuHD63701BusSetAc(hw_);
	CChip* chip = hw_->SoundChip();
	/* FBNeo: フレーム毎に HOLD vblank IRQ 1 回。CLI も 256 サイクル execute の入れ子もしない（$81CD 再入で AE を潰した）。
	   Sys86 曲開始 F4DD は B0 を立て $80A9 を JSR: F14A が $11C0 にベクタ表を再構築、CLI、F20A が $14F0 へリロケ。その窓の IRQ はコピーに C6/C8 を使い終わらない（PC が F364 で固まり AE が 11C0、KeyOn 無し）。AE=$14xx まで待つ。
	   $81CD / F382 は $1182=$A6 の間だけ音楽チェインを走る。ファームは F20A 後 $811F で A6 を書くが、リロケ後に再武装し $80A9 の CLR $1182 がプレーヤを飢えないようにする。
	   F4DD は $1183=1（メイン CPU「枠ビジー」）も格納。$8327 / F249 はまだ $1183 を曲 id として読むので注入コマンドを復元。 */
	while (cycles > 0) {
		const int slice = cycles > 1024 ? 1024 : cycles;
		const int used = HD63701Execute(cpu, slice);
		const int step = used > 0 ? used : slice;
		hw_->AddCpuCycles((uint64_t)step);
		{
			const uint8_t b0 = hw_->HD63701Read8(0x00b0);
			const uint8_t door = hw_->HD63701Read8(0x1182);
			const uint8_t ae = hw_->HD63701Read8(0x00ae);
			const uint16_t pc = HD63701Pc(cpu);
			const int relocated = (ae == 0x14u);
			/* F4DD は AE がまだ $14F0、I クリアのまま B0 を立てる。$80A9 はまだ SEI していない。そこの vblank は半再構築表に $81CD を走らせ F33F へ着地。F20A + CLI 後の $81xx アイドル poll を待つ。 */
			/* roishtar の F249/844E は $8108/$810B（他ゲームはそれらの JSR を $80xx）。アイドルを $8128 まで遅らせ、EOCI/IRQ がその mute パスに入れ子しないようにする。 */
			const unsigned idleLo = (!hw_->Wsg63701() && hw_->HD63701MapKind() == 1)
				? 0x8128u : 0x8100u;
			const int idle = (pc >= idleLo && pc < 0x9000u);
			/* roishtar 80A9 は $1400-$2000 を CLR（813E）してから F20A。リロケ後の IRQ はまだ消された $14F8 に着地し得る（478F → TRAP → FF78 fault 8）。$11C0 は CUS30 で生き残る。それを戻す。 */
			if (!hw_->Wsg63701() && hw_->HD63701MapKind() == 1
				&& relocated && idle
				&& (hw_->HD63701Read8(0x14f8) != 0x81u
					|| hw_->HD63701Read8(0x14f9) != 0xd9u)
				&& hw_->HD63701Read8(0x11c8) == 0x81u
				&& hw_->HD63701Read8(0x11c9) == 0xd9u) {
				unsigned i;
				for (i = 0; i < 0x7cu; i++)
					hw_->HD63701Write8((uint16_t)(0x14f0u + i),
						hw_->HD63701Read8((uint16_t)(0x11c0u + i)));
			}
			int ociArmed = 0;
			if (!hw_->Wsg63701()) {
				/* EOCI は $80DA で武装し、フリーランタイマは 8147 の F220（$AC をゼロ）を通して割り込み続ける。F3C4 は JSR [AC] で $0000 へ。アイドル poll まで OCI をマスク。roishtar の F249/844E は $8108/$810B（skykiddx はまだ $80xx）。アイドルはそれらの JSR のあと $8128 から。 */
				const uint8_t tcsr = hw_->HD63701Read8(0x0008);
				if (!idle && (!b0 || !relocated) && (tcsr & 0x08u)) {
					hw_->HD63701Write8(0x0008, (uint8_t)(tcsr & ~0x08u));
					sys86OciNeed_ = 1;
				} else if (idle && b0 && relocated && sys86OciNeed_) {
					hw_->HD63701Write8(0x0008, (uint8_t)(tcsr | 0x08u));
					sys86OciNeed_ = 0;
					ociArmed = 1;
				}
			}
			if (!hw_->Wsg63701() && b0 && idle && songCmd_) {
				if (songCmd_ > 1u && hw_->HD63701Read8(0x1183) == 1u)
					hw_->HD63701Write8(0x1183, songCmd_);
				/* 846B: $1380=0 は skip、$FF=再生中、他は曲添字。F110 RAM テストが消す。アイドルループが立ってから一度だけ poke し、F14A/F20A 中に OCI が YM 表を引かないようにする。 */
				const uint8_t yreq = hw_->HD63701Read8(0x1380);
				if (yreq == 0)
					hw_->HD63701Write8(0x1380, songCmd_);
			}
			if (b0 && relocated && idle && door != 0xa6u)
				hw_->HD63701Write8(0x1182, 0xa6u);
			const int frameEdge = ((hw_->CpuCycles() / (uint64_t)(hw_->cpuHz_ / 60 + 1))
				!= ((hw_->CpuCycles() - (uint64_t)step) / (uint64_t)(hw_->cpuHz_ / 60 + 1)));
			int fire = b0 && relocated && idle && !ociArmed;
			if (hw_->Wsg63701()) {
				const int aeReady = (ae >= 0x80u) || (ae == 0x11u) || (ae == 0xc0u)
					|| relocated;
				fire = aeReady && !(door == 0xa6u && b0 == 0);
			}
			if (frameEdge && fire)
				HD63701SetInputLine(cpu, HD63701_LINE_IRQ, HD63701_HOLD_LINE);
		}
		if (chip) chip->AdvanceClocks((uint64_t)step);
		if (hw_->PcmChip()) hw_->PcmChip()->AdvanceClocks((uint64_t)step);
		cycles -= step;
	}
}

int CDriverAc::Sys86Render(int16_t* stereo, int frames)
{
	CChip* chip = hw_->SoundChip();
	if (!chip) return 0;
	enum { kChunk = 64 };
	for (int done = 0; done < frames; ) {
		int n = frames - done;
		if (n > (int)kChunk) n = (int)kChunk;
	/* Sys86: Open でワンショット — 再注入しない（sticky $1182 が KeyOn を殺す） */
	if (nextCmdAt_ != (uint64_t)~0ull
		&& hw_->CpuCycles() >= nextCmdAt_ && cmdIndex_ < 1
		&& hw_->board_ != CEMU_AC_BOARD_NAMCO_SYS86) {
			TryInjectCommand();
			nextCmdAt_ = hw_->CpuCycles() + (uint64_t)cpuHz_ / 2;
		}
		sys86Acc_ += (int64_t)cpuHz_ * (int64_t)n;
		{
			const int cycles = (int)(sys86Acc_ / (int64_t)hostRate_);
			sys86Acc_ %= (int64_t)hostRate_;
			Sys86RunCycles(cycles > 4096 ? 4096 : cycles);
		}
		{
			int16_t* p = stereo + (size_t)done * 2;
			chip->Render(p, n);
			/* MAME namcos86: YM2151 右チャネルのみ（左は 0 でルート） */
			if (!hw_->Wsg63701()) {
				for (int i = 0; i < n; i++)
					p[i * 2] = p[i * 2 + 1];
			}
			if (hw_->PcmChip())
				hw_->PcmChip()->MixAdd(p, n, 256);
		}
		done += n;
	}
	for (int i = 0; !heard_ && i < frames * 2; i++)
		if (stereo[i]) heard_ = 1;
	return frames;
}

int CDriverAc::Render(int16_t* stereo, int frames)
{
	if (!hw_ || !stereo || frames <= 0) return 0;
	if (h8Board_) return H8Render(stereo, frames);
	if (m37702Board_) return M37702Render(stereo, frames);
	if (deco_) return DecoRender(stereo, frames);
	if (namcoM6809_) return NamcoM6809Render(stereo, frames);
	if (sys86_) return Sys86Render(stereo, frames);
	if (m62_) return M62Render(stereo, frames);
	if (sega68_) return Sega68Render(stereo, frames);
	if (m92_) return M92Render(stereo, frames);
	if (ms1_) return Ms1Render(stereo, frames);
	Ay_Cpu* cpu = hw_->Cpu();
	CChip* chip = hw_->SoundChip();
	if (!cpu || !chip) return 0;
	CEmuHardAcSetActive(hw_);

	if (hostRate_ < 1 || cpuHz_ < 1) return 0;
	if (hw_->board_ == CEMU_AC_BOARD_CPS1
		|| hw_->board_ == CEMU_AC_BOARD_CPS_QS)
		irqPaceLive_ = 1;

	if (!hasCpu_) {
		/* この基板に音源 CPU コアは無い — 組んだチップを描画（他が駆動しなければ無音） */
		chip->Render(stereo, frames);
		if (hw_->Chip2()) {
			int16_t* mix2 = Scratch(frames);
			if (mix2) {
				hw_->Chip2()->Render(mix2, frames);
				for (int i = 0; i < frames * 2; i++) {
					int s = (int)stereo[i] + (int)mix2[i];
					if (s > 32767) s = 32767;
					if (s < -32768) s = -32768;
					stereo[i] = (int16_t)s;
				}
			}
		}
		if (hw_->PcmChip())
			hw_->PcmChip()->MixAdd(stereo, frames, 256);
		return frames;
	}

	/* 補助ボイスチップ（GNG YM2203×2、System1 SN×2、Taito SJ AY×3）に MixAdd は無いのでスクラッチへ描画してから加算 */
	CChip* pcm = hw_->PcmChip();
	/* mystwarr 系は同じ Z80 マップに 2 本目 K054539。K054539 専用アクセサが必須: pcm2_ は他基板（MegaSystem1/DECO 2 本目 OKI）でも使い、そちらのチップは別経路。ここで混ぜると 57 アーカイブで故障。 */
	CChip* pcm2 = hw_->KonamiPcm2();
	/* Batrider の 2 本目 OKI は同じ Z80（I/O 82 と 84）なのでこのミックスに含める */
	if (!pcm2 && hw_->board_ == CEMU_AC_BOARD_RAIZING)
		pcm2 = hw_->Oki(1);
	if (!pcm2 && hw_->board_ == CEMU_AC_BOARD_TECMO16 && hw_->TecmoOpl() == 5)
		pcm2 = hw_->Oki(1);
	const int auxCount = (hw_->Chip3() ? 2 : (hw_->Chip2() ? 1 : 0));
	int16_t* mix2 = auxCount ? Scratch(frames * auxCount) : NULL;
	int16_t* mix3 = (mix2 && auxCount > 1) ? mix2 + (size_t)frames * 2 : NULL;

	for (int i = 0; i < frames; i++) {
		const uint64_t now = (uint64_t)cpu->time64();
		/* 表が尽きるまで約 0.25s 毎に曲コマンドを再試行（Sys16/CPS 等）。After Burner／ピンしたプレイリストタイトル: 注入は 1 回だけ — 再送は BGM を再開し選択曲を試行表[0] で上書き。 */
		if (now >= nextCmdAt_) {
			if (hw_->board_ == CEMU_AC_BOARD_ABURNER) {
				if (cmdIndex_ == 0)
					TryInjectCommand();
				nextCmdAt_ = (uint64_t)~0ull;
			} else if (pinned_) {
				if (cmdIndex_ == 0) {
					TryInjectCommand();
					const int fast = (hw_->board_ == CEMU_AC_BOARD_IREM_M72
						|| hw_->board_ == CEMU_AC_BOARD_SYS16B
						|| hw_->board_ == CEMU_AC_BOARD_VSYSTEM
						|| hw_->board_ == CEMU_AC_BOARD_GNG
						|| (hw_->board_ == CEMU_AC_BOARD_KONAMI_PCM
							&& (hw_->PcmKind() == 3 || hw_->PcmKind() == 4))
						|| hw_->board_ == CEMU_AC_BOARD_TAITO_OPM
						|| hw_->board_ == CEMU_AC_BOARD_KONAMI_SCRAMBLE
						|| hw_->board_ == CEMU_AC_BOARD_KONAMI_TIMEPLT
						|| hw_->board_ == CEMU_AC_BOARD_KONAMI_GX400
						|| hw_->board_ == CEMU_AC_BOARD_TECHNOS_DDRAGON2
						|| hw_->board_ == CEMU_AC_BOARD_KONAMI_K7232
						|| hw_->board_ == CEMU_AC_BOARD_KONAMI_HCASTLE
						|| hw_->board_ == CEMU_AC_BOARD_ALPHA68K2
						|| hw_->board_ == CEMU_AC_BOARD_TECMO16
						|| hw_->board_ == CEMU_AC_BOARD_RAIZING
						|| hw_->board_ == CEMU_AC_BOARD_FLSTORY
						|| hw_->board_ == CEMU_AC_BOARD_TERRACRE
						|| hw_->board_ == CEMU_AC_BOARD_ROBOKID
						|| hw_->board_ == CEMU_AC_BOARD_BATTLANTIS);
					nextCmdAt_ = now + (uint64_t)cpuHz_ * (fast ? 1ull : 3ull)
						/ ((hw_->board_ == CEMU_AC_BOARD_IREM_M72
							|| hw_->board_ == CEMU_AC_BOARD_SYS16B
							|| hw_->board_ == CEMU_AC_BOARD_VSYSTEM) ? 2ull : 1ull);
				} else if (hw_->board_ == CEMU_AC_BOARD_FLSTORY) {
					/* MSM フレーズはプローブ 1 秒内に減衰 — 同じ BGM を再武装し 4 分類チャンク全て PEAK_MIN 超に保つ */
					cmdIndex_ = 0;
					TryInjectCommand();
					nextCmdAt_ = now + (uint64_t)cpuHz_ * 4ull / 5ull;
				} else if (hw_->board_ == CEMU_AC_BOARD_RAIZING) {
					/* 終端ターミネータに当たるジングル／スクリプトは無音。全チャネルが落ちたら同じタイトルを再投稿。ループ BGM はアイドルにならないので触らない。 */
					if (heard_ && hw_->RaizingHandshakeAcked()
						&& hw_->RaizingTrackIdle()) {
						cmdIndex_ = 0;
						TryInjectCommand();
					}
					nextCmdAt_ = now + (uint64_t)cpuHz_ / 4;
				} else if (hw_->board_ == CEMU_AC_BOARD_KONAMI_PCM
					&& (hw_->PcmKind() == 3 || hw_->PcmKind() == 4)) {
					/* K054539: ラッチを再 poke（DI が最初の IRQ を落とし得る）。K053260: 曲を数回投稿しラッチをクリア、そのあと IRQ0 だけパルスしてシーケンサを tick。 */
					if (hw_->PcmKind() == 3) {
						const uint16_t w = songCmdWord_ ? songCmdWord_
							: (uint16_t)(songCmd_ ? songCmd_ : 0x80);
						/* DI + FA00/HALT / YM タイマ待ち: 空 IRQ0 では復帰できない。同じ曲を約 2Hz で再武装し、短いフレーズ（qgakumon/vendetta）が無音プローブチャンクを残さないようにする。 */
						if ((cmdIndex_ % 30) == 0) {
							if (w > 0xffu)
								hw_->SetSoundCommandWord(w);
							else
								hw_->SetSoundCommand((uint8_t)w);
						} else {
							hw_->PulseIrq();
						}
						cmdIndex_++;
						nextCmdAt_ = now + (uint64_t)cpuHz_ / 60;
					} else if (cmdIndex_ < 40) {
						TryInjectCommand();
						nextCmdAt_ = now + (uint64_t)cpuHz_ / 4;
					} else {
						nextCmdAt_ = (uint64_t)~0ull;
					}
				} else if (hw_->board_ == CEMU_AC_BOARD_GNG) {
					/* 可聴になったら止める — 追加注入は本物 BGM を AUDITION/クリップへ踏み潰す。ピン無し Open() は既に持続を選んだ。 */
					if (!heard_ && cmdIndex_ < 40) {
						TryInjectCommand();
						nextCmdAt_ = now + (uint64_t)cpuHz_ / 4;
					} else {
						nextCmdAt_ = (uint64_t)~0ull;
					}
				} else {
					/* 選んだカタログ／プレイリストコマンドを残す。試行表をハントしない — BGM を最初の試行コードで再開した（sfa song2→song1、CPS/QSound/Taito 等も同じ型）。 */
					nextCmdAt_ = (uint64_t)~0ull;
				}
			} else if (cmdIndex_ == 0) {
				/* ピン無し: カタログ／既定を一度注入 — 試行表走査なし */
				TryInjectCommand();
				nextCmdAt_ = (uint64_t)~0ull;
			} else {
				nextCmdAt_ = (uint64_t)~0ull;
			}
		}
		if (irqPaceLive_ && hostRate_ > 0) {
			irqPaceAcc_ += 250;
			while (irqPaceAcc_ >= hostRate_) {
				irqPaceAcc_ -= hostRate_;
				if (irqPaceDue_ < 2)
					irqPaceDue_++;
			}
		}
		cpuAcc_ += (int64_t)cpuHz_;
		int cyclesPerSample = (int)(cpuAcc_ / (int64_t)hostRate_);
		cpuAcc_ %= (int64_t)hostRate_;
		if (cyclesPerSample < 1) cyclesPerSample = 1;
		const uint64_t end = now + (uint64_t)cyclesPerSample;
		while ((uint64_t)cpu->time64() < end) {
			DeliverIrqs();
			const int cycles = Ay_CpuRunOne(cpu);
			hw_->AddCpuCycles((uint64_t)cycles);
			TickOpm((uint64_t)cycles);
		}
		if (hw_->board_ == CEMU_AC_BOARD_CPS_QS) {
			/* QSound の符号付き 8bit ソース経路は FM 基板より明らかに小さい。基板ミキサで控えめ +1.9 dB。チップコア単独 Render 契約は変えない。 */
			stereo[i * 2] = stereo[i * 2 + 1] = 0;
			chip->MixAdd(stereo + i * 2, 1, 320);
		} else {
			chip->Render(stereo + i * 2, 1);
		}
		/* フレーム毎なので下の無音ウォッチドッグが PCM のみ基板も見る。MixAdd は全て素のフレームループなので等価。 */
		if (pcm) {
			/* CPS1: YM2151 は fmgen フルスケール。OKI 1ch も 12bit×16 で 16bit 一杯なので
			   gain 256 だとドラムで FM がクリップしてざらつく。
			   MAME は YM 0.35 / OKI 0.30。hoot pcm_mix 0x3c を MixAdd に生で入れると
			   （以前 /256 を外して溢れた）量子化と誤ゲインでノイズになるので使わない。
			   96/256 ≈ 0.375 は 1ch ピークが MAME 0.30 付近、2ch 同時でも FM の頭が残る。 */
			const int pcmGain = (hw_->board_ == CEMU_AC_BOARD_CPS1) ? 96 : 256;
			pcm->MixAdd(stereo + i * 2, 1, pcmGain);
		}
		if (pcm2)
			pcm2->MixAdd(stereo + i * 2, 1, 256);
		if (hw_->board_ == CEMU_AC_BOARD_ALPHA68K2)
			hw_->AlphaMixOpll(stereo + i * 2, 1);
		if (mix2)
			hw_->Chip2()->Render(mix2 + i * 2, 1);
		if (mix3)
			hw_->Chip3()->Render(mix3 + i * 2, 1);
		if (!heard_ && (stereo[i * 2] || stereo[i * 2 + 1]
			|| (mix2 && (mix2[i * 2] || mix2[i * 2 + 1]))
			|| (mix3 && (mix3[i * 2] || mix3[i * 2 + 1]))))
			heard_ = 1;
	}

	if (mix2 && hw_->board_ == CEMU_AC_BOARD_GNG) {
		for (int i = 0; i < frames * 2; i++) {
			/* YM2203×2 — 平均してハードクリップを避ける */
			int s = ((int)stereo[i] + (int)mix2[i]) / 2;
			if (s > 32767) s = 32767;
			if (s < -32768) s = -32768;
			stereo[i] = (int16_t)s;
		}
	} else if (mix2) {
		/* SN76489 / AY ボイスは個別に静か。クランプ加算し、生きているチップ 1 本がフルレベルを保つ（MAME は各約 0.5 でルート）。 */
		for (int i = 0; i < frames * 2; i++) {
			int s = (int)stereo[i] + (int)mix2[i] + (mix3 ? (int)mix3[i] : 0);
			s = s * 2 / 3;
			if (s > 32767) s = 32767;
			if (s < -32768) s = -32768;
			stereo[i] = (int16_t)s;
		}
	}
	return frames;
}

int CDriverAc::Seek(uint64_t sample)
{
	(void)sample;
	return 0;
}
